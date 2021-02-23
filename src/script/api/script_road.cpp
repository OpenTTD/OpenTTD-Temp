/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_road.cpp Implementation of ScriptRoad. */

#include "../../stdafx.h"
#include "script_map.hpp"
#include "script_station.hpp"
#include "script_cargo.hpp"
#include "../../station_base.h"
#include "../../script/squirrel_helper_type.hpp"

#include "../../safeguards.h"

/* static */ ScriptRoad::RoadVehicleType ScriptRoad::GetRoadVehicleTypeForCargo(CargoID cargo_type)
{
	return ScriptCargo::HasCargoClass(cargo_type, ScriptCargo::CC_PASSENGERS) ? ROADVEHTYPE_BUS : ROADVEHTYPE_TRUCK;
}

/* static */ char *ScriptRoad::GetName(RoadType road_type)
{
	if (!IsRoadTypeAvailable(road_type)) return nullptr;

	return GetString(GetRoadTypeInfo((::RoadType)road_type)->strings.name);
}

/* static */ bool ScriptRoad::IsRoadTile(TileIndex tile)
{
	if (!::IsValidTile(tile)) return false;

	return (::IsTileType(tile, MP_ROAD) && ::GetRoadTileType(tile) != ROAD_TILE_DEPOT) ||
			IsDriveThroughRoadStationTile(tile);
}

/* static */ bool ScriptRoad::IsRoadDepotTile(TileIndex tile)
{
	if (!::IsValidTile(tile)) return false;
	if (!IsRoadTypeAvailable(GetCurrentRoadType())) return false;

	return ::IsTileType(tile, MP_ROAD) && ::GetRoadTileType(tile) == ROAD_TILE_DEPOT &&
			HasBit(::GetPresentRoadTypes(tile), (::RoadType)GetCurrentRoadType());
}

/* static */ bool ScriptRoad::IsRoadStationTile(TileIndex tile)
{
	if (!::IsValidTile(tile)) return false;
	if (!IsRoadTypeAvailable(GetCurrentRoadType())) return false;

	return ::IsRoadStopTile(tile) && HasBit(::GetPresentRoadTypes(tile), (::RoadType)GetCurrentRoadType());
}

/* static */ bool ScriptRoad::IsDriveThroughRoadStationTile(TileIndex tile)
{
	if (!::IsValidTile(tile)) return false;
	if (!IsRoadTypeAvailable(GetCurrentRoadType())) return false;

	return ::IsDriveThroughStopTile(tile) && HasBit(::GetPresentRoadTypes(tile), (::RoadType)GetCurrentRoadType());
}

/* static */ bool ScriptRoad::IsRoadTypeAvailable(RoadType road_type)
{
	return (::RoadType)road_type < ROADTYPE_END && ::HasRoadTypeAvail(ScriptObject::GetCompany(), (::RoadType)road_type);
}

/* static */ ScriptRoad::RoadType ScriptRoad::GetCurrentRoadType()
{
	return (RoadType)ScriptObject::GetRoadType();
}

/* static */ void ScriptRoad::SetCurrentRoadType(RoadType road_type)
{
	if (!IsRoadTypeAvailable(road_type)) return;

	ScriptObject::SetRoadType((::RoadType)road_type);
}

/* static */ bool ScriptRoad::RoadVehCanRunOnRoad(RoadType engine_road_type, RoadType road_road_type)
{
	return RoadVehHasPowerOnRoad(engine_road_type, road_road_type);
}

/* static */ bool ScriptRoad::RoadVehHasPowerOnRoad(RoadType engine_road_type, RoadType road_road_type)
{
	if (!IsRoadTypeAvailable(engine_road_type)) return false;
	if (!IsRoadTypeAvailable(road_road_type)) return false;

	return ::HasPowerOnRoad((::RoadType)engine_road_type, (::RoadType)road_road_type);
}

/* static */ bool ScriptRoad::HasRoadType(TileIndex tile, RoadType road_type)
{
	if (!ScriptMap::IsValidTile(tile)) return false;
	if (!IsRoadTypeAvailable(road_type)) return false;
	return ::GetAnyRoadBits(tile, ::GetRoadTramType((::RoadType)road_type), false) != ROAD_NONE;
}

