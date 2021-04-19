/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file ai_gui.cpp %Window for configuring the AIs */

#include "../stdafx.h"
#include "../table/sprites.h"
#include "../error.h"
#include "../settings_gui.h"
#include "../querystring_gui.h"
#include "../stringfilter_type.h"
#include "../company_base.h"
#include "../company_gui.h"
#include "../strings_func.h"
#include "../window_func.h"
#include "../gfx_func.h"
#include "../command_func.h"
#include "../network/network.h"
#include "../settings_func.h"
#include "../network/network_content.h"
#include "../textfile_gui.h"
#include "../widgets/dropdown_type.h"
#include "../widgets/dropdown_func.h"
#include "../hotkeys.h"
#include "../core/geometry_func.hpp"
#include "../guitimer_func.h"

#include "ai.hpp"
#include "ai_gui.hpp"
#include "../script/api/script_log.hpp"
#include "ai_config.hpp"
#include "ai_info.hpp"
#include "ai_instance.hpp"
#include "../game/game.hpp"
#include "../game/game_config.hpp"
#include "../game/game_info.hpp"
#include "../game/game_instance.hpp"

#include "table/strings.h"

#include <vector>

#include "../safeguards.h"

static ScriptConfig *GetConfig(CompanyID slot)
{
	if (slot == OWNER_DEITY) return GameConfig::GetConfig();
	return AIConfig::GetConfig(slot);
}

/**
 * Window that let you choose an available AI.
 */
struct AIListWindow : public Window {
	const ScriptInfoList *info_list;    ///< The list of Scripts.
	int selected;                       ///< The currently selected Script.
	CompanyID slot;                     ///< The company we're selecting a new Script for.
	int line_height;                    ///< Height of a row in the matrix widget.
	Scrollbar *vscroll;                 ///< Cache of the vertical scrollbar.

	/**
	 * Constructor for the window.
	 * @param desc The description of the window.
	 * @param slot The company we're changing the AI for.
	 */
	AIListWindow(WindowDesc *desc, CompanyID slot) : Window(desc),
		slot(slot)
	{
		if (slot == OWNER_DEITY) {
			this->info_list = Game::GetUniqueInfoList();
		} else {
			this->info_list = AI::GetUniqueInfoList();
		}

		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_AIL_SCROLLBAR);
		this->FinishInitNested(); // Initializes 'this->line_height' as side effect.

		this->vscroll->SetCount((int)this->info_list->size() + 1);

		/* Try if we can find the currently selected AI */
		this->selected = -1;
		if (GetConfig(slot)->HasScript()) {
			ScriptInfo *info = GetConfig(slot)->GetInfo();
			int i = 0;
			for (const auto &item : *this->info_list) {
				if (item.second == info) {
					this->selected = i;
					break;
				}

				i++;
			}
		}
	}

	void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_AIL_CAPTION:
				SetDParam(0, (this->slot == OWNER_DEITY) ? STR_AI_LIST_CAPTION_GAMESCRIPT : STR_AI_LIST_CAPTION_AI);
				break;
		}
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		if (widget == WID_AIL_LIST) {
			this->line_height = FONT_HEIGHT_NORMAL + WD_MATRIX_TOP + WD_MATRIX_BOTTOM;

			resize->width = 1;
			resize->height = this->line_height;
			size->height = 5 * this->line_height;
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_AIL_LIST: {
				/* Draw a list of all available AIs. */
				int y = this->GetWidget<NWidgetBase>(WID_AIL_LIST)->pos_y;
				/* First AI in the list is hardcoded to random */
				if (this->vscroll->IsVisible(0)) {
					DrawString(r.left + WD_MATRIX_LEFT, r.right - WD_MATRIX_LEFT, y + WD_MATRIX_TOP, this->slot == OWNER_DEITY ? STR_AI_CONFIG_NONE : STR_AI_CONFIG_RANDOM_AI, this->selected == -1 ? TC_WHITE : TC_ORANGE);
					y += this->line_height;
				}
				int i = 0;
				for (const auto &item : *this->info_list) {
					i++;
					if (this->vscroll->IsVisible(i)) {
						DrawString(r.left + WD_MATRIX_LEFT, r.right - WD_MATRIX_RIGHT, y + WD_MATRIX_TOP, item.second->GetName(), (this->selected == i - 1) ? TC_WHITE : TC_ORANGE);
						y += this->line_height;
					}
				}
				break;
			}
			case WID_AIL_INFO_BG: {
				AIInfo *selected_info = nullptr;
				int i = 0;
				for (const auto &item : *this->info_list) {
					i++;
					if (this->selected == i - 1) selected_info = static_cast<AIInfo *>(item.second);
				}
				/* Some info about the currently selected AI. */
				if (selected_info != nullptr) {
					int y = r.top + WD_FRAMERECT_TOP;
					SetDParamStr(0, selected_info->GetAuthor());
					DrawString(r.left + WD_FRAMETEXT_LEFT, r.right - WD_FRAMETEXT_RIGHT, y, STR_AI_LIST_AUTHOR);
					y += FONT_HEIGHT_NORMAL + WD_PAR_VSEP_NORMAL;
					SetDParam(0, selected_info->GetVersion());
					DrawString(r.left + WD_FRAMETEXT_LEFT, r.right - WD_FRAMETEXT_RIGHT, y, STR_AI_LIST_VERSION);
					y += FONT_HEIGHT_NORMAL + WD_PAR_VSEP_NORMAL;
					if (selected_info->GetURL() != nullptr) {
						SetDParamStr(0, selected_info->GetURL());
						DrawString(r.left + WD_FRAMETEXT_LEFT, r.right - WD_FRAMETEXT_RIGHT, y, STR_AI_LIST_URL);
						y += FONT_HEIGHT_NORMAL + WD_PAR_VSEP_NORMAL;
					}
					SetDParamStr(0, selected_info->GetDescription());
					DrawStringMultiLine(r.left + WD_FRAMETEXT_LEFT, r.right - WD_FRAMETEXT_RIGHT, y, r.bottom - WD_FRAMERECT_BOTTOM, STR_JUST_RAW_STRING, TC_WHITE);
				}
				break;
			}
		}
	}

	/**
	 * Changes the AI of the current slot.
	 */
	void ChangeAI()
	{
		if (this->selected == -1) {
			GetConfig(slot)->Change(nullptr);
		} else {
			ScriptInfoList::const_iterator it = this->info_list->begin();
			for (int i = 0; i < this->selected; i++) it++;
			GetConfig(slot)->Change((*it).second->GetName(), (*it).second->GetVersion());
		}
		InvalidateWindowData(WC_GAME_OPTIONS, WN_GAME_OPTIONS_AI);
		InvalidateWindowClassesData(WC_AI_SETTINGS);
		DeleteWindowByClass(WC_QUERY_STRING);
		InvalidateWindowClassesData(WC_TEXTFILE);
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_AIL_LIST: { // Select one of the AIs
				int sel = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_AIL_LIST, 0, this->line_height) - 1;
				if (sel < (int)this->info_list->size()) {
					this->selected = sel;
					this->SetDirty();
					if (click_count > 1) {
						this->ChangeAI();
						delete this;
					}
				}
				break;
			}

			case WID_AIL_ACCEPT: {
				this->ChangeAI();
				delete this;
				break;
			}

			case WID_AIL_CANCEL:
				delete this;
				break;
		}
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_AIL_LIST);
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (_game_mode == GM_NORMAL && Company::IsValidID(this->slot)) {
			delete this;
			return;
		}

		if (!gui_scope) return;

		this->vscroll->SetCount((int)this->info_list->size() + 1);

		/* selected goes from -1 .. length of ai list - 1. */
		this->selected = std::min(this->selected, this->vscroll->GetCount() - 2);
	}
};

