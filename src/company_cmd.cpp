/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file company_cmd.cpp Handling of companies. */

#include "stdafx.h"
#include "company_base.h"
#include "company_func.h"
#include "company_gui.h"
#include "town.h"
#include "news_func.h"
#include "cmd_helper.h"
#include "command_func.h"
#include "network/network.h"
#include "network/network_func.h"
#include "network/network_base.h"
#include "network/network_admin.h"
#include "ai/ai.hpp"
#include "company_manager_face.h"
#include "window_func.h"
#include "strings_func.h"
#include "date_func.h"
#include "sound_func.h"
#include "rail.h"
#include "core/pool_func.hpp"
#include "settings_func.h"
#include "vehicle_base.h"
#include "vehicle_func.h"
#include "smallmap_gui.h"
#include "game/game.hpp"
#include "goal_base.h"
#include "story_base.h"

#include "table/strings.h"

#include "safeguards.h"

void ClearEnginesHiddenFlagOfCompany(CompanyID cid);

CompanyID _local_company;   ///< Company controlled by the human player at this client. Can also be #COMPANY_SPECTATOR.
CompanyID _current_company; ///< Company currently doing an action.
Colours _company_colours[MAX_COMPANIES];  ///< NOSAVE: can be determined from company structs.
CompanyManagerFace _company_manager_face; ///< for company manager face storage in openttd.cfg
uint _next_competitor_start;              ///< the number of ticks before the next AI is started
uint _cur_company_tick_index;             ///< used to generate a name for one company that doesn't have a name yet per tick

CompanyPool _company_pool("Company"); ///< Pool of companies.
INSTANTIATE_POOL_METHODS(Company)

/**
 * Constructor.
 * @param name_1 Name of the company.
 * @param is_ai  A computer program is running for this company.
 */
Company::Company(uint16 name_1, bool is_ai)
{
	this->name_1 = name_1;
	this->location_of_HQ = INVALID_TILE;
	this->is_ai = is_ai;
	this->terraform_limit = _settings_game.construction.terraform_frame_burst << 16;
	this->clear_limit     = _settings_game.construction.clear_frame_burst << 16;
	this->tree_limit      = _settings_game.construction.tree_frame_burst << 16;

	for (uint j = 0; j < 4; j++) this->share_owners[j] = COMPANY_SPECTATOR;
	InvalidateWindowData(WC_PERFORMANCE_DETAIL, 0, INVALID_COMPANY);
}

/** Destructor. */
Company::~Company()
{
	if (CleaningPool()) return;

	DeleteCompanyWindows(this->index);
}

/**
 * Invalidating some stuff after removing item from the pool.
 * @param index index of deleted item
 */
void Company::PostDestructor(size_t index)
{
	InvalidateWindowData(WC_GRAPH_LEGEND, 0, (int)index);
	InvalidateWindowData(WC_PERFORMANCE_DETAIL, 0, (int)index);
	InvalidateWindowData(WC_COMPANY_LEAGUE, 0, 0);
	InvalidateWindowData(WC_LINKGRAPH_LEGEND, 0);
	/* If the currently shown error message has this company in it, then close it. */
	InvalidateWindowData(WC_ERRMSG, 0);
}

/**
 * Sets the local company and updates the settings that are set on a
 * per-company basis to reflect the core's state in the GUI.
 * @param new_company the new company
 * @pre Company::IsValidID(new_company) || new_company == COMPANY_SPECTATOR || new_company == OWNER_NONE
 */
void SetLocalCompany(CompanyID new_company)
{
	/* company could also be COMPANY_SPECTATOR or OWNER_NONE */
	assert(Company::IsValidID(new_company) || new_company == COMPANY_SPECTATOR || new_company == OWNER_NONE);

	/* If actually changing to another company, several windows need closing */
	bool switching_company = _local_company != new_company;

	/* Delete the chat window, if you were team chatting. */
	if (switching_company) InvalidateWindowData(WC_SEND_NETWORK_MSG, DESTTYPE_TEAM, _local_company);

	assert(IsLocalCompany());

	_current_company = _local_company = new_company;

	/* Delete any construction windows... */
	if (switching_company) DeleteConstructionWindows();

	/* ... and redraw the whole screen. */
	MarkWholeScreenDirty();
	InvalidateWindowClassesData(WC_SIGN_LIST, -1);
}

/**
 * Get the colour for DrawString-subroutines which matches the colour of the company.
 * @param company Company to get the colour of.
 * @return Colour of \a company.
 */
TextColour GetDrawStringCompanyColour(CompanyID company)
{
	if (!Company::IsValidID(company)) return (TextColour)_colour_gradient[COLOUR_WHITE][4] | TC_IS_PALETTE_COLOUR;
	return (TextColour)_colour_gradient[_company_colours[company]][4] | TC_IS_PALETTE_COLOUR;
}

/**
 * Draw the icon of a company.
 * @param c Company that needs its icon drawn.
 * @param x Horizontal coordinate of the icon.
 * @param y Vertical coordinate of the icon.
 */
void DrawCompanyIcon(CompanyID c, int x, int y)
{
	DrawSprite(SPR_COMPANY_ICON, COMPANY_SPRITE_COLOUR(c), x, y);
}

/**
 * Checks whether a company manager's face is a valid encoding.
 * Unused bits are not enforced to be 0.
 * @param cmf the fact to check
 * @return true if and only if the face is valid
 */
