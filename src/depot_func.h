/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file depot_func.h Functions related to depots. */

#ifndef DEPOT_FUNC_H
#define DEPOT_FUNC_H

#include "depot_type.h"
#include "vehicle_type.h"
#include "slope_func.h"
#include "command_type.h"

void ShowDepotWindow(TileIndex tile, VehicleType type);

void DeleteDepotHighlightOfVehicle(const Vehicle *v);

/**
 * Find out if the slope of the tile is suitable to build a depot of given direction
 * @param direction The direction in which the depot's exit points
 * @param tileh The slope of the tile in question
 * @return true if the construction is possible
 */
static inline bool CanBuildDepotByTileh(DiagDirection direction, Slope tileh)
{
	assert(tileh != SLOPE_FLAT);
	Slope entrance_corners = InclinedSlope(direction);
	/* For steep slopes both entrance corners must be raised (i.e. neither of them is the lowest corner),
	 * For non-steep slopes at least one corner must be raised. */
	return IsSteepSlope(tileh) ? (tileh & entrance_corners) == entrance_corners : (tileh & entrance_corners) != 0;
}

struct Depot;
CommandCost FindJoiningDepot(TileArea ta, VehicleType veh_type, DepotID &join_to, Depot *&depot, bool adjacent, DoCommandFlag flags);
void ShowSelectDepotIfNeeded(CommandContainer cmd, TileArea ta, VehicleType veh_type);

struct Window;
void CheckRedrawDepotHighlight(const Window *w, VehicleType veh_type);

#endif /* DEPOT_FUNC_H */
