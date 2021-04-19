/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_config.h Functions to find and configure NewGRFs. */

#ifndef NEWGRF_CONFIG_H
#define NEWGRF_CONFIG_H

#include "strings_type.h"
#include "core/alloc_type.hpp"
#include "core/smallmap_type.hpp"
#include "misc/countedptr.hpp"
#include "fileio_type.h"
#include "textfile_type.h"
#include "newgrf_text.h"

/** GRF config bit flags */
enum GCF_Flags {
	GCF_SYSTEM,     ///< GRF file is an openttd-internal system grf
	GCF_UNSAFE,     ///< GRF file is unsafe for static usage
	GCF_STATIC,     ///< GRF file is used statically (can be used in any MP game)
	GCF_COMPATIBLE, ///< GRF file does not exactly match the requested GRF (different MD5SUM), but grfid matches)
	GCF_COPY,       ///< The data is copied from a grf in _all_grfs
	GCF_INIT_ONLY,  ///< GRF file is processed up to GLS_INIT
	GCF_RESERVED,   ///< GRF file passed GLS_RESERVE stage
	GCF_INVALID,    ///< GRF is unusable with this version of OpenTTD
};

/** Status of GRF */
enum GRFStatus {
	GCS_UNKNOWN,      ///< The status of this grf file is unknown
	GCS_DISABLED,     ///< GRF file is disabled
	GCS_NOT_FOUND,    ///< GRF file was not found in the local cache
	GCS_INITIALISED,  ///< GRF file has been initialised
	GCS_ACTIVATED,    ///< GRF file has been activated
};

/** Encountered GRF bugs */
enum GRFBugs {
	GBUG_VEH_LENGTH,        ///< Length of rail vehicle changes when not inside a depot
	GBUG_VEH_REFIT,         ///< Articulated vehicles carry different cargoes resp. are differently refittable than specified in purchase list
	GBUG_VEH_POWERED_WAGON, ///< Powered wagon changed poweredness state when not inside a depot
	GBUG_UNKNOWN_CB_RESULT, ///< A callback returned an unknown/invalid result
	GBUG_VEH_CAPACITY,      ///< Capacity of vehicle changes when not refitting or arranging
};

/** Status of post-gameload GRF compatibility check */
enum GRFListCompatibility {
	GLC_ALL_GOOD,   ///< All GRF needed by game are present
	GLC_COMPATIBLE, ///< Compatible (eg. the same ID, but different checksum) GRF found in at least one case
	GLC_NOT_FOUND,  ///< At least one GRF couldn't be found (higher priority than GLC_COMPATIBLE)
};

/** Information that can/has to be stored about a GRF's palette. */
enum GRFPalette {
	GRFP_USE_BIT     = 0,   ///< The bit used for storing the palette to use.
	GRFP_GRF_OFFSET  = 2,   ///< The offset of the GRFP_GRF data.
	GRFP_GRF_SIZE    = 2,   ///< The size of the GRFP_GRF data.
	GRFP_BLT_OFFSET  = 4,   ///< The offset of the GRFP_BLT data.
	GRFP_BLT_SIZE    = 1,   ///< The size of the GRFP_BLT data.

	GRFP_USE_DOS     = 0x0, ///< The palette state is set to use the DOS palette.
	GRFP_USE_WINDOWS = 0x1, ///< The palette state is set to use the Windows palette.
	GRFP_USE_MASK    = 0x1, ///< Bitmask to get only the use palette use states.

	GRFP_GRF_UNSET   = 0x0 << GRFP_GRF_OFFSET,          ///< The NewGRF provided no information.
	GRFP_GRF_DOS     = 0x1 << GRFP_GRF_OFFSET,          ///< The NewGRF says the DOS palette can be used.
	GRFP_GRF_WINDOWS = 0x2 << GRFP_GRF_OFFSET,          ///< The NewGRF says the Windows palette can be used.
	GRFP_GRF_ANY     = GRFP_GRF_DOS | GRFP_GRF_WINDOWS, ///< The NewGRF says any palette can be used.
	GRFP_GRF_MASK    = GRFP_GRF_ANY,                    ///< Bitmask to get only the NewGRF supplied information.

