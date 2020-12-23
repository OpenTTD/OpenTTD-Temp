/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file rail_cmd.cpp Handling of rail tiles. */

#include "stdafx.h"
#include "cmd_helper.h"
#include "viewport_func.h"
#include "command_func.h"
#include "depot_base.h"
#include "pathfinder/yapf/yapf_cache.h"
#include "newgrf_debug.h"
#include "newgrf_railtype.h"
#include "train.h"
#include "autoslope.h"
#include "water.h"
#include "tunnelbridge_map.h"
#include "vehicle_func.h"
#include "sound_func.h"
#include "tunnelbridge.h"
#include "elrail_func.h"
#include "town.h"
#include "pbs.h"
#include "company_base.h"
#include "core/backup_type.hpp"
#include "date_func.h"
#include "strings_func.h"
#include "company_gui.h"
#include "object_map.h"

#include "table/strings.h"
#include "table/railtypes.h"
#include "table/track_land.h"

#include "safeguards.h"

/** Helper type for lists/vectors of trains */
typedef std::vector<Train *> TrainList;

RailtypeInfo _railtypes[RAILTYPE_END];
std::vector<RailType> _sorted_railtypes;
RailTypes _railtypes_hidden_mask;

/** Enum holding the signal offset in the sprite sheet according to the side it is representing. */
enum SignalOffsets {
	SIGNAL_TO_SOUTHWEST,
	SIGNAL_TO_NORTHEAST,
	SIGNAL_TO_SOUTHEAST,
	SIGNAL_TO_NORTHWEST,
	SIGNAL_TO_EAST,
	SIGNAL_TO_WEST,
	SIGNAL_TO_SOUTH,
	SIGNAL_TO_NORTH,
};

/**
 * Reset all rail type information to its default values.
 */
void ResetRailTypes()
{
	static_assert(lengthof(_original_railtypes) <= lengthof(_railtypes));

	uint i = 0;
	for (; i < lengthof(_original_railtypes); i++) _railtypes[i] = _original_railtypes[i];

	static const RailtypeInfo empty_railtype = {
		{0,0,0,0,0,0,0,0,0,0,0,0},
		{0,0,0,0,0,0,0,0,{}},
		{0,0,0,0,0,0,0,0},
		{0,0,0,0,0,0},
		0, RAILTYPES_NONE, RAILTYPES_NONE, 0, 0, 0, RTFB_NONE, 0, 0, 0, 0, 0,
		RailTypeLabelList(), 0, 0, RAILTYPES_NONE, RAILTYPES_NONE, 0,
		{}, {} };
	for (; i < lengthof(_railtypes);          i++) _railtypes[i] = empty_railtype;

	_railtypes_hidden_mask = RAILTYPES_NONE;
}

void ResolveRailTypeGUISprites(RailtypeInfo *rti)
{
	SpriteID cursors_base = GetCustomRailSprite(rti, INVALID_TILE, RTSG_CURSORS);
	if (cursors_base != 0) {
		rti->gui_sprites.build_ns_rail = cursors_base +  0;
		rti->gui_sprites.build_x_rail  = cursors_base +  1;
		rti->gui_sprites.build_ew_rail = cursors_base +  2;
		rti->gui_sprites.build_y_rail  = cursors_base +  3;
		rti->gui_sprites.auto_rail     = cursors_base +  4;
		rti->gui_sprites.build_depot   = cursors_base +  5;
		rti->gui_sprites.build_tunnel  = cursors_base +  6;
		rti->gui_sprites.convert_rail  = cursors_base +  7;
		rti->cursor.rail_ns   = cursors_base +  8;
		rti->cursor.rail_swne = cursors_base +  9;
		rti->cursor.rail_ew   = cursors_base + 10;
		rti->cursor.rail_nwse = cursors_base + 11;
		rti->cursor.autorail  = cursors_base + 12;
		rti->cursor.depot     = cursors_base + 13;
		rti->cursor.tunnel    = cursors_base + 14;
		rti->cursor.convert   = cursors_base + 15;
	}

	/* Array of default GUI signal sprite numbers. */
	const SpriteID _signal_lookup[2][SIGTYPE_END] = {
		{SPR_IMG_SIGNAL_ELECTRIC_NORM,  SPR_IMG_SIGNAL_ELECTRIC_ENTRY, SPR_IMG_SIGNAL_ELECTRIC_EXIT,
		 SPR_IMG_SIGNAL_ELECTRIC_COMBO, SPR_IMG_SIGNAL_ELECTRIC_PBS,   SPR_IMG_SIGNAL_ELECTRIC_PBS_OWAY},

		{SPR_IMG_SIGNAL_SEMAPHORE_NORM,  SPR_IMG_SIGNAL_SEMAPHORE_ENTRY, SPR_IMG_SIGNAL_SEMAPHORE_EXIT,
		 SPR_IMG_SIGNAL_SEMAPHORE_COMBO, SPR_IMG_SIGNAL_SEMAPHORE_PBS,   SPR_IMG_SIGNAL_SEMAPHORE_PBS_OWAY},
	};

	for (SignalType type = SIGTYPE_NORMAL; type < SIGTYPE_END; type = (SignalType)(type + 1)) {
		for (SignalVariant var = SIG_ELECTRIC; var <= SIG_SEMAPHORE; var = (SignalVariant)(var + 1)) {
			SpriteID red   = GetCustomSignalSprite(rti, INVALID_TILE, type, var, SIGNAL_STATE_RED, true);
			SpriteID green = GetCustomSignalSprite(rti, INVALID_TILE, type, var, SIGNAL_STATE_GREEN, true);
			rti->gui_sprites.signals[type][var][0] = (red != 0)   ? red + SIGNAL_TO_SOUTH   : _signal_lookup[var][type];
			rti->gui_sprites.signals[type][var][1] = (green != 0) ? green + SIGNAL_TO_SOUTH : _signal_lookup[var][type] + 1;
		}
	}
}

/**
 * Compare railtypes based on their sorting order.
 * @param first  The railtype to compare to.
 * @param second The railtype to compare.
 * @return True iff the first should be sorted before the second.
 */
static bool CompareRailTypes(const RailType &first, const RailType &second)
{
	return GetRailTypeInfo(first)->sorting_order < GetRailTypeInfo(second)->sorting_order;
}

/**
 * Resolve sprites of custom rail types
 */
void InitRailTypes()
{
	for (RailType rt = RAILTYPE_BEGIN; rt != RAILTYPE_END; rt++) {
		RailtypeInfo *rti = &_railtypes[rt];
		ResolveRailTypeGUISprites(rti);
		if (HasBit(rti->flags, RTF_HIDDEN)) SetBit(_railtypes_hidden_mask, rt);
	}

	_sorted_railtypes.clear();
	for (RailType rt = RAILTYPE_BEGIN; rt != RAILTYPE_END; rt++) {
		if (_railtypes[rt].label != 0 && !HasBit(_railtypes_hidden_mask, rt)) {
			_sorted_railtypes.push_back(rt);
		}
	}
	std::sort(_sorted_railtypes.begin(), _sorted_railtypes.end(), CompareRailTypes);
}

/**
 * Allocate a new rail type label
 */
RailType AllocateRailType(RailTypeLabel label)
{
	for (RailType rt = RAILTYPE_BEGIN; rt != RAILTYPE_END; rt++) {
		RailtypeInfo *rti = &_railtypes[rt];

		if (rti->label == 0) {
			/* Set up new rail type */
			*rti = _original_railtypes[RAILTYPE_RAIL];
			rti->label = label;
			rti->alternate_labels.clear();

			/* Make us compatible with ourself. */
			rti->powered_railtypes    = (RailTypes)(1LL << rt);
			rti->compatible_railtypes = (RailTypes)(1LL << rt);

			/* We also introduce ourself. */
			rti->introduces_railtypes = (RailTypes)(1LL << rt);

			/* Default sort order; order of allocation, but with some
			 * offsets so it's easier for NewGRF to pick a spot without
			 * changing the order of other (original) rail types.
			 * The << is so you can place other railtypes in between the
			 * other railtypes, the 7 is to be able to place something
			 * before the first (default) rail type. */
			rti->sorting_order = rt << 4 | 7;
			return rt;
		}
	}

	return INVALID_RAILTYPE;
}

static const byte _track_sloped_sprites[14] = {
	14, 15, 22, 13,
	 0, 21, 17, 12,
	23,  0, 18, 20,
	19, 16
};


/*         4
 *     ---------
 *    |\       /|
 *    | \    1/ |
 *    |  \   /  |
 *    |   \ /   |
 *  16|    \    |32
 *    |   / \2  |
 *    |  /   \  |
 *    | /     \ |
 *    |/       \|
 *     ---------
 *         8
 */



/* MAP2 byte:    abcd???? => Signal On? Same coding as map3lo
 * MAP3LO byte:  abcd???? => Signal Exists?
 *               a and b are for diagonals, upper and left,
 *               one for each direction. (ie a == NE->SW, b ==
 *               SW->NE, or v.v., I don't know. b and c are
 *               similar for lower and right.
 * MAP2 byte:    ????abcd => Type of ground.
 * MAP3LO byte:  ????abcd => Type of rail.
 * MAP5:         00abcdef => rail
 *               01abcdef => rail w/ signals
 *               10uuuuuu => unused
 *               11uuuudd => rail depot
 */

/**
 * Tests if a vehicle interacts with the specified track.
 * All track bits interact except parallel #TRACK_BIT_HORZ or #TRACK_BIT_VERT.
 *
 * @param tile The tile.
 * @param track The track.
 * @return Succeeded command (no train found), or a failed command (a train was found).
 */
static CommandCost EnsureNoTrainOnTrack(TileIndex tile, Track track)
{
	TrackBits rail_bits = TrackToTrackBits(track);
	return EnsureNoTrainOnTrackBits(tile, rail_bits);
}

/**
 * Check that the new track bits may be built.
 * @param tile %Tile to build on.
 * @param to_build New track bits.
 * @param flags    Flags of the operation.
 * @return Succeeded or failed command.
 */
static CommandCost CheckTrackCombination(TileIndex tile, TrackBits to_build, uint flags)
{
	if (!IsPlainRail(tile)) return_cmd_error(STR_ERROR_IMPOSSIBLE_TRACK_COMBINATION);

	/* So, we have a tile with tracks on it (and possibly signals). Let's see
	 * what tracks first */
	TrackBits current = GetTrackBits(tile); // The current track layout.
	TrackBits future = current | to_build;  // The track layout we want to build.

	/* Are we really building something new? */
	if (current == future) {
		/* Nothing new is being built */
		return_cmd_error(STR_ERROR_ALREADY_BUILT);
	}

	/* Normally, we may overlap and any combination is valid */
	return CommandCost();
}


/** Valid TrackBits on a specific (non-steep)-slope without foundation */
static const TrackBits _valid_tracks_without_foundation[15] = {
	TRACK_BIT_ALL,
	TRACK_BIT_RIGHT,
	TRACK_BIT_UPPER,
	TRACK_BIT_X,

	TRACK_BIT_LEFT,
	TRACK_BIT_NONE,
	TRACK_BIT_Y,
	TRACK_BIT_LOWER,

	TRACK_BIT_LOWER,
	TRACK_BIT_Y,
	TRACK_BIT_NONE,
	TRACK_BIT_LEFT,

	TRACK_BIT_X,
	TRACK_BIT_UPPER,
	TRACK_BIT_RIGHT,
};

/** Valid TrackBits on a specific (non-steep)-slope with leveled foundation */
static const TrackBits _valid_tracks_on_leveled_foundation[15] = {
	TRACK_BIT_NONE,
	TRACK_BIT_LEFT,
	TRACK_BIT_LOWER,
	TRACK_BIT_Y | TRACK_BIT_LOWER | TRACK_BIT_LEFT,

	TRACK_BIT_RIGHT,
	TRACK_BIT_ALL,
	TRACK_BIT_X | TRACK_BIT_LOWER | TRACK_BIT_RIGHT,
	TRACK_BIT_ALL,

	TRACK_BIT_UPPER,
	TRACK_BIT_X | TRACK_BIT_UPPER | TRACK_BIT_LEFT,
	TRACK_BIT_ALL,
	TRACK_BIT_ALL,

	TRACK_BIT_Y | TRACK_BIT_UPPER | TRACK_BIT_RIGHT,
	TRACK_BIT_ALL,
	TRACK_BIT_ALL
};

/**
 * Checks if a track combination is valid on a specific slope and returns the needed foundation.
 *
 * @param tileh Tile slope.
 * @param bits  Trackbits.
 * @return Needed foundation or FOUNDATION_INVALID if track/slope combination is not allowed.
 */
Foundation GetRailFoundation(Slope tileh, TrackBits bits)
{
	if (bits == TRACK_BIT_NONE) return FOUNDATION_NONE;

	if (IsSteepSlope(tileh)) {
		/* Test for inclined foundations */
		if (bits == TRACK_BIT_X) return FOUNDATION_INCLINED_X;
		if (bits == TRACK_BIT_Y) return FOUNDATION_INCLINED_Y;

		/* Get higher track */
		Corner highest_corner = GetHighestSlopeCorner(tileh);
		TrackBits higher_track = CornerToTrackBits(highest_corner);

		/* Only higher track? */
		if (bits == higher_track) return HalftileFoundation(highest_corner);

		/* Overlap with higher track? */
		if (TracksOverlap(bits | higher_track)) return FOUNDATION_INVALID;

		/* either lower track or both higher and lower track */
		return ((bits & higher_track) != 0 ? FOUNDATION_STEEP_BOTH : FOUNDATION_STEEP_LOWER);
	} else {
		if ((~_valid_tracks_without_foundation[tileh] & bits) == 0) return FOUNDATION_NONE;

		bool valid_on_leveled = ((~_valid_tracks_on_leveled_foundation[tileh] & bits) == 0);

		Corner track_corner;
		switch (bits) {
			case TRACK_BIT_LEFT:  track_corner = CORNER_W; break;
			case TRACK_BIT_LOWER: track_corner = CORNER_S; break;
			case TRACK_BIT_RIGHT: track_corner = CORNER_E; break;
			case TRACK_BIT_UPPER: track_corner = CORNER_N; break;

			case TRACK_BIT_HORZ:
				if (tileh == SLOPE_N) return HalftileFoundation(CORNER_N);
				if (tileh == SLOPE_S) return HalftileFoundation(CORNER_S);
				return (valid_on_leveled ? FOUNDATION_LEVELED : FOUNDATION_INVALID);

			case TRACK_BIT_VERT:
				if (tileh == SLOPE_W) return HalftileFoundation(CORNER_W);
				if (tileh == SLOPE_E) return HalftileFoundation(CORNER_E);
				return (valid_on_leveled ? FOUNDATION_LEVELED : FOUNDATION_INVALID);

			case TRACK_BIT_X:
				if (IsSlopeWithOneCornerRaised(tileh)) return FOUNDATION_INCLINED_X;
				return (valid_on_leveled ? FOUNDATION_LEVELED : FOUNDATION_INVALID);

			case TRACK_BIT_Y:
				if (IsSlopeWithOneCornerRaised(tileh)) return FOUNDATION_INCLINED_Y;
				return (valid_on_leveled ? FOUNDATION_LEVELED : FOUNDATION_INVALID);

			default:
				return (valid_on_leveled ? FOUNDATION_LEVELED : FOUNDATION_INVALID);
		}
		/* Single diagonal track */

		/* Track must be at least valid on leveled foundation */
		if (!valid_on_leveled) return FOUNDATION_INVALID;

		/* If slope has three raised corners, build leveled foundation */
		if (IsSlopeWithThreeCornersRaised(tileh)) return FOUNDATION_LEVELED;

		/* If neighboured corners of track_corner are lowered, build halftile foundation */
		if ((tileh & SlopeWithThreeCornersRaised(OppositeCorner(track_corner))) == SlopeWithOneCornerRaised(track_corner)) return HalftileFoundation(track_corner);

		/* else special anti-zig-zag foundation */
		return SpecialRailFoundation(track_corner);
	}
}


/**
 * Tests if a track can be build on a tile.
 *
 * @param tileh Tile slope.
 * @param rail_bits Tracks to build.
 * @param existing Tracks already built.
 * @param tile Tile (used for water test)
 * @return Error message or cost for foundation building.
 */
static CommandCost CheckRailSlope(Slope tileh, TrackBits rail_bits, TrackBits existing, TileIndex tile)
{
	/* don't allow building on the lower side of a coast */
	if (GetFloodingBehaviour(tile) != FLOOD_NONE) {
		if (!IsSteepSlope(tileh) && ((~_valid_tracks_on_leveled_foundation[tileh] & (rail_bits | existing)) != 0)) return_cmd_error(STR_ERROR_CAN_T_BUILD_ON_WATER);
	}

	Foundation f_new = GetRailFoundation(tileh, rail_bits | existing);

	/* check track/slope combination */
	if ((f_new == FOUNDATION_INVALID) ||
			((f_new != FOUNDATION_NONE) && (!_settings_game.construction.build_on_slopes))) {
		return_cmd_error(STR_ERROR_LAND_SLOPED_IN_WRONG_DIRECTION);
	}

	Foundation f_old = GetRailFoundation(tileh, existing);
	return CommandCost(EXPENSES_CONSTRUCTION, f_new != f_old ? _price[PR_BUILD_FOUNDATION] : (Money)0);
}

/* Validate functions for rail building */
static inline bool ValParamTrackOrientation(Track track)
{
	return IsValidTrack(track);
}

