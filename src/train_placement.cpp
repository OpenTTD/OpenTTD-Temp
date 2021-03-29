/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file train_placement.cpp Handling of trains in depot platforms. */

#include "stdafx.h"
#include "error.h"
#include "news_func.h"
#include "company_func.h"
#include "strings_func.h"
#include "platform_func.h"
#include "depot_base.h"
#include "depot_map.h"
#include "train_placement.h"
#include "train.h"

#include "table/strings.h"

#include "safeguards.h"


/**
 * Check if a train can be placed in a given tile.
 * @param train The train.
 * @param check_tile The tile where we want to check whether it is possible to place the train.
 * @param executing False if testing and true if the call is being executed.
 * @return whether it found a platform to place the train.
 */
bool TrainPlacement::CheckPlacement(const Train *train, TileIndex check_tile, bool executing)
{
	assert(train != nullptr);
	assert(IsRailDepotTile(check_tile));

	RailType rt = GetRailType(check_tile);
	PlacementInfo error_info = PI_FAILED_FREE_WAGGON;
	bool is_big_depot = IsBigRailDepot(check_tile);
	bool succeeded = !train->IsFreeWagon();

	if (succeeded) {
		error_info = PI_FAILED_PLATFORM_TYPE;
		for (const Train *t = train; t != nullptr && succeeded; t = t->Next()) {
			RailType rail_type = Engine::Get(t->engine_type)->u.rail.railtype;
			if (!IsCompatibleRail(rail_type, rt)) succeeded = false;
		}
	}

	if (succeeded && is_big_depot) {
		error_info = PI_FAILED_LENGTH;
		if (train->gcache.cached_total_length > GetPlatformLength(check_tile) * TILE_SIZE) succeeded = false;
	}

	if (succeeded) {
		error_info = PI_FAILED_POWER;
		bool has_power = false;
		for (const Train *t = train; t != nullptr && !has_power; t = t->Next()) {
			if (HasPowerOnRail(train->railtype, rt)) has_power = true;
		}
		if (!has_power) succeeded = false;
	}

	if (succeeded && is_big_depot) {
		error_info = PI_FAILED_RESERVED;

		/* Check whether any tile of the platform is reserved. Don't assume all platform
		 * is reserved as a whole: sections of the platform may be reserved by crashed trains. */
		for (TileIndex tile : GetPlatformTileArea(check_tile)) {
			if (HasDepotReservation(tile)) {
				succeeded = false;
				break;
			}
		}
	}

	if (succeeded && executing) {
		/* Do not check for signals if really not executing and action. */
		error_info = PI_FAILED_SIGNALS;
		SigSegState seg_state = UpdateSignalsOnSegment(check_tile, INVALID_DIAGDIR, train->owner);
		if (train->force_proceed == TFP_NONE && seg_state == SIGSEG_FULL) succeeded = false;
	}

	if (succeeded) error_info = PI_SUCCESS;

	if (error_info > this->info) {
		this->best_tile = check_tile;
		this->info = error_info;

		/* A direction for the train must be choosen: the one that allows the longest train in platform. */
		DiagDirection dir = GetRailDepotDirection(check_tile);
		if (is_big_depot && GetPlatformLength(check_tile, dir) > GetPlatformLength(check_tile, ReverseDiagDir(dir))) {
			dir = ReverseDiagDir(dir);
		}
		this->best_dir = DiagDirToDir(dir);
	}

	return succeeded;
}

/**
 * Before placing a train in the rails of a depot, a valid platform must
 * be found. This function finds a tile for placing the train (and also gets the direction and track).
 * If there is no valid tile, it will be returned as best_tile == INVALID_TILE or info == PI_FAILED_PLATFORM_TYPE.
 * @param t The train we want to place in rails.
 * @param executing False if testing and true if the call is being executed.
 * @pre The train must be inside the rail depot as if it where in a small depot.
 *      (i.e. the track is TRACK_BIT_DEPOT, vehicles are hidden...).
 */