static bool IsValidCompanyManagerFace(CompanyManagerFace cmf)
{
	if (!AreCompanyManagerFaceBitsValid(cmf, CMFV_GEN_ETHN, GE_WM)) return false;

	GenderEthnicity ge   = (GenderEthnicity)GetCompanyManagerFaceBits(cmf, CMFV_GEN_ETHN, GE_WM);
	bool has_moustache   = !HasBit(ge, GENDER_FEMALE) && GetCompanyManagerFaceBits(cmf, CMFV_HAS_MOUSTACHE,   ge) != 0;
	bool has_tie_earring = !HasBit(ge, GENDER_FEMALE) || GetCompanyManagerFaceBits(cmf, CMFV_HAS_TIE_EARRING, ge) != 0;
	bool has_glasses     = GetCompanyManagerFaceBits(cmf, CMFV_HAS_GLASSES, ge) != 0;

	if (!AreCompanyManagerFaceBitsValid(cmf, CMFV_EYE_COLOUR, ge)) return false;
	for (CompanyManagerFaceVariable cmfv = CMFV_CHEEKS; cmfv < CMFV_END; cmfv++) {
		switch (cmfv) {
			case CMFV_MOUSTACHE:   if (!has_moustache)   continue; break;
			case CMFV_LIPS:
			case CMFV_NOSE:        if (has_moustache)    continue; break;
			case CMFV_TIE_EARRING: if (!has_tie_earring) continue; break;
			case CMFV_GLASSES:     if (!has_glasses)     continue; break;
			default: break;
		}
		if (!AreCompanyManagerFaceBitsValid(cmf, cmfv, ge)) return false;
	}

	return true;
}

/**
 * Refresh all windows owned by a company.
 * @param company Company that changed, and needs its windows refreshed.
 */
void InvalidateCompanyWindows(const Company *company)
{
	CompanyID cid = company->index;

	if (cid == _local_company) SetWindowDirty(WC_STATUS_BAR, 0);
	SetWindowDirty(WC_FINANCES, cid);
}

/**
 * Verify whether the company can pay the bill.
 * @param[in,out] cost Money to pay, is changed to an error if the company does not have enough money.
 * @return Function returns \c true if the company has enough money, else it returns \c false.
 */
bool CheckCompanyHasMoney(CommandCost &cost)
{
	if (cost.GetCost() > 0) {
		const Company *c = Company::GetIfValid(_current_company);
		if (c != nullptr && cost.GetCost() > c->money) {
			SetDParam(0, cost.GetCost());
			cost.MakeError(STR_ERROR_NOT_ENOUGH_CASH_REQUIRES_CURRENCY);
			return false;
		}
	}
	return true;
}

/**
 * Deduct costs of a command from the money of a company.
 * @param c Company to pay the bill.
 * @param cost Money to pay.
 */
static void SubtractMoneyFromAnyCompany(Company *c, const CommandCost &cost)
{
	if (cost.GetCost() == 0) return;
	assert(cost.GetExpensesType() != INVALID_EXPENSES);

	c->money -= cost.GetCost();
	c->yearly_expenses[0][cost.GetExpensesType()] += cost.GetCost();

	if (HasBit(1 << EXPENSES_TRAIN_INC    |
	           1 << EXPENSES_ROADVEH_INC  |
	           1 << EXPENSES_AIRCRAFT_INC |
	           1 << EXPENSES_SHIP_INC, cost.GetExpensesType())) {
		c->cur_economy.income -= cost.GetCost();
	} else if (HasBit(1 << EXPENSES_TRAIN_RUN    |
	                  1 << EXPENSES_ROADVEH_RUN  |
	                  1 << EXPENSES_AIRCRAFT_RUN |
	                  1 << EXPENSES_SHIP_RUN     |
	                  1 << EXPENSES_PROPERTY     |
	                  1 << EXPENSES_LOAN_INT, cost.GetExpensesType())) {
		c->cur_economy.expenses -= cost.GetCost();
	}

	InvalidateCompanyWindows(c);
}

/**
 * Subtract money from the #_current_company, if the company is valid.
 * @param cost Money to pay.
 */
void SubtractMoneyFromCompany(const CommandCost &cost)
{
	Company *c = Company::GetIfValid(_current_company);
	if (c != nullptr) SubtractMoneyFromAnyCompany(c, cost);
}

/**
 * Subtract money from a company, including the money fraction.
 * @param company Company paying the bill.
 * @param cst     Cost of a command.
 */
void SubtractMoneyFromCompanyFract(CompanyID company, const CommandCost &cst)
{
	Company *c = Company::Get(company);
	byte m = c->money_fraction;
	Money cost = cst.GetCost();

	c->money_fraction = m - (byte)cost;
	cost >>= 8;
	if (c->money_fraction > m) cost++;
	if (cost != 0) SubtractMoneyFromAnyCompany(c, CommandCost(cst.GetExpensesType(), cost));
}

/** Update the landscaping limits per company. */
void UpdateLandscapingLimits()
{
	for (Company *c : Company::Iterate()) {
		c->terraform_limit = min(c->terraform_limit + _settings_game.construction.terraform_per_64k_frames, (uint32)_settings_game.construction.terraform_frame_burst << 16);
		c->clear_limit     = min(c->clear_limit     + _settings_game.construction.clear_per_64k_frames,     (uint32)_settings_game.construction.clear_frame_burst << 16);
		c->tree_limit      = min(c->tree_limit      + _settings_game.construction.tree_per_64k_frames,      (uint32)_settings_game.construction.tree_frame_burst << 16);
	}
}

/**
 * Set the right DParams to get the name of an owner.
 * @param owner the owner to get the name of.
 * @param tile  optional tile to get the right town.
 * @pre if tile == 0, then owner can't be OWNER_TOWN.
 */