/**
 * Build a single piece of rail
 * @param tile tile  to build on
 * @param flags operation to perform
 * @param p1 railtype of being built piece (normal, mono, maglev)
 * @param p2 various bitstuffed elements
 *           - (bit  0- 2) - track-orientation, valid values: 0-5 (@see Track)
 *           - (bit  3)    - 0 = error on signal in the way, 1 = auto remove signals when in the way
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdBuildSingleRail(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const std::string &text)
{
	RailType railtype = Extract<RailType, 0, 6>(p1);
	Track track = Extract<Track, 0, 3>(p2);
	bool auto_remove_signals = HasBit(p2, 3);
	CommandCost cost(EXPENSES_CONSTRUCTION);

	if (!ValParamRailtype(railtype) || !ValParamTrackOrientation(track)) return CMD_ERROR;

	Slope tileh = GetTileSlope(tile);
	TrackBits trackbit = TrackToTrackBits(track);

	switch (GetTileType(tile)) {
		case MP_RAILWAY: {
			CommandCost ret = CheckTileOwnership(tile);
			if (ret.Failed()) return ret;

			if (!IsPlainRail(tile)) return DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR); // just get appropriate error message

			if (!IsCompatibleRail(GetRailType(tile), railtype)) return_cmd_error(STR_ERROR_IMPOSSIBLE_TRACK_COMBINATION);

			ret = CheckTrackCombination(tile, trackbit, flags);
			if (ret.Succeeded()) ret = EnsureNoTrainOnTrack(tile, track);
			if (ret.Failed()) return ret;

			ret = CheckRailSlope(tileh, trackbit, GetTrackBits(tile), tile);
			if (ret.Failed()) return ret;
			cost.AddCost(ret);

			if (HasSignals(tile) && TracksOverlap(GetTrackBits(tile) | TrackToTrackBits(track))) {
				/* If adding the new track causes any overlap, all signals must be removed first */
				if (!auto_remove_signals) return_cmd_error(STR_ERROR_MUST_REMOVE_SIGNALS_FIRST);

				for (Track track_it = TRACK_BEGIN; track_it < TRACK_END; track_it++) {
					if (HasTrack(tile, track_it) && HasSignalOnTrack(tile, track_it)) {
						CommandCost ret_remove_signals = DoCommand(tile, track_it, 0, flags, CMD_REMOVE_SIGNALS);
						if (ret_remove_signals.Failed()) return ret_remove_signals;
						cost.AddCost(ret_remove_signals);
					}
				}
			}

			/* If the rail types don't match, try to convert only if engines of
			 * the new rail type are not powered on the present rail type and engines of
			 * the present rail type are powered on the new rail type. */
			if (GetRailType(tile) != railtype && !HasPowerOnRail(railtype, GetRailType(tile))) {
				if (HasPowerOnRail(GetRailType(tile), railtype)) {
					ret = DoCommand(tile, tile, railtype, flags, CMD_CONVERT_RAIL);
					if (ret.Failed()) return ret;
					cost.AddCost(ret);
				} else {
					return CMD_ERROR;
				}
			}

			if (flags & DC_EXEC) {
				SetRailGroundType(tile, RAIL_GROUND_BARREN);
				TrackBits bits = GetTrackBits(tile);
				SetTrackBits(tile, bits | trackbit);
				/* Subtract old infrastructure count. */
				uint pieces = CountBits(bits);
				if (TracksOverlap(bits)) pieces *= pieces;
				Company::Get(GetTileOwner(tile))->infrastructure.rail[GetRailType(tile)] -= pieces;
				/* Add new infrastructure count. */
				pieces = CountBits(bits | trackbit);
				if (TracksOverlap(bits | trackbit)) pieces *= pieces;
				Company::Get(GetTileOwner(tile))->infrastructure.rail[GetRailType(tile)] += pieces;
				DirtyCompanyInfrastructureWindows(GetTileOwner(tile));
			}
			break;
		}

		case MP_ROAD: {
			/* Level crossings may only be built on these slopes */
			if (!HasBit(VALID_LEVEL_CROSSING_SLOPES, tileh)) return_cmd_error(STR_ERROR_LAND_SLOPED_IN_WRONG_DIRECTION);

			CommandCost ret = EnsureNoVehicleOnGround(tile);
			if (ret.Failed()) return ret;

			if (IsNormalRoad(tile)) {
				if (HasRoadWorks(tile)) return_cmd_error(STR_ERROR_ROAD_WORKS_IN_PROGRESS);

				if (GetDisallowedRoadDirections(tile) != DRD_NONE) return_cmd_error(STR_ERROR_CROSSING_ON_ONEWAY_ROAD);

				if (RailNoLevelCrossings(railtype)) return_cmd_error(STR_ERROR_CROSSING_DISALLOWED_RAIL);

				RoadType roadtype_road = GetRoadTypeRoad(tile);
				RoadType roadtype_tram = GetRoadTypeTram(tile);

				if (roadtype_road != INVALID_ROADTYPE && RoadNoLevelCrossing(roadtype_road)) return_cmd_error(STR_ERROR_CROSSING_DISALLOWED_ROAD);
				if (roadtype_tram != INVALID_ROADTYPE && RoadNoLevelCrossing(roadtype_tram)) return_cmd_error(STR_ERROR_CROSSING_DISALLOWED_ROAD);

				RoadBits road = GetRoadBits(tile, RTT_ROAD);
				RoadBits tram = GetRoadBits(tile, RTT_TRAM);
				if ((track == TRACK_X && ((road | tram) & ROAD_X) == 0) ||
						(track == TRACK_Y && ((road | tram) & ROAD_Y) == 0)) {
					Owner road_owner = GetRoadOwner(tile, RTT_ROAD);
					Owner tram_owner = GetRoadOwner(tile, RTT_TRAM);
					/* Disallow breaking end-of-line of someone else
					 * so trams can still reverse on this tile. */
					if (Company::IsValidID(tram_owner) && HasExactlyOneBit(tram)) {
						CommandCost ret = CheckOwnership(tram_owner);
						if (ret.Failed()) return ret;
					}

					uint num_new_road_pieces = (road != ROAD_NONE) ? 2 - CountBits(road) : 0;
					if (num_new_road_pieces > 0) {
						cost.AddCost(num_new_road_pieces * RoadBuildCost(roadtype_road));
					}

					uint num_new_tram_pieces = (tram != ROAD_NONE) ? 2 - CountBits(tram) : 0;
					if (num_new_tram_pieces > 0) {
						cost.AddCost(num_new_tram_pieces * RoadBuildCost(roadtype_tram));
					}

					if (flags & DC_EXEC) {
						MakeRoadCrossing(tile, road_owner, tram_owner, _current_company, (track == TRACK_X ? AXIS_Y : AXIS_X), railtype, roadtype_road, roadtype_tram, GetTownIndex(tile));
						UpdateLevelCrossing(tile, false);
						Company::Get(_current_company)->infrastructure.rail[railtype] += LEVELCROSSING_TRACKBIT_FACTOR;
						DirtyCompanyInfrastructureWindows(_current_company);
						if (num_new_road_pieces > 0 && Company::IsValidID(road_owner)) {
							Company::Get(road_owner)->infrastructure.road[roadtype_road] += num_new_road_pieces;
							DirtyCompanyInfrastructureWindows(road_owner);
						}
						if (num_new_tram_pieces > 0 && Company::IsValidID(tram_owner)) {
							Company::Get(tram_owner)->infrastructure.road[roadtype_tram] += num_new_tram_pieces;
							DirtyCompanyInfrastructureWindows(tram_owner);
						}
					}
					break;
				}
			}

			if (IsLevelCrossing(tile) && GetCrossingRailBits(tile) == trackbit) {
				return_cmd_error(STR_ERROR_ALREADY_BUILT);
			}
			FALLTHROUGH;
		}

		default: {
			/* Will there be flat water on the lower halftile? */
			bool water_ground = IsTileType(tile, MP_WATER) && IsSlopeWithOneCornerRaised(tileh);

			CommandCost ret = CheckRailSlope(tileh, trackbit, TRACK_BIT_NONE, tile);
			if (ret.Failed()) return ret;
			cost.AddCost(ret);

			ret = DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
			if (ret.Failed()) return ret;
			cost.AddCost(ret);

			if (water_ground) {
				cost.AddCost(-_price[PR_CLEAR_WATER]);
				cost.AddCost(_price[PR_CLEAR_ROUGH]);
			}

			if (flags & DC_EXEC) {
				MakeRailNormal(tile, _current_company, trackbit, railtype);
				if (water_ground) {
					SetRailGroundType(tile, RAIL_GROUND_WATER);
					if (IsPossibleDockingTile(tile)) CheckForDockingTile(tile);
				}
				Company::Get(_current_company)->infrastructure.rail[railtype]++;
				DirtyCompanyInfrastructureWindows(_current_company);
			}
			break;
		}
	}

	if (flags & DC_EXEC) {
		MarkTileDirtyByTile(tile);
		AddTrackToSignalBuffer(tile, track, _current_company);
		YapfNotifyTrackLayoutChange(tile, track);
	}

	cost.AddCost(RailBuildCost(railtype));
	return cost;
}

/**
 * Remove a single piece of track
 * @param tile tile to remove track from
 * @param flags operation to perform
 * @param p1 unused
 * @param p2 rail orientation
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdRemoveSingleRail(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const std::string &text)
{
	Track track = Extract<Track, 0, 3>(p2);
	CommandCost cost(EXPENSES_CONSTRUCTION);
	bool crossing = false;

	if (!ValParamTrackOrientation(track)) return CMD_ERROR;
	TrackBits trackbit = TrackToTrackBits(track);

	/* Need to read tile owner now because it may change when the rail is removed
	 * Also, in case of floods, _current_company != owner
	 * There may be invalid tiletype even in exec run (when removing long track),
	 * so do not call GetTileOwner(tile) in any case here */
	Owner owner = INVALID_OWNER;

	Train *v = nullptr;

	switch (GetTileType(tile)) {
		case MP_ROAD: {
			if (!IsLevelCrossing(tile) || GetCrossingRailBits(tile) != trackbit) return_cmd_error(STR_ERROR_THERE_IS_NO_RAILROAD_TRACK);

			if (_current_company != OWNER_WATER) {
				CommandCost ret = CheckTileOwnership(tile);
				if (ret.Failed()) return ret;
			}

			if (!(flags & DC_BANKRUPT)) {
				CommandCost ret = EnsureNoVehicleOnGround(tile);
				if (ret.Failed()) return ret;
			}

			cost.AddCost(RailClearCost(GetRailType(tile)));

			if (flags & DC_EXEC) {
				if (HasReservedTracks(tile, trackbit)) {
					v = GetTrainForReservation(tile, track);
					if (v != nullptr) FreeTrainTrackReservation(v);
				}

				owner = GetTileOwner(tile);
				Company::Get(owner)->infrastructure.rail[GetRailType(tile)] -= LEVELCROSSING_TRACKBIT_FACTOR;
				DirtyCompanyInfrastructureWindows(owner);
				MakeRoadNormal(tile, GetCrossingRoadBits(tile), GetRoadTypeRoad(tile), GetRoadTypeTram(tile), GetTownIndex(tile), GetRoadOwner(tile, RTT_ROAD), GetRoadOwner(tile, RTT_TRAM));
				DeleteNewGRFInspectWindow(GSF_RAILTYPES, tile);
			}
			break;
		}

		case MP_RAILWAY: {
			TrackBits present;
			/* There are no rails present at depots. */
			if (!IsPlainRail(tile)) return_cmd_error(STR_ERROR_THERE_IS_NO_RAILROAD_TRACK);

			if (_current_company != OWNER_WATER) {
				CommandCost ret = CheckTileOwnership(tile);
				if (ret.Failed()) return ret;
			}

			CommandCost ret = EnsureNoTrainOnTrack(tile, track);
			if (ret.Failed()) return ret;

			present = GetTrackBits(tile);
			if ((present & trackbit) == 0) return_cmd_error(STR_ERROR_THERE_IS_NO_RAILROAD_TRACK);
			if (present == (TRACK_BIT_X | TRACK_BIT_Y)) crossing = true;

			cost.AddCost(RailClearCost(GetRailType(tile)));

			/* Charge extra to remove signals on the track, if they are there */
			if (HasSignalOnTrack(tile, track)) {
				cost.AddCost(DoCommand(tile, track, 0, flags, CMD_REMOVE_SIGNALS));
			}

			if (flags & DC_EXEC) {
				if (HasReservedTracks(tile, trackbit)) {
					v = GetTrainForReservation(tile, track);
					if (v != nullptr) FreeTrainTrackReservation(v);
				}

				owner = GetTileOwner(tile);

				/* Subtract old infrastructure count. */
				uint pieces = CountBits(present);
				if (TracksOverlap(present)) pieces *= pieces;
				Company::Get(owner)->infrastructure.rail[GetRailType(tile)] -= pieces;
				/* Add new infrastructure count. */
				present ^= trackbit;
				pieces = CountBits(present);
				if (TracksOverlap(present)) pieces *= pieces;
				Company::Get(owner)->infrastructure.rail[GetRailType(tile)] += pieces;
				DirtyCompanyInfrastructureWindows(owner);

				if (present == 0) {
					Slope tileh = GetTileSlope(tile);
					/* If there is flat water on the lower halftile, convert the tile to shore so the water remains */
					if (GetRailGroundType(tile) == RAIL_GROUND_WATER && IsSlopeWithOneCornerRaised(tileh)) {
						bool docking = IsDockingTile(tile);
						MakeShore(tile);
						SetDockingTile(tile, docking);
					} else {
						DoClearSquare(tile);
					}
					DeleteNewGRFInspectWindow(GSF_RAILTYPES, tile);
				} else {
					SetTrackBits(tile, present);
					SetTrackReservation(tile, GetRailReservationTrackBits(tile) & present);
				}
			}
			break;
		}

		default: return_cmd_error(STR_ERROR_THERE_IS_NO_RAILROAD_TRACK);
	}

	if (flags & DC_EXEC) {
		/* if we got that far, 'owner' variable is set correctly */
		assert(Company::IsValidID(owner));

		MarkTileDirtyByTile(tile);
		if (crossing) {
			/* crossing is set when only TRACK_BIT_X and TRACK_BIT_Y are set. As we
			 * are removing one of these pieces, we'll need to update signals for
			 * both directions explicitly, as after the track is removed it won't
			 * 'connect' with the other piece. */
			AddTrackToSignalBuffer(tile, TRACK_X, owner);
			AddTrackToSignalBuffer(tile, TRACK_Y, owner);
			YapfNotifyTrackLayoutChange(tile, TRACK_X);
			YapfNotifyTrackLayoutChange(tile, TRACK_Y);
		} else {
			AddTrackToSignalBuffer(tile, track, owner);
			YapfNotifyTrackLayoutChange(tile, track);
		}

		if (v != nullptr) TryPathReserve(v, true);
	}

	return cost;
}


/**
 * Called from water_cmd if a non-flat rail-tile gets flooded and should be converted to shore.
 * The function floods the lower halftile, if the tile has a halftile foundation.
 *
 * @param t The tile to flood.
 * @return true if something was flooded.
 */
bool FloodHalftile(TileIndex t)
{
	assert(IsPlainRailTile(t));

	bool flooded = false;
	if (GetRailGroundType(t) == RAIL_GROUND_WATER) return flooded;

	Slope tileh = GetTileSlope(t);
	TrackBits rail_bits = GetTrackBits(t);

	if (IsSlopeWithOneCornerRaised(tileh)) {
		TrackBits lower_track = CornerToTrackBits(OppositeCorner(GetHighestSlopeCorner(tileh)));

		TrackBits to_remove = lower_track & rail_bits;
		if (to_remove != 0) {
			Backup<CompanyID> cur_company(_current_company, OWNER_WATER, FILE_LINE);
			flooded = DoCommand(t, 0, FIND_FIRST_BIT(to_remove), DC_EXEC, CMD_REMOVE_SINGLE_RAIL).Succeeded();
			cur_company.Restore();
			if (!flooded) return flooded; // not yet floodable
			rail_bits = rail_bits & ~to_remove;
			if (rail_bits == 0) {
				MakeShore(t);
				MarkTileDirtyByTile(t);
				return flooded;
			}
		}

		if (IsNonContinuousFoundation(GetRailFoundation(tileh, rail_bits))) {
			flooded = true;
			SetRailGroundType(t, RAIL_GROUND_WATER);
			MarkTileDirtyByTile(t);
		}
	} else {
		/* Make shore on steep slopes and 'three-corners-raised'-slopes. */
		if (ApplyFoundationToSlope(GetRailFoundation(tileh, rail_bits), &tileh) == 0) {
			if (IsSteepSlope(tileh) || IsSlopeWithThreeCornersRaised(tileh)) {
				flooded = true;
				SetRailGroundType(t, RAIL_GROUND_WATER);
				MarkTileDirtyByTile(t);
			}
		}
	}
	return flooded;
}

static const TileIndexDiffC _trackdelta[] = {
	{ -1,  0 }, {  0,  1 }, { -1,  0 }, {  0,  1 }, {  1,  0 }, {  0,  1 },
	{  0,  0 },
	{  0,  0 },
	{  1,  0 }, {  0, -1 }, {  0, -1 }, {  1,  0 }, {  0, -1 }, { -1,  0 },
	{  0,  0 },
	{  0,  0 }
};


static CommandCost ValidateAutoDrag(Trackdir *trackdir, TileIndex start, TileIndex end)
{
	int x = TileX(start);
	int y = TileY(start);
	int ex = TileX(end);
	int ey = TileY(end);

	if (!ValParamTrackOrientation(TrackdirToTrack(*trackdir))) return CMD_ERROR;

	/* calculate delta x,y from start to end tile */
	int dx = ex - x;
	int dy = ey - y;

	/* calculate delta x,y for the first direction */
	int trdx = _trackdelta[*trackdir].x;
	int trdy = _trackdelta[*trackdir].y;

	if (!IsDiagonalTrackdir(*trackdir)) {
		trdx += _trackdelta[*trackdir ^ 1].x;
		trdy += _trackdelta[*trackdir ^ 1].y;
	}

	/* validate the direction */
	while ((trdx <= 0 && dx > 0) ||
			(trdx >= 0 && dx < 0) ||
			(trdy <= 0 && dy > 0) ||
			(trdy >= 0 && dy < 0)) {
		if (!HasBit(*trackdir, 3)) { // first direction is invalid, try the other
			SetBit(*trackdir, 3); // reverse the direction
			trdx = -trdx;
			trdy = -trdy;
		} else { // other direction is invalid too, invalid drag
			return CMD_ERROR;
		}
	}

	/* (for diagonal tracks, this is already made sure of by above test), but:
	 * for non-diagonal tracks, check if the start and end tile are on 1 line */
	if (!IsDiagonalTrackdir(*trackdir)) {
		trdx = _trackdelta[*trackdir].x;
		trdy = _trackdelta[*trackdir].y;
		if (abs(dx) != abs(dy) && abs(dx) + abs(trdy) != abs(dy) + abs(trdx)) return CMD_ERROR;
	}

	return CommandCost();
}

