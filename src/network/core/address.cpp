/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file core/address.cpp Implementation of the address. */

#include "../../stdafx.h"

#include "address.h"
#include "../network_internal.h"
#include "../../debug.h"

#include "../../safeguards.h"

/**
 * Get the hostname; in case it wasn't given the
 * IPv4 dotted representation is given.
 * @return the hostname
 */
const std::string &NetworkAddress::GetHostname()
{
	if (this->hostname.empty() && this->address.ss_family != AF_UNSPEC) {
		assert(this->address_length != 0);
		char buffer[NETWORK_HOSTNAME_LENGTH];
		getnameinfo((struct sockaddr *)&this->address, this->address_length, buffer, sizeof(buffer), nullptr, 0, NI_NUMERICHOST);
		this->hostname = buffer;
	}
	return this->hostname;
}

/**
 * Get the port.
 * @return the port.
 */
uint16 NetworkAddress::GetPort() const
{
	switch (this->address.ss_family) {
		case AF_UNSPEC:
		case AF_INET:
			return ntohs(((const struct sockaddr_in *)&this->address)->sin_port);

		case AF_INET6:
			return ntohs(((const struct sockaddr_in6 *)&this->address)->sin6_port);

		default:
			NOT_REACHED();
	}
}

/**
 * Set the port.
 * @param port set the port number.
 */
void NetworkAddress::SetPort(uint16 port)
{
	switch (this->address.ss_family) {
		case AF_UNSPEC:
		case AF_INET:
			((struct sockaddr_in*)&this->address)->sin_port = htons(port);
			break;

		case AF_INET6:
			((struct sockaddr_in6*)&this->address)->sin6_port = htons(port);
			break;

		default:
			NOT_REACHED();
	}
}

/**
 * Helper to get the formatting string of an address for a given family.
 * @param family The family to get the address format for.
 * @param with_family Whether to add the familty to the address (e.g. IPv4).
 * @return The format string for the address.
 */
static const char *GetAddressFormatString(uint16 family, bool with_family)
{
	switch (family) {
		case AF_INET: return with_family ? "{}:{} (IPv4)" : "{}:{}";
		case AF_INET6: return with_family ? "[{}]:{} (IPv6)" : "[{}]:{}";
		default: return with_family ? "{}:{} (IPv?)" : "{}:{}";
	}
}

/**
 * Get the address as a string, e.g. 127.0.0.1:12345.
 * @param with_family whether to add the family (e.g. IPvX).
 * @return the address
 */
std::string NetworkAddress::GetAddressAsString(bool with_family)
{
	return fmt::format(GetAddressFormatString(this->GetAddress()->ss_family, with_family), this->GetHostname(), this->GetPort());
}

/**
 * Helper function to resolve without opening a socket.
 * @param runp information about the socket to try not
 * @return the opened socket or INVALID_SOCKET
 */
static SOCKET ResolveLoopProc(addrinfo *runp)
{
	/* We just want the first 'entry', so return a valid socket. */
	return !INVALID_SOCKET;
}

/**
 * Get the address in its internal representation.
 * @return the address
 */
const sockaddr_storage *NetworkAddress::GetAddress()
{
	if (!this->IsResolved()) {
		/* Here we try to resolve a network address. We use SOCK_STREAM as
		 * socket type because some stupid OSes, like Solaris, cannot be
		 * bothered to implement the specifications and allow '0' as value
		 * that means "don't care whether it is SOCK_STREAM or SOCK_DGRAM".
		 */
		this->Resolve(this->address.ss_family, SOCK_STREAM, AI_ADDRCONFIG, nullptr, ResolveLoopProc);
		this->resolved = true;
	}
	return &this->address;
}

/**
 * Checks of this address is of the given family.
 * @param family the family to check against
 * @return true if it is of the given family
 */
bool NetworkAddress::IsFamily(int family)
{
	if (!this->IsResolved()) {
		this->Resolve(family, SOCK_STREAM, AI_ADDRCONFIG, nullptr, ResolveLoopProc);
	}
	return this->address.ss_family == family;
}

/**
 * Checks whether this IP address is contained by the given netmask.
 * @param netmask the netmask in CIDR notation to test against.
 * @note netmask without /n assumes all bits need to match.
 * @return true if this IP is within the netmask.
 */
