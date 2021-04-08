/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file os2.cpp OS2 related OS support. */

#include "../../stdafx.h"
#include "../../openttd.h"
#include "../../gui.h"
#include "../../fileio_func.h"
#include "../../fios.h"
#include "../../openttd.h"
#include "../../core/random_func.hpp"
#include "../../string_func.h"
#include "../../textbuf_gui.h"
#include "../../thread.h"
#include "../../network/social_plugin_api.h"

#include "table/strings.h"

#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <time.h>
#ifndef __INNOTEK_LIBC__
#	include <dos.h>
#endif

#include "../../safeguards.h"

#define INCL_WIN
#define INCL_WINCLIPBOARD

#include <os2.h>
#ifndef __INNOTEK_LIBC__
#	include <i86.h>
#endif

bool FiosIsRoot(const char *file)
{
	return file[3] == '\0';
}

void FiosGetDrives(FileList &file_list)
{
	uint disk, disk2, save, total;

#ifndef __INNOTEK_LIBC__
	_dos_getdrive(&save); // save original drive
#else
	save = _getdrive(); // save original drive
	char wd[MAX_PATH];
	getcwd(wd, MAX_PATH);
	total = 'z';
#endif

	/* get an available drive letter */
#ifndef __INNOTEK_LIBC__
	for (disk = 1;; disk++) {
		_dos_setdrive(disk, &total);
#else
	for (disk = 'A';; disk++) {
		_chdrive(disk);
#endif
		if (disk >= total)  break;

#ifndef __INNOTEK_LIBC__
		_dos_getdrive(&disk2);
#else
		disk2 = _getdrive();
#endif

		if (disk == disk2) {
			FiosItem *fios = file_list.Append();
			fios->type = FIOS_TYPE_DRIVE;
			fios->mtime = 0;
#ifndef __INNOTEK_LIBC__
			snprintf(fios->name, lengthof(fios->name),  "%c:", 'A' + disk - 1);
#else
			snprintf(fios->name, lengthof(fios->name),  "%c:", disk);
#endif
			strecpy(fios->title, fios->name, lastof(fios->title));
		}
	}

	/* Restore the original drive */
#ifndef __INNOTEK_LIBC__
	_dos_setdrive(save, &total);
#else
	chdir(wd);
#endif
}

bool FiosGetDiskFreeSpace(const char *path, uint64 *tot)
{
#ifndef __INNOTEK_LIBC__
	struct diskfree_t free;
	char drive = path[0] - 'A' + 1;

	if (tot != nullptr && _getdiskfree(drive, &free) == 0) {
		*tot = free.avail_clusters * free.sectors_per_cluster * free.bytes_per_sector;
		return true;
	}

	return false;
#else
	uint64 free = 0;

#ifdef HAS_STATVFS
	{
		struct statvfs s;

		if (statvfs(path, &s) != 0) return false;
		free = (uint64)s.f_frsize * s.f_bavail;
	}
#endif
	if (tot != nullptr) *tot = free;
	return true;
#endif
}

bool FiosIsValidFile(const char *path, const struct dirent *ent, struct stat *sb)
{
	char filename[MAX_PATH];

	snprintf(filename, lengthof(filename), "%s" PATHSEP "%s", path, ent->d_name);
	return stat(filename, sb) == 0;
}

bool FiosIsHiddenFile(const struct dirent *ent)
{
	return ent->d_name[0] == '.';
}

void ShowInfo(const char *str)
{
	HAB hab;
	HMQ hmq;
	ULONG rc;

	/* init PM env. */
	hmq = WinCreateMsgQueue((hab = WinInitialize(0)), 0);

	/* display the box */
	rc = WinMessageBox(HWND_DESKTOP, HWND_DESKTOP, (const unsigned char *)str, (const unsigned char *)"OpenTTD", 0, MB_OK | MB_MOVEABLE | MB_INFORMATION);

	/* terminate PM env. */
	WinDestroyMsgQueue(hmq);
	WinTerminate(hab);
}

void ShowOSErrorBox(const char *buf, bool system)
{
	HAB hab;
	HMQ hmq;
	ULONG rc;

	/* init PM env. */
	hmq = WinCreateMsgQueue((hab = WinInitialize(0)), 0);

	/* display the box */
	rc = WinMessageBox(HWND_DESKTOP, HWND_DESKTOP, (const unsigned char *)buf, (const unsigned char *)"OpenTTD", 0, MB_OK | MB_MOVEABLE | MB_ERROR);

	/* terminate PM env. */
	WinDestroyMsgQueue(hmq);
	WinTerminate(hab);
}

int CDECL main(int argc, char *argv[])
{
	SetRandomSeed(time(nullptr));

	/* Make sure our arguments contain only valid UTF-8 characters. */
	for (int i = 0; i < argc; i++) ValidateString(argv[i]);

	return openttd_main(argc, argv);
}

bool GetClipboardContents(char *buffer, const char *last)
{
/* XXX -- Currently no clipboard support implemented with GCC */
#ifndef __INNOTEK_LIBC__
	HAB hab = 0;

	if (WinOpenClipbrd(hab))
	{
		const char *text = (const char*)WinQueryClipbrdData(hab, CF_TEXT);

		if (text != nullptr)
		{
			strecpy(buffer, text, last);
			WinCloseClipbrd(hab);
			return true;
		}

		WinCloseClipbrd(hab);
	}
#endif
	return false;
}


void OSOpenBrowser(const char *url)
{
	// stub only
	DEBUG(misc, 0, "Failed to open url: %s", url);
}

void SetCurrentThreadName(const char *)
{
}

OpenTTD_SocialPluginInit SocialLoadPlugin()
{
	return nullptr;
}
