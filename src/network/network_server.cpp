/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file network_server.cpp Server part of the network protocol. */

#include "../stdafx.h"
#include "../strings_func.h"
#include "../date_func.h"
#include "network_admin.h"
#include "network_server.h"
#include "network_udp.h"
#include "network_base.h"
#include "../console_func.h"
#include "../company_base.h"
#include "../command_func.h"
#include "../saveload/saveload.h"
#include "../saveload/saveload_filter.h"
#include "../station_base.h"
#include "../genworld.h"
#include "../company_func.h"
#include "../company_gui.h"
#include "../roadveh.h"
#include "../order_backup.h"
#include "../core/pool_func.hpp"
#include "../core/random_func.hpp"
#include "../rev.h"
#include <mutex>
#include <condition_variable>

#include "../safeguards.h"


/* This file handles all the server-commands */

DECLARE_POSTFIX_INCREMENT(ClientID)
/** The identifier counter for new clients (is never decreased) */
static ClientID _network_client_id = CLIENT_ID_FIRST;

/** Make very sure the preconditions given in network_type.h are actually followed */
assert_compile(MAX_CLIENT_SLOTS > MAX_CLIENTS);
/** Yes... */
assert_compile(NetworkClientSocketPool::MAX_SIZE == MAX_CLIENT_SLOTS);

/** The pool with clients. */
NetworkClientSocketPool _networkclientsocket_pool("NetworkClientSocket");
INSTANTIATE_POOL_METHODS(NetworkClientSocket)

/** Instantiate the listen sockets. */
template SocketList TCPListenHandler<ServerNetworkGameSocketHandler, PACKET_SERVER_FULL, PACKET_SERVER_BANNED>::sockets;

/** Writing a savegame directly to a number of packets. */
struct PacketWriter : SaveFilter {
	ServerNetworkGameSocketHandler *cs; ///< Socket we are associated with.
	Packet *current;                    ///< The packet we're currently writing to.
	size_t total_size;                  ///< Total size of the compressed savegame.
	Packet *packets;                    ///< Packet queue of the savegame; send these "slowly" to the client.
	std::mutex mutex;                   ///< Mutex for making threaded saving safe.
	std::condition_variable exit_sig;   ///< Signal for threaded destruction of this packet writer.

	/**
	 * Create the packet writer.
	 * @param cs The socket handler we're making the packets for.
	 */
	PacketWriter(ServerNetworkGameSocketHandler *cs) : SaveFilter(nullptr), cs(cs), current(nullptr), total_size(0), packets(nullptr)
	{
	}

	/** Make sure everything is cleaned up. */
	~PacketWriter()
	{
		std::unique_lock<std::mutex> lock(this->mutex);

		if (this->cs != nullptr) this->exit_sig.wait(lock);

		/* This must all wait until the Destroy function is called. */

		while (this->packets != nullptr) {
			Packet *p = this->packets->next;
			delete this->packets;
			this->packets = p;
		}

		delete this->current;
	}

	/**
	 * Begin the destruction of this packet writer. It can happen in two ways:
	 * in the first case the client disconnected while saving the map. In this
	 * case the saving has not finished and killed this PacketWriter. In that
	 * case we simply set cs to nullptr, triggering the appending to fail due to
	 * the connection problem and eventually triggering the destructor. In the
	 * second case the destructor is already called, and it is waiting for our
	 * signal which we will send. Only then the packets will be removed by the
	 * destructor.
	 */
	void Destroy()
	{
		std::unique_lock<std::mutex> lock(this->mutex);

		this->cs = nullptr;

		this->exit_sig.notify_all();
		lock.unlock();

		/* Make sure the saving is completely cancelled. Yes,
		 * we need to handle the save finish as well as the
		 * next connection might just be requesting a map. */
		WaitTillSaved();
		ProcessAsyncSaveFinish();
	}

	/**
	 * Checks whether there are packets.
	 * It's not 100% threading safe, but this is only asked for when checking
	 * whether there still is something to send. Then another call will be made
	 * to actually get the Packet, which will be the only one popping packets
	 * and thus eventually setting this on false.
	 */
	bool HasPackets()
	{
		return this->packets != nullptr;
	}

	/**
	 * Pop a single created packet from the queue with packets.
	 */
	Packet *PopPacket()
	{
		std::lock_guard<std::mutex> lock(this->mutex);

		Packet *p = this->packets;
		this->packets = p->next;
		p->next = nullptr;

		return p;
	}

	/** Append the current packet to the queue. */
	void AppendQueue()
	{
		if (this->current == nullptr) return;

		Packet **p = &this->packets;
		while (*p != nullptr) {
			p = &(*p)->next;
		}
		*p = this->current;

		this->current = nullptr;
	}

	/** Prepend the current packet to the queue. */
	void PrependQueue()
	{
		if (this->current == nullptr) return;

		this->current->next = this->packets;
		this->packets = this->current;
		this->current = nullptr;
	}

	void Write(byte *buf, size_t size) override
	{
		/* We want to abort the saving when the socket is closed. */
		if (this->cs == nullptr) SlError(STR_NETWORK_ERROR_LOSTCONNECTION);

		if (this->current == nullptr) this->current = new Packet(PACKET_SERVER_MAP_DATA);

		std::lock_guard<std::mutex> lock(this->mutex);

		byte *bufe = buf + size;
		while (buf != bufe) {
			size_t to_write = min(SEND_MTU - this->current->size, bufe - buf);
			memcpy(this->current->buffer + this->current->size, buf, to_write);
			this->current->size += (PacketSize)to_write;
			buf += to_write;

			if (this->current->size == SEND_MTU) {
				this->AppendQueue();
				if (buf != bufe) this->current = new Packet(PACKET_SERVER_MAP_DATA);
			}
		}

		this->total_size += size;
	}

	void Finish() override
	{
		/* We want to abort the saving when the socket is closed. */
		if (this->cs == nullptr) SlError(STR_NETWORK_ERROR_LOSTCONNECTION);

		std::lock_guard<std::mutex> lock(this->mutex);

		/* Make sure the last packet is flushed. */
		this->AppendQueue();

		/* Add a packet stating that this is the end to the queue. */
		this->current = new Packet(PACKET_SERVER_MAP_DONE);
		this->AppendQueue();

		/* Fast-track the size to the client. */
		this->current = new Packet(PACKET_SERVER_MAP_SIZE);
		this->current->Send_uint32((uint32)this->total_size);
		this->PrependQueue();
	}
};


/**
 * Create a new socket for the server side of the game connection.
 * @param s The socket to connect with.
 */
ServerNetworkGameSocketHandler::ServerNetworkGameSocketHandler(SOCKET s) : NetworkGameSocketHandler(s)
{
	this->status = STATUS_INACTIVE;
	this->client_id = _network_client_id++;
	this->receive_limit = _settings_client.network.bytes_per_frame_burst;

	/* The Socket and Info pools need to be the same in size. After all,
	 * each Socket will be associated with at most one Info object. As
	 * such if the Socket was allocated the Info object can as well. */
	assert_compile(NetworkClientSocketPool::MAX_SIZE == NetworkClientInfoPool::MAX_SIZE);
}

/**
 * Clear everything related to this client.
 */
ServerNetworkGameSocketHandler::~ServerNetworkGameSocketHandler()
{
	if (_redirect_console_to_client == this->client_id) _redirect_console_to_client = INVALID_CLIENT_ID;
	OrderBackup::ResetUser(this->client_id);

	if (this->savegame != nullptr) {
		this->savegame->Destroy();
		this->savegame = nullptr;
	}
}

Packet *ServerNetworkGameSocketHandler::ReceivePacket()
{
	/* Only allow receiving when we have some buffer free; this value
	 * can go negative, but eventually it will become positive again. */
	if (this->receive_limit <= 0) return nullptr;

	/* We can receive a packet, so try that and if needed account for
	 * the amount of received data. */
	Packet *p = this->NetworkTCPSocketHandler::ReceivePacket();
	if (p != nullptr) this->receive_limit -= p->size;
	return p;
}

NetworkRecvStatus ServerNetworkGameSocketHandler::CloseConnection(NetworkRecvStatus status)
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

	if (status != NETWORK_RECV_STATUS_CONN_LOST && !this->HasClientQuit() && this->status >= STATUS_AUTHORIZED) {
		/* We did not receive a leave message from this client... */
		char client_name[NETWORK_CLIENT_NAME_LENGTH];

		this->GetClientName(client_name, lastof(client_name));

		NetworkTextMessage(NETWORK_ACTION_LEAVE, CC_DEFAULT, false, client_name, nullptr, STR_NETWORK_ERROR_CLIENT_CONNECTION_LOST);

		/* Inform other clients of this... strange leaving ;) */
		for (NetworkClientSocket *new_cs : NetworkClientSocket::Iterate()) {
			if (new_cs->status > STATUS_AUTHORIZED && this != new_cs) {
				new_cs->SendErrorQuit(this->client_id, NETWORK_ERROR_CONNECTION_LOST);
			}
		}
	}

	NetworkAdminClientError(this->client_id, NETWORK_ERROR_CONNECTION_LOST);
	DEBUG(net, 1, "Closed client connection %d", this->client_id);

	/* We just lost one client :( */
	if (this->status >= STATUS_AUTHORIZED) _network_game_info.clients_on--;
	extern byte _network_clients_connected;
	_network_clients_connected--;

	DeleteWindowById(WC_CLIENT_LIST_POPUP, this->client_id);
	SetWindowDirty(WC_CLIENT_LIST, 0);

	this->SendPackets(true);

	delete this->GetInfo();
	delete this;

	return status;
}

/**
 * Whether an connection is allowed or not at this moment.
 * @return true if the connection is allowed.
 */
/* static */ bool ServerNetworkGameSocketHandler::AllowConnection()
{
	extern byte _network_clients_connected;
	bool accept = _network_clients_connected < MAX_CLIENTS && _network_game_info.clients_on < _settings_client.network.max_clients;

	/* We can't go over the MAX_CLIENTS limit here. However, the
	 * pool must have place for all clients and ourself. */
	assert_compile(NetworkClientSocketPool::MAX_SIZE == MAX_CLIENTS + 1);
	assert(!accept || ServerNetworkGameSocketHandler::CanAllocateItem());
	return accept;
}

