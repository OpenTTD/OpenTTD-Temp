/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file vehicle.cpp Base implementations of all vehicles. */

#include "stdafx.h"
#include "error.h"
#include "roadveh.h"
#include "ship.h"
#include "spritecache.h"
#include "timetable.h"
#include "viewport_func.h"
#include "news_func.h"
#include "command_func.h"
#include "company_func.h"
#include "train.h"
#include "aircraft.h"
#include "newgrf_debug.h"
#include "newgrf_sound.h"
#include "newgrf_station.h"
#include "group_gui.h"
#include "strings_func.h"
#include "zoom_func.h"
#include "date_func.h"
#include "vehicle_func.h"
#include "autoreplace_func.h"
#include "autoreplace_gui.h"
#include "station_base.h"
#include "ai/ai.hpp"
#include "depot_func.h"
#include "network/network.h"
#include "core/pool_func.hpp"
#include "economy_base.h"
#include "articulated_vehicles.h"
#include "roadstop_base.h"
#include "core/random_func.hpp"
#include "core/backup_type.hpp"
#include "order_backup.h"
#include "sound_func.h"
#include "effectvehicle_func.h"
#include "effectvehicle_base.h"
#include "vehiclelist.h"
#include "bridge_map.h"
#include "tunnel_map.h"
#include "depot_map.h"
#include "gamelog.h"
#include "linkgraph/linkgraph.h"
#include "linkgraph/refresh.h"
#include "framerate_type.h"

#include "table/strings.h"

#include "safeguards.h"

/* Number of bits in the hash to use from each vehicle coord */
static const uint GEN_HASHX_BITS = 6;
static const uint GEN_HASHY_BITS = 6;

/* Size of each hash bucket */
static const uint GEN_HASHX_BUCKET_BITS = 7;
static const uint GEN_HASHY_BUCKET_BITS = 6;

/* Compute hash for vehicle coord */
#define GEN_HASHX(x)    GB((x), GEN_HASHX_BUCKET_BITS + ZOOM_LVL_SHIFT, GEN_HASHX_BITS)
#define GEN_HASHY(y)   (GB((y), GEN_HASHY_BUCKET_BITS + ZOOM_LVL_SHIFT, GEN_HASHY_BITS) << GEN_HASHX_BITS)
#define GEN_HASH(x, y) (GEN_HASHY(y) + GEN_HASHX(x))

/* Maximum size until hash repeats */
static const int GEN_HASHX_SIZE = 1 << (GEN_HASHX_BUCKET_BITS + GEN_HASHX_BITS + ZOOM_LVL_SHIFT);
static const int GEN_HASHY_SIZE = 1 << (GEN_HASHY_BUCKET_BITS + GEN_HASHY_BITS + ZOOM_LVL_SHIFT);

/* Increments to reach next bucket in hash table */
static const int GEN_HASHX_INC = 1;
static const int GEN_HASHY_INC = 1 << GEN_HASHX_BITS;

/* Mask to wrap-around buckets */
static const uint GEN_HASHX_MASK =  (1 << GEN_HASHX_BITS) - 1;
static const uint GEN_HASHY_MASK = ((1 << GEN_HASHY_BITS) - 1) << GEN_HASHX_BITS;

VehicleID _new_vehicle_id;
uint _returned_refit_capacity;        ///< Stores the capacity after a refit operation.
uint16 _returned_mail_refit_capacity; ///< Stores the mail capacity after a refit operation (Aircraft only).


/** The pool with all our precious vehicles. */
VehiclePool _vehicle_pool("Vehicle");
INSTANTIATE_POOL_METHODS(Vehicle)


/**
 * Determine shared bounds of all sprites.
 * @param[out] bounds Shared bounds.
 */
void VehicleSpriteSeq::GetBounds(Rect *bounds) const
{
	bounds->left = bounds->top = bounds->right = bounds->bottom = 0;
	for (uint i = 0; i < this->count; ++i) {
		const Sprite *spr = GetSprite(this->seq[i].sprite, ST_NORMAL);
		if (i == 0) {
			bounds->left = spr->x_offs;
			bounds->top  = spr->y_offs;
			bounds->right  = spr->width  + spr->x_offs - 1;
			bounds->bottom = spr->height + spr->y_offs - 1;
		} else {
			if (spr->x_offs < bounds->left) bounds->left = spr->x_offs;
			if (spr->y_offs < bounds->top)  bounds->top  = spr->y_offs;
			int right  = spr->width  + spr->x_offs - 1;
			int bottom = spr->height + spr->y_offs - 1;
			if (right  > bounds->right)  bounds->right  = right;
			if (bottom > bounds->bottom) bounds->bottom = bottom;
		}
	}
}

/**
 * Draw the sprite sequence.
 * @param x X position
 * @param y Y position
 * @param default_pal Vehicle palette
 * @param force_pal Whether to ignore individual palettes, and draw everything with \a default_pal.
 */
void VehicleSpriteSeq::Draw(int x, int y, PaletteID default_pal, bool force_pal) const
{
	for (uint i = 0; i < this->count; ++i) {
		PaletteID pal = force_pal || !this->seq[i].pal ? default_pal : this->seq[i].pal;
		DrawSprite(this->seq[i].sprite, pal, x, y);
	}
}

/**
 * Function to tell if a vehicle needs to be autorenewed
 * @param *c The vehicle owner
 * @param use_renew_setting Should the company renew setting be considered?
 * @return true if the vehicle is old enough for replacement
 */
bool Vehicle::NeedsAutorenewing(const Company *c, bool use_renew_setting) const
{
	/* We can always generate the Company pointer when we have the vehicle.
	 * However this takes time and since the Company pointer is often present
	 * when this function is called then it's faster to pass the pointer as an
	 * argument rather than finding it again. */
	assert(c == Company::Get(this->owner));

	if (use_renew_setting && !c->settings.engine_renew) return false;
	if (this->age - this->max_age < (c->settings.engine_renew_months * 30)) return false;

	/* Only engines need renewing */
	if (this->type == VEH_TRAIN && !Train::From(this)->IsEngine()) return false;

	return true;
}

/**
 * Service a vehicle and all subsequent vehicles in the consist
 *
 * @param *v The vehicle or vehicle chain being serviced
 */
void VehicleServiceInDepot(Vehicle *v)
{
	assert(v != nullptr);
	SetWindowDirty(WC_VEHICLE_DETAILS, v->index); // ensure that last service date and reliability are updated

	do {
		v->date_of_last_service = _date;
		v->breakdowns_since_last_service = 0;
		v->reliability = v->GetEngine()->reliability;
		/* Prevent vehicles from breaking down directly after exiting the depot. */
		v->breakdown_chance /= 4;
		if (_settings_game.difficulty.vehicle_breakdowns == 1) v->breakdown_chance = 0; // on reduced breakdown
		v = v->Next();
	} while (v != nullptr && v->HasEngineType());
}

/**
 * Check if the vehicle needs to go to a depot in near future (if a opportunity presents itself) for service or replacement.
 *
 * @see NeedsAutomaticServicing()
 * @return true if the vehicle should go to a depot if a opportunity presents itself.
 */
bool Vehicle::NeedsServicing() const
{
	/* Stopped or crashed vehicles will not move, as such making unmovable
	 * vehicles to go for service is lame. */
	if (this->vehstatus & (VS_STOPPED | VS_CRASHED)) return false;

	/* Are we ready for the next service cycle? */
	const Company *c = Company::Get(this->owner);
	if (this->ServiceIntervalIsPercent() ?
			(this->reliability >= this->GetEngine()->reliability * (100 - this->GetServiceInterval()) / 100) :
			(this->date_of_last_service + this->GetServiceInterval() >= _date)) {
		return false;
	}

	/* If we're servicing anyway, because we have not disabled servicing when
	 * there are no breakdowns or we are playing with breakdowns, bail out. */
	if (!_settings_game.order.no_servicing_if_no_breakdowns ||
			_settings_game.difficulty.vehicle_breakdowns != 0) {
		return true;
	}

	/* Test whether there is some pending autoreplace.
	 * Note: We do this after the service-interval test.
	 * There are a lot more reasons for autoreplace to fail than we can test here reasonably. */
	bool pending_replace = false;
	Money needed_money = c->settings.engine_renew_money;
	if (needed_money > c->money) return false;

	for (const Vehicle *v = this; v != nullptr; v = (v->type == VEH_TRAIN) ? Train::From(v)->GetNextUnit() : nullptr) {
		bool replace_when_old = false;
		EngineID new_engine = EngineReplacementForCompany(c, v->engine_type, v->group_id, &replace_when_old);

		/* Check engine availability */
		if (new_engine == INVALID_ENGINE || !HasBit(Engine::Get(new_engine)->company_avail, v->owner)) continue;
		/* Is the vehicle old if we are not always replacing? */
		if (replace_when_old && !v->NeedsAutorenewing(c, false)) continue;

		/* Check refittability */
		CargoTypes available_cargo_types, union_mask;
		GetArticulatedRefitMasks(new_engine, true, &union_mask, &available_cargo_types);
		/* Is there anything to refit? */
		if (union_mask != 0) {
			CargoID cargo_type;
			/* We cannot refit to mixed cargoes in an automated way */
			if (IsArticulatedVehicleCarryingDifferentCargoes(v, &cargo_type)) continue;

			/* Did the old vehicle carry anything? */
			if (cargo_type != CT_INVALID) {
				/* We can't refit the vehicle to carry the cargo we want */
				if (!HasBit(available_cargo_types, cargo_type)) continue;
			}
		}

		/* Check money.
		 * We want 2*(the price of the new vehicle) without looking at the value of the vehicle we are going to sell. */
		pending_replace = true;
		needed_money += 2 * Engine::Get(new_engine)->GetCost();
		if (needed_money > c->money) return false;
	}

	return pending_replace;
}

/**
 * Checks if the current order should be interrupted for a service-in-depot order.
 * @see NeedsServicing()
 * @return true if the current order should be interrupted.
 */
bool Vehicle::NeedsAutomaticServicing() const
{
	if (this->HasDepotOrder()) return false;
	if (this->current_order.IsType(OT_LOADING)) return false;
	if (this->current_order.IsType(OT_GOTO_DEPOT) && this->current_order.GetDepotOrderType() != ODTFB_SERVICE) return false;
	return NeedsServicing();
}

uint Vehicle::Crash(bool flooded)
{
	assert((this->vehstatus & VS_CRASHED) == 0);
	assert(this->Previous() == nullptr); // IsPrimaryVehicle fails for free-wagon-chains

	uint pass = 0;
	/* Stop the vehicle. */
	if (this->IsPrimaryVehicle()) this->vehstatus |= VS_STOPPED;
	/* crash all wagons, and count passengers */
	for (Vehicle *v = this; v != nullptr; v = v->Next()) {
		/* We do not transfer reserver cargo back, so TotalCount() instead of StoredCount() */
		if (IsCargoInClass(v->cargo_type, CC_PASSENGERS)) pass += v->cargo.TotalCount();
		v->vehstatus |= VS_CRASHED;
		v->MarkAllViewportsDirty();
	}

	/* Dirty some windows */
	InvalidateWindowClassesData(GetWindowClassForVehicleType(this->type), 0);
	SetWindowWidgetDirty(WC_VEHICLE_VIEW, this->index, WID_VV_START_STOP);
	SetWindowDirty(WC_VEHICLE_DETAILS, this->index);
	SetWindowDirty(WC_VEHICLE_DEPOT, this->tile);

	delete this->cargo_payment;
	assert(this->cargo_payment == nullptr); // cleared by ~CargoPayment

	return RandomRange(pass + 1); // Randomise deceased passengers.
}


/**
 * Displays a "NewGrf Bug" error message for a engine, and pauses the game if not networking.
 * @param engine The engine that caused the problem
 * @param part1  Part 1 of the error message, taking the grfname as parameter 1
 * @param part2  Part 2 of the error message, taking the engine as parameter 2
 * @param bug_type Flag to check and set in grfconfig
 * @param critical Shall the "OpenTTD might crash"-message be shown when the player tries to unpause?
 */
void ShowNewGrfVehicleError(EngineID engine, StringID part1, StringID part2, GRFBugs bug_type, bool critical)
{
	const Engine *e = Engine::Get(engine);
	GRFConfig *grfconfig = GetGRFConfig(e->GetGRFID());

	/* Missing GRF. Nothing useful can be done in this situation. */
	if (grfconfig == nullptr) return;

	if (!HasBit(grfconfig->grf_bugs, bug_type)) {
		SetBit(grfconfig->grf_bugs, bug_type);
		SetDParamStr(0, grfconfig->GetName());
		SetDParam(1, engine);
		ShowErrorMessage(part1, part2, WL_CRITICAL);
		if (!_networking) DoCommand(0, critical ? PM_PAUSED_ERROR : PM_PAUSED_NORMAL, 1, DC_EXEC, CMD_PAUSE);
	}

	/* debug output */
	char buffer[512];

	SetDParamStr(0, grfconfig->GetName());
	GetString(buffer, part1, lastof(buffer));
	Debug(grf, 0, "{}", buffer + 3);

	SetDParam(1, engine);
	GetString(buffer, part2, lastof(buffer));
	Debug(grf, 0, "{}", buffer + 3);
}

/**
 * Logs a bug in GRF and shows a warning message if this
 * is for the first time this happened.
 * @param u first vehicle of chain
 */
void VehicleLengthChanged(const Vehicle *u)
{
	/* show a warning once for each engine in whole game and once for each GRF after each game load */
	const Engine *engine = u->GetEngine();
	uint32 grfid = engine->grf_prop.grffile->grfid;
	GRFConfig *grfconfig = GetGRFConfig(grfid);
	if (GamelogGRFBugReverse(grfid, engine->grf_prop.local_id) || !HasBit(grfconfig->grf_bugs, GBUG_VEH_LENGTH)) {
		ShowNewGrfVehicleError(u->engine_type, STR_NEWGRF_BROKEN, STR_NEWGRF_BROKEN_VEHICLE_LENGTH, GBUG_VEH_LENGTH, true);
	}
}

/**
 * Vehicle constructor.
 * @param type Type of the new vehicle.
 */
Vehicle::Vehicle(VehicleType type)
{
	this->type               = type;
	this->coord.left         = INVALID_COORD;
	this->sprite_cache.old_coord.left = INVALID_COORD;
	this->group_id           = DEFAULT_GROUP;
	this->fill_percent_te_id = INVALID_TE_ID;
	this->first              = this;
	this->colourmap          = PAL_NONE;
	this->cargo_age_counter  = 1;
	this->last_station_visited = INVALID_STATION;
	this->last_loading_station = INVALID_STATION;
}

/**
 * Get a value for a vehicle's random_bits.
 * @return A random value from 0 to 255.
 */
byte VehicleRandomBits()
{
	return GB(Random(), 0, 8);
}

/* Size of the hash, 6 = 64 x 64, 7 = 128 x 128. Larger sizes will (in theory) reduce hash
 * lookup times at the expense of memory usage. */
const int HASH_BITS = 7;
const int HASH_SIZE = 1 << HASH_BITS;
const int HASH_MASK = HASH_SIZE - 1;
const int TOTAL_HASH_SIZE = 1 << (HASH_BITS * 2);
const int TOTAL_HASH_MASK = TOTAL_HASH_SIZE - 1;

