/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file object_cmd.cpp Handling of object tiles. */

#include "stdafx.h"
#include "landscape.h"
#include "command_func.h"
#include "viewport_func.h"
#include "company_base.h"
#include "town.h"
#include "bridge_map.h"
#include "genworld.h"
#include "autoslope.h"
#include "clear_func.h"
#include "water.h"
#include "window_func.h"
#include "company_gui.h"
#include "cheat_type.h"
#include "object.h"
#include "cargopacket.h"
#include "core/random_func.hpp"
#include "core/pool_func.hpp"
#include "object_map.h"
#include "object_base.h"
#include "newgrf_config.h"
#include "newgrf_object.h"
#include "date_func.h"
#include "newgrf_debug.h"
#include "vehicle_func.h"

#include "table/strings.h"
#include "table/object_land.h"

#include "safeguards.h"

ObjectPool _object_pool("Object");
INSTANTIATE_POOL_METHODS(Object)
uint16 Object::counts[NUM_OBJECTS];

/**
 * Get the object associated with a tile.
 * @param tile The tile to fetch the object for.
 * @return The object.
 */
/* static */ Object *Object::GetByTile(TileIndex tile)
{
	return Object::Get(GetObjectIndex(tile));
}

/**
 * Gets the ObjectType of the given object tile
 * @param t the tile to get the type from.
 * @pre IsTileType(t, MP_OBJECT)
 * @return the type.
 */
ObjectType GetObjectType(TileIndex t)
{
	assert(IsTileType(t, MP_OBJECT));
	return Object::GetByTile(t)->type;
}

/** Initialize/reset the objects. */
void InitializeObjects()
{
	Object::ResetTypeCounts();
}

/**
 * Actually build the object.
 * @param type  The type of object to build.
 * @param tile  The tile to build the northern tile of the object on.
 * @param owner The owner of the object.
 * @param town  Town the tile is related with.
 * @param view  The view for the object.
 * @pre All preconditions for building the object at that location
 *      are met, e.g. slope and clearness of tiles are checked.
 */
void BuildObject(ObjectType type, TileIndex tile, CompanyID owner, Town *town, uint8 view)
{
	const ObjectSpec *spec = ObjectSpec::Get(type);

	TileArea ta(tile, GB(spec->size, HasBit(view, 0) ? 4 : 0, 4), GB(spec->size, HasBit(view, 0) ? 0 : 4, 4));
	Object *o = new Object();
	o->type          = type;
	o->location      = ta;
	o->town          = town == nullptr ? CalcClosestTownFromTile(tile) : town;
	o->build_date    = _date;
	o->view          = view;

	/* If nothing owns the object, the colour will be random. Otherwise
	 * get the colour from the company's livery settings. */
	if (owner == OWNER_NONE) {
		o->colour = Random();
	} else {
		const Livery *l = Company::Get(owner)->livery;
		o->colour = l->colour1 + l->colour2 * 16;
	}

	/* If the object wants only one colour, then give it that colour. */
	if ((spec->flags & OBJECT_FLAG_2CC_COLOUR) == 0) o->colour &= 0xF;

	if (HasBit(spec->callback_mask, CBM_OBJ_COLOUR)) {
		uint16 res = GetObjectCallback(CBID_OBJECT_COLOUR, o->colour, 0, spec, o, tile);
		if (res != CALLBACK_FAILED) {
			if (res >= 0x100) ErrorUnknownCallbackResult(spec->grf_prop.grffile->grfid, CBID_OBJECT_COLOUR, res);
			o->colour = GB(res, 0, 8);
		}
	}

	assert(o->town != nullptr);

	TILE_AREA_LOOP(t, ta) {
		WaterClass wc = (IsWaterTile(t) ? GetWaterClass(t) : WATER_CLASS_INVALID);
		/* Update company infrastructure counts for objects build on canals owned by nobody. */
		if (wc == WATER_CLASS_CANAL && owner != OWNER_NONE && (IsTileOwner(tile, OWNER_NONE) || IsTileOwner(tile, OWNER_WATER))) {
			Company::Get(owner)->infrastructure.water++;
			DirtyCompanyInfrastructureWindows(owner);
		}
		MakeObject(t, owner, o->index, wc, Random());
		MarkTileDirtyByTile(t);
	}

	Object::IncTypeCount(type);
	if (spec->flags & OBJECT_FLAG_ANIMATION) TriggerObjectAnimation(o, OAT_BUILT, spec);
}

/**
 * Increase the animation stage of a whole structure.
 * @param tile The tile of the structure.
 */
