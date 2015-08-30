/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file train_cmd.cpp Handling of trains. */

#include "stdafx.h"
#include "error.h"
#include "articulated_vehicles.h"
#include "command_func.h"
#include "pathfinder/npf/npf_func.h"
#include "pathfinder/yapf/yapf.hpp"
#include "news_func.h"
#include "company_func.h"
#include "newgrf_sound.h"
#include "newgrf_text.h"
#include "strings_func.h"
#include "viewport_func.h"
#include "vehicle_func.h"
#include "sound_func.h"
#include "ai/ai.hpp"
#include "game/game.hpp"
#include "newgrf_station.h"
#include "effectvehicle_func.h"
#include "network/network.h"
#include "spritecache.h"
#include "core/random_func.hpp"
#include "company_base.h"
#include "newgrf.h"
#include "order_backup.h"
#include "zoom_func.h"
#include "newgrf_debug.h"
#include "framerate_type.h"
#include "depot_base.h"
#include "platform_func.h"
#include "depot_map.h"
#include "train_placement.h"

#include "table/strings.h"
#include "table/train_cmd.h"

#include "safeguards.h"

static Track ChooseTrainTrack(Train *v, TileIndex tile, DiagDirection enterdir, TrackBits tracks, bool force_res, bool *got_reservation, bool mark_stuck);
static bool TrainCheckIfLineEnds(Train *v, bool reverse = true);
bool TrainController(Train *v, Vehicle *nomove, bool reverse = true); // Also used in vehicle_sl.cpp.
static TileIndex TrainApproachingCrossingTile(const Train *v);
static void CheckIfTrainNeedsService(Train *v);
static void CheckNextTrainTile(Train *v);

template <>
bool IsValidImageIndex<VEH_TRAIN>(uint8 image_index)
{
	return image_index < lengthof(_engine_sprite_base);
}


/**
 * Return the cargo weight multiplier to use for a rail vehicle
 * @param cargo Cargo type to get multiplier for
 * @return Cargo weight multiplier
 */
byte FreightWagonMult(CargoID cargo)
{
	if (!CargoSpec::Get(cargo)->is_freight) return 1;
	return _settings_game.vehicle.freight_trains;
}

/** Checks if lengths of all rail vehicles are valid. If not, shows an error message. */
void CheckTrainsLengths()
{
	bool first = true;

	for (const Train *v : Train::Iterate()) {
		if (v->First() == v && !(v->vehstatus & VS_CRASHED)) {
			for (const Train *u = v, *w = v->Next(); w != nullptr; u = w, w = w->Next()) {
				if (u->track != TRACK_BIT_DEPOT) {
					if ((w->track != TRACK_BIT_DEPOT &&
							std::max(abs(u->x_pos - w->x_pos), abs(u->y_pos - w->y_pos)) != u->CalcNextVehicleOffset()) ||
							(w->track == TRACK_BIT_DEPOT && TicksToLeaveDepot(u) <= 0)) {
						SetDParam(0, v->index);
						SetDParam(1, v->owner);
						ShowErrorMessage(STR_BROKEN_VEHICLE_LENGTH, INVALID_STRING_ID, WL_CRITICAL);

						if (!_networking && first) {
							first = false;
							DoCommandP(0, PM_PAUSED_ERROR, 1, CMD_PAUSE);
						}
						/* Break so we warn only once for each train. */
						break;
					}
				}
			}
		}
	}
}

/**
 * Recalculates the cached stuff of a train. Should be called each time a vehicle is added
 * to/removed from the chain, and when the game is loaded.
 * Note: this needs to be called too for 'wagon chains' (in the depot, without an engine)
 * @param allowed_changes Stuff that is allowed to change.
 */
void Train::ConsistChanged(ConsistChangeFlags allowed_changes)
{
	uint16 max_speed = UINT16_MAX;

	assert(this->IsFrontEngine() || this->IsFreeWagon());

	const RailVehicleInfo *rvi_v = RailVehInfo(this->engine_type);
	EngineID first_engine = this->IsFrontEngine() ? this->engine_type : INVALID_ENGINE;
	this->gcache.cached_total_length = 0;
	this->compatible_railtypes = RAILTYPES_NONE;

	bool train_can_tilt = true;

	for (Train *u = this; u != nullptr; u = u->Next()) {
		const RailVehicleInfo *rvi_u = RailVehInfo(u->engine_type);

		/* Check the this->first cache. */
		assert(u->First() == this);

		/* update the 'first engine' */
		u->gcache.first_engine = this == u ? INVALID_ENGINE : first_engine;
		u->railtype = rvi_u->railtype;

		if (u->IsEngine()) first_engine = u->engine_type;

		/* Set user defined data to its default value */
		u->tcache.user_def_data = rvi_u->user_def_data;
		this->InvalidateNewGRFCache();
		u->InvalidateNewGRFCache();
	}

	for (Train *u = this; u != nullptr; u = u->Next()) {
		/* Update user defined data (must be done before other properties) */
		u->tcache.user_def_data = GetVehicleProperty(u, PROP_TRAIN_USER_DATA, u->tcache.user_def_data);
		this->InvalidateNewGRFCache();
		u->InvalidateNewGRFCache();
	}

	for (Train *u = this; u != nullptr; u = u->Next()) {
		const Engine *e_u = u->GetEngine();
		const RailVehicleInfo *rvi_u = &e_u->u.rail;

		if (!HasBit(e_u->info.misc_flags, EF_RAIL_TILTS)) train_can_tilt = false;

		/* Cache wagon override sprite group. nullptr is returned if there is none */
		u->tcache.cached_override = GetWagonOverrideSpriteSet(u->engine_type, u->cargo_type, u->gcache.first_engine);

		/* Reset colour map */
		u->colourmap = PAL_NONE;

		/* Update powered-wagon-status and visual effect */
		u->UpdateVisualEffect(true);

		if (rvi_v->pow_wag_power != 0 && rvi_u->railveh_type == RAILVEH_WAGON &&
				UsesWagonOverride(u) && !HasBit(u->vcache.cached_vis_effect, VE_DISABLE_WAGON_POWER)) {
			/* wagon is powered */
			SetBit(u->flags, VRF_POWEREDWAGON); // cache 'powered' status
		} else {
			ClrBit(u->flags, VRF_POWEREDWAGON);
		}

		if (!u->IsArticulatedPart()) {
			/* Do not count powered wagons for the compatible railtypes, as wagons always
			   have railtype normal */
			if (rvi_u->power > 0) {
				this->compatible_railtypes |= GetRailTypeInfo(u->railtype)->powered_railtypes;
			}

			/* Some electric engines can be allowed to run on normal rail. It happens to all
			 * existing electric engines when elrails are disabled and then re-enabled */
			if (HasBit(u->flags, VRF_EL_ENGINE_ALLOWED_NORMAL_RAIL)) {
				u->railtype = RAILTYPE_RAIL;
				u->compatible_railtypes |= RAILTYPES_RAIL;
			}

			/* max speed is the minimum of the speed limits of all vehicles in the consist */
			if ((rvi_u->railveh_type != RAILVEH_WAGON || _settings_game.vehicle.wagon_speed_limits) && !UsesWagonOverride(u)) {
				uint16 speed = GetVehicleProperty(u, PROP_TRAIN_SPEED, rvi_u->max_speed);
				if (speed != 0) max_speed = std::min(speed, max_speed);
			}
		}

		uint16 new_cap = e_u->DetermineCapacity(u);
		if (allowed_changes & CCF_CAPACITY) {
			/* Update vehicle capacity. */
			if (u->cargo_cap > new_cap) u->cargo.Truncate(new_cap);
			u->refit_cap = std::min(new_cap, u->refit_cap);
			u->cargo_cap = new_cap;
		} else {
			/* Verify capacity hasn't changed. */
			if (new_cap != u->cargo_cap) ShowNewGrfVehicleError(u->engine_type, STR_NEWGRF_BROKEN, STR_NEWGRF_BROKEN_CAPACITY, GBUG_VEH_CAPACITY, true);
		}
		u->vcache.cached_cargo_age_period = GetVehicleProperty(u, PROP_TRAIN_CARGO_AGE_PERIOD, e_u->info.cargo_age_period);

		/* check the vehicle length (callback) */
		uint16 veh_len = CALLBACK_FAILED;
		if (e_u->GetGRF() != nullptr && e_u->GetGRF()->grf_version >= 8) {
			/* Use callback 36 */
			veh_len = GetVehicleProperty(u, PROP_TRAIN_SHORTEN_FACTOR, CALLBACK_FAILED);

			if (veh_len != CALLBACK_FAILED && veh_len >= VEHICLE_LENGTH) {
				ErrorUnknownCallbackResult(e_u->GetGRFID(), CBID_VEHICLE_LENGTH, veh_len);
			}
		} else if (HasBit(e_u->info.callback_mask, CBM_VEHICLE_LENGTH)) {
			/* Use callback 11 */
			veh_len = GetVehicleCallback(CBID_VEHICLE_LENGTH, 0, 0, u->engine_type, u);
		}
		if (veh_len == CALLBACK_FAILED) veh_len = rvi_u->shorten_factor;
		veh_len = VEHICLE_LENGTH - Clamp(veh_len, 0, VEHICLE_LENGTH - 1);

		if (allowed_changes & CCF_LENGTH) {
			/* Update vehicle length. */
			u->gcache.cached_veh_length = veh_len;
		} else {
			/* Verify length hasn't changed. */
			if (veh_len != u->gcache.cached_veh_length) VehicleLengthChanged(u);
		}

		this->gcache.cached_total_length += u->gcache.cached_veh_length;
		this->InvalidateNewGRFCache();
		u->InvalidateNewGRFCache();
	}

	/* store consist weight/max speed in cache */
	this->vcache.cached_max_speed = max_speed;
	this->tcache.cached_tilt = train_can_tilt;
	this->tcache.cached_max_curve_speed = this->GetCurveSpeedLimit();

	/* recalculate cached weights and power too (we do this *after* the rest, so it is known which wagons are powered and need extra weight added) */
	this->CargoChanged();

	if (this->IsFrontEngine()) {
		this->UpdateAcceleration();
		SetWindowDirty(WC_VEHICLE_DETAILS, this->index);
		InvalidateWindowData(WC_VEHICLE_REFIT, this->index, VIWD_CONSIST_CHANGED);
		InvalidateWindowData(WC_VEHICLE_ORDERS, this->index, VIWD_CONSIST_CHANGED);
		InvalidateNewGRFInspectWindow(GSF_TRAINS, this->index);
	}
}

/**
 * Get the stop location of (the center) of the front vehicle of a train at
 * a platform of a station.
 * @param station_id     the ID of the station where we're stopping
 * @param tile           the tile where the vehicle currently is
 * @param v              the vehicle to get the stop location of
 * @param station_ahead  'return' the amount of 1/16th tiles in front of the train
 * @param station_length 'return' the station length in 1/16th tiles
 * @return the location, calculated from the begin of the station to stop at.
 */
int GetTrainStopLocation(StationID station_id, TileIndex tile, const Train *v, int *station_ahead, int *station_length)
{
	assert(IsRailStationTile(tile));
	*station_ahead  = GetPlatformLength(tile, DirToDiagDir(v->direction)) * TILE_SIZE;
	*station_length = GetPlatformLength(tile) * TILE_SIZE;

	/* Default to the middle of the station for stations stops that are not in
	 * the order list like intermediate stations when non-stop is disabled */
	OrderStopLocation osl = OSL_PLATFORM_MIDDLE;
	if (v->gcache.cached_total_length >= *station_length) {
		/* The train is longer than the station, make it stop at the far end of the platform */
		osl = OSL_PLATFORM_FAR_END;
	} else if (v->current_order.IsType(OT_GOTO_STATION) && v->current_order.GetDestination() == station_id) {
		osl = v->current_order.GetStopLocation();
	}

	/* The stop location of the FRONT! of the train */
	int stop;
	switch (osl) {
		default: NOT_REACHED();

		case OSL_PLATFORM_NEAR_END:
			stop = v->gcache.cached_total_length;
			break;

		case OSL_PLATFORM_MIDDLE:
			stop = *station_length - (*station_length - v->gcache.cached_total_length) / 2;
			break;

		case OSL_PLATFORM_FAR_END:
			stop = *station_length;
			break;
	}

	/* Subtract half the front vehicle length of the train so we get the real
	 * stop location of the train. */
	return stop - (v->gcache.cached_veh_length + 1) / 2;
}


/**
 * Computes train speed limit caused by curves
 * @return imposed speed limit
 */
int Train::GetCurveSpeedLimit() const
{
	assert(this->First() == this);

	static const int absolute_max_speed = UINT16_MAX;
	int max_speed = absolute_max_speed;

	if (_settings_game.vehicle.train_acceleration_model == AM_ORIGINAL) return max_speed;

	int curvecount[2] = {0, 0};

	/* first find the curve speed limit */
	int numcurve = 0;
	int sum = 0;
	int pos = 0;
	int lastpos = -1;
	for (const Vehicle *u = this; u->Next() != nullptr; u = u->Next(), pos++) {
		Direction this_dir = u->direction;
		Direction next_dir = u->Next()->direction;

		DirDiff dirdiff = DirDifference(this_dir, next_dir);
		if (dirdiff == DIRDIFF_SAME) continue;

		if (dirdiff == DIRDIFF_45LEFT) curvecount[0]++;
		if (dirdiff == DIRDIFF_45RIGHT) curvecount[1]++;
		if (dirdiff == DIRDIFF_45LEFT || dirdiff == DIRDIFF_45RIGHT) {
			if (lastpos != -1) {
				numcurve++;
				sum += pos - lastpos;
				if (pos - lastpos == 1 && max_speed > 88) {
					max_speed = 88;
				}
			}
			lastpos = pos;
		}

		/* if we have a 90 degree turn, fix the speed limit to 60 */
		if (dirdiff == DIRDIFF_90LEFT || dirdiff == DIRDIFF_90RIGHT) {
			max_speed = 61;
		}
	}

	if (numcurve > 0 && max_speed > 88) {
		if (curvecount[0] == 1 && curvecount[1] == 1) {
			max_speed = absolute_max_speed;
		} else {
			sum /= numcurve;
			max_speed = 232 - (13 - Clamp(sum, 1, 12)) * (13 - Clamp(sum, 1, 12));
		}
	}

	if (max_speed != absolute_max_speed) {
		/* Apply the current railtype's curve speed advantage */
		const RailtypeInfo *rti = GetRailTypeInfo(GetRailType(this->tile));
		max_speed += (max_speed / 2) * rti->curve_speed;

		if (this->tcache.cached_tilt) {
			/* Apply max_speed bonus of 20% for a tilting train */
			max_speed += max_speed / 5;
		}
	}

	return max_speed;
}

/**
 * Calculates the maximum speed of the vehicle under its current conditions.
 * @return Maximum speed of the vehicle.
 */
int Train::GetCurrentMaxSpeed() const
{
	int max_speed = _settings_game.vehicle.train_acceleration_model == AM_ORIGINAL ?
			this->gcache.cached_max_track_speed :
			this->tcache.cached_max_curve_speed;

	if (_settings_game.vehicle.train_acceleration_model == AM_REALISTIC && IsRailStationTile(this->tile)) {
		StationID sid = GetStationIndex(this->tile);
		if (this->current_order.ShouldStopAtStation(this, sid)) {
			int station_ahead;
			int station_length;
			int stop_at = GetTrainStopLocation(sid, this->tile, this, &station_ahead, &station_length);

			/* The distance to go is whatever is still ahead of the train minus the
			 * distance from the train's stop location to the end of the platform */
			int distance_to_go = station_ahead / TILE_SIZE - (station_length - stop_at) / TILE_SIZE;

			if (distance_to_go > 0) {
				int st_max_speed = 120;

				int delta_v = this->cur_speed / (distance_to_go + 1);
				if (max_speed > (this->cur_speed - delta_v)) {
					st_max_speed = this->cur_speed - (delta_v / 10);
				}

				st_max_speed = std::max(st_max_speed, 25 * distance_to_go);
				max_speed = std::min(max_speed, st_max_speed);
			}
		}
	}

	for (const Train *u = this; u != nullptr; u = u->Next()) {
		if (_settings_game.vehicle.train_acceleration_model == AM_REALISTIC && u->track == TRACK_BIT_DEPOT) {
			max_speed = std::min(max_speed, 61);
			break;
		}

		/* Vehicle is on the middle part of a bridge. */
		if (u->track == TRACK_BIT_WORMHOLE && !(u->vehstatus & VS_HIDDEN)) {
			max_speed = std::min<int>(max_speed, GetBridgeSpec(GetBridgeType(u->tile))->speed);
		}
	}

	max_speed = std::min<int>(max_speed, this->current_order.GetMaxSpeed());
	return std::min<int>(max_speed, this->gcache.cached_max_track_speed);
}

/** Update acceleration of the train from the cached power and weight. */
void Train::UpdateAcceleration()
{
	assert(this->IsFrontEngine() || this->IsFreeWagon());

	uint power = this->gcache.cached_power;
	uint weight = this->gcache.cached_weight;
	assert(weight != 0);
	this->acceleration = Clamp(power / weight * 4, 1, 255);
}

/**
 * Get the width of a train vehicle image in the GUI.
 * @param offset Additional offset for positioning the sprite; set to nullptr if not needed
 * @return Width in pixels
 */
int Train::GetDisplayImageWidth(Point *offset) const
{
	int reference_width = TRAININFO_DEFAULT_VEHICLE_WIDTH;
	int vehicle_pitch = 0;

	const Engine *e = this->GetEngine();
	if (e->GetGRF() != nullptr && is_custom_sprite(e->u.rail.image_index)) {
		reference_width = e->GetGRF()->traininfo_vehicle_width;
		vehicle_pitch = e->GetGRF()->traininfo_vehicle_pitch;
	}

	if (offset != nullptr) {
		offset->x = ScaleGUITrad(reference_width) / 2;
		offset->y = ScaleGUITrad(vehicle_pitch);
	}
	return ScaleGUITrad(this->gcache.cached_veh_length * reference_width / VEHICLE_LENGTH);
}

static SpriteID GetDefaultTrainSprite(uint8 spritenum, Direction direction)
{
	assert(IsValidImageIndex<VEH_TRAIN>(spritenum));
	return ((direction + _engine_sprite_add[spritenum]) & _engine_sprite_and[spritenum]) + _engine_sprite_base[spritenum];
}

/**
 * Get the sprite to display the train.
 * @param direction Direction of view/travel.
 * @param image_type Visualisation context.
 * @return Sprite to display.
 */
void Train::GetImage(Direction direction, EngineImageType image_type, VehicleSpriteSeq *result) const
{
	uint8 spritenum = this->spritenum;

	if (HasBit(this->flags, VRF_REVERSE_DIRECTION)) direction = ReverseDir(direction);

	if (is_custom_sprite(spritenum)) {
		GetCustomVehicleSprite(this, (Direction)(direction + 4 * IS_CUSTOM_SECONDHEAD_SPRITE(spritenum)), image_type, result);
		if (result->IsValid()) return;

		spritenum = this->GetEngine()->original_image_index;
	}

	assert(IsValidImageIndex<VEH_TRAIN>(spritenum));
	SpriteID sprite = GetDefaultTrainSprite(spritenum, direction);

	if (this->cargo.StoredCount() >= this->cargo_cap / 2U) sprite += _wagon_full_adder[spritenum];

	result->Set(sprite);
}

static void GetRailIcon(EngineID engine, bool rear_head, int &y, EngineImageType image_type, VehicleSpriteSeq *result)
{
	const Engine *e = Engine::Get(engine);
	Direction dir = rear_head ? DIR_E : DIR_W;
	uint8 spritenum = e->u.rail.image_index;

	if (is_custom_sprite(spritenum)) {
		GetCustomVehicleIcon(engine, dir, image_type, result);
		if (result->IsValid()) {
			if (e->GetGRF() != nullptr) {
				y += ScaleGUITrad(e->GetGRF()->traininfo_vehicle_pitch);
			}
			return;
		}

		spritenum = Engine::Get(engine)->original_image_index;
	}

	if (rear_head) spritenum++;

	result->Set(GetDefaultTrainSprite(spritenum, DIR_W));
}

void DrawTrainEngine(int left, int right, int preferred_x, int y, EngineID engine, PaletteID pal, EngineImageType image_type)
{
	if (RailVehInfo(engine)->railveh_type == RAILVEH_MULTIHEAD) {
		int yf = y;
		int yr = y;

		VehicleSpriteSeq seqf, seqr;
		GetRailIcon(engine, false, yf, image_type, &seqf);
		GetRailIcon(engine, true, yr, image_type, &seqr);

		Rect rectf, rectr;
		seqf.GetBounds(&rectf);
		seqr.GetBounds(&rectr);

		preferred_x = Clamp(preferred_x,
				left - UnScaleGUI(rectf.left) + ScaleGUITrad(14),
				right - UnScaleGUI(rectr.right) - ScaleGUITrad(15));

		seqf.Draw(preferred_x - ScaleGUITrad(14), yf, pal, pal == PALETTE_CRASH);
		seqr.Draw(preferred_x + ScaleGUITrad(15), yr, pal, pal == PALETTE_CRASH);
	} else {
		VehicleSpriteSeq seq;
		GetRailIcon(engine, false, y, image_type, &seq);

		Rect rect;
		seq.GetBounds(&rect);
		preferred_x = Clamp(preferred_x,
				left - UnScaleGUI(rect.left),
				right - UnScaleGUI(rect.right));

		seq.Draw(preferred_x, y, pal, pal == PALETTE_CRASH);
	}
}

