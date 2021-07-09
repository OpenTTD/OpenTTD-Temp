/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file game_info.cpp Functions to convert NetworkGameInfo to Packet and back.
 */

#include "../../stdafx.h"
#include "game_info.h"
#include "../../core/bitmath_func.hpp"
#include "../../company_base.h"
#include "../../date_func.h"
#include "../../debug.h"
#include "../../map_func.h"
#include "../../settings_type.h"
#include "../../string_func.h"
#include "../../rev.h"
#include "../network_func.h"
#include "../network.h"
#include "packet.h"
#include "../../game/game.hpp"
#include "../../game/game_info.hpp"

#include "../../safeguards.h"


/**
 * How many hex digits of the git hash to include in network revision string.
 * Determined as 10 hex digits + 2 characters for -g/-u/-m prefix.
 */
static const uint GITHASH_SUFFIX_LEN = 12;

NetworkServerGameInfo _network_game_info; ///< Information about our game.

/**
 * Get the network version string used by this build.
 * The returned string is guaranteed to be at most NETWORK_REVISON_LENGTH bytes including '\0' terminator.
 */
std::string_view GetNetworkRevisionString()
{
	static std::string network_revision;

	if (network_revision.empty()) {
		network_revision = _openttd_revision;
		if (_openttd_revision_tagged) {
			/* Tagged; do not mangle further, though ensure it's not too long. */
			if (network_revision.size() >= NETWORK_REVISION_LENGTH) network_revision.resize(NETWORK_REVISION_LENGTH - 1);
		} else {
			/* Not tagged; add the githash suffix while ensuring the string does not become too long. */
			assert(_openttd_revision_modified < 3);
			std::string githash_suffix = fmt::format("-{}{}", "gum"[_openttd_revision_modified], _openttd_revision_hash);
			if (githash_suffix.size() > GITHASH_SUFFIX_LEN) githash_suffix.resize(GITHASH_SUFFIX_LEN);

			/* Where did the hash start in the original string? Overwrite from that position, unless that would create a too long string. */
			size_t hash_end = network_revision.find_last_of('-');
			if (hash_end == std::string::npos) hash_end = network_revision.size();
			if (hash_end + githash_suffix.size() >= NETWORK_REVISION_LENGTH) hash_end = NETWORK_REVISION_LENGTH - githash_suffix.size() - 1;

			/* Replace the git hash in revision string. */
			network_revision.replace(hash_end, std::string::npos, githash_suffix);
		}
		assert(network_revision.size() < NETWORK_REVISION_LENGTH); // size does not include terminator, constant does, hence strictly less than
		Debug(net, 3, "Network revision name: {}", network_revision);
	}

	return network_revision;
}

/**
 * Extract the git hash from the revision string.
 * @param revision_string The revision string (formatted as DATE-BRANCH-GITHASH).
 * @return The git has part of the revision.
 */
static std::string_view ExtractNetworkRevisionHash(std::string_view revision_string)
{
	size_t index = revision_string.find_last_of('-');
	if (index == std::string::npos) return {};
	return revision_string.substr(index);
}

/**
 * Checks whether the given version string is compatible with our version.
 * First tries to match the full string, if that fails, attempts to compare just git hashes.
 * @param other the version string to compare to
 */
bool IsNetworkCompatibleVersion(std::string_view other)
{
	if (GetNetworkRevisionString() == other) return true;

	/* If this version is tagged, then the revision string must be a complete match,
	 * since there is no git hash suffix in it.
	 * This is needed to avoid situations like "1.9.0-beta1" comparing equal to "2.0.0-beta1".  */
	if (_openttd_revision_tagged) return false;

	std::string_view hash1 = ExtractNetworkRevisionHash(GetNetworkRevisionString());
	std::string_view hash2 = ExtractNetworkRevisionHash(other);
	return hash1 == hash2;
}

/**
 * Check if an game entry is compatible with our client.
 */
