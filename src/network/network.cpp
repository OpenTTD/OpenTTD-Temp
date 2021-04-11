/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file network.cpp Base functions for networking support. */

#include "../stdafx.h"

#include "../strings_func.h"
#include "../command_func.h"
#include "../date_func.h"
#include "network_admin.h"
#include "network_client.h"
#include "network_server.h"
#include "network_content.h"
#include "network_udp.h"
#include "network_gamelist.h"
#include "network_base.h"
#include "core/udp.h"
#include "core/host.h"
#include "network_gui.h"
#include "../console_func.h"
#include "../3rdparty/md5/md5.h"
#include "../core/random_func.hpp"
#include "../window_func.h"
#include "../company_func.h"
#include "../company_base.h"
#include "../landscape_type.h"
#include "../rev.h"
#include "../core/pool_func.hpp"
#include "../gfx_func.h"
#include "../error.h"

#include "../safeguards.h"

#ifdef DEBUG_DUMP_COMMANDS
#include "../fileio_func.h"
/** When running the server till the wait point, run as fast as we can! */
bool _ddc_fastforward = true;
#endif /* DEBUG_DUMP_COMMANDS */

/** Make sure both pools have the same size. */
static_assert(NetworkClientInfoPool::MAX_SIZE == NetworkClientSocketPool::MAX_SIZE);

/** The pool with client information. */
NetworkClientInfoPool _networkclientinfo_pool("NetworkClientInfo");
INSTANTIATE_POOL_METHODS(NetworkClientInfo)

bool _networking;         ///< are we in networking mode?
bool _network_server;     ///< network-server is active
bool _network_available;  ///< is network mode available?
bool _network_dedicated;  ///< are we a dedicated server?
bool _is_network_server;  ///< Does this client wants to be a network-server?
NetworkServerGameInfo _network_game_info; ///< Information about our game.
NetworkCompanyState *_network_company_states = nullptr; ///< Statistics about some companies.
ClientID _network_own_client_id;      ///< Our client identifier.
ClientID _redirect_console_to_client; ///< If not invalid, redirect the console output to a client.
bool _network_need_advertise;         ///< Whether we need to advertise.
uint8 _network_reconnect;             ///< Reconnect timeout
StringList _network_bind_list;        ///< The addresses to bind on.
StringList _network_host_list;        ///< The servers we know.
StringList _network_ban_list;         ///< The banned clients.
uint32 _frame_counter_server;         ///< The frame_counter of the server, if in network-mode
uint32 _frame_counter_max;            ///< To where we may go with our clients
uint32 _frame_counter;                ///< The current frame.
uint32 _last_sync_frame;              ///< Used in the server to store the last time a sync packet was sent to clients.
NetworkAddressList _broadcast_list;   ///< List of broadcast addresses.
uint32 _sync_seed_1;                  ///< Seed to compare during sync checks.
#ifdef NETWORK_SEND_DOUBLE_SEED
uint32 _sync_seed_2;                  ///< Second part of the seed.
#endif
uint32 _sync_frame;                   ///< The frame to perform the sync check.
bool _network_first_time;             ///< Whether we have finished joining or not.
bool _network_udp_server;             ///< Is the UDP server started?
uint16 _network_udp_broadcast;        ///< Timeout for the UDP broadcasts.
uint8 _network_advertise_retries;     ///< The number of advertisement retries we did.
CompanyMask _network_company_passworded; ///< Bitmask of the password status of all companies.

/* Check whether NETWORK_NUM_LANDSCAPES is still in sync with NUM_LANDSCAPE */
static_assert((int)NETWORK_NUM_LANDSCAPES == (int)NUM_LANDSCAPE);
static_assert((int)NETWORK_COMPANY_NAME_LENGTH == MAX_LENGTH_COMPANY_NAME_CHARS * MAX_CHAR_LENGTH);

extern NetworkUDPSocketHandler *_udp_client_socket; ///< udp client socket
extern NetworkUDPSocketHandler *_udp_server_socket; ///< udp server socket
extern NetworkUDPSocketHandler *_udp_master_socket; ///< udp master socket

/** The amount of clients connected */
byte _network_clients_connected = 0;

/* Some externs / forwards */
extern void StateGameLoop();

/**
 * Return whether there is any client connected or trying to connect at all.
 * @return whether we have any client activity
 */
bool HasClients()
{
	return !NetworkClientSocket::Iterate().empty();
}

/**
 * Basically a client is leaving us right now.
 */
NetworkClientInfo::~NetworkClientInfo()
{
	/* Delete the chat window, if you were chatting with this client. */
	InvalidateWindowData(WC_SEND_NETWORK_MSG, DESTTYPE_CLIENT, this->client_id);
}

/**
 * Return the CI given it's client-identifier
 * @param client_id the ClientID to search for
 * @return return a pointer to the corresponding NetworkClientInfo struct or nullptr when not found
 */
/* static */ NetworkClientInfo *NetworkClientInfo::GetByClientID(ClientID client_id)
{
	for (NetworkClientInfo *ci : NetworkClientInfo::Iterate()) {
		if (ci->client_id == client_id) return ci;
	}

	return nullptr;
}

/**
 * Return the client state given it's client-identifier
 * @param client_id the ClientID to search for
 * @return return a pointer to the corresponding NetworkClientSocket struct or nullptr when not found
 */
/* static */ ServerNetworkGameSocketHandler *ServerNetworkGameSocketHandler::GetByClientID(ClientID client_id)
{
	for (NetworkClientSocket *cs : NetworkClientSocket::Iterate()) {
		if (cs->client_id == client_id) return cs;
	}

	return nullptr;
}

