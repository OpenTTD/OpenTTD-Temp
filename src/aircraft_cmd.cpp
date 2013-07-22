/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file aircraft_cmd.cpp
 * This file deals with aircraft and airport movements functionalities
 */

#include "stdafx.h"
#include "aircraft.h"
#include "landscape.h"
#include "news_func.h"
#include "newgrf_engine.h"
#include "newgrf_sound.h"
#include "spritecache.h"
#include "strings_func.h"
#include "command_func.h"
#include "window_func.h"
#include "date_func.h"
#include "vehicle_func.h"
#include "sound_func.h"
#include "cheat_type.h"
#include "company_base.h"
#include "ai/ai.hpp"
#include "game/game.hpp"
#include "company_func.h"
#include "effectvehicle_func.h"
#include "station_base.h"
#include "engine_base.h"
#include "core/random_func.hpp"
#include "core/backup_type.hpp"
#include "zoom_func.h"
#include "disaster_vehicle.h"
#include "newgrf_airporttiles.h"
#include "framerate_type.h"
#include "depot_base.h"

#include "table/strings.h"

#include "safeguards.h"

void Aircraft::UpdateDeltaXY()
{
	this->x_offs = -1;
	this->y_offs = -1;
	this->x_extent = 2;
	this->y_extent = 2;

	switch (this->subtype) {
		default: NOT_REACHED();

		case AIR_AIRCRAFT:
		case AIR_HELICOPTER:
			switch (this->state) {
				default: break;
				case ENDTAKEOFF:
				case LANDING:
				case HELILANDING:
				case FLYING:
					this->x_extent = 24;
					this->y_extent = 24;
					break;
			}
			this->z_extent = 5;
			break;

		case AIR_SHADOW:
			this->z_extent = 1;
			this->x_offs = 0;
			this->y_offs = 0;
			break;

		case AIR_ROTOR:
			this->z_extent = 1;
			break;
	}
}

static bool AirportMove(Aircraft *v, const AirportFTAClass *apc);
static bool AirportSetBlocks(Aircraft *v, const AirportFTA *current_pos, const AirportFTAClass *apc);
static bool AirportHasBlock(Aircraft *v, const AirportFTA *current_pos, const AirportFTAClass *apc);
static bool AirportFindFreeTerminal(Aircraft *v, const AirportFTAClass *apc);
static bool AirportFindFreeHelipad(Aircraft *v, const AirportFTAClass *apc);
static void CrashAirplane(Aircraft *v);

static const SpriteID _aircraft_sprite[] = {
	0x0EB5, 0x0EBD, 0x0EC5, 0x0ECD,
	0x0ED5, 0x0EDD, 0x0E9D, 0x0EA5,
	0x0EAD, 0x0EE5, 0x0F05, 0x0F0D,
	0x0F15, 0x0F1D, 0x0F25, 0x0F2D,
	0x0EED, 0x0EF5, 0x0EFD, 0x0F35,
	0x0E9D, 0x0EA5, 0x0EAD, 0x0EB5,
	0x0EBD, 0x0EC5
};

template <>
bool IsValidImageIndex<VEH_AIRCRAFT>(uint8 image_index)
{
	return image_index < lengthof(_aircraft_sprite);
}

/** Helicopter rotor animation states */
enum HelicopterRotorStates {
	HRS_ROTOR_STOPPED,
	HRS_ROTOR_MOVING_1,
	HRS_ROTOR_MOVING_2,
	HRS_ROTOR_MOVING_3,
};

/**
 * Find the nearest hangar to v
 * INVALID_STATION is returned, if the company does not have any suitable
 * airports (like helipads only)
 * @param v vehicle looking for a hangar
 * @return the StationID if one is found, otherwise, INVALID_STATION
 */
static StationID FindNearestHangar(const Aircraft *v)
{
	uint best = 0;
	StationID index = INVALID_STATION;
	TileIndex vtile = TileVirtXY(v->x_pos, v->y_pos);
	const AircraftVehicleInfo *avi = AircraftVehInfo(v->engine_type);
	uint max_range = v->acache.cached_max_range_sqr;

	/* Determine destinations where it's coming from and where it's heading to */
	const Station *last_dest = nullptr;
	const Station *next_dest = nullptr;
	if (max_range != 0) {
		if (v->current_order.IsType(OT_GOTO_STATION) ||
				(v->current_order.IsType(OT_GOTO_DEPOT) && v->current_order.GetDepotActionType() != ODATFB_NEAREST_DEPOT)) {
			last_dest = Station::GetIfValid(v->last_station_visited);
			next_dest = Station::GetIfValid(GetDestination(v, &(v->current_order)));
		} else {
			last_dest = GetTargetAirportIfValid(v);
			next_dest = Station::GetIfValid(v->GetNextStoppingStation().value);
		}
	}

	for (const Station *st : Station::Iterate()) {
		if (st->owner != v->owner || !(st->facilities & FACIL_AIRPORT) || !st->airport.HasHangar()) continue;

		const AirportFTAClass *afc = st->airport.GetFTA();

		/* don't crash the plane if we know it can't land at the airport */
		if ((afc->flags & AirportFTAClass::SHORT_STRIP) && (avi->subtype & AIR_FAST) && !_cheats.no_jetcrash.value) continue;

		/* the plane won't land at any helicopter station */
		if (!(afc->flags & AirportFTAClass::AIRPLANES) && (avi->subtype & AIR_CTOL)) continue;

		/* Check if our last and next destinations can be reached from the depot airport. */
		if (max_range != 0) {
			uint last_dist = (last_dest != nullptr && last_dest->airport.tile != INVALID_TILE) ? DistanceSquare(st->airport.tile, last_dest->airport.tile) : 0;
			uint next_dist = (next_dest != nullptr && next_dest->airport.tile != INVALID_TILE) ? DistanceSquare(st->airport.tile, next_dest->airport.tile) : 0;
			if (last_dist > max_range || next_dist > max_range) continue;
		}

		/* v->tile can't be used here, when aircraft is flying v->tile is set to 0 */
		uint distance = DistanceSquare(vtile, st->airport.tile);
		if (distance < best || index == INVALID_STATION) {
			best = distance;
			index = st->index;
		}
	}
	return index;
}

void Aircraft::GetImage(Direction direction, EngineImageType image_type, VehicleSpriteSeq *result) const
{
	uint8 spritenum = this->spritenum;

	if (is_custom_sprite(spritenum)) {
		GetCustomVehicleSprite(this, direction, image_type, result);
		if (result->IsValid()) return;

		spritenum = this->GetEngine()->original_image_index;
	}

	assert(IsValidImageIndex<VEH_AIRCRAFT>(spritenum));
	result->Set(direction + _aircraft_sprite[spritenum]);
}

void GetRotorImage(const Aircraft *v, EngineImageType image_type, VehicleSpriteSeq *result)
{
	assert(v->subtype == AIR_HELICOPTER);

	const Aircraft *w = v->Next()->Next();
	if (is_custom_sprite(v->spritenum)) {
		GetCustomRotorSprite(v, false, image_type, result);
		if (result->IsValid()) return;
	}

	/* Return standard rotor sprites if there are no custom sprites for this helicopter */
	result->Set(SPR_ROTOR_STOPPED + w->state);
}

static void GetAircraftIcon(EngineID engine, EngineImageType image_type, VehicleSpriteSeq *result)
{
	const Engine *e = Engine::Get(engine);
	uint8 spritenum = e->u.air.image_index;

	if (is_custom_sprite(spritenum)) {
		GetCustomVehicleIcon(engine, DIR_W, image_type, result);
		if (result->IsValid()) return;

		spritenum = e->original_image_index;
	}

	assert(IsValidImageIndex<VEH_AIRCRAFT>(spritenum));
	result->Set(DIR_W + _aircraft_sprite[spritenum]);
}

void DrawAircraftEngine(int left, int right, int preferred_x, int y, EngineID engine, PaletteID pal, EngineImageType image_type)
{
	VehicleSpriteSeq seq;
	GetAircraftIcon(engine, image_type, &seq);

	Rect rect;
	seq.GetBounds(&rect);
	preferred_x = Clamp(preferred_x,
			left - UnScaleGUI(rect.left),
			right - UnScaleGUI(rect.right));

	seq.Draw(preferred_x, y, pal, pal == PALETTE_CRASH);

	if (!(AircraftVehInfo(engine)->subtype & AIR_CTOL)) {
		VehicleSpriteSeq rotor_seq;
		GetCustomRotorIcon(engine, image_type, &rotor_seq);
		if (!rotor_seq.IsValid()) rotor_seq.Set(SPR_ROTOR_STOPPED);
		rotor_seq.Draw(preferred_x, y - ScaleGUITrad(5), PAL_NONE, false);
	}
}

/**
 * Get the size of the sprite of an aircraft sprite heading west (used for lists).
 * @param engine The engine to get the sprite from.
 * @param[out] width The width of the sprite.
 * @param[out] height The height of the sprite.
 * @param[out] xoffs Number of pixels to shift the sprite to the right.
 * @param[out] yoffs Number of pixels to shift the sprite downwards.
 * @param image_type Context the sprite is used in.
 */
void GetAircraftSpriteSize(EngineID engine, uint &width, uint &height, int &xoffs, int &yoffs, EngineImageType image_type)
{
	VehicleSpriteSeq seq;
	GetAircraftIcon(engine, image_type, &seq);

	Rect rect;
	seq.GetBounds(&rect);

	width  = UnScaleGUI(rect.right - rect.left + 1);
	height = UnScaleGUI(rect.bottom - rect.top + 1);
	xoffs  = UnScaleGUI(rect.left);
	yoffs  = UnScaleGUI(rect.top);
}

/**
 * Build an aircraft.
 * @param tile     tile of the depot where aircraft is built.
 * @param flags    type of operation.
 * @param e        the engine to build.
 * @param data     unused.
 * @param[out] ret the vehicle that has been built.
 * @return the cost of this operation or an error.
 */
