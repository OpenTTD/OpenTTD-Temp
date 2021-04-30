/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file oldloader_sl.cpp Chunks and fix-ups for TTO/TTD/TTDP savegames. TTO loader code is based on SVXConverter by Roman Vetter. */

#include "../stdafx.h"
#include "../town.h"
#include "../industry.h"
#include "../company_func.h"
#include "../aircraft.h"
#include "../roadveh.h"
#include "../ship.h"
#include "../train.h"
#include "../signs_base.h"
#include "../station_base.h"
#include "../subsidy_base.h"
#include "../debug.h"
#include "../depot_base.h"
#include "../date_func.h"
#include "../vehicle_func.h"
#include "../effectvehicle_base.h"
#include "../engine_func.h"
#include "../company_base.h"
#include "../disaster_vehicle.h"
#include "../core/smallvec_type.hpp"
#include "saveload_internal.h"
#include "oldloader.h"
#include <array>

#include "table/strings.h"
#include "../table/engines.h"
#include "../table/townname.h"

#include "../safeguards.h"

static bool _read_ttdpatch_flags;    ///< Have we (tried to) read TTDPatch extra flags?
static uint16 _old_extra_chunk_nums; ///< Number of extra TTDPatch chunks
static byte _old_vehicle_multiplier; ///< TTDPatch vehicle multiplier

static uint8 *_old_map3;

void FixOldMapArray()
{
	/* TTO/TTD/TTDP savegames could have buoys at tile 0
	 * (without assigned station struct) */
	MemSetT(&_m[0], 0);
	SetTileType(0, MP_WATER);
	SetTileOwner(0, OWNER_WATER);
}

static void FixTTDMapArray()
{
	/* _old_map3 is moved to _m::m3 and _m::m4 */
	for (TileIndex t = 0; t < OLD_MAP_SIZE; t++) {
		_m[t].m3 = _old_map3[t * 2];
		_m[t].m4 = _old_map3[t * 2 + 1];
	}

	for (TileIndex t = 0; t < OLD_MAP_SIZE; t++) {
		switch (GetTileType(t)) {
			case MP_STATION:
				_m[t].m4 = 0; // We do not understand this TTDP station mapping (yet)
				switch (_m[t].m5) {
					/* We have drive through stops at a totally different place */
					case 0x53: case 0x54: _m[t].m5 += 170 - 0x53; break; // Bus drive through
					case 0x57: case 0x58: _m[t].m5 += 168 - 0x57; break; // Truck drive through
					case 0x55: case 0x56: _m[t].m5 += 170 - 0x55; break; // Bus tram stop
					case 0x59: case 0x5A: _m[t].m5 += 168 - 0x59; break; // Truck tram stop
					default: break;
				}
				break;

			case MP_RAILWAY:
				/* We save presignals different from TTDPatch, convert them */
				if (GB(_m[t].m5, 6, 2) == 1) { // RAIL_TILE_SIGNALS
					/* This byte is always zero in TTD for this type of tile */
					if (_m[t].m4) { // Convert the presignals to our own format
						_m[t].m4 = (_m[t].m4 >> 1) & 7;
					}
				}
				/* TTDPatch stores PBS things in L6 and all elsewhere; so we'll just
				 * clear it for ourselves and let OTTD's rebuild PBS itself */
				_m[t].m4 &= 0xF; // Only keep the lower four bits; upper four is PBS
				break;

			case MP_WATER:
				/* if water class == 3, make river there */
				if (GB(_m[t].m3, 0, 2) == 3) {
					SetTileType(t, MP_WATER);
					SetTileOwner(t, OWNER_WATER);
					_m[t].m2 = 0;
					_m[t].m3 = 2; // WATER_CLASS_RIVER
					_m[t].m4 = Random();
					_m[t].m5 = 0;
				}
				break;

			default:
				break;
		}
	}

	FixOldMapArray();
}

static void FixTTDDepots()
{
	for (const Depot *d : Depot::Iterate(252)) {
		if (!IsDepotTile(d->xy) || GetDepotIndex(d->xy) != d->index) {
			/** Workaround for SVXConverter bug, depots 252-255 could be invalid */
			delete d;
		}
	}
}

#define FIXNUM(x, y, z) (((((x) << 16) / (y)) + 1) << z)

static uint32 RemapOldTownName(uint32 townnameparts, byte old_town_name_type)
{
	switch (old_town_name_type) {
		case 0: case 3: // English, American
			/* Already OK */
			return townnameparts;

		case 1: // French
			/* For some reason 86 needs to be subtracted from townnameparts
			 * 0000 0000 0000 0000 0000 0000 1111 1111 */
			return FIXNUM(townnameparts - 86, lengthof(_name_french_real), 0);

		case 2: // German
			DEBUG(misc, 0, "German Townnames are buggy (%d)", townnameparts);
			return townnameparts;

		case 4: // Latin-American
			/* 0000 0000 0000 0000 0000 0000 1111 1111 */
			return FIXNUM(townnameparts, lengthof(_name_spanish_real), 0);

		case 5: // Silly
			/* NUM_SILLY_1 - lower 16 bits
			 * NUM_SILLY_2 - upper 16 bits without leading 1 (first 8 bytes)
			 * 1000 0000 2222 2222 0000 0000 1111 1111 */
			return FIXNUM(townnameparts, lengthof(_name_silly_1), 0) | FIXNUM(GB(townnameparts, 16, 8), lengthof(_name_silly_2), 16);
	}
	return 0;
}

#undef FIXNUM

static void FixOldTowns()
{
	/* Convert town-names if needed */
	for (Town *town : Town::Iterate()) {
		if (IsInsideMM(town->townnametype, 0x20C1, 0x20C3)) {
			town->townnametype = SPECSTR_TOWNNAME_ENGLISH + _settings_game.game_creation.town_name;
			town->townnameparts = RemapOldTownName(town->townnameparts, _settings_game.game_creation.town_name);
		}
	}
}

static StringID *_old_vehicle_names;

/**
 * Convert the old style vehicles into something that resembles
 * the old new style savegames. Then #AfterLoadGame can handle
 * the rest of the conversion.
 */
void FixOldVehicles()
{
	for (Vehicle *v : Vehicle::Iterate()) {
		if ((size_t)v->next == 0xFFFF) {
			v->next = nullptr;
		} else {
			v->next = Vehicle::GetIfValid((size_t)v->next);
		}

		/* For some reason we need to correct for this */
		switch (v->spritenum) {
			case 0xfd: break;
			case 0xff: v->spritenum = 0xfe; break;
			default:   v->spritenum >>= 1; break;
		}

		/* Vehicle-subtype is different in TTD(Patch) */
		if (v->type == VEH_EFFECT) v->subtype = v->subtype >> 1;

		v->name = CopyFromOldName(_old_vehicle_names[v->index]);

		/* We haven't used this bit for stations for ages */
		if (v->type == VEH_ROAD) {
			RoadVehicle *rv = RoadVehicle::From(v);
			if (rv->state != RVSB_IN_DEPOT && rv->state != RVSB_WORMHOLE) {
				ClrBit(rv->state, 2);
				if (IsTileType(rv->tile, MP_STATION) && _m[rv->tile].m5 >= 168) {
					/* Update the vehicle's road state to show we're in a drive through road stop. */
					SetBit(rv->state, RVS_IN_DT_ROAD_STOP);
				}
			}
		}

		/* The subtype should be 0, but it sometimes isn't :( */
		if (v->type == VEH_ROAD || v->type == VEH_SHIP) v->subtype = 0;

		/* Sometimes primary vehicles would have a nothing (invalid) order
		 * or vehicles that could not have an order would still have a
		 * (loading) order which causes assertions and the like later on.
		 */
		if (!IsCompanyBuildableVehicleType(v) ||
				(v->IsPrimaryVehicle() && v->current_order.IsType(OT_NOTHING))) {
			v->current_order.MakeDummy();
		}

		/* Shared orders are fixed in AfterLoadVehicles now */
	}
}

