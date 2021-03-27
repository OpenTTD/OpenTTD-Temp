/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file depot_type.h Header files for depots (not hangars) */

#ifndef DEPOT_TYPE_H
#define DEPOT_TYPE_H

typedef uint16 DepotID; ///< Type for the unique identifier of depots.
static const DepotID INVALID_DEPOT = UINT16_MAX;        ///< Invalid index for depots.
static const DepotID NEW_DEPOT     = INVALID_DEPOT - 1; ///< Index for building a new depot.

struct Depot;

static const uint MAX_LENGTH_DEPOT_NAME_CHARS = 32; ///< The maximum length of a depot name in characters including '\0'

/** Type of reservation of big ship depots. */
enum DepotReservation {
	DEPOT_RESERVATION_EMPTY = 0,             ///< No vehicle servicing/stopped on depot tile/platform.
	DEPOT_RESERVATION_IN_USE = 1,            ///< At least a vehicle is in the depot, but the depot tile is not full of stopped vehicles.
	DEPOT_RESERVATION_FULL_STOPPED_VEH = 2,  ///< The depot tile/platform is full with stopped vehicles.
	DEPOT_RESERVATION_END
};

#endif /* DEPOT_TYPE_H */
