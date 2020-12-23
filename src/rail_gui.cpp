/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file rail_gui.cpp %File for dealing with rail construction user interface */

#include "stdafx.h"
#include "gui.h"
#include "window_gui.h"
#include "station_gui.h"
#include "terraform_gui.h"
#include "viewport_func.h"
#include "command_func.h"
#include "waypoint_func.h"
#include "newgrf_station.h"
#include "company_base.h"
#include "strings_func.h"
#include "window_func.h"
#include "date_func.h"
#include "sound_func.h"
#include "company_func.h"
#include "widgets/dropdown_type.h"
#include "tunnelbridge.h"
#include "tilehighlight_func.h"
#include "spritecache.h"
#include "core/geometry_func.hpp"
#include "hotkeys.h"
#include "engine_base.h"
#include "vehicle_func.h"
#include "zoom_func.h"
#include "rail_gui.h"
#include "querystring_gui.h"
#include "sortlist_type.h"
#include "stringfilter_type.h"
#include "string_func.h"
#include "depot_func.h"

#include "station_map.h"
#include "tunnelbridge_map.h"

#include "widgets/rail_widget.h"

#include "safeguards.h"


static RailType _cur_railtype;               ///< Rail type of the current build-rail toolbar.
static bool _remove_button_clicked;          ///< Flag whether 'remove' toggle-button is currently enabled
static DiagDirection _build_depot_direction; ///< Currently selected depot direction
static byte _waypoint_count = 1;             ///< Number of waypoint types
static byte _cur_waypoint_type;              ///< Currently selected waypoint type
static bool _convert_signal_button;          ///< convert signal button in the signal GUI pressed
static SignalVariant _cur_signal_variant;    ///< set the signal variant (for signal GUI)
static SignalType _cur_signal_type;          ///< set the signal type (for signal GUI)

/* Map the setting: default_signal_type to the corresponding signal type */
static const SignalType _default_signal_type[] = {SIGTYPE_NORMAL, SIGTYPE_PBS, SIGTYPE_PBS_ONEWAY};

struct RailStationGUISettings {
	Axis orientation;                 ///< Currently selected rail station orientation

	bool newstations;                 ///< Are custom station definitions available?
	StationClassID station_class;     ///< Currently selected custom station class (if newstations is \c true )
	byte station_type;                ///< %Station type within the currently selected custom station class (if newstations is \c true )
	byte station_count;               ///< Number of custom stations (if newstations is \c true )
};
static RailStationGUISettings _railstation; ///< Settings of the station builder GUI


static void HandleStationPlacement(TileIndex start, TileIndex end);
static void ShowBuildTrainDepotPicker(Window *parent);
static void ShowBuildWaypointPicker(Window *parent);
static Window *ShowStationBuilder(Window *parent);
static void ShowSignalBuilder(Window *parent);

/**
 * Check whether a station type can be build.
 * @return true if building is allowed.
 */
static bool IsStationAvailable(const StationSpec *statspec)
{
	if (statspec == nullptr || !HasBit(statspec->callback_mask, CBM_STATION_AVAIL)) return true;

	uint16 cb_res = GetStationCallback(CBID_STATION_AVAILABILITY, 0, 0, statspec, nullptr, INVALID_TILE);
	if (cb_res == CALLBACK_FAILED) return true;

	return Convert8bitBooleanCallback(statspec->grf_prop.grffile, CBID_STATION_AVAILABILITY, cb_res);
}

void CcPlaySound_CONSTRUCTION_RAIL(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2, uint32 cmd)
{
	if (result.Succeeded() && _settings_client.sound.confirm) SndPlayTileFx(SND_20_CONSTRUCTION_RAIL, tile);
}

static void GenericPlaceRail(TileIndex tile, int cmd)
{
	DoCommandP(tile, _cur_railtype, cmd | (_settings_client.gui.auto_remove_signals << 3),
			_remove_button_clicked ?
			CMD_REMOVE_SINGLE_RAIL | CMD_MSG(STR_ERROR_CAN_T_REMOVE_RAILROAD_TRACK) :
			CMD_BUILD_SINGLE_RAIL | CMD_MSG(STR_ERROR_CAN_T_BUILD_RAILROAD_TRACK),
			CcPlaySound_CONSTRUCTION_RAIL);
}

/**
 * Try to add an additional rail-track at the entrance of a depot
 * @param tile  Tile to use for adding the rail-track
 * @param dir   Direction to check for already present tracks
 * @param track Track to add
 * @see CcRailDepot()
 */
static void PlaceExtraDepotRail(TileIndex tile, DiagDirection dir, Track track)
{
	if (GetRailTileType(tile) == RAIL_TILE_DEPOT) return;
	if (GetRailTileType(tile) == RAIL_TILE_SIGNALS && !_settings_client.gui.auto_remove_signals) return;
	if ((GetTrackBits(tile) & DiagdirReachesTracks(dir)) == 0) return;

	DoCommandP(tile, _cur_railtype, track | (_settings_client.gui.auto_remove_signals << 3), CMD_BUILD_SINGLE_RAIL);
}

/** Additional pieces of track to add at the entrance of a depot. */
static const Track _place_depot_extra_track[12] = {
	TRACK_LEFT,  TRACK_UPPER, TRACK_UPPER, TRACK_RIGHT, // First additional track for directions 0..3
	TRACK_X,     TRACK_Y,     TRACK_X,     TRACK_Y,     // Second additional track
	TRACK_LOWER, TRACK_LEFT,  TRACK_RIGHT, TRACK_LOWER, // Third additional track
};

/** Direction to check for existing track pieces. */
static const DiagDirection _place_depot_extra_dir[12] = {
	DIAGDIR_SE, DIAGDIR_SW, DIAGDIR_SE, DIAGDIR_SW,
	DIAGDIR_SW, DIAGDIR_NW, DIAGDIR_NE, DIAGDIR_SE,
	DIAGDIR_NW, DIAGDIR_NE, DIAGDIR_NW, DIAGDIR_NE,
};

void CcRailDepot(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2, uint32 cmd)
{
	if (result.Failed()) return;

	DiagDirection dir = (DiagDirection)GB(p1, 6, 2);

	if (_settings_client.sound.confirm) SndPlayTileFx(SND_20_CONSTRUCTION_RAIL, tile);
	if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();

	tile += TileOffsByDiagDir(dir);

	if (IsTileType(tile, MP_RAILWAY)) {
		PlaceExtraDepotRail(tile, _place_depot_extra_dir[dir], _place_depot_extra_track[dir]);
		PlaceExtraDepotRail(tile, _place_depot_extra_dir[dir + 4], _place_depot_extra_track[dir + 4]);
		PlaceExtraDepotRail(tile, _place_depot_extra_dir[dir + 8], _place_depot_extra_track[dir + 8]);
	}
}

/**
 * Place a rail waypoint.
 * @param tile Position to start dragging a waypoint.
 */
static void PlaceRail_Waypoint(TileIndex tile)
{
	if (_remove_button_clicked) {
		VpStartPlaceSizing(tile, VPM_X_AND_Y, DDSP_REMOVE_STATION);
		return;
	}

	Axis axis = GetAxisForNewWaypoint(tile);
	if (IsValidAxis(axis)) {
		/* Valid tile for waypoints */
		VpStartPlaceSizing(tile, axis == AXIS_X ? VPM_X_LIMITED : VPM_Y_LIMITED, DDSP_BUILD_STATION);
		VpSetPlaceSizingLimit(_settings_game.station.station_spread);
	} else {
		/* Tile where we can't build rail waypoints. This is always going to fail,
		 * but provides the user with a proper error message. */
		DoCommandP(tile, 1 << 8 | 1 << 16, STAT_CLASS_WAYP | INVALID_STATION << 16, CMD_BUILD_RAIL_WAYPOINT | CMD_MSG(STR_ERROR_CAN_T_BUILD_TRAIN_WAYPOINT));
	}
}

void CcStation(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2, uint32 cmd)
{
	if (result.Failed()) return;

	if (_settings_client.sound.confirm) SndPlayTileFx(SND_20_CONSTRUCTION_RAIL, tile);
	/* Only close the station builder window if the default station and non persistent building is chosen. */
	if (_railstation.station_class == STAT_CLASS_DFLT && _railstation.station_type == 0 && !_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();
}

/**
 * Place a rail station.
 * @param tile Position to place or start dragging a station.
 */
static void PlaceRail_Station(TileIndex tile)
{
	if (_remove_button_clicked) {
		VpStartPlaceSizing(tile, VPM_X_AND_Y_LIMITED, DDSP_REMOVE_STATION);
		VpSetPlaceSizingLimit(-1);
	} else if (_settings_client.gui.station_dragdrop) {
		VpStartPlaceSizing(tile, VPM_X_AND_Y_LIMITED, DDSP_BUILD_STATION);
		VpSetPlaceSizingLimit(_settings_game.station.station_spread);
	} else {
		uint32 p1 = _cur_railtype | _railstation.orientation << 6 | _settings_client.gui.station_numtracks << 8 | _settings_client.gui.station_platlength << 16 | _ctrl_pressed << 24;
		uint32 p2 = _railstation.station_class | _railstation.station_type << 8 | INVALID_STATION << 16;

		int w = _settings_client.gui.station_numtracks;
		int h = _settings_client.gui.station_platlength;
		if (!_railstation.orientation) Swap(w, h);

		CommandContainer cmdcont = { tile, p1, p2, CMD_BUILD_RAIL_STATION | CMD_MSG(STR_ERROR_CAN_T_BUILD_RAILROAD_STATION), CcStation, "" };
		ShowSelectStationIfNeeded(cmdcont, TileArea(tile, w, h));
	}
}

/**
 * Build a new signal or edit/remove a present signal, use CmdBuildSingleSignal() or CmdRemoveSingleSignal() in rail_cmd.cpp
 *
 * @param tile The tile where the signal will build or edit
 */
static void GenericPlaceSignals(TileIndex tile)
{
	TrackBits trackbits = TrackStatusToTrackBits(GetTileTrackStatus(tile, TRANSPORT_RAIL, 0));

	if (trackbits & TRACK_BIT_VERT) { // N-S direction
		trackbits = (_tile_fract_coords.x <= _tile_fract_coords.y) ? TRACK_BIT_RIGHT : TRACK_BIT_LEFT;
	}

	if (trackbits & TRACK_BIT_HORZ) { // E-W direction
		trackbits = (_tile_fract_coords.x + _tile_fract_coords.y <= 15) ? TRACK_BIT_UPPER : TRACK_BIT_LOWER;
	}

	Track track = FindFirstTrack(trackbits);

	if (_remove_button_clicked) {
		DoCommandP(tile, track, 0, CMD_REMOVE_SIGNALS | CMD_MSG(STR_ERROR_CAN_T_REMOVE_SIGNALS_FROM), CcPlaySound_CONSTRUCTION_RAIL);
	} else {
		const Window *w = FindWindowById(WC_BUILD_SIGNAL, 0);

		/* Map the setting cycle_signal_types to the lower and upper allowed signal type. */
		static const uint cycle_bounds[] = {SIGTYPE_NORMAL | (SIGTYPE_LAST_NOPBS << 3), SIGTYPE_PBS | (SIGTYPE_LAST << 3), SIGTYPE_NORMAL | (SIGTYPE_LAST << 3)};

		/* various bitstuffed elements for CmdBuildSingleSignal() */
		uint32 p1 = track;

		if (w != nullptr) {
			/* signal GUI is used */
			SB(p1, 3, 1, _ctrl_pressed);
			SB(p1, 4, 1, _cur_signal_variant);
			SB(p1, 5, 3, _cur_signal_type);
			SB(p1, 8, 1, _convert_signal_button);
			SB(p1, 9, 6, cycle_bounds[_settings_client.gui.cycle_signal_types]);
		} else {
			SB(p1, 3, 1, _ctrl_pressed);
			SB(p1, 4, 1, (_cur_year < _settings_client.gui.semaphore_build_before ? SIG_SEMAPHORE : SIG_ELECTRIC));
			SB(p1, 5, 3, _default_signal_type[_settings_client.gui.default_signal_type]);
			SB(p1, 8, 1, 0);
			SB(p1, 9, 6, cycle_bounds[_settings_client.gui.cycle_signal_types]);
		}

		DoCommandP(tile, p1, 0, CMD_BUILD_SIGNALS |
				CMD_MSG((w != nullptr && _convert_signal_button) ? STR_ERROR_SIGNAL_CAN_T_CONVERT_SIGNALS_HERE : STR_ERROR_CAN_T_BUILD_SIGNALS_HERE),
				CcPlaySound_CONSTRUCTION_RAIL);
	}
}

/**
 * Start placing a rail bridge.
 * @param tile Position of the first tile of the bridge.
 * @param w    Rail toolbar window.
 */
static void PlaceRail_Bridge(TileIndex tile, Window *w)
{
	if (IsBridgeTile(tile)) {
		TileIndex other_tile = GetOtherTunnelBridgeEnd(tile);
		Point pt = {0, 0};
		w->OnPlaceMouseUp(VPM_X_OR_Y, DDSP_BUILD_BRIDGE, pt, other_tile, tile);
	} else {
		VpStartPlaceSizing(tile, VPM_X_OR_Y, DDSP_BUILD_BRIDGE);
	}
}

/** Command callback for building a tunnel */
void CcBuildRailTunnel(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2, uint32 cmd)
{
	if (result.Succeeded()) {
		if (_settings_client.sound.confirm) SndPlayTileFx(SND_20_CONSTRUCTION_RAIL, tile);
		if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();
	} else {
		SetRedErrorSquare(_build_tunnel_endtile);
	}
}

/**
 * Toggles state of the Remove button of Build rail toolbar
 * @param w window the button belongs to
 */
static void ToggleRailButton_Remove(Window *w)
{
	CloseWindowById(WC_SELECT_STATION, 0);
	w->ToggleWidgetLoweredState(WID_RAT_REMOVE);
	w->SetWidgetDirty(WID_RAT_REMOVE);
	_remove_button_clicked = w->IsWidgetLowered(WID_RAT_REMOVE);
	SetSelectionRed(_remove_button_clicked);
}

/**
 * Updates the Remove button because of Ctrl state change
 * @param w window the button belongs to
 * @return true iff the remove button was changed
 */
static bool RailToolbar_CtrlChanged(Window *w)
{
	if (w->IsWidgetDisabled(WID_RAT_REMOVE)) return false;

	/* allow ctrl to switch remove mode only for these widgets */
	for (uint i = WID_RAT_BUILD_NS; i <= WID_RAT_BUILD_STATION; i++) {
		if ((i <= WID_RAT_AUTORAIL || i >= WID_RAT_BUILD_WAYPOINT) && w->IsWidgetLowered(i)) {
			ToggleRailButton_Remove(w);
			return true;
		}
	}

	return false;
}


/**
 * The "remove"-button click proc of the build-rail toolbar.
 * @param w Build-rail toolbar window
 * @see BuildRailToolbarWindow::OnClick()
 */
static void BuildRailClick_Remove(Window *w)
{
	if (w->IsWidgetDisabled(WID_RAT_REMOVE)) return;
	ToggleRailButton_Remove(w);
	if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);

	/* handle station builder */
	if (w->IsWidgetLowered(WID_RAT_BUILD_STATION)) {
		if (_remove_button_clicked) {
			/* starting drag & drop remove */
			if (!_settings_client.gui.station_dragdrop) {
				SetTileSelectSize(1, 1);
			} else {
				VpSetPlaceSizingLimit(-1);
			}
		} else {
			/* starting station build mode */
			if (!_settings_client.gui.station_dragdrop) {
				int x = _settings_client.gui.station_numtracks;
				int y = _settings_client.gui.station_platlength;
				if (_railstation.orientation == 0) Swap(x, y);
				SetTileSelectSize(x, y);
			} else {
				VpSetPlaceSizingLimit(_settings_game.station.station_spread);
			}
		}
	}
}

