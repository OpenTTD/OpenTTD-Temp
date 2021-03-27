/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file road_gui.cpp GUI for building roads. */

#include "stdafx.h"
#include "gui.h"
#include "window_gui.h"
#include "station_gui.h"
#include "terraform_gui.h"
#include "viewport_func.h"
#include "command_func.h"
#include "road_cmd.h"
#include "station_func.h"
#include "window_func.h"
#include "vehicle_func.h"
#include "sound_func.h"
#include "company_func.h"
#include "tunnelbridge.h"
#include "tunnelbridge_map.h"
#include "tilehighlight_func.h"
#include "company_base.h"
#include "hotkeys.h"
#include "road_gui.h"
#include "zoom_func.h"
#include "engine_base.h"
#include "strings_func.h"
#include "core/geometry_func.hpp"
#include "date_func.h"
#include "depot_func.h"

#include "widgets/road_widget.h"

#include "table/strings.h"

#include "safeguards.h"

static void ShowRVStationPicker(Window *parent, RoadStopType rs);
static void ShowRoadDepotPicker(Window *parent);

static bool _remove_button_clicked;
static bool _one_way_button_clicked;

/**
 * Define the values of the RoadFlags
 * @see CmdBuildLongRoad
 */
enum RoadFlags {
	RF_NONE             = 0x00,
	RF_START_HALFROAD_Y = 0x01,    // The start tile in Y-dir should have only a half road
	RF_END_HALFROAD_Y   = 0x02,    // The end tile in Y-dir should have only a half road
	RF_DIR_Y            = 0x04,    // The direction is Y-dir
	RF_DIR_X            = RF_NONE, // Dummy; Dir X is set when RF_DIR_Y is not set
	RF_START_HALFROAD_X = 0x08,    // The start tile in X-dir should have only a half road
	RF_END_HALFROAD_X   = 0x10,    // The end tile in X-dir should have only a half road
};
DECLARE_ENUM_AS_BIT_SET(RoadFlags)

static RoadFlags _place_road_flag;

static RoadType _cur_roadtype;

static DiagDirection _road_depot_orientation;
static DiagDirection _road_station_picker_orientation;

void CcPlaySound_CONSTRUCTION_OTHER(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2, uint32 cmd)
{
	if (result.Succeeded() && _settings_client.sound.confirm) SndPlayTileFx(SND_1F_CONSTRUCTION_OTHER, tile);
}

/**
 * Callback to start placing a bridge.
 * @param tile Start tile of the bridge.
 */
static void PlaceRoad_Bridge(TileIndex tile, Window *w)
{
	if (IsBridgeTile(tile)) {
		TileIndex other_tile = GetOtherTunnelBridgeEnd(tile);
		Point pt = {0, 0};
		w->OnPlaceMouseUp(VPM_X_OR_Y, DDSP_BUILD_BRIDGE, pt, other_tile, tile);
	} else {
		VpStartPlaceSizing(tile, VPM_X_OR_Y, DDSP_BUILD_BRIDGE);
	}
}

/**
 * Callback executed after a build road tunnel command has been called.
 *
 * @param result Whether the build succeeded.
 * @param start_tile Starting tile of the tunnel.
 * @param p1 bit 0-3 railtype or roadtypes
 *           bit 8-9 transport type
 * @param p2 unused
 * @param cmd unused
 */
void CcBuildRoadTunnel(const CommandCost &result, TileIndex start_tile, uint32 p1, uint32 p2, uint32 cmd)
{
	if (result.Succeeded()) {
		if (_settings_client.sound.confirm) SndPlayTileFx(SND_1F_CONSTRUCTION_OTHER, start_tile);
		if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();

		DiagDirection start_direction = ReverseDiagDir(GetTunnelBridgeDirection(start_tile));
		ConnectRoadToStructure(start_tile, start_direction);

		TileIndex end_tile = GetOtherTunnelBridgeEnd(start_tile);
		DiagDirection end_direction = ReverseDiagDir(GetTunnelBridgeDirection(end_tile));
		ConnectRoadToStructure(end_tile, end_direction);
	} else {
		SetRedErrorSquare(_build_tunnel_endtile);
	}
}

/**
 * If required, connects a new structure to an existing road or tram by building the missing roadbit.
 * @param tile Tile containing the structure to connect.
 * @param direction Direction to check.
 */
void ConnectRoadToStructure(TileIndex tile, DiagDirection direction)
{
	tile += TileOffsByDiagDir(direction);
	/* if there is a roadpiece just outside of the station entrance, build a connecting route */
	if (IsNormalRoadTile(tile)) {
		if (GetRoadBits(tile, GetRoadTramType(_cur_roadtype)) != ROAD_NONE) {
			DoCommandP(tile, _cur_roadtype << 4 | DiagDirToRoadBits(ReverseDiagDir(direction)), 0, CMD_BUILD_ROAD);
		}
	}
}

void CcRoadDepot(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2, uint32 cmd)
{
	if (result.Failed()) return;

	DiagDirection dir = (DiagDirection)GB(p1, 0, 2);
	if (_settings_client.sound.confirm) SndPlayTileFx(SND_1F_CONSTRUCTION_OTHER, tile);
	if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();

	TileArea ta(tile, p2);
	for (TileIndex tile : ta) {
		ConnectRoadToStructure(tile, dir);
	}
}

/**
 * Command callback for building road stops.
 * @param result Result of the build road stop command.
 * @param tile Start tile.
 * @param p1 bit 0..7: Width of the road stop.
 *           bit 8..15: Length of the road stop.
 * @param p2 bit 0: 0 For bus stops, 1 for truck stops.
 *           bit 1: 0 For normal stops, 1 for drive-through.
 *           bit 2: Allow stations directly adjacent to other stations.
 *           bit 3..4: Entrance direction (#DiagDirection) for normal stops.
 *           bit 3: #Axis of the road for drive-through stops.
 *           bit 5..9: The roadtype.
 *           bit 16..31: Station ID to join (NEW_STATION if build new one).
 * @param cmd Unused.
 * @see CmdBuildRoadStop
 */
void CcRoadStop(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2, uint32 cmd)
{
	if (result.Failed()) return;

	DiagDirection dir = (DiagDirection)GB(p2, 3, 2);
	if (_settings_client.sound.confirm) SndPlayTileFx(SND_1F_CONSTRUCTION_OTHER, tile);
	if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();
	TileArea roadstop_area(tile, GB(p1, 0, 8), GB(p1, 8, 8));
	for (TileIndex cur_tile : roadstop_area) {
		ConnectRoadToStructure(cur_tile, dir);
		/* For a drive-through road stop build connecting road for other entrance. */
		if (HasBit(p2, 1)) ConnectRoadToStructure(cur_tile, ReverseDiagDir(dir));
	}
}

/**
 * Place a new road stop.
 * @param start_tile First tile of the area.
 * @param end_tile Last tile of the area.
 * @param p2 bit 0: 0 For bus stops, 1 for truck stops.
 *           bit 2: Allow stations directly adjacent to other stations.
 *           bit 5..10: The roadtypes.
 * @param cmd Command to use.
 * @see CcRoadStop()
 */
static void PlaceRoadStop(TileIndex start_tile, TileIndex end_tile, uint32 p2, uint32 cmd)
{
	uint8 ddir = _road_station_picker_orientation;
	SB(p2, 16, 16, INVALID_STATION); // no station to join

	if (ddir >= DIAGDIR_END) {
		SetBit(p2, 1); // It's a drive-through stop.
		ddir -= DIAGDIR_END; // Adjust picker result to actual direction.
	}
	p2 |= ddir << 3; // Set the DiagDirecion into p2 bits 3 and 4.

	TileArea ta(start_tile, end_tile);
	CommandContainer cmdcont = { ta.tile, (uint32)(ta.w | ta.h << 8), p2, cmd, CcRoadStop, "" };
	ShowSelectStationIfNeeded(cmdcont, ta);
}

/**
 * Callback for placing a bus station.
 * @param tile Position to place the station.
 */
