/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file network_stun.cpp STUN sending/receiving part of the network protocol. */

#include "../stdafx.h"
#include "../debug.h"
#include "network.h"
#include "network_coordinator.h"
#include "network_stun.h"

#include "../safeguards.h"

/** Connect to the STUN server. */
class NetworkStunConnecter : public TCPConnecter {
private:
	ClientNetworkStunSocketHandler *stun_handler;
	std::string token;
	uint8 family;

public:
	/**
	 * Initiate the connecting.
	 * @param stun_handler The handler for this request.
	 * @param connection_string The address of the server.
	 */
	NetworkStunConnecter(ClientNetworkStunSocketHandler *stun_handler, const std::string &connection_string, const std::string &token, uint8 family) :
		TCPConnecter(connection_string, NETWORK_STUN_SERVER_PORT, NetworkAddress(), family),
		stun_handler(stun_handler),
		token(token),
		family(family)
	{
	}

	void OnFailure() override
	{
		this->stun_handler->connecter = nullptr;

		/* Connection to STUN server failed. For example, the client doesn't
		 * support IPv6, which means it will fail that attempt. */

		_network_coordinator_client.StunResult(this->token, this->family, false);
	}

	void OnConnect(SOCKET s) override
	{
		this->stun_handler->connecter = nullptr;

		assert(this->stun_handler->sock == INVALID_SOCKET);
		this->stun_handler->sock = s;

		/* Store the local address; later connects will reuse it again.
		 * This is what makes STUN work for most NATs. */
		this->stun_handler->local_addr = NetworkAddress::GetSockAddress(s);

		/* We leave the connection open till the real connection is setup later. */
	}
};

/**
 * Connect to the STUN server over either IPv4 or IPv6.
 * @param token The token as received from the Game Coordinator.
 * @param family What IP family to use.
 */
void ClientNetworkStunSocketHandler::Connect(const std::string &token, uint8 family)
{
	this->token = token;
	this->family = family;

	this->connecter = new NetworkStunConnecter(this, NETWORK_STUN_SERVER_HOST, token, family);
}

/**
 * Send a STUN packet to the STUN server.
 * @param token The token as received from the Game Coordinator.
 * @param family What IP family this STUN request is for.
 * @return The handler for this STUN request.
 */
std::unique_ptr<ClientNetworkStunSocketHandler> ClientNetworkStunSocketHandler::Stun(const std::string &token, uint8 family)
{
	auto stun_handler = std::make_unique<ClientNetworkStunSocketHandler>();

	stun_handler->Connect(token, family);

	Packet *p = new Packet(PACKET_STUN_CLIENT_STUN);
	p->Send_uint8(NETWORK_COORDINATOR_VERSION);
	p->Send_string(token);
	p->Send_uint8(family);

	stun_handler->SendPacket(p);

	return stun_handler;
}

NetworkRecvStatus ClientNetworkStunSocketHandler::CloseConnection(bool error)
{
	NetworkStunSocketHandler::CloseConnection(error);

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
 * Check whether we received/can send some data from/to the STUN server and
 * when that's the case handle it appropriately.
 */
void ClientNetworkStunSocketHandler::SendReceive()
{
	if (this->sock == INVALID_SOCKET) return;

	/* We never attempt to receive anything on a STUN socket. After
	 * connecting a STUN connection, the local address will be reused to
	 * to establish the connection with the real server. If we would be to
	 * read this socket, some OSes get confused and deliver us packets meant
	 * for the real connection. It appears most OSes play best when we simply
	 * never attempt to read it to start with (and the packets will remain
	 * available on the other socket).
	 * Protocol-wise, the STUN server will never send any packet back anyway. */

	this->CanSendReceive();
	if (this->SendPackets() == SPS_ALL_SENT && !this->sent_result) {
		/* We delay giving the GC the result this long, as to make sure we
		 * have sent the STUN packet first. This means the GC is more likely
		 * to have the result ready by the time our StunResult() packet
		 * arrives. */
		this->sent_result = true;
		_network_coordinator_client.StunResult(this->token, this->family, true);
	}
}