static void DoRailroadTrack(int mode)
{
	uint32 p2 = _cur_railtype | (mode << 6) | (_settings_client.gui.auto_remove_signals << 11);
	DoCommandP(TileVirtXY(_thd.selstart.x, _thd.selstart.y), TileVirtXY(_thd.selend.x, _thd.selend.y), p2,
			_remove_button_clicked ?
			CMD_REMOVE_RAILROAD_TRACK | CMD_MSG(STR_ERROR_CAN_T_REMOVE_RAILROAD_TRACK) :
			CMD_BUILD_RAILROAD_TRACK  | CMD_MSG(STR_ERROR_CAN_T_BUILD_RAILROAD_TRACK),
			CcPlaySound_CONSTRUCTION_RAIL);
}

static void HandleAutodirPlacement()
{
	int trackstat = _thd.drawstyle & HT_DIR_MASK; // 0..5

	if (_thd.drawstyle & HT_RAIL) { // one tile case
		GenericPlaceRail(TileVirtXY(_thd.selend.x, _thd.selend.y), trackstat);
		return;
	}

	DoRailroadTrack(trackstat);
}

/**
 * Build new signals or remove signals or (if only one tile marked) edit a signal.
 *
 * If one tile marked abort and use GenericPlaceSignals()
 * else use CmdBuildSingleSignal() or CmdRemoveSingleSignal() in rail_cmd.cpp to build many signals
 */
static void HandleAutoSignalPlacement()
{
	uint32 p2 = GB(_thd.drawstyle, 0, 3); // 0..5

	if ((_thd.drawstyle & HT_DRAG_MASK) == HT_RECT) { // one tile case
		GenericPlaceSignals(TileVirtXY(_thd.selend.x, _thd.selend.y));
		return;
	}

	const Window *w = FindWindowById(WC_BUILD_SIGNAL, 0);

	if (w != nullptr) {
		/* signal GUI is used */
		SB(p2,  3, 1, 0);
		SB(p2,  4, 1, _cur_signal_variant);
		SB(p2,  6, 1, _ctrl_pressed);
		SB(p2,  7, 3, _cur_signal_type);
		SB(p2, 24, 8, _settings_client.gui.drag_signals_density);
		SB(p2, 10, 1, !_settings_client.gui.drag_signals_fixed_distance);
	} else {
		SB(p2,  3, 1, 0);
		SB(p2,  4, 1, (_cur_year < _settings_client.gui.semaphore_build_before ? SIG_SEMAPHORE : SIG_ELECTRIC));
		SB(p2,  6, 1, _ctrl_pressed);
		SB(p2,  7, 3, _default_signal_type[_settings_client.gui.default_signal_type]);
		SB(p2, 24, 8, _settings_client.gui.drag_signals_density);
		SB(p2, 10, 1, !_settings_client.gui.drag_signals_fixed_distance);
	}

	/* _settings_client.gui.drag_signals_density is given as a parameter such that each user
	 * in a network game can specify their own signal density */
	DoCommandP(TileVirtXY(_thd.selstart.x, _thd.selstart.y), TileVirtXY(_thd.selend.x, _thd.selend.y), p2,
			_remove_button_clicked ?
			CMD_REMOVE_SIGNAL_TRACK | CMD_MSG(STR_ERROR_CAN_T_REMOVE_SIGNALS_FROM) :
			CMD_BUILD_SIGNAL_TRACK  | CMD_MSG(STR_ERROR_CAN_T_BUILD_SIGNALS_HERE),
			CcPlaySound_CONSTRUCTION_RAIL);
}


/** Rail toolbar management class. */
struct BuildRailToolbarWindow : Window {
	RailType railtype;    ///< Rail type to build.
	int last_user_action; ///< Last started user action.

	BuildRailToolbarWindow(WindowDesc *desc, RailType railtype) : Window(desc)
	{
		this->InitNested(TRANSPORT_RAIL);
		this->SetupRailToolbar(railtype);
		this->DisableWidget(WID_RAT_REMOVE);
		this->last_user_action = WIDGET_LIST_END;

		if (_settings_client.gui.link_terraform_toolbar) ShowTerraformToolbar(this);
	}

	void Close() override
	{
		if (this->IsWidgetLowered(WID_RAT_BUILD_STATION)) SetViewportCatchmentStation(nullptr, true);
		if (_settings_client.gui.link_terraform_toolbar) CloseWindowById(WC_SCEN_LAND_GEN, 0, false);
		if (this->IsWidgetLowered(WID_RAT_BUILD_DEPOT)) SetViewportHighlightDepot(INVALID_DEPOT, true);
		this->Window::Close();
	}

	/**
	 * Configures the rail toolbar for railtype given
	 * @param railtype the railtype to display
	 */
	void SetupRailToolbar(RailType railtype)
	{
		this->railtype = railtype;
		const RailtypeInfo *rti = GetRailTypeInfo(railtype);

		assert(railtype < RAILTYPE_END);
		this->GetWidget<NWidgetCore>(WID_RAT_BUILD_NS)->widget_data     = rti->gui_sprites.build_ns_rail;
		this->GetWidget<NWidgetCore>(WID_RAT_BUILD_X)->widget_data      = rti->gui_sprites.build_x_rail;
		this->GetWidget<NWidgetCore>(WID_RAT_BUILD_EW)->widget_data     = rti->gui_sprites.build_ew_rail;
		this->GetWidget<NWidgetCore>(WID_RAT_BUILD_Y)->widget_data      = rti->gui_sprites.build_y_rail;
		this->GetWidget<NWidgetCore>(WID_RAT_AUTORAIL)->widget_data     = rti->gui_sprites.auto_rail;
		this->GetWidget<NWidgetCore>(WID_RAT_BUILD_DEPOT)->widget_data  = rti->gui_sprites.build_depot;
		this->GetWidget<NWidgetCore>(WID_RAT_CONVERT_RAIL)->widget_data = rti->gui_sprites.convert_rail;
		this->GetWidget<NWidgetCore>(WID_RAT_BUILD_TUNNEL)->widget_data = rti->gui_sprites.build_tunnel;
	}

	/**
	 * Switch to another rail type.
	 * @param railtype New rail type.
	 */
	void ModifyRailType(RailType railtype)
	{
		this->SetupRailToolbar(railtype);
		this->ReInit();
	}

	void UpdateRemoveWidgetStatus(int clicked_widget)
	{
		switch (clicked_widget) {
			case WID_RAT_REMOVE:
				/* If it is the removal button that has been clicked, do nothing,
				 * as it is up to the other buttons to drive removal status */
				return;

			case WID_RAT_BUILD_NS:
			case WID_RAT_BUILD_X:
			case WID_RAT_BUILD_EW:
			case WID_RAT_BUILD_Y:
			case WID_RAT_AUTORAIL:
			case WID_RAT_BUILD_WAYPOINT:
			case WID_RAT_BUILD_STATION:
			case WID_RAT_BUILD_SIGNALS:
				/* Removal button is enabled only if the rail/signal/waypoint/station
				 * button is still lowered.  Once raised, it has to be disabled */
				this->SetWidgetDisabledState(WID_RAT_REMOVE, !this->IsWidgetLowered(clicked_widget));
				break;

			default:
				/* When any other buttons than rail/signal/waypoint/station, raise and
				 * disable the removal button */
				this->DisableWidget(WID_RAT_REMOVE);
				this->RaiseWidget(WID_RAT_REMOVE);
				break;
		}
	}

	void SetStringParameters(int widget) const override
	{
		if (widget == WID_RAT_CAPTION) {
			const RailtypeInfo *rti = GetRailTypeInfo(this->railtype);
			if (rti->max_speed > 0) {
				SetDParam(0, STR_TOOLBAR_RAILTYPE_VELOCITY);
				SetDParam(1, rti->strings.toolbar_caption);
				SetDParam(2, rti->max_speed);
			} else {
				SetDParam(0, rti->strings.toolbar_caption);
			}
		}
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		if (widget < WID_RAT_BUILD_NS) return;

		_remove_button_clicked = false;
		switch (widget) {
			case WID_RAT_BUILD_NS:
				HandlePlacePushButton(this, WID_RAT_BUILD_NS, GetRailTypeInfo(_cur_railtype)->cursor.rail_ns, HT_LINE | HT_DIR_VL);
				this->last_user_action = widget;
				break;

			case WID_RAT_BUILD_X:
				HandlePlacePushButton(this, WID_RAT_BUILD_X, GetRailTypeInfo(_cur_railtype)->cursor.rail_swne, HT_LINE | HT_DIR_X);
				this->last_user_action = widget;
				break;

			case WID_RAT_BUILD_EW:
				HandlePlacePushButton(this, WID_RAT_BUILD_EW, GetRailTypeInfo(_cur_railtype)->cursor.rail_ew, HT_LINE | HT_DIR_HL);
				this->last_user_action = widget;
				break;

			case WID_RAT_BUILD_Y:
				HandlePlacePushButton(this, WID_RAT_BUILD_Y, GetRailTypeInfo(_cur_railtype)->cursor.rail_nwse, HT_LINE | HT_DIR_Y);
				this->last_user_action = widget;
				break;

			case WID_RAT_AUTORAIL:
				HandlePlacePushButton(this, WID_RAT_AUTORAIL, GetRailTypeInfo(_cur_railtype)->cursor.autorail, HT_RAIL);
				this->last_user_action = widget;
				break;

			case WID_RAT_DEMOLISH:
				HandlePlacePushButton(this, WID_RAT_DEMOLISH, ANIMCURSOR_DEMOLISH, HT_RECT | HT_DIAGONAL);
				this->last_user_action = widget;
				break;

			case WID_RAT_BUILD_DEPOT:
				if (HandlePlacePushButton(this, WID_RAT_BUILD_DEPOT, GetRailTypeInfo(_cur_railtype)->cursor.depot, HT_RECT)) {
					ShowBuildTrainDepotPicker(this);
					this->last_user_action = widget;
				}
				break;

			case WID_RAT_BUILD_WAYPOINT:
				this->last_user_action = widget;
				_waypoint_count = StationClass::Get(STAT_CLASS_WAYP)->GetSpecCount();
				if (HandlePlacePushButton(this, WID_RAT_BUILD_WAYPOINT, SPR_CURSOR_WAYPOINT, HT_RECT) && _waypoint_count > 1) {
					ShowBuildWaypointPicker(this);
				}
				break;

			case WID_RAT_BUILD_STATION:
				if (HandlePlacePushButton(this, WID_RAT_BUILD_STATION, SPR_CURSOR_RAIL_STATION, HT_RECT)) {
					ShowStationBuilder(this);
					this->last_user_action = widget;
				}
				break;

			case WID_RAT_BUILD_SIGNALS: {
				this->last_user_action = widget;
				bool started = HandlePlacePushButton(this, WID_RAT_BUILD_SIGNALS, ANIMCURSOR_BUILDSIGNALS, HT_RECT);
				if (started && _settings_client.gui.enable_signal_gui != _ctrl_pressed) {
					ShowSignalBuilder(this);
				}
				break;
			}

			case WID_RAT_BUILD_BRIDGE:
				HandlePlacePushButton(this, WID_RAT_BUILD_BRIDGE, SPR_CURSOR_BRIDGE, HT_RECT);
				this->last_user_action = widget;
				break;

			case WID_RAT_BUILD_TUNNEL:
				HandlePlacePushButton(this, WID_RAT_BUILD_TUNNEL, GetRailTypeInfo(_cur_railtype)->cursor.tunnel, HT_SPECIAL);
				this->last_user_action = widget;
				break;

			case WID_RAT_REMOVE:
				BuildRailClick_Remove(this);
				break;

			case WID_RAT_CONVERT_RAIL:
				HandlePlacePushButton(this, WID_RAT_CONVERT_RAIL, GetRailTypeInfo(_cur_railtype)->cursor.convert, HT_RECT | HT_DIAGONAL);
				this->last_user_action = widget;
				break;

			default: NOT_REACHED();
		}
		this->UpdateRemoveWidgetStatus(widget);
		if (_ctrl_pressed) RailToolbar_CtrlChanged(this);
	}

