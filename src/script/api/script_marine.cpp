/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_marine.cpp Implementation of ScriptMarine. */

#include "../../stdafx.h"
#include "script_marine.hpp"
#include "script_station.hpp"
#include "../../station_base.h"
#include "../../tile_cmd.h"

#include "../../safeguards.h"


/* static */ bool ScriptMarine::IsWaterDepotTile(TileIndex tile)
{
	if (!::IsValidTile(tile)) return false;

	return ::IsTileType(tile, MP_WATER) && ::GetWaterTileType(tile) == WATER_TILE_DEPOT;
}

/* static */ bool ScriptMarine::IsDockTile(TileIndex tile)
{
	if (!::IsValidTile(tile)) return false;

	return ::IsTileType(tile, MP_STATION) && ::IsDock(tile);
}

/* static */ bool ScriptMarine::IsBuoyTile(TileIndex tile)
{
	if (!::IsValidTile(tile)) return false;

	return ::IsTileType(tile, MP_STATION) && ::IsBuoy(tile);
}

/* static */ bool ScriptMarine::IsLockTile(TileIndex tile)
{
	if (!::IsValidTile(tile)) return false;

	return ::IsTileType(tile, MP_WATER) && ::GetWaterTileType(tile) == WATER_TILE_LOCK;
}

/* static */ bool ScriptMarine::IsCanalTile(TileIndex tile)
{
	if (!::IsValidTile(tile)) return false;

	return ::IsTileType(tile, MP_WATER) && ::IsCanal(tile);
}

/* static */ bool ScriptMarine::AreWaterTilesConnected(TileIndex t1, TileIndex t2)
{
	if (!::IsValidTile(t1)) return false;
	if (!::IsValidTile(t2)) return false;

	/* Tiles not neighbouring */
	if (::DistanceManhattan(t1, t2) != 1) return false;

	DiagDirection to_other_tile = ::DiagdirBetweenTiles(t2, t1);

	/* Determine the reachable tracks from the shared edge */
	TrackBits gtts1 = ::TrackStatusToTrackBits(::GetTileTrackStatus(t1, TRANSPORT_WATER, 0, ReverseDiagDir(to_other_tile))) & ::DiagdirReachesTracks(to_other_tile);
	if (gtts1 == TRACK_BIT_NONE) return false;

	to_other_tile = ReverseDiagDir(to_other_tile);
	TrackBits gtts2 = ::TrackStatusToTrackBits(::GetTileTrackStatus(t2, TRANSPORT_WATER, 0, ReverseDiagDir(to_other_tile))) & ::DiagdirReachesTracks(to_other_tile);

	return gtts2 != TRACK_BIT_NONE;
}

/* static */ bool ScriptMarine::BuildWaterDepot(TileIndex tile, TileIndex front)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforcePrecondition(false, ::IsValidTile(front));
	EnforcePrecondition(false, (::TileX(front) == ::TileX(tile)) != (::TileY(front) == ::TileY(tile)));

	return ScriptObject::DoCommand(tile, ::TileX(front) == ::TileX(tile), 0, CMD_BUILD_SHIP_DEPOT);
}

/* static */ bool ScriptMarine::BuildDock(TileIndex tile, StationID station_id)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforcePrecondition(false, station_id == ScriptStation::STATION_NEW || station_id == ScriptStation::STATION_JOIN_ADJACENT || ScriptStation::IsValidStation(station_id));

	uint p1 = station_id == ScriptStation::STATION_JOIN_ADJACENT ? 0 : 1;
	uint p2 = (ScriptStation::IsValidStation(station_id) ? station_id : INVALID_STATION) << 16;
	return ScriptObject::DoCommand(tile, p1, p2, CMD_BUILD_DOCK);
}

/* static */ bool ScriptMarine::BuildBuoy(TileIndex tile)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, ::IsValidTile(tile));

	return ScriptObject::DoCommand(tile, 0, 0, CMD_BUILD_BUOY);
}

/* static */ bool ScriptMarine::BuildLock(TileIndex tile)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, ::IsValidTile(tile));

	return ScriptObject::DoCommand(tile, 0, 0, CMD_BUILD_LOCK);
}

/* static */ bool ScriptMarine::BuildCanal(TileIndex tile)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, ::IsValidTile(tile));

	return ScriptObject::DoCommand(tile, tile, WATER_CLASS_CANAL, CMD_BUILD_CANAL);
}

/* static */ bool ScriptMarine::RemoveWaterDepot(TileIndex tile)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforcePrecondition(false, IsWaterDepotTile(tile));

	return ScriptObject::DoCommand(tile, 0, 0, CMD_LANDSCAPE_CLEAR);
}

/* static */ bool ScriptMarine::RemoveDock(TileIndex tile)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforcePrecondition(false, IsDockTile(tile));

	return ScriptObject::DoCommand(tile, 0, 0, CMD_LANDSCAPE_CLEAR);
}

/* static */ bool ScriptMarine::RemoveBuoy(TileIndex tile)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforcePrecondition(false, IsBuoyTile(tile));

	return ScriptObject::DoCommand(tile, 0, 0, CMD_LANDSCAPE_CLEAR);
}

/* static */ bool ScriptMarine::RemoveLock(TileIndex tile)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforcePrecondition(false, IsLockTile(tile));

	return ScriptObject::DoCommand(tile, 0, 0, CMD_LANDSCAPE_CLEAR);
}

/* static */ bool ScriptMarine::RemoveCanal(TileIndex tile)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforcePrecondition(false, IsCanalTile(tile));

	return ScriptObject::DoCommand(tile, 0, 0, CMD_LANDSCAPE_CLEAR);
}

/* static */ Money ScriptMarine::GetBuildCost(BuildType build_type)
{
	switch (build_type) {
		case BT_DOCK:  return ::GetPrice(PR_BUILD_STATION_DOCK, 1, nullptr);
		case BT_DEPOT: return ::GetPrice(PR_BUILD_DEPOT_SHIP, 1, nullptr);
		case BT_BUOY:  return ::GetPrice(PR_BUILD_WAYPOINT_BUOY, 1, nullptr);
		case BT_LOCK:  return ::GetPrice(PR_BUILD_LOCK, 1, nullptr);
		case BT_CANAL: return ::GetPrice(PR_BUILD_CANAL, 1, nullptr);
		default: return -1;
	}
}
