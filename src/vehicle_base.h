/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file  vehicle_base.h Base class for all vehicles. */

#ifndef VEHICLE_BASE_H
#define VEHICLE_BASE_H

#include "core/smallmap_type.hpp"
#include "track_type.h"
#include "command_type.h"
#include "order_base.h"
#include "cargopacket.h"
#include "texteff.hpp"
#include "engine_type.h"
#include "order_func.h"
#include "transport_type.h"
#include "group_type.h"
#include "base_consist.h"
#include "network/network.h"
#include <list>
#include <map>

/** Vehicle status bits in #Vehicle::vehstatus. */
enum VehStatus {
	VS_HIDDEN          = 0x01, ///< Vehicle is not visible.
	VS_STOPPED         = 0x02, ///< Vehicle is stopped by the player.
	VS_UNCLICKABLE     = 0x04, ///< Vehicle is not clickable by the user (shadow vehicles).
	VS_DEFPAL          = 0x08, ///< Use default vehicle palette. @see DoDrawVehicle
	VS_TRAIN_SLOWING   = 0x10, ///< Train is slowing down.
	VS_SHADOW          = 0x20, ///< Vehicle is a shadow vehicle.
	VS_AIRCRAFT_BROKEN = 0x40, ///< Aircraft is broken down.
	VS_CRASHED         = 0x80, ///< Vehicle is crashed.
};

/** Bit numbers in #Vehicle::vehicle_flags. */
enum VehicleFlags {
	VF_LOADING_FINISHED,        ///< Vehicle has finished loading.
	VF_CARGO_UNLOADING,         ///< Vehicle is unloading cargo.
	VF_BUILT_AS_PROTOTYPE,      ///< Vehicle is a prototype (accepted as exclusive preview).
	VF_TIMETABLE_STARTED,       ///< Whether the vehicle has started running on the timetable yet.
	VF_AUTOFILL_TIMETABLE,      ///< Whether the vehicle should fill in the timetable automatically.
	VF_AUTOFILL_PRES_WAIT_TIME, ///< Whether non-destructive auto-fill should preserve waiting times
	VF_STOP_LOADING,            ///< Don't load anymore during the next load cycle.
	VF_PATHFINDER_LOST,         ///< Vehicle's pathfinder is lost.
	VF_SERVINT_IS_CUSTOM,       ///< Service interval is custom.
	VF_SERVINT_IS_PERCENT,      ///< Service interval is percent.
};

/** Bit numbers used to indicate which of the #NewGRFCache values are valid. */
enum NewGRFCacheValidValues {
	NCVV_POSITION_CONSIST_LENGTH   = 0, ///< This bit will be set if the NewGRF var 40 currently stored is valid.
	NCVV_POSITION_SAME_ID_LENGTH   = 1, ///< This bit will be set if the NewGRF var 41 currently stored is valid.
	NCVV_CONSIST_CARGO_INFORMATION = 2, ///< This bit will be set if the NewGRF var 42 currently stored is valid.
	NCVV_COMPANY_INFORMATION       = 3, ///< This bit will be set if the NewGRF var 43 currently stored is valid.
	NCVV_POSITION_IN_VEHICLE       = 4, ///< This bit will be set if the NewGRF var 4D currently stored is valid.
	NCVV_END,                           ///< End of the bits.
};

/** Cached often queried (NewGRF) values */
struct NewGRFCache {
	/* Values calculated when they are requested for the first time after invalidating the NewGRF cache. */
	uint32 position_consist_length;   ///< Cache for NewGRF var 40.
	uint32 position_same_id_length;   ///< Cache for NewGRF var 41.
	uint32 consist_cargo_information; ///< Cache for NewGRF var 42. (Note: The cargotype is untranslated in the cache because the accessing GRF is yet unknown.)
	uint32 company_information;       ///< Cache for NewGRF var 43.
	uint32 position_in_vehicle;       ///< Cache for NewGRF var 4D.
	uint8  cache_valid;               ///< Bitset that indicates which cache values are valid.
};

/** Meaning of the various bits of the visual effect. */
enum VisualEffect {
	VE_OFFSET_START        = 0, ///< First bit that contains the offset (0 = front, 8 = centre, 15 = rear)
	VE_OFFSET_COUNT        = 4, ///< Number of bits used for the offset
	VE_OFFSET_CENTRE       = 8, ///< Value of offset corresponding to a position above the centre of the vehicle

	VE_TYPE_START          = 4, ///< First bit used for the type of effect
	VE_TYPE_COUNT          = 2, ///< Number of bits used for the effect type
	VE_TYPE_DEFAULT        = 0, ///< Use default from engine class
	VE_TYPE_STEAM          = 1, ///< Steam plumes
	VE_TYPE_DIESEL         = 2, ///< Diesel fumes
	VE_TYPE_ELECTRIC       = 3, ///< Electric sparks

	VE_DISABLE_EFFECT      = 6, ///< Flag to disable visual effect
	VE_ADVANCED_EFFECT     = VE_DISABLE_EFFECT, ///< Flag for advanced effects
	VE_DISABLE_WAGON_POWER = 7, ///< Flag to disable wagon power

	VE_DEFAULT = 0xFF,          ///< Default value to indicate that visual effect should be based on engine class
};

/** Models for spawning visual effects. */
enum VisualEffectSpawnModel {
	VESM_NONE              = 0, ///< No visual effect
	VESM_STEAM,                 ///< Steam model
	VESM_DIESEL,                ///< Diesel model
	VESM_ELECTRIC,              ///< Electric model

	VESM_END
};

/**
 * Enum to handle ground vehicle subtypes.
 * This is defined here instead of at #GroundVehicle because some common function require access to these flags.
 * Do not access it directly unless you have to. Use the subtype access functions.
 */
