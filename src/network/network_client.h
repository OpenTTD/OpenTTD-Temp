/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file network_client.h Client part of the network protocol. */

#ifndef NETWORK_CLIENT_H
#define NETWORK_CLIENT_H

#include "network_internal.h"

/** Class for handling the client side of the game connection. */
class ClientNetworkGameSocketHandler : public ZeroedMemoryAllocator, public NetworkGameSocketHandler {
private:
	struct PacketReader *savegame; ///< Packet reader for reading the savegame.
	byte token;                    ///< The token we need to send back to the server to prove we're the right client.

	/** Status of the connection with the server. */
	enum ServerStatus {
		STATUS_INACTIVE,      ///< The client is not connected nor active.
		STATUS_COMPANY_INFO,  ///< We are trying to get company information.
		STATUS_JOIN,          ///< We are trying to join a server.
		STATUS_NEWGRFS_CHECK, ///< Last action was checking NewGRFs.
		STATUS_AUTH_GAME,     ///< Last action was requesting game (server) password.
		STATUS_AUTH_KEY,      ///< Last action was requesting crypto challenge.
		STATUS_AUTH_COMPANY,  ///< Last action was requesting company password.
		STATUS_AUTHORIZED,    ///< The client is authorized at the server.
		STATUS_MAP_WAIT,      ///< The client is waiting as someone else is downloading the map.
		STATUS_MAP,           ///< The client is downloading the map.
		STATUS_ACTIVE,        ///< The client is active within in the game.
		STATUS_END,           ///< Must ALWAYS be on the end of this list!! (period)
	};

	ServerStatus status; ///< Status of the connection with the server.

protected:
	friend void NetworkExecuteLocalCommandQueue();
	friend void NetworkClose(bool close_admins);
	static ClientNetworkGameSocketHandler *my_client; ///< This is us!

	NetworkRecvStatus Receive_SERVER_FULL(Packet *p) override;
	NetworkRecvStatus Receive_SERVER_BANNED(Packet *p) override;
	NetworkRecvStatus Receive_SERVER_ERROR(Packet *p) override;
	NetworkRecvStatus Receive_SERVER_COMPANY_INFO(Packet *p) override;
	NetworkRecvStatus Receive_SERVER_CLIENT_INFO(Packet *p) override;
	NetworkRecvStatus Receive_SERVER_NEED_GAME_PASSWORD(Packet *p) override;
	NetworkRecvStatus Receive_SERVER_NEED_KEYAUTH(Packet *p) override;
	NetworkRecvStatus Receive_SERVER_NEED_COMPANY_PASSWORD(Packet *p) override;
	NetworkRecvStatus Receive_SERVER_WELCOME(Packet *p) override;
	NetworkRecvStatus Receive_SERVER_WAIT(Packet *p) override;
	NetworkRecvStatus Receive_SERVER_MAP_BEGIN(Packet *p) override;
	NetworkRecvStatus Receive_SERVER_MAP_SIZE(Packet *p) override;
	NetworkRecvStatus Receive_SERVER_MAP_DATA(Packet *p) override;
	NetworkRecvStatus Receive_SERVER_MAP_DONE(Packet *p) override;
	NetworkRecvStatus Receive_SERVER_JOIN(Packet *p) override;
	NetworkRecvStatus Receive_SERVER_FRAME(Packet *p) override;
	NetworkRecvStatus Receive_SERVER_SYNC(Packet *p) override;
	NetworkRecvStatus Receive_SERVER_COMMAND(Packet *p) override;
	NetworkRecvStatus Receive_SERVER_CHAT(Packet *p) override;
	NetworkRecvStatus Receive_SERVER_QUIT(Packet *p) override;
	NetworkRecvStatus Receive_SERVER_ERROR_QUIT(Packet *p) override;
	NetworkRecvStatus Receive_SERVER_SHUTDOWN(Packet *p) override;
	NetworkRecvStatus Receive_SERVER_NEWGAME(Packet *p) override;
	NetworkRecvStatus Receive_SERVER_RCON(Packet *p) override;
	NetworkRecvStatus Receive_SERVER_CHECK_NEWGRFS(Packet *p) override;
	NetworkRecvStatus Receive_SERVER_MOVE(Packet *p) override;
	NetworkRecvStatus Receive_SERVER_COMPANY_UPDATE(Packet *p) override;
	NetworkRecvStatus Receive_SERVER_CONFIG_UPDATE(Packet *p) override;

	static NetworkRecvStatus SendNewGRFsOk();
	static NetworkRecvStatus SendGetMap();
	static NetworkRecvStatus SendMapOk();
	void CheckConnection();
public:
	ClientNetworkGameSocketHandler(SOCKET s);
	~ClientNetworkGameSocketHandler();

	NetworkRecvStatus CloseConnection(NetworkRecvStatus status) override;
	void ClientError(NetworkRecvStatus res);

	static NetworkRecvStatus SendCompanyInformationQuery();

	static NetworkRecvStatus SendJoin();
	static NetworkRecvStatus SendCommand(const CommandPacket *cp);
	static NetworkRecvStatus SendError(NetworkErrorCode errorno);
	static NetworkRecvStatus SendQuit();
	static NetworkRecvStatus SendAck();

	static NetworkRecvStatus SendGamePassword(const char *password);
	static NetworkRecvStatus SendCompanyPassword(const char *password);

	static NetworkRecvStatus SendChat(NetworkAction action, DestType type, int dest, const char *msg, int64 data);
	static NetworkRecvStatus SendSetPassword(const char *password);
	static NetworkRecvStatus SendSetCompanyPubkey(bool protect);
	static NetworkRecvStatus SendSetName(const char *name);
	static NetworkRecvStatus SendRCon(const char *password, const char *command);
	static NetworkRecvStatus SendMove(CompanyID company, const char *password);

	static bool IsConnected();

	static void Send();
	static bool Receive();
	static bool GameLoop();
};

/** Helper to make the code look somewhat nicer. */
typedef ClientNetworkGameSocketHandler MyClient;

void NetworkClient_Connected();
void NetworkClientSetCompanyPassword(const char *password);
void NetworkClientSetCompanyPubkey(bool protect);

extern CompanyID _network_join_as;

extern const char *_network_join_server_password;
extern const char *_network_join_company_password;

extern struct hydro_sign_keypair _network_keypair;

#endif /* NETWORK_CLIENT_H */