static bool FixTTOMapArray()
{
	for (TileIndex t = 0; t < OLD_MAP_SIZE; t++) {
		TileType tt = GetTileType(t);
		if (tt == 11) {
			/* TTO has a different way of storing monorail.
			 * Instead of using bits in m3 it uses a different tile type. */
			_m[t].m3 = 1; // rail type = monorail (in TTD)
			SetTileType(t, MP_RAILWAY);
			_m[t].m2 = 1; // set monorail ground to RAIL_GROUND_GRASS
			tt = MP_RAILWAY;
		}

		switch (tt) {
			case MP_CLEAR:
				break;

			case MP_RAILWAY:
				switch (GB(_m[t].m5, 6, 2)) {
					case 0: // RAIL_TILE_NORMAL
						break;
					case 1: // RAIL_TILE_SIGNALS
						_m[t].m4 = (~_m[t].m5 & 1) << 2;        // signal variant (present only in OTTD)
						SB(_m[t].m2, 6, 2, GB(_m[t].m5, 3, 2)); // signal status
						_m[t].m3 |= 0xC0;                       // both signals are present
						_m[t].m5 = HasBit(_m[t].m5, 5) ? 2 : 1; // track direction (only X or Y)
						_m[t].m5 |= 0x40;                       // RAIL_TILE_SIGNALS
						break;
					case 3: // RAIL_TILE_DEPOT
						_m[t].m2 = 0;
						break;
					default:
						return false;
				}
				break;

			case MP_ROAD: // road (depot) or level crossing
				switch (GB(_m[t].m5, 4, 4)) {
					case 0: // ROAD_TILE_NORMAL
						if (_m[t].m2 == 4) _m[t].m2 = 5; // 'small trees' -> ROADSIDE_TREES
						break;
					case 1: // ROAD_TILE_CROSSING (there aren't monorail crossings in TTO)
						_m[t].m3 = _m[t].m1; // set owner of road = owner of rail
						break;
					case 2: // ROAD_TILE_DEPOT
						break;
					default:
						return false;
				}
				break;

			case MP_HOUSE:
				_m[t].m3 = _m[t].m2 & 0xC0;    // construction stage
				_m[t].m2 &= 0x3F;              // building type
				if (_m[t].m2 >= 5) _m[t].m2++; // skip "large office block on snow"
				break;

			case MP_TREES:
				_m[t].m3 = GB(_m[t].m5, 3, 3); // type of trees
				_m[t].m5 &= 0xC7;              // number of trees and growth status
				break;

			case MP_STATION:
				_m[t].m3 = (_m[t].m5 >= 0x08 && _m[t].m5 <= 0x0F) ? 1 : 0; // monorail -> 1, others 0 (rail, road, airport, dock)
				if (_m[t].m5 >= 8) _m[t].m5 -= 8; // shift for monorail
				if (_m[t].m5 >= 0x42) _m[t].m5++; // skip heliport
				break;

			case MP_WATER:
				_m[t].m3 = _m[t].m2 = 0;
				break;

			case MP_VOID:
				_m[t].m2 = _m[t].m3 = _m[t].m5 = 0;
				break;

			case MP_INDUSTRY:
				_m[t].m3 = 0;
				switch (_m[t].m5) {
					case 0x24: // farm silo
						_m[t].m5 = 0x25;
						break;
					case 0x25: case 0x27: // farm
					case 0x28: case 0x29: case 0x2A: case 0x2B: // factory
						_m[t].m5--;
						break;
					default:
						if (_m[t].m5 >= 0x2C) _m[t].m5 += 3; // iron ore mine, steel mill or bank
						break;
				}
				break;

			case MP_TUNNELBRIDGE:
				if (HasBit(_m[t].m5, 7)) { // bridge
					byte m5 = _m[t].m5;
					_m[t].m5 = m5 & 0xE1; // copy bits 7..5, 1
					if (GB(m5, 1, 2) == 1) _m[t].m5 |= 0x02; // road bridge
					if (GB(m5, 1, 2) == 3) _m[t].m2 |= 0xA0; // monorail bridge -> tubular, steel bridge
					if (!HasBit(m5, 6)) { // bridge head
						_m[t].m3 = (GB(m5, 1, 2) == 3) ? 1 : 0; // track subtype (1 for monorail, 0 for others)
					} else { // middle bridge part
						_m[t].m3 = HasBit(m5, 2) ? 0x10 : 0;  // track subtype on bridge
						if (GB(m5, 3, 2) == 3) _m[t].m3 |= 1; // track subtype under bridge
						if (GB(m5, 3, 2) == 1) _m[t].m5 |= 0x08; // set for road/water under (0 for rail/clear)
					}
				} else { // tunnel entrance/exit
					_m[t].m2 = 0;
					_m[t].m3 = HasBit(_m[t].m5, 3); // monorail
					_m[t].m5 &= HasBit(_m[t].m5, 3) ? 0x03 : 0x07 ; // direction, transport type (== 0 for rail)
				}
				break;

			case MP_OBJECT:
				_m[t].m2 = 0;
				_m[t].m3 = 0;
				break;

			default:
				return false;

		}
	}

	FixOldMapArray();

	return true;
}

static Engine *_old_engines;

static bool FixTTOEngines()
{
	/** TTD->TTO remapping of engines; 255 means there is no equivalent. SVXConverter uses (almost) the same table. */
	static const EngineID ttd_to_tto[] = {
		  0, 255, 255, 255, 255, 255, 255, 255,   5,   7,   8,   9,  10,  11,  12,  13,
		255, 255, 255, 255, 255, 255,  15,  16,  17,  18,  19,  20,  21,  22,  23,  24,
		25,   26,  27,  29,  28,  30, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
		255, 255, 255, 255, 255, 255, 255,  31, 255,  32,  33,  34,  35,  36,  37,  38,
		 39,  40,  41,  42, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
		255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
		255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
		255, 255, 255, 255,  44,  45,  46, 255, 255, 255, 255,  47,  48, 255,  49,  50,
		255, 255, 255, 255,  51,  52, 255,  53,  54, 255,  55,  56, 255,  57,  59, 255,
		 58,  60, 255,  61,  62, 255,  63,  64, 255,  65,  66, 255, 255, 255, 255, 255,
		255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
		255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
		255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,  67,  68,  69,  70,
		 71, 255, 255,  76,  77, 255, 255,  78,  79,  80,  81,  82,  83,  84,  85,  86,
		 87,  88,  89,  90,  91,  92,  93,  94,  95,  96,  97,  98,  99, 100, 101, 255,
		255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 102, 255, 255
	};

	/** TTO->TTD remapping of engines. SVXConverter uses the same table. */
	static const EngineID tto_to_ttd[] = {
		  0,   0,   8,   8,   8,   8,   8,   9,  10,  11,  12,  13,  14,  15,  15,  22,
		 23,  24,  25,  26,  27,  29,  28,  30,  31,  32,  33,  34,  35,  36,  37,  55,
		 57,  59,  58,  60,  61,  62,  63,  64,  65,  66,  67, 116, 116, 117, 118, 123,
		124, 126, 127, 132, 133, 135, 136, 138, 139, 141, 142, 144, 145, 147, 148, 150,
		151, 153, 154, 204, 205, 206, 207, 208, 211, 212, 211, 212, 211, 212, 215, 216,
		217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232,
		233, 234, 235, 236, 237, 238, 253
	};

	for (Vehicle *v : Vehicle::Iterate()) {
		if (v->engine_type >= lengthof(tto_to_ttd)) return false;
		v->engine_type = tto_to_ttd[v->engine_type];
	}

	/* Load the default engine set. Many of them will be overridden later */
	uint j = 0;
	for (uint i = 0; i < lengthof(_orig_rail_vehicle_info); i++, j++) new (GetTempDataEngine(j)) Engine(VEH_TRAIN, i);
	for (uint i = 0; i < lengthof(_orig_road_vehicle_info); i++, j++) new (GetTempDataEngine(j)) Engine(VEH_ROAD, i);
	for (uint i = 0; i < lengthof(_orig_ship_vehicle_info); i++, j++) new (GetTempDataEngine(j)) Engine(VEH_SHIP, i);
	for (uint i = 0; i < lengthof(_orig_aircraft_vehicle_info); i++, j++) new (GetTempDataEngine(j)) Engine(VEH_AIRCRAFT, i);

	Date aging_date = std::min(_date + DAYS_TILL_ORIGINAL_BASE_YEAR, ConvertYMDToDate(2050, 0, 1));

	for (EngineID i = 0; i < 256; i++) {
		int oi = ttd_to_tto[i];
		Engine *e = GetTempDataEngine(i);

		if (oi == 255) {
			/* Default engine is used */
			_date += DAYS_TILL_ORIGINAL_BASE_YEAR;
			StartupOneEngine(e, aging_date);
			e->intro_date -= DAYS_TILL_ORIGINAL_BASE_YEAR;
			_date -= DAYS_TILL_ORIGINAL_BASE_YEAR;

			/* Make sure for example monorail and maglev are available when they should be */
			if (_date >= e->intro_date && HasBit(e->info.climates, 0)) {
				e->flags |= ENGINE_AVAILABLE;
				e->company_avail = (CompanyMask)0xFF;
				e->age = _date > e->intro_date ? (_date - e->intro_date) / 30 : 0;
			}
		} else {
			/* Using data from TTO savegame */
			Engine *oe = &_old_engines[oi];

			e->intro_date          = oe->intro_date;
			e->age                 = oe->age;
			e->reliability         = oe->reliability;
			e->reliability_spd_dec = oe->reliability_spd_dec;
			e->reliability_start   = oe->reliability_start;
			e->reliability_max     = oe->reliability_max;
			e->reliability_final   = oe->reliability_final;
			e->duration_phase_1    = oe->duration_phase_1;
			e->duration_phase_2    = oe->duration_phase_2;
			e->duration_phase_3    = oe->duration_phase_3;
			e->flags               = oe->flags;

			e->company_avail = 0;

			/* One or more engines were remapped to this one. Make this engine available
			 * if at least one of them was available. */
			for (uint j = 0; j < lengthof(tto_to_ttd); j++) {
				if (tto_to_ttd[j] == i && _old_engines[j].company_avail != 0) {
					e->company_avail = (CompanyMask)0xFF;
					e->flags |= ENGINE_AVAILABLE;
					break;
				}
			}

			e->info.climates = 1;
		}

		e->preview_company = INVALID_COMPANY;
		e->preview_asked = (CompanyMask)-1;
		e->preview_wait = 0;
		e->name = std::string{};
	}

	return true;
}

static void FixTTOCompanies()
{
	for (Company *c : Company::Iterate()) {
		c->cur_economy.company_value = CalculateCompanyValue(c); // company value history is zeroed
	}
}

static inline byte RemapTTOColour(byte tto)
{
	/** Lossy remapping of TTO colours to TTD colours. SVXConverter uses the same conversion. */
	static const byte tto_colour_remap[] = {
		COLOUR_DARK_BLUE,  COLOUR_GREY,       COLOUR_YELLOW,     COLOUR_RED,
		COLOUR_PURPLE,     COLOUR_DARK_GREEN, COLOUR_ORANGE,     COLOUR_PALE_GREEN,
		COLOUR_BLUE,       COLOUR_GREEN,      COLOUR_CREAM,      COLOUR_BROWN,
		COLOUR_WHITE,      COLOUR_LIGHT_BLUE, COLOUR_MAUVE,      COLOUR_PINK
	};

	if ((size_t)tto >= lengthof(tto_colour_remap)) return COLOUR_GREY; // this shouldn't happen

	return tto_colour_remap[tto];
}

static inline uint RemapTownIndex(uint x)
{
	return _savegame_type == SGT_TTO ? (x - 0x264) / 78 : (x - 0x264) / 94;
}

static inline uint RemapOrderIndex(uint x)
{
	return _savegame_type == SGT_TTO ? (x - 0x1AC4) / 2 : (x - 0x1C18) / 2;
}

extern std::vector<TileIndex> _animated_tiles;
extern char *_old_name_array;

static uint32 _old_town_index;
static uint16 _old_string_id;
static uint16 _old_string_id_2;