/* static */ bool ScriptRoad::AreRoadTilesConnected(TileIndex t1, TileIndex t2)
{
	if (!::IsValidTile(t1)) return false;
	if (!::IsValidTile(t2)) return false;
	if (!IsRoadTypeAvailable(GetCurrentRoadType())) return false;

	/* Tiles not neighbouring */
	if ((abs((int)::TileX(t1) - (int)::TileX(t2)) + abs((int)::TileY(t1) - (int)::TileY(t2))) != 1) return false;

	RoadTramType rtt = ::GetRoadTramType(ScriptObject::GetRoadType());
	RoadBits r1 = ::GetAnyRoadBits(t1, rtt); // TODO
	RoadBits r2 = ::GetAnyRoadBits(t2, rtt); // TODO

	uint dir_1 = (::TileX(t1) == ::TileX(t2)) ? (::TileY(t1) < ::TileY(t2) ? 2 : 0) : (::TileX(t1) < ::TileX(t2) ? 1 : 3);
	uint dir_2 = 2 ^ dir_1;

	DisallowedRoadDirections drd2 = IsNormalRoadTile(t2) ? GetDisallowedRoadDirections(t2) : DRD_NONE;

	return HasBit(r1, dir_1) && HasBit(r2, dir_2) && drd2 != DRD_BOTH && drd2 != (dir_1 > dir_2 ? DRD_SOUTHBOUND : DRD_NORTHBOUND);
}

/* static */ bool ScriptRoad::ConvertRoadType(TileIndex start_tile, TileIndex end_tile, RoadType road_type)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, ::IsValidTile(start_tile));
	EnforcePrecondition(false, ::IsValidTile(end_tile));
	EnforcePrecondition(false, IsRoadTypeAvailable(road_type));

	return ScriptObject::DoCommand(start_tile, end_tile, (::RoadType)road_type, CMD_CONVERT_ROAD);
}

/* Helper functions for ScriptRoad::CanBuildConnectedRoadParts(). */

/**
 * Check whether the given existing bits the start and end part can be build.
 *  As the function assumes the bits being build on a slope that does not
 *  allow level foundations all of the existing parts will always be in
 *  a straight line. This also needs to hold for the start and end parts,
 *  otherwise it is for sure not valid. Finally a check will be done to
 *  determine whether the existing road parts match the to-be-build parts.
 *  As they can only be placed in one direction, just checking the start
 *  part with the first existing part is enough.
 * @param existing The existing road parts.
 * @param start The part that should be build first.
 * @param end The part that will be build second.
 * @return True if and only if the road bits can be build.
 */
static bool CheckAutoExpandedRoadBits(const Array *existing, int32 start, int32 end)
{
	return (start + end == 0) && (existing->size == 0 || existing->array[0] == start || existing->array[0] == end);
}

/**
 * Lookup function for building road parts when building on slopes is disabled.
 * @param slope The slope of the tile to examine.
 * @param existing The existing road parts.
 * @param start The part that should be build first.
 * @param end The part that will be build second.
 * @return 0 when the build parts do not connect, 1 when they do connect once
 *         they are build or 2 when building the first part automatically
 *         builds the second part.
 */
static int32 LookupWithoutBuildOnSlopes(::Slope slope, const Array *existing, int32 start, int32 end)
{
	switch (slope) {
		/* Flat slopes can always be build. */
		case SLOPE_FLAT:
			return 1;

		/* Only 4 of the slopes can be build upon. Testing the existing bits is
		 * necessary because these bits can be something else when the settings
		 * in the game have been changed.
		 */
		case SLOPE_NE: case SLOPE_SW:
			return (CheckAutoExpandedRoadBits(existing, start, end) && (start == 1 || end == 1)) ? (existing->size == 0 ? 2 : 1) : 0;
		case SLOPE_SE: case SLOPE_NW:
			return (CheckAutoExpandedRoadBits(existing, start, end) && (start != 1 && end != 1)) ? (existing->size == 0 ? 2 : 1) : 0;

		/* Any other tile cannot be built on. */
		default:
			return 0;
	}
}

