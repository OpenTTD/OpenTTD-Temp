/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file newgrf_commons.cpp Implementation of the class %OverrideManagerBase
 * and its descendance, present and future.
 */

#include "stdafx.h"
#include "debug.h"
#include "landscape.h"
#include "house.h"
#include "industrytype.h"
#include "newgrf_config.h"
#include "clear_map.h"
#include "station_map.h"
#include "tree_map.h"
#include "newgrf_bridge.h"
#include "tunnelbridge_map.h"
#include "newgrf_object.h"
#include "genworld.h"
#include "newgrf_spritegroup.h"
#include "newgrf_text.h"
#include "company_base.h"
#include "error.h"
#include "strings_func.h"

#include "table/strings.h"

#include "safeguards.h"

/**
 * Constructor of generic class
 * @param offset end of original data for this entity. i.e: houses = 110
 * @param maximum of entities this manager can deal with. i.e: houses = 512
 * @param invalid is the ID used to identify an invalid entity id
 */
OverrideManagerBase::OverrideManagerBase(uint16 offset, uint16 maximum, uint16 invalid)
{
	max_offset = offset;
	max_new_entities = maximum;
	invalid_ID = invalid;

	mapping_ID = CallocT<EntityIDMapping>(max_new_entities);
	entity_overrides = MallocT<uint16>(max_offset);
	for (size_t i = 0; i < max_offset; i++) entity_overrides[i] = invalid;
	grfid_overrides = CallocT<uint32>(max_offset);
}

/**
 * Destructor of the generic class.
 * Frees allocated memory of constructor
 */
OverrideManagerBase::~OverrideManagerBase()
{
	free(mapping_ID);
	free(entity_overrides);
	free(grfid_overrides);
}

/**
 * Since the entity IDs defined by the GRF file does not necessarily correlate
 * to those used by the game, the IDs used for overriding old entities must be
 * translated when the entity spec is set.
 * @param local_id ID in grf file
 * @param grfid  ID of the grf file
 * @param entity_type original entity type
 */
void OverrideManagerBase::Add(uint8 local_id, uint32 grfid, uint entity_type)
{
	assert(entity_type < max_offset);
	/* An override can be set only once */
	if (entity_overrides[entity_type] != invalid_ID) return;
	entity_overrides[entity_type] = local_id;
	grfid_overrides[entity_type] = grfid;
}

/** Resets the mapping, which is used while initializing game */
void OverrideManagerBase::ResetMapping()
{
	memset(mapping_ID, 0, (max_new_entities - 1) * sizeof(EntityIDMapping));
}

/** Resets the override, which is used while initializing game */
void OverrideManagerBase::ResetOverride()
{
	for (uint16 i = 0; i < max_offset; i++) {
		entity_overrides[i] = invalid_ID;
		grfid_overrides[i] = 0;
	}
}

/**
 * Return the ID (if ever available) of a previously inserted entity.
 * @param grf_local_id ID of this entity within the grfID
 * @param grfid ID of the grf file
 * @return the ID of the candidate, of the Invalid flag item ID
 */
uint16 OverrideManagerBase::GetID(uint8 grf_local_id, uint32 grfid) const
{
	const EntityIDMapping *map;

	for (uint16 id = 0; id < max_new_entities; id++) {
		map = &mapping_ID[id];
		if (map->entity_id == grf_local_id && map->grfid == grfid) {
			return id;
		}
	}

	return invalid_ID;
}

/**
 * Reserves a place in the mapping array for an entity to be installed
 * @param grf_local_id is an arbitrary id given by the grf's author.  Also known as setid
 * @param grfid is the id of the grf file itself
 * @param substitute_id is the original entity from which data is copied for the new one
 * @return the proper usable slot id, or invalid marker if none is found
 */
uint16 OverrideManagerBase::AddEntityID(byte grf_local_id, uint32 grfid, byte substitute_id)
{
	uint16 id = this->GetID(grf_local_id, grfid);
	EntityIDMapping *map;

	/* Look to see if this entity has already been added. This is done
	 * separately from the loop below in case a GRF has been deleted, and there
	 * are any gaps in the array.
	 */
	if (id != invalid_ID) {
		return id;
	}

	/* This entity hasn't been defined before, so give it an ID now. */
	for (id = max_offset; id < max_new_entities; id++) {
		map = &mapping_ID[id];

		if (CheckValidNewID(id) && map->entity_id == 0 && map->grfid == 0) {
			map->entity_id     = grf_local_id;
			map->grfid         = grfid;
			map->substitute_id = substitute_id;
			return id;
		}
	}

	return invalid_ID;
}