bool NetworkAddress::IsInNetmask(const std::string &netmask)
{
	/* Resolve it if we didn't do it already */
	if (!this->IsResolved()) this->GetAddress();

	int cidr = this->address.ss_family == AF_INET ? 32 : 128;

	NetworkAddress mask_address;

	/* Check for CIDR separator */
	auto cidr_separator_location = netmask.find('/');
	if (cidr_separator_location != std::string::npos) {
		int tmp_cidr = atoi(netmask.substr(cidr_separator_location + 1).c_str());

		/* Invalid CIDR, treat as single host */
		if (tmp_cidr > 0 && tmp_cidr < cidr) cidr = tmp_cidr;

		/* Remove the / so that NetworkAddress works on the IP portion */
		mask_address = NetworkAddress(netmask.substr(0, cidr_separator_location), 0, this->address.ss_family);
	} else {
		mask_address = NetworkAddress(netmask, 0, this->address.ss_family);
	}

	if (mask_address.GetAddressLength() == 0) return false;

	uint32 *ip;
	uint32 *mask;
	switch (this->address.ss_family) {
		case AF_INET:
			ip = (uint32*)&((struct sockaddr_in*)&this->address)->sin_addr.s_addr;
			mask = (uint32*)&((struct sockaddr_in*)&mask_address.address)->sin_addr.s_addr;
			break;

		case AF_INET6:
			ip = (uint32*)&((struct sockaddr_in6*)&this->address)->sin6_addr;
			mask = (uint32*)&((struct sockaddr_in6*)&mask_address.address)->sin6_addr;
			break;

		default:
			NOT_REACHED();
	}

	while (cidr > 0) {
		uint32 msk = cidr >= 32 ? (uint32)-1 : htonl(-(1 << (32 - cidr)));
		if ((*mask++ & msk) != (*ip++ & msk)) return false;

		cidr -= 32;
	}

	return true;
}

/**
 * Resolve this address into a socket
 * @param family the type of 'protocol' (IPv4, IPv6)
 * @param socktype the type of socket (TCP, UDP, etc)
 * @param flags the flags to send to getaddrinfo
 * @param sockets the list of sockets to add the sockets to
 * @param func the inner working while looping over the address info
 * @return the resolved socket or INVALID_SOCKET.
 */
SOCKET NetworkAddress::Resolve(int family, int socktype, int flags, SocketList *sockets, LoopProc func)
{
	struct addrinfo *ai;
	struct addrinfo hints;
	memset(&hints, 0, sizeof (hints));
	hints.ai_family   = family;
	hints.ai_flags    = flags;
	hints.ai_socktype = socktype;

	/* The port needs to be a string. Six is enough to contain all characters + '\0'. */
	char port_name[6];
	seprintf(port_name, lastof(port_name), "%u", this->GetPort());

	bool reset_hostname = false;
	/* Setting both hostname to nullptr and port to 0 is not allowed.
	 * As port 0 means bind to any port, the other must mean that
	 * we want to bind to 'all' IPs. */
	if (this->hostname.empty() && this->address_length == 0 && this->GetPort() == 0) {
		reset_hostname = true;
		int fam = this->address.ss_family;
		if (fam == AF_UNSPEC) fam = family;
		this->hostname = fam == AF_INET ? "0.0.0.0" : "::";
	}

	static bool _resolve_timeout_error_message_shown = false;
	auto start = std::chrono::steady_clock::now();
	int e = getaddrinfo(this->hostname.empty() ? nullptr : this->hostname.c_str(), port_name, &hints, &ai);
	auto end = std::chrono::steady_clock::now();
	std::chrono::seconds duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
	if (!_resolve_timeout_error_message_shown && duration >= std::chrono::seconds(5)) {
		Debug(net, 0, "getaddrinfo for hostname \"{}\", port {}, address family {} and socket type {} took {} seconds",
				this->hostname, port_name, AddressFamilyAsString(family), SocketTypeAsString(socktype), duration.count());
		Debug(net, 0, "  this is likely an issue in the DNS name resolver's configuration causing it to time out");
		_resolve_timeout_error_message_shown = true;
	}


	if (reset_hostname) this->hostname.clear();

	if (e != 0) {
		if (func != ResolveLoopProc) {
			Debug(net, 0, "getaddrinfo for hostname \"{}\", port {}, address family {} and socket type {} failed: {}",
				this->hostname, port_name, AddressFamilyAsString(family), SocketTypeAsString(socktype), FS2OTTD(gai_strerror(e)));
		}
		return INVALID_SOCKET;
	}

	SOCKET sock = INVALID_SOCKET;
	for (struct addrinfo *runp = ai; runp != nullptr; runp = runp->ai_next) {
		/* When we are binding to multiple sockets, make sure we do not
		 * connect to one with exactly the same address twice. That's
		 * of course totally unneeded ;) */
		if (sockets != nullptr) {
			NetworkAddress address(runp->ai_addr, (int)runp->ai_addrlen);
			if (sockets->Contains(address)) continue;
		}
		sock = func(runp);
		if (sock == INVALID_SOCKET) continue;

		if (sockets == nullptr) {
			this->address_length = (int)runp->ai_addrlen;
			assert(sizeof(this->address) >= runp->ai_addrlen);
			memcpy(&this->address, runp->ai_addr, runp->ai_addrlen);
#ifdef __EMSCRIPTEN__
			/* Emscripten doesn't zero sin_zero, but as we compare addresses
			 * to see if they are the same address, we need them to be zero'd.
			 * Emscripten is, as far as we know, the only OS not doing this.
			 *
			 * https://github.com/emscripten-core/emscripten/issues/12998
			 */
			if (this->address.ss_family == AF_INET) {
				sockaddr_in *address_ipv4 = (sockaddr_in *)&this->address;
				memset(address_ipv4->sin_zero, 0, sizeof(address_ipv4->sin_zero));
			}
#endif
			break;
		}

		NetworkAddress addr(runp->ai_addr, (int)runp->ai_addrlen);
		(*sockets)[addr] = sock;
		sock = INVALID_SOCKET;
	}
	freeaddrinfo (ai);

	return sock;
}