static void PlaceRoad_BusStation(TileIndex tile)
{
	if (_remove_button_clicked) {
		VpStartPlaceSizing(tile, VPM_X_AND_Y, DDSP_REMOVE_BUSSTOP);
	} else {
		if (_road_station_picker_orientation < DIAGDIR_END) { // Not a drive-through stop.
			VpStartPlaceSizing(tile, (DiagDirToAxis(_road_station_picker_orientation) == AXIS_X) ? VPM_X_LIMITED : VPM_Y_LIMITED, DDSP_BUILD_BUSSTOP);
		} else {
			VpStartPlaceSizing(tile, VPM_X_AND_Y_LIMITED, DDSP_BUILD_BUSSTOP);
		}
		VpSetPlaceSizingLimit(_settings_game.station.station_spread);
	}
}

/**
 * Callback for placing a truck station.
 * @param tile Position to place the station.
 */
static void PlaceRoad_TruckStation(TileIndex tile)
{
	if (_remove_button_clicked) {
		VpStartPlaceSizing(tile, VPM_X_AND_Y, DDSP_REMOVE_TRUCKSTOP);
	} else {
		if (_road_station_picker_orientation < DIAGDIR_END) { // Not a drive-through stop.
			VpStartPlaceSizing(tile, (DiagDirToAxis(_road_station_picker_orientation) == AXIS_X) ? VPM_X_LIMITED : VPM_Y_LIMITED, DDSP_BUILD_TRUCKSTOP);
		} else {
			VpStartPlaceSizing(tile, VPM_X_AND_Y_LIMITED, DDSP_BUILD_TRUCKSTOP);
		}
		VpSetPlaceSizingLimit(_settings_game.station.station_spread);
	}
}

typedef void OnButtonClick(Window *w);

/**
 * Toggles state of the Remove button of Build road toolbar
 * @param w window the button belongs to
 */
static void ToggleRoadButton_Remove(Window *w)
{
	w->ToggleWidgetLoweredState(WID_ROT_REMOVE);
	w->SetWidgetDirty(WID_ROT_REMOVE);
	_remove_button_clicked = w->IsWidgetLowered(WID_ROT_REMOVE);
	SetSelectionRed(_remove_button_clicked);
}

/**
 * Updates the Remove button because of Ctrl state change
 * @param w window the button belongs to
 * @return true iff the remove button was changed
 */
static bool RoadToolbar_CtrlChanged(Window *w)
{
	if (w->IsWidgetDisabled(WID_ROT_REMOVE)) return false;

	/* allow ctrl to switch remove mode only for these widgets */
	for (uint i = WID_ROT_ROAD_X; i <= WID_ROT_AUTOROAD; i++) {
		if (w->IsWidgetLowered(i)) {
			ToggleRoadButton_Remove(w);
			return true;
		}
	}

	return false;
}

/** Road toolbar window handler. */
struct BuildRoadToolbarWindow : Window {
	RoadType roadtype;          ///< Road type to build.
	const RoadTypeInfo *rti;    ///< Information about current road type
	int last_started_action;    ///< Last started user action.

	BuildRoadToolbarWindow(WindowDesc *desc, WindowNumber window_number) : Window(desc)
	{
		this->Initialize(_cur_roadtype);
		this->InitNested(window_number);
		this->SetupRoadToolbar();
		this->SetWidgetDisabledState(WID_ROT_REMOVE, true);

		if (RoadTypeIsRoad(this->roadtype)) {
			this->SetWidgetDisabledState(WID_ROT_ONE_WAY, true);
		}

		this->OnInvalidateData();
		this->last_started_action = WIDGET_LIST_END;

		if (_settings_client.gui.link_terraform_toolbar) ShowTerraformToolbar(this);
	}

	void Close() override
	{
		if (_game_mode == GM_NORMAL && (this->IsWidgetLowered(WID_ROT_BUS_STATION) || this->IsWidgetLowered(WID_ROT_TRUCK_STATION))) SetViewportCatchmentStation(nullptr, true);
		if (_settings_client.gui.link_terraform_toolbar) CloseWindowById(WC_SCEN_LAND_GEN, 0, false);
		if (_game_mode == GM_NORMAL && this->IsWidgetLowered(WID_ROT_DEPOT)) SetViewportHighlightDepot(INVALID_DEPOT, true);
		this->Window::Close();
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (!gui_scope) return;
		RoadTramType rtt = GetRoadTramType(this->roadtype);

		bool can_build = CanBuildVehicleInfrastructure(VEH_ROAD, rtt);
		this->SetWidgetsDisabledState(!can_build,
			WID_ROT_DEPOT,
			WID_ROT_BUS_STATION,
			WID_ROT_TRUCK_STATION,
			WIDGET_LIST_END);
		if (!can_build) {
			CloseWindowById(WC_BUS_STATION, TRANSPORT_ROAD);
			CloseWindowById(WC_TRUCK_STATION, TRANSPORT_ROAD);
			CloseWindowById(WC_BUILD_DEPOT, TRANSPORT_ROAD);
		}

		if (_game_mode != GM_EDITOR) {
			if (!can_build) {
				/* Show in the tooltip why this button is disabled. */
				this->GetWidget<NWidgetCore>(WID_ROT_DEPOT)->SetToolTip(STR_TOOLBAR_DISABLED_NO_VEHICLE_AVAILABLE);
				this->GetWidget<NWidgetCore>(WID_ROT_BUS_STATION)->SetToolTip(STR_TOOLBAR_DISABLED_NO_VEHICLE_AVAILABLE);
				this->GetWidget<NWidgetCore>(WID_ROT_TRUCK_STATION)->SetToolTip(STR_TOOLBAR_DISABLED_NO_VEHICLE_AVAILABLE);
			} else {
				this->GetWidget<NWidgetCore>(WID_ROT_DEPOT)->SetToolTip(rtt == RTT_ROAD ? STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_VEHICLE_DEPOT : STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAM_VEHICLE_DEPOT);
				this->GetWidget<NWidgetCore>(WID_ROT_BUS_STATION)->SetToolTip(rtt == RTT_ROAD ? STR_ROAD_TOOLBAR_TOOLTIP_BUILD_BUS_STATION : STR_ROAD_TOOLBAR_TOOLTIP_BUILD_PASSENGER_TRAM_STATION);
				this->GetWidget<NWidgetCore>(WID_ROT_TRUCK_STATION)->SetToolTip(rtt == RTT_ROAD ? STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRUCK_LOADING_BAY : STR_ROAD_TOOLBAR_TOOLTIP_BUILD_CARGO_TRAM_STATION);
			}
		}
	}

	void Initialize(RoadType roadtype)
	{
		assert(roadtype < ROADTYPE_END);
		this->roadtype = roadtype;
		this->rti = GetRoadTypeInfo(this->roadtype);
	}

	/**
	 * Configures the road toolbar for roadtype given
	 * @param roadtype the roadtype to display
	 */
	void SetupRoadToolbar()
	{
		this->GetWidget<NWidgetCore>(WID_ROT_ROAD_X)->widget_data = rti->gui_sprites.build_x_road;
		this->GetWidget<NWidgetCore>(WID_ROT_ROAD_Y)->widget_data = rti->gui_sprites.build_y_road;
		this->GetWidget<NWidgetCore>(WID_ROT_AUTOROAD)->widget_data = rti->gui_sprites.auto_road;
		if (_game_mode != GM_EDITOR) {
			this->GetWidget<NWidgetCore>(WID_ROT_DEPOT)->widget_data = rti->gui_sprites.build_depot;
		}
		this->GetWidget<NWidgetCore>(WID_ROT_CONVERT_ROAD)->widget_data = rti->gui_sprites.convert_road;
		this->GetWidget<NWidgetCore>(WID_ROT_BUILD_TUNNEL)->widget_data = rti->gui_sprites.build_tunnel;
	}

	/**
	 * Switch to another road type.
	 * @param roadtype New road type.
	 */
	void ModifyRoadType(RoadType roadtype)
	{
		this->Initialize(roadtype);
		this->SetupRoadToolbar();
		this->ReInit();
	}

	void SetStringParameters(int widget) const override
	{
		if (widget == WID_ROT_CAPTION) {
			if (this->rti->max_speed > 0) {
				SetDParam(0, STR_TOOLBAR_RAILTYPE_VELOCITY);
				SetDParam(1, this->rti->strings.toolbar_caption);
				SetDParam(2, this->rti->max_speed / 2);
			} else {
				SetDParam(0, this->rti->strings.toolbar_caption);
			}
		}
	}

