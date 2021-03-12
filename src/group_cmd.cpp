/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file group_cmd.cpp Handling of the engine groups */

#include "stdafx.h"
#include "cmd_helper.h"
#include "command_func.h"
#include "town.h"
#include "train.h"
#include "station_base.h"
#include "vehiclelist.h"
#include "vehicle_func.h"
#include "autoreplace_base.h"
#include "autoreplace_func.h"
#include "strings_func.h"
#include "string_func.h"
#include "company_func.h"
#include "core/pool_func.hpp"
#include "order_backup.h"
#include "strings_func.h"

#include "table/strings.h"

#include <algorithm>
#include <vector>

#include "safeguards.h"

GroupID _new_group_id;

GroupPool _group_pool("Group");
INSTANTIATE_POOL_METHODS(Group)

GroupStatistics::GroupStatistics()
{
	this->num_engines = CallocT<uint16>(Engine::GetPoolSize());
}

GroupStatistics::~GroupStatistics()
{
	free(this->num_engines);
}

/**
 * Clear all caches.
 */
void GroupStatistics::Clear()
{
	this->num_vehicle = 0;
	this->num_profit_vehicle = 0;
	this->profit_last_year = 0;

	/* This is also called when NewGRF change. So the number of engines might have changed. Reallocate. */
	free(this->num_engines);
	this->num_engines = CallocT<uint16>(Engine::GetPoolSize());
}

/**
 * Returns the GroupStatistics for a specific group.
 * @param company Owner of the group.
 * @param id_g    GroupID of the group.
 * @param type    VehicleType of the vehicles in the group.
 * @return Statistics for the group.
 */
/* static */ GroupStatistics &GroupStatistics::Get(CompanyID company, GroupID id_g, VehicleType type)
{
	if (Group::IsValidID(id_g)) {
		Group *g = Group::Get(id_g);
		assert(g->owner == company);
		assert(g->vehicle_type == type);
		return g->statistics;
	}

	if (IsDefaultGroupID(id_g)) return Company::Get(company)->group_default[type];
	if (IsAllGroupID(id_g)) return Company::Get(company)->group_all[type];

	NOT_REACHED();
}

/**
 * Returns the GroupStatistic for the group of a vehicle.
 * @param v Vehicle.
 * @return GroupStatistics for the group of the vehicle.
 */
/* static */ GroupStatistics &GroupStatistics::Get(const Vehicle *v)
{
	return GroupStatistics::Get(v->owner, v->group_id, v->type);
}

/**
 * Returns the GroupStatistic for the ALL_GROUPO of a vehicle type.
 * @param v Vehicle.
 * @return GroupStatistics for the ALL_GROUP of the vehicle type.
 */
/* static */ GroupStatistics &GroupStatistics::GetAllGroup(const Vehicle *v)
{
	return GroupStatistics::Get(v->owner, ALL_GROUP, v->type);
}

/**
 * Update all caches after loading a game, changing NewGRF, etc.
 */
/* static */ void GroupStatistics::UpdateAfterLoad()
{
	/* Set up the engine count for all companies */
	for (Company *c : Company::Iterate()) {
		for (VehicleType type = VEH_BEGIN; type < VEH_COMPANY_END; type++) {
			c->group_all[type].Clear();
			c->group_default[type].Clear();
		}
	}

	/* Recalculate */
	for (Group *g : Group::Iterate()) {
		g->statistics.Clear();
	}

	for (const Vehicle *v : Vehicle::Iterate()) {
		if (!v->IsEngineCountable()) continue;

		GroupStatistics::CountEngine(v, 1);
		if (v->IsPrimaryVehicle()) GroupStatistics::CountVehicle(v, 1);
	}

	for (const Company *c : Company::Iterate()) {
		GroupStatistics::UpdateAutoreplace(c->index);
	}
}

/**
 * Update num_vehicle when adding or removing a vehicle.
 * @param v Vehicle to count.
 * @param delta +1 to add, -1 to remove.
 */
/* static */ void GroupStatistics::CountVehicle(const Vehicle *v, int delta)
{
	assert(delta == 1 || delta == -1);

	GroupStatistics &stats_all = GroupStatistics::GetAllGroup(v);
	GroupStatistics &stats = GroupStatistics::Get(v);

	stats_all.num_vehicle += delta;
	stats.num_vehicle += delta;

	if (v->age > VEHICLE_PROFIT_MIN_AGE) {
		stats_all.num_profit_vehicle += delta;
		stats_all.profit_last_year += v->GetDisplayProfitLastYear() * delta;
		stats.num_profit_vehicle += delta;
		stats.profit_last_year += v->GetDisplayProfitLastYear() * delta;
	}
}

