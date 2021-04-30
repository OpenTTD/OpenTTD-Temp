/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file order_base.h Base class for orders. */

#ifndef ORDER_BASE_H
#define ORDER_BASE_H

#include "order_type.h"
#include "core/pool_type.hpp"
#include "core/bitmath_func.hpp"
#include "cargo_type.h"
#include "depot_type.h"
#include "station_type.h"
#include "vehicle_type.h"
#include "date_type.h"

typedef Pool<Order, OrderID, 256, 0xFF0000> OrderPool;
typedef Pool<OrderList, OrderListID, 128, 64000> OrderListPool;
extern OrderPool _order_pool;
extern OrderListPool _orderlist_pool;

/* If you change this, keep in mind that it is saved on 3 places:
 * - Load_ORDR, all the global orders
 * - Vehicle -> current_order
 * - REF_ORDER (all REFs are currently limited to 16 bits!!)
 */
struct Order : OrderPool::PoolItem<&_order_pool> {
private:
	friend const struct SaveLoad *GetVehicleDescription(VehicleType vt); ///< Saving and loading the current order of vehicles.
	friend void Load_VEHS();                                             ///< Loading of ancient vehicles.
	friend const struct SaveLoad *GetOrderDescription();                 ///< Saving and loading of orders.

	uint8 type;           ///< The type of order + non-stop flags
	uint8 flags;          ///< Load/unload types, depot order/action types.
	DestinationID dest;   ///< The destination of the order.

	CargoID refit_cargo;  ///< Refit CargoID

	uint16 wait_time;    ///< How long in ticks to wait at the destination.
	uint16 travel_time;  ///< How long in ticks the journey to this destination should take.
	uint16 max_speed;    ///< How fast the vehicle may go on the way to the destination.

public:
	Order *next;          ///< Pointer to next order. If nullptr, end of list

	Order() : flags(0), refit_cargo(CT_NO_REFIT), max_speed(UINT16_MAX) {}
	~Order();

	Order(uint32 packed);

	/**
	 * Check whether this order is of the given type.
	 * @param type the type to check against.
	 * @return true if the order matches.
	 */
	inline bool IsType(OrderType type) const { return this->GetType() == type; }

	/**
	 * Get the type of order of this order.
	 * @return the order type.
	 */
	inline OrderType GetType() const { return (OrderType)GB(this->type, 0, 4); }

	void Free();

	void MakeGoToStation(StationID destination);
	void MakeGoToDepot(DepotID destination, OrderDepotTypeFlags order, OrderNonStopFlags non_stop_type = ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS, OrderDepotActionFlags action = ODATF_SERVICE_ONLY, CargoID cargo = CT_NO_REFIT);
	void MakeGoToWaypoint(StationID destination);
	void MakeLoading(bool ordered);
	void MakeLeaveStation();
	void MakeDummy();
	void MakeConditional(VehicleOrderID order);
	void MakeImplicit(StationID destination);
	void MakeWaiting();
	/**
	 * Is this a 'goto' order with a real destination?
	 * @return True if the type is either #OT_GOTO_WAYPOINT, #OT_GOTO_DEPOT or #OT_GOTO_STATION.
	 */
	inline bool IsGotoOrder() const
	{
		return IsType(OT_GOTO_WAYPOINT) || IsType(OT_GOTO_DEPOT) || IsType(OT_GOTO_STATION);
	}

	/**
	 * Gets the destination of this order.
	 * @pre IsType(OT_GOTO_WAYPOINT) || IsType(OT_GOTO_DEPOT) || IsType(OT_GOTO_STATION).
	 * @return the destination of the order.
	 */
	inline DestinationID GetDestination() const { return this->dest; }

	/**
	 * Sets the destination of this order.
	 * @param destination the new destination of the order.
	 * @pre IsType(OT_GOTO_WAYPOINT) || IsType(OT_GOTO_DEPOT) || IsType(OT_GOTO_STATION).
	 */
	inline void SetDestination(DestinationID destination) { this->dest = destination; }

	/**
	 * Is this order a refit order.
	 * @pre IsType(OT_GOTO_DEPOT) || IsType(OT_GOTO_STATION)
	 * @return true if a refit should happen.
	 */
	inline bool IsRefit() const { return this->refit_cargo < NUM_CARGO || this->refit_cargo == CT_AUTO_REFIT; }