CommandCost CmdBuildAircraft(TileIndex tile, DoCommandFlag flags, const Engine *e, uint16 data, Vehicle **ret)
{
	const AircraftVehicleInfo *avi = &e->u.air;
	const Station *st = Station::GetByTile(tile);

	/* Prevent building aircraft types at places which can't handle them */
	if (!CanVehicleUseStation(e->index, st)) return CMD_ERROR;

	/* Make sure all aircraft end up in the first tile of the hangar. */
	tile = st->airport.GetHangarTile(st->airport.GetHangarNum(tile));

	if (flags & DC_EXEC) {
		Aircraft *v = new Aircraft(); // aircraft
		Aircraft *u = new Aircraft(); // shadow
		*ret = v;

		v->direction = DIR_SE;

		v->owner = u->owner = _current_company;

		v->tile = tile;

		uint x = TileX(tile) * TILE_SIZE + 5;
		uint y = TileY(tile) * TILE_SIZE + 3;

		v->x_pos = u->x_pos = x;
		v->y_pos = u->y_pos = y;

		u->z_pos = GetSlopePixelZ(x, y);
		v->z_pos = u->z_pos + 1;

		v->vehstatus = VS_HIDDEN | VS_STOPPED | VS_DEFPAL;
		u->vehstatus = VS_HIDDEN | VS_UNCLICKABLE | VS_SHADOW;

		v->spritenum = avi->image_index;

		v->cargo_cap = avi->passenger_capacity;
		v->refit_cap = 0;
		u->cargo_cap = avi->mail_capacity;
		u->refit_cap = 0;

		v->cargo_type = e->GetDefaultCargoType();
		u->cargo_type = CT_MAIL;

		v->name.clear();
		v->last_station_visited = INVALID_STATION;
		v->last_loading_station = INVALID_STATION;

		v->acceleration = avi->acceleration;
		v->engine_type = e->index;
		u->engine_type = e->index;

		v->subtype = (avi->subtype & AIR_CTOL ? AIR_AIRCRAFT : AIR_HELICOPTER);
		v->UpdateDeltaXY();

		u->subtype = AIR_SHADOW;
		u->UpdateDeltaXY();

		v->reliability = e->reliability;
		v->reliability_spd_dec = e->reliability_spd_dec;
		v->max_age = e->GetLifeLengthInDays();

		_new_vehicle_id = v->index;

		v->pos = GetVehiclePosOnBuild(tile);

		v->state = HANGAR;
		v->previous_pos = v->pos;
		v->targetairport = GetStationIndex(tile);
		v->SetNext(u);

		v->SetServiceInterval(Company::Get(_current_company)->settings.vehicle.servint_aircraft);

		v->date_of_last_service = _date;
		v->build_year = u->build_year = _cur_year;

		v->sprite_cache.sprite_seq.Set(SPR_IMG_QUERY);
		u->sprite_cache.sprite_seq.Set(SPR_IMG_QUERY);

		v->random_bits = VehicleRandomBits();
		u->random_bits = VehicleRandomBits();

		v->vehicle_flags = 0;
		if (e->flags & ENGINE_EXCLUSIVE_PREVIEW) SetBit(v->vehicle_flags, VF_BUILT_AS_PROTOTYPE);
		v->SetServiceIntervalIsPercent(Company::Get(_current_company)->settings.vehicle.servint_ispercent);

		v->InvalidateNewGRFCacheOfChain();

		v->cargo_cap = e->DetermineCapacity(v, &u->cargo_cap);

		v->InvalidateNewGRFCacheOfChain();

		UpdateAircraftCache(v, true);

		v->UpdatePosition();
		u->UpdatePosition();

		/* Aircraft with 3 vehicles (chopper)? */
		if (v->subtype == AIR_HELICOPTER) {
			Aircraft *w = new Aircraft();
			w->engine_type = e->index;
			w->direction = DIR_N;
			w->owner = _current_company;
			w->x_pos = v->x_pos;
			w->y_pos = v->y_pos;
			w->z_pos = v->z_pos + ROTOR_Z_OFFSET;
			w->vehstatus = VS_HIDDEN | VS_UNCLICKABLE;
			w->spritenum = 0xFF;
			w->subtype = AIR_ROTOR;
			w->sprite_cache.sprite_seq.Set(SPR_ROTOR_STOPPED);
			w->random_bits = VehicleRandomBits();
			/* Use rotor's air.state to store the rotor animation frame */
			w->state = HRS_ROTOR_STOPPED;
			w->UpdateDeltaXY();

			u->SetNext(w);
			w->UpdatePosition();
		}
	}

	return CommandCost();
}


bool Aircraft::FindClosestDepot(TileIndex *location, DestinationID *destination, bool *reverse)
{
	const Station *st = GetTargetAirportIfValid(this);
	/* If the station is not a valid airport or if it has no hangars */
	if (st == nullptr || !CanVehicleUseStation(this, st) || !st->airport.HasHangar()) {
		/* the aircraft has to search for a hangar on its own */
		StationID station = FindNearestHangar(this);

		if (station == INVALID_STATION) return false;

		st = Station::Get(station);
		assert(st->airport.depot_id != INVALID_DEPOT);
	}

	if (location    != nullptr) *location    = st->xy;
	if (destination != nullptr) *destination = st->airport.depot_id;

	return true;
}

static void CheckIfAircraftNeedsService(Aircraft *v)
{
	if (Company::Get(v->owner)->settings.vehicle.servint_aircraft == 0 || !v->NeedsAutomaticServicing()) return;
	if (v->IsChainInDepot()) {
		VehicleServiceInDepot(v);
		return;
	}

	/* When we're parsing conditional orders and the like
	 * we don't want to consider going to a depot too. */
	if (!v->current_order.IsType(OT_GOTO_DEPOT) && !v->current_order.IsType(OT_GOTO_STATION)) return;

	const Station *st = Station::Get(GetDestination(v, &(v->current_order)));

	assert(st != nullptr);

	/* only goto depot if the target airport has a depot */
	if (st->airport.HasHangar() && CanVehicleUseStation(v, st)) {
		v->current_order.MakeGoToDepot(st->airport.depot_id, ODTFB_SERVICE);
		SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
	} else if (v->current_order.IsType(OT_GOTO_DEPOT)) {
		v->current_order.MakeDummy();
		SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
	}
}

Money Aircraft::GetRunningCost() const
{
	const Engine *e = this->GetEngine();
	uint cost_factor = GetVehicleProperty(this, PROP_AIRCRAFT_RUNNING_COST_FACTOR, e->u.air.running_cost);
	return GetPrice(PR_RUNNING_AIRCRAFT, cost_factor, e->GetGRF());
}

void Aircraft::OnNewDay()
{
	if (!this->IsNormalAircraft()) return;

	if ((++this->day_counter & 7) == 0) DecreaseVehicleValue(this);

	CheckOrders(this);

	CheckVehicleBreakdown(this);
	AgeVehicle(this);
	CheckIfAircraftNeedsService(this);

	if (this->running_ticks == 0) return;

	CommandCost cost(EXPENSES_AIRCRAFT_RUN, this->GetRunningCost() * this->running_ticks / (DAYS_IN_YEAR * DAY_TICKS));

	this->profit_this_year -= cost.GetCost();
	this->running_ticks = 0;

	SubtractMoneyFromCompanyFract(this->owner, cost);

	SetWindowDirty(WC_VEHICLE_DETAILS, this->index);
	SetWindowClassesDirty(WC_AIRCRAFT_LIST);
}

static void HelicopterTickHandler(Aircraft *v)
{
	Aircraft *u = v->Next()->Next();

	if (u->vehstatus & VS_HIDDEN) return;

	/* if true, helicopter rotors do not rotate. This should only be the case if a helicopter is
	 * loading/unloading at a terminal or stopped */
	if (v->current_order.IsType(OT_LOADING) || (v->vehstatus & VS_STOPPED)) {
		if (u->cur_speed != 0) {
			u->cur_speed++;
			if (u->cur_speed >= 0x80 && u->state == HRS_ROTOR_MOVING_3) {
				u->cur_speed = 0;
			}
		}
	} else {
		if (u->cur_speed == 0) {
			u->cur_speed = 0x70;
		}
		if (u->cur_speed >= 0x50) {
			u->cur_speed--;
		}
	}

	int tick = ++u->tick_counter;
	int spd = u->cur_speed >> 4;

	VehicleSpriteSeq seq;
	if (spd == 0) {
		u->state = HRS_ROTOR_STOPPED;
		GetRotorImage(v, EIT_ON_MAP, &seq);
		if (u->sprite_cache.sprite_seq == seq) return;
	} else if (tick >= spd) {
		u->tick_counter = 0;
		u->state++;
		if (u->state > HRS_ROTOR_MOVING_3) u->state = HRS_ROTOR_MOVING_1;
		GetRotorImage(v, EIT_ON_MAP, &seq);
	} else {
		return;
	}

	u->sprite_cache.sprite_seq = seq;

	u->UpdatePositionAndViewport();
}

/**
 * Set aircraft position.
 * @param v Aircraft to position.
 * @param x New X position.
 * @param y New y position.
 * @param z New z position.
 */
void SetAircraftPosition(Aircraft *v, int x, int y, int z)
{
	v->x_pos = x;
	v->y_pos = y;
	v->z_pos = z;

	v->UpdatePosition();
	v->UpdateViewport(true, false);
	if (v->subtype == AIR_HELICOPTER) {
		GetRotorImage(v, EIT_ON_MAP, &v->Next()->Next()->sprite_cache.sprite_seq);
	}

	Aircraft *u = v->Next();

	int safe_x = Clamp(x, 0, MapMaxX() * TILE_SIZE);
	int safe_y = Clamp(y - 1, 0, MapMaxY() * TILE_SIZE);
	u->x_pos = x;
	u->y_pos = y - ((v->z_pos - GetSlopePixelZ(safe_x, safe_y)) >> 3);

	safe_y = Clamp(u->y_pos, 0, MapMaxY() * TILE_SIZE);
	u->z_pos = GetSlopePixelZ(safe_x, safe_y);
	u->sprite_cache.sprite_seq.CopyWithoutPalette(v->sprite_cache.sprite_seq); // the shadow is never coloured

	u->UpdatePositionAndViewport();

	u = u->Next();
	if (u != nullptr) {
		u->x_pos = x;
		u->y_pos = y;
		u->z_pos = z + ROTOR_Z_OFFSET;

		u->UpdatePositionAndViewport();
	}
}

/**
 * Handle Aircraft specific tasks when an Aircraft enters a hangar
 * @param *v Vehicle that enters the hangar
 */
void HandleAircraftEnterHangar(Aircraft *v)
{
	v->subspeed = 0;
	v->progress = 0;

	Aircraft *u = v->Next();
	u->vehstatus |= VS_HIDDEN;
	u = u->Next();
	if (u != nullptr) {
		u->vehstatus |= VS_HIDDEN;
		u->cur_speed = 0;
	}

	SetAircraftPosition(v, v->x_pos, v->y_pos, v->z_pos);
}

static void PlayAircraftSound(const Vehicle *v)
{
	if (!PlayVehicleSound(v, VSE_START)) {
		SndPlayVehicleFx(AircraftVehInfo(v->engine_type)->sfx, v);
	}
}


/**
 * Update cached values of an aircraft.
 * Currently caches callback 36 max speed.
 * @param v Vehicle
 * @param update_range Update the aircraft range.
 */
