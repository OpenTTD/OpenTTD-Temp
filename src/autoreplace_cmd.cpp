/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file autoreplace_cmd.cpp Deals with autoreplace execution but not the setup */

#include "stdafx.h"
#include "company_func.h"
#include "train.h"
#include "command_func.h"
#include "engine_func.h"
#include "vehicle_func.h"
#include "autoreplace_func.h"
#include "autoreplace_gui.h"
#include "articulated_vehicles.h"
#include "core/random_func.hpp"
#include "vehiclelist.h"
#include "road.h"
#include "ship.h"
#include "ai/ai.hpp"
#include "news_func.h"
#include "strings_func.h"
#include "depot_map.h"

#include "table/strings.h"

#include "safeguards.h"

extern void ChangeVehicleViewports(VehicleID from_index, VehicleID to_index);
extern void ChangeVehicleNews(VehicleID from_index, VehicleID to_index);
extern void ChangeVehicleViewWindow(VehicleID from_index, VehicleID to_index);

/**
 * Figure out if two engines got at least one type of cargo in common (refitting if needed)
 * @param engine_a one of the EngineIDs
 * @param engine_b the other EngineID
 * @return true if they can both carry the same type of cargo (or at least one of them got no capacity at all)
 */
static bool EnginesHaveCargoInCommon(EngineID engine_a, EngineID engine_b)
{
	CargoTypes available_cargoes_a = GetUnionOfArticulatedRefitMasks(engine_a, true);
	CargoTypes available_cargoes_b = GetUnionOfArticulatedRefitMasks(engine_b, true);
	return (available_cargoes_a == 0 || available_cargoes_b == 0 || (available_cargoes_a & available_cargoes_b) != 0);
}

/**
 * Checks some basic properties whether autoreplace is allowed
 * @param from Origin engine
 * @param to Destination engine
 * @param company Company to check for
 * @return true if autoreplace is allowed
 */
bool CheckAutoreplaceValidity(EngineID from, EngineID to, CompanyID company)
{
	assert(Engine::IsValidID(from) && Engine::IsValidID(to));

	/* we can't replace an engine into itself (that would be autorenew) */
	if (from == to) return false;

	const Engine *e_from = Engine::Get(from);
	const Engine *e_to = Engine::Get(to);
	VehicleType type = e_from->type;

	/* check that the new vehicle type is available to the company and its type is the same as the original one */
	if (!IsEngineBuildable(to, type, company)) return false;

	switch (type) {
		case VEH_TRAIN: {
			/* make sure the railtypes are compatible */
			if (!_settings_game.depot.allow_no_comp_railtype_replacements && (GetRailTypeInfo(e_from->u.rail.railtype)->compatible_railtypes & GetRailTypeInfo(e_to->u.rail.railtype)->compatible_railtypes) == 0) return false;

			/* make sure we do not replace wagons with engines or vice versa */
			if ((e_from->u.rail.railveh_type == RAILVEH_WAGON) != (e_to->u.rail.railveh_type == RAILVEH_WAGON)) return false;
			break;
		}

		case VEH_ROAD:
			if (!_settings_game.depot.allow_no_comp_roadtype_replacements) {
				/* make sure the roadtypes are compatible */
				if ((GetRoadTypeInfo(e_from->u.road.roadtype)->powered_roadtypes & GetRoadTypeInfo(e_to->u.road.roadtype)->powered_roadtypes) == ROADTYPES_NONE) return false;

				/* make sure that we do not replace a tram with a normal road vehicles or vice versa */
				if (HasBit(e_from->info.misc_flags, EF_ROAD_TRAM) != HasBit(e_to->info.misc_flags, EF_ROAD_TRAM)) return false;
			}
			break;

		case VEH_AIRCRAFT:
			/* make sure that we do not replace a plane with a helicopter or vice versa */
			if ((e_from->u.air.subtype & AIR_CTOL) != (e_to->u.air.subtype & AIR_CTOL)) return false;
			break;

		default: break;
	}

	/* the engines needs to be able to carry the same cargo */
	return EnginesHaveCargoInCommon(from, to);
}

/**
 * Check the capacity of all vehicles in a chain and spread cargo if needed.
 * @param v The vehicle to check.
 * @pre You can only do this if the consist is not loading or unloading. It
 *      must not carry reserved cargo, nor cargo to be unloaded or transferred.
 */
void CheckCargoCapacity(Vehicle *v)
{
	assert(v == nullptr || v->First() == v);

	for (Vehicle *src = v; src != nullptr; src = src->Next()) {
		assert(src->cargo.TotalCount() == src->cargo.ActionCount(VehicleCargoList::MTA_KEEP));

		/* Do we need to more cargo away? */
		if (src->cargo.TotalCount() <= src->cargo_cap) continue;

		/* We need to move a particular amount. Try that on the other vehicles. */
		uint to_spread = src->cargo.TotalCount() - src->cargo_cap;
		for (Vehicle *dest = v; dest != nullptr && to_spread != 0; dest = dest->Next()) {
			assert(dest->cargo.TotalCount() == dest->cargo.ActionCount(VehicleCargoList::MTA_KEEP));
			if (dest->cargo.TotalCount() >= dest->cargo_cap || dest->cargo_type != src->cargo_type) continue;

			uint amount = std::min(to_spread, dest->cargo_cap - dest->cargo.TotalCount());
			src->cargo.Shift(amount, &dest->cargo);
			to_spread -= amount;
		}

		/* Any left-overs will be thrown away, but not their feeder share. */
		if (src->cargo_cap < src->cargo.TotalCount()) src->cargo.Truncate(src->cargo.TotalCount() - src->cargo_cap);
	}
}

