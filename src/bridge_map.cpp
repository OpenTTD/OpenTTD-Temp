/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file bridge_map.cpp Map accessor functions for bridges. */

#include "stdafx.h"
#include "landscape.h"
#include "tunnelbridge_map.h"

#include "safeguards.h"


/**
 * Finds the end of a bridge in the specified direction starting at a middle tile
 * @param tile the bridge tile to find the bridge ramp for
 * @param dir  the direction to search in
 */
static TileIndex GetBridgeEnd(TileIndex tile, DiagDirection dir)
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
 * @param t    the bridge tile to find the bridge ramp for
 * @param axis the axis to find the bridge ramp for
 * @returns the tile where the bridge head is, or INVALID_TILE if it doesn't exist
 */
TileIndex GetNorthernBridgeEndAtAxis(TileIndex t, Axis axis)
{
	Axis bridge_axis = GetBridgeAxis(t);
	if (bridge_axis != axis && bridge_axis != AXIS_END) return INVALID_TILE;
	return GetBridgeEnd(t, ReverseDiagDir(AxisToDiagDir(axis)));
}


/**
 * Finds the southern end of a bridge starting at a middle tile
 * @param t    the bridge tile to find the bridge ramp for
 * @param axis the axis to find the bridge ramp for
 * @returns the tile where the bridge head is, or INVALID_TILE if it doesn't exist
 */
TileIndex GetSouthernBridgeEndAtAxis(TileIndex t, Axis axis)
{
	Axis bridge_axis = GetBridgeAxis(t);
	if (bridge_axis != axis && bridge_axis != AXIS_END) return INVALID_TILE;
	return GetBridgeEnd(t, AxisToDiagDir(axis));
}


/**
 * Starting at one bridge end finds the other bridge end
 * @param tile the bridge ramp tile to find the other bridge ramp for
 */
TileIndex GetOtherBridgeEnd(TileIndex tile)
{
	assert(IsBridgeTile(tile));
	return GetBridgeEnd(tile, GetTunnelBridgeDirection(tile));
}


/**
 * Returns the height ('z') of the lowest bridge at a tile
 * @param tile the tile at which the bridge(s) are
 * @return the height of the lowest bridge.
 */
int GetLowestBridgeHeight(TileIndex tile) {
	TileIndex north_head_x = GetNorthernBridgeEndAtAxis(tile, AXIS_X);
	TileIndex north_head_y = GetNorthernBridgeEndAtAxis(tile, AXIS_Y);
	if (north_head_x == INVALID_TILE) return GetBridgeHeight(north_head_y);
	else if (north_head_y == INVALID_TILE) return GetBridgeHeight(north_head_x);
	else return std::min(GetBridgeHeight(north_head_x), GetBridgeHeight(north_head_y));
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