void GetNameOfOwner(Owner owner, TileIndex tile)
{
	SetDParam(2, owner);

	if (owner != OWNER_TOWN) {
		if (!Company::IsValidID(owner)) {
			SetDParam(0, STR_COMPANY_SOMEONE);
		} else {
			SetDParam(0, STR_COMPANY_NAME);
			SetDParam(1, owner);
		}
	} else {
		assert(tile != 0);
		const Town *t = ClosestTownFromTile(tile, UINT_MAX);

		SetDParam(0, STR_TOWN_NAME);
		SetDParam(1, t->index);
	}
}


/**
 * Check whether the current owner owns something.
 * If that isn't the case an appropriate error will be given.
 * @param owner the owner of the thing to check.
 * @param tile  optional tile to get the right town.
 * @pre if tile == 0 then the owner can't be OWNER_TOWN.
 * @return A succeeded command iff it's owned by the current company, else a failed command.
 */
CommandCost CheckOwnership(Owner owner, TileIndex tile)
{
	assert(owner < OWNER_END);
	assert(owner != OWNER_TOWN || tile != 0);

	if (owner == _current_company) return CommandCost();

	GetNameOfOwner(owner, tile);
	return_cmd_error(STR_ERROR_OWNED_BY);
}

/**
 * Check whether the current owner owns the stuff on
 * the given tile.  If that isn't the case an
 * appropriate error will be given.
 * @param tile the tile to check.
 * @return A succeeded command iff it's owned by the current company, else a failed command.
 */
CommandCost CheckTileOwnership(TileIndex tile)
{
	Owner owner = GetTileOwner(tile);

	assert(owner < OWNER_END);

	if (owner == _current_company) return CommandCost();

	/* no need to get the name of the owner unless we're the local company (saves some time) */
	if (IsLocalCompany()) GetNameOfOwner(owner, tile);
	return_cmd_error(STR_ERROR_OWNED_BY);
}

/**
 * Generate the name of a company from the last build coordinate.
 * @param c Company to give a name.
 */
static void GenerateCompanyName(Company *c)
{
	/* Reserve space for extra unicode character. We need to do this to be able
	 * to detect too long company name. */
	char buffer[(MAX_LENGTH_COMPANY_NAME_CHARS + 1) * MAX_CHAR_LENGTH];

	if (c->name_1 != STR_SV_UNNAMED) return;
	if (c->last_build_coordinate == 0) return;

	Town *t = ClosestTownFromTile(c->last_build_coordinate, UINT_MAX);

	StringID str;
	uint32 strp;
	if (t->name.empty() && IsInsideMM(t->townnametype, SPECSTR_TOWNNAME_START, SPECSTR_TOWNNAME_LAST + 1)) {
		str = t->townnametype - SPECSTR_TOWNNAME_START + SPECSTR_COMPANY_NAME_START;
		strp = t->townnameparts;

verify_name:;
		/* No companies must have this name already */
		for (const Company *cc : Company::Iterate()) {
			if (cc->name_1 == str && cc->name_2 == strp) goto bad_town_name;
		}

		GetString(buffer, str, lastof(buffer));
		if (Utf8StringLength(buffer) >= MAX_LENGTH_COMPANY_NAME_CHARS) goto bad_town_name;

set_name:;
		c->name_1 = str;
		c->name_2 = strp;

		MarkWholeScreenDirty();

		if (c->is_ai) {
			CompanyNewsInformation *cni = MallocT<CompanyNewsInformation>(1);
			cni->FillData(c);
			SetDParam(0, STR_NEWS_COMPANY_LAUNCH_TITLE);
			SetDParam(1, STR_NEWS_COMPANY_LAUNCH_DESCRIPTION);
			SetDParamStr(2, cni->company_name);
			SetDParam(3, t->index);
			AddNewsItem(STR_MESSAGE_NEWS_FORMAT, NT_COMPANY_INFO, NF_COMPANY, NR_TILE, c->last_build_coordinate, NR_NONE, UINT32_MAX, cni);
		}
		return;
	}
bad_town_name:;

	if (c->president_name_1 == SPECSTR_PRESIDENT_NAME) {
		str = SPECSTR_ANDCO_NAME;
		strp = c->president_name_2;
		goto set_name;
	} else {
		str = SPECSTR_ANDCO_NAME;
		strp = Random();
		goto verify_name;
	}
}

/** Sorting weights for the company colours. */
static const byte _colour_sort[COLOUR_END] = {2, 2, 3, 2, 3, 2, 3, 2, 3, 2, 2, 2, 3, 1, 1, 1};
/** Similar colours, so we can try to prevent same coloured companies. */
static const Colours _similar_colour[COLOUR_END][2] = {
	{ COLOUR_BLUE,       COLOUR_LIGHT_BLUE }, // COLOUR_DARK_BLUE
	{ COLOUR_GREEN,      COLOUR_DARK_GREEN }, // COLOUR_PALE_GREEN
	{ INVALID_COLOUR,    INVALID_COLOUR    }, // COLOUR_PINK
	{ COLOUR_ORANGE,     INVALID_COLOUR    }, // COLOUR_YELLOW
	{ INVALID_COLOUR,    INVALID_COLOUR    }, // COLOUR_RED
	{ COLOUR_DARK_BLUE,  COLOUR_BLUE       }, // COLOUR_LIGHT_BLUE
	{ COLOUR_PALE_GREEN, COLOUR_DARK_GREEN }, // COLOUR_GREEN
	{ COLOUR_PALE_GREEN, COLOUR_GREEN      }, // COLOUR_DARK_GREEN
	{ COLOUR_DARK_BLUE,  COLOUR_LIGHT_BLUE }, // COLOUR_BLUE
	{ COLOUR_BROWN,      COLOUR_ORANGE     }, // COLOUR_CREAM
	{ COLOUR_PURPLE,     INVALID_COLOUR    }, // COLOUR_MAUVE
	{ COLOUR_MAUVE,      INVALID_COLOUR    }, // COLOUR_PURPLE
	{ COLOUR_YELLOW,     COLOUR_CREAM      }, // COLOUR_ORANGE
	{ COLOUR_CREAM,      INVALID_COLOUR    }, // COLOUR_BROWN
	{ COLOUR_WHITE,      INVALID_COLOUR    }, // COLOUR_GREY
	{ COLOUR_GREY,       INVALID_COLOUR    }, // COLOUR_WHITE
};