void UpdateAircraftCache(Aircraft *v, bool update_range)
{
	uint max_speed = GetVehicleProperty(v, PROP_AIRCRAFT_SPEED, 0);
	if (max_speed != 0) {
		/* Convert from original units to km-ish/h */
		max_speed = (max_speed * 128) / 10;

		v->vcache.cached_max_speed = max_speed;
	} else {
		/* Use the default max speed of the vehicle. */
		v->vcache.cached_max_speed = AircraftVehInfo(v->engine_type)->max_speed;
	}

	/* Update cargo aging period. */
	v->vcache.cached_cargo_age_period = GetVehicleProperty(v, PROP_AIRCRAFT_CARGO_AGE_PERIOD, EngInfo(v->engine_type)->cargo_age_period);
	Aircraft *u = v->Next(); // Shadow for mail
	u->vcache.cached_cargo_age_period = GetVehicleProperty(u, PROP_AIRCRAFT_CARGO_AGE_PERIOD, EngInfo(u->engine_type)->cargo_age_period);

	/* Update aircraft range. */
	if (update_range) {
		v->acache.cached_max_range = GetVehicleProperty(v, PROP_AIRCRAFT_RANGE, AircraftVehInfo(v->engine_type)->max_range);
		/* Squared it now so we don't have to do it later all the time. */
		v->acache.cached_max_range_sqr = v->acache.cached_max_range * v->acache.cached_max_range;
	}
}


/**
 * Special velocities for aircraft
 */
enum AircraftSpeedLimits {
	SPEED_LIMIT_TAXI     =     50,  ///< Maximum speed of an aircraft while taxiing
	SPEED_LIMIT_APPROACH =    230,  ///< Maximum speed of an aircraft on finals
	SPEED_LIMIT_BROKEN   =    320,  ///< Maximum speed of an aircraft that is broken
	SPEED_LIMIT_HOLD     =    425,  ///< Maximum speed of an aircraft that flies the holding pattern
	SPEED_LIMIT_NONE     = 0xFFFF,  ///< No environmental speed limit. Speed limit is type dependent
};

/**
 * Sets the new speed for an aircraft
 * @param v The vehicle for which the speed should be obtained
 * @param speed_limit The maximum speed the vehicle may have.
 * @param hard_limit If true, the limit is directly enforced, otherwise the plane is slowed down gradually
 * @return The number of position updates needed within the tick
 */
static int UpdateAircraftSpeed(Aircraft *v, uint speed_limit = SPEED_LIMIT_NONE, bool hard_limit = true)
{
	/**
	 * 'acceleration' has the unit 3/8 mph/tick. This function is called twice per tick.
	 * So the speed amount we need to accelerate is:
	 *     acceleration * 3 / 16 mph = acceleration * 3 / 16 * 16 / 10 km-ish/h
	 *                               = acceleration * 3 / 10 * 256 * (km-ish/h / 256)
	 *                               ~ acceleration * 77 (km-ish/h / 256)
	 */
	uint spd = v->acceleration * 77;
	byte t;

	/* Adjust speed limits by plane speed factor to prevent taxiing
	 * and take-off speeds being too low. */
	speed_limit *= _settings_game.vehicle.plane_speed;

	/* adjust speed for broken vehicles */
	if (v->vehstatus & VS_AIRCRAFT_BROKEN) {
		if (SPEED_LIMIT_BROKEN < speed_limit) hard_limit = false;
		speed_limit = std::min<uint>(speed_limit, SPEED_LIMIT_BROKEN);
	}

	if (v->vcache.cached_max_speed < speed_limit) {
		if (v->cur_speed < speed_limit) hard_limit = false;
		speed_limit = v->vcache.cached_max_speed;
	}

	v->subspeed = (t = v->subspeed) + (byte)spd;

	/* Aircraft's current speed is used twice so that very fast planes are
	 * forced to slow down rapidly in the short distance needed. The magic
	 * value 16384 was determined to give similar results to the old speed/48
	 * method at slower speeds. This also results in less reduction at slow
	 * speeds to that aircraft do not get to taxi speed straight after
	 * touchdown. */
	if (!hard_limit && v->cur_speed > speed_limit) {
		speed_limit = v->cur_speed - std::max(1, ((v->cur_speed * v->cur_speed) / 16384) / _settings_game.vehicle.plane_speed);
	}

	spd = std::min(v->cur_speed + (spd >> 8) + (v->subspeed < t), speed_limit);

	/* updates statusbar only if speed have changed to save CPU time */
	if (spd != v->cur_speed) {
		v->cur_speed = spd;
		SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
	}

	/* Adjust distance moved by plane speed setting */
	if (_settings_game.vehicle.plane_speed > 1) spd /= _settings_game.vehicle.plane_speed;

	/* Convert direction-independent speed into direction-dependent speed. (old movement method) */
	spd = v->GetOldAdvanceSpeed(spd);

	spd += v->progress;
	v->progress = (byte)spd;
	return spd >> 8;
}

/**
 * Get the tile height below the aircraft.
 * This function is needed because aircraft can leave the mapborders.
 *
 * @param v The vehicle to get the height for.
 * @return The height in pixels from 'z_pos' 0.
 */
int GetTileHeightBelowAircraft(const Vehicle *v)
{
	int safe_x = Clamp(v->x_pos, 0, MapMaxX() * TILE_SIZE);
	int safe_y = Clamp(v->y_pos, 0, MapMaxY() * TILE_SIZE);
	return TileHeight(TileVirtXY(safe_x, safe_y)) * TILE_HEIGHT;
}

/**
 * Get the 'flight level' bounds, in pixels from 'z_pos' 0 for a particular
 * vehicle for normal flight situation.
 * When the maximum is reached the vehicle should consider descending.
 * When the minimum is reached the vehicle should consider ascending.
 *
 * @param v              The vehicle to get the flight levels for.
 * @param[out] min_level The minimum bounds for flight level.
 * @param[out] max_level The maximum bounds for flight level.
 */
void GetAircraftFlightLevelBounds(const Vehicle *v, int *min_level, int *max_level)
{
	int base_altitude = GetTileHeightBelowAircraft(v);
	if (v->type == VEH_AIRCRAFT && Aircraft::From(v)->subtype == AIR_HELICOPTER) {
		base_altitude += HELICOPTER_HOLD_MAX_FLYING_ALTITUDE - PLANE_HOLD_MAX_FLYING_ALTITUDE;
	}

	/* Make sure eastbound and westbound planes do not "crash" into each
	 * other by providing them with vertical separation
	 */
	switch (v->direction) {
		case DIR_N:
		case DIR_NE:
		case DIR_E:
		case DIR_SE:
			base_altitude += 10;
			break;

		default: break;
	}

	/* Make faster planes fly higher so that they can overtake slower ones */
	base_altitude += std::min(20 * (v->vcache.cached_max_speed / 200) - 90, 0);

	if (min_level != nullptr) *min_level = base_altitude + AIRCRAFT_MIN_FLYING_ALTITUDE;
	if (max_level != nullptr) *max_level = base_altitude + AIRCRAFT_MAX_FLYING_ALTITUDE;
}

/**
 * Gets the maximum 'flight level' for the holding pattern of the aircraft,
 * in pixels 'z_pos' 0, depending on terrain below.
 *
 * @param v The aircraft that may or may not need to decrease its altitude.
 * @return Maximal aircraft holding altitude, while in normal flight, in pixels.
 */
int GetAircraftHoldMaxAltitude(const Aircraft *v)
{
	int tile_height = GetTileHeightBelowAircraft(v);

	return tile_height + ((v->subtype == AIR_HELICOPTER) ? HELICOPTER_HOLD_MAX_FLYING_ALTITUDE : PLANE_HOLD_MAX_FLYING_ALTITUDE);
}

template <class T>
int GetAircraftFlightLevel(T *v, bool takeoff)
{
	/* Aircraft is in flight. We want to enforce it being somewhere
	 * between the minimum and the maximum allowed altitude. */
	int aircraft_min_altitude;
	int aircraft_max_altitude;
	GetAircraftFlightLevelBounds(v, &aircraft_min_altitude, &aircraft_max_altitude);
	int aircraft_middle_altitude = (aircraft_min_altitude + aircraft_max_altitude) / 2;

	/* If those assumptions would be violated, aircraft would behave fairly strange. */
	assert(aircraft_min_altitude < aircraft_middle_altitude);
	assert(aircraft_middle_altitude < aircraft_max_altitude);

	int z = v->z_pos;
	if (z < aircraft_min_altitude ||
			(HasBit(v->flags, VAF_IN_MIN_HEIGHT_CORRECTION) && z < aircraft_middle_altitude)) {
		/* Ascend. And don't fly into that mountain right ahead.
		 * And avoid our aircraft become a stairclimber, so if we start
		 * correcting altitude, then we stop correction not too early. */
		SetBit(v->flags, VAF_IN_MIN_HEIGHT_CORRECTION);
		z += takeoff ? 2 : 1;
	} else if (!takeoff && (z > aircraft_max_altitude ||
			(HasBit(v->flags, VAF_IN_MAX_HEIGHT_CORRECTION) && z > aircraft_middle_altitude))) {
		/* Descend lower. You are an aircraft, not an space ship.
		 * And again, don't stop correcting altitude too early. */
		SetBit(v->flags, VAF_IN_MAX_HEIGHT_CORRECTION);
		z--;
	} else if (HasBit(v->flags, VAF_IN_MIN_HEIGHT_CORRECTION) && z >= aircraft_middle_altitude) {
		/* Now, we have corrected altitude enough. */
		ClrBit(v->flags, VAF_IN_MIN_HEIGHT_CORRECTION);
	} else if (HasBit(v->flags, VAF_IN_MAX_HEIGHT_CORRECTION) && z <= aircraft_middle_altitude) {
		/* Now, we have corrected altitude enough. */
		ClrBit(v->flags, VAF_IN_MAX_HEIGHT_CORRECTION);
	}

	return z;
}

template int GetAircraftFlightLevel(DisasterVehicle *v, bool takeoff);
template int GetAircraftFlightLevel(Aircraft *v, bool takeoff);

/**
 * Find the entry point to an airport depending on direction which
 * the airport is being approached from. Each airport can have up to
 * four entry points for its approach system so that approaching
 * aircraft do not fly through each other or are forced to do 180
 * degree turns during the approach. The arrivals are grouped into
 * four sectors dependent on the DiagDirection from which the airport
 * is approached.
 *
 * @param v   The vehicle that is approaching the airport
 * @param apc The Airport Class being approached.
 * @param rotation The rotation of the airport.
 * @return   The index of the entry point
 */