/* Resolution of the hash, 0 = 1*1 tile, 1 = 2*2 tiles, 2 = 4*4 tiles, etc.
 * Profiling results show that 0 is fastest. */
const int HASH_RES = 0;

static Vehicle *_vehicle_tile_hash[TOTAL_HASH_SIZE];

static Vehicle *VehicleFromTileHash(int xl, int yl, int xu, int yu, void *data, VehicleFromPosProc *proc, bool find_first)
{
	for (int y = yl; ; y = (y + (1 << HASH_BITS)) & (HASH_MASK << HASH_BITS)) {
		for (int x = xl; ; x = (x + 1) & HASH_MASK) {
			Vehicle *v = _vehicle_tile_hash[(x + y) & TOTAL_HASH_MASK];
			for (; v != nullptr; v = v->hash_tile_next) {
				Vehicle *a = proc(v, data);
				if (find_first && a != nullptr) return a;
			}
			if (x == xu) break;
		}
		if (y == yu) break;
	}

	return nullptr;
}


/**
 * Helper function for FindVehicleOnPos/HasVehicleOnPos.
 * @note Do not call this function directly!
 * @param x    The X location on the map
 * @param y    The Y location on the map
 * @param data Arbitrary data passed to proc
 * @param proc The proc that determines whether a vehicle will be "found".
 * @param find_first Whether to return on the first found or iterate over
 *                   all vehicles
 * @return the best matching or first vehicle (depending on find_first).
 */
static Vehicle *VehicleFromPosXY(int x, int y, void *data, VehicleFromPosProc *proc, bool find_first)
{
	const int COLL_DIST = 6;

	/* Hash area to scan is from xl,yl to xu,yu */
	int xl = GB((x - COLL_DIST) / TILE_SIZE, HASH_RES, HASH_BITS);
	int xu = GB((x + COLL_DIST) / TILE_SIZE, HASH_RES, HASH_BITS);
	int yl = GB((y - COLL_DIST) / TILE_SIZE, HASH_RES, HASH_BITS) << HASH_BITS;
	int yu = GB((y + COLL_DIST) / TILE_SIZE, HASH_RES, HASH_BITS) << HASH_BITS;

	return VehicleFromTileHash(xl, yl, xu, yu, data, proc, find_first);
}

/**
 * Find a vehicle from a specific location. It will call proc for ALL vehicles
 * on the tile and YOU must make SURE that the "best one" is stored in the
 * data value and is ALWAYS the same regardless of the order of the vehicles
 * where proc was called on!
 * When you fail to do this properly you create an almost untraceable DESYNC!
 * @note The return value of proc will be ignored.
 * @note Use this when you have the intention that all vehicles
 *       should be iterated over.
 * @param x    The X location on the map
 * @param y    The Y location on the map
 * @param data Arbitrary data passed to proc
 * @param proc The proc that determines whether a vehicle will be "found".
 */
void FindVehicleOnPosXY(int x, int y, void *data, VehicleFromPosProc *proc)
{
	VehicleFromPosXY(x, y, data, proc, false);
}

/**
 * Checks whether a vehicle in on a specific location. It will call proc for
 * vehicles until it returns non-nullptr.
 * @note Use FindVehicleOnPosXY when you have the intention that all vehicles
 *       should be iterated over.
 * @param x    The X location on the map
 * @param y    The Y location on the map
 * @param data Arbitrary data passed to proc
 * @param proc The proc that determines whether a vehicle will be "found".
 * @return True if proc returned non-nullptr.
 */
bool HasVehicleOnPosXY(int x, int y, void *data, VehicleFromPosProc *proc)
{
	return VehicleFromPosXY(x, y, data, proc, true) != nullptr;
}

/**
 * Helper function for FindVehicleOnPos/HasVehicleOnPos.
 * @note Do not call this function directly!
 * @param tile The location on the map
 * @param data Arbitrary data passed to \a proc.
 * @param proc The proc that determines whether a vehicle will be "found".
 * @param find_first Whether to return on the first found or iterate over
 *                   all vehicles
 * @return the best matching or first vehicle (depending on find_first).
 */
static Vehicle *VehicleFromPos(TileIndex tile, void *data, VehicleFromPosProc *proc, bool find_first)
{
	int x = GB(TileX(tile), HASH_RES, HASH_BITS);
	int y = GB(TileY(tile), HASH_RES, HASH_BITS) << HASH_BITS;

	Vehicle *v = _vehicle_tile_hash[(x + y) & TOTAL_HASH_MASK];
	for (; v != nullptr; v = v->hash_tile_next) {
		if (v->tile != tile) continue;

		Vehicle *a = proc(v, data);
		if (find_first && a != nullptr) return a;
	}

	return nullptr;
}

/**
 * Find a vehicle from a specific location. It will call \a proc for ALL vehicles
 * on the tile and YOU must make SURE that the "best one" is stored in the
 * data value and is ALWAYS the same regardless of the order of the vehicles
 * where proc was called on!
 * When you fail to do this properly you create an almost untraceable DESYNC!
 * @note The return value of \a proc will be ignored.
 * @note Use this function when you have the intention that all vehicles
 *       should be iterated over.
 * @param tile The location on the map
 * @param data Arbitrary data passed to \a proc.
 * @param proc The proc that determines whether a vehicle will be "found".
 */
void FindVehicleOnPos(TileIndex tile, void *data, VehicleFromPosProc *proc)
{
	VehicleFromPos(tile, data, proc, false);
}

/**
 * Checks whether a vehicle is on a specific location. It will call \a proc for
 * vehicles until it returns non-nullptr.
 * @note Use #FindVehicleOnPos when you have the intention that all vehicles
 *       should be iterated over.
 * @param tile The location on the map
 * @param data Arbitrary data passed to \a proc.
 * @param proc The \a proc that determines whether a vehicle will be "found".
 * @return True if proc returned non-nullptr.
 */
bool HasVehicleOnPos(TileIndex tile, void *data, VehicleFromPosProc *proc)
{
	return VehicleFromPos(tile, data, proc, true) != nullptr;
}

/**
 * Callback that returns 'real' vehicles lower or at height \c *(int*)data .
 * @param v Vehicle to examine.
 * @param data Pointer to height data.
 * @return \a v if conditions are met, else \c nullptr.
 */
static Vehicle *EnsureNoVehicleProcZ(Vehicle *v, void *data)
{
	int z = *(int*)data;

	if (v->type == VEH_DISASTER || (v->type == VEH_AIRCRAFT && v->subtype == AIR_SHADOW)) return nullptr;
	if (v->z_pos > z) return nullptr;

	return v;
}

/**
 * Ensure there is no vehicle at the ground at the given position.
 * @param tile Position to examine.
 * @return Succeeded command (ground is free) or failed command (a vehicle is found).
 */
CommandCost EnsureNoVehicleOnGround(TileIndex tile)
{
	int z = GetTileMaxPixelZ(tile);

	/* Value v is not safe in MP games, however, it is used to generate a local
	 * error message only (which may be different for different machines).
	 * Such a message does not affect MP synchronisation.
	 */
	Vehicle *v = VehicleFromPos(tile, &z, &EnsureNoVehicleProcZ, true);
	if (v != nullptr) return_cmd_error(STR_ERROR_TRAIN_IN_THE_WAY + v->type);
	return CommandCost();
}

/** Procedure called for every vehicle found in tunnel/bridge in the hash map */
static Vehicle *GetVehicleTunnelBridgeProc(Vehicle *v, void *data)
{
	if (v->type != VEH_TRAIN && v->type != VEH_ROAD && v->type != VEH_SHIP) return nullptr;
	if (v == (const Vehicle *)data) return nullptr;

	return v;
}

/**
 * Finds vehicle in tunnel / bridge
 * @param tile first end
 * @param endtile second end
 * @param ignore Ignore this vehicle when searching
 * @return Succeeded command (if tunnel/bridge is free) or failed command (if a vehicle is using the tunnel/bridge).
 */
CommandCost TunnelBridgeIsFree(TileIndex tile, TileIndex endtile, const Vehicle *ignore)
{
	/* Value v is not safe in MP games, however, it is used to generate a local
	 * error message only (which may be different for different machines).
	 * Such a message does not affect MP synchronisation.
	 */
	Vehicle *v = VehicleFromPos(tile, const_cast<Vehicle *>(ignore), &GetVehicleTunnelBridgeProc, true);
	if (v == nullptr) v = VehicleFromPos(endtile, const_cast<Vehicle *>(ignore), &GetVehicleTunnelBridgeProc, true);

	if (v != nullptr) return_cmd_error(STR_ERROR_TRAIN_IN_THE_WAY + v->type);
	return CommandCost();
}

static Vehicle *EnsureNoTrainOnTrackProc(Vehicle *v, void *data)
{
	TrackBits rail_bits = *(TrackBits *)data;

	if (v->type != VEH_TRAIN) return nullptr;

	Train *t = Train::From(v);
	if ((t->track != rail_bits) && !TracksOverlap(t->track | rail_bits)) return nullptr;

	return v;
}

/**
 * Tests if a vehicle interacts with the specified track bits.
 * All track bits interact except parallel #TRACK_BIT_HORZ or #TRACK_BIT_VERT.
 *
 * @param tile The tile.
 * @param track_bits The track bits.
 * @return \c true if no train that interacts, is found. \c false if a train is found.
 */
CommandCost EnsureNoTrainOnTrackBits(TileIndex tile, TrackBits track_bits)
{
	/* Value v is not safe in MP games, however, it is used to generate a local
	 * error message only (which may be different for different machines).
	 * Such a message does not affect MP synchronisation.
	 */
	Vehicle *v = VehicleFromPos(tile, &track_bits, &EnsureNoTrainOnTrackProc, true);
	if (v != nullptr) return_cmd_error(STR_ERROR_TRAIN_IN_THE_WAY + v->type);
	return CommandCost();
}

static void UpdateVehicleTileHash(Vehicle *v, bool remove)
{
	Vehicle **old_hash = v->hash_tile_current;
	Vehicle **new_hash;

	if (remove) {
		new_hash = nullptr;
	} else {
		int x = GB(TileX(v->tile), HASH_RES, HASH_BITS);
		int y = GB(TileY(v->tile), HASH_RES, HASH_BITS) << HASH_BITS;
		new_hash = &_vehicle_tile_hash[(x + y) & TOTAL_HASH_MASK];
	}

	if (old_hash == new_hash) return;

	/* Remove from the old position in the hash table */
	if (old_hash != nullptr) {
		if (v->hash_tile_next != nullptr) v->hash_tile_next->hash_tile_prev = v->hash_tile_prev;
		*v->hash_tile_prev = v->hash_tile_next;
	}

	/* Insert vehicle at beginning of the new position in the hash table */
	if (new_hash != nullptr) {
		v->hash_tile_next = *new_hash;
		if (v->hash_tile_next != nullptr) v->hash_tile_next->hash_tile_prev = &v->hash_tile_next;
		v->hash_tile_prev = new_hash;
		*new_hash = v;
	}

	/* Remember current hash position */
	v->hash_tile_current = new_hash;
}

static Vehicle *_vehicle_viewport_hash[1 << (GEN_HASHX_BITS + GEN_HASHY_BITS)];

static void UpdateVehicleViewportHash(Vehicle *v, int x, int y, int old_x, int old_y)
{
	Vehicle **old_hash, **new_hash;

	new_hash = (x == INVALID_COORD) ? nullptr : &_vehicle_viewport_hash[GEN_HASH(x, y)];
	old_hash = (old_x == INVALID_COORD) ? nullptr : &_vehicle_viewport_hash[GEN_HASH(old_x, old_y)];

	if (old_hash == new_hash) return;

	/* remove from hash table? */
	if (old_hash != nullptr) {
		if (v->hash_viewport_next != nullptr) v->hash_viewport_next->hash_viewport_prev = v->hash_viewport_prev;
		*v->hash_viewport_prev = v->hash_viewport_next;
	}

	/* insert into hash table? */
	if (new_hash != nullptr) {
		v->hash_viewport_next = *new_hash;
		if (v->hash_viewport_next != nullptr) v->hash_viewport_next->hash_viewport_prev = &v->hash_viewport_next;
		v->hash_viewport_prev = new_hash;
		*new_hash = v;
	}
}

void ResetVehicleHash()
{
	for (Vehicle *v : Vehicle::Iterate()) { v->hash_tile_current = nullptr; }
	memset(_vehicle_viewport_hash, 0, sizeof(_vehicle_viewport_hash));
	memset(_vehicle_tile_hash, 0, sizeof(_vehicle_tile_hash));
}

void ResetVehicleColourMap()
{
	for (Vehicle *v : Vehicle::Iterate()) { v->colourmap = PAL_NONE; }
}

/**
 * List of vehicles that should check for autoreplace this tick.
 * Mapping of vehicle -> leave depot immediately after autoreplace.
 */
typedef SmallMap<Vehicle *, bool> AutoreplaceMap;
static AutoreplaceMap _vehicles_to_autoreplace;

void InitializeVehicles()
{
	_vehicles_to_autoreplace.clear();
	_vehicles_to_autoreplace.shrink_to_fit();
	ResetVehicleHash();
}

uint CountVehiclesInChain(const Vehicle *v)
{
	uint count = 0;
	do count++; while ((v = v->Next()) != nullptr);
	return count;
}

/**
 * Check if a vehicle is counted in num_engines in each company struct
 * @return true if the vehicle is counted in num_engines
 */
bool Vehicle::IsEngineCountable() const
{
	switch (this->type) {
		case VEH_AIRCRAFT: return Aircraft::From(this)->IsNormalAircraft(); // don't count plane shadows and helicopter rotors
		case VEH_TRAIN:
			return !this->IsArticulatedPart() && // tenders and other articulated parts
					!Train::From(this)->IsRearDualheaded(); // rear parts of multiheaded engines
		case VEH_ROAD: return RoadVehicle::From(this)->IsFrontEngine();
		case VEH_SHIP: return true;
		default: return false; // Only count company buildable vehicles
	}
}

/**
 * Check whether Vehicle::engine_type has any meaning.
 * @return true if the vehicle has a usable engine type.
 */
bool Vehicle::HasEngineType() const
{
	switch (this->type) {
		case VEH_AIRCRAFT: return Aircraft::From(this)->IsNormalAircraft();
		case VEH_TRAIN:
		case VEH_ROAD:
		case VEH_SHIP: return true;
		default: return false;
	}
}

/**
 * Retrieves the engine of the vehicle.
 * @return Engine of the vehicle.
 * @pre HasEngineType() == true
 */
const Engine *Vehicle::GetEngine() const
{
	return Engine::Get(this->engine_type);
}

/**
 * Retrieve the NewGRF the vehicle is tied to.
 * This is the GRF providing the Action 3 for the engine type.
 * @return NewGRF associated to the vehicle.
 */
const GRFFile *Vehicle::GetGRF() const
{
	return this->GetEngine()->GetGRF();
}

/**
 * Retrieve the GRF ID of the NewGRF the vehicle is tied to.
 * This is the GRF providing the Action 3 for the engine type.
 * @return GRF ID of the associated NewGRF.
 */
uint32 Vehicle::GetGRFID() const
{
	return this->GetEngine()->GetGRFID();
}

/**
 * Handle the pathfinding result, especially the lost status.
 * If the vehicle is now lost and wasn't previously fire an
 * event to the AIs and a news message to the user. If the
 * vehicle is not lost anymore remove the news message.
 * @param path_found Whether the vehicle has a path to its destination.
 */
