/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file waypoint_base.h Base of waypoints. */

#ifndef WAYPOINT_BASE_H
#define WAYPOINT_BASE_H

#include "base_station_base.h"

/** Representation of a waypoint. */
struct Waypoint FINAL : SpecializedStation<Waypoint, true> {
	uint16 town_cn;    ///< The N-1th waypoint for this town (consecutive number)

	/**
	 * Create a waypoint at the given tile.
	 * @param tile The location of the waypoint.
	 */
	Waypoint(TileIndex tile = INVALID_TILE) : SpecializedStation<Waypoint, true>(tile) { }
	~Waypoint();

	void UpdateVirtCoord() override;

	void MoveSign(TileIndex new_xy) override;

	inline bool TileBelongsToRailStation(TileIndex tile) const override
	{
		return IsRailWaypointTile(tile) && GetStationIndex(tile) == this->index;
	}

	uint32 GetNewGRFVariable(const struct ResolverObject &object, byte variable, byte parameter, bool *available) const override;

	void GetTileArea(TileArea *ta, StationType type) const override;

	/**
	 * Is this a single tile waypoint?
	 * @return true if it is.
	 */
	inline bool IsSingleTile() const
	{
		return (this->facilities & FACIL_TRAIN) != 0 && this->train_station.w == 1 && this->train_station.h == 1;
	}

	/**
	 * Is the "type" of waypoint the same as the given waypoint,
	 * i.e. are both a rail waypoint or are both a buoy?
	 * @param wp The waypoint to compare to.
	 * @return true iff their types are equal.
	 */
	inline bool IsOfType(const Waypoint *wp) const
	{
		return this->string_id == wp->string_id;
	}
};

#endif /* WAYPOINT_BASE_H */