static void IncreaseAnimationStage(TileIndex tile)
{
	TileArea ta = Object::GetByTile(tile)->location;
	TILE_AREA_LOOP(t, ta) {
		SetAnimationFrame(t, GetAnimationFrame(t) + 1);
		MarkTileDirtyByTile(t);
	}
}

/** We encode the company HQ size in the animation stage. */
#define GetCompanyHQSize GetAnimationFrame
/** We encode the company HQ size in the animation stage. */
#define IncreaseCompanyHQSize IncreaseAnimationStage

/**
 * Update the CompanyHQ to the state associated with the given score
 * @param tile  The (northern) tile of the company HQ, or INVALID_TILE.
 * @param score The current (performance) score of the company.
 */
void UpdateCompanyHQ(TileIndex tile, uint score)
{
	if (tile == INVALID_TILE) return;

	byte val = 0;
	if (score >= 170) val++;
	if (score >= 350) val++;
	if (score >= 520) val++;
	if (score >= 720) val++;

	while (GetCompanyHQSize(tile) < val) {
		IncreaseCompanyHQSize(tile);
	}
}

/**
 * Updates the colour of the object whenever a company changes.
 * @param c The company the company colour changed of.
 */
void UpdateObjectColours(const Company *c)
{
	for (Object *obj : Object::Iterate()) {
		Owner owner = GetTileOwner(obj->location.tile);
		/* Not the current owner, so colour doesn't change. */
		if (owner != c->index) continue;

		const ObjectSpec *spec = ObjectSpec::GetByTile(obj->location.tile);
		/* Using the object colour callback, so not using company colour. */
		if (HasBit(spec->callback_mask, CBM_OBJ_COLOUR)) continue;

		const Livery *l = c->livery;
		obj->colour = ((spec->flags & OBJECT_FLAG_2CC_COLOUR) ? (l->colour2 * 16) : 0) + l->colour1;
	}
}

extern CommandCost CheckBuildableTile(TileIndex tile, uint invalid_dirs, int &allowed_z, bool allow_steep, bool check_bridge);
static CommandCost ClearTile_Object(TileIndex tile, DoCommandFlag flags);