void CheckGameCompatibility(NetworkGameInfo &ngi)
{
	/* Check if we are allowed on this server based on the revision-check. */
	ngi.version_compatible = IsNetworkCompatibleVersion(ngi.server_revision);
	ngi.compatible = ngi.version_compatible;

	/* Check if we have all the GRFs on the client-system too. */
	for (const GRFConfig *c = ngi.grfconfig; c != nullptr; c = c->next) {
		if (c->status == GCS_NOT_FOUND) ngi.compatible = false;
	}
}

/**
 * Fill a NetworkServerGameInfo structure with the static content, or things
 * that are so static they can be updated on request from a settings change.
 */
void FillStaticNetworkServerGameInfo()
{
	_network_game_info.use_password   = !_settings_client.network.server_password.empty();
	_network_game_info.start_date     = ConvertYMDToDate(_settings_game.game_creation.starting_year, 0, 1);
	_network_game_info.clients_max    = _settings_client.network.max_clients;
	_network_game_info.companies_max  = _settings_client.network.max_companies;
	_network_game_info.spectators_max = _settings_client.network.max_spectators;
	_network_game_info.map_width      = MapSizeX();
	_network_game_info.map_height     = MapSizeY();
	_network_game_info.landscape      = _settings_game.game_creation.landscape;
	_network_game_info.dedicated      = _network_dedicated;
	_network_game_info.grfconfig      = _grfconfig;

	_network_game_info.server_name = _settings_client.network.server_name;
	_network_game_info.server_revision = GetNetworkRevisionString();
}

/**
 * Get the NetworkServerGameInfo structure with the latest information of the server.
 * @return The current NetworkServerGameInfo.
 */
const NetworkServerGameInfo *GetCurrentNetworkServerGameInfo()
{
	/* These variables are updated inside _network_game_info as if they are global variables:
	 *  - clients_on
	 *  - join_key
	 * These don't need to be updated manually here.
	 */
	_network_game_info.companies_on  = (byte)Company::GetNumItems();
	_network_game_info.spectators_on = NetworkSpectatorCount();
	_network_game_info.game_date     = _date;

	return &_network_game_info;
}

/**
 * Function that is called for every GRFConfig that is read when receiving
 * a NetworkGameInfo. Only grfid and md5sum are set, the rest is zero. This
 * function must set all appropriate fields. This GRF is later appended to
 * the grfconfig list of the NetworkGameInfo.
 * @param config The GRF to handle.
 * @param name The name of the GRF, if known (for example: the server send this info).
 */
static void HandleIncomingNetworkGameInfoGRFConfig(GRFConfig *config, std::string &name)
{
	/* Find the matching GRF file */
	const GRFConfig *f = FindGRFConfig(config->ident.grfid, FGCM_EXACT, config->ident.md5sum);
	if (f == nullptr) {
		/* Don't know the GRF, so mark game incompatible and the (possibly)
		 * already resolved name for this GRF. */
		config->name = FindUnknownGRFName(config->ident.grfid, config->ident.md5sum, true);
		if (strcmp(GetGRFStringFromGRFText(config->name), UNKNOWN_GRF_NAME_PLACEHOLDER) == 0 && !name.empty()) {
			AddGRFTextToList(config->name, name.c_str());
		}
		config->status = GCS_NOT_FOUND;
	} else {
		config->filename = f->filename;
		config->name = f->name;
		config->info = f->info;
		config->url = f->url;
	}
	SetBit(config->flags, GCF_COPY);
}

/**
 * Serializes the NetworkGameInfo struct to the packet.
 * @param p The packet to write the data to.
 * @param info The NetworkGameInfo struct to serialize from.
 * @param newgrf_mode The NewGRF mode of the NetworkGameInfo. See GameInfoNewGRFMode for details.
 */
