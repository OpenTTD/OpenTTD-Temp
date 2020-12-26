/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file misc_cmd.cpp Some misc functions that are better fitted in other files, but never got moved there... */

#include "stdafx.h"
#include "command_func.h"
#include "economy_func.h"
#include "cmd_helper.h"
#include "window_func.h"
#include "textbuf_gui.h"
#include "network/network.h"
#include "network/network_func.h"
#include "strings_func.h"
#include "company_func.h"
#include "company_gui.h"
#include "company_base.h"
#include "core/backup_type.hpp"

#include "table/strings.h"

#include "safeguards.h"

/**
 * Increase the loan of your company.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 amount to increase the loan with, multitude of LOAN_INTERVAL. Only used when p2 == 2.
 * @param p2 when 0: loans LOAN_INTERVAL
 *           when 1: loans the maximum loan permitting money (press CTRL),
 *           when 2: loans the amount specified in p1
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdIncreaseLoan(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	Company *c = Company::Get(_current_company);

	if (c->current_loan >= _economy.max_loan) {
		SetDParam(0, _economy.max_loan);
		return_cmd_error(STR_ERROR_MAXIMUM_PERMITTED_LOAN);
	}

	Money loan;
	switch (p2) {
		default: return CMD_ERROR; // Invalid method
		case 0: // Take some extra loan
			loan = LOAN_INTERVAL;
			break;
		case 1: // Take a loan as big as possible
			loan = _economy.max_loan - c->current_loan;
			break;
		case 2: // Take the given amount of loan
			if ((int32)p1 < LOAN_INTERVAL || c->current_loan + (int32)p1 > _economy.max_loan || (int32)p1 % LOAN_INTERVAL != 0) return CMD_ERROR;
			loan = p1;
			break;
	}

	/* Overflow protection */
	if ((c->money + c->current_loan + loan) - (c->current_loan + loan) != c->money) return CMD_ERROR;

	if (flags & DC_EXEC) {
		c->money        += loan;
		c->current_loan += loan;
		InvalidateCompanyWindows(c);
	}

	return CommandCost(EXPENSES_OTHER);
}

/**
 * Decrease the loan of your company.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 amount to decrease the loan with, multitude of LOAN_INTERVAL. Only used when p2 == 2.
 * @param p2 when 0: pays back LOAN_INTERVAL
 *           when 1: pays back the maximum loan permitting money (press CTRL),
 *           when 2: pays back the amount specified in p1
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdDecreaseLoan(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	Company *c = Company::Get(_current_company);

	if (c->current_loan == 0) return_cmd_error(STR_ERROR_LOAN_ALREADY_REPAYED);

	Money loan;
	switch (p2) {
		default: return CMD_ERROR; // Invalid method
		case 0: // Pay back one step
			loan = min(c->current_loan, (Money)LOAN_INTERVAL);
			break;
		case 1: // Pay back as much as possible
			loan = max(min(c->current_loan, c->money), (Money)LOAN_INTERVAL);
			loan -= loan % LOAN_INTERVAL;
			break;
		case 2: // Repay the given amount of loan
			if ((int32)p1 % LOAN_INTERVAL != 0 || (int32)p1 < LOAN_INTERVAL || (int32)p1 > c->current_loan) return CMD_ERROR; // Invalid amount to loan
			loan = p1;
			break;
	}

	if (c->money < loan) {
		SetDParam(0, loan);
		return_cmd_error(STR_ERROR_CURRENCY_REQUIRED);
	}

	if (flags & DC_EXEC) {
		c->money        -= loan;
		c->current_loan -= loan;
		InvalidateCompanyWindows(c);
	}
	return CommandCost();
}

/**
 * In case of an unsafe unpause, we want the
 * user to confirm that it might crash.
 * @param w         unused
 * @param confirmed whether the user confirms his/her action
 */
static void AskUnsafeUnpauseCallback(Window *w, bool confirmed)
{
	if (confirmed) {
		DoCommandP(0, PM_PAUSED_ERROR, 0, CMD_PAUSE);
	}
}

