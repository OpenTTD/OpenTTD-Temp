/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_config.cpp Finding NewGRFs and configuring them. */

#include "stdafx.h"
#include "debug.h"
#include "3rdparty/md5/md5.h"
#include "newgrf.h"
#include "network/network_func.h"
#include "gfx_func.h"
#include "newgrf_text.h"
#include "window_func.h"
#include "progress.h"
#include "video/video_driver.hpp"
#include "strings_func.h"
#include "textfile_gui.h"
#include "thread.h"
#include "newgrf_config.h"
#include "newgrf_text.h"

#include "fileio_func.h"
#include "fios.h"

#include "safeguards.h"


/**
 * Create a new GRFConfig.
 * @param filename Set the filename of this GRFConfig to filename. The argument
 *   is copied so the original string isn't needed after the constructor.
 */
GRFConfig::GRFConfig(const char *filename) :
	num_valid_params(lengthof(param))
{
	if (filename != nullptr) this->filename = stredup(filename);
}

/**
 * Create a new GRFConfig that is a deep copy of an existing config.
 * @param config The GRFConfig object to make a copy of.
 */
GRFConfig::GRFConfig(const GRFConfig &config) :
	ZeroedMemoryAllocator(),
	ident(config.ident),
	name(config.name),
	info(config.info),
	url(config.url),
	version(config.version),
	min_loadable_version(config.min_loadable_version),
	flags(config.flags & ~(1 << GCF_COPY)),
	status(config.status),
	grf_bugs(config.grf_bugs),
	num_params(config.num_params),
	num_valid_params(config.num_valid_params),
	palette(config.palette),
	has_param_defaults(config.has_param_defaults)
{
	MemCpyT<uint8>(this->original_md5sum, config.original_md5sum, lengthof(this->original_md5sum));
	MemCpyT<uint32>(this->param, config.param, lengthof(this->param));
	if (config.filename != nullptr) this->filename = stredup(config.filename);
	if (config.error != nullptr) this->error = new GRFError(*config.error);
	for (uint i = 0; i < config.param_info.size(); i++) {
		if (config.param_info[i] == nullptr) {
			this->param_info.push_back(nullptr);
		} else {
			this->param_info.push_back(new GRFParameterInfo(*config.param_info[i]));
		}
	}
}

/** Cleanup a GRFConfig object. */
GRFConfig::~GRFConfig()
{
	/* GCF_COPY as in NOT stredupped/alloced the filename */
	if (!HasBit(this->flags, GCF_COPY)) {
		free(this->filename);
		delete this->error;
	}

	for (uint i = 0; i < this->param_info.size(); i++) delete this->param_info[i];
}

/**
 * Copy the parameter information from the \a src config.
 * @param src Source config.
 */
void GRFConfig::CopyParams(const GRFConfig &src)
{
	this->num_params = src.num_params;
	this->num_valid_params = src.num_valid_params;
	MemCpyT<uint32>(this->param, src.param, lengthof(this->param));
}

/**
 * Get the name of this grf. In case the name isn't known
 * the filename is returned.
 * @return The name of filename of this grf.
 */
const char *GRFConfig::GetName() const
{
	const char *name = GetGRFStringFromGRFText(this->name);
	return StrEmpty(name) ? this->filename : name;
}

/**
 * Get the grf info.
 * @return A string with a description of this grf.
 */
const char *GRFConfig::GetDescription() const
{
	return GetGRFStringFromGRFText(this->info);
}

/**
 * Get the grf url.
 * @return A string with an url of this grf.
 */
const char *GRFConfig::GetURL() const
{
	return GetGRFStringFromGRFText(this->url);
}

/** Set the default value for all parameters as specified by action14. */
void GRFConfig::SetParameterDefaults()
{
	this->num_params = 0;
	MemSetT<uint32>(this->param, 0, lengthof(this->param));

	if (!this->has_param_defaults) return;

	for (uint i = 0; i < this->param_info.size(); i++) {
		if (this->param_info[i] == nullptr) continue;
		this->param_info[i]->SetValue(this, this->param_info[i]->def_value);
	}
}

/**
 * Set the palette of this GRFConfig to something suitable.
 * That is either the setting coming from the NewGRF or
 * the globally used palette.
 */