void SerializeNetworkGameInfo(Packet *p, const NetworkServerGameInfo *info, GameInfoNewGRFMode newgrf_mode)
{
	p->Send_uint8(NETWORK_GAME_INFO_VERSION);

	/* Update the documentation in game_info.h on changes
	 * to the NetworkGameInfo wire-protocol! */

	/* NETWORK_GAME_INFO_VERSION = 6 */
	GameInfo *game_info = Game::GetInfo();
	p->Send_uint32(game_info == nullptr ? -1 : (uint32)game_info->GetVersion());
	p->Send_string(game_info == nullptr ? "" : game_info->GetName());

	/* NETWORK_GAME_INFO_VERSION = 5 */
	assert(info->join_key.empty() || info->join_key[0] == '+');
	p->Send_string(info->join_key.empty() ? "" : info->join_key.substr(1));
	p->Send_uint8(newgrf_mode);

	/* NETWORK_GAME_INFO_VERSION = 4 */
	if (newgrf_mode != GAME_INFO_NEWGRF_MODE_NONE) {
		/* Only send the GRF Identification (GRF_ID and MD5 checksum) of
		 * the GRFs that are needed, i.e. the ones that the server has
		 * selected in the NewGRF GUI and not the ones that are used due
		 * to the fact that they are in [newgrf-static] in openttd.cfg */
		const GRFConfig *c;
		uint count = 0;

		/* Count number of GRFs to send information about */
		for (c = info->grfconfig; c != nullptr; c = c->next) {
			if (!HasBit(c->flags, GCF_STATIC)) count++;
		}
		p->Send_uint8 (count); // Send number of GRFs

		/* Send actual GRF Identifications */
		for (c = info->grfconfig; c != nullptr; c = c->next) {
			if (!HasBit(c->flags, GCF_STATIC)) {
				SerializeGRFIdentifier(p, &c->ident);
				if (newgrf_mode == GAME_INFO_NEWGRF_MODE_FULL) {
					p->Send_string(c->GetName());
				}
			}
		}
	}

	/* NETWORK_GAME_INFO_VERSION = 3 */
	p->Send_uint32(info->game_date);
	p->Send_uint32(info->start_date);

	/* NETWORK_GAME_INFO_VERSION = 2 */
	p->Send_uint8 (info->companies_max);
	p->Send_uint8 (info->companies_on);
	p->Send_uint8 (info->spectators_max);

	/* NETWORK_GAME_INFO_VERSION = 1 */
	p->Send_string(info->server_name);
	p->Send_string(info->server_revision);
	p->Send_bool  (info->use_password);
	p->Send_uint8 (info->clients_max);
	p->Send_uint8 (info->clients_on);
	p->Send_uint8 (info->spectators_on);
	p->Send_uint16(info->map_width);
	p->Send_uint16(info->map_height);
	p->Send_uint8 (info->landscape);
	p->Send_bool  (info->dedicated);
}

/**
 * Deserializes the NetworkGameInfo struct from the packet.
 * @param p    the packet to read the data from.
 * @param info the NetworkGameInfo to deserialize into.
 */
