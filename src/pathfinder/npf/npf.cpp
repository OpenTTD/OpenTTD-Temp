/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file npf.cpp Implementation of the NPF pathfinder. */

#include "../../stdafx.h"
#include "../../debug.h"
#include "../../network/network.h"
#include "../../viewport_func.h"
#include "../../ship.h"
#include "../../roadstop_base.h"
#include "../../vehicle_func.h"
#include "../pathfinder_func.h"
#include "../pathfinder_type.h"
#include "../follow_track.hpp"
#include "aystar.h"
#include "../../platform_func.h"

#include "../../safeguards.h"

static const uint NPF_HASH_BITS = 12; ///< The size of the hash used in pathfinding. Just changing this value should be sufficient to change the hash size. Should be an even value.
/* Do no change below values */
static const uint NPF_HASH_SIZE = 1 << NPF_HASH_BITS;
static const uint NPF_HASH_HALFBITS = NPF_HASH_BITS / 2;
static const uint NPF_HASH_HALFMASK = (1 << NPF_HASH_HALFBITS) - 1;

/** Meant to be stored in AyStar.targetdata */
struct NPFFindStationOrTileData {
	TileIndex dest_coords;    ///< An indication of where the station is, for heuristic purposes, or the target tile
	StationID station_index;  ///< station index we're heading for, or INVALID_STATION if not heading to a station
	DepotID depot_index;      ///< depot index we're heading for, or INVALID_DEPOT if not heading to a depot
	bool reserve_path;        ///< Indicates whether the found path should be reserved
	StationType station_type; ///< The type of station we're heading for
	bool not_articulated;     ///< The (road) vehicle is not articulated
	const Vehicle *v;         ///< The vehicle we are pathfinding for
};

/** Indices into AyStar.userdata[] */
struct AyStarUserData {
	Owner owner;
	TransportType type;
	RailTypes railtypes;
	RoadTypes roadtypes;
	uint subtype;
};

/** Indices into AyStarNode.userdata[] */
enum AyStarNodeUserDataType {
	NPF_TRACKDIR_CHOICE = 0, ///< The trackdir chosen to get here
	NPF_NODE_FLAGS,
};

/** Flags for AyStarNode.userdata[NPF_NODE_FLAGS]. Use NPFSetFlag() and NPFGetFlag() to use them. */
enum NPFNodeFlag {
	NPF_FLAG_SEEN_SIGNAL,       ///< Used to mark that a signal was seen on the way, for rail only
	NPF_FLAG_2ND_SIGNAL,        ///< Used to mark that two signals were seen, rail only
	NPF_FLAG_3RD_SIGNAL,        ///< Used to mark that three signals were seen, rail only
	NPF_FLAG_REVERSE,           ///< Used to mark that this node was reached from the second start node, if applicable
	NPF_FLAG_LAST_SIGNAL_RED,   ///< Used to mark that the last signal on this path was red
	NPF_FLAG_LAST_SIGNAL_BLOCK, ///< Used to mark that the last signal on this path was a block signal
	NPF_FLAG_IGNORE_START_TILE, ///< Used to mark that the start tile is invalid, and searching should start from the second tile on
	NPF_FLAG_TARGET_RESERVED,   ///< Used to mark that the possible reservation target is already reserved
	NPF_FLAG_IGNORE_RESERVED,   ///< Used to mark that reserved tiles should be considered impassable
};

/** Meant to be stored in AyStar.userpath */
struct NPFFoundTargetData {
	uint best_bird_dist;    ///< The best heuristic found. Is 0 if the target was found
	uint best_path_dist;    ///< The shortest path. Is UINT_MAX if no path is found
	Trackdir best_trackdir; ///< The trackdir that leads to the shortest path/closest birds dist
	AyStarNode node;        ///< The node within the target the search led us to
	bool res_okay;          ///< True if a path reservation could be made
};

static AyStar _npf_aystar;

/* The cost of each trackdir. A diagonal piece is the full NPF_TILE_LENGTH,
 * the shorter piece is sqrt(2)/2*NPF_TILE_LENGTH =~ 0.7071
 */
#define NPF_STRAIGHT_LENGTH (uint)(NPF_TILE_LENGTH * STRAIGHT_TRACK_LENGTH)
static const uint _trackdir_length[TRACKDIR_END] = {
	NPF_TILE_LENGTH, NPF_TILE_LENGTH, NPF_STRAIGHT_LENGTH, NPF_STRAIGHT_LENGTH, NPF_STRAIGHT_LENGTH, NPF_STRAIGHT_LENGTH,
	0, 0,
	NPF_TILE_LENGTH, NPF_TILE_LENGTH, NPF_STRAIGHT_LENGTH, NPF_STRAIGHT_LENGTH, NPF_STRAIGHT_LENGTH, NPF_STRAIGHT_LENGTH
};

/**
 * Returns the current value of the given flag on the given AyStarNode.
 */
static inline bool NPFGetFlag(const AyStarNode *node, NPFNodeFlag flag)
{
	return HasBit(node->user_data[NPF_NODE_FLAGS], flag);
}

/**
 * Sets the given flag on the given AyStarNode to the given value.
 */
static inline void NPFSetFlag(AyStarNode *node, NPFNodeFlag flag, bool value)
{
	SB(node->user_data[NPF_NODE_FLAGS], flag, 1, value);
}

bool CheckIgnoreFirstTile(const PathNode *node)
{
	return (node->parent == nullptr && HasBit(node->node.user_data[NPF_NODE_FLAGS], NPF_FLAG_IGNORE_START_TILE));
}

/**
 * Calculates the minimum distance travelled to get from t0 to t1 when only
 * using tracks (ie, only making 45 degree turns). Returns the distance in the
 * NPF scale, ie the number of full tiles multiplied by NPF_TILE_LENGTH to
 * prevent rounding.
 */
static uint NPFDistanceTrack(TileIndex t0, TileIndex t1)
{
	const uint dx = Delta(TileX(t0), TileX(t1));
	const uint dy = Delta(TileY(t0), TileY(t1));

	const uint straightTracks = 2 * std::min(dx, dy); // The number of straight (not full length) tracks
	/* OPTIMISATION:
	 * Original: diagTracks = max(dx, dy) - min(dx,dy);
	 * Proof:
	 * (dx+dy) - straightTracks  == (min + max) - straightTracks = min + max - 2 * min = max - min */
	const uint diagTracks = dx + dy - straightTracks; // The number of diagonal (full tile length) tracks.

	/* Don't factor out NPF_TILE_LENGTH below, this will round values and lose
	 * precision */
	return diagTracks * NPF_TILE_LENGTH + straightTracks * NPF_TILE_LENGTH * STRAIGHT_TRACK_LENGTH;
}

/**
 * Calculates a hash value for use in the NPF.
 * @param key1 The TileIndex of the tile to hash
 * @param key2 The Trackdir of the track on the tile.
 *
 * @todo Think of a better hash.
 */
static uint NPFHash(uint key1, uint key2)
{
	/* TODO: think of a better hash? */
	uint part1 = TileX(key1) & NPF_HASH_HALFMASK;
	uint part2 = TileY(key1) & NPF_HASH_HALFMASK;

	assert(IsValidTrackdir((Trackdir)key2));
	assert(IsValidTile(key1));
	return ((part1 << NPF_HASH_HALFBITS | part2) + (NPF_HASH_SIZE * key2 / TRACKDIR_END)) % NPF_HASH_SIZE;
}

static int32 NPFCalcZero(AyStar *as, AyStarNode *current, OpenListNode *parent)
{
	return 0;
}

/* Calculates the heuristic to the target station or tile. For train stations, it
 * takes into account the direction of approach.
 */
