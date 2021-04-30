/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file ship_cmd.cpp Handling of ships. */

#include "stdafx.h"
#include "ship.h"
#include "landscape.h"
#include "timetable.h"
#include "news_func.h"
#include "company_func.h"
#include "pathfinder/npf/npf_func.h"
#include "depot_base.h"
#include "station_base.h"
#include "newgrf_engine.h"
#include "pathfinder/yapf/yapf.h"
#include "newgrf_sound.h"
#include "spritecache.h"
#include "strings_func.h"
#include "window_func.h"
#include "date_func.h"
#include "vehicle_func.h"
#include "sound_func.h"
#include "ai/ai.hpp"
#include "game/game.hpp"
#include "engine_base.h"
#include "company_base.h"
#include "tunnelbridge_map.h"
#include "zoom_func.h"
#include "framerate_type.h"
#include "industry.h"
#include "industry_map.h"

#include "table/strings.h"

#include "safeguards.h"

/**
 * Determine the effective #WaterClass for a ship travelling on a tile.
 * @param tile Tile of interest
 * @return the waterclass to be used by the ship.
 */
WaterClass GetEffectiveWaterClass(TileIndex tile)
{
	if (HasTileWaterClass(tile)) return GetWaterClass(tile);
	if (IsTileType(tile, MP_TUNNELBRIDGE)) {
		assert(GetTunnelBridgeTransportType(tile) == TRANSPORT_WATER);
		return WATER_CLASS_CANAL;
	}
	if (IsTileType(tile, MP_RAILWAY)) {
		assert(GetRailGroundType(tile) == RAIL_GROUND_WATER);
		return WATER_CLASS_SEA;
	}
	NOT_REACHED();
}

static const uint16 _ship_sprites[] = {0x0E5D, 0x0E55, 0x0E65, 0x0E6D};

template <>
bool IsValidImageIndex<VEH_SHIP>(uint8 image_index)
{
	return image_index < lengthof(_ship_sprites);
}

static inline TrackBits GetTileShipTrackStatus(TileIndex tile)
{
	return TrackStatusToTrackBits(GetTileTrackStatus(tile, TRANSPORT_WATER, 0));
}

static void GetShipIcon(EngineID engine, EngineImageType image_type, VehicleSpriteSeq *result)
{
	const Engine *e = Engine::Get(engine);
	uint8 spritenum = e->u.ship.image_index;

	if (is_custom_sprite(spritenum)) {
		GetCustomVehicleIcon(engine, DIR_W, image_type, result);
		if (result->IsValid()) return;

		spritenum = e->original_image_index;
	}

	assert(IsValidImageIndex<VEH_SHIP>(spritenum));
	result->Set(DIR_W + _ship_sprites[spritenum]);
}

void DrawShipEngine(int left, int right, int preferred_x, int y, EngineID engine, PaletteID pal, EngineImageType image_type)
{
	VehicleSpriteSeq seq;
	GetShipIcon(engine, image_type, &seq);

	Rect rect;
	seq.GetBounds(&rect);
	preferred_x = Clamp(preferred_x,
			left - UnScaleGUI(rect.left),
			right - UnScaleGUI(rect.right));

	seq.Draw(preferred_x, y, pal, pal == PALETTE_CRASH);
}

/**
 * Get the size of the sprite of a ship sprite heading west (used for lists).
 * @param engine The engine to get the sprite from.
 * @param[out] width The width of the sprite.
 * @param[out] height The height of the sprite.
 * @param[out] xoffs Number of pixels to shift the sprite to the right.
 * @param[out] yoffs Number of pixels to shift the sprite downwards.
 * @param image_type Context the sprite is used in.
 */
void GetShipSpriteSize(EngineID engine, uint &width, uint &height, int &xoffs, int &yoffs, EngineImageType image_type)
{
	VehicleSpriteSeq seq;
	GetShipIcon(engine, image_type, &seq);

	Rect rect;
	seq.GetBounds(&rect);

	width  = UnScaleGUI(rect.right - rect.left + 1);
	height = UnScaleGUI(rect.bottom - rect.top + 1);
	xoffs  = UnScaleGUI(rect.left);
	yoffs  = UnScaleGUI(rect.top);
}

