/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file economy.cpp Handling of the economy. */

#include "stdafx.h"
#include "company_func.h"
#include "command_func.h"
#include "industry.h"
#include "town.h"
#include "news_func.h"
#include "network/network.h"
#include "network/network_func.h"
#include "ai/ai.hpp"
#include "aircraft.h"
#include "train.h"
#include "newgrf_engine.h"
#include "engine_base.h"
#include "ground_vehicle.hpp"
#include "newgrf_cargo.h"
#include "newgrf_sound.h"
#include "newgrf_industrytiles.h"
#include "newgrf_station.h"
#include "newgrf_airporttiles.h"
#include "object.h"
#include "strings_func.h"
#include "date_func.h"
#include "vehicle_func.h"
#include "sound_func.h"
#include "autoreplace_func.h"
#include "company_gui.h"
#include "signs_base.h"
#include "subsidy_base.h"
#include "subsidy_func.h"
#include "station_base.h"
#include "waypoint_base.h"
#include "economy_base.h"
#include "core/pool_func.hpp"
#include "core/backup_type.hpp"
#include "cargo_type.h"
#include "water.h"
#include "game/game.hpp"
#include "cargomonitor.h"
#include "goal_base.h"
#include "story_base.h"
#include "linkgraph/refresh.h"

#include "table/strings.h"
#include "table/pricebase.h"

#include "safeguards.h"


/* Initialize the cargo payment-pool */
CargoPaymentPool _cargo_payment_pool("CargoPayment");
INSTANTIATE_POOL_METHODS(CargoPayment)

/**
 * Multiply two integer values and shift the results to right.
 *
 * This function multiplies two integer values. The result is
 * shifted by the amount of shift to right.
 *
 * @param a The first integer
 * @param b The second integer
 * @param shift The amount to shift the value to right.
 * @return The shifted result
 */
static inline int32 BigMulS(const int32 a, const int32 b, const uint8 shift)
{
	return (int32)((int64)a * (int64)b >> shift);
}

typedef std::vector<Industry *> SmallIndustryList;

/**
 * Score info, values used for computing the detailed performance rating.
 */
const ScoreInfo _score_info[] = {
	{     120, 100}, // SCORE_VEHICLES
	{      80, 100}, // SCORE_STATIONS
	{   10000, 100}, // SCORE_MIN_PROFIT
	{   50000,  50}, // SCORE_MIN_INCOME
	{  100000, 100}, // SCORE_MAX_INCOME
	{   40000, 400}, // SCORE_DELIVERED
	{       8,  50}, // SCORE_CARGO
	{10000000,  50}, // SCORE_MONEY
	{  250000,  50}, // SCORE_LOAN
	{       0,   0}  // SCORE_TOTAL
};

int64 _score_part[MAX_COMPANIES][SCORE_END];
Economy _economy;
Prices _price;
Money _additional_cash_required;
static PriceMultipliers _price_base_multiplier;

/**
 * Calculate the value of the company. That is the value of all
 * assets (vehicles, stations, etc) and money minus the loan,
 * except when including_loan is \c false which is useful when
 * we want to calculate the value for bankruptcy.
 * @param c              the company to get the value of.
 * @param including_loan include the loan in the company value.
 * @return the value of the company.
 */
Money CalculateCompanyValue(const Company *c, bool including_loan)
{
	Owner owner = c->index;

	uint num = 0;

	for (const Station *st : Station::Iterate()) {
		if (st->owner == owner) num += CountBits((byte)st->facilities);
	}

	Money value = num * _price[PR_STATION_VALUE] * 25;

	for (const Vehicle *v : Vehicle::Iterate()) {
		if (v->owner != owner) continue;

		if (v->type == VEH_TRAIN ||
				v->type == VEH_ROAD ||
				(v->type == VEH_AIRCRAFT && Aircraft::From(v)->IsNormalAircraft()) ||
				v->type == VEH_SHIP) {
			value += v->value * 3 >> 1;
		}
	}

	/* Add real money value */
	if (including_loan) value -= c->current_loan;
	value += c->money;

	return std::max<Money>(value, 1);
}

/**
 * if update is set to true, the economy is updated with this score
 *  (also the house is updated, should only be true in the on-tick event)
 * @param update the economy with calculated score
 * @param c company been evaluated
 * @return actual score of this company
 *
 */
int UpdateCompanyRatingAndValue(Company *c, bool update)
{
	Owner owner = c->index;
	int score = 0;

	memset(_score_part[owner], 0, sizeof(_score_part[owner]));

	/* Count vehicles */
	{
		Money min_profit = 0;
		bool min_profit_first = true;
		uint num = 0;

		for (const Vehicle *v : Vehicle::Iterate()) {
			if (v->owner != owner) continue;
			if (IsCompanyBuildableVehicleType(v->type) && v->IsPrimaryVehicle()) {
				if (v->profit_last_year > 0) num++; // For the vehicle score only count profitable vehicles
				if (v->age > 730) {
					/* Find the vehicle with the lowest amount of profit */
					if (min_profit_first || min_profit > v->profit_last_year) {
						min_profit = v->profit_last_year;
						min_profit_first = false;
					}
				}
			}
		}

		min_profit >>= 8; // remove the fract part

		_score_part[owner][SCORE_VEHICLES] = num;
		/* Don't allow negative min_profit to show */
		if (min_profit > 0) {
			_score_part[owner][SCORE_MIN_PROFIT] = min_profit;
		}
	}

	/* Count stations */
	{
		uint num = 0;
		for (const Station *st : Station::Iterate()) {
			/* Only count stations that are actually serviced */
			if (st->owner == owner && (st->time_since_load <= 20 || st->time_since_unload <= 20)) num += CountBits((byte)st->facilities);
		}
		_score_part[owner][SCORE_STATIONS] = num;
	}

	/* Generate statistics depending on recent income statistics */
	{
		int numec = std::min<uint>(c->num_valid_stat_ent, 12u);
		if (numec != 0) {
			const CompanyEconomyEntry *cee = c->old_economy;
			Money min_income = cee->income + cee->expenses;
			Money max_income = cee->income + cee->expenses;

			do {
				min_income = std::min(min_income, cee->income + cee->expenses);
				max_income = std::max(max_income, cee->income + cee->expenses);
			} while (++cee, --numec);

			if (min_income > 0) {
				_score_part[owner][SCORE_MIN_INCOME] = min_income;
			}

			_score_part[owner][SCORE_MAX_INCOME] = max_income;
		}
	}

	/* Generate score depending on amount of transported cargo */
	{
		int numec = std::min<uint>(c->num_valid_stat_ent, 4u);
		if (numec != 0) {
			const CompanyEconomyEntry *cee = c->old_economy;
			OverflowSafeInt64 total_delivered = 0;
			do {
				total_delivered += cee->delivered_cargo.GetSum<OverflowSafeInt64>();
			} while (++cee, --numec);

			_score_part[owner][SCORE_DELIVERED] = total_delivered;
		}
	}

	/* Generate score for variety of cargo */
	{
		_score_part[owner][SCORE_CARGO] = c->old_economy->delivered_cargo.GetCount();
	}

	/* Generate score for company's money */
	{
		if (c->money > 0) {
			_score_part[owner][SCORE_MONEY] = c->money;
		}
	}

	/* Generate score for loan */
	{
		_score_part[owner][SCORE_LOAN] = _score_info[SCORE_LOAN].needed - c->current_loan;
	}

	/* Now we calculate the score for each item.. */
	{
		int total_score = 0;
		int s;
		score = 0;
		for (ScoreID i = SCORE_BEGIN; i < SCORE_END; i++) {
			/* Skip the total */
			if (i == SCORE_TOTAL) continue;
			/*  Check the score */
			s = Clamp<int64>(_score_part[owner][i], 0, _score_info[i].needed) * _score_info[i].score / _score_info[i].needed;
			score += s;
			total_score += _score_info[i].score;
		}

		_score_part[owner][SCORE_TOTAL] = score;

		/*  We always want the score scaled to SCORE_MAX (1000) */
		if (total_score != SCORE_MAX) score = score * SCORE_MAX / total_score;
	}

	if (update) {
		c->old_economy[0].performance_history = score;
		UpdateCompanyHQ(c->location_of_HQ, score);
		c->old_economy[0].company_value = CalculateCompanyValue(c);
	}

	SetWindowDirty(WC_PERFORMANCE_DETAIL, 0);
	return score;
}

/**
 * Change the ownership of all the items of a company.
 * @param old_owner The company that gets removed.
 * @param new_owner The company to merge to, or INVALID_OWNER to remove the company.
 */
