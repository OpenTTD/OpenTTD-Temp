/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_order.cpp Implementation of ScriptOrder. */

#include "../../stdafx.h"
#include "script_order.hpp"
#include "script_cargo.hpp"
#include "script_map.hpp"
#include "../script_instance.hpp"
#include "../../debug.h"
#include "../../vehicle_base.h"
#include "../../roadstop_base.h"
#include "../../depot_base.h"
#include "../../station_base.h"
#include "../../waypoint_base.h"

#include "../../safeguards.h"

/**
 * Gets the order type given a tile
 * @param t the tile to get the order from
 * @return the order type, or OT_END when there is no order
 */
static OrderType GetOrderTypeByTile(TileIndex t)
{
	if (!::IsValidTile(t)) return OT_END;

	switch (::GetTileType(t)) {
		default: break;
		case MP_STATION:
			if (IsBuoy(t) || IsRailWaypoint(t)) return OT_GOTO_WAYPOINT;
			if (IsHangar(t)) return OT_GOTO_DEPOT;
			return OT_GOTO_STATION;

		case MP_WATER:   if (::IsShipDepot(t)) return OT_GOTO_DEPOT; break;
		case MP_ROAD:    if (::GetRoadTileType(t) == ROAD_TILE_DEPOT) return OT_GOTO_DEPOT; break;
		case MP_RAILWAY:
			if (IsRailDepot(t)) return OT_GOTO_DEPOT;
			break;
	}

	return OT_END;
}

/* static */ bool ScriptOrder::IsValidVehicleOrder(VehicleID vehicle_id, OrderPosition order_position)
{
	return ScriptVehicle::IsValidVehicle(vehicle_id) && order_position >= 0 && (order_position < ::Vehicle::Get(vehicle_id)->GetNumManualOrders() || order_position == ORDER_CURRENT);
}

/**
 * Get the current order the vehicle is executing. If the current order is in
 *  the order list, return the order from the orderlist. If the current order
 *  was a manual order, return the current order.
 */
static const Order *ResolveOrder(VehicleID vehicle_id, ScriptOrder::OrderPosition order_position)
{
	const Vehicle *v = ::Vehicle::Get(vehicle_id);
	if (order_position == ScriptOrder::ORDER_CURRENT) {
		const Order *order = &v->current_order;
		if (order->GetType() == OT_GOTO_DEPOT && !(order->GetDepotOrderType() & ODTFB_PART_OF_ORDERS)) return order;
		order_position = ScriptOrder::ResolveOrderPosition(vehicle_id, order_position);
		if (order_position == ScriptOrder::ORDER_INVALID) return nullptr;
	}
	const Order *order = v->GetFirstOrder();
	while (order->GetType() == OT_IMPLICIT) order = order->next;
	while (order_position > 0) {
		order_position = (ScriptOrder::OrderPosition)(order_position - 1);
		order = order->next;
		while (order->GetType() == OT_IMPLICIT) order = order->next;
	}
	return order;
}

/**
 * Convert an ScriptOrder::OrderPosition (which is the manual order index) to an order index
 * as expected by the OpenTTD commands.
 * @param order_position The OrderPosition to convert.
 * @return An OpenTTD-internal index for the same order.
 */
static int ScriptOrderPositionToRealOrderPosition(VehicleID vehicle_id, ScriptOrder::OrderPosition order_position)
{
	const Vehicle *v = ::Vehicle::Get(vehicle_id);
	if (order_position == v->GetNumManualOrders()) return v->GetNumOrders();

	assert(ScriptOrder::IsValidVehicleOrder(vehicle_id, order_position));

	int res = (int)order_position;
	const Order *order = v->orders.list->GetFirstOrder();
	for (; order->GetType() == OT_IMPLICIT; order = order->next) res++;
	while (order_position > 0) {
		order_position = (ScriptOrder::OrderPosition)(order_position - 1);
		order = order->next;
		for (; order->GetType() == OT_IMPLICIT; order = order->next) res++;
	}

	return res;
}

/* static */ bool ScriptOrder::IsGotoStationOrder(VehicleID vehicle_id, OrderPosition order_position)
{
	if (!IsValidVehicleOrder(vehicle_id, order_position)) return false;

	const Order *order = ::ResolveOrder(vehicle_id, order_position);
	return order != nullptr && order->GetType() == OT_GOTO_STATION;
}

/* static */ bool ScriptOrder::IsGotoDepotOrder(VehicleID vehicle_id, OrderPosition order_position)
{
	if (!IsValidVehicleOrder(vehicle_id, order_position)) return false;

	const Order *order = ::ResolveOrder(vehicle_id, order_position);
	return order != nullptr && order->GetType() == OT_GOTO_DEPOT;
}