void Ship::GetImage(Direction direction, EngineImageType image_type, VehicleSpriteSeq *result) const
{
	uint8 spritenum = this->spritenum;

	if (image_type == EIT_ON_MAP) direction = this->rotation;

	if (is_custom_sprite(spritenum)) {
		GetCustomVehicleSprite(this, direction, image_type, result);
		if (result->IsValid()) return;

		spritenum = this->GetEngine()->original_image_index;
	}

	assert(IsValidImageIndex<VEH_SHIP>(spritenum));
	result->Set(_ship_sprites[spritenum] + direction);
}

static const Depot *FindClosestShipDepot(const Vehicle *v, uint max_distance)
{
	/* Find the closest depot */
	const Depot *best_depot = nullptr;
	/* If we don't have a maximum distance, i.e. distance = 0,
	 * we want to find any depot so the best distance of no
	 * depot must be more than any correct distance. On the
	 * other hand if we have set a maximum distance, any depot
	 * further away than max_distance can safely be ignored. */
	uint best_dist = max_distance == 0 ? UINT_MAX : max_distance + 1;

	for (const Depot *depot : Depot::Iterate()) {
		TileIndex tile = depot->xy;
		if (IsShipDepotTile(tile) && IsTileOwner(tile, v->owner)) {
			uint dist = DistanceManhattan(tile, v->tile);
			if (dist < best_dist) {
				best_dist = dist;
				best_depot = depot;
			}
		}
	}

	return best_depot;
}

static void CheckIfShipNeedsService(Vehicle *v)
{
	if (Company::Get(v->owner)->settings.vehicle.servint_ships == 0 || !v->NeedsAutomaticServicing()) return;
	if (v->IsChainInDepot()) {
		VehicleServiceInDepot(v);
		return;
	}

	uint max_distance;
	switch (_settings_game.pf.pathfinder_for_ships) {
		case VPF_NPF:  max_distance = _settings_game.pf.npf.maximum_go_to_depot_penalty  / NPF_TILE_LENGTH;  break;
		case VPF_YAPF: max_distance = _settings_game.pf.yapf.maximum_go_to_depot_penalty / YAPF_TILE_LENGTH; break;
		default: NOT_REACHED();
	}

	const Depot *depot = FindClosestShipDepot(v, max_distance);

	if (depot == nullptr) {
		if (v->current_order.IsType(OT_GOTO_DEPOT)) {
			v->current_order.MakeDummy();
			SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
		}
		return;
	}

	v->current_order.MakeGoToDepot(depot->index, ODTFB_SERVICE);
	v->SetDestTile(depot->xy);
	SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
}

/**
 * Update the caches of this ship.
 */
void Ship::UpdateCache()
{
	const ShipVehicleInfo *svi = ShipVehInfo(this->engine_type);

	/* Get speed fraction for the current water type. Aqueducts are always canals. */
	bool is_ocean = GetEffectiveWaterClass(this->tile) == WATER_CLASS_SEA;
	uint raw_speed = GetVehicleProperty(this, PROP_SHIP_SPEED, svi->max_speed);
	this->vcache.cached_max_speed = svi->ApplyWaterClassSpeedFrac(raw_speed, is_ocean);

	/* Update cargo aging period. */
	this->vcache.cached_cargo_age_period = GetVehicleProperty(this, PROP_SHIP_CARGO_AGE_PERIOD, EngInfo(this->engine_type)->cargo_age_period);

	this->UpdateVisualEffect();
}

Money Ship::GetRunningCost() const
{
	const Engine *e = this->GetEngine();
	uint cost_factor = GetVehicleProperty(this, PROP_SHIP_RUNNING_COST_FACTOR, e->u.ship.running_cost);
	return GetPrice(PR_RUNNING_SHIP, cost_factor, e->GetGRF());
}