void GRFConfig::SetSuitablePalette()
{
	PaletteType pal;
	switch (this->palette & GRFP_GRF_MASK) {
		case GRFP_GRF_DOS:     pal = PAL_DOS;      break;
		case GRFP_GRF_WINDOWS: pal = PAL_WINDOWS;  break;
		default:               pal = _settings_client.gui.newgrf_default_palette == 1 ? PAL_WINDOWS : PAL_DOS; break;
	}
	SB(this->palette, GRFP_USE_BIT, 1, pal == PAL_WINDOWS ? GRFP_USE_WINDOWS : GRFP_USE_DOS);
}

/**
 * Finalize Action 14 info after file scan is finished.
 */
void GRFConfig::FinalizeParameterInfo()
{
	for (GRFParameterInfo *info : this->param_info) {
		if (info == nullptr) continue;
		info->Finalize();
	}
}

GRFConfig *_all_grfs;
GRFConfig *_grfconfig;
GRFConfig *_grfconfig_newgame;
GRFConfig *_grfconfig_static;
uint _missing_extra_graphics = 0;

/**
 * Construct a new GRFError.
 * @param severity The severity of this error.
 * @param message The actual error-string.
 */
GRFError::GRFError(StringID severity, StringID message) :
	message(message),
	severity(severity),
	param_value()
{
}

/**
 * Create a new GRFError that is a deep copy of an existing error message.
 * @param error The GRFError object to make a copy of.
 */
GRFError::GRFError(const GRFError &error) :
	custom_message(error.custom_message),
	data(error.data),
	message(error.message),
	severity(error.severity)
{
	memcpy(this->param_value, error.param_value, sizeof(this->param_value));
}

/**
 * Create a new empty GRFParameterInfo object.
 * @param nr The newgrf parameter that is changed.
 */
GRFParameterInfo::GRFParameterInfo(uint nr) :
	name(),
	desc(),
	type(PTYPE_UINT_ENUM),
	min_value(0),
	max_value(UINT32_MAX),
	def_value(0),
	param_nr(nr),
	first_bit(0),
	num_bit(32),
	value_names(),
	complete_labels(false)
{}

/**
 * Create a new GRFParameterInfo object that is a deep copy of an existing
 *   parameter info object.
 * @param info The GRFParameterInfo object to make a copy of.
 */
GRFParameterInfo::GRFParameterInfo(GRFParameterInfo &info) :
	name(info.name),
	desc(info.desc),
	type(info.type),
	min_value(info.min_value),
	max_value(info.max_value),
	def_value(info.def_value),
	param_nr(info.param_nr),
	first_bit(info.first_bit),
	num_bit(info.num_bit),
	value_names(info.value_names),
	complete_labels(info.complete_labels)
{
}

/**
 * Get the value of this user-changeable parameter from the given config.
 * @param config The GRFConfig to get the value from.
 * @return The value of this parameter.
 */
uint32 GRFParameterInfo::GetValue(struct GRFConfig *config) const
{
	/* GB doesn't work correctly with nbits == 32, so handle that case here. */
	if (this->num_bit == 32) return config->param[this->param_nr];
	return GB(config->param[this->param_nr], this->first_bit, this->num_bit);
}

/**
 * Set the value of this user-changeable parameter in the given config.
 * @param config The GRFConfig to set the value in.
 * @param value The new value.
 */
void GRFParameterInfo::SetValue(struct GRFConfig *config, uint32 value)
{
	/* SB doesn't work correctly with nbits == 32, so handle that case here. */
	if (this->num_bit == 32) {
		config->param[this->param_nr] = value;
	} else {
		SB(config->param[this->param_nr], this->first_bit, this->num_bit, value);
	}
	config->num_params = std::max<uint>(config->num_params, this->param_nr + 1);
	SetWindowDirty(WC_GAME_OPTIONS, WN_GAME_OPTIONS_NEWGRF_STATE);
}

/**
 * Finalize Action 14 info after file scan is finished.
 */
void GRFParameterInfo::Finalize()
{
	this->complete_labels = true;
	for (uint32 value = this->min_value; value <= this->max_value; value++) {
		if (!this->value_names.Contains(value)) {
			this->complete_labels = false;
			break;
		}
	}
}

/**
 * Update the palettes of the graphics from the config file.
 * Called when changing the default palette in advanced settings.
 * @param p1 Unused.
 * @return Always true.
 */
