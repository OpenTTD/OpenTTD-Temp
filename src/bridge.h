/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file bridge.h Header file for bridges */

#ifndef BRIDGE_H
#define BRIDGE_H

#include "core/pool_type.hpp"
#include "gfx_type.h"
#include "bridge_type.h"
#include "tile_cmd.h"
#include "newgrf_commons.h"

struct Bridge;
typedef Pool<Bridge, BridgeID, 64, 0xFF0000> BridgePool;
extern BridgePool _bridge_pool;

/** Bridge data structure. */
struct Bridge : BridgePool::PoolItem<&_bridge_pool> {
	BridgeType type;    ///< Type of the bridge
	Date build_date;    ///< Date of construction
	Town *town;         ///< Town the bridge is built in
	uint16 random;      ///< Random bits
	TileIndex heads[2]; ///< Tile where the bridge ramp is located

	/** Make sure the bridge isn't zeroed. */
	Bridge() {}
	/** Make sure the right destructor is called as well! */
	~Bridge() {}

	Axis GetAxis() const {
		return DiagDirToAxis(DiagdirBetweenTiles(this->heads[0], this->heads[1]));
	}

	uint GetLength() const {
		TileIndexDiffC diff = TileIndexToTileIndexDiffC(this->heads[1], this->heads[0]);
		return diff.x != 0 ? (diff.x + 1) : (diff.y + 1);
	}
};

extern BridgeSpec _bridge_specs[NUM_BRIDGES];

Foundation GetBridgeFoundation(Slope tileh, Axis axis);
bool HasBridgeFlatRamp(Slope tileh, Axis axis);

void DrawBridgeMiddle(const TileInfo *ti);

CommandCost CheckBridgeAvailability(BridgeType bridge_type, uint bridge_len, DoCommandFlag flags = DC_NONE);
int CalcBridgeLenCostFactor(int x);

void ResetBridges();

#endif /* BRIDGE_H */