/** Widgets for the AI list window. */
static const NWidgetPart _nested_ai_list_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_MAUVE),
		NWidget(WWT_CAPTION, COLOUR_MAUVE, WID_AIL_CAPTION), SetDataTip(STR_AI_LIST_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_DEFSIZEBOX, COLOUR_MAUVE),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_MATRIX, COLOUR_MAUVE, WID_AIL_LIST), SetMinimalSize(188, 112), SetFill(1, 1), SetResize(1, 1), SetMatrixDataTip(1, 0, STR_AI_LIST_TOOLTIP), SetScrollbar(WID_AIL_SCROLLBAR),
		NWidget(NWID_VSCROLLBAR, COLOUR_MAUVE, WID_AIL_SCROLLBAR),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_MAUVE, WID_AIL_INFO_BG), SetMinimalTextLines(8, WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM), SetResize(1, 0),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
			NWidget(WWT_PUSHTXTBTN, COLOUR_MAUVE, WID_AIL_ACCEPT), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_AI_LIST_ACCEPT, STR_AI_LIST_ACCEPT_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_MAUVE, WID_AIL_CANCEL), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_AI_LIST_CANCEL, STR_AI_LIST_CANCEL_TOOLTIP),
		EndContainer(),
		NWidget(WWT_RESIZEBOX, COLOUR_MAUVE),
	EndContainer(),
};

/** Window definition for the ai list window. */
static WindowDesc _ai_list_desc(
	WDP_CENTER, "settings_script_list", 200, 234,
	WC_AI_LIST, WC_NONE,
	0,
	_nested_ai_list_widgets, lengthof(_nested_ai_list_widgets)
);

/**
 * Open the AI list window to chose an AI for the given company slot.
 * @param slot The slot to change the AI of.
 */
static void ShowAIListWindow(CompanyID slot)
{
	DeleteWindowByClass(WC_AI_LIST);
	new AIListWindow(&_ai_list_desc, slot);
}

/**
 * Window for settings the parameters of an AI.
 */
struct AISettingsWindow : public Window {
	CompanyID slot;                       ///< The currently show company's setting.
	ScriptConfig *ai_config;              ///< The configuration we're modifying.
	int clicked_button;                   ///< The button we clicked.
	bool clicked_increase;                ///< Whether we clicked the increase or decrease button.
	bool clicked_dropdown;                ///< Whether the dropdown is open.
	bool closing_dropdown;                ///< True, if the dropdown list is currently closing.
	GUITimer timeout;                     ///< Timeout for unclicking the button.
	int clicked_row;                      ///< The clicked row of settings.
	int line_height;                      ///< Height of a row in the matrix widget.
	Scrollbar *vscroll;                   ///< Cache of the vertical scrollbar.
	typedef std::vector<const ScriptConfigItem *> VisibleSettingsList;
	VisibleSettingsList visible_settings; ///< List of visible AI settings

	/**
	 * Constructor for the window.
	 * @param desc The description of the window.
	 * @param slot The company we're changing the settings for.
	 */
	AISettingsWindow(WindowDesc *desc, CompanyID slot) : Window(desc),
		slot(slot),
		clicked_button(-1),
		clicked_dropdown(false),
		closing_dropdown(false),
		timeout(0)
	{
		this->ai_config = GetConfig(slot);

		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_AIS_SCROLLBAR);
		this->FinishInitNested(slot);  // Initializes 'this->line_height' as side effect.

		this->RebuildVisibleSettings();
	}

	void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_AIS_CAPTION:
				SetDParam(0, (this->slot == OWNER_DEITY) ? STR_AI_SETTINGS_CAPTION_GAMESCRIPT : STR_AI_SETTINGS_CAPTION_AI);
				break;
		}
	}

	/**
	 * Rebuilds the list of visible settings. AI settings with the flag
	 * AICONFIG_AI_DEVELOPER set will only be visible if the game setting
	 * gui.ai_developer_tools is enabled.
	 */
	void RebuildVisibleSettings()
	{
		visible_settings.clear();

		for (const auto &item : *this->ai_config->GetConfigList()) {
			bool no_hide = (item.flags & SCRIPTCONFIG_DEVELOPER) == 0;
			if (no_hide || _settings_client.gui.ai_developer_tools) {
				visible_settings.push_back(&item);
			}
		}

		this->vscroll->SetCount((int)this->visible_settings.size());
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		if (widget == WID_AIS_BACKGROUND) {
			this->line_height = std::max(SETTING_BUTTON_HEIGHT, FONT_HEIGHT_NORMAL) + WD_MATRIX_TOP + WD_MATRIX_BOTTOM;

			resize->width = 1;
			resize->height = this->line_height;
			size->height = 5 * this->line_height;
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		if (widget != WID_AIS_BACKGROUND) return;

		ScriptConfig *config = this->ai_config;
		VisibleSettingsList::const_iterator it = this->visible_settings.begin();
		int i = 0;
		for (; !this->vscroll->IsVisible(i); i++) it++;

		bool rtl = _current_text_dir == TD_RTL;
		uint buttons_left = rtl ? r.right - SETTING_BUTTON_WIDTH - 3 : r.left + 4;
		uint text_left    = r.left + (rtl ? WD_FRAMERECT_LEFT : SETTING_BUTTON_WIDTH + 8);
		uint text_right   = r.right - (rtl ? SETTING_BUTTON_WIDTH + 8 : WD_FRAMERECT_RIGHT);


		int y = r.top;
		int button_y_offset = (this->line_height - SETTING_BUTTON_HEIGHT) / 2;
		int text_y_offset = (this->line_height - FONT_HEIGHT_NORMAL) / 2;
		for (; this->vscroll->IsVisible(i) && it != visible_settings.end(); i++, it++) {
			const ScriptConfigItem &config_item = **it;
			int current_value = config->GetSetting((config_item).name);
			bool editable = this->IsEditableItem(config_item);

			StringID str;
			TextColour colour;
			uint idx = 0;
			if (StrEmpty(config_item.description)) {
				if (!strcmp(config_item.name, "start_date")) {
					/* Build-in translation */
					str = STR_AI_SETTINGS_START_DELAY;
					colour = TC_LIGHT_BLUE;
				} else {
					str = STR_JUST_STRING;
					colour = TC_ORANGE;
				}
			} else {
				str = STR_AI_SETTINGS_SETTING;
				colour = TC_LIGHT_BLUE;
				SetDParamStr(idx++, config_item.description);
			}

			if ((config_item.flags & SCRIPTCONFIG_BOOLEAN) != 0) {
				DrawBoolButton(buttons_left, y + button_y_offset, current_value != 0, editable);
				SetDParam(idx++, current_value == 0 ? STR_CONFIG_SETTING_OFF : STR_CONFIG_SETTING_ON);
			} else {
				if (config_item.complete_labels) {
					DrawDropDownButton(buttons_left, y + button_y_offset, COLOUR_YELLOW, this->clicked_row == i && clicked_dropdown, editable);
				} else {
					DrawArrowButtons(buttons_left, y + button_y_offset, COLOUR_YELLOW, (this->clicked_button == i) ? 1 + (this->clicked_increase != rtl) : 0, editable && current_value > config_item.min_value, editable && current_value < config_item.max_value);
				}
				if (config_item.labels != nullptr && config_item.labels->Contains(current_value)) {
					SetDParam(idx++, STR_JUST_RAW_STRING);
					SetDParamStr(idx++, config_item.labels->Find(current_value)->second);
				} else {
					SetDParam(idx++, STR_JUST_INT);
					SetDParam(idx++, current_value);
				}
			}

			DrawString(text_left, text_right, y + text_y_offset, str, colour);
			y += this->line_height;
		}
	}

	void OnPaint() override
	{
		if (this->closing_dropdown) {
			this->closing_dropdown = false;
			this->clicked_dropdown = false;
		}
		this->DrawWidgets();
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_AIS_BACKGROUND: {
				const NWidgetBase *wid = this->GetWidget<NWidgetBase>(WID_AIS_BACKGROUND);
				int num = (pt.y - wid->pos_y) / this->line_height + this->vscroll->GetPosition();
				if (num >= (int)this->visible_settings.size()) break;

				VisibleSettingsList::const_iterator it = this->visible_settings.begin();
				for (int i = 0; i < num; i++) it++;
				const ScriptConfigItem config_item = **it;
				if (!this->IsEditableItem(config_item)) return;

				if (this->clicked_row != num) {
					DeleteChildWindows(WC_QUERY_STRING);
					HideDropDownMenu(this);
					this->clicked_row = num;
					this->clicked_dropdown = false;
				}

				bool bool_item = (config_item.flags & SCRIPTCONFIG_BOOLEAN) != 0;

				int x = pt.x - wid->pos_x;
				if (_current_text_dir == TD_RTL) x = wid->current_x - 1 - x;
				x -= 4;

				/* One of the arrows is clicked (or green/red rect in case of bool value) */
				int old_val = this->ai_config->GetSetting(config_item.name);
				if (!bool_item && IsInsideMM(x, 0, SETTING_BUTTON_WIDTH) && config_item.complete_labels) {
					if (this->clicked_dropdown) {
						/* unclick the dropdown */
						HideDropDownMenu(this);
						this->clicked_dropdown = false;
						this->closing_dropdown = false;
					} else {
						const NWidgetBase *wid = this->GetWidget<NWidgetBase>(WID_AIS_BACKGROUND);
						int rel_y = (pt.y - (int)wid->pos_y) % this->line_height;

						Rect wi_rect;
						wi_rect.left = pt.x - (_current_text_dir == TD_RTL ? SETTING_BUTTON_WIDTH - 1 - x : x);
						wi_rect.right = wi_rect.left + SETTING_BUTTON_WIDTH - 1;
						wi_rect.top = pt.y - rel_y + (this->line_height - SETTING_BUTTON_HEIGHT) / 2;
						wi_rect.bottom = wi_rect.top + SETTING_BUTTON_HEIGHT - 1;

						/* For dropdowns we also have to check the y position thoroughly, the mouse may not above the just opening dropdown */
						if (pt.y >= wi_rect.top && pt.y <= wi_rect.bottom) {
							this->clicked_dropdown = true;
							this->closing_dropdown = false;

							DropDownList list;
							for (int i = config_item.min_value; i <= config_item.max_value; i++) {
								list.emplace_back(new DropDownListCharStringItem(config_item.labels->Find(i)->second, i, false));
							}

							ShowDropDownListAt(this, std::move(list), old_val, -1, wi_rect, COLOUR_ORANGE, true);
						}
					}
				} else if (IsInsideMM(x, 0, SETTING_BUTTON_WIDTH)) {
					int new_val = old_val;
					if (bool_item) {
						new_val = !new_val;
					} else if (x >= SETTING_BUTTON_WIDTH / 2) {
						/* Increase button clicked */
						new_val += config_item.step_size;
						if (new_val > config_item.max_value) new_val = config_item.max_value;
						this->clicked_increase = true;
					} else {
						/* Decrease button clicked */
						new_val -= config_item.step_size;
						if (new_val < config_item.min_value) new_val = config_item.min_value;
						this->clicked_increase = false;
					}

					if (new_val != old_val) {
						this->ai_config->SetSetting(config_item.name, new_val);
						this->clicked_button = num;
						this->timeout.SetInterval(150);
					}
				} else if (!bool_item && !config_item.complete_labels) {
					/* Display a query box so users can enter a custom value. */
					SetDParam(0, old_val);
					ShowQueryString(STR_JUST_INT, STR_CONFIG_SETTING_QUERY_CAPTION, 10, this, CS_NUMERAL, QSF_NONE);
				}
				this->SetDirty();
				break;
			}

			case WID_AIS_ACCEPT:
				delete this;
				break;

			case WID_AIS_RESET:
				this->ai_config->ResetEditableSettings(_game_mode == GM_MENU || ((this->slot != OWNER_DEITY) && !Company::IsValidID(this->slot)));
				this->SetDirty();
				break;
		}
	}

	void OnQueryTextFinished(char *str) override
	{
		if (StrEmpty(str)) return;
		VisibleSettingsList::const_iterator it = this->visible_settings.begin();
		for (int i = 0; i < this->clicked_row; i++) it++;
		const ScriptConfigItem config_item = **it;
		if (_game_mode == GM_NORMAL && ((this->slot == OWNER_DEITY) || Company::IsValidID(this->slot)) && (config_item.flags & SCRIPTCONFIG_INGAME) == 0) return;
		int32 value = atoi(str);
		this->ai_config->SetSetting(config_item.name, value);
		this->SetDirty();
	}

	void OnDropdownSelect(int widget, int index) override
	{
		assert(this->clicked_dropdown);
		VisibleSettingsList::const_iterator it = this->visible_settings.begin();
		for (int i = 0; i < this->clicked_row; i++) it++;
		const ScriptConfigItem config_item = **it;
		if (_game_mode == GM_NORMAL && ((this->slot == OWNER_DEITY) || Company::IsValidID(this->slot)) && (config_item.flags & SCRIPTCONFIG_INGAME) == 0) return;
		this->ai_config->SetSetting(config_item.name, index);
		this->SetDirty();
	}

	void OnDropdownClose(Point pt, int widget, int index, bool instant_close) override
	{
		/* We cannot raise the dropdown button just yet. OnClick needs some hint, whether
		 * the same dropdown button was clicked again, and then not open the dropdown again.
		 * So, we only remember that it was closed, and process it on the next OnPaint, which is
		 * after OnClick. */
		assert(this->clicked_dropdown);
		this->closing_dropdown = true;
		this->SetDirty();
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_AIS_BACKGROUND);
	}

	void OnRealtimeTick(uint delta_ms) override
	{
		if (this->timeout.Elapsed(delta_ms)) {
			this->clicked_button = -1;
			this->SetDirty();
		}
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		this->RebuildVisibleSettings();
		HideDropDownMenu(this);
		DeleteChildWindows(WC_QUERY_STRING);
	}

