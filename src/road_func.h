/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file road_func.h Functions related to roads. */

#ifndef ROAD_FUNC_H
#define ROAD_FUNC_H

#include "core/bitmath_func.hpp"
#include "road.h"
#include "economy_func.h"
#include "transparency.h"

/**
 * Whether the given roadtype is valid.
 * @param r the roadtype to check for validness
 * @return true if and only if valid
 */
static inline bool IsValidRoadBits(RoadBits r)
{
	return r < ROAD_END;
}

/**
 * Calculate the complement of a RoadBits value
 *
 * Simply flips all bits in the RoadBits value to get the complement
 * of the RoadBits.
 *
 * @param r The given RoadBits value
 * @return the complement
 */
static inline RoadBits ComplementRoadBits(RoadBits r)
{
	assert(IsValidRoadBits(r));
	return (RoadBits)(ROAD_ALL ^ r);
}

/**
 * Calculate the mirrored RoadBits
 *
 * Simply move the bits to their new position.
 *
 * @param r The given RoadBits value
 * @return the mirrored
 */
static inline RoadBits MirrorRoadBits(RoadBits r)
{
	assert(IsValidRoadBits(r));
	return (RoadBits)(GB(r, 0, 2) << 2 | GB(r, 2, 2));
}

/**
 * Calculate rotated RoadBits
 *
 * Move the Roadbits clockwise until they are in their final position.
 *
 * @param r The given RoadBits value
 * @param rot The given Rotation angle
 * @return the rotated
 */
static inline RoadBits RotateRoadBits(RoadBits r, DiagDirDiff rot)
{
	assert(IsValidRoadBits(r));
	for (; rot > (DiagDirDiff)0; rot--) {
		r = (RoadBits)(GB(r, 0, 1) << 3 | GB(r, 1, 3));
	}
	return r;
}

/**
 * Check if we've got a straight road
 *
 * @param r The given RoadBits
 * @return true if we've got a straight road
 */
static inline bool IsStraightRoad(RoadBits r)
{
	assert(IsValidRoadBits(r));
	return (r == ROAD_X || r == ROAD_Y);
}

/**
 * Create the road-part which belongs to the given DiagDirection
 *
 * This function returns a RoadBits value which belongs to
 * the given DiagDirection.
 *
 * @param d The DiagDirection
 * @return The result RoadBits which the selected road-part set
 */
static inline RoadBits DiagDirToRoadBits(DiagDirection d)
{
	assert(IsValidDiagDirection(d));
	return (RoadBits)(ROAD_NW << (3 ^ d));
}

/**
 * Create the road-part which belongs to the given Axis
 *
 * This function returns a RoadBits value which belongs to
 * the given Axis.
 *
 * @param a The Axis
 * @return The result RoadBits which the selected road-part set
 */
static inline RoadBits AxisToRoadBits(Axis a)
{
	assert(IsValidAxis(a));
	return a == AXIS_X ? ROAD_X : ROAD_Y;
}


/**
 * Calculates the maintenance cost of a number of road bits.
 * @param roadtype Road type to get the cost for.
 * @param num Number of road bits.
 * @param total_num Total number of road bits of all road/tram-types.
 * @return Total cost.
 */
static inline Money RoadMaintenanceCost(RoadType roadtype, uint32 num, uint32 total_num)
{
	assert(roadtype < ROADTYPE_END);
	return (_price[PR_INFRASTRUCTURE_ROAD] * GetRoadTypeInfo(roadtype)->maintenance_multiplier * num * (1 + IntSqrt(total_num))) >> 12;
}

/**
 * Test if a road type has catenary
 * @param roadtype Road type to test
 */
static inline bool HasRoadCatenary(RoadType roadtype)
{
	assert(roadtype < ROADTYPE_END);
	return HasBit(GetRoadTypeInfo(roadtype)->flags, ROTF_CATENARY);
}

/**
 * Test if we should draw road catenary
 * @param roadtype Road type to test
 */
static inline bool HasRoadCatenaryDrawn(RoadType roadtype)
{
	return HasRoadCatenary(roadtype) && !IsInvisibilitySet(TO_CATENARY);
}

bool HasRoadTypeAvail(CompanyID company, RoadType roadtype);
bool ValParamRoadType(RoadType roadtype);
RoadTypes GetCompanyRoadTypes(CompanyID company, bool introduces = true);
RoadTypes GetRoadTypes(bool introduces);
RoadTypes AddDateIntroducedRoadTypes(RoadTypes current, Date date);

void UpdateLevelCrossing(TileIndex tile, bool sound = true);
void UpdateCompanyRoadInfrastructure(RoadType rt, Owner o, int count);

struct TileInfo;
void DrawRoadOverlays(const TileInfo *ti, PaletteID pal, const RoadTypeInfo *road_rti, const RoadTypeInfo *tram_rit, uint road_offset, uint tram_offset);
void DrawRoadOverlays(const TileIndex tile, PaletteID pal, const RoadTypeInfo* road_rti, const RoadTypeInfo* tram_rit, uint road_offset, uint tram_offset);

#endif /* ROAD_FUNC_H */