static int32 NPFCalcStationOrTileHeuristic(AyStar *as, AyStarNode *current, OpenListNode *parent)
{
	NPFFindStationOrTileData *fstd = (NPFFindStationOrTileData*)as->user_target;
	NPFFoundTargetData *ftd = (NPFFoundTargetData*)as->user_path;
	TileIndex from = current->tile;
	TileIndex to = fstd->dest_coords;
	uint dist;
	AyStarUserData *user = (AyStarUserData *)as->user_data;

	/* aim for the closest station/depot tile */
	if (fstd->station_index != INVALID_STATION) {
		to = CalcClosestStationTile(fstd->station_index, from, fstd->station_type);
	} else if (fstd->depot_index != INVALID_DEPOT) {
		to = CalcClosestDepotTile(fstd->depot_index, from);
	}

	if (user->type == TRANSPORT_ROAD) {
		/* Since roads only have diagonal pieces, we use manhattan distance here */
		dist = DistanceManhattan(from, to) * NPF_TILE_LENGTH;
	} else {
		/* Ships and trains can also go diagonal, so the minimum distance is shorter */
		dist = NPFDistanceTrack(from, to);
	}

	Debug(npf, 4, "Calculating H for: ({}, {}). Result: {}", TileX(current->tile), TileY(current->tile), dist);

	if (dist < ftd->best_bird_dist) {
		ftd->best_bird_dist = dist;
		ftd->best_trackdir = (Trackdir)current->user_data[NPF_TRACKDIR_CHOICE];
	}
	return dist;
}


/* Fills AyStarNode.user_data[NPF_TRACKDIRCHOICE] with the chosen direction to
 * get here, either getting it from the current choice or from the parent's
 * choice */
static void NPFFillTrackdirChoice(AyStarNode *current, OpenListNode *parent)
{
	if (parent->path.parent == nullptr) {
		Trackdir trackdir = current->direction;
		/* This is a first order decision, so we'd better save the
		 * direction we chose */
		current->user_data[NPF_TRACKDIR_CHOICE] = trackdir;
		Debug(npf, 6, "Saving trackdir: 0x{:X}", trackdir);
	} else {
		/* We've already made the decision, so just save our parent's decision */
		current->user_data[NPF_TRACKDIR_CHOICE] = parent->path.node.user_data[NPF_TRACKDIR_CHOICE];
	}
}

/* Will return the cost of the tunnel. If it is an entry, it will return the
 * cost of that tile. If the tile is an exit, it will return the tunnel length
 * including the exit tile. Requires that this is a Tunnel tile */
static uint NPFTunnelCost(AyStarNode *current)
{
	DiagDirection exitdir = TrackdirToExitdir(current->direction);
	TileIndex tile = current->tile;
	if (GetTunnelBridgeDirection(tile) == ReverseDiagDir(exitdir)) {
		/* We just popped out if this tunnel, since were
		 * facing the tunnel exit */
		return NPF_TILE_LENGTH * (GetTunnelBridgeLength(current->tile, GetOtherTunnelEnd(current->tile)) + 1);
		/* @todo: Penalty for tunnels? */
	} else {
		/* We are entering the tunnel, the enter tile is just a
		 * straight track */
		return NPF_TILE_LENGTH;
	}
}

static inline uint NPFBridgeCost(AyStarNode *current)
{
	return NPF_TILE_LENGTH * GetTunnelBridgeLength(current->tile, GetOtherBridgeEnd(current->tile));
}

static uint NPFSlopeCost(AyStarNode *current)
{
	TileIndex next = current->tile + TileOffsByDiagDir(TrackdirToExitdir(current->direction));

	/* Get center of tiles */
	int x1 = TileX(current->tile) * TILE_SIZE + TILE_SIZE / 2;
	int y1 = TileY(current->tile) * TILE_SIZE + TILE_SIZE / 2;
	int x2 = TileX(next) * TILE_SIZE + TILE_SIZE / 2;
	int y2 = TileY(next) * TILE_SIZE + TILE_SIZE / 2;

	int dx4 = (x2 - x1) / 4;
	int dy4 = (y2 - y1) / 4;

	/* Get the height on both sides of the tile edge.
	 * Avoid testing the height on the tile-center. This will fail for halftile-foundations.
	 */
	int z1 = GetSlopePixelZ(x1 + dx4, y1 + dy4);
	int z2 = GetSlopePixelZ(x2 - dx4, y2 - dy4);

	if (z2 - z1 > 1) {
		/* Slope up */
		return _settings_game.pf.npf.npf_rail_slope_penalty;
	}
	return 0;
	/* Should we give a bonus for slope down? Probably not, we
	 * could just subtract that bonus from the penalty, because
	 * there is only one level of steepness... */
}

static uint NPFReservedTrackCost(AyStarNode *current)
{
	TileIndex tile = current->tile;
	TrackBits track = TrackToTrackBits(TrackdirToTrack(current->direction));
	TrackBits res = GetReservedTrackbits(tile);

	if (NPFGetFlag(current, NPF_FLAG_3RD_SIGNAL) || NPFGetFlag(current, NPF_FLAG_LAST_SIGNAL_BLOCK) || ((res & track) == TRACK_BIT_NONE && !TracksOverlap(res | track))) return 0;

	if (IsTileType(tile, MP_TUNNELBRIDGE)) {
		DiagDirection exitdir = TrackdirToExitdir(current->direction);
		if (GetTunnelBridgeDirection(tile) == ReverseDiagDir(exitdir)) {
			return  _settings_game.pf.npf.npf_rail_pbs_cross_penalty * (GetTunnelBridgeLength(tile, GetOtherTunnelBridgeEnd(tile)) + 1);
		}
	}
	return  _settings_game.pf.npf.npf_rail_pbs_cross_penalty;
}

/**
 * Mark tiles by mowing the grass when npf debug level >= 1.
 * Will not work for multiplayer games, since it can (will) cause desyncs.
 */
static void NPFMarkTile(TileIndex tile)
{
	if (_debug_npf_level < 1 || _networking) return;
	switch (GetTileType(tile)) {
		case MP_RAILWAY:
			/* DEBUG: mark visited tiles by mowing the grass under them ;-) */
			if (!IsRailDepot(tile)) {
				SetRailGroundType(tile, RAIL_GROUND_BARREN);
				MarkTileDirtyByTile(tile);
			}
			break;

		case MP_ROAD:
			if (!IsRoadDepot(tile)) {
				SetRoadside(tile, ROADSIDE_BARREN);
				MarkTileDirtyByTile(tile);
			}
			break;

		default:
			break;
	}
}

static Vehicle *CountShipProc(Vehicle *v, void *data)
{
	uint *count = (uint *)data;
	/* Ignore other vehicles (aircraft) and ships inside depot. */
	if (v->type == VEH_SHIP && (v->vehstatus & VS_HIDDEN) == 0) (*count)++;

	return nullptr;
}

static int32 NPFWaterPathCost(AyStar *as, AyStarNode *current, OpenListNode *parent)
{
	/* TileIndex tile = current->tile; */
	int32 cost = 0;
	Trackdir trackdir = current->direction;

	cost = _trackdir_length[trackdir]; // Should be different for diagonal tracks

	switch (GetTileType(current->tile)) {
		default: NOT_REACHED();

		case MP_STATION:
			if (IsBuoy(current->tile) && IsDiagonalTrackdir(trackdir)) cost += _settings_game.pf.npf.npf_buoy_penalty;
			break;

		case MP_WATER:
			if (IsShipDepot(current->tile)) {
				if (IsBigDepot(current->tile) && IsDepotFullWithStoppedVehicles(current->tile)) cost += NPF_INFINITE_PENALTY;
			}
			break;
	}

	if (current->direction != NextTrackdir((Trackdir)parent->path.node.direction)) {
		cost += _settings_game.pf.npf.npf_water_curve_penalty;
	}

	if (IsDockingTile(current->tile)) {
		/* Check docking tile for occupancy */
		uint count = 1;
		HasVehicleOnPos(current->tile, &count, &CountShipProc);
		cost += count * 3 * _trackdir_length[trackdir];
	}

	/* @todo More penalties? */

	return cost;
}

