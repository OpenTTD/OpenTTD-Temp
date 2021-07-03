/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file genworld_gui.cpp GUI to configure and show progress during map generation. */

#include "stdafx.h"
#include "heightmap.h"
#include "debug.h"
#include "genworld.h"
#include "network/network.h"
#include "strings_func.h"
#include "window_func.h"
#include "date_func.h"
#include "sound_func.h"
#include "fios.h"
#include "string_func.h"
#include "widgets/dropdown_type.h"
#include "widgets/dropdown_func.h"
#include "querystring_gui.h"
#include "town.h"
#include "core/geometry_func.hpp"
#include "core/random_func.hpp"
#include "saveload/saveload.h"
#include "progress.h"
#include "error.h"
#include "newgrf_townname.h"
#include "townname_type.h"
#include "video/video_driver.hpp"

#include "widgets/genworld_widget.h"

#include "safeguards.h"


extern void MakeNewgameSettingsLive();

/** Enum for the modes we can generate in. */
enum GenerateLandscapeWindowMode {
	GLWM_GENERATE,  ///< Generate new game.
	GLWM_HEIGHTMAP, ///< Load from heightmap.
	GLWM_SCENARIO,  ///< Generate flat land.
};

/**
 * Get the map height limit, or if set to "auto", the absolute limit.
 */
static uint GetMapHeightLimit()
{
	if (_settings_newgame.construction.map_height_limit == 0) return MAX_MAP_HEIGHT_LIMIT;
	return _settings_newgame.construction.map_height_limit;
}

/**
 * Changes landscape type and sets genworld window dirty
 * @param landscape new landscape type
 */
void SetNewLandscapeType(byte landscape)
{
	_settings_newgame.game_creation.landscape = landscape;
	InvalidateWindowClassesData(WC_SELECT_GAME);
	InvalidateWindowClassesData(WC_GENERATE_LANDSCAPE);
	_switch_mode = SM_MENU;
}

/** Widgets of GenerateLandscapeWindow when generating world */
static const NWidgetPart _nested_generate_landscape_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_BROWN),
		NWidget(WWT_CAPTION, COLOUR_BROWN), SetDataTip(STR_MAPGEN_WORLD_GENERATION_CAPTION, STR_NULL),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_BROWN),
		NWidget(NWID_SPACER), SetMinimalSize(0, 10),
		/* Landscape selection. */
		NWidget(NWID_HORIZONTAL), SetPIP(10, 0, 10),
			NWidget(NWID_SPACER), SetFill(1, 0),
			NWidget(WWT_IMGBTN_2, COLOUR_ORANGE, WID_GL_TEMPERATE), SetDataTip(SPR_SELECT_TEMPERATE, STR_INTRO_TOOLTIP_TEMPERATE),
			NWidget(NWID_SPACER), SetFill(1, 0),
			NWidget(WWT_IMGBTN_2, COLOUR_ORANGE, WID_GL_ARCTIC), SetDataTip(SPR_SELECT_SUB_ARCTIC, STR_INTRO_TOOLTIP_SUB_ARCTIC_LANDSCAPE),
			NWidget(NWID_SPACER), SetFill(1, 0),
			NWidget(WWT_IMGBTN_2, COLOUR_ORANGE, WID_GL_TROPICAL), SetDataTip(SPR_SELECT_SUB_TROPICAL, STR_INTRO_TOOLTIP_SUB_TROPICAL_LANDSCAPE),
			NWidget(NWID_SPACER), SetFill(1, 0),
			NWidget(WWT_IMGBTN_2, COLOUR_ORANGE, WID_GL_TOYLAND), SetDataTip(SPR_SELECT_TOYLAND, STR_INTRO_TOOLTIP_TOYLAND_LANDSCAPE),
			NWidget(NWID_SPACER), SetFill(1, 0),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 11),
		NWidget(NWID_HORIZONTAL), SetPIP(10, 5, 10),
			NWidget(NWID_HORIZONTAL), SetPIP(0, 3, 0),
				/* Left column with labels. */
				NWidget(NWID_VERTICAL, NC_EQUALSIZE), SetPIP(0, 4, 0),
					NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_MAPGEN_MAPSIZE, STR_MAPGEN_MAPSIZE_TOOLTIP), SetFill(1, 1),
					NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_MAPGEN_LAND_GENERATOR, STR_NULL), SetFill(1, 1),
					NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_MAPGEN_TERRAIN_TYPE, STR_NULL), SetFill(1, 1),
					NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_MAPGEN_VARIETY, STR_NULL), SetFill(1, 1),
					NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_MAPGEN_QUANTITY_OF_SEA_LAKES, STR_NULL), SetFill(1, 1),
					NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_MAPGEN_NUMBER_OF_TOWNS, STR_NULL), SetFill(1, 1),
					NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_MAPGEN_NUMBER_OF_INDUSTRIES, STR_NULL), SetFill(1, 1),
					NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_MAPGEN_BORDER_TYPE, STR_NULL), SetFill(1, 1),
				EndContainer(),
				/* Widgets at the right of the labels. */
				NWidget(NWID_VERTICAL, NC_EQUALSIZE), SetPIP(0, 4, 0),
					/* Mapsize X * Y. */
					NWidget(NWID_HORIZONTAL), SetPIP(0, 4, 0),
						NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_GL_MAPSIZE_X_PULLDOWN), SetDataTip(STR_JUST_INT, STR_MAPGEN_MAPSIZE_TOOLTIP), SetFill(1, 0),
						NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_MAPGEN_BY, STR_NULL), SetPadding(1, 0, 0, 0), SetFill(1, 1),
						NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_GL_MAPSIZE_Y_PULLDOWN), SetDataTip(STR_JUST_INT, STR_MAPGEN_MAPSIZE_TOOLTIP), SetFill(1, 0),
					EndContainer(),
					NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_GL_LANDSCAPE_PULLDOWN), SetDataTip(STR_JUST_STRING, STR_NULL), SetFill(1, 0),
					NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_GL_TERRAIN_PULLDOWN), SetDataTip(STR_JUST_STRING, STR_NULL), SetFill(1, 0),
					NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_GL_VARIETY_PULLDOWN), SetDataTip(STR_JUST_STRING, STR_NULL), SetFill(1, 0),
					NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_GL_WATER_PULLDOWN), SetDataTip(STR_JUST_STRING, STR_NULL), SetFill(1, 0),
					NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_GL_TOWN_PULLDOWN), SetDataTip(STR_JUST_STRING, STR_NULL), SetFill(1, 0),
					NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_GL_INDUSTRY_PULLDOWN), SetDataTip(STR_JUST_STRING, STR_NULL), SetFill(1, 0),
					NWidget(WWT_TEXTBTN, COLOUR_ORANGE, WID_GL_BORDERS_RANDOM), SetDataTip(STR_JUST_STRING, STR_NULL), SetFill(1, 0),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_VERTICAL), SetPIP(0, 4, 0),
				NWidget(NWID_HORIZONTAL), SetPIP(0, 3, 0),
					NWidget(NWID_VERTICAL, NC_EQUALSIZE), SetPIP(0, 4, 0),
						NWidget(NWID_SELECTION, INVALID_COLOUR, WID_GL_CLIMATE_SEL_LABEL),
							NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_MAPGEN_SNOW_COVERAGE, STR_NULL), SetFill(1, 1),
							NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_MAPGEN_DESERT_COVERAGE, STR_NULL), SetFill(1, 1),
						EndContainer(),
						NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_MAPGEN_DATE, STR_NULL), SetFill(1, 1),
						NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_MAPGEN_SMOOTHNESS, STR_NULL), SetFill(1, 1),
						NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_MAPGEN_QUANTITY_OF_RIVERS, STR_NULL), SetFill(1, 1),
						NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_GAME_OPTIONS_TOWN_NAMES_FRAME, STR_NULL), SetFill(1, 1),
					EndContainer(),
					NWidget(NWID_VERTICAL, NC_EQUALSIZE), SetPIP(0, 4, 0),
						NWidget(NWID_SELECTION, INVALID_COLOUR, WID_GL_CLIMATE_SEL_SELECTOR),
							/* Snow coverage. */
							NWidget(NWID_HORIZONTAL),
								NWidget(WWT_IMGBTN, COLOUR_ORANGE, WID_GL_SNOW_COVERAGE_DOWN), SetDataTip(SPR_ARROW_DOWN, STR_MAPGEN_SNOW_COVERAGE_DOWN), SetFill(0, 1),
								NWidget(WWT_TEXTBTN, COLOUR_ORANGE, WID_GL_SNOW_COVERAGE_TEXT), SetDataTip(STR_MAPGEN_SNOW_COVERAGE_TEXT, STR_NULL), SetFill(1, 0),
								NWidget(WWT_IMGBTN, COLOUR_ORANGE, WID_GL_SNOW_COVERAGE_UP), SetDataTip(SPR_ARROW_UP, STR_MAPGEN_SNOW_COVERAGE_UP), SetFill(0, 1),
							EndContainer(),
							/* Desert coverage. */
							NWidget(NWID_HORIZONTAL),
								NWidget(WWT_IMGBTN, COLOUR_ORANGE, WID_GL_DESERT_COVERAGE_DOWN), SetDataTip(SPR_ARROW_DOWN, STR_MAPGEN_DESERT_COVERAGE_DOWN), SetFill(0, 1),
								NWidget(WWT_TEXTBTN, COLOUR_ORANGE, WID_GL_DESERT_COVERAGE_TEXT), SetDataTip(STR_MAPGEN_DESERT_COVERAGE_TEXT, STR_NULL), SetFill(1, 0),
								NWidget(WWT_IMGBTN, COLOUR_ORANGE, WID_GL_DESERT_COVERAGE_UP), SetDataTip(SPR_ARROW_UP, STR_MAPGEN_DESERT_COVERAGE_UP), SetFill(0, 1),
							EndContainer(),
						EndContainer(),
						/* Starting date. */
						NWidget(NWID_HORIZONTAL),
							NWidget(WWT_IMGBTN, COLOUR_ORANGE, WID_GL_START_DATE_DOWN), SetDataTip(SPR_ARROW_DOWN, STR_SCENEDIT_TOOLBAR_TOOLTIP_MOVE_THE_STARTING_DATE_BACKWARD), SetFill(0, 1),
							NWidget(WWT_PUSHTXTBTN, COLOUR_ORANGE, WID_GL_START_DATE_TEXT), SetDataTip(STR_BLACK_DATE_LONG, STR_NULL), SetFill(1, 0),
							NWidget(WWT_IMGBTN, COLOUR_ORANGE, WID_GL_START_DATE_UP), SetDataTip(SPR_ARROW_UP, STR_SCENEDIT_TOOLBAR_TOOLTIP_MOVE_THE_STARTING_DATE_FORWARD), SetFill(0, 1),
						EndContainer(),
						NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_GL_SMOOTHNESS_PULLDOWN), SetDataTip(STR_JUST_STRING, STR_NULL), SetFill(1, 0),
						NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_GL_RIVER_PULLDOWN), SetDataTip(STR_JUST_STRING, STR_NULL), SetFill(1, 0),
						NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_GL_TOWNNAME_DROPDOWN), SetDataTip(STR_BLACK_STRING, STR_GAME_OPTIONS_TOWN_NAMES_DROPDOWN_TOOLTIP), SetFill(1, 0),
					EndContainer(),
				EndContainer(),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREEN, WID_GL_GENERATE_BUTTON), SetMinimalSize(84, 0), SetDataTip(STR_MAPGEN_GENERATE, STR_NULL), SetFill(1, 1),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 4),
		/* Map borders buttons for each edge. */
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(10, 0, 10),
			NWidget(NWID_HORIZONTAL), SetPIP(0, 0, 3),
				NWidget(NWID_SPACER), SetFill(1, 1),
				NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_MAPGEN_NORTHWEST, STR_NULL), SetPadding(1, 0, 0, 0), SetFill(0, 1),
			EndContainer(),
			NWidget(WWT_TEXTBTN, COLOUR_ORANGE, WID_GL_WATER_NW), SetDataTip(STR_JUST_STRING, STR_MAPGEN_NORTHWEST), SetFill(1, 1),
			NWidget(WWT_TEXTBTN, COLOUR_ORANGE, WID_GL_WATER_NE), SetDataTip(STR_JUST_STRING, STR_MAPGEN_NORTHEAST), SetFill(1, 1),
			NWidget(NWID_HORIZONTAL), SetPIP(3, 0, 0),
				NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_MAPGEN_NORTHEAST, STR_NULL), SetPadding(1, 0, 0, 0), SetFill(0, 1),
				NWidget(NWID_SPACER), SetFill(1, 1),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(10, 0, 10),
			NWidget(NWID_HORIZONTAL), SetPIP(0, 0, 3),
				NWidget(NWID_SPACER), SetFill(1, 1),
				NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_MAPGEN_SOUTHWEST, STR_NULL), SetPadding(1, 0, 0, 0), SetFill(0, 1),
			EndContainer(),
			NWidget(WWT_TEXTBTN, COLOUR_ORANGE, WID_GL_WATER_SW), SetDataTip(STR_JUST_STRING, STR_MAPGEN_SOUTHWEST), SetFill(1, 1),
			NWidget(WWT_TEXTBTN, COLOUR_ORANGE, WID_GL_WATER_SE), SetDataTip(STR_JUST_STRING, STR_MAPGEN_SOUTHEAST), SetFill(1, 1),
			NWidget(NWID_HORIZONTAL), SetPIP(3, 0, 0),
				NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_MAPGEN_SOUTHEAST, STR_NULL), SetPadding(1, 0, 0, 0), SetFill(0, 1),
				NWidget(NWID_SPACER), SetFill(1, 1),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 9), SetFill(1, 1),
	EndContainer(),
};