/**
 * Build or remove a stretch of railroad tracks.
 * @param tile start tile of drag
 * @param flags operation to perform
 * @param p1 end tile of drag
 * @param p2 various bitstuffed elements
 * - p2 = (bit 0-5) - railroad type normal/maglev (0 = normal, 1 = mono, 2 = maglev), only used for building
 * - p2 = (bit 6-8) - track-orientation, valid values: 0-5 (Track enum)
 * - p2 = (bit 9)   - 0 = build, 1 = remove tracks
 * - p2 = (bit 10)  - 0 = build up to an obstacle, 1 = fail if an obstacle is found (used for AIs).
 * - p2 = (bit 11)  - 0 = error on signal in the way, 1 = auto remove signals when in the way
 * @param text unused
 * @return the cost of this operation or an error
 */
static CommandCost CmdRailTrackHelper(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const std::string &text)
{
	CommandCost total_cost(EXPENSES_CONSTRUCTION);
	Track track = Extract<Track, 6, 3>(p2);
	bool remove = HasBit(p2, 9);
	bool auto_remove_signals = HasBit(p2, 11);
	RailType railtype = Extract<RailType, 0, 6>(p2);

	if ((!remove && !ValParamRailtype(railtype)) || !ValParamTrackOrientation(track)) return CMD_ERROR;
	if (p1 >= MapSize()) return CMD_ERROR;
	TileIndex end_tile = p1;
	Trackdir trackdir = TrackToTrackdir(track);

	CommandCost ret = ValidateAutoDrag(&trackdir, tile, end_tile);
	if (ret.Failed()) return ret;

	bool had_success = false;
	CommandCost last_error = CMD_ERROR;
	for (;;) {
		CommandCost ret = DoCommand(tile, remove ? 0 : railtype, TrackdirToTrack(trackdir) | (auto_remove_signals << 3), flags, remove ? CMD_REMOVE_SINGLE_RAIL : CMD_BUILD_SINGLE_RAIL);

		if (ret.Failed()) {
			last_error = ret;
			if (last_error.GetErrorMessage() != STR_ERROR_ALREADY_BUILT && !remove) {
				if (HasBit(p2, 10)) return last_error;
				break;
			}

			/* Ownership errors are more important. */
			if (last_error.GetErrorMessage() == STR_ERROR_OWNED_BY && remove) break;
		} else {
			had_success = true;
			total_cost.AddCost(ret);
		}

		if (tile == end_tile) break;

		tile += ToTileIndexDiff(_trackdelta[trackdir]);

		/* toggle railbit for the non-diagonal tracks */
		if (!IsDiagonalTrackdir(trackdir)) ToggleBit(trackdir, 0);
	}

	if (had_success) return total_cost;
	return last_error;
}

/**
 * Build rail on a stretch of track.
 * Stub for the unified rail builder/remover
 * @param tile start tile of drag
 * @param flags operation to perform
 * @param p1 end tile of drag
 * @param p2 various bitstuffed elements
 * - p2 = (bit 0-5) - railroad type normal/maglev (0 = normal, 1 = mono, 2 = maglev)
 * - p2 = (bit 6-8) - track-orientation, valid values: 0-5 (Track enum)
 * - p2 = (bit 9)   - 0 = build, 1 = remove tracks
 * @param text unused
 * @return the cost of this operation or an error
 * @see CmdRailTrackHelper
 */
CommandCost CmdBuildRailroadTrack(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const std::string &text)
{
	return CmdRailTrackHelper(tile, flags, p1, ClrBit(p2, 9), text);
}

/**
 * Build rail on a stretch of track.
 * Stub for the unified rail builder/remover
 * @param tile start tile of drag
 * @param flags operation to perform
 * @param p1 end tile of drag
 * @param p2 various bitstuffed elements
 * - p2 = (bit 0-5) - railroad type normal/maglev (0 = normal, 1 = mono, 2 = maglev), only used for building
 * - p2 = (bit 6-8) - track-orientation, valid values: 0-5 (Track enum)
 * - p2 = (bit 9)   - 0 = build, 1 = remove tracks
 * @param text unused
 * @return the cost of this operation or an error
 * @see CmdRailTrackHelper
 */
CommandCost CmdRemoveRailroadTrack(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const std::string &text)
{
	return CmdRailTrackHelper(tile, flags, p1, SetBit(p2, 9), text);
}

/**
 * Build a train depot
 * @param tile position of the train depot
 * @param flags operation to perform
 * @param p1 bits 0..5 : rail type
 *           bits 6..7 : entrance direction (DiagDirection)
 * @param p2 unused
 * @param text unused
 * @return the cost of this operation or an error
 *
 * @todo When checking for the tile slope,
 * distinguish between "Flat land required" and "land sloped in wrong direction"
 */
CommandCost CmdBuildTrainDepot(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const std::string &text)
{
	/* check railtype and valid direction for depot (0 through 3), 4 in total */
	RailType railtype = Extract<RailType, 0, 6>(p1);
	if (!ValParamRailtype(railtype)) return CMD_ERROR;

	Slope tileh = GetTileSlope(tile);

	DiagDirection dir = Extract<DiagDirection, 6, 2>(p1);

	CommandCost cost(EXPENSES_CONSTRUCTION);

	/* Prohibit construction if
	 * The tile is non-flat AND
	 * 1) build-on-slopes is disabled
	 * 2) the tile is steep i.e. spans two height levels
	 * 3) the exit points in the wrong direction
	 */

	if (tileh != SLOPE_FLAT) {
		if (!_settings_game.construction.build_on_slopes || !CanBuildDepotByTileh(dir, tileh)) {
			return_cmd_error(STR_ERROR_FLAT_LAND_REQUIRED);
		}
		cost.AddCost(_price[PR_BUILD_FOUNDATION]);
	}

	cost.AddCost(DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR));
	if (cost.Failed()) return cost;

	if (IsBridgeAbove(tile)) return_cmd_error(STR_ERROR_MUST_DEMOLISH_BRIDGE_FIRST);

	if (!Depot::CanAllocateItem()) return CMD_ERROR;

	if (flags & DC_EXEC) {
		Depot *d = new Depot(tile);
		d->build_date = _date;
		d->company = _current_company;
		d->veh_type = VEH_TRAIN;

		MakeRailDepot(tile, _current_company, d->index, dir, railtype);
		MarkTileDirtyByTile(tile);
		MakeDefaultName(d);

		Company::Get(_current_company)->infrastructure.rail[railtype]++;
		DirtyCompanyInfrastructureWindows(_current_company);

		AddSideToSignalBuffer(tile, INVALID_DIAGDIR, _current_company);
		YapfNotifyTrackLayoutChange(tile, DiagDirToDiagTrack(dir));
	}

	cost.AddCost(_price[PR_BUILD_DEPOT_TRAIN]);
	cost.AddCost(RailBuildCost(railtype));
	return cost;
}

/**
 * Build signals, alternate between double/single, signal/semaphore,
 * pre/exit/combo-signals, and what-else not. If the rail piece does not
 * have any signals, bit 4 (cycle signal-type) is ignored
 * @param tile tile where to build the signals
 * @param flags operation to perform
 * @param p1 various bitstuffed elements
 * - p1 = (bit 0-2) - track-orientation, valid values: 0-5 (Track enum)
 * - p1 = (bit 3)   - 1 = override signal/semaphore, or pre/exit/combo signal or (for bit 7) toggle variant (CTRL-toggle)
 * - p1 = (bit 4)   - 0 = signals, 1 = semaphores
 * - p1 = (bit 5-7) - type of the signal, for valid values see enum SignalType in rail_map.h
 * - p1 = (bit 8)   - convert the present signal type and variant
 * - p1 = (bit 9-11)- start cycle from this signal type
 * - p1 = (bit 12-14)-wrap around after this signal type
 * - p1 = (bit 15-16)-cycle the signal direction this many times
 * - p1 = (bit 17)  - 1 = don't modify an existing signal but don't fail either, 0 = always set new signal type
 * @param p2 used for CmdBuildManySignals() to copy direction of first signal
 * @param text unused
 * @return the cost of this operation or an error
 * @todo p2 should be replaced by two bits for "along" and "against" the track.
 */
CommandCost CmdBuildSingleSignal(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const std::string &text)
{
	Track track = Extract<Track, 0, 3>(p1);
	bool ctrl_pressed = HasBit(p1, 3); // was the CTRL button pressed
	SignalVariant sigvar = (ctrl_pressed ^ HasBit(p1, 4)) ? SIG_SEMAPHORE : SIG_ELECTRIC; // the signal variant of the new signal
	SignalType sigtype = Extract<SignalType, 5, 3>(p1); // the signal type of the new signal
	bool convert_signal = HasBit(p1, 8); // convert button pressed
	SignalType cycle_start = Extract<SignalType, 9, 3>(p1);
	SignalType cycle_stop = Extract<SignalType, 12, 3>(p1);
	uint num_dir_cycle = GB(p1, 15, 2);

	if (sigtype > SIGTYPE_LAST) return CMD_ERROR;
	if (cycle_start > cycle_stop || cycle_stop > SIGTYPE_LAST) return CMD_ERROR;

	/* You can only build signals on plain rail tiles, and the selected track must exist */
	if (!ValParamTrackOrientation(track) || !IsPlainRailTile(tile) ||
			!HasTrack(tile, track)) {
		return_cmd_error(STR_ERROR_THERE_IS_NO_RAILROAD_TRACK);
	}
	/* Protect against invalid signal copying */
	if (p2 != 0 && (p2 & SignalOnTrack(track)) == 0) return CMD_ERROR;

	CommandCost ret = CheckTileOwnership(tile);
	if (ret.Failed()) return ret;

	/* See if this is a valid track combination for signals (no overlap) */
	if (TracksOverlap(GetTrackBits(tile))) return_cmd_error(STR_ERROR_NO_SUITABLE_RAILROAD_TRACK);

	/* In case we don't want to change an existing signal, return without error. */
	if (HasBit(p1, 17) && HasSignalOnTrack(tile, track)) return CommandCost();

	/* you can not convert a signal if no signal is on track */
	if (convert_signal && !HasSignalOnTrack(tile, track)) return_cmd_error(STR_ERROR_THERE_ARE_NO_SIGNALS);

	CommandCost cost;
	if (!HasSignalOnTrack(tile, track)) {
		/* build new signals */
		cost = CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_SIGNALS]);
	} else {
		if (p2 != 0 && sigvar != GetSignalVariant(tile, track)) {
			/* convert signals <-> semaphores */
			cost = CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_SIGNALS] + _price[PR_CLEAR_SIGNALS]);

		} else if (convert_signal) {
			/* convert button pressed */
			if (ctrl_pressed || GetSignalVariant(tile, track) != sigvar) {
				/* convert electric <-> semaphore */
				cost = CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_SIGNALS] + _price[PR_CLEAR_SIGNALS]);
			} else {
				/* it is free to change signal type: normal-pre-exit-combo */
				cost = CommandCost();
			}

		} else {
			/* it is free to change orientation/pre-exit-combo signals */
			cost = CommandCost();
		}
	}

	if (flags & DC_EXEC) {
		Train *v = nullptr;
		/* The new/changed signal could block our path. As this can lead to
		 * stale reservations, we clear the path reservation here and try
		 * to redo it later on. */
		if (HasReservedTracks(tile, TrackToTrackBits(track))) {
			v = GetTrainForReservation(tile, track);
			if (v != nullptr) FreeTrainTrackReservation(v);
		}

		if (!HasSignals(tile)) {
			/* there are no signals at all on this tile yet */
			SetHasSignals(tile, true);
			SetSignalStates(tile, 0xF); // all signals are on
			SetPresentSignals(tile, 0); // no signals built by default
			SetSignalType(tile, track, sigtype);
			SetSignalVariant(tile, track, sigvar);
		}

		/* Subtract old signal infrastructure count. */
		Company::Get(GetTileOwner(tile))->infrastructure.signal -= CountBits(GetPresentSignals(tile));

		if (p2 == 0) {
			if (!HasSignalOnTrack(tile, track)) {
				/* build new signals */
				SetPresentSignals(tile, GetPresentSignals(tile) | (IsPbsSignal(sigtype) ? KillFirstBit(SignalOnTrack(track)) : SignalOnTrack(track)));
				SetSignalType(tile, track, sigtype);
				SetSignalVariant(tile, track, sigvar);
				while (num_dir_cycle-- > 0) CycleSignalSide(tile, track);
			} else {
				if (convert_signal) {
					/* convert signal button pressed */
					if (ctrl_pressed) {
						/* toggle the present signal variant: SIG_ELECTRIC <-> SIG_SEMAPHORE */
						SetSignalVariant(tile, track, (GetSignalVariant(tile, track) == SIG_ELECTRIC) ? SIG_SEMAPHORE : SIG_ELECTRIC);
						/* Query current signal type so the check for PBS signals below works. */
						sigtype = GetSignalType(tile, track);
					} else {
						/* convert the present signal to the chosen type and variant */
						SetSignalType(tile, track, sigtype);
						SetSignalVariant(tile, track, sigvar);
						if (IsPbsSignal(sigtype) && (GetPresentSignals(tile) & SignalOnTrack(track)) == SignalOnTrack(track)) {
							SetPresentSignals(tile, (GetPresentSignals(tile) & ~SignalOnTrack(track)) | KillFirstBit(SignalOnTrack(track)));
						}
					}

				} else if (ctrl_pressed) {
					/* cycle between cycle_start and cycle_end */
					sigtype = (SignalType)(GetSignalType(tile, track) + 1);

					if (sigtype < cycle_start || sigtype > cycle_stop) sigtype = cycle_start;

					SetSignalType(tile, track, sigtype);
					if (IsPbsSignal(sigtype) && (GetPresentSignals(tile) & SignalOnTrack(track)) == SignalOnTrack(track)) {
						SetPresentSignals(tile, (GetPresentSignals(tile) & ~SignalOnTrack(track)) | KillFirstBit(SignalOnTrack(track)));
					}
				} else {
					/* cycle the signal side: both -> left -> right -> both -> ... */
					CycleSignalSide(tile, track);
					/* Query current signal type so the check for PBS signals below works. */
					sigtype = GetSignalType(tile, track);
				}
			}
		} else {
			/* If CmdBuildManySignals is called with copying signals, just copy the
			 * direction of the first signal given as parameter by CmdBuildManySignals */
			SetPresentSignals(tile, (GetPresentSignals(tile) & ~SignalOnTrack(track)) | (p2 & SignalOnTrack(track)));
			SetSignalVariant(tile, track, sigvar);
			SetSignalType(tile, track, sigtype);
		}

		/* Add new signal infrastructure count. */
		Company::Get(GetTileOwner(tile))->infrastructure.signal += CountBits(GetPresentSignals(tile));
		DirtyCompanyInfrastructureWindows(GetTileOwner(tile));

		if (IsPbsSignal(sigtype)) {
			/* PBS signals should show red unless they are on reserved tiles without a train. */
			uint mask = GetPresentSignals(tile) & SignalOnTrack(track);
			SetSignalStates(tile, (GetSignalStates(tile) & ~mask) | ((HasBit(GetRailReservationTrackBits(tile), track) && EnsureNoVehicleOnGround(tile).Succeeded() ? UINT_MAX : 0) & mask));
		}
		MarkTileDirtyByTile(tile);
		AddTrackToSignalBuffer(tile, track, _current_company);
		YapfNotifyTrackLayoutChange(tile, track);
		if (v != nullptr && v->track != TRACK_BIT_DEPOT) {
			/* Extend the train's path if it's not stopped or loading, or not at a safe position. */
			if (!(((v->vehstatus & VS_STOPPED) && v->cur_speed == 0) || v->current_order.IsType(OT_LOADING)) ||
					!IsSafeWaitingPosition(v, v->tile, v->GetVehicleTrackdir(), true, _settings_game.pf.forbid_90_deg)) {
				TryPathReserve(v, true);
			}
		}
	}

	return cost;
}

static bool CheckSignalAutoFill(TileIndex &tile, Trackdir &trackdir, int &signal_ctr, bool remove)
{
	tile = AddTileIndexDiffCWrap(tile, _trackdelta[trackdir]);
	if (tile == INVALID_TILE) return false;

	/* Check for track bits on the new tile */
	TrackdirBits trackdirbits = TrackStatusToTrackdirBits(GetTileTrackStatus(tile, TRANSPORT_RAIL, 0));

	if (TracksOverlap(TrackdirBitsToTrackBits(trackdirbits))) return false;
	trackdirbits &= TrackdirReachesTrackdirs(trackdir);

	/* No track bits, must stop */
	if (trackdirbits == TRACKDIR_BIT_NONE) return false;

	/* Get the first track dir */
	trackdir = RemoveFirstTrackdir(&trackdirbits);

	/* Any left? It's a junction so we stop */
	if (trackdirbits != TRACKDIR_BIT_NONE) return false;

	switch (GetTileType(tile)) {
		case MP_RAILWAY:
			if (IsRailDepot(tile)) return false;
			if (!remove && HasSignalOnTrack(tile, TrackdirToTrack(trackdir))) return false;
			signal_ctr++;
			if (IsDiagonalTrackdir(trackdir)) {
				signal_ctr++;
				/* Ensure signal_ctr even so X and Y pieces get signals */
				ClrBit(signal_ctr, 0);
			}
			return true;

		case MP_ROAD:
			if (!IsLevelCrossing(tile)) return false;
			signal_ctr += 2;
			return true;

		case MP_TUNNELBRIDGE: {
			TileIndex orig_tile = tile; // backup old value

			if (GetTunnelBridgeTransportType(tile) != TRANSPORT_RAIL) return false;
			if (GetTunnelBridgeDirection(tile) != TrackdirToExitdir(trackdir)) return false;

			/* Skip to end of tunnel or bridge
			 * note that tile is a parameter by reference, so it must be updated */
			tile = GetOtherTunnelBridgeEnd(tile);

			signal_ctr += (GetTunnelBridgeLength(orig_tile, tile) + 2) * 2;
			return true;
		}

		default: return false;
	}
}

