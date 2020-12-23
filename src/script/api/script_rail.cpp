/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_rail.cpp Implementation of ScriptRail. */

#include "../../stdafx.h"
#include "script_rail.hpp"
#include "script_map.hpp"
#include "script_station.hpp"
#include "script_industrytype.hpp"
#include "script_cargo.hpp"
#include "../../debug.h"
#include "../../station_base.h"
#include "../../newgrf_generic.h"
#include "../../newgrf_station.h"
#include "../../strings_func.h"

#include "../../safeguards.h"

/* static */ char *ScriptRail::GetName(RailType rail_type)
{
	if (!IsRailTypeAvailable(rail_type)) return nullptr;

	return GetString(GetRailTypeInfo((::RailType)rail_type)->strings.menu_text);
}

/* static */ bool ScriptRail::IsRailTile(TileIndex tile)
{
	if (!::IsValidTile(tile)) return false;

	return (::IsTileType(tile, MP_RAILWAY) && !::IsRailDepot(tile)) ||
			(::HasStationTileRail(tile) && !::IsStationTileBlocked(tile)) || ::IsLevelCrossingTile(tile);
}

/* static */ bool ScriptRail::IsLevelCrossingTile(TileIndex tile)
{
	if (!::IsValidTile(tile)) return false;

	return ::IsLevelCrossingTile(tile);
}

/* static */ bool ScriptRail::IsRailDepotTile(TileIndex tile)
{
	if (!::IsValidTile(tile)) return false;

	return ::IsRailDepotTile(tile);
}

/* static */ bool ScriptRail::IsRailStationTile(TileIndex tile)
{
	if (!::IsValidTile(tile)) return false;

	return ::IsRailStationTile(tile);
}

/* static */ bool ScriptRail::IsRailWaypointTile(TileIndex tile)
{
	if (!::IsValidTile(tile)) return false;

	return ::IsRailWaypointTile(tile);
}

/* static */ bool ScriptRail::IsRailTypeAvailable(RailType rail_type)
{
	if ((::RailType)rail_type >= RAILTYPE_END) return false;

	return ScriptObject::GetCompany() == OWNER_DEITY || ::HasRailtypeAvail(ScriptObject::GetCompany(), (::RailType)rail_type);
}

/* static */ ScriptRail::RailType ScriptRail::GetCurrentRailType()
{
	return (RailType)ScriptObject::GetRailType();
}

/* static */ void ScriptRail::SetCurrentRailType(RailType rail_type)
{
	if (!IsRailTypeAvailable(rail_type)) return;

	ScriptObject::SetRailType((::RailType)rail_type);
}

/* static */ bool ScriptRail::TrainCanRunOnRail(ScriptRail::RailType engine_rail_type, ScriptRail::RailType track_rail_type)
{
	if (!ScriptRail::IsRailTypeAvailable(engine_rail_type)) return false;
	if (!ScriptRail::IsRailTypeAvailable(track_rail_type)) return false;

	return ::IsCompatibleRail((::RailType)engine_rail_type, (::RailType)track_rail_type);
}

/* static */ bool ScriptRail::TrainHasPowerOnRail(ScriptRail::RailType engine_rail_type, ScriptRail::RailType track_rail_type)
{\
	if (!ScriptRail::IsRailTypeAvailable(engine_rail_type)) return false;
	if (!ScriptRail::IsRailTypeAvailable(track_rail_type)) return false;

	return ::HasPowerOnRail((::RailType)engine_rail_type, (::RailType)track_rail_type);
}

/* static */ ScriptRail::RailType ScriptRail::GetRailType(TileIndex tile)
{
	if (!ScriptTile::HasTransportType(tile, ScriptTile::TRANSPORT_RAIL)) return RAILTYPE_INVALID;

	return (RailType)::GetRailType(tile);
}

/* static */ bool ScriptRail::ConvertRailType(TileIndex start_tile, TileIndex end_tile, ScriptRail::RailType convert_to)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, ::IsValidTile(start_tile));
	EnforcePrecondition(false, ::IsValidTile(end_tile));
	EnforcePrecondition(false, IsRailTypeAvailable(convert_to));

	return ScriptObject::DoCommand(start_tile, end_tile, convert_to, CMD_CONVERT_RAIL);
}

/* static */ TileIndex ScriptRail::GetRailDepotFrontTile(TileIndex depot)
{
	if (!IsRailDepotTile(depot)) return INVALID_TILE;

	return depot + ::TileOffsByDiagDir(::GetRailDepotDirection(depot));
}