static byte AircraftGetEntryPoint(const Aircraft *v, const AirportFTAClass *apc, Direction rotation)
{
	assert(v != nullptr);
	assert(apc != nullptr);

	/* In the case the station doesn't exit anymore, set target tile 0.
	 * It doesn't hurt much, aircraft will go to next order, nearest hangar
	 * or it will simply crash in next tick */
	TileIndex tile = 0;

	const Station *st = Station::GetIfValid(v->targetairport);
	if (st != nullptr) {
		/* Make sure we don't go to INVALID_TILE if the airport has been removed. */
		tile = (st->airport.tile != INVALID_TILE) ? st->airport.tile : st->xy;
	}

	int delta_x = v->x_pos - TileX(tile) * TILE_SIZE;
	int delta_y = v->y_pos - TileY(tile) * TILE_SIZE;

	DiagDirection dir;
	if (abs(delta_y) < abs(delta_x)) {
		/* We are northeast or southwest of the airport */
		dir = delta_x < 0 ? DIAGDIR_NE : DIAGDIR_SW;
	} else {
		/* We are northwest or southeast of the airport */
		dir = delta_y < 0 ? DIAGDIR_NW : DIAGDIR_SE;
	}
	dir = ChangeDiagDir(dir, DiagDirDifference(DIAGDIR_NE, DirToDiagDir(rotation)));
	return apc->entry_points[dir];
}


static void MaybeCrashAirplane(Aircraft *v);

/**
 * Controls the movement of an aircraft. This function actually moves the vehicle
 * on the map and takes care of minor things like sound playback.
 * @todo    De-mystify the cur_speed values for helicopter rotors.
 * @param v The vehicle that is moved. Must be the first vehicle of the chain
 * @return  Whether the position requested by the State Machine has been reached
 */
static bool AircraftController(Aircraft *v)
{
	/* nullptr if station is invalid */
	const Station *st = Station::GetIfValid(v->targetairport);
	/* INVALID_TILE if there is no station */
	TileIndex tile = INVALID_TILE;
	Direction rotation = DIR_N;
	uint size_x = 1, size_y = 1;
	if (st != nullptr) {
		if (st->airport.tile != INVALID_TILE) {
			tile = st->airport.tile;
			rotation = st->airport.rotation;
			size_x = st->airport.w;
			size_y = st->airport.h;
		} else {
			tile = st->xy;
		}
	}
	/* DUMMY if there is no station or no airport */
	const AirportFTAClass *afc = tile == INVALID_TILE ? GetAirport(AT_DUMMY) : st->airport.GetFTA();

	/* prevent going to INVALID_TILE if airport is deleted. */
	if (st == nullptr || st->airport.tile == INVALID_TILE) {
		/* Jump into our "holding pattern" state machine if possible */
		if (v->pos >= afc->nofelements) {
			v->pos = v->previous_pos = AircraftGetEntryPoint(v, afc, DIR_N);
		} else if (v->targetairport != GetDestination(v, &(v->current_order))) {
			/* If not possible, just get out of here fast */
			v->state = FLYING;
			UpdateAircraftCache(v);
			AircraftNextAirportPos_and_Order(v);
			/* get aircraft back on running altitude */
			SetAircraftPosition(v, v->x_pos, v->y_pos, GetAircraftFlightLevel(v));
			return false;
		}
	}

	/*  get airport moving data */
	const AirportMovingData amd = RotateAirportMovingData(afc->MovingData(v->pos), rotation, size_x, size_y);

	int x = TileX(tile) * TILE_SIZE;
	int y = TileY(tile) * TILE_SIZE;

	/* Helicopter raise */
	if (amd.flag & AMED_HELI_RAISE) {
		Aircraft *u = v->Next()->Next();

		/* Make sure the rotors don't rotate too fast */
		if (u->cur_speed > 32) {
			v->cur_speed = 0;
			if (--u->cur_speed == 32) {
				if (!PlayVehicleSound(v, VSE_START)) {
					SoundID sfx = AircraftVehInfo(v->engine_type)->sfx;
					/* For compatibility with old NewGRF we ignore the sfx property, unless a NewGRF-defined sound is used.
					 * The baseset has only one helicopter sound, so this only limits using plane or cow sounds. */
					if (sfx < ORIGINAL_SAMPLE_COUNT) sfx = SND_18_TAKEOFF_HELICOPTER;
					SndPlayVehicleFx(sfx, v);
				}
			}
		} else {
			u->cur_speed = 32;
			int count = UpdateAircraftSpeed(v);
			if (count > 0) {
				v->tile = 0;

				int z_dest;
				GetAircraftFlightLevelBounds(v, &z_dest, nullptr);

				/* Reached altitude? */
				if (v->z_pos >= z_dest) {
					v->cur_speed = 0;
					return true;
				}
				SetAircraftPosition(v, v->x_pos, v->y_pos, std::min(v->z_pos + count, z_dest));
			}
		}
		return false;
	}

	/* Helicopter landing. */
	if (amd.flag & AMED_HELI_LOWER) {
		SetBit(v->flags, VAF_HELI_DIRECT_DESCENT);

		if (st == nullptr) {
			/* FIXME - AircraftController -> if station no longer exists, do not land
			 * helicopter will circle until sign disappears, then go to next order
			 * what to do when it is the only order left, right now it just stays in 1 place */
			v->state = FLYING;
			UpdateAircraftCache(v);
			AircraftNextAirportPos_and_Order(v);
			return false;
		}

		/* Vehicle is now at the airport.
		 * Helicopter has arrived at the target landing pad, so the current position is also where it should land.
		 * Except for Oilrigs which are special due to being a 1x1 station, and helicopters land outside it. */
		if (st->airport.type != AT_OILRIG) {
			x = v->x_pos;
			y = v->y_pos;
			tile = TileVirtXY(x, y);
		}
		v->tile = tile;

		/* Find altitude of landing position. */
		int z = GetSlopePixelZ(x, y) + 1 + afc->delta_z;

		if (z == v->z_pos) {
			Vehicle *u = v->Next()->Next();

			/*  Increase speed of rotors. When speed is 80, we've landed. */
			if (u->cur_speed >= 80) {
				ClrBit(v->flags, VAF_HELI_DIRECT_DESCENT);
				return true;
			}
			u->cur_speed += 4;
		} else {
			int count = UpdateAircraftSpeed(v);
			if (count > 0) {
				if (v->z_pos > z) {
					SetAircraftPosition(v, v->x_pos, v->y_pos, std::max(v->z_pos - count, z));
				} else {
					SetAircraftPosition(v, v->x_pos, v->y_pos, std::min(v->z_pos + count, z));
				}
			}
		}
		return false;
	}

	/* Get distance from destination pos to current pos. */
	uint dist = abs(x + amd.x - v->x_pos) +  abs(y + amd.y - v->y_pos);

	/* Need exact position? */
	if (!(amd.flag & AMED_EXACTPOS) && dist <= (amd.flag & AMED_SLOWTURN ? 8U : 4U)) return true;

	/* At final pos? */
	if (dist == 0) {
		/* Change direction smoothly to final direction. */
		DirDiff dirdiff = DirDifference(amd.direction, v->direction);
		/* if distance is 0, and plane points in right direction, no point in calling
		 * UpdateAircraftSpeed(). So do it only afterwards */
		if (dirdiff == DIRDIFF_SAME) {
			v->cur_speed = 0;
			return true;
		}

		if (!UpdateAircraftSpeed(v, SPEED_LIMIT_TAXI)) return false;

		v->direction = ChangeDir(v->direction, dirdiff > DIRDIFF_REVERSE ? DIRDIFF_45LEFT : DIRDIFF_45RIGHT);
		v->cur_speed >>= 1;

		SetAircraftPosition(v, v->x_pos, v->y_pos, v->z_pos);
		return false;
	}

	if (amd.flag & AMED_BRAKE && v->cur_speed > SPEED_LIMIT_TAXI * _settings_game.vehicle.plane_speed) {
		MaybeCrashAirplane(v);
		if ((v->vehstatus & VS_CRASHED) != 0) return false;
	}

	uint speed_limit = SPEED_LIMIT_TAXI;
	bool hard_limit = true;

	if (amd.flag & AMED_NOSPDCLAMP)   speed_limit = SPEED_LIMIT_NONE;
	if (amd.flag & AMED_HOLD)       { speed_limit = SPEED_LIMIT_HOLD;     hard_limit = false; }
	if (amd.flag & AMED_LAND)       { speed_limit = SPEED_LIMIT_APPROACH; hard_limit = false; }
	if (amd.flag & AMED_BRAKE)      { speed_limit = SPEED_LIMIT_TAXI;     hard_limit = false; }

	int count = UpdateAircraftSpeed(v, speed_limit, hard_limit);
	if (count == 0) return false;

	/* If the plane will be a few subpixels away from the destination after
	 * this movement loop, start nudging it towards the exact position for
	 * the whole loop. Otherwise, heavily depending on the speed of the plane,
	 * it is possible we totally overshoot the target, causing the plane to
	 * make a loop, and trying again, and again, and again .. */
	bool nudge_towards_target = static_cast<uint>(count) + 3 > dist;

	if (v->turn_counter != 0) v->turn_counter--;

	do {

		GetNewVehiclePosResult gp;

		if (nudge_towards_target || (amd.flag & AMED_LAND)) {
			/* move vehicle one pixel towards target */
			gp.x = (v->x_pos != (x + amd.x)) ?
					v->x_pos + ((x + amd.x > v->x_pos) ? 1 : -1) :
					v->x_pos;
			gp.y = (v->y_pos != (y + amd.y)) ?
					v->y_pos + ((y + amd.y > v->y_pos) ? 1 : -1) :
					v->y_pos;

			/* Oilrigs must keep v->tile as st->airport.tile, since the landing pad is in a non-airport tile */
			gp.new_tile = (st->airport.type == AT_OILRIG) ? st->airport.tile : TileVirtXY(gp.x, gp.y);

		} else {

			/* Turn. Do it slowly if in the air. */
			Direction newdir = GetDirectionTowards(v, x + amd.x, y + amd.y);
			if (newdir != v->direction) {
				if (amd.flag & AMED_SLOWTURN && v->number_consecutive_turns < 8 && v->subtype == AIR_AIRCRAFT) {
					if (v->turn_counter == 0 || newdir == v->last_direction) {
						if (newdir == v->last_direction) {
							v->number_consecutive_turns = 0;
						} else {
							v->number_consecutive_turns++;
						}
						v->turn_counter = 2 * _settings_game.vehicle.plane_speed;
						v->last_direction = v->direction;
						v->direction = newdir;
					}

					/* Move vehicle. */
					gp = GetNewVehiclePos(v);
				} else {
					v->cur_speed >>= 1;
					v->direction = newdir;

					/* When leaving a terminal an aircraft often goes to a position
					 * directly in front of it. If it would move while turning it
					 * would need an two extra turns to end up at the correct position.
					 * To make it easier just disallow all moving while turning as
					 * long as an aircraft is on the ground. */
					gp.x = v->x_pos;
					gp.y = v->y_pos;
					gp.new_tile = gp.old_tile = v->tile;
				}
			} else {
				v->number_consecutive_turns = 0;
				/* Move vehicle. */
				gp = GetNewVehiclePos(v);
			}
		}

		v->tile = gp.new_tile;
		/* If vehicle is in the air, use tile coordinate 0. */
		if (amd.flag & (AMED_TAKEOFF | AMED_SLOWTURN | AMED_LAND)) v->tile = 0;

		/* Adjust Z for land or takeoff? */
		int z = v->z_pos;

		if (amd.flag & AMED_TAKEOFF) {
			z = GetAircraftFlightLevel(v, true);
		} else if (amd.flag & AMED_HOLD) {
			/* Let the plane drop from normal flight altitude to holding pattern altitude */
			if (z > GetAircraftHoldMaxAltitude(v)) z--;
		} else if ((amd.flag & AMED_SLOWTURN) && (amd.flag & AMED_NOSPDCLAMP)) {
			z = GetAircraftFlightLevel(v);
		}

		/* NewGRF airports (like a rotated intercontinental from OpenGFX+Airports) can be non-rectangular
		 * and their primary (north-most) tile does not have to be part of the airport.
		 * As such, the height of the primary tile can be different from the rest of the airport.
		 * Given we are landing/breaking, and as such are not a helicopter, we know that there has to be a hangar.
		 * We also know that the airport itself has to be completely flat (otherwise it is not a valid airport).
		 * Therefore, use the height of this hangar to calculate our z-value. */
		int airport_z = v->z_pos;
		if ((amd.flag & (AMED_LAND | AMED_BRAKE)) && st != nullptr) {
			assert(st->airport.HasHangar());
			TileIndex hangar_tile = st->airport.GetHangarTile(0);
			airport_z = GetTileMaxPixelZ(hangar_tile) + 1; // To avoid clashing with the shadow
		}

		if (amd.flag & AMED_LAND) {
			if (st->airport.tile == INVALID_TILE) {
				/* Airport has been removed, abort the landing procedure */
				v->state = FLYING;
				UpdateAircraftCache(v);
				AircraftNextAirportPos_and_Order(v);
				/* get aircraft back on running altitude */
				SetAircraftPosition(v, gp.x, gp.y, GetAircraftFlightLevel(v));
				continue;
			}

			/* We're not flying below our destination, right? */
			assert(airport_z <= z);
			int t = std::max(1U, dist - 4);
			int delta = z - airport_z;

			/* Only start lowering when we're sufficiently close for a 1:1 glide */
			if (delta >= t) {
				z -= CeilDiv(z - airport_z, t);
			}
			if (z < airport_z) z = airport_z;
		}

		/* We've landed. Decrease speed when we're reaching end of runway. */
		if (amd.flag & AMED_BRAKE) {

			if (z > airport_z) {
				z--;
			} else if (z < airport_z) {
				z++;
			}

		}

		SetAircraftPosition(v, gp.x, gp.y, z);
	} while (--count != 0);
	return false;
}

