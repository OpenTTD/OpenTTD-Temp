/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file settings_type.h Types related to global configuration settings. */

#ifndef SETTINGS_TYPE_H
#define SETTINGS_TYPE_H

#include "date_type.h"
#include "economy_type.h"
#include "town_type.h"
#include "transport_type.h"
#include "network/core/config.h"
#include "company_type.h"
#include "cargotype.h"
#include "linkgraph/linkgraph_type.h"
#include "zoom_type.h"
#include "openttd.h"


/** Settings profiles and highscore tables. */
enum SettingsProfile {
	SP_BEGIN = 0,
	SP_EASY = SP_BEGIN,                       ///< Easy difficulty.
	SP_MEDIUM,                                ///< Medium difficulty.
	SP_HARD,                                  ///< Hard difficulty.

	SP_END,                                   ///< End of setting profiles.

	SP_CUSTOM = SP_END,                       ///< No profile, special "custom" highscore.
	SP_SAVED_HIGHSCORE_END,                   ///< End of saved highscore tables.

	SP_MULTIPLAYER = SP_SAVED_HIGHSCORE_END,  ///< Special "multiplayer" highscore. Not saved, always specific to the current game.
	SP_HIGHSCORE_END,                         ///< End of highscore tables.
};

/** Available industry map generation densities. */
enum IndustryDensity {
	ID_FUND_ONLY, ///< The game does not build industries.
	ID_MINIMAL,   ///< Start with just the industries that must be present.
	ID_VERY_LOW,  ///< Very few industries at game start.
	ID_LOW,       ///< Few industries at game start.
	ID_NORMAL,    ///< Normal amount of industries at game start.
	ID_HIGH,      ///< Many industries at game start.

	ID_END,       ///< Number of industry density settings.
};

/** Settings related to the difficulty of the game */
struct DifficultySettings {
	byte   max_no_competitors;               ///< the number of competitors (AIs)
	byte   number_towns;                     ///< the amount of towns
	byte   industry_density;                 ///< The industry density. @see IndustryDensity
	uint32 max_loan;                         ///< the maximum initial loan
	byte   initial_interest;                 ///< amount of interest (to pay over the loan)
	byte   vehicle_costs;                    ///< amount of money spent on vehicle running cost
	byte   competitor_speed;                 ///< the speed at which the AI builds
	byte   vehicle_breakdowns;               ///< likelihood of vehicles breaking down
	byte   subsidy_multiplier;               ///< amount of subsidy
	byte   construction_cost;                ///< how expensive is building
	byte   terrain_type;                     ///< the mountainousness of the landscape
	byte   quantity_sea_lakes;               ///< the amount of seas/lakes
	bool   economy;                          ///< how volatile is the economy
	bool   line_reverse_mode;                ///< reversing at stations or not
	bool   disasters;                        ///< are disasters enabled
	byte   town_council_tolerance;           ///< minimum required town ratings to be allowed to demolish stuff
};

/** Settings relating to viewport/smallmap scrolling. */
enum ViewportScrollMode {
	VSM_VIEWPORT_RMB_FIXED, ///< Viewport moves with mouse movement on holding right mouse button, cursor position is fixed.
	VSM_MAP_RMB_FIXED,      ///< Map moves with mouse movement on holding right mouse button, cursor position is fixed.
	VSM_MAP_RMB,            ///< Map moves with mouse movement on holding right mouse button, cursor moves.
	VSM_MAP_LMB,            ///< Map moves with mouse movement on holding left mouse button, cursor moves.
	VSM_END,                ///< Number of scroll mode settings.
};