void Ship::OnNewDay()
{
	if ((++this->day_counter & 7) == 0) {
		DecreaseVehicleValue(this);
	}

	CheckVehicleBreakdown(this);
	AgeVehicle(this);
	CheckIfShipNeedsService(this);

	CheckOrders(this);

	if (this->running_ticks == 0) return;

	CommandCost cost(EXPENSES_SHIP_RUN, this->GetRunningCost() * this->running_ticks / (DAYS_IN_YEAR * DAY_TICKS));

	this->profit_this_year -= cost.GetCost();
	this->running_ticks = 0;

	SubtractMoneyFromCompanyFract(this->owner, cost);

	SetWindowDirty(WC_VEHICLE_DETAILS, this->index);
	/* we need this for the profit */
	SetWindowClassesDirty(WC_SHIPS_LIST);
}

Trackdir Ship::GetVehicleTrackdir() const
{
	if (this->vehstatus & VS_CRASHED) return INVALID_TRACKDIR;

	if (this->IsInDepot()) {
		/* We'll assume the ship is facing outwards */
		return DiagDirToDiagTrackdir(GetShipDepotDirection(this->tile));
	}

	if (this->state == TRACK_BIT_WORMHOLE) {
		/* ship on aqueduct, so just use his direction and assume a diagonal track */
		return DiagDirToDiagTrackdir(DirToDiagDir(this->direction));
	}

	return TrackDirectionToTrackdir(FindFirstTrack(this->state), this->direction);
}

void Ship::MarkDirty()
{
	this->colourmap = PAL_NONE;
	this->UpdateViewport(true, false);
	this->UpdateCache();
}

static void PlayShipSound(const Vehicle *v)
{
	if (!PlayVehicleSound(v, VSE_START)) {
		SndPlayVehicleFx(ShipVehInfo(v->engine_type)->sfx, v);
	}
}

void Ship::PlayLeaveStationSound() const
{
	PlayShipSound(this);
}

TileIndex Ship::GetOrderStationLocation(StationID station)
{
	if (station == this->last_station_visited) this->last_station_visited = INVALID_STATION;

	const Station *st = Station::Get(station);
	if (CanVehicleUseStation(this, st)) {
		return st->xy;
	} else {
		this->IncrementRealOrderIndex();
		return 0;
	}
}

void Ship::UpdateDeltaXY()
{
	static const int8 _delta_xy_table[8][4] = {
		/* y_extent, x_extent, y_offs, x_offs */
		{ 6,  6,  -3,  -3}, // N
		{ 6, 32,  -3, -16}, // NE
		{ 6,  6,  -3,  -3}, // E
		{32,  6, -16,  -3}, // SE
		{ 6,  6,  -3,  -3}, // S
		{ 6, 32,  -3, -16}, // SW
		{ 6,  6,  -3,  -3}, // W
		{32,  6, -16,  -3}, // NW
	};

	const int8 *bb = _delta_xy_table[this->rotation];
	this->x_offs        = bb[3];
	this->y_offs        = bb[2];
	this->x_extent      = bb[1];
	this->y_extent      = bb[0];
	this->z_extent      = 6;

	if (this->direction != this->rotation) {
		/* If we are rotating, then it is possible the ship was moved to its next position. In that
		 * case, because we are still showing the old direction, the ship will appear to glitch sideways
		 * slightly. We can work around this by applying an additional offset to make the ship appear
		 * where it was before it moved. */
		this->x_offs -= this->x_pos - this->rotation_x_pos;
		this->y_offs -= this->y_pos - this->rotation_y_pos;
	}
}

/**
 * Test-procedure for HasVehicleOnPos to check for any ships which are visible and not stopped by the player.
 */
static Vehicle *EnsureNoMovingShipProc(Vehicle *v, void *data)
{
	return v->type == VEH_SHIP && (v->vehstatus & (VS_HIDDEN | VS_STOPPED)) == 0 ? v : nullptr;
}