/* static */ bool ScriptOrder::IsGotoWaypointOrder(VehicleID vehicle_id, OrderPosition order_position)
{
	if (!IsValidVehicleOrder(vehicle_id, order_position)) return false;

	const Order *order = ::ResolveOrder(vehicle_id, order_position);
	return order != nullptr && order->GetType() == OT_GOTO_WAYPOINT;
}

/* static */ bool ScriptOrder::IsConditionalOrder(VehicleID vehicle_id, OrderPosition order_position)
{
	if (order_position == ORDER_CURRENT) return false;
	if (!IsValidVehicleOrder(vehicle_id, order_position)) return false;

	const Order *order = ::Vehicle::Get(vehicle_id)->GetOrder(ScriptOrderPositionToRealOrderPosition(vehicle_id, order_position));
	return order->GetType() == OT_CONDITIONAL;
}

/* static */ bool ScriptOrder::IsVoidOrder(VehicleID vehicle_id, OrderPosition order_position)
{
	if (order_position == ORDER_CURRENT) return false;
	if (!IsValidVehicleOrder(vehicle_id, order_position)) return false;

	const Order *order = ::ResolveOrder(vehicle_id, order_position);
	return order->GetType() == OT_DUMMY;
}

/* static */ bool ScriptOrder::IsRefitOrder(VehicleID vehicle_id, OrderPosition order_position)
{
	if (!IsValidVehicleOrder(vehicle_id, order_position)) return false;

	const Order *order = ::ResolveOrder(vehicle_id, order_position);
	return order != nullptr && order->IsRefit();
}

/* static */ bool ScriptOrder::IsCurrentOrderPartOfOrderList(VehicleID vehicle_id)
{
	if (!ScriptVehicle::IsValidVehicle(vehicle_id)) return false;
	if (GetOrderCount(vehicle_id) == 0) return false;

	const Order *order = &::Vehicle::Get(vehicle_id)->current_order;
	if (order->GetType() != OT_GOTO_DEPOT) return true;
	return (order->GetDepotOrderType() & ODTFB_PART_OF_ORDERS) != 0;
}

/* static */ ScriptOrder::OrderPosition ScriptOrder::ResolveOrderPosition(VehicleID vehicle_id, OrderPosition order_position)
{
	if (!ScriptVehicle::IsValidVehicle(vehicle_id)) return ORDER_INVALID;

	int num_manual_orders = ::Vehicle::Get(vehicle_id)->GetNumManualOrders();
	if (num_manual_orders == 0) return ORDER_INVALID;

	if (order_position == ORDER_CURRENT) {
		int cur_order_pos = ::Vehicle::Get(vehicle_id)->cur_real_order_index;
		const Order *order = ::Vehicle::Get(vehicle_id)->GetFirstOrder();
		int num_implicit_orders = 0;
		for (int i = 0; i < cur_order_pos; i++) {
			if (order->GetType() == OT_IMPLICIT) num_implicit_orders++;
			order = order->next;
		}
		int real_order_pos = cur_order_pos - num_implicit_orders;
		assert(real_order_pos < num_manual_orders);
		return (ScriptOrder::OrderPosition)real_order_pos;
	}
	return (order_position >= 0 && order_position < num_manual_orders) ? order_position : ORDER_INVALID;
}


/* static */ bool ScriptOrder::AreOrderFlagsValid(TileIndex destination, ScriptOrderFlags order_flags)
{
	OrderType ot = (order_flags & OF_GOTO_NEAREST_DEPOT) ? OT_GOTO_DEPOT : ::GetOrderTypeByTile(destination);
	switch (ot) {
		case OT_GOTO_STATION:
			return (order_flags & ~(OF_NON_STOP_FLAGS | OF_UNLOAD_FLAGS | OF_LOAD_FLAGS)) == 0 &&
					/* Test the different mutual exclusive flags. */
					((order_flags & OF_TRANSFER)      == 0 || (order_flags & OF_UNLOAD)    == 0) &&
					((order_flags & OF_TRANSFER)      == 0 || (order_flags & OF_NO_UNLOAD) == 0) &&
					((order_flags & OF_UNLOAD)        == 0 || (order_flags & OF_NO_UNLOAD) == 0) &&
					((order_flags & OF_UNLOAD)        == 0 || (order_flags & OF_NO_UNLOAD) == 0) &&
					((order_flags & OF_NO_UNLOAD)     == 0 || (order_flags & OF_NO_LOAD)   == 0) &&
					((order_flags & OF_FULL_LOAD_ANY) == 0 || (order_flags & OF_NO_LOAD)   == 0);

		case OT_GOTO_DEPOT:
			return (order_flags & ~(OF_NON_STOP_FLAGS | OF_DEPOT_FLAGS)) == 0 &&
					((order_flags & OF_SERVICE_IF_NEEDED) == 0 || (order_flags & OF_STOP_IN_DEPOT) == 0);

		case OT_GOTO_WAYPOINT: return (order_flags & ~(OF_NON_STOP_FLAGS)) == 0;
		default:               return false;
	}
}

