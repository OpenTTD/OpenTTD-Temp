/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file tcp_turn.h Basic functions to receive and send TCP packets to/from the TURN server.
 */

#ifndef NETWORK_CORE_TCP_TURN_H
#define NETWORK_CORE_TCP_TURN_H

#include "os_abstraction.h"
#include "tcp.h"
#include "packet.h"
#include "game_info.h"

/** Enum with all types of TCP TURN packets. The order MUST not be changed. **/
enum PacketTurnType {
	PACKET_TURN_SERVER_ERROR,     ///< TURN server is unable to relay.
	PACKET_TURN_CLIENT_CONNECT,   ///< Client is connecting to the TURN server.
	PACKET_TURN_SERVER_CONNECTED, ///< TURN server indicates the socket is now being relayed.
	PACKET_TURN_SERVER_END,       ///< Must ALWAYS be on the end of this list!! (period)
};

/** Base socket handler for all TURN TCP sockets. */
class NetworkTurnSocketHandler : public NetworkTCPSocketHandler {
protected:
	bool ReceiveInvalidPacket(PacketTurnType type);

	/**
	 * TURN server was unable to connect the client based on the ticket.
	 * Most likely cause is an invalid ticket or the other side that hasn't
	 * connected in a reasonable amount of time.
	 *
	 * @param p The packet that was just received.
	 * @return True upon success, otherwise false.
	 */
	virtual bool Receive_SERVER_ERROR(Packet *p);

	/**
	 * Client wants to connect to the TURN server (on request by the
	 * Game Coordinator).
	 *
	 *  uint8   Game Coordinator protocol version.
	 *  string  Token to track the current TURN request.
	 *
	 * @param p The packet that was just received.
	 * @return True upon success, otherwise false.
	 */
	virtual bool Receive_CLIENT_CONNECT(Packet *p);

	/**
	 * TURN server has connected two clients together and will now relay all
	 * packets to each other. No further TURN packets should be send over this
	 * socket, and the socket should be handed over to the game protocol.
	 *
	 *  string  Host of the peer.
	 *
	 * @param p The packet that was just received.
	 * @return True upon success, otherwise false.
	 */
	virtual bool Receive_SERVER_CONNECTED(Packet *p);

	bool HandlePacket(Packet *p);
public:
	/**
	 * Create a new cs socket handler for a given cs.
	 * @param s  the socket we are connected with.
	 * @param address IP etc. of the client.
	 */
	NetworkTurnSocketHandler(SOCKET s = INVALID_SOCKET) : NetworkTCPSocketHandler(s) {}

	bool ReceivePackets();
};

#endif /* NETWORK_CORE_TCP_TURN_H */