/* Determine the cost of this node, for road tracks */
static int32 NPFRoadPathCost(AyStar *as, AyStarNode *current, OpenListNode *parent)
{
	TileIndex tile = current->tile;
	int32 cost = 0;

	/* Determine base length */
	switch (GetTileType(tile)) {
		case MP_TUNNELBRIDGE:
			cost = IsTunnel(tile) ? NPFTunnelCost(current) : NPFBridgeCost(current);
			break;

		case MP_ROAD:
			cost = NPF_TILE_LENGTH;
			/* Increase the cost for level crossings */
			if (IsLevelCrossing(tile)) {
				cost += _settings_game.pf.npf.npf_crossing_penalty;
			} else if (IsRoadDepot(tile) && IsBigRoadDepot(tile)) {
				switch (GetDepotReservation(tile, current->direction)) {
					case DEPOT_RESERVATION_FULL_STOPPED_VEH:
						cost += 32 * NPF_TILE_LENGTH;
						break;
					case DEPOT_RESERVATION_IN_USE:
						cost += 8 * NPF_TILE_LENGTH;
						break;
					case DEPOT_RESERVATION_EMPTY:
						cost += NPF_TILE_LENGTH;
						break;
					default: NOT_REACHED();
				}
			}
			break;

		case MP_STATION: {
			cost = NPF_TILE_LENGTH;
			const RoadStop *rs = RoadStop::GetByTile(tile, GetRoadStopType(tile));
			if (IsDriveThroughStopTile(tile)) {
				/* Increase the cost for drive-through road stops */
				cost += _settings_game.pf.npf.npf_road_drive_through_penalty;
				DiagDirection dir = TrackdirToExitdir(current->direction);
				if (!RoadStop::IsDriveThroughRoadStopContinuation(tile, tile - TileOffsByDiagDir(dir))) {
					/* When we're the first road stop in a 'queue' of them we increase
					 * cost based on the fill percentage of the whole queue. */
					const RoadStop::Entry *entry = rs->GetEntry(dir);
					cost += entry->GetOccupied() * _settings_game.pf.npf.npf_road_dt_occupied_penalty / entry->GetLength();
				}
			} else {
				/* Increase cost for filled road stops */
				cost += _settings_game.pf.npf.npf_road_bay_occupied_penalty * (!rs->IsFreeBay(0) + !rs->IsFreeBay(1)) / 2;
			}
			break;
		}

		default:
			break;
	}

	/* Determine extra costs */

	/* Check for slope */
	cost += NPFSlopeCost(current);

	/* Check for turns. Road vehicles only really drive diagonal, turns are
	 * represented by non-diagonal tracks */
	if (!IsDiagonalTrackdir(current->direction)) {
		cost += _settings_game.pf.npf.npf_road_curve_penalty;
	}

	NPFMarkTile(tile);
	Debug(npf, 4, "Calculating G for: ({}, {}). Result: {}", TileX(current->tile), TileY(current->tile), cost);
	return cost;
}

/* Return a small penalty for longer platform than needed and a bigger penalty for a too short one. */
inline int PlatformLengthPenalty(NPFFindStationOrTileData *fstd, int platform_length)
{
	const Train *v = Train::From(fstd->v);
	assert(v != nullptr);
	assert(v->type == VEH_TRAIN);
	assert(v->gcache.cached_total_length != 0);

	int missing_platform_length = CeilDiv(v->gcache.cached_total_length, TILE_SIZE) - platform_length;

	return _settings_game.pf.npf.npf_rail_station_penalty * (missing_platform_length < 0 ? missing_platform_length : 64);
}


/* Determine the cost of this node, for railway tracks */
static int32 NPFRailPathCost(AyStar *as, AyStarNode *current, OpenListNode *parent)
{
	TileIndex tile = current->tile;
	Trackdir trackdir = current->direction;
	int32 cost = 0;
	/* HACK: We create a OpenListNode manually, so we can call EndNodeCheck */
	OpenListNode new_node;

	/* Determine base length */
	switch (GetTileType(tile)) {
		case MP_TUNNELBRIDGE:
			cost = IsTunnel(tile) ? NPFTunnelCost(current) : NPFBridgeCost(current);
			break;

		case MP_RAILWAY:
			cost = _trackdir_length[trackdir]; // Should be different for diagonal tracks
			if (IsBigRailDepotTile(tile)) {
				DepotReservation depot_reservation = GetDepotReservation(tile);
				if (depot_reservation == DEPOT_RESERVATION_FULL_STOPPED_VEH) {
					cost += NPF_INFINITE_PENALTY;
				} else {
					uint platform_length = GetPlatformLength(tile, TrackdirToExitdir(trackdir));
					cost += (HasDepotReservation(tile) ? 2 : 1) * platform_length * _settings_game.pf.npf.npf_rail_station_penalty;

					/* Add additional length penalties if train has to visit this depot. */
					NPFFindStationOrTileData *fstd = (NPFFindStationOrTileData*)as->user_target;
					if (fstd->v->current_order.IsType(OT_GOTO_DEPOT) &&
							GetDepotIndex(tile) == fstd->v->current_order.GetDestination() &&
							IsAnyStartPlatformTile(tile)) {
						cost += PlatformLengthPenalty(fstd, platform_length);
					}
				}
			}
			break;

		case MP_ROAD: // Railway crossing
			cost = NPF_TILE_LENGTH;
			break;

		case MP_STATION:
			/* We give a station tile a penalty. Logically we would only want to give
			 * station tiles that are not our destination this penalty. This would
			 * discourage trains to drive through busy stations. But, we can just
			 * give any station tile a penalty, because every possible route will get
			 * this penalty exactly once, on its end tile (if it's a station) and it
			 * will therefore not make a difference. */
			cost = NPF_TILE_LENGTH + _settings_game.pf.npf.npf_rail_station_penalty;

			if (IsRailWaypoint(tile)) {
				NPFFindStationOrTileData *fstd = (NPFFindStationOrTileData*)as->user_target;
				if (fstd->v->current_order.IsType(OT_GOTO_WAYPOINT) && GetStationIndex(tile) == fstd->v->current_order.GetDestination()) {
					/* This waypoint is our destination; maybe this isn't an unreserved
					 * one, so check that and if so see that as the last signal being
					 * red. This way waypoints near stations should work better. */
					const Train *train = Train::From(fstd->v);
					CFollowTrackRail ft(train);
					TileIndex t = tile;
					Trackdir td = trackdir;
					while (ft.Follow(t, td)) {
						assert(t != ft.m_new_tile);
						t = ft.m_new_tile;
						if (KillFirstBit(ft.m_new_td_bits) != TRACKDIR_BIT_NONE) {
							/* We encountered a junction; it's going to be too complex to
							 * handle this perfectly, so just bail out. There is no simple
							 * free path, so try the other possibilities. */
							td = INVALID_TRACKDIR;
							break;
						}
						td = RemoveFirstTrackdir(&ft.m_new_td_bits);
						/* If this is a safe waiting position we're done searching for it */
						if (IsSafeWaitingPosition(train, t, td, true, _settings_game.pf.forbid_90_deg)) break;
					}
					if (td == INVALID_TRACKDIR ||
							!IsSafeWaitingPosition(train, t, td, true, _settings_game.pf.forbid_90_deg) ||
							!IsWaitingPositionFree(train, t, td, _settings_game.pf.forbid_90_deg)) {
						cost += _settings_game.pf.npf.npf_rail_lastred_penalty;
					}
				}
			}
			break;

		default:
			break;
	}

	/* Determine extra costs */

	/* Check for signals */
	if (IsTileType(tile, MP_RAILWAY)) {
		if (HasSignalOnTrackdir(tile, trackdir)) {
			SignalType sigtype = GetSignalType(tile, TrackdirToTrack(trackdir));
			/* Ordinary track with signals */
			if (GetSignalStateByTrackdir(tile, trackdir) == SIGNAL_STATE_RED) {
				/* Signal facing us is red */
				if (!NPFGetFlag(current, NPF_FLAG_SEEN_SIGNAL)) {
					/* Penalize the first signal we
					 * encounter, if it is red */

					/* Is this a presignal exit or combo? */
					if (!IsPbsSignal(sigtype)) {
						if (sigtype == SIGTYPE_EXIT || sigtype == SIGTYPE_COMBO) {
							/* Penalise exit and combo signals differently (heavier) */
							cost += _settings_game.pf.npf.npf_rail_firstred_exit_penalty;
						} else {
							cost += _settings_game.pf.npf.npf_rail_firstred_penalty;
						}
					}
				}
				/* Record the state of this signal. Path signals are assumed to
				 * be green as the signal state of them has no meaning for this. */
				NPFSetFlag(current, NPF_FLAG_LAST_SIGNAL_RED, !IsPbsSignal(sigtype));
			} else {
				/* Record the state of this signal */
				NPFSetFlag(current, NPF_FLAG_LAST_SIGNAL_RED, false);
			}
			if (NPFGetFlag(current, NPF_FLAG_SEEN_SIGNAL)) {
				if (NPFGetFlag(current, NPF_FLAG_2ND_SIGNAL)) {
					NPFSetFlag(current, NPF_FLAG_3RD_SIGNAL, true);
				} else {
					NPFSetFlag(current, NPF_FLAG_2ND_SIGNAL, true);
				}
			} else {
				NPFSetFlag(current, NPF_FLAG_SEEN_SIGNAL, true);
			}
			NPFSetFlag(current, NPF_FLAG_LAST_SIGNAL_BLOCK, !IsPbsSignal(sigtype));
		}

		if (HasPbsSignalOnTrackdir(tile, ReverseTrackdir(trackdir)) && !NPFGetFlag(current, NPF_FLAG_3RD_SIGNAL)) {
			cost += _settings_game.pf.npf.npf_rail_pbs_signal_back_penalty;
		}
	}

	/* Penalise the tile if it is a target tile and the last signal was
	 * red */
	/* HACK: We create a new_node here so we can call EndNodeCheck. Ugly as hell
	 * of course... */
	new_node.path.node = *current;
	if (as->EndNodeCheck(as, &new_node) == AYSTAR_FOUND_END_NODE && NPFGetFlag(current, NPF_FLAG_LAST_SIGNAL_RED)) {
		cost += _settings_game.pf.npf.npf_rail_lastred_penalty;
	}

	/* Check for slope */
	cost += NPFSlopeCost(current);

	/* Check for turns */
	if (current->direction != NextTrackdir((Trackdir)parent->path.node.direction)) {
		cost += _settings_game.pf.npf.npf_rail_curve_penalty;
	}
	/* TODO, with realistic acceleration, also the amount of straight track between
	 *      curves should be taken into account, as this affects the speed limit. */

	/* Check for reverse in depot */
	if (IsSmallRailDepotTile(tile) && as->EndNodeCheck(as, &new_node) != AYSTAR_FOUND_END_NODE) {
		/* Penalise any depot tile that is not the last tile in the path. This
		 * _should_ penalise every occurrence of reversing in a depot (and only
		 * that) */
		cost += _settings_game.pf.npf.npf_rail_depot_reverse_penalty;
	}

	/* Check for occupied track */
	cost += NPFReservedTrackCost(current);

	NPFMarkTile(tile);
	Debug(npf, 4, "Calculating G for: ({}, {}). Result: {}", TileX(current->tile), TileY(current->tile), cost);
	return cost;
}

