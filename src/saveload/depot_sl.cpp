/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file depot_sl.cpp Code handling saving and loading of depots */

#include "../stdafx.h"

#include "saveload.h"
#include "compat/depot_sl_compat.h"

#include "../depot_base.h"
#include "../town.h"

#include "../safeguards.h"

static TownID _town_index;

static const SaveLoad _depot_desc[] = {
	 SLE_CONDVAR(Depot, xy,         SLE_FILE_U16 | SLE_VAR_U32, SL_MIN_VERSION, SLV_6),
	 SLE_CONDVAR(Depot, xy,         SLE_UINT32,                 SLV_6, SL_MAX_VERSION),
	SLEG_CONDVAR("town_index", _town_index, SLE_UINT16,       SL_MIN_VERSION, SLV_141),
	 SLE_CONDREF(Depot, town,       REF_TOWN,                 SLV_141, SL_MAX_VERSION),
	 SLE_CONDVAR(Depot, town_cn,    SLE_UINT16,               SLV_141, SL_MAX_VERSION),
	SLE_CONDSSTR(Depot, name,       SLE_STR,                  SLV_141, SL_MAX_VERSION),
	 SLE_CONDVAR(Depot, build_date, SLE_INT32,                SLV_142, SL_MAX_VERSION),
	 SLE_CONDVAR(Depot, company,    SLE_UINT8,                  SLV_MULTITILE_DEPOTS, SL_MAX_VERSION),
	 SLE_CONDVAR(Depot, veh_type,   SLE_UINT8,                  SLV_MULTITILE_DEPOTS, SL_MAX_VERSION),
	 SLE_CONDVAR(Depot, ta.tile,    SLE_UINT32,                 SLV_MULTITILE_DEPOTS, SL_MAX_VERSION),
	 SLE_CONDVAR(Depot, ta.w,       SLE_FILE_U8 | SLE_VAR_U16,  SLV_MULTITILE_DEPOTS, SL_MAX_VERSION),
	 SLE_CONDVAR(Depot, ta.h,       SLE_FILE_U8 | SLE_VAR_U16,  SLV_MULTITILE_DEPOTS, SL_MAX_VERSION),
};

struct DEPTChunkHandler : ChunkHandler {
	DEPTChunkHandler() : ChunkHandler('DEPT', CH_TABLE) {}

	void Save() const override
	{
		SlTableHeader(_depot_desc);

		for (Depot *depot : Depot::Iterate()) {
			SlSetArrayIndex(depot->index);
			SlObject(depot, _depot_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlCompatTableHeader(_depot_desc, _depot_sl_compat);

		int index;

		while ((index = SlIterateArray()) != -1) {
			Depot *depot = new (index) Depot();
			SlObject(depot, slt);

			/* Set the town 'pointer' so we can restore it later. */
			if (IsSavegameVersionBefore(SLV_141)) depot->town = (Town *)(size_t)_town_index;
		}
	}

	void FixPointers() const override
	{
		for (Depot *depot : Depot::Iterate()) {
			SlObject(depot, _depot_desc);
			if (IsSavegameVersionBefore(SLV_141)) depot->town = Town::Get((size_t)depot->town);
		}
	}
};

static const DEPTChunkHandler DEPT;
static const ChunkHandlerRef depot_chunk_handlers[] = {
	DEPT,
};

extern const ChunkHandlerTable _depot_chunk_handlers(depot_chunk_handlers);
