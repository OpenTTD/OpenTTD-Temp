/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file vehicle_cmd.cpp Commands for vehicles. */

#include "stdafx.h"
#include "roadveh.h"
#include "news_func.h"
#include "airport.h"
#include "cmd_helper.h"
#include "command_func.h"
#include "company_func.h"
#include "train.h"
#include "aircraft.h"
#include "newgrf_text.h"
#include "vehicle_func.h"
#include "string_func.h"
#include "depot_map.h"
#include "vehiclelist.h"
#include "engine_func.h"
#include "articulated_vehicles.h"
#include "autoreplace_gui.h"
#include "group.h"
#include "order_backup.h"
#include "ship.h"
#include "newgrf.h"
#include "company_base.h"
#include "core/random_func.hpp"
#include <sstream>
#include <iomanip>

#include "table/strings.h"

#include "safeguards.h"

/* Tables used in vehicle.h to find the right command for a certain vehicle type */
const uint32 _veh_build_proc_table[] = {
	CMD_BUILD_VEHICLE | CMD_MSG(STR_ERROR_CAN_T_BUY_TRAIN),
	CMD_BUILD_VEHICLE | CMD_MSG(STR_ERROR_CAN_T_BUY_ROAD_VEHICLE),
	CMD_BUILD_VEHICLE | CMD_MSG(STR_ERROR_CAN_T_BUY_SHIP),
	CMD_BUILD_VEHICLE | CMD_MSG(STR_ERROR_CAN_T_BUY_AIRCRAFT),
};

const uint32 _veh_sell_proc_table[] = {
	CMD_SELL_VEHICLE | CMD_MSG(STR_ERROR_CAN_T_SELL_TRAIN),
	CMD_SELL_VEHICLE | CMD_MSG(STR_ERROR_CAN_T_SELL_ROAD_VEHICLE),
	CMD_SELL_VEHICLE | CMD_MSG(STR_ERROR_CAN_T_SELL_SHIP),
	CMD_SELL_VEHICLE | CMD_MSG(STR_ERROR_CAN_T_SELL_AIRCRAFT),
};

const uint32 _veh_refit_proc_table[] = {
	CMD_REFIT_VEHICLE | CMD_MSG(STR_ERROR_CAN_T_REFIT_TRAIN),
	CMD_REFIT_VEHICLE | CMD_MSG(STR_ERROR_CAN_T_REFIT_ROAD_VEHICLE),
	CMD_REFIT_VEHICLE | CMD_MSG(STR_ERROR_CAN_T_REFIT_SHIP),
	CMD_REFIT_VEHICLE | CMD_MSG(STR_ERROR_CAN_T_REFIT_AIRCRAFT),
};

const uint32 _send_to_depot_proc_table[] = {
	CMD_SEND_VEHICLE_TO_DEPOT | CMD_MSG(STR_ERROR_CAN_T_SEND_TRAIN_TO_DEPOT),
	CMD_SEND_VEHICLE_TO_DEPOT | CMD_MSG(STR_ERROR_CAN_T_SEND_ROAD_VEHICLE_TO_DEPOT),
	CMD_SEND_VEHICLE_TO_DEPOT | CMD_MSG(STR_ERROR_CAN_T_SEND_SHIP_TO_DEPOT),
	CMD_SEND_VEHICLE_TO_DEPOT | CMD_MSG(STR_ERROR_CAN_T_SEND_AIRCRAFT_TO_HANGAR),
};


CommandCost CmdBuildRailVehicle(TileIndex tile, DoCommandFlag flags, const Engine *e, uint16 data, Vehicle **v);
CommandCost CmdBuildRoadVehicle(TileIndex tile, DoCommandFlag flags, const Engine *e, uint16 data, Vehicle **v);
CommandCost CmdBuildShip       (TileIndex tile, DoCommandFlag flags, const Engine *e, uint16 data, Vehicle **v);
CommandCost CmdBuildAircraft   (TileIndex tile, DoCommandFlag flags, const Engine *e, uint16 data, Vehicle **v);

CommandCost CmdRefitVehicle(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const std::string &text);

