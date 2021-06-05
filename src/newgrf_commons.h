/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file newgrf_commons.h This file simplyfies and embeds a common mechanism of
 * loading/saving and mapping of grf entities.
 */

#ifndef NEWGRF_COMMONS_H
#define NEWGRF_COMMONS_H

#include "sprite.h"
#include "core/alloc_type.hpp"
#include "core/smallvec_type.hpp"
#include "command_type.h"
#include "direction_type.h"
#include "company_type.h"

/** Context for tile accesses */
enum TileContext {
	TCX_NORMAL,         ///< Nothing special.
	TCX_UPPER_HALFTILE, ///< Querying information about the upper part of a tile with halftile foundation.
	TCX_ON_BRIDGE,      ///< Querying information about stuff on the bridge (via some bridgehead).
};

/**
 * Flags to enable register usage in sprite layouts.
 */
enum TileLayoutFlags {
	TLF_NOTHING           = 0x00,

	TLF_DODRAW            = 0x01,   ///< Only draw sprite if value of register TileLayoutRegisters::dodraw is non-zero.
	TLF_SPRITE            = 0x02,   ///< Add signed offset to sprite from register TileLayoutRegisters::sprite.
	TLF_PALETTE           = 0x04,   ///< Add signed offset to palette from register TileLayoutRegisters::palette.
	TLF_CUSTOM_PALETTE    = 0x08,   ///< Palette is from Action 1 (moved to SPRITE_MODIFIER_CUSTOM_SPRITE in palette during loading).

	TLF_BB_XY_OFFSET      = 0x10,   ///< Add signed offset to bounding box X and Y positions from register TileLayoutRegisters::delta.parent[0..1].
	TLF_BB_Z_OFFSET       = 0x20,   ///< Add signed offset to bounding box Z positions from register TileLayoutRegisters::delta.parent[2].

	TLF_CHILD_X_OFFSET    = 0x10,   ///< Add signed offset to child sprite X positions from register TileLayoutRegisters::delta.child[0].
	TLF_CHILD_Y_OFFSET    = 0x20,   ///< Add signed offset to child sprite Y positions from register TileLayoutRegisters::delta.child[1].

	TLF_SPRITE_VAR10      = 0x40,   ///< Resolve sprite with a specific value in variable 10.
	TLF_PALETTE_VAR10     = 0x80,   ///< Resolve palette with a specific value in variable 10.

	TLF_KNOWN_FLAGS       = 0xFF,   ///< Known flags. Any unknown set flag will disable the GRF.

	/** Flags which are still required after loading the GRF. */
	TLF_DRAWING_FLAGS     = ~TLF_CUSTOM_PALETTE,

	/** Flags which do not work for the (first) ground sprite. */
	TLF_NON_GROUND_FLAGS  = TLF_BB_XY_OFFSET | TLF_BB_Z_OFFSET | TLF_CHILD_X_OFFSET | TLF_CHILD_Y_OFFSET,

	/** Flags which refer to using multiple action-1-2-3 chains. */
	TLF_VAR10_FLAGS       = TLF_SPRITE_VAR10 | TLF_PALETTE_VAR10,

	/** Flags which require resolving the action-1-2-3 chain for the sprite, even if it is no action-1 sprite. */
	TLF_SPRITE_REG_FLAGS  = TLF_DODRAW | TLF_SPRITE | TLF_BB_XY_OFFSET | TLF_BB_Z_OFFSET | TLF_CHILD_X_OFFSET | TLF_CHILD_Y_OFFSET,

	/** Flags which require resolving the action-1-2-3 chain for the palette, even if it is no action-1 palette. */
	TLF_PALETTE_REG_FLAGS = TLF_PALETTE,
};
DECLARE_ENUM_AS_BIT_SET(TileLayoutFlags)

/**
 * Determines which sprite to use from a spriteset for a specific construction stage.
 * @param construction_stage Construction stage 0 - 3.
 * @param num_sprites Number of available sprites to select stage from.
 * @return Sprite to use
 */
static inline uint GetConstructionStageOffset(uint construction_stage, uint num_sprites)
{
	assert(num_sprites > 0);
	if (num_sprites > 4) num_sprites = 4;
	switch (construction_stage) {
		case 0: return 0;
		case 1: return num_sprites > 2 ? 1 : 0;
		case 2: return num_sprites > 2 ? num_sprites - 2 : 0;
		case 3: return num_sprites - 1;
		default: NOT_REACHED();
	}
}

/**
 * Additional modifiers for items in sprite layouts.
 */