enum GroundVehicleSubtypeFlags {
	GVSF_FRONT            = 0, ///< Leading engine of a consist.
	GVSF_ARTICULATED_PART = 1, ///< Articulated part of an engine.
	GVSF_WAGON            = 2, ///< Wagon (not used for road vehicles).
	GVSF_ENGINE           = 3, ///< Engine that can be front engine, but might be placed behind another engine (not used for road vehicles).
	GVSF_FREE_WAGON       = 4, ///< First in a wagon chain (in depot) (not used for road vehicles).
	GVSF_MULTIHEADED      = 5, ///< Engine is multiheaded (not used for road vehicles).
};

/** Cached often queried values common to all vehicles. */
struct VehicleCache {
	uint16 cached_max_speed;        ///< Maximum speed of the consist (minimum of the max speed of all vehicles in the consist).
	uint16 cached_cargo_age_period; ///< Number of ticks before carried cargo is aged.

	byte cached_vis_effect;  ///< Visual effect to show (see #VisualEffect)
};

/** Sprite sequence for a vehicle part. */
struct VehicleSpriteSeq {
	PalSpriteID seq[4];
	uint count;

	bool operator==(const VehicleSpriteSeq &other) const
	{
		return this->count == other.count && MemCmpT<PalSpriteID>(this->seq, other.seq, this->count) == 0;
	}

	bool operator!=(const VehicleSpriteSeq &other) const
	{
		return !this->operator==(other);
	}

	/**
	 * Check whether the sequence contains any sprites.
	 */
	bool IsValid() const
	{
		return this->count != 0;
	}

	/**
	 * Clear all information.
	 */
	void Clear()
	{
		this->count = 0;
	}

	/**
	 * Assign a single sprite to the sequence.
	 */
	void Set(SpriteID sprite)
	{
		this->count = 1;
		this->seq[0].sprite = sprite;
		this->seq[0].pal = 0;
	}

	/**
	 * Copy data from another sprite sequence, while dropping all recolouring information.
	 */
	void CopyWithoutPalette(const VehicleSpriteSeq &src)
	{
		this->count = src.count;
		for (uint i = 0; i < src.count; ++i) {
			this->seq[i].sprite = src.seq[i].sprite;
			this->seq[i].pal = 0;
		}
	}

	void GetBounds(Rect *bounds) const;
	void Draw(int x, int y, PaletteID default_pal, bool force_pal) const;
};

/**
 * Cache for vehicle sprites and values relating to whether they should be updated before drawing,
 * or calculating the viewport.
 */
struct MutableSpriteCache {
	Direction last_direction;     ///< Last direction we obtained sprites for
	bool revalidate_before_draw;  ///< We need to do a GetImage() and check bounds before drawing this sprite
	Rect old_coord;               ///< Co-ordinates from the last valid bounding box
	bool is_viewport_candidate;   ///< This vehicle can potentially be drawn on a viewport
	VehicleSpriteSeq sprite_seq;  ///< Vehicle appearance.
};

/** A vehicle pool for a little over 1 million vehicles. */
typedef Pool<Vehicle, VehicleID, 512, 0xFF000> VehiclePool;
extern VehiclePool _vehicle_pool;

/* Some declarations of functions, so we can make them friendly */
struct SaveLoad;
struct GroundVehicleCache;
extern const SaveLoad *GetVehicleDescription(VehicleType vt);
struct LoadgameState;
extern bool LoadOldVehicle(LoadgameState *ls, int num);
extern void FixOldVehicles();

struct GRFFile;

/**
 * Simulated cargo type and capacity for prediction of future links.
 */
struct RefitDesc {
	CargoID cargo;    ///< Cargo type the vehicle will be carrying.
	uint16 capacity;  ///< Capacity the vehicle will have.
	uint16 remaining; ///< Capacity remaining from before the previous refit.
	RefitDesc(CargoID cargo, uint16 capacity, uint16 remaining) :
			cargo(cargo), capacity(capacity), remaining(remaining) {}
};

/** %Vehicle data structure. */
struct Vehicle : VehiclePool::PoolItem<&_vehicle_pool>, BaseVehicle, BaseConsist {
private:
	typedef std::list<RefitDesc> RefitList;
	typedef std::map<CargoID, uint> CapacitiesMap;

	Vehicle *next;                      ///< pointer to the next vehicle in the chain
	Vehicle *previous;                  ///< NOSAVE: pointer to the previous vehicle in the chain
	Vehicle *first;                     ///< NOSAVE: pointer to the first vehicle in the chain

	Vehicle *next_shared;               ///< pointer to the next vehicle that shares the order
	Vehicle *previous_shared;           ///< NOSAVE: pointer to the previous vehicle in the shared order chain

public:
	friend const SaveLoad *GetVehicleDescription(VehicleType vt); ///< So we can use private/protected variables in the saveload code
	friend void FixOldVehicles();
	friend void AfterLoadVehicles(bool part_of_load);             ///< So we can set the #previous and #first pointers while loading
	friend bool LoadOldVehicle(LoadgameState *ls, int num);       ///< So we can set the proper next pointer while loading

	TileIndex tile;                     ///< Current tile index

	/**
	 * Heading for this tile.
	 * For airports and train stations this tile does not necessarily belong to the destination station,
	 * but it can be used for heuristic purposes to estimate the distance.
	 */
	TileIndex dest_tile;

	Money profit_this_year;             ///< Profit this year << 8, low 8 bits are fract
	Money profit_last_year;             ///< Profit last year << 8, low 8 bits are fract
	Money value;                        ///< Value of the vehicle

	CargoPayment *cargo_payment;        ///< The cargo payment we're currently in

	mutable Rect coord;                 ///< NOSAVE: Graphical bounding box of the vehicle, i.e. what to redraw on moves.

	Vehicle *hash_viewport_next;        ///< NOSAVE: Next vehicle in the visual location hash.
	Vehicle **hash_viewport_prev;       ///< NOSAVE: Previous vehicle in the visual location hash.

