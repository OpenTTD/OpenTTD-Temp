/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file tcp_content.cpp Basic functions to receive and send Content packets.
 */

#include "../../stdafx.h"
#ifndef OPENTTD_MSU
#include "../../textfile_gui.h"
#include "../../newgrf_config.h"
#include "../../base_media_base.h"
#include "../../ai/ai.hpp"
#include "../../game/game.hpp"
#include "../../fios.h"
#endif /* OPENTTD_MSU */
#include "tcp_content.h"

#include "../../safeguards.h"

/** Clear everything in the struct */
ContentInfo::ContentInfo()
{
	memset(this, 0, sizeof(*this));
}

/** Free everything allocated */
ContentInfo::~ContentInfo()
{
	free(this->dependencies);
	free(this->tags);
}

/**
 * Copy data from other #ContentInfo and take ownership of allocated stuff.
 * @param other Source to copy from. #dependencies and #tags will be NULLed.
 */
void ContentInfo::TransferFrom(ContentInfo *other)
{
	if (other != this) {
		free(this->dependencies);
		free(this->tags);
		memcpy(this, other, sizeof(ContentInfo));
		other->dependencies = nullptr;
		other->tags = nullptr;
	}
}

/**
 * Get the size of the data as send over the network.
 * @return the size.
 */
size_t ContentInfo::Size() const
{
	size_t len = 0;
	for (uint i = 0; i < this->tag_count; i++) len += strlen(this->tags[i]) + 1;

	/* The size is never larger than the content info size plus the size of the
	 * tags and dependencies */
	return sizeof(*this) +
			sizeof(this->dependency_count) +
			sizeof(*this->dependencies) * this->dependency_count;
}

/**
 * Is the state either selected or autoselected?
 * @return true iff that's the case
 */
bool ContentInfo::IsSelected() const
{
	switch (this->state) {
		case ContentInfo::SELECTED:
		case ContentInfo::AUTOSELECTED:
		case ContentInfo::ALREADY_HERE:
			return true;

		default:
			return false;
	}
}

/**
 * Is the information from this content info valid?
 * @return true iff it's valid
 */
bool ContentInfo::IsValid() const
{
	return this->state < ContentInfo::INVALID && this->type >= CONTENT_TYPE_BEGIN && this->type < CONTENT_TYPE_END;
}

#ifndef OPENTTD_MSU
/**
 * Search a textfile file next to this file in the content list.
 * @param type The type of the textfile to search for.
 * @return The filename for the textfile, \c nullptr otherwise.
 */
const char *ContentInfo::GetTextfile(TextfileType type, TextDirection *text_dir) const
{
	if (this->state == INVALID) return nullptr;
	const char *tmp;
	switch (this->type) {
		default: NOT_REACHED();
		case CONTENT_TYPE_AI:
			tmp = AI::GetScannerInfo()->FindMainScript(this, true);
			break;
		case CONTENT_TYPE_AI_LIBRARY:
			tmp = AI::GetScannerLibrary()->FindMainScript(this, true);
			break;
		case CONTENT_TYPE_GAME:
			tmp = Game::GetScannerInfo()->FindMainScript(this, true);
			break;
		case CONTENT_TYPE_GAME_LIBRARY:
			tmp = Game::GetScannerLibrary()->FindMainScript(this, true);
			break;
		case CONTENT_TYPE_NEWGRF: {
			const GRFConfig *gc = FindGRFConfig(BSWAP32(this->unique_id), FGCM_EXACT, this->md5sum);
			tmp = gc != nullptr ? gc->filename : nullptr;
			break;
		}
		case CONTENT_TYPE_BASE_GRAPHICS:
			tmp = TryGetBaseSetFile(this, true, BaseGraphics::GetAvailableSets());
			break;
		case CONTENT_TYPE_BASE_SOUNDS:
			tmp = TryGetBaseSetFile(this, true, BaseSounds::GetAvailableSets());
			break;
		case CONTENT_TYPE_BASE_MUSIC:
			tmp = TryGetBaseSetFile(this, true, BaseMusic::GetAvailableSets());
			break;
		case CONTENT_TYPE_SCENARIO:
		case CONTENT_TYPE_HEIGHTMAP:
			extern const char *FindScenario(const ContentInfo *ci, bool md5sum);
			tmp = FindScenario(this, true);
			break;
	}
	if (tmp == nullptr) return nullptr;
	return ::GetTextfile(type, GetContentInfoSubDir(this->type), tmp, text_dir);
}
#endif /* OPENTTD_MSU */

void NetworkContentSocketHandler::Close()
{
	CloseConnection();
	if (this->sock == INVALID_SOCKET) return;

	closesocket(this->sock);
	this->sock = INVALID_SOCKET;
}

/**
 * Handle the given packet, i.e. pass it to the right
 * parser receive command.
 * @param p the packet to handle
 * @return true if we should immediately handle further packets, false otherwise
 */
