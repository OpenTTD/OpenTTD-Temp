/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_spritegroup.h Action 2 handling. */

#ifndef NEWGRF_SPRITEGROUP_H
#define NEWGRF_SPRITEGROUP_H

#include "town_type.h"
#include "engine_type.h"
#include "house_type.h"
#include "industry_type.h"

#include "newgrf_callbacks.h"
#include "newgrf_generic.h"
#include "newgrf_storage.h"
#include "newgrf_commons.h"

/**
 * Gets the value of a so-called newgrf "register".
 * @param i index of the register
 * @pre i < 0x110
 * @return the value of the register
 */
static inline uint32 GetRegister(uint i)
{
	extern TemporaryStorageArray<int32, 0x110> _temp_store;
	return _temp_store.GetValue(i);
}

/* List of different sprite group types */
enum SpriteGroupType {
	SGT_REAL,
	SGT_DETERMINISTIC,
	SGT_RANDOMIZED,
	SGT_CALLBACK,
	SGT_RESULT,
	SGT_TILELAYOUT,
	SGT_INDUSTRY_PRODUCTION,
};

struct SpriteGroup;
typedef uint32 SpriteGroupID;
struct ResolverObject;

/* SPRITE_WIDTH is 24. ECS has roughly 30 sprite groups per real sprite.
 * Adding an 'extra' margin would be assuming 64 sprite groups per real
 * sprite. 64 = 2^6, so 2^30 should be enough (for now) */
typedef Pool<SpriteGroup, SpriteGroupID, 1024, 1 << 30, PT_DATA> SpriteGroupPool;
extern SpriteGroupPool _spritegroup_pool;

/* Common wrapper for all the different sprite group types */
struct SpriteGroup : SpriteGroupPool::PoolItem<&_spritegroup_pool> {
protected:
	SpriteGroup(SpriteGroupType type) : nfo_line(0), type(type), has_cb_result(false) {}
	/** Base sprite group resolver */
	virtual const SpriteGroup *Resolve(ResolverObject &object) const { return this; };

public:
	virtual ~SpriteGroup() {}

	uint32 nfo_line;
	SpriteGroupType type;
	bool has_cb_result;

	virtual SpriteID GetResult() const { return 0; }
	virtual byte GetNumResults() const { return 0; }
	virtual uint16 GetCallbackResult() const { return CALLBACK_FAILED; }

	static const SpriteGroup *Resolve(const SpriteGroup *group, ResolverObject &object, bool top_level = true);
};


/* 'Real' sprite groups contain a list of other result or callback sprite
 * groups. */
struct RealSpriteGroup : SpriteGroup {
	RealSpriteGroup() : SpriteGroup(SGT_REAL) {}

	/* Loaded = in motion, loading = not moving
	 * Each group contains several spritesets, for various loading stages */

	/* XXX: For stations the meaning is different - loaded is for stations
	 * with small amount of cargo whilst loading is for stations with a lot
	 * of da stuff. */

	std::vector<const SpriteGroup *> loaded;  ///< List of loaded groups (can be SpriteIDs or Callback results)
	std::vector<const SpriteGroup *> loading; ///< List of loading groups (can be SpriteIDs or Callback results)

protected:
	const SpriteGroup *Resolve(ResolverObject &object) const;
};

/* Shared by deterministic and random groups. */
enum VarSpriteGroupScope {
	VSG_BEGIN,

	VSG_SCOPE_SELF = VSG_BEGIN, ///< Resolved object itself
	VSG_SCOPE_PARENT,           ///< Related object of the resolved one
	VSG_SCOPE_RELATIVE,         ///< Relative position (vehicles only)

	VSG_END
};
DECLARE_POSTFIX_INCREMENT(VarSpriteGroupScope)

enum DeterministicSpriteGroupSize {
	DSG_SIZE_BYTE,
	DSG_SIZE_WORD,
	DSG_SIZE_DWORD,
};

