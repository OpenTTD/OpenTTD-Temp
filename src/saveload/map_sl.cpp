/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file map_sl.cpp Code handling saving and loading of map */

#include "../stdafx.h"

#include "saveload.h"
#include "compat/map_sl_compat.h"

#include "../map_func.h"
#include "../core/bitmath_func.hpp"
#include "../fios.h"
#include <array>

#include "../safeguards.h"

static uint32 _map_dim_x;
static uint32 _map_dim_y;

static const SaveLoad _map_desc[] = {
	SLEG_CONDVAR("dim_x", _map_dim_x, SLE_UINT32, SLV_6, SL_MAX_VERSION),
	SLEG_CONDVAR("dim_y", _map_dim_y, SLE_UINT32, SLV_6, SL_MAX_VERSION),
};

struct MAPSChunkHandler : ChunkHandler {
	MAPSChunkHandler() : ChunkHandler('MAPS', CH_TABLE) {}

	void Save() const override
	{
		SlTableHeader(_map_desc);

		_map_dim_x = MapSizeX();
		_map_dim_y = MapSizeY();

		SlSetArrayIndex(0);
		SlGlobList(_map_desc);
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlCompatTableHeader(_map_desc, _map_sl_compat);

		if (!IsSavegameVersionBefore(SLV_RIFF_TO_ARRAY) && SlIterateArray() == -1) return;
		SlGlobList(slt);
		if (!IsSavegameVersionBefore(SLV_RIFF_TO_ARRAY) && SlIterateArray() != -1) SlErrorCorrupt("Too many MAPS entries");

		AllocateMap(_map_dim_x, _map_dim_y);
	}

	void LoadCheck(size_t) const override
	{
		const std::vector<SaveLoad> slt = SlCompatTableHeader(_map_desc, _map_sl_compat);

		if (!IsSavegameVersionBefore(SLV_RIFF_TO_ARRAY) && SlIterateArray() == -1) return;
		SlGlobList(slt);
		if (!IsSavegameVersionBefore(SLV_RIFF_TO_ARRAY) && SlIterateArray() != -1) SlErrorCorrupt("Too many MAPS entries");

		_load_check_data.map_size_x = _map_dim_x;
		_load_check_data.map_size_y = _map_dim_y;
	}
};

static const uint MAP_SL_BUF_SIZE = 4096;

struct MAPTChunkHandler : ChunkHandler {
	MAPTChunkHandler() : ChunkHandler('MAPT', CH_RIFF) {}

	void Load() const override
	{
		std::array<byte, MAP_SL_BUF_SIZE> buf;
		TileIndex size = MapSize();

		for (TileIndex i = 0; i != size;) {
			SlCopy(buf.data(), MAP_SL_BUF_SIZE, SLE_UINT8);
			for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) _m[i++].type = buf[j];
		}
	}

	void Save() const override
	{
		std::array<byte, MAP_SL_BUF_SIZE> buf;
		TileIndex size = MapSize();

		SlSetLength(size);
		for (TileIndex i = 0; i != size;) {
			for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) buf[j] = _m[i++].type;
			SlCopy(buf.data(), MAP_SL_BUF_SIZE, SLE_UINT8);
		}
	}
};

struct MAPHChunkHandler : ChunkHandler {
	MAPHChunkHandler() : ChunkHandler('MAPH', CH_RIFF) {}

	void Load() const override
	{
		std::array<byte, MAP_SL_BUF_SIZE> buf;
		TileIndex size = MapSize();

		for (TileIndex i = 0; i != size;) {
			SlCopy(buf.data(), MAP_SL_BUF_SIZE, SLE_UINT8);
			for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) _m[i++].height = buf[j];
		}
	}

	void Save() const override
	{
		std::array<byte, MAP_SL_BUF_SIZE> buf;
		TileIndex size = MapSize();

		SlSetLength(size);
		for (TileIndex i = 0; i != size;) {
			for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) buf[j] = _m[i++].height;
			SlCopy(buf.data(), MAP_SL_BUF_SIZE, SLE_UINT8);
		}
	}
};

struct MAPOChunkHandler : ChunkHandler {
	MAPOChunkHandler() : ChunkHandler('MAPO', CH_RIFF) {}