bool UpdateNewGRFConfigPalette(int32 p1)
{
	for (GRFConfig *c = _grfconfig_newgame; c != nullptr; c = c->next) c->SetSuitablePalette();
	for (GRFConfig *c = _grfconfig_static;  c != nullptr; c = c->next) c->SetSuitablePalette();
	for (GRFConfig *c = _all_grfs;          c != nullptr; c = c->next) c->SetSuitablePalette();
	return true;
}

/**
 * Get the data section size of a GRF.
 * @param f GRF.
 * @return Size of the data section or SIZE_MAX if the file has no separate data section.
 */
size_t GRFGetSizeOfDataSection(FILE *f)
{
	extern const byte _grf_cont_v2_sig[];
	static const uint header_len = 14;

	byte data[header_len];
	if (fread(data, 1, header_len, f) == header_len) {
		if (data[0] == 0 && data[1] == 0 && MemCmpT(data + 2, _grf_cont_v2_sig, 8) == 0) {
			/* Valid container version 2, get data section size. */
			size_t offset = ((size_t)data[13] << 24) | ((size_t)data[12] << 16) | ((size_t)data[11] << 8) | (size_t)data[10];
			if (offset >= 1 * 1024 * 1024 * 1024) {
				DEBUG(grf, 0, "Unexpectedly large offset for NewGRF");
				/* Having more than 1 GiB of data is very implausible. Mostly because then
				 * all pools in OpenTTD are flooded already. Or it's just Action C all over.
				 * In any case, the offsets to graphics will likely not work either. */
				return SIZE_MAX;
			}
			return header_len + offset;
		}
	}

	return SIZE_MAX;
}

/**
 * Calculate the MD5 sum for a GRF, and store it in the config.
 * @param config GRF to compute.
 * @param subdir The subdirectory to look in.
 * @return MD5 sum was successfully computed
 */
static bool CalcGRFMD5Sum(GRFConfig *config, Subdirectory subdir)
{
	FILE *f;
	Md5 checksum;
	uint8 buffer[1024];
	size_t len, size;

	/* open the file */
	f = FioFOpenFile(config->filename, "rb", subdir, &size);
	if (f == nullptr) return false;

	long start = ftell(f);
	size = std::min(size, GRFGetSizeOfDataSection(f));

	if (start < 0 || fseek(f, start, SEEK_SET) < 0) {
		FioFCloseFile(f);
		return false;
	}

	/* calculate md5sum */
	while ((len = fread(buffer, 1, (size > sizeof(buffer)) ? sizeof(buffer) : size, f)) != 0 && size != 0) {
		size -= len;
		checksum.Append(buffer, len);
	}
	checksum.Finish(config->ident.md5sum);

	FioFCloseFile(f);

	return true;
}


/**
 * Find the GRFID of a given grf, and calculate its md5sum.
 * @param config    grf to fill.
 * @param is_static grf is static.
 * @param subdir    the subdirectory to search in.
 * @return Operation was successfully completed.
 */
bool FillGRFDetails(GRFConfig *config, bool is_static, Subdirectory subdir)
{
	if (!FioCheckFileExists(config->filename, subdir)) {
		config->status = GCS_NOT_FOUND;
		return false;
	}

	/* Find and load the Action 8 information */
	LoadNewGRFFile(config, CONFIG_SLOT, GLS_FILESCAN, subdir);
	config->SetSuitablePalette();
	config->FinalizeParameterInfo();

	/* Skip if the grfid is 0 (not read) or if it is an internal GRF */
	if (config->ident.grfid == 0 || HasBit(config->flags, GCF_SYSTEM)) return false;

	if (is_static) {
		/* Perform a 'safety scan' for static GRFs */
		LoadNewGRFFile(config, CONFIG_SLOT, GLS_SAFETYSCAN, subdir);

		/* GCF_UNSAFE is set if GLS_SAFETYSCAN finds unsafe actions */
		if (HasBit(config->flags, GCF_UNSAFE)) return false;
	}

	return CalcGRFMD5Sum(config, subdir);
}


/**
 * Clear a GRF Config list, freeing all nodes.
 * @param config Start of the list.
 * @post \a config is set to \c nullptr.
 */