/**
 * Gives the GRFID of the file the entity belongs to.
 * @param entity_id ID of the entity being queried.
 * @return GRFID.
 */
uint32 OverrideManagerBase::GetGRFID(uint16 entity_id) const
{
	return mapping_ID[entity_id].grfid;
}

/**
 * Gives the substitute of the entity, as specified by the grf file
 * @param entity_id of the entity being queried
 * @return mapped id
 */
uint16 OverrideManagerBase::GetSubstituteID(uint16 entity_id) const
{
	return mapping_ID[entity_id].substitute_id;
}

/**
 * Install the specs into the HouseSpecs array
 * It will find itself the proper slot on which it will go
 * @param hs HouseSpec read from the grf file, ready for inclusion
 */
void HouseOverrideManager::SetEntitySpec(const HouseSpec *hs)
{
	HouseID house_id = this->AddEntityID(hs->grf_prop.local_id, hs->grf_prop.grffile->grfid, hs->grf_prop.subst_id);

	if (house_id == invalid_ID) {
		grfmsg(1, "House.SetEntitySpec: Too many houses allocated. Ignoring.");
		return;
	}

	MemCpyT(HouseSpec::Get(house_id), hs);

	/* Now add the overrides. */
	for (int i = 0; i != max_offset; i++) {
		HouseSpec *overridden_hs = HouseSpec::Get(i);

		if (entity_overrides[i] != hs->grf_prop.local_id || grfid_overrides[i] != hs->grf_prop.grffile->grfid) continue;

		overridden_hs->grf_prop.override = house_id;
		entity_overrides[i] = invalid_ID;
		grfid_overrides[i] = 0;
	}
}

/**
 * Return the ID (if ever available) of a previously inserted entity.
 * @param grf_local_id ID of this entity within the grfID
 * @param grfid ID of the grf file
 * @return the ID of the candidate, of the Invalid flag item ID
 */
uint16 IndustryOverrideManager::GetID(uint8 grf_local_id, uint32 grfid) const
{
	uint16 id = OverrideManagerBase::GetID(grf_local_id, grfid);
	if (id != invalid_ID) return id;

	/* No mapping found, try the overrides */
	for (id = 0; id < max_offset; id++) {
		if (entity_overrides[id] == grf_local_id && grfid_overrides[id] == grfid) return id;
	}

	return invalid_ID;
}

/**
 * Method to find an entity ID and to mark it as reserved for the Industry to be included.
 * @param grf_local_id ID used by the grf file for pre-installation work (equivalent of TTDPatch's setid
 * @param grfid ID of the current grf file
 * @param substitute_id industry from which data has been copied
 * @return a free entity id (slotid) if ever one has been found, or Invalid_ID marker otherwise
 */
uint16 IndustryOverrideManager::AddEntityID(byte grf_local_id, uint32 grfid, byte substitute_id)
{
	/* This entity hasn't been defined before, so give it an ID now. */
	for (uint16 id = 0; id < max_new_entities; id++) {
		/* Skip overridden industries */
		if (id < max_offset && entity_overrides[id] != invalid_ID) continue;

		/* Get the real live industry */
		const IndustrySpec *inds = GetIndustrySpec(id);

		/* This industry must be one that is not available(enabled), mostly because of climate.
		 * And it must not already be used by a grf (grffile == nullptr).
		 * So reserve this slot here, as it is the chosen one */
		if (!inds->enabled && inds->grf_prop.grffile == nullptr) {
			EntityIDMapping *map = &mapping_ID[id];

			if (map->entity_id == 0 && map->grfid == 0) {
				/* winning slot, mark it as been used */
				map->entity_id     = grf_local_id;
				map->grfid         = grfid;
				map->substitute_id = substitute_id;
				return id;
			}
		}
	}

	return invalid_ID;
}

/**
 * Method to install the new industry data in its proper slot
 * The slot assignment is internal of this method, since it requires
 * checking what is available
 * @param inds Industryspec that comes from the grf decoding process
 */
