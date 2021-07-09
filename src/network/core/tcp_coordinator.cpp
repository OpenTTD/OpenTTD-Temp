/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file tcp_coordinator.cpp Basic functions to receive and send Game Coordinator packets.
 */

#include "../../stdafx.h"
#include "../../date_func.h"
#include "../../debug.h"
#include "tcp_coordinator.h"

#include "../../safeguards.h"

/**
 * Handle the given packet, i.e. pass it to the right
 * parser receive command.
 * @param p the packet to handle
 * @return true if we should immediately handle further packets, false otherwise
 */
bool NetworkCoordinatorSocketHandler::HandlePacket(Packet *p)
{
	PacketCoordinatorType type = (PacketCoordinatorType)p->Recv_uint8();

	switch (type) {
		case PACKET_COORDINATOR_SERVER_ERROR:          return this->Receive_SERVER_ERROR(p);
		case PACKET_COORDINATOR_CLIENT_REGISTER:       return this->Receive_CLIENT_REGISTER(p);
		case PACKET_COORDINATOR_SERVER_REGISTER_ACK:   return this->Receive_SERVER_REGISTER_ACK(p);
		case PACKET_COORDINATOR_CLIENT_UPDATE:         return this->Receive_CLIENT_UPDATE(p);
		case PACKET_COORDINATOR_CLIENT_LISTING:        return this->Receive_CLIENT_LISTING(p);
		case PACKET_COORDINATOR_SERVER_LISTING:        return this->Receive_SERVER_LISTING(p);
		case PACKET_COORDINATOR_CLIENT_CONNECT:        return this->Receive_CLIENT_CONNECT(p);
		case PACKET_COORDINATOR_SERVER_CONNECTING:     return this->Receive_SERVER_CONNECTING(p);
		case PACKET_COORDINATOR_CLIENT_CONNECT_FAILED: return this->Receive_CLIENT_CONNECT_FAILED(p);
		case PACKET_COORDINATOR_SERVER_CONNECT_FAILED: return this->Receive_SERVER_CONNECT_FAILED(p);
		case PACKET_COORDINATOR_CLIENT_CONNECTED:      return this->Receive_CLIENT_CONNECTED(p);
		case PACKET_COORDINATOR_SERVER_DIRECT_CONNECT: return this->Receive_SERVER_DIRECT_CONNECT(p);
		case PACKET_COORDINATOR_SERVER_STUN_REQUEST:   return this->Receive_SERVER_STUN_REQUEST(p);
		case PACKET_COORDINATOR_SERVER_STUN_CONNECT:   return this->Receive_SERVER_STUN_CONNECT(p);
		case PACKET_COORDINATOR_CLIENT_STUN_RESULT:    return this->Receive_CLIENT_STUN_RESULT(p);
		case PACKET_COORDINATOR_SERVER_TURN_CONNECT:   return this->Receive_SERVER_TURN_CONNECT(p);

		default:
			Debug(net, 0, "[tcp/coordinator] Received invalid packet type {}", type);
			return false;
	}
}

/**
 * Receive a packet at TCP level
 * @return Whether at least one packet was received.
 */
bool NetworkCoordinatorSocketHandler::ReceivePackets()
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
bool NetworkCoordinatorSocketHandler::ReceiveInvalidPacket(PacketCoordinatorType type)
{
	Debug(net, 0, "[tcp/coordinator] Received illegal packet type {}", type);
	return false;
}

bool NetworkCoordinatorSocketHandler::Receive_SERVER_ERROR(Packet *p) { return this->ReceiveInvalidPacket(PACKET_COORDINATOR_SERVER_ERROR); }
bool NetworkCoordinatorSocketHandler::Receive_CLIENT_REGISTER(Packet *p) { return this->ReceiveInvalidPacket(PACKET_COORDINATOR_CLIENT_REGISTER); }
bool NetworkCoordinatorSocketHandler::Receive_SERVER_REGISTER_ACK(Packet *p) { return this->ReceiveInvalidPacket(PACKET_COORDINATOR_SERVER_REGISTER_ACK); }
bool NetworkCoordinatorSocketHandler::Receive_CLIENT_UPDATE(Packet *p) { return this->ReceiveInvalidPacket(PACKET_COORDINATOR_CLIENT_UPDATE); }
bool NetworkCoordinatorSocketHandler::Receive_CLIENT_LISTING(Packet *p) { return this->ReceiveInvalidPacket(PACKET_COORDINATOR_CLIENT_LISTING); }
bool NetworkCoordinatorSocketHandler::Receive_SERVER_LISTING(Packet *p) { return this->ReceiveInvalidPacket(PACKET_COORDINATOR_SERVER_LISTING); }
bool NetworkCoordinatorSocketHandler::Receive_CLIENT_CONNECT(Packet *p) { return this->ReceiveInvalidPacket(PACKET_COORDINATOR_CLIENT_CONNECT); }
bool NetworkCoordinatorSocketHandler::Receive_SERVER_CONNECTING(Packet *p) { return this->ReceiveInvalidPacket(PACKET_COORDINATOR_SERVER_CONNECTING); }
bool NetworkCoordinatorSocketHandler::Receive_CLIENT_CONNECT_FAILED(Packet *p) { return this->ReceiveInvalidPacket(PACKET_COORDINATOR_CLIENT_CONNECT_FAILED); }
bool NetworkCoordinatorSocketHandler::Receive_SERVER_CONNECT_FAILED(Packet *p) { return this->ReceiveInvalidPacket(PACKET_COORDINATOR_SERVER_CONNECT_FAILED); }
bool NetworkCoordinatorSocketHandler::Receive_CLIENT_CONNECTED(Packet *p) { return this->ReceiveInvalidPacket(PACKET_COORDINATOR_CLIENT_CONNECTED); }
bool NetworkCoordinatorSocketHandler::Receive_SERVER_DIRECT_CONNECT(Packet *p) { return this->ReceiveInvalidPacket(PACKET_COORDINATOR_SERVER_DIRECT_CONNECT); }
bool NetworkCoordinatorSocketHandler::Receive_SERVER_STUN_REQUEST(Packet *p) { return this->ReceiveInvalidPacket(PACKET_COORDINATOR_SERVER_STUN_REQUEST); }
bool NetworkCoordinatorSocketHandler::Receive_SERVER_STUN_CONNECT(Packet *p) { return this->ReceiveInvalidPacket(PACKET_COORDINATOR_SERVER_STUN_CONNECT); }
bool NetworkCoordinatorSocketHandler::Receive_CLIENT_STUN_RESULT(Packet *p) { return this->ReceiveInvalidPacket(PACKET_COORDINATOR_CLIENT_STUN_RESULT); }
bool NetworkCoordinatorSocketHandler::Receive_SERVER_TURN_CONNECT(Packet *p) { return this->ReceiveInvalidPacket(PACKET_COORDINATOR_SERVER_TURN_CONNECT); }