void ClearGRFConfigList(GRFConfig **config)
{
	GRFConfig *c, *next;
	for (c = *config; c != nullptr; c = next) {
		next = c->next;
		delete c;
	}
	*config = nullptr;
}


/**
 * Copy a GRF Config list
 * @param dst pointer to destination list
 * @param src pointer to source list values
 * @param init_only the copied GRF will be processed up to GLS_INIT
 * @return pointer to the last value added to the destination list
 */
GRFConfig **CopyGRFConfigList(GRFConfig **dst, const GRFConfig *src, bool init_only)
{
	/* Clear destination as it will be overwritten */
	ClearGRFConfigList(dst);
	for (; src != nullptr; src = src->next) {
		GRFConfig *c = new GRFConfig(*src);

		ClrBit(c->flags, GCF_INIT_ONLY);
		if (init_only) SetBit(c->flags, GCF_INIT_ONLY);

		*dst = c;
		dst = &c->next;
	}

	return dst;
}

/**
 * Removes duplicates from lists of GRFConfigs. These duplicates
 * are introduced when the _grfconfig_static GRFs are appended
 * to the _grfconfig on a newgame or savegame. As the parameters
 * of the static GRFs could be different that the parameters of
 * the ones used non-statically. This can result in desyncs in
 * multiplayers, so the duplicate static GRFs have to be removed.
 *
 * This function _assumes_ that all static GRFs are placed after
 * the non-static GRFs.
 *
 * @param list the list to remove the duplicates from
 */
static void RemoveDuplicatesFromGRFConfigList(GRFConfig *list)
{
	GRFConfig *prev;
	GRFConfig *cur;

	if (list == nullptr) return;

	for (prev = list, cur = list->next; cur != nullptr; prev = cur, cur = cur->next) {
		if (cur->ident.grfid != list->ident.grfid) continue;

		prev->next = cur->next;
		delete cur;
		cur = prev; // Just go back one so it continues as normal later on
	}

	RemoveDuplicatesFromGRFConfigList(list->next);
}

/**
 * Appends the static GRFs to a list of GRFs
 * @param dst the head of the list to add to
 */
void AppendStaticGRFConfigs(GRFConfig **dst)
{
	GRFConfig **tail = dst;
	while (*tail != nullptr) tail = &(*tail)->next;

	CopyGRFConfigList(tail, _grfconfig_static, false);
	RemoveDuplicatesFromGRFConfigList(*dst);
}

/**
 * Appends an element to a list of GRFs
 * @param dst the head of the list to add to
 * @param el the new tail to be
 */
void AppendToGRFConfigList(GRFConfig **dst, GRFConfig *el)
{
	GRFConfig **tail = dst;
	while (*tail != nullptr) tail = &(*tail)->next;
	*tail = el;

	RemoveDuplicatesFromGRFConfigList(*dst);
}


/** Reset the current GRF Config to either blank or newgame settings. */
void ResetGRFConfig(bool defaults)
{
	CopyGRFConfigList(&_grfconfig, _grfconfig_newgame, !defaults);
	AppendStaticGRFConfigs(&_grfconfig);
}


/**
 * Check if all GRFs in the GRF config from a savegame can be loaded.
 * @param grfconfig GrfConfig to check
 * @return will return any of the following 3 values:<br>
 * <ul>
 * <li> GLC_ALL_GOOD: No problems occurred, all GRF files were found and loaded
 * <li> GLC_COMPATIBLE: For one or more GRF's no exact match was found, but a
 *     compatible GRF with the same grfid was found and used instead
 * <li> GLC_NOT_FOUND: For one or more GRF's no match was found at all
 * </ul>
 */