void IndustryOverrideManager::SetEntitySpec(IndustrySpec *inds)
{
	/* First step : We need to find if this industry is already specified in the savegame data. */
	IndustryType ind_id = this->GetID(inds->grf_prop.local_id, inds->grf_prop.grffile->grfid);

	if (ind_id == invalid_ID) {
		/* Not found.
		 * Or it has already been overridden, so you've lost your place.
		 * Or it is a simple substitute.
		 * We need to find a free available slot */
		ind_id = this->AddEntityID(inds->grf_prop.local_id, inds->grf_prop.grffile->grfid, inds->grf_prop.subst_id);
		inds->grf_prop.override = invalid_ID;  // make sure it will not be detected as overridden
	}

	if (ind_id == invalid_ID) {
		grfmsg(1, "Industry.SetEntitySpec: Too many industries allocated. Ignoring.");
		return;
	}

	/* Now that we know we can use the given id, copy the spec to its final destination... */
	_industry_specs[ind_id] = *inds;
	/* ... and mark it as usable*/
	_industry_specs[ind_id].enabled = true;
}

void IndustryTileOverrideManager::SetEntitySpec(const IndustryTileSpec *its)
{
	IndustryGfx indt_id = this->AddEntityID(its->grf_prop.local_id, its->grf_prop.grffile->grfid, its->grf_prop.subst_id);

	if (indt_id == invalid_ID) {
		grfmsg(1, "IndustryTile.SetEntitySpec: Too many industry tiles allocated. Ignoring.");
		return;
	}

	memcpy(&_industry_tile_specs[indt_id], its, sizeof(*its));

	/* Now add the overrides. */
	for (int i = 0; i < max_offset; i++) {
		IndustryTileSpec *overridden_its = &_industry_tile_specs[i];

		if (entity_overrides[i] != its->grf_prop.local_id || grfid_overrides[i] != its->grf_prop.grffile->grfid) continue;

		overridden_its->grf_prop.override = indt_id;
		overridden_its->enabled = false;
		entity_overrides[i] = invalid_ID;
		grfid_overrides[i] = 0;
	}
}

/**
 * Method to install the new object data in its proper slot
 * The slot assignment is internal of this method, since it requires
 * checking what is available
 * @param spec ObjectSpec that comes from the grf decoding process
 */
void ObjectOverrideManager::SetEntitySpec(ObjectSpec *spec)
{
	/* First step : We need to find if this object is already specified in the savegame data. */
	ObjectType type = this->GetID(spec->grf_prop.local_id, spec->grf_prop.grffile->grfid);

	if (type == invalid_ID) {
		/* Not found.
		 * Or it has already been overridden, so you've lost your place.
		 * Or it is a simple substitute.
		 * We need to find a free available slot */
		type = this->AddEntityID(spec->grf_prop.local_id, spec->grf_prop.grffile->grfid, OBJECT_TRANSMITTER);
	}

	if (type == invalid_ID) {
		grfmsg(1, "Object.SetEntitySpec: Too many objects allocated. Ignoring.");
		return;
	}

	extern ObjectSpec _object_specs[NUM_OBJECTS];

	/* Now that we know we can use the given id, copy the spec to its final destination. */
	memcpy(&_object_specs[type], spec, sizeof(*spec));
	ObjectClass::Assign(&_object_specs[type]);
}

/**
 * Method to install the new bridge data in its proper slot
 * The slot assignment is internal of this method, since it requires
 * checking what is available
 * @param spec BridgeSpec that comes from the grf decoding process
 */
void BridgeOverrideManager::SetEntitySpec(BridgeSpec *spec)
{
	/* First step : We need to find if this object is already specified in the savegame data. */
	ObjectType type = this->GetID(spec->grf_prop.local_id, spec->grf_prop.grffile->grfid);

	if (type == invalid_ID) {
		/* Not found.
		 * Or it has already been overridden, so you've lost your place old boy.
		 * Or it is a simple substitute.
		 * We need to find a free available slot */
		type = this->AddEntityID(spec->grf_prop.local_id, spec->grf_prop.grffile->grfid, 0);
	}

	if (type == invalid_ID) {
		grfmsg(1, "Bridge.SetEntitySpec: Too many bridges allocated. Ignoring.");
		return;
	}

	extern BridgeSpec _bridge_specs[NUM_BRIDGES];

	/* Now that we know we can use the given id, copy the spec to its final destination. */
	memcpy(&_bridge_specs[type], spec, sizeof(*spec));
}