static bool CheckShipLeaveDepot(Ship *v)
{
	if (!v->IsChainInDepot()) return false;

	if (v->current_order.IsWaitTimetabled())
		v->HandleWaiting(false);
	if (v->current_order.IsType(OT_WAITING))
		return true;
	
	/* We are leaving a depot, but have to go to the exact same one; re-enter */
	if (v->current_order.IsType(OT_GOTO_DEPOT) &&
			IsShipDepotTile(v->tile) && GetDepotIndex(v->tile) == v->current_order.GetDestination()) {
		VehicleEnterDepot(v);
		return true;
	}

	/* Don't leave depot if no destination set */
	if (v->dest_tile == 0) return true;

	/* Don't leave depot if another vehicle is already entering/leaving */
	/* This helps avoid CPU load if many ships are set to start at the same time */
	if (HasVehicleOnPos(v->tile, nullptr, &EnsureNoMovingShipProc)) return true;

	TileIndex tile = v->tile;
	Axis axis = GetShipDepotAxis(tile);

	DiagDirection north_dir = ReverseDiagDir(AxisToDiagDir(axis));
	TileIndex north_neighbour = TILE_ADD(tile, TileOffsByDiagDir(north_dir));
	DiagDirection south_dir = AxisToDiagDir(axis);
	TileIndex south_neighbour = TILE_ADD(tile, 2 * TileOffsByDiagDir(south_dir));

	TrackBits north_tracks = DiagdirReachesTracks(north_dir) & GetTileShipTrackStatus(north_neighbour);
	TrackBits south_tracks = DiagdirReachesTracks(south_dir) & GetTileShipTrackStatus(south_neighbour);
	if (north_tracks && south_tracks) {
		/* Ask pathfinder for best direction */
		bool reverse = false;
		switch (_settings_game.pf.pathfinder_for_ships) {
			case VPF_NPF: reverse = NPFShipCheckReverse(v); break;
			case VPF_YAPF: reverse = YapfShipCheckReverse(v); break;
			default: NOT_REACHED();
		}
		if (reverse) north_tracks = TRACK_BIT_NONE;
	}

	if (north_tracks) {
		/* Leave towards north */
		v->rotation = v->direction = DiagDirToDir(north_dir);
	} else if (south_tracks) {
		/* Leave towards south */
		v->rotation = v->direction = DiagDirToDir(south_dir);
	} else {
		/* Both ways blocked */
		return false;
	}

	v->state = AxisToTrackBits(axis);
	v->vehstatus &= ~VS_HIDDEN;

	v->cur_speed = 0;
	v->UpdateViewport(true, true);
	SetWindowDirty(WC_VEHICLE_DEPOT, v->tile);

	PlayShipSound(v);
	VehicleServiceInDepot(v);
	InvalidateWindowData(WC_VEHICLE_DEPOT, v->tile);
	SetWindowClassesDirty(WC_SHIPS_LIST);

	return false;
}

static bool ShipAccelerate(Vehicle *v)
{
	uint spd;
	byte t;

	spd = std::min<uint>(v->cur_speed + 1, v->vcache.cached_max_speed);
	spd = std::min<uint>(spd, v->current_order.GetMaxSpeed() * 2);

	/* updates statusbar only if speed have changed to save CPU time */
	if (spd != v->cur_speed) {
		v->cur_speed = spd;
		SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
	}

	/* Convert direction-independent speed into direction-dependent speed. (old movement method) */
	spd = v->GetOldAdvanceSpeed(spd);

	if (spd == 0) return false;
	if ((byte)++spd == 0) return true;

	v->progress = (t = v->progress) - (byte)spd;

	return (t < v->progress);
}

/**
 * Ship arrives at a dock. If it is the first time, send out a news item.
 * @param v  Ship that arrived.
 * @param st Station being visited.
 */
static void ShipArrivesAt(const Vehicle *v, Station *st)
{
	/* Check if station was ever visited before */
	if (!(st->had_vehicle_of_type & HVOT_SHIP)) {
		st->had_vehicle_of_type |= HVOT_SHIP;

		SetDParam(0, st->index);
		AddVehicleNewsItem(
			STR_NEWS_FIRST_SHIP_ARRIVAL,
			(v->owner == _local_company) ? NT_ARRIVAL_COMPANY : NT_ARRIVAL_OTHER,
			v->index,
			st->index
		);
		AI::NewEvent(v->owner, new ScriptEventStationFirstVehicle(st->index, v->index));
		Game::NewEvent(new ScriptEventStationFirstVehicle(st->index, v->index));
	}
}


