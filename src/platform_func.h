/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file platform_func.h Functions related with platforms (tiles in a row that are connected somehow). */

#ifndef PLATFORM_FUNC_H
#define PLATFORM_FUNC_H

#include "station_map.h"
#include "depot_map.h"
#include "platform_type.h"
#include "road_map.h"

/**
 * Check if a tile is a valid continuation to a railstation tile.
 * The tile \a test_tile is a valid continuation to \a station_tile, if all of the following are true:
 * \li \a test_tile is a rail station tile
 * \li the railtype of \a test_tile is compatible with the railtype of \a station_tile
 * \li the tracks on \a test_tile and \a station_tile are in the same direction
 * \li both tiles belong to the same station
 * \li \a test_tile is not blocked (@see IsStationTileBlocked)
 * @param test_tile Tile to test
 * @param station_tile Station tile to compare with
 * @pre IsRailStationTile(station_tile)
 * @return true if the two tiles are compatible
 */
static inline bool IsCompatibleTrainStationTile(TileIndex test_tile, TileIndex station_tile)
{
	assert(IsRailStationTile(station_tile));
	return IsRailStationTile(test_tile) && IsCompatibleRail(GetRailType(test_tile), GetRailType(station_tile)) &&
			GetRailStationAxis(test_tile) == GetRailStationAxis(station_tile) &&
			GetStationIndex(test_tile) == GetStationIndex(station_tile) &&
			!IsStationTileBlocked(test_tile);
}

/**
 * Check if a tile is a valid continuation to a big rail depot tile.
 * The tile \a test_tile is a valid continuation to \a depot_tile, if all of the following are true:
 * \li \a test_tile is a big depot tile
 * \li \a test_tile and \a depot_tile have the same rail type
 * \li the tracks on \a test_tile and \a depot_tile are in the same direction
 * \li both tiles belong to the same depot
 * @param test_tile Tile to test
 * @param depot_tile Depot tile to compare with
 * @pre IsBigRailDepotTile(depot_tile)
 * @return true if the two tiles are compatible
 */
static inline bool IsCompatibleTrainDepotTile(TileIndex test_tile, TileIndex depot_tile)
{
	assert(IsBigRailDepotTile(depot_tile));
	return IsBigRailDepotTile(test_tile) &&
			GetRailType(test_tile) == GetRailType(depot_tile) &&
			GetRailDepotTrack(test_tile) == GetRailDepotTrack(depot_tile) &&
			GetDepotIndex(test_tile) == GetDepotIndex(depot_tile);
}

/**
 * Check if a tile is a valid continuation to a big road depot tile.
 * The tile \a test_tile is a valid continuation to \a depot_tile, if all of the following are true:
 * \li \a test_tile is a big depot tile
 * \li \a test_tile and \a depot_tile have the same road type and appropriate road bits
 * \li the tracks on \a test_tile and \a depot_tile are in the same direction
 * \li both tiles belong to the same depot
 * @param test_tile Tile to test
 * @param depot_tile Depot tile to compare with
 * @param rtt Whether road or tram type.
 * @pre IsBigRoadDepotTile(depot_tile)
 * @return true if the two tiles are compatible
 */
static inline bool IsCompatibleRoadDepotTile(TileIndex test_tile, TileIndex depot_tile, RoadTramType rtt)
{
	assert(IsBigRoadDepotTile(depot_tile));
	if (!IsBigRoadDepotTile(test_tile)) return false;
	if (GetDepotIndex(test_tile) != GetDepotIndex(depot_tile)) return false;
	if (GetRoadType(depot_tile, rtt) != GetRoadType(test_tile, rtt)) return false;

	DiagDirection dir = DiagdirBetweenTiles(test_tile, depot_tile);
	assert(dir != INVALID_DIAGDIR);
	return (GetRoadBits(test_tile, rtt) & DiagDirToRoadBits(dir)) != ROAD_NONE;
}

/**
 * Returns the type of platform of a given tile.
 * @param tile Tile to check
 * @return the type of platform (rail station, rail waypoint...)
 */
static inline PlatformType GetPlatformType(TileIndex tile)
{
	switch (GetTileType(tile)) {
		case MP_STATION:
			if (IsRailStation(tile)) return PT_RAIL_STATION;
			if (IsRailWaypoint(tile)) return PT_RAIL_WAYPOINT;
			break;
		case MP_RAILWAY:
			if (IsBigRailDepotTile(tile)) return PT_RAIL_DEPOT;
			break;
		case MP_ROAD:
			if (IsBigRoadDepotTile(tile)) return PT_ROAD_DEPOT;
			break;
		default: break;
	}

	return INVALID_PLATFORM_TYPE;
}

/**
 * Check whether a tile is a known platform type.
 * @param tile to check
 * @return whether the tile is a known platform type.
 */
static inline bool IsPlatformTile(TileIndex tile)
{
	return GetPlatformType(tile) != INVALID_PLATFORM_TYPE;
}

/**
 * Check whether a platform tile is reserved.
 * @param tile to check
 * @return whether the platform tile is reserved
 */
static inline bool HasPlatformReservation(TileIndex tile)
{
	switch(GetPlatformType(tile)) {
		case PT_RAIL_STATION:
		case PT_RAIL_WAYPOINT:
			return HasStationReservation(tile);
		case PT_RAIL_DEPOT:
			return HasDepotReservation(tile);
		default: NOT_REACHED();
	}
}

/**
 * Check whether two tiles are compatible platform tiles: they must have the same
 * platform type and (depending on the platform type) its railtype or other specs.
 * @param test_tile the tile to check
 * @param orig_tile the tile with the platform type we are interested in
 * @param rtt Whether to check road or tram types (only for road transport);
 * @return whether the two tiles are compatible tiles for defining a platform
 */
static inline bool IsCompatiblePlatformTile(TileIndex test_tile, TileIndex orig_tile, RoadTramType rtt = RTT_ROAD)
{
	switch (GetPlatformType(orig_tile)) {
		case PT_RAIL_STATION:
			return IsCompatibleTrainStationTile(test_tile, orig_tile);
		case PT_RAIL_WAYPOINT:
			return test_tile == orig_tile;
		case PT_RAIL_DEPOT:
			return IsCompatibleTrainDepotTile(test_tile, orig_tile);
		case PT_ROAD_DEPOT:
			return IsCompatibleRoadDepotTile(test_tile, orig_tile, rtt);
		default: NOT_REACHED();
	}
}

void SetPlatformReservation(TileIndex start, DiagDirection dir, bool b);
void SetPlatformReservation(TileIndex start, bool b);

uint GetPlatformLength(TileIndex tile, RoadTramType rtt = RTT_ROAD);
uint GetPlatformLength(TileIndex tile, DiagDirection dir, RoadTramType rtt = RTT_ROAD);

TileIndex GetPlatformExtremeTile(TileIndex tile, DiagDirection dir);
TileArea GetPlatformTileArea(TileIndex tile);

bool IsAnyStartPlatformTile(TileIndex tile);

#endif /* PLATFORM_FUNC_H */
