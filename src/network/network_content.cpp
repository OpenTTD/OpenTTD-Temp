/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file network_content.cpp Content sending/receiving part of the network protocol. */

#include "../stdafx.h"
#include "../rev.h"
#include "../ai/ai.hpp"
#include "../game/game.hpp"
#include "../window_func.h"
#include "../error.h"
#include "../base_media_base.h"
#include "../settings_type.h"
#include "network_content.h"

#include "table/strings.h"

#if defined(WITH_ZLIB)
#include <zlib.h>
#endif

#ifdef __EMSCRIPTEN__
#	include <emscripten.h>
#endif

#include "../safeguards.h"

extern bool HasScenario(const ContentInfo *ci, bool md5sum);

/** The client we use to connect to the server. */
ClientNetworkContentSocketHandler _network_content_client;

/** Wrapper function for the HasProc */
static bool HasGRFConfig(const ContentInfo *ci, bool md5sum)
{
	return FindGRFConfig(BSWAP32(ci->unique_id), md5sum ? FGCM_EXACT : FGCM_ANY, md5sum ? ci->md5sum : nullptr) != nullptr;
}

/**
 * Check whether a function piece of content is locally known.
 * Matches on the unique ID and possibly the MD5 checksum.
 * @param ci     the content info to search for
 * @param md5sum also match the MD5 checksum?
 * @return true iff it's known
 */
typedef bool (*HasProc)(const ContentInfo *ci, bool md5sum);

bool ClientNetworkContentSocketHandler::Receive_SERVER_INFO(Packet *p)
{
	ContentInfo *ci = new ContentInfo();
	ci->type     = (ContentType)p->Recv_uint8();
	ci->id       = (ContentID)p->Recv_uint32();
	ci->filesize = p->Recv_uint32();

	p->Recv_string(ci->name, lengthof(ci->name));
	p->Recv_string(ci->version, lengthof(ci->version));
	p->Recv_string(ci->url, lengthof(ci->url));
	p->Recv_string(ci->description, lengthof(ci->description), SVS_REPLACE_WITH_QUESTION_MARK | SVS_ALLOW_NEWLINE);

	ci->unique_id = p->Recv_uint32();
	for (uint j = 0; j < sizeof(ci->md5sum); j++) {
		ci->md5sum[j] = p->Recv_uint8();
	}

	ci->dependency_count = p->Recv_uint8();
	ci->dependencies = MallocT<ContentID>(ci->dependency_count);
	for (uint i = 0; i < ci->dependency_count; i++) ci->dependencies[i] = (ContentID)p->Recv_uint32();

	ci->tag_count = p->Recv_uint8();
	ci->tags = MallocT<char[32]>(ci->tag_count);
	for (uint i = 0; i < ci->tag_count; i++) p->Recv_string(ci->tags[i], lengthof(*ci->tags));

	ci->upload_date = p->Recv_uint32();

	if (!ci->IsValid()) {
		delete ci;
		this->Close();
		return false;
	}

	/* Find the appropriate check function */
	HasProc proc = nullptr;
	switch (ci->type) {
		case CONTENT_TYPE_NEWGRF:
			proc = HasGRFConfig;
			break;

		case CONTENT_TYPE_BASE_GRAPHICS:
			proc = BaseGraphics::HasSet;
			break;

		case CONTENT_TYPE_BASE_MUSIC:
			proc = BaseMusic::HasSet;
			break;

		case CONTENT_TYPE_BASE_SOUNDS:
			proc = BaseSounds::HasSet;
			break;

		case CONTENT_TYPE_AI:
			proc = AI::HasAI; break;
			break;

		case CONTENT_TYPE_AI_LIBRARY:
			proc = AI::HasAILibrary; break;
			break;

		case CONTENT_TYPE_GAME:
			proc = Game::HasGame; break;
			break;

		case CONTENT_TYPE_GAME_LIBRARY:
			proc = Game::HasGameLibrary; break;
			break;

		case CONTENT_TYPE_SCENARIO:
		case CONTENT_TYPE_HEIGHTMAP:
			proc = HasScenario;
			break;

		default:
			break;
	}

	if (proc != nullptr) {
		if (proc(ci, true)) {
			ci->state = ContentInfo::ALREADY_HERE;
		} else {
			ci->state = ContentInfo::UNSELECTED;
			if (proc(ci, false)) ci->upgrade = true;
		}
	} else {
		ci->state = ContentInfo::UNSELECTED;
	}

	/* Something we don't have and has filesize 0 does not exist in the system */
	if (ci->state == ContentInfo::UNSELECTED && ci->filesize == 0) ci->state = ContentInfo::DOES_NOT_EXIST;

	/* Do we already have a stub for this? */
	for (ContentInfo *ici : this->infos) {
		if (ici->type == ci->type && ici->unique_id == ci->unique_id &&
				memcmp(ci->md5sum, ici->md5sum, sizeof(ci->md5sum)) == 0) {
			/* Preserve the name if possible */
			if (StrEmpty(ci->name)) strecpy(ci->name, ici->name, lastof(ci->name));
			if (ici->IsSelected()) ci->state = ici->state;

			/*
			 * As ici might be selected by the content window we cannot delete that.
			 * However, we want to keep most of the values of ci, except the values
			 * we (just) already preserved.
			 * So transfer data and ownership of allocated memory from ci to ici.
			 */
			ici->TransferFrom(ci);
			delete ci;

			this->OnReceiveContentInfo(ici);
			return true;
		}
	}

	/* Missing content info? Don't list it */
	if (ci->filesize == 0) {
		delete ci;
		return true;
	}

	this->infos.push_back(ci);

	/* Incoming data means that we might need to reconsider dependencies */
	for (ContentInfo *ici : this->infos) {
		this->CheckDependencyState(ici);
	}

	this->OnReceiveContentInfo(ci);

	return true;
}

