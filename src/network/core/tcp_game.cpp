/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file tcp_game.cpp Basic functions to receive and send TCP packets for game purposes.
 */

#include "../../stdafx.h"

#include "../network.h"
#include "../network_internal.h"
#include "../../debug.h"
#include "../../error.h"

#include "table/strings.h"

#include "../../safeguards.h"

/**
 * Create a new socket for the game connection.
 * @param s The socket to connect with.
 */
NetworkGameSocketHandler::NetworkGameSocketHandler(SOCKET s, ClientID client_id) : info(nullptr), client_id(client_id),
		last_frame(_frame_counter), last_frame_server(_frame_counter)
{
	this->sock = s;
	this->last_packet = std::chrono::steady_clock::now();
}

/**
 * Functions to help ReceivePacket/SendPacket a bit
 *  A socket can make errors. When that happens this handles what to do.
 * For clients: close connection and drop back to main-menu
 * For servers: close connection and that is it
 * @return the new status
 */
NetworkRecvStatus NetworkGameSocketHandler::CloseConnection(bool error)
{
	/* Clients drop back to the main menu */
	if (!_network_server && _networking) {
		extern void ClientNetworkEmergencySave(); // from network_client.cpp
		ClientNetworkEmergencySave();
		_switch_mode = SM_MENU;
		_networking = false;
		ShowErrorMessage(STR_NETWORK_ERROR_LOSTCONNECTION, INVALID_STRING_ID, WL_CRITICAL);

		return NETWORK_RECV_STATUS_CONN_LOST;
	}

	return this->CloseConnection(error ? NETWORK_RECV_STATUS_SERVER_ERROR : NETWORK_RECV_STATUS_CONN_LOST);
}


/**
 * Handle the given packet, i.e. pass it to the right parser receive command.
 * @param p the packet to handle
 * @return #NetworkRecvStatus of handling.
 */
NetworkRecvStatus NetworkGameSocketHandler::HandlePacket(Packet *p)
{
	PacketGameType type = (PacketGameType)p->Recv_uint8();

	this->last_packet = std::chrono::steady_clock::now();

	switch (this->HasClientQuit() ? PACKET_END : type) {
		case PACKET_SERVER_FULL:                  return this->Receive_SERVER_FULL(p);
		case PACKET_SERVER_BANNED:                return this->Receive_SERVER_BANNED(p);
		case PACKET_CLIENT_JOIN:                  return this->Receive_CLIENT_JOIN(p);
		case PACKET_SERVER_ERROR:                 return this->Receive_SERVER_ERROR(p);
		case PACKET_CLIENT_COMPANY_INFO:          return this->Receive_CLIENT_COMPANY_INFO(p);
		case PACKET_SERVER_COMPANY_INFO:          return this->Receive_SERVER_COMPANY_INFO(p);
		case PACKET_SERVER_CLIENT_INFO:           return this->Receive_SERVER_CLIENT_INFO(p);
		case PACKET_SERVER_NEED_GAME_PASSWORD:    return this->Receive_SERVER_NEED_GAME_PASSWORD(p);
		case PACKET_SERVER_NEED_COMPANY_PASSWORD: return this->Receive_SERVER_NEED_COMPANY_PASSWORD(p);
		case PACKET_CLIENT_GAME_PASSWORD:         return this->Receive_CLIENT_GAME_PASSWORD(p);
		case PACKET_CLIENT_COMPANY_PASSWORD:      return this->Receive_CLIENT_COMPANY_PASSWORD(p);
		case PACKET_SERVER_WELCOME:               return this->Receive_SERVER_WELCOME(p);
		case PACKET_CLIENT_GETMAP:                return this->Receive_CLIENT_GETMAP(p);
		case PACKET_SERVER_WAIT:                  return this->Receive_SERVER_WAIT(p);
		case PACKET_SERVER_MAP_BEGIN:             return this->Receive_SERVER_MAP_BEGIN(p);
		case PACKET_SERVER_MAP_SIZE:              return this->Receive_SERVER_MAP_SIZE(p);
		case PACKET_SERVER_MAP_DATA:              return this->Receive_SERVER_MAP_DATA(p);
		case PACKET_SERVER_MAP_DONE:              return this->Receive_SERVER_MAP_DONE(p);
		case PACKET_CLIENT_MAP_OK:                return this->Receive_CLIENT_MAP_OK(p);
		case PACKET_SERVER_JOIN:                  return this->Receive_SERVER_JOIN(p);
		case PACKET_SERVER_FRAME:                 return this->Receive_SERVER_FRAME(p);
		case PACKET_SERVER_SYNC:                  return this->Receive_SERVER_SYNC(p);
		case PACKET_CLIENT_ACK:                   return this->Receive_CLIENT_ACK(p);
		case PACKET_CLIENT_COMMAND:               return this->Receive_CLIENT_COMMAND(p);
		case PACKET_SERVER_COMMAND:               return this->Receive_SERVER_COMMAND(p);
		case PACKET_CLIENT_CHAT:                  return this->Receive_CLIENT_CHAT(p);
		case PACKET_SERVER_CHAT:                  return this->Receive_SERVER_CHAT(p);
		case PACKET_CLIENT_SET_PASSWORD:          return this->Receive_CLIENT_SET_PASSWORD(p);
		case PACKET_CLIENT_SET_NAME:              return this->Receive_CLIENT_SET_NAME(p);
		case PACKET_CLIENT_QUIT:                  return this->Receive_CLIENT_QUIT(p);
		case PACKET_CLIENT_ERROR:                 return this->Receive_CLIENT_ERROR(p);
		case PACKET_SERVER_QUIT:                  return this->Receive_SERVER_QUIT(p);
		case PACKET_SERVER_ERROR_QUIT:            return this->Receive_SERVER_ERROR_QUIT(p);
		case PACKET_SERVER_SHUTDOWN:              return this->Receive_SERVER_SHUTDOWN(p);
		case PACKET_SERVER_NEWGAME:               return this->Receive_SERVER_NEWGAME(p);
		case PACKET_SERVER_RCON:                  return this->Receive_SERVER_RCON(p);
		case PACKET_CLIENT_RCON:                  return this->Receive_CLIENT_RCON(p);
		case PACKET_SERVER_CHECK_NEWGRFS:         return this->Receive_SERVER_CHECK_NEWGRFS(p);
		case PACKET_CLIENT_NEWGRFS_CHECKED:       return this->Receive_CLIENT_NEWGRFS_CHECKED(p);
		case PACKET_SERVER_MOVE:                  return this->Receive_SERVER_MOVE(p);
		case PACKET_CLIENT_MOVE:                  return this->Receive_CLIENT_MOVE(p);
		case PACKET_SERVER_COMPANY_UPDATE:        return this->Receive_SERVER_COMPANY_UPDATE(p);
		case PACKET_SERVER_CONFIG_UPDATE:         return this->Receive_SERVER_CONFIG_UPDATE(p);

		default:
			this->CloseConnection();

			if (this->HasClientQuit()) {
				DEBUG(net, 0, "[tcp/game] received invalid packet type %d from client %d", type, this->client_id);
			} else {
				DEBUG(net, 0, "[tcp/game] received illegal packet from client %d", this->client_id);
			}
			return NETWORK_RECV_STATUS_MALFORMED_PACKET;
	}
}