void DeserializeNetworkGameInfo(Packet *p, NetworkGameInfo *info)
{
	static const Date MAX_DATE = ConvertYMDToDate(MAX_YEAR, 11, 31); // December is month 11

	byte game_info_version = p->Recv_uint8();

	/*
	 *              Please observe the order.
	 * The parts must be read in the same order as they are sent!
	 */

	/* Update the documentation in game_info.h on changes
	 * to the NetworkGameInfo wire-protocol! */

	GameInfoNewGRFMode newgrf_mode = GAME_INFO_NEWGRF_MODE_SHORT;
	switch (game_info_version) {
		case 6: {
			info->gamescript_version = (int)p->Recv_uint32();
			info->gamescript_name = p->Recv_string(NETWORK_NAME_LENGTH);
			FALLTHROUGH;
		}

		case 5: {
			std::string join_key = p->Recv_string(NETWORK_JOIN_KEY_LENGTH);
			info->join_key = join_key.empty() ? "" : "+" + join_key;
			newgrf_mode = (GameInfoNewGRFMode)p->Recv_uint8();
			FALLTHROUGH;
		}

		case 4: {
			if (newgrf_mode != GAME_INFO_NEWGRF_MODE_NONE) {
				GRFConfig **dst = &info->grfconfig;
				uint i;
				uint num_grfs = p->Recv_uint8();

				/* Broken/bad data. It cannot have that many NewGRFs. */
				if (num_grfs > NETWORK_MAX_GRF_COUNT) return;

				for (i = 0; i < num_grfs; i++) {
					GRFConfig *c = new GRFConfig();
					DeserializeGRFIdentifier(p, &c->ident);
					std::string name = {};
					if (newgrf_mode == GAME_INFO_NEWGRF_MODE_FULL) {
						name = p->Recv_string(NETWORK_GRF_NAME_LENGTH);
					}
					HandleIncomingNetworkGameInfoGRFConfig(c, name);

					/* Append GRFConfig to the list */
					*dst = c;
					dst = &c->next;
				}
			}
			FALLTHROUGH;
		}

		case 3:
			info->game_date      = Clamp(p->Recv_uint32(), 0, MAX_DATE);
			info->start_date     = Clamp(p->Recv_uint32(), 0, MAX_DATE);
			FALLTHROUGH;

		case 2:
			info->companies_max  = p->Recv_uint8 ();
			info->companies_on   = p->Recv_uint8 ();
			info->spectators_max = p->Recv_uint8 ();
			FALLTHROUGH;

		case 1:
			info->server_name = p->Recv_string(NETWORK_NAME_LENGTH);
			info->server_revision = p->Recv_string(NETWORK_REVISION_LENGTH);
			if (game_info_version < 5) p->Recv_uint8 (); // Used to contain server-lang.
			info->use_password   = p->Recv_bool  ();
			info->clients_max    = p->Recv_uint8 ();
			info->clients_on     = p->Recv_uint8 ();
			info->spectators_on  = p->Recv_uint8 ();
			if (game_info_version < 3) { // 16 bits dates got scrapped and are read earlier
				info->game_date    = p->Recv_uint16() + DAYS_TILL_ORIGINAL_BASE_YEAR;
				info->start_date   = p->Recv_uint16() + DAYS_TILL_ORIGINAL_BASE_YEAR;
			}
			if (game_info_version < 5) while (p->Recv_uint8() != 0) {} // Used to contain the map-name.
			info->map_width      = p->Recv_uint16();
			info->map_height     = p->Recv_uint16();
			info->landscape      = p->Recv_uint8 ();
			info->dedicated      = p->Recv_bool  ();

			if (info->landscape >= NETWORK_NUM_LANDSCAPES) info->landscape = 0;
	}
}

/**
 * Serializes the GRFIdentifier (GRF ID and MD5 checksum) to the packet
 * @param p    the packet to write the data to.
 * @param grf  the GRFIdentifier to serialize.
 */
void SerializeGRFIdentifier(Packet *p, const GRFIdentifier *grf)
{
	uint j;
	p->Send_uint32(grf->grfid);
	for (j = 0; j < sizeof(grf->md5sum); j++) {
		p->Send_uint8(grf->md5sum[j]);
	}
}

/**
 * Deserializes the GRFIdentifier (GRF ID and MD5 checksum) from the packet
 * @param p    the packet to read the data from.
 * @param grf  the GRFIdentifier to deserialize.
 */
void DeserializeGRFIdentifier(Packet *p, GRFIdentifier *grf)
{
	uint j;
	grf->grfid = p->Recv_uint32();
	for (j = 0; j < sizeof(grf->md5sum); j++) {
		grf->md5sum[j] = p->Recv_uint8();
	}
}