/**
 * Runs the pathfinder to choose a track to continue along.
 *
 * @param v Ship to navigate
 * @param tile Tile, the ship is about to enter
 * @param enterdir Direction of entering
 * @param tracks Available track choices on \a tile
 * @return Track to choose, or INVALID_TRACK when to reverse.
 */
static Track ChooseShipTrack(Ship *v, TileIndex tile, DiagDirection enterdir, TrackBits tracks)
{
	assert(IsValidDiagDirection(enterdir));

	bool path_found = true;
	Track track;

	if (v->dest_tile == 0) {
		/* No destination, don't invoke pathfinder. */
		track = TrackBitsToTrack(v->state);
		if (!IsDiagonalTrack(track)) track = TrackToOppositeTrack(track);
		if (!HasBit(tracks, track)) track = FindFirstTrack(tracks);
		path_found = false;
	} else {
		/* Attempt to follow cached path. */
		if (!v->path.empty()) {
			track = TrackdirToTrack(v->path.front());

			if (HasBit(tracks, track)) {
				v->path.pop_front();
				/* HandlePathfindResult() is not called here because this is not a new pathfinder result. */
				return track;
			}

			/* Cached path is invalid so continue with pathfinder. */
			v->path.clear();
		}

		switch (_settings_game.pf.pathfinder_for_ships) {
			case VPF_NPF: track = NPFShipChooseTrack(v, path_found); break;
			case VPF_YAPF: track = YapfShipChooseTrack(v, tile, enterdir, tracks, path_found, v->path); break;
			default: NOT_REACHED();
		}
	}

	v->HandlePathfindingResult(path_found);
	return track;
}

/**
 * Get the available water tracks on a tile for a ship entering a tile.
 * @param tile The tile about to enter.
 * @param dir The entry direction.
 * @return The available trackbits on the next tile.
 */
static inline TrackBits GetAvailShipTracks(TileIndex tile, DiagDirection dir)
{
	TrackBits tracks = GetTileShipTrackStatus(tile) & DiagdirReachesTracks(dir);

	return tracks;
}

static const byte _ship_subcoord[4][6][3] = {
	{
		{15, 8, 1},
		{ 0, 0, 0},
		{ 0, 0, 0},
		{15, 8, 2},
		{15, 7, 0},
		{ 0, 0, 0},
	},
	{
		{ 0, 0, 0},
		{ 8, 0, 3},
		{ 7, 0, 2},
		{ 0, 0, 0},
		{ 8, 0, 4},
		{ 0, 0, 0},
	},
	{
		{ 0, 8, 5},
		{ 0, 0, 0},
		{ 0, 7, 6},
		{ 0, 0, 0},
		{ 0, 0, 0},
		{ 0, 8, 4},
	},
	{
		{ 0, 0, 0},
		{ 8, 15, 7},
		{ 0, 0, 0},
		{ 8, 15, 6},
		{ 0, 0, 0},
		{ 7, 15, 0},
	}
};

/**
 * Test if a ship is in the centre of a lock and should move up or down.
 * @param v Ship being tested.
 * @return 0 if ship is not moving in lock, or -1 to move down, 1 to move up.
 */
static int ShipTestUpDownOnLock(const Ship *v)
{
	/* Suitable tile? */
	if (!IsTileType(v->tile, MP_WATER) || !IsLock(v->tile) || GetLockPart(v->tile) != LOCK_PART_MIDDLE) return 0;

	/* Must be at the centre of the lock */
	if ((v->x_pos & 0xF) != 8 || (v->y_pos & 0xF) != 8) return 0;

	DiagDirection diagdir = GetInclinedSlopeDirection(GetTileSlope(v->tile));
	assert(IsValidDiagDirection(diagdir));

	if (DirToDiagDir(v->direction) == diagdir) {
		/* Move up */
		return (v->z_pos < GetTileMaxZ(v->tile) * (int)TILE_HEIGHT) ? 1 : 0;
	} else {
		/* Move down */
		return (v->z_pos > GetTileZ(v->tile) * (int)TILE_HEIGHT) ? -1 : 0;
	}
}