/* static */ ScriptRail::RailTrack ScriptRail::GetRailStationDirection(TileIndex tile)
{
	if (!IsRailStationTile(tile)) return RAILTRACK_INVALID;

	return (RailTrack)::GetRailStationTrackBits(tile);
}

/* static */ bool ScriptRail::BuildRailDepot(TileIndex tile, TileIndex front)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, tile != front);
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforcePrecondition(false, ::IsValidTile(front));
	EnforcePrecondition(false, ::TileX(tile) == ::TileX(front) || ::TileY(tile) == ::TileY(front));
	EnforcePrecondition(false, IsRailTypeAvailable(GetCurrentRailType()));

	uint entrance_dir = (::TileX(tile) == ::TileX(front)) ? (::TileY(tile) < ::TileY(front) ? 1 : 3) : (::TileX(tile) < ::TileX(front) ? 2 : 0);

	return ScriptObject::DoCommand(tile, ScriptObject::GetRailType() | (entrance_dir << 6), 0, CMD_BUILD_TRAIN_DEPOT);
}

/* static */ bool ScriptRail::BuildRailStation(TileIndex tile, RailTrack direction, uint num_platforms, uint platform_length, StationID station_id)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforcePrecondition(false, direction == RAILTRACK_NW_SE || direction == RAILTRACK_NE_SW);
	EnforcePrecondition(false, num_platforms > 0 && num_platforms <= 0xFF);
	EnforcePrecondition(false, platform_length > 0 && platform_length <= 0xFF);
	EnforcePrecondition(false, IsRailTypeAvailable(GetCurrentRailType()));
	EnforcePrecondition(false, station_id == ScriptStation::STATION_NEW || station_id == ScriptStation::STATION_JOIN_ADJACENT || ScriptStation::IsValidStation(station_id));

	uint32 p1 = GetCurrentRailType() | (platform_length << 16) | (num_platforms << 8);
	if (direction == RAILTRACK_NW_SE) p1 |= (1 << 6);
	if (station_id != ScriptStation::STATION_JOIN_ADJACENT) p1 |= (1 << 24);
	return ScriptObject::DoCommand(tile, p1, (ScriptStation::IsValidStation(station_id) ? station_id : INVALID_STATION) << 16, CMD_BUILD_RAIL_STATION);
}

/* static */ bool ScriptRail::BuildNewGRFRailStation(TileIndex tile, RailTrack direction, uint num_platforms, uint platform_length, StationID station_id, CargoID cargo_id, IndustryType source_industry, IndustryType goal_industry, int distance, bool source_station)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforcePrecondition(false, direction == RAILTRACK_NW_SE || direction == RAILTRACK_NE_SW);
	EnforcePrecondition(false, num_platforms > 0 && num_platforms <= 0xFF);
	EnforcePrecondition(false, platform_length > 0 && platform_length <= 0xFF);
	EnforcePrecondition(false, IsRailTypeAvailable(GetCurrentRailType()));
	EnforcePrecondition(false, station_id == ScriptStation::STATION_NEW || station_id == ScriptStation::STATION_JOIN_ADJACENT || ScriptStation::IsValidStation(station_id));
	EnforcePrecondition(false, ScriptCargo::IsValidCargo(cargo_id));
	EnforcePrecondition(false, source_industry == ScriptIndustryType::INDUSTRYTYPE_UNKNOWN || source_industry == ScriptIndustryType::INDUSTRYTYPE_TOWN || ScriptIndustryType::IsValidIndustryType(source_industry));
	EnforcePrecondition(false, goal_industry   == ScriptIndustryType::INDUSTRYTYPE_UNKNOWN || goal_industry   == ScriptIndustryType::INDUSTRYTYPE_TOWN || ScriptIndustryType::IsValidIndustryType(goal_industry));

	uint32 p1 = GetCurrentRailType() | (platform_length << 16) | (num_platforms << 8);
	if (direction == RAILTRACK_NW_SE) p1 |= 1 << 6;
	if (station_id != ScriptStation::STATION_JOIN_ADJACENT) p1 |= (1 << 24);

	const GRFFile *file;
	uint16 res = GetAiPurchaseCallbackResult(
		GSF_STATIONS,
		cargo_id,
		0,
		source_industry,
		goal_industry,
		std::min(255, distance / 2),
		AICE_STATION_GET_STATION_ID,
		source_station ? 0 : 1,
		std::min(15u, num_platforms) << 4 | std::min(15u, platform_length),
		&file
	);
	uint32 p2 = (ScriptStation::IsValidStation(station_id) ? station_id : INVALID_STATION) << 16;
	if (res != CALLBACK_FAILED) {
		int index = 0;
		const StationSpec *spec = StationClass::GetByGrf(file->grfid, res, &index);
		if (spec == nullptr) {
			Debug(grf, 1, "{} returned an invalid station ID for 'AI construction/purchase selection (18)' callback", file->filename);
		} else {
			/* We might have gotten an usable station spec. Try to build it, but if it fails we'll fall back to the original station. */
			if (ScriptObject::DoCommand(tile, p1, p2 | spec->cls_id | index << 8, CMD_BUILD_RAIL_STATION)) return true;
		}
	}

	return ScriptObject::DoCommand(tile, p1, p2, CMD_BUILD_RAIL_STATION);
}

