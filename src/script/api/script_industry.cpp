/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_industry.cpp Implementation of ScriptIndustry. */

#include "../../stdafx.h"
#include "script_industry.hpp"
#include "script_cargo.hpp"
#include "script_company.hpp"
#include "script_error.hpp"
#include "script_map.hpp"
#include "../../company_base.h"
#include "../../industry.h"
#include "../../string_func.h"
#include "../../strings_func.h"
#include "../../station_base.h"
#include "../../newgrf_industries.h"
#include "table/strings.h"
#include <numeric>

#include "../../safeguards.h"

/* static */ int32 ScriptIndustry::GetIndustryCount()
{
	return (int32)::Industry::GetNumItems();
}

/* static */ bool ScriptIndustry::IsValidIndustry(IndustryID industry_id)
{
	return ::Industry::IsValidID(industry_id);
}

/* static */ IndustryID ScriptIndustry::GetIndustryID(TileIndex tile)
{
	if (!::IsValidTile(tile) || !::IsTileType(tile, MP_INDUSTRY)) return INVALID_INDUSTRY;
	return ::GetIndustryIndex(tile);
}

/* static */ char *ScriptIndustry::GetName(IndustryID industry_id)
{
	if (!IsValidIndustry(industry_id)) return nullptr;

	::SetDParam(0, industry_id);
	return GetString(STR_INDUSTRY_NAME);
}

/* static */ bool ScriptIndustry::SetText(IndustryID industry_id, Text *text)
{
	CCountedPtr<Text> counter(text);

	const char *encoded_text = nullptr;
	if (text != nullptr) {
		encoded_text = text->GetEncodedText();
		EnforcePreconditionEncodedText(false, encoded_text);
	}
	EnforcePrecondition(false, IsValidIndustry(industry_id));

	return ScriptObject::DoCommand(0, industry_id, static_cast<uint32>(IndustryAction::SetText), CMD_INDUSTRY_CTRL, encoded_text);
}

/* static */ ScriptIndustry::CargoAcceptState ScriptIndustry::IsCargoAccepted(IndustryID industry_id, CargoID cargo_id)
{
	if (!IsValidIndustry(industry_id)) return CAS_NOT_ACCEPTED;
	if (!ScriptCargo::IsValidCargo(cargo_id)) return CAS_NOT_ACCEPTED;

	Industry *i = ::Industry::Get(industry_id);

	for (byte j = 0; j < lengthof(i->accepts_cargo); j++) {
		if (i->accepts_cargo[j] == cargo_id) {
			if (IndustryTemporarilyRefusesCargo(i, cargo_id)) return CAS_TEMP_REFUSED;
			return CAS_ACCEPTED;
		}
	}

	return CAS_NOT_ACCEPTED;
}

/* static */ int32 ScriptIndustry::GetStockpiledCargo(IndustryID industry_id, CargoID cargo_id)
{
	if (!IsValidIndustry(industry_id)) return -1;
	if (!ScriptCargo::IsValidCargo(cargo_id)) return -1;

	Industry *ind = ::Industry::Get(industry_id);
	for (uint i = 0; i < lengthof(ind->accepts_cargo); i++) {
		CargoID cid = ind->accepts_cargo[i];
		if (cid == cargo_id) {
			return ind->incoming_cargo_waiting[i];
		}
	}

	return -1;
}

/* static */ int32 ScriptIndustry::GetLastMonthProduction(IndustryID industry_id, CargoID cargo_id)
{
	if (!IsValidIndustry(industry_id)) return -1;
	if (!ScriptCargo::IsValidCargo(cargo_id)) return -1;

	const Industry *i = ::Industry::Get(industry_id);

	for (byte j = 0; j < lengthof(i->produced_cargo); j++) {
		if (i->produced_cargo[j] == cargo_id) return i->past_production[0][j];
	}

	return -1;
}

