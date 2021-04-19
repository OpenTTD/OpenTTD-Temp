/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file intro_gui.cpp The main menu GUI. */

#include "stdafx.h"
#include "error.h"
#include "gui.h"
#include "window_gui.h"
#include "textbuf_gui.h"
#include "help_gui.h"
#include "network/network.h"
#include "genworld.h"
#include "network/network_gui.h"
#include "network/network_content.h"
#include "landscape_type.h"
#include "strings_func.h"
#include "fios.h"
#include "ai/ai_gui.hpp"
#include "gfx_func.h"
#include "core/geometry_func.hpp"
#include "language.h"
#include "rev.h"
#include "highscore.h"

#include "widgets/intro_widget.h"

#include "table/strings.h"
#include "table/sprites.h"

#include "safeguards.h"

struct SelectGameWindow : public Window {

	SelectGameWindow(WindowDesc *desc) : Window(desc)
	{
		this->CreateNestedTree();
		this->FinishInitNested(0);
		this->OnInvalidateData();
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (!gui_scope) return;
		this->SetWidgetLoweredState(WID_SGI_TEMPERATE_LANDSCAPE, _settings_newgame.game_creation.landscape == LT_TEMPERATE);
		this->SetWidgetLoweredState(WID_SGI_ARCTIC_LANDSCAPE,    _settings_newgame.game_creation.landscape == LT_ARCTIC);
		this->SetWidgetLoweredState(WID_SGI_TROPIC_LANDSCAPE,    _settings_newgame.game_creation.landscape == LT_TROPIC);
		this->SetWidgetLoweredState(WID_SGI_TOYLAND_LANDSCAPE,   _settings_newgame.game_creation.landscape == LT_TOYLAND);
	}

	void OnInit() override
	{
		bool missing_sprites = _missing_extra_graphics > 0 && !IsReleasedVersion();
		this->GetWidget<NWidgetStacked>(WID_SGI_BASESET_SELECTION)->SetDisplayedPlane(missing_sprites ? 0 : SZSP_NONE);

		bool missing_lang = _current_language->missing >= _settings_client.gui.missing_strings_threshold && !IsReleasedVersion();
		this->GetWidget<NWidgetStacked>(WID_SGI_TRANSLATION_SELECTION)->SetDisplayedPlane(missing_lang ? 0 : SZSP_NONE);
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_SGI_BASESET:
				SetDParam(0, _missing_extra_graphics);
				DrawStringMultiLine(r.left, r.right, r.top,  r.bottom, STR_INTRO_BASESET, TC_FROMSTRING, SA_CENTER);
				break;

			case WID_SGI_TRANSLATION:
				SetDParam(0, _current_language->missing);
				DrawStringMultiLine(r.left, r.right, r.top,  r.bottom, STR_INTRO_TRANSLATION, TC_FROMSTRING, SA_CENTER);
				break;
		}
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		StringID str = 0;
		switch (widget) {
			case WID_SGI_BASESET:
				SetDParam(0, _missing_extra_graphics);
				str = STR_INTRO_BASESET;
				break;

			case WID_SGI_TRANSLATION:
				SetDParam(0, _current_language->missing);
				str = STR_INTRO_TRANSLATION;
				break;
		}

		if (str != 0) {
			int height = GetStringHeight(str, size->width);
			if (height > 3 * FONT_HEIGHT_NORMAL) {
				/* Don't let the window become too high. */
				Dimension textdim = GetStringBoundingBox(str);
				textdim.height *= 3;
				textdim.width -= textdim.width / 2;
				*size = maxdim(*size, textdim);
			} else {
				size->height = height + padding.height;
			}
		}
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		/* Do not create a network server when you (just) have closed one of the game
		 * creation/load windows for the network server. */
		if (IsInsideMM(widget, WID_SGI_GENERATE_GAME, WID_SGI_EDIT_SCENARIO + 1)) _is_network_server = false;

