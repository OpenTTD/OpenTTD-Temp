/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file yapf_road.cpp The road pathfinding. */

#include "../../stdafx.h"
#include "yapf.hpp"
#include "yapf_node_road.hpp"
#include "../../roadstop_base.h"

#include "../../safeguards.h"


template <class Types>
class CYapfCostRoadT
{
public:
	typedef typename Types::Tpf Tpf; ///< pathfinder (derived from THIS class)
	typedef typename Types::TrackFollower TrackFollower; ///< track follower helper
	typedef typename Types::NodeList::Titem Node; ///< this will be our node type
	typedef typename Node::Key Key;    ///< key to hash tables

protected:
	int m_max_cost;

	CYapfCostRoadT() : m_max_cost(0) {};

	/** to access inherited path finder */
	Tpf& Yapf()
	{
		return *static_cast<Tpf *>(this);
	}

	int SlopeCost(TileIndex tile, TileIndex next_tile, Trackdir trackdir)
	{
		/* height of the center of the current tile */
		int x1 = TileX(tile) * TILE_SIZE;
		int y1 = TileY(tile) * TILE_SIZE;
		int z1 = GetSlopePixelZ(x1 + TILE_SIZE / 2, y1 + TILE_SIZE / 2);

		/* height of the center of the next tile */
		int x2 = TileX(next_tile) * TILE_SIZE;
		int y2 = TileY(next_tile) * TILE_SIZE;
		int z2 = GetSlopePixelZ(x2 + TILE_SIZE / 2, y2 + TILE_SIZE / 2);

		if (z2 - z1 > 1) {
			/* Slope up */
			return Yapf().PfGetSettings().road_slope_penalty;
		}
		return 0;
	}