void ChangeOwnershipOfCompanyItems(Owner old_owner, Owner new_owner)
{
	/* We need to set _current_company to old_owner before we try to move
	 * the client. This is needed as it needs to know whether "you" really
	 * are the current local company. */
	Backup<CompanyID> cur_company(_current_company, old_owner, FILE_LINE);
	/* In all cases, make spectators of clients connected to that company */
	if (_networking) NetworkClientsToSpectators(old_owner);
	if (old_owner == _local_company) {
		/* Single player cheated to AI company.
		 * There are no spectators in singleplayer mode, so we must pick some other company. */
		assert(!_networking);
		Backup<CompanyID> cur_company2(_current_company, FILE_LINE);
		for (const Company *c : Company::Iterate()) {
			if (c->index != old_owner) {
				SetLocalCompany(c->index);
				break;
			}
		}
		cur_company2.Restore();
		assert(old_owner != _local_company);
	}

	assert(old_owner != new_owner);

	{
		uint i;

		/* See if the old_owner had shares in other companies */
		for (const Company *c : Company::Iterate()) {
			for (i = 0; i < 4; i++) {
				if (c->share_owners[i] == old_owner) {
					/* Sell his shares */
					CommandCost res = DoCommand(0, c->index, 0, DC_EXEC | DC_BANKRUPT, CMD_SELL_SHARE_IN_COMPANY);
					/* Because we are in a DoCommand, we can't just execute another one and
					 *  expect the money to be removed. We need to do it ourself! */
					SubtractMoneyFromCompany(res);
				}
			}
		}

		/* Sell all the shares that people have on this company */
		Backup<CompanyID> cur_company2(_current_company, FILE_LINE);
		const Company *c = Company::Get(old_owner);
		for (i = 0; i < 4; i++) {
			cur_company2.Change(c->share_owners[i]);
			if (_current_company != INVALID_OWNER) {
				/* Sell the shares */
				CommandCost res = DoCommand(0, old_owner, 0, DC_EXEC | DC_BANKRUPT, CMD_SELL_SHARE_IN_COMPANY);
				/* Because we are in a DoCommand, we can't just execute another one and
				 *  expect the money to be removed. We need to do it ourself! */
				SubtractMoneyFromCompany(res);
			}
		}
		cur_company2.Restore();
	}

	/* Temporarily increase the company's money, to be sure that
	 * removing his/her property doesn't fail because of lack of money.
	 * Not too drastically though, because it could overflow */
	if (new_owner == INVALID_OWNER) {
		Company::Get(old_owner)->money = UINT64_MAX >> 2; // jackpot ;p
	}

	for (Subsidy *s : Subsidy::Iterate()) {
		if (s->awarded == old_owner) {
			if (new_owner == INVALID_OWNER) {
				delete s;
			} else {
				s->awarded = new_owner;
			}
		}
	}
	if (new_owner == INVALID_OWNER) RebuildSubsidisedSourceAndDestinationCache();

	/* Take care of rating and transport rights in towns */
	for (Town *t : Town::Iterate()) {
		/* If a company takes over, give the ratings to that company. */
		if (new_owner != INVALID_OWNER) {
			if (HasBit(t->have_ratings, old_owner)) {
				if (HasBit(t->have_ratings, new_owner)) {
					/* use max of the two ratings. */
					t->ratings[new_owner] = std::max(t->ratings[new_owner], t->ratings[old_owner]);
				} else {
					SetBit(t->have_ratings, new_owner);
					t->ratings[new_owner] = t->ratings[old_owner];
				}
			}
		}

		/* Reset the ratings for the old owner */
		t->ratings[old_owner] = RATING_INITIAL;
		ClrBit(t->have_ratings, old_owner);

		/* Transfer exclusive rights */
		if (t->exclusive_counter > 0 && t->exclusivity == old_owner) {
			if (new_owner != INVALID_OWNER) {
				t->exclusivity = new_owner;
			} else {
				t->exclusive_counter = 0;
				t->exclusivity = INVALID_COMPANY;
			}
		}
	}

	{
		for (Vehicle *v : Vehicle::Iterate()) {
			if (v->owner == old_owner && IsCompanyBuildableVehicleType(v->type)) {
				if (new_owner == INVALID_OWNER) {
					if (v->Previous() == nullptr) delete v;
				} else {
					if (v->IsEngineCountable()) GroupStatistics::CountEngine(v, -1);
					if (v->IsPrimaryVehicle()) GroupStatistics::CountVehicle(v, -1);
				}
			}
		}
	}

	/* In all cases clear replace engine rules.
	 * Even if it was copied, it could interfere with new owner's rules */
	RemoveAllEngineReplacementForCompany(Company::Get(old_owner));

	if (new_owner == INVALID_OWNER) {
		RemoveAllGroupsForCompany(old_owner);
	} else {
		for (Group *g : Group::Iterate()) {
			if (g->owner == old_owner) g->owner = new_owner;
		}
	}

	{
		FreeUnitIDGenerator unitidgen[] = {
			FreeUnitIDGenerator(VEH_TRAIN, new_owner), FreeUnitIDGenerator(VEH_ROAD,     new_owner),
			FreeUnitIDGenerator(VEH_SHIP,  new_owner), FreeUnitIDGenerator(VEH_AIRCRAFT, new_owner)
		};

		/* Override company settings to new company defaults in case we need to convert them.
		 * This is required as the CmdChangeServiceInt doesn't copy the supplied value when it is non-custom
		 */
		if (new_owner != INVALID_OWNER) {
			Company *old_company = Company::Get(old_owner);
			Company *new_company = Company::Get(new_owner);

			old_company->settings.vehicle.servint_aircraft = new_company->settings.vehicle.servint_aircraft;
			old_company->settings.vehicle.servint_trains = new_company->settings.vehicle.servint_trains;
			old_company->settings.vehicle.servint_roadveh = new_company->settings.vehicle.servint_roadveh;
			old_company->settings.vehicle.servint_ships = new_company->settings.vehicle.servint_ships;
			old_company->settings.vehicle.servint_ispercent = new_company->settings.vehicle.servint_ispercent;
		}

		for (Vehicle *v : Vehicle::Iterate()) {
			if (v->owner == old_owner && IsCompanyBuildableVehicleType(v->type)) {
				assert(new_owner != INVALID_OWNER);

				/* Correct default values of interval settings while maintaining custom set ones.
				 * This prevents invalid values on mismatching company defaults being accepted.
				 */
				if (!v->ServiceIntervalIsCustom()) {
					Company *new_company = Company::Get(new_owner);

					/* Technically, passing the interval is not needed as the command will query the default value itself.
					 * However, do not rely on that behaviour.
					 */
					int interval = CompanyServiceInterval(new_company, v->type);
					DoCommand(v->tile, v->index, interval | (new_company->settings.vehicle.servint_ispercent << 17), DC_EXEC | DC_BANKRUPT, CMD_CHANGE_SERVICE_INT);
				}

				v->owner = new_owner;

				/* Owner changes, clear cache */
				v->colourmap = PAL_NONE;
				v->InvalidateNewGRFCache();

				if (v->IsEngineCountable()) {
					GroupStatistics::CountEngine(v, 1);
				}
				if (v->IsPrimaryVehicle()) {
					GroupStatistics::CountVehicle(v, 1);
					v->unitnumber = unitidgen[v->type].NextID();
				}

				/* Invalidate the vehicle's cargo payment "owner cache". */
				if (v->cargo_payment != nullptr) v->cargo_payment->owner = nullptr;
			}
		}

		if (new_owner != INVALID_OWNER) GroupStatistics::UpdateAutoreplace(new_owner);
	}

	/*  Change ownership of tiles */
	{
		TileIndex tile = 0;
		do {
			ChangeTileOwner(tile, old_owner, new_owner);
		} while (++tile != MapSize());

		if (new_owner != INVALID_OWNER) {
			/* Update all signals because there can be new segment that was owned by two companies
			 * and signals were not propagated
			 * Similar with crossings - it is needed to bar crossings that weren't before
			 * because of different owner of crossing and approaching train */
			tile = 0;

			do {
				if (IsTileType(tile, MP_RAILWAY) && IsTileOwner(tile, new_owner) && HasSignals(tile)) {
					TrackBits tracks = GetTrackBits(tile);
					do { // there may be two tracks with signals for TRACK_BIT_HORZ and TRACK_BIT_VERT
						Track track = RemoveFirstTrack(&tracks);
						if (HasSignalOnTrack(tile, track)) AddTrackToSignalBuffer(tile, track, new_owner);
					} while (tracks != TRACK_BIT_NONE);
				} else if (IsLevelCrossingTile(tile) && IsTileOwner(tile, new_owner)) {
					UpdateLevelCrossing(tile);
				}
			} while (++tile != MapSize());
		}

		/* update signals in buffer */
		UpdateSignalsInBuffer();
	}

	/* Add airport infrastructure count of the old company to the new one. */
	if (new_owner != INVALID_OWNER) Company::Get(new_owner)->infrastructure.airport += Company::Get(old_owner)->infrastructure.airport;

	/* convert owner of stations (including deleted ones, but excluding buoys) */
	for (Station *st : Station::Iterate()) {
		if (st->owner == old_owner) {
			/* if a company goes bankrupt, set owner to OWNER_NONE so the sign doesn't disappear immediately
			 * also, drawing station window would cause reading invalid company's colour */
			st->owner = new_owner == INVALID_OWNER ? OWNER_NONE : new_owner;
		}
	}

	/* do the same for waypoints (we need to do this here so deleted waypoints are converted too) */
	for (Waypoint *wp : Waypoint::Iterate()) {
		if (wp->owner == old_owner) {
			wp->owner = new_owner == INVALID_OWNER ? OWNER_NONE : new_owner;
		}
	}

	for (Sign *si : Sign::Iterate()) {
		if (si->owner == old_owner) si->owner = new_owner == INVALID_OWNER ? OWNER_NONE : new_owner;
	}

	/* Remove Game Script created Goals, CargoMonitors and Story pages. */
	for (Goal *g : Goal::Iterate()) {
		if (g->company == old_owner) delete g;
	}

	ClearCargoPickupMonitoring(old_owner);
	ClearCargoDeliveryMonitoring(old_owner);

	for (StoryPage *sp : StoryPage::Iterate()) {
		if (sp->company == old_owner) delete sp;
	}

	/* Change colour of existing windows */
	if (new_owner != INVALID_OWNER) ChangeWindowOwner(old_owner, new_owner);

	cur_company.Restore();

	MarkWholeScreenDirty();
}

/**
 * Check for bankruptcy of a company. Called every three months.
 * @param c Company to check.
 */
static void CompanyCheckBankrupt(Company *c)
{
	/*  If the company has money again, it does not go bankrupt */
	if (c->money - c->current_loan >= -_economy.max_loan) {
		int previous_months_of_bankruptcy = CeilDiv(c->months_of_bankruptcy, 3);
		c->months_of_bankruptcy = 0;
		c->bankrupt_asked = 0;
		if (previous_months_of_bankruptcy != 0) CompanyAdminUpdate(c);
		return;
	}

	c->months_of_bankruptcy++;

	switch (c->months_of_bankruptcy) {
		/* All the boring cases (months) with a bad balance where no action is taken */
		case 0:
		case 1:
		case 2:
		case 3:

		case 5:
		case 6:

		case 8:
		case 9:
			break;

		/* Warn about bankruptcy after 3 months */
		case 4: {
			CompanyNewsInformation *cni = MallocT<CompanyNewsInformation>(1);
			cni->FillData(c);
			SetDParam(0, STR_NEWS_COMPANY_IN_TROUBLE_TITLE);
			SetDParam(1, STR_NEWS_COMPANY_IN_TROUBLE_DESCRIPTION);
			SetDParamStr(2, cni->company_name);
			AddCompanyNewsItem(STR_MESSAGE_NEWS_FORMAT, cni);
			AI::BroadcastNewEvent(new ScriptEventCompanyInTrouble(c->index));
			Game::NewEvent(new ScriptEventCompanyInTrouble(c->index));
			break;
		}

		/* Offer company for sale after 6 months */
		case 7: {
			/* Don't consider the loan */
			Money val = CalculateCompanyValue(c, false);

			c->bankrupt_value = val;
			c->bankrupt_asked = 1 << c->index; // Don't ask the owner
			c->bankrupt_timeout = 0;

			/* The company assets should always have some value */
			assert(c->bankrupt_value > 0);
			break;
		}

		/* Bankrupt company after 6 months (if the company has no value) or latest
		 * after 9 months (if it still had value after 6 months) */
		default:
		case 10: {
			if (!_networking && _local_company == c->index) {
				/* If we are in singleplayer mode, leave the company playing. Eg. there
				 * is no THE-END, otherwise mark the client as spectator to make sure
				 * he/she is no long in control of this company. However... when you
				 * join another company (cheat) the "unowned" company can bankrupt. */
				c->bankrupt_asked = MAX_UVALUE(CompanyMask);
				break;
			}

			/* Actually remove the company, but not when we're a network client.
			 * In case of network clients we will be getting a command from the
			 * server. It is done in this way as we are called from the
			 * StateGameLoop which can't change the current company, and thus
			 * updating the local company triggers an assert later on. In the
			 * case of a network game the command will be processed at a time
			 * that changing the current company is okay. In case of single
			 * player we are sure (the above check) that we are not the local
			 * company and thus we won't be moved. */
			if (!_networking || _network_server) {
				DoCommandP(0, CCA_DELETE | (c->index << 16) | (CRR_BANKRUPT << 24), 0, CMD_COMPANY_CTRL);
				return;
			}
			break;
		}
	}

	if (CeilDiv(c->months_of_bankruptcy, 3) != CeilDiv(c->months_of_bankruptcy - 1, 3)) CompanyAdminUpdate(c);
}