/**
 * Test and move a ship up or down in a lock.
 * @param v Ship to move.
 * @return true iff ship is moving up or down in a lock.
 */
static bool ShipMoveUpDownOnLock(Ship *v)
{
	/* Moving up/down through lock */
	int dz = ShipTestUpDownOnLock(v);
	if (dz == 0) return false;

	if (v->cur_speed != 0) {
		v->cur_speed = 0;
		SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
	}

	if ((v->tick_counter & 7) == 0) {
		v->z_pos += dz;
		v->UpdatePosition();
		v->UpdateViewport(true, true);
	}

	return true;
}

/**
 * Test if a tile is a docking tile for the given station.
 * @param tile Docking tile to test.
 * @param station Destination station.
 * @return true iff docking tile is next to station.
 */
bool IsShipDestinationTile(TileIndex tile, StationID station)
{
	assert(IsDockingTile(tile));
	/* Check each tile adjacent to docking tile. */
	for (DiagDirection d = DIAGDIR_BEGIN; d != DIAGDIR_END; d++) {
		TileIndex t = tile + TileOffsByDiagDir(d);
		if (!IsValidTile(t)) continue;
		if (IsDockTile(t) && GetStationIndex(t) == station && IsValidDockingDirectionForDock(t, d)) return true;
		if (IsTileType(t, MP_INDUSTRY)) {
			const Industry *i = Industry::GetByTile(t);
			if (i->neutral_station != nullptr && i->neutral_station->index == station) return true;
		}
		if (IsTileType(t, MP_STATION) && IsOilRig(t) && GetStationIndex(t) == station) return true;
	}
	return false;
}

