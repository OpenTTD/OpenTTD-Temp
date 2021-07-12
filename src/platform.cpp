/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file platform.cpp Implementation of platform functions. */

#include "stdafx.h"
#include "station_map.h"
#include "platform_func.h"
#include "viewport_func.h"
#include "depot_base.h"
#include "vehicle_base.h"
#include "engine_base.h"

/**
 * Set the reservation for a complete station platform.
 * @pre IsRailStationTile(start)
 * @param start starting tile of the platform
 * @param dir the direction in which to follow the platform
 * @param b the state the reservation should be set to
 */
void SetRailStationPlatformReservation(TileIndex start, DiagDirection dir, bool b)
{
	TileIndex     tile = start;
	TileIndexDiff diff = TileOffsByDiagDir(dir);

	assert(IsRailStationTile(start));
	assert(GetRailStationAxis(start) == DiagDirToAxis(dir));

	do {
		SetRailStationReservation(tile, b);
		MarkTileDirtyByTile(tile);
		tile = TILE_ADD(tile, diff);
	} while (IsCompatibleTrainStationTile(tile, start));
}


/**
 * Set the reservation for a complete depot platform.
 * @pre IsBigRailDepotTile(start)
 * @param start starting tile of the platform
 * @param dir the direction in which to follow the platform
 * @param b the state the reservation should be set to
 */
void SetRailDepotPlatformReservation(TileIndex start, DiagDirection dir, bool b)
{
	TileIndex     tile = start;
	TileIndexDiff diff = TileOffsByDiagDir(dir);

	assert(IsBigRailDepotTile(start));
	assert(GetRailDepotTrack(start) == DiagDirToDiagTrack(dir));

	do {
		SetDepotReservation(tile, b);
		MarkTileDirtyByTile(tile);
		tile = TILE_ADD(tile, diff);
	} while (IsCompatibleTrainDepotTile(tile, start));
}

/**
 * Set the reservation for a complete platform in a given direction.
 * @param start starting tile of the platform
 * @param dir the direction in which to follow the platform
 * @param b the state the reservation should be set to
 */
void SetPlatformReservation(TileIndex start, DiagDirection dir, bool b)
{
	switch (GetPlatformType(start)) {
		case PT_RAIL_STATION:
			SetRailStationPlatformReservation(start, dir, b);
			return;
		case PT_RAIL_WAYPOINT:
			SetRailStationReservation(start, b);
			return;
		case PT_RAIL_DEPOT:
			SetRailDepotPlatformReservation(start, dir, b);
			return;
		default: NOT_REACHED();
	}
}

/**
 * Set the reservation for a complete platform.
 * @param start A tile of the platform
 * @param b the state the reservation should be set to
 */
void SetPlatformReservation(TileIndex start, bool b)
{
	DiagDirection dir;
	switch (GetPlatformType(start)) {
		case PT_RAIL_STATION:
			NOT_REACHED();
		case PT_RAIL_WAYPOINT:
			NOT_REACHED();
		case PT_RAIL_DEPOT:
			assert(IsBigRailDepotTile(start));
			dir = GetRailDepotDirection(start);
			SetRailDepotPlatformReservation(start, dir, b);
			SetRailDepotPlatformReservation(start, ReverseDiagDir(dir), b);
			return;
		default: NOT_REACHED();
	}
}

/**
 * Get the length of a rail station platform.
 * @pre IsRailStationTile(tile)
 * @param tile Tile to check
 * @return The length of the platform in tile length.
 */
uint GetRailStationPlatformLength(TileIndex tile)
{
	assert(IsRailStationTile(tile));

	TileIndexDiff delta = (GetRailStationAxis(tile) == AXIS_X ? TileDiffXY(1, 0) : TileDiffXY(0, 1));

	TileIndex t = tile;
	uint len = 0;
	do {
		t -= delta;
		len++;
	} while (IsCompatibleTrainStationTile(t, tile));

	t = tile;
	do {
		t += delta;
		len++;
	} while (IsCompatibleTrainStationTile(t, tile));

	return len - 1;
}

/**
 * Get the length of a rail station platform in a given direction.
 * @pre IsRailStationTile(tile)
 * @param tile Tile to check
 * @param dir Direction to check
 * @return The length of the platform in tile length in the given direction.
 */
