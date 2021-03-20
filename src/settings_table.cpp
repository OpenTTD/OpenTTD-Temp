/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file settings_table.cpp The tables of all the settings as well as the implementation of most of their callbacks.
 */

#include "stdafx.h"
#include "settings_table.h"
#include "currency.h"
#include "screenshot.h"
#include "network/network.h"
#include "network/network_func.h"
#include "pathfinder/pathfinder_type.h"
#include "genworld.h"
#include "train.h"
#include "news_func.h"
#include "window_func.h"
#include "company_func.h"
#if defined(WITH_FREETYPE) || defined(_WIN32) || defined(WITH_COCOA)
#define HAS_TRUETYPE_FONT
#include "fontcache.h"
#endif
#include "textbuf_gui.h"
#include "rail_gui.h"
#include "elrail_func.h"
#include "error.h"
#include "town.h"
#include "video/video_driver.hpp"
#include "sound/sound_driver.hpp"
#include "music/music_driver.hpp"
#include "blitter/factory.hpp"
#include "base_media_base.h"
#include "ai/ai_config.hpp"
#include "ai/ai.hpp"
#include "game/game_config.hpp"
#include "ship.h"
#include "smallmap_gui.h"
#include "roadveh.h"
#include "vehicle_func.h"
#include "void_map.h"

#include "table/strings.h"
#include "table/settings.h"

#include "safeguards.h"

SettingTable _company_settings{ _company_settings_table };
SettingTable _currency_settings{ _currency_settings_table };
SettingTable _difficulty_settings{ _difficulty_settings_table };
SettingTable _multimedia_settings{ _multimedia_settings_table };
SettingTable _economy_settings{ _economy_settings_table };
SettingTable _game_settings{ _game_settings_table };
SettingTable _gui_settings{ _gui_settings_table };
SettingTable _linkgraph_settings{ _linkgraph_settings_table };
SettingTable _locale_settings{ _locale_settings_table };
SettingTable _misc_settings{ _misc_settings_table };
SettingTable _network_private_settings{ _network_private_settings_table };
SettingTable _network_secrets_settings{ _network_secrets_settings_table };
SettingTable _network_settings{ _network_settings_table };
SettingTable _news_display_settings{ _news_display_settings_table };
SettingTable _old_gameopt_settings{ _old_gameopt_settings_table };
SettingTable _pathfinding_settings{ _pathfinding_settings_table };
SettingTable _script_settings{ _script_settings_table };
SettingTable _window_settings{ _window_settings_table };
SettingTable _world_settings{ _world_settings_table };
#if defined(_WIN32) && !defined(DEDICATED)
SettingTable _win32_settings{ _win32_settings_table };
#endif /* _WIN32 */


/* Begin - Callback Functions for the various settings. */

/** Reposition the main toolbar as the setting changed. */
static void v_PositionMainToolbar(int32 new_value)
{
	if (_game_mode != GM_MENU) PositionMainToolbar(nullptr);
}

/** Reposition the statusbar as the setting changed. */
static void v_PositionStatusbar(int32 new_value)
{
	if (_game_mode != GM_MENU) {
		PositionStatusbar(nullptr);
		PositionNewsMessage(nullptr);
		PositionNetworkChatWindow(nullptr);
	}
}

/**
 * Redraw the smallmap after a colour scheme change.
 * @param p1 Callback parameter.
 */
static void RedrawSmallmap(int32 new_value)
{
	BuildLandLegend();
	BuildOwnerLegend();
	SetWindowClassesDirty(WC_SMALLMAP);
}

static void StationSpreadChanged(int32 p1)
{
	InvalidateWindowClassesData(WC_SELECT_DEPOT, 0);
	InvalidateWindowData(WC_SELECT_STATION, 0);
	InvalidateWindowData(WC_BUILD_STATION, 0);
}

static void CloseSignalGUI(int32 new_value)
{
	if (new_value == 0) {
		CloseWindowByClass(WC_BUILD_SIGNAL);
	}
}

static void UpdateConsists(int32 new_value)
{
	for (Train *t : Train::Iterate()) {
		/* Update the consist of all trains so the maximum speed is set correctly. */
		if (t->IsFrontEngine() || t->IsFreeWagon()) t->ConsistChanged(CCF_TRACK);
	}
	InvalidateWindowClassesData(WC_BUILD_VEHICLE, INVALID_DEPOT - VEH_TRAIN);
}