/* static */ bool ScriptOrder::IsValidConditionalOrder(OrderCondition condition, CompareFunction compare)
{
	switch (condition) {
		case OC_LOAD_PERCENTAGE:
		case OC_RELIABILITY:
		case OC_MAX_RELIABILITY:
		case OC_MAX_SPEED:
		case OC_AGE:
		case OC_REMAINING_LIFETIME:
			return compare >= CF_EQUALS && compare <= CF_MORE_EQUALS;

		case OC_REQUIRES_SERVICE:
			return compare == CF_IS_TRUE || compare == CF_IS_FALSE;

		case OC_UNCONDITIONALLY:
			return true;

		default: return false;
	}
}

/* static */ int32 ScriptOrder::GetOrderCount(VehicleID vehicle_id)
{
	return ScriptVehicle::IsValidVehicle(vehicle_id) ? ::Vehicle::Get(vehicle_id)->GetNumManualOrders() : -1;
}

/* static */ TileIndex ScriptOrder::GetOrderDestination(VehicleID vehicle_id, OrderPosition order_position)
{
	if (!IsValidVehicleOrder(vehicle_id, order_position)) return INVALID_TILE;

	const Order *order = ::ResolveOrder(vehicle_id, order_position);
	if (order == nullptr || order->GetType() == OT_CONDITIONAL) return INVALID_TILE;

	switch (order->GetType()) {
		case OT_GOTO_DEPOT: {
			/* We don't know where the nearest depot is... (yet) */
			if (order->GetDepotActionType() & ODATFB_NEAREST_DEPOT) return INVALID_TILE;

			return ::Depot::Get(order->GetDestination())->xy;
		}

		case OT_GOTO_STATION: {
			const Station *st = ::Station::Get(order->GetDestination());
			if (st->train_station.tile != INVALID_TILE) {
				for (TileIndex t : st->train_station) {
					if (st->TileBelongsToRailStation(t)) return t;
				}
			} else if (st->ship_station.tile != INVALID_TILE) {
				for (TileIndex t : st->ship_station) {
					if (IsTileType(t, MP_STATION) && (IsDock(t) || IsOilRig(t)) && GetStationIndex(t) == st->index) return t;
				}
			} else if (st->bus_stops != nullptr) {
				return st->bus_stops->xy;
			} else if (st->truck_stops != nullptr) {
				return st->truck_stops->xy;
			} else if (st->airport.tile != INVALID_TILE) {
				for (TileIndex tile : st->airport) {
					if (st->TileBelongsToAirport(tile) && !::IsHangar(tile)) return tile;
				}
			}
			return INVALID_TILE;
		}

		case OT_GOTO_WAYPOINT: {
			const Waypoint *wp = ::Waypoint::Get(order->GetDestination());
			if (wp->train_station.tile != INVALID_TILE) {
				for (TileIndex t : wp->train_station) {
					if (wp->TileBelongsToRailStation(t)) return t;
				}
			}
			/* If the waypoint has no rail waypoint tiles, it must have a buoy */
			return wp->xy;
		}
		default:               return INVALID_TILE;
	}
}

/* static */ ScriptOrder::ScriptOrderFlags ScriptOrder::GetOrderFlags(VehicleID vehicle_id, OrderPosition order_position)
{
	if (!IsValidVehicleOrder(vehicle_id, order_position)) return OF_INVALID;

	const Order *order = ::ResolveOrder(vehicle_id, order_position);
	if (order == nullptr || order->GetType() == OT_CONDITIONAL || order->GetType() == OT_DUMMY) return OF_INVALID;

	ScriptOrderFlags order_flags = OF_NONE;
	order_flags |= (ScriptOrderFlags)order->GetNonStopType();
	switch (order->GetType()) {
		case OT_GOTO_DEPOT:
			if (order->GetDepotOrderType() & ODTFB_SERVICE) order_flags |= OF_SERVICE_IF_NEEDED;
			if (order->GetDepotActionType() & ODATFB_HALT) order_flags |= OF_STOP_IN_DEPOT;
			if (order->GetDepotActionType() & ODATFB_NEAREST_DEPOT) order_flags |= OF_GOTO_NEAREST_DEPOT;
			break;

		case OT_GOTO_STATION:
			order_flags |= (ScriptOrderFlags)(order->GetLoadType()   << 5);
			order_flags |= (ScriptOrderFlags)(order->GetUnloadType() << 2);
			break;

		default: break;
	}

	return order_flags;
}