	/**
	 * Update the remove button lowered state of the road toolbar
	 *
	 * @param clicked_widget The widget which the client clicked just now
	 */
	void UpdateOptionWidgetStatus(RoadToolbarWidgets clicked_widget)
	{
		/* The remove and the one way button state is driven
		 * by the other buttons so they don't act on themselves.
		 * Both are only valid if they are able to apply as options. */
		switch (clicked_widget) {
			case WID_ROT_REMOVE:
				if (RoadTypeIsRoad(this->roadtype)) {
					this->RaiseWidget(WID_ROT_ONE_WAY);
					this->SetWidgetDirty(WID_ROT_ONE_WAY);
				}

				break;

			case WID_ROT_ONE_WAY:
				this->RaiseWidget(WID_ROT_REMOVE);
				this->SetWidgetDirty(WID_ROT_REMOVE);
				break;

			case WID_ROT_BUS_STATION:
			case WID_ROT_TRUCK_STATION:
				if (RoadTypeIsRoad(this->roadtype)) this->DisableWidget(WID_ROT_ONE_WAY);
				this->SetWidgetDisabledState(WID_ROT_REMOVE, !this->IsWidgetLowered(clicked_widget));
				break;

			case WID_ROT_ROAD_X:
			case WID_ROT_ROAD_Y:
			case WID_ROT_AUTOROAD:
				this->SetWidgetDisabledState(WID_ROT_REMOVE, !this->IsWidgetLowered(clicked_widget));
				if (RoadTypeIsRoad(this->roadtype)) {
					this->SetWidgetDisabledState(WID_ROT_ONE_WAY, !this->IsWidgetLowered(clicked_widget));
				}
				break;

			default:
				/* When any other buttons than road/station, raise and
				 * disable the removal button */
				this->SetWidgetDisabledState(WID_ROT_REMOVE, true);
				this->SetWidgetLoweredState(WID_ROT_REMOVE, false);

				if (RoadTypeIsRoad(this->roadtype)) {
					this->SetWidgetDisabledState(WID_ROT_ONE_WAY, true);
					this->SetWidgetLoweredState(WID_ROT_ONE_WAY, false);
				}

				break;
		}
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		_remove_button_clicked = false;
		_one_way_button_clicked = false;
		switch (widget) {
			case WID_ROT_ROAD_X:
				HandlePlacePushButton(this, WID_ROT_ROAD_X, this->rti->cursor.road_nwse, HT_RECT);
				this->last_started_action = widget;
				break;

			case WID_ROT_ROAD_Y:
				HandlePlacePushButton(this, WID_ROT_ROAD_Y, this->rti->cursor.road_swne, HT_RECT);
				this->last_started_action = widget;
				break;

			case WID_ROT_AUTOROAD:
				HandlePlacePushButton(this, WID_ROT_AUTOROAD, this->rti->cursor.autoroad, HT_RECT);
				this->last_started_action = widget;
				break;

			case WID_ROT_DEMOLISH:
				HandlePlacePushButton(this, WID_ROT_DEMOLISH, ANIMCURSOR_DEMOLISH, HT_RECT | HT_DIAGONAL);
				this->last_started_action = widget;
				break;

			case WID_ROT_DEPOT:
				if (HandlePlacePushButton(this, WID_ROT_DEPOT, this->rti->cursor.depot, HT_RECT)) {
					ShowRoadDepotPicker(this);
					this->last_started_action = widget;
				}
				break;

			case WID_ROT_BUS_STATION:
				if (HandlePlacePushButton(this, WID_ROT_BUS_STATION, SPR_CURSOR_BUS_STATION, HT_RECT)) {
					ShowRVStationPicker(this, ROADSTOP_BUS);
					this->last_started_action = widget;
				}
				break;

			case WID_ROT_TRUCK_STATION:
				if (HandlePlacePushButton(this, WID_ROT_TRUCK_STATION, SPR_CURSOR_TRUCK_STATION, HT_RECT)) {
					ShowRVStationPicker(this, ROADSTOP_TRUCK);
					this->last_started_action = widget;
				}
				break;

			case WID_ROT_ONE_WAY:
				if (this->IsWidgetDisabled(WID_ROT_ONE_WAY)) return;
				this->SetDirty();
				this->ToggleWidgetLoweredState(WID_ROT_ONE_WAY);
				SetSelectionRed(false);
				break;

			case WID_ROT_BUILD_BRIDGE:
				HandlePlacePushButton(this, WID_ROT_BUILD_BRIDGE, SPR_CURSOR_BRIDGE, HT_RECT);
				this->last_started_action = widget;
				break;

			case WID_ROT_BUILD_TUNNEL:
				HandlePlacePushButton(this, WID_ROT_BUILD_TUNNEL, this->rti->cursor.tunnel, HT_SPECIAL);
				this->last_started_action = widget;
				break;

			case WID_ROT_REMOVE:
				if (this->IsWidgetDisabled(WID_ROT_REMOVE)) return;

				CloseWindowById(WC_SELECT_STATION, 0);
				ToggleRoadButton_Remove(this);
				if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
				break;

			case WID_ROT_CONVERT_ROAD:
				HandlePlacePushButton(this, WID_ROT_CONVERT_ROAD, this->rti->cursor.convert_road, HT_RECT);
				this->last_started_action = widget;
				break;

			default: NOT_REACHED();
		}
		this->UpdateOptionWidgetStatus((RoadToolbarWidgets)widget);
		if (_ctrl_pressed) RoadToolbar_CtrlChanged(this);
	}

	EventState OnHotkey(int hotkey) override
	{
		MarkTileDirtyByTile(TileVirtXY(_thd.pos.x, _thd.pos.y)); // redraw tile selection
		return Window::OnHotkey(hotkey);
	}

	void OnPlaceObject(Point pt, TileIndex tile) override
	{
		_remove_button_clicked = this->IsWidgetLowered(WID_ROT_REMOVE);
		_one_way_button_clicked = RoadTypeIsRoad(this->roadtype) ? this->IsWidgetLowered(WID_ROT_ONE_WAY) : false;
		switch (this->last_started_action) {
			case WID_ROT_ROAD_X:
				_place_road_flag = RF_DIR_X;
				if (_tile_fract_coords.x >= 8) _place_road_flag |= RF_START_HALFROAD_X;
				VpStartPlaceSizing(tile, VPM_FIX_Y, DDSP_PLACE_ROAD_X_DIR);
				break;

			case WID_ROT_ROAD_Y:
				_place_road_flag = RF_DIR_Y;
				if (_tile_fract_coords.y >= 8) _place_road_flag |= RF_START_HALFROAD_Y;
				VpStartPlaceSizing(tile, VPM_FIX_X, DDSP_PLACE_ROAD_Y_DIR);
				break;

			case WID_ROT_AUTOROAD:
				_place_road_flag = RF_NONE;
				if (_tile_fract_coords.x >= 8) _place_road_flag |= RF_START_HALFROAD_X;
				if (_tile_fract_coords.y >= 8) _place_road_flag |= RF_START_HALFROAD_Y;
				VpStartPlaceSizing(tile, VPM_X_OR_Y, DDSP_PLACE_AUTOROAD);
				break;

			case WID_ROT_DEMOLISH:
				PlaceProc_DemolishArea(tile);
				break;

			case WID_ROT_DEPOT:
				VpSetPlaceSizingLimit(_settings_game.station.station_spread);
				VpStartPlaceSizing(tile, (DiagDirToAxis(_road_depot_orientation) == 0) ? VPM_X_LIMITED : VPM_Y_LIMITED, DDSP_BUILD_DEPOT);
				break;

			case WID_ROT_BUS_STATION:
				PlaceRoad_BusStation(tile);
				break;

			case WID_ROT_TRUCK_STATION:
				PlaceRoad_TruckStation(tile);
				break;

			case WID_ROT_BUILD_BRIDGE:
				PlaceRoad_Bridge(tile, this);
				break;

			case WID_ROT_BUILD_TUNNEL:
				DoCommandP(tile, _cur_roadtype | (TRANSPORT_ROAD << 8), 0,
						CMD_BUILD_TUNNEL | CMD_MSG(STR_ERROR_CAN_T_BUILD_TUNNEL_HERE), CcBuildRoadTunnel);
				break;

			case WID_ROT_CONVERT_ROAD:
				VpStartPlaceSizing(tile, VPM_X_AND_Y, DDSP_CONVERT_ROAD);
				break;

			default: NOT_REACHED();
		}
	}