	/** return one tile cost */
	inline int OneTileCost(TileIndex tile, Trackdir trackdir)
	{
		int cost = 0;
		/* set base cost */
		if (IsDiagonalTrackdir(trackdir)) {
			cost += YAPF_TILE_LENGTH;
			switch (GetTileType(tile)) {
				case MP_ROAD:
					/* Increase the cost for level crossings */
					if (IsLevelCrossing(tile)) {
						cost += Yapf().PfGetSettings().road_crossing_penalty;
					}
					break;

				case MP_STATION: {
					const RoadStop *rs = RoadStop::GetByTile(tile, GetRoadStopType(tile));
					if (IsDriveThroughStopTile(tile)) {
						/* Increase the cost for drive-through road stops */
						cost += Yapf().PfGetSettings().road_stop_penalty;
						DiagDirection dir = TrackdirToExitdir(trackdir);
						if (!RoadStop::IsDriveThroughRoadStopContinuation(tile, tile - TileOffsByDiagDir(dir))) {
							/* When we're the first road stop in a 'queue' of them we increase
							 * cost based on the fill percentage of the whole queue. */
							const RoadStop::Entry *entry = rs->GetEntry(dir);
							cost += entry->GetOccupied() * Yapf().PfGetSettings().road_stop_occupied_penalty / entry->GetLength();
						}
					} else {
						/* Increase cost for filled road stops */
						cost += Yapf().PfGetSettings().road_stop_bay_occupied_penalty * (!rs->IsFreeBay(0) + !rs->IsFreeBay(1)) / 2;
					}
					break;
				}

				default:
					break;
			}
		} else {
			/* non-diagonal trackdir */
			cost = YAPF_TILE_CORNER_LENGTH + Yapf().PfGetSettings().road_curve_penalty;
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
		int segment_cost = 0;
		uint tiles = 0;
		/* start at n.m_key.m_tile / n.m_key.m_td and walk to the end of segment */
		TileIndex tile = n.m_key.m_tile;
		Trackdir trackdir = n.m_key.m_td;
		int parent_cost = (n.m_parent != nullptr) ? n.m_parent->m_cost : 0;

		for (;;) {
			/* base tile cost depending on distance between edges */
			segment_cost += Yapf().OneTileCost(tile, trackdir);

			const RoadVehicle *v = Yapf().GetVehicle();
			/* we have reached the vehicle's destination - segment should end here to avoid target skipping */
			if (Yapf().PfDetectDestinationTile(tile, trackdir)) break;

			/* Finish if we already exceeded the maximum path cost (i.e. when
			 * searching for the nearest depot). */
			if (m_max_cost > 0 && (parent_cost + segment_cost) > m_max_cost) {
				return false;
			}

			/* stop if we have just entered the depot */
			if (IsRoadDepotTile(tile) && trackdir == DiagDirToDiagTrackdir(ReverseDiagDir(GetRoadDepotDirection(tile)))) {
				/* next time we will reverse and leave the depot */
				break;
			}

			/* if there are no reachable trackdirs on new tile, we have end of road */
			TrackFollower F(Yapf().GetVehicle());
			if (!F.Follow(tile, trackdir)) break;

			/* if there are more trackdirs available & reachable, we are at the end of segment */
			if (KillFirstBit(F.m_new_td_bits) != TRACKDIR_BIT_NONE) break;

			Trackdir new_td = (Trackdir)FindFirstBit2x64(F.m_new_td_bits);

			/* stop if RV is on simple loop with no junctions */
			if (F.m_new_tile == n.m_key.m_tile && new_td == n.m_key.m_td) return false;

			/* if we skipped some tunnel tiles, add their cost */
			segment_cost += F.m_tiles_skipped * YAPF_TILE_LENGTH;
			tiles += F.m_tiles_skipped + 1;

			/* add hilly terrain penalty */
			segment_cost += Yapf().SlopeCost(tile, F.m_new_tile, trackdir);

			/* add min/max speed penalties */
			int min_speed = 0;
			int max_veh_speed = std::min<int>(v->GetDisplayMaxSpeed(), v->current_order.GetMaxSpeed() * 2);
			int max_speed = F.GetSpeedLimit(&min_speed);
			if (max_speed < max_veh_speed) segment_cost += YAPF_TILE_LENGTH * (max_veh_speed - max_speed) * (4 + F.m_tiles_skipped) / max_veh_speed;
			if (min_speed > max_veh_speed) segment_cost += YAPF_TILE_LENGTH * (min_speed - max_veh_speed);

			/* move to the next tile */
			tile = F.m_new_tile;
			trackdir = new_td;
			if (tiles > MAX_MAP_SIZE) break;
		}

		/* save end of segment back to the node */
		n.m_segment_last_tile = tile;
		n.m_segment_last_td = trackdir;

		/* save also tile cost */
		n.m_cost = parent_cost + segment_cost;
		return true;
	}
};


template <class Types>
class CYapfDestinationAnyDepotRoadT
{
public:
	typedef typename Types::Tpf Tpf;                     ///< the pathfinder class (derived from THIS class)
	typedef typename Types::TrackFollower TrackFollower;
	typedef typename Types::NodeList::Titem Node;        ///< this will be our node type
	typedef typename Node::Key Key;                      ///< key to hash tables

	/** to access inherited path finder */
	Tpf& Yapf()
	{
		return *static_cast<Tpf *>(this);
	}

	/** Called by YAPF to detect if node ends in the desired destination */
	inline bool PfDetectDestination(Node &n)
	{
		return IsRoadDepotTile(n.m_segment_last_tile);
	}

	inline bool PfDetectDestinationTile(TileIndex tile, Trackdir trackdir)
	{
		return IsRoadDepotTile(tile);
	}

