/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file strings_func.h Functions related to OTTD's strings. */

#ifndef STRINGS_FUNC_H
#define STRINGS_FUNC_H

#include "strings_type.h"
#include "string_type.h"
#include "gfx_type.h"
#include "core/bitmath_func.hpp"
#include "core/overflowsafe_type.hpp"

/**
 * Extract the StringTab from a StringID.
 * @param str String identifier
 * @return StringTab from \a str
 */
static inline StringTab GetStringTab(StringID str)
{
	StringTab result = (StringTab)(str >> TAB_SIZE_BITS);
	if (result >= TEXT_TAB_NEWGRF_START) return TEXT_TAB_NEWGRF_START;
	if (result >= TEXT_TAB_GAMESCRIPT_START) return TEXT_TAB_GAMESCRIPT_START;
	return result;
}

/**
 * Extract the StringIndex from a StringID.
 * @param str String identifier
 * @return StringIndex from \a str
 */
static inline uint GetStringIndex(StringID str)
{
	return str - (GetStringTab(str) << TAB_SIZE_BITS);
}

/**
 * Create a StringID
 * @param tab StringTab
 * @param index StringIndex
 * @return StringID composed from \a tab and \a index
 */
static inline StringID MakeStringID(StringTab tab, uint index)
{
	if (tab == TEXT_TAB_NEWGRF_START) {
		assert(index < TAB_SIZE_NEWGRF);
	} else if (tab == TEXT_TAB_GAMESCRIPT_START) {
		assert(index < TAB_SIZE_GAMESCRIPT);
	} else {
		assert(tab < TEXT_TAB_END);
		assert(index < TAB_SIZE);
	}
	return (tab << TAB_SIZE_BITS) + index;
}

class StringParameters {
	StringParameters *parent; ///< If not nullptr, this instance references data from this parent instance.
	uint64 *data;             ///< Array with the actual data.
	WChar *type;              ///< Array with type information about the data. Can be nullptr when no type information is needed. See #StringControlCode.

public:
	uint offset;              ///< Current offset in the data/type arrays.
	uint num_param;           ///< Length of the data array.

	/** Create a new StringParameters instance. */
	StringParameters(uint64 *data, uint num_param, WChar *type) :
		parent(nullptr),
		data(data),
		type(type),
		offset(0),
		num_param(num_param)
	{ }

	/** Create a new StringParameters instance. */
	template <size_t Tnum_param>
	StringParameters(int64 (&data)[Tnum_param]) :
		parent(nullptr),
		data((uint64 *)data),
		type(nullptr),
		offset(0),
		num_param(Tnum_param)
	{
		assert_compile(sizeof(data[0]) == sizeof(uint64));
	}

	/**
	 * Create a new StringParameters instance that can reference part of the data of
	 * the given partent instance.
	 */
	StringParameters(StringParameters &parent, uint size) :
		parent(&parent),
		data(parent.data + parent.offset),
		offset(0),
		num_param(size)
	{
		assert(size <= parent.GetDataLeft());
		if (parent.type == nullptr) {
			this->type = nullptr;
		} else {
			this->type = parent.type + parent.offset;
		}
	}

	~StringParameters()
	{
		if (this->parent != nullptr) {
			this->parent->offset += this->num_param;
		}
	}

	void ClearTypeInformation();

	int64 GetInt64(WChar type = 0);

	/** Read an int32 from the argument array. @see GetInt64. */
	int32 GetInt32(WChar type = 0)
	{
		return (int32)this->GetInt64(type);
	}

	void ShiftParameters(uint amount);

	/** Get a pointer to the current element in the data array. */
	uint64 *GetDataPointer() const
	{
		return &this->data[this->offset];
	}

	/** Return the amount of elements which can still be read. */
	uint GetDataLeft() const
	{
		return this->num_param - this->offset;
	}

	/** Get a pointer to a specific element in the data array. */
	uint64 *GetPointerToOffset(uint offset) const
	{
		assert(offset < this->num_param);
		return &this->data[offset];
	}

	/** Does this instance store information about the type of the parameters. */
	bool HasTypeInformation() const
	{
		return this->type != nullptr;
	}

	/** Get the type of a specific element. */
	WChar GetTypeAtOffset(uint offset) const
	{
		assert(offset < this->num_param);
		assert(this->HasTypeInformation());
		return this->type[offset];
	}

