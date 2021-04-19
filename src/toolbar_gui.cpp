/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file toolbar_gui.cpp Code related to the (main) toolbar. */

#include "stdafx.h"
#include "gui.h"
#include "window_gui.h"
#include "window_func.h"
#include "viewport_func.h"
#include "command_func.h"
#include "vehicle_gui.h"
#include "rail_gui.h"
#include "road.h"
#include "road_gui.h"
#include "date_func.h"
#include "vehicle_func.h"
#include "sound_func.h"
#include "terraform_gui.h"
#include "strings_func.h"
#include "company_func.h"
#include "company_gui.h"
#include "vehicle_base.h"
#include "cheat_func.h"
#include "transparency_gui.h"
#include "screenshot.h"
#include "signs_func.h"
#include "fios.h"
#include "console_gui.h"
#include "news_gui.h"
#include "ai/ai_gui.hpp"
#include "tilehighlight_func.h"
#include "smallmap_gui.h"
#include "graph_gui.h"
#include "textbuf_gui.h"
#include "linkgraph/linkgraph_gui.h"
#include "newgrf_debug.h"
#include "hotkeys.h"
#include "engine_base.h"
#include "highscore.h"
#include "game/game.hpp"
#include "goal_base.h"
#include "story_base.h"
#include "toolbar_gui.h"
#include "framerate_type.h"
#include "guitimer_func.h"
#include "screenshot_gui.h"
#include "help_gui.h"

#include "widgets/toolbar_widget.h"

#include "network/network.h"
#include "network/network_gui.h"
#include "network/network_func.h"

#include "safeguards.h"


/** Width of the toolbar, shared by statusbar. */
uint _toolbar_width = 0;

RailType _last_built_railtype;
RoadType _last_built_roadtype;
RoadType _last_built_tramtype;

/** Toobar modes */
enum ToolbarMode {
	TB_NORMAL,
	TB_UPPER,
	TB_LOWER
};

/** Callback functions. */
enum CallBackFunction {
	CBF_NONE,
	CBF_PLACE_SIGN,
	CBF_PLACE_LANDINFO,
};

static CallBackFunction _last_started_action = CBF_NONE; ///< Last started user action.


/**
 * Drop down list entry for showing a checked/unchecked toggle item.
 */
class DropDownListCheckedItem : public DropDownListStringItem {
	uint checkmark_width;
public:
	bool checked;

	DropDownListCheckedItem(StringID string, int result, bool masked, bool checked) : DropDownListStringItem(string, result, masked), checked(checked)
	{
		this->checkmark_width = GetStringBoundingBox(STR_JUST_CHECKMARK).width + 3;
	}

	uint Width() const
	{
		return DropDownListStringItem::Width() + this->checkmark_width;
	}

	void Draw(int left, int right, int top, int bottom, bool sel, Colours bg_colour) const
	{
		bool rtl = _current_text_dir == TD_RTL;
		if (this->checked) {
			DrawString(left + WD_FRAMERECT_LEFT, right - WD_FRAMERECT_RIGHT, top, STR_JUST_CHECKMARK, sel ? TC_WHITE : TC_BLACK);
		}
		DrawString(left + WD_FRAMERECT_LEFT + (rtl ? 0 : this->checkmark_width), right - WD_FRAMERECT_RIGHT - (rtl ? this->checkmark_width : 0), top, this->String(), sel ? TC_WHITE : TC_BLACK);
	}
};

/**
 * Drop down list entry for showing a company entry, with companies 'blob'.
 */
class DropDownListCompanyItem : public DropDownListItem {
	Dimension icon_size;
	Dimension lock_size;
public:
	bool greyed;

	DropDownListCompanyItem(int result, bool masked, bool greyed) : DropDownListItem(result, masked), greyed(greyed)
	{
		this->icon_size = GetSpriteSize(SPR_COMPANY_ICON);
		this->lock_size = GetSpriteSize(SPR_LOCK);
	}

	bool Selectable() const override
	{
		return true;
	}

	uint Width() const override
	{
		CompanyID company = (CompanyID)this->result;
		SetDParam(0, company);
		SetDParam(1, company);
		return GetStringBoundingBox(STR_COMPANY_NAME_COMPANY_NUM).width + this->icon_size.width + this->lock_size.width + 6;
	}

	uint Height(uint width) const override
	{
		return std::max(std::max(this->icon_size.height, this->lock_size.height) + 2U, (uint)FONT_HEIGHT_NORMAL);
	}

	void Draw(int left, int right, int top, int bottom, bool sel, Colours bg_colour) const override
	{
		CompanyID company = (CompanyID)this->result;
		bool rtl = _current_text_dir == TD_RTL;

		/* It's possible the company is deleted while the dropdown is open */
		if (!Company::IsValidID(company)) return;

		int icon_offset = (bottom - top - icon_size.height) / 2;
		int text_offset = (bottom - top - FONT_HEIGHT_NORMAL) / 2;
		int lock_offset = (bottom - top - lock_size.height) / 2;

		DrawCompanyIcon(company, rtl ? right - this->icon_size.width - WD_FRAMERECT_RIGHT : left + WD_FRAMERECT_LEFT, top + icon_offset);
		if (NetworkCompanyIsPassworded(company)) {
			DrawSprite(SPR_LOCK, PAL_NONE, rtl ? left + WD_FRAMERECT_LEFT : right - this->lock_size.width - WD_FRAMERECT_RIGHT, top + lock_offset);
		}

		SetDParam(0, company);
		SetDParam(1, company);
		TextColour col;
		if (this->greyed) {
			col = (sel ? TC_SILVER : TC_GREY) | TC_NO_SHADE;
		} else {
			col = sel ? TC_WHITE : TC_BLACK;
		}
		DrawString(left + WD_FRAMERECT_LEFT + (rtl ? 3 + this->lock_size.width : 3 + this->icon_size.width), right - WD_FRAMERECT_RIGHT - (rtl ? 3 + this->icon_size.width : 3 + this->lock_size.width), top + text_offset, STR_COMPANY_NAME_COMPANY_NUM, col);
	}
};

/**
 * Pop up a generic text only menu.
 * @param w Toolbar
 * @param widget Toolbar button
 * @param list List of items
 * @param def Default item
 */
static void PopupMainToolbMenu(Window *w, int widget, DropDownList &&list, int def)
{
	ShowDropDownList(w, std::move(list), def, widget, 0, true, true);
	if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
}

/**
 * Pop up a generic text only menu.
 * @param w Toolbar
 * @param widget Toolbar button
 * @param string String for the first item in the menu
 * @param count Number of items in the menu
 */
static void PopupMainToolbMenu(Window *w, int widget, StringID string, int count)
{
	DropDownList list;
	for (int i = 0; i < count; i++) {
		list.emplace_back(new DropDownListStringItem(string + i, i, false));
	}
	PopupMainToolbMenu(w, widget, std::move(list), 0);
}

/** Enum for the Company Toolbar's network related buttons */
static const int CTMN_CLIENT_LIST = -1; ///< Show the client list
static const int CTMN_NEW_COMPANY = -2; ///< Create a new company
static const int CTMN_SPECTATE    = -3; ///< Become spectator
static const int CTMN_SPECTATOR   = -4; ///< Show a company window as spectator

/**
 * Pop up a generic company list menu.
 * @param w The toolbar window.
 * @param widget The button widget id.
 * @param grey A bitbask of which items to mark as disabled.
 */
static void PopupMainCompanyToolbMenu(Window *w, int widget, int grey = 0)
{
	DropDownList list;

	switch (widget) {
		case WID_TN_COMPANIES:
			if (!_networking) break;

			/* Add the client list button for the companies menu */
			list.emplace_back(new DropDownListStringItem(STR_NETWORK_COMPANY_LIST_CLIENT_LIST, CTMN_CLIENT_LIST, false));

			if (_local_company == COMPANY_SPECTATOR) {
				list.emplace_back(new DropDownListStringItem(STR_NETWORK_COMPANY_LIST_NEW_COMPANY, CTMN_NEW_COMPANY, NetworkMaxCompaniesReached()));
			} else {
				list.emplace_back(new DropDownListStringItem(STR_NETWORK_COMPANY_LIST_SPECTATE, CTMN_SPECTATE, NetworkMaxSpectatorsReached()));
			}
			break;

		case WID_TN_STORY:
			list.emplace_back(new DropDownListStringItem(STR_STORY_BOOK_SPECTATOR, CTMN_SPECTATOR, false));
			break;

		case WID_TN_GOAL:
			list.emplace_back(new DropDownListStringItem(STR_GOALS_SPECTATOR, CTMN_SPECTATOR, false));
			break;
	}

	for (CompanyID c = COMPANY_FIRST; c < MAX_COMPANIES; c++) {
		if (!Company::IsValidID(c)) continue;
		list.emplace_back(new DropDownListCompanyItem(c, false, HasBit(grey, c)));
	}

	PopupMainToolbMenu(w, widget, std::move(list), _local_company == COMPANY_SPECTATOR ? (widget == WID_TN_COMPANIES ? CTMN_CLIENT_LIST : CTMN_SPECTATOR) : (int)_local_company);
}


static ToolbarMode _toolbar_mode;

static CallBackFunction SelectSignTool()
{
	if (_last_started_action == CBF_PLACE_SIGN) {
		ResetObjectToPlace();
		return CBF_NONE;
	} else {
		SetObjectToPlace(SPR_CURSOR_SIGN, PAL_NONE, HT_RECT, WC_MAIN_TOOLBAR, 0);
		return CBF_PLACE_SIGN;
	}
}

/* --- Pausing --- */

static CallBackFunction ToolbarPauseClick(Window *w)
{
	if (_networking && !_network_server) return CBF_NONE; // only server can pause the game

	if (DoCommandP(0, PM_PAUSED_NORMAL, _pause_mode == PM_UNPAUSED, CMD_PAUSE)) {
		if (_settings_client.sound.confirm) SndPlayFx(SND_15_BEEP);
	}
	return CBF_NONE;
}

/**
 * Toggle fast forward mode.
 *
 * @param w Unused.
 * @return #CBF_NONE
 */
static CallBackFunction ToolbarFastForwardClick(Window *w)
{
	ChangeGameSpeed(_game_speed == 100);

	if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
	return CBF_NONE;
}

/**
 * Game Option button menu entries.
 */
enum OptionMenuEntries {
	OME_GAMEOPTIONS,
	OME_SETTINGS,
	OME_SCRIPT_SETTINGS,
	OME_NEWGRFSETTINGS,
	OME_TRANSPARENCIES,
	OME_SHOW_TOWNNAMES,
	OME_SHOW_STATIONNAMES,
	OME_SHOW_WAYPOINTNAMES,
	OME_SHOW_SIGNS,
	OME_SHOW_COMPETITOR_SIGNS,
	OME_FULL_ANIMATION,
	OME_FULL_DETAILS,
	OME_TRANSPARENTBUILDINGS,
	OME_SHOW_STATIONSIGNS,
};

/**
 * Handle click on Options button in toolbar.
 *
 * @param w parent window the shown Drop down list is attached to.
 * @return #CBF_NONE
 */
static CallBackFunction ToolbarOptionsClick(Window *w)
{
	DropDownList list;
	list.emplace_back(new DropDownListStringItem(STR_SETTINGS_MENU_GAME_OPTIONS,             OME_GAMEOPTIONS, false));
	list.emplace_back(new DropDownListStringItem(STR_SETTINGS_MENU_CONFIG_SETTINGS_TREE,     OME_SETTINGS, false));
	/* Changes to the per-AI settings don't get send from the server to the clients. Clients get
	 * the settings once they join but never update it. As such don't show the window at all
	 * to network clients. */
	if (!_networking || _network_server) list.emplace_back(new DropDownListStringItem(STR_SETTINGS_MENU_SCRIPT_SETTINGS, OME_SCRIPT_SETTINGS, false));
	list.emplace_back(new DropDownListStringItem(STR_SETTINGS_MENU_NEWGRF_SETTINGS,          OME_NEWGRFSETTINGS, false));
	list.emplace_back(new DropDownListStringItem(STR_SETTINGS_MENU_TRANSPARENCY_OPTIONS,     OME_TRANSPARENCIES, false));
	list.emplace_back(new DropDownListItem(-1, false));
	list.emplace_back(new DropDownListCheckedItem(STR_SETTINGS_MENU_TOWN_NAMES_DISPLAYED,    OME_SHOW_TOWNNAMES, false, HasBit(_display_opt, DO_SHOW_TOWN_NAMES)));
	list.emplace_back(new DropDownListCheckedItem(STR_SETTINGS_MENU_STATION_NAMES_DISPLAYED, OME_SHOW_STATIONNAMES, false, HasBit(_display_opt, DO_SHOW_STATION_NAMES)));
	list.emplace_back(new DropDownListCheckedItem(STR_SETTINGS_MENU_WAYPOINTS_DISPLAYED,     OME_SHOW_WAYPOINTNAMES, false, HasBit(_display_opt, DO_SHOW_WAYPOINT_NAMES)));
	list.emplace_back(new DropDownListCheckedItem(STR_SETTINGS_MENU_SIGNS_DISPLAYED,         OME_SHOW_SIGNS, false, HasBit(_display_opt, DO_SHOW_SIGNS)));
	list.emplace_back(new DropDownListCheckedItem(STR_SETTINGS_MENU_SHOW_COMPETITOR_SIGNS,   OME_SHOW_COMPETITOR_SIGNS, false, HasBit(_display_opt, DO_SHOW_COMPETITOR_SIGNS)));
	list.emplace_back(new DropDownListCheckedItem(STR_SETTINGS_MENU_FULL_ANIMATION,          OME_FULL_ANIMATION, false, HasBit(_display_opt, DO_FULL_ANIMATION)));
	list.emplace_back(new DropDownListCheckedItem(STR_SETTINGS_MENU_FULL_DETAIL,             OME_FULL_DETAILS, false, HasBit(_display_opt, DO_FULL_DETAIL)));
	list.emplace_back(new DropDownListCheckedItem(STR_SETTINGS_MENU_TRANSPARENT_BUILDINGS,   OME_TRANSPARENTBUILDINGS, false, IsTransparencySet(TO_HOUSES)));
	list.emplace_back(new DropDownListCheckedItem(STR_SETTINGS_MENU_TRANSPARENT_SIGNS,       OME_SHOW_STATIONSIGNS, false, IsTransparencySet(TO_SIGNS)));

	ShowDropDownList(w, std::move(list), 0, WID_TN_SETTINGS, 140, true, true);
	if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
	return CBF_NONE;
}