	GRFP_BLT_UNSET   = 0x0 << GRFP_BLT_OFFSET,          ///< The NewGRF provided no information or doesn't care about a 32 bpp blitter.
	GRFP_BLT_32BPP   = 0x1 << GRFP_BLT_OFFSET,          ///< The NewGRF prefers a 32 bpp blitter.
	GRFP_BLT_MASK    = GRFP_BLT_32BPP,                  ///< Bitmask to only get the blitter information.
};


/** Basic data to distinguish a GRF. Used in the server list window */
struct GRFIdentifier {
	uint32 grfid;     ///< GRF ID (defined by Action 0x08)
	uint8 md5sum[16]; ///< MD5 checksum of file to distinguish files with the same GRF ID (eg. newer version of GRF)

	GRFIdentifier() = default;
	GRFIdentifier(const GRFIdentifier &other) = default;
	GRFIdentifier(GRFIdentifier &&other) = default;
	GRFIdentifier(uint32 grfid, const uint8 *md5sum) : grfid(grfid)
	{
		MemCpyT(this->md5sum, md5sum, lengthof(this->md5sum));
	}

	GRFIdentifier& operator =(const GRFIdentifier &other) = default;

	/**
	 * Does the identification match the provided values?
	 * @param grfid  Expected grfid.
	 * @param md5sum Expected md5sum, may be \c nullptr (in which case, do not check it).
	 * @return the object has the provided grfid and md5sum.
	 */
	inline bool HasGrfIdentifier(uint32 grfid, const uint8 *md5sum) const
	{
		if (this->grfid != grfid) return false;
		if (md5sum == nullptr) return true;
		return memcmp(md5sum, this->md5sum, sizeof(this->md5sum)) == 0;
	}
};

/** Information about why GRF had problems during initialisation */
struct GRFError {
	GRFError(StringID severity, StringID message = 0);
	GRFError(const GRFError &error);

	std::string custom_message; ///< Custom message (if present)
	std::string data;           ///< Additional data for message and custom_message
	StringID message;           ///< Default message
	StringID severity;          ///< Info / Warning / Error / Fatal
	uint32 param_value[2];      ///< Values of GRF parameters to show for message and custom_message
};

/** The possible types of a newgrf parameter. */
enum GRFParameterType {
	PTYPE_UINT_ENUM, ///< The parameter allows a range of numbers, each of which can have a special name
	PTYPE_BOOL,      ///< The parameter is either 0 or 1
	PTYPE_END,       ///< Invalid parameter type
};

/** Information about one grf parameter. */
struct GRFParameterInfo {
	GRFParameterInfo(uint nr);
	GRFParameterInfo(GRFParameterInfo &info);
	GRFTextList name;      ///< The name of this parameter
	GRFTextList desc;      ///< The description of this parameter
	GRFParameterType type; ///< The type of this parameter
	uint32 min_value;      ///< The minimal value this parameter can have
	uint32 max_value;      ///< The maximal value of this parameter
	uint32 def_value;      ///< Default value of this parameter
	byte param_nr;         ///< GRF parameter to store content in
	byte first_bit;        ///< First bit to use in the GRF parameter
	byte num_bit;          ///< Number of bits to use for this parameter
	SmallMap<uint32, GRFTextList> value_names; ///< Names for each value.
	bool complete_labels;  ///< True if all values have a label.

	uint32 GetValue(struct GRFConfig *config) const;
	void SetValue(struct GRFConfig *config, uint32 value);
	void Finalize();
};

/** Information about GRF, used in the game and (part of it) in savegames */
struct GRFConfig : ZeroedMemoryAllocator {
	GRFConfig(const char *filename = nullptr);
	GRFConfig(const GRFConfig &config);
	~GRFConfig();

	GRFIdentifier ident;                        ///< grfid and md5sum to uniquely identify newgrfs
	uint8 original_md5sum[16];                  ///< MD5 checksum of original file if only a 'compatible' file was loaded
	char *filename;                             ///< Filename - either with or without full path
	GRFTextWrapper name;                        ///< NOSAVE: GRF name (Action 0x08)
	GRFTextWrapper info;                        ///< NOSAVE: GRF info (author, copyright, ...) (Action 0x08)
	GRFTextWrapper url;                         ///< NOSAVE: URL belonging to this GRF.
	GRFError *error;                            ///< NOSAVE: Error/Warning during GRF loading (Action 0x0B)