static void ReadTTDPatchFlags()
{
	if (_read_ttdpatch_flags) return;

	_read_ttdpatch_flags = true;

	/* Set default values */
	_old_vehicle_multiplier = 1;
	_ttdp_version = 0;
	_old_extra_chunk_nums = 0;
	_bump_assert_value = 0;

	if (_savegame_type == SGT_TTO) return;

	/* TTDPatch misuses _old_map3 for flags.. read them! */
	_old_vehicle_multiplier = _old_map3[0];
	/* Somehow.... there was an error in some savegames, so 0 becomes 1
	 * and 1 becomes 2. The rest of the values are okay */
	if (_old_vehicle_multiplier < 2) _old_vehicle_multiplier++;

	_old_vehicle_names = MallocT<StringID>(_old_vehicle_multiplier * 850);

	/* TTDPatch increases the Vehicle-part in the middle of the game,
	 * so if the multiplier is anything else but 1, the assert fails..
	 * bump the assert value so it doesn't!
	 * (1 multiplier == 850 vehicles
	 * 1 vehicle   == 128 bytes */
	_bump_assert_value = (_old_vehicle_multiplier - 1) * 850 * 128;

	for (uint i = 0; i < 17; i++) { // check tile 0, too
		if (_old_map3[i] != 0) _savegame_type = SGT_TTDP1;
	}

	/* Check if we have a modern TTDPatch savegame (has extra data all around) */
	if (memcmp(&_old_map3[0x1FFFA], "TTDp", 4) == 0) _savegame_type = SGT_TTDP2;

	_old_extra_chunk_nums = _old_map3[_savegame_type == SGT_TTDP2 ? 0x1FFFE : 0x2];

	/* Clean the misused places */
	for (uint i = 0;       i < 17;      i++) _old_map3[i] = 0;
	for (uint i = 0x1FE00; i < 0x20000; i++) _old_map3[i] = 0;

	if (_savegame_type == SGT_TTDP2) DEBUG(oldloader, 2, "Found TTDPatch game");

	DEBUG(oldloader, 3, "Vehicle-multiplier is set to %d (%d vehicles)", _old_vehicle_multiplier, _old_vehicle_multiplier * 850);
}

static const OldChunks town_chunk[] = {
	OCL_SVAR(   OC_TILE, Town, xy ),
	OCL_NULL( 2 ),         ///< population,        no longer in use
	OCL_SVAR( OC_UINT16, Town, townnametype ),
	OCL_SVAR( OC_UINT32, Town, townnameparts ),
	OCL_SVAR(  OC_FILE_U8 | OC_VAR_U16, Town, grow_counter ),
	OCL_NULL( 1 ),         ///< sort_index,        no longer in use
	OCL_NULL( 4 ),         ///< sign-coordinates,  no longer in use
	OCL_NULL( 2 ),         ///< namewidth,         no longer in use
	OCL_SVAR( OC_FILE_U16 |  OC_VAR_U8, Town, flags ),
	OCL_NULL( 10 ),        ///< radius,            no longer in use

	OCL_SVAR( OC_INT16, Town, ratings[0] ),
	OCL_SVAR( OC_INT16, Town, ratings[1] ),
	OCL_SVAR( OC_INT16, Town, ratings[2] ),
	OCL_SVAR( OC_INT16, Town, ratings[3] ),
	OCL_SVAR( OC_INT16, Town, ratings[4] ),
	OCL_SVAR( OC_INT16, Town, ratings[5] ),
	OCL_SVAR( OC_INT16, Town, ratings[6] ),
	OCL_SVAR( OC_INT16, Town, ratings[7] ),

	OCL_SVAR( OC_FILE_U32 | OC_VAR_U16, Town, have_ratings ),
	OCL_SVAR( OC_FILE_U32 | OC_VAR_U16, Town, statues ),
	OCL_NULL( 2 ),         ///< num_houses,        no longer in use
	OCL_SVAR(  OC_FILE_U8 | OC_VAR_U16, Town, time_until_rebuild ),
	OCL_SVAR(  OC_FILE_U8 | OC_VAR_U16, Town, growth_rate ),

	OCL_SVAR( OC_FILE_U16 | OC_VAR_U32, Town, supplied[CT_PASSENGERS].new_max ),
	OCL_SVAR( OC_FILE_U16 | OC_VAR_U32, Town, supplied[CT_MAIL].new_max ),
	OCL_SVAR( OC_FILE_U16 | OC_VAR_U32, Town, supplied[CT_PASSENGERS].new_act ),
	OCL_SVAR( OC_FILE_U16 | OC_VAR_U32, Town, supplied[CT_MAIL].new_act ),
	OCL_SVAR( OC_FILE_U16 | OC_VAR_U32, Town, supplied[CT_PASSENGERS].old_max ),
	OCL_SVAR( OC_FILE_U16 | OC_VAR_U32, Town, supplied[CT_MAIL].old_max ),
	OCL_SVAR( OC_FILE_U16 | OC_VAR_U32, Town, supplied[CT_PASSENGERS].old_act ),
	OCL_SVAR( OC_FILE_U16 | OC_VAR_U32, Town, supplied[CT_MAIL].old_act ),

	OCL_NULL( 2 ),         ///< pct_pass_transported / pct_mail_transported, now computed on the fly

	OCL_SVAR( OC_TTD | OC_UINT16, Town, received[TE_FOOD].new_act ),
	OCL_SVAR( OC_TTD | OC_UINT16, Town, received[TE_WATER].new_act ),
	OCL_SVAR( OC_TTD | OC_UINT16, Town, received[TE_FOOD].old_act ),
	OCL_SVAR( OC_TTD | OC_UINT16, Town, received[TE_WATER].old_act ),

	OCL_SVAR(  OC_UINT8, Town, road_build_months ),
	OCL_SVAR(  OC_UINT8, Town, fund_buildings_months ),

	OCL_CNULL( OC_TTD, 8 ),         ///< some junk at the end of the record

	OCL_END()
};

static bool LoadOldTown(LoadgameState *ls, int num)
{
	Town *t = new (num) Town();
	if (!LoadChunk(ls, t, town_chunk)) return false;

	if (t->xy != 0) {
		if (_savegame_type == SGT_TTO) {
			/* 0x10B6 is auto-generated name, others are custom names */
			t->townnametype = t->townnametype == 0x10B6 ? 0x20C1 : t->townnametype + 0x2A00;
		}
	} else {
		delete t;
	}

	return true;
}

static uint16 _old_order;
static const OldChunks order_chunk[] = {
	OCL_VAR ( OC_UINT16,   1, &_old_order ),
	OCL_END()
};

static bool LoadOldOrder(LoadgameState *ls, int num)
{
	if (!LoadChunk(ls, nullptr, order_chunk)) return false;

	Order *o = new (num) Order();
	o->AssignOrder(UnpackOldOrder(_old_order));

	if (o->IsType(OT_NOTHING)) {
		delete o;
	} else {
		/* Relink the orders to each other (in the orders for one vehicle are behind each other,
		 * with an invalid order (OT_NOTHING) as indication that it is the last order */
		Order *prev = Order::GetIfValid(num - 1);
		if (prev != nullptr) prev->next = o;
	}

	return true;
}

static bool LoadOldAnimTileList(LoadgameState *ls, int num)
{
	TileIndex anim_list[256];
	const OldChunks anim_chunk[] = {
		OCL_VAR (   OC_TILE, 256, anim_list ),
		OCL_END ()
	};

	if (!LoadChunk(ls, nullptr, anim_chunk)) return false;

	/* The first zero in the loaded array indicates the end of the list. */
	for (int i = 0; i < 256; i++) {
		if (anim_list[i] == 0) break;
		_animated_tiles.push_back(anim_list[i]);
	}

	return true;
}

static const OldChunks depot_chunk[] = {
	OCL_SVAR(   OC_TILE, Depot, xy ),
	OCL_VAR ( OC_UINT32,                1, &_old_town_index ),
	OCL_END()
};

static bool LoadOldDepot(LoadgameState *ls, int num)
{
	Depot *d = new (num) Depot();
	if (!LoadChunk(ls, d, depot_chunk)) return false;

	if (d->xy != 0) {
		/* In some cases, there could be depots referencing invalid town. */
		Town *t = Town::GetIfValid(RemapTownIndex(_old_town_index));
		if (t == nullptr) t = Town::GetRandom();
		d->town = t;
	} else {
		delete d;
	}

	return true;
}

static StationID _current_station_id;
static uint16 _waiting_acceptance;
static uint8  _cargo_source;
static uint8  _cargo_days;

static const OldChunks goods_chunk[] = {
	OCL_VAR ( OC_UINT16, 1,          &_waiting_acceptance ),
	OCL_SVAR(  OC_UINT8, GoodsEntry, time_since_pickup ),
	OCL_SVAR(  OC_UINT8, GoodsEntry, rating ),
	OCL_VAR (  OC_UINT8, 1,          &_cargo_source ),
	OCL_VAR (  OC_UINT8, 1,          &_cargo_days ),
	OCL_SVAR(  OC_UINT8, GoodsEntry, last_speed ),
	OCL_SVAR(  OC_UINT8, GoodsEntry, last_age ),

	OCL_END()
};

static bool LoadOldGood(LoadgameState *ls, int num)
{
	/* for TTO games, 12th (num == 11) goods entry is created in the Station constructor */
	if (_savegame_type == SGT_TTO && num == 11) return true;

	Station *st = Station::Get(_current_station_id);
	GoodsEntry *ge = &st->goods[num];

	if (!LoadChunk(ls, ge, goods_chunk)) return false;

	SB(ge->status, GoodsEntry::GES_ACCEPTANCE, 1, HasBit(_waiting_acceptance, 15));
	SB(ge->status, GoodsEntry::GES_RATING, 1, _cargo_source != 0xFF);
	if (GB(_waiting_acceptance, 0, 12) != 0 && CargoPacket::CanAllocateItem()) {
		ge->cargo.Append(new CargoPacket(GB(_waiting_acceptance, 0, 12), _cargo_days, (_cargo_source == 0xFF) ? INVALID_STATION : _cargo_source, 0, 0),
				INVALID_STATION);
	}

	return true;
}

