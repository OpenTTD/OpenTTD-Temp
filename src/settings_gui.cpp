/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file settings_gui.cpp GUI for settings. */

#include "stdafx.h"
#include "currency.h"
#include "error.h"
#include "settings_gui.h"
#include "textbuf_gui.h"
#include "command_func.h"
#include "network/network.h"
#include "town.h"
#include "settings_internal.h"
#include "newgrf_townname.h"
#include "strings_func.h"
#include "window_func.h"
#include "string_func.h"
#include "widgets/dropdown_type.h"
#include "widgets/dropdown_func.h"
#include "highscore.h"
#include "base_media_base.h"
#include "company_base.h"
#include "company_func.h"
#include "viewport_func.h"
#include "core/geometry_func.hpp"
#include "ai/ai.hpp"
#include "blitter/factory.hpp"
#include "language.h"
#include "textfile_gui.h"
#include "stringfilter_type.h"
#include "querystring_gui.h"
#include "fontcache.h"
#include "zoom_func.h"

#include <vector>

#include "safeguards.h"


static const StringID _driveside_dropdown[] = {
	STR_GAME_OPTIONS_ROAD_VEHICLES_DROPDOWN_LEFT,
	STR_GAME_OPTIONS_ROAD_VEHICLES_DROPDOWN_RIGHT,
	INVALID_STRING_ID
};

static const StringID _autosave_dropdown[] = {
	STR_GAME_OPTIONS_AUTOSAVE_DROPDOWN_OFF,
	STR_GAME_OPTIONS_AUTOSAVE_DROPDOWN_EVERY_1_MONTH,
	STR_GAME_OPTIONS_AUTOSAVE_DROPDOWN_EVERY_3_MONTHS,
	STR_GAME_OPTIONS_AUTOSAVE_DROPDOWN_EVERY_6_MONTHS,
	STR_GAME_OPTIONS_AUTOSAVE_DROPDOWN_EVERY_12_MONTHS,
	INVALID_STRING_ID,
};

static const StringID _gui_zoom_dropdown[] = {
	STR_GAME_OPTIONS_GUI_ZOOM_DROPDOWN_NORMAL,
	STR_GAME_OPTIONS_GUI_ZOOM_DROPDOWN_2X_ZOOM,
	STR_GAME_OPTIONS_GUI_ZOOM_DROPDOWN_4X_ZOOM,
	INVALID_STRING_ID,
};

static const StringID _font_zoom_dropdown[] = {
	STR_GAME_OPTIONS_FONT_ZOOM_DROPDOWN_NORMAL,
	STR_GAME_OPTIONS_FONT_ZOOM_DROPDOWN_2X_ZOOM,
	STR_GAME_OPTIONS_FONT_ZOOM_DROPDOWN_4X_ZOOM,
	INVALID_STRING_ID,
};

int _nb_orig_names = SPECSTR_TOWNNAME_LAST - SPECSTR_TOWNNAME_START + 1; ///< Number of original town names.
static StringID *_grf_names = nullptr; ///< Pointer to town names defined by NewGRFs.
static int _nb_grf_names = 0;       ///< Number of town names defined by NewGRFs.

static Dimension _circle_size; ///< Dimension of the circle +/- icon. This is here as not all users are within the class of the settings window.

static const void *ResolveVariableAddress(const GameSettings *settings_ptr, const SettingDesc *sd);

/** Allocate memory for the NewGRF town names. */
void InitGRFTownGeneratorNames()
{
	free(_grf_names);
	_grf_names = GetGRFTownNameList();
	_nb_grf_names = 0;
	for (StringID *s = _grf_names; *s != INVALID_STRING_ID; s++) _nb_grf_names++;
}

/**
 * Get a town name.
 * @param town_name Number of the wanted town name.
 * @return Name of the town as string ID.
 */
static inline StringID TownName(int town_name)
{
	if (town_name < _nb_orig_names) return STR_GAME_OPTIONS_TOWN_NAME_ORIGINAL_ENGLISH + town_name;
	town_name -= _nb_orig_names;
	if (town_name < _nb_grf_names) return _grf_names[town_name];
	return STR_UNDEFINED;
}

/**
 * Get index of the current screen resolution.
 * @return Index of the current screen resolution if it is a known resolution, _resolutions.size() otherwise.
 */
static uint GetCurRes()
{
	uint i;

	for (i = 0; i != _resolutions.size(); i++) {
		if (_resolutions[i] == Dimension(_screen.width, _screen.height)) break;
	}
	return i;
}

static void ShowCustCurrency();

template <class T>
static DropDownList BuildSetDropDownList(int *selected_index, bool allow_selection)
{
	int n = T::GetNumSets();
	*selected_index = T::GetIndexOfUsedSet();

	DropDownList list;
	for (int i = 0; i < n; i++) {
		list.emplace_back(new DropDownListCharStringItem(T::GetSet(i)->name, i, !allow_selection && (*selected_index != i)));
	}

	return list;
}

DropDownList BuildMusicSetDropDownList(int *selected_index)
{
	return BuildSetDropDownList<BaseMusic>(selected_index, true);
}

/** Window for displaying the textfile of a BaseSet. */
template <class TBaseSet>
struct BaseSetTextfileWindow : public TextfileWindow {
	const TBaseSet* baseset; ///< View the textfile of this BaseSet.
	StringID content_type;   ///< STR_CONTENT_TYPE_xxx for title.

	BaseSetTextfileWindow(TextfileType file_type, const TBaseSet* baseset, StringID content_type) : TextfileWindow(file_type), baseset(baseset), content_type(content_type)
	{
		const char *textfile = this->baseset->GetTextfile(file_type);
		this->LoadTextfile(textfile, BASESET_DIR);
	}

	void SetStringParameters(int widget) const override
	{
		if (widget == WID_TF_CAPTION) {
			SetDParam(0, content_type);
			SetDParamStr(1, this->baseset->name.c_str());
		}
	}
};

/**
 * Open the BaseSet version of the textfile window.
 * @param file_type The type of textfile to display.
 * @param baseset The BaseSet to use.
 * @param content_type STR_CONTENT_TYPE_xxx for title.
 */
template <class TBaseSet>
void ShowBaseSetTextfileWindow(TextfileType file_type, const TBaseSet* baseset, StringID content_type)
{
	DeleteWindowById(WC_TEXTFILE, file_type);
	new BaseSetTextfileWindow<TBaseSet>(file_type, baseset, content_type);
}

struct GameOptionsWindow : Window {
	GameSettings *opt;
	bool reload;

	GameOptionsWindow(WindowDesc *desc) : Window(desc)
	{
		this->opt = &GetGameSettings();
		this->reload = false;

		this->InitNested(WN_GAME_OPTIONS_GAME_OPTIONS);
		this->OnInvalidateData(0);
	}

	~GameOptionsWindow()
	{
		DeleteWindowById(WC_CUSTOM_CURRENCY, 0);
		DeleteWindowByClass(WC_TEXTFILE);
		if (this->reload) _switch_mode = SM_MENU;
	}

	/**
	 * Build the dropdown list for a specific widget.
	 * @param widget         Widget to build list for
	 * @param selected_index Currently selected item
	 * @return the built dropdown list, or nullptr if the widget has no dropdown menu.
	 */
	DropDownList BuildDropDownList(int widget, int *selected_index) const
	{
		DropDownList list;
		switch (widget) {
			case WID_GO_CURRENCY_DROPDOWN: { // Setup currencies dropdown
				*selected_index = this->opt->locale.currency;
				StringID *items = BuildCurrencyDropdown();
				uint64 disabled = _game_mode == GM_MENU ? 0LL : ~GetMaskOfAllowedCurrencies();

				/* Add non-custom currencies; sorted naturally */
				for (uint i = 0; i < CURRENCY_END; items++, i++) {
					if (i == CURRENCY_CUSTOM) continue;
					list.emplace_back(new DropDownListStringItem(*items, i, HasBit(disabled, i)));
				}
				std::sort(list.begin(), list.end(), DropDownListStringItem::NatSortFunc);

				/* Append custom currency at the end */
				list.emplace_back(new DropDownListItem(-1, false)); // separator line
				list.emplace_back(new DropDownListStringItem(STR_GAME_OPTIONS_CURRENCY_CUSTOM, CURRENCY_CUSTOM, HasBit(disabled, CURRENCY_CUSTOM)));
				break;
			}

			case WID_GO_ROADSIDE_DROPDOWN: { // Setup road-side dropdown
				*selected_index = this->opt->vehicle.road_side;
				const StringID *items = _driveside_dropdown;
				uint disabled = 0;

				/* You can only change the drive side if you are in the menu or ingame with
				 * no vehicles present. In a networking game only the server can change it */
				extern bool RoadVehiclesAreBuilt();
				if ((_game_mode != GM_MENU && RoadVehiclesAreBuilt()) || (_networking && !_network_server)) {
					disabled = ~(1 << this->opt->vehicle.road_side); // disable the other value
				}

				for (uint i = 0; *items != INVALID_STRING_ID; items++, i++) {
					list.emplace_back(new DropDownListStringItem(*items, i, HasBit(disabled, i)));
				}
				break;
			}

			case WID_GO_TOWNNAME_DROPDOWN: { // Setup townname dropdown
				*selected_index = this->opt->game_creation.town_name;

				int enabled_item = (_game_mode == GM_MENU || Town::GetNumItems() == 0) ? -1 : *selected_index;

				/* Add and sort newgrf townnames generators */
				for (int i = 0; i < _nb_grf_names; i++) {
					int result = _nb_orig_names + i;
					list.emplace_back(new DropDownListStringItem(_grf_names[i], result, enabled_item != result && enabled_item >= 0));
				}
				std::sort(list.begin(), list.end(), DropDownListStringItem::NatSortFunc);

				size_t newgrf_size = list.size();
				/* Insert newgrf_names at the top of the list */
				if (newgrf_size > 0) {
					list.emplace_back(new DropDownListItem(-1, false)); // separator line
					newgrf_size++;
				}

				/* Add and sort original townnames generators */
				for (int i = 0; i < _nb_orig_names; i++) {
					list.emplace_back(new DropDownListStringItem(STR_GAME_OPTIONS_TOWN_NAME_ORIGINAL_ENGLISH + i, i, enabled_item != i && enabled_item >= 0));
				}
				std::sort(list.begin() + newgrf_size, list.end(), DropDownListStringItem::NatSortFunc);
				break;
			}

			case WID_GO_AUTOSAVE_DROPDOWN: { // Setup autosave dropdown
				*selected_index = _settings_client.gui.autosave;
				const StringID *items = _autosave_dropdown;
				for (uint i = 0; *items != INVALID_STRING_ID; items++, i++) {
					list.emplace_back(new DropDownListStringItem(*items, i, false));
				}
				break;
			}

			case WID_GO_LANG_DROPDOWN: { // Setup interface language dropdown
				for (uint i = 0; i < _languages.size(); i++) {
					if (&_languages[i] == _current_language) *selected_index = i;
					list.emplace_back(new DropDownListStringItem(SPECSTR_LANGUAGE_START + i, i, false));
				}
				std::sort(list.begin(), list.end(), DropDownListStringItem::NatSortFunc);
				break;
			}

			case WID_GO_RESOLUTION_DROPDOWN: // Setup resolution dropdown
				if (_resolutions.empty()) break;

				*selected_index = GetCurRes();
				for (uint i = 0; i < _resolutions.size(); i++) {
					list.emplace_back(new DropDownListStringItem(SPECSTR_RESOLUTION_START + i, i, false));
				}
				break;

			case WID_GO_GUI_ZOOM_DROPDOWN: {
				*selected_index = ZOOM_LVL_OUT_4X - _gui_zoom;
				const StringID *items = _gui_zoom_dropdown;
				for (int i = 0; *items != INVALID_STRING_ID; items++, i++) {
					list.emplace_back(new DropDownListStringItem(*items, i, _settings_client.gui.zoom_min > ZOOM_LVL_OUT_4X - i));
				}
				break;
			}

			case WID_GO_FONT_ZOOM_DROPDOWN: {
				*selected_index = ZOOM_LVL_OUT_4X - _font_zoom;
				const StringID *items = _font_zoom_dropdown;
				for (int i = 0; *items != INVALID_STRING_ID; items++, i++) {
					list.emplace_back(new DropDownListStringItem(*items, i, false));
				}
				break;
			}

			case WID_GO_BASE_GRF_DROPDOWN:
				list = BuildSetDropDownList<BaseGraphics>(selected_index, (_game_mode == GM_MENU));
				break;

			case WID_GO_BASE_SFX_DROPDOWN:
				list = BuildSetDropDownList<BaseSounds>(selected_index, (_game_mode == GM_MENU));
				break;

			case WID_GO_BASE_MUSIC_DROPDOWN:
				list = BuildMusicSetDropDownList(selected_index);
				break;
		}

		return list;
	}

	void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_GO_CURRENCY_DROPDOWN:   SetDParam(0, _currency_specs[this->opt->locale.currency].name); break;
			case WID_GO_ROADSIDE_DROPDOWN:   SetDParam(0, STR_GAME_OPTIONS_ROAD_VEHICLES_DROPDOWN_LEFT + this->opt->vehicle.road_side); break;
			case WID_GO_TOWNNAME_DROPDOWN:   SetDParam(0, TownName(this->opt->game_creation.town_name)); break;
			case WID_GO_AUTOSAVE_DROPDOWN:   SetDParam(0, _autosave_dropdown[_settings_client.gui.autosave]); break;
			case WID_GO_LANG_DROPDOWN:       SetDParamStr(0, _current_language->own_name); break;
			case WID_GO_RESOLUTION_DROPDOWN: SetDParam(0, GetCurRes() == _resolutions.size() ? STR_GAME_OPTIONS_RESOLUTION_OTHER : SPECSTR_RESOLUTION_START + GetCurRes()); break;
			case WID_GO_GUI_ZOOM_DROPDOWN:   SetDParam(0, _gui_zoom_dropdown[ZOOM_LVL_OUT_4X - _gui_zoom]); break;
			case WID_GO_FONT_ZOOM_DROPDOWN:  SetDParam(0, _font_zoom_dropdown[ZOOM_LVL_OUT_4X - _font_zoom]); break;
			case WID_GO_BASE_GRF_DROPDOWN:   SetDParamStr(0, BaseGraphics::GetUsedSet()->name.c_str()); break;
			case WID_GO_BASE_GRF_STATUS:     SetDParam(0, BaseGraphics::GetUsedSet()->GetNumInvalid()); break;
			case WID_GO_BASE_SFX_DROPDOWN:   SetDParamStr(0, BaseSounds::GetUsedSet()->name.c_str()); break;
			case WID_GO_BASE_MUSIC_DROPDOWN: SetDParamStr(0, BaseMusic::GetUsedSet()->name.c_str()); break;
			case WID_GO_BASE_MUSIC_STATUS:   SetDParam(0, BaseMusic::GetUsedSet()->GetNumInvalid()); break;
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_GO_BASE_GRF_DESCRIPTION:
				SetDParamStr(0, BaseGraphics::GetUsedSet()->GetDescription(GetCurrentLanguageIsoCode()));
				DrawStringMultiLine(r.left, r.right, r.top, UINT16_MAX, STR_BLACK_RAW_STRING);
				break;

			case WID_GO_BASE_SFX_DESCRIPTION:
				SetDParamStr(0, BaseSounds::GetUsedSet()->GetDescription(GetCurrentLanguageIsoCode()));
				DrawStringMultiLine(r.left, r.right, r.top, UINT16_MAX, STR_BLACK_RAW_STRING);
				break;