	/**
	 * Is this order a auto-refit order.
	 * @pre IsType(OT_GOTO_DEPOT) || IsType(OT_GOTO_STATION)
	 * @return true if a auto-refit should happen.
	 */
	inline bool IsAutoRefit() const { return this->refit_cargo == CT_AUTO_REFIT; }

	/**
	 * Get the cargo to to refit to.
	 * @pre IsType(OT_GOTO_DEPOT) || IsType(OT_GOTO_STATION)
	 * @return the cargo type.
	 */
	inline CargoID GetRefitCargo() const { return this->refit_cargo; }

	void SetRefit(CargoID cargo);

	/** How must the consist be loaded? */
	inline OrderLoadFlags GetLoadType() const { return (OrderLoadFlags)GB(this->flags, 4, 3); }
	/** How must the consist be unloaded? */
	inline OrderUnloadFlags GetUnloadType() const { return (OrderUnloadFlags)GB(this->flags, 0, 3); }
	/** At which stations must we stop? */
	inline OrderNonStopFlags GetNonStopType() const { return (OrderNonStopFlags)GB(this->type, 6, 2); }
	/** Where must we stop at the platform? */
	inline OrderStopLocation GetStopLocation() const { return (OrderStopLocation)GB(this->type, 4, 2); }
	/** What caused us going to the depot? */
	inline OrderDepotTypeFlags GetDepotOrderType() const { return (OrderDepotTypeFlags)GB(this->flags, 0, 3); }
	/** What are we going to do when in the depot. */
	inline OrderDepotActionFlags GetDepotActionType() const { return (OrderDepotActionFlags)GB(this->flags, 4, 3); }
	/** What variable do we have to compare? */
	inline OrderConditionVariable GetConditionVariable() const { return (OrderConditionVariable)GB(this->dest, 11, 5); }
	/** What is the comparator to use? */
	inline OrderConditionComparator GetConditionComparator() const { return (OrderConditionComparator)GB(this->type, 5, 3); }
	/** Get the order to skip to. */
	inline VehicleOrderID GetConditionSkipToOrder() const { return this->flags; }
	/** Get the value to base the skip on. */
	inline uint16 GetConditionValue() const { return GB(this->dest, 0, 11); }

	/** Set how the consist must be loaded. */
	inline void SetLoadType(OrderLoadFlags load_type) { SB(this->flags, 4, 3, load_type); }
	/** Set how the consist must be unloaded. */
	inline void SetUnloadType(OrderUnloadFlags unload_type) { SB(this->flags, 0, 3, unload_type); }
	/** Set whether we must stop at stations or not. */
	inline void SetNonStopType(OrderNonStopFlags non_stop_type) { SB(this->type, 6, 2, non_stop_type); }
	/** Set where we must stop at the platform. */
	inline void SetStopLocation(OrderStopLocation stop_location) { SB(this->type, 4, 2, stop_location); }
	/** Set the cause to go to the depot. */
	inline void SetDepotOrderType(OrderDepotTypeFlags depot_order_type) { SB(this->flags, 0, 3, depot_order_type); }
	/** Set what we are going to do in the depot. */
	inline void SetDepotActionType(OrderDepotActionFlags depot_service_type) { SB(this->flags, 4, 3, depot_service_type); }
	/** Set variable we have to compare. */
	inline void SetConditionVariable(OrderConditionVariable condition_variable) { SB(this->dest, 11, 5, condition_variable); }
	/** Set the comparator to use. */
	inline void SetConditionComparator(OrderConditionComparator condition_comparator) { SB(this->type, 5, 3, condition_comparator); }
	/** Get the order to skip to. */
	inline void SetConditionSkipToOrder(VehicleOrderID order_id) { this->flags = order_id; }
	/** Set the value to base the skip on. */
	inline void SetConditionValue(uint16 value) { SB(this->dest, 0, 11, value); }

	/* As conditional orders write their "skip to" order all over the flags, we cannot check the
	 * flags to find out if timetabling is enabled. However, as conditional orders are never
	 * autofilled we can be sure that any non-zero values for their wait_time and travel_time are
	 * explicitly set (but travel_time is actually unused for conditionals). */

	/** Does this order have an explicit wait time set? */
	inline bool IsWaitTimetabled() const { return this->IsType(OT_CONDITIONAL) ? this->wait_time > 0 : HasBit(this->flags, 3); }
	/** Does this order have an explicit travel time set? */
	inline bool IsTravelTimetabled() const { return this->IsType(OT_CONDITIONAL) ? this->travel_time > 0 : HasBit(this->flags, 7); }

