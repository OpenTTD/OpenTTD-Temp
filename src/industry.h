/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file industry.h Base of all industries. */

#ifndef INDUSTRY_H
#define INDUSTRY_H

#include "newgrf_storage.h"
#include "subsidy_type.h"
#include "industry_map.h"
#include "industrytype.h"
#include "tilearea_type.h"
#include "station_base.h"


typedef Pool<Industry, IndustryID, 64, 64000> IndustryPool;
extern IndustryPool _industry_pool;

/**
 * Production level maximum, minimum and default values.
 * It is not a value been really used in order to change, but rather an indicator
 * of how the industry is behaving.
 */
enum ProductionLevels {
	PRODLEVEL_CLOSURE = 0x00,  ///< signal set to actually close the industry
	PRODLEVEL_MINIMUM = 0x04,  ///< below this level, the industry is set to be closing
	PRODLEVEL_DEFAULT = 0x10,  ///< default level set when the industry is created
	PRODLEVEL_MAXIMUM = 0x80,  ///< the industry is running at full speed
};

enum class IndustryAction : byte {
	SetControlFlags = 0,       ///< Set IndustryControlFlags
	SetExclusiveSupplier = 1,  ///< Set exclusive supplier
	SetExclusiveConsumer = 2,  ///< Set exclusive consumer
	SetText = 3,               ///< Set additional text
};

/**
 * Flags to control/override the behaviour of an industry.
 * These flags are controlled by game scripts.
 */
enum IndustryControlFlags : byte {
	/** No flags in effect */
	INDCTL_NONE                   = 0,
	/** When industry production change is evaluated, rolls to decrease are ignored. */
	INDCTL_NO_PRODUCTION_DECREASE = 1 << 0,
	/** When industry production change is evaluated, rolls to increase are ignored. */
	INDCTL_NO_PRODUCTION_INCREASE = 1 << 1,
	/**
	 * Industry can not close regardless of production level or time since last delivery.
	 * This does not prevent a closure already announced. */
	INDCTL_NO_CLOSURE             = 1 << 2,
	/** Mask of all flags set */
	INDCTL_MASK = INDCTL_NO_PRODUCTION_DECREASE | INDCTL_NO_PRODUCTION_INCREASE | INDCTL_NO_CLOSURE,
};
DECLARE_ENUM_AS_BIT_SET(IndustryControlFlags);

/**
 * Defines the internal data of a functional industry.
 */
struct Industry : IndustryPool::PoolItem<&_industry_pool> {
	TileArea location;                                     ///< Location of the industry
	Town *town;                                            ///< Nearest town
	Station *neutral_station;                              ///< Associated neutral station
	CargoID produced_cargo[INDUSTRY_NUM_OUTPUTS];          ///< 16 production cargo slots
	uint16 produced_cargo_waiting[INDUSTRY_NUM_OUTPUTS];   ///< amount of cargo produced per cargo
	uint16 incoming_cargo_waiting[INDUSTRY_NUM_INPUTS];    ///< incoming cargo waiting to be processed
	byte production_rate[INDUSTRY_NUM_OUTPUTS];            ///< production rate for each cargo
	byte prod_level;                                       ///< general production level
	CargoID accepts_cargo[INDUSTRY_NUM_INPUTS];            ///< 16 input cargo slots
	uint16 this_month_production[INDUSTRY_NUM_OUTPUTS];    ///< stats of this month's production per cargo
	uint16 this_month_transported[INDUSTRY_NUM_OUTPUTS];   ///< stats of this month's transport per cargo
	byte past_pct_transported[24][INDUSTRY_NUM_OUTPUTS];   ///< percentage transported per cargo in the last 24 months
	uint16 past_production[24][INDUSTRY_NUM_OUTPUTS];      ///< total units produced per cargo in the last 24 months
	uint16 past_transported[24][INDUSTRY_NUM_OUTPUTS];     ///< total units transported per cargo in the last 24 months
	uint16 counter;                                        ///< used for animation and/or production (if available cargo)

	IndustryType type;             ///< type of industry.
	Owner owner;                   ///< owner of the industry.  Which SHOULD always be (imho) OWNER_NONE
	byte random_colour;            ///< randomized colour of the industry, for display purpose
	Year last_prod_year;           ///< last year of production
	byte was_cargo_delivered;      ///< flag that indicate this has been the closest industry chosen for cargo delivery by a station. see DeliverGoodsToIndustry
	IndustryControlFlags ctlflags; ///< flags overriding standard behaviours

	PartOfSubsidy part_of_subsidy; ///< NOSAVE: is this industry a source/destination of a subsidy?
	StationList stations_near;     ///< NOSAVE: List of nearby stations.
	mutable std::string cached_name; ///< NOSAVE: Cache of the resolved name of the industry