			case WID_GO_BASE_MUSIC_DESCRIPTION:
				SetDParamStr(0, BaseMusic::GetUsedSet()->GetDescription(GetCurrentLanguageIsoCode()));
				DrawStringMultiLine(r.left, r.right, r.top, UINT16_MAX, STR_BLACK_RAW_STRING);
				break;
		}
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_GO_BASE_GRF_DESCRIPTION:
				/* Find the biggest description for the default size. */
				for (int i = 0; i < BaseGraphics::GetNumSets(); i++) {
					SetDParamStr(0, BaseGraphics::GetSet(i)->GetDescription(GetCurrentLanguageIsoCode()));
					size->height = max(size->height, (uint)GetStringHeight(STR_BLACK_RAW_STRING, size->width));
				}
				break;

			case WID_GO_BASE_GRF_STATUS:
				/* Find the biggest description for the default size. */
				for (int i = 0; i < BaseGraphics::GetNumSets(); i++) {
					uint invalid_files = BaseGraphics::GetSet(i)->GetNumInvalid();
					if (invalid_files == 0) continue;

					SetDParam(0, invalid_files);
					*size = maxdim(*size, GetStringBoundingBox(STR_GAME_OPTIONS_BASE_GRF_STATUS));
				}
				break;

			case WID_GO_BASE_SFX_DESCRIPTION:
				/* Find the biggest description for the default size. */
				for (int i = 0; i < BaseSounds::GetNumSets(); i++) {
					SetDParamStr(0, BaseSounds::GetSet(i)->GetDescription(GetCurrentLanguageIsoCode()));
					size->height = max(size->height, (uint)GetStringHeight(STR_BLACK_RAW_STRING, size->width));
				}
				break;

			case WID_GO_BASE_MUSIC_DESCRIPTION:
				/* Find the biggest description for the default size. */
				for (int i = 0; i < BaseMusic::GetNumSets(); i++) {
					SetDParamStr(0, BaseMusic::GetSet(i)->GetDescription(GetCurrentLanguageIsoCode()));
					size->height = max(size->height, (uint)GetStringHeight(STR_BLACK_RAW_STRING, size->width));
				}
				break;

			case WID_GO_BASE_MUSIC_STATUS:
				/* Find the biggest description for the default size. */
				for (int i = 0; i < BaseMusic::GetNumSets(); i++) {
					uint invalid_files = BaseMusic::GetSet(i)->GetNumInvalid();
					if (invalid_files == 0) continue;

					SetDParam(0, invalid_files);
					*size = maxdim(*size, GetStringBoundingBox(STR_GAME_OPTIONS_BASE_MUSIC_STATUS));
				}
				break;

			default: {
				int selected;
				DropDownList list = this->BuildDropDownList(widget, &selected);
				if (!list.empty()) {
					/* Find the biggest item for the default size. */
					for (const auto &ddli : list) {
						Dimension string_dim;
						int width = ddli->Width();
						string_dim.width = width + padding.width;
						string_dim.height = ddli->Height(width) + padding.height;
						*size = maxdim(*size, string_dim);
					}
				}
			}
		}
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		if (widget >= WID_GO_BASE_GRF_TEXTFILE && widget < WID_GO_BASE_GRF_TEXTFILE + TFT_END) {
			if (BaseGraphics::GetUsedSet() == nullptr) return;

			ShowBaseSetTextfileWindow((TextfileType)(widget - WID_GO_BASE_GRF_TEXTFILE), BaseGraphics::GetUsedSet(), STR_CONTENT_TYPE_BASE_GRAPHICS);
			return;
		}
		if (widget >= WID_GO_BASE_SFX_TEXTFILE && widget < WID_GO_BASE_SFX_TEXTFILE + TFT_END) {
			if (BaseSounds::GetUsedSet() == nullptr) return;

			ShowBaseSetTextfileWindow((TextfileType)(widget - WID_GO_BASE_SFX_TEXTFILE), BaseSounds::GetUsedSet(), STR_CONTENT_TYPE_BASE_SOUNDS);
			return;
		}
		if (widget >= WID_GO_BASE_MUSIC_TEXTFILE && widget < WID_GO_BASE_MUSIC_TEXTFILE + TFT_END) {
			if (BaseMusic::GetUsedSet() == nullptr) return;

			ShowBaseSetTextfileWindow((TextfileType)(widget - WID_GO_BASE_MUSIC_TEXTFILE), BaseMusic::GetUsedSet(), STR_CONTENT_TYPE_BASE_MUSIC);
			return;
		}
		switch (widget) {
			case WID_GO_FULLSCREEN_BUTTON: // Click fullscreen on/off
				/* try to toggle full-screen on/off */
				if (!ToggleFullScreen(!_fullscreen)) {
					ShowErrorMessage(STR_ERROR_FULLSCREEN_FAILED, INVALID_STRING_ID, WL_ERROR);
				}
				this->SetWidgetLoweredState(WID_GO_FULLSCREEN_BUTTON, _fullscreen);
				this->SetDirty();
				break;

			default: {
				int selected;
				DropDownList list = this->BuildDropDownList(widget, &selected);
				if (!list.empty()) {
					ShowDropDownList(this, std::move(list), selected, widget);
				} else {
					if (widget == WID_GO_RESOLUTION_DROPDOWN) ShowErrorMessage(STR_ERROR_RESOLUTION_LIST_FAILED, INVALID_STRING_ID, WL_ERROR);
				}
				break;
			}
		}
	}

	/**
	 * Set the base media set.
	 * @param index the index of the media set
	 * @tparam T class of media set
	 */
	template <class T>
	void SetMediaSet(int index)
	{
		if (_game_mode == GM_MENU) {
			auto name = T::GetSet(index)->name;

			T::ini_set = name;

			T::SetSet(name);
			this->reload = true;
			this->InvalidateData();
		}
	}

	void OnDropdownSelect(int widget, int index) override
	{
		switch (widget) {
			case WID_GO_CURRENCY_DROPDOWN: // Currency
				if (index == CURRENCY_CUSTOM) ShowCustCurrency();
				this->opt->locale.currency = index;
				ReInitAllWindows();
				break;

			case WID_GO_ROADSIDE_DROPDOWN: // Road side
				if (this->opt->vehicle.road_side != index) { // only change if setting changed
					uint i;
					if (GetSettingFromName("vehicle.road_side", &i) == nullptr) NOT_REACHED();
					SetSettingValue(i, index);
					MarkWholeScreenDirty();
				}
				break;

			case WID_GO_TOWNNAME_DROPDOWN: // Town names
				if (_game_mode == GM_MENU || Town::GetNumItems() == 0) {
					this->opt->game_creation.town_name = index;
					SetWindowDirty(WC_GAME_OPTIONS, WN_GAME_OPTIONS_GAME_OPTIONS);
				}
				break;

			case WID_GO_AUTOSAVE_DROPDOWN: // Autosave options
				_settings_client.gui.autosave = index;
				this->SetDirty();
				break;

			case WID_GO_LANG_DROPDOWN: // Change interface language
				ReadLanguagePack(&_languages[index]);
				DeleteWindowByClass(WC_QUERY_STRING);
				CheckForMissingGlyphs();
				ClearAllCachedNames();
				UpdateAllVirtCoords();
				ReInitAllWindows();
				break;

			case WID_GO_RESOLUTION_DROPDOWN: // Change resolution
				if ((uint)index < _resolutions.size() && ChangeResInGame(_resolutions[index].width, _resolutions[index].height)) {
					this->SetDirty();
				}
				break;

			case WID_GO_GUI_ZOOM_DROPDOWN:
				GfxClearSpriteCache();
				_gui_zoom = (ZoomLevel)(ZOOM_LVL_OUT_4X - index);
				UpdateCursorSize();
				UpdateAllVirtCoords();
				FixTitleGameZoom();
				ReInitAllWindows();
				break;

			case WID_GO_FONT_ZOOM_DROPDOWN:
				GfxClearSpriteCache();
				_font_zoom = (ZoomLevel)(ZOOM_LVL_OUT_4X - index);
				ClearFontCache();
				LoadStringWidthTable();
				UpdateAllVirtCoords();
				break;

			case WID_GO_BASE_GRF_DROPDOWN:
				this->SetMediaSet<BaseGraphics>(index);
				break;

			case WID_GO_BASE_SFX_DROPDOWN:
				this->SetMediaSet<BaseSounds>(index);
				break;

			case WID_GO_BASE_MUSIC_DROPDOWN:
				ChangeMusicSet(index);
				break;
		}
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data. @see GameOptionsInvalidationData
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (!gui_scope) return;
		this->SetWidgetLoweredState(WID_GO_FULLSCREEN_BUTTON, _fullscreen);

		bool missing_files = BaseGraphics::GetUsedSet()->GetNumMissing() == 0;
		this->GetWidget<NWidgetCore>(WID_GO_BASE_GRF_STATUS)->SetDataTip(missing_files ? STR_EMPTY : STR_GAME_OPTIONS_BASE_GRF_STATUS, STR_NULL);

		for (TextfileType tft = TFT_BEGIN; tft < TFT_END; tft++) {
			this->SetWidgetDisabledState(WID_GO_BASE_GRF_TEXTFILE + tft, BaseGraphics::GetUsedSet() == nullptr || BaseGraphics::GetUsedSet()->GetTextfile(tft) == nullptr);
			this->SetWidgetDisabledState(WID_GO_BASE_SFX_TEXTFILE + tft, BaseSounds::GetUsedSet() == nullptr || BaseSounds::GetUsedSet()->GetTextfile(tft) == nullptr);
			this->SetWidgetDisabledState(WID_GO_BASE_MUSIC_TEXTFILE + tft, BaseMusic::GetUsedSet() == nullptr || BaseMusic::GetUsedSet()->GetTextfile(tft) == nullptr);
		}

		missing_files = BaseMusic::GetUsedSet()->GetNumInvalid() == 0;
		this->GetWidget<NWidgetCore>(WID_GO_BASE_MUSIC_STATUS)->SetDataTip(missing_files ? STR_EMPTY : STR_GAME_OPTIONS_BASE_MUSIC_STATUS, STR_NULL);
	}
};

static const NWidgetPart _nested_game_options_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY), SetDataTip(STR_GAME_OPTIONS_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY, WID_GO_BACKGROUND), SetPIP(6, 6, 10),
		NWidget(NWID_HORIZONTAL), SetPIP(10, 10, 10),
			NWidget(NWID_VERTICAL), SetPIP(0, 6, 0),
				NWidget(WWT_FRAME, COLOUR_GREY), SetDataTip(STR_GAME_OPTIONS_ROAD_VEHICLES_FRAME, STR_NULL),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_GO_ROADSIDE_DROPDOWN), SetMinimalSize(150, 12), SetDataTip(STR_BLACK_STRING, STR_GAME_OPTIONS_ROAD_VEHICLES_DROPDOWN_TOOLTIP), SetFill(1, 0),
				EndContainer(),
				NWidget(WWT_FRAME, COLOUR_GREY), SetDataTip(STR_GAME_OPTIONS_AUTOSAVE_FRAME, STR_NULL),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_GO_AUTOSAVE_DROPDOWN), SetMinimalSize(150, 12), SetDataTip(STR_BLACK_STRING, STR_GAME_OPTIONS_AUTOSAVE_DROPDOWN_TOOLTIP), SetFill(1, 0),
				EndContainer(),
				NWidget(WWT_FRAME, COLOUR_GREY), SetDataTip(STR_GAME_OPTIONS_RESOLUTION, STR_NULL),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_GO_RESOLUTION_DROPDOWN), SetMinimalSize(150, 12), SetDataTip(STR_BLACK_STRING, STR_GAME_OPTIONS_RESOLUTION_TOOLTIP), SetFill(1, 0), SetPadding(0, 0, 3, 0),
					NWidget(NWID_HORIZONTAL),
						NWidget(WWT_TEXT, COLOUR_GREY), SetMinimalSize(0, 12), SetFill(1, 0), SetDataTip(STR_GAME_OPTIONS_FULLSCREEN, STR_NULL),
						NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_GO_FULLSCREEN_BUTTON), SetMinimalSize(21, 9), SetDataTip(STR_EMPTY, STR_GAME_OPTIONS_FULLSCREEN_TOOLTIP),
					EndContainer(),
				EndContainer(),
				NWidget(WWT_FRAME, COLOUR_GREY), SetDataTip(STR_GAME_OPTIONS_GUI_ZOOM_FRAME, STR_NULL),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_GO_GUI_ZOOM_DROPDOWN), SetMinimalSize(150, 12), SetDataTip(STR_BLACK_STRING, STR_GAME_OPTIONS_GUI_ZOOM_DROPDOWN_TOOLTIP), SetFill(1, 0),
				EndContainer(),
			EndContainer(),

			NWidget(NWID_VERTICAL), SetPIP(0, 6, 0),
				NWidget(WWT_FRAME, COLOUR_GREY), SetDataTip(STR_GAME_OPTIONS_TOWN_NAMES_FRAME, STR_NULL),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_GO_TOWNNAME_DROPDOWN), SetMinimalSize(150, 12), SetDataTip(STR_BLACK_STRING, STR_GAME_OPTIONS_TOWN_NAMES_DROPDOWN_TOOLTIP), SetFill(1, 0),
				EndContainer(),
				NWidget(WWT_FRAME, COLOUR_GREY), SetDataTip(STR_GAME_OPTIONS_LANGUAGE, STR_NULL),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_GO_LANG_DROPDOWN), SetMinimalSize(150, 12), SetDataTip(STR_BLACK_RAW_STRING, STR_GAME_OPTIONS_LANGUAGE_TOOLTIP), SetFill(1, 0),
				EndContainer(),
				NWidget(WWT_FRAME, COLOUR_GREY), SetDataTip(STR_GAME_OPTIONS_CURRENCY_UNITS_FRAME, STR_NULL),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_GO_CURRENCY_DROPDOWN), SetMinimalSize(150, 12), SetDataTip(STR_BLACK_STRING, STR_GAME_OPTIONS_CURRENCY_UNITS_DROPDOWN_TOOLTIP), SetFill(1, 0),
				EndContainer(),
				NWidget(NWID_SPACER), SetMinimalSize(0, 0), SetFill(0, 1),
				NWidget(WWT_FRAME, COLOUR_GREY), SetDataTip(STR_GAME_OPTIONS_FONT_ZOOM, STR_NULL),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_GO_FONT_ZOOM_DROPDOWN), SetMinimalSize(150, 12), SetDataTip(STR_BLACK_STRING, STR_GAME_OPTIONS_FONT_ZOOM_DROPDOWN_TOOLTIP), SetFill(1, 0),
				EndContainer(),
			EndContainer(),
		EndContainer(),

		NWidget(WWT_FRAME, COLOUR_GREY), SetDataTip(STR_GAME_OPTIONS_BASE_GRF, STR_NULL), SetPadding(0, 10, 0, 10),
			NWidget(NWID_HORIZONTAL), SetPIP(0, 30, 0),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_GO_BASE_GRF_DROPDOWN), SetMinimalSize(150, 12), SetDataTip(STR_BLACK_RAW_STRING, STR_GAME_OPTIONS_BASE_GRF_TOOLTIP),
				NWidget(WWT_TEXT, COLOUR_GREY, WID_GO_BASE_GRF_STATUS), SetMinimalSize(150, 12), SetDataTip(STR_EMPTY, STR_NULL), SetFill(1, 0),
			EndContainer(),
			NWidget(WWT_TEXT, COLOUR_GREY, WID_GO_BASE_GRF_DESCRIPTION), SetMinimalSize(330, 0), SetDataTip(STR_EMPTY, STR_GAME_OPTIONS_BASE_GRF_DESCRIPTION_TOOLTIP), SetFill(1, 0), SetPadding(6, 0, 6, 0),
			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(7, 0, 7),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_GO_BASE_GRF_TEXTFILE + TFT_README), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_TEXTFILE_VIEW_README, STR_NULL),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_GO_BASE_GRF_TEXTFILE + TFT_CHANGELOG), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_TEXTFILE_VIEW_CHANGELOG, STR_NULL),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_GO_BASE_GRF_TEXTFILE + TFT_LICENSE), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_TEXTFILE_VIEW_LICENCE, STR_NULL),
			EndContainer(),
		EndContainer(),

		NWidget(WWT_FRAME, COLOUR_GREY), SetDataTip(STR_GAME_OPTIONS_BASE_SFX, STR_NULL), SetPadding(0, 10, 0, 10),
			NWidget(NWID_HORIZONTAL), SetPIP(0, 30, 0),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_GO_BASE_SFX_DROPDOWN), SetMinimalSize(150, 12), SetDataTip(STR_BLACK_RAW_STRING, STR_GAME_OPTIONS_BASE_SFX_TOOLTIP),
				NWidget(NWID_SPACER), SetFill(1, 0),
			EndContainer(),
			NWidget(WWT_TEXT, COLOUR_GREY, WID_GO_BASE_SFX_DESCRIPTION), SetMinimalSize(330, 0), SetDataTip(STR_EMPTY, STR_GAME_OPTIONS_BASE_SFX_DESCRIPTION_TOOLTIP), SetFill(1, 0), SetPadding(6, 0, 6, 0),
			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(7, 0, 7),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_GO_BASE_SFX_TEXTFILE + TFT_README), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_TEXTFILE_VIEW_README, STR_NULL),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_GO_BASE_SFX_TEXTFILE + TFT_CHANGELOG), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_TEXTFILE_VIEW_CHANGELOG, STR_NULL),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_GO_BASE_SFX_TEXTFILE + TFT_LICENSE), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_TEXTFILE_VIEW_LICENCE, STR_NULL),
			EndContainer(),
		EndContainer(),

		NWidget(WWT_FRAME, COLOUR_GREY), SetDataTip(STR_GAME_OPTIONS_BASE_MUSIC, STR_NULL), SetPadding(0, 10, 0, 10),
			NWidget(NWID_HORIZONTAL), SetPIP(0, 30, 0),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_GO_BASE_MUSIC_DROPDOWN), SetMinimalSize(150, 12), SetDataTip(STR_BLACK_RAW_STRING, STR_GAME_OPTIONS_BASE_MUSIC_TOOLTIP),
				NWidget(WWT_TEXT, COLOUR_GREY, WID_GO_BASE_MUSIC_STATUS), SetMinimalSize(150, 12), SetDataTip(STR_EMPTY, STR_NULL), SetFill(1, 0),
			EndContainer(),
			NWidget(WWT_TEXT, COLOUR_GREY, WID_GO_BASE_MUSIC_DESCRIPTION), SetMinimalSize(330, 0), SetDataTip(STR_EMPTY, STR_GAME_OPTIONS_BASE_MUSIC_DESCRIPTION_TOOLTIP), SetFill(1, 0), SetPadding(6, 0, 6, 0),
			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(7, 0, 7),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_GO_BASE_MUSIC_TEXTFILE + TFT_README), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_TEXTFILE_VIEW_README, STR_NULL),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_GO_BASE_MUSIC_TEXTFILE + TFT_CHANGELOG), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_TEXTFILE_VIEW_CHANGELOG, STR_NULL),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_GO_BASE_MUSIC_TEXTFILE + TFT_LICENSE), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_TEXTFILE_VIEW_LICENCE, STR_NULL),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _game_options_desc(
	WDP_CENTER, "settings_game", 0, 0,
	WC_GAME_OPTIONS, WC_NONE,
	0,
	_nested_game_options_widgets, lengthof(_nested_game_options_widgets)
);