	/** Get the time in ticks a vehicle should wait at the destination or 0 if it's not timetabled. */
	inline uint16 GetTimetabledWait() const { return this->IsWaitTimetabled() ? this->wait_time : 0; }
	/** Get the time in ticks a vehicle should take to reach the destination or 0 if it's not timetabled. */
	inline uint16 GetTimetabledTravel() const { return this->IsTravelTimetabled() ? this->travel_time : 0; }
	/** Get the time in ticks a vehicle will probably wait at the destination (timetabled or not). */
	inline uint16 GetWaitTime() const { return this->wait_time; }
	/** Get the time in ticks a vehicle will probably take to reach the destination (timetabled or not). */
	inline uint16 GetTravelTime() const { return this->travel_time; }

	/**
	 * Get the maxmimum speed in km-ish/h a vehicle is allowed to reach on the way to the
	 * destination.
	 * @return maximum speed.
	 */
	inline uint16 GetMaxSpeed() const { return this->max_speed; }

	/** Set if the wait time is explicitly timetabled (unless the order is conditional). */
	inline void SetWaitTimetabled(bool timetabled) { if (!this->IsType(OT_CONDITIONAL)) SB(this->flags, 3, 1, timetabled ? 1 : 0); }
	/** Set if the travel time is explicitly timetabled (unless the order is conditional). */
	inline void SetTravelTimetabled(bool timetabled) { if (!this->IsType(OT_CONDITIONAL)) SB(this->flags, 7, 1, timetabled ? 1 : 0); }

	/**
	 * Set the time in ticks to wait at the destination.
	 * @param time Time to set as wait time.
	 */
	inline void SetWaitTime(uint16 time) { this->wait_time = time;  }

	/**
	 * Set the time in ticks to take for travelling to the destination.
	 * @param time Time to set as travel time.
	 */
	inline void SetTravelTime(uint16 time) { this->travel_time = time; }

	/**
	 * Set the maxmimum speed in km-ish/h a vehicle is allowed to reach on the way to the
	 * destination.
	 * @param speed Speed to be set.
	 */
	inline void SetMaxSpeed(uint16 speed) { this->max_speed = speed; }

	bool ShouldStopAtStation(const Vehicle *v, StationID station) const;
	bool CanLoadOrUnload() const;
	bool CanLeaveWithCargo(bool has_cargo) const;

	TileIndex GetLocation(const Vehicle *v, bool airport = false) const;

	/** Checks if travel_time and wait_time apply to this order and if they are timetabled. */
	inline bool IsCompletelyTimetabled() const
	{
		if (!this->IsTravelTimetabled() && !this->IsType(OT_CONDITIONAL)) return false;
		if (!this->IsWaitTimetabled() && this->IsType(OT_GOTO_STATION) &&
				!(this->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION)) {
			return false;
		}
		return true;
	}

	void AssignOrder(const Order &other);
	bool Equals(const Order &other) const;

	uint32 Pack() const;
	uint16 MapOldOrder() const;
	void ConvertFromOldSavegame();
};

void InsertOrder(Vehicle *v, Order *new_o, VehicleOrderID sel_ord);
void DeleteOrder(Vehicle *v, VehicleOrderID sel_ord);

/**
 * Shared order list linking together the linked list of orders and the list
 *  of vehicles sharing this order list.
 */
struct OrderList : OrderListPool::PoolItem<&_orderlist_pool> {
private:
	friend void AfterLoadVehicles(bool part_of_load); ///< For instantiating the shared vehicle chain
	friend const struct SaveLoad *GetOrderListDescription(); ///< Saving and loading of order lists.

	StationID GetBestLoadableNext(const Vehicle *v, const Order *o1, const Order *o2) const;

	Order *first;                     ///< First order of the order list.
	VehicleOrderID num_orders;        ///< NOSAVE: How many orders there are in the list.
	VehicleOrderID num_manual_orders; ///< NOSAVE: How many manually added orders are there in the list.
	uint num_vehicles;                ///< NOSAVE: Number of vehicles that share this order list.
	Vehicle *first_shared;            ///< NOSAVE: pointer to the first vehicle in the shared order chain.

	Ticks timetable_duration;         ///< NOSAVE: Total timetabled duration of the order list.
	Ticks total_duration;             ///< NOSAVE: Total (timetabled or not) duration of the order list.

public:
	/** Default constructor producing an invalid order list. */
	OrderList(VehicleOrderID num_orders = INVALID_VEH_ORDER_ID)
		: first(nullptr), num_orders(num_orders), num_manual_orders(0), num_vehicles(0), first_shared(nullptr),
		  timetable_duration(0), total_duration(0) { }