void TrainPlacement::LookForPlaceInDepot(const Train *train, bool executing)
{
	assert(train != nullptr);
	assert(IsRailDepotTile(train->tile));

	/* Initialitzation. */
	bool is_big_depot = IsBigRailDepot(train->tile);
	this->best_tile = (this->placed || !is_big_depot) ? train->tile : GetPlatformExtremeTile(train->tile, DirToDiagDir(train->direction));
	assert(IsSmallRailDepot(this->best_tile) || IsAnyStartPlatformTile(this->best_tile));
	this->best_dir = train->direction;
	this->info = PI_FAILED_PLATFORM_TYPE;

	/* First candidate is the original position of the train. */
	if (CheckPlacement(train, this->best_tile, executing)) return;

	/* Check all platforms. */
	Depot *depot = Depot::GetByTile(train->tile);
	for (std::vector<TileIndex>::iterator it = depot->depot_tiles.begin(); it != depot->depot_tiles.end(); ++it) {
		if (CheckPlacement(train, *it, executing)) return;
	}
}

/**
 * Check if a train can leave now or when other trains
 * move away. It returns whether there is a platform long
 * enough and with the appropriate rail type.
 * @param train The train.
 * @param executing False if testing and true if the call is being executed.
 * @return true iff there is a compatible platform long enough.
 */
bool TrainPlacement::CanFindAppropriatePlatform(const Train *train, bool executing)
{
	this->LookForPlaceInDepot(train, executing);
	return this->info >= PI_WONT_LEAVE;
}


/**
 * Lift a train in a depot: keep the positions of the elements of the chain if needed,
 * and keep also the original tile, direction and track.
 * @param train The train we want to lift.
 * @pre The train must be inside a rail depot.
 *      (i.e. the track is 'valid track | TRACK_BIT_DEPOT' or just 'TRACK_BIT_DEPOT').
 */
void TrainPlacement::LiftTrain(Train *train, DoCommandFlag flags)
{
	assert(train == nullptr || train->IsInDepot());
	assert(train == nullptr || IsRailDepotTile(train->tile));
	assert(this->placed == false);

	/* Lift the train only if we have a train in a big depot. */
	if (train == nullptr || !IsBigRailDepot(train->tile)) return;

	/* Do not lift in recursive commands of autoreplace. */
	if (flags & DC_AUTOREPLACE) return;

	/* If train is not placed... return, because train is already lifted. */
	if ((train->track & ~TRACK_BIT_DEPOT) == 0) return;

	/* Train is placed in rails: lift it. */
	this->placed = true;
	if (flags & DC_EXEC) FreeTrainTrackReservation(train);

	for (Train *t = train; t != nullptr; t = t->Next()) {
		// Lift.
		t->track = TRACK_BIT_DEPOT;
		t->tile = train->tile;
		t->x_pos = train->x_pos;
		t->y_pos = train->y_pos;
		t->UpdatePosition();
		t->UpdateViewport(true, true);

	}

	if ((flags & DC_EXEC) == 0) return;

	SetPlatformReservation(train->tile, false);

	UpdateSignalsOnSegment(train->tile, INVALID_DIAGDIR, train->owner);
}

/**
 * When a train is lifted inside a depot, before starting its way again,
 * must be placed in rails if in a big rail depot; this function does all necessary things to do so.
 * In general, it's the opposite of #LiftTrain
 * @param train The train we want to place in rails.
 * @param flags Associated command flags
 * @pre The train must be inside the big rail depot as if in a small depot.
 *      (i.e. the track is TRACK_BIT_DEPOT, vehicles are hidden...).
 */
