/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_group.hpp Everything to put vehicles into groups. */

#ifndef SCRIPT_GROUP_HPP
#define SCRIPT_GROUP_HPP

#include "script_vehicle.hpp"
#include "../../group_type.h"

/**
 * Class that handles all group related functions.
 * @api ai
 */
class ScriptGroup : public ScriptObject {
public:
	/**
	 * The group IDs of some special groups.
	 */
	enum GroupID {
		/* Note: these values represent part of the in-game static values */
		GROUP_ALL     = ::ALL_GROUP,     ///< All vehicles are in this group.
		GROUP_DEFAULT = ::DEFAULT_GROUP, ///< Vehicles not put in any other group are in this one.
		GROUP_INVALID = ::INVALID_GROUP, ///< An invalid group id.
	};

	/**
	 * Checks whether the given group is valid.
	 * @param group_id The group to check.
	 * @pre group_id != GROUP_DEFAULT && group_id != GROUP_ALL.
	 * @return True if and only if the group is valid.
	 */
	static bool IsValidGroup(GroupID group_id);

	/**
	 * Create a new group.
	 * @param vehicle_type The type of vehicle to create a group for.
	 * @param parent_group_id The parent group id to create this group under, INVALID_GROUP for top-level.
	 * @return The GroupID of the new group, or an invalid GroupID when
	 *  it failed. Check the return value using IsValidGroup(). In test-mode
	 *  0 is returned if it was successful; any other value indicates failure.
	 */
	static GroupID CreateGroup(ScriptVehicle::VehicleType vehicle_type, GroupID parent_group_id);

	/**
	 * Create a new group, rename it with specific name and add vehicle to this group
	 *
	 * @param vehicle_id vehicle to add to a group
	 * @param parent_group_id parent groupid
	 * @param add_shared_vehicles whether to add shared vehicles as well
	 * @return The GroupID of the new group, or an invalid GroupID when
	 *  it failed. Check the return value using IsValidGroup(). In test-mode
	 *  0 is returned if it was successful; any other value indicates failure.
	 */
	static GroupID CreateGroupAutogenName(VehicleID vehicle_id, GroupID parent_group_id, bool add_shared_vehicles = false);

	/**
	 * Create groups for all vehicles of a certain type that are not yet in any group.
	 *
	 * @param vehicle_type The VehicleType of the vehicles that should be auto-grouped.
	 * @return False if the groups was not successfully created.
	 */
	static void AutoGroupVehicles(ScriptVehicle::VehicleType vehicle_type);

	/**
	 * Delete the given group. When the deletion succeeds all vehicles in the
	 *  given group will move to the GROUP_DEFAULT.
	 * @param group_id The group to delete.
	 * @pre IsValidGroup(group_id).
	 * @return True if and only if the group was successfully deleted.
	 */
	static bool DeleteGroup(GroupID group_id);

	/**
	 * Get the vehicle type of a group.
	 * @param group_id The group to get the type from.
	 * @pre IsValidGroup(group_id).
	 * @return The vehicletype of the given group.
	 */
	static ScriptVehicle::VehicleType GetVehicleType(GroupID group_id);

	/**
	 * Set the name of a group.
	 * @param group_id The group to set the name for.
	 * @param name The name for the group (can be either a raw string, or a ScriptText object).
	 * @pre IsValidGroup(group_id).
	 * @pre name != nullptr && len(name) != 0
	 * @exception ScriptError::ERR_NAME_IS_NOT_UNIQUE
	 * @return True if and only if the name was changed.
	 */
	static bool SetName(GroupID group_id, Text *name);

	/**
	 * Get the name of a group.
	 * @param group_id The group to get the name of.
	 * @pre IsValidGroup(group_id).
	 * @return The name the group has.
	 */
	static char *GetName(GroupID group_id);

	/**
	 * Set parent group of a group.
	 * @param group_id The group to set the parent for.
	 * @param parent_group_id The parent group to set.
	 * @pre IsValidGroup(group_id).
	 * @pre IsValidGroup(parent_group_id).
	 * @return True if and only if the parent group was changed.
	 */
	static bool SetParent(GroupID group_id, GroupID parent_group_id);

	/**
	 * Get parent group of a group.
	 * @param group_id The group to get the parent of.
	 * @pre IsValidGroup(group_id).
	 * @return The group id of the parent group.
	 */
	static GroupID GetParent(GroupID group_id);

	/**
	 * Enable or disable autoreplace protected. If the protection is
	 *  enabled, global autoreplace won't affect vehicles in this group.
	 * @param group_id The group to change the protection for.
	 * @param enable True if protection should be enabled.
	 * @pre IsValidGroup(group_id).
	 * @return True if and only if the protection was successfully changed.
	 */
	static bool EnableAutoReplaceProtection(GroupID group_id, bool enable);

	/**
	 * Get the autoreplace protection status.
	 * @param group_id The group to get the protection status for.
	 * @pre IsValidGroup(group_id).
	 * @return The autoreplace protection status for the given group.
	 */
	static bool GetAutoReplaceProtection(GroupID group_id);