struct TileLayoutRegisters {
	TileLayoutFlags flags; ///< Flags defining which members are valid and to be used.
	uint8 dodraw;          ///< Register deciding whether the sprite shall be drawn at all. Non-zero means drawing.
	uint8 sprite;          ///< Register specifying a signed offset for the sprite.
	uint8 palette;         ///< Register specifying a signed offset for the palette.
	uint16 max_sprite_offset;  ///< Maximum offset to add to the sprite. (limited by size of the spriteset)
	uint16 max_palette_offset; ///< Maximum offset to add to the palette. (limited by size of the spriteset)
	union {
		uint8 parent[3];   ///< Registers for signed offsets for the bounding box position of parent sprites.
		uint8 child[2];    ///< Registers for signed offsets for the position of child sprites.
	} delta;
	uint8 sprite_var10;    ///< Value for variable 10 when resolving the sprite.
	uint8 palette_var10;   ///< Value for variable 10 when resolving the palette.
};

static const uint TLR_MAX_VAR10 = 7; ///< Maximum value for var 10.

/**
 * NewGRF supplied spritelayout.
 * In contrast to #DrawTileSprites this struct is for allocated
 * layouts on the heap. It allocates data and frees them on destruction.
 */
struct NewGRFSpriteLayout : ZeroedMemoryAllocator, DrawTileSprites {
	const TileLayoutRegisters *registers;

	/**
	 * Number of sprites in all referenced spritesets.
	 * If these numbers are inconsistent, then this is 0 and the real values are in \c registers.
	 */
	uint consistent_max_offset;

	void Allocate(uint num_sprites);
	void AllocateRegisters();
	void Clone(const DrawTileSeqStruct *source);
	void Clone(const NewGRFSpriteLayout *source);

	/**
	 * Clone a spritelayout.
	 * @param source The spritelayout to copy.
	 */
	void Clone(const DrawTileSprites *source)
	{
		assert(source != nullptr && this != source);
		this->ground = source->ground;
		this->Clone(source->seq);
	}

	virtual ~NewGRFSpriteLayout()
	{
		free(this->seq);
		free(this->registers);
	}

	/**
	 * Tests whether this spritelayout needs preprocessing by
	 * #PrepareLayout() and #ProcessRegisters(), or whether it can be
	 * used directly.
	 * @return true if preprocessing is needed
	 */
	bool NeedsPreprocessing() const
	{
		return this->registers != nullptr;
	}

	uint32 PrepareLayout(uint32 orig_offset, uint32 newgrf_ground_offset, uint32 newgrf_offset, uint constr_stage, bool separate_ground) const;
	void ProcessRegisters(uint8 resolved_var10, uint32 resolved_sprite, bool separate_ground) const;

	/**
	 * Returns the result spritelayout after preprocessing.
	 * @pre #PrepareLayout() and #ProcessRegisters() need calling first.
	 * @return result spritelayout
	 */
	const DrawTileSeqStruct *GetLayout(PalSpriteID *ground) const
	{
		DrawTileSeqStruct *front = result_seq.data();
		*ground = front->image;
		return front + 1;
	}

private:
	static std::vector<DrawTileSeqStruct> result_seq; ///< Temporary storage when preprocessing spritelayouts.
};

/**
 * Maps an entity id stored on the map to a GRF file.
 * Entities are objects used ingame (houses, industries, industry tiles) for
 * which we need to correlate the ids from the grf files with the ones in the
 * the savegames themselves.
 * An array of EntityIDMapping structs is saved with the savegame so
 * that those GRFs can be loaded in a different order, or removed safely. The
 * index in the array is the entity's ID stored on the map.
 *
 * The substitute ID is the ID of an original entity that should be used instead
 * if the GRF containing the new entity is not available.
 */
struct EntityIDMapping {
	uint32 grfid;          ///< The GRF ID of the file the entity belongs to
	uint8  entity_id;      ///< The entity ID within the GRF file
	uint8  substitute_id;  ///< The (original) entity ID to use if this GRF is not available
};

class OverrideManagerBase {
protected:
	uint16 *entity_overrides;
	uint32 *grfid_overrides;

	uint16 max_offset;       ///< what is the length of the original entity's array of specs
	uint16 max_new_entities; ///< what is the amount of entities, old and new summed

	uint16 invalid_ID;       ///< ID used to detected invalid entities;
	virtual bool CheckValidNewID(uint16 testid) { return true; }

public:
	EntityIDMapping *mapping_ID; ///< mapping of ids from grf files.  Public out of convenience

	OverrideManagerBase(uint16 offset, uint16 maximum, uint16 invalid);
	virtual ~OverrideManagerBase();

	void ResetOverride();
	void ResetMapping();

	void Add(uint8 local_id, uint32 grfid, uint entity_type);
	virtual uint16 AddEntityID(byte grf_local_id, uint32 grfid, byte substitute_id);

	uint32 GetGRFID(uint16 entity_id) const;
	uint16 GetSubstituteID(uint16 entity_id) const;
	virtual uint16 GetID(uint8 grf_local_id, uint32 grfid) const;

	inline uint16 GetMaxMapping() const { return max_new_entities; }
	inline uint16 GetMaxOffset() const { return max_offset; }
};


struct HouseSpec;
class HouseOverrideManager : public OverrideManagerBase {
public:
	HouseOverrideManager(uint16 offset, uint16 maximum, uint16 invalid) :
			OverrideManagerBase(offset, maximum, invalid) {}

