/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file string_func.h Functions related to low-level strings.
 *
 * @note Be aware of "dangerous" string functions; string functions that
 * have behaviour that could easily cause buffer overruns and such:
 * - strncpy: does not '\0' terminate when input string is longer than
 *   the size of the output string. Use strecpy instead.
 * - [v]snprintf: returns the length of the string as it would be written
 *   when the output is large enough, so it can be more than the size of
 *   the buffer and than can underflow size_t (uint-ish) which makes all
 *   subsequent snprintf alikes write outside of the buffer. Use
 *   [v]seprintf instead; it will return the number of bytes actually
 *   added so no [v]seprintf will cause outside of bounds writes.
 * - [v]sprintf: does not bounds checking: use [v]seprintf instead.
 */

#ifndef STRING_FUNC_H
#define STRING_FUNC_H

#include <stdarg.h>
#include <iosfwd>

#include "core/bitmath_func.hpp"
#include "string_type.h"

char *strecat(char *dst, const char *src, const char *last);
char *strecpy(char *dst, const char *src, const char *last);
char *stredup(const char *src, const char *last = nullptr);

int CDECL seprintf(char *str, const char *last, const char *format, ...) WARN_FORMAT(3, 4);
int CDECL vseprintf(char *str, const char *last, const char *format, va_list ap) WARN_FORMAT(3, 0);

char *CDECL str_fmt(const char *str, ...) WARN_FORMAT(1, 2);

void str_validate(char *str, const char *last, StringValidationSettings settings = SVS_REPLACE_WITH_QUESTION_MARK);
std::string str_validate(const std::string &str, StringValidationSettings settings = SVS_REPLACE_WITH_QUESTION_MARK);
void ValidateString(const char *str);

void str_fix_scc_encoded(char *str, const char *last);
void str_strip_colours(char *str);
bool strtolower(char *str);
bool strtolower(std::string &str, std::string::size_type offs = 0);

bool StrValid(const char *str, const char *last);

/**
 * Check if a string buffer is empty.
 *
 * @param s The pointer to the first element of the buffer
 * @return true if the buffer starts with the terminating null-character or
 *         if the given pointer points to nullptr else return false
 */
static inline bool StrEmpty(const char *s)
{
	return s == nullptr || s[0] == '\0';
}

/**
 * Get the length of a string, within a limited buffer.
 *
 * @param str The pointer to the first element of the buffer
 * @param maxlen The maximum size of the buffer
 * @return The length of the string
 */
static inline size_t ttd_strnlen(const char *str, size_t maxlen)
{
	const char *t;
	for (t = str; (size_t)(t - str) < maxlen && *t != '\0'; t++) {}
	return t - str;
}

char *md5sumToString(char *buf, const char *last, const uint8 md5sum[16]);

bool IsValidChar(WChar key, CharSetFilter afilter);

size_t Utf8Decode(WChar *c, const char *s);
size_t Utf8Encode(char *buf, WChar c);
size_t Utf8Encode(std::ostreambuf_iterator<char> &buf, WChar c);
size_t Utf8TrimString(char *s, size_t maxlen);


static inline WChar Utf8Consume(const char **s)
{
	WChar c;
	*s += Utf8Decode(&c, *s);
	return c;
}

template <class Titr>
static inline WChar Utf8Consume(Titr &s)
{
	WChar c;
	s += Utf8Decode(&c, &*s);
	return c;
}

/**
 * Return the length of a UTF-8 encoded character.
 * @param c Unicode character.
 * @return Length of UTF-8 encoding for character.
 */
static inline int8 Utf8CharLen(WChar c)
{
	if (c < 0x80)       return 1;
	if (c < 0x800)      return 2;
	if (c < 0x10000)    return 3;
	if (c < 0x110000)   return 4;

	/* Invalid valid, we encode as a '?' */
	return 1;
}


/**
 * Return the length of an UTF-8 encoded value based on a single char. This
 * char should be the first byte of the UTF-8 encoding. If not, or encoding
 * is invalid, return value is 0
 * @param c char to query length of
 * @return requested size
 */
static inline int8 Utf8EncodedCharLen(char c)
{
	if (GB(c, 3, 5) == 0x1E) return 4;
	if (GB(c, 4, 4) == 0x0E) return 3;
	if (GB(c, 5, 3) == 0x06) return 2;
	if (GB(c, 7, 1) == 0x00) return 1;

	/* Invalid UTF8 start encoding */
	return 0;
}


