/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file os_abstraction.cpp OS specific implementations of functions of the OS abstraction layer for network stuff.
 *
 * The general idea is to have simple abstracting functions for things that
 * require different implementations for different environments.
 * In here the functions, and their documentation, are defined only once
 * and the implementation contains the #ifdefs to change the implementation.
 * Since Windows is usually different that is usually the first case, after
 * that the behaviour is usually Unix/BSD-like with occasional variation.
 */

#include "stdafx.h"
#include "os_abstraction.h"
#include "../../string_func.h"
#include <mutex>

#include "../../safeguards.h"

/**
 * Construct the network error with the given error code.
 * @param error The error code.
 */
NetworkError::NetworkError(int error) : error(error)
{
}

/**
 * Check whether this error describes that the operation would block.
 * @return True iff the operation would block.
 */
bool NetworkError::WouldBlock() const
{
#if defined(_WIN32)
	return this->error == WSAEWOULDBLOCK;
#else
	/* Usually EWOULDBLOCK and EAGAIN are the same, but sometimes they are not
	 * and the POSIX.1 specification states that either should be checked. */
	return this->error == EWOULDBLOCK || this->error == EAGAIN;
#endif
}

/**
 * Check whether this error describes a connection reset.
 * @return True iff the connection is reset.
 */
bool NetworkError::IsConnectionReset() const
{
#if defined(_WIN32)
	return this->error == WSAECONNRESET;
#else
	return this->error == ECONNRESET;
#endif
}

/**
 * Check whether this error describes a connect is in progress.
 * @return True iff the connect is already in progress.
 */
bool NetworkError::IsConnectInProgress() const
{
#if defined(_WIN32)
	return this->error == WSAEWOULDBLOCK;
#else
	return this->error == EINPROGRESS;
#endif
}

/**
 * Get the string representation of the error message.
 * @return The string representation that will get overwritten by next calls.
 */
const char *NetworkError::AsString() const
{
	if (this->message.empty()) {
#if defined(_WIN32)
		char buffer[512];
		if (FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, this->error,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buffer, sizeof(buffer), NULL) == 0) {
			seprintf(buffer, lastof(buffer), "Unknown error %d", this->error);
		}
		this->message.assign(buffer);
#else
		/* Make strerror thread safe by locking access to it. There is a thread safe strerror_r, however
		 * the non-POSIX variant is available due to defining _GNU_SOURCE meaning it is not portable.
		 * The problem with the non-POSIX variant is that it does not necessarily fill the buffer with
		 * the error message but can also return a pointer to a static bit of memory, whereas the POSIX
		 * variant always fills the buffer. This makes the behaviour too erratic to work with. */
		static std::mutex mutex;
		std::lock_guard<std::mutex> guard(mutex);
		this->message.assign(strerror(this->error));
#endif
	}
	return this->message.c_str();
}

/**
 * Check whether an error was actually set.
 * @return True iff an error was set.
 */
bool NetworkError::HasError() const
{
	return this->error != 0;
}

/**
 * Get the last network error.
 * @return The network error.
 */
/* static */ NetworkError NetworkError::GetLast()
{
#if defined(_WIN32)
	return NetworkError(WSAGetLastError());
#elif defined(__OS2__)
	return NetworkError(sock_errno());
#else
	return NetworkError(errno);
#endif
}
