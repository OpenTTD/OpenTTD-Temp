/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file tcp_stun.cpp Basic functions to receive and send STUN packets.
 */

#include "../../stdafx.h"
#include "../../debug.h"
#include "tcp_stun.h"

#include "../../safeguards.h"

/**
 * Helper for logging receiving invalid packets.
 * @param type The received packet type.
 * @return Always false, as it's an error.
 */
bool NetworkStunSocketHandler::ReceiveInvalidPacket(PacketStunType type)
{
	Debug(net, 0, "[tcp/stun] Received illegal packet type {}", type);
	return false;
}

bool NetworkStunSocketHandler::Receive_CLIENT_STUN(Packet *p) { return this->ReceiveInvalidPacket(PACKET_STUN_CLIENT_STUN); }