	void OnPlaceObjectAbort() override
	{
		if (_game_mode != GM_EDITOR && (this->IsWidgetLowered(WID_ROT_BUS_STATION) || this->IsWidgetLowered(WID_ROT_TRUCK_STATION))) SetViewportCatchmentStation(nullptr, true);

		if (_game_mode == GM_NORMAL && this->IsWidgetLowered(WID_ROT_DEPOT)) SetViewportHighlightDepot(INVALID_DEPOT, true);

		this->RaiseButtons();
		this->SetWidgetDisabledState(WID_ROT_REMOVE, true);
		this->SetWidgetDirty(WID_ROT_REMOVE);

		if (RoadTypeIsRoad(this->roadtype)) {
			this->SetWidgetDisabledState(WID_ROT_ONE_WAY, true);
			this->SetWidgetDirty(WID_ROT_ONE_WAY);
		}

		CloseWindowById(WC_BUS_STATION, TRANSPORT_ROAD);
		CloseWindowById(WC_TRUCK_STATION, TRANSPORT_ROAD);
		CloseWindowById(WC_BUILD_DEPOT, TRANSPORT_ROAD);
		CloseWindowById(WC_SELECT_STATION, 0);
		CloseWindowById(WC_SELECT_DEPOT, VEH_ROAD);
		CloseWindowByClass(WC_BUILD_BRIDGE);
	}

	void OnPlaceDrag(ViewportPlaceMethod select_method, ViewportDragDropSelectionProcess select_proc, Point pt) override
	{
		/* Here we update the end tile flags
		 * of the road placement actions.
		 * At first we reset the end halfroad
		 * bits and if needed we set them again. */
		switch (select_proc) {
			case DDSP_PLACE_ROAD_X_DIR:
				_place_road_flag &= ~RF_END_HALFROAD_X;
				if (pt.x & 8) _place_road_flag |= RF_END_HALFROAD_X;
				break;

			case DDSP_PLACE_ROAD_Y_DIR:
				_place_road_flag &= ~RF_END_HALFROAD_Y;
				if (pt.y & 8) _place_road_flag |= RF_END_HALFROAD_Y;
				break;

			case DDSP_PLACE_AUTOROAD:
				_place_road_flag &= ~(RF_END_HALFROAD_Y | RF_END_HALFROAD_X);
				if (pt.y & 8) _place_road_flag |= RF_END_HALFROAD_Y;
				if (pt.x & 8) _place_road_flag |= RF_END_HALFROAD_X;

				/* For autoroad we need to update the
				 * direction of the road */
				if (_thd.size.x > _thd.size.y || (_thd.size.x == _thd.size.y &&
						( (_tile_fract_coords.x < _tile_fract_coords.y && (_tile_fract_coords.x + _tile_fract_coords.y) < 16) ||
						(_tile_fract_coords.x > _tile_fract_coords.y && (_tile_fract_coords.x + _tile_fract_coords.y) > 16) ))) {
					/* Set dir = X */
					_place_road_flag &= ~RF_DIR_Y;
				} else {
					/* Set dir = Y */
					_place_road_flag |= RF_DIR_Y;
				}

				break;

			default:
				break;
		}

		VpSelectTilesWithMethod(pt.x, pt.y, select_method);
	}

	void OnPlaceMouseUp(ViewportPlaceMethod select_method, ViewportDragDropSelectionProcess select_proc, Point pt, TileIndex start_tile, TileIndex end_tile) override
	{
		if (pt.x != -1) {
			switch (select_proc) {
				default: NOT_REACHED();
				case DDSP_BUILD_BRIDGE:
					if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();
					ShowBuildBridgeWindow(start_tile, end_tile, TRANSPORT_ROAD, _cur_roadtype);
					break;

				case DDSP_DEMOLISH_AREA:
					GUIPlaceProcDragXY(select_proc, start_tile, end_tile);
					break;

				case DDSP_PLACE_ROAD_X_DIR:
				case DDSP_PLACE_ROAD_Y_DIR:
				case DDSP_PLACE_AUTOROAD:
					/* Flag description:
					 * Use the first three bits (0x07) if dir == Y
					 * else use the last 2 bits (X dir has
					 * not the 3rd bit set) */

					/* Even if _cur_roadtype_id is a uint8 we only use 5 bits so
					 * we could ignore the last 3 bits and reuse them for other
					 * flags */
					_place_road_flag = (RoadFlags)((_place_road_flag & RF_DIR_Y) ? (_place_road_flag & 0x07) : (_place_road_flag >> 3));

					DoCommandP(start_tile, end_tile, _place_road_flag | (_cur_roadtype << 3) | (_one_way_button_clicked << 10),
							_remove_button_clicked ?
							CMD_REMOVE_LONG_ROAD | CMD_MSG(this->rti->strings.err_remove_road) :
							CMD_BUILD_LONG_ROAD | CMD_MSG(this->rti->strings.err_build_road), CcPlaySound_CONSTRUCTION_OTHER);
					break;

				case DDSP_BUILD_BUSSTOP:
				case DDSP_REMOVE_BUSSTOP:
					if (this->IsWidgetLowered(WID_ROT_BUS_STATION)) {
						if (_remove_button_clicked) {
							TileArea ta(start_tile, end_tile);
							DoCommandP(ta.tile, ta.w | ta.h << 8, (_ctrl_pressed << 1) | ROADSTOP_BUS, CMD_REMOVE_ROAD_STOP | CMD_MSG(this->rti->strings.err_remove_station[ROADSTOP_BUS]), CcPlaySound_CONSTRUCTION_OTHER);
						} else {
							PlaceRoadStop(start_tile, end_tile, _cur_roadtype << 5 | (_ctrl_pressed << 2) | ROADSTOP_BUS, CMD_BUILD_ROAD_STOP | CMD_MSG(this->rti->strings.err_build_station[ROADSTOP_BUS]));
						}
					}
					break;

				case DDSP_BUILD_TRUCKSTOP:
				case DDSP_REMOVE_TRUCKSTOP:
					if (this->IsWidgetLowered(WID_ROT_TRUCK_STATION)) {
						if (_remove_button_clicked) {
							TileArea ta(start_tile, end_tile);
							DoCommandP(ta.tile, ta.w | ta.h << 8, (_ctrl_pressed << 1) | ROADSTOP_TRUCK, CMD_REMOVE_ROAD_STOP | CMD_MSG(this->rti->strings.err_remove_station[ROADSTOP_TRUCK]), CcPlaySound_CONSTRUCTION_OTHER);
						} else {
							PlaceRoadStop(start_tile, end_tile, _cur_roadtype << 5 | (_ctrl_pressed << 2) | ROADSTOP_TRUCK, CMD_BUILD_ROAD_STOP | CMD_MSG(this->rti->strings.err_build_station[ROADSTOP_TRUCK]));
						}
					}
					break;

				case DDSP_BUILD_DEPOT: {
					/* Build depot. */
					CommandContainer cmdcont = {start_tile, (uint32)(_cur_roadtype << 2 | _road_depot_orientation | (_ctrl_pressed << 8) |
							(INVALID_DEPOT << 16)), end_tile,
							(uint32)(CMD_BUILD_ROAD_DEPOT | CMD_MSG(this->rti->strings.err_depot)), CcRoadDepot, ""};

					ShowSelectDepotIfNeeded(cmdcont, TileArea(start_tile, end_tile), VEH_ROAD);
					break;
				}

				case DDSP_CONVERT_ROAD:
					DoCommandP(end_tile, start_tile, _cur_roadtype, CMD_CONVERT_ROAD | CMD_MSG(rti->strings.err_convert_road), CcPlaySound_CONSTRUCTION_OTHER);
					break;
			}
		}
	}

	void OnPlacePresize(Point pt, TileIndex tile) override
	{
		DoCommand(tile, _cur_roadtype | (TRANSPORT_ROAD << 8), 0, DC_AUTO, CMD_BUILD_TUNNEL);
		VpSetPresizeRange(tile, _build_tunnel_endtile == 0 ? tile : _build_tunnel_endtile);
	}

	EventState OnCTRLStateChange() override
	{
		if (RoadToolbar_CtrlChanged(this)) return ES_HANDLED;
		return ES_NOT_HANDLED;
	}

	static HotkeyList road_hotkeys;
	static HotkeyList tram_hotkeys;
};