/**
 * Rotate a neighbour bit a single time clockwise.
 * @param neighbour The neighbour.
 * @return The rotate neighbour data.
 */
static int32 RotateNeighbour(int32 neighbour)
{
	switch (neighbour) {
		case -2: return -1;
		case -1: return  2;
		case  1: return -2;
		case  2: return  1;
		default: NOT_REACHED();
	}
}

/**
 * Convert a neighbour to a road bit representation for easy internal use.
 * @param neighbour The neighbour.
 * @return The bits representing the direction.
 */
static RoadBits NeighbourToRoadBits(int32 neighbour)
{
	switch (neighbour) {
		case -2: return ROAD_NW;
		case -1: return ROAD_NE;
		case  2: return ROAD_SE;
		case  1: return ROAD_SW;
		default: NOT_REACHED();
	}
}

/**
 * Lookup function for building road parts when building on slopes is enabled.
 * @param slope The slope of the tile to examine.
 * @param existing The existing neighbours.
 * @param start The part that should be build first.
 * @param end The part that will be build second.
 * @return 0 when the build parts do not connect, 1 when they do connect once
 *         they are build or 2 when building the first part automatically
 *         builds the second part.
 */
static int32 LookupWithBuildOnSlopes(::Slope slope, Array *existing, int32 start, int32 end)
{
	/* Steep slopes behave the same as slopes with one corner raised. */
	if (IsSteepSlope(slope)) {
		slope = SlopeWithOneCornerRaised(GetHighestSlopeCorner(slope));
	}

	/* The slope is not steep. Furthermore lots of slopes are generally the
	 * same but are only rotated. So to reduce the amount of lookup work that
	 * needs to be done the data is made uniform. This means rotating the
	 * existing parts and updating the slope. */
	static const ::Slope base_slopes[] = {
		SLOPE_FLAT, SLOPE_W,   SLOPE_W,   SLOPE_SW,
		SLOPE_W,    SLOPE_EW,  SLOPE_SW,  SLOPE_WSE,
		SLOPE_W,    SLOPE_SW,  SLOPE_EW,  SLOPE_WSE,
		SLOPE_SW,   SLOPE_WSE, SLOPE_WSE};
	static const byte base_rotates[] = {0, 0, 1, 0, 2, 0, 1, 0, 3, 3, 2, 3, 2, 2, 1};

	if (slope >= (::Slope)lengthof(base_slopes)) {
		/* This slope is an invalid slope, so ignore it. */
		return -1;
	}
	byte base_rotate = base_rotates[slope];
	slope = base_slopes[slope];

	/* Some slopes don't need rotating, so return early when we know we do
	 * not need to rotate. */
	switch (slope) {
		case SLOPE_FLAT:
			/* Flat slopes can always be build. */
			return 1;

		case SLOPE_EW:
		case SLOPE_WSE:
			/* A slope similar to a SLOPE_EW or SLOPE_WSE will always cause
			 * foundations which makes them accessible from all sides. */
			return 1;

		case SLOPE_W:
		case SLOPE_SW:
			/* A slope for which we need perform some calculations. */
			break;

		default:
			/* An invalid slope. */
			return -1;
	}

	/* Now perform the actual rotation. */
	for (int j = 0; j < base_rotate; j++) {
		for (size_t i = 0; i < existing->size; i++) {
			existing->array[i] = RotateNeighbour(existing->array[i]);
		}
		start = RotateNeighbour(start);
		end   = RotateNeighbour(end);
	}

	/* Create roadbits out of the data for easier handling. */
	RoadBits start_roadbits    = NeighbourToRoadBits(start);
	RoadBits new_roadbits      = start_roadbits | NeighbourToRoadBits(end);
	RoadBits existing_roadbits = ROAD_NONE;
	for (size_t i = 0; i < existing->size; i++) {
		existing_roadbits |= NeighbourToRoadBits(existing->array[i]);
	}

	switch (slope) {
		case SLOPE_W:
			/* A slope similar to a SLOPE_W. */
			switch (new_roadbits) {
				case ROAD_N:
				case ROAD_E:
				case ROAD_S:
					/* Cannot build anything with a turn from the low side. */
					return 0;

				case ROAD_X:
				case ROAD_Y:
					/* A 'sloped' tile is going to be build. */
					if ((existing_roadbits | new_roadbits) != new_roadbits) {
						/* There is already a foundation on the tile, or at least
						 * another slope that is not compatible with the new one. */
						return 0;
					}
					/* If the start is in the low part, it is automatically
					 * building the second part too. */
					return ((start_roadbits & ROAD_E) && !(existing_roadbits & ROAD_W)) ? 2 : 1;

				default:
					/* Roadbits causing a foundation are going to be build.
					 * When the existing roadbits are slopes (the lower bits
					 * are used), this cannot be done. */
					if ((existing_roadbits | new_roadbits) == new_roadbits) return 1;
					return (existing_roadbits & ROAD_E) ? 0 : 1;
			}

		case SLOPE_SW:
			/* A slope similar to a SLOPE_SW. */
			switch (new_roadbits) {
				case ROAD_N:
				case ROAD_E:
					/* Cannot build anything with a turn from the low side. */
					return 0;

				case ROAD_X:
					/* A 'sloped' tile is going to be build. */
					if ((existing_roadbits | new_roadbits) != new_roadbits) {
						/* There is already a foundation on the tile, or at least
						 * another slope that is not compatible with the new one. */
						return 0;
					}
					/* If the start is in the low part, it is automatically
					 * building the second part too. */
					return ((start_roadbits & ROAD_NE) && !(existing_roadbits & ROAD_SW)) ? 2 : 1;

				default:
					/* Roadbits causing a foundation are going to be build.
					 * When the existing roadbits are slopes (the lower bits
					 * are used), this cannot be done. */
					return (existing_roadbits & ROAD_NE) ? 0 : 1;
			}

		default:
			NOT_REACHED();
	}
}

