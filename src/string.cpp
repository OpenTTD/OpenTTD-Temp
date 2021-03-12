/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file string.cpp Handling of C-type strings (char*). */

#include "stdafx.h"
#include "debug.h"
#include "core/alloc_func.hpp"
#include "core/math_func.hpp"
#include "string_func.h"
#include "string_base.h"

#include "table/control_codes.h"

#include <stdarg.h>
#include <ctype.h> /* required for tolower() */
#include <sstream>

#ifdef _MSC_VER
#include <errno.h> // required by vsnprintf implementation for MSVC
#endif

#ifdef _WIN32
#include "os/windows/win32.h"
#endif

#ifdef WITH_UNISCRIBE
#include "os/windows/string_uniscribe.h"
#endif

#ifdef WITH_ICU_I18N
/* Required by strnatcmp. */
#include <unicode/ustring.h>
#include "language.h"
#include "gfx_func.h"
#endif /* WITH_ICU_I18N */

#if defined(WITH_COCOA)
#include "os/macosx/string_osx.h"
#endif

/* The function vsnprintf is used internally to perform the required formatting
 * tasks. As such this one must be allowed, and makes sure it's terminated. */
#include "safeguards.h"
#undef vsnprintf

/**
 * Safer implementation of vsnprintf; same as vsnprintf except:
 * - last instead of size, i.e. replace sizeof with lastof.
 * - return gives the amount of characters added, not what it would add.
 * @param str    buffer to write to up to last
 * @param last   last character we may write to
 * @param format the formatting (see snprintf)
 * @param ap     the list of arguments for the format
 * @return the number of added characters
 */
int CDECL vseprintf(char *str, const char *last, const char *format, va_list ap)
{
	ptrdiff_t diff = last - str;
	if (diff < 0) return 0;
	return std::min(static_cast<int>(diff), vsnprintf(str, diff + 1, format, ap));
}

/**
 * Appends characters from one string to another.
 *
 * Appends the source string to the destination string with respect of the
 * terminating null-character and and the last pointer to the last element
 * in the destination buffer. If the last pointer is set to nullptr no
 * boundary check is performed.
 *
 * @note usage: strecat(dst, src, lastof(dst));
 * @note lastof() applies only to fixed size arrays
 *
 * @param dst The buffer containing the target string
 * @param src The buffer containing the string to append
 * @param last The pointer to the last element of the destination buffer
 * @return The pointer to the terminating null-character in the destination buffer
 */
char *strecat(char *dst, const char *src, const char *last)
{
	assert(dst <= last);
	while (*dst != '\0') {
		if (dst == last) return dst;
		dst++;
	}

	return strecpy(dst, src, last);
}


/**
 * Copies characters from one buffer to another.
 *
 * Copies the source string to the destination buffer with respect of the
 * terminating null-character and the last pointer to the last element in
 * the destination buffer. If the last pointer is set to nullptr no boundary
 * check is performed.
 *
 * @note usage: strecpy(dst, src, lastof(dst));
 * @note lastof() applies only to fixed size arrays
 *
 * @param dst The destination buffer
 * @param src The buffer containing the string to copy
 * @param last The pointer to the last element of the destination buffer
 * @return The pointer to the terminating null-character in the destination buffer
 */
char *strecpy(char *dst, const char *src, const char *last)
{
	assert(dst <= last);
	while (dst != last && *src != '\0') {
		*dst++ = *src++;
	}
	*dst = '\0';

	if (dst == last && *src != '\0') {
#if defined(STRGEN) || defined(SETTINGSGEN)
		error("String too long for destination buffer");
#else /* STRGEN || SETTINGSGEN */
		DEBUG(misc, 0, "String too long for destination buffer");
#endif /* STRGEN || SETTINGSGEN */
	}
	return dst;
}

/**
 * Create a duplicate of the given string.
 * @param s    The string to duplicate.
 * @param last The last character that is safe to duplicate. If nullptr, the whole string is duplicated.
 * @note The maximum length of the resulting string might therefore be last - s + 1.
 * @return The duplicate of the string.
 */
char *stredup(const char *s, const char *last)
{
	size_t len = last == nullptr ? strlen(s) : ttd_strnlen(s, last - s + 1);
	char *tmp = CallocT<char>(len + 1);
	memcpy(tmp, s, len);
	return tmp;
}