/**
 * Generate a company colour.
 * @return Generated company colour.
 */
static Colours GenerateCompanyColour()
{
	Colours colours[COLOUR_END];

	/* Initialize array */
	for (uint i = 0; i < COLOUR_END; i++) colours[i] = (Colours)i;

	/* And randomize it */
	for (uint i = 0; i < 100; i++) {
		uint r = Random();
		Swap(colours[GB(r, 0, 4)], colours[GB(r, 4, 4)]);
	}

	/* Bubble sort it according to the values in table 1 */
	for (uint i = 0; i < COLOUR_END; i++) {
		for (uint j = 1; j < COLOUR_END; j++) {
			if (_colour_sort[colours[j - 1]] < _colour_sort[colours[j]]) {
				Swap(colours[j - 1], colours[j]);
			}
		}
	}

	/* Move the colours that look similar to each company's colour to the side */
	for (const Company *c : Company::Iterate()) {
		Colours pcolour = (Colours)c->colour;

		for (uint i = 0; i < COLOUR_END; i++) {
			if (colours[i] == pcolour) {
				colours[i] = INVALID_COLOUR;
				break;
			}
		}

		for (uint j = 0; j < 2; j++) {
			Colours similar = _similar_colour[pcolour][j];
			if (similar == INVALID_COLOUR) break;

			for (uint i = 1; i < COLOUR_END; i++) {
				if (colours[i - 1] == similar) Swap(colours[i - 1], colours[i]);
			}
		}
	}

	/* Return the first available colour */
	for (uint i = 0; i < COLOUR_END; i++) {
		if (colours[i] != INVALID_COLOUR) return colours[i];
	}

	NOT_REACHED();
}

/**
 * Generate a random president name of a company.
 * @param c Company that needs a new president name.
 */
static void GeneratePresidentName(Company *c)
{
	for (;;) {
restart:;
		c->president_name_2 = Random();
		c->president_name_1 = SPECSTR_PRESIDENT_NAME;

		/* Reserve space for extra unicode character. We need to do this to be able
		 * to detect too long president name. */
		char buffer[(MAX_LENGTH_PRESIDENT_NAME_CHARS + 1) * MAX_CHAR_LENGTH];
		SetDParam(0, c->index);
		GetString(buffer, STR_PRESIDENT_NAME, lastof(buffer));
		if (Utf8StringLength(buffer) >= MAX_LENGTH_PRESIDENT_NAME_CHARS) continue;

		for (const Company *cc : Company::Iterate()) {
			if (c != cc) {
				/* Reserve extra space so even overlength president names can be compared. */
				char buffer2[(MAX_LENGTH_PRESIDENT_NAME_CHARS + 1) * MAX_CHAR_LENGTH];
				SetDParam(0, cc->index);
				GetString(buffer2, STR_PRESIDENT_NAME, lastof(buffer2));
				if (strcmp(buffer2, buffer) == 0) goto restart;
			}
		}
		return;
	}
}

/**
 * Reset the livery schemes to the company's primary colour.
 * This is used on loading games without livery information and on new company start up.
 * @param c Company to reset.
 */
void ResetCompanyLivery(Company *c)
{
	for (LiveryScheme scheme = LS_BEGIN; scheme < LS_END; scheme++) {
		c->livery[scheme].in_use  = 0;
		c->livery[scheme].colour1 = c->colour;
		c->livery[scheme].colour2 = c->colour;
	}

	for (Group *g : Group::Iterate()) {
		if (g->owner == c->index) {
			g->livery.in_use  = 0;
			g->livery.colour1 = c->colour;
			g->livery.colour2 = c->colour;
		}
	}
}

/**
 * Create a new company and sets all company variables default values
 *
 * @param is_ai is an AI company?
 * @param company CompanyID to use for the new company
 * @return the company struct
 */
Company *DoStartupNewCompany(bool is_ai, CompanyID company = INVALID_COMPANY)
{
	if (!Company::CanAllocateItem()) return nullptr;

	/* we have to generate colour before this company is valid */
	Colours colour = GenerateCompanyColour();

	Company *c;
	if (company == INVALID_COMPANY) {
		c = new Company(STR_SV_UNNAMED, is_ai);
	} else {
		if (Company::IsValidID(company)) return nullptr;
		c = new (company) Company(STR_SV_UNNAMED, is_ai);
	}

	c->colour = colour;

	ResetCompanyLivery(c);
	_company_colours[c->index] = (Colours)c->colour;

	c->money = c->current_loan = (100000ll * _economy.inflation_prices >> 16) / 50000 * 50000;

	c->share_owners[0] = c->share_owners[1] = c->share_owners[2] = c->share_owners[3] = INVALID_OWNER;

	c->avail_railtypes = GetCompanyRailtypes(c->index);
	c->avail_roadtypes = GetCompanyRoadTypes(c->index);
	c->inaugurated_year = _cur_year;
	RandomCompanyManagerFaceBits(c->face, (GenderEthnicity)Random(), false, false); // create a random company manager face

	SetDefaultCompanySettings(c->index);
	ClearEnginesHiddenFlagOfCompany(c->index);

	GeneratePresidentName(c);

	SetWindowDirty(WC_GRAPH_LEGEND, 0);
	SetWindowClassesDirty(WC_CLIENT_LIST_POPUP);
	SetWindowDirty(WC_CLIENT_LIST, 0);
	InvalidateWindowData(WC_LINKGRAPH_LEGEND, 0);
	BuildOwnerLegend();
	InvalidateWindowData(WC_SMALLMAP, 0, 1);

	if (is_ai && (!_networking || _network_server)) AI::StartNew(c->index);

	AI::BroadcastNewEvent(new ScriptEventCompanyNew(c->index), c->index);
	Game::NewEvent(new ScriptEventCompanyNew(c->index));

	return c;
}