/**
 * Normalise all input data so we can easily handle it without needing
 * to call the API lots of times or create large if-elseif-elseif-else
 * constructs.
 * In this case it means that a TileXY(0, -1) becomes -2 and TileXY(0, 1)
 * becomes 2. TileXY(-1, 0) and TileXY(1, 0) stay respectively -1 and 1.
 * Any other value means that it is an invalid tile offset.
 * @param tile The tile to normalise.
 * @return True if and only if the tile offset is valid.
 */
static bool NormaliseTileOffset(int32 *tile)
{
		if (*tile == 1 || *tile == -1) return true;
		if (*tile == ::TileDiffXY(0, -1)) {
			*tile = -2;
			return true;
		}
		if (*tile == ::TileDiffXY(0, 1)) {
			*tile = 2;
			return true;
		}
		return false;
}

/* static */ int32 ScriptRoad::CanBuildConnectedRoadParts(ScriptTile::Slope slope_, Array *existing, TileIndex start_, TileIndex end_)
{
	::Slope slope = (::Slope)slope_;
	int32 start = start_;
	int32 end = end_;

	/* The start tile and end tile cannot be the same tile either. */
	if (start == end) return -1;

	for (size_t i = 0; i < existing->size; i++) {
		if (!NormaliseTileOffset(&existing->array[i])) return -1;
	}

	if (!NormaliseTileOffset(&start)) return -1;
	if (!NormaliseTileOffset(&end)) return -1;

	/* Without build on slopes the characteristics are vastly different, so use
	 * a different helper function (one that is much simpler). */
	return _settings_game.construction.build_on_slopes ? LookupWithBuildOnSlopes(slope, existing, start, end) : LookupWithoutBuildOnSlopes(slope, existing, start, end);
}