bool NetworkContentSocketHandler::HandlePacket(Packet *p)
{
	PacketContentType type = (PacketContentType)p->Recv_uint8();

	switch (this->HasClientQuit() ? PACKET_CONTENT_END : type) {
		case PACKET_CONTENT_CLIENT_INFO_LIST:      return this->Receive_CLIENT_INFO_LIST(p);
		case PACKET_CONTENT_CLIENT_INFO_ID:        return this->Receive_CLIENT_INFO_ID(p);
		case PACKET_CONTENT_CLIENT_INFO_EXTID:     return this->Receive_CLIENT_INFO_EXTID(p);
		case PACKET_CONTENT_CLIENT_INFO_EXTID_MD5: return this->Receive_CLIENT_INFO_EXTID_MD5(p);
		case PACKET_CONTENT_SERVER_INFO:           return this->Receive_SERVER_INFO(p);
		case PACKET_CONTENT_CLIENT_CONTENT:        return this->Receive_CLIENT_CONTENT(p);
		case PACKET_CONTENT_SERVER_CONTENT:        return this->Receive_SERVER_CONTENT(p);

		default:
			if (this->HasClientQuit()) {
				DEBUG(net, 0, "[tcp/content] received invalid packet type %d from %s", type, this->client_addr.GetAddressAsString().c_str());
			} else {
				DEBUG(net, 0, "[tcp/content] received illegal packet from %s", this->client_addr.GetAddressAsString().c_str());
			}
			return false;
	}
}

/**
 * Receive a packet at TCP level
 * @return Whether at least one packet was received.
 */
bool NetworkContentSocketHandler::ReceivePackets()
{
	/*
	 * We read only a few of the packets. This as receiving packets can be expensive
	 * due to the re-resolving of the parent/child relations and checking the toggle
	 * state of all bits. We cannot do this all in one go, as we want to show the
	 * user what we already received. Otherwise, it can take very long before any
	 * progress is shown to the end user that something has been received.
	 * It is also the case that we request extra content from the content server in
	 * case there is an unknown (in the content list) piece of content. These will
	 * come in after the main lists have been requested. As a result, we won't be
	 * getting everything reliably in one batch. Thus, we need to make subsequent
	 * updates in that case as well.
	 *
	 * As a result, we simple handle an arbitrary number of packets in one cycle,
	 * and let the rest be handled in subsequent cycles. These are ran, almost,
	 * immediately after this cycle so in speed it does not matter much, except
	 * that the user inferface will appear better responding.
	 *
	 * What arbitrary number to choose is the ultimate question though.
	 */
	Packet *p;
	static const int MAX_PACKETS_TO_RECEIVE = 42;
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
bool NetworkContentSocketHandler::ReceiveInvalidPacket(PacketContentType type)
{
	DEBUG(net, 0, "[tcp/content] received illegal packet type %d from %s", type, this->client_addr.GetAddressAsString().c_str());
	return false;
}

bool NetworkContentSocketHandler::Receive_CLIENT_INFO_LIST(Packet *p) { return this->ReceiveInvalidPacket(PACKET_CONTENT_CLIENT_INFO_LIST); }
bool NetworkContentSocketHandler::Receive_CLIENT_INFO_ID(Packet *p) { return this->ReceiveInvalidPacket(PACKET_CONTENT_CLIENT_INFO_ID); }
bool NetworkContentSocketHandler::Receive_CLIENT_INFO_EXTID(Packet *p) { return this->ReceiveInvalidPacket(PACKET_CONTENT_CLIENT_INFO_EXTID); }
bool NetworkContentSocketHandler::Receive_CLIENT_INFO_EXTID_MD5(Packet *p) { return this->ReceiveInvalidPacket(PACKET_CONTENT_CLIENT_INFO_EXTID_MD5); }
bool NetworkContentSocketHandler::Receive_SERVER_INFO(Packet *p) { return this->ReceiveInvalidPacket(PACKET_CONTENT_SERVER_INFO); }
bool NetworkContentSocketHandler::Receive_CLIENT_CONTENT(Packet *p) { return this->ReceiveInvalidPacket(PACKET_CONTENT_CLIENT_CONTENT); }
bool NetworkContentSocketHandler::Receive_SERVER_CONTENT(Packet *p) { return this->ReceiveInvalidPacket(PACKET_CONTENT_SERVER_CONTENT); }

#ifndef OPENTTD_MSU
/**
 * Helper to get the subdirectory a #ContentInfo is located in.
 * @param type The type of content.
 * @return The subdirectory the content is located in.
 */
Subdirectory GetContentInfoSubDir(ContentType type)
{
	switch (type) {
		default: return NO_DIRECTORY;
		case CONTENT_TYPE_AI:           return AI_DIR;
		case CONTENT_TYPE_AI_LIBRARY:   return AI_LIBRARY_DIR;
		case CONTENT_TYPE_GAME:         return GAME_DIR;
		case CONTENT_TYPE_GAME_LIBRARY: return GAME_LIBRARY_DIR;
		case CONTENT_TYPE_NEWGRF:       return NEWGRF_DIR;

		case CONTENT_TYPE_BASE_GRAPHICS:
		case CONTENT_TYPE_BASE_SOUNDS:
		case CONTENT_TYPE_BASE_MUSIC:
			return BASESET_DIR;

		case CONTENT_TYPE_SCENARIO:     return SCENARIO_DIR;
		case CONTENT_TYPE_HEIGHTMAP:    return HEIGHTMAP_DIR;
	}
}
#endif /* OPENTTD_MSU */