/** Open the game options window. */
void ShowGameOptions()
{
	DeleteWindowByClass(WC_GAME_OPTIONS);
	new GameOptionsWindow(&_game_options_desc);
}

static int SETTING_HEIGHT = 11;    ///< Height of a single setting in the tree view in pixels
static const int LEVEL_WIDTH = 15; ///< Indenting width of a sub-page in pixels

/**
 * Flags for #SettingEntry
 * @note The #SEF_BUTTONS_MASK matches expectations of the formal parameter 'state' of #DrawArrowButtons
 */
enum SettingEntryFlags {
	SEF_LEFT_DEPRESSED  = 0x01, ///< Of a numeric setting entry, the left button is depressed
	SEF_RIGHT_DEPRESSED = 0x02, ///< Of a numeric setting entry, the right button is depressed
	SEF_BUTTONS_MASK = (SEF_LEFT_DEPRESSED | SEF_RIGHT_DEPRESSED), ///< Bit-mask for button flags

	SEF_LAST_FIELD = 0x04, ///< This entry is the last one in a (sub-)page
	SEF_FILTERED   = 0x08, ///< Entry is hidden by the string filter
};

/** How the list of advanced settings is filtered. */
enum RestrictionMode {
	RM_BASIC,                            ///< Display settings associated to the "basic" list.
	RM_ADVANCED,                         ///< Display settings associated to the "advanced" list.
	RM_ALL,                              ///< List all settings regardless of the default/newgame/... values.
	RM_CHANGED_AGAINST_DEFAULT,          ///< Show only settings which are different compared to default values.
	RM_CHANGED_AGAINST_NEW,              ///< Show only settings which are different compared to the user's new game setting values.
	RM_END,                              ///< End for iteration.
};
DECLARE_POSTFIX_INCREMENT(RestrictionMode)

/** Filter for settings list. */
struct SettingFilter {
	StringFilter string;     ///< Filter string.
	RestrictionMode min_cat; ///< Minimum category needed to display all filtered strings (#RM_BASIC, #RM_ADVANCED, or #RM_ALL).
	bool type_hides;         ///< Whether the type hides filtered strings.
	RestrictionMode mode;    ///< Filter based on category.
	SettingType type;        ///< Filter based on type.
};

/** Data structure describing a single setting in a tab */
struct BaseSettingEntry {
	byte flags; ///< Flags of the setting entry. @see SettingEntryFlags
	byte level; ///< Nesting level of this setting entry

	BaseSettingEntry() : flags(0), level(0) {}
	virtual ~BaseSettingEntry() {}

	virtual void Init(byte level = 0);
	virtual void FoldAll() {}
	virtual void UnFoldAll() {}

	/**
	 * Set whether this is the last visible entry of the parent node.
	 * @param last_field Value to set
	 */
	void SetLastField(bool last_field) { if (last_field) SETBITS(this->flags, SEF_LAST_FIELD); else CLRBITS(this->flags, SEF_LAST_FIELD); }

	virtual uint Length() const = 0;
	virtual void GetFoldingState(bool &all_folded, bool &all_unfolded) const {}
	virtual bool IsVisible(const BaseSettingEntry *item) const;
	virtual BaseSettingEntry *FindEntry(uint row, uint *cur_row);
	virtual uint GetMaxHelpHeight(int maxw) { return 0; }

	/**
	 * Check whether an entry is hidden due to filters
	 * @return true if hidden.
	 */
	bool IsFiltered() const { return (this->flags & SEF_FILTERED) != 0; }

	virtual bool UpdateFilterState(SettingFilter &filter, bool force_visible) = 0;

	virtual uint Draw(GameSettings *settings_ptr, int left, int right, int y, uint first_row, uint max_row, BaseSettingEntry *selected, uint cur_row = 0, uint parent_last = 0) const;

protected:
	virtual void DrawSetting(GameSettings *settings_ptr, int left, int right, int y, bool highlight) const = 0;
};

/** Standard setting */
struct SettingEntry : BaseSettingEntry {
	const char *name;           ///< Name of the setting
	const SettingDesc *setting; ///< Setting description of the setting
	uint index;                 ///< Index of the setting in the settings table

	SettingEntry(const char *name);

	virtual void Init(byte level = 0);
	virtual uint Length() const;
	virtual uint GetMaxHelpHeight(int maxw);
	virtual bool UpdateFilterState(SettingFilter &filter, bool force_visible);

	void SetButtons(byte new_val);

	/**
	 * Get the help text of a single setting.
	 * @return The requested help text.
	 */
	inline StringID GetHelpText() const
	{
		return this->setting->desc.str_help;
	}

	void SetValueDParams(uint first_param, int32 value) const;

protected:
	virtual void DrawSetting(GameSettings *settings_ptr, int left, int right, int y, bool highlight) const;

private:
	bool IsVisibleByRestrictionMode(RestrictionMode mode) const;
};

/** Containers for BaseSettingEntry */
struct SettingsContainer {
	typedef std::vector<BaseSettingEntry*> EntryVector;
	EntryVector entries; ///< Settings on this page

	template<typename T>
	T *Add(T *item)
	{
		this->entries.push_back(item);
		return item;
	}

	void Init(byte level = 0);
	void FoldAll();
	void UnFoldAll();

	uint Length() const;
	void GetFoldingState(bool &all_folded, bool &all_unfolded) const;
	bool IsVisible(const BaseSettingEntry *item) const;
	BaseSettingEntry *FindEntry(uint row, uint *cur_row);
	uint GetMaxHelpHeight(int maxw);

	bool UpdateFilterState(SettingFilter &filter, bool force_visible);

	uint Draw(GameSettings *settings_ptr, int left, int right, int y, uint first_row, uint max_row, BaseSettingEntry *selected, uint cur_row = 0, uint parent_last = 0) const;
};

/** Data structure describing one page of settings in the settings window. */
struct SettingsPage : BaseSettingEntry, SettingsContainer {
	StringID title;     ///< Title of the sub-page
	bool folded;        ///< Sub-page is folded (not visible except for its title)

	SettingsPage(StringID title);

	virtual void Init(byte level = 0);
	virtual void FoldAll();
	virtual void UnFoldAll();

	virtual uint Length() const;
	virtual void GetFoldingState(bool &all_folded, bool &all_unfolded) const;
	virtual bool IsVisible(const BaseSettingEntry *item) const;
	virtual BaseSettingEntry *FindEntry(uint row, uint *cur_row);
	virtual uint GetMaxHelpHeight(int maxw) { return SettingsContainer::GetMaxHelpHeight(maxw); }

	virtual bool UpdateFilterState(SettingFilter &filter, bool force_visible);

	virtual uint Draw(GameSettings *settings_ptr, int left, int right, int y, uint first_row, uint max_row, BaseSettingEntry *selected, uint cur_row = 0, uint parent_last = 0) const;

protected:
	virtual void DrawSetting(GameSettings *settings_ptr, int left, int right, int y, bool highlight) const;
};

/* == BaseSettingEntry methods == */

/**
 * Initialization of a setting entry
 * @param level      Page nesting level of this entry
 */
void BaseSettingEntry::Init(byte level)
{
	this->level = level;
}

/**
 * Check whether an entry is visible and not folded or filtered away.
 * Note: This does not consider the scrolling range; it might still require scrolling to make the setting really visible.
 * @param item Entry to search for.
 * @return true if entry is visible.
 */
bool BaseSettingEntry::IsVisible(const BaseSettingEntry *item) const
{
	if (this->IsFiltered()) return false;
	if (this == item) return true;
	return false;
}

/**
 * Find setting entry at row \a row_num
 * @param row_num Index of entry to return
 * @param cur_row Current row number
 * @return The requested setting entry or \c nullptr if it not found (folded or filtered)
 */
BaseSettingEntry *BaseSettingEntry::FindEntry(uint row_num, uint *cur_row)
{
	if (this->IsFiltered()) return nullptr;
	if (row_num == *cur_row) return this;
	(*cur_row)++;
	return nullptr;
}

/**
 * Draw a row in the settings panel.
 *
 * The scrollbar uses rows of the page, while the page data structure is a tree of #SettingsPage and #SettingEntry objects.
 * As a result, the drawing routing traverses the tree from top to bottom, counting rows in \a cur_row until it reaches \a first_row.
 * Then it enables drawing rows while traversing until \a max_row is reached, at which point drawing is terminated.
 *
 * The \a parent_last parameter ensures that the vertical lines at the left are
 * only drawn when another entry follows, that it prevents output like
 * \verbatim
 *  |-- setting
 *  |-- (-) - Title
 *  |    |-- setting
 *  |    |-- setting
 * \endverbatim
 * The left-most vertical line is not wanted. It is prevented by setting the
 * appropriate bit in the \a parent_last parameter.
 *
 * @param settings_ptr Pointer to current values of all settings
 * @param left         Left-most position in window/panel to start drawing \a first_row
 * @param right        Right-most x position to draw strings at.
 * @param y            Upper-most position in window/panel to start drawing \a first_row
 * @param first_row    First row number to draw
 * @param max_row      Row-number to stop drawing (the row-number of the row below the last row to draw)
 * @param selected     Selected entry by the user.
 * @param cur_row      Current row number (internal variable)
 * @param parent_last  Last-field booleans of parent page level (page level \e i sets bit \e i to 1 if it is its last field)
 * @return Row number of the next row to draw
 */
uint BaseSettingEntry::Draw(GameSettings *settings_ptr, int left, int right, int y, uint first_row, uint max_row, BaseSettingEntry *selected, uint cur_row, uint parent_last) const
{
	if (this->IsFiltered()) return cur_row;
	if (cur_row >= max_row) return cur_row;

	bool rtl = _current_text_dir == TD_RTL;
	int offset = rtl ? -4 : 4;
	int level_width = rtl ? -LEVEL_WIDTH : LEVEL_WIDTH;

	int x = rtl ? right : left;
	if (cur_row >= first_row) {
		int colour = _colour_gradient[COLOUR_ORANGE][4];
		y += (cur_row - first_row) * SETTING_HEIGHT; // Compute correct y start position

		/* Draw vertical for parent nesting levels */
		for (uint lvl = 0; lvl < this->level; lvl++) {
			if (!HasBit(parent_last, lvl)) GfxDrawLine(x + offset, y, x + offset, y + SETTING_HEIGHT - 1, colour);
			x += level_width;
		}
		/* draw own |- prefix */
		int halfway_y = y + SETTING_HEIGHT / 2;
		int bottom_y = (flags & SEF_LAST_FIELD) ? halfway_y : y + SETTING_HEIGHT - 1;
		GfxDrawLine(x + offset, y, x + offset, bottom_y, colour);
		/* Small horizontal line from the last vertical line */
		GfxDrawLine(x + offset, halfway_y, x + level_width - offset, halfway_y, colour);
		x += level_width;

		this->DrawSetting(settings_ptr, rtl ? left : x, rtl ? x : right, y, this == selected);
	}
	cur_row++;

	return cur_row;
}

/* == SettingEntry methods == */

/**
 * Constructor for a single setting in the 'advanced settings' window
 * @param name Name of the setting in the setting table
 */
SettingEntry::SettingEntry(const char *name)
{
	this->name = name;
	this->setting = nullptr;
	this->index = 0;
}

/**
 * Initialization of a setting entry
 * @param level      Page nesting level of this entry
 */
void SettingEntry::Init(byte level)
{
	BaseSettingEntry::Init(level);
	this->setting = GetSettingFromName(this->name, &this->index);
	assert(this->setting != nullptr);
}

/**
 * Set the button-depressed flags (#SEF_LEFT_DEPRESSED and #SEF_RIGHT_DEPRESSED) to a specified value
 * @param new_val New value for the button flags
 * @see SettingEntryFlags
 */
void SettingEntry::SetButtons(byte new_val)
{
	assert((new_val & ~SEF_BUTTONS_MASK) == 0); // Should not touch any flags outside the buttons
	this->flags = (this->flags & ~SEF_BUTTONS_MASK) | new_val;
}

/** Return number of rows needed to display the (filtered) entry */
uint SettingEntry::Length() const
{
	return this->IsFiltered() ? 0 : 1;
}

/**
 * Get the biggest height of the help text(s), if the width is at least \a maxw. Help text gets wrapped if needed.
 * @param maxw Maximal width of a line help text.
 * @return Biggest height needed to display any help text of this node (and its descendants).
 */
uint SettingEntry::GetMaxHelpHeight(int maxw)
{
	return GetStringHeight(this->GetHelpText(), maxw);
}

/**
 * Checks whether an entry shall be made visible based on the restriction mode.
 * @param mode The current status of the restriction drop down box.
 * @return true if the entry shall be visible.
 */
