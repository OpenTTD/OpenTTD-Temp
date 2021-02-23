/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file roadstop_base.h Base class for roadstops. */

#ifndef ROADSTOP_BASE_H
#define ROADSTOP_BASE_H

#include "station_type.h"
#include "core/pool_type.hpp"
#include "core/bitmath_func.hpp"
#include "vehicle_type.h"
#include "newgrf_roadstop.h"

typedef Pool<RoadStop, RoadStopID, 32, 64000> RoadStopPool;
extern RoadStopPool _roadstop_pool;

/** A Stop for a Road Vehicle */
struct RoadStop : RoadStopPool::PoolItem<&_roadstop_pool> {
	enum RoadStopStatusFlags {
		RSSFB_BAY0_FREE  = 0, ///< Non-zero when bay 0 is free
		RSSFB_BAY1_FREE  = 1, ///< Non-zero when bay 1 is free
		RSSFB_BAY_COUNT  = 2, ///< Max. number of bays
		RSSFB_BASE_ENTRY = 6, ///< Non-zero when the entries on this road stop are the primary, i.e. the ones to delete
		RSSFB_ENTRY_BUSY = 7, ///< Non-zero when roadstop entry is busy
	};

	/** Container for each entry point of a drive through road stop */
	struct Entry {
	private:
		int length;      ///< The length of the stop in tile 'units'
		int occupied;    ///< The amount of occupied stop in tile 'units'

	public:
		friend struct RoadStop; ///< Oh yeah, the road stop may play with me.

		/** Create an entry */
		Entry() : length(0), occupied(0) {}

		/**
		 * Get the length of this drive through stop.
		 * @return the length in tile units.
		 */
		inline int GetLength() const
		{
			return this->length;
		}

		/**
		 * Get the amount of occupied space in this drive through stop.
		 * @return the occupied space in tile units.
		 */
		inline int GetOccupied() const
		{
			return this->occupied;
		}

		void Leave(const RoadVehicle *rv);
		void Enter(const RoadVehicle *rv);
		void CheckIntegrity(const RoadStop *rs) const;
		void Rebuild(const RoadStop *rs, int side = -1);
	};

	const RoadStopSpec *roadstopspec;

	TileIndex       xy;     ///< Position on the map
	byte            status; ///< Current status of the Stop, @see RoadStopSatusFlag. Access using *Bay and *Busy functions.
	struct RoadStop *next;  ///< Next stop of the given type at this station

	/** Initializes a RoadStop */
	inline RoadStop(TileIndex tile = INVALID_TILE) :
		xy(tile),
		status((1 << RSSFB_BAY_COUNT) - 1)
	{ }

	~RoadStop();

	/**
	 * Checks whether there is a free bay in this road stop
	 * @return is at least one bay free?
	 */
	inline bool HasFreeBay() const
	{
		return GB(this->status, 0, RSSFB_BAY_COUNT) != 0;
	}

	/**
	 * Checks whether the given bay is free in this road stop
	 * @param nr bay to check
	 * @return is given bay free?
	 */
	inline bool IsFreeBay(uint nr) const
	{
		assert(nr < RSSFB_BAY_COUNT);
		return HasBit(this->status, nr);
	}

	/**
	 * Checks whether the entrance of the road stop is occupied by a vehicle
	 * @return is entrance busy?
	 */
	inline bool IsEntranceBusy() const
	{
		return HasBit(this->status, RSSFB_ENTRY_BUSY);
	}

	/**
	 * Makes an entrance occupied or free
	 * @param busy If true, marks busy; free otherwise.
	 */
	inline void SetEntranceBusy(bool busy)
	{
		SB(this->status, RSSFB_ENTRY_BUSY, 1, busy);
	}

	/**
	 * Get the drive through road stop entry struct for the given direction.
	 * @param dir The direction to get the entry for.
	 * @return the entry
	 */
	inline const Entry *GetEntry(DiagDirection dir) const
	{
		return HasBit((int)dir, 1) ? this->west : this->east;
	}

	/**
	 * Get the drive through road stop entry struct for the given direction.
	 * @param dir The direction to get the entry for.
	 * @return the entry
	 */
	inline Entry *GetEntry(DiagDirection dir)
	{
		return HasBit((int)dir, 1) ? this->west : this->east;
	}

	void MakeDriveThrough();
	void ClearDriveThrough();

	void Leave(RoadVehicle *rv);
	bool Enter(RoadVehicle *rv);

	RoadStop *GetNextRoadStop(const struct RoadVehicle *v) const;

	static RoadStop *GetByTile(TileIndex tile, RoadStopType type);

	static bool IsDriveThroughRoadStopContinuation(TileIndex rs, TileIndex next);

private:
	Entry *east; ///< The vehicles that entered from the east
	Entry *west; ///< The vehicles that entered from the west

	/**
	 * Allocates a bay
	 * @return the allocated bay number
	 * @pre this->HasFreeBay()
	 */
	inline uint AllocateBay()
	{
		assert(this->HasFreeBay());

		/* Find the first free bay. If the bit is set, the bay is free. */
		uint bay_nr = 0;
		while (!HasBit(this->status, bay_nr)) bay_nr++;

		ClrBit(this->status, bay_nr);
		return bay_nr;
	}

	/**
	 * Allocates a bay in a drive-through road stop
	 * @param nr the number of the bay to allocate
	 */
	inline void AllocateDriveThroughBay(uint nr)
	{
		assert(nr < RSSFB_BAY_COUNT);
		ClrBit(this->status, nr);
	}

	/**
	 * Frees the given bay
	 * @param nr the number of the bay to free
	 */
	inline void FreeBay(uint nr)
	{
		assert(nr < RSSFB_BAY_COUNT);
		SetBit(this->status, nr);
	}
};

#endif /* ROADSTOP_BASE_H */