/**
 * Handler for global hotkeys of the BuildRoadToolbarWindow.
 * @param hotkey Hotkey
 * @param last_build Last build road type
 * @return ES_HANDLED if hotkey was accepted.
 */
static EventState RoadTramToolbarGlobalHotkeys(int hotkey, RoadType last_build, RoadTramType rtt)
{
	Window* w = nullptr;
	switch (_game_mode) {
		case GM_NORMAL:
			w = ShowBuildRoadToolbar(last_build);
			break;

		case GM_EDITOR:
			if ((GetRoadTypes(true) & ((rtt == RTT_ROAD) ? ~_roadtypes_type : _roadtypes_type)) == ROADTYPES_NONE) return ES_NOT_HANDLED;
			w = ShowBuildRoadScenToolbar(last_build);
			break;

		default:
			break;
	}

	if (w == nullptr) return ES_NOT_HANDLED;
	return w->OnHotkey(hotkey);
}

static EventState RoadToolbarGlobalHotkeys(int hotkey)
{
	extern RoadType _last_built_roadtype;
	return RoadTramToolbarGlobalHotkeys(hotkey, _last_built_roadtype, RTT_ROAD);
}

static EventState TramToolbarGlobalHotkeys(int hotkey)
{
	extern RoadType _last_built_tramtype;
	return RoadTramToolbarGlobalHotkeys(hotkey, _last_built_tramtype, RTT_TRAM);
}

static Hotkey roadtoolbar_hotkeys[] = {
	Hotkey('1', "build_x", WID_ROT_ROAD_X),
	Hotkey('2', "build_y", WID_ROT_ROAD_Y),
	Hotkey('3', "autoroad", WID_ROT_AUTOROAD),
	Hotkey('4', "demolish", WID_ROT_DEMOLISH),
	Hotkey('5', "depot", WID_ROT_DEPOT),
	Hotkey('6', "bus_station", WID_ROT_BUS_STATION),
	Hotkey('7', "truck_station", WID_ROT_TRUCK_STATION),
	Hotkey('8', "oneway", WID_ROT_ONE_WAY),
	Hotkey('B', "bridge", WID_ROT_BUILD_BRIDGE),
	Hotkey('T', "tunnel", WID_ROT_BUILD_TUNNEL),
	Hotkey('R', "remove", WID_ROT_REMOVE),
	Hotkey('C', "convert", WID_ROT_CONVERT_ROAD),
	HOTKEY_LIST_END
};
HotkeyList BuildRoadToolbarWindow::road_hotkeys("roadtoolbar", roadtoolbar_hotkeys, RoadToolbarGlobalHotkeys);

static Hotkey tramtoolbar_hotkeys[] = {
	Hotkey('1', "build_x", WID_ROT_ROAD_X),
	Hotkey('2', "build_y", WID_ROT_ROAD_Y),
	Hotkey('3', "autoroad", WID_ROT_AUTOROAD),
	Hotkey('4', "demolish", WID_ROT_DEMOLISH),
	Hotkey('5', "depot", WID_ROT_DEPOT),
	Hotkey('6', "bus_station", WID_ROT_BUS_STATION),
	Hotkey('7', "truck_station", WID_ROT_TRUCK_STATION),
	Hotkey('B', "bridge", WID_ROT_BUILD_BRIDGE),
	Hotkey('T', "tunnel", WID_ROT_BUILD_TUNNEL),
	Hotkey('R', "remove", WID_ROT_REMOVE),
	Hotkey('C', "convert", WID_ROT_CONVERT_ROAD),
	HOTKEY_LIST_END
};
HotkeyList BuildRoadToolbarWindow::tram_hotkeys("tramtoolbar", tramtoolbar_hotkeys, TramToolbarGlobalHotkeys);


static const NWidgetPart _nested_build_road_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN, WID_ROT_CAPTION), SetDataTip(STR_WHITE_STRING, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_STICKYBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_ROAD_X),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_ROAD_X_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_SECTION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_ROAD_Y),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_ROAD_Y_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_SECTION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_AUTOROAD),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_AUTOROAD, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_AUTOROAD),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_DEMOLISH),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_DYNAMITE, STR_TOOLTIP_DEMOLISH_BUILDINGS_ETC),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_DEPOT),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_ROAD_DEPOT, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_VEHICLE_DEPOT),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUS_STATION),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_BUS_STATION, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_BUS_STATION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_TRUCK_STATION),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_TRUCK_BAY, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRUCK_LOADING_BAY),
		NWidget(WWT_PANEL, COLOUR_DARK_GREEN, -1), SetMinimalSize(0, 22), SetFill(1, 1), EndContainer(),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_ONE_WAY),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_ROAD_ONE_WAY, STR_ROAD_TOOLBAR_TOOLTIP_TOGGLE_ONE_WAY_ROAD),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUILD_BRIDGE),
						SetFill(0, 1), SetMinimalSize(43, 22), SetDataTip(SPR_IMG_BRIDGE, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_BRIDGE),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUILD_TUNNEL),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_ROAD_TUNNEL, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_TUNNEL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_REMOVE),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_REMOVE, STR_ROAD_TOOLBAR_TOOLTIP_TOGGLE_BUILD_REMOVE_FOR_ROAD),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_CONVERT_ROAD),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_CONVERT_ROAD, STR_ROAD_TOOLBAR_TOOLTIP_CONVERT_ROAD),
	EndContainer(),
};

static WindowDesc _build_road_desc(
	WDP_ALIGN_TOOLBAR, "toolbar_road", 0, 0,
	WC_BUILD_TOOLBAR, WC_NONE,
	WDF_CONSTRUCTION,
	_nested_build_road_widgets, lengthof(_nested_build_road_widgets),
	&BuildRoadToolbarWindow::road_hotkeys
);

static const NWidgetPart _nested_build_tramway_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN, WID_ROT_CAPTION), SetDataTip(STR_WHITE_STRING, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_STICKYBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_ROAD_X),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_TRAMWAY_X_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_SECTION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_ROAD_Y),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_TRAMWAY_Y_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_SECTION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_AUTOROAD),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_AUTOTRAM, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_AUTOTRAM),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_DEMOLISH),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_DYNAMITE, STR_TOOLTIP_DEMOLISH_BUILDINGS_ETC),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_DEPOT),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_ROAD_DEPOT, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAM_VEHICLE_DEPOT),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUS_STATION),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_BUS_STATION, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_PASSENGER_TRAM_STATION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_TRUCK_STATION),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_TRUCK_BAY, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_CARGO_TRAM_STATION),
		NWidget(WWT_PANEL, COLOUR_DARK_GREEN, -1), SetMinimalSize(0, 22), SetFill(1, 1), EndContainer(),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUILD_BRIDGE),
						SetFill(0, 1), SetMinimalSize(43, 22), SetDataTip(SPR_IMG_BRIDGE, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_BRIDGE),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUILD_TUNNEL),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_ROAD_TUNNEL, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_TUNNEL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_REMOVE),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_REMOVE, STR_ROAD_TOOLBAR_TOOLTIP_TOGGLE_BUILD_REMOVE_FOR_TRAMWAYS),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_CONVERT_ROAD),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_CONVERT_ROAD, STR_ROAD_TOOLBAR_TOOLTIP_CONVERT_TRAM),
	EndContainer(),
};

static WindowDesc _build_tramway_desc(
	WDP_ALIGN_TOOLBAR, "toolbar_tramway", 0, 0,
	WC_BUILD_TOOLBAR, WC_NONE,
	WDF_CONSTRUCTION,
	_nested_build_tramway_widgets, lengthof(_nested_build_tramway_widgets),
	&BuildRoadToolbarWindow::tram_hotkeys
);

/**
 * Open the build road toolbar window
 *
 * If the terraform toolbar is linked to the toolbar, that window is also opened.
 *
 * @return newly opened road toolbar, or nullptr if the toolbar could not be opened.
 */
Window *ShowBuildRoadToolbar(RoadType roadtype)
{
	if (!Company::IsValidID(_local_company)) return nullptr;
	if (!ValParamRoadType(roadtype)) return nullptr;

	CloseWindowByClass(WC_BUILD_TOOLBAR);
	_cur_roadtype = roadtype;

	return AllocateWindowDescFront<BuildRoadToolbarWindow>(RoadTypeIsRoad(_cur_roadtype) ? &_build_road_desc : &_build_tramway_desc, TRANSPORT_ROAD);
}