/**
 * Build a vehicle.
 * @param tile tile of depot where the vehicle is built
 * @param flags for command
 * @param p1 various bitstuffed data
 *  bits  0-15: vehicle type being built.
 *  bits 16-23: vehicle type specific bits passed on to the vehicle build functions.
 *  bits 24-31: refit cargo type.
 * @param p2 User
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdBuildVehicle(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const std::string &text)
{
	/* Elementary check for valid location. */
	if (!IsDepotTile(tile) || !IsTileOwner(tile, _current_company)) return CMD_ERROR;

	VehicleType type = GetDepotVehicleType(tile);

	/* Validate the engine type. */
	EngineID eid = GB(p1, 0, 16);
	if (!IsEngineBuildable(eid, type, _current_company)) return_cmd_error(STR_ERROR_RAIL_VEHICLE_NOT_AVAILABLE + type);

	/* Validate the cargo type. */
	CargoID cargo = GB(p1, 24, 8);
	if (cargo >= NUM_CARGO && cargo != CT_INVALID) return CMD_ERROR;

	const Engine *e = Engine::Get(eid);
	CommandCost value(EXPENSES_NEW_VEHICLES, e->GetCost());

	/* Engines without valid cargo should not be available */
	CargoID default_cargo = e->GetDefaultCargoType();
	if (default_cargo == CT_INVALID) return CMD_ERROR;

	bool refitting = cargo != CT_INVALID && cargo != default_cargo;

	/* Check whether the number of vehicles we need to build can be built according to pool space. */
	uint num_vehicles;
	switch (type) {
		case VEH_TRAIN:    num_vehicles = (e->u.rail.railveh_type == RAILVEH_MULTIHEAD ? 2 : 1) + CountArticulatedParts(eid, false); break;
		case VEH_ROAD:     num_vehicles = 1 + CountArticulatedParts(eid, false); break;
		case VEH_SHIP:     num_vehicles = 1; break;
		case VEH_AIRCRAFT: num_vehicles = e->u.air.subtype & AIR_CTOL ? 2 : 3; break;
		default: NOT_REACHED(); // Safe due to IsDepotTile()
	}
	if (!Vehicle::CanAllocateItem(num_vehicles)) return_cmd_error(STR_ERROR_TOO_MANY_VEHICLES_IN_GAME);

	/* Check whether we can allocate a unit number. Autoreplace does not allocate
	 * an unit number as it will (always) reuse the one of the replaced vehicle
	 * and (train) wagons don't have an unit number in any scenario. */
	UnitID unit_num = (flags & DC_QUERY_COST || flags & DC_AUTOREPLACE || (type == VEH_TRAIN && e->u.rail.railveh_type == RAILVEH_WAGON)) ? 0 : GetFreeUnitNumber(type);
	if (unit_num == UINT16_MAX) return_cmd_error(STR_ERROR_TOO_MANY_VEHICLES_IN_GAME);

	/* If we are refitting we need to temporarily purchase the vehicle to be able to
	 * test it. */
	DoCommandFlag subflags = flags;
	if (refitting && !(flags & DC_EXEC)) subflags |= DC_EXEC | DC_AUTOREPLACE;

	/* Vehicle construction needs random bits, so we have to save the random
	 * seeds to prevent desyncs. */
	SavedRandomSeeds saved_seeds;
	SaveRandomSeeds(&saved_seeds);

	Vehicle *v = nullptr;
	switch (type) {
		case VEH_TRAIN:    value.AddCost(CmdBuildRailVehicle(tile, subflags, e, GB(p1, 16, 8), &v)); break;
		case VEH_ROAD:     value.AddCost(CmdBuildRoadVehicle(tile, subflags, e, GB(p1, 16, 8), &v)); break;
		case VEH_SHIP:     value.AddCost(CmdBuildShip       (tile, subflags, e, GB(p1, 16, 8), &v)); break;
		case VEH_AIRCRAFT: value.AddCost(CmdBuildAircraft   (tile, subflags, e, GB(p1, 16, 8), &v)); break;
		default: NOT_REACHED(); // Safe due to IsDepotTile()
	}

	if (value.Succeeded()) {
		if (subflags & DC_EXEC) {
			v->unitnumber = unit_num;
			v->value      = value.GetCost();
		}

		if (refitting) {
			/* Refit only one vehicle. If we purchased an engine, it may have gained free wagons. */
			value.AddCost(CmdRefitVehicle(tile, flags, v->index, cargo | (1 << 16), {}));
		} else {
			/* Fill in non-refitted capacities */
			_returned_refit_capacity = e->GetDisplayDefaultCapacity(&_returned_mail_refit_capacity);
		}

		if (flags & DC_EXEC) {
			InvalidateWindowData(WC_VEHICLE_DEPOT, v->tile);
			InvalidateWindowClassesData(GetWindowClassForVehicleType(type), 0);
			SetWindowDirty(WC_COMPANY, _current_company);
			if (IsLocalCompany()) {
				InvalidateAutoreplaceWindow(v->engine_type, v->group_id); // updates the auto replace window (must be called before incrementing num_engines)
			}
		}

		if (subflags & DC_EXEC) {
			GroupStatistics::CountEngine(v, 1);
			GroupStatistics::UpdateAutoreplace(_current_company);

			if (v->IsPrimaryVehicle()) {
				GroupStatistics::CountVehicle(v, 1);
				if (!(subflags & DC_AUTOREPLACE)) OrderBackup::Restore(v, p2);
			}
		}


		/* If we are not in DC_EXEC undo everything */
		if (flags != subflags) {
			DoCommand(0, v->index, 0, DC_EXEC, GetCmdSellVeh(v));
		}
	}

	/* Only restore if we actually did some refitting */
	if (flags != subflags) RestoreRandomSeeds(saved_seeds);

	return value;
}

CommandCost CmdSellRailWagon(DoCommandFlag flags, Vehicle *v, uint16 data, uint32 user);

/**
 * Sell a vehicle.
 * @param tile unused.
 * @param flags for command.
 * @param p1 various bitstuffed data.
 *  bits  0-19: vehicle ID being sold.
 *  bits 20-30: vehicle type specific bits passed on to the vehicle build functions.
 *  bit     31: make a backup of the vehicle's order (if an engine).
 * @param p2 User.
 * @param text unused.
 * @return the cost of this operation or an error.
 */
CommandCost CmdSellVehicle(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const std::string &text)
{
	Vehicle *v = Vehicle::GetIfValid(GB(p1, 0, 20));
	if (v == nullptr) return CMD_ERROR;

	Vehicle *front = v->First();

	CommandCost ret = CheckOwnership(front->owner);
	if (ret.Failed()) return ret;

	if (front->vehstatus & VS_CRASHED) return_cmd_error(STR_ERROR_VEHICLE_IS_DESTROYED);

	if (!front->IsStoppedInDepot()) return_cmd_error(STR_ERROR_TRAIN_MUST_BE_STOPPED_INSIDE_DEPOT + front->type);

	/* Can we actually make the order backup, i.e. are there enough orders? */
	if (p1 & MAKE_ORDER_BACKUP_FLAG &&
			front->orders.list != nullptr &&
			!front->orders.list->IsShared() &&
			!Order::CanAllocateItem(front->orders.list->GetNumOrders())) {
		/* Only happens in exceptional cases when there aren't enough orders anyhow.
		 * Thus it should be safe to just drop the orders in that case. */
		p1 &= ~MAKE_ORDER_BACKUP_FLAG;
	}

	if (v->type == VEH_TRAIN) {
		ret = CmdSellRailWagon(flags, v, GB(p1, 20, 12), p2);
	} else {
		ret = CommandCost(EXPENSES_NEW_VEHICLES, -front->value);

		if (flags & DC_EXEC) {
			if (front->IsPrimaryVehicle() && p1 & MAKE_ORDER_BACKUP_FLAG) OrderBackup::Backup(front, p2);
			delete front;
		}
	}

	return ret;
}