/**
 * Get the size of the sprite of a train sprite heading west, or both heads (used for lists).
 * @param engine The engine to get the sprite from.
 * @param[out] width The width of the sprite.
 * @param[out] height The height of the sprite.
 * @param[out] xoffs Number of pixels to shift the sprite to the right.
 * @param[out] yoffs Number of pixels to shift the sprite downwards.
 * @param image_type Context the sprite is used in.
 */
void GetTrainSpriteSize(EngineID engine, uint &width, uint &height, int &xoffs, int &yoffs, EngineImageType image_type)
{
	int y = 0;

	VehicleSpriteSeq seq;
	GetRailIcon(engine, false, y, image_type, &seq);

	Rect rect;
	seq.GetBounds(&rect);

	width  = UnScaleGUI(rect.right - rect.left + 1);
	height = UnScaleGUI(rect.bottom - rect.top + 1);
	xoffs  = UnScaleGUI(rect.left);
	yoffs  = UnScaleGUI(rect.top);

	if (RailVehInfo(engine)->railveh_type == RAILVEH_MULTIHEAD) {
		GetRailIcon(engine, true, y, image_type, &seq);
		seq.GetBounds(&rect);

		/* Calculate values relative to an imaginary center between the two sprites. */
		width = ScaleGUITrad(TRAININFO_DEFAULT_VEHICLE_WIDTH) + UnScaleGUI(rect.right) - xoffs;
		height = std::max<uint>(height, UnScaleGUI(rect.bottom - rect.top + 1));
		xoffs  = xoffs - ScaleGUITrad(TRAININFO_DEFAULT_VEHICLE_WIDTH) / 2;
		yoffs  = std::min(yoffs, UnScaleGUI(rect.top));
	}
}


/**
 * Check if a train chain is compatible with a depot tile.
 * @param tile Tile to check.
 * @param t Train chain to check.
 * @return Whether the full train chain is compatible with this tile.
 */
bool IsVehicleCompatibleWithDepotTile(TileIndex tile, const Train *t)
{
	assert(IsRailDepotTile(tile));
	for (const Train *u = t; u != nullptr; u = u->Next()) {
		RailType rail_type = Engine::Get(u->engine_type)->u.rail.railtype;
		if (!IsCompatibleRail(rail_type, GetRailType(tile))) return false;
	}

	return true;
}

/**
 * Check if a depot has a tile where a train chain can be stored.
 * @param tile A tile of the depot.
 * @param v The train to check.
 * @return True iff the depot has a tile compatible with the chain.
 */
bool HasCompatibleDepotTile(TileIndex tile, const Train *t)
{
	assert(IsRailDepotTile(tile));
	Depot *dep = Depot::GetByTile(tile);

	for (std::vector<TileIndex>::iterator it = dep->depot_tiles.begin(); it != dep->depot_tiles.end(); ++it) {
		if (IsVehicleCompatibleWithDepotTile(*it, t)) return true;
	}

	return false;
}

/**
 * Find a tile of a depot compatible with the rail type of a rail vehicle.
 * @param depot_id Index of the depot.
 * @param rail_type Rail type of the new vehicle.
 * @return A compatible tile of the depot or INVALID_TILE if no compatible tile is found.
 */
TileIndex FindCompatibleDepotTile(DepotID depot_id, RailType rail_type, bool is_engine)
{
	assert(Depot::IsValidID(depot_id));
	Depot *depot = Depot::Get(depot_id);

	for (std::vector<TileIndex>::iterator it = depot->depot_tiles.begin(); it != depot->depot_tiles.end(); ++it) {
		TileIndex dep_tile = *it;
		if (is_engine) {
			if (HasPowerOnRail(rail_type, GetRailType(dep_tile))) return dep_tile;
		} else {
			if (IsCompatibleRail(rail_type, GetRailType(dep_tile))) return dep_tile;
		}
	}

	return INVALID_TILE;
}

/**
 * Build a railroad wagon.
 * @param tile     tile of the depot where rail-vehicle is built.
 * @param flags    type of operation.
 * @param e        the engine to build.
 * @param[out] ret the vehicle that has been built.
 * @return the cost of this operation or an error.
 */
static CommandCost CmdBuildRailWagon(TileIndex tile, DoCommandFlag flags, const Engine *e, Vehicle **ret)
{
	const RailVehicleInfo *rvi = &e->u.rail;
	assert(IsRailDepotTile(tile));
	DepotID depot_id = GetDepotIndex(tile);

	/* Find a good tile to place the wagon. */
	tile = FindCompatibleDepotTile(depot_id, rvi->railtype, false);
	if (tile == INVALID_TILE) return CMD_ERROR;

	if (flags & DC_EXEC) {
		Train *v = new Train();
		*ret = v;
		v->spritenum = rvi->image_index;

		v->engine_type = e->index;
		v->gcache.first_engine = INVALID_ENGINE; // needs to be set before first callback

		DiagDirection dir = GetRailDepotDirection(tile);

		v->direction = DiagDirToDir(dir);
		v->tile = tile;

		int x = TileX(tile) * TILE_SIZE | _vehicle_initial_x_fract[dir];
		int y = TileY(tile) * TILE_SIZE | _vehicle_initial_y_fract[dir];

		v->x_pos = x;
		v->y_pos = y;
		v->z_pos = GetSlopePixelZ(x, y);
		v->owner = _current_company;
		v->track = TRACK_BIT_DEPOT;
		v->vehstatus = VS_HIDDEN | VS_DEFPAL;

		v->SetWagon();

		v->SetFreeWagon();
		InvalidateWindowData(WC_VEHICLE_DEPOT, depot_id);

		v->cargo_type = e->GetDefaultCargoType();
		v->cargo_cap = rvi->capacity;
		v->refit_cap = 0;

		v->railtype = rvi->railtype;

		v->date_of_last_service = _date;
		v->build_year = _cur_year;
		v->sprite_cache.sprite_seq.Set(SPR_IMG_QUERY);
		v->random_bits = VehicleRandomBits();

		v->group_id = DEFAULT_GROUP;

		AddArticulatedParts(v);

		_new_vehicle_id = v->index;

		v->UpdatePosition();
		v->First()->ConsistChanged(CCF_ARRANGE);
		UpdateTrainGroupID(v->First());

		CheckConsistencyOfArticulatedVehicle(v);

		/* Try to connect the vehicle to one of free chains of wagons. */
		for (Train *w : Train::Iterate()) {
			if (!IsRailDepotTile(w->tile)) continue;
			if (GetDepotIndex(w->tile) == depot_id &&       ///< Same depot
					w->IsFreeWagon() &&             ///< A free wagon chain
					w->engine_type == e->index &&   ///< Same type
					w->First() != v &&              ///< Don't connect to ourself
					!(w->vehstatus & VS_CRASHED)) { ///< Not crashed/flooded
				if (DoCommand(0, v->index | 1 << 20, w->Last()->index, DC_EXEC, CMD_MOVE_RAIL_VEHICLE).Succeeded()) {
					break;
				}
			}
		}
	}

	return CommandCost();
}

/** Move all free vehicles in the depot to the train */
static void NormalizeTrainVehInDepot(const Train *u)
{
	DepotID dep_id = GetDepotIndex(u->tile);
	for (const Train *v : Train::Iterate()) {
		if (v->IsFreeWagon() && v->IsInDepot() &&
				GetDepotIndex(v->tile) == dep_id) {
			if (DoCommand(0, v->index | 1 << 20, u->index, DC_EXEC,
					CMD_MOVE_RAIL_VEHICLE).Failed())
				break;
		}
	}
}

static void AddRearEngineToMultiheadedTrain(Train *v)
{
	Train *u = new Train();
	v->value >>= 1;
	u->value = v->value;
	u->direction = v->direction;
	u->owner = v->owner;
	u->tile = v->tile;
	u->x_pos = v->x_pos;
	u->y_pos = v->y_pos;
	u->z_pos = v->z_pos;
	u->track = TRACK_BIT_DEPOT;
	u->vehstatus = v->vehstatus & ~VS_STOPPED;
	u->spritenum = v->spritenum + 1;
	u->cargo_type = v->cargo_type;
	u->cargo_subtype = v->cargo_subtype;
	u->cargo_cap = v->cargo_cap;
	u->refit_cap = v->refit_cap;
	u->railtype = v->railtype;
	u->engine_type = v->engine_type;
	u->date_of_last_service = v->date_of_last_service;
	u->build_year = v->build_year;
	u->sprite_cache.sprite_seq.Set(SPR_IMG_QUERY);
	u->random_bits = VehicleRandomBits();
	v->SetMultiheaded();
	u->SetMultiheaded();
	v->SetNext(u);
	u->UpdatePosition();

	/* Now we need to link the front and rear engines together */
	v->other_multiheaded_part = u;
	u->other_multiheaded_part = v;
}

/**
 * Build a railroad vehicle.
 * @param tile     tile of the depot where rail-vehicle is built.
 * @param flags    type of operation.
 * @param e        the engine to build.
 * @param data     bit 0 prevents any free cars from being added to the train.
 * @param[out] ret the vehicle that has been built.
 * @return the cost of this operation or an error.
 */
CommandCost CmdBuildRailVehicle(TileIndex tile, DoCommandFlag flags, const Engine *e, uint16 data, Vehicle **ret)
{
	assert(IsRailDepotTile(tile));
	const RailVehicleInfo *rvi = &e->u.rail;

	if (rvi->railveh_type == RAILVEH_WAGON) return CmdBuildRailWagon(tile, flags, e, ret);

	/* Find a good tile to place the engine and get power on it. */
	tile = FindCompatibleDepotTile(GetDepotIndex(tile), rvi->railtype, true);
	if (tile == INVALID_TILE) return CMD_ERROR;

	if (flags & DC_EXEC) {
		DiagDirection dir = GetRailDepotDirection(tile);
		int x = TileX(tile) * TILE_SIZE + _vehicle_initial_x_fract[dir];
		int y = TileY(tile) * TILE_SIZE + _vehicle_initial_y_fract[dir];

		Train *v = new Train();
		*ret = v;
		v->direction = DiagDirToDir(dir);
		v->tile = tile;
		v->owner = _current_company;
		v->x_pos = x;
		v->y_pos = y;
		v->z_pos = GetSlopePixelZ(x, y);
		v->track = TRACK_BIT_DEPOT;
		v->vehstatus = VS_HIDDEN | VS_STOPPED | VS_DEFPAL;
		v->spritenum = rvi->image_index;
		v->cargo_type = e->GetDefaultCargoType();
		v->cargo_cap = rvi->capacity;
		v->refit_cap = 0;
		v->last_station_visited = INVALID_STATION;
		v->last_loading_station = INVALID_STATION;

		v->engine_type = e->index;
		v->gcache.first_engine = INVALID_ENGINE; // needs to be set before first callback

		v->reliability = e->reliability;
		v->reliability_spd_dec = e->reliability_spd_dec;
		v->max_age = e->GetLifeLengthInDays();

		v->railtype = rvi->railtype;
		_new_vehicle_id = v->index;

		v->SetServiceInterval(Company::Get(_current_company)->settings.vehicle.servint_trains);
		v->date_of_last_service = _date;
		v->build_year = _cur_year;
		v->sprite_cache.sprite_seq.Set(SPR_IMG_QUERY);
		v->random_bits = VehicleRandomBits();

		if (e->flags & ENGINE_EXCLUSIVE_PREVIEW) SetBit(v->vehicle_flags, VF_BUILT_AS_PROTOTYPE);
		v->SetServiceIntervalIsPercent(Company::Get(_current_company)->settings.vehicle.servint_ispercent);

		v->group_id = DEFAULT_GROUP;

		v->SetFrontEngine();
		v->SetEngine();

		v->UpdatePosition();

		if (rvi->railveh_type == RAILVEH_MULTIHEAD) {
			AddRearEngineToMultiheadedTrain(v);
		} else {
			AddArticulatedParts(v);
		}

		v->ConsistChanged(CCF_ARRANGE);
		UpdateTrainGroupID(v);

		if (!HasBit(data, 0) && !(flags & DC_AUTOREPLACE)) { // check if the cars should be added to the new vehicle
			NormalizeTrainVehInDepot(v);
		}

		CheckConsistencyOfArticulatedVehicle(v);

		TrainPlacement train_placement;
		train_placement.LiftTrain(v, flags);
		train_placement.PlaceTrain(v, flags);
	}

	return CommandCost();
}

static Train *FindGoodVehiclePos(const Train *src)
{
	EngineID eng = src->engine_type;
	DepotID dep_id = GetDepotIndex(src->tile);

	for (Train *dst : Train::Iterate()) {
		if (dst->IsFreeWagon() && !(dst->vehstatus & VS_CRASHED) && GetDepotIndex(dst->tile) == dep_id) {
			/* check so all vehicles in the line have the same engine. */
			Train *t = dst;
			while (t->engine_type == eng) {
				t = t->Next();
				if (t == nullptr) return dst;
			}
		}
	}

	return nullptr;
}

/** Helper type for lists/vectors of trains */
typedef std::vector<Train *> TrainList;

/**
 * Make a backup of a train into a train list.
 * @param list to make the backup in
 * @param t    the train to make the backup of
 */
static void MakeTrainBackup(TrainList &list, Train *t)
{
	for (; t != nullptr; t = t->Next()) list.push_back(t);
}

/**
 * Restore the train from the backup list.
 * @param list the train to restore.
 */
static void RestoreTrainBackup(TrainList &list)
{
	/* No train, nothing to do. */
	if (list.size() == 0) return;

	Train *prev = nullptr;
	/* Iterate over the list and rebuild it. */
	for (Train *t : list) {
		if (prev != nullptr) {
			prev->SetNext(t);
		} else if (t->Previous() != nullptr) {
			/* Make sure the head of the train is always the first in the chain. */
			t->Previous()->SetNext(nullptr);
		}
		prev = t;
	}
}

/**
 * Remove the given wagon from its consist.
 * @param part the part of the train to remove.
 * @param chain whether to remove the whole chain.
 */
static void RemoveFromConsist(Train *part, bool chain = false)
{
	Train *tail = chain ? part->Last() : part->GetLastEnginePart();

	/* Unlink at the front, but make it point to the next
	 * vehicle after the to be remove part. */
	if (part->Previous() != nullptr) part->Previous()->SetNext(tail->Next());

	/* Unlink at the back */
	tail->SetNext(nullptr);
}

/**
 * Inserts a chain into the train at dst.
 * @param dst   the place where to append after.
 * @param chain the chain to actually add.
 */
static void InsertInConsist(Train *dst, Train *chain)
{
	/* We do not want to add something in the middle of an articulated part. */
	assert(dst != nullptr && (dst->Next() == nullptr || !dst->Next()->IsArticulatedPart()));

	chain->Last()->SetNext(dst->Next());
	dst->SetNext(chain);
}

/**
 * Normalise the dual heads in the train, i.e. if one is
 * missing move that one to this train.
 * @param t the train to normalise.
 */
static void NormaliseDualHeads(Train *t)
{
	for (; t != nullptr; t = t->GetNextVehicle()) {
		if (!t->IsMultiheaded() || !t->IsEngine()) continue;

		/* Make sure that there are no free cars before next engine */
		Train *u;
		for (u = t; u->Next() != nullptr && !u->Next()->IsEngine(); u = u->Next()) {}

		if (u == t->other_multiheaded_part) continue;

		/* Remove the part from the 'wrong' train */
		RemoveFromConsist(t->other_multiheaded_part);
		/* And add it to the 'right' train */
		InsertInConsist(u, t->other_multiheaded_part);
	}
}

/**
 * Normalise the sub types of the parts in this chain.
 * @param chain the chain to normalise.
 */
static void NormaliseSubtypes(Train *chain)
{
	/* Nothing to do */
	if (chain == nullptr) return;

	/* We must be the first in the chain. */
	assert(chain->Previous() == nullptr);

	/* Set the appropriate bits for the first in the chain. */
	if (chain->IsWagon()) {
		chain->SetFreeWagon();
	} else {
		assert(chain->IsEngine());
		chain->SetFrontEngine();
	}

	/* Now clear the bits for the rest of the chain */
	for (Train *t = chain->Next(); t != nullptr; t = t->Next()) {
		t->ClearFreeWagon();
		t->ClearFrontEngine();
	}
}

/**
 * Check/validate whether we may actually build a new train.
 * @note All vehicles are/were 'heads' of their chains.
 * @param original_dst The original destination chain.
 * @param dst          The destination chain after constructing the train.
 * @param original_src The original source chain.
 * @param src          The source chain after constructing the train.
 * @return possible error of this command.
 */
static CommandCost CheckNewTrain(Train *original_dst, Train *dst, Train *original_src, Train *src)
{
	/* Just add 'new' engines and subtract the original ones.
	 * If that's less than or equal to 0 we can be sure we did
	 * not add any engines (read: trains) along the way. */
	if ((src          != nullptr && src->IsEngine()          ? 1 : 0) +
			(dst          != nullptr && dst->IsEngine()          ? 1 : 0) -
			(original_src != nullptr && original_src->IsEngine() ? 1 : 0) -
			(original_dst != nullptr && original_dst->IsEngine() ? 1 : 0) <= 0) {
		return CommandCost();
	}

	/* Get a free unit number and check whether it's within the bounds.
	 * There will always be a maximum of one new train. */
	if (GetFreeUnitNumber(VEH_TRAIN) <= _settings_game.vehicle.max_trains) return CommandCost();

	return_cmd_error(STR_ERROR_TOO_MANY_VEHICLES_IN_GAME);
}

/**
 * Check whether the train parts can be attached.
 * @param t the train to check
 * @return possible error of this command.
 */
static CommandCost CheckTrainAttachment(Train *t)
{
	/* No multi-part train, no need to check. */
	if (t == nullptr || t->Next() == nullptr) return CommandCost();

	/* The maximum length for a train. For each part we decrease this by one
	 * and if the result is negative the train is simply too long. */
	int allowed_len = _settings_game.vehicle.max_train_length * TILE_SIZE - t->gcache.cached_veh_length;

	/* For free-wagon chains, check if they are within the max_train_length limit. */
	if (!t->IsEngine()) {
		t = t->Next();
		while (t != nullptr) {
			allowed_len -= t->gcache.cached_veh_length;

			t = t->Next();
		}

		if (allowed_len < 0) return_cmd_error(STR_ERROR_TRAIN_TOO_LONG);
		return CommandCost();
	}

	Train *head = t;
	Train *prev = t;

	/* Break the prev -> t link so it always holds within the loop. */
	t = t->Next();
	prev->SetNext(nullptr);

	/* Make sure the cache is cleared. */
	head->InvalidateNewGRFCache();

	while (t != nullptr) {
		allowed_len -= t->gcache.cached_veh_length;

		Train *next = t->Next();

		/* Unlink the to-be-added piece; it is already unlinked from the previous
		 * part due to the fact that the prev -> t link is broken. */
		t->SetNext(nullptr);

		/* Don't check callback for articulated or rear dual headed parts */
		if (!t->IsArticulatedPart() && !t->IsRearDualheaded()) {
			/* Back up and clear the first_engine data to avoid using wagon override group */
			EngineID first_engine = t->gcache.first_engine;
			t->gcache.first_engine = INVALID_ENGINE;

			/* We don't want the cache to interfere. head's cache is cleared before
			 * the loop and after each callback does not need to be cleared here. */
			t->InvalidateNewGRFCache();

			uint16 callback = GetVehicleCallbackParent(CBID_TRAIN_ALLOW_WAGON_ATTACH, 0, 0, head->engine_type, t, head);

			/* Restore original first_engine data */
			t->gcache.first_engine = first_engine;

			/* We do not want to remember any cached variables from the test run */
			t->InvalidateNewGRFCache();
			head->InvalidateNewGRFCache();

			if (callback != CALLBACK_FAILED) {
				/* A failing callback means everything is okay */
				StringID error = STR_NULL;

				if (head->GetGRF()->grf_version < 8) {
					if (callback == 0xFD) error = STR_ERROR_INCOMPATIBLE_RAIL_TYPES;
					if (callback  < 0xFD) error = GetGRFStringID(head->GetGRFID(), 0xD000 + callback);
					if (callback >= 0x100) ErrorUnknownCallbackResult(head->GetGRFID(), CBID_TRAIN_ALLOW_WAGON_ATTACH, callback);
				} else {
					if (callback < 0x400) {
						error = GetGRFStringID(head->GetGRFID(), 0xD000 + callback);
					} else {
						switch (callback) {
							case 0x400: // allow if railtypes match (always the case for OpenTTD)
							case 0x401: // allow
								break;

							default:    // unknown reason -> disallow
							case 0x402: // disallow attaching
								error = STR_ERROR_INCOMPATIBLE_RAIL_TYPES;
								break;
						}
					}
				}

				if (error != STR_NULL) return_cmd_error(error);
			}
		}

		/* And link it to the new part. */
		prev->SetNext(t);
		prev = t;
		t = next;
	}

	if (allowed_len < 0) return_cmd_error(STR_ERROR_TRAIN_TOO_LONG);
	return CommandCost();
}

/**
 * Validate whether we are going to create valid trains.
 * @note All vehicles are/were 'heads' of their chains.
 * @param original_dst The original destination chain.
 * @param dst          The destination chain after constructing the train.
 * @param original_src The original source chain.
 * @param src          The source chain after constructing the train.
 * @param check_limit  Whether to check the vehicle limit.
 * @return possible error of this command.
 */
static CommandCost ValidateTrains(Train *original_dst, Train *dst, Train *original_src, Train *src, bool check_limit)
{
	/* Check whether we may actually construct the trains. */
	CommandCost ret = CheckTrainAttachment(src);
	if (ret.Failed()) return ret;
	ret = CheckTrainAttachment(dst);
	if (ret.Failed()) return ret;

	/* Check whether we need to build a new train. */
	return check_limit ? CheckNewTrain(original_dst, dst, original_src, src) : CommandCost();
}