/* static */ bool ScriptRail::BuildRailWaypoint(TileIndex tile)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforcePrecondition(false, IsRailTile(tile));
	EnforcePrecondition(false, GetRailTracks(tile) == RAILTRACK_NE_SW || GetRailTracks(tile) == RAILTRACK_NW_SE);
	EnforcePrecondition(false, IsRailTypeAvailable(GetCurrentRailType()));

	return ScriptObject::DoCommand(tile, GetCurrentRailType() | (GetRailTracks(tile) == RAILTRACK_NE_SW ? AXIS_X : AXIS_Y) << 6 | 1 << 8 | 1 << 16, STAT_CLASS_WAYP | INVALID_STATION << 16, CMD_BUILD_RAIL_WAYPOINT);
}

/* static */ bool ScriptRail::RemoveRailWaypointTileRectangle(TileIndex tile, TileIndex tile2, bool keep_rail)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforcePrecondition(false, ::IsValidTile(tile2));

	return ScriptObject::DoCommand(tile, tile2, keep_rail ? 1 : 0, CMD_REMOVE_FROM_RAIL_WAYPOINT);
}

/* static */ bool ScriptRail::RemoveRailStationTileRectangle(TileIndex tile, TileIndex tile2, bool keep_rail)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforcePrecondition(false, ::IsValidTile(tile2));

	return ScriptObject::DoCommand(tile, tile2, keep_rail ? 1 : 0, CMD_REMOVE_FROM_RAIL_STATION);
}

/* static */ uint ScriptRail::GetRailTracks(TileIndex tile)
{
	if (!IsRailTile(tile)) return RAILTRACK_INVALID;

	if (IsRailStationTile(tile) || IsRailWaypointTile(tile)) return ::TrackToTrackBits(::GetRailStationTrack(tile));
	if (IsLevelCrossingTile(tile)) return ::GetCrossingRailBits(tile);
	if (IsRailDepotTile(tile)) return ::TRACK_BIT_NONE;
	return ::GetTrackBits(tile);
}

/* static */ bool ScriptRail::BuildRailTrack(TileIndex tile, RailTrack rail_track)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforcePrecondition(false, rail_track != 0);
	EnforcePrecondition(false, (rail_track & ~::TRACK_BIT_ALL) == 0);
	EnforcePrecondition(false, KillFirstBit((uint)rail_track) == 0);
	EnforcePrecondition(false, IsRailTypeAvailable(GetCurrentRailType()));

	return ScriptObject::DoCommand(tile, tile, GetCurrentRailType() | (FindFirstTrack((::TrackBits)rail_track) << 6), CMD_BUILD_RAILROAD_TRACK);
}

/* static */ bool ScriptRail::RemoveRailTrack(TileIndex tile, RailTrack rail_track)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforcePrecondition(false, ::IsPlainRailTile(tile) || ::IsLevelCrossingTile(tile));
	EnforcePrecondition(false, GetRailTracks(tile) & rail_track);
	EnforcePrecondition(false, KillFirstBit((uint)rail_track) == 0);

	return ScriptObject::DoCommand(tile, tile, FindFirstTrack((::TrackBits)rail_track) << 6, CMD_REMOVE_RAILROAD_TRACK);
}