/**
 * Request the content list for the given type.
 * @param type The content type to request the list for.
 */
void ClientNetworkContentSocketHandler::RequestContentList(ContentType type)
{
	if (type == CONTENT_TYPE_END) {
		this->RequestContentList(CONTENT_TYPE_BASE_GRAPHICS);
		this->RequestContentList(CONTENT_TYPE_BASE_MUSIC);
		this->RequestContentList(CONTENT_TYPE_BASE_SOUNDS);
		this->RequestContentList(CONTENT_TYPE_SCENARIO);
		this->RequestContentList(CONTENT_TYPE_HEIGHTMAP);
		this->RequestContentList(CONTENT_TYPE_AI);
		this->RequestContentList(CONTENT_TYPE_AI_LIBRARY);
		this->RequestContentList(CONTENT_TYPE_GAME);
		this->RequestContentList(CONTENT_TYPE_GAME_LIBRARY);
		this->RequestContentList(CONTENT_TYPE_NEWGRF);
		return;
	}

	this->Connect();

	Packet *p = new Packet(PACKET_CONTENT_CLIENT_INFO_LIST);
	p->Send_uint8 ((byte)type);
	p->Send_uint32(_openttd_newgrf_version);

	this->SendPacket(p);
}

/**
 * Request the content list for a given number of content IDs.
 * @param count The number of IDs to request.
 * @param content_ids The unique identifiers of the content to request information about.
 */
void ClientNetworkContentSocketHandler::RequestContentList(uint count, const ContentID *content_ids)
{
	this->Connect();

	while (count > 0) {
		/* We can "only" send a limited number of IDs in a single packet.
		 * A packet begins with the packet size and a byte for the type.
		 * Then this packet adds a uint16 for the count in this packet.
		 * The rest of the packet can be used for the IDs. */
		uint p_count = std::min<uint>(count, (TCP_MTU - sizeof(PacketSize) - sizeof(byte) - sizeof(uint16)) / sizeof(uint32));

		Packet *p = new Packet(PACKET_CONTENT_CLIENT_INFO_ID, TCP_MTU);
		p->Send_uint16(p_count);

		for (uint i = 0; i < p_count; i++) {
			p->Send_uint32(content_ids[i]);
		}

		this->SendPacket(p);
		count -= p_count;
		content_ids += p_count;
	}
}

/**
 * Request the content list for a list of content.
 * @param cv List with unique IDs and MD5 checksums.
 * @param send_md5sum Whether we want a MD5 checksum matched set of files or not.
 */
void ClientNetworkContentSocketHandler::RequestContentList(ContentVector *cv, bool send_md5sum)
{
	if (cv == nullptr) return;

	this->Connect();

	assert(cv->size() < 255);
	assert(cv->size() < (TCP_MTU - sizeof(PacketSize) - sizeof(byte) - sizeof(uint8)) /
			(sizeof(uint8) + sizeof(uint32) + (send_md5sum ? /*sizeof(ContentInfo::md5sum)*/16 : 0)));

	Packet *p = new Packet(send_md5sum ? PACKET_CONTENT_CLIENT_INFO_EXTID_MD5 : PACKET_CONTENT_CLIENT_INFO_EXTID, TCP_MTU);
	p->Send_uint8((uint8)cv->size());

	for (const ContentInfo *ci : *cv) {
		p->Send_uint8((byte)ci->type);
		p->Send_uint32(ci->unique_id);
		if (!send_md5sum) continue;

		for (uint j = 0; j < sizeof(ci->md5sum); j++) {
			p->Send_uint8(ci->md5sum[j]);
		}
	}

	this->SendPacket(p);

	for (ContentInfo *ci : *cv) {
		bool found = false;
		for (ContentInfo *ci2 : this->infos) {
			if (ci->type == ci2->type && ci->unique_id == ci2->unique_id &&
					(!send_md5sum || memcmp(ci->md5sum, ci2->md5sum, sizeof(ci->md5sum)) == 0)) {
				found = true;
				break;
			}
		}
		if (!found) {
			this->infos.push_back(ci);
		} else {
			delete ci;
		}
	}
}