/* Check if the given character is part of a UTF8 sequence */
static inline bool IsUtf8Part(char c)
{
	return GB(c, 6, 2) == 2;
}

/**
 * Retrieve the previous UNICODE character in an UTF-8 encoded string.
 * @param s char pointer pointing to (the first char of) the next character
 * @return a pointer in 's' to the previous UNICODE character's first byte
 * @note The function should not be used to determine the length of the previous
 * encoded char because it might be an invalid/corrupt start-sequence
 */
static inline char *Utf8PrevChar(char *s)
{
	char *ret = s;
	while (IsUtf8Part(*--ret)) {}
	return ret;
}

static inline const char *Utf8PrevChar(const char *s)
{
	const char *ret = s;
	while (IsUtf8Part(*--ret)) {}
	return ret;
}

size_t Utf8StringLength(const char *s);

/**
 * Is the given character a lead surrogate code point?
 * @param c The character to test.
 * @return True if the character is a lead surrogate code point.
 */
static inline bool Utf16IsLeadSurrogate(uint c)
{
	return c >= 0xD800 && c <= 0xDBFF;
}

/**
 * Is the given character a lead surrogate code point?
 * @param c The character to test.
 * @return True if the character is a lead surrogate code point.
 */
static inline bool Utf16IsTrailSurrogate(uint c)
{
	return c >= 0xDC00 && c <= 0xDFFF;
}

/**
 * Convert an UTF-16 surrogate pair to the corresponding Unicode character.
 * @param lead Lead surrogate code point.
 * @param trail Trail surrogate code point.
 * @return Decoded Unicode character.
 */
static inline WChar Utf16DecodeSurrogate(uint lead, uint trail)
{
	return 0x10000 + (((lead - 0xD800) << 10) | (trail - 0xDC00));
}

/**
 * Decode an UTF-16 character.
 * @param c Pointer to one or two UTF-16 code points.
 * @return Decoded Unicode character.
 */
static inline WChar Utf16DecodeChar(const uint16 *c)
{
	if (Utf16IsLeadSurrogate(c[0])) {
		return Utf16DecodeSurrogate(c[0], c[1]);
	} else {
		return *c;
	}
}

/**
 * Is the given character a text direction character.
 * @param c The character to test.
 * @return true iff the character is used to influence
 *         the text direction.
 */
static inline bool IsTextDirectionChar(WChar c)
{
	switch (c) {
		case CHAR_TD_LRM:
		case CHAR_TD_RLM:
		case CHAR_TD_LRE:
		case CHAR_TD_RLE:
		case CHAR_TD_LRO:
		case CHAR_TD_RLO:
		case CHAR_TD_PDF:
			return true;

		default:
			return false;
	}
}

static inline bool IsPrintable(WChar c)
{
	if (c < 0x20)   return false;
	if (c < 0xE000) return true;
	if (c < 0xE200) return false;
	return true;
}

/**
 * Check whether UNICODE character is whitespace or not, i.e. whether
 * this is a potential line-break character.
 * @param c UNICODE character to check
 * @return a boolean value whether 'c' is a whitespace character or not
 * @see http://www.fileformat.info/info/unicode/category/Zs/list.htm
 */
static inline bool IsWhitespace(WChar c)
{
	return c == 0x0020 /* SPACE */ || c == 0x3000; /* IDEOGRAPHIC SPACE */
}

/* Needed for NetBSD version (so feature) testing */
#if defined(__NetBSD__) || defined(__FreeBSD__)
#include <sys/param.h>
#endif

/* strcasestr is available for _GNU_SOURCE, BSD and some Apple */
#if defined(_GNU_SOURCE) || (defined(__BSD_VISIBLE) && __BSD_VISIBLE) || (defined(__APPLE__) && (!defined(_POSIX_C_SOURCE) || defined(_DARWIN_C_SOURCE))) || defined(_NETBSD_SOURCE)
#	undef DEFINE_STRCASESTR
#else
#	define DEFINE_STRCASESTR
char *strcasestr(const char *haystack, const char *needle);
#endif /* strcasestr is available */

const char *SkipGarbage(const char *str);
int strnatcmp(const char *s1, const char *s2, bool ignore_garbage_at_front = false);

#endif /* STRING_FUNC_H */