/**
 * Handle click on one of the entries in the Options button menu.
 *
 * @param index Index being clicked.
 * @return #CBF_NONE
 */
static CallBackFunction MenuClickSettings(int index)
{
	switch (index) {
		case OME_GAMEOPTIONS:          ShowGameOptions();                               return CBF_NONE;
		case OME_SETTINGS:             ShowGameSettings();                              return CBF_NONE;
		case OME_SCRIPT_SETTINGS:      ShowAIConfigWindow();                            return CBF_NONE;
		case OME_NEWGRFSETTINGS:       ShowNewGRFSettings(!_networking && _settings_client.gui.UserIsAllowedToChangeNewGRFs(), true, true, &_grfconfig); return CBF_NONE;
		case OME_TRANSPARENCIES:       ShowTransparencyToolbar();                       break;

		case OME_SHOW_TOWNNAMES:       ToggleBit(_display_opt, DO_SHOW_TOWN_NAMES);     break;
		case OME_SHOW_STATIONNAMES:    ToggleBit(_display_opt, DO_SHOW_STATION_NAMES);  break;
		case OME_SHOW_WAYPOINTNAMES:   ToggleBit(_display_opt, DO_SHOW_WAYPOINT_NAMES); break;
		case OME_SHOW_SIGNS:           ToggleBit(_display_opt, DO_SHOW_SIGNS);          break;
		case OME_SHOW_COMPETITOR_SIGNS:
			ToggleBit(_display_opt, DO_SHOW_COMPETITOR_SIGNS);
			InvalidateWindowClassesData(WC_SIGN_LIST, -1);
			break;
		case OME_FULL_ANIMATION:       ToggleBit(_display_opt, DO_FULL_ANIMATION); CheckBlitter(); break;
		case OME_FULL_DETAILS:         ToggleBit(_display_opt, DO_FULL_DETAIL);         break;
		case OME_TRANSPARENTBUILDINGS: ToggleTransparency(TO_HOUSES);                   break;
		case OME_SHOW_STATIONSIGNS:    ToggleTransparency(TO_SIGNS);                    break;
	}
	MarkWholeScreenDirty();
	return CBF_NONE;
}

/**
 * SaveLoad entries in scenario editor mode.
 */
enum SaveLoadEditorMenuEntries {
	SLEME_SAVE_SCENARIO   = 0,
	SLEME_LOAD_SCENARIO,
	SLEME_SAVE_HEIGHTMAP,
	SLEME_LOAD_HEIGHTMAP,
	SLEME_EXIT_TOINTRO,
	SLEME_EXIT_GAME       = 6,
	SLEME_MENUCOUNT,
};

/**
 * SaveLoad entries in normal game mode.
 */
enum SaveLoadNormalMenuEntries {
	SLNME_SAVE_GAME   = 0,
	SLNME_LOAD_GAME,
	SLNME_EXIT_TOINTRO,
	SLNME_EXIT_GAME = 4,
	SLNME_MENUCOUNT,
};

/**
 * Handle click on Save button in toolbar in normal game mode.
 *
 * @param w parent window the shown save dialogue is attached to.
 * @return #CBF_NONE
 */
static CallBackFunction ToolbarSaveClick(Window *w)
{
	PopupMainToolbMenu(w, WID_TN_SAVE, STR_FILE_MENU_SAVE_GAME, SLNME_MENUCOUNT);
	return CBF_NONE;
}

/**
 * Handle click on SaveLoad button in toolbar in the scenario editor.
 *
 * @param w parent window the shown save dialogue is attached to.
 * @return #CBF_NONE
 */
static CallBackFunction ToolbarScenSaveOrLoad(Window *w)
{
	PopupMainToolbMenu(w, WID_TE_SAVE, STR_SCENEDIT_FILE_MENU_SAVE_SCENARIO, SLEME_MENUCOUNT);
	return CBF_NONE;
}

/**
 * Handle click on one of the entries in the SaveLoad menu.
 *
 * @param index Index being clicked.
 * @return #CBF_NONE
 */
static CallBackFunction MenuClickSaveLoad(int index = 0)
{
	if (_game_mode == GM_EDITOR) {
		switch (index) {
			case SLEME_SAVE_SCENARIO:  ShowSaveLoadDialog(FT_SCENARIO, SLO_SAVE);  break;
			case SLEME_LOAD_SCENARIO:  ShowSaveLoadDialog(FT_SCENARIO, SLO_LOAD);  break;
			case SLEME_SAVE_HEIGHTMAP: ShowSaveLoadDialog(FT_HEIGHTMAP,SLO_SAVE); break;
			case SLEME_LOAD_HEIGHTMAP: ShowSaveLoadDialog(FT_HEIGHTMAP,SLO_LOAD); break;
			case SLEME_EXIT_TOINTRO:   AskExitToGameMenu();                    break;
			case SLEME_EXIT_GAME:      HandleExitGameRequest();                break;
		}
	} else {
		switch (index) {
			case SLNME_SAVE_GAME:      ShowSaveLoadDialog(FT_SAVEGAME, SLO_SAVE); break;
			case SLNME_LOAD_GAME:      ShowSaveLoadDialog(FT_SAVEGAME, SLO_LOAD); break;
			case SLNME_EXIT_TOINTRO:   AskExitToGameMenu();               break;
			case SLNME_EXIT_GAME:      HandleExitGameRequest();           break;
		}
	}
	return CBF_NONE;
}

/* --- Map button menu --- */

enum MapMenuEntries {
	MME_SHOW_SMALLMAP        = 0,
	MME_SHOW_EXTRAVIEWPORTS,
	MME_SHOW_LINKGRAPH,
	MME_SHOW_SIGNLISTS,
	MME_SHOW_TOWNDIRECTORY,
	MME_SHOW_INDUSTRYDIRECTORY,
};

static CallBackFunction ToolbarMapClick(Window *w)
{
	DropDownList list;
	list.emplace_back(new DropDownListStringItem(STR_MAP_MENU_MAP_OF_WORLD,            MME_SHOW_SMALLMAP,          false));
	list.emplace_back(new DropDownListStringItem(STR_MAP_MENU_EXTRA_VIEWPORT,          MME_SHOW_EXTRAVIEWPORTS,    false));
	list.emplace_back(new DropDownListStringItem(STR_MAP_MENU_LINGRAPH_LEGEND,         MME_SHOW_LINKGRAPH,         false));
	list.emplace_back(new DropDownListStringItem(STR_MAP_MENU_SIGN_LIST,               MME_SHOW_SIGNLISTS,         false));
	PopupMainToolbMenu(w, WID_TN_SMALL_MAP, std::move(list), 0);
	return CBF_NONE;
}

static CallBackFunction ToolbarScenMapTownDir(Window *w)
{
	DropDownList list;
	list.emplace_back(new DropDownListStringItem(STR_MAP_MENU_MAP_OF_WORLD,            MME_SHOW_SMALLMAP,          false));
	list.emplace_back(new DropDownListStringItem(STR_MAP_MENU_EXTRA_VIEWPORT,          MME_SHOW_EXTRAVIEWPORTS,    false));
	list.emplace_back(new DropDownListStringItem(STR_MAP_MENU_SIGN_LIST,               MME_SHOW_SIGNLISTS,         false));
	list.emplace_back(new DropDownListStringItem(STR_TOWN_MENU_TOWN_DIRECTORY,         MME_SHOW_TOWNDIRECTORY,     false));
	list.emplace_back(new DropDownListStringItem(STR_INDUSTRY_MENU_INDUSTRY_DIRECTORY, MME_SHOW_INDUSTRYDIRECTORY, false));
	PopupMainToolbMenu(w, WID_TE_SMALL_MAP, std::move(list), 0);
	return CBF_NONE;
}

/**
 * Handle click on one of the entries in the Map menu.
 *
 * @param index Index being clicked.
 * @return #CBF_NONE
 */
static CallBackFunction MenuClickMap(int index)
{
	switch (index) {
		case MME_SHOW_SMALLMAP:          ShowSmallMap();            break;
		case MME_SHOW_EXTRAVIEWPORTS:    ShowExtraViewportWindow(); break;
		case MME_SHOW_LINKGRAPH:         ShowLinkGraphLegend();     break;
		case MME_SHOW_SIGNLISTS:         ShowSignList();            break;
		case MME_SHOW_TOWNDIRECTORY:     ShowTownDirectory();       break;
		case MME_SHOW_INDUSTRYDIRECTORY: ShowIndustryDirectory();   break;
	}
	return CBF_NONE;
}

/* --- Town button menu --- */

static CallBackFunction ToolbarTownClick(Window *w)
{
	PopupMainToolbMenu(w, WID_TN_TOWNS, STR_TOWN_MENU_TOWN_DIRECTORY, (_settings_game.economy.found_town == TF_FORBIDDEN) ? 1 : 2);
	return CBF_NONE;
}

/**
 * Handle click on one of the entries in the Town menu.
 *
 * @param index Index being clicked.
 * @return #CBF_NONE
 */
static CallBackFunction MenuClickTown(int index)
{
	switch (index) {
		case 0: ShowTownDirectory(); break;
		case 1: // setting could be changed when the dropdown was open
			if (_settings_game.economy.found_town != TF_FORBIDDEN) ShowFoundTownWindow();
			break;
	}
	return CBF_NONE;
}

/* --- Subidies button menu --- */

static CallBackFunction ToolbarSubsidiesClick(Window *w)
{
	PopupMainToolbMenu(w, WID_TN_SUBSIDIES, STR_SUBSIDIES_MENU_SUBSIDIES, 1);
	return CBF_NONE;
}

/**
 * Handle click on the entry in the Subsidies menu.
 *
 * @param index Unused.
 * @return #CBF_NONE
 */
static CallBackFunction MenuClickSubsidies(int index)
{
	switch (index) {
		case 0: ShowSubsidiesList(); break;
	}
	return CBF_NONE;
}

/* --- Stations button menu --- */

static CallBackFunction ToolbarStationsClick(Window *w)
{
	PopupMainCompanyToolbMenu(w, WID_TN_STATIONS);
	return CBF_NONE;
}

/**
 * Handle click on the entry in the Stations menu
 *
 * @param index CompanyID to show station list for
 * @return #CBF_NONE
 */
static CallBackFunction MenuClickStations(int index)
{
	ShowCompanyStations((CompanyID)index);
	return CBF_NONE;
}

/* --- Finances button menu --- */

static CallBackFunction ToolbarFinancesClick(Window *w)
{
	PopupMainCompanyToolbMenu(w, WID_TN_FINANCES);
	return CBF_NONE;
}

/**
 * Handle click on the entry in the finances overview menu.
 *
 * @param index CompanyID to show finances for.
 * @return #CBF_NONE
 */
static CallBackFunction MenuClickFinances(int index)
{
	ShowCompanyFinances((CompanyID)index);
	return CBF_NONE;
}

/* --- Company's button menu --- */

static CallBackFunction ToolbarCompaniesClick(Window *w)
{
	PopupMainCompanyToolbMenu(w, WID_TN_COMPANIES, 0);
	return CBF_NONE;
}

/**
 * Handle click on the entry in the Company menu.
 *
 * @param index Menu entry to handle.
 * @return #CBF_NONE
 */
static CallBackFunction MenuClickCompany(int index)
{
	if (_networking) {
		switch (index) {
			case CTMN_CLIENT_LIST:
				ShowClientList();
				return CBF_NONE;

			case CTMN_NEW_COMPANY:
				if (_network_server) {
					DoCommandP(0, CCA_NEW, _network_own_client_id, CMD_COMPANY_CTRL);
				} else {
					NetworkSendCommand(0, CCA_NEW, 0, CMD_COMPANY_CTRL, nullptr, nullptr, _local_company);
				}
				return CBF_NONE;

			case CTMN_SPECTATE:
				if (_network_server) {
					NetworkServerDoMove(CLIENT_ID_SERVER, COMPANY_SPECTATOR);
					MarkWholeScreenDirty();
				} else {
					NetworkClientRequestMove(COMPANY_SPECTATOR);
				}
				return CBF_NONE;
		}
	}
	ShowCompany((CompanyID)index);
	return CBF_NONE;
}

/* --- Story button menu --- */

static CallBackFunction ToolbarStoryClick(Window *w)
{
	PopupMainCompanyToolbMenu(w, WID_TN_STORY, 0);
	return CBF_NONE;
}

/**
 * Handle click on the entry in the Story menu
 *
 * @param index CompanyID to show story book for
 * @return #CBF_NONE
 */
static CallBackFunction MenuClickStory(int index)
{
	ShowStoryBook(index == CTMN_SPECTATOR ? INVALID_COMPANY : (CompanyID)index);
	return CBF_NONE;
}

/* --- Goal button menu --- */

static CallBackFunction ToolbarGoalClick(Window *w)
{
	PopupMainCompanyToolbMenu(w, WID_TN_GOAL, 0);
	return CBF_NONE;
}

/**
 * Handle click on the entry in the Goal menu
 *
 * @param index CompanyID to show story book for
 * @return #CBF_NONE
 */
static CallBackFunction MenuClickGoal(int index)
{
	ShowGoalsList(index == CTMN_SPECTATOR ? INVALID_COMPANY : (CompanyID)index);
	return CBF_NONE;
}

/* --- Graphs button menu --- */

static CallBackFunction ToolbarGraphsClick(Window *w)
{
	PopupMainToolbMenu(w, WID_TN_GRAPHS, STR_GRAPH_MENU_OPERATING_PROFIT_GRAPH, (_toolbar_mode == TB_NORMAL) ? 6 : 8);
	return CBF_NONE;
}

