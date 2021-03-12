/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_group.cpp Implementation of ScriptGroup. */

#include "../../stdafx.h"
#include "script_group.hpp"
#include "script_engine.hpp"
#include "../script_instance.hpp"
#include "../../string_func.h"
#include "../../strings_func.h"
#include "../../autoreplace_func.h"
#include "../../settings_func.h"
#include "../../vehicle_base.h"
#include "table/strings.h"

#include "../../safeguards.h"

/* static */ bool ScriptGroup::IsValidGroup(GroupID group_id)
{
	const Group *g = ::Group::GetIfValid(group_id);
	return g != nullptr && g->owner == ScriptObject::GetCompany();
}

/* static */ ScriptGroup::GroupID ScriptGroup::CreateGroup(ScriptVehicle::VehicleType vehicle_type, GroupID parent_group_id)
{
	if (!ScriptObject::DoCommand(0, (::VehicleType)vehicle_type, parent_group_id, CMD_CREATE_GROUP, nullptr, &ScriptInstance::DoCommandReturnGroupID)) return GROUP_INVALID;

	/* In case of test-mode, we return GroupID 0 */
	return (ScriptGroup::GroupID)0;
}

/* static */ ScriptGroup::GroupID ScriptGroup::CreateGroupAutogenName(VehicleID vehicle_id, GroupID parent_group_id, bool add_shared_vehicles)
{
	EnforcePrecondition(ScriptGroup::GROUP_INVALID, ScriptVehicle::IsValidVehicle(vehicle_id));
	if (!ScriptObject::DoCommand(0, vehicle_id | (add_shared_vehicles << 31), parent_group_id, CMD_CREATE_GROUP_AUTOGEN_NAME, nullptr, &ScriptInstance::DoCommandReturnGroupID)) return GROUP_INVALID;

	/* In case of test-mode, we return GroupID 0 */
	return (ScriptGroup::GroupID)0;
}

/* static */ void ScriptGroup::AutoGroupVehicles(ScriptVehicle::VehicleType vehicle_type)
{
	ScriptObject::DoCommand(0, ScriptObject::GetCompany(), (::VehicleType)vehicle_type, CMD_AUTO_GROUP_VEHICLES, nullptr);
}


/* static */ bool ScriptGroup::DeleteGroup(GroupID group_id)
{
	EnforcePrecondition(false, IsValidGroup(group_id));

	return ScriptObject::DoCommand(0, group_id, 0, CMD_DELETE_GROUP);
}

/* static */ ScriptVehicle::VehicleType ScriptGroup::GetVehicleType(GroupID group_id)
{
	if (!IsValidGroup(group_id)) return ScriptVehicle::VT_INVALID;

	return (ScriptVehicle::VehicleType)((::VehicleType)::Group::Get(group_id)->vehicle_type);
}

/* static */ bool ScriptGroup::SetName(GroupID group_id, Text *name)
{
	CCountedPtr<Text> counter(name);

	EnforcePrecondition(false, IsValidGroup(group_id));
	EnforcePrecondition(false, name != nullptr);
	const char *text = name->GetDecodedText();
	EnforcePreconditionEncodedText(false, text);
	EnforcePreconditionCustomError(false, ::Utf8StringLength(text) < MAX_LENGTH_GROUP_NAME_CHARS, ScriptError::ERR_PRECONDITION_STRING_TOO_LONG);

	return ScriptObject::DoCommand(0, group_id, 0, CMD_ALTER_GROUP, text);
}

/* static */ char *ScriptGroup::GetName(GroupID group_id)
{
	if (!IsValidGroup(group_id)) return nullptr;

	::SetDParam(0, group_id);
	return GetString(STR_GROUP_NAME);
}

/* static */ bool ScriptGroup::SetParent(GroupID group_id, GroupID parent_group_id)
{
	EnforcePrecondition(false, IsValidGroup(group_id));
	EnforcePrecondition(false, IsValidGroup(parent_group_id));

	return ScriptObject::DoCommand(0, group_id | 1 << 16, parent_group_id, CMD_ALTER_GROUP);
}

/* static */ ScriptGroup::GroupID ScriptGroup::GetParent(GroupID group_id)
{
	EnforcePrecondition((ScriptGroup::GroupID)INVALID_GROUP, IsValidGroup(group_id));

	const Group *g = ::Group::GetIfValid(group_id);
	return (ScriptGroup::GroupID)g->parent;
}

/* static */ bool ScriptGroup::EnableAutoReplaceProtection(GroupID group_id, bool enable)
{
	EnforcePrecondition(false, IsValidGroup(group_id));

	return ScriptObject::DoCommand(0, group_id, enable ? 1 : 0, CMD_SET_GROUP_REPLACE_PROTECTION);
}

/* static */ bool ScriptGroup::GetAutoReplaceProtection(GroupID group_id)
{
	if (!IsValidGroup(group_id)) return false;

	return ::Group::Get(group_id)->replace_protection;
}

/* static */ int32 ScriptGroup::GetNumEngines(GroupID group_id, EngineID engine_id)
{
	if (!IsValidGroup(group_id) && group_id != GROUP_DEFAULT && group_id != GROUP_ALL) return -1;

	return GetGroupNumEngines(ScriptObject::GetCompany(), group_id, engine_id);
}