/* static */ int32 ScriptIndustry::GetLastMonthTransported(IndustryID industry_id, CargoID cargo_id)
{
	if (!IsValidIndustry(industry_id)) return -1;
	if (!ScriptCargo::IsValidCargo(cargo_id)) return -1;

	const Industry *i = ::Industry::Get(industry_id);

	for (byte j = 0; j < lengthof(i->produced_cargo); j++) {
		if (i->produced_cargo[j] == cargo_id) return i->past_transported[0][j];
	}

	return -1;
}

/* static */ int32 ScriptIndustry::GetLastMonthTransportedPercentage(IndustryID industry_id, CargoID cargo_id)
{
	if (!IsValidIndustry(industry_id)) return -1;
	if (!ScriptCargo::IsValidCargo(cargo_id)) return -1;

	const Industry *i = ::Industry::Get(industry_id);

	for (byte j = 0; j < lengthof(i->produced_cargo); j++) {
		if (i->produced_cargo[j] == cargo_id) return ::ToPercent8(i->past_pct_transported[0][j]);
	}

	return -1;
}

/* static */ TileIndex ScriptIndustry::GetLocation(IndustryID industry_id)
{
	if (!IsValidIndustry(industry_id)) return INVALID_TILE;

	return ::Industry::Get(industry_id)->location.tile;
}

/* static */ int32 ScriptIndustry::GetAmountOfStationsAround(IndustryID industry_id)
{
	if (!IsValidIndustry(industry_id)) return -1;

	Industry *ind = ::Industry::Get(industry_id);
	return (int32)ind->stations_near.size();
}

/* static */ int32 ScriptIndustry::GetDistanceManhattanToTile(IndustryID industry_id, TileIndex tile)
{
	if (!IsValidIndustry(industry_id)) return -1;

	return ScriptMap::DistanceManhattan(tile, GetLocation(industry_id));
}

/* static */ int32 ScriptIndustry::GetDistanceSquareToTile(IndustryID industry_id, TileIndex tile)
{
	if (!IsValidIndustry(industry_id)) return -1;

	return ScriptMap::DistanceSquare(tile, GetLocation(industry_id));
}

/* static */ bool ScriptIndustry::IsBuiltOnWater(IndustryID industry_id)
{
	if (!IsValidIndustry(industry_id)) return false;

	return (::GetIndustrySpec(::Industry::Get(industry_id)->type)->behaviour & INDUSTRYBEH_BUILT_ONWATER) != 0;
}

/* static */ bool ScriptIndustry::HasHeliport(IndustryID industry_id)
{
	if (!IsValidIndustry(industry_id)) return false;

	return (::GetIndustrySpec(::Industry::Get(industry_id)->type)->behaviour & INDUSTRYBEH_AI_AIRSHIP_ROUTES) != 0;
}

/* static */ TileIndex ScriptIndustry::GetHeliportLocation(IndustryID industry_id)
{
	if (!IsValidIndustry(industry_id)) return INVALID_TILE;
	if (!HasHeliport(industry_id)) return INVALID_TILE;

	const Industry *ind = ::Industry::Get(industry_id);
	TILE_AREA_LOOP(tile_cur, ind->location) {
		if (IsTileType(tile_cur, MP_STATION) && IsOilRig(tile_cur)) {
			return tile_cur;
		}
	}

	return INVALID_TILE;
}

/* static */ bool ScriptIndustry::HasDock(IndustryID industry_id)
{
	if (!IsValidIndustry(industry_id)) return false;

	return (::GetIndustrySpec(::Industry::Get(industry_id)->type)->behaviour & INDUSTRYBEH_AI_AIRSHIP_ROUTES) != 0;
}

/* static */ TileIndex ScriptIndustry::GetDockLocation(IndustryID industry_id)
{
	if (!IsValidIndustry(industry_id)) return INVALID_TILE;
	if (!HasDock(industry_id)) return INVALID_TILE;

	const Industry *ind = ::Industry::Get(industry_id);
	TILE_AREA_LOOP(tile_cur, ind->location) {
		if (IsTileType(tile_cur, MP_STATION) && IsOilRig(tile_cur)) {
			return tile_cur;
		}
	}

	return INVALID_TILE;
}