enum DeterministicSpriteGroupAdjustType {
	DSGA_TYPE_NONE,
	DSGA_TYPE_DIV,
	DSGA_TYPE_MOD,
};

enum DeterministicSpriteGroupAdjustOperation {
	DSGA_OP_ADD,  ///< a + b
	DSGA_OP_SUB,  ///< a - b
	DSGA_OP_SMIN, ///< (signed) min(a, b)
	DSGA_OP_SMAX, ///< (signed) max(a, b)
	DSGA_OP_UMIN, ///< (unsigned) min(a, b)
	DSGA_OP_UMAX, ///< (unsigned) max(a, b)
	DSGA_OP_SDIV, ///< (signed) a / b
	DSGA_OP_SMOD, ///< (signed) a % b
	DSGA_OP_UDIV, ///< (unsigned) a / b
	DSGA_OP_UMOD, ///< (unsigned) a & b
	DSGA_OP_MUL,  ///< a * b
	DSGA_OP_AND,  ///< a & b
	DSGA_OP_OR,   ///< a | b
	DSGA_OP_XOR,  ///< a ^ b
	DSGA_OP_STO,  ///< store a into temporary storage, indexed by b. return a
	DSGA_OP_RST,  ///< return b
	DSGA_OP_STOP, ///< store a into persistent storage, indexed by b, return a
	DSGA_OP_ROR,  ///< rotate a b positions to the right
	DSGA_OP_SCMP, ///< (signed) comparison (a < b -> 0, a == b = 1, a > b = 2)
	DSGA_OP_UCMP, ///< (unsigned) comparison (a < b -> 0, a == b = 1, a > b = 2)
	DSGA_OP_SHL,  ///< a << b
	DSGA_OP_SHR,  ///< (unsigned) a >> b
	DSGA_OP_SAR,  ///< (signed) a >> b
};


struct DeterministicSpriteGroupAdjust {
	DeterministicSpriteGroupAdjustOperation operation;
	DeterministicSpriteGroupAdjustType type;
	byte variable;
	byte parameter; ///< Used for variables between 0x60 and 0x7F inclusive.
	byte shift_num;
	uint32 and_mask;
	uint32 add_val;
	uint32 divmod_val;
	const SpriteGroup *subroutine;
};


struct DeterministicSpriteGroupRange {
	const SpriteGroup *group;
	uint32 low;
	uint32 high;
};


struct DeterministicSpriteGroup : SpriteGroup {
	DeterministicSpriteGroup() : SpriteGroup(SGT_DETERMINISTIC) {}

	VarSpriteGroupScope var_scope;
	DeterministicSpriteGroupSize size;
	bool calculated_result;
	std::vector<DeterministicSpriteGroupAdjust> adjusts;
	std::vector<DeterministicSpriteGroupRange> ranges; // Dynamically allocated

	/* Dynamically allocated, this is the sole owner */
	const SpriteGroup *default_group;

	const SpriteGroup *error_group; // was first range, before sorting ranges

protected:
	const SpriteGroup *Resolve(ResolverObject &object) const;
};

enum RandomizedSpriteGroupCompareMode {
	RSG_CMP_ANY,
	RSG_CMP_ALL,
};

struct RandomizedSpriteGroup : SpriteGroup {
	RandomizedSpriteGroup() : SpriteGroup(SGT_RANDOMIZED) {}

	VarSpriteGroupScope var_scope;  ///< Take this object:

	RandomizedSpriteGroupCompareMode cmp_mode; ///< Check for these triggers:
	byte triggers;
	byte count;

	byte lowest_randbit; ///< Look for this in the per-object randomized bitmask:

	std::vector<const SpriteGroup *> groups; ///< Take the group with appropriate index:

protected:
	const SpriteGroup *Resolve(ResolverObject &object) const;
};


/* This contains a callback result. A failed callback has a value of
 * CALLBACK_FAILED */