static const NWidgetPart _nested_build_road_scen_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN, WID_ROT_CAPTION), SetDataTip(STR_WHITE_STRING, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_STICKYBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_ROAD_X),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_ROAD_X_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_SECTION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_ROAD_Y),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_ROAD_Y_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_SECTION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_AUTOROAD),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_AUTOROAD, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_AUTOROAD),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_DEMOLISH),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_DYNAMITE, STR_TOOLTIP_DEMOLISH_BUILDINGS_ETC),
		NWidget(WWT_PANEL, COLOUR_DARK_GREEN, -1), SetMinimalSize(0, 22), SetFill(1, 1), EndContainer(),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_ONE_WAY),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_ROAD_ONE_WAY, STR_ROAD_TOOLBAR_TOOLTIP_TOGGLE_ONE_WAY_ROAD),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUILD_BRIDGE),
						SetFill(0, 1), SetMinimalSize(43, 22), SetDataTip(SPR_IMG_BRIDGE, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_BRIDGE),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUILD_TUNNEL),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_ROAD_TUNNEL, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_TUNNEL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_REMOVE),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_REMOVE, STR_ROAD_TOOLBAR_TOOLTIP_TOGGLE_BUILD_REMOVE_FOR_ROAD),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_CONVERT_ROAD),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_CONVERT_ROAD, STR_ROAD_TOOLBAR_TOOLTIP_CONVERT_ROAD),
	EndContainer(),
};

static WindowDesc _build_road_scen_desc(
	WDP_AUTO, "toolbar_road_scen", 0, 0,
	WC_SCEN_BUILD_TOOLBAR, WC_NONE,
	WDF_CONSTRUCTION,
	_nested_build_road_scen_widgets, lengthof(_nested_build_road_scen_widgets),
	&BuildRoadToolbarWindow::road_hotkeys
);

static const NWidgetPart _nested_build_tramway_scen_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN, WID_ROT_CAPTION), SetDataTip(STR_WHITE_STRING, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_STICKYBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_ROAD_X),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_TRAMWAY_X_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_SECTION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_ROAD_Y),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_TRAMWAY_Y_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_SECTION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_AUTOROAD),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_AUTOTRAM, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_AUTOTRAM),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_DEMOLISH),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_DYNAMITE, STR_TOOLTIP_DEMOLISH_BUILDINGS_ETC),
		NWidget(WWT_PANEL, COLOUR_DARK_GREEN, -1), SetMinimalSize(0, 22), SetFill(1, 1), EndContainer(),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUILD_BRIDGE),
						SetFill(0, 1), SetMinimalSize(43, 22), SetDataTip(SPR_IMG_BRIDGE, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_BRIDGE),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUILD_TUNNEL),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_ROAD_TUNNEL, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_TUNNEL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_REMOVE),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_REMOVE, STR_ROAD_TOOLBAR_TOOLTIP_TOGGLE_BUILD_REMOVE_FOR_TRAMWAYS),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_CONVERT_ROAD),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_CONVERT_ROAD, STR_ROAD_TOOLBAR_TOOLTIP_CONVERT_TRAM),
	EndContainer(),
};

static WindowDesc _build_tramway_scen_desc(
	WDP_AUTO, "toolbar_tram_scen", 0, 0,
	WC_SCEN_BUILD_TOOLBAR, WC_NONE,
	WDF_CONSTRUCTION,
	_nested_build_tramway_scen_widgets, lengthof(_nested_build_tramway_scen_widgets),
	&BuildRoadToolbarWindow::tram_hotkeys
);

/**
 * Show the road building toolbar in the scenario editor.
 * @return The just opened toolbar, or \c nullptr if the toolbar was already open.
 */
Window *ShowBuildRoadScenToolbar(RoadType roadtype)
{
	CloseWindowById(WC_SCEN_BUILD_TOOLBAR, TRANSPORT_ROAD);
	_cur_roadtype = roadtype;

	return AllocateWindowDescFront<BuildRoadToolbarWindow>(RoadTypeIsRoad(_cur_roadtype) ? &_build_road_scen_desc : &_build_tramway_scen_desc, TRANSPORT_ROAD);
}

struct BuildRoadDepotWindow : public PickerWindowBase {

	BuildRoadDepotWindow(WindowDesc *desc, Window *parent) : PickerWindowBase(desc, parent)
	{
		this->CreateNestedTree();

		this->LowerWidget(_road_depot_orientation + WID_BROD_DEPOT_NE);
		if (RoadTypeIsTram(_cur_roadtype)) {
			this->GetWidget<NWidgetCore>(WID_BROD_CAPTION)->widget_data = STR_BUILD_DEPOT_TRAM_ORIENTATION_CAPTION;
			for (int i = WID_BROD_DEPOT_NE; i <= WID_BROD_DEPOT_NW; i++) this->GetWidget<NWidgetCore>(i)->tool_tip = STR_BUILD_DEPOT_TRAM_ORIENTATION_SELECT_TOOLTIP;
		}

		this->FinishInitNested(TRANSPORT_ROAD);
	}

	virtual ~BuildRoadDepotWindow() {
		CloseWindowById(WC_SELECT_DEPOT, VEH_ROAD);
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		if (!IsInsideMM(widget, WID_BROD_DEPOT_NE, WID_BROD_DEPOT_NW + 1)) return;

		size->width  = ScaleGUITrad(64) + 2;
		size->height = ScaleGUITrad(48) + 2;
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		if (!IsInsideMM(widget, WID_BROD_DEPOT_NE, WID_BROD_DEPOT_NW + 1)) return;

		DrawRoadDepotSprite(r.left + 1 + ScaleGUITrad(31), r.bottom - ScaleGUITrad(31), (DiagDirection)(widget - WID_BROD_DEPOT_NE + DIAGDIR_NE), _cur_roadtype);
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_BROD_DEPOT_NW:
			case WID_BROD_DEPOT_NE:
			case WID_BROD_DEPOT_SW:
			case WID_BROD_DEPOT_SE:
				CloseWindowById(WC_SELECT_DEPOT, VEH_ROAD);
				this->RaiseWidget(_road_depot_orientation + WID_BROD_DEPOT_NE);
				_road_depot_orientation = (DiagDirection)(widget - WID_BROD_DEPOT_NE);
				this->LowerWidget(_road_depot_orientation + WID_BROD_DEPOT_NE);
				if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				break;

			default:
				break;
		}
	}

	void OnRealtimeTick(uint delta_ms) override
	{
		CheckRedrawDepotHighlight(this, VEH_ROAD);
	}
};

static const NWidgetPart _nested_build_road_depot_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN, WID_BROD_CAPTION), SetDataTip(STR_BUILD_DEPOT_ROAD_ORIENTATION_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN),
		NWidget(NWID_SPACER), SetMinimalSize(0, 3),
		NWidget(NWID_HORIZONTAL_LTR),
			NWidget(NWID_SPACER), SetMinimalSize(3, 0), SetFill(1, 0),
			NWidget(NWID_VERTICAL),
				NWidget(WWT_PANEL, COLOUR_GREY, WID_BROD_DEPOT_NW), SetMinimalSize(66, 50), SetDataTip(0x0, STR_BUILD_DEPOT_ROAD_ORIENTATION_SELECT_TOOLTIP),
				EndContainer(),
				NWidget(NWID_SPACER), SetMinimalSize(0, 2),
				NWidget(WWT_PANEL, COLOUR_GREY, WID_BROD_DEPOT_SW), SetMinimalSize(66, 50), SetDataTip(0x0, STR_BUILD_DEPOT_ROAD_ORIENTATION_SELECT_TOOLTIP),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_SPACER), SetMinimalSize(2, 0),
			NWidget(NWID_VERTICAL),
				NWidget(WWT_PANEL, COLOUR_GREY, WID_BROD_DEPOT_NE), SetMinimalSize(66, 50), SetDataTip(0x0, STR_BUILD_DEPOT_ROAD_ORIENTATION_SELECT_TOOLTIP),
				EndContainer(),
				NWidget(NWID_SPACER), SetMinimalSize(0, 2),
				NWidget(WWT_PANEL, COLOUR_GREY, WID_BROD_DEPOT_SE), SetMinimalSize(66, 50), SetDataTip(0x0, STR_BUILD_DEPOT_ROAD_ORIENTATION_SELECT_TOOLTIP),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_SPACER), SetMinimalSize(3, 0), SetFill(1, 0),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 3),
	EndContainer(),
};