void Vehicle::HandlePathfindingResult(bool path_found)
{
	if (path_found) {
		/* Route found, is the vehicle marked with "lost" flag? */
		if (!HasBit(this->vehicle_flags, VF_PATHFINDER_LOST)) return;

		/* Clear the flag as the PF's problem was solved. */
		ClrBit(this->vehicle_flags, VF_PATHFINDER_LOST);
		/* Delete the news item. */
		DeleteVehicleNews(this->index, STR_NEWS_VEHICLE_IS_LOST);
		return;
	}

	/* Were we already lost? */
	if (HasBit(this->vehicle_flags, VF_PATHFINDER_LOST)) return;

	/* It is first time the problem occurred, set the "lost" flag. */
	SetBit(this->vehicle_flags, VF_PATHFINDER_LOST);
	/* Notify user about the event. */
	AI::NewEvent(this->owner, new ScriptEventVehicleLost(this->index));
	if (_settings_client.gui.lost_vehicle_warn && this->owner == _local_company) {
		SetDParam(0, this->index);
		AddVehicleAdviceNewsItem(STR_NEWS_VEHICLE_IS_LOST, this->index);
	}
}

/** Destroy all stuff that (still) needs the virtual functions to work properly */
void Vehicle::PreDestructor()
{
	if (CleaningPool()) return;

	if (Station::IsValidID(this->last_station_visited)) {
		Station *st = Station::Get(this->last_station_visited);
		st->loading_vehicles.remove(this);

		HideFillingPercent(&this->fill_percent_te_id);
		this->CancelReservation(INVALID_STATION, st);
		delete this->cargo_payment;
		assert(this->cargo_payment == nullptr); // cleared by ~CargoPayment
	}

	if (this->IsEngineCountable()) {
		GroupStatistics::CountEngine(this, -1);
		if (this->IsPrimaryVehicle()) GroupStatistics::CountVehicle(this, -1);
		GroupStatistics::UpdateAutoreplace(this->owner);

		if (this->owner == _local_company) InvalidateAutoreplaceWindow(this->engine_type, this->group_id);
		DeleteGroupHighlightOfVehicle(this);
	}

	if (this->type == VEH_AIRCRAFT && this->IsPrimaryVehicle()) {
		Aircraft *a = Aircraft::From(this);
		Station *st = GetTargetAirportIfValid(a);
		if (st != nullptr) {
			const AirportFTA *layout = st->airport.GetFTA()->layout;
			CLRBITS(st->airport.flags, layout[a->previous_pos].block | layout[a->pos].block);
		}
	}


	if (this->type == VEH_ROAD && this->IsPrimaryVehicle()) {
		RoadVehicle *v = RoadVehicle::From(this);
		if (!(v->vehstatus & VS_CRASHED) && IsInsideMM(v->state, RVSB_IN_DT_ROAD_STOP, RVSB_IN_DT_ROAD_STOP_END)) {
			/* Leave the drive through roadstop, when you have not already left it. */
			RoadStop::GetByTile(v->tile, GetRoadStopType(v->tile))->Leave(v);
		}
	}

	if (this->Previous() == nullptr) {
		InvalidateWindowData(WC_VEHICLE_DEPOT, this->tile);
	}

	if (this->IsPrimaryVehicle()) {
		CloseWindowById(WC_VEHICLE_VIEW, this->index);
		CloseWindowById(WC_VEHICLE_ORDERS, this->index);
		CloseWindowById(WC_VEHICLE_REFIT, this->index);
		CloseWindowById(WC_VEHICLE_DETAILS, this->index);
		CloseWindowById(WC_VEHICLE_TIMETABLE, this->index);
		SetWindowDirty(WC_COMPANY, this->owner);
		OrderBackup::ClearVehicle(this);
	}
	InvalidateWindowClassesData(GetWindowClassForVehicleType(this->type), 0);

	this->cargo.Truncate();
	DeleteVehicleOrders(this);
	DeleteDepotHighlightOfVehicle(this);

	extern void StopGlobalFollowVehicle(const Vehicle *v);
	StopGlobalFollowVehicle(this);

	ReleaseDisastersTargetingVehicle(this->index);
}

Vehicle::~Vehicle()
{
	if (CleaningPool()) {
		this->cargo.OnCleanPool();
		return;
	}

	/* sometimes, eg. for disaster vehicles, when company bankrupts, when removing crashed/flooded vehicles,
	 * it may happen that vehicle chain is deleted when visible */
	if (!(this->vehstatus & VS_HIDDEN)) this->MarkAllViewportsDirty();

	Vehicle *v = this->Next();
	this->SetNext(nullptr);

	delete v;

	UpdateVehicleTileHash(this, true);
	UpdateVehicleViewportHash(this, INVALID_COORD, 0, this->sprite_cache.old_coord.left, this->sprite_cache.old_coord.top);
	DeleteVehicleNews(this->index, INVALID_STRING_ID);
	DeleteNewGRFInspectWindow(GetGrfSpecFeature(this->type), this->index);
}

/**
 * Adds a vehicle to the list of vehicles that visited a depot this tick
 * @param *v vehicle to add
 */
void VehicleEnteredDepotThisTick(Vehicle *v)
{
	/* Vehicle should stop in the depot if it was in 'stopping' state */
	_vehicles_to_autoreplace[v] = !(v->vehstatus & VS_STOPPED);

	/* We ALWAYS set the stopped state. Even when the vehicle does not plan on
	 * stopping in the depot, so we stop it to ensure that it will not reserve
	 * the path out of the depot before we might autoreplace it to a different
	 * engine. The new engine would not own the reserved path we store that we
	 * stopped the vehicle, so autoreplace can start it again */
	v->vehstatus |= VS_STOPPED;
}

/**
 * Increases the day counter for all vehicles and calls 1-day and 32-day handlers.
 * Each tick, it processes vehicles with "index % DAY_TICKS == _date_fract",
 * so each day, all vehicles are processes in DAY_TICKS steps.
 */
static void RunVehicleDayProc()
{
	if (_game_mode != GM_NORMAL) return;

	/* Run the day_proc for every DAY_TICKS vehicle starting at _date_fract. */
	for (size_t i = _date_fract; i < Vehicle::GetPoolSize(); i += DAY_TICKS) {
		Vehicle *v = Vehicle::Get(i);
		if (v == nullptr) continue;

		/* Call the 32-day callback if needed */
		if ((v->day_counter & 0x1F) == 0 && v->HasEngineType()) {
			uint16 callback = GetVehicleCallback(CBID_VEHICLE_32DAY_CALLBACK, 0, 0, v->engine_type, v);
			if (callback != CALLBACK_FAILED) {
				if (HasBit(callback, 0)) {
					TriggerVehicle(v, VEHICLE_TRIGGER_CALLBACK_32); // Trigger vehicle trigger 10
				}

				/* After a vehicle trigger, the graphics and properties of the vehicle could change.
				 * Note: MarkDirty also invalidates the palette, which is the meaning of bit 1. So, nothing special there. */
				if (callback != 0) v->First()->MarkDirty();

				if (callback & ~3) ErrorUnknownCallbackResult(v->GetGRFID(), CBID_VEHICLE_32DAY_CALLBACK, callback);
			}
		}

		/* This is called once per day for each vehicle, but not in the first tick of the day */
		v->OnNewDay();
	}
}

void CallVehicleTicks()
{
	_vehicles_to_autoreplace.clear();

	RunVehicleDayProc();

	{
		PerformanceMeasurer framerate(PFE_GL_ECONOMY);
		for (Station *st : Station::Iterate()) LoadUnloadStation(st);
	}
	PerformanceAccumulator::Reset(PFE_GL_TRAINS);
	PerformanceAccumulator::Reset(PFE_GL_ROADVEHS);
	PerformanceAccumulator::Reset(PFE_GL_SHIPS);
	PerformanceAccumulator::Reset(PFE_GL_AIRCRAFT);

	for (Vehicle *v : Vehicle::Iterate()) {
		[[maybe_unused]] size_t vehicle_index = v->index;

		/* Vehicle could be deleted in this tick */
		if (!v->Tick()) {
			assert(Vehicle::Get(vehicle_index) == nullptr);
			continue;
		}

		assert(Vehicle::Get(vehicle_index) == v);

		switch (v->type) {
			default: break;

			case VEH_TRAIN:
			case VEH_ROAD:
			case VEH_AIRCRAFT:
			case VEH_SHIP: {
				Vehicle *front = v->First();

				if (v->vcache.cached_cargo_age_period != 0) {
					v->cargo_age_counter = std::min(v->cargo_age_counter, v->vcache.cached_cargo_age_period);
					if (--v->cargo_age_counter == 0) {
						v->cargo.AgeCargo();
						v->cargo_age_counter = v->vcache.cached_cargo_age_period;
					}
				}

				/* Do not play any sound when crashed */
				if (front->vehstatus & VS_CRASHED) continue;

				/* Do not play any sound when in depot or tunnel */
				if (v->vehstatus & VS_HIDDEN) continue;

				/* Do not play any sound when stopped */
				if ((front->vehstatus & VS_STOPPED) && (front->type != VEH_TRAIN || front->cur_speed == 0)) continue;

				/* Check vehicle type specifics */
				switch (v->type) {
					case VEH_TRAIN:
						if (Train::From(v)->IsWagon()) continue;
						break;

					case VEH_ROAD:
						if (!RoadVehicle::From(v)->IsFrontEngine()) continue;
						break;

					case VEH_AIRCRAFT:
						if (!Aircraft::From(v)->IsNormalAircraft()) continue;
						break;

					default:
						break;
				}

				v->motion_counter += front->cur_speed;
				/* Play a running sound if the motion counter passes 256 (Do we not skip sounds?) */
				if (GB(v->motion_counter, 0, 8) < front->cur_speed) PlayVehicleSound(v, VSE_RUNNING);

				/* Play an alternating running sound every 16 ticks */
				if (GB(v->tick_counter, 0, 4) == 0) {
					/* Play running sound when speed > 0 and not braking */
					bool running = (front->cur_speed > 0) && !(front->vehstatus & (VS_STOPPED | VS_TRAIN_SLOWING));
					PlayVehicleSound(v, running ? VSE_RUNNING_16 : VSE_STOPPED_16);
				}

				break;
			}
		}
	}

	Backup<CompanyID> cur_company(_current_company, FILE_LINE);
	for (auto &it : _vehicles_to_autoreplace) {
		Vehicle *v = it.first;
		/* Autoreplace needs the current company set as the vehicle owner */
		cur_company.Change(v->owner);

		/* Start vehicle if we stopped them in VehicleEnteredDepotThisTick()
		 * We need to stop them between VehicleEnteredDepotThisTick() and here or we risk that
		 * they are already leaving the depot again before being replaced. */
		if (it.second) v->vehstatus &= ~VS_STOPPED;

		/* Store the position of the effect as the vehicle pointer will become invalid later */
		int x = v->x_pos;
		int y = v->y_pos;
		int z = v->z_pos;

		const Company *c = Company::Get(_current_company);
		SubtractMoneyFromCompany(CommandCost(EXPENSES_NEW_VEHICLES, (Money)c->settings.engine_renew_money));
		CommandCost res = DoCommand(0, v->index, 0, DC_EXEC, CMD_AUTOREPLACE_VEHICLE);
		SubtractMoneyFromCompany(CommandCost(EXPENSES_NEW_VEHICLES, -(Money)c->settings.engine_renew_money));

		if (!IsLocalCompany()) continue;

		if (res.Succeeded() && res.GetCost() != 0) {
			ShowCostOrIncomeAnimation(x, y, z, res.GetCost());
			continue;
		}

		StringID error_message = res.GetErrorMessage();
		if (error_message == STR_ERROR_AUTOREPLACE_NOTHING_TO_DO || error_message == INVALID_STRING_ID) continue;

		if (error_message == STR_ERROR_NOT_ENOUGH_CASH_REQUIRES_CURRENCY) error_message = STR_ERROR_AUTOREPLACE_MONEY_LIMIT;

		StringID message;
		if (error_message == STR_ERROR_TRAIN_TOO_LONG_AFTER_REPLACEMENT) {
			message = error_message;
		} else {
			message = STR_NEWS_VEHICLE_AUTORENEW_FAILED;
		}

		SetDParam(0, v->index);
		SetDParam(1, error_message);
		AddVehicleAdviceNewsItem(message, v->index);
	}

	cur_company.Restore();
}

/**
 * Add vehicle sprite for drawing to the screen.
 * @param v Vehicle to draw.
 */
static void DoDrawVehicle(const Vehicle *v)
{
	PaletteID pal = PAL_NONE;

	if (v->vehstatus & VS_DEFPAL) pal = (v->vehstatus & VS_CRASHED) ? PALETTE_CRASH : GetVehiclePalette(v);

	/* Check whether the vehicle shall be transparent due to the game state */
	bool shadowed = (v->vehstatus & VS_SHADOW) != 0;

	if (v->type == VEH_EFFECT) {
		/* Check whether the vehicle shall be transparent/invisible due to GUI settings.
		 * However, transparent smoke and bubbles look weird, so always hide them. */
		TransparencyOption to = EffectVehicle::From(v)->GetTransparencyOption();
		if (to != TO_INVALID && (IsTransparencySet(to) || IsInvisibilitySet(to))) return;
	}

	StartSpriteCombine();
	for (uint i = 0; i < v->sprite_cache.sprite_seq.count; ++i) {
		PaletteID pal2 = v->sprite_cache.sprite_seq.seq[i].pal;
		if (!pal2 || (v->vehstatus & VS_CRASHED)) pal2 = pal;
		AddSortableSpriteToDraw(v->sprite_cache.sprite_seq.seq[i].sprite, pal2, v->x_pos + v->x_offs, v->y_pos + v->y_offs,
			v->x_extent, v->y_extent, v->z_extent, v->z_pos, shadowed, v->x_bb_offs, v->y_bb_offs);
	}
	EndSpriteCombine();
}

/**
 * Add the vehicle sprites that should be drawn at a part of the screen.
 * @param dpi Rectangle being drawn.
 */