/**
 * Transfer cargo from a single (articulated )old vehicle to the new vehicle chain
 * @param old_veh Old vehicle that will be sold
 * @param new_head Head of the completely constructed new vehicle chain
 * @param part_of_chain The vehicle is part of a train
 * @pre You can only do this if both consists are not loading or unloading.
 *      They must not carry reserved cargo, nor cargo to be unloaded or
 *      transferred.
 */
static void TransferCargo(Vehicle *old_veh, Vehicle *new_head, bool part_of_chain)
{
	assert(!part_of_chain || new_head->IsPrimaryVehicle());
	/* Loop through source parts */
	for (Vehicle *src = old_veh; src != nullptr; src = src->Next()) {
		assert(src->cargo.TotalCount() == src->cargo.ActionCount(VehicleCargoList::MTA_KEEP));
		if (!part_of_chain && src->type == VEH_TRAIN && src != old_veh && src != Train::From(old_veh)->other_multiheaded_part && !src->IsArticulatedPart()) {
			/* Skip vehicles, which do not belong to old_veh */
			src = src->GetLastEnginePart();
			continue;
		}
		if (src->cargo_type >= NUM_CARGO || src->cargo.TotalCount() == 0) continue;

		/* Find free space in the new chain */
		for (Vehicle *dest = new_head; dest != nullptr && src->cargo.TotalCount() > 0; dest = dest->Next()) {
			assert(dest->cargo.TotalCount() == dest->cargo.ActionCount(VehicleCargoList::MTA_KEEP));
			if (!part_of_chain && dest->type == VEH_TRAIN && dest != new_head && dest != Train::From(new_head)->other_multiheaded_part && !dest->IsArticulatedPart()) {
				/* Skip vehicles, which do not belong to new_head */
				dest = dest->GetLastEnginePart();
				continue;
			}
			if (dest->cargo_type != src->cargo_type) continue;

			uint amount = std::min(src->cargo.TotalCount(), dest->cargo_cap - dest->cargo.TotalCount());
			if (amount <= 0) continue;

			src->cargo.Shift(amount, &dest->cargo);
		}
	}

	/* Update train weight etc., the old vehicle will be sold anyway */
	if (part_of_chain && new_head->type == VEH_TRAIN) Train::From(new_head)->ConsistChanged(CCF_LOADUNLOAD);
}

/**
 * Tests whether refit orders that applied to v will also apply to the new vehicle type
 * @param v The vehicle to be replaced
 * @param engine_type The type we want to replace with
 * @return true iff all refit orders stay valid
 */
static bool VerifyAutoreplaceRefitForOrders(const Vehicle *v, EngineID engine_type)
{
	CargoTypes union_refit_mask_a = GetUnionOfArticulatedRefitMasks(v->engine_type, false);
	CargoTypes union_refit_mask_b = GetUnionOfArticulatedRefitMasks(engine_type, false);

	const Vehicle *u = (v->type == VEH_TRAIN) ? v->First() : v;
	for (const Order *o : u->Orders()) {
		if (!o->IsRefit() || o->IsAutoRefit()) continue;
		CargoID cargo_type = o->GetRefitCargo();

		if (!HasBit(union_refit_mask_a, cargo_type)) continue;
		if (!HasBit(union_refit_mask_b, cargo_type)) return false;
	}

	return true;
}

/**
 * Gets the index of the first refit order that is incompatible with the requested engine type
 * @param v The vehicle to be replaced
 * @param engine_type The type we want to replace with
 * @return index of the incompatible order or -1 if none were found
 */
static int GetIncompatibleRefitOrderIdForAutoreplace(const Vehicle *v, EngineID engine_type)
{
	CargoTypes union_refit_mask = GetUnionOfArticulatedRefitMasks(engine_type, false);

	const Order *o;
	const Vehicle *u = (v->type == VEH_TRAIN) ? v->First() : v;

	const OrderList *orders = u->orders.list;
	if (orders == nullptr) return -1;
	for (VehicleOrderID i = 0; i < orders->GetNumOrders(); i++) {
		o = orders->GetOrderAt(i);
		if (!o->IsRefit()) continue;
		if (!HasBit(union_refit_mask, o->GetRefitCargo())) return i;
	}

	return -1;
}

/**
 * Function to find what type of cargo to refit to when autoreplacing
 * @param *v Original vehicle that is being replaced.
 * @param engine_type The EngineID of the vehicle that is being replaced to
 * @param part_of_chain The vehicle is part of a train
 * @return The cargo type to replace to
 *    CT_NO_REFIT is returned if no refit is needed
 *    CT_INVALID is returned when both old and new vehicle got cargo capacity and refitting the new one to the old one's cargo type isn't possible
 */