/**
 * Update num_engines when adding/removing an engine.
 * @param v Engine to count.
 * @param delta +1 to add, -1 to remove.
 */
/* static */ void GroupStatistics::CountEngine(const Vehicle *v, int delta)
{
	assert(delta == 1 || delta == -1);
	GroupStatistics::GetAllGroup(v).num_engines[v->engine_type] += delta;
	GroupStatistics::Get(v).num_engines[v->engine_type] += delta;
}

/**
 * Add a vehicle to the profit sum of its group.
 */
/* static */ void GroupStatistics::VehicleReachedProfitAge(const Vehicle *v)
{
	GroupStatistics &stats_all = GroupStatistics::GetAllGroup(v);
	GroupStatistics &stats = GroupStatistics::Get(v);

	stats_all.num_profit_vehicle++;
	stats_all.profit_last_year += v->GetDisplayProfitLastYear();
	stats.num_profit_vehicle++;
	stats.profit_last_year += v->GetDisplayProfitLastYear();
}

/**
 * Recompute the profits for all groups.
 */
/* static */ void GroupStatistics::UpdateProfits()
{
	/* Set up the engine count for all companies */
	for (Company *c : Company::Iterate()) {
		for (VehicleType type = VEH_BEGIN; type < VEH_COMPANY_END; type++) {
			c->group_all[type].ClearProfits();
			c->group_default[type].ClearProfits();
		}
	}

	/* Recalculate */
	for (Group *g : Group::Iterate()) {
		g->statistics.ClearProfits();
	}

	for (const Vehicle *v : Vehicle::Iterate()) {
		if (v->IsPrimaryVehicle() && v->age > VEHICLE_PROFIT_MIN_AGE) GroupStatistics::VehicleReachedProfitAge(v);
	}
}

/**
 * Update autoreplace_defined and autoreplace_finished of all statistics of a company.
 * @param company Company to update statistics for.
 */
/* static */ void GroupStatistics::UpdateAutoreplace(CompanyID company)
{
	/* Set up the engine count for all companies */
	Company *c = Company::Get(company);
	for (VehicleType type = VEH_BEGIN; type < VEH_COMPANY_END; type++) {
		c->group_all[type].ClearAutoreplace();
		c->group_default[type].ClearAutoreplace();
	}

	/* Recalculate */
	for (Group *g : Group::Iterate()) {
		if (g->owner != company) continue;
		g->statistics.ClearAutoreplace();
	}

	for (EngineRenewList erl = c->engine_renew_list; erl != nullptr; erl = erl->next) {
		const Engine *e = Engine::Get(erl->from);
		GroupStatistics &stats = GroupStatistics::Get(company, erl->group_id, e->type);
		if (!stats.autoreplace_defined) {
			stats.autoreplace_defined = true;
			stats.autoreplace_finished = true;
		}
		if (GetGroupNumEngines(company, erl->group_id, erl->from) > 0) stats.autoreplace_finished = false;
	}
}

/**
 * Update the num engines of a groupID. Decrease the old one and increase the new one
 * @note called in SetTrainGroupID and UpdateTrainGroupID
 * @param v     Vehicle we have to update
 * @param old_g index of the old group
 * @param new_g index of the new group
 */
static inline void UpdateNumEngineGroup(const Vehicle *v, GroupID old_g, GroupID new_g)
{
	if (old_g != new_g) {
		/* Decrease the num engines in the old group */
		GroupStatistics::Get(v->owner, old_g, v->type).num_engines[v->engine_type]--;

		/* Increase the num engines in the new group */
		GroupStatistics::Get(v->owner, new_g, v->type).num_engines[v->engine_type]++;
	}
}


const Livery *GetParentLivery(const Group *g)
{
	if (g->parent == INVALID_GROUP) {
		const Company *c = Company::Get(g->owner);
		return &c->livery[LS_DEFAULT];
	}

	const Group *pg = Group::Get(g->parent);
	return &pg->livery;
}


/**
 * Propagate a livery change to a group's children.
 * @param g Group.
 */
void PropagateChildLivery(const Group *g)
{
	/* Company colour data is indirectly cached. */
	for (Vehicle *v : Vehicle::Iterate()) {
		if (v->group_id == g->index && (!v->IsGroundVehicle() || v->IsFrontEngine())) {
			for (Vehicle *u = v; u != nullptr; u = u->Next()) {
				u->colourmap = PAL_NONE;
				u->InvalidateNewGRFCache();
			}
		}
	}

	for (Group *cg : Group::Iterate()) {
		if (cg->parent == g->index) {
			if (!HasBit(cg->livery.in_use, 0)) cg->livery.colour1 = g->livery.colour1;
			if (!HasBit(cg->livery.in_use, 1)) cg->livery.colour2 = g->livery.colour2;
			PropagateChildLivery(cg);
		}
	}
}