/**
 * Function used by houses (and soon industries) to get information
 * on type of "terrain" the tile it is queries sits on.
 * @param tile TileIndex of the tile been queried
 * @param context The context of the tile.
 * @return value corresponding to the grf expected format:
 *         Terrain type: 0 normal, 1 desert, 2 rainforest, 4 on or above snowline
 */
uint32 GetTerrainType(TileIndex tile, TileContext context)
{
	switch (_settings_game.game_creation.landscape) {
		case LT_TROPIC: return GetTropicZone(tile);
		case LT_ARCTIC: {
			bool has_snow;
			switch (GetTileType(tile)) {
				case MP_CLEAR:
					/* During map generation the snowstate may not be valid yet, as the tileloop may not have run yet. */
					if (_generating_world) goto genworld;
					has_snow = IsSnowTile(tile) && GetClearDensity(tile) >= 2;
					break;

				case MP_RAILWAY: {
					/* During map generation the snowstate may not be valid yet, as the tileloop may not have run yet. */
					if (_generating_world) goto genworld; // we do not care about foundations here
					RailGroundType ground = GetRailGroundType(tile);
					has_snow = (ground == RAIL_GROUND_ICE_DESERT || (context == TCX_UPPER_HALFTILE && ground == RAIL_GROUND_HALF_SNOW));
					break;
				}

				case MP_ROAD:
					/* During map generation the snowstate may not be valid yet, as the tileloop may not have run yet. */
					if (_generating_world) goto genworld; // we do not care about foundations here
					has_snow = IsOnSnow(tile);
					break;

				case MP_TREES: {
					/* During map generation the snowstate may not be valid yet, as the tileloop may not have run yet. */
					if (_generating_world) goto genworld;
					TreeGround ground = GetTreeGround(tile);
					has_snow = (ground == TREE_GROUND_SNOW_DESERT || ground == TREE_GROUND_ROUGH_SNOW) && GetTreeDensity(tile) >= 2;
					break;
				}

				case MP_TUNNELBRIDGE:
					if (context == TCX_ON_BRIDGE) {
						has_snow = (GetBridgeHeight(tile) > GetSnowLine());
					} else {
						/* During map generation the snowstate may not be valid yet, as the tileloop may not have run yet. */
						if (_generating_world) goto genworld; // we do not care about foundations here
						has_snow = HasTunnelBridgeSnowOrDesert(tile);
					}
					break;

				case MP_STATION:
				case MP_HOUSE:
				case MP_INDUSTRY:
				case MP_OBJECT:
					/* These tiles usually have a levelling foundation. So use max Z */
					has_snow = (GetTileMaxZ(tile) > GetSnowLine());
					break;

				case MP_VOID:
				case MP_WATER:
				genworld:
					has_snow = (GetTileZ(tile) > GetSnowLine());
					break;

				default: NOT_REACHED();
			}
			return has_snow ? 4 : 0;
		}
		default:        return 0;
	}
}

/**
 * Get the tile at the given offset.
 * @param parameter The NewGRF "encoded" offset.
 * @param tile The tile to base the offset from.
 * @param signed_offsets Whether the offsets are to be interpreted as signed or not.
 * @param axis Axis of a railways station.
 * @return The tile at the offset.
 */
TileIndex GetNearbyTile(byte parameter, TileIndex tile, bool signed_offsets, Axis axis)
{
	int8 x = GB(parameter, 0, 4);
	int8 y = GB(parameter, 4, 4);

	if (signed_offsets && x >= 8) x -= 16;
	if (signed_offsets && y >= 8) y -= 16;

	/* Swap width and height depending on axis for railway stations */
	if (axis == INVALID_AXIS && HasStationTileRail(tile)) axis = GetRailStationAxis(tile);
	if (axis == AXIS_Y) Swap(x, y);

	/* Make sure we never roam outside of the map, better wrap in that case */
	return TILE_MASK(tile + TileDiffXY(x, y));
}

