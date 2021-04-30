/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file industry_cmd.cpp Handling of industry tiles. */

#include "stdafx.h"
#include "clear_map.h"
#include "industry.h"
#include "station_base.h"
#include "landscape.h"
#include "viewport_func.h"
#include "command_func.h"
#include "town.h"
#include "news_func.h"
#include "cheat_type.h"
#include "company_base.h"
#include "genworld.h"
#include "tree_map.h"
#include "newgrf_cargo.h"
#include "newgrf_debug.h"
#include "newgrf_industrytiles.h"
#include "autoslope.h"
#include "water.h"
#include "strings_func.h"
#include "window_func.h"
#include "date_func.h"
#include "vehicle_func.h"
#include "sound_func.h"
#include "animated_tile_func.h"
#include "effectvehicle_func.h"
#include "effectvehicle_base.h"
#include "ai/ai.hpp"
#include "core/pool_func.hpp"
#include "subsidy_func.h"
#include "core/backup_type.hpp"
#include "object_base.h"
#include "game/game.hpp"
#include "error.h"
#include "cmd_helper.h"
#include "string_func.h"

#include "table/strings.h"
#include "table/industry_land.h"
#include "table/build_industry.h"

#include "safeguards.h"

IndustryPool _industry_pool("Industry");
INSTANTIATE_POOL_METHODS(Industry)

void ShowIndustryViewWindow(int industry);
void BuildOilRig(TileIndex tile);

static byte _industry_sound_ctr;
static TileIndex _industry_sound_tile;

uint16 Industry::counts[NUM_INDUSTRYTYPES];

IndustrySpec _industry_specs[NUM_INDUSTRYTYPES];
IndustryTileSpec _industry_tile_specs[NUM_INDUSTRYTILES];
IndustryBuildData _industry_builder; ///< In-game manager of industries.

/**
 * This function initialize the spec arrays of both
 * industry and industry tiles.
 * It adjusts the enabling of the industry too, based on climate availability.
 * This will allow for clearer testings
 */
void ResetIndustries()
{
	for (IndustryType i = 0; i < NUM_INDUSTRYTYPES; i++) {
		/* Reset the spec to default */
		if (i < lengthof(_origin_industry_specs)) {
			_industry_specs[i] = _origin_industry_specs[i];
		} else {
			_industry_specs[i] = IndustrySpec{};
		}

		/* Enable only the current climate industries */
		_industry_specs[i].enabled = i < NEW_INDUSTRYOFFSET &&
				HasBit(_origin_industry_specs[i].climate_availability, _settings_game.game_creation.landscape);
	}

	memset(&_industry_tile_specs, 0, sizeof(_industry_tile_specs));
	memcpy(&_industry_tile_specs, &_origin_industry_tile_specs, sizeof(_origin_industry_tile_specs));

	/* Reset any overrides that have been set. */
	_industile_mngr.ResetOverride();
	_industry_mngr.ResetOverride();
}

/**
 * Retrieve the type for this industry.  Although it is accessed by a tile,
 * it will return the general type of industry, and not the sprite index
 * as would do GetIndustryGfx.
 * @param tile that is queried
 * @pre IsTileType(tile, MP_INDUSTRY)
 * @return general type for this industry, as defined in industry.h
 */
IndustryType GetIndustryType(TileIndex tile)
{
	assert(IsTileType(tile, MP_INDUSTRY));

	const Industry *ind = Industry::GetByTile(tile);
	assert(ind != nullptr);
	return ind->type;
}

/**
 * Accessor for array _industry_specs.
 * This will ensure at once : proper access and
 * not allowing modifications of it.
 * @param thistype of industry (which is the index in _industry_specs)
 * @pre thistype < NUM_INDUSTRYTYPES
 * @return a pointer to the corresponding industry spec
 */
const IndustrySpec *GetIndustrySpec(IndustryType thistype)
{
	assert(thistype < NUM_INDUSTRYTYPES);
	return &_industry_specs[thistype];
}

/**
 * Accessor for array _industry_tile_specs.
 * This will ensure at once : proper access and
 * not allowing modifications of it.
 * @param gfx of industrytile (which is the index in _industry_tile_specs)
 * @pre gfx < INVALID_INDUSTRYTILE
 * @return a pointer to the corresponding industrytile spec
 */
const IndustryTileSpec *GetIndustryTileSpec(IndustryGfx gfx)
{
	assert(gfx < INVALID_INDUSTRYTILE);
	return &_industry_tile_specs[gfx];
}

Industry::~Industry()
{
	if (CleaningPool()) return;

	/* Industry can also be destroyed when not fully initialized.
	 * This means that we do not have to clear tiles either.
	 * Also we must not decrement industry counts in that case. */
	if (this->location.w == 0) return;

	const bool has_neutral_station = this->neutral_station != nullptr;

	TILE_AREA_LOOP(tile_cur, this->location) {
		if (IsTileType(tile_cur, MP_INDUSTRY)) {
			if (GetIndustryIndex(tile_cur) == this->index) {
				DeleteNewGRFInspectWindow(GSF_INDUSTRYTILES, tile_cur);

				/* MakeWaterKeepingClass() can also handle 'land' */
				MakeWaterKeepingClass(tile_cur, OWNER_NONE);
			}
		} else if (IsTileType(tile_cur, MP_STATION) && IsOilRig(tile_cur)) {
			DeleteOilRig(tile_cur);
		}
	}

	if (has_neutral_station) {
		/* Remove possible docking tiles */
		TILE_AREA_LOOP(tile_cur, this->location) {
			ClearDockingTilesCheckingNeighbours(tile_cur);
		}
	}

	if (GetIndustrySpec(this->type)->behaviour & INDUSTRYBEH_PLANT_FIELDS) {
		TileArea ta = TileArea(this->location.tile, 0, 0).Expand(21);

		/* Remove the farmland and convert it to regular tiles over time. */
		TILE_AREA_LOOP(tile_cur, ta) {
			if (IsTileType(tile_cur, MP_CLEAR) && IsClearGround(tile_cur, CLEAR_FIELDS) &&
					GetIndustryIndexOfField(tile_cur) == this->index) {
				SetIndustryIndexOfField(tile_cur, INVALID_INDUSTRY);
			}
		}
	}

	/* don't let any disaster vehicle target invalid industry */
	ReleaseDisastersTargetingIndustry(this->index);

	/* Clear the persistent storage. */
	delete this->psa;

	DecIndustryTypeCount(this->type);

	DeleteIndustryNews(this->index);
	DeleteWindowById(WC_INDUSTRY_VIEW, this->index);
	DeleteNewGRFInspectWindow(GSF_INDUSTRIES, this->index);

	DeleteSubsidyWith(ST_INDUSTRY, this->index);
	CargoPacket::InvalidateAllFrom(ST_INDUSTRY, this->index);

	for (Station *st : this->stations_near) {
		st->industries_near.erase(this);
	}
}

/**
 * Invalidating some stuff after removing item from the pool.
 * @param index index of deleted item
 */
void Industry::PostDestructor(size_t index)
{
	InvalidateWindowData(WC_INDUSTRY_DIRECTORY, 0, IDIWD_FORCE_REBUILD);
}


/**
 * Return a random valid industry.
 * @return random industry, nullptr if there are no industries
 */
/* static */ Industry *Industry::GetRandom()
{
	if (Industry::GetNumItems() == 0) return nullptr;
	int num = RandomRange((uint16)Industry::GetNumItems());
	size_t index = MAX_UVALUE(size_t);

	while (num >= 0) {
		num--;
		index++;

		/* Make sure we have a valid industry */
		while (!Industry::IsValidID(index)) {
			index++;
			assert(index < Industry::GetPoolSize());
		}
	}

	return Industry::Get(index);
}


static void IndustryDrawSugarMine(const TileInfo *ti)
{
	if (!IsIndustryCompleted(ti->tile)) return;

	const DrawIndustryAnimationStruct *d = &_draw_industry_spec1[GetAnimationFrame(ti->tile)];

	AddChildSpriteScreen(SPR_IT_SUGAR_MINE_SIEVE + d->image_1, PAL_NONE, d->x, 0);

	if (d->image_2 != 0) {
		AddChildSpriteScreen(SPR_IT_SUGAR_MINE_CLOUDS + d->image_2 - 1, PAL_NONE, 8, 41);
	}

	if (d->image_3 != 0) {
		AddChildSpriteScreen(SPR_IT_SUGAR_MINE_PILE + d->image_3 - 1, PAL_NONE,
			_drawtile_proc1[d->image_3 - 1].x, _drawtile_proc1[d->image_3 - 1].y);
	}
}

static void IndustryDrawToffeeQuarry(const TileInfo *ti)
{
	uint8 x = 0;

	if (IsIndustryCompleted(ti->tile)) {
		x = _industry_anim_offs_toffee[GetAnimationFrame(ti->tile)];
		if (x == 0xFF) {
			x = 0;
		}
	}

	AddChildSpriteScreen(SPR_IT_TOFFEE_QUARRY_SHOVEL, PAL_NONE, 22 - x, 24 + x);
	AddChildSpriteScreen(SPR_IT_TOFFEE_QUARRY_TOFFEE, PAL_NONE, 6, 14);
}

static void IndustryDrawBubbleGenerator( const TileInfo *ti)
{
	if (IsIndustryCompleted(ti->tile)) {
		AddChildSpriteScreen(SPR_IT_BUBBLE_GENERATOR_BUBBLE, PAL_NONE, 5, _industry_anim_offs_bubbles[GetAnimationFrame(ti->tile)]);
	}
	AddChildSpriteScreen(SPR_IT_BUBBLE_GENERATOR_SPRING, PAL_NONE, 3, 67);
}

static void IndustryDrawToyFactory(const TileInfo *ti)
{
	const DrawIndustryAnimationStruct *d = &_industry_anim_offs_toys[GetAnimationFrame(ti->tile)];

	if (d->image_1 != 0xFF) {
		AddChildSpriteScreen(SPR_IT_TOY_FACTORY_CLAY, PAL_NONE, d->x, 96 + d->image_1);
	}

	if (d->image_2 != 0xFF) {
		AddChildSpriteScreen(SPR_IT_TOY_FACTORY_ROBOT, PAL_NONE, 16 - d->image_2 * 2, 100 + d->image_2);
	}

	AddChildSpriteScreen(SPR_IT_TOY_FACTORY_STAMP, PAL_NONE, 7, d->image_3);
	AddChildSpriteScreen(SPR_IT_TOY_FACTORY_STAMP_HOLDER, PAL_NONE, 0, 42);
}

static void IndustryDrawCoalPlantSparks(const TileInfo *ti)
{
	if (IsIndustryCompleted(ti->tile)) {
		uint8 image = GetAnimationFrame(ti->tile);

		if (image != 0 && image < 7) {
			AddChildSpriteScreen(image + SPR_IT_POWER_PLANT_TRANSFORMERS,
				PAL_NONE,
				_coal_plant_sparks[image - 1].x,
				_coal_plant_sparks[image - 1].y
			);
		}
	}
}

typedef void IndustryDrawTileProc(const TileInfo *ti);
static IndustryDrawTileProc * const _industry_draw_tile_procs[5] = {
	IndustryDrawSugarMine,
	IndustryDrawToffeeQuarry,
	IndustryDrawBubbleGenerator,
	IndustryDrawToyFactory,
	IndustryDrawCoalPlantSparks,
};

static void DrawTile_Industry(TileInfo *ti)
{
	IndustryGfx gfx = GetIndustryGfx(ti->tile);
	Industry *ind = Industry::GetByTile(ti->tile);
	const IndustryTileSpec *indts = GetIndustryTileSpec(gfx);

	/* Retrieve pointer to the draw industry tile struct */
	if (gfx >= NEW_INDUSTRYTILEOFFSET) {
		/* Draw the tile using the specialized method of newgrf industrytile.
		 * DrawNewIndustry will return false if ever the resolver could not
		 * find any sprite to display.  So in this case, we will jump on the
		 * substitute gfx instead. */
		if (indts->grf_prop.spritegroup[0] != nullptr && DrawNewIndustryTile(ti, ind, gfx, indts)) {
			return;
		} else {
			/* No sprite group (or no valid one) found, meaning no graphics associated.
			 * Use the substitute one instead */
			if (indts->grf_prop.subst_id != INVALID_INDUSTRYTILE) {
				gfx = indts->grf_prop.subst_id;
				/* And point the industrytile spec accordingly */
				indts = GetIndustryTileSpec(gfx);
			}
		}
	}

	const DrawBuildingsTileStruct *dits = &_industry_draw_tile_data[gfx << 2 | (indts->anim_state ?
			GetAnimationFrame(ti->tile) & INDUSTRY_COMPLETED :
			GetIndustryConstructionStage(ti->tile))];

	SpriteID image = dits->ground.sprite;

	/* DrawFoundation() modifies ti->z and ti->tileh */
	if (ti->tileh != SLOPE_FLAT) DrawFoundation(ti, FOUNDATION_LEVELED);

	/* If the ground sprite is the default flat water sprite, draw also canal/river borders.
	 * Do not do this if the tile's WaterClass is 'land'. */
	if (image == SPR_FLAT_WATER_TILE && IsTileOnWater(ti->tile)) {
		DrawWaterClassGround(ti);
	} else {
		DrawGroundSprite(image, GroundSpritePaletteTransform(image, dits->ground.pal, GENERAL_SPRITE_COLOUR(ind->random_colour)));
	}

	/* If industries are transparent and invisible, do not draw the upper part */
	if (IsInvisibilitySet(TO_INDUSTRIES)) return;

	/* Add industry on top of the ground? */
	image = dits->building.sprite;
	if (image != 0) {
		AddSortableSpriteToDraw(image, SpriteLayoutPaletteTransform(image, dits->building.pal, GENERAL_SPRITE_COLOUR(ind->random_colour)),
			ti->x + dits->subtile_x,
			ti->y + dits->subtile_y,
			dits->width,
			dits->height,
			dits->dz,
			ti->z,
			IsTransparencySet(TO_INDUSTRIES));

		if (IsTransparencySet(TO_INDUSTRIES)) return;
	}

	{
		int proc = dits->draw_proc - 1;
		if (proc >= 0) _industry_draw_tile_procs[proc](ti);
	}
}

static int GetSlopePixelZ_Industry(TileIndex tile, uint x, uint y)
{
	return GetTileMaxPixelZ(tile);
}

static Foundation GetFoundation_Industry(TileIndex tile, Slope tileh)
{
	IndustryGfx gfx = GetIndustryGfx(tile);

	/* For NewGRF industry tiles we might not be drawing a foundation. We need to
	 * account for this, as other structures should
	 * draw the wall of the foundation in this case.
	 */
	if (gfx >= NEW_INDUSTRYTILEOFFSET) {
		const IndustryTileSpec *indts = GetIndustryTileSpec(gfx);
		if (indts->grf_prop.spritegroup[0] != nullptr && HasBit(indts->callback_mask, CBM_INDT_DRAW_FOUNDATIONS)) {
			uint32 callback_res = GetIndustryTileCallback(CBID_INDTILE_DRAW_FOUNDATIONS, 0, 0, gfx, Industry::GetByTile(tile), tile);
			if (callback_res != CALLBACK_FAILED && !ConvertBooleanCallback(indts->grf_prop.grffile, CBID_INDTILE_DRAW_FOUNDATIONS, callback_res)) return FOUNDATION_NONE;
		}
	}
	return FlatteningFoundation(tileh);
}