/* static */ ScriptOrder::OrderPosition ScriptOrder::GetOrderJumpTo(VehicleID vehicle_id, OrderPosition order_position)
{
	if (!IsValidVehicleOrder(vehicle_id, order_position)) return ORDER_INVALID;
	if (order_position == ORDER_CURRENT || !IsConditionalOrder(vehicle_id, order_position)) return ORDER_INVALID;

	const Order *order = ::ResolveOrder(vehicle_id, order_position);
	return (OrderPosition)order->GetConditionSkipToOrder();
}

/* static */ ScriptOrder::OrderCondition ScriptOrder::GetOrderCondition(VehicleID vehicle_id, OrderPosition order_position)
{
	if (!IsValidVehicleOrder(vehicle_id, order_position)) return OC_INVALID;
	if (order_position == ORDER_CURRENT || !IsConditionalOrder(vehicle_id, order_position)) return OC_INVALID;

	const Order *order = ::ResolveOrder(vehicle_id, order_position);
	return (OrderCondition)order->GetConditionVariable();
}

/* static */ ScriptOrder::CompareFunction ScriptOrder::GetOrderCompareFunction(VehicleID vehicle_id, OrderPosition order_position)
{
	if (!IsValidVehicleOrder(vehicle_id, order_position)) return CF_INVALID;
	if (order_position == ORDER_CURRENT || !IsConditionalOrder(vehicle_id, order_position)) return CF_INVALID;

	const Order *order = ::ResolveOrder(vehicle_id, order_position);
	return (CompareFunction)order->GetConditionComparator();
}

/* static */ int32 ScriptOrder::GetOrderCompareValue(VehicleID vehicle_id, OrderPosition order_position)
{
	if (!IsValidVehicleOrder(vehicle_id, order_position)) return -1;
	if (order_position == ORDER_CURRENT || !IsConditionalOrder(vehicle_id, order_position)) return -1;

	const Order *order = ::ResolveOrder(vehicle_id, order_position);
	int32 value = order->GetConditionValue();
	if (order->GetConditionVariable() == OCV_MAX_SPEED) value = value * 16 / 10;
	return value;
}

/* static */ ScriptOrder::StopLocation ScriptOrder::GetStopLocation(VehicleID vehicle_id, OrderPosition order_position)
{
	if (!IsValidVehicleOrder(vehicle_id, order_position)) return STOPLOCATION_INVALID;
	if (ScriptVehicle::GetVehicleType(vehicle_id) != ScriptVehicle::VT_RAIL) return STOPLOCATION_INVALID;
	if (!IsGotoStationOrder(vehicle_id, order_position)) return STOPLOCATION_INVALID;

	const Order *order = ::ResolveOrder(vehicle_id, order_position);
	return (ScriptOrder::StopLocation)order->GetStopLocation();
}

/* static */ CargoID ScriptOrder::GetOrderRefit(VehicleID vehicle_id, OrderPosition order_position)
{
	if (!IsValidVehicleOrder(vehicle_id, order_position)) return CT_NO_REFIT;
	if (order_position != ORDER_CURRENT && !IsGotoStationOrder(vehicle_id, order_position) && !IsGotoDepotOrder(vehicle_id, order_position)) return CT_NO_REFIT;

	const Order *order = ::ResolveOrder(vehicle_id, order_position);
	return order->IsRefit() ? order->GetRefitCargo() : (CargoID)CT_NO_REFIT;
}

/* static */ bool ScriptOrder::SetOrderJumpTo(VehicleID vehicle_id, OrderPosition order_position, OrderPosition jump_to)
{
	EnforcePrecondition(false, IsValidVehicleOrder(vehicle_id, order_position));
	EnforcePrecondition(false, order_position != ORDER_CURRENT && IsConditionalOrder(vehicle_id, order_position));
	EnforcePrecondition(false, IsValidVehicleOrder(vehicle_id, jump_to) && jump_to != ORDER_CURRENT);

	return ScriptObject::DoCommand(0, vehicle_id | (order_position << 20), MOF_COND_DESTINATION | (jump_to << 4), CMD_MODIFY_ORDER);
}