/** Widgets of GenerateLandscapeWindow when loading heightmap */
static const NWidgetPart _nested_heightmap_load_widgets[] = {
	/* Window header. */
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_BROWN),
		NWidget(WWT_CAPTION, COLOUR_BROWN), SetDataTip(STR_MAPGEN_WORLD_GENERATION_CAPTION, STR_NULL),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_BROWN),
		NWidget(NWID_SPACER), SetMinimalSize(0, 10),
		/* Landscape selection. */
		NWidget(NWID_HORIZONTAL), SetPIP(10, 0, 10),
			NWidget(NWID_SPACER), SetFill(1, 0),
			NWidget(WWT_IMGBTN_2, COLOUR_ORANGE, WID_GL_TEMPERATE), SetDataTip(SPR_SELECT_TEMPERATE, STR_INTRO_TOOLTIP_TEMPERATE),
			NWidget(NWID_SPACER), SetFill(1, 0),
			NWidget(WWT_IMGBTN_2, COLOUR_ORANGE, WID_GL_ARCTIC), SetDataTip(SPR_SELECT_SUB_ARCTIC, STR_INTRO_TOOLTIP_SUB_ARCTIC_LANDSCAPE),
			NWidget(NWID_SPACER), SetFill(1, 0),
			NWidget(WWT_IMGBTN_2, COLOUR_ORANGE, WID_GL_TROPICAL), SetDataTip(SPR_SELECT_SUB_TROPICAL, STR_INTRO_TOOLTIP_SUB_TROPICAL_LANDSCAPE),
			NWidget(NWID_SPACER), SetFill(1, 0),
			NWidget(WWT_IMGBTN_2, COLOUR_ORANGE, WID_GL_TOYLAND), SetDataTip(SPR_SELECT_TOYLAND, STR_INTRO_TOOLTIP_TOYLAND_LANDSCAPE),
			NWidget(NWID_SPACER), SetFill(1, 0),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 11), SetFill(0, 1),
		NWidget(NWID_HORIZONTAL), SetPIP(10, 3, 10),
			/* Labels at the left side. */
			NWidget(NWID_VERTICAL, NC_EQUALSIZE), SetPIP(0, 4, 0),
				NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_MAPGEN_HEIGHTMAP_NAME, STR_NULL), SetFill(1, 1),
				NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_MAPGEN_HEIGHTMAP_SIZE_LABEL, STR_NULL), SetFill(1, 1),
				NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_MAPGEN_MAPSIZE, STR_NULL), SetFill(1, 1),
				NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_MAPGEN_HEIGHTMAP_ROTATION, STR_NULL), SetFill(1, 1),
				NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_MAPGEN_NUMBER_OF_TOWNS, STR_NULL), SetFill(1, 1),
				NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_MAPGEN_NUMBER_OF_INDUSTRIES, STR_NULL), SetFill(1, 1),
				NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_MAPGEN_QUANTITY_OF_RIVERS, STR_NULL), SetFill(1, 1),
			EndContainer(),
			/* Widgets at the right of the labels. */
			NWidget(NWID_VERTICAL), SetPIP(0, 4, 0),
				NWidget(WWT_EMPTY, COLOUR_ORANGE, WID_GL_HEIGHTMAP_NAME_TEXT), SetFill(1, 0),
				NWidget(NWID_HORIZONTAL), SetPIP(0, 5, 0),
					NWidget(NWID_VERTICAL, NC_EQUALSIZE), SetPIP(0, 4, 0),
						NWidget(WWT_TEXT, COLOUR_ORANGE, WID_GL_HEIGHTMAP_SIZE_TEXT), SetDataTip(STR_MAPGEN_HEIGHTMAP_SIZE, STR_NULL), SetFill(1, 0),
						/* Mapsize X * Y. */
						NWidget(NWID_HORIZONTAL), SetPIP(0, 4, 0),
							NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_GL_MAPSIZE_X_PULLDOWN), SetDataTip(STR_JUST_INT, STR_NULL), SetFill(1, 0),
							NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_MAPGEN_BY, STR_NULL), SetPadding(1, 0, 0, 0), SetFill(1, 1),
							NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_GL_MAPSIZE_Y_PULLDOWN), SetDataTip(STR_JUST_INT, STR_NULL), SetFill(1, 0),
						EndContainer(),
						NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_GL_HEIGHTMAP_ROTATION_PULLDOWN), SetDataTip(STR_JUST_STRING, STR_NULL), SetFill(1, 0),
						NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_GL_TOWN_PULLDOWN), SetDataTip(STR_JUST_STRING, STR_NULL), SetFill(1, 0),
						NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_GL_INDUSTRY_PULLDOWN), SetDataTip(STR_JUST_STRING, STR_NULL), SetFill(1, 0),
						NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_GL_RIVER_PULLDOWN), SetDataTip(STR_JUST_STRING, STR_NULL), SetFill(1, 0),
					EndContainer(),
					NWidget(NWID_VERTICAL), SetPIP(0, 4, 0),
						NWidget(NWID_HORIZONTAL), SetPIP(0, 3, 0),
							NWidget(NWID_VERTICAL, NC_EQUALSIZE), SetPIP(0, 4, 0),
								NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_MAPGEN_HEIGHTMAP_HEIGHT, STR_NULL), SetFill(1, 1),
								NWidget(NWID_SELECTION, INVALID_COLOUR, WID_GL_CLIMATE_SEL_LABEL),
									NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_MAPGEN_SNOW_COVERAGE, STR_NULL), SetFill(1, 1),
									NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_MAPGEN_DESERT_COVERAGE, STR_NULL), SetFill(1, 1),
								EndContainer(),
								NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_MAPGEN_DATE, STR_NULL), SetFill(1, 1),
								NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_GAME_OPTIONS_TOWN_NAMES_FRAME, STR_NULL), SetFill(1, 1),
							EndContainer(),
							NWidget(NWID_VERTICAL, NC_EQUALSIZE), SetPIP(0, 4, 0),
								NWidget(NWID_HORIZONTAL),
									NWidget(WWT_IMGBTN, COLOUR_ORANGE, WID_GL_HEIGHTMAP_HEIGHT_DOWN), SetDataTip(SPR_ARROW_DOWN, STR_MAPGEN_HEIGHTMAP_HEIGHT_DOWN), SetFill(0, 1),
									NWidget(WWT_TEXTBTN, COLOUR_ORANGE, WID_GL_HEIGHTMAP_HEIGHT_TEXT), SetDataTip(STR_BLACK_INT, STR_NULL), SetFill(1, 0),
									NWidget(WWT_IMGBTN, COLOUR_ORANGE, WID_GL_HEIGHTMAP_HEIGHT_UP), SetDataTip(SPR_ARROW_UP, STR_MAPGEN_HEIGHTMAP_HEIGHT_UP), SetFill(0, 1),
								EndContainer(),
								NWidget(NWID_SELECTION, INVALID_COLOUR, WID_GL_CLIMATE_SEL_SELECTOR),
									NWidget(NWID_HORIZONTAL),
										NWidget(WWT_IMGBTN, COLOUR_ORANGE, WID_GL_SNOW_COVERAGE_DOWN), SetDataTip(SPR_ARROW_DOWN, STR_MAPGEN_SNOW_COVERAGE_DOWN), SetFill(0, 1),
										NWidget(WWT_TEXTBTN, COLOUR_ORANGE, WID_GL_SNOW_COVERAGE_TEXT), SetDataTip(STR_MAPGEN_SNOW_COVERAGE_TEXT, STR_NULL), SetFill(1, 0),
										NWidget(WWT_IMGBTN, COLOUR_ORANGE, WID_GL_SNOW_COVERAGE_UP), SetDataTip(SPR_ARROW_UP, STR_MAPGEN_SNOW_COVERAGE_UP), SetFill(0, 1),
									EndContainer(),
									NWidget(NWID_HORIZONTAL),
										NWidget(WWT_IMGBTN, COLOUR_ORANGE, WID_GL_DESERT_COVERAGE_DOWN), SetDataTip(SPR_ARROW_DOWN, STR_MAPGEN_DESERT_COVERAGE_DOWN), SetFill(0, 1),
										NWidget(WWT_TEXTBTN, COLOUR_ORANGE, WID_GL_DESERT_COVERAGE_TEXT), SetDataTip(STR_MAPGEN_DESERT_COVERAGE_TEXT, STR_NULL), SetFill(1, 0),
										NWidget(WWT_IMGBTN, COLOUR_ORANGE, WID_GL_DESERT_COVERAGE_UP), SetDataTip(SPR_ARROW_UP, STR_MAPGEN_DESERT_COVERAGE_UP), SetFill(0, 1),
									EndContainer(),
								EndContainer(),
								NWidget(NWID_HORIZONTAL),
									NWidget(WWT_IMGBTN, COLOUR_ORANGE, WID_GL_START_DATE_DOWN), SetDataTip(SPR_ARROW_DOWN, STR_SCENEDIT_TOOLBAR_TOOLTIP_MOVE_THE_STARTING_DATE_BACKWARD), SetFill(0, 1),
									NWidget(WWT_PUSHTXTBTN, COLOUR_ORANGE, WID_GL_START_DATE_TEXT), SetDataTip(STR_BLACK_DATE_LONG, STR_NULL), SetFill(1, 0),
									NWidget(WWT_IMGBTN, COLOUR_ORANGE, WID_GL_START_DATE_UP), SetDataTip(SPR_ARROW_UP, STR_SCENEDIT_TOOLBAR_TOOLTIP_MOVE_THE_STARTING_DATE_FORWARD), SetFill(0, 1),
								EndContainer(),
								NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_GL_TOWNNAME_DROPDOWN), SetDataTip(STR_BLACK_STRING, STR_GAME_OPTIONS_TOWN_NAMES_DROPDOWN_TOOLTIP), SetFill(1, 0),
							EndContainer(),
						EndContainer(),
						NWidget(WWT_PUSHTXTBTN, COLOUR_GREEN, WID_GL_GENERATE_BUTTON), SetMinimalSize(84, 0), SetDataTip(STR_MAPGEN_GENERATE, STR_NULL), SetFill(1, 1),
					EndContainer(),
				EndContainer(),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 9), SetFill(1, 1),
	EndContainer(),
};

