/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file game_info.h Convert NetworkGameInfo to Packet and back.
 */

#ifndef NETWORK_CORE_GAME_INFO_H
#define NETWORK_CORE_GAME_INFO_H

#include "config.h"
#include "core.h"
#include "../../newgrf_config.h"
#include "../../date_type.h"

/*
 * NetworkGameInfo has several revisions which we still need to support on the
 * wire. The table below shows the version and size for each field of the
 * serialized NetworkGameInfo.
 *
 * Version: Bytes:  Description:
 *   all      1       the version of this packet's structure
 *
 *   5+       var     join key of the server
 *   5+       1       NewGRF mode (0 = none: skip next two fields, 1 = short: skip name, 2 = full)
 *   4+       1       number of NewGRFs attached
 *   4+       n * M   unique identifier for NewGRF files. Consists of:
 *   4+                - one 4 byte variable with the NewGRF ID
 *   4+                - 16 bytes (sent sequentially) for the MD5 checksum
 *                       of the GRF
 *   5+                - name of the NewGRF
 *
 *   3+       4       current game date in days since 1-1-0 (DMY)
 *   3+       4       game introduction date in days since 1-1-0 (DMY)
 *
 *   2+       1       maximum number of companies allowed on the server
 *   2+       1       number of companies on the server
 *   2+       1       maximum number of spectators allowed on the server
 *
 *   1+       var     string with the name of the server
 *   1+       var     string with the revision of the server
 *   1..4     1       the language run on the server
 *                    (0 = any, 1 = English, 2 = German, 3 = French)
 *   1+       1       whether the server uses a password (0 = no, 1 = yes)
 *   1+       1       maximum number of clients allowed on the server
 *   1+       1       number of clients on the server
 *   1+       1       number of spectators on the server
 *   1..2     2       current game date in days since 1-1-1920 (DMY)
 *   1..2     2       game introduction date in days since 1-1-1920 (DMY)
 *   1..4     var     string with the name of the map
 *   1+       2       width of the map in tiles
 *   1+       2       height of the map in tiles
 *   1+       1       type of map:
 *                    (0 = temperate, 1 = arctic, 2 = desert, 3 = toyland)
 *   1+       1       whether the server is dedicated (0 = no, 1 = yes)
 */

/**
 * The game information that is sent from the server to the client.
 */
struct NetworkServerGameInfo {
	GRFConfig *grfconfig;        ///< List of NewGRF files used
	Date start_date;             ///< When the game started
	Date game_date;              ///< Current date
	uint16 map_width;            ///< Map width
	uint16 map_height;           ///< Map height
	std::string server_name;     ///< Server name
	std::string server_revision; ///< The version number the server is using (e.g.: 'r304' or 0.5.0)
	std::string join_key;        ///< Join-key of the server
	bool dedicated;              ///< Is this a dedicated server?
	bool use_password;           ///< Is this server passworded?
	byte clients_on;             ///< Current count of clients on server
	byte clients_max;            ///< Max clients allowed on server
	byte companies_on;           ///< How many started companies do we have
	byte companies_max;          ///< Max companies allowed on server
	byte spectators_on;          ///< How many spectators do we have?
	byte spectators_max;         ///< Max spectators allowed on server
	byte landscape;              ///< The used landscape
	int gamescript_version;      ///< Version of the gamescript.
	std::string gamescript_name; ///< Name of the gamescript.
};

/**
 * The game information that is sent from the server to the clients
 * with extra information only required at the client side.
 */
struct NetworkGameInfo : NetworkServerGameInfo {
	bool version_compatible;                        ///< Can we connect to this server or not? (based on server_revision)
	bool compatible;                                ///< Can we connect to this server or not? (based on server_revision _and_ grf_match
};

enum GameInfoNewGRFMode : uint8 {
	GAME_INFO_NEWGRF_MODE_NONE = 0, ///< Send/receive NetworkGameInfo without any NewGRF data.
	GAME_INFO_NEWGRF_MODE_SHORT,    ///< Send/receive NetworkGameInfo with only the ID + MD5 of NewGRFs.
	GAME_INFO_NEWGRF_MODE_FULL,     ///< Send/receive NetworkGameInfo with ID + MD5 + Name of NewGRFs.
};

extern NetworkServerGameInfo _network_game_info;

std::string_view GetNetworkRevisionString();
bool IsNetworkCompatibleVersion(std::string_view other);
void CheckGameCompatibility(NetworkGameInfo &ngi);

void FillStaticNetworkServerGameInfo();
const NetworkServerGameInfo *GetCurrentNetworkServerGameInfo();

void DeserializeGRFIdentifier(Packet *p, GRFIdentifier *grf);
void SerializeGRFIdentifier(Packet *p, const GRFIdentifier *grf);

void DeserializeNetworkGameInfo(Packet *p, NetworkGameInfo *info);
void SerializeNetworkGameInfo(Packet *p, const NetworkServerGameInfo *info, GameInfoNewGRFMode newgrf_mode);

#endif /* NETWORK_CORE_GAME_INFO_H */