static const OldChunks station_chunk[] = {
	OCL_SVAR(   OC_TILE, Station, xy ),
	OCL_VAR ( OC_UINT32,   1, &_old_town_index ),

	OCL_NULL( 4 ), ///< bus/lorry tile
	OCL_SVAR(   OC_TILE, Station, train_station.tile ),
	OCL_SVAR(   OC_TILE, Station, airport.tile ),
	OCL_NULL( 2 ), ///< dock tile
	OCL_SVAR( OC_FILE_U8 | OC_VAR_U16, Station, train_station.w ),

	OCL_NULL( 1 ),         ///< sort-index, no longer in use
	OCL_NULL( 2 ),         ///< sign-width, no longer in use

	OCL_VAR ( OC_UINT16,   1, &_old_string_id ),

	OCL_NULL( 4 ),         ///< sign left/top, no longer in use

	OCL_SVAR( OC_FILE_U16 | OC_VAR_U8, Station, had_vehicle_of_type ),

	OCL_CHUNK( 12, LoadOldGood ),

	OCL_SVAR(  OC_UINT8, Station, time_since_load ),
	OCL_SVAR(  OC_UINT8, Station, time_since_unload ),
	OCL_SVAR(  OC_UINT8, Station, delete_ctr ),
	OCL_SVAR(  OC_UINT8, Station, owner ),
	OCL_SVAR(  OC_UINT8, Station, facilities ),
	OCL_SVAR( OC_TTD | OC_UINT8, Station, airport.type ),
	OCL_SVAR( OC_TTO | OC_FILE_U16 | OC_VAR_U64, Station, airport.flags ),
	OCL_NULL( 3 ),          ///< bus/truck status, blocked months, no longer in use
	OCL_CNULL( OC_TTD, 1 ), ///< unknown
	OCL_SVAR( OC_TTD | OC_FILE_U16 | OC_VAR_U64, Station, airport.flags ),
	OCL_CNULL( OC_TTD, 2 ), ///< last_vehicle. now last_vehicle_type
	OCL_CNULL( OC_TTD, 4 ), ///< junk at end of chunk

	OCL_END()
};

static bool LoadOldStation(LoadgameState *ls, int num)
{
	Station *st = new (num) Station();
	_current_station_id = num;

	if (!LoadChunk(ls, st, station_chunk)) return false;

	if (st->xy != 0) {
		st->town = Town::Get(RemapTownIndex(_old_town_index));

		if (_savegame_type == SGT_TTO) {
			if (IsInsideBS(_old_string_id, 0x180F, 32)) {
				st->string_id = STR_SV_STNAME + (_old_string_id - 0x180F); // automatic name
			} else {
				st->string_id = _old_string_id + 0x2800; // custom name
			}

			if (HasBit(st->airport.flags, 8)) {
				st->airport.type = 1; // large airport
			} else if (HasBit(st->airport.flags, 6)) {
				st->airport.type = 3; // oil rig
			} else {
				st->airport.type = 0; // small airport
			}
		} else {
			st->string_id = RemapOldStringID(_old_string_id);
		}
	} else {
		delete st;
	}

	return true;
}

static const OldChunks industry_chunk[] = {
	OCL_SVAR(   OC_TILE, Industry, location.tile ),
	OCL_VAR ( OC_UINT32,   1, &_old_town_index ),
	OCL_SVAR( OC_FILE_U8 | OC_VAR_U16, Industry, location.w ),
	OCL_SVAR( OC_FILE_U8 | OC_VAR_U16, Industry, location.h ),
	OCL_NULL( 2 ),  ///< used to be industry's produced_cargo

	OCL_SVAR( OC_TTD | OC_UINT16, Industry, produced_cargo_waiting[0] ),
	OCL_SVAR( OC_TTD | OC_UINT16, Industry, produced_cargo_waiting[1] ),
	OCL_SVAR( OC_TTO | OC_FILE_U8 | OC_VAR_U16, Industry, produced_cargo_waiting[0] ),
	OCL_SVAR( OC_TTO | OC_FILE_U8 | OC_VAR_U16, Industry, produced_cargo_waiting[1] ),

	OCL_SVAR(  OC_UINT8, Industry, production_rate[0] ),
	OCL_SVAR(  OC_UINT8, Industry, production_rate[1] ),

	OCL_NULL( 3 ),  ///< used to be industry's accepts_cargo

	OCL_SVAR(  OC_UINT8, Industry, prod_level ),

	OCL_SVAR( OC_UINT16, Industry, this_month_production[0] ),
	OCL_SVAR( OC_UINT16, Industry, this_month_production[1] ),
	OCL_SVAR( OC_UINT16, Industry, this_month_transported[0] ),
	OCL_SVAR( OC_UINT16, Industry, this_month_transported[1] ),

	OCL_SVAR(  OC_UINT8, Industry, past_pct_transported[0][0] ),
	OCL_SVAR(  OC_UINT8, Industry, past_pct_transported[0][1] ),

	OCL_SVAR( OC_UINT16, Industry, past_production[0][0] ),
	OCL_SVAR( OC_UINT16, Industry, past_production[0][1] ),
	OCL_SVAR( OC_UINT16, Industry, past_transported[0][0] ),
	OCL_SVAR( OC_UINT16, Industry, past_transported[0][1] ),

	OCL_SVAR(  OC_UINT8, Industry, type ),
	OCL_SVAR( OC_TTO | OC_FILE_U8 | OC_VAR_U16, Industry, counter ),
	OCL_SVAR(  OC_UINT8, Industry, owner ),
	OCL_SVAR(  OC_UINT8, Industry, random_colour ),
	OCL_SVAR( OC_TTD | OC_FILE_U8 | OC_VAR_I32, Industry, last_prod_year ),
	OCL_SVAR( OC_TTD | OC_UINT16, Industry, counter ),
	OCL_SVAR( OC_TTD | OC_UINT8, Industry, was_cargo_delivered ),

	OCL_CNULL( OC_TTD, 9 ), ///< Random junk at the end of this chunk

	OCL_END()
};

static bool LoadOldIndustry(LoadgameState *ls, int num)
{
	Industry *i = new (num) Industry();
	if (!LoadChunk(ls, i, industry_chunk)) return false;

	if (i->location.tile != 0) {
		i->town = Town::Get(RemapTownIndex(_old_town_index));

		if (_savegame_type == SGT_TTO) {
			if (i->type > 0x06) i->type++; // Printing Works were added
			if (i->type == 0x0A) i->type = 0x12; // Iron Ore Mine has different ID

			YearMonthDay ymd;
			ConvertDateToYMD(_date, &ymd);
			i->last_prod_year = ymd.year;

			i->random_colour = RemapTTOColour(i->random_colour);
		}

		Industry::IncIndustryTypeCount(i->type);
	} else {
		delete i;
	}

	return true;
}

static CompanyID _current_company_id;
static int32 _old_yearly;

static const OldChunks _company_yearly_chunk[] = {
	OCL_VAR(  OC_INT32,   1, &_old_yearly ),
	OCL_END()
};

static bool LoadOldCompanyYearly(LoadgameState *ls, int num)
{
	Company *c = Company::Get(_current_company_id);

	for (uint i = 0; i < 13; i++) {
		if (_savegame_type == SGT_TTO && i == 6) {
			_old_yearly = 0; // property maintenance
		} else {
			if (!LoadChunk(ls, nullptr, _company_yearly_chunk)) return false;
		}

		c->yearly_expenses[num][i] = _old_yearly;
	}

	return true;
}

static const OldChunks _company_economy_chunk[] = {
	OCL_SVAR( OC_FILE_I32 | OC_VAR_I64, CompanyEconomyEntry, income ),
	OCL_SVAR( OC_FILE_I32 | OC_VAR_I64, CompanyEconomyEntry, expenses ),
	OCL_SVAR( OC_INT32,                 CompanyEconomyEntry, delivered_cargo[NUM_CARGO - 1] ),
	OCL_SVAR( OC_INT32,                 CompanyEconomyEntry, performance_history ),
	OCL_SVAR( OC_TTD | OC_FILE_I32 | OC_VAR_I64, CompanyEconomyEntry, company_value ),

	OCL_END()
};

static bool LoadOldCompanyEconomy(LoadgameState *ls, int num)
{
	Company *c = Company::Get(_current_company_id);

	if (!LoadChunk(ls, &c->cur_economy, _company_economy_chunk)) return false;

	/* Don't ask, but the number in TTD(Patch) are inversed to OpenTTD */
	c->cur_economy.income   = -c->cur_economy.income;
	c->cur_economy.expenses = -c->cur_economy.expenses;

	for (uint i = 0; i < 24; i++) {
		if (!LoadChunk(ls, &c->old_economy[i], _company_economy_chunk)) return false;

		c->old_economy[i].income   = -c->old_economy[i].income;
		c->old_economy[i].expenses = -c->old_economy[i].expenses;
	}

	return true;
}

static const OldChunks _company_chunk[] = {
	OCL_VAR ( OC_UINT16,   1, &_old_string_id ),
	OCL_SVAR( OC_UINT32, Company, name_2 ),
	OCL_SVAR( OC_UINT32, Company, face ),
	OCL_VAR ( OC_UINT16,   1, &_old_string_id_2 ),
	OCL_SVAR( OC_UINT32, Company, president_name_2 ),

	OCL_SVAR( OC_FILE_I32 | OC_VAR_I64, Company, money ),
	OCL_SVAR( OC_FILE_I32 | OC_VAR_I64, Company, current_loan ),

	OCL_SVAR(  OC_UINT8, Company, colour ),
	OCL_SVAR(  OC_UINT8, Company, money_fraction ),
	OCL_SVAR(  OC_UINT8, Company, months_of_bankruptcy ),
	OCL_SVAR( OC_FILE_U8  | OC_VAR_U16, Company, bankrupt_asked ),
	OCL_SVAR( OC_FILE_U32 | OC_VAR_I64, Company, bankrupt_value ),
	OCL_SVAR( OC_UINT16, Company, bankrupt_timeout ),

	OCL_CNULL( OC_TTD, 4 ), // cargo_types
	OCL_CNULL( OC_TTO, 2 ), // cargo_types

	OCL_CHUNK( 3, LoadOldCompanyYearly ),
	OCL_CHUNK( 1, LoadOldCompanyEconomy ),

	OCL_SVAR( OC_FILE_U16 | OC_VAR_I32, Company, inaugurated_year),
	OCL_SVAR(                  OC_TILE, Company, last_build_coordinate ),
	OCL_SVAR(                 OC_UINT8, Company, num_valid_stat_ent ),

	OCL_NULL( 230 ),         // Old AI

	OCL_SVAR(  OC_UINT8, Company, block_preview ),
	OCL_CNULL( OC_TTD, 1 ),           // Old AI
	OCL_CNULL( OC_TTD, 1 ), // avail_railtypes
	OCL_SVAR(   OC_TILE, Company, location_of_HQ ),
	OCL_SVAR( OC_TTD | OC_UINT8, Company, share_owners[0] ),
	OCL_SVAR( OC_TTD | OC_UINT8, Company, share_owners[1] ),
	OCL_SVAR( OC_TTD | OC_UINT8, Company, share_owners[2] ),
	OCL_SVAR( OC_TTD | OC_UINT8, Company, share_owners[3] ),

	OCL_CNULL( OC_TTD, 8 ), ///< junk at end of chunk

	OCL_END()
};