static void StartGeneratingLandscape(GenerateLandscapeWindowMode mode)
{
	CloseAllNonVitalWindows();
	ClearErrorMessages();

	/* Copy all XXX_newgame to XXX when coming from outside the editor */
	MakeNewgameSettingsLive();
	ResetGRFConfig(true);

	if (_settings_client.sound.confirm) SndPlayFx(SND_15_BEEP);
	switch (mode) {
		case GLWM_GENERATE:  _switch_mode = (_game_mode == GM_EDITOR) ? SM_GENRANDLAND    : SM_NEWGAME;         break;
		case GLWM_HEIGHTMAP: _switch_mode = (_game_mode == GM_EDITOR) ? SM_LOAD_HEIGHTMAP : SM_START_HEIGHTMAP; break;
		case GLWM_SCENARIO:  _switch_mode = SM_EDITOR; break;
		default: NOT_REACHED();
	}
}

static void LandscapeGenerationCallback(Window *w, bool confirmed)
{
	if (confirmed) StartGeneratingLandscape((GenerateLandscapeWindowMode)w->window_number);
}

static DropDownList BuildMapsizeDropDown()
{
	DropDownList list;

	for (uint i = MIN_MAP_SIZE_BITS; i <= MAX_MAP_SIZE_BITS; i++) {
		DropDownListParamStringItem *item = new DropDownListParamStringItem(STR_JUST_INT, i, false);
		item->SetParam(0, 1LL << i);
		list.emplace_back(item);
	}

	return list;
}

static DropDownList BuildTownNameDropDown()
{
	DropDownList list;

	/* Add and sort newgrf townnames generators */
	const auto &grf_names = GetGRFTownNameList();
	for (uint i = 0; i < grf_names.size(); i++) {
		list.emplace_back(new DropDownListStringItem(grf_names[i], BUILTIN_TOWNNAME_GENERATOR_COUNT + i, false));
	}
	std::sort(list.begin(), list.end(), DropDownListStringItem::NatSortFunc);

	size_t newgrf_size = list.size();
	/* Insert newgrf_names at the top of the list */
	if (newgrf_size > 0) {
		list.emplace_back(new DropDownListItem(-1, false)); // separator line
		newgrf_size++;
	}

	/* Add and sort original townnames generators */
	for (uint i = 0; i < BUILTIN_TOWNNAME_GENERATOR_COUNT; i++) {
		list.emplace_back(new DropDownListStringItem(STR_GAME_OPTIONS_TOWN_NAME_ORIGINAL_ENGLISH + i, i, false));
	}
	std::sort(list.begin() + newgrf_size, list.end(), DropDownListStringItem::NatSortFunc);

	return list;
}


static const StringID _elevations[]  = {STR_TERRAIN_TYPE_VERY_FLAT, STR_TERRAIN_TYPE_FLAT, STR_TERRAIN_TYPE_HILLY, STR_TERRAIN_TYPE_MOUNTAINOUS, STR_TERRAIN_TYPE_ALPINIST, STR_TERRAIN_TYPE_CUSTOM, INVALID_STRING_ID};
static const StringID _sea_lakes[]   = {STR_SEA_LEVEL_VERY_LOW, STR_SEA_LEVEL_LOW, STR_SEA_LEVEL_MEDIUM, STR_SEA_LEVEL_HIGH, STR_SEA_LEVEL_CUSTOM, INVALID_STRING_ID};
static const StringID _rivers[]      = {STR_RIVERS_NONE, STR_RIVERS_FEW, STR_RIVERS_MODERATE, STR_RIVERS_LOT, INVALID_STRING_ID};
static const StringID _smoothness[]  = {STR_CONFIG_SETTING_ROUGHNESS_OF_TERRAIN_VERY_SMOOTH, STR_CONFIG_SETTING_ROUGHNESS_OF_TERRAIN_SMOOTH, STR_CONFIG_SETTING_ROUGHNESS_OF_TERRAIN_ROUGH, STR_CONFIG_SETTING_ROUGHNESS_OF_TERRAIN_VERY_ROUGH, INVALID_STRING_ID};
static const StringID _rotation[]    = {STR_CONFIG_SETTING_HEIGHTMAP_ROTATION_COUNTER_CLOCKWISE, STR_CONFIG_SETTING_HEIGHTMAP_ROTATION_CLOCKWISE, INVALID_STRING_ID};
static const StringID _landscape[]   = {STR_CONFIG_SETTING_LAND_GENERATOR_ORIGINAL, STR_CONFIG_SETTING_LAND_GENERATOR_TERRA_GENESIS, INVALID_STRING_ID};
static const StringID _num_towns[]   = {STR_NUM_VERY_LOW, STR_NUM_LOW, STR_NUM_NORMAL, STR_NUM_HIGH, STR_NUM_CUSTOM, INVALID_STRING_ID};
static const StringID _num_inds[]    = {STR_FUNDING_ONLY, STR_MINIMAL, STR_NUM_VERY_LOW, STR_NUM_LOW, STR_NUM_NORMAL, STR_NUM_HIGH, INVALID_STRING_ID};
static const StringID _variety[]     = {STR_VARIETY_NONE, STR_VARIETY_VERY_LOW, STR_VARIETY_LOW, STR_VARIETY_MEDIUM, STR_VARIETY_HIGH, STR_VARIETY_VERY_HIGH, INVALID_STRING_ID};

static_assert(lengthof(_num_inds) == ID_END + 1);

struct GenerateLandscapeWindow : public Window {
	uint widget_id;
	uint x;
	uint y;
	char name[64];
	GenerateLandscapeWindowMode mode;

	GenerateLandscapeWindow(WindowDesc *desc, WindowNumber number = 0) : Window(desc)
	{
		this->InitNested(number);

		this->LowerWidget(_settings_newgame.game_creation.landscape + WID_GL_TEMPERATE);

		this->mode = (GenerateLandscapeWindowMode)this->window_number;

		/* Disable town and industry in SE */
		this->SetWidgetDisabledState(WID_GL_TOWN_PULLDOWN,     _game_mode == GM_EDITOR);
		this->SetWidgetDisabledState(WID_GL_INDUSTRY_PULLDOWN, _game_mode == GM_EDITOR);

		/* In case the map_height_limit is changed, clamp heightmap_height and custom_terrain_type. */
		_settings_newgame.game_creation.heightmap_height = Clamp(_settings_newgame.game_creation.heightmap_height, MIN_HEIGHTMAP_HEIGHT, GetMapHeightLimit());
		_settings_newgame.game_creation.custom_terrain_type = Clamp(_settings_newgame.game_creation.custom_terrain_type, MIN_CUSTOM_TERRAIN_TYPE, GetMapHeightLimit());

		this->OnInvalidateData();
	}


	void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_GL_START_DATE_TEXT:      SetDParam(0, ConvertYMDToDate(_settings_newgame.game_creation.starting_year, 0, 1)); break;
			case WID_GL_MAPSIZE_X_PULLDOWN:   SetDParam(0, 1LL << _settings_newgame.game_creation.map_x); break;
			case WID_GL_MAPSIZE_Y_PULLDOWN:   SetDParam(0, 1LL << _settings_newgame.game_creation.map_y); break;
			case WID_GL_HEIGHTMAP_HEIGHT_TEXT: SetDParam(0, _settings_newgame.game_creation.heightmap_height); break;
			case WID_GL_SNOW_COVERAGE_TEXT:   SetDParam(0, _settings_newgame.game_creation.snow_coverage); break;
			case WID_GL_DESERT_COVERAGE_TEXT: SetDParam(0, _settings_newgame.game_creation.desert_coverage); break;

			case WID_GL_TOWN_PULLDOWN:
				if (_game_mode == GM_EDITOR) {
					SetDParam(0, STR_CONFIG_SETTING_OFF);
				} else if (_settings_newgame.difficulty.number_towns == CUSTOM_TOWN_NUMBER_DIFFICULTY) {
					SetDParam(0, STR_NUM_CUSTOM_NUMBER);
					SetDParam(1, _settings_newgame.game_creation.custom_town_number);
				} else {
					SetDParam(0, _num_towns[_settings_newgame.difficulty.number_towns]);
				}
				break;

			case WID_GL_TOWNNAME_DROPDOWN: {
				uint gen = _settings_newgame.game_creation.town_name;
				StringID name = gen < BUILTIN_TOWNNAME_GENERATOR_COUNT ?
						STR_GAME_OPTIONS_TOWN_NAME_ORIGINAL_ENGLISH + gen :
						GetGRFTownNameName(gen - BUILTIN_TOWNNAME_GENERATOR_COUNT);
				SetDParam(0, name);
				break;
			}

			case WID_GL_INDUSTRY_PULLDOWN:   SetDParam(0, _game_mode == GM_EDITOR ? STR_CONFIG_SETTING_OFF : _num_inds[_settings_newgame.difficulty.industry_density]); break;
			case WID_GL_LANDSCAPE_PULLDOWN:  SetDParam(0, _landscape[_settings_newgame.game_creation.land_generator]); break;
			case WID_GL_TERRAIN_PULLDOWN:
				if (_settings_newgame.difficulty.terrain_type == CUSTOM_TERRAIN_TYPE_NUMBER_DIFFICULTY) {
					SetDParam(0, STR_TERRAIN_TYPE_CUSTOM_VALUE);
					SetDParam(1, _settings_newgame.game_creation.custom_terrain_type);
				} else {
					SetDParam(0, _elevations[_settings_newgame.difficulty.terrain_type]); break;
				}
				break;

