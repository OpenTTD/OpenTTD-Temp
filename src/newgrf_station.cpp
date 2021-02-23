/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_station.cpp Functions for dealing with station classes and custom stations. */

#include "stdafx.h"
#include "debug.h"
#include "station_base.h"
#include "waypoint_base.h"
#include "roadstop_base.h"
#include "newgrf_cargo.h"
#include "newgrf_station.h"
#include "newgrf_spritegroup.h"
#include "newgrf_sound.h"
#include "newgrf_railtype.h"
#include "town.h"
#include "newgrf_town.h"
#include "company_func.h"
#include "tunnelbridge_map.h"
#include "newgrf_animation_base.h"
#include "newgrf_class_func.h"

#include "safeguards.h"


template <typename Tspec, typename Tid, Tid Tmax>
/* static */ void NewGRFClass<Tspec, Tid, Tmax>::InsertDefaults()
{
	/* Set up initial data */
	classes[0].global_id = 'DFLT';
	classes[0].name = STR_STATION_CLASS_DFLT;
	classes[0].Insert(nullptr);

	classes[1].global_id = 'WAYP';
	classes[1].name = STR_STATION_CLASS_WAYP;
	classes[1].Insert(nullptr);
}

template <typename Tspec, typename Tid, Tid Tmax>
bool NewGRFClass<Tspec, Tid, Tmax>::IsUIAvailable(uint index) const
{
	return true;
}

INSTANTIATE_NEWGRF_CLASS_METHODS(StationClass, StationSpec, StationClassID, STAT_CLASS_MAX)

static const uint NUM_STATIONSSPECS_PER_STATION = 255; ///< Maximum number of parts per station.

enum TriggerArea {
	TA_TILE,
	TA_PLATFORM,
	TA_WHOLE,
};

struct ETileArea : TileArea {
	ETileArea(const BaseStation *st, TileIndex tile, TriggerArea ta)
	{
		switch (ta) {
			default: NOT_REACHED();

			case TA_TILE:
				this->tile = tile;
				this->w    = 1;
				this->h    = 1;
				break;

			case TA_PLATFORM: {
				TileIndex start, end;
				Axis axis = GetRailStationAxis(tile);
				TileIndexDiff delta = TileOffsByDiagDir(AxisToDiagDir(axis));

				for (end = tile; IsRailStationTile(end + delta) && IsCompatibleTrainStationTile(end + delta, tile); end += delta) { /* Nothing */ }
				for (start = tile; IsRailStationTile(start - delta) && IsCompatibleTrainStationTile(start - delta, tile); start -= delta) { /* Nothing */ }

				this->tile = start;
				this->w = TileX(end) - TileX(start) + 1;
				this->h = TileY(end) - TileY(start) + 1;
				break;
			}

			case TA_WHOLE:
				st->GetTileArea(this, Station::IsExpected(st) ? STATION_RAIL : STATION_WAYPOINT);
				break;
		}
	}
};


/**
 * Evaluate a tile's position within a station, and return the result in a bit-stuffed format.
 * if not centered: .TNLcCpP, if centered: .TNL..CP
 * - T = Tile layout number (#GetStationGfx)
 * - N = Number of platforms
 * - L = Length of platforms
 * - C = Current platform number from start, c = from end
 * - P = Position along platform from start, p = from end
 * .
 * if centered, C/P start from the centre and c/p are not available.
 * @return Platform information in bit-stuffed format.
 */
uint32 GetPlatformInfo(Axis axis, byte tile, int platforms, int length, int x, int y, bool centred)
{
	uint32 retval = 0;

	if (axis == AXIS_X) {
		Swap(platforms, length);
		Swap(x, y);
	}

	if (centred) {
		x -= platforms / 2;
		y -= length / 2;
		x = Clamp(x, -8, 7);
		y = Clamp(y, -8, 7);
		SB(retval,  0, 4, y & 0xF);
		SB(retval,  4, 4, x & 0xF);
	} else {
		SB(retval,  0, 4, std::min(15, y));
		SB(retval,  4, 4, std::min(15, length - y - 1));
		SB(retval,  8, 4, std::min(15, x));
		SB(retval, 12, 4, std::min(15, platforms - x - 1));
	}
	SB(retval, 16, 4, std::min(15, length));
	SB(retval, 20, 4, std::min(15, platforms));
	SB(retval, 24, 4, tile);

	return retval;
}


/**
 * Find the end of a railway station, from the \a tile, in the direction of \a delta.
 * @param tile Start tile.
 * @param delta Movement direction.
 * @param check_type Stop when the custom station type changes.
 * @param check_axis Stop when the station direction changes.
 * @return Found end of the railway station.
 */
static TileIndex FindRailStationEnd(TileIndex tile, TileIndexDiff delta, bool check_type, bool check_axis)
{
	byte orig_type = 0;
	Axis orig_axis = AXIS_X;
	StationID sid = GetStationIndex(tile);

	if (check_type) orig_type = GetCustomStationSpecIndex(tile);
	if (check_axis) orig_axis = GetRailStationAxis(tile);

	for (;;) {
		TileIndex new_tile = TILE_ADD(tile, delta);

		if (!IsTileType(new_tile, MP_STATION) || GetStationIndex(new_tile) != sid) break;
		if (!HasStationRail(new_tile)) break;
		if (check_type && GetCustomStationSpecIndex(new_tile) != orig_type) break;
		if (check_axis && GetRailStationAxis(new_tile) != orig_axis) break;

		tile = new_tile;
	}
	return tile;
}