/* static */ bool ScriptRail::AreTilesConnected(TileIndex from, TileIndex tile, TileIndex to)
{
	if (!IsRailTile(tile)) return false;
	if (from == to || ScriptMap::DistanceManhattan(from, tile) != 1 || ScriptMap::DistanceManhattan(tile, to) != 1) return false;

	if (to < from) ::Swap(from, to);

	if (tile - from == 1) {
		if (to - tile == 1) return (GetRailTracks(tile) & RAILTRACK_NE_SW) != 0;
		if (to - tile == ::MapSizeX()) return (GetRailTracks(tile) & RAILTRACK_NE_SE) != 0;
	} else if (tile - from == ::MapSizeX()) {
		if (tile - to == 1) return (GetRailTracks(tile) & RAILTRACK_NW_NE) != 0;
		if (to - tile == 1) return (GetRailTracks(tile) & RAILTRACK_NW_SW) != 0;
		if (to - tile == ::MapSizeX()) return (GetRailTracks(tile) & RAILTRACK_NW_SE) != 0;
	} else {
		return (GetRailTracks(tile) & RAILTRACK_SW_SE) != 0;
	}

	NOT_REACHED();
}

/**
 * Prepare the second parameter for CmdBuildRailroadTrack and CmdRemoveRailroadTrack. The direction
 * depends on all three tiles. Sometimes the third tile needs to be adjusted.
 */
static uint32 SimulateDrag(TileIndex from, TileIndex tile, TileIndex *to)
{
	int diag_offset = abs(abs((int)::TileX(*to) - (int)::TileX(tile)) - abs((int)::TileY(*to) - (int)::TileY(tile)));
	uint32 p2 = 0;
	if (::TileY(from) == ::TileY(*to)) {
		p2 |= (TRACK_X << 6);
		*to -= Clamp((int)::TileX(*to) - (int)::TileX(tile), -1, 1);
	} else if (::TileX(from) == ::TileX(*to)) {
		p2 |= (TRACK_Y << 6);
		*to -= ::MapSizeX() * Clamp((int)::TileY(*to) - (int)::TileY(tile), -1, 1);
	} else if (::TileY(from) < ::TileY(tile)) {
		if (::TileX(*to) < ::TileX(tile)) {
			p2 |= (TRACK_UPPER << 6);
		} else {
			p2 |= (TRACK_LEFT << 6);
		}
		if (diag_offset != 0) {
			*to -= Clamp((int)::TileX(*to) - (int)::TileX(tile), -1, 1);
		} else {
			*to -= ::MapSizeX() * Clamp((int)::TileY(*to) - (int)::TileY(tile), -1, 1);
		}
	} else if (::TileY(from) > ::TileY(tile)) {
		if (::TileX(*to) < ::TileX(tile)) {
			p2 |= (TRACK_RIGHT << 6);
		} else {
			p2 |= (TRACK_LOWER << 6);
		}
		if (diag_offset != 0) {
			*to -= Clamp((int)::TileX(*to) - (int)::TileX(tile), -1, 1);
		} else {
			*to -= ::MapSizeX() * Clamp((int)::TileY(*to) - (int)::TileY(tile), -1, 1);
		}
	} else if (::TileX(from) < ::TileX(tile)) {
		if (::TileY(*to) < ::TileY(tile)) {
			p2 |= (TRACK_UPPER << 6);
		} else {
			p2 |= (TRACK_RIGHT << 6);
		}
		if (diag_offset == 0) {
			*to -= Clamp((int)::TileX(*to) - (int)::TileX(tile), -1, 1);
		} else {
			*to -= ::MapSizeX() * Clamp((int)::TileY(*to) - (int)::TileY(tile), -1, 1);
		}
	} else if (::TileX(from) > ::TileX(tile)) {
		if (::TileY(*to) < ::TileY(tile)) {
			p2 |= (TRACK_LEFT << 6);
		} else {
			p2 |= (TRACK_LOWER << 6);
		}
		if (diag_offset == 0) {
			*to -= Clamp((int)::TileX(*to) - (int)::TileX(tile), -1, 1);
		} else {
			*to -= ::MapSizeX() * Clamp((int)::TileY(*to) - (int)::TileY(tile), -1, 1);
		}
	}
	return p2;
}

/* static */ bool ScriptRail::BuildRail(TileIndex from, TileIndex tile, TileIndex to)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, ::IsValidTile(from));
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforcePrecondition(false, ::IsValidTile(to));
	EnforcePrecondition(false, ::DistanceManhattan(from, tile) == 1);
	EnforcePrecondition(false, ::DistanceManhattan(tile, to) >= 1);
	EnforcePrecondition(false, IsRailTypeAvailable(GetCurrentRailType()));
	int diag_offset = abs(abs((int)::TileX(to) - (int)::TileX(tile)) - abs((int)::TileY(to) - (int)::TileY(tile)));
	EnforcePrecondition(false, diag_offset <= 1 ||
			(::TileX(from) == ::TileX(tile) && ::TileX(tile) == ::TileX(to)) ||
			(::TileY(from) == ::TileY(tile) && ::TileY(tile) == ::TileY(to)));

	uint32 p2 = SimulateDrag(from, tile, &to) | 1 << 10 | ScriptRail::GetCurrentRailType();;
	return ScriptObject::DoCommand(tile, to, p2, CMD_BUILD_RAILROAD_TRACK);
}