/* Will find any depot */
static int32 NPFFindDepot(const AyStar *as, const OpenListNode *current)
{
	AyStarUserData *user = (AyStarUserData *)as->user_data;
	/* It's not worth caching the result with NPF_FLAG_IS_TARGET here as below,
	 * since checking the cache not that much faster than the actual check */
	return IsDepotTypeTile(current->path.node.tile, user->type) ?
		AYSTAR_FOUND_END_NODE : AYSTAR_DONE;
}

/** Find any safe and free tile. */
static int32 NPFFindSafeTile(const AyStar *as, const OpenListNode *current)
{
	const Train *v = Train::From(((NPFFindStationOrTileData *)as->user_target)->v);

	return (IsSafeWaitingPosition(v, current->path.node.tile, current->path.node.direction, true, _settings_game.pf.forbid_90_deg) &&
			IsWaitingPositionFree(v, current->path.node.tile, current->path.node.direction, _settings_game.pf.forbid_90_deg)) ?
				AYSTAR_FOUND_END_NODE : AYSTAR_DONE;
}

/* Will find a station identified using the NPFFindStationOrTileData */
static int32 NPFFindStationOrTile(const AyStar *as, const OpenListNode *current)
{
	NPFFindStationOrTileData *fstd = (NPFFindStationOrTileData*)as->user_target;
	const AyStarUserData *user = (AyStarUserData *)as->user_data;
	const AyStarNode *node = &current->path.node;
	TileIndex tile = node->tile;

	if (fstd->station_index == INVALID_STATION && fstd->depot_index == INVALID_DEPOT &&
			tile == fstd->dest_coords) {
		return AYSTAR_FOUND_END_NODE;
	}

	if (fstd->v->type == VEH_SHIP) {
		/* Ships do not actually reach the destination station, so we check for a docking tile instead. */
		if (IsDockingTile(tile) && IsShipDestinationTile(tile, fstd->station_index)) return AYSTAR_FOUND_END_NODE;
		if (IsDepotTypeTile(tile, user->type) && GetDepotIndex(tile) == fstd->depot_index) return AYSTAR_FOUND_END_NODE;
		return AYSTAR_DONE;
	}

	if (IsTileType(tile, MP_STATION) && GetStationIndex(tile) == fstd->station_index) {
		if (fstd->v->type == VEH_TRAIN) return AYSTAR_FOUND_END_NODE;

		assert(fstd->v->type == VEH_ROAD);
		assert(user->type == TRANSPORT_ROAD);
		/* Only if it is a valid station *and* we can stop there */
		if (GetStationType(tile) == fstd->station_type &&
				(fstd->not_articulated || IsDriveThroughStopTile(tile)))
			return AYSTAR_FOUND_END_NODE;
	} else if (IsDepotTypeTile(tile, user->type) && GetDepotIndex(tile) == fstd->depot_index) {
		return AYSTAR_FOUND_END_NODE;
	}

	return AYSTAR_DONE;
}

/**
 * Find the node containing the first signal on the path.
 *
 * If the first signal is on the very first two tiles of the path,
 * the second signal is returned. If no suitable signal is present, the
 * last node of the path is returned.
 */
static const PathNode *FindSafePosition(PathNode *path, const Train *v)
{
	/* If there is no signal, reserve the whole path. */
	PathNode *sig = path;

	for (; path->parent != nullptr; path = path->parent) {
		if (IsSafeWaitingPosition(v, path->node.tile, path->node.direction, true, _settings_game.pf.forbid_90_deg)) {
			sig = path;
		}
	}

	return sig;
}

/**
 * Lift the reservation of the tiles from @p start till @p end, excluding @p end itself.
 */
static void ClearPathReservation(const PathNode *start, const PathNode *end)
{
	bool first_run = true;
	for (; start != end; start = start->parent) {
		if (IsPlatformTile(start->node.tile) && first_run) {
			SetPlatformReservation(start->node.tile, TrackdirToExitdir(start->node.direction), false);
		} else {
			UnreserveRailTrack(start->node.tile, TrackdirToTrack(start->node.direction));
		}
		first_run = false;
	}
}

/**
 * To be called when @p current contains the (shortest route to) the target node.
 * Will fill the contents of the NPFFoundTargetData using
 * AyStarNode[NPF_TRACKDIR_CHOICE]. If requested, path reservation
 * is done here.
 */
static void NPFSaveTargetData(AyStar *as, OpenListNode *current)
{
	AyStarUserData *user = (AyStarUserData *)as->user_data;
	NPFFoundTargetData *ftd = (NPFFoundTargetData*)as->user_path;
	ftd->best_trackdir = (Trackdir)current->path.node.user_data[NPF_TRACKDIR_CHOICE];
	ftd->best_path_dist = current->g;
	ftd->best_bird_dist = 0;
	ftd->node = current->path.node;
	ftd->res_okay = false;

	if (as->user_target != nullptr && ((NPFFindStationOrTileData*)as->user_target)->reserve_path && user->type == TRANSPORT_RAIL) {
		/* Path reservation is requested. */
		const Train *v = Train::From(((NPFFindStationOrTileData *)as->user_target)->v);

		const PathNode *target = FindSafePosition(&current->path, v);
		ftd->node = target->node;

		/* If the target is a station skip to platform end. */
		if (IsRailStationTile(target->node.tile) || IsBigRailDepotTile(target->node.tile)) {
			DiagDirection dir = TrackdirToExitdir(target->node.direction);
			uint len = GetPlatformLength(target->node.tile, dir);
			TileIndex end_tile = TILE_ADD(target->node.tile, (len - 1) * TileOffsByDiagDir(dir));

			/* Update only end tile, trackdir of a station stays the same. */
			ftd->node.tile = end_tile;
			if (!IsWaitingPositionFree(v, end_tile, target->node.direction, _settings_game.pf.forbid_90_deg)) return;
			SetPlatformReservation(target->node.tile, dir, true);
			if (IsDepotTile(target->node.tile)) {
				SetDepotReservation(target->node.tile, false);
			} else {
				assert(IsTileType(target->node.tile, MP_STATION));
				SetRailStationReservation(target->node.tile, false);
			}
		} else {
			if (!IsWaitingPositionFree(v, target->node.tile, target->node.direction, _settings_game.pf.forbid_90_deg)) return;
		}

		for (const PathNode *cur = target; cur->parent != nullptr; cur = cur->parent) {
			if (!TryReserveRailTrack(cur->node.tile, TrackdirToTrack(cur->node.direction))) {
				/* Reservation failed, undo. */
				ClearPathReservation(target, cur);
				return;
			}
		}

		ftd->res_okay = true;
	}
}