private:
	bool IsEditableItem(const ScriptConfigItem &config_item) const
	{
		return _game_mode == GM_MENU || ((this->slot != OWNER_DEITY) && !Company::IsValidID(this->slot)) || (config_item.flags & SCRIPTCONFIG_INGAME) != 0;
	}
};

/** Widgets for the AI settings window. */
static const NWidgetPart _nested_ai_settings_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_MAUVE),
		NWidget(WWT_CAPTION, COLOUR_MAUVE, WID_AIS_CAPTION), SetDataTip(STR_AI_SETTINGS_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_DEFSIZEBOX, COLOUR_MAUVE),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_MATRIX, COLOUR_MAUVE, WID_AIS_BACKGROUND), SetMinimalSize(188, 182), SetResize(1, 1), SetFill(1, 0), SetMatrixDataTip(1, 0, STR_NULL), SetScrollbar(WID_AIS_SCROLLBAR),
		NWidget(NWID_VSCROLLBAR, COLOUR_MAUVE, WID_AIS_SCROLLBAR),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
			NWidget(WWT_PUSHTXTBTN, COLOUR_MAUVE, WID_AIS_ACCEPT), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_AI_SETTINGS_CLOSE, STR_NULL),
			NWidget(WWT_PUSHTXTBTN, COLOUR_MAUVE, WID_AIS_RESET), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_AI_SETTINGS_RESET, STR_NULL),
		EndContainer(),
		NWidget(WWT_RESIZEBOX, COLOUR_MAUVE),
	EndContainer(),
};

/** Window definition for the AI settings window. */
static WindowDesc _ai_settings_desc(
	WDP_CENTER, "settings_script", 500, 208,
	WC_AI_SETTINGS, WC_NONE,
	0,
	_nested_ai_settings_widgets, lengthof(_nested_ai_settings_widgets)
);

/**
 * Open the AI settings window to change the AI settings for an AI.
 * @param slot The CompanyID of the AI to change the settings.
 */
static void ShowAISettingsWindow(CompanyID slot)
{
	DeleteWindowByClass(WC_AI_LIST);
	DeleteWindowByClass(WC_AI_SETTINGS);
	new AISettingsWindow(&_ai_settings_desc, slot);
}


/** Window for displaying the textfile of a AI. */
struct ScriptTextfileWindow : public TextfileWindow {
	CompanyID slot; ///< View the textfile of this CompanyID slot.

	ScriptTextfileWindow(TextfileType file_type, CompanyID slot) : TextfileWindow(file_type), slot(slot)
	{
		this->OnInvalidateData();
	}

	void SetStringParameters(int widget) const override
	{
		if (widget == WID_TF_CAPTION) {
			SetDParam(0, (slot == OWNER_DEITY) ? STR_CONTENT_TYPE_GAME_SCRIPT : STR_CONTENT_TYPE_AI);
			SetDParamStr(1, GetConfig(slot)->GetInfo()->GetName());
		}
	}

	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		const char *textfile = GetConfig(slot)->GetTextfile(file_type, slot, &this->text_direction);
		if (textfile == nullptr) {
			delete this;
		} else {
			this->LoadTextfile(textfile, (slot == OWNER_DEITY) ? GAME_DIR : AI_DIR);
		}
	}
};

/**
 * Open the AI version of the textfile window.
 * @param file_type The type of textfile to display.
 * @param slot The slot the Script is using.
 */