			case WID_GL_WATER_PULLDOWN:
				if (_settings_newgame.difficulty.quantity_sea_lakes == CUSTOM_SEA_LEVEL_NUMBER_DIFFICULTY) {
					SetDParam(0, STR_SEA_LEVEL_CUSTOM_PERCENTAGE);
					SetDParam(1, _settings_newgame.game_creation.custom_sea_level);
				} else {
					SetDParam(0, _sea_lakes[_settings_newgame.difficulty.quantity_sea_lakes]);
				}
				break;

			case WID_GL_RIVER_PULLDOWN:      SetDParam(0, _rivers[_settings_newgame.game_creation.amount_of_rivers]); break;
			case WID_GL_SMOOTHNESS_PULLDOWN: SetDParam(0, _smoothness[_settings_newgame.game_creation.tgen_smoothness]); break;
			case WID_GL_VARIETY_PULLDOWN:    SetDParam(0, _variety[_settings_newgame.game_creation.variety]); break;
			case WID_GL_BORDERS_RANDOM:      SetDParam(0, (_settings_newgame.game_creation.water_borders == BORDERS_RANDOM) ? STR_MAPGEN_BORDER_RANDOMIZE : STR_MAPGEN_BORDER_MANUAL); break;
			case WID_GL_WATER_NE: SetDParam(0, (_settings_newgame.game_creation.water_borders == BORDERS_RANDOM) ? STR_MAPGEN_BORDER_RANDOM : HasBit(_settings_newgame.game_creation.water_borders, BORDER_NE) ? STR_MAPGEN_BORDER_WATER : STR_MAPGEN_BORDER_FREEFORM); break;
			case WID_GL_WATER_NW: SetDParam(0, (_settings_newgame.game_creation.water_borders == BORDERS_RANDOM) ? STR_MAPGEN_BORDER_RANDOM : HasBit(_settings_newgame.game_creation.water_borders, BORDER_NW) ? STR_MAPGEN_BORDER_WATER : STR_MAPGEN_BORDER_FREEFORM); break;
			case WID_GL_WATER_SE: SetDParam(0, (_settings_newgame.game_creation.water_borders == BORDERS_RANDOM) ? STR_MAPGEN_BORDER_RANDOM : HasBit(_settings_newgame.game_creation.water_borders, BORDER_SE) ? STR_MAPGEN_BORDER_WATER : STR_MAPGEN_BORDER_FREEFORM); break;
			case WID_GL_WATER_SW: SetDParam(0, (_settings_newgame.game_creation.water_borders == BORDERS_RANDOM) ? STR_MAPGEN_BORDER_RANDOM : HasBit(_settings_newgame.game_creation.water_borders, BORDER_SW) ? STR_MAPGEN_BORDER_WATER : STR_MAPGEN_BORDER_FREEFORM); break;
			case WID_GL_HEIGHTMAP_ROTATION_PULLDOWN: SetDParam(0, _rotation[_settings_newgame.game_creation.heightmap_rotation]); break;

			case WID_GL_HEIGHTMAP_SIZE_TEXT:
				if (_settings_newgame.game_creation.heightmap_rotation == HM_CLOCKWISE) {
					SetDParam(0, this->y);
					SetDParam(1, this->x);
				} else {
					SetDParam(0, this->x);
					SetDParam(1, this->y);
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
		if (!gui_scope) return;
		/* Update the climate buttons */
		this->SetWidgetLoweredState(WID_GL_TEMPERATE, _settings_newgame.game_creation.landscape == LT_TEMPERATE);
		this->SetWidgetLoweredState(WID_GL_ARCTIC,    _settings_newgame.game_creation.landscape == LT_ARCTIC);
		this->SetWidgetLoweredState(WID_GL_TROPICAL,  _settings_newgame.game_creation.landscape == LT_TROPIC);
		this->SetWidgetLoweredState(WID_GL_TOYLAND,   _settings_newgame.game_creation.landscape == LT_TOYLAND);

		/* You can't select smoothness / non-water borders if not terragenesis */
		if (mode == GLWM_GENERATE) {
			this->SetWidgetDisabledState(WID_GL_SMOOTHNESS_PULLDOWN, _settings_newgame.game_creation.land_generator == LG_ORIGINAL);
			this->SetWidgetDisabledState(WID_GL_VARIETY_PULLDOWN, _settings_newgame.game_creation.land_generator == LG_ORIGINAL);
			this->SetWidgetDisabledState(WID_GL_BORDERS_RANDOM, _settings_newgame.game_creation.land_generator == LG_ORIGINAL || !_settings_newgame.construction.freeform_edges);
			this->SetWidgetsDisabledState(_settings_newgame.game_creation.land_generator == LG_ORIGINAL || !_settings_newgame.construction.freeform_edges || _settings_newgame.game_creation.water_borders == BORDERS_RANDOM,
					WID_GL_WATER_NW, WID_GL_WATER_NE, WID_GL_WATER_SE, WID_GL_WATER_SW, WIDGET_LIST_END);

			this->SetWidgetLoweredState(WID_GL_BORDERS_RANDOM, _settings_newgame.game_creation.water_borders == BORDERS_RANDOM);

			this->SetWidgetLoweredState(WID_GL_WATER_NW, HasBit(_settings_newgame.game_creation.water_borders, BORDER_NW));
			this->SetWidgetLoweredState(WID_GL_WATER_NE, HasBit(_settings_newgame.game_creation.water_borders, BORDER_NE));
			this->SetWidgetLoweredState(WID_GL_WATER_SE, HasBit(_settings_newgame.game_creation.water_borders, BORDER_SE));
			this->SetWidgetLoweredState(WID_GL_WATER_SW, HasBit(_settings_newgame.game_creation.water_borders, BORDER_SW));

			this->SetWidgetsDisabledState(_settings_newgame.game_creation.land_generator == LG_ORIGINAL && (_settings_newgame.game_creation.landscape == LT_ARCTIC || _settings_newgame.game_creation.landscape == LT_TROPIC),
					WID_GL_TERRAIN_PULLDOWN, WID_GL_WATER_PULLDOWN, WIDGET_LIST_END);
		}

		/* Disable snowline if not arctic */
		this->SetWidgetDisabledState(WID_GL_SNOW_COVERAGE_TEXT, _settings_newgame.game_creation.landscape != LT_ARCTIC);
		/* Disable desert if not tropic */
		this->SetWidgetDisabledState(WID_GL_DESERT_COVERAGE_TEXT, _settings_newgame.game_creation.landscape != LT_TROPIC);

		/* Set snow/rainforest selections */
		int climate_plane = 0;
		switch (_settings_newgame.game_creation.landscape) {
			case LT_TEMPERATE: climate_plane = SZSP_VERTICAL; break;
			case LT_ARCTIC:    climate_plane = 0;             break;
			case LT_TROPIC:    climate_plane = 1;             break;
			case LT_TOYLAND:   climate_plane = SZSP_VERTICAL; break;
		}
		this->GetWidget<NWidgetStacked>(WID_GL_CLIMATE_SEL_LABEL)->SetDisplayedPlane(climate_plane);
		this->GetWidget<NWidgetStacked>(WID_GL_CLIMATE_SEL_SELECTOR)->SetDisplayedPlane(climate_plane);

		/* Update availability of decreasing / increasing start date and snow level */
		if (mode == GLWM_HEIGHTMAP) {
			this->SetWidgetDisabledState(WID_GL_HEIGHTMAP_HEIGHT_DOWN, _settings_newgame.game_creation.heightmap_height <= MIN_HEIGHTMAP_HEIGHT);
			this->SetWidgetDisabledState(WID_GL_HEIGHTMAP_HEIGHT_UP, _settings_newgame.game_creation.heightmap_height >= GetMapHeightLimit());
		}
		this->SetWidgetDisabledState(WID_GL_START_DATE_DOWN, _settings_newgame.game_creation.starting_year <= MIN_YEAR);
		this->SetWidgetDisabledState(WID_GL_START_DATE_UP,   _settings_newgame.game_creation.starting_year >= MAX_YEAR);
		this->SetWidgetDisabledState(WID_GL_SNOW_COVERAGE_DOWN, _settings_newgame.game_creation.snow_coverage <= 0 || _settings_newgame.game_creation.landscape != LT_ARCTIC);
		this->SetWidgetDisabledState(WID_GL_SNOW_COVERAGE_UP,   _settings_newgame.game_creation.snow_coverage >= 100 || _settings_newgame.game_creation.landscape != LT_ARCTIC);
		this->SetWidgetDisabledState(WID_GL_DESERT_COVERAGE_DOWN, _settings_newgame.game_creation.desert_coverage <= 0 || _settings_newgame.game_creation.landscape != LT_TROPIC);
		this->SetWidgetDisabledState(WID_GL_DESERT_COVERAGE_UP,   _settings_newgame.game_creation.desert_coverage >= 100 || _settings_newgame.game_creation.landscape != LT_TROPIC);

		/* Do not allow a custom sea level or terrain type with the original land generator. */
		if (_settings_newgame.game_creation.land_generator == LG_ORIGINAL) {
			if (_settings_newgame.difficulty.quantity_sea_lakes == CUSTOM_SEA_LEVEL_NUMBER_DIFFICULTY) {
				_settings_newgame.difficulty.quantity_sea_lakes = 1;
			}
			if (_settings_newgame.difficulty.terrain_type == CUSTOM_TERRAIN_TYPE_NUMBER_DIFFICULTY) {
				_settings_newgame.difficulty.terrain_type = 1;
			}
		}

	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		const StringID *strs = nullptr;
		switch (widget) {
			case WID_GL_HEIGHTMAP_HEIGHT_TEXT:
				SetDParam(0, MAX_TILE_HEIGHT);
				*size = GetStringBoundingBox(STR_JUST_INT);
				break;

			case WID_GL_START_DATE_TEXT:
				SetDParam(0, ConvertYMDToDate(MAX_YEAR, 0, 1));
				*size = maxdim(*size, GetStringBoundingBox(STR_BLACK_DATE_LONG));
				break;

			case WID_GL_MAPSIZE_X_PULLDOWN:
			case WID_GL_MAPSIZE_Y_PULLDOWN:
				SetDParamMaxValue(0, MAX_MAP_SIZE);
				*size = maxdim(*size, GetStringBoundingBox(STR_JUST_INT));
				break;

			case WID_GL_SNOW_COVERAGE_TEXT:
				SetDParamMaxValue(0, MAX_TILE_HEIGHT);
				*size = maxdim(*size, GetStringBoundingBox(STR_MAPGEN_SNOW_COVERAGE_TEXT));
				break;

			case WID_GL_DESERT_COVERAGE_TEXT:
				SetDParamMaxValue(0, MAX_TILE_HEIGHT);
				*size = maxdim(*size, GetStringBoundingBox(STR_MAPGEN_DESERT_COVERAGE_TEXT));
				break;

			case WID_GL_HEIGHTMAP_SIZE_TEXT:
				SetDParam(0, this->x);
				SetDParam(1, this->y);
				*size = maxdim(*size, GetStringBoundingBox(STR_MAPGEN_HEIGHTMAP_SIZE));
				break;

			case WID_GL_TOWN_PULLDOWN:
				strs = _num_towns;
				SetDParamMaxValue(0, CUSTOM_TOWN_MAX_NUMBER);
				*size = maxdim(*size, GetStringBoundingBox(STR_NUM_CUSTOM_NUMBER));
				break;

			case WID_GL_INDUSTRY_PULLDOWN:   strs = _num_inds; break;
			case WID_GL_LANDSCAPE_PULLDOWN:  strs = _landscape; break;

			case WID_GL_TERRAIN_PULLDOWN:
				strs = _elevations;
				SetDParamMaxValue(0, MAX_MAP_HEIGHT_LIMIT);
				*size = maxdim(*size, GetStringBoundingBox(STR_TERRAIN_TYPE_CUSTOM_VALUE));
				break;

			case WID_GL_WATER_PULLDOWN:
				strs = _sea_lakes;
				SetDParamMaxValue(0, CUSTOM_SEA_LEVEL_MAX_PERCENTAGE);
				*size = maxdim(*size, GetStringBoundingBox(STR_SEA_LEVEL_CUSTOM_PERCENTAGE));
				break;

			case WID_GL_RIVER_PULLDOWN:      strs = _rivers; break;
			case WID_GL_SMOOTHNESS_PULLDOWN: strs = _smoothness; break;
			case WID_GL_VARIETY_PULLDOWN:    strs = _variety; break;
			case WID_GL_HEIGHTMAP_ROTATION_PULLDOWN: strs = _rotation; break;
			case WID_GL_BORDERS_RANDOM:
				*size = maxdim(GetStringBoundingBox(STR_MAPGEN_BORDER_RANDOMIZE), GetStringBoundingBox(STR_MAPGEN_BORDER_MANUAL));
				break;

			case WID_GL_WATER_NE:
			case WID_GL_WATER_NW:
			case WID_GL_WATER_SE:
			case WID_GL_WATER_SW:
				*size = maxdim(GetStringBoundingBox(STR_MAPGEN_BORDER_RANDOM), maxdim(GetStringBoundingBox(STR_MAPGEN_BORDER_WATER), GetStringBoundingBox(STR_MAPGEN_BORDER_FREEFORM)));
				break;

			case WID_GL_HEIGHTMAP_NAME_TEXT:
				size->width = 0;
				break;

			default:
				return;
		}
		if (strs != nullptr) {
			while (*strs != INVALID_STRING_ID) {
				*size = maxdim(*size, GetStringBoundingBox(*strs++));
			}
		}
		size->width += padding.width;
		size->height = std::max(size->height, (uint)(FONT_HEIGHT_NORMAL + WD_DROPDOWNTEXT_TOP + WD_DROPDOWNTEXT_BOTTOM));
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_GL_HEIGHTMAP_NAME_TEXT: {
				DrawString(r.left, r.right, r.top, this->name, TC_ORANGE);
				break;
			}
		}
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_GL_TEMPERATE:
			case WID_GL_ARCTIC:
			case WID_GL_TROPICAL:
			case WID_GL_TOYLAND:
				SetNewLandscapeType(widget - WID_GL_TEMPERATE);
				break;