	/**
	 * Called by YAPF to calculate cost estimate. Calculates distance to the destination
	 *  adds it to the actual cost from origin and stores the sum to the Node::m_estimate
	 */
	inline bool PfCalcEstimate(Node &n)
	{
		n.m_estimate = n.m_cost;
		return true;
	}
};


template <class Types>
class CYapfDestinationTileRoadT
{
public:
	typedef typename Types::Tpf Tpf;                     ///< the pathfinder class (derived from THIS class)
	typedef typename Types::TrackFollower TrackFollower;
	typedef typename Types::NodeList::Titem Node;        ///< this will be our node type
	typedef typename Node::Key Key;                      ///< key to hash tables

protected:
	TileIndex    m_destTile;
	TrackdirBits m_destTrackdirs;
	StationID    m_dest_station;
	DepotID      m_dest_depot;
	bool         m_bus;
	bool         m_non_artic;

public:
	void SetDestination(const RoadVehicle *v)
	{
		if (v->current_order.IsType(OT_GOTO_STATION)) {
			m_dest_station  = v->current_order.GetDestination();
			m_bus           = v->IsBus();
			m_destTile      = CalcClosestStationTile(m_dest_station, v->tile, m_bus ? STATION_BUS : STATION_TRUCK);
			m_non_artic     = !v->HasArticulatedPart();
			m_destTrackdirs = INVALID_TRACKDIR_BIT;
		} else if (v->current_order.IsType(OT_GOTO_DEPOT)) {
			m_dest_station  = INVALID_STATION;
			m_dest_depot    = v->current_order.GetDestination();
			m_destTile      = CalcClosestDepotTile(m_dest_depot, v->tile);
			m_destTrackdirs = INVALID_TRACKDIR_BIT;
		} else {
			m_dest_station  = INVALID_STATION;
			m_dest_depot    = INVALID_DEPOT;
			m_destTile      = v->dest_tile;
			m_destTrackdirs = TrackStatusToTrackdirBits(GetTileTrackStatus(v->dest_tile, TRANSPORT_ROAD, GetRoadTramType(v->roadtype)));
		}
	}

	const Station *GetDestinationStation() const
	{
		return m_dest_station != INVALID_STATION ? Station::GetIfValid(m_dest_station) : nullptr;
	}

protected:
	/** to access inherited path finder */
	Tpf& Yapf()
	{
		return *static_cast<Tpf *>(this);
	}

public:
	/** Called by YAPF to detect if node ends in the desired destination */
	inline bool PfDetectDestination(Node &n)
	{
		return PfDetectDestinationTile(n.m_segment_last_tile, n.m_segment_last_td);
	}

	inline bool PfDetectDestinationTile(TileIndex tile, Trackdir trackdir)
	{
		if (m_dest_station != INVALID_STATION) {
			return IsTileType(tile, MP_STATION) &&
				GetStationIndex(tile) == m_dest_station &&
				(m_bus ? IsBusStop(tile) : IsTruckStop(tile)) &&
				(m_non_artic || IsDriveThroughStopTile(tile));
		}

		if (m_dest_depot != INVALID_DEPOT) {
			return IsRoadDepotTile(tile) &&
				GetDepotIndex(tile) == m_dest_depot;
		}

		return tile == m_destTile && HasTrackdir(m_destTrackdirs, trackdir);
	}

