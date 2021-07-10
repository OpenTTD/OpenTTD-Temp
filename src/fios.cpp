/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file fios.cpp
 * This file contains functions for building file lists for the save/load dialogs.
 */

#include "stdafx.h"
#include "3rdparty/md5/md5.h"
#include "fileio_func.h"
#include "fios.h"
#include "network/network_content.h"
#include "screenshot.h"
#include "string_func.h"
#include "strings_func.h"
#include "tar_type.h"
#include <sys/stat.h>
#include <functional>
#include <optional>
#include <charconv>

#ifndef _WIN32
# include <unistd.h>
#endif /* _WIN32 */

#include "table/strings.h"

#include "safeguards.h"

/* Variables to display file lists */
static std::string *_fios_path = nullptr;
SortingBits _savegame_sort_order = SORT_BY_DATE | SORT_DESCENDING;

/* OS-specific functions are taken from their respective files (win32/unix/os2 .c) */
extern bool FiosIsRoot(const char *path);
extern bool FiosIsValidFile(const char *path, const struct dirent *ent, struct stat *sb);
extern bool FiosIsHiddenFile(const struct dirent *ent);
extern void FiosGetDrives(FileList &file_list);
extern bool FiosGetDiskFreeSpace(const char *path, uint64 *tot);

/* get the name of an oldstyle savegame */
extern void GetOldSaveGameName(const std::string &file, char *title, const char *last);

/**
 * Compare two FiosItem's. Used with sort when sorting the file list.
 * @param other The FiosItem to compare to.
 * @return for ascending order: returns true if da < db. Vice versa for descending order.
 */
bool FiosItem::operator< (const FiosItem &other) const
{
	int r = false;

	if ((_savegame_sort_order & SORT_BY_NAME) == 0 && (*this).mtime != other.mtime) {
		r = (*this).mtime - other.mtime;
	} else {
		r = strnatcmp((*this).title, other.title);
	}
	if (r == 0) return false;
	return (_savegame_sort_order & SORT_DESCENDING) ? r > 0 : r < 0;
}

/**
 * Construct a file list with the given kind of files, for the stated purpose.
 * @param abstract_filetype Kind of files to collect.
 * @param fop Purpose of the collection, either #SLO_LOAD or #SLO_SAVE.
 */
void FileList::BuildFileList(AbstractFileType abstract_filetype, SaveLoadOperation fop)
{
	this->clear();

	assert(fop == SLO_LOAD || fop == SLO_SAVE);
	switch (abstract_filetype) {
		case FT_NONE:
			break;

		case FT_SAVEGAME:
			FiosGetSavegameList(fop, *this);
			break;

		case FT_SCENARIO:
			FiosGetScenarioList(fop, *this);
			break;

		case FT_HEIGHTMAP:
			FiosGetHeightmapList(fop, *this);
			break;

		default:
			NOT_REACHED();
	}
}

/**
 * Find file information of a file by its name from the file list.
 * @param file The filename to return information about. Can be the actual name
 *             or a numbered entry into the filename list.
 * @return The information on the file, or \c nullptr if the file is not available.
 */
const FiosItem *FileList::FindItem(const char *file)
{
	for (const auto &it : *this) {
		const FiosItem *item = &it;
		if (strcmp(file, item->name) == 0) return item;
		if (strcmp(file, item->title) == 0) return item;
	}

	/* If no name matches, try to parse it as number */
	char *endptr;
	int i = strtol(file, &endptr, 10);
	if (file == endptr || *endptr != '\0') i = -1;

	if (IsInsideMM(i, 0, this->size())) return &this->at(i);

	/* As a last effort assume it is an OpenTTD savegame and
	 * that the ".sav" part was not given. */
	char long_file[MAX_PATH];
	seprintf(long_file, lastof(long_file), "%s.sav", file);
	for (const auto &it : *this) {
		const FiosItem *item = &it;
		if (strcmp(long_file, item->name) == 0) return item;
		if (strcmp(long_file, item->title) == 0) return item;
	}

	return nullptr;
}

