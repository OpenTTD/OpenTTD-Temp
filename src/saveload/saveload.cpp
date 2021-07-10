/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file saveload.cpp
 * All actions handling saving and loading goes on in this file. The general actions
 * are as follows for saving a game (loading is analogous):
 * <ol>
 * <li>initialize the writer by creating a temporary memory-buffer for it
 * <li>go through all to-be saved elements, each 'chunk' (#ChunkHandler) prefixed by a label
 * <li>use their description array (#SaveLoad) to know what elements to save and in what version
 *    of the game it was active (used when loading)
 * <li>write all data byte-by-byte to the temporary buffer so it is endian-safe
 * <li>when the buffer is full; flush it to the output (eg save to file) (_sl.buf, _sl.bufp, _sl.bufe)
 * <li>repeat this until everything is done, and flush any remaining output to file
 * </ol>
 */

#include "../stdafx.h"
#include "../debug.h"
#include "../station_base.h"
#include "../thread.h"
#include "../town.h"
#include "../network/network.h"
#include "../window_func.h"
#include "../strings_func.h"
#include "../core/endian_func.hpp"
#include "../vehicle_base.h"
#include "../company_func.h"
#include "../date_func.h"
#include "../autoreplace_base.h"
#include "../roadstop_base.h"
#include "../linkgraph/linkgraph.h"
#include "../linkgraph/linkgraphjob.h"
#include "../statusbar_gui.h"
#include "../fileio_func.h"
#include "../gamelog.h"
#include "../string_func.h"
#include "../fios.h"
#include "../error.h"
#include <atomic>
#include <deque>
#include <vector>
#include <string>
#ifdef __EMSCRIPTEN__
#	include <emscripten.h>
#endif

#include "table/strings.h"

#include "saveload_internal.h"
#include "saveload_filter.h"

#include "../safeguards.h"

extern const SaveLoadVersion SAVEGAME_VERSION = (SaveLoadVersion)(SL_MAX_VERSION - 1); ///< Current savegame version of OpenTTD.

SavegameType _savegame_type; ///< type of savegame we are loading
FileToSaveLoad _file_to_saveload; ///< File to save or load in the openttd loop.

uint32 _ttdp_version;         ///< version of TTDP savegame (if applicable)
SaveLoadVersion _sl_version;  ///< the major savegame version identifier
byte   _sl_minor_version;     ///< the minor savegame version, DO NOT USE!
std::string _savegame_format; ///< how to compress savegames
bool _do_autosave;            ///< are we doing an autosave at the moment?

/** What are we currently doing? */
enum SaveLoadAction {
	SLA_LOAD,        ///< loading
	SLA_SAVE,        ///< saving
	SLA_PTRS,        ///< fixing pointers
	SLA_NULL,        ///< null all pointers (on loading error)
	SLA_LOAD_CHECK,  ///< partial loading into #_load_check_data
};

enum NeedLength {
	NL_NONE = 0,       ///< not working in NeedLength mode
	NL_WANTLENGTH = 1, ///< writing length and data
	NL_CALCLENGTH = 2, ///< need to calculate the length
};

/** Save in chunks of 128 KiB. */
static const size_t MEMORY_CHUNK_SIZE = 128 * 1024;

/** A buffer for reading (and buffering) savegame data. */
struct ReadBuffer {
	byte buf[MEMORY_CHUNK_SIZE]; ///< Buffer we're going to read from.
	byte *bufp;                  ///< Location we're at reading the buffer.
	byte *bufe;                  ///< End of the buffer we can read from.
	LoadFilter *reader;          ///< The filter used to actually read.
	size_t read;                 ///< The amount of read bytes so far from the filter.

	/**
	 * Initialise our variables.
	 * @param reader The filter to actually read data.
	 */
	ReadBuffer(LoadFilter *reader) : bufp(nullptr), bufe(nullptr), reader(reader), read(0)
	{
	}

	inline byte ReadByte()
	{
		if (this->bufp == this->bufe) {
			size_t len = this->reader->Read(this->buf, lengthof(this->buf));
			if (len == 0) SlErrorCorrupt("Unexpected end of chunk");

			this->read += len;
			this->bufp = this->buf;
			this->bufe = this->buf + len;
		}

		return *this->bufp++;
	}

	/**
	 * Get the size of the memory dump made so far.
	 * @return The size.
	 */
	size_t GetSize() const
	{
		return this->read - (this->bufe - this->bufp);
	}
};


/** Container for dumping the savegame (quickly) to memory. */
struct MemoryDumper {
	std::vector<byte *> blocks; ///< Buffer with blocks of allocated memory.
	byte *buf;                  ///< Buffer we're going to write to.
	byte *bufe;                 ///< End of the buffer we write to.

	/** Initialise our variables. */
	MemoryDumper() : buf(nullptr), bufe(nullptr)
	{
	}

	~MemoryDumper()
	{
		for (auto p : this->blocks) {
			free(p);
		}
	}

	/**
	 * Write a single byte into the dumper.
	 * @param b The byte to write.
	 */
	inline void WriteByte(byte b)
	{
		/* Are we at the end of this chunk? */
		if (this->buf == this->bufe) {
			this->buf = CallocT<byte>(MEMORY_CHUNK_SIZE);
			this->blocks.push_back(this->buf);
			this->bufe = this->buf + MEMORY_CHUNK_SIZE;
		}

		*this->buf++ = b;
	}

	/**
	 * Flush this dumper into a writer.
	 * @param writer The filter we want to use.
	 */
	void Flush(SaveFilter *writer)
	{
		uint i = 0;
		size_t t = this->GetSize();

		while (t > 0) {
			size_t to_write = std::min(MEMORY_CHUNK_SIZE, t);

			writer->Write(this->blocks[i++], to_write);
			t -= to_write;
		}

		writer->Finish();
	}

	/**
	 * Get the size of the memory dump made so far.
	 * @return The size.
	 */
	size_t GetSize() const
	{
		return this->blocks.size() * MEMORY_CHUNK_SIZE - (this->bufe - this->buf);
	}
};

/** The saveload struct, containing reader-writer functions, buffer, version, etc. */
struct SaveLoadParams {
	SaveLoadAction action;               ///< are we doing a save or a load atm.
	NeedLength need_length;              ///< working in NeedLength (Autolength) mode?
	byte block_mode;                     ///< ???
	bool error;                          ///< did an error occur or not

	size_t obj_len;                      ///< the length of the current object we are busy with
	int array_index, last_array_index;   ///< in the case of an array, the current and last positions
	bool expect_table_header;            ///< In the case of a table, if the header is saved/loaded.

	MemoryDumper *dumper;                ///< Memory dumper to write the savegame to.
	SaveFilter *sf;                      ///< Filter to write the savegame to.

	ReadBuffer *reader;                  ///< Savegame reading buffer.
	LoadFilter *lf;                      ///< Filter to read the savegame from.

	StringID error_str;                  ///< the translatable error message to show
	char *extra_msg;                     ///< the error message

	uint16 game_speed;                   ///< The game speed when saving started.
	bool saveinprogress;                 ///< Whether there is currently a save in progress.
};

static SaveLoadParams _sl; ///< Parameters used for/at saveload.

static const std::vector<ChunkHandlerRef> &ChunkHandlers()
{
	/* These define the chunks */
	extern const ChunkHandlerTable _gamelog_chunk_handlers;
	extern const ChunkHandlerTable _map_chunk_handlers;
	extern const ChunkHandlerTable _misc_chunk_handlers;
	extern const ChunkHandlerTable _name_chunk_handlers;
	extern const ChunkHandlerTable _cheat_chunk_handlers;
	extern const ChunkHandlerTable _setting_chunk_handlers;
	extern const ChunkHandlerTable _company_chunk_handlers;
	extern const ChunkHandlerTable _engine_chunk_handlers;
	extern const ChunkHandlerTable _veh_chunk_handlers;
	extern const ChunkHandlerTable _waypoint_chunk_handlers;
	extern const ChunkHandlerTable _depot_chunk_handlers;
	extern const ChunkHandlerTable _order_chunk_handlers;
	extern const ChunkHandlerTable _town_chunk_handlers;
	extern const ChunkHandlerTable _sign_chunk_handlers;
	extern const ChunkHandlerTable _station_chunk_handlers;
	extern const ChunkHandlerTable _industry_chunk_handlers;
	extern const ChunkHandlerTable _economy_chunk_handlers;
	extern const ChunkHandlerTable _subsidy_chunk_handlers;
	extern const ChunkHandlerTable _cargomonitor_chunk_handlers;
	extern const ChunkHandlerTable _goal_chunk_handlers;
	extern const ChunkHandlerTable _story_page_chunk_handlers;
	extern const ChunkHandlerTable _ai_chunk_handlers;
	extern const ChunkHandlerTable _game_chunk_handlers;
	extern const ChunkHandlerTable _animated_tile_chunk_handlers;
	extern const ChunkHandlerTable _newgrf_chunk_handlers;
	extern const ChunkHandlerTable _group_chunk_handlers;
	extern const ChunkHandlerTable _cargopacket_chunk_handlers;
	extern const ChunkHandlerTable _autoreplace_chunk_handlers;
	extern const ChunkHandlerTable _labelmaps_chunk_handlers;
	extern const ChunkHandlerTable _linkgraph_chunk_handlers;
	extern const ChunkHandlerTable _airport_chunk_handlers;
	extern const ChunkHandlerTable _object_chunk_handlers;
	extern const ChunkHandlerTable _persistent_storage_chunk_handlers;

	/** List of all chunks in a savegame. */
	static const ChunkHandlerTable _chunk_handler_tables[] = {
		_gamelog_chunk_handlers,
		_map_chunk_handlers,
		_misc_chunk_handlers,
		_name_chunk_handlers,
		_cheat_chunk_handlers,
		_setting_chunk_handlers,
		_veh_chunk_handlers,
		_waypoint_chunk_handlers,
		_depot_chunk_handlers,
		_order_chunk_handlers,
		_industry_chunk_handlers,
		_economy_chunk_handlers,
		_subsidy_chunk_handlers,
		_cargomonitor_chunk_handlers,
		_goal_chunk_handlers,
		_story_page_chunk_handlers,
		_engine_chunk_handlers,
		_town_chunk_handlers,
		_sign_chunk_handlers,
		_station_chunk_handlers,
		_company_chunk_handlers,
		_ai_chunk_handlers,
		_game_chunk_handlers,
		_animated_tile_chunk_handlers,
		_newgrf_chunk_handlers,
		_group_chunk_handlers,
		_cargopacket_chunk_handlers,
		_autoreplace_chunk_handlers,
		_labelmaps_chunk_handlers,
		_linkgraph_chunk_handlers,
		_airport_chunk_handlers,
		_object_chunk_handlers,
		_persistent_storage_chunk_handlers,
	};

	static std::vector<ChunkHandlerRef> _chunk_handlers;

	if (_chunk_handlers.empty()) {
		for (auto &chunk_handler_table : _chunk_handler_tables) {
			for (auto &chunk_handler : chunk_handler_table) {
				_chunk_handlers.push_back(chunk_handler);
			}
		}
	}

	return _chunk_handlers;
}

/** Null all pointers (convert index -> nullptr) */
static void SlNullPointers()
{
	_sl.action = SLA_NULL;

	/* We don't want any savegame conversion code to run
	 * during NULLing; especially those that try to get
	 * pointers from other pools. */
	_sl_version = SAVEGAME_VERSION;

	for (const ChunkHandler &ch : ChunkHandlers()) {
		Debug(sl, 3, "Nulling pointers for {:c}{:c}{:c}{:c}", ch.id >> 24, ch.id >> 16, ch.id >> 8, ch.id);
		ch.FixPointers();
	}

	assert(_sl.action == SLA_NULL);
}

/**
 * Error handler. Sets everything up to show an error message and to clean
 * up the mess of a partial savegame load.
 * @param string The translatable error message to show.
 * @param extra_msg An extra error message coming from one of the APIs.
 * @note This function does never return as it throws an exception to
 *       break out of all the saveload code.
 */
void NORETURN SlError(StringID string, const char *extra_msg)
{
	/* Distinguish between loading into _load_check_data vs. normal save/load. */
	if (_sl.action == SLA_LOAD_CHECK) {
		_load_check_data.error = string;
		free(_load_check_data.error_data);
		_load_check_data.error_data = (extra_msg == nullptr) ? nullptr : stredup(extra_msg);
	} else {
		_sl.error_str = string;
		free(_sl.extra_msg);
		_sl.extra_msg = (extra_msg == nullptr) ? nullptr : stredup(extra_msg);
	}

	/* We have to nullptr all pointers here; we might be in a state where
	 * the pointers are actually filled with indices, which means that
	 * when we access them during cleaning the pool dereferences of
	 * those indices will be made with segmentation faults as result. */
	if (_sl.action == SLA_LOAD || _sl.action == SLA_PTRS) SlNullPointers();

	/* Logging could be active. */
	GamelogStopAnyAction();

	throw std::exception();
}

/**
 * Error handler for corrupt savegames. Sets everything up to show the
 * error message and to clean up the mess of a partial savegame load.
 * @param msg Location the corruption has been spotted.
 * @note This function does never return as it throws an exception to
 *       break out of all the saveload code.
 */
void NORETURN SlErrorCorrupt(const char *msg)
{
	SlError(STR_GAME_SAVELOAD_ERROR_BROKEN_SAVEGAME, msg);
}

/**
 * Issue an SlErrorCorrupt with a format string.
 * @param format format string
 * @param ... arguments to format string
 * @note This function does never return as it throws an exception to
 *       break out of all the saveload code.
 */
void NORETURN SlErrorCorruptFmt(const char *format, ...)
{
	va_list ap;
	char msg[256];

	va_start(ap, format);
	vseprintf(msg, lastof(msg), format, ap);
	va_end(ap);

	SlErrorCorrupt(msg);
}


typedef void (*AsyncSaveFinishProc)();                      ///< Callback for when the savegame loading is finished.
static std::atomic<AsyncSaveFinishProc> _async_save_finish; ///< Callback to call when the savegame loading is finished.
static std::thread _save_thread;                            ///< The thread we're using to compress and write a savegame

/**
 * Called by save thread to tell we finished saving.
 * @param proc The callback to call when saving is done.
 */
static void SetAsyncSaveFinish(AsyncSaveFinishProc proc)
{
	if (_exit_game) return;
	while (_async_save_finish.load(std::memory_order_acquire) != nullptr) CSleep(10);

	_async_save_finish.store(proc, std::memory_order_release);
}

/**
 * Handle async save finishes.
 */
void ProcessAsyncSaveFinish()
{
	AsyncSaveFinishProc proc = _async_save_finish.exchange(nullptr, std::memory_order_acq_rel);
	if (proc == nullptr) return;

	proc();

	if (_save_thread.joinable()) {
		_save_thread.join();
	}
}

/**
 * Wrapper for reading a byte from the buffer.
 * @return The read byte.
 */
byte SlReadByte()
{
	return _sl.reader->ReadByte();
}

/**
 * Wrapper for writing a byte to the dumper.
 * @param b The byte to write.
 */
void SlWriteByte(byte b)
{
	_sl.dumper->WriteByte(b);
}

static inline int SlReadUint16()
{
	int x = SlReadByte() << 8;
	return x | SlReadByte();
}

static inline uint32 SlReadUint32()
{
	uint32 x = SlReadUint16() << 16;
	return x | SlReadUint16();
}

static inline uint64 SlReadUint64()
{
	uint32 x = SlReadUint32();
	uint32 y = SlReadUint32();
	return (uint64)x << 32 | y;
}

static inline void SlWriteUint16(uint16 v)
{
	SlWriteByte(GB(v, 8, 8));
	SlWriteByte(GB(v, 0, 8));
}

static inline void SlWriteUint32(uint32 v)
{
	SlWriteUint16(GB(v, 16, 16));
	SlWriteUint16(GB(v,  0, 16));
}

static inline void SlWriteUint64(uint64 x)
{
	SlWriteUint32((uint32)(x >> 32));
	SlWriteUint32((uint32)x);
}

/**
 * Read in the header descriptor of an object or an array.
 * If the highest bit is set (7), then the index is bigger than 127
 * elements, so use the next byte to read in the real value.
 * The actual value is then both bytes added with the first shifted
 * 8 bits to the left, and dropping the highest bit (which only indicated a big index).
 * x = ((x & 0x7F) << 8) + SlReadByte();
 * @return Return the value of the index
 */
static uint SlReadSimpleGamma()
{
	uint i = SlReadByte();
	if (HasBit(i, 7)) {
		i &= ~0x80;
		if (HasBit(i, 6)) {
			i &= ~0x40;
			if (HasBit(i, 5)) {
				i &= ~0x20;
				if (HasBit(i, 4)) {
					i &= ~0x10;
					if (HasBit(i, 3)) {
						SlErrorCorrupt("Unsupported gamma");
					}
					i = SlReadByte(); // 32 bits only.
				}
				i = (i << 8) | SlReadByte();
			}
			i = (i << 8) | SlReadByte();
		}
		i = (i << 8) | SlReadByte();
	}
	return i;
}

/**
 * Write the header descriptor of an object or an array.
 * If the element is bigger than 127, use 2 bytes for saving
 * and use the highest byte of the first written one as a notice
 * that the length consists of 2 bytes, etc.. like this:
 * 0xxxxxxx
 * 10xxxxxx xxxxxxxx
 * 110xxxxx xxxxxxxx xxxxxxxx
 * 1110xxxx xxxxxxxx xxxxxxxx xxxxxxxx
 * 11110--- xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx
 * We could extend the scheme ad infinum to support arbitrarily
 * large chunks, but as sizeof(size_t) == 4 is still very common
 * we don't support anything above 32 bits. That's why in the last
 * case the 3 most significant bits are unused.
 * @param i Index being written
 */

static void SlWriteSimpleGamma(size_t i)
{
	if (i >= (1 << 7)) {
		if (i >= (1 << 14)) {
			if (i >= (1 << 21)) {
				if (i >= (1 << 28)) {
					assert(i <= UINT32_MAX); // We can only support 32 bits for now.
					SlWriteByte((byte)(0xF0));
					SlWriteByte((byte)(i >> 24));
				} else {
					SlWriteByte((byte)(0xE0 | (i >> 24)));
				}
				SlWriteByte((byte)(i >> 16));
			} else {
				SlWriteByte((byte)(0xC0 | (i >> 16)));
			}
			SlWriteByte((byte)(i >> 8));
		} else {
			SlWriteByte((byte)(0x80 | (i >> 8)));
		}
	}
	SlWriteByte((byte)i);
}

/** Return how many bytes used to encode a gamma value */
static inline uint SlGetGammaLength(size_t i)
{
	return 1 + (i >= (1 << 7)) + (i >= (1 << 14)) + (i >= (1 << 21)) + (i >= (1 << 28));
}

static inline uint SlReadSparseIndex()
{
	return SlReadSimpleGamma();
}

static inline void SlWriteSparseIndex(uint index)
{
	SlWriteSimpleGamma(index);
}

static inline uint SlReadArrayLength()
{
	return SlReadSimpleGamma();
}

static inline void SlWriteArrayLength(size_t length)
{
	SlWriteSimpleGamma(length);
}

static inline uint SlGetArrayLength(size_t length)
{
	return SlGetGammaLength(length);
}

/**
 * Return the type as saved/loaded inside the savegame.
 */
static uint8 GetSavegameFileType(const SaveLoad &sld)
{
	switch (sld.cmd) {
		case SL_VAR:
			return GetVarFileType(sld.conv); break;

		case SL_STR:
		case SL_STDSTR:
		case SL_ARR:
		case SL_VECTOR:
		case SL_DEQUE:
			return GetVarFileType(sld.conv) | SLE_FILE_HAS_LENGTH_FIELD; break;

		case SL_REF:
			return IsSavegameVersionBefore(SLV_69) ? SLE_FILE_U16 : SLE_FILE_U32;

		case SL_REFLIST:
			return (IsSavegameVersionBefore(SLV_69) ? SLE_FILE_U16 : SLE_FILE_U32) | SLE_FILE_HAS_LENGTH_FIELD;

		case SL_SAVEBYTE:
			return SLE_FILE_U8;

		case SL_STRUCT:
		case SL_STRUCTLIST:
			return SLE_FILE_STRUCT | SLE_FILE_HAS_LENGTH_FIELD;

		default: NOT_REACHED();
	}
}

/**
 * Return the size in bytes of a certain type of normal/atomic variable
 * as it appears in memory. See VarTypes
 * @param conv VarType type of variable that is used for calculating the size
 * @return Return the size of this type in bytes
 */
static inline uint SlCalcConvMemLen(VarType conv)
{
	static const byte conv_mem_size[] = {1, 1, 1, 2, 2, 4, 4, 8, 8, 0};

	switch (GetVarMemType(conv)) {
		case SLE_VAR_STRB:
		case SLE_VAR_STR:
		case SLE_VAR_STRQ:
			return SlReadArrayLength();

		default:
			uint8 type = GetVarMemType(conv) >> 4;
			assert(type < lengthof(conv_mem_size));
			return conv_mem_size[type];
	}
}

/**
 * Return the size in bytes of a certain type of normal/atomic variable
 * as it appears in a saved game. See VarTypes
 * @param conv VarType type of variable that is used for calculating the size
 * @return Return the size of this type in bytes
 */
static inline byte SlCalcConvFileLen(VarType conv)
{
	static const byte conv_file_size[] = {0, 1, 1, 2, 2, 4, 4, 8, 8, 2};

	uint8 type = GetVarFileType(conv);
	assert(type < lengthof(conv_file_size));
	return conv_file_size[type];
}

/** Return the size in bytes of a reference (pointer) */
static inline size_t SlCalcRefLen()
{
	return IsSavegameVersionBefore(SLV_69) ? 2 : 4;
}

void SlSetArrayIndex(uint index)
{
	_sl.need_length = NL_WANTLENGTH;
	_sl.array_index = index;
}

static size_t _next_offs;

/**
 * Iterate through the elements of an array and read the whole thing
 * @return The index of the object, or -1 if we have reached the end of current block
 */
int SlIterateArray()
{
	int index;

	/* After reading in the whole array inside the loop
	 * we must have read in all the data, so we must be at end of current block. */
	if (_next_offs != 0 && _sl.reader->GetSize() != _next_offs) SlErrorCorrupt("Invalid chunk size");

	for (;;) {
		uint length = SlReadArrayLength();
		if (length == 0) {
			assert(!_sl.expect_table_header);
			_next_offs = 0;
			return -1;
		}

		_sl.obj_len = --length;
		_next_offs = _sl.reader->GetSize() + length;

		if (_sl.expect_table_header) {
			_sl.expect_table_header = false;
			return INT32_MAX;
		}

		switch (_sl.block_mode) {
			case CH_SPARSE_TABLE:
			case CH_SPARSE_ARRAY: index = (int)SlReadSparseIndex(); break;
			case CH_TABLE:
			case CH_ARRAY:        index = _sl.array_index++; break;
			default:
				Debug(sl, 0, "SlIterateArray error");
				return -1; // error
		}

		if (length != 0) return index;
	}
}

/**
 * Skip an array or sparse array
 */
void SlSkipArray()
{
	while (SlIterateArray() != -1) {
		SlSkipBytes(_next_offs - _sl.reader->GetSize());
	}
}

/**
 * Sets the length of either a RIFF object or the number of items in an array.
 * This lets us load an object or an array of arbitrary size
 * @param length The length of the sought object/array
 */
void SlSetLength(size_t length)
{
	assert(_sl.action == SLA_SAVE);

	switch (_sl.need_length) {
		case NL_WANTLENGTH:
			_sl.need_length = NL_NONE;
			if ((_sl.block_mode == CH_TABLE || _sl.block_mode == CH_SPARSE_TABLE) && _sl.expect_table_header) {
				_sl.expect_table_header = false;
				SlWriteArrayLength(length + 1);
				break;
			}

			switch (_sl.block_mode) {
				case CH_RIFF:
					/* Ugly encoding of >16M RIFF chunks
					 * The lower 24 bits are normal
					 * The uppermost 4 bits are bits 24:27 */
					assert(length < (1 << 28));
					SlWriteUint32((uint32)((length & 0xFFFFFF) | ((length >> 24) << 28)));
					break;
				case CH_TABLE:
				case CH_ARRAY:
					assert(_sl.last_array_index <= _sl.array_index);
					while (++_sl.last_array_index <= _sl.array_index) {
						SlWriteArrayLength(1);
					}
					SlWriteArrayLength(length + 1);
					break;
				case CH_SPARSE_TABLE:
				case CH_SPARSE_ARRAY:
					SlWriteArrayLength(length + 1 + SlGetArrayLength(_sl.array_index)); // Also include length of sparse index.
					SlWriteSparseIndex(_sl.array_index);
					break;
				default: NOT_REACHED();
			}
			break;

		case NL_CALCLENGTH:
			_sl.obj_len += (int)length;
			break;

		default: NOT_REACHED();
	}
}

/**
 * Save/Load bytes. These do not need to be converted to Little/Big Endian
 * so directly write them or read them to/from file
 * @param ptr The source or destination of the object being manipulated
 * @param length number of bytes this fast CopyBytes lasts
 */
static void SlCopyBytes(void *ptr, size_t length)
{
	byte *p = (byte *)ptr;

	switch (_sl.action) {
		case SLA_LOAD_CHECK:
		case SLA_LOAD:
			for (; length != 0; length--) *p++ = SlReadByte();
			break;
		case SLA_SAVE:
			for (; length != 0; length--) SlWriteByte(*p++);
			break;
		default: NOT_REACHED();
	}
}

/** Get the length of the current object */
size_t SlGetFieldLength()
{
	return _sl.obj_len;
}

/**
 * Return a signed-long version of the value of a setting
 * @param ptr pointer to the variable
 * @param conv type of variable, can be a non-clean
 * type, eg one with other flags because it is parsed
 * @return returns the value of the pointer-setting
 */
int64 ReadValue(const void *ptr, VarType conv)
{
	switch (GetVarMemType(conv)) {
		case SLE_VAR_BL:  return (*(const bool *)ptr != 0);
		case SLE_VAR_I8:  return *(const int8  *)ptr;
		case SLE_VAR_U8:  return *(const byte  *)ptr;
		case SLE_VAR_I16: return *(const int16 *)ptr;
		case SLE_VAR_U16: return *(const uint16*)ptr;
		case SLE_VAR_I32: return *(const int32 *)ptr;
		case SLE_VAR_U32: return *(const uint32*)ptr;
		case SLE_VAR_I64: return *(const int64 *)ptr;
		case SLE_VAR_U64: return *(const uint64*)ptr;
		case SLE_VAR_NULL:return 0;
		default: NOT_REACHED();
	}
}

/**
 * Write the value of a setting
 * @param ptr pointer to the variable
 * @param conv type of variable, can be a non-clean type, eg
 *             with other flags. It is parsed upon read
 * @param val the new value being given to the variable
 */
void WriteValue(void *ptr, VarType conv, int64 val)
{
	switch (GetVarMemType(conv)) {
		case SLE_VAR_BL:  *(bool  *)ptr = (val != 0);  break;
		case SLE_VAR_I8:  *(int8  *)ptr = val; break;
		case SLE_VAR_U8:  *(byte  *)ptr = val; break;
		case SLE_VAR_I16: *(int16 *)ptr = val; break;
		case SLE_VAR_U16: *(uint16*)ptr = val; break;
		case SLE_VAR_I32: *(int32 *)ptr = val; break;
		case SLE_VAR_U32: *(uint32*)ptr = val; break;
		case SLE_VAR_I64: *(int64 *)ptr = val; break;
		case SLE_VAR_U64: *(uint64*)ptr = val; break;
		case SLE_VAR_NAME: *reinterpret_cast<std::string *>(ptr) = CopyFromOldName(val); break;
		case SLE_VAR_NULL: break;
		default: NOT_REACHED();
	}
}

/**
 * Handle all conversion and typechecking of variables here.
 * In the case of saving, read in the actual value from the struct
 * and then write them to file, endian safely. Loading a value
 * goes exactly the opposite way
 * @param ptr The object being filled/read
 * @param conv VarType type of the current element of the struct
 */
static void SlSaveLoadConv(void *ptr, VarType conv)
{
	switch (_sl.action) {
		case SLA_SAVE: {
			int64 x = ReadValue(ptr, conv);

			/* Write the value to the file and check if its value is in the desired range */
			switch (GetVarFileType(conv)) {
				case SLE_FILE_I8: assert(x >= -128 && x <= 127);     SlWriteByte(x);break;
				case SLE_FILE_U8: assert(x >= 0 && x <= 255);        SlWriteByte(x);break;
				case SLE_FILE_I16:assert(x >= -32768 && x <= 32767); SlWriteUint16(x);break;
				case SLE_FILE_STRINGID:
				case SLE_FILE_U16:assert(x >= 0 && x <= 65535);      SlWriteUint16(x);break;
				case SLE_FILE_I32:
				case SLE_FILE_U32:                                   SlWriteUint32((uint32)x);break;
				case SLE_FILE_I64:
				case SLE_FILE_U64:                                   SlWriteUint64(x);break;
				default: NOT_REACHED();
			}
			break;
		}
		case SLA_LOAD_CHECK:
		case SLA_LOAD: {
			int64 x;
			/* Read a value from the file */
			switch (GetVarFileType(conv)) {
				case SLE_FILE_I8:  x = (int8  )SlReadByte();   break;
				case SLE_FILE_U8:  x = (byte  )SlReadByte();   break;
				case SLE_FILE_I16: x = (int16 )SlReadUint16(); break;
				case SLE_FILE_U16: x = (uint16)SlReadUint16(); break;
				case SLE_FILE_I32: x = (int32 )SlReadUint32(); break;
				case SLE_FILE_U32: x = (uint32)SlReadUint32(); break;
				case SLE_FILE_I64: x = (int64 )SlReadUint64(); break;
				case SLE_FILE_U64: x = (uint64)SlReadUint64(); break;
				case SLE_FILE_STRINGID: x = RemapOldStringID((uint16)SlReadUint16()); break;
				default: NOT_REACHED();
			}

			/* Write The value to the struct. These ARE endian safe. */
			WriteValue(ptr, conv, x);
			break;
		}
		case SLA_PTRS: break;
		case SLA_NULL: break;
		default: NOT_REACHED();
	}
}

/**
 * Calculate the net length of a string. This is in almost all cases
 * just strlen(), but if the string is not properly terminated, we'll
 * resort to the maximum length of the buffer.
 * @param ptr pointer to the stringbuffer
 * @param length maximum length of the string (buffer). If -1 we don't care
 * about a maximum length, but take string length as it is.
 * @return return the net length of the string
 */
static inline size_t SlCalcNetStringLen(const char *ptr, size_t length)
{
	if (ptr == nullptr) return 0;
	return std::min(strlen(ptr), length - 1);
}

/**
 * Calculate the gross length of the string that it
 * will occupy in the savegame. This includes the real length, returned
 * by SlCalcNetStringLen and the length that the index will occupy.
 * @param ptr pointer to the stringbuffer
 * @param length maximum length of the string (buffer size, etc.)
 * @param conv type of data been used
 * @return return the gross length of the string
 */
static inline size_t SlCalcStringLen(const void *ptr, size_t length, VarType conv)
{
	size_t len;
	const char *str;

	switch (GetVarMemType(conv)) {
		default: NOT_REACHED();
		case SLE_VAR_STR:
		case SLE_VAR_STRQ:
			str = *(const char * const *)ptr;
			len = SIZE_MAX;
			break;
		case SLE_VAR_STRB:
			str = (const char *)ptr;
			len = length;
			break;
	}

	len = SlCalcNetStringLen(str, len);
	return len + SlGetArrayLength(len); // also include the length of the index
}

/**
 * Calculate the gross length of the string that it
 * will occupy in the savegame. This includes the real length, returned
 * by SlCalcNetStringLen and the length that the index will occupy.
 * @param ptr Pointer to the \c std::string.
 * @return The gross length of the string.
 */
static inline size_t SlCalcStdStringLen(const void *ptr)
{
	const std::string *str = reinterpret_cast<const std::string *>(ptr);

	size_t len = str->length();
	return len + SlGetArrayLength(len); // also include the length of the index
}

/**
 * Save/Load a string.
 * @param ptr the string being manipulated
 * @param length of the string (full length)
 * @param conv must be SLE_FILE_STRING
 */
static void SlString(void *ptr, size_t length, VarType conv)
{
	switch (_sl.action) {
		case SLA_SAVE: {
			size_t len;
			switch (GetVarMemType(conv)) {
				default: NOT_REACHED();
				case SLE_VAR_STRB:
					len = SlCalcNetStringLen((char *)ptr, length);
					break;
				case SLE_VAR_STR:
				case SLE_VAR_STRQ:
					ptr = *(char **)ptr;
					len = SlCalcNetStringLen((char *)ptr, SIZE_MAX);
					break;
			}

			SlWriteArrayLength(len);
			SlCopyBytes(ptr, len);
			break;
		}
		case SLA_LOAD_CHECK:
		case SLA_LOAD: {
			size_t len = SlReadArrayLength();

			switch (GetVarMemType(conv)) {
				default: NOT_REACHED();
				case SLE_VAR_NULL:
					SlSkipBytes(len);
					return;
				case SLE_VAR_STRB:
					if (len >= length) {
						Debug(sl, 1, "String length in savegame is bigger than buffer, truncating");
						SlCopyBytes(ptr, length);
						SlSkipBytes(len - length);
						len = length - 1;
					} else {
						SlCopyBytes(ptr, len);
					}
					break;
				case SLE_VAR_STR:
				case SLE_VAR_STRQ: // Malloc'd string, free previous incarnation, and allocate
					free(*(char **)ptr);
					if (len == 0) {
						*(char **)ptr = nullptr;
						return;
					} else {
						*(char **)ptr = MallocT<char>(len + 1); // terminating '\0'
						ptr = *(char **)ptr;
						SlCopyBytes(ptr, len);
					}
					break;
			}

			((char *)ptr)[len] = '\0'; // properly terminate the string
			StringValidationSettings settings = SVS_REPLACE_WITH_QUESTION_MARK;
			if ((conv & SLF_ALLOW_CONTROL) != 0) {
				settings = settings | SVS_ALLOW_CONTROL_CODE;
				if (IsSavegameVersionBefore(SLV_169)) {
					str_fix_scc_encoded((char *)ptr, (char *)ptr + len);
				}
			}
			if ((conv & SLF_ALLOW_NEWLINE) != 0) {
				settings = settings | SVS_ALLOW_NEWLINE;
			}
			StrMakeValidInPlace((char *)ptr, (char *)ptr + len, settings);
			break;
		}
		case SLA_PTRS: break;
		case SLA_NULL: break;
		default: NOT_REACHED();
	}
}

/**
 * Save/Load a \c std::string.
 * @param ptr the string being manipulated
 * @param conv must be SLE_FILE_STRING
 */
static void SlStdString(void *ptr, VarType conv)
{
	std::string *str = reinterpret_cast<std::string *>(ptr);

	switch (_sl.action) {
		case SLA_SAVE: {
			size_t len = str->length();
			SlWriteArrayLength(len);
			SlCopyBytes(const_cast<void *>(static_cast<const void *>(str->c_str())), len);
			break;
		}

		case SLA_LOAD_CHECK:
		case SLA_LOAD: {
			size_t len = SlReadArrayLength();
			if (GetVarMemType(conv) == SLE_VAR_NULL) {
				SlSkipBytes(len);
				return;
			}

			char *buf = AllocaM(char, len + 1);
			SlCopyBytes(buf, len);
			buf[len] = '\0'; // properly terminate the string

			StringValidationSettings settings = SVS_REPLACE_WITH_QUESTION_MARK;
			if ((conv & SLF_ALLOW_CONTROL) != 0) {
				settings = settings | SVS_ALLOW_CONTROL_CODE;
				if (IsSavegameVersionBefore(SLV_169)) {
					str_fix_scc_encoded(buf, buf + len);
				}
			}
			if ((conv & SLF_ALLOW_NEWLINE) != 0) {
				settings = settings | SVS_ALLOW_NEWLINE;
			}
			StrMakeValidInPlace(buf, buf + len, settings);

			// Store sanitized string.
			str->assign(buf);
		}

		case SLA_PTRS: break;
		case SLA_NULL: break;
		default: NOT_REACHED();
	}
}

/**
 * Internal function to save/Load a list of SL_VARs.
 * SlCopy() and SlArray() are very similar, with the exception of the header.
 * This function represents the common part.
 * @param object The object being manipulated.
 * @param length The length of the object in elements
 * @param conv VarType type of the items.
 */
static void SlCopyInternal(void *object, size_t length, VarType conv)
{
	if (GetVarMemType(conv) == SLE_VAR_NULL) {
		assert(_sl.action != SLA_SAVE); // Use SL_NULL if you want to write null-bytes
		SlSkipBytes(length * SlCalcConvFileLen(conv));
		return;
	}

	/* NOTICE - handle some buggy stuff, in really old versions everything was saved
	 * as a byte-type. So detect this, and adjust object size accordingly */
	if (_sl.action != SLA_SAVE && _sl_version == 0) {
		/* all objects except difficulty settings */
		if (conv == SLE_INT16 || conv == SLE_UINT16 || conv == SLE_STRINGID ||
				conv == SLE_INT32 || conv == SLE_UINT32) {
			SlCopyBytes(object, length * SlCalcConvFileLen(conv));
			return;
		}
		/* used for conversion of Money 32bit->64bit */
		if (conv == (SLE_FILE_I32 | SLE_VAR_I64)) {
			for (uint i = 0; i < length; i++) {
				((int64*)object)[i] = (int32)BSWAP32(SlReadUint32());
			}
			return;
		}
	}

	/* If the size of elements is 1 byte both in file and memory, no special
	 * conversion is needed, use specialized copy-copy function to speed up things */
	if (conv == SLE_INT8 || conv == SLE_UINT8) {
		SlCopyBytes(object, length);
	} else {
		byte *a = (byte*)object;
		byte mem_size = SlCalcConvMemLen(conv);

		for (; length != 0; length --) {
			SlSaveLoadConv(a, conv);
			a += mem_size; // get size
		}
	}
}

/**
 * Copy a list of SL_VARs to/from a savegame.
 * These entries are copied as-is, and you as caller have to make sure things
 * like length-fields are calculated correctly.
 * @param object The object being manipulated.
 * @param length The length of the object in elements
 * @param conv VarType type of the items.
 */
void SlCopy(void *object, size_t length, VarType conv)
{
	if (_sl.action == SLA_PTRS || _sl.action == SLA_NULL) return;

	/* Automatically calculate the length? */
	if (_sl.need_length != NL_NONE) {
		SlSetLength(length * SlCalcConvFileLen(conv));
		/* Determine length only? */
		if (_sl.need_length == NL_CALCLENGTH) return;
	}

	SlCopyInternal(object, length, conv);
}

/**
 * Return the size in bytes of a certain type of atomic array
 * @param length The length of the array counted in elements
 * @param conv VarType type of the variable that is used in calculating the size
 */
static inline size_t SlCalcArrayLen(size_t length, VarType conv)
{
	return SlCalcConvFileLen(conv) * length + SlGetArrayLength(length);
}

/**
 * Save/Load the length of the array followed by the array of SL_VAR elements.
 * @param array The array being manipulated
 * @param length The length of the array in elements
 * @param conv VarType type of the atomic array (int, byte, uint64, etc.)
 */
static void SlArray(void *array, size_t length, VarType conv)
{
	switch (_sl.action) {
		case SLA_SAVE:
			SlWriteArrayLength(length);
			SlCopyInternal(array, length, conv);
			return;

		case SLA_LOAD_CHECK:
		case SLA_LOAD: {
			if (!IsSavegameVersionBefore(SLV_SAVELOAD_LIST_LENGTH)) {
				size_t sv_length = SlReadArrayLength();
				if (GetVarMemType(conv) == SLE_VAR_NULL) {
					/* We don't know this field, so we assume the length in the savegame is correct. */
					length = sv_length;
				} else if (sv_length != length) {
					/* If the SLE_ARR changes size, a savegame bump is required
					 * and the developer should have written conversion lines.
					 * Error out to make this more visible. */
					SlErrorCorrupt("Fixed-length array is of wrong length");
				}
			}

			SlCopyInternal(array, length, conv);
			return;
		}

		case SLA_PTRS:
		case SLA_NULL:
			return;

		default:
			NOT_REACHED();
	}
}

/**
 * Pointers cannot be saved to a savegame, so this functions gets
 * the index of the item, and if not available, it hussles with
 * pointers (looks really bad :()
 * Remember that a nullptr item has value 0, and all
 * indices have +1, so vehicle 0 is saved as index 1.
 * @param obj The object that we want to get the index of
 * @param rt SLRefType type of the object the index is being sought of
 * @return Return the pointer converted to an index of the type pointed to
 */
static size_t ReferenceToInt(const void *obj, SLRefType rt)
{
	assert(_sl.action == SLA_SAVE);

	if (obj == nullptr) return 0;

	switch (rt) {
		case REF_VEHICLE_OLD: // Old vehicles we save as new ones
		case REF_VEHICLE:   return ((const  Vehicle*)obj)->index + 1;
		case REF_STATION:   return ((const  Station*)obj)->index + 1;
		case REF_TOWN:      return ((const     Town*)obj)->index + 1;
		case REF_ORDER:     return ((const    Order*)obj)->index + 1;
		case REF_ROADSTOPS: return ((const RoadStop*)obj)->index + 1;
		case REF_ENGINE_RENEWS:  return ((const       EngineRenew*)obj)->index + 1;
		case REF_CARGO_PACKET:   return ((const       CargoPacket*)obj)->index + 1;
		case REF_ORDERLIST:      return ((const         OrderList*)obj)->index + 1;
		case REF_STORAGE:        return ((const PersistentStorage*)obj)->index + 1;
		case REF_LINK_GRAPH:     return ((const         LinkGraph*)obj)->index + 1;
		case REF_LINK_GRAPH_JOB: return ((const      LinkGraphJob*)obj)->index + 1;
		default: NOT_REACHED();
	}
}

/**
 * Pointers cannot be loaded from a savegame, so this function
 * gets the index from the savegame and returns the appropriate
 * pointer from the already loaded base.
 * Remember that an index of 0 is a nullptr pointer so all indices
 * are +1 so vehicle 0 is saved as 1.
 * @param index The index that is being converted to a pointer
 * @param rt SLRefType type of the object the pointer is sought of
 * @return Return the index converted to a pointer of any type
 */
static void *IntToReference(size_t index, SLRefType rt)
{
	static_assert(sizeof(size_t) <= sizeof(void *));

	assert(_sl.action == SLA_PTRS);

	/* After version 4.3 REF_VEHICLE_OLD is saved as REF_VEHICLE,
	 * and should be loaded like that */
	if (rt == REF_VEHICLE_OLD && !IsSavegameVersionBefore(SLV_4, 4)) {
		rt = REF_VEHICLE;
	}

	/* No need to look up nullptr pointers, just return immediately */
	if (index == (rt == REF_VEHICLE_OLD ? 0xFFFF : 0)) return nullptr;

	/* Correct index. Old vehicles were saved differently:
	 * invalid vehicle was 0xFFFF, now we use 0x0000 for everything invalid. */
	if (rt != REF_VEHICLE_OLD) index--;

	switch (rt) {
		case REF_ORDERLIST:
			if (OrderList::IsValidID(index)) return OrderList::Get(index);
			SlErrorCorrupt("Referencing invalid OrderList");

		case REF_ORDER:
			if (Order::IsValidID(index)) return Order::Get(index);
			/* in old versions, invalid order was used to mark end of order list */
			if (IsSavegameVersionBefore(SLV_5, 2)) return nullptr;
			SlErrorCorrupt("Referencing invalid Order");

		case REF_VEHICLE_OLD:
		case REF_VEHICLE:
			if (Vehicle::IsValidID(index)) return Vehicle::Get(index);
			SlErrorCorrupt("Referencing invalid Vehicle");

		case REF_STATION:
			if (Station::IsValidID(index)) return Station::Get(index);
			SlErrorCorrupt("Referencing invalid Station");

		case REF_TOWN:
			if (Town::IsValidID(index)) return Town::Get(index);
			SlErrorCorrupt("Referencing invalid Town");

		case REF_ROADSTOPS:
			if (RoadStop::IsValidID(index)) return RoadStop::Get(index);
			SlErrorCorrupt("Referencing invalid RoadStop");

		case REF_ENGINE_RENEWS:
			if (EngineRenew::IsValidID(index)) return EngineRenew::Get(index);
			SlErrorCorrupt("Referencing invalid EngineRenew");

		case REF_CARGO_PACKET:
			if (CargoPacket::IsValidID(index)) return CargoPacket::Get(index);
			SlErrorCorrupt("Referencing invalid CargoPacket");

		case REF_STORAGE:
			if (PersistentStorage::IsValidID(index)) return PersistentStorage::Get(index);
			SlErrorCorrupt("Referencing invalid PersistentStorage");

		case REF_LINK_GRAPH:
			if (LinkGraph::IsValidID(index)) return LinkGraph::Get(index);
			SlErrorCorrupt("Referencing invalid LinkGraph");

		case REF_LINK_GRAPH_JOB:
			if (LinkGraphJob::IsValidID(index)) return LinkGraphJob::Get(index);
			SlErrorCorrupt("Referencing invalid LinkGraphJob");

		default: NOT_REACHED();
	}
}

/**
 * Handle conversion for references.
 * @param ptr The object being filled/read.
 * @param conv VarType type of the current element of the struct.
 */
void SlSaveLoadRef(void *ptr, VarType conv)
{
	switch (_sl.action) {
		case SLA_SAVE:
			SlWriteUint32((uint32)ReferenceToInt(*(void **)ptr, (SLRefType)conv));
			break;
		case SLA_LOAD_CHECK:
		case SLA_LOAD:
			*(size_t *)ptr = IsSavegameVersionBefore(SLV_69) ? SlReadUint16() : SlReadUint32();
			break;
		case SLA_PTRS:
			*(void **)ptr = IntToReference(*(size_t *)ptr, (SLRefType)conv);
			break;
		case SLA_NULL:
			*(void **)ptr = nullptr;
			break;
		default: NOT_REACHED();
	}
}

/**
 * Template class to help with list-like types.
 */
template <template<typename, typename> typename Tstorage, typename Tvar, typename Tallocator = std::allocator<Tvar>>
class SlStorageHelper {
	typedef Tstorage<Tvar, Tallocator> SlStorageT;
public:
	/**
	 * Internal templated helper to return the size in bytes of a list-like type.
	 * @param storage The storage to find the size of
	 * @param conv VarType type of variable that is used for calculating the size
	 * @param cmd The SaveLoadType ware are saving/loading.
	 */
	static size_t SlCalcLen(const void *storage, VarType conv, SaveLoadType cmd = SL_VAR)
	{
		assert(cmd == SL_VAR || cmd == SL_REF);

		const SlStorageT *list = static_cast<const SlStorageT *>(storage);

		int type_size = SlGetArrayLength(list->size());
		int item_size = SlCalcConvFileLen(cmd == SL_VAR ? conv : (VarType)SLE_FILE_U32);
		return list->size() * item_size + type_size;
	}

	static void SlSaveLoadMember(SaveLoadType cmd, Tvar *item, VarType conv)
	{
		switch (cmd) {
			case SL_VAR: SlSaveLoadConv(item, conv); break;
			case SL_REF: SlSaveLoadRef(item, conv); break;
			default:
				NOT_REACHED();
		}
	}

	/**
	 * Internal templated helper to save/load a list-like type.
	 * @param storage The storage being manipulated.
	 * @param conv VarType type of variable that is used for calculating the size.
	 * @param cmd The SaveLoadType ware are saving/loading.
	 */
	static void SlSaveLoad(void *storage, VarType conv, SaveLoadType cmd = SL_VAR)
	{
		assert(cmd == SL_VAR || cmd == SL_REF);

		SlStorageT *list = static_cast<SlStorageT *>(storage);

		switch (_sl.action) {
			case SLA_SAVE:
				SlWriteArrayLength(list->size());

				for (auto &item : *list) {
					SlSaveLoadMember(cmd, &item, conv);
				}
				break;

			case SLA_LOAD_CHECK:
			case SLA_LOAD: {
				size_t length;
				switch (cmd) {
					case SL_VAR: length = IsSavegameVersionBefore(SLV_SAVELOAD_LIST_LENGTH) ? SlReadUint32() : SlReadArrayLength(); break;
					case SL_REF: length = IsSavegameVersionBefore(SLV_69) ? SlReadUint16() : IsSavegameVersionBefore(SLV_SAVELOAD_LIST_LENGTH) ? SlReadUint32() : SlReadArrayLength(); break;
					default: NOT_REACHED();
				}

				/* Load each value and push to the end of the storage. */
				for (size_t i = 0; i < length; i++) {
					Tvar &data = list->emplace_back();
					SlSaveLoadMember(cmd, &data, conv);
				}
				break;
			}

			case SLA_PTRS:
				for (auto &item : *list) {
					SlSaveLoadMember(cmd, &item, conv);
				}
				break;

			case SLA_NULL:
				list->clear();
				break;

			default: NOT_REACHED();
		}
	}
};

/**
 * Return the size in bytes of a list.
 * @param list The std::list to find the size of.
 * @param conv VarType type of variable that is used for calculating the size.
 */
static inline size_t SlCalcRefListLen(const void *list, VarType conv)
{
	return SlStorageHelper<std::list, void *>::SlCalcLen(list, conv, SL_REF);
}

/**
 * Save/Load a list.
 * @param list The list being manipulated.
 * @param conv VarType type of variable that is used for calculating the size.
 */
static void SlRefList(void *list, VarType conv)
{
	/* Automatically calculate the length? */
	if (_sl.need_length != NL_NONE) {
		SlSetLength(SlCalcRefListLen(list, conv));
		/* Determine length only? */
		if (_sl.need_length == NL_CALCLENGTH) return;
	}

	SlStorageHelper<std::list, void *>::SlSaveLoad(list, conv, SL_REF);
}

/**
 * Return the size in bytes of a std::deque.
 * @param deque The std::deque to find the size of
 * @param conv VarType type of variable that is used for calculating the size
 */
static inline size_t SlCalcDequeLen(const void *deque, VarType conv)
{
	switch (GetVarMemType(conv)) {
		case SLE_VAR_BL: return SlStorageHelper<std::deque, bool>::SlCalcLen(deque, conv);
		case SLE_VAR_I8: return SlStorageHelper<std::deque, int8>::SlCalcLen(deque, conv);
		case SLE_VAR_U8: return SlStorageHelper<std::deque, uint8>::SlCalcLen(deque, conv);
		case SLE_VAR_I16: return SlStorageHelper<std::deque, int16>::SlCalcLen(deque, conv);
		case SLE_VAR_U16: return SlStorageHelper<std::deque, uint16>::SlCalcLen(deque, conv);
		case SLE_VAR_I32: return SlStorageHelper<std::deque, int32>::SlCalcLen(deque, conv);
		case SLE_VAR_U32: return SlStorageHelper<std::deque, uint32>::SlCalcLen(deque, conv);
		case SLE_VAR_I64: return SlStorageHelper<std::deque, int64>::SlCalcLen(deque, conv);
		case SLE_VAR_U64: return SlStorageHelper<std::deque, uint64>::SlCalcLen(deque, conv);
		default: NOT_REACHED();
	}
}

/**
 * Save/load a std::deque.
 * @param deque The std::deque being manipulated
 * @param conv VarType type of variable that is used for calculating the size
 */
static void SlDeque(void *deque, VarType conv)
{
	switch (GetVarMemType(conv)) {
		case SLE_VAR_BL: SlStorageHelper<std::deque, bool>::SlSaveLoad(deque, conv); break;
		case SLE_VAR_I8: SlStorageHelper<std::deque, int8>::SlSaveLoad(deque, conv); break;
		case SLE_VAR_U8: SlStorageHelper<std::deque, uint8>::SlSaveLoad(deque, conv); break;
		case SLE_VAR_I16: SlStorageHelper<std::deque, int16>::SlSaveLoad(deque, conv); break;
		case SLE_VAR_U16: SlStorageHelper<std::deque, uint16>::SlSaveLoad(deque, conv); break;
		case SLE_VAR_I32: SlStorageHelper<std::deque, int32>::SlSaveLoad(deque, conv); break;
		case SLE_VAR_U32: SlStorageHelper<std::deque, uint32>::SlSaveLoad(deque, conv); break;
		case SLE_VAR_I64: SlStorageHelper<std::deque, int64>::SlSaveLoad(deque, conv); break;
		case SLE_VAR_U64: SlStorageHelper<std::deque, uint64>::SlSaveLoad(deque, conv); break;
		default: NOT_REACHED();
	}
}

/**
 * Return the size in bytes of a std::vector.
 * @param vector The std::vector to find the size of
 * @param conv VarType type of variable that is used for calculating the size
 */
static inline size_t SlCalcVectorLen(const void *vector, VarType conv)
{
	switch (GetVarMemType(conv)) {
		case SLE_VAR_BL: NOT_REACHED(); // Not supported
		case SLE_VAR_I8: return SlStorageHelper<std::vector, int8>::SlCalcLen(vector, conv);
		case SLE_VAR_U8: return SlStorageHelper<std::vector, uint8>::SlCalcLen(vector, conv);
		case SLE_VAR_I16: return SlStorageHelper<std::vector, int16>::SlCalcLen(vector, conv);
		case SLE_VAR_U16: return SlStorageHelper<std::vector, uint16>::SlCalcLen(vector, conv);
		case SLE_VAR_I32: return SlStorageHelper<std::vector, int32>::SlCalcLen(vector, conv);
		case SLE_VAR_U32: return SlStorageHelper<std::vector, uint32>::SlCalcLen(vector, conv);
		case SLE_VAR_I64: return SlStorageHelper<std::vector, int64>::SlCalcLen(vector, conv);
		case SLE_VAR_U64: return SlStorageHelper<std::vector, uint64>::SlCalcLen(vector, conv);
		default: NOT_REACHED();
	}
}

/**
 * Save/load a std::vector.
 * @param vector The std::vector being manipulated
 * @param conv VarType type of variable that is used for calculating the size
 */
static void SlVector(void *vector, VarType conv)
{
	switch (GetVarMemType(conv)) {
		case SLE_VAR_BL: NOT_REACHED(); // Not supported
		case SLE_VAR_I8: SlStorageHelper<std::vector, int8>::SlSaveLoad(vector, conv); break;
		case SLE_VAR_U8: SlStorageHelper<std::vector, uint8>::SlSaveLoad(vector, conv); break;
		case SLE_VAR_I16: SlStorageHelper<std::vector, int16>::SlSaveLoad(vector, conv); break;
		case SLE_VAR_U16: SlStorageHelper<std::vector, uint16>::SlSaveLoad(vector, conv); break;
		case SLE_VAR_I32: SlStorageHelper<std::vector, int32>::SlSaveLoad(vector, conv); break;
		case SLE_VAR_U32: SlStorageHelper<std::vector, uint32>::SlSaveLoad(vector, conv); break;
		case SLE_VAR_I64: SlStorageHelper<std::vector, int64>::SlSaveLoad(vector, conv); break;
		case SLE_VAR_U64: SlStorageHelper<std::vector, uint64>::SlSaveLoad(vector, conv); break;
		default: NOT_REACHED();
	}
}

/** Are we going to save this object or not? */
static inline bool SlIsObjectValidInSavegame(const SaveLoad &sld)
{
	return (_sl_version >= sld.version_from && _sl_version < sld.version_to);
}

/**
 * Calculate the size of the table header.
 * @param slt The SaveLoad table with objects to save/load.
 * @return size of given object.
 */
static size_t SlCalcTableHeader(const SaveLoadTable &slt)
{
	size_t length = 0;

	for (auto &sld : slt) {
		if (!SlIsObjectValidInSavegame(sld)) continue;

		length += SlCalcConvFileLen(SLE_UINT8);
		length += SlCalcStdStringLen(&sld.name);
	}

	length += SlCalcConvFileLen(SLE_UINT8); // End-of-list entry.

	for (auto &sld : slt) {
		if (!SlIsObjectValidInSavegame(sld)) continue;
		if (sld.cmd == SL_STRUCTLIST || sld.cmd == SL_STRUCT) {
			length += SlCalcTableHeader(sld.handler->GetDescription());
		}
	}

	return length;
}

/**
 * Calculate the size of an object.
 * @param object to be measured.
 * @param slt The SaveLoad table with objects to save/load.
 * @return size of given object.
 */
size_t SlCalcObjLength(const void *object, const SaveLoadTable &slt)
{
	size_t length = 0;

	/* Need to determine the length and write a length tag. */
	for (auto &sld : slt) {
		length += SlCalcObjMemberLength(object, sld);
	}
	return length;
}

size_t SlCalcObjMemberLength(const void *object, const SaveLoad &sld)
{
	assert(_sl.action == SLA_SAVE);

	if (!SlIsObjectValidInSavegame(sld)) return 0;

	switch (sld.cmd) {
		case SL_VAR: return SlCalcConvFileLen(sld.conv);
		case SL_REF: return SlCalcRefLen();
		case SL_ARR: return SlCalcArrayLen(sld.length, sld.conv);
		case SL_STR: return SlCalcStringLen(GetVariableAddress(object, sld), sld.length, sld.conv);
		case SL_REFLIST: return SlCalcRefListLen(GetVariableAddress(object, sld), sld.conv);
		case SL_DEQUE: return SlCalcDequeLen(GetVariableAddress(object, sld), sld.conv);
		case SL_VECTOR: return SlCalcVectorLen(GetVariableAddress(object, sld), sld.conv);
		case SL_STDSTR: return SlCalcStdStringLen(GetVariableAddress(object, sld));
		case SL_SAVEBYTE: return 1; // a byte is logically of size 1
		case SL_NULL: return SlCalcConvFileLen(sld.conv) * sld.length;

		case SL_STRUCT:
		case SL_STRUCTLIST: {
			NeedLength old_need_length = _sl.need_length;
			size_t old_obj_len = _sl.obj_len;

			_sl.need_length = NL_CALCLENGTH;
			_sl.obj_len = 0;

			/* Pretend that we are saving to collect the object size. Other
			 * means are difficult, as we don't know the length of the list we
			 * are about to store. */
			sld.handler->Save(const_cast<void *>(object));
			size_t length = _sl.obj_len;

			_sl.obj_len = old_obj_len;
			_sl.need_length = old_need_length;

			if (sld.cmd == SL_STRUCT) {
				length += SlGetArrayLength(1);
			}

			return length;
		}

		default: NOT_REACHED();
	}
	return 0;
}

/**
 * Check whether the variable size of the variable in the saveload configuration
 * matches with the actual variable size.
 * @param sld The saveload configuration to test.
 */
[[maybe_unused]] static bool IsVariableSizeRight(const SaveLoad &sld)
{
	if (GetVarMemType(sld.conv) == SLE_VAR_NULL) return true;

	switch (sld.cmd) {
		case SL_VAR:
			switch (GetVarMemType(sld.conv)) {
				case SLE_VAR_BL:
					return sld.size == sizeof(bool);
				case SLE_VAR_I8:
				case SLE_VAR_U8:
					return sld.size == sizeof(int8);
				case SLE_VAR_I16:
				case SLE_VAR_U16:
					return sld.size == sizeof(int16);
				case SLE_VAR_I32:
				case SLE_VAR_U32:
					return sld.size == sizeof(int32);
				case SLE_VAR_I64:
				case SLE_VAR_U64:
					return sld.size == sizeof(int64);
				case SLE_VAR_NAME:
					return sld.size == sizeof(std::string);
				default:
					return sld.size == sizeof(void *);
			}
		case SL_REF:
			/* These should all be pointer sized. */
			return sld.size == sizeof(void *);

		case SL_STR:
			/* These should be pointer sized, or fixed array. */
			return sld.size == sizeof(void *) || sld.size == sld.length;

		case SL_STDSTR:
			/* These should be all pointers to std::string. */
			return sld.size == sizeof(std::string);

		default:
			return true;
	}
}

static bool SlObjectMember(void *object, const SaveLoad &sld)
{
	assert(IsVariableSizeRight(sld));

	if (!SlIsObjectValidInSavegame(sld)) return false;

	VarType conv = GB(sld.conv, 0, 8);
	switch (sld.cmd) {
		case SL_VAR:
		case SL_REF:
		case SL_ARR:
		case SL_STR:
		case SL_REFLIST:
		case SL_DEQUE:
		case SL_VECTOR:
		case SL_STDSTR: {
			void *ptr = GetVariableAddress(object, sld);

			switch (sld.cmd) {
				case SL_VAR: SlSaveLoadConv(ptr, conv); break;
				case SL_REF: SlSaveLoadRef(ptr, conv); break;
				case SL_ARR: SlArray(ptr, sld.length, conv); break;
				case SL_STR: SlString(ptr, sld.length, sld.conv); break;
				case SL_REFLIST: SlRefList(ptr, conv); break;
				case SL_DEQUE: SlDeque(ptr, conv); break;
				case SL_VECTOR: SlVector(ptr, conv); break;
				case SL_STDSTR: SlStdString(ptr, sld.conv); break;
				default: NOT_REACHED();
			}
			break;
		}

		/* SL_SAVEBYTE writes a value to the savegame to identify the type of an object.
		 * When loading, the value is read explicitly with SlReadByte() to determine which
		 * object description to use. */
		case SL_SAVEBYTE: {
			void *ptr = GetVariableAddress(object, sld);

			switch (_sl.action) {
				case SLA_SAVE: SlWriteByte(*(uint8 *)ptr); break;
				case SLA_LOAD_CHECK:
				case SLA_LOAD:
				case SLA_PTRS:
				case SLA_NULL: break;
				default: NOT_REACHED();
			}
			break;
		}

		case SL_NULL: {
			assert(GetVarMemType(sld.conv) == SLE_VAR_NULL);

			switch (_sl.action) {
				case SLA_LOAD_CHECK:
				case SLA_LOAD: SlSkipBytes(SlCalcConvFileLen(sld.conv) * sld.length); break;
				case SLA_SAVE: for (int i = 0; i < SlCalcConvFileLen(sld.conv) * sld.length; i++) SlWriteByte(0); break;
				case SLA_PTRS:
				case SLA_NULL: break;
				default: NOT_REACHED();
			}
			break;
		}

		case SL_STRUCT:
		case SL_STRUCTLIST:
			switch (_sl.action) {
				case SLA_SAVE: {
					if (sld.cmd == SL_STRUCT) {
						/* Store in the savegame if this struct was written or not. */
						SlSetStructListLength(SlCalcObjMemberLength(object, sld) > SlGetArrayLength(1) ? 1 : 0);
					}
					sld.handler->Save(object);
					break;
				}

				case SLA_LOAD_CHECK: {
					if (sld.cmd == SL_STRUCT && !IsSavegameVersionBefore(SLV_SAVELOAD_LIST_LENGTH)) {
						SlGetStructListLength(1);
					}
					sld.handler->LoadCheck(object);
					break;
				}

				case SLA_LOAD: {
					if (sld.cmd == SL_STRUCT && !IsSavegameVersionBefore(SLV_SAVELOAD_LIST_LENGTH)) {
						SlGetStructListLength(1);
					}
					sld.handler->Load(object);
					break;
				}

				case SLA_PTRS:
					sld.handler->FixPointers(object);
					break;

				case SLA_NULL: break;
				default: NOT_REACHED();
			}
			break;

		default: NOT_REACHED();
	}
	return true;
}

/**
 * Set the length of this list.
 * @param The length of the list.
 */
void SlSetStructListLength(size_t length)
{
	/* Automatically calculate the length? */
	if (_sl.need_length != NL_NONE) {
		SlSetLength(SlGetArrayLength(length));
		if (_sl.need_length == NL_CALCLENGTH) return;
	}

	SlWriteArrayLength(length);
}

/**
 * Get the length of this list; if it exceeds the limit, error out.
 * @param limit The maximum size the list can be.
 * @return The length of the list.
 */
size_t SlGetStructListLength(size_t limit)
{
	size_t length = SlReadArrayLength();
	if (length > limit) SlErrorCorrupt("List exceeds storage size");

	return length;
}

/**
 * Main SaveLoad function.
 * @param object The object that is being saved or loaded.
 * @param slt The SaveLoad table with objects to save/load.
 */
void SlObject(void *object, const SaveLoadTable &slt)
{
	/* Automatically calculate the length? */
	if (_sl.need_length != NL_NONE) {
		SlSetLength(SlCalcObjLength(object, slt));
		if (_sl.need_length == NL_CALCLENGTH) return;
	}

	for (auto &sld : slt) {
		SlObjectMember(object, sld);
	}
}

/**
 * Handler that is assigned when there is a struct read in the savegame which
 * is not known to the code. This means we are going to skip it.
 */
class SlSkipHandler : public SaveLoadHandler {
	void Save(void *object) const override
	{
		NOT_REACHED();
	}

	void Load(void *object) const override
	{
		size_t length = SlGetStructListLength(UINT32_MAX);
		for (; length > 0; length--) {
			SlObject(object, this->GetLoadDescription());
		}
	}

	void LoadCheck(void *object) const override
	{
		this->Load(object);
	}

	virtual SaveLoadTable GetDescription() const override
	{
		return {};
	}

	virtual SaveLoadCompatTable GetCompatDescription() const override
	{
		NOT_REACHED();
	}
};

/**
 * Save or Load a table header.
 * @note a table-header can never contain more than 65535 fields.
 * @param slt The SaveLoad table with objects to save/load.
 * @return When loading, the ordered SaveLoad array to use; otherwise an empty list.
 */
std::vector<SaveLoad> SlTableHeader(const SaveLoadTable &slt)
{
	/* You can only use SlTableHeader if you are a CH_TABLE. */
	assert(_sl.block_mode == CH_TABLE || _sl.block_mode == CH_SPARSE_TABLE);

	switch (_sl.action) {
		case SLA_LOAD_CHECK:
		case SLA_LOAD: {
			std::vector<SaveLoad> saveloads;

			/* Build a key lookup mapping based on the available fields. */
			std::map<std::string, const SaveLoad *> key_lookup;
			for (auto &sld : slt) {
				if (!SlIsObjectValidInSavegame(sld)) continue;

				/* Check that there is only one active SaveLoad for a given name. */
				assert(key_lookup.find(sld.name) == key_lookup.end());
				key_lookup[sld.name] = &sld;
			}

			while (true) {
				uint8 type;
				SlSaveLoadConv(&type, SLE_UINT8);
				if (type == SLE_FILE_END) break;

				std::string key;
				SlStdString(&key, SLE_STR);

				auto sld_it = key_lookup.find(key);
				if (sld_it == key_lookup.end()) {
					/* SLA_LOADCHECK triggers this debug statement a lot and is perfectly normal. */
					Debug(sl, _sl.action == SLA_LOAD ? 2 : 6, "Field '{}' of type 0x{:02x} not found, skipping", key, type);

					std::shared_ptr<SaveLoadHandler> handler = nullptr;
					SaveLoadType slt;
					switch (type & SLE_FILE_TYPE_MASK) {
						case SLE_FILE_STRING:
							/* Strings are always marked with SLE_FILE_HAS_LENGTH_FIELD, as they are a list of chars. */
							slt = SL_STR;
							break;

						case SLE_FILE_STRUCT:
							/* Structs are always marked with SLE_FILE_HAS_LENGTH_FIELD as SL_STRUCT is seen as a list of 0/1 in length. */
							slt = SL_STRUCTLIST;
							handler = std::make_shared<SlSkipHandler>();
							break;

						default:
							slt = (type & SLE_FILE_HAS_LENGTH_FIELD) ? SL_ARR : SL_VAR;
							break;
					}

					/* We don't know this field, so read to nothing. */
					saveloads.push_back({key, slt, ((VarType)type & SLE_FILE_TYPE_MASK) | SLE_VAR_NULL, 1, SL_MIN_VERSION, SL_MAX_VERSION, 0, nullptr, 0, handler});
					continue;
				}

				/* Validate the type of the field. If it is changed, the
				 * savegame should have been bumped so we know how to do the
				 * conversion. If this error triggers, that clearly didn't
				 * happen and this is a friendly poke to the developer to bump
				 * the savegame version and add conversion code. */
				uint8 correct_type = GetSavegameFileType(*sld_it->second);
				if (correct_type != type) {
					Debug(sl, 1, "Field type for '{}' was expected to be 0x{:02x} but 0x{:02x} was found", key, correct_type, type);
					SlErrorCorrupt("Field type is different than expected");
				}
				saveloads.push_back(*sld_it->second);
			}

			for (auto &sld : saveloads) {
				if (sld.cmd == SL_STRUCTLIST || sld.cmd == SL_STRUCT) {
					sld.handler->load_description = SlTableHeader(sld.handler->GetDescription());
				}
			}

			return saveloads;
		}

		case SLA_SAVE: {
			/* Automatically calculate the length? */
			if (_sl.need_length != NL_NONE) {
				SlSetLength(SlCalcTableHeader(slt));
				if (_sl.need_length == NL_CALCLENGTH) break;
			}

			for (auto &sld : slt) {
				if (!SlIsObjectValidInSavegame(sld)) continue;
				/* Make sure we are not storing empty keys. */
				assert(!sld.name.empty());

				uint8 type = GetSavegameFileType(sld);
				assert(type != SLE_FILE_END);

				SlSaveLoadConv(&type, SLE_UINT8);
				SlStdString(const_cast<std::string *>(&sld.name), SLE_STR);
			}

			/* Add an end-of-header marker. */
			uint8 type = SLE_FILE_END;
			SlSaveLoadConv(&type, SLE_UINT8);

			/* After the table, write down any sub-tables we might have. */
			for (auto &sld : slt) {
				if (!SlIsObjectValidInSavegame(sld)) continue;
				if (sld.cmd == SL_STRUCTLIST || sld.cmd == SL_STRUCT) {
					/* SlCalcTableHeader already looks in sub-lists, so avoid the length being added twice. */
					NeedLength old_need_length = _sl.need_length;
					_sl.need_length = NL_NONE;

					SlTableHeader(sld.handler->GetDescription());

					_sl.need_length = old_need_length;
				}
			}

			break;
		}

		default: NOT_REACHED();
	}

	return std::vector<SaveLoad>();
}

/**
 * Load a table header in a savegame compatible way. If the savegame was made
 * before table headers were added, it will fall back to the
 * SaveLoadCompatTable for the order of fields while loading.
 *
 * @note You only have to call this function if the chunk existed as a
 * non-table type before converting it to a table. New chunks created as
 * table can call SlTableHeader() directly.
 *
 * @param slt The SaveLoad table with objects to save/load.
 * @param slct The SaveLoadCompat table the original order of the fields.
 * @return When loading, the ordered SaveLoad array to use; otherwise an empty list.
 */
std::vector<SaveLoad> SlCompatTableHeader(const SaveLoadTable &slt, const SaveLoadCompatTable &slct)
{
	assert(_sl.action == SLA_LOAD || _sl.action == SLA_LOAD_CHECK);
	/* CH_TABLE / CH_SPARSE_TABLE always have a header. */
	if (_sl.block_mode == CH_TABLE || _sl.block_mode == CH_SPARSE_TABLE) return SlTableHeader(slt);

	std::vector<SaveLoad> saveloads;

	/* Build a key lookup mapping based on the available fields. */
	std::map<std::string, std::vector<const SaveLoad *>> key_lookup;
	for (auto &sld : slt) {
		/* All entries should have a name; otherwise the entry should just be removed. */
		assert(!sld.name.empty());

		key_lookup[sld.name].push_back(&sld);
	}

	for (auto &slc : slct) {
		if (slc.name.empty()) {
			/* In old savegames there can be data we no longer care for. We
			 * skip this by simply reading the amount of bytes indicated and
			 * send those to /dev/null. */
			saveloads.push_back({"", SL_NULL, SLE_FILE_U8 | SLE_VAR_NULL, slc.length, slc.version_from, slc.version_to, 0, nullptr, 0, nullptr});
		} else {
			auto sld_it = key_lookup.find(slc.name);
			/* If this branch triggers, it means that an entry in the
			 * SaveLoadCompat list is not mentioned in the SaveLoad list. Did
			 * you rename a field in one and not in the other? */
			if (sld_it == key_lookup.end()) {
				/* This isn't an assert, as that leaves no information what
				 * field was to blame. This way at least we have breadcrumbs. */
				Debug(sl, 0, "internal error: saveload compatibility field '{}' not found", slc.name);
				SlErrorCorrupt("Internal error with savegame compatibility");
			}
			for (auto &sld : sld_it->second) {
				saveloads.push_back(*sld);
			}
		}
	}

	for (auto &sld : saveloads) {
		if (!SlIsObjectValidInSavegame(sld)) continue;
		if (sld.cmd == SL_STRUCTLIST || sld.cmd == SL_STRUCT) {
			sld.handler->load_description = SlCompatTableHeader(sld.handler->GetDescription(), sld.handler->GetCompatDescription());
		}
	}

	return saveloads;
}

/**
 * Save or Load (a list of) global variables.
 * @param slt The SaveLoad table with objects to save/load.
 */
void SlGlobList(const SaveLoadTable &slt)
{
	SlObject(nullptr, slt);
}

/**
 * Do something of which I have no idea what it is :P
 * @param proc The callback procedure that is called
 * @param arg The variable that will be used for the callback procedure
 */
void SlAutolength(AutolengthProc *proc, void *arg)
{
	size_t offs;

	assert(_sl.action == SLA_SAVE);

	/* Tell it to calculate the length */
	_sl.need_length = NL_CALCLENGTH;
	_sl.obj_len = 0;
	proc(arg);

	/* Setup length */
	_sl.need_length = NL_WANTLENGTH;
	SlSetLength(_sl.obj_len);

	offs = _sl.dumper->GetSize() + _sl.obj_len;

	/* And write the stuff */
	proc(arg);

	if (offs != _sl.dumper->GetSize()) SlErrorCorrupt("Invalid chunk size");
}

void ChunkHandler::LoadCheck(size_t len) const
{
	switch (_sl.block_mode) {
		case CH_TABLE:
		case CH_SPARSE_TABLE:
			SlTableHeader({});
			FALLTHROUGH;
		case CH_ARRAY:
		case CH_SPARSE_ARRAY:
			SlSkipArray();
			break;
		case CH_RIFF:
			SlSkipBytes(len);
			break;
		default:
			NOT_REACHED();
	}
}

/**
 * Load a chunk of data (eg vehicles, stations, etc.)
 * @param ch The chunkhandler that will be used for the operation
 */
static void SlLoadChunk(const ChunkHandler &ch)
{
	byte m = SlReadByte();
	size_t len;
	size_t endoffs;

	_sl.block_mode = m & CH_TYPE_MASK;
	_sl.obj_len = 0;
	_sl.expect_table_header = (_sl.block_mode == CH_TABLE || _sl.block_mode == CH_SPARSE_TABLE);

	/* The header should always be at the start. Read the length; the
	 * Load() should as first action process the header. */
	if (_sl.expect_table_header) {
		SlIterateArray();
	}

	switch (_sl.block_mode) {
		case CH_TABLE:
		case CH_ARRAY:
			_sl.array_index = 0;
			ch.Load();
			if (_next_offs != 0) SlErrorCorrupt("Invalid array length");
			break;
		case CH_SPARSE_TABLE:
		case CH_SPARSE_ARRAY:
			ch.Load();
			if (_next_offs != 0) SlErrorCorrupt("Invalid array length");
			break;
		case CH_RIFF:
			/* Read length */
			len = (SlReadByte() << 16) | ((m >> 4) << 24);
			len += SlReadUint16();
			_sl.obj_len = len;
			endoffs = _sl.reader->GetSize() + len;
			ch.Load();
			if (_sl.reader->GetSize() != endoffs) SlErrorCorrupt("Invalid chunk size");
			break;
		default:
			SlErrorCorrupt("Invalid chunk type");
			break;
	}

	if (_sl.expect_table_header) SlErrorCorrupt("Table chunk without header");
}

/**
 * Load a chunk of data for checking savegames.
 * If the chunkhandler is nullptr, the chunk is skipped.
 * @param ch The chunkhandler that will be used for the operation
 */
static void SlLoadCheckChunk(const ChunkHandler &ch)
{
	byte m = SlReadByte();
	size_t len;
	size_t endoffs;

	_sl.block_mode = m & CH_TYPE_MASK;
	_sl.obj_len = 0;
	_sl.expect_table_header = (_sl.block_mode == CH_TABLE || _sl.block_mode == CH_SPARSE_TABLE);

	/* The header should always be at the start. Read the length; the
	 * LoadCheck() should as first action process the header. */
	if (_sl.expect_table_header) {
		SlIterateArray();
	}

	switch (_sl.block_mode) {
		case CH_TABLE:
		case CH_ARRAY:
			_sl.array_index = 0;
			ch.LoadCheck();
			break;
		case CH_SPARSE_TABLE:
		case CH_SPARSE_ARRAY:
			ch.LoadCheck();
			break;
		case CH_RIFF:
			/* Read length */
			len = (SlReadByte() << 16) | ((m >> 4) << 24);
			len += SlReadUint16();
			_sl.obj_len = len;
			endoffs = _sl.reader->GetSize() + len;
			ch.LoadCheck(len);
			if (_sl.reader->GetSize() != endoffs) SlErrorCorrupt("Invalid chunk size");
			break;
		default:
			SlErrorCorrupt("Invalid chunk type");
			break;
	}

	if (_sl.expect_table_header) SlErrorCorrupt("Table chunk without header");
}

/**
 * Save a chunk of data (eg. vehicles, stations, etc.). Each chunk is
 * prefixed by an ID identifying it, followed by data, and terminator where appropriate
 * @param ch The chunkhandler that will be used for the operation
 */
static void SlSaveChunk(const ChunkHandler &ch)
{
	if (ch.type == CH_READONLY) return;

	SlWriteUint32(ch.id);
	Debug(sl, 2, "Saving chunk {:c}{:c}{:c}{:c}", ch.id >> 24, ch.id >> 16, ch.id >> 8, ch.id);

	_sl.block_mode = ch.type;
	_sl.expect_table_header = (_sl.block_mode == CH_TABLE || _sl.block_mode == CH_SPARSE_TABLE);

	_sl.need_length = (_sl.expect_table_header || _sl.block_mode == CH_RIFF) ? NL_WANTLENGTH : NL_NONE;

	switch (_sl.block_mode) {
		case CH_RIFF:
			ch.Save();
			break;
		case CH_TABLE:
		case CH_ARRAY:
			_sl.last_array_index = 0;
			SlWriteByte(_sl.block_mode);
			ch.Save();
			SlWriteArrayLength(0); // Terminate arrays
			break;
		case CH_SPARSE_TABLE:
		case CH_SPARSE_ARRAY:
			SlWriteByte(_sl.block_mode);
			ch.Save();
			SlWriteArrayLength(0); // Terminate arrays
			break;
		default: NOT_REACHED();
	}

	if (_sl.expect_table_header) SlErrorCorrupt("Table chunk without header");
}

/** Save all chunks */
static void SlSaveChunks()
{
	for (auto &ch : ChunkHandlers()) {
		SlSaveChunk(ch);
	}

	/* Terminator */
	SlWriteUint32(0);
}

/**
 * Find the ChunkHandler that will be used for processing the found
 * chunk in the savegame or in memory
 * @param id the chunk in question
 * @return returns the appropriate chunkhandler
 */
static const ChunkHandler *SlFindChunkHandler(uint32 id)
{
	for (const ChunkHandler &ch : ChunkHandlers()) if (ch.id == id) return &ch;
	return nullptr;
}

/** Load all chunks */
static void SlLoadChunks()
{
	uint32 id;
	const ChunkHandler *ch;

	for (id = SlReadUint32(); id != 0; id = SlReadUint32()) {
		Debug(sl, 2, "Loading chunk {:c}{:c}{:c}{:c}", id >> 24, id >> 16, id >> 8, id);

		ch = SlFindChunkHandler(id);
		if (ch == nullptr) SlErrorCorrupt("Unknown chunk type");
		SlLoadChunk(*ch);
	}
}

/** Load all chunks for savegame checking */
static void SlLoadCheckChunks()
{
	uint32 id;
	const ChunkHandler *ch;

	for (id = SlReadUint32(); id != 0; id = SlReadUint32()) {
		Debug(sl, 2, "Loading chunk {:c}{:c}{:c}{:c}", id >> 24, id >> 16, id >> 8, id);

		ch = SlFindChunkHandler(id);
		if (ch == nullptr) SlErrorCorrupt("Unknown chunk type");
		SlLoadCheckChunk(*ch);
	}
}

/** Fix all pointers (convert index -> pointer) */
static void SlFixPointers()
{
	_sl.action = SLA_PTRS;

	for (const ChunkHandler &ch : ChunkHandlers()) {
		Debug(sl, 3, "Fixing pointers for {:c}{:c}{:c}{:c}", ch.id >> 24, ch.id >> 16, ch.id >> 8, ch.id);
		ch.FixPointers();
	}

	assert(_sl.action == SLA_PTRS);
}


/** Yes, simply reading from a file. */
struct FileReader : LoadFilter {
	FILE *file; ///< The file to read from.
	long begin; ///< The begin of the file.

	/**
	 * Create the file reader, so it reads from a specific file.
	 * @param file The file to read from.
	 */
	FileReader(FILE *file) : LoadFilter(nullptr), file(file), begin(ftell(file))
	{
	}

	/** Make sure everything is cleaned up. */
	~FileReader()
	{
		if (this->file != nullptr) fclose(this->file);
		this->file = nullptr;

		/* Make sure we don't double free. */
		_sl.sf = nullptr;
	}

	size_t Read(byte *buf, size_t size) override
	{
		/* We're in the process of shutting down, i.e. in "failure" mode. */
		if (this->file == nullptr) return 0;

		return fread(buf, 1, size, this->file);
	}

	void Reset() override
	{
		clearerr(this->file);
		if (fseek(this->file, this->begin, SEEK_SET)) {
			Debug(sl, 1, "Could not reset the file reading");
		}
	}
};

/** Yes, simply writing to a file. */
struct FileWriter : SaveFilter {
	FILE *file; ///< The file to write to.

	/**
	 * Create the file writer, so it writes to a specific file.
	 * @param file The file to write to.
	 */
	FileWriter(FILE *file) : SaveFilter(nullptr), file(file)
	{
	}

	/** Make sure everything is cleaned up. */
	~FileWriter()
	{
		this->Finish();

		/* Make sure we don't double free. */
		_sl.sf = nullptr;
	}

	void Write(byte *buf, size_t size) override
	{
		/* We're in the process of shutting down, i.e. in "failure" mode. */
		if (this->file == nullptr) return;

		if (fwrite(buf, 1, size, this->file) != size) SlError(STR_GAME_SAVELOAD_ERROR_FILE_NOT_WRITEABLE);
	}

	void Finish() override
	{
		if (this->file != nullptr) fclose(this->file);
		this->file = nullptr;
	}
};

/*******************************************
 ********** START OF LZO CODE **************
 *******************************************/

#ifdef WITH_LZO
#include <lzo/lzo1x.h>

/** Buffer size for the LZO compressor */
static const uint LZO_BUFFER_SIZE = 8192;

/** Filter using LZO compression. */
struct LZOLoadFilter : LoadFilter {
	/**
	 * Initialise this filter.
	 * @param chain The next filter in this chain.
	 */
	LZOLoadFilter(LoadFilter *chain) : LoadFilter(chain)
	{
		if (lzo_init() != LZO_E_OK) SlError(STR_GAME_SAVELOAD_ERROR_BROKEN_INTERNAL_ERROR, "cannot initialize decompressor");
	}

	size_t Read(byte *buf, size_t ssize) override
	{
		assert(ssize >= LZO_BUFFER_SIZE);

		/* Buffer size is from the LZO docs plus the chunk header size. */
		byte out[LZO_BUFFER_SIZE + LZO_BUFFER_SIZE / 16 + 64 + 3 + sizeof(uint32) * 2];
		uint32 tmp[2];
		uint32 size;
		lzo_uint len = ssize;

		/* Read header*/
		if (this->chain->Read((byte*)tmp, sizeof(tmp)) != sizeof(tmp)) SlError(STR_GAME_SAVELOAD_ERROR_FILE_NOT_READABLE, "File read failed");

		/* Check if size is bad */
		((uint32*)out)[0] = size = tmp[1];

		if (_sl_version != SL_MIN_VERSION) {
			tmp[0] = TO_BE32(tmp[0]);
			size = TO_BE32(size);
		}

		if (size >= sizeof(out)) SlErrorCorrupt("Inconsistent size");

		/* Read block */
		if (this->chain->Read(out + sizeof(uint32), size) != size) SlError(STR_GAME_SAVELOAD_ERROR_FILE_NOT_READABLE);

		/* Verify checksum */
		if (tmp[0] != lzo_adler32(0, out, size + sizeof(uint32))) SlErrorCorrupt("Bad checksum");

		/* Decompress */
		int ret = lzo1x_decompress_safe(out + sizeof(uint32) * 1, size, buf, &len, nullptr);
		if (ret != LZO_E_OK) SlError(STR_GAME_SAVELOAD_ERROR_FILE_NOT_READABLE);
		return len;
	}
};

/** Filter using LZO compression. */
struct LZOSaveFilter : SaveFilter {
	/**
	 * Initialise this filter.
	 * @param chain             The next filter in this chain.
	 * @param compression_level The requested level of compression.
	 */
	LZOSaveFilter(SaveFilter *chain, byte compression_level) : SaveFilter(chain)
	{
		if (lzo_init() != LZO_E_OK) SlError(STR_GAME_SAVELOAD_ERROR_BROKEN_INTERNAL_ERROR, "cannot initialize compressor");
	}

	void Write(byte *buf, size_t size) override
	{
		const lzo_bytep in = buf;
		/* Buffer size is from the LZO docs plus the chunk header size. */
		byte out[LZO_BUFFER_SIZE + LZO_BUFFER_SIZE / 16 + 64 + 3 + sizeof(uint32) * 2];
		byte wrkmem[LZO1X_1_MEM_COMPRESS];
		lzo_uint outlen;

		do {
			/* Compress up to LZO_BUFFER_SIZE bytes at once. */
			lzo_uint len = size > LZO_BUFFER_SIZE ? LZO_BUFFER_SIZE : (lzo_uint)size;
			lzo1x_1_compress(in, len, out + sizeof(uint32) * 2, &outlen, wrkmem);
			((uint32*)out)[1] = TO_BE32((uint32)outlen);
			((uint32*)out)[0] = TO_BE32(lzo_adler32(0, out + sizeof(uint32), outlen + sizeof(uint32)));
			this->chain->Write(out, outlen + sizeof(uint32) * 2);

			/* Move to next data chunk. */
			size -= len;
			in += len;
		} while (size > 0);
	}
};

#endif /* WITH_LZO */

/*********************************************
 ******** START OF NOCOMP CODE (uncompressed)*
 *********************************************/

/** Filter without any compression. */
struct NoCompLoadFilter : LoadFilter {
	/**
	 * Initialise this filter.
	 * @param chain The next filter in this chain.
	 */
	NoCompLoadFilter(LoadFilter *chain) : LoadFilter(chain)
	{
	}

	size_t Read(byte *buf, size_t size) override
	{
		return this->chain->Read(buf, size);
	}
};

/** Filter without any compression. */
struct NoCompSaveFilter : SaveFilter {
	/**
	 * Initialise this filter.
	 * @param chain             The next filter in this chain.
	 * @param compression_level The requested level of compression.
	 */
	NoCompSaveFilter(SaveFilter *chain, byte compression_level) : SaveFilter(chain)
	{
	}

	void Write(byte *buf, size_t size) override
	{
		this->chain->Write(buf, size);
	}
};

/********************************************
 ********** START OF ZLIB CODE **************
 ********************************************/

#if defined(WITH_ZLIB)
#include <zlib.h>

/** Filter using Zlib compression. */
struct ZlibLoadFilter : LoadFilter {
	z_stream z;                        ///< Stream state we are reading from.
	byte fread_buf[MEMORY_CHUNK_SIZE]; ///< Buffer for reading from the file.

	/**
	 * Initialise this filter.
	 * @param chain The next filter in this chain.
	 */
	ZlibLoadFilter(LoadFilter *chain) : LoadFilter(chain)
	{
		memset(&this->z, 0, sizeof(this->z));
		if (inflateInit(&this->z) != Z_OK) SlError(STR_GAME_SAVELOAD_ERROR_BROKEN_INTERNAL_ERROR, "cannot initialize decompressor");
	}

	/** Clean everything up. */
	~ZlibLoadFilter()
	{
		inflateEnd(&this->z);
	}

	size_t Read(byte *buf, size_t size) override
	{
		this->z.next_out  = buf;
		this->z.avail_out = (uint)size;

		do {
			/* read more bytes from the file? */
			if (this->z.avail_in == 0) {
				this->z.next_in = this->fread_buf;
				this->z.avail_in = (uint)this->chain->Read(this->fread_buf, sizeof(this->fread_buf));
			}

			/* inflate the data */
			int r = inflate(&this->z, 0);
			if (r == Z_STREAM_END) break;

			if (r != Z_OK) SlError(STR_GAME_SAVELOAD_ERROR_BROKEN_INTERNAL_ERROR, "inflate() failed");
		} while (this->z.avail_out != 0);

		return size - this->z.avail_out;
	}
};

/** Filter using Zlib compression. */
struct ZlibSaveFilter : SaveFilter {
	z_stream z; ///< Stream state we are writing to.

	/**
	 * Initialise this filter.
	 * @param chain             The next filter in this chain.
	 * @param compression_level The requested level of compression.
	 */
	ZlibSaveFilter(SaveFilter *chain, byte compression_level) : SaveFilter(chain)
	{
		memset(&this->z, 0, sizeof(this->z));
		if (deflateInit(&this->z, compression_level) != Z_OK) SlError(STR_GAME_SAVELOAD_ERROR_BROKEN_INTERNAL_ERROR, "cannot initialize compressor");
	}

	/** Clean up what we allocated. */
	~ZlibSaveFilter()
	{
		deflateEnd(&this->z);
	}

	/**
	 * Helper loop for writing the data.
	 * @param p    The bytes to write.
	 * @param len  Amount of bytes to write.
	 * @param mode Mode for deflate.
	 */
	void WriteLoop(byte *p, size_t len, int mode)
	{
		byte buf[MEMORY_CHUNK_SIZE]; // output buffer
		uint n;
		this->z.next_in = p;
		this->z.avail_in = (uInt)len;
		do {
			this->z.next_out = buf;
			this->z.avail_out = sizeof(buf);

			/**
			 * For the poor next soul who sees many valgrind warnings of the
			 * "Conditional jump or move depends on uninitialised value(s)" kind:
			 * According to the author of zlib it is not a bug and it won't be fixed.
			 * http://groups.google.com/group/comp.compression/browse_thread/thread/b154b8def8c2a3ef/cdf9b8729ce17ee2
			 * [Mark Adler, Feb 24 2004, 'zlib-1.2.1 valgrind warnings' in the newsgroup comp.compression]
			 */
			int r = deflate(&this->z, mode);

			/* bytes were emitted? */
			if ((n = sizeof(buf) - this->z.avail_out) != 0) {
				this->chain->Write(buf, n);
			}
			if (r == Z_STREAM_END) break;

			if (r != Z_OK) SlError(STR_GAME_SAVELOAD_ERROR_BROKEN_INTERNAL_ERROR, "zlib returned error code");
		} while (this->z.avail_in || !this->z.avail_out);
	}

	void Write(byte *buf, size_t size) override
	{
		this->WriteLoop(buf, size, 0);
	}

	void Finish() override
	{
		this->WriteLoop(nullptr, 0, Z_FINISH);
		this->chain->Finish();
	}
};

#endif /* WITH_ZLIB */

/********************************************
 ********** START OF LZMA CODE **************
 ********************************************/

#if defined(WITH_LIBLZMA)
#include <lzma.h>

/**
 * Have a copy of an initialised LZMA stream. We need this as it's
 * impossible to "re"-assign LZMA_STREAM_INIT to a variable in some
 * compilers, i.e. LZMA_STREAM_INIT can't be used to set something.
 * This var has to be used instead.
 */
static const lzma_stream _lzma_init = LZMA_STREAM_INIT;

/** Filter without any compression. */
struct LZMALoadFilter : LoadFilter {
	lzma_stream lzma;                  ///< Stream state that we are reading from.
	byte fread_buf[MEMORY_CHUNK_SIZE]; ///< Buffer for reading from the file.

	/**
	 * Initialise this filter.
	 * @param chain The next filter in this chain.
	 */
	LZMALoadFilter(LoadFilter *chain) : LoadFilter(chain), lzma(_lzma_init)
	{
		/* Allow saves up to 256 MB uncompressed */
		if (lzma_auto_decoder(&this->lzma, 1 << 28, 0) != LZMA_OK) SlError(STR_GAME_SAVELOAD_ERROR_BROKEN_INTERNAL_ERROR, "cannot initialize decompressor");
	}

	/** Clean everything up. */
	~LZMALoadFilter()
	{
		lzma_end(&this->lzma);
	}

	size_t Read(byte *buf, size_t size) override
	{
		this->lzma.next_out  = buf;
		this->lzma.avail_out = size;

		do {
			/* read more bytes from the file? */
			if (this->lzma.avail_in == 0) {
				this->lzma.next_in  = this->fread_buf;
				this->lzma.avail_in = this->chain->Read(this->fread_buf, sizeof(this->fread_buf));
			}

			/* inflate the data */
			lzma_ret r = lzma_code(&this->lzma, LZMA_RUN);
			if (r == LZMA_STREAM_END) break;
			if (r != LZMA_OK) SlError(STR_GAME_SAVELOAD_ERROR_BROKEN_INTERNAL_ERROR, "liblzma returned error code");
		} while (this->lzma.avail_out != 0);

		return size - this->lzma.avail_out;
	}
};

/** Filter using LZMA compression. */
struct LZMASaveFilter : SaveFilter {
	lzma_stream lzma; ///< Stream state that we are writing to.

	/**
	 * Initialise this filter.
	 * @param chain             The next filter in this chain.
	 * @param compression_level The requested level of compression.
	 */
	LZMASaveFilter(SaveFilter *chain, byte compression_level) : SaveFilter(chain), lzma(_lzma_init)
	{
		if (lzma_easy_encoder(&this->lzma, compression_level, LZMA_CHECK_CRC32) != LZMA_OK) SlError(STR_GAME_SAVELOAD_ERROR_BROKEN_INTERNAL_ERROR, "cannot initialize compressor");
	}

	/** Clean up what we allocated. */
	~LZMASaveFilter()
	{
		lzma_end(&this->lzma);
	}

	/**
	 * Helper loop for writing the data.
	 * @param p      The bytes to write.
	 * @param len    Amount of bytes to write.
	 * @param action Action for lzma_code.
	 */
	void WriteLoop(byte *p, size_t len, lzma_action action)
	{
		byte buf[MEMORY_CHUNK_SIZE]; // output buffer
		size_t n;
		this->lzma.next_in = p;
		this->lzma.avail_in = len;
		do {
			this->lzma.next_out = buf;
			this->lzma.avail_out = sizeof(buf);

			lzma_ret r = lzma_code(&this->lzma, action);

			/* bytes were emitted? */
			if ((n = sizeof(buf) - this->lzma.avail_out) != 0) {
				this->chain->Write(buf, n);
			}
			if (r == LZMA_STREAM_END) break;
			if (r != LZMA_OK) SlError(STR_GAME_SAVELOAD_ERROR_BROKEN_INTERNAL_ERROR, "liblzma returned error code");
		} while (this->lzma.avail_in || !this->lzma.avail_out);
	}

	void Write(byte *buf, size_t size) override
	{
		this->WriteLoop(buf, size, LZMA_RUN);
	}

	void Finish() override
	{
		this->WriteLoop(nullptr, 0, LZMA_FINISH);
		this->chain->Finish();
	}
};

#endif /* WITH_LIBLZMA */

/*******************************************
 ************* END OF CODE *****************
 *******************************************/

/** The format for a reader/writer type of a savegame */
struct SaveLoadFormat {
	const char *name;                     ///< name of the compressor/decompressor (debug-only)
	uint32 tag;                           ///< the 4-letter tag by which it is identified in the savegame

	LoadFilter *(*init_load)(LoadFilter *chain);                    ///< Constructor for the load filter.
	SaveFilter *(*init_write)(SaveFilter *chain, byte compression); ///< Constructor for the save filter.

	byte min_compression;                 ///< the minimum compression level of this format
	byte default_compression;             ///< the default compression level of this format
	byte max_compression;                 ///< the maximum compression level of this format
};

/** The different saveload formats known/understood by OpenTTD. */
static const SaveLoadFormat _saveload_formats[] = {
#if defined(WITH_LZO)
	/* Roughly 75% larger than zlib level 6 at only ~7% of the CPU usage. */
	{"lzo",    TO_BE32X('OTTD'), CreateLoadFilter<LZOLoadFilter>,    CreateSaveFilter<LZOSaveFilter>,    0, 0, 0},
#else
	{"lzo",    TO_BE32X('OTTD'), nullptr,                            nullptr,                            0, 0, 0},
#endif
	/* Roughly 5 times larger at only 1% of the CPU usage over zlib level 6. */
	{"none",   TO_BE32X('OTTN'), CreateLoadFilter<NoCompLoadFilter>, CreateSaveFilter<NoCompSaveFilter>, 0, 0, 0},
#if defined(WITH_ZLIB)
	/* After level 6 the speed reduction is significant (1.5x to 2.5x slower per level), but the reduction in filesize is
	 * fairly insignificant (~1% for each step). Lower levels become ~5-10% bigger by each level than level 6 while level
	 * 1 is "only" 3 times as fast. Level 0 results in uncompressed savegames at about 8 times the cost of "none". */
	{"zlib",   TO_BE32X('OTTZ'), CreateLoadFilter<ZlibLoadFilter>,   CreateSaveFilter<ZlibSaveFilter>,   0, 6, 9},
#else
	{"zlib",   TO_BE32X('OTTZ'), nullptr,                            nullptr,                            0, 0, 0},
#endif
#if defined(WITH_LIBLZMA)
	/* Level 2 compression is speed wise as fast as zlib level 6 compression (old default), but results in ~10% smaller saves.
	 * Higher compression levels are possible, and might improve savegame size by up to 25%, but are also up to 10 times slower.
	 * The next significant reduction in file size is at level 4, but that is already 4 times slower. Level 3 is primarily 50%
	 * slower while not improving the filesize, while level 0 and 1 are faster, but don't reduce savegame size much.
	 * It's OTTX and not e.g. OTTL because liblzma is part of xz-utils and .tar.xz is preferred over .tar.lzma. */
	{"lzma",   TO_BE32X('OTTX'), CreateLoadFilter<LZMALoadFilter>,   CreateSaveFilter<LZMASaveFilter>,   0, 2, 9},
#else
	{"lzma",   TO_BE32X('OTTX'), nullptr,                            nullptr,                            0, 0, 0},
#endif
};

/**
 * Return the savegameformat of the game. Whether it was created with ZLIB compression
 * uncompressed, or another type
 * @param full_name Name of the savegame format. If empty it picks the first available one
 * @param compression_level Output for telling what compression level we want.
 * @return Pointer to SaveLoadFormat struct giving all characteristics of this type of savegame
 */
static const SaveLoadFormat *GetSavegameFormat(const std::string &full_name, byte *compression_level)
{
	const SaveLoadFormat *def = lastof(_saveload_formats);

	/* find default savegame format, the highest one with which files can be written */
	while (!def->init_write) def--;

	if (!full_name.empty()) {
		/* Get the ":..." of the compression level out of the way */
		size_t separator = full_name.find(':');
		bool has_comp_level = separator != std::string::npos;
		const std::string name(full_name, 0, has_comp_level ? separator : full_name.size());

		for (const SaveLoadFormat *slf = &_saveload_formats[0]; slf != endof(_saveload_formats); slf++) {
			if (slf->init_write != nullptr && name.compare(slf->name) == 0) {
				*compression_level = slf->default_compression;
				if (has_comp_level) {
					const std::string complevel(full_name, separator + 1);

					/* Get the level and determine whether all went fine. */
					size_t processed;
					long level = std::stol(complevel, &processed, 10);
					if (processed == 0 || level != Clamp(level, slf->min_compression, slf->max_compression)) {
						SetDParamStr(0, complevel);
						ShowErrorMessage(STR_CONFIG_ERROR, STR_CONFIG_ERROR_INVALID_SAVEGAME_COMPRESSION_LEVEL, WL_CRITICAL);
					} else {
						*compression_level = level;
					}
				}
				return slf;
			}
		}

		SetDParamStr(0, name);
		SetDParamStr(1, def->name);
		ShowErrorMessage(STR_CONFIG_ERROR, STR_CONFIG_ERROR_INVALID_SAVEGAME_COMPRESSION_ALGORITHM, WL_CRITICAL);
	}
	*compression_level = def->default_compression;
	return def;
}

/* actual loader/saver function */
void InitializeGame(uint size_x, uint size_y, bool reset_date, bool reset_settings);
extern bool AfterLoadGame();
extern bool LoadOldSaveGame(const std::string &file);

/**
 * Clear temporary data that is passed between various saveload phases.
 */
static void ResetSaveloadData()
{
	ResetTempEngineData();
	ResetLabelMaps();
	ResetOldWaypoints();
}

/**
 * Clear/free saveload state.
 */
static inline void ClearSaveLoadState()
{
	delete _sl.dumper;
	_sl.dumper = nullptr;

	delete _sl.sf;
	_sl.sf = nullptr;

	delete _sl.reader;
	_sl.reader = nullptr;

	delete _sl.lf;
	_sl.lf = nullptr;
}

/**
 * Update the gui accordingly when starting saving
 * and set locks on saveload. Also turn off fast-forward cause with that
 * saving takes Aaaaages
 */
static void SaveFileStart()
{
	_sl.game_speed = _game_speed;
	_game_speed = 100;
	SetMouseCursorBusy(true);

	InvalidateWindowData(WC_STATUS_BAR, 0, SBI_SAVELOAD_START);
	_sl.saveinprogress = true;
}

/** Update the gui accordingly when saving is done and release locks on saveload. */
static void SaveFileDone()
{
	if (_game_mode != GM_MENU) _game_speed = _sl.game_speed;
	SetMouseCursorBusy(false);

	InvalidateWindowData(WC_STATUS_BAR, 0, SBI_SAVELOAD_FINISH);
	_sl.saveinprogress = false;

#ifdef __EMSCRIPTEN__
	EM_ASM(if (window["openttd_syncfs"]) openttd_syncfs());
#endif
}

/** Set the error message from outside of the actual loading/saving of the game (AfterLoadGame and friends) */
void SetSaveLoadError(StringID str)
{
	_sl.error_str = str;
}

/** Get the string representation of the error message */
const char *GetSaveLoadErrorString()
{
	SetDParam(0, _sl.error_str);
	SetDParamStr(1, _sl.extra_msg);

	static char err_str[512];
	GetString(err_str, _sl.action == SLA_SAVE ? STR_ERROR_GAME_SAVE_FAILED : STR_ERROR_GAME_LOAD_FAILED, lastof(err_str));
	return err_str;
}

/** Show a gui message when saving has failed */
static void SaveFileError()
{
	SetDParamStr(0, GetSaveLoadErrorString());
	ShowErrorMessage(STR_JUST_RAW_STRING, INVALID_STRING_ID, WL_ERROR);
	SaveFileDone();
}

/**
 * We have written the whole game into memory, _memory_savegame, now find
 * and appropriate compressor and start writing to file.
 */
static SaveOrLoadResult SaveFileToDisk(bool threaded)
{
	try {
		byte compression;
		const SaveLoadFormat *fmt = GetSavegameFormat(_savegame_format, &compression);

		/* We have written our stuff to memory, now write it to file! */
		uint32 hdr[2] = { fmt->tag, TO_BE32(SAVEGAME_VERSION << 16) };
		_sl.sf->Write((byte*)hdr, sizeof(hdr));

		_sl.sf = fmt->init_write(_sl.sf, compression);
		_sl.dumper->Flush(_sl.sf);

		ClearSaveLoadState();

		if (threaded) SetAsyncSaveFinish(SaveFileDone);

		return SL_OK;
	} catch (...) {
		ClearSaveLoadState();

		AsyncSaveFinishProc asfp = SaveFileDone;

		/* We don't want to shout when saving is just
		 * cancelled due to a client disconnecting. */
		if (_sl.error_str != STR_NETWORK_ERROR_LOSTCONNECTION) {
			/* Skip the "colour" character */
			Debug(sl, 0, "{}", GetSaveLoadErrorString() + 3);
			asfp = SaveFileError;
		}

		if (threaded) {
			SetAsyncSaveFinish(asfp);
		} else {
			asfp();
		}
		return SL_ERROR;
	}
}

void WaitTillSaved()
{
	if (!_save_thread.joinable()) return;

	_save_thread.join();

	/* Make sure every other state is handled properly as well. */
	ProcessAsyncSaveFinish();
}

/**
 * Actually perform the saving of the savegame.
 * General tactics is to first save the game to memory, then write it to file
 * using the writer, either in threaded mode if possible, or single-threaded.
 * @param writer   The filter to write the savegame to.
 * @param threaded Whether to try to perform the saving asynchronously.
 * @return Return the result of the action. #SL_OK or #SL_ERROR
 */
static SaveOrLoadResult DoSave(SaveFilter *writer, bool threaded)
{
	assert(!_sl.saveinprogress);

	_sl.dumper = new MemoryDumper();
	_sl.sf = writer;

	_sl_version = SAVEGAME_VERSION;

	SaveViewportBeforeSaveGame();
	SlSaveChunks();

	SaveFileStart();

	if (!threaded || !StartNewThread(&_save_thread, "ottd:savegame", &SaveFileToDisk, true)) {
		if (threaded) Debug(sl, 1, "Cannot create savegame thread, reverting to single-threaded mode...");

		SaveOrLoadResult result = SaveFileToDisk(false);
		SaveFileDone();

		return result;
	}

	return SL_OK;
}

/**
 * Save the game using a (writer) filter.
 * @param writer   The filter to write the savegame to.
 * @param threaded Whether to try to perform the saving asynchronously.
 * @return Return the result of the action. #SL_OK or #SL_ERROR
 */
SaveOrLoadResult SaveWithFilter(SaveFilter *writer, bool threaded)
{
	try {
		_sl.action = SLA_SAVE;
		return DoSave(writer, threaded);
	} catch (...) {
		ClearSaveLoadState();
		return SL_ERROR;
	}
}

/**
 * Actually perform the loading of a "non-old" savegame.
 * @param reader     The filter to read the savegame from.
 * @param load_check Whether to perform the checking ("preview") or actually load the game.
 * @return Return the result of the action. #SL_OK or #SL_REINIT ("unload" the game)
 */
static SaveOrLoadResult DoLoad(LoadFilter *reader, bool load_check)
{
	_sl.lf = reader;

	if (load_check) {
		/* Clear previous check data */
		_load_check_data.Clear();
		/* Mark SL_LOAD_CHECK as supported for this savegame. */
		_load_check_data.checkable = true;
	}

	uint32 hdr[2];
	if (_sl.lf->Read((byte*)hdr, sizeof(hdr)) != sizeof(hdr)) SlError(STR_GAME_SAVELOAD_ERROR_FILE_NOT_READABLE);

	/* see if we have any loader for this type. */
	const SaveLoadFormat *fmt = _saveload_formats;
	for (;;) {
		/* No loader found, treat as version 0 and use LZO format */
		if (fmt == endof(_saveload_formats)) {
			Debug(sl, 0, "Unknown savegame type, trying to load it as the buggy format");
			_sl.lf->Reset();
			_sl_version = SL_MIN_VERSION;
			_sl_minor_version = 0;

			/* Try to find the LZO savegame format; it uses 'OTTD' as tag. */
			fmt = _saveload_formats;
			for (;;) {
				if (fmt == endof(_saveload_formats)) {
					/* Who removed LZO support? */
					NOT_REACHED();
				}
				if (fmt->tag == TO_BE32X('OTTD')) break;
				fmt++;
			}
			break;
		}

		if (fmt->tag == hdr[0]) {
			/* check version number */
			_sl_version = (SaveLoadVersion)(TO_BE32(hdr[1]) >> 16);
			/* Minor is not used anymore from version 18.0, but it is still needed
			 * in versions before that (4 cases) which can't be removed easy.
			 * Therefore it is loaded, but never saved (or, it saves a 0 in any scenario). */
			_sl_minor_version = (TO_BE32(hdr[1]) >> 8) & 0xFF;

			Debug(sl, 1, "Loading savegame version {}", _sl_version);

			/* Is the version higher than the current? */
			if (_sl_version > SAVEGAME_VERSION) SlError(STR_GAME_SAVELOAD_ERROR_TOO_NEW_SAVEGAME);
			if (_sl_version >= SLV_START_PATCHPACKS && _sl_version <= SLV_END_PATCHPACKS) SlError(STR_GAME_SAVELOAD_ERROR_PATCHPACK);
			break;
		}

		fmt++;
	}

	/* loader for this savegame type is not implemented? */
	if (fmt->init_load == nullptr) {
		char err_str[64];
		seprintf(err_str, lastof(err_str), "Loader for '%s' is not available.", fmt->name);
		SlError(STR_GAME_SAVELOAD_ERROR_BROKEN_INTERNAL_ERROR, err_str);
	}

	_sl.lf = fmt->init_load(_sl.lf);
	_sl.reader = new ReadBuffer(_sl.lf);
	_next_offs = 0;

	if (!load_check) {
		ResetSaveloadData();

		/* Old maps were hardcoded to 256x256 and thus did not contain
		 * any mapsize information. Pre-initialize to 256x256 to not to
		 * confuse old games */
		InitializeGame(256, 256, true, true);

		GamelogReset();

		if (IsSavegameVersionBefore(SLV_4)) {
			/*
			 * NewGRFs were introduced between 0.3,4 and 0.3.5, which both
			 * shared savegame version 4. Anything before that 'obviously'
			 * does not have any NewGRFs. Between the introduction and
			 * savegame version 41 (just before 0.5) the NewGRF settings
			 * were not stored in the savegame and they were loaded by
			 * using the settings from the main menu.
			 * So, to recap:
			 * - savegame version  <  4:  do not load any NewGRFs.
			 * - savegame version >= 41:  load NewGRFs from savegame, which is
			 *                            already done at this stage by
			 *                            overwriting the main menu settings.
			 * - other savegame versions: use main menu settings.
			 *
			 * This means that users *can* crash savegame version 4..40
			 * savegames if they set incompatible NewGRFs in the main menu,
			 * but can't crash anymore for savegame version < 4 savegames.
			 *
			 * Note: this is done here because AfterLoadGame is also called
			 * for TTO/TTD/TTDP savegames which have their own NewGRF logic.
			 */
			ClearGRFConfigList(&_grfconfig);
		}
	}

	if (load_check) {
		/* Load chunks into _load_check_data.
		 * No pools are loaded. References are not possible, and thus do not need resolving. */
		SlLoadCheckChunks();
	} else {
		/* Load chunks and resolve references */
		SlLoadChunks();
		SlFixPointers();
	}

	ClearSaveLoadState();

	_savegame_type = SGT_OTTD;

	if (load_check) {
		/* The only part from AfterLoadGame() we need */
		_load_check_data.grf_compatibility = IsGoodGRFConfigList(_load_check_data.grfconfig);
	} else {
		GamelogStartAction(GLAT_LOAD);

		/* After loading fix up savegame for any internal changes that
		 * might have occurred since then. If it fails, load back the old game. */
		if (!AfterLoadGame()) {
			GamelogStopAction();
			return SL_REINIT;
		}

		GamelogStopAction();
	}

	return SL_OK;
}

/**
 * Load the game using a (reader) filter.
 * @param reader   The filter to read the savegame from.
 * @return Return the result of the action. #SL_OK or #SL_REINIT ("unload" the game)
 */
SaveOrLoadResult LoadWithFilter(LoadFilter *reader)
{
	try {
		_sl.action = SLA_LOAD;
		return DoLoad(reader, false);
	} catch (...) {
		ClearSaveLoadState();
		return SL_REINIT;
	}
}

/**
 * Main Save or Load function where the high-level saveload functions are
 * handled. It opens the savegame, selects format and checks versions
 * @param filename The name of the savegame being created/loaded
 * @param fop Save or load mode. Load can also be a TTD(Patch) game.
 * @param sb The sub directory to save the savegame in
 * @param threaded True when threaded saving is allowed
 * @return Return the result of the action. #SL_OK, #SL_ERROR, or #SL_REINIT ("unload" the game)
 */
SaveOrLoadResult SaveOrLoad(const std::string &filename, SaveLoadOperation fop, DetailedFileType dft, Subdirectory sb, bool threaded)
{
	/* An instance of saving is already active, so don't go saving again */
	if (_sl.saveinprogress && fop == SLO_SAVE && dft == DFT_GAME_FILE && threaded) {
		/* if not an autosave, but a user action, show error message */
		if (!_do_autosave) ShowErrorMessage(STR_ERROR_SAVE_STILL_IN_PROGRESS, INVALID_STRING_ID, WL_ERROR);
		return SL_OK;
	}
	WaitTillSaved();

	try {
		/* Load a TTDLX or TTDPatch game */
		if (fop == SLO_LOAD && dft == DFT_OLD_GAME_FILE) {
			ResetSaveloadData();

			InitializeGame(256, 256, true, true); // set a mapsize of 256x256 for TTDPatch games or it might get confused

			/* TTD/TTO savegames have no NewGRFs, TTDP savegame have them
			 * and if so a new NewGRF list will be made in LoadOldSaveGame.
			 * Note: this is done here because AfterLoadGame is also called
			 * for OTTD savegames which have their own NewGRF logic. */
			ClearGRFConfigList(&_grfconfig);
			GamelogReset();
			if (!LoadOldSaveGame(filename)) return SL_REINIT;
			_sl_version = SL_MIN_VERSION;
			_sl_minor_version = 0;
			GamelogStartAction(GLAT_LOAD);
			if (!AfterLoadGame()) {
				GamelogStopAction();
				return SL_REINIT;
			}
			GamelogStopAction();
			return SL_OK;
		}

		assert(dft == DFT_GAME_FILE);
		switch (fop) {
			case SLO_CHECK:
				_sl.action = SLA_LOAD_CHECK;
				break;

			case SLO_LOAD:
				_sl.action = SLA_LOAD;
				break;

			case SLO_SAVE:
				_sl.action = SLA_SAVE;
				break;

			default: NOT_REACHED();
		}

		FILE *fh = (fop == SLO_SAVE) ? FioFOpenFile(filename, "wb", sb) : FioFOpenFile(filename, "rb", sb);

		/* Make it a little easier to load savegames from the console */
		if (fh == nullptr && fop != SLO_SAVE) fh = FioFOpenFile(filename, "rb", SAVE_DIR);
		if (fh == nullptr && fop != SLO_SAVE) fh = FioFOpenFile(filename, "rb", BASE_DIR);
		if (fh == nullptr && fop != SLO_SAVE) fh = FioFOpenFile(filename, "rb", SCENARIO_DIR);

		if (fh == nullptr) {
			SlError(fop == SLO_SAVE ? STR_GAME_SAVELOAD_ERROR_FILE_NOT_WRITEABLE : STR_GAME_SAVELOAD_ERROR_FILE_NOT_READABLE);
		}

		if (fop == SLO_SAVE) { // SAVE game
			Debug(desync, 1, "save: {:08x}; {:02x}; {}", _date, _date_fract, filename);
			if (_network_server || !_settings_client.gui.threaded_saves) threaded = false;

			return DoSave(new FileWriter(fh), threaded);
		}

		/* LOAD game */
		assert(fop == SLO_LOAD || fop == SLO_CHECK);
		Debug(desync, 1, "load: {}", filename);
		return DoLoad(new FileReader(fh), fop == SLO_CHECK);
	} catch (...) {
		/* This code may be executed both for old and new save games. */
		ClearSaveLoadState();

		/* Skip the "colour" character */
		if (fop != SLO_CHECK) Debug(sl, 0, "{}", GetSaveLoadErrorString());

		/* A saver/loader exception!! reinitialize all variables to prevent crash! */
		return (fop == SLO_LOAD) ? SL_REINIT : SL_ERROR;
	}
}

/**
 * Create an autosave or netsave.
 * @param counter A reference to the counter variable to be used for rotating the file name.
 * @param netsave Indicates if this is a regular autosave or a netsave.
 */
void DoAutoOrNetsave(FiosNumberedSaveName &counter)
{
	char buf[MAX_PATH];

	if (_settings_client.gui.keep_all_autosave) {
		GenerateDefaultSaveName(buf, lastof(buf));
		strecat(buf, counter.Extension().c_str(), lastof(buf));
	} else {
		strecpy(buf, counter.Filename().c_str(), lastof(buf));
	}

	Debug(sl, 2, "Autosaving to '{}'", buf);
	if (SaveOrLoad(buf, SLO_SAVE, DFT_GAME_FILE, AUTOSAVE_DIR) != SL_OK) {
		ShowErrorMessage(STR_ERROR_AUTOSAVE_FAILED, INVALID_STRING_ID, WL_ERROR);
	}
}


/** Do a save when exiting the game (_settings_client.gui.autosave_on_exit) */
void DoExitSave()
{
	SaveOrLoad("exit.sav", SLO_SAVE, DFT_GAME_FILE, AUTOSAVE_DIR);
}

/**
 * Fill the buffer with the default name for a savegame *or* screenshot.
 * @param buf the buffer to write to.
 * @param last the last element in the buffer.
 */
void GenerateDefaultSaveName(char *buf, const char *last)
{
	/* Check if we have a name for this map, which is the name of the first
	 * available company. When there's no company available we'll use
	 * 'Spectator' as "company" name. */
	CompanyID cid = _local_company;
	if (!Company::IsValidID(cid)) {
		for (const Company *c : Company::Iterate()) {
			cid = c->index;
			break;
		}
	}

	SetDParam(0, cid);

	/* Insert current date */
	switch (_settings_client.gui.date_format_in_default_names) {
		case 0: SetDParam(1, STR_JUST_DATE_LONG); break;
		case 1: SetDParam(1, STR_JUST_DATE_TINY); break;
		case 2: SetDParam(1, STR_JUST_DATE_ISO); break;
		default: NOT_REACHED();
	}
	SetDParam(2, _date);

	/* Get the correct string (special string for when there's not company) */
	GetString(buf, !Company::IsValidID(cid) ? STR_SAVEGAME_NAME_SPECTATOR : STR_SAVEGAME_NAME_DEFAULT, last);
	SanitizeFilename(buf);
}

/**
 * Set the mode and file type of the file to save or load based on the type of file entry at the file system.
 * @param ft Type of file entry of the file system.
 */
void FileToSaveLoad::SetMode(FiosType ft)
{
	this->SetMode(SLO_LOAD, GetAbstractFileType(ft), GetDetailedFileType(ft));
}

/**
 * Set the mode and file type of the file to save or load.
 * @param fop File operation being performed.
 * @param aft Abstract file type.
 * @param dft Detailed file type.
 */
void FileToSaveLoad::SetMode(SaveLoadOperation fop, AbstractFileType aft, DetailedFileType dft)
{
	if (aft == FT_INVALID || aft == FT_NONE) {
		this->file_op = SLO_INVALID;
		this->detail_ftype = DFT_INVALID;
		this->abstract_ftype = FT_INVALID;
		return;
	}

	this->file_op = fop;
	this->detail_ftype = dft;
	this->abstract_ftype = aft;
}

/**
 * Set the name of the file.
 * @param name Name of the file.
 */
void FileToSaveLoad::SetName(const char *name)
{
	this->name = name;
}

/**
 * Set the title of the file.
 * @param title Title of the file.
 */
void FileToSaveLoad::SetTitle(const char *title)
{
	strecpy(this->title, title, lastof(this->title));
}

SaveLoadTable SaveLoadHandler::GetLoadDescription() const
{
	assert(this->load_description.has_value());
	return *this->load_description;
}
