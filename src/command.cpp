/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file command.cpp Handling of commands. */

#include "stdafx.h"
#include "landscape.h"
#include "error.h"
#include "gui.h"
#include "command_func.h"
#include "network/network_type.h"
#include "network/network.h"
#include "genworld.h"
#include "strings_func.h"
#include "texteff.hpp"
#include "town.h"
#include "date_func.h"
#include "company_func.h"
#include "company_base.h"
#include "signal_func.h"
#include "core/backup_type.hpp"
#include "object_base.h"

#include "table/strings.h"

#include "safeguards.h"

CommandProc CmdBuildRailroadTrack;
CommandProc CmdRemoveRailroadTrack;
CommandProc CmdBuildSingleRail;
CommandProc CmdRemoveSingleRail;

CommandProc CmdLandscapeClear;

CommandProc CmdBuildBridge;

CommandProc CmdBuildRailStation;
CommandProc CmdRemoveFromRailStation;
CommandProc CmdConvertRail;

CommandProc CmdBuildSingleSignal;
CommandProc CmdRemoveSingleSignal;

CommandProc CmdTerraformLand;

CommandProc CmdBuildObject;
CommandProc CmdSellLandArea;

CommandProc CmdBuildTunnel;

CommandProc CmdBuildTrainDepot;
CommandProc CmdRemoveTrainDepot;
CommandProc CmdBuildRailWaypoint;
CommandProc CmdRenameWaypoint;
CommandProc CmdRemoveFromRailWaypoint;

CommandProc CmdBuildRoadStop;
CommandProc CmdRemoveRoadStop;

CommandProc CmdBuildLongRoad;
CommandProc CmdRemoveLongRoad;
CommandProc CmdBuildRoad;

CommandProc CmdBuildRoadDepot;

CommandProc CmdConvertRoad;

CommandProc CmdBuildAirport;

CommandProc CmdBuildDock;

CommandProc CmdBuildShipDepot;

CommandProc CmdBuildBuoy;

CommandProc CmdPlantTree;

CommandProc CmdMoveRailVehicle;

CommandProc CmdBuildVehicle;
CommandProc CmdSellVehicle;
CommandProc CmdRefitVehicle;
CommandProc CmdSendVehicleToDepot;
CommandProc CmdSetVehicleVisibility;

CommandProc CmdForceTrainProceed;
CommandProc CmdReverseTrainDirection;

CommandProc CmdClearOrderBackup;
CommandProc CmdModifyOrder;
CommandProc CmdSkipToOrder;
CommandProc CmdDeleteOrder;
CommandProc CmdInsertOrder;
CommandProc CmdChangeServiceInt;

CommandProc CmdBuildIndustry;
CommandProc CmdIndustryCtrl;

CommandProc CmdSetCompanyManagerFace;
CommandProc CmdSetCompanyColour;

CommandProc CmdIncreaseLoan;
CommandProc CmdDecreaseLoan;

CommandProc CmdWantEnginePreview;
CommandProc CmdEngineCtrl;

CommandProc CmdRenameVehicle;
CommandProc CmdRenameEngine;

CommandProc CmdRenameCompany;
CommandProc CmdRenamePresident;

CommandProc CmdRenameStation;
CommandProc CmdRenameDepot;

CommandProc CmdPlaceSign;
CommandProc CmdRenameSign;

CommandProc CmdTurnRoadVeh;

CommandProc CmdPause;

CommandProc CmdBuyShareInCompany;
CommandProc CmdSellShareInCompany;
CommandProc CmdBuyCompany;

CommandProc CmdFoundTown;
CommandProc CmdRenameTown;
CommandProc CmdDoTownAction;
CommandProc CmdTownGrowthRate;
CommandProc CmdTownRating;
CommandProc CmdTownCargoGoal;
CommandProc CmdTownSetText;
CommandProc CmdExpandTown;
CommandProc CmdDeleteTown;

CommandProc CmdChangeSetting;
CommandProc CmdChangeCompanySetting;

CommandProc CmdOrderRefit;
CommandProc CmdCloneOrder;

CommandProc CmdClearArea;

CommandProc CmdGiveMoney;
CommandProc CmdMoneyCheat;
CommandProc CmdChangeBankBalance;
CommandProc CmdBuildCanal;
CommandProc CmdBuildLock;

CommandProc CmdCreateSubsidy;
CommandProc CmdCompanyCtrl;
CommandProc CmdCustomNewsItem;
CommandProc CmdCreateGoal;
CommandProc CmdRemoveGoal;
CommandProc CmdSetGoalText;
CommandProc CmdSetGoalProgress;
CommandProc CmdSetGoalCompleted;
CommandProc CmdGoalQuestion;
CommandProc CmdGoalQuestionAnswer;
CommandProc CmdCreateStoryPage;
CommandProc CmdCreateStoryPageElement;
CommandProc CmdUpdateStoryPageElement;
CommandProc CmdSetStoryPageTitle;
CommandProc CmdSetStoryPageDate;
CommandProc CmdShowStoryPage;
CommandProc CmdRemoveStoryPage;
CommandProc CmdRemoveStoryPageElement;
CommandProc CmdScrollViewport;
CommandProc CmdStoryPageButton;

CommandProc CmdLevelLand;

CommandProc CmdBuildSignalTrack;
CommandProc CmdRemoveSignalTrack;

CommandProc CmdSetAutoReplace;