/**
 * Arrange the trains in the wanted way.
 * @param dst_head   The destination chain of the to be moved vehicle.
 * @param dst        The destination for the to be moved vehicle.
 * @param src_head   The source chain of the to be moved vehicle.
 * @param src        The to be moved vehicle.
 * @param move_chain Whether to move all vehicles after src or not.
 */
static void ArrangeTrains(Train **dst_head, Train *dst, Train **src_head, Train *src, bool move_chain)
{
	/* First determine the front of the two resulting trains */
	if (*src_head == *dst_head) {
		/* If we aren't moving part(s) to a new train, we are just moving the
		 * front back and there is not destination head. */
		*dst_head = nullptr;
	} else if (*dst_head == nullptr) {
		/* If we are moving to a new train the head of the move train would become
		 * the head of the new vehicle. */
		*dst_head = src;
	}

	if (src == *src_head) {
		/* If we are moving the front of a train then we are, in effect, creating
		 * a new head for the train. Point to that. Unless we are moving the whole
		 * train in which case there is not 'source' train anymore.
		 * In case we are a multiheaded part we want the complete thing to come
		 * with us, so src->GetNextUnit(), however... when we are e.g. a wagon
		 * that is followed by a rear multihead we do not want to include that. */
		*src_head = move_chain ? nullptr :
				(src->IsMultiheaded() ? src->GetNextUnit() : src->GetNextVehicle());
	}

	/* Now it's just simply removing the part that we are going to move from the
	 * source train and *if* the destination is a not a new train add the chain
	 * at the destination location. */
	RemoveFromConsist(src, move_chain);
	if (*dst_head != src) InsertInConsist(dst, src);

	/* Now normalise the dual heads, that is move the dual heads around in such
	 * a way that the head and rear of a dual head are in the same train */
	NormaliseDualHeads(*src_head);
	NormaliseDualHeads(*dst_head);
}

/**
 * Normalise the head of the train again, i.e. that is tell the world that
 * we have changed and update all kinds of variables.
 * @param head the train to update.
 */
static void NormaliseTrainHead(Train *head)
{
	/* Not much to do! */
	if (head == nullptr) return;

	/* Tell the 'world' the train changed. */
	head->ConsistChanged(CCF_ARRANGE);
	UpdateTrainGroupID(head);

	/* Not a front engine, i.e. a free wagon chain. No need to do more. */
	if (!head->IsFrontEngine()) return;

	/* Update the refit button and window */
	InvalidateWindowData(WC_VEHICLE_REFIT, head->index, VIWD_CONSIST_CHANGED);
	SetWindowWidgetDirty(WC_VEHICLE_VIEW, head->index, WID_VV_REFIT);

	/* If we don't have a unit number yet, set one. */
	if (head->unitnumber != 0) return;
	head->unitnumber = GetFreeUnitNumber(VEH_TRAIN);
}

/**
 * Move a rail vehicle around inside the depot.
 * @param tile unused
 * @param flags type of operation
 *              Note: DC_AUTOREPLACE is set when autoreplace tries to undo its modifications or moves vehicles to temporary locations inside the depot.
 * @param p1 various bitstuffed elements
 * - p1 (bit  0 - 19) source vehicle index
 * - p1 (bit      20) move all vehicles following the source vehicle
 * @param p2 what wagon to put the source wagon AFTER, XXX - INVALID_VEHICLE to make a new line
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdMoveRailVehicle(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const std::string &text)
{
	VehicleID s = GB(p1, 0, 20);
	VehicleID d = GB(p2, 0, 20);
	bool move_chain = HasBit(p1, 20);

	Train *src = Train::GetIfValid(s);
	if (src == nullptr) return CMD_ERROR;

	CommandCost ret = CheckOwnership(src->owner);
	if (ret.Failed()) return ret;

	/* Do not allow moving crashed vehicles inside the depot, it is likely to cause asserts later */
	if (src->vehstatus & VS_CRASHED) return CMD_ERROR;

	/* if nothing is selected as destination, try and find a matching vehicle to drag to. */
	Train *dst;
	if (d == INVALID_VEHICLE) {
		dst = (src->IsEngine() || (flags & DC_AUTOREPLACE)) ? nullptr : FindGoodVehiclePos(src);
	} else {
		dst = Train::GetIfValid(d);
		if (dst == nullptr) return CMD_ERROR;

		CommandCost ret = CheckOwnership(dst->owner);
		if (ret.Failed()) return ret;

		/* Do not allow appending to crashed vehicles, too */
		if (dst->vehstatus & VS_CRASHED) return CMD_ERROR;
	}

	/* if an articulated part is being handled, deal with its parent vehicle */
	src = src->GetFirstEnginePart();
	if (dst != nullptr) {
		dst = dst->GetFirstEnginePart();
	}

	/* don't move the same vehicle.. */
	if (src == dst) return CommandCost();

	/* locate the head of the two chains */
	Train *src_head = src->First();
	Train *dst_head;
	if (dst != nullptr) {
		dst_head = dst->First();
		if (GetDepotIndex(dst_head->tile) != GetDepotIndex(src_head->tile)) return CMD_ERROR;
		/* Now deal with articulated part of destination wagon */
		dst = dst->GetLastEnginePart();
	} else {
		dst_head = nullptr;
	}

	if (src->IsRearDualheaded()) return_cmd_error(STR_ERROR_REAR_ENGINE_FOLLOW_FRONT);

	/* When moving all wagons, we can't have the same src_head and dst_head */
	if (move_chain && src_head == dst_head) return CommandCost();

	/* When moving a multiheaded part to be place after itself, bail out. */
	if (!move_chain && dst != nullptr && dst->IsRearDualheaded() && src == dst->other_multiheaded_part) return CommandCost();

	/* Check if all vehicles in the source train are stopped inside a depot. */
	if (!src_head->IsStoppedInDepot()) return_cmd_error(STR_ERROR_TRAINS_CAN_ONLY_BE_ALTERED_INSIDE_A_DEPOT);

	/* Check if all vehicles in the destination train are stopped inside a depot. */
	if (dst_head != nullptr && !dst_head->IsStoppedInDepot()) return_cmd_error(STR_ERROR_TRAINS_CAN_ONLY_BE_ALTERED_INSIDE_A_DEPOT);

	/* First make a backup of the order of the trains. That way we can do
	 * whatever we want with the order and later on easily revert. */
	TrainList original_src;
	TrainList original_dst;

	MakeTrainBackup(original_src, src_head);
	MakeTrainBackup(original_dst, dst_head);

	/* Also make backup of the original heads as ArrangeTrains can change them.
	 * For the destination head we do not care if it is the same as the source
	 * head because in that case it's just a copy. */
	Train *original_src_head = src_head;
	Train *original_dst_head = (dst_head == src_head ? nullptr : dst_head);

	/* We want this information from before the rearrangement, but execute this after the validation.
	 * original_src_head can't be nullptr; src is by definition != nullptr, so src_head can't be nullptr as
	 * src->GetFirst() always yields non-nullptr, so eventually original_src_head != nullptr as well. */
	bool original_src_head_front_engine = original_src_head->IsFrontEngine();
	bool original_dst_head_front_engine = original_dst_head != nullptr && original_dst_head->IsFrontEngine();

	TrainPlacement train_placement_src;
	TrainPlacement train_placement_dst;
	train_placement_src.LiftTrain(src_head, flags);
	train_placement_dst.LiftTrain(dst_head, flags);

	assert(src_head != nullptr);

	/* (Re)arrange the trains in the wanted arrangement. */
	ArrangeTrains(&dst_head, dst, &src_head, src, move_chain);

	if ((flags & DC_AUTOREPLACE) == 0) {
		/* If the autoreplace flag is set we do not need to test for the validity
		 * because we are going to revert the train to its original state. As we
		 * assume the original state was correct autoreplace can skip this. */
		CommandCost ret = ValidateTrains(original_dst_head, dst_head, original_src_head, src_head, true);
		if (ret.Failed()) {
			/* Restore the train we had. */
			RestoreTrainBackup(original_src);
			RestoreTrainBackup(original_dst);
			train_placement_src.PlaceTrain(original_src_head, flags & ~DC_EXEC);
			if (src_head != dst_head) train_placement_dst.PlaceTrain(original_dst_head, flags & ~DC_EXEC);
			return ret;
		}
	}

	/* do it? */
	if (flags & DC_EXEC) {
		/* Remove old heads from the statistics */
		if (original_src_head_front_engine) GroupStatistics::CountVehicle(original_src_head, -1);
		if (original_dst_head_front_engine) GroupStatistics::CountVehicle(original_dst_head, -1);

		/* First normalise the sub types of the chains. */
		NormaliseSubtypes(src_head);
		NormaliseSubtypes(dst_head);

		/* There are 14 different cases:
		 *  1) front engine gets moved to a new train, it stays a front engine.
		 *     a) the 'next' part is a wagon that becomes a free wagon chain.
		 *     b) the 'next' part is an engine that becomes a front engine.
		 *     c) there is no 'next' part, nothing else happens
		 *  2) front engine gets moved to another train, it is not a front engine anymore
		 *     a) the 'next' part is a wagon that becomes a free wagon chain.
		 *     b) the 'next' part is an engine that becomes a front engine.
		 *     c) there is no 'next' part, nothing else happens
		 *  3) front engine gets moved to later in the current train, it is not a front engine anymore.
		 *     a) the 'next' part is a wagon that becomes a free wagon chain.
		 *     b) the 'next' part is an engine that becomes a front engine.
		 *  4) free wagon gets moved
		 *     a) the 'next' part is a wagon that becomes a free wagon chain.
		 *     b) the 'next' part is an engine that becomes a front engine.
		 *     c) there is no 'next' part, nothing else happens
		 *  5) non front engine gets moved and becomes a new train, nothing else happens
		 *  6) non front engine gets moved within a train / to another train, nothing happens
		 *  7) wagon gets moved, nothing happens
		 */
		if (src == original_src_head && src->IsEngine() && !src->IsFrontEngine()) {
			/* Cases #2 and #3: the front engine gets trashed. */
			CloseWindowById(WC_VEHICLE_VIEW, src->index);
			CloseWindowById(WC_VEHICLE_ORDERS, src->index);
			CloseWindowById(WC_VEHICLE_REFIT, src->index);
			CloseWindowById(WC_VEHICLE_DETAILS, src->index);
			CloseWindowById(WC_VEHICLE_TIMETABLE, src->index);
			DeleteNewGRFInspectWindow(GSF_TRAINS, src->index);
			SetWindowDirty(WC_COMPANY, _current_company);

			/* Delete orders, group stuff and the unit number as we're not the
			 * front of any vehicle anymore. */
			DeleteVehicleOrders(src);
			RemoveVehicleFromGroup(src);
			src->unitnumber = 0;
		}

		/* We weren't a front engine but are becoming one. So
		 * we should be put in the default group. */
		if (original_src_head != src && dst_head == src) {
			SetTrainGroupID(src, DEFAULT_GROUP);
			SetWindowDirty(WC_COMPANY, _current_company);
		}

		/* Add new heads to statistics */
		if (src_head != nullptr && src_head->IsFrontEngine()) GroupStatistics::CountVehicle(src_head, 1);
		if (dst_head != nullptr && dst_head->IsFrontEngine()) GroupStatistics::CountVehicle(dst_head, 1);

		/* Handle 'new engine' part of cases #1b, #2b, #3b, #4b and #5 in NormaliseTrainHead. */
		NormaliseTrainHead(src_head);
		NormaliseTrainHead(dst_head);

		if ((flags & DC_NO_CARGO_CAP_CHECK) == 0) {
			CheckCargoCapacity(src_head);
			CheckCargoCapacity(dst_head);
		}

		if (src_head != nullptr) src_head->First()->MarkDirty();
		if (dst_head != nullptr) dst_head->First()->MarkDirty();

		bool reverse_emplacement_order = !train_placement_src.placed && train_placement_dst.placed;

		if (!reverse_emplacement_order) train_placement_src.PlaceTrain(src_head, flags);
		if (src_head != dst_head) train_placement_dst.PlaceTrain(dst_head, flags);
		if (reverse_emplacement_order) train_placement_src.PlaceTrain(src_head, flags);

		/* We are undoubtedly changing something in the depot and train list. */
		InvalidateWindowData(WC_VEHICLE_DEPOT, GetDepotIndex(src->tile));
		InvalidateWindowClassesData(WC_TRAINS_LIST, 0);
	} else {
		/* We don't want to execute what we're just tried. */
		RestoreTrainBackup(original_src);
		RestoreTrainBackup(original_dst);
		train_placement_src.PlaceTrain(original_src_head, flags);
		if (src_head != dst_head) train_placement_dst.PlaceTrain(original_dst_head, flags);
	}

	return CommandCost();
}

/**
 * Sell a (single) train wagon/engine.
 * @param flags type of operation
 * @param t     the train wagon to sell
 * @param data  the selling mode
 * - data = 0: only sell the single dragged wagon/engine (and any belonging rear-engines)
 * - data = 1: sell the vehicle and all vehicles following it in the chain
 *             if the wagon is dragged, don't delete the possibly belonging rear-engine to some front
 * @param user  the user for the order backup.
 * @return the cost of this operation or an error
 */
CommandCost CmdSellRailWagon(DoCommandFlag flags, Vehicle *t, uint16 data, uint32 user)
{
	/* Sell a chain of vehicles or not? */
	bool sell_chain = HasBit(data, 0);

	Train *v = Train::From(t)->GetFirstEnginePart();
	Train *first = v->First();

	if (v->IsRearDualheaded()) return_cmd_error(STR_ERROR_REAR_ENGINE_FOLLOW_FRONT);

	TrainPlacement train_placement;
	train_placement.LiftTrain(first, flags);

	/* First make a backup of the order of the train. That way we can do
	 * whatever we want with the order and later on easily revert. */
	TrainList original;
	MakeTrainBackup(original, first);

	/* We need to keep track of the new head and the head of what we're going to sell. */
	Train *new_head = first;
	Train *sell_head = nullptr;

	/* Split the train in the wanted way. */
	ArrangeTrains(&sell_head, nullptr, &new_head, v, sell_chain);

	/* We don't need to validate the second train; it's going to be sold. */
	CommandCost ret = ValidateTrains(nullptr, nullptr, first, new_head, (flags & DC_AUTOREPLACE) == 0);
	if (ret.Failed()) {
		/* Restore the train we had. */
		RestoreTrainBackup(original);
		train_placement.PlaceTrain(first, flags);
		return ret;
	}

	if (first->orders.list == nullptr && !OrderList::CanAllocateItem()) {
		/* Restore the train we had. */
		RestoreTrainBackup(original);
		train_placement.PlaceTrain(first, flags);
		return_cmd_error(STR_ERROR_NO_MORE_SPACE_FOR_ORDERS);
	}

	CommandCost cost(EXPENSES_NEW_VEHICLES);
	for (Train *part = sell_head; part != nullptr; part = part->Next()) cost.AddCost(-part->value);

	/* do it? */
	if (flags & DC_EXEC) {
		/* First normalise the sub types of the chain. */
		NormaliseSubtypes(new_head);

		if (v == first && v->IsEngine() && !sell_chain && new_head != nullptr && new_head->IsFrontEngine()) {
			/* We are selling the front engine. In this case we want to
			 * 'give' the order, unit number and such to the new head. */
			new_head->orders.list = first->orders.list;
			new_head->AddToShared(first);
			DeleteVehicleOrders(first);

			/* Copy other important data from the front engine */
			new_head->CopyVehicleConfigAndStatistics(first);
			GroupStatistics::CountVehicle(new_head, 1); // after copying over the profit
		} else if (v->IsPrimaryVehicle() && data & (MAKE_ORDER_BACKUP_FLAG >> 20)) {
			OrderBackup::Backup(v, user);
		}

		/* We need to update the information about the train. */
		NormaliseTrainHead(new_head);
		train_placement.PlaceTrain(new_head, flags);

		/* We are undoubtedly changing something in the depot and train list. */
		InvalidateWindowData(WC_VEHICLE_DEPOT, GetDepotIndex(v->tile));
		InvalidateWindowClassesData(WC_TRAINS_LIST, 0);

		/* Actually delete the sold 'goods' */
		delete sell_head;
	} else {
		/* We don't want to execute what we have just tried. */
		RestoreTrainBackup(original);
		train_placement.PlaceTrain(first, flags);
	}

	return cost;
}

void Train::UpdateDeltaXY()
{
	/* Set common defaults. */
	this->x_offs    = -1;
	this->y_offs    = -1;
	this->x_extent  =  3;
	this->y_extent  =  3;
	this->z_extent  =  6;
	this->x_bb_offs =  0;
	this->y_bb_offs =  0;

	if (!IsDiagonalDirection(this->direction)) {
		static const int _sign_table[] =
		{
			/* x, y */
			-1, -1, // DIR_N
			-1,  1, // DIR_E
			 1,  1, // DIR_S
			 1, -1, // DIR_W
		};

		int half_shorten = (VEHICLE_LENGTH - this->gcache.cached_veh_length) / 2;

		/* For all straight directions, move the bound box to the centre of the vehicle, but keep the size. */
		this->x_offs -= half_shorten * _sign_table[this->direction];
		this->y_offs -= half_shorten * _sign_table[this->direction + 1];
		this->x_extent += this->x_bb_offs = half_shorten * _sign_table[direction];
		this->y_extent += this->y_bb_offs = half_shorten * _sign_table[direction + 1];
	} else {
		switch (this->direction) {
				/* Shorten southern corner of the bounding box according the vehicle length
				 * and center the bounding box on the vehicle. */
			case DIR_NE:
				this->x_offs    = 1 - (this->gcache.cached_veh_length + 1) / 2;
				this->x_extent  = this->gcache.cached_veh_length - 1;
				this->x_bb_offs = -1;
				break;

			case DIR_NW:
				this->y_offs    = 1 - (this->gcache.cached_veh_length + 1) / 2;
				this->y_extent  = this->gcache.cached_veh_length - 1;
				this->y_bb_offs = -1;
				break;

				/* Move northern corner of the bounding box down according to vehicle length
				 * and center the bounding box on the vehicle. */
			case DIR_SW:
				this->x_offs    = 1 + (this->gcache.cached_veh_length + 1) / 2 - VEHICLE_LENGTH;
				this->x_extent  = VEHICLE_LENGTH - 1;
				this->x_bb_offs = VEHICLE_LENGTH - this->gcache.cached_veh_length - 1;
				break;

			case DIR_SE:
				this->y_offs    = 1 + (this->gcache.cached_veh_length + 1) / 2 - VEHICLE_LENGTH;
				this->y_extent  = VEHICLE_LENGTH - 1;
				this->y_bb_offs = VEHICLE_LENGTH - this->gcache.cached_veh_length - 1;
				break;

			default:
				NOT_REACHED();
		}
	}
}

/**
 * Mark a train as stuck and stop it if it isn't stopped right now.
 * @param v %Train to mark as being stuck.
 */
static void MarkTrainAsStuck(Train *v)
{
	if (!HasBit(v->flags, VRF_TRAIN_STUCK)) {
		/* It is the first time the problem occurred, set the "train stuck" flag. */
		SetBit(v->flags, VRF_TRAIN_STUCK);

		v->wait_counter = 0;

		/* Stop train */
		v->cur_speed = 0;
		v->subspeed = 0;
		v->SetLastSpeed();

		SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
	}
}

/**
 * Swap the two up/down flags in two ways:
 * - Swap values of \a swap_flag1 and \a swap_flag2, and
 * - If going up previously (#GVF_GOINGUP_BIT set), the #GVF_GOINGDOWN_BIT is set, and vice versa.
 * @param[in,out] swap_flag1 First train flag.
 * @param[in,out] swap_flag2 Second train flag.
 */
static void SwapTrainFlags(uint16 *swap_flag1, uint16 *swap_flag2)
{
	uint16 flag1 = *swap_flag1;
	uint16 flag2 = *swap_flag2;

	/* Clear the flags */
	ClrBit(*swap_flag1, GVF_GOINGUP_BIT);
	ClrBit(*swap_flag1, GVF_GOINGDOWN_BIT);
	ClrBit(*swap_flag2, GVF_GOINGUP_BIT);
	ClrBit(*swap_flag2, GVF_GOINGDOWN_BIT);

	/* Reverse the rail-flags (if needed) */
	if (HasBit(flag1, GVF_GOINGUP_BIT)) {
		SetBit(*swap_flag2, GVF_GOINGDOWN_BIT);
	} else if (HasBit(flag1, GVF_GOINGDOWN_BIT)) {
		SetBit(*swap_flag2, GVF_GOINGUP_BIT);
	}
	if (HasBit(flag2, GVF_GOINGUP_BIT)) {
		SetBit(*swap_flag1, GVF_GOINGDOWN_BIT);
	} else if (HasBit(flag2, GVF_GOINGDOWN_BIT)) {
		SetBit(*swap_flag1, GVF_GOINGUP_BIT);
	}
}

/**
 * Updates some variables after swapping the vehicle.
 * @param v swapped vehicle
 */