static void AddAcceptedCargo_Industry(TileIndex tile, CargoArray &acceptance, CargoTypes *always_accepted)
{
	IndustryGfx gfx = GetIndustryGfx(tile);
	const IndustryTileSpec *itspec = GetIndustryTileSpec(gfx);
	const Industry *ind = Industry::GetByTile(tile);

	/* Starting point for acceptance */
	CargoID accepts_cargo[lengthof(itspec->accepts_cargo)];
	int8 cargo_acceptance[lengthof(itspec->acceptance)];
	MemCpyT(accepts_cargo, itspec->accepts_cargo, lengthof(accepts_cargo));
	MemCpyT(cargo_acceptance, itspec->acceptance, lengthof(cargo_acceptance));

	if (itspec->special_flags & INDTILE_SPECIAL_ACCEPTS_ALL_CARGO) {
		/* Copy all accepted cargoes from industry itself */
		for (uint i = 0; i < lengthof(ind->accepts_cargo); i++) {
			CargoID *pos = std::find(accepts_cargo, endof(accepts_cargo), ind->accepts_cargo[i]);
			if (pos == endof(accepts_cargo)) {
				/* Not found, insert */
				pos = std::find(accepts_cargo, endof(accepts_cargo), CT_INVALID);
				if (pos == endof(accepts_cargo)) continue; // nowhere to place, give up on this one
				*pos = ind->accepts_cargo[i];
			}
			cargo_acceptance[pos - accepts_cargo] += 8;
		}
	}

	if (HasBit(itspec->callback_mask, CBM_INDT_ACCEPT_CARGO)) {
		/* Try callback for accepts list, if success override all existing accepts */
		uint16 res = GetIndustryTileCallback(CBID_INDTILE_ACCEPT_CARGO, 0, 0, gfx, Industry::GetByTile(tile), tile);
		if (res != CALLBACK_FAILED) {
			MemSetT(accepts_cargo, CT_INVALID, lengthof(accepts_cargo));
			for (uint i = 0; i < 3; i++) accepts_cargo[i] = GetCargoTranslation(GB(res, i * 5, 5), itspec->grf_prop.grffile);
		}
	}

	if (HasBit(itspec->callback_mask, CBM_INDT_CARGO_ACCEPTANCE)) {
		/* Try callback for acceptance list, if success override all existing acceptance */
		uint16 res = GetIndustryTileCallback(CBID_INDTILE_CARGO_ACCEPTANCE, 0, 0, gfx, Industry::GetByTile(tile), tile);
		if (res != CALLBACK_FAILED) {
			MemSetT(cargo_acceptance, 0, lengthof(cargo_acceptance));
			for (uint i = 0; i < 3; i++) cargo_acceptance[i] = GB(res, i * 4, 4);
		}
	}

	for (byte i = 0; i < lengthof(itspec->accepts_cargo); i++) {
		CargoID a = accepts_cargo[i];
		if (a == CT_INVALID || cargo_acceptance[i] <= 0) continue; // work only with valid cargoes

		/* Add accepted cargo */
		acceptance[a] += cargo_acceptance[i];

		/* Maybe set 'always accepted' bit (if it's not set already) */
		if (HasBit(*always_accepted, a)) continue;

		bool accepts = false;
		for (uint cargo_index = 0; cargo_index < lengthof(ind->accepts_cargo); cargo_index++) {
			/* Test whether the industry itself accepts the cargo type */
			if (ind->accepts_cargo[cargo_index] == a) {
				accepts = true;
				break;
			}
		}

		if (accepts) continue;

		/* If the industry itself doesn't accept this cargo, set 'always accepted' bit */
		SetBit(*always_accepted, a);
	}
}

static void GetTileDesc_Industry(TileIndex tile, TileDesc *td)
{
	const Industry *i = Industry::GetByTile(tile);
	const IndustrySpec *is = GetIndustrySpec(i->type);

	td->owner[0] = i->owner;
	td->str = is->name;
	if (!IsIndustryCompleted(tile)) {
		SetDParamX(td->dparam, 0, td->str);
		td->str = STR_LAI_TOWN_INDUSTRY_DESCRIPTION_UNDER_CONSTRUCTION;
	}

	if (is->grf_prop.grffile != nullptr) {
		td->grf = GetGRFConfig(is->grf_prop.grffile->grfid)->GetName();
	}
}

static CommandCost ClearTile_Industry(TileIndex tile, DoCommandFlag flags)
{
	Industry *i = Industry::GetByTile(tile);
	const IndustrySpec *indspec = GetIndustrySpec(i->type);

	/* water can destroy industries
	 * in editor you can bulldoze industries
	 * with magic_bulldozer cheat you can destroy industries
	 * (area around OILRIG is water, so water shouldn't flood it
	 */
	if ((_current_company != OWNER_WATER && _game_mode != GM_EDITOR &&
			!_cheats.magic_bulldozer.value) ||
			((flags & DC_AUTO) != 0) ||
			(_current_company == OWNER_WATER &&
				((indspec->behaviour & INDUSTRYBEH_BUILT_ONWATER) ||
				HasBit(GetIndustryTileSpec(GetIndustryGfx(tile))->slopes_refused, 5)))) {
		SetDParam(1, indspec->name);
		return_cmd_error(flags & DC_AUTO ? STR_ERROR_GENERIC_OBJECT_IN_THE_WAY : INVALID_STRING_ID);
	}

	if (flags & DC_EXEC) {
		AI::BroadcastNewEvent(new ScriptEventIndustryClose(i->index));
		Game::NewEvent(new ScriptEventIndustryClose(i->index));
		delete i;
	}
	return CommandCost(EXPENSES_CONSTRUCTION, indspec->GetRemovalCost());
}

/**
 * Move produced cargo from industry to nearby stations.
 * @param tile Industry tile
 * @return true if any cargo was moved.
 */
static bool TransportIndustryGoods(TileIndex tile)
{
	Industry *i = Industry::GetByTile(tile);
	const IndustrySpec *indspec = GetIndustrySpec(i->type);
	bool moved_cargo = false;

	for (uint j = 0; j < lengthof(i->produced_cargo_waiting); j++) {
		uint cw = std::min<uint>(i->produced_cargo_waiting[j], 255u);
		if (cw > indspec->minimal_cargo && i->produced_cargo[j] != CT_INVALID) {
			i->produced_cargo_waiting[j] -= cw;

			/* fluctuating economy? */
			if (EconomyIsInRecession()) cw = (cw + 1) / 2;

			i->this_month_production[j] += cw;

			uint am = MoveGoodsToStation(i->produced_cargo[j], cw, ST_INDUSTRY, i->index, &i->stations_near, i->exclusive_consumer);
			i->this_month_transported[j] += am;

			moved_cargo |= (am != 0);
		}
	}

	return moved_cargo;
}


static void AnimateTile_Industry(TileIndex tile)
{
	IndustryGfx gfx = GetIndustryGfx(tile);

	if (GetIndustryTileSpec(gfx)->animation.status != ANIM_STATUS_NO_ANIMATION) {
		AnimateNewIndustryTile(tile);
		return;
	}

	switch (gfx) {
	case GFX_SUGAR_MINE_SIEVE:
		if ((_tick_counter & 1) == 0) {
			byte m = GetAnimationFrame(tile) + 1;

			if (_settings_client.sound.ambient) {
				switch (m & 7) {
					case 2: SndPlayTileFx(SND_2D_SUGAR_MINE_1, tile); break;
					case 6: SndPlayTileFx(SND_29_SUGAR_MINE_2, tile); break;
				}
			}

			if (m >= 96) {
				m = 0;
				DeleteAnimatedTile(tile);
			}
			SetAnimationFrame(tile, m);

			MarkTileDirtyByTile(tile);
		}
		break;

	case GFX_TOFFEE_QUARY:
		if ((_tick_counter & 3) == 0) {
			byte m = GetAnimationFrame(tile);

			if (_industry_anim_offs_toffee[m] == 0xFF && _settings_client.sound.ambient) {
				SndPlayTileFx(SND_30_TOFFEE_QUARRY, tile);
			}

			if (++m >= 70) {
				m = 0;
				DeleteAnimatedTile(tile);
			}
			SetAnimationFrame(tile, m);

			MarkTileDirtyByTile(tile);
		}
		break;

	case GFX_BUBBLE_CATCHER:
		if ((_tick_counter & 1) == 0) {
			byte m = GetAnimationFrame(tile);

			if (++m >= 40) {
				m = 0;
				DeleteAnimatedTile(tile);
			}
			SetAnimationFrame(tile, m);

			MarkTileDirtyByTile(tile);
		}
		break;

	/* Sparks on a coal plant */
	case GFX_POWERPLANT_SPARKS:
		if ((_tick_counter & 3) == 0) {
			byte m = GetAnimationFrame(tile);
			if (m == 6) {
				SetAnimationFrame(tile, 0);
				DeleteAnimatedTile(tile);
			} else {
				SetAnimationFrame(tile, m + 1);
				MarkTileDirtyByTile(tile);
			}
		}
		break;

	case GFX_TOY_FACTORY:
		if ((_tick_counter & 1) == 0) {
			byte m = GetAnimationFrame(tile) + 1;

			switch (m) {
				case  1: if (_settings_client.sound.ambient) SndPlayTileFx(SND_2C_TOY_FACTORY_1, tile); break;
				case 23: if (_settings_client.sound.ambient) SndPlayTileFx(SND_2B_TOY_FACTORY_2, tile); break;
				case 28: if (_settings_client.sound.ambient) SndPlayTileFx(SND_2A_TOY_FACTORY_3, tile); break;
				default:
					if (m >= 50) {
						int n = GetIndustryAnimationLoop(tile) + 1;
						m = 0;
						if (n >= 8) {
							n = 0;
							DeleteAnimatedTile(tile);
						}
						SetIndustryAnimationLoop(tile, n);
					}
			}

			SetAnimationFrame(tile, m);
			MarkTileDirtyByTile(tile);
		}
		break;

	case GFX_PLASTIC_FOUNTAIN_ANIMATED_1: case GFX_PLASTIC_FOUNTAIN_ANIMATED_2:
	case GFX_PLASTIC_FOUNTAIN_ANIMATED_3: case GFX_PLASTIC_FOUNTAIN_ANIMATED_4:
	case GFX_PLASTIC_FOUNTAIN_ANIMATED_5: case GFX_PLASTIC_FOUNTAIN_ANIMATED_6:
	case GFX_PLASTIC_FOUNTAIN_ANIMATED_7: case GFX_PLASTIC_FOUNTAIN_ANIMATED_8:
		if ((_tick_counter & 3) == 0) {
			IndustryGfx gfx = GetIndustryGfx(tile);

			gfx = (gfx < 155) ? gfx + 1 : 148;
			SetIndustryGfx(tile, gfx);
			MarkTileDirtyByTile(tile);
		}
		break;

	case GFX_OILWELL_ANIMATED_1:
	case GFX_OILWELL_ANIMATED_2:
	case GFX_OILWELL_ANIMATED_3:
		if ((_tick_counter & 7) == 0) {
			bool b = Chance16(1, 7);
			IndustryGfx gfx = GetIndustryGfx(tile);

			byte m = GetAnimationFrame(tile) + 1;
			if (m == 4 && (m = 0, ++gfx) == GFX_OILWELL_ANIMATED_3 + 1 && (gfx = GFX_OILWELL_ANIMATED_1, b)) {
				SetIndustryGfx(tile, GFX_OILWELL_NOT_ANIMATED);
				SetIndustryConstructionStage(tile, 3);
				DeleteAnimatedTile(tile);
			} else {
				SetAnimationFrame(tile, m);
				SetIndustryGfx(tile, gfx);
				MarkTileDirtyByTile(tile);
			}
		}
		break;

	case GFX_COAL_MINE_TOWER_ANIMATED:
	case GFX_COPPER_MINE_TOWER_ANIMATED:
	case GFX_GOLD_MINE_TOWER_ANIMATED: {
			int state = _tick_counter & 0x7FF;

			if ((state -= 0x400) < 0) return;

			if (state < 0x1A0) {
				if (state < 0x20 || state >= 0x180) {
					byte m = GetAnimationFrame(tile);
					if (!(m & 0x40)) {
						SetAnimationFrame(tile, m | 0x40);
						if (_settings_client.sound.ambient) SndPlayTileFx(SND_0B_MINE, tile);
					}
					if (state & 7) return;
				} else {
					if (state & 3) return;
				}
				byte m = (GetAnimationFrame(tile) + 1) | 0x40;
				if (m > 0xC2) m = 0xC0;
				SetAnimationFrame(tile, m);
				MarkTileDirtyByTile(tile);
			} else if (state >= 0x200 && state < 0x3A0) {
				int i = (state < 0x220 || state >= 0x380) ? 7 : 3;
				if (state & i) return;

				byte m = (GetAnimationFrame(tile) & 0xBF) - 1;
				if (m < 0x80) m = 0x82;
				SetAnimationFrame(tile, m);
				MarkTileDirtyByTile(tile);
			}
			break;
		}
	}
}

static void CreateChimneySmoke(TileIndex tile)
{
	uint x = TileX(tile) * TILE_SIZE;
	uint y = TileY(tile) * TILE_SIZE;
	int z = GetTileMaxPixelZ(tile);

	CreateEffectVehicle(x + 15, y + 14, z + 59, EV_CHIMNEY_SMOKE);
}

static void MakeIndustryTileBigger(TileIndex tile)
{
	byte cnt = GetIndustryConstructionCounter(tile) + 1;
	if (cnt != 4) {
		SetIndustryConstructionCounter(tile, cnt);
		return;
	}

	byte stage = GetIndustryConstructionStage(tile) + 1;
	SetIndustryConstructionCounter(tile, 0);
	SetIndustryConstructionStage(tile, stage);
	StartStopIndustryTileAnimation(tile, IAT_CONSTRUCTION_STATE_CHANGE);
	if (stage == INDUSTRY_COMPLETED) SetIndustryCompleted(tile);

	MarkTileDirtyByTile(tile);

	if (!IsIndustryCompleted(tile)) return;

	IndustryGfx gfx = GetIndustryGfx(tile);
	if (gfx >= NEW_INDUSTRYTILEOFFSET) {
		/* New industries are already animated on construction. */
		return;
	}

	switch (gfx) {
	case GFX_POWERPLANT_CHIMNEY:
		CreateChimneySmoke(tile);
		break;

	case GFX_OILRIG_1: {
		/* Do not require an industry tile to be after the first two GFX_OILRIG_1
		 * tiles (like the default oil rig). Do a proper check to ensure the
		 * tiles belong to the same industry and based on that build the oil rig's
		 * station. */
		TileIndex other = tile + TileDiffXY(0, 1);

		if (IsTileType(other, MP_INDUSTRY) &&
				GetIndustryGfx(other) == GFX_OILRIG_1 &&
				GetIndustryIndex(tile) == GetIndustryIndex(other)) {
			BuildOilRig(tile);
		}
		break;
	}

	case GFX_TOY_FACTORY:
	case GFX_BUBBLE_CATCHER:
	case GFX_TOFFEE_QUARY:
		SetAnimationFrame(tile, 0);
		SetIndustryAnimationLoop(tile, 0);
		break;

	case GFX_PLASTIC_FOUNTAIN_ANIMATED_1: case GFX_PLASTIC_FOUNTAIN_ANIMATED_2:
	case GFX_PLASTIC_FOUNTAIN_ANIMATED_3: case GFX_PLASTIC_FOUNTAIN_ANIMATED_4:
	case GFX_PLASTIC_FOUNTAIN_ANIMATED_5: case GFX_PLASTIC_FOUNTAIN_ANIMATED_6:
	case GFX_PLASTIC_FOUNTAIN_ANIMATED_7: case GFX_PLASTIC_FOUNTAIN_ANIMATED_8:
		AddAnimatedTile(tile);
		break;
	}
}

static void TileLoopIndustry_BubbleGenerator(TileIndex tile)
{
	static const int8 _bubble_spawn_location[3][4] = {
		{ 11,   0, -4, -14 },
		{ -4, -10, -4,   1 },
		{ 49,  59, 60,  65 },
	};

	if (_settings_client.sound.ambient) SndPlayTileFx(SND_2E_BUBBLE_GENERATOR, tile);

	int dir = Random() & 3;

	EffectVehicle *v = CreateEffectVehicleAbove(
		TileX(tile) * TILE_SIZE + _bubble_spawn_location[0][dir],
		TileY(tile) * TILE_SIZE + _bubble_spawn_location[1][dir],
		_bubble_spawn_location[2][dir],
		EV_BUBBLE
	);

	if (v != nullptr) v->animation_substate = dir;
}