/** Settings related to the GUI and other stuff that is not saved in the savegame. */
struct GUISettings {
	bool   sg_full_load_any;                 ///< new full load calculation, any cargo must be full read from pre v93 savegames
	bool   lost_vehicle_warn;                ///< if a vehicle can't find its destination, show a warning
	uint8  order_review_system;              ///< perform order reviews on vehicles
	bool   vehicle_income_warn;              ///< if a vehicle isn't generating income, show a warning
	bool   show_finances;                    ///< show finances at end of year
	bool   sg_new_nonstop;                   ///< ttdpatch compatible nonstop handling read from pre v93 savegames
	bool   new_nonstop;                      ///< ttdpatch compatible nonstop handling
	uint8  stop_location;                    ///< what is the default stop location of trains?
	uint8  auto_scrolling;                   ///< scroll when moving mouse to the edge (see #ViewportAutoscrolling)
	byte   errmsg_duration;                  ///< duration of error message
	uint16 hover_delay_ms;                   ///< time required to activate a hover event, in milliseconds
	bool   link_terraform_toolbar;           ///< display terraform toolbar when displaying rail, road, water and airport toolbars
	uint8  smallmap_land_colour;             ///< colour used for land and heightmap at the smallmap
	uint8  scroll_mode;                      ///< viewport scroll mode
	bool   smooth_scroll;                    ///< smooth scroll viewports
	bool   measure_tooltip;                  ///< show a permanent tooltip when dragging tools
	byte   liveries;                         ///< options for displaying company liveries, 0=none, 1=self, 2=all
	bool   prefer_teamchat;                  ///< choose the chat message target with \<ENTER\>, true=all clients, false=your team
	uint8  advanced_vehicle_list;            ///< use the "advanced" vehicle list
	uint8  loading_indicators;               ///< show loading indicators
	uint8  default_rail_type;                ///< the default rail type for the rail GUI
	uint8  toolbar_pos;                      ///< position of toolbars, 0=left, 1=center, 2=right
	uint8  statusbar_pos;                    ///< position of statusbar, 0=left, 1=center, 2=right
	uint8  window_snap_radius;               ///< windows snap at each other if closer than this
	uint8  window_soft_limit;                ///< soft limit of maximum number of non-stickied non-vital windows (0 = no limit)
	ZoomLevel zoom_min;                      ///< minimum zoom out level
	ZoomLevel zoom_max;                      ///< maximum zoom out level
	bool   disable_unsuitable_building;      ///< disable infrastructure building when no suitable vehicles are available
	byte   autosave;                         ///< how often should we do autosaves?
	bool   threaded_saves;                   ///< should we do threaded saves?
	bool   keep_all_autosave;                ///< name the autosave in a different way
	bool   autosave_on_exit;                 ///< save an autosave when you quit the game, but do not ask "Do you really want to quit?"
	bool   autosave_on_network_disconnect;   ///< save an autosave when you get disconnected from a network game with an error?
	uint8  date_format_in_default_names;     ///< should the default savegame/screenshot name use long dates (31th Dec 2008), short dates (31-12-2008) or ISO dates (2008-12-31)
	byte   max_num_autosaves;                ///< controls how many autosavegames are made before the game starts to overwrite (names them 0 to max_num_autosaves - 1)
	bool   population_in_label;              ///< show the population of a town in his label?
	uint8  right_mouse_btn_emulation;        ///< should we emulate right mouse clicking?
	uint8  scrollwheel_scrolling;            ///< scrolling using the scroll wheel?
	uint8  scrollwheel_multiplier;           ///< how much 'wheel' per incoming event from the OS?
	bool   timetable_arrival_departure;      ///< show arrivals and departures in vehicle timetables
	bool   right_mouse_wnd_close;            ///< close window with right click
	bool   pause_on_newgame;                 ///< whether to start new games paused or not
	bool   enable_signal_gui;                ///< show the signal GUI when the signal button is pressed
	Year   coloured_news_year;               ///< when does newspaper become coloured?
	bool   timetable_in_ticks;               ///< whether to show the timetable in ticks rather than days
	bool   quick_goto;                       ///< Allow quick access to 'goto button' in vehicle orders window
	bool   auto_euro;                        ///< automatically switch to euro in 2002
	byte   drag_signals_density;             ///< many signals density
	bool   drag_signals_fixed_distance;      ///< keep fixed distance between signals when dragging
	Year   semaphore_build_before;           ///< build semaphore signals automatically before this year
	byte   news_message_timeout;             ///< how much longer than the news message "age" should we keep the message in the history
	bool   show_track_reservation;           ///< highlight reserved tracks.
	uint8  default_signal_type;              ///< the signal type to build by default.
	uint8  cycle_signal_types;               ///< what signal types to cycle with the build signal tool.
	byte   station_numtracks;                ///< the number of platforms to default on for rail stations
	byte   station_platlength;               ///< the platform length, in tiles, for rail stations
	bool   station_dragdrop;                 ///< whether drag and drop is enabled for stations
	bool   station_show_coverage;            ///< whether to highlight coverage area
	bool   persistent_buildingtools;         ///< keep the building tools active after usage
	bool   expenses_layout;                  ///< layout of expenses window
	uint32 last_newgrf_count;                ///< the numbers of NewGRFs we found during the last scan
	byte   missing_strings_threshold;        ///< the number of missing strings before showing the warning
	uint8  graph_line_thickness;             ///< the thickness of the lines in the various graph guis
	uint8  osk_activation;                   ///< Mouse gesture to trigger the OSK.
	byte   starting_colour;                  ///< default color scheme for the company to start a new game with
	bool   show_newgrf_name;                 ///< Show the name of the NewGRF in the build vehicle window