static bool LoadOldCompany(LoadgameState *ls, int num)
{
	Company *c = new (num) Company();

	_current_company_id = (CompanyID)num;

	if (!LoadChunk(ls, c, _company_chunk)) return false;

	if (_old_string_id == 0) {
		delete c;
		return true;
	}

	if (_savegame_type == SGT_TTO) {
		/* adjust manager's face */
		if (HasBit(c->face, 27) && GB(c->face, 26, 1) == GB(c->face, 19, 1)) {
			/* if face would be black in TTD, adjust tie colour and thereby face colour */
			ClrBit(c->face, 27);
		}

		/* Company name */
		if (_old_string_id == 0 || _old_string_id == 0x4C00) {
			_old_string_id = STR_SV_UNNAMED; // "Unnamed"
		} else if (GB(_old_string_id, 8, 8) == 0x52) {
			_old_string_id += 0x2A00; // Custom name
		} else {
			_old_string_id = RemapOldStringID(_old_string_id += 0x240D); // Automatic name
		}
		c->name_1 = _old_string_id;

		/* Manager name */
		switch (_old_string_id_2) {
			case 0x4CDA: _old_string_id_2 = SPECSTR_PRESIDENT_NAME;    break; // automatic name
			case 0x0006: _old_string_id_2 = STR_SV_EMPTY;              break; // empty name
			default:     _old_string_id_2 = _old_string_id_2 + 0x2A00; break; // custom name
		}
		c->president_name_1 = _old_string_id_2;

		c->colour = RemapTTOColour(c->colour);

		if (num != 0) c->is_ai = true;
	} else {
		c->name_1 = RemapOldStringID(_old_string_id);
		c->president_name_1 = RemapOldStringID(_old_string_id_2);

		if (num == 0) {
			/* If the first company has no name, make sure we call it UNNAMED */
			if (c->name_1 == 0) {
				c->name_1 = STR_SV_UNNAMED;
			}
		} else {
			/* Beside some multiplayer maps (1 on 1), which we don't official support,
			 * all other companies are an AI.. mark them as such */
			c->is_ai = true;
		}

		/* Sometimes it is better to not ask.. in old scenarios, the money
		 * was always 893288 pounds. In the newer versions this is correct,
		 * but correct for those oldies
		 * Ps: this also means that if you had exact 893288 pounds, you will go back
		 * to 100000.. this is a very VERY small chance ;) */
		if (c->money == 893288) c->money = c->current_loan = 100000;
	}

	_company_colours[num] = (Colours)c->colour;
	c->inaugurated_year -= ORIGINAL_BASE_YEAR;

	return true;
}

static uint32 _old_order_ptr;
static uint16 _old_next_ptr;
static VehicleID _current_vehicle_id;

static const OldChunks vehicle_train_chunk[] = {
	OCL_SVAR(  OC_UINT8, Train, track ),
	OCL_SVAR(  OC_UINT8, Train, force_proceed ),
	OCL_SVAR( OC_UINT16, Train, crash_anim_pos ),
	OCL_SVAR(  OC_UINT8, Train, railtype ),

	OCL_NULL( 5 ), ///< Junk

	OCL_END()
};

static const OldChunks vehicle_road_chunk[] = {
	OCL_SVAR(  OC_UINT8, RoadVehicle, state ),
	OCL_SVAR(  OC_UINT8, RoadVehicle, frame ),
	OCL_SVAR( OC_UINT16, RoadVehicle, blocked_ctr ),
	OCL_SVAR(  OC_UINT8, RoadVehicle, overtaking ),
	OCL_SVAR(  OC_UINT8, RoadVehicle, overtaking_ctr ),
	OCL_SVAR( OC_UINT16, RoadVehicle, crashed_ctr ),
	OCL_SVAR(  OC_UINT8, RoadVehicle, reverse_ctr ),

	OCL_NULL( 1 ), ///< Junk

	OCL_END()
};

static const OldChunks vehicle_ship_chunk[] = {
	OCL_SVAR(  OC_UINT8, Ship, state ),

	OCL_NULL( 9 ), ///< Junk

	OCL_END()
};

static const OldChunks vehicle_air_chunk[] = {
	OCL_SVAR(  OC_UINT8, Aircraft, pos ),
	OCL_SVAR(  OC_FILE_U8 | OC_VAR_U16, Aircraft, targetairport ),
	OCL_SVAR( OC_UINT16, Aircraft, crashed_counter ),
	OCL_SVAR(  OC_UINT8, Aircraft, state ),

	OCL_NULL( 5 ), ///< Junk

	OCL_END()
};

static const OldChunks vehicle_effect_chunk[] = {
	OCL_SVAR( OC_UINT16, EffectVehicle, animation_state ),
	OCL_SVAR(  OC_UINT8, EffectVehicle, animation_substate ),

	OCL_NULL( 7 ), // Junk

	OCL_END()
};

static const OldChunks vehicle_disaster_chunk[] = {
	OCL_SVAR( OC_UINT16, DisasterVehicle, image_override ),
	OCL_SVAR( OC_UINT16, DisasterVehicle, big_ufo_destroyer_target ),

	OCL_NULL( 6 ), ///< Junk

	OCL_END()
};

static const OldChunks vehicle_empty_chunk[] = {
	OCL_NULL( 10 ), ///< Junk

	OCL_END()
};

static bool LoadOldVehicleUnion(LoadgameState *ls, int num)
{
	Vehicle *v = Vehicle::GetIfValid(_current_vehicle_id);
	uint temp = ls->total_read;
	bool res;

	if (v == nullptr) {
		res = LoadChunk(ls, nullptr, vehicle_empty_chunk);
	} else {
		switch (v->type) {
			default: SlErrorCorrupt("Invalid vehicle type");
			case VEH_TRAIN   : res = LoadChunk(ls, v, vehicle_train_chunk);    break;
			case VEH_ROAD    : res = LoadChunk(ls, v, vehicle_road_chunk);     break;
			case VEH_SHIP    : res = LoadChunk(ls, v, vehicle_ship_chunk);     break;
			case VEH_AIRCRAFT: res = LoadChunk(ls, v, vehicle_air_chunk);      break;
			case VEH_EFFECT  : res = LoadChunk(ls, v, vehicle_effect_chunk);   break;
			case VEH_DISASTER: res = LoadChunk(ls, v, vehicle_disaster_chunk); break;
		}
	}

	/* This chunk size should always be 10 bytes */
	if (ls->total_read - temp != 10) {
		DEBUG(oldloader, 0, "Assert failed in VehicleUnion: invalid chunk size");
		return false;
	}

	return res;
}

static uint16 _cargo_count;

static const OldChunks vehicle_chunk[] = {
	OCL_SVAR(  OC_UINT8, Vehicle, subtype ),

	OCL_NULL( 2 ),         ///< Hash, calculated automatically
	OCL_NULL( 2 ),         ///< Index, calculated automatically

	OCL_VAR ( OC_UINT32,   1, &_old_order_ptr ),
	OCL_VAR ( OC_UINT16,   1, &_old_order ),

	OCL_NULL ( 1 ), ///< num_orders, now calculated
	OCL_SVAR(  OC_UINT8, Vehicle, cur_implicit_order_index ),
	OCL_SVAR(   OC_TILE, Vehicle, dest_tile ),
	OCL_SVAR( OC_UINT16, Vehicle, load_unload_ticks ),
	OCL_SVAR( OC_FILE_U16 | OC_VAR_U32, Vehicle, date_of_last_service ),
	OCL_SVAR( OC_UINT16, Vehicle, service_interval ),
	OCL_SVAR( OC_FILE_U8 | OC_VAR_U16, Vehicle, last_station_visited ),
	OCL_SVAR( OC_TTD | OC_UINT8, Vehicle, tick_counter ),
	OCL_CNULL( OC_TTD, 2 ), ///< max_speed, now it is calculated.
	OCL_CNULL( OC_TTO, 1 ), ///< max_speed, now it is calculated.

	OCL_SVAR( OC_FILE_U16 | OC_VAR_I32, Vehicle, x_pos ),
	OCL_SVAR( OC_FILE_U16 | OC_VAR_I32, Vehicle, y_pos ),
	OCL_SVAR( OC_FILE_U8  | OC_VAR_I32, Vehicle, z_pos ),
	OCL_SVAR(  OC_UINT8, Vehicle, direction ),
	OCL_NULL( 2 ),         ///< x_offs and y_offs, calculated automatically
	OCL_NULL( 2 ),         ///< x_extent and y_extent, calculated automatically
	OCL_NULL( 1 ),         ///< z_extent, calculated automatically

	OCL_SVAR(  OC_UINT8, Vehicle, owner ),
	OCL_SVAR(   OC_TILE, Vehicle, tile ),
	OCL_SVAR( OC_FILE_U16 | OC_VAR_U32, Vehicle, sprite_cache.sprite_seq.seq[0].sprite ),

	OCL_NULL( 8 ),        ///< Vehicle sprite box, calculated automatically

	OCL_SVAR( OC_FILE_U16 | OC_VAR_U8, Vehicle, vehstatus ),
	OCL_SVAR( OC_TTD | OC_UINT16, Vehicle, cur_speed ),
	OCL_SVAR( OC_TTO | OC_FILE_U8 | OC_VAR_U16, Vehicle, cur_speed ),
	OCL_SVAR(  OC_UINT8, Vehicle, subspeed ),
	OCL_SVAR(  OC_UINT8, Vehicle, acceleration ),
	OCL_SVAR(  OC_UINT8, Vehicle, progress ),

	OCL_SVAR(  OC_UINT8, Vehicle, cargo_type ),
	OCL_SVAR( OC_TTD | OC_UINT16, Vehicle, cargo_cap ),
	OCL_SVAR( OC_TTO | OC_FILE_U8 | OC_VAR_U16, Vehicle, cargo_cap ),
	OCL_VAR ( OC_TTD | OC_UINT16, 1, &_cargo_count ),
	OCL_VAR ( OC_TTO | OC_FILE_U8 | OC_VAR_U16, 1, &_cargo_count ),
	OCL_VAR (  OC_UINT8, 1,       &_cargo_source ),
	OCL_VAR (  OC_UINT8, 1,       &_cargo_days ),

	OCL_SVAR( OC_TTO | OC_UINT8, Vehicle, tick_counter ),

	OCL_SVAR( OC_FILE_U16 | OC_VAR_U32, Vehicle, age ),
	OCL_SVAR( OC_FILE_U16 | OC_VAR_U32, Vehicle, max_age ),
	OCL_SVAR( OC_FILE_U8 | OC_VAR_I32, Vehicle, build_year ),
	OCL_SVAR( OC_FILE_U8 | OC_VAR_U16, Vehicle, unitnumber ),

	OCL_SVAR( OC_TTD | OC_UINT16, Vehicle, engine_type ),
	OCL_SVAR( OC_TTO | OC_FILE_U8 | OC_VAR_U16, Vehicle, engine_type ),

	OCL_SVAR(  OC_UINT8, Vehicle, spritenum ),
	OCL_SVAR(  OC_UINT8, Vehicle, day_counter ),

	OCL_SVAR(  OC_UINT8, Vehicle, breakdowns_since_last_service ),
	OCL_SVAR(  OC_UINT8, Vehicle, breakdown_ctr ),
	OCL_SVAR(  OC_UINT8, Vehicle, breakdown_delay ),
	OCL_SVAR(  OC_UINT8, Vehicle, breakdown_chance ),

	OCL_CNULL( OC_TTO, 1 ),

	OCL_SVAR( OC_UINT16, Vehicle, reliability ),
	OCL_SVAR( OC_UINT16, Vehicle, reliability_spd_dec ),

	OCL_SVAR( OC_FILE_I32 | OC_VAR_I64, Vehicle, profit_this_year ),
	OCL_SVAR( OC_FILE_I32 | OC_VAR_I64, Vehicle, profit_last_year ),

	OCL_VAR ( OC_UINT16,   1, &_old_next_ptr ),

	OCL_SVAR( OC_FILE_U32 | OC_VAR_I64, Vehicle, value ),

	OCL_VAR ( OC_UINT16,   1, &_old_string_id ),

	OCL_CHUNK( 1, LoadOldVehicleUnion ),

	OCL_CNULL( OC_TTO, 24 ), ///< junk
	OCL_CNULL( OC_TTD, 20 ), ///< junk at end of struct (TTDPatch has some data in it)

	OCL_END()
};