	void Load() const override
	{
		std::array<byte, MAP_SL_BUF_SIZE> buf;
		TileIndex size = MapSize();

		for (TileIndex i = 0; i != size;) {
			SlCopy(buf.data(), MAP_SL_BUF_SIZE, SLE_UINT8);
			for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) _m[i++].m1 = buf[j];
		}
	}

	void Save() const override
	{
		std::array<byte, MAP_SL_BUF_SIZE> buf;
		TileIndex size = MapSize();

		SlSetLength(size);
		for (TileIndex i = 0; i != size;) {
			for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) buf[j] = _m[i++].m1;
			SlCopy(buf.data(), MAP_SL_BUF_SIZE, SLE_UINT8);
		}
	}
};

struct MAP2ChunkHandler : ChunkHandler {
	MAP2ChunkHandler() : ChunkHandler('MAP2', CH_RIFF) {}

	void Load() const override
	{
		std::array<uint16, MAP_SL_BUF_SIZE> buf;
		TileIndex size = MapSize();

		for (TileIndex i = 0; i != size;) {
			SlCopy(buf.data(), MAP_SL_BUF_SIZE,
				/* In those versions the m2 was 8 bits */
				IsSavegameVersionBefore(SLV_5) ? SLE_FILE_U8 | SLE_VAR_U16 : SLE_UINT16
			);
			for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) _m[i++].m2 = buf[j];
		}
	}

	void Save() const override
	{
		std::array<uint16, MAP_SL_BUF_SIZE> buf;
		TileIndex size = MapSize();

		SlSetLength(size * sizeof(uint16));
		for (TileIndex i = 0; i != size;) {
			for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) buf[j] = _m[i++].m2;
			SlCopy(buf.data(), MAP_SL_BUF_SIZE, SLE_UINT16);
		}
	}
};

struct M3LOChunkHandler : ChunkHandler {
	M3LOChunkHandler() : ChunkHandler('M3LO', CH_RIFF) {}

	void Load() const override
	{
		std::array<byte, MAP_SL_BUF_SIZE> buf;
		TileIndex size = MapSize();

		for (TileIndex i = 0; i != size;) {
			SlCopy(buf.data(), MAP_SL_BUF_SIZE, SLE_UINT8);
			for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) _m[i++].m3 = buf[j];
		}
	}

	void Save() const override
	{
		std::array<byte, MAP_SL_BUF_SIZE> buf;
		TileIndex size = MapSize();

		SlSetLength(size);
		for (TileIndex i = 0; i != size;) {
			for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) buf[j] = _m[i++].m3;
			SlCopy(buf.data(), MAP_SL_BUF_SIZE, SLE_UINT8);
		}
	}
};

struct M3HIChunkHandler : ChunkHandler {
	M3HIChunkHandler() : ChunkHandler('M3HI', CH_RIFF) {}

	void Load() const override
	{
		std::array<byte, MAP_SL_BUF_SIZE> buf;
		TileIndex size = MapSize();

		for (TileIndex i = 0; i != size;) {
			SlCopy(buf.data(), MAP_SL_BUF_SIZE, SLE_UINT8);
			for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) _m[i++].m4 = buf[j];
		}
	}

	void Save() const override
	{
		std::array<byte, MAP_SL_BUF_SIZE> buf;
		TileIndex size = MapSize();

		SlSetLength(size);
		for (TileIndex i = 0; i != size;) {
			for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) buf[j] = _m[i++].m4;
			SlCopy(buf.data(), MAP_SL_BUF_SIZE, SLE_UINT8);
		}
	}
};

struct MAP5ChunkHandler : ChunkHandler {
	MAP5ChunkHandler() : ChunkHandler('MAP5', CH_RIFF) {}

	void Load() const override
	{
		std::array<byte, MAP_SL_BUF_SIZE> buf;
		TileIndex size = MapSize();

		for (TileIndex i = 0; i != size;) {
			SlCopy(buf.data(), MAP_SL_BUF_SIZE, SLE_UINT8);
			for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) _m[i++].m5 = buf[j];
		}

		if (IsSavegameVersionBefore(SLV_MULTITILE_DEPOTS)) {
			/* Move some bits for alignment purposes. */
			for (TileIndex i = 0; i != size; i++) {
				if (IsTileType(i, MP_WATER)) {
					SB(_m[i].m5, 6, 1, GB(_m[i].m5, 4, 1));
					SB(_m[i].m5, 4, 1, 0);
				}
			}
		}
	}

	void Save() const override
	{
		std::array<byte, MAP_SL_BUF_SIZE> buf;
		TileIndex size = MapSize();

		SlSetLength(size);
		for (TileIndex i = 0; i != size;) {
			for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) buf[j] = _m[i++].m5;
			SlCopy(buf.data(), MAP_SL_BUF_SIZE, SLE_UINT8);
		}
	}
};