/**
 * Helper to run the refit cost callback.
 * @param v The vehicle we are refitting, can be nullptr.
 * @param engine_type Which engine to refit
 * @param new_cid Cargo type we are refitting to.
 * @param new_subtype New cargo subtype.
 * @param[out] auto_refit_allowed The refit is allowed as an auto-refit.
 * @return Price for refitting
 */
static int GetRefitCostFactor(const Vehicle *v, EngineID engine_type, CargoID new_cid, byte new_subtype, bool *auto_refit_allowed)
{
	/* Prepare callback param with info about the new cargo type. */
	const Engine *e = Engine::Get(engine_type);

	/* Is this vehicle a NewGRF vehicle? */
	if (e->GetGRF() != nullptr) {
		const CargoSpec *cs = CargoSpec::Get(new_cid);
		uint32 param1 = (cs->classes << 16) | (new_subtype << 8) | e->GetGRF()->cargo_map[new_cid];

		uint16 cb_res = GetVehicleCallback(CBID_VEHICLE_REFIT_COST, param1, 0, engine_type, v);
		if (cb_res != CALLBACK_FAILED) {
			*auto_refit_allowed = HasBit(cb_res, 14);
			int factor = GB(cb_res, 0, 14);
			if (factor >= 0x2000) factor -= 0x4000; // Treat as signed integer.
			return factor;
		}
	}

	*auto_refit_allowed = e->info.refit_cost == 0;
	return (v == nullptr || v->cargo_type != new_cid) ? e->info.refit_cost : 0;
}

/**
 * Learn the price of refitting a certain engine
 * @param v The vehicle we are refitting, can be nullptr.
 * @param engine_type Which engine to refit
 * @param new_cid Cargo type we are refitting to.
 * @param new_subtype New cargo subtype.
 * @param[out] auto_refit_allowed The refit is allowed as an auto-refit.
 * @return Price for refitting
 */
static CommandCost GetRefitCost(const Vehicle *v, EngineID engine_type, CargoID new_cid, byte new_subtype, bool *auto_refit_allowed)
{
	ExpensesType expense_type;
	const Engine *e = Engine::Get(engine_type);
	Price base_price;
	int cost_factor = GetRefitCostFactor(v, engine_type, new_cid, new_subtype, auto_refit_allowed);
	switch (e->type) {
		case VEH_SHIP:
			base_price = PR_BUILD_VEHICLE_SHIP;
			expense_type = EXPENSES_SHIP_RUN;
			break;

		case VEH_ROAD:
			base_price = PR_BUILD_VEHICLE_ROAD;
			expense_type = EXPENSES_ROADVEH_RUN;
			break;

		case VEH_AIRCRAFT:
			base_price = PR_BUILD_VEHICLE_AIRCRAFT;
			expense_type = EXPENSES_AIRCRAFT_RUN;
			break;

		case VEH_TRAIN:
			base_price = (e->u.rail.railveh_type == RAILVEH_WAGON) ? PR_BUILD_VEHICLE_WAGON : PR_BUILD_VEHICLE_TRAIN;
			cost_factor <<= 1;
			expense_type = EXPENSES_TRAIN_RUN;
			break;

		default: NOT_REACHED();
	}
	if (cost_factor < 0) {
		return CommandCost(expense_type, -GetPrice(base_price, -cost_factor, e->GetGRF(), -10));
	} else {
		return CommandCost(expense_type, GetPrice(base_price, cost_factor, e->GetGRF(), -10));
	}
}

/** Helper structure for RefitVehicle() */
struct RefitResult {
	Vehicle *v;         ///< Vehicle to refit
	uint capacity;      ///< New capacity of vehicle
	uint mail_capacity; ///< New mail capacity of aircraft
	byte subtype;       ///< cargo subtype to refit to
};

/**
 * Refits a vehicle (chain).
 * This is the vehicle-type independent part of the CmdRefitXXX functions.
 * @param v            The vehicle to refit.
 * @param only_this    Whether to only refit this vehicle, or to check the rest of them.
 * @param num_vehicles Number of vehicles to refit (not counting articulated parts). Zero means the whole chain.
 * @param new_cid      Cargotype to refit to
 * @param new_subtype  Cargo subtype to refit to. 0xFF means to try keeping the same subtype according to GetBestFittingSubType().
 * @param flags        Command flags
 * @param auto_refit   Refitting is done as automatic refitting outside a depot.
 * @return Refit cost.
 */
