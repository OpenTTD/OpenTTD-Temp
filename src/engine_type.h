/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file engine_type.h Types related to engines. */

#ifndef ENGINE_TYPE_H
#define ENGINE_TYPE_H

#include "economy_type.h"
#include "rail_type.h"
#include "road_type.h"
#include "cargo_type.h"
#include "date_type.h"
#include "sound_type.h"
#include "strings_type.h"

typedef uint16 EngineID; ///< Unique identification number of an engine.

struct Engine;

/** Available types of rail vehicles. */
enum RailVehicleTypes {
	RAILVEH_SINGLEHEAD,  ///< indicates a "standalone" locomotive
	RAILVEH_MULTIHEAD,   ///< indicates a combination of two locomotives
	RAILVEH_WAGON,       ///< simple wagon, not motorized
};

/** Type of rail engine. */
enum EngineClass {
	EC_STEAM,    ///< Steam rail engine.
	EC_DIESEL,   ///< Diesel rail engine.
	EC_ELECTRIC, ///< Electric rail engine.
	EC_MONORAIL, ///< Mono rail engine.
	EC_MAGLEV,   ///< Maglev engine.
};

/** Information about a rail vehicle. */
struct RailVehicleInfo {
	byte image_index;
	RailVehicleTypes railveh_type;
	byte cost_factor;               ///< Purchase cost factor;      For multiheaded engines the sum of both engine prices.
	RailType railtype;
	uint16 max_speed;               ///< Maximum speed (1 unit = 1/1.6 mph = 1 km-ish/h)
	uint16 power;                   ///< Power of engine (hp);      For multiheaded engines the sum of both engine powers.
	uint16 weight;                  ///< Weight of vehicle (tons);  For multiheaded engines the weight of each single engine.
	byte running_cost;              ///< Running cost of engine;    For multiheaded engines the sum of both running costs.
	Price running_cost_class;
	EngineClass engclass;           ///< Class of engine for this vehicle
	byte capacity;                  ///< Cargo capacity of vehicle; For multiheaded engines the capacity of each single engine.
	byte ai_passenger_only;         ///< Bit value to tell AI that this engine is for passenger use only
	uint16 pow_wag_power;           ///< Extra power applied to consist if wagon should be powered
	byte pow_wag_weight;            ///< Extra weight applied to consist if wagon should be powered
	byte visual_effect;             ///< Bitstuffed NewGRF visual effect data
	byte shorten_factor;            ///< length on main map for this type is 8 - shorten_factor
	byte tractive_effort;           ///< Tractive effort coefficient
	byte air_drag;                  ///< Coefficient of air drag
	byte user_def_data;             ///< Property 0x25: "User-defined bit mask" Used only for (very few) NewGRF vehicles
	int16 curve_speed_mod;          ///< Modifier to maximum speed in curves (fixed-point binary with 8 fractional bits)
};

/** Information about a ship vehicle. */
struct ShipVehicleInfo {
	byte image_index;
	byte cost_factor;
	uint16 max_speed;      ///< Maximum speed (1 unit = 1/3.2 mph = 0.5 km-ish/h)
	uint16 capacity;
	byte running_cost;
	SoundID sfx;
	bool old_refittable;   ///< Is ship refittable; only used during initialisation. Later use EngineInfo::refit_mask.
	byte visual_effect;    ///< Bitstuffed NewGRF visual effect data
	byte ocean_speed_frac; ///< Fraction of maximum speed for ocean tiles.
	byte canal_speed_frac; ///< Fraction of maximum speed for canal/river tiles.

	/** Apply ocean/canal speed fraction to a velocity */
	uint ApplyWaterClassSpeedFrac(uint raw_speed, bool is_ocean) const
	{
		/* speed_frac == 0 means no reduction while 0xFF means reduction to 1/256. */
		return raw_speed * (256 - (is_ocean ? this->ocean_speed_frac : this->canal_speed_frac)) / 256;
	}
};

/**
 * AircraftVehicleInfo subtypes, bitmask type.
 * If bit 0 is 0 then it is a helicopter, otherwise it is a plane
 * in which case bit 1 tells us whether it's a big(fast) plane or not.
 */