/**
 * Actually begin downloading the content we selected.
 * @param[out] files The number of files we are going to download.
 * @param[out] bytes The number of bytes we are going to download.
 * @param fallback Whether to use the fallback or not.
 */
void ClientNetworkContentSocketHandler::DownloadSelectedContent(uint &files, uint &bytes, bool fallback)
{
	bytes = 0;

#ifdef __EMSCRIPTEN__
	/* Emscripten is loaded via an HTTPS connection. As such, it is very
	 * difficult to make HTTP connections. So always use the TCP method of
	 * downloading content. */
	fallback = true;
#endif

	ContentIDList content;
	for (const ContentInfo *ci : this->infos) {
		if (!ci->IsSelected() || ci->state == ContentInfo::ALREADY_HERE) continue;

		content.push_back(ci->id);
		bytes += ci->filesize;
	}

	files = (uint)content.size();

	/* If there's nothing to download, do nothing. */
	if (files == 0) return;

	if (_settings_client.network.no_http_content_downloads || fallback) {
		this->DownloadSelectedContentFallback(content);
	} else {
		this->DownloadSelectedContentHTTP(content);
	}
}

/**
 * Initiate downloading the content over HTTP.
 * @param content The content to download.
 */
void ClientNetworkContentSocketHandler::DownloadSelectedContentHTTP(const ContentIDList &content)
{
	uint count = (uint)content.size();

	/* Allocate memory for the whole request.
	 * Requests are "id\nid\n..." (as strings), so assume the maximum ID,
	 * which is uint32 so 10 characters long. Then the newlines and
	 * multiply that all with the count and then add the '\0'. */
	uint bytes = (10 + 1) * count + 1;
	char *content_request = MallocT<char>(bytes);
	const char *lastof = content_request + bytes - 1;

	char *p = content_request;
	for (const ContentID &id : content) {
		p += seprintf(p, lastof, "%d\n", id);
	}

	this->http_response_index = -1;

	NetworkAddress address(NETWORK_CONTENT_MIRROR_HOST, NETWORK_CONTENT_MIRROR_PORT);
	new NetworkHTTPContentConnecter(address, this, NETWORK_CONTENT_MIRROR_URL, content_request);
	/* NetworkHTTPContentConnecter takes over freeing of content_request! */
}

/**
 * Initiate downloading the content over the fallback protocol.
 * @param content The content to download.
 */
void ClientNetworkContentSocketHandler::DownloadSelectedContentFallback(const ContentIDList &content)
{
	uint count = (uint)content.size();
	const ContentID *content_ids = content.data();
	this->Connect();

	while (count > 0) {
		/* We can "only" send a limited number of IDs in a single packet.
		 * A packet begins with the packet size and a byte for the type.
		 * Then this packet adds a uint16 for the count in this packet.
		 * The rest of the packet can be used for the IDs. */
		uint p_count = std::min<uint>(count, (TCP_MTU - sizeof(PacketSize) - sizeof(byte) - sizeof(uint16)) / sizeof(uint32));

		Packet *p = new Packet(PACKET_CONTENT_CLIENT_CONTENT, TCP_MTU);
		p->Send_uint16(p_count);

		for (uint i = 0; i < p_count; i++) {
			p->Send_uint32(content_ids[i]);
		}

		this->SendPacket(p);
		count -= p_count;
		content_ids += p_count;
	}
}

/**
 * Determine the full filename of a piece of content information
 * @param ci         the information to get the filename from
 * @param compressed should the filename end with .gz?
 * @return a statically allocated buffer with the filename or
 *         nullptr when no filename could be made.
 */
static std::string GetFullFilename(const ContentInfo *ci, bool compressed)
{
	Subdirectory dir = GetContentInfoSubDir(ci->type);
	if (dir == NO_DIRECTORY) return {};

	std::string buf = FioGetDirectory(SP_AUTODOWNLOAD_DIR, dir);
	buf += ci->filename;
	buf += compressed ? ".tar.gz" : ".tar";

	return buf;
}