/**
 * Format, "printf", into a newly allocated string.
 * @param str The formatting string.
 * @return The formatted string. You must free this!
 */
char *CDECL str_fmt(const char *str, ...)
{
	char buf[4096];
	va_list va;

	va_start(va, str);
	int len = vseprintf(buf, lastof(buf), str, va);
	va_end(va);
	char *p = MallocT<char>(len + 1);
	memcpy(p, buf, len + 1);
	return p;
}

/**
 * Scan the string for old values of SCC_ENCODED and fix it to
 * it's new, static value.
 * @param str the string to scan
 * @param last the last valid character of str
 */
void str_fix_scc_encoded(char *str, const char *last)
{
	while (str <= last && *str != '\0') {
		size_t len = Utf8EncodedCharLen(*str);
		if ((len == 0 && str + 4 > last) || str + len > last) break;

		WChar c;
		Utf8Decode(&c, str);
		if (c == '\0') break;

		if (c == 0xE028 || c == 0xE02A) {
			c = SCC_ENCODED;
		}
		str += Utf8Encode(str, c);
	}
	*str = '\0';
}


template <class T>
static void str_validate(T &dst, const char *str, const char *last, StringValidationSettings settings)
{
	/* Assume the ABSOLUTE WORST to be in str as it comes from the outside. */

	while (str <= last && *str != '\0') {
		size_t len = Utf8EncodedCharLen(*str);
		/* If the character is unknown, i.e. encoded length is 0
		 * we assume worst case for the length check.
		 * The length check is needed to prevent Utf8Decode to read
		 * over the terminating '\0' if that happens to be placed
		 * within the encoding of an UTF8 character. */
		if ((len == 0 && str + 4 > last) || str + len > last) break;

		WChar c;
		len = Utf8Decode(&c, str);
		/* It's possible to encode the string termination character
		 * into a multiple bytes. This prevents those termination
		 * characters to be skipped */
		if (c == '\0') break;

		if ((IsPrintable(c) && (c < SCC_SPRITE_START || c > SCC_SPRITE_END)) || ((settings & SVS_ALLOW_CONTROL_CODE) != 0 && c == SCC_ENCODED)) {
			/* Copy the character back. Even if dst is current the same as str
			 * (i.e. no characters have been changed) this is quicker than
			 * moving the pointers ahead by len */
			do {
				*dst++ = *str++;
			} while (--len != 0);
		} else if ((settings & SVS_ALLOW_NEWLINE) != 0 && c == '\n') {
			*dst++ = *str++;
		} else {
			if ((settings & SVS_ALLOW_NEWLINE) != 0 && c == '\r' && str[1] == '\n') {
				str += len;
				continue;
			}
			/* Replace the undesirable character with a question mark */
			str += len;
			if ((settings & SVS_REPLACE_WITH_QUESTION_MARK) != 0) *dst++ = '?';
		}
	}
}

/**
 * Scans the string for valid characters and if it finds invalid ones,
 * replaces them with a question mark '?' (if not ignored)
 * @param str the string to validate
 * @param last the last valid character of str
 * @param settings the settings for the string validation.
 */
void str_validate(char *str, const char *last, StringValidationSettings settings)
{
	char *dst = str;
	str_validate(dst, str, last, settings);
	*dst = '\0';
}

/**
 * Scans the string for valid characters and if it finds invalid ones,
 * replaces them with a question mark '?' (if not ignored)
 * @param str the string to validate
 * @param settings the settings for the string validation.
 */
std::string str_validate(const std::string &str, StringValidationSettings settings)
{
	auto buf = str.data();
	auto last = buf + str.size();

	std::ostringstream dst;
	std::ostreambuf_iterator<char> dst_iter(dst);
	str_validate(dst_iter, buf, last, settings);

	return dst.str();
}

/**
 * Scans the string for valid characters and if it finds invalid ones,
 * replaces them with a question mark '?'.
 * @param str the string to validate
 */
void ValidateString(const char *str)
{
	/* We know it is '\0' terminated. */
	str_validate(const_cast<char *>(str), str + strlen(str) + 1);
}


/**
 * Checks whether the given string is valid, i.e. contains only
 * valid (printable) characters and is properly terminated.
 * @param str  The string to validate.
 * @param last The last character of the string, i.e. the string
 *             must be terminated here or earlier.
 */