/**
 * Do the actual receiving of packets.
 * As long as HandlePacket returns OKAY packets are handled. Upon
 * failure, or no more packets to process the last result of
 * HandlePacket is returned.
 * @return #NetworkRecvStatus of the last handled packet.
 */
NetworkRecvStatus NetworkGameSocketHandler::ReceivePackets()
{
	Packet *p;
	while ((p = this->ReceivePacket()) != nullptr) {
		NetworkRecvStatus res = HandlePacket(p);
		delete p;
		if (res != NETWORK_RECV_STATUS_OKAY) return res;
	}

	return NETWORK_RECV_STATUS_OKAY;
}

/**
 * Helper for logging receiving invalid packets.
 * @param type The received packet type.
 * @return The status the network should have, in this case: "malformed packet error".
 */
NetworkRecvStatus NetworkGameSocketHandler::ReceiveInvalidPacket(PacketGameType type)
{
	DEBUG(net, 0, "[tcp/game] received illegal packet type %d from client %d", type, this->client_id);
	return NETWORK_RECV_STATUS_MALFORMED_PACKET;
}

NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_FULL(Packet *p) { return this->ReceiveInvalidPacket(PACKET_SERVER_FULL); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_BANNED(Packet *p) { return this->ReceiveInvalidPacket(PACKET_SERVER_BANNED); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_CLIENT_JOIN(Packet *p) { return this->ReceiveInvalidPacket(PACKET_CLIENT_JOIN); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_ERROR(Packet *p) { return this->ReceiveInvalidPacket(PACKET_SERVER_ERROR); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_CLIENT_COMPANY_INFO(Packet *p) { return this->ReceiveInvalidPacket(PACKET_CLIENT_COMPANY_INFO); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_COMPANY_INFO(Packet *p) { return this->ReceiveInvalidPacket(PACKET_SERVER_COMPANY_INFO); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_CLIENT_INFO(Packet *p) { return this->ReceiveInvalidPacket(PACKET_SERVER_CLIENT_INFO); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_NEED_GAME_PASSWORD(Packet *p) { return this->ReceiveInvalidPacket(PACKET_SERVER_NEED_GAME_PASSWORD); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_NEED_COMPANY_PASSWORD(Packet *p) { return this->ReceiveInvalidPacket(PACKET_SERVER_NEED_COMPANY_PASSWORD); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_CLIENT_GAME_PASSWORD(Packet *p) { return this->ReceiveInvalidPacket(PACKET_CLIENT_GAME_PASSWORD); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_CLIENT_COMPANY_PASSWORD(Packet *p) { return this->ReceiveInvalidPacket(PACKET_CLIENT_COMPANY_PASSWORD); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_WELCOME(Packet *p) { return this->ReceiveInvalidPacket(PACKET_SERVER_WELCOME); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_CLIENT_GETMAP(Packet *p) { return this->ReceiveInvalidPacket(PACKET_CLIENT_GETMAP); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_WAIT(Packet *p) { return this->ReceiveInvalidPacket(PACKET_SERVER_WAIT); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_MAP_BEGIN(Packet *p) { return this->ReceiveInvalidPacket(PACKET_SERVER_MAP_BEGIN); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_MAP_SIZE(Packet *p) { return this->ReceiveInvalidPacket(PACKET_SERVER_MAP_SIZE); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_MAP_DATA(Packet *p) { return this->ReceiveInvalidPacket(PACKET_SERVER_MAP_DATA); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_MAP_DONE(Packet *p) { return this->ReceiveInvalidPacket(PACKET_SERVER_MAP_DONE); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_CLIENT_MAP_OK(Packet *p) { return this->ReceiveInvalidPacket(PACKET_CLIENT_MAP_OK); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_JOIN(Packet *p) { return this->ReceiveInvalidPacket(PACKET_SERVER_JOIN); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_FRAME(Packet *p) { return this->ReceiveInvalidPacket(PACKET_SERVER_FRAME); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_SYNC(Packet *p) { return this->ReceiveInvalidPacket(PACKET_SERVER_SYNC); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_CLIENT_ACK(Packet *p) { return this->ReceiveInvalidPacket(PACKET_CLIENT_ACK); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_CLIENT_COMMAND(Packet *p) { return this->ReceiveInvalidPacket(PACKET_CLIENT_COMMAND); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_COMMAND(Packet *p) { return this->ReceiveInvalidPacket(PACKET_SERVER_COMMAND); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_CLIENT_CHAT(Packet *p) { return this->ReceiveInvalidPacket(PACKET_CLIENT_CHAT); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_CHAT(Packet *p) { return this->ReceiveInvalidPacket(PACKET_SERVER_CHAT); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_CLIENT_SET_PASSWORD(Packet *p) { return this->ReceiveInvalidPacket(PACKET_CLIENT_SET_PASSWORD); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_CLIENT_SET_NAME(Packet *p) { return this->ReceiveInvalidPacket(PACKET_CLIENT_SET_NAME); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_CLIENT_QUIT(Packet *p) { return this->ReceiveInvalidPacket(PACKET_CLIENT_QUIT); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_CLIENT_ERROR(Packet *p) { return this->ReceiveInvalidPacket(PACKET_CLIENT_ERROR); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_QUIT(Packet *p) { return this->ReceiveInvalidPacket(PACKET_SERVER_QUIT); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_ERROR_QUIT(Packet *p) { return this->ReceiveInvalidPacket(PACKET_SERVER_ERROR_QUIT); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_SHUTDOWN(Packet *p) { return this->ReceiveInvalidPacket(PACKET_SERVER_SHUTDOWN); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_NEWGAME(Packet *p) { return this->ReceiveInvalidPacket(PACKET_SERVER_NEWGAME); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_RCON(Packet *p) { return this->ReceiveInvalidPacket(PACKET_SERVER_RCON); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_CLIENT_RCON(Packet *p) { return this->ReceiveInvalidPacket(PACKET_CLIENT_RCON); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_CHECK_NEWGRFS(Packet *p) { return this->ReceiveInvalidPacket(PACKET_SERVER_CHECK_NEWGRFS); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_CLIENT_NEWGRFS_CHECKED(Packet *p) { return this->ReceiveInvalidPacket(PACKET_CLIENT_NEWGRFS_CHECKED); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_MOVE(Packet *p) { return this->ReceiveInvalidPacket(PACKET_SERVER_MOVE); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_CLIENT_MOVE(Packet *p) { return this->ReceiveInvalidPacket(PACKET_CLIENT_MOVE); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_COMPANY_UPDATE(Packet *p) { return this->ReceiveInvalidPacket(PACKET_SERVER_COMPANY_UPDATE); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_CONFIG_UPDATE(Packet *p) { return this->ReceiveInvalidPacket(PACKET_SERVER_CONFIG_UPDATE); }
