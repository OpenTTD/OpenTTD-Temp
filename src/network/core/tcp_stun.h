/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file tcp_stun.h Basic functions to receive and send TCP packets to/from the STUN server.
 */

#ifndef NETWORK_CORE_TCP_STUN_H
#define NETWORK_CORE_TCP_STUN_H

#include "os_abstraction.h"
#include "tcp.h"
#include "packet.h"

/** Enum with all types of TCP STUN packets. The order MUST not be changed. **/
enum PacketStunType {
	PACKET_STUN_CLIENT_STUN,  ///< Send a STUN request to the STUN server.
	PACKET_STUN_END,          ///< Must ALWAYS be on the end of this list!! (period)
};

/** Base socket handler for all STUN TCP sockets. */
class NetworkStunSocketHandler : public NetworkTCPSocketHandler {
protected:
	bool ReceiveInvalidPacket(PacketStunType type);

	/**
	 * Send a STUN request to the STUN server letting the Game Coordinator know
	 * what our actually public IP:port is.
	 *
	 *  uint8   Game Coordinator protocol version.
	 *  string  Token to track the current STUN request.
	 *  uint8   Which interface number this is (for example, IPv4 or IPv6). The GC relays this number in later packets.
	 *
	 * @param p The packet that was just received.
	 * @return True upon success, otherwise false.
	 */
	virtual bool Receive_CLIENT_STUN(Packet *p);

public:
	/**
	 * Create a new cs socket handler for a given cs.
	 * @param s  the socket we are connected with.
	 * @param address IP etc. of the client.
	 */
	NetworkStunSocketHandler(SOCKET s = INVALID_SOCKET) : NetworkTCPSocketHandler(s) {}
};

#endif /* NETWORK_CORE_TCP_STUN_H */