void ShowScriptTextfileWindow(TextfileType file_type, CompanyID slot)
{
	DeleteWindowById(WC_TEXTFILE, file_type);
	new ScriptTextfileWindow(file_type, slot);
}


/** Widgets for the configure AI window. */
static const NWidgetPart _nested_ai_config_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_MAUVE),
		NWidget(WWT_CAPTION, COLOUR_MAUVE), SetDataTip(STR_AI_CONFIG_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_MAUVE, WID_AIC_BACKGROUND),
		NWidget(NWID_VERTICAL), SetPIP(4, 4, 4),
			NWidget(NWID_HORIZONTAL), SetPIP(7, 0, 7),
				NWidget(WWT_PUSHARROWBTN, COLOUR_YELLOW, WID_AIC_DECREASE), SetFill(0, 1), SetDataTip(AWV_DECREASE, STR_NULL),
				NWidget(WWT_PUSHARROWBTN, COLOUR_YELLOW, WID_AIC_INCREASE), SetFill(0, 1), SetDataTip(AWV_INCREASE, STR_NULL),
				NWidget(NWID_SPACER), SetMinimalSize(6, 0),
				NWidget(WWT_TEXT, COLOUR_MAUVE, WID_AIC_NUMBER), SetDataTip(STR_DIFFICULTY_LEVEL_SETTING_MAXIMUM_NO_COMPETITORS, STR_NULL), SetFill(1, 0), SetPadding(1, 0, 0, 0),
			EndContainer(),
			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(7, 0, 7),
				NWidget(WWT_PUSHTXTBTN, COLOUR_YELLOW, WID_AIC_MOVE_UP), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_AI_CONFIG_MOVE_UP, STR_AI_CONFIG_MOVE_UP_TOOLTIP),
				NWidget(WWT_PUSHTXTBTN, COLOUR_YELLOW, WID_AIC_MOVE_DOWN), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_AI_CONFIG_MOVE_DOWN, STR_AI_CONFIG_MOVE_DOWN_TOOLTIP),
			EndContainer(),
		EndContainer(),
		NWidget(WWT_FRAME, COLOUR_MAUVE), SetDataTip(STR_AI_CONFIG_AI, STR_NULL), SetPadding(0, 5, 0, 5),
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_MATRIX, COLOUR_MAUVE, WID_AIC_LIST), SetMinimalSize(288, 112), SetFill(1, 0), SetMatrixDataTip(1, 8, STR_AI_CONFIG_AILIST_TOOLTIP), SetScrollbar(WID_AIC_SCROLLBAR),
				NWidget(NWID_VSCROLLBAR, COLOUR_MAUVE, WID_AIC_SCROLLBAR),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 9),
		NWidget(WWT_FRAME, COLOUR_MAUVE), SetDataTip(STR_AI_CONFIG_GAMESCRIPT, STR_NULL), SetPadding(0, 5, 4, 5),
			NWidget(WWT_MATRIX, COLOUR_MAUVE, WID_AIC_GAMELIST), SetMinimalSize(288, 14), SetFill(1, 0), SetMatrixDataTip(1, 1, STR_AI_CONFIG_GAMELIST_TOOLTIP),
		EndContainer(),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(7, 0, 7),
			NWidget(WWT_PUSHTXTBTN, COLOUR_YELLOW, WID_AIC_CHANGE), SetFill(1, 0), SetMinimalSize(93, 0), SetDataTip(STR_AI_CONFIG_CHANGE, STR_AI_CONFIG_CHANGE_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_YELLOW, WID_AIC_CONFIGURE), SetFill(1, 0), SetMinimalSize(93, 0), SetDataTip(STR_AI_CONFIG_CONFIGURE, STR_AI_CONFIG_CONFIGURE_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_YELLOW, WID_AIC_CLOSE), SetFill(1, 0), SetMinimalSize(93, 0), SetDataTip(STR_AI_SETTINGS_CLOSE, STR_NULL),
		EndContainer(),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(7, 0, 7),
			NWidget(WWT_PUSHTXTBTN, COLOUR_YELLOW, WID_AIC_TEXTFILE + TFT_README), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_TEXTFILE_VIEW_README, STR_NULL),
			NWidget(WWT_PUSHTXTBTN, COLOUR_YELLOW, WID_AIC_TEXTFILE + TFT_CHANGELOG), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_TEXTFILE_VIEW_CHANGELOG, STR_NULL),
			NWidget(WWT_PUSHTXTBTN, COLOUR_YELLOW, WID_AIC_TEXTFILE + TFT_LICENSE), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_TEXTFILE_VIEW_LICENCE, STR_NULL),
		EndContainer(),
		NWidget(WWT_PUSHTXTBTN, COLOUR_YELLOW, WID_AIC_CONTENT_DOWNLOAD), SetFill(1, 0), SetMinimalSize(279, 0), SetPadding(0, 7, 9, 7), SetDataTip(STR_INTRO_ONLINE_CONTENT, STR_INTRO_TOOLTIP_ONLINE_CONTENT),
	EndContainer(),
};

/** Window definition for the configure AI window. */
static WindowDesc _ai_config_desc(
	WDP_CENTER, "settings_script_config", 0, 0,
	WC_GAME_OPTIONS, WC_NONE,
	0,
	_nested_ai_config_widgets, lengthof(_nested_ai_config_widgets)
);

/**
 * Window to configure which AIs will start.
 */
struct AIConfigWindow : public Window {
	CompanyID selected_slot; ///< The currently selected AI slot or \c INVALID_COMPANY.
	int line_height;         ///< Height of a single AI-name line.
	Scrollbar *vscroll;      ///< Cache of the vertical scrollbar.

	AIConfigWindow() : Window(&_ai_config_desc)
	{
		this->InitNested(WN_GAME_OPTIONS_AI); // Initializes 'this->line_height' as a side effect.
		this->vscroll = this->GetScrollbar(WID_AIC_SCROLLBAR);
		this->selected_slot = INVALID_COMPANY;
		NWidgetCore *nwi = this->GetWidget<NWidgetCore>(WID_AIC_LIST);
		this->vscroll->SetCapacity(nwi->current_y / this->line_height);
		this->vscroll->SetCount(MAX_COMPANIES);
		this->OnInvalidateData(0);
	}

	~AIConfigWindow()
	{
		DeleteWindowByClass(WC_AI_LIST);
		DeleteWindowByClass(WC_AI_SETTINGS);
	}

