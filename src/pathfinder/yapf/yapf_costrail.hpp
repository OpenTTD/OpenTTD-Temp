/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file yapf_costrail.hpp Cost determination for rails. */

#ifndef YAPF_COSTRAIL_HPP
#define YAPF_COSTRAIL_HPP

#include <vector>

#include "../../pbs.h"

template <class Types>
class CYapfCostRailT : public CYapfCostBase {
public:
	typedef typename Types::Tpf Tpf;              ///< the pathfinder class (derived from THIS class)
	typedef typename Types::TrackFollower TrackFollower;
	typedef typename Types::NodeList::Titem Node; ///< this will be our node type
	typedef typename Node::Key Key;               ///< key to hash tables
	typedef typename Node::CachedData CachedData;

protected:

	/* Structure used inside PfCalcCost() to keep basic tile information. */
	struct TILE {
		TileIndex   tile;
		Trackdir    td;
		TileType    tile_type;
		RailType    rail_type;

		TILE()
		{
			tile = INVALID_TILE;
			td = INVALID_TRACKDIR;
			tile_type = MP_VOID;
			rail_type = INVALID_RAILTYPE;
		}

		TILE(TileIndex tile, Trackdir td)
		{
			this->tile = tile;
			this->td = td;
			this->tile_type = GetTileType(tile);
			this->rail_type = GetTileRailType(tile);
		}
	};

protected:
	/**
	 * @note maximum cost doesn't work with caching enabled
	 * @todo fix maximum cost failing with caching (e.g. FS#2900)
	 */
	int m_max_cost;
	bool m_disable_cache;
	std::vector<int> m_sig_look_ahead_costs;

public:
	bool          m_stopped_on_first_two_way_signal;
protected:

	static const int s_max_segment_cost = 10000;

	CYapfCostRailT() : m_max_cost(0), m_disable_cache(false), m_stopped_on_first_two_way_signal(false)
	{
		/* pre-compute look-ahead penalties into array */
		int p0 = Yapf().PfGetSettings().rail_look_ahead_signal_p0;
		int p1 = Yapf().PfGetSettings().rail_look_ahead_signal_p1;
		int p2 = Yapf().PfGetSettings().rail_look_ahead_signal_p2;
		m_sig_look_ahead_costs.clear();
		m_sig_look_ahead_costs.reserve(Yapf().PfGetSettings().rail_look_ahead_max_signals);
		for (uint i = 0; i < Yapf().PfGetSettings().rail_look_ahead_max_signals; i++) {
			m_sig_look_ahead_costs.push_back(p0 + i * (p1 + i * p2));
		}
	}

	/** to access inherited path finder */
	Tpf& Yapf()
	{
		return *static_cast<Tpf *>(this);
	}

public:
	inline int SlopeCost(TileIndex tile, Trackdir td)
	{
		if (!stSlopeCost(tile, td)) return 0;
		return Yapf().PfGetSettings().rail_slope_penalty;
	}

	inline int CurveCost(Trackdir td1, Trackdir td2)
	{
		assert(IsValidTrackdir(td1));
		assert(IsValidTrackdir(td2));
		int cost = 0;
		if (TrackFollower::Allow90degTurns()
				&& HasTrackdir(TrackdirCrossesTrackdirs(td1), td2)) {
			/* 90-deg curve penalty */
			cost += Yapf().PfGetSettings().rail_curve90_penalty;
		} else if (td2 != NextTrackdir(td1)) {
			/* 45-deg curve penalty */
			cost += Yapf().PfGetSettings().rail_curve45_penalty;
		}
		return cost;
	}

	inline int SwitchCost(TileIndex tile1, TileIndex tile2, DiagDirection exitdir)
	{
		if (IsPlainRailTile(tile1) && IsPlainRailTile(tile2)) {
			bool t1 = KillFirstBit(GetTrackBits(tile1) & DiagdirReachesTracks(ReverseDiagDir(exitdir))) != TRACK_BIT_NONE;
			bool t2 = KillFirstBit(GetTrackBits(tile2) & DiagdirReachesTracks(exitdir)) != TRACK_BIT_NONE;
			if (t1 && t2) return Yapf().PfGetSettings().rail_doubleslip_penalty;
		}
		return 0;
	}