/**
 * Handle crashed aircraft \a v.
 * @param v Crashed aircraft.
 */
static bool HandleCrashedAircraft(Aircraft *v)
{
	v->crashed_counter += 3;

	Station *st = GetTargetAirportIfValid(v);

	/* make aircraft crash down to the ground */
	if (v->crashed_counter < 500 && st == nullptr && ((v->crashed_counter % 3) == 0) ) {
		int z = GetSlopePixelZ(Clamp(v->x_pos, 0, MapMaxX() * TILE_SIZE), Clamp(v->y_pos, 0, MapMaxY() * TILE_SIZE));
		v->z_pos -= 1;
		if (v->z_pos == z) {
			v->crashed_counter = 500;
			v->z_pos++;
		}
	}

	if (v->crashed_counter < 650) {
		uint32 r;
		if (Chance16R(1, 32, r)) {
			static const DirDiff delta[] = {
				DIRDIFF_45LEFT, DIRDIFF_SAME, DIRDIFF_SAME, DIRDIFF_45RIGHT
			};

			v->direction = ChangeDir(v->direction, delta[GB(r, 16, 2)]);
			SetAircraftPosition(v, v->x_pos, v->y_pos, v->z_pos);
			r = Random();
			CreateEffectVehicleRel(v,
				GB(r, 0, 4) - 4,
				GB(r, 4, 4) - 4,
				GB(r, 8, 4),
				EV_EXPLOSION_SMALL);
		}
	} else if (v->crashed_counter >= 10000) {
		/*  remove rubble of crashed airplane */

		/* clear runway-in on all airports, set by crashing plane
		 * small airports use AIRPORT_BUSY, city airports use RUNWAY_IN_OUT_block, etc.
		 * but they all share the same number */
		if (st != nullptr) {
			CLRBITS(st->airport.flags, RUNWAY_IN_block);
			CLRBITS(st->airport.flags, RUNWAY_IN_OUT_block); // commuter airport
			CLRBITS(st->airport.flags, RUNWAY_IN2_block);    // intercontinental
		}

		delete v;

		return false;
	}

	return true;
}


/**
 * Handle smoke of broken aircraft.
 * @param v Aircraft
 * @param mode Is this the non-first call for this vehicle in this tick?
 */
static void HandleAircraftSmoke(Aircraft *v, bool mode)
{
	static const struct {
		int8 x;
		int8 y;
	} smoke_pos[] = {
		{  5,  5 },
		{  6,  0 },
		{  5, -5 },
		{  0, -6 },
		{ -5, -5 },
		{ -6,  0 },
		{ -5,  5 },
		{  0,  6 }
	};

	if (!(v->vehstatus & VS_AIRCRAFT_BROKEN)) return;

	/* Stop smoking when landed */
	if (v->cur_speed < 10) {
		v->vehstatus &= ~VS_AIRCRAFT_BROKEN;
		v->breakdown_ctr = 0;
		return;
	}

	/* Spawn effect et most once per Tick, i.e. !mode */
	if (!mode && (v->tick_counter & 0x0F) == 0) {
		CreateEffectVehicleRel(v,
			smoke_pos[v->direction].x,
			smoke_pos[v->direction].y,
			2,
			EV_BREAKDOWN_SMOKE_AIRCRAFT
		);
	}
}

void HandleMissingAircraftOrders(Aircraft *v)
{
	/*
	 * We do not have an order. This can be divided into two cases:
	 * 1) we are heading to an invalid station. In this case we must
	 *    find another airport to go to. If there is nowhere to go,
	 *    we will destroy the aircraft as it otherwise will enter
	 *    the holding pattern for the first airport, which can cause
	 *    the plane to go into an undefined state when building an
	 *    airport with the same StationID.
	 * 2) we are (still) heading to a (still) valid airport, then we
	 *    can continue going there. This can happen when you are
	 *    changing the aircraft's orders while in-flight or in for
	 *    example a depot. However, when we have a current order to
	 *    go to a depot, we have to keep that order so the aircraft
	 *    actually stops.
	 */
	const Station *st = GetTargetAirportIfValid(v);
	if (st == nullptr) {
		Backup<CompanyID> cur_company(_current_company, v->owner, FILE_LINE);
		CommandCost ret = DoCommand(v->tile, v->index, 0, DC_EXEC, CMD_SEND_VEHICLE_TO_DEPOT);
		cur_company.Restore();

		if (ret.Failed()) CrashAirplane(v);
	} else if (!v->current_order.IsType(OT_GOTO_DEPOT)) {
		v->current_order.Free();
	}
}


TileIndex Aircraft::GetOrderStationLocation(StationID station)
{
	/* Orders are changed in flight, ensure going to the right station. */
	if (this->state == FLYING) {
		AircraftNextAirportPos_and_Order(this);
	}

	/* Aircraft do not use dest-tile */
	return 0;
}

void Aircraft::MarkDirty()
{
	this->colourmap = PAL_NONE;
	this->UpdateViewport(true, false);
	if (this->subtype == AIR_HELICOPTER) {
		GetRotorImage(this, EIT_ON_MAP, &this->Next()->Next()->sprite_cache.sprite_seq);
	}
}


uint Aircraft::Crash(bool flooded)
{
	uint pass = Vehicle::Crash(flooded) + 2; // pilots
	this->crashed_counter = flooded ? 9000 : 0; // max 10000, disappear pretty fast when flooded

	return pass;
}

/**
 * Bring the aircraft in a crashed state, create the explosion animation, and create a news item about the crash.
 * @param v Aircraft that crashed.
 */
static void CrashAirplane(Aircraft *v)
{
	CreateEffectVehicleRel(v, 4, 4, 8, EV_EXPLOSION_LARGE);

	uint pass = v->Crash();
	SetDParam(0, pass);

	v->cargo.Truncate();
	v->Next()->cargo.Truncate();
	const Station *st = GetTargetAirportIfValid(v);
	StringID newsitem;
	TileIndex vt;
	if (st == nullptr) {
		newsitem = STR_NEWS_PLANE_CRASH_OUT_OF_FUEL;
		vt = TileVirtXY(v->x_pos, v->y_pos);
	} else {
		SetDParam(1, st->index);
		newsitem = STR_NEWS_AIRCRAFT_CRASH;
		vt = v->tile;
	}

	AI::NewEvent(v->owner, new ScriptEventVehicleCrashed(v->index, vt, st == nullptr ? ScriptEventVehicleCrashed::CRASH_AIRCRAFT_NO_AIRPORT : ScriptEventVehicleCrashed::CRASH_PLANE_LANDING));
	Game::NewEvent(new ScriptEventVehicleCrashed(v->index, vt, st == nullptr ? ScriptEventVehicleCrashed::CRASH_AIRCRAFT_NO_AIRPORT : ScriptEventVehicleCrashed::CRASH_PLANE_LANDING));

	AddTileNewsItem(newsitem, NT_ACCIDENT, vt, nullptr, st != nullptr ? st->index : INVALID_STATION);

	ModifyStationRatingAround(vt, v->owner, -160, 30);
	if (_settings_client.sound.disaster) SndPlayVehicleFx(SND_12_EXPLOSION, v);
}

/**
 * Decide whether aircraft \a v should crash.
 * @param v Aircraft to test.
 */