	Vehicle *hash_tile_next;            ///< NOSAVE: Next vehicle in the tile location hash.
	Vehicle **hash_tile_prev;           ///< NOSAVE: Previous vehicle in the tile location hash.
	Vehicle **hash_tile_current;        ///< NOSAVE: Cache of the current hash chain.

	SpriteID colourmap;                 ///< NOSAVE: cached colour mapping

	/* Related to age and service time */
	Year build_year;                    ///< Year the vehicle has been built.
	Date age;                           ///< Age in days
	Date max_age;                       ///< Maximum age
	Date date_of_last_service;          ///< Last date the vehicle had a service at a depot.
	uint16 reliability;                 ///< Reliability.
	uint16 reliability_spd_dec;         ///< Reliability decrease speed.
	byte breakdown_ctr;                 ///< Counter for managing breakdown events. @see Vehicle::HandleBreakdown
	byte breakdown_delay;               ///< Counter for managing breakdown length.
	byte breakdowns_since_last_service; ///< Counter for the amount of breakdowns.
	byte breakdown_chance;              ///< Current chance of breakdowns.

	int32 x_pos;                        ///< x coordinate.
	int32 y_pos;                        ///< y coordinate.
	int32 z_pos;                        ///< z coordinate.
	Direction direction;                ///< facing

	Owner owner;                        ///< Which company owns the vehicle?
	/**
	 * currently displayed sprite index
	 * 0xfd == custom sprite, 0xfe == custom second head sprite
	 * 0xff == reserved for another custom sprite
	 */
	byte spritenum;
	byte x_extent;                      ///< x-extent of vehicle bounding box
	byte y_extent;                      ///< y-extent of vehicle bounding box
	byte z_extent;                      ///< z-extent of vehicle bounding box
	int8 x_bb_offs;                     ///< x offset of vehicle bounding box
	int8 y_bb_offs;                     ///< y offset of vehicle bounding box
	int8 x_offs;                        ///< x offset for vehicle sprite
	int8 y_offs;                        ///< y offset for vehicle sprite
	EngineID engine_type;               ///< The type of engine used for this vehicle.

	TextEffectID fill_percent_te_id;    ///< a text-effect id to a loading indicator object
	UnitID unitnumber;                  ///< unit number, for display purposes only

	uint16 cur_speed;                   ///< current speed
	byte subspeed;                      ///< fractional speed
	byte acceleration;                  ///< used by train & aircraft
	uint32 motion_counter;              ///< counter to occasionally play a vehicle sound.
	byte progress;                      ///< The percentage (if divided by 256) this vehicle already crossed the tile unit.

	byte random_bits;                   ///< Bits used for determining which randomized variational spritegroups to use when drawing.
	byte waiting_triggers;              ///< Triggers to be yet matched before rerandomizing the random bits.

	StationID last_station_visited;     ///< The last station we stopped at.
	StationID last_loading_station;     ///< Last station the vehicle has stopped at and could possibly leave from with any cargo loaded.

	CargoID cargo_type;                 ///< type of cargo this vehicle is carrying
	byte cargo_subtype;                 ///< Used for livery refits (NewGRF variations)
	uint16 cargo_cap;                   ///< total capacity
	uint16 refit_cap;                   ///< Capacity left over from before last refit.
	VehicleCargoList cargo;             ///< The cargo this vehicle is carrying
	uint16 cargo_age_counter;           ///< Ticks till cargo is aged next.
	int8 trip_occupancy;                ///< NOSAVE: Occupancy of vehicle of the current trip (updated after leaving a station).

	byte day_counter;                   ///< Increased by one for each day
	byte tick_counter;                  ///< Increased by one for each tick
	byte running_ticks;                 ///< Number of ticks this vehicle was not stopped this day

	byte vehstatus;                     ///< Status
	Order current_order;                ///< The current order (+ status, like: loading)

	union {
		OrderList *list;            ///< Pointer to the order list for this vehicle
		Order     *old;             ///< Only used during conversion of old save games
	} orders;                           ///< The orders currently assigned to the vehicle.

	uint16 load_unload_ticks;           ///< Ticks to wait before starting next cycle.
	GroupID group_id;                   ///< Index of group Pool array
	byte subtype;                       ///< subtype (Filled with values from #AircraftSubType/#DisasterSubType/#EffectVehicleType/#GroundVehicleSubtypeFlags)

	NewGRFCache grf_cache;              ///< Cache of often used calculated NewGRF values
	VehicleCache vcache;                ///< Cache of often used vehicle values.

	mutable MutableSpriteCache sprite_cache; ///< Cache of sprites and values related to recalculating them, see #MutableSpriteCache

	Vehicle(VehicleType type = VEH_INVALID);

	void PreDestructor();
	/** We want to 'destruct' the right class. */
	virtual ~Vehicle();

	void BeginLoading();
	void CancelReservation(StationID next, Station *st);
	void LeaveStation();

	GroundVehicleCache *GetGroundVehicleCache();
	const GroundVehicleCache *GetGroundVehicleCache() const;

	uint16 &GetGroundVehicleFlags();
	const uint16 &GetGroundVehicleFlags() const;

	void DeleteUnreachedImplicitOrders();

	void HandleLoading(bool mode = false);

	void HandleWaiting(bool stop_waiting = false);
	
	void GetConsistFreeCapacities(SmallMap<CargoID, uint> &capacities) const;

	uint GetConsistTotalCapacity() const;

	/**
	 * Marks the vehicles to be redrawn and updates cached variables
	 *
	 * This method marks the area of the vehicle on the screen as dirty.
	 * It can be use to repaint the vehicle.
	 *
	 * @ingroup dirty
	 */
	virtual void MarkDirty() {}

	/**
	 * Updates the x and y offsets and the size of the sprite used
	 * for this vehicle.
	 */
	virtual void UpdateDeltaXY() {}