Group::Group(Owner owner)
{
	this->owner = owner;
	this->folded = false;
}


/**
 * Create a new vehicle group.
 * @param tile unused
 * @param flags type of operation
 * @param p1   vehicle type
 * @param p2   parent groupid
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdCreateGroup(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleType vt = Extract<VehicleType, 0, 3>(p1);
	if (!IsCompanyBuildableVehicleType(vt)) return CMD_ERROR;

	if (!Group::CanAllocateItem()) return CMD_ERROR;

	const Group *pg = Group::GetIfValid(GB(p2, 0, 16));
	if (pg != nullptr) {
		if (pg->owner != _current_company) return CMD_ERROR;
		if (pg->vehicle_type != vt) return CMD_ERROR;
	}

	if (flags & DC_EXEC) {
		Group *g = new Group(_current_company);
		g->replace_protection = false;
		g->vehicle_type = vt;
		g->parent = INVALID_GROUP;

		if (pg == nullptr) {
			const Company *c = Company::Get(_current_company);
			g->livery.colour1 = c->livery[LS_DEFAULT].colour1;
			g->livery.colour2 = c->livery[LS_DEFAULT].colour2;
		} else {
			g->parent = pg->index;
			g->livery.colour1 = pg->livery.colour1;
			g->livery.colour2 = pg->livery.colour2;
		}

		_new_group_id = g->index;

		InvalidateWindowData(GetWindowClassForVehicleType(vt), VehicleListIdentifier(VL_GROUP_LIST, vt, _current_company).Pack());
		InvalidateWindowData(WC_COMPANY_COLOUR, g->owner, g->vehicle_type);
	}

	return CommandCost();
}


/**
 * Add all vehicles in the given group to the default group and then deletes the group.
 * @param tile unused
 * @param flags type of operation
 * @param p1   index of array group
 *      - p1 bit 0-15 : GroupID
 * @param p2   unused
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdDeleteGroup(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	Group *g = Group::GetIfValid(p1);
	if (g == nullptr || g->owner != _current_company) return CMD_ERROR;

	/* Remove all vehicles from the group */
	DoCommand(0, p1, 0, flags, CMD_REMOVE_ALL_VEHICLES_GROUP);

	/* Delete sub-groups */
	for (const Group *gp : Group::Iterate()) {
		if (gp->parent == g->index) {
			DoCommand(0, gp->index, 0, flags, CMD_DELETE_GROUP);
		}
	}

	if (flags & DC_EXEC) {
		/* Update backupped orders if needed */
		OrderBackup::ClearGroup(g->index);

		/* If we set an autoreplace for the group we delete, remove it. */
		if (_current_company < MAX_COMPANIES) {
			Company *c;

			c = Company::Get(_current_company);
			for (EngineRenew *er : EngineRenew::Iterate()) {
				if (er->group_id == g->index) RemoveEngineReplacementForCompany(c, er->from, g->index, flags);
			}
		}

		VehicleType vt = g->vehicle_type;

		/* Delete the Replace Vehicle Windows */
		DeleteWindowById(WC_REPLACE_VEHICLE, g->vehicle_type);
		delete g;

		InvalidateWindowData(GetWindowClassForVehicleType(vt), VehicleListIdentifier(VL_GROUP_LIST, vt, _current_company).Pack());
		InvalidateWindowData(WC_COMPANY_COLOUR, _current_company, vt);
	}

	return CommandCost();
}

/**
 * Alter a group
 * @param tile unused
 * @param flags type of operation
 * @param p1   index of array group
 *   - p1 bit 0-15 : GroupID
 *   - p1 bit 16: 0 - Rename grouop
 *                1 - Set group parent
 * @param p2   parent group index
 * @param text the new name or an empty string when resetting to the default
 * @return the cost of this operation or an error
 */