static CommandCost RefitVehicle(Vehicle *v, bool only_this, uint8 num_vehicles, CargoID new_cid, byte new_subtype, DoCommandFlag flags, bool auto_refit)
{
	CommandCost cost(v->GetExpenseType(false));
	uint total_capacity = 0;
	uint total_mail_capacity = 0;
	num_vehicles = num_vehicles == 0 ? UINT8_MAX : num_vehicles;

	VehicleSet vehicles_to_refit;
	if (!only_this) {
		GetVehicleSet(vehicles_to_refit, v, num_vehicles);
		/* In this case, we need to check the whole chain. */
		v = v->First();
	}

	std::vector<RefitResult> refit_result;

	v->InvalidateNewGRFCacheOfChain();
	byte actual_subtype = new_subtype;
	for (; v != nullptr; v = (only_this ? nullptr : v->Next())) {
		/* Reset actual_subtype for every new vehicle */
		if (!v->IsArticulatedPart()) actual_subtype = new_subtype;

		if (v->type == VEH_TRAIN && std::find(vehicles_to_refit.begin(), vehicles_to_refit.end(), v->index) == vehicles_to_refit.end() && !only_this) continue;

		const Engine *e = v->GetEngine();
		if (!e->CanCarryCargo()) continue;

		/* If the vehicle is not refittable, or does not allow automatic refitting,
		 * count its capacity nevertheless if the cargo matches */
		bool refittable = HasBit(e->info.refit_mask, new_cid) && (!auto_refit || HasBit(e->info.misc_flags, EF_AUTO_REFIT));
		if (!refittable && v->cargo_type != new_cid) continue;

		/* Determine best fitting subtype if requested */
		if (actual_subtype == 0xFF) {
			actual_subtype = GetBestFittingSubType(v, v, new_cid);
		}

		/* Back up the vehicle's cargo type */
		CargoID temp_cid = v->cargo_type;
		byte temp_subtype = v->cargo_subtype;
		if (refittable) {
			v->cargo_type = new_cid;
			v->cargo_subtype = actual_subtype;
		}

		uint16 mail_capacity = 0;
		uint amount = e->DetermineCapacity(v, &mail_capacity);
		total_capacity += amount;
		/* mail_capacity will always be zero if the vehicle is not an aircraft. */
		total_mail_capacity += mail_capacity;

		if (!refittable) continue;

		/* Restore the original cargo type */
		v->cargo_type = temp_cid;
		v->cargo_subtype = temp_subtype;

		bool auto_refit_allowed;
		CommandCost refit_cost = GetRefitCost(v, v->engine_type, new_cid, actual_subtype, &auto_refit_allowed);
		if (auto_refit && (flags & DC_QUERY_COST) == 0 && !auto_refit_allowed) {
			/* Sorry, auto-refitting not allowed, subtract the cargo amount again from the total.
			 * When querrying cost/capacity (for example in order refit GUI), we always assume 'allowed'.
			 * It is not predictable. */
			total_capacity -= amount;
			total_mail_capacity -= mail_capacity;

			if (v->cargo_type == new_cid) {
				/* Add the old capacity nevertheless, if the cargo matches */
				total_capacity += v->cargo_cap;
				if (v->type == VEH_AIRCRAFT) total_mail_capacity += v->Next()->cargo_cap;
			}
			continue;
		}
		cost.AddCost(refit_cost);

		/* Record the refitting.
		 * Do not execute the refitting immediately, so DetermineCapacity and GetRefitCost do the same in test and exec run.
		 * (weird NewGRFs)
		 * Note:
		 *  - If the capacity of vehicles depends on other vehicles in the chain, the actual capacity is
		 *    set after RefitVehicle() via ConsistChanged() and friends. The estimation via _returned_refit_capacity will be wrong.
		 *  - We have to call the refit cost callback with the pre-refit configuration of the chain because we want refit and
		 *    autorefit to behave the same, and we need its result for auto_refit_allowed.
		 */
		refit_result.push_back({v, amount, mail_capacity, actual_subtype});
	}

	if (flags & DC_EXEC) {
		/* Store the result */
		for (RefitResult &result : refit_result) {
			Vehicle *u = result.v;
			u->refit_cap = (u->cargo_type == new_cid) ? std::min<uint16>(result.capacity, u->refit_cap) : 0;
			if (u->cargo.TotalCount() > u->refit_cap) u->cargo.Truncate(u->cargo.TotalCount() - u->refit_cap);
			u->cargo_type = new_cid;
			u->cargo_cap = result.capacity;
			u->cargo_subtype = result.subtype;
			if (u->type == VEH_AIRCRAFT) {
				Vehicle *w = u->Next();
				w->refit_cap = std::min<uint16>(w->refit_cap, result.mail_capacity);
				w->cargo_cap = result.mail_capacity;
				if (w->cargo.TotalCount() > w->refit_cap) w->cargo.Truncate(w->cargo.TotalCount() - w->refit_cap);
			}
		}
	}

	refit_result.clear();
	_returned_refit_capacity = total_capacity;
	_returned_mail_refit_capacity = total_mail_capacity;
	return cost;
}

/**
 * Refits a vehicle to the specified cargo type.
 * @param tile unused
 * @param flags type of operation
 * @param p1 vehicle ID to refit
 * @param p2 various bitstuffed elements
 * - p2 = (bit 0-7)   - New cargo type to refit to.
 * - p2 = (bit 8-15)  - New cargo subtype to refit to. 0xFF means to try keeping the same subtype according to GetBestFittingSubType().
 * - p2 = (bit 16-23) - Number of vehicles to refit (not counting articulated parts). Zero means all vehicles.
 *                      Only used if "refit only this vehicle" is false.
 * - p2 = (bit 24)     - Automatic refitting.
 * - p2 = (bit 25)     - Refit only this vehicle. Used only for cloning vehicles.
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdRefitVehicle(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const std::string &text)
{
	Vehicle *v = Vehicle::GetIfValid(p1);
	if (v == nullptr) return CMD_ERROR;

	/* Don't allow disasters and sparks and such to be refitted.
	 * We cannot check for IsPrimaryVehicle as autoreplace also refits in free wagon chains. */
	if (!IsCompanyBuildableVehicleType(v->type)) return CMD_ERROR;

	Vehicle *front = v->First();

	CommandCost ret = CheckOwnership(front->owner);
	if (ret.Failed()) return ret;

	bool auto_refit = HasBit(p2, 24);
	bool free_wagon = v->type == VEH_TRAIN && Train::From(front)->IsFreeWagon(); // used by autoreplace/renew

	/* Don't allow shadows and such to be refitted. */
	if (v != front && (v->type == VEH_SHIP || v->type == VEH_AIRCRAFT)) return CMD_ERROR;

	/* Allow auto-refitting only during loading and normal refitting only in a depot. */
	if ((flags & DC_QUERY_COST) == 0 && // used by the refit GUI, including the order refit GUI.
			!free_wagon && // used by autoreplace/renew
			(!auto_refit || !front->current_order.IsType(OT_LOADING)) && // refit inside stations
			!front->IsStoppedInDepot()) { // refit inside depots
		return_cmd_error(STR_ERROR_TRAIN_MUST_BE_STOPPED_INSIDE_DEPOT + front->type);
	}

	if (front->vehstatus & VS_CRASHED) return_cmd_error(STR_ERROR_VEHICLE_IS_DESTROYED);

	/* Check cargo */
	CargoID new_cid = GB(p2, 0, 8);
	byte new_subtype = GB(p2, 8, 8);
	if (new_cid >= NUM_CARGO) return CMD_ERROR;

	/* For ships and aircraft there is always only one. */
	bool only_this = HasBit(p2, 25) || front->type == VEH_SHIP || front->type == VEH_AIRCRAFT;
	uint8 num_vehicles = GB(p2, 16, 8);

	CommandCost cost = RefitVehicle(v, only_this, num_vehicles, new_cid, new_subtype, flags, auto_refit);

	if (flags & DC_EXEC) {
		/* Update the cached variables */
		switch (v->type) {
			case VEH_TRAIN:
				Train::From(front)->ConsistChanged(auto_refit ? CCF_AUTOREFIT : CCF_REFIT);
				break;
			case VEH_ROAD:
				RoadVehUpdateCache(RoadVehicle::From(front), auto_refit);
				if (_settings_game.vehicle.roadveh_acceleration_model != AM_ORIGINAL) RoadVehicle::From(front)->CargoChanged();
				break;

			case VEH_SHIP:
				v->InvalidateNewGRFCacheOfChain();
				Ship::From(v)->UpdateCache();
				break;

			case VEH_AIRCRAFT:
				v->InvalidateNewGRFCacheOfChain();
				UpdateAircraftCache(Aircraft::From(v), true);
				break;

			default: NOT_REACHED();
		}
		front->MarkDirty();

		if (!free_wagon) {
			InvalidateWindowData(WC_VEHICLE_DETAILS, front->index);
			InvalidateWindowClassesData(GetWindowClassForVehicleType(v->type), 0);
		}
		SetWindowDirty(WC_VEHICLE_DEPOT, front->tile);
	} else {
		/* Always invalidate the cache; querycost might have filled it. */
		v->InvalidateNewGRFCacheOfChain();
	}

	return cost;
}