/* static */ bool ScriptOrder::SetOrderCondition(VehicleID vehicle_id, OrderPosition order_position, OrderCondition condition)
{
	EnforcePrecondition(false, IsValidVehicleOrder(vehicle_id, order_position));
	EnforcePrecondition(false, order_position != ORDER_CURRENT && IsConditionalOrder(vehicle_id, order_position));
	EnforcePrecondition(false, condition >= OC_LOAD_PERCENTAGE && condition <= OC_REMAINING_LIFETIME);

	int order_pos = ScriptOrderPositionToRealOrderPosition(vehicle_id, order_position);
	return ScriptObject::DoCommand(0, vehicle_id | (order_pos << 20), MOF_COND_VARIABLE | (condition << 4), CMD_MODIFY_ORDER);
}

/* static */ bool ScriptOrder::SetOrderCompareFunction(VehicleID vehicle_id, OrderPosition order_position, CompareFunction compare)
{
	EnforcePrecondition(false, IsValidVehicleOrder(vehicle_id, order_position));
	EnforcePrecondition(false, order_position != ORDER_CURRENT && IsConditionalOrder(vehicle_id, order_position));
	EnforcePrecondition(false, compare >= CF_EQUALS && compare <= CF_IS_FALSE);

	int order_pos = ScriptOrderPositionToRealOrderPosition(vehicle_id, order_position);
	return ScriptObject::DoCommand(0, vehicle_id | (order_pos << 20), MOF_COND_COMPARATOR | (compare << 4), CMD_MODIFY_ORDER);
}

/* static */ bool ScriptOrder::SetOrderCompareValue(VehicleID vehicle_id, OrderPosition order_position, int32 value)
{
	EnforcePrecondition(false, IsValidVehicleOrder(vehicle_id, order_position));
	EnforcePrecondition(false, order_position != ORDER_CURRENT && IsConditionalOrder(vehicle_id, order_position));
	EnforcePrecondition(false, value >= 0 && value < 2048);
	if (GetOrderCondition(vehicle_id, order_position) == OC_MAX_SPEED) value = value * 10 / 16;

	int order_pos = ScriptOrderPositionToRealOrderPosition(vehicle_id, order_position);
	return ScriptObject::DoCommand(0, vehicle_id | (order_pos << 20), MOF_COND_VALUE | (value << 4), CMD_MODIFY_ORDER);
}

/* static */ bool ScriptOrder::SetStopLocation(VehicleID vehicle_id, OrderPosition order_position, StopLocation stop_location)
{
	EnforcePrecondition(false, IsValidVehicleOrder(vehicle_id, order_position));
	EnforcePrecondition(false, ScriptVehicle::GetVehicleType(vehicle_id) == ScriptVehicle::VT_RAIL);
	EnforcePrecondition(false, IsGotoStationOrder(vehicle_id, order_position));
	EnforcePrecondition(false, stop_location >= STOPLOCATION_NEAR && stop_location <= STOPLOCATION_FAR);

	order_position = ScriptOrder::ResolveOrderPosition(vehicle_id, order_position);

	int order_pos = ScriptOrderPositionToRealOrderPosition(vehicle_id, order_position);
	uint32 p1 = vehicle_id | (order_pos << 20);
	uint32 p2 = MOF_STOP_LOCATION | (stop_location << 4);
	return ScriptObject::DoCommand(0, p1, p2, CMD_MODIFY_ORDER);
}

/* static */ bool ScriptOrder::SetOrderRefit(VehicleID vehicle_id, OrderPosition order_position, CargoID refit_cargo)
{
	EnforcePrecondition(false, IsValidVehicleOrder(vehicle_id, order_position));
	EnforcePrecondition(false, IsGotoStationOrder(vehicle_id, order_position) || (IsGotoDepotOrder(vehicle_id, order_position) && refit_cargo != CT_AUTO_REFIT));
	EnforcePrecondition(false, ScriptCargo::IsValidCargo(refit_cargo) || refit_cargo == CT_AUTO_REFIT || refit_cargo == CT_NO_REFIT);

	uint32 p1 = vehicle_id;
	uint32 p2 = refit_cargo | ScriptOrderPositionToRealOrderPosition(vehicle_id, ScriptOrder::ResolveOrderPosition(vehicle_id, order_position)) << 16;
	return ScriptObject::DoCommand(0, p1, p2, CMD_ORDER_REFIT);
}

/* static */ bool ScriptOrder::AppendOrder(VehicleID vehicle_id, TileIndex destination, ScriptOrderFlags order_flags)
{
	EnforcePrecondition(false, ScriptVehicle::IsValidVehicle(vehicle_id));
	EnforcePrecondition(false, AreOrderFlagsValid(destination, order_flags));

	return InsertOrder(vehicle_id, (ScriptOrder::OrderPosition)::Vehicle::Get(vehicle_id)->GetNumManualOrders(), destination, order_flags);
}