void ViewportAddVehicles(DrawPixelInfo *dpi)
{
	/* The bounding rectangle */
	const int l = dpi->left;
	const int r = dpi->left + dpi->width;
	const int t = dpi->top;
	const int b = dpi->top + dpi->height;

	/* Border size of MAX_VEHICLE_PIXEL_xy */
	const int xb = MAX_VEHICLE_PIXEL_X * ZOOM_LVL_BASE;
	const int yb = MAX_VEHICLE_PIXEL_Y * ZOOM_LVL_BASE;

	/* The hash area to scan */
	int xl, xu, yl, yu;

	if (dpi->width + xb < GEN_HASHX_SIZE) {
		xl = GEN_HASHX(l - xb);
		xu = GEN_HASHX(r);
	} else {
		/* scan whole hash row */
		xl = 0;
		xu = GEN_HASHX_MASK;
	}

	if (dpi->height + yb < GEN_HASHY_SIZE) {
		yl = GEN_HASHY(t - yb);
		yu = GEN_HASHY(b);
	} else {
		/* scan whole column */
		yl = 0;
		yu = GEN_HASHY_MASK;
	}

	for (int y = yl;; y = (y + GEN_HASHY_INC) & GEN_HASHY_MASK) {
		for (int x = xl;; x = (x + GEN_HASHX_INC) & GEN_HASHX_MASK) {
			const Vehicle *v = _vehicle_viewport_hash[x + y]; // already masked & 0xFFF

			while (v != nullptr) {

				if (!(v->vehstatus & VS_HIDDEN) &&
					l <= v->coord.right + xb &&
					t <= v->coord.bottom + yb &&
					r >= v->coord.left - xb &&
					b >= v->coord.top - yb)
				{
					/*
					 * This vehicle can potentially be drawn as part of this viewport and
					 * needs to be revalidated, as the sprite may not be correct.
					 */
					if (v->sprite_cache.revalidate_before_draw) {
						VehicleSpriteSeq seq;
						v->GetImage(v->direction, EIT_ON_MAP, &seq);

						if (seq.IsValid() && v->sprite_cache.sprite_seq != seq) {
							v->sprite_cache.sprite_seq = seq;
							/*
							 * A sprite change may also result in a bounding box change,
							 * so we need to update the bounding box again before we
							 * check to see if the vehicle should be drawn. Note that
							 * we can't interfere with the viewport hash at this point,
							 * so we keep the original hash on the assumption there will
							 * not be a significant change in the top and left coordinates
							 * of the vehicle.
							 */
							v->UpdateBoundingBoxCoordinates(false);

						}

						v->sprite_cache.revalidate_before_draw = false;
					}

					if (l <= v->coord.right &&
						t <= v->coord.bottom &&
						r >= v->coord.left &&
						b >= v->coord.top) DoDrawVehicle(v);
				}

				v = v->hash_viewport_next;
			}

			if (x == xu) break;
		}

		if (y == yu) break;
	}
}

/**
 * Find the vehicle close to the clicked coordinates.
 * @param vp Viewport clicked in.
 * @param x  X coordinate in the viewport.
 * @param y  Y coordinate in the viewport.
 * @return Closest vehicle, or \c nullptr if none found.
 */
Vehicle *CheckClickOnVehicle(const Viewport *vp, int x, int y)
{
	Vehicle *found = nullptr;
	uint dist, best_dist = UINT_MAX;

	if ((uint)(x -= vp->left) >= (uint)vp->width || (uint)(y -= vp->top) >= (uint)vp->height) return nullptr;

	x = ScaleByZoom(x, vp->zoom) + vp->virtual_left;
	y = ScaleByZoom(y, vp->zoom) + vp->virtual_top;

	for (Vehicle *v : Vehicle::Iterate()) {
		if ((v->vehstatus & (VS_HIDDEN | VS_UNCLICKABLE)) == 0 &&
				x >= v->coord.left && x <= v->coord.right &&
				y >= v->coord.top && y <= v->coord.bottom) {

			dist = std::max(
				abs(((v->coord.left + v->coord.right) >> 1) - x),
				abs(((v->coord.top + v->coord.bottom) >> 1) - y)
			);

			if (dist < best_dist) {
				found = v;
				best_dist = dist;
			}
		}
	}

	return found;
}

/**
 * Decrease the value of a vehicle.
 * @param v %Vehicle to devaluate.
 */
void DecreaseVehicleValue(Vehicle *v)
{
	v->value -= v->value >> 8;
	SetWindowDirty(WC_VEHICLE_DETAILS, v->index);
}

static const byte _breakdown_chance[64] = {
	  3,   3,   3,   3,   3,   3,   3,   3,
	  4,   4,   5,   5,   6,   6,   7,   7,
	  8,   8,   9,   9,  10,  10,  11,  11,
	 12,  13,  13,  13,  13,  14,  15,  16,
	 17,  19,  21,  25,  28,  31,  34,  37,
	 40,  44,  48,  52,  56,  60,  64,  68,
	 72,  80,  90, 100, 110, 120, 130, 140,
	150, 170, 190, 210, 230, 250, 250, 250,
};

void CheckVehicleBreakdown(Vehicle *v)
{
	int rel, rel_old;

	/* decrease reliability */
	if (!_settings_game.order.no_servicing_if_no_breakdowns ||
			_settings_game.difficulty.vehicle_breakdowns != 0) {
		v->reliability = rel = std::max((rel_old = v->reliability) - v->reliability_spd_dec, 0);
		if ((rel_old >> 8) != (rel >> 8)) SetWindowDirty(WC_VEHICLE_DETAILS, v->index);
	}

	if (v->breakdown_ctr != 0 || (v->vehstatus & VS_STOPPED) ||
			_settings_game.difficulty.vehicle_breakdowns < 1 ||
			v->cur_speed < 5 || _game_mode == GM_MENU) {
		return;
	}

	uint32 r = Random();

	/* increase chance of failure */
	int chance = v->breakdown_chance + 1;
	if (Chance16I(1, 25, r)) chance += 25;
	v->breakdown_chance = std::min(255, chance);

	/* calculate reliability value to use in comparison */
	rel = v->reliability;
	if (v->type == VEH_SHIP) rel += 0x6666;

	/* reduced breakdowns? */
	if (_settings_game.difficulty.vehicle_breakdowns == 1) rel += 0x6666;

	/* check if to break down */
	if (_breakdown_chance[(uint)std::min(rel, 0xffff) >> 10] <= v->breakdown_chance) {
		v->breakdown_ctr    = GB(r, 16, 6) + 0x3F;
		v->breakdown_delay  = GB(r, 24, 7) + 0x80;
		v->breakdown_chance = 0;
	}
}

/**
 * Handle all of the aspects of a vehicle breakdown
 * This includes adding smoke and sounds, and ending the breakdown when appropriate.
 * @return true iff the vehicle is stopped because of a breakdown
 * @note This function always returns false for aircraft, since these never stop for breakdowns
 */
bool Vehicle::HandleBreakdown()
{
	/* Possible states for Vehicle::breakdown_ctr
	 * 0  - vehicle is running normally
	 * 1  - vehicle is currently broken down
	 * 2  - vehicle is going to break down now
	 * >2 - vehicle is counting down to the actual breakdown event */
	switch (this->breakdown_ctr) {
		case 0:
			return false;

		case 2:
			this->breakdown_ctr = 1;

			if (this->breakdowns_since_last_service != 255) {
				this->breakdowns_since_last_service++;
			}

			if (this->type == VEH_AIRCRAFT) {
				/* Aircraft just need this flag, the rest is handled elsewhere */
				this->vehstatus |= VS_AIRCRAFT_BROKEN;
			} else {
				this->cur_speed = 0;

				if (!PlayVehicleSound(this, VSE_BREAKDOWN)) {
					bool train_or_ship = this->type == VEH_TRAIN || this->type == VEH_SHIP;
					SndPlayVehicleFx((_settings_game.game_creation.landscape != LT_TOYLAND) ?
						(train_or_ship ? SND_10_BREAKDOWN_TRAIN_SHIP : SND_0F_BREAKDOWN_ROADVEHICLE) :
						(train_or_ship ? SND_3A_BREAKDOWN_TRAIN_SHIP_TOYLAND : SND_35_BREAKDOWN_ROADVEHICLE_TOYLAND), this);
				}

				if (!(this->vehstatus & VS_HIDDEN) && !HasBit(EngInfo(this->engine_type)->misc_flags, EF_NO_BREAKDOWN_SMOKE)) {
					EffectVehicle *u = CreateEffectVehicleRel(this, 4, 4, 5, EV_BREAKDOWN_SMOKE);
					if (u != nullptr) u->animation_state = this->breakdown_delay * 2;
				}
			}

			this->MarkDirty(); // Update graphics after speed is zeroed
			SetWindowDirty(WC_VEHICLE_VIEW, this->index);
			SetWindowDirty(WC_VEHICLE_DETAILS, this->index);

			FALLTHROUGH;
		case 1:
			/* Aircraft breakdowns end only when arriving at the airport */
			if (this->type == VEH_AIRCRAFT) return false;

			/* For trains this function is called twice per tick, so decrease v->breakdown_delay at half the rate */
			if ((this->tick_counter & (this->type == VEH_TRAIN ? 3 : 1)) == 0) {
				if (--this->breakdown_delay == 0) {
					this->breakdown_ctr = 0;
					this->MarkDirty();
					SetWindowDirty(WC_VEHICLE_VIEW, this->index);
				}
			}
			return true;

		default:
			if (!this->current_order.IsType(OT_LOADING)) this->breakdown_ctr--;
			return false;
	}
}

/**
 * Update age of a vehicle.
 * @param v Vehicle to update.
 */
void AgeVehicle(Vehicle *v)
{
	if (v->age < MAX_DAY) {
		v->age++;
		if (v->IsPrimaryVehicle() && v->age == VEHICLE_PROFIT_MIN_AGE + 1) GroupStatistics::VehicleReachedProfitAge(v);
	}

	if (!v->IsPrimaryVehicle() && (v->type != VEH_TRAIN || !Train::From(v)->IsEngine())) return;

	int age = v->age - v->max_age;
	if (age == DAYS_IN_LEAP_YEAR * 0 || age == DAYS_IN_LEAP_YEAR * 1 ||
			age == DAYS_IN_LEAP_YEAR * 2 || age == DAYS_IN_LEAP_YEAR * 3 || age == DAYS_IN_LEAP_YEAR * 4) {
		v->reliability_spd_dec <<= 1;
	}

	SetWindowDirty(WC_VEHICLE_DETAILS, v->index);

	/* Don't warn about non-primary or not ours vehicles or vehicles that are crashed */
	if (v->Previous() != nullptr || v->owner != _local_company || (v->vehstatus & VS_CRASHED) != 0) return;

	const Company *c = Company::Get(v->owner);
	/* Don't warn if a renew is active */
	if (c->settings.engine_renew && v->GetEngine()->company_avail != 0) return;
	/* Don't warn if a replacement is active */
	if (EngineHasReplacementForCompany(c, v->engine_type, v->group_id)) return;

	StringID str;
	if (age == -DAYS_IN_LEAP_YEAR) {
		str = STR_NEWS_VEHICLE_IS_GETTING_OLD;
	} else if (age == 0) {
		str = STR_NEWS_VEHICLE_IS_GETTING_VERY_OLD;
	} else if (age > 0 && (age % DAYS_IN_LEAP_YEAR) == 0) {
		str = STR_NEWS_VEHICLE_IS_GETTING_VERY_OLD_AND;
	} else {
		return;
	}

	SetDParam(0, v->index);
	AddVehicleAdviceNewsItem(str, v->index);
}

/**
 * Calculates how full a vehicle is.
 * @param front The front vehicle of the consist to check.
 * @param colour The string to show depending on if we are unloading or loading
 * @return A percentage of how full the Vehicle is.
 *         Percentages are rounded towards 50%, so that 0% and 100% are only returned
 *         if the vehicle is completely empty or full.
 *         This is useful for both display and conditional orders.
 */
uint8 CalcPercentVehicleFilled(const Vehicle *front, StringID *colour)
{
	int count = 0;
	int max = 0;
	int cars = 0;
	int unloading = 0;
	bool loading = false;

	bool is_loading = front->current_order.IsType(OT_LOADING);

	/* The station may be nullptr when the (colour) string does not need to be set. */
	const Station *st = Station::GetIfValid(front->last_station_visited);
	assert(colour == nullptr || (st != nullptr && is_loading));

	bool order_no_load = is_loading && (front->current_order.GetLoadType() & OLFB_NO_LOAD);
	bool order_full_load = is_loading && (front->current_order.GetLoadType() & OLFB_FULL_LOAD);

	/* Count up max and used */
	for (const Vehicle *v = front; v != nullptr; v = v->Next()) {
		count += v->cargo.StoredCount();
		max += v->cargo_cap;
		if (v->cargo_cap != 0 && colour != nullptr) {
			unloading += HasBit(v->vehicle_flags, VF_CARGO_UNLOADING) ? 1 : 0;
			loading |= !order_no_load &&
					(order_full_load || st->goods[v->cargo_type].HasRating()) &&
					!HasBit(v->vehicle_flags, VF_LOADING_FINISHED) && !HasBit(v->vehicle_flags, VF_STOP_LOADING);
			cars++;
		}
	}

	if (colour != nullptr) {
		if (unloading == 0 && loading) {
			*colour = STR_PERCENT_UP;
		} else if (unloading == 0 && !loading) {
			*colour = STR_PERCENT_NONE;
		} else if (cars == unloading || !loading) {
			*colour = STR_PERCENT_DOWN;
		} else {
			*colour = STR_PERCENT_UP_DOWN;
		}
	}

	/* Train without capacity */
	if (max == 0) return 100;

	/* Return the percentage */
	if (count * 2 < max) {
		/* Less than 50%; round up, so that 0% means really empty. */
		return CeilDiv(count * 100, max);
	} else {
		/* More than 50%; round down, so that 100% means really full. */
		return (count * 100) / max;
	}
}

/**
 * Vehicle entirely entered the depot, update its status, orders, vehicle windows, service it, etc.
 * @param v Vehicle that entered a depot.
 */