/**
 * Update the finances of all companies.
 * Pay for the stations, update the history graph, update ratings and company values, and deal with bankruptcy.
 */
static void CompaniesGenStatistics()
{
	/* Check for bankruptcy each month */
	for (Company *c : Company::Iterate()) {
		CompanyCheckBankrupt(c);
	}

	Backup<CompanyID> cur_company(_current_company, FILE_LINE);

	if (!_settings_game.economy.infrastructure_maintenance) {
		for (const Station *st : Station::Iterate()) {
			cur_company.Change(st->owner);
			CommandCost cost(EXPENSES_PROPERTY, _price[PR_STATION_VALUE] >> 1);
			SubtractMoneyFromCompany(cost);
		}
	} else {
		/* Improved monthly infrastructure costs. */
		for (const Company *c : Company::Iterate()) {
			cur_company.Change(c->index);

			CommandCost cost(EXPENSES_PROPERTY);
			uint32 rail_total = c->infrastructure.GetRailTotal();
			for (RailType rt = RAILTYPE_BEGIN; rt < RAILTYPE_END; rt++) {
				if (c->infrastructure.rail[rt] != 0) cost.AddCost(RailMaintenanceCost(rt, c->infrastructure.rail[rt], rail_total));
			}
			cost.AddCost(SignalMaintenanceCost(c->infrastructure.signal));
			uint32 road_total = c->infrastructure.GetRoadTotal();
			uint32 tram_total = c->infrastructure.GetTramTotal();
			for (RoadType rt = ROADTYPE_BEGIN; rt < ROADTYPE_END; rt++) {
				if (c->infrastructure.road[rt] != 0) cost.AddCost(RoadMaintenanceCost(rt, c->infrastructure.road[rt], RoadTypeIsRoad(rt) ? road_total : tram_total));
			}
			cost.AddCost(CanalMaintenanceCost(c->infrastructure.water));
			cost.AddCost(StationMaintenanceCost(c->infrastructure.station));
			cost.AddCost(AirportMaintenanceCost(c->index));

			SubtractMoneyFromCompany(cost);
		}
	}
	cur_company.Restore();

	/* Only run the economic statics and update company stats every 3rd month (1st of quarter). */
	if (!HasBit(1 << 0 | 1 << 3 | 1 << 6 | 1 << 9, _cur_month)) return;

	for (Company *c : Company::Iterate()) {
		/* Drop the oldest history off the end */
		std::copy_backward(c->old_economy, c->old_economy + MAX_HISTORY_QUARTERS - 1, c->old_economy + MAX_HISTORY_QUARTERS);
		c->old_economy[0] = c->cur_economy;
		c->cur_economy = {};

		if (c->num_valid_stat_ent != MAX_HISTORY_QUARTERS) c->num_valid_stat_ent++;

		UpdateCompanyRatingAndValue(c, true);
		if (c->block_preview != 0) c->block_preview--;
	}

	SetWindowDirty(WC_INCOME_GRAPH, 0);
	SetWindowDirty(WC_OPERATING_PROFIT, 0);
	SetWindowDirty(WC_DELIVERED_CARGO, 0);
	SetWindowDirty(WC_PERFORMANCE_HISTORY, 0);
	SetWindowDirty(WC_COMPANY_VALUE, 0);
	SetWindowDirty(WC_COMPANY_LEAGUE, 0);
}

/**
 * Add monthly inflation
 * @param check_year Shall the inflation get stopped after 170 years?
 * @return true if inflation is maxed and nothing was changed
 */
bool AddInflation(bool check_year)
{
	/* The cargo payment inflation differs from the normal inflation, so the
	 * relative amount of money you make with a transport decreases slowly over
	 * the 170 years. After a few hundred years we reach a level in which the
	 * games will become unplayable as the maximum income will be less than
	 * the minimum running cost.
	 *
	 * Furthermore there are a lot of inflation related overflows all over the
	 * place. Solving them is hardly possible because inflation will always
	 * reach the overflow threshold some day. So we'll just perform the
	 * inflation mechanism during the first 170 years (the amount of years that
	 * one had in the original TTD) and stop doing the inflation after that
	 * because it only causes problems that can't be solved nicely and the
	 * inflation doesn't add anything after that either; it even makes playing
	 * it impossible due to the diverging cost and income rates.
	 */
	if (check_year && (_cur_year < ORIGINAL_BASE_YEAR || _cur_year >= ORIGINAL_MAX_YEAR)) return true;

	if (_economy.inflation_prices == MAX_INFLATION || _economy.inflation_payment == MAX_INFLATION) return true;

	/* Approximation for (100 + infl_amount)% ** (1 / 12) - 100%
	 * scaled by 65536
	 * 12 -> months per year
	 * This is only a good approximation for small values
	 */
	_economy.inflation_prices  += (_economy.inflation_prices  * _economy.infl_amount    * 54) >> 16;
	_economy.inflation_payment += (_economy.inflation_payment * _economy.infl_amount_pr * 54) >> 16;

	if (_economy.inflation_prices > MAX_INFLATION) _economy.inflation_prices = MAX_INFLATION;
	if (_economy.inflation_payment > MAX_INFLATION) _economy.inflation_payment = MAX_INFLATION;

	return false;
}

/**
 * Computes all prices, payments and maximum loan.
 */
void RecomputePrices()
{
	/* Setup maximum loan */
	_economy.max_loan = ((uint64)_settings_game.difficulty.max_loan * _economy.inflation_prices >> 16) / 50000 * 50000;

	/* Setup price bases */
	for (Price i = PR_BEGIN; i < PR_END; i++) {
		Money price = _price_base_specs[i].start_price;

		/* Apply difficulty settings */
		uint mod = 1;
		switch (_price_base_specs[i].category) {
			case PCAT_RUNNING:
				mod = _settings_game.difficulty.vehicle_costs;
				break;

			case PCAT_CONSTRUCTION:
				mod = _settings_game.difficulty.construction_cost;
				break;

			default: break;
		}
		switch (mod) {
			case 0: price *= 6; break;
			case 1: price *= 8; break; // normalised to 1 below
			case 2: price *= 9; break;
			default: NOT_REACHED();
		}

		/* Apply inflation */
		price = (int64)price * _economy.inflation_prices;

		/* Apply newgrf modifiers, remove fractional part of inflation, and normalise on medium difficulty. */
		int shift = _price_base_multiplier[i] - 16 - 3;
		if (shift >= 0) {
			price <<= shift;
		} else {
			price >>= -shift;
		}

		/* Make sure the price does not get reduced to zero.
		 * Zero breaks quite a few commands that use a zero
		 * cost to see whether something got changed or not
		 * and based on that cause an error. When the price
		 * is zero that fails even when things are done. */
		if (price == 0) {
			price = Clamp(_price_base_specs[i].start_price, -1, 1);
			/* No base price should be zero, but be sure. */
			assert(price != 0);
		}
		/* Store value */
		_price[i] = price;
	}

	/* Setup cargo payment */
	CargoSpec *cs;
	FOR_ALL_CARGOSPECS(cs) {
		cs->current_payment = ((int64)cs->initial_payment * _economy.inflation_payment) >> 16;
	}

	SetWindowClassesDirty(WC_BUILD_VEHICLE);
	SetWindowClassesDirty(WC_REPLACE_VEHICLE);
	SetWindowClassesDirty(WC_VEHICLE_DETAILS);
	SetWindowClassesDirty(WC_COMPANY_INFRASTRUCTURE);
	InvalidateWindowData(WC_PAYMENT_RATES, 0);
}

/** Let all companies pay the monthly interest on their loan. */
static void CompaniesPayInterest()
{
	Backup<CompanyID> cur_company(_current_company, FILE_LINE);
	for (const Company *c : Company::Iterate()) {
		cur_company.Change(c->index);

		/* Over a year the paid interest should be "loan * interest percentage",
		 * but... as that number is likely not dividable by 12 (pay each month),
		 * one needs to account for that in the monthly fee calculations.
		 * To easily calculate what one should pay "this" month, you calculate
		 * what (total) should have been paid up to this month and you subtract
		 * whatever has been paid in the previous months. This will mean one month
		 * it'll be a bit more and the other it'll be a bit less than the average
		 * monthly fee, but on average it will be exact.
		 * In order to prevent cheating or abuse (just not paying interest by not
		 * taking a loan we make companies pay interest on negative cash as well
		 */
		Money yearly_fee = c->current_loan * _economy.interest_rate / 100;
		if (c->money < 0) {
			yearly_fee += -c->money *_economy.interest_rate / 100;
		}
		Money up_to_previous_month = yearly_fee * _cur_month / 12;
		Money up_to_this_month = yearly_fee * (_cur_month + 1) / 12;

		SubtractMoneyFromCompany(CommandCost(EXPENSES_LOAN_INT, up_to_this_month - up_to_previous_month));

		SubtractMoneyFromCompany(CommandCost(EXPENSES_OTHER, _price[PR_STATION_VALUE] >> 2));
	}
	cur_company.Restore();
}

static void HandleEconomyFluctuations()
{
	if (_settings_game.difficulty.economy != 0) {
		/* When economy is Fluctuating, decrease counter */
		_economy.fluct--;
	} else if (EconomyIsInRecession()) {
		/* When it's Steady and we are in recession, end it now */
		_economy.fluct = -12;
	} else {
		/* No need to do anything else in other cases */
		return;
	}

	if (_economy.fluct == 0) {
		_economy.fluct = -(int)GB(Random(), 0, 2);
		AddNewsItem(STR_NEWS_BEGIN_OF_RECESSION, NT_ECONOMY, NF_NORMAL);
	} else if (_economy.fluct == -12) {
		_economy.fluct = GB(Random(), 0, 8) + 312;
		AddNewsItem(STR_NEWS_END_OF_RECESSION, NT_ECONOMY, NF_NORMAL);
	}
}


/**
 * Reset changes to the price base multipliers.
 */