/**
 * Load the vehicles of an old style savegame.
 * @param ls  State (buffer) of the currently loaded game.
 * @param num The number of vehicles to load.
 * @return True iff loading went without problems.
 */
bool LoadOldVehicle(LoadgameState *ls, int num)
{
	/* Read the TTDPatch flags, because we need some info from it */
	ReadTTDPatchFlags();

	for (uint i = 0; i < _old_vehicle_multiplier; i++) {
		_current_vehicle_id = num * _old_vehicle_multiplier + i;

		Vehicle *v;

		if (_savegame_type == SGT_TTO) {
			uint type = ReadByte(ls);
			switch (type) {
				default: return false;
				case 0x00 /* VEH_INVALID  */: v = nullptr;                                        break;
				case 0x25 /* MONORAIL     */:
				case 0x20 /* VEH_TRAIN    */: v = new (_current_vehicle_id) Train();           break;
				case 0x21 /* VEH_ROAD     */: v = new (_current_vehicle_id) RoadVehicle();     break;
				case 0x22 /* VEH_SHIP     */: v = new (_current_vehicle_id) Ship();            break;
				case 0x23 /* VEH_AIRCRAFT */: v = new (_current_vehicle_id) Aircraft();        break;
				case 0x24 /* VEH_EFFECT   */: v = new (_current_vehicle_id) EffectVehicle();   break;
				case 0x26 /* VEH_DISASTER */: v = new (_current_vehicle_id) DisasterVehicle(); break;
			}

			if (!LoadChunk(ls, v, vehicle_chunk)) return false;
			if (v == nullptr) continue;
			v->refit_cap = v->cargo_cap;

			SpriteID sprite = v->sprite_cache.sprite_seq.seq[0].sprite;
			/* no need to override other sprites */
			if (IsInsideMM(sprite, 1460, 1465)) {
				sprite += 580; // aircraft smoke puff
			} else if (IsInsideMM(sprite, 2096, 2115)) {
				sprite += 977; // special effects part 1
			} else if (IsInsideMM(sprite, 2396, 2436)) {
				sprite += 1305; // special effects part 2
			} else if (IsInsideMM(sprite, 2516, 2539)) {
				sprite += 1385; // rotor or disaster-related vehicles
			}
			v->sprite_cache.sprite_seq.seq[0].sprite = sprite;

			switch (v->type) {
				case VEH_TRAIN: {
					static const byte spriteset_rail[] = {
						  0,   2,   4,   4,   8,  10,  12,  14,  16,  18,  20,  22,  40,  42,  44,  46,
						 48,  52,  54,  66,  68,  70,  72,  74,  76,  78,  80,  82,  84,  86, 120, 122,
						124, 126, 128, 130, 132, 134, 136, 138, 140
					};
					if (v->spritenum / 2 >= lengthof(spriteset_rail)) return false;
					v->spritenum = spriteset_rail[v->spritenum / 2]; // adjust railway sprite set offset
					/* Should be the original values for monorail / rail, can't use RailType constants */
					Train::From(v)->railtype = static_cast<RailType>(type == 0x25 ? 1 : 0);
					break;
				}

				case VEH_ROAD:
					if (v->spritenum >= 22) v->spritenum += 12;
					break;

				case VEH_SHIP:
					v->spritenum += 2;

					switch (v->spritenum) {
						case 2: // oil tanker && cargo type != oil
							if (v->cargo_type != CT_OIL) v->spritenum = 0; // make it a coal/goods ship
							break;
						case 4: // passenger ship && cargo type == mail
							if (v->cargo_type == CT_MAIL) v->spritenum = 0; // make it a mail ship
							break;
						default:
							break;
					}
					break;

				default:
					break;
			}

			switch (_old_string_id) {
				case 0x0000: break; // empty (invalid vehicles)
				case 0x0006: _old_string_id  = STR_SV_EMPTY;              break; // empty (special vehicles)
				case 0x8495: _old_string_id  = STR_SV_TRAIN_NAME;         break; // "Train X"
				case 0x8842: _old_string_id  = STR_SV_ROAD_VEHICLE_NAME;  break; // "Road Vehicle X"
				case 0x8C3B: _old_string_id  = STR_SV_SHIP_NAME;          break; // "Ship X"
				case 0x9047: _old_string_id  = STR_SV_AIRCRAFT_NAME;      break; // "Aircraft X"
				default:     _old_string_id += 0x2A00;                    break; // custom name
			}

			_old_vehicle_names[_current_vehicle_id] = _old_string_id;
		} else {
			/* Read the vehicle type and allocate the right vehicle */
			switch (ReadByte(ls)) {
				default: SlErrorCorrupt("Invalid vehicle type");
				case 0x00 /* VEH_INVALID */: v = nullptr;                                        break;
				case 0x10 /* VEH_TRAIN   */: v = new (_current_vehicle_id) Train();           break;
				case 0x11 /* VEH_ROAD    */: v = new (_current_vehicle_id) RoadVehicle();     break;
				case 0x12 /* VEH_SHIP    */: v = new (_current_vehicle_id) Ship();            break;
				case 0x13 /* VEH_AIRCRAFT*/: v = new (_current_vehicle_id) Aircraft();        break;
				case 0x14 /* VEH_EFFECT  */: v = new (_current_vehicle_id) EffectVehicle();   break;
				case 0x15 /* VEH_DISASTER*/: v = new (_current_vehicle_id) DisasterVehicle(); break;
			}

			if (!LoadChunk(ls, v, vehicle_chunk)) return false;
			if (v == nullptr) continue;

			_old_vehicle_names[_current_vehicle_id] = RemapOldStringID(_old_string_id);

			/* This should be consistent, else we have a big problem... */
			if (v->index != _current_vehicle_id) {
				DEBUG(oldloader, 0, "Loading failed - vehicle-array is invalid");
				return false;
			}
		}

		if (_old_order_ptr != 0 && _old_order_ptr != 0xFFFFFFFF) {
			uint max = _savegame_type == SGT_TTO ? 3000 : 5000;
			uint old_id = RemapOrderIndex(_old_order_ptr);
			if (old_id < max) v->orders.old = Order::Get(old_id); // don't accept orders > max number of orders
		}
		v->current_order.AssignOrder(UnpackOldOrder(_old_order));

		v->next = (Vehicle *)(size_t)_old_next_ptr;

		if (_cargo_count != 0 && CargoPacket::CanAllocateItem()) {
			StationID source =    (_cargo_source == 0xFF) ? INVALID_STATION : _cargo_source;
			TileIndex source_xy = (source != INVALID_STATION) ? Station::Get(source)->xy : 0;
			v->cargo.Append(new CargoPacket(_cargo_count, _cargo_days, source, source_xy, source_xy));
		}
	}

	return true;
}

static const OldChunks sign_chunk[] = {
	OCL_VAR ( OC_UINT16, 1, &_old_string_id ),
	OCL_SVAR( OC_FILE_U16 | OC_VAR_I32, Sign, x ),
	OCL_SVAR( OC_FILE_U16 | OC_VAR_I32, Sign, y ),
	OCL_SVAR( OC_FILE_U16 | OC_VAR_I8, Sign, z ),

	OCL_NULL( 6 ),         ///< Width of sign, no longer in use

	OCL_END()
};

static bool LoadOldSign(LoadgameState *ls, int num)
{
	Sign *si = new (num) Sign();
	if (!LoadChunk(ls, si, sign_chunk)) return false;

	if (_old_string_id != 0) {
		if (_savegame_type == SGT_TTO) {
			if (_old_string_id != 0x140A) si->name = CopyFromOldName(_old_string_id + 0x2A00);
		} else {
			si->name = CopyFromOldName(RemapOldStringID(_old_string_id));
		}
		si->owner = OWNER_NONE;
	} else {
		delete si;
	}

	return true;
}