/* static */ bool ScriptOrder::AppendConditionalOrder(VehicleID vehicle_id, OrderPosition jump_to)
{
	EnforcePrecondition(false, ScriptVehicle::IsValidVehicle(vehicle_id));
	EnforcePrecondition(false, IsValidVehicleOrder(vehicle_id, jump_to));

	return InsertConditionalOrder(vehicle_id, (ScriptOrder::OrderPosition)::Vehicle::Get(vehicle_id)->GetNumManualOrders(), jump_to);
}

/* static */ bool ScriptOrder::InsertOrder(VehicleID vehicle_id, OrderPosition order_position, TileIndex destination, ScriptOrder::ScriptOrderFlags order_flags)
{
	/* IsValidVehicleOrder is not good enough because it does not allow appending. */
	if (order_position == ORDER_CURRENT) order_position = ScriptOrder::ResolveOrderPosition(vehicle_id, order_position);

	EnforcePrecondition(false, ScriptVehicle::IsValidVehicle(vehicle_id));
	EnforcePrecondition(false, order_position >= 0 && order_position <= ::Vehicle::Get(vehicle_id)->GetNumManualOrders());
	EnforcePrecondition(false, AreOrderFlagsValid(destination, order_flags));

	Order order;
	OrderType ot = (order_flags & OF_GOTO_NEAREST_DEPOT) ? OT_GOTO_DEPOT : ::GetOrderTypeByTile(destination);
	switch (ot) {
		case OT_GOTO_DEPOT: {
			OrderDepotTypeFlags odtf = (OrderDepotTypeFlags)(ODTFB_PART_OF_ORDERS | ((order_flags & OF_SERVICE_IF_NEEDED) ? ODTFB_SERVICE : 0));
			OrderDepotActionFlags odaf = (OrderDepotActionFlags)(ODATF_SERVICE_ONLY | ((order_flags & OF_STOP_IN_DEPOT) ? ODATFB_HALT : 0));
			if (order_flags & OF_GOTO_NEAREST_DEPOT) odaf |= ODATFB_NEAREST_DEPOT;
			OrderNonStopFlags onsf = (OrderNonStopFlags)((order_flags & OF_NON_STOP_INTERMEDIATE) ? ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS : ONSF_STOP_EVERYWHERE);
			if (order_flags & OF_GOTO_NEAREST_DEPOT) {
				order.MakeGoToDepot(0, odtf, onsf, odaf);
			} else {
				/* Check explicitly if the order is to a station (for aircraft) or
				 * to a depot (other vehicle types). */
				if (::Vehicle::Get(vehicle_id)->type == VEH_AIRCRAFT) {
					if (!::IsTileType(destination, MP_STATION)) return false;
					order.MakeGoToDepot(::GetStationIndex(destination), odtf, onsf, odaf);
				} else {
					if (::IsTileType(destination, MP_STATION)) return false;
					order.MakeGoToDepot(::GetDepotIndex(destination), odtf, onsf, odaf);
				}
			}
			break;
		}

		case OT_GOTO_STATION:
			order.MakeGoToStation(::GetStationIndex(destination));
			order.SetLoadType((OrderLoadFlags)GB(order_flags, 5, 3));
			order.SetUnloadType((OrderUnloadFlags)GB(order_flags, 2, 3));
			order.SetStopLocation(OSL_PLATFORM_FAR_END);
			break;

		case OT_GOTO_WAYPOINT:
			order.MakeGoToWaypoint(::GetStationIndex(destination));
			break;

		default:
			return false;
	}

	order.SetNonStopType((OrderNonStopFlags)GB(order_flags, 0, 2));

	int order_pos = ScriptOrderPositionToRealOrderPosition(vehicle_id, order_position);
	return ScriptObject::DoCommand(0, vehicle_id | (order_pos << 20), order.Pack(), CMD_INSERT_ORDER);
}

/* static */ bool ScriptOrder::InsertConditionalOrder(VehicleID vehicle_id, OrderPosition order_position, OrderPosition jump_to)
{
	/* IsValidVehicleOrder is not good enough because it does not allow appending. */
	if (order_position == ORDER_CURRENT) order_position = ScriptOrder::ResolveOrderPosition(vehicle_id, order_position);

	EnforcePrecondition(false, ScriptVehicle::IsValidVehicle(vehicle_id));
	EnforcePrecondition(false, order_position >= 0 && order_position <= ::Vehicle::Get(vehicle_id)->GetNumManualOrders());
	EnforcePrecondition(false, IsValidVehicleOrder(vehicle_id, jump_to) && jump_to != ORDER_CURRENT);

	Order order;
	order.MakeConditional(jump_to);

	int order_pos = ScriptOrderPositionToRealOrderPosition(vehicle_id, order_position);
	return ScriptObject::DoCommand(0, vehicle_id | (order_pos << 20), order.Pack(), CMD_INSERT_ORDER);
}