static CargoID GetNewCargoTypeForReplace(Vehicle *v, EngineID engine_type, bool part_of_chain)
{
	CargoTypes available_cargo_types, union_mask;
	GetArticulatedRefitMasks(engine_type, true, &union_mask, &available_cargo_types);

	if (union_mask == 0) return CT_NO_REFIT; // Don't try to refit an engine with no cargo capacity

	CargoID cargo_type;
	if (IsArticulatedVehicleCarryingDifferentCargoes(v, &cargo_type)) return CT_INVALID; // We cannot refit to mixed cargoes in an automated way

	if (cargo_type == CT_INVALID) {
		if (v->type != VEH_TRAIN) return CT_NO_REFIT; // If the vehicle does not carry anything at all, every replacement is fine.

		if (!part_of_chain) return CT_NO_REFIT;

		/* the old engine didn't have cargo capacity, but the new one does
		 * now we will figure out what cargo the train is carrying and refit to fit this */

		for (v = v->First(); v != nullptr; v = v->Next()) {
			if (!v->GetEngine()->CanCarryCargo()) continue;
			/* Now we found a cargo type being carried on the train and we will see if it is possible to carry to this one */
			if (HasBit(available_cargo_types, v->cargo_type)) return v->cargo_type;
		}

		return CT_NO_REFIT; // We failed to find a cargo type on the old vehicle and we will not refit the new one
	} else {
		if (!HasBit(available_cargo_types, cargo_type)) return CT_INVALID; // We can't refit the vehicle to carry the cargo we want

		if (part_of_chain && !VerifyAutoreplaceRefitForOrders(v, engine_type)) return CT_INVALID; // Some refit orders lose their effect

		return cargo_type;
	}
}

/**
 * Get the EngineID of the replacement for a vehicle
 * @param v The vehicle to find a replacement for
 * @param c The vehicle's owner (it's faster to forward the pointer than refinding it)
 * @param always_replace Always replace, even if not old.
 * @param[out] e the EngineID of the replacement. INVALID_ENGINE if no replacement is found
 * @return Error if the engine to build is not available
 */
static CommandCost GetNewEngineType(const Vehicle *v, const Company *c, bool always_replace, EngineID &e)
{
	assert(v->type != VEH_TRAIN || !v->IsArticulatedPart());

	e = INVALID_ENGINE;

	if (v->type == VEH_TRAIN && Train::From(v)->IsRearDualheaded()) {
		/* we build the rear ends of multiheaded trains with the front ones */
		return CommandCost();
	}

	bool replace_when_old;
	e = EngineReplacementForCompany(c, v->engine_type, v->group_id, &replace_when_old);
	if (!always_replace && replace_when_old && !v->NeedsAutorenewing(c, false)) e = INVALID_ENGINE;

	/* Autoreplace, if engine is available */
	if (e != INVALID_ENGINE && IsEngineBuildable(e, v->type, _current_company)) {
		return CommandCost();
	}

	/* Autorenew if needed */
	if (v->NeedsAutorenewing(c)) e = v->engine_type;

	/* Nothing to do or all is fine? */
	if (e == INVALID_ENGINE || IsEngineBuildable(e, v->type, _current_company)) return CommandCost();

	/* The engine we need is not available. Report error to user */
	return CommandCost(STR_ERROR_RAIL_VEHICLE_NOT_AVAILABLE + v->type);
}

/**
 * Builds and refits a replacement vehicle
 * Important: The old vehicle is still in the original vehicle chain (used for determining the cargo when the old vehicle did not carry anything, but the new one does)
 * @param old_veh A single (articulated/multiheaded) vehicle that shall be replaced.
 * @param new_vehicle Returns the newly build and refitted vehicle
 * @param part_of_chain The vehicle is part of a train
 * @return cost or error
 */
