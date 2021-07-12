/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file economy_type.h Types related to the economy. */

#ifndef ECONOMY_TYPE_H
#define ECONOMY_TYPE_H

#include "core/overflowsafe_type.hpp"
#include "core/enum_type.hpp"

typedef OverflowSafeInt64 Money;

/** Type of the game economy. */
enum EconomyType : uint8 {
	ET_BEGIN = 0,
	ET_ORIGINAL = 0,
	ET_SMOOTH = 1,
	ET_FROZEN = 2,
	ET_END = 3,
};

/** Data of the economy. */
struct Economy {
	Money max_loan;                       ///< NOSAVE: Maximum possible loan
	int16 fluct;                          ///< Economy fluctuation status
	byte interest_rate;                   ///< Interest
	byte infl_amount;                     ///< inflation amount
	byte infl_amount_pr;                  ///< inflation rate for payment rates
	uint32 industry_daily_change_counter; ///< Bits 31-16 are number of industry to be performed, 15-0 are fractional collected daily
	uint32 industry_daily_increment;      ///< The value which will increment industry_daily_change_counter. Computed value. NOSAVE
	uint64 inflation_prices;              ///< Cumulated inflation of prices since game start; 16 bit fractional part
	uint64 inflation_payment;             ///< Cumulated inflation of cargo paypent since game start; 16 bit fractional part

	/* Old stuff for savegame conversion only */
	Money old_max_loan_unround;           ///< Old: Unrounded max loan
	uint16 old_max_loan_unround_fract;    ///< Old: Fraction of the unrounded max loan
};

/** Score categories in the detailed performance rating. */
enum ScoreID {
	SCORE_BEGIN      = 0,
	SCORE_VEHICLES   = 0,
	SCORE_STATIONS   = 1,
	SCORE_MIN_PROFIT = 2,
	SCORE_MIN_INCOME = 3,
	SCORE_MAX_INCOME = 4,
	SCORE_DELIVERED  = 5,
	SCORE_CARGO      = 6,
	SCORE_MONEY      = 7,
	SCORE_LOAN       = 8,
	SCORE_TOTAL      = 9,  ///< This must always be the last entry
	SCORE_END        = 10, ///< How many scores are there..

	SCORE_MAX = 1000,      ///< The max score that can be in the performance history
	/* the scores together of score_info is allowed to be more! */
};
DECLARE_POSTFIX_INCREMENT(ScoreID)

/** Data structure for storing how the score is computed for a single score id. */
struct ScoreInfo {
	int needed; ///< How much you need to get the perfect score
	int score;  ///< How much score it will give
};

/**
 * Enumeration of all base prices for use with #Prices.
 * The prices are ordered as they are expected by NewGRF cost multipliers, so don't shuffle them.
 */
enum Price {
	PR_BEGIN = 0,
	PR_STATION_VALUE = 0,
	PR_BUILD_RAIL,
	PR_BUILD_ROAD,
	PR_BUILD_SIGNALS,
	PR_BUILD_BRIDGE,
	PR_BUILD_DEPOT_TRAIN,
	PR_BUILD_DEPOT_ROAD,
	PR_BUILD_DEPOT_SHIP,
	PR_BUILD_TUNNEL,
	PR_BUILD_STATION_RAIL,
	PR_BUILD_STATION_RAIL_LENGTH,
	PR_BUILD_STATION_AIRPORT,
	PR_BUILD_STATION_BUS,
	PR_BUILD_STATION_TRUCK,
	PR_BUILD_STATION_DOCK,
	PR_BUILD_VEHICLE_TRAIN,
	PR_BUILD_VEHICLE_WAGON,
	PR_BUILD_VEHICLE_AIRCRAFT,
	PR_BUILD_VEHICLE_ROAD,
	PR_BUILD_VEHICLE_SHIP,
	PR_BUILD_TREES,
	PR_TERRAFORM,
	PR_CLEAR_GRASS,
	PR_CLEAR_ROUGH,
	PR_CLEAR_ROCKS,
	PR_CLEAR_FIELDS,
	PR_CLEAR_TREES,
	PR_CLEAR_RAIL,
	PR_CLEAR_SIGNALS,
	PR_CLEAR_BRIDGE,
	PR_CLEAR_DEPOT_TRAIN,
	PR_CLEAR_DEPOT_ROAD,
	PR_CLEAR_DEPOT_SHIP,
	PR_CLEAR_TUNNEL,
	PR_CLEAR_WATER,
	PR_CLEAR_STATION_RAIL,
	PR_CLEAR_STATION_AIRPORT,
	PR_CLEAR_STATION_BUS,
	PR_CLEAR_STATION_TRUCK,
	PR_CLEAR_STATION_DOCK,
	PR_CLEAR_HOUSE,
	PR_CLEAR_ROAD,
	PR_RUNNING_TRAIN_STEAM,
	PR_RUNNING_TRAIN_DIESEL,
	PR_RUNNING_TRAIN_ELECTRIC,
	PR_RUNNING_AIRCRAFT,
	PR_RUNNING_ROADVEH,
	PR_RUNNING_SHIP,
	PR_BUILD_INDUSTRY,
	PR_CLEAR_INDUSTRY,
	PR_BUILD_OBJECT,
	PR_CLEAR_OBJECT,
	PR_BUILD_WAYPOINT_RAIL,
	PR_CLEAR_WAYPOINT_RAIL,
	PR_BUILD_WAYPOINT_BUOY,
	PR_CLEAR_WAYPOINT_BUOY,
	PR_TOWN_ACTION,
	PR_BUILD_FOUNDATION,
	PR_BUILD_INDUSTRY_RAW,
	PR_BUILD_TOWN,
	PR_BUILD_CANAL,
	PR_CLEAR_CANAL,
	PR_BUILD_AQUEDUCT,
	PR_CLEAR_AQUEDUCT,
	PR_BUILD_LOCK,
	PR_CLEAR_LOCK,
	PR_INFRASTRUCTURE_RAIL,
	PR_INFRASTRUCTURE_ROAD,
	PR_INFRASTRUCTURE_WATER,
	PR_INFRASTRUCTURE_STATION,
	PR_INFRASTRUCTURE_AIRPORT,