static void TileLoop_Industry(TileIndex tile)
{
	if (IsTileOnWater(tile)) TileLoop_Water(tile);

	/* Normally this doesn't happen, but if an industry NewGRF is removed
	 * an industry that was previously build on water can now be flooded.
	 * If this happens the tile is no longer an industry tile after
	 * returning from TileLoop_Water. */
	if (!IsTileType(tile, MP_INDUSTRY)) return;

	TriggerIndustryTile(tile, INDTILE_TRIGGER_TILE_LOOP);

	if (!IsIndustryCompleted(tile)) {
		MakeIndustryTileBigger(tile);
		return;
	}

	if (_game_mode == GM_EDITOR) return;

	if (TransportIndustryGoods(tile) && !StartStopIndustryTileAnimation(Industry::GetByTile(tile), IAT_INDUSTRY_DISTRIBUTES_CARGO)) {
		uint newgfx = GetIndustryTileSpec(GetIndustryGfx(tile))->anim_production;

		if (newgfx != INDUSTRYTILE_NOANIM) {
			ResetIndustryConstructionStage(tile);
			SetIndustryCompleted(tile);
			SetIndustryGfx(tile, newgfx);
			MarkTileDirtyByTile(tile);
			return;
		}
	}

	if (StartStopIndustryTileAnimation(tile, IAT_TILELOOP)) return;

	IndustryGfx newgfx = GetIndustryTileSpec(GetIndustryGfx(tile))->anim_next;
	if (newgfx != INDUSTRYTILE_NOANIM) {
		ResetIndustryConstructionStage(tile);
		SetIndustryGfx(tile, newgfx);
		MarkTileDirtyByTile(tile);
		return;
	}

	IndustryGfx gfx = GetIndustryGfx(tile);
	switch (gfx) {
	case GFX_COAL_MINE_TOWER_NOT_ANIMATED:
	case GFX_COPPER_MINE_TOWER_NOT_ANIMATED:
	case GFX_GOLD_MINE_TOWER_NOT_ANIMATED:
		if (!(_tick_counter & 0x400) && Chance16(1, 2)) {
			switch (gfx) {
				case GFX_COAL_MINE_TOWER_NOT_ANIMATED:   gfx = GFX_COAL_MINE_TOWER_ANIMATED;   break;
				case GFX_COPPER_MINE_TOWER_NOT_ANIMATED: gfx = GFX_COPPER_MINE_TOWER_ANIMATED; break;
				case GFX_GOLD_MINE_TOWER_NOT_ANIMATED:   gfx = GFX_GOLD_MINE_TOWER_ANIMATED;   break;
			}
			SetIndustryGfx(tile, gfx);
			SetAnimationFrame(tile, 0x80);
			AddAnimatedTile(tile);
		}
		break;

	case GFX_OILWELL_NOT_ANIMATED:
		if (Chance16(1, 6)) {
			SetIndustryGfx(tile, GFX_OILWELL_ANIMATED_1);
			SetAnimationFrame(tile, 0);
			AddAnimatedTile(tile);
		}
		break;

	case GFX_COAL_MINE_TOWER_ANIMATED:
	case GFX_COPPER_MINE_TOWER_ANIMATED:
	case GFX_GOLD_MINE_TOWER_ANIMATED:
		if (!(_tick_counter & 0x400)) {
			switch (gfx) {
				case GFX_COAL_MINE_TOWER_ANIMATED:   gfx = GFX_COAL_MINE_TOWER_NOT_ANIMATED;   break;
				case GFX_COPPER_MINE_TOWER_ANIMATED: gfx = GFX_COPPER_MINE_TOWER_NOT_ANIMATED; break;
				case GFX_GOLD_MINE_TOWER_ANIMATED:   gfx = GFX_GOLD_MINE_TOWER_NOT_ANIMATED;   break;
			}
			SetIndustryGfx(tile, gfx);
			SetIndustryCompleted(tile);
			SetIndustryConstructionStage(tile, 3);
			DeleteAnimatedTile(tile);
		}
		break;

	case GFX_POWERPLANT_SPARKS:
		if (Chance16(1, 3)) {
			if (_settings_client.sound.ambient) SndPlayTileFx(SND_0C_POWER_STATION, tile);
			AddAnimatedTile(tile);
		}
		break;

	case GFX_COPPER_MINE_CHIMNEY:
		CreateEffectVehicleAbove(TileX(tile) * TILE_SIZE + 6, TileY(tile) * TILE_SIZE + 6, 43, EV_COPPER_MINE_SMOKE);
		break;


	case GFX_TOY_FACTORY: {
			Industry *i = Industry::GetByTile(tile);
			if (i->was_cargo_delivered) {
				i->was_cargo_delivered = false;
				SetIndustryAnimationLoop(tile, 0);
				AddAnimatedTile(tile);
			}
		}
		break;

	case GFX_BUBBLE_GENERATOR:
		TileLoopIndustry_BubbleGenerator(tile);
		break;

	case GFX_TOFFEE_QUARY:
		AddAnimatedTile(tile);
		break;

	case GFX_SUGAR_MINE_SIEVE:
		if (Chance16(1, 3)) AddAnimatedTile(tile);
		break;
	}
}

static bool ClickTile_Industry(TileIndex tile)
{
	ShowIndustryViewWindow(GetIndustryIndex(tile));
	return true;
}

static TrackStatus GetTileTrackStatus_Industry(TileIndex tile, TransportType mode, uint sub_mode, DiagDirection side)
{
	return 0;
}

static void ChangeTileOwner_Industry(TileIndex tile, Owner old_owner, Owner new_owner)
{
	/* If the founder merges, the industry was created by the merged company */
	Industry *i = Industry::GetByTile(tile);
	if (i->founder == old_owner) i->founder = (new_owner == INVALID_OWNER) ? OWNER_NONE : new_owner;

	if (i->exclusive_supplier == old_owner) i->exclusive_supplier = new_owner;
	if (i->exclusive_consumer == old_owner) i->exclusive_consumer = new_owner;
}

/**
 * Check whether the tile is a forest.
 * @param tile the tile to investigate.
 * @return true if and only if the tile is a forest
 */
bool IsTileForestIndustry(TileIndex tile)
{
	/* Check for industry tile */
	if (!IsTileType(tile, MP_INDUSTRY)) return false;

	const Industry *ind = Industry::GetByTile(tile);

	/* Check for organic industry (i.e. not processing or extractive) */
	if ((GetIndustrySpec(ind->type)->life_type & INDUSTRYLIFE_ORGANIC) == 0) return false;

	/* Check for wood production */
	for (uint i = 0; i < lengthof(ind->produced_cargo); i++) {
		/* The industry produces wood. */
		if (ind->produced_cargo[i] != CT_INVALID && CargoSpec::Get(ind->produced_cargo[i])->label == 'WOOD') return true;
	}

	return false;
}

static const byte _plantfarmfield_type[] = {1, 1, 1, 1, 1, 3, 3, 4, 4, 4, 5, 5, 5, 6, 6, 6};

/**
 * Check whether the tile can be replaced by a farm field.
 * @param tile the tile to investigate.
 * @param allow_fields if true, the method will return true even if
 * the tile is a farm tile, otherwise the tile may not be a farm tile
 * @return true if the tile can become a farm field
 */
static bool IsSuitableForFarmField(TileIndex tile, bool allow_fields)
{
	switch (GetTileType(tile)) {
		case MP_CLEAR: return !IsClearGround(tile, CLEAR_SNOW) && !IsClearGround(tile, CLEAR_DESERT) && (allow_fields || !IsClearGround(tile, CLEAR_FIELDS));
		case MP_TREES: return GetTreeGround(tile) != TREE_GROUND_SHORE;
		default:       return false;
	}
}

/**
 * Build farm field fence
 * @param tile the tile to position the fence on
 * @param size the size of the field being planted in tiles
 * @param type type of fence to set
 * @param side the side of the tile to attempt placement
 */
static void SetupFarmFieldFence(TileIndex tile, int size, byte type, DiagDirection side)
{
	TileIndexDiff diff = (DiagDirToAxis(side) == AXIS_Y ? TileDiffXY(1, 0) : TileDiffXY(0, 1));

	do {
		tile = TILE_MASK(tile);

		if (IsTileType(tile, MP_CLEAR) && IsClearGround(tile, CLEAR_FIELDS)) {
			byte or_ = type;

			if (or_ == 1 && Chance16(1, 7)) or_ = 2;

			SetFence(tile, side, or_);
		}

		tile += diff;
	} while (--size);
}

static void PlantFarmField(TileIndex tile, IndustryID industry)
{
	if (_settings_game.game_creation.landscape == LT_ARCTIC) {
		if (GetTileZ(tile) + 2 >= GetSnowLine()) return;
	}

	/* determine field size */
	uint32 r = (Random() & 0x303) + 0x404;
	if (_settings_game.game_creation.landscape == LT_ARCTIC) r += 0x404;
	uint size_x = GB(r, 0, 8);
	uint size_y = GB(r, 8, 8);

	TileArea ta(tile - TileDiffXY(std::min(TileX(tile), size_x / 2), std::min(TileY(tile), size_y / 2)), size_x, size_y);
	ta.ClampToMap();

	if (ta.w == 0 || ta.h == 0) return;

	/* check the amount of bad tiles */
	int count = 0;
	TILE_AREA_LOOP(cur_tile, ta) {
		assert(cur_tile < MapSize());
		count += IsSuitableForFarmField(cur_tile, false);
	}
	if (count * 2 < ta.w * ta.h) return;

	/* determine type of field */
	r = Random();
	uint counter = GB(r, 5, 3);
	uint field_type = GB(r, 8, 8) * 9 >> 8;

	/* make field */
	TILE_AREA_LOOP(cur_tile, ta) {
		assert(cur_tile < MapSize());
		if (IsSuitableForFarmField(cur_tile, true)) {
			MakeField(cur_tile, field_type, industry);
			SetClearCounter(cur_tile, counter);
			MarkTileDirtyByTile(cur_tile);
		}
	}

	int type = 3;
	if (_settings_game.game_creation.landscape != LT_ARCTIC && _settings_game.game_creation.landscape != LT_TROPIC) {
		type = _plantfarmfield_type[Random() & 0xF];
	}

	SetupFarmFieldFence(ta.tile, ta.h, type, DIAGDIR_NE);
	SetupFarmFieldFence(ta.tile, ta.w, type, DIAGDIR_NW);
	SetupFarmFieldFence(ta.tile + TileDiffXY(ta.w - 1, 0), ta.h, type, DIAGDIR_SW);
	SetupFarmFieldFence(ta.tile + TileDiffXY(0, ta.h - 1), ta.w, type, DIAGDIR_SE);
}

void PlantRandomFarmField(const Industry *i)
{
	int x = i->location.w / 2 + Random() % 31 - 16;
	int y = i->location.h / 2 + Random() % 31 - 16;

	TileIndex tile = TileAddWrap(i->location.tile, x, y);

	if (tile != INVALID_TILE) PlantFarmField(tile, i->index);
}

/**
 * Search callback function for ChopLumberMillTrees
 * @param tile to test
 * @param user_data that is passed by the caller.  In this case, nothing
 * @return the result of the test
 */
static bool SearchLumberMillTrees(TileIndex tile, void *user_data)
{
	if (IsTileType(tile, MP_TREES) && GetTreeGrowth(tile) > 2) { ///< 3 and up means all fully grown trees
		/* found a tree */

		Backup<CompanyID> cur_company(_current_company, OWNER_NONE, FILE_LINE);

		_industry_sound_ctr = 1;
		_industry_sound_tile = tile;
		if (_settings_client.sound.ambient) SndPlayTileFx(SND_38_LUMBER_MILL_1, tile);

		DoCommand(tile, 0, 0, DC_EXEC, CMD_LANDSCAPE_CLEAR);

		cur_company.Restore();
		return true;
	}
	return false;
}

/**
 * Perform a circular search around the Lumber Mill in order to find trees to cut
 * @param i industry
 */
static void ChopLumberMillTrees(Industry *i)
{
	/* We only want to cut trees if all tiles are completed. */
	TILE_AREA_LOOP(tile_cur, i->location) {
		if (i->TileBelongsToIndustry(tile_cur)) {
			if (!IsIndustryCompleted(tile_cur)) return;
		}
	}

	TileIndex tile = i->location.tile;
	if (CircularTileSearch(&tile, 40, SearchLumberMillTrees, nullptr)) { // 40x40 tiles  to search.
		i->produced_cargo_waiting[0] = std::min(0xffff, i->produced_cargo_waiting[0] + 45); // Found a tree, add according value to waiting cargo.
	}
}

static void ProduceIndustryGoods(Industry *i)
{
	const IndustrySpec *indsp = GetIndustrySpec(i->type);

	/* play a sound? */
	if ((i->counter & 0x3F) == 0) {
		uint32 r;
		if (Chance16R(1, 14, r) && indsp->number_of_sounds != 0 && _settings_client.sound.ambient) {
			for (size_t j = 0; j < lengthof(i->past_production[0]); j++) {
				if (i->past_production[0][j] > 0) {
					/* Play sound since last month had production */
					SndPlayTileFx(
						(SoundFx)(indsp->random_sounds[((r >> 16) * indsp->number_of_sounds) >> 16]),
						i->location.tile);
					break;
				}
			}
		}
	}

	i->counter--;

	/* produce some cargo */
	if ((i->counter % INDUSTRY_PRODUCE_TICKS) == 0) {
		if (HasBit(indsp->callback_mask, CBM_IND_PRODUCTION_256_TICKS)) IndustryProductionCallback(i, 1);

		IndustryBehaviour indbehav = indsp->behaviour;
		for (size_t j = 0; j < lengthof(i->produced_cargo_waiting); j++) {
			i->produced_cargo_waiting[j] = std::min(0xffff, i->produced_cargo_waiting[j] + i->production_rate[j]);
		}

		if ((indbehav & INDUSTRYBEH_PLANT_FIELDS) != 0) {
			uint16 cb_res = CALLBACK_FAILED;
			if (HasBit(indsp->callback_mask, CBM_IND_SPECIAL_EFFECT)) {
				cb_res = GetIndustryCallback(CBID_INDUSTRY_SPECIAL_EFFECT, Random(), 0, i, i->type, i->location.tile);
			}

			bool plant;
			if (cb_res != CALLBACK_FAILED) {
				plant = ConvertBooleanCallback(indsp->grf_prop.grffile, CBID_INDUSTRY_SPECIAL_EFFECT, cb_res);
			} else {
				plant = Chance16(1, 8);
			}

			if (plant) PlantRandomFarmField(i);
		}
		if ((indbehav & INDUSTRYBEH_CUT_TREES) != 0) {
			uint16 cb_res = CALLBACK_FAILED;
			if (HasBit(indsp->callback_mask, CBM_IND_SPECIAL_EFFECT)) {
				cb_res = GetIndustryCallback(CBID_INDUSTRY_SPECIAL_EFFECT, Random(), 1, i, i->type, i->location.tile);
			}

			bool cut;
			if (cb_res != CALLBACK_FAILED) {
				cut = ConvertBooleanCallback(indsp->grf_prop.grffile, CBID_INDUSTRY_SPECIAL_EFFECT, cb_res);
			} else {
				cut = ((i->counter % INDUSTRY_CUT_TREE_TICKS) == 0);
			}

			if (cut) ChopLumberMillTrees(i);
		}

		TriggerIndustry(i, INDUSTRY_TRIGGER_INDUSTRY_TICK);
		StartStopIndustryTileAnimation(i, IAT_INDUSTRY_TICK);
	}
}

void OnTick_Industry()
{
	if (_industry_sound_ctr != 0) {
		_industry_sound_ctr++;

		if (_industry_sound_ctr == 75) {
			if (_settings_client.sound.ambient) SndPlayTileFx(SND_37_LUMBER_MILL_2, _industry_sound_tile);
		} else if (_industry_sound_ctr == 160) {
			_industry_sound_ctr = 0;
			if (_settings_client.sound.ambient) SndPlayTileFx(SND_36_LUMBER_MILL_3, _industry_sound_tile);
		}
	}

	if (_game_mode == GM_EDITOR) return;

	for (Industry *i : Industry::Iterate()) {
		ProduceIndustryGoods(i);
	}
}

/**
 * Check the conditions of #CHECK_NOTHING (Always succeeds).
 * @param tile %Tile to perform the checking.
 * @return Succeeded or failed command.
 */
static CommandCost CheckNewIndustry_NULL(TileIndex tile)
{
	return CommandCost();
}

/**
 * Check the conditions of #CHECK_FOREST (Industry should be build above snow-line in arctic climate).
 * @param tile %Tile to perform the checking.
 * @return Succeeded or failed command.
 */
static CommandCost CheckNewIndustry_Forest(TileIndex tile)
{
	if (_settings_game.game_creation.landscape == LT_ARCTIC) {
		if (GetTileZ(tile) < HighestSnowLine() + 2) {
			return_cmd_error(STR_ERROR_FOREST_CAN_ONLY_BE_PLANTED);
		}
	}
	return CommandCost();
}