	void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_AIC_NUMBER:
				SetDParam(0, GetGameSettings().difficulty.max_no_competitors);
				break;
			case WID_AIC_CHANGE:
				switch (selected_slot) {
					case OWNER_DEITY:
						SetDParam(0, STR_AI_CONFIG_CHANGE_GAMESCRIPT);
						break;

					case INVALID_COMPANY:
						SetDParam(0, STR_AI_CONFIG_CHANGE_NONE);
						break;

					default:
						SetDParam(0, STR_AI_CONFIG_CHANGE_AI);
						break;
				}
				break;
		}
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_AIC_GAMELIST:
				this->line_height = FONT_HEIGHT_NORMAL + WD_MATRIX_TOP + WD_MATRIX_BOTTOM;
				size->height = 1 * this->line_height;
				break;

			case WID_AIC_LIST:
				this->line_height = FONT_HEIGHT_NORMAL + WD_MATRIX_TOP + WD_MATRIX_BOTTOM;
				size->height = 8 * this->line_height;
				break;

			case WID_AIC_CHANGE: {
				SetDParam(0, STR_AI_CONFIG_CHANGE_GAMESCRIPT);
				Dimension dim = GetStringBoundingBox(STR_AI_CONFIG_CHANGE);

				SetDParam(0, STR_AI_CONFIG_CHANGE_NONE);
				dim = maxdim(dim, GetStringBoundingBox(STR_AI_CONFIG_CHANGE));

				SetDParam(0, STR_AI_CONFIG_CHANGE_AI);
				dim = maxdim(dim, GetStringBoundingBox(STR_AI_CONFIG_CHANGE));

				dim.width += padding.width;
				dim.height += padding.height;
				*size = maxdim(*size, dim);
				break;
			}
		}
	}

	/**
	 * Can the AI config in the given company slot be edited?
	 * @param slot The slot to query.
	 * @return True if and only if the given AI Config slot can e edited.
	 */
	static bool IsEditable(CompanyID slot)
	{
		if (slot == OWNER_DEITY) return _game_mode != GM_NORMAL || Game::GetInstance() != nullptr;

		if (_game_mode != GM_NORMAL) {
			return slot > 0 && slot <= GetGameSettings().difficulty.max_no_competitors;
		}
		if (Company::IsValidID(slot)) return false;

		int max_slot = GetGameSettings().difficulty.max_no_competitors;
		for (CompanyID cid = COMPANY_FIRST; cid < (CompanyID)max_slot && cid < MAX_COMPANIES; cid++) {
			if (Company::IsValidHumanID(cid)) max_slot++;
		}
		return slot < max_slot;
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_AIC_GAMELIST: {
				StringID text = STR_AI_CONFIG_NONE;

				if (GameConfig::GetConfig()->GetInfo() != nullptr) {
					SetDParamStr(0, GameConfig::GetConfig()->GetInfo()->GetName());
					text = STR_JUST_RAW_STRING;
				}

				DrawString(r.left + 10, r.right - 10, r.top + WD_MATRIX_TOP, text,
						(this->selected_slot == OWNER_DEITY) ? TC_WHITE : (IsEditable(OWNER_DEITY) ? TC_ORANGE : TC_SILVER));

				break;
			}

			case WID_AIC_LIST: {
				int y = r.top;
				for (int i = this->vscroll->GetPosition(); this->vscroll->IsVisible(i) && i < MAX_COMPANIES; i++) {
					StringID text;

					if ((_game_mode != GM_NORMAL && i == 0) || (_game_mode == GM_NORMAL && Company::IsValidHumanID(i))) {
						text = STR_AI_CONFIG_HUMAN_PLAYER;
					} else if (AIConfig::GetConfig((CompanyID)i)->GetInfo() != nullptr) {
						SetDParamStr(0, AIConfig::GetConfig((CompanyID)i)->GetInfo()->GetName());
						text = STR_JUST_RAW_STRING;
					} else {
						text = STR_AI_CONFIG_RANDOM_AI;
					}
					DrawString(r.left + 10, r.right - 10, y + WD_MATRIX_TOP, text,
							(this->selected_slot == i) ? TC_WHITE : (IsEditable((CompanyID)i) ? TC_ORANGE : TC_SILVER));
					y += this->line_height;
				}
				break;
			}
		}
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		if (widget >= WID_AIC_TEXTFILE && widget < WID_AIC_TEXTFILE + TFT_END) {
			if (this->selected_slot == INVALID_COMPANY || GetConfig(this->selected_slot) == nullptr) return;

			ShowScriptTextfileWindow((TextfileType)(widget - WID_AIC_TEXTFILE), this->selected_slot);
			return;
		}

		switch (widget) {
			case WID_AIC_DECREASE:
			case WID_AIC_INCREASE: {
				int new_value;
				if (widget == WID_AIC_DECREASE) {
					new_value = std::max(0, GetGameSettings().difficulty.max_no_competitors - 1);
				} else {
					new_value = std::min(MAX_COMPANIES - 1, GetGameSettings().difficulty.max_no_competitors + 1);
				}
				IConsoleSetSetting("difficulty.max_no_competitors", new_value);
				break;
			}

			case WID_AIC_GAMELIST: {
				this->selected_slot = OWNER_DEITY;
				this->InvalidateData();
				if (click_count > 1 && this->selected_slot != INVALID_COMPANY && _game_mode != GM_NORMAL) ShowAIListWindow((CompanyID)this->selected_slot);
				break;
			}

			case WID_AIC_LIST: { // Select a slot
				this->selected_slot = (CompanyID)this->vscroll->GetScrolledRowFromWidget(pt.y, this, widget, 0, this->line_height);
				this->InvalidateData();
				if (click_count > 1 && this->selected_slot != INVALID_COMPANY) ShowAIListWindow((CompanyID)this->selected_slot);
				break;
			}

			case WID_AIC_MOVE_UP:
				if (IsEditable(this->selected_slot) && IsEditable((CompanyID)(this->selected_slot - 1))) {
					Swap(GetGameSettings().ai_config[this->selected_slot], GetGameSettings().ai_config[this->selected_slot - 1]);
					this->selected_slot--;
					this->vscroll->ScrollTowards(this->selected_slot);
					this->InvalidateData();
				}
				break;

			case WID_AIC_MOVE_DOWN:
				if (IsEditable(this->selected_slot) && IsEditable((CompanyID)(this->selected_slot + 1))) {
					Swap(GetGameSettings().ai_config[this->selected_slot], GetGameSettings().ai_config[this->selected_slot + 1]);
					this->selected_slot++;
					this->vscroll->ScrollTowards(this->selected_slot);
					this->InvalidateData();
				}
				break;

			case WID_AIC_CHANGE:  // choose other AI
				ShowAIListWindow((CompanyID)this->selected_slot);
				break;

			case WID_AIC_CONFIGURE: // change the settings for an AI
				ShowAISettingsWindow((CompanyID)this->selected_slot);
				break;

			case WID_AIC_CLOSE:
				delete this;
				break;

			case WID_AIC_CONTENT_DOWNLOAD:
				if (!_network_available) {
					ShowErrorMessage(STR_NETWORK_ERROR_NOTAVAILABLE, INVALID_STRING_ID, WL_ERROR);
				} else {
					ShowNetworkContentListWindow(nullptr, CONTENT_TYPE_AI, CONTENT_TYPE_GAME);
				}
				break;
		}
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (!IsEditable(this->selected_slot)) {
			this->selected_slot = INVALID_COMPANY;
		}

		if (!gui_scope) return;

		this->SetWidgetDisabledState(WID_AIC_DECREASE, GetGameSettings().difficulty.max_no_competitors == 0);
		this->SetWidgetDisabledState(WID_AIC_INCREASE, GetGameSettings().difficulty.max_no_competitors == MAX_COMPANIES - 1);
		this->SetWidgetDisabledState(WID_AIC_CHANGE, (this->selected_slot == OWNER_DEITY && _game_mode == GM_NORMAL) || this->selected_slot == INVALID_COMPANY);
		this->SetWidgetDisabledState(WID_AIC_CONFIGURE, this->selected_slot == INVALID_COMPANY || GetConfig(this->selected_slot)->GetConfigList()->size() == 0);
		this->SetWidgetDisabledState(WID_AIC_MOVE_UP, this->selected_slot == OWNER_DEITY || this->selected_slot == INVALID_COMPANY || !IsEditable((CompanyID)(this->selected_slot - 1)));
		this->SetWidgetDisabledState(WID_AIC_MOVE_DOWN, this->selected_slot == OWNER_DEITY || this->selected_slot == INVALID_COMPANY || !IsEditable((CompanyID)(this->selected_slot + 1)));

		for (TextfileType tft = TFT_BEGIN; tft < TFT_END; tft++) {
			this->SetWidgetDisabledState(WID_AIC_TEXTFILE + tft, this->selected_slot == INVALID_COMPANY || (GetConfig(this->selected_slot)->GetTextfile(tft, this->selected_slot) == nullptr));
		}
	}
};

/** Open the AI config window. */
void ShowAIConfigWindow()
{
	DeleteWindowByClass(WC_GAME_OPTIONS);
	new AIConfigWindow();
}

/**
 * Set the widget colour of a button based on the
 * state of the script. (dead or alive)
 * @param button the button to update.
 * @param dead true if the script is dead, otherwise false.
 * @param paused true if the script is paused, otherwise false.
 * @return true if the colour was changed and the window need to be marked as dirty.
 */
static bool SetScriptButtonColour(NWidgetCore &button, bool dead, bool paused)
{
	/* Dead scripts are indicated with red background and
	 * paused scripts are indicated with yellow background. */
	Colours colour = dead ? COLOUR_RED :
			(paused ? COLOUR_YELLOW : COLOUR_GREY);
	if (button.colour != colour) {
		button.colour = colour;
		return true;
	}
	return false;
}

/**
 * Window with everything an AI prints via ScriptLog.
 */
struct AIDebugWindow : public Window {
	static const int top_offset;    ///< Offset of the text at the top of the WID_AID_LOG_PANEL.
	static const int bottom_offset; ///< Offset of the text at the bottom of the WID_AID_LOG_PANEL.

	static const uint MAX_BREAK_STR_STRING_LENGTH = 256;   ///< Maximum length of the break string.