static uint32 GetPlatformInfoHelper(TileIndex tile, bool check_type, bool check_axis, bool centred)
{
	int tx = TileX(tile);
	int ty = TileY(tile);
	int sx = TileX(FindRailStationEnd(tile, TileDiffXY(-1,  0), check_type, check_axis));
	int sy = TileY(FindRailStationEnd(tile, TileDiffXY( 0, -1), check_type, check_axis));
	int ex = TileX(FindRailStationEnd(tile, TileDiffXY( 1,  0), check_type, check_axis)) + 1;
	int ey = TileY(FindRailStationEnd(tile, TileDiffXY( 0,  1), check_type, check_axis)) + 1;

	tx -= sx; ex -= sx;
	ty -= sy; ey -= sy;

	return GetPlatformInfo(GetRailStationAxis(tile), GetStationGfx(tile), ex, ey, tx, ty, centred);
}


static uint32 GetRailContinuationInfo(TileIndex tile)
{
	/* Tile offsets and exit dirs for X axis */
	static const Direction x_dir[8] = { DIR_SW, DIR_NE, DIR_SE, DIR_NW, DIR_S, DIR_E, DIR_W, DIR_N };
	static const DiagDirection x_exits[8] = { DIAGDIR_SW, DIAGDIR_NE, DIAGDIR_SE, DIAGDIR_NW, DIAGDIR_SW, DIAGDIR_NE, DIAGDIR_SW, DIAGDIR_NE };

	/* Tile offsets and exit dirs for Y axis */
	static const Direction y_dir[8] = { DIR_SE, DIR_NW, DIR_SW, DIR_NE, DIR_S, DIR_W, DIR_E, DIR_N };
	static const DiagDirection y_exits[8] = { DIAGDIR_SE, DIAGDIR_NW, DIAGDIR_SW, DIAGDIR_NE, DIAGDIR_SE, DIAGDIR_NW, DIAGDIR_SE, DIAGDIR_NW };

	Axis axis = GetRailStationAxis(tile);

	/* Choose appropriate lookup table to use */
	const Direction *dir = axis == AXIS_X ? x_dir : y_dir;
	const DiagDirection *diagdir = axis == AXIS_X ? x_exits : y_exits;

	uint32 res = 0;
	uint i;

	for (i = 0; i < lengthof(x_dir); i++, dir++, diagdir++) {
		TileIndex neighbour_tile = tile + TileOffsByDir(*dir);
		TrackBits trackbits = TrackStatusToTrackBits(GetTileTrackStatus(neighbour_tile, TRANSPORT_RAIL, 0));
		if (trackbits != TRACK_BIT_NONE) {
			/* If there is any track on the tile, set the bit in the second byte */
			SetBit(res, i + 8);

			/* With tunnels and bridges the tile has tracks, but they are not necessarily connected
			 * with the next tile because the ramp is not going in the right direction. */
			if (IsTileType(neighbour_tile, MP_TUNNELBRIDGE) && GetTunnelBridgeDirection(neighbour_tile) != *diagdir) {
				continue;
			}

			/* If any track reaches our exit direction, set the bit in the lower byte */
			if (trackbits & DiagdirReachesTracks(*diagdir)) SetBit(res, i);
		}
	}

	return res;
}


/* Station Resolver Functions */
/* virtual */ uint32 StationScopeResolver::GetRandomBits() const
{
	return (this->st == nullptr ? 0 : this->st->random_bits) | (this->tile == INVALID_TILE ? 0 : GetStationTileRandomBits(this->tile) << 16);
}


/* virtual */ uint32 StationScopeResolver::GetTriggers() const
{
	return this->st == nullptr ? 0 : this->st->waiting_triggers;
}


/**
 * Station variable cache
 * This caches 'expensive' station variable lookups which iterate over
 * several tiles that may be called multiple times per Resolve().
 */
static struct {
	uint32 v40;
	uint32 v41;
	uint32 v45;
	uint32 v46;
	uint32 v47;
	uint32 v49;
	uint8 valid; ///< Bits indicating what variable is valid (for each bit, \c 0 is invalid, \c 1 is valid).
} _svc;

/**
 * Get the town scope associated with a station, if it exists.
 * On the first call, the town scope is created (if possible).
 * @return Town scope, if available.
 */
TownScopeResolver *StationResolverObject::GetTown()
{
	if (this->town_scope == nullptr) {
		Town *t = nullptr;
		if (this->station_scope.st != nullptr) {
			t = this->station_scope.st->town;
		} else if (this->station_scope.tile != INVALID_TILE) {
			t = ClosestTownFromTile(this->station_scope.tile, UINT_MAX);
		}
		if (t == nullptr) return nullptr;
		this->town_scope = new TownScopeResolver(*this, t, this->station_scope.st == nullptr);
	}
	return this->town_scope;
}

