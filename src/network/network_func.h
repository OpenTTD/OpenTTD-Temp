/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file network_func.h Network functions used by other parts of OpenTTD. */

#ifndef NETWORK_FUNC_H
#define NETWORK_FUNC_H

/**
 * Uncomment the following define to enable command replaying.
 * See docs/desync.txt for details.
 */
// #define DEBUG_DUMP_COMMANDS
// #define DEBUG_FAILED_DUMP_COMMANDS

#include "network_type.h"
#include "../console_type.h"
#include "../gfx_type.h"
#include "../openttd.h"
#include "../company_type.h"
#include "../string_type.h"

extern NetworkCompanyState *_network_company_states;

extern ClientID _network_own_client_id;
extern ClientID _redirect_console_to_client;
extern uint8 _network_reconnect;
extern StringList _network_bind_list;
extern StringList _network_host_list;
extern StringList _network_ban_list;

byte NetworkSpectatorCount();
bool NetworkIsValidClientName(const std::string_view client_name);
bool NetworkValidateOurClientName();
bool NetworkValidateClientName(std::string &client_name);
bool NetworkValidateServerName(std::string &server_name);
void NetworkUpdateClientName(const std::string &client_name);
void NetworkUpdateServerGameType();
bool NetworkCompanyHasClients(CompanyID company);
std::string NetworkChangeCompanyPassword(CompanyID company_id, std::string password);
void NetworkReboot();
void NetworkDisconnect(bool blocking = false, bool close_admins = true);
void NetworkGameLoop();
void NetworkBackgroundLoop();
std::string_view ParseFullConnectionString(const std::string &connection_string, uint16 &port, CompanyID *company_id = nullptr);
void NetworkStartDebugLog(const std::string &connection_string);
void NetworkPopulateCompanyStats(NetworkCompanyStats *stats);

void NetworkUpdateClientInfo(ClientID client_id);
void NetworkClientsToSpectators(CompanyID cid);
bool NetworkClientConnectGame(const std::string &connection_string, CompanyID default_company, const std::string &join_server_password = "", const std::string &join_company_password = "");
void NetworkClientJoinGame();
void NetworkClientRequestMove(CompanyID company, const std::string &pass = "");
void NetworkClientSendRcon(const std::string &password, const std::string &command);
void NetworkClientSendChat(NetworkAction action, DestType type, int dest, const std::string &msg, int64 data = 0);
bool NetworkClientPreferTeamChat(const NetworkClientInfo *cio);
bool NetworkCompanyIsPassworded(CompanyID company_id);
bool NetworkMaxCompaniesReached();
bool NetworkMaxSpectatorsReached();
void NetworkPrintClients();
void NetworkHandlePauseChange(PauseMode prev_mode, PauseMode changed_mode);

/*** Commands ran by the server ***/
void NetworkServerDailyLoop();
void NetworkServerMonthlyLoop();
void NetworkServerYearlyLoop();
void NetworkServerSendConfigUpdate();
void NetworkServerUpdateGameInfo();
void NetworkServerShowStatusToConsole();
bool NetworkServerStart();
void NetworkServerNewCompany(const Company *company, NetworkClientInfo *ci);
bool NetworkServerChangeClientName(ClientID client_id, const std::string &new_name);


void NetworkServerDoMove(ClientID client_id, CompanyID company_id);
void NetworkServerSendRcon(ClientID client_id, TextColour colour_code, const std::string &string);
void NetworkServerSendChat(NetworkAction action, DestType type, int dest, const std::string &msg, ClientID from_id, int64 data = 0, bool from_admin = false);

void NetworkServerKickClient(ClientID client_id, const std::string &reason);
uint NetworkServerKickOrBanIP(ClientID client_id, bool ban, const std::string &reason);
uint NetworkServerKickOrBanIP(const std::string &ip, bool ban, const std::string &reason);

void NetworkInitChatMessage();
void CDECL NetworkAddChatMessage(TextColour colour, uint duration, const std::string &message);
void NetworkUndrawChatMessage();
void NetworkChatMessageLoop();

void NetworkAfterNewGRFScan();

#endif /* NETWORK_FUNC_H */
