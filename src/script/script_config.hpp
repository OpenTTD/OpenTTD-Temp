/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_config.hpp ScriptConfig stores the configuration settings of every Script. */

#ifndef SCRIPT_CONFIG_HPP
#define SCRIPT_CONFIG_HPP

#include <map>
#include <list>
#include "../core/smallmap_type.hpp"
#include "../core/string_compare_type.hpp"
#include "../company_type.h"
#include "../textfile_gui.h"

/** Bitmask of flags for Script settings. */
enum ScriptConfigFlags {
	SCRIPTCONFIG_NONE      = 0x0, ///< No flags set.
	SCRIPTCONFIG_RANDOM    = 0x1, ///< When randomizing the Script, pick any value between min_value and max_value when on custom difficulty setting.
	SCRIPTCONFIG_BOOLEAN   = 0x2, ///< This value is a boolean (either 0 (false) or 1 (true) ).
	SCRIPTCONFIG_INGAME    = 0x4, ///< This setting can be changed while the Script is running.
	SCRIPTCONFIG_DEVELOPER = 0x8, ///< This setting will only be visible when the Script development tools are active.
};

typedef SmallMap<int, char *> LabelMapping; ///< Map-type used to map the setting numbers to labels.

/** Info about a single Script setting. */
struct ScriptConfigItem {
	const char *name;        ///< The name of the configuration setting.
	const char *description; ///< The description of the configuration setting.
	int min_value;           ///< The minimal value this configuration setting can have.
	int max_value;           ///< The maximal value this configuration setting can have.
	int custom_value;        ///< The default value on custom difficulty setting.
	int easy_value;          ///< The default value on easy difficulty setting.
	int medium_value;        ///< The default value on medium difficulty setting.
	int hard_value;          ///< The default value on hard difficulty setting.
	int random_deviation;    ///< The maximum random deviation from the default value.
	int step_size;           ///< The step size in the gui.
	ScriptConfigFlags flags; ///< Flags for the configuration setting.
	LabelMapping *labels;    ///< Text labels for the integer values.
	bool complete_labels;    ///< True if all values have a label.
};

typedef std::list<ScriptConfigItem> ScriptConfigItemList; ///< List of ScriptConfig items.

extern ScriptConfigItem _start_date_config;

/**
 * Script settings.
 */
class ScriptConfig {
protected:
	/** List with name=>value pairs of all script-specific settings */
	typedef std::map<const char *, int, StringCompare> SettingValueList;

public:
	ScriptConfig() :
		name(nullptr),
		version(-1),
		info(nullptr),
		config_list(nullptr),
		is_random(false)
	{}

	/**
	 * Create a new Script config that is a copy of an existing config.
	 * @param config The object to copy.
	 */
	ScriptConfig(const ScriptConfig *config);

	/** Delete an Script configuration. */
	virtual ~ScriptConfig();

	/**
	 * Set another Script to be loaded in this slot.
	 * @param name The name of the Script.
	 * @param version The version of the Script to load, or -1 of latest.
	 * @param force_exact_match If true try to find the exact same version
	 *   as specified. If false any compatible version is ok.
	 * @param is_random Is the Script chosen randomly?
	 */
	void Change(const char *name, int version = -1, bool force_exact_match = false, bool is_random = false);

	/**
	 * Get the ScriptInfo linked to this ScriptConfig.
	 */
	class ScriptInfo *GetInfo() const;

	/**
	 * Get the config list for this ScriptConfig.
	 */
	const ScriptConfigItemList *GetConfigList();

	/**
	 * Where to get the config from, either default (depends on current game
	 * mode) or force either newgame or normal
	 */
	enum ScriptSettingSource {
		SSS_DEFAULT,       ///< Get the Script config from the current game mode
		SSS_FORCE_NEWGAME, ///< Get the newgame Script config
		SSS_FORCE_GAME,    ///< Get the Script config from the current game
	};

	/**
	 * As long as the default of a setting has not been changed, the value of
	 * the setting is not stored. This to allow changing the difficulty setting
	 * without having to reset the script's config. However, when a setting may
	 * not be changed in game, we must "anchor" this value to what the setting
	 * would be at the time of starting. Otherwise changing the difficulty
	 * setting would change the setting's value (which isn't allowed).
	 */
	void AnchorUnchangeableSettings();

	/**
	 * Get the value of a setting for this config. It might fallback to his
	 *  'info' to find the default value (if not set or if not-custom difficulty
	 *  level).
	 * @return The (default) value of the setting, or -1 if the setting was not
	 *  found.
	 */
	virtual int GetSetting(const char *name) const;

	/**
	 * Set the value of a setting for this config.
	 */
	virtual void SetSetting(const char *name, int value);

	/**
	 * Reset all settings to their default value.
	 */
	void ResetSettings();

	/**
	 * Reset only editable and visible settings to their default value.
	 */
	void ResetEditableSettings(bool yet_to_start);

	/**
	 * Randomize all settings the Script requested to be randomized.
	 */
	virtual void AddRandomDeviation();

	/**
	 * Is this config attached to an Script? In other words, is there a Script
	 *  that is assigned to this slot.
	 */
	bool HasScript() const;

	/**
	 * Is the current Script a randomly chosen Script?
	 */
	bool IsRandom() const;

	/**
	 * Get the name of the Script.
	 */
	const char *GetName() const;

	/**
	 * Get the version of the Script.
	 */
	int GetVersion() const;

	/**
	 * Convert a string which is stored in the config file or savegames to
	 *  custom settings of this Script.
	 */
	void StringToSettings(const char *value);

	/**
	 * Convert the custom settings to a string that can be stored in the config
	 *  file or savegames.
	 */
	void SettingsToString(char *string, const char *last) const;

	/**
	 * Search a textfile file next to this script.
	 * @param type The type of the textfile to search for.
	 * @param slot #CompanyID to check status of.
	 * @return The filename for the textfile, \c nullptr otherwise.
	 */
	const char *GetTextfile(TextfileType type, CompanyID slot, TextDirection *text_dir = nullptr) const;

protected:
	const char *name;                  ///< Name of the Script
	int version;                       ///< Version of the Script
	class ScriptInfo *info;            ///< ScriptInfo object for related to this Script version
	SettingValueList settings;         ///< List with all setting=>value pairs that are configure for this Script
	ScriptConfigItemList *config_list; ///< List with all settings defined by this Script
	bool is_random;                    ///< True if the AI in this slot was randomly chosen.

	/**
	 * In case you have mandatory non-Script-definable config entries in your
	 *  list, add them to this function.
	 */
	virtual void PushExtraConfigList() {};

	/**
	 * Routine that clears the config list.
	 */
	virtual void ClearConfigList();

	/**
	 * This function should call back to the Scanner in charge of this Config,
	 *  to find the ScriptInfo belonging to a name+version.
	 */
	virtual ScriptInfo *FindInfo(const char *name, int version, bool force_exact_match) = 0;
};

#endif /* SCRIPT_CONFIG_HPP */