enum AircraftSubTypeBits {
	AIR_HELI = 0,
	AIR_CTOL = 1, ///< Conventional Take Off and Landing, i.e. planes
	AIR_FAST = 2
};

/** Information about a aircraft vehicle. */
struct AircraftVehicleInfo {
	byte image_index;
	byte cost_factor;
	byte running_cost;
	byte subtype;               ///< Type of aircraft. @see AircraftSubTypeBits
	SoundID sfx;
	byte acceleration;
	uint16 max_speed;           ///< Maximum speed (1 unit = 8 mph = 12.8 km-ish/h)
	byte mail_capacity;         ///< Mail capacity (bags).
	uint16 passenger_capacity;  ///< Passenger capacity (persons).
	uint16 max_range;           ///< Maximum range of this aircraft.
};

/** Information about a road vehicle. */
struct RoadVehicleInfo {
	byte image_index;
	byte cost_factor;
	byte running_cost;
	Price running_cost_class;
	SoundID sfx;
	uint16 max_speed;        ///< Maximum speed (1 unit = 1/3.2 mph = 0.5 km-ish/h)
	byte capacity;
	uint8 weight;            ///< Weight in 1/4t units
	uint8 power;             ///< Power in 10hp units
	uint8 tractive_effort;   ///< Coefficient of tractive effort
	uint8 air_drag;          ///< Coefficient of air drag
	byte visual_effect;      ///< Bitstuffed NewGRF visual effect data
	byte shorten_factor;     ///< length on main map for this type is 8 - shorten_factor
	RoadType roadtype;       ///< Road type
};

/**
 * Information about a vehicle
 *  @see table/engines.h
 */
struct EngineInfo {
	Date base_intro;    ///< Basic date of engine introduction (without random parts).
	Year lifelength;    ///< Lifetime of a single vehicle
	Year base_life;     ///< Basic duration of engine availability (without random parts). \c 0xFF means infinite life.
	byte decay_speed;
	byte load_amount;
	byte climates;      ///< Climates supported by the engine.
	CargoID cargo_type;
	CargoTypes refit_mask;
	byte refit_cost;
	byte misc_flags;    ///< Miscellaneous flags. @see EngineMiscFlags
	byte callback_mask; ///< Bitmask of vehicle callbacks that have to be called
	int8 retire_early;  ///< Number of years early to retire vehicle
	StringID string_id; ///< Default name of engine
	uint16 cargo_age_period; ///< Number of ticks before carried cargo is aged.
};

/**
 * EngineInfo.misc_flags is a bitmask, with the following values
 */
enum EngineMiscFlags {
	EF_RAIL_TILTS = 0, ///< Rail vehicle tilts in curves
	EF_ROAD_TRAM  = 0, ///< Road vehicle is a tram/light rail vehicle
	EF_USES_2CC   = 1, ///< Vehicle uses two company colours
	EF_RAIL_IS_MU = 2, ///< Rail vehicle is a multiple-unit (DMU/EMU)
	EF_RAIL_FLIPS = 3, ///< Rail vehicle can be flipped in the depot
	EF_AUTO_REFIT = 4, ///< Automatic refitting is allowed
	EF_NO_DEFAULT_CARGO_MULTIPLIER = 5, ///< Use the new capacity algorithm. The default cargotype of the vehicle does not affect capacity multipliers. CB 15 is also called in purchase list.
	EF_NO_BREAKDOWN_SMOKE          = 6, ///< Do not show black smoke during a breakdown.
	EF_SPRITE_STACK                = 7, ///< Draw vehicle by stacking multiple sprites.
};

/**
 * Engine.flags is a bitmask, with the following values.
 */
enum EngineFlags {
	ENGINE_AVAILABLE         = 1, ///< This vehicle is available to everyone.
	ENGINE_EXCLUSIVE_PREVIEW = 2, ///< This vehicle is in the exclusive preview stage, either being used or being offered to a company.
};

static const uint MAX_LENGTH_ENGINE_NAME_CHARS = 32; ///< The maximum length of an engine name in characters including '\0'

static const EngineID INVALID_ENGINE = 0xFFFF; ///< Constant denoting an invalid engine.

#endif /* ENGINE_TYPE_H */