byte NetworkSpectatorCount()
{
	byte count = 0;

	for (const NetworkClientInfo *ci : NetworkClientInfo::Iterate()) {
		if (ci->client_playas == COMPANY_SPECTATOR) count++;
	}

	/* Don't count a dedicated server as spectator */
	if (_network_dedicated) count--;

	return count;
}

/**
 * Change the company password of a given company.
 * @param company_id ID of the company the password should be changed for.
 * @param password The unhashed password we like to set ('*' or '' resets the password)
 * @return The password.
 */
const char *NetworkChangeCompanyPassword(CompanyID company_id, const char *password)
{
	if (strcmp(password, "*") == 0) password = "";

	if (_network_server) {
		NetworkServerSetCompanyPassword(company_id, password, false);
	} else {
		NetworkClientSetCompanyPassword(password);
	}

	return password;
}

/**
 * Hash the given password using server ID and game seed.
 * @param password Password to hash.
 * @param password_server_id Server ID.
 * @param password_game_seed Game seed.
 * @return The hashed password.
 */
const char *GenerateCompanyPasswordHash(const char *password, const char *password_server_id, uint32 password_game_seed)
{
	if (StrEmpty(password)) return password;

	char salted_password[NETWORK_SERVER_ID_LENGTH];

	memset(salted_password, 0, sizeof(salted_password));
	seprintf(salted_password, lastof(salted_password), "%s", password);
	/* Add the game seed and the server's ID as the salt. */
	for (uint i = 0; i < NETWORK_SERVER_ID_LENGTH - 1; i++) {
		salted_password[i] ^= password_server_id[i] ^ (password_game_seed >> (i % 32));
	}

	Md5 checksum;
	uint8 digest[16];
	static char hashed_password[NETWORK_SERVER_ID_LENGTH];

	/* Generate the MD5 hash */
	checksum.Append(salted_password, sizeof(salted_password) - 1);
	checksum.Finish(digest);

	for (int di = 0; di < 16; di++) seprintf(hashed_password + di * 2, lastof(hashed_password), "%02x", digest[di]);

	return hashed_password;
}

/**
 * Check if the company we want to join requires a password.
 * @param company_id id of the company we want to check the 'passworded' flag for.
 * @return true if the company requires a password.
 */
bool NetworkCompanyIsPassworded(CompanyID company_id)
{
	return HasBit(_network_company_passworded, company_id);
}

/* This puts a text-message to the console, or in the future, the chat-box,
 *  (to keep it all a bit more general)
 * If 'self_send' is true, this is the client who is sending the message */
void NetworkTextMessage(NetworkAction action, TextColour colour, bool self_send, const char *name, const char *str, int64 data)
{
	StringID strid;
	switch (action) {
		case NETWORK_ACTION_SERVER_MESSAGE:
			/* Ignore invalid messages */
			strid = STR_NETWORK_SERVER_MESSAGE;
			colour = CC_DEFAULT;
			break;
		case NETWORK_ACTION_COMPANY_SPECTATOR:
			colour = CC_DEFAULT;
			strid = STR_NETWORK_MESSAGE_CLIENT_COMPANY_SPECTATE;
			break;
		case NETWORK_ACTION_COMPANY_JOIN:
			colour = CC_DEFAULT;
			strid = STR_NETWORK_MESSAGE_CLIENT_COMPANY_JOIN;
			break;
		case NETWORK_ACTION_COMPANY_NEW:
			colour = CC_DEFAULT;
			strid = STR_NETWORK_MESSAGE_CLIENT_COMPANY_NEW;
			break;
		case NETWORK_ACTION_JOIN:
			/* Show the Client ID for the server but not for the client. */
			strid = _network_server ? STR_NETWORK_MESSAGE_CLIENT_JOINED_ID :  STR_NETWORK_MESSAGE_CLIENT_JOINED;
			break;
		case NETWORK_ACTION_LEAVE:          strid = STR_NETWORK_MESSAGE_CLIENT_LEFT; break;
		case NETWORK_ACTION_NAME_CHANGE:    strid = STR_NETWORK_MESSAGE_NAME_CHANGE; break;
		case NETWORK_ACTION_GIVE_MONEY:     strid = STR_NETWORK_MESSAGE_GIVE_MONEY; break;
		case NETWORK_ACTION_CHAT_COMPANY:   strid = self_send ? STR_NETWORK_CHAT_TO_COMPANY : STR_NETWORK_CHAT_COMPANY; break;
		case NETWORK_ACTION_CHAT_CLIENT:    strid = self_send ? STR_NETWORK_CHAT_TO_CLIENT  : STR_NETWORK_CHAT_CLIENT;  break;
		case NETWORK_ACTION_KICKED:         strid = STR_NETWORK_MESSAGE_KICKED; break;
		default:                            strid = STR_NETWORK_CHAT_ALL; break;
	}

	char message[1024];
	SetDParamStr(0, name);
	SetDParamStr(1, str);
	SetDParam(2, data);

	/* All of these strings start with "***". These characters are interpreted as both left-to-right and
	 * right-to-left characters depending on the context. As the next text might be an user's name, the
	 * user name's characters will influence the direction of the "***" instead of the language setting
	 * of the game. Manually set the direction of the "***" by inserting a text-direction marker. */
	char *msg_ptr = message + Utf8Encode(message, _current_text_dir == TD_LTR ? CHAR_TD_LRM : CHAR_TD_RLM);
	GetString(msg_ptr, strid, lastof(message));

	DEBUG(desync, 1, "msg: %08x; %02x; %s", _date, _date_fract, message);
	IConsolePrintF(colour, "%s", message);
	NetworkAddChatMessage((TextColour)colour, _settings_client.gui.network_chat_timeout, "%s", message);
}

