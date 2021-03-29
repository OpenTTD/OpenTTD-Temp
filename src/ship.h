/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file ship.h Base for ships. */

#ifndef SHIP_H
#define SHIP_H

#include <deque>

#include "vehicle_base.h"
#include "water_map.h"

void GetShipSpriteSize(EngineID engine, uint &width, uint &height, int &xoffs, int &yoffs, EngineImageType image_type);
WaterClass GetEffectiveWaterClass(TileIndex tile);

typedef std::deque<Trackdir> ShipPathCache;

/**
 * All ships have this type.
 */
struct Ship FINAL : public SpecializedVehicle<Ship, VEH_SHIP> {
	TrackBits state;      ///< The "track" the ship is following.
	ShipPathCache path;   ///< Cached path.
	Direction rotation;   ///< Visible direction.
	int16 rotation_x_pos; ///< NOSAVE: X Position before rotation.
	int16 rotation_y_pos; ///< NOSAVE: Y Position before rotation.

	/** We don't want GCC to zero our struct! It already is zeroed and has an index! */
	Ship() : SpecializedVehicleBase() {}
	/** We want to 'destruct' the right class. */
	virtual ~Ship();

	void MarkDirty();
	void UpdateDeltaXY();
	ExpensesType GetExpenseType(bool income) const { return income ? EXPENSES_SHIP_INC : EXPENSES_SHIP_RUN; }
	void PlayLeaveStationSound() const;
	bool IsPrimaryVehicle() const { return true; }
	void GetImage(Direction direction, EngineImageType image_type, VehicleSpriteSeq *result) const;
	int GetDisplaySpeed() const { return this->cur_speed / 2; }
	int GetDisplayMaxSpeed() const { return this->vcache.cached_max_speed / 2; }
	int GetCurrentMaxSpeed() const { return std::min<int>(this->vcache.cached_max_speed, this->current_order.GetMaxSpeed() * 2); }
	Money GetRunningCost() const;
	bool IsInDepot() const { return HasBit((byte)this->state, TRACK_DEPOT); }
	bool Tick();
	void OnNewDay();
	Trackdir GetVehicleTrackdir() const;
	TileIndex GetOrderStationLocation(StationID station);
	bool FindClosestDepot(TileIndex *location, DestinationID *destination, bool *reverse);
	void UpdateCache();
	void SetDestTile(TileIndex tile);
};

bool IsShipDestinationTile(TileIndex tile, StationID station);

#endif /* SHIP_H */