/**
 * Handle click on the entry in the Graphs menu.
 *
 * @param index Graph to show.
 * @return #CBF_NONE
 */
static CallBackFunction MenuClickGraphs(int index)
{
	switch (index) {
		case 0: ShowOperatingProfitGraph();    break;
		case 1: ShowIncomeGraph();             break;
		case 2: ShowDeliveredCargoGraph();     break;
		case 3: ShowPerformanceHistoryGraph(); break;
		case 4: ShowCompanyValueGraph();       break;
		case 5: ShowCargoPaymentRates();       break;
		/* functions for combined graphs/league button */
		case 6: ShowCompanyLeagueTable();      break;
		case 7: ShowPerformanceRatingDetail(); break;
	}
	return CBF_NONE;
}

/* --- League button menu --- */

static CallBackFunction ToolbarLeagueClick(Window *w)
{
	PopupMainToolbMenu(w, WID_TN_LEAGUE, STR_GRAPH_MENU_COMPANY_LEAGUE_TABLE, _networking ? 2 : 3);
	return CBF_NONE;
}

/**
 * Handle click on the entry in the CompanyLeague menu.
 *
 * @param index Menu entry number.
 * @return #CBF_NONE
 */
static CallBackFunction MenuClickLeague(int index)
{
	switch (index) {
		case 0: ShowCompanyLeagueTable();      break;
		case 1: ShowPerformanceRatingDetail(); break;
		case 2: ShowHighscoreTable();          break;
	}
	return CBF_NONE;
}

/* --- Industries button menu --- */

static CallBackFunction ToolbarIndustryClick(Window *w)
{
	/* Disable build-industry menu if we are a spectator */
	PopupMainToolbMenu(w, WID_TN_INDUSTRIES, STR_INDUSTRY_MENU_INDUSTRY_DIRECTORY, (_local_company == COMPANY_SPECTATOR) ? 2 : 3);
	return CBF_NONE;
}

/**
 * Handle click on the entry in the Industry menu.
 *
 * @param index Menu entry number.
 * @return #CBF_NONE
 */
static CallBackFunction MenuClickIndustry(int index)
{
	switch (index) {
		case 0: ShowIndustryDirectory();     break;
		case 1: ShowIndustryCargoesWindow(); break;
		case 2: ShowBuildIndustryWindow();   break;
	}
	return CBF_NONE;
}

/* --- Trains button menu + 1 helper function for all vehicles. --- */

static void ToolbarVehicleClick(Window *w, VehicleType veh)
{
	int dis = ~0;

	for (const Vehicle *v : Vehicle::Iterate()) {
		if (v->type == veh && v->IsPrimaryVehicle()) ClrBit(dis, v->owner);
	}
	PopupMainCompanyToolbMenu(w, WID_TN_VEHICLE_START + veh, dis);
}


static CallBackFunction ToolbarTrainClick(Window *w)
{
	ToolbarVehicleClick(w, VEH_TRAIN);
	return CBF_NONE;
}

/**
 * Handle click on the entry in the Train menu.
 *
 * @param index CompanyID to show train list for.
 * @return #CBF_NONE
 */
static CallBackFunction MenuClickShowTrains(int index)
{
	ShowVehicleListWindow((CompanyID)index, VEH_TRAIN);
	return CBF_NONE;
}

/* --- Road vehicle button menu --- */

static CallBackFunction ToolbarRoadClick(Window *w)
{
	ToolbarVehicleClick(w, VEH_ROAD);
	return CBF_NONE;
}

/**
 * Handle click on the entry in the Road Vehicles menu.
 *
 * @param index CompanyID to show road vehicles list for.
 * @return #CBF_NONE
 */
static CallBackFunction MenuClickShowRoad(int index)
{
	ShowVehicleListWindow((CompanyID)index, VEH_ROAD);
	return CBF_NONE;
}

/* --- Ship button menu --- */

static CallBackFunction ToolbarShipClick(Window *w)
{
	ToolbarVehicleClick(w, VEH_SHIP);
	return CBF_NONE;
}

/**
 * Handle click on the entry in the Ships menu.
 *
 * @param index CompanyID to show ship list for.
 * @return #CBF_NONE
 */
static CallBackFunction MenuClickShowShips(int index)
{
	ShowVehicleListWindow((CompanyID)index, VEH_SHIP);
	return CBF_NONE;
}

/* --- Aircraft button menu --- */

static CallBackFunction ToolbarAirClick(Window *w)
{
	ToolbarVehicleClick(w, VEH_AIRCRAFT);
	return CBF_NONE;
}

/**
 * Handle click on the entry in the Aircraft menu.
 *
 * @param index CompanyID to show aircraft list for.
 * @return #CBF_NONE
 */
static CallBackFunction MenuClickShowAir(int index)
{
	ShowVehicleListWindow((CompanyID)index, VEH_AIRCRAFT);
	return CBF_NONE;
}

/* --- Zoom in button --- */

static CallBackFunction ToolbarZoomInClick(Window *w)
{
	if (DoZoomInOutWindow(ZOOM_IN, FindWindowById(WC_MAIN_WINDOW, 0))) {
		w->HandleButtonClick((_game_mode == GM_EDITOR) ? (byte)WID_TE_ZOOM_IN : (byte)WID_TN_ZOOM_IN);
		if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
	}
	return CBF_NONE;
}

/* --- Zoom out button --- */

static CallBackFunction ToolbarZoomOutClick(Window *w)
{
	if (DoZoomInOutWindow(ZOOM_OUT, FindWindowById(WC_MAIN_WINDOW, 0))) {
		w->HandleButtonClick((_game_mode == GM_EDITOR) ? (byte)WID_TE_ZOOM_OUT : (byte)WID_TN_ZOOM_OUT);
		if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
	}
	return CBF_NONE;
}

/* --- Rail button menu --- */

static CallBackFunction ToolbarBuildRailClick(Window *w)
{
	ShowDropDownList(w, GetRailTypeDropDownList(), _last_built_railtype, WID_TN_RAILS, 140, true, true);
	if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
	return CBF_NONE;
}

/**
 * Handle click on the entry in the Build Rail menu.
 *
 * @param index RailType to show the build toolbar for.
 * @return #CBF_NONE
 */
static CallBackFunction MenuClickBuildRail(int index)
{
	_last_built_railtype = (RailType)index;
	ShowBuildRailToolbar(_last_built_railtype);
	return CBF_NONE;
}

/* --- Road button menu --- */

static CallBackFunction ToolbarBuildRoadClick(Window *w)
{
	ShowDropDownList(w, GetRoadTypeDropDownList(RTTB_ROAD), _last_built_roadtype, WID_TN_ROADS, 140, true, true);
	if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
	return CBF_NONE;
}

/**
 * Handle click on the entry in the Build Road menu.
 *
 * @param index RoadType to show the build toolbar for.
 * @return #CBF_NONE
 */
static CallBackFunction MenuClickBuildRoad(int index)
{
	_last_built_roadtype = (RoadType)index;
	ShowBuildRoadToolbar(_last_built_roadtype);
	return CBF_NONE;
}

/* --- Tram button menu --- */

static CallBackFunction ToolbarBuildTramClick(Window *w)
{
	ShowDropDownList(w, GetRoadTypeDropDownList(RTTB_TRAM), _last_built_tramtype, WID_TN_TRAMS, 140, true, true);
	if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
	return CBF_NONE;
}

/**
 * Handle click on the entry in the Build Tram menu.
 *
 * @param index RoadType to show the build toolbar for.
 * @return #CBF_NONE
 */
static CallBackFunction MenuClickBuildTram(int index)
{
	_last_built_tramtype = (RoadType)index;
	ShowBuildRoadToolbar(_last_built_tramtype);
	return CBF_NONE;
}

/* --- Water button menu --- */

static CallBackFunction ToolbarBuildWaterClick(Window *w)
{
	DropDownList list;
	list.emplace_back(new DropDownListIconItem(SPR_IMG_BUILD_CANAL, PAL_NONE, STR_WATERWAYS_MENU_WATERWAYS_CONSTRUCTION, 0, false));
	ShowDropDownList(w, std::move(list), 0, WID_TN_WATER, 140, true, true);
	if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
	return CBF_NONE;
}

/**
 * Handle click on the entry in the Build Waterways menu.
 *
 * @param index Unused.
 * @return #CBF_NONE
 */
static CallBackFunction MenuClickBuildWater(int index)
{
	ShowBuildDocksToolbar();
	return CBF_NONE;
}

/* --- Airport button menu --- */

static CallBackFunction ToolbarBuildAirClick(Window *w)
{
	DropDownList list;
	list.emplace_back(new DropDownListIconItem(SPR_IMG_AIRPORT, PAL_NONE, STR_AIRCRAFT_MENU_AIRPORT_CONSTRUCTION, 0, false));
	ShowDropDownList(w, std::move(list), 0, WID_TN_AIR, 140, true, true);
	if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
	return CBF_NONE;
}

/**
 * Handle click on the entry in the Build Air menu.
 *
 * @param index Unused.
 * @return #CBF_NONE
 */
static CallBackFunction MenuClickBuildAir(int index)
{
	ShowBuildAirToolbar();
	return CBF_NONE;
}

/* --- Forest button menu --- */

static CallBackFunction ToolbarForestClick(Window *w)
{
	DropDownList list;
	list.emplace_back(new DropDownListIconItem(SPR_IMG_LANDSCAPING, PAL_NONE, STR_LANDSCAPING_MENU_LANDSCAPING, 0, false));
	list.emplace_back(new DropDownListIconItem(SPR_IMG_PLANTTREES, PAL_NONE, STR_LANDSCAPING_MENU_PLANT_TREES, 1, false));
	list.emplace_back(new DropDownListIconItem(SPR_IMG_SIGN, PAL_NONE, STR_LANDSCAPING_MENU_PLACE_SIGN, 2, false));
	ShowDropDownList(w, std::move(list), 0, WID_TN_LANDSCAPE, 100, true, true);
	if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
	return CBF_NONE;
}

/**
 * Handle click on the entry in the landscaping menu.
 *
 * @param index Menu entry clicked.
 * @return #CBF_NONE
 */
static CallBackFunction MenuClickForest(int index)
{
	switch (index) {
		case 0: ShowTerraformToolbar();  break;
		case 1: ShowBuildTreesToolbar(); break;
		case 2: return SelectSignTool();
	}
	return CBF_NONE;
}

/* --- Music button menu --- */

static CallBackFunction ToolbarMusicClick(Window *w)
{
	PopupMainToolbMenu(w, _game_mode == GM_EDITOR ? (int)WID_TE_MUSIC_SOUND : (int)WID_TN_MUSIC_SOUND, STR_TOOLBAR_SOUND_MUSIC, 1);
	return CBF_NONE;
}

/**
 * Handle click on the entry in the Music menu.
 *
 * @param index Unused.
 * @return #CBF_NONE
 */
static CallBackFunction MenuClickMusicWindow(int index)
{
	ShowMusicWindow();
	return CBF_NONE;
}

/* --- Newspaper button menu --- */

static CallBackFunction ToolbarNewspaperClick(Window *w)
{
	PopupMainToolbMenu(w, WID_TN_MESSAGES, STR_NEWS_MENU_LAST_MESSAGE_NEWS_REPORT, 3);
	return CBF_NONE;
}

/**
 * Handle click on the entry in the Newspaper menu.
 *
 * @param index Menu entry clicked.
 * @return #CBF_NONE
 */
static CallBackFunction MenuClickNewspaper(int index)
{
	switch (index) {
		case 0: ShowLastNewsMessage(); break;
		case 1: ShowMessageHistory();  break;
		case 2: DeleteAllMessages();   break;
	}
	return CBF_NONE;
}

/* --- Help button menu --- */

static CallBackFunction PlaceLandBlockInfo()
{
	if (_last_started_action == CBF_PLACE_LANDINFO) {
		ResetObjectToPlace();
		return CBF_NONE;
	} else {
		SetObjectToPlace(SPR_CURSOR_QUERY, PAL_NONE, HT_RECT, WC_MAIN_TOOLBAR, 0);
		return CBF_PLACE_LANDINFO;
	}
}

static CallBackFunction ToolbarHelpClick(Window *w)
{
	PopupMainToolbMenu(w, _game_mode == GM_EDITOR ? (int)WID_TE_HELP : (int)WID_TN_HELP, STR_ABOUT_MENU_LAND_BLOCK_INFO, _settings_client.gui.newgrf_developer_tools ? 11 : 8);
	return CBF_NONE;
}

/**
 * Toggle drawing of sprites' bounding boxes.
 * @note has only an effect when newgrf_developer_tools are active.
 *
 * Function is found here and not in viewport.cpp in order to avoid
 * importing the settings structs to there.
 */
void ToggleBoundingBoxes()
{
	extern bool _draw_bounding_boxes;
	/* Always allow to toggle them off */
	if (_settings_client.gui.newgrf_developer_tools || _draw_bounding_boxes) {
		_draw_bounding_boxes = !_draw_bounding_boxes;
		MarkWholeScreenDirty();
	}
}

/**
 * Toggle drawing of the dirty blocks.
 * @note has only an effect when newgrf_developer_tools are active.
 *
 * Function is found here and not in viewport.cpp in order to avoid
 * importing the settings structs to there.
 */
void ToggleDirtyBlocks()
{
	extern bool _draw_dirty_blocks;
	/* Always allow to toggle them off */
	if (_settings_client.gui.newgrf_developer_tools || _draw_dirty_blocks) {
		_draw_dirty_blocks = !_draw_dirty_blocks;
		MarkWholeScreenDirty();
	}
}

/**
 * Set the starting year for a scenario.
 * @param year New starting year.
 */