		switch (widget) {
			case WID_SGI_GENERATE_GAME:
				if (_ctrl_pressed) {
					StartNewGameWithoutGUI(GENERATE_NEW_SEED);
				} else {
					ShowGenerateLandscape();
				}
				break;

			case WID_SGI_LOAD_GAME:      ShowSaveLoadDialog(FT_SAVEGAME, SLO_LOAD); break;
			case WID_SGI_PLAY_SCENARIO:  ShowSaveLoadDialog(FT_SCENARIO, SLO_LOAD); break;
			case WID_SGI_PLAY_HEIGHTMAP: ShowSaveLoadDialog(FT_HEIGHTMAP,SLO_LOAD); break;
			case WID_SGI_EDIT_SCENARIO:  StartScenarioEditor(); break;

			case WID_SGI_PLAY_NETWORK:
				if (!_network_available) {
					ShowErrorMessage(STR_NETWORK_ERROR_NOTAVAILABLE, INVALID_STRING_ID, WL_ERROR);
				} else {
					ShowNetworkGameWindow();
				}
				break;

			case WID_SGI_TEMPERATE_LANDSCAPE: case WID_SGI_ARCTIC_LANDSCAPE:
			case WID_SGI_TROPIC_LANDSCAPE: case WID_SGI_TOYLAND_LANDSCAPE:
				SetNewLandscapeType(widget - WID_SGI_TEMPERATE_LANDSCAPE);
				break;

			case WID_SGI_OPTIONS:         ShowGameOptions(); break;
			case WID_SGI_HIGHSCORE:       ShowHighscoreTable(); break;
			case WID_SGI_HELP:            ShowHelpWindow(); break;
			case WID_SGI_SETTINGS_OPTIONS:ShowGameSettings(); break;
			case WID_SGI_GRF_SETTINGS:    ShowNewGRFSettings(true, true, false, &_grfconfig_newgame); break;
			case WID_SGI_CONTENT_DOWNLOAD:
				if (!_network_available) {
					ShowErrorMessage(STR_NETWORK_ERROR_NOTAVAILABLE, INVALID_STRING_ID, WL_ERROR);
				} else {
					ShowNetworkContentListWindow();
				}
				break;
			case WID_SGI_AI_SETTINGS:     ShowAIConfigWindow(); break;
			case WID_SGI_EXIT:            HandleExitGameRequest(); break;
		}
	}
};

