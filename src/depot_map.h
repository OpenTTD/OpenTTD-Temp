/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file depot_map.h Map related accessors for depots. */

#ifndef DEPOT_MAP_H
#define DEPOT_MAP_H

#include "station_map.h"

static const byte DEPOT_TYPE = 0x02;

/**
 * Check if a tile is a depot and it is a depot of the given type.
 */
static inline bool IsDepotTypeTile(TileIndex tile, TransportType type)
{
	if (type == TRANSPORT_AIR) return IsHangarTile(tile);

	if (GB(_m[tile].m5, 6, 2) != DEPOT_TYPE) return false;

	switch (type) {
		default: NOT_REACHED();
		case TRANSPORT_RAIL:
			return IsTileType(tile, MP_RAILWAY);
		case TRANSPORT_ROAD:
			return IsTileType(tile, MP_ROAD);
		case TRANSPORT_WATER:
			return IsTileType(tile, MP_WATER);
	}
}

/**
 * Is the given tile a tile with a depot on it?
 * @param tile the tile to check
 * @return true if and only if there is a depot on the tile.
 */
static inline bool IsDepotTile(TileIndex tile)
{
	TileType type = GetTileType(tile);
	if (type == MP_STATION) return IsHangar(tile);
	if (GB(_m[tile].m5, 6, 2) != DEPOT_TYPE) return false;

	return type == MP_RAILWAY || type == MP_ROAD || type == MP_WATER;
}

/**
 * Get the index of which depot is attached to the tile.
 * @param t the tile
 * @pre IsDepotTile(t)
 * @return DepotID
 */
static inline DepotID GetDepotIndex(TileIndex t)
{
	assert(IsDepotTile(t));

	/* Hangars don't store depot id on m2. */
	extern DepotID GetHangarIndex(TileIndex t);
	if (IsTileType(t, MP_STATION)) return GetHangarIndex(t);

	return _m[t].m2;
}

/**
 * Get the type of vehicles that can use a depot
 * @param t The tile
 * @pre IsDepotTile(t)
 * @return the type of vehicles that can use the depot
 */
static inline VehicleType GetDepotVehicleType(TileIndex t)
{
	switch (GetTileType(t)) {
		default: NOT_REACHED();
		case MP_RAILWAY: return VEH_TRAIN;
		case MP_ROAD:    return VEH_ROAD;
		case MP_WATER:   return VEH_SHIP;
		case MP_STATION: return VEH_AIRCRAFT;
	}
}

#endif /* DEPOT_MAP_H */