/* Check service intervals of vehicles, newvalue is value of % or day based servicing */
static void UpdateAllServiceInterval(int32 new_value)
{
	bool update_vehicles;
	VehicleDefaultSettings *vds;
	if (_game_mode == GM_MENU || !Company::IsValidID(_current_company)) {
		vds = &_settings_client.company.vehicle;
		update_vehicles = false;
	} else {
		vds = &Company::Get(_current_company)->settings.vehicle;
		update_vehicles = true;
	}

	if (new_value != 0) {
		vds->servint_trains   = 50;
		vds->servint_roadveh  = 50;
		vds->servint_aircraft = 50;
		vds->servint_ships    = 50;
	} else {
		vds->servint_trains   = 150;
		vds->servint_roadveh  = 150;
		vds->servint_aircraft = 100;
		vds->servint_ships    = 360;
	}

	if (update_vehicles) {
		const Company *c = Company::Get(_current_company);
		for (Vehicle *v : Vehicle::Iterate()) {
			if (v->owner == _current_company && v->IsPrimaryVehicle() && !v->ServiceIntervalIsCustom()) {
				v->SetServiceInterval(CompanyServiceInterval(c, v->type));
				v->SetServiceIntervalIsPercent(new_value != 0);
			}
		}
	}

	SetWindowClassesDirty(WC_VEHICLE_DETAILS);
}

static bool CanUpdateServiceInterval(VehicleType type, int32 &new_value)
{
	VehicleDefaultSettings *vds;
	if (_game_mode == GM_MENU || !Company::IsValidID(_current_company)) {
		vds = &_settings_client.company.vehicle;
	} else {
		vds = &Company::Get(_current_company)->settings.vehicle;
	}

	/* Test if the interval is valid */
	int32 interval = GetServiceIntervalClamped(new_value, vds->servint_ispercent);
	return interval == new_value;
}

static void UpdateServiceInterval(VehicleType type, int32 new_value)
{
	if (_game_mode != GM_MENU && Company::IsValidID(_current_company)) {
		for (Vehicle *v : Vehicle::Iterate()) {
			if (v->owner == _current_company && v->type == type && v->IsPrimaryVehicle() && !v->ServiceIntervalIsCustom()) {
				v->SetServiceInterval(new_value);
			}
		}
	}

	SetWindowClassesDirty(WC_VEHICLE_DETAILS);
}

static void TrainAccelerationModelChanged(int32 new_value)
{
	for (Train *t : Train::Iterate()) {
		if (t->IsFrontEngine()) {
			t->tcache.cached_max_curve_speed = t->GetCurveSpeedLimit();
			t->UpdateAcceleration();
		}
	}

	/* These windows show acceleration values only when realistic acceleration is on. They must be redrawn after a setting change. */
	SetWindowClassesDirty(WC_ENGINE_PREVIEW);
	InvalidateWindowClassesData(WC_BUILD_VEHICLE, INVALID_DEPOT - VEH_TRAIN);
	SetWindowClassesDirty(WC_VEHICLE_DETAILS);
}

/**
 * This function updates the train acceleration cache after a steepness change.
 * @param new_value Unused new value of setting.
 */
static void TrainSlopeSteepnessChanged(int32 new_value)
{
	for (Train *t : Train::Iterate()) {
		if (t->IsFrontEngine()) t->CargoChanged();
	}
}

/**
 * This function updates realistic acceleration caches when the setting "Road vehicle acceleration model" is set.
 * @param new_value Unused new value of setting.
 */
static void RoadVehAccelerationModelChanged(int32 new_value)
{
	if (_settings_game.vehicle.roadveh_acceleration_model != AM_ORIGINAL) {
		for (RoadVehicle *rv : RoadVehicle::Iterate()) {
			if (rv->IsFrontEngine()) {
				rv->CargoChanged();
			}
		}
	}

	/* These windows show acceleration values only when realistic acceleration is on. They must be redrawn after a setting change. */
	SetWindowClassesDirty(WC_ENGINE_PREVIEW);
	InvalidateWindowClassesData(WC_BUILD_VEHICLE, INVALID_DEPOT - VEH_ROAD);
	SetWindowClassesDirty(WC_VEHICLE_DETAILS);
}

/**
 * This function updates the road vehicle acceleration cache after a steepness change.
 * @param new_value Unused new value of setting.
 */
static void RoadVehSlopeSteepnessChanged(int32 new_value)
{
	for (RoadVehicle *rv : RoadVehicle::Iterate()) {
		if (rv->IsFrontEngine()) rv->CargoChanged();
	}
}