/* Calculate the frame-lag of a client */
uint NetworkCalculateLag(const NetworkClientSocket *cs)
{
	int lag = cs->last_frame_server - cs->last_frame;
	/* This client has missed his ACK packet after 1 DAY_TICKS..
	 *  so we increase his lag for every frame that passes!
	 * The packet can be out by a max of _net_frame_freq */
	if (cs->last_frame_server + DAY_TICKS + _settings_client.network.frame_freq < _frame_counter) {
		lag += _frame_counter - (cs->last_frame_server + DAY_TICKS + _settings_client.network.frame_freq);
	}
	return lag;
}


/* There was a non-recoverable error, drop back to the main menu with a nice
 *  error */
void NetworkError(StringID error_string)
{
	_switch_mode = SM_MENU;
	ShowErrorMessage(error_string, INVALID_STRING_ID, WL_CRITICAL);
}

/**
 * Retrieve the string id of an internal error number
 * @param err NetworkErrorCode
 * @return the StringID
 */
StringID GetNetworkErrorMsg(NetworkErrorCode err)
{
	/* List of possible network errors, used by
	 * PACKET_SERVER_ERROR and PACKET_CLIENT_ERROR */
	static const StringID network_error_strings[] = {
		STR_NETWORK_ERROR_CLIENT_GENERAL,
		STR_NETWORK_ERROR_CLIENT_DESYNC,
		STR_NETWORK_ERROR_CLIENT_SAVEGAME,
		STR_NETWORK_ERROR_CLIENT_CONNECTION_LOST,
		STR_NETWORK_ERROR_CLIENT_PROTOCOL_ERROR,
		STR_NETWORK_ERROR_CLIENT_NEWGRF_MISMATCH,
		STR_NETWORK_ERROR_CLIENT_NOT_AUTHORIZED,
		STR_NETWORK_ERROR_CLIENT_NOT_EXPECTED,
		STR_NETWORK_ERROR_CLIENT_WRONG_REVISION,
		STR_NETWORK_ERROR_CLIENT_NAME_IN_USE,
		STR_NETWORK_ERROR_CLIENT_WRONG_PASSWORD,
		STR_NETWORK_ERROR_CLIENT_COMPANY_MISMATCH,
		STR_NETWORK_ERROR_CLIENT_KICKED,
		STR_NETWORK_ERROR_CLIENT_CHEATER,
		STR_NETWORK_ERROR_CLIENT_SERVER_FULL,
		STR_NETWORK_ERROR_CLIENT_TOO_MANY_COMMANDS,
		STR_NETWORK_ERROR_CLIENT_TIMEOUT_PASSWORD,
		STR_NETWORK_ERROR_CLIENT_TIMEOUT_COMPUTER,
		STR_NETWORK_ERROR_CLIENT_TIMEOUT_MAP,
		STR_NETWORK_ERROR_CLIENT_TIMEOUT_JOIN,
		STR_NETWORK_ERROR_CLIENT_UNSUPPORTED_FORMAT,
	};
	static_assert(lengthof(network_error_strings) == NETWORK_ERROR_END);

	if (err >= (ptrdiff_t)lengthof(network_error_strings)) err = NETWORK_ERROR_GENERAL;

	return network_error_strings[err];
}

/**
 * Handle the pause mode change so we send the right messages to the chat.
 * @param prev_mode The previous pause mode.
 * @param changed_mode The pause mode that got changed.
 */
void NetworkHandlePauseChange(PauseMode prev_mode, PauseMode changed_mode)
{
	if (!_networking) return;

	switch (changed_mode) {
		case PM_PAUSED_NORMAL:
		case PM_PAUSED_JOIN:
		case PM_PAUSED_GAME_SCRIPT:
		case PM_PAUSED_ACTIVE_CLIENTS:
		case PM_PAUSED_LINK_GRAPH: {
			bool changed = ((_pause_mode == PM_UNPAUSED) != (prev_mode == PM_UNPAUSED));
			bool paused = (_pause_mode != PM_UNPAUSED);
			if (!paused && !changed) return;

			StringID str;
			if (!changed) {
				int i = -1;

				if ((_pause_mode & PM_PAUSED_NORMAL) != PM_UNPAUSED)         SetDParam(++i, STR_NETWORK_SERVER_MESSAGE_GAME_REASON_MANUAL);
				if ((_pause_mode & PM_PAUSED_JOIN) != PM_UNPAUSED)           SetDParam(++i, STR_NETWORK_SERVER_MESSAGE_GAME_REASON_CONNECTING_CLIENTS);
				if ((_pause_mode & PM_PAUSED_GAME_SCRIPT) != PM_UNPAUSED)    SetDParam(++i, STR_NETWORK_SERVER_MESSAGE_GAME_REASON_GAME_SCRIPT);
				if ((_pause_mode & PM_PAUSED_ACTIVE_CLIENTS) != PM_UNPAUSED) SetDParam(++i, STR_NETWORK_SERVER_MESSAGE_GAME_REASON_NOT_ENOUGH_PLAYERS);
				if ((_pause_mode & PM_PAUSED_LINK_GRAPH) != PM_UNPAUSED)     SetDParam(++i, STR_NETWORK_SERVER_MESSAGE_GAME_REASON_LINK_GRAPH);
				str = STR_NETWORK_SERVER_MESSAGE_GAME_STILL_PAUSED_1 + i;
			} else {
				switch (changed_mode) {
					case PM_PAUSED_NORMAL:         SetDParam(0, STR_NETWORK_SERVER_MESSAGE_GAME_REASON_MANUAL); break;
					case PM_PAUSED_JOIN:           SetDParam(0, STR_NETWORK_SERVER_MESSAGE_GAME_REASON_CONNECTING_CLIENTS); break;
					case PM_PAUSED_GAME_SCRIPT:    SetDParam(0, STR_NETWORK_SERVER_MESSAGE_GAME_REASON_GAME_SCRIPT); break;
					case PM_PAUSED_ACTIVE_CLIENTS: SetDParam(0, STR_NETWORK_SERVER_MESSAGE_GAME_REASON_NOT_ENOUGH_PLAYERS); break;
					case PM_PAUSED_LINK_GRAPH:     SetDParam(0, STR_NETWORK_SERVER_MESSAGE_GAME_REASON_LINK_GRAPH); break;
					default: NOT_REACHED();
				}
				str = paused ? STR_NETWORK_SERVER_MESSAGE_GAME_PAUSED : STR_NETWORK_SERVER_MESSAGE_GAME_UNPAUSED;
			}

			char buffer[DRAW_STRING_BUFFER];
			GetString(buffer, str, lastof(buffer));
			NetworkTextMessage(NETWORK_ACTION_SERVER_MESSAGE, CC_DEFAULT, false, nullptr, buffer);
			break;
		}

		default:
			return;
	}
}