static const OldChunks engine_chunk[] = {
	OCL_SVAR( OC_UINT16, Engine, company_avail ),
	OCL_SVAR( OC_FILE_U16 | OC_VAR_U32, Engine, intro_date ),
	OCL_SVAR( OC_FILE_U16 | OC_VAR_U32, Engine, age ),
	OCL_SVAR( OC_UINT16, Engine, reliability ),
	OCL_SVAR( OC_UINT16, Engine, reliability_spd_dec ),
	OCL_SVAR( OC_UINT16, Engine, reliability_start ),
	OCL_SVAR( OC_UINT16, Engine, reliability_max ),
	OCL_SVAR( OC_UINT16, Engine, reliability_final ),
	OCL_SVAR( OC_UINT16, Engine, duration_phase_1 ),
	OCL_SVAR( OC_UINT16, Engine, duration_phase_2 ),
	OCL_SVAR( OC_UINT16, Engine, duration_phase_3 ),

	OCL_NULL( 1 ), // lifelength
	OCL_SVAR(  OC_UINT8, Engine, flags ),
	OCL_NULL( 1 ), // preview_company_rank
	OCL_SVAR(  OC_UINT8, Engine, preview_wait ),

	OCL_CNULL( OC_TTD, 2 ), ///< railtype + junk

	OCL_END()
};

static bool LoadOldEngine(LoadgameState *ls, int num)
{
	Engine *e = _savegame_type == SGT_TTO ? &_old_engines[num] : GetTempDataEngine(num);
	return LoadChunk(ls, e, engine_chunk);
}

static bool LoadOldEngineName(LoadgameState *ls, int num)
{
	Engine *e = GetTempDataEngine(num);
	e->name = CopyFromOldName(RemapOldStringID(ReadUint16(ls)));
	return true;
}

static const OldChunks subsidy_chunk[] = {
	OCL_SVAR(  OC_UINT8, Subsidy, cargo_type ),
	OCL_SVAR(  OC_UINT8, Subsidy, remaining ),
	OCL_SVAR(  OC_FILE_U8 | OC_VAR_U16, Subsidy, src ),
	OCL_SVAR(  OC_FILE_U8 | OC_VAR_U16, Subsidy, dst ),

	OCL_END()
};

static bool LoadOldSubsidy(LoadgameState *ls, int num)
{
	Subsidy *s = new (num) Subsidy();
	bool ret = LoadChunk(ls, s, subsidy_chunk);
	if (s->cargo_type == CT_INVALID) delete s;
	return ret;
}

static const OldChunks game_difficulty_chunk[] = {
	OCL_SVAR( OC_FILE_U16 |  OC_VAR_U8, DifficultySettings, max_no_competitors ),
	OCL_NULL( 2), // competitor_start_time
	OCL_SVAR( OC_FILE_U16 |  OC_VAR_U8, DifficultySettings, number_towns ),
	OCL_SVAR( OC_FILE_U16 |  OC_VAR_U8, DifficultySettings, industry_density ),
	OCL_SVAR( OC_FILE_U16 | OC_VAR_U32, DifficultySettings, max_loan ),
	OCL_SVAR( OC_FILE_U16 |  OC_VAR_U8, DifficultySettings, initial_interest ),
	OCL_SVAR( OC_FILE_U16 |  OC_VAR_U8, DifficultySettings, vehicle_costs ),
	OCL_SVAR( OC_FILE_U16 |  OC_VAR_U8, DifficultySettings, competitor_speed ),
	OCL_NULL( 2), // competitor_intelligence
	OCL_SVAR( OC_FILE_U16 |  OC_VAR_U8, DifficultySettings, vehicle_breakdowns ),
	OCL_SVAR( OC_FILE_U16 |  OC_VAR_U8, DifficultySettings, subsidy_multiplier ),
	OCL_SVAR( OC_FILE_U16 |  OC_VAR_U8, DifficultySettings, construction_cost ),
	OCL_SVAR( OC_FILE_U16 |  OC_VAR_U8, DifficultySettings, terrain_type ),
	OCL_SVAR( OC_FILE_U16 |  OC_VAR_U8, DifficultySettings, quantity_sea_lakes ),
	OCL_SVAR( OC_FILE_U16 |  OC_VAR_U8, DifficultySettings, economy ),
	OCL_SVAR( OC_FILE_U16 |  OC_VAR_U8, DifficultySettings, line_reverse_mode ),
	OCL_SVAR( OC_FILE_U16 |  OC_VAR_U8, DifficultySettings, disasters ),
	OCL_END()
};

static bool LoadOldGameDifficulty(LoadgameState *ls, int num)
{
	bool ret = LoadChunk(ls, &_settings_game.difficulty, game_difficulty_chunk);
	_settings_game.difficulty.max_loan *= 1000;
	return ret;
}


static bool LoadOldMapPart1(LoadgameState *ls, int num)
{
	if (_savegame_type == SGT_TTO) {
		MemSetT(_m, 0, OLD_MAP_SIZE);
		MemSetT(_me, 0, OLD_MAP_SIZE);
	}

	for (uint i = 0; i < OLD_MAP_SIZE; i++) {
		_m[i].m1 = ReadByte(ls);
	}
	for (uint i = 0; i < OLD_MAP_SIZE; i++) {
		_m[i].m2 = ReadByte(ls);
	}

	if (_savegame_type != SGT_TTO) {
		for (uint i = 0; i < OLD_MAP_SIZE; i++) {
			_old_map3[i * 2] = ReadByte(ls);
			_old_map3[i * 2 + 1] = ReadByte(ls);
		}
		for (uint i = 0; i < OLD_MAP_SIZE / 4; i++) {
			byte b = ReadByte(ls);
			_me[i * 4 + 0].m6 = GB(b, 0, 2);
			_me[i * 4 + 1].m6 = GB(b, 2, 2);
			_me[i * 4 + 2].m6 = GB(b, 4, 2);
			_me[i * 4 + 3].m6 = GB(b, 6, 2);
		}
	}

	return true;
}

static bool LoadOldMapPart2(LoadgameState *ls, int num)
{
	uint i;

	for (i = 0; i < OLD_MAP_SIZE; i++) {
		_m[i].type = ReadByte(ls);
	}
	for (i = 0; i < OLD_MAP_SIZE; i++) {
		_m[i].m5 = ReadByte(ls);
	}

	return true;
}

static bool LoadTTDPatchExtraChunks(LoadgameState *ls, int num)
{
	ReadTTDPatchFlags();

	DEBUG(oldloader, 2, "Found %d extra chunk(s)", _old_extra_chunk_nums);

	for (int i = 0; i != _old_extra_chunk_nums; i++) {
		uint16 id = ReadUint16(ls);
		uint32 len = ReadUint32(ls);

		switch (id) {
			/* List of GRFIDs, used in the savegame. 0x8004 is the new ID
			 * They are saved in a 'GRFID:4 active:1' format, 5 bytes for each entry */
			case 0x2:
			case 0x8004: {
				/* Skip the first element: TTDP hack for the Action D special variables (FFFF0000 01) */
				ReadUint32(ls); ReadByte(ls); len -= 5;

				ClearGRFConfigList(&_grfconfig);
				while (len != 0) {
					uint32 grfid = ReadUint32(ls);

					if (ReadByte(ls) == 1) {
						GRFConfig *c = new GRFConfig("TTDP game, no information");
						c->ident.grfid = grfid;

						AppendToGRFConfigList(&_grfconfig, c);
						DEBUG(oldloader, 3, "TTDPatch game using GRF file with GRFID %0X", BSWAP32(c->ident.grfid));
					}
					len -= 5;
				}

				/* Append static NewGRF configuration */
				AppendStaticGRFConfigs(&_grfconfig);
				break;
			}

			/* TTDPatch version and configuration */
			case 0x3:
				_ttdp_version = ReadUint32(ls);
				DEBUG(oldloader, 3, "Game saved with TTDPatch version %d.%d.%d r%d",
					GB(_ttdp_version, 24, 8), GB(_ttdp_version, 20, 4), GB(_ttdp_version, 16, 4), GB(_ttdp_version, 0, 16));
				len -= 4;
				while (len-- != 0) ReadByte(ls); // skip the configuration
				break;

			default:
				DEBUG(oldloader, 4, "Skipping unknown extra chunk %X", id);
				while (len-- != 0) ReadByte(ls);
				break;
		}
	}

	return true;
}