static CommandCost BuildReplacementVehicle(Vehicle *old_veh, Vehicle **new_vehicle, bool part_of_chain)
{
	*new_vehicle = nullptr;

	/* Shall the vehicle be replaced? */
	const Company *c = Company::Get(_current_company);
	EngineID e;
	CommandCost cost = GetNewEngineType(old_veh, c, true, e);
	if (cost.Failed()) return cost;
	if (e == INVALID_ENGINE) return CommandCost(); // neither autoreplace is set, nor autorenew is triggered

	/* Does it need to be refitted */
	CargoID refit_cargo = GetNewCargoTypeForReplace(old_veh, e, part_of_chain);
	if (refit_cargo == CT_INVALID) {
		if (!IsLocalCompany()) return CommandCost();

		SetDParam(0, old_veh->index);

		int order_id = GetIncompatibleRefitOrderIdForAutoreplace(old_veh, e);
		if (order_id != -1) {
			/* Orders contained a refit order that is incompatible with the new vehicle. */
			SetDParam(1, STR_ERROR_AUTOREPLACE_INCOMPATIBLE_REFIT);
			SetDParam(2, order_id + 1); // 1-based indexing for display
		} else {
			/* Current cargo is incompatible with the new vehicle. */
			SetDParam(1, STR_ERROR_AUTOREPLACE_INCOMPATIBLE_CARGO);
			SetDParam(2, CargoSpec::Get(old_veh->cargo_type)->name);
		}

		AddVehicleAdviceNewsItem(STR_NEWS_VEHICLE_AUTORENEW_FAILED, old_veh->index);
		return CommandCost();
	}

	/* Build the new vehicle */
	cost = DoCommand(old_veh->tile, e | (CT_INVALID << 24), 0, DC_EXEC | DC_AUTOREPLACE, GetCmdBuildVeh(old_veh));
	if (cost.Failed()) return cost;

	Vehicle *new_veh = Vehicle::Get(_new_vehicle_id);
	*new_vehicle = new_veh;

	/* Refit the vehicle if needed */
	if (refit_cargo != CT_NO_REFIT) {
		byte subtype = GetBestFittingSubType(old_veh, new_veh, refit_cargo);

		cost.AddCost(DoCommand(0, new_veh->index, refit_cargo | (subtype << 8), DC_EXEC | DC_AUTOREPLACE, GetCmdRefitVeh(new_veh)));
		assert(cost.Succeeded()); // This should be ensured by GetNewCargoTypeForReplace()
	}

	/* Try to reverse the vehicle, but do not care if it fails as the new type might not be reversible */
	if (new_veh->type == VEH_TRAIN && HasBit(Train::From(old_veh)->flags, VRF_REVERSE_DIRECTION)) {
		DoCommand(0, new_veh->index, true, DC_EXEC | DC_AUTOREPLACE, CMD_REVERSE_TRAIN_DIRECTION);
	}

	return cost;
}

/**
 * Issue a start/stop command
 * @param v a vehicle
 * @param evaluate_callback shall the start/stop callback be evaluated?
 * @return success or error
 */
static inline CommandCost CmdStartStopVehicle(const Vehicle *v, bool evaluate_callback)
{
	return DoCommand(0, v->index, evaluate_callback ? 1 : 0, DC_EXEC | DC_AUTOREPLACE, CMD_START_STOP_VEHICLE);
}

/**
 * Issue a train vehicle move command
 * @param v The vehicle to move
 * @param after The vehicle to insert 'v' after, or nullptr to start new chain
 * @param flags the command flags to use
 * @param whole_chain move all vehicles following 'v' (true), or only 'v' (false)
 * @return success or error
 */
static inline CommandCost CmdMoveVehicle(const Vehicle *v, const Vehicle *after, DoCommandFlag flags, bool whole_chain)
{
	return DoCommand(0, v->index | (whole_chain ? 1 : 0) << 20, after != nullptr ? after->index : INVALID_VEHICLE, flags | DC_NO_CARGO_CAP_CHECK, CMD_MOVE_RAIL_VEHICLE);
}

/**
 * Copy head specific things to the new vehicle chain after it was successfully constructed
 * @param old_head The old front vehicle (no wagons attached anymore)
 * @param new_head The new head of the completely replaced vehicle chain
 * @param flags the command flags to use
 */
static CommandCost CopyHeadSpecificThings(Vehicle *old_head, Vehicle *new_head, DoCommandFlag flags)
{
	CommandCost cost = CommandCost();

	/* Share orders */
	if (cost.Succeeded() && old_head != new_head) cost.AddCost(DoCommand(0, new_head->index | CO_SHARE << 30, old_head->index, DC_EXEC, CMD_CLONE_ORDER));

	/* Copy group membership */
	if (cost.Succeeded() && old_head != new_head) cost.AddCost(DoCommand(0, old_head->group_id, new_head->index, DC_EXEC, CMD_ADD_VEHICLE_GROUP));

	/* Perform start/stop check whether the new vehicle suits newgrf restrictions etc. */
	if (cost.Succeeded()) {
		/* Start the vehicle, might be denied by certain things */
		assert((new_head->vehstatus & VS_STOPPED) != 0);
		cost.AddCost(CmdStartStopVehicle(new_head, true));

		/* Stop the vehicle again, but do not care about evil newgrfs allowing starting but not stopping :p */
		if (cost.Succeeded()) cost.AddCost(CmdStartStopVehicle(new_head, false));
	}

	/* Last do those things which do never fail (resp. we do not care about), but which are not undo-able */
	if (cost.Succeeded() && old_head != new_head && (flags & DC_EXEC) != 0) {
		/* Copy other things which cannot be copied by a command and which shall not stay resetted from the build vehicle command */
		new_head->CopyVehicleConfigAndStatistics(old_head);

		/* Switch vehicle windows/news to the new vehicle, so they are not closed/deleted when the old vehicle is sold */
		ChangeVehicleViewports(old_head->index, new_head->index);
		ChangeVehicleViewWindow(old_head->index, new_head->index);
		ChangeVehicleNews(old_head->index, new_head->index);
	}

	return cost;
}