	/**
	 * Determines the effective direction-specific vehicle movement speed.
	 *
	 * This method belongs to the old vehicle movement method:
	 * A vehicle moves a step every 256 progress units.
	 * The vehicle speed is scaled by 3/4 when moving in X or Y direction due to the longer distance.
	 *
	 * However, this method is slightly wrong in corners, as the leftover progress is not scaled correctly
	 * when changing movement direction. #GetAdvanceSpeed() and #GetAdvanceDistance() are better wrt. this.
	 *
	 * @param speed Direction-independent unscaled speed.
	 * @return speed scaled by movement direction. 256 units are required for each movement step.
	 */
	inline uint GetOldAdvanceSpeed(uint speed)
	{
		return (this->direction & 1) ? speed : speed * 3 / 4;
	}

	/**
	 * Determines the effective vehicle movement speed.
	 *
	 * Together with #GetAdvanceDistance() this function is a replacement for #GetOldAdvanceSpeed().
	 *
	 * A vehicle progresses independent of it's movement direction.
	 * However different amounts of "progress" are needed for moving a step in a specific direction.
	 * That way the leftover progress does not need any adaption when changing movement direction.
	 *
	 * @param speed Direction-independent unscaled speed.
	 * @return speed, scaled to match #GetAdvanceDistance().
	 */
	static inline uint GetAdvanceSpeed(uint speed)
	{
		return speed * 3 / 4;
	}

	/**
	 * Determines the vehicle "progress" needed for moving a step.
	 *
	 * Together with #GetAdvanceSpeed() this function is a replacement for #GetOldAdvanceSpeed().
	 *
	 * @return distance to drive for a movement step on the map.
	 */
	inline uint GetAdvanceDistance()
	{
		return (this->direction & 1) ? 192 : 256;
	}

	/**
	 * Sets the expense type associated to this vehicle type
	 * @param income whether this is income or (running) expenses of the vehicle
	 */
	virtual ExpensesType GetExpenseType(bool income) const { return EXPENSES_OTHER; }

	/**
	 * Play the sound associated with leaving the station
	 */
	virtual void PlayLeaveStationSound() const {}

	/**
	 * Whether this is the primary vehicle in the chain.
	 */
	virtual bool IsPrimaryVehicle() const { return false; }

	const Engine *GetEngine() const;

	/**
	 * Gets the sprite to show for the given direction
	 * @param direction the direction the vehicle is facing
	 * @param[out] result Vehicle sprite sequence.
	 */
	virtual void GetImage(Direction direction, EngineImageType image_type, VehicleSpriteSeq *result) const { result->Clear(); }

	const GRFFile *GetGRF() const;
	uint32 GetGRFID() const;

	/**
	 * Invalidates cached NewGRF variables
	 * @see InvalidateNewGRFCacheOfChain
	 */
	inline void InvalidateNewGRFCache()
	{
		this->grf_cache.cache_valid = 0;
	}

	/**
	 * Invalidates cached NewGRF variables of all vehicles in the chain (after the current vehicle)
	 * @see InvalidateNewGRFCache
	 */
	inline void InvalidateNewGRFCacheOfChain()
	{
		for (Vehicle *u = this; u != nullptr; u = u->Next()) {
			u->InvalidateNewGRFCache();
		}
	}

	/**
	 * Check if the vehicle is a ground vehicle.
	 * @return True iff the vehicle is a train or a road vehicle.
	 */
	inline bool IsGroundVehicle() const
	{
		return this->type == VEH_TRAIN || this->type == VEH_ROAD;
	}

	/**
	 * Gets the speed in km-ish/h that can be sent into SetDParam for string processing.
	 * @return the vehicle's speed
	 */
	virtual int GetDisplaySpeed() const { return 0; }

	/**
	 * Gets the maximum speed in km-ish/h that can be sent into SetDParam for string processing.
	 * @return the vehicle's maximum speed
	 */
	virtual int GetDisplayMaxSpeed() const { return 0; }

	/**
	 * Calculates the maximum speed of the vehicle under its current conditions.
	 * @return Current maximum speed in native units.
	 */
	virtual int GetCurrentMaxSpeed() const { return 0; }

	/**
	 * Gets the running cost of a vehicle
	 * @return the vehicle's running cost
	 */
	virtual Money GetRunningCost() const { return 0; }

	/**
	 * Check whether the vehicle is in the depot.
	 * @return true if and only if the vehicle is in the depot.
	 */
	virtual bool IsInDepot() const { return false; }

	/**
	 * Check whether the whole vehicle chain is in the depot.
	 * @return true if and only if the whole chain is in the depot.
	 */
	virtual bool IsChainInDepot() const { return this->IsInDepot(); }

	/**
	 * Check whether the vehicle is in the depot *and* stopped.
	 * @return true if and only if the vehicle is in the depot and stopped.
	 */
	bool IsStoppedInDepot() const
	{
		assert(this == this->First());
		/* Free wagons have no VS_STOPPED state */
		if (this->IsPrimaryVehicle() && !(this->vehstatus & VS_STOPPED)) return false;
		return this->IsChainInDepot();
	}

	/**
	 * Calls the tick handler of the vehicle
	 * @return is this vehicle still valid?
	 */
	virtual bool Tick() { return true; };

	/**
	 * Calls the new day handler of the vehicle
	 */
	virtual void OnNewDay() {};

	/**
	 * Crash the (whole) vehicle chain.
	 * @param flooded whether the cause of the crash is flooding or not.
	 * @return the number of lost souls.
	 */
	virtual uint Crash(bool flooded = false);