void SetStartingYear(Year year)
{
	_settings_game.game_creation.starting_year = Clamp(year, MIN_YEAR, MAX_YEAR);
	Date new_date = ConvertYMDToDate(_settings_game.game_creation.starting_year, 0, 1);
	/* If you open a savegame as scenario there may already be link graphs.*/
	LinkGraphSchedule::instance.ShiftDates(new_date - _date);
	SetDate(new_date, 0);
}

/**
 * Choose the proper callback function for the main toolbar's help menu.
 * @param index The menu index which was selected.
 * @return CBF_NONE
 */
static CallBackFunction MenuClickHelp(int index)
{
	switch (index) {
		case  0: return PlaceLandBlockInfo();
		case  1: ShowHelpWindow();                 break;
		case  3: IConsoleSwitch();                 break;
		case  4: ShowAIDebugWindow();              break;
		case  5: ShowScreenshotWindow();           break;
		case  6: ShowFramerateWindow();            break;
		case  7: ShowAboutWindow();                break;
		case  8: ShowSpriteAlignerWindow();        break;
		case  9: ToggleBoundingBoxes();            break;
		case 10: ToggleDirtyBlocks();              break;
	}
	return CBF_NONE;
}

/* --- Switch toolbar button --- */

static CallBackFunction ToolbarSwitchClick(Window *w)
{
	if (_toolbar_mode != TB_LOWER) {
		_toolbar_mode = TB_LOWER;
	} else {
		_toolbar_mode = TB_UPPER;
	}

	w->ReInit();
	w->SetWidgetLoweredState(_game_mode == GM_EDITOR ? (uint)WID_TE_SWITCH_BAR : (uint)WID_TN_SWITCH_BAR, _toolbar_mode == TB_LOWER);
	if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
	return CBF_NONE;
}

/* --- Scenario editor specific handlers. */

/**
 * Called when clicking at the date panel of the scenario editor toolbar.
 */
static CallBackFunction ToolbarScenDatePanel(Window *w)
{
	SetDParam(0, _settings_game.game_creation.starting_year);
	ShowQueryString(STR_JUST_INT, STR_MAPGEN_START_DATE_QUERY_CAPT, 8, w, CS_NUMERAL, QSF_ENABLE_DEFAULT);
	_left_button_clicked = false;
	return CBF_NONE;
}

static CallBackFunction ToolbarScenDateBackward(Window *w)
{
	/* don't allow too fast scrolling */
	if (!(w->flags & WF_TIMEOUT) || w->timeout_timer <= 1) {
		w->HandleButtonClick(WID_TE_DATE_BACKWARD);
		w->SetDirty();

		SetStartingYear(_settings_game.game_creation.starting_year - 1);
	}
	_left_button_clicked = false;
	return CBF_NONE;
}

static CallBackFunction ToolbarScenDateForward(Window *w)
{
	/* don't allow too fast scrolling */
	if (!(w->flags & WF_TIMEOUT) || w->timeout_timer <= 1) {
		w->HandleButtonClick(WID_TE_DATE_FORWARD);
		w->SetDirty();

		SetStartingYear(_settings_game.game_creation.starting_year + 1);
	}
	_left_button_clicked = false;
	return CBF_NONE;
}

static CallBackFunction ToolbarScenGenLand(Window *w)
{
	w->HandleButtonClick(WID_TE_LAND_GENERATE);
	if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);

	ShowEditorTerraformToolbar();
	return CBF_NONE;
}


static CallBackFunction ToolbarScenGenTown(Window *w)
{
	w->HandleButtonClick(WID_TE_TOWN_GENERATE);
	if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
	ShowFoundTownWindow();
	return CBF_NONE;
}

static CallBackFunction ToolbarScenGenIndustry(Window *w)
{
	w->HandleButtonClick(WID_TE_INDUSTRY);
	if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
	ShowBuildIndustryWindow();
	return CBF_NONE;
}

static CallBackFunction ToolbarScenBuildRoadClick(Window *w)
{
	ShowDropDownList(w, GetScenRoadTypeDropDownList(RTTB_ROAD), _last_built_roadtype, WID_TE_ROADS, 140, true, true);
	if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
	return CBF_NONE;
}

/**
 * Handle click on the entry in the Build Road menu.
 *
 * @param index RoadType to show the build toolbar for.
 * @return #CBF_NONE
 */
static CallBackFunction ToolbarScenBuildRoad(int index)
{
	_last_built_roadtype = (RoadType)index;
	ShowBuildRoadScenToolbar(_last_built_roadtype);
	return CBF_NONE;
}

static CallBackFunction ToolbarScenBuildTramClick(Window *w)
{
	ShowDropDownList(w, GetScenRoadTypeDropDownList(RTTB_TRAM), _last_built_tramtype, WID_TE_TRAMS, 140, true, true);
	if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
	return CBF_NONE;
}

/**
 * Handle click on the entry in the Build Tram menu.
 *
 * @param index RoadType to show the build toolbar for.
 * @return #CBF_NONE
 */
static CallBackFunction ToolbarScenBuildTram(int index)
{
	_last_built_tramtype = (RoadType)index;
	ShowBuildRoadScenToolbar(_last_built_tramtype);
	return CBF_NONE;
}

static CallBackFunction ToolbarScenBuildDocks(Window *w)
{
	w->HandleButtonClick(WID_TE_WATER);
	if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
	ShowBuildDocksScenToolbar();
	return CBF_NONE;
}

static CallBackFunction ToolbarScenPlantTrees(Window *w)
{
	w->HandleButtonClick(WID_TE_TREES);
	if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
	ShowBuildTreesToolbar();
	return CBF_NONE;
}

static CallBackFunction ToolbarScenPlaceSign(Window *w)
{
	w->HandleButtonClick(WID_TE_SIGNS);
	if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
	return SelectSignTool();
}

static CallBackFunction ToolbarBtn_NULL(Window *w)
{
	return CBF_NONE;
}

typedef CallBackFunction MenuClickedProc(int index);

static MenuClickedProc * const _menu_clicked_procs[] = {
	nullptr,                 // 0
	nullptr,                 // 1
	MenuClickSettings,    // 2
	MenuClickSaveLoad,    // 3
	MenuClickMap,         // 4
	MenuClickTown,        // 5
	MenuClickSubsidies,   // 6
	MenuClickStations,    // 7
	MenuClickFinances,    // 8
	MenuClickCompany,     // 9
	MenuClickStory,       // 10
	MenuClickGoal,        // 11
	MenuClickGraphs,      // 12
	MenuClickLeague,      // 13
	MenuClickIndustry,    // 14
	MenuClickShowTrains,  // 15
	MenuClickShowRoad,    // 16
	MenuClickShowShips,   // 17
	MenuClickShowAir,     // 18
	MenuClickMap,         // 19
	nullptr,                 // 20
	MenuClickBuildRail,   // 21
	MenuClickBuildRoad,   // 22
	MenuClickBuildTram,   // 23
	MenuClickBuildWater,  // 24
	MenuClickBuildAir,    // 25
	MenuClickForest,      // 26
	MenuClickMusicWindow, // 27
	MenuClickNewspaper,   // 28
	MenuClickHelp,        // 29
};

/** Full blown container to make it behave exactly as we want :) */
class NWidgetToolbarContainer : public NWidgetContainer {
	bool visible[WID_TN_END]; ///< The visible headers
protected:
	uint spacers;          ///< Number of spacer widgets in this toolbar

public:
	NWidgetToolbarContainer() : NWidgetContainer(NWID_HORIZONTAL)
	{
	}

	/**
	 * Check whether the given widget type is a button for us.
	 * @param type the widget type to check.
	 * @return true if it is a button for us.
	 */
	bool IsButton(WidgetType type) const
	{
		return type == WWT_IMGBTN || type == WWT_IMGBTN_2 || type == WWT_PUSHIMGBTN;
	}

	void SetupSmallestSize(Window *w, bool init_array) override
	{
		this->smallest_x = 0; // Biggest child
		this->smallest_y = 0; // Biggest child
		this->fill_x = 1;
		this->fill_y = 0;
		this->resize_x = 1; // We only resize in this direction
		this->resize_y = 0; // We never resize in this direction
		this->spacers = 0;

		uint nbuttons = 0;
		/* First initialise some variables... */
		for (NWidgetBase *child_wid = this->head; child_wid != nullptr; child_wid = child_wid->next) {
			child_wid->SetupSmallestSize(w, init_array);
			this->smallest_y = std::max(this->smallest_y, child_wid->smallest_y + child_wid->padding_top + child_wid->padding_bottom);
			if (this->IsButton(child_wid->type)) {
				nbuttons++;
				this->smallest_x = std::max(this->smallest_x, child_wid->smallest_x + child_wid->padding_left + child_wid->padding_right);
			} else if (child_wid->type == NWID_SPACER) {
				this->spacers++;
			}
		}

		/* ... then in a second pass make sure the 'current' heights are set. Won't change ever. */
		for (NWidgetBase *child_wid = this->head; child_wid != nullptr; child_wid = child_wid->next) {
			child_wid->current_y = this->smallest_y;
			if (!this->IsButton(child_wid->type)) {
				child_wid->current_x = child_wid->smallest_x;
			}
		}
		_toolbar_width = nbuttons * this->smallest_x;
	}

	void AssignSizePosition(SizingType sizing, uint x, uint y, uint given_width, uint given_height, bool rtl) override
	{
		assert(given_width >= this->smallest_x && given_height >= this->smallest_y);

		this->pos_x = x;
		this->pos_y = y;
		this->current_x = given_width;
		this->current_y = given_height;

		/* Figure out what are the visible buttons */
		memset(this->visible, 0, sizeof(this->visible));
		uint arrangable_count, button_count, spacer_count;
		const byte *arrangement = GetButtonArrangement(given_width, arrangable_count, button_count, spacer_count);
		for (uint i = 0; i < arrangable_count; i++) {
			this->visible[arrangement[i]] = true;
		}

		/* Create us ourselves a quick lookup table */
		NWidgetBase *widgets[WID_TN_END];
		for (NWidgetBase *child_wid = this->head; child_wid != nullptr; child_wid = child_wid->next) {
			if (child_wid->type == NWID_SPACER) continue;
			widgets[((NWidgetCore*)child_wid)->index] = child_wid;
		}

		/* Now assign the widgets to their rightful place */
		uint position = 0; // Place to put next child relative to origin of the container.
		uint spacer_space = std::max(0, (int)given_width - (int)(button_count * this->smallest_x)); // Remaining spacing for 'spacer' widgets
		uint button_space = given_width - spacer_space; // Remaining spacing for the buttons
		uint spacer_i = 0;
		uint button_i = 0;

		/* Index into the arrangement indices. The macro lastof cannot be used here! */
		const byte *cur_wid = rtl ? &arrangement[arrangable_count - 1] : arrangement;
		for (uint i = 0; i < arrangable_count; i++) {
			NWidgetBase *child_wid = widgets[*cur_wid];
			/* If we have to give space to the spacers, do that */
			if (spacer_space != 0) {
				NWidgetBase *possible_spacer = rtl ? child_wid->next : child_wid->prev;
				if (possible_spacer != nullptr && possible_spacer->type == NWID_SPACER) {
					uint add = spacer_space / (spacer_count - spacer_i);
					position += add;
					spacer_space -= add;
					spacer_i++;
				}
			}

			/* Buttons can be scaled, the others not. */
			if (this->IsButton(child_wid->type)) {
				child_wid->current_x = button_space / (button_count - button_i);
				button_space -= child_wid->current_x;
				button_i++;
			}
			child_wid->AssignSizePosition(sizing, x + position, y, child_wid->current_x, this->current_y, rtl);
			position += child_wid->current_x;

			if (rtl) {
				cur_wid--;
			} else {
				cur_wid++;
			}
		}
	}

	void Draw(const Window *w) override
	{
		/* Draw brown-red toolbar bg. */
		GfxFillRect(this->pos_x, this->pos_y, this->pos_x + this->current_x - 1, this->pos_y + this->current_y - 1, PC_VERY_DARK_RED);
		GfxFillRect(this->pos_x, this->pos_y, this->pos_x + this->current_x - 1, this->pos_y + this->current_y - 1, PC_DARK_RED, FILLRECT_CHECKER);

		bool rtl = _current_text_dir == TD_RTL;
		for (NWidgetBase *child_wid = rtl ? this->tail : this->head; child_wid != nullptr; child_wid = rtl ? child_wid->prev : child_wid->next) {
			if (child_wid->type == NWID_SPACER) continue;
			if (!this->visible[((NWidgetCore*)child_wid)->index]) continue;

			child_wid->Draw(w);
		}
	}

	NWidgetCore *GetWidgetFromPos(int x, int y) override
	{
		if (!IsInsideBS(x, this->pos_x, this->current_x) || !IsInsideBS(y, this->pos_y, this->current_y)) return nullptr;

		for (NWidgetBase *child_wid = this->head; child_wid != nullptr; child_wid = child_wid->next) {
			if (child_wid->type == NWID_SPACER) continue;
			if (!this->visible[((NWidgetCore*)child_wid)->index]) continue;

			NWidgetCore *nwid = child_wid->GetWidgetFromPos(x, y);
			if (nwid != nullptr) return nwid;
		}
		return nullptr;
	}

	/**
	 * Get the arrangement of the buttons for the toolbar.
	 * @param width the new width of the toolbar.
	 * @param arrangable_count output of the number of visible items.
	 * @param button_count output of the number of visible buttons.
	 * @param spacer_count output of the number of spacers.
	 * @return the button configuration.
	 */
	virtual const byte *GetButtonArrangement(uint &width, uint &arrangable_count, uint &button_count, uint &spacer_count) const = 0;
};