/* static */ bool ScriptOrder::RemoveOrder(VehicleID vehicle_id, OrderPosition order_position)
{
	order_position = ScriptOrder::ResolveOrderPosition(vehicle_id, order_position);

	EnforcePrecondition(false, IsValidVehicleOrder(vehicle_id, order_position));

	int order_pos = ScriptOrderPositionToRealOrderPosition(vehicle_id, order_position);
	return ScriptObject::DoCommand(0, vehicle_id, order_pos, CMD_DELETE_ORDER);
}

/* static */ bool ScriptOrder::SkipToOrder(VehicleID vehicle_id, OrderPosition next_order)
{
	next_order = ScriptOrder::ResolveOrderPosition(vehicle_id, next_order);

	EnforcePrecondition(false, IsValidVehicleOrder(vehicle_id, next_order));

	int order_pos = ScriptOrderPositionToRealOrderPosition(vehicle_id, next_order);
	return ScriptObject::DoCommand(0, vehicle_id, order_pos, CMD_SKIP_TO_ORDER);
}

/**
 * Callback handler as SetOrderFlags possibly needs multiple DoCommand calls
 * to be able to set all order flags correctly. As we need to wait till the
 * command has completed before we know the next bits to change we need to
 * call the function multiple times. Each time it'll reduce the difference
 * between the wanted and the current order.
 * @param instance The script instance we are doing the callback for.
 */
static void _DoCommandReturnSetOrderFlags(class ScriptInstance *instance)
{
	ScriptObject::SetLastCommandRes(ScriptOrder::_SetOrderFlags());
	ScriptInstance::DoCommandReturn(instance);
}

/* static */ bool ScriptOrder::_SetOrderFlags()
{
	/* Make sure we don't go into an infinite loop */
	int retry = ScriptObject::GetCallbackVariable(3) - 1;
	if (retry < 0) {
		Debug(script, 0, "Possible infinite loop in SetOrderFlags() detected");
		return false;
	}
	ScriptObject::SetCallbackVariable(3, retry);

	VehicleID vehicle_id = (VehicleID)ScriptObject::GetCallbackVariable(0);
	OrderPosition order_position = (OrderPosition)ScriptObject::GetCallbackVariable(1);
	ScriptOrderFlags order_flags = (ScriptOrderFlags)ScriptObject::GetCallbackVariable(2);

	order_position = ScriptOrder::ResolveOrderPosition(vehicle_id, order_position);

	EnforcePrecondition(false, IsValidVehicleOrder(vehicle_id, order_position));
	EnforcePrecondition(false, AreOrderFlagsValid(GetOrderDestination(vehicle_id, order_position), order_flags));

	const Order *order = ::ResolveOrder(vehicle_id, order_position);
	int order_pos = ScriptOrderPositionToRealOrderPosition(vehicle_id, order_position);

	ScriptOrderFlags current = GetOrderFlags(vehicle_id, order_position);

	EnforcePrecondition(false, (order_flags & OF_GOTO_NEAREST_DEPOT) == (current & OF_GOTO_NEAREST_DEPOT));

	if ((current & OF_NON_STOP_FLAGS) != (order_flags & OF_NON_STOP_FLAGS)) {
		return ScriptObject::DoCommand(0, vehicle_id | (order_pos << 20), (order_flags & OF_NON_STOP_FLAGS) << 4 | MOF_NON_STOP, CMD_MODIFY_ORDER, nullptr, &::_DoCommandReturnSetOrderFlags);
	}

	switch (order->GetType()) {
		case OT_GOTO_DEPOT:
			if ((current & OF_DEPOT_FLAGS) != (order_flags & OF_DEPOT_FLAGS)) {
				uint data = DA_ALWAYS_GO;
				if (order_flags & OF_SERVICE_IF_NEEDED) data = DA_SERVICE;
				if (order_flags & OF_STOP_IN_DEPOT) data = DA_STOP;
				return ScriptObject::DoCommand(0, vehicle_id | (order_pos << 20), (data << 4) | MOF_DEPOT_ACTION, CMD_MODIFY_ORDER, nullptr, &::_DoCommandReturnSetOrderFlags);
			}
			break;

		case OT_GOTO_STATION:
			if ((current & OF_UNLOAD_FLAGS) != (order_flags & OF_UNLOAD_FLAGS)) {
				return ScriptObject::DoCommand(0, vehicle_id | (order_pos << 20), (order_flags & OF_UNLOAD_FLAGS) << 2 | MOF_UNLOAD, CMD_MODIFY_ORDER, nullptr, &::_DoCommandReturnSetOrderFlags);
			}
			if ((current & OF_LOAD_FLAGS) != (order_flags & OF_LOAD_FLAGS)) {
				return ScriptObject::DoCommand(0, vehicle_id | (order_pos << 20), (order_flags & OF_LOAD_FLAGS) >> 1 | MOF_LOAD, CMD_MODIFY_ORDER, nullptr, &::_DoCommandReturnSetOrderFlags);
			}
			break;

		default: break;
	}

	assert(GetOrderFlags(vehicle_id, order_position) == order_flags);

	return true;
}