	uint16 console_backlog_timeout;          ///< the minimum amount of time items should be in the console backlog before they will be removed in ~3 seconds granularity.
	uint16 console_backlog_length;           ///< the minimum amount of items in the console backlog before items will be removed.

	uint8  station_gui_group_order;          ///< the order of grouping cargo entries in the station gui
	uint8  station_gui_sort_by;              ///< sort cargo entries in the station gui by station name or amount
	uint8  station_gui_sort_order;           ///< the sort order of entries in the station gui - ascending or descending
	uint16 network_chat_box_width_pct;       ///< width of the chat box in percent
	uint8  network_chat_box_height;          ///< height of the chat box in lines
	uint16 network_chat_timeout;             ///< timeout of chat messages in seconds

	uint8  developer;                        ///< print non-fatal warnings in console (>= 1), copy debug output to console (== 2)
	bool   show_date_in_logs;                ///< whether to show dates in console logs
	bool   newgrf_developer_tools;           ///< activate NewGRF developer tools and allow modifying NewGRFs in an existing game
	bool   ai_developer_tools;               ///< activate AI developer tools
	bool   scenario_developer;               ///< activate scenario developer: allow modifying NewGRFs in an existing game
	uint8  settings_restriction_mode;        ///< selected restriction mode in adv. settings GUI. @see RestrictionMode
	bool   newgrf_show_old_versions;         ///< whether to show old versions in the NewGRF list
	uint8  newgrf_default_palette;           ///< default palette to use for NewGRFs without action 14 palette information

	/**
	 * Returns true when the user has sufficient privileges to edit newgrfs on a running game
	 * @return whether the user has sufficient privileges to edit newgrfs in an existing game
	 */
	bool UserIsAllowedToChangeNewGRFs() const
	{
		return this->scenario_developer || this->newgrf_developer_tools;
	}
};

/** Settings related to sound effects. */
struct SoundSettings {
	bool   news_ticker;                      ///< Play a ticker sound when a news item is published.
	bool   news_full;                        ///< Play sound effects associated to certain news types.
	bool   new_year;                         ///< Play sound on new year, summarising the performance during the last year.
	bool   confirm;                          ///< Play sound effect on successful constructions or other actions.
	bool   click_beep;                       ///< Beep on a random selection of buttons.
	bool   disaster;                         ///< Play disaster and accident sounds.
	bool   vehicle;                          ///< Play vehicle sound effects.
	bool   ambient;                          ///< Play ambient, industry and town sounds.
};

/** Settings related to music. */
struct MusicSettings {
	byte playlist;     ///< The playlist (number) to play
	byte music_vol;    ///< The requested music volume
	byte effect_vol;   ///< The requested effects volume
	byte custom_1[33]; ///< The order of the first custom playlist
	byte custom_2[33]; ///< The order of the second custom playlist
	bool playing;      ///< Whether music is playing
	bool shuffle;      ///< Whether to shuffle the music
};

/** Settings related to currency/unit systems. */
struct LocaleSettings {
	byte   currency;                         ///< currency we currently use
	byte   units_velocity;                   ///< unit system for velocity
	byte   units_power;                      ///< unit system for power
	byte   units_weight;                     ///< unit system for weight
	byte   units_volume;                     ///< unit system for volume
	byte   units_force;                      ///< unit system for force
	byte   units_height;                     ///< unit system for height
	char  *digit_group_separator;            ///< thousand separator for non-currencies
	char  *digit_group_separator_currency;   ///< thousand separator for currencies
	char  *digit_decimal_separator;          ///< decimal separator
};

/** Settings related to news */
struct NewsSettings {
	uint8 arrival_player;                                 ///< NewsDisplay of vehicles arriving at new stations of current player
	uint8 arrival_other;                                  ///< NewsDisplay of vehicles arriving at new stations of other players
	uint8 accident;                                       ///< NewsDisplay of accidents that occur
	uint8 company_info;                                   ///< NewsDisplay of general company information
	uint8 open;                                           ///< NewsDisplay on new industry constructions
	uint8 close;                                          ///< NewsDisplay about closing industries
	uint8 economy;                                        ///< NewsDisplay on economical changes
	uint8 production_player;                              ///< NewsDisplay of production changes of industries affecting current player
	uint8 production_other;                               ///< NewsDisplay of production changes of industries affecting competitors
	uint8 production_nobody;                              ///< NewsDisplay of production changes of industries affecting no one
	uint8 advice;                                         ///< NewsDisplay on advice affecting the player's vehicles
	uint8 new_vehicles;                                   ///< NewsDisplay of new vehicles becoming available
	uint8 acceptance;                                     ///< NewsDisplay on changes affecting the acceptance of cargo at stations
	uint8 subsidies;                                      ///< NewsDisplay of changes on subsidies
	uint8 general;                                        ///< NewsDisplay of other topics
};