/**
 * Build an object object
 * @param tile tile where the object will be located
 * @param flags type of operation
 * @param p1 the object type to build
 * @param p2 the view for the object
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdBuildObject(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	CommandCost cost(EXPENSES_PROPERTY);

	ObjectType type = (ObjectType)GB(p1, 0, 16);
	if (type >= NUM_OBJECTS) return CMD_ERROR;
	uint8 view = GB(p2, 0, 2);
	const ObjectSpec *spec = ObjectSpec::Get(type);
	if (_game_mode == GM_NORMAL && !spec->IsAvailable() && !_generating_world) return CMD_ERROR;
	if ((_game_mode == GM_EDITOR || _generating_world) && !spec->WasEverAvailable()) return CMD_ERROR;

	if ((spec->flags & OBJECT_FLAG_ONLY_IN_SCENEDIT) != 0 && ((!_generating_world && _game_mode != GM_EDITOR) || _current_company != OWNER_NONE)) return CMD_ERROR;
	if ((spec->flags & OBJECT_FLAG_ONLY_IN_GAME) != 0 && (_generating_world || _game_mode != GM_NORMAL || _current_company > MAX_COMPANIES)) return CMD_ERROR;
	if (view >= spec->views) return CMD_ERROR;

	if (!Object::CanAllocateItem()) return_cmd_error(STR_ERROR_TOO_MANY_OBJECTS);
	if (Town::GetNumItems() == 0) return_cmd_error(STR_ERROR_MUST_FOUND_TOWN_FIRST);

	int size_x = GB(spec->size, HasBit(view, 0) ? 4 : 0, 4);
	int size_y = GB(spec->size, HasBit(view, 0) ? 0 : 4, 4);
	TileArea ta(tile, size_x, size_y);

	if (type == OBJECT_OWNED_LAND) {
		/* Owned land is special as it can be placed on any slope. */
		cost.AddCost(DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR));
	} else {
		/* Check the surface to build on. At this time we can't actually execute the
		 * the CLEAR_TILE commands since the newgrf callback later on can check
		 * some information about the tiles. */
		bool allow_water = (spec->flags & (OBJECT_FLAG_BUILT_ON_WATER | OBJECT_FLAG_NOT_ON_LAND)) != 0;
		bool allow_ground = (spec->flags & OBJECT_FLAG_NOT_ON_LAND) == 0;
		TILE_AREA_LOOP(t, ta) {
			if (HasTileWaterGround(t)) {
				if (!allow_water) return_cmd_error(STR_ERROR_CAN_T_BUILD_ON_WATER);
				if (!IsWaterTile(t)) {
					/* Normal water tiles don't have to be cleared. For all other tile types clear
					 * the tile but leave the water. */
					cost.AddCost(DoCommand(t, 0, 0, flags & ~DC_NO_WATER & ~DC_EXEC, CMD_LANDSCAPE_CLEAR));
				} else {
					/* Can't build on water owned by another company. */
					Owner o = GetTileOwner(t);
					if (o != OWNER_NONE && o != OWNER_WATER) cost.AddCost(CheckOwnership(o, t));

					/* However, the tile has to be clear of vehicles. */
					cost.AddCost(EnsureNoVehicleOnGround(t));
				}
			} else {
				if (!allow_ground) return_cmd_error(STR_ERROR_MUST_BE_BUILT_ON_WATER);
				/* For non-water tiles, we'll have to clear it before building. */

				/* When relocating HQ, allow it to be relocated (partial) on itself. */
				if (!(type == OBJECT_HQ &&
						IsTileType(t, MP_OBJECT) &&
						IsTileOwner(t, _current_company) &&
						IsObjectType(t, OBJECT_HQ))) {
					cost.AddCost(DoCommand(t, 0, 0, flags & ~DC_EXEC, CMD_LANDSCAPE_CLEAR));
				}
			}
		}

		/* So, now the surface is checked... check the slope of said surface. */
		int allowed_z;
		if (GetTileSlope(tile, &allowed_z) != SLOPE_FLAT) allowed_z++;

		TILE_AREA_LOOP(t, ta) {
			uint16 callback = CALLBACK_FAILED;
			if (HasBit(spec->callback_mask, CBM_OBJ_SLOPE_CHECK)) {
				TileIndex diff = t - tile;
				callback = GetObjectCallback(CBID_OBJECT_LAND_SLOPE_CHECK, GetTileSlope(t), TileY(diff) << 4 | TileX(diff), spec, nullptr, t, view);
			}

			if (callback == CALLBACK_FAILED) {
				cost.AddCost(CheckBuildableTile(t, 0, allowed_z, false, false));
			} else {
				/* The meaning of bit 10 is inverted for a grf version < 8. */
				if (spec->grf_prop.grffile->grf_version < 8) ToggleBit(callback, 10);
				CommandCost ret = GetErrorMessageFromLocationCallbackResult(callback, spec->grf_prop.grffile, STR_ERROR_LAND_SLOPED_IN_WRONG_DIRECTION);
				if (ret.Failed()) return ret;
			}
		}

		if (flags & DC_EXEC) {
			/* This is basically a copy of the loop above with the exception that we now
			 * execute the commands and don't check for errors, since that's already done. */
			TILE_AREA_LOOP(t, ta) {
				if (HasTileWaterGround(t)) {
					if (!IsWaterTile(t)) {
						DoCommand(t, 0, 0, (flags & ~DC_NO_WATER) | DC_NO_MODIFY_TOWN_RATING, CMD_LANDSCAPE_CLEAR);
					}
				} else {
					DoCommand(t, 0, 0, flags | DC_NO_MODIFY_TOWN_RATING, CMD_LANDSCAPE_CLEAR);
				}
			}
		}
	}
	if (cost.Failed()) return cost;

	/* Finally do a check for bridges. */
	TILE_AREA_LOOP(t, ta) {
		if (IsBridgeAbove(t)) {
			int bridge_height = GetLowestBridgeHeight(tile);
			if (!(spec->flags & OBJECT_FLAG_ALLOW_UNDER_BRIDGE) || (GetTileMaxZ(t) + spec->height >= bridge_height)) {
				return_cmd_error(STR_ERROR_MUST_DEMOLISH_BRIDGE_FIRST);
			}
		}
	}

	int hq_score = 0;
	switch (type) {
		case OBJECT_TRANSMITTER:
		case OBJECT_LIGHTHOUSE:
			if (!IsTileFlat(tile)) return_cmd_error(STR_ERROR_FLAT_LAND_REQUIRED);
			break;

		case OBJECT_OWNED_LAND:
			if (IsTileType(tile, MP_OBJECT) &&
					IsTileOwner(tile, _current_company) &&
					IsObjectType(tile, OBJECT_OWNED_LAND)) {
				return_cmd_error(STR_ERROR_YOU_ALREADY_OWN_IT);
			}
			break;

		case OBJECT_HQ: {
			Company *c = Company::Get(_current_company);
			if (c->location_of_HQ != INVALID_TILE) {
				/* We need to persuade a bit harder to remove the old HQ. */
				_current_company = OWNER_WATER;
				cost.AddCost(ClearTile_Object(c->location_of_HQ, flags));
				_current_company = c->index;
			}

			if (flags & DC_EXEC) {
				hq_score = UpdateCompanyRatingAndValue(c, false);
				c->location_of_HQ = tile;
				SetWindowDirty(WC_COMPANY, c->index);
			}
			break;
		}

		case OBJECT_STATUE:
			/* This may never be constructed using this method. */
			return CMD_ERROR;

		default: // i.e. NewGRF provided.
			break;
	}

	if (flags & DC_EXEC) {
		BuildObject(type, tile, _current_company, nullptr, view);

		/* Make sure the HQ starts at the right size. */
		if (type == OBJECT_HQ) UpdateCompanyHQ(tile, hq_score);
	}

	cost.AddCost(ObjectSpec::Get(type)->GetBuildCost() * size_x * size_y);
	return cost;
}