/* static */ int32 ScriptRoad::CanBuildConnectedRoadPartsHere(TileIndex tile, TileIndex start, TileIndex end)
{
	if (!::IsValidTile(tile) || !::IsValidTile(start) || !::IsValidTile(end)) return -1;
	if (::DistanceManhattan(tile, start) != 1 || ::DistanceManhattan(tile, end) != 1) return -1;

	/*                                           ROAD_NW              ROAD_SW             ROAD_SE             ROAD_NE */
	const TileIndexDiff neighbours[] = {::TileDiffXY(0, -1), ::TileDiffXY(1, 0), ::TileDiffXY(0, 1), ::TileDiffXY(-1, 0)};
	Array *existing = (Array*)alloca(sizeof(Array) + lengthof(neighbours) * sizeof(int32));
	existing->size = 0;

	::RoadBits rb = ::ROAD_NONE;
	if (::IsNormalRoadTile(tile)) {
		rb = ::GetAllRoadBits(tile);
	} else {
		rb = ::GetAnyRoadBits(tile, RTT_ROAD) | ::GetAnyRoadBits(tile, RTT_TRAM);
	}
	for (uint i = 0; i < lengthof(neighbours); i++) {
		if (HasBit(rb, i)) existing->array[existing->size++] = neighbours[i];
	}

	return ScriptRoad::CanBuildConnectedRoadParts(ScriptTile::GetSlope(tile), existing, start - tile, end - tile);
}

/**
 * Check whether one can reach (possibly by building) a road piece the center
 * of the neighbouring tile. This includes roads and (drive through) stations.
 * @param rt The road type we want to know reachability for
 * @param start_tile The tile to "enter" the neighbouring tile.
 * @param neighbour The direction to the neighbouring tile to "enter".
 * @return true if and only if the tile is reachable.
 */
static bool NeighbourHasReachableRoad(::RoadType rt, TileIndex start_tile, DiagDirection neighbour)
{
	TileIndex neighbour_tile = ::TileAddByDiagDir(start_tile, neighbour);
	if (!HasBit(::GetPresentRoadTypes(neighbour_tile), rt)) return false;

	switch (::GetTileType(neighbour_tile)) {
		case MP_ROAD:
			return (::GetRoadTileType(neighbour_tile) != ROAD_TILE_DEPOT);

		case MP_STATION:
			if (::IsDriveThroughStopTile(neighbour_tile)) {
				return (::DiagDirToAxis(neighbour) == ::DiagDirToAxis(::GetRoadStopDir(neighbour_tile)));
			}
			return false;

		default:
			return false;
	}
}

/* static */ int32 ScriptRoad::GetNeighbourRoadCount(TileIndex tile)
{
	if (!::IsValidTile(tile)) return false;
	if (!IsRoadTypeAvailable(GetCurrentRoadType())) return false;

	::RoadType rt = (::RoadType)GetCurrentRoadType();
	int32 neighbour = 0;

	if (TileX(tile) > 0 && NeighbourHasReachableRoad(rt, tile, DIAGDIR_NE)) neighbour++;
	if (NeighbourHasReachableRoad(rt, tile, DIAGDIR_SE)) neighbour++;
	if (NeighbourHasReachableRoad(rt, tile, DIAGDIR_SW)) neighbour++;
	if (TileY(tile) > 0 && NeighbourHasReachableRoad(rt, tile, DIAGDIR_NW)) neighbour++;

	return neighbour;
}

/* static */ TileIndex ScriptRoad::GetRoadDepotFrontTile(TileIndex depot)
{
	if (!IsRoadDepotTile(depot)) return INVALID_TILE;

	return depot + ::TileOffsByDiagDir(::GetRoadDepotDirection(depot));
}

/* static */ TileIndex ScriptRoad::GetRoadStationFrontTile(TileIndex station)
{
	if (!IsRoadStationTile(station)) return INVALID_TILE;

	return station + ::TileOffsByDiagDir(::GetRoadStopDir(station));
}

/* static */ TileIndex ScriptRoad::GetDriveThroughBackTile(TileIndex station)
{
	if (!IsDriveThroughRoadStationTile(station)) return INVALID_TILE;

	return station + ::TileOffsByDiagDir(::ReverseDiagDir(::GetRoadStopDir(station)));
}