	PR_END,
	INVALID_PRICE = 0xFF
};
DECLARE_POSTFIX_INCREMENT(Price)

typedef Money Prices[PR_END]; ///< Prices of everything. @see Price
typedef int8 PriceMultipliers[PR_END];

/** Types of expenses. */
enum ExpensesType : byte {
	EXPENSES_CONSTRUCTION =  0,   ///< Construction costs.
	EXPENSES_NEW_VEHICLES,        ///< New vehicles.
	EXPENSES_TRAIN_RUN,           ///< Running costs trains.
	EXPENSES_ROADVEH_RUN,         ///< Running costs road vehicles.
	EXPENSES_AIRCRAFT_RUN,        ///< Running costs aircraft.
	EXPENSES_SHIP_RUN,            ///< Running costs ships.
	EXPENSES_PROPERTY,            ///< Property costs.
	EXPENSES_TRAIN_INC,           ///< Income from trains.
	EXPENSES_ROADVEH_INC,         ///< Income from road vehicles.
	EXPENSES_AIRCRAFT_INC,        ///< Income from aircraft.
	EXPENSES_SHIP_INC,            ///< Income from ships.
	EXPENSES_LOAN_INT,            ///< Interest payments over the loan.
	EXPENSES_OTHER,               ///< Other expenses.
	EXPENSES_END,                 ///< Number of expense types.
	INVALID_EXPENSES      = 0xFF, ///< Invalid expense type.
};

/** Define basic enum properties for ExpensesType */
template <> struct EnumPropsT<ExpensesType> : MakeEnumPropsT<ExpensesType, byte, EXPENSES_CONSTRUCTION, EXPENSES_END, INVALID_EXPENSES, 8> {};

/**
 * Categories of a price bases.
 */
enum PriceCategory {
	PCAT_NONE,         ///< Not affected by difficulty settings
	PCAT_RUNNING,      ///< Price is affected by "vehicle running cost" difficulty setting
	PCAT_CONSTRUCTION, ///< Price is affected by "construction cost" difficulty setting
};

/**
 * Describes properties of price bases.
 */
struct PriceBaseSpec {
	Money start_price;      ///< Default value at game start, before adding multipliers.
	PriceCategory category; ///< Price is affected by certain difficulty settings.
	uint grf_feature;       ///< GRF Feature that decides whether price multipliers apply locally or globally, #GSF_END if none.
	Price fallback_price;   ///< Fallback price multiplier for new prices but old grfs.
};

/** The "steps" in loan size, in British Pounds! */
static const int LOAN_INTERVAL = 10000;
/** The size of loan for a new company, in British Pounds! */
static const int64 INITIAL_LOAN = 100000;

/**
 * Maximum inflation (including fractional part) without causing overflows in int64 price computations.
 * This allows for 32 bit base prices (21 are currently needed).
 * Considering the sign bit and 16 fractional bits, there are 15 bits left.
 * 170 years of 4% inflation result in a inflation of about 822, so 10 bits are actually enough.
 * Note that NewGRF multipliers share the 16 fractional bits.
 * @see MAX_PRICE_MODIFIER
 */
static const uint64 MAX_INFLATION = (1ull << (63 - 32)) - 1;

/**
 * Maximum NewGRF price modifiers.
 * Increasing base prices by factor 65536 should be enough.
 * @see MAX_INFLATION
 */
static const int MIN_PRICE_MODIFIER = -8;
static const int MAX_PRICE_MODIFIER = 16;
static const int INVALID_PRICE_MODIFIER = MIN_PRICE_MODIFIER - 1;

/** Multiplier for how many regular track bits a tunnel/bridge counts. */
static const uint TUNNELBRIDGE_TRACKBIT_FACTOR = 4;
/** Multiplier for how many regular track bits a level crossing counts. */
static const uint LEVELCROSSING_TRACKBIT_FACTOR = 2;
/** Multiplier for how many regular track bits a bay stop counts. */
static const uint ROAD_STOP_TRACKBIT_FACTOR = 2;
/** Multiplier for how many regular tiles a lock counts. */
static const uint LOCK_DEPOT_TILE_FACTOR = 2;

struct CargoPayment;
typedef uint32 CargoPaymentID;

#endif /* ECONOMY_TYPE_H */
