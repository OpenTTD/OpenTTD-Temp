/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_config.cpp Implementation of ScriptConfig. */

#include "../stdafx.h"
#include "../settings_type.h"
#include "../core/random_func.hpp"
#include "script_info.hpp"
#include "../textfile_gui.h"
#include "../string_func.h"

#include "../safeguards.h"

void ScriptConfig::Change(const char *name, int version, bool force_exact_match, bool is_random)
{
	free(this->name);
	this->name = (name == nullptr) ? nullptr : stredup(name);
	this->info = (name == nullptr) ? nullptr : this->FindInfo(this->name, version, force_exact_match);
	this->version = (info == nullptr) ? -1 : info->GetVersion();
	this->is_random = is_random;
	if (this->config_list != nullptr) delete this->config_list;
	this->config_list = (info == nullptr) ? nullptr : new ScriptConfigItemList();
	if (this->config_list != nullptr) this->PushExtraConfigList();

	this->ClearConfigList();

	if (_game_mode == GM_NORMAL && this->info != nullptr) {
		/* If we're in an existing game and the Script is changed, set all settings
		 *  for the Script that have the random flag to a random value. */
		for (const auto &item : *this->info->GetConfigList()) {
			if (item.flags & SCRIPTCONFIG_RANDOM) {
				this->SetSetting(item.name, InteractiveRandomRange(item.max_value + 1 - item.min_value) + item.min_value);
			}
		}
		this->AddRandomDeviation();
	}
}

ScriptConfig::ScriptConfig(const ScriptConfig *config)
{
	this->name = (config->name == nullptr) ? nullptr : stredup(config->name);
	this->info = config->info;
	this->version = config->version;
	this->config_list = nullptr;
	this->is_random = config->is_random;

	for (const auto &item : config->settings) {
		this->settings[stredup(item.first)] = item.second;
	}
	this->AddRandomDeviation();
}

ScriptConfig::~ScriptConfig()
{
	free(this->name);
	this->ResetSettings();
	if (this->config_list != nullptr) delete this->config_list;
}

ScriptInfo *ScriptConfig::GetInfo() const
{
	return this->info;
}

const ScriptConfigItemList *ScriptConfig::GetConfigList()
{
	if (this->info != nullptr) return this->info->GetConfigList();
	if (this->config_list == nullptr) {
		this->config_list = new ScriptConfigItemList();
		this->PushExtraConfigList();
	}
	return this->config_list;
}

void ScriptConfig::ClearConfigList()
{
	for (const auto &item : this->settings) {
		free(item.first);
	}
	this->settings.clear();
}

void ScriptConfig::AnchorUnchangeableSettings()
{
	for (const auto &item : *this->GetConfigList()) {
		if ((item.flags & SCRIPTCONFIG_INGAME) == 0) {
			this->SetSetting(item.name, this->GetSetting(item.name));
		}
	}
}

int ScriptConfig::GetSetting(const char *name) const
{
	const auto it = this->settings.find(name);
	if (it == this->settings.end()) return this->info->GetSettingDefaultValue(name);
	return (*it).second;
}

void ScriptConfig::SetSetting(const char *name, int value)
{
	/* You can only set Script specific settings if an Script is selected. */
	if (this->info == nullptr) return;

	const ScriptConfigItem *config_item = this->info->GetConfigItem(name);
	if (config_item == nullptr) return;

	value = Clamp(value, config_item->min_value, config_item->max_value);

	const auto it = this->settings.find(name);
	if (it != this->settings.end()) {
		(*it).second = value;
	} else {
		this->settings[stredup(name)] = value;
	}
}

void ScriptConfig::ResetSettings()
{
	for (const auto &item : this->settings) {
		free(item.first);
	}
	this->settings.clear();
}

void ScriptConfig::ResetEditableSettings(bool yet_to_start)
{
	if (this->info == nullptr) return ResetSettings();

	for (SettingValueList::iterator it = this->settings.begin(); it != this->settings.end();) {
		const ScriptConfigItem *config_item = this->info->GetConfigItem(it->first);
		assert(config_item != nullptr);

		bool editable = yet_to_start || (config_item->flags & SCRIPTCONFIG_INGAME) != 0;
		bool visible = _settings_client.gui.ai_developer_tools || (config_item->flags & SCRIPTCONFIG_DEVELOPER) == 0;

		if (editable && visible) {
			free(it->first);
			it = this->settings.erase(it);
		} else {
			it++;
		}
	}
}

void ScriptConfig::AddRandomDeviation()
{
	for (const auto &item : *this->GetConfigList()) {
		if (item.random_deviation != 0) {
			this->SetSetting(item.name, InteractiveRandomRange(item.random_deviation * 2 + 1) - item.random_deviation + this->GetSetting(item.name));
		}
	}
}

bool ScriptConfig::HasScript() const
{
	return this->info != nullptr;
}

bool ScriptConfig::IsRandom() const
{
	return this->is_random;
}

const char *ScriptConfig::GetName() const
{
	return this->name;
}

int ScriptConfig::GetVersion() const
{
	return this->version;
}

void ScriptConfig::StringToSettings(const char *value)
{
	char *value_copy = stredup(value);
	char *s = value_copy;

	while (s != nullptr) {
		/* Analyze the string ('name=value,name=value\0') */
		char *item_name = s;
		s = strchr(s, '=');
		if (s == nullptr) break;
		if (*s == '\0') break;
		*s = '\0';
		s++;

		char *item_value = s;
		s = strchr(s, ',');
		if (s != nullptr) {
			*s = '\0';
			s++;
		}

		this->SetSetting(item_name, atoi(item_value));
	}
	free(value_copy);
}

void ScriptConfig::SettingsToString(char *string, const char *last) const
{
	char *s = string;
	*s = '\0';
	for (const auto &item : this->settings) {
		char no[10];
		seprintf(no, lastof(no), "%d", item.second);

		/* Check if the string would fit in the destination */
		size_t needed_size = strlen(item.first) + 1 + strlen(no);
		/* If it doesn't fit, skip the next settings */
		if (string + needed_size > last) break;

		s = strecat(s, item.first, last);
		s = strecat(s, "=", last);
		s = strecat(s, no, last);
		s = strecat(s, ",", last);
	}

	/* Remove the last ',', but only if at least one setting was saved. */
	if (s != string) s[-1] = '\0';
}

const char *ScriptConfig::GetTextfile(TextfileType type, CompanyID slot, TextDirection *text_dir) const
{
	if (slot == INVALID_COMPANY || this->GetInfo() == nullptr) return nullptr;

	return ::GetTextfile(type, (slot == OWNER_DEITY) ? GAME_DIR : AI_DIR, this->GetInfo()->GetMainScript(), text_dir);
}