/**
 * Check if a tile is within a distance from map edges, scaled by map dimensions independently.
 * Each dimension is checked independently, and dimensions smaller than 256 are not scaled.
 * @param tile Which tile to check distance of.
 * @param maxdist Normal distance on a 256x256 map.
 * @return True if the tile is near the map edge.
 */
static bool CheckScaledDistanceFromEdge(TileIndex tile, uint maxdist)
{
	uint maxdist_x = maxdist;
	uint maxdist_y = maxdist;

	if (MapSizeX() > 256) maxdist_x *= MapSizeX() / 256;
	if (MapSizeY() > 256) maxdist_y *= MapSizeY() / 256;

	if (DistanceFromEdgeDir(tile, DIAGDIR_NE) < maxdist_x) return true;
	if (DistanceFromEdgeDir(tile, DIAGDIR_NW) < maxdist_y) return true;
	if (DistanceFromEdgeDir(tile, DIAGDIR_SW) < maxdist_x) return true;
	if (DistanceFromEdgeDir(tile, DIAGDIR_SE) < maxdist_y) return true;

	return false;
}

/**
 * Check the conditions of #CHECK_REFINERY (Industry should be positioned near edge of the map).
 * @param tile %Tile to perform the checking.
 * @return Succeeded or failed command.
 */
static CommandCost CheckNewIndustry_OilRefinery(TileIndex tile)
{
	if (_game_mode == GM_EDITOR) return CommandCost();

	if (CheckScaledDistanceFromEdge(TILE_ADDXY(tile, 1, 1), _settings_game.game_creation.oil_refinery_limit)) return CommandCost();

	return_cmd_error(STR_ERROR_CAN_ONLY_BE_POSITIONED);
}

extern bool _ignore_restrictions;

/**
 * Check the conditions of #CHECK_OIL_RIG (Industries at sea should be positioned near edge of the map).
 * @param tile %Tile to perform the checking.
 * @return Succeeded or failed command.
 */
static CommandCost CheckNewIndustry_OilRig(TileIndex tile)
{
	if (_game_mode == GM_EDITOR && _ignore_restrictions) return CommandCost();

	if (TileHeight(tile) == 0 &&
			CheckScaledDistanceFromEdge(TILE_ADDXY(tile, 1, 1), _settings_game.game_creation.oil_refinery_limit)) return CommandCost();

	return_cmd_error(STR_ERROR_CAN_ONLY_BE_POSITIONED);
}

/**
 * Check the conditions of #CHECK_FARM (Industry should be below snow-line in arctic).
 * @param tile %Tile to perform the checking.
 * @return Succeeded or failed command.
 */
static CommandCost CheckNewIndustry_Farm(TileIndex tile)
{
	if (_settings_game.game_creation.landscape == LT_ARCTIC) {
		if (GetTileZ(tile) + 2 >= HighestSnowLine()) {
			return_cmd_error(STR_ERROR_SITE_UNSUITABLE);
		}
	}
	return CommandCost();
}

/**
 * Check the conditions of #CHECK_PLANTATION (Industry should NOT be in the desert).
 * @param tile %Tile to perform the checking.
 * @return Succeeded or failed command.
 */
static CommandCost CheckNewIndustry_Plantation(TileIndex tile)
{
	if (GetTropicZone(tile) == TROPICZONE_DESERT) {
		return_cmd_error(STR_ERROR_SITE_UNSUITABLE);
	}
	return CommandCost();
}

/**
 * Check the conditions of #CHECK_WATER (Industry should be in the desert).
 * @param tile %Tile to perform the checking.
 * @return Succeeded or failed command.
 */
static CommandCost CheckNewIndustry_Water(TileIndex tile)
{
	if (GetTropicZone(tile) != TROPICZONE_DESERT) {
		return_cmd_error(STR_ERROR_CAN_ONLY_BE_BUILT_IN_DESERT);
	}
	return CommandCost();
}

/**
 * Check the conditions of #CHECK_LUMBERMILL (Industry should be in the rain forest).
 * @param tile %Tile to perform the checking.
 * @return Succeeded or failed command.
 */
static CommandCost CheckNewIndustry_Lumbermill(TileIndex tile)
{
	if (GetTropicZone(tile) != TROPICZONE_RAINFOREST) {
		return_cmd_error(STR_ERROR_CAN_ONLY_BE_BUILT_IN_RAINFOREST);
	}
	return CommandCost();
}

/**
 * Check the conditions of #CHECK_BUBBLEGEN (Industry should be in low land).
 * @param tile %Tile to perform the checking.
 * @return Succeeded or failed command.
 */
static CommandCost CheckNewIndustry_BubbleGen(TileIndex tile)
{
	if (GetTileZ(tile) > 4) {
		return_cmd_error(STR_ERROR_CAN_ONLY_BE_BUILT_IN_LOW_AREAS);
	}
	return CommandCost();
}

/**
 * Industrytype check function signature.
 * @param tile %Tile to check.
 * @return Succeeded or failed command.
 */
typedef CommandCost CheckNewIndustryProc(TileIndex tile);

/** Check functions for different types of industry. */
static CheckNewIndustryProc * const _check_new_industry_procs[CHECK_END] = {
	CheckNewIndustry_NULL,        ///< CHECK_NOTHING
	CheckNewIndustry_Forest,      ///< CHECK_FOREST
	CheckNewIndustry_OilRefinery, ///< CHECK_REFINERY
	CheckNewIndustry_Farm,        ///< CHECK_FARM
	CheckNewIndustry_Plantation,  ///< CHECK_PLANTATION
	CheckNewIndustry_Water,       ///< CHECK_WATER
	CheckNewIndustry_Lumbermill,  ///< CHECK_LUMBERMILL
	CheckNewIndustry_BubbleGen,   ///< CHECK_BUBBLEGEN
	CheckNewIndustry_OilRig,      ///< CHECK_OIL_RIG
};

/**
 * Find a town for the industry, while checking for multiple industries in the same town.
 * @param tile Position of the industry to build.
 * @param type Industry type.
 * @param[out] t Pointer to return town for the new industry, \c nullptr is written if no good town can be found.
 * @return Succeeded or failed command.
 *
 * @pre \c *t != nullptr
 * @post \c *t points to a town on success, and \c nullptr on failure.
 */
static CommandCost FindTownForIndustry(TileIndex tile, int type, Town **t)
{
	*t = ClosestTownFromTile(tile, UINT_MAX);

	if (_settings_game.economy.multiple_industry_per_town) return CommandCost();

	for (const Industry *i : Industry::Iterate()) {
		if (i->type == (byte)type && i->town == *t) {
			*t = nullptr;
			return_cmd_error(STR_ERROR_ONLY_ONE_ALLOWED_PER_TOWN);
		}
	}

	return CommandCost();
}

bool IsSlopeRefused(Slope current, Slope refused)
{
	if (IsSteepSlope(current)) return true;
	if (current != SLOPE_FLAT) {
		if (IsSteepSlope(refused)) return true;

		Slope t = ComplementSlope(current);

		if ((refused & SLOPE_W) && (t & SLOPE_NW)) return true;
		if ((refused & SLOPE_S) && (t & SLOPE_NE)) return true;
		if ((refused & SLOPE_E) && (t & SLOPE_SW)) return true;
		if ((refused & SLOPE_N) && (t & SLOPE_SE)) return true;
	}

	return false;
}

/**
 * Are the tiles of the industry free?
 * @param tile                    Position to check.
 * @param layout                  Industry tiles table.
 * @param layout_index            The index of the layout to build/fund
 * @param type                    Type of the industry.
 * @param initial_random_bits     The random bits the industry is going to have after construction.
 * @param founder                 Industry founder
 * @param creation_type           The circumstances the industry is created under.
 * @param[out] custom_shape_check Perform custom check for the site.
 * @return Failed or succeeded command.
 */
static CommandCost CheckIfIndustryTilesAreFree(TileIndex tile, const IndustryTileLayout &layout, size_t layout_index, int type, uint16 initial_random_bits, Owner founder, IndustryAvailabilityCallType creation_type, bool *custom_shape_check = nullptr)
{
	bool refused_slope = false;
	bool custom_shape = false;

	for (const IndustryTileLayoutTile &it : layout) {
		IndustryGfx gfx = GetTranslatedIndustryTileID(it.gfx);
		TileIndex cur_tile = TileAddWrap(tile, it.ti.x, it.ti.y);

		if (!IsValidTile(cur_tile)) {
			return_cmd_error(STR_ERROR_SITE_UNSUITABLE);
		}

		if (gfx == GFX_WATERTILE_SPECIALCHECK) {
			if (!IsWaterTile(cur_tile) ||
					!IsTileFlat(cur_tile)) {
				return_cmd_error(STR_ERROR_SITE_UNSUITABLE);
			}
		} else {
			CommandCost ret = EnsureNoVehicleOnGround(cur_tile);
			if (ret.Failed()) return ret;
			if (IsBridgeAbove(cur_tile)) return_cmd_error(STR_ERROR_SITE_UNSUITABLE);

			const IndustryTileSpec *its = GetIndustryTileSpec(gfx);

			IndustryBehaviour ind_behav = GetIndustrySpec(type)->behaviour;

			/* Perform land/water check if not disabled */
			if (!HasBit(its->slopes_refused, 5) && ((HasTileWaterClass(cur_tile) && IsTileOnWater(cur_tile)) == !(ind_behav & INDUSTRYBEH_BUILT_ONWATER))) return_cmd_error(STR_ERROR_SITE_UNSUITABLE);

			if (HasBit(its->callback_mask, CBM_INDT_SHAPE_CHECK)) {
				custom_shape = true;
				CommandCost ret = PerformIndustryTileSlopeCheck(tile, cur_tile, its, type, gfx, layout_index, initial_random_bits, founder, creation_type);
				if (ret.Failed()) return ret;
			} else {
				Slope tileh = GetTileSlope(cur_tile);
				refused_slope |= IsSlopeRefused(tileh, its->slopes_refused);
			}

			if ((ind_behav & (INDUSTRYBEH_ONLY_INTOWN | INDUSTRYBEH_TOWN1200_MORE)) || // Tile must be a house
					((ind_behav & INDUSTRYBEH_ONLY_NEARTOWN) && IsTileType(cur_tile, MP_HOUSE))) { // Tile is allowed to be a house (and it is a house)
				if (!IsTileType(cur_tile, MP_HOUSE)) {
					return_cmd_error(STR_ERROR_CAN_ONLY_BE_BUILT_IN_TOWNS);
				}

				/* Clear the tiles as OWNER_TOWN to not affect town rating, and to not clear protected buildings */
				Backup<CompanyID> cur_company(_current_company, OWNER_TOWN, FILE_LINE);
				CommandCost ret = DoCommand(cur_tile, 0, 0, DC_NONE, CMD_LANDSCAPE_CLEAR);
				cur_company.Restore();

				if (ret.Failed()) return ret;
			} else {
				/* Clear the tiles, but do not affect town ratings */
				CommandCost ret = DoCommand(cur_tile, 0, 0, DC_AUTO | DC_NO_TEST_TOWN_RATING | DC_NO_MODIFY_TOWN_RATING, CMD_LANDSCAPE_CLEAR);

				if (ret.Failed()) return ret;
			}
		}
	}

	if (custom_shape_check != nullptr) *custom_shape_check = custom_shape;

	/* It is almost impossible to have a fully flat land in TG, so what we
	 *  do is that we check if we can make the land flat later on. See
	 *  CheckIfCanLevelIndustryPlatform(). */
	if (!refused_slope || (_settings_game.game_creation.land_generator == LG_TERRAGENESIS && _generating_world && !custom_shape && !_ignore_restrictions)) {
		return CommandCost();
	}
	return_cmd_error(STR_ERROR_SITE_UNSUITABLE);
}

/**
 * Is the industry allowed to be built at this place for the town?
 * @param tile Tile to construct the industry.
 * @param type Type of the industry.
 * @param t    Town authority that the industry belongs to.
 * @return Succeeded or failed command.
 */
static CommandCost CheckIfIndustryIsAllowed(TileIndex tile, int type, const Town *t)
{
	if ((GetIndustrySpec(type)->behaviour & INDUSTRYBEH_TOWN1200_MORE) && t->cache.population < 1200) {
		return_cmd_error(STR_ERROR_CAN_ONLY_BE_BUILT_IN_TOWNS_WITH_POPULATION_OF_1200);
	}

	if ((GetIndustrySpec(type)->behaviour & INDUSTRYBEH_ONLY_NEARTOWN) && DistanceMax(t->xy, tile) > 9) {
		return_cmd_error(STR_ERROR_CAN_ONLY_BE_BUILT_NEAR_TOWN_CENTER);
	}

	return CommandCost();
}

static bool CheckCanTerraformSurroundingTiles(TileIndex tile, uint height, int internal)
{
	/* Check if we don't leave the map */
	if (TileX(tile) == 0 || TileY(tile) == 0 || GetTileType(tile) == MP_VOID) return false;

	TileArea ta(tile - TileDiffXY(1, 1), 2, 2);
	TILE_AREA_LOOP(tile_walk, ta) {
		uint curh = TileHeight(tile_walk);
		/* Is the tile clear? */
		if ((GetTileType(tile_walk) != MP_CLEAR) && (GetTileType(tile_walk) != MP_TREES)) return false;

		/* Don't allow too big of a change if this is the sub-tile check */
		if (internal != 0 && Delta(curh, height) > 1) return false;

		/* Different height, so the surrounding tiles of this tile
		 *  has to be correct too (in level, or almost in level)
		 *  else you get a chain-reaction of terraforming. */
		if (internal == 0 && curh != height) {
			if (TileX(tile_walk) == 0 || TileY(tile_walk) == 0 || !CheckCanTerraformSurroundingTiles(tile_walk + TileDiffXY(-1, -1), height, internal + 1)) {
				return false;
			}
		}
	}

	return true;
}

/**
 * This function tries to flatten out the land below an industry, without
 *  damaging the surroundings too much.
 */
static bool CheckIfCanLevelIndustryPlatform(TileIndex tile, DoCommandFlag flags, const IndustryTileLayout &layout, int type)
{
	int max_x = 0;
	int max_y = 0;

	/* Finds dimensions of largest variant of this industry */
	for (const IndustryTileLayoutTile &it : layout) {
		if (it.gfx == GFX_WATERTILE_SPECIALCHECK) continue; // watercheck tiles don't count for footprint size
		if (it.ti.x > max_x) max_x = it.ti.x;
		if (it.ti.y > max_y) max_y = it.ti.y;
	}

	/* Remember level height */
	uint h = TileHeight(tile);

	if (TileX(tile) <= _settings_game.construction.industry_platform + 1U || TileY(tile) <= _settings_game.construction.industry_platform + 1U) return false;
	/* Check that all tiles in area and surrounding are clear
	 * this determines that there are no obstructing items */

	/* TileArea::Expand is not used here as we need to abort
	 * instead of clamping if the bounds cannot expanded. */
	TileArea ta(tile + TileDiffXY(-_settings_game.construction.industry_platform, -_settings_game.construction.industry_platform),
			max_x + 2 + 2 * _settings_game.construction.industry_platform, max_y + 2 + 2 * _settings_game.construction.industry_platform);

	if (TileX(ta.tile) + ta.w >= MapMaxX() || TileY(ta.tile) + ta.h >= MapMaxY()) return false;

	/* _current_company is OWNER_NONE for randomly generated industries and in editor, or the company who funded or prospected the industry.
	 * Perform terraforming as OWNER_TOWN to disable autoslope and town ratings. */
	Backup<CompanyID> cur_company(_current_company, OWNER_TOWN, FILE_LINE);

	TILE_AREA_LOOP(tile_walk, ta) {
		uint curh = TileHeight(tile_walk);
		if (curh != h) {
			/* This tile needs terraforming. Check if we can do that without
			 *  damaging the surroundings too much. */
			if (!CheckCanTerraformSurroundingTiles(tile_walk, h, 0)) {
				cur_company.Restore();
				return false;
			}
			/* This is not 100% correct check, but the best we can do without modifying the map.
			 *  What is missing, is if the difference in height is more than 1.. */
			if (DoCommand(tile_walk, SLOPE_N, (curh > h) ? 0 : 1, flags & ~DC_EXEC, CMD_TERRAFORM_LAND).Failed()) {
				cur_company.Restore();
				return false;
			}
		}
	}

	if (flags & DC_EXEC) {
		/* Terraform the land under the industry */
		TILE_AREA_LOOP(tile_walk, ta) {
			uint curh = TileHeight(tile_walk);
			while (curh != h) {
				/* We give the terraforming for free here, because we can't calculate
				 *  exact cost in the test-round, and as we all know, that will cause
				 *  a nice assert if they don't match ;) */
				DoCommand(tile_walk, SLOPE_N, (curh > h) ? 0 : 1, flags, CMD_TERRAFORM_LAND);
				curh += (curh > h) ? -1 : 1;
			}
		}
	}

	cur_company.Restore();
	return true;
}