/* virtual */ uint32 StationScopeResolver::GetVariable(byte variable, uint32 parameter, bool *available) const
{
	if (this->st == nullptr) {
		/* Station does not exist, so we're in a purchase list or the land slope check callback. */
		switch (variable) {
			case 0x40:
			case 0x41:
			case 0x46:
			case 0x47:
			case 0x49: return 0x2110000;        // Platforms, tracks & position
			case 0x42: return 0;                // Rail type (XXX Get current type from GUI?)
			case 0x43: return GetCompanyInfo(_current_company); // Station owner
			case 0x44: return 2;                // PBS status
			case 0x67: // Land info of nearby tile
				if (this->axis != INVALID_AXIS && this->tile != INVALID_TILE) {
					TileIndex tile = this->tile;
					if (parameter != 0) tile = GetNearbyTile(parameter, tile, true, this->axis); // only perform if it is required

					Slope tileh = GetTileSlope(tile);
					bool swap = (this->axis == AXIS_Y && HasBit(tileh, CORNER_W) != HasBit(tileh, CORNER_E));

					return GetNearbyTileInformation(tile, this->ro.grffile->grf_version >= 8) ^ (swap ? SLOPE_EW : 0);
				}
				break;

			case 0xFA: return Clamp(_date - DAYS_TILL_ORIGINAL_BASE_YEAR, 0, 65535); // Build date, clamped to a 16 bit value
		}

		*available = false;
		return UINT_MAX;
	}

	switch (variable) {
		/* Calculated station variables */
		case 0x40:
			if (!HasBit(_svc.valid, 0)) { _svc.v40 = GetPlatformInfoHelper(this->tile, false, false, false); SetBit(_svc.valid, 0); }
			return _svc.v40;

		case 0x41:
			if (!HasBit(_svc.valid, 1)) { _svc.v41 = GetPlatformInfoHelper(this->tile, true,  false, false); SetBit(_svc.valid, 1); }
			return _svc.v41;

		case 0x42: return GetTerrainType(this->tile) | (GetReverseRailTypeTranslation(GetRailType(this->tile), this->statspec->grf_prop.grffile) << 8);
		case 0x43: return GetCompanyInfo(this->st->owner); // Station owner
		case 0x44: return HasStationReservation(this->tile) ? 7 : 4; // PBS status
		case 0x45:
			if (!HasBit(_svc.valid, 2)) { _svc.v45 = GetRailContinuationInfo(this->tile); SetBit(_svc.valid, 2); }
			return _svc.v45;

		case 0x46:
			if (!HasBit(_svc.valid, 3)) { _svc.v46 = GetPlatformInfoHelper(this->tile, false, false, true); SetBit(_svc.valid, 3); }
			return _svc.v46;

		case 0x47:
			if (!HasBit(_svc.valid, 4)) { _svc.v47 = GetPlatformInfoHelper(this->tile, true,  false, true); SetBit(_svc.valid, 4); }
			return _svc.v47;

		case 0x49:
			if (!HasBit(_svc.valid, 5)) { _svc.v49 = GetPlatformInfoHelper(this->tile, false, true, false); SetBit(_svc.valid, 5); }
			return _svc.v49;

		case 0x4A: // Animation frame of tile
			return GetAnimationFrame(this->tile);

		/* Variables which use the parameter */
		/* Variables 0x60 to 0x65 and 0x69 are handled separately below */
		case 0x66: { // Animation frame of nearby tile
			TileIndex tile = this->tile;
			if (parameter != 0) tile = GetNearbyTile(parameter, tile);
			return this->st->TileBelongsToRailStation(tile) ? GetAnimationFrame(tile) : UINT_MAX;
		}

		case 0x67: { // Land info of nearby tile
			Axis axis = GetRailStationAxis(this->tile);
			TileIndex tile = this->tile;
			if (parameter != 0) tile = GetNearbyTile(parameter, tile); // only perform if it is required

			Slope tileh = GetTileSlope(tile);
			bool swap = (axis == AXIS_Y && HasBit(tileh, CORNER_W) != HasBit(tileh, CORNER_E));

			return GetNearbyTileInformation(tile, this->ro.grffile->grf_version >= 8) ^ (swap ? SLOPE_EW : 0);
		}

		case 0x68: { // Station info of nearby tiles
			TileIndex nearby_tile = GetNearbyTile(parameter, this->tile);

			if (!HasStationTileRail(nearby_tile)) return 0xFFFFFFFF;

			uint32 grfid = this->st->speclist[GetCustomStationSpecIndex(this->tile)].grfid;
			bool perpendicular = GetRailStationAxis(this->tile) != GetRailStationAxis(nearby_tile);
			bool same_station = this->st->TileBelongsToRailStation(nearby_tile);
			uint32 res = GB(GetStationGfx(nearby_tile), 1, 2) << 12 | !!perpendicular << 11 | !!same_station << 10;

			if (IsCustomStationSpecIndex(nearby_tile)) {
				const StationSpecList ssl = BaseStation::GetByTile(nearby_tile)->speclist[GetCustomStationSpecIndex(nearby_tile)];
				res |= 1 << (ssl.grfid != grfid ? 9 : 8) | ssl.localidx;
			}
			return res;
		}

		case 0x6A: { // GRFID of nearby station tiles
			TileIndex nearby_tile = GetNearbyTile(parameter, this->tile);

			if (!HasStationTileRail(nearby_tile)) return 0xFFFFFFFF;
			if (!IsCustomStationSpecIndex(nearby_tile)) return 0;

			const StationSpecList ssl = BaseStation::GetByTile(nearby_tile)->speclist[GetCustomStationSpecIndex(nearby_tile)];
			return ssl.grfid;
		}

		/* General station variables */
		case 0x82: return 50;
		case 0x84: return this->st->string_id;
		case 0x86: return 0;
		case 0xF0: return this->st->facilities;
		case 0xFA: return Clamp(this->st->build_date - DAYS_TILL_ORIGINAL_BASE_YEAR, 0, 65535);
	}

	return this->st->GetNewGRFVariable(this->ro, variable, parameter, available);
}