	static CompanyID ai_debug_company;                     ///< The AI that is (was last) being debugged.
	int redraw_timer;                                      ///< Timer for redrawing the window, otherwise it'll happen every tick.
	int last_vscroll_pos;                                  ///< Last position of the scrolling.
	bool autoscroll;                                       ///< Whether automatically scrolling should be enabled or not.
	bool show_break_box;                                   ///< Whether the break/debug box is visible.
	static bool break_check_enabled;                       ///< Stop an AI when it prints a matching string
	static char break_string[MAX_BREAK_STR_STRING_LENGTH]; ///< The string to match to the AI output
	QueryString break_editbox;                             ///< Break editbox
	static StringFilter break_string_filter;               ///< Log filter for break.
	static bool case_sensitive_break_check;                ///< Is the matching done case-sensitive
	int highlight_row;                                     ///< The output row that matches the given string, or -1
	Scrollbar *vscroll;                                    ///< Cache of the vertical scrollbar.

	ScriptLog::LogData *GetLogPointer() const
	{
		if (ai_debug_company == OWNER_DEITY) return (ScriptLog::LogData *)Game::GetInstance()->GetLogPointer();
		return (ScriptLog::LogData *)Company::Get(ai_debug_company)->ai_instance->GetLogPointer();
	}

	/**
	 * Check whether the currently selected AI/GS is dead.
	 * @return true if dead.
	 */
	bool IsDead() const
	{
		if (ai_debug_company == OWNER_DEITY) {
			GameInstance *game = Game::GetInstance();
			return game == nullptr || game->IsDead();
		}
		return !Company::IsValidAiID(ai_debug_company) || Company::Get(ai_debug_company)->ai_instance->IsDead();
	}

	/**
	 * Check whether a company is a valid AI company or GS.
	 * @param company Company to check for validity.
	 * @return true if company is valid for debugging.
	 */
	bool IsValidDebugCompany(CompanyID company) const
	{
		switch (company) {
			case INVALID_COMPANY: return false;
			case OWNER_DEITY:     return Game::GetInstance() != nullptr;
			default:              return Company::IsValidAiID(company);
		}
	}

	/**
	 * Ensure that \c ai_debug_company refers to a valid AI company or GS, or is set to #INVALID_COMPANY.
	 * If no valid company is selected, it selects the first valid AI or GS if any.
	 */
	void SelectValidDebugCompany()
	{
		/* Check if the currently selected company is still active. */
		if (this->IsValidDebugCompany(ai_debug_company)) return;

		ai_debug_company = INVALID_COMPANY;

		for (const Company *c : Company::Iterate()) {
			if (c->is_ai) {
				ChangeToAI(c->index);
				return;
			}
		}

		/* If no AI is available, see if there is a game script. */
		if (Game::GetInstance() != nullptr) ChangeToAI(OWNER_DEITY);
	}