/* static */ bool ScriptOrder::SetOrderFlags(VehicleID vehicle_id, OrderPosition order_position, ScriptOrder::ScriptOrderFlags order_flags)
{
	ScriptObject::SetCallbackVariable(0, vehicle_id);
	ScriptObject::SetCallbackVariable(1, order_position);
	ScriptObject::SetCallbackVariable(2, order_flags);
	/* In case another client(s) change orders at the same time we could
	 * end in an infinite loop. This stops that from happening ever. */
	ScriptObject::SetCallbackVariable(3, 8);
	return ScriptOrder::_SetOrderFlags();
}

/* static */ bool ScriptOrder::MoveOrder(VehicleID vehicle_id, OrderPosition order_position_move, OrderPosition order_position_target)
{
	order_position_move   = ScriptOrder::ResolveOrderPosition(vehicle_id, order_position_move);
	order_position_target = ScriptOrder::ResolveOrderPosition(vehicle_id, order_position_target);

	EnforcePrecondition(false, IsValidVehicleOrder(vehicle_id, order_position_move));
	EnforcePrecondition(false, IsValidVehicleOrder(vehicle_id, order_position_target));
	EnforcePrecondition(false, order_position_move != order_position_target);

	int order_pos_move = ScriptOrderPositionToRealOrderPosition(vehicle_id, order_position_move);
	int order_pos_target = ScriptOrderPositionToRealOrderPosition(vehicle_id, order_position_target);
	return ScriptObject::DoCommand(0, vehicle_id, order_pos_move | (order_pos_target << 16), CMD_MOVE_ORDER);
}

/* static */ bool ScriptOrder::CopyOrders(VehicleID vehicle_id, VehicleID main_vehicle_id)
{
	EnforcePrecondition(false, ScriptVehicle::IsValidVehicle(vehicle_id));
	EnforcePrecondition(false, ScriptVehicle::IsValidVehicle(main_vehicle_id));

	return ScriptObject::DoCommand(0, vehicle_id | CO_COPY << 30, main_vehicle_id, CMD_CLONE_ORDER);
}

/* static */ bool ScriptOrder::ShareOrders(VehicleID vehicle_id, VehicleID main_vehicle_id)
{
	EnforcePrecondition(false, ScriptVehicle::IsValidVehicle(vehicle_id));
	EnforcePrecondition(false, ScriptVehicle::IsValidVehicle(main_vehicle_id));

	return ScriptObject::DoCommand(0, vehicle_id | CO_SHARE << 30, main_vehicle_id, CMD_CLONE_ORDER);
}

/* static */ bool ScriptOrder::UnshareOrders(VehicleID vehicle_id)
{
	EnforcePrecondition(false, ScriptVehicle::IsValidVehicle(vehicle_id));

	return ScriptObject::DoCommand(0, vehicle_id | CO_UNSHARE << 30, 0, CMD_CLONE_ORDER);
}

/* static */ uint ScriptOrder::GetOrderDistance(ScriptVehicle::VehicleType vehicle_type, TileIndex origin_tile, TileIndex dest_tile)
{
	if (vehicle_type == ScriptVehicle::VT_AIR) {
		if (ScriptTile::IsStationTile(origin_tile)) {
			Station *orig_station = ::Station::GetByTile(origin_tile);
			if (orig_station != nullptr && orig_station->airport.tile != INVALID_TILE) origin_tile = orig_station->airport.tile;
		}
		if (ScriptTile::IsStationTile(dest_tile)) {
			Station *dest_station = ::Station::GetByTile(dest_tile);
			if (dest_station != nullptr && dest_station->airport.tile != INVALID_TILE) dest_tile = dest_station->airport.tile;
		}

		return ScriptMap::DistanceSquare(origin_tile, dest_tile);
	} else {
		return ScriptMap::DistanceManhattan(origin_tile, dest_tile);
	}
}
