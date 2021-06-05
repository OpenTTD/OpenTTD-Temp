/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file bridge_map.h Map accessor functions for bridges. */

#ifndef BRIDGE_MAP_H
#define BRIDGE_MAP_H

#include "core/multimap.hpp"
#include "rail_map.h"
#include "road_map.h"
#include "bridge.h"
#include "water_map.h"

extern MultiMap<uint, BridgeID> _bridge_index[2];

/**
 * Checks if this is a bridge, instead of a tunnel
 * @param t The tile to analyze
 * @pre IsTileType(t, MP_TUNNELBRIDGE)
 * @return true if the structure is a bridge one
 */
static inline bool IsBridge(TileIndex t)
{
	assert(IsTileType(t, MP_TUNNELBRIDGE));
	return HasBit(_m[t].m5, 7);
}

/**
 * checks if there is a bridge on this tile
 * @param t The tile to analyze
 * @return true if a bridge is present
 */
static inline bool IsBridgeTile(TileIndex t)
{
	return IsTileType(t, MP_TUNNELBRIDGE) && IsBridge(t);
}

/**
 * checks if a bridge is set above the ground of this tile
 * @param t The tile to analyze
 * @return true if a bridge is detected above
 */
static inline bool IsBridgeAbove(TileIndex t)
{
	return GB(_m[t].type, 2, 1) != 0;
}

/**
 * Get the index of bridge on a tile
 * @param t the tile
 * @pre IsBridgeTile(t)
 * @return The BridgeID of the bridge.
 */
static inline BridgeID GetBridgeIndex(TileIndex t)
{
	assert(IsBridgeTile(t));
	return _m[t].m2 | _me[t].m6 << 16;
}

/**
 * Determines the type of bridge on a tile
 * @param t The tile to analyze
 * @pre IsBridgeTile(t)
 * @return The bridge type
 */
static inline BridgeType GetBridgeType(TileIndex t)
{
	assert(IsBridgeTile(t));
	return Bridge::Get(GetBridgeIndex(t))->type;
}

std::pair<MultiMap<uint, BridgeID>::iterator, MultiMap<uint, BridgeID>::iterator> GetBridgeIterator(Axis axis, TileIndex tile);

Bridge *GetBridgeFromMiddle(TileIndex t);

/**
 * Get the axis of the bridge that goes over the tile. Not the axis or the ramp.
 * @param t The tile to analyze
 * @pre IsBridgeAbove(t)
 * @return the above mentioned axis
 */
static inline Axis GetBridgeAxis(TileIndex t)
{
	assert(IsBridgeAbove(t));
	Bridge *b = GetBridgeFromMiddle(t);
	return b->GetAxis();
}

TileIndex GetBridgeEnd(TileIndex t, DiagDirection dir);
TileIndex GetNorthernBridgeEnd(TileIndex t);
TileIndex GetSouthernBridgeEnd(TileIndex t);
TileIndex GetOtherBridgeEnd(TileIndex t);

int GetBridgeHeight(TileIndex tile);
/**
 * Get the height ('z') of a bridge in pixels.
 * @param tile the bridge ramp tile to get the bridge height from
 * @return the height of the bridge in pixels
 */
static inline int GetBridgePixelHeight(TileIndex tile)
{
	return GetBridgeHeight(tile) * TILE_HEIGHT;
}

/**
 * Removes bridges from the given, that is bridges along the X and Y axis.
 * @param t the tile to remove the bridge from
 */
static inline void ClearBridgeMiddle(TileIndex t)
{
	ClrBit(_m[t].type, 2);
}

/**
 * Set that there is a bridge over the given axis.
 * @param t the tile to add the bridge to
 */
static inline void SetBridgeMiddle(TileIndex t)
{
	SetBit(_m[t].type, 2);
}

/**
 * Generic part to make a bridge ramp for both roads and rails.
 * @param t          the tile to make a bridge ramp
 * @param o          the new owner of the bridge ramp
 * @param index      Index to the bridge.
 * @param d          the direction this ramp must be facing
 * @param tt         the transport type of the bridge
 * @note this function should not be called directly.
 */
static inline void MakeBridgeRamp(TileIndex t, Owner o, BridgeID index, DiagDirection d, TransportType tt)
{
	SetTileType(t, MP_TUNNELBRIDGE);
	SetTileOwner(t, o);
	SetDockingTile(t, false);
	_m[t].m2 = index;
	_m[t].m3 = 0;
	_m[t].m4 = INVALID_ROADTYPE;
	_m[t].m5 = 1 << 7 | tt << 2 | d;
	_me[t].m6 = index >> 16;
	_me[t].m7 = 0;
	_me[t].m8 = INVALID_ROADTYPE << 6;
}

/**
 * Make a bridge ramp for roads.
 * @param t          the tile to make a bridge ramp
 * @param o          the new owner of the bridge ramp
 * @param owner_road the new owner of the road on the bridge
 * @param owner_tram the new owner of the tram on the bridge
 * @param index      Index to the bridge.
 * @param d          the direction this ramp must be facing
 * @param road_rt    the road type of the bridge
 * @param tram_rt    the tram type of the bridge
 */
static inline void MakeRoadBridgeRamp(TileIndex t, Owner o, Owner owner_road, Owner owner_tram, BridgeID index, DiagDirection d, RoadType road_rt, RoadType tram_rt)
{
	MakeBridgeRamp(t, o, index, d, TRANSPORT_ROAD);
	SetRoadOwner(t, RTT_ROAD, owner_road);
	if (owner_tram != OWNER_TOWN) SetRoadOwner(t, RTT_TRAM, owner_tram);
	SetRoadTypes(t, road_rt, tram_rt);
}

/**
 * Make a bridge ramp for rails.
 * @param t          the tile to make a bridge ramp
 * @param o          the new owner of the bridge ramp
 * @param index      Index to the bridge.
 * @param d          the direction this ramp must be facing
 * @param rt         the rail type of the bridge
 */
static inline void MakeRailBridgeRamp(TileIndex t, Owner o, BridgeID index, DiagDirection d, RailType rt)
{
	MakeBridgeRamp(t, o, index, d, TRANSPORT_RAIL);
	SetRailType(t, rt);
}

/**
 * Make a bridge ramp for aqueducts.
 * @param t          the tile to make a bridge ramp
 * @param o          the new owner of the bridge ramp
 * @param d          the direction this ramp must be facing
 */
static inline void MakeAqueductBridgeRamp(TileIndex t, Owner o, BridgeID index, DiagDirection d)
{
	MakeBridgeRamp(t, o, index, d, TRANSPORT_WATER);
}

#endif /* BRIDGE_MAP_H */
