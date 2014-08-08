/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file yapf_destrail.hpp Determining the destination for rail vehicles. */

#ifndef YAPF_DESTRAIL_HPP
#define YAPF_DESTRAIL_HPP

class CYapfDestinationRailBase {
protected:
	RailTypes m_compatible_railtypes;

public:
	void SetDestination(const Train *v, bool override_rail_type = false)
	{
		m_compatible_railtypes = v->compatible_railtypes;
		if (override_rail_type) m_compatible_railtypes |= GetRailTypeInfo(v->railtype)->compatible_railtypes;
	}

	bool IsCompatibleRailType(RailType rt)
	{
		return HasBit(m_compatible_railtypes, rt);
	}

	RailTypes GetCompatibleRailTypes() const
	{
		return m_compatible_railtypes;
	}
};

template <class Types>
class CYapfDestinationAnyDepotRailT : public CYapfDestinationRailBase {
public:
	typedef typename Types::Tpf Tpf;              ///< the pathfinder class (derived from THIS class)
	typedef typename Types::NodeList::Titem Node; ///< this will be our node type
	typedef typename Node::Key Key;               ///< key to hash tables

	/** to access inherited path finder */
	Tpf& Yapf()
	{
		return *static_cast<Tpf *>(this);
	}

	/** Called by YAPF to detect if node ends in the desired destination */
	inline bool PfDetectDestination(Node &n)
	{
		return PfDetectDestination(n.GetLastTile(), n.GetLastTrackdir());
	}

	/** Called by YAPF to detect if node ends in the desired destination */
	inline bool PfDetectDestination(TileIndex tile, Trackdir td)
	{
		bool bDest = IsRailDepotTile(tile);
		return bDest;
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
class CYapfDestinationAnySafeTileRailT : public CYapfDestinationRailBase {
public:
	typedef typename Types::Tpf Tpf;              ///< the pathfinder class (derived from THIS class)
	typedef typename Types::NodeList::Titem Node; ///< this will be our node type
	typedef typename Node::Key Key;               ///< key to hash tables
	typedef typename Types::TrackFollower TrackFollower; ///< TrackFollower. Need to typedef for gcc 2.95

	/** to access inherited path finder */
	Tpf& Yapf()
	{
		return *static_cast<Tpf *>(this);
	}

	/** Called by YAPF to detect if node ends in the desired destination */
	inline bool PfDetectDestination(Node &n)
	{
		return PfDetectDestination(n.GetLastTile(), n.GetLastTrackdir());
	}

	/** Called by YAPF to detect if node ends in the desired destination */
	inline bool PfDetectDestination(TileIndex tile, Trackdir td)
	{
		return IsSafeWaitingPosition(Yapf().GetVehicle(), tile, td, true, !TrackFollower::Allow90degTurns()) &&
				IsWaitingPositionFree(Yapf().GetVehicle(), tile, td, !TrackFollower::Allow90degTurns());
	}

	/**
	 * Called by YAPF to calculate cost estimate. Calculates distance to the destination
	 *  adds it to the actual cost from origin and stores the sum to the Node::m_estimate.
	 */
	inline bool PfCalcEstimate(Node &n)
	{
		n.m_estimate = n.m_cost;
		return true;
	}
};

template <class Types>
class CYapfDestinationTileOrStationRailT : public CYapfDestinationRailBase {
public:
	typedef typename Types::Tpf Tpf;              ///< the pathfinder class (derived from THIS class)
	typedef typename Types::NodeList::Titem Node; ///< this will be our node type
	typedef typename Node::Key Key;               ///< key to hash tables

protected:
	TileIndex    m_destTile;
	TrackdirBits m_destTrackdirs;
	StationID    m_dest_station_id;
	DepotID      m_dest_depot_id;

	/** to access inherited path finder */
	Tpf& Yapf()
	{
		return *static_cast<Tpf *>(this);
	}

public:
	void SetDestination(const Train *v)
	{
		switch (v->current_order.GetType()) {
			case OT_GOTO_WAYPOINT:
				if (!Waypoint::Get(v->current_order.GetDestination())->IsSingleTile()) {
					/* In case of 'complex' waypoints we need to do a look
					 * ahead. This look ahead messes a bit about, which
					 * means that it 'corrupts' the cache. To prevent this
					 * we disable caching when we're looking for a complex
					 * waypoint. */
					Yapf().DisableCache(true);
				}
				FALLTHROUGH;

			case OT_GOTO_STATION:
				m_destTile = CalcClosestStationTile(v->current_order.GetDestination(), v->tile, v->current_order.IsType(OT_GOTO_STATION) ? STATION_RAIL : STATION_WAYPOINT);
				m_dest_station_id = v->current_order.GetDestination();
				m_destTrackdirs = INVALID_TRACKDIR_BIT;
				break;

			case OT_GOTO_DEPOT:
				m_dest_station_id = INVALID_STATION;
				m_dest_depot_id = v->current_order.GetDestination();
				assert(Depot::IsValidID(m_dest_depot_id));
				m_destTile = CalcClosestDepotTile(m_dest_depot_id, v->tile);
				m_destTrackdirs = INVALID_TRACKDIR_BIT;
				break;

			default:
				m_destTile = v->dest_tile;
				m_dest_station_id = INVALID_STATION;
				m_dest_depot_id = INVALID_DEPOT;
				m_destTrackdirs = TrackStatusToTrackdirBits(GetTileTrackStatus(v->dest_tile, TRANSPORT_RAIL, 0));
				break;
		}
		CYapfDestinationRailBase::SetDestination(v);
	}

	/** Called by YAPF to detect if node ends in the desired destination */
	inline bool PfDetectDestination(Node &n)
	{
		return PfDetectDestination(n.GetLastTile(), n.GetLastTrackdir());
	}

	/** Called by YAPF to detect if node ends in the desired destination */
	inline bool PfDetectDestination(TileIndex tile, Trackdir td)
	{
		if (m_dest_station_id != INVALID_STATION) {
			return HasStationTileRail(tile)
				&& (GetStationIndex(tile) == m_dest_station_id)
				&& (GetRailStationTrack(tile) == TrackdirToTrack(td));
		} else if (m_dest_depot_id != INVALID_DEPOT) {
			return IsRailDepotTile(tile)
				&& (GetDepotIndex(tile) == m_dest_depot_id)
				&& (GetRailDepotTrack(tile) == TrackdirToTrack(td));
		}

		return (tile == m_destTile) && HasTrackdir(m_destTrackdirs, td);
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

		TileIndex tile = n.GetLastTile();
		DiagDirection exitdir = TrackdirToExitdir(n.GetLastTrackdir());
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

#endif /* YAPF_DESTRAIL_HPP */