bool SettingEntry::IsVisibleByRestrictionMode(RestrictionMode mode) const
{
	/* There shall not be any restriction, i.e. all settings shall be visible. */
	if (mode == RM_ALL) return true;

	GameSettings *settings_ptr = &GetGameSettings();
	const SettingDesc *sd = this->setting;

	if (mode == RM_BASIC) return (this->setting->desc.cat & SC_BASIC_LIST) != 0;
	if (mode == RM_ADVANCED) return (this->setting->desc.cat & SC_ADVANCED_LIST) != 0;

	/* Read the current value. */
	const void *var = ResolveVariableAddress(settings_ptr, sd);
	int64 current_value = ReadValue(var, sd->save.conv);

	int64 filter_value;

	if (mode == RM_CHANGED_AGAINST_DEFAULT) {
		/* This entry shall only be visible, if the value deviates from its default value. */

		/* Read the default value. */
		filter_value = ReadValue(&sd->desc.def, sd->save.conv);
	} else {
		assert(mode == RM_CHANGED_AGAINST_NEW);
		/* This entry shall only be visible, if the value deviates from
		 * its value is used when starting a new game. */

		/* Make sure we're not comparing the new game settings against itself. */
		assert(settings_ptr != &_settings_newgame);

		/* Read the new game's value. */
		var = ResolveVariableAddress(&_settings_newgame, sd);
		filter_value = ReadValue(var, sd->save.conv);
	}

	return current_value != filter_value;
}

/**
 * Update the filter state.
 * @param filter Filter
 * @param force_visible Whether to force all items visible, no matter what (due to filter text; not affected by restriction drop down box).
 * @return true if item remains visible
 */
bool SettingEntry::UpdateFilterState(SettingFilter &filter, bool force_visible)
{
	CLRBITS(this->flags, SEF_FILTERED);

	bool visible = true;

	const SettingDesc *sd = this->setting;
	if (!force_visible && !filter.string.IsEmpty()) {
		/* Process the search text filter for this item. */
		filter.string.ResetState();

		const SettingDescBase *sdb = &sd->desc;

		SetDParam(0, STR_EMPTY);
		filter.string.AddLine(sdb->str);
		filter.string.AddLine(this->GetHelpText());

		visible = filter.string.GetState();
	}

	if (visible) {
		if (filter.type != ST_ALL && sd->GetType() != filter.type) {
			filter.type_hides = true;
			visible = false;
		}
		if (!this->IsVisibleByRestrictionMode(filter.mode)) {
			while (filter.min_cat < RM_ALL && (filter.min_cat == filter.mode || !this->IsVisibleByRestrictionMode(filter.min_cat))) filter.min_cat++;
			visible = false;
		}
	}

	if (!visible) SETBITS(this->flags, SEF_FILTERED);
	return visible;
}


static const void *ResolveVariableAddress(const GameSettings *settings_ptr, const SettingDesc *sd)
{
	if ((sd->desc.flags & SGF_PER_COMPANY) != 0) {
		if (Company::IsValidID(_local_company) && _game_mode != GM_MENU) {
			return GetVariableAddress(&Company::Get(_local_company)->settings, &sd->save);
		} else {
			return GetVariableAddress(&_settings_client.company, &sd->save);
		}
	} else {
		return GetVariableAddress(settings_ptr, &sd->save);
	}
}

/**
 * Set the DParams for drawing the value of a setting.
 * @param first_param First DParam to use
 * @param value Setting value to set params for.
 */
void SettingEntry::SetValueDParams(uint first_param, int32 value) const
{
	const SettingDescBase *sdb = &this->setting->desc;
	if (sdb->cmd == SDT_BOOLX) {
		SetDParam(first_param++, value != 0 ? STR_CONFIG_SETTING_ON : STR_CONFIG_SETTING_OFF);
	} else {
		if ((sdb->flags & SGF_MULTISTRING) != 0) {
			SetDParam(first_param++, sdb->str_val - sdb->min + value);
		} else if ((sdb->flags & SGF_DISPLAY_ABS) != 0) {
			SetDParam(first_param++, sdb->str_val + ((value >= 0) ? 1 : 0));
			value = abs(value);
		} else {
			SetDParam(first_param++, sdb->str_val + ((value == 0 && (sdb->flags & SGF_0ISDISABLED) != 0) ? 1 : 0));
		}
		SetDParam(first_param++, value);
	}
}

/**
 * Function to draw setting value (button + text + current value)
 * @param settings_ptr Pointer to current values of all settings
 * @param left         Left-most position in window/panel to start drawing
 * @param right        Right-most position in window/panel to draw
 * @param y            Upper-most position in window/panel to start drawing
 * @param highlight    Highlight entry.
 */
void SettingEntry::DrawSetting(GameSettings *settings_ptr, int left, int right, int y, bool highlight) const
{
	const SettingDesc *sd = this->setting;
	const SettingDescBase *sdb = &sd->desc;
	const void *var = ResolveVariableAddress(settings_ptr, sd);
	int state = this->flags & SEF_BUTTONS_MASK;

	bool rtl = _current_text_dir == TD_RTL;
	uint buttons_left = rtl ? right + 1 - SETTING_BUTTON_WIDTH : left;
	uint text_left  = left + (rtl ? 0 : SETTING_BUTTON_WIDTH + 5);
	uint text_right = right - (rtl ? SETTING_BUTTON_WIDTH + 5 : 0);
	uint button_y = y + (SETTING_HEIGHT - SETTING_BUTTON_HEIGHT) / 2;

	/* We do not allow changes of some items when we are a client in a networkgame */
	bool editable = sd->IsEditable();

	SetDParam(0, highlight ? STR_ORANGE_STRING1_WHITE : STR_ORANGE_STRING1_LTBLUE);
	int32 value = (int32)ReadValue(var, sd->save.conv);
	if (sdb->cmd == SDT_BOOLX) {
		/* Draw checkbox for boolean-value either on/off */
		DrawBoolButton(buttons_left, button_y, value != 0, editable);
	} else if ((sdb->flags & SGF_MULTISTRING) != 0) {
		/* Draw [v] button for settings of an enum-type */
		DrawDropDownButton(buttons_left, button_y, COLOUR_YELLOW, state != 0, editable);
	} else {
		/* Draw [<][>] boxes for settings of an integer-type */
		DrawArrowButtons(buttons_left, button_y, COLOUR_YELLOW, state,
				editable && value != (sdb->flags & SGF_0ISDISABLED ? 0 : sdb->min), editable && (uint32)value != sdb->max);
	}
	this->SetValueDParams(1, value);
	DrawString(text_left, text_right, y + (SETTING_HEIGHT - FONT_HEIGHT_NORMAL) / 2, sdb->str, highlight ? TC_WHITE : TC_LIGHT_BLUE);
}

/* == SettingsContainer methods == */

/**
 * Initialization of an entire setting page
 * @param level Nesting level of this page (internal variable, do not provide a value for it when calling)
 */
void SettingsContainer::Init(byte level)
{
	for (EntryVector::iterator it = this->entries.begin(); it != this->entries.end(); ++it) {
		(*it)->Init(level);
	}
}

/** Recursively close all folds of sub-pages */
void SettingsContainer::FoldAll()
{
	for (EntryVector::iterator it = this->entries.begin(); it != this->entries.end(); ++it) {
		(*it)->FoldAll();
	}
}

/** Recursively open all folds of sub-pages */
void SettingsContainer::UnFoldAll()
{
	for (EntryVector::iterator it = this->entries.begin(); it != this->entries.end(); ++it) {
		(*it)->UnFoldAll();
	}
}

/**
 * Recursively accumulate the folding state of the tree.
 * @param[in,out] all_folded Set to false, if one entry is not folded.
 * @param[in,out] all_unfolded Set to false, if one entry is folded.
 */
void SettingsContainer::GetFoldingState(bool &all_folded, bool &all_unfolded) const
{
	for (EntryVector::const_iterator it = this->entries.begin(); it != this->entries.end(); ++it) {
		(*it)->GetFoldingState(all_folded, all_unfolded);
	}
}

/**
 * Update the filter state.
 * @param filter Filter
 * @param force_visible Whether to force all items visible, no matter what
 * @return true if item remains visible
 */
bool SettingsContainer::UpdateFilterState(SettingFilter &filter, bool force_visible)
{
	bool visible = false;
	bool first_visible = true;
	for (EntryVector::reverse_iterator it = this->entries.rbegin(); it != this->entries.rend(); ++it) {
		visible |= (*it)->UpdateFilterState(filter, force_visible);
		(*it)->SetLastField(first_visible);
		if (visible && first_visible) first_visible = false;
	}
	return visible;
}


/**
 * Check whether an entry is visible and not folded or filtered away.
 * Note: This does not consider the scrolling range; it might still require scrolling to make the setting really visible.
 * @param item Entry to search for.
 * @return true if entry is visible.
 */
bool SettingsContainer::IsVisible(const BaseSettingEntry *item) const
{
	for (EntryVector::const_iterator it = this->entries.begin(); it != this->entries.end(); ++it) {
		if ((*it)->IsVisible(item)) return true;
	}
	return false;
}

/** Return number of rows needed to display the whole page */
uint SettingsContainer::Length() const
{
	uint length = 0;
	for (EntryVector::const_iterator it = this->entries.begin(); it != this->entries.end(); ++it) {
		length += (*it)->Length();
	}
	return length;
}

/**
 * Find the setting entry at row number \a row_num
 * @param row_num Index of entry to return
 * @param cur_row Variable used for keeping track of the current row number. Should point to memory initialized to \c 0 when first called.
 * @return The requested setting entry or \c nullptr if it does not exist
 */
BaseSettingEntry *SettingsContainer::FindEntry(uint row_num, uint *cur_row)
{
	BaseSettingEntry *pe = nullptr;
	for (EntryVector::iterator it = this->entries.begin(); it != this->entries.end(); ++it) {
		pe = (*it)->FindEntry(row_num, cur_row);
		if (pe != nullptr) {
			break;
		}
	}
	return pe;
}

/**
 * Get the biggest height of the help texts, if the width is at least \a maxw. Help text gets wrapped if needed.
 * @param maxw Maximal width of a line help text.
 * @return Biggest height needed to display any help text of this (sub-)tree.
 */
uint SettingsContainer::GetMaxHelpHeight(int maxw)
{
	uint biggest = 0;
	for (EntryVector::const_iterator it = this->entries.begin(); it != this->entries.end(); ++it) {
		biggest = max(biggest, (*it)->GetMaxHelpHeight(maxw));
	}
	return biggest;
}


/**
 * Draw a row in the settings panel.
 *
 * @param settings_ptr Pointer to current values of all settings
 * @param left         Left-most position in window/panel to start drawing \a first_row
 * @param right        Right-most x position to draw strings at.
 * @param y            Upper-most position in window/panel to start drawing \a first_row
 * @param first_row    First row number to draw
 * @param max_row      Row-number to stop drawing (the row-number of the row below the last row to draw)
 * @param selected     Selected entry by the user.
 * @param cur_row      Current row number (internal variable)
 * @param parent_last  Last-field booleans of parent page level (page level \e i sets bit \e i to 1 if it is its last field)
 * @return Row number of the next row to draw
 */
uint SettingsContainer::Draw(GameSettings *settings_ptr, int left, int right, int y, uint first_row, uint max_row, BaseSettingEntry *selected, uint cur_row, uint parent_last) const
{
	for (EntryVector::const_iterator it = this->entries.begin(); it != this->entries.end(); ++it) {
		cur_row = (*it)->Draw(settings_ptr, left, right, y, first_row, max_row, selected, cur_row, parent_last);
		if (cur_row >= max_row) {
			break;
		}
	}
	return cur_row;
}

/* == SettingsPage methods == */

/**
 * Constructor for a sub-page in the 'advanced settings' window
 * @param title Title of the sub-page
 */
SettingsPage::SettingsPage(StringID title)
{
	this->title = title;
	this->folded = true;
}

/**
 * Initialization of an entire setting page
 * @param level Nesting level of this page (internal variable, do not provide a value for it when calling)
 */
void SettingsPage::Init(byte level)
{
	BaseSettingEntry::Init(level);
	SettingsContainer::Init(level + 1);
}

/** Recursively close all (filtered) folds of sub-pages */
void SettingsPage::FoldAll()
{
	if (this->IsFiltered()) return;
	this->folded = true;

	SettingsContainer::FoldAll();
}

/** Recursively open all (filtered) folds of sub-pages */
void SettingsPage::UnFoldAll()
{
	if (this->IsFiltered()) return;
	this->folded = false;

	SettingsContainer::UnFoldAll();
}

/**
 * Recursively accumulate the folding state of the (filtered) tree.
 * @param[in,out] all_folded Set to false, if one entry is not folded.
 * @param[in,out] all_unfolded Set to false, if one entry is folded.
 */
void SettingsPage::GetFoldingState(bool &all_folded, bool &all_unfolded) const
{
	if (this->IsFiltered()) return;

	if (this->folded) {
		all_unfolded = false;
	} else {
		all_folded = false;
	}

	SettingsContainer::GetFoldingState(all_folded, all_unfolded);
}

/**
 * Update the filter state.
 * @param filter Filter
 * @param force_visible Whether to force all items visible, no matter what (due to filter text; not affected by restriction drop down box).
 * @return true if item remains visible
 */
bool SettingsPage::UpdateFilterState(SettingFilter &filter, bool force_visible)
{
	if (!force_visible && !filter.string.IsEmpty()) {
		filter.string.ResetState();
		filter.string.AddLine(this->title);
		force_visible = filter.string.GetState();
	}

	bool visible = SettingsContainer::UpdateFilterState(filter, force_visible);
	if (visible) {
		CLRBITS(this->flags, SEF_FILTERED);
	} else {
		SETBITS(this->flags, SEF_FILTERED);
	}
	return visible;
}

/**
 * Check whether an entry is visible and not folded or filtered away.
 * Note: This does not consider the scrolling range; it might still require scrolling to make the setting really visible.
 * @param item Entry to search for.
 * @return true if entry is visible.
 */
bool SettingsPage::IsVisible(const BaseSettingEntry *item) const
{
	if (this->IsFiltered()) return false;
	if (this == item) return true;
	if (this->folded) return false;

	return SettingsContainer::IsVisible(item);
}

/** Return number of rows needed to display the (filtered) entry */
uint SettingsPage::Length() const
{
	if (this->IsFiltered()) return 0;
	if (this->folded) return 1; // Only displaying the title

	return 1 + SettingsContainer::Length();
}

/**
 * Find setting entry at row \a row_num
 * @param row_num Index of entry to return
 * @param cur_row Current row number
 * @return The requested setting entry or \c nullptr if it not found (folded or filtered)
 */
BaseSettingEntry *SettingsPage::FindEntry(uint row_num, uint *cur_row)
{
	if (this->IsFiltered()) return nullptr;
	if (row_num == *cur_row) return this;
	(*cur_row)++;
	if (this->folded) return nullptr;

	return SettingsContainer::FindEntry(row_num, cur_row);
}

/**
 * Draw a row in the settings panel.
 *
 * @param settings_ptr Pointer to current values of all settings
 * @param left         Left-most position in window/panel to start drawing \a first_row
 * @param right        Right-most x position to draw strings at.
 * @param y            Upper-most position in window/panel to start drawing \a first_row
 * @param first_row    First row number to draw
 * @param max_row      Row-number to stop drawing (the row-number of the row below the last row to draw)
 * @param selected     Selected entry by the user.
 * @param cur_row      Current row number (internal variable)
 * @param parent_last  Last-field booleans of parent page level (page level \e i sets bit \e i to 1 if it is its last field)
 * @return Row number of the next row to draw
 */