static void MaybeCrashAirplane(Aircraft *v)
{

	Station *st = Station::Get(v->targetairport);

	uint32 prob;
	if ((st->airport.GetFTA()->flags & AirportFTAClass::SHORT_STRIP) &&
			(AircraftVehInfo(v->engine_type)->subtype & AIR_FAST) &&
			!_cheats.no_jetcrash.value) {
		prob = 3276;
	} else {
		if (_settings_game.vehicle.plane_crashes == 0) return;
		prob = (0x4000 << _settings_game.vehicle.plane_crashes) / 1500;
	}

	if (GB(Random(), 0, 22) > prob) return;

	/* Crash the airplane. Remove all goods stored at the station. */
	for (CargoID i = 0; i < NUM_CARGO; i++) {
		st->goods[i].rating = 1;
		st->goods[i].cargo.Truncate();
	}

	CrashAirplane(v);
}

/**
 * Aircraft arrives at a terminal. If it is the first aircraft, throw a party.
 * Start loading cargo.
 * @param v Aircraft that arrived.
 */
static void AircraftEntersTerminal(Aircraft *v)
{
	if (v->current_order.IsType(OT_GOTO_DEPOT)) return;

	Station *st = Station::Get(v->targetairport);
	v->last_station_visited = v->targetairport;

	/* Check if station was ever visited before */
	if (!(st->had_vehicle_of_type & HVOT_AIRCRAFT)) {
		st->had_vehicle_of_type |= HVOT_AIRCRAFT;
		SetDParam(0, st->index);
		/* show newsitem of celebrating citizens */
		AddVehicleNewsItem(
			STR_NEWS_FIRST_AIRCRAFT_ARRIVAL,
			(v->owner == _local_company) ? NT_ARRIVAL_COMPANY : NT_ARRIVAL_OTHER,
			v->index,
			st->index
		);
		AI::NewEvent(v->owner, new ScriptEventStationFirstVehicle(st->index, v->index));
		Game::NewEvent(new ScriptEventStationFirstVehicle(st->index, v->index));
	}

	v->BeginLoading();
}

/**
 * Aircraft touched down at the landing strip.
 * @param v Aircraft that landed.
 */
static void AircraftLandAirplane(Aircraft *v)
{
	Station *st = Station::Get(v->targetairport);

	TileIndex vt = TileVirtXY(v->x_pos, v->y_pos);

	v->UpdateDeltaXY();

	AirportTileAnimationTrigger(st, vt, AAT_STATION_AIRPLANE_LAND);

	if (!PlayVehicleSound(v, VSE_TOUCHDOWN)) {
		SndPlayVehicleFx(SND_17_SKID_PLANE, v);
	}
}


/** set the right pos when heading to other airports after takeoff */
void AircraftNextAirportPos_and_Order(Aircraft *v)
{
	if (v->current_order.IsType(OT_GOTO_STATION) || v->current_order.IsType(OT_GOTO_DEPOT)) {
		v->targetairport = GetDestination(v, &(v->current_order));
	}

	const Station *st = GetTargetAirportIfValid(v);
	const AirportFTAClass *apc = st == nullptr ? GetAirport(AT_DUMMY) : st->airport.GetFTA();
	Direction rotation = st == nullptr ? DIR_N : st->airport.rotation;
	v->pos = v->previous_pos = AircraftGetEntryPoint(v, apc, rotation);
}

/**
 * Aircraft is about to leave the hangar.
 * @param v Aircraft leaving.
 * @param exit_dir The direction the vehicle leaves the hangar.
 * @note This function is called in AfterLoadGame for old savegames, so don't rely
 *       on any data to be valid, especially don't rely on the fact that the vehicle
 *       is actually on the ground inside a depot.
 */
void AircraftLeaveHangar(Aircraft *v, Direction exit_dir)
{
	v->cur_speed = 0;
	v->subspeed = 0;
	v->progress = 0;
	v->direction = exit_dir;
	v->vehstatus &= ~VS_HIDDEN;
	{
		Vehicle *u = v->Next();
		u->vehstatus &= ~VS_HIDDEN;

		/* Rotor blades */
		u = u->Next();
		if (u != nullptr) {
			u->vehstatus &= ~VS_HIDDEN;
			u->cur_speed = 80;
		}
	}

	VehicleServiceInDepot(v);
	SetAircraftPosition(v, v->x_pos, v->y_pos, v->z_pos);
	InvalidateWindowData(WC_VEHICLE_DEPOT, GetDepotIndex(v->tile));
	SetWindowClassesDirty(WC_AIRCRAFT_LIST);
}

////////////////////////////////////////////////////////////////////////////////
///////////////////   AIRCRAFT MOVEMENT SCHEME  ////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
static void AircraftEventHandler_EnterTerminal(Aircraft *v, const AirportFTAClass *apc)
{
	AircraftEntersTerminal(v);
	v->state = apc->layout[v->pos].heading;
}

/**
 * Aircraft arrived in an airport hangar.
 * @param v Aircraft in the hangar.
 * @param apc Airport description containing the hangar.
 */
static void AircraftEventHandler_EnterHangar(Aircraft *v, const AirportFTAClass *apc)
{
	VehicleEnterDepot(v);
	v->state = apc->layout[v->pos].heading;
}

/**
 * Handle aircraft movement/decision making in an airport hangar.
 * @param v Aircraft in the hangar.
 * @param apc Airport description containing the hangar.
 */
static void AircraftEventHandler_InHangar(Aircraft *v, const AirportFTAClass *apc)
{
	/* if we just arrived, execute EnterHangar first */
	if (v->previous_pos != v->pos) {
		AircraftEventHandler_EnterHangar(v, apc);
		return;
	}

	/* if we were sent to the depot, stay there */
	if (v->current_order.IsType(OT_GOTO_DEPOT) && (v->vehstatus & VS_STOPPED)) {
		v->current_order.Free();
		return;
	}

	if (!v->current_order.IsType(OT_GOTO_STATION) &&
			!v->current_order.IsType(OT_GOTO_DEPOT))
		return;

	/* We are leaving a hangar, but have to go to the exact same one; re-enter */
	if (v->current_order.IsType(OT_GOTO_DEPOT) && GetDestination(v, &(v->current_order)) == v->targetairport) {
		VehicleEnterDepot(v);
		return;
	}

	/* if the block of the next position is busy, stay put */
	if (AirportHasBlock(v, &apc->layout[v->pos], apc)) return;

	/* We are already at the target airport, we need to find a terminal */
	if (GetDestination(v, &(v->current_order)) == v->targetairport) {
		/* FindFreeTerminal:
		 * 1. Find a free terminal, 2. Occupy it, 3. Set the vehicle's state to that terminal */
		if (v->subtype == AIR_HELICOPTER) {
			if (!AirportFindFreeHelipad(v, apc)) return; // helicopter
		} else {
			if (!AirportFindFreeTerminal(v, apc)) return; // airplane
		}
	} else { // Else prepare for launch.
		/* airplane goto state takeoff, helicopter to helitakeoff */
		v->state = (v->subtype == AIR_HELICOPTER) ? HELITAKEOFF : TAKEOFF;
	}
	const Station *st = Station::GetByTile(v->tile);
	AircraftLeaveHangar(v, st->airport.GetHangarExitDirection(v->tile));
	AirportMove(v, apc);
}

/** At one of the Airport's Terminals */
static void AircraftEventHandler_AtTerminal(Aircraft *v, const AirportFTAClass *apc)
{
	/* if we just arrived, execute EnterTerminal first */
	if (v->previous_pos != v->pos) {
		AircraftEventHandler_EnterTerminal(v, apc);
		/* on an airport with helipads, a helicopter will always land there
		 * and get serviced at the same time - setting */
		if (_settings_game.order.serviceathelipad) {
			if (v->subtype == AIR_HELICOPTER && apc->num_helipads > 0) {
				/* an excerpt of ServiceAircraft, without the invisibility stuff */
				v->date_of_last_service = _date;
				v->breakdowns_since_last_service = 0;
				v->reliability = v->GetEngine()->reliability;
				SetWindowDirty(WC_VEHICLE_DETAILS, v->index);
			}
		}
		return;
	}

	if (v->current_order.IsType(OT_NOTHING)) return;

	/* if the block of the next position is busy, stay put */
	if (AirportHasBlock(v, &apc->layout[v->pos], apc)) return;

	/* airport-road is free. We either have to go to another airport, or to the hangar
	 * ---> start moving */

	bool go_to_hangar = false;
	switch (v->current_order.GetType()) {
		case OT_GOTO_STATION: // ready to fly to another airport
			break;
		case OT_GOTO_DEPOT:   // visit hangar for servicing, sale, etc.
			go_to_hangar = GetDestination(v, &(v->current_order)) == v->targetairport;
			break;
		case OT_CONDITIONAL:
			/* In case of a conditional order we just have to wait a tick
			 * longer, so the conditional order can actually be processed;
			 * we should not clear the order as that makes us go nowhere. */
			return;
		default:  // orders have been deleted (no orders), goto depot and don't bother us
			v->current_order.Free();
			go_to_hangar = true;
	}

	if (go_to_hangar && Station::Get(v->targetairport)->airport.HasHangar()) {
		v->state = HANGAR;
	} else {
		/* airplane goto state takeoff, helicopter to helitakeoff */
		v->state = (v->subtype == AIR_HELICOPTER) ? HELITAKEOFF : TAKEOFF;
	}
	AirportMove(v, apc);
}

static void AircraftEventHandler_General(Aircraft *v, const AirportFTAClass *apc)
{
	error("OK, you shouldn't be here, check your Airport Scheme!");
}

static void AircraftEventHandler_TakeOff(Aircraft *v, const AirportFTAClass *apc)
{
	PlayAircraftSound(v); // play takeoffsound for airplanes
	v->state = STARTTAKEOFF;
}

static void AircraftEventHandler_StartTakeOff(Aircraft *v, const AirportFTAClass *apc)
{
	v->state = ENDTAKEOFF;
	v->UpdateDeltaXY();
}

static void AircraftEventHandler_EndTakeOff(Aircraft *v, const AirportFTAClass *apc)
{
	v->state = FLYING;
	/* get the next position to go to, differs per airport */
	AircraftNextAirportPos_and_Order(v);
}

static void AircraftEventHandler_HeliTakeOff(Aircraft *v, const AirportFTAClass *apc)
{
	v->state = FLYING;
	v->UpdateDeltaXY();

	/* get the next position to go to, differs per airport */
	AircraftNextAirportPos_and_Order(v);

	/* Send the helicopter to a hangar if needed for replacement */
	if (v->NeedsAutomaticServicing()) {
		Backup<CompanyID> cur_company(_current_company, v->owner, FILE_LINE);
		DoCommand(v->tile, v->index | DEPOT_SERVICE | DEPOT_LOCATE_HANGAR, 0, DC_EXEC, CMD_SEND_VEHICLE_TO_DEPOT);
		cur_company.Restore();
	}
}