/**
 * Finds out if a given company's vehicles are allowed to enter a given tile.
 * @param owner    The owner of the vehicle.
 * @param tile     The tile that is about to be entered.
 * @param enterdir The direction in which the vehicle wants to enter the tile.
 * @return         true if the vehicle can enter the tile.
 * @todo           This function should be used in other places than just NPF,
 *                 maybe moved to another file too.
 */
static bool CanEnterTileOwnerCheck(Owner owner, TileIndex tile, DiagDirection enterdir)
{
	if (IsTileType(tile, MP_RAILWAY) || // Rail tile (also rail depot)
			HasStationTileRail(tile) ||     // Rail station tile/waypoint
			IsRoadDepotTile(tile) ||        // Road depot tile
			IsStandardRoadStopTile(tile)) { // Road station tile (but not drive-through stops)
		return IsTileOwner(tile, owner);  // You need to own these tiles entirely to use them
	}

	switch (GetTileType(tile)) {
		case MP_ROAD:
			/* rail-road crossing : are we looking at the railway part? */
			if (IsLevelCrossing(tile) &&
					DiagDirToAxis(enterdir) != GetCrossingRoadAxis(tile)) {
				return IsTileOwner(tile, owner); // Railway needs owner check, while the street is public
			}
			break;

		case MP_TUNNELBRIDGE:
			if (GetTunnelBridgeTransportType(tile) == TRANSPORT_RAIL) {
				return IsTileOwner(tile, owner);
			}
			break;

		default:
			break;
	}

	return true; // no need to check
}


/**
 * Returns the direction the exit of the depot on the given tile is facing.
 */
static DiagDirection GetDepotDirection(TileIndex tile, TransportType type)
{
	assert(IsDepotTypeTile(tile, type));

	switch (type) {
		case TRANSPORT_RAIL:  return GetRailDepotDirection(tile);
		case TRANSPORT_ROAD:  return GetRoadDepotDirection(tile);
		case TRANSPORT_WATER: return GetShipDepotDirection(tile);
		default: return INVALID_DIAGDIR; // Not reached
	}
}

/** Tests if a tile is a road tile with a single tramtrack (tram can reverse) */
static DiagDirection GetSingleTramBit(TileIndex tile)
{
	if (IsNormalRoadTile(tile)) {
		RoadBits rb = GetRoadBits(tile, RTT_TRAM);
		switch (rb) {
			case ROAD_NW: return DIAGDIR_NW;
			case ROAD_SW: return DIAGDIR_SW;
			case ROAD_SE: return DIAGDIR_SE;
			case ROAD_NE: return DIAGDIR_NE;
			default: break;
		}
	}
	return INVALID_DIAGDIR;
}

/**
 * Tests if a tile can be entered or left only from one side.
 *
 * Depots, non-drive-through roadstops, and tiles with single trambits are tested.
 *
 * @param tile The tile of interest.
 * @param type The transporttype of the vehicle.
 * @param subtype For TRANSPORT_ROAD the compatible RoadTypes of the vehicle.
 * @return The single entry/exit-direction of the tile, or INVALID_DIAGDIR if there are more or less directions
 */
static DiagDirection GetTileSingleEntry(TileIndex tile, TransportType type, uint subtype)
{
	if (type != TRANSPORT_WATER && IsDepotTypeTile(tile, type) && !IsBigDepotTile(tile)) return GetDepotDirection(tile, type);

	if (type == TRANSPORT_ROAD) {
		if (IsStandardRoadStopTile(tile)) return GetRoadStopDir(tile);
		if ((RoadTramType)subtype == RTT_TRAM) return GetSingleTramBit(tile);
	}

	return INVALID_DIAGDIR;
}

/**
 * Tests if a vehicle must reverse on a tile.
 *
 * @param tile The tile of interest.
 * @param dir The direction in which the vehicle drives on a tile.
 * @param type The transporttype of the vehicle.
 * @param subtype For TRANSPORT_ROAD the compatible RoadTypes of the vehicle.
 * @return true iff the vehicle must reverse on the tile.
 */
static inline bool ForceReverse(TileIndex tile, DiagDirection dir, TransportType type, uint subtype)
{
	DiagDirection single_entry = GetTileSingleEntry(tile, type, subtype);
	return single_entry != INVALID_DIAGDIR && single_entry != dir;
}

/**
 * Tests if a vehicle can enter a tile.
 *
 * @param tile The tile of interest.
 * @param dir The direction in which the vehicle drives onto a tile.
 * @param user Vehicle information.
 * @return true iff the vehicle can enter the tile.
 */
static bool CanEnterTile(TileIndex tile, DiagDirection dir, AyStarUserData *user)
{
	/* Check tunnel entries and bridge ramps */
	if (IsTileType(tile, MP_TUNNELBRIDGE) && GetTunnelBridgeDirection(tile) != dir) return false;

	/* Test ownership */
	if (!CanEnterTileOwnerCheck(user->owner, tile, dir)) return false;

	/* check correct rail type (mono, maglev, etc) */
	switch (user->type) {
		case TRANSPORT_RAIL: {
			RailType rail_type = GetTileRailType(tile);
			if (!HasBit(user->railtypes, rail_type)) return false;
			break;
		}

		case TRANSPORT_ROAD: {
			RoadType road_type = GetRoadType(tile, (RoadTramType)user->subtype);
			if (!HasBit(user->roadtypes, road_type)) return false;
			break;
		}

		default: break;
	}

	/* Depots, standard roadstops and single tram bits can only be entered from one direction */
	DiagDirection single_entry = GetTileSingleEntry(tile, user->type, user->subtype);
	return single_entry == INVALID_DIAGDIR || single_entry == ReverseDiagDir(dir);
}

/**
 * Returns the driveable Trackdirs on a tile.
 *
 * One-way-roads are taken into account. Signals are not tested.
 *
 * @param dst_tile The tile of interest.
 * @param src_tile The originating tile.
 * @param src_trackdir The direction the vehicle is currently moving.
 * @param type The transporttype of the vehicle.
 * @param subtype For TRANSPORT_ROAD the compatible RoadTypes of the vehicle.
 * @return The Trackdirs the vehicle can continue moving on.
 */
static TrackdirBits GetDriveableTrackdirBits(TileIndex dst_tile, TileIndex src_tile, Trackdir src_trackdir, TransportType type, uint subtype)
{
	TrackdirBits trackdirbits = TrackStatusToTrackdirBits(GetTileTrackStatus(dst_tile, type, subtype));

	if (trackdirbits == TRACKDIR_BIT_NONE && type == TRANSPORT_ROAD && (RoadTramType)subtype == RTT_TRAM) {
		/* GetTileTrackStatus() returns 0 for single tram bits.
		 * As we cannot change it there (easily) without breaking something, change it here */
		switch (GetSingleTramBit(dst_tile)) {
			case DIAGDIR_NE:
			case DIAGDIR_SW:
				trackdirbits = TRACKDIR_BIT_X_NE | TRACKDIR_BIT_X_SW;
				break;

			case DIAGDIR_NW:
			case DIAGDIR_SE:
				trackdirbits = TRACKDIR_BIT_Y_NW | TRACKDIR_BIT_Y_SE;
				break;

			default: break;
		}
	}

	Debug(npf, 4, "Next node: ({}, {}) [{}], possible trackdirs: 0x{:X}", TileX(dst_tile), TileY(dst_tile), dst_tile, trackdirbits);

	/* Select only trackdirs we can reach from our current trackdir */
	trackdirbits &= TrackdirReachesTrackdirs(src_trackdir);

	/* Filter out trackdirs that would make 90 deg turns for trains */
	if (type == TRANSPORT_RAIL && Rail90DegTurnDisallowed(GetTileRailType(src_tile), GetTileRailType(dst_tile))) {
		trackdirbits &= ~TrackdirCrossesTrackdirs(src_trackdir);
	}

	Debug(npf, 6, "After filtering: ({}, {}), possible trackdirs: 0x{:X}", TileX(dst_tile), TileY(dst_tile), trackdirbits);

	return trackdirbits;
}