/**
 * Get descriptive texts. Returns the path and free space
 * left on the device
 * @param path string describing the path
 * @param total_free total free space in megabytes, optional (can be nullptr)
 * @return StringID describing the path (free space or failure)
 */
StringID FiosGetDescText(const char **path, uint64 *total_free)
{
	*path = _fios_path->c_str();
	return FiosGetDiskFreeSpace(*path, total_free) ? STR_SAVELOAD_BYTES_FREE : STR_ERROR_UNABLE_TO_READ_DRIVE;
}

/**
 * Browse to a new path based on the passed \a item, starting at #_fios_path.
 * @param *item Item telling us what to do.
 * @return A filename w/path if we reached a file, otherwise \c nullptr.
 */
const char *FiosBrowseTo(const FiosItem *item)
{
	switch (item->type) {
		case FIOS_TYPE_DRIVE:
#if defined(_WIN32) || defined(__OS2__)
			assert(_fios_path != nullptr);
			*_fios_path = std::string{ item->title[0] } + ":" PATHSEP;
#endif
			break;

		case FIOS_TYPE_INVALID:
			break;

		case FIOS_TYPE_PARENT: {
			assert(_fios_path != nullptr);
			auto s = _fios_path->find_last_of(PATHSEPCHAR);
			if (s != std::string::npos && s != 0) {
				_fios_path->erase(s); // Remove last path separator character, so we can go up one level.
			}

			s = _fios_path->find_last_of(PATHSEPCHAR);
			if (s != std::string::npos) {
				_fios_path->erase(s + 1); // go up a directory
			}
			break;
		}

		case FIOS_TYPE_DIR:
			assert(_fios_path != nullptr);
			*_fios_path += item->name;
			*_fios_path += PATHSEP;
			break;

		case FIOS_TYPE_DIRECT:
			assert(_fios_path != nullptr);
			*_fios_path = item->name;
			break;

		case FIOS_TYPE_FILE:
		case FIOS_TYPE_OLDFILE:
		case FIOS_TYPE_SCENARIO:
		case FIOS_TYPE_OLD_SCENARIO:
		case FIOS_TYPE_PNG:
		case FIOS_TYPE_BMP:
			return item->name;
	}

	return nullptr;
}

/**
 * Construct a filename from its components in destination buffer \a buf.
 * @param path Directory path, may be \c nullptr.
 * @param name Filename.
 * @param ext Filename extension (use \c "" for no extension).
 * @return The completed filename.
 */
static std::string FiosMakeFilename(const std::string *path, const char *name, const char *ext)
{
	std::string buf;

	if (path != nullptr) {
		buf = *path;
		/* Remove trailing path separator, if present */
		if (!buf.empty() && buf.back() == PATHSEPCHAR) buf.pop_back();
	}

	/* Don't append the extension if it is already there */
	const char *period = strrchr(name, '.');
	if (period != nullptr && strcasecmp(period, ext) == 0) ext = "";

	return buf + PATHSEP + name + ext;
}

/**
 * Make a save game or scenario filename from a name.
 * @param buf Destination buffer for saving the filename.
 * @param name Name of the file.
 * @param last Last element of buffer \a buf.
 * @return The completed filename.
 */
std::string FiosMakeSavegameName(const char *name)
{
	const char *extension = (_game_mode == GM_EDITOR) ? ".scn" : ".sav";

	return FiosMakeFilename(_fios_path, name, extension);
}

/**
 * Construct a filename for a height map.
 * @param name Filename.
 * @return The completed filename.
 */
std::string FiosMakeHeightmapName(const char *name)
{
	std::string ext(".");
	ext += GetCurrentScreenshotExtension();

	return FiosMakeFilename(_fios_path, name, ext.c_str());
}

/**
 * Delete a file.
 * @param name Filename to delete.
 * @return Whether the file deletion was successful.
 */
bool FiosDelete(const char *name)
{
	std::string filename = FiosMakeSavegameName(name);
	return unlink(filename.c_str()) == 0;
}