static Foundation GetFoundation_Object(TileIndex tile, Slope tileh);

static void DrawTile_Object(TileInfo *ti)
{
	ObjectType type = GetObjectType(ti->tile);
	const ObjectSpec *spec = ObjectSpec::Get(type);

	/* Fall back for when the object doesn't exist anymore. */
	if (!spec->enabled) type = OBJECT_TRANSMITTER;

	if ((spec->flags & OBJECT_FLAG_HAS_NO_FOUNDATION) == 0) DrawFoundation(ti, GetFoundation_Object(ti->tile, ti->tileh));

	if (type < NEW_OBJECT_OFFSET) {
		const DrawTileSprites *dts = nullptr;
		Owner to = GetTileOwner(ti->tile);
		PaletteID palette = to == OWNER_NONE ? PAL_NONE : COMPANY_SPRITE_COLOUR(to);

		if (type == OBJECT_HQ) {
			TileIndex diff = ti->tile - Object::GetByTile(ti->tile)->location.tile;
			dts = &_object_hq[GetCompanyHQSize(ti->tile) << 2 | TileY(diff) << 1 | TileX(diff)];
		} else {
			dts = &_objects[type];
		}

		if (spec->flags & OBJECT_FLAG_HAS_NO_FOUNDATION) {
			/* If an object has no foundation, but tries to draw a (flat) ground
			 * type... we have to be nice and convert that for them. */
			switch (dts->ground.sprite) {
				case SPR_FLAT_BARE_LAND:          DrawClearLandTile(ti, 0); break;
				case SPR_FLAT_1_THIRD_GRASS_TILE: DrawClearLandTile(ti, 1); break;
				case SPR_FLAT_2_THIRD_GRASS_TILE: DrawClearLandTile(ti, 2); break;
				case SPR_FLAT_GRASS_TILE:         DrawClearLandTile(ti, 3); break;
				default: DrawGroundSprite(dts->ground.sprite, palette);     break;
			}
		} else {
			DrawGroundSprite(dts->ground.sprite, palette);
		}

		if (!IsInvisibilitySet(TO_STRUCTURES)) {
			const DrawTileSeqStruct *dtss;
			foreach_draw_tile_seq(dtss, dts->seq) {
				AddSortableSpriteToDraw(
					dtss->image.sprite, palette,
					ti->x + dtss->delta_x, ti->y + dtss->delta_y,
					dtss->size_x, dtss->size_y,
					dtss->size_z, ti->z + dtss->delta_z,
					IsTransparencySet(TO_STRUCTURES)
				);
			}
		}
	} else {
		DrawNewObjectTile(ti, spec);
	}

	DrawBridgeMiddle(ti);
}

static int GetSlopePixelZ_Object(TileIndex tile, uint x, uint y)
{
	if (IsObjectType(tile, OBJECT_OWNED_LAND)) {
		int z;
		Slope tileh = GetTilePixelSlope(tile, &z);

		return z + GetPartialPixelZ(x & 0xF, y & 0xF, tileh);
	} else {
		return GetTileMaxPixelZ(tile);
	}
}

static Foundation GetFoundation_Object(TileIndex tile, Slope tileh)
{
	return IsObjectType(tile, OBJECT_OWNED_LAND) ? FOUNDATION_NONE : FlatteningFoundation(tileh);
}

