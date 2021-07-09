/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file network_turn.cpp TURN sending/receiving part of the network protocol. */

#include "../stdafx.h"
#include "../debug.h"
#include "network_coordinator.h"
#include "network_turn.h"

#include "../safeguards.h"

/** Connect to the TURN server. */
class NetworkTurnConnecter : public TCPConnecter {
private:
	ClientNetworkTurnSocketHandler *handler;

public:
	/**
	 * Initiate the connecting.
	 * @param connection_string The address of the TURN server.
	 */
	NetworkTurnConnecter(ClientNetworkTurnSocketHandler *handler, const std::string &hostname, uint16 port) : TCPConnecter(hostname, port), handler(handler) {}

	void OnFailure() override
	{
		this->handler->connecter = nullptr;

		this->handler->ConnectFailure();
	}

	void OnConnect(SOCKET s) override
	{
		this->handler->connecter = nullptr;

		handler->sock = s;
	}
};

bool ClientNetworkTurnSocketHandler::Receive_SERVER_ERROR(Packet *p)
{
	this->ConnectFailure();

	return false;
}

bool ClientNetworkTurnSocketHandler::Receive_SERVER_CONNECTED(Packet *p)
{
	std::string hostname = p->Recv_string(NETWORK_HOSTNAME_LENGTH);

	/* Act like we no longer have a socket, as we are handing it over to the
	 * game handler. */
	SOCKET game_sock = this->sock;
	this->sock = INVALID_SOCKET;

	NetworkAddress address = NetworkAddress(hostname, NETWORK_DEFAULT_PORT);
	_network_coordinator_client.ConnectSuccess(this->token, game_sock, address);

	return false;
}

/**
 * Connect to the TURN server.
 * @param host Hostname of the TURN server.
 * @param port Port of the TURN server.
 */
void ClientNetworkTurnSocketHandler::Connect(const std::string &hostname, uint16 port)
{
	this->connecter = new NetworkTurnConnecter(this, hostname, port);
}

/**
 * Send a TURN connect packet to the TURN server.
 * @param token The token as received from the Game Coordinator.
 * @param host Hostname of the TURN server.
 * @param port Port of the TURN server.
 * @return The handler for this TURN connection.
 */
/* static */ std::unique_ptr<ClientNetworkTurnSocketHandler> ClientNetworkTurnSocketHandler::Turn(const std::string &token, uint8 tracking_number, const std::string &hostname, uint16 port)
{
	auto turn_handler = std::make_unique<ClientNetworkTurnSocketHandler>(token, tracking_number);

	turn_handler->Connect(hostname, port);

	Packet *p = new Packet(PACKET_TURN_CLIENT_CONNECT);
	p->Send_uint8(NETWORK_COORDINATOR_VERSION);
	p->Send_string(token);

	turn_handler->SendPacket(p);

	return turn_handler;
}

void ClientNetworkTurnSocketHandler::ConnectFailure()
{
	_network_coordinator_client.ConnectFailure(this->token, this->tracking_number);
}

NetworkRecvStatus ClientNetworkTurnSocketHandler::CloseConnection(bool error)
{
	NetworkTurnSocketHandler::CloseConnection(error);

	/* If our connecter is still pending, shut it down too. Otherwise the
	 * callback of the connecter can call into us, and our object is most
	 * likely about to be destroyed. */
	if (this->connecter != nullptr) {
		this->connecter->Kill();
		this->connecter = nullptr;
	}

	return NETWORK_RECV_STATUS_OKAY;
}

/**
 * Check whether we received/can send some data from/to the TURN server and
 * when that's the case handle it appropriately
 */
void ClientNetworkTurnSocketHandler::SendReceive()
{
	if (this->sock == INVALID_SOCKET) return;

	if (this->CanSendReceive()) {
		this->ReceivePackets();
	}

	this->SendPackets();
}