struct CallbackResultSpriteGroup : SpriteGroup {
	/**
	 * Creates a spritegroup representing a callback result
	 * @param value The value that was used to represent this callback result
	 * @param grf_version8 True, if we are dealing with a new NewGRF which uses GRF version >= 8.
	 */
	CallbackResultSpriteGroup(uint16 value, bool grf_version8) :
		SpriteGroup(SGT_CALLBACK),
		result(value)
	{
		/* Old style callback results (only valid for version < 8) have the highest byte 0xFF so signify it is a callback result.
		 * New style ones only have the highest bit set (allows 15-bit results, instead of just 8) */
		if (!grf_version8 && (this->result >> 8) == 0xFF) {
			this->result &= ~0xFF00;
		} else {
			this->result &= ~0x8000;
		}

		has_cb_result = true;
	}

	uint16 result;
	uint16 GetCallbackResult() const { return this->result; }
};


/* A result sprite group returns the first SpriteID and the number of
 * sprites in the set */
struct ResultSpriteGroup : SpriteGroup {
	/**
	 * Creates a spritegroup representing a sprite number result.
	 * @param sprite The sprite number.
	 * @param num_sprites The number of sprites per set.
	 * @return A spritegroup representing the sprite number result.
	 */
	ResultSpriteGroup(SpriteID sprite, byte num_sprites) :
		SpriteGroup(SGT_RESULT),
		sprite(sprite),
		num_sprites(num_sprites)
	{
	}

	SpriteID sprite;
	byte num_sprites;
	SpriteID GetResult() const { return this->sprite; }
	byte GetNumResults() const { return this->num_sprites; }
};

/**
 * Action 2 sprite layout for houses, industry tiles, objects and airport tiles.
 */
struct TileLayoutSpriteGroup : SpriteGroup {
	TileLayoutSpriteGroup() : SpriteGroup(SGT_TILELAYOUT) {}
	~TileLayoutSpriteGroup() {}

	NewGRFSpriteLayout dts;

	const DrawTileSprites *ProcessRegisters(uint8 *stage) const;
};

struct IndustryProductionSpriteGroup : SpriteGroup {
	IndustryProductionSpriteGroup() : SpriteGroup(SGT_INDUSTRY_PRODUCTION) {}

	uint8 version;                              ///< Production callback version used, or 0xFF if marked invalid
	uint8 num_input;                            ///< How many subtract_input values are valid
	int16 subtract_input[INDUSTRY_NUM_INPUTS];  ///< Take this much of the input cargo (can be negative, is indirect in cb version 1+)
	CargoID cargo_input[INDUSTRY_NUM_INPUTS];   ///< Which input cargoes to take from (only cb version 2)
	uint8 num_output;                           ///< How many add_output values are valid
	uint16 add_output[INDUSTRY_NUM_OUTPUTS];    ///< Add this much output cargo when successful (unsigned, is indirect in cb version 1+)
	CargoID cargo_output[INDUSTRY_NUM_OUTPUTS]; ///< Which output cargoes to add to (only cb version 2)
	uint8 again;

};

/**
 * Interface to query and set values specific to a single #VarSpriteGroupScope (action 2 scope).
 *
 * Multiple of these interfaces are combined into a #ResolverObject to allow access
 * to different game entities from a #SpriteGroup-chain (action 1-2-3 chain).
 */
struct ScopeResolver {
	ResolverObject &ro; ///< Surrounding resolver object.

	ScopeResolver(ResolverObject &ro) : ro(ro) {}
	virtual ~ScopeResolver() {}

	virtual uint32 GetRandomBits() const;
	virtual uint32 GetTriggers() const;

	virtual uint32 GetVariable(byte variable, uint32 parameter, bool *available) const;
	virtual void StorePSA(uint reg, int32 value);
};

/**
 * Interface for #SpriteGroup-s to access the gamestate.
 *
 * Using this interface #SpriteGroup-chains (action 1-2-3 chains) can be resolved,
 * to get the results of callbacks, rerandomisations or normal sprite lookups.
 */