/**
 * Start/Stop a vehicle
 * @param tile unused
 * @param flags type of operation
 * @param p1 vehicle to start/stop, don't forget to change CcStartStopVehicle if you modify this!
 * @param p2 bit 0: Shall the start/stop newgrf callback be evaluated (only valid with DC_AUTOREPLACE for network safety)
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdStartStopVehicle(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const std::string &text)
{
	/* Disable the effect of p2 bit 0, when DC_AUTOREPLACE is not set */
	if ((flags & DC_AUTOREPLACE) == 0) SetBit(p2, 0);

	Vehicle *v = Vehicle::GetIfValid(p1);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->vehstatus & VS_CRASHED) return_cmd_error(STR_ERROR_VEHICLE_IS_DESTROYED);

	switch (v->type) {
		case VEH_TRAIN:
			if ((v->vehstatus & VS_STOPPED) && Train::From(v)->gcache.cached_power == 0) return_cmd_error(STR_ERROR_TRAIN_START_NO_POWER);
			break;

		case VEH_SHIP:
		case VEH_ROAD:
			break;

		case VEH_AIRCRAFT: {
			Aircraft *a = Aircraft::From(v);
			/* cannot stop airplane when in flight, or when taking off / landing */
			if (a->state >= STARTTAKEOFF && a->state < TERM7) return_cmd_error(STR_ERROR_AIRCRAFT_IS_IN_FLIGHT);
			if (HasBit(a->flags, VAF_HELI_DIRECT_DESCENT)) return_cmd_error(STR_ERROR_AIRCRAFT_IS_IN_FLIGHT);
			break;
		}

		default: return CMD_ERROR;
	}

	if (HasBit(p2, 0)) {
		/* Check if this vehicle can be started/stopped. Failure means 'allow'. */
		uint16 callback = GetVehicleCallback(CBID_VEHICLE_START_STOP_CHECK, 0, 0, v->engine_type, v);
		StringID error = STR_NULL;
		if (callback != CALLBACK_FAILED) {
			if (v->GetGRF()->grf_version < 8) {
				/* 8 bit result 0xFF means 'allow' */
				if (callback < 0x400 && GB(callback, 0, 8) != 0xFF) error = GetGRFStringID(v->GetGRFID(), 0xD000 + callback);
			} else {
				if (callback < 0x400) {
					error = GetGRFStringID(v->GetGRFID(), 0xD000 + callback);
				} else {
					switch (callback) {
						case 0x400: // allow
							break;

						default: // unknown reason -> disallow
							error = STR_ERROR_INCOMPATIBLE_RAIL_TYPES;
							break;
					}
				}
			}
		}
		if (error != STR_NULL) return_cmd_error(error);
	}

	if (flags & DC_EXEC) {
		if (v->IsStoppedInDepot() && (flags & DC_AUTOREPLACE) == 0) DeleteVehicleNews(p1, STR_NEWS_TRAIN_IS_WAITING + v->type);

		v->vehstatus ^= VS_STOPPED;
		if (v->type != VEH_TRAIN) v->cur_speed = 0; // trains can stop 'slowly'
		v->MarkDirty();
		SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
		SetWindowDirty(WC_VEHICLE_DEPOT, v->tile);
		SetWindowClassesDirty(GetWindowClassForVehicleType(v->type));
		InvalidateWindowData(WC_VEHICLE_VIEW, v->index);
	}
	return CommandCost();
}

