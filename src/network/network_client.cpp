/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file network_client.cpp Client part of the network protocol. */

#include "../stdafx.h"
#include "network_gui.h"
#include "../saveload/saveload.h"
#include "../saveload/saveload_filter.h"
#include "../command_func.h"
#include "../console_func.h"
#include "../strings_func.h"
#include "../window_func.h"
#include "../company_func.h"
#include "../company_base.h"
#include "../company_gui.h"
#include "../core/random_func.hpp"
#include "../date_func.h"
#include "../gfx_func.h"
#include "../error.h"
#include "../rev.h"
#include "network.h"
#include "network_base.h"
#include "network_client.h"
#include "../core/backup_type.hpp"
#include "../thread.h"
#include "social_presence.h"

#include "table/strings.h"

#include "../safeguards.h"

/* This file handles all the client-commands */


/** Read some packets, and when do use that data as initial load filter. */
struct PacketReader : LoadFilter {
	static const size_t CHUNK = 32 * 1024;  ///< 32 KiB chunks of memory.

	std::vector<byte *> blocks;             ///< Buffer with blocks of allocated memory.
	byte *buf;                              ///< Buffer we're going to write to/read from.
	byte *bufe;                             ///< End of the buffer we write to/read from.
	byte **block;                           ///< The block we're reading from/writing to.
	size_t written_bytes;                   ///< The total number of bytes we've written.
	size_t read_bytes;                      ///< The total number of read bytes.

	/** Initialise everything. */
	PacketReader() : LoadFilter(nullptr), buf(nullptr), bufe(nullptr), block(nullptr), written_bytes(0), read_bytes(0)
	{
	}

	~PacketReader() override
	{
		for (auto p : this->blocks) {
			free(p);
		}
	}

	/**
	 * Add a packet to this buffer.
	 * @param p The packet to add.
	 */
	void AddPacket(const Packet *p)
	{
		assert(this->read_bytes == 0);

		size_t in_packet = p->size - p->pos;
		size_t to_write  = std::min<size_t>(this->bufe - this->buf, in_packet);
		const byte *pbuf = p->buffer + p->pos;

		this->written_bytes += in_packet;
		if (to_write != 0) {
			memcpy(this->buf, pbuf, to_write);
			this->buf += to_write;
		}

		/* Did everything fit in the current chunk, then we're done. */
		if (to_write == in_packet) return;

		/* Allocate a new chunk and add the remaining data. */
		pbuf += to_write;
		to_write   = in_packet - to_write;
		this->blocks.push_back(this->buf = CallocT<byte>(CHUNK));
		this->bufe = this->buf + CHUNK;

		memcpy(this->buf, pbuf, to_write);
		this->buf += to_write;
	}

	size_t Read(byte *rbuf, size_t size) override
	{
		/* Limit the amount to read to whatever we still have. */
		size_t ret_size = size = std::min(this->written_bytes - this->read_bytes, size);
		this->read_bytes += ret_size;
		const byte *rbufe = rbuf + ret_size;

		while (rbuf != rbufe) {
			if (this->buf == this->bufe) {
				this->buf = *this->block++;
				this->bufe = this->buf + CHUNK;
			}

			size_t to_write = std::min(this->bufe - this->buf, rbufe - rbuf);
			memcpy(rbuf, this->buf, to_write);
			rbuf += to_write;
			this->buf += to_write;
		}

		return ret_size;
	}

	void Reset() override
	{
		this->read_bytes = 0;

		this->block = this->blocks.data();
		this->buf   = *this->block++;
		this->bufe  = this->buf + CHUNK;
	}
};


/**
 * Create an emergency savegame when the network connection is lost.
 */
void ClientNetworkEmergencySave()
{
	if (!_settings_client.gui.autosave_on_network_disconnect) return;
	if (!_networking) return;

	const char *filename = "netsave.sav";
	DEBUG(net, 0, "Client: Performing emergency save (%s)", filename);
	SaveOrLoad(filename, SLO_SAVE, DFT_GAME_FILE, AUTOSAVE_DIR, false);
}


/**
 * Create a new socket for the client side of the game connection.
 * @param s The socket to connect with.
 */
ClientNetworkGameSocketHandler::ClientNetworkGameSocketHandler(SOCKET s) : NetworkGameSocketHandler(s), savegame(nullptr), status(STATUS_INACTIVE)
{
	assert(ClientNetworkGameSocketHandler::my_client == nullptr);
	ClientNetworkGameSocketHandler::my_client = this;
}