CommandProc CmdCloneVehicle;
CommandProc CmdStartStopVehicle;
CommandProc CmdMassStartStopVehicle;
CommandProc CmdAutoreplaceVehicle;
CommandProc CmdDepotSellAllVehicles;
CommandProc CmdDepotMassAutoReplace;

CommandProc CmdCreateGroup;
CommandProc CmdAlterGroup;
CommandProc CmdDeleteGroup;
CommandProc CmdAddVehicleGroup;
CommandProc CmdAddSharedVehicleGroup;
CommandProc CmdRemoveAllVehiclesGroup;
CommandProc CmdSetGroupFlag;
CommandProc CmdSetGroupLivery;

CommandProc CmdMoveOrder;
CommandProc CmdChangeTimetable;
CommandProc CmdSetVehicleOnTime;
CommandProc CmdAutofillTimetable;
CommandProc CmdSetTimetableStart;

CommandProc CmdOpenCloseAirport;

#define DEF_CMD(proc, flags, type) {proc, #proc, (CommandFlags)flags, type}

/**
 * The master command table
 *
 * This table contains all possible CommandProc functions with
 * the flags which belongs to it. The indices are the same
 * as the value from the CMD_* enums.
 */
static const Command _command_proc_table[] = {
	DEF_CMD(CmdBuildRailroadTrack,       CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_RAILROAD_TRACK
	DEF_CMD(CmdRemoveRailroadTrack,                     CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_REMOVE_RAILROAD_TRACK
	DEF_CMD(CmdBuildSingleRail,          CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_SINGLE_RAIL
	DEF_CMD(CmdRemoveSingleRail,                        CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_REMOVE_SINGLE_RAIL
	DEF_CMD(CmdLandscapeClear,                         CMD_DEITY, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_LANDSCAPE_CLEAR
	DEF_CMD(CmdBuildBridge,  CMD_DEITY | CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_BRIDGE
	DEF_CMD(CmdBuildRailStation,         CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_RAIL_STATION
	DEF_CMD(CmdBuildTrainDepot,          CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_TRAIN_DEPOT
	DEF_CMD(CmdRemoveTrainDepot,                        CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_REMOVE_TRAIN_DEPOT
	DEF_CMD(CmdBuildSingleSignal,                       CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_SIGNALS
	DEF_CMD(CmdRemoveSingleSignal,                      CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_REMOVE_SIGNALS
	DEF_CMD(CmdTerraformLand,           CMD_ALL_TILES | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_TERRAFORM_LAND
	DEF_CMD(CmdBuildObject,              CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_OBJECT
	DEF_CMD(CmdBuildTunnel,                 CMD_DEITY | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_TUNNEL
	DEF_CMD(CmdRemoveFromRailStation,                          0, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_REMOVE_FROM_RAIL_STATION
	DEF_CMD(CmdConvertRail,                                    0, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_CONVERT_RAILD
	DEF_CMD(CmdBuildRailWaypoint,                              0, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_RAIL_WAYPOINT
	DEF_CMD(CmdRenameWaypoint,                                 0, CMDT_OTHER_MANAGEMENT      ), // CMD_RENAME_WAYPOINT
	DEF_CMD(CmdRemoveFromRailWaypoint,                         0, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_REMOVE_FROM_RAIL_WAYPOINT

	DEF_CMD(CmdBuildRoadStop,            CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_ROAD_STOP
	DEF_CMD(CmdRemoveRoadStop,                                 0, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_REMOVE_ROAD_STOP
	DEF_CMD(CmdBuildLongRoad,CMD_DEITY | CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_LONG_ROAD
	DEF_CMD(CmdRemoveLongRoad,            CMD_NO_TEST | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_REMOVE_LONG_ROAD; towns may disallow removing road bits (as they are connected) in test, but in exec they're removed and thus removing is allowed.
	DEF_CMD(CmdBuildRoad,    CMD_DEITY | CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_ROAD
	DEF_CMD(CmdBuildRoadDepot,           CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_ROAD_DEPOT
	DEF_CMD(CmdConvertRoad,                                    0, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_CONVERT_ROAD

	DEF_CMD(CmdBuildAirport,             CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_AIRPORT
	DEF_CMD(CmdBuildDock,                               CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_DOCK
	DEF_CMD(CmdBuildShipDepot,                          CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_SHIP_DEPOT
	DEF_CMD(CmdBuildBuoy,                               CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_BUOY
	DEF_CMD(CmdPlantTree,                               CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_PLANT_TREE

	DEF_CMD(CmdBuildVehicle,                       CMD_CLIENT_ID, CMDT_VEHICLE_CONSTRUCTION  ), // CMD_BUILD_VEHICLE
	DEF_CMD(CmdSellVehicle,                        CMD_CLIENT_ID, CMDT_VEHICLE_CONSTRUCTION  ), // CMD_SELL_VEHICLE
	DEF_CMD(CmdRefitVehicle,                                   0, CMDT_VEHICLE_CONSTRUCTION  ), // CMD_REFIT_VEHICLE
	DEF_CMD(CmdSendVehicleToDepot,                             0, CMDT_VEHICLE_MANAGEMENT    ), // CMD_SEND_VEHICLE_TO_DEPOT
	DEF_CMD(CmdSetVehicleVisibility,                           0, CMDT_COMPANY_SETTING       ), // CMD_SET_VEHICLE_VISIBILITY

	DEF_CMD(CmdMoveRailVehicle,                                0, CMDT_VEHICLE_CONSTRUCTION  ), // CMD_MOVE_RAIL_VEHICLE
	DEF_CMD(CmdForceTrainProceed,                              0, CMDT_VEHICLE_MANAGEMENT    ), // CMD_FORCE_TRAIN_PROCEED
	DEF_CMD(CmdReverseTrainDirection,                          0, CMDT_VEHICLE_MANAGEMENT    ), // CMD_REVERSE_TRAIN_DIRECTION

	DEF_CMD(CmdClearOrderBackup,                   CMD_CLIENT_ID, CMDT_SERVER_SETTING        ), // CMD_CLEAR_ORDER_BACKUP
	DEF_CMD(CmdModifyOrder,                                    0, CMDT_ROUTE_MANAGEMENT      ), // CMD_MODIFY_ORDER
	DEF_CMD(CmdSkipToOrder,                                    0, CMDT_ROUTE_MANAGEMENT      ), // CMD_SKIP_TO_ORDER
	DEF_CMD(CmdDeleteOrder,                                    0, CMDT_ROUTE_MANAGEMENT      ), // CMD_DELETE_ORDER
	DEF_CMD(CmdInsertOrder,                                    0, CMDT_ROUTE_MANAGEMENT      ), // CMD_INSERT_ORDER

	DEF_CMD(CmdChangeServiceInt,                               0, CMDT_VEHICLE_MANAGEMENT    ), // CMD_CHANGE_SERVICE_INT

	DEF_CMD(CmdBuildIndustry,                          CMD_DEITY, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_INDUSTRY
	DEF_CMD(CmdIndustryCtrl,            CMD_STR_CTRL | CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_INDUSTRY_CTRL

	DEF_CMD(CmdSetCompanyManagerFace,                          0, CMDT_OTHER_MANAGEMENT      ), // CMD_SET_COMPANY_MANAGER_FACE
	DEF_CMD(CmdSetCompanyColour,                               0, CMDT_OTHER_MANAGEMENT      ), // CMD_SET_COMPANY_COLOUR

	DEF_CMD(CmdIncreaseLoan,                                   0, CMDT_MONEY_MANAGEMENT      ), // CMD_INCREASE_LOAN
	DEF_CMD(CmdDecreaseLoan,                                   0, CMDT_MONEY_MANAGEMENT      ), // CMD_DECREASE_LOAN

	DEF_CMD(CmdWantEnginePreview,                              0, CMDT_VEHICLE_MANAGEMENT    ), // CMD_WANT_ENGINE_PREVIEW
	DEF_CMD(CmdEngineCtrl,                             CMD_DEITY, CMDT_VEHICLE_MANAGEMENT    ), // CMD_ENGINE_CTRL

	DEF_CMD(CmdRenameVehicle,                                  0, CMDT_OTHER_MANAGEMENT      ), // CMD_RENAME_VEHICLE
	DEF_CMD(CmdRenameEngine,                          CMD_SERVER, CMDT_OTHER_MANAGEMENT      ), // CMD_RENAME_ENGINE

	DEF_CMD(CmdRenameCompany,                                  0, CMDT_OTHER_MANAGEMENT      ), // CMD_RENAME_COMPANY
	DEF_CMD(CmdRenamePresident,                                0, CMDT_OTHER_MANAGEMENT      ), // CMD_RENAME_PRESIDENT

	DEF_CMD(CmdRenameStation,                                  0, CMDT_OTHER_MANAGEMENT      ), // CMD_RENAME_STATION
	DEF_CMD(CmdRenameDepot,                                    0, CMDT_OTHER_MANAGEMENT      ), // CMD_RENAME_DEPOT

	DEF_CMD(CmdPlaceSign,                              CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_PLACE_SIGN
	DEF_CMD(CmdRenameSign,                             CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_RENAME_SIGN

	DEF_CMD(CmdTurnRoadVeh,                                    0, CMDT_VEHICLE_MANAGEMENT    ), // CMD_TURN_ROADVEH

	DEF_CMD(CmdPause,                    CMD_SERVER | CMD_NO_EST, CMDT_SERVER_SETTING        ), // CMD_PAUSE

	DEF_CMD(CmdBuyShareInCompany,                              0, CMDT_MONEY_MANAGEMENT      ), // CMD_BUY_SHARE_IN_COMPANY
	DEF_CMD(CmdSellShareInCompany,                             0, CMDT_MONEY_MANAGEMENT      ), // CMD_SELL_SHARE_IN_COMPANY
	DEF_CMD(CmdBuyCompany,                                     0, CMDT_MONEY_MANAGEMENT      ), // CMD_BUY_COMANY

	DEF_CMD(CmdFoundTown,                CMD_DEITY | CMD_NO_TEST, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_FOUND_TOWN; founding random town can fail only in exec run
	DEF_CMD(CmdRenameTown,                CMD_DEITY | CMD_SERVER, CMDT_OTHER_MANAGEMENT      ), // CMD_RENAME_TOWN
	DEF_CMD(CmdDoTownAction,                                   0, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_DO_TOWN_ACTION
	DEF_CMD(CmdTownCargoGoal,                          CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_TOWN_CARGO_GOAL
	DEF_CMD(CmdTownGrowthRate,                         CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_TOWN_GROWTH_RATE
	DEF_CMD(CmdTownRating,                             CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_TOWN_RATING
	DEF_CMD(CmdTownSetText,             CMD_STR_CTRL | CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_TOWN_SET_TEXT
	DEF_CMD(CmdExpandTown,                             CMD_DEITY, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_EXPAND_TOWN
	DEF_CMD(CmdDeleteTown,                           CMD_OFFLINE, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_DELETE_TOWN

	DEF_CMD(CmdOrderRefit,                                     0, CMDT_ROUTE_MANAGEMENT      ), // CMD_ORDER_REFIT
	DEF_CMD(CmdCloneOrder,                                     0, CMDT_ROUTE_MANAGEMENT      ), // CMD_CLONE_ORDER

	DEF_CMD(CmdClearArea,                            CMD_NO_TEST, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_CLEAR_AREA; destroying multi-tile houses makes town rating differ between test and execution

	DEF_CMD(CmdMoneyCheat,                           CMD_OFFLINE, CMDT_CHEAT                 ), // CMD_MONEY_CHEAT
	DEF_CMD(CmdChangeBankBalance,                      CMD_DEITY, CMDT_MONEY_MANAGEMENT      ), // CMD_CHANGE_BANK_BALANCE
	DEF_CMD(CmdBuildCanal,                              CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_CANAL
	DEF_CMD(CmdCreateSubsidy,                          CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_CREATE_SUBSIDY
	DEF_CMD(CmdCompanyCtrl, CMD_SPECTATOR | CMD_CLIENT_ID | CMD_NO_EST, CMDT_SERVER_SETTING  ), // CMD_COMPANY_CTRL
	DEF_CMD(CmdCustomNewsItem,          CMD_STR_CTRL | CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_CUSTOM_NEWS_ITEM
	DEF_CMD(CmdCreateGoal,              CMD_STR_CTRL | CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_CREATE_GOAL
	DEF_CMD(CmdRemoveGoal,                             CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_REMOVE_GOAL
	DEF_CMD(CmdSetGoalText,             CMD_STR_CTRL | CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_SET_GOAL_TEXT
	DEF_CMD(CmdSetGoalProgress,         CMD_STR_CTRL | CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_SET_GOAL_PROGRESS
	DEF_CMD(CmdSetGoalCompleted,        CMD_STR_CTRL | CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_SET_GOAL_COMPLETED
	DEF_CMD(CmdGoalQuestion,            CMD_STR_CTRL | CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_GOAL_QUESTION
	DEF_CMD(CmdGoalQuestionAnswer,                     CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_GOAL_QUESTION_ANSWER
	DEF_CMD(CmdCreateStoryPage,         CMD_STR_CTRL | CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_CREATE_STORY_PAGE
	DEF_CMD(CmdCreateStoryPageElement,  CMD_STR_CTRL | CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_CREATE_STORY_PAGE_ELEMENT
	DEF_CMD(CmdUpdateStoryPageElement,  CMD_STR_CTRL | CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_UPDATE_STORY_PAGE_ELEMENT
	DEF_CMD(CmdSetStoryPageTitle,       CMD_STR_CTRL | CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_SET_STORY_PAGE_TITLE
	DEF_CMD(CmdSetStoryPageDate,                       CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_SET_STORY_PAGE_DATE
	DEF_CMD(CmdShowStoryPage,                          CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_SHOW_STORY_PAGE
	DEF_CMD(CmdRemoveStoryPage,                        CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_REMOVE_STORY_PAGE
	DEF_CMD(CmdRemoveStoryPageElement,                 CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_REMOVE_STORY_ELEMENT_PAGE
	DEF_CMD(CmdScrollViewport,                         CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_SCROLL_VIEWPORT
	DEF_CMD(CmdStoryPageButton,                        CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_STORY_PAGE_BUTTON

	DEF_CMD(CmdLevelLand, CMD_ALL_TILES | CMD_NO_TEST | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_LEVEL_LAND; test run might clear tiles multiple times, in execution that only happens once

	DEF_CMD(CmdBuildLock,                               CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_LOCK

	DEF_CMD(CmdBuildSignalTrack,                        CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_SIGNAL_TRACK
	DEF_CMD(CmdRemoveSignalTrack,                       CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_REMOVE_SIGNAL_TRACK

	DEF_CMD(CmdGiveMoney,                                      0, CMDT_MONEY_MANAGEMENT      ), // CMD_GIVE_MONEY
	DEF_CMD(CmdChangeSetting,                         CMD_SERVER, CMDT_SERVER_SETTING        ), // CMD_CHANGE_SETTING
	DEF_CMD(CmdChangeCompanySetting,                           0, CMDT_COMPANY_SETTING       ), // CMD_CHANGE_COMPANY_SETTING
	DEF_CMD(CmdSetAutoReplace,                                 0, CMDT_VEHICLE_MANAGEMENT    ), // CMD_SET_AUTOREPLACE
	DEF_CMD(CmdCloneVehicle,                         CMD_NO_TEST, CMDT_VEHICLE_CONSTRUCTION  ), // CMD_CLONE_VEHICLE; NewGRF callbacks influence building and refitting making it impossible to correctly estimate the cost
	DEF_CMD(CmdStartStopVehicle,                               0, CMDT_VEHICLE_MANAGEMENT    ), // CMD_START_STOP_VEHICLE
	DEF_CMD(CmdMassStartStopVehicle,                           0, CMDT_VEHICLE_MANAGEMENT    ), // CMD_MASS_START_STOP
	DEF_CMD(CmdAutoreplaceVehicle,                             0, CMDT_VEHICLE_MANAGEMENT    ), // CMD_AUTOREPLACE_VEHICLE
	DEF_CMD(CmdDepotSellAllVehicles,                           0, CMDT_VEHICLE_CONSTRUCTION  ), // CMD_DEPOT_SELL_ALL_VEHICLES
	DEF_CMD(CmdDepotMassAutoReplace,                           0, CMDT_VEHICLE_CONSTRUCTION  ), // CMD_DEPOT_MASS_AUTOREPLACE
	DEF_CMD(CmdCreateGroup,                                    0, CMDT_ROUTE_MANAGEMENT      ), // CMD_CREATE_GROUP
	DEF_CMD(CmdDeleteGroup,                                    0, CMDT_ROUTE_MANAGEMENT      ), // CMD_DELETE_GROUP
	DEF_CMD(CmdAlterGroup,                                     0, CMDT_OTHER_MANAGEMENT      ), // CMD_ALTER_GROUP
	DEF_CMD(CmdAddVehicleGroup,                                0, CMDT_ROUTE_MANAGEMENT      ), // CMD_ADD_VEHICLE_GROUP
	DEF_CMD(CmdAddSharedVehicleGroup,                          0, CMDT_ROUTE_MANAGEMENT      ), // CMD_ADD_SHARE_VEHICLE_GROUP
	DEF_CMD(CmdRemoveAllVehiclesGroup,                         0, CMDT_ROUTE_MANAGEMENT      ), // CMD_REMOVE_ALL_VEHICLES_GROUP
	DEF_CMD(CmdSetGroupFlag,                                   0, CMDT_ROUTE_MANAGEMENT      ), // CMD_SET_GROUP_FLAG
	DEF_CMD(CmdSetGroupLivery,                                 0, CMDT_ROUTE_MANAGEMENT      ), // CMD_SET_GROUP_LIVERY
	DEF_CMD(CmdMoveOrder,                                      0, CMDT_ROUTE_MANAGEMENT      ), // CMD_MOVE_ORDER
	DEF_CMD(CmdChangeTimetable,                                0, CMDT_ROUTE_MANAGEMENT      ), // CMD_CHANGE_TIMETABLE
	DEF_CMD(CmdSetVehicleOnTime,                               0, CMDT_ROUTE_MANAGEMENT      ), // CMD_SET_VEHICLE_ON_TIME
	DEF_CMD(CmdAutofillTimetable,                              0, CMDT_ROUTE_MANAGEMENT      ), // CMD_AUTOFILL_TIMETABLE
	DEF_CMD(CmdSetTimetableStart,                              0, CMDT_ROUTE_MANAGEMENT      ), // CMD_SET_TIMETABLE_START

	DEF_CMD(CmdOpenCloseAirport,                               0, CMDT_ROUTE_MANAGEMENT      ), // CMD_OPEN_CLOSE_AIRPORT
};

/*!
 * This function range-checks a cmd, and checks if the cmd is not nullptr
 *
 * @param cmd The integer value of a command
 * @return true if the command is valid (and got a CommandProc function)
 */
bool IsValidCommand(uint32 cmd)
{
	cmd &= CMD_ID_MASK;

	return cmd < lengthof(_command_proc_table) && _command_proc_table[cmd].proc != nullptr;
}

/*!
 * This function mask the parameter with CMD_ID_MASK and returns
 * the flags which belongs to the given command.
 *
 * @param cmd The integer value of the command
 * @return The flags for this command
 */
CommandFlags GetCommandFlags(uint32 cmd)
{
	assert(IsValidCommand(cmd));

	return _command_proc_table[cmd & CMD_ID_MASK].flags;
}

/*!
 * This function mask the parameter with CMD_ID_MASK and returns
 * the name which belongs to the given command.
 *
 * @param cmd The integer value of the command
 * @return The name for this command
 */
const char *GetCommandName(uint32 cmd)
{
	assert(IsValidCommand(cmd));

	return _command_proc_table[cmd & CMD_ID_MASK].name;
}

/**
 * Returns whether the command is allowed while the game is paused.
 * @param cmd The command to check.
 * @return True if the command is allowed while paused, false otherwise.
 */
bool IsCommandAllowedWhilePaused(uint32 cmd)
{
	/* Lookup table for the command types that are allowed for a given pause level setting. */
	static const int command_type_lookup[] = {
		CMDPL_ALL_ACTIONS,     ///< CMDT_LANDSCAPE_CONSTRUCTION
		CMDPL_NO_LANDSCAPING,  ///< CMDT_VEHICLE_CONSTRUCTION
		CMDPL_NO_LANDSCAPING,  ///< CMDT_MONEY_MANAGEMENT
		CMDPL_NO_CONSTRUCTION, ///< CMDT_VEHICLE_MANAGEMENT
		CMDPL_NO_CONSTRUCTION, ///< CMDT_ROUTE_MANAGEMENT
		CMDPL_NO_CONSTRUCTION, ///< CMDT_OTHER_MANAGEMENT
		CMDPL_NO_CONSTRUCTION, ///< CMDT_COMPANY_SETTING
		CMDPL_NO_ACTIONS,      ///< CMDT_SERVER_SETTING
		CMDPL_NO_ACTIONS,      ///< CMDT_CHEAT
	};
	static_assert(lengthof(command_type_lookup) == CMDT_END);

	assert(IsValidCommand(cmd));
	return _game_mode == GM_EDITOR || command_type_lookup[_command_proc_table[cmd & CMD_ID_MASK].type] <= _settings_game.construction.command_pause_level;
}


static int _docommand_recursive = 0;

/**
 * Shorthand for calling the long DoCommand with a container.
 *
 * @param container Container with (almost) all information
 * @param flags Flags for the command and how to execute the command
 * @see CommandProc
 * @return the cost
 */
CommandCost DoCommand(const CommandContainer *container, DoCommandFlag flags)
{
	return DoCommand(container->tile, container->p1, container->p2, flags, container->cmd & CMD_ID_MASK, container->text);
}

/*!
 * This function executes a given command with the parameters from the #CommandProc parameter list.
 * Depending on the flags parameter it execute or test a command.
 *
 * @param tile The tile to apply the command on (for the #CommandProc)
 * @param p1 Additional data for the command (for the #CommandProc)
 * @param p2 Additional data for the command (for the #CommandProc)
 * @param flags Flags for the command and how to execute the command
 * @param cmd The command-id to execute (a value of the CMD_* enums)
 * @param text The text to pass
 * @see CommandProc
 * @return the cost
 */
CommandCost DoCommand(TileIndex tile, uint32 p1, uint32 p2, DoCommandFlag flags, uint32 cmd, const std::string &text)
{
	CommandCost res;

	/* Do not even think about executing out-of-bounds tile-commands */
	if (tile != 0 && (tile >= MapSize() || (!IsValidTile(tile) && (flags & DC_ALL_TILES) == 0))) return CMD_ERROR;

	/* Chop of any CMD_MSG or other flags; we don't need those here */
	CommandProc *proc = _command_proc_table[cmd & CMD_ID_MASK].proc;

	_docommand_recursive++;

	/* only execute the test call if it's toplevel, or we're not execing. */
	if (_docommand_recursive == 1 || !(flags & DC_EXEC) ) {
		if (_docommand_recursive == 1) _cleared_object_areas.clear();
		SetTownRatingTestMode(true);
		res = proc(tile, flags & ~DC_EXEC, p1, p2, text);
		SetTownRatingTestMode(false);
		if (res.Failed()) {
			goto error;
		}

		if (_docommand_recursive == 1 &&
				!(flags & DC_QUERY_COST) &&
				!(flags & DC_BANKRUPT) &&
				!CheckCompanyHasMoney(res)) { // CheckCompanyHasMoney() modifies 'res' to an error if it fails.
			goto error;
		}

		if (!(flags & DC_EXEC)) {
			_docommand_recursive--;
			return res;
		}
	}

	/* Execute the command here. All cost-relevant functions set the expenses type
	 * themselves to the cost object at some point */
	if (_docommand_recursive == 1) _cleared_object_areas.clear();
	res = proc(tile, flags, p1, p2, text);
	if (res.Failed()) {
error:
		_docommand_recursive--;
		return res;
	}

	/* if toplevel, subtract the money. */
	if (--_docommand_recursive == 0 && !(flags & DC_BANKRUPT)) {
		SubtractMoneyFromCompany(res);
	}

	return res;
}

/*!
 * This functions returns the money which can be used to execute a command.
 * This is either the money of the current company or INT64_MAX if there
 * is no such a company "at the moment" like the server itself.
 *
 * @return The available money of a company or INT64_MAX
 */
Money GetAvailableMoneyForCommand()
{
	CompanyID company = _current_company;
	if (!Company::IsValidID(company)) return INT64_MAX;
	return Company::Get(company)->money;
}

/**
 * Shortcut for the long DoCommandP when having a container with the data.
 * @param container the container with information.
 * @param my_cmd indicator if the command is from a company or server (to display error messages for a user)
 * @return true if the command succeeded, else false
 */
bool DoCommandP(const CommandContainer *container, bool my_cmd)
{
	return DoCommandP(container->tile, container->p1, container->p2, container->cmd, container->callback, container->text, my_cmd);
}

/*!
 * Toplevel network safe docommand function for the current company. Must not be called recursively.
 * The callback is called when the command succeeded or failed. The parameters
 * \a tile, \a p1, and \a p2 are from the #CommandProc function. The parameter \a cmd is the command to execute.
 * The parameter \a my_cmd is used to indicate if the command is from a company or the server.
 *
 * @param tile The tile to perform a command on (see #CommandProc)
 * @param p1 Additional data for the command (see #CommandProc)
 * @param p2 Additional data for the command (see #CommandProc)
 * @param cmd The command to execute (a CMD_* value)
 * @param callback A callback function to call after the command is finished
 * @param text The text to pass
 * @param my_cmd indicator if the command is from a company or server (to display error messages for a user)
 * @return \c true if the command succeeded, else \c false.
 */
bool DoCommandP(TileIndex tile, uint32 p1, uint32 p2, uint32 cmd, CommandCallback *callback, const std::string &text, bool my_cmd)
{
	/* Cost estimation is generally only done when the
	 * local user presses shift while doing something.
	 * However, in case of incoming network commands,
	 * map generation or the pause button we do want
	 * to execute. */
	bool estimate_only = _shift_pressed && IsLocalCompany() &&
			!_generating_world &&
			!(cmd & CMD_NETWORK_COMMAND) &&
			!(GetCommandFlags(cmd) & CMD_NO_EST);

	/* We're only sending the command, so don't do
	 * fancy things for 'success'. */
	bool only_sending = _networking && !(cmd & CMD_NETWORK_COMMAND);

	/* Where to show the message? */
	int x = TileX(tile) * TILE_SIZE;
	int y = TileY(tile) * TILE_SIZE;

	if (_pause_mode != PM_UNPAUSED && !IsCommandAllowedWhilePaused(cmd) && !estimate_only) {
		ShowErrorMessage(GB(cmd, 16, 16), STR_ERROR_NOT_ALLOWED_WHILE_PAUSED, WL_INFO, x, y);
		return false;
	}

	/* Only set p2 when the command does not come from the network. */
	if (!(cmd & CMD_NETWORK_COMMAND) && GetCommandFlags(cmd) & CMD_CLIENT_ID && p2 == 0) p2 = CLIENT_ID_SERVER;

	CommandCost res = DoCommandPInternal(tile, p1, p2, cmd, callback, text, my_cmd, estimate_only);
	if (res.Failed()) {
		/* Only show the error when it's for us. */
		StringID error_part1 = GB(cmd, 16, 16);
		if (estimate_only || (IsLocalCompany() && error_part1 != 0 && my_cmd)) {
			ShowErrorMessage(error_part1, res.GetErrorMessage(), WL_INFO, x, y, res.GetTextRefStackGRF(), res.GetTextRefStackSize(), res.GetTextRefStack());
		}
	} else if (estimate_only) {
		ShowEstimatedCostOrIncome(res.GetCost(), x, y);
	} else if (!only_sending && res.GetCost() != 0 && tile != 0 && IsLocalCompany() && _game_mode != GM_EDITOR) {
		/* Only show the cost animation when we did actually
		 * execute the command, i.e. we're not sending it to
		 * the server, when it has cost the local company
		 * something. Furthermore in the editor there is no
		 * concept of cost, so don't show it there either. */
		ShowCostOrIncomeAnimation(x, y, GetSlopePixelZ(x, y), res.GetCost());
	}

	if (!estimate_only && !only_sending && callback != nullptr) {
		callback(res, tile, p1, p2, cmd);
	}

	return res.Succeeded();
}


/**
 * Helper to deduplicate the code for returning.
 * @param cmd   the command cost to return.
 */
#define return_dcpi(cmd) { _docommand_recursive = 0; return cmd; }

/*!
 * Helper function for the toplevel network safe docommand function for the current company.
 *
 * @param tile The tile to perform a command on (see #CommandProc)
 * @param p1 Additional data for the command (see #CommandProc)
 * @param p2 Additional data for the command (see #CommandProc)
 * @param cmd The command to execute (a CMD_* value)
 * @param callback A callback function to call after the command is finished
 * @param text The text to pass
 * @param my_cmd indicator if the command is from a company or server (to display error messages for a user)
 * @param estimate_only whether to give only the estimate or also execute the command
 * @return the command cost of this function.
 */
CommandCost DoCommandPInternal(TileIndex tile, uint32 p1, uint32 p2, uint32 cmd, CommandCallback *callback, const std::string &text, bool my_cmd, bool estimate_only)
{
	/* Prevent recursion; it gives a mess over the network */
	assert(_docommand_recursive == 0);
	_docommand_recursive = 1;

	/* Reset the state. */
	_additional_cash_required = 0;

	/* Get pointer to command handler */
	byte cmd_id = cmd & CMD_ID_MASK;
	assert(cmd_id < lengthof(_command_proc_table));

	CommandProc *proc = _command_proc_table[cmd_id].proc;
	/* Shouldn't happen, but you never know when someone adds
	 * NULLs to the _command_proc_table. */
	assert(proc != nullptr);

	/* Command flags are used internally */
	CommandFlags cmd_flags = GetCommandFlags(cmd);
	/* Flags get send to the DoCommand */
	DoCommandFlag flags = CommandFlagsToDCFlags(cmd_flags);

	/* Make sure p2 is properly set to a ClientID. */
	assert(!(cmd_flags & CMD_CLIENT_ID) || p2 != 0);

	/* Do not even think about executing out-of-bounds tile-commands */
	if (tile != 0 && (tile >= MapSize() || (!IsValidTile(tile) && (cmd_flags & CMD_ALL_TILES) == 0))) return_dcpi(CMD_ERROR);

	/* Always execute server and spectator commands as spectator */
	bool exec_as_spectator = (cmd_flags & (CMD_SPECTATOR | CMD_SERVER)) != 0;

	/* If the company isn't valid it may only do server command or start a new company!
	 * The server will ditch any server commands a client sends to it, so effectively
	 * this guards the server from executing functions for an invalid company. */
	if (_game_mode == GM_NORMAL && !exec_as_spectator && !Company::IsValidID(_current_company) && !(_current_company == OWNER_DEITY && (cmd_flags & CMD_DEITY) != 0)) {
		return_dcpi(CMD_ERROR);
	}

	Backup<CompanyID> cur_company(_current_company, FILE_LINE);
	if (exec_as_spectator) cur_company.Change(COMPANY_SPECTATOR);

	bool test_and_exec_can_differ = (cmd_flags & CMD_NO_TEST) != 0;

	/* Test the command. */
	_cleared_object_areas.clear();
	SetTownRatingTestMode(true);
	BasePersistentStorageArray::SwitchMode(PSM_ENTER_TESTMODE);
	CommandCost res = proc(tile, flags, p1, p2, text);
	BasePersistentStorageArray::SwitchMode(PSM_LEAVE_TESTMODE);
	SetTownRatingTestMode(false);

	/* Make sure we're not messing things up here. */
	assert(exec_as_spectator ? _current_company == COMPANY_SPECTATOR : cur_company.Verify());

	/* If the command fails, we're doing an estimate
	 * or the player does not have enough money
	 * (unless it's a command where the test and
	 * execution phase might return different costs)
	 * we bail out here. */
	if (res.Failed() || estimate_only ||
			(!test_and_exec_can_differ && !CheckCompanyHasMoney(res))) {
		if (!_networking || _generating_world || (cmd & CMD_NETWORK_COMMAND) != 0) {
			/* Log the failed command as well. Just to be able to be find
			 * causes of desyncs due to bad command test implementations. */
			Debug(desync, 1, "cmdf: {:08x}; {:02x}; {:02x}; {:06x}; {:08x}; {:08x}; {:08x}; \"{}\" ({})", _date, _date_fract, (int)_current_company, tile, p1, p2, cmd & ~CMD_NETWORK_COMMAND, text, GetCommandName(cmd));
		}
		cur_company.Restore();
		return_dcpi(res);
	}

	/*
	 * If we are in network, and the command is not from the network
	 * send it to the command-queue and abort execution
	 */
	if (_networking && !_generating_world && !(cmd & CMD_NETWORK_COMMAND)) {
		NetworkSendCommand(tile, p1, p2, cmd & ~CMD_FLAGS_MASK, callback, text, _current_company);
		cur_company.Restore();

		/* Don't return anything special here; no error, no costs.
		 * This way it's not handled by DoCommand and only the
		 * actual execution of the command causes messages. Also
		 * reset the storages as we've not executed the command. */
		return_dcpi(CommandCost());
	}
	Debug(desync, 1, "cmd: {:08x}; {:02x}; {:02x}; {:06x}; {:08x}; {:08x}; {:08x}; \"{}\" ({})", _date, _date_fract, (int)_current_company, tile, p1, p2, cmd & ~CMD_NETWORK_COMMAND, text, GetCommandName(cmd));

	/* Actually try and execute the command. If no cost-type is given
	 * use the construction one */
	_cleared_object_areas.clear();
	BasePersistentStorageArray::SwitchMode(PSM_ENTER_COMMAND);
	CommandCost res2 = proc(tile, flags | DC_EXEC, p1, p2, text);
	BasePersistentStorageArray::SwitchMode(PSM_LEAVE_COMMAND);

	if (cmd_id == CMD_COMPANY_CTRL) {
		cur_company.Trash();
		/* We are a new company                  -> Switch to new local company.
		 * We were closed down                   -> Switch to spectator
		 * Some other company opened/closed down -> The outside function will switch back */
		_current_company = _local_company;
	} else {
		/* Make sure nothing bad happened, like changing the current company. */
		assert(exec_as_spectator ? _current_company == COMPANY_SPECTATOR : cur_company.Verify());
		cur_company.Restore();
	}

	/* If the test and execution can differ we have to check the
	 * return of the command. Otherwise we can check whether the
	 * test and execution have yielded the same result,
	 * i.e. cost and error state are the same. */
	if (!test_and_exec_can_differ) {
		assert(res.GetCost() == res2.GetCost() && res.Failed() == res2.Failed()); // sanity check
	} else if (res2.Failed()) {
		return_dcpi(res2);
	}

	/* If we're needing more money and we haven't done
	 * anything yet, ask for the money! */
	if (_additional_cash_required != 0 && res2.GetCost() == 0) {
		/* It could happen we removed rail, thus gained money, and deleted something else.
		 * So make sure the signal buffer is empty even in this case */
		UpdateSignalsInBuffer();
		SetDParam(0, _additional_cash_required);
		return_dcpi(CommandCost(STR_ERROR_NOT_ENOUGH_CASH_REQUIRES_CURRENCY));
	}

	/* update last build coordinate of company. */
	if (tile != 0) {
		Company *c = Company::GetIfValid(_current_company);
		if (c != nullptr) c->last_build_coordinate = tile;
	}

	SubtractMoneyFromCompany(res2);

	/* update signals if needed */
	UpdateSignalsInBuffer();

	return_dcpi(res2);
}
#undef return_dcpi


/**
 * Adds the cost of the given command return value to this cost.
 * Also takes a possible error message when it is set.
 * @param ret The command to add the cost of.
 */
void CommandCost::AddCost(const CommandCost &ret)
{
	this->AddCost(ret.cost);
	if (this->success && !ret.success) {
		this->message = ret.message;
		this->success = false;
	}
}

/**
 * Values to put on the #TextRefStack for the error message.
 * There is only one static instance of the array, just like there is only one
 * instance of normal DParams.
 */
uint32 CommandCost::textref_stack[16];

/**
 * Activate usage of the NewGRF #TextRefStack for the error message.
 * @param grffile NewGRF that provides the #TextRefStack
 * @param num_registers number of entries to copy from the temporary NewGRF registers
 */
void CommandCost::UseTextRefStack(const GRFFile *grffile, uint num_registers)
{
	extern TemporaryStorageArray<int32, 0x110> _temp_store;

	assert(num_registers < lengthof(textref_stack));
	this->textref_stack_grffile = grffile;
	this->textref_stack_size = num_registers;
	for (uint i = 0; i < num_registers; i++) {
		textref_stack[i] = _temp_store.GetValue(0x100 + i);
	}
}