static void UpdateStatusAfterSwap(Train *v)
{
	/* Reverse the direction. */
	if (v->track != TRACK_BIT_DEPOT) v->direction = ReverseDir(v->direction);

	/* Call the proper EnterTile function unless we are in a wormhole. */
	if (v->track != TRACK_BIT_WORMHOLE) {
		VehicleEnterTile(v, v->tile, v->x_pos, v->y_pos);
	} else {
		/* VehicleEnter_TunnelBridge() sets TRACK_BIT_WORMHOLE when the vehicle
		 * is on the last bit of the bridge head (frame == TILE_SIZE - 1).
		 * If we were swapped with such a vehicle, we have set TRACK_BIT_WORMHOLE,
		 * when we shouldn't have. Check if this is the case. */
		TileIndex vt = TileVirtXY(v->x_pos, v->y_pos);
		if (IsTileType(vt, MP_TUNNELBRIDGE)) {
			VehicleEnterTile(v, vt, v->x_pos, v->y_pos);
			if (v->track != TRACK_BIT_WORMHOLE && IsBridgeTile(v->tile)) {
				/* We have just left the wormhole, possibly set the
				 * "goingdown" bit. UpdateInclination() can be used
				 * because we are at the border of the tile. */
				v->UpdatePosition();
				v->UpdateInclination(true, true);
				return;
			}
		}
	}

	v->UpdatePosition();
	v->UpdateViewport(true, true);
}

/**
 * Swap vehicles \a l and \a r in consist \a v, and reverse their direction.
 * @param v Consist to change.
 * @param l %Vehicle index in the consist of the first vehicle.
 * @param r %Vehicle index in the consist of the second vehicle.
 */
void ReverseTrainSwapVeh(Train *v, int l, int r)
{
	Train *a, *b;

	/* locate vehicles to swap */
	for (a = v; l != 0; l--) a = a->Next();
	for (b = v; r != 0; r--) b = b->Next();

	if (a != b) {
		/* swap the hidden bits */
		{
			uint16 tmp = (a->vehstatus & ~VS_HIDDEN) | (b->vehstatus & VS_HIDDEN);
			b->vehstatus = (b->vehstatus & ~VS_HIDDEN) | (a->vehstatus & VS_HIDDEN);
			a->vehstatus = tmp;
		}

		Swap(a->track, b->track);
		Swap(a->direction, b->direction);
		Swap(a->x_pos, b->x_pos);
		Swap(a->y_pos, b->y_pos);
		Swap(a->tile,  b->tile);
		Swap(a->z_pos, b->z_pos);

		SwapTrainFlags(&a->gv_flags, &b->gv_flags);

		UpdateStatusAfterSwap(a);
		UpdateStatusAfterSwap(b);
	} else {
		/* Swap GVF_GOINGUP_BIT/GVF_GOINGDOWN_BIT.
		 * This is a little bit redundant way, a->gv_flags will
		 * be (re)set twice, but it reduces code duplication */
		SwapTrainFlags(&a->gv_flags, &a->gv_flags);
		UpdateStatusAfterSwap(a);
	}
}


/**
 * Check if the vehicle is a train
 * @param v vehicle on tile
 * @return v if it is a train, nullptr otherwise
 */
static Vehicle *TrainOnTileEnum(Vehicle *v, void *)
{
	return (v->type == VEH_TRAIN) ? v : nullptr;
}


/**
 * Checks if a train is approaching a rail-road crossing
 * @param v vehicle on tile
 * @param data tile with crossing we are testing
 * @return v if it is approaching a crossing, nullptr otherwise
 */
static Vehicle *TrainApproachingCrossingEnum(Vehicle *v, void *data)
{
	if (v->type != VEH_TRAIN || (v->vehstatus & VS_CRASHED)) return nullptr;

	Train *t = Train::From(v);
	if (!t->IsFrontEngine()) return nullptr;

	TileIndex tile = *(TileIndex *)data;

	if (TrainApproachingCrossingTile(t) != tile) return nullptr;

	return t;
}


/**
 * Finds a vehicle approaching rail-road crossing
 * @param tile tile to test
 * @return true if a vehicle is approaching the crossing
 * @pre tile is a rail-road crossing
 */
static bool TrainApproachingCrossing(TileIndex tile)
{
	assert(IsLevelCrossingTile(tile));

	DiagDirection dir = AxisToDiagDir(GetCrossingRailAxis(tile));
	TileIndex tile_from = tile + TileOffsByDiagDir(dir);

	if (HasVehicleOnPos(tile_from, &tile, &TrainApproachingCrossingEnum)) return true;

	dir = ReverseDiagDir(dir);
	tile_from = tile + TileOffsByDiagDir(dir);

	return HasVehicleOnPos(tile_from, &tile, &TrainApproachingCrossingEnum);
}


/**
 * Sets correct crossing state
 * @param tile tile to update
 * @param sound should we play sound?
 * @pre tile is a rail-road crossing
 */
void UpdateLevelCrossing(TileIndex tile, bool sound)
{
	assert(IsLevelCrossingTile(tile));

	/* reserved || train on crossing || train approaching crossing */
	bool new_state = HasCrossingReservation(tile) || HasVehicleOnPos(tile, nullptr, &TrainOnTileEnum) || TrainApproachingCrossing(tile);

	if (new_state != IsCrossingBarred(tile)) {
		if (new_state && sound) {
			if (_settings_client.sound.ambient) SndPlayTileFx(SND_0E_LEVEL_CROSSING, tile);
		}
		SetCrossingBarred(tile, new_state);
		MarkTileDirtyByTile(tile);
	}
}


/**
 * Bars crossing and plays ding-ding sound if not barred already
 * @param tile tile with crossing
 * @pre tile is a rail-road crossing
 */
static inline void MaybeBarCrossingWithSound(TileIndex tile)
{
	if (!IsCrossingBarred(tile)) {
		BarCrossing(tile);
		if (_settings_client.sound.ambient) SndPlayTileFx(SND_0E_LEVEL_CROSSING, tile);
		MarkTileDirtyByTile(tile);
	}
}


/**
 * Advances wagons for train reversing, needed for variable length wagons.
 * This one is called before the train is reversed.
 * @param v First vehicle in chain
 */
static void AdvanceWagonsBeforeSwap(Train *v)
{
	Train *base = v;
	Train *first = base; // first vehicle to move
	Train *last = v->Last(); // last vehicle to move
	uint length = CountVehiclesInChain(v);

	while (length > 2) {
		last = last->Previous();
		first = first->Next();

		int differential = base->CalcNextVehicleOffset() - last->CalcNextVehicleOffset();

		/* do not update images now
		 * negative differential will be handled in AdvanceWagonsAfterSwap() */
		for (int i = 0; i < differential; i++) TrainController(first, last->Next());

		base = first; // == base->Next()
		length -= 2;
	}
}


/**
 * Advances wagons for train reversing, needed for variable length wagons.
 * This one is called after the train is reversed.
 * @param v First vehicle in chain
 */
static void AdvanceWagonsAfterSwap(Train *v)
{
	/* first of all, fix the situation when the train was entering a depot */
	Train *dep = v; // last vehicle in front of just left depot
	while (dep->Next() != nullptr && (dep->track == TRACK_BIT_DEPOT || dep->Next()->track != TRACK_BIT_DEPOT)) {
		dep = dep->Next(); // find first vehicle outside of a depot, with next vehicle inside a depot
	}

	Train *leave = dep->Next(); // first vehicle in a depot we are leaving now

	if (leave != nullptr) {
		/* 'pull' next wagon out of the depot, so we won't miss it (it could stay in depot forever) */
		int d = TicksToLeaveDepot(dep);

		if (d <= 0) {
			leave->vehstatus &= ~VS_HIDDEN; // move it out of the depot
			leave->track = TrackToTrackBits(GetRailDepotTrack(leave->tile));
			for (int i = 0; i >= d; i--) TrainController(leave, nullptr); // maybe move it, and maybe let another wagon leave
		}
	} else {
		dep = nullptr; // no vehicle in a depot, so no vehicle leaving a depot
	}

	Train *base = v;
	Train *first = base; // first vehicle to move
	Train *last = v->Last(); // last vehicle to move
	uint length = CountVehiclesInChain(v);

	/* We have to make sure all wagons that leave a depot because of train reversing are moved correctly
	 * they have already correct spacing, so we have to make sure they are moved how they should */
	bool nomove = (dep == nullptr); // If there is no vehicle leaving a depot, limit the number of wagons moved immediately.

	while (length > 2) {
		/* we reached vehicle (originally) in front of a depot, stop now
		 * (we would move wagons that are already moved with new wagon length). */
		if (base == dep) break;

		/* the last wagon was that one leaving a depot, so do not move it anymore */
		if (last == dep) nomove = true;

		last = last->Previous();
		first = first->Next();

		int differential = last->CalcNextVehicleOffset() - base->CalcNextVehicleOffset();

		/* do not update images now */
		for (int i = 0; i < differential; i++) TrainController(first, (nomove ? last->Next() : nullptr));

		base = first; // == base->Next()
		length -= 2;
	}
}

/**
 * Turn a train around.
 * @param v %Train to turn around.
 */
void ReverseTrainDirection(Train *v)
{
	if (IsRailDepotTile(v->tile)) {
		InvalidateWindowData(WC_VEHICLE_DEPOT, GetDepotIndex(v->tile));
		if (IsBigDepot(v->tile) && (v->track & TRACK_BIT_DEPOT) != 0 && (v->track & ~TRACK_BIT_DEPOT) != 0) return;
	}

	/* Clear path reservation in front if train is not stuck. */
	if (!HasBit(v->flags, VRF_TRAIN_STUCK)) FreeTrainTrackReservation(v);

	/* Check if we were approaching a rail/road-crossing */
	TileIndex crossing = TrainApproachingCrossingTile(v);

	/* count number of vehicles */
	int r = CountVehiclesInChain(v) - 1;  // number of vehicles - 1

	AdvanceWagonsBeforeSwap(v);

	/* swap start<>end, start+1<>end-1, ... */
	int l = 0;
	do {
		ReverseTrainSwapVeh(v, l++, r--);
	} while (l <= r);

	AdvanceWagonsAfterSwap(v);

	if (IsRailDepotTile(v->tile)) {
		InvalidateWindowData(WC_VEHICLE_DEPOT, GetDepotIndex(v->tile));
	}

	ToggleBit(v->flags, VRF_TOGGLE_REVERSE);

	ClrBit(v->flags, VRF_REVERSING);

	/* recalculate cached data */
	v->ConsistChanged(CCF_TRACK);

	/* update all images */
	for (Train *u = v; u != nullptr; u = u->Next()) u->UpdateViewport(false, false);

	/* update crossing we were approaching */
	if (crossing != INVALID_TILE) UpdateLevelCrossing(crossing);

	/* maybe we are approaching crossing now, after reversal */
	crossing = TrainApproachingCrossingTile(v);
	if (crossing != INVALID_TILE) MaybeBarCrossingWithSound(crossing);

	/* If we are inside a depot after reversing, don't bother with path reserving. */
	if (v->track == TRACK_BIT_DEPOT) {
		/* Can't be stuck here as inside a depot is always a safe tile. */
		if (HasBit(v->flags, VRF_TRAIN_STUCK)) SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
		ClrBit(v->flags, VRF_TRAIN_STUCK);
		return;
	}

	/* VehicleExitDir does not always produce the desired dir for depots and
	 * tunnels/bridges that is needed for UpdateSignalsOnSegment. */
	DiagDirection dir = VehicleExitDir(v->direction, v->track);
	if (IsRailDepotTile(v->tile) || IsTileType(v->tile, MP_TUNNELBRIDGE)) dir = INVALID_DIAGDIR;

	if (UpdateSignalsOnSegment(v->tile, dir, v->owner) == SIGSEG_PBS || _settings_game.pf.reserve_paths) {
		/* If we are currently on a tile with conventional signals, we can't treat the
		 * current tile as a safe tile or we would enter a PBS block without a reservation. */
		bool first_tile_okay = !(IsTileType(v->tile, MP_RAILWAY) &&
			HasSignalOnTrackdir(v->tile, v->GetVehicleTrackdir()) &&
			!IsPbsSignal(GetSignalType(v->tile, FindFirstTrack(v->track))));

		/* If we are on a depot tile facing outwards, do not treat the current tile as safe. */
		if (IsSmallRailDepotTile(v->tile) && TrackdirToExitdir(v->GetVehicleTrackdir()) == GetRailDepotDirection(v->tile)) first_tile_okay = false;

		if (IsPlatformTile(v->tile)) SetPlatformReservation(v->tile, TrackdirToExitdir(v->GetVehicleTrackdir()), true);
		if (TryPathReserve(v, false, first_tile_okay)) {
			/* Do a look-ahead now in case our current tile was already a safe tile. */
			CheckNextTrainTile(v);
		} else if (v->current_order.GetType() != OT_LOADING) {
			/* Do not wait for a way out when we're still loading */
			MarkTrainAsStuck(v);
		}
	} else if (HasBit(v->flags, VRF_TRAIN_STUCK)) {
		/* A train not inside a PBS block can't be stuck. */
		ClrBit(v->flags, VRF_TRAIN_STUCK);
		v->wait_counter = 0;
	}
}

/**
 * Reverse train.
 * @param tile unused
 * @param flags type of operation
 * @param p1 train to reverse
 * @param p2 if true, reverse a unit in a train (needs to be in a depot)
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdReverseTrainDirection(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const std::string &text)
{
	Train *v = Train::GetIfValid(p1);
	if (v == nullptr) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (p2 != 0) {
		/* turn a single unit around */

		if (v->IsMultiheaded() || HasBit(EngInfo(v->engine_type)->callback_mask, CBM_VEHICLE_ARTIC_ENGINE)) {
			return_cmd_error(STR_ERROR_CAN_T_REVERSE_DIRECTION_RAIL_VEHICLE_MULTIPLE_UNITS);
		}
		if (!HasBit(EngInfo(v->engine_type)->misc_flags, EF_RAIL_FLIPS)) return CMD_ERROR;

		Train *front = v->First();
		/* make sure the vehicle is stopped in the depot */
		if (!front->IsStoppedInDepot()) {
			return_cmd_error(STR_ERROR_TRAINS_CAN_ONLY_BE_ALTERED_INSIDE_A_DEPOT);
		}

		if (flags & DC_EXEC) {
			ToggleBit(v->flags, VRF_REVERSE_DIRECTION);

			front->ConsistChanged(CCF_ARRANGE);
			if (IsRailDepotTile(front->tile)) {
				if (IsBigDepot(front->tile)) {
					TrainPlacement tp;
					tp.LiftTrain(front, flags);
					tp.PlaceTrain(front, flags & ~DC_EXEC);
				}
				SetWindowDirty(WC_VEHICLE_DEPOT, GetDepotIndex(front->tile));
			}
			SetWindowDirty(WC_VEHICLE_DETAILS, front->index);
			SetWindowDirty(WC_VEHICLE_VIEW, front->index);
			SetWindowClassesDirty(WC_TRAINS_LIST);
		}
	} else {
		/* turn the whole train around */
		if ((v->vehstatus & VS_CRASHED) || v->breakdown_ctr != 0) return CMD_ERROR;

		/* Do not reverse while servicing. */
		if (IsBigRailDepotTile(v->tile) && (v->track & TRACK_BIT_DEPOT) != 0 && (v->track & ~TRACK_BIT_DEPOT) != 0) return CMD_ERROR;

		if (flags & DC_EXEC) {
			/* Properly leave the station if we are loading and won't be loading anymore */
			if (v->current_order.IsType(OT_LOADING)) {
				const Vehicle *last = v;
				while (last->Next() != nullptr) last = last->Next();

				/* not a station || different station --> leave the station */
				if (!IsTileType(last->tile, MP_STATION) || GetStationIndex(last->tile) != GetStationIndex(v->tile)) {
					v->LeaveStation();
				}
			}

			/* We cancel any 'skip signal at dangers' here */
			v->force_proceed = TFP_NONE;
			SetWindowDirty(WC_VEHICLE_VIEW, v->index);

			if (_settings_game.vehicle.train_acceleration_model != AM_ORIGINAL && v->cur_speed != 0) {
				ToggleBit(v->flags, VRF_REVERSING);
			} else {
				v->cur_speed = 0;
				v->SetLastSpeed();
				HideFillingPercent(&v->fill_percent_te_id);
				ReverseTrainDirection(v);
			}
		}
	}
	return CommandCost();
}

/**
 * Force a train through a red signal
 * @param tile unused
 * @param flags type of operation
 * @param p1 train to ignore the red signal
 * @param p2 unused
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdForceTrainProceed(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const std::string &text)
{
	Train *t = Train::GetIfValid(p1);
	if (t == nullptr) return CMD_ERROR;

	if (!t->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(t->owner);
	if (ret.Failed()) return ret;


	if (flags & DC_EXEC) {
		/* If we are forced to proceed, cancel that order.
		 * If we are marked stuck we would want to force the train
		 * to proceed to the next signal. In the other cases we
		 * would like to pass the signal at danger and run till the
		 * next signal we encounter. */
		t->force_proceed = t->force_proceed == TFP_SIGNAL ? TFP_NONE : HasBit(t->flags, VRF_TRAIN_STUCK) || t->IsChainInDepot() ? TFP_STUCK : TFP_SIGNAL;
		SetWindowDirty(WC_VEHICLE_VIEW, t->index);
	}

	return CommandCost();
}

/**
 * Try to find a depot nearby.
 * @param v %Train that wants a depot.
 * @param max_distance Maximal search distance.
 * @return Information where the closest train depot is located.
 * @pre The given vehicle must not be crashed!
 */
static FindDepotData FindClosestTrainDepot(Train *v, int max_distance)
{
	assert(!(v->vehstatus & VS_CRASHED));

	if (IsRailDepotTile(v->tile)) return FindDepotData(v->tile, 0);

	PBSTileInfo origin = FollowTrainReservation(v);
	if (IsRailDepotTile(origin.tile)) return FindDepotData(origin.tile, 0);

	switch (_settings_game.pf.pathfinder_for_trains) {
		case VPF_NPF: return NPFTrainFindNearestDepot(v, max_distance);
		case VPF_YAPF: return YapfTrainFindNearestDepot(v, max_distance);

		default: NOT_REACHED();
	}
}

/**
 * Locate the closest depot for this consist, and return the information to the caller.
 * @param[out] location    If not \c nullptr and a depot is found, store its location in the given address.
 * @param[out] destination If not \c nullptr and a depot is found, store its index in the given address.
 * @param[out] reverse     If not \c nullptr and a depot is found, store reversal information in the given address.
 * @return A depot has been found.
 */
bool Train::FindClosestDepot(TileIndex *location, DestinationID *destination, bool *reverse)
{
	FindDepotData tfdd = FindClosestTrainDepot(this, 0);
	if (tfdd.best_length == UINT_MAX) return false;

	if (location    != nullptr) *location    = tfdd.tile;
	if (destination != nullptr) *destination = GetDepotIndex(tfdd.tile);
	if (reverse     != nullptr) *reverse     = tfdd.reverse;

	return true;
}

/** Play a sound for a train leaving the station. */
void Train::PlayLeaveStationSound() const
{
	static const SoundFx sfx[] = {
		SND_04_DEPARTURE_STEAM,
		SND_0A_DEPARTURE_TRAIN,
		SND_0A_DEPARTURE_TRAIN,
		SND_47_DEPARTURE_MONORAIL,
		SND_41_DEPARTURE_MAGLEV
	};

	if (PlayVehicleSound(this, VSE_START)) return;

	EngineID engtype = this->engine_type;
	SndPlayVehicleFx(sfx[RailVehInfo(engtype)->engclass], this);
}

/**
 * Check if the train is on the last reserved tile and try to extend the path then.
 * @param v %Train that needs its path extended.
 */
static void CheckNextTrainTile(Train *v)
{
	/* Don't do any look-ahead if path_backoff_interval is 255. */
	if (_settings_game.pf.path_backoff_interval == 255) return;

	/* Exit if we are inside a depot. */
	if (v->track == TRACK_BIT_DEPOT) return;

	switch (v->current_order.GetType()) {
		/* Exit if we reached our destination depot. */
		case OT_GOTO_DEPOT:
			if (IsRailDepotTile(v->tile) && v->current_order.ShouldStopAtDepot(GetDepotIndex(v->tile))) return;
			break;

		case OT_GOTO_WAYPOINT:
			/* If we reached our waypoint, make sure we see that. */
			if (IsRailWaypointTile(v->tile) && GetStationIndex(v->tile) == v->current_order.GetDestination()) ProcessOrders(v);
			break;

		case OT_NOTHING:
		case OT_LEAVESTATION:
		case OT_LOADING:
			/* Exit if the current order doesn't have a destination, but the train has orders. */
			if (v->GetNumOrders() > 0) return;
			break;

		default:
			break;
	}
	/* Exit if we are on a station tile and are going to stop. */
	if (IsRailStationTile(v->tile) && v->current_order.ShouldStopAtStation(v, GetStationIndex(v->tile))) return;

	Trackdir td = v->GetVehicleTrackdir();

	/* On a tile with a red non-pbs signal, don't look ahead. */
	if (IsTileType(v->tile, MP_RAILWAY) && HasSignalOnTrackdir(v->tile, td) &&
			!IsPbsSignal(GetSignalType(v->tile, TrackdirToTrack(td))) &&
			GetSignalStateByTrackdir(v->tile, td) == SIGNAL_STATE_RED) return;

	CFollowTrackRail ft(v);
	if (!ft.Follow(v->tile, td)) return;

	if (!HasReservedTracks(ft.m_new_tile, TrackdirBitsToTrackBits(ft.m_new_td_bits))) {
		/* Next tile is not reserved. */
		if (KillFirstBit(ft.m_new_td_bits) == TRACKDIR_BIT_NONE) {
			if (HasPbsSignalOnTrackdir(ft.m_new_tile, FindFirstTrackdir(ft.m_new_td_bits))) {
				/* If the next tile is a PBS signal, try to make a reservation. */
				TrackBits tracks = TrackdirBitsToTrackBits(ft.m_new_td_bits);
				if (Rail90DegTurnDisallowed(GetTileRailType(ft.m_old_tile), GetTileRailType(ft.m_new_tile))) {
					tracks &= ~TrackCrossesTracks(TrackdirToTrack(ft.m_old_td));
				}
				ChooseTrainTrack(v, ft.m_new_tile, ft.m_exitdir, tracks, false, nullptr, false);
			}
		}
	}
}