static const NWidgetPart _nested_select_game_widgets[] = {
	NWidget(WWT_CAPTION, COLOUR_BROWN), SetDataTip(STR_INTRO_CAPTION, STR_NULL),
	NWidget(WWT_PANEL, COLOUR_BROWN),
	NWidget(NWID_SPACER), SetMinimalSize(0, 8),

	/* 'generate game' and 'load game' buttons */
	NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
		NWidget(WWT_PUSHTXTBTN, COLOUR_ORANGE, WID_SGI_GENERATE_GAME), SetMinimalSize(158, 12),
							SetDataTip(STR_INTRO_NEW_GAME, STR_INTRO_TOOLTIP_NEW_GAME), SetPadding(0, 0, 0, 10), SetFill(1, 0),
		NWidget(WWT_PUSHTXTBTN, COLOUR_ORANGE, WID_SGI_LOAD_GAME), SetMinimalSize(158, 12),
							SetDataTip(STR_INTRO_LOAD_GAME, STR_INTRO_TOOLTIP_LOAD_GAME), SetPadding(0, 10, 0, 0), SetFill(1, 0),
	EndContainer(),

	NWidget(NWID_SPACER), SetMinimalSize(0, 6),

	/* 'play scenario' and 'play heightmap' buttons */
	NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
		NWidget(WWT_PUSHTXTBTN, COLOUR_ORANGE, WID_SGI_PLAY_SCENARIO), SetMinimalSize(158, 12),
							SetDataTip(STR_INTRO_PLAY_SCENARIO, STR_INTRO_TOOLTIP_PLAY_SCENARIO), SetPadding(0, 0, 0, 10), SetFill(1, 0),
		NWidget(WWT_PUSHTXTBTN, COLOUR_ORANGE, WID_SGI_PLAY_HEIGHTMAP), SetMinimalSize(158, 12),
							SetDataTip(STR_INTRO_PLAY_HEIGHTMAP, STR_INTRO_TOOLTIP_PLAY_HEIGHTMAP), SetPadding(0, 10, 0, 0), SetFill(1, 0),
	EndContainer(),

	NWidget(NWID_SPACER), SetMinimalSize(0, 6),

	/* 'edit scenario' and 'play multiplayer' buttons */
	NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
		NWidget(WWT_PUSHTXTBTN, COLOUR_ORANGE, WID_SGI_EDIT_SCENARIO), SetMinimalSize(158, 12),
							SetDataTip(STR_INTRO_SCENARIO_EDITOR, STR_INTRO_TOOLTIP_SCENARIO_EDITOR), SetPadding(0, 0, 0, 10), SetFill(1, 0),
		NWidget(WWT_PUSHTXTBTN, COLOUR_ORANGE, WID_SGI_PLAY_NETWORK), SetMinimalSize(158, 12),
							SetDataTip(STR_INTRO_MULTIPLAYER, STR_INTRO_TOOLTIP_MULTIPLAYER), SetPadding(0, 10, 0, 0), SetFill(1, 0),
	EndContainer(),

	NWidget(NWID_SPACER), SetMinimalSize(0, 7),

	/* climate selection buttons */
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_SPACER), SetMinimalSize(10, 0), SetFill(1, 0),
		NWidget(WWT_IMGBTN_2, COLOUR_ORANGE, WID_SGI_TEMPERATE_LANDSCAPE), SetMinimalSize(77, 55),
							SetDataTip(SPR_SELECT_TEMPERATE, STR_INTRO_TOOLTIP_TEMPERATE),
		NWidget(NWID_SPACER), SetMinimalSize(3, 0), SetFill(1, 0),
		NWidget(WWT_IMGBTN_2, COLOUR_ORANGE, WID_SGI_ARCTIC_LANDSCAPE), SetMinimalSize(77, 55),
							SetDataTip(SPR_SELECT_SUB_ARCTIC, STR_INTRO_TOOLTIP_SUB_ARCTIC_LANDSCAPE),
		NWidget(NWID_SPACER), SetMinimalSize(3, 0), SetFill(1, 0),
		NWidget(WWT_IMGBTN_2, COLOUR_ORANGE, WID_SGI_TROPIC_LANDSCAPE), SetMinimalSize(77, 55),
							SetDataTip(SPR_SELECT_SUB_TROPICAL, STR_INTRO_TOOLTIP_SUB_TROPICAL_LANDSCAPE),
		NWidget(NWID_SPACER), SetMinimalSize(3, 0), SetFill(1, 0),
		NWidget(WWT_IMGBTN_2, COLOUR_ORANGE, WID_SGI_TOYLAND_LANDSCAPE), SetMinimalSize(77, 55),
							SetDataTip(SPR_SELECT_TOYLAND, STR_INTRO_TOOLTIP_TOYLAND_LANDSCAPE),
		NWidget(NWID_SPACER), SetMinimalSize(10, 0), SetFill(1, 0),
	EndContainer(),

	NWidget(NWID_SPACER), SetMinimalSize(0, 7),
	NWidget(NWID_SELECTION, INVALID_COLOUR, WID_SGI_BASESET_SELECTION),
		NWidget(NWID_VERTICAL),
			NWidget(WWT_EMPTY, COLOUR_ORANGE, WID_SGI_BASESET), SetMinimalSize(316, 12), SetFill(1, 0), SetPadding(0, 10, 7, 10),
		EndContainer(),
	EndContainer(),
	NWidget(NWID_SELECTION, INVALID_COLOUR, WID_SGI_TRANSLATION_SELECTION),
		NWidget(NWID_VERTICAL),
			NWidget(WWT_EMPTY, COLOUR_ORANGE, WID_SGI_TRANSLATION), SetMinimalSize(316, 12), SetFill(1, 0), SetPadding(0, 10, 7, 10),
		EndContainer(),
	EndContainer(),

	/* 'game options' and 'advanced settings' buttons */
	NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
		NWidget(WWT_PUSHTXTBTN, COLOUR_ORANGE, WID_SGI_OPTIONS), SetMinimalSize(158, 12),
							SetDataTip(STR_INTRO_GAME_OPTIONS, STR_INTRO_TOOLTIP_GAME_OPTIONS), SetPadding(0, 0, 0, 10), SetFill(1, 0),
		NWidget(WWT_PUSHTXTBTN, COLOUR_ORANGE, WID_SGI_SETTINGS_OPTIONS), SetMinimalSize(158, 12),
							SetDataTip(STR_INTRO_CONFIG_SETTINGS_TREE, STR_INTRO_TOOLTIP_CONFIG_SETTINGS_TREE), SetPadding(0, 10, 0, 0), SetFill(1, 0),
	EndContainer(),

	NWidget(NWID_SPACER), SetMinimalSize(0, 6),

	/* 'script settings' and 'newgrf settings' buttons */
	NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
		NWidget(WWT_PUSHTXTBTN, COLOUR_ORANGE, WID_SGI_AI_SETTINGS), SetMinimalSize(158, 12),
							SetDataTip(STR_INTRO_SCRIPT_SETTINGS, STR_INTRO_TOOLTIP_SCRIPT_SETTINGS), SetPadding(0, 0, 0, 10), SetFill(1, 0),
		NWidget(WWT_PUSHTXTBTN, COLOUR_ORANGE, WID_SGI_GRF_SETTINGS), SetMinimalSize(158, 12),
							SetDataTip(STR_INTRO_NEWGRF_SETTINGS, STR_INTRO_TOOLTIP_NEWGRF_SETTINGS), SetPadding(0, 10, 0, 0), SetFill(1, 0),
	EndContainer(),

	NWidget(NWID_SPACER), SetMinimalSize(0, 6),

	/* 'online content' and 'highscore' buttons */
	NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
		NWidget(WWT_PUSHTXTBTN, COLOUR_ORANGE, WID_SGI_CONTENT_DOWNLOAD), SetMinimalSize(158, 12),
							SetDataTip(STR_INTRO_ONLINE_CONTENT, STR_INTRO_TOOLTIP_ONLINE_CONTENT), SetPadding(0, 0, 0, 10), SetFill(1, 0),
		NWidget(WWT_PUSHTXTBTN, COLOUR_ORANGE, WID_SGI_HELP), SetMinimalSize(158, 12),
							SetDataTip(STR_INTRO_HELP, STR_INTRO_TOOLTIP_HELP), SetPadding(0, 10, 0, 0), SetFill(1, 0),
	EndContainer(),

	NWidget(NWID_SPACER), SetMinimalSize(0, 6),

	/* 'exit program' button */
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_SPACER), SetFill(1, 0),
		NWidget(WWT_PUSHTXTBTN, COLOUR_ORANGE, WID_SGI_EXIT), SetMinimalSize(128, 12),
							SetDataTip(STR_INTRO_QUIT, STR_INTRO_TOOLTIP_QUIT),
		NWidget(NWID_SPACER), SetFill(1, 0),
	EndContainer(),

	NWidget(NWID_SPACER), SetMinimalSize(0, 8),

	EndContainer(),
};

static WindowDesc _select_game_desc(
	WDP_CENTER, nullptr, 0, 0,
	WC_SELECT_GAME, WC_NONE,
	0,
	_nested_select_game_widgets, lengthof(_nested_select_game_widgets)
);

void ShowSelectGameWindow()
{
	new SelectGameWindow(&_select_game_desc);
}

static void AskExitGameCallback(Window *w, bool confirmed)
{
	if (confirmed) _exit_game = true;
}

void AskExitGame()
{
	ShowQuery(
		STR_QUIT_CAPTION,
		STR_QUIT_ARE_YOU_SURE_YOU_WANT_TO_EXIT_OPENTTD,
		nullptr,
		AskExitGameCallback
	);
}


static void AskExitToGameMenuCallback(Window *w, bool confirmed)
{
	if (confirmed) {
		_switch_mode = SM_MENU;
		ClearErrorMessages();
	}
}

void AskExitToGameMenu()
{
	ShowQuery(
		STR_ABANDON_GAME_CAPTION,
		(_game_mode != GM_EDITOR) ? STR_ABANDON_GAME_QUERY : STR_ABANDON_SCENARIO_QUERY,
		nullptr,
		AskExitToGameMenuCallback
	);
}