	EventState OnHotkey(int hotkey) override
	{
		MarkTileDirtyByTile(TileVirtXY(_thd.pos.x, _thd.pos.y)); // redraw tile selection
		return Window::OnHotkey(hotkey);
	}

	void OnPlaceObject(Point pt, TileIndex tile) override
	{
		switch (this->last_user_action) {
			case WID_RAT_BUILD_NS:
				VpStartPlaceSizing(tile, VPM_FIX_VERTICAL | VPM_RAILDIRS, DDSP_PLACE_RAIL);
				break;

			case WID_RAT_BUILD_X:
				VpStartPlaceSizing(tile, VPM_FIX_Y | VPM_RAILDIRS, DDSP_PLACE_RAIL);
				break;

			case WID_RAT_BUILD_EW:
				VpStartPlaceSizing(tile, VPM_FIX_HORIZONTAL | VPM_RAILDIRS, DDSP_PLACE_RAIL);
				break;

			case WID_RAT_BUILD_Y:
				VpStartPlaceSizing(tile, VPM_FIX_X | VPM_RAILDIRS, DDSP_PLACE_RAIL);
				break;

			case WID_RAT_AUTORAIL:
				VpStartPlaceSizing(tile, VPM_RAILDIRS, DDSP_PLACE_RAIL);
				break;

			case WID_RAT_DEMOLISH:
				PlaceProc_DemolishArea(tile);
				break;

			case WID_RAT_BUILD_DEPOT:
				DoCommandP(tile, (uint32)(_cur_railtype | (_build_depot_direction << 6), 0,
						CMD_BUILD_TRAIN_DEPOT | CMD_MSG(STR_ERROR_CAN_T_BUILD_TRAIN_DEPOT),
						CcRailDepot);
				break;

			case WID_RAT_BUILD_WAYPOINT:
				PlaceRail_Waypoint(tile);
				break;

			case WID_RAT_BUILD_STATION:
				PlaceRail_Station(tile);
				break;

			case WID_RAT_BUILD_SIGNALS:
				VpStartPlaceSizing(tile, VPM_SIGNALDIRS, DDSP_BUILD_SIGNALS);
				break;

			case WID_RAT_BUILD_BRIDGE:
				PlaceRail_Bridge(tile, this);
				break;

			case WID_RAT_BUILD_TUNNEL:
				DoCommandP(tile, _cur_railtype | (TRANSPORT_RAIL << 8), 0, CMD_BUILD_TUNNEL | CMD_MSG(STR_ERROR_CAN_T_BUILD_TUNNEL_HERE), CcBuildRailTunnel);
				break;

			case WID_RAT_CONVERT_RAIL:
				VpStartPlaceSizing(tile, VPM_X_AND_Y, DDSP_CONVERT_RAIL);
				break;

			default: NOT_REACHED();
		}
	}

	void OnPlaceDrag(ViewportPlaceMethod select_method, ViewportDragDropSelectionProcess select_proc, Point pt) override
	{
		/* no dragging if you have pressed the convert button */
		if (FindWindowById(WC_BUILD_SIGNAL, 0) != nullptr && _convert_signal_button && this->IsWidgetLowered(WID_RAT_BUILD_SIGNALS)) return;

		VpSelectTilesWithMethod(pt.x, pt.y, select_method);
	}

	void OnPlaceMouseUp(ViewportPlaceMethod select_method, ViewportDragDropSelectionProcess select_proc, Point pt, TileIndex start_tile, TileIndex end_tile) override
	{
		if (pt.x != -1) {
			switch (select_proc) {
				default: NOT_REACHED();
				case DDSP_BUILD_BRIDGE:
					if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();
					ShowBuildBridgeWindow(start_tile, end_tile, TRANSPORT_RAIL, _cur_railtype);
					break;

				case DDSP_PLACE_RAIL:
					HandleAutodirPlacement();
					break;

				case DDSP_BUILD_SIGNALS:
					HandleAutoSignalPlacement();
					break;

				case DDSP_DEMOLISH_AREA:
					GUIPlaceProcDragXY(select_proc, start_tile, end_tile);
					break;

				case DDSP_CONVERT_RAIL:
					DoCommandP(end_tile, start_tile, _cur_railtype | (_ctrl_pressed ? 1 << 6 : 0), CMD_CONVERT_RAIL | CMD_MSG(STR_ERROR_CAN_T_CONVERT_RAIL), CcPlaySound_CONSTRUCTION_RAIL);
					break;

				case DDSP_REMOVE_STATION:
				case DDSP_BUILD_STATION:
					if (this->IsWidgetLowered(WID_RAT_BUILD_STATION)) {
						/* Station */
						if (_remove_button_clicked) {
							DoCommandP(end_tile, start_tile, _ctrl_pressed ? 0 : 1, CMD_REMOVE_FROM_RAIL_STATION | CMD_MSG(STR_ERROR_CAN_T_REMOVE_PART_OF_STATION), CcPlaySound_CONSTRUCTION_RAIL);
						} else {
							HandleStationPlacement(start_tile, end_tile);
						}
					} else {
						/* Waypoint */
						if (_remove_button_clicked) {
							DoCommandP(end_tile, start_tile, _ctrl_pressed ? 0 : 1, CMD_REMOVE_FROM_RAIL_WAYPOINT | CMD_MSG(STR_ERROR_CAN_T_REMOVE_TRAIN_WAYPOINT), CcPlaySound_CONSTRUCTION_RAIL);
						} else {
							TileArea ta(start_tile, end_tile);
							uint32 p1 = _cur_railtype | (select_method == VPM_X_LIMITED ? AXIS_X : AXIS_Y) << 6 | ta.w << 8 | ta.h << 16 | _ctrl_pressed << 24;
							uint32 p2 = STAT_CLASS_WAYP | _cur_waypoint_type << 8 | INVALID_STATION << 16;

							CommandContainer cmdcont = { ta.tile, p1, p2, CMD_BUILD_RAIL_WAYPOINT | CMD_MSG(STR_ERROR_CAN_T_BUILD_TRAIN_WAYPOINT), CcPlaySound_CONSTRUCTION_RAIL, "" };
							ShowSelectWaypointIfNeeded(cmdcont, ta);
						}
					}
					break;
			}
		}
	}

	void OnPlaceObjectAbort() override
	{
		if (this->IsWidgetLowered(WID_RAT_BUILD_STATION)) SetViewportCatchmentStation(nullptr, true);

		if (this->IsWidgetLowered(WID_RAT_BUILD_DEPOT)) SetViewportHighlightDepot(INVALID_DEPOT, true);

		this->RaiseButtons();
		this->DisableWidget(WID_RAT_REMOVE);
		this->SetWidgetDirty(WID_RAT_REMOVE);

		CloseWindowById(WC_BUILD_SIGNAL, TRANSPORT_RAIL);
		CloseWindowById(WC_BUILD_STATION, TRANSPORT_RAIL);
		CloseWindowById(WC_BUILD_DEPOT, TRANSPORT_RAIL);
		CloseWindowById(WC_BUILD_WAYPOINT, TRANSPORT_RAIL);
		CloseWindowById(WC_SELECT_STATION, 0);
		CloseWindowById(WC_SELECT_DEPOT, VEH_TRAIN);
		CloseWindowByClass(WC_BUILD_BRIDGE);
	}

	void OnPlacePresize(Point pt, TileIndex tile) override
	{
		DoCommand(tile, _cur_railtype | (TRANSPORT_RAIL << 8), 0, DC_AUTO, CMD_BUILD_TUNNEL);
		VpSetPresizeRange(tile, _build_tunnel_endtile == 0 ? tile : _build_tunnel_endtile);
	}

	EventState OnCTRLStateChange() override
	{
		/* do not toggle Remove button by Ctrl when placing station */
		if (!this->IsWidgetLowered(WID_RAT_BUILD_STATION) && !this->IsWidgetLowered(WID_RAT_BUILD_WAYPOINT) && RailToolbar_CtrlChanged(this)) return ES_HANDLED;
		return ES_NOT_HANDLED;
	}

	static HotkeyList hotkeys;
};

/**
 * Handler for global hotkeys of the BuildRailToolbarWindow.
 * @param hotkey Hotkey
 * @return ES_HANDLED if hotkey was accepted.
 */
static EventState RailToolbarGlobalHotkeys(int hotkey)
{
	if (_game_mode != GM_NORMAL) return ES_NOT_HANDLED;
	extern RailType _last_built_railtype;
	Window *w = ShowBuildRailToolbar(_last_built_railtype);
	if (w == nullptr) return ES_NOT_HANDLED;
	return w->OnHotkey(hotkey);
}

const uint16 _railtoolbar_autorail_keys[] = {'5', 'A' | WKC_GLOBAL_HOTKEY, 0};

static Hotkey railtoolbar_hotkeys[] = {
	Hotkey('1', "build_ns", WID_RAT_BUILD_NS),
	Hotkey('2', "build_x", WID_RAT_BUILD_X),
	Hotkey('3', "build_ew", WID_RAT_BUILD_EW),
	Hotkey('4', "build_y", WID_RAT_BUILD_Y),
	Hotkey(_railtoolbar_autorail_keys, "autorail", WID_RAT_AUTORAIL),
	Hotkey('6', "demolish", WID_RAT_DEMOLISH),
	Hotkey('7', "depot", WID_RAT_BUILD_DEPOT),
	Hotkey('8', "waypoint", WID_RAT_BUILD_WAYPOINT),
	Hotkey('9', "station", WID_RAT_BUILD_STATION),
	Hotkey('S', "signal", WID_RAT_BUILD_SIGNALS),
	Hotkey('B', "bridge", WID_RAT_BUILD_BRIDGE),
	Hotkey('T', "tunnel", WID_RAT_BUILD_TUNNEL),
	Hotkey('R', "remove", WID_RAT_REMOVE),
	Hotkey('C', "convert", WID_RAT_CONVERT_RAIL),
	HOTKEY_LIST_END
};
HotkeyList BuildRailToolbarWindow::hotkeys("railtoolbar", railtoolbar_hotkeys, RailToolbarGlobalHotkeys);

static const NWidgetPart _nested_build_rail_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN, WID_RAT_CAPTION), SetDataTip(STR_WHITE_STRING, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_STICKYBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_RAT_BUILD_NS),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_RAIL_NS, STR_RAIL_TOOLBAR_TOOLTIP_BUILD_RAILROAD_TRACK),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_RAT_BUILD_X),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_RAIL_NE, STR_RAIL_TOOLBAR_TOOLTIP_BUILD_RAILROAD_TRACK),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_RAT_BUILD_EW),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_RAIL_EW, STR_RAIL_TOOLBAR_TOOLTIP_BUILD_RAILROAD_TRACK),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_RAT_BUILD_Y),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_RAIL_NW, STR_RAIL_TOOLBAR_TOOLTIP_BUILD_RAILROAD_TRACK),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_RAT_AUTORAIL),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_AUTORAIL, STR_RAIL_TOOLBAR_TOOLTIP_BUILD_AUTORAIL),

		NWidget(WWT_PANEL, COLOUR_DARK_GREEN), SetMinimalSize(4, 22), SetDataTip(0x0, STR_NULL), EndContainer(),

		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_RAT_DEMOLISH),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_DYNAMITE, STR_TOOLTIP_DEMOLISH_BUILDINGS_ETC),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_RAT_BUILD_DEPOT),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_DEPOT_RAIL, STR_RAIL_TOOLBAR_TOOLTIP_BUILD_TRAIN_DEPOT_FOR_BUILDING),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_RAT_BUILD_WAYPOINT),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_WAYPOINT, STR_RAIL_TOOLBAR_TOOLTIP_CONVERT_RAIL_TO_WAYPOINT),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_RAT_BUILD_STATION),
						SetFill(0, 1), SetMinimalSize(42, 22), SetDataTip(SPR_IMG_RAIL_STATION, STR_RAIL_TOOLBAR_TOOLTIP_BUILD_RAILROAD_STATION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_RAT_BUILD_SIGNALS),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_RAIL_SIGNALS, STR_RAIL_TOOLBAR_TOOLTIP_BUILD_RAILROAD_SIGNALS),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_RAT_BUILD_BRIDGE),
						SetFill(0, 1), SetMinimalSize(42, 22), SetDataTip(SPR_IMG_BRIDGE, STR_RAIL_TOOLBAR_TOOLTIP_BUILD_RAILROAD_BRIDGE),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_RAT_BUILD_TUNNEL),
						SetFill(0, 1), SetMinimalSize(20, 22), SetDataTip(SPR_IMG_TUNNEL_RAIL, STR_RAIL_TOOLBAR_TOOLTIP_BUILD_RAILROAD_TUNNEL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_RAT_REMOVE),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_REMOVE, STR_RAIL_TOOLBAR_TOOLTIP_TOGGLE_BUILD_REMOVE_FOR),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_RAT_CONVERT_RAIL),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_CONVERT_RAIL, STR_RAIL_TOOLBAR_TOOLTIP_CONVERT_RAIL),
	EndContainer(),
};