CommandCost CmdAlterGroup(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	Group *g = Group::GetIfValid(GB(p1, 0, 16));
	if (g == nullptr || g->owner != _current_company) return CMD_ERROR;

	if (!HasBit(p1, 16)) {
		/* Rename group */
		bool reset = StrEmpty(text);

		if (!reset) {
			if (Utf8StringLength(text) >= MAX_LENGTH_GROUP_NAME_CHARS) return CMD_ERROR;
		}

		if (flags & DC_EXEC) {
			/* Assign the new one */
			if (reset) {
				g->name.clear();
			} else {
				g->name = text;
			}
		}
	} else {
		/* Set group parent */
		const Group *pg = Group::GetIfValid(GB(p2, 0, 16));

		if (pg != nullptr) {
			if (pg->owner != _current_company) return CMD_ERROR;
			if (pg->vehicle_type != g->vehicle_type) return CMD_ERROR;

			/* Ensure request parent isn't child of group.
			 * This is the only place that infinite loops are prevented. */
			if (GroupIsInGroup(pg->index, g->index)) return_cmd_error(STR_ERROR_GROUP_CAN_T_SET_PARENT_RECURSION);
		}

		if (flags & DC_EXEC) {
			g->parent = (pg == nullptr) ? INVALID_GROUP : pg->index;
			GroupStatistics::UpdateAutoreplace(g->owner);

			if (g->livery.in_use == 0) {
				const Livery *livery = GetParentLivery(g);
				g->livery.colour1 = livery->colour1;
				g->livery.colour2 = livery->colour2;

				PropagateChildLivery(g);
				MarkWholeScreenDirty();
			}
		}
	}

	if (flags & DC_EXEC) {
		InvalidateWindowData(WC_REPLACE_VEHICLE, g->vehicle_type, 1);
		InvalidateWindowData(GetWindowClassForVehicleType(g->vehicle_type), VehicleListIdentifier(VL_GROUP_LIST, g->vehicle_type, _current_company).Pack());
		InvalidateWindowData(WC_COMPANY_COLOUR, g->owner, g->vehicle_type);
		InvalidateWindowClassesData(WC_VEHICLE_VIEW);
		InvalidateWindowClassesData(WC_VEHICLE_DETAILS);
	}

	return CommandCost();
}


/**
 * Do add a vehicle to a group.
 * @param v Vehicle to add.
 * @param new_g Group to add to.
 */
static void AddVehicleToGroup(Vehicle *v, GroupID new_g)
{
	GroupStatistics::CountVehicle(v, -1);

	switch (v->type) {
		default: NOT_REACHED();
		case VEH_TRAIN:
			SetTrainGroupID(Train::From(v), new_g);
			break;

		case VEH_ROAD:
		case VEH_SHIP:
		case VEH_AIRCRAFT:
			if (v->IsEngineCountable()) UpdateNumEngineGroup(v, v->group_id, new_g);
			v->group_id = new_g;
			for (Vehicle *u = v; u != nullptr; u = u->Next()) {
				u->colourmap = PAL_NONE;
				u->InvalidateNewGRFCache();
				u->UpdateViewport(true);
			}
			break;
	}

	GroupStatistics::CountVehicle(v, 1);
}

