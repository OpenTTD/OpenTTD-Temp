/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file window_type.h Types related to windows */

#ifndef WINDOW_TYPE_H
#define WINDOW_TYPE_H

/** %Window numbers. */
enum WindowNumberEnum {
	WN_GAME_OPTIONS_AI = 0,          ///< AI settings.
	WN_GAME_OPTIONS_ABOUT,           ///< About window.
	WN_GAME_OPTIONS_NEWGRF_STATE,    ///< NewGRF settings.
	WN_GAME_OPTIONS_GAME_OPTIONS,    ///< Game options.
	WN_GAME_OPTIONS_GAME_SETTINGS,   ///< Game settings.

	WN_QUERY_STRING = 0,  ///< Query string.
	WN_QUERY_STRING_SIGN, ///< Query string for signs.

	WN_CONFIRM_POPUP_QUERY = 0,       ///< Query popup confirm.
	WN_CONFIRM_POPUP_QUERY_BOOTSTRAP, ///< Query popup confirm for bootstrap.

	WN_NETWORK_WINDOW_GAME = 0,     ///< Network game window.
	WN_NETWORK_WINDOW_LOBBY,        ///< Network lobby window.
	WN_NETWORK_WINDOW_CONTENT_LIST, ///< Network content list.
	WN_NETWORK_WINDOW_START,        ///< Network start server.

	WN_NETWORK_STATUS_WINDOW_JOIN = 0,         ///< Network join status.
	WN_NETWORK_STATUS_WINDOW_CONTENT_DOWNLOAD, ///< Network content download status.
};

/** %Window classes. */
enum WindowClass {
	WC_NONE, ///< No window, redirects to WC_MAIN_WINDOW.

	/**
	 * Main window; %Window numbers:
	 *   - 0 = #MainWidgets
	 */
	WC_MAIN_WINDOW = WC_NONE,

	/**
	 * Main toolbar (the long bar at the top); %Window numbers:
	 *   - 0 = #ToolbarNormalWidgets
	 *   - 0 = #ToolbarEditorWidgets
	 */
	WC_MAIN_TOOLBAR,

	/**
	 * Statusbar (at the bottom of your screen); %Window numbers:
	 *   - 0 = #StatusbarWidgets
	 */
	WC_STATUS_BAR,

	/**
	 * Build toolbar; %Window numbers:
	 *   - #TRANSPORT_RAIL = #RailToolbarWidgets
	 *   - #TRANSPORT_AIR = #AirportToolbarWidgets
	 *   - #TRANSPORT_WATER = #DockToolbarWidgets
	 *   - #TRANSPORT_ROAD = #RoadToolbarWidgets
	 */
	WC_BUILD_TOOLBAR,

	/**
	 * Scenario build toolbar; %Window numbers:
	 *   - #TRANSPORT_WATER = #DockToolbarWidgets
	 *   - #TRANSPORT_ROAD = #RoadToolbarWidgets
	 */
	WC_SCEN_BUILD_TOOLBAR,

	/**
	 * Build trees toolbar; %Window numbers:
	 *   - 0 = #BuildTreesWidgets
	 */
	WC_BUILD_TREES,

	/**
	 * Transparency toolbar; %Window numbers:
	 *   - 0 = #TransparencyToolbarWidgets
	 */
	WC_TRANSPARENCY_TOOLBAR,

	/**
	 * Build signal toolbar; %Window numbers:
	 *   - #TRANSPORT_RAIL = #BuildSignalWidgets
	 */
	WC_BUILD_SIGNAL,

	/**
	 * Small map; %Window numbers:
	 *   - 0 = #SmallMapWidgets
	 */
	WC_SMALLMAP,

	/**
	 * Error message; %Window numbers:
	 *   - 0 = #ErrorMessageWidgets
	 */
	WC_ERRMSG,

	/**
	 * Tooltip window; %Window numbers:
	 *   - 0 = #ToolTipsWidgets
	 */
	WC_TOOLTIPS,

	/**
	 * Query string window; %Window numbers:
	 *   - #WN_QUERY_STRING = #QueryStringWidgets
	 *   - #WN_QUERY_STRING_SIGN = #QueryEditSignWidgets
	 */
	WC_QUERY_STRING,

	/**
	 * Popup with confirm question; %Window numbers:
	 *   - #WN_CONFIRM_POPUP_QUERY = #QueryWidgets
	 *   - #WN_CONFIRM_POPUP_QUERY_BOOTSTRAP = #BootstrapAskForDownloadWidgets
	 */
	WC_CONFIRM_POPUP_QUERY,

