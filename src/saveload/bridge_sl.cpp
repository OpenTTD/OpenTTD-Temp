/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

 /** @file bridge_sl.cpp Code handling saving and loading of bridges */

#include "../stdafx.h"
#include "../bridge.h"
#include "../bridge_map.h"

#include "saveload.h"
#include "newgrf_sl.h"

#include "../safeguards.h"

static const SaveLoad _bridge_desc[] = {
		SLE_VAR(Bridge, build_date,                 SLE_UINT32),
		SLE_VAR(Bridge, type,                       SLE_VAR_U32 | SLE_FILE_U16),
		SLE_REF(Bridge, town,                       REF_TOWN),
		SLE_VAR(Bridge, heads[0],                   SLE_UINT32),
		SLE_VAR(Bridge, heads[1],                   SLE_UINT32),
		SLE_VAR(Bridge, random,                     SLE_UINT16),

	SLE_END()
};

static void Save_BRDS()
{
	/* Write the objects */
	for (Bridge *b : Bridge::Iterate()) {
		SlSetArrayIndex(b->index);
		SlObject(b, _bridge_desc);
	}
}

static void Load_BRDS()
{
	int index;
	while ((index = SlIterateArray()) != -1) {
		Bridge *o = new (index) Bridge();
		SlObject(o, _bridge_desc);

		Axis axis = DiagDirToAxis(DiagdirBetweenTiles(o->heads[0], o->heads[1]));
		uint pos = axis == AXIS_X ? TileY(o->heads[0]) : TileX(o->heads[0]);

		_bridge_index[axis].Insert(pos, index);
	}
}

static void Ptrs_BRDS()
{
	for (Bridge *o : Bridge::Iterate()) {
		SlObject(o, _bridge_desc);
	}
}

static void Save_BRID()
{
	Save_NewGRFMapping(_bridge_mngr);
}

static void Load_BRID()
{
	Load_NewGRFMapping(_bridge_mngr);
}

extern const ChunkHandler _bridge_chunk_handlers[] = {
	{ 'BRID', Save_BRID, Load_BRID, nullptr,   nullptr, CH_ARRAY },
	{ 'BRDS', Save_BRDS, Load_BRDS, Ptrs_BRDS, nullptr, CH_ARRAY | CH_LAST},
};
