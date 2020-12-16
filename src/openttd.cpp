/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file openttd.cpp Functions related to starting OpenTTD. */

#include "stdafx.h"

#include "blitter/factory.hpp"
#include "sound/sound_driver.hpp"
#include "music/music_driver.hpp"
#include "video/video_driver.hpp"

#include "fontcache.h"
#include "error.h"
#include "gui.h"

#include "base_media_base.h"
#include "saveload/saveload.h"
#include "company_func.h"
#include "command_func.h"
#include "news_func.h"
#include "fios.h"
#include "aircraft.h"
#include "roadveh.h"
#include "train.h"
#include "ship.h"
#include "console_func.h"
#include "screenshot.h"
#include "network/network.h"
#include "network/network_func.h"
#include "ai/ai.hpp"
#include "ai/ai_config.hpp"
#include "settings_func.h"
#include "genworld.h"
#include "progress.h"
#include "strings_func.h"
#include "date_func.h"
#include "vehicle_func.h"
#include "gamelog.h"
#include "animated_tile_func.h"
#include "roadstop_base.h"
#include "elrail_func.h"
#include "rev.h"
#include "highscore.h"
#include "station_base.h"
#include "crashlog.h"
#include "engine_func.h"
#include "core/random_func.hpp"
#include "rail_gui.h"
#include "road_gui.h"
#include "core/backup_type.hpp"
#include "hotkeys.h"
#include "newgrf.h"
#include "misc/getoptdata.h"
#include "game/game.hpp"
#include "game/game_config.hpp"
#include "town.h"
#include "subsidy_func.h"
#include "gfx_layout.h"
#include "viewport_func.h"
#include "viewport_sprite_sorter.h"
#include "framerate_type.h"
#include "industry.h"

#include "linkgraph/linkgraphschedule.h"

#include <stdarg.h>
#include <system_error>

#include "safeguards.h"

#ifdef __EMSCRIPTEN__
#	include <emscripten.h>
#	include <emscripten/html5.h>
#endif

void CallLandscapeTick();
void IncreaseDate();
void DoPaletteAnimations();
void MusicLoop();
void ResetMusic();
void CallWindowGameTickEvent();
bool HandleBootstrap();

extern Company *DoStartupNewCompany(bool is_ai, CompanyID company = INVALID_COMPANY);
extern void ShowOSErrorBox(const char *buf, bool system);
extern char *_config_file;

bool _save_config = false;

/**
 * Error handling for fatal user errors.
 * @param s the string to print.
 * @note Does NEVER return.
 */
void CDECL usererror(const char *s, ...)
{
	va_list va;
	char buf[512];

	va_start(va, s);
	vseprintf(buf, lastof(buf), s, va);
	va_end(va);

	ShowOSErrorBox(buf, false);
	if (VideoDriver::GetInstance() != nullptr) VideoDriver::GetInstance()->Stop();

#ifdef __EMSCRIPTEN__
	emscripten_exit_pointerlock();
	/* In effect, the game ends here. As emscripten_set_main_loop() caused
	 * the stack to be unwound, the code after MainLoop() in
	 * openttd_main() is never executed. */
	EM_ASM(if (window["openttd_syncfs"]) openttd_syncfs());
	EM_ASM(if (window["openttd_abort"]) openttd_abort());
#endif

	exit(1);
}

/**
 * Error handling for fatal non-user errors.
 * @param s the string to print.
 * @note Does NEVER return.
 */
void CDECL error(const char *s, ...)
{
	va_list va;
	char buf[512];

	va_start(va, s);
	vseprintf(buf, lastof(buf), s, va);
	va_end(va);

	if (VideoDriver::GetInstance() == nullptr || VideoDriver::GetInstance()->HasGUI()) {
		ShowOSErrorBox(buf, true);
	}

	/* Set the error message for the crash log and then invoke it. */
	CrashLog::SetErrorMessage(buf);
	abort();
}

/**
 * Shows some information on the console/a popup box depending on the OS.
 * @param str the text to show.
 */
void CDECL ShowInfoF(const char *str, ...)
{
	va_list va;
	char buf[1024];
	va_start(va, str);
	vseprintf(buf, lastof(buf), str, va);
	va_end(va);
	ShowInfo(buf);
}

/**
 * Show the help message when someone passed a wrong parameter.
 */
static void ShowHelp()
{
	char buf[8192];
	char *p = buf;

	p += seprintf(p, lastof(buf), "OpenTTD %s\n", _openttd_revision);
	p = strecpy(p,
		"\n"
		"\n"
		"Command line options:\n"
		"  -v drv              = Set video driver (see below)\n"
		"  -s drv              = Set sound driver (see below) (param bufsize,hz)\n"
		"  -m drv              = Set music driver (see below)\n"
		"  -b drv              = Set the blitter to use (see below)\n"
		"  -r res              = Set resolution (for instance 800x600)\n"
		"  -h                  = Display this help text\n"
		"  -t year             = Set starting year\n"
		"  -d [[fac=]lvl[,...]]= Debug mode\n"
		"  -e                  = Start Editor\n"
		"  -g [savegame]       = Start new/save game immediately\n"
		"  -G seed             = Set random seed\n"
		"  -n [ip:port#company]= Join network game\n"
		"  -p password         = Password to join server\n"
		"  -P password         = Password to join company\n"
		"  -D [ip][:port]      = Start dedicated server\n"
		"  -l ip[:port]        = Redirect DEBUG()\n"
#if !defined(_WIN32)
		"  -f                  = Fork into the background (dedicated only)\n"
#endif
		"  -I graphics_set     = Force the graphics set (see below)\n"
		"  -S sounds_set       = Force the sounds set (see below)\n"
		"  -M music_set        = Force the music set (see below)\n"
		"  -c config_file      = Use 'config_file' instead of 'openttd.cfg'\n"
		"  -x                  = Never save configuration changes to disk\n"
		"  -q savegame         = Write some information about the savegame and exit\n"
		"\n",
		lastof(buf)
	);

	/* List the graphics packs */
	p = BaseGraphics::GetSetsList(p, lastof(buf));

	/* List the sounds packs */
	p = BaseSounds::GetSetsList(p, lastof(buf));

	/* List the music packs */
	p = BaseMusic::GetSetsList(p, lastof(buf));

	/* List the drivers */
	p = DriverFactoryBase::GetDriversInfo(p, lastof(buf));

	/* List the blitters */
	p = BlitterFactory::GetBlittersInfo(p, lastof(buf));

	/* List the debug facilities. */
	p = DumpDebugFacilityNames(p, lastof(buf));

	/* We need to initialize the AI, so it finds the AIs */
	AI::Initialize();
	p = AI::GetConsoleList(p, lastof(buf), true);
	AI::Uninitialize(true);

	/* We need to initialize the GameScript, so it finds the GSs */
	Game::Initialize();
	p = Game::GetConsoleList(p, lastof(buf), true);
	Game::Uninitialize(true);

	/* ShowInfo put output to stderr, but version information should go
	 * to stdout; this is the only exception */
#if !defined(_WIN32)
	printf("%s\n", buf);
#else
	ShowInfo(buf);
#endif
}