/* Will just follow the results of GetTileTrackStatus concerning where we can
 * go and where not. Uses AyStar.user_data[NPF_TYPE] as the transport type and
 * an argument to GetTileTrackStatus. Will skip tunnels, meaning that the
 * entry and exit are neighbours. Will fill
 * AyStarNode.user_data[NPF_TRACKDIR_CHOICE] with an appropriate value, and
 * copy AyStarNode.user_data[NPF_NODE_FLAGS] from the parent */
static void NPFFollowTrack(AyStar *aystar, OpenListNode *current)
{
	AyStarUserData *user = (AyStarUserData *)aystar->user_data;
	/* We leave src_tile on track src_trackdir in direction src_exitdir */
	Trackdir src_trackdir = current->path.node.direction;
	TileIndex src_tile = current->path.node.tile;
	DiagDirection src_exitdir = TrackdirToExitdir(src_trackdir);

	/* Information about the vehicle: TransportType (road/rail/water) and SubType (compatible rail/road types) */
	TransportType type = user->type;
	uint subtype = user->subtype;

	/* Initialize to 0, so we can jump out (return) somewhere an have no neighbours */
	aystar->num_neighbours = 0;
	Debug(npf, 4, "Expanding: ({}, {}, {}) [{}]", TileX(src_tile), TileY(src_tile), src_trackdir, src_tile);

	/* We want to determine the tile we arrive, and which choices we have there */
	TileIndex dst_tile;
	TrackdirBits trackdirbits;

	/* Find dest tile */
	/* Is src_tile valid, and can be used?
	 * When choosing track on a junction src_tile is the tile neighboured to the junction wrt. exitdir.
	 * But we must not check the validity of this move, as src_tile is totally unrelated to the move, if a roadvehicle reversed on a junction. */
	if (CheckIgnoreFirstTile(&current->path)) {
		/* Do not perform any checks that involve src_tile */
		dst_tile = src_tile + TileOffsByDiagDir(src_exitdir);
		trackdirbits = GetDriveableTrackdirBits(dst_tile, src_tile, src_trackdir, type, subtype);
	} else if (IsTileType(src_tile, MP_TUNNELBRIDGE) && GetTunnelBridgeDirection(src_tile) == src_exitdir) {
		/* We drive through the wormhole and arrive on the other side */
		dst_tile = GetOtherTunnelBridgeEnd(src_tile);
		trackdirbits = TrackdirToTrackdirBits(src_trackdir);
	} else if (ForceReverse(src_tile, src_exitdir, type, subtype)) {
		/* We can only reverse on this tile */
		dst_tile = src_tile;
		src_trackdir = ReverseTrackdir(src_trackdir);
		trackdirbits = TrackdirToTrackdirBits(src_trackdir);
	} else {
		/* We leave src_tile in src_exitdir and reach dst_tile */
		dst_tile = AddTileIndexDiffCWrap(src_tile, TileIndexDiffCByDiagDir(src_exitdir));

		if (dst_tile != INVALID_TILE && IsNormalRoadTile(dst_tile) && !CanEnterTile(dst_tile, src_exitdir, user)) dst_tile = INVALID_TILE;

		if (dst_tile == INVALID_TILE) {
			/* We cannot enter the next tile. Road vehicles can reverse, others reach dead end */
			if (type != TRANSPORT_ROAD || (RoadTramType)subtype == RTT_TRAM) return;

			dst_tile = src_tile;
			src_trackdir = ReverseTrackdir(src_trackdir);
		}

		trackdirbits = GetDriveableTrackdirBits(dst_tile, src_tile, src_trackdir, type, subtype);

		if (trackdirbits == TRACKDIR_BIT_NONE) {
			/* We cannot enter the next tile. Road vehicles can reverse, others reach dead end */
			if (type != TRANSPORT_ROAD || (RoadTramType)subtype == RTT_TRAM) return;

			dst_tile = src_tile;
			src_trackdir = ReverseTrackdir(src_trackdir);

			trackdirbits = GetDriveableTrackdirBits(dst_tile, src_tile, src_trackdir, type, subtype);
		}
	}

	if (NPFGetFlag(&current->path.node, NPF_FLAG_IGNORE_RESERVED)) {
		/* Mask out any reserved tracks. */
		TrackBits reserved = GetReservedTrackbits(dst_tile);
		trackdirbits &= ~TrackBitsToTrackdirBits(reserved);

		for (Track t : SetTrackBitIterator(TrackdirBitsToTrackBits(trackdirbits))) {
			if (TracksOverlap(reserved | TrackToTrackBits(t))) trackdirbits &= ~TrackToTrackdirBits(t);
		}
	}

	/* Enumerate possible track */
	uint i = 0;
	while (trackdirbits != TRACKDIR_BIT_NONE) {
		Trackdir dst_trackdir = RemoveFirstTrackdir(&trackdirbits);
		Debug(npf, 5, "Expanded into trackdir: {}, remaining trackdirs: 0x{:X}", dst_trackdir, trackdirbits);

		/* Tile with signals? */
		if (IsTileType(dst_tile, MP_RAILWAY) && GetRailTileType(dst_tile) == RAIL_TILE_SIGNALS) {
			if (HasSignalOnTrackdir(dst_tile, ReverseTrackdir(dst_trackdir)) && !HasSignalOnTrackdir(dst_tile, dst_trackdir) && IsOnewaySignal(dst_tile, TrackdirToTrack(dst_trackdir))) {
				/* If there's a one-way signal not pointing towards us, stop going in this direction. */
				break;
			}
		}
		{
			/* We've found ourselves a neighbour :-) */
			AyStarNode *neighbour = &aystar->neighbours[i];
			neighbour->tile = dst_tile;
			neighbour->direction = dst_trackdir;
			/* Save user data */
			neighbour->user_data[NPF_NODE_FLAGS] = current->path.node.user_data[NPF_NODE_FLAGS];
			NPFFillTrackdirChoice(neighbour, current);
		}
		i++;
	}
	aystar->num_neighbours = i;
}

/*
 * Plan a route to the specified target (which is checked by target_proc),
 * from start1 and if not nullptr, from start2 as well. The type of transport we
 * are checking is in type. reverse_penalty is applied to all routes that
 * originate from the second start node.
 * When we are looking for one specific target (optionally multiple tiles), we
 * should use a good heuristic to perform aystar search. When we search for
 * multiple targets that are spread around, we should perform a breadth first
 * search by specifying CalcZero as our heuristic.
 */