static WindowDesc _build_rail_desc(
	WDP_ALIGN_TOOLBAR, "toolbar_rail", 0, 0,
	WC_BUILD_TOOLBAR, WC_NONE,
	WDF_CONSTRUCTION,
	_nested_build_rail_widgets, lengthof(_nested_build_rail_widgets),
	&BuildRailToolbarWindow::hotkeys
);


/**
 * Open the build rail toolbar window for a specific rail type.
 *
 * If the terraform toolbar is linked to the toolbar, that window is also opened.
 *
 * @param railtype Rail type to open the window for
 * @return newly opened rail toolbar, or nullptr if the toolbar could not be opened.
 */
Window *ShowBuildRailToolbar(RailType railtype)
{
	if (!Company::IsValidID(_local_company)) return nullptr;
	if (!ValParamRailtype(railtype)) return nullptr;

	CloseWindowByClass(WC_BUILD_TOOLBAR);
	_cur_railtype = railtype;
	_remove_button_clicked = false;
	return new BuildRailToolbarWindow(&_build_rail_desc, railtype);
}

/* TODO: For custom stations, respect their allowed platforms/lengths bitmasks!
 * --pasky */

static void HandleStationPlacement(TileIndex start, TileIndex end)
{
	TileArea ta(start, end);
	uint numtracks = ta.w;
	uint platlength = ta.h;

	if (_railstation.orientation == AXIS_X) Swap(numtracks, platlength);

	uint32 p1 = _cur_railtype | _railstation.orientation << 6 | numtracks << 8 | platlength << 16 | _ctrl_pressed << 24;
	uint32 p2 = _railstation.station_class | _railstation.station_type << 8 | INVALID_STATION << 16;

	CommandContainer cmdcont = { ta.tile, p1, p2, CMD_BUILD_RAIL_STATION | CMD_MSG(STR_ERROR_CAN_T_BUILD_RAILROAD_STATION), CcStation, "" };
	ShowSelectStationIfNeeded(cmdcont, ta);
}

/** Enum referring to the Hotkeys in the build rail station window */
enum BuildRalStationHotkeys {
	BRASHK_FOCUS_FILTER_BOX, ///< Focus the edit box for editing the filter string
};

struct BuildRailStationWindow : public PickerWindowBase {
private:
	uint line_height;     ///< Height of a single line in the newstation selection matrix (#WID_BRAS_NEWST_LIST widget).
	uint coverage_height; ///< Height of the coverage texts.
	Scrollbar *vscroll;   ///< Vertical scrollbar of the new station list.
	Scrollbar *vscroll2;  ///< Vertical scrollbar of the matrix with new stations.

	typedef GUIList<StationClassID, StringFilter &> GUIStationClassList; ///< Type definition for the list to hold available station classes.

	static const uint EDITBOX_MAX_SIZE = 16; ///< The maximum number of characters for the filter edit box.

	static Listing   last_sorting;           ///< Default sorting of #GUIStationClassList.
	static Filtering last_filtering;         ///< Default filtering of #GUIStationClassList.
	static GUIStationClassList::SortFunction * const sorter_funcs[];   ///< Sort functions of the #GUIStationClassList.
	static GUIStationClassList::FilterFunction * const filter_funcs[]; ///< Filter functions of the #GUIStationClassList.
	GUIStationClassList station_classes;     ///< Available station classes.
	StringFilter string_filter;              ///< Filter for available station classes.
	QueryString filter_editbox;              ///< Filter editbox.

	/**
	 * Scrolls #WID_BRAS_NEWST_SCROLL so that the selected station class is visible.
	 *
	 * Note that this method should be called shortly after SelectClassAndStation() which will ensure
	 * an actual existing station class is selected, or the one at position 0 which will always be
	 * the default TTD rail station.
	 */
	void EnsureSelectedStationClassIsVisible()
	{
		uint pos = 0;
		for (auto station_class : this->station_classes) {
			if (station_class == _railstation.station_class) break;
			pos++;
		}
		this->vscroll->SetCount((int)this->station_classes.size());
		this->vscroll->ScrollTowards(pos);
	}

	/**
	 * Verify whether the currently selected station size is allowed after selecting a new station class/type.
	 * If not, change the station size variables ( _settings_client.gui.station_numtracks and _settings_client.gui.station_platlength ).
	 * @param statspec Specification of the new station class/type
	 */
	void CheckSelectedSize(const StationSpec *statspec)
	{
		if (statspec == nullptr || _settings_client.gui.station_dragdrop) return;

		/* If current number of tracks is not allowed, make it as big as possible */
		if (HasBit(statspec->disallowed_platforms, _settings_client.gui.station_numtracks - 1)) {
			this->RaiseWidget(_settings_client.gui.station_numtracks + WID_BRAS_PLATFORM_NUM_BEGIN);
			_settings_client.gui.station_numtracks = 1;
			if (statspec->disallowed_platforms != UINT8_MAX) {
				while (HasBit(statspec->disallowed_platforms, _settings_client.gui.station_numtracks - 1)) {
					_settings_client.gui.station_numtracks++;
				}
				this->LowerWidget(_settings_client.gui.station_numtracks + WID_BRAS_PLATFORM_NUM_BEGIN);
			}
		}

		if (HasBit(statspec->disallowed_lengths, _settings_client.gui.station_platlength - 1)) {
			this->RaiseWidget(_settings_client.gui.station_platlength + WID_BRAS_PLATFORM_LEN_BEGIN);
			_settings_client.gui.station_platlength = 1;
			if (statspec->disallowed_lengths != UINT8_MAX) {
				while (HasBit(statspec->disallowed_lengths, _settings_client.gui.station_platlength - 1)) {
					_settings_client.gui.station_platlength++;
				}
				this->LowerWidget(_settings_client.gui.station_platlength + WID_BRAS_PLATFORM_LEN_BEGIN);
			}
		}
	}

public:
	BuildRailStationWindow(WindowDesc *desc, Window *parent, bool newstation) : PickerWindowBase(desc, parent), filter_editbox(EDITBOX_MAX_SIZE * MAX_CHAR_LENGTH, EDITBOX_MAX_SIZE)
	{
		this->coverage_height = 2 * FONT_HEIGHT_NORMAL + 3 * WD_PAR_VSEP_NORMAL;
		this->vscroll = nullptr;
		_railstation.newstations = newstation;

		this->CreateNestedTree();
		NWidgetStacked *newst_additions = this->GetWidget<NWidgetStacked>(WID_BRAS_SHOW_NEWST_ADDITIONS);
		newst_additions->SetDisplayedPlane(newstation ? 0 : SZSP_NONE);
		newst_additions = this->GetWidget<NWidgetStacked>(WID_BRAS_SHOW_NEWST_MATRIX);
		newst_additions->SetDisplayedPlane(newstation ? 0 : SZSP_NONE);
		newst_additions = this->GetWidget<NWidgetStacked>(WID_BRAS_SHOW_NEWST_DEFSIZE);
		newst_additions->SetDisplayedPlane(newstation ? 0 : SZSP_NONE);
		newst_additions = this->GetWidget<NWidgetStacked>(WID_BRAS_SHOW_NEWST_RESIZE);
		newst_additions->SetDisplayedPlane(newstation ? 0 : SZSP_NONE);
		/* Hide the station class filter if no stations other than the default one are available. */
		this->GetWidget<NWidgetStacked>(WID_BRAS_FILTER_CONTAINER)->SetDisplayedPlane(newstation ? 0 : SZSP_NONE);
		if (newstation) {
			this->vscroll = this->GetScrollbar(WID_BRAS_NEWST_SCROLL);
			this->vscroll2 = this->GetScrollbar(WID_BRAS_MATRIX_SCROLL);

			this->querystrings[WID_BRAS_FILTER_EDITBOX] = &this->filter_editbox;
			this->station_classes.SetListing(this->last_sorting);
			this->station_classes.SetFiltering(this->last_filtering);
			this->station_classes.SetSortFuncs(this->sorter_funcs);
			this->station_classes.SetFilterFuncs(this->filter_funcs);
		}

		this->station_classes.ForceRebuild();

		BuildStationClassesAvailable();
		SelectClassAndStation();

		this->FinishInitNested(TRANSPORT_RAIL);

		this->LowerWidget(_railstation.orientation + WID_BRAS_PLATFORM_DIR_X);
		if (_settings_client.gui.station_dragdrop) {
			this->LowerWidget(WID_BRAS_PLATFORM_DRAG_N_DROP);
		} else {
			this->LowerWidget(_settings_client.gui.station_numtracks + WID_BRAS_PLATFORM_NUM_BEGIN);
			this->LowerWidget(_settings_client.gui.station_platlength + WID_BRAS_PLATFORM_LEN_BEGIN);
		}
		this->SetWidgetLoweredState(WID_BRAS_HIGHLIGHT_OFF, !_settings_client.gui.station_show_coverage);
		this->SetWidgetLoweredState(WID_BRAS_HIGHLIGHT_ON, _settings_client.gui.station_show_coverage);

		if (!newstation) {
			_railstation.station_class = StationClassID::STAT_CLASS_DFLT;
			_railstation.station_type = 0;
			this->vscroll2 = nullptr;
		} else {
			_railstation.station_count = StationClass::Get(_railstation.station_class)->GetSpecCount();
			_railstation.station_type = std::min<int>(_railstation.station_type, _railstation.station_count - 1);

			NWidgetMatrix *matrix = this->GetWidget<NWidgetMatrix>(WID_BRAS_MATRIX);
			matrix->SetScrollbar(this->vscroll2);
			matrix->SetCount(_railstation.station_count);
			matrix->SetClicked(_railstation.station_type);

			EnsureSelectedStationClassIsVisible();
		}

		this->InvalidateData();
	}

	void Close() override
	{
		CloseWindowById(WC_SELECT_STATION, 0);
		this->PickerWindowBase::Close();
	}

	/** Sort station classes by StationClassID. */
	static bool StationClassIDSorter(StationClassID const &a, StationClassID const &b)
	{
		return a < b;
	}

	/** Filter station classes by class name. */
	static bool CDECL TagNameFilter(StationClassID const * sc, StringFilter &filter)
	{
		char buffer[DRAW_STRING_BUFFER];
		GetString(buffer, StationClass::Get(*sc)->name, lastof(buffer));

		filter.ResetState();
		filter.AddLine(buffer);
		return filter.GetState();
	}

	/** Builds the filter list of available station classes. */
	void BuildStationClassesAvailable()
	{
		if (!this->station_classes.NeedRebuild()) return;

		this->station_classes.clear();

		for (uint i = 0; i < StationClass::GetClassCount(); i++) {
			StationClassID station_class_id = (StationClassID)i;
			if (station_class_id == StationClassID::STAT_CLASS_WAYP) {
				// Skip waypoints.
				continue;
			}
			StationClass *station_class = StationClass::Get(station_class_id);
			if (station_class->GetUISpecCount() == 0) continue;
			station_classes.push_back(station_class_id);
		}

		if (_railstation.newstations) {
			this->station_classes.Filter(this->string_filter);
			this->station_classes.shrink_to_fit();
			this->station_classes.RebuildDone();
			this->station_classes.Sort();

			this->vscroll->SetCount((uint)this->station_classes.size());
		}
	}

	/**
	 * Checks if the previously selected current station class and station
	 * can be shown as selected to the user when the dialog is opened.
	 */
	void SelectClassAndStation()
	{
		if (_railstation.station_class == StationClassID::STAT_CLASS_DFLT) {
			/* This happens during the first time the window is open during the game life cycle. */
			this->SelectOtherClass(StationClassID::STAT_CLASS_DFLT);
		} else {
			/* Check if the previously selected station class is not available anymore as a
			 * result of starting a new game without the corresponding NewGRF. */
			bool available = false;
			for (uint i = 0; i < StationClass::GetClassCount(); ++i) {
				if ((StationClassID)i == _railstation.station_class) {
					available = true;
					break;
				}
			}

			this->SelectOtherClass(available ? _railstation.station_class : StationClassID::STAT_CLASS_DFLT);
		}
	}