GRFListCompatibility IsGoodGRFConfigList(GRFConfig *grfconfig)
{
	GRFListCompatibility res = GLC_ALL_GOOD;

	for (GRFConfig *c = grfconfig; c != nullptr; c = c->next) {
		const GRFConfig *f = FindGRFConfig(c->ident.grfid, FGCM_EXACT, c->ident.md5sum);
		if (f == nullptr || HasBit(f->flags, GCF_INVALID)) {
			char buf[256];

			/* If we have not found the exactly matching GRF try to find one with the
			 * same grfid, as it most likely is compatible */
			f = FindGRFConfig(c->ident.grfid, FGCM_COMPATIBLE, nullptr, c->version);
			if (f != nullptr) {
				md5sumToString(buf, lastof(buf), c->ident.md5sum);
				DEBUG(grf, 1, "NewGRF %08X (%s) not found; checksum %s. Compatibility mode on", BSWAP32(c->ident.grfid), c->filename, buf);
				if (!HasBit(c->flags, GCF_COMPATIBLE)) {
					/* Preserve original_md5sum after it has been assigned */
					SetBit(c->flags, GCF_COMPATIBLE);
					memcpy(c->original_md5sum, c->ident.md5sum, sizeof(c->original_md5sum));
				}

				/* Non-found has precedence over compatibility load */
				if (res != GLC_NOT_FOUND) res = GLC_COMPATIBLE;
				goto compatible_grf;
			}

			/* No compatible grf was found, mark it as disabled */
			md5sumToString(buf, lastof(buf), c->ident.md5sum);
			DEBUG(grf, 0, "NewGRF %08X (%s) not found; checksum %s", BSWAP32(c->ident.grfid), c->filename, buf);

			c->status = GCS_NOT_FOUND;
			res = GLC_NOT_FOUND;
		} else {
compatible_grf:
			DEBUG(grf, 1, "Loading GRF %08X from %s", BSWAP32(f->ident.grfid), f->filename);
			/* The filename could be the filename as in the savegame. As we need
			 * to load the GRF here, we need the correct filename, so overwrite that
			 * in any case and set the name and info when it is not set already.
			 * When the GCF_COPY flag is set, it is certain that the filename is
			 * already a local one, so there is no need to replace it. */
			if (!HasBit(c->flags, GCF_COPY)) {
				free(c->filename);
				c->filename = stredup(f->filename);
				memcpy(c->ident.md5sum, f->ident.md5sum, sizeof(c->ident.md5sum));
				c->name = f->name;
				c->info = f->name;
				c->error = nullptr;
				c->version = f->version;
				c->min_loadable_version = f->min_loadable_version;
				c->num_valid_params = f->num_valid_params;
				c->has_param_defaults = f->has_param_defaults;
				for (uint i = 0; i < f->param_info.size(); i++) {
					if (f->param_info[i] == nullptr) {
						c->param_info.push_back(nullptr);
					} else {
						c->param_info.push_back(new GRFParameterInfo(*f->param_info[i]));
					}
				}
			}
		}
	}

	return res;
}

/** Helper for scanning for files with GRF as extension */
class GRFFileScanner : FileScanner {
	std::chrono::steady_clock::time_point next_update; ///< The next moment we do update the screen.
	uint num_scanned; ///< The number of GRFs we have scanned.

public:
	GRFFileScanner() : num_scanned(0)
	{
		this->next_update = std::chrono::steady_clock::now();
	}

	bool AddFile(const std::string &filename, size_t basepath_length, const std::string &tar_filename) override;

	/** Do the scan for GRFs. */
	static uint DoScan()
	{
		GRFFileScanner fs;
		int ret = fs.Scan(".grf", NEWGRF_DIR);
		/* The number scanned and the number returned may not be the same;
		 * duplicate NewGRFs and base sets are ignored in the return value. */
		_settings_client.gui.last_newgrf_count = fs.num_scanned;
		return ret;
	}
};

bool GRFFileScanner::AddFile(const std::string &filename, size_t basepath_length, const std::string &tar_filename)
{
	/* Abort if the user stopped the game during a scan. */
	if (_exit_game) return false;

	GRFConfig *c = new GRFConfig(filename.c_str() + basepath_length);

	bool added = true;
	if (FillGRFDetails(c, false)) {
		if (_all_grfs == nullptr) {
			_all_grfs = c;
		} else {
			/* Insert file into list at a position determined by its
			 * name, so the list is sorted as we go along */
			GRFConfig **pd, *d;
			bool stop = false;
			for (pd = &_all_grfs; (d = *pd) != nullptr; pd = &d->next) {
				if (c->ident.grfid == d->ident.grfid && memcmp(c->ident.md5sum, d->ident.md5sum, sizeof(c->ident.md5sum)) == 0) added = false;
				/* Because there can be multiple grfs with the same name, make sure we checked all grfs with the same name,
				 *  before inserting the entry. So insert a new grf at the end of all grfs with the same name, instead of
				 *  just after the first with the same name. Avoids doubles in the list. */
				if (strcasecmp(c->GetName(), d->GetName()) <= 0) {
					stop = true;
				} else if (stop) {
					break;
				}
			}
			if (added) {
				c->next = d;
				*pd = c;
			}
		}
	} else {
		added = false;
	}

	this->num_scanned++;

	const char *name = nullptr;
	if (c->name != nullptr) name = GetGRFStringFromGRFText(c->name);
	if (name == nullptr) name = c->filename;
	UpdateNewGRFScanStatus(this->num_scanned, name);
	VideoDriver::GetInstance()->GameLoopPause();

	if (!added) {
		/* File couldn't be opened, or is either not a NewGRF or is a
		 * 'system' NewGRF or it's already known, so forget about it. */
		delete c;
	}

	return added;
}