	void SetEntitySpec(const HouseSpec *hs);
};


struct IndustrySpec;
class IndustryOverrideManager : public OverrideManagerBase {
public:
	IndustryOverrideManager(uint16 offset, uint16 maximum, uint16 invalid) :
			OverrideManagerBase(offset, maximum, invalid) {}

	uint16 AddEntityID(byte grf_local_id, uint32 grfid, byte substitute_id) override;
	uint16 GetID(uint8 grf_local_id, uint32 grfid) const override;

	void SetEntitySpec(IndustrySpec *inds);
};


struct IndustryTileSpec;
class IndustryTileOverrideManager : public OverrideManagerBase {
protected:
	virtual bool CheckValidNewID(uint16 testid) { return testid != 0xFF; }
public:
	IndustryTileOverrideManager(uint16 offset, uint16 maximum, uint16 invalid) :
			OverrideManagerBase(offset, maximum, invalid) {}

	void SetEntitySpec(const IndustryTileSpec *indts);
};

struct AirportSpec;
class AirportOverrideManager : public OverrideManagerBase {
public:
	AirportOverrideManager(uint16 offset, uint16 maximum, uint16 invalid) :
			OverrideManagerBase(offset, maximum, invalid) {}

	void SetEntitySpec(AirportSpec *inds);
};

struct AirportTileSpec;
class AirportTileOverrideManager : public OverrideManagerBase {
protected:
	virtual bool CheckValidNewID(uint16 testid) { return testid != 0xFF; }
public:
	AirportTileOverrideManager(uint16 offset, uint16 maximum, uint16 invalid) :
			OverrideManagerBase(offset, maximum, invalid) {}

	void SetEntitySpec(const AirportTileSpec *ats);
};

struct ObjectSpec;
class ObjectOverrideManager : public OverrideManagerBase {
protected:
	virtual bool CheckValidNewID(uint16 testid) { return testid != 0xFF; }
public:
	ObjectOverrideManager(uint16 offset, uint16 maximum, uint16 invalid) :
			OverrideManagerBase(offset, maximum, invalid) {}

	void SetEntitySpec(ObjectSpec *spec);
};

struct BridgeSpec;
class BridgeOverrideManager : public OverrideManagerBase {
protected:
	virtual bool CheckValidNewID(uint16 testid) { return testid != 0xFF; }
public:
	BridgeOverrideManager(uint16 offset, uint16 maximum, uint16 invalid) :
		OverrideManagerBase(offset, maximum, invalid) {}

	void SetEntitySpec(BridgeSpec *spec);
};

extern HouseOverrideManager _house_mngr;
extern IndustryOverrideManager _industry_mngr;
extern IndustryTileOverrideManager _industile_mngr;
extern AirportOverrideManager _airport_mngr;
extern AirportTileOverrideManager _airporttile_mngr;
extern ObjectOverrideManager _object_mngr;
extern BridgeOverrideManager _bridge_mngr;

uint32 GetTerrainType(TileIndex tile, TileContext context = TCX_NORMAL);
TileIndex GetNearbyTile(byte parameter, TileIndex tile, bool signed_offsets = true, Axis axis = INVALID_AXIS);
uint32 GetNearbyTileInformation(TileIndex tile, bool grf_version8);
uint32 GetCompanyInfo(CompanyID owner, const struct Livery *l = nullptr);
CommandCost GetErrorMessageFromLocationCallbackResult(uint16 cb_res, const GRFFile *grffile, StringID default_error);

void ErrorUnknownCallbackResult(uint32 grfid, uint16 cbid, uint16 cb_res);
bool ConvertBooleanCallback(const struct GRFFile *grffile, uint16 cbid, uint16 cb_res);
bool Convert8bitBooleanCallback(const struct GRFFile *grffile, uint16 cbid, uint16 cb_res);

/**
 * Data related to the handling of grf files.
 * @tparam Tcnt Number of spritegroups
 */
template <size_t Tcnt>
struct GRFFilePropsBase {
	GRFFilePropsBase() : local_id(0), grffile(0)
	{
		/* The lack of some compilers to provide default constructors complying to the specs
		 * requires us to zero the stuff ourself. */
		memset(spritegroup, 0, sizeof(spritegroup));
	}

	uint16 local_id;                             ///< id defined by the grf file for this entity
	const struct GRFFile *grffile;               ///< grf file that introduced this entity
	const struct SpriteGroup *spritegroup[Tcnt]; ///< pointer to the different sprites of the entity
};

/** Data related to the handling of grf files. */
struct GRFFileProps : GRFFilePropsBase<1> {
	/** Set all default data constructor for the props. */
	GRFFileProps(uint16 subst_id = 0) :
			GRFFilePropsBase<1>(), subst_id(subst_id), override(subst_id)
	{
	}

	uint16 subst_id;
	uint16 override;                      ///< id of the entity been replaced by
};

#endif /* NEWGRF_COMMONS_H */