uint GetRailStationPlatformLength(TileIndex tile, DiagDirection dir)
{
	TileIndex start_tile = tile;
	uint length = 0;
	assert(IsRailStationTile(tile));
	assert(dir < DIAGDIR_END);

	do {
		length++;
		tile += TileOffsByDiagDir(dir);
	} while (IsCompatibleTrainStationTile(tile, start_tile));

	return length;
}

/**
 * Get the length of a rail depot platform.
 * @pre IsDepotTypeTile(tile, TRANSPORT_RAIL)
 * @param tile Tile to check
 * @return The length of the platform in tile length.
 */
uint GetRailDepotPlatformLength(TileIndex tile)
{
	assert(IsBigRailDepotTile(tile));

	TileIndexDiff delta = (GetRailDepotTrack(tile) == TRACK_X ? TileDiffXY(1, 0) : TileDiffXY(0, 1));

	TileIndex t = tile;
	uint len = 0;
	do {
		t -= delta;
		len++;
	} while (IsCompatibleTrainDepotTile(t, tile));

	t = tile;
	do {
		t += delta;
		len++;
	} while (IsCompatibleTrainDepotTile(t, tile));

	return len - 1;
}

/**
 * Get the length of a road depot platform.
 * @pre IsDepotTypeTile(tile, TRANSPORT_ROAD)
 * @param tile Tile to check
 * @param rtt Whether to check for road or tram type.
 * @return The length of the platform in tile length.
 */
uint GetRoadDepotPlatformLength(TileIndex tile, RoadTramType rtt)
{
	assert(IsBigRoadDepotTile(tile));

	DiagDirection dir = GetRoadDepotDirection(tile);
	TileIndexDiff delta = TileOffsByDiagDir(dir);

	TileIndex t = tile;
	uint len = 0;
	do {
		len++;
		if ((GetRoadBits(t, rtt) & DiagDirToRoadBits(dir)) == ROAD_NONE) break;
		t -= delta;
	} while (IsCompatibleRoadDepotTile(t, tile, rtt));

	t = tile;
	dir = ReverseDiagDir(dir);
	do {
		len++;
		if ((GetRoadBits(t, rtt) & DiagDirToRoadBits(dir)) == ROAD_NONE) break;
		t += delta;
	} while (IsCompatibleRoadDepotTile(t, tile, rtt));

	return len - 1;
}


/**
 * Get the length of a rail depot platform in a given direction.
 * @pre IsRailDepotTile(tile)
 * @param tile Tile to check
 * @param dir Direction to check
 * @return The length of the platform in tile length in the given direction.
 */
uint GetRailDepotPlatformLength(TileIndex tile, DiagDirection dir)
{
	TileIndex start_tile = tile;
	uint length = 0;
	assert(IsBigRailDepotTile(tile));
	assert(dir < DIAGDIR_END);

	do {
		length++;
		tile += TileOffsByDiagDir(dir);
	} while (IsCompatibleTrainDepotTile(tile, start_tile));

	return length;
}

/**
 * Get the length of a road depot platform in a given direction.
 * @pre IsRoadDepotTile(tile)
 * @param tile Tile to check
 * @param dir Direction to check
 * @param rtt Whether to check for road or tram type.
 * @return The length of the platform in tile length in the given direction.
 */
uint GetRoadDepotPlatformLength(TileIndex tile, DiagDirection dir, RoadTramType rtt)
{
	TileIndex start_tile = tile;
	uint length = 0;
	assert(IsBigRoadDepotTile(tile));
	assert(dir < DIAGDIR_END);

	do {
		length++;
		if ((GetRoadBits(tile, rtt) & DiagDirToRoadBits(dir)) == ROAD_NONE) break;
		tile += TileOffsByDiagDir(dir);
	} while (IsCompatibleRoadDepotTile(tile, start_tile, rtt));

	return length;
}

/**
 * Get the length of a platform.
 * @param tile Tile to check
 * @param rtt Whether to check for road or tram type (only for road transport).
 * @return The length of the platform in tile length.
 */
uint GetPlatformLength(TileIndex tile, RoadTramType rtt)
{
	switch (GetPlatformType(tile)) {
		case PT_RAIL_STATION:
			return GetRailStationPlatformLength(tile);
		case PT_RAIL_WAYPOINT:
			return 1;
		case PT_RAIL_DEPOT:
			return GetRailDepotPlatformLength(tile);
		case PT_ROAD_DEPOT:
			return GetRoadDepotPlatformLength(tile, rtt);
		default: NOT_REACHED();
	}
}