	/**
	 * Returns the Trackdir on which the vehicle is currently located.
	 * Works for trains and ships.
	 * Currently works only sortof for road vehicles, since they have a fuzzy
	 * concept of being "on" a trackdir. Dunno really what it returns for a road
	 * vehicle that is halfway a tile, never really understood that part. For road
	 * vehicles that are at the beginning or end of the tile, should just return
	 * the diagonal trackdir on which they are driving. I _think_.
	 * For other vehicles types, or vehicles with no clear trackdir (such as those
	 * in depots), returns 0xFF.
	 * @return the trackdir of the vehicle
	 */
	virtual Trackdir GetVehicleTrackdir() const { return INVALID_TRACKDIR; }

	/**
	 * Gets the running cost of a vehicle  that can be sent into SetDParam for string processing.
	 * @return the vehicle's running cost
	 */
	Money GetDisplayRunningCost() const { return (this->GetRunningCost() >> 8); }

	/**
	 * Gets the profit vehicle had this year. It can be sent into SetDParam for string processing.
	 * @return the vehicle's profit this year
	 */
	Money GetDisplayProfitThisYear() const { return (this->profit_this_year >> 8); }

	/**
	 * Gets the profit vehicle had last year. It can be sent into SetDParam for string processing.
	 * @return the vehicle's profit last year
	 */
	Money GetDisplayProfitLastYear() const { return (this->profit_last_year >> 8); }

	void SetNext(Vehicle *next);

	/**
	 * Get the next vehicle of this vehicle.
	 * @note articulated parts are also counted as vehicles.
	 * @return the next vehicle or nullptr when there isn't a next vehicle.
	 */
	inline Vehicle *Next() const { return this->next; }

	/**
	 * Get the previous vehicle of this vehicle.
	 * @note articulated parts are also counted as vehicles.
	 * @return the previous vehicle or nullptr when there isn't a previous vehicle.
	 */
	inline Vehicle *Previous() const { return this->previous; }

	/**
	 * Get the first vehicle of this vehicle chain.
	 * @return the first vehicle of the chain.
	 */
	inline Vehicle *First() const { return this->first; }

	/**
	 * Get the last vehicle of this vehicle chain.
	 * @return the last vehicle of the chain.
	 */
	inline Vehicle *Last()
	{
		Vehicle *v = this;
		while (v->Next() != nullptr) v = v->Next();
		return v;
	}

	/**
	 * Get the last vehicle of this vehicle chain.
	 * @return the last vehicle of the chain.
	 */
	inline const Vehicle *Last() const
	{
		const Vehicle *v = this;
		while (v->Next() != nullptr) v = v->Next();
		return v;
	}

	/**
	 * Get the vehicle at offset \a n of this vehicle chain.
	 * @param n Offset from the current vehicle.
	 * @return The new vehicle or nullptr if the offset is out-of-bounds.
	 */
	inline Vehicle *Move(int n)
	{
		Vehicle *v = this;
		if (n < 0) {
			for (int i = 0; i != n && v != nullptr; i--) v = v->Previous();
		} else {
			for (int i = 0; i != n && v != nullptr; i++) v = v->Next();
		}
		return v;
	}

	/**
	 * Get the vehicle at offset \a n of this vehicle chain.
	 * @param n Offset from the current vehicle.
	 * @return The new vehicle or nullptr if the offset is out-of-bounds.
	 */
	inline const Vehicle *Move(int n) const
	{
		const Vehicle *v = this;
		if (n < 0) {
			for (int i = 0; i != n && v != nullptr; i--) v = v->Previous();
		} else {
			for (int i = 0; i != n && v != nullptr; i++) v = v->Next();
		}
		return v;
	}

	/**
	 * Get the first order of the vehicles order list.
	 * @return first order of order list.
	 */
	inline Order *GetFirstOrder() const { return (this->orders.list == nullptr) ? nullptr : this->orders.list->GetFirstOrder(); }

	void AddToShared(Vehicle *shared_chain);
	void RemoveFromShared();

	/**
	 * Get the next vehicle of the shared vehicle chain.
	 * @return the next shared vehicle or nullptr when there isn't a next vehicle.
	 */
	inline Vehicle *NextShared() const { return this->next_shared; }

	/**
	 * Get the previous vehicle of the shared vehicle chain
	 * @return the previous shared vehicle or nullptr when there isn't a previous vehicle.
	 */
	inline Vehicle *PreviousShared() const { return this->previous_shared; }

	/**
	 * Get the first vehicle of this vehicle chain.
	 * @return the first vehicle of the chain.
	 */
	inline Vehicle *FirstShared() const { return (this->orders.list == nullptr) ? this->First() : this->orders.list->GetFirstSharedVehicle(); }

	/**
	 * Check if we share our orders with another vehicle.
	 * @return true if there are other vehicles sharing the same order
	 */
	inline bool IsOrderListShared() const { return this->orders.list != nullptr && this->orders.list->IsShared(); }

	/**
	 * Get the number of orders this vehicle has.
	 * @return the number of orders this vehicle has.
	 */
	inline VehicleOrderID GetNumOrders() const { return (this->orders.list == nullptr) ? 0 : this->orders.list->GetNumOrders(); }

	/**
	 * Get the number of manually added orders this vehicle has.
	 * @return the number of manually added orders this vehicle has.
	 */
	inline VehicleOrderID GetNumManualOrders() const { return (this->orders.list == nullptr) ? 0 : this->orders.list->GetNumManualOrders(); }

	/**
	 * Get the next station the vehicle will stop at.
	 * @return ID of the next station the vehicle will stop at or INVALID_STATION.
	 */
	inline StationIDStack GetNextStoppingStation() const
	{
		return (this->orders.list == nullptr) ? INVALID_STATION : this->orders.list->GetNextStoppingStation(this);
	}

	void ResetRefitCaps();

