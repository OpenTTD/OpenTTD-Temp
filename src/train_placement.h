/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file train_placement.h Handling of trains in depot platforms. */

#ifndef TRAIN_PLA_H
#define TRAIN_PLA_H

#include "core/enum_type.hpp"
#include "train.h"


/* Flags of failure and success when placing a train. */
enum PlacementInfo {
	PI_BEGIN = 0,
	PI_FAILED_FREE_WAGGON = PI_BEGIN,         // Free waggon: not to be placed.
	PI_ERROR_BEGIN,
	PI_FAILED_PLATFORM_TYPE = PI_ERROR_BEGIN, // No compatible platforms with train type.
	PI_FAILED_LENGTH,                         // There are compatible platforms but not long enough.
	PI_FAILED_POWER,                          // No engine gets power in the platform.
	PI_WONT_LEAVE,
	PI_FAILED_RESERVED = PI_WONT_LEAVE,       // There are compatible platforms but reserved right now.
	PI_FAILED_SIGNALS,                        // There are compatible platforms not reserved, but signals don't allow placing it now.
	PI_FAILED_END,
	PI_SUCCESS = PI_FAILED_END,               // There is an appropriate platform.
	PI_END,
};

/* Store position of a train and lift it when necessary. */
struct TrainPlacement {
	bool placed;                   // True if train is placed in rails.
	TileIndex best_tile;           // Best tile for the train.
	Direction best_dir;            // Best direction for the train.
	PlacementInfo info;            // Info of possible problems in best platform.

	TrainPlacement() : placed(false),
			best_tile(INVALID_TILE),
			best_dir(INVALID_DIR),
			info(PI_FAILED_PLATFORM_TYPE) {}

	bool CheckPlacement(const Train *train, TileIndex tile, bool executing);
	void LookForPlaceInDepot(const Train *train, bool executing);
	bool CanFindAppropriatePlatform(const Train *train, bool executing);

	void LiftTrain(Train *train, DoCommandFlag flags);
	void PlaceTrain(Train *train, DoCommandFlag flags);
};

static inline bool CheckIfTrainNeedsPlacement(const Train *train)
{
	return IsBigRailDepot(train->tile) && (train->track & ~TRACK_BIT_DEPOT) == 0;
}

#endif /* TRAIN_PLA_H */