void VehicleEnterDepot(Vehicle *v)
{
	/* Always work with the front of the vehicle */
	assert(v == v->First());

	switch (v->type) {
		case VEH_TRAIN: {
			Train *t = Train::From(v);
			SetWindowClassesDirty(WC_TRAINS_LIST);
			/* Clear path reservation */
			SetDepotReservation(t->tile, false);
			if (_settings_client.gui.show_track_reservation) MarkTileDirtyByTile(t->tile);

			UpdateSignalsOnSegment(t->tile, INVALID_DIAGDIR, t->owner);
			t->wait_counter = 0;
			t->force_proceed = TFP_NONE;
			ClrBit(t->flags, VRF_TOGGLE_REVERSE);
			t->ConsistChanged(CCF_ARRANGE);
			break;
		}

		case VEH_ROAD:
			SetWindowClassesDirty(WC_ROADVEH_LIST);
			break;

		case VEH_SHIP: {
			SetWindowClassesDirty(WC_SHIPS_LIST);
			Ship *ship = Ship::From(v);
			ship->state = TRACK_BIT_DEPOT;
			ship->UpdateCache();
			ship->UpdateViewport(true, true);
			SetWindowDirty(WC_VEHICLE_DEPOT, v->tile);
			break;
		}

		case VEH_AIRCRAFT:
			SetWindowClassesDirty(WC_AIRCRAFT_LIST);
			HandleAircraftEnterHangar(Aircraft::From(v));
			break;
		default: NOT_REACHED();
	}
	SetWindowDirty(WC_VEHICLE_VIEW, v->index);

	if (v->type != VEH_TRAIN) {
		/* Trains update the vehicle list when the first unit enters the depot and calls VehicleEnterDepot() when the last unit enters.
		 * We only increase the number of vehicles when the first one enters, so we will not need to search for more vehicles in the depot */
		InvalidateWindowData(WC_VEHICLE_DEPOT, v->tile);
	}
	SetWindowDirty(WC_VEHICLE_DEPOT, v->tile);

	v->vehstatus |= VS_HIDDEN;
	v->cur_speed = 0;

	VehicleServiceInDepot(v);

	/* After a vehicle trigger, the graphics and properties of the vehicle could change. */
	TriggerVehicle(v, VEHICLE_TRIGGER_DEPOT);
	v->MarkDirty();

	InvalidateWindowData(WC_VEHICLE_VIEW, v->index);

	if (v->current_order.IsType(OT_GOTO_DEPOT)) {
		SetWindowDirty(WC_VEHICLE_VIEW, v->index);

		const Order *real_order = v->GetOrder(v->cur_real_order_index);

		/* Test whether we are heading for this depot. If not, do nothing.
		 * Note: The target depot for nearest-/manual-depot-orders is only updated on junctions, but we want to accept every depot. */
		if ((v->current_order.GetDepotOrderType() & ODTFB_PART_OF_ORDERS) &&
				real_order != nullptr && !(real_order->GetDepotActionType() & ODATFB_NEAREST_DEPOT) &&
				v->current_order.GetDestination() != depot_id) {
			/* We are heading for another depot, keep driving. */
			return;
		}

		if (v->current_order.IsRefit()) {
			Backup<CompanyID> cur_company(_current_company, v->owner, FILE_LINE);
			CommandCost cost = DoCommand(v->tile, v->index, v->current_order.GetRefitCargo() | 0xFF << 8, DC_EXEC, GetCmdRefitVeh(v));
			cur_company.Restore();

			if (cost.Failed()) {
				_vehicles_to_autoreplace[v] = false;
				if (v->owner == _local_company) {
					/* Notify the user that we stopped the vehicle */
					SetDParam(0, v->index);
					AddVehicleAdviceNewsItem(STR_NEWS_ORDER_REFIT_FAILED, v->index);
				}
			} else if (cost.GetCost() != 0) {
				v->profit_this_year -= cost.GetCost() << 8;
				if (v->owner == _local_company) {
					ShowCostOrIncomeAnimation(v->x_pos, v->y_pos, v->z_pos, cost.GetCost());
				}
			}
		}

		if (v->current_order.GetDepotOrderType() & ODTFB_PART_OF_ORDERS) {
			/* Part of orders */
			v->DeleteUnreachedImplicitOrders();
			UpdateVehicleTimetable(v, true);
			v->IncrementImplicitOrderIndex();
		}
		if (v->current_order.GetDepotActionType() & ODATFB_HALT) {
			/* Vehicles are always stopped on entering depots. Do not restart this one. */
			_vehicles_to_autoreplace[v] = false;
			/* Invalidate last_loading_station. As the link from the station
			 * before the stop to the station after the stop can't be predicted
			 * we shouldn't construct it when the vehicle visits the next stop. */
			v->last_loading_station = INVALID_STATION;
			if (v->owner == _local_company) {
				SetDParam(0, v->index);
				AddVehicleAdviceNewsItem(STR_NEWS_TRAIN_IS_WAITING + v->type, v->index);
			}
			AI::NewEvent(v->owner, new ScriptEventVehicleWaitingInDepot(v->index));
		}
		v->current_order.MakeDummy();
	}
}


/**
 * Update the position of the vehicle. This will update the hash that tells
 *  which vehicles are on a tile.
 */
void Vehicle::UpdatePosition()
{
	UpdateVehicleTileHash(this, false);
}

/**
 * Update the bounding box co-ordinates of the vehicle
 * @param update_cache Update the cached values for previous co-ordinate values
*/
void Vehicle::UpdateBoundingBoxCoordinates(bool update_cache) const
{
	Rect new_coord;
	this->sprite_cache.sprite_seq.GetBounds(&new_coord);

	Point pt = RemapCoords(this->x_pos + this->x_offs, this->y_pos + this->y_offs, this->z_pos);
	new_coord.left   += pt.x;
	new_coord.top    += pt.y;
	new_coord.right  += pt.x + 2 * ZOOM_LVL_BASE;
	new_coord.bottom += pt.y + 2 * ZOOM_LVL_BASE;

	if (update_cache) {
		/*
		 * If the old coordinates are invalid, set the cache to the new coordinates for correct
		 * behaviour the next time the coordinate cache is checked.
		 */
		this->sprite_cache.old_coord = this->coord.left == INVALID_COORD ? new_coord : this->coord;
	}
	else {
		/* Extend the bounds of the existing cached bounding box so the next dirty window is correct */
		this->sprite_cache.old_coord.left   = std::min(this->sprite_cache.old_coord.left,   this->coord.left);
		this->sprite_cache.old_coord.top    = std::min(this->sprite_cache.old_coord.top,    this->coord.top);
		this->sprite_cache.old_coord.right  = std::max(this->sprite_cache.old_coord.right,  this->coord.right);
		this->sprite_cache.old_coord.bottom = std::max(this->sprite_cache.old_coord.bottom, this->coord.bottom);
	}

	this->coord = new_coord;
}

/**
 * Update the vehicle on the viewport, updating the right hash and setting the new coordinates.
 * @param dirty Mark the (new and old) coordinates of the vehicle as dirty.
 */
void Vehicle::UpdateViewport(bool dirty)
{
	/* If the existing cache is invalid we should ignore it, as it will be set to the current coords by UpdateBoundingBoxCoordinates */
	bool ignore_cached_coords = this->sprite_cache.old_coord.left == INVALID_COORD;

	this->UpdateBoundingBoxCoordinates(true);

	if (ignore_cached_coords) {
		UpdateVehicleViewportHash(this, this->coord.left, this->coord.top, INVALID_COORD, INVALID_COORD);
	} else {
		UpdateVehicleViewportHash(this, this->coord.left, this->coord.top, this->sprite_cache.old_coord.left, this->sprite_cache.old_coord.top);
	}

	if (dirty) {
		if (ignore_cached_coords) {
			this->sprite_cache.is_viewport_candidate = this->MarkAllViewportsDirty();
		} else {
			this->sprite_cache.is_viewport_candidate = ::MarkAllViewportsDirty(
				std::min(this->sprite_cache.old_coord.left, this->coord.left),
				std::min(this->sprite_cache.old_coord.top, this->coord.top),
				std::max(this->sprite_cache.old_coord.right, this->coord.right),
				std::max(this->sprite_cache.old_coord.bottom, this->coord.bottom));
		}
	}
}

/**
 * Update the position of the vehicle, and update the viewport.
 */
void Vehicle::UpdatePositionAndViewport()
{
	this->UpdatePosition();
	this->UpdateViewport(true);
}

/**
 * Marks viewports dirty where the vehicle's image is.
 * @return true if at least one viewport has a dirty block
 */
bool Vehicle::MarkAllViewportsDirty() const
{
	return ::MarkAllViewportsDirty(this->coord.left, this->coord.top, this->coord.right, this->coord.bottom);
}

/**
 * Get position information of a vehicle when moving one pixel in the direction it is facing
 * @param v Vehicle to move
 * @return Position information after the move
 */
GetNewVehiclePosResult GetNewVehiclePos(const Vehicle *v)
{
	static const int8 _delta_coord[16] = {
		-1,-1,-1, 0, 1, 1, 1, 0, /* x */
		-1, 0, 1, 1, 1, 0,-1,-1, /* y */
	};

	int x = v->x_pos + _delta_coord[v->direction];
	int y = v->y_pos + _delta_coord[v->direction + 8];

	GetNewVehiclePosResult gp;
	gp.x = x;
	gp.y = y;
	gp.old_tile = v->tile;
	gp.new_tile = TileVirtXY(x, y);
	return gp;
}

static const Direction _new_direction_table[] = {
	DIR_N,  DIR_NW, DIR_W,
	DIR_NE, DIR_SE, DIR_SW,
	DIR_E,  DIR_SE, DIR_S
};

Direction GetDirectionTowards(const Vehicle *v, int x, int y)
{
	int i = 0;

	if (y >= v->y_pos) {
		if (y != v->y_pos) i += 3;
		i += 3;
	}

	if (x >= v->x_pos) {
		if (x != v->x_pos) i++;
		i++;
	}

	Direction dir = v->direction;

	DirDiff dirdiff = DirDifference(_new_direction_table[i], dir);
	if (dirdiff == DIRDIFF_SAME) return dir;
	return ChangeDir(dir, dirdiff > DIRDIFF_REVERSE ? DIRDIFF_45LEFT : DIRDIFF_45RIGHT);
}

/**
 * Call the tile callback function for a vehicle entering a tile
 * @param v    Vehicle entering the tile
 * @param tile Tile entered
 * @param x    X position
 * @param y    Y position
 * @return Some meta-data over the to be entered tile.
 * @see VehicleEnterTileStatus to see what the bits in the return value mean.
 */
VehicleEnterTileStatus VehicleEnterTile(Vehicle *v, TileIndex tile, int x, int y)
{
	return _tile_type_procs[GetTileType(tile)]->vehicle_enter_tile_proc(v, tile, x, y);
}

/**
 * Initializes the structure. Vehicle unit numbers are supposed not to change after
 * struct initialization, except after each call to this->NextID() the returned value
 * is assigned to a vehicle.
 * @param type type of vehicle
 * @param owner owner of vehicles
 */
FreeUnitIDGenerator::FreeUnitIDGenerator(VehicleType type, CompanyID owner) : cache(nullptr), maxid(0), curid(0)
{
	/* Find maximum */
	for (const Vehicle *v : Vehicle::Iterate()) {
		if (v->type == type && v->owner == owner) {
			this->maxid = std::max<UnitID>(this->maxid, v->unitnumber);
		}
	}

	if (this->maxid == 0) return;

	/* Reserving 'maxid + 2' because we need:
	 * - space for the last item (with v->unitnumber == maxid)
	 * - one free slot working as loop terminator in FreeUnitIDGenerator::NextID() */
	this->cache = CallocT<bool>(this->maxid + 2);

	/* Fill the cache */
	for (const Vehicle *v : Vehicle::Iterate()) {
		if (v->type == type && v->owner == owner) {
			this->cache[v->unitnumber] = true;
		}
	}
}

/** Returns next free UnitID. Supposes the last returned value was assigned to a vehicle. */
UnitID FreeUnitIDGenerator::NextID()
{
	if (this->maxid <= this->curid) return ++this->curid;

	while (this->cache[++this->curid]) { } // it will stop, we reserved more space than needed

	return this->curid;
}

/**
 * Get an unused unit number for a vehicle (if allowed).
 * @param type Type of vehicle
 * @return A unused unit number for the given type of vehicle if it is allowed to build one, else \c UINT16_MAX.
 */
UnitID GetFreeUnitNumber(VehicleType type)
{
	/* Check whether it is allowed to build another vehicle. */
	uint max_veh;
	switch (type) {
		case VEH_TRAIN:    max_veh = _settings_game.vehicle.max_trains;   break;
		case VEH_ROAD:     max_veh = _settings_game.vehicle.max_roadveh;  break;
		case VEH_SHIP:     max_veh = _settings_game.vehicle.max_ships;    break;
		case VEH_AIRCRAFT: max_veh = _settings_game.vehicle.max_aircraft; break;
		default: NOT_REACHED();
	}

	const Company *c = Company::Get(_current_company);
	if (c->group_all[type].num_vehicle >= max_veh) return UINT16_MAX; // Currently already at the limit, no room to make a new one.

	FreeUnitIDGenerator gen(type, _current_company);

	return gen.NextID();
}


/**
 * Check whether we can build infrastructure for the given
 * vehicle type. This to disable building stations etc. when
 * you are not allowed/able to have the vehicle type yet.
 * @param type the vehicle type to check this for
 * @return true if there is any reason why you may build
 *         the infrastructure for the given vehicle type
 */
bool CanBuildVehicleInfrastructure(VehicleType type, byte subtype)
{
	assert(IsCompanyBuildableVehicleType(type));

	if (!Company::IsValidID(_local_company)) return false;

	UnitID max;
	switch (type) {
		case VEH_TRAIN:
			if (!HasAnyRailtypesAvail(_local_company)) return false;
			max = _settings_game.vehicle.max_trains;
			break;
		case VEH_ROAD:
			if (!HasAnyRoadTypesAvail(_local_company, (RoadTramType)subtype)) return false;
			max = _settings_game.vehicle.max_roadveh;
			break;
		case VEH_SHIP:     max = _settings_game.vehicle.max_ships; break;
		case VEH_AIRCRAFT: max = _settings_game.vehicle.max_aircraft; break;
		default: NOT_REACHED();
	}

	/* We can build vehicle infrastructure when we may build the vehicle type */
	if (max > 0) {
		/* Can we actually build the vehicle type? */
		for (const Engine *e : Engine::IterateType(type)) {
			if (type == VEH_ROAD && GetRoadTramType(e->u.road.roadtype) != (RoadTramType)subtype) continue;
			if (HasBit(e->company_avail, _local_company)) return true;
		}
		return false;
	}

	/* We should be able to build infrastructure when we have the actual vehicle type */
	for (const Vehicle *v : Vehicle::Iterate()) {
		if (v->type == VEH_ROAD && GetRoadTramType(RoadVehicle::From(v)->roadtype) != (RoadTramType)subtype) continue;
		if (v->owner == _local_company && v->type == type) return true;
	}

	return false;
}


/**
 * Determines the #LiveryScheme for a vehicle.
 * @param engine_type Engine of the vehicle.
 * @param parent_engine_type Engine of the front vehicle, #INVALID_ENGINE if vehicle is at front itself.
 * @param v the vehicle, \c nullptr if in purchase list etc.
 * @return livery scheme to use.
 */