uint SettingsPage::Draw(GameSettings *settings_ptr, int left, int right, int y, uint first_row, uint max_row, BaseSettingEntry *selected, uint cur_row, uint parent_last) const
{
	if (this->IsFiltered()) return cur_row;
	if (cur_row >= max_row) return cur_row;

	cur_row = BaseSettingEntry::Draw(settings_ptr, left, right, y, first_row, max_row, selected, cur_row, parent_last);

	if (!this->folded) {
		if (this->flags & SEF_LAST_FIELD) {
			assert(this->level < 8 * sizeof(parent_last));
			SetBit(parent_last, this->level); // Add own last-field state
		}

		cur_row = SettingsContainer::Draw(settings_ptr, left, right, y, first_row, max_row, selected, cur_row, parent_last);
	}

	return cur_row;
}

/**
 * Function to draw setting value (button + text + current value)
 * @param settings_ptr Pointer to current values of all settings
 * @param left         Left-most position in window/panel to start drawing
 * @param right        Right-most position in window/panel to draw
 * @param y            Upper-most position in window/panel to start drawing
 * @param highlight    Highlight entry.
 */
void SettingsPage::DrawSetting(GameSettings *settings_ptr, int left, int right, int y, bool highlight) const
{
	bool rtl = _current_text_dir == TD_RTL;
	DrawSprite((this->folded ? SPR_CIRCLE_FOLDED : SPR_CIRCLE_UNFOLDED), PAL_NONE, rtl ? right - _circle_size.width : left, y + (SETTING_HEIGHT - _circle_size.height) / 2);
	DrawString(rtl ? left : left + _circle_size.width + 2, rtl ? right - _circle_size.width - 2 : right, y + (SETTING_HEIGHT - FONT_HEIGHT_NORMAL) / 2, this->title);
}

/** Construct settings tree */
static SettingsContainer &GetSettingsTree()
{
	static SettingsContainer *main = nullptr;

	if (main == nullptr)
	{
		/* Build up the dynamic settings-array only once per OpenTTD session */
		main = new SettingsContainer();

		SettingsPage *localisation = main->Add(new SettingsPage(STR_CONFIG_SETTING_LOCALISATION));
		{
			localisation->Add(new SettingEntry("locale.units_velocity"));
			localisation->Add(new SettingEntry("locale.units_power"));
			localisation->Add(new SettingEntry("locale.units_weight"));
			localisation->Add(new SettingEntry("locale.units_volume"));
			localisation->Add(new SettingEntry("locale.units_force"));
			localisation->Add(new SettingEntry("locale.units_height"));
			localisation->Add(new SettingEntry("gui.date_format_in_default_names"));
		}

		SettingsPage *graphics = main->Add(new SettingsPage(STR_CONFIG_SETTING_GRAPHICS));
		{
			graphics->Add(new SettingEntry("gui.zoom_min"));
			graphics->Add(new SettingEntry("gui.zoom_max"));
			graphics->Add(new SettingEntry("gui.smallmap_land_colour"));
			graphics->Add(new SettingEntry("gui.graph_line_thickness"));
		}

		SettingsPage *sound = main->Add(new SettingsPage(STR_CONFIG_SETTING_SOUND));
		{
			sound->Add(new SettingEntry("sound.click_beep"));
			sound->Add(new SettingEntry("sound.confirm"));
			sound->Add(new SettingEntry("sound.news_ticker"));
			sound->Add(new SettingEntry("sound.news_full"));
			sound->Add(new SettingEntry("sound.new_year"));
			sound->Add(new SettingEntry("sound.disaster"));
			sound->Add(new SettingEntry("sound.vehicle"));
			sound->Add(new SettingEntry("sound.ambient"));
		}

		SettingsPage *interface = main->Add(new SettingsPage(STR_CONFIG_SETTING_INTERFACE));
		{
			SettingsPage *general = interface->Add(new SettingsPage(STR_CONFIG_SETTING_INTERFACE_GENERAL));
			{
				general->Add(new SettingEntry("gui.osk_activation"));
				general->Add(new SettingEntry("gui.hover_delay_ms"));
				general->Add(new SettingEntry("gui.errmsg_duration"));
				general->Add(new SettingEntry("gui.window_snap_radius"));
				general->Add(new SettingEntry("gui.window_soft_limit"));
				general->Add(new SettingEntry("gui.right_mouse_wnd_close"));
			}

			SettingsPage *viewports = interface->Add(new SettingsPage(STR_CONFIG_SETTING_INTERFACE_VIEWPORTS));
			{
				viewports->Add(new SettingEntry("gui.auto_scrolling"));
				viewports->Add(new SettingEntry("gui.scroll_mode"));
				viewports->Add(new SettingEntry("gui.smooth_scroll"));
				/* While the horizontal scrollwheel scrolling is written as general code, only
				 *  the cocoa (OSX) driver generates input for it.
				 *  Since it's also able to completely disable the scrollwheel will we display it on all platforms anyway */
				viewports->Add(new SettingEntry("gui.scrollwheel_scrolling"));
				viewports->Add(new SettingEntry("gui.scrollwheel_multiplier"));
#ifdef __APPLE__
				/* We might need to emulate a right mouse button on mac */
				viewports->Add(new SettingEntry("gui.right_mouse_btn_emulation"));
#endif
				viewports->Add(new SettingEntry("gui.population_in_label"));
				viewports->Add(new SettingEntry("gui.liveries"));
				viewports->Add(new SettingEntry("construction.train_signal_side"));
				viewports->Add(new SettingEntry("gui.measure_tooltip"));
				viewports->Add(new SettingEntry("gui.loading_indicators"));
				viewports->Add(new SettingEntry("gui.show_track_reservation"));
			}

			SettingsPage *construction = interface->Add(new SettingsPage(STR_CONFIG_SETTING_INTERFACE_CONSTRUCTION));
			{
				construction->Add(new SettingEntry("gui.link_terraform_toolbar"));
				construction->Add(new SettingEntry("gui.enable_signal_gui"));
				construction->Add(new SettingEntry("gui.persistent_buildingtools"));
				construction->Add(new SettingEntry("gui.quick_goto"));
				construction->Add(new SettingEntry("gui.default_rail_type"));
				construction->Add(new SettingEntry("gui.disable_unsuitable_building"));
			}

			interface->Add(new SettingEntry("gui.autosave"));
			interface->Add(new SettingEntry("gui.toolbar_pos"));
			interface->Add(new SettingEntry("gui.statusbar_pos"));
			interface->Add(new SettingEntry("gui.prefer_teamchat"));
			interface->Add(new SettingEntry("gui.advanced_vehicle_list"));
			interface->Add(new SettingEntry("gui.timetable_in_ticks"));
			interface->Add(new SettingEntry("gui.timetable_arrival_departure"));
			interface->Add(new SettingEntry("gui.expenses_layout"));
			interface->Add(new SettingEntry("gui.show_newgrf_name"));
		}

		SettingsPage *advisors = main->Add(new SettingsPage(STR_CONFIG_SETTING_ADVISORS));
		{
			advisors->Add(new SettingEntry("gui.coloured_news_year"));
			advisors->Add(new SettingEntry("news_display.general"));
			advisors->Add(new SettingEntry("news_display.new_vehicles"));
			advisors->Add(new SettingEntry("news_display.accident"));
			advisors->Add(new SettingEntry("news_display.company_info"));
			advisors->Add(new SettingEntry("news_display.acceptance"));
			advisors->Add(new SettingEntry("news_display.arrival_player"));
			advisors->Add(new SettingEntry("news_display.arrival_other"));
			advisors->Add(new SettingEntry("news_display.advice"));
			advisors->Add(new SettingEntry("gui.order_review_system"));
			advisors->Add(new SettingEntry("gui.vehicle_income_warn"));
			advisors->Add(new SettingEntry("gui.lost_vehicle_warn"));
			advisors->Add(new SettingEntry("gui.show_finances"));
			advisors->Add(new SettingEntry("news_display.economy"));
			advisors->Add(new SettingEntry("news_display.subsidies"));
			advisors->Add(new SettingEntry("news_display.open"));
			advisors->Add(new SettingEntry("news_display.close"));
			advisors->Add(new SettingEntry("news_display.production_player"));
			advisors->Add(new SettingEntry("news_display.production_other"));
			advisors->Add(new SettingEntry("news_display.production_nobody"));
		}

		SettingsPage *company = main->Add(new SettingsPage(STR_CONFIG_SETTING_COMPANY));
		{
			company->Add(new SettingEntry("gui.semaphore_build_before"));
			company->Add(new SettingEntry("gui.default_signal_type"));
			company->Add(new SettingEntry("gui.cycle_signal_types"));
			company->Add(new SettingEntry("gui.drag_signals_fixed_distance"));
			company->Add(new SettingEntry("gui.new_nonstop"));
			company->Add(new SettingEntry("gui.stop_location"));
			company->Add(new SettingEntry("gui.starting_colour"));
			company->Add(new SettingEntry("company.engine_renew"));
			company->Add(new SettingEntry("company.engine_renew_months"));
			company->Add(new SettingEntry("company.engine_renew_money"));
			company->Add(new SettingEntry("vehicle.servint_ispercent"));
			company->Add(new SettingEntry("vehicle.servint_trains"));
			company->Add(new SettingEntry("vehicle.servint_roadveh"));
			company->Add(new SettingEntry("vehicle.servint_ships"));
			company->Add(new SettingEntry("vehicle.servint_aircraft"));
			company->Add(new SettingEntry("network.company_autoprotect"));
		}

		SettingsPage *accounting = main->Add(new SettingsPage(STR_CONFIG_SETTING_ACCOUNTING));
		{
			accounting->Add(new SettingEntry("economy.inflation"));
			accounting->Add(new SettingEntry("difficulty.initial_interest"));
			accounting->Add(new SettingEntry("difficulty.max_loan"));
			accounting->Add(new SettingEntry("difficulty.subsidy_multiplier"));
			accounting->Add(new SettingEntry("economy.feeder_payment_share"));
			accounting->Add(new SettingEntry("economy.infrastructure_maintenance"));
			accounting->Add(new SettingEntry("difficulty.vehicle_costs"));
			accounting->Add(new SettingEntry("difficulty.construction_cost"));
		}

		SettingsPage *vehicles = main->Add(new SettingsPage(STR_CONFIG_SETTING_VEHICLES));
		{
			SettingsPage *physics = vehicles->Add(new SettingsPage(STR_CONFIG_SETTING_VEHICLES_PHYSICS));
			{
				physics->Add(new SettingEntry("vehicle.train_acceleration_model"));
				physics->Add(new SettingEntry("vehicle.train_slope_steepness"));
				physics->Add(new SettingEntry("vehicle.wagon_speed_limits"));
				physics->Add(new SettingEntry("vehicle.freight_trains"));
				physics->Add(new SettingEntry("vehicle.roadveh_acceleration_model"));
				physics->Add(new SettingEntry("vehicle.roadveh_slope_steepness"));
				physics->Add(new SettingEntry("vehicle.smoke_amount"));
				physics->Add(new SettingEntry("vehicle.plane_speed"));
			}

			SettingsPage *routing = vehicles->Add(new SettingsPage(STR_CONFIG_SETTING_VEHICLES_ROUTING));
			{
				routing->Add(new SettingEntry("pf.pathfinder_for_trains"));
				routing->Add(new SettingEntry("difficulty.line_reverse_mode"));
				routing->Add(new SettingEntry("pf.reverse_at_signals"));
				routing->Add(new SettingEntry("pf.forbid_90_deg"));
				routing->Add(new SettingEntry("pf.pathfinder_for_roadvehs"));
				routing->Add(new SettingEntry("pf.pathfinder_for_ships"));
			}

			vehicles->Add(new SettingEntry("order.no_servicing_if_no_breakdowns"));
			vehicles->Add(new SettingEntry("order.serviceathelipad"));
		}

		SettingsPage *limitations = main->Add(new SettingsPage(STR_CONFIG_SETTING_LIMITATIONS));
		{
			limitations->Add(new SettingEntry("construction.command_pause_level"));
			limitations->Add(new SettingEntry("construction.autoslope"));
			limitations->Add(new SettingEntry("construction.extra_dynamite"));
			limitations->Add(new SettingEntry("construction.max_heightlevel"));
			limitations->Add(new SettingEntry("construction.max_bridge_length"));
			limitations->Add(new SettingEntry("construction.max_bridge_height"));
			limitations->Add(new SettingEntry("construction.max_tunnel_length"));
			limitations->Add(new SettingEntry("station.never_expire_airports"));
			limitations->Add(new SettingEntry("vehicle.never_expire_vehicles"));
			limitations->Add(new SettingEntry("vehicle.max_trains"));
			limitations->Add(new SettingEntry("vehicle.max_roadveh"));
			limitations->Add(new SettingEntry("vehicle.max_aircraft"));
			limitations->Add(new SettingEntry("vehicle.max_ships"));
			limitations->Add(new SettingEntry("vehicle.max_train_length"));
			limitations->Add(new SettingEntry("station.station_spread"));
			limitations->Add(new SettingEntry("station.distant_join_stations"));
			limitations->Add(new SettingEntry("construction.road_stop_on_town_road"));
			limitations->Add(new SettingEntry("construction.road_stop_on_competitor_road"));
			limitations->Add(new SettingEntry("vehicle.disable_elrails"));
		}

		SettingsPage *disasters = main->Add(new SettingsPage(STR_CONFIG_SETTING_ACCIDENTS));
		{
			disasters->Add(new SettingEntry("difficulty.disasters"));
			disasters->Add(new SettingEntry("difficulty.economy"));
			disasters->Add(new SettingEntry("difficulty.vehicle_breakdowns"));
			disasters->Add(new SettingEntry("vehicle.plane_crashes"));
		}

		SettingsPage *genworld = main->Add(new SettingsPage(STR_CONFIG_SETTING_GENWORLD));
		{
			genworld->Add(new SettingEntry("game_creation.landscape"));
			genworld->Add(new SettingEntry("game_creation.land_generator"));
			genworld->Add(new SettingEntry("difficulty.terrain_type"));
			genworld->Add(new SettingEntry("game_creation.tgen_smoothness"));
			genworld->Add(new SettingEntry("game_creation.variety"));
			genworld->Add(new SettingEntry("game_creation.snow_line_height"));
			genworld->Add(new SettingEntry("game_creation.amount_of_rivers"));
			genworld->Add(new SettingEntry("game_creation.tree_placer"));
			genworld->Add(new SettingEntry("vehicle.road_side"));
			genworld->Add(new SettingEntry("economy.larger_towns"));
			genworld->Add(new SettingEntry("economy.initial_city_size"));
			genworld->Add(new SettingEntry("economy.town_layout"));
			genworld->Add(new SettingEntry("difficulty.industry_density"));
			genworld->Add(new SettingEntry("gui.pause_on_newgame"));
			genworld->Add(new SettingEntry("game_creation.ending_year"));
		}

		SettingsPage *environment = main->Add(new SettingsPage(STR_CONFIG_SETTING_ENVIRONMENT));
		{
			SettingsPage *authorities = environment->Add(new SettingsPage(STR_CONFIG_SETTING_ENVIRONMENT_AUTHORITIES));
			{
				authorities->Add(new SettingEntry("difficulty.town_council_tolerance"));
				authorities->Add(new SettingEntry("economy.bribe"));
				authorities->Add(new SettingEntry("economy.exclusive_rights"));
				authorities->Add(new SettingEntry("economy.fund_roads"));
				authorities->Add(new SettingEntry("economy.fund_buildings"));
				authorities->Add(new SettingEntry("economy.station_noise_level"));
			}

			SettingsPage *towns = environment->Add(new SettingsPage(STR_CONFIG_SETTING_ENVIRONMENT_TOWNS));
			{
				towns->Add(new SettingEntry("economy.town_growth_rate"));
				towns->Add(new SettingEntry("economy.allow_town_roads"));
				towns->Add(new SettingEntry("economy.allow_town_level_crossings"));
				towns->Add(new SettingEntry("economy.found_town"));
				towns->Add(new SettingEntry("economy.town_cargogen_mode"));
			}

			SettingsPage *industries = environment->Add(new SettingsPage(STR_CONFIG_SETTING_ENVIRONMENT_INDUSTRIES));
			{
				industries->Add(new SettingEntry("construction.raw_industry_construction"));
				industries->Add(new SettingEntry("construction.industry_platform"));
				industries->Add(new SettingEntry("economy.multiple_industry_per_town"));
				industries->Add(new SettingEntry("game_creation.oil_refinery_limit"));
				industries->Add(new SettingEntry("economy.type"));
				industries->Add(new SettingEntry("station.serve_neutral_industries"));
			}

			SettingsPage *cdist = environment->Add(new SettingsPage(STR_CONFIG_SETTING_ENVIRONMENT_CARGODIST));
			{
				cdist->Add(new SettingEntry("linkgraph.recalc_time"));
				cdist->Add(new SettingEntry("linkgraph.recalc_interval"));
				cdist->Add(new SettingEntry("linkgraph.distribution_pax"));
				cdist->Add(new SettingEntry("linkgraph.distribution_mail"));
				cdist->Add(new SettingEntry("linkgraph.distribution_armoured"));
				cdist->Add(new SettingEntry("linkgraph.distribution_default"));
				cdist->Add(new SettingEntry("linkgraph.accuracy"));
				cdist->Add(new SettingEntry("linkgraph.demand_distance"));
				cdist->Add(new SettingEntry("linkgraph.demand_size"));
				cdist->Add(new SettingEntry("linkgraph.short_path_saturation"));
			}

			environment->Add(new SettingEntry("station.modified_catchment"));
			environment->Add(new SettingEntry("construction.extra_tree_placement"));
		}

		SettingsPage *ai = main->Add(new SettingsPage(STR_CONFIG_SETTING_AI));
		{
			SettingsPage *npc = ai->Add(new SettingsPage(STR_CONFIG_SETTING_AI_NPC));
			{
				npc->Add(new SettingEntry("script.settings_profile"));
				npc->Add(new SettingEntry("script.script_max_opcode_till_suspend"));
				npc->Add(new SettingEntry("script.script_max_memory_megabytes"));
				npc->Add(new SettingEntry("difficulty.competitor_speed"));
				npc->Add(new SettingEntry("ai.ai_in_multiplayer"));
				npc->Add(new SettingEntry("ai.ai_disable_veh_train"));
				npc->Add(new SettingEntry("ai.ai_disable_veh_roadveh"));
				npc->Add(new SettingEntry("ai.ai_disable_veh_aircraft"));
				npc->Add(new SettingEntry("ai.ai_disable_veh_ship"));
			}

			ai->Add(new SettingEntry("economy.give_money"));
			ai->Add(new SettingEntry("economy.allow_shares"));
			ai->Add(new SettingEntry("economy.min_years_for_shares"));
		}

		main->Init();
	}
	return *main;
}