/**
 * Perform the actual removal of the object from the map.
 * @param o The object to really clear.
 */
static void ReallyClearObjectTile(Object *o)
{
	Object::DecTypeCount(o->type);
	TILE_AREA_LOOP(tile_cur, o->location) {
		DeleteNewGRFInspectWindow(GSF_OBJECTS, tile_cur);

		MakeWaterKeepingClass(tile_cur, GetTileOwner(tile_cur));
	}
	delete o;
}

std::vector<ClearedObjectArea> _cleared_object_areas;

/**
 * Find the entry in _cleared_object_areas which occupies a certain tile.
 * @param tile Tile of interest
 * @return Occupying entry, or nullptr if none
 */
ClearedObjectArea *FindClearedObject(TileIndex tile)
{
	TileArea ta = TileArea(tile, 1, 1);

	for (ClearedObjectArea &coa : _cleared_object_areas) {
		if (coa.area.Intersects(ta)) return &coa;
	}

	return nullptr;
}

static CommandCost ClearTile_Object(TileIndex tile, DoCommandFlag flags)
{
	/* Get to the northern most tile. */
	Object *o = Object::GetByTile(tile);
	TileArea ta = o->location;

	ObjectType type = o->type;
	const ObjectSpec *spec = ObjectSpec::Get(type);

	CommandCost cost(EXPENSES_CONSTRUCTION, spec->GetClearCost() * ta.w * ta.h / 5);
	if (spec->flags & OBJECT_FLAG_CLEAR_INCOME) cost.MultiplyCost(-1); // They get an income!

	/* Towns can't remove any objects. */
	if (_current_company == OWNER_TOWN) return CMD_ERROR;

	/* Water can remove everything! */
	if (_current_company != OWNER_WATER) {
		if ((flags & DC_NO_WATER) && IsTileOnWater(tile)) {
			/* There is water under the object, treat it as water tile. */
			return_cmd_error(STR_ERROR_CAN_T_BUILD_ON_WATER);
		} else if (!(spec->flags & OBJECT_FLAG_AUTOREMOVE) && (flags & DC_AUTO)) {
			/* No automatic removal by overbuilding stuff. */
			return_cmd_error(type == OBJECT_HQ ? STR_ERROR_COMPANY_HEADQUARTERS_IN : STR_ERROR_OBJECT_IN_THE_WAY);
		} else if (_game_mode == GM_EDITOR) {
			/* No further limitations for the editor. */
		} else if (GetTileOwner(tile) == OWNER_NONE) {
			/* Owned by nobody and unremovable, so we can only remove it with brute force! */
			if (!_cheats.magic_bulldozer.value && (spec->flags & OBJECT_FLAG_CANNOT_REMOVE) != 0) return CMD_ERROR;
		} else if (CheckTileOwnership(tile).Failed()) {
			/* We don't own it!. */
			return_cmd_error(STR_ERROR_OWNED_BY);
		} else if ((spec->flags & OBJECT_FLAG_CANNOT_REMOVE) != 0 && (spec->flags & OBJECT_FLAG_AUTOREMOVE) == 0) {
			/* In the game editor or with cheats we can remove, otherwise we can't. */
			if (!_cheats.magic_bulldozer.value) {
				if (type == OBJECT_HQ) return_cmd_error(STR_ERROR_COMPANY_HEADQUARTERS_IN);
				return CMD_ERROR;
			}

			/* Removing with the cheat costs more in TTDPatch / the specs. */
			cost.MultiplyCost(25);
		}
	} else if ((spec->flags & (OBJECT_FLAG_BUILT_ON_WATER | OBJECT_FLAG_NOT_ON_LAND)) != 0) {
		/* Water can't remove objects that are buildable on water. */
		return CMD_ERROR;
	}

	switch (type) {
		case OBJECT_HQ: {
			Company *c = Company::Get(GetTileOwner(tile));
			if (flags & DC_EXEC) {
				c->location_of_HQ = INVALID_TILE; // reset HQ position
				SetWindowDirty(WC_COMPANY, c->index);
				CargoPacket::InvalidateAllFrom(ST_HEADQUARTERS, c->index);
			}

			/* cost of relocating company is 1% of company value */
			cost = CommandCost(EXPENSES_PROPERTY, CalculateCompanyValue(c) / 100);
			break;
		}

		case OBJECT_STATUE:
			if (flags & DC_EXEC) {
				Town *town = o->town;
				ClrBit(town->statues, GetTileOwner(tile));
				SetWindowDirty(WC_TOWN_AUTHORITY, town->index);
			}
			break;

		default:
			break;
	}

	_cleared_object_areas.push_back({tile, ta});

	if (flags & DC_EXEC) ReallyClearObjectTile(o);

	return cost;
}