void TrainPlacement::PlaceTrain(Train *train, DoCommandFlag flags)
{
	if (train == nullptr) return;
	if (train != train->First()) return;
	if (!IsRailDepotTile(train->tile)) return;
	if (flags & DC_AUTOREPLACE) return;

	bool executing = (flags & DC_EXEC) != 0;

	/* Look for an appropriate platform. */
	this->LookForPlaceInDepot(train, executing);
	assert(!IsBigRailDepot(this->best_tile) || IsAnyStartPlatformTile(this->best_tile));

	if (this->info < PI_FAILED_END || !executing) {
		if (!executing) {
			/* Restore the train. */
			this->best_tile = train->tile;
			this->best_dir = train->direction;
			this->info = PI_SUCCESS;
		}

		if (!this->placed || (this->info < PI_FAILED_END && executing)) {
			for (Train *t = train; t != nullptr; t = t->Next()) {
				t->tile = this->best_tile;
				t->vehstatus |= VS_HIDDEN;
				t->track = TRACK_BIT_DEPOT;
			}
			if (!executing) return;
			train->PowerChanged();
		}

		if (this->info < PI_FAILED_END && executing) {
			/* Train cannot leave until changing the depot. Stop the train and send a message. */
			if (info < PI_WONT_LEAVE) {
				train->vehstatus |= VS_STOPPED;
				/* If vehicle is not stopped and user is the local company, send a message if needed. */
				if ((train->vehstatus & VS_STOPPED) == 0 && train->owner == _local_company && train->IsFrontEngine()) {
					SetDParam(0, train->index);
					AddVehicleAdviceNewsItem(STR_ADVICE_PLATFORM_TYPE + info - PI_ERROR_BEGIN, train->index);
				}
			}
			return;
		}
	}

	assert(this->best_tile != INVALID_TILE);
	assert(this->best_dir != INVALID_DIR);
	assert(IsRailDepotTile(this->best_tile));

	if (executing) {
		train->tile = this->best_tile;
		train->track = TrackToTrackBits(GetRailDepotTrack(this->best_tile));
		train->direction = this->best_dir;
		train->PowerChanged();
	}

	if (IsSmallRailDepot(this->best_tile)) {
		int x = TileX(this->best_tile) * TILE_SIZE + _vehicle_initial_x_fract[DirToDiagDir(this->best_dir)];
		int y = TileY(this->best_tile) * TILE_SIZE + _vehicle_initial_y_fract[DirToDiagDir(this->best_dir)];
		for (Train *t = train; t != nullptr; t = t->Next()) {
			t->tile = this->best_tile;
			t->direction = this->best_dir;
			t->vehstatus |= VS_HIDDEN;
			t->track = TRACK_BIT_DEPOT;
			t->x_pos = x;
			t->y_pos = y;
			t->z_pos = GetSlopePixelZ(x, y);
			t->UpdatePosition();
			t->UpdateViewport(true, true);
		}
		return;
	}

	DiagDirection placing_dir = ReverseDiagDir(DirToDiagDir(this->best_dir));

	static const byte _plat_initial_x_fract[4] = {15, 8, 0,  8};
	static const byte _plat_initial_y_fract[4] = { 8, 0, 8, 15};

	int x = TileX(this->best_tile) * TILE_SIZE | _plat_initial_x_fract[placing_dir];
	int y = TileY(this->best_tile) * TILE_SIZE | _plat_initial_y_fract[placing_dir];

	/* Add the offset for the first vehicle. */
	x += TileIndexDiffCByDiagDir(placing_dir).x * (train->gcache.cached_veh_length + 1) / 2;
	y += TileIndexDiffCByDiagDir(placing_dir).y * (train->gcache.cached_veh_length + 1) / 2;

	/* Proceed placing the train in the given tile.
	 * At this point, the first vehicle contains the direction, tile and track.
	 * We must update positions of all the chain. */
	for (Train *t = train; t != nullptr; t = t->Next()) {
		t->vehstatus &= ~VS_HIDDEN;
		t->direction = this->best_dir;
		t->track = DiagDirToDiagTrackBits(placing_dir) | TRACK_BIT_DEPOT;
		t->x_pos = x;
		t->y_pos = y;
		t->z_pos = GetSlopePixelZ(t->x_pos, t->y_pos);
		t->tile = TileVirtXY(t->x_pos,t->y_pos);

		assert(t->z_pos == train->z_pos);
		assert(IsBigRailDepotTile(t->tile));

		t->UpdatePosition();
		t->UpdateViewport(true, true);

		int advance = t->CalcNextVehicleOffset();
		x += TileIndexDiffCByDiagDir(placing_dir).x * advance;
		y += TileIndexDiffCByDiagDir(placing_dir).y * advance;
	}

	SetPlatformReservation(train->tile, true);

	UpdateSignalsOnSegment(train->tile, INVALID_DIAGDIR, train->owner);
}