void ResetPriceBaseMultipliers()
{
	memset(_price_base_multiplier, 0, sizeof(_price_base_multiplier));
}

/**
 * Change a price base by the given factor.
 * The price base is altered by factors of two.
 * NewBaseCost = OldBaseCost * 2^n
 * @param price Index of price base to change.
 * @param factor Amount to change by.
 */
void SetPriceBaseMultiplier(Price price, int factor)
{
	assert(price < PR_END);
	_price_base_multiplier[price] = Clamp(factor, MIN_PRICE_MODIFIER, MAX_PRICE_MODIFIER);
}

/**
 * Initialize the variables that will maintain the daily industry change system.
 * @param init_counter specifies if the counter is required to be initialized
 */
void StartupIndustryDailyChanges(bool init_counter)
{
	uint map_size = MapLogX() + MapLogY();
	/* After getting map size, it needs to be scaled appropriately and divided by 31,
	 * which stands for the days in a month.
	 * Using just 31 will make it so that a monthly reset (based on the real number of days of that month)
	 * would not be needed.
	 * Since it is based on "fractional parts", the leftover days will not make much of a difference
	 * on the overall total number of changes performed */
	_economy.industry_daily_increment = (1 << map_size) / 31;

	if (init_counter) {
		/* A new game or a savegame from an older version will require the counter to be initialized */
		_economy.industry_daily_change_counter = 0;
	}
}

void StartupEconomy()
{
	_economy.interest_rate = _settings_game.difficulty.initial_interest;
	_economy.infl_amount = _settings_game.difficulty.initial_interest;
	_economy.infl_amount_pr = std::max(0, _settings_game.difficulty.initial_interest - 1);
	_economy.fluct = GB(Random(), 0, 8) + 168;

	if (_settings_game.economy.inflation) {
		/* Apply inflation that happened before our game start year. */
		int months = (std::min(_cur_year, ORIGINAL_MAX_YEAR) - ORIGINAL_BASE_YEAR) * 12;
		for (int i = 0; i < months; i++) {
			AddInflation(false);
		}
	}

	/* Set up prices */
	RecomputePrices();

	StartupIndustryDailyChanges(true); // As we are starting a new game, initialize the counter too

}

/**
 * Resets economy to initial values
 */
void InitializeEconomy()
{
	_economy.inflation_prices = _economy.inflation_payment = 1 << 16;
	ClearCargoPickupMonitoring();
	ClearCargoDeliveryMonitoring();
}

/**
 * Determine a certain price
 * @param index Price base
 * @param cost_factor Price factor
 * @param grf_file NewGRF to use local price multipliers from.
 * @param shift Extra bit shifting after the computation
 * @return Price
 */
Money GetPrice(Price index, uint cost_factor, const GRFFile *grf_file, int shift)
{
	if (index >= PR_END) return 0;

	Money cost = _price[index] * cost_factor;
	if (grf_file != nullptr) shift += grf_file->price_base_multipliers[index];

	if (shift >= 0) {
		cost <<= shift;
	} else {
		cost >>= -shift;
	}

	return cost;
}

Money GetTransportedGoodsIncome(uint num_pieces, uint dist, uint16 transit_days, CargoID cargo_type)
{
	const CargoSpec *cs = CargoSpec::Get(cargo_type);
	if (!cs->IsValid()) {
		/* User changed newgrfs and some vehicle still carries some cargo which is no longer available. */
		return 0;
	}

	/* Use callback to calculate cargo profit, if available */
	if (HasBit(cs->callback_mask, CBM_CARGO_PROFIT_CALC)) {
		uint32 var18 = std::min(dist, 0xFFFFu) | (std::min(num_pieces, 0xFFu) << 16) | (std::min<uint16>(transit_days, 0xFFu) << 24);
		uint16 callback = GetCargoCallback(CBID_CARGO_PROFIT_CALC, 0, var18, cs);
		if (callback != CALLBACK_FAILED) {
			int result = GB(callback, 0, 14);

			/* Simulate a 15 bit signed value */
			if (HasBit(callback, 14)) result -= 0x4000;

			/* "The result should be a signed multiplier that gets multiplied
			 * by the amount of cargo moved and the price factor, then gets
			 * divided by 8192." */
			return result * num_pieces * cs->current_payment / 8192;
		}
	}

	static const int TIME_FACTOR_FRAC_BITS = 4;
	static const int TIME_FACTOR_FRAC = 1 << TIME_FACTOR_FRAC_BITS;

	const int max_time_factor = cs->age_time_factor[0];
	const int min_time_factor = cs->age_time_factor[1];
	const int days1 = cs->transit_days[0];
	const int days2 = cs->transit_days[1];
	const int days_over_days1 = std::max(   transit_days - days1, 0);
	const int days_over_days2 = std::max(days_over_days1 - days2, 0);
	const int days_over_max   = transit_days - (max_time_factor - min_time_factor + 2 * days1 + days2) / 2;

	/*
	 * The time factor is calculated based on the time it took
	 * (transit_days) compared two cargo-depending values. The
	 * range is divided into four parts:
	 *
	 *  - constant for fast transits
	 *  - linear decreasing with time with a slope of -1 for medium transports
	 *  - linear decreasing with time with a slope of -2 for slow transports
	 *  - after hitting MIN_TIME_FACTOR, the time factor will be asymptotically decreased to a limit of 1 with a scaled 1/(x+1) function.
	 *
	 */
	if (days_over_max > 0) {
		const int time_factor = std::max(min_time_factor * TIME_FACTOR_FRAC * TIME_FACTOR_FRAC / (days_over_max + TIME_FACTOR_FRAC), 1); // min_time_factor / (x/TIME_FACTOR_FRAC + 1), expressed as fixed point with TIME_FACTOR_FRAC_BITS.
		return BigMulS(dist * time_factor * num_pieces, cs->current_payment, 21 + TIME_FACTOR_FRAC_BITS);
	} else {
		const int time_factor = std::max(max_time_factor - days_over_days1 - days_over_days2, min_time_factor);
		return BigMulS(dist * time_factor * num_pieces, cs->current_payment, 21);
	}
}

/** The industries we've currently brought cargo to. */
static SmallIndustryList _cargo_delivery_destinations;

/**
 * Transfer goods from station to industry.
 * All cargo is delivered to the nearest (Manhattan) industry to the station sign, which is inside the acceptance rectangle and actually accepts the cargo.
 * @param st The station that accepted the cargo
 * @param cargo_type Type of cargo delivered
 * @param num_pieces Amount of cargo delivered
 * @param source The source of the cargo
 * @param company The company delivering the cargo
 * @return actually accepted pieces of cargo
 */
static uint DeliverGoodsToIndustry(const Station *st, CargoID cargo_type, uint num_pieces, IndustryID source, CompanyID company)
{
	/* Find the nearest industrytile to the station sign inside the catchment area, whose industry accepts the cargo.
	 * This fails in three cases:
	 *  1) The station accepts the cargo because there are enough houses around it accepting the cargo.
	 *  2) The industries in the catchment area temporarily reject the cargo, and the daily station loop has not yet updated station acceptance.
	 *  3) The results of callbacks CBID_INDUSTRY_REFUSE_CARGO and CBID_INDTILE_CARGO_ACCEPTANCE are inconsistent. (documented behaviour)
	 */

	uint accepted = 0;

	for (Industry *ind : st->industries_near) {
		if (num_pieces == 0) break;

		if (ind->index == source) continue;

		uint cargo_index;
		for (cargo_index = 0; cargo_index < lengthof(ind->accepts_cargo); cargo_index++) {
			if (cargo_type == ind->accepts_cargo[cargo_index]) break;
		}
		/* Check if matching cargo has been found */
		if (cargo_index >= lengthof(ind->accepts_cargo)) continue;

		/* Check if industry temporarily refuses acceptance */
		if (IndustryTemporarilyRefusesCargo(ind, cargo_type)) continue;

		if (ind->exclusive_supplier != INVALID_OWNER && ind->exclusive_supplier != st->owner) continue;

		/* Insert the industry into _cargo_delivery_destinations, if not yet contained */
		include(_cargo_delivery_destinations, ind);

		uint amount = std::min(num_pieces, 0xFFFFu - ind->incoming_cargo_waiting[cargo_index]);
		ind->incoming_cargo_waiting[cargo_index] += amount;
		ind->last_cargo_accepted_at[cargo_index] = _date;
		num_pieces -= amount;
		accepted += amount;

		/* Update the cargo monitor. */
		AddCargoDelivery(cargo_type, company, amount, ST_INDUSTRY, source, st, ind->index);
	}

	return accepted;
}

/**
 * Delivers goods to industries/towns and calculates the payment
 * @param num_pieces amount of cargo delivered
 * @param cargo_type the type of cargo that is delivered
 * @param dest Station the cargo has been unloaded
 * @param source_tile The origin of the cargo for distance calculation
 * @param days_in_transit Travel time
 * @param company The company delivering the cargo
 * @param src_type Type of source of cargo (industry, town, headquarters)
 * @param src Index of source of cargo
 * @return Revenue for delivering cargo
 * @note The cargo is just added to the stockpile of the industry. It is due to the caller to trigger the industry's production machinery
 */
static Money DeliverGoods(int num_pieces, CargoID cargo_type, StationID dest, TileIndex source_tile, uint16 days_in_transit, Company *company, SourceType src_type, SourceID src)
{
	assert(num_pieces > 0);

	Station *st = Station::Get(dest);

	/* Give the goods to the industry. */
	uint accepted_ind = DeliverGoodsToIndustry(st, cargo_type, num_pieces, src_type == ST_INDUSTRY ? src : INVALID_INDUSTRY, company->index);

	/* If this cargo type is always accepted, accept all */
	uint accepted_total = HasBit(st->always_accepted, cargo_type) ? num_pieces : accepted_ind;

	/* Update station statistics */
	if (accepted_total > 0) {
		SetBit(st->goods[cargo_type].status, GoodsEntry::GES_EVER_ACCEPTED);
		SetBit(st->goods[cargo_type].status, GoodsEntry::GES_CURRENT_MONTH);
		SetBit(st->goods[cargo_type].status, GoodsEntry::GES_ACCEPTED_BIGTICK);
	}

	/* Update company statistics */
	company->cur_economy.delivered_cargo[cargo_type] += accepted_total;

	/* Increase town's counter for town effects */
	const CargoSpec *cs = CargoSpec::Get(cargo_type);
	st->town->received[cs->town_effect].new_act += accepted_total;

	/* Determine profit */
	Money profit = GetTransportedGoodsIncome(accepted_total, DistanceManhattan(source_tile, st->xy), days_in_transit, cargo_type);

	/* Update the cargo monitor. */
	AddCargoDelivery(cargo_type, company->index, accepted_total - accepted_ind, src_type, src, st);

	/* Modify profit if a subsidy is in effect */
	if (CheckSubsidised(cargo_type, company->index, src_type, src, st))  {
		switch (_settings_game.difficulty.subsidy_multiplier) {
			case 0:  profit += profit >> 1; break;
			case 1:  profit *= 2; break;
			case 2:  profit *= 3; break;
			default: profit *= 4; break;
		}
	}

	return profit;
}