	/**
	 * Select the specified station class.
	 * @param station_class Station class select.
	 */
	void SelectOtherClass(StationClassID station_class)
	{
		_railstation.station_class = station_class;
	}

	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (!gui_scope) return;

		this->BuildStationClassesAvailable();
	}

	EventState OnHotkey(int hotkey) override
	{
		switch (hotkey) {
			case BRASHK_FOCUS_FILTER_BOX:
				this->SetFocusedWidget(WID_BRAS_FILTER_EDITBOX);
				SetFocusedWindow(this); // The user has asked to give focus to the text box, so make sure this window is focused.
				break;

			default:
				return ES_NOT_HANDLED;
		}

		return ES_HANDLED;
	}

	void OnEditboxChanged(int wid) override
	{
		string_filter.SetFilterTerm(this->filter_editbox.text.buf);
		this->station_classes.SetFilterState(!string_filter.IsEmpty());
		this->station_classes.ForceRebuild();
		this->InvalidateData();
	}

	void OnPaint() override
	{
		bool newstations = _railstation.newstations;
		const StationSpec *statspec = newstations ? StationClass::Get(_railstation.station_class)->GetSpec(_railstation.station_type) : nullptr;

		if (_settings_client.gui.station_dragdrop) {
			SetTileSelectSize(1, 1);
		} else {
			int x = _settings_client.gui.station_numtracks;
			int y = _settings_client.gui.station_platlength;
			if (_railstation.orientation == AXIS_X) Swap(x, y);
			if (!_remove_button_clicked) {
				SetTileSelectSize(x, y);
			}
		}

		int rad = (_settings_game.station.modified_catchment) ? CA_TRAIN : CA_UNMODIFIED;

		if (_settings_client.gui.station_show_coverage) SetTileSelectBigSize(-rad, -rad, 2 * rad, 2 * rad);

		for (uint bits = 0; bits < 7; bits++) {
			bool disable = bits >= _settings_game.station.station_spread;
			if (statspec == nullptr) {
				this->SetWidgetDisabledState(bits + WID_BRAS_PLATFORM_NUM_1, disable);
				this->SetWidgetDisabledState(bits + WID_BRAS_PLATFORM_LEN_1, disable);
			} else {
				this->SetWidgetDisabledState(bits + WID_BRAS_PLATFORM_NUM_1, HasBit(statspec->disallowed_platforms, bits) || disable);
				this->SetWidgetDisabledState(bits + WID_BRAS_PLATFORM_LEN_1, HasBit(statspec->disallowed_lengths,   bits) || disable);
			}
		}

		this->DrawWidgets();

		if (this->IsShaded()) return;
		/* 'Accepts' and 'Supplies' texts. */
		NWidgetBase *cov = this->GetWidget<NWidgetBase>(WID_BRAS_COVERAGE_TEXTS);
		int top = cov->pos_y + WD_PAR_VSEP_NORMAL;
		int left = cov->pos_x + WD_FRAMERECT_LEFT;
		int right = cov->pos_x + cov->current_x - WD_FRAMERECT_RIGHT;
		int bottom = cov->pos_y + cov->current_y;
		top = DrawStationCoverageAreaText(left, right, top, SCT_ALL, rad, false) + WD_PAR_VSEP_NORMAL;
		top = DrawStationCoverageAreaText(left, right, top, SCT_ALL, rad, true) + WD_PAR_VSEP_NORMAL;
		/* Resize background if the window is too small.
		 * Never make the window smaller to avoid oscillating if the size change affects the acceptance.
		 * (This is the case, if making the window bigger moves the mouse into the window.) */
		if (top > bottom) {
			this->coverage_height += top - bottom;
			this->ReInit();
		}
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_BRAS_NEWST_LIST: {
				Dimension d = {0, 0};
				for (auto station_class : this->station_classes) {
					d = maxdim(d, GetStringBoundingBox(StationClass::Get(station_class)->name));
				}
				size->width = std::max(size->width, d.width + padding.width);
				this->line_height = FONT_HEIGHT_NORMAL + WD_MATRIX_TOP + WD_MATRIX_BOTTOM;
				size->height = 5 * this->line_height;
				resize->height = this->line_height;
				break;
			}

			case WID_BRAS_SHOW_NEWST_TYPE: {
				if (!_railstation.newstations) {
					size->width = 0;
					size->height = 0;
					break;
				}

				/* If newstations exist, compute the non-zero minimal size. */
				Dimension d = {0, 0};
				StringID str = this->GetWidget<NWidgetCore>(widget)->widget_data;
				for (auto station_class : this->station_classes) {
					StationClass *stclass = StationClass::Get(station_class);
					for (uint j = 0; j < stclass->GetSpecCount(); j++) {
						const StationSpec *statspec = stclass->GetSpec(j);
						SetDParam(0, (statspec != nullptr && statspec->name != 0) ? statspec->name : STR_STATION_CLASS_DFLT);
						d = maxdim(d, GetStringBoundingBox(str));
					}
				}
				size->width = std::max(size->width, d.width + padding.width);
				break;
			}

			case WID_BRAS_PLATFORM_DIR_X:
			case WID_BRAS_PLATFORM_DIR_Y:
			case WID_BRAS_IMAGE:
				size->width  = ScaleGUITrad(64) + 2;
				size->height = ScaleGUITrad(58) + 2;
				break;

			case WID_BRAS_COVERAGE_TEXTS:
				size->height = this->coverage_height;
				break;

			case WID_BRAS_MATRIX:
				fill->height = 1;
				resize->height = 1;
				break;
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		DrawPixelInfo tmp_dpi;

		switch (GB(widget, 0, 16)) {
			case WID_BRAS_PLATFORM_DIR_X:
				/* Set up a clipping area for the '/' station preview */
				if (FillDrawPixelInfo(&tmp_dpi, r.left, r.top, r.right - r.left + 1, r.bottom - r.top + 1)) {
					DrawPixelInfo *old_dpi = _cur_dpi;
					_cur_dpi = &tmp_dpi;
					int x = ScaleGUITrad(31) + 1;
					int y = r.bottom - r.top - ScaleGUITrad(31);
					if (!DrawStationTile(x, y, _cur_railtype, AXIS_X, _railstation.station_class, _railstation.station_type)) {
						StationPickerDrawSprite(x, y, STATION_RAIL, _cur_railtype, INVALID_ROADTYPE, 2);
					}
					_cur_dpi = old_dpi;
				}
				break;

			case WID_BRAS_PLATFORM_DIR_Y:
				/* Set up a clipping area for the '\' station preview */
				if (FillDrawPixelInfo(&tmp_dpi, r.left, r.top, r.right - r.left + 1, r.bottom - r.top + 1)) {
					DrawPixelInfo *old_dpi = _cur_dpi;
					_cur_dpi = &tmp_dpi;
					int x = ScaleGUITrad(31) + 1;
					int y = r.bottom - r.top - ScaleGUITrad(31);
					if (!DrawStationTile(x, y, _cur_railtype, AXIS_Y, _railstation.station_class, _railstation.station_type)) {
						StationPickerDrawSprite(x, y, STATION_RAIL, _cur_railtype, INVALID_ROADTYPE, 3);
					}
					_cur_dpi = old_dpi;
				}
				break;

			case WID_BRAS_NEWST_LIST: {
				uint statclass = 0;
				uint row = 0;
				for (auto station_class : this->station_classes) {
					if (this->vscroll->IsVisible(statclass)) {
						DrawString(r.left + WD_MATRIX_LEFT, r.right - WD_MATRIX_RIGHT, row * this->line_height + r.top + WD_MATRIX_TOP,
								StationClass::Get(station_class)->name,
								station_class == _railstation.station_class ? TC_WHITE : TC_BLACK);
						row++;
					}
					statclass++;
				}
				break;
			}

			case WID_BRAS_IMAGE: {
				byte type = GB(widget, 16, 16);
				assert(type < _railstation.station_count);
				/* Check station availability callback */
				const StationSpec *statspec = StationClass::Get(_railstation.station_class)->GetSpec(type);
				if (!IsStationAvailable(statspec)) {
					GfxFillRect(r.left + 1, r.top + 1, r.right - 1, r.bottom - 1, PC_BLACK, FILLRECT_CHECKER);
				}

				/* Set up a clipping area for the station preview. */
				if (FillDrawPixelInfo(&tmp_dpi, r.left, r.top, r.right - r.left + 1, r.bottom - r.top + 1)) {
					DrawPixelInfo *old_dpi = _cur_dpi;
					_cur_dpi = &tmp_dpi;
					int x = ScaleGUITrad(31) + 1;
					int y = r.bottom - r.top - ScaleGUITrad(31);
					if (!DrawStationTile(x, y, _cur_railtype, _railstation.orientation, _railstation.station_class, type)) {
						StationPickerDrawSprite(x, y, STATION_RAIL, _cur_railtype, INVALID_ROADTYPE, 2 + _railstation.orientation);
					}
					_cur_dpi = old_dpi;
				}
				break;
			}
		}
	}

	void OnResize() override
	{
		if (this->vscroll != nullptr) { // New stations available.
			this->vscroll->SetCapacityFromWidget(this, WID_BRAS_NEWST_LIST);
		}
	}

	void SetStringParameters(int widget) const override
	{
		if (widget == WID_BRAS_SHOW_NEWST_TYPE) {
			const StationSpec *statspec = StationClass::Get(_railstation.station_class)->GetSpec(_railstation.station_type);
			SetDParam(0, (statspec != nullptr && statspec->name != 0) ? statspec->name : STR_STATION_CLASS_DFLT);
		}
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (GB(widget, 0, 16)) {
			case WID_BRAS_PLATFORM_DIR_X:
			case WID_BRAS_PLATFORM_DIR_Y:
				this->RaiseWidget(_railstation.orientation + WID_BRAS_PLATFORM_DIR_X);
				_railstation.orientation = (Axis)(widget - WID_BRAS_PLATFORM_DIR_X);
				this->LowerWidget(_railstation.orientation + WID_BRAS_PLATFORM_DIR_X);
				if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				CloseWindowById(WC_SELECT_STATION, 0);
				break;

			case WID_BRAS_PLATFORM_NUM_1:
			case WID_BRAS_PLATFORM_NUM_2:
			case WID_BRAS_PLATFORM_NUM_3:
			case WID_BRAS_PLATFORM_NUM_4:
			case WID_BRAS_PLATFORM_NUM_5:
			case WID_BRAS_PLATFORM_NUM_6:
			case WID_BRAS_PLATFORM_NUM_7: {
				this->RaiseWidget(_settings_client.gui.station_numtracks + WID_BRAS_PLATFORM_NUM_BEGIN);
				this->RaiseWidget(WID_BRAS_PLATFORM_DRAG_N_DROP);

				_settings_client.gui.station_numtracks = widget - WID_BRAS_PLATFORM_NUM_BEGIN;
				_settings_client.gui.station_dragdrop = false;

				_settings_client.gui.station_dragdrop = false;

				const StationSpec *statspec = _railstation.newstations ? StationClass::Get(_railstation.station_class)->GetSpec(_railstation.station_type) : nullptr;
				if (statspec != nullptr && HasBit(statspec->disallowed_lengths, _settings_client.gui.station_platlength - 1)) {
					/* The previously selected number of platforms in invalid */
					for (uint i = 0; i < 7; i++) {
						if (!HasBit(statspec->disallowed_lengths, i)) {
							this->RaiseWidget(_settings_client.gui.station_platlength + WID_BRAS_PLATFORM_LEN_BEGIN);
							_settings_client.gui.station_platlength = i + 1;
							break;
						}
					}
				}

				this->LowerWidget(_settings_client.gui.station_numtracks + WID_BRAS_PLATFORM_NUM_BEGIN);
				this->LowerWidget(_settings_client.gui.station_platlength + WID_BRAS_PLATFORM_LEN_BEGIN);
				if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				CloseWindowById(WC_SELECT_STATION, 0);
				break;
			}

			case WID_BRAS_PLATFORM_LEN_1:
			case WID_BRAS_PLATFORM_LEN_2:
			case WID_BRAS_PLATFORM_LEN_3:
			case WID_BRAS_PLATFORM_LEN_4:
			case WID_BRAS_PLATFORM_LEN_5:
			case WID_BRAS_PLATFORM_LEN_6:
			case WID_BRAS_PLATFORM_LEN_7: {
				this->RaiseWidget(_settings_client.gui.station_platlength + WID_BRAS_PLATFORM_LEN_BEGIN);
				this->RaiseWidget(WID_BRAS_PLATFORM_DRAG_N_DROP);

				_settings_client.gui.station_platlength = widget - WID_BRAS_PLATFORM_LEN_BEGIN;
				_settings_client.gui.station_dragdrop = false;

				_settings_client.gui.station_dragdrop = false;

				const StationSpec *statspec = _railstation.newstations ? StationClass::Get(_railstation.station_class)->GetSpec(_railstation.station_type) : nullptr;
				if (statspec != nullptr && HasBit(statspec->disallowed_platforms, _settings_client.gui.station_numtracks - 1)) {
					/* The previously selected number of tracks in invalid */
					for (uint i = 0; i < 7; i++) {
						if (!HasBit(statspec->disallowed_platforms, i)) {
							this->RaiseWidget(_settings_client.gui.station_numtracks + WID_BRAS_PLATFORM_NUM_BEGIN);
							_settings_client.gui.station_numtracks = i + 1;
							break;
						}
					}
				}

				this->LowerWidget(_settings_client.gui.station_numtracks + WID_BRAS_PLATFORM_NUM_BEGIN);
				this->LowerWidget(_settings_client.gui.station_platlength + WID_BRAS_PLATFORM_LEN_BEGIN);
				if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				CloseWindowById(WC_SELECT_STATION, 0);
				break;
			}

			case WID_BRAS_PLATFORM_DRAG_N_DROP: {
				_settings_client.gui.station_dragdrop ^= true;

				this->ToggleWidgetLoweredState(WID_BRAS_PLATFORM_DRAG_N_DROP);

				/* get the first allowed length/number of platforms */
				const StationSpec *statspec = _railstation.newstations ? StationClass::Get(_railstation.station_class)->GetSpec(_railstation.station_type) : nullptr;
				if (statspec != nullptr && HasBit(statspec->disallowed_lengths, _settings_client.gui.station_platlength - 1)) {
					for (uint i = 0; i < 7; i++) {
						if (!HasBit(statspec->disallowed_lengths, i)) {
							this->RaiseWidget(_settings_client.gui.station_platlength + WID_BRAS_PLATFORM_LEN_BEGIN);
							_settings_client.gui.station_platlength = i + 1;
							break;
						}
					}
				}
				if (statspec != nullptr && HasBit(statspec->disallowed_platforms, _settings_client.gui.station_numtracks - 1)) {
					for (uint i = 0; i < 7; i++) {
						if (!HasBit(statspec->disallowed_platforms, i)) {
							this->RaiseWidget(_settings_client.gui.station_numtracks + WID_BRAS_PLATFORM_NUM_BEGIN);
							_settings_client.gui.station_numtracks = i + 1;
							break;
						}
					}
				}

				this->SetWidgetLoweredState(_settings_client.gui.station_numtracks + WID_BRAS_PLATFORM_NUM_BEGIN, !_settings_client.gui.station_dragdrop);
				this->SetWidgetLoweredState(_settings_client.gui.station_platlength + WID_BRAS_PLATFORM_LEN_BEGIN, !_settings_client.gui.station_dragdrop);
				if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				CloseWindowById(WC_SELECT_STATION, 0);
				break;
			}

			case WID_BRAS_HIGHLIGHT_OFF:
			case WID_BRAS_HIGHLIGHT_ON:
				_settings_client.gui.station_show_coverage = (widget != WID_BRAS_HIGHLIGHT_OFF);

				this->SetWidgetLoweredState(WID_BRAS_HIGHLIGHT_OFF, !_settings_client.gui.station_show_coverage);
				this->SetWidgetLoweredState(WID_BRAS_HIGHLIGHT_ON, _settings_client.gui.station_show_coverage);
				if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				SetViewportCatchmentStation(nullptr, true);
				break;

			case WID_BRAS_NEWST_LIST: {
				int y = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_BRAS_NEWST_LIST);
				if (y >= (int)this->station_classes.size()) return;
				StationClassID station_class_id = this->station_classes[y];
				if (_railstation.station_class != station_class_id) {
					StationClass *station_class = StationClass::Get(station_class_id);
					_railstation.station_class = station_class_id;
					_railstation.station_count = station_class->GetSpecCount();
					_railstation.station_type  = 0;

					this->CheckSelectedSize(station_class->GetSpec(_railstation.station_type));

					NWidgetMatrix *matrix = this->GetWidget<NWidgetMatrix>(WID_BRAS_MATRIX);
					matrix->SetCount(_railstation.station_count);
					matrix->SetClicked(_railstation.station_type);
				}
				if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				CloseWindowById(WC_SELECT_STATION, 0);
				break;
			}

			case WID_BRAS_IMAGE: {
				int y = GB(widget, 16, 16);
				if (y >= _railstation.station_count) return;

				/* Check station availability callback */
				const StationSpec *statspec = StationClass::Get(_railstation.station_class)->GetSpec(y);
				if (!IsStationAvailable(statspec)) return;

				_railstation.station_type = y;

				this->CheckSelectedSize(statspec);
				this->GetWidget<NWidgetMatrix>(WID_BRAS_MATRIX)->SetClicked(_railstation.station_type);

				if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				CloseWindowById(WC_SELECT_STATION, 0);
				break;
			}
		}
	}

	void OnRealtimeTick(uint delta_ms) override
	{
		CheckRedrawStationCoverage(this);
	}

	static HotkeyList hotkeys;
};