	/**
	 * Copy certain configurations and statistics of a vehicle after successful autoreplace/renew
	 * The function shall copy everything that cannot be copied by a command (like orders / group etc),
	 * and that shall not be resetted for the new vehicle.
	 * @param src The old vehicle
	 */
	inline void CopyVehicleConfigAndStatistics(const Vehicle *src)
	{
		this->CopyConsistPropertiesFrom(src);

		this->unitnumber = src->unitnumber;

		this->current_order = src->current_order;
		this->dest_tile  = src->dest_tile;

		this->profit_this_year = src->profit_this_year;
		this->profit_last_year = src->profit_last_year;
	}


	bool HandleBreakdown();

	bool NeedsAutorenewing(const Company *c, bool use_renew_setting = true) const;

	bool NeedsServicing() const;
	bool NeedsAutomaticServicing() const;

	/**
	 * Determine the location for the station where the vehicle goes to next.
	 * Things done for example are allocating slots in a road stop or exact
	 * location of the platform is determined for ships.
	 * @param station the station to make the next location of the vehicle.
	 * @return the location (tile) to aim for.
	 */
	virtual TileIndex GetOrderStationLocation(StationID station) { return INVALID_TILE; }

	/**
	 * Find the closest depot for this vehicle and tell us the location,
	 * DestinationID and whether we should reverse.
	 * @param location    where do we go to?
	 * @param destination what hangar do we go to?
	 * @param reverse     should the vehicle be reversed?
	 * @return true if a depot could be found.
	 */
	virtual bool FindClosestDepot(TileIndex *location, DestinationID *destination, bool *reverse) { return false; }

	virtual void SetDestTile(TileIndex tile) { this->dest_tile = tile; }

	CommandCost SendToDepot(DoCommandFlag flags, DepotCommand command);

	void UpdateVisualEffect(bool allow_power_change = true);
	void ShowVisualEffect() const;

	void UpdatePosition();
	void UpdateViewport(bool dirty);
	void UpdateBoundingBoxCoordinates(bool update_cache) const;
	void UpdatePositionAndViewport();
	bool MarkAllViewportsDirty() const;

	inline uint16 GetServiceInterval() const { return this->service_interval; }

	inline void SetServiceInterval(uint16 interval) { this->service_interval = interval; }

	inline bool ServiceIntervalIsCustom() const { return HasBit(this->vehicle_flags, VF_SERVINT_IS_CUSTOM); }

	inline bool ServiceIntervalIsPercent() const { return HasBit(this->vehicle_flags, VF_SERVINT_IS_PERCENT); }

	inline void SetServiceIntervalIsCustom(bool on) { SB(this->vehicle_flags, VF_SERVINT_IS_CUSTOM, 1, on); }

	inline void SetServiceIntervalIsPercent(bool on) { SB(this->vehicle_flags, VF_SERVINT_IS_PERCENT, 1, on); }

private:
	/**
	 * Advance cur_real_order_index to the next real order.
	 * cur_implicit_order_index is not touched.
	 */
	void SkipToNextRealOrderIndex()
	{
		if (this->GetNumManualOrders() > 0) {
			/* Advance to next real order */
			do {
				this->cur_real_order_index++;
				if (this->cur_real_order_index >= this->GetNumOrders()) this->cur_real_order_index = 0;
			} while (this->GetOrder(this->cur_real_order_index)->IsType(OT_IMPLICIT));
		} else {
			this->cur_real_order_index = 0;
		}
	}

public:
	/**
	 * Increments cur_implicit_order_index, keeps care of the wrap-around and invalidates the GUI.
	 * cur_real_order_index is incremented as well, if needed.
	 * Note: current_order is not invalidated.
	 */
	void IncrementImplicitOrderIndex()
	{
		if (this->cur_implicit_order_index == this->cur_real_order_index) {
			/* Increment real order index as well */
			this->SkipToNextRealOrderIndex();
		}

		assert(this->cur_real_order_index == 0 || this->cur_real_order_index < this->GetNumOrders());

		/* Advance to next implicit order */
		do {
			this->cur_implicit_order_index++;
			if (this->cur_implicit_order_index >= this->GetNumOrders()) this->cur_implicit_order_index = 0;
		} while (this->cur_implicit_order_index != this->cur_real_order_index && !this->GetOrder(this->cur_implicit_order_index)->IsType(OT_IMPLICIT));

		InvalidateVehicleOrder(this, 0);
	}

	/**
	 * Advanced cur_real_order_index to the next real order, keeps care of the wrap-around and invalidates the GUI.
	 * cur_implicit_order_index is incremented as well, if it was equal to cur_real_order_index, i.e. cur_real_order_index is skipped
	 * but not any implicit orders.
	 * Note: current_order is not invalidated.
	 */
	void IncrementRealOrderIndex()
	{
		if (this->cur_implicit_order_index == this->cur_real_order_index) {
			/* Increment both real and implicit order */
			this->IncrementImplicitOrderIndex();
		} else {
			/* Increment real order only */
			this->SkipToNextRealOrderIndex();
			InvalidateVehicleOrder(this, 0);
		}
	}

	/**
	 * Skip implicit orders until cur_real_order_index is a non-implicit order.
	 */
	void UpdateRealOrderIndex()
	{
		/* Make sure the index is valid */
		if (this->cur_real_order_index >= this->GetNumOrders()) this->cur_real_order_index = 0;

		if (this->GetNumManualOrders() > 0) {
			/* Advance to next real order */
			while (this->GetOrder(this->cur_real_order_index)->IsType(OT_IMPLICIT)) {
				this->cur_real_order_index++;
				if (this->cur_real_order_index >= this->GetNumOrders()) this->cur_real_order_index = 0;
			}
		} else {
			this->cur_real_order_index = 0;
		}
	}

	/**
	 * Returns order 'index' of a vehicle or nullptr when it doesn't exists
	 * @param index the order to fetch
	 * @return the found (or not) order
	 */
	inline Order *GetOrder(int index) const
	{
		return (this->orders.list == nullptr) ? nullptr : this->orders.list->GetOrderAt(index);
	}