/**
 * Inform the industry about just delivered cargo
 * DeliverGoodsToIndustry() silently incremented incoming_cargo_waiting, now it is time to do something with the new cargo.
 * @param i The industry to process
 */
static void TriggerIndustryProduction(Industry *i)
{
	const IndustrySpec *indspec = GetIndustrySpec(i->type);
	uint16 callback = indspec->callback_mask;

	i->was_cargo_delivered = true;

	if (HasBit(callback, CBM_IND_PRODUCTION_CARGO_ARRIVAL) || HasBit(callback, CBM_IND_PRODUCTION_256_TICKS)) {
		if (HasBit(callback, CBM_IND_PRODUCTION_CARGO_ARRIVAL)) {
			IndustryProductionCallback(i, 0);
		} else {
			SetWindowDirty(WC_INDUSTRY_VIEW, i->index);
		}
	} else {
		for (uint ci_in = 0; ci_in < lengthof(i->incoming_cargo_waiting); ci_in++) {
			uint cargo_waiting = i->incoming_cargo_waiting[ci_in];
			if (cargo_waiting == 0) continue;

			for (uint ci_out = 0; ci_out < lengthof(i->produced_cargo_waiting); ci_out++) {
				i->produced_cargo_waiting[ci_out] = std::min(i->produced_cargo_waiting[ci_out] + (cargo_waiting * indspec->input_cargo_multiplier[ci_in][ci_out] / 256), 0xFFFFu);
			}

			i->incoming_cargo_waiting[ci_in] = 0;
		}
	}

	TriggerIndustry(i, INDUSTRY_TRIGGER_RECEIVED_CARGO);
	StartStopIndustryTileAnimation(i, IAT_INDUSTRY_RECEIVED_CARGO);
}

/**
 * Makes us a new cargo payment helper.
 * @param front The front of the train
 */
CargoPayment::CargoPayment(Vehicle *front) :
	front(front),
	current_station(front->last_station_visited)
{
}

CargoPayment::~CargoPayment()
{
	if (this->CleaningPool()) return;

	this->front->cargo_payment = nullptr;

	if (this->visual_profit == 0 && this->visual_transfer == 0) return;

	Backup<CompanyID> cur_company(_current_company, this->front->owner, FILE_LINE);

	SubtractMoneyFromCompany(CommandCost(this->front->GetExpenseType(true), -this->route_profit));
	this->front->profit_this_year += (this->visual_profit + this->visual_transfer) << 8;

	if (this->route_profit != 0 && IsLocalCompany() && !PlayVehicleSound(this->front, VSE_LOAD_UNLOAD)) {
		SndPlayVehicleFx(SND_14_CASHTILL, this->front);
	}

	if (this->visual_transfer != 0) {
		ShowFeederIncomeAnimation(this->front->x_pos, this->front->y_pos,
				this->front->z_pos, this->visual_transfer, -this->visual_profit);
	} else if (this->visual_profit != 0) {
		ShowCostOrIncomeAnimation(this->front->x_pos, this->front->y_pos,
				this->front->z_pos, -this->visual_profit);
	}

	cur_company.Restore();
}

/**
 * Handle payment for final delivery of the given cargo packet.
 * @param cp The cargo packet to pay for.
 * @param count The number of packets to pay for.
 */
void CargoPayment::PayFinalDelivery(const CargoPacket *cp, uint count)
{
	if (this->owner == nullptr) {
		this->owner = Company::Get(this->front->owner);
	}

	/* Handle end of route payment */
	Money profit = DeliverGoods(count, this->ct, this->current_station, cp->SourceStationXY(), cp->DaysInTransit(), this->owner, cp->SourceSubsidyType(), cp->SourceSubsidyID());
	this->route_profit += profit;

	/* The vehicle's profit is whatever route profit there is minus feeder shares. */
	this->visual_profit += profit - cp->FeederShare(count);
}

/**
 * Handle payment for transfer of the given cargo packet.
 * @param cp The cargo packet to pay for; actual payment won't be made!.
 * @param count The number of packets to pay for.
 * @return The amount of money paid for the transfer.
 */
Money CargoPayment::PayTransfer(const CargoPacket *cp, uint count)
{
	Money profit = -cp->FeederShare(count) + GetTransportedGoodsIncome(
			count,
			/* pay transfer vehicle the difference between the payment for the journey from
			 * the source to the current point, and the sum of the previous transfer payments */
			DistanceManhattan(cp->SourceStationXY(), Station::Get(this->current_station)->xy),
			cp->DaysInTransit(),
			this->ct);

	profit = profit * _settings_game.economy.feeder_payment_share / 100;

	this->visual_transfer += profit; // accumulate transfer profits for whole vehicle
	return profit; // account for the (virtual) profit already made for the cargo packet
}

/**
 * Prepare the vehicle to be unloaded.
 * @param front_v the vehicle to be unloaded
 */
void PrepareUnload(Vehicle *front_v)
{
	Station *curr_station = Station::Get(front_v->last_station_visited);
	curr_station->loading_vehicles.push_back(front_v);

	/* At this moment loading cannot be finished */
	ClrBit(front_v->vehicle_flags, VF_LOADING_FINISHED);

	/* Start unloading at the first possible moment */
	front_v->load_unload_ticks = 1;

	assert(front_v->cargo_payment == nullptr);
	/* One CargoPayment per vehicle and the vehicle limit equals the
	 * limit in number of CargoPayments. Can't go wrong. */
	static_assert(CargoPaymentPool::MAX_SIZE == VehiclePool::MAX_SIZE);
	assert(CargoPayment::CanAllocateItem());
	front_v->cargo_payment = new CargoPayment(front_v);

	StationIDStack next_station = front_v->GetNextStoppingStation();
	if (front_v->orders.list == nullptr || (front_v->current_order.GetUnloadType() & OUFB_NO_UNLOAD) == 0) {
		Station *st = Station::Get(front_v->last_station_visited);
		for (Vehicle *v = front_v; v != nullptr; v = v->Next()) {
			const GoodsEntry *ge = &st->goods[v->cargo_type];
			if (v->cargo_cap > 0 && v->cargo.TotalCount() > 0) {
				v->cargo.Stage(
						HasBit(ge->status, GoodsEntry::GES_ACCEPTANCE),
						front_v->last_station_visited, next_station,
						front_v->current_order.GetUnloadType(), ge,
						front_v->cargo_payment);
				if (v->cargo.UnloadCount() > 0) SetBit(v->vehicle_flags, VF_CARGO_UNLOADING);
			}
		}
	}
}

/**
 * Gets the amount of cargo the given vehicle can load in the current tick.
 * This is only about loading speed. The free capacity is ignored.
 * @param v Vehicle to be queried.
 * @return Amount of cargo the vehicle can load at once.
 */
static uint GetLoadAmount(Vehicle *v)
{
	const Engine *e = v->GetEngine();
	uint load_amount = e->info.load_amount;

	/* The default loadamount for mail is 1/4 of the load amount for passengers */
	bool air_mail = v->type == VEH_AIRCRAFT && !Aircraft::From(v)->IsNormalAircraft();
	if (air_mail) load_amount = CeilDiv(load_amount, 4);

	if (_settings_game.order.gradual_loading) {
		uint16 cb_load_amount = CALLBACK_FAILED;
		if (e->GetGRF() != nullptr && e->GetGRF()->grf_version >= 8) {
			/* Use callback 36 */
			cb_load_amount = GetVehicleProperty(v, PROP_VEHICLE_LOAD_AMOUNT, CALLBACK_FAILED);
		} else if (HasBit(e->info.callback_mask, CBM_VEHICLE_LOAD_AMOUNT)) {
			/* Use callback 12 */
			cb_load_amount = GetVehicleCallback(CBID_VEHICLE_LOAD_AMOUNT, 0, 0, v->engine_type, v);
		}
		if (cb_load_amount != CALLBACK_FAILED) {
			if (e->GetGRF()->grf_version < 8) cb_load_amount = GB(cb_load_amount, 0, 8);
			if (cb_load_amount >= 0x100) {
				ErrorUnknownCallbackResult(e->GetGRFID(), CBID_VEHICLE_LOAD_AMOUNT, cb_load_amount);
			} else if (cb_load_amount != 0) {
				load_amount = cb_load_amount;
			}
		}
	}

	/* Scale load amount the same as capacity */
	if (HasBit(e->info.misc_flags, EF_NO_DEFAULT_CARGO_MULTIPLIER) && !air_mail) load_amount = CeilDiv(load_amount * CargoSpec::Get(v->cargo_type)->multiplier, 0x100);

	/* Zero load amount breaks a lot of things. */
	return std::max(1u, load_amount);
}

/**
 * Iterate the articulated parts of a vehicle, also considering the special cases of "normal"
 * aircraft and double headed trains. Apply an action to each vehicle and immediately return false
 * if that action does so. Otherwise return true.
 * @tparam Taction Class of action to be applied. Must implement bool operator()([const] Vehicle *).
 * @param v First articulated part.
 * @param action Instance of Taction.
 * @return false if any of the action invocations returned false, true otherwise.
 */
template<class Taction>
bool IterateVehicleParts(Vehicle *v, Taction action)
{
	for (Vehicle *w = v; w != nullptr;
			w = w->HasArticulatedPart() ? w->GetNextArticulatedPart() : nullptr) {
		if (!action(w)) return false;
		if (w->type == VEH_TRAIN) {
			Train *train = Train::From(w);
			if (train->IsMultiheaded() && !action(train->other_multiheaded_part)) return false;
		}
	}
	if (v->type == VEH_AIRCRAFT && Aircraft::From(v)->IsNormalAircraft()) return action(v->Next());
	return true;
}

/**
 * Action to check if a vehicle has no stored cargo.
 */
struct IsEmptyAction
{
	/**
	 * Checks if the vehicle has stored cargo.
	 * @param v Vehicle to be checked.
	 * @return true if v is either empty or has only reserved cargo, false otherwise.
	 */
	bool operator()(const Vehicle *v)
	{
		return v->cargo.StoredCount() == 0;
	}
};

/**
 * Refit preparation action.
 */
struct PrepareRefitAction
{
	CargoArray &consist_capleft; ///< Capacities left in the consist.
	CargoTypes &refit_mask;          ///< Bitmask of possible refit cargoes.