/**
 * Common part of station var 0x67, house var 0x62, indtile var 0x60, industry var 0x62.
 *
 * @param tile the tile of interest.
 * @param grf_version8 True, if we are dealing with a new NewGRF which uses GRF version >= 8.
 * @return 0czzbbss: c = TileType; zz = TileZ; bb: 7-3 zero, 4-2 TerrainType, 1 water/shore, 0 zero; ss = TileSlope
 */
uint32 GetNearbyTileInformation(TileIndex tile, bool grf_version8)
{
	TileType tile_type = GetTileType(tile);

	/* Fake tile type for trees on shore */
	if (IsTileType(tile, MP_TREES) && GetTreeGround(tile) == TREE_GROUND_SHORE) tile_type = MP_WATER;

	int z;
	Slope tileh = GetTilePixelSlope(tile, &z);
	/* Return 0 if the tile is a land tile */
	byte terrain_type = (HasTileWaterClass(tile) ? (GetWaterClass(tile) + 1) & 3 : 0) << 5 | GetTerrainType(tile) << 2 | (tile_type == MP_WATER ? 1 : 0) << 1;
	if (grf_version8) z /= TILE_HEIGHT;
	return tile_type << 24 | Clamp(z, 0, 0xFF) << 16 | terrain_type << 8 | tileh;
}

/**
 * Returns company information like in vehicle var 43 or station var 43.
 * @param owner Owner of the object.
 * @param l Livery of the object; nullptr to use default.
 * @return NewGRF company information.
 */
uint32 GetCompanyInfo(CompanyID owner, const Livery *l)
{
	if (l == nullptr && Company::IsValidID(owner)) l = &Company::Get(owner)->livery[LS_DEFAULT];
	return owner | (Company::IsValidAiID(owner) ? 0x10000 : 0) | (l != nullptr ? (l->colour1 << 24) | (l->colour2 << 28) : 0);
}

/**
 * Get the error message from a shape/location/slope check callback result.
 * @param cb_res Callback result to translate. If bit 10 is set this is a standard error message, otherwise a NewGRF provided string.
 * @param grffile NewGRF to use to resolve a custom error message.
 * @param default_error Error message to use for the generic error.
 * @return CommandCost indicating success or the error message.
 */
CommandCost GetErrorMessageFromLocationCallbackResult(uint16 cb_res, const GRFFile *grffile, StringID default_error)
{
	CommandCost res;

	if (cb_res < 0x400) {
		res = CommandCost(GetGRFStringID(grffile->grfid, 0xD000 + cb_res));
	} else {
		switch (cb_res) {
			case 0x400: return res; // No error.

			default:    // unknown reason -> default error
			case 0x401: res = CommandCost(default_error); break;

			case 0x402: res = CommandCost(STR_ERROR_CAN_ONLY_BE_BUILT_IN_RAINFOREST); break;
			case 0x403: res = CommandCost(STR_ERROR_CAN_ONLY_BE_BUILT_IN_DESERT); break;
			case 0x404: res = CommandCost(STR_ERROR_CAN_ONLY_BE_BUILT_ABOVE_SNOW_LINE); break;
			case 0x405: res = CommandCost(STR_ERROR_CAN_ONLY_BE_BUILT_BELOW_SNOW_LINE); break;
			case 0x406: res = CommandCost(STR_ERROR_CAN_T_BUILD_ON_SEA); break;
			case 0x407: res = CommandCost(STR_ERROR_CAN_T_BUILD_ON_CANAL); break;
			case 0x408: res = CommandCost(STR_ERROR_CAN_T_BUILD_ON_RIVER); break;
		}
	}

	/* Copy some parameters from the registers to the error message text ref. stack */
	res.UseTextRefStack(grffile, 4);

	return res;
}

/**
 * Record that a NewGRF returned an unknown/invalid callback result.
 * Also show an error to the user.
 * @param grfid ID of the NewGRF causing the problem.
 * @param cbid Callback causing the problem.
 * @param cb_res Invalid result returned by the callback.
 */