static void AircraftEventHandler_Flying(Aircraft *v, const AirportFTAClass *apc)
{
	Station *st = Station::Get(v->targetairport);

	/* Runway busy, not allowed to use this airstation or closed, circle. */
	if (CanVehicleUseStation(v, st) && (st->owner == OWNER_NONE || st->owner == v->owner) && !(st->airport.flags & AIRPORT_CLOSED_block)) {
		/* {32,FLYING,NOTHING_block,37}, {32,LANDING,N,33}, {32,HELILANDING,N,41},
		 * if it is an airplane, look for LANDING, for helicopter HELILANDING
		 * it is possible to choose from multiple landing runways, so loop until a free one is found */
		byte landingtype = (v->subtype == AIR_HELICOPTER) ? HELILANDING : LANDING;
		const AirportFTA *current = apc->layout[v->pos].next;
		while (current != nullptr) {
			if (current->heading == landingtype) {
				/* save speed before, since if AirportHasBlock is false, it resets them to 0
				 * we don't want that for plane in air
				 * hack for speed thingie */
				uint16 tcur_speed = v->cur_speed;
				uint16 tsubspeed = v->subspeed;
				if (!AirportHasBlock(v, current, apc)) {
					v->state = landingtype; // LANDING / HELILANDING
					if (v->state == HELILANDING) SetBit(v->flags, VAF_HELI_DIRECT_DESCENT);
					/* it's a bit dirty, but I need to set position to next position, otherwise
					 * if there are multiple runways, plane won't know which one it took (because
					 * they all have heading LANDING). And also occupy that block! */
					v->pos = current->next_position;
					SETBITS(st->airport.flags, apc->layout[v->pos].block);
					return;
				}
				v->cur_speed = tcur_speed;
				v->subspeed = tsubspeed;
			}
			current = current->next;
		}
	}
	v->state = FLYING;
	v->pos = apc->layout[v->pos].next_position;
}

static void AircraftEventHandler_Landing(Aircraft *v, const AirportFTAClass *apc)
{
	v->state = ENDLANDING;
	AircraftLandAirplane(v);  // maybe crash airplane

	/* check if the aircraft needs to be replaced or renewed and send it to a hangar if needed */
	if (v->NeedsAutomaticServicing()) {
		Backup<CompanyID> cur_company(_current_company, v->owner, FILE_LINE);
		DoCommand(v->tile, v->index | DEPOT_SERVICE, 0, DC_EXEC, CMD_SEND_VEHICLE_TO_DEPOT);
		cur_company.Restore();
	}
}

static void AircraftEventHandler_HeliLanding(Aircraft *v, const AirportFTAClass *apc)
{
	v->state = HELIENDLANDING;
	v->UpdateDeltaXY();
}

static void AircraftEventHandler_EndLanding(Aircraft *v, const AirportFTAClass *apc)
{
	/* next block busy, don't do a thing, just wait */
	if (AirportHasBlock(v, &apc->layout[v->pos], apc)) return;

	/* if going to terminal (OT_GOTO_STATION) choose one
	 * 1. in case all terminals are busy AirportFindFreeTerminal() returns false or
	 * 2. not going for terminal (but depot, no order),
	 * --> get out of the way to the hangar. */
	if (v->current_order.IsType(OT_GOTO_STATION)) {
		if (AirportFindFreeTerminal(v, apc)) return;
	}
	v->state = HANGAR;

}

static void AircraftEventHandler_HeliEndLanding(Aircraft *v, const AirportFTAClass *apc)
{
	/*  next block busy, don't do a thing, just wait */
	if (AirportHasBlock(v, &apc->layout[v->pos], apc)) return;

	/* if going to helipad (OT_GOTO_STATION) choose one. If airport doesn't have helipads, choose terminal
	 * 1. in case all terminals/helipads are busy (AirportFindFreeHelipad() returns false) or
	 * 2. not going for terminal (but depot, no order),
	 * --> get out of the way to the hangar IF there are terminals on the airport.
	 * --> else TAKEOFF
	 * the reason behind this is that if an airport has a terminal, it also has a hangar. Airplanes
	 * must go to a hangar. */
	if (v->current_order.IsType(OT_GOTO_STATION)) {
		if (AirportFindFreeHelipad(v, apc)) return;
	}
	v->state = Station::Get(v->targetairport)->airport.HasHangar() ? HANGAR : HELITAKEOFF;
}

/**
 * Signature of the aircraft handler function.
 * @param v Aircraft to handle.
 * @param apc Airport state machine.
 */
typedef void AircraftStateHandler(Aircraft *v, const AirportFTAClass *apc);
/** Array of handler functions for each target of the aircraft. */
static AircraftStateHandler * const _aircraft_state_handlers[] = {
	AircraftEventHandler_General,        // TO_ALL         =  0
	AircraftEventHandler_InHangar,       // HANGAR         =  1
	AircraftEventHandler_AtTerminal,     // TERM1          =  2
	AircraftEventHandler_AtTerminal,     // TERM2          =  3
	AircraftEventHandler_AtTerminal,     // TERM3          =  4
	AircraftEventHandler_AtTerminal,     // TERM4          =  5
	AircraftEventHandler_AtTerminal,     // TERM5          =  6
	AircraftEventHandler_AtTerminal,     // TERM6          =  7
	AircraftEventHandler_AtTerminal,     // HELIPAD1       =  8
	AircraftEventHandler_AtTerminal,     // HELIPAD2       =  9
	AircraftEventHandler_TakeOff,        // TAKEOFF        = 10
	AircraftEventHandler_StartTakeOff,   // STARTTAKEOFF   = 11
	AircraftEventHandler_EndTakeOff,     // ENDTAKEOFF     = 12
	AircraftEventHandler_HeliTakeOff,    // HELITAKEOFF    = 13
	AircraftEventHandler_Flying,         // FLYING         = 14
	AircraftEventHandler_Landing,        // LANDING        = 15
	AircraftEventHandler_EndLanding,     // ENDLANDING     = 16
	AircraftEventHandler_HeliLanding,    // HELILANDING    = 17
	AircraftEventHandler_HeliEndLanding, // HELIENDLANDING = 18
	AircraftEventHandler_AtTerminal,     // TERM7          = 19
	AircraftEventHandler_AtTerminal,     // TERM8          = 20
	AircraftEventHandler_AtTerminal,     // HELIPAD3       = 21
};

static void AirportClearBlock(const Aircraft *v, const AirportFTAClass *apc)
{
	/* we have left the previous block, and entered the new one. Free the previous block */
	if (apc->layout[v->previous_pos].block != apc->layout[v->pos].block) {
		Station *st = Station::Get(v->targetairport);

		CLRBITS(st->airport.flags, apc->layout[v->previous_pos].block);
	}
}

static void AirportGoToNextPosition(Aircraft *v)
{
	/* if aircraft is not in position, wait until it is */
	if (!AircraftController(v)) return;

	const AirportFTAClass *apc = Station::Get(v->targetairport)->airport.GetFTA();

	AirportClearBlock(v, apc);
	AirportMove(v, apc); // move aircraft to next position
}

/* gets pos from vehicle and next orders */
static bool AirportMove(Aircraft *v, const AirportFTAClass *apc)
{
	/* error handling */
	if (v->pos >= apc->nofelements) {
		Debug(misc, 0, "[Ap] position {} is not valid for current airport. Max position is {}", v->pos, apc->nofelements-1);
		assert(v->pos < apc->nofelements);
	}

	const AirportFTA *current = &apc->layout[v->pos];
	/* we have arrived in an important state (eg terminal, hangar, etc.) */
	if (current->heading == v->state) {
		byte prev_pos = v->pos; // location could be changed in state, so save it before-hand
		byte prev_state = v->state;
		_aircraft_state_handlers[v->state](v, apc);
		if (v->state != FLYING) v->previous_pos = prev_pos;
		if (v->state != prev_state || v->pos != prev_pos) UpdateAircraftCache(v);
		return true;
	}

	v->previous_pos = v->pos; // save previous location

	/* there is only one choice to move to */
	if (current->next == nullptr) {
		if (AirportSetBlocks(v, current, apc)) {
			v->pos = current->next_position;
			UpdateAircraftCache(v);
		} // move to next position
		return false;
	}

	/* there are more choices to choose from, choose the one that
	 * matches our heading */
	do {
		if (v->state == current->heading || current->heading == TO_ALL) {
			if (AirportSetBlocks(v, current, apc)) {
				v->pos = current->next_position;
				UpdateAircraftCache(v);
			} // move to next position
			return false;
		}
		current = current->next;
	} while (current != nullptr);

	Debug(misc, 0, "[Ap] cannot move further on Airport! (pos {} state {}) for vehicle {}", v->pos, v->state, v->index);
	NOT_REACHED();
}

/** returns true if the road ahead is busy, eg. you must wait before proceeding. */
static bool AirportHasBlock(Aircraft *v, const AirportFTA *current_pos, const AirportFTAClass *apc)
{
	const AirportFTA *reference = &apc->layout[v->pos];
	const AirportFTA *next = &apc->layout[current_pos->next_position];

	/* same block, then of course we can move */
	if (apc->layout[current_pos->position].block != next->block) {
		const Station *st = Station::Get(v->targetairport);
		uint64 airport_flags = next->block;

		/* check additional possible extra blocks */
		if (current_pos != reference && current_pos->block != NOTHING_block) {
			airport_flags |= current_pos->block;
		}

		if (st->airport.flags & airport_flags) {
			v->cur_speed = 0;
			v->subspeed = 0;
			return true;
		}
	}
	return false;
}

/**
 * "reserve" a block for the plane
 * @param v airplane that requires the operation
 * @param current_pos of the vehicle in the list of blocks
 * @param apc airport on which block is requested to be set
 * @returns true on success. Eg, next block was free and we have occupied it
 */
static bool AirportSetBlocks(Aircraft *v, const AirportFTA *current_pos, const AirportFTAClass *apc)
{
	const AirportFTA *next = &apc->layout[current_pos->next_position];
	const AirportFTA *reference = &apc->layout[v->pos];

	/* if the next position is in another block, check it and wait until it is free */
	if ((apc->layout[current_pos->position].block & next->block) != next->block) {
		uint64 airport_flags = next->block;
		/* search for all all elements in the list with the same state, and blocks != N
		 * this means more blocks should be checked/set */
		const AirportFTA *current = current_pos;
		if (current == reference) current = current->next;
		while (current != nullptr) {
			if (current->heading == current_pos->heading && current->block != 0) {
				airport_flags |= current->block;
				break;
			}
			current = current->next;
		}

		/* if the block to be checked is in the next position, then exclude that from
		 * checking, because it has been set by the airplane before */
		if (current_pos->block == next->block) airport_flags ^= next->block;

		Station *st = Station::Get(v->targetairport);
		if (st->airport.flags & airport_flags) {
			v->cur_speed = 0;
			v->subspeed = 0;
			return false;
		}

		if (next->block != NOTHING_block) {
			SETBITS(st->airport.flags, airport_flags); // occupy next block
		}
	}
	return true;
}