struct ResolverObject {
	/**
	 * Resolver constructor.
	 * @param grffile NewGRF file associated with the object (or \c nullptr if none).
	 * @param callback Callback code being resolved (default value is #CBID_NO_CALLBACK).
	 * @param callback_param1 First parameter (var 10) of the callback (only used when \a callback is also set).
	 * @param callback_param2 Second parameter (var 18) of the callback (only used when \a callback is also set).
	 */
	ResolverObject(const GRFFile *grffile, CallbackID callback = CBID_NO_CALLBACK, uint32 callback_param1 = 0, uint32 callback_param2 = 0)
		: default_scope(*this), callback(callback), callback_param1(callback_param1), callback_param2(callback_param2), grffile(grffile), root_spritegroup(nullptr)
	{
		this->ResetState();
	}

	virtual ~ResolverObject() {}

	ScopeResolver default_scope; ///< Default implementation of the grf scope.

	CallbackID callback;        ///< Callback being resolved.
	uint32 callback_param1;     ///< First parameter (var 10) of the callback.
	uint32 callback_param2;     ///< Second parameter (var 18) of the callback.

	uint32 last_value;          ///< Result of most recent DeterministicSpriteGroup (including procedure calls)

	uint32 waiting_triggers;    ///< Waiting triggers to be used by any rerandomisation. (scope independent)
	uint32 used_triggers;       ///< Subset of cur_triggers, which actually triggered some rerandomisation. (scope independent)
	uint32 reseed[VSG_END];     ///< Collects bits to rerandomise while triggering triggers.

	const GRFFile *grffile;     ///< GRFFile the resolved SpriteGroup belongs to
	const SpriteGroup *root_spritegroup; ///< Root SpriteGroup to use for resolving

	/**
	 * Resolve SpriteGroup.
	 * @return Result spritegroup.
	 */
	const SpriteGroup *Resolve()
	{
		return SpriteGroup::Resolve(this->root_spritegroup, *this);
	}

	/**
	 * Resolve callback.
	 * @return Callback result.
	 */
	uint16 ResolveCallback()
	{
		const SpriteGroup *result = Resolve();
		return result != nullptr ? result->GetCallbackResult() : CALLBACK_FAILED;
	}

	virtual const SpriteGroup *ResolveReal(const RealSpriteGroup *group) const;

	virtual ScopeResolver *GetScope(VarSpriteGroupScope scope = VSG_SCOPE_SELF, byte relative = 0);

	/**
	 * Returns the waiting triggers that did not trigger any rerandomisation.
	 */
	uint32 GetRemainingTriggers() const
	{
		return this->waiting_triggers & ~this->used_triggers;
	}

	/**
	 * Returns the OR-sum of all bits that need reseeding
	 * independent of the scope they were accessed with.
	 * @return OR-sum of the bits.
	 */
	uint32 GetReseedSum() const
	{
		uint32 sum = 0;
		for (VarSpriteGroupScope vsg = VSG_BEGIN; vsg < VSG_END; vsg++) {
			sum |= this->reseed[vsg];
		}
		return sum;
	}

	/**
	 * Resets the dynamic state of the resolver object.
	 * To be called before resolving an Action-1-2-3 chain.
	 */
	void ResetState()
	{
		this->last_value = 0;
		this->waiting_triggers = 0;
		this->used_triggers = 0;
		memset(this->reseed, 0, sizeof(this->reseed));
	}

	/**
	 * Get the feature number being resolved for.
	 * This function is mainly intended for the callback profiling feature.
	 */
	virtual GrfSpecFeature GetFeature() const { return GSF_INVALID; }
	/**
	 * Get an identifier for the item being resolved.
	 * This function is mainly intended for the callback profiling feature,
	 * and should return an identifier recognisable by the NewGRF developer.
	 */
	virtual uint32 GetDebugID() const { return 0; }
};

#endif /* NEWGRF_SPRITEGROUP_H */