/** Clear whatever we assigned. */
ClientNetworkGameSocketHandler::~ClientNetworkGameSocketHandler()
{
	assert(ClientNetworkGameSocketHandler::my_client == this);
	ClientNetworkGameSocketHandler::my_client = nullptr;

	delete this->savegame;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::CloseConnection(NetworkRecvStatus status)
{
	assert(status != NETWORK_RECV_STATUS_OKAY);
	/*
	 * Sending a message just before leaving the game calls cs->SendPackets.
	 * This might invoke this function, which means that when we close the
	 * connection after cs->SendPackets we will close an already closed
	 * connection. This handles that case gracefully without having to make
	 * that code any more complex or more aware of the validity of the socket.
	 */
	if (this->sock == INVALID_SOCKET) return status;

	DEBUG(net, 1, "Closed client connection %d", this->client_id);

	this->SendPackets(true);

	/* Wait a number of ticks so our leave message can reach the server.
	 * This is especially needed for Windows servers as they seem to get
	 * the "socket is closed" message before receiving our leave message,
	 * which would trigger the server to close the connection as well. */
	CSleep(3 * MILLISECONDS_PER_TICK);

	delete this->GetInfo();
	delete this;

	return status;
}

/**
 * Handle an error coming from the client side.
 * @param res The "error" that happened.
 */
void ClientNetworkGameSocketHandler::ClientError(NetworkRecvStatus res)
{
	/* First, send a CLIENT_ERROR to the server, so he knows we are
	 *  disconnection (and why!) */
	NetworkErrorCode errorno;

	/* We just want to close the connection.. */
	if (res == NETWORK_RECV_STATUS_CLOSE_QUERY) {
		this->NetworkSocketHandler::CloseConnection();
		this->CloseConnection(res);
		_networking = false;

		DeleteWindowById(WC_NETWORK_STATUS_WINDOW, WN_NETWORK_STATUS_WINDOW_JOIN);
		return;
	}

	switch (res) {
		case NETWORK_RECV_STATUS_DESYNC:          errorno = NETWORK_ERROR_DESYNC; break;
		case NETWORK_RECV_STATUS_SAVEGAME:        errorno = NETWORK_ERROR_SAVEGAME_FAILED; break;
		case NETWORK_RECV_STATUS_NEWGRF_MISMATCH: errorno = NETWORK_ERROR_NEWGRF_MISMATCH; break;
		default:                                  errorno = NETWORK_ERROR_GENERAL; break;
	}

	if (res == NETWORK_RECV_STATUS_SERVER_ERROR || res == NETWORK_RECV_STATUS_SERVER_FULL ||
			res == NETWORK_RECV_STATUS_SERVER_BANNED) {
		/* This means the server closed the connection. Emergency save is
		 * already created if this was appropriate during handling of the
		 * disconnect. */
		this->CloseConnection(res);
	} else {
		/* This means we as client made a boo-boo. */
		SendError(errorno);

		/* Close connection before we make an emergency save, as the save can
		 * take a bit of time; better that the server doesn't stall while we
		 * are doing the save, and already disconnects us. */
		this->CloseConnection(res);
		ClientNetworkEmergencySave();
	}

	_switch_mode = SM_MENU;
	_networking = false;
}


/**
 * Check whether we received/can send some data from/to the server and
 * when that's the case handle it appropriately.
 * @return true when everything went okay.
 */
/* static */ bool ClientNetworkGameSocketHandler::Receive()
{
	if (my_client->CanSendReceive()) {
		NetworkRecvStatus res = my_client->ReceivePackets();
		if (res != NETWORK_RECV_STATUS_OKAY) {
			/* The client made an error of which we can not recover.
			 * Close the connection and drop back to the main menu. */
			my_client->ClientError(res);
			return false;
		}
	}
	return _networking;
}

/** Send the packets of this socket handler. */
/* static */ void ClientNetworkGameSocketHandler::Send()
{
	my_client->SendPackets();
	my_client->CheckConnection();
}

/**
 * Actual game loop for the client.
 * @return Whether everything went okay, or not.
 */
/* static */ bool ClientNetworkGameSocketHandler::GameLoop()
{
	_frame_counter++;

	NetworkExecuteLocalCommandQueue();

	extern void StateGameLoop();
	StateGameLoop();

	/* Check if we are in sync! */
	if (_sync_frame != 0) {
		if (_sync_frame == _frame_counter) {
#ifdef NETWORK_SEND_DOUBLE_SEED
			if (_sync_seed_1 != _random.state[0] || _sync_seed_2 != _random.state[1]) {
#else
			if (_sync_seed_1 != _random.state[0]) {
#endif
				NetworkError(STR_NETWORK_ERROR_DESYNC);
				DEBUG(desync, 1, "sync_err: %08x; %02x", _date, _date_fract);
				DEBUG(net, 0, "Sync error detected!");
				my_client->ClientError(NETWORK_RECV_STATUS_DESYNC);
				return false;
			}

			/* If this is the first time we have a sync-frame, we
			 *   need to let the server know that we are ready and at the same
			 *   frame as he is.. so we can start playing! */
			if (_network_first_time) {
				_network_first_time = false;
				SendAck();
			}

			_sync_frame = 0;
		} else if (_sync_frame < _frame_counter) {
			DEBUG(net, 1, "Missed frame for sync-test (%d / %d)", _sync_frame, _frame_counter);
			_sync_frame = 0;
		}
	}

	return true;
}


/** Our client's connection. */
ClientNetworkGameSocketHandler * ClientNetworkGameSocketHandler::my_client = nullptr;

/** Last frame we performed an ack. */
static uint32 last_ack_frame;

/** One bit of 'entropy' used to generate a salt for the company passwords. */
static uint32 _password_game_seed;
/** The other bit of 'entropy' used to generate a salt for the company passwords. */
static char _password_server_id[NETWORK_SERVER_ID_LENGTH];

/** Maximum number of companies of the currently joined server. */
static uint8 _network_server_max_companies;
/** Maximum number of spectators of the currently joined server. */
static uint8 _network_server_max_spectators;

/** Who would we like to join as. */
CompanyID _network_join_as;

/** Login password from -p argument */
const char *_network_join_server_password = nullptr;
/** Company password from -P argument */
const char *_network_join_company_password = nullptr;

/** Make sure the server ID length is the same as a md5 hash. */
static_assert(NETWORK_SERVER_ID_LENGTH == 16 * 2 + 1);

/***********
 * Sending functions
 *   DEF_CLIENT_SEND_COMMAND has no parameters
 ************/

/** Query the server for company information. */
NetworkRecvStatus ClientNetworkGameSocketHandler::SendCompanyInformationQuery()
{
	my_client->status = STATUS_COMPANY_INFO;
	_network_join_status = NETWORK_JOIN_STATUS_GETTING_COMPANY_INFO;
	SetWindowDirty(WC_NETWORK_STATUS_WINDOW, WN_NETWORK_STATUS_WINDOW_JOIN);

	Packet *p = new Packet(PACKET_CLIENT_COMPANY_INFO);
	my_client->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/** Tell the server we would like to join. */
NetworkRecvStatus ClientNetworkGameSocketHandler::SendJoin()
{
	my_client->status = STATUS_JOIN;
	_network_join_status = NETWORK_JOIN_STATUS_AUTHORIZING;
	SetWindowDirty(WC_NETWORK_STATUS_WINDOW, WN_NETWORK_STATUS_WINDOW_JOIN);

	Packet *p = new Packet(PACKET_CLIENT_JOIN);
	p->Send_string(GetNetworkRevisionString());
	p->Send_uint32(_openttd_newgrf_version);
	p->Send_string(_settings_client.network.client_name); // Client name
	p->Send_uint8 (_network_join_as);     // PlayAs
	p->Send_uint8 (NETLANG_ANY);          // Language
	my_client->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/** Tell the server we got all the NewGRFs. */
NetworkRecvStatus ClientNetworkGameSocketHandler::SendNewGRFsOk()
{
	Packet *p = new Packet(PACKET_CLIENT_NEWGRFS_CHECKED);
	my_client->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/**
 * Set the game password as requested.
 * @param password The game password.
 */
NetworkRecvStatus ClientNetworkGameSocketHandler::SendGamePassword(const char *password)
{
	Packet *p = new Packet(PACKET_CLIENT_GAME_PASSWORD);
	p->Send_string(password);
	my_client->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/**
 * Set the company password as requested.
 * @param password The company password.
 */
NetworkRecvStatus ClientNetworkGameSocketHandler::SendCompanyPassword(const char *password)
{
	Packet *p = new Packet(PACKET_CLIENT_COMPANY_PASSWORD);
	p->Send_string(GenerateCompanyPasswordHash(password, _password_server_id, _password_game_seed));
	my_client->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/** Request the map from the server. */
NetworkRecvStatus ClientNetworkGameSocketHandler::SendGetMap()
{
	my_client->status = STATUS_MAP_WAIT;

	Packet *p = new Packet(PACKET_CLIENT_GETMAP);
	my_client->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/** Tell the server we received the complete map. */
NetworkRecvStatus ClientNetworkGameSocketHandler::SendMapOk()
{
	my_client->status = STATUS_ACTIVE;

	Packet *p = new Packet(PACKET_CLIENT_MAP_OK);
	my_client->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/** Send an acknowledgement from the server's ticks. */
NetworkRecvStatus ClientNetworkGameSocketHandler::SendAck()
{
	Packet *p = new Packet(PACKET_CLIENT_ACK);

	p->Send_uint32(_frame_counter);
	p->Send_uint8 (my_client->token);
	my_client->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/**
 * Send a command to the server.
 * @param cp The command to send.
 */
NetworkRecvStatus ClientNetworkGameSocketHandler::SendCommand(const CommandPacket *cp)
{
	Packet *p = new Packet(PACKET_CLIENT_COMMAND);
	my_client->NetworkGameSocketHandler::SendCommand(p, cp);

	my_client->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/** Send a chat-packet over the network */
NetworkRecvStatus ClientNetworkGameSocketHandler::SendChat(NetworkAction action, DestType type, int dest, const char *msg, int64 data)
{
	Packet *p = new Packet(PACKET_CLIENT_CHAT);

	p->Send_uint8 (action);
	p->Send_uint8 (type);
	p->Send_uint32(dest);
	p->Send_string(msg);
	p->Send_uint64(data);

	my_client->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/** Send an error-packet over the network */
NetworkRecvStatus ClientNetworkGameSocketHandler::SendError(NetworkErrorCode errorno)
{
	Packet *p = new Packet(PACKET_CLIENT_ERROR);

	p->Send_uint8(errorno);
	my_client->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/**
 * Tell the server that we like to change the password of the company.
 * @param password The new password.
 */
NetworkRecvStatus ClientNetworkGameSocketHandler::SendSetPassword(const char *password)
{
	Packet *p = new Packet(PACKET_CLIENT_SET_PASSWORD);

	p->Send_string(GenerateCompanyPasswordHash(password, _password_server_id, _password_game_seed));
	my_client->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/**
 * Tell the server that we like to change the name of the client.
 * @param name The new name.
 */
NetworkRecvStatus ClientNetworkGameSocketHandler::SendSetName(const char *name)
{
	Packet *p = new Packet(PACKET_CLIENT_SET_NAME);

	p->Send_string(name);
	my_client->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/**
 * Tell the server we would like to quit.
 */
NetworkRecvStatus ClientNetworkGameSocketHandler::SendQuit()
{
	Packet *p = new Packet(PACKET_CLIENT_QUIT);

	my_client->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/**
 * Send a console command.
 * @param pass The password for the remote command.
 * @param command The actual command.
 */
NetworkRecvStatus ClientNetworkGameSocketHandler::SendRCon(const char *pass, const char *command)
{
	Packet *p = new Packet(PACKET_CLIENT_RCON);
	p->Send_string(pass);
	p->Send_string(command);
	my_client->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/**
 * Ask the server to move us.
 * @param company The company to move to.
 * @param password The password of the company to move to.
 */
NetworkRecvStatus ClientNetworkGameSocketHandler::SendMove(CompanyID company, const char *password)
{
	Packet *p = new Packet(PACKET_CLIENT_MOVE);
	p->Send_uint8(company);
	p->Send_string(GenerateCompanyPasswordHash(password, _password_server_id, _password_game_seed));
	my_client->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/**
 * Check whether the client is actually connected (and in the game).
 * @return True when the client is connected.
 */
bool ClientNetworkGameSocketHandler::IsConnected()
{
	return my_client != nullptr && my_client->status == STATUS_ACTIVE;
}


/***********
 * Receiving functions
 *   DEF_CLIENT_RECEIVE_COMMAND has parameter: Packet *p
 ************/

extern bool SafeLoad(const std::string &filename, SaveLoadOperation fop, DetailedFileType dft, GameMode newgm, Subdirectory subdir, struct LoadFilter *lf = nullptr);

NetworkRecvStatus ClientNetworkGameSocketHandler::Receive_SERVER_FULL(Packet *p)
{
	/* We try to join a server which is full */
	ShowErrorMessage(STR_NETWORK_ERROR_SERVER_FULL, INVALID_STRING_ID, WL_CRITICAL);
	DeleteWindowById(WC_NETWORK_STATUS_WINDOW, WN_NETWORK_STATUS_WINDOW_JOIN);

	return NETWORK_RECV_STATUS_SERVER_FULL;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::Receive_SERVER_BANNED(Packet *p)
{
	/* We try to join a server where we are banned */
	ShowErrorMessage(STR_NETWORK_ERROR_SERVER_BANNED, INVALID_STRING_ID, WL_CRITICAL);
	DeleteWindowById(WC_NETWORK_STATUS_WINDOW, WN_NETWORK_STATUS_WINDOW_JOIN);

	return NETWORK_RECV_STATUS_SERVER_BANNED;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::Receive_SERVER_COMPANY_INFO(Packet *p)
{
	if (this->status != STATUS_COMPANY_INFO) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	byte company_info_version = p->Recv_uint8();

	if (!this->HasClientQuit() && company_info_version == NETWORK_COMPANY_INFO_VERSION) {
		/* We have received all data... (there are no more packets coming) */
		if (!p->Recv_bool()) return NETWORK_RECV_STATUS_CLOSE_QUERY;

		CompanyID current = (Owner)p->Recv_uint8();
		if (current >= MAX_COMPANIES) return NETWORK_RECV_STATUS_CLOSE_QUERY;

		NetworkCompanyInfo *company_info = GetLobbyCompanyInfo(current);
		if (company_info == nullptr) return NETWORK_RECV_STATUS_CLOSE_QUERY;

		p->Recv_string(company_info->company_name, sizeof(company_info->company_name));
		company_info->inaugurated_year = p->Recv_uint32();
		company_info->company_value    = p->Recv_uint64();
		company_info->money            = p->Recv_uint64();
		company_info->income           = p->Recv_uint64();
		company_info->performance      = p->Recv_uint16();
		company_info->use_password     = p->Recv_bool();
		for (uint i = 0; i < NETWORK_VEH_END; i++) {
			company_info->num_vehicle[i] = p->Recv_uint16();
		}
		for (uint i = 0; i < NETWORK_VEH_END; i++) {
			company_info->num_station[i] = p->Recv_uint16();
		}
		company_info->ai               = p->Recv_bool();

		p->Recv_string(company_info->clients, sizeof(company_info->clients));

		SetWindowDirty(WC_NETWORK_WINDOW, WN_NETWORK_WINDOW_LOBBY);

		return NETWORK_RECV_STATUS_OKAY;
	}

	return NETWORK_RECV_STATUS_CLOSE_QUERY;
}

/* This packet contains info about the client (playas and name)
 *  as client we save this in NetworkClientInfo, linked via 'client_id'
 *  which is always an unique number on a server. */
NetworkRecvStatus ClientNetworkGameSocketHandler::Receive_SERVER_CLIENT_INFO(Packet *p)
{
	NetworkClientInfo *ci;
	ClientID client_id = (ClientID)p->Recv_uint32();
	CompanyID playas = (CompanyID)p->Recv_uint8();
	char name[NETWORK_NAME_LENGTH];

	p->Recv_string(name, sizeof(name));

	if (this->status < STATUS_AUTHORIZED) return NETWORK_RECV_STATUS_MALFORMED_PACKET;
	if (this->HasClientQuit()) return NETWORK_RECV_STATUS_CONN_LOST;

	ci = NetworkClientInfo::GetByClientID(client_id);
	if (ci != nullptr) {
		if (playas == ci->client_playas && strcmp(name, ci->client_name) != 0) {
			/* Client name changed, display the change */
			NetworkTextMessage(NETWORK_ACTION_NAME_CHANGE, CC_DEFAULT, false, ci->client_name, name);
		} else if (playas != ci->client_playas) {
			/* The client changed from client-player..
			 * Do not display that for now */
		}

		/* Make sure we're in the company the server tells us to be in,
		 * for the rare case that we get moved while joining. */
		if (client_id == _network_own_client_id) SetLocalCompany(!Company::IsValidID(playas) ? COMPANY_SPECTATOR : playas);

		ci->client_playas = playas;
		strecpy(ci->client_name, name, lastof(ci->client_name));

		SetWindowDirty(WC_CLIENT_LIST, 0);

		return NETWORK_RECV_STATUS_OKAY;
	}

	/* There are at most as many ClientInfo as ClientSocket objects in a
	 * server. Having more info than a server can have means something
	 * has gone wrong somewhere, i.e. the server has more info than it
	 * has actual clients. That means the server is feeding us an invalid
	 * state. So, bail out! This server is broken. */
	if (!NetworkClientInfo::CanAllocateItem()) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	/* We don't have this client_id yet, find an empty client_id, and put the data there */
	ci = new NetworkClientInfo(client_id);
	ci->client_playas = playas;
	if (client_id == _network_own_client_id) this->SetInfo(ci);

	strecpy(ci->client_name, name, lastof(ci->client_name));

	SetWindowDirty(WC_CLIENT_LIST, 0);

	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::Receive_SERVER_ERROR(Packet *p)
{
	static const StringID network_error_strings[] = {
		STR_NETWORK_ERROR_LOSTCONNECTION,    // NETWORK_ERROR_GENERAL
		STR_NETWORK_ERROR_LOSTCONNECTION,    // NETWORK_ERROR_DESYNC
		STR_NETWORK_ERROR_LOSTCONNECTION,    // NETWORK_ERROR_SAVEGAME_FAILED
		STR_NETWORK_ERROR_LOSTCONNECTION,    // NETWORK_ERROR_CONNECTION_LOST
		STR_NETWORK_ERROR_LOSTCONNECTION,    // NETWORK_ERROR_ILLEGAL_PACKET
		STR_NETWORK_ERROR_LOSTCONNECTION,    // NETWORK_ERROR_NEWGRF_MISMATCH
		STR_NETWORK_ERROR_SERVER_ERROR,      // NETWORK_ERROR_NOT_AUTHORIZED
		STR_NETWORK_ERROR_SERVER_ERROR,      // NETWORK_ERROR_NOT_EXPECTED
		STR_NETWORK_ERROR_WRONG_REVISION,    // NETWORK_ERROR_WRONG_REVISION
		STR_NETWORK_ERROR_LOSTCONNECTION,    // NETWORK_ERROR_NAME_IN_USE
		STR_NETWORK_ERROR_WRONG_PASSWORD,    // NETWORK_ERROR_WRONG_PASSWORD
		STR_NETWORK_ERROR_SERVER_ERROR,      // NETWORK_ERROR_COMPANY_MISMATCH
		STR_NETWORK_ERROR_KICKED,            // NETWORK_ERROR_KICKED
		STR_NETWORK_ERROR_CHEATER,           // NETWORK_ERROR_CHEATER
		STR_NETWORK_ERROR_SERVER_FULL,       // NETWORK_ERROR_FULL
		STR_NETWORK_ERROR_TOO_MANY_COMMANDS, // NETWORK_ERROR_TOO_MANY_COMMANDS
		STR_NETWORK_ERROR_TIMEOUT_PASSWORD,  // NETWORK_ERROR_TIMEOUT_PASSWORD
		STR_NETWORK_ERROR_TIMEOUT_COMPUTER,  // NETWORK_ERROR_TIMEOUT_COMPUTER
		STR_NETWORK_ERROR_TIMEOUT_MAP,       // NETWORK_ERROR_TIMEOUT_MAP
		STR_NETWORK_ERROR_TIMEOUT_JOIN,      // NETWORK_ERROR_TIMEOUT_JOIN
	};
	static_assert(lengthof(network_error_strings) == NETWORK_ERROR_END);

	NetworkErrorCode error = (NetworkErrorCode)p->Recv_uint8();

	StringID err = STR_NETWORK_ERROR_LOSTCONNECTION;
	if (error < (ptrdiff_t)lengthof(network_error_strings)) err = network_error_strings[error];
	/* In case of kicking a client, we assume there is a kick message in the packet if we can read one byte */
	if (error == NETWORK_ERROR_KICKED && p->CanReadFromPacket(1)) {
		char kick_msg[255];
		p->Recv_string(kick_msg, sizeof(kick_msg));
		SetDParamStr(0, kick_msg);
		ShowErrorMessage(err, STR_NETWORK_ERROR_KICK_MESSAGE, WL_CRITICAL);
	} else {
		ShowErrorMessage(err, INVALID_STRING_ID, WL_CRITICAL);
	}

	/* Perform an emergency save if we had already entered the game */
	if (this->status == STATUS_ACTIVE) ClientNetworkEmergencySave();

	DeleteWindowById(WC_NETWORK_STATUS_WINDOW, WN_NETWORK_STATUS_WINDOW_JOIN);

	return NETWORK_RECV_STATUS_SERVER_ERROR;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::Receive_SERVER_CHECK_NEWGRFS(Packet *p)
{
	if (this->status != STATUS_JOIN) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	uint grf_count = p->Recv_uint8();
	NetworkRecvStatus ret = NETWORK_RECV_STATUS_OKAY;

	/* Check all GRFs */
	for (; grf_count > 0; grf_count--) {
		GRFIdentifier c;
		this->ReceiveGRFIdentifier(p, &c);

		/* Check whether we know this GRF */
		const GRFConfig *f = FindGRFConfig(c.grfid, FGCM_EXACT, c.md5sum);
		if (f == nullptr) {
			/* We do not know this GRF, bail out of initialization */
			char buf[sizeof(c.md5sum) * 2 + 1];
			md5sumToString(buf, lastof(buf), c.md5sum);
			DEBUG(grf, 0, "NewGRF %08X not found; checksum %s", BSWAP32(c.grfid), buf);
			ret = NETWORK_RECV_STATUS_NEWGRF_MISMATCH;
		}
	}

	if (ret == NETWORK_RECV_STATUS_OKAY) {
		/* Start receiving the map */
		return SendNewGRFsOk();
	}

	/* NewGRF mismatch, bail out */
	ShowErrorMessage(STR_NETWORK_ERROR_NEWGRF_MISMATCH, INVALID_STRING_ID, WL_CRITICAL);
	return ret;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::Receive_SERVER_NEED_GAME_PASSWORD(Packet *p)
{
	if (this->status < STATUS_JOIN || this->status >= STATUS_AUTH_GAME) return NETWORK_RECV_STATUS_MALFORMED_PACKET;
	this->status = STATUS_AUTH_GAME;

	const char *password = _network_join_server_password;
	if (!StrEmpty(password)) {
		return SendGamePassword(password);
	}

	ShowNetworkNeedPassword(NETWORK_GAME_PASSWORD);

	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::Receive_SERVER_NEED_COMPANY_PASSWORD(Packet *p)
{
	if (this->status < STATUS_JOIN || this->status >= STATUS_AUTH_COMPANY) return NETWORK_RECV_STATUS_MALFORMED_PACKET;
	this->status = STATUS_AUTH_COMPANY;

	_password_game_seed = p->Recv_uint32();
	p->Recv_string(_password_server_id, sizeof(_password_server_id));
	if (this->HasClientQuit()) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	const char *password = _network_join_company_password;
	if (!StrEmpty(password)) {
		return SendCompanyPassword(password);
	}

	ShowNetworkNeedPassword(NETWORK_COMPANY_PASSWORD);

	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::Receive_SERVER_WELCOME(Packet *p)
{
	if (this->status < STATUS_JOIN || this->status >= STATUS_AUTHORIZED) return NETWORK_RECV_STATUS_MALFORMED_PACKET;
	this->status = STATUS_AUTHORIZED;

	_network_own_client_id = (ClientID)p->Recv_uint32();

	/* Initialize the password hash salting variables, even if they were previously. */
	_password_game_seed = p->Recv_uint32();
	p->Recv_string(_password_server_id, sizeof(_password_server_id));

	/* Start receiving the map */
	return SendGetMap();
}

NetworkRecvStatus ClientNetworkGameSocketHandler::Receive_SERVER_WAIT(Packet *p)
{
	/* We set the internal wait state when requesting the map. */
	if (this->status != STATUS_MAP_WAIT) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	/* But... only now we set the join status to waiting, instead of requesting. */
	_network_join_status = NETWORK_JOIN_STATUS_WAITING;
	_network_join_waiting = p->Recv_uint8();
	SetWindowDirty(WC_NETWORK_STATUS_WINDOW, WN_NETWORK_STATUS_WINDOW_JOIN);

	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::Receive_SERVER_MAP_BEGIN(Packet *p)
{
	if (this->status < STATUS_AUTHORIZED || this->status >= STATUS_MAP) return NETWORK_RECV_STATUS_MALFORMED_PACKET;
	this->status = STATUS_MAP;

	if (this->savegame != nullptr) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	this->savegame = new PacketReader();

	_frame_counter = _frame_counter_server = _frame_counter_max = p->Recv_uint32();

	_network_join_bytes = 0;
	_network_join_bytes_total = 0;

	_network_join_status = NETWORK_JOIN_STATUS_DOWNLOADING;
	SetWindowDirty(WC_NETWORK_STATUS_WINDOW, WN_NETWORK_STATUS_WINDOW_JOIN);

	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::Receive_SERVER_MAP_SIZE(Packet *p)
{
	if (this->status != STATUS_MAP) return NETWORK_RECV_STATUS_MALFORMED_PACKET;
	if (this->savegame == nullptr) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	_network_join_bytes_total = p->Recv_uint32();
	SetWindowDirty(WC_NETWORK_STATUS_WINDOW, WN_NETWORK_STATUS_WINDOW_JOIN);

	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::Receive_SERVER_MAP_DATA(Packet *p)
{
	if (this->status != STATUS_MAP) return NETWORK_RECV_STATUS_MALFORMED_PACKET;
	if (this->savegame == nullptr) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	/* We are still receiving data, put it to the file */
	this->savegame->AddPacket(p);

	_network_join_bytes = (uint32)this->savegame->written_bytes;
	SetWindowDirty(WC_NETWORK_STATUS_WINDOW, WN_NETWORK_STATUS_WINDOW_JOIN);

	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::Receive_SERVER_MAP_DONE(Packet *p)
{
	if (this->status != STATUS_MAP) return NETWORK_RECV_STATUS_MALFORMED_PACKET;
	if (this->savegame == nullptr) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	_network_join_status = NETWORK_JOIN_STATUS_PROCESSING;
	SetWindowDirty(WC_NETWORK_STATUS_WINDOW, WN_NETWORK_STATUS_WINDOW_JOIN);

	/*
	 * Make sure everything is set for reading.
	 *
	 * We need the local copy and reset this->savegame because when
	 * loading fails the network gets reset upon loading the intro
	 * game, which would cause us to free this->savegame twice.
	 */
	LoadFilter *lf = this->savegame;
	this->savegame = nullptr;
	lf->Reset();

	/* The map is done downloading, load it */
	ClearErrorMessages();
	bool load_success = SafeLoad({}, SLO_LOAD, DFT_GAME_FILE, GM_NORMAL, NO_DIRECTORY, lf);

	/* Long savegame loads shouldn't affect the lag calculation! */
	this->last_packet = std::chrono::steady_clock::now();

	if (!load_success) {
		DeleteWindowById(WC_NETWORK_STATUS_WINDOW, WN_NETWORK_STATUS_WINDOW_JOIN);
		ShowErrorMessage(STR_NETWORK_ERROR_SAVEGAMEERROR, INVALID_STRING_ID, WL_CRITICAL);
		return NETWORK_RECV_STATUS_SAVEGAME;
	}
	/* If the savegame has successfully loaded, ALL windows have been removed,
	 * only toolbar/statusbar and gamefield are visible */

	/* Say we received the map and loaded it correctly! */
	SendMapOk();

	SocialCompleteEnterMultiplayer();

	/* New company/spectator (invalid company) or company we want to join is not active
	 * Switch local company to spectator and await the server's judgement */
	if (_network_join_as == COMPANY_NEW_COMPANY || !Company::IsValidID(_network_join_as)) {
		SetLocalCompany(COMPANY_SPECTATOR);

		if (_network_join_as != COMPANY_SPECTATOR) {
			/* We have arrived and ready to start playing; send a command to make a new company;
			 * the server will give us a client-id and let us in */
			_network_join_status = NETWORK_JOIN_STATUS_REGISTERING;
			ShowJoinStatusWindow();
			NetworkSendCommand(0, CCA_NEW, 0, CMD_COMPANY_CTRL, nullptr, nullptr, _local_company);
		}
	} else {
		/* take control over an existing company */
		SetLocalCompany(_network_join_as);
	}

	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::Receive_SERVER_FRAME(Packet *p)
{
	if (this->status != STATUS_ACTIVE) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	_frame_counter_server = p->Recv_uint32();
	_frame_counter_max = p->Recv_uint32();
#ifdef ENABLE_NETWORK_SYNC_EVERY_FRAME
	/* Test if the server supports this option
	 *  and if we are at the frame the server is */
	if (p->pos + 1 < p->size) {
		_sync_frame = _frame_counter_server;
		_sync_seed_1 = p->Recv_uint32();
#ifdef NETWORK_SEND_DOUBLE_SEED
		_sync_seed_2 = p->Recv_uint32();
#endif
	}
#endif
	/* Receive the token. */
	if (p->pos != p->size) this->token = p->Recv_uint8();

	DEBUG(net, 5, "Received FRAME %d", _frame_counter_server);

	/* Let the server know that we received this frame correctly
	 *  We do this only once per day, to save some bandwidth ;) */
	if (!_network_first_time && last_ack_frame < _frame_counter) {
		last_ack_frame = _frame_counter + DAY_TICKS;
		DEBUG(net, 4, "Sent ACK at %d", _frame_counter);
		SendAck();
	}

	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::Receive_SERVER_SYNC(Packet *p)
{
	if (this->status != STATUS_ACTIVE) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	_sync_frame = p->Recv_uint32();
	_sync_seed_1 = p->Recv_uint32();
#ifdef NETWORK_SEND_DOUBLE_SEED
	_sync_seed_2 = p->Recv_uint32();
#endif

	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::Receive_SERVER_COMMAND(Packet *p)
{
	if (this->status != STATUS_ACTIVE) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	CommandPacket cp;
	const char *err = this->ReceiveCommand(p, &cp);
	cp.frame    = p->Recv_uint32();
	cp.my_cmd   = p->Recv_bool();

	if (err != nullptr) {
		IConsolePrintF(CC_ERROR, "WARNING: %s from server, dropping...", err);
		return NETWORK_RECV_STATUS_MALFORMED_PACKET;
	}

	this->incoming_queue.Append(&cp);

	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::Receive_SERVER_CHAT(Packet *p)
{
	if (this->status != STATUS_ACTIVE) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	char name[NETWORK_NAME_LENGTH], msg[NETWORK_CHAT_LENGTH];
	const NetworkClientInfo *ci = nullptr, *ci_to;

	NetworkAction action = (NetworkAction)p->Recv_uint8();
	ClientID client_id = (ClientID)p->Recv_uint32();
	bool self_send = p->Recv_bool();
	p->Recv_string(msg, NETWORK_CHAT_LENGTH);
	int64 data = p->Recv_uint64();

	ci_to = NetworkClientInfo::GetByClientID(client_id);
	if (ci_to == nullptr) return NETWORK_RECV_STATUS_OKAY;

	/* Did we initiate the action locally? */
	if (self_send) {
		switch (action) {
			case NETWORK_ACTION_CHAT_CLIENT:
				/* For speaking to client we need the client-name */
				seprintf(name, lastof(name), "%s", ci_to->client_name);
				ci = NetworkClientInfo::GetByClientID(_network_own_client_id);
				break;

			/* For speaking to company, we need the company-name */
			case NETWORK_ACTION_CHAT_COMPANY: {
				StringID str = Company::IsValidID(ci_to->client_playas) ? STR_COMPANY_NAME : STR_NETWORK_SPECTATORS;
				SetDParam(0, ci_to->client_playas);

				GetString(name, str, lastof(name));
				ci = NetworkClientInfo::GetByClientID(_network_own_client_id);
				break;
			}

			default: return NETWORK_RECV_STATUS_MALFORMED_PACKET;
		}
	} else {
		/* Display message from somebody else */
		seprintf(name, lastof(name), "%s", ci_to->client_name);
		ci = ci_to;
	}

	if (ci != nullptr) {
		NetworkTextMessage(action, GetDrawStringCompanyColour(ci->client_playas), self_send, name, msg, data);
	}
	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::Receive_SERVER_ERROR_QUIT(Packet *p)
{
	if (this->status < STATUS_AUTHORIZED) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	ClientID client_id = (ClientID)p->Recv_uint32();

	NetworkClientInfo *ci = NetworkClientInfo::GetByClientID(client_id);
	if (ci != nullptr) {
		NetworkTextMessage(NETWORK_ACTION_LEAVE, CC_DEFAULT, false, ci->client_name, nullptr, GetNetworkErrorMsg((NetworkErrorCode)p->Recv_uint8()));
		delete ci;
	}

	SetWindowDirty(WC_CLIENT_LIST, 0);

	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::Receive_SERVER_QUIT(Packet *p)
{
	if (this->status < STATUS_AUTHORIZED) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	ClientID client_id = (ClientID)p->Recv_uint32();

	NetworkClientInfo *ci = NetworkClientInfo::GetByClientID(client_id);
	if (ci != nullptr) {
		NetworkTextMessage(NETWORK_ACTION_LEAVE, CC_DEFAULT, false, ci->client_name, nullptr, STR_NETWORK_MESSAGE_CLIENT_LEAVING);
		delete ci;
	} else {
		DEBUG(net, 0, "Unknown client (%d) is leaving the game", client_id);
	}

	SetWindowDirty(WC_CLIENT_LIST, 0);

	/* If we come here it means we could not locate the client.. strange :s */
	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::Receive_SERVER_JOIN(Packet *p)
{
	if (this->status < STATUS_AUTHORIZED) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	ClientID client_id = (ClientID)p->Recv_uint32();

	NetworkClientInfo *ci = NetworkClientInfo::GetByClientID(client_id);
	if (ci != nullptr) {
		NetworkTextMessage(NETWORK_ACTION_JOIN, CC_DEFAULT, false, ci->client_name);
	}

	SetWindowDirty(WC_CLIENT_LIST, 0);

	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::Receive_SERVER_SHUTDOWN(Packet *p)
{
	/* Only when we're trying to join we really
	 * care about the server shutting down. */
	if (this->status >= STATUS_JOIN) {
		ShowErrorMessage(STR_NETWORK_MESSAGE_SERVER_SHUTDOWN, INVALID_STRING_ID, WL_CRITICAL);
	}

	if (this->status == STATUS_ACTIVE) ClientNetworkEmergencySave();

	return NETWORK_RECV_STATUS_SERVER_ERROR;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::Receive_SERVER_NEWGAME(Packet *p)
{
	/* Only when we're trying to join we really
	 * care about the server shutting down. */
	if (this->status >= STATUS_JOIN) {
		/* To throttle the reconnects a bit, every clients waits its
		 * Client ID modulo 16. This way reconnects should be spread
		 * out a bit. */
		_network_reconnect = _network_own_client_id % 16;
		ShowErrorMessage(STR_NETWORK_MESSAGE_SERVER_REBOOT, INVALID_STRING_ID, WL_CRITICAL);
	}

	if (this->status == STATUS_ACTIVE) ClientNetworkEmergencySave();

	return NETWORK_RECV_STATUS_SERVER_ERROR;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::Receive_SERVER_RCON(Packet *p)
{
	if (this->status < STATUS_AUTHORIZED) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	TextColour colour_code = (TextColour)p->Recv_uint16();
	if (!IsValidConsoleColour(colour_code)) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	char rcon_out[NETWORK_RCONCOMMAND_LENGTH];
	p->Recv_string(rcon_out, sizeof(rcon_out));

	IConsolePrint(colour_code, rcon_out);

	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::Receive_SERVER_MOVE(Packet *p)
{
	if (this->status < STATUS_AUTHORIZED) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	/* Nothing more in this packet... */
	ClientID client_id   = (ClientID)p->Recv_uint32();
	CompanyID company_id = (CompanyID)p->Recv_uint8();

	if (client_id == 0) {
		/* definitely an invalid client id, debug message and do nothing. */
		DEBUG(net, 0, "[move] received invalid client index = 0");
		return NETWORK_RECV_STATUS_MALFORMED_PACKET;
	}

	const NetworkClientInfo *ci = NetworkClientInfo::GetByClientID(client_id);
	/* Just make sure we do not try to use a client_index that does not exist */
	if (ci == nullptr) return NETWORK_RECV_STATUS_OKAY;

	/* if not valid player, force spectator, else check player exists */
	if (!Company::IsValidID(company_id)) company_id = COMPANY_SPECTATOR;

	if (client_id == _network_own_client_id) {
		SetLocalCompany(company_id);
	}

	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::Receive_SERVER_CONFIG_UPDATE(Packet *p)
{
	if (this->status < STATUS_ACTIVE) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	_network_server_max_companies = p->Recv_uint8();
	_network_server_max_spectators = p->Recv_uint8();

	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ClientNetworkGameSocketHandler::Receive_SERVER_COMPANY_UPDATE(Packet *p)
{
	if (this->status < STATUS_ACTIVE) return NETWORK_RECV_STATUS_MALFORMED_PACKET;

	_network_company_passworded = p->Recv_uint16();
	SetWindowClassesDirty(WC_COMPANY);

	return NETWORK_RECV_STATUS_OKAY;
}

/**
 * Check the connection's state, i.e. is the connection still up?
 */
void ClientNetworkGameSocketHandler::CheckConnection()
{
	/* Only once we're authorized we can expect a steady stream of packets. */
	if (this->status < STATUS_AUTHORIZED) return;

	/* 5 seconds are roughly twice the server's "you're slow" threshold (1 game day). */
	std::chrono::steady_clock::duration lag = std::chrono::steady_clock::now() - this->last_packet;
	if (lag < std::chrono::seconds(5)) return;

	/* 20 seconds are (way) more than 4 game days after which
	 * the server will forcefully disconnect you. */
	if (lag > std::chrono::seconds(20)) {
		this->NetworkGameSocketHandler::CloseConnection();
		return;
	}

	/* Prevent showing the lag message every tick; just update it when needed. */
	static std::chrono::steady_clock::duration last_lag = {};
	if (std::chrono::duration_cast<std::chrono::seconds>(last_lag) == std::chrono::duration_cast<std::chrono::seconds>(lag)) return;

	last_lag = lag;
	SetDParam(0, std::chrono::duration_cast<std::chrono::seconds>(lag).count());
	ShowErrorMessage(STR_NETWORK_ERROR_CLIENT_GUI_LOST_CONNECTION_CAPTION, STR_NETWORK_ERROR_CLIENT_GUI_LOST_CONNECTION, WL_INFO);
}


/** Is called after a client is connected to the server */
void NetworkClient_Connected()
{
	/* Set the frame-counter to 0 so nothing happens till we are ready */
	_frame_counter = 0;
	_frame_counter_server = 0;
	last_ack_frame = 0;
	/* Request the game-info */
	MyClient::SendJoin();
}

/**
 * Send a remote console command.
 * @param password The password.
 * @param command The command to execute.
 */
void NetworkClientSendRcon(const char *password, const char *command)
{
	MyClient::SendRCon(password, command);
}

/**
 * Notify the server of this client wanting to be moved to another company.
 * @param company_id id of the company the client wishes to be moved to.
 * @param pass the password, is only checked on the server end if a password is needed.
 * @return void
 */
void NetworkClientRequestMove(CompanyID company_id, const char *pass)
{
	MyClient::SendMove(company_id, pass);
}

/**
 * Move the clients of a company to the spectators.
 * @param cid The company to move the clients of.
 */
void NetworkClientsToSpectators(CompanyID cid)
{
	Backup<CompanyID> cur_company(_current_company, FILE_LINE);
	/* If our company is changing owner, go to spectators */
	if (cid == _local_company) SetLocalCompany(COMPANY_SPECTATOR);

	for (NetworkClientInfo *ci : NetworkClientInfo::Iterate()) {
		if (ci->client_playas != cid) continue;
		NetworkTextMessage(NETWORK_ACTION_COMPANY_SPECTATOR, CC_DEFAULT, false, ci->client_name);
		ci->client_playas = COMPANY_SPECTATOR;
	}

	cur_company.Restore();
}

/**
 * Send the server our name.
 */
void NetworkUpdateClientName()
{
	NetworkClientInfo *ci = NetworkClientInfo::GetByClientID(_network_own_client_id);

	if (ci == nullptr) return;

	/* Don't change the name if it is the same as the old name */
	if (strcmp(ci->client_name, _settings_client.network.client_name) != 0) {
		if (!_network_server) {
			MyClient::SendSetName(_settings_client.network.client_name);
		} else {
			if (NetworkFindName(_settings_client.network.client_name, lastof(_settings_client.network.client_name))) {
				NetworkTextMessage(NETWORK_ACTION_NAME_CHANGE, CC_DEFAULT, false, ci->client_name, _settings_client.network.client_name);
				strecpy(ci->client_name, _settings_client.network.client_name, lastof(ci->client_name));
				NetworkUpdateClientInfo(CLIENT_ID_SERVER);
			}
		}
	}
}

/**
 * Send a chat message.
 * @param action The action associated with the message.
 * @param type The destination type.
 * @param dest The destination index, be it a company index or client id.
 * @param msg The actual message.
 * @param data Arbitrary extra data.
 */
void NetworkClientSendChat(NetworkAction action, DestType type, int dest, const char *msg, int64 data)
{
	MyClient::SendChat(action, type, dest, msg, data);
}

/**
 * Set/Reset company password on the client side.
 * @param password Password to be set.
 */
void NetworkClientSetCompanyPassword(const char *password)
{
	MyClient::SendSetPassword(password);
}

/**
 * Tell whether the client has team members where he/she can chat to.
 * @param cio client to check members of.
 * @return true if there is at least one team member.
 */
bool NetworkClientPreferTeamChat(const NetworkClientInfo *cio)
{
	/* Only companies actually playing can speak to team. Eg spectators cannot */
	if (!_settings_client.gui.prefer_teamchat || !Company::IsValidID(cio->client_playas)) return false;

	for (const NetworkClientInfo *ci : NetworkClientInfo::Iterate()) {
		if (ci->client_playas == cio->client_playas && ci != cio) return true;
	}

	return false;
}

/**
 * Check if max_companies has been reached on the server (local check only).
 * @return true if the max value has been reached or exceeded, false otherwise.
 */
bool NetworkMaxCompaniesReached()
{
	return Company::GetNumItems() >= (_network_server ? _settings_client.network.max_companies : _network_server_max_companies);
}

/**
 * Check if max_spectatos has been reached on the server (local check only).
 * @return true if the max value has been reached or exceeded, false otherwise.
 */
bool NetworkMaxSpectatorsReached()
{
	return NetworkSpectatorCount() >= (_network_server ? _settings_client.network.max_spectators : _network_server_max_spectators);
}