static void WriteSavegameInfo(const char *name)
{
	extern SaveLoadVersion _sl_version;
	uint32 last_ottd_rev = 0;
	byte ever_modified = 0;
	bool removed_newgrfs = false;

	GamelogInfo(_load_check_data.gamelog_action, _load_check_data.gamelog_actions, &last_ottd_rev, &ever_modified, &removed_newgrfs);

	char buf[8192];
	char *p = buf;
	p += seprintf(p, lastof(buf), "Name:         %s\n", name);
	p += seprintf(p, lastof(buf), "Savegame ver: %d\n", _sl_version);
	p += seprintf(p, lastof(buf), "NewGRF ver:   0x%08X\n", last_ottd_rev);
	p += seprintf(p, lastof(buf), "Modified:     %d\n", ever_modified);

	if (removed_newgrfs) {
		p += seprintf(p, lastof(buf), "NewGRFs have been removed\n");
	}

	p = strecpy(p, "NewGRFs:\n", lastof(buf));
	if (_load_check_data.HasNewGrfs()) {
		for (GRFConfig *c = _load_check_data.grfconfig; c != nullptr; c = c->next) {
			char md5sum[33];
			md5sumToString(md5sum, lastof(md5sum), HasBit(c->flags, GCF_COMPATIBLE) ? c->original_md5sum : c->ident.md5sum);
			p += seprintf(p, lastof(buf), "%08X %s %s\n", c->ident.grfid, md5sum, c->filename);
		}
	}

	/* ShowInfo put output to stderr, but version information should go
	 * to stdout; this is the only exception */
#if !defined(_WIN32)
	printf("%s\n", buf);
#else
	ShowInfo(buf);
#endif
}


/**
 * Extract the resolution from the given string and store
 * it in the 'res' parameter.
 * @param res variable to store the resolution in.
 * @param s   the string to decompose.
 */
static void ParseResolution(Dimension *res, const char *s)
{
	const char *t = strchr(s, 'x');
	if (t == nullptr) {
		ShowInfoF("Invalid resolution '%s'", s);
		return;
	}

	res->width  = max(strtoul(s, nullptr, 0), 64UL);
	res->height = max(strtoul(t + 1, nullptr, 0), 64UL);
}


/**
 * Uninitializes drivers, frees allocated memory, cleans pools, ...
 * Generally, prepares the game for shutting down
 */
static void ShutdownGame()
{
	IConsoleFree();

	if (_network_available) NetworkShutDown(); // Shut down the network and close any open connections

	DriverFactoryBase::ShutdownDrivers();

	UnInitWindowSystem();

	/* stop the scripts */
	AI::Uninitialize(false);
	Game::Uninitialize(false);

	/* Uninitialize variables that are allocated dynamically */
	GamelogReset();

	free(_config_file);

	LinkGraphSchedule::Clear();
	PoolBase::Clean(PT_ALL);

	/* No NewGRFs were loaded when it was still bootstrapping. */
	if (_game_mode != GM_BOOTSTRAP) ResetNewGRFData();

	/* Close all and any open filehandles */
	FioCloseAll();

	UninitFreeType();
}

/**
 * Load the introduction game.
 * @param load_newgrfs Whether to load the NewGRFs or not.
 */
static void LoadIntroGame(bool load_newgrfs = true)
{
	_game_mode = GM_MENU;

	if (load_newgrfs) ResetGRFConfig(false);

	/* Setup main window */
	ResetWindowSystem();
	SetupColoursAndInitialWindow();

	/* Load the default opening screen savegame */
	if (SaveOrLoad("opntitle.dat", SLO_LOAD, DFT_GAME_FILE, BASESET_DIR) != SL_OK) {
		GenerateWorld(GWM_EMPTY, 64, 64); // if failed loading, make empty world.
		WaitTillGeneratedWorld();
		SetLocalCompany(COMPANY_SPECTATOR);
	} else {
		SetLocalCompany(COMPANY_FIRST);
	}

	FixTitleGameZoom();
	_pause_mode = PM_UNPAUSED;
	_cursor.fix_at = false;

	CheckForMissingGlyphs();

	MusicLoop(); // ensure music is correct
}

void MakeNewgameSettingsLive()
{
	for (CompanyID c = COMPANY_FIRST; c < MAX_COMPANIES; c++) {
		if (_settings_game.ai_config[c] != nullptr) {
			delete _settings_game.ai_config[c];
		}
	}
	if (_settings_game.game_config != nullptr) {
		delete _settings_game.game_config;
	}

	/* Copy newgame settings to active settings.
	 * Also initialise old settings needed for savegame conversion. */
	_settings_game = _settings_newgame;
	_old_vds = _settings_client.company.vehicle;

	for (CompanyID c = COMPANY_FIRST; c < MAX_COMPANIES; c++) {
		_settings_game.ai_config[c] = nullptr;
		if (_settings_newgame.ai_config[c] != nullptr) {
			_settings_game.ai_config[c] = new AIConfig(_settings_newgame.ai_config[c]);
			if (!AIConfig::GetConfig(c, AIConfig::SSS_FORCE_GAME)->HasScript()) {
				AIConfig::GetConfig(c, AIConfig::SSS_FORCE_GAME)->Change(nullptr);
			}
		}
	}
	_settings_game.game_config = nullptr;
	if (_settings_newgame.game_config != nullptr) {
		_settings_game.game_config = new GameConfig(_settings_newgame.game_config);
	}
}

void OpenBrowser(const char *url)
{
	/* Make sure we only accept urls that are sure to open a browser. */
	if (strstr(url, "http://") != url && strstr(url, "https://") != url) return;

	extern void OSOpenBrowser(const char *url);
	OSOpenBrowser(url);
}

/** Callback structure of statements to be executed after the NewGRF scan. */
struct AfterNewGRFScan : NewGRFScanCallback {
	Year startyear;                    ///< The start year.
	uint32 generation_seed;            ///< Seed for the new game.
	char *dedicated_host;              ///< Hostname for the dedicated server.
	uint16 dedicated_port;             ///< Port for the dedicated server.
	char *network_conn;                ///< Information about the server to connect to, or nullptr.
	const char *join_server_password;  ///< The password to join the server with.
	const char *join_company_password; ///< The password to join the company with.
	bool save_config;                  ///< The save config setting.

	/**
	 * Create a new callback.
	 */
	AfterNewGRFScan() :
			startyear(INVALID_YEAR), generation_seed(GENERATE_NEW_SEED),
			dedicated_host(nullptr), dedicated_port(0), network_conn(nullptr),
			join_server_password(nullptr), join_company_password(nullptr),
			save_config(true)
	{
		/* Visual C++ 2015 fails compiling this line (AfterNewGRFScan::generation_seed undefined symbol)
		 * if it's placed outside a member function, directly in the struct body. */
		assert_compile(sizeof(generation_seed) == sizeof(_settings_game.game_creation.generation_seed));
	}