	/**
	 * Constructor for the window.
	 * @param desc The description of the window.
	 * @param number The window number (actually unused).
	 */
	AIDebugWindow(WindowDesc *desc, WindowNumber number) : Window(desc), break_editbox(MAX_BREAK_STR_STRING_LENGTH)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_AID_SCROLLBAR);
		this->show_break_box = _settings_client.gui.ai_developer_tools;
		this->GetWidget<NWidgetStacked>(WID_AID_BREAK_STRING_WIDGETS)->SetDisplayedPlane(this->show_break_box ? 0 : SZSP_HORIZONTAL);
		this->FinishInitNested(number);

		if (!this->show_break_box) break_check_enabled = false;

		this->last_vscroll_pos = 0;
		this->autoscroll = true;
		this->highlight_row = -1;

		this->querystrings[WID_AID_BREAK_STR_EDIT_BOX] = &this->break_editbox;

		SetWidgetsDisabledState(!this->show_break_box, WID_AID_BREAK_STR_ON_OFF_BTN, WID_AID_BREAK_STR_EDIT_BOX, WID_AID_MATCH_CASE_BTN, WIDGET_LIST_END);

		/* Restore the break string value from static variable */
		this->break_editbox.text.Assign(this->break_string);

		this->SelectValidDebugCompany();
		this->InvalidateData(-1);
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		if (widget == WID_AID_LOG_PANEL) {
			resize->height = FONT_HEIGHT_NORMAL + WD_PAR_VSEP_NORMAL;
			size->height = 14 * resize->height + this->top_offset + this->bottom_offset;
		}
	}

	void OnPaint() override
	{
		this->SelectValidDebugCompany();

		/* Draw standard stuff */
		this->DrawWidgets();

		if (this->IsShaded()) return; // Don't draw anything when the window is shaded.

		bool dirty = false;

		/* Paint the company icons */
		for (CompanyID i = COMPANY_FIRST; i < MAX_COMPANIES; i++) {
			NWidgetCore *button = this->GetWidget<NWidgetCore>(i + WID_AID_COMPANY_BUTTON_START);

			bool valid = Company::IsValidAiID(i);

			/* Check whether the validity of the company changed */
			dirty |= (button->IsDisabled() == valid);

			/* Mark dead/paused AIs by setting the background colour. */
			bool dead = valid && Company::Get(i)->ai_instance->IsDead();
			bool paused = valid && Company::Get(i)->ai_instance->IsPaused();
			/* Re-paint if the button was updated.
			 * (note that it is intentional that SetScriptButtonColour is always called) */
			dirty |= SetScriptButtonColour(*button, dead, paused);

			/* Draw company icon only for valid AI companies */
			if (!valid) continue;

			byte offset = (i == ai_debug_company) ? 1 : 0;
			DrawCompanyIcon(i, button->pos_x + button->current_x / 2 - 7 + offset, this->GetWidget<NWidgetBase>(WID_AID_COMPANY_BUTTON_START + i)->pos_y + 2 + offset);
		}

		/* Set button colour for Game Script. */
		GameInstance *game = Game::GetInstance();
		bool valid = game != nullptr;
		bool dead = valid && game->IsDead();
		bool paused = valid && game->IsPaused();

		NWidgetCore *button = this->GetWidget<NWidgetCore>(WID_AID_SCRIPT_GAME);
		dirty |= (button->IsDisabled() == valid) || SetScriptButtonColour(*button, dead, paused);

		if (dirty) this->InvalidateData(-1);

		/* If there are no active companies, don't display anything else. */
		if (ai_debug_company == INVALID_COMPANY) return;

		ScriptLog::LogData *log = this->GetLogPointer();

		int scroll_count = (log == nullptr) ? 0 : log->used;
		if (this->vscroll->GetCount() != scroll_count) {
			this->vscroll->SetCount(scroll_count);

			/* We need a repaint */
			this->SetWidgetDirty(WID_AID_SCROLLBAR);
		}

		if (log == nullptr) return;

		/* Detect when the user scrolls the window. Enable autoscroll when the
		 * bottom-most line becomes visible. */
		if (this->last_vscroll_pos != this->vscroll->GetPosition()) {
			this->autoscroll = this->vscroll->GetPosition() >= log->used - this->vscroll->GetCapacity();
		}
		if (this->autoscroll) {
			int scroll_pos = std::max(0, log->used - this->vscroll->GetCapacity());
			if (scroll_pos != this->vscroll->GetPosition()) {
				this->vscroll->SetPosition(scroll_pos);

				/* We need a repaint */
				this->SetWidgetDirty(WID_AID_SCROLLBAR);
				this->SetWidgetDirty(WID_AID_LOG_PANEL);
			}
		}
		this->last_vscroll_pos = this->vscroll->GetPosition();
	}

	void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_AID_NAME_TEXT:
				if (ai_debug_company == OWNER_DEITY) {
					const GameInfo *info = Game::GetInfo();
					assert(info != nullptr);
					SetDParam(0, STR_AI_DEBUG_NAME_AND_VERSION);
					SetDParamStr(1, info->GetName());
					SetDParam(2, info->GetVersion());
				} else if (ai_debug_company == INVALID_COMPANY || !Company::IsValidAiID(ai_debug_company)) {
					SetDParam(0, STR_EMPTY);
				} else {
					const AIInfo *info = Company::Get(ai_debug_company)->ai_info;
					assert(info != nullptr);
					SetDParam(0, STR_AI_DEBUG_NAME_AND_VERSION);
					SetDParamStr(1, info->GetName());
					SetDParam(2, info->GetVersion());
				}
				break;
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		if (ai_debug_company == INVALID_COMPANY) return;

		switch (widget) {
			case WID_AID_LOG_PANEL: {
				ScriptLog::LogData *log = this->GetLogPointer();
				if (log == nullptr) return;

				int y = this->top_offset;
				for (int i = this->vscroll->GetPosition(); this->vscroll->IsVisible(i) && i < log->used; i++) {
					int pos = (i + log->pos + 1 - log->used + log->count) % log->count;
					if (log->lines[pos] == nullptr) break;

					TextColour colour;
					switch (log->type[pos]) {
						case ScriptLog::LOG_SQ_INFO:  colour = TC_BLACK;  break;
						case ScriptLog::LOG_SQ_ERROR: colour = TC_RED;    break;
						case ScriptLog::LOG_INFO:     colour = TC_BLACK;  break;
						case ScriptLog::LOG_WARNING:  colour = TC_YELLOW; break;
						case ScriptLog::LOG_ERROR:    colour = TC_RED;    break;
						default:                  colour = TC_BLACK;  break;
					}

					/* Check if the current line should be highlighted */
					if (pos == this->highlight_row) {
						GfxFillRect(r.left + 1, r.top + y, r.right - 1, r.top + y + this->resize.step_height - WD_PAR_VSEP_NORMAL, PC_BLACK);
						if (colour == TC_BLACK) colour = TC_WHITE; // Make black text readable by inverting it to white.
					}

					DrawString(r.left + 7, r.right - 7, r.top + y, log->lines[pos], colour, SA_LEFT | SA_FORCE);
					y += this->resize.step_height;
				}
				break;
			}
		}
	}

	/**
	 * Change all settings to select another AI.
	 * @param show_ai The new AI to show.
	 */
	void ChangeToAI(CompanyID show_ai)
	{
		if (!this->IsValidDebugCompany(show_ai)) return;

		ai_debug_company = show_ai;

		this->highlight_row = -1; // The highlight of one AI make little sense for another AI.

		/* Close AI settings window to prevent confusion */
		DeleteWindowByClass(WC_AI_SETTINGS);

		this->InvalidateData(-1);

		this->autoscroll = true;
		this->last_vscroll_pos = this->vscroll->GetPosition();
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		/* Also called for hotkeys, so check for disabledness */
		if (this->IsWidgetDisabled(widget)) return;

		/* Check which button is clicked */
		if (IsInsideMM(widget, WID_AID_COMPANY_BUTTON_START, WID_AID_COMPANY_BUTTON_END + 1)) {
			ChangeToAI((CompanyID)(widget - WID_AID_COMPANY_BUTTON_START));
		}

		switch (widget) {
			case WID_AID_SCRIPT_GAME:
				ChangeToAI(OWNER_DEITY);
				break;

			case WID_AID_RELOAD_TOGGLE:
				if (ai_debug_company == OWNER_DEITY) break;
				/* First kill the company of the AI, then start a new one. This should start the current AI again */
				DoCommandP(0, CCA_DELETE | ai_debug_company << 16 | CRR_MANUAL << 24, 0, CMD_COMPANY_CTRL);
				DoCommandP(0, CCA_NEW_AI | ai_debug_company << 16, 0, CMD_COMPANY_CTRL);
				break;

			case WID_AID_SETTINGS:
				ShowAISettingsWindow(ai_debug_company);
				break;

			case WID_AID_BREAK_STR_ON_OFF_BTN:
				this->break_check_enabled = !this->break_check_enabled;
				this->InvalidateData(-1);
				break;

			case WID_AID_MATCH_CASE_BTN:
				this->case_sensitive_break_check = !this->case_sensitive_break_check;
				this->InvalidateData(-1);
				break;

			case WID_AID_CONTINUE_BTN:
				/* Unpause current AI / game script and mark the corresponding script button dirty. */
				if (!this->IsDead()) {
					if (ai_debug_company == OWNER_DEITY) {
						Game::Unpause();
					} else {
						AI::Unpause(ai_debug_company);
					}
				}

				/* If the last AI/Game Script is unpaused, unpause the game too. */
				if ((_pause_mode & PM_PAUSED_NORMAL) == PM_PAUSED_NORMAL) {
					bool all_unpaused = !Game::IsPaused();
					if (all_unpaused) {
						for (const Company *c : Company::Iterate()) {
							if (c->is_ai && AI::IsPaused(c->index)) {
								all_unpaused = false;
								break;
							}
						}
						if (all_unpaused) {
							/* All scripts have been unpaused => unpause the game. */
							DoCommandP(0, PM_PAUSED_NORMAL, 0, CMD_PAUSE);
						}
					}
				}

				this->highlight_row = -1;
				this->InvalidateData(-1);
				break;
		}
	}

	void OnEditboxChanged(int wid) override
	{
		if (wid == WID_AID_BREAK_STR_EDIT_BOX) {
			/* Save the current string to static member so it can be restored next time the window is opened. */
			strecpy(this->break_string, this->break_editbox.text.buf, lastof(this->break_string));
			break_string_filter.SetFilterTerm(this->break_string);
		}
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 *             This is the company ID of the AI/GS which wrote a new log message, or -1 in other cases.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		/* If the log message is related to the active company tab, check the break string.
		 * This needs to be done in gameloop-scope, so the AI is suspended immediately. */
		if (!gui_scope && data == ai_debug_company && this->IsValidDebugCompany(ai_debug_company) && this->break_check_enabled && !this->break_string_filter.IsEmpty()) {
			/* Get the log instance of the active company */
			ScriptLog::LogData *log = this->GetLogPointer();

			if (log != nullptr) {
				this->break_string_filter.ResetState();
				this->break_string_filter.AddLine(log->lines[log->pos]);
				if (this->break_string_filter.GetState()) {
					/* Pause execution of script. */
					if (!this->IsDead()) {
						if (ai_debug_company == OWNER_DEITY) {
							Game::Pause();
						} else {
							AI::Pause(ai_debug_company);
						}
					}

					/* Pause the game. */
					if ((_pause_mode & PM_PAUSED_NORMAL) == PM_UNPAUSED) {
						DoCommandP(0, PM_PAUSED_NORMAL, 1, CMD_PAUSE);
					}

					/* Highlight row that matched */
					this->highlight_row = log->pos;
				}
			}
		}

		if (!gui_scope) return;

		this->SelectValidDebugCompany();

		ScriptLog::LogData *log = ai_debug_company != INVALID_COMPANY ? this->GetLogPointer() : nullptr;
		this->vscroll->SetCount((log == nullptr) ? 0 : log->used);

		/* Update company buttons */
		for (CompanyID i = COMPANY_FIRST; i < MAX_COMPANIES; i++) {
			this->SetWidgetDisabledState(i + WID_AID_COMPANY_BUTTON_START, !Company::IsValidAiID(i));
			this->SetWidgetLoweredState(i + WID_AID_COMPANY_BUTTON_START, ai_debug_company == i);
		}

		this->SetWidgetDisabledState(WID_AID_SCRIPT_GAME, Game::GetGameInstance() == nullptr);
		this->SetWidgetLoweredState(WID_AID_SCRIPT_GAME, ai_debug_company == OWNER_DEITY);

		this->SetWidgetLoweredState(WID_AID_BREAK_STR_ON_OFF_BTN, this->break_check_enabled);
		this->SetWidgetLoweredState(WID_AID_MATCH_CASE_BTN, this->case_sensitive_break_check);

		this->SetWidgetDisabledState(WID_AID_SETTINGS, ai_debug_company == INVALID_COMPANY);
		extern CompanyID _local_company;
		this->SetWidgetDisabledState(WID_AID_RELOAD_TOGGLE, ai_debug_company == INVALID_COMPANY || ai_debug_company == OWNER_DEITY || ai_debug_company == _local_company);
		this->SetWidgetDisabledState(WID_AID_CONTINUE_BTN, ai_debug_company == INVALID_COMPANY ||
				(ai_debug_company == OWNER_DEITY ? !Game::IsPaused() : !AI::IsPaused(ai_debug_company)));
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_AID_LOG_PANEL);
	}

	static HotkeyList hotkeys;
};

const int AIDebugWindow::top_offset = WD_FRAMERECT_TOP + 2;
const int AIDebugWindow::bottom_offset = WD_FRAMERECT_BOTTOM;
CompanyID AIDebugWindow::ai_debug_company = INVALID_COMPANY;
char AIDebugWindow::break_string[MAX_BREAK_STR_STRING_LENGTH] = "";
bool AIDebugWindow::break_check_enabled = true;
bool AIDebugWindow::case_sensitive_break_check = false;
StringFilter AIDebugWindow::break_string_filter(&AIDebugWindow::case_sensitive_break_check);