/**
 * Check that the new industry is far enough from conflicting industries.
 * @param tile Tile to construct the industry.
 * @param type Type of the new industry.
 * @return Succeeded or failed command.
 */
static CommandCost CheckIfFarEnoughFromConflictingIndustry(TileIndex tile, int type)
{
	const IndustrySpec *indspec = GetIndustrySpec(type);

	/* On a large map with many industries, it may be faster to check an area. */
	static const int dmax = 14;
	if (Industry::GetNumItems() > (size_t) (dmax * dmax * 2)) {
		const Industry* i = nullptr;
		TileArea tile_area = TileArea(tile, 1, 1).Expand(dmax);
		TILE_AREA_LOOP(atile, tile_area) {
			if (GetTileType(atile) == MP_INDUSTRY) {
				const Industry *i2 = Industry::GetByTile(atile);
				if (i == i2) continue;
				i = i2;
				if (DistanceMax(tile, i->location.tile) > (uint)dmax) continue;
				if (i->type == indspec->conflicting[0] ||
						i->type == indspec->conflicting[1] ||
						i->type == indspec->conflicting[2]) {
					return_cmd_error(STR_ERROR_INDUSTRY_TOO_CLOSE);
				}
			}
		}
		return CommandCost();
	}

	for (const Industry *i : Industry::Iterate()) {
		/* Within 14 tiles from another industry is considered close */
		if (DistanceMax(tile, i->location.tile) > 14) continue;

		/* check if there are any conflicting industry types around */
		if (i->type == indspec->conflicting[0] ||
				i->type == indspec->conflicting[1] ||
				i->type == indspec->conflicting[2]) {
			return_cmd_error(STR_ERROR_INDUSTRY_TOO_CLOSE);
		}
	}
	return CommandCost();
}

/**
 * Advertise about a new industry opening.
 * @param ind Industry being opened.
 */
static void AdvertiseIndustryOpening(const Industry *ind)
{
	const IndustrySpec *ind_spc = GetIndustrySpec(ind->type);
	SetDParam(0, ind_spc->name);
	if (ind_spc->new_industry_text > STR_LAST_STRINGID) {
		SetDParam(1, STR_TOWN_NAME);
		SetDParam(2, ind->town->index);
	} else {
		SetDParam(1, ind->town->index);
	}
	AddIndustryNewsItem(ind_spc->new_industry_text, NT_INDUSTRY_OPEN, ind->index);
	AI::BroadcastNewEvent(new ScriptEventIndustryOpen(ind->index));
	Game::NewEvent(new ScriptEventIndustryOpen(ind->index));
}

/**
 * Populate an industry's list of nearby stations, and if it accepts any cargo, also
 * add the industry to each station's nearby industry list.
 * @param ind Industry
 */
static void PopulateStationsNearby(Industry *ind)
{
	if (ind->neutral_station != nullptr && !_settings_game.station.serve_neutral_industries) {
		/* Industry has a neutral station. Use it and ignore any other nearby stations. */
		ind->stations_near.insert(ind->neutral_station);
		ind->neutral_station->industries_near.clear();
		ind->neutral_station->industries_near.insert(ind);
		return;
	}

	ForAllStationsAroundTiles(ind->location, [ind](Station *st, TileIndex tile) {
		if (!IsTileType(tile, MP_INDUSTRY) || GetIndustryIndex(tile) != ind->index) return false;
		ind->stations_near.insert(st);
		st->AddIndustryToDeliver(ind);
		return true;
	});
}

/**
 * Put an industry on the map.
 * @param i                   Just allocated poolitem, mostly empty.
 * @param tile                North tile of the industry.
 * @param type                Type of the industry.
 * @param layout              Industrylayout to build.
 * @param layout_index        Number of the industry layout.
 * @param t                   Nearest town.
 * @param founder             Founder of the industry; OWNER_NONE in case of random construction.
 * @param initial_random_bits Random bits for the industry.
 */
static void DoCreateNewIndustry(Industry *i, TileIndex tile, IndustryType type, const IndustryTileLayout &layout, size_t layout_index, Town *t, Owner founder, uint16 initial_random_bits)
{
	const IndustrySpec *indspec = GetIndustrySpec(type);

	i->location = TileArea(tile, 1, 1);
	i->type = type;
	Industry::IncIndustryTypeCount(type);

	MemCpyT(i->produced_cargo,  indspec->produced_cargo,  lengthof(i->produced_cargo));
	MemCpyT(i->production_rate, indspec->production_rate, lengthof(i->production_rate));
	MemCpyT(i->accepts_cargo,   indspec->accepts_cargo,   lengthof(i->accepts_cargo));

	MemSetT(i->produced_cargo_waiting,     0, lengthof(i->produced_cargo_waiting));
	MemSetT(i->this_month_production,      0, lengthof(i->this_month_production));
	MemSetT(i->this_month_transported,     0, lengthof(i->this_month_transported));
	MemSetT(i->past_production,            0, lengthof(i->past_production));
	MemSetT(i->past_pct_transported,       0, lengthof(i->past_pct_transported));
	MemSetT(i->past_transported,           0, lengthof(i->past_transported));
	MemSetT(i->incoming_cargo_waiting,     0, lengthof(i->incoming_cargo_waiting));
	MemSetT(i->last_cargo_accepted_at,     0, lengthof(i->last_cargo_accepted_at));

	/* Randomize inital production if non-original economy is used and there are no production related callbacks. */
	if (!indspec->UsesOriginalEconomy()) {
		for (size_t ci = 0; ci < lengthof(i->production_rate); ci++) {
			i->production_rate[ci] = std::min((RandomRange(256) + 128) * i->production_rate[ci] >> 8, 255u);
		}
	}

	i->town = t;
	i->owner = OWNER_NONE;

	uint16 r = Random();
	i->random_colour = GB(r, 0, 4);
	i->counter = GB(r, 4, 12);
	i->random = initial_random_bits;
	i->was_cargo_delivered = false;
	i->last_prod_year = _cur_year;
	i->founder = founder;
	i->ctlflags = INDCTL_NONE;

	i->construction_date = _date;
	i->construction_type = (_game_mode == GM_EDITOR) ? ICT_SCENARIO_EDITOR :
			(_generating_world ? ICT_MAP_GENERATION : ICT_NORMAL_GAMEPLAY);

	/* Adding 1 here makes it conform to specs of var44 of varaction2 for industries
	 * 0 = created prior of newindustries
	 * else, chosen layout + 1 */
	i->selected_layout = (byte)(layout_index + 1);

	i->exclusive_supplier = INVALID_OWNER;
	i->exclusive_consumer = INVALID_OWNER;

	i->prod_level = PRODLEVEL_DEFAULT;

	/* Call callbacks after the regular fields got initialised. */

	if (HasBit(indspec->callback_mask, CBM_IND_PROD_CHANGE_BUILD)) {
		uint16 res = GetIndustryCallback(CBID_INDUSTRY_PROD_CHANGE_BUILD, 0, Random(), i, type, INVALID_TILE);
		if (res != CALLBACK_FAILED) {
			if (res < PRODLEVEL_MINIMUM || res > PRODLEVEL_MAXIMUM) {
				ErrorUnknownCallbackResult(indspec->grf_prop.grffile->grfid, CBID_INDUSTRY_PROD_CHANGE_BUILD, res);
			} else {
				i->prod_level = res;
				i->RecomputeProductionMultipliers();
			}
		}
	}

	if (_generating_world) {
		if (HasBit(indspec->callback_mask, CBM_IND_PRODUCTION_256_TICKS)) {
			IndustryProductionCallback(i, 1);
			for (size_t ci = 0; ci < lengthof(i->past_production[0]); ci++) {
				i->past_production[0][ci] = i->produced_cargo_waiting[ci] * 8;
				i->produced_cargo_waiting[ci] = 0;
			}
		}

		for (size_t ci = 0; ci < lengthof(i->past_production[0]); ci++) {
			i->past_production[0][ci] += i->production_rate[ci] * 8;
		}
	}

	if (HasBit(indspec->callback_mask, CBM_IND_DECIDE_COLOUR)) {
		uint16 res = GetIndustryCallback(CBID_INDUSTRY_DECIDE_COLOUR, 0, 0, i, type, INVALID_TILE);
		if (res != CALLBACK_FAILED) {
			if (GB(res, 4, 11) != 0) ErrorUnknownCallbackResult(indspec->grf_prop.grffile->grfid, CBID_INDUSTRY_DECIDE_COLOUR, res);
			i->random_colour = GB(res, 0, 4);
		}
	}

	if (HasBit(indspec->callback_mask, CBM_IND_INPUT_CARGO_TYPES)) {
		/* Clear all input cargo types */
		for (uint j = 0; j < lengthof(i->accepts_cargo); j++) i->accepts_cargo[j] = CT_INVALID;
		/* Query actual types */
		uint maxcargoes = (indspec->behaviour & INDUSTRYBEH_CARGOTYPES_UNLIMITED) ? lengthof(i->accepts_cargo) : 3;
		for (uint j = 0; j < maxcargoes; j++) {
			uint16 res = GetIndustryCallback(CBID_INDUSTRY_INPUT_CARGO_TYPES, j, 0, i, type, INVALID_TILE);
			if (res == CALLBACK_FAILED || GB(res, 0, 8) == CT_INVALID) break;
			if (indspec->grf_prop.grffile->grf_version >= 8 && res >= 0x100) {
				ErrorUnknownCallbackResult(indspec->grf_prop.grffile->grfid, CBID_INDUSTRY_INPUT_CARGO_TYPES, res);
				break;
			}
			CargoID cargo = GetCargoTranslation(GB(res, 0, 8), indspec->grf_prop.grffile);
			/* Industries without "unlimited" cargo types support depend on the specific order/slots of cargo types.
			 * They need to be able to blank out specific slots without aborting the callback sequence,
			 * and solve this by returning undefined cargo indexes. Skip these. */
			if (cargo == CT_INVALID && !(indspec->behaviour & INDUSTRYBEH_CARGOTYPES_UNLIMITED)) continue;
			/* Verify valid cargo */
			if (std::find(indspec->accepts_cargo, endof(indspec->accepts_cargo), cargo) == endof(indspec->accepts_cargo)) {
				/* Cargo not in spec, error in NewGRF */
				ErrorUnknownCallbackResult(indspec->grf_prop.grffile->grfid, CBID_INDUSTRY_INPUT_CARGO_TYPES, res);
				break;
			}
			if (std::find(i->accepts_cargo, i->accepts_cargo + j, cargo) != i->accepts_cargo + j) {
				/* Duplicate cargo */
				ErrorUnknownCallbackResult(indspec->grf_prop.grffile->grfid, CBID_INDUSTRY_INPUT_CARGO_TYPES, res);
				break;
			}
			i->accepts_cargo[j] = cargo;
		}
	}

	if (HasBit(indspec->callback_mask, CBM_IND_OUTPUT_CARGO_TYPES)) {
		/* Clear all output cargo types */
		for (uint j = 0; j < lengthof(i->produced_cargo); j++) i->produced_cargo[j] = CT_INVALID;
		/* Query actual types */
		uint maxcargoes = (indspec->behaviour & INDUSTRYBEH_CARGOTYPES_UNLIMITED) ? lengthof(i->produced_cargo) : 2;
		for (uint j = 0; j < maxcargoes; j++) {
			uint16 res = GetIndustryCallback(CBID_INDUSTRY_OUTPUT_CARGO_TYPES, j, 0, i, type, INVALID_TILE);
			if (res == CALLBACK_FAILED || GB(res, 0, 8) == CT_INVALID) break;
			if (indspec->grf_prop.grffile->grf_version >= 8 && res >= 0x100) {
				ErrorUnknownCallbackResult(indspec->grf_prop.grffile->grfid, CBID_INDUSTRY_OUTPUT_CARGO_TYPES, res);
				break;
			}
			CargoID cargo = GetCargoTranslation(GB(res, 0, 8), indspec->grf_prop.grffile);
			/* Allow older GRFs to skip slots. */
			if (cargo == CT_INVALID && !(indspec->behaviour & INDUSTRYBEH_CARGOTYPES_UNLIMITED)) continue;
			/* Verify valid cargo */
			if (std::find(indspec->produced_cargo, endof(indspec->produced_cargo), cargo) == endof(indspec->produced_cargo)) {
				/* Cargo not in spec, error in NewGRF */
				ErrorUnknownCallbackResult(indspec->grf_prop.grffile->grfid, CBID_INDUSTRY_OUTPUT_CARGO_TYPES, res);
				break;
			}
			if (std::find(i->produced_cargo, i->produced_cargo + j, cargo) != i->produced_cargo + j) {
				/* Duplicate cargo */
				ErrorUnknownCallbackResult(indspec->grf_prop.grffile->grfid, CBID_INDUSTRY_OUTPUT_CARGO_TYPES, res);
				break;
			}
			i->produced_cargo[j] = cargo;
		}
	}

	/* Plant the tiles */

	for (const IndustryTileLayoutTile &it : layout) {
		TileIndex cur_tile = tile + ToTileIndexDiff(it.ti);

		if (it.gfx != GFX_WATERTILE_SPECIALCHECK) {
			i->location.Add(cur_tile);

			WaterClass wc = (IsWaterTile(cur_tile) ? GetWaterClass(cur_tile) : WATER_CLASS_INVALID);

			DoCommand(cur_tile, 0, 0, DC_EXEC | DC_NO_TEST_TOWN_RATING | DC_NO_MODIFY_TOWN_RATING, CMD_LANDSCAPE_CLEAR);

			MakeIndustry(cur_tile, i->index, it.gfx, Random(), wc);

			if (_generating_world) {
				SetIndustryConstructionCounter(cur_tile, 3);
				SetIndustryConstructionStage(cur_tile, 2);
			}

			/* it->gfx is stored in the map. But the translated ID cur_gfx is the interesting one */
			IndustryGfx cur_gfx = GetTranslatedIndustryTileID(it.gfx);
			const IndustryTileSpec *its = GetIndustryTileSpec(cur_gfx);
			if (its->animation.status != ANIM_STATUS_NO_ANIMATION) AddAnimatedTile(cur_tile);
		}
	}

	if (GetIndustrySpec(i->type)->behaviour & INDUSTRYBEH_PLANT_ON_BUILT) {
		for (uint j = 0; j != 50; j++) PlantRandomFarmField(i);
	}
	InvalidateWindowData(WC_INDUSTRY_DIRECTORY, 0, IDIWD_FORCE_REBUILD);

	if (!_generating_world) PopulateStationsNearby(i);
}

/**
 * Helper function for Build/Fund an industry
 * @param tile tile where industry is built
 * @param type of industry to build
 * @param flags of operations to conduct
 * @param indspec pointer to industry specifications
 * @param layout_index the index of the itsepc to build/fund
 * @param random_var8f random seed (possibly) used by industries
 * @param random_initial_bits The random bits the industry is going to have after construction.
 * @param founder Founder of the industry
 * @param creation_type The circumstances the industry is created under.
 * @param[out] ip Pointer to store newly created industry.
 * @return Succeeded or failed command.
 *
 * @post \c *ip contains the newly created industry if all checks are successful and the \a flags request actual creation, else it contains \c nullptr afterwards.
 */
