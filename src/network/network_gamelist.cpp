/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file network_gamelist.cpp This file handles the GameList
 * Also, it handles the request to a server for data about the server
 */

#include "../stdafx.h"
#include "../debug.h"
#include "../window_func.h"
#include "network_internal.h"
#include "network_udp.h"
#include "network_gamelist.h"
#include <atomic>

#include "../safeguards.h"

NetworkGameList *_network_game_list = nullptr; ///< Game list of this client.
int _network_game_list_version = 0; ///< Current version of all items in the list.

/**
 * Add a new item to the linked gamelist. If the IP and Port match
 * return the existing item instead of adding it again
 * @param connection_string the address of the to-be added item
 * @return a point to the newly added or already existing item
 */
NetworkGameList *NetworkGameListAddItem(const std::string &connection_string)
{
	NetworkGameList *item, *prev_item;

	/* Parse the connection string to ensure the default port is there. */
	const std::string resolved_connection_string = ServerAddress::Parse(connection_string, NETWORK_DEFAULT_PORT).connection_string;

	prev_item = nullptr;
	for (item = _network_game_list; item != nullptr; item = item->next) {
		if (item->connection_string == resolved_connection_string) return item;
		prev_item = item;
	}

	item = new NetworkGameList(resolved_connection_string);
	item->version = _network_game_list_version;

	if (prev_item == nullptr) {
		_network_game_list = item;
	} else {
		prev_item->next = item;
	}

	UpdateNetworkGameWindow();

	return item;
}

/**
 * Remove an item from the gamelist linked list
 * @param remove pointer to the item to be removed
 */
void NetworkGameListRemoveItem(NetworkGameList *remove)
{
	NetworkGameList *prev_item = nullptr;
	for (NetworkGameList *item = _network_game_list; item != nullptr; item = item->next) {
		if (remove == item) {
			if (prev_item == nullptr) {
				_network_game_list = remove->next;
			} else {
				prev_item->next = remove->next;
			}

			/* Remove GRFConfig information */
			ClearGRFConfigList(&remove->info.grfconfig);
			delete remove;

			NetworkRebuildHostList();
			UpdateNetworkGameWindow();
			return;
		}
		prev_item = item;
	}
}

/**
 * Remove all servers that have not recently been updated.
 * Call this after you received all the servers from the Game Coordinator, so
 * the ones that are no longer listed are removed.
 */
void NetworkGameListRemoveExpired()
{
	NetworkGameList **prev_item = &_network_game_list;

	for (NetworkGameList *item = _network_game_list; item != nullptr;) {
		if (!item->manually && item->version < _network_game_list_version) {
			NetworkGameList *remove = item;
			item = item->next;
			*prev_item = item;

			/* Remove GRFConfig information */
			ClearGRFConfigList(&remove->info.grfconfig);
			delete remove;
		} else {
			prev_item = &item->next;
			item = item->next;
		}
	}

	UpdateNetworkGameWindow();
}

/**
 * Rebuild the GRFConfig's of the servers in the game list as we did
 * a rescan and might have found new NewGRFs.
 */
void NetworkAfterNewGRFScan()
{
	for (NetworkGameList *item = _network_game_list; item != nullptr; item = item->next) {
		/* Reset compatibility state */
		item->info.compatible = item->info.version_compatible;

		for (GRFConfig *c = item->info.grfconfig; c != nullptr; c = c->next) {
			assert(HasBit(c->flags, GCF_COPY));

			const GRFConfig *f = FindGRFConfig(c->ident.grfid, FGCM_EXACT, c->ident.md5sum);
			if (f == nullptr) {
				/* Don't know the GRF, so mark game incompatible and the (possibly)
				 * already resolved name for this GRF (another server has sent the
				 * name of the GRF already. */
				c->name = FindUnknownGRFName(c->ident.grfid, c->ident.md5sum, true);
				c->status = GCS_NOT_FOUND;

				/* If we miss a file, we're obviously incompatible. */
				item->info.compatible = false;
			} else {
				c->filename = f->filename;
				c->name = f->name;
				c->info = f->info;
				c->status = GCS_UNKNOWN;
			}
		}
	}

	InvalidateWindowClassesData(WC_NETWORK_WINDOW);
}