/**
 * Handler for global hotkeys of the BuildRailStationWindow.
 * @param hotkey Hotkey
 * @return ES_HANDLED if hotkey was accepted.
 */
static EventState BuildRailStationGlobalHotkeys(int hotkey)
{
	if (_game_mode == GM_MENU) return ES_NOT_HANDLED;
	Window *w = ShowStationBuilder(FindWindowById(WC_BUILD_TOOLBAR, TRANSPORT_RAIL));
	if (w == nullptr) return ES_NOT_HANDLED;
	return w->OnHotkey(hotkey);
}

static Hotkey buildrailstation_hotkeys[] = {
	Hotkey('F', "focus_filter_box", BRASHK_FOCUS_FILTER_BOX),
	HOTKEY_LIST_END
};
HotkeyList BuildRailStationWindow::hotkeys("buildrailstation", buildrailstation_hotkeys, BuildRailStationGlobalHotkeys);

Listing BuildRailStationWindow::last_sorting = { false, 0 };
Filtering BuildRailStationWindow::last_filtering = { false, 0 };

BuildRailStationWindow::GUIStationClassList::SortFunction * const BuildRailStationWindow::sorter_funcs[] = {
	&StationClassIDSorter,
};

BuildRailStationWindow::GUIStationClassList::FilterFunction * const BuildRailStationWindow::filter_funcs[] = {
	&TagNameFilter,
};

static const NWidgetPart _nested_station_builder_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN), SetDataTip(STR_STATION_BUILD_RAIL_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_DARK_GREEN),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BRAS_SHOW_NEWST_DEFSIZE),
			NWidget(WWT_DEFSIZEBOX, COLOUR_DARK_GREEN),
		EndContainer(),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN),
		NWidget(NWID_HORIZONTAL), SetPadding(2, 0, 0, 2),
			NWidget(NWID_VERTICAL),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BRAS_FILTER_CONTAINER),
					NWidget(NWID_HORIZONTAL), SetPadding(0, 5, 2, 0),
						NWidget(WWT_TEXT, COLOUR_DARK_GREEN), SetFill(0, 1), SetDataTip(STR_LIST_FILTER_TITLE, STR_NULL),
						NWidget(WWT_EDITBOX, COLOUR_GREY, WID_BRAS_FILTER_EDITBOX), SetFill(1, 0), SetResize(1, 0),
								SetDataTip(STR_LIST_FILTER_OSKTITLE, STR_LIST_FILTER_TOOLTIP),
					EndContainer(),
				EndContainer(),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BRAS_SHOW_NEWST_ADDITIONS),
					NWidget(NWID_HORIZONTAL), SetPadding(0, 5, 2, 0),
						NWidget(WWT_MATRIX, COLOUR_GREY, WID_BRAS_NEWST_LIST), SetMinimalSize(122, 71), SetFill(1, 0),
								SetMatrixDataTip(1, 0, STR_STATION_BUILD_STATION_CLASS_TOOLTIP), SetScrollbar(WID_BRAS_NEWST_SCROLL),
						NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_BRAS_NEWST_SCROLL),
					EndContainer(),
				EndContainer(),
				NWidget(WWT_LABEL, COLOUR_DARK_GREEN), SetMinimalSize(144, 11), SetDataTip(STR_STATION_BUILD_ORIENTATION, STR_NULL), SetPadding(1, 2, 0, 0),
				NWidget(NWID_HORIZONTAL),
					NWidget(NWID_SPACER), SetMinimalSize(7, 0), SetFill(1, 0),
					NWidget(WWT_PANEL, COLOUR_GREY, WID_BRAS_PLATFORM_DIR_X), SetMinimalSize(66, 60), SetFill(0, 0), SetDataTip(0x0, STR_STATION_BUILD_RAILROAD_ORIENTATION_TOOLTIP), EndContainer(),
					NWidget(NWID_SPACER), SetMinimalSize(2, 0), SetFill(1, 0),
					NWidget(WWT_PANEL, COLOUR_GREY, WID_BRAS_PLATFORM_DIR_Y), SetMinimalSize(66, 60), SetFill(0, 0), SetDataTip(0x0, STR_STATION_BUILD_RAILROAD_ORIENTATION_TOOLTIP), EndContainer(),
					NWidget(NWID_SPACER), SetMinimalSize(7, 0), SetFill(1, 0),
				EndContainer(),
				NWidget(WWT_LABEL, COLOUR_DARK_GREEN, WID_BRAS_SHOW_NEWST_TYPE), SetMinimalSize(144, 11), SetDataTip(STR_ORANGE_STRING, STR_NULL), SetPadding(1, 2, 4, 2),
				NWidget(WWT_LABEL, COLOUR_DARK_GREEN), SetMinimalSize(144, 11), SetDataTip(STR_STATION_BUILD_NUMBER_OF_TRACKS, STR_NULL), SetPadding(0, 2, 0, 2),
				NWidget(NWID_HORIZONTAL),
					NWidget(NWID_SPACER), SetFill(1, 0),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_NUM_1), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_1, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_NUM_2), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_2, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_NUM_3), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_3, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_NUM_4), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_4, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_NUM_5), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_5, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_NUM_6), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_6, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_NUM_7), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_7, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
					NWidget(NWID_SPACER), SetFill(1, 0),
				EndContainer(),
				NWidget(WWT_LABEL, COLOUR_DARK_GREEN), SetMinimalSize(144, 11), SetDataTip(STR_STATION_BUILD_PLATFORM_LENGTH, STR_NULL), SetPadding(2, 2, 0, 2),
				NWidget(NWID_HORIZONTAL),
					NWidget(NWID_SPACER), SetFill(1, 0),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_LEN_1), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_1, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_LEN_2), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_2, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_LEN_3), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_3, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_LEN_4), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_4, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_LEN_5), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_5, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_LEN_6), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_6, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_LEN_7), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_7, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
					NWidget(NWID_SPACER), SetFill(1, 0),
				EndContainer(),
				NWidget(NWID_SPACER), SetMinimalSize(0, 2),
				NWidget(NWID_HORIZONTAL),
					NWidget(NWID_SPACER), SetMinimalSize(2, 0), SetFill(1, 0),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_DRAG_N_DROP), SetMinimalSize(75, 12), SetDataTip(STR_STATION_BUILD_DRAG_DROP, STR_STATION_BUILD_DRAG_DROP_TOOLTIP),
					NWidget(NWID_SPACER), SetMinimalSize(2, 0), SetFill(1, 0),
				EndContainer(),
				NWidget(WWT_LABEL, COLOUR_DARK_GREEN), SetMinimalSize(144, 11), SetDataTip(STR_STATION_BUILD_COVERAGE_AREA_TITLE, STR_NULL), SetPadding(3, 2, 0, 0),
				NWidget(NWID_HORIZONTAL),
					NWidget(NWID_SPACER), SetMinimalSize(2, 0), SetFill(1, 0),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_HIGHLIGHT_OFF), SetMinimalSize(60, 12),
												SetDataTip(STR_STATION_BUILD_COVERAGE_OFF, STR_STATION_BUILD_COVERAGE_AREA_OFF_TOOLTIP),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_HIGHLIGHT_ON), SetMinimalSize(60, 12),
												SetDataTip(STR_STATION_BUILD_COVERAGE_ON, STR_STATION_BUILD_COVERAGE_AREA_ON_TOOLTIP),
					NWidget(NWID_SPACER), SetMinimalSize(2, 0), SetFill(1, 0),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BRAS_SHOW_NEWST_MATRIX),
				/* We need an additional background for the matrix, as the matrix cannot handle the scrollbar due to not being an NWidgetCore. */
				NWidget(WWT_PANEL, COLOUR_DARK_GREEN), SetScrollbar(WID_BRAS_MATRIX_SCROLL),
					NWidget(NWID_HORIZONTAL),
						NWidget(NWID_MATRIX, COLOUR_DARK_GREEN, WID_BRAS_MATRIX), SetScrollbar(WID_BRAS_MATRIX_SCROLL), SetPIP(0, 2, 0),
							NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BRAS_IMAGE), SetMinimalSize(66, 60),
									SetFill(0, 0), SetResize(0, 0), SetDataTip(0x0, STR_STATION_BUILD_STATION_TYPE_TOOLTIP), SetScrollbar(WID_BRAS_MATRIX_SCROLL),
							EndContainer(),
						EndContainer(),
						NWidget(NWID_VSCROLLBAR, COLOUR_DARK_GREEN, WID_BRAS_MATRIX_SCROLL),
					EndContainer(),
				EndContainer(),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_HORIZONTAL),
			NWidget(WWT_EMPTY, INVALID_COLOUR, WID_BRAS_COVERAGE_TEXTS), SetPadding(2, 5, 0, 1), SetFill(1, 1), SetResize(1, 0),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BRAS_SHOW_NEWST_RESIZE),
				NWidget(NWID_VERTICAL),
					NWidget(WWT_PANEL, COLOUR_DARK_GREEN), SetFill(0, 1), EndContainer(),
					NWidget(WWT_RESIZEBOX, COLOUR_DARK_GREEN),
				EndContainer(),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