static WindowDesc _build_road_depot_desc(
	WDP_AUTO, nullptr, 0, 0,
	WC_BUILD_DEPOT, WC_BUILD_TOOLBAR,
	WDF_CONSTRUCTION,
	_nested_build_road_depot_widgets, lengthof(_nested_build_road_depot_widgets)
);

static void ShowRoadDepotPicker(Window *parent)
{
	new BuildRoadDepotWindow(&_build_road_depot_desc, parent);
}

struct BuildRoadStationWindow : public PickerWindowBase {
	BuildRoadStationWindow(WindowDesc *desc, Window *parent, RoadStopType rs) : PickerWindowBase(desc, parent)
	{
		this->CreateNestedTree();

		/* Trams don't have non-drivethrough stations */
		if (RoadTypeIsTram(_cur_roadtype) && _road_station_picker_orientation < DIAGDIR_END) {
			_road_station_picker_orientation = DIAGDIR_END;
		}
		const RoadTypeInfo *rti = GetRoadTypeInfo(_cur_roadtype);
		this->GetWidget<NWidgetCore>(WID_BROS_CAPTION)->widget_data = rti->strings.picker_title[rs];

		for (uint i = RoadTypeIsTram(_cur_roadtype) ? WID_BROS_STATION_X : WID_BROS_STATION_NE; i < WID_BROS_LT_OFF; i++) {
			this->GetWidget<NWidgetCore>(i)->tool_tip = rti->strings.picker_tooltip[rs];
		}

		this->LowerWidget(_road_station_picker_orientation + WID_BROS_STATION_NE);
		this->LowerWidget(_settings_client.gui.station_show_coverage + WID_BROS_LT_OFF);

		this->FinishInitNested(TRANSPORT_ROAD);

		this->window_class = (rs == ROADSTOP_BUS) ? WC_BUS_STATION : WC_TRUCK_STATION;
	}

	void Close() override
	{
		CloseWindowById(WC_SELECT_STATION, 0);
		this->PickerWindowBase::Close();
	}

	void OnPaint() override
	{
		this->DrawWidgets();

		int rad = _settings_game.station.modified_catchment ? ((this->window_class == WC_BUS_STATION) ? CA_BUS : CA_TRUCK) : CA_UNMODIFIED;
		if (_settings_client.gui.station_show_coverage) {
			SetTileSelectBigSize(-rad, -rad, 2 * rad, 2 * rad);
		} else {
			SetTileSelectSize(1, 1);
		}

		/* 'Accepts' and 'Supplies' texts. */
		StationCoverageType sct = (this->window_class == WC_BUS_STATION) ? SCT_PASSENGERS_ONLY : SCT_NON_PASSENGERS_ONLY;
		int top = this->GetWidget<NWidgetBase>(WID_BROS_LT_ON)->pos_y + this->GetWidget<NWidgetBase>(WID_BROS_LT_ON)->current_y + WD_PAR_VSEP_NORMAL;
		NWidgetBase *back_nwi = this->GetWidget<NWidgetBase>(WID_BROS_BACKGROUND);
		int right = back_nwi->pos_x +  back_nwi->current_x;
		int bottom = back_nwi->pos_y +  back_nwi->current_y;
		top = DrawStationCoverageAreaText(back_nwi->pos_x + WD_FRAMERECT_LEFT, right - WD_FRAMERECT_RIGHT, top, sct, rad, false) + WD_PAR_VSEP_NORMAL;
		top = DrawStationCoverageAreaText(back_nwi->pos_x + WD_FRAMERECT_LEFT, right - WD_FRAMERECT_RIGHT, top, sct, rad, true) + WD_PAR_VSEP_NORMAL;
		/* Resize background if the window is too small.
		 * Never make the window smaller to avoid oscillating if the size change affects the acceptance.
		 * (This is the case, if making the window bigger moves the mouse into the window.) */
		if (top > bottom) {
			ResizeWindow(this, 0, top - bottom, false);
		}
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		if (!IsInsideMM(widget, WID_BROS_STATION_NE, WID_BROS_STATION_Y + 1)) return;

		size->width  = ScaleGUITrad(64) + 2;
		size->height = ScaleGUITrad(48) + 2;
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		if (!IsInsideMM(widget, WID_BROS_STATION_NE, WID_BROS_STATION_Y + 1)) return;

		StationType st = (this->window_class == WC_BUS_STATION) ? STATION_BUS : STATION_TRUCK;
		StationPickerDrawSprite(r.left + 1 + ScaleGUITrad(31), r.bottom - ScaleGUITrad(31), st, INVALID_RAILTYPE, _cur_roadtype, widget - WID_BROS_STATION_NE);
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_BROS_STATION_NE:
			case WID_BROS_STATION_SE:
			case WID_BROS_STATION_SW:
			case WID_BROS_STATION_NW:
			case WID_BROS_STATION_X:
			case WID_BROS_STATION_Y:
				this->RaiseWidget(_road_station_picker_orientation + WID_BROS_STATION_NE);
				_road_station_picker_orientation = (DiagDirection)(widget - WID_BROS_STATION_NE);
				this->LowerWidget(_road_station_picker_orientation + WID_BROS_STATION_NE);
				if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				CloseWindowById(WC_SELECT_STATION, 0);
				break;

			case WID_BROS_LT_OFF:
			case WID_BROS_LT_ON:
				this->RaiseWidget(_settings_client.gui.station_show_coverage + WID_BROS_LT_OFF);
				_settings_client.gui.station_show_coverage = (widget != WID_BROS_LT_OFF);
				this->LowerWidget(_settings_client.gui.station_show_coverage + WID_BROS_LT_OFF);
				if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				SetViewportCatchmentStation(nullptr, true);
				break;

			default:
				break;
		}
	}

	void OnRealtimeTick(uint delta_ms) override
	{
		CheckRedrawStationCoverage(this);
	}
};

/** Widget definition of the build road station window */
static const NWidgetPart _nested_road_station_picker_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION,  COLOUR_DARK_GREEN, WID_BROS_CAPTION),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BROS_BACKGROUND),
		NWidget(NWID_SPACER), SetMinimalSize(0, 3),
		NWidget(NWID_HORIZONTAL), SetPIP(0, 2, 0),
			NWidget(NWID_SPACER), SetFill(1, 0),
			NWidget(WWT_PANEL, COLOUR_GREY, WID_BROS_STATION_NW), SetMinimalSize(66, 50), SetFill(0, 0), EndContainer(),
			NWidget(WWT_PANEL, COLOUR_GREY, WID_BROS_STATION_NE), SetMinimalSize(66, 50), SetFill(0, 0), EndContainer(),
			NWidget(WWT_PANEL, COLOUR_GREY, WID_BROS_STATION_X),  SetMinimalSize(66, 50), SetFill(0, 0), EndContainer(),
			NWidget(NWID_SPACER), SetFill(1, 0),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 2),
		NWidget(NWID_HORIZONTAL), SetPIP(0, 2, 0),
			NWidget(NWID_SPACER), SetFill(1, 0),
			NWidget(WWT_PANEL, COLOUR_GREY, WID_BROS_STATION_SW), SetMinimalSize(66, 50), SetFill(0, 0), EndContainer(),
			NWidget(WWT_PANEL, COLOUR_GREY, WID_BROS_STATION_SE), SetMinimalSize(66, 50), SetFill(0, 0), EndContainer(),
			NWidget(WWT_PANEL, COLOUR_GREY, WID_BROS_STATION_Y),  SetMinimalSize(66, 50), SetFill(0, 0), EndContainer(),
			NWidget(NWID_SPACER), SetFill(1, 0),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 1),
		NWidget(NWID_HORIZONTAL), SetPIP(2, 0, 2),
			NWidget(WWT_LABEL, COLOUR_DARK_GREEN, WID_BROS_INFO), SetMinimalSize(140, 14), SetDataTip(STR_STATION_BUILD_COVERAGE_AREA_TITLE, STR_NULL),
			NWidget(NWID_SPACER), SetFill(1, 0),
		EndContainer(),
		NWidget(NWID_HORIZONTAL), SetPIP(2, 0, 2),
			NWidget(NWID_SPACER), SetFill(1, 0),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BROS_LT_OFF), SetMinimalSize(60, 12),
											SetDataTip(STR_STATION_BUILD_COVERAGE_OFF, STR_STATION_BUILD_COVERAGE_AREA_OFF_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BROS_LT_ON), SetMinimalSize(60, 12),
											SetDataTip(STR_STATION_BUILD_COVERAGE_ON, STR_STATION_BUILD_COVERAGE_AREA_ON_TOOLTIP),
			NWidget(NWID_SPACER), SetFill(1, 0),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 10), SetResize(0, 1),
	EndContainer(),
};