/**
 * Build many signals by dragging; AutoSignals
 * @param tile start tile of drag
 * @param flags operation to perform
 * @param p1  end tile of drag
 * @param p2 various bitstuffed elements
 * - p2 = (bit  0- 2) - track-orientation, valid values: 0-5 (Track enum)
 * - p2 = (bit  3)    - 1 = override signal/semaphore, or pre/exit/combo signal (CTRL-toggle)
 * - p2 = (bit  4)    - 0 = signals, 1 = semaphores
 * - p2 = (bit  5)    - 0 = build, 1 = remove signals
 * - p2 = (bit  6)    - 0 = selected stretch, 1 = auto fill
 * - p2 = (bit  7- 9) - default signal type
 * - p2 = (bit 10)    - 0 = keep fixed distance, 1 = minimise gaps between signals
 * - p2 = (bit 24-31) - user defined signals_density
 * @param text unused
 * @return the cost of this operation or an error
 */
static CommandCost CmdSignalTrackHelper(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const std::string &text)
{
	CommandCost total_cost(EXPENSES_CONSTRUCTION);
	TileIndex start_tile = tile;

	Track track = Extract<Track, 0, 3>(p2);
	bool mode = HasBit(p2, 3);
	bool semaphores = HasBit(p2, 4);
	bool remove = HasBit(p2, 5);
	bool autofill = HasBit(p2, 6);
	bool minimise_gaps = HasBit(p2, 10);
	byte signal_density = GB(p2, 24, 8);

	if (p1 >= MapSize() || !ValParamTrackOrientation(track)) return CMD_ERROR;
	TileIndex end_tile = p1;
	if (signal_density == 0 || signal_density > 20) return CMD_ERROR;

	if (!IsPlainRailTile(tile)) return_cmd_error(STR_ERROR_THERE_IS_NO_RAILROAD_TRACK);

	/* for vertical/horizontal tracks, double the given signals density
	 * since the original amount will be too dense (shorter tracks) */
	signal_density *= 2;

	Trackdir trackdir = TrackToTrackdir(track);
	CommandCost ret = ValidateAutoDrag(&trackdir, tile, end_tile);
	if (ret.Failed()) return ret;

	track = TrackdirToTrack(trackdir); // trackdir might have changed, keep track in sync
	Trackdir start_trackdir = trackdir;

	/* Must start on a valid track to be able to avoid loops */
	if (!HasTrack(tile, track)) return CMD_ERROR;

	SignalType sigtype = (SignalType)GB(p2, 7, 3);
	if (sigtype > SIGTYPE_LAST) return CMD_ERROR;

	byte signals;
	/* copy the signal-style of the first rail-piece if existing */
	if (HasSignalOnTrack(tile, track)) {
		signals = GetPresentSignals(tile) & SignalOnTrack(track);
		assert(signals != 0);

		/* copy signal/semaphores style (independent of CTRL) */
		semaphores = GetSignalVariant(tile, track) != SIG_ELECTRIC;

		sigtype = GetSignalType(tile, track);
		/* Don't but copy entry or exit-signal type */
		if (sigtype == SIGTYPE_ENTRY || sigtype == SIGTYPE_EXIT) sigtype = SIGTYPE_NORMAL;
	} else { // no signals exist, drag a two-way signal stretch
		signals = IsPbsSignal(sigtype) ? SignalAlongTrackdir(trackdir) : SignalOnTrack(track);
	}

	byte signal_dir = 0;
	if (signals & SignalAlongTrackdir(trackdir))   SetBit(signal_dir, 0);
	if (signals & SignalAgainstTrackdir(trackdir)) SetBit(signal_dir, 1);

	/* signal_ctr         - amount of tiles already processed
	 * last_used_ctr      - amount of tiles before previously placed signal
	 * signals_density    - setting to put signal on every Nth tile (double space on |, -- tracks)
	 * last_suitable_ctr  - amount of tiles before last possible signal place
	 * last_suitable_tile - last tile where it is possible to place a signal
	 * last_suitable_trackdir - trackdir of the last tile
	 **********
	 * trackdir   - trackdir to build with autorail
	 * semaphores - semaphores or signals
	 * signals    - is there a signal/semaphore on the first tile, copy its style (two-way/single-way)
	 *              and convert all others to semaphore/signal
	 * remove     - 1 remove signals, 0 build signals */
	int signal_ctr = 0;
	int last_used_ctr = INT_MIN; // initially INT_MIN to force building/removing at the first tile
	int last_suitable_ctr = 0;
	TileIndex last_suitable_tile = INVALID_TILE;
	Trackdir last_suitable_trackdir = INVALID_TRACKDIR;
	CommandCost last_error = CMD_ERROR;
	bool had_success = false;
	for (;;) {
		/* only build/remove signals with the specified density */
		if (remove || minimise_gaps || signal_ctr % signal_density == 0) {
			uint32 param1 = GB(TrackdirToTrack(trackdir), 0, 3);
			SB(param1, 3, 1, mode);
			SB(param1, 4, 1, semaphores);
			SB(param1, 5, 3, sigtype);
			if (!remove && signal_ctr == 0) SetBit(param1, 17);

			/* Pick the correct orientation for the track direction */
			signals = 0;
			if (HasBit(signal_dir, 0)) signals |= SignalAlongTrackdir(trackdir);
			if (HasBit(signal_dir, 1)) signals |= SignalAgainstTrackdir(trackdir);

			/* Test tiles in between for suitability as well if minimising gaps. */
			bool test_only = !remove && minimise_gaps && signal_ctr < (last_used_ctr + signal_density);
			CommandCost ret = DoCommand(tile, param1, signals, test_only ? flags & ~DC_EXEC : flags, remove ? CMD_REMOVE_SIGNALS : CMD_BUILD_SIGNALS);

			if (ret.Succeeded()) {
				/* Remember last track piece where we can place a signal. */
				last_suitable_ctr = signal_ctr;
				last_suitable_tile = tile;
				last_suitable_trackdir = trackdir;
			} else if (!test_only && last_suitable_tile != INVALID_TILE) {
				/* If a signal can't be placed, place it at the last possible position. */
				SB(param1, 0, 3, TrackdirToTrack(last_suitable_trackdir));
				ClrBit(param1, 17);

				/* Pick the correct orientation for the track direction. */
				signals = 0;
				if (HasBit(signal_dir, 0)) signals |= SignalAlongTrackdir(last_suitable_trackdir);
				if (HasBit(signal_dir, 1)) signals |= SignalAgainstTrackdir(last_suitable_trackdir);

				ret = DoCommand(last_suitable_tile, param1, signals, flags, remove ? CMD_REMOVE_SIGNALS : CMD_BUILD_SIGNALS);
			}

			/* Collect cost. */
			if (!test_only) {
				/* Be user-friendly and try placing signals as much as possible */
				if (ret.Succeeded()) {
					had_success = true;
					total_cost.AddCost(ret);
					last_used_ctr = last_suitable_ctr;
					last_suitable_tile = INVALID_TILE;
				} else {
					/* The "No railway" error is the least important one. */
					if (ret.GetErrorMessage() != STR_ERROR_THERE_IS_NO_RAILROAD_TRACK ||
							last_error.GetErrorMessage() == INVALID_STRING_ID) {
						last_error = ret;
					}
				}
			}
		}

		if (autofill) {
			if (!CheckSignalAutoFill(tile, trackdir, signal_ctr, remove)) break;

			/* Prevent possible loops */
			if (tile == start_tile && trackdir == start_trackdir) break;
		} else {
			if (tile == end_tile) break;

			tile += ToTileIndexDiff(_trackdelta[trackdir]);
			signal_ctr++;

			/* toggle railbit for the non-diagonal tracks (|, -- tracks) */
			if (IsDiagonalTrackdir(trackdir)) {
				signal_ctr++;
			} else {
				ToggleBit(trackdir, 0);
			}
		}
	}

	return had_success ? total_cost : last_error;
}

/**
 * Build signals on a stretch of track.
 * Stub for the unified signal builder/remover
 * @param tile start tile of drag
 * @param flags operation to perform
 * @param p1  end tile of drag
 * @param p2 various bitstuffed elements
 * - p2 = (bit  0- 2) - track-orientation, valid values: 0-5 (Track enum)
 * - p2 = (bit  3)    - 1 = override signal/semaphore, or pre/exit/combo signal (CTRL-toggle)
 * - p2 = (bit  4)    - 0 = signals, 1 = semaphores
 * - p2 = (bit  5)    - 0 = build, 1 = remove signals
 * - p2 = (bit  6)    - 0 = selected stretch, 1 = auto fill
 * - p2 = (bit  7- 9) - default signal type
 * - p2 = (bit 24-31) - user defined signals_density
 * @param text unused
 * @return the cost of this operation or an error
 * @see CmdSignalTrackHelper
 */
CommandCost CmdBuildSignalTrack(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const std::string &text)
{
	return CmdSignalTrackHelper(tile, flags, p1, p2, text);
}

/**
 * Remove signals
 * @param tile coordinates where signal is being deleted from
 * @param flags operation to perform
 * @param p1 various bitstuffed elements, only track information is used
 *           - (bit  0- 2) - track-orientation, valid values: 0-5 (Track enum)
 *           - (bit  3)    - override signal/semaphore, or pre/exit/combo signal (CTRL-toggle)
 *           - (bit  4)    - 0 = signals, 1 = semaphores
 * @param p2 unused
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdRemoveSingleSignal(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const std::string &text)
{
	Track track = Extract<Track, 0, 3>(p1);

	if (!ValParamTrackOrientation(track) || !IsPlainRailTile(tile) || !HasTrack(tile, track)) {
		return_cmd_error(STR_ERROR_THERE_IS_NO_RAILROAD_TRACK);
	}
	if (!HasSignalOnTrack(tile, track)) {
		return_cmd_error(STR_ERROR_THERE_ARE_NO_SIGNALS);
	}

	/* Only water can remove signals from anyone */
	if (_current_company != OWNER_WATER) {
		CommandCost ret = CheckTileOwnership(tile);
		if (ret.Failed()) return ret;
	}

	/* Do it? */
	if (flags & DC_EXEC) {
		Train *v = nullptr;
		if (HasReservedTracks(tile, TrackToTrackBits(track))) {
			v = GetTrainForReservation(tile, track);
		} else if (IsPbsSignal(GetSignalType(tile, track))) {
			/* PBS signal, might be the end of a path reservation. */
			Trackdir td = TrackToTrackdir(track);
			for (int i = 0; v == nullptr && i < 2; i++, td = ReverseTrackdir(td)) {
				/* Only test the active signal side. */
				if (!HasSignalOnTrackdir(tile, ReverseTrackdir(td))) continue;
				TileIndex next = TileAddByDiagDir(tile, TrackdirToExitdir(td));
				TrackBits tracks = TrackdirBitsToTrackBits(TrackdirReachesTrackdirs(td));
				if (HasReservedTracks(next, tracks)) {
					v = GetTrainForReservation(next, TrackBitsToTrack(GetReservedTrackbits(next) & tracks));
				}
			}
		}
		Company::Get(GetTileOwner(tile))->infrastructure.signal -= CountBits(GetPresentSignals(tile));
		SetPresentSignals(tile, GetPresentSignals(tile) & ~SignalOnTrack(track));
		Company::Get(GetTileOwner(tile))->infrastructure.signal += CountBits(GetPresentSignals(tile));
		DirtyCompanyInfrastructureWindows(GetTileOwner(tile));

		/* removed last signal from tile? */
		if (GetPresentSignals(tile) == 0) {
			SetSignalStates(tile, 0);
			SetHasSignals(tile, false);
			SetSignalVariant(tile, INVALID_TRACK, SIG_ELECTRIC); // remove any possible semaphores
		}

		AddTrackToSignalBuffer(tile, track, GetTileOwner(tile));
		YapfNotifyTrackLayoutChange(tile, track);
		if (v != nullptr) TryPathReserve(v, false);

		MarkTileDirtyByTile(tile);
	}

	return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_CLEAR_SIGNALS]);
}

/**
 * Remove signals on a stretch of track.
 * Stub for the unified signal builder/remover
 * @param tile start tile of drag
 * @param flags operation to perform
 * @param p1  end tile of drag
 * @param p2 various bitstuffed elements
 * - p2 = (bit  0- 2) - track-orientation, valid values: 0-5 (Track enum)
 * - p2 = (bit  3)    - 1 = override signal/semaphore, or pre/exit/combo signal (CTRL-toggle)
 * - p2 = (bit  4)    - 0 = signals, 1 = semaphores
 * - p2 = (bit  5)    - 0 = build, 1 = remove signals
 * - p2 = (bit  6)    - 0 = selected stretch, 1 = auto fill
 * - p2 = (bit  7- 9) - default signal type
 * - p2 = (bit 24-31) - user defined signals_density
 * @param text unused
 * @return the cost of this operation or an error
 * @see CmdSignalTrackHelper
 */
CommandCost CmdRemoveSignalTrack(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const std::string &text)
{
	return CmdSignalTrackHelper(tile, flags, p1, SetBit(p2, 5), text); // bit 5 is remove bit
}

/** Update power of train under which is the railtype being converted */
static Vehicle *UpdateTrainPowerProc(Vehicle *v, void *data)
{
	if (v->type != VEH_TRAIN) return nullptr;

	TrainList *affected_trains = static_cast<TrainList*>(data);
	include(*affected_trains, Train::From(v)->First());

	return nullptr;
}