	virtual void OnNewGRFsScanned()
	{
		ResetGRFConfig(false);

		TarScanner::DoScan(TarScanner::SCENARIO);

		AI::Initialize();
		Game::Initialize();

		/* We want the new (correct) NewGRF count to survive the loading. */
		uint last_newgrf_count = _settings_client.gui.last_newgrf_count;
		LoadFromConfig();
		_settings_client.gui.last_newgrf_count = last_newgrf_count;
		/* Since the default for the palette might have changed due to
		 * reading the configuration file, recalculate that now. */
		UpdateNewGRFConfigPalette();

		Game::Uninitialize(true);
		AI::Uninitialize(true);
		LoadFromHighScore();
		LoadHotkeysFromConfig();
		WindowDesc::LoadFromConfig();

		/* We have loaded the config, so we may possibly save it. */
		_save_config = save_config;

		/* restore saved music volume */
		MusicDriver::GetInstance()->SetVolume(_settings_client.music.music_vol);

		if (startyear != INVALID_YEAR) _settings_newgame.game_creation.starting_year = startyear;
		if (generation_seed != GENERATE_NEW_SEED) _settings_newgame.game_creation.generation_seed = generation_seed;

		if (dedicated_host != nullptr) {
			_network_bind_list.clear();
			_network_bind_list.emplace_back(dedicated_host);
		}
		if (dedicated_port != 0) _settings_client.network.server_port = dedicated_port;

		/* initialize the ingame console */
		IConsoleInit();
		InitializeGUI();
		IConsoleCmdExec("exec scripts/autoexec.scr 0");

		/* Make sure _settings is filled with _settings_newgame if we switch to a game directly */
		if (_switch_mode != SM_NONE) MakeNewgameSettingsLive();

		if (_network_available && network_conn != nullptr) {
			const char *port = nullptr;
			const char *company = nullptr;
			uint16 rport = NETWORK_DEFAULT_PORT;
			CompanyID join_as = COMPANY_NEW_COMPANY;

			ParseConnectionString(&company, &port, network_conn);

			if (company != nullptr) {
				join_as = (CompanyID)atoi(company);

				if (join_as != COMPANY_SPECTATOR) {
					join_as--;
					if (join_as >= MAX_COMPANIES) {
						delete this;
						return;
					}
				}
			}
			if (port != nullptr) rport = atoi(port);

			LoadIntroGame();
			_switch_mode = SM_NONE;
			NetworkClientConnectGame(NetworkAddress(network_conn, rport), join_as, join_server_password, join_company_password);
		}

		/* After the scan we're not used anymore. */
		delete this;
	}
};

#if defined(UNIX)
extern void DedicatedFork();
#endif

/** Options of OpenTTD. */
static const OptionData _options[] = {
	 GETOPT_SHORT_VALUE('I'),
	 GETOPT_SHORT_VALUE('S'),
	 GETOPT_SHORT_VALUE('M'),
	 GETOPT_SHORT_VALUE('m'),
	 GETOPT_SHORT_VALUE('s'),
	 GETOPT_SHORT_VALUE('v'),
	 GETOPT_SHORT_VALUE('b'),
	GETOPT_SHORT_OPTVAL('D'),
	GETOPT_SHORT_OPTVAL('n'),
	 GETOPT_SHORT_VALUE('l'),
	 GETOPT_SHORT_VALUE('p'),
	 GETOPT_SHORT_VALUE('P'),
#if !defined(_WIN32)
	 GETOPT_SHORT_NOVAL('f'),
#endif
	 GETOPT_SHORT_VALUE('r'),
	 GETOPT_SHORT_VALUE('t'),
	GETOPT_SHORT_OPTVAL('d'),
	 GETOPT_SHORT_NOVAL('e'),
	GETOPT_SHORT_OPTVAL('g'),
	 GETOPT_SHORT_VALUE('G'),
	 GETOPT_SHORT_VALUE('c'),
	 GETOPT_SHORT_NOVAL('x'),
	 GETOPT_SHORT_VALUE('q'),
	 GETOPT_SHORT_NOVAL('h'),
	GETOPT_END()
};

/**
 * Main entry point for this lovely game.
 * @param argc The number of arguments passed to this game.
 * @param argv The values of the arguments.
 * @return 0 when there is no error.
 */