static CommandCost CreateNewIndustryHelper(TileIndex tile, IndustryType type, DoCommandFlag flags, const IndustrySpec *indspec, size_t layout_index, uint32 random_var8f, uint16 random_initial_bits, Owner founder, IndustryAvailabilityCallType creation_type, Industry **ip)
{
	assert(layout_index < indspec->layouts.size());
	const IndustryTileLayout &layout = indspec->layouts[layout_index];
	bool custom_shape_check = false;

	*ip = nullptr;

	std::vector<ClearedObjectArea> object_areas(_cleared_object_areas);
	CommandCost ret = CheckIfIndustryTilesAreFree(tile, layout, layout_index, type, random_initial_bits, founder, creation_type, &custom_shape_check);
	_cleared_object_areas = object_areas;
	if (ret.Failed()) return ret;

	if (HasBit(GetIndustrySpec(type)->callback_mask, CBM_IND_LOCATION)) {
		ret = CheckIfCallBackAllowsCreation(tile, type, layout_index, random_var8f, random_initial_bits, founder, creation_type);
	} else {
		ret = _check_new_industry_procs[indspec->check_proc](tile);
	}
	if (ret.Failed()) return ret;

	if (!custom_shape_check && _settings_game.game_creation.land_generator == LG_TERRAGENESIS && _generating_world &&
			!_ignore_restrictions && !CheckIfCanLevelIndustryPlatform(tile, DC_NO_WATER, layout, type)) {
		return_cmd_error(STR_ERROR_SITE_UNSUITABLE);
	}

	ret = CheckIfFarEnoughFromConflictingIndustry(tile, type);
	if (ret.Failed()) return ret;

	Town *t = nullptr;
	ret = FindTownForIndustry(tile, type, &t);
	if (ret.Failed()) return ret;
	assert(t != nullptr);

	ret = CheckIfIndustryIsAllowed(tile, type, t);
	if (ret.Failed()) return ret;

	if (!Industry::CanAllocateItem()) return_cmd_error(STR_ERROR_TOO_MANY_INDUSTRIES);

	if (flags & DC_EXEC) {
		*ip = new Industry(tile);
		if (!custom_shape_check) CheckIfCanLevelIndustryPlatform(tile, DC_NO_WATER | DC_EXEC, layout, type);
		DoCreateNewIndustry(*ip, tile, type, layout, layout_index, t, founder, random_initial_bits);
	}

	return CommandCost();
}

/**
 * Build/Fund an industry
 * @param tile tile where industry is built
 * @param flags of operations to conduct
 * @param p1 various bitstuffed elements
 * - p1 = (bit  0 -  7) - industry type see build_industry.h and see industry.h
 * - p1 = (bit  8 - 15) - first layout to try
 * - p1 = (bit 16     ) - 0 = prospect, 1 = fund (only valid if current company is DEITY)
 * @param p2 seed to use for desyncfree randomisations
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdBuildIndustry(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	IndustryType it = GB(p1, 0, 8);
	if (it >= NUM_INDUSTRYTYPES) return CMD_ERROR;

	const IndustrySpec *indspec = GetIndustrySpec(it);

	/* Check if the to-be built/founded industry is available for this climate. */
	if (!indspec->enabled || indspec->layouts.empty()) return CMD_ERROR;

	/* If the setting for raw-material industries is not on, you cannot build raw-material industries.
	 * Raw material industries are industries that do not accept cargo (at least for now) */
	if (_game_mode != GM_EDITOR && _current_company != OWNER_DEITY && _settings_game.construction.raw_industry_construction == 0 && indspec->IsRawIndustry()) {
		return CMD_ERROR;
	}

	if (_game_mode != GM_EDITOR && GetIndustryProbabilityCallback(it, _current_company == OWNER_DEITY ? IACT_RANDOMCREATION : IACT_USERCREATION, 1) == 0) {
		return CMD_ERROR;
	}

	Randomizer randomizer;
	randomizer.SetSeed(p2);
	uint16 random_initial_bits = GB(p2, 0, 16);
	uint32 random_var8f = randomizer.Next();
	size_t num_layouts = indspec->layouts.size();
	CommandCost ret = CommandCost(STR_ERROR_SITE_UNSUITABLE);
	const bool deity_prospect = _current_company == OWNER_DEITY && !HasBit(p1, 16);

	Industry *ind = nullptr;
	if (deity_prospect || (_game_mode != GM_EDITOR && _current_company != OWNER_DEITY && _settings_game.construction.raw_industry_construction == 2 && indspec->IsRawIndustry())) {
		if (flags & DC_EXEC) {
			/* Prospected industries are build as OWNER_TOWN to not e.g. be build on owned land of the founder */
			Backup<CompanyID> cur_company(_current_company, OWNER_TOWN, FILE_LINE);
			/* Prospecting has a chance to fail, however we cannot guarantee that something can
			 * be built on the map, so the chance gets lower when the map is fuller, but there
			 * is nothing we can really do about that. */
			if (deity_prospect || Random() <= indspec->prospecting_chance) {
				for (int i = 0; i < 5000; i++) {
					/* We should not have more than one Random() in a function call
					 * because parameter evaluation order is not guaranteed in the c++ standard
					 */
					tile = RandomTile();
					/* Start with a random layout */
					size_t layout = RandomRange((uint32)num_layouts);
					/* Check now each layout, starting with the random one */
					for (size_t j = 0; j < num_layouts; j++) {
						layout = (layout + 1) % num_layouts;
						ret = CreateNewIndustryHelper(tile, it, flags, indspec, layout, random_var8f, random_initial_bits, cur_company.GetOriginalValue(), _current_company == OWNER_DEITY ? IACT_RANDOMCREATION : IACT_PROSPECTCREATION, &ind);
						if (ret.Succeeded()) break;
					}
					if (ret.Succeeded()) break;
				}
			}
			cur_company.Restore();
		}
	} else {
		size_t layout = GB(p1, 8, 8);
		if (layout >= num_layouts) return CMD_ERROR;

		/* Check subsequently each layout, starting with the given layout in p1 */
		for (size_t i = 0; i < num_layouts; i++) {
			layout = (layout + 1) % num_layouts;
			ret = CreateNewIndustryHelper(tile, it, flags, indspec, layout, random_var8f, random_initial_bits, _current_company, _current_company == OWNER_DEITY ? IACT_RANDOMCREATION : IACT_USERCREATION, &ind);
			if (ret.Succeeded()) break;
		}

		/* If it still failed, there's no suitable layout to build here, return the error */
		if (ret.Failed()) return ret;
	}

	if ((flags & DC_EXEC) && ind != nullptr && _game_mode != GM_EDITOR) {
		AdvertiseIndustryOpening(ind);
	}

	return CommandCost(EXPENSES_OTHER, indspec->GetConstructionCost());
}

/**
 * Change industry properties
 * @param tile Unused.
 * @param flags Type of operation.
 * @param p1 IndustryID
 * @param p2 various bitstuffed elements
 * - p2 = (bit 0 - 7) - IndustryAction to perform
 * - p2 = (bit 8 - 15) - IndustryControlFlags
 *                       (only used with set control flags)
 * - p2 = (bit 16 - 23) - CompanyID to set or INVALID_OWNER (available to everyone) or
 *                        OWNER_NONE (neutral stations only) or OWNER_DEITY (no one)
 *                        (only used with set exclusive supplier / consumer)
 * @param text - Additional industry text (only used with set text action)
 * @return Empty cost or an error.
 */
CommandCost CmdIndustryCtrl(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	if (_current_company != OWNER_DEITY) return CMD_ERROR;

	Industry *ind = Industry::GetIfValid(p1);
	if (ind == nullptr) return CMD_ERROR;

	auto action = static_cast<IndustryAction>(GB(p2, 0, 8));

	switch (action) {
		case IndustryAction::SetControlFlags: {
			IndustryControlFlags ctlflags = (IndustryControlFlags)GB(p2, 8, 8) & INDCTL_MASK;

			if (flags & DC_EXEC) ind->ctlflags = ctlflags;

			break;
		}

		case IndustryAction::SetExclusiveSupplier:
		case IndustryAction::SetExclusiveConsumer: {
			Owner company_id = (Owner)GB(p2, 16, 8);

			if (company_id != OWNER_NONE && company_id != INVALID_OWNER && company_id != OWNER_DEITY
				&& !Company::IsValidID(company_id)) return CMD_ERROR;

			if (flags & DC_EXEC) {
				if (action == IndustryAction::SetExclusiveSupplier) {
					ind->exclusive_supplier = company_id;
				} else {
					ind->exclusive_consumer = company_id;
				}
			}

			break;
		}

		case IndustryAction::SetText: {
			ind->text.clear();
			if (!StrEmpty(text)) ind->text = text;
			InvalidateWindowData(WC_INDUSTRY_VIEW, ind->index);
			break;
		}

		default:
			NOT_REACHED();
	}

	return CommandCost();
}

/**
 * Create a new industry of random layout.
 * @param tile The location to build the industry.
 * @param type The industry type to build.
 * @param creation_type The circumstances the industry is created under.
 * @return the created industry or nullptr if it failed.
 */
static Industry *CreateNewIndustry(TileIndex tile, IndustryType type, IndustryAvailabilityCallType creation_type)
{
	const IndustrySpec *indspec = GetIndustrySpec(type);

	uint32 seed = Random();
	uint32 seed2 = Random();
	Industry *i = nullptr;
	size_t layout_index = RandomRange((uint32)indspec->layouts.size());
	CommandCost ret = CreateNewIndustryHelper(tile, type, DC_EXEC, indspec, layout_index, seed, GB(seed2, 0, 16), OWNER_NONE, creation_type, &i);
	(void)ret; // assert only
	assert(i != nullptr || ret.Failed());
	return i;
}

/**
 * Compute the appearance probability for an industry during map creation.
 * @param it Industry type to compute.
 * @param[out] force_at_least_one Returns whether at least one instance should be forced on map creation.
 * @return Relative probability for the industry to appear.
 */
static uint32 GetScaledIndustryGenerationProbability(IndustryType it, bool *force_at_least_one)
{
	const IndustrySpec *ind_spc = GetIndustrySpec(it);
	uint32 chance = ind_spc->appear_creation[_settings_game.game_creation.landscape];
	if (!ind_spc->enabled || ind_spc->layouts.empty() ||
			(_game_mode != GM_EDITOR && _settings_game.difficulty.industry_density == ID_FUND_ONLY) ||
			(chance = GetIndustryProbabilityCallback(it, IACT_MAPGENERATION, chance)) == 0) {
		*force_at_least_one = false;
		return 0;
	} else {
		chance *= 16; // to increase precision
		/* We want industries appearing at coast to appear less often on bigger maps, as length of coast increases slower than map area.
		 * For simplicity we scale in both cases, though scaling the probabilities of all industries has no effect. */
		chance = (ind_spc->check_proc == CHECK_REFINERY || ind_spc->check_proc == CHECK_OIL_RIG) ? ScaleByMapSize1D(chance) : ScaleByMapSize(chance);

		*force_at_least_one = (chance > 0) && !(ind_spc->behaviour & INDUSTRYBEH_NOBUILT_MAPCREATION) && (_game_mode != GM_EDITOR);
		return chance;
	}
}

/**
 * Compute the probability for constructing a new industry during game play.
 * @param it Industry type to compute.
 * @param[out] min_number Minimal number of industries that should exist at the map.
 * @return Relative probability for the industry to appear.
 */
static uint16 GetIndustryGamePlayProbability(IndustryType it, byte *min_number)
{
	if (_settings_game.difficulty.industry_density == ID_FUND_ONLY) {
		*min_number = 0;
		return 0;
	}

	const IndustrySpec *ind_spc = GetIndustrySpec(it);
	byte chance = ind_spc->appear_ingame[_settings_game.game_creation.landscape];
	if (!ind_spc->enabled || ind_spc->layouts.empty() ||
			((ind_spc->behaviour & INDUSTRYBEH_BEFORE_1950) && _cur_year > 1950) ||
			((ind_spc->behaviour & INDUSTRYBEH_AFTER_1960) && _cur_year < 1960) ||
			(chance = GetIndustryProbabilityCallback(it, IACT_RANDOMCREATION, chance)) == 0) {
		*min_number = 0;
		return 0;
	}
	*min_number = (ind_spc->behaviour & INDUSTRYBEH_CANCLOSE_LASTINSTANCE) ? 1 : 0;
	return chance;
}

/**
 * Get wanted number of industries on the map.
 * @return Wanted number of industries at the map.
 */
static uint GetNumberOfIndustries()
{
	/* Number of industries on a 256x256 map. */
	static const uint16 numof_industry_table[] = {
		0,    // none
		0,    // minimal
		10,   // very low
		25,   // low
		55,   // normal
		80,   // high
	};

	assert(lengthof(numof_industry_table) == ID_END);
	uint difficulty = (_game_mode != GM_EDITOR) ? _settings_game.difficulty.industry_density : (uint)ID_VERY_LOW;
	return std::min<uint>(IndustryPool::MAX_SIZE, ScaleByMapSize(numof_industry_table[difficulty]));
}

/**
 * Try to place the industry in the game.
 * Since there is no feedback why placement fails, there is no other option
 * than to try a few times before concluding it does not work.
 * @param type     Industry type of the desired industry.
 * @param try_hard Try very hard to find a place. (Used to place at least one industry per type.)
 * @return Pointer to created industry, or \c nullptr if creation failed.
 */
static Industry *PlaceIndustry(IndustryType type, IndustryAvailabilityCallType creation_type, bool try_hard)
{
	uint tries = try_hard ? 10000u : 2000u;
	for (; tries > 0; tries--) {
		Industry *ind = CreateNewIndustry(RandomTile(), type, creation_type);
		if (ind != nullptr) return ind;
	}
	return nullptr;
}

/**
 * Try to build a industry on the map.
 * @param type IndustryType of the desired industry
 * @param try_hard Try very hard to find a place. (Used to place at least one industry per type)
 */
static void PlaceInitialIndustry(IndustryType type, bool try_hard)
{
	Backup<CompanyID> cur_company(_current_company, OWNER_NONE, FILE_LINE);

	IncreaseGeneratingWorldProgress(GWP_INDUSTRY);
	PlaceIndustry(type, IACT_MAPGENERATION, try_hard);

	cur_company.Restore();
}

/**
 * Get total number of industries existing in the game.
 * @return Number of industries currently in the game.
 */
static uint GetCurrentTotalNumberOfIndustries()
{
	int total = 0;
	for (IndustryType it = 0; it < NUM_INDUSTRYTYPES; it++) total += Industry::GetIndustryTypeCount(it);
	return total;
}


/** Reset the entry. */
void IndustryTypeBuildData::Reset()
{
	this->probability  = 0;
	this->min_number   = 0;
	this->target_count = 0;
	this->max_wait     = 1;
	this->wait_count   = 0;
}

/** Completely reset the industry build data. */
void IndustryBuildData::Reset()
{
	this->wanted_inds = GetCurrentTotalNumberOfIndustries() << 16;

	for (IndustryType it = 0; it < NUM_INDUSTRYTYPES; it++) {
		this->builddata[it].Reset();
	}
}

/** Monthly update of industry build data. */
void IndustryBuildData::MonthlyLoop()
{
	static const int NEWINDS_PER_MONTH = 0x38000 / (10 * 12); // lower 16 bits is a float fraction, 3.5 industries per decade, divided by 10 * 12 months.
	if (_settings_game.difficulty.industry_density == ID_FUND_ONLY) return; // 'no industries' setting.

	/* To prevent running out of unused industries for the player to connect,
	 * add a fraction of new industries each month, but only if the manager can keep up. */
	uint max_behind = 1 + std::min(99u, ScaleByMapSize(3)); // At most 2 industries for small maps, and 100 at the biggest map (about 6 months industry build attempts).
	if (GetCurrentTotalNumberOfIndustries() + max_behind >= (this->wanted_inds >> 16)) {
		this->wanted_inds += ScaleByMapSize(NEWINDS_PER_MONTH);
	}
}

/**
 * This function will create random industries during game creation.
 * It will scale the amount of industries by mapsize and difficulty level.
 */