/**
 * Combination of aircraft state for going to a certain terminal and the
 * airport flag for that terminal block.
 */
struct MovementTerminalMapping {
	AirportMovementStates state; ///< Aircraft movement state when going to this terminal.
	uint64 airport_flag;         ///< Bitmask in the airport flags that need to be free for this terminal.
};

/** A list of all valid terminals and their associated blocks. */
static const MovementTerminalMapping _airport_terminal_mapping[] = {
	{TERM1, TERM1_block},
	{TERM2, TERM2_block},
	{TERM3, TERM3_block},
	{TERM4, TERM4_block},
	{TERM5, TERM5_block},
	{TERM6, TERM6_block},
	{TERM7, TERM7_block},
	{TERM8, TERM8_block},
	{HELIPAD1, HELIPAD1_block},
	{HELIPAD2, HELIPAD2_block},
	{HELIPAD3, HELIPAD3_block},
};

/**
 * Find a free terminal or helipad, and if available, assign it.
 * @param v Aircraft looking for a free terminal or helipad.
 * @param i First terminal to examine.
 * @param last_terminal Terminal number to stop examining.
 * @return A terminal or helipad has been found, and has been assigned to the aircraft.
 */
static bool FreeTerminal(Aircraft *v, byte i, byte last_terminal)
{
	assert(last_terminal <= lengthof(_airport_terminal_mapping));
	Station *st = Station::Get(v->targetairport);
	for (; i < last_terminal; i++) {
		if ((st->airport.flags & _airport_terminal_mapping[i].airport_flag) == 0) {
			/* TERMINAL# HELIPAD# */
			v->state = _airport_terminal_mapping[i].state; // start moving to that terminal/helipad
			SETBITS(st->airport.flags, _airport_terminal_mapping[i].airport_flag); // occupy terminal/helipad
			return true;
		}
	}
	return false;
}

/**
 * Get the number of terminals at the airport.
 * @param apc Airport description.
 * @return Number of terminals.
 */
static uint GetNumTerminals(const AirportFTAClass *apc)
{
	uint num = 0;

	for (uint i = apc->terminals[0]; i > 0; i--) num += apc->terminals[i];

	return num;
}

/**
 * Find a free terminal, and assign it if available.
 * @param v Aircraft to handle.
 * @param apc Airport state machine.
 * @return Found a free terminal and assigned it.
 */
static bool AirportFindFreeTerminal(Aircraft *v, const AirportFTAClass *apc)
{
	/* example of more terminalgroups
	 * {0,HANGAR,NOTHING_block,1}, {0,TERMGROUP,TERM_GROUP1_block,0}, {0,TERMGROUP,TERM_GROUP2_ENTER_block,1}, {0,0,N,1},
	 * Heading TERMGROUP denotes a group. We see 2 groups here:
	 * 1. group 0 -- TERM_GROUP1_block (check block)
	 * 2. group 1 -- TERM_GROUP2_ENTER_block (check block)
	 * First in line is checked first, group 0. If the block (TERM_GROUP1_block) is free, it
	 * looks at the corresponding terminals of that group. If no free ones are found, other
	 * possible groups are checked (in this case group 1, since that is after group 0). If that
	 * fails, then attempt fails and plane waits
	 */
	if (apc->terminals[0] > 1) {
		const Station *st = Station::Get(v->targetairport);
		const AirportFTA *temp = apc->layout[v->pos].next;

		while (temp != nullptr) {
			if (temp->heading == TERMGROUP) {
				if (!(st->airport.flags & temp->block)) {
					/* read which group do we want to go to?
					 * (the first free group) */
					uint target_group = temp->next_position + 1;

					/* at what terminal does the group start?
					 * that means, sum up all terminals of
					 * groups with lower number */
					uint group_start = 0;
					for (uint i = 1; i < target_group; i++) {
						group_start += apc->terminals[i];
					}

					uint group_end = group_start + apc->terminals[target_group];
					if (FreeTerminal(v, group_start, group_end)) return true;
				}
			} else {
				/* once the heading isn't 255, we've exhausted the possible blocks.
				 * So we cannot move */
				return false;
			}
			temp = temp->next;
		}
	}

	/* if there is only 1 terminalgroup, all terminals are checked (starting from 0 to max) */
	return FreeTerminal(v, 0, GetNumTerminals(apc));
}

/**
 * Find a free helipad, and assign it if available.
 * @param v Aircraft to handle.
 * @param apc Airport state machine.
 * @return Found a free helipad and assigned it.
 */
static bool AirportFindFreeHelipad(Aircraft *v, const AirportFTAClass *apc)
{
	/* if an airport doesn't have helipads, use terminals */
	if (apc->num_helipads == 0) return AirportFindFreeTerminal(v, apc);

	/* only 1 helicoptergroup, check all helipads
	 * The blocks for helipads start after the last terminal (MAX_TERMINALS) */
	return FreeTerminal(v, MAX_TERMINALS, apc->num_helipads + MAX_TERMINALS);
}

/**
 * Handle the 'dest too far' flag and the corresponding news message for aircraft.
 * @param v The aircraft.
 * @param too_far True if the current destination is too far away.
 */
static void AircraftHandleDestTooFar(Aircraft *v, bool too_far)
{
	if (too_far) {
		if (!HasBit(v->flags, VAF_DEST_TOO_FAR)) {
			SetBit(v->flags, VAF_DEST_TOO_FAR);
			SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
			AI::NewEvent(v->owner, new ScriptEventAircraftDestTooFar(v->index));
			if (v->owner == _local_company) {
				/* Post a news message. */
				SetDParam(0, v->index);
				AddVehicleAdviceNewsItem(STR_NEWS_AIRCRAFT_DEST_TOO_FAR, v->index);
			}
		}
		return;
	}

	if (HasBit(v->flags, VAF_DEST_TOO_FAR)) {
		/* Not too far anymore, clear flag and message. */
		ClrBit(v->flags, VAF_DEST_TOO_FAR);
		SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
		DeleteVehicleNews(v->index, STR_NEWS_AIRCRAFT_DEST_TOO_FAR);
	}
}

static bool AircraftEventHandler(Aircraft *v, int loop)
{
	if (v->vehstatus & VS_CRASHED) {
		return HandleCrashedAircraft(v);
	}

	if (v->vehstatus & VS_STOPPED) return true;

	v->HandleBreakdown();

	HandleAircraftSmoke(v, loop != 0);
	ProcessOrders(v);
	v->HandleLoading(loop != 0);

	if (v->current_order.IsType(OT_LOADING) || v->current_order.IsType(OT_LEAVESTATION)) return true;

	if (v->state >= ENDTAKEOFF && v->state <= HELIENDLANDING) {
		/* If we are flying, unconditionally clear the 'dest too far' state. */
		AircraftHandleDestTooFar(v, false);
	} else if (v->acache.cached_max_range_sqr != 0) {
		/* Check the distance to the next destination. This code works because the target
		 * airport is only updated after take off and not on the ground. */
		Station *cur_st = Station::GetIfValid(v->targetairport);
		Station *next_st = Station::GetIfValid(GetDestination(v, &(v->current_order)));

		if (cur_st != nullptr && cur_st->airport.tile != INVALID_TILE && next_st != nullptr && next_st->airport.tile != INVALID_TILE) {
			uint dist = DistanceSquare(cur_st->airport.tile, next_st->airport.tile);
			AircraftHandleDestTooFar(v, dist > v->acache.cached_max_range_sqr);
		}
	}

	if (!HasBit(v->flags, VAF_DEST_TOO_FAR)) AirportGoToNextPosition(v);

	return true;
}

bool Aircraft::Tick()
{
	if (!this->IsNormalAircraft()) return true;

	PerformanceAccumulator framerate(PFE_GL_AIRCRAFT);

	this->tick_counter++;

	if (!(this->vehstatus & VS_STOPPED)) this->running_ticks++;

	if (this->subtype == AIR_HELICOPTER) HelicopterTickHandler(this);

	this->current_order_time++;

	for (uint i = 0; i != 2; i++) {
		/* stop if the aircraft was deleted */
		if (!AircraftEventHandler(this, i)) return false;
	}

	return true;
}


/**
 * Returns aircraft's target station if v->target_airport
 * is a valid station with airport.
 * @param v vehicle to get target airport for
 * @return pointer to target station, nullptr if invalid
 */
Station *GetTargetAirportIfValid(const Aircraft *v)
{
	assert(v->type == VEH_AIRCRAFT);

	Station *st = Station::GetIfValid(v->targetairport);
	if (st == nullptr) return nullptr;

	return st->airport.tile == INVALID_TILE ? nullptr : st;
}

/**
 * Updates the status of the Aircraft heading or in the station
 * @param st Station been updated
 */
void UpdateAirplanesOnNewStation(const Station *st)
{
	/* only 1 station is updated per function call, so it is enough to get entry_point once */
	const AirportFTAClass *ap = st->airport.GetFTA();
	Direction rotation = st->airport.tile == INVALID_TILE ? DIR_N : st->airport.rotation;

	for (Aircraft *v : Aircraft::Iterate()) {
		if (!v->IsNormalAircraft() || v->targetairport != st->index) continue;
		assert(v->state == FLYING);

		Order *o = &v->current_order;
		/* The aircraft is heading to a hangar, but the new station doesn't have one,
		 * or the aircraft can't land on the new station. Cancel current order. */
		if (o->IsType(OT_GOTO_DEPOT) && !(o->GetDepotOrderType() & ODTFB_PART_OF_ORDERS) && o->GetDestination() == st->airport.depot_id &&
				(!st->airport.HasHangar() || !CanVehicleUseStation(v, st))) {
			o->MakeDummy();
			SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
		}
		v->pos = v->previous_pos = AircraftGetEntryPoint(v, ap, rotation);
		UpdateAircraftCache(v);
	}

	/* Heliports don't have a hangar. Invalidate all go to hangar orders from all aircraft. */
	if (!st->airport.HasHangar()) RemoveOrderFromAllVehicles(OT_GOTO_DEPOT, st->airport.depot_id);
}