/**
 * Convert one rail type to the other. You can convert normal rail to
 * monorail/maglev easily or vice-versa.
 * @param tile end tile of rail conversion drag
 * @param flags operation to perform
 * @param p1 start tile of drag
 * @param p2 various bitstuffed elements:
 * - p2 = (bit  0- 5) new railtype to convert to.
 * - p2 = (bit  6)    build diagonally or not.
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdConvertRail(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const std::string &text)
{
	RailType totype = Extract<RailType, 0, 6>(p2);
	TileIndex area_start = p1;
	TileIndex area_end = tile;
	bool diagonal = HasBit(p2, 6);

	if (!ValParamRailtype(totype)) return CMD_ERROR;
	if (area_start >= MapSize()) return CMD_ERROR;

	TrainList affected_trains;

	CommandCost cost(EXPENSES_CONSTRUCTION);
	CommandCost error = CommandCost(STR_ERROR_NO_SUITABLE_RAILROAD_TRACK); // by default, there is no track to convert.
	bool found_convertible_track = false; // whether we actually did convert some track (see bug #7633)

	TileIterator *iter = diagonal ? (TileIterator *)new DiagonalTileIterator(area_start, area_end) : new OrthogonalTileIterator(area_start, area_end);
	for (; (tile = *iter) != INVALID_TILE; ++(*iter)) {
		TileType tt = GetTileType(tile);

		/* Check if there is any track on tile */
		switch (tt) {
			case MP_RAILWAY:
				break;
			case MP_STATION:
				if (!HasStationRail(tile)) continue;
				break;
			case MP_ROAD:
				if (!IsLevelCrossing(tile)) continue;
				if (RailNoLevelCrossings(totype)) {
					error.MakeError(STR_ERROR_CROSSING_DISALLOWED_RAIL);
					continue;
				}
				break;
			case MP_TUNNELBRIDGE:
				if (GetTunnelBridgeTransportType(tile) != TRANSPORT_RAIL) continue;
				break;
			default: continue;
		}

		/* Original railtype we are converting from */
		RailType type = GetRailType(tile);

		/* Converting to the same type or converting 'hidden' elrail -> rail */
		if (type == totype || (_settings_game.vehicle.disable_elrails && totype == RAILTYPE_RAIL && type == RAILTYPE_ELECTRIC)) continue;

		/* Trying to convert other's rail */
		CommandCost ret = CheckTileOwnership(tile);
		if (ret.Failed()) {
			error = ret;
			continue;
		}

		std::vector<Train *> vehicles_affected;

		/* Vehicle on the tile when not converting Rail <-> ElRail
		 * Tunnels and bridges have special check later */
		if (tt != MP_TUNNELBRIDGE) {
			if (!IsCompatibleRail(type, totype)) {
				CommandCost ret = IsPlainRailTile(tile) ? EnsureNoTrainOnTrackBits(tile, GetTrackBits(tile)) : EnsureNoVehicleOnGround(tile);
				if (ret.Failed()) {
					error = ret;
					continue;
				}
			}
			if (flags & DC_EXEC) { // we can safely convert, too
				TrackBits reserved = GetReservedTrackbits(tile);
				Track     track;
				while ((track = RemoveFirstTrack(&reserved)) != INVALID_TRACK) {
					Train *v = GetTrainForReservation(tile, track);
					if (v != nullptr && !HasPowerOnRail(v->railtype, totype)) {
						/* No power on new rail type, reroute. */
						FreeTrainTrackReservation(v);
						vehicles_affected.push_back(v);
					}
				}

				/* Update the company infrastructure counters. */
				if (!IsRailStationTile(tile) || !IsStationTileBlocked(tile)) {
					Company *c = Company::Get(GetTileOwner(tile));
					uint num_pieces = IsLevelCrossingTile(tile) ? LEVELCROSSING_TRACKBIT_FACTOR : 1;
					if (IsPlainRailTile(tile)) {
						TrackBits bits = GetTrackBits(tile);
						num_pieces = CountBits(bits);
						if (TracksOverlap(bits)) num_pieces *= num_pieces;
					}
					c->infrastructure.rail[type] -= num_pieces;
					c->infrastructure.rail[totype] += num_pieces;
					DirtyCompanyInfrastructureWindows(c->index);
				}

				SetRailType(tile, totype);
				MarkTileDirtyByTile(tile);
				/* update power of train on this tile */
				FindVehicleOnPos(tile, &affected_trains, &UpdateTrainPowerProc);
			}
		}

		switch (tt) {
			case MP_RAILWAY:
				switch (GetRailTileType(tile)) {
					case RAIL_TILE_DEPOT:
						if (flags & DC_EXEC) {
							/* notify YAPF about the track layout change */
							YapfNotifyTrackLayoutChange(tile, GetRailDepotTrack(tile));

							/* Update build vehicle window related to this depot */
							DepotID depot_id = GetDepotIndex(tile);
							InvalidateWindowData(WC_VEHICLE_DEPOT, depot_id);
							InvalidateWindowData(WC_BUILD_VEHICLE, depot_id);
						}
						found_convertible_track = true;
						cost.AddCost(RailConvertCost(type, totype));
						break;

					default: // RAIL_TILE_NORMAL, RAIL_TILE_SIGNALS
						if (flags & DC_EXEC) {
							/* notify YAPF about the track layout change */
							TrackBits tracks = GetTrackBits(tile);
							while (tracks != TRACK_BIT_NONE) {
								YapfNotifyTrackLayoutChange(tile, RemoveFirstTrack(&tracks));
							}
						}
						found_convertible_track = true;
						cost.AddCost(RailConvertCost(type, totype) * CountBits(GetTrackBits(tile)));
						break;
				}
				break;

			case MP_TUNNELBRIDGE: {
				TileIndex endtile = GetOtherTunnelBridgeEnd(tile);

				/* If both ends of tunnel/bridge are in the range, do not try to convert twice -
				 * it would cause assert because of different test and exec runs */
				if (endtile < tile) {
					if (diagonal) {
						if (DiagonalTileArea(area_start, area_end).Contains(endtile)) continue;
					} else {
						if (OrthogonalTileArea(area_start, area_end).Contains(endtile)) continue;
					}
				}

				/* When not converting rail <-> el. rail, any vehicle cannot be in tunnel/bridge */
				if (!IsCompatibleRail(GetRailType(tile), totype)) {
					CommandCost ret = TunnelBridgeIsFree(tile, endtile);
					if (ret.Failed()) {
						error = ret;
						continue;
					}
				}

				if (flags & DC_EXEC) {
					Track track = DiagDirToDiagTrack(GetTunnelBridgeDirection(tile));
					if (HasTunnelBridgeReservation(tile)) {
						Train *v = GetTrainForReservation(tile, track);
						if (v != nullptr && !HasPowerOnRail(v->railtype, totype)) {
							/* No power on new rail type, reroute. */
							FreeTrainTrackReservation(v);
							vehicles_affected.push_back(v);
						}
					}

					/* Update the company infrastructure counters. */
					uint num_pieces = (GetTunnelBridgeLength(tile, endtile) + 2) * TUNNELBRIDGE_TRACKBIT_FACTOR;
					Company *c = Company::Get(GetTileOwner(tile));
					c->infrastructure.rail[GetRailType(tile)] -= num_pieces;
					c->infrastructure.rail[totype] += num_pieces;
					DirtyCompanyInfrastructureWindows(c->index);

					SetRailType(tile, totype);
					SetRailType(endtile, totype);

					FindVehicleOnPos(tile, &affected_trains, &UpdateTrainPowerProc);
					FindVehicleOnPos(endtile, &affected_trains, &UpdateTrainPowerProc);

					YapfNotifyTrackLayoutChange(tile, track);
					YapfNotifyTrackLayoutChange(endtile, track);

					if (IsBridge(tile)) {
						MarkBridgeDirty(tile);
					} else {
						MarkTileDirtyByTile(tile);
						MarkTileDirtyByTile(endtile);
					}
				}

				found_convertible_track = true;
				cost.AddCost((GetTunnelBridgeLength(tile, endtile) + 2) * RailConvertCost(type, totype));
				break;
			}

			default: // MP_STATION, MP_ROAD
				if (flags & DC_EXEC) {
					Track track = ((tt == MP_STATION) ? GetRailStationTrack(tile) : GetCrossingRailTrack(tile));
					YapfNotifyTrackLayoutChange(tile, track);
				}

				found_convertible_track = true;
				cost.AddCost(RailConvertCost(type, totype));
				break;
		}

		for (uint i = 0; i < vehicles_affected.size(); ++i) {
			TryPathReserve(vehicles_affected[i], true);
		}
	}

	if (flags & DC_EXEC) {
		/* Railtype changed, update trains as when entering different track */
		for (Train *v : affected_trains) {
			v->ConsistChanged(CCF_TRACK);
		}
	}

	delete iter;
	return found_convertible_track ? cost : error;
}

static CommandCost RemoveTrainDepot(TileIndex tile, DoCommandFlag flags)
{
	if (_current_company != OWNER_WATER) {
		CommandCost ret = CheckTileOwnership(tile);
		if (ret.Failed()) return ret;
	}

	CommandCost ret = EnsureNoVehicleOnGround(tile);
	if (ret.Failed()) return ret;

	if (flags & DC_EXEC) {
		/* read variables before the depot is removed */
		DiagDirection dir = GetRailDepotDirection(tile);
		Owner owner = GetTileOwner(tile);
		Train *v = nullptr;

		if (HasDepotReservation(tile)) {
			v = GetTrainForReservation(tile, DiagDirToDiagTrack(dir));
			if (v != nullptr) FreeTrainTrackReservation(v);
		}

		Company::Get(owner)->infrastructure.rail[GetRailType(tile)]--;
		DirtyCompanyInfrastructureWindows(owner);

		delete Depot::GetByTile(tile);
		DoClearSquare(tile);
		AddSideToSignalBuffer(tile, dir, owner);
		YapfNotifyTrackLayoutChange(tile, DiagDirToDiagTrack(dir));
		if (v != nullptr) TryPathReserve(v, true);
	}

	return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_CLEAR_DEPOT_TRAIN]);
}

static CommandCost ClearTile_Track(TileIndex tile, DoCommandFlag flags)
{
	CommandCost cost(EXPENSES_CONSTRUCTION);

	if (flags & DC_AUTO) {
		if (!IsTileOwner(tile, _current_company)) {
			return_cmd_error(STR_ERROR_AREA_IS_OWNED_BY_ANOTHER);
		}

		if (IsPlainRail(tile)) {
			return_cmd_error(STR_ERROR_MUST_REMOVE_RAILROAD_TRACK);
		} else {
			return_cmd_error(STR_ERROR_BUILDING_MUST_BE_DEMOLISHED);
		}
	}

	switch (GetRailTileType(tile)) {
		case RAIL_TILE_SIGNALS:
		case RAIL_TILE_NORMAL: {
			Slope tileh = GetTileSlope(tile);
			/* Is there flat water on the lower halftile that gets cleared expensively? */
			bool water_ground = (GetRailGroundType(tile) == RAIL_GROUND_WATER && IsSlopeWithOneCornerRaised(tileh));

			TrackBits tracks = GetTrackBits(tile);
			while (tracks != TRACK_BIT_NONE) {
				Track track = RemoveFirstTrack(&tracks);
				CommandCost ret = DoCommand(tile, 0, track, flags, CMD_REMOVE_SINGLE_RAIL);
				if (ret.Failed()) return ret;
				cost.AddCost(ret);
			}

			/* When bankrupting, don't make water dirty, there could be a ship on lower halftile.
			 * Same holds for non-companies clearing the tile, e.g. disasters. */
			if (water_ground && !(flags & DC_BANKRUPT) && Company::IsValidID(_current_company)) {
				CommandCost ret = EnsureNoVehicleOnGround(tile);
				if (ret.Failed()) return ret;

				/* The track was removed, and left a coast tile. Now also clear the water. */
				if (flags & DC_EXEC) {
					bool remove = IsDockingTile(tile);
					DoClearSquare(tile);
					if (remove) RemoveDockingTile(tile);
				}
				cost.AddCost(_price[PR_CLEAR_WATER]);
			}

			return cost;
		}

		case RAIL_TILE_DEPOT:
			return RemoveTrainDepot(tile, flags);

		default:
			return CMD_ERROR;
	}
}

/**
 * Get surface height in point (x,y)
 * On tiles with halftile foundations move (x,y) to a safe point wrt. track
 */
static uint GetSaveSlopeZ(uint x, uint y, Track track)
{
	switch (track) {
		case TRACK_UPPER: x &= ~0xF; y &= ~0xF; break;
		case TRACK_LOWER: x |=  0xF; y |=  0xF; break;
		case TRACK_LEFT:  x |=  0xF; y &= ~0xF; break;
		case TRACK_RIGHT: x &= ~0xF; y |=  0xF; break;
		default: break;
	}
	return GetSlopePixelZ(x, y);
}

static void DrawSingleSignal(TileIndex tile, const RailtypeInfo *rti, Track track, SignalState condition, SignalOffsets image, uint pos)
{
	bool side;
	switch (_settings_game.construction.train_signal_side) {
		case 0:  side = false;                                 break; // left
		case 2:  side = true;                                  break; // right
		default: side = _settings_game.vehicle.road_side != 0; break; // driving side
	}
	static const Point SignalPositions[2][12] = {
		{ // Signals on the left side
		/*  LEFT      LEFT      RIGHT     RIGHT     UPPER     UPPER */
			{ 8,  5}, {14,  1}, { 1, 14}, { 9, 11}, { 1,  0}, { 3, 10},
		/*  LOWER     LOWER     X         X         Y         Y     */
			{11,  4}, {14, 14}, {11,  3}, { 4, 13}, { 3,  4}, {11, 13}
		}, { // Signals on the right side
		/*  LEFT      LEFT      RIGHT     RIGHT     UPPER     UPPER */
			{14,  1}, {12, 10}, { 4,  6}, { 1, 14}, {10,  4}, { 0,  1},
		/*  LOWER     LOWER     X         X         Y         Y     */
			{14, 14}, { 5, 12}, {11, 13}, { 4,  3}, {13,  4}, { 3, 11}
		}
	};

	uint x = TileX(tile) * TILE_SIZE + SignalPositions[side][pos].x;
	uint y = TileY(tile) * TILE_SIZE + SignalPositions[side][pos].y;

	SignalType type       = GetSignalType(tile, track);
	SignalVariant variant = GetSignalVariant(tile, track);

	SpriteID sprite = GetCustomSignalSprite(rti, tile, type, variant, condition);
	if (sprite != 0) {
		sprite += image;
	} else {
		/* Normal electric signals are stored in a different sprite block than all other signals. */
		sprite = (type == SIGTYPE_NORMAL && variant == SIG_ELECTRIC) ? SPR_ORIGINAL_SIGNALS_BASE : SPR_SIGNALS_BASE - 16;
		sprite += type * 16 + variant * 64 + image * 2 + condition + (type > SIGTYPE_LAST_NOPBS ? 64 : 0);
	}

	AddSortableSpriteToDraw(sprite, PAL_NONE, x, y, 1, 1, BB_HEIGHT_UNDER_BRIDGE, GetSaveSlopeZ(x, y, track));
}

static uint32 _drawtile_track_palette;



/** Offsets for drawing fences */
struct FenceOffset {
	Corner height_ref;  //!< Corner to use height offset from.
	int x_offs;         //!< Bounding box X offset.
	int y_offs;         //!< Bounding box Y offset.
	int x_size;         //!< Bounding box X size.
	int y_size;         //!< Bounding box Y size.
};

/** Offsets for drawing fences */
static FenceOffset _fence_offsets[] = {
	{ CORNER_INVALID,  0,  1, 16,  1 }, // RFO_FLAT_X_NW
	{ CORNER_INVALID,  1,  0,  1, 16 }, // RFO_FLAT_Y_NE
	{ CORNER_W,        8,  8,  1,  1 }, // RFO_FLAT_LEFT
	{ CORNER_N,        8,  8,  1,  1 }, // RFO_FLAT_UPPER
	{ CORNER_INVALID,  0,  1, 16,  1 }, // RFO_SLOPE_SW_NW
	{ CORNER_INVALID,  1,  0,  1, 16 }, // RFO_SLOPE_SE_NE
	{ CORNER_INVALID,  0,  1, 16,  1 }, // RFO_SLOPE_NE_NW
	{ CORNER_INVALID,  1,  0,  1, 16 }, // RFO_SLOPE_NW_NE
	{ CORNER_INVALID,  0, 15, 16,  1 }, // RFO_FLAT_X_SE
	{ CORNER_INVALID, 15,  0,  1, 16 }, // RFO_FLAT_Y_SW
	{ CORNER_E,        8,  8,  1,  1 }, // RFO_FLAT_RIGHT
	{ CORNER_S,        8,  8,  1,  1 }, // RFO_FLAT_LOWER
	{ CORNER_INVALID,  0, 15, 16,  1 }, // RFO_SLOPE_SW_SE
	{ CORNER_INVALID, 15,  0,  1, 16 }, // RFO_SLOPE_SE_SW
	{ CORNER_INVALID,  0, 15, 16,  1 }, // RFO_SLOPE_NE_SE
	{ CORNER_INVALID, 15,  0,  1, 16 }, // RFO_SLOPE_NW_SW
};

/**
 * Draw a track fence.
 * @param ti Tile drawing information.
 * @param base_image First fence sprite.
 * @param num_sprites Number of fence sprites.
 * @param rfo Fence to draw.
 */
static void DrawTrackFence(const TileInfo *ti, SpriteID base_image, uint num_sprites, RailFenceOffset rfo)
{
	int z = ti->z;
	if (_fence_offsets[rfo].height_ref != CORNER_INVALID) {
		z += GetSlopePixelZInCorner(RemoveHalftileSlope(ti->tileh), _fence_offsets[rfo].height_ref);
	}
	AddSortableSpriteToDraw(base_image + (rfo % num_sprites), _drawtile_track_palette,
		ti->x + _fence_offsets[rfo].x_offs,
		ti->y + _fence_offsets[rfo].y_offs,
		_fence_offsets[rfo].x_size,
		_fence_offsets[rfo].y_size,
		4, z);
}

/**
 * Draw fence at NW border matching the tile slope.
 */
static void DrawTrackFence_NW(const TileInfo *ti, SpriteID base_image, uint num_sprites)
{
	RailFenceOffset rfo = RFO_FLAT_X_NW;
	if (ti->tileh & SLOPE_NW) rfo = (ti->tileh & SLOPE_W) ? RFO_SLOPE_SW_NW : RFO_SLOPE_NE_NW;
	DrawTrackFence(ti, base_image, num_sprites, rfo);
}

/**
 * Draw fence at SE border matching the tile slope.
 */
static void DrawTrackFence_SE(const TileInfo *ti, SpriteID base_image, uint num_sprites)
{
	RailFenceOffset rfo = RFO_FLAT_X_SE;
	if (ti->tileh & SLOPE_SE) rfo = (ti->tileh & SLOPE_S) ? RFO_SLOPE_SW_SE : RFO_SLOPE_NE_SE;
	DrawTrackFence(ti, base_image, num_sprites, rfo);
}

/**
 * Draw fence at NE border matching the tile slope.
 */
static void DrawTrackFence_NE(const TileInfo *ti, SpriteID base_image, uint num_sprites)
{
	RailFenceOffset rfo = RFO_FLAT_Y_NE;
	if (ti->tileh & SLOPE_NE) rfo = (ti->tileh & SLOPE_E) ? RFO_SLOPE_SE_NE : RFO_SLOPE_NW_NE;
	DrawTrackFence(ti, base_image, num_sprites, rfo);
}

/**
 * Draw fence at SW border matching the tile slope.
 */
static void DrawTrackFence_SW(const TileInfo *ti, SpriteID base_image, uint num_sprites)
{
	RailFenceOffset rfo = RFO_FLAT_Y_SW;
	if (ti->tileh & SLOPE_SW) rfo = (ti->tileh & SLOPE_S) ? RFO_SLOPE_SE_SW : RFO_SLOPE_NW_SW;
	DrawTrackFence(ti, base_image, num_sprites, rfo);
}

/**
 * Draw track fences.
 * @param ti Tile drawing information.
 * @param rti Rail type information.
 */
static void DrawTrackDetails(const TileInfo *ti, const RailtypeInfo *rti)
{
	/* Base sprite for track fences.
	 * Note: Halftile slopes only have fences on the upper part. */
	uint num_sprites = 0;
	SpriteID base_image = GetCustomRailSprite(rti, ti->tile, RTSG_FENCES, IsHalftileSlope(ti->tileh) ? TCX_UPPER_HALFTILE : TCX_NORMAL, &num_sprites);
	if (base_image == 0) {
		base_image = SPR_TRACK_FENCE_FLAT_X;
		num_sprites = 8;
	}

	assert(num_sprites > 0);

	switch (GetRailGroundType(ti->tile)) {
		case RAIL_GROUND_FENCE_NW:     DrawTrackFence_NW(ti, base_image, num_sprites); break;
		case RAIL_GROUND_FENCE_SE:     DrawTrackFence_SE(ti, base_image, num_sprites); break;
		case RAIL_GROUND_FENCE_SENW:   DrawTrackFence_NW(ti, base_image, num_sprites);
		                               DrawTrackFence_SE(ti, base_image, num_sprites); break;
		case RAIL_GROUND_FENCE_NE:     DrawTrackFence_NE(ti, base_image, num_sprites); break;
		case RAIL_GROUND_FENCE_SW:     DrawTrackFence_SW(ti, base_image, num_sprites); break;
		case RAIL_GROUND_FENCE_NESW:   DrawTrackFence_NE(ti, base_image, num_sprites);
		                               DrawTrackFence_SW(ti, base_image, num_sprites); break;
		case RAIL_GROUND_FENCE_VERT1:  DrawTrackFence(ti, base_image, num_sprites, RFO_FLAT_LEFT);  break;
		case RAIL_GROUND_FENCE_VERT2:  DrawTrackFence(ti, base_image, num_sprites, RFO_FLAT_RIGHT); break;
		case RAIL_GROUND_FENCE_HORIZ1: DrawTrackFence(ti, base_image, num_sprites, RFO_FLAT_UPPER); break;
		case RAIL_GROUND_FENCE_HORIZ2: DrawTrackFence(ti, base_image, num_sprites, RFO_FLAT_LOWER); break;
		case RAIL_GROUND_WATER: {
			Corner track_corner;
			if (IsHalftileSlope(ti->tileh)) {
				/* Steep slope or one-corner-raised slope with halftile foundation */
				track_corner = GetHalftileSlopeCorner(ti->tileh);
			} else {
				/* Three-corner-raised slope */
				track_corner = OppositeCorner(GetHighestSlopeCorner(ComplementSlope(ti->tileh)));
			}
			switch (track_corner) {
				case CORNER_W: DrawTrackFence(ti, base_image, num_sprites, RFO_FLAT_LEFT);  break;
				case CORNER_S: DrawTrackFence(ti, base_image, num_sprites, RFO_FLAT_LOWER); break;
				case CORNER_E: DrawTrackFence(ti, base_image, num_sprites, RFO_FLAT_RIGHT); break;
				case CORNER_N: DrawTrackFence(ti, base_image, num_sprites, RFO_FLAT_UPPER); break;
				default: NOT_REACHED();
			}
			break;
		}
		default: break;
	}
}