void GenerateIndustries()
{
	if (_game_mode != GM_EDITOR && _settings_game.difficulty.industry_density == ID_FUND_ONLY) return; // No industries in the game.

	uint32 industry_probs[NUM_INDUSTRYTYPES];
	bool force_at_least_one[NUM_INDUSTRYTYPES];
	uint32 total_prob = 0;
	uint num_forced = 0;

	for (IndustryType it = 0; it < NUM_INDUSTRYTYPES; it++) {
		industry_probs[it] = GetScaledIndustryGenerationProbability(it, force_at_least_one + it);
		total_prob += industry_probs[it];
		if (force_at_least_one[it]) num_forced++;
	}

	uint total_amount = GetNumberOfIndustries();
	if (total_prob == 0 || total_amount < num_forced) {
		/* Only place the forced ones */
		total_amount = num_forced;
	}

	SetGeneratingWorldProgress(GWP_INDUSTRY, total_amount);

	/* Try to build one industry per type independent of any probabilities */
	for (IndustryType it = 0; it < NUM_INDUSTRYTYPES; it++) {
		if (force_at_least_one[it]) {
			assert(total_amount > 0);
			total_amount--;
			PlaceInitialIndustry(it, true);
		}
	}

	/* Add the remaining industries according to their probabilities */
	for (uint i = 0; i < total_amount; i++) {
		uint32 r = RandomRange(total_prob);
		IndustryType it = 0;
		while (r >= industry_probs[it]) {
			r -= industry_probs[it];
			it++;
			assert(it < NUM_INDUSTRYTYPES);
		}
		assert(industry_probs[it] > 0);
		PlaceInitialIndustry(it, false);
	}
	_industry_builder.Reset();
}

/**
 * Monthly update of industry statistics.
 * @param i Industry to update.
 */
static void UpdateIndustryStatistics(Industry *i)
{
	for (byte j = 0; j < lengthof(i->produced_cargo); j++) {
		if (i->produced_cargo[j] != CT_INVALID) {
			byte pct = 0;
			if (i->this_month_production[j] != 0) {
				i->last_prod_year = _cur_year;
				pct = std::min(i->this_month_transported[j] * 256 / i->this_month_production[j], 255);
			}
			for (size_t k = lengthof(i->past_pct_transported) - 1; k >= 1; k--){
				i->past_pct_transported[k][j] = i->past_pct_transported[k-1][j];
			}
			i->past_pct_transported[0][j] = pct;

			for (size_t k = lengthof(i->past_production) - 1; k >= 1; k--){
				i->past_production[k][j] = i->past_production[k-1][j];
			}
			i->past_production[0][j] = i->this_month_production[j];
			i->this_month_production[j] = 0;

			for (size_t k = lengthof(i->past_transported) - 1; k >= 1; k--){
				i->past_transported[k][j] = i->past_transported[k-1][j];
			}
			i->past_transported[0][j] = i->this_month_transported[j];
			i->this_month_transported[j] = 0;
		}
	}
}

/**
 * Recompute #production_rate for current #prod_level.
 * This function is only valid when not using smooth economy.
 */
void Industry::RecomputeProductionMultipliers()
{
	const IndustrySpec *indspec = GetIndustrySpec(this->type);
	assert(indspec->UsesOriginalEconomy());

	/* Rates are rounded up, so e.g. oilrig always produces some passengers */
	for (size_t i = 0; i < lengthof(this->production_rate); i++) {
		this->production_rate[i] = std::min(CeilDiv(indspec->production_rate[i] * this->prod_level, PRODLEVEL_DEFAULT), 0xFFu);
	}
}

void Industry::FillCachedName() const
{
	char buf[256];
	int64 args_array[] = { this->index };
	StringParameters tmp_params(args_array);
	char *end = GetStringWithArgs(buf, STR_INDUSTRY_NAME, &tmp_params, lastof(buf));
	this->cached_name.assign(buf, end);
}

void ClearAllIndustryCachedNames()
{
	for (Industry *ind : Industry::Iterate()) {
		ind->cached_name.clear();
	}
}

/**
 * Set the #probability and #min_number fields for the industry type \a it for a running game.
 * @param it Industry type.
 * @return At least one of the fields has changed value.
 */
bool IndustryTypeBuildData::GetIndustryTypeData(IndustryType it)
{
	byte min_number;
	uint32 probability = GetIndustryGamePlayProbability(it, &min_number);
	bool changed = min_number != this->min_number || probability != this->probability;
	this->min_number = min_number;
	this->probability = probability;
	return changed;
}

/** Decide how many industries of each type are needed. */
void IndustryBuildData::SetupTargetCount()
{
	bool changed = false;
	uint num_planned = 0; // Number of industries planned in the industry build data.
	for (IndustryType it = 0; it < NUM_INDUSTRYTYPES; it++) {
		changed |= this->builddata[it].GetIndustryTypeData(it);
		num_planned += this->builddata[it].target_count;
	}
	uint total_amount = this->wanted_inds >> 16; // Desired total number of industries.
	changed |= num_planned != total_amount;
	if (!changed) return; // All industries are still the same, no need to re-randomize.

	/* Initialize the target counts. */
	uint force_build = 0;  // Number of industries that should always be available.
	uint32 total_prob = 0; // Sum of probabilities.
	for (IndustryType it = 0; it < NUM_INDUSTRYTYPES; it++) {
		IndustryTypeBuildData *ibd = this->builddata + it;
		force_build += ibd->min_number;
		ibd->target_count = ibd->min_number;
		total_prob += ibd->probability;
	}

	if (total_prob == 0) return; // No buildable industries.

	/* Subtract forced industries from the number of industries available for construction. */
	total_amount = (total_amount <= force_build) ? 0 : total_amount - force_build;

	/* Assign number of industries that should be aimed for, by using the probability as a weight. */
	while (total_amount > 0) {
		uint32 r = RandomRange(total_prob);
		IndustryType it = 0;
		while (r >= this->builddata[it].probability) {
			r -= this->builddata[it].probability;
			it++;
			assert(it < NUM_INDUSTRYTYPES);
		}
		assert(this->builddata[it].probability > 0);
		this->builddata[it].target_count++;
		total_amount--;
	}
}

/**
 * Try to create a random industry, during gameplay
 */
void IndustryBuildData::TryBuildNewIndustry()
{
	this->SetupTargetCount();

	int missing = 0;       // Number of industries that need to be build.
	uint count = 0;        // Number of industry types eligible for build.
	uint32 total_prob = 0; // Sum of probabilities.
	IndustryType forced_build = NUM_INDUSTRYTYPES; // Industry type that should be forcibly build.
	for (IndustryType it = 0; it < NUM_INDUSTRYTYPES; it++) {
		int difference = this->builddata[it].target_count - Industry::GetIndustryTypeCount(it);
		missing += difference;
		if (this->builddata[it].wait_count > 0) continue; // This type may not be built now.
		if (difference > 0) {
			if (Industry::GetIndustryTypeCount(it) == 0 && this->builddata[it].min_number > 0) {
				/* An industry that should exist at least once, is not available. Force it, trying the most needed one first. */
				if (forced_build == NUM_INDUSTRYTYPES ||
						difference > this->builddata[forced_build].target_count - Industry::GetIndustryTypeCount(forced_build)) {
					forced_build = it;
				}
			}
			total_prob += difference;
			count++;
		}
	}

	if (EconomyIsInRecession() || (forced_build == NUM_INDUSTRYTYPES && (missing <= 0 || total_prob == 0))) count = 0; // Skip creation of an industry.

	if (count >= 1) {
		/* If not forced, pick a weighted random industry to build.
		 * For the case that count == 1, there is no need to draw a random number. */
		IndustryType it;
		if (forced_build != NUM_INDUSTRYTYPES) {
			it = forced_build;
		} else {
			/* Non-forced, select an industry type to build (weighted random). */
			uint32 r = 0; // Initialized to silence the compiler.
			if (count > 1) r = RandomRange(total_prob);
			for (it = 0; it < NUM_INDUSTRYTYPES; it++) {
				if (this->builddata[it].wait_count > 0) continue; // Type may not be built now.
				int difference = this->builddata[it].target_count - Industry::GetIndustryTypeCount(it);
				if (difference <= 0) continue; // Too many of this kind.
				if (count == 1) break;
				if (r < (uint)difference) break;
				r -= difference;
			}
			assert(it < NUM_INDUSTRYTYPES && this->builddata[it].target_count > Industry::GetIndustryTypeCount(it));
		}

		/* Try to create the industry. */
		const Industry *ind = PlaceIndustry(it, IACT_RANDOMCREATION, false);
		if (ind == nullptr) {
			this->builddata[it].wait_count = this->builddata[it].max_wait + 1; // Compensate for decrementing below.
			this->builddata[it].max_wait = std::min(1000, this->builddata[it].max_wait + 2);
		} else {
			AdvertiseIndustryOpening(ind);
			this->builddata[it].max_wait = std::max(this->builddata[it].max_wait / 2, 1); // Reduce waiting time of the industry type.
		}
	}

	/* Decrement wait counters. */
	for (IndustryType it = 0; it < NUM_INDUSTRYTYPES; it++) {
		if (this->builddata[it].wait_count > 0) this->builddata[it].wait_count--;
	}
}

/**
 * Protects an industry from closure if the appropriate flags and conditions are met
 * INDUSTRYBEH_CANCLOSE_LASTINSTANCE must be set (which, by default, it is not) and the
 * count of industries of this type must one (or lower) in order to be protected
 * against closure.
 * @param type IndustryType been queried
 * @result true if protection is on, false otherwise (except for oil wells)
 */
static bool CheckIndustryCloseDownProtection(IndustryType type)
{
	const IndustrySpec *indspec = GetIndustrySpec(type);

	/* oil wells (or the industries with that flag set) are always allowed to closedown */
	if ((indspec->behaviour & INDUSTRYBEH_DONT_INCR_PROD) && _settings_game.game_creation.landscape == LT_TEMPERATE) return false;
	return (indspec->behaviour & INDUSTRYBEH_CANCLOSE_LASTINSTANCE) == 0 && Industry::GetIndustryTypeCount(type) <= 1;
}

/**
 * Can given cargo type be accepted or produced by the industry?
 * @param cargo: Cargo type
 * @param ind: Industry
 * @param *c_accepts: Pointer to boolean for acceptance of cargo
 * @param *c_produces: Pointer to boolean for production of cargo
 * @return: \c *c_accepts is set when industry accepts the cargo type,
 *          \c *c_produces is set when the industry produces the cargo type
 */
static void CanCargoServiceIndustry(CargoID cargo, Industry *ind, bool *c_accepts, bool *c_produces)
{
	if (cargo == CT_INVALID) return;

	/* Check for acceptance of cargo */
	for (byte j = 0; j < lengthof(ind->accepts_cargo); j++) {
		if (cargo == ind->accepts_cargo[j] && !IndustryTemporarilyRefusesCargo(ind, cargo)) {
			*c_accepts = true;
			break;
		}
	}

	/* Check for produced cargo */
	for (byte j = 0; j < lengthof(ind->produced_cargo); j++) {
		if (cargo == ind->produced_cargo[j]) {
			*c_produces = true;
			break;
		}
	}
}

/**
 * Compute who can service the industry.
 *
 * Here, 'can service' means that he/she has trains and stations close enough
 * to the industry with the right cargo type and the right orders (ie has the
 * technical means).
 *
 * @param ind: Industry being investigated.
 *
 * @return: 0 if nobody can service the industry, 2 if the local company can
 * service the industry, and 1 otherwise (only competitors can service the
 * industry)
 */
static int WhoCanServiceIndustry(Industry *ind)
{
	if (ind->stations_near.size() == 0) return 0; // No stations found at all => nobody services

	int result = 0;
	for (const Vehicle *v : Vehicle::Iterate()) {
		/* Is it worthwhile to try this vehicle? */
		if (v->owner != _local_company && result != 0) continue;

		/* Check whether it accepts the right kind of cargo */
		bool c_accepts = false;
		bool c_produces = false;
		if (v->type == VEH_TRAIN && v->IsFrontEngine()) {
			for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
				CanCargoServiceIndustry(u->cargo_type, ind, &c_accepts, &c_produces);
			}
		} else if (v->type == VEH_ROAD || v->type == VEH_SHIP || v->type == VEH_AIRCRAFT) {
			CanCargoServiceIndustry(v->cargo_type, ind, &c_accepts, &c_produces);
		} else {
			continue;
		}
		if (!c_accepts && !c_produces) continue; // Wrong cargo

		/* Check orders of the vehicle.
		 * We cannot check the first of shared orders only, since the first vehicle in such a chain
		 * may have a different cargo type.
		 */
		for (const Order *o : v->Orders()) {
			if (o->IsType(OT_GOTO_STATION) && !(o->GetUnloadType() & OUFB_TRANSFER)) {
				/* Vehicle visits a station to load or unload */
				Station *st = Station::Get(o->GetDestination());
				assert(st != nullptr);

				/* Same cargo produced by industry is dropped here => not serviced by vehicle v */
				if ((o->GetUnloadType() & OUFB_UNLOAD) && !c_accepts) break;

				if (ind->stations_near.find(st) != ind->stations_near.end()) {
					if (v->owner == _local_company) return 2; // Company services industry
					result = 1; // Competitor services industry
				}
			}
		}
	}
	return result;
}

/**
 * Report news that industry production has changed significantly
 *
 * @param ind: Industry with changed production
 * @param type: Cargo type that has changed
 * @param percent: Percentage of change (>0 means increase, <0 means decrease)
 */
static void ReportNewsProductionChangeIndustry(Industry *ind, CargoID type, int percent)
{
	NewsType nt;

	switch (WhoCanServiceIndustry(ind)) {
		case 0: nt = NT_INDUSTRY_NOBODY;  break;
		case 1: nt = NT_INDUSTRY_OTHER;   break;
		case 2: nt = NT_INDUSTRY_COMPANY; break;
		default: NOT_REACHED();
	}
	SetDParam(2, abs(percent));
	SetDParam(0, CargoSpec::Get(type)->name);
	SetDParam(1, ind->index);
	AddIndustryNewsItem(
		percent >= 0 ? STR_NEWS_INDUSTRY_PRODUCTION_INCREASE_SMOOTH : STR_NEWS_INDUSTRY_PRODUCTION_DECREASE_SMOOTH,
		nt,
		ind->index
	);
}

static const uint PERCENT_TRANSPORTED_60 = 153;
static const uint PERCENT_TRANSPORTED_80 = 204;

/**
 * Change industry production or do closure
 * @param i Industry for which changes are performed
 * @param monthly true if it's the monthly call, false if it's the random call
 */