uint32 Station::GetNewGRFVariable(const ResolverObject &object, byte variable, byte parameter, bool *available) const
{
	switch (variable) {
		case 0x48: { // Accepted cargo types
			CargoID cargo_type;
			uint32 value = 0;

			for (cargo_type = 0; cargo_type < NUM_CARGO; cargo_type++) {
				if (HasBit(this->goods[cargo_type].status, GoodsEntry::GES_ACCEPTANCE)) SetBit(value, cargo_type);
			}
			return value;
		}

		case 0x8A: return this->had_vehicle_of_type;
		case 0xF1: return (this->airport.tile != INVALID_TILE) ? this->airport.GetSpec()->ttd_airport_type : ATP_TTDP_LARGE;
		case 0xF2: return (this->truck_stops != nullptr) ? this->truck_stops->status : 0;
		case 0xF3: return (this->bus_stops != nullptr)   ? this->bus_stops->status   : 0;
		case 0xF6: return this->airport.flags;
		case 0xF7: return GB(this->airport.flags, 8, 8);
	}

	/* Handle cargo variables with parameter, 0x60 to 0x65 and 0x69 */
	if ((variable >= 0x60 && variable <= 0x65) || variable == 0x69) {
		CargoID c = GetCargoTranslation(parameter, object.grffile);

		if (c == CT_INVALID) {
			switch (variable) {
				case 0x62: return 0xFFFFFFFF;
				case 0x64: return 0xFF00;
				default:   return 0;
			}
		}
		const GoodsEntry *ge = &this->goods[c];

		switch (variable) {
			case 0x60: return std::min(ge->cargo.TotalCount(), 4095u);
			case 0x61: return ge->HasVehicleEverTriedLoading() ? ge->time_since_pickup : 0;
			case 0x62: return ge->HasRating() ? ge->rating : 0xFFFFFFFF;
			case 0x63: return ge->cargo.DaysInTransit();
			case 0x64: return ge->HasVehicleEverTriedLoading() ? ge->last_speed | (ge->last_age << 8) : 0xFF00;
			case 0x65: return GB(ge->status, GoodsEntry::GES_ACCEPTANCE, 1) << 3;
			case 0x69: {
				static_assert((int)GoodsEntry::GES_EVER_ACCEPTED + 1 == (int)GoodsEntry::GES_LAST_MONTH);
				static_assert((int)GoodsEntry::GES_EVER_ACCEPTED + 2 == (int)GoodsEntry::GES_CURRENT_MONTH);
				static_assert((int)GoodsEntry::GES_EVER_ACCEPTED + 3 == (int)GoodsEntry::GES_ACCEPTED_BIGTICK);
				return GB(ge->status, GoodsEntry::GES_EVER_ACCEPTED, 4);
			}
		}
	}

	/* Handle cargo variables (deprecated) */
	if (variable >= 0x8C && variable <= 0xEC) {
		const GoodsEntry *g = &this->goods[GB(variable - 0x8C, 3, 4)];
		switch (GB(variable - 0x8C, 0, 3)) {
			case 0: return g->cargo.TotalCount();
			case 1: return GB(std::min(g->cargo.TotalCount(), 4095u), 0, 4) | (GB(g->status, GoodsEntry::GES_ACCEPTANCE, 1) << 7);
			case 2: return g->time_since_pickup;
			case 3: return g->rating;
			case 4: return g->cargo.Source();
			case 5: return g->cargo.DaysInTransit();
			case 6: return g->last_speed;
			case 7: return g->last_age;
		}
	}

	DEBUG(grf, 1, "Unhandled station variable 0x%X", variable);

	*available = false;
	return UINT_MAX;
}

uint32 Waypoint::GetNewGRFVariable(const ResolverObject &object, byte variable, byte parameter, bool *available) const
{
	switch (variable) {
		case 0x48: return 0; // Accepted cargo types
		case 0x8A: return HVOT_WAYPOINT;
		case 0xF1: return 0; // airport type
		case 0xF2: return 0; // truck stop status
		case 0xF3: return 0; // bus stop status
		case 0xF6: return 0; // airport flags
		case 0xF7: return 0; // airport flags cont.
	}

	/* Handle cargo variables with parameter, 0x60 to 0x65 */
	if (variable >= 0x60 && variable <= 0x65) {
		return 0;
	}

	/* Handle cargo variables (deprecated) */
	if (variable >= 0x8C && variable <= 0xEC) {
		switch (GB(variable - 0x8C, 0, 3)) {
			case 3: return INITIAL_STATION_RATING;
			case 4: return INVALID_STATION;
			default: return 0;
		}
	}

	DEBUG(grf, 1, "Unhandled station variable 0x%X", variable);

	*available = false;
	return UINT_MAX;
}

/* virtual */ const SpriteGroup *StationResolverObject::ResolveReal(const RealSpriteGroup *group) const
{
	if (this->station_scope.st == nullptr || this->station_scope.statspec->cls_id == STAT_CLASS_WAYP) {
		return group->loading[0];
	}

	uint cargo = 0;
	const Station *st = Station::From(this->station_scope.st);

	switch (this->station_scope.cargo_type) {
		case CT_INVALID:
		case CT_DEFAULT_NA:
		case CT_PURCHASE:
			cargo = 0;
			break;

		case CT_DEFAULT:
			for (CargoID cargo_type = 0; cargo_type < NUM_CARGO; cargo_type++) {
				cargo += st->goods[cargo_type].cargo.TotalCount();
			}
			break;

		default:
			cargo = st->goods[this->station_scope.cargo_type].cargo.TotalCount();
			break;
	}

	if (HasBit(this->station_scope.statspec->flags, SSF_DIV_BY_STATION_SIZE)) cargo /= (st->train_station.w + st->train_station.h);
	cargo = std::min(0xfffu, cargo);

	if (cargo > this->station_scope.statspec->cargo_threshold) {
		if (group->num_loading > 0) {
			uint set = ((cargo - this->station_scope.statspec->cargo_threshold) * group->num_loading) / (4096 - this->station_scope.statspec->cargo_threshold);
			return group->loading[set];
		}
	} else {
		if (group->num_loaded > 0) {
			uint set = (cargo * group->num_loaded) / (this->station_scope.statspec->cargo_threshold + 1);
			return group->loaded[set];
		}
	}

	return group->loading[0];
}