LiveryScheme GetEngineLiveryScheme(EngineID engine_type, EngineID parent_engine_type, const Vehicle *v)
{
	CargoID cargo_type = v == nullptr ? (CargoID)CT_INVALID : v->cargo_type;
	const Engine *e = Engine::Get(engine_type);
	switch (e->type) {
		default: NOT_REACHED();
		case VEH_TRAIN:
			if (v != nullptr && parent_engine_type != INVALID_ENGINE && (UsesWagonOverride(v) || (v->IsArticulatedPart() && e->u.rail.railveh_type != RAILVEH_WAGON))) {
				/* Wagonoverrides use the colour scheme of the front engine.
				 * Articulated parts use the colour scheme of the first part. (Not supported for articulated wagons) */
				engine_type = parent_engine_type;
				e = Engine::Get(engine_type);
				/* Note: Luckily cargo_type is not needed for engines */
			}

			if (cargo_type == CT_INVALID) cargo_type = e->GetDefaultCargoType();
			if (cargo_type == CT_INVALID) cargo_type = CT_GOODS; // The vehicle does not carry anything, let's pick some freight cargo
			if (e->u.rail.railveh_type == RAILVEH_WAGON) {
				if (!CargoSpec::Get(cargo_type)->is_freight) {
					if (parent_engine_type == INVALID_ENGINE) {
						return LS_PASSENGER_WAGON_STEAM;
					} else {
						bool is_mu = HasBit(EngInfo(parent_engine_type)->misc_flags, EF_RAIL_IS_MU);
						switch (RailVehInfo(parent_engine_type)->engclass) {
							default: NOT_REACHED();
							case EC_STEAM:    return LS_PASSENGER_WAGON_STEAM;
							case EC_DIESEL:   return is_mu ? LS_DMU : LS_PASSENGER_WAGON_DIESEL;
							case EC_ELECTRIC: return is_mu ? LS_EMU : LS_PASSENGER_WAGON_ELECTRIC;
							case EC_MONORAIL: return LS_PASSENGER_WAGON_MONORAIL;
							case EC_MAGLEV:   return LS_PASSENGER_WAGON_MAGLEV;
						}
					}
				} else {
					return LS_FREIGHT_WAGON;
				}
			} else {
				bool is_mu = HasBit(e->info.misc_flags, EF_RAIL_IS_MU);

				switch (e->u.rail.engclass) {
					default: NOT_REACHED();
					case EC_STEAM:    return LS_STEAM;
					case EC_DIESEL:   return is_mu ? LS_DMU : LS_DIESEL;
					case EC_ELECTRIC: return is_mu ? LS_EMU : LS_ELECTRIC;
					case EC_MONORAIL: return LS_MONORAIL;
					case EC_MAGLEV:   return LS_MAGLEV;
				}
			}

		case VEH_ROAD:
			/* Always use the livery of the front */
			if (v != nullptr && parent_engine_type != INVALID_ENGINE) {
				engine_type = parent_engine_type;
				e = Engine::Get(engine_type);
				cargo_type = v->First()->cargo_type;
			}
			if (cargo_type == CT_INVALID) cargo_type = e->GetDefaultCargoType();
			if (cargo_type == CT_INVALID) cargo_type = CT_GOODS; // The vehicle does not carry anything, let's pick some freight cargo

			/* Important: Use Tram Flag of front part. Luckily engine_type refers to the front part here. */
			if (HasBit(e->info.misc_flags, EF_ROAD_TRAM)) {
				/* Tram */
				return IsCargoInClass(cargo_type, CC_PASSENGERS) ? LS_PASSENGER_TRAM : LS_FREIGHT_TRAM;
			} else {
				/* Bus or truck */
				return IsCargoInClass(cargo_type, CC_PASSENGERS) ? LS_BUS : LS_TRUCK;
			}

		case VEH_SHIP:
			if (cargo_type == CT_INVALID) cargo_type = e->GetDefaultCargoType();
			if (cargo_type == CT_INVALID) cargo_type = CT_GOODS; // The vehicle does not carry anything, let's pick some freight cargo
			return IsCargoInClass(cargo_type, CC_PASSENGERS) ? LS_PASSENGER_SHIP : LS_FREIGHT_SHIP;

		case VEH_AIRCRAFT:
			switch (e->u.air.subtype) {
				case AIR_HELI: return LS_HELICOPTER;
				case AIR_CTOL: return LS_SMALL_PLANE;
				case AIR_CTOL | AIR_FAST: return LS_LARGE_PLANE;
				default: NOT_REACHED();
			}
	}
}

/**
 * Determines the livery for a vehicle.
 * @param engine_type EngineID of the vehicle
 * @param company Owner of the vehicle
 * @param parent_engine_type EngineID of the front vehicle. INVALID_VEHICLE if vehicle is at front itself.
 * @param v the vehicle. nullptr if in purchase list etc.
 * @param livery_setting The livery settings to use for acquiring the livery information.
 * @return livery to use
 */
const Livery *GetEngineLivery(EngineID engine_type, CompanyID company, EngineID parent_engine_type, const Vehicle *v, byte livery_setting)
{
	const Company *c = Company::Get(company);
	LiveryScheme scheme = LS_DEFAULT;

	if (livery_setting == LIT_ALL || (livery_setting == LIT_COMPANY && company == _local_company)) {
		if (v != nullptr) {
			const Group *g = Group::GetIfValid(v->First()->group_id);
			if (g != nullptr) {
				/* Traverse parents until we find a livery or reach the top */
				while (g->livery.in_use == 0 && g->parent != INVALID_GROUP) {
					g = Group::Get(g->parent);
				}
				if (g->livery.in_use != 0) return &g->livery;
			}
		}

		/* The default livery is always available for use, but its in_use flag determines
		 * whether any _other_ liveries are in use. */
		if (c->livery[LS_DEFAULT].in_use != 0) {
			/* Determine the livery scheme to use */
			scheme = GetEngineLiveryScheme(engine_type, parent_engine_type, v);
		}
	}

	return &c->livery[scheme];
}


static PaletteID GetEngineColourMap(EngineID engine_type, CompanyID company, EngineID parent_engine_type, const Vehicle *v)
{
	PaletteID map = (v != nullptr) ? v->colourmap : PAL_NONE;

	/* Return cached value if any */
	if (map != PAL_NONE) return map;

	const Engine *e = Engine::Get(engine_type);

	/* Check if we should use the colour map callback */
	if (HasBit(e->info.callback_mask, CBM_VEHICLE_COLOUR_REMAP)) {
		uint16 callback = GetVehicleCallback(CBID_VEHICLE_COLOUR_MAPPING, 0, 0, engine_type, v);
		/* Failure means "use the default two-colour" */
		if (callback != CALLBACK_FAILED) {
			static_assert(PAL_NONE == 0); // Returning 0x4000 (resp. 0xC000) coincidences with default value (PAL_NONE)
			map = GB(callback, 0, 14);
			/* If bit 14 is set, then the company colours are applied to the
			 * map else it's returned as-is. */
			if (!HasBit(callback, 14)) {
				/* Update cache */
				if (v != nullptr) const_cast<Vehicle *>(v)->colourmap = map;
				return map;
			}
		}
	}

	bool twocc = HasBit(e->info.misc_flags, EF_USES_2CC);

	if (map == PAL_NONE) map = twocc ? (PaletteID)SPR_2CCMAP_BASE : (PaletteID)PALETTE_RECOLOUR_START;

	/* Spectator has news shown too, but has invalid company ID - as well as dedicated server */
	if (!Company::IsValidID(company)) return map;

	const Livery *livery = GetEngineLivery(engine_type, company, parent_engine_type, v, _settings_client.gui.liveries);

	map += livery->colour1;
	if (twocc) map += livery->colour2 * 16;

	/* Update cache */
	if (v != nullptr) const_cast<Vehicle *>(v)->colourmap = map;
	return map;
}

/**
 * Get the colour map for an engine. This used for unbuilt engines in the user interface.
 * @param engine_type ID of engine
 * @param company ID of company
 * @return A ready-to-use palette modifier
 */
PaletteID GetEnginePalette(EngineID engine_type, CompanyID company)
{
	return GetEngineColourMap(engine_type, company, INVALID_ENGINE, nullptr);
}

/**
 * Get the colour map for a vehicle.
 * @param v Vehicle to get colour map for
 * @return A ready-to-use palette modifier
 */
PaletteID GetVehiclePalette(const Vehicle *v)
{
	if (v->IsGroundVehicle()) {
		return GetEngineColourMap(v->engine_type, v->owner, v->GetGroundVehicleCache()->first_engine, v);
	}

	return GetEngineColourMap(v->engine_type, v->owner, INVALID_ENGINE, v);
}

/**
 * Delete all implicit orders which were not reached.
 */
void Vehicle::DeleteUnreachedImplicitOrders()
{
	if (this->IsGroundVehicle()) {
		uint16 &gv_flags = this->GetGroundVehicleFlags();
		if (HasBit(gv_flags, GVF_SUPPRESS_IMPLICIT_ORDERS)) {
			/* Do not delete orders, only skip them */
			ClrBit(gv_flags, GVF_SUPPRESS_IMPLICIT_ORDERS);
			this->cur_implicit_order_index = this->cur_real_order_index;
			InvalidateVehicleOrder(this, 0);
			return;
		}
	}

	const Order *order = this->GetOrder(this->cur_implicit_order_index);
	while (order != nullptr) {
		if (this->cur_implicit_order_index == this->cur_real_order_index) break;

		if (order->IsType(OT_IMPLICIT)) {
			DeleteOrder(this, this->cur_implicit_order_index);
			/* DeleteOrder does various magic with order_indices, so resync 'order' with 'cur_implicit_order_index' */
			order = this->GetOrder(this->cur_implicit_order_index);
		} else {
			/* Skip non-implicit orders, e.g. service-orders */
			order = order->next;
			this->cur_implicit_order_index++;
		}

		/* Wrap around */
		if (order == nullptr) {
			order = this->GetOrder(0);
			this->cur_implicit_order_index = 0;
		}
	}
}

/**
 * Prepare everything to begin the loading when arriving at a station.
 * @pre IsTileType(this->tile, MP_STATION) || this->type == VEH_SHIP.
 */
void Vehicle::BeginLoading()
{
	assert(IsTileType(this->tile, MP_STATION) || this->type == VEH_SHIP);

	if (this->current_order.IsType(OT_GOTO_STATION) &&
			this->current_order.GetDestination() == this->last_station_visited) {
		this->DeleteUnreachedImplicitOrders();

		/* Now both order indices point to the destination station, and we can start loading */
		this->current_order.MakeLoading(true);
		UpdateVehicleTimetable(this, true);

		/* Furthermore add the Non Stop flag to mark that this station
		 * is the actual destination of the vehicle, which is (for example)
		 * necessary to be known for HandleTrainLoading to determine
		 * whether the train is lost or not; not marking a train lost
		 * that arrives at random stations is bad. */
		this->current_order.SetNonStopType(ONSF_NO_STOP_AT_ANY_STATION);

	} else {
		/* We weren't scheduled to stop here. Insert an implicit order
		 * to show that we are stopping here.
		 * While only groundvehicles have implicit orders, e.g. aircraft might still enter
		 * the 'wrong' terminal when skipping orders etc. */
		Order *in_list = this->GetOrder(this->cur_implicit_order_index);
		if (this->IsGroundVehicle() &&
				(in_list == nullptr || !in_list->IsType(OT_IMPLICIT) ||
				in_list->GetDestination() != this->last_station_visited)) {
			bool suppress_implicit_orders = HasBit(this->GetGroundVehicleFlags(), GVF_SUPPRESS_IMPLICIT_ORDERS);
			/* Do not create consecutive duplicates of implicit orders */
			Order *prev_order = this->cur_implicit_order_index > 0 ? this->GetOrder(this->cur_implicit_order_index - 1) : (this->GetNumOrders() > 1 ? this->GetLastOrder() : nullptr);
			if (prev_order == nullptr ||
					(!prev_order->IsType(OT_IMPLICIT) && !prev_order->IsType(OT_GOTO_STATION)) ||
					prev_order->GetDestination() != this->last_station_visited) {

				/* Prefer deleting implicit orders instead of inserting new ones,
				 * so test whether the right order follows later. In case of only
				 * implicit orders treat the last order in the list like an
				 * explicit one, except if the overall number of orders surpasses
				 * IMPLICIT_ORDER_ONLY_CAP. */
				int target_index = this->cur_implicit_order_index;
				bool found = false;
				while (target_index != this->cur_real_order_index || this->GetNumManualOrders() == 0) {
					const Order *order = this->GetOrder(target_index);
					if (order == nullptr) break; // No orders.
					if (order->IsType(OT_IMPLICIT) && order->GetDestination() == this->last_station_visited) {
						found = true;
						break;
					}
					target_index++;
					if (target_index >= this->orders.list->GetNumOrders()) {
						if (this->GetNumManualOrders() == 0 &&
								this->GetNumOrders() < IMPLICIT_ORDER_ONLY_CAP) {
							break;
						}
						target_index = 0;
					}
					if (target_index == this->cur_implicit_order_index) break; // Avoid infinite loop.
				}

				if (found) {
					if (suppress_implicit_orders) {
						/* Skip to the found order */
						this->cur_implicit_order_index = target_index;
						InvalidateVehicleOrder(this, 0);
					} else {
						/* Delete all implicit orders up to the station we just reached */
						const Order *order = this->GetOrder(this->cur_implicit_order_index);
						while (!order->IsType(OT_IMPLICIT) || order->GetDestination() != this->last_station_visited) {
							if (order->IsType(OT_IMPLICIT)) {
								DeleteOrder(this, this->cur_implicit_order_index);
								/* DeleteOrder does various magic with order_indices, so resync 'order' with 'cur_implicit_order_index' */
								order = this->GetOrder(this->cur_implicit_order_index);
							} else {
								/* Skip non-implicit orders, e.g. service-orders */
								order = order->next;
								this->cur_implicit_order_index++;
							}

							/* Wrap around */
							if (order == nullptr) {
								order = this->GetOrder(0);
								this->cur_implicit_order_index = 0;
							}
							assert(order != nullptr);
						}
					}
				} else if (!suppress_implicit_orders &&
						((this->orders.list == nullptr ? OrderList::CanAllocateItem() : this->orders.list->GetNumOrders() < MAX_VEH_ORDER_ID)) &&
						Order::CanAllocateItem()) {
					/* Insert new implicit order */
					Order *implicit_order = new Order();
					implicit_order->MakeImplicit(this->last_station_visited);
					InsertOrder(this, implicit_order, this->cur_implicit_order_index);
					if (this->cur_implicit_order_index > 0) --this->cur_implicit_order_index;

					/* InsertOrder disabled creation of implicit orders for all vehicles with the same implicit order.
					 * Reenable it for this vehicle */
					uint16 &gv_flags = this->GetGroundVehicleFlags();
					ClrBit(gv_flags, GVF_SUPPRESS_IMPLICIT_ORDERS);
				}
			}
		}
		this->current_order.MakeLoading(false);
	}

	if (this->last_loading_station != INVALID_STATION &&
			this->last_loading_station != this->last_station_visited &&
			((this->current_order.GetLoadType() & OLFB_NO_LOAD) == 0 ||
			(this->current_order.GetUnloadType() & OUFB_NO_UNLOAD) == 0)) {
		IncreaseStats(Station::Get(this->last_loading_station), this, this->last_station_visited);
	}

	PrepareUnload(this);

	SetWindowDirty(GetWindowClassForVehicleType(this->type), this->owner);
	SetWindowWidgetDirty(WC_VEHICLE_VIEW, this->index, WID_VV_START_STOP);
	SetWindowDirty(WC_VEHICLE_DETAILS, this->index);
	SetWindowDirty(WC_STATION_VIEW, this->last_station_visited);

	Station::Get(this->last_station_visited)->MarkTilesDirty(true);
	this->cur_speed = 0;
	this->MarkDirty();
}

/**
 * Return all reserved cargo packets to the station and reset all packets
 * staged for transfer.
 * @param st the station where the reserved packets should go.
 */
void Vehicle::CancelReservation(StationID next, Station *st)
{
	for (Vehicle *v = this; v != nullptr; v = v->next) {
		VehicleCargoList &cargo = v->cargo;
		if (cargo.ActionCount(VehicleCargoList::MTA_LOAD) > 0) {
			Debug(misc, 1, "cancelling cargo reservation");
			cargo.Return(UINT_MAX, &st->goods[v->cargo_type].cargo, next);
			cargo.SetTransferLoadPlace(st->xy);
		}
		cargo.KeepAll();
	}
}

/**
 * Perform all actions when leaving a station.
 * @pre this->current_order.IsType(OT_LOADING)
 */