static NPFFoundTargetData NPFRouteInternal(AyStarNode *start1, bool ignore_start_tile1, AyStarNode *start2, bool ignore_start_tile2, NPFFindStationOrTileData *target, AyStar_EndNodeCheck target_proc, AyStar_CalculateH heuristic_proc, AyStarUserData *user, uint reverse_penalty, bool ignore_reserved = false, int max_penalty = 0)
{
	/* Initialize procs */
	_npf_aystar.max_path_cost = max_penalty;
	_npf_aystar.CalculateH = heuristic_proc;
	_npf_aystar.EndNodeCheck = target_proc;
	_npf_aystar.FoundEndNode = NPFSaveTargetData;
	_npf_aystar.GetNeighbours = NPFFollowTrack;
	switch (user->type) {
		default: NOT_REACHED();
		case TRANSPORT_RAIL:  _npf_aystar.CalculateG = NPFRailPathCost;  break;
		case TRANSPORT_ROAD:  _npf_aystar.CalculateG = NPFRoadPathCost;  break;
		case TRANSPORT_WATER: _npf_aystar.CalculateG = NPFWaterPathCost; break;
	}

	/* Initialize Start Node(s) */
	start1->user_data[NPF_TRACKDIR_CHOICE] = INVALID_TRACKDIR;
	start1->user_data[NPF_NODE_FLAGS] = 0;
	NPFSetFlag(start1, NPF_FLAG_IGNORE_START_TILE, ignore_start_tile1);
	NPFSetFlag(start1, NPF_FLAG_IGNORE_RESERVED, ignore_reserved);
	_npf_aystar.AddStartNode(start1, 0);
	if (start2 != nullptr) {
		start2->user_data[NPF_TRACKDIR_CHOICE] = INVALID_TRACKDIR;
		start2->user_data[NPF_NODE_FLAGS] = 0;
		NPFSetFlag(start2, NPF_FLAG_IGNORE_START_TILE, ignore_start_tile2);
		NPFSetFlag(start2, NPF_FLAG_REVERSE, true);
		NPFSetFlag(start2, NPF_FLAG_IGNORE_RESERVED, ignore_reserved);
		_npf_aystar.AddStartNode(start2, reverse_penalty);
	}

	/* Initialize result */
	NPFFoundTargetData result;
	result.best_bird_dist = UINT_MAX;
	result.best_path_dist = UINT_MAX;
	result.best_trackdir  = INVALID_TRACKDIR;
	result.node.tile      = INVALID_TILE;
	result.res_okay       = false;
	_npf_aystar.user_path = &result;

	/* Initialize target */
	_npf_aystar.user_target = target;

	/* Initialize user_data */
	_npf_aystar.user_data = user;

	/* GO! */
	[[maybe_unused]] int r = _npf_aystar.Main();
	assert(r != AYSTAR_STILL_BUSY);

	if (result.best_bird_dist != 0) {
		if (target != nullptr) {
			Debug(npf, 1, "Could not find route to tile 0x{:X} from 0x{:X}.", target->dest_coords, start1->tile);
		} else {
			/* Assumption: target == nullptr, so we are looking for a depot */
			Debug(npf, 1, "Could not find route to a depot from tile 0x{:X}.", start1->tile);
		}

	}
	return result;
}

/* Will search as below, but with two start nodes, the second being the
 * reverse. Look at the NPF_FLAG_REVERSE flag in the result node to see which
 * direction was taken (NPFGetFlag(result.node, NPF_FLAG_REVERSE)) */
static NPFFoundTargetData NPFRouteToStationOrTileTwoWay(TileIndex tile1, Trackdir trackdir1, bool ignore_start_tile1, TileIndex tile2, Trackdir trackdir2, bool ignore_start_tile2, NPFFindStationOrTileData *target, AyStarUserData *user)
{
	AyStarNode start1;
	AyStarNode start2;

	start1.tile = tile1;
	start2.tile = tile2;
	start1.direction = trackdir1;
	start2.direction = trackdir2;

	return NPFRouteInternal(&start1, ignore_start_tile1, (IsValidTile(tile2) ? &start2 : nullptr), ignore_start_tile2, target, NPFFindStationOrTile, NPFCalcStationOrTileHeuristic, user, 0);
}

/* Will search from the given tile and direction, for a route to the given
 * station for the given transport type. See the declaration of
 * NPFFoundTargetData above for the meaning of the result. */
static NPFFoundTargetData NPFRouteToStationOrTile(TileIndex tile, Trackdir trackdir, bool ignore_start_tile, NPFFindStationOrTileData *target, AyStarUserData *user)
{
	return NPFRouteToStationOrTileTwoWay(tile, trackdir, ignore_start_tile, INVALID_TILE, INVALID_TRACKDIR, false, target, user);
}

/* Search using breadth first. Good for little track choice and inaccurate
 * heuristic, such as railway/road with two start nodes, the second being the reverse. Call
 * NPFGetFlag(result.node, NPF_FLAG_REVERSE) to see from which node the path
 * originated. All paths from the second node will have the given
 * reverse_penalty applied (NPF_TILE_LENGTH is the equivalent of one full
 * tile).
 */
static NPFFoundTargetData NPFRouteToDepotBreadthFirstTwoWay(TileIndex tile1, Trackdir trackdir1, bool ignore_start_tile1, TileIndex tile2, Trackdir trackdir2, bool ignore_start_tile2, NPFFindStationOrTileData *target, AyStarUserData *user, uint reverse_penalty, int max_penalty)
{
	AyStarNode start1;
	AyStarNode start2;

	start1.tile = tile1;
	start2.tile = tile2;
	start1.direction = trackdir1;
	start2.direction = trackdir2;

	/* perform a breadth first search. Target is nullptr,
	 * since we are just looking for any depot...*/
	return NPFRouteInternal(&start1, ignore_start_tile1, (IsValidTile(tile2) ? &start2 : nullptr), ignore_start_tile2, target, NPFFindDepot, NPFCalcZero, user, reverse_penalty, false, max_penalty);
}

void InitializeNPF()
{
	static bool first_init = true;
	if (first_init) {
		first_init = false;
		_npf_aystar.Init(NPFHash, NPF_HASH_SIZE);
	} else {
		_npf_aystar.Clear();
	}
	_npf_aystar.loops_per_tick = 0;
	_npf_aystar.max_path_cost = 0;
	//_npf_aystar.max_search_nodes = 0;
	/* We will limit the number of nodes for now, until we have a better
	 * solution to really fix performance */
	_npf_aystar.max_search_nodes = _settings_game.pf.npf.npf_max_search_nodes;
}

static void NPFFillWithOrderData(NPFFindStationOrTileData *fstd, const Vehicle *v, bool reserve_path = false)
{
	fstd->station_index = INVALID_STATION;
	fstd->depot_index = INVALID_DEPOT;

	if (v->current_order.IsType(OT_GOTO_DEPOT)) {
		fstd->depot_index = v->current_order.GetDestination();
	} else if (v->current_order.IsType(OT_GOTO_STATION) || v->current_order.IsType(OT_GOTO_WAYPOINT)) {
		/* Ships don't really reach their stations, but the tile in front. So don't
		 * save the station id for ships. For roadvehs we don't store it either,
		 * because multistop depends on vehicles actually reaching the exact
		 * dest_tile, not just any stop of that station.
		 * So only for train orders to stations we fill fstd->station_index, for all
		 * others only dest_coords */
		fstd->station_index = v->current_order.GetDestination();
		if (v->type == VEH_TRAIN) {
			fstd->station_type = v->current_order.IsType(OT_GOTO_STATION) ? STATION_RAIL : STATION_WAYPOINT;
		} else if (v->type == VEH_ROAD) {
			fstd->station_type = RoadVehicle::From(v)->IsBus() ? STATION_BUS : STATION_TRUCK;
		} else if (v->type == VEH_SHIP) {
			fstd->station_type = v->current_order.IsType(OT_GOTO_STATION) ? STATION_DOCK : STATION_BUOY;
		}
	}

	if(fstd->station_index != INVALID_STATION || fstd->depot_index != INVALID_DEPOT) {
		fstd->not_articulated = v->type == VEH_ROAD && !RoadVehicle::From(v)->HasArticulatedPart();
		/* Let's take the closest tile of the station as our target for vehicles */
		if (fstd->station_index != INVALID_STATION) {
			fstd->dest_coords = CalcClosestStationTile(fstd->station_index, v->tile, fstd->station_type);
		} else {
			fstd->dest_coords = CalcClosestDepotTile(fstd->depot_index, v->tile);
		}
	} else {
		fstd->dest_coords = v->dest_tile;
		fstd->station_index = INVALID_STATION;
		fstd->depot_index = INVALID_DEPOT;
	}
	fstd->reserve_path = reserve_path;
	fstd->v = v;
}

/*** Road vehicles ***/