/**
 * Starts or stops a lot of vehicles
 * @param tile Tile of the depot where the vehicles are started/stopped (only used for depots)
 * @param flags type of operation
 * @param p1 bitmask
 *   - bit 0 set = start vehicles, unset = stop vehicles
 *   - bit 1 if set, then it's a vehicle list window, not a depot and Tile is ignored in this case
 * @param p2 packed VehicleListIdentifier
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdMassStartStopVehicle(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const std::string &text)
{
	VehicleList list;
	bool do_start = HasBit(p1, 0);
	bool vehicle_list_window = HasBit(p1, 1);

	VehicleListIdentifier vli;
	if (!vli.UnpackIfValid(p2)) return CMD_ERROR;
	if (!IsCompanyBuildableVehicleType(vli.vtype)) return CMD_ERROR;

	if (vehicle_list_window) {
		if (!GenerateVehicleSortList(&list, vli)) return CMD_ERROR;
	} else {
		/* Get the list of vehicles in the depot */
		BuildDepotVehicleList(vli.vtype, tile, &list, nullptr);
	}

	for (uint i = 0; i < list.size(); i++) {
		const Vehicle *v = list[i];

		if (!!(v->vehstatus & VS_STOPPED) != do_start) continue;

		if (!vehicle_list_window && !v->IsChainInDepot()) continue;

		/* Just try and don't care if some vehicle's can't be stopped. */
		DoCommand(tile, v->index, 0, flags, CMD_START_STOP_VEHICLE);
	}

	return CommandCost();
}

/**
 * Sells all vehicles in a depot
 * @param tile Tile of the depot where the depot is
 * @param flags type of operation
 * @param p1 Vehicle type
 * @param p2 unused
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdDepotSellAllVehicles(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const std::string &text)
{
	VehicleList list;

	CommandCost cost(EXPENSES_NEW_VEHICLES);
	VehicleType vehicle_type = Extract<VehicleType, 0, 3>(p1);

	if (!IsCompanyBuildableVehicleType(vehicle_type)) return CMD_ERROR;

	uint sell_command = GetCmdSellVeh(vehicle_type);

	/* Get the list of vehicles in the depot */
	BuildDepotVehicleList(vehicle_type, tile, &list, &list);

	CommandCost last_error = CMD_ERROR;
	bool had_success = false;
	for (uint i = 0; i < list.size(); i++) {
		CommandCost ret = DoCommand(tile, list[i]->index | (1 << 20), 0, flags, sell_command);
		if (ret.Succeeded()) {
			cost.AddCost(ret);
			had_success = true;
		} else {
			last_error = ret;
		}
	}

	return had_success ? cost : last_error;
}

/**
 * Autoreplace all vehicles in the depot
 * @param tile Tile of the depot where the vehicles are
 * @param flags type of operation
 * @param p1 Type of vehicle
 * @param p2 unused
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdDepotMassAutoReplace(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const std::string &text)
{
	VehicleList list;
	CommandCost cost = CommandCost(EXPENSES_NEW_VEHICLES);
	VehicleType vehicle_type = Extract<VehicleType, 0, 3>(p1);

	if (!IsCompanyBuildableVehicleType(vehicle_type)) return CMD_ERROR;
	if (!IsDepotTile(tile) || !IsTileOwner(tile, _current_company)) return CMD_ERROR;

	/* Get the list of vehicles in the depot */
	BuildDepotVehicleList(vehicle_type, tile, &list, &list, true);

	for (uint i = 0; i < list.size(); i++) {
		const Vehicle *v = list[i];

		/* Ensure that the vehicle completely in the depot */
		if (!v->IsChainInDepot()) continue;

		CommandCost ret = DoCommand(0, v->index, 0, flags, CMD_AUTOREPLACE_VEHICLE);

		if (ret.Succeeded()) cost.AddCost(ret);
	}
	return cost;
}

/**
 * Test if a name is unique among vehicle names.
 * @param name Name to test.
 * @return True ifffffff the name is unique.
 */
static bool IsUniqueVehicleName(const std::string &name)
{
	for (const Vehicle *v : Vehicle::Iterate()) {
		if (!v->name.empty() && v->name == name) return false;
	}

	return true;
}

/**
 * Clone the custom name of a vehicle, adding or incrementing a number.
 * @param src Source vehicle, with a custom name.
 * @param dst Destination vehicle.
 */
static void CloneVehicleName(const Vehicle *src, Vehicle *dst)
{
	std::string buf;

	/* Find the position of the first digit in the last group of digits. */
	size_t number_position;
	for (number_position = src->name.length(); number_position > 0; number_position--) {
		/* The design of UTF-8 lets this work simply without having to check
		 * for UTF-8 sequences. */
		if (src->name[number_position - 1] < '0' || src->name[number_position - 1] > '9') break;
	}

	/* Format buffer and determine starting number. */
	long num;
	byte padding = 0;
	if (number_position == src->name.length()) {
		/* No digit at the end, so start at number 2. */
		buf = src->name;
		buf += " ";
		number_position = buf.length();
		num = 2;
	} else {
		/* Found digits, parse them and start at the next number. */
		buf = src->name.substr(0, number_position);

		auto num_str = src->name.substr(number_position);
		padding = (byte)num_str.length();

		std::istringstream iss(num_str);
		iss >> num;
		num++;
	}

	/* Check if this name is already taken. */
	for (int max_iterations = 1000; max_iterations > 0; max_iterations--, num++) {
		std::ostringstream oss;

		/* Attach the number to the temporary name. */
		oss << buf << std::setw(padding) << std::setfill('0') << std::internal << num;

		/* Check the name is unique. */
		auto new_name = oss.str();
		if (IsUniqueVehicleName(new_name)) {
			dst->name = new_name;
			break;
		}
	}

	/* All done. If we didn't find a name, it'll just use its default. */
}