	/**
	 * Called by YAPF to calculate cost estimate. Calculates distance to the destination
	 *  adds it to the actual cost from origin and stores the sum to the Node::m_estimate
	 */
	inline bool PfCalcEstimate(Node &n)
	{
		static const int dg_dir_to_x_offs[] = {-1, 0, 1, 0};
		static const int dg_dir_to_y_offs[] = {0, 1, 0, -1};
		if (PfDetectDestination(n)) {
			n.m_estimate = n.m_cost;
			return true;
		}

		TileIndex tile = n.m_segment_last_tile;
		DiagDirection exitdir = TrackdirToExitdir(n.m_segment_last_td);
		int x1 = 2 * TileX(tile) + dg_dir_to_x_offs[(int)exitdir];
		int y1 = 2 * TileY(tile) + dg_dir_to_y_offs[(int)exitdir];
		int x2 = 2 * TileX(m_destTile);
		int y2 = 2 * TileY(m_destTile);
		int dx = abs(x1 - x2);
		int dy = abs(y1 - y2);
		int dmin = std::min(dx, dy);
		int dxy = abs(dx - dy);
		int d = dmin * YAPF_TILE_CORNER_LENGTH + (dxy - 1) * (YAPF_TILE_LENGTH / 2);
		n.m_estimate = n.m_cost + d;
		assert(n.m_estimate >= n.m_parent->m_estimate);
		return true;
	}
};



template <class Types>
class CYapfFollowRoadT
{
public:
	typedef typename Types::Tpf Tpf;                     ///< the pathfinder class (derived from THIS class)
	typedef typename Types::TrackFollower TrackFollower;
	typedef typename Types::NodeList::Titem Node;        ///< this will be our node type
	typedef typename Node::Key Key;                      ///< key to hash tables

protected:
	/** to access inherited path finder */
	inline Tpf& Yapf()
	{
		return *static_cast<Tpf *>(this);
	}

public:

	/**
	 * Called by YAPF to move from the given node to the next tile. For each
	 *  reachable trackdir on the new tile creates new node, initializes it
	 *  and adds it to the open list by calling Yapf().AddNewNode(n)
	 */
	inline void PfFollowNode(Node &old_node)
	{
		TrackFollower F(Yapf().GetVehicle());
		if (F.Follow(old_node.m_segment_last_tile, old_node.m_segment_last_td)) {
			Yapf().AddMultipleNodes(&old_node, F);
		}
	}

	/** return debug report character to identify the transportation type */
	inline char TransportTypeChar() const
	{
		return 'r';
	}

	static Trackdir stChooseRoadTrack(const RoadVehicle *v, TileIndex tile, DiagDirection enterdir, bool &path_found, RoadVehPathCache &path_cache)
	{
		Tpf pf;
		return pf.ChooseRoadTrack(v, tile, enterdir, path_found, path_cache);
	}

	inline Trackdir ChooseRoadTrack(const RoadVehicle *v, TileIndex tile, DiagDirection enterdir, bool &path_found, RoadVehPathCache &path_cache)
	{
		/* Handle special case - when next tile is destination tile.
		 * However, when going to a station the (initial) destination
		 * tile might not be a station, but a junction, in which case
		 * this method forces the vehicle to jump in circles. */
		if (tile == v->dest_tile && !v->current_order.IsType(OT_GOTO_STATION)) {
			/* choose diagonal trackdir reachable from enterdir */
			return DiagDirToDiagTrackdir(enterdir);
		}
		/* our source tile will be the next vehicle tile (should be the given one) */
		TileIndex src_tile = tile;
		/* get available trackdirs on the start tile */
		TrackdirBits src_trackdirs = GetTrackdirBitsForRoad(tile, GetRoadTramType(v->roadtype));
		/* select reachable trackdirs only */
		src_trackdirs &= DiagdirReachesTrackdirs(enterdir);

		/* set origin and destination nodes */
		Yapf().SetOrigin(src_tile, src_trackdirs);
		Yapf().SetDestination(v);

		/* find the best path */
		path_found = Yapf().FindPath(v);

		/* if path not found - return INVALID_TRACKDIR */
		Trackdir next_trackdir = INVALID_TRACKDIR;
		Node *pNode = Yapf().GetBestNode();
		if (pNode != nullptr) {
			uint steps = 0;
			for (Node *n = pNode; n->m_parent != nullptr; n = n->m_parent) steps++;

			/* path was found or at least suggested
			 * walk through the path back to its origin */
			while (pNode->m_parent != nullptr) {
				steps--;
				if (pNode->GetIsChoice() && steps < YAPF_ROADVEH_PATH_CACHE_SEGMENTS) {
					path_cache.td.push_front(pNode->GetTrackdir());
					path_cache.tile.push_front(pNode->GetTile());
				}
				pNode = pNode->m_parent;
			}
			/* return trackdir from the best origin node (one of start nodes) */
			Node &best_next_node = *pNode;
			assert(best_next_node.GetTile() == tile);
			next_trackdir = best_next_node.GetTrackdir();
			/* remove last element for the special case when tile == dest_tile */
			if (path_found && !path_cache.empty() && tile == v->dest_tile) {
				path_cache.td.pop_back();
				path_cache.tile.pop_back();
			}

			/* Check if target is a station, and cached path ends within 8 tiles of the dest tile */
			const Station *st = Yapf().GetDestinationStation();
			if (st) {
				const RoadStop *stop = st->GetPrimaryRoadStop(v);
				if (stop != nullptr && (IsDriveThroughStopTile(stop->xy) || stop->GetNextRoadStop(v) != nullptr)) {
					/* Destination station has at least 2 usable road stops, or first is a drive-through stop,
					 * trim end of path cache within a number of tiles of road stop tile area */
					TileArea non_cached_area = v->IsBus() ? st->bus_station : st->truck_station;
					non_cached_area.Expand(YAPF_ROADVEH_PATH_CACHE_DESTINATION_LIMIT);
					while (!path_cache.empty() && non_cached_area.Contains(path_cache.tile.back())) {
						path_cache.td.pop_back();
						path_cache.tile.pop_back();
					}
				}
			}
		}
		return next_trackdir;
	}