/**
 * Simple sorter for GRFS
 * @param c1 the first GRFConfig *
 * @param c2 the second GRFConfig *
 * @return true if the name of first NewGRF is before the name of the second.
 */
static bool GRFSorter(GRFConfig * const &c1, GRFConfig * const &c2)
{
	return strnatcmp(c1->GetName(), c2->GetName()) < 0;
}

/**
 * Really perform the scan for all NewGRFs.
 * @param callback The callback to call after the scanning is complete.
 */
void DoScanNewGRFFiles(NewGRFScanCallback *callback)
{
	ClearGRFConfigList(&_all_grfs);
	TarScanner::DoScan(TarScanner::NEWGRF);

	DEBUG(grf, 1, "Scanning for NewGRFs");
	uint num = GRFFileScanner::DoScan();

	DEBUG(grf, 1, "Scan complete, found %d files", num);
	if (num != 0 && _all_grfs != nullptr) {
		/* Sort the linked list using quicksort.
		 * For that we first have to make an array, then sort and
		 * then remake the linked list. */
		std::vector<GRFConfig *> to_sort;

		uint i = 0;
		for (GRFConfig *p = _all_grfs; p != nullptr; p = p->next, i++) {
			to_sort.push_back(p);
		}
		/* Number of files is not necessarily right */
		num = i;

		std::sort(to_sort.begin(), to_sort.end(), GRFSorter);

		for (i = 1; i < num; i++) {
			to_sort[i - 1]->next = to_sort[i];
		}
		to_sort[num - 1]->next = nullptr;
		_all_grfs = to_sort[0];

		NetworkAfterNewGRFScan();
	}

	/* Yes... these are the NewGRF windows */
	InvalidateWindowClassesData(WC_SAVELOAD, 0, true);
	InvalidateWindowData(WC_GAME_OPTIONS, WN_GAME_OPTIONS_NEWGRF_STATE, GOID_NEWGRF_RESCANNED, true);
	if (!_exit_game && callback != nullptr) callback->OnNewGRFsScanned();

	DeleteWindowByClass(WC_MODAL_PROGRESS);
	SetModalProgress(false);
	MarkWholeScreenDirty();
}

/**
 * Scan for all NewGRFs.
 * @param callback The callback to call after the scanning is complete.
 */
void ScanNewGRFFiles(NewGRFScanCallback *callback)
{
	/* First set the modal progress. This ensures that it will eventually let go of the paint mutex. */
	SetModalProgress(true);
	/* Only then can we really start, especially by marking the whole screen dirty. Get those other windows hidden!. */
	MarkWholeScreenDirty();

	DoScanNewGRFFiles(callback);
}

/**
 * Find a NewGRF in the scanned list.
 * @param grfid GRFID to look for,
 * @param mode Restrictions for matching grfs
 * @param md5sum Expected MD5 sum
 * @param desired_version Requested version
 * @return The matching grf, if it exists in #_all_grfs, else \c nullptr.
 */