/**
 * Replace a single unit in a free wagon chain
 * @param single_unit vehicle to let autoreplace/renew operator on
 * @param flags command flags
 * @param nothing_to_do is set to 'false' when something was done (only valid when not failed)
 * @return cost or error
 */
static CommandCost ReplaceFreeUnit(Vehicle **single_unit, DoCommandFlag flags, bool *nothing_to_do)
{
	Train *old_v = Train::From(*single_unit);
	assert(!old_v->IsArticulatedPart() && !old_v->IsRearDualheaded());

	CommandCost cost = CommandCost(EXPENSES_NEW_VEHICLES, 0);

	/* Build and refit replacement vehicle */
	Vehicle *new_v = nullptr;
	cost.AddCost(BuildReplacementVehicle(old_v, &new_v, false));

	/* Was a new vehicle constructed? */
	if (cost.Succeeded() && new_v != nullptr) {
		*nothing_to_do = false;

		if ((flags & DC_EXEC) != 0) {
			/* Move the new vehicle behind the old */
			CmdMoveVehicle(new_v, old_v, DC_EXEC | DC_AUTOREPLACE, false);

			/* Take over cargo
			 * Note: We do only transfer cargo from the old to the new vehicle.
			 *       I.e. we do not transfer remaining cargo to other vehicles.
			 *       Else you would also need to consider moving cargo to other free chains,
			 *       or doing the same in ReplaceChain(), which would be quite troublesome.
			 */
			TransferCargo(old_v, new_v, false);

			*single_unit = new_v;

			AI::NewEvent(old_v->owner, new ScriptEventVehicleAutoReplaced(old_v->index, new_v->index));
		}

		/* Sell the old vehicle */
		cost.AddCost(DoCommand(0, old_v->index, 0, flags, GetCmdSellVeh(old_v)));

		/* If we are not in DC_EXEC undo everything */
		if ((flags & DC_EXEC) == 0) {
			DoCommand(0, new_v->index, 0, DC_EXEC | DC_AUTOREPLACE, GetCmdSellVeh(new_v));
		}
	}

	return cost;
}

/**
 * When replacing a ship in an extended depot, copy the direction as well.
 * @param old_ship The ship being replaced.
 * @param new_ship The new ship that will replace the old one.
 */
void CopyShipStatusInExtendedDepot(const Ship *old_ship, Ship *new_ship)
{
	assert(IsBigDepotTile(old_ship->tile));
	assert(old_ship->tile == new_ship->tile);

	new_ship->x_pos = old_ship->x_pos;
	new_ship->y_pos = old_ship->y_pos;
	new_ship->z_pos = old_ship->z_pos;
	new_ship->state = old_ship->state;
	new_ship->direction = old_ship->direction;
	new_ship->rotation = old_ship->rotation;
	new_ship->GetImage(new_ship->direction, EIT_ON_MAP, &new_ship->sprite_cache.sprite_seq);
}

/**
 * Replace a whole vehicle chain
 * @param chain vehicle chain to let autoreplace/renew operator on
 * @param flags command flags
 * @param wagon_removal remove wagons when the resulting chain occupies more tiles than the old did
 * @param nothing_to_do is set to 'false' when something was done (only valid when not failed)
 * @return cost or error
 */
