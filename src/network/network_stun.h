/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file network_stun.h Part of the network protocol handling STUN requests. */

#ifndef NETWORK_STUN_H
#define NETWORK_STUN_H

#include "core/tcp_stun.h"

/** Class for handling the client side of the STUN connection. */
class ClientNetworkStunSocketHandler : public NetworkStunSocketHandler {
private:
	std::string token;                 ///< Token of this STUN handler.
	uint8 family = AF_UNSPEC;          ///< Family of this STUN handler.
	bool sent_result = false;          ///< Did we sent the result of the STUN connection?

public:
	TCPConnecter *connecter = nullptr; ///< Connecter instance.
	NetworkAddress local_addr;         ///< Local addresses of the socket.

	NetworkRecvStatus CloseConnection(bool error = true) override;
	void SendReceive();

	void Connect(const std::string &token, uint8 family);

	static std::unique_ptr<ClientNetworkStunSocketHandler> Stun(const std::string &token, uint8 family);
};

#endif /* NETWORK_STUN_H */