/* static */ bool ScriptRail::RemoveRail(TileIndex from, TileIndex tile, TileIndex to)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, ::IsValidTile(from));
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforcePrecondition(false, ::IsValidTile(to));
	EnforcePrecondition(false, ::DistanceManhattan(from, tile) == 1);
	EnforcePrecondition(false, ::DistanceManhattan(tile, to) >= 1);
	int diag_offset = abs(abs((int)::TileX(to) - (int)::TileX(tile)) - abs((int)::TileY(to) - (int)::TileY(tile)));
	EnforcePrecondition(false, diag_offset <= 1 ||
			(::TileX(from) == ::TileX(tile) && ::TileX(tile) == ::TileX(to)) ||
			(::TileY(from) == ::TileY(tile) && ::TileY(tile) == ::TileY(to)));

	uint32 p2 = SimulateDrag(from, tile, &to);
	return ScriptObject::DoCommand(tile, to, p2, CMD_REMOVE_RAILROAD_TRACK);
}

/**
 * Contains information about the trackdir that belongs to a track when entering
 *   from a specific direction.
 */
struct ScriptRailSignalData {
	Track track;        ///< The track that will be taken to travel.
	Trackdir trackdir;  ///< The Trackdir belonging to that track.
	uint signal_cycles; ///< How many times the signal should be cycled in order to build it in the correct direction.
};

static const int NUM_TRACK_DIRECTIONS = 3; ///< The number of directions you can go when entering a tile.

/**
 * List information about the trackdir and number of needed cycles for building signals when
 *   entering a track from a specific direction. The first index is the difference between the
 *   TileIndex of the previous and current tile, where (-)MapSizeX is replaced with -2 / 2 and
 *   2 it added.
 */
static const ScriptRailSignalData _possible_trackdirs[5][NUM_TRACK_DIRECTIONS] = {
	{{TRACK_UPPER,   TRACKDIR_UPPER_E, 0}, {TRACK_Y,       TRACKDIR_Y_SE,    0}, {TRACK_LEFT,    TRACKDIR_LEFT_S,  1}},
	{{TRACK_RIGHT,   TRACKDIR_RIGHT_S, 1}, {TRACK_X,       TRACKDIR_X_SW,    1}, {TRACK_UPPER,   TRACKDIR_UPPER_W, 1}},
	{{INVALID_TRACK, INVALID_TRACKDIR, 0}, {INVALID_TRACK, INVALID_TRACKDIR, 0}, {INVALID_TRACK, INVALID_TRACKDIR, 0}},
	{{TRACK_LOWER,   TRACKDIR_LOWER_E, 0}, {TRACK_X,       TRACKDIR_X_NE,    0}, {TRACK_LEFT,    TRACKDIR_LEFT_N,  0}},
	{{TRACK_RIGHT,   TRACKDIR_RIGHT_N, 0}, {TRACK_Y,       TRACKDIR_Y_NW,    1}, {TRACK_LOWER,   TRACKDIR_LOWER_W, 1}}
};

/* static */ ScriptRail::SignalType ScriptRail::GetSignalType(TileIndex tile, TileIndex front)
{
	if (ScriptMap::DistanceManhattan(tile, front) != 1) return SIGNALTYPE_NONE;
	if (!::IsTileType(tile, MP_RAILWAY) || !::HasSignals(tile)) return SIGNALTYPE_NONE;

	int data_index = 2 + (::TileX(front) - ::TileX(tile)) + 2 * (::TileY(front) - ::TileY(tile));

	for (int i = 0; i < NUM_TRACK_DIRECTIONS; i++) {
		const Track &track = _possible_trackdirs[data_index][i].track;
		if (!(::TrackToTrackBits(track) & GetRailTracks(tile))) continue;
		if (!HasSignalOnTrack(tile, track)) continue;
		if (!HasSignalOnTrackdir(tile, _possible_trackdirs[data_index][i].trackdir)) continue;
		SignalType st = (SignalType)::GetSignalType(tile, track);
		if (HasSignalOnTrackdir(tile, ::ReverseTrackdir(_possible_trackdirs[data_index][i].trackdir))) st = (SignalType)(st | SIGNALTYPE_TWOWAY);
		return st;
	}

	return SIGNALTYPE_NONE;
}