	/** Return one tile cost (base cost + level crossing penalty). */
	inline int OneTileCost(TileIndex &tile, Trackdir trackdir)
	{
		int cost = 0;
		/* set base cost */
		if (IsDiagonalTrackdir(trackdir)) {
			cost += YAPF_TILE_LENGTH;
			switch (GetTileType(tile)) {
				case MP_ROAD:
					/* Increase the cost for level crossings */
					if (IsLevelCrossing(tile)) {
						cost += Yapf().PfGetSettings().rail_crossing_penalty;
					}
					break;

				default:
					break;
			}
		} else {
			/* non-diagonal trackdir */
			cost = YAPF_TILE_CORNER_LENGTH;
		}
		return cost;
	}

	/** Check for a reserved station platform. */
	inline bool IsAnyStationTileReserved(TileIndex tile, Trackdir trackdir, int skipped)
	{
		TileIndexDiff diff = TileOffsByDiagDir(TrackdirToExitdir(ReverseTrackdir(trackdir)));
		for (; skipped >= 0; skipped--, tile += diff) {
			if (HasStationReservation(tile)) return true;
		}
		return false;
	}

	/** The cost for reserved tiles, including skipped ones. */
	inline int ReservationCost(Node &n, TileIndex tile, Trackdir trackdir, int skipped)
	{
		if (n.m_num_signals_passed >= m_sig_look_ahead_costs.size() / 2) return 0;
		if (!IsPbsSignal(n.m_last_signal_type)) return 0;

		if (IsRailStationTile(tile) && IsAnyStationTileReserved(tile, trackdir, skipped)) {
			return Yapf().PfGetSettings().rail_pbs_station_penalty * (skipped + 1);
		} else if (TrackOverlapsTracks(GetReservedTrackbits(tile), TrackdirToTrack(trackdir))) {
			int cost = Yapf().PfGetSettings().rail_pbs_cross_penalty;
			if (!IsDiagonalTrackdir(trackdir)) cost = (cost * YAPF_TILE_CORNER_LENGTH) / YAPF_TILE_LENGTH;
			return cost * (skipped + 1);
		}
		return 0;
	}