	/**
	 * Create a refit preparation action.
	 * @param consist_capleft Capacities left in consist, to be updated here.
	 * @param refit_mask Refit mask to be constructed from refit information of vehicles.
	 */
	PrepareRefitAction(CargoArray &consist_capleft, CargoTypes &refit_mask) :
		consist_capleft(consist_capleft), refit_mask(refit_mask) {}

	/**
	 * Prepares for refitting of a vehicle, subtracting its free capacity from consist_capleft and
	 * adding the cargoes it can refit to to the refit mask.
	 * @param v The vehicle to be refitted.
	 * @return true.
	 */
	bool operator()(const Vehicle *v)
	{
		this->consist_capleft[v->cargo_type] -= v->cargo_cap - v->cargo.ReservedCount();
		this->refit_mask |= EngInfo(v->engine_type)->refit_mask;
		return true;
	}
};

/**
 * Action for returning reserved cargo.
 */
struct ReturnCargoAction
{
	Station *st;        ///< Station to give the returned cargo to.
	StationID next_hop; ///< Next hop the cargo should be assigned to.

	/**
	 * Construct a cargo return action.
	 * @param st Station to give the returned cargo to.
	 * @param next_one Next hop the cargo should be assigned to.
	 */
	ReturnCargoAction(Station *st, StationID next_one) : st(st), next_hop(next_one) {}

	/**
	 * Return all reserved cargo from a vehicle.
	 * @param v Vehicle to return cargo from.
	 * @return true.
	 */
	bool operator()(Vehicle *v)
	{
		v->cargo.Return(UINT_MAX, &this->st->goods[v->cargo_type].cargo, this->next_hop);
		return true;
	}
};

/**
 * Action for finalizing a refit.
 */
struct FinalizeRefitAction
{
	CargoArray &consist_capleft;  ///< Capacities left in the consist.
	Station *st;                  ///< Station to reserve cargo from.
	StationIDStack &next_station; ///< Next hops to reserve cargo for.
	bool do_reserve;              ///< If the vehicle should reserve.

	/**
	 * Create a finalizing action.
	 * @param consist_capleft Capacities left in the consist.
	 * @param st Station to reserve cargo from.
	 * @param next_station Next hops to reserve cargo for.
	 * @param do_reserve If we should reserve cargo or just add up the capacities.
	 */
	FinalizeRefitAction(CargoArray &consist_capleft, Station *st, StationIDStack &next_station, bool do_reserve) :
		consist_capleft(consist_capleft), st(st), next_station(next_station), do_reserve(do_reserve) {}

	/**
	 * Reserve cargo from the station and update the remaining consist capacities with the
	 * vehicle's remaining free capacity.
	 * @param v Vehicle to be finalized.
	 * @return true.
	 */
	bool operator()(Vehicle *v)
	{
		if (this->do_reserve) {
			this->st->goods[v->cargo_type].cargo.Reserve(v->cargo_cap - v->cargo.RemainingCount(),
					&v->cargo, st->xy, this->next_station);
		}
		this->consist_capleft[v->cargo_type] += v->cargo_cap - v->cargo.RemainingCount();
		return true;
	}
};

/**
 * Refit a vehicle in a station.
 * @param v Vehicle to be refitted.
 * @param consist_capleft Added cargo capacities in the consist.
 * @param st Station the vehicle is loading at.
 * @param next_station Possible next stations the vehicle can travel to.
 * @param new_cid Target cargo for refit.
 */
static void HandleStationRefit(Vehicle *v, CargoArray &consist_capleft, Station *st, StationIDStack next_station, CargoID new_cid)
{
	Vehicle *v_start = v->GetFirstEnginePart();
	if (!IterateVehicleParts(v_start, IsEmptyAction())) return;

	Backup<CompanyID> cur_company(_current_company, v->owner, FILE_LINE);

	CargoTypes refit_mask = v->GetEngine()->info.refit_mask;

	/* Remove old capacity from consist capacity and collect refit mask. */
	IterateVehicleParts(v_start, PrepareRefitAction(consist_capleft, refit_mask));

	bool is_auto_refit = new_cid == CT_AUTO_REFIT;
	if (is_auto_refit) {
		/* Get a refittable cargo type with waiting cargo for next_station or INVALID_STATION. */
		CargoID cid;
		new_cid = v_start->cargo_type;
		FOR_EACH_SET_CARGO_ID(cid, refit_mask) {
			if (st->goods[cid].cargo.HasCargoFor(next_station)) {
				/* Try to find out if auto-refitting would succeed. In case the refit is allowed,
				 * the returned refit capacity will be greater than zero. */
				DoCommand(v_start->tile, v_start->index, cid | 1U << 24 | 0xFF << 8 | 1U << 16, DC_QUERY_COST, GetCmdRefitVeh(v_start)); // Auto-refit and only this vehicle including artic parts.
				/* Try to balance different loadable cargoes between parts of the consist, so that
				 * all of them can be loaded. Avoid a situation where all vehicles suddenly switch
				 * to the first loadable cargo for which there is only one packet. If the capacities
				 * are equal refit to the cargo of which most is available. This is important for
				 * consists of only a single vehicle as those will generally have a consist_capleft
				 * of 0 for all cargoes. */
				if (_returned_refit_capacity > 0 && (consist_capleft[cid] < consist_capleft[new_cid] ||
						(consist_capleft[cid] == consist_capleft[new_cid] &&
						st->goods[cid].cargo.AvailableCount() > st->goods[new_cid].cargo.AvailableCount()))) {
					new_cid = cid;
				}
			}
		}
	}

	/* Refit if given a valid cargo. */
	if (new_cid < NUM_CARGO && new_cid != v_start->cargo_type) {
		/* INVALID_STATION because in the DT_MANUAL case that's correct and in the DT_(A)SYMMETRIC
		 * cases the next hop of the vehicle doesn't really tell us anything if the cargo had been
		 * "via any station" before reserving. We rather produce some more "any station" cargo than
		 * misrouting it. */
		IterateVehicleParts(v_start, ReturnCargoAction(st, INVALID_STATION));
		CommandCost cost = DoCommand(v_start->tile, v_start->index, new_cid | 1U << 24 | 0xFF << 8 | 1U << 16, DC_EXEC, GetCmdRefitVeh(v_start)); // Auto-refit and only this vehicle including artic parts.
		if (cost.Succeeded()) v->First()->profit_this_year -= cost.GetCost() << 8;
	}

	/* Add new capacity to consist capacity and reserve cargo */
	IterateVehicleParts(v_start, FinalizeRefitAction(consist_capleft, st, next_station,
			is_auto_refit || (v->First()->current_order.GetLoadType() & OLFB_FULL_LOAD) != 0));

	cur_company.Restore();
}

/**
 * Test whether a vehicle can load cargo at a station even if exclusive transport rights are present.
 * @param st Station with cargo waiting to be loaded.
 * @param v Vehicle loading the cargo.
 * @return true when a vehicle can load the cargo.
 */
static bool MayLoadUnderExclusiveRights(const Station *st, const Vehicle *v)
{
	return st->owner != OWNER_NONE || st->town->exclusive_counter == 0 || st->town->exclusivity == v->owner;
}

struct ReserveCargoAction {
	Station *st;
	StationIDStack *next_station;

	ReserveCargoAction(Station *st, StationIDStack *next_station) :
		st(st), next_station(next_station) {}

	bool operator()(Vehicle *v)
	{
		if (v->cargo_cap > v->cargo.RemainingCount() && MayLoadUnderExclusiveRights(st, v)) {
			st->goods[v->cargo_type].cargo.Reserve(v->cargo_cap - v->cargo.RemainingCount(),
					&v->cargo, st->xy, *next_station);
		}

		return true;
	}

};

/**
 * Reserves cargo if the full load order and improved_load is set or if the
 * current order allows autorefit.
 * @param st Station where the consist is loading at the moment.
 * @param u Front of the loading vehicle consist.
 * @param consist_capleft If given, save free capacities after reserving there.
 * @param next_station Station(s) the vehicle will stop at next.
 */
static void ReserveConsist(Station *st, Vehicle *u, CargoArray *consist_capleft, StationIDStack *next_station)
{
	/* If there is a cargo payment not all vehicles of the consist have tried to do the refit.
	 * In that case, only reserve if it's a fixed refit and the equivalent of "articulated chain"
	 * a vehicle belongs to already has the right cargo. */
	bool must_reserve = !u->current_order.IsRefit() || u->cargo_payment == nullptr;
	for (Vehicle *v = u; v != nullptr; v = v->Next()) {
		assert(v->cargo_cap >= v->cargo.RemainingCount());

		/* Exclude various ways in which the vehicle might not be the head of an equivalent of
		 * "articulated chain". Also don't do the reservation if the vehicle is going to refit
		 * to a different cargo and hasn't tried to do so, yet. */
		if (!v->IsArticulatedPart() &&
				(v->type != VEH_TRAIN || !Train::From(v)->IsRearDualheaded()) &&
				(v->type != VEH_AIRCRAFT || Aircraft::From(v)->IsNormalAircraft()) &&
				(must_reserve || u->current_order.GetRefitCargo() == v->cargo_type)) {
			IterateVehicleParts(v, ReserveCargoAction(st, next_station));
		}
		if (consist_capleft == nullptr || v->cargo_cap == 0) continue;
		(*consist_capleft)[v->cargo_type] += v->cargo_cap - v->cargo.RemainingCount();
	}
}

/**
 * Update the vehicle's load_unload_ticks, the time it will wait until it tries to load or unload
 * again. Adjust for overhang of trains and set it at least to 1.
 * @param front The vehicle to be updated.
 * @param st The station the vehicle is loading at.
 * @param ticks The time it would normally wait, based on cargo loaded and unloaded.
 */
static void UpdateLoadUnloadTicks(Vehicle *front, const Station *st, int ticks)
{
	if (front->type == VEH_TRAIN) {
		/* Each platform tile is worth 2 rail vehicles. */
		int overhang = front->GetGroundVehicleCache()->cached_total_length - st->GetPlatformLength(front->tile) * TILE_SIZE;
		if (overhang > 0) {
			ticks <<= 1;
			ticks += (overhang * ticks) / 8;
		}
	}
	/* Always wait at least 1, otherwise we'll wait 'infinitively' long. */
	front->load_unload_ticks = std::max(1, ticks);
}

/**
 * Loads/unload the vehicle if possible.
 * @param front the vehicle to be (un)loaded
 */