bool HandleTrainEnterDepot(Train *v)
{
	assert(IsRailDepotTile(v->tile));

	if (IsBigRailDepot(v->tile)) {
		v->cur_speed = 0;
		Train *t = Train::From(v);
		for (Train *u = t; u != nullptr; u = u->Next()) {
			if (!IsCompatibleTrainDepotTile(u->tile, t->tile)) {
				SetDParam(0, v->index);
				AddVehicleAdviceNewsItem(STR_NEWS_VEHICLE_TOO_LONG_FOR_SERVICING, v->index);
				return false;
			}
		}

		for (Train *u = t; u != nullptr; u = u->Next()) u->track |= TRACK_BIT_DEPOT;
		t->force_proceed = TFP_NONE;
		ClrBit(t->flags, VRF_TOGGLE_REVERSE);
		v->UpdateViewport(true, true);
		SetWindowClassesDirty(WC_TRAINS_LIST);
		SetWindowDirty(WC_VEHICLE_VIEW, v->index);

		InvalidateWindowData(WC_VEHICLE_DEPOT, GetDepotIndex(v->tile));
		v->StartService();
	} else {
		/* Clear path reservation */
		SetDepotReservation(v->tile, false);
		VehicleEnterDepot(v);
	}

	return true;
}


bool CheckReverseTrain(const Train *v)
{
	if (_settings_game.difficulty.line_reverse_mode != 0 ||
			v->track == TRACK_BIT_DEPOT || v->track == TRACK_BIT_WORMHOLE ||
			!(v->direction & 1)) {
		return false;
	}

	assert(v->track != TRACK_BIT_NONE);

	switch (_settings_game.pf.pathfinder_for_trains) {
		case VPF_NPF: return NPFTrainCheckReverse(v);
		case VPF_YAPF: return YapfTrainCheckReverse(v);

		default: NOT_REACHED();
	}
}


/**
 * Will the train stay in the depot the next tick?
 * @param v %Train to check.
 * @return True if it stays in the depot, false otherwise.
 */
static bool CheckTrainStayInDepot(Train *v)
{
	/* bail out if not all wagons are in the same depot or not in a depot at all */
	if (!v->IsInDepot()) return false;
	assert(IsRailDepotTile(v->tile));

	DepotID depot_id = GetDepotIndex(v->tile);
	if (IsBigRailDepot(v->tile)) {
		/* If not placed, try it. If not possible, exit. */
		if (CheckIfTrainNeedsPlacement(v)) {
			/* If stuck, wait a little bit, so we can avoid
			 * trying placing it as much as we can. */
			bool already_stuck = false;
			bool send_message = false;
			if (HasBit(v->flags, VRF_TRAIN_STUCK)) {
				already_stuck = true;
				v->wait_counter++;
				if (v->wait_counter % (1 << 7) != 0) {
					return true;
				} else if (v->wait_counter % (1 << 11) == 0) {
					send_message = true;
				}
				ClrBit(v->flags, VRF_TRAIN_STUCK);
			}

			TrainPlacement train_placement;
			train_placement.LiftTrain(v, DC_EXEC);
			train_placement.LookForPlaceInDepot(v, true);
			if (train_placement.info < PI_FAILED_END) {
				if (send_message) {
					ClrBit(v->flags, VRF_TRAIN_STUCK);
					/* Show message to player. */
					if (_settings_client.gui.lost_vehicle_warn && v->owner == _local_company) {
						SetDParam(0, v->index);
						AddVehicleAdviceNewsItem(STR_ADVICE_PLATFORM_TYPE + train_placement.info - PI_ERROR_BEGIN, v->index);
					}
				}
				if (already_stuck) {
					SetBit(v->flags, VRF_TRAIN_STUCK);
				} else {
					MarkTrainAsStuck(v);
				}
				return true;
			} else {
				VehicleServiceInBigDepot(v);
				train_placement.PlaceTrain(v, DC_EXEC);
				if (!IsBigDepot(v->tile)) return true;
			}
		} else {
			VehicleServiceInBigDepot(v);
		}

		for (Train *u = v; u != nullptr; u = u->Next()) u->track &= ~TRACK_BIT_DEPOT;

		v->cur_speed = 0;
		v->UpdateAcceleration();
		if (CheckReverseTrain(v)) ReverseTrainDirection(v);
		InvalidateWindowData(WC_VEHICLE_DEPOT, depot_id);

		/* Check whether it is safe to exit the depot. */
		if (UpdateSignalsOnSegment(v->tile, VehicleExitDir(v->direction, v->track), v->owner) == SIGSEG_PBS || _settings_game.pf.reserve_paths) {
			if (!TryPathReserve(v, true, true)) return true;
		}
		return false;
	} else {
		for (const Train *u = v; u != nullptr; u = u->Next()) {
			if (!u->IsInDepot() || u->tile != v->tile) return false;
		}

		/* if the train got no power, then keep it in the depot */
		if (v->gcache.cached_power == 0) {
			v->vehstatus |= VS_STOPPED;
			SetWindowDirty(WC_VEHICLE_DEPOT, depot_id);
			return true;
		}
	}

	SigSegState seg_state;

	if (v->force_proceed == TFP_NONE) {
		/* force proceed was not pressed */
		if (++v->wait_counter < 37) {
			SetWindowClassesDirty(WC_TRAINS_LIST);
			return true;
		}

		v->wait_counter = 0;

		seg_state = _settings_game.pf.reserve_paths ? SIGSEG_PBS : UpdateSignalsOnSegment(v->tile, INVALID_DIAGDIR, v->owner);
		if (seg_state == SIGSEG_FULL || HasDepotReservation(v->tile)) {
			/* Full and no PBS signal in block or depot reserved, can't exit. */
			SetWindowClassesDirty(WC_TRAINS_LIST);
			return true;
		}
	} else {
		seg_state = _settings_game.pf.reserve_paths ? SIGSEG_PBS : UpdateSignalsOnSegment(v->tile, INVALID_DIAGDIR, v->owner);
	}

	/* We are leaving a depot, but have to go to the exact same one; re-enter. */
	if (v->current_order.IsType(OT_GOTO_DEPOT) && v->tile == v->dest_tile) {
		/* Service when depot has no reservation. */
		if (!HasDepotReservation(v->tile)) HandleTrainEnterDepot(v);
		return true;
	}

	/* Only leave when we can reserve a path to our destination. */
	if (seg_state == SIGSEG_PBS && !TryPathReserve(v) && v->force_proceed == TFP_NONE) {
		/* No path and no force proceed. */
		SetWindowClassesDirty(WC_TRAINS_LIST);
		MarkTrainAsStuck(v);
		return true;
	}

	SetDepotReservation(v->tile, true);
	if (_settings_client.gui.show_track_reservation) MarkTileDirtyByTile(v->tile);

	VehicleServiceInDepot(v);
	SetWindowClassesDirty(WC_TRAINS_LIST);
	v->PlayLeaveStationSound();

	v->track = TRACK_BIT_X;
	if (v->direction & 2) v->track = TRACK_BIT_Y;

	v->vehstatus &= ~VS_HIDDEN;
	v->cur_speed = 0;

	v->UpdateViewport(true, true);
	v->UpdatePosition();
	UpdateSignalsOnSegment(v->tile, INVALID_DIAGDIR, v->owner);
	v->UpdateAcceleration();
	InvalidateWindowData(WC_VEHICLE_DEPOT, depot_id);

	return false;
}

/**
 * Clear the reservation of \a tile that was just left by a wagon on \a track_dir.
 * @param v %Train owning the reservation.
 * @param tile Tile with reservation to clear.
 * @param track_dir Track direction to clear.
 */
static void ClearPathReservation(const Train *v, TileIndex tile, Trackdir track_dir)
{
	DiagDirection dir = TrackdirToExitdir(track_dir);

	if (IsTileType(tile, MP_TUNNELBRIDGE)) {
		/* Are we just leaving a tunnel/bridge? */
		if (GetTunnelBridgeDirection(tile) == ReverseDiagDir(dir)) {
			TileIndex end = GetOtherTunnelBridgeEnd(tile);

			if (TunnelBridgeIsFree(tile, end, v).Succeeded()) {
				/* Free the reservation only if no other train is on the tiles. */
				SetTunnelBridgeReservation(tile, false);
				SetTunnelBridgeReservation(end, false);

				if (_settings_client.gui.show_track_reservation) {
					if (IsBridge(tile)) {
						MarkBridgeDirty(tile);
					} else {
						MarkTileDirtyByTile(tile);
						MarkTileDirtyByTile(end);
					}
				}
			}
		}
	} else if (IsRailStationTile(tile) || IsBigRailDepotTile(tile)) {
		TileIndex new_tile = TileAddByDiagDir(tile, dir);
		/* If the new tile is not a further tile of the same station, we
		 * clear the reservation for the whole platform. */
		if (!IsCompatiblePlatformTile(new_tile, tile)) {
			SetPlatformReservation(tile, ReverseDiagDir(dir), false);
		}
	} else {
		/* Any other tile */
		UnreserveRailTrack(tile, TrackdirToTrack(track_dir));
	}
}

/**
 * Free the reserved path in front of a vehicle.
 * @param v %Train owning the reserved path.
 */
void FreeTrainTrackReservation(const Train *v)
{
	assert(v->IsFrontEngine());

	TileIndex tile = v->tile;
	Trackdir  td = v->GetVehicleTrackdir();
	bool      free_tile = !(IsRailStationTile(v->tile) || IsBigRailDepotTile(v->tile) || IsTileType(v->tile, MP_TUNNELBRIDGE));
	StationID station_id = IsRailStationTile(v->tile) ? GetStationIndex(v->tile) : INVALID_STATION;
	DepotID   depot_id = IsBigRailDepotTile(v->tile) ? GetDepotIndex(v->tile) : INVALID_DEPOT;

	/* Can't be holding a reservation if we enter a depot. */
	if (IsSmallRailDepotTile(tile) && TrackdirToExitdir(td) != GetRailDepotDirection(tile)) return;
	if (v->track == TRACK_BIT_DEPOT) {
		/* Front engine is in a depot. We enter if some part is not in the depot. */
		for (const Train *u = v; u != nullptr; u = u->Next()) {
			if (u->track != TRACK_BIT_DEPOT || u->tile != v->tile) return;
		}
	}
	/* Don't free reservation if it's not ours. */
	if (TracksOverlap(GetReservedTrackbits(tile) | TrackToTrackBits(TrackdirToTrack(td)))) return;

	CFollowTrackRail ft(v, GetRailTypeInfo(v->railtype)->compatible_railtypes);
	while (ft.Follow(tile, td)) {
		tile = ft.m_new_tile;
		TrackdirBits bits = ft.m_new_td_bits & TrackBitsToTrackdirBits(GetReservedTrackbits(tile));
		td = RemoveFirstTrackdir(&bits);
		assert(bits == TRACKDIR_BIT_NONE);

		if (!IsValidTrackdir(td)) break;

		if (IsTileType(tile, MP_RAILWAY)) {
			if (HasSignalOnTrackdir(tile, td) && !IsPbsSignal(GetSignalType(tile, TrackdirToTrack(td)))) {
				/* Conventional signal along trackdir: remove reservation and stop. */
				UnreserveRailTrack(tile, TrackdirToTrack(td));
				break;
			}
			if (HasPbsSignalOnTrackdir(tile, td)) {
				if (GetSignalStateByTrackdir(tile, td) == SIGNAL_STATE_RED) {
					/* Red PBS signal? Can't be our reservation, would be green then. */
					break;
				} else {
					/* Turn the signal back to red. */
					SetSignalStateByTrackdir(tile, td, SIGNAL_STATE_RED);
					MarkTileDirtyByTile(tile);
				}
			} else if (HasSignalOnTrackdir(tile, ReverseTrackdir(td)) && IsOnewaySignal(tile, TrackdirToTrack(td))) {
				break;
			}
		}

		/* Don't free first station/bridge/tunnel if we are on it. */
		if (free_tile || (!(ft.m_is_station && GetStationIndex(ft.m_new_tile) == station_id) && !(ft.m_is_big_depot && GetDepotIndex(ft.m_new_tile) == depot_id) && !ft.m_is_tunnel && !ft.m_is_bridge)) ClearPathReservation(v, tile, td);

		free_tile = true;
	}
}

static const byte _initial_tile_subcoord[6][4][3] = {
{{ 15, 8, 1 }, { 0, 0, 0 }, { 0, 8, 5 }, { 0,  0, 0 }},
{{  0, 0, 0 }, { 8, 0, 3 }, { 0, 0, 0 }, { 8, 15, 7 }},
{{  0, 0, 0 }, { 7, 0, 2 }, { 0, 7, 6 }, { 0,  0, 0 }},
{{ 15, 8, 2 }, { 0, 0, 0 }, { 0, 0, 0 }, { 8, 15, 6 }},
{{ 15, 7, 0 }, { 8, 0, 4 }, { 0, 0, 0 }, { 0,  0, 0 }},
{{  0, 0, 0 }, { 0, 0, 0 }, { 0, 8, 4 }, { 7, 15, 0 }},
};

/**
 * Perform pathfinding for a train.
 *
 * @param v The train
 * @param tile The tile the train is about to enter
 * @param enterdir Diagonal direction the train is coming from
 * @param tracks Usable tracks on the new tile
 * @param[out] path_found Whether a path has been found or not.
 * @param do_track_reservation Path reservation is requested
 * @param[out] dest State and destination of the requested path
 * @return The best track the train should follow
 */
static Track DoTrainPathfind(const Train *v, TileIndex tile, DiagDirection enterdir, TrackBits tracks, bool &path_found, bool do_track_reservation, PBSTileInfo *dest)
{
	switch (_settings_game.pf.pathfinder_for_trains) {
		case VPF_NPF: return NPFTrainChooseTrack(v, path_found, do_track_reservation, dest);
		case VPF_YAPF: return YapfTrainChooseTrack(v, tile, enterdir, tracks, path_found, do_track_reservation, dest);

		default: NOT_REACHED();
	}
}

/**
 * Extend a train path as far as possible. Stops on encountering a safe tile,
 * another reservation or a track choice.
 * @return INVALID_TILE indicates that the reservation failed.
 */
static PBSTileInfo ExtendTrainReservation(const Train *v, TrackBits *new_tracks, DiagDirection *enterdir)
{
	PBSTileInfo origin = FollowTrainReservation(v);

	CFollowTrackRail ft(v);

	TileIndex tile = origin.tile;
	Trackdir  cur_td = origin.trackdir;
	while (ft.Follow(tile, cur_td)) {
		if (KillFirstBit(ft.m_new_td_bits) == TRACKDIR_BIT_NONE) {
			/* Possible signal tile. */
			if (HasOnewaySignalBlockingTrackdir(ft.m_new_tile, FindFirstTrackdir(ft.m_new_td_bits))) break;
		}

		if (Rail90DegTurnDisallowed(GetTileRailType(ft.m_old_tile), GetTileRailType(ft.m_new_tile))) {
			ft.m_new_td_bits &= ~TrackdirCrossesTrackdirs(ft.m_old_td);
			if (ft.m_new_td_bits == TRACKDIR_BIT_NONE) break;
		}

		/* Station, depot or waypoint are a possible target. */
		bool target_seen = ft.m_is_station || ft.m_is_big_depot || (IsTileType(ft.m_new_tile, MP_RAILWAY) && !IsPlainRail(ft.m_new_tile));
		if (target_seen || KillFirstBit(ft.m_new_td_bits) != TRACKDIR_BIT_NONE) {
			/* Choice found or possible target encountered.
			 * On finding a possible target, we need to stop and let the pathfinder handle the
			 * remaining path. This is because we don't know if this target is in one of our
			 * orders, so we might cause pathfinding to fail later on if we find a choice.
			 * This failure would cause a bogous call to TryReserveSafePath which might reserve
			 * a wrong path not leading to our next destination. */
			if (HasReservedTracks(ft.m_new_tile, TrackdirBitsToTrackBits(TrackdirReachesTrackdirs(ft.m_old_td)))) break;

			/* If we did skip some tiles, backtrack to the first skipped tile so the pathfinder
			 * actually starts its search at the first unreserved tile. */
			if (ft.m_tiles_skipped != 0) ft.m_new_tile -= TileOffsByDiagDir(ft.m_exitdir) * ft.m_tiles_skipped;

			/* Choice found, path valid but not okay. Save info about the choice tile as well. */
			if (new_tracks != nullptr) *new_tracks = TrackdirBitsToTrackBits(ft.m_new_td_bits);
			if (enterdir != nullptr) *enterdir = ft.m_exitdir;
			return PBSTileInfo(ft.m_new_tile, ft.m_old_td, false);
		}

		tile = ft.m_new_tile;
		cur_td = FindFirstTrackdir(ft.m_new_td_bits);

		if (IsSafeWaitingPosition(v, tile, cur_td, true, _settings_game.pf.forbid_90_deg)) {
			bool wp_free = IsWaitingPositionFree(v, tile, cur_td, _settings_game.pf.forbid_90_deg);
			if (!(wp_free && TryReserveRailTrack(tile, TrackdirToTrack(cur_td)))) break;
			/* Safe position is all good, path valid and okay. */
			return PBSTileInfo(tile, cur_td, true);
		}

		if (!TryReserveRailTrack(tile, TrackdirToTrack(cur_td))) break;
	}

	if (ft.m_err == CFollowTrackRail::EC_OWNER || ft.m_err == CFollowTrackRail::EC_NO_WAY) {
		/* End of line, path valid and okay. */
		return PBSTileInfo(ft.m_old_tile, ft.m_old_td, true);
	}

	/* Sorry, can't reserve path, back out. */
	tile = origin.tile;
	cur_td = origin.trackdir;
	TileIndex stopped = ft.m_old_tile;
	Trackdir  stopped_td = ft.m_old_td;
	while (tile != stopped || cur_td != stopped_td) {
		if (!ft.Follow(tile, cur_td)) break;

		if (Rail90DegTurnDisallowed(GetTileRailType(ft.m_old_tile), GetTileRailType(ft.m_new_tile))) {
			ft.m_new_td_bits &= ~TrackdirCrossesTrackdirs(ft.m_old_td);
			assert(ft.m_new_td_bits != TRACKDIR_BIT_NONE);
		}
		assert(KillFirstBit(ft.m_new_td_bits) == TRACKDIR_BIT_NONE);

		tile = ft.m_new_tile;
		cur_td = FindFirstTrackdir(ft.m_new_td_bits);

		UnreserveRailTrack(tile, TrackdirToTrack(cur_td));
	}

	/* Path invalid. */
	return PBSTileInfo();
}

/**
 * Try to reserve any path to a safe tile, ignoring the vehicle's destination.
 * Safe tiles are tiles in front of a signal, depots and station tiles at end of line.
 *
 * @param v The vehicle.
 * @param tile The tile the search should start from.
 * @param td The trackdir the search should start from.
 * @param override_railtype Whether all physically compatible railtypes should be followed.
 * @return True if a path to a safe stopping tile could be reserved.
 */
static bool TryReserveSafeTrack(const Train *v, TileIndex tile, Trackdir td, bool override_railtype)
{
	switch (_settings_game.pf.pathfinder_for_trains) {
		case VPF_NPF: return NPFTrainFindNearestSafeTile(v, tile, td, override_railtype);
		case VPF_YAPF: return YapfTrainFindNearestSafeTile(v, tile, td, override_railtype);

		default: NOT_REACHED();
	}
}

/** This class will save the current order of a vehicle and restore it on destruction. */
class VehicleOrderSaver {
private:
	Train          *v;
	Order          old_order;
	TileIndex      old_dest_tile;
	StationID      old_last_station_visited;
	VehicleOrderID index;
	bool           suppress_implicit_orders;

public:
	VehicleOrderSaver(Train *_v) :
		v(_v),
		old_order(_v->current_order),
		old_dest_tile(_v->dest_tile),
		old_last_station_visited(_v->last_station_visited),
		index(_v->cur_real_order_index),
		suppress_implicit_orders(HasBit(_v->gv_flags, GVF_SUPPRESS_IMPLICIT_ORDERS))
	{
	}

	~VehicleOrderSaver()
	{
		this->v->current_order = this->old_order;
		this->v->dest_tile = this->old_dest_tile;
		this->v->last_station_visited = this->old_last_station_visited;
		SB(this->v->gv_flags, GVF_SUPPRESS_IMPLICIT_ORDERS, 1, suppress_implicit_orders ? 1: 0);
	}

	/**
	 * Set the current vehicle order to the next order in the order list.
	 * @param skip_first Shall the first (i.e. active) order be skipped?
	 * @return True if a suitable next order could be found.
	 */
	bool SwitchToNextOrder(bool skip_first)
	{
		if (this->v->GetNumOrders() == 0) return false;

		if (skip_first) ++this->index;

		int depth = 0;

		do {
			/* Wrap around. */
			if (this->index >= this->v->GetNumOrders()) this->index = 0;

			Order *order = this->v->GetOrder(this->index);
			assert(order != nullptr);

			switch (order->GetType()) {
				case OT_GOTO_DEPOT:
					/* Skip service in depot orders when the train doesn't need service. */
					if ((order->GetDepotOrderType() & ODTFB_SERVICE) && !this->v->NeedsServicing()) break;
					FALLTHROUGH;
				case OT_GOTO_STATION:
				case OT_GOTO_WAYPOINT:
					this->v->current_order = *order;
					return UpdateOrderDest(this->v, order, 0, true);
				case OT_CONDITIONAL: {
					VehicleOrderID next = ProcessConditionalOrder(order, this->v);
					if (next != INVALID_VEH_ORDER_ID) {
						depth++;
						this->index = next;
						/* Don't increment next, so no break here. */
						continue;
					}
					break;
				}
				default:
					break;
			}
			/* Don't increment inside the while because otherwise conditional
			 * orders can lead to an infinite loop. */
			++this->index;
			depth++;
		} while (this->index != this->v->cur_real_order_index && depth < this->v->GetNumOrders());

		return false;
	}
};