/**
 * Gunzip a given file and remove the .gz if successful.
 * @param ci container with filename
 * @return true if the gunzip completed
 */
static bool GunzipFile(const ContentInfo *ci)
{
#if defined(WITH_ZLIB)
	bool ret = true;

	/* Need to open the file with fopen() to support non-ASCII on Windows. */
	FILE *ftmp = fopen(GetFullFilename(ci, true).c_str(), "rb");
	if (ftmp == nullptr) return false;
	/* Duplicate the handle, and close the FILE*, to avoid double-closing the handle later. */
	gzFile fin = gzdopen(dup(fileno(ftmp)), "rb");
	fclose(ftmp);

	FILE *fout = fopen(GetFullFilename(ci, false).c_str(), "wb");

	if (fin == nullptr || fout == nullptr) {
		ret = false;
	} else {
		byte buff[8192];
		for (;;) {
			int read = gzread(fin, buff, sizeof(buff));
			if (read == 0) {
				/* If gzread() returns 0, either the end-of-file has been
				 * reached or an underlying read error has occurred.
				 *
				 * gzeof() can't be used, because:
				 * 1.2.5 - it is safe, 1 means 'everything was OK'
				 * 1.2.3.5, 1.2.4 - 0 or 1 is returned 'randomly'
				 * 1.2.3.3 - 1 is returned for truncated archive
				 *
				 * So we use gzerror(). When proper end of archive
				 * has been reached, then:
				 * errnum == Z_STREAM_END in 1.2.3.3,
				 * errnum == 0 in 1.2.4 and 1.2.5 */
				int errnum;
				gzerror(fin, &errnum);
				if (errnum != 0 && errnum != Z_STREAM_END) ret = false;
				break;
			}
			if (read < 0 || (size_t)read != fwrite(buff, 1, read, fout)) {
				/* If gzread() returns -1, there was an error in archive */
				ret = false;
				break;
			}
			/* DO NOT DO THIS! It will fail to detect broken archive with 1.2.3.3!
			 * if (read < sizeof(buff)) break; */
		}
	}

	if (fin != nullptr) gzclose(fin);
	if (fout != nullptr) fclose(fout);

	return ret;
#else
	NOT_REACHED();
#endif /* defined(WITH_ZLIB) */
}

/**
 * Simple wrapper around fwrite to be able to pass it to Packet's TransferOut.
 * @param file   The file to write data to.
 * @param buffer The buffer to write to the file.
 * @param amount The number of bytes to write.
 * @return The number of bytes that were written.
 */
static inline ssize_t TransferOutFWrite(FILE *file, const char *buffer, size_t amount)
{
	return fwrite(buffer, 1, amount, file);
}

bool ClientNetworkContentSocketHandler::Receive_SERVER_CONTENT(Packet *p)
{
	if (this->curFile == nullptr) {
		delete this->curInfo;
		/* When we haven't opened a file this must be our first packet with metadata. */
		this->curInfo = new ContentInfo;
		this->curInfo->type     = (ContentType)p->Recv_uint8();
		this->curInfo->id       = (ContentID)p->Recv_uint32();
		this->curInfo->filesize = p->Recv_uint32();
		p->Recv_string(this->curInfo->filename, lengthof(this->curInfo->filename));

		if (!this->BeforeDownload()) {
			this->Close();
			return false;
		}
	} else {
		/* We have a file opened, thus are downloading internal content */
		size_t toRead = p->RemainingBytesToTransfer();
		if (toRead != 0 && (size_t)p->TransferOut(TransferOutFWrite, this->curFile) != toRead) {
			DeleteWindowById(WC_NETWORK_STATUS_WINDOW, WN_NETWORK_STATUS_WINDOW_CONTENT_DOWNLOAD);
			ShowErrorMessage(STR_CONTENT_ERROR_COULD_NOT_DOWNLOAD, STR_CONTENT_ERROR_COULD_NOT_DOWNLOAD_FILE_NOT_WRITABLE, WL_ERROR);
			this->Close();
			fclose(this->curFile);
			this->curFile = nullptr;

			return false;
		}

		this->OnDownloadProgress(this->curInfo, (int)toRead);

		if (toRead == 0) this->AfterDownload();
	}

	return true;
}

/**
 * Handle the opening of the file before downloading.
 * @return false on any error.
 */