/* static */ bool ScriptRoad::_BuildRoadInternal(TileIndex start, TileIndex end, bool one_way, bool full)
{
	EnforcePrecondition(false, start != end);
	EnforcePrecondition(false, ::IsValidTile(start));
	EnforcePrecondition(false, ::IsValidTile(end));
	EnforcePrecondition(false, ::TileX(start) == ::TileX(end) || ::TileY(start) == ::TileY(end));
	EnforcePrecondition(false, !one_way || RoadTypeIsRoad(ScriptObject::GetRoadType()));
	EnforcePrecondition(false, IsRoadTypeAvailable(GetCurrentRoadType()));

	return ScriptObject::DoCommand(start, end, (::TileY(start) != ::TileY(end) ? 4 : 0) | (((start < end) == !full) ? 1 : 2) | (ScriptObject::GetRoadType() << 3) | ((one_way ? 1 : 0) << 10) | 1 << 11, CMD_BUILD_LONG_ROAD);
}

/* static */ bool ScriptRoad::BuildRoad(TileIndex start, TileIndex end)
{
	return _BuildRoadInternal(start, end, false, false);
}

/* static */ bool ScriptRoad::BuildOneWayRoad(TileIndex start, TileIndex end)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	return _BuildRoadInternal(start, end, true, false);
}

/* static */ bool ScriptRoad::BuildRoadFull(TileIndex start, TileIndex end)
{
	return _BuildRoadInternal(start, end, false, true);
}

/* static */ bool ScriptRoad::BuildOneWayRoadFull(TileIndex start, TileIndex end)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	return _BuildRoadInternal(start, end, true, true);
}

/* static */ bool ScriptRoad::BuildRoadDepot(TileIndex tile, TileIndex front)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, tile != front);
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforcePrecondition(false, ::IsValidTile(front));
	EnforcePrecondition(false, ::TileX(tile) == ::TileX(front) || ::TileY(tile) == ::TileY(front));
	EnforcePrecondition(false, IsRoadTypeAvailable(GetCurrentRoadType()));

	uint entrance_dir = (::TileX(tile) == ::TileX(front)) ? (::TileY(tile) < ::TileY(front) ? 1 : 3) : (::TileX(tile) < ::TileX(front) ? 2 : 0);

	return ScriptObject::DoCommand(tile, entrance_dir | (ScriptObject::GetRoadType() << 2), 0, CMD_BUILD_ROAD_DEPOT);
}

/* static */ bool ScriptRoad::_BuildRoadStationInternal(TileIndex tile, TileIndex front, RoadVehicleType road_veh_type, bool drive_through, StationID station_id)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, tile != front);
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforcePrecondition(false, ::IsValidTile(front));
	EnforcePrecondition(false, ::TileX(tile) == ::TileX(front) || ::TileY(tile) == ::TileY(front));
	EnforcePrecondition(false, station_id == ScriptStation::STATION_NEW || station_id == ScriptStation::STATION_JOIN_ADJACENT || ScriptStation::IsValidStation(station_id));
	EnforcePrecondition(false, road_veh_type == ROADVEHTYPE_BUS || road_veh_type == ROADVEHTYPE_TRUCK);
	EnforcePrecondition(false, IsRoadTypeAvailable(GetCurrentRoadType()));

	uint entrance_dir;
	if (drive_through) {
		entrance_dir = ::TileY(tile) != ::TileY(front);
	} else {
		entrance_dir = (::TileX(tile) == ::TileX(front)) ? (::TileY(tile) < ::TileY(front) ? 1 : 3) : (::TileX(tile) < ::TileX(front) ? 2 : 0);
	}

	// Documentation for these values can be found at CmdBuildRoadStop, in station_cmd.cpp
	uint p1 = 1 | 1 << 8;
	p1 |= (ScriptStation::IsValidStation(station_id) ? station_id : INVALID_STATION) << 16;

	uint p2 = road_veh_type == ROADVEHTYPE_TRUCK;
	p2 |= (drive_through ? 1 : 0) << 1;
	p2 |= !(station_id == ScriptStation::STATION_JOIN_ADJACENT) << 2;
	p2 |= entrance_dir << 3;
	p2 |= ScriptObject::GetRoadType() << 22;
	return ScriptObject::DoCommand(tile, p1, p2, CMD_BUILD_ROAD_STOP);
}

