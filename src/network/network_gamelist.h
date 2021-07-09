/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file network_gamelist.h Handling of the list of games. */

#ifndef NETWORK_GAMELIST_H
#define NETWORK_GAMELIST_H

#include "core/address.h"
#include "core/game_info.h"
#include "network_type.h"

/** Structure with information shown in the game list (GUI) */
struct NetworkGameList {
	NetworkGameList(const std::string &connection_string) : connection_string(connection_string) {}

	NetworkGameInfo info = {};       ///< The game information of this server
	std::string connection_string;   ///< Address of the server
	bool online = false;             ///< False if the server did not respond (default status)
	bool manually = false;           ///< True if the server was added manually
	uint8 retries = 0;               ///< Number of retries (to stop requerying)
	int version = 0;                 ///< Used to see which servers are no longer available on the Game Coordinator and can be removed.
	NetworkGameList *next = nullptr; ///< Next pointer to make a linked game list
};

extern NetworkGameList *_network_game_list;
extern int _network_game_list_version;

NetworkGameList *NetworkGameListAddItem(const std::string &connection_string);
void NetworkGameListRemoveItem(NetworkGameList *remove);
void NetworkGameListRemoveExpired();

#endif /* NETWORK_GAMELIST_H */