/** All settings related to the network. */
struct NetworkSettings {
	uint16 sync_freq;                                     ///< how often do we check whether we are still in-sync
	uint8  frame_freq;                                    ///< how often do we send commands to the clients
	uint16 commands_per_frame;                            ///< how many commands may be sent each frame_freq frames?
	uint16 max_commands_in_queue;                         ///< how many commands may there be in the incoming queue before dropping the connection?
	uint16 bytes_per_frame;                               ///< how many bytes may, over a long period, be received per frame?
	uint16 bytes_per_frame_burst;                         ///< how many bytes may, over a short period, be received?
	uint16 max_init_time;                                 ///< maximum amount of time, in game ticks, a client may take to initiate joining
	uint16 max_join_time;                                 ///< maximum amount of time, in game ticks, a client may take to sync up during joining
	uint16 max_download_time;                             ///< maximum amount of time, in game ticks, a client may take to download the map
	uint16 max_password_time;                             ///< maximum amount of time, in game ticks, a client may take to enter the password
	uint16 max_lag_time;                                  ///< maximum amount of time, in game ticks, a client may be lagging behind the server
	bool   pause_on_join;                                 ///< pause the game when people join
	uint16 server_port;                                   ///< port the server listens on
	uint16 server_admin_port;                             ///< port the server listens on for the admin network
	bool   server_admin_chat;                             ///< allow private chat for the server to be distributed to the admin network
	char   server_name[NETWORK_NAME_LENGTH];              ///< name of the server
	char   server_password[NETWORK_PASSWORD_LENGTH];      ///< password for joining this server
	char   rcon_password[NETWORK_PASSWORD_LENGTH];        ///< password for rconsole (server side)
	char   admin_password[NETWORK_PASSWORD_LENGTH];       ///< password for the admin network
	bool   server_advertise;                              ///< advertise the server to the masterserver
	uint8  lan_internet;                                  ///< search on the LAN or internet for servers
	char   client_name[NETWORK_CLIENT_NAME_LENGTH];       ///< name of the player (as client)
	char   default_company_pass[NETWORK_PASSWORD_LENGTH]; ///< default password for new companies in encrypted form
	bool   company_autoprotect;                           ///< whether to protect new companies with pubkey
	char   connect_to_ip[NETWORK_HOSTNAME_LENGTH];        ///< default for the "Add server" query
	char   network_id[NETWORK_SERVER_ID_LENGTH];          ///< network ID for servers
	bool   autoclean_companies;                           ///< automatically remove companies that are not in use
	uint8  autoclean_unprotected;                         ///< remove passwordless companies after this many months
	uint8  autoclean_protected;                           ///< remove the password from passworded companies after this many months
	uint8  autoclean_novehicles;                          ///< remove companies with no vehicles after this many months
	uint8  max_companies;                                 ///< maximum amount of companies
	uint8  max_clients;                                   ///< maximum amount of clients
	uint8  max_spectators;                                ///< maximum amount of spectators
	Year   restart_game_year;                             ///< year the server restarts
	uint8  min_active_clients;                            ///< minimum amount of active clients to unpause the game
	uint8  server_lang;                                   ///< language of the server
	bool   reload_cfg;                                    ///< reload the config file before restarting
	char   last_host[NETWORK_HOSTNAME_LENGTH];            ///< IP address of the last joined server
	uint16 last_port;                                     ///< port of the last joined server
	bool   no_http_content_downloads;                     ///< do not do content downloads over HTTP
};

/** Settings related to the creation of games. */
struct GameCreationSettings {
	uint32 generation_seed;                  ///< noise seed for world generation
	Year   starting_year;                    ///< starting date
	Year   ending_year;                      ///< scoring end date
	uint8  map_x;                            ///< X size of map
	uint8  map_y;                            ///< Y size of map
	byte   land_generator;                   ///< the landscape generator
	byte   oil_refinery_limit;               ///< distance oil refineries allowed from map edge
	byte   snow_line_height;                 ///< the configured snow line height
	byte   tgen_smoothness;                  ///< how rough is the terrain from 0-3
	byte   tree_placer;                      ///< the tree placer algorithm
	byte   heightmap_rotation;               ///< rotation director for the heightmap
	byte   se_flat_world_height;             ///< land height a flat world gets in SE
	byte   town_name;                        ///< the town name generator used for town names
	byte   landscape;                        ///< the landscape we're currently in
	byte   water_borders;                    ///< bitset of the borders that are water
	uint16 custom_town_number;               ///< manually entered number of towns
	byte   variety;                          ///< variety level applied to TGP
	byte   custom_sea_level;                 ///< manually entered percentage of water in the map
	byte   min_river_length;                 ///< the minimum river length
	byte   river_route_random;               ///< the amount of randomicity for the route finding
	byte   amount_of_rivers;                 ///< the amount of rivers
};