/**
 * Helper function for the pause checkers. If pause is true and the
 * current pause mode isn't set the game will be paused, if it it false
 * and the pause mode is set the game will be unpaused. In the other
 * cases nothing happens to the pause state.
 * @param pause whether we'd like to pause
 * @param pm the mode which we would like to pause with
 */
static void CheckPauseHelper(bool pause, PauseMode pm)
{
	if (pause == ((_pause_mode & pm) != PM_UNPAUSED)) return;

	DoCommandP(0, pm, pause ? 1 : 0, CMD_PAUSE);
}

/**
 * Counts the number of active clients connected.
 * It has to be in STATUS_ACTIVE and not a spectator
 * @return number of active clients
 */
static uint NetworkCountActiveClients()
{
	uint count = 0;

	for (const NetworkClientSocket *cs : NetworkClientSocket::Iterate()) {
		if (cs->status != NetworkClientSocket::STATUS_ACTIVE) continue;
		if (!Company::IsValidID(cs->GetInfo()->client_playas)) continue;
		count++;
	}

	return count;
}

/**
 * Check if the minimum number of active clients has been reached and pause or unpause the game as appropriate
 */
static void CheckMinActiveClients()
{
	if ((_pause_mode & PM_PAUSED_ERROR) != PM_UNPAUSED ||
			!_network_dedicated ||
			(_settings_client.network.min_active_clients == 0 && (_pause_mode & PM_PAUSED_ACTIVE_CLIENTS) == PM_UNPAUSED)) {
		return;
	}
	CheckPauseHelper(NetworkCountActiveClients() < _settings_client.network.min_active_clients, PM_PAUSED_ACTIVE_CLIENTS);
}

/**
 * Checks whether there is a joining client
 * @return true iff one client is joining (but not authorizing)
 */
static bool NetworkHasJoiningClient()
{
	for (const NetworkClientSocket *cs : NetworkClientSocket::Iterate()) {
		if (cs->status >= NetworkClientSocket::STATUS_AUTHORIZED && cs->status < NetworkClientSocket::STATUS_ACTIVE) return true;
	}

	return false;
}

/**
 * Check whether we should pause on join
 */
static void CheckPauseOnJoin()
{
	if ((_pause_mode & PM_PAUSED_ERROR) != PM_UNPAUSED ||
			(!_settings_client.network.pause_on_join && (_pause_mode & PM_PAUSED_JOIN) == PM_UNPAUSED)) {
		return;
	}
	CheckPauseHelper(NetworkHasJoiningClient(), PM_PAUSED_JOIN);
}

/**
 * Converts a string to ip/port/company
 *  Format: IP:port#company
 *
 * connection_string will be re-terminated to separate out the hostname, and company and port will
 * be set to the company and port strings given by the user, inside the memory area originally
 * occupied by connection_string.
 */
void ParseConnectionString(const char **company, const char **port, char *connection_string)
{
	bool ipv6 = (strchr(connection_string, ':') != strrchr(connection_string, ':'));
	char *p;
	for (p = connection_string; *p != '\0'; p++) {
		switch (*p) {
			case '[':
				ipv6 = true;
				break;

			case ']':
				ipv6 = false;
				break;

			case '#':
				*company = p + 1;
				*p = '\0';
				break;

			case ':':
				if (ipv6) break;
				*port = p + 1;
				*p = '\0';
				break;
		}
	}
}

/**
 * Handle the accepting of a connection to the server.
 * @param s The socket of the new connection.
 * @param address The address of the peer.
 */
/* static */ void ServerNetworkGameSocketHandler::AcceptConnection(SOCKET s, const NetworkAddress &address)
{
	/* Register the login */
	_network_clients_connected++;

	SetWindowDirty(WC_CLIENT_LIST, 0);
	ServerNetworkGameSocketHandler *cs = new ServerNetworkGameSocketHandler(s);
	cs->client_address = address; // Save the IP of the client
}

/**
 * Resets the pools used for network clients, and the admin pool if needed.
 * @param close_admins Whether the admin pool has to be cleared as well.
 */
static void InitializeNetworkPools(bool close_admins = true)
{
	PoolBase::Clean(PT_NCLIENT | (close_admins ? PT_NADMIN : PT_NONE));
}

/**
 * Close current connections.
 * @param close_admins Whether the admin connections have to be closed as well.
 */