/* SubSprite for drawing the track halftile of 'three-corners-raised'-sloped rail sprites. */
static const int INF = 1000; // big number compared to tilesprite size
static const SubSprite _halftile_sub_sprite[4] = {
	{ -INF    , -INF  , 32 - 33, INF     }, // CORNER_W, clip 33 pixels from right
	{ -INF    ,  0 + 7, INF    , INF     }, // CORNER_S, clip 7 pixels from top
	{ -31 + 33, -INF  , INF    , INF     }, // CORNER_E, clip 33 pixels from left
	{ -INF    , -INF  , INF    , 30 - 23 }  // CORNER_N, clip 23 pixels from bottom
};

static inline void DrawTrackSprite(SpriteID sprite, PaletteID pal, const TileInfo *ti, Slope s)
{
	DrawGroundSprite(sprite, pal, nullptr, 0, (ti->tileh & s) ? -8 : 0);
}

static void DrawTrackBitsOverlay(TileInfo *ti, TrackBits track, const RailtypeInfo *rti)
{
	RailGroundType rgt = GetRailGroundType(ti->tile);
	Foundation f = GetRailFoundation(ti->tileh, track);
	Corner halftile_corner = CORNER_INVALID;

	if (IsNonContinuousFoundation(f)) {
		/* Save halftile corner */
		halftile_corner = (f == FOUNDATION_STEEP_BOTH ? GetHighestSlopeCorner(ti->tileh) : GetHalftileFoundationCorner(f));
		/* Draw lower part first */
		track &= ~CornerToTrackBits(halftile_corner);
		f = (f == FOUNDATION_STEEP_BOTH ? FOUNDATION_STEEP_LOWER : FOUNDATION_NONE);
	}

	DrawFoundation(ti, f);
	/* DrawFoundation modifies ti */

	/* Draw ground */
	if (rgt == RAIL_GROUND_WATER) {
		if (track != TRACK_BIT_NONE || IsSteepSlope(ti->tileh)) {
			/* three-corner-raised slope or steep slope with track on upper part */
			DrawShoreTile(ti->tileh);
		} else {
			/* single-corner-raised slope with track on upper part */
			DrawGroundSprite(SPR_FLAT_WATER_TILE, PAL_NONE);
		}
	} else {
		SpriteID image;

		switch (rgt) {
			case RAIL_GROUND_BARREN:     image = SPR_FLAT_BARE_LAND;  break;
			case RAIL_GROUND_ICE_DESERT: image = SPR_FLAT_SNOW_DESERT_TILE; break;
			default:                     image = SPR_FLAT_GRASS_TILE; break;
		}

		image += SlopeToSpriteOffset(ti->tileh);

		DrawGroundSprite(image, PAL_NONE);
	}

	bool no_combine = ti->tileh == SLOPE_FLAT && HasBit(rti->flags, RTF_NO_SPRITE_COMBINE);
	SpriteID overlay = GetCustomRailSprite(rti, ti->tile, RTSG_OVERLAY);
	SpriteID ground = GetCustomRailSprite(rti, ti->tile, no_combine ? RTSG_GROUND_COMPLETE : RTSG_GROUND);
	TrackBits pbs = _settings_client.gui.show_track_reservation ? GetRailReservationTrackBits(ti->tile) : TRACK_BIT_NONE;

	if (track == TRACK_BIT_NONE) {
		/* Half-tile foundation, no track here? */
	} else if (no_combine) {
		/* Use trackbits as direct index from ground sprite, subtract 1
		 * because there is no sprite for no bits. */
		DrawGroundSprite(ground + track - 1, PAL_NONE);

		/* Draw reserved track bits */
		if (pbs & TRACK_BIT_X)     DrawGroundSprite(overlay + RTO_X, PALETTE_CRASH);
		if (pbs & TRACK_BIT_Y)     DrawGroundSprite(overlay + RTO_Y, PALETTE_CRASH);
		if (pbs & TRACK_BIT_UPPER) DrawTrackSprite(overlay + RTO_N, PALETTE_CRASH, ti, SLOPE_N);
		if (pbs & TRACK_BIT_LOWER) DrawTrackSprite(overlay + RTO_S, PALETTE_CRASH, ti, SLOPE_S);
		if (pbs & TRACK_BIT_RIGHT) DrawTrackSprite(overlay + RTO_E, PALETTE_CRASH, ti, SLOPE_E);
		if (pbs & TRACK_BIT_LEFT)  DrawTrackSprite(overlay + RTO_W, PALETTE_CRASH, ti, SLOPE_W);
	} else if (ti->tileh == SLOPE_NW && track == TRACK_BIT_Y) {
		DrawGroundSprite(ground + RTO_SLOPE_NW, PAL_NONE);
		if (pbs != TRACK_BIT_NONE) DrawGroundSprite(overlay + RTO_SLOPE_NW, PALETTE_CRASH);
	} else if (ti->tileh == SLOPE_NE && track == TRACK_BIT_X) {
		DrawGroundSprite(ground + RTO_SLOPE_NE, PAL_NONE);
		if (pbs != TRACK_BIT_NONE) DrawGroundSprite(overlay + RTO_SLOPE_NE, PALETTE_CRASH);
	} else if (ti->tileh == SLOPE_SE && track == TRACK_BIT_Y) {
		DrawGroundSprite(ground + RTO_SLOPE_SE, PAL_NONE);
		if (pbs != TRACK_BIT_NONE) DrawGroundSprite(overlay + RTO_SLOPE_SE, PALETTE_CRASH);
	} else if (ti->tileh == SLOPE_SW && track == TRACK_BIT_X) {
		DrawGroundSprite(ground + RTO_SLOPE_SW, PAL_NONE);
		if (pbs != TRACK_BIT_NONE) DrawGroundSprite(overlay + RTO_SLOPE_SW, PALETTE_CRASH);
	} else {
		switch (track) {
			/* Draw single ground sprite when not overlapping. No track overlay
			 * is necessary for these sprites. */
			case TRACK_BIT_X:     DrawGroundSprite(ground + RTO_X, PAL_NONE); break;
			case TRACK_BIT_Y:     DrawGroundSprite(ground + RTO_Y, PAL_NONE); break;
			case TRACK_BIT_UPPER: DrawTrackSprite(ground + RTO_N, PAL_NONE, ti, SLOPE_N); break;
			case TRACK_BIT_LOWER: DrawTrackSprite(ground + RTO_S, PAL_NONE, ti, SLOPE_S); break;
			case TRACK_BIT_RIGHT: DrawTrackSprite(ground + RTO_E, PAL_NONE, ti, SLOPE_E); break;
			case TRACK_BIT_LEFT:  DrawTrackSprite(ground + RTO_W, PAL_NONE, ti, SLOPE_W); break;
			case TRACK_BIT_CROSS: DrawGroundSprite(ground + RTO_CROSSING_XY, PAL_NONE); break;
			case TRACK_BIT_HORZ:  DrawTrackSprite(ground + RTO_N, PAL_NONE, ti, SLOPE_N);
			                      DrawTrackSprite(ground + RTO_S, PAL_NONE, ti, SLOPE_S); break;
			case TRACK_BIT_VERT:  DrawTrackSprite(ground + RTO_E, PAL_NONE, ti, SLOPE_E);
			                      DrawTrackSprite(ground + RTO_W, PAL_NONE, ti, SLOPE_W); break;

			default:
				/* We're drawing a junction tile */
				if ((track & TRACK_BIT_3WAY_NE) == 0) {
					DrawGroundSprite(ground + RTO_JUNCTION_SW, PAL_NONE);
				} else if ((track & TRACK_BIT_3WAY_SW) == 0) {
					DrawGroundSprite(ground + RTO_JUNCTION_NE, PAL_NONE);
				} else if ((track & TRACK_BIT_3WAY_NW) == 0) {
					DrawGroundSprite(ground + RTO_JUNCTION_SE, PAL_NONE);
				} else if ((track & TRACK_BIT_3WAY_SE) == 0) {
					DrawGroundSprite(ground + RTO_JUNCTION_NW, PAL_NONE);
				} else {
					DrawGroundSprite(ground + RTO_JUNCTION_NSEW, PAL_NONE);
				}

				/* Mask out PBS bits as we shall draw them afterwards anyway. */
				track &= ~pbs;

				/* Draw regular track bits */
				if (track & TRACK_BIT_X)     DrawGroundSprite(overlay + RTO_X, PAL_NONE);
				if (track & TRACK_BIT_Y)     DrawGroundSprite(overlay + RTO_Y, PAL_NONE);
				if (track & TRACK_BIT_UPPER) DrawGroundSprite(overlay + RTO_N, PAL_NONE);
				if (track & TRACK_BIT_LOWER) DrawGroundSprite(overlay + RTO_S, PAL_NONE);
				if (track & TRACK_BIT_RIGHT) DrawGroundSprite(overlay + RTO_E, PAL_NONE);
				if (track & TRACK_BIT_LEFT)  DrawGroundSprite(overlay + RTO_W, PAL_NONE);
		}

		/* Draw reserved track bits */
		if (pbs & TRACK_BIT_X)     DrawGroundSprite(overlay + RTO_X, PALETTE_CRASH);
		if (pbs & TRACK_BIT_Y)     DrawGroundSprite(overlay + RTO_Y, PALETTE_CRASH);
		if (pbs & TRACK_BIT_UPPER) DrawTrackSprite(overlay + RTO_N, PALETTE_CRASH, ti, SLOPE_N);
		if (pbs & TRACK_BIT_LOWER) DrawTrackSprite(overlay + RTO_S, PALETTE_CRASH, ti, SLOPE_S);
		if (pbs & TRACK_BIT_RIGHT) DrawTrackSprite(overlay + RTO_E, PALETTE_CRASH, ti, SLOPE_E);
		if (pbs & TRACK_BIT_LEFT)  DrawTrackSprite(overlay + RTO_W, PALETTE_CRASH, ti, SLOPE_W);
	}

	if (IsValidCorner(halftile_corner)) {
		DrawFoundation(ti, HalftileFoundation(halftile_corner));
		overlay = GetCustomRailSprite(rti, ti->tile, RTSG_OVERLAY, TCX_UPPER_HALFTILE);
		ground = GetCustomRailSprite(rti, ti->tile, RTSG_GROUND, TCX_UPPER_HALFTILE);

		/* Draw higher halftile-overlay: Use the sloped sprites with three corners raised. They probably best fit the lightning. */
		Slope fake_slope = SlopeWithThreeCornersRaised(OppositeCorner(halftile_corner));

		SpriteID image;
		switch (rgt) {
			case RAIL_GROUND_BARREN:     image = SPR_FLAT_BARE_LAND;  break;
			case RAIL_GROUND_ICE_DESERT:
			case RAIL_GROUND_HALF_SNOW:  image = SPR_FLAT_SNOW_DESERT_TILE; break;
			default:                     image = SPR_FLAT_GRASS_TILE; break;
		}

		image += SlopeToSpriteOffset(fake_slope);

		DrawGroundSprite(image, PAL_NONE, &(_halftile_sub_sprite[halftile_corner]));

		track = CornerToTrackBits(halftile_corner);

		int offset;
		switch (track) {
			default: NOT_REACHED();
			case TRACK_BIT_UPPER: offset = RTO_N; break;
			case TRACK_BIT_LOWER: offset = RTO_S; break;
			case TRACK_BIT_RIGHT: offset = RTO_E; break;
			case TRACK_BIT_LEFT:  offset = RTO_W; break;
		}

		DrawTrackSprite(ground + offset, PAL_NONE, ti, fake_slope);
		if (_settings_client.gui.show_track_reservation && HasReservedTracks(ti->tile, track)) {
			DrawTrackSprite(overlay + offset, PALETTE_CRASH, ti, fake_slope);
		}
	}
}

/**
 * Draw ground sprite and track bits
 * @param ti TileInfo
 * @param track TrackBits to draw
 */
static void DrawTrackBits(TileInfo *ti, TrackBits track)
{
	const RailtypeInfo *rti = GetRailTypeInfo(GetRailType(ti->tile));

	if (rti->UsesOverlay()) {
		DrawTrackBitsOverlay(ti, track, rti);
		return;
	}

	RailGroundType rgt = GetRailGroundType(ti->tile);
	Foundation f = GetRailFoundation(ti->tileh, track);
	Corner halftile_corner = CORNER_INVALID;

	if (IsNonContinuousFoundation(f)) {
		/* Save halftile corner */
		halftile_corner = (f == FOUNDATION_STEEP_BOTH ? GetHighestSlopeCorner(ti->tileh) : GetHalftileFoundationCorner(f));
		/* Draw lower part first */
		track &= ~CornerToTrackBits(halftile_corner);
		f = (f == FOUNDATION_STEEP_BOTH ? FOUNDATION_STEEP_LOWER : FOUNDATION_NONE);
	}

	DrawFoundation(ti, f);
	/* DrawFoundation modifies ti */

	SpriteID image;
	PaletteID pal = PAL_NONE;
	const SubSprite *sub = nullptr;
	bool junction = false;

	/* Select the sprite to use. */
	if (track == 0) {
		/* Clear ground (only track on halftile foundation) */
		if (rgt == RAIL_GROUND_WATER) {
			if (IsSteepSlope(ti->tileh)) {
				DrawShoreTile(ti->tileh);
				image = 0;
			} else {
				image = SPR_FLAT_WATER_TILE;
			}
		} else {
			switch (rgt) {
				case RAIL_GROUND_BARREN:     image = SPR_FLAT_BARE_LAND;  break;
				case RAIL_GROUND_ICE_DESERT: image = SPR_FLAT_SNOW_DESERT_TILE; break;
				default:                     image = SPR_FLAT_GRASS_TILE; break;
			}
			image += SlopeToSpriteOffset(ti->tileh);
		}
	} else {
		if (ti->tileh != SLOPE_FLAT) {
			/* track on non-flat ground */
			image = _track_sloped_sprites[ti->tileh - 1] + rti->base_sprites.track_y;
		} else {
			/* track on flat ground */
			switch (track) {
				/* single track, select combined track + ground sprite*/
				case TRACK_BIT_Y:     image = rti->base_sprites.track_y;     break;
				case TRACK_BIT_X:     image = rti->base_sprites.track_y + 1; break;
				case TRACK_BIT_UPPER: image = rti->base_sprites.track_y + 2; break;
				case TRACK_BIT_LOWER: image = rti->base_sprites.track_y + 3; break;
				case TRACK_BIT_RIGHT: image = rti->base_sprites.track_y + 4; break;
				case TRACK_BIT_LEFT:  image = rti->base_sprites.track_y + 5; break;
				case TRACK_BIT_CROSS: image = rti->base_sprites.track_y + 6; break;

				/* double diagonal track, select combined track + ground sprite*/
				case TRACK_BIT_HORZ:  image = rti->base_sprites.track_ns;     break;
				case TRACK_BIT_VERT:  image = rti->base_sprites.track_ns + 1; break;

				/* junction, select only ground sprite, handle track sprite later */
				default:
					junction = true;
					if ((track & TRACK_BIT_3WAY_NE) == 0) { image = rti->base_sprites.ground;     break; }
					if ((track & TRACK_BIT_3WAY_SW) == 0) { image = rti->base_sprites.ground + 1; break; }
					if ((track & TRACK_BIT_3WAY_NW) == 0) { image = rti->base_sprites.ground + 2; break; }
					if ((track & TRACK_BIT_3WAY_SE) == 0) { image = rti->base_sprites.ground + 3; break; }
					image = rti->base_sprites.ground + 4;
					break;
			}
		}

		switch (rgt) {
			case RAIL_GROUND_BARREN:     pal = PALETTE_TO_BARE_LAND; break;
			case RAIL_GROUND_ICE_DESERT: image += rti->snow_offset;  break;
			case RAIL_GROUND_WATER: {
				/* three-corner-raised slope */
				DrawShoreTile(ti->tileh);
				Corner track_corner = OppositeCorner(GetHighestSlopeCorner(ComplementSlope(ti->tileh)));
				sub = &(_halftile_sub_sprite[track_corner]);
				break;
			}
			default: break;
		}
	}

	if (image != 0) DrawGroundSprite(image, pal, sub);

	/* Draw track pieces individually for junction tiles */
	if (junction) {
		if (track & TRACK_BIT_X)     DrawGroundSprite(rti->base_sprites.single_x, PAL_NONE);
		if (track & TRACK_BIT_Y)     DrawGroundSprite(rti->base_sprites.single_y, PAL_NONE);
		if (track & TRACK_BIT_UPPER) DrawGroundSprite(rti->base_sprites.single_n, PAL_NONE);
		if (track & TRACK_BIT_LOWER) DrawGroundSprite(rti->base_sprites.single_s, PAL_NONE);
		if (track & TRACK_BIT_LEFT)  DrawGroundSprite(rti->base_sprites.single_w, PAL_NONE);
		if (track & TRACK_BIT_RIGHT) DrawGroundSprite(rti->base_sprites.single_e, PAL_NONE);
	}

	/* PBS debugging, draw reserved tracks darker */
	if (_game_mode != GM_MENU && _settings_client.gui.show_track_reservation) {
		/* Get reservation, but mask track on halftile slope */
		TrackBits pbs = GetRailReservationTrackBits(ti->tile) & track;
		if (pbs & TRACK_BIT_X) {
			if (ti->tileh == SLOPE_FLAT || ti->tileh == SLOPE_ELEVATED) {
				DrawGroundSprite(rti->base_sprites.single_x, PALETTE_CRASH);
			} else {
				DrawGroundSprite(_track_sloped_sprites[ti->tileh - 1] + rti->base_sprites.single_sloped - 20, PALETTE_CRASH);
			}
		}
		if (pbs & TRACK_BIT_Y) {
			if (ti->tileh == SLOPE_FLAT || ti->tileh == SLOPE_ELEVATED) {
				DrawGroundSprite(rti->base_sprites.single_y, PALETTE_CRASH);
			} else {
				DrawGroundSprite(_track_sloped_sprites[ti->tileh - 1] + rti->base_sprites.single_sloped - 20, PALETTE_CRASH);
			}
		}
		if (pbs & TRACK_BIT_UPPER) DrawGroundSprite(rti->base_sprites.single_n, PALETTE_CRASH, nullptr, 0, ti->tileh & SLOPE_N ? -(int)TILE_HEIGHT : 0);
		if (pbs & TRACK_BIT_LOWER) DrawGroundSprite(rti->base_sprites.single_s, PALETTE_CRASH, nullptr, 0, ti->tileh & SLOPE_S ? -(int)TILE_HEIGHT : 0);
		if (pbs & TRACK_BIT_LEFT)  DrawGroundSprite(rti->base_sprites.single_w, PALETTE_CRASH, nullptr, 0, ti->tileh & SLOPE_W ? -(int)TILE_HEIGHT : 0);
		if (pbs & TRACK_BIT_RIGHT) DrawGroundSprite(rti->base_sprites.single_e, PALETTE_CRASH, nullptr, 0, ti->tileh & SLOPE_E ? -(int)TILE_HEIGHT : 0);
	}

	if (IsValidCorner(halftile_corner)) {
		DrawFoundation(ti, HalftileFoundation(halftile_corner));

		/* Draw higher halftile-overlay: Use the sloped sprites with three corners raised. They probably best fit the lightning. */
		Slope fake_slope = SlopeWithThreeCornersRaised(OppositeCorner(halftile_corner));
		image = _track_sloped_sprites[fake_slope - 1] + rti->base_sprites.track_y;
		pal = PAL_NONE;
		switch (rgt) {
			case RAIL_GROUND_BARREN:     pal = PALETTE_TO_BARE_LAND; break;
			case RAIL_GROUND_ICE_DESERT:
			case RAIL_GROUND_HALF_SNOW:  image += rti->snow_offset;  break; // higher part has snow in this case too
			default: break;
		}
		DrawGroundSprite(image, pal, &(_halftile_sub_sprite[halftile_corner]));

		if (_game_mode != GM_MENU && _settings_client.gui.show_track_reservation && HasReservedTracks(ti->tile, CornerToTrackBits(halftile_corner))) {
			static const byte _corner_to_track_sprite[] = {3, 1, 2, 0};
			DrawGroundSprite(_corner_to_track_sprite[halftile_corner] + rti->base_sprites.single_n, PALETTE_CRASH, nullptr, 0, -(int)TILE_HEIGHT);
		}
	}
}