/**
 * Check if signal_type is a valid SignalType.
 */
static bool IsValidSignalType(int signal_type)
{
	if (signal_type < ScriptRail::SIGNALTYPE_NORMAL || signal_type > ScriptRail::SIGNALTYPE_COMBO_TWOWAY) return false;
	if (signal_type > ScriptRail::SIGNALTYPE_PBS_ONEWAY && signal_type < ScriptRail::SIGNALTYPE_NORMAL_TWOWAY) return false;
	return true;
}

/* static */ bool ScriptRail::BuildSignal(TileIndex tile, TileIndex front, SignalType signal)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, ScriptMap::DistanceManhattan(tile, front) == 1)
	EnforcePrecondition(false, ::IsPlainRailTile(tile));
	EnforcePrecondition(false, ::IsValidSignalType(signal));

	Track track = INVALID_TRACK;
	uint signal_cycles;

	int data_index = 2 + (::TileX(front) - ::TileX(tile)) + 2 * (::TileY(front) - ::TileY(tile));
	for (int i = 0; i < NUM_TRACK_DIRECTIONS; i++) {
		const Track &t = _possible_trackdirs[data_index][i].track;
		if (!(::TrackToTrackBits(t) & GetRailTracks(tile))) continue;
		track = t;
		signal_cycles = _possible_trackdirs[data_index][i].signal_cycles;
		break;
	}
	EnforcePrecondition(false, track != INVALID_TRACK);

	uint p1 = track;
	if (signal < SIGNALTYPE_TWOWAY) {
		if (signal != SIGNALTYPE_PBS && signal != SIGNALTYPE_PBS_ONEWAY) signal_cycles++;
		p1 |= (signal_cycles << 15);
	}
	p1 |= ((signal >= SIGNALTYPE_TWOWAY ? signal ^ SIGNALTYPE_TWOWAY : signal) << 5);

	return ScriptObject::DoCommand(tile, p1, 0, CMD_BUILD_SIGNALS);
}

/* static */ bool ScriptRail::RemoveSignal(TileIndex tile, TileIndex front)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, ScriptMap::DistanceManhattan(tile, front) == 1)
	EnforcePrecondition(false, GetSignalType(tile, front) != SIGNALTYPE_NONE);

	Track track = INVALID_TRACK;
	int data_index = 2 + (::TileX(front) - ::TileX(tile)) + 2 * (::TileY(front) - ::TileY(tile));
	for (int i = 0; i < NUM_TRACK_DIRECTIONS; i++) {
		const Track &t = _possible_trackdirs[data_index][i].track;
		if (!(::TrackToTrackBits(t) & GetRailTracks(tile))) continue;
		track = t;
		break;
	}
	EnforcePrecondition(false, track != INVALID_TRACK);

	return ScriptObject::DoCommand(tile, track, 0, CMD_REMOVE_SIGNALS);
}

/* static */ Money ScriptRail::GetBuildCost(RailType railtype, BuildType build_type)
{
	if (!ScriptRail::IsRailTypeAvailable(railtype)) return -1;

	switch (build_type) {
		case BT_TRACK:    return ::RailBuildCost((::RailType)railtype);
		case BT_SIGNAL:   return ::GetPrice(PR_BUILD_SIGNALS, 1, nullptr);
		case BT_DEPOT:    return ::GetPrice(PR_BUILD_DEPOT_TRAIN, 1, nullptr);
		case BT_STATION:  return ::GetPrice(PR_BUILD_STATION_RAIL, 1, nullptr) + ::GetPrice(PR_BUILD_STATION_RAIL_LENGTH, 1, nullptr);
		case BT_WAYPOINT: return ::GetPrice(PR_BUILD_WAYPOINT_RAIL, 1, nullptr);
		default: return -1;
	}
}

/* static */ int32 ScriptRail::GetMaxSpeed(RailType railtype)
{
	if (!ScriptRail::IsRailTypeAvailable(railtype)) return -1;

	return ::GetRailTypeInfo((::RailType)railtype)->max_speed;
}

/* static */ uint16 ScriptRail::GetMaintenanceCostFactor(RailType railtype)
{
	if (!ScriptRail::IsRailTypeAvailable(railtype)) return 0;

	return ::GetRailTypeInfo((::RailType)railtype)->maintenance_multiplier;
}