/**
 * Clone a vehicle. If it is a train, it will clone all the cars too
 * @param tile tile of the depot where the cloned vehicle is build
 * @param flags type of operation
 * @param p1 the original vehicle's index
 * @param p2 1 = shared orders, else copied orders
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdCloneVehicle(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const std::string &text)
{
	CommandCost total_cost(EXPENSES_NEW_VEHICLES);

	Vehicle *v = Vehicle::GetIfValid(p1);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;
	Vehicle *v_front = v;
	Vehicle *w = nullptr;
	Vehicle *w_front = nullptr;
	Vehicle *w_rear = nullptr;

	/*
	 * v_front is the front engine in the original vehicle
	 * v is the car/vehicle of the original vehicle that is currently being copied
	 * w_front is the front engine of the cloned vehicle
	 * w is the car/vehicle currently being cloned
	 * w_rear is the rear end of the cloned train. It's used to add more cars and is only used by trains
	 */

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->type == VEH_TRAIN && (!v->IsFrontEngine() || Train::From(v)->crash_anim_pos >= 4400)) return CMD_ERROR;

	/* check that we can allocate enough vehicles */
	if (!(flags & DC_EXEC)) {
		int veh_counter = 0;
		do {
			veh_counter++;
		} while ((v = v->Next()) != nullptr);

		if (!Vehicle::CanAllocateItem(veh_counter)) {
			return_cmd_error(STR_ERROR_TOO_MANY_VEHICLES_IN_GAME);
		}
	}

	v = v_front;

	do {
		if (v->type == VEH_TRAIN && Train::From(v)->IsRearDualheaded()) {
			/* we build the rear ends of multiheaded trains with the front ones */
			continue;
		}

		/* In case we're building a multi headed vehicle and the maximum number of
		 * vehicles is almost reached (e.g. max trains - 1) not all vehicles would
		 * be cloned. When the non-primary engines were build they were seen as
		 * 'new' vehicles whereas they would immediately be joined with a primary
		 * engine. This caused the vehicle to be not build as 'the limit' had been
		 * reached, resulting in partially build vehicles and such. */
		DoCommandFlag build_flags = flags;
		if ((flags & DC_EXEC) && !v->IsPrimaryVehicle()) build_flags |= DC_AUTOREPLACE;

		CommandCost cost = DoCommand(tile, v->engine_type | (1 << 16) | (CT_INVALID << 24), 0, build_flags, GetCmdBuildVeh(v));

		if (cost.Failed()) {
			/* Can't build a part, then sell the stuff we already made; clear up the mess */
			if (w_front != nullptr) DoCommand(w_front->tile, w_front->index | (1 << 20), 0, flags, GetCmdSellVeh(w_front));
			return cost;
		}

		total_cost.AddCost(cost);

		if (flags & DC_EXEC) {
			w = Vehicle::Get(_new_vehicle_id);

			if (v->type == VEH_TRAIN && HasBit(Train::From(v)->flags, VRF_REVERSE_DIRECTION)) {
				SetBit(Train::From(w)->flags, VRF_REVERSE_DIRECTION);
			}

			if (v->type == VEH_TRAIN && !v->IsFrontEngine()) {
				/* this s a train car
				 * add this unit to the end of the train */
				CommandCost result = DoCommand(0, w->index | 1 << 20, w_rear->index, flags, CMD_MOVE_RAIL_VEHICLE);
				if (result.Failed()) {
					/* The train can't be joined to make the same consist as the original.
					 * Sell what we already made (clean up) and return an error.           */
					DoCommand(w_front->tile, w_front->index | 1 << 20, 0, flags, GetCmdSellVeh(w_front));
					DoCommand(w_front->tile, w->index       | 1 << 20, 0, flags, GetCmdSellVeh(w));
					return result; // return error and the message returned from CMD_MOVE_RAIL_VEHICLE
				}
			} else {
				/* this is a front engine or not a train. */
				w_front = w;
				w->service_interval = v->service_interval;
				w->SetServiceIntervalIsCustom(v->ServiceIntervalIsCustom());
				w->SetServiceIntervalIsPercent(v->ServiceIntervalIsPercent());
			}
			w_rear = w; // trains needs to know the last car in the train, so they can add more in next loop
		}
	} while (v->type == VEH_TRAIN && (v = v->GetNextVehicle()) != nullptr);

	if ((flags & DC_EXEC) && v_front->type == VEH_TRAIN) {
		/* for trains this needs to be the front engine due to the callback function */
		_new_vehicle_id = w_front->index;
	}

	if (flags & DC_EXEC) {
		/* Cloned vehicles belong to the same group */
		DoCommand(0, v_front->group_id, w_front->index, flags, CMD_ADD_VEHICLE_GROUP);
	}


	/* Take care of refitting. */
	w = w_front;
	v = v_front;

	/* Both building and refitting are influenced by newgrf callbacks, which
	 * makes it impossible to accurately estimate the cloning costs. In
	 * particular, it is possible for engines of the same type to be built with
	 * different numbers of articulated parts, so when refitting we have to
	 * loop over real vehicles first, and then the articulated parts of those
	 * vehicles in a different loop. */
	do {
		do {
			if (flags & DC_EXEC) {
				assert(w != nullptr);

				/* Find out what's the best sub type */
				byte subtype = GetBestFittingSubType(v, w, v->cargo_type);
				if (w->cargo_type != v->cargo_type || w->cargo_subtype != subtype) {
					CommandCost cost = DoCommand(0, w->index, v->cargo_type | 1U << 25 | (subtype << 8), flags, GetCmdRefitVeh(v));
					if (cost.Succeeded()) total_cost.AddCost(cost);
				}

				if (w->IsGroundVehicle() && w->HasArticulatedPart()) {
					w = w->GetNextArticulatedPart();
				} else {
					break;
				}
			} else {
				const Engine *e = v->GetEngine();
				CargoID initial_cargo = (e->CanCarryCargo() ? e->GetDefaultCargoType() : (CargoID)CT_INVALID);

				if (v->cargo_type != initial_cargo && initial_cargo != CT_INVALID) {
					bool dummy;
					total_cost.AddCost(GetRefitCost(nullptr, v->engine_type, v->cargo_type, v->cargo_subtype, &dummy));
				}
			}

			if (v->IsGroundVehicle() && v->HasArticulatedPart()) {
				v = v->GetNextArticulatedPart();
			} else {
				break;
			}
		} while (v != nullptr);

		if ((flags & DC_EXEC) && v->type == VEH_TRAIN) w = w->GetNextVehicle();
	} while (v->type == VEH_TRAIN && (v = v->GetNextVehicle()) != nullptr);

	if (flags & DC_EXEC) {
		/*
		 * Set the orders of the vehicle. Cannot do it earlier as we need
		 * the vehicle refitted before doing this, otherwise the moved
		 * cargo types might not match (passenger vs non-passenger)
		 */
		CommandCost result = DoCommand(0, w_front->index | (p2 & 1 ? CO_SHARE : CO_COPY) << 30, v_front->index, flags, CMD_CLONE_ORDER);
		if (result.Failed()) {
			/* The vehicle has already been bought, so now it must be sold again. */
			DoCommand(w_front->tile, w_front->index | 1 << 20, 0, flags, GetCmdSellVeh(w_front));
			return result;
		}

		/* Now clone the vehicle's name, if it has one. */
		if (!v_front->name.empty()) CloneVehicleName(v_front, w_front);

		/* Since we can't estimate the cost of cloning a vehicle accurately we must
		 * check whether the company has enough money manually. */
		if (!CheckCompanyHasMoney(total_cost)) {
			/* The vehicle has already been bought, so now it must be sold again. */
			DoCommand(w_front->tile, w_front->index | 1 << 20, 0, flags, GetCmdSellVeh(w_front));
			return total_cost;
		}
	}

	return total_cost;
}