	Owner founder;                 ///< Founder of the industry
	Date construction_date;        ///< Date of the construction of the industry
	uint8 construction_type;       ///< Way the industry was constructed (@see IndustryConstructionType)
	Date last_cargo_accepted_at[INDUSTRY_NUM_INPUTS]; ///< Last day each cargo type was accepted by this industry
	byte selected_layout;          ///< Which tile layout was used when creating the industry
	Owner exclusive_supplier;      ///< Which company has exclusive rights to deliver cargo (INVALID_OWNER = anyone)
	Owner exclusive_consumer;      ///< Which company has exclusive rights to take cargo (INVALID_OWNER = anyone)
	std::string text;              ///< General text with additional information.

	uint16 random;                 ///< Random value used for randomisation of all kinds of things

	PersistentStorage *psa;        ///< Persistent storage for NewGRF industries.

	Industry(TileIndex tile = INVALID_TILE) : location(tile, 0, 0) {}
	~Industry();

	void RecomputeProductionMultipliers();

	/**
	 * Check if a given tile belongs to this industry.
	 * @param tile The tile to check.
	 * @return True if the tile is part of this industry.
	 */
	inline bool TileBelongsToIndustry(TileIndex tile) const
	{
		return IsTileType(tile, MP_INDUSTRY) && GetIndustryIndex(tile) == this->index;
	}

	inline int GetCargoProducedIndex(CargoID cargo) const
	{
		if (cargo == CT_INVALID) return -1;
		const CargoID *pos = std::find(this->produced_cargo, endof(this->produced_cargo), cargo);
		if (pos == endof(this->produced_cargo)) return -1;
		return pos - this->produced_cargo;
	}

	inline int GetCargoAcceptedIndex(CargoID cargo) const
	{
		if (cargo == CT_INVALID) return -1;
		const CargoID *pos = std::find(this->accepts_cargo, endof(this->accepts_cargo), cargo);
		if (pos == endof(this->accepts_cargo)) return -1;
		return pos - this->accepts_cargo;
	}

	/**
	 * Get the industry of the given tile
	 * @param tile the tile to get the industry from
	 * @pre IsTileType(t, MP_INDUSTRY)
	 * @return the industry
	 */
	static inline Industry *GetByTile(TileIndex tile)
	{
		return Industry::Get(GetIndustryIndex(tile));
	}

	static Industry *GetRandom();
	static void PostDestructor(size_t index);

	/**
	 * Increment the count of industries for this type.
	 * @param type IndustryType to increment
	 * @pre type < NUM_INDUSTRYTYPES
	 */
	static inline void IncIndustryTypeCount(IndustryType type)
	{
		assert(type < NUM_INDUSTRYTYPES);
		counts[type]++;
	}

	/**
	 * Decrement the count of industries for this type.
	 * @param type IndustryType to decrement
	 * @pre type < NUM_INDUSTRYTYPES
	 */
	static inline void DecIndustryTypeCount(IndustryType type)
	{
		assert(type < NUM_INDUSTRYTYPES);
		counts[type]--;
	}

	/**
	 * Get the count of industries for this type.
	 * @param type IndustryType to query
	 * @pre type < NUM_INDUSTRYTYPES
	 */
	static inline uint16 GetIndustryTypeCount(IndustryType type)
	{
		assert(type < NUM_INDUSTRYTYPES);
		return counts[type];
	}

	/** Resets industry counts. */
	static inline void ResetIndustryCounts()
	{
		memset(&counts, 0, sizeof(counts));
	}

	inline const char *GetCachedName() const
	{
		if (this->cached_name.empty()) this->FillCachedName();
		return this->cached_name.c_str();
	}

private:
	void FillCachedName() const;

protected:
	static uint16 counts[NUM_INDUSTRYTYPES]; ///< Number of industries per type ingame
};

void ClearAllIndustryCachedNames();

void PlantRandomFarmField(const Industry *i);

void ReleaseDisastersTargetingIndustry(IndustryID);

bool IsTileForestIndustry(TileIndex tile);

/** Data for managing the number of industries of a single industry type. */
struct IndustryTypeBuildData {
	uint32 probability;  ///< Relative probability of building this industry.
	byte   min_number;   ///< Smallest number of industries that should exist (either \c 0 or \c 1).
	uint16 target_count; ///< Desired number of industries of this type.
	uint16 max_wait;     ///< Starting number of turns to wait (copied to #wait_count).
	uint16 wait_count;   ///< Number of turns to wait before trying to build again.

	void Reset();

	bool GetIndustryTypeData(IndustryType it);
};

/**
 * Data for managing the number and type of industries in the game.
 */
struct IndustryBuildData {
	IndustryTypeBuildData builddata[NUM_INDUSTRYTYPES]; ///< Industry build data for every industry type.
	uint32 wanted_inds; ///< Number of wanted industries (bits 31-16), and a fraction (bits 15-0).

	void Reset();

	void SetupTargetCount();
	void TryBuildNewIndustry();

	void MonthlyLoop();
};

extern IndustryBuildData _industry_builder;


/** Special values for the industry list window for the data parameter of #InvalidateWindowData. */
enum IndustryDirectoryInvalidateWindowData {
	IDIWD_FORCE_REBUILD,
	IDIWD_PRODUCTION_CHANGE,
	IDIWD_FORCE_RESORT,
};

#endif /* INDUSTRY_H */