/** Settings related to construction in-game */
struct ConstructionSettings {
	uint8  max_heightlevel;                  ///< maximum allowed heightlevel
	bool   build_on_slopes;                  ///< allow building on slopes
	bool   autoslope;                        ///< allow terraforming under things
	uint16 max_bridge_length;                ///< maximum length of bridges
	byte   max_bridge_height;                ///< maximum height of bridges
	uint16 max_tunnel_length;                ///< maximum length of tunnels
	byte   train_signal_side;                ///< show signals on left / driving / right side
	bool   extra_dynamite;                   ///< extra dynamite
	bool   road_stop_on_town_road;           ///< allow building of drive-through road stops on town owned roads
	bool   road_stop_on_competitor_road;     ///< allow building of drive-through road stops on roads owned by competitors
	uint8  raw_industry_construction;        ///< type of (raw) industry construction (none, "normal", prospecting)
	uint8  industry_platform;                ///< the amount of flat land around an industry
	bool   freeform_edges;                   ///< allow terraforming the tiles at the map edges
	uint8  extra_tree_placement;             ///< (dis)allow building extra trees in-game
	uint8  command_pause_level;              ///< level/amount of commands that can't be executed while paused

	uint32 terraform_per_64k_frames;         ///< how many tile heights may, over a long period, be terraformed per 65536 frames?
	uint16 terraform_frame_burst;            ///< how many tile heights may, over a short period, be terraformed?
	uint32 clear_per_64k_frames;             ///< how many tiles may, over a long period, be cleared per 65536 frames?
	uint16 clear_frame_burst;                ///< how many tiles may, over a short period, be cleared?
	uint32 tree_per_64k_frames;              ///< how many trees may, over a long period, be planted per 65536 frames?
	uint16 tree_frame_burst;                 ///< how many trees may, over a short period, be planted?
};

/** Settings related to the AI. */
struct AISettings {
	bool   ai_in_multiplayer;                ///< so we allow AIs in multiplayer
	bool   ai_disable_veh_train;             ///< disable types for AI
	bool   ai_disable_veh_roadveh;           ///< disable types for AI
	bool   ai_disable_veh_aircraft;          ///< disable types for AI
	bool   ai_disable_veh_ship;              ///< disable types for AI
};

/** Settings related to scripts. */
struct ScriptSettings {
	uint8  settings_profile;                 ///< difficulty profile to set initial settings of scripts, esp. random AIs
	uint32 script_max_opcode_till_suspend;   ///< max opcode calls till scripts will suspend
	uint32 script_max_memory_megabytes;      ///< limit on memory a single script instance may have allocated
};

/** Settings related to the new pathfinder. */
struct NPFSettings {
	/**
	 * The maximum amount of search nodes a single NPF run should take. This
	 * limit should make sure performance stays at acceptable levels at the cost
	 * of not being perfect anymore.
	 */
	uint32 npf_max_search_nodes;
	uint32 maximum_go_to_depot_penalty;      ///< What is the maximum penalty that may be endured for going to a depot

	uint32 npf_rail_firstred_penalty;        ///< the penalty for when the first signal is red (and it is not an exit or combo signal)
	uint32 npf_rail_firstred_exit_penalty;   ///< the penalty for when the first signal is red (and it is an exit or combo signal)
	uint32 npf_rail_lastred_penalty;         ///< the penalty for when the last signal is red
	uint32 npf_rail_station_penalty;         ///< the penalty for station tiles
	uint32 npf_rail_slope_penalty;           ///< the penalty for sloping upwards
	uint32 npf_rail_curve_penalty;           ///< the penalty for curves
	uint32 npf_rail_depot_reverse_penalty;   ///< the penalty for reversing in depots
	uint32 npf_rail_pbs_cross_penalty;       ///< the penalty for crossing a reserved rail track
	uint32 npf_rail_pbs_signal_back_penalty; ///< the penalty for passing a pbs signal from the backside
	uint32 npf_buoy_penalty;                 ///< the penalty for going over (through) a buoy
	uint32 npf_water_curve_penalty;          ///< the penalty for curves
	uint32 npf_road_curve_penalty;           ///< the penalty for curves
	uint32 npf_crossing_penalty;             ///< the penalty for level crossings
	uint32 npf_road_drive_through_penalty;   ///< the penalty for going through a drive-through road stop
	uint32 npf_road_dt_occupied_penalty;     ///< the penalty multiplied by the fill percentage of a drive-through road stop
	uint32 npf_road_bay_occupied_penalty;    ///< the penalty multiplied by the fill percentage of a road bay
};