	/**
	 * Returns the last order of a vehicle, or nullptr if it doesn't exists
	 * @return last order of a vehicle, if available
	 */
	inline Order *GetLastOrder() const
	{
		return (this->orders.list == nullptr) ? nullptr : this->orders.list->GetLastOrder();
	}

	bool IsEngineCountable() const;
	bool HasEngineType() const;
	bool HasDepotOrder() const;
	void HandlePathfindingResult(bool path_found);

	/**
	 * Check if the vehicle is a front engine.
	 * @return Returns true if the vehicle is a front engine.
	 */
	inline bool IsFrontEngine() const
	{
		return this->IsGroundVehicle() && HasBit(this->subtype, GVSF_FRONT);
	}

	/**
	 * Check if the vehicle is an articulated part of an engine.
	 * @return Returns true if the vehicle is an articulated part.
	 */
	inline bool IsArticulatedPart() const
	{
		return this->IsGroundVehicle() && HasBit(this->subtype, GVSF_ARTICULATED_PART);
	}

	/**
	 * Check if an engine has an articulated part.
	 * @return True if the engine has an articulated part.
	 */
	inline bool HasArticulatedPart() const
	{
		return this->Next() != nullptr && this->Next()->IsArticulatedPart();
	}

	/**
	 * Get the next part of an articulated engine.
	 * @return Next part of the articulated engine.
	 * @pre The vehicle is an articulated engine.
	 */
	inline Vehicle *GetNextArticulatedPart() const
	{
		assert(this->HasArticulatedPart());
		return this->Next();
	}

	/**
	 * Get the first part of an articulated engine.
	 * @return First part of the engine.
	 */
	inline Vehicle *GetFirstEnginePart()
	{
		Vehicle *v = this;
		while (v->IsArticulatedPart()) v = v->Previous();
		return v;
	}

	/**
	 * Get the first part of an articulated engine.
	 * @return First part of the engine.
	 */
	inline const Vehicle *GetFirstEnginePart() const
	{
		const Vehicle *v = this;
		while (v->IsArticulatedPart()) v = v->Previous();
		return v;
	}

	/**
	 * Get the last part of an articulated engine.
	 * @return Last part of the engine.
	 */
	inline Vehicle *GetLastEnginePart()
	{
		Vehicle *v = this;
		while (v->HasArticulatedPart()) v = v->GetNextArticulatedPart();
		return v;
	}

	/**
	 * Get the next real (non-articulated part) vehicle in the consist.
	 * @return Next vehicle in the consist.
	 */
	inline Vehicle *GetNextVehicle() const
	{
		const Vehicle *v = this;
		while (v->HasArticulatedPart()) v = v->GetNextArticulatedPart();

		/* v now contains the last articulated part in the engine */
		return v->Next();
	}

	/**
	 * Get the previous real (non-articulated part) vehicle in the consist.
	 * @return Previous vehicle in the consist.
	 */
	inline Vehicle *GetPrevVehicle() const
	{
		Vehicle *v = this->Previous();
		while (v != nullptr && v->IsArticulatedPart()) v = v->Previous();

		return v;
	}

	/**
	 * Iterator to iterate orders
	 * Supports deletion of current order
	 */
	struct OrderIterator {
		typedef Order value_type;
		typedef Order* pointer;
		typedef Order& reference;
		typedef size_t difference_type;
		typedef std::forward_iterator_tag iterator_category;

		explicit OrderIterator(OrderList *list) : list(list), prev(nullptr)
		{
			this->order = (this->list == nullptr) ? nullptr : this->list->GetFirstOrder();
		}

		bool operator==(const OrderIterator &other) const { return this->order == other.order; }
		bool operator!=(const OrderIterator &other) const { return !(*this == other); }
		Order * operator*() const { return this->order; }
		OrderIterator & operator++()
		{
			this->prev = (this->prev == nullptr) ? this->list->GetFirstOrder() : this->prev->next;
			this->order = (this->prev == nullptr) ? nullptr : this->prev->next;
			return *this;
		}

	private:
		OrderList *list;
		Order *order;
		Order *prev;
	};

	/**
	 * Iterable ensemble of orders
	 */
	struct IterateWrapper {
		OrderList *list;
		IterateWrapper(OrderList *list = nullptr) : list(list) {}
		OrderIterator begin() { return OrderIterator(this->list); }
		OrderIterator end() { return OrderIterator(nullptr); }
		bool empty() { return this->begin() == this->end(); }
	};

	/**
	 * Returns an iterable ensemble of orders of a vehicle
	 * @return an iterable ensemble of orders of a vehicle
	 */
	IterateWrapper Orders() const { return IterateWrapper(this->orders.list); }
};

/**
 * Class defining several overloaded accessors so we don't
 * have to cast vehicle types that often
 */
template <class T, VehicleType Type>
struct SpecializedVehicle : public Vehicle {
	static const VehicleType EXPECTED_TYPE = Type; ///< Specialized type

	typedef SpecializedVehicle<T, Type> SpecializedVehicleBase; ///< Our type

	/**
	 * Set vehicle type correctly
	 */
	inline SpecializedVehicle<T, Type>() : Vehicle(Type)
	{
		this->sprite_cache.sprite_seq.count = 1;
	}

	/**
	 * Get the first vehicle in the chain
	 * @return first vehicle in the chain
	 */
	inline T *First() const { return (T *)this->Vehicle::First(); }

	/**
	 * Get the last vehicle in the chain
	 * @return last vehicle in the chain
	 */
	inline T *Last() { return (T *)this->Vehicle::Last(); }

	/**
	 * Get the last vehicle in the chain
	 * @return last vehicle in the chain
	 */
	inline const T *Last() const { return (const T *)this->Vehicle::Last(); }