			case WID_GL_MAPSIZE_X_PULLDOWN: // Mapsize X
				ShowDropDownList(this, BuildMapsizeDropDown(), _settings_newgame.game_creation.map_x, WID_GL_MAPSIZE_X_PULLDOWN);
				break;

			case WID_GL_MAPSIZE_Y_PULLDOWN: // Mapsize Y
				ShowDropDownList(this, BuildMapsizeDropDown(), _settings_newgame.game_creation.map_y, WID_GL_MAPSIZE_Y_PULLDOWN);
				break;

			case WID_GL_TOWN_PULLDOWN: // Number of towns
				ShowDropDownMenu(this, _num_towns, _settings_newgame.difficulty.number_towns, WID_GL_TOWN_PULLDOWN, 0, 0);
				break;

			case WID_GL_TOWNNAME_DROPDOWN: // Townname generator
				ShowDropDownList(this, BuildTownNameDropDown(), _settings_newgame.game_creation.town_name, WID_GL_TOWNNAME_DROPDOWN);
				break;

			case WID_GL_INDUSTRY_PULLDOWN: // Number of industries
				ShowDropDownMenu(this, _num_inds, _settings_newgame.difficulty.industry_density, WID_GL_INDUSTRY_PULLDOWN, 0, 0);
				break;

			case WID_GL_GENERATE_BUTTON: { // Generate
				/* Get rotated map size. */
				uint map_x;
				uint map_y;
				if (_settings_newgame.game_creation.heightmap_rotation == HM_CLOCKWISE) {
					map_x = this->y;
					map_y = this->x;
				} else {
					map_x = this->x;
					map_y = this->y;
				}
				if (mode == GLWM_HEIGHTMAP &&
						(map_x * 2 < (1U << _settings_newgame.game_creation.map_x) ||
						map_x / 2 > (1U << _settings_newgame.game_creation.map_x) ||
						map_y * 2 < (1U << _settings_newgame.game_creation.map_y) ||
						map_y / 2 > (1U << _settings_newgame.game_creation.map_y))) {
					ShowQuery(
						STR_WARNING_HEIGHTMAP_SCALE_CAPTION,
						STR_WARNING_HEIGHTMAP_SCALE_MESSAGE,
						this,
						LandscapeGenerationCallback);
				} else {
					StartGeneratingLandscape(mode);
				}
				break;
			}

			case WID_GL_HEIGHTMAP_HEIGHT_DOWN:
			case WID_GL_HEIGHTMAP_HEIGHT_UP: // Height level buttons
				/* Don't allow too fast scrolling */
				if (!(this->flags & WF_TIMEOUT) || this->timeout_timer <= 1) {
					this->HandleButtonClick(widget);

					_settings_newgame.game_creation.heightmap_height = Clamp(_settings_newgame.game_creation.heightmap_height + widget - WID_GL_HEIGHTMAP_HEIGHT_TEXT, MIN_HEIGHTMAP_HEIGHT, GetMapHeightLimit());
					this->InvalidateData();
				}
				_left_button_clicked = false;
				break;

			case WID_GL_HEIGHTMAP_HEIGHT_TEXT: // Height level text
				this->widget_id = WID_GL_HEIGHTMAP_HEIGHT_TEXT;
				SetDParam(0, _settings_newgame.game_creation.heightmap_height);
				ShowQueryString(STR_JUST_INT, STR_MAPGEN_HEIGHTMAP_HEIGHT_QUERY_CAPT, 4, this, CS_NUMERAL, QSF_ENABLE_DEFAULT);
				break;


			case WID_GL_START_DATE_DOWN:
			case WID_GL_START_DATE_UP: // Year buttons
				/* Don't allow too fast scrolling */
				if (!(this->flags & WF_TIMEOUT) || this->timeout_timer <= 1) {
					this->HandleButtonClick(widget);

					_settings_newgame.game_creation.starting_year = Clamp(_settings_newgame.game_creation.starting_year + widget - WID_GL_START_DATE_TEXT, MIN_YEAR, MAX_YEAR);
					this->InvalidateData();
				}
				_left_button_clicked = false;
				break;

			case WID_GL_START_DATE_TEXT: // Year text
				this->widget_id = WID_GL_START_DATE_TEXT;
				SetDParam(0, _settings_newgame.game_creation.starting_year);
				ShowQueryString(STR_JUST_INT, STR_MAPGEN_START_DATE_QUERY_CAPT, 8, this, CS_NUMERAL, QSF_ENABLE_DEFAULT);
				break;

			case WID_GL_SNOW_COVERAGE_DOWN:
			case WID_GL_SNOW_COVERAGE_UP: // Snow coverage buttons
				/* Don't allow too fast scrolling */
				if (!(this->flags & WF_TIMEOUT) || this->timeout_timer <= 1) {
					this->HandleButtonClick(widget);

					_settings_newgame.game_creation.snow_coverage = Clamp(_settings_newgame.game_creation.snow_coverage + (widget - WID_GL_SNOW_COVERAGE_TEXT) * 10, 0, 100);
					this->InvalidateData();
				}
				_left_button_clicked = false;
				break;

			case WID_GL_SNOW_COVERAGE_TEXT: // Snow coverage text
				this->widget_id = WID_GL_SNOW_COVERAGE_TEXT;
				SetDParam(0, _settings_newgame.game_creation.snow_coverage);
				ShowQueryString(STR_JUST_INT, STR_MAPGEN_SNOW_COVERAGE_QUERY_CAPT, 4, this, CS_NUMERAL, QSF_ENABLE_DEFAULT);
				break;

			case WID_GL_DESERT_COVERAGE_DOWN:
			case WID_GL_DESERT_COVERAGE_UP: // Desert coverage buttons
				/* Don't allow too fast scrolling */
				if (!(this->flags & WF_TIMEOUT) || this->timeout_timer <= 1) {
					this->HandleButtonClick(widget);

					_settings_newgame.game_creation.desert_coverage = Clamp(_settings_newgame.game_creation.desert_coverage + (widget - WID_GL_DESERT_COVERAGE_TEXT) * 10, 0, 100);
					this->InvalidateData();
				}
				_left_button_clicked = false;
				break;

			case WID_GL_DESERT_COVERAGE_TEXT: // Desert line text
				this->widget_id = WID_GL_DESERT_COVERAGE_TEXT;
				SetDParam(0, _settings_newgame.game_creation.desert_coverage);
				ShowQueryString(STR_JUST_INT, STR_MAPGEN_DESERT_COVERAGE_QUERY_CAPT, 4, this, CS_NUMERAL, QSF_ENABLE_DEFAULT);
				break;

			case WID_GL_LANDSCAPE_PULLDOWN: // Landscape generator
				ShowDropDownMenu(this, _landscape, _settings_newgame.game_creation.land_generator, WID_GL_LANDSCAPE_PULLDOWN, 0, 0);
				break;

			case WID_GL_HEIGHTMAP_ROTATION_PULLDOWN: // Heightmap rotation
				ShowDropDownMenu(this, _rotation, _settings_newgame.game_creation.heightmap_rotation, WID_GL_HEIGHTMAP_ROTATION_PULLDOWN, 0, 0);
				break;

			case WID_GL_TERRAIN_PULLDOWN: // Terrain type
				/* For the original map generation only the first four are valid. */
				ShowDropDownMenu(this, _elevations, _settings_newgame.difficulty.terrain_type, WID_GL_TERRAIN_PULLDOWN, 0, _settings_newgame.game_creation.land_generator == LG_ORIGINAL ? ~0xF : 0);
				break;

			case WID_GL_WATER_PULLDOWN: { // Water quantity
				uint32 hidden_mask = 0;
				/* Disable custom water level when the original map generator is active. */
				if (_settings_newgame.game_creation.land_generator == LG_ORIGINAL) {
					SetBit(hidden_mask, CUSTOM_SEA_LEVEL_NUMBER_DIFFICULTY);
				}
				ShowDropDownMenu(this, _sea_lakes, _settings_newgame.difficulty.quantity_sea_lakes, WID_GL_WATER_PULLDOWN, 0, hidden_mask);
				break;
			}

			case WID_GL_RIVER_PULLDOWN: // Amount of rivers
				ShowDropDownMenu(this, _rivers, _settings_newgame.game_creation.amount_of_rivers, WID_GL_RIVER_PULLDOWN, 0, 0);
				break;