/**
 * Pause/Unpause the game (server-only).
 * Set or unset a bit in the pause mode. If pause mode is zero the game is
 * unpaused. A bitset is used instead of a boolean value/counter to have
 * more control over the game when saving/loading, etc.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 the pause mode to change
 * @param p2 1 pauses, 0 unpauses this mode
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdPause(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	switch (p1) {
		case PM_PAUSED_SAVELOAD:
		case PM_PAUSED_ERROR:
		case PM_PAUSED_NORMAL:
		case PM_PAUSED_GAME_SCRIPT:
		case PM_PAUSED_LINK_GRAPH:
			break;

		case PM_PAUSED_JOIN:
		case PM_PAUSED_ACTIVE_CLIENTS:
			if (!_networking) return CMD_ERROR;
			break;

		default: return CMD_ERROR;
	}
	if (flags & DC_EXEC) {
		if (p1 == PM_PAUSED_NORMAL && _pause_mode & PM_PAUSED_ERROR) {
			ShowQuery(
				STR_NEWGRF_UNPAUSE_WARNING_TITLE,
				STR_NEWGRF_UNPAUSE_WARNING,
				nullptr,
				AskUnsafeUnpauseCallback
			);
		} else {
			PauseMode prev_mode = _pause_mode;

			if (p2 == 0) {
				_pause_mode = static_cast<PauseMode>(_pause_mode & (byte)~p1);
			} else {
				_pause_mode = static_cast<PauseMode>(_pause_mode | (byte)p1);
			}

			NetworkHandlePauseChange(prev_mode, (PauseMode)p1);
		}

		SetWindowDirty(WC_STATUS_BAR, 0);
		SetWindowDirty(WC_MAIN_TOOLBAR, 0);
	}
	return CommandCost();
}

/**
 * Change the financial flow of your company.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 the amount of money to receive (if positive), or spend (if negative)
 * @param p2 unused
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdMoneyCheat(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	return CommandCost(EXPENSES_OTHER, -(int32)p1);
}

/**
 * Change the bank bank balance of a company by inserting or removing money without affecting the loan.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 the amount of money to receive (if positive), or spend (if negative)
 * @param p2 (bit 0-7)  - the company ID.
 *           (bit 8-15) - the expenses type which should register the cost/income @see ExpensesType.
 * @param text unused
 * @return zero cost or an error
 */
CommandCost CmdChangeBankBalance(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	int32 delta = (int32)p1;
	CompanyID company = (CompanyID) GB(p2, 0, 8);
	ExpensesType expenses_type = Extract<ExpensesType, 8, 8>(p2);

	if (!Company::IsValidID(company)) return CMD_ERROR;
	if (expenses_type >= EXPENSES_END) return CMD_ERROR;
	if (_current_company != OWNER_DEITY) return CMD_ERROR;

	if (flags & DC_EXEC) {
		/* Change company bank balance of company. */
		Backup<CompanyID> cur_company(_current_company, company, FILE_LINE);
		SubtractMoneyFromCompany(CommandCost(expenses_type, -delta));
		cur_company.Restore();
	}

	/* This command doesn't cost anything for deity. */
	CommandCost zero_cost(expenses_type, 0);
	return zero_cost;
}

/**
 * Transfer funds (money) from one company to another.
 * To prevent abuse in multiplayer games you can only send money to other
 * companies if you have paid off your loan (either explicitly, or implicitly
 * given the fact that you have more money than loan).
 * @param tile unused
 * @param flags operation to perform
 * @param p1 the amount of money to transfer; max 20.000.000
 * @param p2 the company to transfer the money to
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdGiveMoney(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	if (!_settings_game.economy.give_money) return CMD_ERROR;

	const Company *c = Company::Get(_current_company);
	CommandCost amount(EXPENSES_OTHER, min((Money)p1, (Money)20000000LL));
	CompanyID dest_company = (CompanyID)p2;

	/* You can only transfer funds that is in excess of your loan */
	if (c->money - c->current_loan < amount.GetCost() || amount.GetCost() < 0) return CMD_ERROR;
	if (!_networking || !Company::IsValidID(dest_company)) return CMD_ERROR;

	if (flags & DC_EXEC) {
		/* Add money to company */
		Backup<CompanyID> cur_company(_current_company, dest_company, FILE_LINE);
		SubtractMoneyFromCompany(CommandCost(EXPENSES_OTHER, -amount.GetCost()));
		cur_company.Restore();
	}

	/* Subtract money from local-company */
	return amount;
}