static void TownFoundingChanged(int32 new_value)
{
	if (_game_mode != GM_EDITOR && _settings_game.economy.found_town == TF_FORBIDDEN) {
		CloseWindowById(WC_FOUND_TOWN, 0);
	} else {
		InvalidateWindowData(WC_FOUND_TOWN, 0);
	}
}

static void ZoomMinMaxChanged(int32 new_value)
{
	extern void ConstrainAllViewportsZoom();
	ConstrainAllViewportsZoom();
	GfxClearSpriteCache();
	if (_settings_client.gui.zoom_min > _gui_zoom) {
		/* Restrict GUI zoom if it is no longer available. */
		_gui_zoom = _settings_client.gui.zoom_min;
		UpdateCursorSize();
		LoadStringWidthTable();
	}
}

static void SpriteZoomMinChanged(int32 new_value)
{
	GfxClearSpriteCache();
	/* Force all sprites to redraw at the new chosen zoom level */
	MarkWholeScreenDirty();
}

static bool CheckDifferentRailRoadTypesReplacements(int32 &new_value)
{
	if (_game_mode == GM_NORMAL) {
		if (new_value == 0) {
			ShowErrorMessage(STR_CONFIG_SETTING_REPLACEMENTS_DIFF_TYPE, INVALID_STRING_ID, WL_ERROR);
			return false;
		}
	}
	return true;
}

static void InvalidateReplacementWindows(int32 new_value)
{
	InvalidateWindowClassesData(WC_REPLACE_VEHICLE);
}

static void DepotSettingsChanged(int32 new_value)
{
	CloseWindowByClass(WC_BUILD_TOOLBAR);
}

/**
 * Update any possible saveload window and delete any newgrf dialogue as
 * its widget parts might change. Reinit all windows as it allows access to the
 * newgrf debug button.
 * @param new_value unused.
 */
static void InvalidateNewGRFChangeWindows(int32 new_value)
{
	InvalidateWindowClassesData(WC_SAVELOAD);
	CloseWindowByClass(WC_GAME_OPTIONS);
	ReInitAllWindows(_gui_zoom_cfg);
}

static void InvalidateCompanyLiveryWindow(int32 new_value)
{
	InvalidateWindowClassesData(WC_COMPANY_COLOUR, -1);
	ResetVehicleColourMap();
}

static void DifficultyNoiseChange(int32 new_value)
{
	if (_game_mode == GM_NORMAL) {
		UpdateAirportsNoise();
		if (_settings_game.economy.station_noise_level) {
			InvalidateWindowClassesData(WC_TOWN_VIEW, 0);
		}
	}
}

static void MaxNoAIsChange(int32 new_value)
{
	if (GetGameSettings().difficulty.max_no_competitors != 0 &&
			AI::GetInfoList()->size() == 0 &&
			(!_networking || _network_server)) {
		ShowErrorMessage(STR_WARNING_NO_SUITABLE_AI, INVALID_STRING_ID, WL_CRITICAL);
	}

	InvalidateWindowClassesData(WC_GAME_OPTIONS, 0);
}

/**
 * Check whether the road side may be changed.
 * @param new_value unused
 * @return true if the road side may be changed.
 */
static bool CheckRoadSide(int32 &new_value)
{
	extern bool RoadVehiclesAreBuilt();
	return _game_mode == GM_MENU || !RoadVehiclesAreBuilt();
}

/**
 * Conversion callback for _gameopt_settings_game.landscape
 * It converts (or try) between old values and the new ones,
 * without losing initial setting of the user
 * @param value that was read from config file
 * @return the "hopefully" converted value
 */
static size_t ConvertLandscape(const char *value)
{
	/* try with the old values */
	static std::vector<std::string> _old_landscape_values{"normal", "hilly", "desert", "candy"};
	return OneOfManySettingDesc::ParseSingleValue(value, strlen(value), _old_landscape_values);
}