GrfSpecFeature StationResolverObject::GetFeature() const
{
	return GSF_STATIONS;
}

uint32 StationResolverObject::GetDebugID() const
{
	return this->station_scope.statspec->grf_prop.local_id;
}

/**
 * Resolver for stations.
 * @param statspec Station (type) specification.
 * @param st Instance of the station.
 * @param tile %Tile of the station.
 * @param callback Callback ID.
 * @param callback_param1 First parameter (var 10) of the callback.
 * @param callback_param2 Second parameter (var 18) of the callback.
 */
StationResolverObject::StationResolverObject(const StationSpec *statspec, BaseStation *st, TileIndex tile,
		CallbackID callback, uint32 callback_param1, uint32 callback_param2)
	: ResolverObject(statspec->grf_prop.grffile, callback, callback_param1, callback_param2),
	station_scope(*this, statspec, st, tile), town_scope(nullptr) {
	/* Invalidate all cached vars */
	_svc.valid = 0;

	CargoID ctype = CT_DEFAULT_NA;

	if (this->station_scope.st == nullptr) {
		/* No station, so we are in a purchase list */
		ctype = CT_PURCHASE;
	} else if (Station::IsExpected(this->station_scope.st)) {
		const Station *st = Station::From(this->station_scope.st);
		/* Pick the first cargo that we have waiting */
		const CargoSpec *cs;
		FOR_ALL_CARGOSPECS(cs) {
			if (this->station_scope.statspec->grf_prop.spritegroup[cs->Index()] != nullptr &&
					st->goods[cs->Index()].cargo.TotalCount() > 0) {
				ctype = cs->Index();
				break;
			}
		}
	}

	if (this->station_scope.statspec->grf_prop.spritegroup[ctype] == nullptr) {
		ctype = CT_DEFAULT;
	}

	/* Remember the cargo type we've picked */
	this->station_scope.cargo_type = ctype;
	this->root_spritegroup = this->station_scope.statspec->grf_prop.spritegroup[this->station_scope.cargo_type];
}

StationResolverObject::~StationResolverObject()
{
	delete this->town_scope;
}

/**
 * Resolve sprites for drawing a station tile.
 * @param statspec Station spec
 * @param st Station (nullptr in GUI)
 * @param tile Station tile being drawn (INVALID_TILE in GUI)
 * @param var10 Value to put in variable 10; normally 0; 1 when resolving the groundsprite and SSF_SEPARATE_GROUND is set.
 * @return First sprite of the Action 1 spriteset to use, minus an offset of 0x42D to accommodate for weird NewGRF specs.
 */
SpriteID GetCustomStationRelocation(const StationSpec *statspec, BaseStation *st, TileIndex tile, uint32 var10)
{
	StationResolverObject object(statspec, st, tile, CBID_NO_CALLBACK, var10);
	const SpriteGroup *group = object.Resolve();
	if (group == nullptr || group->type != SGT_RESULT) return 0;
	return group->GetResult() - 0x42D;
}

/**
 * Resolve the sprites for custom station foundations.
 * @param statspec Station spec
 * @param st Station
 * @param tile Station tile being drawn
 * @param layout Spritelayout as returned by previous callback
 * @param edge_info Information about northern tile edges; whether they need foundations or merge into adjacent tile's foundations.
 * @return First sprite of a set of foundation sprites for various slopes, or 0 if default foundations shall be drawn.
 */
SpriteID GetCustomStationFoundationRelocation(const StationSpec *statspec, BaseStation *st, TileIndex tile, uint layout, uint edge_info)
{
	/* callback_param1 == 2 means  we are resolving the foundation sprites. */
	StationResolverObject object(statspec, st, tile, CBID_NO_CALLBACK, 2, layout | (edge_info << 16));

	const SpriteGroup *group = object.Resolve();
	if (group == nullptr || group->type != SGT_RESULT) return 0;

	/* Note: SpriteGroup::Resolve zeroes all registers, so register 0x100 is initialised to 0. (compatibility) */
	return group->GetResult() + GetRegister(0x100);
}


uint16 GetStationCallback(CallbackID callback, uint32 param1, uint32 param2, const StationSpec *statspec, BaseStation *st, TileIndex tile)
{
	StationResolverObject object(statspec, st, tile, callback, param1, param2);
	return object.ResolveCallback();
}

/**
 * Check the slope of a tile of a new station.
 * @param north_tile Norther tile of the station rect.
 * @param cur_tile Tile to check.
 * @param statspec Station spec.
 * @param axis Axis of the new station.
 * @param plat_len Platform length.
 * @param numtracks Number of platforms.
 * @return Succeeded or failed command.
 */
