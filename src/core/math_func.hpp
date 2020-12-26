/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file math_func.hpp Integer math functions */

#ifndef MATH_FUNC_HPP
#define MATH_FUNC_HPP

#include "../stdafx.h"
#include <limits>

/**
 * Returns the maximum of two values.
 *
 * This function returns the greater value of two given values.
 * If they are equal the value of a is returned.
 *
 * @param a The first value
 * @param b The second value
 * @return The greater value or a if equals
 */
template <typename T>
static inline constexpr T max(const T& a, const T& b)
{
	return (a >= b) ? a : b;
}

/**
 * Returns the minimum of two values.
 *
 * This function returns the smaller value of two given values.
 * If they are equal the value of b is returned.
 *
 * @param a The first value
 * @param b The second value
 * @return The smaller value or b if equals
 */
template <typename T>
static inline constexpr T min(const T& a, const T& b)
{
	return (a < b) ? a : b;
}

/**
 * Returns the minimum of two integer.
 *
 * This function returns the smaller value of two given integers.
 *
 * @param a The first integer
 * @param b The second integer
 * @return The smaller value
 */
static inline int min(const int a, const int b)
{
	return min<int>(a, b);
}

/**
 * Returns the minimum of two unsigned integers.
 *
 * This function returns the smaller value of two given unsigned integers.
 *
 * @param a The first unsigned integer
 * @param b The second unsigned integer
 * @return The smaller value
 */
static inline uint minu(const uint a, const uint b)
{
	return min<uint>(a, b);
}

/**
 * Returns the absolute value of (scalar) variable.
 *
 * @note assumes variable to be signed
 * @param a The value we want to unsign
 * @return The unsigned value
 */
template <typename T>
static inline constexpr T abs(const T& a)
{
	return (a < (T)0) ? -a : a;
}

/**
 * Returns the sign of (scalar) variable.
 *
 * @param a The value to obtain the sign of
 * @return -1 if a < 0, +1 if a > 0, a otherwise
 */
template <typename T>
static inline constexpr T signum(const T& a)
{
	return likely(a > (T)0) ? (T)1 : (likely(a < (T)0) ? (T)(-1) : a);
}

/**
 * Return the smallest multiple of n equal or greater than x
 *
 * @note n must be a power of 2
 * @param x The min value
 * @param n The base of the number we are searching
 * @return The smallest multiple of n equal or greater than x
 */
template <typename T>
static inline T Align(const T x, uint n)
{
	assert((n & (n - 1)) == 0 && n != 0);
	n--;
	return (T)((x + n) & ~((T)n));
}

/**
 * Return the smallest multiple of n equal or greater than x
 * Applies to pointers only
 *
 * @note n must be a power of 2
 * @param x The min value
 * @param n The base of the number we are searching
 * @return The smallest multiple of n equal or greater than x
 * @see Align()
 */
template <typename T>
static inline T *AlignPtr(T *x, uint n)
{
	assert_compile(sizeof(size_t) == sizeof(void *));
	return reinterpret_cast<T *>(Align((size_t)x, n));
}

/**
 * Clamp a value between an interval.
 *
 * This function returns a value which is between the given interval of
 * min and max. If the given value is in this interval the value itself
 * is returned otherwise the border of the interval is returned, according
 * which side of the interval was 'left'.
 *
 * @note The min value must be less or equal of max or you get some
 *       unexpected results.
 * @param a The value to clamp/truncate.
 * @param min The minimum of the interval.
 * @param max the maximum of the interval.
 * @returns A value between min and max which is closest to a.
 * @see ClampU(uint, uint, uint)
 * @see Clamp(int, int, int)
 */
template <typename T>
static inline constexpr T Clamp(const T& a, const T& min, const T& max)
{
	return unlikely(max < min) ? Clamp<T>(a, max, min) : unlikely(a < min) ? min : unlikely(a > max) ? max : a;
}

/**
 * Clamp an integer between an interval.
 *
 * This function returns a value which is between the given interval of
 * min and max. If the given value is in this interval the value itself
 * is returned otherwise the border of the interval is returned, according
 * which side of the interval was 'left'.
 *
 * @note The min value must be less or equal of max or you get some
 *       unexpected results.
 * @param a The value to clamp/truncate.
 * @param min The minimum of the interval.
 * @param max the maximum of the interval.
 * @returns A value between min and max which is closest to a.
 * @see ClampU(uint, uint, uint)
 */
static inline int Clamp(const int a, const int min, const int max)
{
	return Clamp<int>(a, min, max);
}

/**
 * Clamp an unsigned integer between an interval.
 *
 * This function returns a value which is between the given interval of
 * min and max. If the given value is in this interval the value itself
 * is returned otherwise the border of the interval is returned, according
 * which side of the interval was 'left'.
 *
 * @note The min value must be less or equal of max or you get some
 *       unexpected results.
 * @param a The value to clamp/truncate.
 * @param min The minimum of the interval.
 * @param max the maximum of the interval.
 * @returns A value between min and max which is closest to a.
 * @see Clamp(int, int, int)
 */
static inline uint ClampU(const uint a, const uint min, const uint max)
{
	return Clamp<uint>(a, min, max);
}

/**
 * Reduce one integral type to another integral type by using saturation arithmetic
 *
 * @tparam T The type to reduce to
 * @tparam U The type to reduce from
 * @param a The value to reduce
 * @return The value, clamped to fit within the result type
 * @see Clamp(int, int, int)
 */
template <class T, class U, class = typename std::enable_if<std::is_integral<T>::value && std::is_integral<U>::value>::type>
static inline constexpr T ClampTo(const U& a)
{
	return static_cast<T>(Clamp<U>(a,
			/* min */ static_cast<U>(max(static_cast<intmax_t>(std::numeric_limits<T>::min()), static_cast<intmax_t>(std::numeric_limits<U>::min()))),
			/* max */ static_cast<U>(min(static_cast<uintmax_t>(std::numeric_limits<T>::max()), static_cast<uintmax_t>(std::numeric_limits<U>::max())))));
}