	static uint stDistanceToTile(const RoadVehicle *v, TileIndex tile)
	{
		Tpf pf;
		return pf.DistanceToTile(v, tile);
	}

	inline uint DistanceToTile(const RoadVehicle *v, TileIndex dst_tile)
	{
		/* handle special case - when current tile is the destination tile */
		if (dst_tile == v->tile) {
			/* distance is zero in this case */
			return 0;
		}

		if (!SetOriginFromVehiclePos(v)) return UINT_MAX;

		/* get available trackdirs on the destination tile */
		Yapf().SetDestination(v);

		/* if path not found - return distance = UINT_MAX */
		uint dist = UINT_MAX;

		/* find the best path */
		if (!Yapf().FindPath(v)) return dist;

		Node *pNode = Yapf().GetBestNode();
		if (pNode != nullptr) {
			/* path was found
			 * get the path cost estimate */
			dist = pNode->GetCostEstimate();
		}

		return dist;
	}

	/** Return true if the valid origin (tile/trackdir) was set from the current vehicle position. */
	inline bool SetOriginFromVehiclePos(const RoadVehicle *v)
	{
		/* set origin (tile, trackdir) */
		TileIndex src_tile = v->tile;
		Trackdir src_td = v->GetVehicleTrackdir();
		if (!HasTrackdir(GetTrackdirBitsForRoad(src_tile, this->IsTram() ? RTT_TRAM : RTT_ROAD), src_td)) {
			/* sometimes the roadveh is not on the road (it resides on non-existing track)
			 * how should we handle that situation? */
			return false;
		}
		Yapf().SetOrigin(src_tile, TrackdirToTrackdirBits(src_td));
		return true;
	}

	static FindDepotData stFindNearestDepot(const RoadVehicle *v, TileIndex tile, Trackdir td, int max_distance)
	{
		Tpf pf;
		return pf.FindNearestDepot(v, tile, td, max_distance);
	}

	/**
	 * Find the best depot for a road vehicle.
	 * @param v Vehicle
	 * @param tile Tile of the vehicle.
	 * @param td Trackdir of the vehicle.
	 * @param max_distance max length (penalty) for paths.
	 */
	inline FindDepotData FindNearestDepot(const RoadVehicle *v, TileIndex tile, Trackdir td, int max_distance)
	{
		/* Set origin. */
		Yapf().SetOrigin(tile, TrackdirToTrackdirBits(td));
		Yapf().SetMaxCost(max_distance);

		/* Find the best path and return if no depot is found. */
		if (!Yapf().FindPath(v)) return FindDepotData();

		/* Return the cost of the best path and its depot. */
		Node *n = Yapf().GetBestNode();
		return FindDepotData(n->m_segment_last_tile, n->m_cost);
	}
};

template <class Tpf_, class Tnode_list, template <class Types> class Tdestination>
struct CYapfRoad_TypesT
{
	typedef CYapfRoad_TypesT<Tpf_, Tnode_list, Tdestination>  Types;