CommandCost PerformStationTileSlopeCheck(TileIndex north_tile, TileIndex cur_tile, const StationSpec *statspec, Axis axis, byte plat_len, byte numtracks)
{
	TileIndexDiff diff = cur_tile - north_tile;
	Slope slope = GetTileSlope(cur_tile);

	StationResolverObject object(statspec, nullptr, cur_tile, CBID_STATION_LAND_SLOPE_CHECK,
			(slope << 4) | (slope ^ (axis == AXIS_Y && HasBit(slope, CORNER_W) != HasBit(slope, CORNER_E) ? SLOPE_EW : 0)),
			(numtracks << 24) | (plat_len << 16) | (axis == AXIS_Y ? TileX(diff) << 8 | TileY(diff) : TileY(diff) << 8 | TileX(diff)));
	object.station_scope.axis = axis;

	uint16 cb_res = object.ResolveCallback();

	/* Failed callback means success. */
	if (cb_res == CALLBACK_FAILED) return CommandCost();

	/* The meaning of bit 10 is inverted for a grf version < 8. */
	if (statspec->grf_prop.grffile->grf_version < 8) ToggleBit(cb_res, 10);
	return GetErrorMessageFromLocationCallbackResult(cb_res, statspec->grf_prop.grffile, STR_ERROR_LAND_SLOPED_IN_WRONG_DIRECTION);
}


/**
 * Allocate a StationSpec to a Station. This is called once per build operation.
 * @param statspec StationSpec to allocate.
 * @param st Station to allocate it to.
 * @param exec Whether to actually allocate the spec.
 * @return Index within the Station's spec list, or -1 if the allocation failed.
 */
int AllocateSpecToStation(const StationSpec *statspec, BaseStation *st, bool exec)
{
	uint i;

	if (statspec == nullptr || st == nullptr) return 0;

	for (i = 1; i < st->num_specs && i < NUM_STATIONSSPECS_PER_STATION; i++) {
		if (st->speclist[i].spec == nullptr && st->speclist[i].grfid == 0) break;
	}

	if (i == NUM_STATIONSSPECS_PER_STATION) {
		/* As final effort when the spec list is already full...
		 * try to find the same spec and return that one. This might
		 * result in slightly "wrong" (as per specs) looking stations,
		 * but it's fairly unlikely that one reaches the limit anyways.
		 */
		for (i = 1; i < st->num_specs && i < NUM_STATIONSSPECS_PER_STATION; i++) {
			if (st->speclist[i].spec == statspec) return i;
		}

		return -1;
	}

	if (exec) {
		if (i >= st->num_specs) {
			st->num_specs = i + 1;
			st->speclist = ReallocT(st->speclist, st->num_specs);

			if (st->num_specs == 2) {
				/* Initial allocation */
				st->speclist[0].spec     = nullptr;
				st->speclist[0].grfid    = 0;
				st->speclist[0].localidx = 0;
			}
		}

		st->speclist[i].spec     = statspec;
		st->speclist[i].grfid    = statspec->grf_prop.grffile->grfid;
		st->speclist[i].localidx = statspec->grf_prop.local_id;

		StationUpdateCachedTriggers(st);
	}

	return i;
}


/**
 * Deallocate a StationSpec from a Station. Called when removing a single station tile.
 * @param st Station to work with.
 * @param specindex Index of the custom station within the Station's spec list.
 * @return Indicates whether the StationSpec was deallocated.
 */
void DeallocateSpecFromStation(BaseStation *st, byte specindex)
{
	/* specindex of 0 (default) is never freeable */
	if (specindex == 0) return;

	ETileArea area = ETileArea(st, INVALID_TILE, TA_WHOLE);
	/* Check all tiles over the station to check if the specindex is still in use */
	TILE_AREA_LOOP(tile, area) {
		if (st->TileBelongsToRailStation(tile) && GetCustomStationSpecIndex(tile) == specindex) {
			return;
		}
	}

	/* This specindex is no longer in use, so deallocate it */
	st->speclist[specindex].spec     = nullptr;
	st->speclist[specindex].grfid    = 0;
	st->speclist[specindex].localidx = 0;

	/* If this was the highest spec index, reallocate */
	if (specindex == st->num_specs - 1) {
		for (; st->speclist[st->num_specs - 1].grfid == 0 && st->num_specs > 1; st->num_specs--) {}

		if (st->num_specs > 1) {
			st->speclist = ReallocT(st->speclist, st->num_specs);
		} else {
			free(st->speclist);
			st->num_specs = 0;
			st->speclist  = nullptr;
			st->cached_anim_triggers = 0;
			st->cached_cargo_triggers = 0;
			return;
		}
	}

	StationUpdateCachedTriggers(st);
}

/**
 * Draw representation of a station tile for GUI purposes.
 * @param x Position x of image.
 * @param y Position y of image.
 * @param axis Axis.
 * @param railtype Rail type.
 * @param sclass, station Type of station.
 * @param station station ID
 * @return True if the tile was drawn (allows for fallback to default graphic)
 */