/**
 * Get the length of a rail depot platform in a given direction.
 * @pre IsRailDepotTile(tile)
 * @param tile Tile to check
 * @param dir Direction to check
 * @param rtt Whether to check for road or tram type (only for road transport).
 * @return The length of the platform in tile length in the given direction.
 */
uint GetPlatformLength(TileIndex tile, DiagDirection dir, RoadTramType rtt)
{
	switch (GetPlatformType(tile)) {
		case PT_RAIL_STATION:
			return GetRailStationPlatformLength(tile, dir);
		case PT_RAIL_WAYPOINT:
			return 1;
		case PT_RAIL_DEPOT:
			return GetRailDepotPlatformLength(tile, dir);
		case PT_ROAD_DEPOT:
			return GetRoadDepotPlatformLength(tile, dir, rtt);
		default: NOT_REACHED();
	}
}

/**
 * Get a tile where a rail station platform begins or ends.
 * @pre IsRailStationTile(tile)
 * @param tile Tile to check
 * @param dir The diagonal direction to check
 * @return The last tile of the platform seen from tile with direction dir.
 */
TileIndex GetRailStationExtreme(TileIndex tile, DiagDirection dir)
{
	assert(IsRailStationTile(tile));
	assert(GetRailStationAxis(tile) == DiagDirToAxis(dir));
	TileIndexDiff delta = TileOffsByDiagDir(dir);

	TileIndex t = tile;
	uint len = 0;
	do {
		t -= delta;
		len++;
	} while (IsCompatibleTrainStationTile(t, tile));

	return t + delta;
}

/**
 * Get a tile where a depot platform begins or ends.
 * @pre IsBigDepotTile(tile)
 * @param tile Tile to check
 * @param dir The diagonal direction to check
 * @return The last tile of the platform seen from tile with direction dir.
 */
TileIndex GetRailDepotExtreme(TileIndex tile, DiagDirection dir)
{
	assert(IsBigDepotTile(tile));
	assert(GetRailDepotTrack(tile) == DiagDirToDiagTrack(dir));
	TileIndexDiff delta = TileOffsByDiagDir(dir);

	TileIndex t = tile;
	uint len = 0;
	do {
		t -= delta;
		len++;
	} while (IsCompatibleTrainDepotTile(t, tile));

	return t + delta;
}

/**
 * Get a tile where a platform begins or ends.
 * @param tile Tile to check
 * @param dir Direction to check
 * @return The last tile of the platform seen from tile with direction dir.
 */
TileIndex GetPlatformExtremeTile(TileIndex tile, DiagDirection dir)
{
	switch (GetPlatformType(tile)) {
		case PT_RAIL_STATION:
			return GetRailStationExtreme(tile, dir);
		case PT_RAIL_WAYPOINT:
			return tile;
		case PT_RAIL_DEPOT:
			return GetRailDepotExtreme(tile, dir);
		default: NOT_REACHED();
	}
}

/**
 * Get the tiles belonging to a platform.
 * @param tile Tile of a platform
 * @return the tile area of the platform
 */
TileArea GetPlatformTileArea(TileIndex tile)
{
	switch (GetPlatformType(tile)) {
		case PT_RAIL_STATION: {
			assert(IsRailStationTile(tile));
			DiagDirection dir = AxisToDiagDir(GetRailStationAxis(tile));
			return TileArea(GetRailStationExtreme(tile, dir), GetRailStationExtreme(tile, ReverseDiagDir(dir)));
		}
		case PT_RAIL_WAYPOINT:
			return TileArea(tile);
		case PT_RAIL_DEPOT: {
			assert(IsBigRailDepotTile(tile));
			DiagDirection dir = GetRailDepotDirection(tile);
			return TileArea(GetRailDepotExtreme(tile, dir), GetRailDepotExtreme(tile, ReverseDiagDir(dir)));
		}
		default: NOT_REACHED();
	}
}


/**
 * Check whether this tile is an extreme of a platform.
 * @param tile Tile to check
 * @return Whether the tile is the extreme of a platform.
 */
bool IsAnyStartPlatformTile(TileIndex tile)
{
	assert(IsBigRailDepotTile(tile));
	DiagDirection dir = GetRailDepotDirection(tile);
	return tile == GetPlatformExtremeTile(tile, dir) || tile == GetPlatformExtremeTile(tile, ReverseDiagDir(dir));
}