/** Start the next competitor now. */
void StartupCompanies()
{
	_next_competitor_start = 0;
}

/** Start a new competitor company if possible. */
static bool MaybeStartNewCompany()
{
	if (_networking && Company::GetNumItems() >= _settings_client.network.max_companies) return false;

	/* count number of competitors */
	uint n = 0;
	for (const Company *c : Company::Iterate()) {
		if (c->is_ai) n++;
	}

	if (n < (uint)_settings_game.difficulty.max_no_competitors) {
		/* Send a command to all clients to start up a new AI.
		 * Works fine for Multiplayer and Singleplayer */
		return DoCommandP(0, CCA_NEW_AI | INVALID_COMPANY << 16, 0, CMD_COMPANY_CTRL);
	}

	return false;
}

/** Initialize the pool of companies. */
void InitializeCompanies()
{
	_cur_company_tick_index = 0;
}

/**
 * May company \a cbig buy company \a csmall?
 * @param cbig   Company buying \a csmall.
 * @param csmall Company getting bought.
 * @return Return \c true if it is allowed.
 */
bool MayCompanyTakeOver(CompanyID cbig, CompanyID csmall)
{
	const Company *c1 = Company::Get(cbig);
	const Company *c2 = Company::Get(csmall);

	/* Do the combined vehicle counts stay within the limits? */
	return c1->group_all[VEH_TRAIN].num_vehicle + c2->group_all[VEH_TRAIN].num_vehicle <= _settings_game.vehicle.max_trains &&
		c1->group_all[VEH_ROAD].num_vehicle     + c2->group_all[VEH_ROAD].num_vehicle     <= _settings_game.vehicle.max_roadveh &&
		c1->group_all[VEH_SHIP].num_vehicle     + c2->group_all[VEH_SHIP].num_vehicle     <= _settings_game.vehicle.max_ships &&
		c1->group_all[VEH_AIRCRAFT].num_vehicle + c2->group_all[VEH_AIRCRAFT].num_vehicle <= _settings_game.vehicle.max_aircraft;
}

/**
 * Handle the bankruptcy take over of a company.
 * Companies going bankrupt will ask the other companies in order of their
 * performance rating, so better performing companies get the 'do you want to
 * merge with Y' question earlier. The question will then stay till either the
 * company has gone bankrupt or got merged with a company.
 *
 * @param c the company that is going bankrupt.
 */
static void HandleBankruptcyTakeover(Company *c)
{
	/* Amount of time out for each company to take over a company;
	 * Timeout is a quarter (3 months of 30 days) divided over the
	 * number of companies. The minimum number of days in a quarter
	 * is 90: 31 in January, 28 in February and 31 in March.
	 * Note that the company going bankrupt can't buy itself. */
	static const int TAKE_OVER_TIMEOUT = 3 * 30 * DAY_TICKS / (MAX_COMPANIES - 1);

	assert(c->bankrupt_asked != 0);

	/* We're currently asking some company to buy 'us' */
	if (c->bankrupt_timeout != 0) {
		c->bankrupt_timeout -= MAX_COMPANIES;
		if (c->bankrupt_timeout > 0) return;
		c->bankrupt_timeout = 0;

		return;
	}

	/* Did we ask everyone for bankruptcy? If so, bail out. */
	if (c->bankrupt_asked == MAX_UVALUE(CompanyMask)) return;

	Company *best = nullptr;
	int32 best_performance = -1;

	/* Ask the company with the highest performance history first */
	for (Company *c2 : Company::Iterate()) {
		if (c2->bankrupt_asked == 0 && // Don't ask companies going bankrupt themselves
				!HasBit(c->bankrupt_asked, c2->index) &&
				best_performance < c2->old_economy[1].performance_history &&
				MayCompanyTakeOver(c2->index, c->index)) {
			best_performance = c2->old_economy[1].performance_history;
			best = c2;
		}
	}

	/* Asked all companies? */
	if (best_performance == -1) {
		c->bankrupt_asked = MAX_UVALUE(CompanyMask);
		return;
	}

	SetBit(c->bankrupt_asked, best->index);

	c->bankrupt_timeout = TAKE_OVER_TIMEOUT;
	if (best->is_ai) {
		AI::NewEvent(best->index, new ScriptEventCompanyAskMerger(c->index, ClampToI32(c->bankrupt_value)));
	} else if (IsInteractiveCompany(best->index)) {
		ShowBuyCompanyDialog(c->index);
	}
}