	int SignalCost(Node &n, TileIndex tile, Trackdir trackdir)
	{
		int cost = 0;
		/* if there is one-way signal in the opposite direction, then it is not our way */
		if (IsTileType(tile, MP_RAILWAY)) {
			bool has_signal_against = HasSignalOnTrackdir(tile, ReverseTrackdir(trackdir));
			bool has_signal_along = HasSignalOnTrackdir(tile, trackdir);
			if (has_signal_against && !has_signal_along && IsOnewaySignal(tile, TrackdirToTrack(trackdir))) {
				/* one-way signal in opposite direction */
				n.m_segment->m_end_segment_reason |= ESRB_DEAD_END;
			} else {
				if (has_signal_along) {
					SignalState sig_state = GetSignalStateByTrackdir(tile, trackdir);
					SignalType sig_type = GetSignalType(tile, TrackdirToTrack(trackdir));

					n.m_last_signal_type = sig_type;

					/* cache the look-ahead polynomial constant only if we didn't pass more signals than the look-ahead limit is */
					int look_ahead_cost = (n.m_num_signals_passed < m_sig_look_ahead_costs.size()) ? m_sig_look_ahead_costs[n.m_num_signals_passed] : 0;
					if (sig_state != SIGNAL_STATE_RED) {
						/* green signal */
						n.flags_u.flags_s.m_last_signal_was_red = false;
						/* negative look-ahead red-signal penalties would cause problems later, so use them as positive penalties for green signal */
						if (look_ahead_cost < 0) {
							/* add its negation to the cost */
							cost -= look_ahead_cost;
						}
					} else {
						/* we have a red signal in our direction
						 * was it first signal which is two-way? */
						if (!IsPbsSignal(sig_type) && Yapf().TreatFirstRedTwoWaySignalAsEOL() && n.flags_u.flags_s.m_choice_seen && has_signal_against && n.m_num_signals_passed == 0) {
							/* yes, the first signal is two-way red signal => DEAD END. Prune this branch... */
							Yapf().PruneIntermediateNodeBranch(&n);
							n.m_segment->m_end_segment_reason |= ESRB_DEAD_END;
							Yapf().m_stopped_on_first_two_way_signal = true;
							return -1;
						}
						n.m_last_red_signal_type = sig_type;
						n.flags_u.flags_s.m_last_signal_was_red = true;

						/* look-ahead signal penalty */
						if (!IsPbsSignal(sig_type) && look_ahead_cost > 0) {
							/* add the look ahead penalty only if it is positive */
							cost += look_ahead_cost;
						}

						/* special signal penalties */
						if (n.m_num_signals_passed == 0) {
							switch (sig_type) {
								case SIGTYPE_COMBO:
								case SIGTYPE_EXIT:   cost += Yapf().PfGetSettings().rail_firstred_exit_penalty; break; // first signal is red pre-signal-exit
								case SIGTYPE_NORMAL:
								case SIGTYPE_ENTRY:  cost += Yapf().PfGetSettings().rail_firstred_penalty; break;
								default: break;
							}
						}
					}

					n.m_num_signals_passed++;
					n.m_segment->m_last_signal_tile = tile;
					n.m_segment->m_last_signal_td = trackdir;
				}

				if (has_signal_against && IsPbsSignal(GetSignalType(tile, TrackdirToTrack(trackdir)))) {
					cost += n.m_num_signals_passed < Yapf().PfGetSettings().rail_look_ahead_max_signals ? Yapf().PfGetSettings().rail_pbs_signal_back_penalty : 0;
				}
			}
		}
		return cost;
	}

	inline int PlatformLengthPenalty(int platform_length)
	{
		int cost = 0;
		const Train *v = Yapf().GetVehicle();
		assert(v != nullptr);
		assert(v->type == VEH_TRAIN);
		assert(v->gcache.cached_total_length != 0);
		int missing_platform_length = CeilDiv(v->gcache.cached_total_length, TILE_SIZE) - platform_length;
		if (missing_platform_length < 0) {
			/* apply penalty for longer platform than needed */
			cost += Yapf().PfGetSettings().rail_longer_platform_penalty + Yapf().PfGetSettings().rail_longer_platform_per_tile_penalty * -missing_platform_length;
		} else if (missing_platform_length > 0) {
			/* apply penalty for shorter platform than needed */
			cost += Yapf().PfGetSettings().rail_shorter_platform_penalty + Yapf().PfGetSettings().rail_shorter_platform_per_tile_penalty * missing_platform_length;
		}
		return cost;
	}

public:
	inline void SetMaxCost(int max_cost)
	{
		m_max_cost = max_cost;
	}