	typedef Tpf_                              Tpf;
	typedef CFollowTrackRoad                  TrackFollower;
	typedef Tnode_list                        NodeList;
	typedef RoadVehicle                       VehicleType;
	typedef CYapfBaseT<Types>                 PfBase;
	typedef CYapfFollowRoadT<Types>           PfFollow;
	typedef CYapfOriginTileT<Types>           PfOrigin;
	typedef Tdestination<Types>               PfDestination;
	typedef CYapfSegmentCostCacheNoneT<Types> PfCache;
	typedef CYapfCostRoadT<Types>             PfCost;
};

struct CYapfRoad1         : CYapfT<CYapfRoad_TypesT<CYapfRoad1        , CRoadNodeListTrackDir, CYapfDestinationTileRoadT    > > {};
struct CYapfRoad2         : CYapfT<CYapfRoad_TypesT<CYapfRoad2        , CRoadNodeListExitDir , CYapfDestinationTileRoadT    > > {};

struct CYapfRoadAnyDepot1 : CYapfT<CYapfRoad_TypesT<CYapfRoadAnyDepot1, CRoadNodeListTrackDir, CYapfDestinationAnyDepotRoadT> > {};
struct CYapfRoadAnyDepot2 : CYapfT<CYapfRoad_TypesT<CYapfRoadAnyDepot2, CRoadNodeListExitDir , CYapfDestinationAnyDepotRoadT> > {};


Trackdir YapfRoadVehicleChooseTrack(const RoadVehicle *v, TileIndex tile, DiagDirection enterdir, TrackdirBits trackdirs, bool &path_found, RoadVehPathCache &path_cache)
{
	/* default is YAPF type 2 */
	typedef Trackdir (*PfnChooseRoadTrack)(const RoadVehicle*, TileIndex, DiagDirection, bool &path_found, RoadVehPathCache &path_cache);
	PfnChooseRoadTrack pfnChooseRoadTrack = &CYapfRoad2::stChooseRoadTrack; // default: ExitDir, allow 90-deg

	/* check if non-default YAPF type should be used */
	if (_settings_game.pf.yapf.disable_node_optimization) {
		pfnChooseRoadTrack = &CYapfRoad1::stChooseRoadTrack; // Trackdir
	}

	Trackdir td_ret = pfnChooseRoadTrack(v, tile, enterdir, path_found, path_cache);
	return (td_ret != INVALID_TRACKDIR) ? td_ret : (Trackdir)FindFirstBit2x64(trackdirs);
}

FindDepotData YapfRoadVehicleFindNearestDepot(const RoadVehicle *v, int max_distance)
{
	TileIndex tile = v->tile;
	Trackdir trackdir = v->GetVehicleTrackdir();

	if (!HasTrackdir(GetTrackdirBitsForRoad(tile, GetRoadTramType(v->roadtype)), trackdir)) {
		return FindDepotData();
	}

	/* default is YAPF type 2 */
	typedef FindDepotData (*PfnFindNearestDepot)(const RoadVehicle*, TileIndex, Trackdir, int);
	PfnFindNearestDepot pfnFindNearestDepot = &CYapfRoadAnyDepot2::stFindNearestDepot;

	/* check if non-default YAPF type should be used */
	if (_settings_game.pf.yapf.disable_node_optimization) {
		pfnFindNearestDepot = &CYapfRoadAnyDepot1::stFindNearestDepot; // Trackdir
	}

	return pfnFindNearestDepot(v, tile, trackdir, max_distance);
}