static void AddAcceptedCargo_Object(TileIndex tile, CargoArray &acceptance, CargoTypes *always_accepted)
{
	if (!IsObjectType(tile, OBJECT_HQ)) return;

	/* HQ accepts passenger and mail; but we have to divide the values
	 * between 4 tiles it occupies! */

	/* HQ level (depends on company performance) in the range 1..5. */
	uint level = GetCompanyHQSize(tile) + 1;

	/* Top town building generates 10, so to make HQ interesting, the top
	 * type makes 20. */
	acceptance[CT_PASSENGERS] += std::max(1U, level);
	SetBit(*always_accepted, CT_PASSENGERS);

	/* Top town building generates 4, HQ can make up to 8. The
	 * proportion passengers:mail is different because such a huge
	 * commercial building generates unusually high amount of mail
	 * correspondence per physical visitor. */
	acceptance[CT_MAIL] += std::max(1U, level / 2);
	SetBit(*always_accepted, CT_MAIL);
}

static void AddProducedCargo_Object(TileIndex tile, CargoArray &produced)
{
	if (!IsObjectType(tile, OBJECT_HQ)) return;

	produced[CT_PASSENGERS]++;
	produced[CT_MAIL]++;
}


static void GetTileDesc_Object(TileIndex tile, TileDesc *td)
{
	const ObjectSpec *spec = ObjectSpec::GetByTile(tile);
	td->str = spec->name;
	td->owner[0] = GetTileOwner(tile);
	td->build_date = Object::GetByTile(tile)->build_date;

	if (spec->grf_prop.grffile != nullptr) {
		td->grf = GetGRFConfig(spec->grf_prop.grffile->grfid)->GetName();
	}
}

static void TileLoop_Object(TileIndex tile)
{
	const ObjectSpec *spec = ObjectSpec::GetByTile(tile);
	if (spec->flags & OBJECT_FLAG_ANIMATION) {
		Object *o = Object::GetByTile(tile);
		TriggerObjectTileAnimation(o, tile, OAT_TILELOOP, spec);
		if (o->location.tile == tile) TriggerObjectAnimation(o, OAT_256_TICKS, spec);
	}

	if (IsTileOnWater(tile)) TileLoop_Water(tile);

	if (!IsObjectType(tile, OBJECT_HQ)) return;

	/* HQ accepts passenger and mail; but we have to divide the values
	 * between 4 tiles it occupies! */

	/* HQ level (depends on company performance) in the range 1..5. */
	uint level = GetCompanyHQSize(tile) + 1;
	assert(level < 6);

	StationFinder stations(TileArea(tile, 2, 2));

	uint r = Random();
	/* Top town buildings generate 250, so the top HQ type makes 256. */
	if (GB(r, 0, 8) < (256 / 4 / (6 - level))) {
		uint amt = GB(r, 0, 8) / 8 / 4 + 1;
		if (EconomyIsInRecession()) amt = (amt + 1) >> 1;
		MoveGoodsToStation(CT_PASSENGERS, amt, ST_HEADQUARTERS, GetTileOwner(tile), stations.GetStations());
	}

	/* Top town building generates 90, HQ can make up to 196. The
	 * proportion passengers:mail is about the same as in the acceptance
	 * equations. */
	if (GB(r, 8, 8) < (196 / 4 / (6 - level))) {
		uint amt = GB(r, 8, 8) / 8 / 4 + 1;
		if (EconomyIsInRecession()) amt = (amt + 1) >> 1;
		MoveGoodsToStation(CT_MAIL, amt, ST_HEADQUARTERS, GetTileOwner(tile), stations.GetStations());
	}
}


static TrackStatus GetTileTrackStatus_Object(TileIndex tile, TransportType mode, uint sub_mode, DiagDirection side)
{
	return 0;
}

static bool ClickTile_Object(TileIndex tile)
{
	if (!IsObjectType(tile, OBJECT_HQ)) return false;

	ShowCompany(GetTileOwner(tile));
	return true;
}

static void AnimateTile_Object(TileIndex tile)
{
	AnimateNewObjectTile(tile);
}

/**
 * Helper function for \c CircularTileSearch.
 * @param tile The tile to check.
 * @param user Ignored.
 * @return True iff the tile has a radio tower.
 */