			case WID_GL_SMOOTHNESS_PULLDOWN: // Map smoothness
				ShowDropDownMenu(this, _smoothness, _settings_newgame.game_creation.tgen_smoothness, WID_GL_SMOOTHNESS_PULLDOWN, 0, 0);
				break;

			case WID_GL_VARIETY_PULLDOWN: // Map variety
				ShowDropDownMenu(this, _variety, _settings_newgame.game_creation.variety, WID_GL_VARIETY_PULLDOWN, 0, 0);
				break;

			/* Freetype map borders */
			case WID_GL_WATER_NW:
				_settings_newgame.game_creation.water_borders = ToggleBit(_settings_newgame.game_creation.water_borders, BORDER_NW);
				this->InvalidateData();
				break;

			case WID_GL_WATER_NE:
				_settings_newgame.game_creation.water_borders = ToggleBit(_settings_newgame.game_creation.water_borders, BORDER_NE);
				this->InvalidateData();
				break;

			case WID_GL_WATER_SE:
				_settings_newgame.game_creation.water_borders = ToggleBit(_settings_newgame.game_creation.water_borders, BORDER_SE);
				this->InvalidateData();
				break;

			case WID_GL_WATER_SW:
				_settings_newgame.game_creation.water_borders = ToggleBit(_settings_newgame.game_creation.water_borders, BORDER_SW);
				this->InvalidateData();
				break;

			case WID_GL_BORDERS_RANDOM:
				_settings_newgame.game_creation.water_borders = (_settings_newgame.game_creation.water_borders == BORDERS_RANDOM) ? 0 : BORDERS_RANDOM;
				this->InvalidateData();
				break;
		}
	}

	void OnTimeout() override
	{
		static const int newgame_raise_widgets[] = {WID_GL_START_DATE_DOWN, WID_GL_START_DATE_UP, WID_GL_SNOW_COVERAGE_UP, WID_GL_SNOW_COVERAGE_DOWN, WID_GL_DESERT_COVERAGE_UP, WID_GL_DESERT_COVERAGE_DOWN, WIDGET_LIST_END};
		static const int heightmap_raise_widgets[] = {WID_GL_HEIGHTMAP_HEIGHT_DOWN, WID_GL_HEIGHTMAP_HEIGHT_UP, WID_GL_START_DATE_DOWN, WID_GL_START_DATE_UP, WID_GL_SNOW_COVERAGE_UP, WID_GL_SNOW_COVERAGE_DOWN, WID_GL_DESERT_COVERAGE_UP, WID_GL_DESERT_COVERAGE_DOWN, WIDGET_LIST_END};

		const int *widget = (mode == GLWM_HEIGHTMAP) ? heightmap_raise_widgets : newgame_raise_widgets;

		for (; *widget != WIDGET_LIST_END; widget++) {
			if (this->IsWidgetLowered(*widget)) {
				this->RaiseWidget(*widget);
				this->SetWidgetDirty(*widget);
			}
		}
	}

	void OnDropdownSelect(int widget, int index) override
	{
		switch (widget) {
			case WID_GL_MAPSIZE_X_PULLDOWN:     _settings_newgame.game_creation.map_x = index; break;
			case WID_GL_MAPSIZE_Y_PULLDOWN:     _settings_newgame.game_creation.map_y = index; break;
			case WID_GL_RIVER_PULLDOWN:         _settings_newgame.game_creation.amount_of_rivers = index; break;
			case WID_GL_SMOOTHNESS_PULLDOWN:    _settings_newgame.game_creation.tgen_smoothness = index;  break;
			case WID_GL_VARIETY_PULLDOWN:       _settings_newgame.game_creation.variety = index; break;

			case WID_GL_LANDSCAPE_PULLDOWN:     _settings_newgame.game_creation.land_generator = index;
				/* If original landgenerator is selected and alpinist terrain_type was selected, revert to mountainous. */
				if (_settings_newgame.game_creation.land_generator == LG_ORIGINAL) {
					_settings_newgame.difficulty.terrain_type = Clamp(_settings_newgame.difficulty.terrain_type, 0, 3);
				}
				break;

			case WID_GL_HEIGHTMAP_ROTATION_PULLDOWN: _settings_newgame.game_creation.heightmap_rotation = index; break;

			case WID_GL_TOWN_PULLDOWN:
				if ((uint)index == CUSTOM_TOWN_NUMBER_DIFFICULTY) {
					this->widget_id = widget;
					SetDParam(0, _settings_newgame.game_creation.custom_town_number);
					ShowQueryString(STR_JUST_INT, STR_MAPGEN_NUMBER_OF_TOWNS, 5, this, CS_NUMERAL, QSF_NONE);
				}
				_settings_newgame.difficulty.number_towns = index;
				break;

			case WID_GL_TOWNNAME_DROPDOWN: // Town names
				if (_game_mode == GM_MENU || Town::GetNumItems() == 0) {
					_settings_newgame.game_creation.town_name = index;
					SetWindowDirty(WC_GAME_OPTIONS, WN_GAME_OPTIONS_GAME_OPTIONS);
				}
				break;

			case WID_GL_INDUSTRY_PULLDOWN: _settings_newgame.difficulty.industry_density = index; break;
			case WID_GL_TERRAIN_PULLDOWN: {
				if ((uint)index == CUSTOM_TERRAIN_TYPE_NUMBER_DIFFICULTY) {
					this->widget_id = widget;
					SetDParam(0, _settings_newgame.game_creation.custom_terrain_type);
					ShowQueryString(STR_JUST_INT, STR_MAPGEN_TERRAIN_TYPE_QUERY_CAPT, 4, this, CS_NUMERAL, QSF_NONE);
				}
				_settings_newgame.difficulty.terrain_type = index;
				break;
			}

			case WID_GL_WATER_PULLDOWN: {
				if ((uint)index == CUSTOM_SEA_LEVEL_NUMBER_DIFFICULTY) {
					this->widget_id = widget;
					SetDParam(0, _settings_newgame.game_creation.custom_sea_level);
					ShowQueryString(STR_JUST_INT, STR_MAPGEN_QUANTITY_OF_SEA_LAKES, 3, this, CS_NUMERAL, QSF_NONE);
				}
				_settings_newgame.difficulty.quantity_sea_lakes = index;
				break;
			}
		}
		this->InvalidateData();
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
			switch (this->widget_id) {
				case WID_GL_HEIGHTMAP_HEIGHT_TEXT: value = MAP_HEIGHT_LIMIT_AUTO_MINIMUM; break;
				case WID_GL_START_DATE_TEXT: value = DEF_START_YEAR; break;
				case WID_GL_SNOW_COVERAGE_TEXT: value = DEF_SNOW_COVERAGE; break;
				case WID_GL_DESERT_COVERAGE_TEXT: value = DEF_DESERT_COVERAGE; break;
				case WID_GL_TOWN_PULLDOWN: value = 1; break;
				case WID_GL_TERRAIN_PULLDOWN: value = MIN_MAP_HEIGHT_LIMIT; break;
				case WID_GL_WATER_PULLDOWN: value = CUSTOM_SEA_LEVEL_MIN_PERCENTAGE; break;
				default: NOT_REACHED();
			}
		}

		switch (this->widget_id) {
			case WID_GL_HEIGHTMAP_HEIGHT_TEXT:
				this->SetWidgetDirty(WID_GL_HEIGHTMAP_HEIGHT_TEXT);
				_settings_newgame.game_creation.heightmap_height = Clamp(value, MIN_HEIGHTMAP_HEIGHT, GetMapHeightLimit());
				break;

			case WID_GL_START_DATE_TEXT:
				this->SetWidgetDirty(WID_GL_START_DATE_TEXT);
				_settings_newgame.game_creation.starting_year = Clamp(value, MIN_YEAR, MAX_YEAR);
				break;

			case WID_GL_SNOW_COVERAGE_TEXT:
				this->SetWidgetDirty(WID_GL_SNOW_COVERAGE_TEXT);
				_settings_newgame.game_creation.snow_coverage = Clamp(value, 0, 100);
				break;

			case WID_GL_DESERT_COVERAGE_TEXT:
				this->SetWidgetDirty(WID_GL_DESERT_COVERAGE_TEXT);
				_settings_newgame.game_creation.desert_coverage = Clamp(value, 0, 100);
				break;

			case WID_GL_TOWN_PULLDOWN:
				_settings_newgame.game_creation.custom_town_number = Clamp(value, 1, CUSTOM_TOWN_MAX_NUMBER);
				break;

			case WID_GL_TERRAIN_PULLDOWN:
				_settings_newgame.game_creation.custom_terrain_type = Clamp(value, MIN_CUSTOM_TERRAIN_TYPE, GetMapHeightLimit());
				break;

			case WID_GL_WATER_PULLDOWN:
				_settings_newgame.game_creation.custom_sea_level = Clamp(value, CUSTOM_SEA_LEVEL_MIN_PERCENTAGE, CUSTOM_SEA_LEVEL_MAX_PERCENTAGE);
				break;
		}

		this->InvalidateData();
	}
};

static WindowDesc _generate_landscape_desc(
	WDP_CENTER, nullptr, 0, 0,
	WC_GENERATE_LANDSCAPE, WC_NONE,
	0,
	_nested_generate_landscape_widgets, lengthof(_nested_generate_landscape_widgets)
);

static WindowDesc _heightmap_load_desc(
	WDP_CENTER, nullptr, 0, 0,
	WC_GENERATE_LANDSCAPE, WC_NONE,
	0,
	_nested_heightmap_load_widgets, lengthof(_nested_heightmap_load_widgets)
);

static void _ShowGenerateLandscape(GenerateLandscapeWindowMode mode)
{
	uint x = 0;
	uint y = 0;

	CloseWindowByClass(WC_GENERATE_LANDSCAPE);

	/* Generate a new seed when opening the window */
	_settings_newgame.game_creation.generation_seed = InteractiveRandom();

	if (mode == GLWM_HEIGHTMAP) {
		/* If the function returns negative, it means there was a problem loading the heightmap */
		if (!GetHeightmapDimensions(_file_to_saveload.detail_ftype, _file_to_saveload.name.c_str(), &x, &y)) return;
	}

	WindowDesc *desc = (mode == GLWM_HEIGHTMAP) ? &_heightmap_load_desc : &_generate_landscape_desc;
	GenerateLandscapeWindow *w = AllocateWindowDescFront<GenerateLandscapeWindow>(desc, mode, true);

	if (mode == GLWM_HEIGHTMAP) {
		w->x = x;
		w->y = y;
		strecpy(w->name, _file_to_saveload.title, lastof(w->name));
	}

	SetWindowDirty(WC_GENERATE_LANDSCAPE, mode);
}

/** Start with a normal game. */
void ShowGenerateLandscape()
{
	_ShowGenerateLandscape(GLWM_GENERATE);
}

/** Start with loading a heightmap. */
void ShowHeightmapLoad()
{
	_ShowGenerateLandscape(GLWM_HEIGHTMAP);
}

/** Start with a scenario editor. */
void StartScenarioEditor()
{
	StartGeneratingLandscape(GLWM_SCENARIO);
}

/**
 * Start a normal game without the GUI.
 * @param seed The seed of the new game.
 */