static CommandCost ReplaceChain(Vehicle **chain, DoCommandFlag flags, bool wagon_removal, bool *nothing_to_do)
{
	assert(flags & DC_AUTOREPLACE);

	Vehicle *old_head = *chain;
	assert(old_head->IsPrimaryVehicle());
	TileIndex tile = old_head->tile;

	CommandCost cost = CommandCost(EXPENSES_NEW_VEHICLES, 0);

	if (old_head->type == VEH_TRAIN) {
		/* Store the length of the old vehicle chain, rounded up to whole tiles */
		uint16 old_total_length = CeilDiv(Train::From(old_head)->gcache.cached_total_length, TILE_SIZE) * TILE_SIZE;

		int num_units = 0; ///< Number of units in the chain
		for (Train *w = Train::From(old_head); w != nullptr; w = w->GetNextUnit()) num_units++;

		Train **old_vehs = CallocT<Train *>(num_units); ///< Will store vehicles of the old chain in their order
		Train **new_vehs = CallocT<Train *>(num_units); ///< New vehicles corresponding to old_vehs or nullptr if no replacement
		Money *new_costs = MallocT<Money>(num_units);   ///< Costs for buying and refitting the new vehicles

		/* Collect vehicles and build replacements
		 * Note: The replacement vehicles can only successfully build as long as the old vehicles are still in their chain */
		int i;
		Train *w;
		for (w = Train::From(old_head), i = 0; w != nullptr; w = w->GetNextUnit(), i++) {
			assert(i < num_units);
			old_vehs[i] = w;

			CommandCost ret = BuildReplacementVehicle(old_vehs[i], (Vehicle**)&new_vehs[i], true);
			cost.AddCost(ret);
			if (cost.Failed()) break;

			new_costs[i] = ret.GetCost();
			if (new_vehs[i] != nullptr) *nothing_to_do = false;
		}
		Train *new_head = (new_vehs[0] != nullptr ? new_vehs[0] : old_vehs[0]);

		/* Note: When autoreplace has already failed here, old_vehs[] is not completely initialized. But it is also not needed. */
		if (cost.Succeeded()) {
			/* Separate the head, so we can start constructing the new chain */
			Train *second = Train::From(old_head)->GetNextUnit();
			if (second != nullptr) cost.AddCost(CmdMoveVehicle(second, nullptr, DC_EXEC | DC_AUTOREPLACE, true));

			assert(Train::From(new_head)->GetNextUnit() == nullptr);

			/* Append engines to the new chain
			 * We do this from back to front, so that the head of the temporary vehicle chain does not change all the time.
			 * That way we also have less trouble when exceeding the unitnumber limit.
			 * OTOH the vehicle attach callback is more expensive this way :s */
			Train *last_engine = nullptr; ///< Shall store the last engine unit after this step
			if (cost.Succeeded()) {
				for (int i = num_units - 1; i > 0; i--) {
					Train *append = (new_vehs[i] != nullptr ? new_vehs[i] : old_vehs[i]);

					if (RailVehInfo(append->engine_type)->railveh_type == RAILVEH_WAGON) continue;

					if (new_vehs[i] != nullptr) {
						/* Move the old engine to a separate row with DC_AUTOREPLACE. Else
						 * moving the wagon in front may fail later due to unitnumber limit.
						 * (We have to attach wagons without DC_AUTOREPLACE.) */
						CmdMoveVehicle(old_vehs[i], nullptr, DC_EXEC | DC_AUTOREPLACE, false);
					}

					if (last_engine == nullptr) last_engine = append;
					cost.AddCost(CmdMoveVehicle(append, new_head, DC_EXEC | DC_AUTOREPLACE, false));
					if (cost.Failed()) break;
				}
				if (last_engine == nullptr) last_engine = new_head;
			}

			/* When wagon removal is enabled and the new engines without any wagons are already longer than the old, we have to fail */
			if (cost.Succeeded() && wagon_removal && new_head->gcache.cached_total_length > old_total_length) cost = CommandCost(STR_ERROR_TRAIN_TOO_LONG_AFTER_REPLACEMENT);

			/* Append/insert wagons into the new vehicle chain
			 * We do this from back to front, so we can stop when wagon removal or maximum train length (i.e. from mammoth-train setting) is triggered.
			 */
			if (cost.Succeeded()) {
				for (int i = num_units - 1; i > 0; i--) {
					assert(last_engine != nullptr);
					Vehicle *append = (new_vehs[i] != nullptr ? new_vehs[i] : old_vehs[i]);

					if (RailVehInfo(append->engine_type)->railveh_type == RAILVEH_WAGON) {
						/* Insert wagon after 'last_engine' */
						CommandCost res = CmdMoveVehicle(append, last_engine, DC_EXEC | DC_AUTOREPLACE, false);

						/* When we allow removal of wagons, either the move failing due
						 * to the train becoming too long, or the train becoming longer
						 * would move the vehicle to the empty vehicle chain. */
						if (wagon_removal && (res.Failed() ? res.GetErrorMessage() == STR_ERROR_TRAIN_TOO_LONG : new_head->gcache.cached_total_length > old_total_length)) {
							CmdMoveVehicle(append, nullptr, DC_EXEC | DC_AUTOREPLACE, false);
							break;
						}

						cost.AddCost(res);
						if (cost.Failed()) break;
					} else {
						/* We have reached 'last_engine', continue with the next engine towards the front */
						assert(append == last_engine);
						last_engine = last_engine->GetPrevUnit();
					}
				}
			}

			/* Sell superfluous new vehicles that could not be inserted. */
			if (cost.Succeeded() && wagon_removal) {
				assert(new_head->gcache.cached_total_length <= _settings_game.vehicle.max_train_length * TILE_SIZE);
				for (int i = 1; i < num_units; i++) {
					Vehicle *wagon = new_vehs[i];
					if (wagon == nullptr) continue;
					if (wagon->First() == new_head) break;

					assert(RailVehInfo(wagon->engine_type)->railveh_type == RAILVEH_WAGON);

					/* Sell wagon */
					[[maybe_unused]] CommandCost ret = DoCommand(0, wagon->index, 0, DC_EXEC | DC_AUTOREPLACE, GetCmdSellVeh(wagon));
					assert(ret.Succeeded());
					new_vehs[i] = nullptr;

					/* Revert the money subtraction when the vehicle was built.
					 * This value is different from the sell value, esp. because of refitting */
					cost.AddCost(-new_costs[i]);
				}
			}

			/* The new vehicle chain is constructed, now take over orders and everything... */
			if (cost.Succeeded()) cost.AddCost(CopyHeadSpecificThings(old_head, new_head, flags));

			if (cost.Succeeded()) {
				/* Success ! */
				if ((flags & DC_EXEC) != 0 && new_head != old_head) {
					*chain = new_head;
					AI::NewEvent(old_head->owner, new ScriptEventVehicleAutoReplaced(old_head->index, new_head->index));
				}

				/* Transfer cargo of old vehicles and sell them */
				for (int i = 0; i < num_units; i++) {
					Vehicle *w = old_vehs[i];
					/* Is the vehicle again part of the new chain?
					 * Note: We cannot test 'new_vehs[i] != nullptr' as wagon removal might cause to remove both */
					if (w->First() == new_head) continue;

					if ((flags & DC_EXEC) != 0) TransferCargo(w, new_head, true);

					/* Sell the vehicle.
					 * Note: This might temporarily construct new trains, so use DC_AUTOREPLACE to prevent
					 *       it from failing due to engine limits. */
					cost.AddCost(DoCommand(0, w->index, 0, flags | DC_AUTOREPLACE, GetCmdSellVeh(w)));
					if ((flags & DC_EXEC) != 0) {
						old_vehs[i] = nullptr;
						if (i == 0) old_head = nullptr;
					}
				}

				if ((flags & DC_EXEC) != 0) CheckCargoCapacity(new_head);
			}

			assert(IsValidTile(tile));
			if (!HasCompatibleDepotTile(tile, new_head)) cost.MakeError(STR_ERROR_UNABLE_TO_FIND_APPROPRIATE_DEPOT_TILE);

			/* If we are not in DC_EXEC undo everything, i.e. rearrange old vehicles.
			 * We do this from back to front, so that the head of the temporary vehicle chain does not change all the time.
			 * Note: The vehicle attach callback is disabled here :) */
			if ((flags & DC_EXEC) == 0) {
				/* Separate the head, so we can reattach the old vehicles */
				Train *second = Train::From(old_head)->GetNextUnit();
				if (second != nullptr) CmdMoveVehicle(second, nullptr, DC_EXEC | DC_AUTOREPLACE, true);

				assert(Train::From(old_head)->GetNextUnit() == nullptr);

				for (int i = num_units - 1; i > 0; i--) {
					[[maybe_unused]] CommandCost ret = CmdMoveVehicle(old_vehs[i], old_head, DC_EXEC | DC_AUTOREPLACE, false);
					assert(ret.Succeeded());
				}
			}
		}

		/* Finally undo buying of new vehicles */
		if ((flags & DC_EXEC) == 0) {
			for (int i = num_units - 1; i >= 0; i--) {
				if (new_vehs[i] != nullptr) {
					DoCommand(0, new_vehs[i]->index, 0, DC_EXEC | DC_AUTOREPLACE, GetCmdSellVeh(new_vehs[i]));
					new_vehs[i] = nullptr;
				}
			}
		}

		free(old_vehs);
		free(new_vehs);
		free(new_costs);
	} else {
		/* Build and refit replacement vehicle */
		Vehicle *new_head = nullptr;
		cost.AddCost(BuildReplacementVehicle(old_head, &new_head, true));

		/* Was a new vehicle constructed? */
		if (cost.Succeeded() && new_head != nullptr) {
			*nothing_to_do = false;

			/* The new vehicle is constructed, now take over orders and everything... */
			cost.AddCost(CopyHeadSpecificThings(old_head, new_head, flags));

			if (cost.Succeeded()) {
				/* Copy position and direction for ships in extended depots. */
				if (old_head->type == VEH_SHIP && IsBigDepotTile(old_head->tile)) {
					CopyShipStatusInExtendedDepot(Ship::From(old_head), Ship::From(new_head));
				}

				/* The new vehicle is constructed, now take over cargo */
				if ((flags & DC_EXEC) != 0) {
					TransferCargo(old_head, new_head, true);
					*chain = new_head;

					AI::NewEvent(old_head->owner, new ScriptEventVehicleAutoReplaced(old_head->index, new_head->index));
				}

				/* Sell the old vehicle */
				cost.AddCost(DoCommand(0, old_head->index, 0, flags, GetCmdSellVeh(old_head)));
			}

			/* If we are not in DC_EXEC undo everything */
			if ((flags & DC_EXEC) == 0) {
				DoCommand(0, new_head->index, 0, DC_EXEC | DC_AUTOREPLACE, GetCmdSellVeh(new_head));
			}
		}
	}

	assert(!(cost.Failed() && (DC_EXEC & flags) != 0));
	return cost;
}