void NetworkClose(bool close_admins)
{
	if (_network_server) {
		if (close_admins) {
			for (ServerNetworkAdminSocketHandler *as : ServerNetworkAdminSocketHandler::Iterate()) {
				as->CloseConnection(true);
			}
		}

		for (NetworkClientSocket *cs : NetworkClientSocket::Iterate()) {
			cs->CloseConnection(NETWORK_RECV_STATUS_CONN_LOST);
		}
		ServerNetworkGameSocketHandler::CloseListeners();
		ServerNetworkAdminSocketHandler::CloseListeners();
	} else if (MyClient::my_client != nullptr) {
		MyClient::SendQuit();
		MyClient::my_client->CloseConnection(NETWORK_RECV_STATUS_CONN_LOST);
	}

	TCPConnecter::KillAll();

	_networking = false;
	_network_server = false;

	NetworkFreeLocalCommandQueue();

	free(_network_company_states);
	_network_company_states = nullptr;

	InitializeNetworkPools(close_admins);
}

/* Initializes the network (cleans sockets and stuff) */
static void NetworkInitialize(bool close_admins = true)
{
	InitializeNetworkPools(close_admins);
	NetworkUDPInitialize();

	_sync_frame = 0;
	_network_first_time = true;

	_network_reconnect = 0;
}

/** Non blocking connection create to query servers */
class TCPQueryConnecter : TCPConnecter {
public:
	TCPQueryConnecter(const NetworkAddress &address) : TCPConnecter(address) {}

	void OnFailure() override
	{
		NetworkDisconnect();
	}

	void OnConnect(SOCKET s) override
	{
		_networking = true;
		new ClientNetworkGameSocketHandler(s);
		MyClient::SendCompanyInformationQuery();
	}
};

/* Query a server to fetch his game-info
 *  If game_info is true, only the gameinfo is fetched,
 *   else only the client_info is fetched */
void NetworkTCPQueryServer(NetworkAddress address)
{
	if (!_network_available) return;

	NetworkDisconnect();
	NetworkInitialize();

	new TCPQueryConnecter(address);
}

/* Validates an address entered as a string and adds the server to
 * the list. If you use this function, the games will be marked
 * as manually added. */
void NetworkAddServer(const char *b)
{
	if (*b != '\0') {
		const char *port = nullptr;
		const char *company = nullptr;
		char host[NETWORK_HOSTNAME_LENGTH];
		uint16 rport;

		strecpy(host, b, lastof(host));

		strecpy(_settings_client.network.connect_to_ip, b, lastof(_settings_client.network.connect_to_ip));
		rport = NETWORK_DEFAULT_PORT;

		ParseConnectionString(&company, &port, host);
		if (port != nullptr) rport = atoi(port);

		NetworkUDPQueryServer(NetworkAddress(host, rport), true);
	}
}

/**
 * Get the addresses to bind to.
 * @param addresses the list to write to.
 * @param port the port to bind to.
 */
void GetBindAddresses(NetworkAddressList *addresses, uint16 port)
{
	for (const auto &iter : _network_bind_list) {
		addresses->emplace_back(iter.c_str(), port);
	}

	/* No address, so bind to everything. */
	if (addresses->size() == 0) {
		addresses->emplace_back("", port);
	}
}

/* Generates the list of manually added hosts from NetworkGameList and
 * dumps them into the array _network_host_list. This array is needed
 * by the function that generates the config file. */
void NetworkRebuildHostList()
{
	_network_host_list.clear();

	for (NetworkGameList *item = _network_game_list; item != nullptr; item = item->next) {
		if (item->manually) _network_host_list.emplace_back(item->address.GetAddressAsString(false));
	}
}

/** Non blocking connection create to actually connect to servers */
class TCPClientConnecter : TCPConnecter {
public:
	TCPClientConnecter(const NetworkAddress &address) : TCPConnecter(address) {}

	void OnFailure() override
	{
		NetworkError(STR_NETWORK_ERROR_NOCONNECTION);
	}

	void OnConnect(SOCKET s) override
	{
		_networking = true;
		new ClientNetworkGameSocketHandler(s);
		IConsoleCmdExec("exec scripts/on_client.scr 0");
		NetworkClient_Connected();
	}
};


/* Used by clients, to connect to a server */
void NetworkClientConnectGame(NetworkAddress address, CompanyID join_as, const char *join_server_password, const char *join_company_password)
{
	if (!_network_available) return;

	if (address.GetPort() == 0) return;

	strecpy(_settings_client.network.last_host, address.GetHostname(), lastof(_settings_client.network.last_host));
	_settings_client.network.last_port = address.GetPort();
	_network_join_as = join_as;
	_network_join_server_password = join_server_password;
	_network_join_company_password = join_company_password;

	NetworkDisconnect();
	NetworkInitialize();

	_network_join_status = NETWORK_JOIN_STATUS_CONNECTING;
	ShowJoinStatusWindow();

	new TCPClientConnecter(address);
}

static void NetworkInitGameInfo()
{
	if (StrEmpty(_settings_client.network.server_name)) {
		seprintf(_settings_client.network.server_name, lastof(_settings_client.network.server_name), "Unnamed Server");
	}

	/* The server is a client too */
	_network_game_info.clients_on = _network_dedicated ? 0 : 1;

	/* There should be always space for the server. */
	assert(NetworkClientInfo::CanAllocateItem());
	NetworkClientInfo *ci = new NetworkClientInfo(CLIENT_ID_SERVER);
	ci->client_playas = _network_dedicated ? COMPANY_SPECTATOR : COMPANY_FIRST;

	strecpy(ci->client_name, _settings_client.network.client_name, lastof(ci->client_name));
}

