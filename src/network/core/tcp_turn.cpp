/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file tcp_turn.cpp Basic functions to receive and send TURN packets.
 */

#include "../../stdafx.h"
#include "../../date_func.h"
#include "../../debug.h"
#include "tcp_turn.h"

#include "../../safeguards.h"

/**
 * Handle the given packet, i.e. pass it to the right
 * parser receive command.
 * @param p the packet to handle
 * @return true if we should immediately handle further packets, false otherwise
 */
bool NetworkTurnSocketHandler::HandlePacket(Packet *p)
{
	PacketTurnType type = (PacketTurnType)p->Recv_uint8();

	switch (type) {
		case PACKET_TURN_SERVER_ERROR:     return this->Receive_SERVER_ERROR(p);
		case PACKET_TURN_CLIENT_CONNECT:   return this->Receive_CLIENT_CONNECT(p);
		case PACKET_TURN_SERVER_CONNECTED: return this->Receive_SERVER_CONNECTED(p);

		default:
			Debug(net, 0, "[tcp/turn] Received invalid packet type {}", type);
			return false;
	}
}

/**
 * Receive a packet at TCP level
 * @return Whether at least one packet was received.
 */
bool NetworkTurnSocketHandler::ReceivePackets()
{
	Packet *p;
	static const int MAX_PACKETS_TO_RECEIVE = 4;
	int i = MAX_PACKETS_TO_RECEIVE;
	while (--i != 0 && (p = this->ReceivePacket()) != nullptr) {
		bool cont = this->HandlePacket(p);
		delete p;
		if (!cont) return true;
	}

	return i != MAX_PACKETS_TO_RECEIVE - 1;
}

/**
 * Helper for logging receiving invalid packets.
 * @param type The received packet type.
 * @return Always false, as it's an error.
 */
bool NetworkTurnSocketHandler::ReceiveInvalidPacket(PacketTurnType type)
{
	Debug(net, 0, "[tcp/turn] Received illegal packet type {}", type);
	return false;
}

bool NetworkTurnSocketHandler::Receive_SERVER_ERROR(Packet *p) { return this->ReceiveInvalidPacket(PACKET_TURN_SERVER_ERROR); }
bool NetworkTurnSocketHandler::Receive_CLIENT_CONNECT(Packet *p) { return this->ReceiveInvalidPacket(PACKET_TURN_CLIENT_CONNECT); }
bool NetworkTurnSocketHandler::Receive_SERVER_CONNECTED(Packet *p) { return this->ReceiveInvalidPacket(PACKET_TURN_SERVER_CONNECTED); }