/**
 * Autoreplaces a vehicle
 * Trains are replaced as a whole chain, free wagons in depot are replaced on their own
 * @param tile not used
 * @param flags type of operation
 * @param p1 Index of vehicle
 * @param p2 not used
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdAutoreplaceVehicle(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const std::string &text)
{
	Vehicle *v = Vehicle::GetIfValid(p1);
	if (v == nullptr) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (!v->IsChainInDepot()) return CMD_ERROR;
	if (v->vehstatus & VS_CRASHED) return CMD_ERROR;

	bool free_wagon = false;
	if (v->type == VEH_TRAIN) {
		Train *t = Train::From(v);
		if (t->IsArticulatedPart() || t->IsRearDualheaded()) return CMD_ERROR;
		free_wagon = !t->IsFrontEngine();
		if (free_wagon && t->First()->IsFrontEngine()) return CMD_ERROR;
	} else {
		if (!v->IsPrimaryVehicle()) return CMD_ERROR;
	}

	const Company *c = Company::Get(_current_company);
	bool wagon_removal = c->settings.renew_keep_length;

	const Group *g = Group::GetIfValid(v->group_id);
	if (g != nullptr) wagon_removal = HasBit(g->flags, GroupFlags::GF_REPLACE_WAGON_REMOVAL);

	/* Test whether any replacement is set, before issuing a whole lot of commands that would end in nothing changed */
	Vehicle *w = v;
	bool any_replacements = false;
	while (w != nullptr) {
		EngineID e;
		CommandCost cost = GetNewEngineType(w, c, false, e);
		if (cost.Failed()) return cost;
		any_replacements |= (e != INVALID_ENGINE);
		w = (!free_wagon && w->type == VEH_TRAIN ? Train::From(w)->GetNextUnit() : nullptr);
	}
	if (!any_replacements) return_cmd_error(STR_ERROR_AUTOREPLACE_NOTHING_TO_DO);

	CommandCost cost = CommandCost(EXPENSES_NEW_VEHICLES, 0);
	bool nothing_to_do = true;
	bool was_stopped = free_wagon || ((v->vehstatus & VS_STOPPED) != 0);

	/* Stop the vehicle */
	if (!was_stopped) cost.AddCost(CmdStartStopVehicle(v, true));
	if (cost.Failed()) return cost;

	assert(free_wagon || v->IsStoppedInDepot());

	if (IsBigDepotTile(v->tile)) SetBigDepotReservation(v, false);

	/* Start autoreplacing the vehicle. */
	flags |= DC_AUTOREPLACE;

	/* We have to construct the new vehicle chain to test whether it is valid.
	 * Vehicle construction needs random bits, so we have to save the random seeds
	 * to prevent desyncs and to replay newgrf callbacks during DC_EXEC */
	SavedRandomSeeds saved_seeds;
	SaveRandomSeeds(&saved_seeds);
	if (free_wagon) {
		cost.AddCost(ReplaceFreeUnit(&v, flags & ~DC_EXEC, &nothing_to_do));
	} else {
		cost.AddCost(ReplaceChain(&v, flags & ~DC_EXEC, wagon_removal, &nothing_to_do));
	}
	RestoreRandomSeeds(saved_seeds);

	if (cost.Succeeded() && (flags & DC_EXEC) != 0) {
		CommandCost ret;
		if (free_wagon) {
			ret = ReplaceFreeUnit(&v, flags, &nothing_to_do);
		} else {
			ret = ReplaceChain(&v, flags, wagon_removal, &nothing_to_do);
		}
		assert(ret.Succeeded());
		assert(ret.GetCost() == cost.GetCost());
	}

	if (IsBigDepotTile(v->tile)) SetBigDepotReservation(v, true);

	/* Restart the vehicle */
	if (!was_stopped) cost.AddCost(CmdStartStopVehicle(v, false));

	assert(cost.Failed() || !nothing_to_do);
	return cost;
}