	/**
	 * Popup with a set of buttons, designed to ask the user a question
	 *  from a GameScript. %Window numbers:
	 *   - uniqueid = #GoalQuestionWidgets
	 */
	WC_GOAL_QUESTION,


	/**
	 * Saveload window; %Window numbers:
	 *   - 0 = #SaveLoadWidgets
	 */
	WC_SAVELOAD,

	/**
	 * Land info window; %Window numbers:
	 *   - 0 = #LandInfoWidgets
	 */
	WC_LAND_INFO,

	/**
	 * Drop down menu; %Window numbers:
	 *   - 0 = #DropdownMenuWidgets
	 */
	WC_DROPDOWN_MENU,

	/**
	 * On Screen Keyboard; %Window numbers:
	 *   - 0 = #OnScreenKeyboardWidgets
	 */
	WC_OSK,

	/**
	 * Set date; %Window numbers:
	 *   - #VehicleID = #SetDateWidgets
	 */
	WC_SET_DATE,


	/**
	 * AI settings; %Window numbers:
	 *   - 0 = #AISettingsWidgets
	 */
	WC_AI_SETTINGS,

	/**
	 * NewGRF parameters; %Window numbers:
	 *   - 0 = #NewGRFParametersWidgets
	 */
	WC_GRF_PARAMETERS,

	/**
	 * textfile; %Window numbers:
	 *   - 0 = #TextfileWidgets
	 */
	WC_TEXTFILE,


	/**
	 * Town authority; %Window numbers:
	 *   - #TownID = #TownAuthorityWidgets
	 */
	WC_TOWN_AUTHORITY,

	/**
	 * Vehicle details; %Window numbers:
	 *   - #VehicleID = #VehicleDetailsWidgets
	 */
	WC_VEHICLE_DETAILS,

	/**
	 * Vehicle refit; %Window numbers:
	 *   - #VehicleID = #VehicleRefitWidgets
	 */
	WC_VEHICLE_REFIT,

	/**
	 * Vehicle orders; %Window numbers:
	 *   - #VehicleID = #OrderWidgets
	 */
	WC_VEHICLE_ORDERS,

	/**
	 * Replace vehicle window; %Window numbers:
	 *   - #VehicleType = #ReplaceVehicleWidgets
	 */
	WC_REPLACE_VEHICLE,

	/**
	 * Vehicle timetable; %Window numbers:
	 *   - #VehicleID = #VehicleTimetableWidgets
	 */
	WC_VEHICLE_TIMETABLE,

	/**
	 * Company colour selection; %Window numbers:
	 *   - #CompanyID = #SelectCompanyLiveryWidgets
	 */
	WC_COMPANY_COLOUR,

	/**
	 * Alter company face window; %Window numbers:
	 *   - #CompanyID = #SelectCompanyManagerFaceWidgets
	 */
	WC_COMPANY_MANAGER_FACE,

	/**
	 * Select station (when joining stations); %Window numbers:
	 *   - 0 = #JoinStationWidgets
	 */
	WC_SELECT_STATION,

	/**
	 * News window; %Window numbers:
	 *   - 0 = #NewsWidgets
	 */
	WC_NEWS_WINDOW,

	/**
	 * Town directory; %Window numbers:
	 *   - 0 = #TownDirectoryWidgets
	 */
	WC_TOWN_DIRECTORY,

	/**
	 * Subsidies list; %Window numbers:
	 *   - 0 = #SubsidyListWidgets
	 */
	WC_SUBSIDIES_LIST,

	/**
	 * Industry directory; %Window numbers:
	 *   - 0 = #IndustryDirectoryWidgets
	 */
	WC_INDUSTRY_DIRECTORY,

	/**
	 * News history list; %Window numbers:
	 *   - 0 = #MessageHistoryWidgets
	 */
	WC_MESSAGE_HISTORY,

	/**
	 * Sign list; %Window numbers:
	 *   - 0 = #SignListWidgets
	 */
	WC_SIGN_LIST,

	/**
	 * AI list; %Window numbers:
	 *   - 0 = #AIListWidgets
	 */
	WC_AI_LIST,

	/**
	 * Goals list; %Window numbers:
	 *   - 0 ; #GoalListWidgets
	 */
	WC_GOALS_LIST,

	/**
	 * Story book; %Window numbers:
	 *   - CompanyID = #StoryBookWidgets
	 */
	WC_STORY_BOOK,

