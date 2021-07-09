/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file network_turn.h Part of the network protocol handling TURN requests. */

#ifndef NETWORK_TURN_H
#define NETWORK_TURN_H

#include "core/tcp_turn.h"

/** Class for handling the client side of the TURN connection. */
class ClientNetworkTurnSocketHandler : public NetworkTurnSocketHandler {
private:
	std::string token;     ///< Token of this connection.
	uint8 tracking_number; ///< Tracking number of this connection.

protected:
	bool Receive_SERVER_ERROR(Packet *p) override;
	bool Receive_SERVER_CONNECTED(Packet *p) override;

public:
	TCPConnecter *connecter = nullptr; ///< Connecter instance.

	ClientNetworkTurnSocketHandler(const std::string &token, uint8 tracking_number) : token(token), tracking_number(tracking_number) {}

	NetworkRecvStatus CloseConnection(bool error = true) override;
	void SendReceive();

	void Connect(const std::string &hostname, uint16 port);
	void ConnectFailure();

	static std::unique_ptr<ClientNetworkTurnSocketHandler> Turn(const std::string &token, uint8 tracking_number, const std::string &host, uint16 port);
};

#endif /* NETWORK_TURN_H */