static void DrawSignals(TileIndex tile, TrackBits rails, const RailtypeInfo *rti)
{
#define MAYBE_DRAW_SIGNAL(x, y, z, t) if (IsSignalPresent(tile, x)) DrawSingleSignal(tile, rti, t, GetSingleSignalState(tile, x), y, z)

	if (!(rails & TRACK_BIT_Y)) {
		if (!(rails & TRACK_BIT_X)) {
			if (rails & TRACK_BIT_LEFT) {
				MAYBE_DRAW_SIGNAL(2, SIGNAL_TO_NORTH, 0, TRACK_LEFT);
				MAYBE_DRAW_SIGNAL(3, SIGNAL_TO_SOUTH, 1, TRACK_LEFT);
			}
			if (rails & TRACK_BIT_RIGHT) {
				MAYBE_DRAW_SIGNAL(0, SIGNAL_TO_NORTH, 2, TRACK_RIGHT);
				MAYBE_DRAW_SIGNAL(1, SIGNAL_TO_SOUTH, 3, TRACK_RIGHT);
			}
			if (rails & TRACK_BIT_UPPER) {
				MAYBE_DRAW_SIGNAL(3, SIGNAL_TO_WEST, 4, TRACK_UPPER);
				MAYBE_DRAW_SIGNAL(2, SIGNAL_TO_EAST, 5, TRACK_UPPER);
			}
			if (rails & TRACK_BIT_LOWER) {
				MAYBE_DRAW_SIGNAL(1, SIGNAL_TO_WEST, 6, TRACK_LOWER);
				MAYBE_DRAW_SIGNAL(0, SIGNAL_TO_EAST, 7, TRACK_LOWER);
			}
		} else {
			MAYBE_DRAW_SIGNAL(3, SIGNAL_TO_SOUTHWEST, 8, TRACK_X);
			MAYBE_DRAW_SIGNAL(2, SIGNAL_TO_NORTHEAST, 9, TRACK_X);
		}
	} else {
		MAYBE_DRAW_SIGNAL(3, SIGNAL_TO_SOUTHEAST, 10, TRACK_Y);
		MAYBE_DRAW_SIGNAL(2, SIGNAL_TO_NORTHWEST, 11, TRACK_Y);
	}
}

static void DrawTile_Track(TileInfo *ti)
{
	const RailtypeInfo *rti = GetRailTypeInfo(GetRailType(ti->tile));

	_drawtile_track_palette = COMPANY_SPRITE_COLOUR(GetTileOwner(ti->tile));

	if (IsPlainRail(ti->tile)) {
		TrackBits rails = GetTrackBits(ti->tile);

		DrawTrackBits(ti, rails);

		if (HasBit(_display_opt, DO_FULL_DETAIL)) DrawTrackDetails(ti, rti);

		if (HasRailCatenaryDrawn(GetRailType(ti->tile))) DrawRailCatenary(ti);

		if (HasSignals(ti->tile)) DrawSignals(ti->tile, rails, rti);
	} else {
		/* draw depot */
		const DrawTileSprites *dts;
		PaletteID pal = PAL_NONE;
		SpriteID relocation;

		if (ti->tileh != SLOPE_FLAT) DrawFoundation(ti, FOUNDATION_LEVELED);

		if (IsInvisibilitySet(TO_BUILDINGS)) {
			/* Draw rail instead of depot */
			dts = &_depot_invisible_gfx_table[GetRailDepotDirection(ti->tile)];
		} else {
			dts = &_depot_gfx_table[GetRailDepotDirection(ti->tile)];
		}

		SpriteID image;
		if (rti->UsesOverlay()) {
			image = SPR_FLAT_GRASS_TILE;
		} else {
			image = dts->ground.sprite;
			if (image != SPR_FLAT_GRASS_TILE) image += rti->GetRailtypeSpriteOffset();
		}

		/* Adjust ground tile for desert and snow. */
		if (IsSnowRailGround(ti->tile)) {
			if (image != SPR_FLAT_GRASS_TILE) {
				image += rti->snow_offset; // tile with tracks
			} else {
				image = SPR_FLAT_SNOW_DESERT_TILE; // flat ground
			}
		}

		DrawGroundSprite(image, GroundSpritePaletteTransform(image, pal, _drawtile_track_palette));

		if (rti->UsesOverlay()) {
			SpriteID ground = GetCustomRailSprite(rti, ti->tile, RTSG_GROUND);

			switch (GetRailDepotDirection(ti->tile)) {
				case DIAGDIR_NE:
					if (!IsInvisibilitySet(TO_BUILDINGS)) break;
					FALLTHROUGH;
				case DIAGDIR_SW:
					DrawGroundSprite(ground + RTO_X, PAL_NONE);
					break;
				case DIAGDIR_NW:
					if (!IsInvisibilitySet(TO_BUILDINGS)) break;
					FALLTHROUGH;
				case DIAGDIR_SE:
					DrawGroundSprite(ground + RTO_Y, PAL_NONE);
					break;
				default:
					break;
			}

			if (_settings_client.gui.show_track_reservation && HasDepotReservation(ti->tile)) {
				SpriteID overlay = GetCustomRailSprite(rti, ti->tile, RTSG_OVERLAY);

				switch (GetRailDepotDirection(ti->tile)) {
					case DIAGDIR_NE:
						if (!IsInvisibilitySet(TO_BUILDINGS)) break;
						FALLTHROUGH;
					case DIAGDIR_SW:
						DrawGroundSprite(overlay + RTO_X, PALETTE_CRASH);
						break;
					case DIAGDIR_NW:
						if (!IsInvisibilitySet(TO_BUILDINGS)) break;
						FALLTHROUGH;
					case DIAGDIR_SE:
						DrawGroundSprite(overlay + RTO_Y, PALETTE_CRASH);
						break;
					default:
						break;
				}
			}
		} else {
			/* PBS debugging, draw reserved tracks darker */
			if (_game_mode != GM_MENU && _settings_client.gui.show_track_reservation && HasDepotReservation(ti->tile)) {
				switch (GetRailDepotDirection(ti->tile)) {
					case DIAGDIR_NE:
						if (!IsInvisibilitySet(TO_BUILDINGS)) break;
						FALLTHROUGH;
					case DIAGDIR_SW:
						DrawGroundSprite(rti->base_sprites.single_x, PALETTE_CRASH);
						break;
					case DIAGDIR_NW:
						if (!IsInvisibilitySet(TO_BUILDINGS)) break;
						FALLTHROUGH;
					case DIAGDIR_SE:
						DrawGroundSprite(rti->base_sprites.single_y, PALETTE_CRASH);
						break;
					default:
						break;
				}
			}
		}
		int depot_sprite = GetCustomRailSprite(rti, ti->tile, RTSG_DEPOT);
		relocation = depot_sprite != 0 ? depot_sprite - SPR_RAIL_DEPOT_SE_1 : rti->GetRailtypeSpriteOffset();

		if (HasRailCatenaryDrawn(GetRailType(ti->tile))) DrawRailCatenary(ti);

		DrawRailTileSeq(ti, dts, TO_BUILDINGS, relocation, 0, _drawtile_track_palette);
	}
	DrawBridgeMiddle(ti);
}

void DrawTrainDepotSprite(int x, int y, int dir, RailType railtype)
{
	const DrawTileSprites *dts = &_depot_gfx_table[dir];
	const RailtypeInfo *rti = GetRailTypeInfo(railtype);
	SpriteID image = rti->UsesOverlay() ? SPR_FLAT_GRASS_TILE : dts->ground.sprite;
	uint32 offset = rti->GetRailtypeSpriteOffset();

	if (image != SPR_FLAT_GRASS_TILE) image += offset;
	PaletteID palette = COMPANY_SPRITE_COLOUR(_local_company);

	DrawSprite(image, PAL_NONE, x, y);

	if (rti->UsesOverlay()) {
		SpriteID ground = GetCustomRailSprite(rti, INVALID_TILE, RTSG_GROUND);

		switch (dir) {
			case DIAGDIR_SW: DrawSprite(ground + RTO_X, PAL_NONE, x, y); break;
			case DIAGDIR_SE: DrawSprite(ground + RTO_Y, PAL_NONE, x, y); break;
			default: break;
		}
	}
	int depot_sprite = GetCustomRailSprite(rti, INVALID_TILE, RTSG_DEPOT);
	if (depot_sprite != 0) offset = depot_sprite - SPR_RAIL_DEPOT_SE_1;

	DrawRailTileSeqInGUI(x, y, dts, offset, 0, palette);
}

static int GetSlopePixelZ_Track(TileIndex tile, uint x, uint y)
{
	if (IsPlainRail(tile)) {
		int z;
		Slope tileh = GetTilePixelSlope(tile, &z);
		if (tileh == SLOPE_FLAT) return z;

		z += ApplyPixelFoundationToSlope(GetRailFoundation(tileh, GetTrackBits(tile)), &tileh);
		return z + GetPartialPixelZ(x & 0xF, y & 0xF, tileh);
	} else {
		return GetTileMaxPixelZ(tile);
	}
}

static Foundation GetFoundation_Track(TileIndex tile, Slope tileh)
{
	return IsPlainRail(tile) ? GetRailFoundation(tileh, GetTrackBits(tile)) : FlatteningFoundation(tileh);
}

static void TileLoop_Track(TileIndex tile)
{
	RailGroundType old_ground = GetRailGroundType(tile);
	RailGroundType new_ground;

	if (old_ground == RAIL_GROUND_WATER) {
		TileLoop_Water(tile);
		return;
	}

	switch (_settings_game.game_creation.landscape) {
		case LT_ARCTIC: {
			int z;
			Slope slope = GetTileSlope(tile, &z);
			bool half = false;

			/* for non-flat track, use lower part of track
			 * in other cases, use the highest part with track */
			if (IsPlainRail(tile)) {
				TrackBits track = GetTrackBits(tile);
				Foundation f = GetRailFoundation(slope, track);

				switch (f) {
					case FOUNDATION_NONE:
						/* no foundation - is the track on the upper side of three corners raised tile? */
						if (IsSlopeWithThreeCornersRaised(slope)) z++;
						break;

					case FOUNDATION_INCLINED_X:
					case FOUNDATION_INCLINED_Y:
						/* sloped track - is it on a steep slope? */
						if (IsSteepSlope(slope)) z++;
						break;

					case FOUNDATION_STEEP_LOWER:
						/* only lower part of steep slope */
						z++;
						break;

					default:
						/* if it is a steep slope, then there is a track on higher part */
						if (IsSteepSlope(slope)) z++;
						z++;
						break;
				}

				half = IsInsideMM(f, FOUNDATION_STEEP_BOTH, FOUNDATION_HALFTILE_N + 1);
			} else {
				/* is the depot on a non-flat tile? */
				if (slope != SLOPE_FLAT) z++;
			}

			/* 'z' is now the lowest part of the highest track bit -
			 * for sloped track, it is 'z' of lower part
			 * for two track bits, it is 'z' of higher track bit
			 * For non-continuous foundations (and STEEP_BOTH), 'half' is set */
			if (z > GetSnowLine()) {
				if (half && z - GetSnowLine() == 1) {
					/* track on non-continuous foundation, lower part is not under snow */
					new_ground = RAIL_GROUND_HALF_SNOW;
				} else {
					new_ground = RAIL_GROUND_ICE_DESERT;
				}
				goto set_ground;
			}
			break;
			}

		case LT_TROPIC:
			if (GetTropicZone(tile) == TROPICZONE_DESERT) {
				new_ground = RAIL_GROUND_ICE_DESERT;
				goto set_ground;
			}
			break;
	}

	new_ground = RAIL_GROUND_GRASS;

	if (IsPlainRail(tile) && old_ground != RAIL_GROUND_BARREN) { // wait until bottom is green
		/* determine direction of fence */
		TrackBits rail = GetTrackBits(tile);

		Owner owner = GetTileOwner(tile);
		byte fences = 0;

		for (DiagDirection d = DIAGDIR_BEGIN; d < DIAGDIR_END; d++) {
			static const TrackBits dir_to_trackbits[DIAGDIR_END] = {TRACK_BIT_3WAY_NE, TRACK_BIT_3WAY_SE, TRACK_BIT_3WAY_SW, TRACK_BIT_3WAY_NW};

			/* Track bit on this edge => no fence. */
			if ((rail & dir_to_trackbits[d]) != TRACK_BIT_NONE) continue;

			TileIndex tile2 = tile + TileOffsByDiagDir(d);

			/* Show fences if it's a house, industry, object, road, tunnelbridge or not owned by us. */
			if (!IsValidTile(tile2) || IsTileType(tile2, MP_HOUSE) || IsTileType(tile2, MP_INDUSTRY) ||
					IsTileType(tile2, MP_ROAD) || (IsTileType(tile2, MP_OBJECT) && !IsObjectType(tile2, OBJECT_OWNED_LAND)) || IsTileType(tile2, MP_TUNNELBRIDGE) || !IsTileOwner(tile2, owner)) {
				fences |= 1 << d;
			}
		}

		switch (fences) {
			case 0: break;
			case (1 << DIAGDIR_NE): new_ground = RAIL_GROUND_FENCE_NE; break;
			case (1 << DIAGDIR_SE): new_ground = RAIL_GROUND_FENCE_SE; break;
			case (1 << DIAGDIR_SW): new_ground = RAIL_GROUND_FENCE_SW; break;
			case (1 << DIAGDIR_NW): new_ground = RAIL_GROUND_FENCE_NW; break;
			case (1 << DIAGDIR_NE) | (1 << DIAGDIR_SW): new_ground = RAIL_GROUND_FENCE_NESW; break;
			case (1 << DIAGDIR_SE) | (1 << DIAGDIR_NW): new_ground = RAIL_GROUND_FENCE_SENW; break;
			case (1 << DIAGDIR_NE) | (1 << DIAGDIR_SE): new_ground = RAIL_GROUND_FENCE_VERT1; break;
			case (1 << DIAGDIR_NE) | (1 << DIAGDIR_NW): new_ground = RAIL_GROUND_FENCE_HORIZ2; break;
			case (1 << DIAGDIR_SE) | (1 << DIAGDIR_SW): new_ground = RAIL_GROUND_FENCE_HORIZ1; break;
			case (1 << DIAGDIR_SW) | (1 << DIAGDIR_NW): new_ground = RAIL_GROUND_FENCE_VERT2; break;
			default: NOT_REACHED();
		}
	}

set_ground:
	if (old_ground != new_ground) {
		SetRailGroundType(tile, new_ground);
		MarkTileDirtyByTile(tile);
	}
}