void ErrorUnknownCallbackResult(uint32 grfid, uint16 cbid, uint16 cb_res)
{
	GRFConfig *grfconfig = GetGRFConfig(grfid);

	if (!HasBit(grfconfig->grf_bugs, GBUG_UNKNOWN_CB_RESULT)) {
		SetBit(grfconfig->grf_bugs, GBUG_UNKNOWN_CB_RESULT);
		SetDParamStr(0, grfconfig->GetName());
		SetDParam(1, cbid);
		SetDParam(2, cb_res);
		ShowErrorMessage(STR_NEWGRF_BUGGY, STR_NEWGRF_BUGGY_UNKNOWN_CALLBACK_RESULT, WL_CRITICAL);
	}

	/* debug output */
	char buffer[512];

	SetDParamStr(0, grfconfig->GetName());
	GetString(buffer, STR_NEWGRF_BUGGY, lastof(buffer));
	DEBUG(grf, 0, "%s", buffer + 3);

	SetDParam(1, cbid);
	SetDParam(2, cb_res);
	GetString(buffer, STR_NEWGRF_BUGGY_UNKNOWN_CALLBACK_RESULT, lastof(buffer));
	DEBUG(grf, 0, "%s", buffer + 3);
}

/**
 * Converts a callback result into a boolean.
 * For grf version < 8 the result is checked for zero or non-zero.
 * For grf version >= 8 the callback result must be 0 or 1.
 * @param grffile NewGRF returning the value.
 * @param cbid Callback returning the value.
 * @param cb_res Callback result.
 * @return Boolean value. True if cb_res != 0.
 */
bool ConvertBooleanCallback(const GRFFile *grffile, uint16 cbid, uint16 cb_res)
{
	assert(cb_res != CALLBACK_FAILED); // We do not know what to return

	if (grffile->grf_version < 8) return cb_res != 0;

	if (cb_res > 1) ErrorUnknownCallbackResult(grffile->grfid, cbid, cb_res);
	return cb_res != 0;
}

/**
 * Converts a callback result into a boolean.
 * For grf version < 8 the first 8 bit of the result are checked for zero or non-zero.
 * For grf version >= 8 the callback result must be 0 or 1.
 * @param grffile NewGRF returning the value.
 * @param cbid Callback returning the value.
 * @param cb_res Callback result.
 * @return Boolean value. True if cb_res != 0.
 */
bool Convert8bitBooleanCallback(const GRFFile *grffile, uint16 cbid, uint16 cb_res)
{
	assert(cb_res != CALLBACK_FAILED); // We do not know what to return

	if (grffile->grf_version < 8) return GB(cb_res, 0, 8) != 0;

	if (cb_res > 1) ErrorUnknownCallbackResult(grffile->grfid, cbid, cb_res);
	return cb_res != 0;
}


/* static */ std::vector<DrawTileSeqStruct> NewGRFSpriteLayout::result_seq;

/**
 * Clone the building sprites of a spritelayout.
 * @param source The building sprites to copy.
 */
void NewGRFSpriteLayout::Clone(const DrawTileSeqStruct *source)
{
	assert(this->seq == nullptr);
	assert(source != nullptr);

	size_t count = 1; // 1 for the terminator
	const DrawTileSeqStruct *element;
	foreach_draw_tile_seq(element, source) count++;

	DrawTileSeqStruct *sprites = MallocT<DrawTileSeqStruct>(count);
	MemCpyT(sprites, source, count);
	this->seq = sprites;
}

/**
 * Clone a spritelayout.
 * @param source The spritelayout to copy.
 */
void NewGRFSpriteLayout::Clone(const NewGRFSpriteLayout *source)
{
	this->Clone((const DrawTileSprites*)source);

	if (source->registers != nullptr) {
		size_t count = 1; // 1 for the ground sprite
		const DrawTileSeqStruct *element;
		foreach_draw_tile_seq(element, source->seq) count++;

		TileLayoutRegisters *regs = MallocT<TileLayoutRegisters>(count);
		MemCpyT(regs, source->registers, count);
		this->registers = regs;
	}
}


/**
 * Allocate a spritelayout for \a num_sprites building sprites.
 * @param num_sprites Number of building sprites to allocate memory for. (not counting the terminator)
 */
void NewGRFSpriteLayout::Allocate(uint num_sprites)
{
	assert(this->seq == nullptr);

	DrawTileSeqStruct *sprites = CallocT<DrawTileSeqStruct>(num_sprites + 1);
	sprites[num_sprites].MakeTerminator();
	this->seq = sprites;
}