void StartNewGameWithoutGUI(uint32 seed)
{
	/* GenerateWorld takes care of the possible GENERATE_NEW_SEED value in 'seed' */
	_settings_newgame.game_creation.generation_seed = seed;

	StartGeneratingLandscape(GLWM_GENERATE);
}

struct CreateScenarioWindow : public Window
{
	uint widget_id;

	CreateScenarioWindow(WindowDesc *desc, WindowNumber window_number) : Window(desc)
	{
		this->InitNested(window_number);
		this->LowerWidget(_settings_newgame.game_creation.landscape + WID_CS_TEMPERATE);
	}

	void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_CS_START_DATE_TEXT:
				SetDParam(0, ConvertYMDToDate(_settings_newgame.game_creation.starting_year, 0, 1));
				break;

			case WID_CS_MAPSIZE_X_PULLDOWN:
				SetDParam(0, 1LL << _settings_newgame.game_creation.map_x);
				break;

			case WID_CS_MAPSIZE_Y_PULLDOWN:
				SetDParam(0, 1LL << _settings_newgame.game_creation.map_y);
				break;

			case WID_CS_FLAT_LAND_HEIGHT_TEXT:
				SetDParam(0, _settings_newgame.game_creation.se_flat_world_height);
				break;
		}
	}

	void OnPaint() override
	{
		this->SetWidgetDisabledState(WID_CS_START_DATE_DOWN,       _settings_newgame.game_creation.starting_year <= MIN_YEAR);
		this->SetWidgetDisabledState(WID_CS_START_DATE_UP,         _settings_newgame.game_creation.starting_year >= MAX_YEAR);
		this->SetWidgetDisabledState(WID_CS_FLAT_LAND_HEIGHT_DOWN, _settings_newgame.game_creation.se_flat_world_height <= 0);
		this->SetWidgetDisabledState(WID_CS_FLAT_LAND_HEIGHT_UP,   _settings_newgame.game_creation.se_flat_world_height >= GetMapHeightLimit());

		this->SetWidgetLoweredState(WID_CS_TEMPERATE, _settings_newgame.game_creation.landscape == LT_TEMPERATE);
		this->SetWidgetLoweredState(WID_CS_ARCTIC,    _settings_newgame.game_creation.landscape == LT_ARCTIC);
		this->SetWidgetLoweredState(WID_CS_TROPICAL,  _settings_newgame.game_creation.landscape == LT_TROPIC);
		this->SetWidgetLoweredState(WID_CS_TOYLAND,   _settings_newgame.game_creation.landscape == LT_TOYLAND);

		this->DrawWidgets();
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		StringID str = STR_JUST_INT;
		switch (widget) {
			case WID_CS_START_DATE_TEXT:
				SetDParam(0, ConvertYMDToDate(MAX_YEAR, 0, 1));
				str = STR_BLACK_DATE_LONG;
				break;

			case WID_CS_MAPSIZE_X_PULLDOWN:
			case WID_CS_MAPSIZE_Y_PULLDOWN:
				SetDParamMaxValue(0, MAX_MAP_SIZE);
				break;

			case WID_CS_FLAT_LAND_HEIGHT_TEXT:
				SetDParamMaxValue(0, MAX_TILE_HEIGHT);
				break;

			default:
				return;
		}
		*size = maxdim(*size, GetStringBoundingBox(str));
		size->width += padding.width;
		size->height += padding.height;
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_CS_TEMPERATE:
			case WID_CS_ARCTIC:
			case WID_CS_TROPICAL:
			case WID_CS_TOYLAND:
				this->RaiseWidget(_settings_newgame.game_creation.landscape + WID_CS_TEMPERATE);
				SetNewLandscapeType(widget - WID_CS_TEMPERATE);
				break;

			case WID_CS_MAPSIZE_X_PULLDOWN: // Mapsize X
				ShowDropDownList(this, BuildMapsizeDropDown(), _settings_newgame.game_creation.map_x, WID_CS_MAPSIZE_X_PULLDOWN);
				break;

			case WID_CS_MAPSIZE_Y_PULLDOWN: // Mapsize Y
				ShowDropDownList(this, BuildMapsizeDropDown(), _settings_newgame.game_creation.map_y, WID_CS_MAPSIZE_Y_PULLDOWN);
				break;

			case WID_CS_EMPTY_WORLD: // Empty world / flat world
				StartGeneratingLandscape(GLWM_SCENARIO);
				break;

			case WID_CS_RANDOM_WORLD: // Generate
				ShowGenerateLandscape();
				break;

			case WID_CS_START_DATE_DOWN:
			case WID_CS_START_DATE_UP: // Year buttons
				/* Don't allow too fast scrolling */
				if (!(this->flags & WF_TIMEOUT) || this->timeout_timer <= 1) {
					this->HandleButtonClick(widget);
					this->SetDirty();

					_settings_newgame.game_creation.starting_year = Clamp(_settings_newgame.game_creation.starting_year + widget - WID_CS_START_DATE_TEXT, MIN_YEAR, MAX_YEAR);
				}
				_left_button_clicked = false;
				break;

			case WID_CS_START_DATE_TEXT: // Year text
				this->widget_id = WID_CS_START_DATE_TEXT;
				SetDParam(0, _settings_newgame.game_creation.starting_year);
				ShowQueryString(STR_JUST_INT, STR_MAPGEN_START_DATE_QUERY_CAPT, 8, this, CS_NUMERAL, QSF_NONE);
				break;

			case WID_CS_FLAT_LAND_HEIGHT_DOWN:
			case WID_CS_FLAT_LAND_HEIGHT_UP: // Height level buttons
				/* Don't allow too fast scrolling */
				if (!(this->flags & WF_TIMEOUT) || this->timeout_timer <= 1) {
					this->HandleButtonClick(widget);
					this->SetDirty();

					_settings_newgame.game_creation.se_flat_world_height = Clamp(_settings_newgame.game_creation.se_flat_world_height + widget - WID_CS_FLAT_LAND_HEIGHT_TEXT, 0, GetMapHeightLimit());
				}
				_left_button_clicked = false;
				break;

			case WID_CS_FLAT_LAND_HEIGHT_TEXT: // Height level text
				this->widget_id = WID_CS_FLAT_LAND_HEIGHT_TEXT;
				SetDParam(0, _settings_newgame.game_creation.se_flat_world_height);
				ShowQueryString(STR_JUST_INT, STR_SE_MAPGEN_FLAT_WORLD_HEIGHT_QUERY_CAPT, 4, this, CS_NUMERAL, QSF_NONE);
				break;
		}
	}

	void OnTimeout() override
	{
		static const int raise_widgets[] = {WID_CS_START_DATE_DOWN, WID_CS_START_DATE_UP, WID_CS_FLAT_LAND_HEIGHT_DOWN, WID_CS_FLAT_LAND_HEIGHT_UP, WIDGET_LIST_END};
		for (const int *widget = raise_widgets; *widget != WIDGET_LIST_END; widget++) {
			if (this->IsWidgetLowered(*widget)) {
				this->RaiseWidget(*widget);
				this->SetWidgetDirty(*widget);
			}
		}
	}

	void OnDropdownSelect(int widget, int index) override
	{
		switch (widget) {
			case WID_CS_MAPSIZE_X_PULLDOWN: _settings_newgame.game_creation.map_x = index; break;
			case WID_CS_MAPSIZE_Y_PULLDOWN: _settings_newgame.game_creation.map_y = index; break;
		}
		this->SetDirty();
	}

	void OnQueryTextFinished(char *str) override
	{
		if (!StrEmpty(str)) {
			int32 value = atoi(str);

			switch (this->widget_id) {
				case WID_CS_START_DATE_TEXT:
					this->SetWidgetDirty(WID_CS_START_DATE_TEXT);
					_settings_newgame.game_creation.starting_year = Clamp(value, MIN_YEAR, MAX_YEAR);
					break;

				case WID_CS_FLAT_LAND_HEIGHT_TEXT:
					this->SetWidgetDirty(WID_CS_FLAT_LAND_HEIGHT_TEXT);
					_settings_newgame.game_creation.se_flat_world_height = Clamp(value, 0, GetMapHeightLimit());
					break;
			}

			this->SetDirty();
		}
	}
};

static const NWidgetPart _nested_create_scenario_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_BROWN),
		NWidget(WWT_CAPTION, COLOUR_BROWN), SetDataTip(STR_SE_MAPGEN_CAPTION, STR_NULL),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_BROWN),
		NWidget(NWID_SPACER), SetMinimalSize(0, 10),
		/* Landscape style selection. */
		NWidget(NWID_HORIZONTAL), SetPIP(10, 3, 10),
			NWidget(WWT_IMGBTN_2, COLOUR_ORANGE, WID_CS_TEMPERATE), SetDataTip(SPR_SELECT_TEMPERATE, STR_INTRO_TOOLTIP_TEMPERATE),
			NWidget(WWT_IMGBTN_2, COLOUR_ORANGE, WID_CS_ARCTIC), SetDataTip(SPR_SELECT_SUB_ARCTIC, STR_INTRO_TOOLTIP_SUB_ARCTIC_LANDSCAPE),
			NWidget(WWT_IMGBTN_2, COLOUR_ORANGE, WID_CS_TROPICAL), SetDataTip(SPR_SELECT_SUB_TROPICAL, STR_INTRO_TOOLTIP_SUB_TROPICAL_LANDSCAPE),
			NWidget(WWT_IMGBTN_2, COLOUR_ORANGE, WID_CS_TOYLAND), SetDataTip(SPR_SELECT_TOYLAND, STR_INTRO_TOOLTIP_TOYLAND_LANDSCAPE),
		EndContainer(),
		NWidget(NWID_HORIZONTAL), SetPIP(10, 8, 10),
			/* Green generation type buttons: 'Flat land' and 'Random land'. */
			NWidget(NWID_VERTICAL), SetPIP(10, 6, 10),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREEN, WID_CS_EMPTY_WORLD), SetDataTip(STR_SE_MAPGEN_FLAT_WORLD, STR_SE_MAPGEN_FLAT_WORLD_TOOLTIP), SetFill(1, 1),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREEN, WID_CS_RANDOM_WORLD), SetDataTip(STR_SE_MAPGEN_RANDOM_LAND, STR_TERRAFORM_TOOLTIP_GENERATE_RANDOM_LAND), SetFill(1, 1),
			EndContainer(),
			/* Labels + setting drop-downs */
			NWidget(NWID_VERTICAL), SetPIP(10, 6, 10),
				/* Map size. */
				NWidget(NWID_HORIZONTAL),
					NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_MAPGEN_MAPSIZE, STR_NULL), SetPadding(1, 0, 0, 0),
					NWidget(NWID_SPACER), SetMinimalSize(6, 0), SetFill(1, 0),
					NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_CS_MAPSIZE_X_PULLDOWN), SetDataTip(STR_JUST_INT, STR_NULL), SetPadding(0, 4, 0, 0),
					NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_MAPGEN_BY, STR_NULL), SetPadding(1, 2, 0, 0),
					NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_CS_MAPSIZE_Y_PULLDOWN), SetDataTip(STR_JUST_INT, STR_NULL),
				EndContainer(),
				/* Date. */
				NWidget(NWID_HORIZONTAL),
					NWidget(WWT_TEXT, COLOUR_ORANGE), SetDataTip(STR_MAPGEN_DATE, STR_NULL), SetPadding(1, 0, 0, 0),
					NWidget(NWID_SPACER), SetMinimalSize(6, 0), SetFill(1, 0),
					NWidget(WWT_IMGBTN, COLOUR_ORANGE, WID_CS_START_DATE_DOWN), SetFill(0, 1), SetDataTip(SPR_ARROW_DOWN, STR_SCENEDIT_TOOLBAR_TOOLTIP_MOVE_THE_STARTING_DATE_BACKWARD),
					NWidget(WWT_PUSHTXTBTN, COLOUR_ORANGE, WID_CS_START_DATE_TEXT), SetDataTip(STR_BLACK_DATE_LONG, STR_NULL),
					NWidget(WWT_IMGBTN, COLOUR_ORANGE, WID_CS_START_DATE_UP), SetFill(0, 1), SetDataTip(SPR_ARROW_UP, STR_SCENEDIT_TOOLBAR_TOOLTIP_MOVE_THE_STARTING_DATE_FORWARD),
				EndContainer(),
				/* Flat map height. */
				NWidget(NWID_HORIZONTAL),
					NWidget(WWT_TEXT, COLOUR_ORANGE),
												SetDataTip(STR_SE_MAPGEN_FLAT_WORLD_HEIGHT, STR_NULL), SetPadding(1, 0, 0, 0),
					NWidget(NWID_SPACER), SetMinimalSize(6, 0), SetFill(1, 0),
					NWidget(WWT_IMGBTN, COLOUR_ORANGE, WID_CS_FLAT_LAND_HEIGHT_DOWN), SetFill(0, 1), SetDataTip(SPR_ARROW_DOWN, STR_SE_MAPGEN_FLAT_WORLD_HEIGHT_DOWN),
					NWidget(WWT_PUSHTXTBTN, COLOUR_ORANGE, WID_CS_FLAT_LAND_HEIGHT_TEXT), SetDataTip(STR_BLACK_INT, STR_NULL),
					NWidget(WWT_IMGBTN, COLOUR_ORANGE, WID_CS_FLAT_LAND_HEIGHT_UP), SetFill(0, 1), SetDataTip(SPR_ARROW_UP, STR_SE_MAPGEN_FLAT_WORLD_HEIGHT_UP),
				EndContainer(),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _create_scenario_desc(
	WDP_CENTER, nullptr, 0, 0,
	WC_GENERATE_LANDSCAPE, WC_NONE,
	0,
	_nested_create_scenario_widgets, lengthof(_nested_create_scenario_widgets)
);