/**
 * Send all vehicles of type to depots
 * @param flags   the flags used for DoCommand()
 * @param service should the vehicles only get service in the depots
 * @param vli     identifier of the vehicle list
 * @return 0 for success and CMD_ERROR if no vehicle is able to go to depot
 */
static CommandCost SendAllVehiclesToDepot(DoCommandFlag flags, bool service, const VehicleListIdentifier &vli)
{
	VehicleList list;

	if (!GenerateVehicleSortList(&list, vli)) return CMD_ERROR;

	/* Send all the vehicles to a depot */
	bool had_success = false;
	for (uint i = 0; i < list.size(); i++) {
		const Vehicle *v = list[i];
		CommandCost ret = DoCommand(v->tile, v->index | (service ? DEPOT_SERVICE : 0U) | DEPOT_DONT_CANCEL, 0, flags, GetCmdSendToDepot(vli.vtype));

		if (ret.Succeeded()) {
			had_success = true;

			/* Return 0 if DC_EXEC is not set this is a valid goto depot command)
			 * In this case we know that at least one vehicle can be sent to a depot
			 * and we will issue the command. We can now safely quit the loop, knowing
			 * it will succeed at least once. With DC_EXEC we really need to send them to the depot */
			if (!(flags & DC_EXEC)) break;
		}
	}

	return had_success ? CommandCost() : CMD_ERROR;
}

/**
 * Send a vehicle to the depot.
 * @param tile unused
 * @param flags for command type
 * @param p1 bitmask
 * - p1 0-20: bitvehicle ID to send to the depot
 * - p1 bits 25-8  - DEPOT_ flags (see vehicle_type.h)
 * @param p2 packed VehicleListIdentifier.
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdSendVehicleToDepot(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const std::string &text)
{
	if (p1 & DEPOT_MASS_SEND) {
		/* Mass goto depot requested */
		VehicleListIdentifier vli;
		if (!vli.UnpackIfValid(p2)) return CMD_ERROR;
		return SendAllVehiclesToDepot(flags, (p1 & DEPOT_SERVICE) != 0, vli);
	}

	Vehicle *v = Vehicle::GetIfValid(GB(p1, 0, 20));
	if (v == nullptr) return CMD_ERROR;
	if (!v->IsPrimaryVehicle()) return CMD_ERROR;

	return v->SendToDepot(flags, (DepotCommand)(p1 & DEPOT_COMMAND_MASK));
}

/**
 * Give a custom name to your vehicle
 * @param tile unused
 * @param flags type of operation
 * @param p1 vehicle ID to name
 * @param p2 unused
 * @param text the new name or an empty string when resetting to the default
 * @return the cost of this operation or an error
 */
CommandCost CmdRenameVehicle(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const std::string &text)
{
	Vehicle *v = Vehicle::GetIfValid(p1);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	bool reset = text.empty();

	if (!reset) {
		if (Utf8StringLength(text) >= MAX_LENGTH_VEHICLE_NAME_CHARS) return CMD_ERROR;
		if (!(flags & DC_AUTOREPLACE) && !IsUniqueVehicleName(text)) return_cmd_error(STR_ERROR_NAME_MUST_BE_UNIQUE);
	}

	if (flags & DC_EXEC) {
		if (reset) {
			v->name.clear();
		} else {
			v->name = text;
		}
		InvalidateWindowClassesData(GetWindowClassForVehicleType(v->type), 1);
		MarkWholeScreenDirty();
	}

	return CommandCost();
}


/**
 * Change the service interval of a vehicle
 * @param tile unused
 * @param flags type of operation
 * @param p1 vehicle ID that is being service-interval-changed
 * @param p2 bitmask
 * - p2 = (bit  0-15) - new service interval
 * - p2 = (bit 16)    - service interval is custom flag
 * - p2 = (bit 17)    - service interval is percentage flag
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdChangeServiceInt(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const std::string &text)
{
	Vehicle *v = Vehicle::GetIfValid(p1);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	const Company *company = Company::Get(v->owner);
	bool iscustom  = HasBit(p2, 16);
	bool ispercent = iscustom ? HasBit(p2, 17) : company->settings.vehicle.servint_ispercent;

	uint16 serv_int;
	if (iscustom) {
		serv_int = GB(p2, 0, 16);
		if (serv_int != GetServiceIntervalClamped(serv_int, ispercent)) return CMD_ERROR;
	} else {
		serv_int = CompanyServiceInterval(company, v->type);
	}

	if (flags & DC_EXEC) {
		v->SetServiceInterval(serv_int);
		v->SetServiceIntervalIsCustom(iscustom);
		v->SetServiceIntervalIsPercent(ispercent);
		SetWindowDirty(WC_VEHICLE_DETAILS, v->index);
	}

	return CommandCost();
}
