/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file tcp_content.h Basic functions to receive and send TCP packets to/from the content server.
 */

#ifndef NETWORK_CORE_TCP_CONTENT_H
#define NETWORK_CORE_TCP_CONTENT_H

#include "os_abstraction.h"
#include "tcp.h"
#include "packet.h"
#include "../../debug.h"

/** The values in the enum are important; they are used as database 'keys' */
enum ContentType {
	CONTENT_TYPE_BEGIN         = 1, ///< Helper to mark the begin of the types
	CONTENT_TYPE_BASE_GRAPHICS = 1, ///< The content consists of base graphics
	CONTENT_TYPE_NEWGRF        = 2, ///< The content consists of a NewGRF
	CONTENT_TYPE_AI            = 3, ///< The content consists of an AI
	CONTENT_TYPE_AI_LIBRARY    = 4, ///< The content consists of an AI library
	CONTENT_TYPE_SCENARIO      = 5, ///< The content consists of a scenario
	CONTENT_TYPE_HEIGHTMAP     = 6, ///< The content consists of a heightmap
	CONTENT_TYPE_BASE_SOUNDS   = 7, ///< The content consists of base sounds
	CONTENT_TYPE_BASE_MUSIC    = 8, ///< The content consists of base music
	CONTENT_TYPE_GAME          = 9, ///< The content consists of a game script
	CONTENT_TYPE_GAME_LIBRARY  = 10, ///< The content consists of a GS library
	CONTENT_TYPE_END,               ///< Helper to mark the end of the types
};

/** Enum with all types of TCP content packets. The order MUST not be changed **/
enum PacketContentType {
	PACKET_CONTENT_CLIENT_INFO_LIST,      ///< Queries the content server for a list of info of a given content type
	PACKET_CONTENT_CLIENT_INFO_ID,        ///< Queries the content server for information about a list of internal IDs
	PACKET_CONTENT_CLIENT_INFO_EXTID,     ///< Queries the content server for information about a list of external IDs
	PACKET_CONTENT_CLIENT_INFO_EXTID_MD5, ///< Queries the content server for information about a list of external IDs and MD5
	PACKET_CONTENT_SERVER_INFO,           ///< Reply of content server with information about content
	PACKET_CONTENT_CLIENT_CONTENT,        ///< Request a content file given an internal ID
	PACKET_CONTENT_SERVER_CONTENT,        ///< Reply with the content of the given ID
	PACKET_CONTENT_END,                   ///< Must ALWAYS be on the end of this list!! (period)
};

/** Unique identifier for the content. */
enum ContentID {
	INVALID_CONTENT_ID = UINT32_MAX, ///< Sentinel for invalid content.
};

/** Container for all important information about a piece of content. */
struct ContentInfo {
	/** The state the content can be in. */
	enum State {
		UNSELECTED,     ///< The content has not been selected
		SELECTED,       ///< The content has been manually selected
		AUTOSELECTED,   ///< The content has been selected as dependency
		ALREADY_HERE,   ///< The content is already at the client side
		DOES_NOT_EXIST, ///< The content does not exist in the content system
		INVALID,        ///< The content's invalid
	};

	ContentType type;        ///< Type of content
	ContentID id;            ///< Unique (server side) ID for the content
	uint32 filesize;         ///< Size of the file
	char filename[48];       ///< Filename (for the .tar.gz; only valid on download)
	char name[32];           ///< Name of the content
	char version[16];        ///< Version of the content
	char url[96];            ///< URL related to the content
	char description[512];   ///< Description of the content
	uint32 unique_id;        ///< Unique ID; either GRF ID or shortname
	byte md5sum[16];         ///< The MD5 checksum
	uint8 dependency_count;  ///< Number of dependencies
	ContentID *dependencies; ///< Malloced array of dependencies (unique server side ids)
	uint8 tag_count;         ///< Number of tags
	char (*tags)[32];        ///< Malloced array of tags (strings)
	uint32 upload_date;      ///< Upload date as epoch timestamp
	State state;             ///< Whether the content info is selected (for download)
	bool upgrade;            ///< This item is an upgrade

	ContentInfo();
	~ContentInfo();