struct MAPEChunkHandler : ChunkHandler {
	MAPEChunkHandler() : ChunkHandler('MAPE', CH_RIFF) {}

	void Load() const override
	{
		std::array<byte, MAP_SL_BUF_SIZE> buf;
		TileIndex size = MapSize();

		if (IsSavegameVersionBefore(SLV_42)) {
			for (TileIndex i = 0; i != size;) {
				/* 1024, otherwise we overflow on 64x64 maps! */
				SlCopy(buf.data(), 1024, SLE_UINT8);
				for (uint j = 0; j != 1024; j++) {
					_me[i++].m6 = GB(buf[j], 0, 2);
					_me[i++].m6 = GB(buf[j], 2, 2);
					_me[i++].m6 = GB(buf[j], 4, 2);
					_me[i++].m6 = GB(buf[j], 6, 2);
				}
			}
		} else {
			for (TileIndex i = 0; i != size;) {
				SlCopy(buf.data(), MAP_SL_BUF_SIZE, SLE_UINT8);
				for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) _me[i++].m6 = buf[j];
			}
		}
	}

	void Save() const override
	{
		std::array<byte, MAP_SL_BUF_SIZE> buf;
		TileIndex size = MapSize();

		SlSetLength(size);
		for (TileIndex i = 0; i != size;) {
			for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) buf[j] = _me[i++].m6;
			SlCopy(buf.data(), MAP_SL_BUF_SIZE, SLE_UINT8);
		}
	}
};

struct MAP7ChunkHandler : ChunkHandler {
	MAP7ChunkHandler() : ChunkHandler('MAP7', CH_RIFF) {}

	void Load() const override
	{
		std::array<byte, MAP_SL_BUF_SIZE> buf;
		TileIndex size = MapSize();

		for (TileIndex i = 0; i != size;) {
			SlCopy(buf.data(), MAP_SL_BUF_SIZE, SLE_UINT8);
			for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) _me[i++].m7 = buf[j];
		}
	}

	void Save() const override
	{
		std::array<byte, MAP_SL_BUF_SIZE> buf;
		TileIndex size = MapSize();

		SlSetLength(size);
		for (TileIndex i = 0; i != size;) {
			for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) buf[j] = _me[i++].m7;
			SlCopy(buf.data(), MAP_SL_BUF_SIZE, SLE_UINT8);
		}
	}
};

struct MAP8ChunkHandler : ChunkHandler {
	MAP8ChunkHandler() : ChunkHandler('MAP8', CH_RIFF) {}

	void Load() const override
	{
		std::array<uint16, MAP_SL_BUF_SIZE> buf;
		TileIndex size = MapSize();

		for (TileIndex i = 0; i != size;) {
			SlCopy(buf.data(), MAP_SL_BUF_SIZE, SLE_UINT16);
			for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) _me[i++].m8 = buf[j];
		}
	}

	void Save() const override
	{
		std::array<uint16, MAP_SL_BUF_SIZE> buf;
		TileIndex size = MapSize();

		SlSetLength(size * sizeof(uint16));
		for (TileIndex i = 0; i != size;) {
			for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) buf[j] = _me[i++].m8;
			SlCopy(buf.data(), MAP_SL_BUF_SIZE, SLE_UINT16);
		}
	}
};

static const MAPSChunkHandler MAPS;
static const MAPTChunkHandler MAPT;
static const MAPHChunkHandler MAPH;
static const MAPOChunkHandler MAPO;
static const MAP2ChunkHandler MAP2;
static const M3LOChunkHandler M3LO;
static const M3HIChunkHandler M3HI;
static const MAP5ChunkHandler MAP5;
static const MAPEChunkHandler MAPE;
static const MAP7ChunkHandler MAP7;
static const MAP8ChunkHandler MAP8;
static const ChunkHandlerRef map_chunk_handlers[] = {
	MAPS,
	MAPT,
	MAPH,
	MAPO,
	MAP2,
	M3LO,
	M3HI,
	MAP5,
	MAPE,
	MAP7,
	MAP8,
};

extern const ChunkHandlerTable _map_chunk_handlers(map_chunk_handlers);