static WindowDesc _road_station_picker_desc(
	WDP_AUTO, nullptr, 0, 0,
	WC_BUS_STATION, WC_BUILD_TOOLBAR,
	WDF_CONSTRUCTION,
	_nested_road_station_picker_widgets, lengthof(_nested_road_station_picker_widgets)
);

/** Widget definition of the build tram station window */
static const NWidgetPart _nested_tram_station_picker_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION,  COLOUR_DARK_GREEN, WID_BROS_CAPTION),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BROS_BACKGROUND),
		NWidget(NWID_SPACER), SetMinimalSize(0, 3),
		NWidget(NWID_HORIZONTAL), SetPIP(0, 2, 0),
			NWidget(NWID_SPACER), SetFill(1, 0),
			NWidget(WWT_PANEL, COLOUR_GREY, WID_BROS_STATION_X),  SetMinimalSize(66, 50), SetFill(0, 0), EndContainer(),
			NWidget(WWT_PANEL, COLOUR_GREY, WID_BROS_STATION_Y),  SetMinimalSize(66, 50), SetFill(0, 0), EndContainer(),
			NWidget(NWID_SPACER), SetFill(1, 0),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 1),
		NWidget(NWID_HORIZONTAL), SetPIP(2, 0, 2),
			NWidget(WWT_LABEL, COLOUR_DARK_GREEN, WID_BROS_INFO), SetMinimalSize(140, 14), SetDataTip(STR_STATION_BUILD_COVERAGE_AREA_TITLE, STR_NULL),
			NWidget(NWID_SPACER), SetFill(1, 0),
		EndContainer(),
		NWidget(NWID_HORIZONTAL), SetPIP(2, 0, 2),
			NWidget(NWID_SPACER), SetFill(1, 0),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BROS_LT_OFF), SetMinimalSize(60, 12),
											SetDataTip(STR_STATION_BUILD_COVERAGE_OFF, STR_STATION_BUILD_COVERAGE_AREA_OFF_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BROS_LT_ON), SetMinimalSize(60, 12),
											SetDataTip(STR_STATION_BUILD_COVERAGE_ON, STR_STATION_BUILD_COVERAGE_AREA_ON_TOOLTIP),
			NWidget(NWID_SPACER), SetFill(1, 0),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 10), SetResize(0, 1),
	EndContainer(),
};

static WindowDesc _tram_station_picker_desc(
	WDP_AUTO, nullptr, 0, 0,
	WC_BUS_STATION, WC_BUILD_TOOLBAR,
	WDF_CONSTRUCTION,
	_nested_tram_station_picker_widgets, lengthof(_nested_tram_station_picker_widgets)
);

static void ShowRVStationPicker(Window *parent, RoadStopType rs)
{
	new BuildRoadStationWindow(RoadTypeIsRoad(_cur_roadtype) ? &_road_station_picker_desc : &_tram_station_picker_desc, parent, rs);
}

void InitializeRoadGui()
{
	_road_depot_orientation = DIAGDIR_NW;
	_road_station_picker_orientation = DIAGDIR_NW;
}

/**
 * I really don't know why rail_gui.cpp has this too, shouldn't be included in the other one?
 */
void InitializeRoadGUI()
{
	BuildRoadToolbarWindow *w = dynamic_cast<BuildRoadToolbarWindow *>(FindWindowById(WC_BUILD_TOOLBAR, TRANSPORT_ROAD));
	if (w != nullptr) w->ModifyRoadType(_cur_roadtype);
}

DropDownList GetRoadTypeDropDownList(RoadTramTypes rtts, bool for_replacement, bool all_option)
{
	RoadTypes used_roadtypes;
	RoadTypes avail_roadtypes;

	const Company *c = Company::Get(_local_company);

	/* Find the used roadtypes. */
	if (for_replacement) {
		avail_roadtypes = GetCompanyRoadTypes(c->index, false);
		used_roadtypes  = GetRoadTypes(false);
	} else {
		avail_roadtypes = c->avail_roadtypes;
		used_roadtypes  = GetRoadTypes(true);
	}

	/* Filter listed road types */
	if (!HasBit(rtts, RTT_ROAD)) used_roadtypes &= _roadtypes_type;
	if (!HasBit(rtts, RTT_TRAM)) used_roadtypes &= ~_roadtypes_type;

	DropDownList list;

	if (all_option) {
		list.emplace_back(new DropDownListStringItem(STR_REPLACE_ALL_ROADTYPE, INVALID_ROADTYPE, false));
	}

	Dimension d = { 0, 0 };
	/* Get largest icon size, to ensure text is aligned on each menu item. */
	if (!for_replacement) {
		for (const auto &rt : _sorted_roadtypes) {
			if (!HasBit(used_roadtypes, rt)) continue;
			const RoadTypeInfo *rti = GetRoadTypeInfo(rt);
			d = maxdim(d, GetSpriteSize(rti->gui_sprites.build_x_road));
		}
	}

	for (const auto &rt : _sorted_roadtypes) {
		/* If it's not used ever, don't show it to the user. */
		if (!HasBit(used_roadtypes, rt)) continue;

		const RoadTypeInfo *rti = GetRoadTypeInfo(rt);

		DropDownListParamStringItem *item;
		if (for_replacement) {
			item = new DropDownListParamStringItem(rti->strings.replace_text, rt, !HasBit(avail_roadtypes, rt));
		} else {
			StringID str = rti->max_speed > 0 ? STR_TOOLBAR_RAILTYPE_VELOCITY : STR_JUST_STRING;
			DropDownListIconItem *iconitem = new DropDownListIconItem(rti->gui_sprites.build_x_road, PAL_NONE, str, rt, !HasBit(avail_roadtypes, rt));
			iconitem->SetDimension(d);
			item = iconitem;
		}
		item->SetParam(0, rti->strings.menu_text);
		item->SetParam(1, rti->max_speed / 2);
		list.emplace_back(item);
	}

	if (list.size() == 0) {
		/* Empty dropdowns are not allowed */
		list.emplace_back(new DropDownListStringItem(STR_NONE, INVALID_ROADTYPE, true));
	}

	return list;
}

DropDownList GetScenRoadTypeDropDownList(RoadTramTypes rtts)
{
	RoadTypes avail_roadtypes = GetRoadTypes(false);
	avail_roadtypes = AddDateIntroducedRoadTypes(avail_roadtypes, _date);
	RoadTypes used_roadtypes = GetRoadTypes(true);

	/* Filter listed road types */
	if (!HasBit(rtts, RTT_ROAD)) used_roadtypes &= _roadtypes_type;
	if (!HasBit(rtts, RTT_TRAM)) used_roadtypes &= ~_roadtypes_type;

	DropDownList list;

	/* If it's not used ever, don't show it to the user. */
	Dimension d = { 0, 0 };
	for (const auto &rt : _sorted_roadtypes) {
		if (!HasBit(used_roadtypes, rt)) continue;
		const RoadTypeInfo *rti = GetRoadTypeInfo(rt);
		d = maxdim(d, GetSpriteSize(rti->gui_sprites.build_x_road));
	}
	for (const auto &rt : _sorted_roadtypes) {
		if (!HasBit(used_roadtypes, rt)) continue;

		const RoadTypeInfo *rti = GetRoadTypeInfo(rt);

		StringID str = rti->max_speed > 0 ? STR_TOOLBAR_RAILTYPE_VELOCITY : STR_JUST_STRING;
		DropDownListIconItem *item = new DropDownListIconItem(rti->gui_sprites.build_x_road, PAL_NONE, str, rt, !HasBit(avail_roadtypes, rt));
		item->SetDimension(d);
		item->SetParam(0, rti->strings.menu_text);
		item->SetParam(1, rti->max_speed / 2);
		list.emplace_back(item);
	}

	if (list.size() == 0) {
		/* Empty dropdowns are not allowed */
		list.emplace_back(new DropDownListStringItem(STR_NONE, -1, true));
	}

	return list;
}