static bool CheckFreeformEdges(int32 &new_value)
{
	if (_game_mode == GM_MENU) return true;
	if (new_value != 0) {
		for (Ship *s : Ship::Iterate()) {
			/* Check if there is a ship on the northern border. */
			if (TileX(s->tile) == 0 || TileY(s->tile) == 0) {
				ShowErrorMessage(STR_CONFIG_SETTING_EDGES_NOT_EMPTY, INVALID_STRING_ID, WL_ERROR);
				return false;
			}
		}
		for (const BaseStation *st : BaseStation::Iterate()) {
			/* Check if there is a non-deleted buoy on the northern border. */
			if (st->IsInUse() && (TileX(st->xy) == 0 || TileY(st->xy) == 0)) {
				ShowErrorMessage(STR_CONFIG_SETTING_EDGES_NOT_EMPTY, INVALID_STRING_ID, WL_ERROR);
				return false;
			}
		}
	} else {
		for (uint i = 0; i < MapMaxX(); i++) {
			if (TileHeight(TileXY(i, 1)) != 0) {
				ShowErrorMessage(STR_CONFIG_SETTING_EDGES_NOT_WATER, INVALID_STRING_ID, WL_ERROR);
				return false;
			}
		}
		for (uint i = 1; i < MapMaxX(); i++) {
			if (!IsTileType(TileXY(i, MapMaxY() - 1), MP_WATER) || TileHeight(TileXY(1, MapMaxY())) != 0) {
				ShowErrorMessage(STR_CONFIG_SETTING_EDGES_NOT_WATER, INVALID_STRING_ID, WL_ERROR);
				return false;
			}
		}
		for (uint i = 0; i < MapMaxY(); i++) {
			if (TileHeight(TileXY(1, i)) != 0) {
				ShowErrorMessage(STR_CONFIG_SETTING_EDGES_NOT_WATER, INVALID_STRING_ID, WL_ERROR);
				return false;
			}
		}
		for (uint i = 1; i < MapMaxY(); i++) {
			if (!IsTileType(TileXY(MapMaxX() - 1, i), MP_WATER) || TileHeight(TileXY(MapMaxX(), i)) != 0) {
				ShowErrorMessage(STR_CONFIG_SETTING_EDGES_NOT_WATER, INVALID_STRING_ID, WL_ERROR);
				return false;
			}
		}
	}
	return true;
}

static void UpdateFreeformEdges(int32 new_value)
{
	if (_game_mode == GM_MENU) return;

	if (new_value != 0) {
		for (uint x = 0; x < MapSizeX(); x++) MakeVoid(TileXY(x, 0));
		for (uint y = 0; y < MapSizeY(); y++) MakeVoid(TileXY(0, y));
	} else {
		/* Make tiles at the border water again. */
		for (uint i = 0; i < MapMaxX(); i++) {
			SetTileHeight(TileXY(i, 0), 0);
			SetTileType(TileXY(i, 0), MP_WATER);
		}
		for (uint i = 0; i < MapMaxY(); i++) {
			SetTileHeight(TileXY(0, i), 0);
			SetTileType(TileXY(0, i), MP_WATER);
		}
	}
	MarkWholeScreenDirty();
}

/**
 * Changing the setting "allow multiple NewGRF sets" is not allowed
 * if there are vehicles.
 */
static bool CheckDynamicEngines(int32 &new_value)
{
	if (_game_mode == GM_MENU) return true;

	if (!EngineOverrideManager::ResetToCurrentNewGRFConfig()) {
		ShowErrorMessage(STR_CONFIG_SETTING_DYNAMIC_ENGINES_EXISTING_VEHICLES, INVALID_STRING_ID, WL_ERROR);
		return false;
	}

	return true;
}

static bool CheckMaxHeightLevel(int32 &new_value)
{
	if (_game_mode == GM_NORMAL) return false;
	if (_game_mode != GM_EDITOR) return true;

	/* Check if at least one mountain on the map is higher than the new value.
	 * If yes, disallow the change. */
	for (TileIndex t = 0; t < MapSize(); t++) {
		if ((int32)TileHeight(t) > new_value) {
			ShowErrorMessage(STR_CONFIG_SETTING_TOO_HIGH_MOUNTAIN, INVALID_STRING_ID, WL_ERROR);
			/* Return old, unchanged value */
			return false;
		}
	}

	return true;
}

static void StationCatchmentChanged(int32 new_value)
{
	Station::RecomputeCatchmentForAll();
	MarkWholeScreenDirty();
}

static void MaxVehiclesChanged(int32 new_value)
{
	InvalidateWindowClassesData(WC_BUILD_TOOLBAR);
	MarkWholeScreenDirty();
}

static void InvalidateShipPathCache(int32 new_value)
{
	for (Ship *s : Ship::Iterate()) {
		s->path.clear();
	}
}

/**
 * Replace a passwords that are a literal asterisk with an empty string.
 * @param newval The new string value for this password field.
 * @return Always true.
 */
static bool ReplaceAsteriskWithEmptyPassword(std::string &newval)
{
	if (newval.compare("*") == 0) newval.clear();
	return true;
}

/** Update the game info, and send it to the clients when we are running as a server. */
static void UpdateClientConfigValues()
{
	NetworkServerUpdateGameInfo();
	if (_network_server) NetworkServerSendConfigUpdate();
}

/* End - Callback Functions */
