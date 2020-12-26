/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file cargomonitor_sl.cpp Code handling saving and loading of Cargo monitoring. */

#include "../stdafx.h"
#include "../cargomonitor.h"

#include "saveload.h"

#include "../safeguards.h"

/** Temporary storage of cargo monitoring data for loading or saving it. */
struct TempStorage {
	CargoMonitorID number;
	uint32 amount;
};

/** Description of the #TempStorage structure for the purpose of load and save. */
static const SaveLoad _cargomonitor_pair_desc[] = {
	SLE_VAR(TempStorage, number, SLE_UINT32),
	SLE_VAR(TempStorage, amount, SLE_UINT32),
	SLE_END()
};

static CargoMonitorID FixupCargoMonitor(CargoMonitorID number)
{
	/* Between SLV_EXTEND_CARGOTYPES and SLV_FIX_CARGO_MONITOR, the
	 * CargoMonitorID structure had insufficient packing for more
	 * than 32 cargo types. Here we have to shuffle bits to account
	 * for the change.
	 * Company moved from bits 24-31 to 25-28.
	 * Cargo type increased from bits 19-23 to 19-24.
	 */
	SB(number, 25, 4, GB(number, 24, 4));
	SB(number, 29, 3, 0);
	ClrBit(number, 24);
	return number;
}

/** Save the #_cargo_deliveries monitoring map. */
static void SaveDelivery()
{
	TempStorage storage;

	int i = 0;
	CargoMonitorMap::const_iterator iter = _cargo_deliveries.begin();
	while (iter != _cargo_deliveries.end()) {
		storage.number = iter->first;
		storage.amount = static_cast<int32>(iter->second);

		SlSetArrayIndex(i);
		SlObject(&storage, _cargomonitor_pair_desc);

		i++;
		iter++;
	}
}

/** Load the #_cargo_deliveries monitoring map. */
static void LoadDelivery()
{
	TempStorage storage;
	bool fix = IsSavegameVersionBefore(SLV_FIX_CARGO_MONITOR);

	ClearCargoDeliveryMonitoring();
	for (;;) {
		if (SlIterateArray() < 0) break;
		SlObject(&storage, _cargomonitor_pair_desc);

		if (fix) storage.number = FixupCargoMonitor(storage.number);

		std::pair<CargoMonitorID, uint32> p(storage.number, storage.amount);
		_cargo_deliveries.insert(p);
	}
}


/** Save the #_cargo_pickups monitoring map. */
static void SavePickup()
{
	TempStorage storage;

	int i = 0;
	CargoMonitorMap::const_iterator iter = _cargo_pickups.begin();
	while (iter != _cargo_pickups.end()) {
		storage.number = iter->first;
		storage.amount = static_cast<int32>(iter->second);

		SlSetArrayIndex(i);
		SlObject(&storage, _cargomonitor_pair_desc);

		i++;
		iter++;
	}
}

/** Load the #_cargo_pickups monitoring map. */
static void LoadPickup()
{
	TempStorage storage;
	bool fix = IsSavegameVersionBefore(SLV_FIX_CARGO_MONITOR);

	ClearCargoPickupMonitoring();
	for (;;) {
		if (SlIterateArray() < 0) break;
		SlObject(&storage, _cargomonitor_pair_desc);

		if (fix) storage.number = FixupCargoMonitor(storage.number);

		std::pair<CargoMonitorID, uint32> p(storage.number, storage.amount);
		_cargo_pickups.insert(p);
	}
}

/** Chunk definition of the cargomonitoring maps. */
extern const ChunkHandler _cargomonitor_chunk_handlers[] = {
	{ 'CMDL', SaveDelivery, LoadDelivery, nullptr, nullptr, CH_ARRAY},
	{ 'CMPU', SavePickup,   LoadPickup,   nullptr, nullptr, CH_ARRAY | CH_LAST},
};