static void LoadUnloadVehicle(Vehicle *front)
{
	assert(front->current_order.IsType(OT_LOADING));

	StationID last_visited = front->last_station_visited;
	Station *st = Station::Get(last_visited);

	StationIDStack next_station = front->GetNextStoppingStation();
	bool use_autorefit = front->current_order.IsRefit() && front->current_order.GetRefitCargo() == CT_AUTO_REFIT;
	CargoArray consist_capleft;
	if (_settings_game.order.improved_load && use_autorefit ?
			front->cargo_payment == nullptr : (front->current_order.GetLoadType() & OLFB_FULL_LOAD) != 0) {
		ReserveConsist(st, front,
				(use_autorefit && front->load_unload_ticks != 0) ? &consist_capleft : nullptr,
				&next_station);
	}

	/* We have not waited enough time till the next round of loading/unloading */
	if (front->load_unload_ticks != 0) return;

	if (front->type == VEH_TRAIN && (!IsTileType(front->tile, MP_STATION) || GetStationIndex(front->tile) != st->index)) {
		/* The train reversed in the station. Take the "easy" way
		 * out and let the train just leave as it always did. */
		SetBit(front->vehicle_flags, VF_LOADING_FINISHED);
		front->load_unload_ticks = 1;
		return;
	}

	int new_load_unload_ticks = 0;
	bool dirty_vehicle = false;
	bool dirty_station = false;

	bool completely_emptied = true;
	bool anything_unloaded  = false;
	bool anything_loaded    = false;
	CargoTypes full_load_amount = 0;
	CargoTypes cargo_not_full   = 0;
	CargoTypes cargo_full       = 0;
	CargoTypes reservation_left = 0;

	front->cur_speed = 0;

	CargoPayment *payment = front->cargo_payment;

	uint artic_part = 0; // Articulated part we are currently trying to load. (not counting parts without capacity)
	for (Vehicle *v = front; v != nullptr; v = v->Next()) {
		if (v == front || !v->Previous()->HasArticulatedPart()) artic_part = 0;
		if (v->cargo_cap == 0) continue;
		artic_part++;

		GoodsEntry *ge = &st->goods[v->cargo_type];

		if (HasBit(v->vehicle_flags, VF_CARGO_UNLOADING) && (front->current_order.GetUnloadType() & OUFB_NO_UNLOAD) == 0) {
			uint cargo_count = v->cargo.UnloadCount();
			uint amount_unloaded = _settings_game.order.gradual_loading ? std::min(cargo_count, GetLoadAmount(v)) : cargo_count;
			bool remaining = false; // Are there cargo entities in this vehicle that can still be unloaded here?

			assert(payment != nullptr);
			payment->SetCargo(v->cargo_type);

			if (!HasBit(ge->status, GoodsEntry::GES_ACCEPTANCE) && v->cargo.ActionCount(VehicleCargoList::MTA_DELIVER) > 0) {
				/* The station does not accept our goods anymore. */
				if (front->current_order.GetUnloadType() & (OUFB_TRANSFER | OUFB_UNLOAD)) {
					/* Transfer instead of delivering. */
					v->cargo.Reassign<VehicleCargoList::MTA_DELIVER, VehicleCargoList::MTA_TRANSFER>(
							v->cargo.ActionCount(VehicleCargoList::MTA_DELIVER), INVALID_STATION);
				} else {
					uint new_remaining = v->cargo.RemainingCount() + v->cargo.ActionCount(VehicleCargoList::MTA_DELIVER);
					if (v->cargo_cap < new_remaining) {
						/* Return some of the reserved cargo to not overload the vehicle. */
						v->cargo.Return(new_remaining - v->cargo_cap, &ge->cargo, INVALID_STATION);
					}

					/* Keep instead of delivering. This may lead to no cargo being unloaded, so ...*/
					v->cargo.Reassign<VehicleCargoList::MTA_DELIVER, VehicleCargoList::MTA_KEEP>(
							v->cargo.ActionCount(VehicleCargoList::MTA_DELIVER));

					/* ... say we unloaded something, otherwise we'll think we didn't unload
					 * something and we didn't load something, so we must be finished
					 * at this station. Setting the unloaded means that we will get a
					 * retry for loading in the next cycle. */
					anything_unloaded = true;
				}
			}

			if (v->cargo.ActionCount(VehicleCargoList::MTA_TRANSFER) > 0) {
				/* Mark the station dirty if we transfer, but not if we only deliver. */
				dirty_station = true;

				if (!ge->HasRating()) {
					/* Upon transferring cargo, make sure the station has a rating. Fake a pickup for the
					 * first unload to prevent the cargo from quickly decaying after the initial drop. */
					ge->time_since_pickup = 0;
					SetBit(ge->status, GoodsEntry::GES_RATING);
				}
			}

			amount_unloaded = v->cargo.Unload(amount_unloaded, &ge->cargo, payment);
			remaining = v->cargo.UnloadCount() > 0;
			if (amount_unloaded > 0) {
				dirty_vehicle = true;
				anything_unloaded = true;
				new_load_unload_ticks += amount_unloaded;

				/* Deliver goods to the station */
				st->time_since_unload = 0;
			}

			if (_settings_game.order.gradual_loading && remaining) {
				completely_emptied = false;
			} else {
				/* We have finished unloading (cargo count == 0) */
				ClrBit(v->vehicle_flags, VF_CARGO_UNLOADING);
			}

			continue;
		}

		/* Do not pick up goods when we have no-load set or loading is stopped. */
		if (front->current_order.GetLoadType() & OLFB_NO_LOAD || HasBit(front->vehicle_flags, VF_STOP_LOADING)) continue;

		/* This order has a refit, if this is the first vehicle part carrying cargo and the whole vehicle is empty, try refitting. */
		if (front->current_order.IsRefit() && artic_part == 1) {
			HandleStationRefit(v, consist_capleft, st, next_station, front->current_order.GetRefitCargo());
			ge = &st->goods[v->cargo_type];
		}

		/* As we're loading here the following link can carry the full capacity of the vehicle. */
		v->refit_cap = v->cargo_cap;

		/* update stats */
		int t;
		switch (front->type) {
			case VEH_TRAIN:
			case VEH_SHIP:
				t = front->vcache.cached_max_speed;
				break;

			case VEH_ROAD:
				t = front->vcache.cached_max_speed / 2;
				break;

			case VEH_AIRCRAFT:
				t = Aircraft::From(front)->GetSpeedOldUnits(); // Convert to old units.
				break;

			default: NOT_REACHED();
		}

		/* if last speed is 0, we treat that as if no vehicle has ever visited the station. */
		ge->last_speed = std::min(t, 255);
		ge->last_age = std::min(_cur_year - front->build_year, 255);

		assert(v->cargo_cap >= v->cargo.StoredCount());
		/* Capacity available for loading more cargo. */
		uint cap_left = v->cargo_cap - v->cargo.StoredCount();

		if (cap_left > 0) {
			/* If vehicle can load cargo, reset time_since_pickup. */
			ge->time_since_pickup = 0;

			/* If there's goods waiting at the station, and the vehicle
			 * has capacity for it, load it on the vehicle. */
			if ((v->cargo.ActionCount(VehicleCargoList::MTA_LOAD) > 0 || ge->cargo.AvailableCount() > 0) && MayLoadUnderExclusiveRights(st, v)) {
				if (v->cargo.StoredCount() == 0) TriggerVehicle(v, VEHICLE_TRIGGER_NEW_CARGO);
				if (_settings_game.order.gradual_loading) cap_left = std::min(cap_left, GetLoadAmount(v));

				uint loaded = ge->cargo.Load(cap_left, &v->cargo, st->xy, next_station);
				if (v->cargo.ActionCount(VehicleCargoList::MTA_LOAD) > 0) {
					/* Remember if there are reservations left so that we don't stop
					 * loading before they're loaded. */
					SetBit(reservation_left, v->cargo_type);
				}

				/* Store whether the maximum possible load amount was loaded or not.*/
				if (loaded == cap_left) {
					SetBit(full_load_amount, v->cargo_type);
				} else {
					ClrBit(full_load_amount, v->cargo_type);
				}

				/* TODO: Regarding this, when we do gradual loading, we
				 * should first unload all vehicles and then start
				 * loading them. Since this will cause
				 * VEHICLE_TRIGGER_EMPTY to be called at the time when
				 * the whole vehicle chain is really totally empty, the
				 * completely_emptied assignment can then be safely
				 * removed; that's how TTDPatch behaves too. --pasky */
				if (loaded > 0) {
					completely_emptied = false;
					anything_loaded = true;

					st->time_since_load = 0;
					st->last_vehicle_type = v->type;

					if (ge->cargo.TotalCount() == 0) {
						TriggerStationRandomisation(st, st->xy, SRT_CARGO_TAKEN, v->cargo_type);
						TriggerStationAnimation(st, st->xy, SAT_CARGO_TAKEN, v->cargo_type);
						AirportAnimationTrigger(st, AAT_STATION_CARGO_TAKEN, v->cargo_type);
					}

					new_load_unload_ticks += loaded;

					dirty_vehicle = dirty_station = true;
				}
			}
		}

		if (v->cargo.StoredCount() >= v->cargo_cap) {
			SetBit(cargo_full, v->cargo_type);
		} else {
			SetBit(cargo_not_full, v->cargo_type);
		}
	}

	if (anything_loaded || anything_unloaded) {
		if (front->type == VEH_TRAIN) {
			TriggerStationRandomisation(st, front->tile, SRT_TRAIN_LOADS);
			TriggerStationAnimation(st, front->tile, SAT_TRAIN_LOADS);
		}
	}

	/* Only set completely_emptied, if we just unloaded all remaining cargo */
	completely_emptied &= anything_unloaded;

	if (!anything_unloaded) delete payment;

	ClrBit(front->vehicle_flags, VF_STOP_LOADING);
	if (anything_loaded || anything_unloaded) {
		if (_settings_game.order.gradual_loading) {
			/* The time it takes to load one 'slice' of cargo or passengers depends
			 * on the vehicle type - the values here are those found in TTDPatch */
			const uint gradual_loading_wait_time[] = { 40, 20, 10, 20 };

			new_load_unload_ticks = gradual_loading_wait_time[front->type];
		}
		/* We loaded less cargo than possible for all cargo types and it's not full
		 * load and we're not supposed to wait any longer: stop loading. */
		if (!anything_unloaded && full_load_amount == 0 && reservation_left == 0 && !(front->current_order.GetLoadType() & OLFB_FULL_LOAD) &&
				front->current_order_time >= (uint)std::max(front->current_order.GetTimetabledWait() - front->lateness_counter, 0)) {
			SetBit(front->vehicle_flags, VF_STOP_LOADING);
		}

		UpdateLoadUnloadTicks(front, st, new_load_unload_ticks);
	} else {
		UpdateLoadUnloadTicks(front, st, 20); // We need the ticks for link refreshing.
		bool finished_loading = true;
		if (front->current_order.GetLoadType() & OLFB_FULL_LOAD) {
			if (front->current_order.GetLoadType() == OLF_FULL_LOAD_ANY) {
				/* if the aircraft carries passengers and is NOT full, then
				 * continue loading, no matter how much mail is in */
				if ((front->type == VEH_AIRCRAFT && IsCargoInClass(front->cargo_type, CC_PASSENGERS) && front->cargo_cap > front->cargo.StoredCount()) ||
						(cargo_not_full != 0 && (cargo_full & ~cargo_not_full) == 0)) { // There are still non-full cargoes
					finished_loading = false;
				}
			} else if (cargo_not_full != 0) {
				finished_loading = false;
			}

			/* Refresh next hop stats if we're full loading to make the links
			 * known to the distribution algorithm and allow cargo to be sent
			 * along them. Otherwise the vehicle could wait for cargo
			 * indefinitely if it hasn't visited the other links yet, or if the
			 * links die while it's loading. */
			if (!finished_loading) LinkRefresher::Run(front, true, true);
		}

		SB(front->vehicle_flags, VF_LOADING_FINISHED, 1, finished_loading);
	}

	/* Calculate the loading indicator fill percent and display
	 * In the Game Menu do not display indicators
	 * If _settings_client.gui.loading_indicators == 2, show indicators (bool can be promoted to int as 0 or 1 - results in 2 > 0,1 )
	 * if _settings_client.gui.loading_indicators == 1, _local_company must be the owner or must be a spectator to show ind., so 1 > 0
	 * if _settings_client.gui.loading_indicators == 0, do not display indicators ... 0 is never greater than anything
	 */
	if (_game_mode != GM_MENU && (_settings_client.gui.loading_indicators > (uint)(front->owner != _local_company && _local_company != COMPANY_SPECTATOR))) {
		StringID percent_up_down = STR_NULL;
		int percent = CalcPercentVehicleFilled(front, &percent_up_down);
		if (front->fill_percent_te_id == INVALID_TE_ID) {
			front->fill_percent_te_id = ShowFillingPercent(front->x_pos, front->y_pos, front->z_pos + 20, percent, percent_up_down);
		} else {
			UpdateFillingPercent(front->fill_percent_te_id, percent, percent_up_down);
		}
	}

	if (completely_emptied) {
		/* Make sure the vehicle is marked dirty, since we need to update the NewGRF
		 * properties such as weight, power and TE whenever the trigger runs. */
		dirty_vehicle = true;
		TriggerVehicle(front, VEHICLE_TRIGGER_EMPTY);
	}

	if (dirty_vehicle) {
		SetWindowDirty(GetWindowClassForVehicleType(front->type), front->owner);
		SetWindowDirty(WC_VEHICLE_DETAILS, front->index);
		front->MarkDirty();
	}
	if (dirty_station) {
		st->MarkTilesDirty(true);
		SetWindowDirty(WC_STATION_VIEW, last_visited);
		InvalidateWindowData(WC_STATION_LIST, last_visited);
	}
}