/** Send the packets for the server sockets. */
/* static */ void ServerNetworkGameSocketHandler::Send()
{
	for (NetworkClientSocket *cs : NetworkClientSocket::Iterate()) {
		if (cs->writable) {
			if (cs->SendPackets() != SPS_CLOSED && cs->status == STATUS_MAP) {
				/* This client is in the middle of a map-send, call the function for that */
				cs->SendMap();
			}
		}
	}
}

static void NetworkHandleCommandQueue(NetworkClientSocket *cs);

/***********
 * Sending functions
 *   DEF_SERVER_SEND_COMMAND has parameter: NetworkClientSocket *cs
 ************/

/**
 * Send the client information about a client.
 * @param ci The client to send information about.
 */
NetworkRecvStatus ServerNetworkGameSocketHandler::SendClientInfo(NetworkClientInfo *ci)
{
	if (ci->client_id != INVALID_CLIENT_ID) {
		Packet *p = new Packet(PACKET_SERVER_CLIENT_INFO);
		p->Send_uint32(ci->client_id);
		p->Send_uint8 (ci->client_playas);
		p->Send_string(ci->client_name);

		this->SendPacket(p);
	}
	return NETWORK_RECV_STATUS_OKAY;
}

/** Send the client information about the companies. */
NetworkRecvStatus ServerNetworkGameSocketHandler::SendCompanyInfo()
{
	/* Fetch the latest version of the stats */
	NetworkCompanyStats company_stats[MAX_COMPANIES];
	NetworkPopulateCompanyStats(company_stats);

	/* Make a list of all clients per company */
	char clients[MAX_COMPANIES][NETWORK_CLIENTS_LENGTH];
	memset(clients, 0, sizeof(clients));

	/* Add the local player (if not dedicated) */
	const NetworkClientInfo *ci = NetworkClientInfo::GetByClientID(CLIENT_ID_SERVER);
	if (ci != nullptr && Company::IsValidID(ci->client_playas)) {
		strecpy(clients[ci->client_playas], ci->client_name, lastof(clients[ci->client_playas]));
	}

	for (NetworkClientSocket *csi : NetworkClientSocket::Iterate()) {
		char client_name[NETWORK_CLIENT_NAME_LENGTH];

		((ServerNetworkGameSocketHandler*)csi)->GetClientName(client_name, lastof(client_name));

		ci = csi->GetInfo();
		if (ci != nullptr && Company::IsValidID(ci->client_playas)) {
			if (!StrEmpty(clients[ci->client_playas])) {
				strecat(clients[ci->client_playas], ", ", lastof(clients[ci->client_playas]));
			}

			strecat(clients[ci->client_playas], client_name, lastof(clients[ci->client_playas]));
		}
	}

	/* Now send the data */

	Packet *p;

	for (const Company *company : Company::Iterate()) {
		p = new Packet(PACKET_SERVER_COMPANY_INFO);

		p->Send_uint8 (NETWORK_COMPANY_INFO_VERSION);
		p->Send_bool  (true);
		this->SendCompanyInformation(p, company, &company_stats[company->index]);

		if (StrEmpty(clients[company->index])) {
			p->Send_string("<none>");
		} else {
			p->Send_string(clients[company->index]);
		}

		this->SendPacket(p);
	}

	p = new Packet(PACKET_SERVER_COMPANY_INFO);

	p->Send_uint8 (NETWORK_COMPANY_INFO_VERSION);
	p->Send_bool  (false);

	this->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/**
 * Send an error to the client, and close its connection.
 * @param error The error to disconnect for.
 * @param reason In case of kicking a client, specifies the reason for kicking the client.
 */
NetworkRecvStatus ServerNetworkGameSocketHandler::SendError(NetworkErrorCode error, const char *reason)
{
	char str[100];
	Packet *p = new Packet(PACKET_SERVER_ERROR);

	p->Send_uint8(error);
	if (reason != nullptr) p->Send_string(reason);
	this->SendPacket(p);

	StringID strid = GetNetworkErrorMsg(error);
	GetString(str, strid, lastof(str));

	/* Only send when the current client was in game */
	if (this->status > STATUS_AUTHORIZED) {
		char client_name[NETWORK_CLIENT_NAME_LENGTH];

		this->GetClientName(client_name, lastof(client_name));

		DEBUG(net, 1, "'%s' made an error and has been disconnected. Reason: '%s'", client_name, str);

		if (error == NETWORK_ERROR_KICKED && reason != nullptr) {
			NetworkTextMessage(NETWORK_ACTION_KICKED, CC_DEFAULT, false, client_name, reason, strid);
		} else {
			NetworkTextMessage(NETWORK_ACTION_LEAVE, CC_DEFAULT, false, client_name, nullptr, strid);
		}

		for (NetworkClientSocket *new_cs : NetworkClientSocket::Iterate()) {
			if (new_cs->status > STATUS_AUTHORIZED && new_cs != this) {
				/* Some errors we filter to a more general error. Clients don't have to know the real
				 *  reason a joining failed. */
				if (error == NETWORK_ERROR_NOT_AUTHORIZED || error == NETWORK_ERROR_NOT_EXPECTED || error == NETWORK_ERROR_WRONG_REVISION) {
					error = NETWORK_ERROR_ILLEGAL_PACKET;
				}
				new_cs->SendErrorQuit(this->client_id, error);
			}
		}

		NetworkAdminClientError(this->client_id, error);
	} else {
		DEBUG(net, 1, "Client %d made an error and has been disconnected. Reason: '%s'", this->client_id, str);
	}

	/* The client made a mistake, so drop his connection now! */
	return this->CloseConnection(NETWORK_RECV_STATUS_SERVER_ERROR);
}

/** Send the check for the NewGRFs. */
NetworkRecvStatus ServerNetworkGameSocketHandler::SendNewGRFCheck()
{
	Packet *p = new Packet(PACKET_SERVER_CHECK_NEWGRFS);
	const GRFConfig *c;
	uint grf_count = 0;

	for (c = _grfconfig; c != nullptr; c = c->next) {
		if (!HasBit(c->flags, GCF_STATIC)) grf_count++;
	}

	p->Send_uint8 (grf_count);
	for (c = _grfconfig; c != nullptr; c = c->next) {
		if (!HasBit(c->flags, GCF_STATIC)) this->SendGRFIdentifier(p, &c->ident);
	}

	this->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/** Request the game password. */
NetworkRecvStatus ServerNetworkGameSocketHandler::SendNeedGamePassword()
{
	/* Invalid packet when status is STATUS_AUTH_GAME or higher */
	if (this->status >= STATUS_AUTH_GAME) return this->CloseConnection(NETWORK_RECV_STATUS_MALFORMED_PACKET);

	this->status = STATUS_AUTH_GAME;
	/* Reset 'lag' counters */
	this->last_frame = this->last_frame_server = _frame_counter;

	Packet *p = new Packet(PACKET_SERVER_NEED_GAME_PASSWORD);
	this->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/** Request the company password. */
NetworkRecvStatus ServerNetworkGameSocketHandler::SendNeedCompanyPassword()
{
	/* Invalid packet when status is STATUS_AUTH_COMPANY or higher */
	if (this->status >= STATUS_AUTH_COMPANY) return this->CloseConnection(NETWORK_RECV_STATUS_MALFORMED_PACKET);

	this->status = STATUS_AUTH_COMPANY;
	/* Reset 'lag' counters */
	this->last_frame = this->last_frame_server = _frame_counter;

	Packet *p = new Packet(PACKET_SERVER_NEED_COMPANY_PASSWORD);
	p->Send_uint32(_settings_game.game_creation.generation_seed);
	p->Send_string(_settings_client.network.network_id);
	this->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/** Send the client a welcome message with some basic information. */
NetworkRecvStatus ServerNetworkGameSocketHandler::SendWelcome()
{
	Packet *p;

	/* Invalid packet when status is AUTH or higher */
	if (this->status >= STATUS_AUTHORIZED) return this->CloseConnection(NETWORK_RECV_STATUS_MALFORMED_PACKET);

	this->status = STATUS_AUTHORIZED;
	/* Reset 'lag' counters */
	this->last_frame = this->last_frame_server = _frame_counter;

	_network_game_info.clients_on++;

	p = new Packet(PACKET_SERVER_WELCOME);
	p->Send_uint32(this->client_id);
	p->Send_uint32(_settings_game.game_creation.generation_seed);
	p->Send_string(_settings_client.network.network_id);
	this->SendPacket(p);

	/* Transmit info about all the active clients */
	for (NetworkClientSocket *new_cs : NetworkClientSocket::Iterate()) {
		if (new_cs != this && new_cs->status > STATUS_AUTHORIZED) {
			this->SendClientInfo(new_cs->GetInfo());
		}
	}
	/* Also send the info of the server */
	return this->SendClientInfo(NetworkClientInfo::GetByClientID(CLIENT_ID_SERVER));
}

/** Tell the client that its put in a waiting queue. */
NetworkRecvStatus ServerNetworkGameSocketHandler::SendWait()
{
	int waiting = 0;
	Packet *p;

	/* Count how many clients are waiting in the queue, in front of you! */
	for (NetworkClientSocket *new_cs : NetworkClientSocket::Iterate()) {
		if (new_cs->status != STATUS_MAP_WAIT) continue;
		if (new_cs->GetInfo()->join_date < this->GetInfo()->join_date || (new_cs->GetInfo()->join_date == this->GetInfo()->join_date && new_cs->client_id < this->client_id)) waiting++;
	}

	p = new Packet(PACKET_SERVER_WAIT);
	p->Send_uint8(waiting);
	this->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/** This sends the map to the client */
NetworkRecvStatus ServerNetworkGameSocketHandler::SendMap()
{
	static uint sent_packets; // How many packets we did send successfully last time

	if (this->status < STATUS_AUTHORIZED) {
		/* Illegal call, return error and ignore the packet */
		return this->SendError(NETWORK_ERROR_NOT_AUTHORIZED);
	}

	if (this->status == STATUS_AUTHORIZED) {
		this->savegame = new PacketWriter(this);

		/* Now send the _frame_counter and how many packets are coming */
		Packet *p = new Packet(PACKET_SERVER_MAP_BEGIN);
		p->Send_uint32(_frame_counter);
		this->SendPacket(p);

		NetworkSyncCommandQueue(this);
		this->status = STATUS_MAP;
		/* Mark the start of download */
		this->last_frame = _frame_counter;
		this->last_frame_server = _frame_counter;

		sent_packets = 4; // We start with trying 4 packets

		/* Make a dump of the current game */
		if (SaveWithFilter(this->savegame, true) != SL_OK) usererror("network savedump failed");
	}

	if (this->status == STATUS_MAP) {
		bool last_packet = false;
		bool has_packets = false;

		for (uint i = 0; (has_packets = this->savegame->HasPackets()) && i < sent_packets; i++) {
			Packet *p = this->savegame->PopPacket();
			last_packet = p->buffer[2] == PACKET_SERVER_MAP_DONE;

			this->SendPacket(p);

			if (last_packet) {
				/* There is no more data, so break the for */
				break;
			}
		}

		if (last_packet) {
			/* Done reading, make sure saving is done as well */
			this->savegame->Destroy();
			this->savegame = nullptr;

			/* Set the status to DONE_MAP, no we will wait for the client
			 *  to send it is ready (maybe that happens like never ;)) */
			this->status = STATUS_DONE_MAP;

			/* Find the best candidate for joining, i.e. the first joiner. */
			NetworkClientSocket *best = nullptr;
			for (NetworkClientSocket *new_cs : NetworkClientSocket::Iterate()) {
				if (new_cs->status == STATUS_MAP_WAIT) {
					if (best == nullptr || best->GetInfo()->join_date > new_cs->GetInfo()->join_date || (best->GetInfo()->join_date == new_cs->GetInfo()->join_date && best->client_id > new_cs->client_id)) {
						best = new_cs;
					}
				}
			}

			/* Is there someone else to join? */
			if (best != nullptr) {
				/* Let the first start joining. */
				best->status = STATUS_AUTHORIZED;
				best->SendMap();

				/* And update the rest. */
				for (NetworkClientSocket *new_cs : NetworkClientSocket::Iterate()) {
					if (new_cs->status == STATUS_MAP_WAIT) new_cs->SendWait();
				}
			}
		}

		switch (this->SendPackets()) {
			case SPS_CLOSED:
				return NETWORK_RECV_STATUS_CONN_LOST;

			case SPS_ALL_SENT:
				/* All are sent, increase the sent_packets */
				if (has_packets) sent_packets *= 2;
				break;

			case SPS_PARTLY_SENT:
				/* Only a part is sent; leave the transmission state. */
				break;

			case SPS_NONE_SENT:
				/* Not everything is sent, decrease the sent_packets */
				if (sent_packets > 1) sent_packets /= 2;
				break;
		}
	}
	return NETWORK_RECV_STATUS_OKAY;
}

/**
 * Tell that a client joined.
 * @param client_id The client that joined.
 */
NetworkRecvStatus ServerNetworkGameSocketHandler::SendJoin(ClientID client_id)
{
	Packet *p = new Packet(PACKET_SERVER_JOIN);

	p->Send_uint32(client_id);

	this->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/** Tell the client that they may run to a particular frame. */
NetworkRecvStatus ServerNetworkGameSocketHandler::SendFrame()
{
	Packet *p = new Packet(PACKET_SERVER_FRAME);
	p->Send_uint32(_frame_counter);
	p->Send_uint32(_frame_counter_max);
#ifdef ENABLE_NETWORK_SYNC_EVERY_FRAME
	p->Send_uint32(_sync_seed_1);
#ifdef NETWORK_SEND_DOUBLE_SEED
	p->Send_uint32(_sync_seed_2);
#endif
#endif

	/* If token equals 0, we need to make a new token and send that. */
	if (this->last_token == 0) {
		this->last_token = InteractiveRandomRange(UINT8_MAX - 1) + 1;
		p->Send_uint8(this->last_token);
	}

	this->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/** Request the client to sync. */
NetworkRecvStatus ServerNetworkGameSocketHandler::SendSync()
{
	Packet *p = new Packet(PACKET_SERVER_SYNC);
	p->Send_uint32(_frame_counter);
	p->Send_uint32(_sync_seed_1);

#ifdef NETWORK_SEND_DOUBLE_SEED
	p->Send_uint32(_sync_seed_2);
#endif
	this->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/**
 * Send a command to the client to execute.
 * @param cp The command to send.
 */
NetworkRecvStatus ServerNetworkGameSocketHandler::SendCommand(const CommandPacket *cp)
{
	Packet *p = new Packet(PACKET_SERVER_COMMAND);

	this->NetworkGameSocketHandler::SendCommand(p, cp);
	p->Send_uint32(cp->frame);
	p->Send_bool  (cp->my_cmd);

	this->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/**
 * Send a chat message.
 * @param action The action associated with the message.
 * @param client_id The origin of the chat message.
 * @param self_send Whether we did send the message.
 * @param msg The actual message.
 * @param data Arbitrary extra data.
 */
NetworkRecvStatus ServerNetworkGameSocketHandler::SendChat(NetworkAction action, ClientID client_id, bool self_send, const char *msg, int64 data)
{
	if (this->status < STATUS_PRE_ACTIVE) return NETWORK_RECV_STATUS_OKAY;

	Packet *p = new Packet(PACKET_SERVER_CHAT);

	p->Send_uint8 (action);
	p->Send_uint32(client_id);
	p->Send_bool  (self_send);
	p->Send_string(msg);
	p->Send_uint64(data);

	this->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/**
 * Tell the client another client quit with an error.
 * @param client_id The client that quit.
 * @param errorno The reason the client quit.
 */
NetworkRecvStatus ServerNetworkGameSocketHandler::SendErrorQuit(ClientID client_id, NetworkErrorCode errorno)
{
	Packet *p = new Packet(PACKET_SERVER_ERROR_QUIT);

	p->Send_uint32(client_id);
	p->Send_uint8 (errorno);

	this->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/**
 * Tell the client another client quit.
 * @param client_id The client that quit.
 */
NetworkRecvStatus ServerNetworkGameSocketHandler::SendQuit(ClientID client_id)
{
	Packet *p = new Packet(PACKET_SERVER_QUIT);

	p->Send_uint32(client_id);

	this->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/** Tell the client we're shutting down. */
NetworkRecvStatus ServerNetworkGameSocketHandler::SendShutdown()
{
	Packet *p = new Packet(PACKET_SERVER_SHUTDOWN);
	this->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/** Tell the client we're starting a new game. */
NetworkRecvStatus ServerNetworkGameSocketHandler::SendNewGame()
{
	Packet *p = new Packet(PACKET_SERVER_NEWGAME);
	this->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/**
 * Send the result of a console action.
 * @param colour The colour of the result.
 * @param command The command that was executed.
 */
NetworkRecvStatus ServerNetworkGameSocketHandler::SendRConResult(uint16 colour, const char *command)
{
	Packet *p = new Packet(PACKET_SERVER_RCON);

	p->Send_uint16(colour);
	p->Send_string(command);
	this->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/**
 * Tell that a client moved to another company.
 * @param client_id The client that moved.
 * @param company_id The company the client moved to.
 */
NetworkRecvStatus ServerNetworkGameSocketHandler::SendMove(ClientID client_id, CompanyID company_id)
{
	Packet *p = new Packet(PACKET_SERVER_MOVE);

	p->Send_uint32(client_id);
	p->Send_uint8(company_id);
	this->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/** Send an update about the company password states. */
NetworkRecvStatus ServerNetworkGameSocketHandler::SendCompanyUpdate()
{
	Packet *p = new Packet(PACKET_SERVER_COMPANY_UPDATE);

	p->Send_uint16(_network_company_passworded);
	this->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/** Send an update about the max company/spectator counts. */
NetworkRecvStatus ServerNetworkGameSocketHandler::SendConfigUpdate()
{
	Packet *p = new Packet(PACKET_SERVER_CONFIG_UPDATE);

	p->Send_uint8(_settings_client.network.max_companies);
	p->Send_uint8(_settings_client.network.max_spectators);
	this->SendPacket(p);
	return NETWORK_RECV_STATUS_OKAY;
}

/***********
 * Receiving functions
 *   DEF_SERVER_RECEIVE_COMMAND has parameter: NetworkClientSocket *cs, Packet *p
 ************/

NetworkRecvStatus ServerNetworkGameSocketHandler::Receive_CLIENT_COMPANY_INFO(Packet *p)
{
	return this->SendCompanyInfo();
}

NetworkRecvStatus ServerNetworkGameSocketHandler::Receive_CLIENT_NEWGRFS_CHECKED(Packet *p)
{
	if (this->status != STATUS_NEWGRFS_CHECK) {
		/* Illegal call, return error and ignore the packet */
		return this->SendError(NETWORK_ERROR_NOT_EXPECTED);
	}

	NetworkClientInfo *ci = this->GetInfo();

	/* We now want a password from the client else we do not allow him in! */
	if (!StrEmpty(_settings_client.network.server_password)) {
		return this->SendNeedGamePassword();
	}

	if (Company::IsValidID(ci->client_playas) && !StrEmpty(_network_company_states[ci->client_playas].password)) {
		return this->SendNeedCompanyPassword();
	}

	return this->SendWelcome();
}

NetworkRecvStatus ServerNetworkGameSocketHandler::Receive_CLIENT_JOIN(Packet *p)
{
	if (this->status != STATUS_INACTIVE) {
		/* Illegal call, return error and ignore the packet */
		return this->SendError(NETWORK_ERROR_NOT_EXPECTED);
	}

	char name[NETWORK_CLIENT_NAME_LENGTH];
	CompanyID playas;
	NetworkLanguage client_lang;
	char client_revision[NETWORK_REVISION_LENGTH];

	p->Recv_string(client_revision, sizeof(client_revision));
	uint32 newgrf_version = p->Recv_uint32();

	/* Check if the client has revision control enabled */
	if (!IsNetworkCompatibleVersion(client_revision) || _openttd_newgrf_version != newgrf_version) {
		/* Different revisions!! */
		return this->SendError(NETWORK_ERROR_WRONG_REVISION);
	}

	p->Recv_string(name, sizeof(name));
	playas = (Owner)p->Recv_uint8();
	client_lang = (NetworkLanguage)p->Recv_uint8();

	if (this->HasClientQuit()) return NETWORK_RECV_STATUS_CONN_LOST;

	/* join another company does not affect these values */
	switch (playas) {
		case COMPANY_NEW_COMPANY: // New company
			if (Company::GetNumItems() >= _settings_client.network.max_companies) {
				return this->SendError(NETWORK_ERROR_FULL);
			}
			break;
		case COMPANY_SPECTATOR: // Spectator
			if (NetworkSpectatorCount() >= _settings_client.network.max_spectators) {
				return this->SendError(NETWORK_ERROR_FULL);
			}
			break;
		default: // Join another company (companies 1-8 (index 0-7))
			if (!Company::IsValidHumanID(playas)) {
				return this->SendError(NETWORK_ERROR_COMPANY_MISMATCH);
			}
			break;
	}

	/* We need a valid name.. make it Player */
	if (StrEmpty(name)) strecpy(name, "Player", lastof(name));

	if (!NetworkFindName(name, lastof(name))) { // Change name if duplicate
		/* We could not create a name for this client */
		return this->SendError(NETWORK_ERROR_NAME_IN_USE);
	}

	assert(NetworkClientInfo::CanAllocateItem());
	NetworkClientInfo *ci = new NetworkClientInfo(this->client_id);
	this->SetInfo(ci);
	ci->join_date = _date;
	strecpy(ci->client_name, name, lastof(ci->client_name));
	ci->client_playas = playas;
	ci->client_lang = client_lang;
	DEBUG(desync, 1, "client: %08x; %02x; %02x; %02x", _date, _date_fract, (int)ci->client_playas, (int)ci->index);

	/* Make sure companies to which people try to join are not autocleaned */
	if (Company::IsValidID(playas)) _network_company_states[playas].months_empty = 0;

	this->status = STATUS_NEWGRFS_CHECK;

	if (_grfconfig == nullptr) {
		/* Behave as if we received PACKET_CLIENT_NEWGRFS_CHECKED */
		return this->Receive_CLIENT_NEWGRFS_CHECKED(nullptr);
	}

	return this->SendNewGRFCheck();
}

NetworkRecvStatus ServerNetworkGameSocketHandler::Receive_CLIENT_GAME_PASSWORD(Packet *p)
{
	if (this->status != STATUS_AUTH_GAME) {
		return this->SendError(NETWORK_ERROR_NOT_EXPECTED);
	}

	char password[NETWORK_PASSWORD_LENGTH];
	p->Recv_string(password, sizeof(password));

	/* Check game password. Allow joining if we cleared the password meanwhile */
	if (!StrEmpty(_settings_client.network.server_password) &&
			strcmp(password, _settings_client.network.server_password) != 0) {
		/* Password is invalid */
		return this->SendError(NETWORK_ERROR_WRONG_PASSWORD);
	}

	const NetworkClientInfo *ci = this->GetInfo();
	if (Company::IsValidID(ci->client_playas) && !StrEmpty(_network_company_states[ci->client_playas].password)) {
		return this->SendNeedCompanyPassword();
	}

	/* Valid password, allow user */
	return this->SendWelcome();
}

NetworkRecvStatus ServerNetworkGameSocketHandler::Receive_CLIENT_COMPANY_PASSWORD(Packet *p)
{
	if (this->status != STATUS_AUTH_COMPANY) {
		return this->SendError(NETWORK_ERROR_NOT_EXPECTED);
	}

	char password[NETWORK_PASSWORD_LENGTH];
	p->Recv_string(password, sizeof(password));

	/* Check company password. Allow joining if we cleared the password meanwhile.
	 * Also, check the company is still valid - client could be moved to spectators
	 * in the middle of the authorization process */
	CompanyID playas = this->GetInfo()->client_playas;
	if (Company::IsValidID(playas) && !StrEmpty(_network_company_states[playas].password) &&
			strcmp(password, _network_company_states[playas].password) != 0) {
		/* Password is invalid */
		return this->SendError(NETWORK_ERROR_WRONG_PASSWORD);
	}

	return this->SendWelcome();
}

NetworkRecvStatus ServerNetworkGameSocketHandler::Receive_CLIENT_GETMAP(Packet *p)
{
	/* The client was never joined.. so this is impossible, right?
	 *  Ignore the packet, give the client a warning, and close his connection */
	if (this->status < STATUS_AUTHORIZED || this->HasClientQuit()) {
		return this->SendError(NETWORK_ERROR_NOT_AUTHORIZED);
	}

	/* Check if someone else is receiving the map */
	for (NetworkClientSocket *new_cs : NetworkClientSocket::Iterate()) {
		if (new_cs->status == STATUS_MAP) {
			/* Tell the new client to wait */
			this->status = STATUS_MAP_WAIT;
			return this->SendWait();
		}
	}

	/* We receive a request to upload the map.. give it to the client! */
	return this->SendMap();
}

NetworkRecvStatus ServerNetworkGameSocketHandler::Receive_CLIENT_MAP_OK(Packet *p)
{
	/* Client has the map, now start syncing */
	if (this->status == STATUS_DONE_MAP && !this->HasClientQuit()) {
		char client_name[NETWORK_CLIENT_NAME_LENGTH];

		this->GetClientName(client_name, lastof(client_name));

		NetworkTextMessage(NETWORK_ACTION_JOIN, CC_DEFAULT, false, client_name, nullptr, this->client_id);

		/* Mark the client as pre-active, and wait for an ACK
		 *  so we know he is done loading and in sync with us */
		this->status = STATUS_PRE_ACTIVE;
		NetworkHandleCommandQueue(this);
		this->SendFrame();
		this->SendSync();

		/* This is the frame the client receives
		 *  we need it later on to make sure the client is not too slow */
		this->last_frame = _frame_counter;
		this->last_frame_server = _frame_counter;

		for (NetworkClientSocket *new_cs : NetworkClientSocket::Iterate()) {
			if (new_cs->status > STATUS_AUTHORIZED) {
				new_cs->SendClientInfo(this->GetInfo());
				new_cs->SendJoin(this->client_id);
			}
		}

		NetworkAdminClientInfo(this, true);

		/* also update the new client with our max values */
		this->SendConfigUpdate();

		/* quickly update the syncing client with company details */
		return this->SendCompanyUpdate();
	}

	/* Wrong status for this packet, give a warning to client, and close connection */
	return this->SendError(NETWORK_ERROR_NOT_EXPECTED);
}

/**
 * The client has done a command and wants us to handle it
 * @param p the packet in which the command was sent
 */
NetworkRecvStatus ServerNetworkGameSocketHandler::Receive_CLIENT_COMMAND(Packet *p)
{
	/* The client was never joined.. so this is impossible, right?
	 *  Ignore the packet, give the client a warning, and close his connection */
	if (this->status < STATUS_DONE_MAP || this->HasClientQuit()) {
		return this->SendError(NETWORK_ERROR_NOT_EXPECTED);
	}

	if (this->incoming_queue.Count() >= _settings_client.network.max_commands_in_queue) {
		return this->SendError(NETWORK_ERROR_TOO_MANY_COMMANDS);
	}

	CommandPacket cp;
	const char *err = this->ReceiveCommand(p, &cp);

	if (this->HasClientQuit()) return NETWORK_RECV_STATUS_CONN_LOST;

	NetworkClientInfo *ci = this->GetInfo();

	if (err != nullptr) {
		IConsolePrintF(CC_ERROR, "WARNING: %s from client %d (IP: %s).", err, ci->client_id, this->GetClientIP());
		return this->SendError(NETWORK_ERROR_NOT_EXPECTED);
	}


	if ((GetCommandFlags(cp.cmd) & CMD_SERVER) && ci->client_id != CLIENT_ID_SERVER) {
		IConsolePrintF(CC_ERROR, "WARNING: server only command from: client %d (IP: %s), kicking...", ci->client_id, this->GetClientIP());
		return this->SendError(NETWORK_ERROR_KICKED);
	}

	if ((GetCommandFlags(cp.cmd) & CMD_SPECTATOR) == 0 && !Company::IsValidID(cp.company) && ci->client_id != CLIENT_ID_SERVER) {
		IConsolePrintF(CC_ERROR, "WARNING: spectator issuing command from client %d (IP: %s), kicking...", ci->client_id, this->GetClientIP());
		return this->SendError(NETWORK_ERROR_KICKED);
	}

	/**
	 * Only CMD_COMPANY_CTRL is always allowed, for the rest, playas needs
	 * to match the company in the packet. If it doesn't, the client has done
	 * something pretty naughty (or a bug), and will be kicked
	 */
	if (!(cp.cmd == CMD_COMPANY_CTRL && cp.p1 == 0 && ci->client_playas == COMPANY_NEW_COMPANY) && ci->client_playas != cp.company) {
		IConsolePrintF(CC_ERROR, "WARNING: client %d (IP: %s) tried to execute a command as company %d, kicking...",
		               ci->client_playas + 1, this->GetClientIP(), cp.company + 1);
		return this->SendError(NETWORK_ERROR_COMPANY_MISMATCH);
	}

	if (cp.cmd == CMD_COMPANY_CTRL) {
		if (cp.p1 != 0 || cp.company != COMPANY_SPECTATOR) {
			return this->SendError(NETWORK_ERROR_CHEATER);
		}

		/* Check if we are full - else it's possible for spectators to send a CMD_COMPANY_CTRL and the company is created regardless of max_companies! */
		if (Company::GetNumItems() >= _settings_client.network.max_companies) {
			NetworkServerSendChat(NETWORK_ACTION_SERVER_MESSAGE, DESTTYPE_CLIENT, ci->client_id, "cannot create new company, server full", CLIENT_ID_SERVER);
			return NETWORK_RECV_STATUS_OKAY;
		}
	}

	if (GetCommandFlags(cp.cmd) & CMD_CLIENT_ID) cp.p2 = this->client_id;

	this->incoming_queue.Append(&cp);
	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ServerNetworkGameSocketHandler::Receive_CLIENT_ERROR(Packet *p)
{
	/* This packets means a client noticed an error and is reporting this
	 *  to us. Display the error and report it to the other clients */
	char str[100];
	char client_name[NETWORK_CLIENT_NAME_LENGTH];
	NetworkErrorCode errorno = (NetworkErrorCode)p->Recv_uint8();

	/* The client was never joined.. thank the client for the packet, but ignore it */
	if (this->status < STATUS_DONE_MAP || this->HasClientQuit()) {
		return this->CloseConnection(NETWORK_RECV_STATUS_CONN_LOST);
	}

	this->GetClientName(client_name, lastof(client_name));

	StringID strid = GetNetworkErrorMsg(errorno);
	GetString(str, strid, lastof(str));

	DEBUG(net, 2, "'%s' reported an error and is closing its connection (%s)", client_name, str);

	NetworkTextMessage(NETWORK_ACTION_LEAVE, CC_DEFAULT, false, client_name, nullptr, strid);

	for (NetworkClientSocket *new_cs : NetworkClientSocket::Iterate()) {
		if (new_cs->status > STATUS_AUTHORIZED) {
			new_cs->SendErrorQuit(this->client_id, errorno);
		}
	}

	NetworkAdminClientError(this->client_id, errorno);

	return this->CloseConnection(NETWORK_RECV_STATUS_CONN_LOST);
}

NetworkRecvStatus ServerNetworkGameSocketHandler::Receive_CLIENT_QUIT(Packet *p)
{
	/* The client wants to leave. Display this and report it to the other
	 *  clients. */
	char client_name[NETWORK_CLIENT_NAME_LENGTH];

	/* The client was never joined.. thank the client for the packet, but ignore it */
	if (this->status < STATUS_DONE_MAP || this->HasClientQuit()) {
		return this->CloseConnection(NETWORK_RECV_STATUS_CONN_LOST);
	}

	this->GetClientName(client_name, lastof(client_name));

	NetworkTextMessage(NETWORK_ACTION_LEAVE, CC_DEFAULT, false, client_name, nullptr, STR_NETWORK_MESSAGE_CLIENT_LEAVING);

	for (NetworkClientSocket *new_cs : NetworkClientSocket::Iterate()) {
		if (new_cs->status > STATUS_AUTHORIZED && new_cs != this) {
			new_cs->SendQuit(this->client_id);
		}
	}

	NetworkAdminClientQuit(this->client_id);

	return this->CloseConnection(NETWORK_RECV_STATUS_CONN_LOST);
}

NetworkRecvStatus ServerNetworkGameSocketHandler::Receive_CLIENT_ACK(Packet *p)
{
	if (this->status < STATUS_AUTHORIZED) {
		/* Illegal call, return error and ignore the packet */
		return this->SendError(NETWORK_ERROR_NOT_AUTHORIZED);
	}

	uint32 frame = p->Recv_uint32();

	/* The client is trying to catch up with the server */
	if (this->status == STATUS_PRE_ACTIVE) {
		/* The client is not yet caught up? */
		if (frame + DAY_TICKS < _frame_counter) return NETWORK_RECV_STATUS_OKAY;

		/* Now he is! Unpause the game */
		this->status = STATUS_ACTIVE;
		this->last_token_frame = _frame_counter;

		/* Execute script for, e.g. MOTD */
		IConsoleCmdExec("exec scripts/on_server_connect.scr 0");
	}

	/* Get, and validate the token. */
	uint8 token = p->Recv_uint8();
	if (token == this->last_token) {
		/* We differentiate between last_token_frame and last_frame so the lag
		 * test uses the actual lag of the client instead of the lag for getting
		 * the token back and forth; after all, the token is only sent every
		 * time we receive a PACKET_CLIENT_ACK, after which we will send a new
		 * token to the client. If the lag would be one day, then we would not
		 * be sending the new token soon enough for the new daily scheduled
		 * PACKET_CLIENT_ACK. This would then register the lag of the client as
		 * two days, even when it's only a single day. */
		this->last_token_frame = _frame_counter;
		/* Request a new token. */
		this->last_token = 0;
	}

	/* The client received the frame, make note of it */
	this->last_frame = frame;
	/* With those 2 values we can calculate the lag realtime */
	this->last_frame_server = _frame_counter;
	return NETWORK_RECV_STATUS_OKAY;
}


/**
 * Send an actual chat message.
 * @param action The action that's performed.
 * @param desttype The type of destination.
 * @param dest The actual destination index.
 * @param msg The actual message.
 * @param from_id The origin of the message.
 * @param data Arbitrary data.
 * @param from_admin Whether the origin is an admin or not.
 */
void NetworkServerSendChat(NetworkAction action, DestType desttype, int dest, const char *msg, ClientID from_id, int64 data, bool from_admin)
{
	const NetworkClientInfo *ci, *ci_own, *ci_to;

	switch (desttype) {
		case DESTTYPE_CLIENT:
			/* Are we sending to the server? */
			if ((ClientID)dest == CLIENT_ID_SERVER) {
				ci = NetworkClientInfo::GetByClientID(from_id);
				/* Display the text locally, and that is it */
				if (ci != nullptr) {
					NetworkTextMessage(action, GetDrawStringCompanyColour(ci->client_playas), false, ci->client_name, msg, data);

					if (_settings_client.network.server_admin_chat) {
						NetworkAdminChat(action, desttype, from_id, msg, data, from_admin);
					}
				}
			} else {
				/* Else find the client to send the message to */
				for (NetworkClientSocket *cs : NetworkClientSocket::Iterate()) {
					if (cs->client_id == (ClientID)dest) {
						cs->SendChat(action, from_id, false, msg, data);
						break;
					}
				}
			}

			/* Display the message locally (so you know you have sent it) */
			if (from_id != (ClientID)dest) {
				if (from_id == CLIENT_ID_SERVER) {
					ci = NetworkClientInfo::GetByClientID(from_id);
					ci_to = NetworkClientInfo::GetByClientID((ClientID)dest);
					if (ci != nullptr && ci_to != nullptr) {
						NetworkTextMessage(action, GetDrawStringCompanyColour(ci->client_playas), true, ci_to->client_name, msg, data);
					}
				} else {
					for (NetworkClientSocket *cs : NetworkClientSocket::Iterate()) {
						if (cs->client_id == from_id) {
							cs->SendChat(action, (ClientID)dest, true, msg, data);
							break;
						}
					}
				}
			}
			break;
		case DESTTYPE_TEAM: {
			/* If this is false, the message is already displayed on the client who sent it. */
			bool show_local = true;
			/* Find all clients that belong to this company */
			ci_to = nullptr;
			for (NetworkClientSocket *cs : NetworkClientSocket::Iterate()) {
				ci = cs->GetInfo();
				if (ci != nullptr && ci->client_playas == (CompanyID)dest) {
					cs->SendChat(action, from_id, false, msg, data);
					if (cs->client_id == from_id) show_local = false;
					ci_to = ci; // Remember a client that is in the company for company-name
				}
			}

			/* if the server can read it, let the admin network read it, too. */
			if (_local_company == (CompanyID)dest && _settings_client.network.server_admin_chat) {
				NetworkAdminChat(action, desttype, from_id, msg, data, from_admin);
			}

			ci = NetworkClientInfo::GetByClientID(from_id);
			ci_own = NetworkClientInfo::GetByClientID(CLIENT_ID_SERVER);
			if (ci != nullptr && ci_own != nullptr && ci_own->client_playas == dest) {
				NetworkTextMessage(action, GetDrawStringCompanyColour(ci->client_playas), false, ci->client_name, msg, data);
				if (from_id == CLIENT_ID_SERVER) show_local = false;
				ci_to = ci_own;
			}

			/* There is no such client */
			if (ci_to == nullptr) break;

			/* Display the message locally (so you know you have sent it) */
			if (ci != nullptr && show_local) {
				if (from_id == CLIENT_ID_SERVER) {
					char name[NETWORK_NAME_LENGTH];
					StringID str = Company::IsValidID(ci_to->client_playas) ? STR_COMPANY_NAME : STR_NETWORK_SPECTATORS;
					SetDParam(0, ci_to->client_playas);
					GetString(name, str, lastof(name));
					NetworkTextMessage(action, GetDrawStringCompanyColour(ci_own->client_playas), true, name, msg, data);
				} else {
					for (NetworkClientSocket *cs : NetworkClientSocket::Iterate()) {
						if (cs->client_id == from_id) {
							cs->SendChat(action, ci_to->client_id, true, msg, data);
						}
					}
				}
			}
			break;
		}
		default:
			DEBUG(net, 0, "[server] received unknown chat destination type %d. Doing broadcast instead", desttype);
			FALLTHROUGH;

		case DESTTYPE_BROADCAST:
			for (NetworkClientSocket *cs : NetworkClientSocket::Iterate()) {
				cs->SendChat(action, from_id, false, msg, data);
			}

			NetworkAdminChat(action, desttype, from_id, msg, data, from_admin);

			ci = NetworkClientInfo::GetByClientID(from_id);
			if (ci != nullptr) {
				NetworkTextMessage(action, GetDrawStringCompanyColour(ci->client_playas), false, ci->client_name, msg, data);
			}
			break;
	}
}

NetworkRecvStatus ServerNetworkGameSocketHandler::Receive_CLIENT_CHAT(Packet *p)
{
	if (this->status < STATUS_PRE_ACTIVE) {
		/* Illegal call, return error and ignore the packet */
		return this->SendError(NETWORK_ERROR_NOT_AUTHORIZED);
	}

	NetworkAction action = (NetworkAction)p->Recv_uint8();
	DestType desttype = (DestType)p->Recv_uint8();
	int dest = p->Recv_uint32();
	char msg[NETWORK_CHAT_LENGTH];

	p->Recv_string(msg, NETWORK_CHAT_LENGTH);
	int64 data = p->Recv_uint64();

	NetworkClientInfo *ci = this->GetInfo();
	switch (action) {
		case NETWORK_ACTION_GIVE_MONEY:
			if (!Company::IsValidID(ci->client_playas)) break;
			FALLTHROUGH;
		case NETWORK_ACTION_CHAT:
		case NETWORK_ACTION_CHAT_CLIENT:
		case NETWORK_ACTION_CHAT_COMPANY:
			NetworkServerSendChat(action, desttype, dest, msg, this->client_id, data);
			break;
		default:
			IConsolePrintF(CC_ERROR, "WARNING: invalid chat action from client %d (IP: %s).", ci->client_id, this->GetClientIP());
			return this->SendError(NETWORK_ERROR_NOT_EXPECTED);
	}
	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ServerNetworkGameSocketHandler::Receive_CLIENT_SET_PASSWORD(Packet *p)
{
	if (this->status != STATUS_ACTIVE) {
		/* Illegal call, return error and ignore the packet */
		return this->SendError(NETWORK_ERROR_NOT_EXPECTED);
	}

	char password[NETWORK_PASSWORD_LENGTH];
	const NetworkClientInfo *ci;

	p->Recv_string(password, sizeof(password));
	ci = this->GetInfo();

	NetworkServerSetCompanyPassword(ci->client_playas, password);
	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ServerNetworkGameSocketHandler::Receive_CLIENT_SET_NAME(Packet *p)
{
	if (this->status != STATUS_ACTIVE) {
		/* Illegal call, return error and ignore the packet */
		return this->SendError(NETWORK_ERROR_NOT_EXPECTED);
	}

	char client_name[NETWORK_CLIENT_NAME_LENGTH];
	NetworkClientInfo *ci;

	p->Recv_string(client_name, sizeof(client_name));
	ci = this->GetInfo();

	if (this->HasClientQuit()) return NETWORK_RECV_STATUS_CONN_LOST;

	if (ci != nullptr) {
		/* Display change */
		if (NetworkFindName(client_name, lastof(client_name))) {
			NetworkTextMessage(NETWORK_ACTION_NAME_CHANGE, CC_DEFAULT, false, ci->client_name, client_name);
			strecpy(ci->client_name, client_name, lastof(ci->client_name));
			NetworkUpdateClientInfo(ci->client_id);
		}
	}
	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ServerNetworkGameSocketHandler::Receive_CLIENT_RCON(Packet *p)
{
	if (this->status != STATUS_ACTIVE) return this->SendError(NETWORK_ERROR_NOT_EXPECTED);

	char pass[NETWORK_PASSWORD_LENGTH];
	char command[NETWORK_RCONCOMMAND_LENGTH];

	if (StrEmpty(_settings_client.network.rcon_password)) return NETWORK_RECV_STATUS_OKAY;

	p->Recv_string(pass, sizeof(pass));
	p->Recv_string(command, sizeof(command));

	if (strcmp(pass, _settings_client.network.rcon_password) != 0) {
		DEBUG(net, 0, "[rcon] wrong password from client-id %d", this->client_id);
		return NETWORK_RECV_STATUS_OKAY;
	}

	DEBUG(net, 0, "[rcon] client-id %d executed: '%s'", this->client_id, command);

	_redirect_console_to_client = this->client_id;
	IConsoleCmdExec(command);
	_redirect_console_to_client = INVALID_CLIENT_ID;
	return NETWORK_RECV_STATUS_OKAY;
}

NetworkRecvStatus ServerNetworkGameSocketHandler::Receive_CLIENT_MOVE(Packet *p)
{
	if (this->status != STATUS_ACTIVE) return this->SendError(NETWORK_ERROR_NOT_EXPECTED);

	CompanyID company_id = (Owner)p->Recv_uint8();

	/* Check if the company is valid, we don't allow moving to AI companies */
	if (company_id != COMPANY_SPECTATOR && !Company::IsValidHumanID(company_id)) return NETWORK_RECV_STATUS_OKAY;

	/* Check if we require a password for this company */
	if (company_id != COMPANY_SPECTATOR && !StrEmpty(_network_company_states[company_id].password)) {
		/* we need a password from the client - should be in this packet */
		char password[NETWORK_PASSWORD_LENGTH];
		p->Recv_string(password, sizeof(password));

		/* Incorrect password sent, return! */
		if (strcmp(password, _network_company_states[company_id].password) != 0) {
			DEBUG(net, 2, "[move] wrong password from client-id #%d for company #%d", this->client_id, company_id + 1);
			return NETWORK_RECV_STATUS_OKAY;
		}
	}

	/* if we get here we can move the client */
	NetworkServerDoMove(this->client_id, company_id);
	return NETWORK_RECV_STATUS_OKAY;
}

/**
 * Package some generic company information into a packet.
 * @param p       The packet that will contain the data.
 * @param c       The company to put the of into the packet.
 * @param stats   The statistics to put in the packet.
 * @param max_len The maximum length of the company name.
 */
void NetworkSocketHandler::SendCompanyInformation(Packet *p, const Company *c, const NetworkCompanyStats *stats, uint max_len)
{
	/* Grab the company name */
	char company_name[NETWORK_COMPANY_NAME_LENGTH];
	SetDParam(0, c->index);

	assert(max_len <= lengthof(company_name));
	GetString(company_name, STR_COMPANY_NAME, company_name + max_len - 1);

	/* Get the income */
	Money income = 0;
	if (_cur_year - 1 == c->inaugurated_year) {
		/* The company is here just 1 year, so display [2], else display[1] */
		for (uint i = 0; i < lengthof(c->yearly_expenses[2]); i++) {
			income -= c->yearly_expenses[2][i];
		}
	} else {
		for (uint i = 0; i < lengthof(c->yearly_expenses[1]); i++) {
			income -= c->yearly_expenses[1][i];
		}
	}

	/* Send the information */
	p->Send_uint8 (c->index);
	p->Send_string(company_name);
	p->Send_uint32(c->inaugurated_year);
	p->Send_uint64(static_cast<int64>(c->old_economy[0].company_value));
	p->Send_uint64(static_cast<int64>(c->money));
	p->Send_uint64(static_cast<int64>(income));
	p->Send_uint16(c->old_economy[0].performance_history);

	/* Send 1 if there is a password for the company else send 0 */
	p->Send_bool  (!StrEmpty(_network_company_states[c->index].password));

	for (uint i = 0; i < NETWORK_VEH_END; i++) {
		p->Send_uint16(stats->num_vehicle[i]);
	}

	for (uint i = 0; i < NETWORK_VEH_END; i++) {
		p->Send_uint16(stats->num_station[i]);
	}

	p->Send_bool(c->is_ai);
}

/**
 * Populate the company stats.
 * @param stats the stats to update
 */
void NetworkPopulateCompanyStats(NetworkCompanyStats *stats)
{
	memset(stats, 0, sizeof(*stats) * MAX_COMPANIES);

	/* Go through all vehicles and count the type of vehicles */
	for (const Vehicle *v : Vehicle::Iterate()) {
		if (!Company::IsValidID(v->owner) || !v->IsPrimaryVehicle()) continue;
		byte type = 0;
		switch (v->type) {
			case VEH_TRAIN: type = NETWORK_VEH_TRAIN; break;
			case VEH_ROAD: type = RoadVehicle::From(v)->IsBus() ? NETWORK_VEH_BUS : NETWORK_VEH_LORRY; break;
			case VEH_AIRCRAFT: type = NETWORK_VEH_PLANE; break;
			case VEH_SHIP: type = NETWORK_VEH_SHIP; break;
			default: continue;
		}
		stats[v->owner].num_vehicle[type]++;
	}

	/* Go through all stations and count the types of stations */
	for (const Station *s : Station::Iterate()) {
		if (Company::IsValidID(s->owner)) {
			NetworkCompanyStats *npi = &stats[s->owner];

			if (s->facilities & FACIL_TRAIN)      npi->num_station[NETWORK_VEH_TRAIN]++;
			if (s->facilities & FACIL_TRUCK_STOP) npi->num_station[NETWORK_VEH_LORRY]++;
			if (s->facilities & FACIL_BUS_STOP)   npi->num_station[NETWORK_VEH_BUS]++;
			if (s->facilities & FACIL_AIRPORT)    npi->num_station[NETWORK_VEH_PLANE]++;
			if (s->facilities & FACIL_DOCK)       npi->num_station[NETWORK_VEH_SHIP]++;
		}
	}
}

/**
 * Send updated client info of a particular client.
 * @param client_id The client to send it for.
 */
void NetworkUpdateClientInfo(ClientID client_id)
{
	NetworkClientInfo *ci = NetworkClientInfo::GetByClientID(client_id);

	if (ci == nullptr) return;

	DEBUG(desync, 1, "client: %08x; %02x; %02x; %04x", _date, _date_fract, (int)ci->client_playas, client_id);

	for (NetworkClientSocket *cs : NetworkClientSocket::Iterate()) {
		cs->SendClientInfo(ci);
	}

	NetworkAdminClientUpdate(ci);
}

/** Check if we want to restart the map */
static void NetworkCheckRestartMap()
{
	if (_settings_client.network.restart_game_year != 0 && _cur_year >= _settings_client.network.restart_game_year) {
		DEBUG(net, 0, "Auto-restarting map. Year %d reached", _cur_year);

		_settings_newgame.game_creation.generation_seed = GENERATE_NEW_SEED;
		switch(_file_to_saveload.abstract_ftype) {
			case FT_SAVEGAME:
			case FT_SCENARIO:
				_switch_mode = SM_LOAD_GAME;
				break;

			case FT_HEIGHTMAP:
				_switch_mode = SM_START_HEIGHTMAP;
				break;

			default:
				_switch_mode = SM_NEWGAME;
		}
	}
}

/** Check if the server has autoclean_companies activated
 * Two things happen:
 *     1) If a company is not protected, it is closed after 1 year (for example)
 *     2) If a company is protected, protection is disabled after 3 years (for example)
 *          (and item 1. happens a year later)
 */
static void NetworkAutoCleanCompanies()
{
	bool clients_in_company[MAX_COMPANIES];
	int vehicles_in_company[MAX_COMPANIES];

	if (!_settings_client.network.autoclean_companies) return;

	memset(clients_in_company, 0, sizeof(clients_in_company));

	/* Detect the active companies */
	for (const NetworkClientInfo *ci : NetworkClientInfo::Iterate()) {
		if (Company::IsValidID(ci->client_playas)) clients_in_company[ci->client_playas] = true;
	}

	if (!_network_dedicated) {
		const NetworkClientInfo *ci = NetworkClientInfo::GetByClientID(CLIENT_ID_SERVER);
		if (Company::IsValidID(ci->client_playas)) clients_in_company[ci->client_playas] = true;
	}

	if (_settings_client.network.autoclean_novehicles != 0) {
		memset(vehicles_in_company, 0, sizeof(vehicles_in_company));

		for (const Vehicle *v : Vehicle::Iterate()) {
			if (!Company::IsValidID(v->owner) || !v->IsPrimaryVehicle()) continue;
			vehicles_in_company[v->owner]++;
		}
	}

	/* Go through all the companies */
	for (const Company *c : Company::Iterate()) {
		/* Skip the non-active once */
		if (c->is_ai) continue;

		if (!clients_in_company[c->index]) {
			/* The company is empty for one month more */
			_network_company_states[c->index].months_empty++;

			/* Is the company empty for autoclean_unprotected-months, and is there no protection? */
			if (_settings_client.network.autoclean_unprotected != 0 && _network_company_states[c->index].months_empty > _settings_client.network.autoclean_unprotected && StrEmpty(_network_company_states[c->index].password)) {
				/* Shut the company down */
				DoCommandP(0, CCA_DELETE | c->index << 16 | CRR_AUTOCLEAN << 24, 0, CMD_COMPANY_CTRL);
				IConsolePrintF(CC_DEFAULT, "Auto-cleaned company #%d with no password", c->index + 1);
			}
			/* Is the company empty for autoclean_protected-months, and there is a protection? */
			if (_settings_client.network.autoclean_protected != 0 && _network_company_states[c->index].months_empty > _settings_client.network.autoclean_protected && !StrEmpty(_network_company_states[c->index].password)) {
				/* Unprotect the company */
				_network_company_states[c->index].password[0] = '\0';
				IConsolePrintF(CC_DEFAULT, "Auto-removed protection from company #%d", c->index + 1);
				_network_company_states[c->index].months_empty = 0;
				NetworkServerUpdateCompanyPassworded(c->index, false);
			}
			/* Is the company empty for autoclean_novehicles-months, and has no vehicles? */
			if (_settings_client.network.autoclean_novehicles != 0 && _network_company_states[c->index].months_empty > _settings_client.network.autoclean_novehicles && vehicles_in_company[c->index] == 0) {
				/* Shut the company down */
				DoCommandP(0, CCA_DELETE | c->index << 16 | CRR_AUTOCLEAN << 24, 0, CMD_COMPANY_CTRL);
				IConsolePrintF(CC_DEFAULT, "Auto-cleaned company #%d with no vehicles", c->index + 1);
			}
		} else {
			/* It is not empty, reset the date */
			_network_company_states[c->index].months_empty = 0;
		}
	}
}

/**
 * Check whether a name is unique, and otherwise try to make it unique.
 * @param new_name The name to check/modify.
 * @param last     The last writeable element of the buffer.
 * @return True if an unique name was achieved.
 */
bool NetworkFindName(char *new_name, const char *last)
{
	bool found_name = false;
	uint number = 0;
	char original_name[NETWORK_CLIENT_NAME_LENGTH];

	strecpy(original_name, new_name, lastof(original_name));

	while (!found_name) {
		found_name = true;
		for (const NetworkClientInfo *ci : NetworkClientInfo::Iterate()) {
			if (strcmp(ci->client_name, new_name) == 0) {
				/* Name already in use */
				found_name = false;
				break;
			}
		}
		/* Check if it is the same as the server-name */
		const NetworkClientInfo *ci = NetworkClientInfo::GetByClientID(CLIENT_ID_SERVER);
		if (ci != nullptr) {
			if (strcmp(ci->client_name, new_name) == 0) found_name = false; // name already in use
		}

		if (!found_name) {
			/* Try a new name (<name> #1, <name> #2, and so on) */

			/* Something's really wrong when there're more names than clients */
			if (number++ > MAX_CLIENTS) break;
			seprintf(new_name, last, "%s #%d", original_name, number);
		}
	}

	return found_name;
}

/**
 * Change the client name of the given client
 * @param client_id the client to change the name of
 * @param new_name the new name for the client
 * @return true iff the name was changed
 */
bool NetworkServerChangeClientName(ClientID client_id, const char *new_name)
{
	/* Check if the name's already in use */
	for (NetworkClientInfo *ci : NetworkClientInfo::Iterate()) {
		if (strcmp(ci->client_name, new_name) == 0) return false;
	}

	NetworkClientInfo *ci = NetworkClientInfo::GetByClientID(client_id);
	if (ci == nullptr) return false;

	NetworkTextMessage(NETWORK_ACTION_NAME_CHANGE, CC_DEFAULT, true, ci->client_name, new_name);

	strecpy(ci->client_name, new_name, lastof(ci->client_name));

	NetworkUpdateClientInfo(client_id);
	return true;
}

/**
 * Set/Reset a company password on the server end.
 * @param company_id ID of the company the password should be changed for.
 * @param password The new password.
 * @param already_hashed Is the given password already hashed?
 */
void NetworkServerSetCompanyPassword(CompanyID company_id, const char *password, bool already_hashed)
{
	if (!Company::IsValidHumanID(company_id)) return;

	if (!already_hashed) {
		password = GenerateCompanyPasswordHash(password, _settings_client.network.network_id, _settings_game.game_creation.generation_seed);
	}

	strecpy(_network_company_states[company_id].password, password, lastof(_network_company_states[company_id].password));
	NetworkServerUpdateCompanyPassworded(company_id, !StrEmpty(_network_company_states[company_id].password));
}

/**
 * Handle the command-queue of a socket.
 * @param cs The socket to handle the queue for.
 */
static void NetworkHandleCommandQueue(NetworkClientSocket *cs)
{
	CommandPacket *cp;
	while ((cp = cs->outgoing_queue.Pop()) != nullptr) {
		cs->SendCommand(cp);
		free(cp);
	}
}

/**
 * This is called every tick if this is a _network_server
 * @param send_frame Whether to send the frame to the clients.
 */
void NetworkServer_Tick(bool send_frame)
{
#ifndef ENABLE_NETWORK_SYNC_EVERY_FRAME
	bool send_sync = false;
#endif

#ifndef ENABLE_NETWORK_SYNC_EVERY_FRAME
	if (_frame_counter >= _last_sync_frame + _settings_client.network.sync_freq) {
		_last_sync_frame = _frame_counter;
		send_sync = true;
	}
#endif

	/* Now we are done with the frame, inform the clients that they can
	 *  do their frame! */
	for (NetworkClientSocket *cs : NetworkClientSocket::Iterate()) {
		/* We allow a number of bytes per frame, but only to the burst amount
		 * to be available for packet receiving at any particular time. */
		cs->receive_limit = min(cs->receive_limit + _settings_client.network.bytes_per_frame,
				_settings_client.network.bytes_per_frame_burst);

		/* Check if the speed of the client is what we can expect from a client */
		uint lag = NetworkCalculateLag(cs);
		switch (cs->status) {
			case NetworkClientSocket::STATUS_ACTIVE:
				if (lag > _settings_client.network.max_lag_time) {
					/* Client did still not report in within the specified limit. */
					IConsolePrintF(CC_ERROR, cs->last_packet + lag * MILLISECONDS_PER_TICK > _realtime_tick ?
							/* A packet was received in the last three game days, so the client is likely lagging behind. */
								"Client #%d is dropped because the client's game state is more than %d ticks behind" :
							/* No packet was received in the last three game days; sounds like a lost connection. */
								"Client #%d is dropped because the client did not respond for more than %d ticks",
							cs->client_id, lag);
					cs->SendError(NETWORK_ERROR_TIMEOUT_COMPUTER);
					continue;
				}

				/* Report once per time we detect the lag, and only when we
				 * received a packet in the last 2000 milliseconds. If we
				 * did not receive a packet, then the client is not just
				 * slow, but the connection is likely severed. Mentioning
				 * frame_freq is not useful in this case. */
				if (lag > (uint)DAY_TICKS && cs->lag_test == 0 && cs->last_packet + 2000 > _realtime_tick) {
					IConsolePrintF(CC_WARNING, "[%d] Client #%d is slow, try increasing [network.]frame_freq to a higher value!", _frame_counter, cs->client_id);
					cs->lag_test = 1;
				}

				if (cs->last_frame_server - cs->last_token_frame >= _settings_client.network.max_lag_time) {
					/* This is a bad client! It didn't send the right token back within time. */
					IConsolePrintF(CC_ERROR, "Client #%d is dropped because it fails to send valid acks", cs->client_id);
					cs->SendError(NETWORK_ERROR_TIMEOUT_COMPUTER);
					continue;
				}
				break;

			case NetworkClientSocket::STATUS_INACTIVE:
			case NetworkClientSocket::STATUS_NEWGRFS_CHECK:
			case NetworkClientSocket::STATUS_AUTHORIZED:
				/* NewGRF check and authorized states should be handled almost instantly.
				 * So give them some lee-way, likewise for the query with inactive. */
				if (lag > _settings_client.network.max_init_time) {
					IConsolePrintF(CC_ERROR, "Client #%d is dropped because it took longer than %d ticks to start the joining process", cs->client_id, _settings_client.network.max_init_time);
					cs->SendError(NETWORK_ERROR_TIMEOUT_COMPUTER);
					continue;
				}
				break;

			case NetworkClientSocket::STATUS_MAP:
				/* Downloading the map... this is the amount of time since starting the saving. */
				if (lag > _settings_client.network.max_download_time) {
					IConsolePrintF(CC_ERROR, "Client #%d is dropped because it took longer than %d ticks to download the map", cs->client_id, _settings_client.network.max_download_time);
					cs->SendError(NETWORK_ERROR_TIMEOUT_MAP);
					continue;
				}
				break;

			case NetworkClientSocket::STATUS_DONE_MAP:
			case NetworkClientSocket::STATUS_PRE_ACTIVE:
				/* The map has been sent, so this is for loading the map and syncing up. */
				if (lag > _settings_client.network.max_join_time) {
					IConsolePrintF(CC_ERROR, "Client #%d is dropped because it took longer than %d ticks to join", cs->client_id, _settings_client.network.max_join_time);
					cs->SendError(NETWORK_ERROR_TIMEOUT_JOIN);
					continue;
				}
				break;

			case NetworkClientSocket::STATUS_AUTH_GAME:
			case NetworkClientSocket::STATUS_AUTH_COMPANY:
				/* These don't block? */
				if (lag > _settings_client.network.max_password_time) {
					IConsolePrintF(CC_ERROR, "Client #%d is dropped because it took longer than %d ticks to enter the password", cs->client_id, _settings_client.network.max_password_time);
					cs->SendError(NETWORK_ERROR_TIMEOUT_PASSWORD);
					continue;
				}
				break;

			case NetworkClientSocket::STATUS_MAP_WAIT:
				/* This is an internal state where we do not wait
				 * on the client to move to a different state. */
				break;

			case NetworkClientSocket::STATUS_END:
				/* Bad server/code. */
				NOT_REACHED();
		}

		if (cs->status >= NetworkClientSocket::STATUS_PRE_ACTIVE) {
			/* Check if we can send command, and if we have anything in the queue */
			NetworkHandleCommandQueue(cs);

			/* Send an updated _frame_counter_max to the client */
			if (send_frame) cs->SendFrame();

#ifndef ENABLE_NETWORK_SYNC_EVERY_FRAME
			/* Send a sync-check packet */
			if (send_sync) cs->SendSync();
#endif
		}
	}

	/* See if we need to advertise */
	NetworkUDPAdvertise();
}

/** Yearly "callback". Called whenever the year changes. */
void NetworkServerYearlyLoop()
{
	NetworkCheckRestartMap();
	NetworkAdminUpdate(ADMIN_FREQUENCY_ANUALLY);
}

/** Monthly "callback". Called whenever the month changes. */
void NetworkServerMonthlyLoop()
{
	NetworkAutoCleanCompanies();
	NetworkAdminUpdate(ADMIN_FREQUENCY_MONTHLY);
	if ((_cur_month % 3) == 0) NetworkAdminUpdate(ADMIN_FREQUENCY_QUARTERLY);
}

/** Daily "callback". Called whenever the date changes. */
void NetworkServerDailyLoop()
{
	NetworkAdminUpdate(ADMIN_FREQUENCY_DAILY);
	if ((_date % 7) == 3) NetworkAdminUpdate(ADMIN_FREQUENCY_WEEKLY);
}

/**
 * Get the IP address/hostname of the connected client.
 * @return The IP address.
 */
const char *ServerNetworkGameSocketHandler::GetClientIP()
{
	return this->client_address.GetHostname();
}

/** Show the status message of all clients on the console. */
void NetworkServerShowStatusToConsole()
{
	static const char * const stat_str[] = {
		"inactive",
		"checking NewGRFs",
		"authorizing (server password)",
		"authorizing (company password)",
		"authorized",
		"waiting",
		"loading map",
		"map done",
		"ready",
		"active"
	};
	assert_compile(lengthof(stat_str) == NetworkClientSocket::STATUS_END);

	for (NetworkClientSocket *cs : NetworkClientSocket::Iterate()) {
		NetworkClientInfo *ci = cs->GetInfo();
		if (ci == nullptr) continue;
		uint lag = NetworkCalculateLag(cs);
		const char *status;

		status = (cs->status < (ptrdiff_t)lengthof(stat_str) ? stat_str[cs->status] : "unknown");
		IConsolePrintF(CC_INFO, "Client #%1d  name: '%s'  status: '%s'  frame-lag: %3d  company: %1d  IP: %s",
			cs->client_id, ci->client_name, status, lag,
			ci->client_playas + (Company::IsValidID(ci->client_playas) ? 1 : 0),
			cs->GetClientIP());
	}
}

/**
 * Send Config Update
 */
void NetworkServerSendConfigUpdate()
{
	for (NetworkClientSocket *cs : NetworkClientSocket::Iterate()) {
		if (cs->status >= NetworkClientSocket::STATUS_PRE_ACTIVE) cs->SendConfigUpdate();
	}
}

/**
 * Tell that a particular company is (not) passworded.
 * @param company_id The company that got/removed the password.
 * @param passworded Whether the password was received or removed.
 */
void NetworkServerUpdateCompanyPassworded(CompanyID company_id, bool passworded)
{
	if (NetworkCompanyIsPassworded(company_id) == passworded) return;

	SB(_network_company_passworded, company_id, 1, !!passworded);
	SetWindowClassesDirty(WC_COMPANY);

	for (NetworkClientSocket *cs : NetworkClientSocket::Iterate()) {
		if (cs->status >= NetworkClientSocket::STATUS_PRE_ACTIVE) cs->SendCompanyUpdate();
	}

	NetworkAdminCompanyUpdate(Company::GetIfValid(company_id));
}

/**
 * Handle the tid-bits of moving a client from one company to another.
 * @param client_id id of the client we want to move.
 * @param company_id id of the company we want to move the client to.
 * @return void
 */
void NetworkServerDoMove(ClientID client_id, CompanyID company_id)
{
	/* Only allow non-dedicated servers and normal clients to be moved */
	if (client_id == CLIENT_ID_SERVER && _network_dedicated) return;

	NetworkClientInfo *ci = NetworkClientInfo::GetByClientID(client_id);

	/* No need to waste network resources if the client is in the company already! */
	if (ci->client_playas == company_id) return;

	ci->client_playas = company_id;

	if (client_id == CLIENT_ID_SERVER) {
		SetLocalCompany(company_id);
	} else {
		NetworkClientSocket *cs = NetworkClientSocket::GetByClientID(client_id);
		/* When the company isn't authorized we can't move them yet. */
		if (cs->status < NetworkClientSocket::STATUS_AUTHORIZED) return;
		cs->SendMove(client_id, company_id);
	}

	/* announce the client's move */
	NetworkUpdateClientInfo(client_id);

	NetworkAction action = (company_id == COMPANY_SPECTATOR) ? NETWORK_ACTION_COMPANY_SPECTATOR : NETWORK_ACTION_COMPANY_JOIN;
	NetworkServerSendChat(action, DESTTYPE_BROADCAST, 0, "", client_id, company_id + 1);
}

/**
 * Send an rcon reply to the client.
 * @param client_id The identifier of the client.
 * @param colour_code The colour of the text.
 * @param string The actual reply.
 */
void NetworkServerSendRcon(ClientID client_id, TextColour colour_code, const char *string)
{
	NetworkClientSocket::GetByClientID(client_id)->SendRConResult(colour_code, string);
}

/**
 * Kick a single client.
 * @param client_id The client to kick.
 * @param reason In case of kicking a client, specifies the reason for kicking the client.
 */
void NetworkServerKickClient(ClientID client_id, const char *reason)
{
	if (client_id == CLIENT_ID_SERVER) return;
	NetworkClientSocket::GetByClientID(client_id)->SendError(NETWORK_ERROR_KICKED, reason);
}

/**
 * Ban, or kick, everyone joined from the given client's IP.
 * @param client_id The client to check for.
 * @param ban Whether to ban or kick.
 * @param reason In case of kicking a client, specifies the reason for kicking the client.
 */
uint NetworkServerKickOrBanIP(ClientID client_id, bool ban, const char *reason)
{
	return NetworkServerKickOrBanIP(NetworkClientSocket::GetByClientID(client_id)->GetClientIP(), ban, reason);
}

/**
 * Kick or ban someone based on an IP address.
 * @param ip The IP address/range to ban/kick.
 * @param ban Whether to ban or just kick.
 * @param reason In case of kicking a client, specifies the reason for kicking the client.
 */
uint NetworkServerKickOrBanIP(const char *ip, bool ban, const char *reason)
{
	/* Add address to ban-list */
	if (ban) {
		bool contains = false;
		for (const auto &iter : _network_ban_list) {
			if (iter == ip) {
				contains = true;
				break;
			}
		}
		if (!contains) _network_ban_list.emplace_back(ip);
	}

	uint n = 0;

	/* There can be multiple clients with the same IP, kick them all but don't kill the server,
	 * or the client doing the rcon. The latter can't be kicked because kicking frees closes
	 * and subsequently free the connection related instances, which we would be reading from
	 * and writing to after returning. So we would read or write data from freed memory up till
	 * the segfault triggers. */
	for (NetworkClientSocket *cs : NetworkClientSocket::Iterate()) {
		if (cs->client_id == CLIENT_ID_SERVER) continue;
		if (cs->client_id == _redirect_console_to_client) continue;
		if (cs->client_address.IsInNetmask(ip)) {
			NetworkServerKickClient(cs->client_id, reason);
			n++;
		}
	}

	return n;
}

/**
 * Check whether a particular company has clients.
 * @param company The company to check.
 * @return True if at least one client is joined to the company.
 */
bool NetworkCompanyHasClients(CompanyID company)
{
	for (const NetworkClientInfo *ci : NetworkClientInfo::Iterate()) {
		if (ci->client_playas == company) return true;
	}
	return false;
}


/**
 * Get the name of the client, if the user did not send it yet, Client ID is used.
 * @param client_name The variable to write the name to.
 * @param last        The pointer to the last element of the destination buffer
 */
void ServerNetworkGameSocketHandler::GetClientName(char *client_name, const char *last) const
{
	const NetworkClientInfo *ci = this->GetInfo();

	if (ci == nullptr || StrEmpty(ci->client_name)) {
		seprintf(client_name, last, "Client #%4d", this->client_id);
	} else {
		strecpy(client_name, ci->client_name, last);
	}
}

/**
 * Print all the clients to the console
 */
void NetworkPrintClients()
{
	for (NetworkClientInfo *ci : NetworkClientInfo::Iterate()) {
		if (_network_server) {
			IConsolePrintF(CC_INFO, "Client #%1d  name: '%s'  company: %1d  IP: %s",
					ci->client_id,
					ci->client_name,
					ci->client_playas + (Company::IsValidID(ci->client_playas) ? 1 : 0),
					ci->client_id == CLIENT_ID_SERVER ? "server" : NetworkClientSocket::GetByClientID(ci->client_id)->GetClientIP());
		} else {
			IConsolePrintF(CC_INFO, "Client #%1d  name: '%s'  company: %1d",
					ci->client_id,
					ci->client_name,
					ci->client_playas + (Company::IsValidID(ci->client_playas) ? 1 : 0));
		}
	}
}

/**
 * Perform all the server specific administration of a new company.
 * @param c  The newly created company; can't be nullptr.
 * @param ci The client information of the client that made the company; can be nullptr.
 */
void NetworkServerNewCompany(const Company *c, NetworkClientInfo *ci)
{
	assert(c != nullptr);

	if (!_network_server) return;

	_network_company_states[c->index].months_empty = 0;
	_network_company_states[c->index].password[0] = '\0';
	NetworkServerUpdateCompanyPassworded(c->index, false);

	if (ci != nullptr) {
		/* ci is nullptr when replaying, or for AIs. In neither case there is a client. */
		ci->client_playas = c->index;
		NetworkUpdateClientInfo(ci->client_id);
		NetworkSendCommand(0, 0, 0, CMD_RENAME_PRESIDENT, nullptr, ci->client_name, c->index);
	}

	/* Announce new company on network. */
	NetworkAdminCompanyInfo(c, true);

	if (ci != nullptr) {
		/* ci is nullptr when replaying, or for AIs. In neither case there is a client.
		   We need to send Admin port update here so that they first know about the new company
		   and then learn about a possibly joining client (see FS#6025) */
		NetworkServerSendChat(NETWORK_ACTION_COMPANY_NEW, DESTTYPE_BROADCAST, 0, "", ci->client_id, c->index + 1);
	}
}