bool NetworkServerStart()
{
	if (!_network_available) return false;

	/* Call the pre-scripts */
	IConsoleCmdExec("exec scripts/pre_server.scr 0");
	if (_network_dedicated) IConsoleCmdExec("exec scripts/pre_dedicated.scr 0");

	NetworkDisconnect(false, false);
	NetworkInitialize(false);
	DEBUG(net, 1, "starting listeners for clients");
	if (!ServerNetworkGameSocketHandler::Listen(_settings_client.network.server_port)) return false;

	/* Only listen for admins when the password isn't empty. */
	if (!StrEmpty(_settings_client.network.admin_password)) {
		DEBUG(net, 1, "starting listeners for admins");
		if (!ServerNetworkAdminSocketHandler::Listen(_settings_client.network.server_admin_port)) return false;
	}

	/* Try to start UDP-server */
	DEBUG(net, 1, "starting listeners for incoming server queries");
	_network_udp_server = _udp_server_socket->Listen();

	_network_company_states = CallocT<NetworkCompanyState>(MAX_COMPANIES);
	_network_server = true;
	_networking = true;
	_frame_counter = 0;
	_frame_counter_server = 0;
	_frame_counter_max = 0;
	_last_sync_frame = 0;
	_network_own_client_id = CLIENT_ID_SERVER;

	_network_clients_connected = 0;
	_network_company_passworded = 0;

	NetworkInitGameInfo();

	/* execute server initialization script */
	IConsoleCmdExec("exec scripts/on_server.scr 0");
	/* if the server is dedicated ... add some other script */
	if (_network_dedicated) IConsoleCmdExec("exec scripts/on_dedicated.scr 0");

	/* Try to register us to the master server */
	_network_need_advertise = true;
	NetworkUDPAdvertise();

	/* welcome possibly still connected admins - this can only happen on a dedicated server. */
	if (_network_dedicated) ServerNetworkAdminSocketHandler::WelcomeAll();

	return true;
}

/* The server is rebooting...
 * The only difference with NetworkDisconnect, is the packets that is sent */
void NetworkReboot()
{
	if (_network_server) {
		for (NetworkClientSocket *cs : NetworkClientSocket::Iterate()) {
			cs->SendNewGame();
			cs->SendPackets();
		}

		for (ServerNetworkAdminSocketHandler *as : ServerNetworkAdminSocketHandler::IterateActive()) {
			as->SendNewGame();
			as->SendPackets();
		}
	}

	/* For non-dedicated servers we have to kick the admins as we are not
	 * certain that we will end up in a new network game. */
	NetworkClose(!_network_dedicated);
}

/**
 * We want to disconnect from the host/clients.
 * @param blocking whether to wait till everything has been closed.
 * @param close_admins Whether the admin sockets need to be closed as well.
 */
void NetworkDisconnect(bool blocking, bool close_admins)
{
	if (_network_server) {
		for (NetworkClientSocket *cs : NetworkClientSocket::Iterate()) {
			cs->SendShutdown();
			cs->SendPackets();
		}

		if (close_admins) {
			for (ServerNetworkAdminSocketHandler *as : ServerNetworkAdminSocketHandler::IterateActive()) {
				as->SendShutdown();
				as->SendPackets();
			}
		}
	}

	if (_settings_client.network.server_advertise) NetworkUDPRemoveAdvertise(blocking);

	DeleteWindowById(WC_NETWORK_STATUS_WINDOW, WN_NETWORK_STATUS_WINDOW_JOIN);

	NetworkClose(close_admins);

	/* Reinitialize the UDP stack, i.e. close all existing connections. */
	NetworkUDPInitialize();
}

/**
 * Receives something from the network.
 * @return true if everything went fine, false when the connection got closed.
 */
static bool NetworkReceive()
{
	if (_network_server) {
		ServerNetworkAdminSocketHandler::Receive();
		return ServerNetworkGameSocketHandler::Receive();
	} else {
		return ClientNetworkGameSocketHandler::Receive();
	}
}

/* This sends all buffered commands (if possible) */
static void NetworkSend()
{
	if (_network_server) {
		ServerNetworkAdminSocketHandler::Send();
		ServerNetworkGameSocketHandler::Send();
	} else {
		ClientNetworkGameSocketHandler::Send();
	}
}

/**
 * We have to do some (simple) background stuff that runs normally,
 * even when we are not in multiplayer. For example stuff needed
 * for finding servers or downloading content.
 */
void NetworkBackgroundLoop()
{
	_network_content_client.SendReceive();
	TCPConnecter::CheckCallbacks();
	NetworkHTTPSocketHandler::HTTPReceive();

	NetworkBackgroundUDPLoop();
}

/* The main loop called from ttd.c
 *  Here we also have to do StateGameLoop if needed! */