int openttd_main(int argc, char *argv[])
{
	std::string musicdriver;
	std::string sounddriver;
	std::string videodriver;
	std::string blitter;
	std::string graphics_set;
	std::string sounds_set;
	std::string music_set;
	Dimension resolution = {0, 0};
	std::unique_ptr<AfterNewGRFScan> scanner(new AfterNewGRFScan());
	bool dedicated = false;
	char *debuglog_conn = nullptr;

	extern bool _dedicated_forks;
	_dedicated_forks = false;

	std::unique_lock<std::mutex> modal_work_lock(_modal_progress_work_mutex, std::defer_lock);
	std::unique_lock<std::mutex> modal_paint_lock(_modal_progress_paint_mutex, std::defer_lock);

	_game_mode = GM_MENU;
	_switch_mode = SM_MENU;
	_config_file = nullptr;

	GetOptData mgo(argc - 1, argv + 1, _options);
	int ret = 0;

	int i;
	while ((i = mgo.GetOpt()) != -1) {
		switch (i) {
		case 'I': graphics_set = mgo.opt; break;
		case 'S': sounds_set = mgo.opt; break;
		case 'M': music_set = mgo.opt; break;
		case 'm': musicdriver = mgo.opt; break;
		case 's': sounddriver = mgo.opt; break;
		case 'v': videodriver = mgo.opt; break;
		case 'b': blitter = mgo.opt; break;
		case 'D':
			musicdriver = "null";
			sounddriver = "null";
			videodriver = "dedicated";
			blitter = "null";
			dedicated = true;
			SetDebugString("net=6");
			if (mgo.opt != nullptr) {
				/* Use the existing method for parsing (openttd -n).
				 * However, we do ignore the #company part. */
				const char *temp = nullptr;
				const char *port = nullptr;
				ParseConnectionString(&temp, &port, mgo.opt);
				if (!StrEmpty(mgo.opt)) scanner->dedicated_host = mgo.opt;
				if (port != nullptr) scanner->dedicated_port = atoi(port);
			}
			break;
		case 'f': _dedicated_forks = true; break;
		case 'n':
			scanner->network_conn = mgo.opt; // optional IP parameter, nullptr if unset
			break;
		case 'l':
			debuglog_conn = mgo.opt;
			break;
		case 'p':
			scanner->join_server_password = mgo.opt;
			break;
		case 'P':
			scanner->join_company_password = mgo.opt;
			break;
		case 'r': ParseResolution(&resolution, mgo.opt); break;
		case 't': scanner->startyear = atoi(mgo.opt); break;
		case 'd': {
#if defined(_WIN32)
				CreateConsole();
#endif
				if (mgo.opt != nullptr) SetDebugString(mgo.opt);
				break;
			}
		case 'e': _switch_mode = (_switch_mode == SM_LOAD_GAME || _switch_mode == SM_LOAD_SCENARIO ? SM_LOAD_SCENARIO : SM_EDITOR); break;
		case 'g':
			if (mgo.opt != nullptr) {
				_file_to_saveload.SetName(mgo.opt);
				bool is_scenario = _switch_mode == SM_EDITOR || _switch_mode == SM_LOAD_SCENARIO;
				_switch_mode = is_scenario ? SM_LOAD_SCENARIO : SM_LOAD_GAME;
				_file_to_saveload.SetMode(SLO_LOAD, is_scenario ? FT_SCENARIO : FT_SAVEGAME, DFT_GAME_FILE);

				/* if the file doesn't exist or it is not a valid savegame, let the saveload code show an error */
				const char *t = strrchr(_file_to_saveload.name, '.');
				if (t != nullptr) {
					FiosType ft = FiosGetSavegameListCallback(SLO_LOAD, _file_to_saveload.name, t, nullptr, nullptr);
					if (ft != FIOS_TYPE_INVALID) _file_to_saveload.SetMode(ft);
				}

				break;
			}

			_switch_mode = SM_NEWGAME;
			/* Give a random map if no seed has been given */
			if (scanner->generation_seed == GENERATE_NEW_SEED) {
				scanner->generation_seed = InteractiveRandom();
			}
			break;
		case 'q': {
			DeterminePaths(argv[0]);
			if (StrEmpty(mgo.opt)) {
				ret = 1;
				return ret;
			}

			char title[80];
			title[0] = '\0';
			FiosGetSavegameListCallback(SLO_LOAD, mgo.opt, strrchr(mgo.opt, '.'), title, lastof(title));

			_load_check_data.Clear();
			SaveOrLoadResult res = SaveOrLoad(mgo.opt, SLO_CHECK, DFT_GAME_FILE, SAVE_DIR, false);
			if (res != SL_OK || _load_check_data.HasErrors()) {
				fprintf(stderr, "Failed to open savegame\n");
				if (_load_check_data.HasErrors()) {
					char buf[256];
					SetDParamStr(0, _load_check_data.error_data);
					GetString(buf, _load_check_data.error, lastof(buf));
					fprintf(stderr, "%s\n", buf);
				}
				return ret;
			}

			WriteSavegameInfo(title);
			return ret;
		}
		case 'G': scanner->generation_seed = strtoul(mgo.opt, nullptr, 10); break;
		case 'c': free(_config_file); _config_file = stredup(mgo.opt); break;
		case 'x': scanner->save_config = false; break;
		case 'h':
			i = -2; // Force printing of help.
			break;
		}
		if (i == -2) break;
	}

	if (i == -2 || mgo.numleft > 0) {
		/* Either the user typed '-h', he made an error, or he added unrecognized command line arguments.
		 * In all cases, print the help, and exit.
		 *
		 * The next two functions are needed to list the graphics sets. We can't do them earlier
		 * because then we cannot show it on the debug console as that hasn't been configured yet. */
		DeterminePaths(argv[0]);
		TarScanner::DoScan(TarScanner::BASESET);
		BaseGraphics::FindSets();
		BaseSounds::FindSets();
		BaseMusic::FindSets();
		ShowHelp();
		return ret;
	}

	DeterminePaths(argv[0]);
	TarScanner::DoScan(TarScanner::BASESET);

	if (dedicated) DEBUG(net, 0, "Starting dedicated version %s", _openttd_revision);
	if (_dedicated_forks && !dedicated) _dedicated_forks = false;

#if defined(UNIX)
	/* We must fork here, or we'll end up without some resources we need (like sockets) */
	if (_dedicated_forks) DedicatedFork();
#endif

	LoadFromConfig(true);

	if (resolution.width != 0) _cur_resolution = resolution;

	/*
	 * The width and height must be at least 1 pixel and width times
	 * height times bytes per pixel must still fit within a 32 bits
	 * integer, even for 32 bpp video modes. This way all internal
	 * drawing routines work correctly.
	 */
	_cur_resolution.width  = ClampU(_cur_resolution.width,  1, UINT16_MAX / 2);
	_cur_resolution.height = ClampU(_cur_resolution.height, 1, UINT16_MAX / 2);

	/* Assume the cursor starts within the game as not all video drivers
	 * get an event that the cursor is within the window when it is opened.
	 * Saying the cursor is there makes no visible difference as it would
	 * just be out of the bounds of the window. */
	_cursor.in_window = true;

	/* enumerate language files */
	InitializeLanguagePacks();

	/* Initialize the regular font for FreeType */
	InitFreeType(false);

	/* This must be done early, since functions use the SetWindowDirty* calls */
	InitWindowSystem();

	BaseGraphics::FindSets();
	if (graphics_set.empty() && !BaseGraphics::ini_set.empty()) graphics_set = BaseGraphics::ini_set;
	if (!BaseGraphics::SetSet(graphics_set)) {
		if (!graphics_set.empty()) {
			BaseGraphics::SetSet({});

			ErrorMessageData msg(STR_CONFIG_ERROR, STR_CONFIG_ERROR_INVALID_BASE_GRAPHICS_NOT_FOUND);
			msg.SetDParamStr(0, graphics_set.c_str());
			ScheduleErrorMessage(msg);
		}
	}

	/* Initialize game palette */
	GfxInitPalettes();

	DEBUG(misc, 1, "Loading blitter...");
	if (blitter.empty() && !_ini_blitter.empty()) blitter = _ini_blitter;
	_blitter_autodetected = blitter.empty();
	/* Activate the initial blitter.
	 * This is only some initial guess, after NewGRFs have been loaded SwitchNewGRFBlitter may switch to a different one.
	 *  - Never guess anything, if the user specified a blitter. (_blitter_autodetected)
	 *  - Use 32bpp blitter if baseset or 8bpp-support settings says so.
	 *  - Use 8bpp blitter otherwise.
	 */
	if (!_blitter_autodetected ||
			(_support8bpp != S8BPP_NONE && (BaseGraphics::GetUsedSet() == nullptr || BaseGraphics::GetUsedSet()->blitter == BLT_8BPP)) ||
			BlitterFactory::SelectBlitter("32bpp-anim") == nullptr) {
		if (BlitterFactory::SelectBlitter(blitter) == nullptr) {
			blitter.empty() ?
				usererror("Failed to autoprobe blitter") :
				usererror("Failed to select requested blitter '%s'; does it exist?", blitter.c_str());
		}
	}

	if (videodriver.empty() && !_ini_videodriver.empty()) videodriver = _ini_videodriver;
	DriverFactoryBase::SelectDriver(videodriver, Driver::DT_VIDEO);

	InitializeSpriteSorter();

	/* Initialize the zoom level of the screen to normal */
	_screen.zoom = ZOOM_LVL_NORMAL;

	NetworkStartUp(); // initialize network-core

	if (debuglog_conn != nullptr && _network_available) {
		const char *not_used = nullptr;
		const char *port = nullptr;
		uint16 rport;

		rport = NETWORK_DEFAULT_DEBUGLOG_PORT;

		ParseConnectionString(&not_used, &port, debuglog_conn);
		if (port != nullptr) rport = atoi(port);

		NetworkStartDebugLog(NetworkAddress(debuglog_conn, rport));
	}

	if (!HandleBootstrap()) {
		ShutdownGame();
		return ret;
	}

	VideoDriver::GetInstance()->ClaimMousePointer();

	/* initialize screenshot formats */
	InitializeScreenshotFormats();

	BaseSounds::FindSets();
	if (sounds_set.empty() && !BaseSounds::ini_set.empty()) sounds_set = BaseSounds::ini_set;
	if (!BaseSounds::SetSet(sounds_set)) {
		if (sounds_set.empty() || !BaseSounds::SetSet({})) {
			usererror("Failed to find a sounds set. Please acquire a sounds set for OpenTTD. See section 1.4 of README.md.");
		} else {
			ErrorMessageData msg(STR_CONFIG_ERROR, STR_CONFIG_ERROR_INVALID_BASE_SOUNDS_NOT_FOUND);
			msg.SetDParamStr(0, sounds_set.c_str());
			ScheduleErrorMessage(msg);
		}
	}

	BaseMusic::FindSets();
	if (music_set.empty() && !BaseMusic::ini_set.empty()) music_set = BaseMusic::ini_set;
	if (!BaseMusic::SetSet(music_set)) {
		if (music_set.empty() || !BaseMusic::SetSet({})) {
			usererror("Failed to find a music set. Please acquire a music set for OpenTTD. See section 1.4 of README.md.");
		} else {
			ErrorMessageData msg(STR_CONFIG_ERROR, STR_CONFIG_ERROR_INVALID_BASE_MUSIC_NOT_FOUND);
			msg.SetDParamStr(0, music_set.c_str());
			ScheduleErrorMessage(msg);
		}
	}

	if (sounddriver.empty() && !_ini_sounddriver.empty()) sounddriver = _ini_sounddriver;
	DriverFactoryBase::SelectDriver(sounddriver, Driver::DT_SOUND);

	if (musicdriver.empty() && !_ini_musicdriver.empty()) musicdriver = _ini_musicdriver;
	DriverFactoryBase::SelectDriver(musicdriver, Driver::DT_MUSIC);

	/* Take our initial lock on whatever we might want to do! */
	try {
		modal_work_lock.lock();
		modal_paint_lock.lock();
	} catch (const std::system_error&) {
		/* If there is some error we assume that threads aren't usable on the system we run. */
		extern bool _use_threaded_modal_progress; // From progress.cpp
		_use_threaded_modal_progress = false;
	}

	GenerateWorld(GWM_EMPTY, 64, 64); // Make the viewport initialization happy
	WaitTillGeneratedWorld();

	LoadIntroGame(false);

	CheckForMissingGlyphs();

	/* ScanNewGRFFiles now has control over the scanner. */
	ScanNewGRFFiles(scanner.release());

	VideoDriver::GetInstance()->MainLoop();

	WaitTillSaved();
	WaitTillGeneratedWorld(); // Make sure any generate world threads have been joined.

	/* only save config if we have to */
	if (_save_config) {
		SaveToConfig();
		SaveHotkeysToConfig();
		WindowDesc::SaveToConfig();
		SaveToHighScore();
	}

	/* Reset windowing system, stop drivers, free used memory, ... */
	ShutdownGame();
	return ret;
}