/** High level window description of the station-build window (default & newGRF) */
static WindowDesc _station_builder_desc(
	WDP_AUTO, "build_station_rail", 350, 0,
	WC_BUILD_STATION, WC_BUILD_TOOLBAR,
	WDF_CONSTRUCTION,
	_nested_station_builder_widgets, lengthof(_nested_station_builder_widgets),
	&BuildRailStationWindow::hotkeys
);

/** Open station build window */
static Window *ShowStationBuilder(Window *parent)
{
	bool newstations = StationClass::GetClassCount() > 2 || StationClass::Get(STAT_CLASS_DFLT)->GetSpecCount() != 1;
	return new BuildRailStationWindow(&_station_builder_desc, parent, newstations);
}

struct BuildSignalWindow : public PickerWindowBase {
private:
	Dimension sig_sprite_size;     ///< Maximum size of signal GUI sprites.
	int sig_sprite_bottom_offset;  ///< Maximum extent of signal GUI sprite from reference point towards bottom.

	/**
	 * Draw dynamic a signal-sprite in a button in the signal GUI
	 * Draw the sprite +1px to the right and down if the button is lowered
	 *
	 * @param widget_index index of this widget in the window
	 * @param image        the sprite to draw
	 */
	void DrawSignalSprite(byte widget_index, SpriteID image) const
	{
		Point offset;
		Dimension sprite_size = GetSpriteSize(image, &offset);
		const NWidgetBase *widget = this->GetWidget<NWidgetBase>(widget_index);
		int x = widget->pos_x - offset.x +
				(widget->current_x - sprite_size.width + offset.x) / 2;  // centered
		int y = widget->pos_y - sig_sprite_bottom_offset + WD_IMGBTN_TOP +
				(widget->current_y - WD_IMGBTN_TOP - WD_IMGBTN_BOTTOM + sig_sprite_size.height) / 2; // aligned to bottom

		DrawSprite(image, PAL_NONE,
				x + this->IsWidgetLowered(widget_index),
				y + this->IsWidgetLowered(widget_index));
	}

public:
	BuildSignalWindow(WindowDesc *desc, Window *parent) : PickerWindowBase(desc, parent)
	{
		this->InitNested(TRANSPORT_RAIL);
		this->OnInvalidateData();
	}

	void Close() override
	{
		_convert_signal_button = false;
		this->PickerWindowBase::Close();
	}

	void OnInit() override
	{
		/* Calculate maximum signal sprite size. */
		this->sig_sprite_size.width = 0;
		this->sig_sprite_size.height = 0;
		this->sig_sprite_bottom_offset = 0;
		const RailtypeInfo *rti = GetRailTypeInfo(_cur_railtype);
		for (uint type = SIGTYPE_NORMAL; type < SIGTYPE_END; type++) {
			for (uint variant = SIG_ELECTRIC; variant <= SIG_SEMAPHORE; variant++) {
				for (uint lowered = 0; lowered < 2; lowered++) {
					Point offset;
					Dimension sprite_size = GetSpriteSize(rti->gui_sprites.signals[type][variant][lowered], &offset);
					this->sig_sprite_bottom_offset = std::max<int>(this->sig_sprite_bottom_offset, sprite_size.height);
					this->sig_sprite_size.width = std::max<int>(this->sig_sprite_size.width, sprite_size.width - offset.x);
					this->sig_sprite_size.height = std::max<int>(this->sig_sprite_size.height, sprite_size.height - offset.y);
				}
			}
		}
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		if (widget == WID_BS_DRAG_SIGNALS_DENSITY_LABEL) {
			/* Two digits for signals density. */
			size->width = std::max(size->width, 2 * GetDigitWidth() + padding.width + WD_FRAMERECT_LEFT + WD_FRAMERECT_RIGHT);
		} else if (IsInsideMM(widget, WID_BS_SEMAPHORE_NORM, WID_BS_ELECTRIC_PBS_OWAY + 1)) {
			size->width = std::max(size->width, this->sig_sprite_size.width + WD_IMGBTN_LEFT + WD_IMGBTN_RIGHT);
			size->height = std::max(size->height, this->sig_sprite_size.height + WD_IMGBTN_TOP + WD_IMGBTN_BOTTOM);
		}
	}

	void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_BS_DRAG_SIGNALS_DENSITY_LABEL:
				SetDParam(0, _settings_client.gui.drag_signals_density);
				break;
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		if (IsInsideMM(widget, WID_BS_SEMAPHORE_NORM, WID_BS_ELECTRIC_PBS_OWAY + 1)) {
			/* Extract signal from widget number. */
			int type = (widget - WID_BS_SEMAPHORE_NORM) % SIGTYPE_END;
			int var = SIG_SEMAPHORE - (widget - WID_BS_SEMAPHORE_NORM) / SIGTYPE_END; // SignalVariant order is reversed compared to the widgets.
			SpriteID sprite = GetRailTypeInfo(_cur_railtype)->gui_sprites.signals[type][var][this->IsWidgetLowered(widget)];

			this->DrawSignalSprite(widget, sprite);
		}
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_BS_SEMAPHORE_NORM:
			case WID_BS_SEMAPHORE_ENTRY:
			case WID_BS_SEMAPHORE_EXIT:
			case WID_BS_SEMAPHORE_COMBO:
			case WID_BS_SEMAPHORE_PBS:
			case WID_BS_SEMAPHORE_PBS_OWAY:
			case WID_BS_ELECTRIC_NORM:
			case WID_BS_ELECTRIC_ENTRY:
			case WID_BS_ELECTRIC_EXIT:
			case WID_BS_ELECTRIC_COMBO:
			case WID_BS_ELECTRIC_PBS:
			case WID_BS_ELECTRIC_PBS_OWAY:
				this->RaiseWidget((_cur_signal_variant == SIG_ELECTRIC ? WID_BS_ELECTRIC_NORM : WID_BS_SEMAPHORE_NORM) + _cur_signal_type);

				_cur_signal_type = (SignalType)((uint)((widget - WID_BS_SEMAPHORE_NORM) % (SIGTYPE_LAST + 1)));
				_cur_signal_variant = widget >= WID_BS_ELECTRIC_NORM ? SIG_ELECTRIC : SIG_SEMAPHORE;

				/* If 'remove' button of rail build toolbar is active, disable it. */
				if (_remove_button_clicked) {
					Window *w = FindWindowById(WC_BUILD_TOOLBAR, TRANSPORT_RAIL);
					if (w != nullptr) ToggleRailButton_Remove(w);
				}

				break;

			case WID_BS_CONVERT:
				_convert_signal_button = !_convert_signal_button;
				break;

			case WID_BS_DRAG_SIGNALS_DENSITY_DECREASE:
				if (_settings_client.gui.drag_signals_density > 1) {
					_settings_client.gui.drag_signals_density--;
					SetWindowDirty(WC_GAME_OPTIONS, WN_GAME_OPTIONS_GAME_SETTINGS);
				}
				break;

			case WID_BS_DRAG_SIGNALS_DENSITY_INCREASE:
				if (_settings_client.gui.drag_signals_density < 20) {
					_settings_client.gui.drag_signals_density++;
					SetWindowDirty(WC_GAME_OPTIONS, WN_GAME_OPTIONS_GAME_SETTINGS);
				}
				break;

			default: break;
		}

		this->InvalidateData();
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (!gui_scope) return;
		this->LowerWidget((_cur_signal_variant == SIG_ELECTRIC ? WID_BS_ELECTRIC_NORM : WID_BS_SEMAPHORE_NORM) + _cur_signal_type);

		this->SetWidgetLoweredState(WID_BS_CONVERT, _convert_signal_button);

		this->SetWidgetDisabledState(WID_BS_DRAG_SIGNALS_DENSITY_DECREASE, _settings_client.gui.drag_signals_density == 1);
		this->SetWidgetDisabledState(WID_BS_DRAG_SIGNALS_DENSITY_INCREASE, _settings_client.gui.drag_signals_density == 20);
	}
};

/** Nested widget definition of the build signal window */
static const NWidgetPart _nested_signal_builder_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN), SetDataTip(STR_BUILD_SIGNAL_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(NWID_VERTICAL, NC_EQUALSIZE),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BS_SEMAPHORE_NORM), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_SEMAPHORE_NORM_TOOLTIP), EndContainer(), SetFill(1, 1),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BS_SEMAPHORE_ENTRY), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_SEMAPHORE_ENTRY_TOOLTIP), EndContainer(), SetFill(1, 1),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BS_SEMAPHORE_EXIT), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_SEMAPHORE_EXIT_TOOLTIP), EndContainer(), SetFill(1, 1),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BS_SEMAPHORE_COMBO), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_SEMAPHORE_COMBO_TOOLTIP), EndContainer(), SetFill(1, 1),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BS_SEMAPHORE_PBS), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_SEMAPHORE_PBS_TOOLTIP), EndContainer(), SetFill(1, 1),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BS_SEMAPHORE_PBS_OWAY), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_SEMAPHORE_PBS_OWAY_TOOLTIP), EndContainer(), SetFill(1, 1),
			NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_BS_CONVERT), SetDataTip(SPR_IMG_SIGNAL_CONVERT, STR_BUILD_SIGNAL_CONVERT_TOOLTIP), SetFill(1, 1),
		EndContainer(),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BS_ELECTRIC_NORM), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_ELECTRIC_NORM_TOOLTIP), EndContainer(), SetFill(1, 1),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BS_ELECTRIC_ENTRY), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_ELECTRIC_ENTRY_TOOLTIP), EndContainer(), SetFill(1, 1),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BS_ELECTRIC_EXIT), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_ELECTRIC_EXIT_TOOLTIP), EndContainer(), SetFill(1, 1),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BS_ELECTRIC_COMBO), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_ELECTRIC_COMBO_TOOLTIP), EndContainer(), SetFill(1, 1),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BS_ELECTRIC_PBS), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_ELECTRIC_PBS_TOOLTIP), EndContainer(), SetFill(1, 1),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BS_ELECTRIC_PBS_OWAY), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_ELECTRIC_PBS_OWAY_TOOLTIP), EndContainer(), SetFill(1, 1),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_DRAG_SIGNALS_DENSITY_TOOLTIP), SetFill(1, 1),
				NWidget(WWT_LABEL, COLOUR_DARK_GREEN, WID_BS_DRAG_SIGNALS_DENSITY_LABEL), SetDataTip(STR_ORANGE_INT, STR_BUILD_SIGNAL_DRAG_SIGNALS_DENSITY_TOOLTIP), SetFill(1, 1),
				NWidget(NWID_HORIZONTAL), SetPIP(2, 0, 2),
					NWidget(NWID_SPACER), SetFill(1, 0),
					NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_BS_DRAG_SIGNALS_DENSITY_DECREASE), SetMinimalSize(9, 12), SetDataTip(AWV_DECREASE, STR_BUILD_SIGNAL_DRAG_SIGNALS_DENSITY_DECREASE_TOOLTIP),
					NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_BS_DRAG_SIGNALS_DENSITY_INCREASE), SetMinimalSize(9, 12), SetDataTip(AWV_INCREASE, STR_BUILD_SIGNAL_DRAG_SIGNALS_DENSITY_INCREASE_TOOLTIP),
					NWidget(NWID_SPACER), SetFill(1, 0),
				EndContainer(),
				NWidget(NWID_SPACER), SetMinimalSize(0, 2), SetFill(1, 0),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

/** Signal selection window description */
static WindowDesc _signal_builder_desc(
	WDP_AUTO, "build_signal", 0, 0,
	WC_BUILD_SIGNAL, WC_BUILD_TOOLBAR,
	WDF_CONSTRUCTION,
	_nested_signal_builder_widgets, lengthof(_nested_signal_builder_widgets)
);

/**
 * Open the signal selection window
 */
static void ShowSignalBuilder(Window *parent)
{
	new BuildSignalWindow(&_signal_builder_desc, parent);
}

struct BuildRailDepotWindow : public PickerWindowBase {
	BuildRailDepotWindow(WindowDesc *desc, Window *parent) : PickerWindowBase(desc, parent)
	{
		this->InitNested(TRANSPORT_RAIL);
		this->LowerWidget(_build_depot_direction + WID_BRAD_DEPOT_NE);
	}

	virtual ~BuildRailDepotWindow() {
		CloseWindowById(WC_SELECT_DEPOT, VEH_TRAIN);
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		if (!IsInsideMM(widget, WID_BRAD_DEPOT_NE, WID_BRAD_DEPOT_NW + 1)) return;

		size->width  = ScaleGUITrad(64) + 2;
		size->height = ScaleGUITrad(48) + 2;
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		if (!IsInsideMM(widget, WID_BRAD_DEPOT_NE, WID_BRAD_DEPOT_NW + 1)) return;

		DrawTrainDepotSprite(r.left + 1 + ScaleGUITrad(31), r.bottom - ScaleGUITrad(31), widget - WID_BRAD_DEPOT_NE + DIAGDIR_NE, _cur_railtype);
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_BRAD_DEPOT_NE:
			case WID_BRAD_DEPOT_SE:
			case WID_BRAD_DEPOT_SW:
			case WID_BRAD_DEPOT_NW:
				CloseWindowById(WC_SELECT_DEPOT, VEH_TRAIN);
				this->RaiseWidget(_build_depot_direction + WID_BRAD_DEPOT_NE);
				_build_depot_direction = (DiagDirection)(widget - WID_BRAD_DEPOT_NE);
				this->LowerWidget(_build_depot_direction + WID_BRAD_DEPOT_NE);
				if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				break;
		}
	}

	void OnRealtimeTick(uint delta_ms) override
	{
		CheckRedrawDepotHighlight(this, VEH_TRAIN);
	}
};