static void ShipController(Ship *v)
{
	uint32 r;
	const byte *b;
	Track track;
	TrackBits tracks;

	v->tick_counter++;
	v->current_order_time++;

	if (v->HandleBreakdown()) return;

	if (v->vehstatus & VS_STOPPED) return;

	ProcessOrders(v);
	v->HandleLoading();

	if (v->current_order.IsType(OT_LOADING)) return;

	if (CheckShipLeaveDepot(v)) return;

	v->ShowVisualEffect();

	/* Rotating on spot */
	if (v->direction != v->rotation) {
		if ((v->tick_counter & 7) == 0) {
			DirDiff diff = DirDifference(v->direction, v->rotation);
			v->rotation = ChangeDir(v->rotation, diff > DIRDIFF_REVERSE ? DIRDIFF_45LEFT : DIRDIFF_45RIGHT);
			/* Invalidate the sprite cache direction to force recalculation of viewport */
			v->sprite_cache.last_direction = INVALID_DIR;
			v->UpdateViewport(true, true);
		}
		return;
	}

	if (ShipMoveUpDownOnLock(v)) return;

	if (!ShipAccelerate(v)) return;

	GetNewVehiclePosResult gp = GetNewVehiclePos(v);
	if (v->state != TRACK_BIT_WORMHOLE) {
		/* Not on a bridge */
		if (gp.old_tile == gp.new_tile) {
			/* Staying in tile */
			if (v->IsInDepot()) {
				gp.x = v->x_pos;
				gp.y = v->y_pos;
			} else {
				/* Not inside depot */
				r = VehicleEnterTile(v, gp.new_tile, gp.x, gp.y);
				if (HasBit(r, VETS_CANNOT_ENTER)) goto reverse_direction;

				/* A leave station order only needs one tick to get processed, so we can
				 * always skip ahead. */
				if (v->current_order.IsType(OT_LEAVESTATION)) {
					v->current_order.Free();
					SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
					/* Test if continuing forward would lead to a dead-end, moving into the dock. */
					DiagDirection exitdir = VehicleExitDir(v->direction, v->state);
					TileIndex tile = TileAddByDiagDir(v->tile, exitdir);
					if (TrackStatusToTrackBits(GetTileTrackStatus(tile, TRANSPORT_WATER, 0, exitdir)) == TRACK_BIT_NONE) goto reverse_direction;
				} else if (v->dest_tile != 0) {
					/* We have a target, let's see if we reached it... */
					if (v->current_order.IsType(OT_GOTO_WAYPOINT) &&
							DistanceManhattan(v->dest_tile, gp.new_tile) <= 3) {
						/* We got within 3 tiles of our target buoy, so let's skip to our
						 * next order */
						UpdateVehicleTimetable(v, true);
						v->IncrementRealOrderIndex();
						v->current_order.MakeDummy();
					} else if (v->current_order.IsType(OT_GOTO_DEPOT) &&
						v->dest_tile == gp.new_tile) {
						/* Depot orders really need to reach the tile */
						if ((gp.x & 0xF) == 8 && (gp.y & 0xF) == 8) {
							VehicleEnterDepot(v);
							return;
						}
					} else if (v->current_order.IsType(OT_GOTO_STATION) && IsDockingTile(gp.new_tile)) {
						/* Process station in the orderlist. */
						Station *st = Station::Get(v->current_order.GetDestination());
						if (st->docking_station.Contains(gp.new_tile) && IsShipDestinationTile(gp.new_tile, st->index)) {
							v->last_station_visited = st->index;
							if (st->facilities & FACIL_DOCK) { // ugly, ugly workaround for problem with ships able to drop off cargo at wrong stations
								ShipArrivesAt(v, st);
								v->BeginLoading();
							} else { // leave stations without docks right away
								v->current_order.MakeLeaveStation();
								v->IncrementRealOrderIndex();
							}
						}
					}
				}
			}
		} else {
			/* New tile */
			if (!IsValidTile(gp.new_tile)) goto reverse_direction;

			DiagDirection diagdir = DiagdirBetweenTiles(gp.old_tile, gp.new_tile);
			assert(diagdir != INVALID_DIAGDIR);
			tracks = GetAvailShipTracks(gp.new_tile, diagdir);
			if (tracks == TRACK_BIT_NONE) goto reverse_direction;

			/* Choose a direction, and continue if we find one */
			track = ChooseShipTrack(v, gp.new_tile, diagdir, tracks);
			if (track == INVALID_TRACK) goto reverse_direction;

			b = _ship_subcoord[diagdir][track];

			gp.x = (gp.x & ~0xF) | b[0];
			gp.y = (gp.y & ~0xF) | b[1];

			/* Call the landscape function and tell it that the vehicle entered the tile */
			r = VehicleEnterTile(v, gp.new_tile, gp.x, gp.y);
			if (HasBit(r, VETS_CANNOT_ENTER)) goto reverse_direction;

			if (!HasBit(r, VETS_ENTERED_WORMHOLE)) {
				v->tile = gp.new_tile;
				v->state = TrackToTrackBits(track);

				/* Update ship cache when the water class changes. Aqueducts are always canals. */
				WaterClass old_wc = GetEffectiveWaterClass(gp.old_tile);
				WaterClass new_wc = GetEffectiveWaterClass(gp.new_tile);
				if (old_wc != new_wc) v->UpdateCache();
			}

			Direction new_direction = (Direction)b[2];
			DirDiff diff = DirDifference(new_direction, v->direction);
			switch (diff) {
				case DIRDIFF_SAME:
				case DIRDIFF_45RIGHT:
				case DIRDIFF_45LEFT:
					/* Continue at speed */
					v->rotation = v->direction = new_direction;
					break;

				default:
					/* Stop for rotation */
					v->cur_speed = 0;
					v->direction = new_direction;
					/* Remember our current location to avoid movement glitch */
					v->rotation_x_pos = v->x_pos;
					v->rotation_y_pos = v->y_pos;
					break;
			}
		}
	} else {
		/* On a bridge */
		if (!IsTileType(gp.new_tile, MP_TUNNELBRIDGE) || !HasBit(VehicleEnterTile(v, gp.new_tile, gp.x, gp.y), VETS_ENTERED_WORMHOLE)) {
			v->x_pos = gp.x;
			v->y_pos = gp.y;
			v->UpdatePosition();
			if ((v->vehstatus & VS_HIDDEN) == 0) v->Vehicle::UpdateViewport(true);
			return;
		}

		/* Ship is back on the bridge head, we need to consume its path
		 * cache entry here as we didn't have to choose a ship track. */
		if (!v->path.empty()) v->path.pop_front();
	}

	/* update image of ship, as well as delta XY */
	v->x_pos = gp.x;
	v->y_pos = gp.y;

getout:
	v->UpdatePosition();
	v->UpdateViewport(true, true);
	return;

reverse_direction:
	v->direction = ReverseDir(v->direction);
	/* Remember our current location to avoid movement glitch */
	v->rotation_x_pos = v->x_pos;
	v->rotation_y_pos = v->y_pos;
	v->cur_speed = 0;
	v->path.clear();
	goto getout;
}