void HandleExitGameRequest()
{
	if (_game_mode == GM_MENU || _game_mode == GM_BOOTSTRAP) { // do not ask to quit on the main screen
		_exit_game = true;
	} else if (_settings_client.gui.autosave_on_exit) {
		DoExitSave();
		_exit_game = true;
	} else {
		AskExitGame();
	}
}

static void MakeNewGameDone()
{
	SettingsDisableElrail(_settings_game.vehicle.disable_elrails);

	/* In a dedicated server, the server does not play */
	if (!VideoDriver::GetInstance()->HasGUI()) {
		SetLocalCompany(COMPANY_SPECTATOR);
		if (_settings_client.gui.pause_on_newgame) DoCommandP(0, PM_PAUSED_NORMAL, 1, CMD_PAUSE);
		IConsoleCmdExec("exec scripts/game_start.scr 0");
		return;
	}

	/* Create a single company */
	DoStartupNewCompany(false);

	Company *c = Company::Get(COMPANY_FIRST);
	c->settings = _settings_client.company;

	/* Overwrite color from settings if needed
	 * COLOUR_END corresponds to Random colour */
	if (_settings_client.gui.starting_colour != COLOUR_END) {
		c->colour = _settings_client.gui.starting_colour;
		ResetCompanyLivery(c);
		_company_colours[c->index] = (Colours)c->colour;
	}

	IConsoleCmdExec("exec scripts/game_start.scr 0");

	SetLocalCompany(COMPANY_FIRST);

	InitializeRailGUI();
	InitializeRoadGUI();

	/* We are the server, we start a new company (not dedicated),
	 * so set the default password *if* needed. */
	if (_network_server && !StrEmpty(_settings_client.network.default_company_pass)) {
		NetworkChangeCompanyPassword(_local_company, _settings_client.network.default_company_pass);
	}
	if (_network_server && _settings_client.network.company_autoprotect) {
		NetworkChangeCompanyPubkey(_local_company, true);
	}

	if (_settings_client.gui.pause_on_newgame) DoCommandP(0, PM_PAUSED_NORMAL, 1, CMD_PAUSE);

	CheckEngines();
	CheckIndustries();
	MarkWholeScreenDirty();
}

static void MakeNewGame(bool from_heightmap, bool reset_settings)
{
	_game_mode = GM_NORMAL;

	ResetGRFConfig(true);

	GenerateWorldSetCallback(&MakeNewGameDone);
	GenerateWorld(from_heightmap ? GWM_HEIGHTMAP : GWM_NEWGAME, 1 << _settings_game.game_creation.map_x, 1 << _settings_game.game_creation.map_y, reset_settings);
}

static void MakeNewEditorWorldDone()
{
	SetLocalCompany(OWNER_NONE);
}

static void MakeNewEditorWorld()
{
	_game_mode = GM_EDITOR;

	ResetGRFConfig(true);

	GenerateWorldSetCallback(&MakeNewEditorWorldDone);
	GenerateWorld(GWM_EMPTY, 1 << _settings_game.game_creation.map_x, 1 << _settings_game.game_creation.map_y);
}

/**
 * Load the specified savegame but on error do different things.
 * If loading fails due to corrupt savegame, bad version, etc. go back to
 * a previous correct state. In the menu for example load the intro game again.
 * @param filename file to be loaded
 * @param fop mode of loading, always SLO_LOAD
 * @param newgm switch to this mode of loading fails due to some unknown error
 * @param subdir default directory to look for filename, set to 0 if not needed
 * @param lf Load filter to use, if nullptr: use filename + subdir.
 */