void NetworkGameLoop()
{
	if (!_networking) return;

	if (!NetworkReceive()) return;

	if (_network_server) {
		/* Log the sync state to check for in-syncedness of replays. */
		if (_date_fract == 0) {
			/* We don't want to log multiple times if paused. */
			static Date last_log;
			if (last_log != _date) {
				DEBUG(desync, 1, "sync: %08x; %02x; %08x; %08x", _date, _date_fract, _random.state[0], _random.state[1]);
				last_log = _date;
			}
		}

#ifdef DEBUG_DUMP_COMMANDS
		/* Loading of the debug commands from -ddesync>=1 */
		static FILE *f = FioFOpenFile("commands.log", "rb", SAVE_DIR);
		static Date next_date = 0;
		static uint32 next_date_fract;
		static CommandPacket *cp = nullptr;
		static bool check_sync_state = false;
		static uint32 sync_state[2];
		if (f == nullptr && next_date == 0) {
			DEBUG(net, 0, "Cannot open commands.log");
			next_date = 1;
		}

		while (f != nullptr && !feof(f)) {
			if (_date == next_date && _date_fract == next_date_fract) {
				if (cp != nullptr) {
					NetworkSendCommand(cp->tile, cp->p1, cp->p2, cp->cmd & ~CMD_FLAGS_MASK, nullptr, cp->text, cp->company);
					DEBUG(net, 0, "injecting: %08x; %02x; %02x; %06x; %08x; %08x; %08x; \"%s\" (%s)", _date, _date_fract, (int)_current_company, cp->tile, cp->p1, cp->p2, cp->cmd, cp->text, GetCommandName(cp->cmd));
					free(cp);
					cp = nullptr;
				}
				if (check_sync_state) {
					if (sync_state[0] == _random.state[0] && sync_state[1] == _random.state[1]) {
						DEBUG(net, 0, "sync check: %08x; %02x; match", _date, _date_fract);
					} else {
						DEBUG(net, 0, "sync check: %08x; %02x; mismatch expected {%08x, %08x}, got {%08x, %08x}",
									_date, _date_fract, sync_state[0], sync_state[1], _random.state[0], _random.state[1]);
						NOT_REACHED();
					}
					check_sync_state = false;
				}
			}

			if (cp != nullptr || check_sync_state) break;

			char buff[4096];
			if (fgets(buff, lengthof(buff), f) == nullptr) break;

			char *p = buff;
			/* Ignore the "[date time] " part of the message */
			if (*p == '[') {
				p = strchr(p, ']');
				if (p == nullptr) break;
				p += 2;
			}

			if (strncmp(p, "cmd: ", 5) == 0
#ifdef DEBUG_FAILED_DUMP_COMMANDS
				|| strncmp(p, "cmdf: ", 6) == 0
#endif
				) {
				p += 5;
				if (*p == ' ') p++;
				cp = CallocT<CommandPacket>(1);
				int company;
				static_assert(sizeof(cp->text) == 128);
				int ret = sscanf(p, "%x; %x; %x; %x; %x; %x; %x; \"%127[^\"]\"", &next_date, &next_date_fract, &company, &cp->tile, &cp->p1, &cp->p2, &cp->cmd, cp->text);
				/* There are 8 pieces of data to read, however the last is a
				 * string that might or might not exist. Ignore it if that
				 * string misses because in 99% of the time it's not used. */
				assert(ret == 8 || ret == 7);
				cp->company = (CompanyID)company;
			} else if (strncmp(p, "join: ", 6) == 0) {
				/* Manually insert a pause when joining; this way the client can join at the exact right time. */
				int ret = sscanf(p + 6, "%x; %x", &next_date, &next_date_fract);
				assert(ret == 2);
				DEBUG(net, 0, "injecting pause for join at %08x:%02x; please join when paused", next_date, next_date_fract);
				cp = CallocT<CommandPacket>(1);
				cp->company = COMPANY_SPECTATOR;
				cp->cmd = CMD_PAUSE;
				cp->p1 = PM_PAUSED_NORMAL;
				cp->p2 = 1;
				_ddc_fastforward = false;
			} else if (strncmp(p, "sync: ", 6) == 0) {
				int ret = sscanf(p + 6, "%x; %x; %x; %x", &next_date, &next_date_fract, &sync_state[0], &sync_state[1]);
				assert(ret == 4);
				check_sync_state = true;
			} else if (strncmp(p, "msg: ", 5) == 0 || strncmp(p, "client: ", 8) == 0 ||
						strncmp(p, "load: ", 6) == 0 || strncmp(p, "save: ", 6) == 0) {
				/* A message that is not very important to the log playback, but part of the log. */
#ifndef DEBUG_FAILED_DUMP_COMMANDS
			} else if (strncmp(p, "cmdf: ", 6) == 0) {
				DEBUG(net, 0, "Skipping replay of failed command: %s", p + 6);
#endif
			} else {
				/* Can't parse a line; what's wrong here? */
				DEBUG(net, 0, "trying to parse: %s", p);
				NOT_REACHED();
			}
		}
		if (f != nullptr && feof(f)) {
			DEBUG(net, 0, "End of commands.log");
			fclose(f);
			f = nullptr;
		}
#endif /* DEBUG_DUMP_COMMANDS */
		if (_frame_counter >= _frame_counter_max) {
			/* Only check for active clients just before we're going to send out
			 * the commands so we don't send multiple pause/unpause commands when
			 * the frame_freq is more than 1 tick. Same with distributing commands. */
			CheckPauseOnJoin();
			CheckMinActiveClients();
			NetworkDistributeCommands();
		}

		bool send_frame = false;

		/* We first increase the _frame_counter */
		_frame_counter++;
		/* Update max-frame-counter */
		if (_frame_counter > _frame_counter_max) {
			_frame_counter_max = _frame_counter + _settings_client.network.frame_freq;
			send_frame = true;
		}

		NetworkExecuteLocalCommandQueue();

		/* Then we make the frame */
		StateGameLoop();

		_sync_seed_1 = _random.state[0];
#ifdef NETWORK_SEND_DOUBLE_SEED
		_sync_seed_2 = _random.state[1];
#endif

		NetworkServer_Tick(send_frame);
	} else {
		/* Client */

		/* Make sure we are at the frame were the server is (quick-frames) */
		if (_frame_counter_server > _frame_counter) {
			/* Run a number of frames; when things go bad, get out. */
			while (_frame_counter_server > _frame_counter) {
				if (!ClientNetworkGameSocketHandler::GameLoop()) return;
			}
		} else {
			/* Else, keep on going till _frame_counter_max */
			if (_frame_counter_max > _frame_counter) {
				/* Run one frame; if things went bad, get out. */
				if (!ClientNetworkGameSocketHandler::GameLoop()) return;
			}
		}
	}

	NetworkSend();
}