/** Container for the 'normal' main toolbar */
class NWidgetMainToolbarContainer : public NWidgetToolbarContainer {
	const byte *GetButtonArrangement(uint &width, uint &arrangable_count, uint &button_count, uint &spacer_count) const override
	{
		static const uint SMALLEST_ARRANGEMENT = 14;
		static const uint BIGGEST_ARRANGEMENT  = 20;

		/* The number of buttons of each row of the toolbar should match the number of items which we want to be visible.
		 * The total number of buttons should be equal to arrangable_count * 2.
		 * No bad things happen, but we could see strange behaviours if we have buttons < (arrangable_count * 2) like a
		 * pause button appearing on the right of the lower toolbar and weird resizing of the widgets even if there is
		 * enough space.
		 */
		static const byte arrange14[] = {
			WID_TN_PAUSE,
			WID_TN_FAST_FORWARD,
			WID_TN_TRAINS,
			WID_TN_ROADVEHS,
			WID_TN_SHIPS,
			WID_TN_AIRCRAFT,
			WID_TN_ZOOM_IN,
			WID_TN_ZOOM_OUT,
			WID_TN_RAILS,
			WID_TN_ROADS,
			WID_TN_WATER,
			WID_TN_AIR,
			WID_TN_LANDSCAPE,
			WID_TN_SWITCH_BAR,
			// lower toolbar
			WID_TN_SETTINGS,
			WID_TN_SAVE,
			WID_TN_SMALL_MAP,
			WID_TN_TOWNS,
			WID_TN_SUBSIDIES,
			WID_TN_STATIONS,
			WID_TN_FINANCES,
			WID_TN_COMPANIES,
			WID_TN_GRAPHS,
			WID_TN_INDUSTRIES,
			WID_TN_MUSIC_SOUND,
			WID_TN_MESSAGES,
			WID_TN_HELP,
			WID_TN_SWITCH_BAR,
		};
		static const byte arrange15[] = {
			WID_TN_PAUSE,
			WID_TN_FAST_FORWARD,
			WID_TN_SMALL_MAP,
			WID_TN_TRAINS,
			WID_TN_ROADVEHS,
			WID_TN_SHIPS,
			WID_TN_AIRCRAFT,
			WID_TN_RAILS,
			WID_TN_ROADS,
			WID_TN_WATER,
			WID_TN_AIR,
			WID_TN_LANDSCAPE,
			WID_TN_ZOOM_IN,
			WID_TN_ZOOM_OUT,
			WID_TN_SWITCH_BAR,
			// lower toolbar
			WID_TN_PAUSE,
			WID_TN_SETTINGS,
			WID_TN_SMALL_MAP,
			WID_TN_SAVE,
			WID_TN_TOWNS,
			WID_TN_SUBSIDIES,
			WID_TN_STATIONS,
			WID_TN_FINANCES,
			WID_TN_COMPANIES,
			WID_TN_GRAPHS,
			WID_TN_INDUSTRIES,
			WID_TN_MUSIC_SOUND,
			WID_TN_MESSAGES,
			WID_TN_HELP,
			WID_TN_SWITCH_BAR,
		};
		static const byte arrange16[] = {
			WID_TN_PAUSE,
			WID_TN_FAST_FORWARD,
			WID_TN_SETTINGS,
			WID_TN_SMALL_MAP,
			WID_TN_TRAINS,
			WID_TN_ROADVEHS,
			WID_TN_SHIPS,
			WID_TN_AIRCRAFT,
			WID_TN_RAILS,
			WID_TN_ROADS,
			WID_TN_WATER,
			WID_TN_AIR,
			WID_TN_LANDSCAPE,
			WID_TN_ZOOM_IN,
			WID_TN_ZOOM_OUT,
			WID_TN_SWITCH_BAR,
			// lower toolbar
			WID_TN_PAUSE,
			WID_TN_FAST_FORWARD,
			WID_TN_SAVE,
			WID_TN_TOWNS,
			WID_TN_SUBSIDIES,
			WID_TN_STATIONS,
			WID_TN_FINANCES,
			WID_TN_COMPANIES,
			WID_TN_GRAPHS,
			WID_TN_INDUSTRIES,
			WID_TN_MUSIC_SOUND,
			WID_TN_MESSAGES,
			WID_TN_HELP,
			WID_TN_ZOOM_IN,
			WID_TN_ZOOM_OUT,
			WID_TN_SWITCH_BAR,
		};
		static const byte arrange17[] = {
			WID_TN_PAUSE,
			WID_TN_FAST_FORWARD,
			WID_TN_SETTINGS,
			WID_TN_SMALL_MAP,
			WID_TN_SUBSIDIES,
			WID_TN_TRAINS,
			WID_TN_ROADVEHS,
			WID_TN_SHIPS,
			WID_TN_AIRCRAFT,
			WID_TN_RAILS,
			WID_TN_ROADS,
			WID_TN_WATER,
			WID_TN_AIR,
			WID_TN_LANDSCAPE,
			WID_TN_ZOOM_IN,
			WID_TN_ZOOM_OUT,
			WID_TN_SWITCH_BAR,
			// lower toolbar
			WID_TN_PAUSE,
			WID_TN_FAST_FORWARD,
			WID_TN_SAVE,
			WID_TN_SMALL_MAP,
			WID_TN_SUBSIDIES,
			WID_TN_TOWNS,
			WID_TN_STATIONS,
			WID_TN_FINANCES,
			WID_TN_COMPANIES,
			WID_TN_GRAPHS,
			WID_TN_INDUSTRIES,
			WID_TN_MUSIC_SOUND,
			WID_TN_MESSAGES,
			WID_TN_HELP,
			WID_TN_ZOOM_IN,
			WID_TN_ZOOM_OUT,
			WID_TN_SWITCH_BAR,
		};
		static const byte arrange18[] = {
			WID_TN_PAUSE,
			WID_TN_FAST_FORWARD,
			WID_TN_SETTINGS,
			WID_TN_SMALL_MAP,
			WID_TN_TOWNS,
			WID_TN_SUBSIDIES,
			WID_TN_STATIONS,
			WID_TN_FINANCES,
			WID_TN_COMPANIES,
			WID_TN_INDUSTRIES,
			WID_TN_RAILS,
			WID_TN_ROADS,
			WID_TN_WATER,
			WID_TN_AIR,
			WID_TN_LANDSCAPE,
			WID_TN_ZOOM_IN,
			WID_TN_ZOOM_OUT,
			WID_TN_SWITCH_BAR,
			// lower toolbar
			WID_TN_PAUSE,
			WID_TN_FAST_FORWARD,
			WID_TN_SAVE,
			WID_TN_SMALL_MAP,
			WID_TN_TOWNS,
			WID_TN_SUBSIDIES,
			WID_TN_STATIONS,
			WID_TN_GRAPHS,
			WID_TN_TRAINS,
			WID_TN_ROADVEHS,
			WID_TN_SHIPS,
			WID_TN_AIRCRAFT,
			WID_TN_MUSIC_SOUND,
			WID_TN_MESSAGES,
			WID_TN_HELP,
			WID_TN_ZOOM_IN,
			WID_TN_ZOOM_OUT,
			WID_TN_SWITCH_BAR,
		};
		static const byte arrange19[] = {
			WID_TN_PAUSE,
			WID_TN_FAST_FORWARD,
			WID_TN_SETTINGS,
			WID_TN_SMALL_MAP,
			WID_TN_TOWNS,
			WID_TN_SUBSIDIES,
			WID_TN_TRAINS,
			WID_TN_ROADVEHS,
			WID_TN_SHIPS,
			WID_TN_AIRCRAFT,
			WID_TN_RAILS,
			WID_TN_ROADS,
			WID_TN_WATER,
			WID_TN_AIR,
			WID_TN_LANDSCAPE,
			WID_TN_MUSIC_SOUND,
			WID_TN_ZOOM_IN,
			WID_TN_ZOOM_OUT,
			WID_TN_SWITCH_BAR,
			// lower toolbar
			WID_TN_PAUSE,
			WID_TN_FAST_FORWARD,
			WID_TN_SAVE,
			WID_TN_SMALL_MAP,
			WID_TN_STATIONS,
			WID_TN_FINANCES,
			WID_TN_COMPANIES,
			WID_TN_GRAPHS,
			WID_TN_INDUSTRIES,
			WID_TN_MESSAGES,
			WID_TN_RAILS,
			WID_TN_ROADS,
			WID_TN_WATER,
			WID_TN_AIR,
			WID_TN_LANDSCAPE,
			WID_TN_HELP,
			WID_TN_ZOOM_IN,
			WID_TN_ZOOM_OUT,
			WID_TN_SWITCH_BAR,
		};
		static const byte arrange20[] = {
			WID_TN_PAUSE,
			WID_TN_FAST_FORWARD,
			WID_TN_SETTINGS,
			WID_TN_SMALL_MAP,
			WID_TN_TOWNS,
			WID_TN_SUBSIDIES,
			WID_TN_TRAINS,
			WID_TN_ROADVEHS,
			WID_TN_SHIPS,
			WID_TN_AIRCRAFT,
			WID_TN_RAILS,
			WID_TN_ROADS,
			WID_TN_WATER,
			WID_TN_AIR,
			WID_TN_LANDSCAPE,
			WID_TN_MUSIC_SOUND,
			WID_TN_GOAL,
			WID_TN_ZOOM_IN,
			WID_TN_ZOOM_OUT,
			WID_TN_SWITCH_BAR,
			// lower toolbar
			WID_TN_PAUSE,
			WID_TN_FAST_FORWARD,
			WID_TN_SAVE,
			WID_TN_SMALL_MAP,
			WID_TN_STATIONS,
			WID_TN_FINANCES,
			WID_TN_COMPANIES,
			WID_TN_GRAPHS,
			WID_TN_INDUSTRIES,
			WID_TN_MESSAGES,
			WID_TN_RAILS,
			WID_TN_ROADS,
			WID_TN_WATER,
			WID_TN_AIR,
			WID_TN_LANDSCAPE,
			WID_TN_STORY,
			WID_TN_HELP,
			WID_TN_ZOOM_IN,
			WID_TN_ZOOM_OUT,
			WID_TN_SWITCH_BAR,
		};
		static const byte arrange_all[] = {
			WID_TN_PAUSE,
			WID_TN_FAST_FORWARD,
			WID_TN_SETTINGS,
			WID_TN_SAVE,
			WID_TN_SMALL_MAP,
			WID_TN_TOWNS,
			WID_TN_SUBSIDIES,
			WID_TN_STATIONS,
			WID_TN_FINANCES,
			WID_TN_COMPANIES,
			WID_TN_STORY,
			WID_TN_GOAL,
			WID_TN_GRAPHS,
			WID_TN_LEAGUE,
			WID_TN_INDUSTRIES,
			WID_TN_TRAINS,
			WID_TN_ROADVEHS,
			WID_TN_SHIPS,
			WID_TN_AIRCRAFT,
			WID_TN_ZOOM_IN,
			WID_TN_ZOOM_OUT,
			WID_TN_RAILS,
			WID_TN_ROADS,
			WID_TN_TRAMS,
			WID_TN_WATER,
			WID_TN_AIR,
			WID_TN_LANDSCAPE,
			WID_TN_MUSIC_SOUND,
			WID_TN_MESSAGES,
			WID_TN_HELP
		};

		/* If at least BIGGEST_ARRANGEMENT fit, just spread all the buttons nicely */
		uint full_buttons = std::max(CeilDiv(width, this->smallest_x), SMALLEST_ARRANGEMENT);
		if (full_buttons > BIGGEST_ARRANGEMENT) {
			button_count = arrangable_count = lengthof(arrange_all);
			spacer_count = this->spacers;
			return arrange_all;
		}

		/* Introduce the split toolbar */
		static const byte * const arrangements[] = { arrange14, arrange15, arrange16, arrange17, arrange18, arrange19, arrange20 };

		button_count = arrangable_count = full_buttons;
		spacer_count = this->spacers;
		return arrangements[full_buttons - SMALLEST_ARRANGEMENT] + ((_toolbar_mode == TB_LOWER) ? full_buttons : 0);
	}
};

/** Container for the scenario editor's toolbar */
class NWidgetScenarioToolbarContainer : public NWidgetToolbarContainer {
	uint panel_widths[2]; ///< The width of the two panels (the text panel and date panel)

	void SetupSmallestSize(Window *w, bool init_array) override
	{
		this->NWidgetToolbarContainer::SetupSmallestSize(w, init_array);

		/* Find the size of panel_widths */
		uint i = 0;
		for (NWidgetBase *child_wid = this->head; child_wid != nullptr; child_wid = child_wid->next) {
			if (child_wid->type == NWID_SPACER || this->IsButton(child_wid->type)) continue;

			assert(i < lengthof(this->panel_widths));
			this->panel_widths[i++] = child_wid->current_x;
			_toolbar_width += child_wid->current_x;
		}
	}

