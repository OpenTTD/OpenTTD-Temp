/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf.cpp Base of all NewGRF support. */

#include "stdafx.h"

#include <stdarg.h>

#include "debug.h"
#include "fileio_func.h"
#include "engine_func.h"
#include "engine_base.h"
#include "bridge.h"
#include "town.h"
#include "newgrf_engine.h"
#include "newgrf_text.h"
#include "fontcache.h"
#include "currency.h"
#include "landscape.h"
#include "newgrf_cargo.h"
#include "newgrf_house.h"
#include "newgrf_sound.h"
#include "newgrf_station.h"
#include "industrytype.h"
#include "newgrf_canal.h"
#include "newgrf_townname.h"
#include "newgrf_industries.h"
#include "newgrf_airporttiles.h"
#include "newgrf_airport.h"
#include "newgrf_object.h"
#include "rev.h"
#include "fios.h"
#include "strings_func.h"
#include "date_func.h"
#include "string_func.h"
#include "network/network.h"
#include <map>
#include "smallmap_gui.h"
#include "genworld.h"
#include "error.h"
#include "vehicle_func.h"
#include "language.h"
#include "vehicle_base.h"
#include "road.h"
#include "newgrf_roadstop.h"

#include "table/strings.h"
#include "table/build_industry.h"

#include "safeguards.h"

/* TTDPatch extended GRF format codec
 * (c) Petr Baudis 2004 (GPL'd)
 * Changes by Florian octo Forster are (c) by the OpenTTD development team.
 *
 * Contains portions of documentation by TTDPatch team.
 * Thanks especially to Josef Drexler for the documentation as well as a lot
 * of help at #tycoon. Also thanks to Michael Blunck for his GRF files which
 * served as subject to the initial testing of this codec. */

/** List of all loaded GRF files */
static std::vector<GRFFile *> _grf_files;

const std::vector<GRFFile *> &GetAllGRFFiles()
{
	return _grf_files;
}

/** Miscellaneous GRF features, set by Action 0x0D, parameter 0x9E */
byte _misc_grf_features = 0;

/** 32 * 8 = 256 flags. Apparently TTDPatch uses this many.. */
static uint32 _ttdpatch_flags[8];

/** Indicates which are the newgrf features currently loaded ingame */
GRFLoadedFeatures _loaded_newgrf_features;

static const uint MAX_SPRITEGROUP = UINT8_MAX; ///< Maximum GRF-local ID for a spritegroup.

/** Temporary data during loading of GRFs */
struct GrfProcessingState {
private:
	/** Definition of a single Action1 spriteset */
	struct SpriteSet {
		SpriteID sprite;  ///< SpriteID of the first sprite of the set.
		uint num_sprites; ///< Number of sprites in the set.
	};

	/** Currently referenceable spritesets */
	std::map<uint, SpriteSet> spritesets[GSF_END];

public:
	/* Global state */
	GrfLoadingStage stage;    ///< Current loading stage
	SpriteID spriteid;        ///< First available SpriteID for loading realsprites.

	/* Local state in the file */
	uint file_index;          ///< File index of currently processed GRF file.
	GRFFile *grffile;         ///< Currently processed GRF file.
	GRFConfig *grfconfig;     ///< Config of the currently processed GRF file.
	uint32 nfo_line;          ///< Currently processed pseudo sprite number in the GRF.
	byte grf_container_ver;   ///< Container format of the current GRF file.

	/* Kind of return values when processing certain actions */
	int skip_sprites;         ///< Number of pseudo sprites to skip before processing the next one. (-1 to skip to end of file)

	/* Currently referenceable spritegroups */
	SpriteGroup *spritegroups[MAX_SPRITEGROUP + 1];

	/** Clear temporary data before processing the next file in the current loading stage */
	void ClearDataForNextFile()
	{
		this->nfo_line = 0;
		this->skip_sprites = 0;

		for (uint i = 0; i < GSF_END; i++) {
			this->spritesets[i].clear();
		}

		memset(this->spritegroups, 0, sizeof(this->spritegroups));
	}

	/**
	 * Records new spritesets.
	 * @param feature GrfSpecFeature the set is defined for.
	 * @param first_sprite SpriteID of the first sprite in the set.
	 * @param first_set First spriteset to define.
	 * @param numsets Number of sets to define.
	 * @param numents Number of sprites per set to define.
	 */
	void AddSpriteSets(byte feature, SpriteID first_sprite, uint first_set, uint numsets, uint numents)
	{
		assert(feature < GSF_END);
		for (uint i = 0; i < numsets; i++) {
			SpriteSet &set = this->spritesets[feature][first_set + i];
			set.sprite = first_sprite + i * numents;
			set.num_sprites = numents;
		}
	}

	/**
	 * Check whether there are any valid spritesets for a feature.
	 * @param feature GrfSpecFeature to check.
	 * @return true if there are any valid sets.
	 * @note Spritesets with zero sprites are valid to allow callback-failures.
	 */
	bool HasValidSpriteSets(byte feature) const
	{
		assert(feature < GSF_END);
		return !this->spritesets[feature].empty();
	}

	/**
	 * Check whether a specific set is defined.
	 * @param feature GrfSpecFeature to check.
	 * @param set Set to check.
	 * @return true if the set is valid.
	 * @note Spritesets with zero sprites are valid to allow callback-failures.
	 */
	bool IsValidSpriteSet(byte feature, uint set) const
	{
		assert(feature < GSF_END);
		return this->spritesets[feature].find(set) != this->spritesets[feature].end();
	}

	/**
	 * Returns the first sprite of a spriteset.
	 * @param feature GrfSpecFeature to query.
	 * @param set Set to query.
	 * @return First sprite of the set.
	 */
	SpriteID GetSprite(byte feature, uint set) const
	{
		assert(IsValidSpriteSet(feature, set));
		return this->spritesets[feature].find(set)->second.sprite;
	}

	/**
	 * Returns the number of sprites in a spriteset
	 * @param feature GrfSpecFeature to query.
	 * @param set Set to query.
	 * @return Number of sprites in the set.
	 */
	uint GetNumEnts(byte feature, uint set) const
	{
		assert(IsValidSpriteSet(feature, set));
		return this->spritesets[feature].find(set)->second.num_sprites;
	}
};

static GrfProcessingState _cur;


/**
 * Helper to check whether an image index is valid for a particular NewGRF vehicle.
 * @tparam T The type of vehicle.
 * @param image_index The image index to check.
 * @return True iff the image index is valid, or 0xFD (use new graphics).
 */
template <VehicleType T>
static inline bool IsValidNewGRFImageIndex(uint8 image_index)
{
	return image_index == 0xFD || IsValidImageIndex<T>(image_index);
}

class OTTDByteReaderSignal { };

/** Class to read from a NewGRF file */
class ByteReader {
protected:
	byte *data;
	byte *end;

public:
	ByteReader(byte *data, byte *end) : data(data), end(end) { }

	inline byte ReadByte()
	{
		if (data < end) return *(data)++;
		throw OTTDByteReaderSignal();
	}

	uint16 ReadWord()
	{
		uint16 val = ReadByte();
		return val | (ReadByte() << 8);
	}

	uint16 ReadExtendedByte()
	{
		uint16 val = ReadByte();
		return val == 0xFF ? ReadWord() : val;
	}

	uint32 ReadDWord()
	{
		uint32 val = ReadWord();
		return val | (ReadWord() << 16);
	}

	uint32 ReadVarSize(byte size)
	{
		switch (size) {
			case 1: return ReadByte();
			case 2: return ReadWord();
			case 4: return ReadDWord();
			default:
				NOT_REACHED();
				return 0;
		}
	}

	const char *ReadString()
	{
		char *string = reinterpret_cast<char *>(data);
		size_t string_length = ttd_strnlen(string, Remaining());

		if (string_length == Remaining()) {
			/* String was not NUL terminated, so make sure it is now. */
			string[string_length - 1] = '\0';
			grfmsg(7, "String was not terminated with a zero byte.");
		} else {
			/* Increase the string length to include the NUL byte. */
			string_length++;
		}
		Skip(string_length);

		return string;
	}

	inline size_t Remaining() const
	{
		return end - data;
	}

	inline bool HasData(size_t count = 1) const
	{
		return data + count <= end;
	}

	inline byte *Data()
	{
		return data;
	}

	inline void Skip(size_t len)
	{
		data += len;
		/* It is valid to move the buffer to exactly the end of the data,
		 * as there may not be any more data read. */
		if (data > end) throw OTTDByteReaderSignal();
	}
};

typedef void (*SpecialSpriteHandler)(ByteReader *buf);

static const uint NUM_STATIONS_PER_GRF = 255; ///< Number of StationSpecs per NewGRF; limited to 255 to allow extending Action3 with an extended byte later on.

/** Temporary engine data used when loading only */
struct GRFTempEngineData {
	/** Summary state of refittability properties */
	enum Refittability {
		UNSET    =  0,  ///< No properties assigned. Default refit masks shall be activated.
		EMPTY,          ///< GRF defined vehicle as not-refittable. The vehicle shall only carry the default cargo.
		NONEMPTY,       ///< GRF defined the vehicle as refittable. If the refitmask is empty after translation (cargotypes not available), disable the vehicle.
	};

	uint16 cargo_allowed;
	uint16 cargo_disallowed;
	RailTypeLabel railtypelabel;
	uint8 roadtramtype;
	const GRFFile *defaultcargo_grf; ///< GRF defining the cargo translation table to use if the default cargo is the 'first refittable'.
	Refittability refittability;     ///< Did the newgrf set any refittability property? If not, default refittability will be applied.
	bool prop27_set;         ///< Did the NewGRF set property 27 (misc flags)?
	uint8 rv_max_speed;      ///< Temporary storage of RV prop 15, maximum speed in mph/0.8
	CargoTypes ctt_include_mask; ///< Cargo types always included in the refit mask.
	CargoTypes ctt_exclude_mask; ///< Cargo types always excluded from the refit mask.

	/**
	 * Update the summary refittability on setting a refittability property.
	 * @param non_empty true if the GRF sets the vehicle to be refittable.
	 */
	void UpdateRefittability(bool non_empty)
	{
		if (non_empty) {
			this->refittability = NONEMPTY;
		} else if (this->refittability == UNSET) {
			this->refittability = EMPTY;
		}
	}
};

static GRFTempEngineData *_gted;  ///< Temporary engine data used during NewGRF loading

/**
 * Contains the GRF ID of the owner of a vehicle if it has been reserved.
 * GRM for vehicles is only used if dynamic engine allocation is disabled,
 * so 256 is the number of original engines. */
static uint32 _grm_engines[256];

/** Contains the GRF ID of the owner of a cargo if it has been reserved */
static uint32 _grm_cargoes[NUM_CARGO * 2];

struct GRFLocation {
	uint32 grfid;
	uint32 nfoline;

	GRFLocation(uint32 grfid, uint32 nfoline) : grfid(grfid), nfoline(nfoline) { }

	bool operator<(const GRFLocation &other) const
	{
		return this->grfid < other.grfid || (this->grfid == other.grfid && this->nfoline < other.nfoline);
	}

	bool operator == (const GRFLocation &other) const
	{
		return this->grfid == other.grfid && this->nfoline == other.nfoline;
	}
};

static std::map<GRFLocation, SpriteID> _grm_sprites;
typedef std::map<GRFLocation, byte*> GRFLineToSpriteOverride;
static GRFLineToSpriteOverride _grf_line_to_action6_sprite_override;

/**
 * DEBUG() function dedicated to newGRF debugging messages
 * Function is essentially the same as DEBUG(grf, severity, ...) with the
 * addition of file:line information when parsing grf files.
 * NOTE: for the above reason(s) grfmsg() should ONLY be used for
 * loading/parsing grf files, not for runtime debug messages as there
 * is no file information available during that time.
 * @param severity debugging severity level, see debug.h
 * @param str message in printf() format
 */
void CDECL grfmsg(int severity, const char *str, ...)
{
	char buf[1024];
	va_list va;

	va_start(va, str);
	vseprintf(buf, lastof(buf), str, va);
	va_end(va);

	DEBUG(grf, severity, "[%s:%d] %s", _cur.grfconfig->filename, _cur.nfo_line, buf);
}

/**
 * Obtain a NewGRF file by its grfID
 * @param grfid The grfID to obtain the file for
 * @return The file.
 */
static GRFFile *GetFileByGRFID(uint32 grfid)
{
	for (GRFFile * const file : _grf_files) {
		if (file->grfid == grfid) return file;
	}
	return nullptr;
}

/**
 * Obtain a NewGRF file by its filename
 * @param filename The filename to obtain the file for.
 * @return The file.
 */
static GRFFile *GetFileByFilename(const char *filename)
{
	for (GRFFile * const file : _grf_files) {
		if (strcmp(file->filename, filename) == 0) return file;
	}
	return nullptr;
}

/** Reset all NewGRFData that was used only while processing data */
static void ClearTemporaryNewGRFData(GRFFile *gf)
{
	/* Clear the GOTO labels used for GRF processing */
	for (GRFLabel *l = gf->label; l != nullptr;) {
		GRFLabel *l2 = l->next;
		free(l);
		l = l2;
	}
	gf->label = nullptr;
}

/**
 * Disable a GRF
 * @param message Error message or STR_NULL.
 * @param config GRFConfig to disable, nullptr for current.
 * @return Error message of the GRF for further customisation.
 */
static GRFError *DisableGrf(StringID message = STR_NULL, GRFConfig *config = nullptr)
{
	GRFFile *file;
	if (config != nullptr) {
		file = GetFileByGRFID(config->ident.grfid);
	} else {
		config = _cur.grfconfig;
		file = _cur.grffile;
	}

	config->status = GCS_DISABLED;
	if (file != nullptr) ClearTemporaryNewGRFData(file);
	if (config == _cur.grfconfig) _cur.skip_sprites = -1;

	if (message != STR_NULL) {
		delete config->error;
		config->error = new GRFError(STR_NEWGRF_ERROR_MSG_FATAL, message);
		if (config == _cur.grfconfig) config->error->param_value[0] = _cur.nfo_line;
	}

	return config->error;
}

/**
 * Information for mapping static StringIDs.
 */
struct StringIDMapping {
	uint32 grfid;     ///< Source NewGRF.
	StringID source;  ///< Source StringID (GRF local).
	StringID *target; ///< Destination for mapping result.
};
typedef std::vector<StringIDMapping> StringIDMappingVector;
static StringIDMappingVector _string_to_grf_mapping;

/**
 * Record a static StringID for getting translated later.
 * @param source Source StringID (GRF local).
 * @param target Destination for the mapping result.
 */
static void AddStringForMapping(StringID source, StringID *target)
{
	*target = STR_UNDEFINED;
	_string_to_grf_mapping.push_back({_cur.grffile->grfid, source, target});
}

/**
 * Perform a mapping from TTDPatch's string IDs to OpenTTD's
 * string IDs, but only for the ones we are aware off; the rest
 * like likely unused and will show a warning.
 * @param str the string ID to convert
 * @return the converted string ID
 */
static StringID TTDPStringIDToOTTDStringIDMapping(StringID str)
{
	/* StringID table for TextIDs 0x4E->0x6D */
	static const StringID units_volume[] = {
		STR_ITEMS,      STR_PASSENGERS, STR_TONS,       STR_BAGS,
		STR_LITERS,     STR_ITEMS,      STR_CRATES,     STR_TONS,
		STR_TONS,       STR_TONS,       STR_TONS,       STR_BAGS,
		STR_TONS,       STR_TONS,       STR_TONS,       STR_BAGS,
		STR_TONS,       STR_TONS,       STR_BAGS,       STR_LITERS,
		STR_TONS,       STR_LITERS,     STR_TONS,       STR_ITEMS,
		STR_BAGS,       STR_LITERS,     STR_TONS,       STR_ITEMS,
		STR_TONS,       STR_ITEMS,      STR_LITERS,     STR_ITEMS
	};

	/* A string straight from a NewGRF; this was already translated by MapGRFStringID(). */
	assert(!IsInsideMM(str, 0xD000, 0xD7FF));

#define TEXTID_TO_STRINGID(begin, end, stringid, stringend) \
	static_assert(stringend - stringid == end - begin); \
	if (str >= begin && str <= end) return str + (stringid - begin)

	/* We have some changes in our cargo strings, resulting in some missing. */
	TEXTID_TO_STRINGID(0x000E, 0x002D, STR_CARGO_PLURAL_NOTHING,                      STR_CARGO_PLURAL_FIZZY_DRINKS);
	TEXTID_TO_STRINGID(0x002E, 0x004D, STR_CARGO_SINGULAR_NOTHING,                    STR_CARGO_SINGULAR_FIZZY_DRINK);
	if (str >= 0x004E && str <= 0x006D) return units_volume[str - 0x004E];
	TEXTID_TO_STRINGID(0x006E, 0x008D, STR_QUANTITY_NOTHING,                          STR_QUANTITY_FIZZY_DRINKS);
	TEXTID_TO_STRINGID(0x008E, 0x00AD, STR_ABBREV_NOTHING,                            STR_ABBREV_FIZZY_DRINKS);
	TEXTID_TO_STRINGID(0x00D1, 0x00E0, STR_COLOUR_DARK_BLUE,                          STR_COLOUR_WHITE);

	/* Map building names according to our lang file changes. There are several
	 * ranges of house ids, all of which need to be remapped to allow newgrfs
	 * to use original house names. */
	TEXTID_TO_STRINGID(0x200F, 0x201F, STR_TOWN_BUILDING_NAME_TALL_OFFICE_BLOCK_1,    STR_TOWN_BUILDING_NAME_OLD_HOUSES_1);
	TEXTID_TO_STRINGID(0x2036, 0x2041, STR_TOWN_BUILDING_NAME_COTTAGES_1,             STR_TOWN_BUILDING_NAME_SHOPPING_MALL_1);
	TEXTID_TO_STRINGID(0x2059, 0x205C, STR_TOWN_BUILDING_NAME_IGLOO_1,                STR_TOWN_BUILDING_NAME_PIGGY_BANK_1);

	/* Same thing for industries */
	TEXTID_TO_STRINGID(0x4802, 0x4826, STR_INDUSTRY_NAME_COAL_MINE,                   STR_INDUSTRY_NAME_SUGAR_MINE);
	TEXTID_TO_STRINGID(0x482D, 0x482E, STR_NEWS_INDUSTRY_CONSTRUCTION,                STR_NEWS_INDUSTRY_PLANTED);
	TEXTID_TO_STRINGID(0x4832, 0x4834, STR_NEWS_INDUSTRY_CLOSURE_GENERAL,             STR_NEWS_INDUSTRY_CLOSURE_LACK_OF_TREES);
	TEXTID_TO_STRINGID(0x4835, 0x4838, STR_NEWS_INDUSTRY_PRODUCTION_INCREASE_GENERAL, STR_NEWS_INDUSTRY_PRODUCTION_INCREASE_FARM);
	TEXTID_TO_STRINGID(0x4839, 0x483A, STR_NEWS_INDUSTRY_PRODUCTION_DECREASE_GENERAL, STR_NEWS_INDUSTRY_PRODUCTION_DECREASE_FARM);

	switch (str) {
		case 0x4830: return STR_ERROR_CAN_T_CONSTRUCT_THIS_INDUSTRY;
		case 0x4831: return STR_ERROR_FOREST_CAN_ONLY_BE_PLANTED;
		case 0x483B: return STR_ERROR_CAN_ONLY_BE_POSITIONED;
	}
#undef TEXTID_TO_STRINGID

	if (str == STR_NULL) return STR_EMPTY;

	DEBUG(grf, 0, "Unknown StringID 0x%04X remapped to STR_EMPTY. Please open a Feature Request if you need it", str);

	return STR_EMPTY;
}

/**
 * Used when setting an object's property to map to the GRF's strings
 * while taking in consideration the "drift" between TTDPatch string system and OpenTTD's one
 * @param grfid Id of the grf file.
 * @param str StringID that we want to have the equivalent in OoenTTD.
 * @return The properly adjusted StringID.
 */
StringID MapGRFStringID(uint32 grfid, StringID str)
{
	if (IsInsideMM(str, 0xD800, 0xE000)) {
		/* General text provided by NewGRF.
		 * In the specs this is called the 0xDCxx range (misc persistent texts),
		 * but we meanwhile extended the range to 0xD800-0xDFFF.
		 * Note: We are not involved in the "persistent" business, since we do not store
		 * any NewGRF strings in savegames. */
		return GetGRFStringID(grfid, str);
	} else if (IsInsideMM(str, 0xD000, 0xD800)) {
		/* Callback text provided by NewGRF.
		 * In the specs this is called the 0xD0xx range (misc graphics texts).
		 * These texts can be returned by various callbacks.
		 *
		 * Due to how TTDP implements the GRF-local- to global-textid translation
		 * texts included via 0x80 or 0x81 control codes have to add 0x400 to the textid.
		 * We do not care about that difference and just mask out the 0x400 bit.
		 */
		str &= ~0x400;
		return GetGRFStringID(grfid, str);
	} else {
		/* The NewGRF wants to include/reference an original TTD string.
		 * Try our best to find an equivalent one. */
		return TTDPStringIDToOTTDStringIDMapping(str);
	}
}

static std::map<uint32, uint32> _grf_id_overrides;

/**
 * Set the override for a NewGRF
 * @param source_grfid The grfID which wants to override another NewGRF.
 * @param target_grfid The grfID which is being overridden.
 */
static void SetNewGRFOverride(uint32 source_grfid, uint32 target_grfid)
{
	_grf_id_overrides[source_grfid] = target_grfid;
	grfmsg(5, "SetNewGRFOverride: Added override of 0x%X to 0x%X", BSWAP32(source_grfid), BSWAP32(target_grfid));
}

/**
 * Returns the engine associated to a certain internal_id, resp. allocates it.
 * @param file NewGRF that wants to change the engine.
 * @param type Vehicle type.
 * @param internal_id Engine ID inside the NewGRF.
 * @param static_access If the engine is not present, return nullptr instead of allocating a new engine. (Used for static Action 0x04).
 * @return The requested engine.
 */
static Engine *GetNewEngine(const GRFFile *file, VehicleType type, uint16 internal_id, bool static_access = false)
{
	/* Hack for add-on GRFs that need to modify another GRF's engines. This lets
	 * them use the same engine slots. */
	uint32 scope_grfid = INVALID_GRFID; // If not using dynamic_engines, all newgrfs share their ID range
	if (_settings_game.vehicle.dynamic_engines) {
		/* If dynamic_engies is enabled, there can be multiple independent ID ranges. */
		scope_grfid = file->grfid;
		uint32 override = _grf_id_overrides[file->grfid];
		if (override != 0) {
			scope_grfid = override;
			const GRFFile *grf_match = GetFileByGRFID(override);
			if (grf_match == nullptr) {
				grfmsg(5, "Tried mapping from GRFID %x to %x but target is not loaded", BSWAP32(file->grfid), BSWAP32(override));
			} else {
				grfmsg(5, "Mapping from GRFID %x to %x", BSWAP32(file->grfid), BSWAP32(override));
			}
		}

		/* Check if the engine is registered in the override manager */
		EngineID engine = _engine_mngr.GetID(type, internal_id, scope_grfid);
		if (engine != INVALID_ENGINE) {
			Engine *e = Engine::Get(engine);
			if (e->grf_prop.grffile == nullptr) e->grf_prop.grffile = file;
			return e;
		}
	}

	/* Check if there is an unreserved slot */
	EngineID engine = _engine_mngr.GetID(type, internal_id, INVALID_GRFID);
	if (engine != INVALID_ENGINE) {
		Engine *e = Engine::Get(engine);

		if (e->grf_prop.grffile == nullptr) {
			e->grf_prop.grffile = file;
			grfmsg(5, "Replaced engine at index %d for GRFID %x, type %d, index %d", e->index, BSWAP32(file->grfid), type, internal_id);
		}

		/* Reserve the engine slot */
		if (!static_access) {
			EngineIDMapping *eid = _engine_mngr.data() + engine;
			eid->grfid           = scope_grfid; // Note: this is INVALID_GRFID if dynamic_engines is disabled, so no reservation
		}

		return e;
	}

	if (static_access) return nullptr;

	if (!Engine::CanAllocateItem()) {
		grfmsg(0, "Can't allocate any more engines");
		return nullptr;
	}

	size_t engine_pool_size = Engine::GetPoolSize();

	/* ... it's not, so create a new one based off an existing engine */
	Engine *e = new Engine(type, internal_id);
	e->grf_prop.grffile = file;

	/* Reserve the engine slot */
	assert(_engine_mngr.size() == e->index);
	_engine_mngr.push_back({
			scope_grfid, // Note: this is INVALID_GRFID if dynamic_engines is disabled, so no reservation
			internal_id,
			type,
			std::min<uint8>(internal_id, _engine_counts[type]) // substitute_id == _engine_counts[subtype] means "no substitute"
	});

	if (engine_pool_size != Engine::GetPoolSize()) {
		/* Resize temporary engine data ... */
		_gted = ReallocT(_gted, Engine::GetPoolSize());

		/* and blank the new block. */
		size_t len = (Engine::GetPoolSize() - engine_pool_size) * sizeof(*_gted);
		memset(_gted + engine_pool_size, 0, len);
	}
	if (type == VEH_TRAIN) {
		_gted[e->index].railtypelabel = GetRailTypeInfo(e->u.rail.railtype)->label;
	}

	grfmsg(5, "Created new engine at index %d for GRFID %x, type %d, index %d", e->index, BSWAP32(file->grfid), type, internal_id);

	return e;
}

/**
 * Return the ID of a new engine
 * @param file The NewGRF file providing the engine.
 * @param type The Vehicle type.
 * @param internal_id NewGRF-internal ID of the engine.
 * @return The new EngineID.
 * @note depending on the dynamic_engine setting and a possible override
 *       property the grfID may be unique or overwriting or partially re-defining
 *       properties of an existing engine.
 */
EngineID GetNewEngineID(const GRFFile *file, VehicleType type, uint16 internal_id)
{
	uint32 scope_grfid = INVALID_GRFID; // If not using dynamic_engines, all newgrfs share their ID range
	if (_settings_game.vehicle.dynamic_engines) {
		scope_grfid = file->grfid;
		uint32 override = _grf_id_overrides[file->grfid];
		if (override != 0) scope_grfid = override;
	}

	return _engine_mngr.GetID(type, internal_id, scope_grfid);
}

/**
 * Map the colour modifiers of TTDPatch to those that Open is using.
 * @param grf_sprite Pointer to the structure been modified.
 */
static void MapSpriteMappingRecolour(PalSpriteID *grf_sprite)
{
	if (HasBit(grf_sprite->pal, 14)) {
		ClrBit(grf_sprite->pal, 14);
		SetBit(grf_sprite->sprite, SPRITE_MODIFIER_OPAQUE);
	}

	if (HasBit(grf_sprite->sprite, 14)) {
		ClrBit(grf_sprite->sprite, 14);
		SetBit(grf_sprite->sprite, PALETTE_MODIFIER_TRANSPARENT);
	}

	if (HasBit(grf_sprite->sprite, 15)) {
		ClrBit(grf_sprite->sprite, 15);
		SetBit(grf_sprite->sprite, PALETTE_MODIFIER_COLOUR);
	}
}

/**
 * Read a sprite and a palette from the GRF and convert them into a format
 * suitable to OpenTTD.
 * @param buf                 Input stream.
 * @param read_flags          Whether to read TileLayoutFlags.
 * @param invert_action1_flag Set to true, if palette bit 15 means 'not from action 1'.
 * @param use_cur_spritesets  Whether to use currently referenceable action 1 sets.
 * @param feature             GrfSpecFeature to use spritesets from.
 * @param[out] grf_sprite     Read sprite and palette.
 * @param[out] max_sprite_offset  Optionally returns the number of sprites in the spriteset of the sprite. (0 if no spritset)
 * @param[out] max_palette_offset Optionally returns the number of sprites in the spriteset of the palette. (0 if no spritset)
 * @return Read TileLayoutFlags.
 */
static TileLayoutFlags ReadSpriteLayoutSprite(ByteReader *buf, bool read_flags, bool invert_action1_flag, bool use_cur_spritesets, int feature, PalSpriteID *grf_sprite, uint16 *max_sprite_offset = nullptr, uint16 *max_palette_offset = nullptr)
{
	grf_sprite->sprite = buf->ReadWord();
	grf_sprite->pal = buf->ReadWord();
	TileLayoutFlags flags = read_flags ? (TileLayoutFlags)buf->ReadWord() : TLF_NOTHING;

	MapSpriteMappingRecolour(grf_sprite);

	bool custom_sprite = HasBit(grf_sprite->pal, 15) != invert_action1_flag;
	ClrBit(grf_sprite->pal, 15);
	if (custom_sprite) {
		/* Use sprite from Action 1 */
		uint index = GB(grf_sprite->sprite, 0, 14);
		if (use_cur_spritesets && (!_cur.IsValidSpriteSet(feature, index) || _cur.GetNumEnts(feature, index) == 0)) {
			grfmsg(1, "ReadSpriteLayoutSprite: Spritelayout uses undefined custom spriteset %d", index);
			grf_sprite->sprite = SPR_IMG_QUERY;
			grf_sprite->pal = PAL_NONE;
		} else {
			SpriteID sprite = use_cur_spritesets ? _cur.GetSprite(feature, index) : index;
			if (max_sprite_offset != nullptr) *max_sprite_offset = use_cur_spritesets ? _cur.GetNumEnts(feature, index) : UINT16_MAX;
			SB(grf_sprite->sprite, 0, SPRITE_WIDTH, sprite);
			SetBit(grf_sprite->sprite, SPRITE_MODIFIER_CUSTOM_SPRITE);
		}
	} else if ((flags & TLF_SPRITE_VAR10) && !(flags & TLF_SPRITE_REG_FLAGS)) {
		grfmsg(1, "ReadSpriteLayoutSprite: Spritelayout specifies var10 value for non-action-1 sprite");
		DisableGrf(STR_NEWGRF_ERROR_INVALID_SPRITE_LAYOUT);
		return flags;
	}

	if (flags & TLF_CUSTOM_PALETTE) {
		/* Use palette from Action 1 */
		uint index = GB(grf_sprite->pal, 0, 14);
		if (use_cur_spritesets && (!_cur.IsValidSpriteSet(feature, index) || _cur.GetNumEnts(feature, index) == 0)) {
			grfmsg(1, "ReadSpriteLayoutSprite: Spritelayout uses undefined custom spriteset %d for 'palette'", index);
			grf_sprite->pal = PAL_NONE;
		} else {
			SpriteID sprite = use_cur_spritesets ? _cur.GetSprite(feature, index) : index;
			if (max_palette_offset != nullptr) *max_palette_offset = use_cur_spritesets ? _cur.GetNumEnts(feature, index) : UINT16_MAX;
			SB(grf_sprite->pal, 0, SPRITE_WIDTH, sprite);
			SetBit(grf_sprite->pal, SPRITE_MODIFIER_CUSTOM_SPRITE);
		}
	} else if ((flags & TLF_PALETTE_VAR10) && !(flags & TLF_PALETTE_REG_FLAGS)) {
		grfmsg(1, "ReadSpriteLayoutRegisters: Spritelayout specifies var10 value for non-action-1 palette");
		DisableGrf(STR_NEWGRF_ERROR_INVALID_SPRITE_LAYOUT);
		return flags;
	}

	return flags;
}

/**
 * Preprocess the TileLayoutFlags and read register modifiers from the GRF.
 * @param buf        Input stream.
 * @param flags      TileLayoutFlags to process.
 * @param is_parent  Whether the sprite is a parentsprite with a bounding box.
 * @param dts        Sprite layout to insert data into.
 * @param index      Sprite index to process; 0 for ground sprite.
 */
static void ReadSpriteLayoutRegisters(ByteReader *buf, TileLayoutFlags flags, bool is_parent, NewGRFSpriteLayout *dts, uint index)
{
	if (!(flags & TLF_DRAWING_FLAGS)) return;

	if (dts->registers == nullptr) dts->AllocateRegisters();
	TileLayoutRegisters &regs = const_cast<TileLayoutRegisters&>(dts->registers[index]);
	regs.flags = flags & TLF_DRAWING_FLAGS;

	if (flags & TLF_DODRAW)  regs.dodraw  = buf->ReadByte();
	if (flags & TLF_SPRITE)  regs.sprite  = buf->ReadByte();
	if (flags & TLF_PALETTE) regs.palette = buf->ReadByte();

	if (is_parent) {
		if (flags & TLF_BB_XY_OFFSET) {
			regs.delta.parent[0] = buf->ReadByte();
			regs.delta.parent[1] = buf->ReadByte();
		}
		if (flags & TLF_BB_Z_OFFSET)    regs.delta.parent[2] = buf->ReadByte();
	} else {
		if (flags & TLF_CHILD_X_OFFSET) regs.delta.child[0]  = buf->ReadByte();
		if (flags & TLF_CHILD_Y_OFFSET) regs.delta.child[1]  = buf->ReadByte();
	}

	if (flags & TLF_SPRITE_VAR10) {
		regs.sprite_var10 = buf->ReadByte();
		if (regs.sprite_var10 > TLR_MAX_VAR10) {
			grfmsg(1, "ReadSpriteLayoutRegisters: Spritelayout specifies var10 (%d) exceeding the maximal allowed value %d", regs.sprite_var10, TLR_MAX_VAR10);
			DisableGrf(STR_NEWGRF_ERROR_INVALID_SPRITE_LAYOUT);
			return;
		}
	}

	if (flags & TLF_PALETTE_VAR10) {
		regs.palette_var10 = buf->ReadByte();
		if (regs.palette_var10 > TLR_MAX_VAR10) {
			grfmsg(1, "ReadSpriteLayoutRegisters: Spritelayout specifies var10 (%d) exceeding the maximal allowed value %d", regs.palette_var10, TLR_MAX_VAR10);
			DisableGrf(STR_NEWGRF_ERROR_INVALID_SPRITE_LAYOUT);
			return;
		}
	}
}

/**
 * Read a spritelayout from the GRF.
 * @param buf                  Input
 * @param num_building_sprites Number of building sprites to read
 * @param use_cur_spritesets   Whether to use currently referenceable action 1 sets.
 * @param feature              GrfSpecFeature to use spritesets from.
 * @param allow_var10          Whether the spritelayout may specify var10 values for resolving multiple action-1-2-3 chains
 * @param no_z_position        Whether bounding boxes have no Z offset
 * @param dts                  Layout container to output into
 * @return True on error (GRF was disabled).
 */
static bool ReadSpriteLayout(ByteReader *buf, uint num_building_sprites, bool use_cur_spritesets, byte feature, bool allow_var10, bool no_z_position, NewGRFSpriteLayout *dts)
{
	bool has_flags = HasBit(num_building_sprites, 6);
	ClrBit(num_building_sprites, 6);
	TileLayoutFlags valid_flags = TLF_KNOWN_FLAGS;
	if (!allow_var10) valid_flags &= ~TLF_VAR10_FLAGS;
	dts->Allocate(num_building_sprites); // allocate before reading groundsprite flags

	uint16 *max_sprite_offset = AllocaM(uint16, num_building_sprites + 1);
	uint16 *max_palette_offset = AllocaM(uint16, num_building_sprites + 1);
	MemSetT(max_sprite_offset, 0, num_building_sprites + 1);
	MemSetT(max_palette_offset, 0, num_building_sprites + 1);

	/* Groundsprite */
	TileLayoutFlags flags = ReadSpriteLayoutSprite(buf, has_flags, false, use_cur_spritesets, feature, &dts->ground, max_sprite_offset, max_palette_offset);
	if (_cur.skip_sprites < 0) return true;

	if (flags & ~(valid_flags & ~TLF_NON_GROUND_FLAGS)) {
		grfmsg(1, "ReadSpriteLayout: Spritelayout uses invalid flag 0x%x for ground sprite", flags & ~(valid_flags & ~TLF_NON_GROUND_FLAGS));
		DisableGrf(STR_NEWGRF_ERROR_INVALID_SPRITE_LAYOUT);
		return true;
	}

	ReadSpriteLayoutRegisters(buf, flags, false, dts, 0);
	if (_cur.skip_sprites < 0) return true;

	for (uint i = 0; i < num_building_sprites; i++) {
		DrawTileSeqStruct *seq = const_cast<DrawTileSeqStruct*>(&dts->seq[i]);

		flags = ReadSpriteLayoutSprite(buf, has_flags, false, use_cur_spritesets, feature, &seq->image, max_sprite_offset + i + 1, max_palette_offset + i + 1);
		if (_cur.skip_sprites < 0) return true;

		if (flags & ~valid_flags) {
			grfmsg(1, "ReadSpriteLayout: Spritelayout uses unknown flag 0x%x", flags & ~valid_flags);
			DisableGrf(STR_NEWGRF_ERROR_INVALID_SPRITE_LAYOUT);
			return true;
		}

		seq->delta_x = buf->ReadByte();
		seq->delta_y = buf->ReadByte();

		if (!no_z_position) seq->delta_z = buf->ReadByte();

		if (seq->IsParentSprite()) {
			seq->size_x = buf->ReadByte();
			seq->size_y = buf->ReadByte();
			seq->size_z = buf->ReadByte();
		}

		ReadSpriteLayoutRegisters(buf, flags, seq->IsParentSprite(), dts, i + 1);
		if (_cur.skip_sprites < 0) return true;
	}

	/* Check if the number of sprites per spriteset is consistent */
	bool is_consistent = true;
	dts->consistent_max_offset = 0;
	for (uint i = 0; i < num_building_sprites + 1; i++) {
		if (max_sprite_offset[i] > 0) {
			if (dts->consistent_max_offset == 0) {
				dts->consistent_max_offset = max_sprite_offset[i];
			} else if (dts->consistent_max_offset != max_sprite_offset[i]) {
				is_consistent = false;
				break;
			}
		}
		if (max_palette_offset[i] > 0) {
			if (dts->consistent_max_offset == 0) {
				dts->consistent_max_offset = max_palette_offset[i];
			} else if (dts->consistent_max_offset != max_palette_offset[i]) {
				is_consistent = false;
				break;
			}
		}
	}

	/* When the Action1 sets are unknown, everything should be 0 (no spriteset usage) or UINT16_MAX (some spriteset usage) */
	assert(use_cur_spritesets || (is_consistent && (dts->consistent_max_offset == 0 || dts->consistent_max_offset == UINT16_MAX)));

	if (!is_consistent || dts->registers != nullptr) {
		dts->consistent_max_offset = 0;
		if (dts->registers == nullptr) dts->AllocateRegisters();

		for (uint i = 0; i < num_building_sprites + 1; i++) {
			TileLayoutRegisters &regs = const_cast<TileLayoutRegisters&>(dts->registers[i]);
			regs.max_sprite_offset = max_sprite_offset[i];
			regs.max_palette_offset = max_palette_offset[i];
		}
	}

	return false;
}

/**
 * Translate the refit mask. refit_mask is uint32 as it has not been mapped to CargoTypes.
 */
static CargoTypes TranslateRefitMask(uint32 refit_mask)
{
	CargoTypes result = 0;
	uint8 bit;
	FOR_EACH_SET_BIT(bit, refit_mask) {
		CargoID cargo = GetCargoTranslation(bit, _cur.grffile, true);
		if (cargo != CT_INVALID) SetBit(result, cargo);
	}
	return result;
}

/**
 * Converts TTD(P) Base Price pointers into the enum used by OTTD
 * See http://wiki.ttdpatch.net/tiki-index.php?page=BaseCosts
 * @param base_pointer TTD(P) Base Price Pointer
 * @param error_location Function name for grf error messages
 * @param[out] index If \a base_pointer is valid, \a index is assigned to the matching price; else it is left unchanged
 */
static void ConvertTTDBasePrice(uint32 base_pointer, const char *error_location, Price *index)
{
	/* Special value for 'none' */
	if (base_pointer == 0) {
		*index = INVALID_PRICE;
		return;
	}

	static const uint32 start = 0x4B34; ///< Position of first base price
	static const uint32 size  = 6;      ///< Size of each base price record

	if (base_pointer < start || (base_pointer - start) % size != 0 || (base_pointer - start) / size >= PR_END) {
		grfmsg(1, "%s: Unsupported running cost base 0x%04X, ignoring", error_location, base_pointer);
		return;
	}

	*index = (Price)((base_pointer - start) / size);
}

/** Possible return values for the FeatureChangeInfo functions */
enum ChangeInfoResult {
	CIR_SUCCESS,    ///< Variable was parsed and read
	CIR_DISABLED,   ///< GRF was disabled due to error
	CIR_UNHANDLED,  ///< Variable was parsed but unread
	CIR_UNKNOWN,    ///< Variable is unknown
	CIR_INVALID_ID, ///< Attempt to modify an invalid ID
};

typedef ChangeInfoResult (*VCI_Handler)(uint engine, int numinfo, int prop, ByteReader *buf);

/**
 * Define properties common to all vehicles
 * @param ei Engine info.
 * @param prop The property to change.
 * @param buf The property value.
 * @return ChangeInfoResult.
 */
static ChangeInfoResult CommonVehicleChangeInfo(EngineInfo *ei, int prop, ByteReader *buf)
{
	switch (prop) {
		case 0x00: // Introduction date
			ei->base_intro = buf->ReadWord() + DAYS_TILL_ORIGINAL_BASE_YEAR;
			break;

		case 0x02: // Decay speed
			ei->decay_speed = buf->ReadByte();
			break;

		case 0x03: // Vehicle life
			ei->lifelength = buf->ReadByte();
			break;

		case 0x04: // Model life
			ei->base_life = buf->ReadByte();
			break;

		case 0x06: // Climates available
			ei->climates = buf->ReadByte();
			break;

		case PROP_VEHICLE_LOAD_AMOUNT: // 0x07 Loading speed
			/* Amount of cargo loaded during a vehicle's "loading tick" */
			ei->load_amount = buf->ReadByte();
			break;

		default:
			return CIR_UNKNOWN;
	}

	return CIR_SUCCESS;
}

/**
 * Define properties for rail vehicles
 * @param engine :ocal ID of the first vehicle.
 * @param numinfo Number of subsequent IDs to change the property for.
 * @param prop The property to change.
 * @param buf The property value.
 * @return ChangeInfoResult.
 */
static ChangeInfoResult RailVehicleChangeInfo(uint engine, int numinfo, int prop, ByteReader *buf)
{
	ChangeInfoResult ret = CIR_SUCCESS;

	for (int i = 0; i < numinfo; i++) {
		Engine *e = GetNewEngine(_cur.grffile, VEH_TRAIN, engine + i);
		if (e == nullptr) return CIR_INVALID_ID; // No engine could be allocated, so neither can any next vehicles

		EngineInfo *ei = &e->info;
		RailVehicleInfo *rvi = &e->u.rail;

		switch (prop) {
			case 0x05: { // Track type
				uint8 tracktype = buf->ReadByte();

				if (tracktype < _cur.grffile->railtype_list.size()) {
					_gted[e->index].railtypelabel = _cur.grffile->railtype_list[tracktype];
					break;
				}

				switch (tracktype) {
					case 0: _gted[e->index].railtypelabel = rvi->engclass >= 2 ? RAILTYPE_ELECTRIC_LABEL : RAILTYPE_RAIL_LABEL; break;
					case 1: _gted[e->index].railtypelabel = RAILTYPE_MONO_LABEL; break;
					case 2: _gted[e->index].railtypelabel = RAILTYPE_MAGLEV_LABEL; break;
					default:
						grfmsg(1, "RailVehicleChangeInfo: Invalid track type %d specified, ignoring", tracktype);
						break;
				}
				break;
			}

			case 0x08: // AI passenger service
				/* Tells the AI that this engine is designed for
				 * passenger services and shouldn't be used for freight. */
				rvi->ai_passenger_only = buf->ReadByte();
				break;

			case PROP_TRAIN_SPEED: { // 0x09 Speed (1 unit is 1 km-ish/h)
				uint16 speed = buf->ReadWord();
				if (speed == 0xFFFF) speed = 0;

				rvi->max_speed = speed;
				break;
			}

			case PROP_TRAIN_POWER: // 0x0B Power
				rvi->power = buf->ReadWord();

				/* Set engine / wagon state based on power */
				if (rvi->power != 0) {
					if (rvi->railveh_type == RAILVEH_WAGON) {
						rvi->railveh_type = RAILVEH_SINGLEHEAD;
					}
				} else {
					rvi->railveh_type = RAILVEH_WAGON;
				}
				break;

			case PROP_TRAIN_RUNNING_COST_FACTOR: // 0x0D Running cost factor
				rvi->running_cost = buf->ReadByte();
				break;

			case 0x0E: // Running cost base
				ConvertTTDBasePrice(buf->ReadDWord(), "RailVehicleChangeInfo", &rvi->running_cost_class);
				break;

			case 0x12: { // Sprite ID
				uint8 spriteid = buf->ReadByte();
				uint8 orig_spriteid = spriteid;

				/* TTD sprite IDs point to a location in a 16bit array, but we use it
				 * as an array index, so we need it to be half the original value. */
				if (spriteid < 0xFD) spriteid >>= 1;

				if (IsValidNewGRFImageIndex<VEH_TRAIN>(spriteid)) {
					rvi->image_index = spriteid;
				} else {
					grfmsg(1, "RailVehicleChangeInfo: Invalid Sprite %d specified, ignoring", orig_spriteid);
					rvi->image_index = 0;
				}
				break;
			}

			case 0x13: { // Dual-headed
				uint8 dual = buf->ReadByte();

				if (dual != 0) {
					rvi->railveh_type = RAILVEH_MULTIHEAD;
				} else {
					rvi->railveh_type = rvi->power == 0 ?
						RAILVEH_WAGON : RAILVEH_SINGLEHEAD;
				}
				break;
			}

			case PROP_TRAIN_CARGO_CAPACITY: // 0x14 Cargo capacity
				rvi->capacity = buf->ReadByte();
				break;

			case 0x15: { // Cargo type
				_gted[e->index].defaultcargo_grf = _cur.grffile;
				uint8 ctype = buf->ReadByte();

				if (ctype == 0xFF) {
					/* 0xFF is specified as 'use first refittable' */
					ei->cargo_type = CT_INVALID;
				} else if (_cur.grffile->grf_version >= 8) {
					/* Use translated cargo. Might result in CT_INVALID (first refittable), if cargo is not defined. */
					ei->cargo_type = GetCargoTranslation(ctype, _cur.grffile);
				} else if (ctype < NUM_CARGO) {
					/* Use untranslated cargo. */
					ei->cargo_type = ctype;
				} else {
					ei->cargo_type = CT_INVALID;
					grfmsg(2, "RailVehicleChangeInfo: Invalid cargo type %d, using first refittable", ctype);
				}
				break;
			}

			case PROP_TRAIN_WEIGHT: // 0x16 Weight
				SB(rvi->weight, 0, 8, buf->ReadByte());
				break;

			case PROP_TRAIN_COST_FACTOR: // 0x17 Cost factor
				rvi->cost_factor = buf->ReadByte();
				break;

			case 0x18: // AI rank
				grfmsg(2, "RailVehicleChangeInfo: Property 0x18 'AI rank' not used by NoAI, ignored.");
				buf->ReadByte();
				break;

			case 0x19: { // Engine traction type
				/* What do the individual numbers mean?
				 * 0x00 .. 0x07: Steam
				 * 0x08 .. 0x27: Diesel
				 * 0x28 .. 0x31: Electric
				 * 0x32 .. 0x37: Monorail
				 * 0x38 .. 0x41: Maglev
				 */
				uint8 traction = buf->ReadByte();
				EngineClass engclass;

				if (traction <= 0x07) {
					engclass = EC_STEAM;
				} else if (traction <= 0x27) {
					engclass = EC_DIESEL;
				} else if (traction <= 0x31) {
					engclass = EC_ELECTRIC;
				} else if (traction <= 0x37) {
					engclass = EC_MONORAIL;
				} else if (traction <= 0x41) {
					engclass = EC_MAGLEV;
				} else {
					break;
				}

				if (_cur.grffile->railtype_list.size() == 0) {
					/* Use traction type to select between normal and electrified
					 * rail only when no translation list is in place. */
					if (_gted[e->index].railtypelabel == RAILTYPE_RAIL_LABEL     && engclass >= EC_ELECTRIC) _gted[e->index].railtypelabel = RAILTYPE_ELECTRIC_LABEL;
					if (_gted[e->index].railtypelabel == RAILTYPE_ELECTRIC_LABEL && engclass  < EC_ELECTRIC) _gted[e->index].railtypelabel = RAILTYPE_RAIL_LABEL;
				}

				rvi->engclass = engclass;
				break;
			}

			case 0x1A: // Alter purchase list sort order
				AlterVehicleListOrder(e->index, buf->ReadExtendedByte());
				break;

			case 0x1B: // Powered wagons power bonus
				rvi->pow_wag_power = buf->ReadWord();
				break;

			case 0x1C: // Refit cost
				ei->refit_cost = buf->ReadByte();
				break;

			case 0x1D: { // Refit cargo
				uint32 mask = buf->ReadDWord();
				_gted[e->index].UpdateRefittability(mask != 0);
				ei->refit_mask = TranslateRefitMask(mask);
				_gted[e->index].defaultcargo_grf = _cur.grffile;
				break;
			}

			case 0x1E: // Callback
				ei->callback_mask = buf->ReadByte();
				break;

			case PROP_TRAIN_TRACTIVE_EFFORT: // 0x1F Tractive effort coefficient
				rvi->tractive_effort = buf->ReadByte();
				break;

			case 0x20: // Air drag
				rvi->air_drag = buf->ReadByte();
				break;

			case PROP_TRAIN_SHORTEN_FACTOR: // 0x21 Shorter vehicle
				rvi->shorten_factor = buf->ReadByte();
				break;

			case 0x22: // Visual effect
				rvi->visual_effect = buf->ReadByte();
				/* Avoid accidentally setting visual_effect to the default value
				 * Since bit 6 (disable effects) is set anyways, we can safely erase some bits. */
				if (rvi->visual_effect == VE_DEFAULT) {
					assert(HasBit(rvi->visual_effect, VE_DISABLE_EFFECT));
					SB(rvi->visual_effect, VE_TYPE_START, VE_TYPE_COUNT, 0);
				}
				break;

			case 0x23: // Powered wagons weight bonus
				rvi->pow_wag_weight = buf->ReadByte();
				break;

			case 0x24: { // High byte of vehicle weight
				byte weight = buf->ReadByte();

				if (weight > 4) {
					grfmsg(2, "RailVehicleChangeInfo: Nonsensical weight of %d tons, ignoring", weight << 8);
				} else {
					SB(rvi->weight, 8, 8, weight);
				}
				break;
			}

			case PROP_TRAIN_USER_DATA: // 0x25 User-defined bit mask to set when checking veh. var. 42
				rvi->user_def_data = buf->ReadByte();
				break;

			case 0x26: // Retire vehicle early
				ei->retire_early = buf->ReadByte();
				break;

			case 0x27: // Miscellaneous flags
				ei->misc_flags = buf->ReadByte();
				_loaded_newgrf_features.has_2CC |= HasBit(ei->misc_flags, EF_USES_2CC);
				_gted[e->index].prop27_set = true;
				break;

			case 0x28: // Cargo classes allowed
				_gted[e->index].cargo_allowed = buf->ReadWord();
				_gted[e->index].UpdateRefittability(_gted[e->index].cargo_allowed != 0);
				_gted[e->index].defaultcargo_grf = _cur.grffile;
				break;

			case 0x29: // Cargo classes disallowed
				_gted[e->index].cargo_disallowed = buf->ReadWord();
				_gted[e->index].UpdateRefittability(false);
				break;

			case 0x2A: // Long format introduction date (days since year 0)
				ei->base_intro = buf->ReadDWord();
				break;

			case PROP_TRAIN_CARGO_AGE_PERIOD: // 0x2B Cargo aging period
				ei->cargo_age_period = buf->ReadWord();
				break;

			case 0x2C:   // CTT refit include list
			case 0x2D: { // CTT refit exclude list
				uint8 count = buf->ReadByte();
				_gted[e->index].UpdateRefittability(prop == 0x2C && count != 0);
				if (prop == 0x2C) _gted[e->index].defaultcargo_grf = _cur.grffile;
				CargoTypes &ctt = prop == 0x2C ? _gted[e->index].ctt_include_mask : _gted[e->index].ctt_exclude_mask;
				ctt = 0;
				while (count--) {
					CargoID ctype = GetCargoTranslation(buf->ReadByte(), _cur.grffile);
					if (ctype == CT_INVALID) continue;
					SetBit(ctt, ctype);
				}
				break;
			}

			default:
				ret = CommonVehicleChangeInfo(ei, prop, buf);
				break;
		}
	}

	return ret;
}

/**
 * Define properties for road vehicles
 * @param engine Local ID of the first vehicle.
 * @param numinfo Number of subsequent IDs to change the property for.
 * @param prop The property to change.
 * @param buf The property value.
 * @return ChangeInfoResult.
 */
static ChangeInfoResult RoadVehicleChangeInfo(uint engine, int numinfo, int prop, ByteReader *buf)
{
	ChangeInfoResult ret = CIR_SUCCESS;

	for (int i = 0; i < numinfo; i++) {
		Engine *e = GetNewEngine(_cur.grffile, VEH_ROAD, engine + i);
		if (e == nullptr) return CIR_INVALID_ID; // No engine could be allocated, so neither can any next vehicles

		EngineInfo *ei = &e->info;
		RoadVehicleInfo *rvi = &e->u.road;

		switch (prop) {
			case 0x05: // Road/tram type
				/* RoadTypeLabel is looked up later after the engine's road/tram
				 * flag is set, however 0 means the value has not been set. */
				_gted[e->index].roadtramtype = buf->ReadByte() + 1;
				break;

			case 0x08: // Speed (1 unit is 0.5 kmh)
				rvi->max_speed = buf->ReadByte();
				break;

			case PROP_ROADVEH_RUNNING_COST_FACTOR: // 0x09 Running cost factor
				rvi->running_cost = buf->ReadByte();
				break;

			case 0x0A: // Running cost base
				ConvertTTDBasePrice(buf->ReadDWord(), "RoadVehicleChangeInfo", &rvi->running_cost_class);
				break;

			case 0x0E: { // Sprite ID
				uint8 spriteid = buf->ReadByte();
				uint8 orig_spriteid = spriteid;

				/* cars have different custom id in the GRF file */
				if (spriteid == 0xFF) spriteid = 0xFD;

				if (spriteid < 0xFD) spriteid >>= 1;

				if (IsValidNewGRFImageIndex<VEH_ROAD>(spriteid)) {
					rvi->image_index = spriteid;
				} else {
					grfmsg(1, "RoadVehicleChangeInfo: Invalid Sprite %d specified, ignoring", orig_spriteid);
					rvi->image_index = 0;
				}
				break;
			}

			case PROP_ROADVEH_CARGO_CAPACITY: // 0x0F Cargo capacity
				rvi->capacity = buf->ReadByte();
				break;

			case 0x10: { // Cargo type
				_gted[e->index].defaultcargo_grf = _cur.grffile;
				uint8 ctype = buf->ReadByte();

				if (ctype == 0xFF) {
					/* 0xFF is specified as 'use first refittable' */
					ei->cargo_type = CT_INVALID;
				} else if (_cur.grffile->grf_version >= 8) {
					/* Use translated cargo. Might result in CT_INVALID (first refittable), if cargo is not defined. */
					ei->cargo_type = GetCargoTranslation(ctype, _cur.grffile);
				} else if (ctype < NUM_CARGO) {
					/* Use untranslated cargo. */
					ei->cargo_type = ctype;
				} else {
					ei->cargo_type = CT_INVALID;
					grfmsg(2, "RailVehicleChangeInfo: Invalid cargo type %d, using first refittable", ctype);
				}
				break;
			}

			case PROP_ROADVEH_COST_FACTOR: // 0x11 Cost factor
				rvi->cost_factor = buf->ReadByte();
				break;

			case 0x12: // SFX
				rvi->sfx = GetNewGRFSoundID(_cur.grffile, buf->ReadByte());
				break;

			case PROP_ROADVEH_POWER: // Power in units of 10 HP.
				rvi->power = buf->ReadByte();
				break;

			case PROP_ROADVEH_WEIGHT: // Weight in units of 1/4 tons.
				rvi->weight = buf->ReadByte();
				break;

			case PROP_ROADVEH_SPEED: // Speed in mph/0.8
				_gted[e->index].rv_max_speed = buf->ReadByte();
				break;

			case 0x16: { // Cargoes available for refitting
				uint32 mask = buf->ReadDWord();
				_gted[e->index].UpdateRefittability(mask != 0);
				ei->refit_mask = TranslateRefitMask(mask);
				_gted[e->index].defaultcargo_grf = _cur.grffile;
				break;
			}

			case 0x17: // Callback mask
				ei->callback_mask = buf->ReadByte();
				break;

			case PROP_ROADVEH_TRACTIVE_EFFORT: // Tractive effort coefficient in 1/256.
				rvi->tractive_effort = buf->ReadByte();
				break;

			case 0x19: // Air drag
				rvi->air_drag = buf->ReadByte();
				break;

			case 0x1A: // Refit cost
				ei->refit_cost = buf->ReadByte();
				break;

			case 0x1B: // Retire vehicle early
				ei->retire_early = buf->ReadByte();
				break;

			case 0x1C: // Miscellaneous flags
				ei->misc_flags = buf->ReadByte();
				_loaded_newgrf_features.has_2CC |= HasBit(ei->misc_flags, EF_USES_2CC);
				break;

			case 0x1D: // Cargo classes allowed
				_gted[e->index].cargo_allowed = buf->ReadWord();
				_gted[e->index].UpdateRefittability(_gted[e->index].cargo_allowed != 0);
				_gted[e->index].defaultcargo_grf = _cur.grffile;
				break;

			case 0x1E: // Cargo classes disallowed
				_gted[e->index].cargo_disallowed = buf->ReadWord();
				_gted[e->index].UpdateRefittability(false);
				break;

			case 0x1F: // Long format introduction date (days since year 0)
				ei->base_intro = buf->ReadDWord();
				break;

			case 0x20: // Alter purchase list sort order
				AlterVehicleListOrder(e->index, buf->ReadExtendedByte());
				break;

			case 0x21: // Visual effect
				rvi->visual_effect = buf->ReadByte();
				/* Avoid accidentally setting visual_effect to the default value
				 * Since bit 6 (disable effects) is set anyways, we can safely erase some bits. */
				if (rvi->visual_effect == VE_DEFAULT) {
					assert(HasBit(rvi->visual_effect, VE_DISABLE_EFFECT));
					SB(rvi->visual_effect, VE_TYPE_START, VE_TYPE_COUNT, 0);
				}
				break;

			case PROP_ROADVEH_CARGO_AGE_PERIOD: // 0x22 Cargo aging period
				ei->cargo_age_period = buf->ReadWord();
				break;

			case PROP_ROADVEH_SHORTEN_FACTOR: // 0x23 Shorter vehicle
				rvi->shorten_factor = buf->ReadByte();
				break;

			case 0x24:   // CTT refit include list
			case 0x25: { // CTT refit exclude list
				uint8 count = buf->ReadByte();
				_gted[e->index].UpdateRefittability(prop == 0x24 && count != 0);
				if (prop == 0x24) _gted[e->index].defaultcargo_grf = _cur.grffile;
				CargoTypes &ctt = prop == 0x24 ? _gted[e->index].ctt_include_mask : _gted[e->index].ctt_exclude_mask;
				ctt = 0;
				while (count--) {
					CargoID ctype = GetCargoTranslation(buf->ReadByte(), _cur.grffile);
					if (ctype == CT_INVALID) continue;
					SetBit(ctt, ctype);
				}
				break;
			}

			default:
				ret = CommonVehicleChangeInfo(ei, prop, buf);
				break;
		}
	}

	return ret;
}

/**
 * Define properties for ships
 * @param engine Local ID of the first vehicle.
 * @param numinfo Number of subsequent IDs to change the property for.
 * @param prop The property to change.
 * @param buf The property value.
 * @return ChangeInfoResult.
 */
static ChangeInfoResult ShipVehicleChangeInfo(uint engine, int numinfo, int prop, ByteReader *buf)
{
	ChangeInfoResult ret = CIR_SUCCESS;

	for (int i = 0; i < numinfo; i++) {
		Engine *e = GetNewEngine(_cur.grffile, VEH_SHIP, engine + i);
		if (e == nullptr) return CIR_INVALID_ID; // No engine could be allocated, so neither can any next vehicles

		EngineInfo *ei = &e->info;
		ShipVehicleInfo *svi = &e->u.ship;

		switch (prop) {
			case 0x08: { // Sprite ID
				uint8 spriteid = buf->ReadByte();
				uint8 orig_spriteid = spriteid;

				/* ships have different custom id in the GRF file */
				if (spriteid == 0xFF) spriteid = 0xFD;

				if (spriteid < 0xFD) spriteid >>= 1;

				if (IsValidNewGRFImageIndex<VEH_SHIP>(spriteid)) {
					svi->image_index = spriteid;
				} else {
					grfmsg(1, "ShipVehicleChangeInfo: Invalid Sprite %d specified, ignoring", orig_spriteid);
					svi->image_index = 0;
				}
				break;
			}

			case 0x09: // Refittable
				svi->old_refittable = (buf->ReadByte() != 0);
				break;

			case PROP_SHIP_COST_FACTOR: // 0x0A Cost factor
				svi->cost_factor = buf->ReadByte();
				break;

			case PROP_SHIP_SPEED: // 0x0B Speed (1 unit is 0.5 km-ish/h)
				svi->max_speed = buf->ReadByte();
				break;

			case 0x0C: { // Cargo type
				_gted[e->index].defaultcargo_grf = _cur.grffile;
				uint8 ctype = buf->ReadByte();

				if (ctype == 0xFF) {
					/* 0xFF is specified as 'use first refittable' */
					ei->cargo_type = CT_INVALID;
				} else if (_cur.grffile->grf_version >= 8) {
					/* Use translated cargo. Might result in CT_INVALID (first refittable), if cargo is not defined. */
					ei->cargo_type = GetCargoTranslation(ctype, _cur.grffile);
				} else if (ctype < NUM_CARGO) {
					/* Use untranslated cargo. */
					ei->cargo_type = ctype;
				} else {
					ei->cargo_type = CT_INVALID;
					grfmsg(2, "RailVehicleChangeInfo: Invalid cargo type %d, using first refittable", ctype);
				}
				break;
			}

			case PROP_SHIP_CARGO_CAPACITY: // 0x0D Cargo capacity
				svi->capacity = buf->ReadWord();
				break;

			case PROP_SHIP_RUNNING_COST_FACTOR: // 0x0F Running cost factor
				svi->running_cost = buf->ReadByte();
				break;

			case 0x10: // SFX
				svi->sfx = GetNewGRFSoundID(_cur.grffile, buf->ReadByte());
				break;

			case 0x11: { // Cargoes available for refitting
				uint32 mask = buf->ReadDWord();
				_gted[e->index].UpdateRefittability(mask != 0);
				ei->refit_mask = TranslateRefitMask(mask);
				_gted[e->index].defaultcargo_grf = _cur.grffile;
				break;
			}

			case 0x12: // Callback mask
				ei->callback_mask = buf->ReadByte();
				break;

			case 0x13: // Refit cost
				ei->refit_cost = buf->ReadByte();
				break;

			case 0x14: // Ocean speed fraction
				svi->ocean_speed_frac = buf->ReadByte();
				break;

			case 0x15: // Canal speed fraction
				svi->canal_speed_frac = buf->ReadByte();
				break;

			case 0x16: // Retire vehicle early
				ei->retire_early = buf->ReadByte();
				break;

			case 0x17: // Miscellaneous flags
				ei->misc_flags = buf->ReadByte();
				_loaded_newgrf_features.has_2CC |= HasBit(ei->misc_flags, EF_USES_2CC);
				break;

			case 0x18: // Cargo classes allowed
				_gted[e->index].cargo_allowed = buf->ReadWord();
				_gted[e->index].UpdateRefittability(_gted[e->index].cargo_allowed != 0);
				_gted[e->index].defaultcargo_grf = _cur.grffile;
				break;

			case 0x19: // Cargo classes disallowed
				_gted[e->index].cargo_disallowed = buf->ReadWord();
				_gted[e->index].UpdateRefittability(false);
				break;

			case 0x1A: // Long format introduction date (days since year 0)
				ei->base_intro = buf->ReadDWord();
				break;

			case 0x1B: // Alter purchase list sort order
				AlterVehicleListOrder(e->index, buf->ReadExtendedByte());
				break;

			case 0x1C: // Visual effect
				svi->visual_effect = buf->ReadByte();
				/* Avoid accidentally setting visual_effect to the default value
				 * Since bit 6 (disable effects) is set anyways, we can safely erase some bits. */
				if (svi->visual_effect == VE_DEFAULT) {
					assert(HasBit(svi->visual_effect, VE_DISABLE_EFFECT));
					SB(svi->visual_effect, VE_TYPE_START, VE_TYPE_COUNT, 0);
				}
				break;

			case PROP_SHIP_CARGO_AGE_PERIOD: // 0x1D Cargo aging period
				ei->cargo_age_period = buf->ReadWord();
				break;

			case 0x1E:   // CTT refit include list
			case 0x1F: { // CTT refit exclude list
				uint8 count = buf->ReadByte();
				_gted[e->index].UpdateRefittability(prop == 0x1E && count != 0);
				if (prop == 0x1E) _gted[e->index].defaultcargo_grf = _cur.grffile;
				CargoTypes &ctt = prop == 0x1E ? _gted[e->index].ctt_include_mask : _gted[e->index].ctt_exclude_mask;
				ctt = 0;
				while (count--) {
					CargoID ctype = GetCargoTranslation(buf->ReadByte(), _cur.grffile);
					if (ctype == CT_INVALID) continue;
					SetBit(ctt, ctype);
				}
				break;
			}

			default:
				ret = CommonVehicleChangeInfo(ei, prop, buf);
				break;
		}
	}

	return ret;
}

/**
 * Define properties for aircraft
 * @param engine Local ID of the aircraft.
 * @param numinfo Number of subsequent IDs to change the property for.
 * @param prop The property to change.
 * @param buf The property value.
 * @return ChangeInfoResult.
 */
static ChangeInfoResult AircraftVehicleChangeInfo(uint engine, int numinfo, int prop, ByteReader *buf)
{
	ChangeInfoResult ret = CIR_SUCCESS;

	for (int i = 0; i < numinfo; i++) {
		Engine *e = GetNewEngine(_cur.grffile, VEH_AIRCRAFT, engine + i);
		if (e == nullptr) return CIR_INVALID_ID; // No engine could be allocated, so neither can any next vehicles

		EngineInfo *ei = &e->info;
		AircraftVehicleInfo *avi = &e->u.air;

		switch (prop) {
			case 0x08: { // Sprite ID
				uint8 spriteid = buf->ReadByte();
				uint8 orig_spriteid = spriteid;

				/* aircraft have different custom id in the GRF file */
				if (spriteid == 0xFF) spriteid = 0xFD;

				if (spriteid < 0xFD) spriteid >>= 1;

				if (IsValidNewGRFImageIndex<VEH_AIRCRAFT>(spriteid)) {
					avi->image_index = spriteid;
				} else {
					grfmsg(1, "AircraftVehicleChangeInfo: Invalid Sprite %d specified, ignoring", orig_spriteid);
					avi->image_index = 0;
				}
				break;
			}

			case 0x09: // Helicopter
				if (buf->ReadByte() == 0) {
					avi->subtype = AIR_HELI;
				} else {
					SB(avi->subtype, 0, 1, 1); // AIR_CTOL
				}
				break;

			case 0x0A: // Large
				SB(avi->subtype, 1, 1, (buf->ReadByte() != 0 ? 1 : 0)); // AIR_FAST
				break;

			case PROP_AIRCRAFT_COST_FACTOR: // 0x0B Cost factor
				avi->cost_factor = buf->ReadByte();
				break;

			case PROP_AIRCRAFT_SPEED: // 0x0C Speed (1 unit is 8 mph, we translate to 1 unit is 1 km-ish/h)
				avi->max_speed = (buf->ReadByte() * 128) / 10;
				break;

			case 0x0D: // Acceleration
				avi->acceleration = buf->ReadByte();
				break;

			case PROP_AIRCRAFT_RUNNING_COST_FACTOR: // 0x0E Running cost factor
				avi->running_cost = buf->ReadByte();
				break;

			case PROP_AIRCRAFT_PASSENGER_CAPACITY: // 0x0F Passenger capacity
				avi->passenger_capacity = buf->ReadWord();
				break;

			case PROP_AIRCRAFT_MAIL_CAPACITY: // 0x11 Mail capacity
				avi->mail_capacity = buf->ReadByte();
				break;

			case 0x12: // SFX
				avi->sfx = GetNewGRFSoundID(_cur.grffile, buf->ReadByte());
				break;

			case 0x13: { // Cargoes available for refitting
				uint32 mask = buf->ReadDWord();
				_gted[e->index].UpdateRefittability(mask != 0);
				ei->refit_mask = TranslateRefitMask(mask);
				_gted[e->index].defaultcargo_grf = _cur.grffile;
				break;
			}

			case 0x14: // Callback mask
				ei->callback_mask = buf->ReadByte();
				break;

			case 0x15: // Refit cost
				ei->refit_cost = buf->ReadByte();
				break;

			case 0x16: // Retire vehicle early
				ei->retire_early = buf->ReadByte();
				break;

			case 0x17: // Miscellaneous flags
				ei->misc_flags = buf->ReadByte();
				_loaded_newgrf_features.has_2CC |= HasBit(ei->misc_flags, EF_USES_2CC);
				break;

			case 0x18: // Cargo classes allowed
				_gted[e->index].cargo_allowed = buf->ReadWord();
				_gted[e->index].UpdateRefittability(_gted[e->index].cargo_allowed != 0);
				_gted[e->index].defaultcargo_grf = _cur.grffile;
				break;

			case 0x19: // Cargo classes disallowed
				_gted[e->index].cargo_disallowed = buf->ReadWord();
				_gted[e->index].UpdateRefittability(false);
				break;

			case 0x1A: // Long format introduction date (days since year 0)
				ei->base_intro = buf->ReadDWord();
				break;

			case 0x1B: // Alter purchase list sort order
				AlterVehicleListOrder(e->index, buf->ReadExtendedByte());
				break;

			case PROP_AIRCRAFT_CARGO_AGE_PERIOD: // 0x1C Cargo aging period
				ei->cargo_age_period = buf->ReadWord();
				break;

			case 0x1D:   // CTT refit include list
			case 0x1E: { // CTT refit exclude list
				uint8 count = buf->ReadByte();
				_gted[e->index].UpdateRefittability(prop == 0x1D && count != 0);
				if (prop == 0x1D) _gted[e->index].defaultcargo_grf = _cur.grffile;
				CargoTypes &ctt = prop == 0x1D ? _gted[e->index].ctt_include_mask : _gted[e->index].ctt_exclude_mask;
				ctt = 0;
				while (count--) {
					CargoID ctype = GetCargoTranslation(buf->ReadByte(), _cur.grffile);
					if (ctype == CT_INVALID) continue;
					SetBit(ctt, ctype);
				}
				break;
			}

			case PROP_AIRCRAFT_RANGE: // 0x1F Max aircraft range
				avi->max_range = buf->ReadWord();
				break;

			default:
				ret = CommonVehicleChangeInfo(ei, prop, buf);
				break;
		}
	}

	return ret;
}

/**
 * Define properties for stations
 * @param stid StationID of the first station tile.
 * @param numinfo Number of subsequent station tiles to change the property for.
 * @param prop The property to change.
 * @param buf The property value.
 * @return ChangeInfoResult.
 */
static ChangeInfoResult StationChangeInfo(uint stid, int numinfo, int prop, ByteReader *buf)
{
	ChangeInfoResult ret = CIR_SUCCESS;

	if (stid + numinfo > NUM_STATIONS_PER_GRF) {
		grfmsg(1, "StationChangeInfo: Station %u is invalid, max %u, ignoring", stid + numinfo, NUM_STATIONS_PER_GRF);
		return CIR_INVALID_ID;
	}

	/* Allocate station specs if necessary */
	if (_cur.grffile->stations == nullptr) _cur.grffile->stations = CallocT<StationSpec*>(NUM_STATIONS_PER_GRF);

	for (int i = 0; i < numinfo; i++) {
		StationSpec *statspec = _cur.grffile->stations[stid + i];

		/* Check that the station we are modifying is defined. */
		if (statspec == nullptr && prop != 0x08) {
			grfmsg(2, "StationChangeInfo: Attempt to modify undefined station %u, ignoring", stid + i);
			return CIR_INVALID_ID;
		}

		switch (prop) {
			case 0x08: { // Class ID
				StationSpec **spec = &_cur.grffile->stations[stid + i];

				/* Property 0x08 is special; it is where the station is allocated */
				if (*spec == nullptr) *spec = CallocT<StationSpec>(1);

				/* Swap classid because we read it in BE meaning WAYP or DFLT */
				uint32 classid = buf->ReadDWord();
				(*spec)->cls_id = StationClass::Allocate(BSWAP32(classid));
				break;
			}

			case 0x09: // Define sprite layout
				statspec->tiles = buf->ReadExtendedByte();
				delete[] statspec->renderdata; // delete earlier loaded stuff
				statspec->renderdata = new NewGRFSpriteLayout[statspec->tiles];

				for (uint t = 0; t < statspec->tiles; t++) {
					NewGRFSpriteLayout *dts = &statspec->renderdata[t];
					dts->consistent_max_offset = UINT16_MAX; // Spritesets are unknown, so no limit.

					if (buf->HasData(4) && *(uint32*)buf->Data() == 0) {
						buf->Skip(4);
						extern const DrawTileSprites _station_display_datas_rail[8];
						dts->Clone(&_station_display_datas_rail[t % 8]);
						continue;
					}

					ReadSpriteLayoutSprite(buf, false, false, false, GSF_STATIONS, &dts->ground);
					/* On error, bail out immediately. Temporary GRF data was already freed */
					if (_cur.skip_sprites < 0) return CIR_DISABLED;

					static std::vector<DrawTileSeqStruct> tmp_layout;
					tmp_layout.clear();
					for (;;) {
						/* no relative bounding box support */
						DrawTileSeqStruct &dtss = tmp_layout.emplace_back();
						MemSetT(&dtss, 0);

						dtss.delta_x = buf->ReadByte();
						if (dtss.IsTerminator()) break;
						dtss.delta_y = buf->ReadByte();
						dtss.delta_z = buf->ReadByte();
						dtss.size_x = buf->ReadByte();
						dtss.size_y = buf->ReadByte();
						dtss.size_z = buf->ReadByte();

						ReadSpriteLayoutSprite(buf, false, true, false, GSF_STATIONS, &dtss.image);
						/* On error, bail out immediately. Temporary GRF data was already freed */
						if (_cur.skip_sprites < 0) return CIR_DISABLED;
					}
					dts->Clone(tmp_layout.data());
				}
				break;

			case 0x0A: { // Copy sprite layout
				byte srcid = buf->ReadByte();
				const StationSpec *srcstatspec = _cur.grffile->stations[srcid];

				if (srcstatspec == nullptr) {
					grfmsg(1, "StationChangeInfo: Station %u is not defined, cannot copy sprite layout to %u.", srcid, stid + i);
					continue;
				}

				delete[] statspec->renderdata; // delete earlier loaded stuff

				statspec->tiles = srcstatspec->tiles;
				statspec->renderdata = new NewGRFSpriteLayout[statspec->tiles];
				for (uint t = 0; t < statspec->tiles; t++) {
					statspec->renderdata[t].Clone(&srcstatspec->renderdata[t]);
				}
				break;
			}

			case 0x0B: // Callback mask
				statspec->callback_mask = buf->ReadByte();
				break;

			case 0x0C: // Disallowed number of platforms
				statspec->disallowed_platforms = buf->ReadByte();
				break;

			case 0x0D: // Disallowed platform lengths
				statspec->disallowed_lengths = buf->ReadByte();
				break;

			case 0x0E: // Define custom layout
				statspec->copied_layouts = false;

				while (buf->HasData()) {
					byte length = buf->ReadByte();
					byte number = buf->ReadByte();
					StationLayout layout;
					uint l, p;

					if (length == 0 || number == 0) break;

					if (length > statspec->lengths) {
						byte diff_length = length - statspec->lengths;
						statspec->platforms = ReallocT(statspec->platforms, length);
						memset(statspec->platforms + statspec->lengths, 0, diff_length);

						statspec->layouts = ReallocT(statspec->layouts, length);
						memset(statspec->layouts + statspec->lengths, 0, diff_length * sizeof(*statspec->layouts));

						statspec->lengths = length;
					}
					l = length - 1; // index is zero-based

					if (number > statspec->platforms[l]) {
						statspec->layouts[l] = ReallocT(statspec->layouts[l], number);
						/* We expect nullptr being 0 here, but C99 guarantees that. */
						memset(statspec->layouts[l] + statspec->platforms[l], 0,
						       (number - statspec->platforms[l]) * sizeof(**statspec->layouts));

						statspec->platforms[l] = number;
					}

					p = 0;
					layout = MallocT<byte>(length * number);
					try {
						for (l = 0; l < length; l++) {
							for (p = 0; p < number; p++) {
								layout[l * number + p] = buf->ReadByte();
							}
						}
					} catch (...) {
						free(layout);
						throw;
					}

					l--;
					p--;
					free(statspec->layouts[l][p]);
					statspec->layouts[l][p] = layout;
				}
				break;

			case 0x0F: { // Copy custom layout
				byte srcid = buf->ReadByte();
				const StationSpec *srcstatspec = _cur.grffile->stations[srcid];

				if (srcstatspec == nullptr) {
					grfmsg(1, "StationChangeInfo: Station %u is not defined, cannot copy tile layout to %u.", srcid, stid + i);
					continue;
				}

				statspec->lengths   = srcstatspec->lengths;
				statspec->platforms = srcstatspec->platforms;
				statspec->layouts   = srcstatspec->layouts;
				statspec->copied_layouts = true;
				break;
			}

			case 0x10: // Little/lots cargo threshold
				statspec->cargo_threshold = buf->ReadWord();
				break;

			case 0x11: // Pylon placement
				statspec->pylons = buf->ReadByte();
				break;

			case 0x12: // Cargo types for random triggers
				if (_cur.grffile->grf_version >= 7) {
					statspec->cargo_triggers = TranslateRefitMask(buf->ReadDWord());
				} else {
					statspec->cargo_triggers = (CargoTypes)buf->ReadDWord();
				}
				break;

			case 0x13: // General flags
				statspec->flags = buf->ReadByte();
				break;

			case 0x14: // Overhead wire placement
				statspec->wires = buf->ReadByte();
				break;

			case 0x15: // Blocked tiles
				statspec->blocked = buf->ReadByte();
				break;

			case 0x16: // Animation info
				statspec->animation.frames = buf->ReadByte();
				statspec->animation.status = buf->ReadByte();
				break;

			case 0x17: // Animation speed
				statspec->animation.speed = buf->ReadByte();
				break;

			case 0x18: // Animation triggers
				statspec->animation.triggers = buf->ReadWord();
				break;

			case 0x1A: // Advanced sprite layout
				statspec->tiles = buf->ReadExtendedByte();
				delete[] statspec->renderdata; // delete earlier loaded stuff
				statspec->renderdata = new NewGRFSpriteLayout[statspec->tiles];

				for (uint t = 0; t < statspec->tiles; t++) {
					NewGRFSpriteLayout *dts = &statspec->renderdata[t];
					uint num_building_sprites = buf->ReadByte();
					/* On error, bail out immediately. Temporary GRF data was already freed */
					if (ReadSpriteLayout(buf, num_building_sprites, false, GSF_STATIONS, true, false, dts)) return CIR_DISABLED;
				}
				break;

			default:
				ret = CIR_UNKNOWN;
				break;
		}
	}

	return ret;
}

/**
 * Define properties for water features
 * @param id Type of the first water feature.
 * @param numinfo Number of subsequent water feature ids to change the property for.
 * @param prop The property to change.
 * @param buf The property value.
 * @return ChangeInfoResult.
 */
static ChangeInfoResult CanalChangeInfo(uint id, int numinfo, int prop, ByteReader *buf)
{
	ChangeInfoResult ret = CIR_SUCCESS;

	if (id + numinfo > CF_END) {
		grfmsg(1, "CanalChangeInfo: Canal feature 0x%02X is invalid, max %u, ignoring", id + numinfo, CF_END);
		return CIR_INVALID_ID;
	}

	for (int i = 0; i < numinfo; i++) {
		CanalProperties *cp = &_cur.grffile->canal_local_properties[id + i];

		switch (prop) {
			case 0x08:
				cp->callback_mask = buf->ReadByte();
				break;

			case 0x09:
				cp->flags = buf->ReadByte();
				break;

			default:
				ret = CIR_UNKNOWN;
				break;
		}
	}

	return ret;
}

/**
 * Define properties for bridges
 * @param brid BridgeID of the bridge.
 * @param numinfo Number of subsequent bridgeIDs to change the property for.
 * @param prop The property to change.
 * @param buf The property value.
 * @return ChangeInfoResult.
 */
static ChangeInfoResult BridgeChangeInfo(uint brid, int numinfo, int prop, ByteReader *buf)
{
	ChangeInfoResult ret = CIR_SUCCESS;

	if (brid + numinfo > MAX_BRIDGES) {
		grfmsg(1, "BridgeChangeInfo: Bridge %u is invalid, max %u, ignoring", brid + numinfo, MAX_BRIDGES);
		return CIR_INVALID_ID;
	}

	for (int i = 0; i < numinfo; i++) {
		BridgeSpec *bridge = &_bridge[brid + i];

		switch (prop) {
			case 0x08: { // Year of availability
				/* We treat '0' as always available */
				byte year = buf->ReadByte();
				bridge->avail_year = (year > 0 ? ORIGINAL_BASE_YEAR + year : 0);
				break;
			}

			case 0x09: // Minimum length
				bridge->min_length = buf->ReadByte();
				break;

			case 0x0A: // Maximum length
				bridge->max_length = buf->ReadByte();
				if (bridge->max_length > 16) bridge->max_length = 0xFFFF;
				break;

			case 0x0B: // Cost factor
				bridge->price = buf->ReadByte();
				break;

			case 0x0C: // Maximum speed
				bridge->speed = buf->ReadWord();
				break;

			case 0x0D: { // Bridge sprite tables
				byte tableid = buf->ReadByte();
				byte numtables = buf->ReadByte();

				if (bridge->sprite_table == nullptr) {
					/* Allocate memory for sprite table pointers and zero out */
					bridge->sprite_table = CallocT<PalSpriteID*>(7);
				}

				for (; numtables-- != 0; tableid++) {
					if (tableid >= 7) { // skip invalid data
						grfmsg(1, "BridgeChangeInfo: Table %d >= 7, skipping", tableid);
						for (byte sprite = 0; sprite < 32; sprite++) buf->ReadDWord();
						continue;
					}

					if (bridge->sprite_table[tableid] == nullptr) {
						bridge->sprite_table[tableid] = MallocT<PalSpriteID>(32);
					}

					for (byte sprite = 0; sprite < 32; sprite++) {
						SpriteID image = buf->ReadWord();
						PaletteID pal  = buf->ReadWord();

						bridge->sprite_table[tableid][sprite].sprite = image;
						bridge->sprite_table[tableid][sprite].pal    = pal;

						MapSpriteMappingRecolour(&bridge->sprite_table[tableid][sprite]);
					}
				}
				break;
			}

			case 0x0E: // Flags; bit 0 - disable far pillars
				bridge->flags = buf->ReadByte();
				break;

			case 0x0F: // Long format year of availability (year since year 0)
				bridge->avail_year = Clamp(buf->ReadDWord(), MIN_YEAR, MAX_YEAR);
				break;

			case 0x10: { // purchase string
				StringID newone = GetGRFStringID(_cur.grffile->grfid, buf->ReadWord());
				if (newone != STR_UNDEFINED) bridge->material = newone;
				break;
			}

			case 0x11: // description of bridge with rails or roads
			case 0x12: {
				StringID newone = GetGRFStringID(_cur.grffile->grfid, buf->ReadWord());
				if (newone != STR_UNDEFINED) bridge->transport_name[prop - 0x11] = newone;
				break;
			}

			case 0x13: // 16 bits cost multiplier
				bridge->price = buf->ReadWord();
				break;

			default:
				ret = CIR_UNKNOWN;
				break;
		}
	}

	return ret;
}

/**
 * Ignore a house property
 * @param prop Property to read.
 * @param buf Property value.
 * @return ChangeInfoResult.
 */
static ChangeInfoResult IgnoreTownHouseProperty(int prop, ByteReader *buf)
{
	ChangeInfoResult ret = CIR_SUCCESS;

	switch (prop) {
		case 0x09:
		case 0x0B:
		case 0x0C:
		case 0x0D:
		case 0x0E:
		case 0x0F:
		case 0x11:
		case 0x14:
		case 0x15:
		case 0x16:
		case 0x18:
		case 0x19:
		case 0x1A:
		case 0x1B:
		case 0x1C:
		case 0x1D:
		case 0x1F:
			buf->ReadByte();
			break;

		case 0x0A:
		case 0x10:
		case 0x12:
		case 0x13:
		case 0x21:
		case 0x22:
			buf->ReadWord();
			break;

		case 0x1E:
			buf->ReadDWord();
			break;

		case 0x17:
			for (uint j = 0; j < 4; j++) buf->ReadByte();
			break;

		case 0x20: {
			byte count = buf->ReadByte();
			for (byte j = 0; j < count; j++) buf->ReadByte();
			break;
		}

		case 0x23:
			buf->Skip(buf->ReadByte() * 2);
			break;

		default:
			ret = CIR_UNKNOWN;
			break;
	}
	return ret;
}

/**
 * Define properties for houses
 * @param hid HouseID of the house.
 * @param numinfo Number of subsequent houseIDs to change the property for.
 * @param prop The property to change.
 * @param buf The property value.
 * @return ChangeInfoResult.
 */
static ChangeInfoResult TownHouseChangeInfo(uint hid, int numinfo, int prop, ByteReader *buf)
{
	ChangeInfoResult ret = CIR_SUCCESS;

	if (hid + numinfo > NUM_HOUSES_PER_GRF) {
		grfmsg(1, "TownHouseChangeInfo: Too many houses loaded (%u), max (%u). Ignoring.", hid + numinfo, NUM_HOUSES_PER_GRF);
		return CIR_INVALID_ID;
	}

	/* Allocate house specs if they haven't been allocated already. */
	if (_cur.grffile->housespec == nullptr) {
		_cur.grffile->housespec = CallocT<HouseSpec*>(NUM_HOUSES_PER_GRF);
	}

	for (int i = 0; i < numinfo; i++) {
		HouseSpec *housespec = _cur.grffile->housespec[hid + i];

		if (prop != 0x08 && housespec == nullptr) {
			/* If the house property 08 is not yet set, ignore this property */
			ChangeInfoResult cir = IgnoreTownHouseProperty(prop, buf);
			if (cir > ret) ret = cir;
			continue;
		}

		switch (prop) {
			case 0x08: { // Substitute building type, and definition of a new house
				HouseSpec **house = &_cur.grffile->housespec[hid + i];
				byte subs_id = buf->ReadByte();

				if (subs_id == 0xFF) {
					/* Instead of defining a new house, a substitute house id
					 * of 0xFF disables the old house with the current id. */
					HouseSpec::Get(hid + i)->enabled = false;
					continue;
				} else if (subs_id >= NEW_HOUSE_OFFSET) {
					/* The substitute id must be one of the original houses. */
					grfmsg(2, "TownHouseChangeInfo: Attempt to use new house %u as substitute house for %u. Ignoring.", subs_id, hid + i);
					continue;
				}

				/* Allocate space for this house. */
				if (*house == nullptr) *house = CallocT<HouseSpec>(1);

				housespec = *house;

				MemCpyT(housespec, HouseSpec::Get(subs_id));

				housespec->enabled = true;
				housespec->grf_prop.local_id = hid + i;
				housespec->grf_prop.subst_id = subs_id;
				housespec->grf_prop.grffile = _cur.grffile;
				housespec->random_colour[0] = 0x04;  // those 4 random colours are the base colour
				housespec->random_colour[1] = 0x08;  // for all new houses
				housespec->random_colour[2] = 0x0C;  // they stand for red, blue, orange and green
				housespec->random_colour[3] = 0x06;

				/* Make sure that the third cargo type is valid in this
				 * climate. This can cause problems when copying the properties
				 * of a house that accepts food, where the new house is valid
				 * in the temperate climate. */
				if (!CargoSpec::Get(housespec->accepts_cargo[2])->IsValid()) {
					housespec->cargo_acceptance[2] = 0;
				}
				break;
			}

			case 0x09: // Building flags
				housespec->building_flags = (BuildingFlags)buf->ReadByte();
				break;

			case 0x0A: { // Availability years
				uint16 years = buf->ReadWord();
				housespec->min_year = GB(years, 0, 8) > 150 ? MAX_YEAR : ORIGINAL_BASE_YEAR + GB(years, 0, 8);
				housespec->max_year = GB(years, 8, 8) > 150 ? MAX_YEAR : ORIGINAL_BASE_YEAR + GB(years, 8, 8);
				break;
			}

			case 0x0B: // Population
				housespec->population = buf->ReadByte();
				break;

			case 0x0C: // Mail generation multiplier
				housespec->mail_generation = buf->ReadByte();
				break;

			case 0x0D: // Passenger acceptance
			case 0x0E: // Mail acceptance
				housespec->cargo_acceptance[prop - 0x0D] = buf->ReadByte();
				break;

			case 0x0F: { // Goods/candy, food/fizzy drinks acceptance
				int8 goods = buf->ReadByte();

				/* If value of goods is negative, it means in fact food or, if in toyland, fizzy_drink acceptance.
				 * Else, we have "standard" 3rd cargo type, goods or candy, for toyland once more */
				CargoID cid = (goods >= 0) ? ((_settings_game.game_creation.landscape == LT_TOYLAND) ? CT_CANDY : CT_GOODS) :
						((_settings_game.game_creation.landscape == LT_TOYLAND) ? CT_FIZZY_DRINKS : CT_FOOD);

				/* Make sure the cargo type is valid in this climate. */
				if (!CargoSpec::Get(cid)->IsValid()) goods = 0;

				housespec->accepts_cargo[2] = cid;
				housespec->cargo_acceptance[2] = abs(goods); // but we do need positive value here
				break;
			}

			case 0x10: // Local authority rating decrease on removal
				housespec->remove_rating_decrease = buf->ReadWord();
				break;

			case 0x11: // Removal cost multiplier
				housespec->removal_cost = buf->ReadByte();
				break;

			case 0x12: // Building name ID
				AddStringForMapping(buf->ReadWord(), &housespec->building_name);
				break;

			case 0x13: // Building availability mask
				housespec->building_availability = (HouseZones)buf->ReadWord();
				break;

			case 0x14: // House callback mask
				housespec->callback_mask |= buf->ReadByte();
				break;

			case 0x15: { // House override byte
				byte override = buf->ReadByte();

				/* The house being overridden must be an original house. */
				if (override >= NEW_HOUSE_OFFSET) {
					grfmsg(2, "TownHouseChangeInfo: Attempt to override new house %u with house id %u. Ignoring.", override, hid + i);
					continue;
				}

				_house_mngr.Add(hid + i, _cur.grffile->grfid, override);
				break;
			}

			case 0x16: // Periodic refresh multiplier
				housespec->processing_time = std::min<byte>(buf->ReadByte(), 63u);
				break;

			case 0x17: // Four random colours to use
				for (uint j = 0; j < 4; j++) housespec->random_colour[j] = buf->ReadByte();
				break;

			case 0x18: // Relative probability of appearing
				housespec->probability = buf->ReadByte();
				break;

			case 0x19: // Extra flags
				housespec->extra_flags = (HouseExtraFlags)buf->ReadByte();
				break;

			case 0x1A: // Animation frames
				housespec->animation.frames = buf->ReadByte();
				housespec->animation.status = GB(housespec->animation.frames, 7, 1);
				SB(housespec->animation.frames, 7, 1, 0);
				break;

			case 0x1B: // Animation speed
				housespec->animation.speed = Clamp(buf->ReadByte(), 2, 16);
				break;

			case 0x1C: // Class of the building type
				housespec->class_id = AllocateHouseClassID(buf->ReadByte(), _cur.grffile->grfid);
				break;

			case 0x1D: // Callback mask part 2
				housespec->callback_mask |= (buf->ReadByte() << 8);
				break;

			case 0x1E: { // Accepted cargo types
				uint32 cargotypes = buf->ReadDWord();

				/* Check if the cargo types should not be changed */
				if (cargotypes == 0xFFFFFFFF) break;

				for (uint j = 0; j < 3; j++) {
					/* Get the cargo number from the 'list' */
					uint8 cargo_part = GB(cargotypes, 8 * j, 8);
					CargoID cargo = GetCargoTranslation(cargo_part, _cur.grffile);

					if (cargo == CT_INVALID) {
						/* Disable acceptance of invalid cargo type */
						housespec->cargo_acceptance[j] = 0;
					} else {
						housespec->accepts_cargo[j] = cargo;
					}
				}
				break;
			}

			case 0x1F: // Minimum life span
				housespec->minimum_life = buf->ReadByte();
				break;

			case 0x20: { // Cargo acceptance watch list
				byte count = buf->ReadByte();
				for (byte j = 0; j < count; j++) {
					CargoID cargo = GetCargoTranslation(buf->ReadByte(), _cur.grffile);
					if (cargo != CT_INVALID) SetBit(housespec->watched_cargoes, cargo);
				}
				break;
			}

			case 0x21: // long introduction year
				housespec->min_year = buf->ReadWord();
				break;

			case 0x22: // long maximum year
				housespec->max_year = buf->ReadWord();
				break;

			case 0x23: { // variable length cargo types accepted
				uint count = buf->ReadByte();
				if (count > lengthof(housespec->accepts_cargo)) {
					GRFError *error = DisableGrf(STR_NEWGRF_ERROR_LIST_PROPERTY_TOO_LONG);
					error->param_value[1] = prop;
					return CIR_DISABLED;
				}
				/* Always write the full accepts_cargo array, and check each index for being inside the
				 * provided data. This ensures all values are properly initialized, and also avoids
				 * any risks of array overrun. */
				for (uint i = 0; i < lengthof(housespec->accepts_cargo); i++) {
					if (i < count) {
						housespec->accepts_cargo[i] = GetCargoTranslation(buf->ReadByte(), _cur.grffile);
						housespec->cargo_acceptance[i] = buf->ReadByte();
					} else {
						housespec->accepts_cargo[i] = CT_INVALID;
						housespec->cargo_acceptance[i] = 0;
					}
				}
				break;
			}

			default:
				ret = CIR_UNKNOWN;
				break;
		}
	}

	return ret;
}

/**
 * Get the language map associated with a given NewGRF and language.
 * @param grfid       The NewGRF to get the map for.
 * @param language_id The (NewGRF) language ID to get the map for.
 * @return The LanguageMap, or nullptr if it couldn't be found.
 */
/* static */ const LanguageMap *LanguageMap::GetLanguageMap(uint32 grfid, uint8 language_id)
{
	/* LanguageID "MAX_LANG", i.e. 7F is any. This language can't have a gender/case mapping, but has to be handled gracefully. */
	const GRFFile *grffile = GetFileByGRFID(grfid);
	return (grffile != nullptr && grffile->language_map != nullptr && language_id < MAX_LANG) ? &grffile->language_map[language_id] : nullptr;
}

/**
 * Load a cargo- or railtype-translation table.
 * @param gvid ID of the global variable. This is basically only checked for zerones.
 * @param numinfo Number of subsequent IDs to change the property for.
 * @param buf The property value.
 * @param[in,out] translation_table Storage location for the translation table.
 * @param name Name of the table for debug output.
 * @return ChangeInfoResult.
 */
template <typename T>
static ChangeInfoResult LoadTranslationTable(uint gvid, int numinfo, ByteReader *buf, T &translation_table, const char *name)
{
	if (gvid != 0) {
		grfmsg(1, "LoadTranslationTable: %s translation table must start at zero", name);
		return CIR_INVALID_ID;
	}

	translation_table.clear();
	for (int i = 0; i < numinfo; i++) {
		uint32 item = buf->ReadDWord();
		translation_table.push_back(BSWAP32(item));
	}

	return CIR_SUCCESS;
}

/**
 * Define properties for global variables
 * @param gvid ID of the global variable.
 * @param numinfo Number of subsequent IDs to change the property for.
 * @param prop The property to change.
 * @param buf The property value.
 * @return ChangeInfoResult.
 */
static ChangeInfoResult GlobalVarChangeInfo(uint gvid, int numinfo, int prop, ByteReader *buf)
{
	/* Properties which are handled as a whole */
	switch (prop) {
		case 0x09: // Cargo Translation Table; loading during both reservation and activation stage (in case it is selected depending on defined cargos)
			return LoadTranslationTable(gvid, numinfo, buf, _cur.grffile->cargo_list, "Cargo");

		case 0x12: // Rail type translation table; loading during both reservation and activation stage (in case it is selected depending on defined railtypes)
			return LoadTranslationTable(gvid, numinfo, buf, _cur.grffile->railtype_list, "Rail type");

		case 0x16: // Road type translation table; loading during both reservation and activation stage (in case it is selected depending on defined railtypes)
			return LoadTranslationTable(gvid, numinfo, buf, _cur.grffile->roadtype_list, "Road type");

		case 0x17: // Tram type translation table; loading during both reservation and activation stage (in case it is selected depending on defined railtypes)
			return LoadTranslationTable(gvid, numinfo, buf, _cur.grffile->tramtype_list, "Tram type");

		default:
			break;
	}

	/* Properties which are handled per item */
	ChangeInfoResult ret = CIR_SUCCESS;
	for (int i = 0; i < numinfo; i++) {
		switch (prop) {
			case 0x08: { // Cost base factor
				int factor = buf->ReadByte();
				uint price = gvid + i;

				if (price < PR_END) {
					_cur.grffile->price_base_multipliers[price] = std::min<int>(factor - 8, MAX_PRICE_MODIFIER);
				} else {
					grfmsg(1, "GlobalVarChangeInfo: Price %d out of range, ignoring", price);
				}
				break;
			}

			case 0x0A: { // Currency display names
				uint curidx = GetNewgrfCurrencyIdConverted(gvid + i);
				StringID newone = GetGRFStringID(_cur.grffile->grfid, buf->ReadWord());

				if ((newone != STR_UNDEFINED) && (curidx < CURRENCY_END)) {
					_currency_specs[curidx].name = newone;
				}
				break;
			}

			case 0x0B: { // Currency multipliers
				uint curidx = GetNewgrfCurrencyIdConverted(gvid + i);
				uint32 rate = buf->ReadDWord();

				if (curidx < CURRENCY_END) {
					/* TTDPatch uses a multiple of 1000 for its conversion calculations,
					 * which OTTD does not. For this reason, divide grf value by 1000,
					 * to be compatible */
					_currency_specs[curidx].rate = rate / 1000;
				} else {
					grfmsg(1, "GlobalVarChangeInfo: Currency multipliers %d out of range, ignoring", curidx);
				}
				break;
			}

			case 0x0C: { // Currency options
				uint curidx = GetNewgrfCurrencyIdConverted(gvid + i);
				uint16 options = buf->ReadWord();

				if (curidx < CURRENCY_END) {
					_currency_specs[curidx].separator[0] = GB(options, 0, 8);
					_currency_specs[curidx].separator[1] = '\0';
					/* By specifying only one bit, we prevent errors,
					 * since newgrf specs said that only 0 and 1 can be set for symbol_pos */
					_currency_specs[curidx].symbol_pos = GB(options, 8, 1);
				} else {
					grfmsg(1, "GlobalVarChangeInfo: Currency option %d out of range, ignoring", curidx);
				}
				break;
			}

			case 0x0D: { // Currency prefix symbol
				uint curidx = GetNewgrfCurrencyIdConverted(gvid + i);
				uint32 tempfix = buf->ReadDWord();

				if (curidx < CURRENCY_END) {
					memcpy(_currency_specs[curidx].prefix, &tempfix, 4);
					_currency_specs[curidx].prefix[4] = 0;
				} else {
					grfmsg(1, "GlobalVarChangeInfo: Currency symbol %d out of range, ignoring", curidx);
				}
				break;
			}

			case 0x0E: { // Currency suffix symbol
				uint curidx = GetNewgrfCurrencyIdConverted(gvid + i);
				uint32 tempfix = buf->ReadDWord();

				if (curidx < CURRENCY_END) {
					memcpy(&_currency_specs[curidx].suffix, &tempfix, 4);
					_currency_specs[curidx].suffix[4] = 0;
				} else {
					grfmsg(1, "GlobalVarChangeInfo: Currency symbol %d out of range, ignoring", curidx);
				}
				break;
			}

			case 0x0F: { //  Euro introduction dates
				uint curidx = GetNewgrfCurrencyIdConverted(gvid + i);
				Year year_euro = buf->ReadWord();

				if (curidx < CURRENCY_END) {
					_currency_specs[curidx].to_euro = year_euro;
				} else {
					grfmsg(1, "GlobalVarChangeInfo: Euro intro date %d out of range, ignoring", curidx);
				}
				break;
			}

			case 0x10: // Snow line height table
				if (numinfo > 1 || IsSnowLineSet()) {
					grfmsg(1, "GlobalVarChangeInfo: The snowline can only be set once (%d)", numinfo);
				} else if (buf->Remaining() < SNOW_LINE_MONTHS * SNOW_LINE_DAYS) {
					grfmsg(1, "GlobalVarChangeInfo: Not enough entries set in the snowline table (" PRINTF_SIZE ")", buf->Remaining());
				} else {
					byte table[SNOW_LINE_MONTHS][SNOW_LINE_DAYS];

					for (uint i = 0; i < SNOW_LINE_MONTHS; i++) {
						for (uint j = 0; j < SNOW_LINE_DAYS; j++) {
							table[i][j] = buf->ReadByte();
							if (_cur.grffile->grf_version >= 8) {
								if (table[i][j] != 0xFF) table[i][j] = table[i][j] * (1 + _settings_game.construction.max_heightlevel) / 256;
							} else {
								if (table[i][j] >= 128) {
									/* no snow */
									table[i][j] = 0xFF;
								} else {
									table[i][j] = table[i][j] * (1 + _settings_game.construction.max_heightlevel) / 128;
								}
							}
						}
					}
					SetSnowLine(table);
				}
				break;

			case 0x11: // GRF match for engine allocation
				/* This is loaded during the reservation stage, so just skip it here. */
				/* Each entry is 8 bytes. */
				buf->Skip(8);
				break;

			case 0x13:   // Gender translation table
			case 0x14:   // Case translation table
			case 0x15: { // Plural form translation
				uint curidx = gvid + i; // The current index, i.e. language.
				const LanguageMetadata *lang = curidx < MAX_LANG ? GetLanguage(curidx) : nullptr;
				if (lang == nullptr) {
					grfmsg(1, "GlobalVarChangeInfo: Language %d is not known, ignoring", curidx);
					/* Skip over the data. */
					if (prop == 0x15) {
						buf->ReadByte();
					} else {
						while (buf->ReadByte() != 0) {
							buf->ReadString();
						}
					}
					break;
				}

				if (_cur.grffile->language_map == nullptr) _cur.grffile->language_map = new LanguageMap[MAX_LANG];

				if (prop == 0x15) {
					uint plural_form = buf->ReadByte();
					if (plural_form >= LANGUAGE_MAX_PLURAL) {
						grfmsg(1, "GlobalVarChanceInfo: Plural form %d is out of range, ignoring", plural_form);
					} else {
						_cur.grffile->language_map[curidx].plural_form = plural_form;
					}
					break;
				}

				byte newgrf_id = buf->ReadByte(); // The NewGRF (custom) identifier.
				while (newgrf_id != 0) {
					const char *name = buf->ReadString(); // The name for the OpenTTD identifier.

					/* We'll just ignore the UTF8 identifier character. This is (fairly)
					 * safe as OpenTTD's strings gender/cases are usually in ASCII which
					 * is just a subset of UTF8, or they need the bigger UTF8 characters
					 * such as Cyrillic. Thus we will simply assume they're all UTF8. */
					WChar c;
					size_t len = Utf8Decode(&c, name);
					if (c == NFO_UTF8_IDENTIFIER) name += len;

					LanguageMap::Mapping map;
					map.newgrf_id = newgrf_id;
					if (prop == 0x13) {
						map.openttd_id = lang->GetGenderIndex(name);
						if (map.openttd_id >= MAX_NUM_GENDERS) {
							grfmsg(1, "GlobalVarChangeInfo: Gender name %s is not known, ignoring", name);
						} else {
							_cur.grffile->language_map[curidx].gender_map.push_back(map);
						}
					} else {
						map.openttd_id = lang->GetCaseIndex(name);
						if (map.openttd_id >= MAX_NUM_CASES) {
							grfmsg(1, "GlobalVarChangeInfo: Case name %s is not known, ignoring", name);
						} else {
							_cur.grffile->language_map[curidx].case_map.push_back(map);
						}
					}
					newgrf_id = buf->ReadByte();
				}
				break;
			}

			default:
				ret = CIR_UNKNOWN;
				break;
		}
	}

	return ret;
}

static ChangeInfoResult GlobalVarReserveInfo(uint gvid, int numinfo, int prop, ByteReader *buf)
{
	/* Properties which are handled as a whole */
	switch (prop) {
		case 0x09: // Cargo Translation Table; loading during both reservation and activation stage (in case it is selected depending on defined cargos)
			return LoadTranslationTable(gvid, numinfo, buf, _cur.grffile->cargo_list, "Cargo");

		case 0x12: // Rail type translation table; loading during both reservation and activation stage (in case it is selected depending on defined railtypes)
			return LoadTranslationTable(gvid, numinfo, buf, _cur.grffile->railtype_list, "Rail type");

		case 0x16: // Road type translation table; loading during both reservation and activation stage (in case it is selected depending on defined roadtypes)
			return LoadTranslationTable(gvid, numinfo, buf, _cur.grffile->roadtype_list, "Road type");

		case 0x17: // Tram type translation table; loading during both reservation and activation stage (in case it is selected depending on defined tramtypes)
			return LoadTranslationTable(gvid, numinfo, buf, _cur.grffile->tramtype_list, "Tram type");

		default:
			break;
	}

	/* Properties which are handled per item */
	ChangeInfoResult ret = CIR_SUCCESS;
	for (int i = 0; i < numinfo; i++) {
		switch (prop) {
			case 0x08: // Cost base factor
			case 0x15: // Plural form translation
				buf->ReadByte();
				break;

			case 0x0A: // Currency display names
			case 0x0C: // Currency options
			case 0x0F: // Euro introduction dates
				buf->ReadWord();
				break;

			case 0x0B: // Currency multipliers
			case 0x0D: // Currency prefix symbol
			case 0x0E: // Currency suffix symbol
				buf->ReadDWord();
				break;

			case 0x10: // Snow line height table
				buf->Skip(SNOW_LINE_MONTHS * SNOW_LINE_DAYS);
				break;

			case 0x11: { // GRF match for engine allocation
				uint32 s = buf->ReadDWord();
				uint32 t = buf->ReadDWord();
				SetNewGRFOverride(s, t);
				break;
			}

			case 0x13: // Gender translation table
			case 0x14: // Case translation table
				while (buf->ReadByte() != 0) {
					buf->ReadString();
				}
				break;

			default:
				ret = CIR_UNKNOWN;
				break;
		}
	}

	return ret;
}


/**
 * Define properties for cargoes
 * @param cid Local ID of the cargo.
 * @param numinfo Number of subsequent IDs to change the property for.
 * @param prop The property to change.
 * @param buf The property value.
 * @return ChangeInfoResult.
 */
static ChangeInfoResult CargoChangeInfo(uint cid, int numinfo, int prop, ByteReader *buf)
{
	ChangeInfoResult ret = CIR_SUCCESS;

	if (cid + numinfo > NUM_CARGO) {
		grfmsg(2, "CargoChangeInfo: Cargo type %d out of range (max %d)", cid + numinfo, NUM_CARGO - 1);
		return CIR_INVALID_ID;
	}

	for (int i = 0; i < numinfo; i++) {
		CargoSpec *cs = CargoSpec::Get(cid + i);

		switch (prop) {
			case 0x08: // Bit number of cargo
				cs->bitnum = buf->ReadByte();
				if (cs->IsValid()) {
					cs->grffile = _cur.grffile;
					SetBit(_cargo_mask, cid + i);
				} else {
					ClrBit(_cargo_mask, cid + i);
				}
				break;

			case 0x09: // String ID for cargo type name
				AddStringForMapping(buf->ReadWord(), &cs->name);
				break;

			case 0x0A: // String for 1 unit of cargo
				AddStringForMapping(buf->ReadWord(), &cs->name_single);
				break;

			case 0x0B: // String for singular quantity of cargo (e.g. 1 tonne of coal)
			case 0x1B: // String for cargo units
				/* String for units of cargo. This is different in OpenTTD
				 * (e.g. tonnes) to TTDPatch (e.g. {COMMA} tonne of coal).
				 * Property 1B is used to set OpenTTD's behaviour. */
				AddStringForMapping(buf->ReadWord(), &cs->units_volume);
				break;

			case 0x0C: // String for plural quantity of cargo (e.g. 10 tonnes of coal)
			case 0x1C: // String for any amount of cargo
				/* Strings for an amount of cargo. This is different in OpenTTD
				 * (e.g. {WEIGHT} of coal) to TTDPatch (e.g. {COMMA} tonnes of coal).
				 * Property 1C is used to set OpenTTD's behaviour. */
				AddStringForMapping(buf->ReadWord(), &cs->quantifier);
				break;

			case 0x0D: // String for two letter cargo abbreviation
				AddStringForMapping(buf->ReadWord(), &cs->abbrev);
				break;

			case 0x0E: // Sprite ID for cargo icon
				cs->sprite = buf->ReadWord();
				break;

			case 0x0F: // Weight of one unit of cargo
				cs->weight = buf->ReadByte();
				break;

			case 0x10: // Used for payment calculation
				cs->transit_days[0] = buf->ReadByte();
				break;

			case 0x11: // Used for payment calculation
				cs->transit_days[1] = buf->ReadByte();
				break;

			case 0x12: // Base cargo price
				cs->initial_payment = buf->ReadDWord();
				break;

			case 0x13: // Colour for station rating bars
				cs->rating_colour = buf->ReadByte();
				break;

			case 0x14: // Colour for cargo graph
				cs->legend_colour = buf->ReadByte();
				break;

			case 0x15: // Freight status
				cs->is_freight = (buf->ReadByte() != 0);
				break;

			case 0x16: // Cargo classes
				cs->classes = buf->ReadWord();
				break;

			case 0x17: // Cargo label
				cs->label = buf->ReadDWord();
				cs->label = BSWAP32(cs->label);
				break;

			case 0x18: { // Town growth substitute type
				uint8 substitute_type = buf->ReadByte();

				switch (substitute_type) {
					case 0x00: cs->town_effect = TE_PASSENGERS; break;
					case 0x02: cs->town_effect = TE_MAIL; break;
					case 0x05: cs->town_effect = TE_GOODS; break;
					case 0x09: cs->town_effect = TE_WATER; break;
					case 0x0B: cs->town_effect = TE_FOOD; break;
					default:
						grfmsg(1, "CargoChangeInfo: Unknown town growth substitute value %d, setting to none.", substitute_type);
						FALLTHROUGH;
					case 0xFF: cs->town_effect = TE_NONE; break;
				}
				break;
			}

			case 0x19: // Town growth coefficient
				cs->multipliertowngrowth = buf->ReadWord();
				break;

			case 0x1A: // Bitmask of callbacks to use
				cs->callback_mask = buf->ReadByte();
				break;

			case 0x1D: // Vehicle capacity muliplier
				cs->multiplier = std::max<uint16>(1u, buf->ReadWord());
				break;

			default:
				ret = CIR_UNKNOWN;
				break;
		}
	}

	return ret;
}


/**
 * Define properties for sound effects
 * @param sid Local ID of the sound.
 * @param numinfo Number of subsequent IDs to change the property for.
 * @param prop The property to change.
 * @param buf The property value.
 * @return ChangeInfoResult.
 */
static ChangeInfoResult SoundEffectChangeInfo(uint sid, int numinfo, int prop, ByteReader *buf)
{
	ChangeInfoResult ret = CIR_SUCCESS;

	if (_cur.grffile->sound_offset == 0) {
		grfmsg(1, "SoundEffectChangeInfo: No effects defined, skipping");
		return CIR_INVALID_ID;
	}

	if (sid + numinfo - ORIGINAL_SAMPLE_COUNT > _cur.grffile->num_sounds) {
		grfmsg(1, "SoundEffectChangeInfo: Attempting to change undefined sound effect (%u), max (%u). Ignoring.", sid + numinfo, ORIGINAL_SAMPLE_COUNT + _cur.grffile->num_sounds);
		return CIR_INVALID_ID;
	}

	for (int i = 0; i < numinfo; i++) {
		SoundEntry *sound = GetSound(sid + i + _cur.grffile->sound_offset - ORIGINAL_SAMPLE_COUNT);

		switch (prop) {
			case 0x08: // Relative volume
				sound->volume = buf->ReadByte();
				break;

			case 0x09: // Priority
				sound->priority = buf->ReadByte();
				break;

			case 0x0A: { // Override old sound
				SoundID orig_sound = buf->ReadByte();

				if (orig_sound >= ORIGINAL_SAMPLE_COUNT) {
					grfmsg(1, "SoundEffectChangeInfo: Original sound %d not defined (max %d)", orig_sound, ORIGINAL_SAMPLE_COUNT);
				} else {
					SoundEntry *old_sound = GetSound(orig_sound);

					/* Literally copy the data of the new sound over the original */
					*old_sound = *sound;
				}
				break;
			}

			default:
				ret = CIR_UNKNOWN;
				break;
		}
	}

	return ret;
}

/**
 * Ignore an industry tile property
 * @param prop The property to ignore.
 * @param buf The property value.
 * @return ChangeInfoResult.
 */
static ChangeInfoResult IgnoreIndustryTileProperty(int prop, ByteReader *buf)
{
	ChangeInfoResult ret = CIR_SUCCESS;

	switch (prop) {
		case 0x09:
		case 0x0D:
		case 0x0E:
		case 0x10:
		case 0x11:
		case 0x12:
			buf->ReadByte();
			break;

		case 0x0A:
		case 0x0B:
		case 0x0C:
		case 0x0F:
			buf->ReadWord();
			break;

		case 0x13:
			buf->Skip(buf->ReadByte() * 2);
			break;

		default:
			ret = CIR_UNKNOWN;
			break;
	}
	return ret;
}

/**
 * Define properties for industry tiles
 * @param indtid Local ID of the industry tile.
 * @param numinfo Number of subsequent industry tile IDs to change the property for.
 * @param prop The property to change.
 * @param buf The property value.
 * @return ChangeInfoResult.
 */
static ChangeInfoResult IndustrytilesChangeInfo(uint indtid, int numinfo, int prop, ByteReader *buf)
{
	ChangeInfoResult ret = CIR_SUCCESS;

	if (indtid + numinfo > NUM_INDUSTRYTILES_PER_GRF) {
		grfmsg(1, "IndustryTilesChangeInfo: Too many industry tiles loaded (%u), max (%u). Ignoring.", indtid + numinfo, NUM_INDUSTRYTILES_PER_GRF);
		return CIR_INVALID_ID;
	}

	/* Allocate industry tile specs if they haven't been allocated already. */
	if (_cur.grffile->indtspec == nullptr) {
		_cur.grffile->indtspec = CallocT<IndustryTileSpec*>(NUM_INDUSTRYTILES_PER_GRF);
	}

	for (int i = 0; i < numinfo; i++) {
		IndustryTileSpec *tsp = _cur.grffile->indtspec[indtid + i];

		if (prop != 0x08 && tsp == nullptr) {
			ChangeInfoResult cir = IgnoreIndustryTileProperty(prop, buf);
			if (cir > ret) ret = cir;
			continue;
		}

		switch (prop) {
			case 0x08: { // Substitute industry tile type
				IndustryTileSpec **tilespec = &_cur.grffile->indtspec[indtid + i];
				byte subs_id = buf->ReadByte();

				if (subs_id >= NEW_INDUSTRYTILEOFFSET) {
					/* The substitute id must be one of the original industry tile. */
					grfmsg(2, "IndustryTilesChangeInfo: Attempt to use new industry tile %u as substitute industry tile for %u. Ignoring.", subs_id, indtid + i);
					continue;
				}

				/* Allocate space for this industry. */
				if (*tilespec == nullptr) {
					*tilespec = CallocT<IndustryTileSpec>(1);
					tsp = *tilespec;

					memcpy(tsp, &_industry_tile_specs[subs_id], sizeof(_industry_tile_specs[subs_id]));
					tsp->enabled = true;

					/* A copied tile should not have the animation infos copied too.
					 * The anim_state should be left untouched, though
					 * It is up to the author to animate them himself */
					tsp->anim_production = INDUSTRYTILE_NOANIM;
					tsp->anim_next = INDUSTRYTILE_NOANIM;

					tsp->grf_prop.local_id = indtid + i;
					tsp->grf_prop.subst_id = subs_id;
					tsp->grf_prop.grffile = _cur.grffile;
					_industile_mngr.AddEntityID(indtid + i, _cur.grffile->grfid, subs_id); // pre-reserve the tile slot
				}
				break;
			}

			case 0x09: { // Industry tile override
				byte ovrid = buf->ReadByte();

				/* The industry being overridden must be an original industry. */
				if (ovrid >= NEW_INDUSTRYTILEOFFSET) {
					grfmsg(2, "IndustryTilesChangeInfo: Attempt to override new industry tile %u with industry tile id %u. Ignoring.", ovrid, indtid + i);
					continue;
				}

				_industile_mngr.Add(indtid + i, _cur.grffile->grfid, ovrid);
				break;
			}

			case 0x0A: // Tile acceptance
			case 0x0B:
			case 0x0C: {
				uint16 acctp = buf->ReadWord();
				tsp->accepts_cargo[prop - 0x0A] = GetCargoTranslation(GB(acctp, 0, 8), _cur.grffile);
				tsp->acceptance[prop - 0x0A] = Clamp(GB(acctp, 8, 8), 0, 16);
				break;
			}

			case 0x0D: // Land shape flags
				tsp->slopes_refused = (Slope)buf->ReadByte();
				break;

			case 0x0E: // Callback mask
				tsp->callback_mask = buf->ReadByte();
				break;

			case 0x0F: // Animation information
				tsp->animation.frames = buf->ReadByte();
				tsp->animation.status = buf->ReadByte();
				break;

			case 0x10: // Animation speed
				tsp->animation.speed = buf->ReadByte();
				break;

			case 0x11: // Triggers for callback 25
				tsp->animation.triggers = buf->ReadByte();
				break;

			case 0x12: // Special flags
				tsp->special_flags = (IndustryTileSpecialFlags)buf->ReadByte();
				break;

			case 0x13: { // variable length cargo acceptance
				byte num_cargoes = buf->ReadByte();
				if (num_cargoes > lengthof(tsp->acceptance)) {
					GRFError *error = DisableGrf(STR_NEWGRF_ERROR_LIST_PROPERTY_TOO_LONG);
					error->param_value[1] = prop;
					return CIR_DISABLED;
				}
				for (uint i = 0; i < lengthof(tsp->acceptance); i++) {
					if (i < num_cargoes) {
						tsp->accepts_cargo[i] = GetCargoTranslation(buf->ReadByte(), _cur.grffile);
						/* Tile acceptance can be negative to counteract the INDTILE_SPECIAL_ACCEPTS_ALL_CARGO flag */
						tsp->acceptance[i] = (int8)buf->ReadByte();
					} else {
						tsp->accepts_cargo[i] = CT_INVALID;
						tsp->acceptance[i] = 0;
					}
				}
				break;
			}

			default:
				ret = CIR_UNKNOWN;
				break;
		}
	}

	return ret;
}

/**
 * Ignore an industry property
 * @param prop The property to ignore.
 * @param buf The property value.
 * @return ChangeInfoResult.
 */
static ChangeInfoResult IgnoreIndustryProperty(int prop, ByteReader *buf)
{
	ChangeInfoResult ret = CIR_SUCCESS;

	switch (prop) {
		case 0x09:
		case 0x0B:
		case 0x0F:
		case 0x12:
		case 0x13:
		case 0x14:
		case 0x17:
		case 0x18:
		case 0x19:
		case 0x21:
		case 0x22:
			buf->ReadByte();
			break;

		case 0x0C:
		case 0x0D:
		case 0x0E:
		case 0x10:
		case 0x1B:
		case 0x1F:
		case 0x24:
			buf->ReadWord();
			break;

		case 0x11:
		case 0x1A:
		case 0x1C:
		case 0x1D:
		case 0x1E:
		case 0x20:
		case 0x23:
			buf->ReadDWord();
			break;

		case 0x0A: {
			byte num_table = buf->ReadByte();
			for (byte j = 0; j < num_table; j++) {
				for (uint k = 0;; k++) {
					byte x = buf->ReadByte();
					if (x == 0xFE && k == 0) {
						buf->ReadByte();
						buf->ReadByte();
						break;
					}

					byte y = buf->ReadByte();
					if (x == 0 && y == 0x80) break;

					byte gfx = buf->ReadByte();
					if (gfx == 0xFE) buf->ReadWord();
				}
			}
			break;
		}

		case 0x16:
			for (byte j = 0; j < 3; j++) buf->ReadByte();
			break;

		case 0x15:
		case 0x25:
		case 0x26:
		case 0x27:
			buf->Skip(buf->ReadByte());
			break;

		case 0x28: {
			int num_inputs = buf->ReadByte();
			int num_outputs = buf->ReadByte();
			buf->Skip(num_inputs * num_outputs * 2);
			break;
		}

		default:
			ret = CIR_UNKNOWN;
			break;
	}
	return ret;
}

/**
 * Validate the industry layout; e.g. to prevent duplicate tiles.
 * @param layout The layout to check.
 * @return True if the layout is deemed valid.
 */
static bool ValidateIndustryLayout(const IndustryTileLayout &layout)
{
	const size_t size = layout.size();
	for (size_t i = 0; i < size - 1; i++) {
		for (size_t j = i + 1; j < size; j++) {
			if (layout[i].ti.x == layout[j].ti.x &&
					layout[i].ti.y == layout[j].ti.y) {
				return false;
			}
		}
	}
	return true;
}

/**
 * Define properties for industries
 * @param indid Local ID of the industry.
 * @param numinfo Number of subsequent industry IDs to change the property for.
 * @param prop The property to change.
 * @param buf The property value.
 * @return ChangeInfoResult.
 */
static ChangeInfoResult IndustriesChangeInfo(uint indid, int numinfo, int prop, ByteReader *buf)
{
	ChangeInfoResult ret = CIR_SUCCESS;

	if (indid + numinfo > NUM_INDUSTRYTYPES_PER_GRF) {
		grfmsg(1, "IndustriesChangeInfo: Too many industries loaded (%u), max (%u). Ignoring.", indid + numinfo, NUM_INDUSTRYTYPES_PER_GRF);
		return CIR_INVALID_ID;
	}

	/* Allocate industry specs if they haven't been allocated already. */
	if (_cur.grffile->industryspec == nullptr) {
		_cur.grffile->industryspec = CallocT<IndustrySpec*>(NUM_INDUSTRYTYPES_PER_GRF);
	}

	for (int i = 0; i < numinfo; i++) {
		IndustrySpec *indsp = _cur.grffile->industryspec[indid + i];

		if (prop != 0x08 && indsp == nullptr) {
			ChangeInfoResult cir = IgnoreIndustryProperty(prop, buf);
			if (cir > ret) ret = cir;
			continue;
		}

		switch (prop) {
			case 0x08: { // Substitute industry type
				IndustrySpec **indspec = &_cur.grffile->industryspec[indid + i];
				byte subs_id = buf->ReadByte();

				if (subs_id == 0xFF) {
					/* Instead of defining a new industry, a substitute industry id
					 * of 0xFF disables the old industry with the current id. */
					_industry_specs[indid + i].enabled = false;
					continue;
				} else if (subs_id >= NEW_INDUSTRYOFFSET) {
					/* The substitute id must be one of the original industry. */
					grfmsg(2, "_industry_specs: Attempt to use new industry %u as substitute industry for %u. Ignoring.", subs_id, indid + i);
					continue;
				}

				/* Allocate space for this industry.
				 * Only need to do it once. If ever it is called again, it should not
				 * do anything */
				if (*indspec == nullptr) {
					*indspec = new IndustrySpec;
					indsp = *indspec;

					*indsp = _origin_industry_specs[subs_id];
					indsp->enabled = true;
					indsp->grf_prop.local_id = indid + i;
					indsp->grf_prop.subst_id = subs_id;
					indsp->grf_prop.grffile = _cur.grffile;
					/* If the grf industry needs to check its surrounding upon creation, it should
					 * rely on callbacks, not on the original placement functions */
					indsp->check_proc = CHECK_NOTHING;
				}
				break;
			}

			case 0x09: { // Industry type override
				byte ovrid = buf->ReadByte();

				/* The industry being overridden must be an original industry. */
				if (ovrid >= NEW_INDUSTRYOFFSET) {
					grfmsg(2, "IndustriesChangeInfo: Attempt to override new industry %u with industry id %u. Ignoring.", ovrid, indid + i);
					continue;
				}
				indsp->grf_prop.override = ovrid;
				_industry_mngr.Add(indid + i, _cur.grffile->grfid, ovrid);
				break;
			}

			case 0x0A: { // Set industry layout(s)
				byte new_num_layouts = buf->ReadByte();
				uint32 definition_size = buf->ReadDWord();
				uint32 bytes_read = 0;
				std::vector<IndustryTileLayout> new_layouts;
				IndustryTileLayout layout;

				for (byte j = 0; j < new_num_layouts; j++) {
					layout.clear();

					for (uint k = 0;; k++) {
						if (bytes_read >= definition_size) {
							grfmsg(3, "IndustriesChangeInfo: Incorrect size for industry tile layout definition for industry %u.", indid);
							/* Avoid warning twice */
							definition_size = UINT32_MAX;
						}

						layout.push_back(IndustryTileLayoutTile{});
						IndustryTileLayoutTile &it = layout.back();

						it.ti.x = buf->ReadByte(); // Offsets from northermost tile
						++bytes_read;

						if (it.ti.x == 0xFE && k == 0) {
							/* This means we have to borrow the layout from an old industry */
							IndustryType type = buf->ReadByte();
							byte laynbr = buf->ReadByte();
							bytes_read += 2;

							if (type >= lengthof(_origin_industry_specs)) {
								grfmsg(1, "IndustriesChangeInfo: Invalid original industry number for layout import, industry %u", indid);
								DisableGrf(STR_NEWGRF_ERROR_INVALID_ID);
								return CIR_DISABLED;
							}
							if (laynbr >= _origin_industry_specs[type].layouts.size()) {
								grfmsg(1, "IndustriesChangeInfo: Invalid original industry layout index for layout import, industry %u", indid);
								DisableGrf(STR_NEWGRF_ERROR_INVALID_ID);
								return CIR_DISABLED;
							}
							layout = _origin_industry_specs[type].layouts[laynbr];
							break;
						}

						it.ti.y = buf->ReadByte(); // Or table definition finalisation
						++bytes_read;

						if (it.ti.x == 0 && it.ti.y == 0x80) {
							/* Terminator, remove and finish up */
							layout.pop_back();
							break;
						}

						it.gfx = buf->ReadByte();
						++bytes_read;

						if (it.gfx == 0xFE) {
							/* Use a new tile from this GRF */
							int local_tile_id = buf->ReadWord();
							bytes_read += 2;

							/* Read the ID from the _industile_mngr. */
							int tempid = _industile_mngr.GetID(local_tile_id, _cur.grffile->grfid);

							if (tempid == INVALID_INDUSTRYTILE) {
								grfmsg(2, "IndustriesChangeInfo: Attempt to use industry tile %u with industry id %u, not yet defined. Ignoring.", local_tile_id, indid);
							} else {
								/* Declared as been valid, can be used */
								it.gfx = tempid;
							}
						} else if (it.gfx == 0xFF) {
							it.ti.x = (int8)GB(it.ti.x, 0, 8);
							it.ti.y = (int8)GB(it.ti.y, 0, 8);

							/* When there were only 256x256 maps, TileIndex was a uint16 and
								* it.ti was just a TileIndexDiff that was added to it.
								* As such negative "x" values were shifted into the "y" position.
								*   x = -1, y = 1 -> x = 255, y = 0
								* Since GRF version 8 the position is interpreted as pair of independent int8.
								* For GRF version < 8 we need to emulate the old shifting behaviour.
								*/
							if (_cur.grffile->grf_version < 8 && it.ti.x < 0) it.ti.y += 1;
						}
					}

					if (!ValidateIndustryLayout(layout)) {
						/* The industry layout was not valid, so skip this one. */
						grfmsg(1, "IndustriesChangeInfo: Invalid industry layout for industry id %u. Ignoring", indid);
						new_num_layouts--;
						j--;
					} else {
						new_layouts.push_back(layout);
					}
				}

				/* Install final layout construction in the industry spec */
				indsp->layouts = new_layouts;
				break;
			}

			case 0x0B: // Industry production flags
				indsp->life_type = (IndustryLifeType)buf->ReadByte();
				break;

			case 0x0C: // Industry closure message
				AddStringForMapping(buf->ReadWord(), &indsp->closure_text);
				break;

			case 0x0D: // Production increase message
				AddStringForMapping(buf->ReadWord(), &indsp->production_up_text);
				break;

			case 0x0E: // Production decrease message
				AddStringForMapping(buf->ReadWord(), &indsp->production_down_text);
				break;

			case 0x0F: // Fund cost multiplier
				indsp->cost_multiplier = buf->ReadByte();
				break;

			case 0x10: // Production cargo types
				for (byte j = 0; j < 2; j++) {
					indsp->produced_cargo[j] = GetCargoTranslation(buf->ReadByte(), _cur.grffile);
				}
				break;

			case 0x11: // Acceptance cargo types
				for (byte j = 0; j < 3; j++) {
					indsp->accepts_cargo[j] = GetCargoTranslation(buf->ReadByte(), _cur.grffile);
				}
				buf->ReadByte(); // Unnused, eat it up
				break;

			case 0x12: // Production multipliers
			case 0x13:
				indsp->production_rate[prop - 0x12] = buf->ReadByte();
				break;

			case 0x14: // Minimal amount of cargo distributed
				indsp->minimal_cargo = buf->ReadByte();
				break;

			case 0x15: { // Random sound effects
				indsp->number_of_sounds = buf->ReadByte();
				uint8 *sounds = MallocT<uint8>(indsp->number_of_sounds);

				try {
					for (uint8 j = 0; j < indsp->number_of_sounds; j++) {
						sounds[j] = buf->ReadByte();
					}
				} catch (...) {
					free(sounds);
					throw;
				}

				if (HasBit(indsp->cleanup_flag, CLEAN_RANDOMSOUNDS)) {
					free(indsp->random_sounds);
				}
				indsp->random_sounds = sounds;
				SetBit(indsp->cleanup_flag, CLEAN_RANDOMSOUNDS);
				break;
			}

			case 0x16: // Conflicting industry types
				for (byte j = 0; j < 3; j++) indsp->conflicting[j] = buf->ReadByte();
				break;

			case 0x17: // Probability in random game
				indsp->appear_creation[_settings_game.game_creation.landscape] = buf->ReadByte();
				break;

			case 0x18: // Probability during gameplay
				indsp->appear_ingame[_settings_game.game_creation.landscape] = buf->ReadByte();
				break;

			case 0x19: // Map colour
				indsp->map_colour = buf->ReadByte();
				break;

			case 0x1A: // Special industry flags to define special behavior
				indsp->behaviour = (IndustryBehaviour)buf->ReadDWord();
				break;

			case 0x1B: // New industry text ID
				AddStringForMapping(buf->ReadWord(), &indsp->new_industry_text);
				break;

			case 0x1C: // Input cargo multipliers for the three input cargo types
			case 0x1D:
			case 0x1E: {
					uint32 multiples = buf->ReadDWord();
					indsp->input_cargo_multiplier[prop - 0x1C][0] = GB(multiples, 0, 16);
					indsp->input_cargo_multiplier[prop - 0x1C][1] = GB(multiples, 16, 16);
					break;
				}

			case 0x1F: // Industry name
				AddStringForMapping(buf->ReadWord(), &indsp->name);
				break;

			case 0x20: // Prospecting success chance
				indsp->prospecting_chance = buf->ReadDWord();
				break;

			case 0x21:   // Callback mask
			case 0x22: { // Callback additional mask
				byte aflag = buf->ReadByte();
				SB(indsp->callback_mask, (prop - 0x21) * 8, 8, aflag);
				break;
			}

			case 0x23: // removal cost multiplier
				indsp->removal_cost_multiplier = buf->ReadDWord();
				break;

			case 0x24: { // name for nearby station
				uint16 str = buf->ReadWord();
				if (str == 0) {
					indsp->station_name = STR_NULL;
				} else {
					AddStringForMapping(str, &indsp->station_name);
				}
				break;
			}

			case 0x25: { // variable length produced cargoes
				byte num_cargoes = buf->ReadByte();
				if (num_cargoes > lengthof(indsp->produced_cargo)) {
					GRFError *error = DisableGrf(STR_NEWGRF_ERROR_LIST_PROPERTY_TOO_LONG);
					error->param_value[1] = prop;
					return CIR_DISABLED;
				}
				for (uint i = 0; i < lengthof(indsp->produced_cargo); i++) {
					if (i < num_cargoes) {
						CargoID cargo = GetCargoTranslation(buf->ReadByte(), _cur.grffile);
						indsp->produced_cargo[i] = cargo;
					} else {
						indsp->produced_cargo[i] = CT_INVALID;
					}
				}
				break;
			}

			case 0x26: { // variable length accepted cargoes
				byte num_cargoes = buf->ReadByte();
				if (num_cargoes > lengthof(indsp->accepts_cargo)) {
					GRFError *error = DisableGrf(STR_NEWGRF_ERROR_LIST_PROPERTY_TOO_LONG);
					error->param_value[1] = prop;
					return CIR_DISABLED;
				}
				for (uint i = 0; i < lengthof(indsp->accepts_cargo); i++) {
					if (i < num_cargoes) {
						CargoID cargo = GetCargoTranslation(buf->ReadByte(), _cur.grffile);
						indsp->accepts_cargo[i] = cargo;
					} else {
						indsp->accepts_cargo[i] = CT_INVALID;
					}
				}
				break;
			}

			case 0x27: { // variable length production rates
				byte num_cargoes = buf->ReadByte();
				if (num_cargoes > lengthof(indsp->production_rate)) {
					GRFError *error = DisableGrf(STR_NEWGRF_ERROR_LIST_PROPERTY_TOO_LONG);
					error->param_value[1] = prop;
					return CIR_DISABLED;
				}
				for (uint i = 0; i < lengthof(indsp->production_rate); i++) {
					if (i < num_cargoes) {
						indsp->production_rate[i] = buf->ReadByte();
					} else {
						indsp->production_rate[i] = 0;
					}
				}
				break;
			}

			case 0x28: { // variable size input/output production multiplier table
				byte num_inputs = buf->ReadByte();
				byte num_outputs = buf->ReadByte();
				if (num_inputs > lengthof(indsp->accepts_cargo) || num_outputs > lengthof(indsp->produced_cargo)) {
					GRFError *error = DisableGrf(STR_NEWGRF_ERROR_LIST_PROPERTY_TOO_LONG);
					error->param_value[1] = prop;
					return CIR_DISABLED;
				}
				for (uint i = 0; i < lengthof(indsp->accepts_cargo); i++) {
					for (uint j = 0; j < lengthof(indsp->produced_cargo); j++) {
						uint16 mult = 0;
						if (i < num_inputs && j < num_outputs) mult = buf->ReadWord();
						indsp->input_cargo_multiplier[i][j] = mult;
					}
				}
				break;
			}

			default:
				ret = CIR_UNKNOWN;
				break;
		}
	}

	return ret;
}

/**
 * Create a copy of the tile table so it can be freed later
 * without problems.
 * @param as The AirportSpec to copy the arrays of.
 */
static void DuplicateTileTable(AirportSpec *as)
{
	AirportTileTable **table_list = MallocT<AirportTileTable*>(as->num_table);
	for (int i = 0; i < as->num_table; i++) {
		uint num_tiles = 1;
		const AirportTileTable *it = as->table[0];
		do {
			num_tiles++;
		} while ((++it)->ti.x != -0x80);
		table_list[i] = MallocT<AirportTileTable>(num_tiles);
		MemCpyT(table_list[i], as->table[i], num_tiles);
	}
	as->table = table_list;
	HangarTileTable *depot_table = MallocT<HangarTileTable>(as->nof_depots);
	MemCpyT(depot_table, as->depot_table, as->nof_depots);
	as->depot_table = depot_table;
	Direction *rotation = MallocT<Direction>(as->num_table);
	MemCpyT(rotation, as->rotation, as->num_table);
	as->rotation = rotation;
}

/**
 * Define properties for airports
 * @param airport Local ID of the airport.
 * @param numinfo Number of subsequent airport IDs to change the property for.
 * @param prop The property to change.
 * @param buf The property value.
 * @return ChangeInfoResult.
 */
static ChangeInfoResult AirportChangeInfo(uint airport, int numinfo, int prop, ByteReader *buf)
{
	ChangeInfoResult ret = CIR_SUCCESS;

	if (airport + numinfo > NUM_AIRPORTS_PER_GRF) {
		grfmsg(1, "AirportChangeInfo: Too many airports, trying id (%u), max (%u). Ignoring.", airport + numinfo, NUM_AIRPORTS_PER_GRF);
		return CIR_INVALID_ID;
	}

	/* Allocate industry specs if they haven't been allocated already. */
	if (_cur.grffile->airportspec == nullptr) {
		_cur.grffile->airportspec = CallocT<AirportSpec*>(NUM_AIRPORTS_PER_GRF);
	}

	for (int i = 0; i < numinfo; i++) {
		AirportSpec *as = _cur.grffile->airportspec[airport + i];

		if (as == nullptr && prop != 0x08 && prop != 0x09) {
			grfmsg(2, "AirportChangeInfo: Attempt to modify undefined airport %u, ignoring", airport + i);
			return CIR_INVALID_ID;
		}

		switch (prop) {
			case 0x08: { // Modify original airport
				byte subs_id = buf->ReadByte();

				if (subs_id == 0xFF) {
					/* Instead of defining a new airport, an airport id
					 * of 0xFF disables the old airport with the current id. */
					AirportSpec::GetWithoutOverride(airport + i)->enabled = false;
					continue;
				} else if (subs_id >= NEW_AIRPORT_OFFSET) {
					/* The substitute id must be one of the original airports. */
					grfmsg(2, "AirportChangeInfo: Attempt to use new airport %u as substitute airport for %u. Ignoring.", subs_id, airport + i);
					continue;
				}

				AirportSpec **spec = &_cur.grffile->airportspec[airport + i];
				/* Allocate space for this airport.
				 * Only need to do it once. If ever it is called again, it should not
				 * do anything */
				if (*spec == nullptr) {
					*spec = MallocT<AirportSpec>(1);
					as = *spec;

					memcpy(as, AirportSpec::GetWithoutOverride(subs_id), sizeof(*as));
					as->enabled = true;
					as->grf_prop.local_id = airport + i;
					as->grf_prop.subst_id = subs_id;
					as->grf_prop.grffile = _cur.grffile;
					/* override the default airport */
					_airport_mngr.Add(airport + i, _cur.grffile->grfid, subs_id);
					/* Create a copy of the original tiletable so it can be freed later. */
					DuplicateTileTable(as);
				}
				break;
			}

			case 0x0A: { // Set airport layout
				free(as->rotation);
				as->num_table = buf->ReadByte(); // Number of layaouts
				as->rotation = MallocT<Direction>(as->num_table);
				uint32 defsize = buf->ReadDWord();  // Total size of the definition
				AirportTileTable **tile_table = CallocT<AirportTileTable*>(as->num_table); // Table with tiles to compose the airport
				AirportTileTable *att = CallocT<AirportTileTable>(defsize); // Temporary array to read the tile layouts from the GRF
				int size;
				const AirportTileTable *copy_from;
				try {
					for (byte j = 0; j < as->num_table; j++) {
						const_cast<Direction&>(as->rotation[j]) = (Direction)buf->ReadByte();
						for (int k = 0;; k++) {
							att[k].ti.x = buf->ReadByte(); // Offsets from northermost tile
							att[k].ti.y = buf->ReadByte();

							if (att[k].ti.x == 0 && att[k].ti.y == 0x80) {
								/*  Not the same terminator.  The one we are using is rather
								 * x = -80, y = 0 .  So, adjust it. */
								att[k].ti.x = -0x80;
								att[k].ti.y =  0;
								att[k].gfx  =  0;

								size = k + 1;
								copy_from = att;
								break;
							}

							att[k].gfx = buf->ReadByte();

							if (att[k].gfx == 0xFE) {
								/* Use a new tile from this GRF */
								int local_tile_id = buf->ReadWord();

								/* Read the ID from the _airporttile_mngr. */
								uint16 tempid = _airporttile_mngr.GetID(local_tile_id, _cur.grffile->grfid);

								if (tempid == INVALID_AIRPORTTILE) {
									grfmsg(2, "AirportChangeInfo: Attempt to use airport tile %u with airport id %u, not yet defined. Ignoring.", local_tile_id, airport + i);
								} else {
									/* Declared as been valid, can be used */
									att[k].gfx = tempid;
								}
							} else if (att[k].gfx == 0xFF) {
								att[k].ti.x = (int8)GB(att[k].ti.x, 0, 8);
								att[k].ti.y = (int8)GB(att[k].ti.y, 0, 8);
							}

							if (as->rotation[j] == DIR_E || as->rotation[j] == DIR_W) {
								as->size_x = std::max<byte>(as->size_x, att[k].ti.y + 1);
								as->size_y = std::max<byte>(as->size_y, att[k].ti.x + 1);
							} else {
								as->size_x = std::max<byte>(as->size_x, att[k].ti.x + 1);
								as->size_y = std::max<byte>(as->size_y, att[k].ti.y + 1);
							}
						}
						tile_table[j] = CallocT<AirportTileTable>(size);
						memcpy(tile_table[j], copy_from, sizeof(*copy_from) * size);
					}
					/* Install final layout construction in the airport spec */
					as->table = tile_table;
					free(att);
				} catch (...) {
					for (int i = 0; i < as->num_table; i++) {
						free(tile_table[i]);
					}
					free(tile_table);
					free(att);
					throw;
				}
				break;
			}

			case 0x0C:
				as->min_year = buf->ReadWord();
				as->max_year = buf->ReadWord();
				if (as->max_year == 0xFFFF) as->max_year = MAX_YEAR;
				break;

			case 0x0D:
				as->ttd_airport_type = (TTDPAirportType)buf->ReadByte();
				break;

			case 0x0E:
				as->catchment = Clamp(buf->ReadByte(), 1, MAX_CATCHMENT);
				break;

			case 0x0F:
				as->noise_level = buf->ReadByte();
				break;

			case 0x10:
				AddStringForMapping(buf->ReadWord(), &as->name);
				break;

			case 0x11: // Maintenance cost factor
				as->maintenance_cost = buf->ReadWord();
				break;

			default:
				ret = CIR_UNKNOWN;
				break;
		}
	}

	return ret;
}

/**
 * Ignore properties for objects
 * @param prop The property to ignore.
 * @param buf The property value.
 * @return ChangeInfoResult.
 */
static ChangeInfoResult IgnoreObjectProperty(uint prop, ByteReader *buf)
{
	ChangeInfoResult ret = CIR_SUCCESS;

	switch (prop) {
		case 0x0B:
		case 0x0C:
		case 0x0D:
		case 0x12:
		case 0x14:
		case 0x16:
		case 0x17:
			buf->ReadByte();
			break;

		case 0x09:
		case 0x0A:
		case 0x10:
		case 0x11:
		case 0x13:
		case 0x15:
			buf->ReadWord();
			break;

		case 0x08:
		case 0x0E:
		case 0x0F:
			buf->ReadDWord();
			break;

		default:
			ret = CIR_UNKNOWN;
			break;
	}

	return ret;
}

/**
 * Define properties for objects
 * @param id Local ID of the object.
 * @param numinfo Number of subsequent objectIDs to change the property for.
 * @param prop The property to change.
 * @param buf The property value.
 * @return ChangeInfoResult.
 */
static ChangeInfoResult ObjectChangeInfo(uint id, int numinfo, int prop, ByteReader *buf)
{
	ChangeInfoResult ret = CIR_SUCCESS;

	if (id + numinfo > NUM_OBJECTS_PER_GRF) {
		grfmsg(1, "ObjectChangeInfo: Too many objects loaded (%u), max (%u). Ignoring.", id + numinfo, NUM_OBJECTS_PER_GRF);
		return CIR_INVALID_ID;
	}

	/* Allocate object specs if they haven't been allocated already. */
	if (_cur.grffile->objectspec == nullptr) {
		_cur.grffile->objectspec = CallocT<ObjectSpec*>(NUM_OBJECTS_PER_GRF);
	}

	for (int i = 0; i < numinfo; i++) {
		ObjectSpec *spec = _cur.grffile->objectspec[id + i];

		if (prop != 0x08 && spec == nullptr) {
			/* If the object property 08 is not yet set, ignore this property */
			ChangeInfoResult cir = IgnoreObjectProperty(prop, buf);
			if (cir > ret) ret = cir;
			continue;
		}

		switch (prop) {
			case 0x08: { // Class ID
				ObjectSpec **ospec = &_cur.grffile->objectspec[id + i];

				/* Allocate space for this object. */
				if (*ospec == nullptr) {
					*ospec = CallocT<ObjectSpec>(1);
					(*ospec)->views = 1; // Default for NewGRFs that don't set it.
					(*ospec)->size = 0x11; // Default for NewGRFs that manage to not set it (1x1)
				}

				/* Swap classid because we read it in BE. */
				uint32 classid = buf->ReadDWord();
				(*ospec)->cls_id = ObjectClass::Allocate(BSWAP32(classid));
				(*ospec)->enabled = true;
				break;
			}

			case 0x09: { // Class name
				ObjectClass *objclass = ObjectClass::Get(spec->cls_id);
				AddStringForMapping(buf->ReadWord(), &objclass->name);
				break;
			}

			case 0x0A: // Object name
				AddStringForMapping(buf->ReadWord(), &spec->name);
				break;

			case 0x0B: // Climate mask
				spec->climate = buf->ReadByte();
				break;

			case 0x0C: // Size
				spec->size = buf->ReadByte();
				if ((spec->size & 0xF0) == 0 || (spec->size & 0x0F) == 0) {
					grfmsg(1, "ObjectChangeInfo: Invalid object size requested (%u) for object id %u. Ignoring.", spec->size, id + i);
					spec->size = 0x11; // 1x1
				}
				break;

			case 0x0D: // Build cost multipler
				spec->build_cost_multiplier = buf->ReadByte();
				spec->clear_cost_multiplier = spec->build_cost_multiplier;
				break;

			case 0x0E: // Introduction date
				spec->introduction_date = buf->ReadDWord();
				break;

			case 0x0F: // End of life
				spec->end_of_life_date = buf->ReadDWord();
				break;

			case 0x10: // Flags
				spec->flags = (ObjectFlags)buf->ReadWord();
				_loaded_newgrf_features.has_2CC |= (spec->flags & OBJECT_FLAG_2CC_COLOUR) != 0;
				break;

			case 0x11: // Animation info
				spec->animation.frames = buf->ReadByte();
				spec->animation.status = buf->ReadByte();
				break;

			case 0x12: // Animation speed
				spec->animation.speed = buf->ReadByte();
				break;

			case 0x13: // Animation triggers
				spec->animation.triggers = buf->ReadWord();
				break;

			case 0x14: // Removal cost multiplier
				spec->clear_cost_multiplier = buf->ReadByte();
				break;

			case 0x15: // Callback mask
				spec->callback_mask = buf->ReadWord();
				break;

			case 0x16: // Building height
				spec->height = buf->ReadByte();
				break;

			case 0x17: // Views
				spec->views = buf->ReadByte();
				if (spec->views != 1 && spec->views != 2 && spec->views != 4) {
					grfmsg(2, "ObjectChangeInfo: Invalid number of views (%u) for object id %u. Ignoring.", spec->views, id + i);
					spec->views = 1;
				}
				break;

			case 0x18: // Amount placed on 256^2 map on map creation
				spec->generate_amount = buf->ReadByte();
				break;

			default:
				ret = CIR_UNKNOWN;
				break;
		}
	}

	return ret;
}

/**
 * Define properties for railtypes
 * @param id ID of the railtype.
 * @param numinfo Number of subsequent IDs to change the property for.
 * @param prop The property to change.
 * @param buf The property value.
 * @return ChangeInfoResult.
 */
static ChangeInfoResult RailTypeChangeInfo(uint id, int numinfo, int prop, ByteReader *buf)
{
	ChangeInfoResult ret = CIR_SUCCESS;

	extern RailtypeInfo _railtypes[RAILTYPE_END];

	if (id + numinfo > RAILTYPE_END) {
		grfmsg(1, "RailTypeChangeInfo: Rail type %u is invalid, max %u, ignoring", id + numinfo, RAILTYPE_END);
		return CIR_INVALID_ID;
	}

	for (int i = 0; i < numinfo; i++) {
		RailType rt = _cur.grffile->railtype_map[id + i];
		if (rt == INVALID_RAILTYPE) return CIR_INVALID_ID;

		RailtypeInfo *rti = &_railtypes[rt];

		switch (prop) {
			case 0x08: // Label of rail type
				/* Skipped here as this is loaded during reservation stage. */
				buf->ReadDWord();
				break;

			case 0x09: { // Toolbar caption of railtype (sets name as well for backwards compatibility for grf ver < 8)
				uint16 str = buf->ReadWord();
				AddStringForMapping(str, &rti->strings.toolbar_caption);
				if (_cur.grffile->grf_version < 8) {
					AddStringForMapping(str, &rti->strings.name);
				}
				break;
			}

			case 0x0A: // Menu text of railtype
				AddStringForMapping(buf->ReadWord(), &rti->strings.menu_text);
				break;

			case 0x0B: // Build window caption
				AddStringForMapping(buf->ReadWord(), &rti->strings.build_caption);
				break;

			case 0x0C: // Autoreplace text
				AddStringForMapping(buf->ReadWord(), &rti->strings.replace_text);
				break;

			case 0x0D: // New locomotive text
				AddStringForMapping(buf->ReadWord(), &rti->strings.new_loco);
				break;

			case 0x0E: // Compatible railtype list
			case 0x0F: // Powered railtype list
			case 0x18: // Railtype list required for date introduction
			case 0x19: // Introduced railtype list
			{
				/* Rail type compatibility bits are added to the existing bits
				 * to allow multiple GRFs to modify compatibility with the
				 * default rail types. */
				int n = buf->ReadByte();
				for (int j = 0; j != n; j++) {
					RailTypeLabel label = buf->ReadDWord();
					RailType rt = GetRailTypeByLabel(BSWAP32(label), false);
					if (rt != INVALID_RAILTYPE) {
						switch (prop) {
							case 0x0F: SetBit(rti->powered_railtypes, rt);               FALLTHROUGH; // Powered implies compatible.
							case 0x0E: SetBit(rti->compatible_railtypes, rt);            break;
							case 0x18: SetBit(rti->introduction_required_railtypes, rt); break;
							case 0x19: SetBit(rti->introduces_railtypes, rt);            break;
						}
					}
				}
				break;
			}

			case 0x10: // Rail Type flags
				rti->flags = (RailTypeFlags)buf->ReadByte();
				break;

			case 0x11: // Curve speed advantage
				rti->curve_speed = buf->ReadByte();
				break;

			case 0x12: // Station graphic
				rti->fallback_railtype = Clamp(buf->ReadByte(), 0, 2);
				break;

			case 0x13: // Construction cost factor
				rti->cost_multiplier = buf->ReadWord();
				break;

			case 0x14: // Speed limit
				rti->max_speed = buf->ReadWord();
				break;

			case 0x15: // Acceleration model
				rti->acceleration_type = Clamp(buf->ReadByte(), 0, 2);
				break;

			case 0x16: // Map colour
				rti->map_colour = buf->ReadByte();
				break;

			case 0x17: // Introduction date
				rti->introduction_date = buf->ReadDWord();
				break;

			case 0x1A: // Sort order
				rti->sorting_order = buf->ReadByte();
				break;

			case 0x1B: // Name of railtype (overridden by prop 09 for grf ver < 8)
				AddStringForMapping(buf->ReadWord(), &rti->strings.name);
				break;

			case 0x1C: // Maintenance cost factor
				rti->maintenance_multiplier = buf->ReadWord();
				break;

			case 0x1D: // Alternate rail type label list
				/* Skipped here as this is loaded during reservation stage. */
				for (int j = buf->ReadByte(); j != 0; j--) buf->ReadDWord();
				break;

			default:
				ret = CIR_UNKNOWN;
				break;
		}
	}

	return ret;
}

static ChangeInfoResult RailTypeReserveInfo(uint id, int numinfo, int prop, ByteReader *buf)
{
	ChangeInfoResult ret = CIR_SUCCESS;

	extern RailtypeInfo _railtypes[RAILTYPE_END];

	if (id + numinfo > RAILTYPE_END) {
		grfmsg(1, "RailTypeReserveInfo: Rail type %u is invalid, max %u, ignoring", id + numinfo, RAILTYPE_END);
		return CIR_INVALID_ID;
	}

	for (int i = 0; i < numinfo; i++) {
		switch (prop) {
			case 0x08: // Label of rail type
			{
				RailTypeLabel rtl = buf->ReadDWord();
				rtl = BSWAP32(rtl);

				RailType rt = GetRailTypeByLabel(rtl, false);
				if (rt == INVALID_RAILTYPE) {
					/* Set up new rail type */
					rt = AllocateRailType(rtl);
				}

				_cur.grffile->railtype_map[id + i] = rt;
				break;
			}

			case 0x09: // Toolbar caption of railtype
			case 0x0A: // Menu text
			case 0x0B: // Build window caption
			case 0x0C: // Autoreplace text
			case 0x0D: // New loco
			case 0x13: // Construction cost
			case 0x14: // Speed limit
			case 0x1B: // Name of railtype
			case 0x1C: // Maintenance cost factor
				buf->ReadWord();
				break;

			case 0x1D: // Alternate rail type label list
				if (_cur.grffile->railtype_map[id + i] != INVALID_RAILTYPE) {
					int n = buf->ReadByte();
					for (int j = 0; j != n; j++) {
						_railtypes[_cur.grffile->railtype_map[id + i]].alternate_labels.push_back(BSWAP32(buf->ReadDWord()));
					}
					break;
				}
				grfmsg(1, "RailTypeReserveInfo: Ignoring property 1D for rail type %u because no label was set", id + i);
				FALLTHROUGH;

			case 0x0E: // Compatible railtype list
			case 0x0F: // Powered railtype list
			case 0x18: // Railtype list required for date introduction
			case 0x19: // Introduced railtype list
				for (int j = buf->ReadByte(); j != 0; j--) buf->ReadDWord();
				break;

			case 0x10: // Rail Type flags
			case 0x11: // Curve speed advantage
			case 0x12: // Station graphic
			case 0x15: // Acceleration model
			case 0x16: // Map colour
			case 0x1A: // Sort order
				buf->ReadByte();
				break;

			case 0x17: // Introduction date
				buf->ReadDWord();
				break;

			default:
				ret = CIR_UNKNOWN;
				break;
		}
	}

	return ret;
}

/**
 * Define properties for roadtypes
 * @param id ID of the roadtype.
 * @param numinfo Number of subsequent IDs to change the property for.
 * @param prop The property to change.
 * @param buf The property value.
 * @return ChangeInfoResult.
 */
static ChangeInfoResult RoadTypeChangeInfo(uint id, int numinfo, int prop, ByteReader *buf, RoadTramType rtt)
{
	ChangeInfoResult ret = CIR_SUCCESS;

	extern RoadTypeInfo _roadtypes[ROADTYPE_END];
	RoadType *type_map = (rtt == RTT_TRAM) ? _cur.grffile->tramtype_map : _cur.grffile->roadtype_map;

	if (id + numinfo > ROADTYPE_END) {
		grfmsg(1, "RoadTypeChangeInfo: Road type %u is invalid, max %u, ignoring", id + numinfo, ROADTYPE_END);
		return CIR_INVALID_ID;
	}

	for (int i = 0; i < numinfo; i++) {
		RoadType rt = type_map[id + i];
		if (rt == INVALID_ROADTYPE) return CIR_INVALID_ID;

		RoadTypeInfo *rti = &_roadtypes[rt];

		switch (prop) {
			case 0x08: // Label of road type
				/* Skipped here as this is loaded during reservation stage. */
				buf->ReadDWord();
				break;

			case 0x09: { // Toolbar caption of roadtype (sets name as well for backwards compatibility for grf ver < 8)
				uint16 str = buf->ReadWord();
				AddStringForMapping(str, &rti->strings.toolbar_caption);
				break;
			}

			case 0x0A: // Menu text of roadtype
				AddStringForMapping(buf->ReadWord(), &rti->strings.menu_text);
				break;

			case 0x0B: // Build window caption
				AddStringForMapping(buf->ReadWord(), &rti->strings.build_caption);
				break;

			case 0x0C: // Autoreplace text
				AddStringForMapping(buf->ReadWord(), &rti->strings.replace_text);
				break;

			case 0x0D: // New engine text
				AddStringForMapping(buf->ReadWord(), &rti->strings.new_engine);
				break;

			case 0x0F: // Powered roadtype list
			case 0x18: // Roadtype list required for date introduction
			case 0x19: { // Introduced roadtype list
				/* Road type compatibility bits are added to the existing bits
				 * to allow multiple GRFs to modify compatibility with the
				 * default road types. */
				int n = buf->ReadByte();
				for (int j = 0; j != n; j++) {
					RoadTypeLabel label = buf->ReadDWord();
					RoadType rt = GetRoadTypeByLabel(BSWAP32(label), false);
					if (rt != INVALID_ROADTYPE) {
						switch (prop) {
							case 0x0F: SetBit(rti->powered_roadtypes, rt);               break;
							case 0x18: SetBit(rti->introduction_required_roadtypes, rt); break;
							case 0x19: SetBit(rti->introduces_roadtypes, rt);            break;
						}
					}
				}
				break;
			}

			case 0x10: // Road Type flags
				rti->flags = (RoadTypeFlags)buf->ReadByte();
				break;

			case 0x13: // Construction cost factor
				rti->cost_multiplier = buf->ReadWord();
				break;

			case 0x14: // Speed limit
				rti->max_speed = buf->ReadWord();
				break;

			case 0x16: // Map colour
				rti->map_colour = buf->ReadByte();
				break;

			case 0x17: // Introduction date
				rti->introduction_date = buf->ReadDWord();
				break;

			case 0x1A: // Sort order
				rti->sorting_order = buf->ReadByte();
				break;

			case 0x1B: // Name of roadtype
				AddStringForMapping(buf->ReadWord(), &rti->strings.name);
				break;

			case 0x1C: // Maintenance cost factor
				rti->maintenance_multiplier = buf->ReadWord();
				break;

			case 0x1D: // Alternate road type label list
				/* Skipped here as this is loaded during reservation stage. */
				for (int j = buf->ReadByte(); j != 0; j--) buf->ReadDWord();
				break;

			default:
				ret = CIR_UNKNOWN;
				break;
		}
	}

	return ret;
}

static ChangeInfoResult RoadTypeChangeInfo(uint id, int numinfo, int prop, ByteReader *buf)
{
	return RoadTypeChangeInfo(id, numinfo, prop, buf, RTT_ROAD);
}

static ChangeInfoResult TramTypeChangeInfo(uint id, int numinfo, int prop, ByteReader *buf)
{
	return RoadTypeChangeInfo(id, numinfo, prop, buf, RTT_TRAM);
}


static ChangeInfoResult RoadTypeReserveInfo(uint id, int numinfo, int prop, ByteReader *buf, RoadTramType rtt)
{
	ChangeInfoResult ret = CIR_SUCCESS;

	extern RoadTypeInfo _roadtypes[ROADTYPE_END];
	RoadType *type_map = (rtt == RTT_TRAM) ? _cur.grffile->tramtype_map : _cur.grffile->roadtype_map;

	if (id + numinfo > ROADTYPE_END) {
		grfmsg(1, "RoadTypeReserveInfo: Road type %u is invalid, max %u, ignoring", id + numinfo, ROADTYPE_END);
		return CIR_INVALID_ID;
	}

	for (int i = 0; i < numinfo; i++) {
		switch (prop) {
			case 0x08: { // Label of road type
				RoadTypeLabel rtl = buf->ReadDWord();
				rtl = BSWAP32(rtl);

				RoadType rt = GetRoadTypeByLabel(rtl, false);
				if (rt == INVALID_ROADTYPE) {
					/* Set up new road type */
					rt = AllocateRoadType(rtl, rtt);
				} else if (GetRoadTramType(rt) != rtt) {
					grfmsg(1, "RoadTypeReserveInfo: Road type %u is invalid type (road/tram), ignoring", id + numinfo);
					return CIR_INVALID_ID;
				}

				type_map[id + i] = rt;
				break;
			}
			case 0x09: // Toolbar caption of roadtype
			case 0x0A: // Menu text
			case 0x0B: // Build window caption
			case 0x0C: // Autoreplace text
			case 0x0D: // New loco
			case 0x13: // Construction cost
			case 0x14: // Speed limit
			case 0x1B: // Name of roadtype
			case 0x1C: // Maintenance cost factor
				buf->ReadWord();
				break;

			case 0x1D: // Alternate road type label list
				if (type_map[id + i] != INVALID_ROADTYPE) {
					int n = buf->ReadByte();
					for (int j = 0; j != n; j++) {
						_roadtypes[type_map[id + i]].alternate_labels.push_back(BSWAP32(buf->ReadDWord()));
					}
					break;
				}
				grfmsg(1, "RoadTypeReserveInfo: Ignoring property 1D for road type %u because no label was set", id + i);
				/* FALL THROUGH */

			case 0x0F: // Powered roadtype list
			case 0x18: // Roadtype list required for date introduction
			case 0x19: // Introduced roadtype list
				for (int j = buf->ReadByte(); j != 0; j--) buf->ReadDWord();
				break;

			case 0x10: // Road Type flags
			case 0x16: // Map colour
			case 0x1A: // Sort order
				buf->ReadByte();
				break;

			case 0x17: // Introduction date
				buf->ReadDWord();
				break;

			default:
				ret = CIR_UNKNOWN;
				break;
		}
	}

	return ret;
}

static ChangeInfoResult RoadTypeReserveInfo(uint id, int numinfo, int prop, ByteReader *buf)
{
	return RoadTypeReserveInfo(id, numinfo, prop, buf, RTT_ROAD);
}

static ChangeInfoResult TramTypeReserveInfo(uint id, int numinfo, int prop, ByteReader *buf)
{
	return RoadTypeReserveInfo(id, numinfo, prop, buf, RTT_TRAM);
}

static ChangeInfoResult AirportTilesChangeInfo(uint airtid, int numinfo, int prop, ByteReader *buf)
{
	ChangeInfoResult ret = CIR_SUCCESS;

	if (airtid + numinfo > NUM_AIRPORTTILES_PER_GRF) {
		grfmsg(1, "AirportTileChangeInfo: Too many airport tiles loaded (%u), max (%u). Ignoring.", airtid + numinfo, NUM_AIRPORTTILES_PER_GRF);
		return CIR_INVALID_ID;
	}

	/* Allocate airport tile specs if they haven't been allocated already. */
	if (_cur.grffile->airtspec == nullptr) {
		_cur.grffile->airtspec = CallocT<AirportTileSpec*>(NUM_AIRPORTTILES_PER_GRF);
	}

	for (int i = 0; i < numinfo; i++) {
		AirportTileSpec *tsp = _cur.grffile->airtspec[airtid + i];

		if (prop != 0x08 && tsp == nullptr) {
			grfmsg(2, "AirportTileChangeInfo: Attempt to modify undefined airport tile %u. Ignoring.", airtid + i);
			return CIR_INVALID_ID;
		}

		switch (prop) {
			case 0x08: { // Substitute airport tile type
				AirportTileSpec **tilespec = &_cur.grffile->airtspec[airtid + i];
				byte subs_id = buf->ReadByte();

				if (subs_id >= NEW_AIRPORTTILE_OFFSET) {
					/* The substitute id must be one of the original airport tiles. */
					grfmsg(2, "AirportTileChangeInfo: Attempt to use new airport tile %u as substitute airport tile for %u. Ignoring.", subs_id, airtid + i);
					continue;
				}

				/* Allocate space for this airport tile. */
				if (*tilespec == nullptr) {
					*tilespec = CallocT<AirportTileSpec>(1);
					tsp = *tilespec;

					memcpy(tsp, AirportTileSpec::Get(subs_id), sizeof(AirportTileSpec));
					tsp->enabled = true;

					tsp->animation.status = ANIM_STATUS_NO_ANIMATION;

					tsp->grf_prop.local_id = airtid + i;
					tsp->grf_prop.subst_id = subs_id;
					tsp->grf_prop.grffile = _cur.grffile;
					_airporttile_mngr.AddEntityID(airtid + i, _cur.grffile->grfid, subs_id); // pre-reserve the tile slot
				}
				break;
			}

			case 0x09: { // Airport tile override
				byte override = buf->ReadByte();

				/* The airport tile being overridden must be an original airport tile. */
				if (override >= NEW_AIRPORTTILE_OFFSET) {
					grfmsg(2, "AirportTileChangeInfo: Attempt to override new airport tile %u with airport tile id %u. Ignoring.", override, airtid + i);
					continue;
				}

				_airporttile_mngr.Add(airtid + i, _cur.grffile->grfid, override);
				break;
			}

			case 0x0E: // Callback mask
				tsp->callback_mask = buf->ReadByte();
				break;

			case 0x0F: // Animation information
				tsp->animation.frames = buf->ReadByte();
				tsp->animation.status = buf->ReadByte();
				break;

			case 0x10: // Animation speed
				tsp->animation.speed = buf->ReadByte();
				break;

			case 0x11: // Animation triggers
				tsp->animation.triggers = buf->ReadByte();
				break;

			default:
				ret = CIR_UNKNOWN;
				break;
		}
	}

	return ret;
}

static ChangeInfoResult RoadStopChangeInfo(uint id, int numinfo, int prop, ByteReader *buf) {
	ChangeInfoResult ret = CIR_SUCCESS;

	if (id + numinfo > 255) {
		grfmsg(1, "RoadStopChangeInfo: RoadStop %u is invalid, max %u, ignoring", id + numinfo, 255);
		return CIR_INVALID_ID;
	}

	if (_cur.grffile->roadstops == nullptr) _cur.grffile->roadstops = CallocT<RoadStopSpec*>(255);

	for (int i = 0; i < numinfo; i++) {
		RoadStopSpec *rs = _cur.grffile->roadstops[id + i];

		if (rs == nullptr && prop != 0x08) {
			grfmsg(1, "RoadStopChangeInfo: Attempt to modify undefined road stop %u, ignoring", id + i);
			return CIR_INVALID_ID;
		}

		switch (prop) {
			case 0x08: { // Road Stop Class ID
				RoadStopSpec **spec = &_cur.grffile->roadstops[id + i];

				if (*spec == nullptr) *spec = CallocT<RoadStopSpec>(1);

				uint32 classid = buf->ReadDWord();
				(*spec)->cls_id = RoadStopClass::Allocate(BSWAP32(classid));
				break;
			}

			case 0x09:
				rs->stop_type = (RoadStopAvailabilityType)buf->ReadByte();
				break;

			case 0x0A: // Road Stop Name
				AddStringForMapping(buf->ReadWord(), &rs->name);
				break;

			case 0x0B: // Road Stop Class name
				AddStringForMapping(buf->ReadWord(), &RoadStopClass::Get(rs->cls_id)->name);
				break;

			case 0x0C: // The draw mode
				rs->draw_mode = (RoadStopDrawMode)buf->ReadByte();
				break;

			default:
				ret = CIR_UNKNOWN;
				break;
		}
	}

	return ret;
}

static bool HandleChangeInfoResult(const char *caller, ChangeInfoResult cir, uint8 feature, uint8 property)
{
	switch (cir) {
		default: NOT_REACHED();

		case CIR_DISABLED:
			/* Error has already been printed; just stop parsing */
			return true;

		case CIR_SUCCESS:
			return false;

		case CIR_UNHANDLED:
			grfmsg(1, "%s: Ignoring property 0x%02X of feature 0x%02X (not implemented)", caller, property, feature);
			return false;

		case CIR_UNKNOWN:
			grfmsg(0, "%s: Unknown property 0x%02X of feature 0x%02X, disabling", caller, property, feature);
			FALLTHROUGH;

		case CIR_INVALID_ID: {
			/* No debug message for an invalid ID, as it has already been output */
			GRFError *error = DisableGrf(cir == CIR_INVALID_ID ? STR_NEWGRF_ERROR_INVALID_ID : STR_NEWGRF_ERROR_UNKNOWN_PROPERTY);
			if (cir != CIR_INVALID_ID) error->param_value[1] = property;
			return true;
		}
	}
}

/* Action 0x00 */
static void FeatureChangeInfo(ByteReader *buf)
{
	/* <00> <feature> <num-props> <num-info> <id> (<property <new-info>)...
	 *
	 * B feature
	 * B num-props     how many properties to change per vehicle/station
	 * B num-info      how many vehicles/stations to change
	 * E id            ID of first vehicle/station to change, if num-info is
	 *                 greater than one, this one and the following
	 *                 vehicles/stations will be changed
	 * B property      what property to change, depends on the feature
	 * V new-info      new bytes of info (variable size; depends on properties) */

	static const VCI_Handler handler[] = {
		/* GSF_TRAINS */        RailVehicleChangeInfo,
		/* GSF_ROADVEHICLES */  RoadVehicleChangeInfo,
		/* GSF_SHIPS */         ShipVehicleChangeInfo,
		/* GSF_AIRCRAFT */      AircraftVehicleChangeInfo,
		/* GSF_STATIONS */      StationChangeInfo,
		/* GSF_CANALS */        CanalChangeInfo,
		/* GSF_BRIDGES */       BridgeChangeInfo,
		/* GSF_HOUSES */        TownHouseChangeInfo,
		/* GSF_GLOBALVAR */     GlobalVarChangeInfo,
		/* GSF_INDUSTRYTILES */ IndustrytilesChangeInfo,
		/* GSF_INDUSTRIES */    IndustriesChangeInfo,
		/* GSF_CARGOES */       nullptr, // Cargo is handled during reservation
		/* GSF_SOUNDFX */       SoundEffectChangeInfo,
		/* GSF_AIRPORTS */      AirportChangeInfo,
		/* GSF_SIGNALS */       nullptr,
		/* GSF_OBJECTS */       ObjectChangeInfo,
		/* GSF_RAILTYPES */     RailTypeChangeInfo,
		/* GSF_AIRPORTTILES */  AirportTilesChangeInfo,
		/* GSF_ROADTYPES */     RoadTypeChangeInfo,
		/* GSF_TRAMTYPES */     TramTypeChangeInfo,
		/* GSF_ROADSTOPS */     RoadStopChangeInfo,
	};

	uint8 feature  = buf->ReadByte();
	uint8 numprops = buf->ReadByte();
	uint numinfo  = buf->ReadByte();
	uint engine   = buf->ReadExtendedByte();

	if (feature >= GSF_END) {
		grfmsg(1, "FeatureChangeInfo: Unsupported feature 0x%02X, skipping", feature);
		return;
	}

	grfmsg(6, "FeatureChangeInfo: Feature 0x%02X, %d properties, to apply to %d+%d",
	               feature, numprops, engine, numinfo);

	if (feature >= lengthof(handler) || handler[feature] == nullptr) {
		if (feature != GSF_CARGOES) grfmsg(1, "FeatureChangeInfo: Unsupported feature 0x%02X, skipping", feature);
		return;
	}

	/* Mark the feature as used by the grf */
	SetBit(_cur.grffile->grf_features, feature);

	while (numprops-- && buf->HasData()) {
		uint8 prop = buf->ReadByte();

		ChangeInfoResult cir = handler[feature](engine, numinfo, prop, buf);
		if (HandleChangeInfoResult("FeatureChangeInfo", cir, feature, prop)) return;
	}
}

/* Action 0x00 (GLS_SAFETYSCAN) */
static void SafeChangeInfo(ByteReader *buf)
{
	uint8 feature  = buf->ReadByte();
	uint8 numprops = buf->ReadByte();
	uint numinfo = buf->ReadByte();
	buf->ReadExtendedByte(); // id

	if (feature == GSF_BRIDGES && numprops == 1) {
		uint8 prop = buf->ReadByte();
		/* Bridge property 0x0D is redefinition of sprite layout tables, which
		 * is considered safe. */
		if (prop == 0x0D) return;
	} else if (feature == GSF_GLOBALVAR && numprops == 1) {
		uint8 prop = buf->ReadByte();
		/* Engine ID Mappings are safe, if the source is static */
		if (prop == 0x11) {
			bool is_safe = true;
			for (uint i = 0; i < numinfo; i++) {
				uint32 s = buf->ReadDWord();
				buf->ReadDWord(); // dest
				const GRFConfig *grfconfig = GetGRFConfig(s);
				if (grfconfig != nullptr && !HasBit(grfconfig->flags, GCF_STATIC)) {
					is_safe = false;
					break;
				}
			}
			if (is_safe) return;
		}
	}

	SetBit(_cur.grfconfig->flags, GCF_UNSAFE);

	/* Skip remainder of GRF */
	_cur.skip_sprites = -1;
}

/* Action 0x00 (GLS_RESERVE) */
static void ReserveChangeInfo(ByteReader *buf)
{
	uint8 feature  = buf->ReadByte();

	if (feature != GSF_CARGOES && feature != GSF_GLOBALVAR && feature != GSF_RAILTYPES && feature != GSF_ROADTYPES && feature != GSF_TRAMTYPES) return;

	uint8 numprops = buf->ReadByte();
	uint8 numinfo  = buf->ReadByte();
	uint8 index    = buf->ReadExtendedByte();

	while (numprops-- && buf->HasData()) {
		uint8 prop = buf->ReadByte();
		ChangeInfoResult cir = CIR_SUCCESS;

		switch (feature) {
			default: NOT_REACHED();
			case GSF_CARGOES:
				cir = CargoChangeInfo(index, numinfo, prop, buf);
				break;

			case GSF_GLOBALVAR:
				cir = GlobalVarReserveInfo(index, numinfo, prop, buf);
				break;

			case GSF_RAILTYPES:
				cir = RailTypeReserveInfo(index, numinfo, prop, buf);
				break;

			case GSF_ROADTYPES:
				cir = RoadTypeReserveInfo(index, numinfo, prop, buf);
				break;

			case GSF_TRAMTYPES:
				cir = TramTypeReserveInfo(index, numinfo, prop, buf);
				break;
		}

		if (HandleChangeInfoResult("ReserveChangeInfo", cir, feature, prop)) return;
	}
}

/* Action 0x01 */
static void NewSpriteSet(ByteReader *buf)
{
	/* Basic format:    <01> <feature> <num-sets> <num-ent>
	 * Extended format: <01> <feature> 00 <first-set> <num-sets> <num-ent>
	 *
	 * B feature       feature to define sprites for
	 *                 0, 1, 2, 3: veh-type, 4: train stations
	 * E first-set     first sprite set to define
	 * B num-sets      number of sprite sets (extended byte in extended format)
	 * E num-ent       how many entries per sprite set
	 *                 For vehicles, this is the number of different
	 *                         vehicle directions in each sprite set
	 *                         Set num-dirs=8, unless your sprites are symmetric.
	 *                         In that case, use num-dirs=4.
	 */

	uint8  feature   = buf->ReadByte();
	uint16 num_sets  = buf->ReadByte();
	uint16 first_set = 0;

	if (num_sets == 0 && buf->HasData(3)) {
		/* Extended Action1 format.
		 * Some GRFs define zero sets of zero sprites, though there is actually no use in that. Ignore them. */
		first_set = buf->ReadExtendedByte();
		num_sets = buf->ReadExtendedByte();
	}
	uint16 num_ents = buf->ReadExtendedByte();

	if (feature >= GSF_END) {
		_cur.skip_sprites = num_sets * num_ents;
		grfmsg(1, "NewSpriteSet: Unsupported feature 0x%02X, skipping %d sprites", feature, _cur.skip_sprites);
		return;
	}

	_cur.AddSpriteSets(feature, _cur.spriteid, first_set, num_sets, num_ents);

	grfmsg(7, "New sprite set at %d of feature 0x%02X, consisting of %d sets with %d views each (total %d)",
		_cur.spriteid, feature, num_sets, num_ents, num_sets * num_ents
	);

	for (int i = 0; i < num_sets * num_ents; i++) {
		_cur.nfo_line++;
		LoadNextSprite(_cur.spriteid++, _cur.file_index, _cur.nfo_line, _cur.grf_container_ver);
	}
}

/* Action 0x01 (SKIP) */
static void SkipAct1(ByteReader *buf)
{
	buf->ReadByte();
	uint16 num_sets  = buf->ReadByte();

	if (num_sets == 0 && buf->HasData(3)) {
		/* Extended Action1 format.
		 * Some GRFs define zero sets of zero sprites, though there is actually no use in that. Ignore them. */
		buf->ReadExtendedByte(); // first_set
		num_sets = buf->ReadExtendedByte();
	}
	uint16 num_ents = buf->ReadExtendedByte();

	_cur.skip_sprites = num_sets * num_ents;

	grfmsg(3, "SkipAct1: Skipping %d sprites", _cur.skip_sprites);
}

/* Helper function to either create a callback or link to a previously
 * defined spritegroup. */
static const SpriteGroup *GetGroupFromGroupID(byte setid, byte type, uint16 groupid)
{
	if (HasBit(groupid, 15)) {
		assert(CallbackResultSpriteGroup::CanAllocateItem());
		return new CallbackResultSpriteGroup(groupid, _cur.grffile->grf_version >= 8);
	}

	if (groupid > MAX_SPRITEGROUP || _cur.spritegroups[groupid] == nullptr) {
		grfmsg(1, "GetGroupFromGroupID(0x%02X:0x%02X): Groupid 0x%04X does not exist, leaving empty", setid, type, groupid);
		return nullptr;
	}

	return _cur.spritegroups[groupid];
}

/**
 * Helper function to either create a callback or a result sprite group.
 * @param feature GrfSpecFeature to define spritegroup for.
 * @param setid SetID of the currently being parsed Action2. (only for debug output)
 * @param type Type of the currently being parsed Action2. (only for debug output)
 * @param spriteid Raw value from the GRF for the new spritegroup; describes either the return value or the referenced spritegroup.
 * @return Created spritegroup.
 */
static const SpriteGroup *CreateGroupFromGroupID(byte feature, byte setid, byte type, uint16 spriteid)
{
	if (HasBit(spriteid, 15)) {
		assert(CallbackResultSpriteGroup::CanAllocateItem());
		return new CallbackResultSpriteGroup(spriteid, _cur.grffile->grf_version >= 8);
	}

	if (!_cur.IsValidSpriteSet(feature, spriteid)) {
		grfmsg(1, "CreateGroupFromGroupID(0x%02X:0x%02X): Sprite set %u invalid", setid, type, spriteid);
		return nullptr;
	}

	SpriteID spriteset_start = _cur.GetSprite(feature, spriteid);
	uint num_sprites = _cur.GetNumEnts(feature, spriteid);

	/* Ensure that the sprites are loeded */
	assert(spriteset_start + num_sprites <= _cur.spriteid);

	assert(ResultSpriteGroup::CanAllocateItem());
	return new ResultSpriteGroup(spriteset_start, num_sprites);
}

/* Action 0x02 */
static void NewSpriteGroup(ByteReader *buf)
{
	/* <02> <feature> <set-id> <type/num-entries> <feature-specific-data...>
	 *
	 * B feature       see action 1
	 * B set-id        ID of this particular definition
	 * B type/num-entries
	 *                 if 80 or greater, this is a randomized or variational
	 *                 list definition, see below
	 *                 otherwise it specifies a number of entries, the exact
	 *                 meaning depends on the feature
	 * V feature-specific-data (huge mess, don't even look it up --pasky) */
	SpriteGroup *act_group = nullptr;

	uint8 feature = buf->ReadByte();
	if (feature >= GSF_END) {
		grfmsg(1, "NewSpriteGroup: Unsupported feature 0x%02X, skipping", feature);
		return;
	}

	uint8 setid   = buf->ReadByte();
	uint8 type    = buf->ReadByte();

	/* Sprite Groups are created here but they are allocated from a pool, so
	 * we do not need to delete anything if there is an exception from the
	 * ByteReader. */

	switch (type) {
		/* Deterministic Sprite Group */
		case 0x81: // Self scope, byte
		case 0x82: // Parent scope, byte
		case 0x85: // Self scope, word
		case 0x86: // Parent scope, word
		case 0x89: // Self scope, dword
		case 0x8A: // Parent scope, dword
		{
			byte varadjust;
			byte varsize;

			assert(DeterministicSpriteGroup::CanAllocateItem());
			DeterministicSpriteGroup *group = new DeterministicSpriteGroup();
			group->nfo_line = _cur.nfo_line;
			act_group = group;
			group->var_scope = HasBit(type, 1) ? VSG_SCOPE_PARENT : VSG_SCOPE_SELF;

			switch (GB(type, 2, 2)) {
				default: NOT_REACHED();
				case 0: group->size = DSG_SIZE_BYTE;  varsize = 1; break;
				case 1: group->size = DSG_SIZE_WORD;  varsize = 2; break;
				case 2: group->size = DSG_SIZE_DWORD; varsize = 4; break;
			}

			static std::vector<DeterministicSpriteGroupAdjust> adjusts;
			adjusts.clear();

			/* Loop through the var adjusts. Unfortunately we don't know how many we have
			 * from the outset, so we shall have to keep reallocing. */
			do {
				DeterministicSpriteGroupAdjust &adjust = adjusts.emplace_back();

				/* The first var adjust doesn't have an operation specified, so we set it to add. */
				adjust.operation = adjusts.size() == 1 ? DSGA_OP_ADD : (DeterministicSpriteGroupAdjustOperation)buf->ReadByte();
				adjust.variable  = buf->ReadByte();
				if (adjust.variable == 0x7E) {
					/* Link subroutine group */
					adjust.subroutine = GetGroupFromGroupID(setid, type, buf->ReadByte());
				} else {
					adjust.parameter = IsInsideMM(adjust.variable, 0x60, 0x80) ? buf->ReadByte() : 0;
				}

				varadjust = buf->ReadByte();
				adjust.shift_num = GB(varadjust, 0, 5);
				adjust.type      = (DeterministicSpriteGroupAdjustType)GB(varadjust, 6, 2);
				adjust.and_mask  = buf->ReadVarSize(varsize);

				if (adjust.type != DSGA_TYPE_NONE) {
					adjust.add_val    = buf->ReadVarSize(varsize);
					adjust.divmod_val = buf->ReadVarSize(varsize);
				} else {
					adjust.add_val    = 0;
					adjust.divmod_val = 0;
				}

				/* Continue reading var adjusts while bit 5 is set. */
			} while (HasBit(varadjust, 5));

			group->num_adjusts = (uint)adjusts.size();
			group->adjusts = MallocT<DeterministicSpriteGroupAdjust>(group->num_adjusts);
			MemCpyT(group->adjusts, adjusts.data(), group->num_adjusts);

			std::vector<DeterministicSpriteGroupRange> ranges;
			ranges.resize(buf->ReadByte());
			for (uint i = 0; i < ranges.size(); i++) {
				ranges[i].group = GetGroupFromGroupID(setid, type, buf->ReadWord());
				ranges[i].low   = buf->ReadVarSize(varsize);
				ranges[i].high  = buf->ReadVarSize(varsize);
			}

			group->default_group = GetGroupFromGroupID(setid, type, buf->ReadWord());
			group->error_group = ranges.size() > 0 ? ranges[0].group : group->default_group;
			/* nvar == 0 is a special case -- we turn our value into a callback result */
			group->calculated_result = ranges.size() == 0;

			/* Sort ranges ascending. When ranges overlap, this may required clamping or splitting them */
			std::vector<uint32> bounds;
			for (uint i = 0; i < ranges.size(); i++) {
				bounds.push_back(ranges[i].low);
				if (ranges[i].high != UINT32_MAX) bounds.push_back(ranges[i].high + 1);
			}
			std::sort(bounds.begin(), bounds.end());
			bounds.erase(std::unique(bounds.begin(), bounds.end()), bounds.end());

			std::vector<const SpriteGroup *> target;
			for (uint j = 0; j < bounds.size(); ++j) {
				uint32 v = bounds[j];
				const SpriteGroup *t = group->default_group;
				for (uint i = 0; i < ranges.size(); i++) {
					if (ranges[i].low <= v && v <= ranges[i].high) {
						t = ranges[i].group;
						break;
					}
				}
				target.push_back(t);
			}
			assert(target.size() == bounds.size());

			std::vector<DeterministicSpriteGroupRange> optimised;
			for (uint j = 0; j < bounds.size(); ) {
				if (target[j] != group->default_group) {
					DeterministicSpriteGroupRange r;
					r.group = target[j];
					r.low = bounds[j];
					while (j < bounds.size() && target[j] == r.group) {
						j++;
					}
					r.high = j < bounds.size() ? bounds[j] - 1 : UINT32_MAX;
					optimised.push_back(r);
				} else {
					j++;
				}
			}

			group->num_ranges = (uint)optimised.size(); // cast is safe, there should never be 2**31 elements here
			if (group->num_ranges > 0) {
				group->ranges = MallocT<DeterministicSpriteGroupRange>(group->num_ranges);
				MemCpyT(group->ranges, &optimised.front(), group->num_ranges);
			}
			break;
		}

		/* Randomized Sprite Group */
		case 0x80: // Self scope
		case 0x83: // Parent scope
		case 0x84: // Relative scope
		{
			assert(RandomizedSpriteGroup::CanAllocateItem());
			RandomizedSpriteGroup *group = new RandomizedSpriteGroup();
			group->nfo_line = _cur.nfo_line;
			act_group = group;
			group->var_scope = HasBit(type, 1) ? VSG_SCOPE_PARENT : VSG_SCOPE_SELF;

			if (HasBit(type, 2)) {
				if (feature <= GSF_AIRCRAFT) group->var_scope = VSG_SCOPE_RELATIVE;
				group->count = buf->ReadByte();
			}

			uint8 triggers = buf->ReadByte();
			group->triggers       = GB(triggers, 0, 7);
			group->cmp_mode       = HasBit(triggers, 7) ? RSG_CMP_ALL : RSG_CMP_ANY;
			group->lowest_randbit = buf->ReadByte();
			group->num_groups     = buf->ReadByte();
			group->groups = CallocT<const SpriteGroup*>(group->num_groups);

			for (uint i = 0; i < group->num_groups; i++) {
				group->groups[i] = GetGroupFromGroupID(setid, type, buf->ReadWord());
			}

			break;
		}

		/* Neither a variable or randomized sprite group... must be a real group */
		default:
		{
			switch (feature) {
				case GSF_TRAINS:
				case GSF_ROADVEHICLES:
				case GSF_SHIPS:
				case GSF_AIRCRAFT:
				case GSF_STATIONS:
				case GSF_CANALS:
				case GSF_CARGOES:
				case GSF_AIRPORTS:
				case GSF_RAILTYPES:
				case GSF_ROADTYPES:
				case GSF_TRAMTYPES:
				{
					byte num_loaded  = type;
					byte num_loading = buf->ReadByte();

					if (!_cur.HasValidSpriteSets(feature)) {
						grfmsg(0, "NewSpriteGroup: No sprite set to work on! Skipping");
						return;
					}

					assert(RealSpriteGroup::CanAllocateItem());
					RealSpriteGroup *group = new RealSpriteGroup();
					group->nfo_line = _cur.nfo_line;
					act_group = group;

					group->num_loaded  = num_loaded;
					group->num_loading = num_loading;
					if (num_loaded  > 0) group->loaded = CallocT<const SpriteGroup*>(num_loaded);
					if (num_loading > 0) group->loading = CallocT<const SpriteGroup*>(num_loading);

					grfmsg(6, "NewSpriteGroup: New SpriteGroup 0x%02X, %u loaded, %u loading",
							setid, num_loaded, num_loading);

					for (uint i = 0; i < num_loaded; i++) {
						uint16 spriteid = buf->ReadWord();
						group->loaded[i] = CreateGroupFromGroupID(feature, setid, type, spriteid);
						grfmsg(8, "NewSpriteGroup: + rg->loaded[%i]  = subset %u", i, spriteid);
					}

					for (uint i = 0; i < num_loading; i++) {
						uint16 spriteid = buf->ReadWord();
						group->loading[i] = CreateGroupFromGroupID(feature, setid, type, spriteid);
						grfmsg(8, "NewSpriteGroup: + rg->loading[%i] = subset %u", i, spriteid);
					}

					break;
				}

				case GSF_HOUSES:
				case GSF_AIRPORTTILES:
				case GSF_OBJECTS:
				case GSF_INDUSTRYTILES:
				case GSF_ROADSTOPS: {
					byte num_building_sprites = std::max((uint8)1, type);

					assert(TileLayoutSpriteGroup::CanAllocateItem());
					TileLayoutSpriteGroup *group = new TileLayoutSpriteGroup();
					group->nfo_line = _cur.nfo_line;
					act_group = group;

					/* On error, bail out immediately. Temporary GRF data was already freed */
					if (ReadSpriteLayout(buf, num_building_sprites, true, feature, false, type == 0, &group->dts)) return;
					break;
				}

				case GSF_INDUSTRIES: {
					if (type > 2) {
						grfmsg(1, "NewSpriteGroup: Unsupported industry production version %d, skipping", type);
						break;
					}

					assert(IndustryProductionSpriteGroup::CanAllocateItem());
					IndustryProductionSpriteGroup *group = new IndustryProductionSpriteGroup();
					group->nfo_line = _cur.nfo_line;
					act_group = group;
					group->version = type;
					if (type == 0) {
						group->num_input = 3;
						for (uint i = 0; i < 3; i++) {
							group->subtract_input[i] = (int16)buf->ReadWord(); // signed
						}
						group->num_output = 2;
						for (uint i = 0; i < 2; i++) {
							group->add_output[i] = buf->ReadWord(); // unsigned
						}
						group->again = buf->ReadByte();
					} else if (type == 1) {
						group->num_input = 3;
						for (uint i = 0; i < 3; i++) {
							group->subtract_input[i] = buf->ReadByte();
						}
						group->num_output = 2;
						for (uint i = 0; i < 2; i++) {
							group->add_output[i] = buf->ReadByte();
						}
						group->again = buf->ReadByte();
					} else if (type == 2) {
						group->num_input = buf->ReadByte();
						if (group->num_input > lengthof(group->subtract_input)) {
							GRFError *error = DisableGrf(STR_NEWGRF_ERROR_INDPROD_CALLBACK);
							error->data = "too many inputs (max 16)";
							return;
						}
						for (uint i = 0; i < group->num_input; i++) {
							byte rawcargo = buf->ReadByte();
							CargoID cargo = GetCargoTranslation(rawcargo, _cur.grffile);
							if (cargo == CT_INVALID) {
								/* The mapped cargo is invalid. This is permitted at this point,
								 * as long as the result is not used. Mark it invalid so this
								 * can be tested later. */
								group->version = 0xFF;
							} else if (std::find(group->cargo_input, group->cargo_input + i, cargo) != group->cargo_input + i) {
								GRFError *error = DisableGrf(STR_NEWGRF_ERROR_INDPROD_CALLBACK);
								error->data = "duplicate input cargo";
								return;
							}
							group->cargo_input[i] = cargo;
							group->subtract_input[i] = buf->ReadByte();
						}
						group->num_output = buf->ReadByte();
						if (group->num_output > lengthof(group->add_output)) {
							GRFError *error = DisableGrf(STR_NEWGRF_ERROR_INDPROD_CALLBACK);
							error->data = "too many outputs (max 16)";
							return;
						}
						for (uint i = 0; i < group->num_output; i++) {
							byte rawcargo = buf->ReadByte();
							CargoID cargo = GetCargoTranslation(rawcargo, _cur.grffile);
							if (cargo == CT_INVALID) {
								/* Mark this result as invalid to use */
								group->version = 0xFF;
							} else if (std::find(group->cargo_output, group->cargo_output + i, cargo) != group->cargo_output + i) {
								GRFError *error = DisableGrf(STR_NEWGRF_ERROR_INDPROD_CALLBACK);
								error->data = "duplicate output cargo";
								return;
							}
							group->cargo_output[i] = cargo;
							group->add_output[i] = buf->ReadByte();
						}
						group->again = buf->ReadByte();
					} else {
						NOT_REACHED();
					}
					break;
				}

				/* Loading of Tile Layout and Production Callback groups would happen here */
				default: grfmsg(1, "NewSpriteGroup: Unsupported feature 0x%02X, skipping", feature);
			}
		}
	}

	_cur.spritegroups[setid] = act_group;
}

static CargoID TranslateCargo(uint8 feature, uint8 ctype)
{
	if (feature == GSF_OBJECTS) {
		switch (ctype) {
			case 0:    return 0;
			case 0xFF: return CT_PURCHASE_OBJECT;
			default:
				grfmsg(1, "TranslateCargo: Invalid cargo bitnum %d for objects, skipping.", ctype);
				return CT_INVALID;
		}
	}
	/* Special cargo types for purchase list and stations */
	if (feature == GSF_STATIONS && ctype == 0xFE) return CT_DEFAULT_NA;
	if (ctype == 0xFF) return CT_PURCHASE;

	if (_cur.grffile->cargo_list.size() == 0) {
		/* No cargo table, so use bitnum values */
		if (ctype >= 32) {
			grfmsg(1, "TranslateCargo: Cargo bitnum %d out of range (max 31), skipping.", ctype);
			return CT_INVALID;
		}

		const CargoSpec *cs;
		FOR_ALL_CARGOSPECS(cs) {
			if (cs->bitnum == ctype) {
				grfmsg(6, "TranslateCargo: Cargo bitnum %d mapped to cargo type %d.", ctype, cs->Index());
				return cs->Index();
			}
		}

		grfmsg(5, "TranslateCargo: Cargo bitnum %d not available in this climate, skipping.", ctype);
		return CT_INVALID;
	}

	/* Check if the cargo type is out of bounds of the cargo translation table */
	if (ctype >= _cur.grffile->cargo_list.size()) {
		grfmsg(1, "TranslateCargo: Cargo type %d out of range (max %d), skipping.", ctype, (unsigned int)_cur.grffile->cargo_list.size() - 1);
		return CT_INVALID;
	}

	/* Look up the cargo label from the translation table */
	CargoLabel cl = _cur.grffile->cargo_list[ctype];
	if (cl == 0) {
		grfmsg(5, "TranslateCargo: Cargo type %d not available in this climate, skipping.", ctype);
		return CT_INVALID;
	}

	ctype = GetCargoIDByLabel(cl);
	if (ctype == CT_INVALID) {
		grfmsg(5, "TranslateCargo: Cargo '%c%c%c%c' unsupported, skipping.", GB(cl, 24, 8), GB(cl, 16, 8), GB(cl, 8, 8), GB(cl, 0, 8));
		return CT_INVALID;
	}

	grfmsg(6, "TranslateCargo: Cargo '%c%c%c%c' mapped to cargo type %d.", GB(cl, 24, 8), GB(cl, 16, 8), GB(cl, 8, 8), GB(cl, 0, 8), ctype);
	return ctype;
}


static bool IsValidGroupID(uint16 groupid, const char *function)
{
	if (groupid > MAX_SPRITEGROUP || _cur.spritegroups[groupid] == nullptr) {
		grfmsg(1, "%s: Spritegroup 0x%04X out of range or empty, skipping.", function, groupid);
		return false;
	}

	return true;
}

static void VehicleMapSpriteGroup(ByteReader *buf, byte feature, uint8 idcount)
{
	static EngineID *last_engines;
	static uint last_engines_count;
	bool wagover = false;

	/* Test for 'wagon override' flag */
	if (HasBit(idcount, 7)) {
		wagover = true;
		/* Strip off the flag */
		idcount = GB(idcount, 0, 7);

		if (last_engines_count == 0) {
			grfmsg(0, "VehicleMapSpriteGroup: WagonOverride: No engine to do override with");
			return;
		}

		grfmsg(6, "VehicleMapSpriteGroup: WagonOverride: %u engines, %u wagons",
				last_engines_count, idcount);
	} else {
		if (last_engines_count != idcount) {
			last_engines = ReallocT(last_engines, idcount);
			last_engines_count = idcount;
		}
	}

	EngineID *engines = AllocaM(EngineID, idcount);
	for (uint i = 0; i < idcount; i++) {
		Engine *e = GetNewEngine(_cur.grffile, (VehicleType)feature, buf->ReadExtendedByte());
		if (e == nullptr) {
			/* No engine could be allocated?!? Deal with it. Okay,
			 * this might look bad. Also make sure this NewGRF
			 * gets disabled, as a half loaded one is bad. */
			HandleChangeInfoResult("VehicleMapSpriteGroup", CIR_INVALID_ID, 0, 0);
			return;
		}

		engines[i] = e->index;
		if (!wagover) last_engines[i] = engines[i];
	}

	uint8 cidcount = buf->ReadByte();
	for (uint c = 0; c < cidcount; c++) {
		uint8 ctype = buf->ReadByte();
		uint16 groupid = buf->ReadWord();
		if (!IsValidGroupID(groupid, "VehicleMapSpriteGroup")) continue;

		grfmsg(8, "VehicleMapSpriteGroup: * [%d] Cargo type 0x%X, group id 0x%02X", c, ctype, groupid);

		ctype = TranslateCargo(feature, ctype);
		if (ctype == CT_INVALID) continue;

		for (uint i = 0; i < idcount; i++) {
			EngineID engine = engines[i];

			grfmsg(7, "VehicleMapSpriteGroup: [%d] Engine %d...", i, engine);

			if (wagover) {
				SetWagonOverrideSprites(engine, ctype, _cur.spritegroups[groupid], last_engines, last_engines_count);
			} else {
				SetCustomEngineSprites(engine, ctype, _cur.spritegroups[groupid]);
			}
		}
	}

	uint16 groupid = buf->ReadWord();
	if (!IsValidGroupID(groupid, "VehicleMapSpriteGroup")) return;

	grfmsg(8, "-- Default group id 0x%04X", groupid);

	for (uint i = 0; i < idcount; i++) {
		EngineID engine = engines[i];

		if (wagover) {
			SetWagonOverrideSprites(engine, CT_DEFAULT, _cur.spritegroups[groupid], last_engines, last_engines_count);
		} else {
			SetCustomEngineSprites(engine, CT_DEFAULT, _cur.spritegroups[groupid]);
			SetEngineGRF(engine, _cur.grffile);
		}
	}
}


static void CanalMapSpriteGroup(ByteReader *buf, uint8 idcount)
{
	CanalFeature *cfs = AllocaM(CanalFeature, idcount);
	for (uint i = 0; i < idcount; i++) {
		cfs[i] = (CanalFeature)buf->ReadByte();
	}

	uint8 cidcount = buf->ReadByte();
	buf->Skip(cidcount * 3);

	uint16 groupid = buf->ReadWord();
	if (!IsValidGroupID(groupid, "CanalMapSpriteGroup")) return;

	for (uint i = 0; i < idcount; i++) {
		CanalFeature cf = cfs[i];

		if (cf >= CF_END) {
			grfmsg(1, "CanalMapSpriteGroup: Canal subset %d out of range, skipping", cf);
			continue;
		}

		_water_feature[cf].grffile = _cur.grffile;
		_water_feature[cf].group = _cur.spritegroups[groupid];
	}
}


static void StationMapSpriteGroup(ByteReader *buf, uint8 idcount)
{
	uint8 *stations = AllocaM(uint8, idcount);
	for (uint i = 0; i < idcount; i++) {
		stations[i] = buf->ReadByte();
	}

	uint8 cidcount = buf->ReadByte();
	for (uint c = 0; c < cidcount; c++) {
		uint8 ctype = buf->ReadByte();
		uint16 groupid = buf->ReadWord();
		if (!IsValidGroupID(groupid, "StationMapSpriteGroup")) continue;

		ctype = TranslateCargo(GSF_STATIONS, ctype);
		if (ctype == CT_INVALID) continue;

		for (uint i = 0; i < idcount; i++) {
			StationSpec *statspec = _cur.grffile->stations == nullptr ? nullptr : _cur.grffile->stations[stations[i]];

			if (statspec == nullptr) {
				grfmsg(1, "StationMapSpriteGroup: Station with ID 0x%02X does not exist, skipping", stations[i]);
				continue;
			}

			statspec->grf_prop.spritegroup[ctype] = _cur.spritegroups[groupid];
		}
	}

	uint16 groupid = buf->ReadWord();
	if (!IsValidGroupID(groupid, "StationMapSpriteGroup")) return;

	for (uint i = 0; i < idcount; i++) {
		StationSpec *statspec = _cur.grffile->stations == nullptr ? nullptr : _cur.grffile->stations[stations[i]];

		if (statspec == nullptr) {
			grfmsg(1, "StationMapSpriteGroup: Station with ID 0x%02X does not exist, skipping", stations[i]);
			continue;
		}

		if (statspec->grf_prop.grffile != nullptr) {
			grfmsg(1, "StationMapSpriteGroup: Station with ID 0x%02X mapped multiple times, skipping", stations[i]);
			continue;
		}

		statspec->grf_prop.spritegroup[CT_DEFAULT] = _cur.spritegroups[groupid];
		statspec->grf_prop.grffile = _cur.grffile;
		statspec->grf_prop.local_id = stations[i];
		StationClass::Assign(statspec);
	}
}


static void TownHouseMapSpriteGroup(ByteReader *buf, uint8 idcount)
{
	uint8 *houses = AllocaM(uint8, idcount);
	for (uint i = 0; i < idcount; i++) {
		houses[i] = buf->ReadByte();
	}

	/* Skip the cargo type section, we only care about the default group */
	uint8 cidcount = buf->ReadByte();
	buf->Skip(cidcount * 3);

	uint16 groupid = buf->ReadWord();
	if (!IsValidGroupID(groupid, "TownHouseMapSpriteGroup")) return;

	if (_cur.grffile->housespec == nullptr) {
		grfmsg(1, "TownHouseMapSpriteGroup: No houses defined, skipping");
		return;
	}

	for (uint i = 0; i < idcount; i++) {
		HouseSpec *hs = _cur.grffile->housespec[houses[i]];

		if (hs == nullptr) {
			grfmsg(1, "TownHouseMapSpriteGroup: House %d undefined, skipping.", houses[i]);
			continue;
		}

		hs->grf_prop.spritegroup[0] = _cur.spritegroups[groupid];
	}
}

static void IndustryMapSpriteGroup(ByteReader *buf, uint8 idcount)
{
	uint8 *industries = AllocaM(uint8, idcount);
	for (uint i = 0; i < idcount; i++) {
		industries[i] = buf->ReadByte();
	}

	/* Skip the cargo type section, we only care about the default group */
	uint8 cidcount = buf->ReadByte();
	buf->Skip(cidcount * 3);

	uint16 groupid = buf->ReadWord();
	if (!IsValidGroupID(groupid, "IndustryMapSpriteGroup")) return;

	if (_cur.grffile->industryspec == nullptr) {
		grfmsg(1, "IndustryMapSpriteGroup: No industries defined, skipping");
		return;
	}

	for (uint i = 0; i < idcount; i++) {
		IndustrySpec *indsp = _cur.grffile->industryspec[industries[i]];

		if (indsp == nullptr) {
			grfmsg(1, "IndustryMapSpriteGroup: Industry %d undefined, skipping", industries[i]);
			continue;
		}

		indsp->grf_prop.spritegroup[0] = _cur.spritegroups[groupid];
	}
}

static void IndustrytileMapSpriteGroup(ByteReader *buf, uint8 idcount)
{
	uint8 *indtiles = AllocaM(uint8, idcount);
	for (uint i = 0; i < idcount; i++) {
		indtiles[i] = buf->ReadByte();
	}

	/* Skip the cargo type section, we only care about the default group */
	uint8 cidcount = buf->ReadByte();
	buf->Skip(cidcount * 3);

	uint16 groupid = buf->ReadWord();
	if (!IsValidGroupID(groupid, "IndustrytileMapSpriteGroup")) return;

	if (_cur.grffile->indtspec == nullptr) {
		grfmsg(1, "IndustrytileMapSpriteGroup: No industry tiles defined, skipping");
		return;
	}

	for (uint i = 0; i < idcount; i++) {
		IndustryTileSpec *indtsp = _cur.grffile->indtspec[indtiles[i]];

		if (indtsp == nullptr) {
			grfmsg(1, "IndustrytileMapSpriteGroup: Industry tile %d undefined, skipping", indtiles[i]);
			continue;
		}

		indtsp->grf_prop.spritegroup[0] = _cur.spritegroups[groupid];
	}
}

static void CargoMapSpriteGroup(ByteReader *buf, uint8 idcount)
{
	CargoID *cargoes = AllocaM(CargoID, idcount);
	for (uint i = 0; i < idcount; i++) {
		cargoes[i] = buf->ReadByte();
	}

	/* Skip the cargo type section, we only care about the default group */
	uint8 cidcount = buf->ReadByte();
	buf->Skip(cidcount * 3);

	uint16 groupid = buf->ReadWord();
	if (!IsValidGroupID(groupid, "CargoMapSpriteGroup")) return;

	for (uint i = 0; i < idcount; i++) {
		CargoID cid = cargoes[i];

		if (cid >= NUM_CARGO) {
			grfmsg(1, "CargoMapSpriteGroup: Cargo ID %d out of range, skipping", cid);
			continue;
		}

		CargoSpec *cs = CargoSpec::Get(cid);
		cs->grffile = _cur.grffile;
		cs->group = _cur.spritegroups[groupid];
	}
}

static void ObjectMapSpriteGroup(ByteReader *buf, uint8 idcount)
{
	if (_cur.grffile->objectspec == nullptr) {
		grfmsg(1, "ObjectMapSpriteGroup: No object tiles defined, skipping");
		return;
	}

	uint8 *objects = AllocaM(uint8, idcount);
	for (uint i = 0; i < idcount; i++) {
		objects[i] = buf->ReadByte();
	}

	uint8 cidcount = buf->ReadByte();
	for (uint c = 0; c < cidcount; c++) {
		uint8 ctype = buf->ReadByte();
		uint16 groupid = buf->ReadWord();
		if (!IsValidGroupID(groupid, "ObjectMapSpriteGroup")) continue;

		ctype = TranslateCargo(GSF_OBJECTS, ctype);
		if (ctype == CT_INVALID) continue;

		for (uint i = 0; i < idcount; i++) {
			ObjectSpec *spec = _cur.grffile->objectspec[objects[i]];

			if (spec == nullptr) {
				grfmsg(1, "ObjectMapSpriteGroup: Object with ID 0x%02X undefined, skipping", objects[i]);
				continue;
			}

			spec->grf_prop.spritegroup[ctype] = _cur.spritegroups[groupid];
		}
	}

	uint16 groupid = buf->ReadWord();
	if (!IsValidGroupID(groupid, "ObjectMapSpriteGroup")) return;

	for (uint i = 0; i < idcount; i++) {
		ObjectSpec *spec = _cur.grffile->objectspec[objects[i]];

		if (spec == nullptr) {
			grfmsg(1, "ObjectMapSpriteGroup: Object with ID 0x%02X undefined, skipping", objects[i]);
			continue;
		}

		if (spec->grf_prop.grffile != nullptr) {
			grfmsg(1, "ObjectMapSpriteGroup: Object with ID 0x%02X mapped multiple times, skipping", objects[i]);
			continue;
		}

		spec->grf_prop.spritegroup[0] = _cur.spritegroups[groupid];
		spec->grf_prop.grffile        = _cur.grffile;
		spec->grf_prop.local_id       = objects[i];
	}
}

static void RailTypeMapSpriteGroup(ByteReader *buf, uint8 idcount)
{
	uint8 *railtypes = AllocaM(uint8, idcount);
	for (uint i = 0; i < idcount; i++) {
		uint8 id = buf->ReadByte();
		railtypes[i] = id < RAILTYPE_END ? _cur.grffile->railtype_map[id] : INVALID_RAILTYPE;
	}

	uint8 cidcount = buf->ReadByte();
	for (uint c = 0; c < cidcount; c++) {
		uint8 ctype = buf->ReadByte();
		uint16 groupid = buf->ReadWord();
		if (!IsValidGroupID(groupid, "RailTypeMapSpriteGroup")) continue;

		if (ctype >= RTSG_END) continue;

		extern RailtypeInfo _railtypes[RAILTYPE_END];
		for (uint i = 0; i < idcount; i++) {
			if (railtypes[i] != INVALID_RAILTYPE) {
				RailtypeInfo *rti = &_railtypes[railtypes[i]];

				rti->grffile[ctype] = _cur.grffile;
				rti->group[ctype] = _cur.spritegroups[groupid];
			}
		}
	}

	/* Railtypes do not use the default group. */
	buf->ReadWord();
}

static void RoadTypeMapSpriteGroup(ByteReader *buf, uint8 idcount, RoadTramType rtt)
{
	RoadType *type_map = (rtt == RTT_TRAM) ? _cur.grffile->tramtype_map : _cur.grffile->roadtype_map;

	uint8 *roadtypes = AllocaM(uint8, idcount);
	for (uint i = 0; i < idcount; i++) {
		uint8 id = buf->ReadByte();
		roadtypes[i] = id < ROADTYPE_END ? type_map[id] : INVALID_ROADTYPE;
	}

	uint8 cidcount = buf->ReadByte();
	for (uint c = 0; c < cidcount; c++) {
		uint8 ctype = buf->ReadByte();
		uint16 groupid = buf->ReadWord();
		if (!IsValidGroupID(groupid, "RoadTypeMapSpriteGroup")) continue;

		if (ctype >= ROTSG_END) continue;

		extern RoadTypeInfo _roadtypes[ROADTYPE_END];
		for (uint i = 0; i < idcount; i++) {
			if (roadtypes[i] != INVALID_ROADTYPE) {
				RoadTypeInfo *rti = &_roadtypes[roadtypes[i]];

				rti->grffile[ctype] = _cur.grffile;
				rti->group[ctype] = _cur.spritegroups[groupid];
			}
		}
	}

	/* Roadtypes do not use the default group. */
	buf->ReadWord();
}

static void AirportMapSpriteGroup(ByteReader *buf, uint8 idcount)
{
	uint8 *airports = AllocaM(uint8, idcount);
	for (uint i = 0; i < idcount; i++) {
		airports[i] = buf->ReadByte();
	}

	/* Skip the cargo type section, we only care about the default group */
	uint8 cidcount = buf->ReadByte();
	buf->Skip(cidcount * 3);

	uint16 groupid = buf->ReadWord();
	if (!IsValidGroupID(groupid, "AirportMapSpriteGroup")) return;

	if (_cur.grffile->airportspec == nullptr) {
		grfmsg(1, "AirportMapSpriteGroup: No airports defined, skipping");
		return;
	}

	for (uint i = 0; i < idcount; i++) {
		AirportSpec *as = _cur.grffile->airportspec[airports[i]];

		if (as == nullptr) {
			grfmsg(1, "AirportMapSpriteGroup: Airport %d undefined, skipping", airports[i]);
			continue;
		}

		as->grf_prop.spritegroup[0] = _cur.spritegroups[groupid];
	}
}

static void AirportTileMapSpriteGroup(ByteReader *buf, uint8 idcount)
{
	uint8 *airptiles = AllocaM(uint8, idcount);
	for (uint i = 0; i < idcount; i++) {
		airptiles[i] = buf->ReadByte();
	}

	/* Skip the cargo type section, we only care about the default group */
	uint8 cidcount = buf->ReadByte();
	buf->Skip(cidcount * 3);

	uint16 groupid = buf->ReadWord();
	if (!IsValidGroupID(groupid, "AirportTileMapSpriteGroup")) return;

	if (_cur.grffile->airtspec == nullptr) {
		grfmsg(1, "AirportTileMapSpriteGroup: No airport tiles defined, skipping");
		return;
	}

	for (uint i = 0; i < idcount; i++) {
		AirportTileSpec *airtsp = _cur.grffile->airtspec[airptiles[i]];

		if (airtsp == nullptr) {
			grfmsg(1, "AirportTileMapSpriteGroup: Airport tile %d undefined, skipping", airptiles[i]);
			continue;
		}

		airtsp->grf_prop.spritegroup[0] = _cur.spritegroups[groupid];
	}
}

static void RoadStopMapSpriteGroup(ByteReader *buf, uint8 idcount) {
	uint8 *roadstops = AllocaM(uint8, idcount);
	for (uint i = 0; i < idcount; i++) {
		roadstops[i] = buf->ReadByte();
	}

	/* Skip the cargo type section, we only care about the default group */
	uint8 cidcount = buf->ReadByte();
	buf->Skip(cidcount * 3);

	uint16 groupid = buf->ReadWord();
	if (!IsValidGroupID(groupid, "RoadStopMapSpriteGroup")) return;

	if (_cur.grffile->roadstops == nullptr) {
		grfmsg(0, "RoadStopMapSpriteGroup: No roadstops defined, skipping.");
		return;
	}

	for (uint i = 0; i < idcount; i++) {
		RoadStopSpec *roadstopspec = _cur.grffile->roadstops == nullptr ? nullptr : _cur.grffile->roadstops[roadstops[i]];

		if (roadstopspec == nullptr) {
			grfmsg(1, "RoadStopMapSpriteGroup: Road stop with ID 0x%02X does not exist, skipping.", roadstops[i]);
			continue;
		}

		if (roadstopspec->grf_prop.grffile != nullptr) {
			grfmsg(1, "RoadStopMapSpriteGroup: Road stop with ID 0x%02X mapped multiple times, skipping", roadstops[i]);
			continue;
		}

		roadstopspec->grf_prop.spritegroup[CT_DEFAULT] = _cur.spritegroups[groupid];
		roadstopspec->grf_prop.grffile = _cur.grffile;
		roadstopspec->grf_prop.local_id = roadstops[i];
		RoadStopClass::Assign(roadstopspec);
	}
}

/* Action 0x03 */
static void FeatureMapSpriteGroup(ByteReader *buf)
{
	/* <03> <feature> <n-id> <ids>... <num-cid> [<cargo-type> <cid>]... <def-cid>
	 * id-list    := [<id>] [id-list]
	 * cargo-list := <cargo-type> <cid> [cargo-list]
	 *
	 * B feature       see action 0
	 * B n-id          bits 0-6: how many IDs this definition applies to
	 *                 bit 7: if set, this is a wagon override definition (see below)
	 * B ids           the IDs for which this definition applies
	 * B num-cid       number of cargo IDs (sprite group IDs) in this definition
	 *                 can be zero, in that case the def-cid is used always
	 * B cargo-type    type of this cargo type (e.g. mail=2, wood=7, see below)
	 * W cid           cargo ID (sprite group ID) for this type of cargo
	 * W def-cid       default cargo ID (sprite group ID) */

	uint8 feature = buf->ReadByte();
	uint8 idcount = buf->ReadByte();

	if (feature >= GSF_END) {
		grfmsg(1, "FeatureMapSpriteGroup: Unsupported feature 0x%02X, skipping", feature);
		return;
	}

	/* If idcount is zero, this is a feature callback */
	if (idcount == 0) {
		/* Skip number of cargo ids? */
		buf->ReadByte();
		uint16 groupid = buf->ReadWord();
		if (!IsValidGroupID(groupid, "FeatureMapSpriteGroup")) return;

		grfmsg(6, "FeatureMapSpriteGroup: Adding generic feature callback for feature 0x%02X", feature);

		AddGenericCallback(feature, _cur.grffile, _cur.spritegroups[groupid]);
		return;
	}

	/* Mark the feature as used by the grf (generic callbacks do not count) */
	SetBit(_cur.grffile->grf_features, feature);

	grfmsg(6, "FeatureMapSpriteGroup: Feature 0x%02X, %d ids", feature, idcount);

	switch (feature) {
		case GSF_TRAINS:
		case GSF_ROADVEHICLES:
		case GSF_SHIPS:
		case GSF_AIRCRAFT:
			VehicleMapSpriteGroup(buf, feature, idcount);
			return;

		case GSF_CANALS:
			CanalMapSpriteGroup(buf, idcount);
			return;

		case GSF_STATIONS:
			StationMapSpriteGroup(buf, idcount);
			return;

		case GSF_HOUSES:
			TownHouseMapSpriteGroup(buf, idcount);
			return;

		case GSF_INDUSTRIES:
			IndustryMapSpriteGroup(buf, idcount);
			return;

		case GSF_INDUSTRYTILES:
			IndustrytileMapSpriteGroup(buf, idcount);
			return;

		case GSF_CARGOES:
			CargoMapSpriteGroup(buf, idcount);
			return;

		case GSF_AIRPORTS:
			AirportMapSpriteGroup(buf, idcount);
			return;

		case GSF_OBJECTS:
			ObjectMapSpriteGroup(buf, idcount);
			break;

		case GSF_RAILTYPES:
			RailTypeMapSpriteGroup(buf, idcount);
			break;

		case GSF_ROADTYPES:
			RoadTypeMapSpriteGroup(buf, idcount, RTT_ROAD);
			break;

		case GSF_TRAMTYPES:
			RoadTypeMapSpriteGroup(buf, idcount, RTT_TRAM);
			break;

		case GSF_AIRPORTTILES:
			AirportTileMapSpriteGroup(buf, idcount);
			return;

		case GSF_ROADSTOPS:
			RoadStopMapSpriteGroup(buf, idcount);
			return;

		default:
			grfmsg(1, "FeatureMapSpriteGroup: Unsupported feature 0x%02X, skipping", feature);
			return;
	}
}

/* Action 0x04 */
static void FeatureNewName(ByteReader *buf)
{
	/* <04> <veh-type> <language-id> <num-veh> <offset> <data...>
	 *
	 * B veh-type      see action 0 (as 00..07, + 0A
	 *                 But IF veh-type = 48, then generic text
	 * B language-id   If bit 6 is set, This is the extended language scheme,
	 *                 with up to 64 language.
	 *                 Otherwise, it is a mapping where set bits have meaning
	 *                 0 = american, 1 = english, 2 = german, 3 = french, 4 = spanish
	 *                 Bit 7 set means this is a generic text, not a vehicle one (or else)
	 * B num-veh       number of vehicles which are getting a new name
	 * B/W offset      number of the first vehicle that gets a new name
	 *                 Byte : ID of vehicle to change
	 *                 Word : ID of string to change/add
	 * S data          new texts, each of them zero-terminated, after
	 *                 which the next name begins. */

	bool new_scheme = _cur.grffile->grf_version >= 7;

	uint8 feature  = buf->ReadByte();
	if (feature >= GSF_END && feature != 0x48) {
		grfmsg(1, "FeatureNewName: Unsupported feature 0x%02X, skipping", feature);
		return;
	}

	uint8 lang     = buf->ReadByte();
	uint8 num      = buf->ReadByte();
	bool generic   = HasBit(lang, 7);
	uint16 id;
	if (generic) {
		id = buf->ReadWord();
	} else if (feature <= GSF_AIRCRAFT) {
		id = buf->ReadExtendedByte();
	} else {
		id = buf->ReadByte();
	}

	ClrBit(lang, 7);

	uint16 endid = id + num;

	grfmsg(6, "FeatureNewName: About to rename engines %d..%d (feature 0x%02X) in language 0x%02X",
	               id, endid, feature, lang);

	for (; id < endid && buf->HasData(); id++) {
		const char *name = buf->ReadString();
		grfmsg(8, "FeatureNewName: 0x%04X <- %s", id, name);

		switch (feature) {
			case GSF_TRAINS:
			case GSF_ROADVEHICLES:
			case GSF_SHIPS:
			case GSF_AIRCRAFT:
				if (!generic) {
					Engine *e = GetNewEngine(_cur.grffile, (VehicleType)feature, id, HasBit(_cur.grfconfig->flags, GCF_STATIC));
					if (e == nullptr) break;
					StringID string = AddGRFString(_cur.grffile->grfid, e->index, lang, new_scheme, false, name, e->info.string_id);
					e->info.string_id = string;
				} else {
					AddGRFString(_cur.grffile->grfid, id, lang, new_scheme, true, name, STR_UNDEFINED);
				}
				break;

			default:
				if (IsInsideMM(id, 0xD000, 0xD400) || IsInsideMM(id, 0xD800, 0xE000)) {
					AddGRFString(_cur.grffile->grfid, id, lang, new_scheme, true, name, STR_UNDEFINED);
					break;
				}

				switch (GB(id, 8, 8)) {
					case 0xC4: // Station class name
						if (_cur.grffile->stations == nullptr || _cur.grffile->stations[GB(id, 0, 8)] == nullptr) {
							grfmsg(1, "FeatureNewName: Attempt to name undefined station 0x%X, ignoring", GB(id, 0, 8));
						} else {
							StationClassID cls_id = _cur.grffile->stations[GB(id, 0, 8)]->cls_id;
							StationClass::Get(cls_id)->name = AddGRFString(_cur.grffile->grfid, id, lang, new_scheme, false, name, STR_UNDEFINED);
						}
						break;

					case 0xC5: // Station name
						if (_cur.grffile->stations == nullptr || _cur.grffile->stations[GB(id, 0, 8)] == nullptr) {
							grfmsg(1, "FeatureNewName: Attempt to name undefined station 0x%X, ignoring", GB(id, 0, 8));
						} else {
							_cur.grffile->stations[GB(id, 0, 8)]->name = AddGRFString(_cur.grffile->grfid, id, lang, new_scheme, false, name, STR_UNDEFINED);
						}
						break;

					case 0xC7: // Airporttile name
						if (_cur.grffile->airtspec == nullptr || _cur.grffile->airtspec[GB(id, 0, 8)] == nullptr) {
							grfmsg(1, "FeatureNewName: Attempt to name undefined airport tile 0x%X, ignoring", GB(id, 0, 8));
						} else {
							_cur.grffile->airtspec[GB(id, 0, 8)]->name = AddGRFString(_cur.grffile->grfid, id, lang, new_scheme, false, name, STR_UNDEFINED);
						}
						break;

					case 0xC9: // House name
						if (_cur.grffile->housespec == nullptr || _cur.grffile->housespec[GB(id, 0, 8)] == nullptr) {
							grfmsg(1, "FeatureNewName: Attempt to name undefined house 0x%X, ignoring.", GB(id, 0, 8));
						} else {
							_cur.grffile->housespec[GB(id, 0, 8)]->building_name = AddGRFString(_cur.grffile->grfid, id, lang, new_scheme, false, name, STR_UNDEFINED);
						}
						break;

					default:
						grfmsg(7, "FeatureNewName: Unsupported ID (0x%04X)", id);
						break;
				}
				break;
		}
	}
}

/**
 * Sanitize incoming sprite offsets for Action 5 graphics replacements.
 * @param num         The number of sprites to load.
 * @param offset      Offset from the base.
 * @param max_sprites The maximum number of sprites that can be loaded in this action 5.
 * @param name        Used for error warnings.
 * @return The number of sprites that is going to be skipped.
 */
static uint16 SanitizeSpriteOffset(uint16& num, uint16 offset, int max_sprites, const char *name)
{

	if (offset >= max_sprites) {
		grfmsg(1, "GraphicsNew: %s sprite offset must be less than %i, skipping", name, max_sprites);
		uint orig_num = num;
		num = 0;
		return orig_num;
	}

	if (offset + num > max_sprites) {
		grfmsg(4, "GraphicsNew: %s sprite overflow, truncating...", name);
		uint orig_num = num;
		num = std::max(max_sprites - offset, 0);
		return orig_num - num;
	}

	return 0;
}


/** The type of action 5 type. */
enum Action5BlockType {
	A5BLOCK_FIXED,                ///< Only allow replacing a whole block of sprites. (TTDP compatible)
	A5BLOCK_ALLOW_OFFSET,         ///< Allow replacing any subset by specifiing an offset.
	A5BLOCK_INVALID,              ///< unknown/not-implemented type
};
/** Information about a single action 5 type. */
struct Action5Type {
	Action5BlockType block_type;  ///< How is this Action5 type processed?
	SpriteID sprite_base;         ///< Load the sprites starting from this sprite.
	uint16 min_sprites;           ///< If the Action5 contains less sprites, the whole block will be ignored.
	uint16 max_sprites;           ///< If the Action5 contains more sprites, only the first max_sprites sprites will be used.
	const char *name;             ///< Name for error messages.
};

/** The information about action 5 types. */
static const Action5Type _action5_types[] = {
	/* Note: min_sprites should not be changed. Therefore these constants are directly here and not in sprites.h */
	/* 0x00 */ { A5BLOCK_INVALID,      0,                            0, 0,                                           "Type 0x00"                },
	/* 0x01 */ { A5BLOCK_INVALID,      0,                            0, 0,                                           "Type 0x01"                },
	/* 0x02 */ { A5BLOCK_INVALID,      0,                            0, 0,                                           "Type 0x02"                },
	/* 0x03 */ { A5BLOCK_INVALID,      0,                            0, 0,                                           "Type 0x03"                },
	/* 0x04 */ { A5BLOCK_ALLOW_OFFSET, SPR_SIGNALS_BASE,             1, PRESIGNAL_SEMAPHORE_AND_PBS_SPRITE_COUNT,    "Signal graphics"          },
	/* 0x05 */ { A5BLOCK_ALLOW_OFFSET, SPR_ELRAIL_BASE,              1, ELRAIL_SPRITE_COUNT,                         "Rail catenary graphics"   },
	/* 0x06 */ { A5BLOCK_ALLOW_OFFSET, SPR_SLOPES_BASE,              1, NORMAL_AND_HALFTILE_FOUNDATION_SPRITE_COUNT, "Foundation graphics"      },
	/* 0x07 */ { A5BLOCK_INVALID,      0,                           75, 0,                                           "TTDP GUI graphics"        }, // Not used by OTTD.
	/* 0x08 */ { A5BLOCK_ALLOW_OFFSET, SPR_CANALS_BASE,              1, CANALS_SPRITE_COUNT,                         "Canal graphics"           },
	/* 0x09 */ { A5BLOCK_ALLOW_OFFSET, SPR_ONEWAY_BASE,              1, ONEWAY_SPRITE_COUNT,                         "One way road graphics"    },
	/* 0x0A */ { A5BLOCK_ALLOW_OFFSET, SPR_2CCMAP_BASE,              1, TWOCCMAP_SPRITE_COUNT,                       "2CC colour maps"          },
	/* 0x0B */ { A5BLOCK_ALLOW_OFFSET, SPR_TRAMWAY_BASE,             1, TRAMWAY_SPRITE_COUNT,                        "Tramway graphics"         },
	/* 0x0C */ { A5BLOCK_INVALID,      0,                          133, 0,                                           "Snowy temperate tree"     }, // Not yet used by OTTD.
	/* 0x0D */ { A5BLOCK_FIXED,        SPR_SHORE_BASE,              16, SPR_SHORE_SPRITE_COUNT,                      "Shore graphics"           },
	/* 0x0E */ { A5BLOCK_INVALID,      0,                            0, 0,                                           "New Signals graphics"     }, // Not yet used by OTTD.
	/* 0x0F */ { A5BLOCK_ALLOW_OFFSET, SPR_TRACKS_FOR_SLOPES_BASE,   1, TRACKS_FOR_SLOPES_SPRITE_COUNT,              "Sloped rail track"        },
	/* 0x10 */ { A5BLOCK_ALLOW_OFFSET, SPR_AIRPORTX_BASE,            1, AIRPORTX_SPRITE_COUNT,                       "Airport graphics"         },
	/* 0x11 */ { A5BLOCK_ALLOW_OFFSET, SPR_ROADSTOP_BASE,            1, ROADSTOP_SPRITE_COUNT,                       "Road stop graphics"       },
	/* 0x12 */ { A5BLOCK_ALLOW_OFFSET, SPR_AQUEDUCT_BASE,            1, AQUEDUCT_SPRITE_COUNT,                       "Aqueduct graphics"        },
	/* 0x13 */ { A5BLOCK_ALLOW_OFFSET, SPR_AUTORAIL_BASE,            1, AUTORAIL_SPRITE_COUNT,                       "Autorail graphics"        },
	/* 0x14 */ { A5BLOCK_ALLOW_OFFSET, SPR_FLAGS_BASE,               1, FLAGS_SPRITE_COUNT,                          "Flag graphics"            },
	/* 0x15 */ { A5BLOCK_ALLOW_OFFSET, SPR_OPENTTD_BASE,             1, OPENTTD_SPRITE_COUNT,                        "OpenTTD GUI graphics"     },
	/* 0x16 */ { A5BLOCK_ALLOW_OFFSET, SPR_AIRPORT_PREVIEW_BASE,     1, SPR_AIRPORT_PREVIEW_COUNT,                   "Airport preview graphics" },
	/* 0x17 */ { A5BLOCK_ALLOW_OFFSET, SPR_RAILTYPE_TUNNEL_BASE,     1, RAILTYPE_TUNNEL_BASE_COUNT,                  "Railtype tunnel base"     },
	/* 0x18 */ { A5BLOCK_ALLOW_OFFSET, SPR_PALETTE_BASE,             1, PALETTE_SPRITE_COUNT,                        "Palette"                  },
};

/* Action 0x05 */
static void GraphicsNew(ByteReader *buf)
{
	/* <05> <graphics-type> <num-sprites> <other data...>
	 *
	 * B graphics-type What set of graphics the sprites define.
	 * E num-sprites   How many sprites are in this set?
	 * V other data    Graphics type specific data.  Currently unused. */

	uint8 type = buf->ReadByte();
	uint16 num = buf->ReadExtendedByte();
	uint16 offset = HasBit(type, 7) ? buf->ReadExtendedByte() : 0;
	ClrBit(type, 7); // Clear the high bit as that only indicates whether there is an offset.

	if ((type == 0x0D) && (num == 10) && HasBit(_cur.grfconfig->flags, GCF_SYSTEM)) {
		/* Special not-TTDP-compatible case used in openttd.grf
		 * Missing shore sprites and initialisation of SPR_SHORE_BASE */
		grfmsg(2, "GraphicsNew: Loading 10 missing shore sprites from extra grf.");
		LoadNextSprite(SPR_SHORE_BASE +  0, _cur.file_index, _cur.nfo_line++, _cur.grf_container_ver); // SLOPE_STEEP_S
		LoadNextSprite(SPR_SHORE_BASE +  5, _cur.file_index, _cur.nfo_line++, _cur.grf_container_ver); // SLOPE_STEEP_W
		LoadNextSprite(SPR_SHORE_BASE +  7, _cur.file_index, _cur.nfo_line++, _cur.grf_container_ver); // SLOPE_WSE
		LoadNextSprite(SPR_SHORE_BASE + 10, _cur.file_index, _cur.nfo_line++, _cur.grf_container_ver); // SLOPE_STEEP_N
		LoadNextSprite(SPR_SHORE_BASE + 11, _cur.file_index, _cur.nfo_line++, _cur.grf_container_ver); // SLOPE_NWS
		LoadNextSprite(SPR_SHORE_BASE + 13, _cur.file_index, _cur.nfo_line++, _cur.grf_container_ver); // SLOPE_ENW
		LoadNextSprite(SPR_SHORE_BASE + 14, _cur.file_index, _cur.nfo_line++, _cur.grf_container_ver); // SLOPE_SEN
		LoadNextSprite(SPR_SHORE_BASE + 15, _cur.file_index, _cur.nfo_line++, _cur.grf_container_ver); // SLOPE_STEEP_E
		LoadNextSprite(SPR_SHORE_BASE + 16, _cur.file_index, _cur.nfo_line++, _cur.grf_container_ver); // SLOPE_EW
		LoadNextSprite(SPR_SHORE_BASE + 17, _cur.file_index, _cur.nfo_line++, _cur.grf_container_ver); // SLOPE_NS
		if (_loaded_newgrf_features.shore == SHORE_REPLACE_NONE) _loaded_newgrf_features.shore = SHORE_REPLACE_ONLY_NEW;
		return;
	}

	/* Supported type? */
	if ((type >= lengthof(_action5_types)) || (_action5_types[type].block_type == A5BLOCK_INVALID)) {
		grfmsg(2, "GraphicsNew: Custom graphics (type 0x%02X) sprite block of length %u (unimplemented, ignoring)", type, num);
		_cur.skip_sprites = num;
		return;
	}

	const Action5Type *action5_type = &_action5_types[type];

	/* Contrary to TTDP we allow always to specify too few sprites as we allow always an offset,
	 * except for the long version of the shore type:
	 * Ignore offset if not allowed */
	if ((action5_type->block_type != A5BLOCK_ALLOW_OFFSET) && (offset != 0)) {
		grfmsg(1, "GraphicsNew: %s (type 0x%02X) do not allow an <offset> field. Ignoring offset.", action5_type->name, type);
		offset = 0;
	}

	/* Ignore action5 if too few sprites are specified. (for TTDP compatibility)
	 * This does not make sense, if <offset> is allowed */
	if ((action5_type->block_type == A5BLOCK_FIXED) && (num < action5_type->min_sprites)) {
		grfmsg(1, "GraphicsNew: %s (type 0x%02X) count must be at least %d. Only %d were specified. Skipping.", action5_type->name, type, action5_type->min_sprites, num);
		_cur.skip_sprites = num;
		return;
	}

	/* Load at most max_sprites sprites. Skip remaining sprites. (for compatibility with TTDP and future extensions) */
	uint16 skip_num = SanitizeSpriteOffset(num, offset, action5_type->max_sprites, action5_type->name);
	SpriteID replace = action5_type->sprite_base + offset;

	/* Load <num> sprites starting from <replace>, then skip <skip_num> sprites. */
	grfmsg(2, "GraphicsNew: Replacing sprites %d to %d of %s (type 0x%02X) at SpriteID 0x%04X", offset, offset + num - 1, action5_type->name, type, replace);

	if (type == 0x0D) _loaded_newgrf_features.shore = SHORE_REPLACE_ACTION_5;

	if (type == 0x0B) {
		static const SpriteID depot_with_track_offset = SPR_TRAMWAY_DEPOT_WITH_TRACK - SPR_TRAMWAY_BASE;
		static const SpriteID depot_no_track_offset = SPR_TRAMWAY_DEPOT_NO_TRACK - SPR_TRAMWAY_BASE;
		if (offset <= depot_with_track_offset && offset + num > depot_with_track_offset) _loaded_newgrf_features.tram = TRAMWAY_REPLACE_DEPOT_WITH_TRACK;
		if (offset <= depot_no_track_offset && offset + num > depot_no_track_offset) _loaded_newgrf_features.tram = TRAMWAY_REPLACE_DEPOT_NO_TRACK;
	}

	for (; num > 0; num--) {
		_cur.nfo_line++;
		LoadNextSprite(replace == 0 ? _cur.spriteid++ : replace++, _cur.file_index, _cur.nfo_line, _cur.grf_container_ver);
	}

	_cur.skip_sprites = skip_num;
}

/* Action 0x05 (SKIP) */
static void SkipAct5(ByteReader *buf)
{
	/* Ignore type byte */
	buf->ReadByte();

	/* Skip the sprites of this action */
	_cur.skip_sprites = buf->ReadExtendedByte();

	grfmsg(3, "SkipAct5: Skipping %d sprites", _cur.skip_sprites);
}

/**
 * Reads a variable common to VarAction2 and Action7/9/D.
 *
 * Returns VarAction2 variable 'param' resp. Action7/9/D variable '0x80 + param'.
 * If a variable is not accessible from all four actions, it is handled in the action specific functions.
 *
 * @param param variable number (as for VarAction2, for Action7/9/D you have to subtract 0x80 first).
 * @param value returns the value of the variable.
 * @param grffile NewGRF querying the variable
 * @return true iff the variable is known and the value is returned in 'value'.
 */
bool GetGlobalVariable(byte param, uint32 *value, const GRFFile *grffile)
{
	switch (param) {
		case 0x00: // current date
			*value = std::max(_date - DAYS_TILL_ORIGINAL_BASE_YEAR, 0);
			return true;

		case 0x01: // current year
			*value = Clamp(_cur_year, ORIGINAL_BASE_YEAR, ORIGINAL_MAX_YEAR) - ORIGINAL_BASE_YEAR;
			return true;

		case 0x02: { // detailed date information: month of year (bit 0-7), day of month (bit 8-12), leap year (bit 15), day of year (bit 16-24)
			YearMonthDay ymd;
			ConvertDateToYMD(_date, &ymd);
			Date start_of_year = ConvertYMDToDate(ymd.year, 0, 1);
			*value = ymd.month | (ymd.day - 1) << 8 | (IsLeapYear(ymd.year) ? 1 << 15 : 0) | (_date - start_of_year) << 16;
			return true;
		}

		case 0x03: // current climate, 0=temp, 1=arctic, 2=trop, 3=toyland
			*value = _settings_game.game_creation.landscape;
			return true;

		case 0x06: // road traffic side, bit 4 clear=left, set=right
			*value = _settings_game.vehicle.road_side << 4;
			return true;

		case 0x09: // date fraction
			*value = _date_fract * 885;
			return true;

		case 0x0A: // animation counter
			*value = _tick_counter;
			return true;

		case 0x0B: { // TTDPatch version
			uint major    = 2;
			uint minor    = 6;
			uint revision = 1; // special case: 2.0.1 is 2.0.10
			uint build    = 1382;
			*value = (major << 24) | (minor << 20) | (revision << 16) | build;
			return true;
		}

		case 0x0D: // TTD Version, 00=DOS, 01=Windows
			*value = _cur.grfconfig->palette & GRFP_USE_MASK;
			return true;

		case 0x0E: // Y-offset for train sprites
			*value = _cur.grffile->traininfo_vehicle_pitch;
			return true;

		case 0x0F: // Rail track type cost factors
			*value = 0;
			SB(*value, 0, 8, GetRailTypeInfo(RAILTYPE_RAIL)->cost_multiplier); // normal rail
			if (_settings_game.vehicle.disable_elrails) {
				/* skip elrail multiplier - disabled */
				SB(*value, 8, 8, GetRailTypeInfo(RAILTYPE_MONO)->cost_multiplier); // monorail
			} else {
				SB(*value, 8, 8, GetRailTypeInfo(RAILTYPE_ELECTRIC)->cost_multiplier); // electified railway
				/* Skip monorail multiplier - no space in result */
			}
			SB(*value, 16, 8, GetRailTypeInfo(RAILTYPE_MAGLEV)->cost_multiplier); // maglev
			return true;

		case 0x11: // current rail tool type
			*value = 0; // constant fake value to avoid desync
			return true;

		case 0x12: // Game mode
			*value = _game_mode;
			return true;

		/* case 0x13: // Tile refresh offset to left    not implemented */
		/* case 0x14: // Tile refresh offset to right   not implemented */
		/* case 0x15: // Tile refresh offset upwards    not implemented */
		/* case 0x16: // Tile refresh offset downwards  not implemented */
		/* case 0x17: // temperate snow line            not implemented */

		case 0x1A: // Always -1
			*value = UINT_MAX;
			return true;

		case 0x1B: // Display options
			*value = 0x3F; // constant fake value to avoid desync
			return true;

		case 0x1D: // TTD Platform, 00=TTDPatch, 01=OpenTTD
			*value = 1;
			return true;

		case 0x1E: // Miscellaneous GRF features
			*value = _misc_grf_features;

			/* Add the local flags */
			assert(!HasBit(*value, GMB_TRAIN_WIDTH_32_PIXELS));
			if (_cur.grffile->traininfo_vehicle_width == VEHICLEINFO_FULL_VEHICLE_WIDTH) SetBit(*value, GMB_TRAIN_WIDTH_32_PIXELS);
			return true;

		/* case 0x1F: // locale dependent settings not implemented to avoid desync */

		case 0x20: { // snow line height
			byte snowline = GetSnowLine();
			if (_settings_game.game_creation.landscape == LT_ARCTIC && snowline <= _settings_game.construction.max_heightlevel) {
				*value = Clamp(snowline * (grffile->grf_version >= 8 ? 1 : TILE_HEIGHT), 0, 0xFE);
			} else {
				/* No snow */
				*value = 0xFF;
			}
			return true;
		}

		case 0x21: // OpenTTD version
			*value = _openttd_newgrf_version;
			return true;

		case 0x22: // difficulty level
			*value = SP_CUSTOM;
			return true;

		case 0x23: // long format date
			*value = _date;
			return true;

		case 0x24: // long format year
			*value = _cur_year;
			return true;

		default: return false;
	}
}

static uint32 GetParamVal(byte param, uint32 *cond_val)
{
	/* First handle variable common with VarAction2 */
	uint32 value;
	if (GetGlobalVariable(param - 0x80, &value, _cur.grffile)) return value;

	/* Non-common variable */
	switch (param) {
		case 0x84: { // GRF loading stage
			uint32 res = 0;

			if (_cur.stage > GLS_INIT) SetBit(res, 0);
			if (_cur.stage == GLS_RESERVE) SetBit(res, 8);
			if (_cur.stage == GLS_ACTIVATION) SetBit(res, 9);
			return res;
		}

		case 0x85: // TTDPatch flags, only for bit tests
			if (cond_val == nullptr) {
				/* Supported in Action 0x07 and 0x09, not 0x0D */
				return 0;
			} else {
				uint32 index = *cond_val / 0x20;
				uint32 param_val = index < lengthof(_ttdpatch_flags) ? _ttdpatch_flags[index] : 0;
				*cond_val %= 0x20;
				return param_val;
			}

		case 0x88: // GRF ID check
			return 0;

		/* case 0x99: Global ID offset not implemented */

		default:
			/* GRF Parameter */
			if (param < 0x80) return _cur.grffile->GetParam(param);

			/* In-game variable. */
			grfmsg(1, "Unsupported in-game variable 0x%02X", param);
			return UINT_MAX;
	}
}

/* Action 0x06 */
static void CfgApply(ByteReader *buf)
{
	/* <06> <param-num> <param-size> <offset> ... <FF>
	 *
	 * B param-num     Number of parameter to substitute (First = "zero")
	 *                 Ignored if that parameter was not specified in newgrf.cfg
	 * B param-size    How many bytes to replace.  If larger than 4, the
	 *                 bytes of the following parameter are used.  In that
	 *                 case, nothing is applied unless *all* parameters
	 *                 were specified.
	 * B offset        Offset into data from beginning of next sprite
	 *                 to place where parameter is to be stored. */

	/* Preload the next sprite */
	size_t pos = FioGetPos();
	uint32 num = _cur.grf_container_ver >= 2 ? FioReadDword() : FioReadWord();
	uint8 type = FioReadByte();
	byte *preload_sprite = nullptr;

	/* Check if the sprite is a pseudo sprite. We can't operate on real sprites. */
	if (type == 0xFF) {
		preload_sprite = MallocT<byte>(num);
		FioReadBlock(preload_sprite, num);
	}

	/* Reset the file position to the start of the next sprite */
	FioSeekTo(pos, SEEK_SET);

	if (type != 0xFF) {
		grfmsg(2, "CfgApply: Ignoring (next sprite is real, unsupported)");
		free(preload_sprite);
		return;
	}

	GRFLocation location(_cur.grfconfig->ident.grfid, _cur.nfo_line + 1);
	GRFLineToSpriteOverride::iterator it = _grf_line_to_action6_sprite_override.find(location);
	if (it != _grf_line_to_action6_sprite_override.end()) {
		free(preload_sprite);
		preload_sprite = _grf_line_to_action6_sprite_override[location];
	} else {
		_grf_line_to_action6_sprite_override[location] = preload_sprite;
	}

	/* Now perform the Action 0x06 on our data. */

	for (;;) {
		uint i;
		uint param_num;
		uint param_size;
		uint offset;
		bool add_value;

		/* Read the parameter to apply. 0xFF indicates no more data to change. */
		param_num = buf->ReadByte();
		if (param_num == 0xFF) break;

		/* Get the size of the parameter to use. If the size covers multiple
		 * double words, sequential parameter values are used. */
		param_size = buf->ReadByte();

		/* Bit 7 of param_size indicates we should add to the original value
		 * instead of replacing it. */
		add_value  = HasBit(param_size, 7);
		param_size = GB(param_size, 0, 7);

		/* Where to apply the data to within the pseudo sprite data. */
		offset     = buf->ReadExtendedByte();

		/* If the parameter is a GRF parameter (not an internal variable) check
		 * if it (and all further sequential parameters) has been defined. */
		if (param_num < 0x80 && (param_num + (param_size - 1) / 4) >= _cur.grffile->param_end) {
			grfmsg(2, "CfgApply: Ignoring (param %d not set)", (param_num + (param_size - 1) / 4));
			break;
		}

		grfmsg(8, "CfgApply: Applying %u bytes from parameter 0x%02X at offset 0x%04X", param_size, param_num, offset);

		bool carry = false;
		for (i = 0; i < param_size && offset + i < num; i++) {
			uint32 value = GetParamVal(param_num + i / 4, nullptr);
			/* Reset carry flag for each iteration of the variable (only really
			 * matters if param_size is greater than 4) */
			if (i % 4 == 0) carry = false;

			if (add_value) {
				uint new_value = preload_sprite[offset + i] + GB(value, (i % 4) * 8, 8) + (carry ? 1 : 0);
				preload_sprite[offset + i] = GB(new_value, 0, 8);
				/* Check if the addition overflowed */
				carry = new_value >= 256;
			} else {
				preload_sprite[offset + i] = GB(value, (i % 4) * 8, 8);
			}
		}
	}
}

/**
 * Disable a static NewGRF when it is influencing another (non-static)
 * NewGRF as this could cause desyncs.
 *
 * We could just tell the NewGRF querying that the file doesn't exist,
 * but that might give unwanted results. Disabling the NewGRF gives the
 * best result as no NewGRF author can complain about that.
 * @param c The NewGRF to disable.
 */
static void DisableStaticNewGRFInfluencingNonStaticNewGRFs(GRFConfig *c)
{
	GRFError *error = DisableGrf(STR_NEWGRF_ERROR_STATIC_GRF_CAUSES_DESYNC, c);
	error->data = _cur.grfconfig->GetName();
}

/* Action 0x07
 * Action 0x09 */
static void SkipIf(ByteReader *buf)
{
	/* <07/09> <param-num> <param-size> <condition-type> <value> <num-sprites>
	 *
	 * B param-num
	 * B param-size
	 * B condition-type
	 * V value
	 * B num-sprites */
	uint32 cond_val = 0;
	uint32 mask = 0;
	bool result;

	uint8 param     = buf->ReadByte();
	uint8 paramsize = buf->ReadByte();
	uint8 condtype  = buf->ReadByte();

	if (condtype < 2) {
		/* Always 1 for bit tests, the given value should be ignored. */
		paramsize = 1;
	}

	switch (paramsize) {
		case 8: cond_val = buf->ReadDWord(); mask = buf->ReadDWord(); break;
		case 4: cond_val = buf->ReadDWord(); mask = 0xFFFFFFFF; break;
		case 2: cond_val = buf->ReadWord();  mask = 0x0000FFFF; break;
		case 1: cond_val = buf->ReadByte();  mask = 0x000000FF; break;
		default: break;
	}

	if (param < 0x80 && _cur.grffile->param_end <= param) {
		grfmsg(7, "SkipIf: Param %d undefined, skipping test", param);
		return;
	}

	grfmsg(7, "SkipIf: Test condtype %d, param 0x%02X, condval 0x%08X", condtype, param, cond_val);

	/* condtypes that do not use 'param' are always valid.
	 * condtypes that use 'param' are either not valid for param 0x88, or they are only valid for param 0x88.
	 */
	if (condtype >= 0x0B) {
		/* Tests that ignore 'param' */
		switch (condtype) {
			case 0x0B: result = GetCargoIDByLabel(BSWAP32(cond_val)) == CT_INVALID;
				break;
			case 0x0C: result = GetCargoIDByLabel(BSWAP32(cond_val)) != CT_INVALID;
				break;
			case 0x0D: result = GetRailTypeByLabel(BSWAP32(cond_val)) == INVALID_RAILTYPE;
				break;
			case 0x0E: result = GetRailTypeByLabel(BSWAP32(cond_val)) != INVALID_RAILTYPE;
				break;
			case 0x0F: {
				RoadType rt = GetRoadTypeByLabel(BSWAP32(cond_val));
				result = rt == INVALID_ROADTYPE || !RoadTypeIsRoad(rt);
				break;
			}
			case 0x10: {
				RoadType rt = GetRoadTypeByLabel(BSWAP32(cond_val));
				result = rt != INVALID_ROADTYPE && RoadTypeIsRoad(rt);
				break;
			}
			case 0x11: {
				RoadType rt = GetRoadTypeByLabel(BSWAP32(cond_val));
				result = rt == INVALID_ROADTYPE || !RoadTypeIsTram(rt);
				break;
			}
			case 0x12: {
				RoadType rt = GetRoadTypeByLabel(BSWAP32(cond_val));
				result = rt != INVALID_ROADTYPE && RoadTypeIsTram(rt);
				break;
			}
			default: grfmsg(1, "SkipIf: Unsupported condition type %02X. Ignoring", condtype); return;
		}
	} else if (param == 0x88) {
		/* GRF ID checks */

		GRFConfig *c = GetGRFConfig(cond_val, mask);

		if (c != nullptr && HasBit(c->flags, GCF_STATIC) && !HasBit(_cur.grfconfig->flags, GCF_STATIC) && _networking) {
			DisableStaticNewGRFInfluencingNonStaticNewGRFs(c);
			c = nullptr;
		}

		if (condtype != 10 && c == nullptr) {
			grfmsg(7, "SkipIf: GRFID 0x%08X unknown, skipping test", BSWAP32(cond_val));
			return;
		}

		switch (condtype) {
			/* Tests 0x06 to 0x0A are only for param 0x88, GRFID checks */
			case 0x06: // Is GRFID active?
				result = c->status == GCS_ACTIVATED;
				break;

			case 0x07: // Is GRFID non-active?
				result = c->status != GCS_ACTIVATED;
				break;

			case 0x08: // GRFID is not but will be active?
				result = c->status == GCS_INITIALISED;
				break;

			case 0x09: // GRFID is or will be active?
				result = c->status == GCS_ACTIVATED || c->status == GCS_INITIALISED;
				break;

			case 0x0A: // GRFID is not nor will be active
				/* This is the only condtype that doesn't get ignored if the GRFID is not found */
				result = c == nullptr || c->status == GCS_DISABLED || c->status == GCS_NOT_FOUND;
				break;

			default: grfmsg(1, "SkipIf: Unsupported GRF condition type %02X. Ignoring", condtype); return;
		}
	} else {
		/* Tests that use 'param' and are not GRF ID checks.  */
		uint32 param_val = GetParamVal(param, &cond_val); // cond_val is modified for param == 0x85
		switch (condtype) {
			case 0x00: result = !!(param_val & (1 << cond_val));
				break;
			case 0x01: result = !(param_val & (1 << cond_val));
				break;
			case 0x02: result = (param_val & mask) == cond_val;
				break;
			case 0x03: result = (param_val & mask) != cond_val;
				break;
			case 0x04: result = (param_val & mask) < cond_val;
				break;
			case 0x05: result = (param_val & mask) > cond_val;
				break;
			default: grfmsg(1, "SkipIf: Unsupported condition type %02X. Ignoring", condtype); return;
		}
	}

	if (!result) {
		grfmsg(2, "SkipIf: Not skipping sprites, test was false");
		return;
	}

	uint8 numsprites = buf->ReadByte();

	/* numsprites can be a GOTO label if it has been defined in the GRF
	 * file. The jump will always be the first matching label that follows
	 * the current nfo_line. If no matching label is found, the first matching
	 * label in the file is used. */
	GRFLabel *choice = nullptr;
	for (GRFLabel *label = _cur.grffile->label; label != nullptr; label = label->next) {
		if (label->label != numsprites) continue;

		/* Remember a goto before the current line */
		if (choice == nullptr) choice = label;
		/* If we find a label here, this is definitely good */
		if (label->nfo_line > _cur.nfo_line) {
			choice = label;
			break;
		}
	}

	if (choice != nullptr) {
		grfmsg(2, "SkipIf: Jumping to label 0x%0X at line %d, test was true", choice->label, choice->nfo_line);
		FioSeekTo(choice->pos, SEEK_SET);
		_cur.nfo_line = choice->nfo_line;
		return;
	}

	grfmsg(2, "SkipIf: Skipping %d sprites, test was true", numsprites);
	_cur.skip_sprites = numsprites;
	if (_cur.skip_sprites == 0) {
		/* Zero means there are no sprites to skip, so
		 * we use -1 to indicate that all further
		 * sprites should be skipped. */
		_cur.skip_sprites = -1;

		/* If an action 8 hasn't been encountered yet, disable the grf. */
		if (_cur.grfconfig->status != (_cur.stage < GLS_RESERVE ? GCS_INITIALISED : GCS_ACTIVATED)) {
			DisableGrf();
		}
	}
}


/* Action 0x08 (GLS_FILESCAN) */
static void ScanInfo(ByteReader *buf)
{
	uint8 grf_version = buf->ReadByte();
	uint32 grfid      = buf->ReadDWord();
	const char *name  = buf->ReadString();

	_cur.grfconfig->ident.grfid = grfid;

	if (grf_version < 2 || grf_version > 8) {
		SetBit(_cur.grfconfig->flags, GCF_INVALID);
		DEBUG(grf, 0, "%s: NewGRF \"%s\" (GRFID %08X) uses GRF version %d, which is incompatible with this version of OpenTTD.", _cur.grfconfig->filename, name, BSWAP32(grfid), grf_version);
	}

	/* GRF IDs starting with 0xFF are reserved for internal TTDPatch use */
	if (GB(grfid, 0, 8) == 0xFF) SetBit(_cur.grfconfig->flags, GCF_SYSTEM);

	AddGRFTextToList(_cur.grfconfig->name, 0x7F, grfid, false, name);

	if (buf->HasData()) {
		const char *info = buf->ReadString();
		AddGRFTextToList(_cur.grfconfig->info, 0x7F, grfid, true, info);
	}

	/* GLS_INFOSCAN only looks for the action 8, so we can skip the rest of the file */
	_cur.skip_sprites = -1;
}

/* Action 0x08 */
static void GRFInfo(ByteReader *buf)
{
	/* <08> <version> <grf-id> <name> <info>
	 *
	 * B version       newgrf version, currently 06
	 * 4*B grf-id      globally unique ID of this .grf file
	 * S name          name of this .grf set
	 * S info          string describing the set, and e.g. author and copyright */

	uint8 version    = buf->ReadByte();
	uint32 grfid     = buf->ReadDWord();
	const char *name = buf->ReadString();

	if (_cur.stage < GLS_RESERVE && _cur.grfconfig->status != GCS_UNKNOWN) {
		DisableGrf(STR_NEWGRF_ERROR_MULTIPLE_ACTION_8);
		return;
	}

	if (_cur.grffile->grfid != grfid) {
		DEBUG(grf, 0, "GRFInfo: GRFID %08X in FILESCAN stage does not match GRFID %08X in INIT/RESERVE/ACTIVATION stage", BSWAP32(_cur.grffile->grfid), BSWAP32(grfid));
		_cur.grffile->grfid = grfid;
	}

	_cur.grffile->grf_version = version;
	_cur.grfconfig->status = _cur.stage < GLS_RESERVE ? GCS_INITIALISED : GCS_ACTIVATED;

	/* Do swap the GRFID for displaying purposes since people expect that */
	DEBUG(grf, 1, "GRFInfo: Loaded GRFv%d set %08X - %s (palette: %s, version: %i)", version, BSWAP32(grfid), name, (_cur.grfconfig->palette & GRFP_USE_MASK) ? "Windows" : "DOS", _cur.grfconfig->version);
}

/* Action 0x0A */
static void SpriteReplace(ByteReader *buf)
{
	/* <0A> <num-sets> <set1> [<set2> ...]
	 * <set>: <num-sprites> <first-sprite>
	 *
	 * B num-sets      How many sets of sprites to replace.
	 * Each set:
	 * B num-sprites   How many sprites are in this set
	 * W first-sprite  First sprite number to replace */

	uint8 num_sets = buf->ReadByte();

	for (uint i = 0; i < num_sets; i++) {
		uint8 num_sprites = buf->ReadByte();
		uint16 first_sprite = buf->ReadWord();

		grfmsg(2, "SpriteReplace: [Set %d] Changing %d sprites, beginning with %d",
			i, num_sprites, first_sprite
		);

		for (uint j = 0; j < num_sprites; j++) {
			int load_index = first_sprite + j;
			_cur.nfo_line++;
			LoadNextSprite(load_index, _cur.file_index, _cur.nfo_line, _cur.grf_container_ver); // XXX

			/* Shore sprites now located at different addresses.
			 * So detect when the old ones get replaced. */
			if (IsInsideMM(load_index, SPR_ORIGINALSHORE_START, SPR_ORIGINALSHORE_END + 1)) {
				if (_loaded_newgrf_features.shore != SHORE_REPLACE_ACTION_5) _loaded_newgrf_features.shore = SHORE_REPLACE_ACTION_A;
			}
		}
	}
}

/* Action 0x0A (SKIP) */
static void SkipActA(ByteReader *buf)
{
	uint8 num_sets = buf->ReadByte();

	for (uint i = 0; i < num_sets; i++) {
		/* Skip the sprites this replaces */
		_cur.skip_sprites += buf->ReadByte();
		/* But ignore where they go */
		buf->ReadWord();
	}

	grfmsg(3, "SkipActA: Skipping %d sprites", _cur.skip_sprites);
}

/* Action 0x0B */
static void GRFLoadError(ByteReader *buf)
{
	/* <0B> <severity> <language-id> <message-id> [<message...> 00] [<data...>] 00 [<parnum>]
	 *
	 * B severity      00: notice, continue loading grf file
	 *                 01: warning, continue loading grf file
	 *                 02: error, but continue loading grf file, and attempt
	 *                     loading grf again when loading or starting next game
	 *                 03: error, abort loading and prevent loading again in
	 *                     the future (only when restarting the patch)
	 * B language-id   see action 4, use 1F for built-in error messages
	 * B message-id    message to show, see below
	 * S message       for custom messages (message-id FF), text of the message
	 *                 not present for built-in messages.
	 * V data          additional data for built-in (or custom) messages
	 * B parnum        parameter numbers to be shown in the message (maximum of 2) */

	static const StringID msgstr[] = {
		STR_NEWGRF_ERROR_VERSION_NUMBER,
		STR_NEWGRF_ERROR_DOS_OR_WINDOWS,
		STR_NEWGRF_ERROR_UNSET_SWITCH,
		STR_NEWGRF_ERROR_INVALID_PARAMETER,
		STR_NEWGRF_ERROR_LOAD_BEFORE,
		STR_NEWGRF_ERROR_LOAD_AFTER,
		STR_NEWGRF_ERROR_OTTD_VERSION_NUMBER,
	};

	static const StringID sevstr[] = {
		STR_NEWGRF_ERROR_MSG_INFO,
		STR_NEWGRF_ERROR_MSG_WARNING,
		STR_NEWGRF_ERROR_MSG_ERROR,
		STR_NEWGRF_ERROR_MSG_FATAL
	};

	byte severity   = buf->ReadByte();
	byte lang       = buf->ReadByte();
	byte message_id = buf->ReadByte();

	/* Skip the error if it isn't valid for the current language. */
	if (!CheckGrfLangID(lang, _cur.grffile->grf_version)) return;

	/* Skip the error until the activation stage unless bit 7 of the severity
	 * is set. */
	if (!HasBit(severity, 7) && _cur.stage == GLS_INIT) {
		grfmsg(7, "GRFLoadError: Skipping non-fatal GRFLoadError in stage %d", _cur.stage);
		return;
	}
	ClrBit(severity, 7);

	if (severity >= lengthof(sevstr)) {
		grfmsg(7, "GRFLoadError: Invalid severity id %d. Setting to 2 (non-fatal error).", severity);
		severity = 2;
	} else if (severity == 3) {
		/* This is a fatal error, so make sure the GRF is deactivated and no
		 * more of it gets loaded. */
		DisableGrf();

		/* Make sure we show fatal errors, instead of silly infos from before */
		delete _cur.grfconfig->error;
		_cur.grfconfig->error = nullptr;
	}

	if (message_id >= lengthof(msgstr) && message_id != 0xFF) {
		grfmsg(7, "GRFLoadError: Invalid message id.");
		return;
	}

	if (buf->Remaining() <= 1) {
		grfmsg(7, "GRFLoadError: No message data supplied.");
		return;
	}

	/* For now we can only show one message per newgrf file. */
	if (_cur.grfconfig->error != nullptr) return;

	GRFError *error = new GRFError(sevstr[severity]);

	if (message_id == 0xFF) {
		/* This is a custom error message. */
		if (buf->HasData()) {
			const char *message = buf->ReadString();

			error->custom_message = TranslateTTDPatchCodes(_cur.grffile->grfid, lang, true, message, SCC_RAW_STRING_POINTER);
		} else {
			grfmsg(7, "GRFLoadError: No custom message supplied.");
			error->custom_message.clear();
		}
	} else {
		error->message = msgstr[message_id];
	}

	if (buf->HasData()) {
		const char *data = buf->ReadString();

		error->data = TranslateTTDPatchCodes(_cur.grffile->grfid, lang, true, data);
	} else {
		grfmsg(7, "GRFLoadError: No message data supplied.");
		error->data.clear();
	}

	/* Only two parameter numbers can be used in the string. */
	for (uint i = 0; i < lengthof(error->param_value) && buf->HasData(); i++) {
		uint param_number = buf->ReadByte();
		error->param_value[i] = _cur.grffile->GetParam(param_number);
	}

	_cur.grfconfig->error = error;
}

/* Action 0x0C */
static void GRFComment(ByteReader *buf)
{
	/* <0C> [<ignored...>]
	 *
	 * V ignored       Anything following the 0C is ignored */

	if (!buf->HasData()) return;

	const char *text = buf->ReadString();
	grfmsg(2, "GRFComment: %s", text);
}

/* Action 0x0D (GLS_SAFETYSCAN) */
static void SafeParamSet(ByteReader *buf)
{
	uint8 target = buf->ReadByte();

	/* Writing GRF parameters and some bits of 'misc GRF features' are safe. */
	if (target < 0x80 || target == 0x9E) return;

	/* GRM could be unsafe, but as here it can only happen after other GRFs
	 * are loaded, it should be okay. If the GRF tried to use the slots it
	 * reserved, it would be marked unsafe anyway. GRM for (e.g. bridge)
	 * sprites  is considered safe. */

	SetBit(_cur.grfconfig->flags, GCF_UNSAFE);

	/* Skip remainder of GRF */
	_cur.skip_sprites = -1;
}


static uint32 GetPatchVariable(uint8 param)
{
	switch (param) {
		/* start year - 1920 */
		case 0x0B: return std::max(_settings_game.game_creation.starting_year, ORIGINAL_BASE_YEAR) - ORIGINAL_BASE_YEAR;

		/* freight trains weight factor */
		case 0x0E: return _settings_game.vehicle.freight_trains;

		/* empty wagon speed increase */
		case 0x0F: return 0;

		/* plane speed factor; our patch option is reversed from TTDPatch's,
		 * the following is good for 1x, 2x and 4x (most common?) and...
		 * well not really for 3x. */
		case 0x10:
			switch (_settings_game.vehicle.plane_speed) {
				default:
				case 4: return 1;
				case 3: return 2;
				case 2: return 2;
				case 1: return 4;
			}


		/* 2CC colourmap base sprite */
		case 0x11: return SPR_2CCMAP_BASE;

		/* map size: format = -MABXYSS
		 * M  : the type of map
		 *       bit 0 : set   : squared map. Bit 1 is now not relevant
		 *               clear : rectangle map. Bit 1 will indicate the bigger edge of the map
		 *       bit 1 : set   : Y is the bigger edge. Bit 0 is clear
		 *               clear : X is the bigger edge.
		 * A  : minimum edge(log2) of the map
		 * B  : maximum edge(log2) of the map
		 * XY : edges(log2) of each side of the map.
		 * SS : combination of both X and Y, thus giving the size(log2) of the map
		 */
		case 0x13: {
			byte map_bits = 0;
			byte log_X = MapLogX() - 6; // subtraction is required to make the minimal size (64) zero based
			byte log_Y = MapLogY() - 6;
			byte max_edge = std::max(log_X, log_Y);

			if (log_X == log_Y) { // we have a squared map, since both edges are identical
				SetBit(map_bits, 0);
			} else {
				if (max_edge == log_Y) SetBit(map_bits, 1); // edge Y been the biggest, mark it
			}

			return (map_bits << 24) | (std::min(log_X, log_Y) << 20) | (max_edge << 16) |
				(log_X << 12) | (log_Y << 8) | (log_X + log_Y);
		}

		/* The maximum height of the map. */
		case 0x14:
			return _settings_game.construction.max_heightlevel;

		/* Extra foundations base sprite */
		case 0x15:
			return SPR_SLOPES_BASE;

		/* Shore base sprite */
		case 0x16:
			return SPR_SHORE_BASE;

		default:
			grfmsg(2, "ParamSet: Unknown Patch variable 0x%02X.", param);
			return 0;
	}
}


static uint32 PerformGRM(uint32 *grm, uint16 num_ids, uint16 count, uint8 op, uint8 target, const char *type)
{
	uint start = 0;
	uint size  = 0;

	if (op == 6) {
		/* Return GRFID of set that reserved ID */
		return grm[_cur.grffile->GetParam(target)];
	}

	/* With an operation of 2 or 3, we want to reserve a specific block of IDs */
	if (op == 2 || op == 3) start = _cur.grffile->GetParam(target);

	for (uint i = start; i < num_ids; i++) {
		if (grm[i] == 0) {
			size++;
		} else {
			if (op == 2 || op == 3) break;
			start = i + 1;
			size = 0;
		}

		if (size == count) break;
	}

	if (size == count) {
		/* Got the slot... */
		if (op == 0 || op == 3) {
			grfmsg(2, "ParamSet: GRM: Reserving %d %s at %d", count, type, start);
			for (uint i = 0; i < count; i++) grm[start + i] = _cur.grffile->grfid;
		}
		return start;
	}

	/* Unable to allocate */
	if (op != 4 && op != 5) {
		/* Deactivate GRF */
		grfmsg(0, "ParamSet: GRM: Unable to allocate %d %s, deactivating", count, type);
		DisableGrf(STR_NEWGRF_ERROR_GRM_FAILED);
		return UINT_MAX;
	}

	grfmsg(1, "ParamSet: GRM: Unable to allocate %d %s", count, type);
	return UINT_MAX;
}


/** Action 0x0D: Set parameter */
static void ParamSet(ByteReader *buf)
{
	/* <0D> <target> <operation> <source1> <source2> [<data>]
	 *
	 * B target        parameter number where result is stored
	 * B operation     operation to perform, see below
	 * B source1       first source operand
	 * B source2       second source operand
	 * D data          data to use in the calculation, not necessary
	 *                 if both source1 and source2 refer to actual parameters
	 *
	 * Operations
	 * 00      Set parameter equal to source1
	 * 01      Addition, source1 + source2
	 * 02      Subtraction, source1 - source2
	 * 03      Unsigned multiplication, source1 * source2 (both unsigned)
	 * 04      Signed multiplication, source1 * source2 (both signed)
	 * 05      Unsigned bit shift, source1 by source2 (source2 taken to be a
	 *         signed quantity; left shift if positive and right shift if
	 *         negative, source1 is unsigned)
	 * 06      Signed bit shift, source1 by source2
	 *         (source2 like in 05, and source1 as well)
	 */

	uint8 target = buf->ReadByte();
	uint8 oper   = buf->ReadByte();
	uint32 src1  = buf->ReadByte();
	uint32 src2  = buf->ReadByte();

	uint32 data = 0;
	if (buf->Remaining() >= 4) data = buf->ReadDWord();

	/* You can add 80 to the operation to make it apply only if the target
	 * is not defined yet.  In this respect, a parameter is taken to be
	 * defined if any of the following applies:
	 * - it has been set to any value in the newgrf(w).cfg parameter list
	 * - it OR A PARAMETER WITH HIGHER NUMBER has been set to any value by
	 *   an earlier action D */
	if (HasBit(oper, 7)) {
		if (target < 0x80 && target < _cur.grffile->param_end) {
			grfmsg(7, "ParamSet: Param %u already defined, skipping", target);
			return;
		}

		oper = GB(oper, 0, 7);
	}

	if (src2 == 0xFE) {
		if (GB(data, 0, 8) == 0xFF) {
			if (data == 0x0000FFFF) {
				/* Patch variables */
				src1 = GetPatchVariable(src1);
			} else {
				/* GRF Resource Management */
				uint8  op      = src1;
				uint8  feature = GB(data, 8, 8);
				uint16 count   = GB(data, 16, 16);

				if (_cur.stage == GLS_RESERVE) {
					if (feature == 0x08) {
						/* General sprites */
						if (op == 0) {
							/* Check if the allocated sprites will fit below the original sprite limit */
							if (_cur.spriteid + count >= 16384) {
								grfmsg(0, "ParamSet: GRM: Unable to allocate %d sprites; try changing NewGRF order", count);
								DisableGrf(STR_NEWGRF_ERROR_GRM_FAILED);
								return;
							}

							/* Reserve space at the current sprite ID */
							grfmsg(4, "ParamSet: GRM: Allocated %d sprites at %d", count, _cur.spriteid);
							_grm_sprites[GRFLocation(_cur.grffile->grfid, _cur.nfo_line)] = _cur.spriteid;
							_cur.spriteid += count;
						}
					}
					/* Ignore GRM result during reservation */
					src1 = 0;
				} else if (_cur.stage == GLS_ACTIVATION) {
					switch (feature) {
						case 0x00: // Trains
						case 0x01: // Road Vehicles
						case 0x02: // Ships
						case 0x03: // Aircraft
							if (!_settings_game.vehicle.dynamic_engines) {
								src1 = PerformGRM(&_grm_engines[_engine_offsets[feature]], _engine_counts[feature], count, op, target, "vehicles");
								if (_cur.skip_sprites == -1) return;
							} else {
								/* GRM does not apply for dynamic engine allocation. */
								switch (op) {
									case 2:
									case 3:
										src1 = _cur.grffile->GetParam(target);
										break;

									default:
										src1 = 0;
										break;
								}
							}
							break;

						case 0x08: // General sprites
							switch (op) {
								case 0:
									/* Return space reserved during reservation stage */
									src1 = _grm_sprites[GRFLocation(_cur.grffile->grfid, _cur.nfo_line)];
									grfmsg(4, "ParamSet: GRM: Using pre-allocated sprites at %d", src1);
									break;

								case 1:
									src1 = _cur.spriteid;
									break;

								default:
									grfmsg(1, "ParamSet: GRM: Unsupported operation %d for general sprites", op);
									return;
							}
							break;

						case 0x0B: // Cargo
							/* There are two ranges: one for cargo IDs and one for cargo bitmasks */
							src1 = PerformGRM(_grm_cargoes, NUM_CARGO * 2, count, op, target, "cargoes");
							if (_cur.skip_sprites == -1) return;
							break;

						default: grfmsg(1, "ParamSet: GRM: Unsupported feature 0x%X", feature); return;
					}
				} else {
					/* Ignore GRM during initialization */
					src1 = 0;
				}
			}
		} else {
			/* Read another GRF File's parameter */
			const GRFFile *file = GetFileByGRFID(data);
			GRFConfig *c = GetGRFConfig(data);
			if (c != nullptr && HasBit(c->flags, GCF_STATIC) && !HasBit(_cur.grfconfig->flags, GCF_STATIC) && _networking) {
				/* Disable the read GRF if it is a static NewGRF. */
				DisableStaticNewGRFInfluencingNonStaticNewGRFs(c);
				src1 = 0;
			} else if (file == nullptr || c == nullptr || c->status == GCS_DISABLED) {
				src1 = 0;
			} else if (src1 == 0xFE) {
				src1 = c->version;
			} else {
				src1 = file->GetParam(src1);
			}
		}
	} else {
		/* The source1 and source2 operands refer to the grf parameter number
		 * like in action 6 and 7.  In addition, they can refer to the special
		 * variables available in action 7, or they can be FF to use the value
		 * of <data>.  If referring to parameters that are undefined, a value
		 * of 0 is used instead.  */
		src1 = (src1 == 0xFF) ? data : GetParamVal(src1, nullptr);
		src2 = (src2 == 0xFF) ? data : GetParamVal(src2, nullptr);
	}

	uint32 res;
	switch (oper) {
		case 0x00:
			res = src1;
			break;

		case 0x01:
			res = src1 + src2;
			break;

		case 0x02:
			res = src1 - src2;
			break;

		case 0x03:
			res = src1 * src2;
			break;

		case 0x04:
			res = (int32)src1 * (int32)src2;
			break;

		case 0x05:
			if ((int32)src2 < 0) {
				res = src1 >> -(int32)src2;
			} else {
				res = src1 << (src2 & 0x1F); // Same behaviour as in EvalAdjustT, mask 'value' to 5 bits, which should behave the same on all architectures.
			}
			break;

		case 0x06:
			if ((int32)src2 < 0) {
				res = (int32)src1 >> -(int32)src2;
			} else {
				res = (int32)src1 << (src2 & 0x1F); // Same behaviour as in EvalAdjustT, mask 'value' to 5 bits, which should behave the same on all architectures.
			}
			break;

		case 0x07: // Bitwise AND
			res = src1 & src2;
			break;

		case 0x08: // Bitwise OR
			res = src1 | src2;
			break;

		case 0x09: // Unsigned division
			if (src2 == 0) {
				res = src1;
			} else {
				res = src1 / src2;
			}
			break;

		case 0x0A: // Signed division
			if (src2 == 0) {
				res = src1;
			} else {
				res = (int32)src1 / (int32)src2;
			}
			break;

		case 0x0B: // Unsigned modulo
			if (src2 == 0) {
				res = src1;
			} else {
				res = src1 % src2;
			}
			break;

		case 0x0C: // Signed modulo
			if (src2 == 0) {
				res = src1;
			} else {
				res = (int32)src1 % (int32)src2;
			}
			break;

		default: grfmsg(0, "ParamSet: Unknown operation %d, skipping", oper); return;
	}

	switch (target) {
		case 0x8E: // Y-Offset for train sprites
			_cur.grffile->traininfo_vehicle_pitch = res;
			break;

		case 0x8F: { // Rail track type cost factors
			extern RailtypeInfo _railtypes[RAILTYPE_END];
			_railtypes[RAILTYPE_RAIL].cost_multiplier = GB(res, 0, 8);
			if (_settings_game.vehicle.disable_elrails) {
				_railtypes[RAILTYPE_ELECTRIC].cost_multiplier = GB(res, 0, 8);
				_railtypes[RAILTYPE_MONO].cost_multiplier = GB(res, 8, 8);
			} else {
				_railtypes[RAILTYPE_ELECTRIC].cost_multiplier = GB(res, 8, 8);
				_railtypes[RAILTYPE_MONO].cost_multiplier = GB(res, 16, 8);
			}
			_railtypes[RAILTYPE_MAGLEV].cost_multiplier = GB(res, 16, 8);
			break;
		}

		/* not implemented */
		case 0x93: // Tile refresh offset to left -- Intended to allow support for larger sprites, not necessary for OTTD
		case 0x94: // Tile refresh offset to right
		case 0x95: // Tile refresh offset upwards
		case 0x96: // Tile refresh offset downwards
		case 0x97: // Snow line height -- Better supported by feature 8 property 10h (snow line table) TODO: implement by filling the entire snow line table with the given value
		case 0x99: // Global ID offset -- Not necessary since IDs are remapped automatically
			grfmsg(7, "ParamSet: Skipping unimplemented target 0x%02X", target);
			break;

		case 0x9E: // Miscellaneous GRF features
			/* Set train list engine width */
			_cur.grffile->traininfo_vehicle_width = HasBit(res, GMB_TRAIN_WIDTH_32_PIXELS) ? VEHICLEINFO_FULL_VEHICLE_WIDTH : TRAININFO_DEFAULT_VEHICLE_WIDTH;
			/* Remove the local flags from the global flags */
			ClrBit(res, GMB_TRAIN_WIDTH_32_PIXELS);

			/* Only copy safe bits for static grfs */
			if (HasBit(_cur.grfconfig->flags, GCF_STATIC)) {
				uint32 safe_bits = 0;
				SetBit(safe_bits, GMB_SECOND_ROCKY_TILE_SET);

				_misc_grf_features = (_misc_grf_features & ~safe_bits) | (res & safe_bits);
			} else {
				_misc_grf_features = res;
			}
			break;

		case 0x9F: // locale-dependent settings
			grfmsg(7, "ParamSet: Skipping unimplemented target 0x%02X", target);
			break;

		default:
			if (target < 0x80) {
				_cur.grffile->param[target] = res;
				/* param is zeroed by default */
				if (target + 1U > _cur.grffile->param_end) _cur.grffile->param_end = target + 1;
			} else {
				grfmsg(7, "ParamSet: Skipping unknown target 0x%02X", target);
			}
			break;
	}
}

/* Action 0x0E (GLS_SAFETYSCAN) */
static void SafeGRFInhibit(ByteReader *buf)
{
	/* <0E> <num> <grfids...>
	 *
	 * B num           Number of GRFIDs that follow
	 * D grfids        GRFIDs of the files to deactivate */

	uint8 num = buf->ReadByte();

	for (uint i = 0; i < num; i++) {
		uint32 grfid = buf->ReadDWord();

		/* GRF is unsafe it if tries to deactivate other GRFs */
		if (grfid != _cur.grfconfig->ident.grfid) {
			SetBit(_cur.grfconfig->flags, GCF_UNSAFE);

			/* Skip remainder of GRF */
			_cur.skip_sprites = -1;

			return;
		}
	}
}

/* Action 0x0E */
static void GRFInhibit(ByteReader *buf)
{
	/* <0E> <num> <grfids...>
	 *
	 * B num           Number of GRFIDs that follow
	 * D grfids        GRFIDs of the files to deactivate */

	uint8 num = buf->ReadByte();

	for (uint i = 0; i < num; i++) {
		uint32 grfid = buf->ReadDWord();
		GRFConfig *file = GetGRFConfig(grfid);

		/* Unset activation flag */
		if (file != nullptr && file != _cur.grfconfig) {
			grfmsg(2, "GRFInhibit: Deactivating file '%s'", file->filename);
			GRFError *error = DisableGrf(STR_NEWGRF_ERROR_FORCEFULLY_DISABLED, file);
			error->data = stredup(_cur.grfconfig->GetName());
		}
	}
}

/** Action 0x0F - Define Town names */
static void FeatureTownName(ByteReader *buf)
{
	/* <0F> <id> <style-name> <num-parts> <parts>
	 *
	 * B id          ID of this definition in bottom 7 bits (final definition if bit 7 set)
	 * V style-name  Name of the style (only for final definition)
	 * B num-parts   Number of parts in this definition
	 * V parts       The parts */

	uint32 grfid = _cur.grffile->grfid;

	GRFTownName *townname = AddGRFTownName(grfid);

	byte id = buf->ReadByte();
	grfmsg(6, "FeatureTownName: definition 0x%02X", id & 0x7F);

	if (HasBit(id, 7)) {
		/* Final definition */
		ClrBit(id, 7);
		bool new_scheme = _cur.grffile->grf_version >= 7;

		byte lang = buf->ReadByte();

		byte nb_gen = townname->nb_gen;
		do {
			ClrBit(lang, 7);

			const char *name = buf->ReadString();

			std::string lang_name = TranslateTTDPatchCodes(grfid, lang, false, name);
			grfmsg(6, "FeatureTownName: lang 0x%X -> '%s'", lang, lang_name.c_str());

			townname->name[nb_gen] = AddGRFString(grfid, id, lang, new_scheme, false, name, STR_UNDEFINED);

			lang = buf->ReadByte();
		} while (lang != 0);
		townname->id[nb_gen] = id;
		townname->nb_gen++;
	}

	byte nb = buf->ReadByte();
	grfmsg(6, "FeatureTownName: %u parts", nb);

	townname->nbparts[id] = nb;
	townname->partlist[id] = CallocT<NamePartList>(nb);

	for (int i = 0; i < nb; i++) {
		byte nbtext =  buf->ReadByte();
		townname->partlist[id][i].bitstart  = buf->ReadByte();
		townname->partlist[id][i].bitcount  = buf->ReadByte();
		townname->partlist[id][i].maxprob   = 0;
		townname->partlist[id][i].partcount = nbtext;
		townname->partlist[id][i].parts     = CallocT<NamePart>(nbtext);
		grfmsg(6, "FeatureTownName: part %d contains %d texts and will use GB(seed, %d, %d)", i, nbtext, townname->partlist[id][i].bitstart, townname->partlist[id][i].bitcount);

		for (int j = 0; j < nbtext; j++) {
			byte prob = buf->ReadByte();

			if (HasBit(prob, 7)) {
				byte ref_id = buf->ReadByte();

				if (townname->nbparts[ref_id] == 0) {
					grfmsg(0, "FeatureTownName: definition 0x%02X doesn't exist, deactivating", ref_id);
					DelGRFTownName(grfid);
					DisableGrf(STR_NEWGRF_ERROR_INVALID_ID);
					return;
				}

				grfmsg(6, "FeatureTownName: part %d, text %d, uses intermediate definition 0x%02X (with probability %d)", i, j, ref_id, prob & 0x7F);
				townname->partlist[id][i].parts[j].data.id = ref_id;
			} else {
				const char *text = buf->ReadString();
				townname->partlist[id][i].parts[j].data.text = stredup(TranslateTTDPatchCodes(grfid, 0, false, text).c_str());
				grfmsg(6, "FeatureTownName: part %d, text %d, '%s' (with probability %d)", i, j, townname->partlist[id][i].parts[j].data.text, prob);
			}
			townname->partlist[id][i].parts[j].prob = prob;
			townname->partlist[id][i].maxprob += GB(prob, 0, 7);
		}
		grfmsg(6, "FeatureTownName: part %d, total probability %d", i, townname->partlist[id][i].maxprob);
	}
}

/** Action 0x10 - Define goto label */
static void DefineGotoLabel(ByteReader *buf)
{
	/* <10> <label> [<comment>]
	 *
	 * B label      The label to define
	 * V comment    Optional comment - ignored */

	byte nfo_label = buf->ReadByte();

	GRFLabel *label = MallocT<GRFLabel>(1);
	label->label    = nfo_label;
	label->nfo_line = _cur.nfo_line;
	label->pos      = FioGetPos();
	label->next     = nullptr;

	/* Set up a linked list of goto targets which we will search in an Action 0x7/0x9 */
	if (_cur.grffile->label == nullptr) {
		_cur.grffile->label = label;
	} else {
		/* Attach the label to the end of the list */
		GRFLabel *l;
		for (l = _cur.grffile->label; l->next != nullptr; l = l->next) {}
		l->next = label;
	}

	grfmsg(2, "DefineGotoLabel: GOTO target with label 0x%02X", label->label);
}

/**
 * Process a sound import from another GRF file.
 * @param sound Destination for sound.
 */
static void ImportGRFSound(SoundEntry *sound)
{
	const GRFFile *file;
	uint32 grfid = FioReadDword();
	SoundID sound_id = FioReadWord();

	file = GetFileByGRFID(grfid);
	if (file == nullptr || file->sound_offset == 0) {
		grfmsg(1, "ImportGRFSound: Source file not available");
		return;
	}

	if (sound_id >= file->num_sounds) {
		grfmsg(1, "ImportGRFSound: Sound effect %d is invalid", sound_id);
		return;
	}

	grfmsg(2, "ImportGRFSound: Copying sound %d (%d) from file %X", sound_id, file->sound_offset + sound_id, grfid);

	*sound = *GetSound(file->sound_offset + sound_id);

	/* Reset volume and priority, which TTDPatch doesn't copy */
	sound->volume   = 128;
	sound->priority = 0;
}

/**
 * Load a sound from a file.
 * @param offs File offset to read sound from.
 * @param sound Destination for sound.
 */
static void LoadGRFSound(size_t offs, SoundEntry *sound)
{
	/* Set default volume and priority */
	sound->volume = 0x80;
	sound->priority = 0;

	if (offs != SIZE_MAX) {
		/* Sound is present in the NewGRF. */
		sound->file_slot = _cur.file_index;
		sound->file_offset = offs;
		sound->grf_container_ver = _cur.grf_container_ver;
	}
}

/* Action 0x11 */
static void GRFSound(ByteReader *buf)
{
	/* <11> <num>
	 *
	 * W num      Number of sound files that follow */

	uint16 num = buf->ReadWord();
	if (num == 0) return;

	SoundEntry *sound;
	if (_cur.grffile->sound_offset == 0) {
		_cur.grffile->sound_offset = GetNumSounds();
		_cur.grffile->num_sounds = num;
		sound = AllocateSound(num);
	} else {
		sound = GetSound(_cur.grffile->sound_offset);
	}

	for (int i = 0; i < num; i++) {
		_cur.nfo_line++;

		/* Check whether the index is in range. This might happen if multiple action 11 are present.
		 * While this is invalid, we do not check for this. But we should prevent it from causing bigger trouble */
		bool invalid = i >= _cur.grffile->num_sounds;

		size_t offs = FioGetPos();

		uint32 len = _cur.grf_container_ver >= 2 ? FioReadDword() : FioReadWord();
		byte type = FioReadByte();

		if (_cur.grf_container_ver >= 2 && type == 0xFD) {
			/* Reference to sprite section. */
			if (invalid) {
				grfmsg(1, "GRFSound: Sound index out of range (multiple Action 11?)");
				FioSkipBytes(len);
			} else if (len != 4) {
				grfmsg(1, "GRFSound: Invalid sprite section import");
				FioSkipBytes(len);
			} else {
				uint32 id = FioReadDword();
				if (_cur.stage == GLS_INIT) LoadGRFSound(GetGRFSpriteOffset(id), sound + i);
			}
			continue;
		}

		if (type != 0xFF) {
			grfmsg(1, "GRFSound: Unexpected RealSprite found, skipping");
			FioSkipBytes(7);
			SkipSpriteData(type, len - 8);
			continue;
		}

		if (invalid) {
			grfmsg(1, "GRFSound: Sound index out of range (multiple Action 11?)");
			FioSkipBytes(len);
		}

		byte action = FioReadByte();
		switch (action) {
			case 0xFF:
				/* Allocate sound only in init stage. */
				if (_cur.stage == GLS_INIT) {
					if (_cur.grf_container_ver >= 2) {
						grfmsg(1, "GRFSound: Inline sounds are not supported for container version >= 2");
					} else {
						LoadGRFSound(offs, sound + i);
					}
				}
				FioSkipBytes(len - 1); // already read <action>
				break;

			case 0xFE:
				if (_cur.stage == GLS_ACTIVATION) {
					/* XXX 'Action 0xFE' isn't really specified. It is only mentioned for
					 * importing sounds, so this is probably all wrong... */
					if (FioReadByte() != 0) grfmsg(1, "GRFSound: Import type mismatch");
					ImportGRFSound(sound + i);
				} else {
					FioSkipBytes(len - 1); // already read <action>
				}
				break;

			default:
				grfmsg(1, "GRFSound: Unexpected Action %x found, skipping", action);
				FioSkipBytes(len - 1); // already read <action>
				break;
		}
	}
}

/* Action 0x11 (SKIP) */
static void SkipAct11(ByteReader *buf)
{
	/* <11> <num>
	 *
	 * W num      Number of sound files that follow */

	_cur.skip_sprites = buf->ReadWord();

	grfmsg(3, "SkipAct11: Skipping %d sprites", _cur.skip_sprites);
}

/** Action 0x12 */
static void LoadFontGlyph(ByteReader *buf)
{
	/* <12> <num_def> <font_size> <num_char> <base_char>
	 *
	 * B num_def      Number of definitions
	 * B font_size    Size of font (0 = normal, 1 = small, 2 = large, 3 = mono)
	 * B num_char     Number of consecutive glyphs
	 * W base_char    First character index */

	uint8 num_def = buf->ReadByte();

	for (uint i = 0; i < num_def; i++) {
		FontSize size    = (FontSize)buf->ReadByte();
		uint8  num_char  = buf->ReadByte();
		uint16 base_char = buf->ReadWord();

		if (size >= FS_END) {
			grfmsg(1, "LoadFontGlyph: Size %u is not supported, ignoring", size);
		}

		grfmsg(7, "LoadFontGlyph: Loading %u glyph(s) at 0x%04X for size %u", num_char, base_char, size);

		for (uint c = 0; c < num_char; c++) {
			if (size < FS_END) SetUnicodeGlyph(size, base_char + c, _cur.spriteid);
			_cur.nfo_line++;
			LoadNextSprite(_cur.spriteid++, _cur.file_index, _cur.nfo_line, _cur.grf_container_ver);
		}
	}
}

/** Action 0x12 (SKIP) */
static void SkipAct12(ByteReader *buf)
{
	/* <12> <num_def> <font_size> <num_char> <base_char>
	 *
	 * B num_def      Number of definitions
	 * B font_size    Size of font (0 = normal, 1 = small, 2 = large)
	 * B num_char     Number of consecutive glyphs
	 * W base_char    First character index */

	uint8 num_def = buf->ReadByte();

	for (uint i = 0; i < num_def; i++) {
		/* Ignore 'size' byte */
		buf->ReadByte();

		/* Sum up number of characters */
		_cur.skip_sprites += buf->ReadByte();

		/* Ignore 'base_char' word */
		buf->ReadWord();
	}

	grfmsg(3, "SkipAct12: Skipping %d sprites", _cur.skip_sprites);
}

/** Action 0x13 */
static void TranslateGRFStrings(ByteReader *buf)
{
	/* <13> <grfid> <num-ent> <offset> <text...>
	 *
	 * 4*B grfid     The GRFID of the file whose texts are to be translated
	 * B   num-ent   Number of strings
	 * W   offset    First text ID
	 * S   text...   Zero-terminated strings */

	uint32 grfid = buf->ReadDWord();
	const GRFConfig *c = GetGRFConfig(grfid);
	if (c == nullptr || (c->status != GCS_INITIALISED && c->status != GCS_ACTIVATED)) {
		grfmsg(7, "TranslateGRFStrings: GRFID 0x%08x unknown, skipping action 13", BSWAP32(grfid));
		return;
	}

	if (c->status == GCS_INITIALISED) {
		/* If the file is not active but will be activated later, give an error
		 * and disable this file. */
		GRFError *error = DisableGrf(STR_NEWGRF_ERROR_LOAD_AFTER);

		char tmp[256];
		GetString(tmp, STR_NEWGRF_ERROR_AFTER_TRANSLATED_FILE, lastof(tmp));
		error->data = tmp;

		return;
	}

	/* Since no language id is supplied for with version 7 and lower NewGRFs, this string has
	 * to be added as a generic string, thus the language id of 0x7F. For this to work
	 * new_scheme has to be true as well, which will also be implicitly the case for version 8
	 * and higher. A language id of 0x7F will be overridden by a non-generic id, so this will
	 * not change anything if a string has been provided specifically for this language. */
	byte language = _cur.grffile->grf_version >= 8 ? buf->ReadByte() : 0x7F;
	byte num_strings = buf->ReadByte();
	uint16 first_id  = buf->ReadWord();

	if (!((first_id >= 0xD000 && first_id + num_strings <= 0xD400) || (first_id >= 0xD800 && first_id + num_strings <= 0xE000))) {
		grfmsg(7, "TranslateGRFStrings: Attempting to set out-of-range string IDs in action 13 (first: 0x%4X, number: 0x%2X)", first_id, num_strings);
		return;
	}

	for (uint i = 0; i < num_strings && buf->HasData(); i++) {
		const char *string = buf->ReadString();

		if (StrEmpty(string)) {
			grfmsg(7, "TranslateGRFString: Ignoring empty string.");
			continue;
		}

		AddGRFString(grfid, first_id + i, language, true, true, string, STR_UNDEFINED);
	}
}

/** Callback function for 'INFO'->'NAME' to add a translation to the newgrf name. */
static bool ChangeGRFName(byte langid, const char *str)
{
	AddGRFTextToList(_cur.grfconfig->name, langid, _cur.grfconfig->ident.grfid, false, str);
	return true;
}

/** Callback function for 'INFO'->'DESC' to add a translation to the newgrf description. */
static bool ChangeGRFDescription(byte langid, const char *str)
{
	AddGRFTextToList(_cur.grfconfig->info, langid, _cur.grfconfig->ident.grfid, true, str);
	return true;
}

/** Callback function for 'INFO'->'URL_' to set the newgrf url. */
static bool ChangeGRFURL(byte langid, const char *str)
{
	AddGRFTextToList(_cur.grfconfig->url, langid, _cur.grfconfig->ident.grfid, false, str);
	return true;
}

/** Callback function for 'INFO'->'NPAR' to set the number of valid parameters. */
static bool ChangeGRFNumUsedParams(size_t len, ByteReader *buf)
{
	if (len != 1) {
		grfmsg(2, "StaticGRFInfo: expected only 1 byte for 'INFO'->'NPAR' but got " PRINTF_SIZE ", ignoring this field", len);
		buf->Skip(len);
	} else {
		_cur.grfconfig->num_valid_params = std::min<byte>(buf->ReadByte(), lengthof(_cur.grfconfig->param));
	}
	return true;
}

/** Callback function for 'INFO'->'PALS' to set the number of valid parameters. */
static bool ChangeGRFPalette(size_t len, ByteReader *buf)
{
	if (len != 1) {
		grfmsg(2, "StaticGRFInfo: expected only 1 byte for 'INFO'->'PALS' but got " PRINTF_SIZE ", ignoring this field", len);
		buf->Skip(len);
	} else {
		char data = buf->ReadByte();
		GRFPalette pal = GRFP_GRF_UNSET;
		switch (data) {
			case '*':
			case 'A': pal = GRFP_GRF_ANY;     break;
			case 'W': pal = GRFP_GRF_WINDOWS; break;
			case 'D': pal = GRFP_GRF_DOS;     break;
			default:
				grfmsg(2, "StaticGRFInfo: unexpected value '%02x' for 'INFO'->'PALS', ignoring this field", data);
				break;
		}
		if (pal != GRFP_GRF_UNSET) {
			_cur.grfconfig->palette &= ~GRFP_GRF_MASK;
			_cur.grfconfig->palette |= pal;
		}
	}
	return true;
}

/** Callback function for 'INFO'->'BLTR' to set the blitter info. */
static bool ChangeGRFBlitter(size_t len, ByteReader *buf)
{
	if (len != 1) {
		grfmsg(2, "StaticGRFInfo: expected only 1 byte for 'INFO'->'BLTR' but got " PRINTF_SIZE ", ignoring this field", len);
		buf->Skip(len);
	} else {
		char data = buf->ReadByte();
		GRFPalette pal = GRFP_BLT_UNSET;
		switch (data) {
			case '8': pal = GRFP_BLT_UNSET; break;
			case '3': pal = GRFP_BLT_32BPP;  break;
			default:
				grfmsg(2, "StaticGRFInfo: unexpected value '%02x' for 'INFO'->'BLTR', ignoring this field", data);
				return true;
		}
		_cur.grfconfig->palette &= ~GRFP_BLT_MASK;
		_cur.grfconfig->palette |= pal;
	}
	return true;
}

/** Callback function for 'INFO'->'VRSN' to the version of the NewGRF. */
static bool ChangeGRFVersion(size_t len, ByteReader *buf)
{
	if (len != 4) {
		grfmsg(2, "StaticGRFInfo: expected 4 bytes for 'INFO'->'VRSN' but got " PRINTF_SIZE ", ignoring this field", len);
		buf->Skip(len);
	} else {
		/* Set min_loadable_version as well (default to minimal compatibility) */
		_cur.grfconfig->version = _cur.grfconfig->min_loadable_version = buf->ReadDWord();
	}
	return true;
}

/** Callback function for 'INFO'->'MINV' to the minimum compatible version of the NewGRF. */
static bool ChangeGRFMinVersion(size_t len, ByteReader *buf)
{
	if (len != 4) {
		grfmsg(2, "StaticGRFInfo: expected 4 bytes for 'INFO'->'MINV' but got " PRINTF_SIZE ", ignoring this field", len);
		buf->Skip(len);
	} else {
		_cur.grfconfig->min_loadable_version = buf->ReadDWord();
		if (_cur.grfconfig->version == 0) {
			grfmsg(2, "StaticGRFInfo: 'MINV' defined before 'VRSN' or 'VRSN' set to 0, ignoring this field");
			_cur.grfconfig->min_loadable_version = 0;
		}
		if (_cur.grfconfig->version < _cur.grfconfig->min_loadable_version) {
			grfmsg(2, "StaticGRFInfo: 'MINV' defined as %d, limiting it to 'VRSN'", _cur.grfconfig->min_loadable_version);
			_cur.grfconfig->min_loadable_version = _cur.grfconfig->version;
		}
	}
	return true;
}

static GRFParameterInfo *_cur_parameter; ///< The parameter which info is currently changed by the newgrf.

/** Callback function for 'INFO'->'PARAM'->param_num->'NAME' to set the name of a parameter. */
static bool ChangeGRFParamName(byte langid, const char *str)
{
	AddGRFTextToList(_cur_parameter->name, langid, _cur.grfconfig->ident.grfid, false, str);
	return true;
}

/** Callback function for 'INFO'->'PARAM'->param_num->'DESC' to set the description of a parameter. */
static bool ChangeGRFParamDescription(byte langid, const char *str)
{
	AddGRFTextToList(_cur_parameter->desc, langid, _cur.grfconfig->ident.grfid, true, str);
	return true;
}

/** Callback function for 'INFO'->'PARAM'->param_num->'TYPE' to set the typeof a parameter. */
static bool ChangeGRFParamType(size_t len, ByteReader *buf)
{
	if (len != 1) {
		grfmsg(2, "StaticGRFInfo: expected 1 byte for 'INFO'->'PARA'->'TYPE' but got " PRINTF_SIZE ", ignoring this field", len);
		buf->Skip(len);
	} else {
		GRFParameterType type = (GRFParameterType)buf->ReadByte();
		if (type < PTYPE_END) {
			_cur_parameter->type = type;
		} else {
			grfmsg(3, "StaticGRFInfo: unknown parameter type %d, ignoring this field", type);
		}
	}
	return true;
}

/** Callback function for 'INFO'->'PARAM'->param_num->'LIMI' to set the min/max value of a parameter. */
static bool ChangeGRFParamLimits(size_t len, ByteReader *buf)
{
	if (_cur_parameter->type != PTYPE_UINT_ENUM) {
		grfmsg(2, "StaticGRFInfo: 'INFO'->'PARA'->'LIMI' is only valid for parameters with type uint/enum, ignoring this field");
		buf->Skip(len);
	} else if (len != 8) {
		grfmsg(2, "StaticGRFInfo: expected 8 bytes for 'INFO'->'PARA'->'LIMI' but got " PRINTF_SIZE ", ignoring this field", len);
		buf->Skip(len);
	} else {
		uint32 min_value = buf->ReadDWord();
		uint32 max_value = buf->ReadDWord();
		if (min_value <= max_value) {
			_cur_parameter->min_value = min_value;
			_cur_parameter->max_value = max_value;
		} else {
			grfmsg(2, "StaticGRFInfo: 'INFO'->'PARA'->'LIMI' values are incoherent, ignoring this field");
		}
	}
	return true;
}

/** Callback function for 'INFO'->'PARAM'->param_num->'MASK' to set the parameter and bits to use. */
static bool ChangeGRFParamMask(size_t len, ByteReader *buf)
{
	if (len < 1 || len > 3) {
		grfmsg(2, "StaticGRFInfo: expected 1 to 3 bytes for 'INFO'->'PARA'->'MASK' but got " PRINTF_SIZE ", ignoring this field", len);
		buf->Skip(len);
	} else {
		byte param_nr = buf->ReadByte();
		if (param_nr >= lengthof(_cur.grfconfig->param)) {
			grfmsg(2, "StaticGRFInfo: invalid parameter number in 'INFO'->'PARA'->'MASK', param %d, ignoring this field", param_nr);
			buf->Skip(len - 1);
		} else {
			_cur_parameter->param_nr = param_nr;
			if (len >= 2) _cur_parameter->first_bit = std::min<byte>(buf->ReadByte(), 31);
			if (len >= 3) _cur_parameter->num_bit = std::min<byte>(buf->ReadByte(), 32 - _cur_parameter->first_bit);
		}
	}

	return true;
}

/** Callback function for 'INFO'->'PARAM'->param_num->'DFLT' to set the default value. */
static bool ChangeGRFParamDefault(size_t len, ByteReader *buf)
{
	if (len != 4) {
		grfmsg(2, "StaticGRFInfo: expected 4 bytes for 'INFO'->'PARA'->'DEFA' but got " PRINTF_SIZE ", ignoring this field", len);
		buf->Skip(len);
	} else {
		_cur_parameter->def_value = buf->ReadDWord();
	}
	_cur.grfconfig->has_param_defaults = true;
	return true;
}

typedef bool (*DataHandler)(size_t, ByteReader *);  ///< Type of callback function for binary nodes
typedef bool (*TextHandler)(byte, const char *str); ///< Type of callback function for text nodes
typedef bool (*BranchHandler)(ByteReader *);        ///< Type of callback function for branch nodes

/**
 * Data structure to store the allowed id/type combinations for action 14. The
 * data can be represented as a tree with 3 types of nodes:
 * 1. Branch nodes (identified by 'C' for choice).
 * 2. Binary leaf nodes (identified by 'B').
 * 3. Text leaf nodes (identified by 'T').
 */
struct AllowedSubtags {
	/** Create empty subtags object used to identify the end of a list. */
	AllowedSubtags() :
		id(0),
		type(0)
	{}

	/**
	 * Create a binary leaf node.
	 * @param id The id for this node.
	 * @param handler The callback function to call.
	 */
	AllowedSubtags(uint32 id, DataHandler handler) :
		id(id),
		type('B')
	{
		this->handler.data = handler;
	}

	/**
	 * Create a text leaf node.
	 * @param id The id for this node.
	 * @param handler The callback function to call.
	 */
	AllowedSubtags(uint32 id, TextHandler handler) :
		id(id),
		type('T')
	{
		this->handler.text = handler;
	}

	/**
	 * Create a branch node with a callback handler
	 * @param id The id for this node.
	 * @param handler The callback function to call.
	 */
	AllowedSubtags(uint32 id, BranchHandler handler) :
		id(id),
		type('C')
	{
		this->handler.call_handler = true;
		this->handler.u.branch = handler;
	}

	/**
	 * Create a branch node with a list of sub-nodes.
	 * @param id The id for this node.
	 * @param subtags Array with all valid subtags.
	 */
	AllowedSubtags(uint32 id, AllowedSubtags *subtags) :
		id(id),
		type('C')
	{
		this->handler.call_handler = false;
		this->handler.u.subtags = subtags;
	}

	uint32 id; ///< The identifier for this node
	byte type; ///< The type of the node, must be one of 'C', 'B' or 'T'.
	union {
		DataHandler data; ///< Callback function for a binary node, only valid if type == 'B'.
		TextHandler text; ///< Callback function for a text node, only valid if type == 'T'.
		struct {
			union {
				BranchHandler branch;    ///< Callback function for a branch node, only valid if type == 'C' && call_handler.
				AllowedSubtags *subtags; ///< Pointer to a list of subtags, only valid if type == 'C' && !call_handler.
			} u;
			bool call_handler; ///< True if there is a callback function for this node, false if there is a list of subnodes.
		};
	} handler;
};

static bool SkipUnknownInfo(ByteReader *buf, byte type);
static bool HandleNodes(ByteReader *buf, AllowedSubtags *tags);

/**
 * Callback function for 'INFO'->'PARA'->param_num->'VALU' to set the names
 * of some parameter values (type uint/enum) or the names of some bits
 * (type bitmask). In both cases the format is the same:
 * Each subnode should be a text node with the value/bit number as id.
 */
static bool ChangeGRFParamValueNames(ByteReader *buf)
{
	byte type = buf->ReadByte();
	while (type != 0) {
		uint32 id = buf->ReadDWord();
		if (type != 'T' || id > _cur_parameter->max_value) {
			grfmsg(2, "StaticGRFInfo: all child nodes of 'INFO'->'PARA'->param_num->'VALU' should have type 't' and the value/bit number as id");
			if (!SkipUnknownInfo(buf, type)) return false;
			type = buf->ReadByte();
			continue;
		}

		byte langid = buf->ReadByte();
		const char *name_string = buf->ReadString();

		std::pair<uint32, GRFTextList> *val_name = _cur_parameter->value_names.Find(id);
		if (val_name != _cur_parameter->value_names.End()) {
			AddGRFTextToList(val_name->second, langid, _cur.grfconfig->ident.grfid, false, name_string);
		} else {
			GRFTextList list;
			AddGRFTextToList(list, langid, _cur.grfconfig->ident.grfid, false, name_string);
			_cur_parameter->value_names.Insert(id, list);
		}

		type = buf->ReadByte();
	}
	return true;
}

/** Action14 parameter tags */
AllowedSubtags _tags_parameters[] = {
	AllowedSubtags('NAME', ChangeGRFParamName),
	AllowedSubtags('DESC', ChangeGRFParamDescription),
	AllowedSubtags('TYPE', ChangeGRFParamType),
	AllowedSubtags('LIMI', ChangeGRFParamLimits),
	AllowedSubtags('MASK', ChangeGRFParamMask),
	AllowedSubtags('VALU', ChangeGRFParamValueNames),
	AllowedSubtags('DFLT', ChangeGRFParamDefault),
	AllowedSubtags()
};

/**
 * Callback function for 'INFO'->'PARA' to set extra information about the
 * parameters. Each subnode of 'INFO'->'PARA' should be a branch node with
 * the parameter number as id. The first parameter has id 0. The maximum
 * parameter that can be changed is set by 'INFO'->'NPAR' which defaults to 80.
 */
static bool HandleParameterInfo(ByteReader *buf)
{
	byte type = buf->ReadByte();
	while (type != 0) {
		uint32 id = buf->ReadDWord();
		if (type != 'C' || id >= _cur.grfconfig->num_valid_params) {
			grfmsg(2, "StaticGRFInfo: all child nodes of 'INFO'->'PARA' should have type 'C' and their parameter number as id");
			if (!SkipUnknownInfo(buf, type)) return false;
			type = buf->ReadByte();
			continue;
		}

		if (id >= _cur.grfconfig->param_info.size()) {
			_cur.grfconfig->param_info.resize(id + 1);
		}
		if (_cur.grfconfig->param_info[id] == nullptr) {
			_cur.grfconfig->param_info[id] = new GRFParameterInfo(id);
		}
		_cur_parameter = _cur.grfconfig->param_info[id];
		/* Read all parameter-data and process each node. */
		if (!HandleNodes(buf, _tags_parameters)) return false;
		type = buf->ReadByte();
	}
	return true;
}

/** Action14 tags for the INFO node */
AllowedSubtags _tags_info[] = {
	AllowedSubtags('NAME', ChangeGRFName),
	AllowedSubtags('DESC', ChangeGRFDescription),
	AllowedSubtags('URL_', ChangeGRFURL),
	AllowedSubtags('NPAR', ChangeGRFNumUsedParams),
	AllowedSubtags('PALS', ChangeGRFPalette),
	AllowedSubtags('BLTR', ChangeGRFBlitter),
	AllowedSubtags('VRSN', ChangeGRFVersion),
	AllowedSubtags('MINV', ChangeGRFMinVersion),
	AllowedSubtags('PARA', HandleParameterInfo),
	AllowedSubtags()
};

/** Action14 root tags */
AllowedSubtags _tags_root[] = {
	AllowedSubtags('INFO', _tags_info),
	AllowedSubtags()
};


/**
 * Try to skip the current node and all subnodes (if it's a branch node).
 * @param buf Buffer.
 * @param type The node type to skip.
 * @return True if we could skip the node, false if an error occurred.
 */
static bool SkipUnknownInfo(ByteReader *buf, byte type)
{
	/* type and id are already read */
	switch (type) {
		case 'C': {
			byte new_type = buf->ReadByte();
			while (new_type != 0) {
				buf->ReadDWord(); // skip the id
				if (!SkipUnknownInfo(buf, new_type)) return false;
				new_type = buf->ReadByte();
			}
			break;
		}

		case 'T':
			buf->ReadByte(); // lang
			buf->ReadString(); // actual text
			break;

		case 'B': {
			uint16 size = buf->ReadWord();
			buf->Skip(size);
			break;
		}

		default:
			return false;
	}

	return true;
}

/**
 * Handle the nodes of an Action14
 * @param type Type of node.
 * @param id ID.
 * @param buf Buffer.
 * @param subtags Allowed subtags.
 * @return Whether all tags could be handled.
 */
static bool HandleNode(byte type, uint32 id, ByteReader *buf, AllowedSubtags subtags[])
{
	uint i = 0;
	AllowedSubtags *tag;
	while ((tag = &subtags[i++])->type != 0) {
		if (tag->id != BSWAP32(id) || tag->type != type) continue;
		switch (type) {
			default: NOT_REACHED();

			case 'T': {
				byte langid = buf->ReadByte();
				return tag->handler.text(langid, buf->ReadString());
			}

			case 'B': {
				size_t len = buf->ReadWord();
				if (buf->Remaining() < len) return false;
				return tag->handler.data(len, buf);
			}

			case 'C': {
				if (tag->handler.call_handler) {
					return tag->handler.u.branch(buf);
				}
				return HandleNodes(buf, tag->handler.u.subtags);
			}
		}
	}
	grfmsg(2, "StaticGRFInfo: unknown type/id combination found, type=%c, id=%x", type, id);
	return SkipUnknownInfo(buf, type);
}

/**
 * Handle the contents of a 'C' choice of an Action14
 * @param buf Buffer.
 * @param subtags List of subtags.
 * @return Whether the nodes could all be handled.
 */
static bool HandleNodes(ByteReader *buf, AllowedSubtags subtags[])
{
	byte type = buf->ReadByte();
	while (type != 0) {
		uint32 id = buf->ReadDWord();
		if (!HandleNode(type, id, buf, subtags)) return false;
		type = buf->ReadByte();
	}
	return true;
}

/**
 * Handle Action 0x14
 * @param buf Buffer.
 */
static void StaticGRFInfo(ByteReader *buf)
{
	/* <14> <type> <id> <text/data...> */
	HandleNodes(buf, _tags_root);
}

/**
 * Set the current NewGRF as unsafe for static use
 * @param buf Unused.
 * @note Used during safety scan on unsafe actions.
 */
static void GRFUnsafe(ByteReader *buf)
{
	SetBit(_cur.grfconfig->flags, GCF_UNSAFE);

	/* Skip remainder of GRF */
	_cur.skip_sprites = -1;
}


/** Initialize the TTDPatch flags */
static void InitializeGRFSpecial()
{
	_ttdpatch_flags[0] = ((_settings_game.station.never_expire_airports ? 1 : 0) << 0x0C)  // keepsmallairport
	                   |                                                      (1 << 0x0D)  // newairports
	                   |                                                      (1 << 0x0E)  // largestations
	                   | ((_settings_game.construction.max_bridge_length > 16 ? 1 : 0) << 0x0F)  // longbridges
	                   |                                                      (0 << 0x10)  // loadtime
	                   |                                                      (1 << 0x12)  // presignals
	                   |                                                      (1 << 0x13)  // extpresignals
	                   | ((_settings_game.vehicle.never_expire_vehicles ? 1 : 0) << 0x16)  // enginespersist
	                   |                                                      (1 << 0x1B)  // multihead
	                   |                                                      (1 << 0x1D)  // lowmemory
	                   |                                                      (1 << 0x1E); // generalfixes

	_ttdpatch_flags[1] =   ((_settings_game.economy.station_noise_level ? 1 : 0) << 0x07)  // moreairports - based on units of noise
	                   |                                                      (1 << 0x08)  // mammothtrains
	                   |                                                      (1 << 0x09)  // trainrefit
	                   |                                                      (0 << 0x0B)  // subsidiaries
	                   |         ((_settings_game.order.gradual_loading ? 1 : 0) << 0x0C)  // gradualloading
	                   |                                                      (1 << 0x12)  // unifiedmaglevmode - set bit 0 mode. Not revelant to OTTD
	                   |                                                      (1 << 0x13)  // unifiedmaglevmode - set bit 1 mode
	                   |                                                      (1 << 0x14)  // bridgespeedlimits
	                   |                                                      (1 << 0x16)  // eternalgame
	                   |                                                      (1 << 0x17)  // newtrains
	                   |                                                      (1 << 0x18)  // newrvs
	                   |                                                      (1 << 0x19)  // newships
	                   |                                                      (1 << 0x1A)  // newplanes
	                   | ((_settings_game.construction.train_signal_side == 1 ? 1 : 0) << 0x1B)  // signalsontrafficside
	                   |       ((_settings_game.vehicle.disable_elrails ? 0 : 1) << 0x1C); // electrifiedrailway

	_ttdpatch_flags[2] =                                                      (1 << 0x01)  // loadallgraphics - obsolote
	                   |                                                      (1 << 0x03)  // semaphores
	                   |                                                      (1 << 0x0A)  // newobjects
	                   |                                                      (0 << 0x0B)  // enhancedgui
	                   |                                                      (0 << 0x0C)  // newagerating
	                   |  ((_settings_game.construction.build_on_slopes ? 1 : 0) << 0x0D)  // buildonslopes
	                   |                                                      (1 << 0x0E)  // fullloadany
	                   |                                                      (1 << 0x0F)  // planespeed
	                   |                                                      (0 << 0x10)  // moreindustriesperclimate - obsolete
	                   |                                                      (0 << 0x11)  // moretoylandfeatures
	                   |                                                      (1 << 0x12)  // newstations
	                   |                                                      (1 << 0x13)  // tracktypecostdiff
	                   |                                                      (1 << 0x14)  // manualconvert
	                   |  ((_settings_game.construction.build_on_slopes ? 1 : 0) << 0x15)  // buildoncoasts
	                   |                                                      (1 << 0x16)  // canals
	                   |                                                      (1 << 0x17)  // newstartyear
	                   |    ((_settings_game.vehicle.freight_trains > 1 ? 1 : 0) << 0x18)  // freighttrains
	                   |                                                      (1 << 0x19)  // newhouses
	                   |                                                      (1 << 0x1A)  // newbridges
	                   |                                                      (1 << 0x1B)  // newtownnames
	                   |                                                      (1 << 0x1C)  // moreanimation
	                   |    ((_settings_game.vehicle.wagon_speed_limits ? 1 : 0) << 0x1D)  // wagonspeedlimits
	                   |                                                      (1 << 0x1E)  // newshistory
	                   |                                                      (0 << 0x1F); // custombridgeheads

	_ttdpatch_flags[3] =                                                      (0 << 0x00)  // newcargodistribution
	                   |                                                      (1 << 0x01)  // windowsnap
	                   | ((_settings_game.economy.allow_town_roads || _generating_world ? 0 : 1) << 0x02)  // townbuildnoroad
	                   |                                                      (1 << 0x03)  // pathbasedsignalling
	                   |                                                      (0 << 0x04)  // aichoosechance
	                   |                                                      (1 << 0x05)  // resolutionwidth
	                   |                                                      (1 << 0x06)  // resolutionheight
	                   |                                                      (1 << 0x07)  // newindustries
	                   |           ((_settings_game.order.improved_load ? 1 : 0) << 0x08)  // fifoloading
	                   |                                                      (0 << 0x09)  // townroadbranchprob
	                   |                                                      (0 << 0x0A)  // tempsnowline
	                   |                                                      (1 << 0x0B)  // newcargo
	                   |                                                      (1 << 0x0C)  // enhancemultiplayer
	                   |                                                      (1 << 0x0D)  // onewayroads
	                   |                                                      (1 << 0x0E)  // irregularstations
	                   |                                                      (1 << 0x0F)  // statistics
	                   |                                                      (1 << 0x10)  // newsounds
	                   |                                                      (1 << 0x11)  // autoreplace
	                   |                                                      (1 << 0x12)  // autoslope
	                   |                                                      (0 << 0x13)  // followvehicle
	                   |                                                      (1 << 0x14)  // trams
	                   |                                                      (0 << 0x15)  // enhancetunnels
	                   |                                                      (1 << 0x16)  // shortrvs
	                   |                                                      (1 << 0x17)  // articulatedrvs
	                   |       ((_settings_game.vehicle.dynamic_engines ? 1 : 0) << 0x18)  // dynamic engines
	                   |                                                      (1 << 0x1E)  // variablerunningcosts
	                   |                                                      (1 << 0x1F); // any switch is on

	_ttdpatch_flags[4] =                                                      (1 << 0x00)  // larger persistent storage
	                   |             ((_settings_game.economy.inflation ? 1 : 0) << 0x01); // inflation is on
}

/** Reset and clear all NewGRF stations */
static void ResetCustomStations()
{
	for (GRFFile * const file : _grf_files) {
		StationSpec **&stations = file->stations;
		if (stations == nullptr) continue;
		for (uint i = 0; i < NUM_STATIONS_PER_GRF; i++) {
			if (stations[i] == nullptr) continue;
			StationSpec *statspec = stations[i];

			delete[] statspec->renderdata;

			/* Release platforms and layouts */
			if (!statspec->copied_layouts) {
				for (uint l = 0; l < statspec->lengths; l++) {
					for (uint p = 0; p < statspec->platforms[l]; p++) {
						free(statspec->layouts[l][p]);
					}
					free(statspec->layouts[l]);
				}
				free(statspec->layouts);
				free(statspec->platforms);
			}

			/* Release this station */
			free(statspec);
		}

		/* Free and reset the station data */
		free(stations);
		stations = nullptr;
	}
}

/** Reset and clear all NewGRF houses */
static void ResetCustomHouses()
{
	for (GRFFile * const file : _grf_files) {
		HouseSpec **&housespec = file->housespec;
		if (housespec == nullptr) continue;
		for (uint i = 0; i < NUM_HOUSES_PER_GRF; i++) {
			free(housespec[i]);
		}

		free(housespec);
		housespec = nullptr;
	}
}

/** Reset and clear all NewGRF airports */
static void ResetCustomAirports()
{
	for (GRFFile * const file : _grf_files) {
		AirportSpec **aslist = file->airportspec;
		if (aslist != nullptr) {
			for (uint i = 0; i < NUM_AIRPORTS_PER_GRF; i++) {
				AirportSpec *as = aslist[i];

				if (as != nullptr) {
					/* We need to remove the tiles layouts */
					for (int j = 0; j < as->num_table; j++) {
						/* remove the individual layouts */
						free(as->table[j]);
					}
					free(as->table);
					free(as->depot_table);
					free(as->rotation);

					free(as);
				}
			}
			free(aslist);
			file->airportspec = nullptr;
		}

		AirportTileSpec **&airporttilespec = file->airtspec;
		if (airporttilespec != nullptr) {
			for (uint i = 0; i < NUM_AIRPORTTILES_PER_GRF; i++) {
				free(airporttilespec[i]);
			}
			free(airporttilespec);
			airporttilespec = nullptr;
		}
	}
}

/** Reset and clear all NewGRF industries */
static void ResetCustomIndustries()
{
	for (GRFFile * const file : _grf_files) {
		IndustrySpec **&industryspec = file->industryspec;
		IndustryTileSpec **&indtspec = file->indtspec;

		/* We are verifiying both tiles and industries specs loaded from the grf file
		 * First, let's deal with industryspec */
		if (industryspec != nullptr) {
			for (uint i = 0; i < NUM_INDUSTRYTYPES_PER_GRF; i++) {
				IndustrySpec *ind = industryspec[i];
				delete ind;
			}

			free(industryspec);
			industryspec = nullptr;
		}

		if (indtspec == nullptr) continue;
		for (uint i = 0; i < NUM_INDUSTRYTILES_PER_GRF; i++) {
			free(indtspec[i]);
		}

		free(indtspec);
		indtspec = nullptr;
	}
}

/** Reset and clear all NewObjects */
static void ResetCustomObjects()
{
	for (GRFFile * const file : _grf_files) {
		ObjectSpec **&objectspec = file->objectspec;
		if (objectspec == nullptr) continue;
		for (uint i = 0; i < NUM_OBJECTS_PER_GRF; i++) {
			free(objectspec[i]);
		}

		free(objectspec);
		objectspec = nullptr;
	}
}

/** Reset and clear all NewGRFs */
static void ResetNewGRF()
{
	for (GRFFile * const file : _grf_files) {
		delete file;
	}

	_grf_files.clear();
	_cur.grffile   = nullptr;
}

/** Clear all NewGRF errors */
static void ResetNewGRFErrors()
{
	for (GRFConfig *c = _grfconfig; c != nullptr; c = c->next) {
		if (!HasBit(c->flags, GCF_COPY) && c->error != nullptr) {
			delete c->error;
			c->error = nullptr;
		}
	}
}

/**
 * Reset all NewGRF loaded data
 */
void ResetNewGRFData()
{
	CleanUpStrings();
	CleanUpGRFTownNames();

	/* Copy/reset original engine info data */
	SetupEngines();

	/* Copy/reset original bridge info data */
	ResetBridges();

	/* Reset rail type information */
	ResetRailTypes();

	/* Copy/reset original road type info data */
	ResetRoadTypes();

	/* Allocate temporary refit/cargo class data */
	_gted = CallocT<GRFTempEngineData>(Engine::GetPoolSize());

	/* Fill rail type label temporary data for default trains */
	for (const Engine *e : Engine::IterateType(VEH_TRAIN)) {
		_gted[e->index].railtypelabel = GetRailTypeInfo(e->u.rail.railtype)->label;
	}

	/* Reset GRM reservations */
	memset(&_grm_engines, 0, sizeof(_grm_engines));
	memset(&_grm_cargoes, 0, sizeof(_grm_cargoes));

	/* Reset generic feature callback lists */
	ResetGenericCallbacks();

	/* Reset price base data */
	ResetPriceBaseMultipliers();

	/* Reset the curencies array */
	ResetCurrencies();

	/* Reset the house array */
	ResetCustomHouses();
	ResetHouses();

	/* Reset the industries structures*/
	ResetCustomIndustries();
	ResetIndustries();

	/* Reset the objects. */
	ObjectClass::Reset();
	ResetCustomObjects();
	ResetObjects();

	/* Reset station classes */
	StationClass::Reset();
	ResetCustomStations();

	/* Reset airport-related structures */
	AirportClass::Reset();
	ResetCustomAirports();
	AirportSpec::ResetAirports();
	AirportTileSpec::ResetAirportTiles();

	/* Reset road stop classes */
	RoadStopClass::Reset();
	//ResetCustomRoadStops();

	/* Reset canal sprite groups and flags */
	memset(_water_feature, 0, sizeof(_water_feature));

	/* Reset the snowline table. */
	ClearSnowLine();

	/* Reset NewGRF files */
	ResetNewGRF();

	/* Reset NewGRF errors. */
	ResetNewGRFErrors();

	/* Set up the default cargo types */
	SetupCargoForClimate(_settings_game.game_creation.landscape);

	/* Reset misc GRF features and train list display variables */
	_misc_grf_features = 0;

	_loaded_newgrf_features.has_2CC           = false;
	_loaded_newgrf_features.used_liveries     = 1 << LS_DEFAULT;
	_loaded_newgrf_features.shore             = SHORE_REPLACE_NONE;
	_loaded_newgrf_features.tram              = TRAMWAY_REPLACE_DEPOT_NONE;

	/* Clear all GRF overrides */
	_grf_id_overrides.clear();

	InitializeSoundPool();
	_spritegroup_pool.CleanPool();
}

/**
 * Reset NewGRF data which is stored persistently in savegames.
 */
void ResetPersistentNewGRFData()
{
	/* Reset override managers */
	_engine_mngr.ResetToDefaultMapping();
	_house_mngr.ResetMapping();
	_industry_mngr.ResetMapping();
	_industile_mngr.ResetMapping();
	_airport_mngr.ResetMapping();
	_airporttile_mngr.ResetMapping();
}

/**
 * Construct the Cargo Mapping
 * @note This is the reverse of a cargo translation table
 */
static void BuildCargoTranslationMap()
{
	memset(_cur.grffile->cargo_map, 0xFF, sizeof(_cur.grffile->cargo_map));

	for (CargoID c = 0; c < NUM_CARGO; c++) {
		const CargoSpec *cs = CargoSpec::Get(c);
		if (!cs->IsValid()) continue;

		if (_cur.grffile->cargo_list.size() == 0) {
			/* Default translation table, so just a straight mapping to bitnum */
			_cur.grffile->cargo_map[c] = cs->bitnum;
		} else {
			/* Check the translation table for this cargo's label */
			int idx = find_index(_cur.grffile->cargo_list, {cs->label});
			if (idx >= 0) _cur.grffile->cargo_map[c] = idx;
		}
	}
}

/**
 * Prepare loading a NewGRF file with its config
 * @param config The NewGRF configuration struct with name, id, parameters and alike.
 */
static void InitNewGRFFile(const GRFConfig *config)
{
	GRFFile *newfile = GetFileByFilename(config->filename);
	if (newfile != nullptr) {
		/* We already loaded it once. */
		_cur.grffile = newfile;
		return;
	}

	newfile = new GRFFile(config);
	_grf_files.push_back(_cur.grffile = newfile);
}

/**
 * Constructor for GRFFile
 * @param config GRFConfig to copy name, grfid and parameters from.
 */
GRFFile::GRFFile(const GRFConfig *config)
{
	this->filename = stredup(config->filename);
	this->grfid = config->ident.grfid;

	/* Initialise local settings to defaults */
	this->traininfo_vehicle_pitch = 0;
	this->traininfo_vehicle_width = TRAININFO_DEFAULT_VEHICLE_WIDTH;

	/* Mark price_base_multipliers as 'not set' */
	for (Price i = PR_BEGIN; i < PR_END; i++) {
		this->price_base_multipliers[i] = INVALID_PRICE_MODIFIER;
	}

	/* Initialise rail type map with default rail types */
	std::fill(std::begin(this->railtype_map), std::end(this->railtype_map), INVALID_RAILTYPE);
	this->railtype_map[0] = RAILTYPE_RAIL;
	this->railtype_map[1] = RAILTYPE_ELECTRIC;
	this->railtype_map[2] = RAILTYPE_MONO;
	this->railtype_map[3] = RAILTYPE_MAGLEV;

	/* Initialise road type map with default road types */
	std::fill(std::begin(this->roadtype_map), std::end(this->roadtype_map), INVALID_ROADTYPE);
	this->roadtype_map[0] = ROADTYPE_ROAD;

	/* Initialise tram type map with default tram types */
	std::fill(std::begin(this->tramtype_map), std::end(this->tramtype_map), INVALID_ROADTYPE);
	this->tramtype_map[0] = ROADTYPE_TRAM;

	/* Copy the initial parameter list
	 * 'Uninitialised' parameters are zeroed as that is their default value when dynamically creating them. */
	static_assert(lengthof(this->param) == lengthof(config->param) && lengthof(this->param) == 0x80);

	assert(config->num_params <= lengthof(config->param));
	this->param_end = config->num_params;
	if (this->param_end > 0) {
		MemCpyT(this->param, config->param, this->param_end);
	}
}

GRFFile::~GRFFile()
{
	free(this->filename);
	delete[] this->language_map;
}


/**
 * List of what cargo labels are refittable for the given the vehicle-type.
 * Only currently active labels are applied.
 */
static const CargoLabel _default_refitmasks_rail[] = {
	'PASS', 'COAL', 'MAIL', 'LVST', 'GOOD', 'GRAI', 'WHEA', 'MAIZ', 'WOOD',
	'IORE', 'STEL', 'VALU', 'GOLD', 'DIAM', 'PAPR', 'FOOD', 'FRUT', 'CORE',
	'WATR', 'SUGR', 'TOYS', 'BATT', 'SWET', 'TOFF', 'COLA', 'CTCD', 'BUBL',
	'PLST', 'FZDR',
	0 };

static const CargoLabel _default_refitmasks_road[] = {
	0 };

static const CargoLabel _default_refitmasks_ships[] = {
	'COAL', 'MAIL', 'LVST', 'GOOD', 'GRAI', 'WHEA', 'MAIZ', 'WOOD', 'IORE',
	'STEL', 'VALU', 'GOLD', 'DIAM', 'PAPR', 'FOOD', 'FRUT', 'CORE', 'WATR',
	'RUBR', 'SUGR', 'TOYS', 'BATT', 'SWET', 'TOFF', 'COLA', 'CTCD', 'BUBL',
	'PLST', 'FZDR',
	0 };

static const CargoLabel _default_refitmasks_aircraft[] = {
	'PASS', 'MAIL', 'GOOD', 'VALU', 'GOLD', 'DIAM', 'FOOD', 'FRUT', 'SUGR',
	'TOYS', 'BATT', 'SWET', 'TOFF', 'COLA', 'CTCD', 'BUBL', 'PLST', 'FZDR',
	0 };

static const CargoLabel * const _default_refitmasks[] = {
	_default_refitmasks_rail,
	_default_refitmasks_road,
	_default_refitmasks_ships,
	_default_refitmasks_aircraft,
};


/**
 * Precalculate refit masks from cargo classes for all vehicles.
 */
static void CalculateRefitMasks()
{
	for (Engine *e : Engine::Iterate()) {
		EngineID engine = e->index;
		EngineInfo *ei = &e->info;
		bool only_defaultcargo; ///< Set if the vehicle shall carry only the default cargo

		/* Did the newgrf specify any refitting? If not, use defaults. */
		if (_gted[engine].refittability != GRFTempEngineData::UNSET) {
			CargoTypes mask = 0;
			CargoTypes not_mask = 0;
			CargoTypes xor_mask = ei->refit_mask;

			/* If the original masks set by the grf are zero, the vehicle shall only carry the default cargo.
			 * Note: After applying the translations, the vehicle may end up carrying no defined cargo. It becomes unavailable in that case. */
			only_defaultcargo = _gted[engine].refittability == GRFTempEngineData::EMPTY;

			if (_gted[engine].cargo_allowed != 0) {
				/* Build up the list of cargo types from the set cargo classes. */
				const CargoSpec *cs;
				FOR_ALL_CARGOSPECS(cs) {
					if (_gted[engine].cargo_allowed    & cs->classes) SetBit(mask,     cs->Index());
					if (_gted[engine].cargo_disallowed & cs->classes) SetBit(not_mask, cs->Index());
				}
			}

			ei->refit_mask = ((mask & ~not_mask) ^ xor_mask) & _cargo_mask;

			/* Apply explicit refit includes/excludes. */
			ei->refit_mask |= _gted[engine].ctt_include_mask;
			ei->refit_mask &= ~_gted[engine].ctt_exclude_mask;
		} else {
			CargoTypes xor_mask = 0;

			/* Don't apply default refit mask to wagons nor engines with no capacity */
			if (e->type != VEH_TRAIN || (e->u.rail.capacity != 0 && e->u.rail.railveh_type != RAILVEH_WAGON)) {
				const CargoLabel *cl = _default_refitmasks[e->type];
				for (uint i = 0;; i++) {
					if (cl[i] == 0) break;

					CargoID cargo = GetCargoIDByLabel(cl[i]);
					if (cargo == CT_INVALID) continue;

					SetBit(xor_mask, cargo);
				}
			}

			ei->refit_mask = xor_mask & _cargo_mask;

			/* If the mask is zero, the vehicle shall only carry the default cargo */
			only_defaultcargo = (ei->refit_mask == 0);
		}

		/* Clear invalid cargoslots (from default vehicles or pre-NewCargo GRFs) */
		if (!HasBit(_cargo_mask, ei->cargo_type)) ei->cargo_type = CT_INVALID;

		/* Ensure that the vehicle is either not refittable, or that the default cargo is one of the refittable cargoes.
		 * Note: Vehicles refittable to no cargo are handle differently to vehicle refittable to a single cargo. The latter might have subtypes. */
		if (!only_defaultcargo && (e->type != VEH_SHIP || e->u.ship.old_refittable) && ei->cargo_type != CT_INVALID && !HasBit(ei->refit_mask, ei->cargo_type)) {
			ei->cargo_type = CT_INVALID;
		}

		/* Check if this engine's cargo type is valid. If not, set to the first refittable
		 * cargo type. Finally disable the vehicle, if there is still no cargo. */
		if (ei->cargo_type == CT_INVALID && ei->refit_mask != 0) {
			/* Figure out which CTT to use for the default cargo, if it is 'first refittable'. */
			const uint8 *cargo_map_for_first_refittable = nullptr;
			{
				const GRFFile *file = _gted[engine].defaultcargo_grf;
				if (file == nullptr) file = e->GetGRF();
				if (file != nullptr && file->grf_version >= 8 && file->cargo_list.size() != 0) {
					cargo_map_for_first_refittable = file->cargo_map;
				}
			}

			if (cargo_map_for_first_refittable != nullptr) {
				/* Use first refittable cargo from cargo translation table */
				byte best_local_slot = 0xFF;
				CargoID cargo_type;
				FOR_EACH_SET_CARGO_ID(cargo_type, ei->refit_mask) {
					byte local_slot = cargo_map_for_first_refittable[cargo_type];
					if (local_slot < best_local_slot) {
						best_local_slot = local_slot;
						ei->cargo_type = cargo_type;
					}
				}
			}

			if (ei->cargo_type == CT_INVALID) {
				/* Use first refittable cargo slot */
				ei->cargo_type = (CargoID)FindFirstBit(ei->refit_mask);
			}
		}
		if (ei->cargo_type == CT_INVALID) ei->climates = 0;

		/* Clear refit_mask for not refittable ships */
		if (e->type == VEH_SHIP && !e->u.ship.old_refittable) {
			ei->refit_mask = 0;
		}
	}
}

/** Set to use the correct action0 properties for each canal feature */
static void FinaliseCanals()
{
	for (uint i = 0; i < CF_END; i++) {
		if (_water_feature[i].grffile != nullptr) {
			_water_feature[i].callback_mask = _water_feature[i].grffile->canal_local_properties[i].callback_mask;
			_water_feature[i].flags = _water_feature[i].grffile->canal_local_properties[i].flags;
		}
	}
}

/** Check for invalid engines */
static void FinaliseEngineArray()
{
	for (Engine *e : Engine::Iterate()) {
		if (e->GetGRF() == nullptr) {
			const EngineIDMapping &eid = _engine_mngr[e->index];
			if (eid.grfid != INVALID_GRFID || eid.internal_id != eid.substitute_id) {
				e->info.string_id = STR_NEWGRF_INVALID_ENGINE;
			}
		}

		if (!HasBit(e->info.climates, _settings_game.game_creation.landscape)) continue;

		/* When the train does not set property 27 (misc flags), but it
		 * is overridden by a NewGRF graphically we want to disable the
		 * flipping possibility. */
		if (e->type == VEH_TRAIN && !_gted[e->index].prop27_set && e->GetGRF() != nullptr && is_custom_sprite(e->u.rail.image_index)) {
			ClrBit(e->info.misc_flags, EF_RAIL_FLIPS);
		}

		/* Skip wagons, there livery is defined via the engine */
		if (e->type != VEH_TRAIN || e->u.rail.railveh_type != RAILVEH_WAGON) {
			LiveryScheme ls = GetEngineLiveryScheme(e->index, INVALID_ENGINE, nullptr);
			SetBit(_loaded_newgrf_features.used_liveries, ls);
			/* Note: For ships and roadvehicles we assume that they cannot be refitted between passenger and freight */

			if (e->type == VEH_TRAIN) {
				SetBit(_loaded_newgrf_features.used_liveries, LS_FREIGHT_WAGON);
				switch (ls) {
					case LS_STEAM:
					case LS_DIESEL:
					case LS_ELECTRIC:
					case LS_MONORAIL:
					case LS_MAGLEV:
						SetBit(_loaded_newgrf_features.used_liveries, LS_PASSENGER_WAGON_STEAM + ls - LS_STEAM);
						break;

					case LS_DMU:
					case LS_EMU:
						SetBit(_loaded_newgrf_features.used_liveries, LS_PASSENGER_WAGON_DIESEL + ls - LS_DMU);
						break;

					default: NOT_REACHED();
				}
			}
		}
	}
}

/** Check for invalid cargoes */
static void FinaliseCargoArray()
{
	for (CargoID c = 0; c < NUM_CARGO; c++) {
		CargoSpec *cs = CargoSpec::Get(c);
		if (!cs->IsValid()) {
			cs->name = cs->name_single = cs->units_volume = STR_NEWGRF_INVALID_CARGO;
			cs->quantifier = STR_NEWGRF_INVALID_CARGO_QUANTITY;
			cs->abbrev = STR_NEWGRF_INVALID_CARGO_ABBREV;
		}
	}
}

/**
 * Check if a given housespec is valid and disable it if it's not.
 * The housespecs that follow it are used to check the validity of
 * multitile houses.
 * @param hs The housespec to check.
 * @param next1 The housespec that follows \c hs.
 * @param next2 The housespec that follows \c next1.
 * @param next3 The housespec that follows \c next2.
 * @param filename The filename of the newgrf this house was defined in.
 * @return Whether the given housespec is valid.
 */
static bool IsHouseSpecValid(HouseSpec *hs, const HouseSpec *next1, const HouseSpec *next2, const HouseSpec *next3, const char *filename)
{
	if (((hs->building_flags & BUILDING_HAS_2_TILES) != 0 &&
				(next1 == nullptr || !next1->enabled || (next1->building_flags & BUILDING_HAS_1_TILE) != 0)) ||
			((hs->building_flags & BUILDING_HAS_4_TILES) != 0 &&
				(next2 == nullptr || !next2->enabled || (next2->building_flags & BUILDING_HAS_1_TILE) != 0 ||
				next3 == nullptr || !next3->enabled || (next3->building_flags & BUILDING_HAS_1_TILE) != 0))) {
		hs->enabled = false;
		if (filename != nullptr) DEBUG(grf, 1, "FinaliseHouseArray: %s defines house %d as multitile, but no suitable tiles follow. Disabling house.", filename, hs->grf_prop.local_id);
		return false;
	}

	/* Some places sum population by only counting north tiles. Other places use all tiles causing desyncs.
	 * As the newgrf specs define population to be zero for non-north tiles, we just disable the offending house.
	 * If you want to allow non-zero populations somewhen, make sure to sum the population of all tiles in all places. */
	if (((hs->building_flags & BUILDING_HAS_2_TILES) != 0 && next1->population != 0) ||
			((hs->building_flags & BUILDING_HAS_4_TILES) != 0 && (next2->population != 0 || next3->population != 0))) {
		hs->enabled = false;
		if (filename != nullptr) DEBUG(grf, 1, "FinaliseHouseArray: %s defines multitile house %d with non-zero population on additional tiles. Disabling house.", filename, hs->grf_prop.local_id);
		return false;
	}

	/* Substitute type is also used for override, and having an override with a different size causes crashes.
	 * This check should only be done for NewGRF houses because grf_prop.subst_id is not set for original houses.*/
	if (filename != nullptr && (hs->building_flags & BUILDING_HAS_1_TILE) != (HouseSpec::Get(hs->grf_prop.subst_id)->building_flags & BUILDING_HAS_1_TILE)) {
		hs->enabled = false;
		DEBUG(grf, 1, "FinaliseHouseArray: %s defines house %d with different house size then it's substitute type. Disabling house.", filename, hs->grf_prop.local_id);
		return false;
	}

	/* Make sure that additional parts of multitile houses are not available. */
	if ((hs->building_flags & BUILDING_HAS_1_TILE) == 0 && (hs->building_availability & HZ_ZONALL) != 0 && (hs->building_availability & HZ_CLIMALL) != 0) {
		hs->enabled = false;
		if (filename != nullptr) DEBUG(grf, 1, "FinaliseHouseArray: %s defines house %d without a size but marked it as available. Disabling house.", filename, hs->grf_prop.local_id);
		return false;
	}

	return true;
}

/**
 * Make sure there is at least one house available in the year 0 for the given
 * climate / housezone combination.
 * @param bitmask The climate and housezone to check for. Exactly one climate
 *   bit and one housezone bit should be set.
 */
static void EnsureEarlyHouse(HouseZones bitmask)
{
	Year min_year = MAX_YEAR;

	for (int i = 0; i < NUM_HOUSES; i++) {
		HouseSpec *hs = HouseSpec::Get(i);
		if (hs == nullptr || !hs->enabled) continue;
		if ((hs->building_availability & bitmask) != bitmask) continue;
		if (hs->min_year < min_year) min_year = hs->min_year;
	}

	if (min_year == 0) return;

	for (int i = 0; i < NUM_HOUSES; i++) {
		HouseSpec *hs = HouseSpec::Get(i);
		if (hs == nullptr || !hs->enabled) continue;
		if ((hs->building_availability & bitmask) != bitmask) continue;
		if (hs->min_year == min_year) hs->min_year = 0;
	}
}

/**
 * Add all new houses to the house array. House properties can be set at any
 * time in the GRF file, so we can only add a house spec to the house array
 * after the file has finished loading. We also need to check the dates, due to
 * the TTDPatch behaviour described below that we need to emulate.
 */
static void FinaliseHouseArray()
{
	/* If there are no houses with start dates before 1930, then all houses
	 * with start dates of 1930 have them reset to 0. This is in order to be
	 * compatible with TTDPatch, where if no houses have start dates before
	 * 1930 and the date is before 1930, the game pretends that this is 1930.
	 * If there have been any houses defined with start dates before 1930 then
	 * the dates are left alone.
	 * On the other hand, why 1930? Just 'fix' the houses with the lowest
	 * minimum introduction date to 0.
	 */
	for (GRFFile * const file : _grf_files) {
		HouseSpec **&housespec = file->housespec;
		if (housespec == nullptr) continue;

		for (int i = 0; i < NUM_HOUSES_PER_GRF; i++) {
			HouseSpec *hs = housespec[i];

			if (hs == nullptr) continue;

			const HouseSpec *next1 = (i + 1 < NUM_HOUSES_PER_GRF ? housespec[i + 1] : nullptr);
			const HouseSpec *next2 = (i + 2 < NUM_HOUSES_PER_GRF ? housespec[i + 2] : nullptr);
			const HouseSpec *next3 = (i + 3 < NUM_HOUSES_PER_GRF ? housespec[i + 3] : nullptr);

			if (!IsHouseSpecValid(hs, next1, next2, next3, file->filename)) continue;

			_house_mngr.SetEntitySpec(hs);
		}
	}

	for (int i = 0; i < NUM_HOUSES; i++) {
		HouseSpec *hs = HouseSpec::Get(i);
		const HouseSpec *next1 = (i + 1 < NUM_HOUSES ? HouseSpec::Get(i + 1) : nullptr);
		const HouseSpec *next2 = (i + 2 < NUM_HOUSES ? HouseSpec::Get(i + 2) : nullptr);
		const HouseSpec *next3 = (i + 3 < NUM_HOUSES ? HouseSpec::Get(i + 3) : nullptr);

		/* We need to check all houses again to we are sure that multitile houses
		 * did get consecutive IDs and none of the parts are missing. */
		if (!IsHouseSpecValid(hs, next1, next2, next3, nullptr)) {
			/* GetHouseNorthPart checks 3 houses that are directly before
			 * it in the house pool. If any of those houses have multi-tile
			 * flags set it assumes it's part of a multitile house. Since
			 * we can have invalid houses in the pool marked as disabled, we
			 * don't want to have them influencing valid tiles. As such set
			 * building_flags to zero here to make sure any house following
			 * this one in the pool is properly handled as 1x1 house. */
			hs->building_flags = TILE_NO_FLAG;
		}
	}

	HouseZones climate_mask = (HouseZones)(1 << (_settings_game.game_creation.landscape + 12));
	EnsureEarlyHouse(HZ_ZON1 | climate_mask);
	EnsureEarlyHouse(HZ_ZON2 | climate_mask);
	EnsureEarlyHouse(HZ_ZON3 | climate_mask);
	EnsureEarlyHouse(HZ_ZON4 | climate_mask);
	EnsureEarlyHouse(HZ_ZON5 | climate_mask);

	if (_settings_game.game_creation.landscape == LT_ARCTIC) {
		EnsureEarlyHouse(HZ_ZON1 | HZ_SUBARTC_ABOVE);
		EnsureEarlyHouse(HZ_ZON2 | HZ_SUBARTC_ABOVE);
		EnsureEarlyHouse(HZ_ZON3 | HZ_SUBARTC_ABOVE);
		EnsureEarlyHouse(HZ_ZON4 | HZ_SUBARTC_ABOVE);
		EnsureEarlyHouse(HZ_ZON5 | HZ_SUBARTC_ABOVE);
	}
}

/**
 * Add all new industries to the industry array. Industry properties can be set at any
 * time in the GRF file, so we can only add a industry spec to the industry array
 * after the file has finished loading.
 */
static void FinaliseIndustriesArray()
{
	for (GRFFile * const file : _grf_files) {
		IndustrySpec **&industryspec = file->industryspec;
		IndustryTileSpec **&indtspec = file->indtspec;
		if (industryspec != nullptr) {
			for (int i = 0; i < NUM_INDUSTRYTYPES_PER_GRF; i++) {
				IndustrySpec *indsp = industryspec[i];

				if (indsp != nullptr && indsp->enabled) {
					StringID strid;
					/* process the conversion of text at the end, so to be sure everything will be fine
					 * and available.  Check if it does not return undefind marker, which is a very good sign of a
					 * substitute industry who has not changed the string been examined, thus using it as such */
					strid = GetGRFStringID(indsp->grf_prop.grffile->grfid, indsp->name);
					if (strid != STR_UNDEFINED) indsp->name = strid;

					strid = GetGRFStringID(indsp->grf_prop.grffile->grfid, indsp->closure_text);
					if (strid != STR_UNDEFINED) indsp->closure_text = strid;

					strid = GetGRFStringID(indsp->grf_prop.grffile->grfid, indsp->production_up_text);
					if (strid != STR_UNDEFINED) indsp->production_up_text = strid;

					strid = GetGRFStringID(indsp->grf_prop.grffile->grfid, indsp->production_down_text);
					if (strid != STR_UNDEFINED) indsp->production_down_text = strid;

					strid = GetGRFStringID(indsp->grf_prop.grffile->grfid, indsp->new_industry_text);
					if (strid != STR_UNDEFINED) indsp->new_industry_text = strid;

					if (indsp->station_name != STR_NULL) {
						/* STR_NULL (0) can be set by grf.  It has a meaning regarding assignation of the
						 * station's name. Don't want to lose the value, therefore, do not process. */
						strid = GetGRFStringID(indsp->grf_prop.grffile->grfid, indsp->station_name);
						if (strid != STR_UNDEFINED) indsp->station_name = strid;
					}

					_industry_mngr.SetEntitySpec(indsp);
				}
			}
		}

		if (indtspec != nullptr) {
			for (int i = 0; i < NUM_INDUSTRYTILES_PER_GRF; i++) {
				IndustryTileSpec *indtsp = indtspec[i];
				if (indtsp != nullptr) {
					_industile_mngr.SetEntitySpec(indtsp);
				}
			}
		}
	}

	for (uint j = 0; j < NUM_INDUSTRYTYPES; j++) {
		IndustrySpec *indsp = &_industry_specs[j];
		if (indsp->enabled && indsp->grf_prop.grffile != nullptr) {
			for (uint i = 0; i < 3; i++) {
				indsp->conflicting[i] = MapNewGRFIndustryType(indsp->conflicting[i], indsp->grf_prop.grffile->grfid);
			}
		}
		if (!indsp->enabled) {
			indsp->name = STR_NEWGRF_INVALID_INDUSTRYTYPE;
		}
	}
}

/**
 * Add all new objects to the object array. Object properties can be set at any
 * time in the GRF file, so we can only add an object spec to the object array
 * after the file has finished loading.
 */
static void FinaliseObjectsArray()
{
	for (GRFFile * const file : _grf_files) {
		ObjectSpec **&objectspec = file->objectspec;
		if (objectspec != nullptr) {
			for (int i = 0; i < NUM_OBJECTS_PER_GRF; i++) {
				if (objectspec[i] != nullptr && objectspec[i]->grf_prop.grffile != nullptr && objectspec[i]->enabled) {
					_object_mngr.SetEntitySpec(objectspec[i]);
				}
			}
		}
	}
}

/**
 * Add all new airports to the airport array. Airport properties can be set at any
 * time in the GRF file, so we can only add a airport spec to the airport array
 * after the file has finished loading.
 */
static void FinaliseAirportsArray()
{
	for (GRFFile * const file : _grf_files) {
		AirportSpec **&airportspec = file->airportspec;
		if (airportspec != nullptr) {
			for (int i = 0; i < NUM_AIRPORTS_PER_GRF; i++) {
				if (airportspec[i] != nullptr && airportspec[i]->enabled) {
					_airport_mngr.SetEntitySpec(airportspec[i]);
				}
			}
		}

		AirportTileSpec **&airporttilespec = file->airtspec;
		if (airporttilespec != nullptr) {
			for (uint i = 0; i < NUM_AIRPORTTILES_PER_GRF; i++) {
				if (airporttilespec[i] != nullptr && airporttilespec[i]->enabled) {
					_airporttile_mngr.SetEntitySpec(airporttilespec[i]);
				}
			}
		}
	}
}

/* Here we perform initial decoding of some special sprites (as are they
 * described at http://www.ttdpatch.net/src/newgrf.txt, but this is only a very
 * partial implementation yet).
 * XXX: We consider GRF files trusted. It would be trivial to exploit OTTD by
 * a crafted invalid GRF file. We should tell that to the user somehow, or
 * better make this more robust in the future. */
static void DecodeSpecialSprite(byte *buf, uint num, GrfLoadingStage stage)
{
	/* XXX: There is a difference between staged loading in TTDPatch and
	 * here.  In TTDPatch, for some reason actions 1 and 2 are carried out
	 * during stage 1, whilst action 3 is carried out during stage 2 (to
	 * "resolve" cargo IDs... wtf). This is a little problem, because cargo
	 * IDs are valid only within a given set (action 1) block, and may be
	 * overwritten after action 3 associates them. But overwriting happens
	 * in an earlier stage than associating, so...  We just process actions
	 * 1 and 2 in stage 2 now, let's hope that won't get us into problems.
	 * --pasky
	 * We need a pre-stage to set up GOTO labels of Action 0x10 because the grf
	 * is not in memory and scanning the file every time would be too expensive.
	 * In other stages we skip action 0x10 since it's already dealt with. */
	static const SpecialSpriteHandler handlers[][GLS_END] = {
		/* 0x00 */ { nullptr,       SafeChangeInfo, nullptr,         nullptr,         ReserveChangeInfo, FeatureChangeInfo, },
		/* 0x01 */ { SkipAct1,      SkipAct1,       SkipAct1,        SkipAct1,        SkipAct1,          NewSpriteSet, },
		/* 0x02 */ { nullptr,       nullptr,        nullptr,         nullptr,         nullptr,           NewSpriteGroup, },
		/* 0x03 */ { nullptr,       GRFUnsafe,      nullptr,         nullptr,         nullptr,           FeatureMapSpriteGroup, },
		/* 0x04 */ { nullptr,       nullptr,        nullptr,         nullptr,         nullptr,           FeatureNewName, },
		/* 0x05 */ { SkipAct5,      SkipAct5,       SkipAct5,        SkipAct5,        SkipAct5,          GraphicsNew, },
		/* 0x06 */ { nullptr,       nullptr,        nullptr,         CfgApply,        CfgApply,          CfgApply, },
		/* 0x07 */ { nullptr,       nullptr,        nullptr,         nullptr,         SkipIf,            SkipIf, },
		/* 0x08 */ { ScanInfo,      nullptr,        nullptr,         GRFInfo,         GRFInfo,           GRFInfo, },
		/* 0x09 */ { nullptr,       nullptr,        nullptr,         SkipIf,          SkipIf,            SkipIf, },
		/* 0x0A */ { SkipActA,      SkipActA,       SkipActA,        SkipActA,        SkipActA,          SpriteReplace, },
		/* 0x0B */ { nullptr,       nullptr,        nullptr,         GRFLoadError,    GRFLoadError,      GRFLoadError, },
		/* 0x0C */ { nullptr,       nullptr,        nullptr,         GRFComment,      nullptr,           GRFComment, },
		/* 0x0D */ { nullptr,       SafeParamSet,   nullptr,         ParamSet,        ParamSet,          ParamSet, },
		/* 0x0E */ { nullptr,       SafeGRFInhibit, nullptr,         GRFInhibit,      GRFInhibit,        GRFInhibit, },
		/* 0x0F */ { nullptr,       GRFUnsafe,      nullptr,         FeatureTownName, nullptr,           nullptr, },
		/* 0x10 */ { nullptr,       nullptr,        DefineGotoLabel, nullptr,         nullptr,           nullptr, },
		/* 0x11 */ { SkipAct11,     GRFUnsafe,      SkipAct11,       GRFSound,        SkipAct11,         GRFSound, },
		/* 0x12 */ { SkipAct12,     SkipAct12,      SkipAct12,       SkipAct12,       SkipAct12,         LoadFontGlyph, },
		/* 0x13 */ { nullptr,       nullptr,        nullptr,         nullptr,         nullptr,           TranslateGRFStrings, },
		/* 0x14 */ { StaticGRFInfo, nullptr,        nullptr,         nullptr,         nullptr,           nullptr, },
	};

	GRFLocation location(_cur.grfconfig->ident.grfid, _cur.nfo_line);

	GRFLineToSpriteOverride::iterator it = _grf_line_to_action6_sprite_override.find(location);
	if (it == _grf_line_to_action6_sprite_override.end()) {
		/* No preloaded sprite to work with; read the
		 * pseudo sprite content. */
		FioReadBlock(buf, num);
	} else {
		/* Use the preloaded sprite data. */
		buf = _grf_line_to_action6_sprite_override[location];
		grfmsg(7, "DecodeSpecialSprite: Using preloaded pseudo sprite data");

		/* Skip the real (original) content of this action. */
		FioSeekTo(num, SEEK_CUR);
	}

	ByteReader br(buf, buf + num);
	ByteReader *bufp = &br;

	try {
		byte action = bufp->ReadByte();

		if (action == 0xFF) {
			grfmsg(2, "DecodeSpecialSprite: Unexpected data block, skipping");
		} else if (action == 0xFE) {
			grfmsg(2, "DecodeSpecialSprite: Unexpected import block, skipping");
		} else if (action >= lengthof(handlers)) {
			grfmsg(7, "DecodeSpecialSprite: Skipping unknown action 0x%02X", action);
		} else if (handlers[action][stage] == nullptr) {
			grfmsg(7, "DecodeSpecialSprite: Skipping action 0x%02X in stage %d", action, stage);
		} else {
			grfmsg(7, "DecodeSpecialSprite: Handling action 0x%02X in stage %d", action, stage);
			handlers[action][stage](bufp);
		}
	} catch (...) {
		grfmsg(1, "DecodeSpecialSprite: Tried to read past end of pseudo-sprite data");
		DisableGrf(STR_NEWGRF_ERROR_READ_BOUNDS);
	}
}


/** Signature of a container version 2 GRF. */
extern const byte _grf_cont_v2_sig[8] = {'G', 'R', 'F', 0x82, 0x0D, 0x0A, 0x1A, 0x0A};

/**
 * Get the container version of the currently opened GRF file.
 * @return Container version of the GRF file or 0 if the file is corrupt/no GRF file.
 */
byte GetGRFContainerVersion()
{
	size_t pos = FioGetPos();

	if (FioReadWord() == 0) {
		/* Check for GRF container version 2, which is identified by the bytes
		 * '47 52 46 82 0D 0A 1A 0A' at the start of the file. */
		for (uint i = 0; i < lengthof(_grf_cont_v2_sig); i++) {
			if (FioReadByte() != _grf_cont_v2_sig[i]) return 0; // Invalid format
		}

		return 2;
	}

	/* Container version 1 has no header, rewind to start. */
	FioSeekTo(pos, SEEK_SET);
	return 1;
}

/**
 * Load a particular NewGRF.
 * @param config     The configuration of the to be loaded NewGRF.
 * @param file_index The Fio index of the first NewGRF to load.
 * @param stage      The loading stage of the NewGRF.
 * @param subdir     The sub directory to find the NewGRF in.
 */
void LoadNewGRFFile(GRFConfig *config, uint file_index, GrfLoadingStage stage, Subdirectory subdir)
{
	const char *filename = config->filename;

	/* A .grf file is activated only if it was active when the game was
	 * started.  If a game is loaded, only its active .grfs will be
	 * reactivated, unless "loadallgraphics on" is used.  A .grf file is
	 * considered active if its action 8 has been processed, i.e. its
	 * action 8 hasn't been skipped using an action 7.
	 *
	 * During activation, only actions 0, 1, 2, 3, 4, 5, 7, 8, 9, 0A and 0B are
	 * carried out.  All others are ignored, because they only need to be
	 * processed once at initialization.  */
	if (stage != GLS_FILESCAN && stage != GLS_SAFETYSCAN && stage != GLS_LABELSCAN) {
		_cur.grffile = GetFileByFilename(filename);
		if (_cur.grffile == nullptr) usererror("File '%s' lost in cache.\n", filename);
		if (stage == GLS_RESERVE && config->status != GCS_INITIALISED) return;
		if (stage == GLS_ACTIVATION && !HasBit(config->flags, GCF_RESERVED)) return;
	}

	if (file_index >= MAX_FILE_SLOTS) {
		DEBUG(grf, 0, "'%s' is not loaded as the maximum number of file slots has been reached", filename);
		config->status = GCS_DISABLED;
		config->error  = new GRFError(STR_NEWGRF_ERROR_MSG_FATAL, STR_NEWGRF_ERROR_TOO_MANY_NEWGRFS_LOADED);
		return;
	}

	FioOpenFile(file_index, filename, subdir);
	_cur.file_index = file_index; // XXX
	_palette_remap_grf[_cur.file_index] = (config->palette & GRFP_USE_MASK);

	_cur.grfconfig = config;

	DEBUG(grf, 2, "LoadNewGRFFile: Reading NewGRF-file '%s'", filename);

	_cur.grf_container_ver = GetGRFContainerVersion();
	if (_cur.grf_container_ver == 0) {
		DEBUG(grf, 7, "LoadNewGRFFile: Custom .grf has invalid format");
		return;
	}

	if (stage == GLS_INIT || stage == GLS_ACTIVATION) {
		/* We need the sprite offsets in the init stage for NewGRF sounds
		 * and in the activation stage for real sprites. */
		ReadGRFSpriteOffsets(_cur.grf_container_ver);
	} else {
		/* Skip sprite section offset if present. */
		if (_cur.grf_container_ver >= 2) FioReadDword();
	}

	if (_cur.grf_container_ver >= 2) {
		/* Read compression value. */
		byte compression = FioReadByte();
		if (compression != 0) {
			DEBUG(grf, 7, "LoadNewGRFFile: Unsupported compression format");
			return;
		}
	}

	/* Skip the first sprite; we don't care about how many sprites this
	 * does contain; newest TTDPatches and George's longvehicles don't
	 * neither, apparently. */
	uint32 num = _cur.grf_container_ver >= 2 ? FioReadDword() : FioReadWord();
	if (num == 4 && FioReadByte() == 0xFF) {
		FioReadDword();
	} else {
		DEBUG(grf, 7, "LoadNewGRFFile: Custom .grf has invalid format");
		return;
	}

	_cur.ClearDataForNextFile();

	ReusableBuffer<byte> buf;

	while ((num = (_cur.grf_container_ver >= 2 ? FioReadDword() : FioReadWord())) != 0) {
		byte type = FioReadByte();
		_cur.nfo_line++;

		if (type == 0xFF) {
			if (_cur.skip_sprites == 0) {
				DecodeSpecialSprite(buf.Allocate(num), num, stage);

				/* Stop all processing if we are to skip the remaining sprites */
				if (_cur.skip_sprites == -1) break;

				continue;
			} else {
				FioSkipBytes(num);
			}
		} else {
			if (_cur.skip_sprites == 0) {
				grfmsg(0, "LoadNewGRFFile: Unexpected sprite, disabling");
				DisableGrf(STR_NEWGRF_ERROR_UNEXPECTED_SPRITE);
				break;
			}

			if (_cur.grf_container_ver >= 2 && type == 0xFD) {
				/* Reference to data section. Container version >= 2 only. */
				FioSkipBytes(num);
			} else {
				FioSkipBytes(7);
				SkipSpriteData(type, num - 8);
			}
		}

		if (_cur.skip_sprites > 0) _cur.skip_sprites--;
	}
}

/**
 * Relocates the old shore sprites at new positions.
 *
 * 1. If shore sprites are neither loaded by Action5 nor ActionA, the extra sprites from openttd(w/d).grf are used. (SHORE_REPLACE_ONLY_NEW)
 * 2. If a newgrf replaces some shore sprites by ActionA. The (maybe also replaced) grass tiles are used for corner shores. (SHORE_REPLACE_ACTION_A)
 * 3. If a newgrf replaces shore sprites by Action5 any shore replacement by ActionA has no effect. (SHORE_REPLACE_ACTION_5)
 */
static void ActivateOldShore()
{
	/* Use default graphics, if no shore sprites were loaded.
	 * Should not happen, as the base set's extra grf should include some. */
	if (_loaded_newgrf_features.shore == SHORE_REPLACE_NONE) _loaded_newgrf_features.shore = SHORE_REPLACE_ACTION_A;

	if (_loaded_newgrf_features.shore != SHORE_REPLACE_ACTION_5) {
		DupSprite(SPR_ORIGINALSHORE_START +  1, SPR_SHORE_BASE +  1); // SLOPE_W
		DupSprite(SPR_ORIGINALSHORE_START +  2, SPR_SHORE_BASE +  2); // SLOPE_S
		DupSprite(SPR_ORIGINALSHORE_START +  6, SPR_SHORE_BASE +  3); // SLOPE_SW
		DupSprite(SPR_ORIGINALSHORE_START +  0, SPR_SHORE_BASE +  4); // SLOPE_E
		DupSprite(SPR_ORIGINALSHORE_START +  4, SPR_SHORE_BASE +  6); // SLOPE_SE
		DupSprite(SPR_ORIGINALSHORE_START +  3, SPR_SHORE_BASE +  8); // SLOPE_N
		DupSprite(SPR_ORIGINALSHORE_START +  7, SPR_SHORE_BASE +  9); // SLOPE_NW
		DupSprite(SPR_ORIGINALSHORE_START +  5, SPR_SHORE_BASE + 12); // SLOPE_NE
	}

	if (_loaded_newgrf_features.shore == SHORE_REPLACE_ACTION_A) {
		DupSprite(SPR_FLAT_GRASS_TILE + 16, SPR_SHORE_BASE +  0); // SLOPE_STEEP_S
		DupSprite(SPR_FLAT_GRASS_TILE + 17, SPR_SHORE_BASE +  5); // SLOPE_STEEP_W
		DupSprite(SPR_FLAT_GRASS_TILE +  7, SPR_SHORE_BASE +  7); // SLOPE_WSE
		DupSprite(SPR_FLAT_GRASS_TILE + 15, SPR_SHORE_BASE + 10); // SLOPE_STEEP_N
		DupSprite(SPR_FLAT_GRASS_TILE + 11, SPR_SHORE_BASE + 11); // SLOPE_NWS
		DupSprite(SPR_FLAT_GRASS_TILE + 13, SPR_SHORE_BASE + 13); // SLOPE_ENW
		DupSprite(SPR_FLAT_GRASS_TILE + 14, SPR_SHORE_BASE + 14); // SLOPE_SEN
		DupSprite(SPR_FLAT_GRASS_TILE + 18, SPR_SHORE_BASE + 15); // SLOPE_STEEP_E

		/* XXX - SLOPE_EW, SLOPE_NS are currently not used.
		 *       If they would be used somewhen, then these grass tiles will most like not look as needed */
		DupSprite(SPR_FLAT_GRASS_TILE +  5, SPR_SHORE_BASE + 16); // SLOPE_EW
		DupSprite(SPR_FLAT_GRASS_TILE + 10, SPR_SHORE_BASE + 17); // SLOPE_NS
	}
}

/**
 * Replocate the old tram depot sprites to the new position, if no new ones were loaded.
 */
static void ActivateOldTramDepot()
{
	if (_loaded_newgrf_features.tram == TRAMWAY_REPLACE_DEPOT_WITH_TRACK) {
		DupSprite(SPR_ROAD_DEPOT               + 0, SPR_TRAMWAY_DEPOT_NO_TRACK + 0); // use road depot graphics for "no tracks"
		DupSprite(SPR_TRAMWAY_DEPOT_WITH_TRACK + 1, SPR_TRAMWAY_DEPOT_NO_TRACK + 1);
		DupSprite(SPR_ROAD_DEPOT               + 2, SPR_TRAMWAY_DEPOT_NO_TRACK + 2); // use road depot graphics for "no tracks"
		DupSprite(SPR_TRAMWAY_DEPOT_WITH_TRACK + 3, SPR_TRAMWAY_DEPOT_NO_TRACK + 3);
		DupSprite(SPR_TRAMWAY_DEPOT_WITH_TRACK + 4, SPR_TRAMWAY_DEPOT_NO_TRACK + 4);
		DupSprite(SPR_TRAMWAY_DEPOT_WITH_TRACK + 5, SPR_TRAMWAY_DEPOT_NO_TRACK + 5);
	}
}

/**
 * Decide whether price base multipliers of grfs shall apply globally or only to the grf specifying them
 */
static void FinalisePriceBaseMultipliers()
{
	extern const PriceBaseSpec _price_base_specs[];
	/** Features, to which '_grf_id_overrides' applies. Currently vehicle features only. */
	static const uint32 override_features = (1 << GSF_TRAINS) | (1 << GSF_ROADVEHICLES) | (1 << GSF_SHIPS) | (1 << GSF_AIRCRAFT);

	/* Evaluate grf overrides */
	int num_grfs = (uint)_grf_files.size();
	int *grf_overrides = AllocaM(int, num_grfs);
	for (int i = 0; i < num_grfs; i++) {
		grf_overrides[i] = -1;

		GRFFile *source = _grf_files[i];
		uint32 override = _grf_id_overrides[source->grfid];
		if (override == 0) continue;

		GRFFile *dest = GetFileByGRFID(override);
		if (dest == nullptr) continue;

		grf_overrides[i] = find_index(_grf_files, dest);
		assert(grf_overrides[i] >= 0);
	}

	/* Override features and price base multipliers of earlier loaded grfs */
	for (int i = 0; i < num_grfs; i++) {
		if (grf_overrides[i] < 0 || grf_overrides[i] >= i) continue;
		GRFFile *source = _grf_files[i];
		GRFFile *dest = _grf_files[grf_overrides[i]];

		uint32 features = (source->grf_features | dest->grf_features) & override_features;
		source->grf_features |= features;
		dest->grf_features |= features;

		for (Price p = PR_BEGIN; p < PR_END; p++) {
			/* No price defined -> nothing to do */
			if (!HasBit(features, _price_base_specs[p].grf_feature) || source->price_base_multipliers[p] == INVALID_PRICE_MODIFIER) continue;
			DEBUG(grf, 3, "'%s' overrides price base multiplier %d of '%s'", source->filename, p, dest->filename);
			dest->price_base_multipliers[p] = source->price_base_multipliers[p];
		}
	}

	/* Propagate features and price base multipliers of afterwards loaded grfs, if none is present yet */
	for (int i = num_grfs - 1; i >= 0; i--) {
		if (grf_overrides[i] < 0 || grf_overrides[i] <= i) continue;
		GRFFile *source = _grf_files[i];
		GRFFile *dest = _grf_files[grf_overrides[i]];

		uint32 features = (source->grf_features | dest->grf_features) & override_features;
		source->grf_features |= features;
		dest->grf_features |= features;

		for (Price p = PR_BEGIN; p < PR_END; p++) {
			/* Already a price defined -> nothing to do */
			if (!HasBit(features, _price_base_specs[p].grf_feature) || dest->price_base_multipliers[p] != INVALID_PRICE_MODIFIER) continue;
			DEBUG(grf, 3, "Price base multiplier %d from '%s' propagated to '%s'", p, source->filename, dest->filename);
			dest->price_base_multipliers[p] = source->price_base_multipliers[p];
		}
	}

	/* The 'master grf' now have the correct multipliers. Assign them to the 'addon grfs' to make everything consistent. */
	for (int i = 0; i < num_grfs; i++) {
		if (grf_overrides[i] < 0) continue;
		GRFFile *source = _grf_files[i];
		GRFFile *dest = _grf_files[grf_overrides[i]];

		uint32 features = (source->grf_features | dest->grf_features) & override_features;
		source->grf_features |= features;
		dest->grf_features |= features;

		for (Price p = PR_BEGIN; p < PR_END; p++) {
			if (!HasBit(features, _price_base_specs[p].grf_feature)) continue;
			if (source->price_base_multipliers[p] != dest->price_base_multipliers[p]) {
				DEBUG(grf, 3, "Price base multiplier %d from '%s' propagated to '%s'", p, dest->filename, source->filename);
			}
			source->price_base_multipliers[p] = dest->price_base_multipliers[p];
		}
	}

	/* Apply fallback prices for grf version < 8 */
	for (GRFFile * const file : _grf_files) {
		if (file->grf_version >= 8) continue;
		PriceMultipliers &price_base_multipliers = file->price_base_multipliers;
		for (Price p = PR_BEGIN; p < PR_END; p++) {
			Price fallback_price = _price_base_specs[p].fallback_price;
			if (fallback_price != INVALID_PRICE && price_base_multipliers[p] == INVALID_PRICE_MODIFIER) {
				/* No price multiplier has been set.
				 * So copy the multiplier from the fallback price, maybe a multiplier was set there. */
				price_base_multipliers[p] = price_base_multipliers[fallback_price];
			}
		}
	}

	/* Decide local/global scope of price base multipliers */
	for (GRFFile * const file : _grf_files) {
		PriceMultipliers &price_base_multipliers = file->price_base_multipliers;
		for (Price p = PR_BEGIN; p < PR_END; p++) {
			if (price_base_multipliers[p] == INVALID_PRICE_MODIFIER) {
				/* No multiplier was set; set it to a neutral value */
				price_base_multipliers[p] = 0;
			} else {
				if (!HasBit(file->grf_features, _price_base_specs[p].grf_feature)) {
					/* The grf does not define any objects of the feature,
					 * so it must be a difficulty setting. Apply it globally */
					DEBUG(grf, 3, "'%s' sets global price base multiplier %d", file->filename, p);
					SetPriceBaseMultiplier(p, price_base_multipliers[p]);
					price_base_multipliers[p] = 0;
				} else {
					DEBUG(grf, 3, "'%s' sets local price base multiplier %d", file->filename, p);
				}
			}
		}
	}
}

extern void InitGRFTownGeneratorNames();

/** Finish loading NewGRFs and execute needed post-processing */
static void AfterLoadGRFs()
{
	for (StringIDMapping &it : _string_to_grf_mapping) {
		*it.target = MapGRFStringID(it.grfid, it.source);
	}
	_string_to_grf_mapping.clear();

	/* Free the action 6 override sprites. */
	for (GRFLineToSpriteOverride::iterator it = _grf_line_to_action6_sprite_override.begin(); it != _grf_line_to_action6_sprite_override.end(); it++) {
		free((*it).second);
	}
	_grf_line_to_action6_sprite_override.clear();

	/* Polish cargoes */
	FinaliseCargoArray();

	/* Pre-calculate all refit masks after loading GRF files. */
	CalculateRefitMasks();

	/* Polish engines */
	FinaliseEngineArray();

	/* Set the actually used Canal properties */
	FinaliseCanals();

	/* Add all new houses to the house array. */
	FinaliseHouseArray();

	/* Add all new industries to the industry array. */
	FinaliseIndustriesArray();

	/* Add all new objects to the object array. */
	FinaliseObjectsArray();

	InitializeSortedCargoSpecs();

	/* Sort the list of industry types. */
	SortIndustryTypes();

	/* Create dynamic list of industry legends for smallmap_gui.cpp */
	BuildIndustriesLegend();

	/* Build the routemap legend, based on the available cargos */
	BuildLinkStatsLegend();

	/* Add all new airports to the airports array. */
	FinaliseAirportsArray();
	BindAirportSpecs();

	/* Update the townname generators list */
	InitGRFTownGeneratorNames();

	/* Run all queued vehicle list order changes */
	CommitVehicleListOrderChanges();

	/* Load old shore sprites in new position, if they were replaced by ActionA */
	ActivateOldShore();

	/* Load old tram depot sprites in new position, if no new ones are present */
	ActivateOldTramDepot();

	/* Set up custom rail types */
	InitRailTypes();
	InitRoadTypes();

	for (Engine *e : Engine::IterateType(VEH_ROAD)) {
		if (_gted[e->index].rv_max_speed != 0) {
			/* Set RV maximum speed from the mph/0.8 unit value */
			e->u.road.max_speed = _gted[e->index].rv_max_speed * 4;
		}

		RoadTramType rtt = HasBit(e->info.misc_flags, EF_ROAD_TRAM) ? RTT_TRAM : RTT_ROAD;

		const GRFFile *file = e->GetGRF();
		if (file == nullptr || _gted[e->index].roadtramtype == 0) {
			e->u.road.roadtype = (rtt == RTT_TRAM) ? ROADTYPE_TRAM : ROADTYPE_ROAD;
			continue;
		}

		/* Remove +1 offset. */
		_gted[e->index].roadtramtype--;

		const std::vector<RoadTypeLabel> *list = (rtt == RTT_TRAM) ? &file->tramtype_list : &file->roadtype_list;
		if (_gted[e->index].roadtramtype < list->size())
		{
			RoadTypeLabel rtl = (*list)[_gted[e->index].roadtramtype];
			RoadType rt = GetRoadTypeByLabel(rtl);
			if (rt != INVALID_ROADTYPE && GetRoadTramType(rt) == rtt) {
				e->u.road.roadtype = rt;
				continue;
			}
		}

		/* Road type is not available, so disable this engine */
		e->info.climates = 0;
	}

	for (Engine *e : Engine::IterateType(VEH_TRAIN)) {
		RailType railtype = GetRailTypeByLabel(_gted[e->index].railtypelabel);
		if (railtype == INVALID_RAILTYPE) {
			/* Rail type is not available, so disable this engine */
			e->info.climates = 0;
		} else {
			e->u.rail.railtype = railtype;
		}
	}

	SetYearEngineAgingStops();

	FinalisePriceBaseMultipliers();

	/* Deallocate temporary loading data */
	free(_gted);
	_grm_sprites.clear();
}

/**
 * Load all the NewGRFs.
 * @param load_index The offset for the first sprite to add.
 * @param file_index The Fio index of the first NewGRF to load.
 * @param num_baseset Number of NewGRFs at the front of the list to look up in the baseset dir instead of the newgrf dir.
 */
void LoadNewGRF(uint load_index, uint file_index, uint num_baseset)
{
	/* In case of networking we need to "sync" the start values
	 * so all NewGRFs are loaded equally. For this we use the
	 * start date of the game and we set the counters, etc. to
	 * 0 so they're the same too. */
	Date date            = _date;
	Year year            = _cur_year;
	DateFract date_fract = _date_fract;
	uint16 tick_counter  = _tick_counter;
	byte display_opt     = _display_opt;

	if (_networking) {
		_cur_year     = _settings_game.game_creation.starting_year;
		_date         = ConvertYMDToDate(_cur_year, 0, 1);
		_date_fract   = 0;
		_tick_counter = 0;
		_display_opt  = 0;
	}

	InitializeGRFSpecial();

	ResetNewGRFData();

	/*
	 * Reset the status of all files, so we can 'retry' to load them.
	 * This is needed when one for example rearranges the NewGRFs in-game
	 * and a previously disabled NewGRF becomes usable. If it would not
	 * be reset, the NewGRF would remain disabled even though it should
	 * have been enabled.
	 */
	for (GRFConfig *c = _grfconfig; c != nullptr; c = c->next) {
		if (c->status != GCS_NOT_FOUND) c->status = GCS_UNKNOWN;
	}

	_cur.spriteid = load_index;

	/* Load newgrf sprites
	 * in each loading stage, (try to) open each file specified in the config
	 * and load information from it. */
	for (GrfLoadingStage stage = GLS_LABELSCAN; stage <= GLS_ACTIVATION; stage++) {
		/* Set activated grfs back to will-be-activated between reservation- and activation-stage.
		 * This ensures that action7/9 conditions 0x06 - 0x0A work correctly. */
		for (GRFConfig *c = _grfconfig; c != nullptr; c = c->next) {
			if (c->status == GCS_ACTIVATED) c->status = GCS_INITIALISED;
		}

		if (stage == GLS_RESERVE) {
			static const uint32 overrides[][2] = {
				{ 0x44442202, 0x44440111 }, // UKRS addons modifies UKRS
				{ 0x6D620402, 0x6D620401 }, // DBSetXL ECS extension modifies DBSetXL
				{ 0x4D656f20, 0x4D656F17 }, // LV4cut modifies LV4
			};
			for (size_t i = 0; i < lengthof(overrides); i++) {
				SetNewGRFOverride(BSWAP32(overrides[i][0]), BSWAP32(overrides[i][1]));
			}
		}

		uint slot = file_index;
		uint num_non_static = 0;

		_cur.stage = stage;
		for (GRFConfig *c = _grfconfig; c != nullptr; c = c->next) {
			if (c->status == GCS_DISABLED || c->status == GCS_NOT_FOUND) continue;
			if (stage > GLS_INIT && HasBit(c->flags, GCF_INIT_ONLY)) continue;

			Subdirectory subdir = slot < file_index + num_baseset ? BASESET_DIR : NEWGRF_DIR;
			if (!FioCheckFileExists(c->filename, subdir)) {
				DEBUG(grf, 0, "NewGRF file is missing '%s'; disabling", c->filename);
				c->status = GCS_NOT_FOUND;
				continue;
			}

			if (stage == GLS_LABELSCAN) InitNewGRFFile(c);

			if (!HasBit(c->flags, GCF_STATIC) && !HasBit(c->flags, GCF_SYSTEM)) {
				if (num_non_static == NETWORK_MAX_GRF_COUNT) {
					DEBUG(grf, 0, "'%s' is not loaded as the maximum number of non-static GRFs has been reached", c->filename);
					c->status = GCS_DISABLED;
					c->error  = new GRFError(STR_NEWGRF_ERROR_MSG_FATAL, STR_NEWGRF_ERROR_TOO_MANY_NEWGRFS_LOADED);
					continue;
				}
				num_non_static++;
			}
			LoadNewGRFFile(c, slot++, stage, subdir);
			if (stage == GLS_RESERVE) {
				SetBit(c->flags, GCF_RESERVED);
			} else if (stage == GLS_ACTIVATION) {
				ClrBit(c->flags, GCF_RESERVED);
				assert(GetFileByGRFID(c->ident.grfid) == _cur.grffile);
				ClearTemporaryNewGRFData(_cur.grffile);
				BuildCargoTranslationMap();
				DEBUG(sprite, 2, "LoadNewGRF: Currently %i sprites are loaded", _cur.spriteid);
			} else if (stage == GLS_INIT && HasBit(c->flags, GCF_INIT_ONLY)) {
				/* We're not going to activate this, so free whatever data we allocated */
				ClearTemporaryNewGRFData(_cur.grffile);
			}
		}
	}

	/* Pseudo sprite processing is finished; free temporary stuff */
	_cur.ClearDataForNextFile();

	/* Call any functions that should be run after GRFs have been loaded. */
	AfterLoadGRFs();

	/* Now revert back to the original situation */
	_cur_year     = year;
	_date         = date;
	_date_fract   = date_fract;
	_tick_counter = tick_counter;
	_display_opt  = display_opt;
}