/**
 * Create a new group, rename it with an automatically generated name and add vehicle to this group
 * @param tile unused
 * @param flags type of operation
 * @param p1   vehicle to add to a group
 *   - p1 bit 0-19 : VehicleID
 *   - p1 bit   31 : Add shared vehicles as well.
 * @param p2   parent groupid
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdCreateGroupAutogenName(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	Vehicle *v = Vehicle::GetIfValid(GB(p1, 0, 20));

	if (v == nullptr) return CMD_ERROR;

	if (v->owner != _current_company || !v->IsPrimaryVehicle()) return CMD_ERROR;

	/* Get the essential orders */
	std::vector<Order *> unique_orders;

	Order *order;
	FOR_VEHICLE_ORDERS(v, order) {
		if (order->IsType(OT_GOTO_STATION)) {
			if (std::find_if(unique_orders.begin(), unique_orders.end(), [order](Order *o) { return o->GetDestination() == order->GetDestination(); }) == unique_orders.end()) {
				unique_orders.push_back(order);
			}
		}
	}

	if (unique_orders.empty()) return_cmd_error(STR_ERROR_GROUP_CAN_T_CREATE_NAME);

	/* Create the name */

	static char str[71] = { "" };  // 5 + 31 + 3 + 31 + 1: ["cargo abbreviation"] "town/station name max. length" - "town/station name max. length""\0"

	StringID cargo_abbreviation_si = INVALID_STRING_ID;
	StringID cargo_name = INVALID_STRING_ID;

	for (Vehicle *u = v; u != nullptr; u = u->Next()) {
		if (u->cargo_cap == 0) continue;

		const CargoSpec *cs = CargoSpec::Get(u->cargo_type);
		cargo_abbreviation_si = cs->abbrev;
		cargo_name = cs->name;
		break;
	}

	if (cargo_abbreviation_si == INVALID_STRING_ID || cargo_name == INVALID_STRING_ID) return_cmd_error(STR_ERROR_GROUP_CAN_T_CREATE_NAME);

	// Remove the 'tiny font' formatting
	static char buf[7] = { "" }; // 3 + 2 + 2 : TINYFONT + cargo abbreviation + "\0\0"
	SetDParam(0, cargo_abbreviation_si);
	GetString(buf, STR_JUST_STRING, lastof(buf));
	const char *cargo_abbreviation = SkipGarbage(buf);

	if (_settings_client.gui.autogen_group_name == 1) { // Use station names

		static char stationname_first[MAX_LENGTH_STATION_NAME_CHARS] = { "" };
		static char stationname_last[MAX_LENGTH_STATION_NAME_CHARS] = { "" };

		SetDParam(0, unique_orders.front()->GetDestination());
		GetString(stationname_first, STR_STATION_NAME, lastof(stationname_first));

		SetDParam(0, unique_orders.back()->GetDestination());
		GetString(stationname_last, STR_STATION_NAME, lastof(stationname_last));

		SetDParamStr(0, cargo_abbreviation);
		SetDParam(1, unique_orders.front()->GetDestination());
		SetDParam(2, unique_orders.back()->GetDestination());
		GetString(str, STR_GROUP_AUTOGEN_NAME_STATION, lastof(str));

	} else { //Use town names

		Station *station_first = Station::GetIfValid(unique_orders.front()->GetDestination());
		Station *station_last = Station::GetIfValid(unique_orders.back()->GetDestination());

		if (station_last == nullptr || station_first == nullptr) return_cmd_error(STR_ERROR_GROUP_CAN_T_CREATE_NAME);

		Town *town_first = station_first->town;
		Town *town_last = station_last->town;

		if (town_first->index == town_last->index) { // First and last station belong to the same town
			SetDParamStr(0, cargo_abbreviation);
			SetDParam(1, town_first->index);
			GetString(str, STR_GROUP_AUTOGEN_NAME_TOWN_LOCAL, lastof(str));
		} else {
			static char townname_first[MAX_LENGTH_TOWN_NAME_CHARS] = { "" };
			static char townname_last[MAX_LENGTH_TOWN_NAME_CHARS] = { "" };

			SetDParam(0, town_first->index);
			GetString(townname_first, STR_TOWN_NAME, lastof(townname_first));

			SetDParam(0, town_last->index);
			GetString(townname_last, STR_TOWN_NAME, lastof(townname_last));

			SetDParamStr(0, cargo_abbreviation);
			SetDParam(1, town_first->index);
			SetDParam(2, town_last->index );
			GetString(str, STR_GROUP_AUTOGEN_NAME_TOWN, lastof(str));
		}
	}

	if (Utf8StringLength(str) >= MAX_LENGTH_GROUP_NAME_CHARS) return CMD_ERROR;

	static char ca_str[64] = { "" };
	SetDParam(0, cargo_name);
	GetString(ca_str, STR_JUST_STRING, lastof(ca_str));

	GroupID pg = p2;
	if (pg == INVALID_GROUP) {
		for (const Group *g : Group::Iterate()) {
			if (g->vehicle_type == v->type && g->owner == _current_company && !g->name.empty() && strnatcmp(ca_str, g->name.c_str()) == 0) {
				pg = g->index;
				break;
			}
		}
		if (pg == INVALID_GROUP) {
			CommandCost ret = CmdCreateGroup(0, flags, v->type, INVALID_GROUP, nullptr);
			if (ret.Failed()) return ret;
			pg = _new_group_id;
			CmdAlterGroup(0, flags, pg, 0, ca_str);
		}
	}

	CommandCost ret = CmdCreateGroup(0, flags, v->type, pg, nullptr);

	if (ret.Failed()) return ret;

	GroupID new_g = _new_group_id;
	CmdAlterGroup(0, flags, new_g, 0, str);

	if (flags & DC_EXEC) {
		AddVehicleToGroup(v, new_g);

		if (HasBit(p1, 31)) {
			/* Add vehicles in the shared order list as well. */
			for (Vehicle *u = v->FirstShared(); u != nullptr; u = u->NextShared()) {
				if (u->group_id != new_g) {
					AddVehicleToGroup(u, new_g);
				}
			}
		}

		GroupStatistics::UpdateAutoreplace(v->owner);

		/* Update the Replace Vehicle Windows */
		SetWindowDirty(WC_REPLACE_VEHICLE, v->type);
		InvalidateWindowData(GetWindowClassForVehicleType(v->type), VehicleListIdentifier(VL_GROUP_LIST, v->type, _current_company).Pack());
	}

	return CommandCost();
}