	/**
	 * Create an order list with the given order chain for the given vehicle.
	 *  @param chain pointer to the first order of the order chain
	 *  @param v any vehicle using this orderlist
	 */
	OrderList(Order *chain, Vehicle *v) { this->Initialize(chain, v); }

	/** Destructor. Invalidates OrderList for re-usage by the pool. */
	~OrderList() {}

	void Initialize(Order *chain, Vehicle *v);

	void RecalculateTimetableDuration();

	/**
	 * Get the first order of the order chain.
	 * @return the first order of the chain.
	 */
	inline Order *GetFirstOrder() const { return this->first; }

	Order *GetOrderAt(int index) const;

	/**
	 * Get the last order of the order chain.
	 * @return the last order of the chain.
	 */
	inline Order *GetLastOrder() const { return this->GetOrderAt(this->num_orders - 1); }

	/**
	 * Get the order after the given one or the first one, if the given one is the
	 * last one.
	 * @param curr Order to find the next one for.
	 * @return Next order.
	 */
	inline const Order *GetNext(const Order *curr) const { return (curr->next == nullptr) ? this->GetFirstOrder() : curr->next; }

	/**
	 * Get number of orders in the order list.
	 * @return number of orders in the chain.
	 */
	inline VehicleOrderID GetNumOrders() const { return this->num_orders; }

	/**
	 * Get number of manually added orders in the order list.
	 * @return number of manual orders in the chain.
	 */
	inline VehicleOrderID GetNumManualOrders() const { return this->num_manual_orders; }

	StationIDStack GetNextStoppingStation(const Vehicle *v, const Order *first = nullptr, uint hops = 0) const;
	const Order *GetNextDecisionNode(const Order *next, uint hops) const;

	void InsertOrderAt(Order *new_order, int index);
	void DeleteOrderAt(int index);
	void MoveOrder(int from, int to);

	/**
	 * Is this a shared order list?
	 * @return whether this order list is shared among multiple vehicles
	 */
	inline bool IsShared() const { return this->num_vehicles > 1; };

	/**
	 * Get the first vehicle of this vehicle chain.
	 * @return the first vehicle of the chain.
	 */
	inline Vehicle *GetFirstSharedVehicle() const { return this->first_shared; }

	/**
	 * Return the number of vehicles that share this orders list
	 * @return the count of vehicles that use this shared orders list
	 */
	inline uint GetNumVehicles() const { return this->num_vehicles; }

	bool IsVehicleInSharedOrdersList(const Vehicle *v) const;
	int GetPositionInSharedOrderList(const Vehicle *v) const;

	/**
	 * Adds the given vehicle to this shared order list.
	 * @note This is supposed to be called after the vehicle has been inserted
	 *       into the shared vehicle chain.
	 * @param v vehicle to add to the list
	 */
	inline void AddVehicle(Vehicle *v) { ++this->num_vehicles; }

	void RemoveVehicle(Vehicle *v);

	bool IsCompleteTimetable() const;

	/**
	 * Gets the total duration of the vehicles timetable or INVALID_TICKS is the timetable is not complete.
	 * @return total timetable duration or INVALID_TICKS for incomplete timetables
	 */
	inline Ticks GetTimetableTotalDuration() const { return this->IsCompleteTimetable() ? this->timetable_duration : INVALID_TICKS; }

	/**
	 * Gets the known duration of the vehicles timetable even if the timetable is not complete.
	 * @return known timetable duration
	 */
	inline Ticks GetTimetableDurationIncomplete() const { return this->timetable_duration; }

	/**
	 * Gets the known duration of the vehicles orders, timetabled or not.
	 * @return  known order duration.
	 */
	inline Ticks GetTotalDuration() const { return this->total_duration; }

	/**
	 * Must be called if an order's timetable is changed to update internal book keeping.
	 * @param delta By how many ticks has the timetable duration changed
	 */
	void UpdateTimetableDuration(Ticks delta) { this->timetable_duration += delta; }

	/**
	 * Must be called if an order's timetable is changed to update internal book keeping.
	 * @param delta By how many ticks has the total duration changed
	 */
	void UpdateTotalDuration(Ticks delta) { this->total_duration += delta; }

	void FreeChain(bool keep_orderlist = false);

	void DebugCheckSanity() const;
};

#endif /* ORDER_BASE_H */