FindDepotData NPFRoadVehicleFindNearestDepot(const RoadVehicle *v, int max_penalty)
{
	Trackdir trackdir = v->GetVehicleTrackdir();

	AyStarUserData user = { v->owner, TRANSPORT_ROAD, RAILTYPES_NONE, v->compatible_roadtypes, GetRoadTramType(v->roadtype) };
	NPFFoundTargetData ftd = NPFRouteToDepotBreadthFirstTwoWay(v->tile, trackdir, false, INVALID_TILE, INVALID_TRACKDIR, false, nullptr, &user, 0, max_penalty);

	if (ftd.best_bird_dist != 0) return FindDepotData();

	/* Found target */
	/* Our caller expects a number of tiles, so we just approximate that
	 * number by this. It might not be completely what we want, but it will
	 * work for now :-) We can possibly change this when the old pathfinder
	 * is removed. */
	return FindDepotData(ftd.node.tile, ftd.best_path_dist);
}

Trackdir NPFRoadVehicleChooseTrack(const RoadVehicle *v, TileIndex tile, DiagDirection enterdir, bool &path_found)
{
	NPFFindStationOrTileData fstd;

	NPFFillWithOrderData(&fstd, v);
	Trackdir trackdir = DiagDirToDiagTrackdir(enterdir);

	AyStarUserData user = { v->owner, TRANSPORT_ROAD, RAILTYPES_NONE, v->compatible_roadtypes, GetRoadTramType(v->roadtype) };
	NPFFoundTargetData ftd = NPFRouteToStationOrTile(tile - TileOffsByDiagDir(enterdir), trackdir, true, &fstd, &user);

	assert(ftd.best_trackdir != INVALID_TRACKDIR);

	/* If ftd.best_bird_dist is 0, we found our target and ftd.best_trackdir contains
	 * the direction we need to take to get there, if ftd.best_bird_dist is not 0,
	 * we did not find our target, but ftd.best_trackdir contains the direction leading
	 * to the tile closest to our target. */
	path_found = (ftd.best_bird_dist == 0);
	return ftd.best_trackdir;
}

/*** Ships ***/

Track NPFShipChooseTrack(const Ship *v, bool &path_found)
{
	NPFFindStationOrTileData fstd;
	Trackdir trackdir = v->GetVehicleTrackdir();
	assert(trackdir != INVALID_TRACKDIR); // Check that we are not in a depot

	NPFFillWithOrderData(&fstd, v);

	AyStarUserData user = { v->owner, TRANSPORT_WATER, RAILTYPES_NONE, ROADTYPES_NONE, 0 };
	NPFFoundTargetData ftd = NPFRouteToStationOrTile(v->tile, trackdir, true, &fstd, &user);

	assert(ftd.best_trackdir != INVALID_TRACKDIR);

	/* If ftd.best_bird_dist is 0, we found our target and ftd.best_trackdir contains
	 * the direction we need to take to get there, if ftd.best_bird_dist is not 0,
	 * we did not find our target, but ftd.best_trackdir contains the direction leading
	 * to the tile closest to our target. */
	path_found = (ftd.best_bird_dist == 0);
	return TrackdirToTrack(ftd.best_trackdir);
}

bool NPFShipCheckReverse(const Ship *v)
{
	NPFFindStationOrTileData fstd;
	NPFFoundTargetData ftd;

	NPFFillWithOrderData(&fstd, v);

	Trackdir trackdir = v->GetVehicleTrackdir();
	Trackdir trackdir_rev = ReverseTrackdir(trackdir);
	assert(trackdir != INVALID_TRACKDIR);
	assert(trackdir_rev != INVALID_TRACKDIR);

	AyStarUserData user = { v->owner, TRANSPORT_WATER, RAILTYPES_NONE, ROADTYPES_NONE, 0 };
	ftd = NPFRouteToStationOrTileTwoWay(v->tile, trackdir, false, v->tile, trackdir_rev, false, &fstd, &user);
	/* If we didn't find anything, just keep on going straight ahead, otherwise take the reverse flag */
	return ftd.best_bird_dist == 0 && NPFGetFlag(&ftd.node, NPF_FLAG_REVERSE);
}

/*** Trains ***/

FindDepotData NPFTrainFindNearestDepot(const Train *v, int max_penalty)
{
	const Train *last = v->Last();
	Trackdir trackdir = v->GetVehicleTrackdir();
	Trackdir trackdir_rev = ReverseTrackdir(last->GetVehicleTrackdir());
	NPFFindStationOrTileData fstd;
	fstd.v = v;
	fstd.reserve_path = false;

	assert(trackdir != INVALID_TRACKDIR);
	AyStarUserData user = { v->owner, TRANSPORT_RAIL, v->compatible_railtypes, ROADTYPES_NONE, 0 };
	NPFFoundTargetData ftd = NPFRouteToDepotBreadthFirstTwoWay(v->tile, trackdir, false, last->tile, trackdir_rev, false, &fstd, &user, NPF_INFINITE_PENALTY, max_penalty);
	if (ftd.best_bird_dist != 0) return FindDepotData();

	/* Found target */
	/* Our caller expects a number of tiles, so we just approximate that
	 * number by this. It might not be completely what we want, but it will
	 * work for now :-) We can possibly change this when the old pathfinder
	 * is removed. */
	return FindDepotData(ftd.node.tile, ftd.best_path_dist, NPFGetFlag(&ftd.node, NPF_FLAG_REVERSE));
}

bool NPFTrainFindNearestSafeTile(const Train *v, TileIndex tile, Trackdir trackdir, bool override_railtype)
{
	assert(v->type == VEH_TRAIN);

	NPFFindStationOrTileData fstd;
	fstd.v = v;
	fstd.reserve_path = true;

	AyStarNode start1;
	start1.tile = tile;
	start1.direction = trackdir;

	RailTypes railtypes = v->compatible_railtypes;
	if (override_railtype) railtypes |= GetRailTypeInfo(v->railtype)->compatible_railtypes;

	/* perform a breadth first search. Target is nullptr,
	 * since we are just looking for any safe tile...*/
	AyStarUserData user = { v->owner, TRANSPORT_RAIL, railtypes, ROADTYPES_NONE, 0 };
	return NPFRouteInternal(&start1, true, nullptr, false, &fstd, NPFFindSafeTile, NPFCalcZero, &user, 0, true).res_okay;
}

bool NPFTrainCheckReverse(const Train *v)
{
	NPFFindStationOrTileData fstd;
	NPFFoundTargetData ftd;
	const Train *last = v->Last();

	NPFFillWithOrderData(&fstd, v);

	Trackdir trackdir = v->GetVehicleTrackdir();
	Trackdir trackdir_rev = ReverseTrackdir(last->GetVehicleTrackdir());
	assert(trackdir != INVALID_TRACKDIR);
	assert(trackdir_rev != INVALID_TRACKDIR);

	AyStarUserData user = { v->owner, TRANSPORT_RAIL, v->compatible_railtypes, ROADTYPES_NONE, 0 };
	ftd = NPFRouteToStationOrTileTwoWay(v->tile, trackdir, false, last->tile, trackdir_rev, false, &fstd, &user);
	/* If we didn't find anything, just keep on going straight ahead, otherwise take the reverse flag */
	return ftd.best_bird_dist == 0 && NPFGetFlag(&ftd.node, NPF_FLAG_REVERSE);
}

Track NPFTrainChooseTrack(const Train *v, bool &path_found, bool reserve_track, struct PBSTileInfo *target)
{
	NPFFindStationOrTileData fstd;
	NPFFillWithOrderData(&fstd, v, reserve_track);

	PBSTileInfo origin = FollowTrainReservation(v);
	assert(IsValidTrackdir(origin.trackdir));

	AyStarUserData user = { v->owner, TRANSPORT_RAIL, v->compatible_railtypes, ROADTYPES_NONE, 0 };
	NPFFoundTargetData ftd = NPFRouteToStationOrTile(origin.tile, origin.trackdir, true, &fstd, &user);

	if (target != nullptr) {
		target->tile = ftd.node.tile;
		target->trackdir = (Trackdir)ftd.node.direction;
		target->okay = ftd.res_okay;
	}

	assert(ftd.best_trackdir != INVALID_TRACKDIR);

	/* If ftd.best_bird_dist is 0, we found our target and ftd.best_trackdir contains
	 * the direction we need to take to get there, if ftd.best_bird_dist is not 0,
	 * we did not find our target, but ftd.best_trackdir contains the direction leading
	 * to the tile closest to our target. */
	path_found = (ftd.best_bird_dist == 0);
	/* Discard enterdir information, making it a normal track */
	return TrackdirToTrack(ftd.best_trackdir);
}