	/**
	 * Station list; %Window numbers:
	 *   - #CompanyID = #StationListWidgets
	 */
	WC_STATION_LIST,

	/**
	 * Trains list; %Window numbers:
	 *   - Packed value = #GroupListWidgets / #VehicleListWidgets
	 */
	WC_TRAINS_LIST,

	/**
	 * Road vehicle list; %Window numbers:
	 *   - Packed value = #GroupListWidgets / #VehicleListWidgets
	 */
	WC_ROADVEH_LIST,

	/**
	 * Ships list; %Window numbers:
	 *   - Packed value = #GroupListWidgets / #VehicleListWidgets
	 */
	WC_SHIPS_LIST,

	/**
	 * Aircraft list; %Window numbers:
	 *   - Packed value = #GroupListWidgets / #VehicleListWidgets
	 */
	WC_AIRCRAFT_LIST,


	/**
	 * Town view; %Window numbers:
	 *   - #TownID = #TownViewWidgets
	 */
	WC_TOWN_VIEW,

	/**
	 * Vehicle view; %Window numbers:
	 *   - #VehicleID = #VehicleViewWidgets
	 */
	WC_VEHICLE_VIEW,

	/**
	 * Station view; %Window numbers:
	 *   - #StationID = #StationViewWidgets
	 */
	WC_STATION_VIEW,

	/**
	 * Depot view; %Window numbers:
	 *   - #TileIndex = #DepotWidgets
	 */
	WC_VEHICLE_DEPOT,

	/**
	 * Waypoint view; %Window numbers:
	 *   - #WaypointID = #WaypointWidgets
	 */
	WC_WAYPOINT_VIEW,

	/**
	 * Industry view; %Window numbers:
	 *   - #IndustryID = #IndustryViewWidgets
	 */
	WC_INDUSTRY_VIEW,

	/**
	 * Company view; %Window numbers:
	 *   - #CompanyID = #CompanyWidgets
	 */
	WC_COMPANY,


	/**
	 * Build object; %Window numbers:
	 *   - 0 = #BuildObjectWidgets
	 */
	WC_BUILD_OBJECT,

	/**
	 * Build vehicle; %Window numbers:
	 *   - #VehicleType = #BuildVehicleWidgets
	 *   - #TileIndex = #BuildVehicleWidgets
	 */
	WC_BUILD_VEHICLE,

	/**
	 * Build bridge; %Window numbers:
	 *   - #TransportType = #BuildBridgeSelectionWidgets
	 */
	WC_BUILD_BRIDGE,

	/**
	 * Build station; %Window numbers:
	 *   - #TRANSPORT_AIR = #AirportPickerWidgets
	 *   - #TRANSPORT_WATER = #DockToolbarWidgets
	 *   - #TRANSPORT_RAIL = #BuildRailStationWidgets
	 */
	WC_BUILD_STATION,

	/**
	 * Build bus station; %Window numbers:
	 *   - #TRANSPORT_ROAD = #BuildRoadStationWidgets
	 */
	WC_BUS_STATION,

	/**
	 * Build truck station; %Window numbers:
	 *   - #TRANSPORT_ROAD = #BuildRoadStationWidgets
	 */
	WC_TRUCK_STATION,

	/**
	 * Build tram station; %Window numbers:
	 *   - #TRANSPORT_ROAD = #BuildRoadStationWidgets
	 */
	WC_TRAM_STATION,

	/**
	 * Build tram station; %Window numbers:
	 *   - #TRANSPORT_ROAD = #BuildRoadStationWidgets
	 */
	WC_FREIGHT_TRAM_STATION,

	/**
	 * Build depot; %Window numbers:
	 *   - #TRANSPORT_WATER = #BuildDockDepotWidgets
	 *   - #TRANSPORT_RAIL = #BuildRailDepotWidgets
	 *   - #TRANSPORT_ROAD = #BuildRoadDepotWidgets
	 */
	WC_BUILD_DEPOT,

	/**
	 * Build waypoint; %Window numbers:
	 *   - #TRANSPORT_RAIL = #BuildRailWaypointWidgets
	 */
	WC_BUILD_WAYPOINT,

	/**
	 * Found a town; %Window numbers:
	 *   - 0 = #TownFoundingWidgets
	 */
	WC_FOUND_TOWN,

	/**
	 * Build industry; %Window numbers:
	 *   - 0 = #DynamicPlaceIndustriesWidgets
	 */
	WC_BUILD_INDUSTRY,