	const byte *GetButtonArrangement(uint &width, uint &arrangable_count, uint &button_count, uint &spacer_count) const override
	{
		static const byte arrange_all[] = {
			WID_TE_PAUSE,
			WID_TE_FAST_FORWARD,
			WID_TE_SETTINGS,
			WID_TE_SAVE,
			WID_TE_SPACER,
			WID_TE_DATE_PANEL,
			WID_TE_SMALL_MAP,
			WID_TE_ZOOM_IN,
			WID_TE_ZOOM_OUT,
			WID_TE_LAND_GENERATE,
			WID_TE_TOWN_GENERATE,
			WID_TE_INDUSTRY,
			WID_TE_ROADS,
			WID_TE_TRAMS,
			WID_TE_WATER,
			WID_TE_TREES,
			WID_TE_SIGNS,
			WID_TE_MUSIC_SOUND,
			WID_TE_HELP,
		};
		static const byte arrange_nopanel[] = {
			WID_TE_PAUSE,
			WID_TE_FAST_FORWARD,
			WID_TE_SETTINGS,
			WID_TE_SAVE,
			WID_TE_DATE_PANEL,
			WID_TE_SMALL_MAP,
			WID_TE_ZOOM_IN,
			WID_TE_ZOOM_OUT,
			WID_TE_LAND_GENERATE,
			WID_TE_TOWN_GENERATE,
			WID_TE_INDUSTRY,
			WID_TE_ROADS,
			WID_TE_TRAMS,
			WID_TE_WATER,
			WID_TE_TREES,
			WID_TE_SIGNS,
			WID_TE_MUSIC_SOUND,
			WID_TE_HELP,
		};
		static const byte arrange_switch[] = {
			WID_TE_DATE_PANEL,
			WID_TE_SMALL_MAP,
			WID_TE_LAND_GENERATE,
			WID_TE_TOWN_GENERATE,
			WID_TE_INDUSTRY,
			WID_TE_ROADS,
			WID_TE_TRAMS,
			WID_TE_WATER,
			WID_TE_TREES,
			WID_TE_SIGNS,
			WID_TE_SWITCH_BAR,
			// lower toolbar
			WID_TE_PAUSE,
			WID_TE_FAST_FORWARD,
			WID_TE_SETTINGS,
			WID_TE_SAVE,
			WID_TE_DATE_PANEL,
			WID_TE_SMALL_MAP,
			WID_TE_ZOOM_IN,
			WID_TE_ZOOM_OUT,
			WID_TE_MUSIC_SOUND,
			WID_TE_HELP,
			WID_TE_SWITCH_BAR,
		};

		/* If we can place all buttons *and* the panels, show them. */
		uint min_full_width = (lengthof(arrange_all) - lengthof(this->panel_widths)) * this->smallest_x + this->panel_widths[0] + this->panel_widths[1];
		if (width >= min_full_width) {
			width -= this->panel_widths[0] + this->panel_widths[1];
			arrangable_count = lengthof(arrange_all);
			button_count = arrangable_count - 2;
			spacer_count = this->spacers;
			return arrange_all;
		}

		/* Otherwise don't show the date panel and if we can't fit half the buttons and the panels anymore, split the toolbar in two */
		uint min_small_width = (lengthof(arrange_switch) - lengthof(this->panel_widths)) * this->smallest_x / 2 + this->panel_widths[1];
		if (width > min_small_width) {
			width -= this->panel_widths[1];
			arrangable_count = lengthof(arrange_nopanel);
			button_count = arrangable_count - 1;
			spacer_count = this->spacers - 1;
			return arrange_nopanel;
		}

		/* Split toolbar */
		width -= this->panel_widths[1];
		arrangable_count = lengthof(arrange_switch) / 2;
		button_count = arrangable_count - 1;
		spacer_count = 0;
		return arrange_switch + ((_toolbar_mode == TB_LOWER) ? arrangable_count : 0);
	}
};

/* --- Toolbar handling for the 'normal' case */

typedef CallBackFunction ToolbarButtonProc(Window *w);

static ToolbarButtonProc * const _toolbar_button_procs[] = {
	ToolbarPauseClick,
	ToolbarFastForwardClick,
	ToolbarOptionsClick,
	ToolbarSaveClick,
	ToolbarMapClick,
	ToolbarTownClick,
	ToolbarSubsidiesClick,
	ToolbarStationsClick,
	ToolbarFinancesClick,
	ToolbarCompaniesClick,
	ToolbarStoryClick,
	ToolbarGoalClick,
	ToolbarGraphsClick,
	ToolbarLeagueClick,
	ToolbarIndustryClick,
	ToolbarTrainClick,
	ToolbarRoadClick,
	ToolbarShipClick,
	ToolbarAirClick,
	ToolbarZoomInClick,
	ToolbarZoomOutClick,
	ToolbarBuildRailClick,
	ToolbarBuildRoadClick,
	ToolbarBuildTramClick,
	ToolbarBuildWaterClick,
	ToolbarBuildAirClick,
	ToolbarForestClick,
	ToolbarMusicClick,
	ToolbarNewspaperClick,
	ToolbarHelpClick,
	ToolbarSwitchClick,
};

enum MainToolbarHotkeys {
	MTHK_PAUSE,
	MTHK_FASTFORWARD,
	MTHK_SETTINGS,
	MTHK_SAVEGAME,
	MTHK_LOADGAME,
	MTHK_SMALLMAP,
	MTHK_TOWNDIRECTORY,
	MTHK_SUBSIDIES,
	MTHK_STATIONS,
	MTHK_FINANCES,
	MTHK_COMPANIES,
	MTHK_STORY,
	MTHK_GOAL,
	MTHK_GRAPHS,
	MTHK_LEAGUE,
	MTHK_INDUSTRIES,
	MTHK_TRAIN_LIST,
	MTHK_ROADVEH_LIST,
	MTHK_SHIP_LIST,
	MTHK_AIRCRAFT_LIST,
	MTHK_ZOOM_IN,
	MTHK_ZOOM_OUT,
	MTHK_BUILD_RAIL,
	MTHK_BUILD_ROAD,
	MTHK_BUILD_TRAM,
	MTHK_BUILD_DOCKS,
	MTHK_BUILD_AIRPORT,
	MTHK_BUILD_TREES,
	MTHK_MUSIC,
	MTHK_LANDINFO,
	MTHK_AI_DEBUG,
	MTHK_SMALL_SCREENSHOT,
	MTHK_ZOOMEDIN_SCREENSHOT,
	MTHK_DEFAULTZOOM_SCREENSHOT,
	MTHK_GIANT_SCREENSHOT,
	MTHK_CHEATS,
	MTHK_TERRAFORM,
	MTHK_EXTRA_VIEWPORT,
	MTHK_CLIENT_LIST,
	MTHK_SIGN_LIST,
};

/** Main toolbar. */
struct MainToolbarWindow : Window {
	GUITimer timer;

	MainToolbarWindow(WindowDesc *desc) : Window(desc)
	{
		this->InitNested(0);

		_last_started_action = CBF_NONE;
		CLRBITS(this->flags, WF_WHITE_BORDER);
		this->SetWidgetDisabledState(WID_TN_PAUSE, _networking && !_network_server); // if not server, disable pause button
		this->SetWidgetDisabledState(WID_TN_FAST_FORWARD, _networking); // if networking, disable fast-forward button
		PositionMainToolbar(this);
		DoZoomInOutWindow(ZOOM_NONE, this);

		this->timer.SetInterval(MILLISECONDS_PER_TICK);
	}

	void FindWindowPlacementAndResize(int def_width, int def_height) override
	{
		Window::FindWindowPlacementAndResize(_toolbar_width, def_height);
	}

	void OnPaint() override
	{
		/* If spectator, disable all construction buttons
		 * ie : Build road, rail, ships, airports and landscaping
		 * Since enabled state is the default, just disable when needed */
		this->SetWidgetsDisabledState(_local_company == COMPANY_SPECTATOR, WID_TN_RAILS, WID_TN_ROADS, WID_TN_TRAMS, WID_TN_WATER, WID_TN_AIR, WID_TN_LANDSCAPE, WIDGET_LIST_END);
		/* disable company list drop downs, if there are no companies */
		this->SetWidgetsDisabledState(Company::GetNumItems() == 0, WID_TN_STATIONS, WID_TN_FINANCES, WID_TN_TRAINS, WID_TN_ROADVEHS, WID_TN_SHIPS, WID_TN_AIRCRAFT, WIDGET_LIST_END);

		this->SetWidgetDisabledState(WID_TN_GOAL, Goal::GetNumItems() == 0);
		this->SetWidgetDisabledState(WID_TN_STORY, StoryPage::GetNumItems() == 0);

		this->DrawWidgets();
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		if (_game_mode != GM_MENU && !this->IsWidgetDisabled(widget)) _toolbar_button_procs[widget](this);
	}

	void OnDropdownSelect(int widget, int index) override
	{
		CallBackFunction cbf = _menu_clicked_procs[widget](index);
		if (cbf != CBF_NONE) _last_started_action = cbf;
	}

	EventState OnHotkey(int hotkey) override
	{
		CallBackFunction cbf = CBF_NONE;
		switch (hotkey) {
			case MTHK_PAUSE: ToolbarPauseClick(this); break;
			case MTHK_FASTFORWARD: ToolbarFastForwardClick(this); break;
			case MTHK_SETTINGS: ShowGameOptions(); break;
			case MTHK_SAVEGAME: MenuClickSaveLoad(); break;
			case MTHK_LOADGAME: ShowSaveLoadDialog(FT_SAVEGAME, SLO_LOAD); break;
			case MTHK_SMALLMAP: ShowSmallMap(); break;
			case MTHK_TOWNDIRECTORY: ShowTownDirectory(); break;
			case MTHK_SUBSIDIES: ShowSubsidiesList(); break;
			case MTHK_STATIONS: ShowCompanyStations(_local_company); break;
			case MTHK_FINANCES: ShowCompanyFinances(_local_company); break;
			case MTHK_COMPANIES: ShowCompany(_local_company); break;
			case MTHK_STORY: ShowStoryBook(_local_company); break;
			case MTHK_GOAL: ShowGoalsList(_local_company); break;
			case MTHK_GRAPHS: ShowOperatingProfitGraph(); break;
			case MTHK_LEAGUE: ShowCompanyLeagueTable(); break;
			case MTHK_INDUSTRIES: ShowBuildIndustryWindow(); break;
			case MTHK_TRAIN_LIST: ShowVehicleListWindow(_local_company, VEH_TRAIN); break;
			case MTHK_ROADVEH_LIST: ShowVehicleListWindow(_local_company, VEH_ROAD); break;
			case MTHK_SHIP_LIST: ShowVehicleListWindow(_local_company, VEH_SHIP); break;
			case MTHK_AIRCRAFT_LIST: ShowVehicleListWindow(_local_company, VEH_AIRCRAFT); break;
			case MTHK_ZOOM_IN: ToolbarZoomInClick(this); break;
			case MTHK_ZOOM_OUT: ToolbarZoomOutClick(this); break;
			case MTHK_BUILD_RAIL: ShowBuildRailToolbar(_last_built_railtype); break;
			case MTHK_BUILD_ROAD: ShowBuildRoadToolbar(_last_built_roadtype); break;
			case MTHK_BUILD_TRAM: ShowBuildRoadToolbar(_last_built_tramtype); break;
			case MTHK_BUILD_DOCKS: ShowBuildDocksToolbar(); break;
			case MTHK_BUILD_AIRPORT: ShowBuildAirToolbar(); break;
			case MTHK_BUILD_TREES: ShowBuildTreesToolbar(); break;
			case MTHK_MUSIC: ShowMusicWindow(); break;
			case MTHK_AI_DEBUG: ShowAIDebugWindow(); break;
			case MTHK_SMALL_SCREENSHOT: MakeScreenshotWithConfirm(SC_VIEWPORT); break;
			case MTHK_ZOOMEDIN_SCREENSHOT: MakeScreenshotWithConfirm(SC_ZOOMEDIN); break;
			case MTHK_DEFAULTZOOM_SCREENSHOT: MakeScreenshotWithConfirm(SC_DEFAULTZOOM); break;
			case MTHK_GIANT_SCREENSHOT: MakeScreenshotWithConfirm(SC_WORLD); break;
			case MTHK_CHEATS: if (!_networking) ShowCheatWindow(); break;
			case MTHK_TERRAFORM: ShowTerraformToolbar(); break;
			case MTHK_EXTRA_VIEWPORT: ShowExtraViewportWindowForTileUnderCursor(); break;
			case MTHK_CLIENT_LIST: if (_networking) ShowClientList(); break;
			case MTHK_SIGN_LIST: ShowSignList(); break;
			case MTHK_LANDINFO: cbf = PlaceLandBlockInfo(); break;
			default: return ES_NOT_HANDLED;
		}
		if (cbf != CBF_NONE) _last_started_action = cbf;
		return ES_HANDLED;
	}

	void OnPlaceObject(Point pt, TileIndex tile) override
	{
		switch (_last_started_action) {
			case CBF_PLACE_SIGN:
				PlaceProc_Sign(tile);
				break;

			case CBF_PLACE_LANDINFO:
				ShowLandInfo(tile);
				break;

			default: NOT_REACHED();
		}
	}

	void OnPlaceObjectAbort() override
	{
		_last_started_action = CBF_NONE;
	}

	void OnRealtimeTick(uint delta_ms) override
	{
		if (!this->timer.Elapsed(delta_ms)) return;
		this->timer.SetInterval(MILLISECONDS_PER_TICK);

		if (this->IsWidgetLowered(WID_TN_PAUSE) != !!_pause_mode) {
			this->ToggleWidgetLoweredState(WID_TN_PAUSE);
			this->SetWidgetDirty(WID_TN_PAUSE);
		}

		if (this->IsWidgetLowered(WID_TN_FAST_FORWARD) != (_game_speed != 100)) {
			this->ToggleWidgetLoweredState(WID_TN_FAST_FORWARD);
			this->SetWidgetDirty(WID_TN_FAST_FORWARD);
		}
	}

	void OnTimeout() override
	{
		/* We do not want to automatically raise the pause, fast forward and
		 * switchbar buttons; they have to stay down when pressed etc. */
		for (uint i = WID_TN_SETTINGS; i < WID_TN_SWITCH_BAR; i++) {
			if (this->IsWidgetLowered(i)) {
				this->RaiseWidget(i);
				this->SetWidgetDirty(i);
			}
		}
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (!gui_scope) return;
		if (FindWindowById(WC_MAIN_WINDOW, 0) != nullptr) HandleZoomMessage(this, FindWindowById(WC_MAIN_WINDOW, 0)->viewport, WID_TN_ZOOM_IN, WID_TN_ZOOM_OUT);
	}

	static HotkeyList hotkeys;
};

const uint16 _maintoolbar_pause_keys[] = {WKC_F1, WKC_PAUSE, 0};
const uint16 _maintoolbar_zoomin_keys[] = {WKC_NUM_PLUS, WKC_EQUALS, WKC_SHIFT | WKC_EQUALS, WKC_SHIFT | WKC_F5, 0};
const uint16 _maintoolbar_zoomout_keys[] = {WKC_NUM_MINUS, WKC_MINUS, WKC_SHIFT | WKC_MINUS, WKC_SHIFT | WKC_F6, 0};
const uint16 _maintoolbar_smallmap_keys[] = {WKC_F4, 'M', 0};

