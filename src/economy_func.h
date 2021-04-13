/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file economy_func.h Functions related to the economy. */

#ifndef ECONOMY_FUNC_H
#define ECONOMY_FUNC_H

#include "economy_type.h"
#include "station_type.h"
#include "cargo_type.h"
#include "vehicle_type.h"
#include "company_type.h"

void ResetPriceBaseMultipliers();
void SetPriceBaseMultiplier(Price price, int factor);

extern const ScoreInfo _score_info[];
extern int64 _score_part[MAX_COMPANIES][SCORE_END];
extern Economy _economy;
/* Prices and also the fractional part. */
extern Prices _price;

int UpdateCompanyRatingAndValue(Company *c, bool update);
void StartupIndustryDailyChanges(bool init_counter);

Money GetTransportedGoodsIncome(uint num_pieces, uint dist, uint16 transit_days, CargoID cargo_type);
uint MoveGoodsToStation(CargoID type, uint amount, SourceType source_type, SourceID source_id, const StationList *all_stations, Owner exclusivity = INVALID_OWNER);

void PrepareUnload(Vehicle *front_v);
void LoadUnloadStation(Station *st);

Money GetPrice(Price index, uint cost_factor, const struct GRFFile *grf_file, int shift = 0);

void InitializeEconomy();
void RecomputePrices();
bool AddInflation(bool check_year = true);

/**
 * Is the economy in recession?
 * @return \c True if economy is in recession, \c false otherwise.
 */
static inline bool EconomyIsInRecession()
{
	return _economy.fluct <= 0;
}

#endif /* ECONOMY_FUNC_H */