bool ClientNetworkContentSocketHandler::BeforeDownload()
{
	if (!this->curInfo->IsValid()) {
		delete this->curInfo;
		this->curInfo = nullptr;
		return false;
	}

	if (this->curInfo->filesize != 0) {
		/* The filesize is > 0, so we are going to download it */
		std::string filename = GetFullFilename(this->curInfo, true);
		if (filename.empty() || (this->curFile = fopen(filename.c_str(), "wb")) == nullptr) {
			/* Unless that fails of course... */
			DeleteWindowById(WC_NETWORK_STATUS_WINDOW, WN_NETWORK_STATUS_WINDOW_CONTENT_DOWNLOAD);
			ShowErrorMessage(STR_CONTENT_ERROR_COULD_NOT_DOWNLOAD, STR_CONTENT_ERROR_COULD_NOT_DOWNLOAD_FILE_NOT_WRITABLE, WL_ERROR);
			return false;
		}
	}
	return true;
}

/**
 * Handle the closing and extracting of a file after
 * downloading it has been done.
 */
void ClientNetworkContentSocketHandler::AfterDownload()
{
	/* We read nothing; that's our marker for end-of-stream.
	 * Now gunzip the tar and make it known. */
	fclose(this->curFile);
	this->curFile = nullptr;

	if (GunzipFile(this->curInfo)) {
		unlink(GetFullFilename(this->curInfo, true).c_str());

		Subdirectory sd = GetContentInfoSubDir(this->curInfo->type);
		if (sd == NO_DIRECTORY) NOT_REACHED();

		TarScanner ts;
		std::string fname = GetFullFilename(this->curInfo, false);
		ts.AddFile(sd, fname);

		if (this->curInfo->type == CONTENT_TYPE_BASE_MUSIC) {
			/* Music can't be in a tar. So extract the tar! */
			ExtractTar(fname, BASESET_DIR);
			unlink(fname.c_str());
		}

#ifdef __EMSCRIPTEN__
		EM_ASM(if (window["openttd_syncfs"]) openttd_syncfs());
#endif

		this->OnDownloadComplete(this->curInfo->id);
	} else {
		ShowErrorMessage(STR_CONTENT_ERROR_COULD_NOT_EXTRACT, INVALID_STRING_ID, WL_ERROR);
	}
}

/* Also called to just clean up the mess. */
void ClientNetworkContentSocketHandler::OnFailure()
{
	/* If we fail, download the rest via the 'old' system. */
	uint files, bytes;
	this->DownloadSelectedContent(files, bytes, true);

	this->http_response.clear();
	this->http_response.shrink_to_fit();
	this->http_response_index = -2;

	if (this->curFile != nullptr) {
		/* Revert the download progress when we are going for the old system. */
		long size = ftell(this->curFile);
		if (size > 0) this->OnDownloadProgress(this->curInfo, (int)-size);

		fclose(this->curFile);
		this->curFile = nullptr;
	}
}

void ClientNetworkContentSocketHandler::OnReceiveData(const char *data, size_t length)
{
	assert(data == nullptr || length != 0);

	/* Ignore any latent data coming from a connection we closed. */
	if (this->http_response_index == -2) return;

	if (this->http_response_index == -1) {
		if (data != nullptr) {
			/* Append the rest of the response. */
			this->http_response.insert(this->http_response.end(), data, data + length);
			return;
		} else {
			/* Make sure the response is properly terminated. */
			this->http_response.push_back('\0');

			/* And prepare for receiving the rest of the data. */
			this->http_response_index = 0;
		}
	}

	if (data != nullptr) {
		/* We have data, so write it to the file. */
		if (fwrite(data, 1, length, this->curFile) != length) {
			/* Writing failed somehow, let try via the old method. */
			this->OnFailure();
		} else {
			/* Just received the data. */
			this->OnDownloadProgress(this->curInfo, (int)length);
		}
		/* Nothing more to do now. */
		return;
	}

	if (this->curFile != nullptr) {
		/* We've finished downloading a file. */
		this->AfterDownload();
	}

	if ((uint)this->http_response_index >= this->http_response.size()) {
		/* It's not a real failure, but if there's
		 * nothing more to download it helps with
		 * cleaning up the stuff we allocated. */
		this->OnFailure();
		return;
	}

	delete this->curInfo;
	/* When we haven't opened a file this must be our first packet with metadata. */
	this->curInfo = new ContentInfo;

/** Check p for not being null and return calling OnFailure if that's not the case. */
#define check_not_null(p) { if ((p) == nullptr) { this->OnFailure(); return; } }
/** Check p for not being null and then terminate, or return calling OnFailure. */
#define check_and_terminate(p) { check_not_null(p); *(p) = '\0'; }

	for (;;) {
		char *str = this->http_response.data() + this->http_response_index;
		char *p = strchr(str, '\n');
		check_and_terminate(p);

		/* Update the index for the next one */
		this->http_response_index += (int)strlen(str) + 1;

		/* Read the ID */
		p = strchr(str, ',');
		check_and_terminate(p);
		this->curInfo->id = (ContentID)atoi(str);

		/* Read the type */
		str = p + 1;
		p = strchr(str, ',');
		check_and_terminate(p);
		this->curInfo->type = (ContentType)atoi(str);

		/* Read the file size */
		str = p + 1;
		p = strchr(str, ',');
		check_and_terminate(p);
		this->curInfo->filesize = atoi(str);

		/* Read the URL */
		str = p + 1;
		/* Is it a fallback URL? If so, just continue with the next one. */
		if (strncmp(str, "ottd", 4) == 0) {
			if ((uint)this->http_response_index >= this->http_response.size()) {
				/* Have we gone through all lines? */
				this->OnFailure();
				return;
			}
			continue;
		}

		p = strrchr(str, '/');
		check_not_null(p);
		p++; // Start after the '/'

		char tmp[MAX_PATH];
		if (strecpy(tmp, p, lastof(tmp)) == lastof(tmp)) {
			this->OnFailure();
			return;
		}
		/* Remove the extension from the string. */
		for (uint i = 0; i < 2; i++) {
			p = strrchr(tmp, '.');
			check_and_terminate(p);
		}

		/* Copy the string, without extension, to the filename. */
		strecpy(this->curInfo->filename, tmp, lastof(this->curInfo->filename));

		/* Request the next file. */
		if (!this->BeforeDownload()) {
			this->OnFailure();
			return;
		}

		NetworkHTTPSocketHandler::Connect(str, this);
		return;
	}

#undef check
#undef check_and_terminate
}