/** Settings related to the yet another pathfinder. */
struct YAPFSettings {
	bool   disable_node_optimization;        ///< whether to use exit-dir instead of trackdir in node key
	uint32 max_search_nodes;                 ///< stop path-finding when this number of nodes visited
	uint32 maximum_go_to_depot_penalty;      ///< What is the maximum penalty that may be endured for going to a depot
	bool   ship_use_yapf;                    ///< use YAPF for ships
	bool   road_use_yapf;                    ///< use YAPF for road
	bool   rail_use_yapf;                    ///< use YAPF for rail
	uint32 road_slope_penalty;               ///< penalty for up-hill slope
	uint32 road_curve_penalty;               ///< penalty for curves
	uint32 road_crossing_penalty;            ///< penalty for level crossing
	uint32 road_stop_penalty;                ///< penalty for going through a drive-through road stop
	uint32 road_stop_occupied_penalty;       ///< penalty multiplied by the fill percentage of a drive-through road stop
	uint32 road_stop_bay_occupied_penalty;   ///< penalty multiplied by the fill percentage of a road bay
	bool   rail_firstred_twoway_eol;         ///< treat first red two-way signal as dead end
	uint32 rail_firstred_penalty;            ///< penalty for first red signal
	uint32 rail_firstred_exit_penalty;       ///< penalty for first red exit signal
	uint32 rail_lastred_penalty;             ///< penalty for last red signal
	uint32 rail_lastred_exit_penalty;        ///< penalty for last red exit signal
	uint32 rail_station_penalty;             ///< penalty for non-target station tile
	uint32 rail_slope_penalty;               ///< penalty for up-hill slope
	uint32 rail_curve45_penalty;             ///< penalty for curve
	uint32 rail_curve90_penalty;             ///< penalty for 90-deg curve
	uint32 rail_depot_reverse_penalty;       ///< penalty for reversing in the depot
	uint32 rail_crossing_penalty;            ///< penalty for level crossing
	uint32 rail_look_ahead_max_signals;      ///< max. number of signals taken into consideration in look-ahead load balancer
	int32  rail_look_ahead_signal_p0;        ///< constant in polynomial penalty function
	int32  rail_look_ahead_signal_p1;        ///< constant in polynomial penalty function
	int32  rail_look_ahead_signal_p2;        ///< constant in polynomial penalty function
	uint32 rail_pbs_cross_penalty;           ///< penalty for crossing a reserved tile
	uint32 rail_pbs_station_penalty;         ///< penalty for crossing a reserved station tile
	uint32 rail_pbs_signal_back_penalty;     ///< penalty for passing a pbs signal from the backside
	uint32 rail_doubleslip_penalty;          ///< penalty for passing a double slip switch

	uint32 rail_longer_platform_penalty;           ///< penalty for longer  station platform than train
	uint32 rail_longer_platform_per_tile_penalty;  ///< penalty for longer  station platform than train (per tile)
	uint32 rail_shorter_platform_penalty;          ///< penalty for shorter station platform than train
	uint32 rail_shorter_platform_per_tile_penalty; ///< penalty for shorter station platform than train (per tile)
	uint32 ship_curve45_penalty;                   ///< penalty for 45-deg curve for ships
	uint32 ship_curve90_penalty;                   ///< penalty for 90-deg curve for ships
};

/** Settings related to all pathfinders. */
struct PathfinderSettings {
	uint8  pathfinder_for_trains;            ///< the pathfinder to use for trains
	uint8  pathfinder_for_roadvehs;          ///< the pathfinder to use for roadvehicles
	uint8  pathfinder_for_ships;             ///< the pathfinder to use for ships
	bool   new_pathfinding_all;              ///< use the newest pathfinding algorithm for all

	bool   roadveh_queue;                    ///< buggy road vehicle queueing
	bool   forbid_90_deg;                    ///< forbid trains to make 90 deg turns