/**
 * Load/unload the vehicles in this station according to the order
 * they entered.
 * @param st the station to do the loading/unloading for
 */
void LoadUnloadStation(Station *st)
{
	/* No vehicle is here... */
	if (st->loading_vehicles.empty()) return;

	Vehicle *last_loading = nullptr;
	std::list<Vehicle *>::iterator iter;

	/* Check if anything will be loaded at all. Otherwise we don't need to reserve either. */
	for (iter = st->loading_vehicles.begin(); iter != st->loading_vehicles.end(); ++iter) {
		Vehicle *v = *iter;

		if ((v->vehstatus & (VS_STOPPED | VS_CRASHED))) continue;

		assert(v->load_unload_ticks != 0);
		if (--v->load_unload_ticks == 0) last_loading = v;
	}

	/* We only need to reserve and load/unload up to the last loading vehicle.
	 * Anything else will be forgotten anyway after returning from this function.
	 *
	 * Especially this means we do _not_ need to reserve cargo for a single
	 * consist in a station which is not allowed to load yet because its
	 * load_unload_ticks is still not 0.
	 */
	if (last_loading == nullptr) return;

	for (iter = st->loading_vehicles.begin(); iter != st->loading_vehicles.end(); ++iter) {
		Vehicle *v = *iter;
		if (!(v->vehstatus & (VS_STOPPED | VS_CRASHED))) LoadUnloadVehicle(v);
		if (v == last_loading) break;
	}

	/* Call the production machinery of industries */
	for (Industry *iid : _cargo_delivery_destinations) {
		TriggerIndustryProduction(iid);
	}
	_cargo_delivery_destinations.clear();
}

/**
 * Monthly update of the economic data (of the companies as well as economic fluctuations).
 */
void CompaniesMonthlyLoop()
{
	CompaniesGenStatistics();
	if (_settings_game.economy.inflation) {
		AddInflation();
		RecomputePrices();
	}
	CompaniesPayInterest();
	HandleEconomyFluctuations();
}

static void DoAcquireCompany(Company *c)
{
	CompanyID ci = c->index;

	CompanyNewsInformation *cni = MallocT<CompanyNewsInformation>(1);
	cni->FillData(c, Company::Get(_current_company));

	SetDParam(0, STR_NEWS_COMPANY_MERGER_TITLE);
	SetDParam(1, c->bankrupt_value == 0 ? STR_NEWS_MERGER_TAKEOVER_TITLE : STR_NEWS_COMPANY_MERGER_DESCRIPTION);
	SetDParamStr(2, cni->company_name);
	SetDParamStr(3, cni->other_company_name);
	SetDParam(4, c->bankrupt_value);
	AddCompanyNewsItem(STR_MESSAGE_NEWS_FORMAT, cni);
	AI::BroadcastNewEvent(new ScriptEventCompanyMerger(ci, _current_company));
	Game::NewEvent(new ScriptEventCompanyMerger(ci, _current_company));

	ChangeOwnershipOfCompanyItems(ci, _current_company);

	if (c->bankrupt_value == 0) {
		Company *owner = Company::Get(_current_company);
		owner->current_loan += c->current_loan;
	}

	if (c->is_ai) AI::Stop(c->index);

	DeleteCompanyWindows(ci);
	InvalidateWindowClassesData(WC_TRAINS_LIST, 0);
	InvalidateWindowClassesData(WC_SHIPS_LIST, 0);
	InvalidateWindowClassesData(WC_ROADVEH_LIST, 0);
	InvalidateWindowClassesData(WC_AIRCRAFT_LIST, 0);

	delete c;
}

extern int GetAmountOwnedBy(const Company *c, Owner owner);

/**
 * Acquire shares in an opposing company.
 * @param tile unused
 * @param flags type of operation
 * @param p1 company to buy the shares from
 * @param p2 unused
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdBuyShareInCompany(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	CommandCost cost(EXPENSES_OTHER);
	CompanyID target_company = (CompanyID)p1;
	Company *c = Company::GetIfValid(target_company);

	/* Check if buying shares is allowed (protection against modified clients)
	 * Cannot buy own shares */
	if (c == nullptr || !_settings_game.economy.allow_shares || _current_company == target_company) return CMD_ERROR;

	/* Protect new companies from hostile takeovers */
	if (_cur_year - c->inaugurated_year < _settings_game.economy.min_years_for_shares) return_cmd_error(STR_ERROR_PROTECTED);

	/* Those lines are here for network-protection (clients can be slow) */
	if (GetAmountOwnedBy(c, COMPANY_SPECTATOR) == 0) return cost;

	if (GetAmountOwnedBy(c, COMPANY_SPECTATOR) == 1) {
		if (!c->is_ai) return cost; //  We can not buy out a real company (temporarily). TODO: well, enable it obviously.

		if (GetAmountOwnedBy(c, _current_company) == 3 && !MayCompanyTakeOver(_current_company, target_company)) return_cmd_error(STR_ERROR_TOO_MANY_VEHICLES_IN_GAME);
	}


	cost.AddCost(CalculateCompanyValue(c) >> 2);
	if (flags & DC_EXEC) {
		Owner *b = c->share_owners;

		while (*b != COMPANY_SPECTATOR) b++; // share owners is guaranteed to contain at least one COMPANY_SPECTATOR
		*b = _current_company;

		for (int i = 0; c->share_owners[i] == _current_company;) {
			if (++i == 4) {
				c->bankrupt_value = 0;
				DoAcquireCompany(c);
				break;
			}
		}
		InvalidateWindowData(WC_COMPANY, target_company);
		CompanyAdminUpdate(c);
	}
	return cost;
}

/**
 * Sell shares in an opposing company.
 * @param tile unused
 * @param flags type of operation
 * @param p1 company to sell the shares from
 * @param p2 unused
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdSellShareInCompany(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	CompanyID target_company = (CompanyID)p1;
	Company *c = Company::GetIfValid(target_company);

	/* Cannot sell own shares */
	if (c == nullptr || _current_company == target_company) return CMD_ERROR;

	/* Check if selling shares is allowed (protection against modified clients).
	 * However, we must sell shares of companies being closed down. */
	if (!_settings_game.economy.allow_shares && !(flags & DC_BANKRUPT)) return CMD_ERROR;

	/* Those lines are here for network-protection (clients can be slow) */
	if (GetAmountOwnedBy(c, _current_company) == 0) return CommandCost();

	/* adjust it a little to make it less profitable to sell and buy */
	Money cost = CalculateCompanyValue(c) >> 2;
	cost = -(cost - (cost >> 7));

	if (flags & DC_EXEC) {
		Owner *b = c->share_owners;
		while (*b != _current_company) b++; // share owners is guaranteed to contain company
		*b = COMPANY_SPECTATOR;
		InvalidateWindowData(WC_COMPANY, target_company);
		CompanyAdminUpdate(c);
	}
	return CommandCost(EXPENSES_OTHER, cost);
}

/**
 * Buy up another company.
 * When a competing company is gone bankrupt you get the chance to purchase
 * that company.
 * @todo currently this only works for AI companies
 * @param tile unused
 * @param flags type of operation
 * @param p1 company to buy up
 * @param p2 unused
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdBuyCompany(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	CompanyID target_company = (CompanyID)p1;
	Company *c = Company::GetIfValid(target_company);
	if (c == nullptr) return CMD_ERROR;

	/* Disable takeovers when not asked */
	if (!HasBit(c->bankrupt_asked, _current_company)) return CMD_ERROR;

	/* Disable taking over the local company in singleplayer mode */
	if (!_networking && _local_company == c->index) return CMD_ERROR;

	/* Do not allow companies to take over themselves */
	if (target_company == _current_company) return CMD_ERROR;

	/* Disable taking over when not allowed. */
	if (!MayCompanyTakeOver(_current_company, target_company)) return CMD_ERROR;

	/* Get the cost here as the company is deleted in DoAcquireCompany. */
	CommandCost cost(EXPENSES_OTHER, c->bankrupt_value);

	if (flags & DC_EXEC) {
		DoAcquireCompany(c);
	}
	return cost;
}