extern TileIndex _cur_tileloop_tile;
extern uint16 _disaster_delay;
extern byte _trees_tick_ctr;
extern byte _age_cargo_skip_counter; // From misc_sl.cpp
extern uint8 _old_diff_level;
extern uint8 _old_units;
static const OldChunks main_chunk[] = {
	OCL_ASSERT( OC_TTD, 0 ),
	OCL_ASSERT( OC_TTO, 0 ),
	OCL_VAR ( OC_FILE_U16 | OC_VAR_U32, 1, &_date ),
	OCL_VAR ( OC_UINT16,   1, &_date_fract ),
	OCL_NULL( 600 ),            ///< TextEffects
	OCL_VAR ( OC_UINT32,   2, &_random.state ),

	OCL_ASSERT( OC_TTD, 0x264 ),
	OCL_ASSERT( OC_TTO, 0x264 ),

	OCL_CCHUNK( OC_TTD, 70, LoadOldTown ),
	OCL_CCHUNK( OC_TTO, 80, LoadOldTown ),

	OCL_ASSERT( OC_TTD, 0x1C18 ),
	OCL_ASSERT( OC_TTO, 0x1AC4 ),

	OCL_CCHUNK( OC_TTD, 5000, LoadOldOrder ),
	OCL_CCHUNK( OC_TTO, 3000, LoadOldOrder ),

	OCL_ASSERT( OC_TTD, 0x4328 ),
	OCL_ASSERT( OC_TTO, 0x3234 ),

	OCL_CHUNK( 1, LoadOldAnimTileList ),
	OCL_NULL( 4 ),              ///< old end-of-order-list-pointer, no longer in use

	OCL_ASSERT( OC_TTO, 0x3438 ),

	OCL_CCHUNK( OC_TTD, 255, LoadOldDepot ),
	OCL_CCHUNK( OC_TTO, 252, LoadOldDepot ),

	OCL_ASSERT( OC_TTD, 0x4B26 ),
	OCL_ASSERT( OC_TTO, 0x3A20 ),

	OCL_NULL( 4 ),              ///< town counter,  no longer in use
	OCL_NULL( 2 ),              ///< timer_counter, no longer in use
	OCL_NULL( 2 ),              ///< land_code,     no longer in use

	OCL_VAR ( OC_FILE_U16 | OC_VAR_U8, 1, &_age_cargo_skip_counter ),
	OCL_VAR ( OC_UINT16,   1, &_tick_counter ),
	OCL_VAR (   OC_TILE,   1, &_cur_tileloop_tile ),

	OCL_ASSERT( OC_TTO, 0x3A2E ),

	OCL_CNULL( OC_TTO, 48 * 6 ), ///< prices
	OCL_CNULL( OC_TTD, 49 * 6 ), ///< prices

	OCL_ASSERT( OC_TTO, 0x3B4E ),

	OCL_CNULL( OC_TTO, 11 * 8 ), ///< cargo payment rates
	OCL_CNULL( OC_TTD, 12 * 8 ), ///< cargo payment rates

	OCL_ASSERT( OC_TTD, 0x4CBA ),
	OCL_ASSERT( OC_TTO, 0x3BA6 ),

	OCL_CHUNK( 1, LoadOldMapPart1 ),

	OCL_ASSERT( OC_TTD, 0x48CBA ),
	OCL_ASSERT( OC_TTO, 0x23BA6 ),

	OCL_CCHUNK( OC_TTD, 250, LoadOldStation ),
	OCL_CCHUNK( OC_TTO, 200, LoadOldStation ),

	OCL_ASSERT( OC_TTO, 0x29E16 ),

	OCL_CCHUNK( OC_TTD, 90, LoadOldIndustry ),
	OCL_CCHUNK( OC_TTO, 100, LoadOldIndustry ),

	OCL_ASSERT( OC_TTO, 0x2ADB6 ),

	OCL_CHUNK(  8, LoadOldCompany ),

	OCL_ASSERT( OC_TTD, 0x547F2 ),
	OCL_ASSERT( OC_TTO, 0x2C746 ),

	OCL_CCHUNK( OC_TTD, 850, LoadOldVehicle ),
	OCL_CCHUNK( OC_TTO, 800, LoadOldVehicle ),

	OCL_ASSERT( OC_TTD, 0x6F0F2 ),
	OCL_ASSERT( OC_TTO, 0x45746 ),

	OCL_VAR ( OC_TTD | OC_UINT8 | OC_DEREFERENCE_POINTER, 32 * 500, &_old_name_array ),
	OCL_VAR ( OC_TTO | OC_UINT8 | OC_DEREFERENCE_POINTER, 24 * 200, &_old_name_array ),

	OCL_ASSERT( OC_TTO, 0x46A06 ),

	OCL_NULL( 0x2000 ),            ///< Old hash-table, no longer in use

	OCL_CHUNK( 40, LoadOldSign ),

	OCL_ASSERT( OC_TTO, 0x48C36 ),

	OCL_CCHUNK( OC_TTD, 256, LoadOldEngine ),
	OCL_CCHUNK( OC_TTO, 103, LoadOldEngine ),

	OCL_ASSERT( OC_TTO, 0x496AC ),

	OCL_NULL ( 2 ), // _vehicle_id_ctr_day

	OCL_CHUNK(  8, LoadOldSubsidy ),

	OCL_ASSERT( OC_TTO, 0x496CE ),

	OCL_VAR ( OC_FILE_U16 | OC_VAR_U32,   1, &_next_competitor_start ),

	OCL_CNULL( OC_TTO, 2 ),  ///< available monorail bitmask

	OCL_VAR ( OC_FILE_I16 | OC_VAR_I32,   1, &_saved_scrollpos_x ),
	OCL_VAR ( OC_FILE_I16 | OC_VAR_I32,   1, &_saved_scrollpos_y ),
	OCL_VAR ( OC_FILE_U16 | OC_VAR_U8,    1, &_saved_scrollpos_zoom ),

	OCL_NULL( 4 ),           ///< max_loan
	OCL_VAR ( OC_FILE_U32 | OC_VAR_I64,   1, &_economy.old_max_loan_unround ),
	OCL_VAR (  OC_INT16,    1, &_economy.fluct ),

	OCL_VAR ( OC_UINT16,    1, &_disaster_delay ),

	OCL_ASSERT( OC_TTO, 0x496E4 ),

	OCL_CNULL( OC_TTD, 144 ),             ///< cargo-stuff

	OCL_CCHUNK( OC_TTD, 256, LoadOldEngineName ),

	OCL_CNULL( OC_TTD, 144 ),             ///< AI cargo-stuff
	OCL_NULL( 2 ),               ///< Company indexes of companies, no longer in use
	OCL_NULL( 1 ),               ///< Station tick counter, no longer in use

	OCL_VAR (  OC_UINT8,    1, &_settings_game.locale.currency ),
	OCL_VAR (  OC_UINT8,    1, &_old_units ),
	OCL_VAR ( OC_FILE_U8 | OC_VAR_U32,    1, &_cur_company_tick_index ),

	OCL_NULL( 2 ),               ///< Date stuff, calculated automatically
	OCL_NULL( 8 ),               ///< Company colours, calculated automatically

	OCL_VAR (  OC_UINT8,    1, &_economy.infl_amount ),
	OCL_VAR (  OC_UINT8,    1, &_economy.infl_amount_pr ),
	OCL_VAR (  OC_UINT8,    1, &_economy.interest_rate ),
	OCL_NULL( 1 ), // available airports
	OCL_VAR (  OC_UINT8,    1, &_settings_game.vehicle.road_side ),
	OCL_VAR (  OC_UINT8,    1, &_settings_game.game_creation.town_name ),

	OCL_CHUNK( 1, LoadOldGameDifficulty ),

	OCL_ASSERT( OC_TTD, 0x77130 ),

	OCL_VAR (  OC_UINT8,    1, &_old_diff_level ),

	OCL_VAR ( OC_TTD | OC_UINT8,    1, &_settings_game.game_creation.landscape ),
	OCL_VAR ( OC_TTD | OC_UINT8,    1, &_trees_tick_ctr ),

	OCL_CNULL( OC_TTD, 1 ),               ///< Custom vehicle types yes/no, no longer used
	OCL_VAR ( OC_TTD | OC_UINT8,    1, &_settings_game.game_creation.snow_line_height ),

	OCL_CNULL( OC_TTD, 32 ),              ///< new_industry_randtable, no longer used (because of new design)
	OCL_CNULL( OC_TTD, 36 ),              ///< cargo-stuff

	OCL_ASSERT( OC_TTD, 0x77179 ),
	OCL_ASSERT( OC_TTO, 0x4971D ),

	OCL_CHUNK( 1, LoadOldMapPart2 ),

	OCL_ASSERT( OC_TTD, 0x97179 ),
	OCL_ASSERT( OC_TTO, 0x6971D ),

	/* Below any (if available) extra chunks from TTDPatch can follow */
	OCL_CHUNK(1, LoadTTDPatchExtraChunks),

	OCL_END()
};

bool LoadTTDMain(LoadgameState *ls)
{
	DEBUG(oldloader, 3, "Reading main chunk...");

	_read_ttdpatch_flags = false;

	/* Load the biggest chunk */
	std::array<byte, OLD_MAP_SIZE * 2> map3;
	_old_map3 = map3.data();
	_old_vehicle_names = nullptr;
	try {
		if (!LoadChunk(ls, nullptr, main_chunk)) {
			DEBUG(oldloader, 0, "Loading failed");
			free(_old_vehicle_names);
			return false;
		}
	} catch (...) {
		free(_old_vehicle_names);
		throw;
	}

	DEBUG(oldloader, 3, "Done, converting game data...");

	FixTTDMapArray();
	FixTTDDepots();

	/* Fix some general stuff */
	_settings_game.game_creation.landscape = _settings_game.game_creation.landscape & 0xF;

	/* Fix the game to be compatible with OpenTTD */
	FixOldTowns();
	FixOldVehicles();

	/* We have a new difficulty setting */
	_settings_game.difficulty.town_council_tolerance = Clamp(_old_diff_level, 0, 2);

	DEBUG(oldloader, 3, "Finished converting game data");
	DEBUG(oldloader, 1, "TTD(Patch) savegame successfully converted");

	free(_old_vehicle_names);

	return true;
}

bool LoadTTOMain(LoadgameState *ls)
{
	DEBUG(oldloader, 3, "Reading main chunk...");

	_read_ttdpatch_flags = false;

	std::array<byte, 103 * sizeof(Engine)> engines; // we don't want to call Engine constructor here
	_old_engines = (Engine *)engines.data();
	std::array<StringID, 800> vehnames;
	_old_vehicle_names = vehnames.data();

	/* Load the biggest chunk */
	if (!LoadChunk(ls, nullptr, main_chunk)) {
		DEBUG(oldloader, 0, "Loading failed");
		return false;
	}
	DEBUG(oldloader, 3, "Done, converting game data...");

	if (_settings_game.game_creation.town_name != 0) _settings_game.game_creation.town_name++;

	_settings_game.game_creation.landscape = 0;
	_trees_tick_ctr = 0xFF;

	if (!FixTTOMapArray() || !FixTTOEngines()) {
		DEBUG(oldloader, 0, "Conversion failed");
		return false;
	}

	FixOldTowns();
	FixOldVehicles();
	FixTTOCompanies();

	/* We have a new difficulty setting */
	_settings_game.difficulty.town_council_tolerance = Clamp(_old_diff_level, 0, 2);

	/* SVXConverter about cargo payment rates correction:
	 * "increase them to compensate for the faster time advance in TTD compared to TTO
	 * which otherwise would cause much less income while the annual running costs of
	 * the vehicles stay the same" */
	_economy.inflation_payment = std::min(_economy.inflation_payment * 124 / 74, MAX_INFLATION);

	DEBUG(oldloader, 3, "Finished converting game data");
	DEBUG(oldloader, 1, "TTO savegame successfully converted");

	return true;
}