/* static */ IndustryType ScriptIndustry::GetIndustryType(IndustryID industry_id)
{
	if (!IsValidIndustry(industry_id)) return INVALID_INDUSTRYTYPE;

	return ::Industry::Get(industry_id)->type;
}

int32 ScriptIndustry::GetLastProductionYear(IndustryID industry_id)
{
	Industry *i = Industry::GetIfValid(industry_id);
	if (i == nullptr) return 0;
	return i->last_prod_year;
}

ScriptDate::Date ScriptIndustry::GetCargoLastAcceptedDate(IndustryID industry_id, CargoID cargo_type)
{
	Industry *i = Industry::GetIfValid(industry_id);
	if (i == nullptr) return ScriptDate::DATE_INVALID;

	if (cargo_type == CT_INVALID) {
		return (ScriptDate::Date)std::accumulate(std::begin(i->last_cargo_accepted_at), std::end(i->last_cargo_accepted_at), 0, [](Date a, Date b) { return std::max(a, b); });
	} else {
		int index = i->GetCargoAcceptedIndex(cargo_type);
		if (index < 0) return ScriptDate::DATE_INVALID;
		return (ScriptDate::Date)i->last_cargo_accepted_at[index];
	}
}

uint32 ScriptIndustry::GetControlFlags(IndustryID industry_id)
{
	Industry *i = Industry::GetIfValid(industry_id);
	if (i == nullptr) return 0;
	return i->ctlflags;
}

bool ScriptIndustry::SetControlFlags(IndustryID industry_id, uint32 control_flags)
{
	if (ScriptObject::GetCompany() != OWNER_DEITY) return false;
	if (!IsValidIndustry(industry_id)) return false;

	return ScriptObject::DoCommand(0, industry_id, 0 | ((control_flags & ::INDCTL_MASK) << 8), CMD_INDUSTRY_CTRL);
}

/* static */ ScriptCompany::CompanyID ScriptIndustry::GetExclusiveSupplier(IndustryID industry_id)
{
	if (!IsValidIndustry(industry_id)) return ScriptCompany::COMPANY_INVALID;

	auto company_id = ::Industry::Get(industry_id)->exclusive_supplier;
	if (!::Company::IsValidID(company_id)) return ScriptCompany::COMPANY_INVALID;

	return (ScriptCompany::CompanyID)((byte)company_id);
}

/* static */ bool ScriptIndustry::SetExclusiveSupplier(IndustryID industry_id, ScriptCompany::CompanyID company_id)
{
	EnforcePrecondition(false, IsValidIndustry(industry_id));

	auto company = ScriptCompany::ResolveCompanyID(company_id);
	::Owner owner = (company == ScriptCompany::COMPANY_INVALID ? ::INVALID_OWNER : (::Owner)company);
	return ScriptObject::DoCommand(0, industry_id, 1 | (((uint8)owner) << 16), CMD_INDUSTRY_CTRL);
}

/* static */ ScriptCompany::CompanyID ScriptIndustry::GetExclusiveConsumer(IndustryID industry_id)
{
	if (!IsValidIndustry(industry_id)) return ScriptCompany::COMPANY_INVALID;

	auto company_id = ::Industry::Get(industry_id)->exclusive_consumer;
	if (!::Company::IsValidID(company_id)) return ScriptCompany::COMPANY_INVALID;

	return (ScriptCompany::CompanyID)((byte)company_id);
}

/* static */ bool ScriptIndustry::SetExclusiveConsumer(IndustryID industry_id, ScriptCompany::CompanyID company_id)
{
	EnforcePrecondition(false, IsValidIndustry(industry_id));

	auto company = ScriptCompany::ResolveCompanyID(company_id);
	::Owner owner = (company == ScriptCompany::COMPANY_INVALID ? ::INVALID_OWNER : (::Owner)company);
	return ScriptObject::DoCommand(0, industry_id, 2 | (((uint8)owner) << 16), CMD_INDUSTRY_CTRL);
}