/** Make a number of rows with buttons for each company for the AI debug window. */
NWidgetBase *MakeCompanyButtonRowsAIDebug(int *biggest_index)
{
	return MakeCompanyButtonRows(biggest_index, WID_AID_COMPANY_BUTTON_START, WID_AID_COMPANY_BUTTON_END, COLOUR_GREY, 8, STR_AI_DEBUG_SELECT_AI_TOOLTIP);
}

/**
 * Handler for global hotkeys of the AIDebugWindow.
 * @param hotkey Hotkey
 * @return ES_HANDLED if hotkey was accepted.
 */
static EventState AIDebugGlobalHotkeys(int hotkey)
{
	if (_game_mode != GM_NORMAL) return ES_NOT_HANDLED;
	Window *w = ShowAIDebugWindow(INVALID_COMPANY);
	if (w == nullptr) return ES_NOT_HANDLED;
	return w->OnHotkey(hotkey);
}

static Hotkey aidebug_hotkeys[] = {
	Hotkey('1', "company_1", WID_AID_COMPANY_BUTTON_START),
	Hotkey('2', "company_2", WID_AID_COMPANY_BUTTON_START + 1),
	Hotkey('3', "company_3", WID_AID_COMPANY_BUTTON_START + 2),
	Hotkey('4', "company_4", WID_AID_COMPANY_BUTTON_START + 3),
	Hotkey('5', "company_5", WID_AID_COMPANY_BUTTON_START + 4),
	Hotkey('6', "company_6", WID_AID_COMPANY_BUTTON_START + 5),
	Hotkey('7', "company_7", WID_AID_COMPANY_BUTTON_START + 6),
	Hotkey('8', "company_8", WID_AID_COMPANY_BUTTON_START + 7),
	Hotkey('9', "company_9", WID_AID_COMPANY_BUTTON_START + 8),
	Hotkey((uint16)0, "company_10", WID_AID_COMPANY_BUTTON_START + 9),
	Hotkey((uint16)0, "company_11", WID_AID_COMPANY_BUTTON_START + 10),
	Hotkey((uint16)0, "company_12", WID_AID_COMPANY_BUTTON_START + 11),
	Hotkey((uint16)0, "company_13", WID_AID_COMPANY_BUTTON_START + 12),
	Hotkey((uint16)0, "company_14", WID_AID_COMPANY_BUTTON_START + 13),
	Hotkey((uint16)0, "company_15", WID_AID_COMPANY_BUTTON_START + 14),
	Hotkey('S', "settings", WID_AID_SETTINGS),
	Hotkey('0', "game_script", WID_AID_SCRIPT_GAME),
	Hotkey((uint16)0, "reload", WID_AID_RELOAD_TOGGLE),
	Hotkey('B', "break_toggle", WID_AID_BREAK_STR_ON_OFF_BTN),
	Hotkey('F', "break_string", WID_AID_BREAK_STR_EDIT_BOX),
	Hotkey('C', "match_case", WID_AID_MATCH_CASE_BTN),
	Hotkey(WKC_RETURN, "continue", WID_AID_CONTINUE_BTN),
	HOTKEY_LIST_END
};
HotkeyList AIDebugWindow::hotkeys("aidebug", aidebug_hotkeys, AIDebugGlobalHotkeys);

/** Widgets for the AI debug window. */
static const NWidgetPart _nested_ai_debug_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY), SetDataTip(STR_AI_DEBUG, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY, WID_AID_VIEW),
		NWidgetFunction(MakeCompanyButtonRowsAIDebug), SetPadding(0, 2, 1, 2),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_AID_SCRIPT_GAME), SetMinimalSize(100, 20), SetResize(1, 0), SetDataTip(STR_AI_GAME_SCRIPT, STR_AI_GAME_SCRIPT_TOOLTIP),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_AID_NAME_TEXT), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_JUST_STRING, STR_AI_DEBUG_NAME_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_AID_SETTINGS), SetMinimalSize(100, 20), SetDataTip(STR_AI_DEBUG_SETTINGS, STR_AI_DEBUG_SETTINGS_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_AID_RELOAD_TOGGLE), SetMinimalSize(100, 20), SetDataTip(STR_AI_DEBUG_RELOAD, STR_AI_DEBUG_RELOAD_TOOLTIP),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_VERTICAL),
			/* Log panel */
			NWidget(WWT_PANEL, COLOUR_GREY, WID_AID_LOG_PANEL), SetMinimalSize(287, 180), SetResize(1, 1), SetScrollbar(WID_AID_SCROLLBAR),
			EndContainer(),
			/* Break string widgets */
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_AID_BREAK_STRING_WIDGETS),
				NWidget(NWID_HORIZONTAL),
					NWidget(WWT_IMGBTN_2, COLOUR_GREY, WID_AID_BREAK_STR_ON_OFF_BTN), SetFill(0, 1), SetDataTip(SPR_FLAG_VEH_STOPPED, STR_AI_DEBUG_BREAK_STR_ON_OFF_TOOLTIP),
					NWidget(WWT_PANEL, COLOUR_GREY),
						NWidget(NWID_HORIZONTAL),
							NWidget(WWT_LABEL, COLOUR_GREY), SetPadding(2, 2, 2, 4), SetDataTip(STR_AI_DEBUG_BREAK_ON_LABEL, 0x0),
							NWidget(WWT_EDITBOX, COLOUR_GREY, WID_AID_BREAK_STR_EDIT_BOX), SetFill(1, 1), SetResize(1, 0), SetPadding(2, 2, 2, 2), SetDataTip(STR_AI_DEBUG_BREAK_STR_OSKTITLE, STR_AI_DEBUG_BREAK_STR_TOOLTIP),
						EndContainer(),
					EndContainer(),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_AID_MATCH_CASE_BTN), SetMinimalSize(100, 0), SetFill(0, 1), SetDataTip(STR_AI_DEBUG_MATCH_CASE, STR_AI_DEBUG_MATCH_CASE_TOOLTIP),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_AID_CONTINUE_BTN), SetMinimalSize(100, 0), SetFill(0, 1), SetDataTip(STR_AI_DEBUG_CONTINUE, STR_AI_DEBUG_CONTINUE_TOOLTIP),
				EndContainer(),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_VERTICAL),
			NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_AID_SCROLLBAR),
			NWidget(WWT_RESIZEBOX, COLOUR_GREY),
		EndContainer(),
	EndContainer(),
};

/** Window definition for the AI debug window. */
static WindowDesc _ai_debug_desc(
	WDP_AUTO, "script_debug", 600, 450,
	WC_AI_DEBUG, WC_NONE,
	0,
	_nested_ai_debug_widgets, lengthof(_nested_ai_debug_widgets),
	&AIDebugWindow::hotkeys
);

/**
 * Open the AI debug window and select the given company.
 * @param show_company Display debug information about this AI company.
 */
Window *ShowAIDebugWindow(CompanyID show_company)
{
	if (!_networking || _network_server) {
		AIDebugWindow *w = (AIDebugWindow *)BringWindowToFrontById(WC_AI_DEBUG, 0);
		if (w == nullptr) w = new AIDebugWindow(&_ai_debug_desc, 0);
		if (show_company != INVALID_COMPANY) w->ChangeToAI(show_company);
		return w;
	} else {
		ShowErrorMessage(STR_ERROR_AI_DEBUG_SERVER_ONLY, INVALID_STRING_ID, WL_INFO);
	}

	return nullptr;
}

/**
 * Reset the AI windows to their initial state.
 */
void InitializeAIGui()
{
	AIDebugWindow::ai_debug_company = INVALID_COMPANY;
}

/** Open the AI debug window if one of the AI scripts has crashed. */
void ShowAIDebugWindowIfAIError()
{
	/* Network clients can't debug AIs. */
	if (_networking && !_network_server) return;

	for (const Company *c : Company::Iterate()) {
		if (c->is_ai && c->ai_instance->IsDead()) {
			ShowAIDebugWindow(c->index);
			break;
		}
	}

	GameInstance *g = Game::GetGameInstance();
	if (g != nullptr && g->IsDead()) {
		ShowAIDebugWindow(OWNER_DEITY);
	}
}