bool DrawStationTile(int x, int y, RailType railtype, Axis axis, StationClassID sclass, uint station)
{
	const DrawTileSprites *sprites = nullptr;
	const RailtypeInfo *rti = GetRailTypeInfo(railtype);
	PaletteID palette = COMPANY_SPRITE_COLOUR(_local_company);
	uint tile = 2;

	const StationSpec *statspec = StationClass::Get(sclass)->GetSpec(station);
	if (statspec == nullptr) return false;

	if (HasBit(statspec->callback_mask, CBM_STATION_SPRITE_LAYOUT)) {
		uint16 callback = GetStationCallback(CBID_STATION_SPRITE_LAYOUT, 0x2110000, 0, statspec, nullptr, INVALID_TILE);
		if (callback != CALLBACK_FAILED) tile = callback;
	}

	uint32 total_offset = rti->GetRailtypeSpriteOffset();
	uint32 relocation = 0;
	uint32 ground_relocation = 0;
	const NewGRFSpriteLayout *layout = nullptr;
	DrawTileSprites tmp_rail_layout;

	if (statspec->renderdata == nullptr) {
		sprites = GetStationTileLayout(STATION_RAIL, tile + axis);
	} else {
		layout = &statspec->renderdata[(tile < statspec->tiles) ? tile + axis : (uint)axis];
		if (!layout->NeedsPreprocessing()) {
			sprites = layout;
			layout = nullptr;
		}
	}

	if (layout != nullptr) {
		/* Sprite layout which needs preprocessing */
		bool separate_ground = HasBit(statspec->flags, SSF_SEPARATE_GROUND);
		uint32 var10_values = layout->PrepareLayout(total_offset, rti->fallback_railtype, 0, 0, separate_ground);
		uint8 var10;
		FOR_EACH_SET_BIT(var10, var10_values) {
			uint32 var10_relocation = GetCustomStationRelocation(statspec, nullptr, INVALID_TILE, var10);
			layout->ProcessRegisters(var10, var10_relocation, separate_ground);
		}

		tmp_rail_layout.seq = layout->GetLayout(&tmp_rail_layout.ground);
		sprites = &tmp_rail_layout;
		total_offset = 0;
	} else {
		/* Simple sprite layout */
		ground_relocation = relocation = GetCustomStationRelocation(statspec, nullptr, INVALID_TILE, 0);
		if (HasBit(sprites->ground.sprite, SPRITE_MODIFIER_CUSTOM_SPRITE)) {
			ground_relocation = GetCustomStationRelocation(statspec, nullptr, INVALID_TILE, 1);
		}
		ground_relocation += rti->fallback_railtype;
	}

	SpriteID image = sprites->ground.sprite;
	PaletteID pal = sprites->ground.pal;
	RailTrackOffset overlay_offset;
	if (rti->UsesOverlay() && SplitGroundSpriteForOverlay(nullptr, &image, &overlay_offset)) {
		SpriteID ground = GetCustomRailSprite(rti, INVALID_TILE, RTSG_GROUND);
		DrawSprite(image, PAL_NONE, x, y);
		DrawSprite(ground + overlay_offset, PAL_NONE, x, y);
	} else {
		image += HasBit(image, SPRITE_MODIFIER_CUSTOM_SPRITE) ? ground_relocation : total_offset;
		if (HasBit(pal, SPRITE_MODIFIER_CUSTOM_SPRITE)) pal += ground_relocation;
		DrawSprite(image, GroundSpritePaletteTransform(image, pal, palette), x, y);
	}

	DrawRailTileSeqInGUI(x, y, sprites, total_offset, relocation, palette);

	return true;
}


const StationSpec *GetStationSpec(TileIndex t)
{
	if (!IsCustomStationSpecIndex(t)) return nullptr;

	const BaseStation *st = BaseStation::GetByTile(t);
	uint specindex = GetCustomStationSpecIndex(t);
	return specindex < st->num_specs ? st->speclist[specindex].spec : nullptr;
}


/**
 * Check whether a rail station tile is NOT traversable.
 * @param tile %Tile to test.
 * @return Station tile is blocked.
 * @note This could be cached (during build) in the map array to save on all the dereferencing.
 */
bool IsStationTileBlocked(TileIndex tile)
{
	const StationSpec *statspec = GetStationSpec(tile);

	return statspec != nullptr && HasBit(statspec->blocked, GetStationGfx(tile));
}

/**
 * Check if a rail station tile shall have pylons when electrified.
 * @param tile %Tile to test.
 * @return Tile shall have pylons.
 * @note This could be cached (during build) in the map array to save on all the dereferencing.
 */
bool CanStationTileHavePylons(TileIndex tile)
{
	const StationSpec *statspec = GetStationSpec(tile);
	uint gfx = GetStationGfx(tile);
	/* Default stations do not draw pylons under roofs (gfx >= 4) */
	return statspec != nullptr ? HasBit(statspec->pylons, gfx) : gfx < 4;
}

/**
 * Check if a rail station tile shall have wires when electrified.
 * @param tile %Tile to test.
 * @return Tile shall have wires.
 * @note This could be cached (during build) in the map array to save on all the dereferencing.
 */
bool CanStationTileHaveWires(TileIndex tile)
{
	const StationSpec *statspec = GetStationSpec(tile);
	return statspec == nullptr || !HasBit(statspec->wires, GetStationGfx(tile));
}

/** Wrapper for animation control, see GetStationCallback. */
uint16 GetAnimStationCallback(CallbackID callback, uint32 param1, uint32 param2, const StationSpec *statspec, BaseStation *st, TileIndex tile, int extra_data)
{
	return GetStationCallback(callback, param1, param2, statspec, st, tile);
}

/** Helper class for animation control. */
struct StationAnimationBase : public AnimationBase<StationAnimationBase, StationSpec, BaseStation, int, GetAnimStationCallback> {
	static const CallbackID cb_animation_speed      = CBID_STATION_ANIMATION_SPEED;
	static const CallbackID cb_animation_next_frame = CBID_STATION_ANIM_NEXT_FRAME;

	static const StationCallbackMask cbm_animation_speed      = CBM_STATION_ANIMATION_SPEED;
	static const StationCallbackMask cbm_animation_next_frame = CBM_STATION_ANIMATION_NEXT_FRAME;
};