/**
* Create groups for all vehicles of a certain type that are not yet in any group.
* @param tile unused
* @param flags type of operation
* @param p1   The ID of the company whos vehicles should be auto-grouped.
* @param p2   The VehicleType of the vehicles that should be auto-grouped.
* @param text unused
* @return the cost of this operation or an error
*/
CommandCost CmdAutoGroupVehicles(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	CompanyID company_id = (CompanyID)p1;
	VehicleType vehicle_type = (VehicleType)p2;

	assert(Company::GetIfValid(company_id) != nullptr);

	if (flags & DC_EXEC) {
		for (const Vehicle *v : Vehicle::Iterate()) {
			if (v->type == vehicle_type && v->IsPrimaryVehicle() && v->owner == company_id && v->group_id == DEFAULT_GROUP) {
				DoCommand(0, v->index | (1 << 31), INVALID_GROUP, flags, CMD_CREATE_GROUP_AUTOGEN_NAME);
			}
		}

		InvalidateWindowData(GetWindowClassForVehicleType(vehicle_type), VehicleListIdentifier(VL_GROUP_LIST, vehicle_type, _current_company).Pack());
		InvalidateWindowClassesData(GetWindowClassForVehicleType(vehicle_type));
	}

	return CommandCost();
}

/**
 * Add a vehicle to a group
 * @param tile unused
 * @param flags type of operation
 * @param p1   index of array group
 *   - p1 bit 0-15 : GroupID
 * @param p2   vehicle to add to a group
 *   - p2 bit 0-19 : VehicleID
 *   - p2 bit   31 : Add shared vehicles as well.
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdAddVehicleGroup(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	Vehicle *v = Vehicle::GetIfValid(GB(p2, 0, 20));
	GroupID new_g = p1;

	if (v == nullptr || (!Group::IsValidID(new_g) && !IsDefaultGroupID(new_g) && new_g != NEW_GROUP)) return CMD_ERROR;

	if (Group::IsValidID(new_g)) {
		Group *g = Group::Get(new_g);
		if (g->owner != _current_company || g->vehicle_type != v->type) return CMD_ERROR;
	}

	if (v->owner != _current_company || !v->IsPrimaryVehicle()) return CMD_ERROR;

	if (new_g == NEW_GROUP) {
		/* Create new group. */
		CommandCost ret = CmdCreateGroup(0, flags, v->type, INVALID_GROUP, nullptr);
		if (ret.Failed()) return ret;

		new_g = _new_group_id;
	}

	if (flags & DC_EXEC) {
		AddVehicleToGroup(v, new_g);

		if (HasBit(p2, 31)) {
			/* Add vehicles in the shared order list as well. */
			for (Vehicle *v2 = v->FirstShared(); v2 != nullptr; v2 = v2->NextShared()) {
				if (v2->group_id != new_g) AddVehicleToGroup(v2, new_g);
			}
		}

		GroupStatistics::UpdateAutoreplace(v->owner);

		/* Update the Replace Vehicle Windows */
		SetWindowDirty(WC_REPLACE_VEHICLE, v->type);
		SetWindowDirty(WC_VEHICLE_DEPOT, v->tile);
		SetWindowDirty(WC_VEHICLE_VIEW, v->index);
		SetWindowDirty(WC_VEHICLE_DETAILS, v->index);
		InvalidateWindowData(GetWindowClassForVehicleType(v->type), VehicleListIdentifier(VL_GROUP_LIST, v->type, _current_company).Pack());
		InvalidateWindowData(WC_VEHICLE_VIEW, v->index);
		InvalidateWindowData(WC_VEHICLE_DETAILS, v->index);
	}

	return CommandCost();
}

/**
 * Add all shared vehicles of all vehicles from a group
 * @param tile unused
 * @param flags type of operation
 * @param p1   index of group array
 *  - p1 bit 0-15 : GroupID
 * @param p2   type of vehicles
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdAddSharedVehicleGroup(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleType type = Extract<VehicleType, 0, 3>(p2);
	GroupID id_g = p1;
	if (!Group::IsValidID(id_g) || !IsCompanyBuildableVehicleType(type)) return CMD_ERROR;

	if (flags & DC_EXEC) {
		/* Find the first front engine which belong to the group id_g
		 * then add all shared vehicles of this front engine to the group id_g */
		for (const Vehicle *v : Vehicle::Iterate()) {
			if (v->type == type && v->IsPrimaryVehicle()) {
				if (v->group_id != id_g) continue;

				/* For each shared vehicles add it to the group */
				for (Vehicle *v2 = v->FirstShared(); v2 != nullptr; v2 = v2->NextShared()) {
					if (v2->group_id != id_g) DoCommand(tile, id_g, v2->index, flags, CMD_ADD_VEHICLE_GROUP, text);
				}
			}
		}

		InvalidateWindowData(GetWindowClassForVehicleType(type), VehicleListIdentifier(VL_GROUP_LIST, type, _current_company).Pack());
	}

	return CommandCost();
}