/** Called every tick for updating some company info. */
void OnTick_Companies()
{
	if (_game_mode == GM_EDITOR) return;

	Company *c = Company::GetIfValid(_cur_company_tick_index);
	if (c != nullptr) {
		if (c->name_1 != 0) GenerateCompanyName(c);
		if (c->bankrupt_asked != 0) HandleBankruptcyTakeover(c);
	}

	if (_next_competitor_start == 0) {
		/* AI::GetStartNextTime() can return 0. */
		_next_competitor_start = max(1, AI::GetStartNextTime() * DAY_TICKS);
	}

	if (_game_mode != GM_MENU && AI::CanStartNew() && --_next_competitor_start == 0) {
		/* Allow multiple AIs to possibly start in the same tick. */
		do {
			if (!MaybeStartNewCompany()) break;

			/* In networking mode, we can only send a command to start but it
			 * didn't execute yet, so we cannot loop. */
			if (_networking) break;
		} while (AI::GetStartNextTime() == 0);
	}

	_cur_company_tick_index = (_cur_company_tick_index + 1) % MAX_COMPANIES;
}

/**
 * A year has passed, update the economic data of all companies, and perhaps show the
 * financial overview window of the local company.
 */
void CompaniesYearlyLoop()
{
	/* Copy statistics */
	for (Company *c : Company::Iterate()) {
		memmove(&c->yearly_expenses[1], &c->yearly_expenses[0], sizeof(c->yearly_expenses) - sizeof(c->yearly_expenses[0]));
		memset(&c->yearly_expenses[0], 0, sizeof(c->yearly_expenses[0]));
		SetWindowDirty(WC_FINANCES, c->index);
	}

	if (_settings_client.gui.show_finances && _local_company != COMPANY_SPECTATOR) {
		ShowCompanyFinances(_local_company);
		Company *c = Company::Get(_local_company);
		if (c->num_valid_stat_ent > 5 && c->old_economy[0].performance_history < c->old_economy[4].performance_history) {
			if (_settings_client.sound.new_year) SndPlayFx(SND_01_BAD_YEAR);
		} else {
			if (_settings_client.sound.new_year) SndPlayFx(SND_00_GOOD_YEAR);
		}
	}
}

/**
 * Fill the CompanyNewsInformation struct with the required data.
 * @param c the current company.
 * @param other the other company (use \c nullptr if not relevant).
 */
void CompanyNewsInformation::FillData(const Company *c, const Company *other)
{
	SetDParam(0, c->index);
	GetString(this->company_name, STR_COMPANY_NAME, lastof(this->company_name));

	if (other == nullptr) {
		*this->other_company_name = '\0';
	} else {
		SetDParam(0, other->index);
		GetString(this->other_company_name, STR_COMPANY_NAME, lastof(this->other_company_name));
		c = other;
	}

	SetDParam(0, c->index);
	GetString(this->president_name, STR_PRESIDENT_NAME_MANAGER, lastof(this->president_name));

	this->colour = c->colour;
	this->face = c->face;

}

/**
 * Called whenever company related information changes in order to notify admins.
 * @param company The company data changed of.
 */
void CompanyAdminUpdate(const Company *company)
{
	if (_network_server) NetworkAdminCompanyUpdate(company);
}

/**
 * Called whenever a company is removed in order to notify admins.
 * @param company_id The company that was removed.
 * @param reason     The reason the company was removed.
 */
void CompanyAdminRemove(CompanyID company_id, CompanyRemoveReason reason)
{
	if (_network_server) NetworkAdminCompanyRemove(company_id, (AdminCompanyRemoveReason)reason);
}

/**
 * Control the companies: add, delete, etc.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 various functionality
 * - bits 0..15: CompanyCtrlAction
 * - bits 16..23: CompanyID
 * - bits 24..31: CompanyRemoveReason (with CCA_DELETE)
 * @param p2 ClientID
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdCompanyCtrl(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	InvalidateWindowData(WC_COMPANY_LEAGUE, 0, 0);
	CompanyID company_id = (CompanyID)GB(p1, 16, 8);

	switch ((CompanyCtrlAction)GB(p1, 0, 16)) {
		case CCA_NEW: { // Create a new company
			/* This command is only executed in a multiplayer game */
			if (!_networking) return CMD_ERROR;

			/* Has the network client a correct ClientIndex? */
			if (!(flags & DC_EXEC)) return CommandCost();

			ClientID client_id = (ClientID)p2;
			NetworkClientInfo *ci = NetworkClientInfo::GetByClientID(client_id);
#ifndef DEBUG_DUMP_COMMANDS
			/* When replaying the client ID is not a valid client; there
			 * are actually no clients at all. However, the company has to
			 * be created, otherwise we cannot rerun the game properly.
			 * So only allow a nullptr client info in that case. */
			if (ci == nullptr) return CommandCost();