static const StringID _game_settings_restrict_dropdown[] = {
	STR_CONFIG_SETTING_RESTRICT_BASIC,                            // RM_BASIC
	STR_CONFIG_SETTING_RESTRICT_ADVANCED,                         // RM_ADVANCED
	STR_CONFIG_SETTING_RESTRICT_ALL,                              // RM_ALL
	STR_CONFIG_SETTING_RESTRICT_CHANGED_AGAINST_DEFAULT,          // RM_CHANGED_AGAINST_DEFAULT
	STR_CONFIG_SETTING_RESTRICT_CHANGED_AGAINST_NEW,              // RM_CHANGED_AGAINST_NEW
};
assert_compile(lengthof(_game_settings_restrict_dropdown) == RM_END);

/** Warnings about hidden search results. */
enum WarnHiddenResult {
	WHR_NONE,          ///< Nothing was filtering matches away.
	WHR_CATEGORY,      ///< Category setting filtered matches away.
	WHR_TYPE,          ///< Type setting filtered matches away.
	WHR_CATEGORY_TYPE, ///< Both category and type settings filtered matches away.
};

/** Window to edit settings of the game. */
struct GameSettingsWindow : Window {
	static const int SETTINGTREE_LEFT_OFFSET   = 5; ///< Position of left edge of setting values
	static const int SETTINGTREE_RIGHT_OFFSET  = 5; ///< Position of right edge of setting values
	static const int SETTINGTREE_TOP_OFFSET    = 5; ///< Position of top edge of setting values
	static const int SETTINGTREE_BOTTOM_OFFSET = 5; ///< Position of bottom edge of setting values

	static GameSettings *settings_ptr; ///< Pointer to the game settings being displayed and modified.

	SettingEntry *valuewindow_entry;   ///< If non-nullptr, pointer to setting for which a value-entering window has been opened.
	SettingEntry *clicked_entry;       ///< If non-nullptr, pointer to a clicked numeric setting (with a depressed left or right button).
	SettingEntry *last_clicked;        ///< If non-nullptr, pointer to the last clicked setting.
	SettingEntry *valuedropdown_entry; ///< If non-nullptr, pointer to the value for which a dropdown window is currently opened.
	bool closing_dropdown;             ///< True, if the dropdown list is currently closing.

	SettingFilter filter;              ///< Filter for the list.
	QueryString filter_editbox;        ///< Filter editbox;
	bool manually_changed_folding;     ///< Whether the user expanded/collapsed something manually.
	WarnHiddenResult warn_missing;     ///< Whether and how to warn about missing search results.
	int warn_lines;                    ///< Number of lines used for warning about missing search results.

	Scrollbar *vscroll;

	GameSettingsWindow(WindowDesc *desc) : Window(desc), filter_editbox(50)
	{
		this->warn_missing = WHR_NONE;
		this->warn_lines = 0;
		this->filter.mode = (RestrictionMode)_settings_client.gui.settings_restriction_mode;
		this->filter.min_cat = RM_ALL;
		this->filter.type = ST_ALL;
		this->filter.type_hides = false;
		this->settings_ptr = &GetGameSettings();

		_circle_size = maxdim(GetSpriteSize(SPR_CIRCLE_FOLDED), GetSpriteSize(SPR_CIRCLE_UNFOLDED));
		GetSettingsTree().FoldAll(); // Close all sub-pages

		this->valuewindow_entry = nullptr; // No setting entry for which a entry window is opened
		this->clicked_entry = nullptr; // No numeric setting buttons are depressed
		this->last_clicked = nullptr;
		this->valuedropdown_entry = nullptr;
		this->closing_dropdown = false;
		this->manually_changed_folding = false;

		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_GS_SCROLLBAR);
		this->FinishInitNested(WN_GAME_OPTIONS_GAME_SETTINGS);

		this->querystrings[WID_GS_FILTER] = &this->filter_editbox;
		this->filter_editbox.cancel_button = QueryString::ACTION_CLEAR;
		this->SetFocusedWidget(WID_GS_FILTER);

		this->InvalidateData();
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_GS_OPTIONSPANEL:
				resize->height = SETTING_HEIGHT = max(max<int>(_circle_size.height, SETTING_BUTTON_HEIGHT), FONT_HEIGHT_NORMAL) + 1;
				resize->width  = 1;

				size->height = 5 * resize->height + SETTINGTREE_TOP_OFFSET + SETTINGTREE_BOTTOM_OFFSET;
				break;

			case WID_GS_HELP_TEXT: {
				static const StringID setting_types[] = {
					STR_CONFIG_SETTING_TYPE_CLIENT,
					STR_CONFIG_SETTING_TYPE_COMPANY_MENU, STR_CONFIG_SETTING_TYPE_COMPANY_INGAME,
					STR_CONFIG_SETTING_TYPE_GAME_MENU, STR_CONFIG_SETTING_TYPE_GAME_INGAME,
				};
				for (uint i = 0; i < lengthof(setting_types); i++) {
					SetDParam(0, setting_types[i]);
					size->width = max(size->width, GetStringBoundingBox(STR_CONFIG_SETTING_TYPE).width);
				}
				size->height = 2 * FONT_HEIGHT_NORMAL + WD_PAR_VSEP_NORMAL +
						max(size->height, GetSettingsTree().GetMaxHelpHeight(size->width));
				break;
			}

			case WID_GS_RESTRICT_CATEGORY:
			case WID_GS_RESTRICT_TYPE:
				size->width = max(GetStringBoundingBox(STR_CONFIG_SETTING_RESTRICT_CATEGORY).width, GetStringBoundingBox(STR_CONFIG_SETTING_RESTRICT_TYPE).width);
				break;