/* static */ bool ScriptRoad::BuildRoadStation(TileIndex tile, TileIndex front, RoadVehicleType road_veh_type, StationID station_id)
{
	return _BuildRoadStationInternal(tile, front, road_veh_type, false, station_id);
}

/* static */ bool ScriptRoad::BuildDriveThroughRoadStation(TileIndex tile, TileIndex front, RoadVehicleType road_veh_type, StationID station_id)
{
	return _BuildRoadStationInternal(tile, front, road_veh_type, true, station_id);
}

/* static */ bool ScriptRoad::RemoveRoad(TileIndex start, TileIndex end)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, start != end);
	EnforcePrecondition(false, ::IsValidTile(start));
	EnforcePrecondition(false, ::IsValidTile(end));
	EnforcePrecondition(false, ::TileX(start) == ::TileX(end) || ::TileY(start) == ::TileY(end));
	EnforcePrecondition(false, IsRoadTypeAvailable(GetCurrentRoadType()));

	return ScriptObject::DoCommand(start, end, (::TileY(start) != ::TileY(end) ? 4 : 0) | (start < end ? 1 : 2) | (ScriptObject::GetRoadType() << 3), CMD_REMOVE_LONG_ROAD);
}

/* static */ bool ScriptRoad::RemoveRoadFull(TileIndex start, TileIndex end)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, start != end);
	EnforcePrecondition(false, ::IsValidTile(start));
	EnforcePrecondition(false, ::IsValidTile(end));
	EnforcePrecondition(false, ::TileX(start) == ::TileX(end) || ::TileY(start) == ::TileY(end));
	EnforcePrecondition(false, IsRoadTypeAvailable(GetCurrentRoadType()));

	return ScriptObject::DoCommand(start, end, (::TileY(start) != ::TileY(end) ? 4 : 0) | (start < end ? 2 : 1) | (ScriptObject::GetRoadType() << 3), CMD_REMOVE_LONG_ROAD);
}

/* static */ bool ScriptRoad::RemoveRoadDepot(TileIndex tile)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforcePrecondition(false, IsTileType(tile, MP_ROAD))
	EnforcePrecondition(false, GetRoadTileType(tile) == ROAD_TILE_DEPOT);

	return ScriptObject::DoCommand(tile, 0, 0, CMD_LANDSCAPE_CLEAR);
}

/* static */ bool ScriptRoad::RemoveRoadStation(TileIndex tile)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforcePrecondition(false, IsTileType(tile, MP_STATION));
	EnforcePrecondition(false, IsRoadStop(tile));

	return ScriptObject::DoCommand(tile, 1 | 1 << 8, GetRoadStopType(tile), CMD_REMOVE_ROAD_STOP);
}

/* static */ Money ScriptRoad::GetBuildCost(RoadType roadtype, BuildType build_type)
{
	if (!ScriptRoad::IsRoadTypeAvailable(roadtype)) return -1;

	switch (build_type) {
		case BT_ROAD:       return ::RoadBuildCost((::RoadType)roadtype);
		case BT_DEPOT:      return ::GetPrice(PR_BUILD_DEPOT_ROAD, 1, nullptr);
		case BT_BUS_STOP:   return ::GetPrice(PR_BUILD_STATION_BUS, 1, nullptr);
		case BT_TRUCK_STOP: return ::GetPrice(PR_BUILD_STATION_TRUCK, 1, nullptr);
		default: return -1;
	}
}

/* static */ ScriptRoad::RoadTramTypes ScriptRoad::GetRoadTramType(RoadType roadtype)
{
	return (RoadTramTypes)(1 << ::GetRoadTramType((::RoadType)roadtype));
}

/* static */ int32 ScriptRoad::GetMaxSpeed(RoadType road_type)
{
	if (!ScriptRoad::IsRoadTypeAvailable(road_type)) return 0;

	return GetRoadTypeInfo((::RoadType)road_type)->max_speed;
}

/* static */ uint16 ScriptRoad::GetMaintenanceCostFactor(RoadType roadtype)
{
	if (!ScriptRoad::IsRoadTypeAvailable(roadtype)) return 0;

	return GetRoadTypeInfo((::RoadType)roadtype)->maintenance_multiplier;
}