typedef FiosType fios_getlist_callback_proc(SaveLoadOperation fop, const std::string &filename, const char *ext, char *title, const char *last);

/**
 * Scanner to scan for a particular type of FIOS file.
 */
class FiosFileScanner : public FileScanner {
	SaveLoadOperation fop;   ///< The kind of file we are looking for.
	fios_getlist_callback_proc *callback_proc; ///< Callback to check whether the file may be added
	FileList &file_list;     ///< Destination of the found files.
public:
	/**
	 * Create the scanner
	 * @param fop Purpose of collecting the list.
	 * @param callback_proc The function that is called where you need to do the filtering.
	 * @param file_list Destination of the found files.
	 */
	FiosFileScanner(SaveLoadOperation fop, fios_getlist_callback_proc *callback_proc, FileList &file_list) :
			fop(fop), callback_proc(callback_proc), file_list(file_list)
	{}

	bool AddFile(const std::string &filename, size_t basepath_length, const std::string &tar_filename) override;
};

/**
 * Try to add a fios item set with the given filename.
 * @param filename        the full path to the file to read
 * @param basepath_length amount of characters to chop of before to get a relative filename
 * @return true if the file is added.
 */
bool FiosFileScanner::AddFile(const std::string &filename, size_t basepath_length, const std::string &tar_filename)
{
	auto sep = filename.rfind('.');
	if (sep == std::string::npos) return false;
	std::string ext = filename.substr(sep);

	char fios_title[64];
	fios_title[0] = '\0'; // reset the title;

	FiosType type = this->callback_proc(this->fop, filename, ext.c_str(), fios_title, lastof(fios_title));
	if (type == FIOS_TYPE_INVALID) return false;

	for (const auto &fios : file_list) {
		if (filename == fios.name) return false;
	}

	FiosItem *fios = &file_list.emplace_back();
#ifdef _WIN32
	// Retrieve the file modified date using GetFileTime rather than stat to work around an obscure MSVC bug that affects Windows XP
	HANDLE fh = CreateFile(OTTD2FS(filename).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);

	if (fh != INVALID_HANDLE_VALUE) {
		FILETIME ft;
		ULARGE_INTEGER ft_int64;

		if (GetFileTime(fh, nullptr, nullptr, &ft) != 0) {
			ft_int64.HighPart = ft.dwHighDateTime;
			ft_int64.LowPart = ft.dwLowDateTime;

			// Convert from hectonanoseconds since 01/01/1601 to seconds since 01/01/1970
			fios->mtime = ft_int64.QuadPart / 10000000ULL - 11644473600ULL;
		} else {
			fios->mtime = 0;
		}

		CloseHandle(fh);
#else
	struct stat sb;
	if (stat(filename.c_str(), &sb) == 0) {
		fios->mtime = sb.st_mtime;
#endif
	} else {
		fios->mtime = 0;
	}

	fios->type = type;
	strecpy(fios->name, filename.c_str(), lastof(fios->name));

	/* If the file doesn't have a title, use its filename */
	const char *t = fios_title;
	if (StrEmpty(fios_title)) {
		auto ps = filename.rfind(PATHSEPCHAR);
		t = filename.c_str() + (ps == std::string::npos ? 0 : ps + 1);
	}
	strecpy(fios->title, t, lastof(fios->title));
	StrMakeValidInPlace(fios->title, lastof(fios->title));

	return true;
}


/**
 * Fill the list of the files in a directory, according to some arbitrary rule.
 * @param fop Purpose of collecting the list.
 * @param callback_proc The function that is called where you need to do the filtering.
 * @param subdir The directory from where to start (global) searching.
 * @param file_list Destination of the found files.
 */
static void FiosGetFileList(SaveLoadOperation fop, fios_getlist_callback_proc *callback_proc, Subdirectory subdir, FileList &file_list)
{
	struct stat sb;
	struct dirent *dirent;
	DIR *dir;
	FiosItem *fios;
	size_t sort_start;
	char d_name[sizeof(fios->name)];

	file_list.clear();

	assert(_fios_path != nullptr);

	/* A parent directory link exists if we are not in the root directory */
	if (!FiosIsRoot(_fios_path->c_str())) {
		fios = &file_list.emplace_back();
		fios->type = FIOS_TYPE_PARENT;
		fios->mtime = 0;
		strecpy(fios->name, "..", lastof(fios->name));
		SetDParamStr(0, "..");
		GetString(fios->title, STR_SAVELOAD_PARENT_DIRECTORY, lastof(fios->title));
	}

	/* Show subdirectories */
	if ((dir = ttd_opendir(_fios_path->c_str())) != nullptr) {
		while ((dirent = readdir(dir)) != nullptr) {
			strecpy(d_name, FS2OTTD(dirent->d_name).c_str(), lastof(d_name));

			/* found file must be directory, but not '.' or '..' */
			if (FiosIsValidFile(_fios_path->c_str(), dirent, &sb) && S_ISDIR(sb.st_mode) &&
					(!FiosIsHiddenFile(dirent) || strncasecmp(d_name, PERSONAL_DIR, strlen(d_name)) == 0) &&
					strcmp(d_name, ".") != 0 && strcmp(d_name, "..") != 0) {
				fios = &file_list.emplace_back();
				fios->type = FIOS_TYPE_DIR;
				fios->mtime = 0;
				strecpy(fios->name, d_name, lastof(fios->name));
				std::string dirname = std::string(d_name) + PATHSEP;
				SetDParamStr(0, dirname);
				GetString(fios->title, STR_SAVELOAD_DIRECTORY, lastof(fios->title));
				StrMakeValidInPlace(fios->title, lastof(fios->title));
			}
		}
		closedir(dir);
	}

	/* Sort the subdirs always by name, ascending, remember user-sorting order */
	{
		SortingBits order = _savegame_sort_order;
		_savegame_sort_order = SORT_BY_NAME | SORT_ASCENDING;
		std::sort(file_list.begin(), file_list.end());
		_savegame_sort_order = order;
	}

	/* This is where to start sorting for the filenames */
	sort_start = file_list.size();

	/* Show files */
	FiosFileScanner scanner(fop, callback_proc, file_list);
	if (subdir == NO_DIRECTORY) {
		scanner.Scan(nullptr, _fios_path->c_str(), false);
	} else {
		scanner.Scan(nullptr, subdir, true, true);
	}

	std::sort(file_list.begin() + sort_start, file_list.end());

	/* Show drives */
	FiosGetDrives(file_list);

	file_list.shrink_to_fit();
}

/**
 * Get the title of a file, which (if exists) is stored in a file named
 * the same as the data file but with '.title' added to it.
 * @param file filename to get the title for
 * @param title the title buffer to fill
 * @param last the last element in the title buffer
 * @param subdir the sub directory to search in
 */
static void GetFileTitle(const std::string &file, char *title, const char *last, Subdirectory subdir)
{
	std::string buf = file;
	buf += ".title";

	FILE *f = FioFOpenFile(buf, "r", subdir);
	if (f == nullptr) return;

	size_t read = fread(title, 1, last - title, f);
	assert(title + read <= last);
	title[read] = '\0';
	StrMakeValidInPlace(title, last);
	FioFCloseFile(f);
}

/**
 * Callback for FiosGetFileList. It tells if a file is a savegame or not.
 * @param fop Purpose of collecting the list.
 * @param file Name of the file to check.
 * @param ext A pointer to the extension identifier inside file
 * @param title Buffer if a callback wants to lookup the title of the file; nullptr to skip the lookup
 * @param last Last available byte in buffer (to prevent buffer overflows); not used when title == nullptr
 * @return a FIOS_TYPE_* type of the found file, FIOS_TYPE_INVALID if not a savegame
 * @see FiosGetFileList
 * @see FiosGetSavegameList
 */
FiosType FiosGetSavegameListCallback(SaveLoadOperation fop, const std::string &file, const char *ext, char *title, const char *last)
{
	/* Show savegame files
	 * .SAV OpenTTD saved game
	 * .SS1 Transport Tycoon Deluxe preset game
	 * .SV1 Transport Tycoon Deluxe (Patch) saved game
	 * .SV2 Transport Tycoon Deluxe (Patch) saved 2-player game */

	/* Don't crash if we supply no extension */
	if (ext == nullptr) return FIOS_TYPE_INVALID;

	if (strcasecmp(ext, ".sav") == 0) {
		GetFileTitle(file, title, last, SAVE_DIR);
		return FIOS_TYPE_FILE;
	}

	if (fop == SLO_LOAD) {
		if (strcasecmp(ext, ".ss1") == 0 || strcasecmp(ext, ".sv1") == 0 ||
				strcasecmp(ext, ".sv2") == 0) {
			if (title != nullptr) GetOldSaveGameName(file, title, last);
			return FIOS_TYPE_OLDFILE;
		}
	}

	return FIOS_TYPE_INVALID;
}

/**
 * Get a list of savegames.
 * @param fop Purpose of collecting the list.
 * @param file_list Destination of the found files.
 * @see FiosGetFileList
 */
void FiosGetSavegameList(SaveLoadOperation fop, FileList &file_list)
{
	static std::optional<std::string> fios_save_path;

	if (!fios_save_path) fios_save_path = FioFindDirectory(SAVE_DIR);

	_fios_path = &(*fios_save_path);

	FiosGetFileList(fop, &FiosGetSavegameListCallback, NO_DIRECTORY, file_list);
}

/**
 * Callback for FiosGetFileList. It tells if a file is a scenario or not.
 * @param fop Purpose of collecting the list.
 * @param file Name of the file to check.
 * @param ext A pointer to the extension identifier inside file
 * @param title Buffer if a callback wants to lookup the title of the file
 * @param last Last available byte in buffer (to prevent buffer overflows)
 * @return a FIOS_TYPE_* type of the found file, FIOS_TYPE_INVALID if not a scenario
 * @see FiosGetFileList
 * @see FiosGetScenarioList
 */
static FiosType FiosGetScenarioListCallback(SaveLoadOperation fop, const std::string &file, const char *ext, char *title, const char *last)
{
	/* Show scenario files
	 * .SCN OpenTTD style scenario file
	 * .SV0 Transport Tycoon Deluxe (Patch) scenario
	 * .SS0 Transport Tycoon Deluxe preset scenario */
	if (strcasecmp(ext, ".scn") == 0) {
		GetFileTitle(file, title, last, SCENARIO_DIR);
		return FIOS_TYPE_SCENARIO;
	}

	if (fop == SLO_LOAD) {
		if (strcasecmp(ext, ".sv0") == 0 || strcasecmp(ext, ".ss0") == 0 ) {
			GetOldSaveGameName(file, title, last);
			return FIOS_TYPE_OLD_SCENARIO;
		}
	}

	return FIOS_TYPE_INVALID;
}

/**
 * Get a list of scenarios.
 * @param fop Purpose of collecting the list.
 * @param file_list Destination of the found files.
 * @see FiosGetFileList
 */
void FiosGetScenarioList(SaveLoadOperation fop, FileList &file_list)
{
	static std::optional<std::string> fios_scn_path;

	/* Copy the default path on first run or on 'New Game' */
	if (!fios_scn_path) fios_scn_path = FioFindDirectory(SCENARIO_DIR);

	_fios_path = &(*fios_scn_path);

	std::string base_path = FioFindDirectory(SCENARIO_DIR);
	Subdirectory subdir = (fop == SLO_LOAD && base_path == *_fios_path) ? SCENARIO_DIR : NO_DIRECTORY;
	FiosGetFileList(fop, &FiosGetScenarioListCallback, subdir, file_list);
}

static FiosType FiosGetHeightmapListCallback(SaveLoadOperation fop, const std::string &file, const char *ext, char *title, const char *last)
{
	/* Show heightmap files
	 * .PNG PNG Based heightmap files
	 * .BMP BMP Based heightmap files
	 */

	FiosType type = FIOS_TYPE_INVALID;

#ifdef WITH_PNG
	if (strcasecmp(ext, ".png") == 0) type = FIOS_TYPE_PNG;
#endif /* WITH_PNG */

	if (strcasecmp(ext, ".bmp") == 0) type = FIOS_TYPE_BMP;

	if (type == FIOS_TYPE_INVALID) return FIOS_TYPE_INVALID;

	TarFileList::iterator it = _tar_filelist[SCENARIO_DIR].find(file);
	if (it != _tar_filelist[SCENARIO_DIR].end()) {
		/* If the file is in a tar and that tar is not in a heightmap
		 * directory we are for sure not supposed to see it.
		 * Examples of this are pngs part of documentation within
		 * collections of NewGRFs or 32 bpp graphics replacement PNGs.
		 */
		bool match = false;
		for (Searchpath sp : _valid_searchpaths) {
			std::string buf = FioGetDirectory(sp, HEIGHTMAP_DIR);

			if (buf.compare(0, buf.size(), it->second.tar_filename, 0, buf.size()) == 0) {
				match = true;
				break;
			}
		}

		if (!match) return FIOS_TYPE_INVALID;
	}

	GetFileTitle(file, title, last, HEIGHTMAP_DIR);

	return type;
}

/**
 * Get a list of heightmaps.
 * @param fop Purpose of collecting the list.
 * @param file_list Destination of the found files.
 */
void FiosGetHeightmapList(SaveLoadOperation fop, FileList &file_list)
{
	static std::optional<std::string> fios_hmap_path;

	if (!fios_hmap_path) fios_hmap_path = FioFindDirectory(HEIGHTMAP_DIR);

	_fios_path = &(*fios_hmap_path);

	std::string base_path = FioFindDirectory(HEIGHTMAP_DIR);
	Subdirectory subdir = base_path == *_fios_path ? HEIGHTMAP_DIR : NO_DIRECTORY;
	FiosGetFileList(fop, &FiosGetHeightmapListCallback, subdir, file_list);
}

/**
 * Get the directory for screenshots.
 * @return path to screenshots
 */
const char *FiosGetScreenshotDir()
{
	static std::optional<std::string> fios_screenshot_path;

	if (!fios_screenshot_path) fios_screenshot_path = FioFindDirectory(SCREENSHOT_DIR);

	return fios_screenshot_path->c_str();
}

/** Basic data to distinguish a scenario. Used in the server list window */
struct ScenarioIdentifier {
	uint32 scenid;           ///< ID for the scenario (generated by content).
	uint8 md5sum[16];        ///< MD5 checksum of file.
	char filename[MAX_PATH]; ///< filename of the file.

	bool operator == (const ScenarioIdentifier &other) const
	{
		return this->scenid == other.scenid &&
				memcmp(this->md5sum, other.md5sum, sizeof(this->md5sum)) == 0;
	}

	bool operator != (const ScenarioIdentifier &other) const
	{
		return !(*this == other);
	}
};

/**
 * Scanner to find the unique IDs of scenarios
 */
class ScenarioScanner : protected FileScanner, public std::vector<ScenarioIdentifier> {
	bool scanned; ///< Whether we've already scanned
public:
	/** Initialise */
	ScenarioScanner() : scanned(false) {}

	/**
	 * Scan, but only if it's needed.
	 * @param rescan whether to force scanning even when it's not necessary
	 */
	void Scan(bool rescan)
	{
		if (this->scanned && !rescan) return;

		this->FileScanner::Scan(".id", SCENARIO_DIR, true, true);
		this->scanned = true;
	}

	bool AddFile(const std::string &filename, size_t basepath_length, const std::string &tar_filename) override
	{
		FILE *f = FioFOpenFile(filename, "r", SCENARIO_DIR);
		if (f == nullptr) return false;

		ScenarioIdentifier id;
		int fret = fscanf(f, "%u", &id.scenid);
		FioFCloseFile(f);
		if (fret != 1) return false;
		strecpy(id.filename, filename.c_str(), lastof(id.filename));

		Md5 checksum;
		uint8 buffer[1024];
		size_t len, size;

		/* open the scenario file, but first get the name.
		 * This is safe as we check on extension which
		 * must always exist. */
		f = FioFOpenFile(filename.substr(0, filename.rfind('.')), "rb", SCENARIO_DIR, &size);
		if (f == nullptr) return false;

		/* calculate md5sum */
		while ((len = fread(buffer, 1, (size > sizeof(buffer)) ? sizeof(buffer) : size, f)) != 0 && size != 0) {
			size -= len;
			checksum.Append(buffer, len);
		}
		checksum.Finish(id.md5sum);

		FioFCloseFile(f);

		include(*this, id);
		return true;
	}
};

/** Scanner for scenarios */
static ScenarioScanner _scanner;

/**
 * Find a given scenario based on its unique ID.
 * @param ci The content info to compare it to.
 * @param md5sum Whether to look at the md5sum or the id.
 * @return The filename of the file, else \c nullptr.
 */
const char *FindScenario(const ContentInfo *ci, bool md5sum)
{
	_scanner.Scan(false);

	for (ScenarioIdentifier &id : _scanner) {
		if (md5sum ? (memcmp(id.md5sum, ci->md5sum, sizeof(id.md5sum)) == 0)
		           : (id.scenid == ci->unique_id)) {
			return id.filename;
		}
	}

	return nullptr;
}

/**
 * Check whether we've got a given scenario based on its unique ID.
 * @param ci The content info to compare it to.
 * @param md5sum Whether to look at the md5sum or the id.
 * @return True iff we've got the scenario.
 */
bool HasScenario(const ContentInfo *ci, bool md5sum)
{
	return (FindScenario(ci, md5sum) != nullptr);
}

/**
 * Force a (re)scan of the scenarios.
 */
void ScanScenarios()
{
	_scanner.Scan(true);
}

/**
 * Constructs FiosNumberedSaveName. Initial number is the most recent save, or -1 if not found.
 * @param prefix The prefix to use to generate a filename.
*/
FiosNumberedSaveName::FiosNumberedSaveName(const std::string &prefix) : prefix(prefix), number(-1)
{
	static std::optional<std::string> _autosave_path;
	if (!_autosave_path) _autosave_path = FioFindDirectory(AUTOSAVE_DIR);

	static std::string _prefix; ///< Static as the lambda needs access to it.

	/* Callback for FiosFileScanner. */
	static fios_getlist_callback_proc *proc = [](SaveLoadOperation fop, const std::string &file, const char *ext, char *title, const char *last) {
		if (strcasecmp(ext, ".sav") == 0 && StrStartsWith(file, _prefix)) return FIOS_TYPE_FILE;
		return FIOS_TYPE_INVALID;
	};

	/* Prefix to check in the callback. */
	_prefix = *_autosave_path + this->prefix;

	/* Get the save list. */
	FileList list;
	FiosFileScanner scanner(SLO_SAVE, proc, list);
	scanner.Scan(".sav", _autosave_path->c_str(), false);

	/* Find the number for the most recent save, if any. */
	if (list.begin() != list.end()) {
		SortingBits order = _savegame_sort_order;
		_savegame_sort_order = SORT_BY_DATE | SORT_DESCENDING;
		std::sort(list.begin(), list.end());
		_savegame_sort_order = order;

		std::string_view name = list.begin()->title;
		std::from_chars(name.data() + this->prefix.size(), name.data() + name.size(), this->number);
	}
}

/**
 * Generate a savegame name and number according to _settings_client.gui.max_num_autosaves.
 * @return A filename in format "<prefix><number>.sav".
*/
std::string FiosNumberedSaveName::Filename()
{
	if (++this->number >= _settings_client.gui.max_num_autosaves) this->number = 0;
	return fmt::format("{}{}.sav", this->prefix, this->number);
}

/**
 * Generate an extension for a savegame name.
 * @return An extension in format "-<prefix>.sav".
*/
std::string FiosNumberedSaveName::Extension()
{
	return fmt::format("-{}.sav", this->prefix);
}