	uint32 version;                             ///< NOSAVE: Version a NewGRF can set so only the newest NewGRF is shown
	uint32 min_loadable_version;                ///< NOSAVE: Minimum compatible version a NewGRF can define
	uint8 flags;                                ///< NOSAVE: GCF_Flags, bitset
	GRFStatus status;                           ///< NOSAVE: GRFStatus, enum
	uint32 grf_bugs;                            ///< NOSAVE: bugs in this GRF in this run, @see enum GRFBugs
	uint32 param[0x80];                         ///< GRF parameters
	uint8 num_params;                           ///< Number of used parameters
	uint8 num_valid_params;                     ///< NOSAVE: Number of valid parameters (action 0x14)
	uint8 palette;                              ///< GRFPalette, bitset
	std::vector<GRFParameterInfo *> param_info; ///< NOSAVE: extra information about the parameters
	bool has_param_defaults;                    ///< NOSAVE: did this newgrf specify any defaults for it's parameters

	struct GRFConfig *next;                     ///< NOSAVE: Next item in the linked list

	void CopyParams(const GRFConfig &src);

	const char *GetTextfile(TextfileType type, TextDirection *text_dir = nullptr) const;
	const char *GetName() const;
	const char *GetDescription() const;
	const char *GetURL() const;

	void SetParameterDefaults();
	void SetSuitablePalette();
	void FinalizeParameterInfo();
};

/** Method to find GRFs using FindGRFConfig */
enum FindGRFConfigMode {
	FGCM_EXACT,       ///< Only find Grfs matching md5sum
	FGCM_COMPATIBLE,  ///< Find best compatible Grf wrt. desired_version
	FGCM_NEWEST,      ///< Find newest Grf
	FGCM_NEWEST_VALID,///< Find newest Grf, ignoring Grfs with GCF_INVALID set
	FGCM_ANY,         ///< Use first found
};

extern GRFConfig *_all_grfs;          ///< First item in list of all scanned NewGRFs
extern GRFConfig *_grfconfig;         ///< First item in list of current GRF set up
extern GRFConfig *_grfconfig_newgame; ///< First item in list of default GRF set up
extern GRFConfig *_grfconfig_static;  ///< First item in list of static GRF set up
extern uint _missing_extra_graphics;  ///< Number of sprites provided by the fallback extra GRF, i.e. missing in the baseset.

/** Callback for NewGRF scanning. */
struct NewGRFScanCallback {
	/** Make sure the right destructor gets called. */
	virtual ~NewGRFScanCallback() {}
	/** Called whenever the NewGRF scan completed. */
	virtual void OnNewGRFsScanned() = 0;
};

size_t GRFGetSizeOfDataSection(FILE *f);

void ScanNewGRFFiles(NewGRFScanCallback *callback);
const GRFConfig *FindGRFConfig(uint32 grfid, FindGRFConfigMode mode, const uint8 *md5sum = nullptr, uint32 desired_version = 0);
GRFConfig *GetGRFConfig(uint32 grfid, uint32 mask = 0xFFFFFFFF);
GRFConfig **CopyGRFConfigList(GRFConfig **dst, const GRFConfig *src, bool init_only);
void AppendStaticGRFConfigs(GRFConfig **dst);
void AppendToGRFConfigList(GRFConfig **dst, GRFConfig *el);
void ClearGRFConfigList(GRFConfig **config);
void ResetGRFConfig(bool defaults);
GRFListCompatibility IsGoodGRFConfigList(GRFConfig *grfconfig);
bool FillGRFDetails(GRFConfig *config, bool is_static, Subdirectory subdir = NEWGRF_DIR);
char *GRFBuildParamList(char *dst, const GRFConfig *c, const char *last);

/* In newgrf_gui.cpp */
void ShowNewGRFSettings(bool editable, bool show_params, bool exec_changes, GRFConfig **config);

/** For communication about GRFs over the network */
#define UNKNOWN_GRF_NAME_PLACEHOLDER "<Unknown>"
GRFTextWrapper FindUnknownGRFName(uint32 grfid, uint8 *md5sum, bool create);

void UpdateNewGRFScanStatus(uint num, const char *name);
bool UpdateNewGRFConfigPalette(int32 p1 = 0);

#endif /* NEWGRF_CONFIG_H */