bool SafeLoad(const char *filename, SaveLoadOperation fop, DetailedFileType dft, GameMode newgm, Subdirectory subdir, struct LoadFilter *lf = nullptr)
{
	assert(fop == SLO_LOAD);
	assert(dft == DFT_GAME_FILE || (lf == nullptr && dft == DFT_OLD_GAME_FILE));
	GameMode ogm = _game_mode;

	_game_mode = newgm;

	switch (lf == nullptr ? SaveOrLoad(filename, fop, dft, subdir) : LoadWithFilter(lf)) {
		case SL_OK: return true;

		case SL_REINIT:
			if (_network_dedicated) {
				/*
				 * We need to reinit a network map...
				 * We can't simply load the intro game here as that game has many
				 * special cases which make clients desync immediately. So we fall
				 * back to just generating a new game with the current settings.
				 */
				DEBUG(net, 0, "Loading game failed, so a new (random) game will be started!");
				MakeNewGame(false, true);
				return false;
			}
			if (_network_server) {
				/* We can't load the intro game as server, so disconnect first. */
				NetworkDisconnect();
			}

			switch (ogm) {
				default:
				case GM_MENU:   LoadIntroGame();      break;
				case GM_EDITOR: MakeNewEditorWorld(); break;
			}
			return false;

		default:
			_game_mode = ogm;
			return false;
	}
}

void SwitchToMode(SwitchMode new_mode)
{
	/* If we are saving something, the network stays in his current state */
	if (new_mode != SM_SAVE_GAME) {
		/* If the network is active, make it not-active */
		if (_networking) {
			if (_network_server && (new_mode == SM_LOAD_GAME || new_mode == SM_NEWGAME || new_mode == SM_RESTARTGAME)) {
				NetworkReboot();
			} else {
				NetworkDisconnect();
			}
		}

		/* If we are a server, we restart the server */
		if (_is_network_server) {
			/* But not if we are going to the menu */
			if (new_mode != SM_MENU) {
				/* check if we should reload the config */
				if (_settings_client.network.reload_cfg) {
					LoadFromConfig();
					MakeNewgameSettingsLive();
					ResetGRFConfig(false);
				}
				NetworkServerStart();
			} else {
				/* This client no longer wants to be a network-server */
				_is_network_server = false;
			}
		}
	}

	/* Make sure all AI controllers are gone at quitting game */
	if (new_mode != SM_SAVE_GAME) AI::KillAll();

	switch (new_mode) {
		case SM_EDITOR: // Switch to scenario editor
			MakeNewEditorWorld();
			break;

		case SM_RESTARTGAME: // Restart --> Current settings preserved
			if (_file_to_saveload.abstract_ftype == FT_SAVEGAME || _file_to_saveload.abstract_ftype == FT_SCENARIO) {
				/* Restart current savegame/scenario */
				_switch_mode = _game_mode == GM_EDITOR ? SM_LOAD_SCENARIO : SM_LOAD_GAME;
				SwitchToMode(_switch_mode);
				break;
			} else if (_file_to_saveload.abstract_ftype == FT_HEIGHTMAP) {
				/* Restart current heightmap */
				_switch_mode = _game_mode == GM_EDITOR ? SM_LOAD_HEIGHTMAP : SM_RESTART_HEIGHTMAP;
				SwitchToMode(_switch_mode);
				break;
			}
			/* No break here, to enter the next case:
			 * Restart --> 'Random game' with current settings */
			FALLTHROUGH;

		case SM_NEWGAME: // New Game --> 'Random game'
			if (_network_server) {
				seprintf(_network_game_info.map_name, lastof(_network_game_info.map_name), "Random Map");
			}
			MakeNewGame(false, new_mode == SM_NEWGAME);
			break;

		case SM_LOAD_GAME: { // Load game, Play Scenario
			ResetGRFConfig(true);
			ResetWindowSystem();

			if (!SafeLoad(_file_to_saveload.name, _file_to_saveload.file_op, _file_to_saveload.detail_ftype, GM_NORMAL, NO_DIRECTORY)) {
				SetDParamStr(0, GetSaveLoadErrorString());
				ShowErrorMessage(STR_JUST_RAW_STRING, INVALID_STRING_ID, WL_ERROR);
			} else {
				if (_file_to_saveload.abstract_ftype == FT_SCENARIO) {
					/* Reset engine pool to simplify changing engine NewGRFs in scenario editor. */
					EngineOverrideManager::ResetToCurrentNewGRFConfig();
				}
				/* Update the local company for a loaded game. It is either always
				 * company #1 (eg 0) or in the case of a dedicated server a spectator */
				SetLocalCompany(_network_dedicated ? COMPANY_SPECTATOR : COMPANY_FIRST);
				/* Execute the game-start script */
				IConsoleCmdExec("exec scripts/game_start.scr 0");
				/* Decrease pause counter (was increased from opening load dialog) */
				DoCommandP(0, PM_PAUSED_SAVELOAD, 0, CMD_PAUSE);
				if (_network_server) {
					seprintf(_network_game_info.map_name, lastof(_network_game_info.map_name), "%s (Loaded game)", _file_to_saveload.title);
				}
			}
			break;
		}

		case SM_RESTART_HEIGHTMAP: // Load a heightmap and start a new game from it with current settings
		case SM_START_HEIGHTMAP: // Load a heightmap and start a new game from it
			if (_network_server) {
				seprintf(_network_game_info.map_name, lastof(_network_game_info.map_name), "%s (Heightmap)", _file_to_saveload.title);
			}
			MakeNewGame(true, new_mode == SM_START_HEIGHTMAP);
			break;

		case SM_LOAD_HEIGHTMAP: // Load heightmap from scenario editor
			SetLocalCompany(OWNER_NONE);

			GenerateWorld(GWM_HEIGHTMAP, 1 << _settings_game.game_creation.map_x, 1 << _settings_game.game_creation.map_y);
			MarkWholeScreenDirty();
			break;

		case SM_LOAD_SCENARIO: { // Load scenario from scenario editor
			if (SafeLoad(_file_to_saveload.name, _file_to_saveload.file_op, _file_to_saveload.detail_ftype, GM_EDITOR, NO_DIRECTORY)) {
				SetLocalCompany(OWNER_NONE);
				_settings_newgame.game_creation.starting_year = _cur_year;
				/* Cancel the saveload pausing */
				DoCommandP(0, PM_PAUSED_SAVELOAD, 0, CMD_PAUSE);
			} else {
				SetDParamStr(0, GetSaveLoadErrorString());
				ShowErrorMessage(STR_JUST_RAW_STRING, INVALID_STRING_ID, WL_ERROR);
			}
			break;
		}

		case SM_MENU: // Switch to game intro menu
			LoadIntroGame();
			if (BaseSounds::ini_set.empty() && BaseSounds::GetUsedSet()->fallback && SoundDriver::GetInstance()->HasOutput()) {
				ShowErrorMessage(STR_WARNING_FALLBACK_SOUNDSET, INVALID_STRING_ID, WL_CRITICAL);
				BaseSounds::ini_set = BaseSounds::GetUsedSet()->name;
			}
			break;

		case SM_SAVE_GAME: // Save game.
			/* Make network saved games on pause compatible to singleplayer */
			if (SaveOrLoad(_file_to_saveload.name, SLO_SAVE, DFT_GAME_FILE, NO_DIRECTORY) != SL_OK) {
				SetDParamStr(0, GetSaveLoadErrorString());
				ShowErrorMessage(STR_JUST_RAW_STRING, INVALID_STRING_ID, WL_ERROR);
			} else {
				DeleteWindowById(WC_SAVELOAD, 0);
			}
			break;

		case SM_SAVE_HEIGHTMAP: // Save heightmap.
			MakeHeightmapScreenshot(_file_to_saveload.name);
			DeleteWindowById(WC_SAVELOAD, 0);
			break;

		case SM_GENRANDLAND: // Generate random land within scenario editor
			SetLocalCompany(OWNER_NONE);
			GenerateWorld(GWM_RANDOM, 1 << _settings_game.game_creation.map_x, 1 << _settings_game.game_creation.map_y);
			/* XXX: set date */
			MarkWholeScreenDirty();
			break;

		default: NOT_REACHED();
	}
}