void Vehicle::LeaveStation()
{
	assert(this->current_order.IsType(OT_LOADING));

	delete this->cargo_payment;
	assert(this->cargo_payment == nullptr); // cleared by ~CargoPayment

	/* Only update the timetable if the vehicle was supposed to stop here. */
	if (this->current_order.GetNonStopType() != ONSF_STOP_EVERYWHERE) UpdateVehicleTimetable(this, false);

	if ((this->current_order.GetLoadType() & OLFB_NO_LOAD) == 0 ||
			(this->current_order.GetUnloadType() & OUFB_NO_UNLOAD) == 0) {
		if (this->current_order.CanLeaveWithCargo(this->last_loading_station != INVALID_STATION)) {
			/* Refresh next hop stats to make sure we've done that at least once
			 * during the stop and that refit_cap == cargo_cap for each vehicle in
			 * the consist. */
			this->ResetRefitCaps();
			LinkRefresher::Run(this);

			/* if the vehicle could load here or could stop with cargo loaded set the last loading station */
			this->last_loading_station = this->last_station_visited;
		} else {
			/* if the vehicle couldn't load and had to unload or transfer everything
			 * set the last loading station to invalid as it will leave empty. */
			this->last_loading_station = INVALID_STATION;
		}
	}

	this->current_order.MakeLeaveStation();
	Station *st = Station::Get(this->last_station_visited);
	this->CancelReservation(INVALID_STATION, st);
	st->loading_vehicles.remove(this);

	HideFillingPercent(&this->fill_percent_te_id);
	trip_occupancy = CalcPercentVehicleFilled(this, nullptr);

	if (this->type == VEH_TRAIN && !(this->vehstatus & VS_CRASHED)) {
		/* Trigger station animation (trains only) */
		if (IsTileType(this->tile, MP_STATION)) {
			TriggerStationRandomisation(st, this->tile, SRT_TRAIN_DEPARTS);
			TriggerStationAnimation(st, this->tile, SAT_TRAIN_DEPARTS);
		}

		SetBit(Train::From(this)->flags, VRF_LEAVING_STATION);
	}

	this->MarkDirty();
}

/**
 * Reset all refit_cap in the consist to cargo_cap.
 */
void Vehicle::ResetRefitCaps()
{
	for (Vehicle *v = this; v != nullptr; v = v->Next()) v->refit_cap = v->cargo_cap;
}

/**
 * Handle the loading of the vehicle; when not it skips through dummy
 * orders and does nothing in all other cases.
 * @param mode is the non-first call for this vehicle in this tick?
 */
void Vehicle::HandleLoading(bool mode)
{
	switch (this->current_order.GetType()) {
		case OT_LOADING: {
			uint wait_time = std::max(this->current_order.GetTimetabledWait() - this->lateness_counter, 0);

			/* Not the first call for this tick, or still loading */
			if (mode || !HasBit(this->vehicle_flags, VF_LOADING_FINISHED) || this->current_order_time < wait_time) return;

			this->PlayLeaveStationSound();

			this->LeaveStation();

			/* Only advance to next order if we just loaded at the current one */
			const Order *order = this->GetOrder(this->cur_implicit_order_index);
			if (order == nullptr ||
					(!order->IsType(OT_IMPLICIT) && !order->IsType(OT_GOTO_STATION)) ||
					order->GetDestination() != this->last_station_visited) {
				return;
			}
			break;
		}

		case OT_DUMMY: break;

		default: return;
	}

	this->IncrementImplicitOrderIndex();
}

/**
 * Get a map of cargoes and free capacities in the consist.
 * @param capacities Map to be filled with cargoes and capacities.
 */
void Vehicle::GetConsistFreeCapacities(SmallMap<CargoID, uint> &capacities) const
{
	for (const Vehicle *v = this; v != nullptr; v = v->Next()) {
		if (v->cargo_cap == 0) continue;
		std::pair<CargoID, uint> *pair = capacities.Find(v->cargo_type);
		if (pair == capacities.End()) {
			capacities.push_back({v->cargo_type, v->cargo_cap - v->cargo.StoredCount()});
		} else {
			pair->second += v->cargo_cap - v->cargo.StoredCount();
		}
	}
}

uint Vehicle::GetConsistTotalCapacity() const
{
	uint result = 0;
	for (const Vehicle *v = this; v != nullptr; v = v->Next()) {
		result += v->cargo_cap;
	}
	return result;
}

/**
 * Send this vehicle to the depot using the given command(s).
 * @param flags   the command flags (like execute and such).
 * @param command the command to execute.
 * @return the cost of the depot action.
 */
CommandCost Vehicle::SendToDepot(DoCommandFlag flags, DepotCommand command)
{
	CommandCost ret = CheckOwnership(this->owner);
	if (ret.Failed()) return ret;

	if (this->vehstatus & VS_CRASHED) return CMD_ERROR;
	if (this->IsStoppedInDepot()) return CMD_ERROR;

	if (this->current_order.IsType(OT_GOTO_DEPOT)) {
		bool halt_in_depot = (this->current_order.GetDepotActionType() & ODATFB_HALT) != 0;
		if (!!(command & DEPOT_SERVICE) == halt_in_depot) {
			/* We called with a different DEPOT_SERVICE setting.
			 * Now we change the setting to apply the new one and let the vehicle head for the same depot.
			 * Note: the if is (true for requesting service == true for ordered to stop in depot)          */
			if (flags & DC_EXEC) {
				this->current_order.SetDepotOrderType(ODTF_MANUAL);
				this->current_order.SetDepotActionType(halt_in_depot ? ODATF_SERVICE_ONLY : ODATFB_HALT);
				SetWindowWidgetDirty(WC_VEHICLE_VIEW, this->index, WID_VV_START_STOP);
			}
			return CommandCost();
		}

		if (command & DEPOT_DONT_CANCEL) return CMD_ERROR; // Requested no cancellation of depot orders
		if (flags & DC_EXEC) {
			/* If the orders to 'goto depot' are in the orders list (forced servicing),
			 * then skip to the next order; effectively cancelling this forced service */
			if (this->current_order.GetDepotOrderType() & ODTFB_PART_OF_ORDERS) this->IncrementRealOrderIndex();

			if (this->IsGroundVehicle()) {
				uint16 &gv_flags = this->GetGroundVehicleFlags();
				SetBit(gv_flags, GVF_SUPPRESS_IMPLICIT_ORDERS);
			}

			this->current_order.MakeDummy();
			SetWindowWidgetDirty(WC_VEHICLE_VIEW, this->index, WID_VV_START_STOP);
		}
		return CommandCost();
	}

	TileIndex location;
	DestinationID destination;
	bool reverse;
	static const StringID no_depot[] = {STR_ERROR_UNABLE_TO_FIND_ROUTE_TO, STR_ERROR_UNABLE_TO_FIND_LOCAL_DEPOT, STR_ERROR_UNABLE_TO_FIND_LOCAL_DEPOT, STR_ERROR_CAN_T_SEND_AIRCRAFT_TO_HANGAR};
	if (!this->FindClosestDepot(&location, &destination, &reverse)) return_cmd_error(no_depot[this->type]);

	if (flags & DC_EXEC) {
		if (this->current_order.IsType(OT_LOADING)) this->LeaveStation();

		if (this->IsGroundVehicle() && this->GetNumManualOrders() > 0) {
			uint16 &gv_flags = this->GetGroundVehicleFlags();
			SetBit(gv_flags, GVF_SUPPRESS_IMPLICIT_ORDERS);
		}

		this->SetDestTile(location);
		this->current_order.MakeGoToDepot(destination, ODTF_MANUAL);
		if (!(command & DEPOT_SERVICE)) this->current_order.SetDepotActionType(ODATFB_HALT);
		SetWindowWidgetDirty(WC_VEHICLE_VIEW, this->index, WID_VV_START_STOP);

		/* If there is no depot in front and the train is not already reversing, reverse automatically (trains only) */
		if (this->type == VEH_TRAIN && (reverse ^ HasBit(Train::From(this)->flags, VRF_REVERSING))) {
			DoCommand(this->tile, this->index, 0, DC_EXEC, CMD_REVERSE_TRAIN_DIRECTION);
		}

		if (this->type == VEH_AIRCRAFT) {
			Aircraft *a = Aircraft::From(this);
			if (a->state == FLYING && a->targetairport != destination) {
				/* The aircraft is now heading for a different hangar than the next in the orders */
				extern void AircraftNextAirportPos_and_Order(Aircraft *a);
				AircraftNextAirportPos_and_Order(a);
			}
		}
	}

	return CommandCost();

}

/**
 * Update the cached visual effect.
 * @param allow_power_change true if the wagon-is-powered-state may change.
 */
void Vehicle::UpdateVisualEffect(bool allow_power_change)
{
	bool powered_before = HasBit(this->vcache.cached_vis_effect, VE_DISABLE_WAGON_POWER);
	const Engine *e = this->GetEngine();

	/* Evaluate properties */
	byte visual_effect;
	switch (e->type) {
		case VEH_TRAIN: visual_effect = e->u.rail.visual_effect; break;
		case VEH_ROAD:  visual_effect = e->u.road.visual_effect; break;
		case VEH_SHIP:  visual_effect = e->u.ship.visual_effect; break;
		default:        visual_effect = 1 << VE_DISABLE_EFFECT;  break;
	}

	/* Check powered wagon / visual effect callback */
	if (HasBit(e->info.callback_mask, CBM_VEHICLE_VISUAL_EFFECT)) {
		uint16 callback = GetVehicleCallback(CBID_VEHICLE_VISUAL_EFFECT, 0, 0, this->engine_type, this);

		if (callback != CALLBACK_FAILED) {
			if (callback >= 0x100 && e->GetGRF()->grf_version >= 8) ErrorUnknownCallbackResult(e->GetGRFID(), CBID_VEHICLE_VISUAL_EFFECT, callback);

			callback = GB(callback, 0, 8);
			/* Avoid accidentally setting 'visual_effect' to the default value
			 * Since bit 6 (disable effects) is set anyways, we can safely erase some bits. */
			if (callback == VE_DEFAULT) {
				assert(HasBit(callback, VE_DISABLE_EFFECT));
				SB(callback, VE_TYPE_START, VE_TYPE_COUNT, 0);
			}
			visual_effect = callback;
		}
	}

	/* Apply default values */
	if (visual_effect == VE_DEFAULT ||
			(!HasBit(visual_effect, VE_DISABLE_EFFECT) && GB(visual_effect, VE_TYPE_START, VE_TYPE_COUNT) == VE_TYPE_DEFAULT)) {
		/* Only train engines have default effects.
		 * Note: This is independent of whether the engine is a front engine or articulated part or whatever. */
		if (e->type != VEH_TRAIN || e->u.rail.railveh_type == RAILVEH_WAGON || !IsInsideMM(e->u.rail.engclass, EC_STEAM, EC_MONORAIL)) {
			if (visual_effect == VE_DEFAULT) {
				visual_effect = 1 << VE_DISABLE_EFFECT;
			} else {
				SetBit(visual_effect, VE_DISABLE_EFFECT);
			}
		} else {
			if (visual_effect == VE_DEFAULT) {
				/* Also set the offset */
				visual_effect = (VE_OFFSET_CENTRE - (e->u.rail.engclass == EC_STEAM ? 4 : 0)) << VE_OFFSET_START;
			}
			SB(visual_effect, VE_TYPE_START, VE_TYPE_COUNT, e->u.rail.engclass - EC_STEAM + VE_TYPE_STEAM);
		}
	}

	this->vcache.cached_vis_effect = visual_effect;

	if (!allow_power_change && powered_before != HasBit(this->vcache.cached_vis_effect, VE_DISABLE_WAGON_POWER)) {
		ToggleBit(this->vcache.cached_vis_effect, VE_DISABLE_WAGON_POWER);
		ShowNewGrfVehicleError(this->engine_type, STR_NEWGRF_BROKEN, STR_NEWGRF_BROKEN_POWERED_WAGON, GBUG_VEH_POWERED_WAGON, false);
	}
}

static const int8 _vehicle_smoke_pos[8] = {
	1, 1, 1, 0, -1, -1, -1, 0
};

/**
 * Call CBID_VEHICLE_SPAWN_VISUAL_EFFECT and spawn requested effects.
 * @param v Vehicle to create effects for.
 */
static void SpawnAdvancedVisualEffect(const Vehicle *v)
{
	uint16 callback = GetVehicleCallback(CBID_VEHICLE_SPAWN_VISUAL_EFFECT, 0, Random(), v->engine_type, v);
	if (callback == CALLBACK_FAILED) return;

	uint count = GB(callback, 0, 2);
	bool auto_center = HasBit(callback, 13);
	bool auto_rotate = !HasBit(callback, 14);

	int8 l_center = 0;
	if (auto_center) {
		/* For road vehicles: Compute offset from vehicle position to vehicle center */
		if (v->type == VEH_ROAD) l_center = -(int)(VEHICLE_LENGTH - RoadVehicle::From(v)->gcache.cached_veh_length) / 2;
	} else {
		/* For trains: Compute offset from vehicle position to sprite position */
		if (v->type == VEH_TRAIN) l_center = (VEHICLE_LENGTH - Train::From(v)->gcache.cached_veh_length) / 2;
	}

	Direction l_dir = v->direction;
	if (v->type == VEH_TRAIN && HasBit(Train::From(v)->flags, VRF_REVERSE_DIRECTION)) l_dir = ReverseDir(l_dir);
	Direction t_dir = ChangeDir(l_dir, DIRDIFF_90RIGHT);

	int8 x_center = _vehicle_smoke_pos[l_dir] * l_center;
	int8 y_center = _vehicle_smoke_pos[t_dir] * l_center;

	for (uint i = 0; i < count; i++) {
		uint32 reg = GetRegister(0x100 + i);
		uint type = GB(reg,  0, 8);
		int8 x    = GB(reg,  8, 8);
		int8 y    = GB(reg, 16, 8);
		int8 z    = GB(reg, 24, 8);

		if (auto_rotate) {
			int8 l = x;
			int8 t = y;
			x = _vehicle_smoke_pos[l_dir] * l + _vehicle_smoke_pos[t_dir] * t;
			y = _vehicle_smoke_pos[t_dir] * l - _vehicle_smoke_pos[l_dir] * t;
		}

		if (type >= 0xF0) {
			switch (type) {
				case 0xF1: CreateEffectVehicleRel(v, x_center + x, y_center + y, z, EV_STEAM_SMOKE); break;
				case 0xF2: CreateEffectVehicleRel(v, x_center + x, y_center + y, z, EV_DIESEL_SMOKE); break;
				case 0xF3: CreateEffectVehicleRel(v, x_center + x, y_center + y, z, EV_ELECTRIC_SPARK); break;
				case 0xFA: CreateEffectVehicleRel(v, x_center + x, y_center + y, z, EV_BREAKDOWN_SMOKE_AIRCRAFT); break;
				default: break;
			}
		}
	}
}

/**
 * Draw visual effects (smoke and/or sparks) for a vehicle chain.
 * @pre this->IsPrimaryVehicle()
 */
