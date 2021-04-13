/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_cargo.cpp Implementation of ScriptCargo. */

#include "../../stdafx.h"
#include "script_cargo.hpp"
#include "../../economy_func.h"
#include "../../core/bitmath_func.hpp"
#include "../../strings_func.h"
#include "../../settings_type.h"
#include "table/strings.h"

#include "../../safeguards.h"

/* static */ bool ScriptCargo::IsValidCargo(CargoID cargo_type)
{
	return (cargo_type < NUM_CARGO && ::CargoSpec::Get(cargo_type)->IsValid());
}

/* static */ bool ScriptCargo::IsValidTownEffect(TownEffect towneffect_type)
{
	return (towneffect_type >= (TownEffect)TE_BEGIN && towneffect_type < (TownEffect)TE_END);
}

/* static */ char *ScriptCargo::GetName(CargoID cargo_type)
{
	if (!IsValidCargo(cargo_type)) return nullptr;

	::SetDParam(0, 1ULL << cargo_type);
	return GetString(STR_JUST_CARGO_LIST);
}

/* static */ char *ScriptCargo::GetCargoLabel(CargoID cargo_type)
{
	if (!IsValidCargo(cargo_type)) return nullptr;
	const CargoSpec *cargo = ::CargoSpec::Get(cargo_type);

	/* cargo->label is a uint32 packing a 4 character non-terminated string,
	 * like "PASS", "COAL", "OIL_". New ones can be defined by NewGRFs */
	char *cargo_label = MallocT<char>(sizeof(cargo->label) + 1);
	for (uint i = 0; i < sizeof(cargo->label); i++) {
		cargo_label[i] = GB(cargo->label, (uint8)(sizeof(cargo->label) - i - 1) * 8, 8);
	}
	cargo_label[sizeof(cargo->label)] = '\0';
	return cargo_label;
}

/* static */ bool ScriptCargo::IsFreight(CargoID cargo_type)
{
	if (!IsValidCargo(cargo_type)) return false;
	const CargoSpec *cargo = ::CargoSpec::Get(cargo_type);
	return cargo->is_freight;
}

/* static */ bool ScriptCargo::HasCargoClass(CargoID cargo_type, CargoClass cargo_class)
{
	if (!IsValidCargo(cargo_type)) return false;
	return ::IsCargoInClass(cargo_type, (::CargoClass)cargo_class);
}

/* static */ ScriptCargo::TownEffect ScriptCargo::GetTownEffect(CargoID cargo_type)
{
	if (!IsValidCargo(cargo_type)) return TE_NONE;

	return (ScriptCargo::TownEffect)::CargoSpec::Get(cargo_type)->town_effect;
}

/* static */ Money ScriptCargo::GetCargoIncome(CargoID cargo_type, uint32 distance, uint32 days_in_transit)
{
	if (!IsValidCargo(cargo_type)) return -1;
	return ::GetTransportedGoodsIncome(1, distance, Clamp(days_in_transit * 2 / 5, 0, UINT16_MAX), cargo_type);
}

/* static */ ScriptCargo::DistributionType ScriptCargo::GetDistributionType(CargoID cargo_type)
{
	if (!ScriptCargo::IsValidCargo(cargo_type)) return INVALID_DISTRIBUTION_TYPE;
	return (ScriptCargo::DistributionType)_settings_game.linkgraph.GetDistributionType(cargo_type);
}