/**
 * Remove all vehicles from a group
 * @param tile unused
 * @param flags type of operation
 * @param p1   index of group array
 * - p1 bit 0-15 : GroupID
 * @param p2   unused
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdRemoveAllVehiclesGroup(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	GroupID old_g = p1;
	Group *g = Group::GetIfValid(old_g);

	if (g == nullptr || g->owner != _current_company) return CMD_ERROR;

	if (flags & DC_EXEC) {
		/* Find each Vehicle that belongs to the group old_g and add it to the default group */
		for (const Vehicle *v : Vehicle::Iterate()) {
			if (v->IsPrimaryVehicle()) {
				if (v->group_id != old_g) continue;

				/* Add The Vehicle to the default group */
				DoCommand(tile, DEFAULT_GROUP, v->index, flags, CMD_ADD_VEHICLE_GROUP, text);
			}
		}

		InvalidateWindowData(GetWindowClassForVehicleType(g->vehicle_type), VehicleListIdentifier(VL_GROUP_LIST, g->vehicle_type, _current_company).Pack());
	}

	return CommandCost();
}

/**
 * Set the livery for a vehicle group.
 * @param tile      Unused.
 * @param flags     Command flags.
 * @param p1
 * - p1 bit  0-15   Group ID.
 * @param p2
 * - p2 bit  8      Set secondary instead of primary colour
 * - p2 bit 16-23   Colour.
 */
CommandCost CmdSetGroupLivery(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	Group *g = Group::GetIfValid(p1);
	bool primary = !HasBit(p2, 8);
	Colours colour = Extract<Colours, 16, 8>(p2);

	if (g == nullptr || g->owner != _current_company) return CMD_ERROR;

	if (colour >= COLOUR_END && colour != INVALID_COLOUR) return CMD_ERROR;

	if (flags & DC_EXEC) {
		if (primary) {
			SB(g->livery.in_use, 0, 1, colour != INVALID_COLOUR);
			if (colour == INVALID_COLOUR) colour = (Colours)GetParentLivery(g)->colour1;
			g->livery.colour1 = colour;
		} else {
			SB(g->livery.in_use, 1, 1, colour != INVALID_COLOUR);
			if (colour == INVALID_COLOUR) colour = (Colours)GetParentLivery(g)->colour2;
			g->livery.colour2 = colour;
		}

		PropagateChildLivery(g);
		MarkWholeScreenDirty();
	}

	return CommandCost();
}

/**
 * Set replace protection for a group and its sub-groups.
 * @param g initial group.
 * @param protect 1 to set or 0 to clear protection.
 */
static void SetGroupReplaceProtection(Group *g, bool protect)
{
	g->replace_protection = protect;

	for (Group *pg : Group::Iterate()) {
		if (pg->parent == g->index) SetGroupReplaceProtection(pg, protect);
	}
}

/**
 * (Un)set global replace protection from a group
 * @param tile unused
 * @param flags type of operation
 * @param p1   index of group array
 * - p1 bit 0-15 : GroupID
 * @param p2
 * - p2 bit 0    : 1 to set or 0 to clear protection.
 * - p2 bit 1    : 1 to apply to sub-groups.
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdSetGroupReplaceProtection(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	Group *g = Group::GetIfValid(p1);
	if (g == nullptr || g->owner != _current_company) return CMD_ERROR;

	if (flags & DC_EXEC) {
		if (HasBit(p2, 1)) {
			SetGroupReplaceProtection(g, HasBit(p2, 0));
		} else {
			g->replace_protection = HasBit(p2, 0);
		}

		SetWindowDirty(GetWindowClassForVehicleType(g->vehicle_type), VehicleListIdentifier(VL_GROUP_LIST, g->vehicle_type, _current_company).Pack());
		InvalidateWindowData(WC_REPLACE_VEHICLE, g->vehicle_type);
	}

	return CommandCost();
}

/**
 * Decrease the num_vehicle variable before delete an front engine from a group
 * @note Called in CmdSellRailWagon and DeleteLasWagon,
 * @param v     FrontEngine of the train we want to remove.
 */
void RemoveVehicleFromGroup(const Vehicle *v)
{
	if (!v->IsPrimaryVehicle()) return;

	if (!IsDefaultGroupID(v->group_id)) GroupStatistics::CountVehicle(v, -1);
}


/**
 * Affect the groupID of a train to new_g.
 * @note called in CmdAddVehicleGroup and CmdMoveRailVehicle
 * @param v     First vehicle of the chain.
 * @param new_g index of array group
 */
void SetTrainGroupID(Train *v, GroupID new_g)
{
	if (!Group::IsValidID(new_g) && !IsDefaultGroupID(new_g)) return;

	assert(v->IsFrontEngine() || IsDefaultGroupID(new_g));

	for (Vehicle *u = v; u != nullptr; u = u->Next()) {
		if (u->IsEngineCountable()) UpdateNumEngineGroup(u, u->group_id, new_g);

		u->group_id = new_g;
		u->colourmap = PAL_NONE;
		u->InvalidateNewGRFCache();
		u->UpdateViewport(true);
	}

	/* Update the Replace Vehicle Windows */
	GroupStatistics::UpdateAutoreplace(v->owner);
	SetWindowDirty(WC_REPLACE_VEHICLE, VEH_TRAIN);
}