static void ChangeIndustryProduction(Industry *i, bool monthly)
{
	StringID str = STR_NULL;
	bool closeit = false;
	const IndustrySpec *indspec = GetIndustrySpec(i->type);
	bool standard = false;
	bool suppress_message = false;
	bool recalculate_multipliers = false; ///< reinitialize production_rate to match prod_level
	/* use original economy for industries using production related callbacks */
	bool original_economy = indspec->UsesOriginalEconomy();
	byte div = 0;
	byte mul = 0;
	int8 increment = 0;

	bool callback_enabled = HasBit(indspec->callback_mask, monthly ? CBM_IND_MONTHLYPROD_CHANGE : CBM_IND_PRODUCTION_CHANGE);
	if (callback_enabled) {
		uint16 res = GetIndustryCallback(monthly ? CBID_INDUSTRY_MONTHLYPROD_CHANGE : CBID_INDUSTRY_PRODUCTION_CHANGE, 0, Random(), i, i->type, i->location.tile);
		if (res != CALLBACK_FAILED) { // failed callback means "do nothing"
			suppress_message = HasBit(res, 7);
			/* Get the custom message if any */
			if (HasBit(res, 8)) str = MapGRFStringID(indspec->grf_prop.grffile->grfid, GB(GetRegister(0x100), 0, 16));
			res = GB(res, 0, 4);
			switch (res) {
				default: NOT_REACHED();
				case 0x0: break;                  // Do nothing, but show the custom message if any
				case 0x1: div = 1; break;         // Halve industry production. If production reaches the quarter of the default, the industry is closed instead.
				case 0x2: mul = 1; break;         // Double industry production if it hasn't reached eight times of the original yet.
				case 0x3: closeit = true; break;  // The industry announces imminent closure, and is physically removed from the map next month.
				case 0x4: standard = true; break; // Do the standard random production change as if this industry was a primary one.
				case 0x5: case 0x6: case 0x7:     // Divide production by 4, 8, 16
				case 0x8: div = res - 0x3; break; // Divide production by 32
				case 0x9: case 0xA: case 0xB:     // Multiply production by 4, 8, 16
				case 0xC: mul = res - 0x7; break; // Multiply production by 32
				case 0xD:                         // decrement production
				case 0xE:                         // increment production
					increment = res == 0x0D ? -1 : 1;
					break;
				case 0xF:                         // Set production to third byte of register 0x100
					i->prod_level = Clamp(GB(GetRegister(0x100), 16, 8), PRODLEVEL_MINIMUM, PRODLEVEL_MAXIMUM);
					recalculate_multipliers = true;
					break;
			}
		}
	} else {
		if (monthly == original_economy) return;
		if (!original_economy && _settings_game.economy.type == ET_FROZEN) return;
		if (indspec->life_type == INDUSTRYLIFE_BLACK_HOLE) return;
	}

	if (standard || (!callback_enabled && (indspec->life_type & (INDUSTRYLIFE_ORGANIC | INDUSTRYLIFE_EXTRACTIVE)) != 0)) {
		/* decrease or increase */
		bool only_decrease = (indspec->behaviour & INDUSTRYBEH_DONT_INCR_PROD) && _settings_game.game_creation.landscape == LT_TEMPERATE;

		if (original_economy) {
			if (only_decrease || Chance16(1, 3)) {
				/* If more than 60% transported, 66% chance of increase, else 33% chance of increase */
				if (!only_decrease && (i->past_pct_transported[0][0] > PERCENT_TRANSPORTED_60) != Chance16(1, 3)) {
					mul = 1; // Increase production
				} else {
					div = 1; // Decrease production
				}
			}
		} else if (_settings_game.economy.type == ET_SMOOTH) {
			closeit = !(i->ctlflags & (INDCTL_NO_CLOSURE | INDCTL_NO_PRODUCTION_DECREASE));
			for (byte j = 0; j < lengthof(i->produced_cargo); j++) {
				if (i->produced_cargo[j] == CT_INVALID) continue;
				uint32 r = Random();
				int old_prod, new_prod, percent;
				/* If over 60% is transported, mult is 1, else mult is -1. */
				int mult = (i->past_pct_transported[0][j] > PERCENT_TRANSPORTED_60) ? 1 : -1;

				new_prod = old_prod = i->production_rate[j];

				/* For industries with only_decrease flags (temperate terrain Oil Wells),
				 * the multiplier will always be -1 so they will only decrease. */
				if (only_decrease) {
					mult = -1;
				/* For normal industries, if over 60% is transported, 33% chance for decrease.
				 * Bonus for very high station ratings (over 80%): 16% chance for decrease. */
				} else if (Chance16I(1, ((i->past_pct_transported[0][j] > PERCENT_TRANSPORTED_80) ? 6 : 3), r)) {
					mult *= -1;
				}

				/* 4.5% chance for 3-23% (or 1 unit for very low productions) production change,
				 * determined by mult value. If mult = 1 prod. increases, else (-1) it decreases. */
				if (Chance16I(1, 22, r >> 16)) {
					new_prod += mult * (std::max(((RandomRange(50) + 10) * old_prod) >> 8, 1U));
				}

				/* Prevent production to overflow or Oil Rig passengers to be over-"produced" */
				new_prod = Clamp(new_prod, 1, 255);
				if (i->produced_cargo[j] == CT_PASSENGERS && !(indspec->behaviour & INDUSTRYBEH_NO_PAX_PROD_CLAMP)) {
					new_prod = Clamp(new_prod, 0, 16);
				}

				/* If override flags are set, prevent actually changing production if any was decided on */
				if ((i->ctlflags & INDCTL_NO_PRODUCTION_DECREASE) && new_prod < old_prod) continue;
				if ((i->ctlflags & INDCTL_NO_PRODUCTION_INCREASE) && new_prod > old_prod) continue;

				/* Do not stop closing the industry when it has the lowest possible production rate */
				if (new_prod == old_prod && old_prod > 1) {
					closeit = false;
					continue;
				}

				percent = (old_prod == 0) ? 100 : (new_prod * 100 / old_prod - 100);
				i->production_rate[j] = new_prod;

				/* Close the industry when it has the lowest possible production rate */
				if (new_prod > 1) closeit = false;

				if (abs(percent) >= 10) {
					ReportNewsProductionChangeIndustry(i, i->produced_cargo[j], percent);
				}
			}
		}
	}

	/* If override flags are set, prevent actually changing production if any was decided on */
	if ((i->ctlflags & INDCTL_NO_PRODUCTION_DECREASE) && (div > 0 || increment < 0)) return;
	if ((i->ctlflags & INDCTL_NO_PRODUCTION_INCREASE) && (mul > 0 || increment > 0)) return;

	if (!callback_enabled && (indspec->life_type & INDUSTRYLIFE_PROCESSING)) {
		if ( (byte)(_cur_year - i->last_prod_year) >= 5 && Chance16(1, original_economy ? 2 : 180)) {
			closeit = true;
		}
	}

	/* Increase if needed */
	while (mul-- != 0 && i->prod_level < PRODLEVEL_MAXIMUM) {
		i->prod_level = std::min<int>(i->prod_level * 2, PRODLEVEL_MAXIMUM);
		recalculate_multipliers = true;
		if (str == STR_NULL) str = indspec->production_up_text;
	}

	/* Decrease if needed */
	while (div-- != 0 && !closeit) {
		if (i->prod_level == PRODLEVEL_MINIMUM) {
			closeit = true;
			break;
		} else {
			i->prod_level = std::max<int>(i->prod_level / 2, PRODLEVEL_MINIMUM);
			recalculate_multipliers = true;
			if (str == STR_NULL) str = indspec->production_down_text;
		}
	}

	/* Increase or Decreasing the production level if needed */
	if (increment != 0) {
		if (increment < 0 && i->prod_level == PRODLEVEL_MINIMUM) {
			closeit = true;
		} else {
			i->prod_level = ClampU(i->prod_level + increment, PRODLEVEL_MINIMUM, PRODLEVEL_MAXIMUM);
			recalculate_multipliers = true;
		}
	}

	/* Recalculate production_rate
	 * For non-smooth economy these should always be synchronized with prod_level */
	if (recalculate_multipliers) i->RecomputeProductionMultipliers();

	/* Close if needed and allowed */
	if (closeit && !CheckIndustryCloseDownProtection(i->type) && !(i->ctlflags & INDCTL_NO_CLOSURE)) {
		i->prod_level = PRODLEVEL_CLOSURE;
		SetWindowDirty(WC_INDUSTRY_VIEW, i->index);
		str = indspec->closure_text;
	}

	if (!suppress_message && str != STR_NULL) {
		NewsType nt;
		/* Compute news category */
		if (closeit) {
			nt = NT_INDUSTRY_CLOSE;
			AI::BroadcastNewEvent(new ScriptEventIndustryClose(i->index));
			Game::NewEvent(new ScriptEventIndustryClose(i->index));
		} else {
			switch (WhoCanServiceIndustry(i)) {
				case 0: nt = NT_INDUSTRY_NOBODY;  break;
				case 1: nt = NT_INDUSTRY_OTHER;   break;
				case 2: nt = NT_INDUSTRY_COMPANY; break;
				default: NOT_REACHED();
			}
		}
		/* Set parameters of news string */
		if (str > STR_LAST_STRINGID) {
			SetDParam(0, STR_TOWN_NAME);
			SetDParam(1, i->town->index);
			SetDParam(2, indspec->name);
		} else if (closeit) {
			SetDParam(0, STR_FORMAT_INDUSTRY_NAME);
			SetDParam(1, i->town->index);
			SetDParam(2, indspec->name);
		} else {
			SetDParam(0, i->index);
		}
		/* and report the news to the user */
		if (closeit) {
			AddTileNewsItem(str, nt, i->location.tile + TileDiffXY(1, 1));
		} else {
			AddIndustryNewsItem(str, nt, i->index);
		}
	}
}

/**
 * Daily handler for the industry changes
 * Taking the original map size of 256*256, the number of random changes was always of just one unit.
 * But it cannot be the same on smaller or bigger maps. That number has to be scaled up or down.
 * For small maps, it implies that less than one change per month is required, while on bigger maps,
 * it would be way more. The daily loop handles those changes.
 */
void IndustryDailyLoop()
{
	_economy.industry_daily_change_counter += _economy.industry_daily_increment;

	/* Bits 16-31 of industry_construction_counter contain the number of industries to change/create today,
	 * the lower 16 bit are a fractional part that might accumulate over several days until it
	 * is sufficient for an industry. */
	uint16 change_loop = _economy.industry_daily_change_counter >> 16;

	/* Reset the active part of the counter, just keeping the "fractional part" */
	_economy.industry_daily_change_counter &= 0xFFFF;

	if (change_loop == 0) {
		return;  // Nothing to do? get out
	}

	Backup<CompanyID> cur_company(_current_company, OWNER_NONE, FILE_LINE);

	/* perform the required industry changes for the day */

	uint perc = 3; // Between 3% and 9% chance of creating a new industry.
	if ((_industry_builder.wanted_inds >> 16) > GetCurrentTotalNumberOfIndustries()) {
		perc = std::min(9u, perc + (_industry_builder.wanted_inds >> 16) - GetCurrentTotalNumberOfIndustries());
	}
	for (uint16 j = 0; j < change_loop; j++) {
		if (Chance16(perc, 100)) {
			_industry_builder.TryBuildNewIndustry();
		} else {
			Industry *i = Industry::GetRandom();
			if (i != nullptr) {
				ChangeIndustryProduction(i, false);
				SetWindowDirty(WC_INDUSTRY_VIEW, i->index);
			}
		}
	}

	cur_company.Restore();

	/* production-change */
	InvalidateWindowData(WC_INDUSTRY_DIRECTORY, 0, IDIWD_PRODUCTION_CHANGE);
}

void IndustryMonthlyLoop()
{
	Backup<CompanyID> cur_company(_current_company, OWNER_NONE, FILE_LINE);

	_industry_builder.MonthlyLoop();

	for (Industry *i : Industry::Iterate()) {
		UpdateIndustryStatistics(i);
		if (i->prod_level == PRODLEVEL_CLOSURE) {
			delete i;
		} else {
			ChangeIndustryProduction(i, true);
			SetWindowDirty(WC_INDUSTRY_VIEW, i->index);
		}
	}

	cur_company.Restore();

	/* production-change */
	InvalidateWindowData(WC_INDUSTRY_DIRECTORY, 0, IDIWD_PRODUCTION_CHANGE);
}


void InitializeIndustries()
{
	Industry::ResetIndustryCounts();
	_industry_sound_tile = 0;

	_industry_builder.Reset();
}

/** Verify whether the generated industries are complete, and warn the user if not. */
void CheckIndustries()
{
	int count = 0;
	for (IndustryType it = 0; it < NUM_INDUSTRYTYPES; it++) {
		if (Industry::GetIndustryTypeCount(it) > 0) continue; // Types of existing industries can be skipped.

		bool force_at_least_one;
		uint32 chance = GetScaledIndustryGenerationProbability(it, &force_at_least_one);
		if (chance == 0 || !force_at_least_one) continue; // Types that are not available can be skipped.

		const IndustrySpec *is = GetIndustrySpec(it);
		SetDParam(0, is->name);
		ShowErrorMessage(STR_ERROR_NO_SUITABLE_PLACES_FOR_INDUSTRIES, STR_ERROR_NO_SUITABLE_PLACES_FOR_INDUSTRIES_EXPLANATION, WL_WARNING);

		count++;
		if (count >= 3) break; // Don't swamp the user with errors.
	}
}

/**
 * Is an industry with the spec a raw industry?
 * @return true if it should be handled as a raw industry
 */
bool IndustrySpec::IsRawIndustry() const
{
	return (this->life_type & (INDUSTRYLIFE_EXTRACTIVE | INDUSTRYLIFE_ORGANIC)) != 0;
}

/**
 * Is an industry with the spec a processing industry?
 * @return true if it should be handled as a processing industry
 */
bool IndustrySpec::IsProcessingIndustry() const
{
	/* Lumber mills are neither raw nor processing */
	return (this->life_type & INDUSTRYLIFE_PROCESSING) != 0 &&
			(this->behaviour & INDUSTRYBEH_CUT_TREES) == 0;
}

/**
 * Get the cost for constructing this industry
 * @return the cost (inflation corrected etc)
 */
Money IndustrySpec::GetConstructionCost() const
{
	/* Building raw industries like secondary uses different price base */
	return (_price[(_settings_game.construction.raw_industry_construction == 1 && this->IsRawIndustry()) ?
			PR_BUILD_INDUSTRY_RAW : PR_BUILD_INDUSTRY] * this->cost_multiplier) >> 8;
}

/**
 * Get the cost for removing this industry
 * Take note that the cost will always be zero for non-grf industries.
 * Only if the grf author did specified a cost will it be applicable.
 * @return the cost (inflation corrected etc)
 */
Money IndustrySpec::GetRemovalCost() const
{
	return (_price[PR_CLEAR_INDUSTRY] * this->removal_cost_multiplier) >> 8;
}

/**
 * Determines whether this industrytype uses standard/newgrf production changes.
 * @return true if original economy is used.
 */
bool IndustrySpec::UsesOriginalEconomy() const
{
	return _settings_game.economy.type == ET_ORIGINAL ||
		HasBit(this->callback_mask, CBM_IND_PRODUCTION_256_TICKS) || HasBit(this->callback_mask, CBM_IND_PRODUCTION_CARGO_ARRIVAL) || // production callbacks
		HasBit(this->callback_mask, CBM_IND_MONTHLYPROD_CHANGE) || HasBit(this->callback_mask, CBM_IND_PRODUCTION_CHANGE) || HasBit(this->callback_mask, CBM_IND_PROD_CHANGE_BUILD); // production change callbacks
}

IndustrySpec::~IndustrySpec()
{
	if (HasBit(this->cleanup_flag, CLEAN_RANDOMSOUNDS)) {
		free(this->random_sounds);
	}
}

static CommandCost TerraformTile_Industry(TileIndex tile, DoCommandFlag flags, int z_new, Slope tileh_new)
{
	if (AutoslopeEnabled()) {
		/* We imitate here TTDP's behaviour:
		 *  - Both new and old slope must not be steep.
		 *  - TileMaxZ must not be changed.
		 *  - Allow autoslope by default.
		 *  - Disallow autoslope if callback succeeds and returns non-zero.
		 */
		Slope tileh_old = GetTileSlope(tile);
		/* TileMaxZ must not be changed. Slopes must not be steep. */
		if (!IsSteepSlope(tileh_old) && !IsSteepSlope(tileh_new) && (GetTileMaxZ(tile) == z_new + GetSlopeMaxZ(tileh_new))) {
			const IndustryGfx gfx = GetIndustryGfx(tile);
			const IndustryTileSpec *itspec = GetIndustryTileSpec(gfx);

			/* Call callback 3C 'disable autosloping for industry tiles'. */
			if (HasBit(itspec->callback_mask, CBM_INDT_AUTOSLOPE)) {
				/* If the callback fails, allow autoslope. */
				uint16 res = GetIndustryTileCallback(CBID_INDTILE_AUTOSLOPE, 0, 0, gfx, Industry::GetByTile(tile), tile);
				if (res == CALLBACK_FAILED || !ConvertBooleanCallback(itspec->grf_prop.grffile, CBID_INDTILE_AUTOSLOPE, res)) return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_FOUNDATION]);
			} else {
				/* allow autoslope */
				return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_FOUNDATION]);
			}
		}
	}
	return DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
}

extern const TileTypeProcs _tile_type_industry_procs = {
	DrawTile_Industry,           // draw_tile_proc
	GetSlopePixelZ_Industry,     // get_slope_z_proc
	ClearTile_Industry,          // clear_tile_proc
	AddAcceptedCargo_Industry,   // add_accepted_cargo_proc
	GetTileDesc_Industry,        // get_tile_desc_proc
	GetTileTrackStatus_Industry, // get_tile_track_status_proc
	ClickTile_Industry,          // click_tile_proc
	AnimateTile_Industry,        // animate_tile_proc
	TileLoop_Industry,           // tile_loop_proc
	ChangeTileOwner_Industry,    // change_tile_owner_proc
	nullptr,                        // add_produced_cargo_proc
	nullptr,                        // vehicle_enter_tile_proc
	GetFoundation_Industry,      // get_foundation_proc
	TerraformTile_Industry,      // terraform_tile_proc
};

bool IndustryCompare::operator() (const Industry *lhs, const Industry *rhs) const
{
	return lhs->index < rhs->index;
}