/* static */ bool ScriptGroup::MoveVehicle(GroupID group_id, VehicleID vehicle_id)
{
	EnforcePrecondition(false, IsValidGroup(group_id) || group_id == GROUP_DEFAULT);
	EnforcePrecondition(false, ScriptVehicle::IsValidVehicle(vehicle_id));

	return ScriptObject::DoCommand(0, group_id, vehicle_id, CMD_ADD_VEHICLE_GROUP);
}

/* static */ bool ScriptGroup::EnableWagonRemoval(bool enable_removal)
{
	if (HasWagonRemoval() == enable_removal) return true;

	return ScriptObject::DoCommand(0, ::GetCompanySettingIndex("company.renew_keep_length"), enable_removal ? 1 : 0, CMD_CHANGE_COMPANY_SETTING);
}

/* static */ bool ScriptGroup::HasWagonRemoval()
{
	return ::Company::Get(ScriptObject::GetCompany())->settings.renew_keep_length;
}

/* static */ bool ScriptGroup::SetAutoReplace(GroupID group_id, EngineID engine_id_old, EngineID engine_id_new)
{
	EnforcePrecondition(false, IsValidGroup(group_id) || group_id == GROUP_DEFAULT || group_id == GROUP_ALL);
	EnforcePrecondition(false, ScriptEngine::IsBuildable(engine_id_new));

	return ScriptObject::DoCommand(0, group_id << 16, (engine_id_new << 16) | engine_id_old, CMD_SET_AUTOREPLACE);
}

/* static */ EngineID ScriptGroup::GetEngineReplacement(GroupID group_id, EngineID engine_id)
{
	if (!IsValidGroup(group_id) && group_id != GROUP_DEFAULT && group_id != GROUP_ALL) return ::INVALID_ENGINE;

	return ::EngineReplacementForCompany(Company::Get(ScriptObject::GetCompany()), engine_id, group_id);
}

/* static */ bool ScriptGroup::StopAutoReplace(GroupID group_id, EngineID engine_id)
{
	EnforcePrecondition(false, IsValidGroup(group_id) || group_id == GROUP_DEFAULT || group_id == GROUP_ALL);

	return ScriptObject::DoCommand(0, group_id << 16, (::INVALID_ENGINE << 16) | engine_id, CMD_SET_AUTOREPLACE);
}

/* static */ Money ScriptGroup::GetProfitThisYear(GroupID group_id)
{
	if (!IsValidGroup(group_id)) return -1;

	Money profit = 0;

	for (const Vehicle *v : Vehicle::Iterate()) {
		if (v->group_id != group_id) continue;
		if (!v->IsPrimaryVehicle()) continue;

		profit += v->GetDisplayProfitThisYear();
	}

	return profit;
}

/* static */ Money ScriptGroup::GetProfitLastYear(GroupID group_id)
{
	if (!IsValidGroup(group_id)) return -1;

	return ::Group::Get(group_id)->statistics.profit_last_year;
}

/* static */ uint32 ScriptGroup::GetCurrentUsage(GroupID group_id)
{
	if (!IsValidGroup(group_id)) return -1;

	uint32 occupancy = 0;
	uint32 vehicle_count = 0;

	for (const Vehicle *v : Vehicle::Iterate()) {
		if (v->group_id != group_id) continue;
		if (!v->IsPrimaryVehicle()) continue;

		occupancy += v->trip_occupancy;
		vehicle_count++;
	}

	if (vehicle_count == 0) return -1;

	return occupancy / vehicle_count;
}

/* static */ bool ScriptGroup::SetPrimaryColour(GroupID group_id, ScriptCompany::Colours colour)
{
	EnforcePrecondition(false, IsValidGroup(group_id));

	return ScriptObject::DoCommand(0, group_id, colour << 16, CMD_SET_GROUP_LIVERY);
}

/* static */ bool ScriptGroup::SetSecondaryColour(GroupID group_id, ScriptCompany::Colours colour)
{
	EnforcePrecondition(false, IsValidGroup(group_id));

	return ScriptObject::DoCommand(0, group_id, (1 << 8) | (colour << 16), CMD_SET_GROUP_LIVERY);
}

/* static */ ScriptCompany::Colours ScriptGroup::GetPrimaryColour(GroupID group_id)
{
	EnforcePrecondition(ScriptCompany::Colours::COLOUR_INVALID, IsValidGroup(group_id));

	const Group *g = ::Group::GetIfValid(group_id);
	if (!HasBit(g->livery.in_use, 0)) return ScriptCompany::Colours::COLOUR_INVALID;
	return (ScriptCompany::Colours)g->livery.colour1;
}

/* static */ ScriptCompany::Colours ScriptGroup::GetSecondaryColour(GroupID group_id)
{
	EnforcePrecondition(ScriptCompany::Colours::COLOUR_INVALID, IsValidGroup(group_id));

	const Group *g = ::Group::GetIfValid(group_id);
	if (!HasBit(g->livery.in_use, 1)) return ScriptCompany::Colours::COLOUR_INVALID;
	return (ScriptCompany::Colours)g->livery.colour2;
}