/**
 * Create a socket handler to handle the connection.
 */
ClientNetworkContentSocketHandler::ClientNetworkContentSocketHandler() :
	NetworkContentSocketHandler(),
	http_response_index(-2),
	curFile(nullptr),
	curInfo(nullptr),
	isConnecting(false)
{
	this->lastActivity = std::chrono::steady_clock::now();
}

/** Clear up the mess ;) */
ClientNetworkContentSocketHandler::~ClientNetworkContentSocketHandler()
{
	delete this->curInfo;
	if (this->curFile != nullptr) fclose(this->curFile);

	for (ContentInfo *ci : this->infos) delete ci;
}

/** Connect to the content server. */
class NetworkContentConnecter : TCPConnecter {
public:
	/**
	 * Initiate the connecting.
	 * @param address The address of the server.
	 */
	NetworkContentConnecter(const NetworkAddress &address) : TCPConnecter(address) {}

	void OnFailure() override
	{
		_network_content_client.isConnecting = false;
		_network_content_client.OnConnect(false);
	}

	void OnConnect(SOCKET s) override
	{
		assert(_network_content_client.sock == INVALID_SOCKET);
		_network_content_client.lastActivity = std::chrono::steady_clock::now();
		_network_content_client.isConnecting = false;
		_network_content_client.sock = s;
		_network_content_client.Reopen();
		_network_content_client.OnConnect(true);
	}
};

/**
 * Connect with the content server.
 */
void ClientNetworkContentSocketHandler::Connect()
{
	if (this->sock != INVALID_SOCKET || this->isConnecting) return;
	this->isConnecting = true;
	new NetworkContentConnecter(NetworkAddress(NETWORK_CONTENT_SERVER_HOST, NETWORK_CONTENT_SERVER_PORT, AF_UNSPEC));
}

/**
 * Disconnect from the content server.
 */
void ClientNetworkContentSocketHandler::Close()
{
	if (this->sock == INVALID_SOCKET) return;
	NetworkContentSocketHandler::Close();

	this->OnDisconnect();
}

/**
 * Check whether we received/can send some data from/to the content server and
 * when that's the case handle it appropriately
 */
void ClientNetworkContentSocketHandler::SendReceive()
{
	if (this->sock == INVALID_SOCKET || this->isConnecting) return;

	if (std::chrono::steady_clock::now() > this->lastActivity + IDLE_TIMEOUT) {
		this->Close();
		return;
	}

	if (this->CanSendReceive()) {
		if (this->ReceivePackets()) {
			/* Only update activity once a packet is received, instead of every time we try it. */
			this->lastActivity = std::chrono::steady_clock::now();
		}
	}

	this->SendPackets();
}

/**
 * Download information of a given Content ID if not already tried
 * @param cid the ID to try
 */
void ClientNetworkContentSocketHandler::DownloadContentInfo(ContentID cid)
{
	/* When we tried to download it already, don't try again */
	if (std::find(this->requested.begin(), this->requested.end(), cid) != this->requested.end()) return;

	this->requested.push_back(cid);
	this->RequestContentList(1, &cid);
}

/**
 * Get the content info based on a ContentID
 * @param cid the ContentID to search for
 * @return the ContentInfo or nullptr if not found
 */
