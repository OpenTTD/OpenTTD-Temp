/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file config.h Configuration options of the network stuff. It is used even when compiling without network support.
 */

#ifndef NETWORK_CORE_CONFIG_H
#define NETWORK_CORE_CONFIG_H

/** DNS hostname of the masterserver */
static const char * const NETWORK_MASTER_SERVER_HOST            = "master.openttd.org";
/** DNS hostname of the content server */
static const char * const NETWORK_CONTENT_SERVER_HOST           = "content.openttd.org";
/** DNS hostname of the HTTP-content mirror server */
static const char * const NETWORK_CONTENT_MIRROR_HOST           = "binaries.openttd.org";
/** URL of the HTTP mirror system */
static const char * const NETWORK_CONTENT_MIRROR_URL            = "/bananas";
/** Message sent to the masterserver to 'identify' this client as OpenTTD */
static const char * const NETWORK_MASTER_SERVER_WELCOME_MESSAGE = "OpenTTDRegister";

static const uint16 NETWORK_MASTER_SERVER_PORT    = 3978;         ///< The default port of the master server (UDP)
static const uint16 NETWORK_CONTENT_SERVER_PORT   = 3978;         ///< The default port of the content server (TCP)
static const uint16 NETWORK_CONTENT_MIRROR_PORT   =   80;         ///< The default port of the content mirror (TCP)
static const uint16 NETWORK_DEFAULT_PORT          = 3979;         ///< The default port of the game server (TCP & UDP)
static const uint16 NETWORK_ADMIN_PORT            = 3977;         ///< The default port for admin network
static const uint16 NETWORK_DEFAULT_DEBUGLOG_PORT = 3982;         ///< The default port debug-log is sent to (TCP)

static const uint16 UDP_MTU                       = 1460;         ///< Number of bytes we can pack in a single UDP packet
/*
 * Technically a TCP packet could become 64kiB, however the high bit is kept so it becomes possible in the future
 * to go to (significantly) larger packets if needed. This would entail a strategy such as employed for UTF-8.
 *
 * Packets up to 32 KiB have the high bit not set:
 * 00000000 00000000 0bbbbbbb aaaaaaaa -> aaaaaaaa 0bbbbbbb
 * Send_uint16(GB(size, 0, 15)
 *
 * Packets up to 1 GiB, first uint16 has high bit set so it knows to read a
 * next uint16 for the remaining bits of the size.
 * 00dddddd cccccccc bbbbbbbb aaaaaaaa -> cccccccc 10dddddd aaaaaaaa bbbbbbbb
 * Send_uint16(GB(size, 16, 14) | 0b10 << 14)
 * Send_uint16(GB(size,  0, 16))
 */
static const uint16 TCP_MTU                       = 32767;        ///< Number of bytes we can pack in a single TCP packet
static const uint16 COMPAT_MTU                    = 1460;         ///< Number of bytes we can pack in a single packet for backward compatibility

static const byte NETWORK_GAME_ADMIN_VERSION      =    1;         ///< What version of the admin network do we use?
static const byte NETWORK_GAME_INFO_VERSION       =    5;         ///< What version of game-info do we use?
static const byte NETWORK_COMPANY_INFO_VERSION    =    6;         ///< What version of company info is this?
static const byte NETWORK_MASTER_SERVER_VERSION   =    2;         ///< What version of master-server-protocol do we use?

static const uint NETWORK_NAME_LENGTH             =   80;         ///< The maximum length of the server name and map name, in bytes including '\0'
static const uint NETWORK_COMPANY_NAME_LENGTH     =  128;         ///< The maximum length of the company name, in bytes including '\0'
static const uint NETWORK_HOSTNAME_LENGTH         =   80;         ///< The maximum length of the host name, in bytes including '\0'
static const uint NETWORK_HOSTNAME_PORT_LENGTH    =   80 + 6;     ///< The maximum length of the host name + port, in bytes including '\0'. The extra six is ":" + port number (with a max of 65536)
static const uint NETWORK_SERVER_ID_LENGTH        =   33;         ///< The maximum length of the network id of the servers, in bytes including '\0'
static const uint NETWORK_REVISION_LENGTH         =   33;         ///< The maximum length of the revision, in bytes including '\0'
static const uint NETWORK_PASSWORD_LENGTH         =   33;         ///< The maximum length of the password, in bytes including '\0' (must be >= NETWORK_SERVER_ID_LENGTH)
static const uint NETWORK_CLIENTS_LENGTH          =  200;         ///< The maximum length for the list of clients that controls a company, in bytes including '\0'
static const uint NETWORK_CLIENT_NAME_LENGTH      =   25;         ///< The maximum length of a client's name, in bytes including '\0'
static const uint NETWORK_RCONCOMMAND_LENGTH      =  500;         ///< The maximum length of a rconsole command, in bytes including '\0'
static const uint NETWORK_GAMESCRIPT_JSON_LENGTH  = COMPAT_MTU-3; ///< The maximum length of a gamescript json string, in bytes including '\0'. Must not be longer than COMPAT_MTU including header (3 bytes)
static const uint NETWORK_CHAT_LENGTH             =  900;         ///< The maximum length of a chat message, in bytes including '\0'

static const uint NETWORK_GRF_NAME_LENGTH         =   80;         ///< Maximum length of the name of a GRF

/**
 * Maximum number of GRFs that can be sent.
 * This limit is reached when PACKET_UDP_SERVER_RESPONSE reaches the maximum size of UDP_MTU bytes.
 */
static const uint NETWORK_MAX_GRF_COUNT           =   62;

/**
 * The number of landscapes in OpenTTD.
 * This number must be equal to NUM_LANDSCAPE, but as this number is used
 * within the network code and that the network code is shared with the
 * masterserver/updater, it has to be declared in here too. In network.cpp
 * there is a compile assertion to check that this NUM_LANDSCAPE is equal
 * to NETWORK_NUM_LANDSCAPES.
 */
static const uint NETWORK_NUM_LANDSCAPES          =    4;

#endif /* NETWORK_CORE_CONFIG_H */