	/**
	 * Get the number of engines in a given group.
	 * @param group_id The group to get the number of engines in.
	 * @param engine_id The engine id to count.
	 * @pre IsValidGroup(group_id) || group_id == GROUP_ALL || group_id == GROUP_DEFAULT.
	 * @return The number of engines with id engine_id in the group with id group_id.
	 */
	static int32 GetNumEngines(GroupID group_id, EngineID engine_id);

	/**
	 * Move a vehicle to a group.
	 * @param group_id The group to move the vehicle to.
	 * @param vehicle_id The vehicle to move to the group.
	 * @pre IsValidGroup(group_id) || group_id == GROUP_DEFAULT.
	 * @pre ScriptVehicle::IsValidVehicle(vehicle_id).
	 * @return True if and only if the vehicle was successfully moved to the group.
	 * @note A vehicle can be in only one group at the same time. To remove it from
	 *  a group, move it to another or to GROUP_DEFAULT. Moving the vehicle to the
	 *  given group means removing it from another group.
	 */
	static bool MoveVehicle(GroupID group_id, VehicleID vehicle_id);

	/**
	 * Enable or disable the removal of wagons when a (part of a) vehicle is
	 *  (auto)replaced with a longer variant (longer wagons or longer engines)
	 *  If enabled, wagons are removed from the end of the vehicle until it
	 *  fits in the same number of tiles as it did before.
	 * @param keep_length If true, wagons will be removed if the new engine is longer.
	 * @return True if and only if the value was successfully changed.
	 */
	static bool EnableWagonRemoval(bool keep_length);

	/**
	 * Get the current status of wagon removal.
	 * @return Whether or not wagon removal is enabled.
	 */
	static bool HasWagonRemoval();

	/**
	 * Start replacing all vehicles with a specified engine with another engine.
	 * @param group_id The group to replace vehicles from. Use ALL_GROUP to replace
	 *  vehicles from all groups that haven't set autoreplace protection.
	 * @param engine_id_old The engine id to start replacing.
	 * @param engine_id_new The engine id to replace with.
	 * @pre IsValidGroup(group_id) || group_id == GROUP_DEFAULT || group_id == GROUP_ALL.
	 * @pre ScriptEngine.IsBuildable(engine_id_new).
	 * @return True if and if the replacing was successfully started.
	 * @note To stop autoreplacing engine_id_old, call StopAutoReplace(group_id, engine_id_old).
	 */
	static bool SetAutoReplace(GroupID group_id, EngineID engine_id_old, EngineID engine_id_new);

	/**
	 * Get the EngineID the given EngineID is replaced with.
	 * @param group_id The group to get the replacement from.
	 * @param engine_id The engine that is being replaced.
	 * @pre IsValidGroup(group_id) || group_id == GROUP_DEFAULT || group_id == GROUP_ALL.
	 * @return The EngineID that is replacing engine_id or an invalid EngineID
	 *   in case engine_id is not begin replaced.
	 */
	static EngineID GetEngineReplacement(GroupID group_id, EngineID engine_id);

	/**
	 * Stop replacing a certain engine in the specified group.
	 * @param group_id The group to stop replacing the engine in.
	 * @param engine_id The engine id to stop replacing with another engine.
	 * @pre IsValidGroup(group_id) || group_id == GROUP_DEFAULT || group_id == GROUP_ALL.
	 * @return True if and if the replacing was successfully stopped.
	 */
	static bool StopAutoReplace(GroupID group_id, EngineID engine_id);

	/**
	 * Get the current profit of a group.
	 * @param group_id The group to get the profit of.
	 * @pre IsValidGroup(group_id).
	 * @return The current profit the group has.
	 */
	static Money GetProfitThisYear(GroupID group_id);

	/**
	 * Get the profit of last year of a group.
	 * @param group_id The group to get the profit of.
	 * @pre IsValidGroup(group_id).
	 * @return The current profit the group had last year.
	 */
	static Money GetProfitLastYear(GroupID group_id);

	/**
	 * Get the current vehicle usage of a group.
	 * @param group_id The group to get the current usage of.
	 * @pre IsValidGroup(group_id).
	 * @return The current usage of the group.
	 */
	static uint32 GetCurrentUsage(GroupID group_id);

	/**
	 * Set primary colour for a group.
	 * @param group_id The group id to set the colour of.
	 * @param colour Colour to set.
	 * @pre IsValidGroup(group_id).
	 */
	static bool SetPrimaryColour(GroupID group_id, ScriptCompany::Colours colour);

	/**
	 * Set secondary colour for a group.
	 * @param group_id The group id to set the colour of.
	 * @param colour Colour to set.
	 * @pre IsValidGroup(group_id).
	 */
	static bool SetSecondaryColour(GroupID group_id, ScriptCompany::Colours colour);

	/**
	 * Get primary colour of a group.
	 * @param group_id The group id to get the colour of.
	 * @pre IsValidGroup(group_id).
	 */
	static ScriptCompany::Colours GetPrimaryColour(GroupID group_id);

	/**
	 * Get secondary colour for a group.
	 * @param group_id The group id to get the colour of.
	 * @pre IsValidGroup(group_id).
	 */
	static ScriptCompany::Colours GetSecondaryColour(GroupID group_id);
};

#endif /* SCRIPT_GROUP_HPP */