static bool HasTransmitter(TileIndex tile, void *user)
{
	return IsObjectTypeTile(tile, OBJECT_TRANSMITTER);
}

/**
 * Try to build a lighthouse.
 * @return True iff building a lighthouse succeeded.
 */
static bool TryBuildLightHouse()
{
	uint maxx = MapMaxX();
	uint maxy = MapMaxY();
	uint r = Random();

	/* Scatter the lighthouses more evenly around the perimeter */
	int perimeter = (GB(r, 16, 16) % (2 * (maxx + maxy))) - maxy;
	DiagDirection dir;
	for (dir = DIAGDIR_NE; perimeter > 0; dir++) {
		perimeter -= (DiagDirToAxis(dir) == AXIS_X) ? maxx : maxy;
	}

	TileIndex tile;
	switch (dir) {
		default:
		case DIAGDIR_NE: tile = TileXY(maxx - 1, r % maxy); break;
		case DIAGDIR_SE: tile = TileXY(r % maxx, 1); break;
		case DIAGDIR_SW: tile = TileXY(1,        r % maxy); break;
		case DIAGDIR_NW: tile = TileXY(r % maxx, maxy - 1); break;
	}

	/* Only build lighthouses at tiles where the border is sea. */
	if (!IsTileType(tile, MP_WATER)) return false;

	for (int j = 0; j < 19; j++) {
		int h;
		if (IsTileType(tile, MP_CLEAR) && IsTileFlat(tile, &h) && h <= 2 && !IsBridgeAbove(tile)) {
			BuildObject(OBJECT_LIGHTHOUSE, tile);
			assert(tile < MapSize());
			return true;
		}
		tile += TileOffsByDiagDir(dir);
		if (!IsValidTile(tile)) return false;
	}
	return false;
}

/**
 * Try to build a transmitter.
 * @return True iff a transmitter was built.
 */
static bool TryBuildTransmitter()
{
	TileIndex tile = RandomTile();
	int h;
	if (IsTileType(tile, MP_CLEAR) && IsTileFlat(tile, &h) && h >= 4 && !IsBridgeAbove(tile)) {
		TileIndex t = tile;
		if (CircularTileSearch(&t, 9, HasTransmitter, nullptr)) return false;

		BuildObject(OBJECT_TRANSMITTER, tile);
		return true;
	}
	return false;
}

void GenerateObjects()
{
	/* Set a guestimate on how much we progress */
	SetGeneratingWorldProgress(GWP_OBJECT, NUM_OBJECTS);

	/* Determine number of water tiles at map border needed for freeform_edges */
	uint num_water_tiles = 0;
	if (_settings_game.construction.freeform_edges) {
		for (uint x = 0; x < MapMaxX(); x++) {
			if (IsTileType(TileXY(x, 1), MP_WATER)) num_water_tiles++;
			if (IsTileType(TileXY(x, MapMaxY() - 1), MP_WATER)) num_water_tiles++;
		}
		for (uint y = 1; y < MapMaxY() - 1; y++) {
			if (IsTileType(TileXY(1, y), MP_WATER)) num_water_tiles++;
			if (IsTileType(TileXY(MapMaxX() - 1, y), MP_WATER)) num_water_tiles++;
		}
	}

	/* Iterate over all possible object types */
	for (uint i = 0; i < NUM_OBJECTS; i++) {
		const ObjectSpec *spec = ObjectSpec::Get(i);

		/* Continue, if the object was never available till now or shall not be placed */
		if (!spec->WasEverAvailable() || spec->generate_amount == 0) continue;

		uint16 amount = spec->generate_amount;

		/* Scale by map size */
		if ((spec->flags & OBJECT_FLAG_SCALE_BY_WATER) && _settings_game.construction.freeform_edges) {
			/* Scale the amount of lighthouses with the amount of land at the borders.
			 * The -6 is because the top borders are MP_VOID (-2) and all corners
			 * are counted twice (-4). */
			amount = ScaleByMapSize1D(amount * num_water_tiles) / (2 * MapMaxY() + 2 * MapMaxX() - 6);
		} else if (spec->flags & OBJECT_FLAG_SCALE_BY_WATER) {
			amount = ScaleByMapSize1D(amount);
		} else {
			amount = ScaleByMapSize(amount);
		}

		/* Now try to place the requested amount of this object */
		for (uint j = ScaleByMapSize(1000); j != 0 && amount != 0 && Object::CanAllocateItem(); j--) {
			switch (i) {
				case OBJECT_TRANSMITTER:
					if (TryBuildTransmitter()) amount--;
					break;

				case OBJECT_LIGHTHOUSE:
					if (TryBuildLightHouse()) amount--;
					break;

				default:
					uint8 view = RandomRange(spec->views);
					if (CmdBuildObject(RandomTile(), DC_EXEC | DC_AUTO | DC_NO_TEST_TOWN_RATING | DC_NO_MODIFY_TOWN_RATING, i, view, nullptr).Succeeded()) amount--;
					break;
			}
		}
		IncreaseGeneratingWorldProgress(GWP_OBJECT);
	}
}