	void TransferFrom(ContentInfo *other);

	size_t Size() const;
	bool IsSelected() const;
	bool IsValid() const;
#ifndef OPENTTD_MSU
	const char *GetTextfile(TextfileType type) const;
#endif /* OPENTTD_MSU */
};

/** Base socket handler for all Content TCP sockets */
class NetworkContentSocketHandler : public NetworkTCPSocketHandler {
protected:
	void Close() override;

	bool ReceiveInvalidPacket(PacketContentType type);

	/**
	 * Client requesting a list of content info:
	 *  byte    type
	 *  uint32  openttd version
	 * @param p The packet that was just received.
	 * @return True upon success, otherwise false.
	 */
	virtual bool Receive_CLIENT_INFO_LIST(Packet *p);

	/**
	 * Client requesting a list of content info:
	 *  uint16  count of ids
	 *  uint32  id (count times)
	 * @param p The packet that was just received.
	 * @return True upon success, otherwise false.
	 */
	virtual bool Receive_CLIENT_INFO_ID(Packet *p);

	/**
	 * Client requesting a list of content info based on an external
	 * 'unique' id; GRF ID for NewGRFS, shortname and for base
	 * graphics and AIs.
	 * Scenarios and AI libraries are not supported
	 *  uint8   count of requests
	 *  for each request:
	 *    uint8 type
	 *    unique id (uint32)
	 * @param p The packet that was just received.
	 * @return True upon success, otherwise false.
	 */
	virtual bool Receive_CLIENT_INFO_EXTID(Packet *p);

	/**
	 * Client requesting a list of content info based on an external
	 * 'unique' id; GRF ID + MD5 checksum for NewGRFS, shortname and
	 * xor-ed MD5 checksums for base graphics and AIs.
	 * Scenarios and AI libraries are not supported
	 *  uint8   count of requests
	 *  for each request:
	 *    uint8 type
	 *    unique id (uint32)
	 *    md5 (16 bytes)
	 * @param p The packet that was just received.
	 * @return True upon success, otherwise false.
	 */
	virtual bool Receive_CLIENT_INFO_EXTID_MD5(Packet *p);

	/**
	 * Server sending list of content info:
	 *  byte    type (invalid ID == does not exist)
	 *  uint32  id
	 *  uint32  file_size
	 *  string  name (max 32 characters)
	 *  string  version (max 16 characters)
	 *  uint32  unique id
	 *  uint8   md5sum (16 bytes)
	 *  uint8   dependency count
	 *  uint32  unique id of dependency (dependency count times)
	 *  uint8   tag count
	 *  string  tag (max 32 characters for tag count times)
	 * @param p The packet that was just received.
	 * @return True upon success, otherwise false.
	 */
	virtual bool Receive_SERVER_INFO(Packet *p);

	/**
	 * Client requesting the actual content:
	 *  uint16  count of unique ids
	 *  uint32  unique id (count times)
	 * @param p The packet that was just received.
	 * @return True upon success, otherwise false.
	 */
	virtual bool Receive_CLIENT_CONTENT(Packet *p);

	/**
	 * Server sending list of content info:
	 *  uint32  unique id
	 *  uint32  file size (0 == does not exist)
	 *  string  file name (max 48 characters)
	 * After this initial packet, packets with the actual data are send using
	 * the same packet type.
	 * @param p The packet that was just received.
	 * @return True upon success, otherwise false.
	 */
	virtual bool Receive_SERVER_CONTENT(Packet *p);

	bool HandlePacket(Packet *p);
public:
	/**
	 * Create a new cs socket handler for a given cs
	 * @param s  the socket we are connected with
	 * @param address IP etc. of the client
	 */
	NetworkContentSocketHandler(SOCKET s = INVALID_SOCKET) :
		NetworkTCPSocketHandler(s)
	{
	}

	/** On destructing of this class, the socket needs to be closed */
	virtual ~NetworkContentSocketHandler() { this->Close(); }

	bool ReceivePackets();
};

#ifndef OPENTTD_MSU
Subdirectory GetContentInfoSubDir(ContentType type);
#endif /* OPENTTD_MSU */

#endif /* NETWORK_CORE_TCP_CONTENT_H */