bool StrValid(const char *str, const char *last)
{
	/* Assume the ABSOLUTE WORST to be in str as it comes from the outside. */

	while (str <= last && *str != '\0') {
		size_t len = Utf8EncodedCharLen(*str);
		/* Encoded length is 0 if the character isn't known.
		 * The length check is needed to prevent Utf8Decode to read
		 * over the terminating '\0' if that happens to be placed
		 * within the encoding of an UTF8 character. */
		if (len == 0 || str + len > last) return false;

		WChar c;
		len = Utf8Decode(&c, str);
		if (!IsPrintable(c) || (c >= SCC_SPRITE_START && c <= SCC_SPRITE_END)) {
			return false;
		}

		str += len;
	}

	return *str == '\0';
}

/** Scans the string for colour codes and strips them */
void str_strip_colours(char *str)
{
	char *dst = str;
	WChar c;
	size_t len;

	for (len = Utf8Decode(&c, str); c != '\0'; len = Utf8Decode(&c, str)) {
		if (c < SCC_BLUE || c > SCC_BLACK) {
			/* Copy the character back. Even if dst is current the same as str
			 * (i.e. no characters have been changed) this is quicker than
			 * moving the pointers ahead by len */
			do {
				*dst++ = *str++;
			} while (--len != 0);
		} else {
			/* Just skip (strip) the colour codes */
			str += len;
		}
	}
	*dst = '\0';
}

/**
 * Get the length of an UTF-8 encoded string in number of characters
 * and thus not the number of bytes that the encoded string contains.
 * @param s The string to get the length for.
 * @return The length of the string in characters.
 */
size_t Utf8StringLength(const char *s)
{
	size_t len = 0;
	const char *t = s;
	while (Utf8Consume(&t) != 0) len++;
	return len;
}


/**
 * Convert a given ASCII string to lowercase.
 * NOTE: only support ASCII characters, no UTF8 fancy. As currently
 * the function is only used to lowercase data-filenames if they are
 * not found, this is sufficient. If more, or general functionality is
 * needed, look to r7271 where it was removed because it was broken when
 * using certain locales: eg in Turkish the uppercase 'I' was converted to
 * '?', so just revert to the old functionality
 * @param str string to convert
 * @return String has changed.
 */
bool strtolower(char *str)
{
	bool changed = false;
	for (; *str != '\0'; str++) {
		char new_str = tolower(*str);
		changed |= new_str != *str;
		*str = new_str;
	}
	return changed;
}

bool strtolower(std::string &str, std::string::size_type offs)
{
	bool changed = false;
	for (auto ch = str.begin() + offs; ch != str.end(); ++ch) {
		auto new_ch = static_cast<char>(tolower(static_cast<unsigned char>(*ch)));
		changed |= new_ch != *ch;
		*ch = new_ch;
	}
	return changed;
}

/**
 * Only allow certain keys. You can define the filter to be used. This makes
 *  sure no invalid keys can get into an editbox, like BELL.
 * @param key character to be checked
 * @param afilter the filter to use
 * @return true or false depending if the character is printable/valid or not
 */
bool IsValidChar(WChar key, CharSetFilter afilter)
{
	switch (afilter) {
		case CS_ALPHANUMERAL:  return IsPrintable(key);
		case CS_NUMERAL:       return (key >= '0' && key <= '9');
		case CS_NUMERAL_SPACE: return (key >= '0' && key <= '9') || key == ' ';
		case CS_ALPHA:         return IsPrintable(key) && !(key >= '0' && key <= '9');
		case CS_HEXADECIMAL:   return (key >= '0' && key <= '9') || (key >= 'a' && key <= 'f') || (key >= 'A' && key <= 'F');
		default: NOT_REACHED();
	}
}

#ifdef _WIN32
#if defined(_MSC_VER) && _MSC_VER < 1900
/**
 * Almost POSIX compliant implementation of \c vsnprintf for VC compiler.
 * The difference is in the value returned on output truncation. This
 * implementation returns size whereas a POSIX implementation returns
 * size or more (the number of bytes that would be written to str
 * had size been sufficiently large excluding the terminating null byte).
 */