/** Nested widget definition of the build rail depot window */
static const NWidgetPart _nested_build_depot_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN), SetDataTip(STR_BUILD_DEPOT_TRAIN_ORIENTATION_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN),
		NWidget(NWID_SPACER), SetMinimalSize(0, 3),
		NWidget(NWID_HORIZONTAL_LTR),
			NWidget(NWID_SPACER), SetMinimalSize(3, 0), SetFill(1, 0),
			NWidget(NWID_VERTICAL),
				NWidget(WWT_PANEL, COLOUR_GREY, WID_BRAD_DEPOT_NW), SetMinimalSize(66, 50), SetDataTip(0x0, STR_BUILD_DEPOT_TRAIN_ORIENTATION_TOOLTIP),
				EndContainer(),
				NWidget(NWID_SPACER), SetMinimalSize(0, 2),
				NWidget(WWT_PANEL, COLOUR_GREY, WID_BRAD_DEPOT_SW), SetMinimalSize(66, 50), SetDataTip(0x0, STR_BUILD_DEPOT_TRAIN_ORIENTATION_TOOLTIP),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_SPACER), SetMinimalSize(2, 0),
			NWidget(NWID_VERTICAL),
				NWidget(WWT_PANEL, COLOUR_GREY, WID_BRAD_DEPOT_NE), SetMinimalSize(66, 50), SetDataTip(0x0, STR_BUILD_DEPOT_TRAIN_ORIENTATION_TOOLTIP),
				EndContainer(),
				NWidget(NWID_SPACER), SetMinimalSize(0, 2),
				NWidget(WWT_PANEL, COLOUR_GREY, WID_BRAD_DEPOT_SE), SetMinimalSize(66, 50), SetDataTip(0x0, STR_BUILD_DEPOT_TRAIN_ORIENTATION_TOOLTIP),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_SPACER), SetMinimalSize(3, 0), SetFill(1, 0),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 3),
	EndContainer(),
};

static WindowDesc _build_depot_desc(
	WDP_AUTO, nullptr, 0, 0,
	WC_BUILD_DEPOT, WC_BUILD_TOOLBAR,
	WDF_CONSTRUCTION,
	_nested_build_depot_widgets, lengthof(_nested_build_depot_widgets)
);

static void ShowBuildTrainDepotPicker(Window *parent)
{
	new BuildRailDepotWindow(&_build_depot_desc, parent);
}

struct BuildRailWaypointWindow : PickerWindowBase {
	BuildRailWaypointWindow(WindowDesc *desc, Window *parent) : PickerWindowBase(desc, parent)
	{
		this->CreateNestedTree();

		NWidgetMatrix *matrix = this->GetWidget<NWidgetMatrix>(WID_BRW_WAYPOINT_MATRIX);
		matrix->SetScrollbar(this->GetScrollbar(WID_BRW_SCROLL));

		this->FinishInitNested(TRANSPORT_RAIL);

		matrix->SetCount(_waypoint_count);
		matrix->SetClicked(_cur_waypoint_type);
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_BRW_WAYPOINT_MATRIX:
				/* Three blobs high and wide. */
				size->width  += resize->width  * 2;
				size->height += resize->height * 2;

				/* Resizing in X direction only at blob size, but at pixel level in Y. */
				resize->height = 1;
				break;

			case WID_BRW_WAYPOINT:
				size->width  = ScaleGUITrad(64) + 2;
				size->height = ScaleGUITrad(58) + 2;
				break;
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (GB(widget, 0, 16)) {
			case WID_BRW_WAYPOINT: {
				byte type = GB(widget, 16, 16);
				const StationSpec *statspec = StationClass::Get(STAT_CLASS_WAYP)->GetSpec(type);
				DrawWaypointSprite(r.left + 1 + ScaleGUITrad(31), r.bottom - ScaleGUITrad(31), type, _cur_railtype);

				if (!IsStationAvailable(statspec)) {
					GfxFillRect(r.left + 1, r.top + 1, r.right - 1, r.bottom - 1, PC_BLACK, FILLRECT_CHECKER);
				}
			}
		}
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (GB(widget, 0, 16)) {
			case WID_BRW_WAYPOINT: {
				byte type = GB(widget, 16, 16);
				this->GetWidget<NWidgetMatrix>(WID_BRW_WAYPOINT_MATRIX)->SetClicked(_cur_waypoint_type);

				/* Check station availability callback */
				const StationSpec *statspec = StationClass::Get(STAT_CLASS_WAYP)->GetSpec(type);
				if (!IsStationAvailable(statspec)) return;

				_cur_waypoint_type = type;
				this->GetWidget<NWidgetMatrix>(WID_BRW_WAYPOINT_MATRIX)->SetClicked(_cur_waypoint_type);
				if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				break;
			}
		}
	}
};

/** Nested widget definition for the build NewGRF rail waypoint window */
static const NWidgetPart _nested_build_waypoint_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN), SetDataTip(STR_WAYPOINT_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_DEFSIZEBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_MATRIX, COLOUR_DARK_GREEN, WID_BRW_WAYPOINT_MATRIX), SetPIP(3, 2, 3), SetScrollbar(WID_BRW_SCROLL),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BRW_WAYPOINT), SetMinimalSize(66, 60), SetDataTip(0x0, STR_WAYPOINT_GRAPHICS_TOOLTIP), SetScrollbar(WID_BRW_SCROLL), EndContainer(),
		EndContainer(),
		NWidget(NWID_VERTICAL),
			NWidget(NWID_VSCROLLBAR, COLOUR_DARK_GREEN, WID_BRW_SCROLL),
			NWidget(WWT_RESIZEBOX, COLOUR_DARK_GREEN),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _build_waypoint_desc(
	WDP_AUTO, "build_waypoint", 0, 0,
	WC_BUILD_WAYPOINT, WC_BUILD_TOOLBAR,
	WDF_CONSTRUCTION,
	_nested_build_waypoint_widgets, lengthof(_nested_build_waypoint_widgets)
);

static void ShowBuildWaypointPicker(Window *parent)
{
	new BuildRailWaypointWindow(&_build_waypoint_desc, parent);
}

/**
 * Initialize rail building GUI settings
 */
void InitializeRailGui()
{
	_build_depot_direction = DIAGDIR_NW;
	_railstation.station_class = StationClassID::STAT_CLASS_DFLT;
}

/**
 * Re-initialize rail-build toolbar after toggling support for electric trains
 * @param disable Boolean whether electric trains are disabled (removed from the game)
 */
void ReinitGuiAfterToggleElrail(bool disable)
{
	extern RailType _last_built_railtype;
	if (disable && _last_built_railtype == RAILTYPE_ELECTRIC) {
		_last_built_railtype = _cur_railtype = RAILTYPE_RAIL;
		BuildRailToolbarWindow *w = dynamic_cast<BuildRailToolbarWindow *>(FindWindowById(WC_BUILD_TOOLBAR, TRANSPORT_RAIL));
		if (w != nullptr) w->ModifyRailType(_cur_railtype);
	}
	MarkWholeScreenDirty();
}

/** Set the initial (default) railtype to use */
static void SetDefaultRailGui()
{
	if (_local_company == COMPANY_SPECTATOR || !Company::IsValidID(_local_company)) return;

	extern RailType _last_built_railtype;
	RailType rt;
	switch (_settings_client.gui.default_rail_type) {
		case 2: {
			/* Find the most used rail type */
			uint count[RAILTYPE_END];
			memset(count, 0, sizeof(count));
			for (TileIndex t = 0; t < MapSize(); t++) {
				if (IsTileType(t, MP_RAILWAY) || IsLevelCrossingTile(t) || HasStationTileRail(t) ||
						(IsTileType(t, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL)) {
					count[GetRailType(t)]++;
				}
			}

			rt = static_cast<RailType>(std::max_element(count + RAILTYPE_BEGIN, count + RAILTYPE_END) - count);
			if (count[rt] > 0) break;

			/* No rail, just get the first available one */
			FALLTHROUGH;
		}
		case 0: {
			/* Use first available type */
			std::vector<RailType>::const_iterator it = std::find_if(_sorted_railtypes.begin(), _sorted_railtypes.end(),
					[](RailType r){ return HasRailtypeAvail(_local_company, r); });
			rt = it != _sorted_railtypes.end() ? *it : RAILTYPE_BEGIN;
			break;
		}
		case 1: {
			/* Use last available type */
			std::vector<RailType>::const_reverse_iterator it = std::find_if(_sorted_railtypes.rbegin(), _sorted_railtypes.rend(),
					[](RailType r){ return HasRailtypeAvail(_local_company, r); });
			rt = it != _sorted_railtypes.rend() ? *it : RAILTYPE_BEGIN;
			break;
		}
		default:
			NOT_REACHED();
	}

	_last_built_railtype = _cur_railtype = rt;
	BuildRailToolbarWindow *w = dynamic_cast<BuildRailToolbarWindow *>(FindWindowById(WC_BUILD_TOOLBAR, TRANSPORT_RAIL));
	if (w != nullptr) w->ModifyRailType(_cur_railtype);
}

/**
 * Updates the current signal variant used in the signal GUI
 * to the one adequate to current year.
 * @param new_value needed to be called when a setting changes
 */
void ResetSignalVariant(int32 new_value)
{
	SignalVariant new_variant = (_cur_year < _settings_client.gui.semaphore_build_before ? SIG_SEMAPHORE : SIG_ELECTRIC);

	if (new_variant != _cur_signal_variant) {
		Window *w = FindWindowById(WC_BUILD_SIGNAL, 0);
		if (w != nullptr) {
			w->SetDirty();
			w->RaiseWidget((_cur_signal_variant == SIG_ELECTRIC ? WID_BS_ELECTRIC_NORM : WID_BS_SEMAPHORE_NORM) + _cur_signal_type);
		}
		_cur_signal_variant = new_variant;
	}
}

/**
 * Resets the rail GUI - sets default railtype to build
 * and resets the signal GUI
 */
void InitializeRailGUI()
{
	SetDefaultRailGui();

	_convert_signal_button = false;
	_cur_signal_type = _default_signal_type[_settings_client.gui.default_signal_type];
	ResetSignalVariant();
}

/**
 * Create a drop down list for all the rail types of the local company.
 * @param for_replacement Whether this list is for the replacement window.
 * @param all_option Whether to add an 'all types' item.
 * @return The populated and sorted #DropDownList.
 */
DropDownList GetRailTypeDropDownList(bool for_replacement, bool all_option)
{
	RailTypes used_railtypes;
	RailTypes avail_railtypes;

	const Company *c = Company::Get(_local_company);

	/* Find the used railtypes. */
	if (for_replacement) {
		avail_railtypes = GetCompanyRailtypes(c->index, false);
		used_railtypes  = GetRailTypes(false);
	} else {
		avail_railtypes = c->avail_railtypes;
		used_railtypes  = GetRailTypes(true);
	}

	DropDownList list;

	if (all_option) {
		list.emplace_back(new DropDownListStringItem(STR_REPLACE_ALL_RAILTYPE, INVALID_RAILTYPE, false));
	}

	Dimension d = { 0, 0 };
	/* Get largest icon size, to ensure text is aligned on each menu item. */
	if (!for_replacement) {
		for (const auto &rt : _sorted_railtypes) {
			if (!HasBit(used_railtypes, rt)) continue;
			const RailtypeInfo *rti = GetRailTypeInfo(rt);
			d = maxdim(d, GetSpriteSize(rti->gui_sprites.build_x_rail));
		}
	}

	for (const auto &rt : _sorted_railtypes) {
		/* If it's not used ever, don't show it to the user. */
		if (!HasBit(used_railtypes, rt)) continue;

		const RailtypeInfo *rti = GetRailTypeInfo(rt);

		StringID str = for_replacement ? rti->strings.replace_text : (rti->max_speed > 0 ? STR_TOOLBAR_RAILTYPE_VELOCITY : STR_JUST_STRING);
		DropDownListParamStringItem *item;
		if (for_replacement) {
			item = new DropDownListParamStringItem(str, rt, !HasBit(avail_railtypes, rt));
		} else {
			DropDownListIconItem *iconitem = new DropDownListIconItem(rti->gui_sprites.build_x_rail, PAL_NONE, str, rt, !HasBit(avail_railtypes, rt));
			iconitem->SetDimension(d);
			item = iconitem;
		}
		item->SetParam(0, rti->strings.menu_text);
		item->SetParam(1, rti->max_speed);
		list.emplace_back(item);
	}

	if (list.size() == 0) {
		/* Empty dropdowns are not allowed */
		list.emplace_back(new DropDownListStringItem(STR_NONE, INVALID_RAILTYPE, true));
	}

	return list;
}