const GRFConfig *FindGRFConfig(uint32 grfid, FindGRFConfigMode mode, const uint8 *md5sum, uint32 desired_version)
{
	assert((mode == FGCM_EXACT) != (md5sum == nullptr));
	const GRFConfig *best = nullptr;
	for (const GRFConfig *c = _all_grfs; c != nullptr; c = c->next) {
		/* if md5sum is set, we look for an exact match and continue if not found */
		if (!c->ident.HasGrfIdentifier(grfid, md5sum)) continue;
		/* return it, if the exact same newgrf is found, or if we do not care about finding "the best" */
		if (md5sum != nullptr || mode == FGCM_ANY) return c;
		/* Skip incompatible stuff, unless explicitly allowed */
		if (mode != FGCM_NEWEST && HasBit(c->flags, GCF_INVALID)) continue;
		/* check version compatibility */
		if (mode == FGCM_COMPATIBLE && (c->version < desired_version || c->min_loadable_version > desired_version)) continue;
		/* remember the newest one as "the best" */
		if (best == nullptr || c->version > best->version) best = c;
	}

	return best;
}

/** Structure for UnknownGRFs; this is a lightweight variant of GRFConfig */
struct UnknownGRF : public GRFIdentifier {
	GRFTextWrapper name; ///< Name of the GRF.

	UnknownGRF() = default;
	UnknownGRF(const UnknownGRF &other) = default;
	UnknownGRF(UnknownGRF &&other) = default;
	UnknownGRF(uint32 grfid, const uint8 *_md5sum) : GRFIdentifier(grfid, _md5sum), name(new GRFTextList) {}
};

/**
 * Finds the name of a NewGRF in the list of names for unknown GRFs. An
 * unknown GRF is a GRF where the .grf is not found during scanning.
 *
 * The names are resolved via UDP calls to servers that should know the name,
 * though the replies may not come. This leaves "<Unknown>" as name, though
 * that shouldn't matter _very_ much as they need GRF crawler or so to look
 * up the GRF anyway and that works better with the GRF ID.
 *
 * @param grfid  the GRF ID part of the 'unique' GRF identifier
 * @param md5sum the MD5 checksum part of the 'unique' GRF identifier
 * @param create whether to create a new GRFConfig if the GRFConfig did not
 *               exist in the fake list of GRFConfigs.
 * @return The GRFTextWrapper of the name of the GRFConfig with the given GRF ID
 *         and MD5 checksum or nullptr when it does not exist and create is false.
 *         This value must NEVER be freed by the caller.
 */
GRFTextWrapper FindUnknownGRFName(uint32 grfid, uint8 *md5sum, bool create)
{
	static std::vector<UnknownGRF> unknown_grfs;

	for (const auto &grf : unknown_grfs) {
		if (grf.grfid == grfid) {
			if (memcmp(md5sum, grf.md5sum, sizeof(grf.md5sum)) == 0) return grf.name;
		}
	}

	if (!create) return nullptr;

	unknown_grfs.emplace_back(grfid, md5sum);
	UnknownGRF &grf = unknown_grfs.back();

	AddGRFTextToList(grf.name, UNKNOWN_GRF_NAME_PLACEHOLDER);

	return grf.name;
}

/**
 * Retrieve a NewGRF from the current config by its grfid.
 * @param grfid grf to look for.
 * @param mask  GRFID mask to allow for partial matching.
 * @return The grf config, if it exists, else \c nullptr.
 */
GRFConfig *GetGRFConfig(uint32 grfid, uint32 mask)
{
	GRFConfig *c;

	for (c = _grfconfig; c != nullptr; c = c->next) {
		if ((c->ident.grfid & mask) == (grfid & mask)) return c;
	}

	return nullptr;
}


/** Build a string containing space separated parameter values, and terminate */
char *GRFBuildParamList(char *dst, const GRFConfig *c, const char *last)
{
	uint i;

	/* Return an empty string if there are no parameters */
	if (c->num_params == 0) return strecpy(dst, "", last);

	for (i = 0; i < c->num_params; i++) {
		if (i > 0) dst = strecpy(dst, " ", last);
		dst += seprintf(dst, last, "%d", c->param[i]);
	}
	return dst;
}

/** Base GRF ID for OpenTTD's base graphics GRFs. */
static const uint32 OPENTTD_GRAPHICS_BASE_GRF_ID = BSWAP32(0xFF4F5400);

/**
 * Search a textfile file next to this NewGRF.
 * @param type The type of the textfile to search for.
 * @return The filename for the textfile, \c nullptr otherwise.
 */
const char *GRFConfig::GetTextfile(TextfileType type, TextDirection *text_dir) const
{
	return ::GetTextfile(type, NEWGRF_DIR, this->filename, text_dir);
}