/**
 * Reduce a signed 64-bit int to a signed 32-bit one
 *
 * This function clamps a 64-bit integer to a 32-bit integer.
 * If the 64-bit value is smaller than the smallest 32-bit integer
 * value 0x80000000 this value is returned (the left one bit is the sign bit).
 * If the 64-bit value is greater than the greatest 32-bit integer value 0x7FFFFFFF
 * this value is returned. In all other cases the 64-bit value 'fits' in a
 * 32-bits integer field and so the value is casted to int32 and returned.
 *
 * @tparam T The type of the value to clamp (default int64)
 * @param a The 64-bit value to clamp
 * @return The 64-bit value reduced to a 32-bit value
 * @see Clamp(int, int, int)
 */
template <class T = int64>
static inline int32 ClampToI32(const T a)
{
	return ClampTo<int32, T>(a);
}

/**
 * Reduce an unsigned 64-bit int to an unsigned 16-bit one
 *
 * @tparam T The type of the value to clamp (default uint64)
 * @param a The 64-bit value to clamp
 * @return The 64-bit value reduced to a 16-bit value
 * @see ClampU(uint, uint, uint)
 */
template <class T = uint64>
static inline uint16 ClampToU16(const T a)
{
	return ClampTo<uint16, T>(a);
}

/**
 * Returns the (absolute) difference between two (scalar) variables
 *
 * @param a The first scalar
 * @param b The second scalar
 * @return The absolute difference between the given scalars
 */
template <typename T>
static inline T Delta(const T a, const T b)
{
	return (a < b) ? b - a : a - b;
}

/**
 * Checks if a value is between a window started at some base point.
 *
 * This function checks if the value x is between the value of base
 * and base+size. If x equals base this returns true. If x equals
 * base+size this returns false.
 *
 * @param x The value to check
 * @param base The base value of the interval
 * @param size The size of the interval
 * @return True if the value is in the interval, false else.
 * @see IsInsideMM()
 */
template <typename T>
static inline bool IsInsideBS(const T x, const size_t base, const size_t size)
{
	return (size_t)(x - base) < size;
}

/**
 * Checks if a value is in an interval.
 *
 * Returns true if a value is in the interval of [min, max).
 *
 * @param x The value to check
 * @param min The minimum of the interval
 * @param max The maximum of the interval
 * @see IsInsideBS()
 */
template <typename T>
static inline bool IsInsideMM(const T x, const size_t min, const size_t max)
{
	return (size_t)(x - min) < (max - min);
}

/**
 * Type safe swap operation
 * @param a variable to swap with b
 * @param b variable to swap with a
 */
template <typename T>
static inline void Swap(T &a, T &b)
{
	T t = a;
	a = b;
	b = t;
}

/**
 * Converts a "fract" value 0..255 to "percent" value 0..100
 * @param i value to convert, range 0..255
 * @return value in range 0..100
 */
static inline uint ToPercent8(uint i)
{
	assert(i < 256);
	return i * 101 >> 8;
}

/**
 * Converts a "fract" value 0..65535 to "percent" value 0..100
 * @param i value to convert, range 0..65535
 * @return value in range 0..100
 */
static inline uint ToPercent16(uint i)
{
	assert(i < 65536);
	return i * 101 >> 16;
}

int LeastCommonMultiple(int a, int b);
int GreatestCommonDivisor(int a, int b);
int DivideApprox(int a, int b);

/**
 * Computes ceil(a / b) for non-negative a and b.
 * @param a Numerator.
 * @param b Denominator.
 * @return Quotient, rounded up.
 */
static inline uint CeilDiv(const uint a, const uint b)
{
	return (a + b - 1) / b;
}

/**
 * Computes ceil(a / b) * b for non-negative a and b.
 * @param a Numerator.
 * @param b Denominator.
 * @return a rounded up to the nearest multiple of b.
 */
static inline uint Ceil(const uint a, const uint b)
{
	return CeilDiv(a, b) * b;
}

/**
 * Computes round(a / b) for b > 0.
 * @param a Numerator.
 * @param b Denominator.
 * @return Quotient, rounded to nearest.
 */
static inline int RoundDivSU(const int a, const uint b)
{
	if (unlikely(b >= static_cast<uint>(std::numeric_limits<int>::max()))) {
		/* This is correct for everything except the 0.5 boundary condition;
		 * and certainly more correct than unchecked numeric overflow. */
		return (static_cast<uint>(abs(a)) > (b / 2)) ? signum(a) : 0;
	}

	const int b_ = static_cast<int>(b);
	if (a > 0) {
		/* 0.5 is rounded to 1 */
		return (a + b_ / 2) / b_;
	} else {
		/* -0.5 is rounded to 0 */
		return (a - (b_ - 1) / 2) / b_;
	}
}

/**
 * Computes (a / b) rounded away from zero for b > 0.
 * @param a Numerator.
 * @param b Denominator.
 * @return Quotient, rounded away from zero.
 */
static inline int DivAwayFromZero(const int a, const uint b)
{
	if (unlikely(b >= static_cast<uint>(std::numeric_limits<int>::max()))) return signum(a);

	const int b_ = static_cast<int>(b);
	if (a > 0) {
		return (a + b_ - 1) / b_;
	} else {
		/* Note: Behaviour of negative numerator division is truncation toward zero. */
		return (a - b_ + 1) / b_;
	}
}

uint32 IntSqrt(uint32 num);

#endif /* MATH_FUNC_HPP */