/**
 * Allocate memory for register modifiers.
 */
void NewGRFSpriteLayout::AllocateRegisters()
{
	assert(this->seq != nullptr);
	assert(this->registers == nullptr);

	size_t count = 1; // 1 for the ground sprite
	const DrawTileSeqStruct *element;
	foreach_draw_tile_seq(element, this->seq) count++;

	this->registers = CallocT<TileLayoutRegisters>(count);
}

/**
 * Prepares a sprite layout before resolving action-1-2-3 chains.
 * Integrates offsets into the layout and determines which chains to resolve.
 * @note The function uses statically allocated temporary storage, which is reused every time when calling the function.
 *       That means, you have to use the sprite layout before calling #PrepareLayout() the next time.
 * @param orig_offset          Offset to apply to non-action-1 sprites.
 * @param newgrf_ground_offset Offset to apply to action-1 ground sprites.
 * @param newgrf_offset        Offset to apply to action-1 non-ground sprites.
 * @param constr_stage         Construction stage (0-3) to apply to all action-1 sprites.
 * @param separate_ground      Whether the ground sprite shall be resolved by a separate action-1-2-3 chain by default.
 * @return Bitmask of values for variable 10 to resolve action-1-2-3 chains for.
 */
uint32 NewGRFSpriteLayout::PrepareLayout(uint32 orig_offset, uint32 newgrf_ground_offset, uint32 newgrf_offset, uint constr_stage, bool separate_ground) const
{
	result_seq.clear();
	uint32 var10_values = 0;

	/* Create a copy of the spritelayout, so we can modify some values.
	 * Also include the groundsprite into the sequence for easier processing. */
	DrawTileSeqStruct *result = &result_seq.emplace_back();
	result->image = ground;
	result->delta_x = 0;
	result->delta_y = 0;
	result->delta_z = (int8)0x80;

	const DrawTileSeqStruct *dtss;
	foreach_draw_tile_seq(dtss, this->seq) {
		result_seq.push_back(*dtss);
	}
	result_seq.emplace_back().MakeTerminator();
	/* Determine the var10 values the action-1-2-3 chains needs to be resolved for,
	 * and apply the default sprite offsets (unless disabled). */
	const TileLayoutRegisters *regs = this->registers;
	bool ground = true;
	foreach_draw_tile_seq(result, result_seq.data()) {
		TileLayoutFlags flags = TLF_NOTHING;
		if (regs != nullptr) flags = regs->flags;

		/* Record var10 value for the sprite */
		if (HasBit(result->image.sprite, SPRITE_MODIFIER_CUSTOM_SPRITE) || (flags & TLF_SPRITE_REG_FLAGS)) {
			uint8 var10 = (flags & TLF_SPRITE_VAR10) ? regs->sprite_var10 : (ground && separate_ground ? 1 : 0);
			SetBit(var10_values, var10);
		}

		/* Add default sprite offset, unless there is a custom one */
		if (!(flags & TLF_SPRITE)) {
			if (HasBit(result->image.sprite, SPRITE_MODIFIER_CUSTOM_SPRITE)) {
				result->image.sprite += ground ? newgrf_ground_offset : newgrf_offset;
				if (constr_stage > 0 && regs != nullptr) result->image.sprite += GetConstructionStageOffset(constr_stage, regs->max_sprite_offset);
			} else {
				result->image.sprite += orig_offset;
			}
		}

		/* Record var10 value for the palette */
		if (HasBit(result->image.pal, SPRITE_MODIFIER_CUSTOM_SPRITE) || (flags & TLF_PALETTE_REG_FLAGS)) {
			uint8 var10 = (flags & TLF_PALETTE_VAR10) ? regs->palette_var10 : (ground && separate_ground ? 1 : 0);
			SetBit(var10_values, var10);
		}

		/* Add default palette offset, unless there is a custom one */
		if (!(flags & TLF_PALETTE)) {
			if (HasBit(result->image.pal, SPRITE_MODIFIER_CUSTOM_SPRITE)) {
				result->image.sprite += ground ? newgrf_ground_offset : newgrf_offset;
				if (constr_stage > 0 && regs != nullptr) result->image.sprite += GetConstructionStageOffset(constr_stage, regs->max_palette_offset);
			}
		}

		ground = false;
		if (regs != nullptr) regs++;
	}

	return var10_values;
}