/** Show the window to create a scenario. */
void ShowCreateScenario()
{
	CloseWindowByClass(WC_GENERATE_LANDSCAPE);
	new CreateScenarioWindow(&_create_scenario_desc, GLWM_SCENARIO);
}

static const NWidgetPart _nested_generate_progress_widgets[] = {
	NWidget(WWT_CAPTION, COLOUR_GREY), SetDataTip(STR_GENERATION_WORLD, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	NWidget(WWT_PANEL, COLOUR_GREY),
		NWidget(NWID_HORIZONTAL), SetPIP(20, 0, 20),
			NWidget(NWID_VERTICAL), SetPIP(11, 8, 11),
				NWidget(WWT_EMPTY, INVALID_COLOUR, WID_GP_PROGRESS_BAR), SetFill(1, 0),
				NWidget(WWT_EMPTY, INVALID_COLOUR, WID_GP_PROGRESS_TEXT), SetFill(1, 0),
				NWidget(WWT_TEXTBTN, COLOUR_WHITE, WID_GP_ABORT), SetDataTip(STR_GENERATION_ABORT, STR_NULL), SetFill(1, 0),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};


static WindowDesc _generate_progress_desc(
	WDP_CENTER, nullptr, 0, 0,
	WC_MODAL_PROGRESS, WC_NONE,
	0,
	_nested_generate_progress_widgets, lengthof(_nested_generate_progress_widgets)
);

struct GenWorldStatus {
	uint percent;
	StringID cls;
	uint current;
	uint total;
	std::chrono::steady_clock::time_point next_update;
};

static GenWorldStatus _gws;

static const StringID _generation_class_table[]  = {
	STR_GENERATION_WORLD_GENERATION,
	STR_SCENEDIT_TOOLBAR_LANDSCAPE_GENERATION,
	STR_GENERATION_RIVER_GENERATION,
	STR_GENERATION_CLEARING_TILES,
	STR_SCENEDIT_TOOLBAR_TOWN_GENERATION,
	STR_SCENEDIT_TOOLBAR_INDUSTRY_GENERATION,
	STR_GENERATION_OBJECT_GENERATION,
	STR_GENERATION_TREE_GENERATION,
	STR_GENERATION_SETTINGUP_GAME,
	STR_GENERATION_PREPARING_TILELOOP,
	STR_GENERATION_PREPARING_SCRIPT,
	STR_GENERATION_PREPARING_GAME
};
static_assert(lengthof(_generation_class_table) == GWP_CLASS_COUNT);


static void AbortGeneratingWorldCallback(Window *w, bool confirmed)
{
	if (confirmed) {
		AbortGeneratingWorld();
	} else if (HasModalProgress() && !IsGeneratingWorldAborted()) {
		SetMouseCursor(SPR_CURSOR_ZZZ, PAL_NONE);
	}
}

struct GenerateProgressWindow : public Window {

	GenerateProgressWindow() : Window(&_generate_progress_desc)
	{
		this->InitNested();
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_GP_ABORT:
				SetMouseCursorBusy(false);
				ShowQuery(
					STR_GENERATION_ABORT_CAPTION,
					STR_GENERATION_ABORT_MESSAGE,
					this,
					AbortGeneratingWorldCallback
				);
				break;
		}
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_GP_PROGRESS_BAR: {
				SetDParamMaxValue(0, 100);
				*size = GetStringBoundingBox(STR_GENERATION_PROGRESS);
				/* We need some spacing for the 'border' */
				size->height += 8;
				size->width += 8;
				break;
			}

			case WID_GP_PROGRESS_TEXT:
				for (uint i = 0; i < GWP_CLASS_COUNT; i++) {
					size->width = std::max(size->width, GetStringBoundingBox(_generation_class_table[i]).width);
				}
				size->height = FONT_HEIGHT_NORMAL * 2 + WD_PAR_VSEP_NORMAL;
				break;
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_GP_PROGRESS_BAR:
				/* Draw the % complete with a bar and a text */
				DrawFrameRect(r.left, r.top, r.right, r.bottom, COLOUR_GREY, FR_BORDERONLY);
				DrawFrameRect(r.left + 1, r.top + 1, (int)((r.right - r.left - 2) * _gws.percent / 100) + r.left + 1, r.bottom - 1, COLOUR_MAUVE, FR_NONE);
				SetDParam(0, _gws.percent);
				DrawString(r.left, r.right, r.top + 5, STR_GENERATION_PROGRESS, TC_FROMSTRING, SA_HOR_CENTER);
				break;

			case WID_GP_PROGRESS_TEXT:
				/* Tell which class we are generating */
				DrawString(r.left, r.right, r.top, _gws.cls, TC_FROMSTRING, SA_HOR_CENTER);

				/* And say where we are in that class */
				SetDParam(0, _gws.current);
				SetDParam(1, _gws.total);
				DrawString(r.left, r.right, r.top + FONT_HEIGHT_NORMAL + WD_PAR_VSEP_NORMAL, STR_GENERATION_PROGRESS_NUM, TC_FROMSTRING, SA_HOR_CENTER);
		}
	}
};

/**
 * Initializes the progress counters to the starting point.
 */
void PrepareGenerateWorldProgress()
{
	_gws.cls = STR_GENERATION_WORLD_GENERATION;
	_gws.current = 0;
	_gws.total = 0;
	_gws.percent = 0;
	_gws.next_update = std::chrono::steady_clock::now();
}

/**
 * Show the window where a user can follow the process of the map generation.
 */
void ShowGenerateWorldProgress()
{
	if (BringWindowToFrontById(WC_MODAL_PROGRESS, 0)) return;
	new GenerateProgressWindow();
}

static void _SetGeneratingWorldProgress(GenWorldProgress cls, uint progress, uint total)
{
	static const int percent_table[] = {0, 5, 14, 17, 20, 40, 60, 65, 80, 85, 95, 99, 100 };
	static_assert(lengthof(percent_table) == GWP_CLASS_COUNT + 1);
	assert(cls < GWP_CLASS_COUNT);

	/* Check if we really are generating the world.
	 * For example, placing trees via the SE also calls this function, but
	 * shouldn't try to update the progress.
	 */
	if (!HasModalProgress()) return;

	if (IsGeneratingWorldAborted()) {
		HandleGeneratingWorldAbortion();
		return;
	}

	if (total == 0) {
		assert(_gws.cls == _generation_class_table[cls]);
		_gws.current += progress;
		assert(_gws.current <= _gws.total);
	} else {
		_gws.cls     = _generation_class_table[cls];
		_gws.current = progress;
		_gws.total   = total;
		_gws.percent = percent_table[cls];
	}

	/* Percentage is about the number of completed tasks, so 'current - 1' */
	_gws.percent = percent_table[cls] + (percent_table[cls + 1] - percent_table[cls]) * (_gws.current == 0 ? 0 : _gws.current - 1) / _gws.total;

	if (_network_dedicated) {
		static uint last_percent = 0;

		/* Never display 0% */
		if (_gws.percent == 0) return;
		/* Reset if percent is lower than the last recorded */
		if (_gws.percent < last_percent) last_percent = 0;
		/* Display every 5%, but 6% is also very valid.. just not smaller steps than 5% */
		if (_gws.percent % 5 != 0 && _gws.percent <= last_percent + 5) return;
		/* Never show steps smaller than 2%, even if it is a mod 5% */
		if (_gws.percent <= last_percent + 2) return;

		Debug(net, 3, "Map generation percentage complete: {}", _gws.percent);
		last_percent = _gws.percent;

		return;
	}

	SetWindowDirty(WC_MODAL_PROGRESS, 0);

	VideoDriver::GetInstance()->GameLoopPause();
}

/**
 * Set the total of a stage of the world generation.
 * @param cls the current class we are in.
 * @param total Set the total expected items for this class.
 *
 * Warning: this function isn't clever. Don't go from class 4 to 3. Go upwards, always.
 *  Also, progress works if total is zero, total works if progress is zero.
 */
void SetGeneratingWorldProgress(GenWorldProgress cls, uint total)
{
	if (total == 0) return;

	_SetGeneratingWorldProgress(cls, 0, total);
}

/**
 * Increases the current stage of the world generation with one.
 * @param cls the current class we are in.
 *
 * Warning: this function isn't clever. Don't go from class 4 to 3. Go upwards, always.
 *  Also, progress works if total is zero, total works if progress is zero.
 */
void IncreaseGeneratingWorldProgress(GenWorldProgress cls)
{
	/* In fact the param 'class' isn't needed.. but for some security reasons, we want it around */
	_SetGeneratingWorldProgress(cls, 1, 0);
}