/**
 * Check the validity of some of the caches.
 * Especially in the sense of desyncs between
 * the cached value and what the value would
 * be when calculated from the 'base' data.
 */
static void CheckCaches()
{
	/* Return here so it is easy to add checks that are run
	 * always to aid testing of caches. */
	if (_debug_desync_level <= 1) return;

	/* Check the town caches. */
	std::vector<TownCache> old_town_caches;
	for (const Town *t : Town::Iterate()) {
		old_town_caches.push_back(t->cache);
	}

	extern void RebuildTownCaches();
	RebuildTownCaches();
	RebuildSubsidisedSourceAndDestinationCache();

	uint i = 0;
	for (Town *t : Town::Iterate()) {
		if (MemCmpT(old_town_caches.data() + i, &t->cache) != 0) {
			DEBUG(desync, 2, "town cache mismatch: town %i", (int)t->index);
		}
		i++;
	}

	/* Check company infrastructure cache. */
	std::vector<CompanyInfrastructure> old_infrastructure;
	for (const Company *c : Company::Iterate()) old_infrastructure.push_back(c->infrastructure);

	extern void AfterLoadCompanyStats();
	AfterLoadCompanyStats();

	i = 0;
	for (const Company *c : Company::Iterate()) {
		if (MemCmpT(old_infrastructure.data() + i, &c->infrastructure) != 0) {
			DEBUG(desync, 2, "infrastructure cache mismatch: company %i", (int)c->index);
		}
		i++;
	}

	/* Strict checking of the road stop cache entries */
	for (const RoadStop *rs : RoadStop::Iterate()) {
		if (IsStandardRoadStopTile(rs->xy)) continue;

		assert(rs->GetEntry(DIAGDIR_NE) != rs->GetEntry(DIAGDIR_NW));
		rs->GetEntry(DIAGDIR_NE)->CheckIntegrity(rs);
		rs->GetEntry(DIAGDIR_NW)->CheckIntegrity(rs);
	}

	for (Vehicle *v : Vehicle::Iterate()) {
		extern void FillNewGRFVehicleCache(const Vehicle *v);
		if (v != v->First() || v->vehstatus & VS_CRASHED || !v->IsPrimaryVehicle()) continue;

		uint length = 0;
		for (const Vehicle *u = v; u != nullptr; u = u->Next()) length++;

		NewGRFCache        *grf_cache = CallocT<NewGRFCache>(length);
		VehicleCache       *veh_cache = CallocT<VehicleCache>(length);
		GroundVehicleCache *gro_cache = CallocT<GroundVehicleCache>(length);
		TrainCache         *tra_cache = CallocT<TrainCache>(length);

		length = 0;
		for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
			FillNewGRFVehicleCache(u);
			grf_cache[length] = u->grf_cache;
			veh_cache[length] = u->vcache;
			switch (u->type) {
				case VEH_TRAIN:
					gro_cache[length] = Train::From(u)->gcache;
					tra_cache[length] = Train::From(u)->tcache;
					break;
				case VEH_ROAD:
					gro_cache[length] = RoadVehicle::From(u)->gcache;
					break;
				default:
					break;
			}
			length++;
		}

		switch (v->type) {
			case VEH_TRAIN:    Train::From(v)->ConsistChanged(CCF_TRACK); break;
			case VEH_ROAD:     RoadVehUpdateCache(RoadVehicle::From(v)); break;
			case VEH_AIRCRAFT: UpdateAircraftCache(Aircraft::From(v));   break;
			case VEH_SHIP:     Ship::From(v)->UpdateCache();             break;
			default: break;
		}

		length = 0;
		for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
			FillNewGRFVehicleCache(u);
			if (memcmp(&grf_cache[length], &u->grf_cache, sizeof(NewGRFCache)) != 0) {
				DEBUG(desync, 2, "newgrf cache mismatch: type %i, vehicle %i, company %i, unit number %i, wagon %i", (int)v->type, v->index, (int)v->owner, v->unitnumber, length);
			}
			if (memcmp(&veh_cache[length], &u->vcache, sizeof(VehicleCache)) != 0) {
				DEBUG(desync, 2, "vehicle cache mismatch: type %i, vehicle %i, company %i, unit number %i, wagon %i", (int)v->type, v->index, (int)v->owner, v->unitnumber, length);
			}
			switch (u->type) {
				case VEH_TRAIN:
					if (memcmp(&gro_cache[length], &Train::From(u)->gcache, sizeof(GroundVehicleCache)) != 0) {
						DEBUG(desync, 2, "train ground vehicle cache mismatch: vehicle %i, company %i, unit number %i, wagon %i", v->index, (int)v->owner, v->unitnumber, length);
					}
					if (memcmp(&tra_cache[length], &Train::From(u)->tcache, sizeof(TrainCache)) != 0) {
						DEBUG(desync, 2, "train cache mismatch: vehicle %i, company %i, unit number %i, wagon %i", v->index, (int)v->owner, v->unitnumber, length);
					}
					break;
				case VEH_ROAD:
					if (memcmp(&gro_cache[length], &RoadVehicle::From(u)->gcache, sizeof(GroundVehicleCache)) != 0) {
						DEBUG(desync, 2, "road vehicle ground vehicle cache mismatch: vehicle %i, company %i, unit number %i, wagon %i", v->index, (int)v->owner, v->unitnumber, length);
					}
					break;
				default:
					break;
			}
			length++;
		}

		free(grf_cache);
		free(veh_cache);
		free(gro_cache);
		free(tra_cache);
	}

	/* Check whether the caches are still valid */
	for (Vehicle *v : Vehicle::Iterate()) {
		byte buff[sizeof(VehicleCargoList)];
		memcpy(buff, &v->cargo, sizeof(VehicleCargoList));
		v->cargo.InvalidateCache();
		assert(memcmp(&v->cargo, buff, sizeof(VehicleCargoList)) == 0);
	}

	/* Backup stations_near */
	std::vector<StationList> old_town_stations_near;
	for (Town *t : Town::Iterate()) old_town_stations_near.push_back(t->stations_near);

	std::vector<StationList> old_industry_stations_near;
	for (Industry *ind : Industry::Iterate())  old_industry_stations_near.push_back(ind->stations_near);

	for (Station *st : Station::Iterate()) {
		for (CargoID c = 0; c < NUM_CARGO; c++) {
			byte buff[sizeof(StationCargoList)];
			memcpy(buff, &st->goods[c].cargo, sizeof(StationCargoList));
			st->goods[c].cargo.InvalidateCache();
			assert(memcmp(&st->goods[c].cargo, buff, sizeof(StationCargoList)) == 0);
		}

		/* Check docking tiles */
		TileArea ta;
		std::map<TileIndex, bool> docking_tiles;
		TILE_AREA_LOOP(tile, st->docking_station) {
			ta.Add(tile);
			docking_tiles[tile] = IsDockingTile(tile);
		}
		UpdateStationDockingTiles(st);
		if (ta.tile != st->docking_station.tile || ta.w != st->docking_station.w || ta.h != st->docking_station.h) {
			DEBUG(desync, 2, "station docking mismatch: station %i, company %i", st->index, (int)st->owner);
		}
		TILE_AREA_LOOP(tile, ta) {
			if (docking_tiles[tile] != IsDockingTile(tile)) {
				DEBUG(desync, 2, "docking tile mismatch: tile %i", (int)tile);
			}
		}

		/* Check industries_near */
		IndustryList industries_near = st->industries_near;
		st->RecomputeCatchment();
		if (st->industries_near != industries_near) {
			DEBUG(desync, 2, "station industries near mismatch: station %i", st->index);
		}
	}

	/* Check stations_near */
	i = 0;
	for (Town *t : Town::Iterate()) {
		if (t->stations_near != old_town_stations_near[i]) {
			DEBUG(desync, 2, "town stations near mismatch: town %i", t->index);
		}
		i++;
	}
	i = 0;
	for (Industry *ind : Industry::Iterate()) {
		if (ind->stations_near != old_industry_stations_near[i]) {
			DEBUG(desync, 2, "industry stations near mismatch: industry %i", ind->index);
		}
		i++;
	}
}