static void ChangeTileOwner_Object(TileIndex tile, Owner old_owner, Owner new_owner)
{
	if (!IsTileOwner(tile, old_owner)) return;

	bool do_clear = false;

	ObjectType type = GetObjectType(tile);
	if ((type == OBJECT_OWNED_LAND || type >= NEW_OBJECT_OFFSET) && new_owner != INVALID_OWNER) {
		SetTileOwner(tile, new_owner);
	} else if (type == OBJECT_STATUE) {
		Town *t = Object::GetByTile(tile)->town;
		ClrBit(t->statues, old_owner);
		if (new_owner != INVALID_OWNER && !HasBit(t->statues, new_owner)) {
			/* Transfer ownership to the new company */
			SetBit(t->statues, new_owner);
			SetTileOwner(tile, new_owner);
		} else {
			do_clear = true;
		}

		SetWindowDirty(WC_TOWN_AUTHORITY, t->index);
	} else {
		do_clear = true;
	}

	if (do_clear) {
		ReallyClearObjectTile(Object::GetByTile(tile));
		/* When clearing objects, they may turn into canal, which may require transferring ownership. */
		ChangeTileOwner(tile, old_owner, new_owner);
	}
}

static CommandCost TerraformTile_Object(TileIndex tile, DoCommandFlag flags, int z_new, Slope tileh_new)
{
	ObjectType type = GetObjectType(tile);

	if (type == OBJECT_OWNED_LAND) {
		/* Owned land remains unsold */
		CommandCost ret = CheckTileOwnership(tile);
		if (ret.Succeeded()) return CommandCost();
	} else if (AutoslopeEnabled() && type != OBJECT_TRANSMITTER && type != OBJECT_LIGHTHOUSE) {
		/* Behaviour:
		 *  - Both new and old slope must not be steep.
		 *  - TileMaxZ must not be changed.
		 *  - Allow autoslope by default.
		 *  - Disallow autoslope if callback succeeds and returns non-zero.
		 */
		Slope tileh_old = GetTileSlope(tile);
		/* TileMaxZ must not be changed. Slopes must not be steep. */
		if (!IsSteepSlope(tileh_old) && !IsSteepSlope(tileh_new) && (GetTileMaxZ(tile) == z_new + GetSlopeMaxZ(tileh_new))) {
			const ObjectSpec *spec = ObjectSpec::Get(type);

			/* Call callback 'disable autosloping for objects'. */
			if (HasBit(spec->callback_mask, CBM_OBJ_AUTOSLOPE)) {
				/* If the callback fails, allow autoslope. */
				uint16 res = GetObjectCallback(CBID_OBJECT_AUTOSLOPE, 0, 0, spec, Object::GetByTile(tile), tile);
				if (res == CALLBACK_FAILED || !ConvertBooleanCallback(spec->grf_prop.grffile, CBID_OBJECT_AUTOSLOPE, res)) return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_FOUNDATION]);
			} else if (spec->enabled) {
				/* allow autoslope */
				return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_FOUNDATION]);
			}
		}
	}

	return DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
}

extern const TileTypeProcs _tile_type_object_procs = {
	DrawTile_Object,             // draw_tile_proc
	GetSlopePixelZ_Object,       // get_slope_z_proc
	ClearTile_Object,            // clear_tile_proc
	AddAcceptedCargo_Object,     // add_accepted_cargo_proc
	GetTileDesc_Object,          // get_tile_desc_proc
	GetTileTrackStatus_Object,   // get_tile_track_status_proc
	ClickTile_Object,            // click_tile_proc
	AnimateTile_Object,          // animate_tile_proc
	TileLoop_Object,             // tile_loop_proc
	ChangeTileOwner_Object,      // change_tile_owner_proc
	AddProducedCargo_Object,     // add_produced_cargo_proc
	nullptr,                        // vehicle_enter_tile_proc
	GetFoundation_Object,        // get_foundation_proc
	TerraformTile_Object,        // terraform_tile_proc
};