	bool   reverse_at_signals;               ///< whether to reverse at signals at all
	byte   wait_oneway_signal;               ///< waitingtime in days before a oneway signal
	byte   wait_twoway_signal;               ///< waitingtime in days before a twoway signal

	bool   reserve_paths;                    ///< always reserve paths regardless of signal type.
	byte   wait_for_pbs_path;                ///< how long to wait for a path reservation.
	byte   path_backoff_interval;            ///< ticks between checks for a free path.

	NPFSettings  npf;                        ///< pathfinder settings for the new pathfinder
	YAPFSettings yapf;                       ///< pathfinder settings for the yet another pathfinder
};

/** Settings related to orders. */
struct OrderSettings {
	bool   improved_load;                    ///< improved loading algorithm
	bool   gradual_loading;                  ///< load vehicles gradually
	bool   selectgoods;                      ///< only send the goods to station if a train has been there
	bool   no_servicing_if_no_breakdowns;    ///< don't send vehicles to depot when breakdowns are disabled
	bool   serviceathelipad;                 ///< service helicopters at helipads automatically (no need to send to depot)
};

/** Settings related to vehicles. */
struct VehicleSettings {
	uint8  max_train_length;                 ///< maximum length for trains
	uint8  smoke_amount;                     ///< amount of smoke/sparks locomotives produce
	uint8  train_acceleration_model;         ///< realistic acceleration for trains
	uint8  roadveh_acceleration_model;       ///< realistic acceleration for road vehicles
	uint8  train_slope_steepness;            ///< Steepness of hills for trains when using realistic acceleration
	uint8  roadveh_slope_steepness;          ///< Steepness of hills for road vehicles when using realistic acceleration
	bool   wagon_speed_limits;               ///< enable wagon speed limits
	bool   disable_elrails;                  ///< when true, the elrails are disabled
	UnitID max_trains;                       ///< max trains in game per company
	UnitID max_roadveh;                      ///< max trucks in game per company
	UnitID max_aircraft;                     ///< max planes in game per company
	UnitID max_ships;                        ///< max ships in game per company
	uint8  plane_speed;                      ///< divisor for speed of aircraft
	uint8  freight_trains;                   ///< value to multiply the weight of cargo by
	bool   dynamic_engines;                  ///< enable dynamic allocation of engine data
	bool   never_expire_vehicles;            ///< never expire vehicles
	byte   extend_vehicle_life;              ///< extend vehicle life by this many years
	byte   road_side;                        ///< the side of the road vehicles drive on
	uint8  plane_crashes;                    ///< number of plane crashes, 0 = none, 1 = reduced, 2 = normal
};

/** Settings related to the economy. */
struct EconomySettings {
	bool   inflation;                        ///< disable inflation
	bool   bribe;                            ///< enable bribing the local authority
	EconomyType type;                        ///< economy type (original/smooth/frozen)
	bool   allow_shares;                     ///< allow the buying/selling of shares
	uint8  min_years_for_shares;             ///< minimum age of a company for it to trade shares
	uint8  feeder_payment_share;             ///< percentage of leg payment to virtually pay in feeder systems
	byte   dist_local_authority;             ///< distance for town local authority, default 20
	bool   exclusive_rights;                 ///< allow buying exclusive rights
	bool   fund_buildings;                   ///< allow funding new buildings
	bool   fund_roads;                       ///< allow funding local road reconstruction
	bool   give_money;                       ///< allow giving other companies money
	bool   mod_road_rebuild;                 ///< roadworks remove unnecessary RoadBits
	bool   multiple_industry_per_town;       ///< allow many industries of the same type per town
	uint8  town_growth_rate;                 ///< town growth rate
	uint8  larger_towns;                     ///< the number of cities to build. These start off larger and grow twice as fast
	uint8  initial_city_size;                ///< multiplier for the initial size of the cities compared to towns
	TownLayout town_layout;                  ///< select town layout, @see TownLayout
	TownCargoGenMode town_cargogen_mode;     ///< algorithm for generating cargo from houses, @see TownCargoGenMode
	bool   allow_town_roads;                 ///< towns are allowed to build roads (always allowed when generating world / in SE)
	TownFounding found_town;                 ///< town founding.
	bool   station_noise_level;              ///< build new airports when the town noise level is still within accepted limits
	uint16 town_noise_population[3];         ///< population to base decision on noise evaluation (@see town_council_tolerance)
	bool   allow_town_level_crossings;       ///< towns are allowed to build level crossings
	bool   infrastructure_maintenance;       ///< enable monthly maintenance fee for owner infrastructure
};

