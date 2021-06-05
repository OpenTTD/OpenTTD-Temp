/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file bridge_map.cpp Map accessor functions for bridges. */

#include "stdafx.h"
#include "core/multimap.hpp"
#include "landscape.h"
#include "tunnelbridge_map.h"
#include <utility>

#include "safeguards.h"

MultiMap<uint, BridgeID> _bridge_index[2];

/**
 * Get the range of bridges in specific row or column
 * @param axis the direction to search in
 * @param tile the bridge tile to find the bridges for
 */
std::pair<MultiMap<uint, BridgeID>::iterator, MultiMap<uint, BridgeID>::iterator> GetBridgeIterator(Axis axis, TileIndex tile)
{
	uint pos = axis == AXIS_X ? TileY(tile) : TileX(tile);
	return _bridge_index[axis].equal_range(pos);
}

/**
 * Finds the bridge above a middle tile
 * @param tile the bridge tile to find the bridge for
 * @pre IsBridgeAbove(tile)
 */
Bridge *GetBridgeFromMiddle(TileIndex tile)
{
	assert(IsBridgeAbove(tile));

	uint x = TileX(tile);
	uint y = TileY(tile);

	auto range = GetBridgeIterator(AXIS_X, tile);
	for (auto it = range.first; it != range.second; it++) {
		Bridge *b = Bridge::Get(*it);

		uint x1 = TileX(b->heads[0]);
		uint x2 = TileX(b->heads[1]);

		if (x1 <= x && x <= x2) {
			return b;
		}
	}

	range = GetBridgeIterator(AXIS_Y, tile);
	for (auto it = range.first; it != range.second; it++) {
		Bridge *b = Bridge::Get(*it);

		uint y1 = TileY(b->heads[0]);
		uint y2 = TileY(b->heads[1]);

		if (y1 <= y && y <= y2) {
			return b;
		}
	}

	return nullptr;
}

/**
 * Finds the end of a bridge in the specified direction starting at a middle tile
 * @param tile the bridge tile to find the bridge ramp for
 * @param dir  the direction to search in
 */
TileIndex GetBridgeEnd(TileIndex tile, DiagDirection dir)
{
	TileIndexDiff delta = TileOffsByDiagDir(dir);

	dir = ReverseDiagDir(dir);
	do {
		tile += delta;
	} while (!IsBridgeTile(tile) || GetTunnelBridgeDirection(tile) != dir);

	return tile;
}


/**
 * Finds the northern end of a bridge starting at a middle tile
 * @param t the bridge tile to find the bridge ramp for
 */
TileIndex GetNorthernBridgeEnd(TileIndex t)
{
	return GetBridgeFromMiddle(t)->heads[0];
}


/**
 * Finds the southern end of a bridge starting at a middle tile
 * @param t the bridge tile to find the bridge ramp for
 */
TileIndex GetSouthernBridgeEnd(TileIndex t)
{
	return GetBridgeFromMiddle(t)->heads[1];
}


/**
 * Starting at one bridge end finds the other bridge end
 * @param tile the bridge ramp tile to find the other bridge ramp for
 */
TileIndex GetOtherBridgeEnd(TileIndex tile)
{
	assert(IsBridgeTile(tile));

	if (Bridge::IsValidID(GetBridgeIndex(tile))) {
		TileIndex *heads = Bridge::Get(GetBridgeIndex(tile))->heads;
		return tile == heads[0] ? heads[1] : heads[0];
	} else {
		return GetBridgeEnd(tile, GetTunnelBridgeDirection(tile));
	}
}

/**
 * Get the height ('z') of a bridge.
 * @param t the bridge ramp tile to get the bridge height from
 * @return the height of the bridge.
 */
int GetBridgeHeight(TileIndex t)
{
	int h;
	Slope tileh = GetTileSlope(t, &h);
	Foundation f = GetBridgeFoundation(tileh, DiagDirToAxis(GetTunnelBridgeDirection(t)));

	/* one height level extra for the ramp */
	return h + 1 + ApplyFoundationToSlope(f, &tileh);
}