/**
 * Evaluates the register modifiers and integrates them into the preprocessed sprite layout.
 * @pre #PrepareLayout() needs calling first.
 * @param resolved_var10  The value of var10 the action-1-2-3 chain was evaluated for.
 * @param resolved_sprite Result sprite of the action-1-2-3 chain.
 * @param separate_ground Whether the ground sprite is resolved by a separate action-1-2-3 chain.
 * @return Resulting spritelayout after processing the registers.
 */
void NewGRFSpriteLayout::ProcessRegisters(uint8 resolved_var10, uint32 resolved_sprite, bool separate_ground) const
{
	DrawTileSeqStruct *result;
	const TileLayoutRegisters *regs = this->registers;
	bool ground = true;
	foreach_draw_tile_seq(result, result_seq.data()) {
		TileLayoutFlags flags = TLF_NOTHING;
		if (regs != nullptr) flags = regs->flags;

		/* Is the sprite or bounding box affected by an action-1-2-3 chain? */
		if (HasBit(result->image.sprite, SPRITE_MODIFIER_CUSTOM_SPRITE) || (flags & TLF_SPRITE_REG_FLAGS)) {
			/* Does the var10 value apply to this sprite? */
			uint8 var10 = (flags & TLF_SPRITE_VAR10) ? regs->sprite_var10 : (ground && separate_ground ? 1 : 0);
			if (var10 == resolved_var10) {
				/* Apply registers */
				if ((flags & TLF_DODRAW) && GetRegister(regs->dodraw) == 0) {
					result->image.sprite = 0;
				} else {
					if (HasBit(result->image.sprite, SPRITE_MODIFIER_CUSTOM_SPRITE)) result->image.sprite += resolved_sprite;
					if (flags & TLF_SPRITE) {
						int16 offset = (int16)GetRegister(regs->sprite); // mask to 16 bits to avoid trouble
						if (!HasBit(result->image.sprite, SPRITE_MODIFIER_CUSTOM_SPRITE) || (offset >= 0 && offset < regs->max_sprite_offset)) {
							result->image.sprite += offset;
						} else {
							result->image.sprite = SPR_IMG_QUERY;
						}
					}

					if (result->IsParentSprite()) {
						if (flags & TLF_BB_XY_OFFSET) {
							result->delta_x += (int32)GetRegister(regs->delta.parent[0]);
							result->delta_y += (int32)GetRegister(regs->delta.parent[1]);
						}
						if (flags & TLF_BB_Z_OFFSET)    result->delta_z += (int32)GetRegister(regs->delta.parent[2]);
					} else {
						if (flags & TLF_CHILD_X_OFFSET) result->delta_x += (int32)GetRegister(regs->delta.child[0]);
						if (flags & TLF_CHILD_Y_OFFSET) result->delta_y += (int32)GetRegister(regs->delta.child[1]);
					}
				}
			}
		}

		/* Is the palette affected by an action-1-2-3 chain? */
		if (result->image.sprite != 0 && (HasBit(result->image.pal, SPRITE_MODIFIER_CUSTOM_SPRITE) || (flags & TLF_PALETTE_REG_FLAGS))) {
			/* Does the var10 value apply to this sprite? */
			uint8 var10 = (flags & TLF_PALETTE_VAR10) ? regs->palette_var10 : (ground && separate_ground ? 1 : 0);
			if (var10 == resolved_var10) {
				/* Apply registers */
				if (HasBit(result->image.pal, SPRITE_MODIFIER_CUSTOM_SPRITE)) result->image.pal += resolved_sprite;
				if (flags & TLF_PALETTE) {
					int16 offset = (int16)GetRegister(regs->palette); // mask to 16 bits to avoid trouble
					if (!HasBit(result->image.pal, SPRITE_MODIFIER_CUSTOM_SPRITE) || (offset >= 0 && offset < regs->max_palette_offset)) {
						result->image.pal += offset;
					} else {
						result->image.sprite = SPR_IMG_QUERY;
						result->image.pal = PAL_NONE;
					}
				}
			}
		}

		ground = false;
		if (regs != nullptr) regs++;
	}
}