	/**
	 * Called by YAPF to calculate the cost from the origin to the given node.
	 *  Calculates only the cost of given node, adds it to the parent node cost
	 *  and stores the result into Node::m_cost member
	 */
	inline bool PfCalcCost(Node &n, const TrackFollower *tf)
	{
		assert(!n.flags_u.flags_s.m_targed_seen);
		assert(tf->m_new_tile == n.m_key.m_tile);
		assert((HasTrackdir(tf->m_new_td_bits, n.m_key.m_td)));

		/* Does the node have some parent node? */
		bool has_parent = (n.m_parent != nullptr);

		/* Do we already have a cached segment? */
		CachedData &segment = *n.m_segment;
		bool is_cached_segment = (segment.m_cost >= 0);

		int parent_cost = has_parent ? n.m_parent->m_cost : 0;

		/* Each node cost contains 2 or 3 main components:
		 *  1. Transition cost - cost of the move from previous node (tile):
		 *    - curve cost (or zero for straight move)
		 *  2. Tile cost:
		 *    - base tile cost
		 *      - YAPF_TILE_LENGTH for diagonal tiles
		 *      - YAPF_TILE_CORNER_LENGTH for non-diagonal tiles
		 *    - tile penalties
		 *      - tile slope penalty (upward slopes)
		 *      - red signal penalty
		 *      - level crossing penalty
		 *      - speed-limit penalty (bridges)
		 *      - station platform penalty
		 *      - penalty for reversing in the depot
		 *      - etc.
		 *  3. Extra cost (applies to the last node only)
		 *    - last red signal penalty
		 *    - penalty for too long or too short platform on the destination station
		 */
		int transition_cost = 0;
		int extra_cost = 0;

		/* Segment: one or more tiles connected by contiguous tracks of the same type.
		 * Each segment cost includes 'Tile cost' for all its tiles (including the first
		 * and last), and the 'Transition cost' between its tiles. The first transition
		 * cost of segment entry (move from the 'parent' node) is not included!
		 */
		int segment_entry_cost = 0;
		int segment_cost = 0;

		const Train *v = Yapf().GetVehicle();

		/* start at n.m_key.m_tile / n.m_key.m_td and walk to the end of segment */
		TILE cur(n.m_key.m_tile, n.m_key.m_td);

		/* the previous tile will be needed for transition cost calculations */
		TILE prev = !has_parent ? TILE() : TILE(n.m_parent->GetLastTile(), n.m_parent->GetLastTrackdir());

		EndSegmentReasonBits end_segment_reason = ESRB_NONE;

		TrackFollower tf_local(v, Yapf().GetCompatibleRailTypes());

		if (!has_parent) {
			/* We will jump to the middle of the cost calculator assuming that segment cache is not used. */
			assert(!is_cached_segment);
			/* Skip the first transition cost calculation. */
			goto no_entry_cost;
		}

		for (;;) {
			/* Transition cost (cost of the move from previous tile) */
			transition_cost = Yapf().CurveCost(prev.td, cur.td);
			transition_cost += Yapf().SwitchCost(prev.tile, cur.tile, TrackdirToExitdir(prev.td));

			/* First transition cost counts against segment entry cost, other transitions
			 * inside segment will come to segment cost (and will be cached) */
			if (segment_cost == 0) {
				/* We just entered the loop. First transition cost goes to segment entry cost)*/
				segment_entry_cost = transition_cost;
				transition_cost = 0;

				/* It is the right time now to look if we can reuse the cached segment cost. */
				if (is_cached_segment) {
					/* Yes, we already know the segment cost. */
					segment_cost = segment.m_cost;
					/* We know also the reason why the segment ends. */
					end_segment_reason = segment.m_end_segment_reason;
					/* We will need also some information about the last signal (if it was red). */
					if (segment.m_last_signal_tile != INVALID_TILE) {
						assert(HasSignalOnTrackdir(segment.m_last_signal_tile, segment.m_last_signal_td));
						SignalState sig_state = GetSignalStateByTrackdir(segment.m_last_signal_tile, segment.m_last_signal_td);
						bool is_red = (sig_state == SIGNAL_STATE_RED);
						n.flags_u.flags_s.m_last_signal_was_red = is_red;
						if (is_red) {
							n.m_last_red_signal_type = GetSignalType(segment.m_last_signal_tile, TrackdirToTrack(segment.m_last_signal_td));
						}
					}
					/* No further calculation needed. */
					cur = TILE(n.GetLastTile(), n.GetLastTrackdir());
					break;
				}
			} else {
				/* Other than first transition cost count as the regular segment cost. */
				segment_cost += transition_cost;
			}

no_entry_cost: // jump here at the beginning if the node has no parent (it is the first node)

			/* All other tile costs will be calculated here. */
			segment_cost += Yapf().OneTileCost(cur.tile, cur.td);

			/* If we skipped some tunnel/bridge/station tiles, add their base cost */
			segment_cost += YAPF_TILE_LENGTH * tf->m_tiles_skipped;

			/* Slope cost. */
			segment_cost += Yapf().SlopeCost(cur.tile, cur.td);

			/* Signal cost (routine can modify segment data). */
			segment_cost += Yapf().SignalCost(n, cur.tile, cur.td);

			/* Reserved tiles. */
			segment_cost += Yapf().ReservationCost(n, cur.tile, cur.td, tf->m_tiles_skipped);

			end_segment_reason = segment.m_end_segment_reason;

			/* Tests for 'potential target' reasons to close the segment. */
			if (cur.tile == prev.tile) {
				/* Penalty for reversing in a depot. */
				assert(IsSmallRailDepot(cur.tile));
				segment_cost += Yapf().PfGetSettings().rail_depot_reverse_penalty;

			} else if (IsSmallRailDepotTile(cur.tile)) {
				/* We will end in this pass (depot is possible target) */
				end_segment_reason |= ESRB_DEPOT;
			} else if (cur.tile_type == MP_STATION && IsRailWaypoint(cur.tile)) {
				if (v->current_order.IsType(OT_GOTO_WAYPOINT) &&
						GetStationIndex(cur.tile) == v->current_order.GetDestination() &&
						!Waypoint::Get(v->current_order.GetDestination())->IsSingleTile()) {
					/* This waypoint is our destination; maybe this isn't an unreserved
					 * one, so check that and if so see that as the last signal being
					 * red. This way waypoints near stations should work better. */
					CFollowTrackRail ft(v);
					TileIndex t = cur.tile;
					Trackdir td = cur.td;
					/* Arbitrary maximum tiles to follow to avoid infinite loops. */
					uint max_tiles = 20;
					while (ft.Follow(t, td)) {
						assert(t != ft.m_new_tile);
						t = ft.m_new_tile;
						if (t == cur.tile || --max_tiles == 0) {
							/* We looped back on ourself or found another loop, bail out. */
							td = INVALID_TRACKDIR;
							break;
						}
						if (KillFirstBit(ft.m_new_td_bits) != TRACKDIR_BIT_NONE) {
							/* We encountered a junction; it's going to be too complex to
							 * handle this perfectly, so just bail out. There is no simple
							 * free path, so try the other possibilities. */
							td = INVALID_TRACKDIR;
							break;
						}
						td = RemoveFirstTrackdir(&ft.m_new_td_bits);
						/* If this is a safe waiting position we're done searching for it */
						if (IsSafeWaitingPosition(v, t, td, true, _settings_game.pf.forbid_90_deg)) break;
					}

					/* In the case this platform is (possibly) occupied we add penalty so the
					 * other platforms of this waypoint are evaluated as well, i.e. we assume
					 * that there is a red signal in the waypoint when it's occupied. */
					if (td == INVALID_TRACKDIR ||
							!IsSafeWaitingPosition(v, t, td, true, _settings_game.pf.forbid_90_deg) ||
							!IsWaitingPositionFree(v, t, td, _settings_game.pf.forbid_90_deg)) {
						extra_cost += Yapf().PfGetSettings().rail_lastred_penalty;
					}
				}
				/* Waypoint is also a good reason to finish. */
				end_segment_reason |= ESRB_WAYPOINT;

			} else if (tf->m_is_station) {
				/* Station penalties. */
				uint platform_length = tf->m_tiles_skipped + 1;
				/* We don't know yet if the station is our target or not. Act like
				 * if it is pass-through station (not our destination). */
				segment_cost += Yapf().PfGetSettings().rail_station_penalty * platform_length;
				/* We will end in this pass (station is possible target) */
				end_segment_reason |= ESRB_STATION;

			} else if (TrackFollower::DoTrackMasking() && cur.tile_type == MP_RAILWAY) {
				/* Searching for a safe tile? */
				if (HasSignalOnTrackdir(cur.tile, cur.td) && !IsPbsSignal(GetSignalType(cur.tile, TrackdirToTrack(cur.td)))) {
					end_segment_reason |= ESRB_SAFE_TILE;
				}
			}

			/* Apply min/max speed penalties only when inside the look-ahead radius. Otherwise
			 * it would cause desync in MP. */
			if (n.m_num_signals_passed < m_sig_look_ahead_costs.size())
			{
				int min_speed = 0;
				int max_speed = tf->GetSpeedLimit(&min_speed);
				int max_veh_speed = std::min<int>(v->GetDisplayMaxSpeed(), v->current_order.GetMaxSpeed());
				if (max_speed < max_veh_speed) {
					extra_cost += YAPF_TILE_LENGTH * (max_veh_speed - max_speed) * (4 + tf->m_tiles_skipped) / max_veh_speed;
				}
				if (min_speed > max_veh_speed) {
					extra_cost += YAPF_TILE_LENGTH * (min_speed - max_veh_speed);
				}
			}

			/* Finish if we already exceeded the maximum path cost (i.e. when
			 * searching for the nearest depot). */
			if (m_max_cost > 0 && (parent_cost + segment_entry_cost + segment_cost) > m_max_cost) {
				end_segment_reason |= ESRB_PATH_TOO_LONG;
			}

			/* Move to the next tile/trackdir. */
			tf = &tf_local;
			tf_local.Init(v, Yapf().GetCompatibleRailTypes());

			if (!tf_local.Follow(cur.tile, cur.td)) {
				assert(tf_local.m_err != TrackFollower::EC_NONE);
				/* Can't move to the next tile (EOL?). */
				if (tf_local.m_err == TrackFollower::EC_RAIL_ROAD_TYPE) {
					end_segment_reason |= ESRB_RAIL_TYPE;
				} else {
					end_segment_reason |= ESRB_DEAD_END;
				}

				if (TrackFollower::DoTrackMasking() && !HasOnewaySignalBlockingTrackdir(cur.tile, cur.td)) {
					end_segment_reason |= ESRB_SAFE_TILE;
				}
				break;
			}

			/* Check if the next tile is not a choice. */
			if (KillFirstBit(tf_local.m_new_td_bits) != TRACKDIR_BIT_NONE) {
				/* More than one segment will follow. Close this one. */
				end_segment_reason |= ESRB_CHOICE_FOLLOWS;
				break;
			}

			/* Gather the next tile/trackdir/tile_type/rail_type. */
			TILE next(tf_local.m_new_tile, (Trackdir)FindFirstBit2x64(tf_local.m_new_td_bits));

			if (TrackFollower::DoTrackMasking() && IsTileType(next.tile, MP_RAILWAY)) {
				if (HasSignalOnTrackdir(next.tile, next.td) && IsPbsSignal(GetSignalType(next.tile, TrackdirToTrack(next.td)))) {
					/* Possible safe tile. */
					end_segment_reason |= ESRB_SAFE_TILE;
				} else if (HasSignalOnTrackdir(next.tile, ReverseTrackdir(next.td)) && GetSignalType(next.tile, TrackdirToTrack(next.td)) == SIGTYPE_PBS_ONEWAY) {
					/* Possible safe tile, but not so good as it's the back of a signal... */
					end_segment_reason |= ESRB_SAFE_TILE | ESRB_DEAD_END;
					extra_cost += Yapf().PfGetSettings().rail_lastred_exit_penalty;
				}
			}

			/* Check the next tile for the rail type. */
			if (next.rail_type != cur.rail_type) {
				/* Segment must consist from the same rail_type tiles. */
				end_segment_reason |= ESRB_RAIL_TYPE;
				break;
			}

			/* Avoid infinite looping. */
			if (next.tile == n.m_key.m_tile && next.td == n.m_key.m_td) {
				end_segment_reason |= ESRB_INFINITE_LOOP;
				break;
			}

			if (segment_cost > s_max_segment_cost) {
				/* Potentially in the infinite loop (or only very long segment?). We should
				 * not force it to finish prematurely unless we are on a regular tile. */
				if (IsTileType(tf->m_new_tile, MP_RAILWAY)) {
					end_segment_reason |= ESRB_SEGMENT_TOO_LONG;
					break;
				}
			}

			/* Any other reason bit set? */
			if (end_segment_reason != ESRB_NONE) {
				break;
			}

			/* For the next loop set new prev and cur tile info. */
			prev = cur;
			cur = next;

		} // for (;;)

		/* Don't consider path any further it if exceeded max_cost. */
		if (end_segment_reason & ESRB_PATH_TOO_LONG) return false;

		bool target_seen = false;
		if ((end_segment_reason & ESRB_POSSIBLE_TARGET) != ESRB_NONE) {
			/* Depot, station or waypoint. */
			if (Yapf().PfDetectDestination(cur.tile, cur.td)) {
				/* Destination found. */
				target_seen = true;
			}
		}

		/* Update the segment if needed. */
		if (!is_cached_segment) {
			/* Write back the segment information so it can be reused the next time. */
			segment.m_cost = segment_cost;
			segment.m_end_segment_reason = end_segment_reason & ESRB_CACHED_MASK;
			/* Save end of segment back to the node. */
			n.SetLastTileTrackdir(cur.tile, cur.td);
		}

		/* Do we have an excuse why not to continue pathfinding in this direction? */
		if (!target_seen && (end_segment_reason & ESRB_ABORT_PF_MASK) != ESRB_NONE) {
			/* Reason to not continue. Stop this PF branch. */
			return false;
		}

		/* Special costs for the case we have reached our target. */
		if (target_seen) {
			n.flags_u.flags_s.m_targed_seen = true;
			/* Last-red and last-red-exit penalties. */
			if (n.flags_u.flags_s.m_last_signal_was_red) {
				if (n.m_last_red_signal_type == SIGTYPE_EXIT) {
					/* last signal was red pre-signal-exit */
					extra_cost += Yapf().PfGetSettings().rail_lastred_exit_penalty;
				} else if (!IsPbsSignal(n.m_last_red_signal_type)) {
					/* Last signal was red, but not exit or path signal. */
					extra_cost += Yapf().PfGetSettings().rail_lastred_penalty;
				}
			}

			/* Platform-length penalty. */
			if ((end_segment_reason & ESRB_STATION) != ESRB_NONE) {
				assert(HasStationTileRail(n.GetLastTile()));
				uint platform_length = GetPlatformLength(n.GetLastTile(), ReverseDiagDir(TrackdirToExitdir(n.GetLastTrackdir())));
				/* Reduce the extra cost caused by passing-platform penalty (each platform receives it in the segment cost). */
				extra_cost -= Yapf().PfGetSettings().rail_station_penalty * platform_length;
				/* Add penalty for the inappropriate platform length. */
				extra_cost += PlatformLengthPenalty(platform_length);
			}
		}

		/* total node cost */
		n.m_cost = parent_cost + segment_entry_cost + segment_cost + extra_cost;

		return true;
	}

	inline bool CanUseGlobalCache(Node &n) const
	{
		return !m_disable_cache
			&& (n.m_parent != nullptr)
			&& (n.m_parent->m_num_signals_passed >= m_sig_look_ahead_costs.size());
	}

	inline void ConnectNodeToCachedData(Node &n, CachedData &ci)
	{
		n.m_segment = &ci;
		if (n.m_segment->m_cost < 0) {
			n.m_segment->m_last_tile = n.m_key.m_tile;
			n.m_segment->m_last_td = n.m_key.m_td;
		}
	}

	void DisableCache(bool disable)
	{
		m_disable_cache = disable;
	}
};

#endif /* YAPF_COSTRAIL_HPP */