ContentInfo *ClientNetworkContentSocketHandler::GetContent(ContentID cid)
{
	for (ContentInfo *ci : this->infos) {
		if (ci->id == cid) return ci;
	}
	return nullptr;
}


/**
 * Select a specific content id.
 * @param cid the content ID to select
 */
void ClientNetworkContentSocketHandler::Select(ContentID cid)
{
	ContentInfo *ci = this->GetContent(cid);
	if (ci == nullptr || ci->state != ContentInfo::UNSELECTED) return;

	ci->state = ContentInfo::SELECTED;
	this->CheckDependencyState(ci);
}

/**
 * Unselect a specific content id.
 * @param cid the content ID to deselect
 */
void ClientNetworkContentSocketHandler::Unselect(ContentID cid)
{
	ContentInfo *ci = this->GetContent(cid);
	if (ci == nullptr || !ci->IsSelected()) return;

	ci->state = ContentInfo::UNSELECTED;
	this->CheckDependencyState(ci);
}

/** Select everything we can select */
void ClientNetworkContentSocketHandler::SelectAll()
{
	for (ContentInfo *ci : this->infos) {
		if (ci->state == ContentInfo::UNSELECTED) {
			ci->state = ContentInfo::SELECTED;
			this->CheckDependencyState(ci);
		}
	}
}

/** Select everything that's an update for something we've got */
void ClientNetworkContentSocketHandler::SelectUpgrade()
{
	for (ContentInfo *ci : this->infos) {
		if (ci->state == ContentInfo::UNSELECTED && ci->upgrade) {
			ci->state = ContentInfo::SELECTED;
			this->CheckDependencyState(ci);
		}
	}
}

/** Unselect everything that we've not downloaded so far. */
void ClientNetworkContentSocketHandler::UnselectAll()
{
	for (ContentInfo *ci : this->infos) {
		if (ci->IsSelected() && ci->state != ContentInfo::ALREADY_HERE) ci->state = ContentInfo::UNSELECTED;
	}
}

/** Toggle the state of a content info and check its dependencies */
void ClientNetworkContentSocketHandler::ToggleSelectedState(const ContentInfo *ci)
{
	switch (ci->state) {
		case ContentInfo::SELECTED:
		case ContentInfo::AUTOSELECTED:
			this->Unselect(ci->id);
			break;

		case ContentInfo::UNSELECTED:
			this->Select(ci->id);
			break;

		default:
			break;
	}
}

/**
 * Reverse lookup the dependencies of (direct) parents over a given child.
 * @param parents list to store all parents in (is not cleared)
 * @param child   the child to search the parents' dependencies for
 */
void ClientNetworkContentSocketHandler::ReverseLookupDependency(ConstContentVector &parents, const ContentInfo *child) const
{
	for (const ContentInfo *ci : this->infos) {
		if (ci == child) continue;

		for (uint i = 0; i < ci->dependency_count; i++) {
			if (ci->dependencies[i] == child->id) {
				parents.push_back(ci);
				break;
			}
		}
	}
}

/**
 * Reverse lookup the dependencies of all parents over a given child.
 * @param tree  list to store all parents in (is not cleared)
 * @param child the child to search the parents' dependencies for
 */
void ClientNetworkContentSocketHandler::ReverseLookupTreeDependency(ConstContentVector &tree, const ContentInfo *child) const
{
	tree.push_back(child);

	/* First find all direct parents. We can't use the "normal" iterator as
	 * we are including stuff into the vector and as such the vector's data
	 * store can be reallocated (and thus move), which means out iterating
	 * pointer gets invalid. So fall back to the indices. */
	for (uint i = 0; i < tree.size(); i++) {
		ConstContentVector parents;
		this->ReverseLookupDependency(parents, tree[i]);

		for (const ContentInfo *ci : parents) {
			include(tree, ci);
		}
	}
}

/**
 * Check the dependencies (recursively) of this content info
 * @param ci the content info to check the dependencies of
 */