			default:
				break;
		}
	}

	void OnPaint() override
	{
		if (this->closing_dropdown) {
			this->closing_dropdown = false;
			assert(this->valuedropdown_entry != nullptr);
			this->valuedropdown_entry->SetButtons(0);
			this->valuedropdown_entry = nullptr;
		}

		/* Reserve the correct number of lines for the 'some search results are hidden' notice in the central settings display panel. */
		const NWidgetBase *panel = this->GetWidget<NWidgetBase>(WID_GS_OPTIONSPANEL);
		StringID warn_str = STR_CONFIG_SETTING_CATEGORY_HIDES - 1 + this->warn_missing;
		int new_warn_lines;
		if (this->warn_missing == WHR_NONE) {
			new_warn_lines = 0;
		} else {
			SetDParam(0, _game_settings_restrict_dropdown[this->filter.min_cat]);
			new_warn_lines = GetStringLineCount(warn_str, panel->current_x);
		}
		if (this->warn_lines != new_warn_lines) {
			this->vscroll->SetCount(this->vscroll->GetCount() - this->warn_lines + new_warn_lines);
			this->warn_lines = new_warn_lines;
		}

		this->DrawWidgets();

		/* Draw the 'some search results are hidden' notice. */
		if (this->warn_missing != WHR_NONE) {
			const int left = panel->pos_x;
			const int right = left + panel->current_x - 1;
			const int top = panel->pos_y + WD_FRAMETEXT_TOP + (SETTING_HEIGHT - FONT_HEIGHT_NORMAL) * this->warn_lines / 2;
			SetDParam(0, _game_settings_restrict_dropdown[this->filter.min_cat]);
			if (this->warn_lines == 1) {
				/* If the warning fits at one line, center it. */
				DrawString(left + WD_FRAMETEXT_LEFT, right - WD_FRAMETEXT_RIGHT, top, warn_str, TC_FROMSTRING, SA_HOR_CENTER);
			} else {
				DrawStringMultiLine(left + WD_FRAMERECT_LEFT, right - WD_FRAMERECT_RIGHT, top, INT32_MAX, warn_str, TC_FROMSTRING, SA_HOR_CENTER);
			}
		}
	}

	void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_GS_RESTRICT_DROPDOWN:
				SetDParam(0, _game_settings_restrict_dropdown[this->filter.mode]);
				break;

			case WID_GS_TYPE_DROPDOWN:
				switch (this->filter.type) {
					case ST_GAME:    SetDParam(0, _game_mode == GM_MENU ? STR_CONFIG_SETTING_TYPE_DROPDOWN_GAME_MENU : STR_CONFIG_SETTING_TYPE_DROPDOWN_GAME_INGAME); break;
					case ST_COMPANY: SetDParam(0, _game_mode == GM_MENU ? STR_CONFIG_SETTING_TYPE_DROPDOWN_COMPANY_MENU : STR_CONFIG_SETTING_TYPE_DROPDOWN_COMPANY_INGAME); break;
					case ST_CLIENT:  SetDParam(0, STR_CONFIG_SETTING_TYPE_DROPDOWN_CLIENT); break;
					default:         SetDParam(0, STR_CONFIG_SETTING_TYPE_DROPDOWN_ALL); break;
				}
				break;
		}
	}

	DropDownList BuildDropDownList(int widget) const
	{
		DropDownList list;
		switch (widget) {
			case WID_GS_RESTRICT_DROPDOWN:
				for (int mode = 0; mode != RM_END; mode++) {
					/* If we are in adv. settings screen for the new game's settings,
					 * we don't want to allow comparing with new game's settings. */
					bool disabled = mode == RM_CHANGED_AGAINST_NEW && settings_ptr == &_settings_newgame;

					list.emplace_back(new DropDownListStringItem(_game_settings_restrict_dropdown[mode], mode, disabled));
				}
				break;

			case WID_GS_TYPE_DROPDOWN:
				list.emplace_back(new DropDownListStringItem(STR_CONFIG_SETTING_TYPE_DROPDOWN_ALL, ST_ALL, false));
				list.emplace_back(new DropDownListStringItem(_game_mode == GM_MENU ? STR_CONFIG_SETTING_TYPE_DROPDOWN_GAME_MENU : STR_CONFIG_SETTING_TYPE_DROPDOWN_GAME_INGAME, ST_GAME, false));
				list.emplace_back(new DropDownListStringItem(_game_mode == GM_MENU ? STR_CONFIG_SETTING_TYPE_DROPDOWN_COMPANY_MENU : STR_CONFIG_SETTING_TYPE_DROPDOWN_COMPANY_INGAME, ST_COMPANY, false));
				list.emplace_back(new DropDownListStringItem(STR_CONFIG_SETTING_TYPE_DROPDOWN_CLIENT, ST_CLIENT, false));
				break;
		}
		return list;
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_GS_OPTIONSPANEL: {
				int top_pos = r.top + SETTINGTREE_TOP_OFFSET + 1 + this->warn_lines * SETTING_HEIGHT;
				uint last_row = this->vscroll->GetPosition() + this->vscroll->GetCapacity() - this->warn_lines;
				int next_row = GetSettingsTree().Draw(settings_ptr, r.left + SETTINGTREE_LEFT_OFFSET, r.right - SETTINGTREE_RIGHT_OFFSET, top_pos,
						this->vscroll->GetPosition(), last_row, this->last_clicked);
				if (next_row == 0) DrawString(r.left + SETTINGTREE_LEFT_OFFSET, r.right - SETTINGTREE_RIGHT_OFFSET, top_pos, STR_CONFIG_SETTINGS_NONE);
				break;
			}

			case WID_GS_HELP_TEXT:
				if (this->last_clicked != nullptr) {
					const SettingDesc *sd = this->last_clicked->setting;

					int y = r.top;
					switch (sd->GetType()) {
						case ST_COMPANY: SetDParam(0, _game_mode == GM_MENU ? STR_CONFIG_SETTING_TYPE_COMPANY_MENU : STR_CONFIG_SETTING_TYPE_COMPANY_INGAME); break;
						case ST_CLIENT:  SetDParam(0, STR_CONFIG_SETTING_TYPE_CLIENT); break;
						case ST_GAME:    SetDParam(0, _game_mode == GM_MENU ? STR_CONFIG_SETTING_TYPE_GAME_MENU : STR_CONFIG_SETTING_TYPE_GAME_INGAME); break;
						default: NOT_REACHED();
					}
					DrawString(r.left, r.right, y, STR_CONFIG_SETTING_TYPE);
					y += FONT_HEIGHT_NORMAL;

					int32 default_value = ReadValue(&sd->desc.def, sd->save.conv);
					this->last_clicked->SetValueDParams(0, default_value);
					DrawString(r.left, r.right, y, STR_CONFIG_SETTING_DEFAULT_VALUE);
					y += FONT_HEIGHT_NORMAL + WD_PAR_VSEP_NORMAL;

					DrawStringMultiLine(r.left, r.right, y, r.bottom, this->last_clicked->GetHelpText(), TC_WHITE);
				}
				break;

			default:
				break;
		}
	}

	/**
	 * Set the entry that should have its help text displayed, and mark the window dirty so it gets repainted.
	 * @param pe Setting to display help text of, use \c nullptr to stop displaying help of the currently displayed setting.
	 */
	void SetDisplayedHelpText(SettingEntry *pe)
	{
		if (this->last_clicked != pe) this->SetDirty();
		this->last_clicked = pe;
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_GS_EXPAND_ALL:
				this->manually_changed_folding = true;
				GetSettingsTree().UnFoldAll();
				this->InvalidateData();
				break;

			case WID_GS_COLLAPSE_ALL:
				this->manually_changed_folding = true;
				GetSettingsTree().FoldAll();
				this->InvalidateData();
				break;

			case WID_GS_RESTRICT_DROPDOWN: {
				DropDownList list = this->BuildDropDownList(widget);
				if (!list.empty()) {
					ShowDropDownList(this, std::move(list), this->filter.mode, widget);
				}
				break;
			}

			case WID_GS_TYPE_DROPDOWN: {
				DropDownList list = this->BuildDropDownList(widget);
				if (!list.empty()) {
					ShowDropDownList(this, std::move(list), this->filter.type, widget);
				}
				break;
			}
		}

		if (widget != WID_GS_OPTIONSPANEL) return;

		uint btn = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_GS_OPTIONSPANEL, SETTINGTREE_TOP_OFFSET);
		if (btn == INT_MAX || (int)btn < this->warn_lines) return;
		btn -= this->warn_lines;

		uint cur_row = 0;
		BaseSettingEntry *clicked_entry = GetSettingsTree().FindEntry(btn, &cur_row);

		if (clicked_entry == nullptr) return;  // Clicked below the last setting of the page

		int x = (_current_text_dir == TD_RTL ? this->width - 1 - pt.x : pt.x) - SETTINGTREE_LEFT_OFFSET - (clicked_entry->level + 1) * LEVEL_WIDTH;  // Shift x coordinate
		if (x < 0) return;  // Clicked left of the entry

		SettingsPage *clicked_page = dynamic_cast<SettingsPage*>(clicked_entry);
		if (clicked_page != nullptr) {
			this->SetDisplayedHelpText(nullptr);
			clicked_page->folded = !clicked_page->folded; // Flip 'folded'-ness of the sub-page

			this->manually_changed_folding = true;

			this->InvalidateData();
			return;
		}

		SettingEntry *pe = dynamic_cast<SettingEntry*>(clicked_entry);
		assert(pe != nullptr);
		const SettingDesc *sd = pe->setting;

		/* return if action is only active in network, or only settable by server */
		if (!sd->IsEditable()) {
			this->SetDisplayedHelpText(pe);
			return;
		}

		const void *var = ResolveVariableAddress(settings_ptr, sd);
		int32 value = (int32)ReadValue(var, sd->save.conv);

		/* clicked on the icon on the left side. Either scroller, bool on/off or dropdown */
		if (x < SETTING_BUTTON_WIDTH && (sd->desc.flags & SGF_MULTISTRING)) {
			const SettingDescBase *sdb = &sd->desc;
			this->SetDisplayedHelpText(pe);

			if (this->valuedropdown_entry == pe) {
				/* unclick the dropdown */
				HideDropDownMenu(this);
				this->closing_dropdown = false;
				this->valuedropdown_entry->SetButtons(0);
				this->valuedropdown_entry = nullptr;
			} else {
				if (this->valuedropdown_entry != nullptr) this->valuedropdown_entry->SetButtons(0);
				this->closing_dropdown = false;

				const NWidgetBase *wid = this->GetWidget<NWidgetBase>(WID_GS_OPTIONSPANEL);
				int rel_y = (pt.y - (int)wid->pos_y - SETTINGTREE_TOP_OFFSET) % wid->resize_y;

				Rect wi_rect;
				wi_rect.left = pt.x - (_current_text_dir == TD_RTL ? SETTING_BUTTON_WIDTH - 1 - x : x);
				wi_rect.right = wi_rect.left + SETTING_BUTTON_WIDTH - 1;
				wi_rect.top = pt.y - rel_y + (SETTING_HEIGHT - SETTING_BUTTON_HEIGHT) / 2;
				wi_rect.bottom = wi_rect.top + SETTING_BUTTON_HEIGHT - 1;

				/* For dropdowns we also have to check the y position thoroughly, the mouse may not above the just opening dropdown */
				if (pt.y >= wi_rect.top && pt.y <= wi_rect.bottom) {
					this->valuedropdown_entry = pe;
					this->valuedropdown_entry->SetButtons(SEF_LEFT_DEPRESSED);

					DropDownList list;
					for (int i = sdb->min; i <= (int)sdb->max; i++) {
						list.emplace_back(new DropDownListStringItem(sdb->str_val + i - sdb->min, i, false));
					}

					ShowDropDownListAt(this, std::move(list), value, -1, wi_rect, COLOUR_ORANGE, true);
				}
			}
			this->SetDirty();
		} else if (x < SETTING_BUTTON_WIDTH) {
			this->SetDisplayedHelpText(pe);
			const SettingDescBase *sdb = &sd->desc;
			int32 oldvalue = value;

			switch (sdb->cmd) {
				case SDT_BOOLX: value ^= 1; break;
				case SDT_ONEOFMANY:
				case SDT_NUMX: {
					/* Add a dynamic step-size to the scroller. In a maximum of
					 * 50-steps you should be able to get from min to max,
					 * unless specified otherwise in the 'interval' variable
					 * of the current setting. */
					uint32 step = (sdb->interval == 0) ? ((sdb->max - sdb->min) / 50) : sdb->interval;
					if (step == 0) step = 1;

					/* don't allow too fast scrolling */
					if ((this->flags & WF_TIMEOUT) && this->timeout_timer > 1) {
						_left_button_clicked = false;
						return;
					}

					/* Increase or decrease the value and clamp it to extremes */
					if (x >= SETTING_BUTTON_WIDTH / 2) {
						value += step;
						if (sdb->min < 0) {
							assert((int32)sdb->max >= 0);
							if (value > (int32)sdb->max) value = (int32)sdb->max;
						} else {
							if ((uint32)value > sdb->max) value = (int32)sdb->max;
						}
						if (value < sdb->min) value = sdb->min; // skip between "disabled" and minimum
					} else {
						value -= step;
						if (value < sdb->min) value = (sdb->flags & SGF_0ISDISABLED) ? 0 : sdb->min;
					}

					/* Set up scroller timeout for numeric values */
					if (value != oldvalue) {
						if (this->clicked_entry != nullptr) { // Release previous buttons if any
							this->clicked_entry->SetButtons(0);
						}
						this->clicked_entry = pe;
						this->clicked_entry->SetButtons((x >= SETTING_BUTTON_WIDTH / 2) != (_current_text_dir == TD_RTL) ? SEF_RIGHT_DEPRESSED : SEF_LEFT_DEPRESSED);
						this->SetTimeout();
						_left_button_clicked = false;
					}
					break;
				}

				default: NOT_REACHED();
			}

			if (value != oldvalue) {
				if ((sd->desc.flags & SGF_PER_COMPANY) != 0) {
					SetCompanySetting(pe->index, value);
				} else {
					SetSettingValue(pe->index, value);
				}
				this->SetDirty();
			}
		} else {
			/* Only open editbox if clicked for the second time, and only for types where it is sensible for. */
			if (this->last_clicked == pe && sd->desc.cmd != SDT_BOOLX && !(sd->desc.flags & SGF_MULTISTRING)) {
				/* Show the correct currency-translated value */
				if (sd->desc.flags & SGF_CURRENCY) value *= _currency->rate;

				this->valuewindow_entry = pe;
				SetDParam(0, value);
				ShowQueryString(STR_JUST_INT, STR_CONFIG_SETTING_QUERY_CAPTION, 10, this, CS_NUMERAL, QSF_ENABLE_DEFAULT);
			}
			this->SetDisplayedHelpText(pe);
		}
	}

	void OnTimeout() override
	{
		if (this->clicked_entry != nullptr) { // On timeout, release any depressed buttons
			this->clicked_entry->SetButtons(0);
			this->clicked_entry = nullptr;
			this->SetDirty();
		}
	}

	void OnQueryTextFinished(char *str) override
	{
		/* The user pressed cancel */
		if (str == nullptr) return;

		assert(this->valuewindow_entry != nullptr);
		const SettingDesc *sd = this->valuewindow_entry->setting;

		int32 value;
		if (!StrEmpty(str)) {
			value = atoi(str);

			/* Save the correct currency-translated value */
			if (sd->desc.flags & SGF_CURRENCY) value /= _currency->rate;
		} else {
			value = (int32)(size_t)sd->desc.def;
		}

		if ((sd->desc.flags & SGF_PER_COMPANY) != 0) {
			SetCompanySetting(this->valuewindow_entry->index, value);
		} else {
			SetSettingValue(this->valuewindow_entry->index, value);
		}
		this->SetDirty();
	}

	void OnDropdownSelect(int widget, int index) override
	{
		switch (widget) {
			case WID_GS_RESTRICT_DROPDOWN:
				this->filter.mode = (RestrictionMode)index;
				if (this->filter.mode == RM_CHANGED_AGAINST_DEFAULT ||
						this->filter.mode == RM_CHANGED_AGAINST_NEW) {

					if (!this->manually_changed_folding) {
						/* Expand all when selecting 'changes'. Update the filter state first, in case it becomes less restrictive in some cases. */
						GetSettingsTree().UpdateFilterState(this->filter, false);
						GetSettingsTree().UnFoldAll();
					}
				} else {
					/* Non-'changes' filter. Save as default. */
					_settings_client.gui.settings_restriction_mode = this->filter.mode;
				}
				this->InvalidateData();
				break;

			case WID_GS_TYPE_DROPDOWN:
				this->filter.type = (SettingType)index;
				this->InvalidateData();
				break;

			default:
				if (widget < 0) {
					/* Deal with drop down boxes on the panel. */
					assert(this->valuedropdown_entry != nullptr);
					const SettingDesc *sd = this->valuedropdown_entry->setting;
					assert(sd->desc.flags & SGF_MULTISTRING);

					if ((sd->desc.flags & SGF_PER_COMPANY) != 0) {
						SetCompanySetting(this->valuedropdown_entry->index, index);
					} else {
						SetSettingValue(this->valuedropdown_entry->index, index);
					}

					this->SetDirty();
				}
				break;
		}
	}

	void OnDropdownClose(Point pt, int widget, int index, bool instant_close) override
	{
		if (widget >= 0) {
			/* Normally the default implementation of OnDropdownClose() takes care of
			 * a few things. We want that behaviour here too, but only for
			 * "normal" dropdown boxes. The special dropdown boxes added for every
			 * setting that needs one can't have this call. */
			Window::OnDropdownClose(pt, widget, index, instant_close);
		} else {
			/* We cannot raise the dropdown button just yet. OnClick needs some hint, whether
			 * the same dropdown button was clicked again, and then not open the dropdown again.
			 * So, we only remember that it was closed, and process it on the next OnPaint, which is
			 * after OnClick. */
			assert(this->valuedropdown_entry != nullptr);
			this->closing_dropdown = true;
			this->SetDirty();
		}
	}

	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (!gui_scope) return;

		/* Update which settings are to be visible. */
		RestrictionMode min_level = (this->filter.mode <= RM_ALL) ? this->filter.mode : RM_BASIC;
		this->filter.min_cat = min_level;
		this->filter.type_hides = false;
		GetSettingsTree().UpdateFilterState(this->filter, false);

		if (this->filter.string.IsEmpty()) {
			this->warn_missing = WHR_NONE;
		} else if (min_level < this->filter.min_cat) {
			this->warn_missing = this->filter.type_hides ? WHR_CATEGORY_TYPE : WHR_CATEGORY;
		} else {
			this->warn_missing = this->filter.type_hides ? WHR_TYPE : WHR_NONE;
		}
		this->vscroll->SetCount(GetSettingsTree().Length() + this->warn_lines);

		if (this->last_clicked != nullptr && !GetSettingsTree().IsVisible(this->last_clicked)) {
			this->SetDisplayedHelpText(nullptr);
		}

		bool all_folded = true;
		bool all_unfolded = true;
		GetSettingsTree().GetFoldingState(all_folded, all_unfolded);
		this->SetWidgetDisabledState(WID_GS_EXPAND_ALL, all_unfolded);
		this->SetWidgetDisabledState(WID_GS_COLLAPSE_ALL, all_folded);
	}

	void OnEditboxChanged(int wid) override
	{
		if (wid == WID_GS_FILTER) {
			this->filter.string.SetFilterTerm(this->filter_editbox.text.buf);
			if (!this->filter.string.IsEmpty() && !this->manually_changed_folding) {
				/* User never expanded/collapsed single pages and entered a filter term.
				 * Expand everything, to save weird expand clicks, */
				GetSettingsTree().UnFoldAll();
			}
			this->InvalidateData();
		}
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_GS_OPTIONSPANEL, SETTINGTREE_TOP_OFFSET + SETTINGTREE_BOTTOM_OFFSET);
	}
};

GameSettings *GameSettingsWindow::settings_ptr = nullptr;

static const NWidgetPart _nested_settings_selection_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_MAUVE),
		NWidget(WWT_CAPTION, COLOUR_MAUVE), SetDataTip(STR_CONFIG_SETTING_TREE_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_DEFSIZEBOX, COLOUR_MAUVE),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_MAUVE),
		NWidget(NWID_VERTICAL), SetPIP(0, WD_PAR_VSEP_NORMAL, 0), SetPadding(WD_TEXTPANEL_TOP, 0, WD_TEXTPANEL_BOTTOM, 0),
			NWidget(NWID_HORIZONTAL), SetPIP(WD_FRAMETEXT_LEFT, WD_FRAMETEXT_RIGHT, WD_FRAMETEXT_RIGHT),
				NWidget(WWT_TEXT, COLOUR_MAUVE, WID_GS_RESTRICT_CATEGORY), SetDataTip(STR_CONFIG_SETTING_RESTRICT_CATEGORY, STR_NULL),
				NWidget(WWT_DROPDOWN, COLOUR_MAUVE, WID_GS_RESTRICT_DROPDOWN), SetMinimalSize(100, 12), SetDataTip(STR_BLACK_STRING, STR_CONFIG_SETTING_RESTRICT_DROPDOWN_HELPTEXT), SetFill(1, 0), SetResize(1, 0),
			EndContainer(),
			NWidget(NWID_HORIZONTAL), SetPIP(WD_FRAMETEXT_LEFT, WD_FRAMETEXT_RIGHT, WD_FRAMETEXT_RIGHT),
				NWidget(WWT_TEXT, COLOUR_MAUVE, WID_GS_RESTRICT_TYPE), SetDataTip(STR_CONFIG_SETTING_RESTRICT_TYPE, STR_NULL),
				NWidget(WWT_DROPDOWN, COLOUR_MAUVE, WID_GS_TYPE_DROPDOWN), SetMinimalSize(100, 12), SetDataTip(STR_BLACK_STRING, STR_CONFIG_SETTING_TYPE_DROPDOWN_HELPTEXT), SetFill(1, 0), SetResize(1, 0),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_HORIZONTAL), SetPadding(0, 0, WD_TEXTPANEL_BOTTOM, 0),
				SetPIP(WD_FRAMETEXT_LEFT, WD_FRAMETEXT_RIGHT, WD_FRAMETEXT_RIGHT),
			NWidget(WWT_TEXT, COLOUR_MAUVE), SetFill(0, 1), SetDataTip(STR_CONFIG_SETTING_FILTER_TITLE, STR_NULL),
			NWidget(WWT_EDITBOX, COLOUR_MAUVE, WID_GS_FILTER), SetFill(1, 0), SetMinimalSize(50, 12), SetResize(1, 0),
					SetDataTip(STR_LIST_FILTER_OSKTITLE, STR_LIST_FILTER_TOOLTIP),
		EndContainer(),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_MAUVE, WID_GS_OPTIONSPANEL), SetMinimalSize(400, 174), SetScrollbar(WID_GS_SCROLLBAR), EndContainer(),
		NWidget(NWID_VSCROLLBAR, COLOUR_MAUVE, WID_GS_SCROLLBAR),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_MAUVE), SetMinimalSize(400, 40),
		NWidget(WWT_EMPTY, INVALID_COLOUR, WID_GS_HELP_TEXT), SetMinimalSize(300, 25), SetFill(1, 1), SetResize(1, 0),
				SetPadding(WD_FRAMETEXT_TOP, WD_FRAMETEXT_RIGHT, WD_FRAMETEXT_BOTTOM, WD_FRAMETEXT_LEFT),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHTXTBTN, COLOUR_MAUVE, WID_GS_EXPAND_ALL), SetDataTip(STR_CONFIG_SETTING_EXPAND_ALL, STR_NULL),
		NWidget(WWT_PUSHTXTBTN, COLOUR_MAUVE, WID_GS_COLLAPSE_ALL), SetDataTip(STR_CONFIG_SETTING_COLLAPSE_ALL, STR_NULL),
		NWidget(WWT_PANEL, COLOUR_MAUVE), SetFill(1, 0), SetResize(1, 0),
		EndContainer(),
		NWidget(WWT_RESIZEBOX, COLOUR_MAUVE),
	EndContainer(),
};