int CDECL vsnprintf(char *str, size_t size, const char *format, va_list ap)
{
	if (size == 0) return 0;

	errno = 0;
	int ret = _vsnprintf(str, size, format, ap);

	if (ret < 0) {
		if (errno != ERANGE) {
			/* There's a formatting error, better get that looked
			 * at properly instead of ignoring it. */
			NOT_REACHED();
		}
	} else if ((size_t)ret < size) {
		/* The buffer is big enough for the number of
		 * characters stored (excluding null), i.e.
		 * the string has been null-terminated. */
		return ret;
	}

	/* The buffer is too small for _vsnprintf to write the
	 * null-terminator at its end and return size. */
	str[size - 1] = '\0';
	return (int)size;
}
#endif /* _MSC_VER */

#endif /* _WIN32 */

/**
 * Safer implementation of snprintf; same as snprintf except:
 * - last instead of size, i.e. replace sizeof with lastof.
 * - return gives the amount of characters added, not what it would add.
 * @param str    buffer to write to up to last
 * @param last   last character we may write to
 * @param format the formatting (see snprintf)
 * @return the number of added characters
 */
int CDECL seprintf(char *str, const char *last, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	int ret = vseprintf(str, last, format, ap);
	va_end(ap);
	return ret;
}


/**
 * Convert the md5sum to a hexadecimal string representation
 * @param buf buffer to put the md5sum into
 * @param last last character of buffer (usually lastof(buf))
 * @param md5sum the md5sum itself
 * @return a pointer to the next character after the md5sum
 */
char *md5sumToString(char *buf, const char *last, const uint8 md5sum[16])
{
	char *p = buf;

	for (uint i = 0; i < 16; i++) {
		p += seprintf(p, last, "%02X", md5sum[i]);
	}

	return p;
}


/* UTF-8 handling routines */


/**
 * Decode and consume the next UTF-8 encoded character.
 * @param c Buffer to place decoded character.
 * @param s Character stream to retrieve character from.
 * @return Number of characters in the sequence.
 */