void ClientNetworkContentSocketHandler::CheckDependencyState(ContentInfo *ci)
{
	if (ci->IsSelected() || ci->state == ContentInfo::ALREADY_HERE) {
		/* Selection is easy; just walk all children and set the
		 * autoselected state. That way we can see what we automatically
		 * selected and thus can unselect when a dependency is removed. */
		for (uint i = 0; i < ci->dependency_count; i++) {
			ContentInfo *c = this->GetContent(ci->dependencies[i]);
			if (c == nullptr) {
				this->DownloadContentInfo(ci->dependencies[i]);
			} else if (c->state == ContentInfo::UNSELECTED) {
				c->state = ContentInfo::AUTOSELECTED;
				this->CheckDependencyState(c);
			}
		}
		return;
	}

	if (ci->state != ContentInfo::UNSELECTED) return;

	/* For unselection we need to find the parents of us. We need to
	 * unselect them. After that we unselect all children that we
	 * depend on and are not used as dependency for us, but only when
	 * we automatically selected them. */
	ConstContentVector parents;
	this->ReverseLookupDependency(parents, ci);
	for (const ContentInfo *c : parents) {
		if (!c->IsSelected()) continue;

		this->Unselect(c->id);
	}

	for (uint i = 0; i < ci->dependency_count; i++) {
		const ContentInfo *c = this->GetContent(ci->dependencies[i]);
		if (c == nullptr) {
			DownloadContentInfo(ci->dependencies[i]);
			continue;
		}
		if (c->state != ContentInfo::AUTOSELECTED) continue;

		/* Only unselect when WE are the only parent. */
		parents.clear();
		this->ReverseLookupDependency(parents, c);

		/* First check whether anything depends on us */
		int sel_count = 0;
		bool force_selection = false;
		for (const ContentInfo *ci : parents) {
			if (ci->IsSelected()) sel_count++;
			if (ci->state == ContentInfo::SELECTED) force_selection = true;
		}
		if (sel_count == 0) {
			/* Nothing depends on us */
			this->Unselect(c->id);
			continue;
		}
		/* Something manually selected depends directly on us */
		if (force_selection) continue;

		/* "Flood" search to find all items in the dependency graph*/
		parents.clear();
		this->ReverseLookupTreeDependency(parents, c);

		/* Is there anything that is "force" selected?, if so... we're done. */
		for (const ContentInfo *ci : parents) {
			if (ci->state != ContentInfo::SELECTED) continue;

			force_selection = true;
			break;
		}

		/* So something depended directly on us */
		if (force_selection) continue;

		/* Nothing depends on us, mark the whole graph as unselected.
		 * After that's done run over them once again to test their children
		 * to unselect. Don't do it immediately because it'll do exactly what
		 * we're doing now. */
		for (const ContentInfo *c : parents) {
			if (c->state == ContentInfo::AUTOSELECTED) this->Unselect(c->id);
		}
		for (const ContentInfo *c : parents) {
			this->CheckDependencyState(this->GetContent(c->id));
		}
	}
}

/** Clear all downloaded content information. */
void ClientNetworkContentSocketHandler::Clear()
{
	for (ContentInfo *c : this->infos) delete c;

	this->infos.clear();
	this->requested.clear();
}

/*** CALLBACK ***/

void ClientNetworkContentSocketHandler::OnConnect(bool success)
{
	for (size_t i = 0; i < this->callbacks.size(); /* nothing */) {
		ContentCallback *cb = this->callbacks[i];
		/* the callback may remove itself from this->callbacks */
		cb->OnConnect(success);
		if (i != this->callbacks.size() && this->callbacks[i] == cb) i++;
	}
}

void ClientNetworkContentSocketHandler::OnDisconnect()
{
	for (size_t i = 0; i < this->callbacks.size(); /* nothing */) {
		ContentCallback *cb = this->callbacks[i];
		cb->OnDisconnect();
		if (i != this->callbacks.size() && this->callbacks[i] == cb) i++;
	}
}

void ClientNetworkContentSocketHandler::OnReceiveContentInfo(const ContentInfo *ci)
{
	for (size_t i = 0; i < this->callbacks.size(); /* nothing */) {
		ContentCallback *cb = this->callbacks[i];
		/* the callback may add items and/or remove itself from this->callbacks */
		cb->OnReceiveContentInfo(ci);
		if (i != this->callbacks.size() && this->callbacks[i] == cb) i++;
	}
}

void ClientNetworkContentSocketHandler::OnDownloadProgress(const ContentInfo *ci, int bytes)
{
	for (size_t i = 0; i < this->callbacks.size(); /* nothing */) {
		ContentCallback *cb = this->callbacks[i];
		cb->OnDownloadProgress(ci, bytes);
		if (i != this->callbacks.size() && this->callbacks[i] == cb) i++;
	}
}

void ClientNetworkContentSocketHandler::OnDownloadComplete(ContentID cid)
{
	ContentInfo *ci = this->GetContent(cid);
	if (ci != nullptr) {
		ci->state = ContentInfo::ALREADY_HERE;
	}

	for (size_t i = 0; i < this->callbacks.size(); /* nothing */) {
		ContentCallback *cb = this->callbacks[i];
		/* the callback may remove itself from this->callbacks */
		cb->OnDownloadComplete(cid);
		if (i != this->callbacks.size() && this->callbacks[i] == cb) i++;
	}
}