	/**
	 * Get next vehicle in the chain
	 * @return next vehicle in the chain
	 */
	inline T *Next() const { return (T *)this->Vehicle::Next(); }

	/**
	 * Get previous vehicle in the chain
	 * @return previous vehicle in the chain
	 */
	inline T *Previous() const { return (T *)this->Vehicle::Previous(); }

	/**
	 * Get the next part of an articulated engine.
	 * @return Next part of the articulated engine.
	 * @pre The vehicle is an articulated engine.
	 */
	inline T *GetNextArticulatedPart() { return (T *)this->Vehicle::GetNextArticulatedPart(); }

	/**
	 * Get the next part of an articulated engine.
	 * @return Next part of the articulated engine.
	 * @pre The vehicle is an articulated engine.
	 */
	inline T *GetNextArticulatedPart() const { return (T *)this->Vehicle::GetNextArticulatedPart(); }

	/**
	 * Get the first part of an articulated engine.
	 * @return First part of the engine.
	 */
	inline T *GetFirstEnginePart() { return (T *)this->Vehicle::GetFirstEnginePart(); }

	/**
	 * Get the first part of an articulated engine.
	 * @return First part of the engine.
	 */
	inline const T *GetFirstEnginePart() const { return (const T *)this->Vehicle::GetFirstEnginePart(); }

	/**
	 * Get the last part of an articulated engine.
	 * @return Last part of the engine.
	 */
	inline T *GetLastEnginePart() { return (T *)this->Vehicle::GetLastEnginePart(); }

	/**
	 * Get the next real (non-articulated part) vehicle in the consist.
	 * @return Next vehicle in the consist.
	 */
	inline T *GetNextVehicle() const { return (T *)this->Vehicle::GetNextVehicle(); }

	/**
	 * Get the previous real (non-articulated part) vehicle in the consist.
	 * @return Previous vehicle in the consist.
	 */
	inline T *GetPrevVehicle() const { return (T *)this->Vehicle::GetPrevVehicle(); }

	/**
	 * Tests whether given index is a valid index for vehicle of this type
	 * @param index tested index
	 * @return is this index valid index of T?
	 */
	static inline bool IsValidID(size_t index)
	{
		return Vehicle::IsValidID(index) && Vehicle::Get(index)->type == Type;
	}

	/**
	 * Gets vehicle with given index
	 * @return pointer to vehicle with given index casted to T *
	 */
	static inline T *Get(size_t index)
	{
		return (T *)Vehicle::Get(index);
	}

	/**
	 * Returns vehicle if the index is a valid index for this vehicle type
	 * @return pointer to vehicle with given index if it's a vehicle of this type
	 */
	static inline T *GetIfValid(size_t index)
	{
		return IsValidID(index) ? Get(index) : nullptr;
	}

	/**
	 * Converts a Vehicle to SpecializedVehicle with type checking.
	 * @param v Vehicle pointer
	 * @return pointer to SpecializedVehicle
	 */
	static inline T *From(Vehicle *v)
	{
		assert(v->type == Type);
		return (T *)v;
	}

	/**
	 * Converts a const Vehicle to const SpecializedVehicle with type checking.
	 * @param v Vehicle pointer
	 * @return pointer to SpecializedVehicle
	 */
	static inline const T *From(const Vehicle *v)
	{
		assert(v->type == Type);
		return (const T *)v;
	}

	/**
	 * Update vehicle sprite- and position caches
	 * @param force_update Force updating the vehicle on the viewport.
	 * @param update_delta Also update the delta?
	 */
	inline void UpdateViewport(bool force_update, bool update_delta)
	{
		bool sprite_has_changed = false;

		/* Skip updating sprites on dedicated servers without screen */
		if (_network_dedicated) return;

		/* Explicitly choose method to call to prevent vtable dereference -
		 * it gives ~3% runtime improvements in games with many vehicles */
		if (update_delta) ((T *)this)->T::UpdateDeltaXY();

		/*
		 * Only check for a new sprite sequence if the vehicle direction
		 * has changed since we last checked it, assuming that otherwise
		 * there won't be enough change in bounding box or offsets to need
		 * to resolve a new sprite.
		 */
		if (this->direction != this->sprite_cache.last_direction || this->sprite_cache.is_viewport_candidate) {
			VehicleSpriteSeq seq;

			((T*)this)->T::GetImage(this->direction, EIT_ON_MAP, &seq);
			if (this->sprite_cache.sprite_seq != seq) {
				sprite_has_changed = true;
				this->sprite_cache.sprite_seq = seq;
			}

			this->sprite_cache.last_direction = this->direction;
			this->sprite_cache.revalidate_before_draw = false;
		} else {
			/*
			 * A change that could potentially invalidate the sprite has been
			 * made, signal that we should still resolve it before drawing on a
			 * viewport.
			 */
			this->sprite_cache.revalidate_before_draw = true;
		}

		if (force_update || sprite_has_changed) {
			this->Vehicle::UpdateViewport(true);
		}
	}

	/**
	 * Returns an iterable ensemble of all valid vehicles of type T
	 * @param from index of the first vehicle to consider
	 * @return an iterable ensemble of all valid vehicles of type T
	 */
	static Pool::IterateWrapper<T> Iterate(size_t from = 0) { return Pool::IterateWrapper<T>(from); }
};

/** Generates sequence of free UnitID numbers */
struct FreeUnitIDGenerator {
	bool *cache;  ///< array of occupied unit id numbers
	UnitID maxid; ///< maximum ID at the moment of constructor call
	UnitID curid; ///< last ID returned; 0 if none

	FreeUnitIDGenerator(VehicleType type, CompanyID owner);
	UnitID NextID();

	/** Releases allocated memory */
	~FreeUnitIDGenerator() { free(this->cache); }
};

/** Sentinel for an invalid coordinate. */
static const int32 INVALID_COORD = 0x7fffffff;

#endif /* VEHICLE_BASE_H */