/* choose a track */
static Track ChooseTrainTrack(Train *v, TileIndex tile, DiagDirection enterdir, TrackBits tracks, bool force_res, bool *got_reservation, bool mark_stuck)
{
	Track best_track = INVALID_TRACK;
	bool do_track_reservation = _settings_game.pf.reserve_paths || force_res;
	bool changed_signal = false;

	assert((tracks & ~TRACK_BIT_MASK) == 0);

	if (got_reservation != nullptr) *got_reservation = false;

	/* Don't use tracks here as the setting to forbid 90 deg turns might have been switched between reservation and now. */
	TrackBits res_tracks = (TrackBits)(GetReservedTrackbits(tile) & DiagdirReachesTracks(enterdir));
	/* Do we have a suitable reserved track? */
	if (res_tracks != TRACK_BIT_NONE) return FindFirstTrack(res_tracks);

	/* Quick return in case only one possible track is available */
	if (KillFirstBit(tracks) == TRACK_BIT_NONE) {
		Track track = FindFirstTrack(tracks);
		/* We need to check for signals only here, as a junction tile can't have signals. */
		if (track != INVALID_TRACK && HasPbsSignalOnTrackdir(tile, TrackEnterdirToTrackdir(track, enterdir))) {
			do_track_reservation = true;
			changed_signal = true;
			SetSignalStateByTrackdir(tile, TrackEnterdirToTrackdir(track, enterdir), SIGNAL_STATE_GREEN);
		} else if (!do_track_reservation) {
			return track;
		}
		best_track = track;
	}

	PBSTileInfo   res_dest(tile, INVALID_TRACKDIR, false);
	DiagDirection dest_enterdir = enterdir;
	if (do_track_reservation) {
		res_dest = ExtendTrainReservation(v, &tracks, &dest_enterdir);
		if (res_dest.tile == INVALID_TILE) {
			/* Reservation failed? */
			if (mark_stuck) MarkTrainAsStuck(v);
			if (changed_signal) SetSignalStateByTrackdir(tile, TrackEnterdirToTrackdir(best_track, enterdir), SIGNAL_STATE_RED);
			return FindFirstTrack(tracks);
		}
		if (res_dest.okay) {
			/* Got a valid reservation that ends at a safe target, quick exit. */
			if (got_reservation != nullptr) *got_reservation = true;
			if (changed_signal) MarkTileDirtyByTile(tile);
			TryReserveRailTrack(v->tile, TrackdirToTrack(v->GetVehicleTrackdir()));
			return best_track;
		}

		/* Check if the train needs service here, so it has a chance to always find a depot.
		 * Also check if the current order is a service order so we don't reserve a path to
		 * the destination but instead to the next one if service isn't needed. */
		CheckIfTrainNeedsService(v);
		if (v->current_order.IsType(OT_DUMMY) || v->current_order.IsType(OT_CONDITIONAL) || v->current_order.IsType(OT_GOTO_DEPOT)) ProcessOrders(v);
	}

	/* Save the current train order. The destructor will restore the old order on function exit. */
	VehicleOrderSaver orders(v);

	/* If the current tile is the destination of the current order and
	 * a reservation was requested, advance to the next order.
	 * Don't advance on a depot order as depots are always safe end points
	 * for a path and no look-ahead is necessary. This also avoids a
	 * problem with depot orders not part of the order list when the
	 * order list itself is empty. */
	if (v->current_order.IsType(OT_LEAVESTATION)) {
		orders.SwitchToNextOrder(false);
	} else if (v->current_order.IsType(OT_LOADING) || (!v->current_order.IsType(OT_GOTO_DEPOT) && (
			v->current_order.IsType(OT_GOTO_STATION) ?
			IsRailStationTile(v->tile) && v->current_order.GetDestination() == GetStationIndex(v->tile) :
			v->tile == v->dest_tile))) {
		orders.SwitchToNextOrder(true);
	}

	if (res_dest.tile != INVALID_TILE && !res_dest.okay) {
		/* Pathfinders are able to tell that route was only 'guessed'. */
		bool      path_found = true;
		TileIndex new_tile = res_dest.tile;

		Track next_track = DoTrainPathfind(v, new_tile, dest_enterdir, tracks, path_found, do_track_reservation, &res_dest);
		if (new_tile == tile) best_track = next_track;
		v->HandlePathfindingResult(path_found);
	}

	/* No track reservation requested -> finished. */
	if (!do_track_reservation) return best_track;

	/* A path was found, but could not be reserved. */
	if (res_dest.tile != INVALID_TILE && !res_dest.okay) {
		if (mark_stuck) MarkTrainAsStuck(v);
		FreeTrainTrackReservation(v);
		return best_track;
	}

	/* No possible reservation target found, we are probably lost. */
	if (res_dest.tile == INVALID_TILE) {
		/* Try to find any safe destination. */
		PBSTileInfo origin = FollowTrainReservation(v);
		if (TryReserveSafeTrack(v, origin.tile, origin.trackdir, false)) {
			TrackBits res = GetReservedTrackbits(tile) & DiagdirReachesTracks(enterdir);
			best_track = FindFirstTrack(res);
			TryReserveRailTrack(v->tile, TrackdirToTrack(v->GetVehicleTrackdir()));
			if (got_reservation != nullptr) *got_reservation = true;
			if (changed_signal) MarkTileDirtyByTile(tile);
		} else {
			FreeTrainTrackReservation(v);
			if (mark_stuck) MarkTrainAsStuck(v);
		}
		return best_track;
	}

	if (got_reservation != nullptr) *got_reservation = true;

	/* Reservation target found and free, check if it is safe. */
	while (!IsSafeWaitingPosition(v, res_dest.tile, res_dest.trackdir, true, _settings_game.pf.forbid_90_deg)) {
		/* Extend reservation until we have found a safe position. */
		DiagDirection exitdir = TrackdirToExitdir(res_dest.trackdir);
		TileIndex     next_tile = TileAddByDiagDir(res_dest.tile, exitdir);
		TrackBits     reachable = TrackdirBitsToTrackBits((TrackdirBits)(GetTileTrackStatus(next_tile, TRANSPORT_RAIL, 0))) & DiagdirReachesTracks(exitdir);
		if (Rail90DegTurnDisallowed(GetTileRailType(res_dest.tile), GetTileRailType(next_tile))) {
			reachable &= ~TrackCrossesTracks(TrackdirToTrack(res_dest.trackdir));
		}

		/* Get next order with destination. */
		if (orders.SwitchToNextOrder(true)) {
			PBSTileInfo cur_dest;
			bool path_found;
			DoTrainPathfind(v, next_tile, exitdir, reachable, path_found, true, &cur_dest);
			if (cur_dest.tile != INVALID_TILE) {
				res_dest = cur_dest;
				if (res_dest.okay) continue;
				/* Path found, but could not be reserved. */
				FreeTrainTrackReservation(v);
				if (mark_stuck) MarkTrainAsStuck(v);
				if (got_reservation != nullptr) *got_reservation = false;
				changed_signal = false;
				break;
			}
		}
		/* No order or no safe position found, try any position. */
		if (!TryReserveSafeTrack(v, res_dest.tile, res_dest.trackdir, true)) {
			FreeTrainTrackReservation(v);
			if (mark_stuck) MarkTrainAsStuck(v);
			if (got_reservation != nullptr) *got_reservation = false;
			changed_signal = false;
		}
		break;
	}

	TryReserveRailTrack(v->tile, TrackdirToTrack(v->GetVehicleTrackdir()));

	if (changed_signal) MarkTileDirtyByTile(tile);

	return best_track;
}

/**
 * Try to reserve a path to a safe position.
 *
 * @param v The vehicle
 * @param mark_as_stuck Should the train be marked as stuck on a failed reservation?
 * @param first_tile_okay True if no path should be reserved if the current tile is a safe position.
 * @return True if a path could be reserved.
 */
bool TryPathReserve(Train *v, bool mark_as_stuck, bool first_tile_okay)
{
	assert(v->IsFrontEngine());

	/* We have to handle depots specially as the track follower won't look
	 * at the depot tile itself but starts from the next tile. If we are still
	 * inside the depot, a depot reservation can never be ours. */
	if (v->track == TRACK_BIT_DEPOT) {
		if (HasDepotReservation(v->tile)) {
			if (mark_as_stuck) MarkTrainAsStuck(v);
			return false;
		} else {
			assert(IsSmallRailDepotTile(v->tile));
			/* Depot not reserved, but the next tile might be. */
			TileIndex next_tile = TileAddByDiagDir(v->tile, GetRailDepotDirection(v->tile));
			if (HasReservedTracks(next_tile, DiagdirReachesTracks(GetRailDepotDirection(v->tile)))) return false;
		}
	}

	Vehicle *other_train = nullptr;
	PBSTileInfo origin = FollowTrainReservation(v, &other_train);
	/* The path we are driving on is already blocked by some other train.
	 * This can only happen in certain situations when mixing path and
	 * block signals or when changing tracks and/or signals.
	 * Exit here as doing any further reservations will probably just
	 * make matters worse. */
	if (other_train != nullptr && other_train->index != v->index) {
		if (mark_as_stuck) MarkTrainAsStuck(v);
		return false;
	}
	/* If we have a reserved path and the path ends at a safe tile, we are finished already. */
	if (origin.okay && (v->tile != origin.tile || first_tile_okay)) {
		/* Can't be stuck then. */
		if (HasBit(v->flags, VRF_TRAIN_STUCK)) SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
		ClrBit(v->flags, VRF_TRAIN_STUCK);
		return true;
	}

	/* If we are in a depot, tentatively reserve the depot. */
	if (v->track == TRACK_BIT_DEPOT) {
		SetDepotReservation(v->tile, true);
		if (_settings_client.gui.show_track_reservation) MarkTileDirtyByTile(v->tile);
	}

	DiagDirection exitdir = TrackdirToExitdir(origin.trackdir);
	TileIndex     new_tile = TileAddByDiagDir(origin.tile, exitdir);
	TrackBits     reachable = TrackdirBitsToTrackBits(TrackStatusToTrackdirBits(GetTileTrackStatus(new_tile, TRANSPORT_RAIL, 0)) & DiagdirReachesTrackdirs(exitdir));

	if (Rail90DegTurnDisallowed(GetTileRailType(origin.tile), GetTileRailType(new_tile))) reachable &= ~TrackCrossesTracks(TrackdirToTrack(origin.trackdir));

	bool res_made = false;
	ChooseTrainTrack(v, new_tile, exitdir, reachable, true, &res_made, mark_as_stuck);

	if (!res_made) {
		/* Free the depot reservation as well. */
		if (v->track == TRACK_BIT_DEPOT) SetDepotReservation(v->tile, false);
		return false;
	}

	if (HasBit(v->flags, VRF_TRAIN_STUCK)) {
		v->wait_counter = 0;
		SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
	}
	ClrBit(v->flags, VRF_TRAIN_STUCK);
	return true;
}


/**
 * Get the location of the next station to visit.
 * @param station Next station to visit.
 * @return Location of the new station.
 */
TileIndex Train::GetOrderStationLocation(StationID station)
{
	if (station == this->last_station_visited) this->last_station_visited = INVALID_STATION;

	const Station *st = Station::Get(station);
	if (!(st->facilities & FACIL_TRAIN)) {
		/* The destination station has no trainstation tiles. */
		this->IncrementRealOrderIndex();
		return 0;
	}

	return st->xy;
}

/** Goods at the consist have changed, update the graphics, cargo, and acceleration. */
void Train::MarkDirty()
{
	Train *v = this;
	do {
		v->colourmap = PAL_NONE;
		v->UpdateViewport(true, false);
	} while ((v = v->Next()) != nullptr);

	/* need to update acceleration and cached values since the goods on the train changed. */
	this->CargoChanged();
	this->UpdateAcceleration();
}

/**
 * This function looks at the vehicle and updates its speed (cur_speed
 * and subspeed) variables. Furthermore, it returns the distance that
 * the train can drive this tick. #Vehicle::GetAdvanceDistance() determines
 * the distance to drive before moving a step on the map.
 * @return distance to drive.
 */
int Train::UpdateSpeed()
{
	switch (_settings_game.vehicle.train_acceleration_model) {
		default: NOT_REACHED();
		case AM_ORIGINAL:
			return this->DoUpdateSpeed(this->acceleration * (this->GetAccelerationStatus() == AS_BRAKE ? -4 : 2), 0, this->GetCurrentMaxSpeed());

		case AM_REALISTIC:
			return this->DoUpdateSpeed(this->GetAcceleration(), this->GetAccelerationStatus() == AS_BRAKE ? 0 : 2, this->GetCurrentMaxSpeed());
	}
}

/**
 * Trains enters a station, send out a news item if it is the first train, and start loading.
 * @param v Train that entered the station.
 * @param station Station visited.
 */
static void TrainEnterStation(Train *v, StationID station)
{
	v->last_station_visited = station;

	/* check if a train ever visited this station before */
	Station *st = Station::Get(station);
	if (!(st->had_vehicle_of_type & HVOT_TRAIN)) {
		st->had_vehicle_of_type |= HVOT_TRAIN;
		SetDParam(0, st->index);
		AddVehicleNewsItem(
			STR_NEWS_FIRST_TRAIN_ARRIVAL,
			v->owner == _local_company ? NT_ARRIVAL_COMPANY : NT_ARRIVAL_OTHER,
			v->index,
			st->index
		);
		AI::NewEvent(v->owner, new ScriptEventStationFirstVehicle(st->index, v->index));
		Game::NewEvent(new ScriptEventStationFirstVehicle(st->index, v->index));
	}

	v->force_proceed = TFP_NONE;
	SetWindowDirty(WC_VEHICLE_VIEW, v->index);

	v->BeginLoading();

	TriggerStationRandomisation(st, v->tile, SRT_TRAIN_ARRIVES);
	TriggerStationAnimation(st, v->tile, SAT_TRAIN_ARRIVES);
}

/* Check if the vehicle is compatible with the specified tile */
static inline bool CheckCompatibleRail(const Train *v, TileIndex tile)
{
	return IsTileOwner(tile, v->owner) &&
			(!v->IsFrontEngine() || HasBit(v->compatible_railtypes, GetRailType(tile)));
}

/** Data structure for storing engine speed changes of an acceleration type. */
struct AccelerationSlowdownParams {
	byte small_turn; ///< Speed change due to a small turn.
	byte large_turn; ///< Speed change due to a large turn.
	byte z_up;       ///< Fraction to remove when moving up.
	byte z_down;     ///< Fraction to add when moving down.
};

/** Speed update fractions for each acceleration type. */
static const AccelerationSlowdownParams _accel_slowdown[] = {
	/* normal accel */
	{256 / 4, 256 / 2, 256 / 4, 2}, ///< normal
	{256 / 4, 256 / 2, 256 / 4, 2}, ///< monorail
	{0,       256 / 2, 256 / 4, 2}, ///< maglev
};

/**
 * Modify the speed of the vehicle due to a change in altitude.
 * @param v %Train to update.
 * @param old_z Previous height.
 */
static inline void AffectSpeedByZChange(Train *v, int old_z)
{
	if (old_z == v->z_pos || _settings_game.vehicle.train_acceleration_model != AM_ORIGINAL) return;

	const AccelerationSlowdownParams *asp = &_accel_slowdown[GetRailTypeInfo(v->railtype)->acceleration_type];

	if (old_z < v->z_pos) {
		v->cur_speed -= (v->cur_speed * asp->z_up >> 8);
	} else {
		uint16 spd = v->cur_speed + asp->z_down;
		if (spd <= v->gcache.cached_max_track_speed) v->cur_speed = spd;
	}
}

static bool TrainMovedChangeSignals(TileIndex tile, DiagDirection dir)
{
	if (IsTileType(tile, MP_RAILWAY) &&
			GetRailTileType(tile) == RAIL_TILE_SIGNALS) {
		TrackdirBits tracks = TrackBitsToTrackdirBits(GetTrackBits(tile)) & DiagdirReachesTrackdirs(dir);
		Trackdir trackdir = FindFirstTrackdir(tracks);
		if (UpdateSignalsOnSegment(tile,  TrackdirToExitdir(trackdir), GetTileOwner(tile)) == SIGSEG_PBS && HasSignalOnTrackdir(tile, trackdir)) {
			/* A PBS block with a non-PBS signal facing us? */
			if (!IsPbsSignal(GetSignalType(tile, TrackdirToTrack(trackdir)))) return true;
		}
	}
	return false;
}

/** Tries to reserve track under whole train consist. */
void Train::ReserveTrackUnderConsist() const
{
	for (const Train *u = this; u != nullptr; u = u->Next()) {
		if (u->vehstatus & VS_HIDDEN) continue;
		switch (u->track) {
			case TRACK_BIT_WORMHOLE:
				TryReserveRailTrack(u->tile, DiagDirToDiagTrack(GetTunnelBridgeDirection(u->tile)));
				break;
			case TRACK_BIT_DEPOT:
				break;
			default:
				TryReserveRailTrack(u->tile, TrackBitsToTrack(u->track));
				break;
		}
	}
}

/**
 * The train vehicle crashed!
 * Update its status and other parts around it.
 * @param flooded Crash was caused by flooding.
 * @return Number of people killed.
 */
uint Train::Crash(bool flooded)
{
	uint pass = 0;
	if (this->IsFrontEngine()) {
		pass += 2; // driver

		/* Remove the reserved path in front of the train if it is not stuck.
		 * Also clear all reserved tracks the train is currently on. */
		if (!HasBit(this->flags, VRF_TRAIN_STUCK)) FreeTrainTrackReservation(this);

		if (IsBigRailDepotTile(this->tile)) {
			if (this->track & ~TRACK_BIT_DEPOT) {
				for (Train *v = this; v != nullptr; v = v->Next()) {
					v->track &= ~TRACK_BIT_DEPOT;
				}
				InvalidateWindowData(WC_VEHICLE_DEPOT, GetDepotIndex(this->tile));
			}
			/* Remove reserved tracks of platform ahead. */
			TileIndexDiff diff = TileOffsByDiagDir(TrackdirToExitdir(this->GetVehicleTrackdir()));
			for (TileIndex pt_tile = this->tile + diff; IsCompatiblePlatformTile(pt_tile, this->tile) && HasDepotReservation(pt_tile); pt_tile += diff) {
				if (EnsureNoVisibleVehicleOnGround(pt_tile).Failed()) break;
				SetDepotReservation(pt_tile, false);
				MarkTileDirtyByTile(pt_tile);
			}
		}

		for (const Train *v = this; v != nullptr; v = v->Next()) {
			ClearPathReservation(v, v->tile, v->GetVehicleTrackdir());
			if (IsTileType(v->tile, MP_TUNNELBRIDGE)) {
				/* ClearPathReservation will not free the wormhole exit
				 * if the train has just entered the wormhole. */
				SetTunnelBridgeReservation(GetOtherTunnelBridgeEnd(v->tile), false);
			}

			if (v->Next() == nullptr && (IsRailStationTile(v->tile) || IsBigRailDepotTile(v->tile))) {
				/* Remove reserved tracks of platform tiles behind the train. */
				TileIndexDiff diff = TileOffsByDiagDir(TrackdirToExitdir(ReverseTrackdir(v->GetVehicleTrackdir())));
				for (TileIndex pt_tile = v->tile + diff; IsCompatiblePlatformTile(pt_tile, v->tile); pt_tile += diff) {
					if (IsBigRailDepotTile(pt_tile)) {
						if (!HasDepotReservation(pt_tile)) break;
						if (EnsureNoVisibleVehicleOnGround(pt_tile).Failed()) break;
						SetDepotReservation(pt_tile, false);
					} else {
						if (!HasStationReservation(pt_tile)) break;
						if (EnsureNoVisibleVehicleOnGround(pt_tile).Failed()) break;
						SetRailStationReservation(pt_tile, false);
					}
					MarkTileDirtyByTile(pt_tile);
				}
			}
		}

		/* we may need to update crossing we were approaching,
		 * but must be updated after the train has been marked crashed */
		TileIndex crossing = TrainApproachingCrossingTile(this);
		if (crossing != INVALID_TILE) UpdateLevelCrossing(crossing);

		/* Remove the loading indicators (if any) */
		HideFillingPercent(&this->fill_percent_te_id);
	}

	pass += this->GroundVehicleBase::Crash(flooded);
	this->ReserveTrackUnderConsist();

	this->crash_anim_pos = flooded ? 4000 : 1; // max 4440, disappear pretty fast when flooded
	return pass;
}

/**
 * Marks train as crashed and creates an AI event.
 * Doesn't do anything if the train is crashed already.
 * @param v first vehicle of chain
 * @return number of victims (including 2 drivers; zero if train was already crashed)
 */
static uint TrainCrashed(Train *v)
{
	uint num = 0;

	/* do not crash train twice */
	if (!(v->vehstatus & VS_CRASHED)) {
		num = v->Crash();
		AI::NewEvent(v->owner, new ScriptEventVehicleCrashed(v->index, v->tile, ScriptEventVehicleCrashed::CRASH_TRAIN));
		Game::NewEvent(new ScriptEventVehicleCrashed(v->index, v->tile, ScriptEventVehicleCrashed::CRASH_TRAIN));
	}

	return num;
}