/**
 * Change engine renewal parameters
 * @param tile unused
 * @param flags operation to perform
 * @param p1 packed data
 *   - bit      0 = replace when engine gets old?
 *   - bits 16-31 = engine group
 * @param p2 packed data
 *   - bits  0-15 = old engine type
 *   - bits 16-31 = new engine type
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdSetAutoReplace(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const std::string &text)
{
	Company *c = Company::GetIfValid(_current_company);
	if (c == nullptr) return CMD_ERROR;

	EngineID old_engine_type = GB(p2, 0, 16);
	EngineID new_engine_type = GB(p2, 16, 16);
	GroupID id_g = GB(p1, 16, 16);
	CommandCost cost;

	if (Group::IsValidID(id_g) ? Group::Get(id_g)->owner != _current_company : !IsAllGroupID(id_g) && !IsDefaultGroupID(id_g)) return CMD_ERROR;
	if (!Engine::IsValidID(old_engine_type)) return CMD_ERROR;

	if (new_engine_type != INVALID_ENGINE) {
		if (!Engine::IsValidID(new_engine_type)) return CMD_ERROR;
		if (!CheckAutoreplaceValidity(old_engine_type, new_engine_type, _current_company)) return CMD_ERROR;

		cost = AddEngineReplacementForCompany(c, old_engine_type, new_engine_type, id_g, HasBit(p1, 0), flags);
	} else {
		cost = RemoveEngineReplacementForCompany(c, old_engine_type, id_g, flags);
	}

	if (flags & DC_EXEC) {
		GroupStatistics::UpdateAutoreplace(_current_company);
		if (IsLocalCompany()) SetWindowDirty(WC_REPLACE_VEHICLE, Engine::Get(old_engine_type)->type);

		const VehicleType vt = Engine::Get(old_engine_type)->type;
		SetWindowDirty(GetWindowClassForVehicleType(vt), VehicleListIdentifier(VL_GROUP_LIST, vt, _current_company).Pack());
	}
	if ((flags & DC_EXEC) && IsLocalCompany()) InvalidateAutoreplaceWindow(old_engine_type, id_g);

	return cost;
}