/**
 * State controlling game loop.
 * The state must not be changed from anywhere but here.
 * That check is enforced in DoCommand.
 */
void StateGameLoop()
{
	/* don't execute the state loop during pause */
	if (_pause_mode != PM_UNPAUSED) {
		PerformanceMeasurer::Paused(PFE_GAMELOOP);
		PerformanceMeasurer::Paused(PFE_GL_ECONOMY);
		PerformanceMeasurer::Paused(PFE_GL_TRAINS);
		PerformanceMeasurer::Paused(PFE_GL_ROADVEHS);
		PerformanceMeasurer::Paused(PFE_GL_SHIPS);
		PerformanceMeasurer::Paused(PFE_GL_AIRCRAFT);
		PerformanceMeasurer::Paused(PFE_GL_LANDSCAPE);

		UpdateLandscapingLimits();
#ifndef DEBUG_DUMP_COMMANDS
		Game::GameLoop();
#endif
		return;
	}

	PerformanceMeasurer framerate(PFE_GAMELOOP);
	PerformanceAccumulator::Reset(PFE_GL_LANDSCAPE);
	if (HasModalProgress()) return;

	Layouter::ReduceLineCache();

	if (_game_mode == GM_EDITOR) {
		BasePersistentStorageArray::SwitchMode(PSM_ENTER_GAMELOOP);
		RunTileLoop();
		CallVehicleTicks();
		CallLandscapeTick();
		BasePersistentStorageArray::SwitchMode(PSM_LEAVE_GAMELOOP);
		UpdateLandscapingLimits();

		CallWindowGameTickEvent();
		NewsLoop();
	} else {
		if (_debug_desync_level > 2 && _date_fract == 0 && (_date & 0x1F) == 0) {
			/* Save the desync savegame if needed. */
			char name[MAX_PATH];
			seprintf(name, lastof(name), "dmp_cmds_%08x_%08x.sav", _settings_game.game_creation.generation_seed, _date);
			SaveOrLoad(name, SLO_SAVE, DFT_GAME_FILE, AUTOSAVE_DIR, false);
		}

		CheckCaches();

		/* All these actions has to be done from OWNER_NONE
		 *  for multiplayer compatibility */
		Backup<CompanyID> cur_company(_current_company, OWNER_NONE, FILE_LINE);

		BasePersistentStorageArray::SwitchMode(PSM_ENTER_GAMELOOP);
		AnimateAnimatedTiles();
		IncreaseDate();
		RunTileLoop();
		CallVehicleTicks();
		CallLandscapeTick();
		BasePersistentStorageArray::SwitchMode(PSM_LEAVE_GAMELOOP);

#ifndef DEBUG_DUMP_COMMANDS
		{
			PerformanceMeasurer framerate(PFE_ALLSCRIPTS);
			AI::GameLoop();
			Game::GameLoop();
		}
#endif
		UpdateLandscapingLimits();

		CallWindowGameTickEvent();
		NewsLoop();
		cur_company.Restore();
	}

	assert(IsLocalCompany());
}

/**
 * Create an autosave. The default name is "autosave#.sav". However with
 * the setting 'keep_all_autosave' the name defaults to company-name + date
 */
static void DoAutosave()
{
	char buf[MAX_PATH];

	if (_settings_client.gui.keep_all_autosave) {
		GenerateDefaultSaveName(buf, lastof(buf));
		strecat(buf, ".sav", lastof(buf));
	} else {
		static int _autosave_ctr = 0;

		/* generate a savegame name and number according to _settings_client.gui.max_num_autosaves */
		seprintf(buf, lastof(buf), "autosave%d.sav", _autosave_ctr);

		if (++_autosave_ctr >= _settings_client.gui.max_num_autosaves) _autosave_ctr = 0;
	}

	DEBUG(sl, 2, "Autosaving to '%s'", buf);
	if (SaveOrLoad(buf, SLO_SAVE, DFT_GAME_FILE, AUTOSAVE_DIR) != SL_OK) {
		ShowErrorMessage(STR_ERROR_AUTOSAVE_FAILED, INVALID_STRING_ID, WL_ERROR);
	}
}

void GameLoop()
{
	if (_game_mode == GM_BOOTSTRAP) {
		/* Check for UDP stuff */
		if (_network_available) NetworkBackgroundLoop();
		InputLoop();
		return;
	}

	ProcessAsyncSaveFinish();

	/* autosave game? */
	if (_do_autosave) {
		DoAutosave();
		_do_autosave = false;
		SetWindowDirty(WC_STATUS_BAR, 0);
	}

	/* switch game mode? */
	if (_switch_mode != SM_NONE && !HasModalProgress()) {
		SwitchToMode(_switch_mode);
		_switch_mode = SM_NONE;
	}

	IncreaseSpriteLRU();
	InteractiveRandom();

	/* Check for UDP stuff */
	if (_network_available) NetworkBackgroundLoop();

	if (_networking && !HasModalProgress()) {
		/* Multiplayer */
		NetworkGameLoop();
	} else {
		if (_network_reconnect > 0 && --_network_reconnect == 0) {
			/* This means that we want to reconnect to the last host
			 * We do this here, because it means that the network is really closed */
			NetworkClientConnectGame(NetworkAddress(_settings_client.network.last_host, _settings_client.network.last_port), COMPANY_SPECTATOR);
		}
		/* Singleplayer */
		StateGameLoop();
	}

	if (!_pause_mode && HasBit(_display_opt, DO_FULL_ANIMATION)) DoPaletteAnimations();

	InputLoop();

	SoundDriver::GetInstance()->MainLoop();
	MusicLoop();
}