#endif /* NOT DEBUG_DUMP_COMMANDS */

			/* Delete multiplayer progress bar */
			DeleteWindowById(WC_NETWORK_STATUS_WINDOW, WN_NETWORK_STATUS_WINDOW_JOIN);

			Company *c = DoStartupNewCompany(false);

			/* A new company could not be created, revert to being a spectator */
			if (c == nullptr) {
				if (_network_server) {
					ci->client_playas = COMPANY_SPECTATOR;
					NetworkUpdateClientInfo(ci->client_id);
				}
				break;
			}

			/* This is the client (or non-dedicated server) who wants a new company */
			if (client_id == _network_own_client_id) {
				assert(_local_company == COMPANY_SPECTATOR);
				SetLocalCompany(c->index);
				if (!StrEmpty(_settings_client.network.default_company_pass)) {
					NetworkChangeCompanyPassword(_local_company, _settings_client.network.default_company_pass);
				}
				if (_settings_client.network.company_autoprotect) {
					NetworkChangeCompanyPubkey(_local_company, true);
				}

				/* Now that we have a new company, broadcast our company settings to
				 * all clients so everything is in sync */
				SyncCompanySettings();

				MarkWholeScreenDirty();
			}

			NetworkServerNewCompany(c, ci);
			break;
		}

		case CCA_NEW_AI: { // Make a new AI company
			if (!(flags & DC_EXEC)) return CommandCost();

			if (company_id != INVALID_COMPANY && (company_id >= MAX_COMPANIES || Company::IsValidID(company_id))) return CMD_ERROR;
			Company *c = DoStartupNewCompany(true, company_id);
			if (c != nullptr) NetworkServerNewCompany(c, nullptr);
			break;
		}

		case CCA_DELETE: { // Delete a company
			CompanyRemoveReason reason = (CompanyRemoveReason)GB(p1, 24, 8);
			if (reason >= CRR_END) return CMD_ERROR;

			Company *c = Company::GetIfValid(company_id);
			if (c == nullptr) return CMD_ERROR;

			if (!(flags & DC_EXEC)) return CommandCost();

			/* Delete any open window of the company */
			DeleteCompanyWindows(c->index);
			CompanyNewsInformation *cni = MallocT<CompanyNewsInformation>(1);
			cni->FillData(c);

			/* Show the bankrupt news */
			SetDParam(0, STR_NEWS_COMPANY_BANKRUPT_TITLE);
			SetDParam(1, STR_NEWS_COMPANY_BANKRUPT_DESCRIPTION);
			SetDParamStr(2, cni->company_name);
			AddCompanyNewsItem(STR_MESSAGE_NEWS_FORMAT, cni);

			/* Remove the company */
			ChangeOwnershipOfCompanyItems(c->index, INVALID_OWNER);
			if (c->is_ai) AI::Stop(c->index);

			CompanyID c_index = c->index;
			delete c;
			AI::BroadcastNewEvent(new ScriptEventCompanyBankrupt(c_index));
			Game::NewEvent(new ScriptEventCompanyBankrupt(c_index));
			CompanyAdminRemove(c_index, (CompanyRemoveReason)reason);

			if (StoryPage::GetNumItems() == 0 || Goal::GetNumItems() == 0) InvalidateWindowData(WC_MAIN_TOOLBAR, 0);
			break;
		}

		default: return CMD_ERROR;
	}

	InvalidateWindowClassesData(WC_GAME_OPTIONS);
	InvalidateWindowClassesData(WC_AI_SETTINGS);
	InvalidateWindowClassesData(WC_AI_LIST);

	return CommandCost();
}

/**
 * Change the company manager's face.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 unused
 * @param p2 face bitmasked
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdSetCompanyManagerFace(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	CompanyManagerFace cmf = (CompanyManagerFace)p2;

	if (!IsValidCompanyManagerFace(cmf)) return CMD_ERROR;

	if (flags & DC_EXEC) {
		Company::Get(_current_company)->face = cmf;
		MarkWholeScreenDirty();
	}
	return CommandCost();
}

/**
 * Change the company's company-colour
 * @param tile unused
 * @param flags operation to perform
 * @param p1 bitstuffed:
 * p1 bits 0-7 scheme to set
 * p1 bit 8 set first/second colour
 * @param p2 new colour for vehicles, property, etc.
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdSetCompanyColour(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	Colours colour = Extract<Colours, 0, 8>(p2);
	LiveryScheme scheme = Extract<LiveryScheme, 0, 8>(p1);
	bool second = HasBit(p1, 8);

	if (scheme >= LS_END || (colour >= COLOUR_END && colour != INVALID_COLOUR)) return CMD_ERROR;

	/* Default scheme can't be reset to invalid. */
	if (scheme == LS_DEFAULT && colour == INVALID_COLOUR) return CMD_ERROR;

	Company *c = Company::Get(_current_company);

	/* Ensure no two companies have the same primary colour */
	if (scheme == LS_DEFAULT && !second) {
		for (const Company *cc : Company::Iterate()) {
			if (cc != c && cc->colour == colour) return CMD_ERROR;
		}
	}

	if (flags & DC_EXEC) {
		if (!second) {
			if (scheme != LS_DEFAULT) SB(c->livery[scheme].in_use, 0, 1, colour != INVALID_COLOUR);
			if (colour == INVALID_COLOUR) colour = (Colours)c->livery[LS_DEFAULT].colour1;
			c->livery[scheme].colour1 = colour;

			/* If setting the first colour of the default scheme, adjust the
			 * original and cached company colours too. */
			if (scheme == LS_DEFAULT) {
				for (int i = 1; i < LS_END; i++) {
					if (!HasBit(c->livery[i].in_use, 0)) c->livery[i].colour1 = colour;
				}
				_company_colours[_current_company] = colour;
				c->colour = colour;
				CompanyAdminUpdate(c);
			}
		} else {
			if (scheme != LS_DEFAULT) SB(c->livery[scheme].in_use, 1, 1, colour != INVALID_COLOUR);
			if (colour == INVALID_COLOUR) colour = (Colours)c->livery[LS_DEFAULT].colour2;
			c->livery[scheme].colour2 = colour;

			if (scheme == LS_DEFAULT) {
				for (int i = 1; i < LS_END; i++) {
					if (!HasBit(c->livery[i].in_use, 1)) c->livery[i].colour2 = colour;
				}
			}
		}

		if (c->livery[scheme].in_use != 0) {
			/* If enabling a scheme, set the default scheme to be in use too */
			c->livery[LS_DEFAULT].in_use = 1;
		} else {
			/* Else loop through all schemes to see if any are left enabled.
			 * If not, disable the default scheme too. */
			c->livery[LS_DEFAULT].in_use = 0;
			for (scheme = LS_DEFAULT; scheme < LS_END; scheme++) {
				if (c->livery[scheme].in_use != 0) {
					c->livery[LS_DEFAULT].in_use = 1;
					break;
				}
			}
		}

		ResetVehicleColourMap();
		MarkWholeScreenDirty();

		/* All graph related to companies use the company colour. */
		InvalidateWindowData(WC_INCOME_GRAPH, 0);
		InvalidateWindowData(WC_OPERATING_PROFIT, 0);
		InvalidateWindowData(WC_DELIVERED_CARGO, 0);
		InvalidateWindowData(WC_PERFORMANCE_HISTORY, 0);
		InvalidateWindowData(WC_COMPANY_VALUE, 0);
		InvalidateWindowData(WC_LINKGRAPH_LEGEND, 0);
		/* The smallmap owner view also stores the company colours. */
		BuildOwnerLegend();
		InvalidateWindowData(WC_SMALLMAP, 0, 1);

		/* Company colour data is indirectly cached. */
		for (Vehicle *v : Vehicle::Iterate()) {
			if (v->owner == _current_company) v->InvalidateNewGRFCache();
		}

		extern void UpdateObjectColours(const Company *c);
		UpdateObjectColours(c);
	}
	return CommandCost();
}