bool Ship::Tick()
{
	PerformanceAccumulator framerate(PFE_GL_SHIPS);

	if (!(this->vehstatus & VS_STOPPED)) this->running_ticks++;

	ShipController(this);

	return true;
}

void Ship::SetDestTile(TileIndex tile)
{
	if (tile == this->dest_tile) return;
	this->path.clear();
	this->dest_tile = tile;
}

/**
 * Build a ship.
 * @param tile     tile of the depot where ship is built.
 * @param flags    type of operation.
 * @param e        the engine to build.
 * @param data     unused.
 * @param[out] ret the vehicle that has been built.
 * @return the cost of this operation or an error.
 */
CommandCost CmdBuildShip(TileIndex tile, DoCommandFlag flags, const Engine *e, uint16 data, Vehicle **ret)
{
	tile = GetShipDepotNorthTile(tile);
	if (flags & DC_EXEC) {
		int x;
		int y;

		const ShipVehicleInfo *svi = &e->u.ship;

		Ship *v = new Ship();
		*ret = v;

		v->owner = _current_company;
		v->tile = tile;
		x = TileX(tile) * TILE_SIZE + TILE_SIZE / 2;
		y = TileY(tile) * TILE_SIZE + TILE_SIZE / 2;
		v->x_pos = x;
		v->y_pos = y;
		v->z_pos = GetSlopePixelZ(x, y);

		v->UpdateDeltaXY();
		v->vehstatus = VS_HIDDEN | VS_STOPPED | VS_DEFPAL;

		v->spritenum = svi->image_index;
		v->cargo_type = e->GetDefaultCargoType();
		v->cargo_cap = svi->capacity;
		v->refit_cap = 0;

		v->last_station_visited = INVALID_STATION;
		v->last_loading_station = INVALID_STATION;
		v->engine_type = e->index;

		v->reliability = e->reliability;
		v->reliability_spd_dec = e->reliability_spd_dec;
		v->max_age = e->GetLifeLengthInDays();
		_new_vehicle_id = v->index;

		v->state = TRACK_BIT_DEPOT;

		v->SetServiceInterval(Company::Get(_current_company)->settings.vehicle.servint_ships);
		v->date_of_last_service = _date;
		v->build_year = _cur_year;
		v->sprite_cache.sprite_seq.Set(SPR_IMG_QUERY);
		v->random_bits = VehicleRandomBits();

		v->UpdateCache();

		if (e->flags & ENGINE_EXCLUSIVE_PREVIEW) SetBit(v->vehicle_flags, VF_BUILT_AS_PROTOTYPE);
		v->SetServiceIntervalIsPercent(Company::Get(_current_company)->settings.vehicle.servint_ispercent);

		v->InvalidateNewGRFCacheOfChain();

		v->cargo_cap = e->DetermineCapacity(v);

		v->InvalidateNewGRFCacheOfChain();

		v->UpdatePosition();
	}

	return CommandCost();
}

bool Ship::FindClosestDepot(TileIndex *location, DestinationID *destination, bool *reverse)
{
	const Depot *depot = FindClosestShipDepot(this, 0);

	if (depot == nullptr) return false;

	if (location    != nullptr) *location    = depot->xy;
	if (destination != nullptr) *destination = depot->index;

	return true;
}