/**
 * Recalculates the groupID of a train. Should be called each time a vehicle is added
 * to/removed from the chain,.
 * @note this needs to be called too for 'wagon chains' (in the depot, without an engine)
 * @note Called in CmdBuildRailVehicle, CmdBuildRailWagon, CmdMoveRailVehicle, CmdSellRailWagon
 * @param v First vehicle of the chain.
 */
void UpdateTrainGroupID(Train *v)
{
	assert(v->IsFrontEngine() || v->IsFreeWagon());

	GroupID new_g = v->IsFrontEngine() ? v->group_id : (GroupID)DEFAULT_GROUP;
	for (Vehicle *u = v; u != nullptr; u = u->Next()) {
		if (u->IsEngineCountable()) UpdateNumEngineGroup(u, u->group_id, new_g);

		u->group_id = new_g;
		u->colourmap = PAL_NONE;
		u->InvalidateNewGRFCache();
	}

	/* Update the Replace Vehicle Windows */
	GroupStatistics::UpdateAutoreplace(v->owner);
	SetWindowDirty(WC_REPLACE_VEHICLE, VEH_TRAIN);
}

/**
 * Get the number of engines with EngineID id_e in the group with GroupID
 * id_g and its sub-groups.
 * @param company The company the group belongs to
 * @param id_g The GroupID of the group used
 * @param id_e The EngineID of the engine to count
 * @return The number of engines with EngineID id_e in the group
 */
uint GetGroupNumEngines(CompanyID company, GroupID id_g, EngineID id_e)
{
	uint count = 0;
	const Engine *e = Engine::Get(id_e);
	for (const Group *g : Group::Iterate()) {
		if (g->parent == id_g) count += GetGroupNumEngines(company, g->index, id_e);
	}
	return count + GroupStatistics::Get(company, id_g, e->type).num_engines[id_e];
}

/**
 * Get the number of vehicles in the group with GroupID
 * id_g and its sub-groups.
 * @param company The company the group belongs to
 * @param id_g The GroupID of the group used
 * @param type The vehicle type of the group
 * @return The number of vehicles in the group
 */
uint GetGroupNumVehicle(CompanyID company, GroupID id_g, VehicleType type)
{
	uint count = 0;
	for (const Group *g : Group::Iterate()) {
		if (g->parent == id_g) count += GetGroupNumVehicle(company, g->index, type);
	}
	return count + GroupStatistics::Get(company, id_g, type).num_vehicle;
}

/**
 * Get the number of vehicles above profit minimum age in the group with GroupID
 * id_g and its sub-groups.
 * @param company The company the group belongs to
 * @param id_g The GroupID of the group used
 * @param type The vehicle type of the group
 * @return The number of vehicles above profit minimum age in the group
 */
uint GetGroupNumProfitVehicle(CompanyID company, GroupID id_g, VehicleType type)
{
	uint count = 0;
	for (const Group *g : Group::Iterate()) {
		if (g->parent == id_g) count += GetGroupNumProfitVehicle(company, g->index, type);
	}
	return count + GroupStatistics::Get(company, id_g, type).num_profit_vehicle;
}

/**
 * Get last year's profit for the group with GroupID
 * id_g and its sub-groups.
 * @param company The company the group belongs to
 * @param id_g The GroupID of the group used
 * @param type The vehicle type of the group
 * @return Last year's profit for the group
 */
Money GetGroupProfitLastYear(CompanyID company, GroupID id_g, VehicleType type)
{
	Money sum = 0;
	for (const Group *g : Group::Iterate()) {
		if (g->parent == id_g) sum += GetGroupProfitLastYear(company, g->index, type);
	}
	return sum + GroupStatistics::Get(company, id_g, type).profit_last_year;
}

void RemoveAllGroupsForCompany(const CompanyID company)
{
	for (Group *g : Group::Iterate()) {
		if (company == g->owner) delete g;
	}
}


/**
 * Test if GroupID group is a descendant of (or is) GroupID search
 * @param search The GroupID to search in
 * @param group The GroupID to search for
 * @return True iff group is search or a descendant of search
 */
bool GroupIsInGroup(GroupID search, GroupID group)
{
	if (!Group::IsValidID(search)) return search == group;

	do {
		if (search == group) return true;
		search = Group::Get(search)->parent;
	} while (search != INVALID_GROUP);

	return false;
}
