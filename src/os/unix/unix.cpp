/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file unix.cpp Implementation of Unix specific file handling. */

#include "../../stdafx.h"
#include "../../textbuf_gui.h"
#include "../../openttd.h"
#include "../../crashlog.h"
#include "../../core/random_func.hpp"
#include "../../debug.h"
#include "../../string_func.h"
#include "../../fios.h"
#include "../../thread.h"


#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <signal.h>

#ifdef WITH_SDL2
#include <SDL.h>
#endif

#ifdef __EMSCRIPTEN__
#	include <emscripten.h>
#endif

#ifdef __APPLE__
#	include <sys/mount.h>
#elif (defined(_POSIX_VERSION) && _POSIX_VERSION >= 200112L) || defined(__GLIBC__)
#	define HAS_STATVFS
#endif

#if defined(OPENBSD) || defined(__NetBSD__) || defined(__FreeBSD__)
#	define HAS_SYSCTL
#endif

#ifdef HAS_STATVFS
#include <sys/statvfs.h>
#endif

#ifdef HAS_SYSCTL
#include <sys/sysctl.h>
#endif

#ifndef NO_THREADS
#include <pthread.h>
#endif

#if defined(__APPLE__)
#	if defined(WITH_SDL)
		/* the mac implementation needs this file included in the same file as main() */
#		include <SDL.h>
#	endif

#	include "../macosx/macos.h"
#endif

#include "../../safeguards.h"

bool FiosIsRoot(const char *path)
{
	return path[1] == '\0';
}

void FiosGetDrives(FileList &file_list)
{
	return;
}

bool FiosGetDiskFreeSpace(const char *path, uint64 *tot)
{
	uint64 free = 0;

#ifdef __APPLE__
	struct statfs s;

	if (statfs(path, &s) != 0) return false;
	free = (uint64)s.f_bsize * s.f_bavail;
#elif defined(HAS_STATVFS)
	struct statvfs s;

	if (statvfs(path, &s) != 0) return false;
	free = (uint64)s.f_frsize * s.f_bavail;
#endif
	if (tot != nullptr) *tot = free;
	return true;
}

bool FiosIsValidFile(const char *path, const struct dirent *ent, struct stat *sb)
{
	char filename[MAX_PATH];
	int res;
	assert(path[strlen(path) - 1] == PATHSEPCHAR);
	if (strlen(path) > 2) assert(path[strlen(path) - 2] != PATHSEPCHAR);
	res = seprintf(filename, lastof(filename), "%s%s", path, ent->d_name);

	/* Could we fully concatenate the path and filename? */
	if (res >= (int)lengthof(filename) || res < 0) return false;

	return stat(filename, sb) == 0;
}

bool FiosIsHiddenFile(const struct dirent *ent)
{
	return ent->d_name[0] == '.';
}

#ifdef WITH_ICONV

#include <iconv.h>
#include <errno.h>
#include "../../debug.h"
#include "../../string_func.h"

const char *GetCurrentLocale(const char *param);

#define INTERNALCODE "UTF-8"

/**
 * Try and try to decipher the current locale from environmental
 * variables. MacOSX is hardcoded, other OS's are dynamic. If no suitable
 * locale can be found, don't do any conversion ""
 */
static const char *GetLocalCode()
{
#if defined(__APPLE__)
	return "UTF-8-MAC";
#else
	/* Strip locale (eg en_US.UTF-8) to only have UTF-8 */
	const char *locale = GetCurrentLocale("LC_CTYPE");
	if (locale != nullptr) locale = strchr(locale, '.');

	return (locale == nullptr) ? "" : locale + 1;
#endif
}

/**
 * Convert between locales, which from and which to is set in the calling
 * functions OTTD2FS() and FS2OTTD().
 */
static const char *convert_tofrom_fs(iconv_t convd, const char *name, char *outbuf, size_t outlen)
{
	/* There are different implementations of iconv. The older ones,
	 * e.g. SUSv2, pass a const pointer, whereas the newer ones, e.g.
	 * IEEE 1003.1 (2004), pass a non-const pointer. */
#ifdef HAVE_NON_CONST_ICONV
	char *inbuf = const_cast<char*>(name);
#else
	const char *inbuf = name;
#endif

	size_t inlen  = strlen(name);
	char *buf = outbuf;

	strecpy(outbuf, name, outbuf + outlen);

	iconv(convd, nullptr, nullptr, nullptr, nullptr);
	if (iconv(convd, &inbuf, &inlen, &outbuf, &outlen) == (size_t)(-1)) {
		DEBUG(misc, 0, "[iconv] error converting '%s'. Errno %d", name, errno);
	}

	*outbuf = '\0';
	/* FIX: invalid characters will abort conversion, but they shouldn't occur? */
	return buf;
}