size_t Utf8Decode(WChar *c, const char *s)
{
	assert(c != nullptr);

	if (!HasBit(s[0], 7)) {
		/* Single byte character: 0xxxxxxx */
		*c = s[0];
		return 1;
	} else if (GB(s[0], 5, 3) == 6) {
		if (IsUtf8Part(s[1])) {
			/* Double byte character: 110xxxxx 10xxxxxx */
			*c = GB(s[0], 0, 5) << 6 | GB(s[1], 0, 6);
			if (*c >= 0x80) return 2;
		}
	} else if (GB(s[0], 4, 4) == 14) {
		if (IsUtf8Part(s[1]) && IsUtf8Part(s[2])) {
			/* Triple byte character: 1110xxxx 10xxxxxx 10xxxxxx */
			*c = GB(s[0], 0, 4) << 12 | GB(s[1], 0, 6) << 6 | GB(s[2], 0, 6);
			if (*c >= 0x800) return 3;
		}
	} else if (GB(s[0], 3, 5) == 30) {
		if (IsUtf8Part(s[1]) && IsUtf8Part(s[2]) && IsUtf8Part(s[3])) {
			/* 4 byte character: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
			*c = GB(s[0], 0, 3) << 18 | GB(s[1], 0, 6) << 12 | GB(s[2], 0, 6) << 6 | GB(s[3], 0, 6);
			if (*c >= 0x10000 && *c <= 0x10FFFF) return 4;
		}
	}

	/* DEBUG(misc, 1, "[utf8] invalid UTF-8 sequence"); */
	*c = '?';
	return 1;
}


/**
 * Encode a unicode character and place it in the buffer.
 * @tparam T Type of the buffer.
 * @param buf Buffer to place character.
 * @param c   Unicode character to encode.
 * @return Number of characters in the encoded sequence.
 */
template <class T>
inline size_t Utf8Encode(T buf, WChar c)
{
	if (c < 0x80) {
		*buf = c;
		return 1;
	} else if (c < 0x800) {
		*buf++ = 0xC0 + GB(c,  6, 5);
		*buf   = 0x80 + GB(c,  0, 6);
		return 2;
	} else if (c < 0x10000) {
		*buf++ = 0xE0 + GB(c, 12, 4);
		*buf++ = 0x80 + GB(c,  6, 6);
		*buf   = 0x80 + GB(c,  0, 6);
		return 3;
	} else if (c < 0x110000) {
		*buf++ = 0xF0 + GB(c, 18, 3);
		*buf++ = 0x80 + GB(c, 12, 6);
		*buf++ = 0x80 + GB(c,  6, 6);
		*buf   = 0x80 + GB(c,  0, 6);
		return 4;
	}

	/* DEBUG(misc, 1, "[utf8] can't UTF-8 encode value 0x%X", c); */
	*buf = '?';
	return 1;
}

size_t Utf8Encode(char *buf, WChar c)
{
	return Utf8Encode<char *>(buf, c);
}

size_t Utf8Encode(std::ostreambuf_iterator<char> &buf, WChar c)
{
	return Utf8Encode<std::ostreambuf_iterator<char> &>(buf, c);
}

/**
 * Properly terminate an UTF8 string to some maximum length
 * @param s string to check if it needs additional trimming
 * @param maxlen the maximum length the buffer can have.
 * @return the new length in bytes of the string (eg. strlen(new_string))
 * @note maxlen is the string length _INCLUDING_ the terminating '\0'
 */
size_t Utf8TrimString(char *s, size_t maxlen)
{
	size_t length = 0;

	for (const char *ptr = strchr(s, '\0'); *s != '\0';) {
		size_t len = Utf8EncodedCharLen(*s);
		/* Silently ignore invalid UTF8 sequences, our only concern trimming */
		if (len == 0) len = 1;

		/* Take care when a hard cutoff was made for the string and
		 * the last UTF8 sequence is invalid */
		if (length + len >= maxlen || (s + len > ptr)) break;
		s += len;
		length += len;
	}

	*s = '\0';
	return length;
}

#ifdef DEFINE_STRCASESTR
char *strcasestr(const char *haystack, const char *needle)
{
	size_t hay_len = strlen(haystack);
	size_t needle_len = strlen(needle);
	while (hay_len >= needle_len) {
		if (strncasecmp(haystack, needle, needle_len) == 0) return const_cast<char *>(haystack);

		haystack++;
		hay_len--;
	}

	return nullptr;
}
#endif /* DEFINE_STRCASESTR */

/**
 * Skip some of the 'garbage' in the string that we don't want to use
 * to sort on. This way the alphabetical sorting will work better as
 * we would be actually using those characters instead of some other
 * characters such as spaces and tildes at the begin of the name.
 * @param str The string to skip the initial garbage of.
 * @return The string with the garbage skipped.
 */
const char *SkipGarbage(const char *str)
{
	while (*str != '\0' && (*str < '0' || IsInsideMM(*str, ';', '@' + 1) || IsInsideMM(*str, '[', '`' + 1) || IsInsideMM(*str, '{', '~' + 1))) str++;
	return str;
}

/**
 * Compares two strings using case insensitive natural sort.
 *
 * @param s1 First string to compare.
 * @param s2 Second string to compare.
 * @param ignore_garbage_at_front Skip punctuation characters in the front
 * @return Less than zero if s1 < s2, zero if s1 == s2, greater than zero if s1 > s2.
 */
int strnatcmp(const char *s1, const char *s2, bool ignore_garbage_at_front)
{
	if (ignore_garbage_at_front) {
		s1 = SkipGarbage(s1);
		s2 = SkipGarbage(s2);
	}

#ifdef WITH_ICU_I18N
	if (_current_collator) {
		UErrorCode status = U_ZERO_ERROR;
		int result = _current_collator->compareUTF8(s1, s2, status);
		if (U_SUCCESS(status)) return result;
	}
#endif /* WITH_ICU_I18N */

#if defined(_WIN32) && !defined(STRGEN) && !defined(SETTINGSGEN)
	int res = OTTDStringCompare(s1, s2);
	if (res != 0) return res - 2; // Convert to normal C return values.
#endif

#if defined(WITH_COCOA) && !defined(STRGEN) && !defined(SETTINGSGEN)
	int res = MacOSStringCompare(s1, s2);
	if (res != 0) return res - 2; // Convert to normal C return values.
#endif

	/* Do a normal comparison if ICU is missing or if we cannot create a collator. */
	return strcasecmp(s1, s2);
}

#ifdef WITH_UNISCRIBE

/* static */ StringIterator *StringIterator::Create()
{
	return new UniscribeStringIterator();
}

#elif defined(WITH_ICU_I18N)

#include <unicode/utext.h>
#include <unicode/brkiter.h>

/** String iterator using ICU as a backend. */
class IcuStringIterator : public StringIterator
{
	icu::BreakIterator *char_itr; ///< ICU iterator for characters.
	icu::BreakIterator *word_itr; ///< ICU iterator for words.

	std::vector<UChar> utf16_str;      ///< UTF-16 copy of the string.
	std::vector<size_t> utf16_to_utf8; ///< Mapping from UTF-16 code point position to index in the UTF-8 source string.

public:
	IcuStringIterator() : char_itr(nullptr), word_itr(nullptr)
	{
		UErrorCode status = U_ZERO_ERROR;
		this->char_itr = icu::BreakIterator::createCharacterInstance(icu::Locale(_current_language != nullptr ? _current_language->isocode : "en"), status);
		this->word_itr = icu::BreakIterator::createWordInstance(icu::Locale(_current_language != nullptr ? _current_language->isocode : "en"), status);

		this->utf16_str.push_back('\0');
		this->utf16_to_utf8.push_back(0);
	}

	~IcuStringIterator() override
	{
		delete this->char_itr;
		delete this->word_itr;
	}

	void SetString(const char *s) override
	{
		const char *string_base = s;

		/* Unfortunately current ICU versions only provide rudimentary support
		 * for word break iterators (especially for CJK languages) in combination
		 * with UTF-8 input. As a work around we have to convert the input to
		 * UTF-16 and create a mapping back to UTF-8 character indices. */
		this->utf16_str.clear();
		this->utf16_to_utf8.clear();

		while (*s != '\0') {
			size_t idx = s - string_base;

			WChar c = Utf8Consume(&s);
			if (c < 0x10000) {
				this->utf16_str.push_back((UChar)c);
			} else {
				/* Make a surrogate pair. */
				this->utf16_str.push_back((UChar)(0xD800 + ((c - 0x10000) >> 10)));
				this->utf16_str.push_back((UChar)(0xDC00 + ((c - 0x10000) & 0x3FF)));
				this->utf16_to_utf8.push_back(idx);
			}
			this->utf16_to_utf8.push_back(idx);
		}
		this->utf16_str.push_back('\0');
		this->utf16_to_utf8.push_back(s - string_base);

		UText text = UTEXT_INITIALIZER;
		UErrorCode status = U_ZERO_ERROR;
		utext_openUChars(&text, this->utf16_str.data(), this->utf16_str.size() - 1, &status);
		this->char_itr->setText(&text, status);
		this->word_itr->setText(&text, status);
		this->char_itr->first();
		this->word_itr->first();
	}

	size_t SetCurPosition(size_t pos) override
	{
		/* Convert incoming position to an UTF-16 string index. */
		uint utf16_pos = 0;
		for (uint i = 0; i < this->utf16_to_utf8.size(); i++) {
			if (this->utf16_to_utf8[i] == pos) {
				utf16_pos = i;
				break;
			}
		}

		/* isBoundary has the documented side-effect of setting the current
		 * position to the first valid boundary equal to or greater than
		 * the passed value. */
		this->char_itr->isBoundary(utf16_pos);
		return this->utf16_to_utf8[this->char_itr->current()];
	}

	size_t Next(IterType what) override
	{
		int32_t pos;
		switch (what) {
			case ITER_CHARACTER:
				pos = this->char_itr->next();
				break;

			case ITER_WORD:
				pos = this->word_itr->following(this->char_itr->current());
				/* The ICU word iterator considers both the start and the end of a word a valid
				 * break point, but we only want word starts. Move to the next location in
				 * case the new position points to whitespace. */
				while (pos != icu::BreakIterator::DONE &&
						IsWhitespace(Utf16DecodeChar((const uint16 *)&this->utf16_str[pos]))) {
					int32_t new_pos = this->word_itr->next();
					/* Don't set it to DONE if it was valid before. Otherwise we'll return END
					 * even though the iterator wasn't at the end of the string before. */
					if (new_pos == icu::BreakIterator::DONE) break;
					pos = new_pos;
				}

				this->char_itr->isBoundary(pos);
				break;

			default:
				NOT_REACHED();
		}

		return pos == icu::BreakIterator::DONE ? END : this->utf16_to_utf8[pos];
	}

	size_t Prev(IterType what) override
	{
		int32_t pos;
		switch (what) {
			case ITER_CHARACTER:
				pos = this->char_itr->previous();
				break;

			case ITER_WORD:
				pos = this->word_itr->preceding(this->char_itr->current());
				/* The ICU word iterator considers both the start and the end of a word a valid
				 * break point, but we only want word starts. Move to the previous location in
				 * case the new position points to whitespace. */
				while (pos != icu::BreakIterator::DONE &&
						IsWhitespace(Utf16DecodeChar((const uint16 *)&this->utf16_str[pos]))) {
					int32_t new_pos = this->word_itr->previous();
					/* Don't set it to DONE if it was valid before. Otherwise we'll return END
					 * even though the iterator wasn't at the start of the string before. */
					if (new_pos == icu::BreakIterator::DONE) break;
					pos = new_pos;
				}

				this->char_itr->isBoundary(pos);
				break;

			default:
				NOT_REACHED();
		}

		return pos == icu::BreakIterator::DONE ? END : this->utf16_to_utf8[pos];
	}
};

/* static */ StringIterator *StringIterator::Create()
{
	return new IcuStringIterator();
}

#else

/** Fallback simple string iterator. */
class DefaultStringIterator : public StringIterator
{
	const char *string; ///< Current string.
	size_t len;         ///< String length.
	size_t cur_pos;     ///< Current iteration position.

public:
	DefaultStringIterator() : string(nullptr), len(0), cur_pos(0)
	{
	}

	virtual void SetString(const char *s)
	{
		this->string = s;
		this->len = strlen(s);
		this->cur_pos = 0;
	}

	virtual size_t SetCurPosition(size_t pos)
	{
		assert(this->string != nullptr && pos <= this->len);
		/* Sanitize in case we get a position inside an UTF-8 sequence. */
		while (pos > 0 && IsUtf8Part(this->string[pos])) pos--;
		return this->cur_pos = pos;
	}

	virtual size_t Next(IterType what)
	{
		assert(this->string != nullptr);

		/* Already at the end? */
		if (this->cur_pos >= this->len) return END;

		switch (what) {
			case ITER_CHARACTER: {
				WChar c;
				this->cur_pos += Utf8Decode(&c, this->string + this->cur_pos);
				return this->cur_pos;
			}

			case ITER_WORD: {
				WChar c;
				/* Consume current word. */
				size_t offs = Utf8Decode(&c, this->string + this->cur_pos);
				while (this->cur_pos < this->len && !IsWhitespace(c)) {
					this->cur_pos += offs;
					offs = Utf8Decode(&c, this->string + this->cur_pos);
				}
				/* Consume whitespace to the next word. */
				while (this->cur_pos < this->len && IsWhitespace(c)) {
					this->cur_pos += offs;
					offs = Utf8Decode(&c, this->string + this->cur_pos);
				}

				return this->cur_pos;
			}

			default:
				NOT_REACHED();
		}

		return END;
	}

	virtual size_t Prev(IterType what)
	{
		assert(this->string != nullptr);

		/* Already at the beginning? */
		if (this->cur_pos == 0) return END;

		switch (what) {
			case ITER_CHARACTER:
				return this->cur_pos = Utf8PrevChar(this->string + this->cur_pos) - this->string;

			case ITER_WORD: {
				const char *s = this->string + this->cur_pos;
				WChar c;
				/* Consume preceding whitespace. */
				do {
					s = Utf8PrevChar(s);
					Utf8Decode(&c, s);
				} while (s > this->string && IsWhitespace(c));
				/* Consume preceding word. */
				while (s > this->string && !IsWhitespace(c)) {
					s = Utf8PrevChar(s);
					Utf8Decode(&c, s);
				}
				/* Move caret back to the beginning of the word. */
				if (IsWhitespace(c)) Utf8Consume(&s);

				return this->cur_pos = s - this->string;
			}

			default:
				NOT_REACHED();
		}

		return END;
	}
};

#if defined(WITH_COCOA) && !defined(STRGEN) && !defined(SETTINGSGEN)
/* static */ StringIterator *StringIterator::Create()
{
	StringIterator *i = OSXStringIterator::Create();
	if (i != nullptr) return i;

	return new DefaultStringIterator();
}
#else
/* static */ StringIterator *StringIterator::Create()
{
	return new DefaultStringIterator();
}
#endif /* defined(WITH_COCOA) && !defined(STRGEN) && !defined(SETTINGSGEN) */

#endif
