/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file network_udp.cpp This file handles the UDP related communication.
 *
 * This is the GameServer <-> GameClient
 * communication before the game is being joined.
 */

#include "../stdafx.h"
#include "../date_func.h"
#include "../map_func.h"
#include "../debug.h"
#include "core/game_info.h"
#include "network_gamelist.h"
#include "network_internal.h"
#include "network_udp.h"
#include "network.h"
#include "../core/endian_func.hpp"
#include "../company_base.h"
#include "../rev.h"
#include "../newgrf_text.h"
#include "../strings_func.h"
#include "table/strings.h"

#include "core/udp.h"

#include "../safeguards.h"

static bool _network_udp_server;         ///< Is the UDP server started?
static uint16 _network_udp_broadcast;    ///< Timeout for the UDP broadcasts.

/** Some information about a socket, which exists before the actual socket has been created to provide locking and the likes. */
struct UDPSocket {
	const std::string name;                     ///< The name of the socket.
	NetworkUDPSocketHandler *socket;            ///< The actual socket, which may be nullptr when not initialized yet.

	UDPSocket(const std::string &name) : name(name), socket(nullptr) {}

	void CloseSocket()
	{
		this->socket->CloseSocket();
		delete this->socket;
		this->socket = nullptr;
	}

	void ReceivePackets()
	{
		this->socket->ReceivePackets();
	}
};

static UDPSocket _udp_client("Client"); ///< udp client socket
static UDPSocket _udp_server("Server"); ///< udp server socket

///*** Communication with clients (we are server) ***/

/** Helper class for handling all server side communication. */
class ServerNetworkUDPSocketHandler : public NetworkUDPSocketHandler {
protected:
	void Receive_CLIENT_FIND_SERVER(Packet *p, NetworkAddress *client_addr) override;
public:
	/**
	 * Create the socket.
	 * @param addresses The addresses to bind on.
	 */
	ServerNetworkUDPSocketHandler(NetworkAddressList *addresses) : NetworkUDPSocketHandler(addresses) {}
	virtual ~ServerNetworkUDPSocketHandler() {}
};

void ServerNetworkUDPSocketHandler::Receive_CLIENT_FIND_SERVER(Packet *p, NetworkAddress *client_addr)
{
	Packet packet(PACKET_UDP_SERVER_RESPONSE);
	this->SendPacket(&packet, client_addr);

	Debug(net, 7, "Queried from {}", client_addr->GetHostname());
}

///*** Communication with servers (we are client) ***/

/** Helper class for handling all client side communication. */
class ClientNetworkUDPSocketHandler : public NetworkUDPSocketHandler {
protected:
	void Receive_SERVER_RESPONSE(Packet *p, NetworkAddress *client_addr) override;
public:
	virtual ~ClientNetworkUDPSocketHandler() {}
};

void ClientNetworkUDPSocketHandler::Receive_SERVER_RESPONSE(Packet *p, NetworkAddress *client_addr)
{
	Debug(net, 3, "Server response from {}", client_addr->GetAddressAsString());

	NetworkAddServer(client_addr->GetAddressAsString(false), false, true);
}

/** Broadcast to all ips */
static void NetworkUDPBroadCast(NetworkUDPSocketHandler *socket)
{
	for (NetworkAddress &addr : _broadcast_list) {
		Debug(net, 5, "Broadcasting to {}", addr.GetHostname());

		Packet p(PACKET_UDP_CLIENT_FIND_SERVER);
		socket->SendPacket(&p, &addr, true, true);
	}
}

/** Find all servers */
void NetworkUDPSearchGame()
{
	/* We are still searching.. */
	if (_network_udp_broadcast > 0) return;

	Debug(net, 3, "Searching server");

	NetworkUDPBroadCast(_udp_client.socket);
	_network_udp_broadcast = 300; // Stay searching for 300 ticks
}

/** Initialize the whole UDP bit. */
void NetworkUDPInitialize()
{
	/* If not closed, then do it. */
	if (_udp_server.socket != nullptr) NetworkUDPClose();

	Debug(net, 3, "Initializing UDP listeners");
	assert(_udp_client.socket == nullptr && _udp_server.socket == nullptr);

	_udp_client.socket = new ClientNetworkUDPSocketHandler();

	NetworkAddressList server;
	GetBindAddresses(&server, _settings_client.network.server_port);
	_udp_server.socket = new ServerNetworkUDPSocketHandler(&server);

	_network_udp_server = false;
	_network_udp_broadcast = 0;
}

/** Start the listening of the UDP server component. */
void NetworkUDPServerListen()
{
	_network_udp_server = _udp_server.socket->Listen();
}

/** Close all UDP related stuff. */
void NetworkUDPClose()
{
	_udp_client.CloseSocket();
	_udp_server.CloseSocket();

	_network_udp_server = false;
	_network_udp_broadcast = 0;
	Debug(net, 5, "Closed UDP listeners");
}

/** Receive the UDP packets. */
void NetworkBackgroundUDPLoop()
{
	if (_network_udp_server) {
		_udp_server.ReceivePackets();
	} else {
		_udp_client.ReceivePackets();
		if (_network_udp_broadcast > 0) _network_udp_broadcast--;
	}
}