static WindowDesc _settings_selection_desc(
	WDP_CENTER, "settings", 510, 450,
	WC_GAME_OPTIONS, WC_NONE,
	0,
	_nested_settings_selection_widgets, lengthof(_nested_settings_selection_widgets)
);

/** Open advanced settings window. */
void ShowGameSettings()
{
	DeleteWindowByClass(WC_GAME_OPTIONS);
	new GameSettingsWindow(&_settings_selection_desc);
}


/**
 * Draw [<][>] boxes.
 * @param x the x position to draw
 * @param y the y position to draw
 * @param button_colour the colour of the button
 * @param state 0 = none clicked, 1 = first clicked, 2 = second clicked
 * @param clickable_left is the left button clickable?
 * @param clickable_right is the right button clickable?
 */
void DrawArrowButtons(int x, int y, Colours button_colour, byte state, bool clickable_left, bool clickable_right)
{
	int colour = _colour_gradient[button_colour][2];
	Dimension dim = NWidgetScrollbar::GetHorizontalDimension();

	DrawFrameRect(x,             y, x + dim.width - 1,             y + dim.height - 1, button_colour, (state == 1) ? FR_LOWERED : FR_NONE);
	DrawFrameRect(x + dim.width, y, x + dim.width + dim.width - 1, y + dim.height - 1, button_colour, (state == 2) ? FR_LOWERED : FR_NONE);
	DrawSprite(SPR_ARROW_LEFT, PAL_NONE, x + WD_IMGBTN_LEFT, y + WD_IMGBTN_TOP);
	DrawSprite(SPR_ARROW_RIGHT, PAL_NONE, x + WD_IMGBTN_LEFT + dim.width, y + WD_IMGBTN_TOP);

	/* Grey out the buttons that aren't clickable */
	bool rtl = _current_text_dir == TD_RTL;
	if (rtl ? !clickable_right : !clickable_left) {
		GfxFillRect(x + 1, y, x + dim.width - 1, y + dim.height - 2, colour, FILLRECT_CHECKER);
	}
	if (rtl ? !clickable_left : !clickable_right) {
		GfxFillRect(x + dim.width + 1, y, x + dim.width + dim.width - 1, y + dim.height - 2, colour, FILLRECT_CHECKER);
	}
}

/**
 * Draw a dropdown button.
 * @param x the x position to draw
 * @param y the y position to draw
 * @param button_colour the colour of the button
 * @param state true = lowered
 * @param clickable is the button clickable?
 */
void DrawDropDownButton(int x, int y, Colours button_colour, bool state, bool clickable)
{
	int colour = _colour_gradient[button_colour][2];

	DrawFrameRect(x, y, x + SETTING_BUTTON_WIDTH - 1, y + SETTING_BUTTON_HEIGHT - 1, button_colour, state ? FR_LOWERED : FR_NONE);
	DrawSprite(SPR_ARROW_DOWN, PAL_NONE, x + (SETTING_BUTTON_WIDTH - NWidgetScrollbar::GetVerticalDimension().width) / 2 + state, y + 2 + state);

	if (!clickable) {
		GfxFillRect(x +  1, y, x + SETTING_BUTTON_WIDTH - 1, y + SETTING_BUTTON_HEIGHT - 2, colour, FILLRECT_CHECKER);
	}
}

/**
 * Draw a toggle button.
 * @param x the x position to draw
 * @param y the y position to draw
 * @param state true = lowered
 * @param clickable is the button clickable?
 */
void DrawBoolButton(int x, int y, bool state, bool clickable)
{
	static const Colours _bool_ctabs[2][2] = {{COLOUR_CREAM, COLOUR_RED}, {COLOUR_DARK_GREEN, COLOUR_GREEN}};
	DrawFrameRect(x, y, x + SETTING_BUTTON_WIDTH - 1, y + SETTING_BUTTON_HEIGHT - 1, _bool_ctabs[state][clickable], state ? FR_LOWERED : FR_NONE);
}

struct CustomCurrencyWindow : Window {
	int query_widget;

	CustomCurrencyWindow(WindowDesc *desc) : Window(desc)
	{
		this->InitNested();

		SetButtonState();
	}

	void SetButtonState()
	{
		this->SetWidgetDisabledState(WID_CC_RATE_DOWN, _custom_currency.rate == 1);
		this->SetWidgetDisabledState(WID_CC_RATE_UP, _custom_currency.rate == UINT16_MAX);
		this->SetWidgetDisabledState(WID_CC_YEAR_DOWN, _custom_currency.to_euro == CF_NOEURO);
		this->SetWidgetDisabledState(WID_CC_YEAR_UP, _custom_currency.to_euro == MAX_YEAR);
	}

	void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_CC_RATE:      SetDParam(0, 1); SetDParam(1, 1);            break;
			case WID_CC_SEPARATOR: SetDParamStr(0, _custom_currency.separator); break;
			case WID_CC_PREFIX:    SetDParamStr(0, _custom_currency.prefix);    break;
			case WID_CC_SUFFIX:    SetDParamStr(0, _custom_currency.suffix);    break;
			case WID_CC_YEAR:
				SetDParam(0, (_custom_currency.to_euro != CF_NOEURO) ? STR_CURRENCY_SWITCH_TO_EURO : STR_CURRENCY_SWITCH_TO_EURO_NEVER);
				SetDParam(1, _custom_currency.to_euro);
				break;

			case WID_CC_PREVIEW:
				SetDParam(0, 10000);
				break;
		}
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			/* Set the appropriate width for the edit 'buttons' */
			case WID_CC_SEPARATOR_EDIT:
			case WID_CC_PREFIX_EDIT:
			case WID_CC_SUFFIX_EDIT:
				size->width  = this->GetWidget<NWidgetBase>(WID_CC_RATE_DOWN)->smallest_x + this->GetWidget<NWidgetBase>(WID_CC_RATE_UP)->smallest_x;
				break;

			/* Make sure the window is wide enough for the widest exchange rate */
			case WID_CC_RATE:
				SetDParam(0, 1);
				SetDParam(1, INT32_MAX);
				*size = GetStringBoundingBox(STR_CURRENCY_EXCHANGE_RATE);
				break;
		}
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		int line = 0;
		int len = 0;
		StringID str = 0;
		CharSetFilter afilter = CS_ALPHANUMERAL;

		switch (widget) {
			case WID_CC_RATE_DOWN:
				if (_custom_currency.rate > 1) _custom_currency.rate--;
				if (_custom_currency.rate == 1) this->DisableWidget(WID_CC_RATE_DOWN);
				this->EnableWidget(WID_CC_RATE_UP);
				break;

			case WID_CC_RATE_UP:
				if (_custom_currency.rate < UINT16_MAX) _custom_currency.rate++;
				if (_custom_currency.rate == UINT16_MAX) this->DisableWidget(WID_CC_RATE_UP);
				this->EnableWidget(WID_CC_RATE_DOWN);
				break;

			case WID_CC_RATE:
				SetDParam(0, _custom_currency.rate);
				str = STR_JUST_INT;
				len = 5;
				line = WID_CC_RATE;
				afilter = CS_NUMERAL;
				break;

			case WID_CC_SEPARATOR_EDIT:
			case WID_CC_SEPARATOR:
				SetDParamStr(0, _custom_currency.separator);
				str = STR_JUST_RAW_STRING;
				len = 1;
				line = WID_CC_SEPARATOR;
				break;

			case WID_CC_PREFIX_EDIT:
			case WID_CC_PREFIX:
				SetDParamStr(0, _custom_currency.prefix);
				str = STR_JUST_RAW_STRING;
				len = 12;
				line = WID_CC_PREFIX;
				break;

			case WID_CC_SUFFIX_EDIT:
			case WID_CC_SUFFIX:
				SetDParamStr(0, _custom_currency.suffix);
				str = STR_JUST_RAW_STRING;
				len = 12;
				line = WID_CC_SUFFIX;
				break;

			case WID_CC_YEAR_DOWN:
				_custom_currency.to_euro = (_custom_currency.to_euro <= 2000) ? CF_NOEURO : _custom_currency.to_euro - 1;
				if (_custom_currency.to_euro == CF_NOEURO) this->DisableWidget(WID_CC_YEAR_DOWN);
				this->EnableWidget(WID_CC_YEAR_UP);
				break;

			case WID_CC_YEAR_UP:
				_custom_currency.to_euro = Clamp(_custom_currency.to_euro + 1, 2000, MAX_YEAR);
				if (_custom_currency.to_euro == MAX_YEAR) this->DisableWidget(WID_CC_YEAR_UP);
				this->EnableWidget(WID_CC_YEAR_DOWN);
				break;

			case WID_CC_YEAR:
				SetDParam(0, _custom_currency.to_euro);
				str = STR_JUST_INT;
				len = 7;
				line = WID_CC_YEAR;
				afilter = CS_NUMERAL;
				break;
		}

		if (len != 0) {
			this->query_widget = line;
			ShowQueryString(str, STR_CURRENCY_CHANGE_PARAMETER, len + 1, this, afilter, QSF_NONE);
		}

		this->SetTimeout();
		this->SetDirty();
	}

	void OnQueryTextFinished(char *str) override
	{
		if (str == nullptr) return;

		switch (this->query_widget) {
			case WID_CC_RATE:
				_custom_currency.rate = Clamp(atoi(str), 1, UINT16_MAX);
				break;

			case WID_CC_SEPARATOR: // Thousands separator
				strecpy(_custom_currency.separator, str, lastof(_custom_currency.separator));
				break;

			case WID_CC_PREFIX:
				strecpy(_custom_currency.prefix, str, lastof(_custom_currency.prefix));
				break;

			case WID_CC_SUFFIX:
				strecpy(_custom_currency.suffix, str, lastof(_custom_currency.suffix));
				break;

			case WID_CC_YEAR: { // Year to switch to euro
				int val = atoi(str);

				_custom_currency.to_euro = (val < 2000 ? CF_NOEURO : min(val, MAX_YEAR));
				break;
			}
		}
		MarkWholeScreenDirty();
		SetButtonState();
	}

	void OnTimeout() override
	{
		this->SetDirty();
	}
};

static const NWidgetPart _nested_cust_currency_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY), SetDataTip(STR_CURRENCY_WINDOW, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY),
		NWidget(NWID_VERTICAL, NC_EQUALSIZE), SetPIP(7, 3, 0),
			NWidget(NWID_HORIZONTAL), SetPIP(10, 0, 5),
				NWidget(WWT_PUSHARROWBTN, COLOUR_YELLOW, WID_CC_RATE_DOWN), SetDataTip(AWV_DECREASE, STR_CURRENCY_DECREASE_EXCHANGE_RATE_TOOLTIP),
				NWidget(WWT_PUSHARROWBTN, COLOUR_YELLOW, WID_CC_RATE_UP), SetDataTip(AWV_INCREASE, STR_CURRENCY_INCREASE_EXCHANGE_RATE_TOOLTIP),
				NWidget(NWID_SPACER), SetMinimalSize(5, 0),
				NWidget(WWT_TEXT, COLOUR_BLUE, WID_CC_RATE), SetDataTip(STR_CURRENCY_EXCHANGE_RATE, STR_CURRENCY_SET_EXCHANGE_RATE_TOOLTIP), SetFill(1, 0),
			EndContainer(),
			NWidget(NWID_HORIZONTAL), SetPIP(10, 0, 5),
				NWidget(WWT_PUSHBTN, COLOUR_DARK_BLUE, WID_CC_SEPARATOR_EDIT), SetDataTip(0x0, STR_CURRENCY_SET_CUSTOM_CURRENCY_SEPARATOR_TOOLTIP), SetFill(0, 1),
				NWidget(NWID_SPACER), SetMinimalSize(5, 0),
				NWidget(WWT_TEXT, COLOUR_BLUE, WID_CC_SEPARATOR), SetDataTip(STR_CURRENCY_SEPARATOR, STR_CURRENCY_SET_CUSTOM_CURRENCY_SEPARATOR_TOOLTIP), SetFill(1, 0),
			EndContainer(),
			NWidget(NWID_HORIZONTAL), SetPIP(10, 0, 5),
				NWidget(WWT_PUSHBTN, COLOUR_DARK_BLUE, WID_CC_PREFIX_EDIT), SetDataTip(0x0, STR_CURRENCY_SET_CUSTOM_CURRENCY_PREFIX_TOOLTIP), SetFill(0, 1),
				NWidget(NWID_SPACER), SetMinimalSize(5, 0),
				NWidget(WWT_TEXT, COLOUR_BLUE, WID_CC_PREFIX), SetDataTip(STR_CURRENCY_PREFIX, STR_CURRENCY_SET_CUSTOM_CURRENCY_PREFIX_TOOLTIP), SetFill(1, 0),
			EndContainer(),
			NWidget(NWID_HORIZONTAL), SetPIP(10, 0, 5),
				NWidget(WWT_PUSHBTN, COLOUR_DARK_BLUE, WID_CC_SUFFIX_EDIT), SetDataTip(0x0, STR_CURRENCY_SET_CUSTOM_CURRENCY_SUFFIX_TOOLTIP), SetFill(0, 1),
				NWidget(NWID_SPACER), SetMinimalSize(5, 0),
				NWidget(WWT_TEXT, COLOUR_BLUE, WID_CC_SUFFIX), SetDataTip(STR_CURRENCY_SUFFIX, STR_CURRENCY_SET_CUSTOM_CURRENCY_SUFFIX_TOOLTIP), SetFill(1, 0),
			EndContainer(),
			NWidget(NWID_HORIZONTAL), SetPIP(10, 0, 5),
				NWidget(WWT_PUSHARROWBTN, COLOUR_YELLOW, WID_CC_YEAR_DOWN), SetDataTip(AWV_DECREASE, STR_CURRENCY_DECREASE_CUSTOM_CURRENCY_TO_EURO_TOOLTIP),
				NWidget(WWT_PUSHARROWBTN, COLOUR_YELLOW, WID_CC_YEAR_UP), SetDataTip(AWV_INCREASE, STR_CURRENCY_INCREASE_CUSTOM_CURRENCY_TO_EURO_TOOLTIP),
				NWidget(NWID_SPACER), SetMinimalSize(5, 0),
				NWidget(WWT_TEXT, COLOUR_BLUE, WID_CC_YEAR), SetDataTip(STR_JUST_STRING, STR_CURRENCY_SET_CUSTOM_CURRENCY_TO_EURO_TOOLTIP), SetFill(1, 0),
			EndContainer(),
		EndContainer(),
		NWidget(WWT_LABEL, COLOUR_BLUE, WID_CC_PREVIEW),
								SetDataTip(STR_CURRENCY_PREVIEW, STR_CURRENCY_CUSTOM_CURRENCY_PREVIEW_TOOLTIP), SetPadding(15, 1, 18, 2),
	EndContainer(),
};

static WindowDesc _cust_currency_desc(
	WDP_CENTER, nullptr, 0, 0,
	WC_CUSTOM_CURRENCY, WC_NONE,
	0,
	_nested_cust_currency_widgets, lengthof(_nested_cust_currency_widgets)
);

/** Open custom currency window. */
static void ShowCustCurrency()
{
	DeleteWindowById(WC_CUSTOM_CURRENCY, 0);
	new CustomCurrencyWindow(&_cust_currency_desc);
}