/**
 * Is the given name in use as name of a company?
 * @param name Name to search.
 * @return \c true if the name us unique (that is, not in use), else \c false.
 */
static bool IsUniqueCompanyName(const char *name)
{
	for (const Company *c : Company::Iterate()) {
		if (!c->name.empty() && c->name == name) return false;
	}

	return true;
}

/**
 * Change the name of the company.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 unused
 * @param p2 unused
 * @param text the new name or an empty string when resetting to the default
 * @return the cost of this operation or an error
 */
CommandCost CmdRenameCompany(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	bool reset = StrEmpty(text);

	if (!reset) {
		if (Utf8StringLength(text) >= MAX_LENGTH_COMPANY_NAME_CHARS) return CMD_ERROR;
		if (!IsUniqueCompanyName(text)) return_cmd_error(STR_ERROR_NAME_MUST_BE_UNIQUE);
	}

	if (flags & DC_EXEC) {
		Company *c = Company::Get(_current_company);
		if (reset) {
			c->name.clear();
		} else {
			c->name = text;
		}
		MarkWholeScreenDirty();
		CompanyAdminUpdate(c);
	}

	return CommandCost();
}

/**
 * Is the given name in use as president name of a company?
 * @param name Name to search.
 * @return \c true if the name us unique (that is, not in use), else \c false.
 */
static bool IsUniquePresidentName(const char *name)
{
	for (const Company *c : Company::Iterate()) {
		if (!c->president_name.empty() && c->president_name == name) return false;
	}

	return true;
}

/**
 * Change the name of the president.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 unused
 * @param p2 unused
 * @param text the new name or an empty string when resetting to the default
 * @return the cost of this operation or an error
 */
CommandCost CmdRenamePresident(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	bool reset = StrEmpty(text);

	if (!reset) {
		if (Utf8StringLength(text) >= MAX_LENGTH_PRESIDENT_NAME_CHARS) return CMD_ERROR;
		if (!IsUniquePresidentName(text)) return_cmd_error(STR_ERROR_NAME_MUST_BE_UNIQUE);
	}

	if (flags & DC_EXEC) {
		Company *c = Company::Get(_current_company);

		if (reset) {
			c->president_name.clear();
		} else {
			c->president_name = text;

			if (c->name_1 == STR_SV_UNNAMED && c->name.empty()) {
				char buf[80];

				seprintf(buf, lastof(buf), "%s Transport", text);
				DoCommand(0, 0, 0, DC_EXEC, CMD_RENAME_COMPANY, buf);
			}
		}

		MarkWholeScreenDirty();
		CompanyAdminUpdate(c);
	}

	return CommandCost();
}

/**
 * Get the service interval for the given company and vehicle type.
 * @param c The company, or nullptr for client-default settings.
 * @param type The vehicle type to get the interval for.
 * @return The service interval.
 */
int CompanyServiceInterval(const Company *c, VehicleType type)
{
	const VehicleDefaultSettings *vds = (c == nullptr) ? &_settings_client.company.vehicle : &c->settings.vehicle;
	switch (type) {
		default: NOT_REACHED();
		case VEH_TRAIN:    return vds->servint_trains;
		case VEH_ROAD:     return vds->servint_roadveh;
		case VEH_AIRCRAFT: return vds->servint_aircraft;
		case VEH_SHIP:     return vds->servint_ships;
	}
}

/**
 * Get total sum of all owned road bits.
 * @return Combined total road road bits.
 */
uint32 CompanyInfrastructure::GetRoadTotal() const
{
	uint32 total = 0;
	for (RoadType rt = ROADTYPE_BEGIN; rt != ROADTYPE_END; rt++) {
		if (RoadTypeIsRoad(rt)) total += this->road[rt];
	}
	return total;
}

/**
 * Get total sum of all owned tram bits.
 * @return Combined total of tram road bits.
 */
uint32 CompanyInfrastructure::GetTramTotal() const
{
	uint32 total = 0;
	for (RoadType rt = ROADTYPE_BEGIN; rt != ROADTYPE_END; rt++) {
		if (RoadTypeIsTram(rt)) total += this->road[rt];
	}
	return total;
}
