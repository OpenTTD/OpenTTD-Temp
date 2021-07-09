/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file network_coordinator.h Part of the network protocol handling Game Coordinator requests. */

#ifndef NETWORK_COORDINATOR_H
#define NETWORK_COORDINATOR_H

#include "core/tcp_coordinator.h"
#include "network_stun.h"
#include "network_turn.h"
#include <map>

/**
 * Game Coordinator communication.
 *
 * For servers:
 *  - Server sends CLIENT_REGISTER.
 *  - GC probes server to check if it can directly connect, needs STUN, etc.
 *  - GC sends SERVER_REGISTER_ACK with type of connection.
 *  - Server sends every 30 seconds CLIENT_UPDATE.
 *
 * For clients (listing):
 *  - Client sends CLIENT_LISTING.
 *  - GC returns the full list of public servers via SERVER_LISTING (multiple packets).
 *
 * For clients (connecting):
 *  - Client sends CLIENT_CONNECT.
 *  - GC checks what type of connections the servers supports:
 *    1) Direct connect?
 *        - Send the client a SERVER_CONNECT with the peer address.
 *        - a) Client connects, closes GC connection.
 *        - b) Client connect fails, client sends CLIENT_CONNECT_FAILED to GC.
 *    2) STUN?
 *        - GC sends SERVER_STUN_REQUEST to server/client (asking for both IPv4 and IPv6).
 *        - GC collects what combination works and sends SERVER_STUN_CONNECT to server/client.
 *        - a) Server/client connect, client closes GC connection.
 *        - b) Server/client connect fails, connecting side sends CLIENT_CONNECT_FAILED to GC.
 *        - GC tries other combination if available.
 *    3) TURN?
 *        - GC sends SERVER_TURN_CONNECT to server/client.
 *        - a) Server/client connect, client closes GC connection.
 *        - b) Server/client connect fails, both send CLIENT_CONNECT_FAILED to GC.
 *  - If all fails, GC sends SERVER_CONNECT_FAILED to indicate no connection was possible.
 */

/** Class for handling the client side of the Game Coordinator connection. */
class ClientNetworkCoordinatorSocketHandler : public NetworkCoordinatorSocketHandler {
private:
	std::chrono::steady_clock::time_point next_update; ///< When to send the next update (if server and public).
	std::map<std::string, TCPServerConnecter *> connecter; ///< Based on tokens, the current connecters that are pending.
	std::map<std::string, TCPServerConnecter *> connecter_pre; ///< Based on join-keys, the current connecters that are pending.
	std::map<std::string, std::map<int, std::unique_ptr<ClientNetworkStunSocketHandler>>> stun_handlers; ///< All pending STUN handlers.
	std::unique_ptr<ClientNetworkTurnSocketHandler> turn_handler; ///< Pending TURN handler (if any).
	TCPConnecter *game_connecter = nullptr; ///< Pending connecter to the game server.

protected:
	bool Receive_SERVER_ERROR(Packet *p) override;
	bool Receive_SERVER_REGISTER_ACK(Packet *p) override;
	bool Receive_SERVER_LISTING(Packet *p) override;
	bool Receive_SERVER_CONNECTING(Packet *p) override;
	bool Receive_SERVER_CONNECT_FAILED(Packet *p) override;
	bool Receive_SERVER_DIRECT_CONNECT(Packet *p) override;
	bool Receive_SERVER_STUN_REQUEST(Packet *p) override;
	bool Receive_SERVER_STUN_CONNECT(Packet *p) override;
	bool Receive_SERVER_TURN_CONNECT(Packet *p) override;

public:
	/** The idle timeout; when to close the connection because it's idle. */
	static constexpr std::chrono::seconds IDLE_TIMEOUT = std::chrono::seconds(60);

	std::chrono::steady_clock::time_point last_activity;  ///< The last time there was network activity.
	bool connecting; ///< Are we connecting to the Game Coordinator?

	ClientNetworkCoordinatorSocketHandler() : connecting(false) {}

	NetworkRecvStatus CloseConnection(bool error = true) override;
	void SendReceive();

	void ConnectFailure(const std::string &token, uint8 tracking_number);
	void ConnectSuccess(const std::string &token, SOCKET sock, NetworkAddress &address);
	void StunResult(const std::string &token, uint8 family, bool result);

	void Connect();
	void CloseToken(const std::string &token);
	void CloseAllTokens();
	void CloseStunHandler(const std::string &token, uint8 family = AF_UNSPEC);
	void CloseTurnHandler(const std::string &token);

	void Register();
	void SendServerUpdate(GameInfoNewGRFMode newgrf_mode);
	void GetListing();

	void ConnectToServer(const std::string &join_key, TCPServerConnecter *connecter);
};

extern ClientNetworkCoordinatorSocketHandler _network_coordinator_client;

#endif /* NETWORK_COORDINATOR_H */