struct LinkGraphSettings {
	uint16 recalc_time;                     ///< time (in days) for recalculating each link graph component.
	uint16 recalc_interval;                 ///< time (in days) between subsequent checks for link graphs to be calculated.
	DistributionType distribution_pax;      ///< distribution type for passengers
	DistributionType distribution_mail;     ///< distribution type for mail
	DistributionType distribution_armoured; ///< distribution type for armoured cargo class
	DistributionType distribution_default;  ///< distribution type for all other goods
	uint8 accuracy;                         ///< accuracy when calculating things on the link graph. low accuracy => low running time
	uint8 demand_size;                      ///< influence of supply ("station size") on the demand function
	uint8 demand_distance;                  ///< influence of distance between stations on the demand function
	uint8 short_path_saturation;            ///< percentage up to which short paths are saturated before saturating most capacious paths

	inline DistributionType GetDistributionType(CargoID cargo) const {
		if (IsCargoInClass(cargo, CC_PASSENGERS)) return this->distribution_pax;
		if (IsCargoInClass(cargo, CC_MAIL)) return this->distribution_mail;
		if (IsCargoInClass(cargo, CC_ARMOURED)) return this->distribution_armoured;
		return this->distribution_default;
	}
};

/** Settings related to stations. */
struct StationSettings {
	bool   modified_catchment;               ///< different-size catchment areas
	bool   serve_neutral_industries;         ///< company stations can serve industries with attached neutral stations
	bool   adjacent_stations;                ///< allow stations to be built directly adjacent to other stations
	bool   distant_join_stations;            ///< allow to join non-adjacent stations
	bool   never_expire_airports;            ///< never expire airports
	byte   station_spread;                   ///< amount a station may spread
};

/** Default settings for vehicles. */
struct VehicleDefaultSettings {
	bool   servint_ispercent;                ///< service intervals are in percents
	uint16 servint_trains;                   ///< service interval for trains
	uint16 servint_roadveh;                  ///< service interval for road vehicles
	uint16 servint_aircraft;                 ///< service interval for aircraft
	uint16 servint_ships;                    ///< service interval for ships
};

/** Settings that can be set per company. */
struct CompanySettings {
	bool engine_renew;                       ///< is autorenew enabled
	int16 engine_renew_months;               ///< months before/after the maximum vehicle age a vehicle should be renewed
	uint32 engine_renew_money;               ///< minimum amount of money before autorenew is used
	bool renew_keep_length;                  ///< sell some wagons if after autoreplace the train is longer than before
	VehicleDefaultSettings vehicle;          ///< default settings for vehicles
};

/** All settings together for the game. */
struct GameSettings {
	DifficultySettings   difficulty;         ///< settings related to the difficulty
	GameCreationSettings game_creation;      ///< settings used during the creation of a game (map)
	ConstructionSettings construction;       ///< construction of things in-game
	AISettings           ai;                 ///< what may the AI do?
	ScriptSettings       script;             ///< settings for scripts
	class AIConfig      *ai_config[MAX_COMPANIES]; ///< settings per company
	class GameConfig    *game_config;        ///< settings for gamescript
	PathfinderSettings   pf;                 ///< settings for all pathfinders
	OrderSettings        order;              ///< settings related to orders
	VehicleSettings      vehicle;            ///< options for vehicles
	EconomySettings      economy;            ///< settings to change the economy
	LinkGraphSettings    linkgraph;          ///< settings for link graph calculations
	StationSettings      station;            ///< settings related to station management
	LocaleSettings       locale;             ///< settings related to used currency/unit system in the current game
};

/** All settings that are only important for the local client. */
struct ClientSettings {
	GUISettings          gui;                ///< settings related to the GUI
	NetworkSettings      network;            ///< settings related to the network
	CompanySettings      company;            ///< default values for per-company settings
	SoundSettings        sound;              ///< sound effect settings
	MusicSettings        music;              ///< settings related to music/sound
	NewsSettings         news_display;       ///< news display settings.
};

/** The current settings for this game. */
extern ClientSettings _settings_client;

/** The current settings for this game. */
extern GameSettings _settings_game;

/** The settings values that are used for new games and/or modified in config file. */
extern GameSettings _settings_newgame;

/** Old vehicle settings, which were game settings before, and are company settings now. (Needed for savegame conversion) */
extern VehicleDefaultSettings _old_vds;

/**
 * Get the settings-object applicable for the current situation: the newgame settings
 * when we're in the main menu and otherwise the settings of the current game.
 */
static inline GameSettings &GetGameSettings()
{
	return (_game_mode == GM_MENU) ? _settings_newgame : _settings_game;
}

#endif /* SETTINGS_TYPE_H */