	/**
	 * Select game window; %Window numbers:
	 *   - 0 = #SelectGameIntroWidgets
	 */
	WC_SELECT_GAME,

	/**
	 * Landscape generation (in Scenario Editor); %Window numbers:
	 *   - 0 = #TerraformToolbarWidgets
	 *   - 0 = #EditorTerraformToolbarWidgets
	 */
	WC_SCEN_LAND_GEN,

	/**
	 * Generate landscape (newgame); %Window numbers:
	 *   - GLWM_SCENARIO = #CreateScenarioWidgets
	 *   - #GenerateLandscapeWindowMode = #GenerateLandscapeWidgets
	 */
	WC_GENERATE_LANDSCAPE,

	/**
	 * Progress report of landscape generation; %Window numbers:
	 *   - 0 = #GenerationProgressWidgets
	 *   - 1 = #ScanProgressWidgets
	 */
	WC_MODAL_PROGRESS,


	/**
	 * Network window; %Window numbers:
	 *   - #WN_NETWORK_WINDOW_GAME = #NetworkGameWidgets
	 *   - #WN_NETWORK_WINDOW_LOBBY = #NetworkLobbyWidgets
	 *   - #WN_NETWORK_WINDOW_CONTENT_LIST = #NetworkContentListWidgets
	 *   - #WN_NETWORK_WINDOW_START = #NetworkStartServerWidgets
	 */
	WC_NETWORK_WINDOW,

	/**
	 * Client list; %Window numbers:
	 *   - 0 = #ClientListWidgets
	 */
	WC_CLIENT_LIST,

	/**
	 * Popup for the client list; %Window numbers:
	 *   - #ClientID = #ClientListPopupWidgets
	 */
	WC_CLIENT_LIST_POPUP,

	/**
	 * Network status window; %Window numbers:
	 *   - #WN_NETWORK_STATUS_WINDOW_JOIN = #NetworkJoinStatusWidgets
	 *   - #WN_NETWORK_STATUS_WINDOW_CONTENT_DOWNLOAD = #NetworkContentDownloadStatusWidgets
	 */
	WC_NETWORK_STATUS_WINDOW,

	/**
	 * Chatbox; %Window numbers:
	 *   - #DestType = #NetWorkChatWidgets
	 */
	WC_SEND_NETWORK_MSG,

	/**
	 * Company password query; %Window numbers:
	 *   - 0 = #NetworkCompanyPasswordWidgets
	 */
	WC_COMPANY_PASSWORD_WINDOW,


	/**
	 * Industry cargoes chain; %Window numbers:
	 *   - 0 = #IndustryCargoesWidgets
	 */
	WC_INDUSTRY_CARGOES,

	/**
	 * Legend for graphs; %Window numbers:
	 *   - 0 = #GraphLegendWidgets
	 */
	WC_GRAPH_LEGEND,

	/**
	 * Finances of a company; %Window numbers:
	 *   - #CompanyID = #CompanyWidgets
	 */
	WC_FINANCES,

	/**
	 * Income graph; %Window numbers:
	 *   - 0 = #CompanyValueWidgets
	 */
	WC_INCOME_GRAPH,

	/**
	 * Operating profit graph; %Window numbers:
	 *   - 0 = #CompanyValueWidgets
	 */
	WC_OPERATING_PROFIT,

	/**
	 * Delivered cargo graph; %Window numbers:
	 *   - 0 = #CompanyValueWidgets
	 */
	WC_DELIVERED_CARGO,

	/**
	 * Performance history graph; %Window numbers:
	 *   - 0 = #PerformanceHistoryGraphWidgets
	 */
	WC_PERFORMANCE_HISTORY,

	/**
	 * Company value graph; %Window numbers:
	 *   - 0 = #CompanyValueWidgets
	 */
	WC_COMPANY_VALUE,

	/**
	 * Company league window; %Window numbers:
	 *   - 0 = #CompanyLeagueWidgets
	 */
	WC_COMPANY_LEAGUE,

	/**
	 * Payment rates graph; %Window numbers:
	 *   - 0 = #CargoPaymentRatesWidgets
	 */
	WC_PAYMENT_RATES,

	/**
	 * Performance detail window; %Window numbers:
	 *   - 0 = #PerformanceRatingDetailsWidgets
	 */
	WC_PERFORMANCE_DETAIL,

	/**
	 * Company infrastructure overview; %Window numbers:
	 *   - #CompanyID = #CompanyInfrastructureWidgets
	 */
	WC_COMPANY_INFRASTRUCTURE,


