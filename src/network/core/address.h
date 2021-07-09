/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file core/address.h Wrapper for network addresses. */

#ifndef NETWORK_CORE_ADDRESS_H
#define NETWORK_CORE_ADDRESS_H

#include "os_abstraction.h"
#include "config.h"
#include "../../company_type.h"
#include "../../string_func.h"
#include "../../core/smallmap_type.hpp"

#include <string>

class NetworkAddress;
typedef std::vector<NetworkAddress> NetworkAddressList; ///< Type for a list of addresses.
typedef SmallMap<NetworkAddress, SOCKET> SocketList;    ///< Type for a mapping between address and socket.

/**
 * Wrapper for (un)resolved network addresses; there's no reason to transform
 * a numeric IP to a string and then back again to pass it to functions. It
 * furthermore allows easier delaying of the hostname lookup.
 */
class NetworkAddress {
private:
	std::string hostname;     ///< The hostname
	int address_length;       ///< The length of the resolved address
	sockaddr_storage address; ///< The resolved address
	bool resolved;            ///< Whether the address has been (tried to be) resolved

	/**
	 * Helper function to resolve something to a socket.
	 * @param runp information about the socket to try not
	 * @return the opened socket or INVALID_SOCKET
	 */
	typedef SOCKET (*LoopProc)(addrinfo *runp);

	SOCKET Resolve(int family, int socktype, int flags, SocketList *sockets, LoopProc func);
public:
	/**
	 * Create a network address based on a resolved IP and port.
	 * @param address The IP address with port.
	 * @param address_length The length of the address.
	 */
	NetworkAddress(struct sockaddr_storage &address, int address_length) :
		address_length(address_length),
		address(address),
		resolved(address_length != 0)
	{
	}

	/**
	 * Create a network address based on a resolved IP and port.
	 * @param address The IP address with port.
	 * @param address_length The length of the address.
	 */
	NetworkAddress(sockaddr *address, int address_length) :
		address_length(address_length),
		resolved(address_length != 0)
	{
		memset(&this->address, 0, sizeof(this->address));
		memcpy(&this->address, address, address_length);
	}

	/**
	 * Create a network address based on a unresolved host and port
	 * @param hostname the unresolved hostname
	 * @param port the port
	 * @param family the address family
	 */
	NetworkAddress(std::string_view hostname = "", uint16 port = 0, int family = AF_UNSPEC) :
		address_length(0),
		resolved(false)
	{
		if (!hostname.empty() && hostname.front() == '[' && hostname.back() == ']') {
			hostname.remove_prefix(1);
			hostname.remove_suffix(1);
		}
		this->hostname = hostname;

		memset(&this->address, 0, sizeof(this->address));
		this->address.ss_family = family;
		this->SetPort(port);
	}

	const std::string &GetHostname();
	std::string GetAddressAsString(bool with_family = true);
	const sockaddr_storage *GetAddress();

	/**
	 * Get the (valid) length of the address.
	 * @return the length
	 */
	int GetAddressLength()
	{
		/* Resolve it if we didn't do it already */
		if (!this->IsResolved()) this->GetAddress();
		return this->address_length;
	}

	uint16 GetPort() const;
	void SetPort(uint16 port);

	/**
	 * Check whether the IP address has been resolved already
	 * @return true iff the port has been resolved
	 */
	bool IsResolved() const
	{
		return this->resolved;
	}

	bool IsFamily(int family);
	bool IsInNetmask(const std::string &netmask);

	/**
	 * Compare the address of this class with the address of another.
	 * @param address the other address.
	 * @return < 0 if address is less, 0 if equal and > 0 if address is more
	 */
	int CompareTo(NetworkAddress &address)
	{
		int r = this->GetAddressLength() - address.GetAddressLength();
		if (r == 0) r = this->address.ss_family - address.address.ss_family;
		if (r == 0) r = memcmp(&this->address, &address.address, this->address_length);
		if (r == 0) r = this->GetPort() - address.GetPort();
		return r;
	}

	/**
	 * Compare the address of this class with the address of another.
	 * @param address the other address.
	 * @return true if both match.
	 */
	bool operator == (NetworkAddress &address)
	{
		return this->CompareTo(address) == 0;
	}

	/**
	 * Compare the address of this class with the address of another.
	 * @param address the other address.
	 * @return true if both match.
	 */
	bool operator == (NetworkAddress &address) const
	{
		return const_cast<NetworkAddress*>(this)->CompareTo(address) == 0;
	}
	/**
	 * Compare the address of this class with the address of another.
	 * @param address the other address.
	 * @return true if both do not match.
	 */
	bool operator != (NetworkAddress address) const
	{
		return const_cast<NetworkAddress*>(this)->CompareTo(address) != 0;
	}

	/**
	 * Compare the address of this class with the address of another.
	 * @param address the other address.
	 */
	bool operator < (NetworkAddress &address)
	{
		return this->CompareTo(address) < 0;
	}

	void Listen(int socktype, SocketList *sockets);

	static const char *SocketTypeAsString(int socktype);
	static const char *AddressFamilyAsString(int family);
	static NetworkAddress GetPeerAddress(SOCKET sock);
	static NetworkAddress GetSockAddress(SOCKET sock);
	static const std::string GetPeerName(SOCKET sock);
};

/**
 * Types of server addresses we know.
 *
 * Sorting will prefer entries at the top of this list above ones at the bottom.
 */
enum ServerAddressType {
	SERVER_ADDRESS_DIRECT,   ///< Server-address is based on an IP:port.
	SERVER_ADDRESS_JOIN_KEY, ///< Server-address is based on a join-key given by the Game Coordinator.
};

/**
 * Address to a game server.
 *
 * This generalises addresses which are based on different identifiers.
 */
class ServerAddress {
private:
	/**
	 * Create a new ServerAddress object.
	 *
	 * Please use ServerAddress::Parse() instead of calling this directly.
	 *
	 * @param type The type of the ServerAdress.
	 * @param connection_string The connection_string that belongs to this ServerAddress type.
	 */
	ServerAddress(ServerAddressType type, const std::string &connection_string) : type(type), connection_string(connection_string) {}

public:
	ServerAddressType type;        ///< The type of this ServerAddress.
	std::string connection_string; ///< The connection string for this ServerAddress.

	static ServerAddress Parse(const std::string &connection_string, uint16 default_port, CompanyID *company_id = nullptr);
};

#endif /* NETWORK_CORE_ADDRESS_H */