/** Temporary data storage for testing collisions. */
struct TrainCollideChecker {
	Train *v; ///< %Vehicle we are testing for collision.
	uint num; ///< Total number of victims if train collided.
};

/**
 * Collision test function.
 * @param v %Train vehicle to test collision with.
 * @param data %Train being examined.
 * @return \c nullptr (always continue search)
 */
static Vehicle *FindTrainCollideEnum(Vehicle *v, void *data)
{
	TrainCollideChecker *tcc = (TrainCollideChecker*)data;

	/* not a train or in depot */
	if (v->type != VEH_TRAIN || Train::From(v)->track == TRACK_BIT_DEPOT) return nullptr;

	/* do not crash into trains of another company. */
	if (v->owner != tcc->v->owner) return nullptr;

	/* get first vehicle now to make most usual checks faster */
	Train *coll = Train::From(v)->First();

	/* can't collide with own wagons */
	if (coll == tcc->v) return nullptr;

	int x_diff = v->x_pos - tcc->v->x_pos;
	int y_diff = v->y_pos - tcc->v->y_pos;

	/* Do fast calculation to check whether trains are not in close vicinity
	 * and quickly reject trains distant enough for any collision.
	 * Differences are shifted by 7, mapping range [-7 .. 8] into [0 .. 15]
	 * Differences are then ORed and then we check for any higher bits */
	uint hash = (y_diff + 7) | (x_diff + 7);
	if (hash & ~15) return nullptr;

	/* Slower check using multiplication */
	int min_diff = (Train::From(v)->gcache.cached_veh_length + 1) / 2 + (tcc->v->gcache.cached_veh_length + 1) / 2 - 1;
	if (x_diff * x_diff + y_diff * y_diff > min_diff * min_diff) return nullptr;

	/* Happens when there is a train under bridge next to bridge head */
	if (abs(v->z_pos - tcc->v->z_pos) > 5) return nullptr;

	/* crash both trains */
	tcc->num += TrainCrashed(tcc->v);
	tcc->num += TrainCrashed(coll);

	/* The crashing of the coll train frees reservation of train v: Reserve again for train v. */
	tcc->v->ReserveTrackUnderConsist();

	return nullptr; // continue searching
}

/**
 * Checks whether the specified train has a collision with another vehicle. If
 * so, destroys this vehicle, and the other vehicle if its subtype has TS_Front.
 * Reports the incident in a flashy news item, modifies station ratings and
 * plays a sound.
 * @param v %Train to test.
 */
static bool CheckTrainCollision(Train *v)
{
	/* can't collide in depot */
	if (v->track == TRACK_BIT_DEPOT) return false;

	assert(v->track == TRACK_BIT_WORMHOLE || TileVirtXY(v->x_pos, v->y_pos) == v->tile);

	TrainCollideChecker tcc;
	tcc.v = v;
	tcc.num = 0;

	/* find colliding vehicles */
	if (v->track == TRACK_BIT_WORMHOLE) {
		FindVehicleOnPos(v->tile, &tcc, FindTrainCollideEnum);
		FindVehicleOnPos(GetOtherTunnelBridgeEnd(v->tile), &tcc, FindTrainCollideEnum);
	} else {
		FindVehicleOnPosXY(v->x_pos, v->y_pos, &tcc, FindTrainCollideEnum);
	}

	/* any dead -> no crash */
	if (tcc.num == 0) return false;

	SetDParam(0, tcc.num);
	AddTileNewsItem(STR_NEWS_TRAIN_CRASH, NT_ACCIDENT, v->tile);

	ModifyStationRatingAround(v->tile, v->owner, -160, 30);
	if (_settings_client.sound.disaster) SndPlayVehicleFx(SND_13_TRAIN_COLLISION, v);
	return true;
}

static Vehicle *CheckTrainAtSignal(Vehicle *v, void *data)
{
	if (v->type != VEH_TRAIN || (v->vehstatus & VS_CRASHED)) return nullptr;

	Train *t = Train::From(v);
	DiagDirection exitdir = *(DiagDirection *)data;

	/* not front engine of a train, inside wormhole or depot, crashed */
	if (!t->IsFrontEngine() || !(t->track & TRACK_BIT_MASK)) return nullptr;

	if (t->cur_speed > 5 || VehicleExitDir(t->direction, t->track) != exitdir) return nullptr;

	return t;
}

/**
 * Move a vehicle chain one movement stop forwards.
 * @param v First vehicle to move.
 * @param nomove Stop moving this and all following vehicles.
 * @param reverse Set to false to not execute the vehicle reversing. This does not change any other logic.
 * @return True if the vehicle could be moved forward, false otherwise.
 */
bool TrainController(Train *v, Vehicle *nomove, bool reverse)
{
	Train *first = v->First();
	Train *prev;
	bool direction_changed = false; // has direction of any part changed?

	/* For every vehicle after and including the given vehicle */
	for (prev = v->Previous(); v != nomove; prev = v, v = v->Next()) {
		DiagDirection enterdir = DIAGDIR_BEGIN;
		bool update_signals_crossing = false; // will we update signals or crossing state?

		GetNewVehiclePosResult gp = GetNewVehiclePos(v);
		if (v->track != TRACK_BIT_WORMHOLE) {
			/* Not inside tunnel */
			if (gp.old_tile == gp.new_tile) {
				/* Staying in the old tile */
				if (v->track == TRACK_BIT_DEPOT) {
					/* Inside depot */
					gp.x = v->x_pos;
					gp.y = v->y_pos;
				} else {
					/* Not inside depot */

					/* Reverse when we are at the end of the track already, do not move to the new position */
					if (v->IsFrontEngine() && !TrainCheckIfLineEnds(v, reverse)) return false;

					uint32 r = VehicleEnterTile(v, gp.new_tile, gp.x, gp.y);
					if (HasBit(r, VETS_CANNOT_ENTER)) {
						goto invalid_rail;
					}
					if (HasBit(r, VETS_ENTERED_STATION)) {
						/* The new position is the end of the platform */
						TrainEnterStation(v, r >> VETS_STATION_ID_OFFSET);
					} else if (HasBit(r, VETS_ENTERED_DEPOT_PLATFORM)) {
						if (HandleTrainEnterDepot(first)) {
							v->UpdatePosition();
							v->UpdateViewport(true, true);
							return false;
						}
					}
				}
			} else {
				/* A new tile is about to be entered. */

				/* Determine what direction we're entering the new tile from */
				enterdir = DiagdirBetweenTiles(gp.old_tile, gp.new_tile);
				assert(IsValidDiagDirection(enterdir));

				/* Get the status of the tracks in the new tile and mask
				 * away the bits that aren't reachable. */
				TrackStatus ts = GetTileTrackStatus(gp.new_tile, TRANSPORT_RAIL, 0, ReverseDiagDir(enterdir));
				TrackdirBits reachable_trackdirs = DiagdirReachesTrackdirs(enterdir);

				TrackdirBits trackdirbits = TrackStatusToTrackdirBits(ts) & reachable_trackdirs;
				TrackBits red_signals = TrackdirBitsToTrackBits(TrackStatusToRedSignals(ts) & reachable_trackdirs);

				TrackBits bits = TrackdirBitsToTrackBits(trackdirbits);
				if (Rail90DegTurnDisallowed(GetTileRailType(gp.old_tile), GetTileRailType(gp.new_tile)) && prev == nullptr) {
					/* We allow wagons to make 90 deg turns, because forbid_90_deg
					 * can be switched on halfway a turn */
					bits &= ~TrackCrossesTracks(FindFirstTrack(v->track));
				}

				if (bits == TRACK_BIT_NONE) goto invalid_rail;

				/* Check if the new tile constrains tracks that are compatible
				 * with the current train, if not, bail out. */
				if (!CheckCompatibleRail(v, gp.new_tile)) goto invalid_rail;

				TrackBits chosen_track;
				if (prev == nullptr) {
					/* Currently the locomotive is active. Determine which one of the
					 * available tracks to choose */
					chosen_track = TrackToTrackBits(ChooseTrainTrack(v, gp.new_tile, enterdir, bits, false, nullptr, true));
					assert(chosen_track & (bits | GetReservedTrackbits(gp.new_tile)));

					if (v->force_proceed != TFP_NONE && IsPlainRailTile(gp.new_tile) && HasSignals(gp.new_tile)) {
						/* For each signal we find decrease the counter by one.
						 * We start at two, so the first signal we pass decreases
						 * this to one, then if we reach the next signal it is
						 * decreased to zero and we won't pass that new signal. */
						Trackdir dir = FindFirstTrackdir(trackdirbits);
						if (HasSignalOnTrackdir(gp.new_tile, dir) ||
								(HasSignalOnTrackdir(gp.new_tile, ReverseTrackdir(dir)) &&
								GetSignalType(gp.new_tile, TrackdirToTrack(dir)) != SIGTYPE_PBS)) {
							/* However, we do not want to be stopped by PBS signals
							 * entered via the back. */
							v->force_proceed = (v->force_proceed == TFP_SIGNAL) ? TFP_STUCK : TFP_NONE;
							SetWindowDirty(WC_VEHICLE_VIEW, v->index);
						}
					}

					/* Check if it's a red signal and that force proceed is not clicked. */
					if ((red_signals & chosen_track) && v->force_proceed == TFP_NONE) {
						/* In front of a red signal */
						Trackdir i = FindFirstTrackdir(trackdirbits);

						/* Don't handle stuck trains here. */
						if (HasBit(v->flags, VRF_TRAIN_STUCK)) return false;

						if (!HasSignalOnTrackdir(gp.new_tile, ReverseTrackdir(i))) {
							v->cur_speed = 0;
							v->subspeed = 0;
							v->progress = 255; // make sure that every bit of acceleration will hit the signal again, so speed stays 0.
							if (!_settings_game.pf.reverse_at_signals || ++v->wait_counter < _settings_game.pf.wait_oneway_signal * DAY_TICKS * 2) return false;
						} else if (HasSignalOnTrackdir(gp.new_tile, i)) {
							v->cur_speed = 0;
							v->subspeed = 0;
							v->progress = 255; // make sure that every bit of acceleration will hit the signal again, so speed stays 0.
							if (!_settings_game.pf.reverse_at_signals || ++v->wait_counter < _settings_game.pf.wait_twoway_signal * DAY_TICKS * 2) {
								DiagDirection exitdir = TrackdirToExitdir(i);
								TileIndex o_tile = TileAddByDiagDir(gp.new_tile, exitdir);

								exitdir = ReverseDiagDir(exitdir);

								/* check if a train is waiting on the other side */
								if (!HasVehicleOnPos(o_tile, &exitdir, &CheckTrainAtSignal)) return false;
							}
						}

						/* If we would reverse but are currently in a PBS block and
						 * reversing of stuck trains is disabled, don't reverse.
						 * This does not apply if the reason for reversing is a one-way
						 * signal blocking us, because a train would then be stuck forever. */
						if (!_settings_game.pf.reverse_at_signals && !HasOnewaySignalBlockingTrackdir(gp.new_tile, i) &&
								UpdateSignalsOnSegment(v->tile, enterdir, v->owner) == SIGSEG_PBS) {
							v->wait_counter = 0;
							return false;
						}
						goto reverse_train_direction;
					} else {
						TryReserveRailTrack(gp.new_tile, TrackBitsToTrack(chosen_track), false);
					}
				} else {
					/* The wagon is active, simply follow the prev vehicle. */
					if (prev->tile == gp.new_tile) {
						/* Choose the same track as prev */
						if (prev->track == TRACK_BIT_WORMHOLE) {
							/* Vehicles entering tunnels enter the wormhole earlier than for bridges.
							 * However, just choose the track into the wormhole. */
							assert(IsTunnel(prev->tile));
							chosen_track = bits;
						} else {
							chosen_track = prev->track;
						}
					} else {
						/* Choose the track that leads to the tile where prev is.
						 * This case is active if 'prev' is already on the second next tile, when 'v' just enters the next tile.
						 * I.e. when the tile between them has only space for a single vehicle like
						 *  1) horizontal/vertical track tiles and
						 *  2) some orientations of tunnel entries, where the vehicle is already inside the wormhole at 8/16 from the tile edge.
						 *     Is also the train just reversing, the wagon inside the tunnel is 'on' the tile of the opposite tunnel entry.
						 */
						static const TrackBits _connecting_track[DIAGDIR_END][DIAGDIR_END] = {
							{TRACK_BIT_X,     TRACK_BIT_LOWER, TRACK_BIT_NONE,  TRACK_BIT_LEFT },
							{TRACK_BIT_UPPER, TRACK_BIT_Y,     TRACK_BIT_LEFT,  TRACK_BIT_NONE },
							{TRACK_BIT_NONE,  TRACK_BIT_RIGHT, TRACK_BIT_X,     TRACK_BIT_UPPER},
							{TRACK_BIT_RIGHT, TRACK_BIT_NONE,  TRACK_BIT_LOWER, TRACK_BIT_Y    }
						};
						DiagDirection exitdir = DiagdirBetweenTiles(gp.new_tile, prev->tile);
						assert(IsValidDiagDirection(exitdir));
						chosen_track = _connecting_track[enterdir][exitdir];
					}
					chosen_track &= bits;
				}

				/* Make sure chosen track is a valid track */
				assert(
						chosen_track == TRACK_BIT_X     || chosen_track == TRACK_BIT_Y ||
						chosen_track == TRACK_BIT_UPPER || chosen_track == TRACK_BIT_LOWER ||
						chosen_track == TRACK_BIT_LEFT  || chosen_track == TRACK_BIT_RIGHT);

				/* Update XY to reflect the entrance to the new tile, and select the direction to use */
				const byte *b = _initial_tile_subcoord[FIND_FIRST_BIT(chosen_track)][enterdir];
				gp.x = (gp.x & ~0xF) | b[0];
				gp.y = (gp.y & ~0xF) | b[1];
				Direction chosen_dir = (Direction)b[2];

				/* Call the landscape function and tell it that the vehicle entered the tile */
				uint32 r = VehicleEnterTile(v, gp.new_tile, gp.x, gp.y);
				if (HasBit(r, VETS_CANNOT_ENTER)) {
					goto invalid_rail;
				}

				if (!HasBit(r, VETS_ENTERED_WORMHOLE)) {
					Track track = FindFirstTrack(chosen_track);
					Trackdir tdir = TrackDirectionToTrackdir(track, chosen_dir);
					if (v->IsFrontEngine() && HasPbsSignalOnTrackdir(gp.new_tile, tdir)) {
						SetSignalStateByTrackdir(gp.new_tile, tdir, SIGNAL_STATE_RED);
						MarkTileDirtyByTile(gp.new_tile);
					}

					/* Clear any track reservation when the last vehicle leaves the tile */
					if (v->Next() == nullptr) ClearPathReservation(v, v->tile, v->GetVehicleTrackdir());

					v->tile = gp.new_tile;

					if (GetTileRailType(gp.new_tile) != GetTileRailType(gp.old_tile)) {
						v->First()->ConsistChanged(CCF_TRACK);
					}

					v->track = chosen_track;
					assert(v->track);
				}

				/* We need to update signal status, but after the vehicle position hash
				 * has been updated by UpdateInclination() */
				update_signals_crossing = true;

				if (chosen_dir != v->direction) {
					if (prev == nullptr && _settings_game.vehicle.train_acceleration_model == AM_ORIGINAL) {
						const AccelerationSlowdownParams *asp = &_accel_slowdown[GetRailTypeInfo(v->railtype)->acceleration_type];
						DirDiff diff = DirDifference(v->direction, chosen_dir);
						v->cur_speed -= (diff == DIRDIFF_45RIGHT || diff == DIRDIFF_45LEFT ? asp->small_turn : asp->large_turn) * v->cur_speed >> 8;
					}
					direction_changed = true;
					v->direction = chosen_dir;
				}

				if (v->IsFrontEngine()) {
					v->wait_counter = 0;

					/* If we are approaching a crossing that is reserved, play the sound now. */
					TileIndex crossing = TrainApproachingCrossingTile(v);
					if (crossing != INVALID_TILE && HasCrossingReservation(crossing) && _settings_client.sound.ambient) SndPlayTileFx(SND_0E_LEVEL_CROSSING, crossing);

					/* Always try to extend the reservation when entering a tile. */
					CheckNextTrainTile(v);
				}

				if (HasBit(r, VETS_ENTERED_STATION)) {
					/* The new position is the location where we want to stop */
					TrainEnterStation(v, r >> VETS_STATION_ID_OFFSET);
				}
			}
		} else {
			if (IsTileType(gp.new_tile, MP_TUNNELBRIDGE) && HasBit(VehicleEnterTile(v, gp.new_tile, gp.x, gp.y), VETS_ENTERED_WORMHOLE)) {
				/* Perform look-ahead on tunnel exit. */
				if (v->IsFrontEngine()) {
					TryReserveRailTrack(gp.new_tile, DiagDirToDiagTrack(GetTunnelBridgeDirection(gp.new_tile)));
					CheckNextTrainTile(v);
				}
				/* Prevent v->UpdateInclination() being called with wrong parameters.
				 * This could happen if the train was reversed inside the tunnel/bridge. */
				if (gp.old_tile == gp.new_tile) {
					gp.old_tile = GetOtherTunnelBridgeEnd(gp.old_tile);
				}
			} else {
				v->x_pos = gp.x;
				v->y_pos = gp.y;
				v->UpdatePosition();
				if ((v->vehstatus & VS_HIDDEN) == 0) v->Vehicle::UpdateViewport(true);
				continue;
			}
		}

		/* update image of train, as well as delta XY */
		v->UpdateDeltaXY();

		v->x_pos = gp.x;
		v->y_pos = gp.y;
		v->UpdatePosition();

		/* update the Z position of the vehicle */
		int old_z = v->UpdateInclination(gp.new_tile != gp.old_tile, false);

		if (prev == nullptr) {
			/* This is the first vehicle in the train */
			AffectSpeedByZChange(v, old_z);
		}

		if (update_signals_crossing) {
			if (v->IsFrontEngine()) {
				if (TrainMovedChangeSignals(gp.new_tile, enterdir)) {
					/* We are entering a block with PBS signals right now, but
					 * not through a PBS signal. This means we don't have a
					 * reservation right now. As a conventional signal will only
					 * ever be green if no other train is in the block, getting
					 * a path should always be possible. If the player built
					 * such a strange network that it is not possible, the train
					 * will be marked as stuck and the player has to deal with
					 * the problem. */
					if ((!HasReservedTracks(gp.new_tile, v->track) &&
							!TryReserveRailTrack(gp.new_tile, FindFirstTrack(v->track))) ||
							!TryPathReserve(v)) {
						MarkTrainAsStuck(v);
					}
				}
			}

			/* Signals can only change when the first
			 * (above) or the last vehicle moves. */
			if (v->Next() == nullptr) {
				TrainMovedChangeSignals(gp.old_tile, ReverseDiagDir(enterdir));
				if (IsLevelCrossingTile(gp.old_tile)) UpdateLevelCrossing(gp.old_tile);
			}
		}

		/* Do not check on every tick to save some computing time. */
		if (v->IsFrontEngine() && v->tick_counter % _settings_game.pf.path_backoff_interval == 0) CheckNextTrainTile(v);
	}

	if (direction_changed) first->tcache.cached_max_curve_speed = first->GetCurveSpeedLimit();

	return true;

invalid_rail:
	/* We've reached end of line?? */
	if (prev != nullptr) error("Disconnecting train");

reverse_train_direction:
	if (reverse) {
		v->wait_counter = 0;
		v->cur_speed = 0;
		v->subspeed = 0;
		ReverseTrainDirection(v);
	}

	return false;
}

/**
 * Collect trackbits of all crashed train vehicles on a tile
 * @param v Vehicle passed from Find/HasVehicleOnPos()
 * @param data trackdirbits for the result
 * @return nullptr to iterate over all vehicles on the tile.
 */
static Vehicle *CollectTrackbitsFromCrashedVehiclesEnum(Vehicle *v, void *data)
{
	TrackBits *trackbits = (TrackBits *)data;

	if (v->type == VEH_TRAIN && (v->vehstatus & VS_CRASHED) != 0) {
		TrackBits train_tbits = Train::From(v)->track;
		if (train_tbits == TRACK_BIT_WORMHOLE) {
			/* Vehicle is inside a wormhole, v->track contains no useful value then. */
			*trackbits |= DiagDirToDiagTrackBits(GetTunnelBridgeDirection(v->tile));
		} else if (train_tbits != TRACK_BIT_DEPOT) {
			*trackbits |= train_tbits;
		}
	}

	return nullptr;
}

/**
 * Deletes/Clears the last wagon of a crashed train. It takes the engine of the
 * train, then goes to the last wagon and deletes that. Each call to this function
 * will remove the last wagon of a crashed train. If this wagon was on a crossing,
 * or inside a tunnel/bridge, recalculate the signals as they might need updating
 * @param v the Vehicle of which last wagon is to be removed
 */