static Hotkey maintoolbar_hotkeys[] = {
	Hotkey(_maintoolbar_pause_keys, "pause", MTHK_PAUSE),
	Hotkey((uint16)0, "fastforward", MTHK_FASTFORWARD),
	Hotkey(WKC_F2, "settings", MTHK_SETTINGS),
	Hotkey(WKC_F3, "saveload", MTHK_SAVEGAME),
	Hotkey((uint16)0, "load_game", MTHK_LOADGAME),
	Hotkey(_maintoolbar_smallmap_keys, "smallmap", MTHK_SMALLMAP),
	Hotkey(WKC_F5, "town_list", MTHK_TOWNDIRECTORY),
	Hotkey(WKC_F6, "subsidies", MTHK_SUBSIDIES),
	Hotkey(WKC_F7, "station_list", MTHK_STATIONS),
	Hotkey(WKC_F8, "finances", MTHK_FINANCES),
	Hotkey(WKC_F9, "companies", MTHK_COMPANIES),
	Hotkey((uint16)0, "story_book", MTHK_STORY),
	Hotkey((uint16)0, "goal_list", MTHK_GOAL),
	Hotkey(WKC_F10, "graphs", MTHK_GRAPHS),
	Hotkey(WKC_F11, "league", MTHK_LEAGUE),
	Hotkey(WKC_F12, "industry_list", MTHK_INDUSTRIES),
	Hotkey(WKC_SHIFT | WKC_F1, "train_list", MTHK_TRAIN_LIST),
	Hotkey(WKC_SHIFT | WKC_F2, "roadveh_list", MTHK_ROADVEH_LIST),
	Hotkey(WKC_SHIFT | WKC_F3, "ship_list", MTHK_SHIP_LIST),
	Hotkey(WKC_SHIFT | WKC_F4, "aircraft_list", MTHK_AIRCRAFT_LIST),
	Hotkey(_maintoolbar_zoomin_keys, "zoomin", MTHK_ZOOM_IN),
	Hotkey(_maintoolbar_zoomout_keys, "zoomout", MTHK_ZOOM_OUT),
	Hotkey(WKC_SHIFT | WKC_F7, "build_rail", MTHK_BUILD_RAIL),
	Hotkey(WKC_SHIFT | WKC_F8, "build_road", MTHK_BUILD_ROAD),
	Hotkey((uint16)0, "build_tram", MTHK_BUILD_TRAM),
	Hotkey(WKC_SHIFT | WKC_F9, "build_docks", MTHK_BUILD_DOCKS),
	Hotkey(WKC_SHIFT | WKC_F10, "build_airport", MTHK_BUILD_AIRPORT),
	Hotkey(WKC_SHIFT | WKC_F11, "build_trees", MTHK_BUILD_TREES),
	Hotkey(WKC_SHIFT | WKC_F12, "music", MTHK_MUSIC),
	Hotkey((uint16)0, "ai_debug", MTHK_AI_DEBUG),
	Hotkey(WKC_CTRL  | 'S', "small_screenshot", MTHK_SMALL_SCREENSHOT),
	Hotkey(WKC_CTRL  | 'P', "zoomedin_screenshot", MTHK_ZOOMEDIN_SCREENSHOT),
	Hotkey(WKC_CTRL  | 'D', "defaultzoom_screenshot", MTHK_DEFAULTZOOM_SCREENSHOT),
	Hotkey((uint16)0, "giant_screenshot", MTHK_GIANT_SCREENSHOT),
	Hotkey(WKC_CTRL | WKC_ALT | 'C', "cheats", MTHK_CHEATS),
	Hotkey('L', "terraform", MTHK_TERRAFORM),
	Hotkey('V', "extra_viewport", MTHK_EXTRA_VIEWPORT),
	Hotkey((uint16)0, "client_list", MTHK_CLIENT_LIST),
	Hotkey((uint16)0, "sign_list", MTHK_SIGN_LIST),
	Hotkey((uint16)0, "land_info", MTHK_LANDINFO),
	HOTKEY_LIST_END
};
HotkeyList MainToolbarWindow::hotkeys("maintoolbar", maintoolbar_hotkeys);

static NWidgetBase *MakeMainToolbar(int *biggest_index)
{
	/** Sprites to use for the different toolbar buttons */
	static const SpriteID toolbar_button_sprites[] = {
		SPR_IMG_PAUSE,           // WID_TN_PAUSE
		SPR_IMG_FASTFORWARD,     // WID_TN_FAST_FORWARD
		SPR_IMG_SETTINGS,        // WID_TN_SETTINGS
		SPR_IMG_SAVE,            // WID_TN_SAVE
		SPR_IMG_SMALLMAP,        // WID_TN_SMALL_MAP
		SPR_IMG_TOWN,            // WID_TN_TOWNS
		SPR_IMG_SUBSIDIES,       // WID_TN_SUBSIDIES
		SPR_IMG_COMPANY_LIST,    // WID_TN_STATIONS
		SPR_IMG_COMPANY_FINANCE, // WID_TN_FINANCES
		SPR_IMG_COMPANY_GENERAL, // WID_TN_COMPANIES
		SPR_IMG_STORY_BOOK,      // WID_TN_STORY
		SPR_IMG_GOAL,            // WID_TN_GOAL
		SPR_IMG_GRAPHS,          // WID_TN_GRAPHS
		SPR_IMG_COMPANY_LEAGUE,  // WID_TN_LEAGUE
		SPR_IMG_INDUSTRY,        // WID_TN_INDUSTRIES
		SPR_IMG_TRAINLIST,       // WID_TN_TRAINS
		SPR_IMG_TRUCKLIST,       // WID_TN_ROADVEHS
		SPR_IMG_SHIPLIST,        // WID_TN_SHIPS
		SPR_IMG_AIRPLANESLIST,   // WID_TN_AIRCRAFT
		SPR_IMG_ZOOMIN,          // WID_TN_ZOOMIN
		SPR_IMG_ZOOMOUT,         // WID_TN_ZOOMOUT
		SPR_IMG_BUILDRAIL,       // WID_TN_RAILS
		SPR_IMG_BUILDROAD,       // WID_TN_ROADS
		SPR_IMG_BUILDTRAMS,      // WID_TN_TRAMS
		SPR_IMG_BUILDWATER,      // WID_TN_WATER
		SPR_IMG_BUILDAIR,        // WID_TN_AIR
		SPR_IMG_LANDSCAPING,     // WID_TN_LANDSCAPE
		SPR_IMG_MUSIC,           // WID_TN_MUSIC_SOUND
		SPR_IMG_MESSAGES,        // WID_TN_MESSAGES
		SPR_IMG_QUERY,           // WID_TN_HELP
		SPR_IMG_SWITCH_TOOLBAR,  // WID_TN_SWITCH_BAR
	};

	NWidgetMainToolbarContainer *hor = new NWidgetMainToolbarContainer();
	for (uint i = 0; i < WID_TN_END; i++) {
		switch (i) {
			case WID_TN_SMALL_MAP:
			case WID_TN_FINANCES:
			case WID_TN_VEHICLE_START:
			case WID_TN_ZOOM_IN:
			case WID_TN_BUILDING_TOOLS_START:
			case WID_TN_MUSIC_SOUND:
				hor->Add(new NWidgetSpacer(0, 0));
				break;
		}
		hor->Add(new NWidgetLeaf(i == WID_TN_SAVE ? WWT_IMGBTN_2 : WWT_IMGBTN, COLOUR_GREY, i, toolbar_button_sprites[i], STR_TOOLBAR_TOOLTIP_PAUSE_GAME + i));
	}

	*biggest_index = std::max<int>(*biggest_index, WID_TN_SWITCH_BAR);
	return hor;
}

static const NWidgetPart _nested_toolbar_normal_widgets[] = {
	NWidgetFunction(MakeMainToolbar),
};

static WindowDesc _toolb_normal_desc(
	WDP_MANUAL, nullptr, 0, 0,
	WC_MAIN_TOOLBAR, WC_NONE,
	WDF_NO_FOCUS,
	_nested_toolbar_normal_widgets, lengthof(_nested_toolbar_normal_widgets),
	&MainToolbarWindow::hotkeys
);


/* --- Toolbar handling for the scenario editor */

static MenuClickedProc * const _scen_toolbar_dropdown_procs[] = {
	nullptr,              // 0
	nullptr,              // 1
	MenuClickSettings,    // 2
	MenuClickSaveLoad,    // 3
	nullptr,              // 4
	nullptr,              // 5
	nullptr,              // 6
	nullptr,              // 7
	MenuClickMap,         // 8
	nullptr,              // 9
	nullptr,              // 10
	nullptr,              // 11
	nullptr,              // 12
	nullptr,              // 13
	ToolbarScenBuildRoad, // 14
	ToolbarScenBuildTram, // 15
	nullptr,              // 16
	nullptr,              // 17
	nullptr,              // 18
	nullptr,              // 19
	MenuClickMusicWindow, // 20
	MenuClickHelp,        // 21
	nullptr,              // 22
};

static ToolbarButtonProc * const _scen_toolbar_button_procs[] = {
	ToolbarPauseClick,
	ToolbarFastForwardClick,
	ToolbarOptionsClick,
	ToolbarScenSaveOrLoad,
	ToolbarBtn_NULL,
	ToolbarScenDatePanel,
	ToolbarScenDateBackward,
	ToolbarScenDateForward,
	ToolbarScenMapTownDir,
	ToolbarZoomInClick,
	ToolbarZoomOutClick,
	ToolbarScenGenLand,
	ToolbarScenGenTown,
	ToolbarScenGenIndustry,
	ToolbarScenBuildRoadClick,
	ToolbarScenBuildTramClick,
	ToolbarScenBuildDocks,
	ToolbarScenPlantTrees,
	ToolbarScenPlaceSign,
	ToolbarBtn_NULL,
	ToolbarMusicClick,
	ToolbarHelpClick,
	ToolbarSwitchClick,
};

enum MainToolbarEditorHotkeys {
	MTEHK_PAUSE,
	MTEHK_FASTFORWARD,
	MTEHK_SETTINGS,
	MTEHK_SAVEGAME,
	MTEHK_GENLAND,
	MTEHK_GENTOWN,
	MTEHK_GENINDUSTRY,
	MTEHK_BUILD_ROAD,
	MTEHK_BUILD_TRAM,
	MTEHK_BUILD_DOCKS,
	MTEHK_BUILD_TREES,
	MTEHK_SIGN,
	MTEHK_MUSIC,
	MTEHK_LANDINFO,
	MTEHK_SMALL_SCREENSHOT,
	MTEHK_ZOOMEDIN_SCREENSHOT,
	MTEHK_DEFAULTZOOM_SCREENSHOT,
	MTEHK_GIANT_SCREENSHOT,
	MTEHK_ZOOM_IN,
	MTEHK_ZOOM_OUT,
	MTEHK_TERRAFORM,
	MTEHK_SMALLMAP,
	MTEHK_EXTRA_VIEWPORT,
};

struct ScenarioEditorToolbarWindow : Window {
	GUITimer timer;

	ScenarioEditorToolbarWindow(WindowDesc *desc) : Window(desc)
	{
		this->InitNested(0);

		_last_started_action = CBF_NONE;
		CLRBITS(this->flags, WF_WHITE_BORDER);
		PositionMainToolbar(this);
		DoZoomInOutWindow(ZOOM_NONE, this);

		this->timer.SetInterval(MILLISECONDS_PER_TICK);
	}

	void FindWindowPlacementAndResize(int def_width, int def_height) override
	{
		Window::FindWindowPlacementAndResize(_toolbar_width, def_height);
	}

	void OnPaint() override
	{
		this->SetWidgetDisabledState(WID_TE_DATE_BACKWARD, _settings_game.game_creation.starting_year <= MIN_YEAR);
		this->SetWidgetDisabledState(WID_TE_DATE_FORWARD, _settings_game.game_creation.starting_year >= MAX_YEAR);
		this->SetWidgetDisabledState(WID_TE_ROADS, (GetRoadTypes(true) & ~_roadtypes_type) == ROADTYPES_NONE);
		this->SetWidgetDisabledState(WID_TE_TRAMS, (GetRoadTypes(true) & _roadtypes_type) == ROADTYPES_NONE);

		this->DrawWidgets();
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_TE_DATE:
				SetDParam(0, ConvertYMDToDate(_settings_game.game_creation.starting_year, 0, 1));
				DrawString(r.left, r.right, (this->height - FONT_HEIGHT_NORMAL) / 2, STR_WHITE_DATE_LONG, TC_FROMSTRING, SA_HOR_CENTER);
				break;

			case WID_TE_SPACER: {
				int height = r.bottom - r.top;
				if (height > 2 * FONT_HEIGHT_NORMAL) {
					DrawString(r.left, r.right, (height + 1) / 2 - FONT_HEIGHT_NORMAL, STR_SCENEDIT_TOOLBAR_OPENTTD, TC_FROMSTRING, SA_HOR_CENTER);
					DrawString(r.left, r.right, (height + 1) / 2, STR_SCENEDIT_TOOLBAR_SCENARIO_EDITOR, TC_FROMSTRING, SA_HOR_CENTER);
				} else {
					DrawString(r.left, r.right, (height - FONT_HEIGHT_NORMAL) / 2, STR_SCENEDIT_TOOLBAR_SCENARIO_EDITOR, TC_FROMSTRING, SA_HOR_CENTER);
				}
				break;
			}
		}
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_TE_SPACER:
				size->width = std::max(GetStringBoundingBox(STR_SCENEDIT_TOOLBAR_OPENTTD).width, GetStringBoundingBox(STR_SCENEDIT_TOOLBAR_SCENARIO_EDITOR).width) + WD_FRAMERECT_LEFT + WD_FRAMERECT_RIGHT;
				break;