void Vehicle::ShowVisualEffect() const
{
	assert(this->IsPrimaryVehicle());
	bool sound = false;

	/* Do not show any smoke when:
	 * - vehicle smoke is disabled by the player
	 * - the vehicle is slowing down or stopped (by the player)
	 * - the vehicle is moving very slowly
	 */
	if (_settings_game.vehicle.smoke_amount == 0 ||
			this->vehstatus & (VS_TRAIN_SLOWING | VS_STOPPED) ||
			this->cur_speed < 2) {
		return;
	}

	/* Use the speed as limited by underground and orders. */
	uint max_speed = this->GetCurrentMaxSpeed();

	if (this->type == VEH_TRAIN) {
		const Train *t = Train::From(this);
		/* For trains, do not show any smoke when:
		 * - the train is reversing
		 * - is entering a station with an order to stop there and its speed is equal to maximum station entering speed
		 */
		if (HasBit(t->flags, VRF_REVERSING) ||
				(IsRailStationTile(t->tile) && t->IsFrontEngine() && t->current_order.ShouldStopAtStation(t, GetStationIndex(t->tile)) &&
				t->cur_speed >= max_speed)) {
			return;
		}
	}

	const Vehicle *v = this;

	do {
		bool advanced = HasBit(v->vcache.cached_vis_effect, VE_ADVANCED_EFFECT);
		int effect_offset = GB(v->vcache.cached_vis_effect, VE_OFFSET_START, VE_OFFSET_COUNT) - VE_OFFSET_CENTRE;
		VisualEffectSpawnModel effect_model = VESM_NONE;
		if (advanced) {
			effect_offset = VE_OFFSET_CENTRE;
			effect_model = (VisualEffectSpawnModel)GB(v->vcache.cached_vis_effect, 0, VE_ADVANCED_EFFECT);
			if (effect_model >= VESM_END) effect_model = VESM_NONE; // unknown spawning model
		} else {
			effect_model = (VisualEffectSpawnModel)GB(v->vcache.cached_vis_effect, VE_TYPE_START, VE_TYPE_COUNT);
			assert(effect_model != (VisualEffectSpawnModel)VE_TYPE_DEFAULT); // should have been resolved by UpdateVisualEffect
			static_assert((uint)VESM_STEAM    == (uint)VE_TYPE_STEAM);
			static_assert((uint)VESM_DIESEL   == (uint)VE_TYPE_DIESEL);
			static_assert((uint)VESM_ELECTRIC == (uint)VE_TYPE_ELECTRIC);
		}

		/* Show no smoke when:
		 * - Smoke has been disabled for this vehicle
		 * - The vehicle is not visible
		 * - The vehicle is under a bridge
		 * - The vehicle is on a depot tile
		 * - The vehicle is on a tunnel tile
		 * - The vehicle is a train engine that is currently unpowered */
		if (effect_model == VESM_NONE ||
				v->vehstatus & VS_HIDDEN ||
				IsBridgeAbove(v->tile) ||
				IsDepotTile(v->tile) ||
				IsTunnelTile(v->tile) ||
				(v->type == VEH_TRAIN &&
				!HasPowerOnRail(Train::From(v)->railtype, GetTileRailType(v->tile)))) {
			continue;
		}

		EffectVehicleType evt = EV_END;
		switch (effect_model) {
			case VESM_STEAM:
				/* Steam smoke - amount is gradually falling until vehicle reaches its maximum speed, after that it's normal.
				 * Details: while vehicle's current speed is gradually increasing, steam plumes' density decreases by one third each
				 * third of its maximum speed spectrum. Steam emission finally normalises at very close to vehicle's maximum speed.
				 * REGULATION:
				 * - instead of 1, 4 / 2^smoke_amount (max. 2) is used to provide sufficient regulation to steam puffs' amount. */
				if (GB(v->tick_counter, 0, ((4 >> _settings_game.vehicle.smoke_amount) + ((this->cur_speed * 3) / max_speed))) == 0) {
					evt = EV_STEAM_SMOKE;
				}
				break;

			case VESM_DIESEL: {
				/* Diesel smoke - thicker when vehicle is starting, gradually subsiding till it reaches its maximum speed
				 * when smoke emission stops.
				 * Details: Vehicle's (max.) speed spectrum is divided into 32 parts. When max. speed is reached, chance for smoke
				 * emission erodes by 32 (1/4). For trains, power and weight come in handy too to either increase smoke emission in
				 * 6 steps (1000HP each) if the power is low or decrease smoke emission in 6 steps (512 tonnes each) if the train
				 * isn't overweight. Power and weight contributions are expressed in a way that neither extreme power, nor
				 * extreme weight can ruin the balance (e.g. FreightWagonMultiplier) in the formula. When the vehicle reaches
				 * maximum speed no diesel_smoke is emitted.
				 * REGULATION:
				 * - up to which speed a diesel vehicle is emitting smoke (with reduced/small setting only until 1/2 of max_speed),
				 * - in Chance16 - the last value is 512 / 2^smoke_amount (max. smoke when 128 = smoke_amount of 2). */
				int power_weight_effect = 0;
				if (v->type == VEH_TRAIN) {
					power_weight_effect = (32 >> (Train::From(this)->gcache.cached_power >> 10)) - (32 >> (Train::From(this)->gcache.cached_weight >> 9));
				}
				if (this->cur_speed < (max_speed >> (2 >> _settings_game.vehicle.smoke_amount)) &&
						Chance16((64 - ((this->cur_speed << 5) / max_speed) + power_weight_effect), (512 >> _settings_game.vehicle.smoke_amount))) {
					evt = EV_DIESEL_SMOKE;
				}
				break;
			}

			case VESM_ELECTRIC:
				/* Electric train's spark - more often occurs when train is departing (more load)
				 * Details: Electric locomotives are usually at least twice as powerful as their diesel counterparts, so spark
				 * emissions are kept simple. Only when starting, creating huge force are sparks more likely to happen, but when
				 * reaching its max. speed, quarter by quarter of it, chance decreases until the usual 2,22% at train's top speed.
				 * REGULATION:
				 * - in Chance16 the last value is 360 / 2^smoke_amount (max. sparks when 90 = smoke_amount of 2). */
				if (GB(v->tick_counter, 0, 2) == 0 &&
						Chance16((6 - ((this->cur_speed << 2) / max_speed)), (360 >> _settings_game.vehicle.smoke_amount))) {
					evt = EV_ELECTRIC_SPARK;
				}
				break;

			default:
				NOT_REACHED();
		}

		if (evt != EV_END && advanced) {
			sound = true;
			SpawnAdvancedVisualEffect(v);
		} else if (evt != EV_END) {
			sound = true;

			/* The effect offset is relative to a point 4 units behind the vehicle's
			 * front (which is the center of an 8/8 vehicle). Shorter vehicles need a
			 * correction factor. */
			if (v->type == VEH_TRAIN) effect_offset += (VEHICLE_LENGTH - Train::From(v)->gcache.cached_veh_length) / 2;

			int x = _vehicle_smoke_pos[v->direction] * effect_offset;
			int y = _vehicle_smoke_pos[(v->direction + 2) % 8] * effect_offset;

			if (v->type == VEH_TRAIN && HasBit(Train::From(v)->flags, VRF_REVERSE_DIRECTION)) {
				x = -x;
				y = -y;
			}

			CreateEffectVehicleRel(v, x, y, 10, evt);
		}
	} while ((v = v->Next()) != nullptr);

	if (sound) PlayVehicleSound(this, VSE_VISUAL_EFFECT);
}

/**
 * Set the next vehicle of this vehicle.
 * @param next the next vehicle. nullptr removes the next vehicle.
 */
void Vehicle::SetNext(Vehicle *next)
{
	assert(this != next);

	if (this->next != nullptr) {
		/* We had an old next vehicle. Update the first and previous pointers */
		for (Vehicle *v = this->next; v != nullptr; v = v->Next()) {
			v->first = this->next;
		}
		this->next->previous = nullptr;
	}

	this->next = next;

	if (this->next != nullptr) {
		/* A new next vehicle. Update the first and previous pointers */
		if (this->next->previous != nullptr) this->next->previous->next = nullptr;
		this->next->previous = this;
		for (Vehicle *v = this->next; v != nullptr; v = v->Next()) {
			v->first = this->first;
		}
	}
}

/**
 * Adds this vehicle to a shared vehicle chain.
 * @param shared_chain a vehicle of the chain with shared vehicles.
 * @pre !this->IsOrderListShared()
 */
void Vehicle::AddToShared(Vehicle *shared_chain)
{
	assert(this->previous_shared == nullptr && this->next_shared == nullptr);

	if (shared_chain->orders.list == nullptr) {
		assert(shared_chain->previous_shared == nullptr);
		assert(shared_chain->next_shared == nullptr);
		this->orders.list = shared_chain->orders.list = new OrderList(nullptr, shared_chain);
	}

	this->next_shared     = shared_chain->next_shared;
	this->previous_shared = shared_chain;

	shared_chain->next_shared = this;

	if (this->next_shared != nullptr) this->next_shared->previous_shared = this;

	shared_chain->orders.list->AddVehicle(this);
}

/**
 * Removes the vehicle from the shared order list.
 */
void Vehicle::RemoveFromShared()
{
	/* Remember if we were first and the old window number before RemoveVehicle()
	 * as this changes first if needed. */
	bool were_first = (this->FirstShared() == this);
	VehicleListIdentifier vli(VL_SHARED_ORDERS, this->type, this->owner, this->FirstShared()->index);

	this->orders.list->RemoveVehicle(this);

	if (!were_first) {
		/* We are not the first shared one, so only relink our previous one. */
		this->previous_shared->next_shared = this->NextShared();
	}

	if (this->next_shared != nullptr) this->next_shared->previous_shared = this->previous_shared;


	if (this->orders.list->GetNumVehicles() == 1) {
		/* When there is only one vehicle, remove the shared order list window. */
		CloseWindowById(GetWindowClassForVehicleType(this->type), vli.Pack());
		InvalidateVehicleOrder(this->FirstShared(), VIWD_MODIFY_ORDERS);
	} else if (were_first) {
		/* If we were the first one, update to the new first one.
		 * Note: FirstShared() is already the new first */
		InvalidateWindowData(GetWindowClassForVehicleType(this->type), vli.Pack(), this->FirstShared()->index | (1U << 31));
	}

	this->next_shared     = nullptr;
	this->previous_shared = nullptr;
}

void VehiclesYearlyLoop()
{
	for (Vehicle *v : Vehicle::Iterate()) {
		if (v->IsPrimaryVehicle()) {
			/* show warning if vehicle is not generating enough income last 2 years (corresponds to a red icon in the vehicle list) */
			Money profit = v->GetDisplayProfitThisYear();
			if (v->age >= 730 && profit < 0) {
				if (_settings_client.gui.vehicle_income_warn && v->owner == _local_company) {
					SetDParam(0, v->index);
					SetDParam(1, profit);
					AddVehicleAdviceNewsItem(STR_NEWS_VEHICLE_IS_UNPROFITABLE, v->index);
				}
				AI::NewEvent(v->owner, new ScriptEventVehicleUnprofitable(v->index));
			}

			v->profit_last_year = v->profit_this_year;
			v->profit_this_year = 0;
			SetWindowDirty(WC_VEHICLE_DETAILS, v->index);
		}
	}
	GroupStatistics::UpdateProfits();
	SetWindowClassesDirty(WC_TRAINS_LIST);
	SetWindowClassesDirty(WC_SHIPS_LIST);
	SetWindowClassesDirty(WC_ROADVEH_LIST);
	SetWindowClassesDirty(WC_AIRCRAFT_LIST);
}


/**
 * Can this station be used by the given engine type?
 * @param engine_type the type of vehicles to test
 * @param st the station to test for
 * @return true if and only if the vehicle of the type can use this station.
 * @note For road vehicles the Vehicle is needed to determine whether it can
 *       use the station. This function will return true for road vehicles
 *       when at least one of the facilities is available.
 */
bool CanVehicleUseStation(EngineID engine_type, const Station *st)
{
	const Engine *e = Engine::GetIfValid(engine_type);
	assert(e != nullptr);

	switch (e->type) {
		case VEH_TRAIN:
			return (st->facilities & FACIL_TRAIN) != 0;

		case VEH_ROAD:
			/* For road vehicles we need the vehicle to know whether it can actually
			 * use the station, but if it doesn't have facilities for RVs it is
			 * certainly not possible that the station can be used. */
			return (st->facilities & (FACIL_BUS_STOP | FACIL_TRUCK_STOP)) != 0;

		case VEH_SHIP:
			return (st->facilities & FACIL_DOCK) != 0;

		case VEH_AIRCRAFT:
			return (st->facilities & FACIL_AIRPORT) != 0 &&
					(st->airport.GetFTA()->flags & (e->u.air.subtype & AIR_CTOL ? AirportFTAClass::AIRPLANES : AirportFTAClass::HELICOPTERS)) != 0;

		default:
			return false;
	}
}

/**
 * Can this station be used by the given vehicle?
 * @param v the vehicle to test
 * @param st the station to test for
 * @return true if and only if the vehicle can use this station.
 */
bool CanVehicleUseStation(const Vehicle *v, const Station *st)
{
	if (v->type == VEH_ROAD) return st->GetPrimaryRoadStop(RoadVehicle::From(v)) != nullptr;

	return CanVehicleUseStation(v->engine_type, st);
}

/**
 * Access the ground vehicle cache of the vehicle.
 * @pre The vehicle is a #GroundVehicle.
 * @return #GroundVehicleCache of the vehicle.
 */
GroundVehicleCache *Vehicle::GetGroundVehicleCache()
{
	assert(this->IsGroundVehicle());
	if (this->type == VEH_TRAIN) {
		return &Train::From(this)->gcache;
	} else {
		return &RoadVehicle::From(this)->gcache;
	}
}

/**
 * Access the ground vehicle cache of the vehicle.
 * @pre The vehicle is a #GroundVehicle.
 * @return #GroundVehicleCache of the vehicle.
 */
const GroundVehicleCache *Vehicle::GetGroundVehicleCache() const
{
	assert(this->IsGroundVehicle());
	if (this->type == VEH_TRAIN) {
		return &Train::From(this)->gcache;
	} else {
		return &RoadVehicle::From(this)->gcache;
	}
}

/**
 * Access the ground vehicle flags of the vehicle.
 * @pre The vehicle is a #GroundVehicle.
 * @return #GroundVehicleFlags of the vehicle.
 */
uint16 &Vehicle::GetGroundVehicleFlags()
{
	assert(this->IsGroundVehicle());
	if (this->type == VEH_TRAIN) {
		return Train::From(this)->gv_flags;
	} else {
		return RoadVehicle::From(this)->gv_flags;
	}
}

/**
 * Access the ground vehicle flags of the vehicle.
 * @pre The vehicle is a #GroundVehicle.
 * @return #GroundVehicleFlags of the vehicle.
 */
const uint16 &Vehicle::GetGroundVehicleFlags() const
{
	assert(this->IsGroundVehicle());
	if (this->type == VEH_TRAIN) {
		return Train::From(this)->gv_flags;
	} else {
		return RoadVehicle::From(this)->gv_flags;
	}
}

/**
 * Calculates the set of vehicles that will be affected by a given selection.
 * @param[in,out] set Set of affected vehicles.
 * @param v First vehicle of the selection.
 * @param num_vehicles Number of vehicles in the selection (not counting articulated parts).
 * @pre \a set must be empty.
 * @post \a set will contain the vehicles that will be refitted.
 */
void GetVehicleSet(VehicleSet &set, Vehicle *v, uint8 num_vehicles)
{
	if (v->type == VEH_TRAIN) {
		Train *u = Train::From(v);
		/* Only include whole vehicles, so start with the first articulated part */
		u = u->GetFirstEnginePart();

		/* Include num_vehicles vehicles, not counting articulated parts */
		for (; u != nullptr && num_vehicles > 0; num_vehicles--) {
			do {
				/* Include current vehicle in the selection. */
				include(set, u->index);

				/* If the vehicle is multiheaded, add the other part too. */
				if (u->IsMultiheaded()) include(set, u->other_multiheaded_part->index);

				u = u->Next();
			} while (u != nullptr && u->IsArticulatedPart());
		}
	}
}