static void DeleteLastWagon(Train *v)
{
	Train *first = v->First();

	/* Go to the last wagon and delete the link pointing there
	 * *u is then the one-before-last wagon, and *v the last
	 * one which will physically be removed */
	Train *u = v;
	for (; v->Next() != nullptr; v = v->Next()) u = v;
	u->SetNext(nullptr);

	if (first != v) {
		/* Recalculate cached train properties */
		first->ConsistChanged(CCF_ARRANGE);
		/* Update the depot window if the first vehicle is in depot -
		 * if v == first, then it is updated in PreDestructor() */
		if (first->track == TRACK_BIT_DEPOT) {
			SetWindowDirty(WC_VEHICLE_DEPOT, GetDepotIndex(first->tile));
		}
		v->last_station_visited = first->last_station_visited; // for PreDestructor
	}

	/* 'v' shouldn't be accessed after it has been deleted */
	TrackBits trackbits = v->track;
	TileIndex tile = v->tile;
	Owner owner = v->owner;

	delete v;
	v = nullptr; // make sure nobody will try to read 'v' anymore

	if (trackbits == TRACK_BIT_WORMHOLE) {
		/* Vehicle is inside a wormhole, v->track contains no useful value then. */
		trackbits = DiagDirToDiagTrackBits(GetTunnelBridgeDirection(tile));
	}

	Track track = TrackBitsToTrack(trackbits);
	if (HasReservedTracks(tile, trackbits)) {
		UnreserveRailTrack(tile, track);

		/* If there are still crashed vehicles on the tile, give the track reservation to them */
		TrackBits remaining_trackbits = TRACK_BIT_NONE;
		FindVehicleOnPos(tile, &remaining_trackbits, CollectTrackbitsFromCrashedVehiclesEnum);

		/* It is important that these two are the first in the loop, as reservation cannot deal with every trackbit combination */
		assert(TRACK_BEGIN == TRACK_X && TRACK_Y == TRACK_BEGIN + 1);
		for (Track t : SetTrackBitIterator(remaining_trackbits)) TryReserveRailTrack(tile, t);
	}

	/* check if the wagon was on a road/rail-crossing */
	if (IsLevelCrossingTile(tile)) UpdateLevelCrossing(tile);

	/* Update signals */
	if (IsTileType(tile, MP_TUNNELBRIDGE) || IsSmallRailDepotTile(tile)) {
		UpdateSignalsOnSegment(tile, INVALID_DIAGDIR, owner);
	} else {
		SetSignalsOnBothDir(tile, track, owner);
	}
}

/**
 * Rotate all vehicles of a (crashed) train chain randomly to animate the crash.
 * @param v First crashed vehicle.
 */
static void ChangeTrainDirRandomly(Train *v)
{
	static const DirDiff delta[] = {
		DIRDIFF_45LEFT, DIRDIFF_SAME, DIRDIFF_SAME, DIRDIFF_45RIGHT
	};

	do {
		/* We don't need to twist around vehicles if they're not visible */
		if (!(v->vehstatus & VS_HIDDEN)) {
			v->direction = ChangeDir(v->direction, delta[GB(Random(), 0, 2)]);
			/* Refrain from updating the z position of the vehicle when on
			 * a bridge, because UpdateInclination() will put the vehicle under
			 * the bridge in that case */
			if (v->track != TRACK_BIT_WORMHOLE) {
				v->UpdatePosition();
				v->UpdateInclination(false, true);
			} else {
				v->UpdateViewport(false, true);
			}
		}
	} while ((v = v->Next()) != nullptr);
}

/**
 * Handle a crashed train.
 * @param v First train vehicle.
 * @return %Vehicle chain still exists.
 */
static bool HandleCrashedTrain(Train *v)
{
	int state = ++v->crash_anim_pos;

	if (state == 4 && !(v->vehstatus & VS_HIDDEN)) {
		CreateEffectVehicleRel(v, 4, 4, 8, EV_EXPLOSION_LARGE);
	}

	uint32 r;
	if (state <= 200 && Chance16R(1, 7, r)) {
		int index = (r * 10 >> 16);

		Vehicle *u = v;
		do {
			if (--index < 0) {
				r = Random();

				CreateEffectVehicleRel(u,
					GB(r,  8, 3) + 2,
					GB(r, 16, 3) + 2,
					GB(r,  0, 3) + 5,
					EV_EXPLOSION_SMALL);
				break;
			}
		} while ((u = u->Next()) != nullptr);
	}

	if (state <= 240 && !(v->tick_counter & 3)) ChangeTrainDirRandomly(v);

	if (state >= 4440 && !(v->tick_counter & 0x1F)) {
		bool ret = v->Next() != nullptr;
		DeleteLastWagon(v);
		return ret;
	}

	return true;
}

/** Maximum speeds for train that is broken down or approaching line end */
static const uint16 _breakdown_speeds[16] = {
	225, 210, 195, 180, 165, 150, 135, 120, 105, 90, 75, 60, 45, 30, 15, 15
};


/**
 * Train is approaching line end, slow down and possibly reverse
 *
 * @param v front train engine
 * @param signal not line end, just a red signal
 * @param reverse Set to false to not execute the vehicle reversing. This does not change any other logic.
 * @return true iff we did NOT have to reverse
 */
static bool TrainApproachingLineEnd(Train *v, bool signal, bool reverse)
{
	/* Calc position within the current tile */
	uint x = v->x_pos & 0xF;
	uint y = v->y_pos & 0xF;

	/* for diagonal directions, 'x' will be 0..15 -
	 * for other directions, it will be 1, 3, 5, ..., 15 */
	switch (v->direction) {
		case DIR_N : x = ~x + ~y + 25; break;
		case DIR_NW: x = y;            FALLTHROUGH;
		case DIR_NE: x = ~x + 16;      break;
		case DIR_E : x = ~x + y + 9;   break;
		case DIR_SE: x = y;            break;
		case DIR_S : x = x + y - 7;    break;
		case DIR_W : x = ~y + x + 9;   break;
		default: break;
	}

	/* Do not reverse when approaching red signal. Make sure the vehicle's front
	 * does not cross the tile boundary when we do reverse, but as the vehicle's
	 * location is based on their center, use half a vehicle's length as offset.
	 * Multiply the half-length by two for straight directions to compensate that
	 * we only get odd x offsets there. */
	if (!signal && x + (v->gcache.cached_veh_length + 1) / 2 * (IsDiagonalDirection(v->direction) ? 1 : 2) >= TILE_SIZE) {
		/* we are too near the tile end, reverse now */
		v->cur_speed = 0;
		if (reverse) ReverseTrainDirection(v);
		return false;
	}

	/* slow down */
	v->vehstatus |= VS_TRAIN_SLOWING;
	uint16 break_speed = _breakdown_speeds[x & 0xF];
	if (break_speed < v->cur_speed) v->cur_speed = break_speed;

	return true;
}


/**
 * Determines whether train would like to leave the tile
 * @param v train to test
 * @return true iff vehicle is NOT entering or inside a depot or tunnel/bridge
 */
static bool TrainCanLeaveTile(const Train *v)
{
	/* Exit if inside a tunnel/bridge or a depot */
	if (v->track == TRACK_BIT_WORMHOLE || v->track == TRACK_BIT_DEPOT) return false;

	TileIndex tile = v->tile;

	/* entering a tunnel/bridge? */
	if (IsTileType(tile, MP_TUNNELBRIDGE)) {
		DiagDirection dir = GetTunnelBridgeDirection(tile);
		if (DiagDirToDir(dir) == v->direction) return false;
	}

	/* entering a depot? */
	if (IsRailDepotTile(tile)) {
		if (IsBigRailDepot(tile)) {
			Direction dir = DiagDirToDir(GetRailDepotDirection(tile));
			if (dir == v->direction || ReverseDir(dir) == v->direction) return false;
		} else {
			DiagDirection dir = ReverseDiagDir(GetRailDepotDirection(tile));
			if (DiagDirToDir(dir) == v->direction) return false;
		}
	}

	return true;
}


/**
 * Determines whether train is approaching a rail-road crossing
 *   (thus making it barred)
 * @param v front engine of train
 * @return TileIndex of crossing the train is approaching, else INVALID_TILE
 * @pre v in non-crashed front engine
 */
static TileIndex TrainApproachingCrossingTile(const Train *v)
{
	assert(v->IsFrontEngine());
	assert(!(v->vehstatus & VS_CRASHED));

	if (!TrainCanLeaveTile(v)) return INVALID_TILE;

	DiagDirection dir = VehicleExitDir(v->direction, v->track);
	TileIndex tile = v->tile + TileOffsByDiagDir(dir);

	/* not a crossing || wrong axis || unusable rail (wrong type or owner) */
	if (!IsLevelCrossingTile(tile) || DiagDirToAxis(dir) == GetCrossingRoadAxis(tile) ||
			!CheckCompatibleRail(v, tile)) {
		return INVALID_TILE;
	}

	return tile;
}


/**
 * Checks for line end. Also, bars crossing at next tile if needed
 *
 * @param v vehicle we are checking
 * @param reverse Set to false to not execute the vehicle reversing. This does not change any other logic.
 * @return true iff we did NOT have to reverse
 */
static bool TrainCheckIfLineEnds(Train *v, bool reverse)
{
	/* First, handle broken down train */

	int t = v->breakdown_ctr;
	if (t > 1) {
		v->vehstatus |= VS_TRAIN_SLOWING;

		uint16 break_speed = _breakdown_speeds[GB(~t, 4, 4)];
		if (break_speed < v->cur_speed) v->cur_speed = break_speed;
	} else {
		v->vehstatus &= ~VS_TRAIN_SLOWING;
	}

	if (!TrainCanLeaveTile(v)) return true;

	/* Determine the non-diagonal direction in which we will exit this tile */
	DiagDirection dir = VehicleExitDir(v->direction, v->track);
	/* Calculate next tile */
	TileIndex tile = v->tile + TileOffsByDiagDir(dir);

	/* Determine the track status on the next tile */
	TrackStatus ts = GetTileTrackStatus(tile, TRANSPORT_RAIL, 0, ReverseDiagDir(dir));
	TrackdirBits reachable_trackdirs = DiagdirReachesTrackdirs(dir);

	TrackdirBits trackdirbits = TrackStatusToTrackdirBits(ts) & reachable_trackdirs;
	TrackdirBits red_signals = TrackStatusToRedSignals(ts) & reachable_trackdirs;

	/* We are sure the train is not entering a depot, it is detected above */

	/* mask unreachable track bits if we are forbidden to do 90deg turns */
	TrackBits bits = TrackdirBitsToTrackBits(trackdirbits);
	if (Rail90DegTurnDisallowed(GetTileRailType(v->tile), GetTileRailType(tile))) {
		bits &= ~TrackCrossesTracks(FindFirstTrack(v->track));
	}

	/* no suitable trackbits at all || unusable rail (wrong type or owner) */
	if (bits == TRACK_BIT_NONE || !CheckCompatibleRail(v, tile)) {
		return TrainApproachingLineEnd(v, false, reverse);
	}

	/* approaching red signal */
	if ((trackdirbits & red_signals) != 0) return TrainApproachingLineEnd(v, true, reverse);

	/* approaching a rail/road crossing? then make it red */
	if (IsLevelCrossingTile(tile)) MaybeBarCrossingWithSound(tile);

	return true;
}


static bool TrainLocoHandler(Train *v, bool mode)
{
	/* train has crashed? */
	if (v->vehstatus & VS_CRASHED) {
		return mode ? true : HandleCrashedTrain(v); // 'this' can be deleted here
	}

	if (v->force_proceed != TFP_NONE) {
		ClrBit(v->flags, VRF_TRAIN_STUCK);
		SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
	}

	/* train is broken down? */
	if (v->HandleBreakdown()) return true;

	if (HasBit(v->flags, VRF_REVERSING) && v->cur_speed == 0) {
		ReverseTrainDirection(v);
	}

	/* exit if train is stopped */
	if ((v->vehstatus & VS_STOPPED) && v->cur_speed == 0) return true;

	if (v->ContinueServicing()) return true;

	bool valid_order = !v->current_order.IsType(OT_NOTHING) && v->current_order.GetType() != OT_CONDITIONAL;
	if (ProcessOrders(v) && CheckReverseTrain(v)) {
		v->wait_counter = 0;
		v->cur_speed = 0;
		v->subspeed = 0;
		ClrBit(v->flags, VRF_LEAVING_STATION);
		ReverseTrainDirection(v);
		return true;
	} else if (HasBit(v->flags, VRF_LEAVING_STATION)) {
		/* Try to reserve a path when leaving the station as we
		 * might not be marked as wanting a reservation, e.g.
		 * when an overlength train gets turned around in a station. */
		DiagDirection dir = VehicleExitDir(v->direction, v->track);
		if (IsRailDepotTile(v->tile) || IsTileType(v->tile, MP_TUNNELBRIDGE)) dir = INVALID_DIAGDIR;

		if (UpdateSignalsOnSegment(v->tile, dir, v->owner) == SIGSEG_PBS || _settings_game.pf.reserve_paths) {
			TryPathReserve(v, true, true);
		}
		ClrBit(v->flags, VRF_LEAVING_STATION);
	}

	v->HandleLoading(mode);

	if (v->current_order.IsType(OT_LOADING)) return true;

	if (CheckTrainStayInDepot(v)) return true;

	if (!mode) v->ShowVisualEffect();

	/* We had no order but have an order now, do look ahead. */
	if (!valid_order && !v->current_order.IsType(OT_NOTHING)) {
		CheckNextTrainTile(v);
	}

	/* Handle stuck trains. */
	if (!mode && HasBit(v->flags, VRF_TRAIN_STUCK)) {
		++v->wait_counter;

		/* Should we try reversing this tick if still stuck? */
		bool turn_around = v->wait_counter % (_settings_game.pf.wait_for_pbs_path * DAY_TICKS) == 0 && _settings_game.pf.reverse_at_signals;

		if (!turn_around && v->wait_counter % _settings_game.pf.path_backoff_interval != 0 && v->force_proceed == TFP_NONE) return true;
		if (!TryPathReserve(v)) {
			/* Still stuck. */
			if (turn_around) ReverseTrainDirection(v);

			if (HasBit(v->flags, VRF_TRAIN_STUCK) && v->wait_counter > 2 * _settings_game.pf.wait_for_pbs_path * DAY_TICKS) {
				/* Show message to player. */
				if (_settings_client.gui.lost_vehicle_warn && v->owner == _local_company) {
					SetDParam(0, v->index);
					AddVehicleAdviceNewsItem(STR_NEWS_TRAIN_IS_STUCK, v->index);
				}
				v->wait_counter = 0;
			}
			/* Exit if force proceed not pressed, else reset stuck flag anyway. */
			if (v->force_proceed == TFP_NONE) return true;
			ClrBit(v->flags, VRF_TRAIN_STUCK);
			v->wait_counter = 0;
			SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
		}
	}

	if (v->current_order.IsType(OT_LEAVESTATION)) {
		v->current_order.Free();
		SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
		return true;
	}

	int j = v->UpdateSpeed();

	/* we need to invalidate the widget if we are stopping from 'Stopping 0 km/h' to 'Stopped' */
	if (v->cur_speed == 0 && (v->vehstatus & VS_STOPPED)) {
		/* If we manually stopped, we're not force-proceeding anymore. */
		v->force_proceed = TFP_NONE;
		SetWindowDirty(WC_VEHICLE_VIEW, v->index);
	}

	int adv_spd = v->GetAdvanceDistance();
	if (j < adv_spd) {
		/* if the vehicle has speed 0, update the last_speed field. */
		if (v->cur_speed == 0) v->SetLastSpeed();
	} else {
		TrainCheckIfLineEnds(v);
		/* Loop until the train has finished moving. */
		for (;;) {
			j -= adv_spd;
			TrainController(v, nullptr);
			/* Don't continue to move if the train crashed. */
			if (CheckTrainCollision(v)) break;
			/* Determine distance to next map position */
			adv_spd = v->GetAdvanceDistance();

			/* No more moving this tick */
			if (j < adv_spd || v->cur_speed == 0) break;

			OrderType order_type = v->current_order.GetType();
			/* Do not skip waypoints (incl. 'via' stations) when passing through at full speed. */
			if ((order_type == OT_GOTO_WAYPOINT || order_type == OT_GOTO_STATION) &&
						(v->current_order.GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION) &&
						IsTileType(v->tile, MP_STATION) &&
						v->current_order.GetDestination() == GetStationIndex(v->tile)) {
				ProcessOrders(v);
			}
		}
		v->SetLastSpeed();
	}

	for (Train *u = v; u != nullptr; u = u->Next()) {
		if ((u->vehstatus & VS_HIDDEN) != 0) continue;

		u->UpdateViewport(false, false);
	}

	if (v->progress == 0) v->progress = j; // Save unused spd for next time, if TrainController didn't set progress

	return true;
}

/**
 * Get running cost for the train consist.
 * @return Yearly running costs.
 */
Money Train::GetRunningCost() const
{
	Money cost = 0;
	const Train *v = this;

	do {
		const Engine *e = v->GetEngine();
		if (e->u.rail.running_cost_class == INVALID_PRICE) continue;

		uint cost_factor = GetVehicleProperty(v, PROP_TRAIN_RUNNING_COST_FACTOR, e->u.rail.running_cost);
		if (cost_factor == 0) continue;

		/* Halve running cost for multiheaded parts */
		if (v->IsMultiheaded()) cost_factor /= 2;

		cost += GetPrice(e->u.rail.running_cost_class, cost_factor, e->GetGRF());
	} while ((v = v->GetNextVehicle()) != nullptr);

	return cost;
}

/**
 * Update train vehicle data for a tick.
 * @return True if the vehicle still exists, false if it has ceased to exist (front of consists only).
 */
bool Train::Tick()
{
	PerformanceAccumulator framerate(PFE_GL_TRAINS);

	this->tick_counter++;

	if (this->IsFrontEngine()) {
		if (!(this->vehstatus & VS_STOPPED) || this->cur_speed > 0) this->running_ticks++;

		this->current_order_time++;

		if (!TrainLocoHandler(this, false)) return false;

		return TrainLocoHandler(this, true);
	} else if (this->IsFreeWagon() && (this->vehstatus & VS_CRASHED)) {
		/* Delete flooded standalone wagon chain */
		if (++this->crash_anim_pos >= 4400) {
			delete this;
			return false;
		}
	}

	return true;
}

/**
 * Check whether a train needs service, and if so, find a depot or service it.
 * @return v %Train to check.
 */
static void CheckIfTrainNeedsService(Train *v)
{
	if (Company::Get(v->owner)->settings.vehicle.servint_trains == 0 || !v->NeedsAutomaticServicing()) return;
	if (v->IsChainInDepot()) {
		VehicleServiceInDepot(v);
		return;
	}

	uint max_penalty;
	switch (_settings_game.pf.pathfinder_for_trains) {
		case VPF_NPF:  max_penalty = _settings_game.pf.npf.maximum_go_to_depot_penalty;  break;
		case VPF_YAPF: max_penalty = _settings_game.pf.yapf.maximum_go_to_depot_penalty; break;
		default: NOT_REACHED();
	}

	FindDepotData tfdd = FindClosestTrainDepot(v, max_penalty);
	/* Only go to the depot if it is not too far out of our way. */
	if (tfdd.best_length == UINT_MAX || tfdd.best_length > max_penalty) {
		if (v->current_order.IsType(OT_GOTO_DEPOT)) {
			/* If we were already heading for a depot but it has
			 * suddenly moved farther away, we continue our normal
			 * schedule? */
			v->current_order.MakeDummy();
			SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
		}
		return;
	}

	DepotID depot = GetDepotIndex(tfdd.tile);

	if (v->current_order.IsType(OT_GOTO_DEPOT) &&
			v->current_order.GetDestination() != depot &&
			!Chance16(3, 16)) {
		return;
	}

	SetBit(v->gv_flags, GVF_SUPPRESS_IMPLICIT_ORDERS);
	v->current_order.MakeGoToDepot(depot, ODTFB_SERVICE);
	v->dest_tile = tfdd.tile;
	SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
}

/** Update day counters of the train vehicle. */
void Train::OnNewDay()
{
	AgeVehicle(this);

	if ((++this->day_counter & 7) == 0) DecreaseVehicleValue(this);

	if (this->IsFrontEngine()) {
		CheckVehicleBreakdown(this);

		CheckIfTrainNeedsService(this);

		CheckOrders(this);

		/* update destination */
		if (this->current_order.IsType(OT_GOTO_STATION)) {
			TileIndex tile = Station::Get(this->current_order.GetDestination())->train_station.tile;
			if (tile != INVALID_TILE) this->dest_tile = tile;
		}

		if (this->running_ticks != 0) {
			/* running costs */
			CommandCost cost(EXPENSES_TRAIN_RUN, this->GetRunningCost() * this->running_ticks / (DAYS_IN_YEAR  * DAY_TICKS));

			this->profit_this_year -= cost.GetCost();
			this->running_ticks = 0;

			SubtractMoneyFromCompanyFract(this->owner, cost);

			SetWindowDirty(WC_VEHICLE_DETAILS, this->index);
			SetWindowClassesDirty(WC_TRAINS_LIST);
		}
	}
}

/**
 * Get the tracks of the train vehicle.
 * @return Current tracks of the vehicle.
 */
Trackdir Train::GetVehicleTrackdir() const
{
	if (this->vehstatus & VS_CRASHED) return INVALID_TRACKDIR;

	if (this->IsInDepot()) {
		/* We'll assume the train is facing outwards */
		if (this->track == TRACK_BIT_DEPOT)
			return DiagDirToDiagTrackdir(DirToDiagDir(this->direction)); // Train in depot
		Track track = FindFirstTrack(this->track & ~TRACK_BIT_DEPOT);
		assert(IsValidTrack(track));
		return TrackDirectionToTrackdir(track, this->direction);
	}

	if (this->track == TRACK_BIT_WORMHOLE) {
		/* train in tunnel or on bridge, so just use its direction and assume a diagonal track */
		return DiagDirToDiagTrackdir(DirToDiagDir(this->direction));
	}

	return TrackDirectionToTrackdir(FindFirstTrack(this->track), this->direction);
}