static void NetworkGenerateServerId()
{
	Md5 checksum;
	uint8 digest[16];
	char hex_output[16 * 2 + 1];
	char coding_string[NETWORK_NAME_LENGTH];
	int di;

	seprintf(coding_string, lastof(coding_string), "%d%s", (uint)Random(), "OpenTTD Server ID");

	/* Generate the MD5 hash */
	checksum.Append((const uint8*)coding_string, strlen(coding_string));
	checksum.Finish(digest);

	for (di = 0; di < 16; ++di) {
		seprintf(hex_output + di * 2, lastof(hex_output), "%02x", digest[di]);
	}

	/* _settings_client.network.network_id is our id */
	seprintf(_settings_client.network.network_id, lastof(_settings_client.network.network_id), "%s", hex_output);
}

void NetworkStartDebugLog(NetworkAddress address)
{
	extern SOCKET _debug_socket;  // Comes from debug.c

	DEBUG(net, 0, "Redirecting DEBUG() to %s:%d", address.GetHostname(), address.GetPort());

	SOCKET s = address.Connect();
	if (s == INVALID_SOCKET) {
		DEBUG(net, 0, "Failed to open socket for redirection DEBUG()");
		return;
	}

	_debug_socket = s;

	DEBUG(net, 0, "DEBUG() is now redirected");
}

/** This tries to launch the network for a given OS */
void NetworkStartUp()
{
	DEBUG(net, 3, "[core] starting network...");

	/* Network is available */
	_network_available = NetworkCoreInitialize();
	_network_dedicated = false;
	_network_need_advertise = true;
	_network_advertise_retries = 0;

	/* Generate an server id when there is none yet */
	if (StrEmpty(_settings_client.network.network_id)) NetworkGenerateServerId();

	memset(&_network_game_info, 0, sizeof(_network_game_info));

	NetworkInitialize();
	DEBUG(net, 3, "[core] network online, multiplayer available");
	NetworkFindBroadcastIPs(&_broadcast_list);
}

/** This shuts the network down */
void NetworkShutDown()
{
	NetworkDisconnect(true);
	NetworkUDPClose();

	DEBUG(net, 3, "[core] shutting down network");

	_network_available = false;

	NetworkCoreShutdown();
}

/**
 * How many hex digits of the git hash to include in network revision string.
 * Determined as 10 hex digits + 2 characters for -g/-u/-m prefix.
 */
static const uint GITHASH_SUFFIX_LEN = 12;

/**
 * Get the network version string used by this build.
 * The returned string is guaranteed to be at most NETWORK_REVISON_LENGTH bytes.
 */
const char * GetNetworkRevisionString()
{
	/* This will be allocated on heap and never free'd, but only once so not a "real" leak. */
	static char *network_revision = nullptr;

	if (!network_revision) {
		/* Start by taking a chance on the full revision string. */
		network_revision = stredup(_openttd_revision);
		/* Ensure it's not longer than the packet buffer length. */
		if (strlen(network_revision) >= NETWORK_REVISION_LENGTH) network_revision[NETWORK_REVISION_LENGTH - 1] = '\0';

		/* Tag names are not mangled further. */
		if (_openttd_revision_tagged) {
			DEBUG(net, 1, "Network revision name is '%s'", network_revision);
			return network_revision;
		}

		/* Prepare a prefix of the git hash.
		* Size is length + 1 for terminator, +2 for -g prefix. */
		assert(_openttd_revision_modified < 3);
		char githash_suffix[GITHASH_SUFFIX_LEN + 1] = "-";
		githash_suffix[1] = "gum"[_openttd_revision_modified];
		for (uint i = 2; i < GITHASH_SUFFIX_LEN; i++) {
			githash_suffix[i] = _openttd_revision_hash[i-2];
		}

		/* Where did the hash start in the original string?
		 * Overwrite from that position, unless that would go past end of packet buffer length. */
		ptrdiff_t hashofs = strrchr(_openttd_revision, '-') - _openttd_revision;
		if (hashofs + strlen(githash_suffix) + 1 > NETWORK_REVISION_LENGTH) hashofs = strlen(network_revision) - strlen(githash_suffix);
		/* Replace the git hash in revision string. */
		strecpy(network_revision + hashofs, githash_suffix, network_revision + NETWORK_REVISION_LENGTH);
		assert(strlen(network_revision) < NETWORK_REVISION_LENGTH); // strlen does not include terminator, constant does, hence strictly less than
		DEBUG(net, 1, "Network revision name is '%s'", network_revision);
	}

	return network_revision;
}

static const char *ExtractNetworkRevisionHash(const char *revstr)
{
	return strrchr(revstr, '-');
}

/**
 * Checks whether the given version string is compatible with our version.
 * First tries to match the full string, if that fails, attempts to compare just git hashes.
 * @param other the version string to compare to
 */
bool IsNetworkCompatibleVersion(const char *other)
{
	if (strncmp(GetNetworkRevisionString(), other, NETWORK_REVISION_LENGTH - 1) == 0) return true;

	/* If this version is tagged, then the revision string must be a complete match,
	 * since there is no git hash suffix in it.
	 * This is needed to avoid situations like "1.9.0-beta1" comparing equal to "2.0.0-beta1".  */
	if (_openttd_revision_tagged) return false;

	const char *hash1 = ExtractNetworkRevisionHash(GetNetworkRevisionString());
	const char *hash2 = ExtractNetworkRevisionHash(other);
	return hash1 && hash2 && (strncmp(hash1, hash2, GITHASH_SUFFIX_LEN) == 0);
}

#ifdef __EMSCRIPTEN__
extern "C" {

void CDECL em_openttd_add_server(const char *host, int port)
{
	NetworkUDPQueryServer(NetworkAddress(host, port), true);
}

}
#endif