			case WID_TE_DATE:
				SetDParam(0, ConvertYMDToDate(MAX_YEAR, 0, 1));
				*size = GetStringBoundingBox(STR_WHITE_DATE_LONG);
				size->height = std::max(size->height, GetSpriteSize(SPR_IMG_SAVE).height + WD_IMGBTN_TOP + WD_IMGBTN_BOTTOM);
				break;
		}
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		if (_game_mode == GM_MENU) return;
		CallBackFunction cbf = _scen_toolbar_button_procs[widget](this);
		if (cbf != CBF_NONE) _last_started_action = cbf;
	}

	void OnDropdownSelect(int widget, int index) override
	{
		CallBackFunction cbf = _scen_toolbar_dropdown_procs[widget](index);
		if (cbf != CBF_NONE) _last_started_action = cbf;
		if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
	}

	EventState OnHotkey(int hotkey) override
	{
		CallBackFunction cbf = CBF_NONE;
		switch (hotkey) {
			case MTEHK_PAUSE:                  ToolbarPauseClick(this); break;
			case MTEHK_FASTFORWARD:            ToolbarFastForwardClick(this); break;
			case MTEHK_SETTINGS:               ShowGameOptions(); break;
			case MTEHK_SAVEGAME:               MenuClickSaveLoad(); break;
			case MTEHK_GENLAND:                ToolbarScenGenLand(this); break;
			case MTEHK_GENTOWN:                ToolbarScenGenTown(this); break;
			case MTEHK_GENINDUSTRY:            ToolbarScenGenIndustry(this); break;
			case MTEHK_BUILD_ROAD:             ToolbarScenBuildRoadClick(this); break;
			case MTEHK_BUILD_TRAM:             ToolbarScenBuildTramClick(this); break;
			case MTEHK_BUILD_DOCKS:            ToolbarScenBuildDocks(this); break;
			case MTEHK_BUILD_TREES:            ToolbarScenPlantTrees(this); break;
			case MTEHK_SIGN:                   cbf = ToolbarScenPlaceSign(this); break;
			case MTEHK_MUSIC:                  ShowMusicWindow(); break;
			case MTEHK_LANDINFO:               cbf = PlaceLandBlockInfo(); break;
			case MTEHK_SMALL_SCREENSHOT:       MakeScreenshotWithConfirm(SC_VIEWPORT); break;
			case MTEHK_ZOOMEDIN_SCREENSHOT:    MakeScreenshotWithConfirm(SC_ZOOMEDIN); break;
			case MTEHK_DEFAULTZOOM_SCREENSHOT: MakeScreenshotWithConfirm(SC_DEFAULTZOOM); break;
			case MTEHK_GIANT_SCREENSHOT:       MakeScreenshotWithConfirm(SC_WORLD); break;
			case MTEHK_ZOOM_IN:                ToolbarZoomInClick(this); break;
			case MTEHK_ZOOM_OUT:               ToolbarZoomOutClick(this); break;
			case MTEHK_TERRAFORM:              ShowEditorTerraformToolbar(); break;
			case MTEHK_SMALLMAP:               ShowSmallMap(); break;
			case MTEHK_EXTRA_VIEWPORT:         ShowExtraViewportWindowForTileUnderCursor(); break;
			default: return ES_NOT_HANDLED;
		}
		if (cbf != CBF_NONE) _last_started_action = cbf;
		return ES_HANDLED;
	}

	void OnPlaceObject(Point pt, TileIndex tile) override
	{
		switch (_last_started_action) {
			case CBF_PLACE_SIGN:
				PlaceProc_Sign(tile);
				break;

			case CBF_PLACE_LANDINFO:
				ShowLandInfo(tile);
				break;

			default: NOT_REACHED();
		}
	}

	void OnPlaceObjectAbort() override
	{
		_last_started_action = CBF_NONE;
	}

	void OnTimeout() override
	{
		this->SetWidgetsLoweredState(false, WID_TE_DATE_BACKWARD, WID_TE_DATE_FORWARD, WIDGET_LIST_END);
		this->SetWidgetDirty(WID_TE_DATE_BACKWARD);
		this->SetWidgetDirty(WID_TE_DATE_FORWARD);
	}

	void OnRealtimeTick(uint delta_ms) override
	{
		if (!this->timer.Elapsed(delta_ms)) return;
		this->timer.SetInterval(MILLISECONDS_PER_TICK);

		if (this->IsWidgetLowered(WID_TE_PAUSE) != !!_pause_mode) {
			this->ToggleWidgetLoweredState(WID_TE_PAUSE);
			this->SetDirty();
		}

		if (this->IsWidgetLowered(WID_TE_FAST_FORWARD) != (_game_speed != 100)) {
			this->ToggleWidgetLoweredState(WID_TE_FAST_FORWARD);
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
		if (!gui_scope) return;
		if (FindWindowById(WC_MAIN_WINDOW, 0) != nullptr) HandleZoomMessage(this, FindWindowById(WC_MAIN_WINDOW, 0)->viewport, WID_TE_ZOOM_IN, WID_TE_ZOOM_OUT);
	}

	void OnQueryTextFinished(char *str) override
	{
		/* Was 'cancel' pressed? */
		if (str == nullptr) return;

		int32 value;
		if (!StrEmpty(str)) {
			value = atoi(str);
		} else {
			/* An empty string means revert to the default */
			value = DEF_START_YEAR;
		}
		SetStartingYear(value);

		this->SetDirty();
	}

	static HotkeyList hotkeys;
};

static Hotkey scenedit_maintoolbar_hotkeys[] = {
	Hotkey(_maintoolbar_pause_keys, "pause", MTEHK_PAUSE),
	Hotkey((uint16)0, "fastforward", MTEHK_FASTFORWARD),
	Hotkey(WKC_F2, "settings", MTEHK_SETTINGS),
	Hotkey(WKC_F3, "saveload", MTEHK_SAVEGAME),
	Hotkey(WKC_F4, "gen_land", MTEHK_GENLAND),
	Hotkey(WKC_F5, "gen_town", MTEHK_GENTOWN),
	Hotkey(WKC_F6, "gen_industry", MTEHK_GENINDUSTRY),
	Hotkey(WKC_F7, "build_road", MTEHK_BUILD_ROAD),
	Hotkey((uint16)0, "build_tram", MTEHK_BUILD_TRAM),
	Hotkey(WKC_F8, "build_docks", MTEHK_BUILD_DOCKS),
	Hotkey(WKC_F9, "build_trees", MTEHK_BUILD_TREES),
	Hotkey(WKC_F10, "build_sign", MTEHK_SIGN),
	Hotkey(WKC_F11, "music", MTEHK_MUSIC),
	Hotkey(WKC_F12, "land_info", MTEHK_LANDINFO),
	Hotkey(WKC_CTRL  | 'S', "small_screenshot", MTEHK_SMALL_SCREENSHOT),
	Hotkey(WKC_CTRL  | 'P', "zoomedin_screenshot", MTEHK_ZOOMEDIN_SCREENSHOT),
	Hotkey(WKC_CTRL  | 'D', "defaultzoom_screenshot", MTEHK_DEFAULTZOOM_SCREENSHOT),
	Hotkey((uint16)0, "giant_screenshot", MTEHK_GIANT_SCREENSHOT),
	Hotkey(_maintoolbar_zoomin_keys, "zoomin", MTEHK_ZOOM_IN),
	Hotkey(_maintoolbar_zoomout_keys, "zoomout", MTEHK_ZOOM_OUT),
	Hotkey('L', "terraform", MTEHK_TERRAFORM),
	Hotkey('M', "smallmap", MTEHK_SMALLMAP),
	Hotkey('V', "extra_viewport", MTEHK_EXTRA_VIEWPORT),
	HOTKEY_LIST_END
};
HotkeyList ScenarioEditorToolbarWindow::hotkeys("scenedit_maintoolbar", scenedit_maintoolbar_hotkeys);

static const NWidgetPart _nested_toolb_scen_inner_widgets[] = {
	NWidget(WWT_IMGBTN, COLOUR_GREY, WID_TE_PAUSE), SetDataTip(SPR_IMG_PAUSE, STR_TOOLBAR_TOOLTIP_PAUSE_GAME),
	NWidget(WWT_IMGBTN, COLOUR_GREY, WID_TE_FAST_FORWARD), SetDataTip(SPR_IMG_FASTFORWARD, STR_TOOLBAR_TOOLTIP_FORWARD),
	NWidget(WWT_IMGBTN, COLOUR_GREY, WID_TE_SETTINGS), SetDataTip(SPR_IMG_SETTINGS, STR_TOOLBAR_TOOLTIP_OPTIONS),
	NWidget(WWT_IMGBTN_2, COLOUR_GREY, WID_TE_SAVE), SetDataTip(SPR_IMG_SAVE, STR_SCENEDIT_TOOLBAR_TOOLTIP_SAVE_SCENARIO_LOAD_SCENARIO),
	NWidget(NWID_SPACER),
	NWidget(WWT_PANEL, COLOUR_GREY, WID_TE_SPACER), EndContainer(),
	NWidget(NWID_SPACER),
	NWidget(WWT_PANEL, COLOUR_GREY, WID_TE_DATE_PANEL),
		NWidget(NWID_HORIZONTAL), SetPIP(3, 2, 3),
			NWidget(WWT_IMGBTN, COLOUR_GREY, WID_TE_DATE_BACKWARD), SetDataTip(SPR_ARROW_DOWN, STR_SCENEDIT_TOOLBAR_TOOLTIP_MOVE_THE_STARTING_DATE_BACKWARD),
			NWidget(WWT_EMPTY, COLOUR_GREY, WID_TE_DATE), SetDataTip(STR_NULL, STR_SCENEDIT_TOOLBAR_TOOLTIP_SET_DATE),
			NWidget(WWT_IMGBTN, COLOUR_GREY, WID_TE_DATE_FORWARD), SetDataTip(SPR_ARROW_UP, STR_SCENEDIT_TOOLBAR_TOOLTIP_MOVE_THE_STARTING_DATE_FORWARD),
		EndContainer(),
	EndContainer(),
	NWidget(NWID_SPACER),
	NWidget(WWT_IMGBTN, COLOUR_GREY, WID_TE_SMALL_MAP), SetDataTip(SPR_IMG_SMALLMAP, STR_SCENEDIT_TOOLBAR_TOOLTIP_DISPLAY_MAP_TOWN_DIRECTORY),
	NWidget(NWID_SPACER),
	NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_TE_ZOOM_IN), SetDataTip(SPR_IMG_ZOOMIN, STR_TOOLBAR_TOOLTIP_ZOOM_THE_VIEW_IN),
	NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_TE_ZOOM_OUT), SetDataTip(SPR_IMG_ZOOMOUT, STR_TOOLBAR_TOOLTIP_ZOOM_THE_VIEW_OUT),
	NWidget(NWID_SPACER),
	NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_TE_LAND_GENERATE), SetDataTip(SPR_IMG_LANDSCAPING, STR_SCENEDIT_TOOLBAR_LANDSCAPE_GENERATION),
	NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_TE_TOWN_GENERATE), SetDataTip(SPR_IMG_TOWN, STR_SCENEDIT_TOOLBAR_TOWN_GENERATION),
	NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_TE_INDUSTRY), SetDataTip(SPR_IMG_INDUSTRY, STR_SCENEDIT_TOOLBAR_INDUSTRY_GENERATION),
	NWidget(WWT_IMGBTN, COLOUR_GREY, WID_TE_ROADS), SetDataTip(SPR_IMG_BUILDROAD, STR_SCENEDIT_TOOLBAR_ROAD_CONSTRUCTION),
	NWidget(WWT_IMGBTN, COLOUR_GREY, WID_TE_TRAMS), SetDataTip(SPR_IMG_BUILDTRAMS, STR_SCENEDIT_TOOLBAR_TRAM_CONSTRUCTION),
	NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_TE_WATER), SetDataTip(SPR_IMG_BUILDWATER, STR_TOOLBAR_TOOLTIP_BUILD_SHIP_DOCKS),
	NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_TE_TREES), SetDataTip(SPR_IMG_PLANTTREES, STR_SCENEDIT_TOOLBAR_PLANT_TREES),
	NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_TE_SIGNS), SetDataTip(SPR_IMG_SIGN, STR_SCENEDIT_TOOLBAR_PLACE_SIGN),
	NWidget(NWID_SPACER),
	NWidget(WWT_IMGBTN, COLOUR_GREY, WID_TE_MUSIC_SOUND), SetDataTip(SPR_IMG_MUSIC, STR_TOOLBAR_TOOLTIP_SHOW_SOUND_MUSIC_WINDOW),
	NWidget(WWT_IMGBTN, COLOUR_GREY, WID_TE_HELP), SetDataTip(SPR_IMG_QUERY, STR_TOOLBAR_TOOLTIP_LAND_BLOCK_INFORMATION),
	NWidget(WWT_IMGBTN, COLOUR_GREY, WID_TE_SWITCH_BAR), SetDataTip(SPR_IMG_SWITCH_TOOLBAR, STR_TOOLBAR_TOOLTIP_SWITCH_TOOLBAR),
};

static NWidgetBase *MakeScenarioToolbar(int *biggest_index)
{
	return MakeNWidgets(_nested_toolb_scen_inner_widgets, lengthof(_nested_toolb_scen_inner_widgets), biggest_index, new NWidgetScenarioToolbarContainer());
}

static const NWidgetPart _nested_toolb_scen_widgets[] = {
	NWidgetFunction(MakeScenarioToolbar),
};

static WindowDesc _toolb_scen_desc(
	WDP_MANUAL, nullptr, 0, 0,
	WC_MAIN_TOOLBAR, WC_NONE,
	WDF_NO_FOCUS,
	_nested_toolb_scen_widgets, lengthof(_nested_toolb_scen_widgets),
	&ScenarioEditorToolbarWindow::hotkeys
);

/** Allocate the toolbar. */
void AllocateToolbar()
{
	/* Clean old GUI values; railtype is (re)set by rail_gui.cpp */
	_last_built_roadtype = ROADTYPE_ROAD;
	_last_built_tramtype = ROADTYPE_TRAM;

	if (_game_mode == GM_EDITOR) {
		new ScenarioEditorToolbarWindow(&_toolb_scen_desc);
	} else {
		new MainToolbarWindow(&_toolb_normal_desc);
	}
}