	void SetParam(uint n, uint64 v)
	{
		assert(n < this->num_param);
		this->data[n] = v;
	}

	uint64 GetParam(uint n) const
	{
		assert(n < this->num_param);
		return this->data[n];
	}
};
extern StringParameters _global_string_params;

char *GetString(char *buffr, StringID string, const char *last);
char *GetStringWithArgs(char *buffr, StringID string, StringParameters *args, const char *last, uint case_index = 0, bool game_script = false);
const char *GetStringPtr(StringID string);

uint ConvertKmhishSpeedToDisplaySpeed(uint speed);
uint ConvertDisplaySpeedToKmhishSpeed(uint speed);

void InjectDParam(uint amount);

/**
 * Set a string parameter \a v at index \a n in a given array \a s.
 * @param s Array of string parameters.
 * @param n Index of the string parameter.
 * @param v Value of the string parameter.
 */
static inline void SetDParamX(uint64 *s, uint n, uint64 v)
{
	s[n] = v;
}

/**
 * Set a string parameter \a v at index \a n in the global string parameter array.
 * @param n Index of the string parameter.
 * @param v Value of the string parameter.
 */
static inline void SetDParam(uint n, uint64 v)
{
	_global_string_params.SetParam(n, v);
}
template <class T, class U> static inline void SetDParam(uint n, const OverflowSafeInt<T, U>& v) { SetDParam(n, static_cast<int64>(v)); }

void SetDParamMaxValue(uint n, uint64 max_value, uint min_count = 0, FontSize size = FS_NORMAL);
template <class T, class U> static inline void SetDParamMaxValue(uint n, const OverflowSafeInt<T, U>& max_value, uint min_count = 0, FontSize size = FS_NORMAL) { SetDParamMaxValue(n, static_cast<int64>(max_value), min_count, size); }

void SetDParamMaxDigits(uint n, uint count, FontSize size = FS_NORMAL);

void SetDParamStr(uint n, const char *str);

void CopyInDParam(int offs, const uint64 *src, int num);
void CopyOutDParam(uint64 *dst, int offs, int num);
void CopyOutDParam(uint64 *dst, const char **strings, StringID string, int num);

/**
 * Get the current string parameter at index \a n from parameter array \a s.
 * @param s Array of string parameters.
 * @param n Index of the string parameter.
 * @return Value of the requested string parameter.
 */
static inline uint64 GetDParamX(const uint64 *s, uint n)
{
	return s[n];
}

/**
 * Get the current string parameter at index \a n from the global string parameter array.
 * @param n Index of the string parameter.
 * @return Value of the requested string parameter.
 */
static inline uint64 GetDParam(uint n)
{
	return _global_string_params.GetParam(n);
}

extern TextDirection _current_text_dir; ///< Text direction of the currently selected language

void InitializeLanguagePacks();
const char *GetCurrentLanguageIsoCode();

bool StringIDSorter(const StringID &a, const StringID &b);

/**
 * A searcher for missing glyphs.
 */
class MissingGlyphSearcher {
public:
	/** Make sure everything gets destructed right. */
	virtual ~MissingGlyphSearcher() {}

	/**
	 * Get the next string to search through.
	 * @return The next string or nullptr if there is none.
	 */
	virtual const char *NextString() = 0;

	/**
	 * Get the default (font) size of the string.
	 * @return The font size.
	 */
	virtual FontSize DefaultSize() = 0;

	/**
	 * Reset the search, i.e. begin from the beginning again.
	 */
	virtual void Reset() = 0;

	/**
	 * Whether to search for a monospace font or not.
	 * @return True if searching for monospace.
	 */
	virtual bool Monospace() = 0;

	/**
	 * Set the right font names.
	 * @param settings  The settings to modify.
	 * @param font_name The new font name.
	 * @param os_data Opaque pointer to OS-specific data.
	 */
	virtual void SetFontNames(struct FreeTypeSettings *settings, const char *font_name, const void *os_data = nullptr) = 0;

	bool FindMissingGlyphs(const char **str);
};

void CheckForMissingGlyphs(bool base_font = true, MissingGlyphSearcher *search = nullptr);

#endif /* STRINGS_FUNC_H */