/**
 * Helper function to resolve a listening.
 * @param runp information about the socket to try not
 * @return the opened socket or INVALID_SOCKET
 */
static SOCKET ListenLoopProc(addrinfo *runp)
{
	std::string address = NetworkAddress(runp->ai_addr, (int)runp->ai_addrlen).GetAddressAsString();

	SOCKET sock = socket(runp->ai_family, runp->ai_socktype, runp->ai_protocol);
	if (sock == INVALID_SOCKET) {
		const char *type = NetworkAddress::SocketTypeAsString(runp->ai_socktype);
		const char *family = NetworkAddress::AddressFamilyAsString(runp->ai_family);
		Debug(net, 0, "Could not create {} {} socket: {}", type, family, NetworkError::GetLast().AsString());
		return INVALID_SOCKET;
	}

	if (runp->ai_socktype == SOCK_STREAM && !SetNoDelay(sock)) {
		Debug(net, 1, "Setting no-delay mode failed: {}", NetworkError::GetLast().AsString());
	}

	if (!SetReusePort(sock)) {
		Debug(net, 0, "Setting reuse-address mode failed: {}", NetworkError::GetLast().AsString());
	}

#ifndef __OS2__
	int on = 1;
	if (runp->ai_family == AF_INET6 &&
			setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&on, sizeof(on)) == -1) {
		Debug(net, 3, "Could not disable IPv4 over IPv6: {}", NetworkError::GetLast().AsString());
	}
#endif

	if (bind(sock, runp->ai_addr, (int)runp->ai_addrlen) != 0) {
		Debug(net, 0, "Could not bind socket on {}: {}", address, NetworkError::GetLast().AsString());
		closesocket(sock);
		return INVALID_SOCKET;
	}

	if (runp->ai_socktype != SOCK_DGRAM && listen(sock, 1) != 0) {
		Debug(net, 0, "Could not listen on socket: {}", NetworkError::GetLast().AsString());
		closesocket(sock);
		return INVALID_SOCKET;
	}

	/* Connection succeeded */

	if (!SetNonBlocking(sock)) {
		Debug(net, 0, "Setting non-blocking mode failed: {}", NetworkError::GetLast().AsString());
	}

	Debug(net, 3, "Listening on {}", address);
	return sock;
}

/**
 * Make the given socket listen.
 * @param socktype the type of socket (TCP, UDP, etc)
 * @param sockets the list of sockets to add the sockets to
 */
