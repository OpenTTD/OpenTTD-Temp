/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file tcp_listen.h Basic functions to listen for TCP connections.
 */

#ifndef NETWORK_CORE_TCP_LISTEN_H
#define NETWORK_CORE_TCP_LISTEN_H

#include "tcp.h"
#include "../network.h"
#include "../../core/pool_type.hpp"
#include "../../debug.h"
#include "table/strings.h"

/**
 * Template for TCP listeners.
 * @param Tsocket      The class we create sockets for.
 * @param Tfull_packet The packet type to return when we don't allow more sockets.
 * @param Tban_packet  The packet type to return when the client is banned.
 */
template <class Tsocket, PacketType Tfull_packet, PacketType Tban_packet>
class TCPListenHandler {
	/** List of sockets we listen on. */
	static SocketList sockets;

public:
	/**
	 * Accepts clients from the sockets.
	 * @param ls Socket to accept clients from.
	 */
	static void AcceptClient(SOCKET ls)
	{
		for (;;) {
			struct sockaddr_storage sin;
			memset(&sin, 0, sizeof(sin));
			socklen_t sin_len = sizeof(sin);
			SOCKET s = accept(ls, (struct sockaddr*)&sin, &sin_len);
			if (s == INVALID_SOCKET) return;
#ifdef __EMSCRIPTEN__
			sin_len = FixAddrLenForEmscripten(sin);
#endif

			SetNonBlocking(s); // XXX error handling?

			NetworkAddress address(sin, sin_len);
			DEBUG(net, 2, "[%s] Client connected from %s on frame %d", Tsocket::GetName(), address.GetHostname(), _frame_counter);

			SetNoDelay(s); // XXX error handling?

			/* Check if the client is banned */
			bool banned = false;
			for (const auto &entry : _network_ban_list) {
				banned = address.IsInNetmask(entry.c_str());
				if (banned) {
					Packet p(Tban_packet);
					p.PrepareToSend();

					DEBUG(net, 1, "[%s] Banned ip tried to join (%s), refused", Tsocket::GetName(), entry.c_str());

					if (p.TransferOut<int>(send, s, 0) < 0) {
						DEBUG(net, 0, "send failed with error %s", NetworkError::GetLast().AsString());
					}
					closesocket(s);
					break;
				}
			}
			/* If this client is banned, continue with next client */
			if (banned) continue;

			/* Can we handle a new client? */
			if (!Tsocket::AllowConnection()) {
				/* no more clients allowed?
				 * Send to the client that we are full! */
				Packet p(Tfull_packet);
				p.PrepareToSend();

				if (p.TransferOut<int>(send, s, 0) < 0) {
					DEBUG(net, 0, "send failed with error %s", NetworkError::GetLast().AsString());
				}
				closesocket(s);

				continue;
			}

			Tsocket::AcceptConnection(s, address);
		}
	}

	/**
	 * Handle the receiving of packets.
	 * @return true if everything went okay.
	 */
	static bool Receive()
	{
		fd_set read_fd, write_fd;
		struct timeval tv;

		FD_ZERO(&read_fd);
		FD_ZERO(&write_fd);


		for (Tsocket *cs : Tsocket::Iterate()) {
			FD_SET(cs->sock, &read_fd);
			FD_SET(cs->sock, &write_fd);
		}

		/* take care of listener port */
		for (auto &s : sockets) {
			FD_SET(s.second, &read_fd);
		}

		tv.tv_sec = tv.tv_usec = 0; // don't block at all.
		if (select(FD_SETSIZE, &read_fd, &write_fd, nullptr, &tv) < 0) return false;

		/* accept clients.. */
		for (auto &s : sockets) {
			if (FD_ISSET(s.second, &read_fd)) AcceptClient(s.second);
		}

		/* read stuff from clients */
		for (Tsocket *cs : Tsocket::Iterate()) {
			cs->writable = !!FD_ISSET(cs->sock, &write_fd);
			if (FD_ISSET(cs->sock, &read_fd)) {
				cs->ReceivePackets();
			}
		}
		return _networking;
	}

	/**
	 * Listen on a particular port.
	 * @param port The port to listen on.
	 * @return true if listening succeeded.
	 */
	static bool Listen(uint16 port)
	{
		assert(sockets.size() == 0);

		NetworkAddressList addresses;
		GetBindAddresses(&addresses, port);

		for (NetworkAddress &address : addresses) {
			address.Listen(SOCK_STREAM, &sockets);
		}

		if (sockets.size() == 0) {
			DEBUG(net, 0, "[server] could not start network: could not create listening socket");
			ShowNetworkError(STR_NETWORK_ERROR_SERVER_START);
			return false;
		}

		return true;
	}

	/** Close the sockets we're listening on. */
	static void CloseListeners()
	{
		for (auto &s : sockets) {
			closesocket(s.second);
		}
		sockets.clear();
		DEBUG(net, 1, "[%s] closed listeners", Tsocket::GetName());
	}
};

template <class Tsocket, PacketType Tfull_packet, PacketType Tban_packet> SocketList TCPListenHandler<Tsocket, Tfull_packet, Tban_packet>::sockets;

#endif /* NETWORK_CORE_TCP_LISTEN_H */