void AnimateStationTile(TileIndex tile)
{
	const StationSpec *ss = GetStationSpec(tile);
	if (ss == nullptr) return;

	StationAnimationBase::AnimateTile(ss, BaseStation::GetByTile(tile), tile, HasBit(ss->flags, SSF_CB141_RANDOM_BITS));
}

void TriggerStationAnimation(BaseStation *st, TileIndex tile, StationAnimationTrigger trigger, CargoID cargo_type)
{
	/* List of coverage areas for each animation trigger */
	static const TriggerArea tas[] = {
		TA_TILE, TA_WHOLE, TA_WHOLE, TA_PLATFORM, TA_PLATFORM, TA_PLATFORM, TA_WHOLE
	};

	/* Get Station if it wasn't supplied */
	if (st == nullptr) st = BaseStation::GetByTile(tile);

	/* Check the cached animation trigger bitmask to see if we need
	 * to bother with any further processing. */
	if (!HasBit(st->cached_anim_triggers, trigger)) return;

	uint16 random_bits = Random();
	ETileArea area = ETileArea(st, tile, tas[trigger]);

	/* Check all tiles over the station to check if the specindex is still in use */
	TILE_AREA_LOOP(tile, area) {
		if (st->TileBelongsToRailStation(tile)) {
			const StationSpec *ss = GetStationSpec(tile);
			if (ss != nullptr && HasBit(ss->animation.triggers, trigger)) {
				CargoID cargo;
				if (cargo_type == CT_INVALID) {
					cargo = CT_INVALID;
				} else {
					cargo = ss->grf_prop.grffile->cargo_map[cargo_type];
				}
				StationAnimationBase::ChangeAnimationFrame(CBID_STATION_ANIM_START_STOP, ss, st, tile, (random_bits << 16) | Random(), (uint8)trigger | (cargo << 8));
			}
		}
	}
}

/**
 * Trigger station randomisation
 * @param st station being triggered
 * @param tile specific tile of platform to trigger
 * @param trigger trigger type
 * @param cargo_type cargo type causing trigger
 */
void TriggerStationRandomisation(Station *st, TileIndex tile, StationRandomTrigger trigger, CargoID cargo_type)
{
	/* List of coverage areas for each animation trigger */
	static const TriggerArea tas[] = {
		TA_WHOLE, TA_WHOLE, TA_PLATFORM, TA_PLATFORM, TA_PLATFORM, TA_PLATFORM
	};

	/* Get Station if it wasn't supplied */
	if (st == nullptr) st = Station::GetByTile(tile);

	/* Check the cached cargo trigger bitmask to see if we need
	 * to bother with any further processing. */
	if (st->cached_cargo_triggers == 0) return;
	if (cargo_type != CT_INVALID && !HasBit(st->cached_cargo_triggers, cargo_type)) return;

	uint32 whole_reseed = 0;
	ETileArea area = ETileArea(st, tile, tas[trigger]);

	CargoTypes empty_mask = 0;
	if (trigger == SRT_CARGO_TAKEN) {
		/* Create a bitmask of completely empty cargo types to be matched */
		for (CargoID i = 0; i < NUM_CARGO; i++) {
			if (st->goods[i].cargo.TotalCount() == 0) {
				SetBit(empty_mask, i);
			}
		}
	}

	/* Store triggers now for var 5F */
	SetBit(st->waiting_triggers, trigger);
	uint32 used_triggers = 0;

	/* Check all tiles over the station to check if the specindex is still in use */
	TILE_AREA_LOOP(tile, area) {
		if (st->TileBelongsToRailStation(tile)) {
			const StationSpec *ss = GetStationSpec(tile);
			if (ss == nullptr) continue;

			/* Cargo taken "will only be triggered if all of those
			 * cargo types have no more cargo waiting." */
			if (trigger == SRT_CARGO_TAKEN) {
				if ((ss->cargo_triggers & ~empty_mask) != 0) continue;
			}

			if (cargo_type == CT_INVALID || HasBit(ss->cargo_triggers, cargo_type)) {
				StationResolverObject object(ss, st, tile, CBID_RANDOM_TRIGGER, 0);
				object.waiting_triggers = st->waiting_triggers;

				const SpriteGroup *group = object.Resolve();
				if (group == nullptr) continue;

				used_triggers |= object.used_triggers;

				uint32 reseed = object.GetReseedSum();
				if (reseed != 0) {
					whole_reseed |= reseed;
					reseed >>= 16;

					/* Set individual tile random bits */
					uint8 random_bits = GetStationTileRandomBits(tile);
					random_bits &= ~reseed;
					random_bits |= Random() & reseed;
					SetStationTileRandomBits(tile, random_bits);

					MarkTileDirtyByTile(tile);
				}
			}
		}
	}

	/* Update whole station random bits */
	st->waiting_triggers &= ~used_triggers;
	if ((whole_reseed & 0xFFFF) != 0) {
		st->random_bits &= ~whole_reseed;
		st->random_bits |= Random() & whole_reseed;
	}
}

/**
 * Update the cached animation trigger bitmask for a station.
 * @param st Station to update.
 */
void StationUpdateCachedTriggers(BaseStation *st)
{
	st->cached_anim_triggers = 0;
	st->cached_cargo_triggers = 0;

	/* Combine animation trigger bitmask for all station specs
	 * of this station. */
	for (uint i = 0; i < st->num_specs; i++) {
		const StationSpec *ss = st->speclist[i].spec;
		if (ss != nullptr) {
			st->cached_anim_triggers |= ss->animation.triggers;
			st->cached_cargo_triggers |= ss->cargo_triggers;
		}
	}
}