	/**
	 * Buyout company (merger); %Window numbers:
	 *   - #CompanyID = #BuyCompanyWidgets
	 */
	WC_BUY_COMPANY,

	/**
	 * Engine preview window; %Window numbers:
	 *   - #EngineID = #EnginePreviewWidgets
	 */
	WC_ENGINE_PREVIEW,


	/**
	 * Music window; %Window numbers:
	 *   - 0 = #MusicWidgets
	 */
	WC_MUSIC_WINDOW,

	/**
	 * Music track selection; %Window numbers:
	 *   - 0 = MusicTrackSelectionWidgets
	 */
	WC_MUSIC_TRACK_SELECTION,

	/**
	 * Game options window; %Window numbers:
	 *   - #WN_GAME_OPTIONS_AI = #AIConfigWidgets
	 *   - #WN_GAME_OPTIONS_ABOUT = #AboutWidgets
	 *   - #WN_GAME_OPTIONS_NEWGRF_STATE = #NewGRFStateWidgets
	 *   - #WN_GAME_OPTIONS_GAME_OPTIONS = #GameOptionsWidgets
	 *   - #WN_GAME_OPTIONS_GAME_SETTINGS = #GameSettingsWidgets
	 */
	WC_GAME_OPTIONS,

	/**
	 * Custom currency; %Window numbers:
	 *   - 0 = #CustomCurrencyWidgets
	 */
	WC_CUSTOM_CURRENCY,

	/**
	 * Cheat window; %Window numbers:
	 *   - 0 = #CheatWidgets
	 */
	WC_CHEATS,

	/**
	 * Extra viewport; %Window numbers:
	 *   - Ascending value = #ExtraViewportWidgets
	 */
	WC_EXTRA_VIEWPORT,


	/**
	 * Console; %Window numbers:
	 *   - 0 = #ConsoleWidgets
	 */
	WC_CONSOLE,

	/**
	 * Bootstrap; %Window numbers:
	 *   - 0 = #BootstrapBackgroundWidgets
	 */
	WC_BOOTSTRAP,

	/**
	 * Highscore; %Window numbers:
	 *   - 0 = #HighscoreWidgets
	 */
	WC_HIGHSCORE,

	/**
	 * Endscreen; %Window numbers:
	 *   - 0 = #HighscoreWidgets
	 */
	WC_ENDSCREEN,


	/**
	 * AI debug window; %Window numbers:
	 *   - 0 = #AIDebugWidgets
	 */
	WC_AI_DEBUG,

	/**
	 * NewGRF inspect (debug); %Window numbers:
	 *   - Packed value = #NewGRFInspectWidgets
	 */
	WC_NEWGRF_INSPECT,

	/**
	 * Sprite aligner (debug); %Window numbers:
	 *   - 0 = #SpriteAlignerWidgets
	 */
	WC_SPRITE_ALIGNER,

	/**
	 * Linkgraph legend; %Window numbers:
	 *   - 0 = #LinkGraphWidgets
	 */
	WC_LINKGRAPH_LEGEND,

	/**
	 * Save preset; %Window numbers:
	 *   - 0 = #SavePresetWidgets
	 */
	WC_SAVE_PRESET,

	/**
	 * Framerate display; %Window numbers:
	 *   - 0 = #FramerateDisplayWidgets
	 */
	WC_FRAMERATE_DISPLAY,

	/**
	 * Frame time graph; %Window numbers:
	 *   - 0 = #FrametimeGraphWindowWidgets
	 */
	WC_FRAMETIME_GRAPH,

	/**
	 * Screenshot window; %Window numbers:
	 *   - 0 = #ScreenshotWidgets
	 */
	WC_SCREENSHOT,

	WC_INVALID = 0xFFFF, ///< Invalid window.
};

/** Data value for #Window::OnInvalidateData() of windows with class #WC_GAME_OPTIONS. */
enum GameOptionsInvalidationData {
	GOID_DEFAULT = 0,
	GOID_NEWGRF_RESCANNED,     ///< NewGRFs were just rescanned.
	GOID_NEWGRF_LIST_EDITED,   ///< List of active NewGRFs is being edited.
	GOID_NEWGRF_PRESET_LOADED, ///< A NewGRF preset was picked.
};

struct Window;

/** Number to differentiate different windows of the same class */
typedef int32 WindowNumber;

/** State of handling an event. */
enum EventState {
	ES_HANDLED,     ///< The passed event is handled.
	ES_NOT_HANDLED, ///< The passed event is not handled.
};

#endif /* WINDOW_TYPE_H */