/**
 * Convert from OpenTTD's encoding to that of the local environment
 * @param name pointer to a valid string that will be converted
 * @return pointer to a new stringbuffer that contains the converted string
 */
std::string OTTD2FS(const std::string &name)
{
	static iconv_t convd = (iconv_t)(-1);
	char buf[1024] = {};

	if (convd == (iconv_t)(-1)) {
		const char *env = GetLocalCode();
		convd = iconv_open(env, INTERNALCODE);
		if (convd == (iconv_t)(-1)) {
			DEBUG(misc, 0, "[iconv] conversion from codeset '%s' to '%s' unsupported", INTERNALCODE, env);
			return name;
		}
	}

	return convert_tofrom_fs(convd, name.c_str(), buf, lengthof(buf));
}

/**
 * Convert to OpenTTD's encoding from that of the local environment
 * @param name valid string that will be converted
 * @return pointer to a new stringbuffer that contains the converted string
 */
std::string FS2OTTD(const std::string &name)
{
	static iconv_t convd = (iconv_t)(-1);
	char buf[1024] = {};

	if (convd == (iconv_t)(-1)) {
		const char *env = GetLocalCode();
		convd = iconv_open(INTERNALCODE, env);
		if (convd == (iconv_t)(-1)) {
			DEBUG(misc, 0, "[iconv] conversion from codeset '%s' to '%s' unsupported", env, INTERNALCODE);
			return name;
		}
	}

	return convert_tofrom_fs(convd, name.c_str(), buf, lengthof(buf));
}

#endif /* WITH_ICONV */

void ShowInfo(const char *str)
{
	fprintf(stderr, "%s\n", str);
}

#if !defined(__APPLE__)
void ShowOSErrorBox(const char *buf, bool system)
{
	/* All unix systems, except OSX. Only use escape codes on a TTY. */
	if (isatty(fileno(stderr))) {
		fprintf(stderr, "\033[1;31mError: %s\033[0;39m\n", buf);
	} else {
		fprintf(stderr, "Error: %s\n", buf);
	}
}
#endif

#ifdef WITH_COCOA
void CocoaSetupAutoreleasePool();
void CocoaReleaseAutoreleasePool();
#endif

#ifndef TEST
int CDECL main(int argc, char *argv[])
{
	/* Make sure our arguments contain only valid UTF-8 characters. */
	for (int i = 0; i < argc; i++) ValidateString(argv[i]);

#ifdef WITH_COCOA
	CocoaSetupAutoreleasePool();
	/* This is passed if we are launched by double-clicking */
	if (argc >= 2 && strncmp(argv[1], "-psn", 4) == 0) {
		argv[1] = nullptr;
		argc = 1;
	}
#endif
	CrashLog::InitialiseCrashLog();

	SetRandomSeed(time(nullptr));

	signal(SIGPIPE, SIG_IGN);

	int ret = openttd_main(argc, argv);

#ifdef WITH_COCOA
	CocoaReleaseAutoreleasePool();
#endif

	return ret;
}
#endif

#ifndef WITH_COCOA
bool GetClipboardContents(char *buffer, const char *last)
{
#ifdef WITH_SDL2
	if (SDL_HasClipboardText() == SDL_FALSE) {
		return false;
	}

	char *clip = SDL_GetClipboardText();
	if (clip != NULL) {
		strecpy(buffer, clip, last);
		SDL_free(clip);
		return true;
	}
#endif

	return false;
}
#endif


#if defined(__EMSCRIPTEN__)
void OSOpenBrowser(const char *url)
{
	/* Implementation in pre.js */
	EM_ASM({ if(window["openttd_open_url"]) window.openttd_open_url($0, $1) }, url, strlen(url));
}
#elif !defined( __APPLE__)
void OSOpenBrowser(const char *url)
{
	pid_t child_pid = fork();
	if (child_pid != 0) return;

	const char *args[3];
	args[0] = "xdg-open";
	args[1] = url;
	args[2] = nullptr;
	execvp(args[0], const_cast<char * const *>(args));
	DEBUG(misc, 0, "Failed to open url: %s", url);
	exit(0);
}
#endif /* __APPLE__ */

void SetCurrentThreadName(const char *threadName) {
#if !defined(NO_THREADS) && defined(__GLIBC__)
#if __GLIBC_PREREQ(2, 12)
	if (threadName) pthread_setname_np(pthread_self(), threadName);
#endif /* __GLIBC_PREREQ(2, 12) */
#endif /* !defined(NO_THREADS) && defined(__GLIBC__) */
#if defined(__APPLE__)
	MacOSSetThreadName(threadName);
#endif /* defined(__APPLE__) */
}