void NetworkAddress::Listen(int socktype, SocketList *sockets)
{
	assert(sockets != nullptr);

	/* Setting both hostname to "" and port to 0 is not allowed.
	 * As port 0 means bind to any port, the other must mean that
	 * we want to bind to 'all' IPs. */
	if (this->address_length == 0 && this->address.ss_family == AF_UNSPEC &&
			this->hostname.empty() && this->GetPort() == 0) {
		this->Resolve(AF_INET,  socktype, AI_ADDRCONFIG | AI_PASSIVE, sockets, ListenLoopProc);
		this->Resolve(AF_INET6, socktype, AI_ADDRCONFIG | AI_PASSIVE, sockets, ListenLoopProc);
	} else {
		this->Resolve(AF_UNSPEC, socktype, AI_ADDRCONFIG | AI_PASSIVE, sockets, ListenLoopProc);
	}
}

/**
 * Convert the socket type into a string
 * @param socktype the socket type to convert
 * @return the string representation
 * @note only works for SOCK_STREAM and SOCK_DGRAM
 */
/* static */ const char *NetworkAddress::SocketTypeAsString(int socktype)
{
	switch (socktype) {
		case SOCK_STREAM: return "tcp";
		case SOCK_DGRAM:  return "udp";
		default:          return "unsupported";
	}
}

/**
 * Convert the address family into a string
 * @param family the family to convert
 * @return the string representation
 * @note only works for AF_INET, AF_INET6 and AF_UNSPEC
 */
/* static */ const char *NetworkAddress::AddressFamilyAsString(int family)
{
	switch (family) {
		case AF_UNSPEC: return "either IPv4 or IPv6";
		case AF_INET:   return "IPv4";
		case AF_INET6:  return "IPv6";
		default:        return "unsupported";
	}
}

/**
 * Get the peer address of a socket as NetworkAddress.
 * @param sock The socket to get the peer address of.
 * @return The NetworkAddress of the peer address.
 */
/* static */ NetworkAddress NetworkAddress::GetPeerAddress(SOCKET sock)
{
	sockaddr_storage addr = {};
	socklen_t addr_len = sizeof(addr);
	if (getpeername(sock, (sockaddr *)&addr, &addr_len) != 0) {
		Debug(net, 0, "Failed to get address of the peer: {}", NetworkError::GetLast().AsString());
		return NetworkAddress();
	}
	return NetworkAddress(addr, addr_len);
}

/**
 * Get the local address of a socket as NetworkAddress.
 * @param sock The socket to get the local address of.
 * @return The NetworkAddress of the local address.
 */
/* static */ NetworkAddress NetworkAddress::GetSockAddress(SOCKET sock)
{
	sockaddr_storage addr = {};
	socklen_t addr_len = sizeof(addr);
	if (getsockname(sock, (sockaddr *)&addr, &addr_len) != 0) {
		Debug(net, 0, "Failed to get address of the socket: {}", NetworkError::GetLast().AsString());
		return NetworkAddress();
	}
	return NetworkAddress(addr, addr_len);
}

/**
 * Get the peer name of a socket in string format.
 * @param sock The socket to get the peer name of.
 * @return The string representation of the peer name.
 */
/* static */ const std::string NetworkAddress::GetPeerName(SOCKET sock)
{
	return NetworkAddress::GetPeerAddress(sock).GetAddressAsString();
}

/**
 * Convert a string containing either "hostname" or "hostname:ip" to a
 * ServerAddress, where the string can be postfixed with "#company" to
 * indicate the requested company.
 *
 * @param connection_string The string to parse.
 * @param default_port The default port to set port to if not in connection_string.
 * @param company Pointer to the company variable to set iff indicted.
 * @return A valid ServerAddress of the parsed information.
 */
/* static */ ServerAddress ServerAddress::Parse(const std::string &connection_string, uint16 default_port, CompanyID *company_id)
{
	if (!connection_string.empty() && connection_string[0] == '+') {
		std::string_view ip = ParseCompanyFromConnectionString(connection_string, company_id);
		return ServerAddress(SERVER_ADDRESS_JOIN_KEY, std::string(ip));
	}

	uint16 port = default_port;
	std::string_view ip = ParseFullConnectionString(connection_string, port, company_id);
	return ServerAddress(SERVER_ADDRESS_DIRECT, std::string(ip) + ":" + std::to_string(port));
}