static TrackStatus GetTileTrackStatus_Track(TileIndex tile, TransportType mode, uint sub_mode, DiagDirection side)
{
	/* Case of half tile slope with water. */
	if (mode == TRANSPORT_WATER && IsPlainRail(tile) && GetRailGroundType(tile) == RAIL_GROUND_WATER && IsSlopeWithOneCornerRaised(GetTileSlope(tile))) {
		TrackBits tb = GetTrackBits(tile);
		switch (tb) {
			default: NOT_REACHED();
			case TRACK_BIT_UPPER: tb = TRACK_BIT_LOWER; break;
			case TRACK_BIT_LOWER: tb = TRACK_BIT_UPPER; break;
			case TRACK_BIT_LEFT:  tb = TRACK_BIT_RIGHT; break;
			case TRACK_BIT_RIGHT: tb = TRACK_BIT_LEFT;  break;
		}
		return CombineTrackStatus(TrackBitsToTrackdirBits(tb), TRACKDIR_BIT_NONE);
	}

	if (mode != TRANSPORT_RAIL) return 0;

	TrackBits trackbits = TRACK_BIT_NONE;
	TrackdirBits red_signals = TRACKDIR_BIT_NONE;

	switch (GetRailTileType(tile)) {
		default: NOT_REACHED();
		case RAIL_TILE_NORMAL:
			trackbits = GetTrackBits(tile);
			break;

		case RAIL_TILE_SIGNALS: {
			trackbits = GetTrackBits(tile);
			byte a = GetPresentSignals(tile);
			uint b = GetSignalStates(tile);

			b &= a;

			/* When signals are not present (in neither direction),
			 * we pretend them to be green. Otherwise, it depends on
			 * the signal type. For signals that are only active from
			 * one side, we set the missing signals explicitly to
			 * `green'. Otherwise, they implicitly become `red'. */
			if (!IsOnewaySignal(tile, TRACK_UPPER) || (a & SignalOnTrack(TRACK_UPPER)) == 0) b |= ~a & SignalOnTrack(TRACK_UPPER);
			if (!IsOnewaySignal(tile, TRACK_LOWER) || (a & SignalOnTrack(TRACK_LOWER)) == 0) b |= ~a & SignalOnTrack(TRACK_LOWER);

			if ((b & 0x8) == 0) red_signals |= (TRACKDIR_BIT_LEFT_N | TRACKDIR_BIT_X_NE | TRACKDIR_BIT_Y_SE | TRACKDIR_BIT_UPPER_E);
			if ((b & 0x4) == 0) red_signals |= (TRACKDIR_BIT_LEFT_S | TRACKDIR_BIT_X_SW | TRACKDIR_BIT_Y_NW | TRACKDIR_BIT_UPPER_W);
			if ((b & 0x2) == 0) red_signals |= (TRACKDIR_BIT_RIGHT_N | TRACKDIR_BIT_LOWER_E);
			if ((b & 0x1) == 0) red_signals |= (TRACKDIR_BIT_RIGHT_S | TRACKDIR_BIT_LOWER_W);

			break;
		}

		case RAIL_TILE_DEPOT: {
			DiagDirection dir = GetRailDepotDirection(tile);

			if (side != INVALID_DIAGDIR && side != dir) break;

			trackbits = DiagDirToDiagTrackBits(dir);
			break;
		}
	}

	return CombineTrackStatus(TrackBitsToTrackdirBits(trackbits), red_signals);
}

static bool ClickTile_Track(TileIndex tile)
{
	if (!IsRailDepot(tile)) return false;

	ShowDepotWindow(tile, VEH_TRAIN);
	return true;
}

static void GetTileDesc_Track(TileIndex tile, TileDesc *td)
{
	const RailtypeInfo *rti = GetRailTypeInfo(GetRailType(tile));
	td->rail_speed = rti->max_speed;
	td->railtype = rti->strings.name;
	td->owner[0] = GetTileOwner(tile);
	switch (GetRailTileType(tile)) {
		case RAIL_TILE_NORMAL:
			td->str = STR_LAI_RAIL_DESCRIPTION_TRACK;
			break;

		case RAIL_TILE_SIGNALS: {
			static const StringID signal_type[6][6] = {
				{
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_NORMAL_SIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_NORMAL_PRESIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_NORMAL_EXITSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_NORMAL_COMBOSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_NORMAL_PBSSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_NORMAL_NOENTRYSIGNALS
				},
				{
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_NORMAL_PRESIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_PRESIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_PRE_EXITSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_PRE_COMBOSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_PRE_PBSSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_PRE_NOENTRYSIGNALS
				},
				{
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_NORMAL_EXITSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_PRE_EXITSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_EXITSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_EXIT_COMBOSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_EXIT_PBSSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_EXIT_NOENTRYSIGNALS
				},
				{
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_NORMAL_COMBOSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_PRE_COMBOSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_EXIT_COMBOSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_COMBOSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_COMBO_PBSSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_COMBO_NOENTRYSIGNALS
				},
				{
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_NORMAL_PBSSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_PRE_PBSSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_EXIT_PBSSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_COMBO_PBSSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_PBSSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_PBS_NOENTRYSIGNALS
				},
				{
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_NORMAL_NOENTRYSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_PRE_NOENTRYSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_EXIT_NOENTRYSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_COMBO_NOENTRYSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_PBS_NOENTRYSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_NOENTRYSIGNALS
				}
			};

			SignalType primary_signal;
			SignalType secondary_signal;
			if (HasSignalOnTrack(tile, TRACK_UPPER)) {
				primary_signal = GetSignalType(tile, TRACK_UPPER);
				secondary_signal = HasSignalOnTrack(tile, TRACK_LOWER) ? GetSignalType(tile, TRACK_LOWER) : primary_signal;
			} else {
				secondary_signal = primary_signal = GetSignalType(tile, TRACK_LOWER);
			}

			td->str = signal_type[secondary_signal][primary_signal];
			break;
		}

		case RAIL_TILE_DEPOT:
			td->str = STR_LAI_RAIL_DESCRIPTION_TRAIN_DEPOT;
			if (_settings_game.vehicle.train_acceleration_model != AM_ORIGINAL) {
				if (td->rail_speed > 0) {
					td->rail_speed = std::min<uint16>(td->rail_speed, 61);
				} else {
					td->rail_speed = 61;
				}
			}
			td->build_date = Depot::GetByTile(tile)->build_date;
			break;

		default:
			NOT_REACHED();
	}
}

static void ChangeTileOwner_Track(TileIndex tile, Owner old_owner, Owner new_owner)
{
	if (!IsTileOwner(tile, old_owner)) return;

	if (new_owner != INVALID_OWNER) {
		/* Update company infrastructure counts. No need to dirty windows here, we'll redraw the whole screen anyway. */
		uint num_pieces = 1;
		if (IsPlainRail(tile)) {
			TrackBits bits = GetTrackBits(tile);
			num_pieces = CountBits(bits);
			if (TracksOverlap(bits)) num_pieces *= num_pieces;
		}
		RailType rt = GetRailType(tile);
		Company::Get(old_owner)->infrastructure.rail[rt] -= num_pieces;
		Company::Get(new_owner)->infrastructure.rail[rt] += num_pieces;

		if (HasSignals(tile)) {
			uint num_sigs = CountBits(GetPresentSignals(tile));
			Company::Get(old_owner)->infrastructure.signal -= num_sigs;
			Company::Get(new_owner)->infrastructure.signal += num_sigs;
		}

		SetTileOwner(tile, new_owner);
	} else {
		DoCommand(tile, 0, 0, DC_EXEC | DC_BANKRUPT, CMD_LANDSCAPE_CLEAR);
	}
}

static const byte _fractcoords_behind[4] = { 0x8F, 0x8, 0x80, 0xF8 };
static const byte _fractcoords_enter[4] = { 0x8A, 0x48, 0x84, 0xA8 };
static const int8 _deltacoord_leaveoffset[8] = {
	-1,  0,  1,  0, /* x */
	 0,  1,  0, -1  /* y */
};


/**
 * Compute number of ticks when next wagon will leave a depot.
 * Negative means next wagon should have left depot n ticks before.
 * @param v vehicle outside (leaving) the depot
 * @return number of ticks when the next wagon will leave
 */
int TicksToLeaveDepot(const Train *v)
{
	DiagDirection dir = GetRailDepotDirection(v->tile);
	int length = v->CalcNextVehicleOffset();

	switch (dir) {
		case DIAGDIR_NE: return  ((int)(v->x_pos & 0x0F) - ((_fractcoords_enter[dir] & 0x0F) - (length + 1)));
		case DIAGDIR_SE: return -((int)(v->y_pos & 0x0F) - ((_fractcoords_enter[dir] >> 4)   + (length + 1)));
		case DIAGDIR_SW: return -((int)(v->x_pos & 0x0F) - ((_fractcoords_enter[dir] & 0x0F) + (length + 1)));
		case DIAGDIR_NW: return  ((int)(v->y_pos & 0x0F) - ((_fractcoords_enter[dir] >> 4)   - (length + 1)));
		default: NOT_REACHED();
	}
}

/**
 * Tile callback routine when vehicle enters tile
 * @see vehicle_enter_tile_proc
 */
static VehicleEnterTileStatus VehicleEnter_Track(Vehicle *u, TileIndex tile, int x, int y)
{
	/* This routine applies only to trains in depot tiles. */
	if (u->type != VEH_TRAIN || !IsRailDepotTile(tile)) return VETSB_CONTINUE;

	/* Depot direction. */
	DiagDirection dir = GetRailDepotDirection(tile);

	byte fract_coord = (x & 0xF) + ((y & 0xF) << 4);

	/* Make sure a train is not entering the tile from behind. */
	if (_fractcoords_behind[dir] == fract_coord) return VETSB_CANNOT_ENTER;

	Train *v = Train::From(u);

	/* Leaving depot? */
	if (v->direction == DiagDirToDir(dir)) {
		/* Calculate the point where the following wagon should be activated. */
		int length = v->CalcNextVehicleOffset();

		byte fract_coord_leave =
			((_fractcoords_enter[dir] & 0x0F) + // x
				(length + 1) * _deltacoord_leaveoffset[dir]) +
			(((_fractcoords_enter[dir] >> 4) +  // y
				((length + 1) * _deltacoord_leaveoffset[dir + 4])) << 4);

		if (fract_coord_leave == fract_coord) {
			/* Leave the depot. */
			if ((v = v->Next()) != nullptr) {
				v->vehstatus &= ~VS_HIDDEN;
				v->track = (DiagDirToAxis(dir) == AXIS_X ? TRACK_BIT_X : TRACK_BIT_Y);
			}
		}
	} else if (_fractcoords_enter[dir] == fract_coord) {
		/* Entering depot. */
		assert(DiagDirToDir(ReverseDiagDir(dir)) == v->direction);
		v->track = TRACK_BIT_DEPOT,
		v->vehstatus |= VS_HIDDEN;
		v->direction = ReverseDir(v->direction);
		if (v->Next() == nullptr) VehicleEnterDepot(v->First());
		v->tile = tile;

		InvalidateWindowData(WC_VEHICLE_DEPOT, GetDepotIndex(v->tile));
		return VETSB_ENTERED_WORMHOLE;
	}

	return VETSB_CONTINUE;
}

/**
 * Tests if autoslope is allowed.
 *
 * @param tile The tile.
 * @param flags Terraform command flags.
 * @param z_old Old TileZ.
 * @param tileh_old Old TileSlope.
 * @param z_new New TileZ.
 * @param tileh_new New TileSlope.
 * @param rail_bits Trackbits.
 */
static CommandCost TestAutoslopeOnRailTile(TileIndex tile, uint flags, int z_old, Slope tileh_old, int z_new, Slope tileh_new, TrackBits rail_bits)
{
	if (!_settings_game.construction.build_on_slopes || !AutoslopeEnabled()) return_cmd_error(STR_ERROR_MUST_REMOVE_RAILROAD_TRACK);

	/* Is the slope-rail_bits combination valid in general? I.e. is it safe to call GetRailFoundation() ? */
	if (CheckRailSlope(tileh_new, rail_bits, TRACK_BIT_NONE, tile).Failed()) return_cmd_error(STR_ERROR_MUST_REMOVE_RAILROAD_TRACK);

	/* Get the slopes on top of the foundations */
	z_old += ApplyFoundationToSlope(GetRailFoundation(tileh_old, rail_bits), &tileh_old);
	z_new += ApplyFoundationToSlope(GetRailFoundation(tileh_new, rail_bits), &tileh_new);

	Corner track_corner;
	switch (rail_bits) {
		case TRACK_BIT_LEFT:  track_corner = CORNER_W; break;
		case TRACK_BIT_LOWER: track_corner = CORNER_S; break;
		case TRACK_BIT_RIGHT: track_corner = CORNER_E; break;
		case TRACK_BIT_UPPER: track_corner = CORNER_N; break;

		/* Surface slope must not be changed */
		default:
			if (z_old != z_new || tileh_old != tileh_new) return_cmd_error(STR_ERROR_MUST_REMOVE_RAILROAD_TRACK);
			return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_FOUNDATION]);
	}

	/* The height of the track_corner must not be changed. The rest ensures GetRailFoundation() already. */
	z_old += GetSlopeZInCorner(RemoveHalftileSlope(tileh_old), track_corner);
	z_new += GetSlopeZInCorner(RemoveHalftileSlope(tileh_new), track_corner);
	if (z_old != z_new) return_cmd_error(STR_ERROR_MUST_REMOVE_RAILROAD_TRACK);

	CommandCost cost = CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_FOUNDATION]);
	/* Make the ground dirty, if surface slope has changed */
	if (tileh_old != tileh_new) {
		/* If there is flat water on the lower halftile add the cost for clearing it */
		if (GetRailGroundType(tile) == RAIL_GROUND_WATER && IsSlopeWithOneCornerRaised(tileh_old)) cost.AddCost(_price[PR_CLEAR_WATER]);
		if ((flags & DC_EXEC) != 0) SetRailGroundType(tile, RAIL_GROUND_BARREN);
	}
	return  cost;
}

/**
 * Test-procedure for HasVehicleOnPos to check for a ship.
 */
static Vehicle *EnsureNoShipProc(Vehicle *v, void *data)
{
	return v->type == VEH_SHIP ? v : nullptr;
}

static CommandCost TerraformTile_Track(TileIndex tile, DoCommandFlag flags, int z_new, Slope tileh_new)
{
	int z_old;
	Slope tileh_old = GetTileSlope(tile, &z_old);
	if (IsPlainRail(tile)) {
		TrackBits rail_bits = GetTrackBits(tile);
		/* Is there flat water on the lower halftile that must be cleared expensively? */
		bool was_water = (GetRailGroundType(tile) == RAIL_GROUND_WATER && IsSlopeWithOneCornerRaised(tileh_old));

		/* Allow clearing the water only if there is no ship */
		if (was_water && HasVehicleOnPos(tile, nullptr, &EnsureNoShipProc)) return_cmd_error(STR_ERROR_SHIP_IN_THE_WAY);

		/* First test autoslope. However if it succeeds we still have to test the rest, because non-autoslope terraforming is cheaper. */
		CommandCost autoslope_result = TestAutoslopeOnRailTile(tile, flags, z_old, tileh_old, z_new, tileh_new, rail_bits);

		/* When there is only a single horizontal/vertical track, one corner can be terraformed. */
		Corner allowed_corner;
		switch (rail_bits) {
			case TRACK_BIT_RIGHT: allowed_corner = CORNER_W; break;
			case TRACK_BIT_UPPER: allowed_corner = CORNER_S; break;
			case TRACK_BIT_LEFT:  allowed_corner = CORNER_E; break;
			case TRACK_BIT_LOWER: allowed_corner = CORNER_N; break;
			default: return autoslope_result;
		}

		Foundation f_old = GetRailFoundation(tileh_old, rail_bits);

		/* Do not allow terraforming if allowed_corner is part of anti-zig-zag foundations */
		if (tileh_old != SLOPE_NS && tileh_old != SLOPE_EW && IsSpecialRailFoundation(f_old)) return autoslope_result;

		/* Everything is valid, which only changes allowed_corner */
		for (Corner corner = (Corner)0; corner < CORNER_END; corner = (Corner)(corner + 1)) {
			if (allowed_corner == corner) continue;
			if (z_old + GetSlopeZInCorner(tileh_old, corner) != z_new + GetSlopeZInCorner(tileh_new, corner)) return autoslope_result;
		}

		/* Make the ground dirty */
		if ((flags & DC_EXEC) != 0) SetRailGroundType(tile, RAIL_GROUND_BARREN);

		/* allow terraforming */
		return CommandCost(EXPENSES_CONSTRUCTION, was_water ? _price[PR_CLEAR_WATER] : (Money)0);
	} else if (_settings_game.construction.build_on_slopes && AutoslopeEnabled() &&
			AutoslopeCheckForEntranceEdge(tile, z_new, tileh_new, GetRailDepotDirection(tile))) {
		return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_FOUNDATION]);
	}
	return DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
}


extern const TileTypeProcs _tile_type_rail_procs = {
	DrawTile_Track,           // draw_tile_proc
	GetSlopePixelZ_Track,     // get_slope_z_proc
	ClearTile_Track,          // clear_tile_proc
	nullptr,                     // add_accepted_cargo_proc
	GetTileDesc_Track,        // get_tile_desc_proc
	GetTileTrackStatus_Track, // get_tile_track_status_proc
	ClickTile_Track,          // click_tile_proc
	nullptr,                     // animate_tile_proc
	TileLoop_Track,           // tile_loop_proc
	ChangeTileOwner_Track,    // change_tile_owner_proc
	nullptr,                     // add_produced_cargo_proc
	VehicleEnter_Track,       // vehicle_enter_tile_proc
	GetFoundation_Track,      // get_foundation_proc
	TerraformTile_Track,      // terraform_tile_proc
};
