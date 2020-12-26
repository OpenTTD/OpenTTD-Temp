/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file overflowsafe_type.hpp An overflow safe integer-like type. */

#ifndef OVERFLOWSAFE_TYPE_HPP
#define OVERFLOWSAFE_TYPE_HPP

#include "math_func.hpp"
#include "../stdafx.h"
#include <limits>

/**
 * Overflow safe template for integers, i.e. integers that will never overflow if
 * you multiply the maximum value with 2, or add 2, or subtract something from
 * the minimum value, etc.
 *
 * @note The result of arithmetic between two overflow-safe integers has the same
 *       type as the widest of the two operands. Arithmetic with a non-overflow-safe
 *       operand promotes the operand to the same type as the overflow-safe operand.
 *
 * @tparam T     the type these integers are stored with.
 * @tparam U     the type used for comparing absolute values, defaults to \c std::make_unsigned<T>.
 */
template <class T, class U = typename std::make_unsigned<T>::type>
class OverflowSafeInt
{
private:
	/** Shorthand aliases for the numeric limits of \c T. */
	static constexpr T T_MIN = std::numeric_limits<T>::min();
	static constexpr T T_MAX = std::numeric_limits<T>::max();

	/** Verify that the datatypes meet the requirements (U must hold all absolute values of T). */
	template <uintmax_t U_MAX = static_cast<uintmax_t>(std::numeric_limits<U>::max()),
			uintmax_t T_DELTA = static_cast<uintmax_t>(((T_MIN + T_MAX) < (T)0) ? -(T_MIN + T_MAX) : (T)0),
			uintmax_t ABS_TMAX = static_cast<uintmax_t>(T_MAX),
			uintmax_t ABS_TMIN = static_cast<uintmax_t>(T_MAX) + T_DELTA,
			U U_TMIN = static_cast<U>(ABS_TMIN),
			bool CHECK_T_IS_SIGNED  = std::is_integral<T>::value && std::is_signed<T>::value,
			bool CHECK_U_IS_INTEGER = std::is_integral<U>::value,
			bool CHECK_U_HOLDS_TMAX = (U_MAX >= ABS_TMAX),
			bool CHECK_U_HOLDS_TMIN = (U_MAX >= ABS_TMIN),
			bool CHECK_U_UNDERFLOW  = ((static_cast<uintmax_t>(U_TMIN) == ABS_TMIN) && (U_TMIN > (U)0))>
	class CheckTypeConstraints {
		static typename std::enable_if<CHECK_T_IS_SIGNED>::type  check_t_is_signed() {}
		static typename std::enable_if<CHECK_U_IS_INTEGER>::type check_u_is_integer() {}
		static typename std::enable_if<CHECK_U_HOLDS_TMAX>::type check_u_holds_tmax() {}
		static typename std::enable_if<CHECK_U_HOLDS_TMIN>::type check_u_holds_tmin() {}
		static typename std::enable_if<CHECK_U_UNDERFLOW>::type  check_u_underflow() {}
	public:
		typedef T type;
	};

	/** The non-overflow safe backend to store the value in. */
	typename CheckTypeConstraints<>::type m_value;

	/**
	 * Overflow-safe version of the absolute value function.
	 */
	static inline constexpr U abs(const T value)
	{
		return
				/* Test for positive value */
				(value >= 0) ? static_cast<U>(value) :

				/* Test for "normal" negative value */
				(T_MAX + value >= 0) ? static_cast<U>(-value) :

				/* Handle "abnormal" negative value */
				static_cast<U>(T_MAX) + static_cast<U>(-(T_MAX + value));
	}

	/**
	 * Return the saturation limit in the given direction.
	 */
	static constexpr inline T Limit(const T predicate)
	{
		return (predicate > 0) ? T_MAX : T_MIN;
	}

	/**
	 * Return the saturation limit in the opposite direction.
	 */
	static constexpr inline T InvLimit(const T predicate)
	{
		return (predicate < 0) ? T_MAX : T_MIN;
	}

	/**
	 * Safe implementation of division by positive divisor.
	 * @param divisor the divisor to divide this by.
	 * @note when the division would yield more than T_MAX (or less than T_MIN),
	 *       it will be T_MAX (respectively T_MIN).
	 */
	inline OverflowSafeInt& SafeDiv (const U divisor)
	{
		if (likely(divisor <= static_cast<U>(T_MAX))) {
			/* Expected case; divisor is representable by T */
			this->m_value /= static_cast<T>(divisor);
		} else {
			/* Divisor is only representable by U */
			this->m_value = signum(this->m_value) * static_cast<T>(abs(this->m_value) / divisor);
		}
		return *this;
	}

	/* Comparison functions */
	static inline constexpr int cmp(const OverflowSafeInt& a, const OverflowSafeInt& b)
	{
		return signum(static_cast<int>(a - b));
	}

	template <class A, class = typename std::enable_if<!std::is_base_of<OverflowSafeInt, A>::value && !std::is_integral<A>::value>::type>
	static inline constexpr int cmp(const OverflowSafeInt& a, const A&               b)
	{
		return signum(static_cast<int>(OverflowSafeInt<intmax_t>(a) - OverflowSafeInt<intmax_t>(b)));
	}

	template <class I = intmax_t, class = typename std::enable_if<std::is_integral<I>::value>::type>
	static inline constexpr int cmp(const OverflowSafeInt& a, const I                b)
	{
		return
				/* Test upper limit */
				unlikely(static_cast<uintmax_t>(std::numeric_limits<I>::max()) > static_cast<uintmax_t>(T_MAX) && b >= (I)0 && static_cast<uintmax_t>(b) > static_cast<uintmax_t>(T_MAX)) ? 1 :

				/* Test lower limit */
				unlikely(static_cast<intmax_t> (std::numeric_limits<I>::min()) < static_cast<intmax_t> (T_MIN) &&              static_cast<intmax_t> (b) < static_cast<intmax_t> (T_MIN)) ? -1 :

				/* Compare values */
				cmp(a, OverflowSafeInt(b));
	}

	template <class A = intmax_t, class = typename std::enable_if<!std::is_base_of<OverflowSafeInt, A>::value>::type>
	static inline constexpr int cmp(const A&               a, const OverflowSafeInt& b) { return -cmp(b, a); }

public:
	OverflowSafeInt() : m_value(0) { }

	template <class A, class B>
	OverflowSafeInt(const OverflowSafeInt<A, B>& other) : OverflowSafeInt(static_cast<A>(other)) { }

	template <class I = intmax_t, class = typename std::enable_if<std::is_integral<I>::value>::type>
	OverflowSafeInt(const I int_) : m_value(ClampTo<T>(int_)) { }

	inline OverflowSafeInt& operator = (const OverflowSafeInt& other) { this->m_value = other.m_value; return *this; }

	inline OverflowSafeInt operator - () const
	{
		if (unlikely(abs(this->m_value) > abs(InvLimit(this->m_value)))) {
			return OverflowSafeInt(InvLimit(this->m_value));
		} else {
			return OverflowSafeInt(-this->m_value);
		}
	}

	/* Absolute value */
	friend inline const U abs(const OverflowSafeInt& value) { return abs(value.m_value); }

	/**
	 * Safe implementation of addition.
	 * @param other the amount to add
	 * @note when the addition would yield more than T_MAX (or less than T_MIN),
	 *       it will be T_MAX (respectively T_MIN).
	 */
	inline OverflowSafeInt& operator += (const OverflowSafeInt& other)
	{
		if ((signum(this->m_value) == signum(other.m_value)) &&
				unlikely((abs(Limit(this->m_value)) - abs(other.m_value)) < abs(this->m_value))) {
			this->m_value = Limit(this->m_value);
		} else {
			this->m_value += other.m_value;
		}
		return *this;
	}

	/* Operators for addition of overflow-safe integers */
	template <class A = T, class B = U, class = typename std::enable_if<std::is_integral<A>::value && (static_cast<uintmax_t>(std::numeric_limits<A>::max()) <= static_cast<uintmax_t>(T_MAX))>::type>
	friend inline OverflowSafeInt       operator + (      OverflowSafeInt  a, const OverflowSafeInt<A, B>& b) { return a += b; }
	template <class A = intmax_t, class B = uintmax_t, class = typename std::enable_if<std::is_integral<A>::value && (static_cast<uintmax_t>(std::numeric_limits<A>::max()) > static_cast<uintmax_t>(T_MAX))>::type>
	friend inline OverflowSafeInt<A, B> operator + (const OverflowSafeInt& a,       OverflowSafeInt<A, B>  b) { return b += a; }

	/* Operators for addition of non-overflow-safe integers */
	template <class I = intmax_t, class = typename std::enable_if<std::is_integral<I>::value>::type>
	friend inline OverflowSafeInt operator + (      OverflowSafeInt  a, const I  b) { return a += b; }
	template <class I = intmax_t, class = typename std::enable_if<std::is_integral<I>::value>::type>
	friend inline OverflowSafeInt operator + (const I  a,       OverflowSafeInt  b) { return b += a; }

	/**
	 * Safe implementation of subtraction.
	 * @param other the amount to subtract
	 * @note when the subtraction would yield more than T_MAX (or less than T_MIN),
	 *       it will be T_MAX (respectively T_MIN).
	 */
	inline OverflowSafeInt& operator -= (const OverflowSafeInt& other) { return *this += (-other); }

	/* Operators for subtraction of overflow-safe integers */
	template <class A = T, class B = U, class = typename std::enable_if<std::is_integral<A>::value && (static_cast<uintmax_t>(std::numeric_limits<A>::max()) <= static_cast<uintmax_t>(T_MAX))>::type>
	friend inline OverflowSafeInt       operator - (      OverflowSafeInt  a, const OverflowSafeInt<A, B>& b) { return a -= b; }
	template <class A = intmax_t, class B = uintmax_t, class = typename std::enable_if<std::is_integral<A>::value && (static_cast<uintmax_t>(std::numeric_limits<A>::max()) > static_cast<uintmax_t>(T_MAX))>::type>
	friend inline OverflowSafeInt<A, B> operator - (const OverflowSafeInt& a, const OverflowSafeInt<A, B>& b) { return -b + a; }

	/* Operators for subtraction of non-overflow-safe integers */
	template <class I = intmax_t, class = typename std::enable_if<std::is_integral<I>::value>::type>
	friend inline OverflowSafeInt operator - (      OverflowSafeInt  a, const I  b) { return a -= b; }
	template <class I = intmax_t, class = typename std::enable_if<std::is_integral<I>::value>::type>
	friend inline OverflowSafeInt operator - (const I  a, const OverflowSafeInt& b) { return -b + a; }

	/* Operators for increment and decrement */
	inline OverflowSafeInt& operator ++ () { this->m_value += unlikely(this->m_value >= T_MAX) ? 0 : 1; return *this; }
	inline OverflowSafeInt& operator -- () { this->m_value -= unlikely(this->m_value <= T_MIN) ? 0 : 1; return *this; }
	inline OverflowSafeInt operator ++ (int) { auto res = *this; ++(*this); return res; }
	inline OverflowSafeInt operator -- (int) { auto res = *this; --(*this); return res; }

	/**
	 * Safe implementation of multiplication.
	 * @param factor the factor to multiply this with.
	 * @note when the multiplication would yield more than T_MAX (or less than T_MIN),
	 *       it will be T_MAX (respectively T_MIN).
	 */
	inline OverflowSafeInt& operator *= (const OverflowSafeInt& factor)
	{
		const T limit = Limit(signum(this->m_value) * signum(factor.m_value));
		if (likely(factor.m_value != 0) && unlikely((abs(limit) / abs(factor.m_value)) < abs(this->m_value))) {
			 this->m_value = limit;
		} else {
			this->m_value *= factor.m_value;
		}
		return *this;
	}

	/* Operators for multiplication of overflow-safe integers */
	template <class A = T, class B = U, class = typename std::enable_if<std::is_integral<A>::value && (static_cast<uintmax_t>(std::numeric_limits<A>::max()) <= static_cast<uintmax_t>(T_MAX))>::type>
	friend inline OverflowSafeInt       operator * (      OverflowSafeInt  a, const OverflowSafeInt<A, B>& b) { return a *= b; }
	template <class A = intmax_t, class B = uintmax_t, class = typename std::enable_if<std::is_integral<A>::value && (static_cast<uintmax_t>(std::numeric_limits<A>::max()) > static_cast<uintmax_t>(T_MAX))>::type>
	friend inline OverflowSafeInt<A, B> operator * (const OverflowSafeInt& a,       OverflowSafeInt<A, B>  b) { return b *= a; }

	/* Operators for multiplication of non-overflow-safe integers */
	template <class I = intmax_t, class = typename std::enable_if<std::is_integral<I>::value>::type>
	friend inline OverflowSafeInt operator * (      OverflowSafeInt  a, const I  b) { return a *= b; }
	template <class I = intmax_t, class = typename std::enable_if<std::is_integral<I>::value>::type>
	friend inline OverflowSafeInt operator * (const I  a,       OverflowSafeInt  b) { return b *= a; }

	/**
	 * Safe implementation of division.
	 * @param divisor the divisor to divide this by.
	 * @note when the division would yield more than T_MAX (or less than T_MIN),
	 *       it will be T_MAX (respectively T_MIN).
	 */
	inline OverflowSafeInt& operator /= (const OverflowSafeInt& divisor)
	{
		/* Special case for negative divisor, avoids the possibility of overflow when negating T_MIN or T_MAX */
		if (unlikely(divisor.m_value < 0)) *this = -(*this);

		this->SafeDiv(abs(divisor.m_value));
		return *this;
	}

	/* Operators for division of overflow-safe integers */
	template <class A = T, class B = U, class = typename std::enable_if<std::is_integral<A>::value && (static_cast<uintmax_t>(std::numeric_limits<A>::max()) <= static_cast<uintmax_t>(T_MAX))>::type>
	friend inline OverflowSafeInt       operator / (      OverflowSafeInt  a, const OverflowSafeInt<A, B>& b) { return a /= b; }
	template <class A = intmax_t, class B = uintmax_t, class = typename std::enable_if<std::is_integral<A>::value && (static_cast<uintmax_t>(std::numeric_limits<A>::max()) > static_cast<uintmax_t>(T_MAX))>::type>
	friend inline OverflowSafeInt<A, B> operator / (const OverflowSafeInt& a, const OverflowSafeInt<A, B>& b) { return OverflowSafeInt<A, B>(a) / b; }

	/* Operators for division of non-overflow-safe integers */
	template <class I = intmax_t, class = typename std::enable_if<std::is_integral<I>::value>::type>
	friend inline OverflowSafeInt operator / (      OverflowSafeInt  a, const I  b) { return a /= b; }
	template <class I = intmax_t, class = typename std::enable_if<std::is_integral<I>::value>::type>
	friend inline OverflowSafeInt operator / (const I  a, const OverflowSafeInt& b) { return OverflowSafeInt(a) / b; }

	/**
	 * Safe implementation of modulo.
	 * @param divisor the divisor to divide this by.
	 * @note when the modulo would yield more than T_MAX (or less than T_MIN),
	 *       it will be T_MAX (respectively T_MIN).
	 */
	inline OverflowSafeInt& operator %= (const OverflowSafeInt& divisor)
	{
		return *this -= ((*this / divisor) * divisor);
	}

	/* Operators for modulo of overflow-safe integers */
	template <class A = T, class B = U, class = typename std::enable_if<std::is_integral<A>::value && (static_cast<uintmax_t>(std::numeric_limits<A>::max()) <= static_cast<uintmax_t>(T_MAX))>::type>
	friend inline OverflowSafeInt       operator % (      OverflowSafeInt  a, const OverflowSafeInt<A, B>& b) { return a %= b; }
	template <class A = intmax_t, class B = uintmax_t, class = typename std::enable_if<std::is_integral<A>::value && (static_cast<uintmax_t>(std::numeric_limits<A>::max()) > static_cast<uintmax_t>(T_MAX))>::type>
	friend inline OverflowSafeInt<A, B> operator % (const OverflowSafeInt& a, const OverflowSafeInt<A, B>& b) { return OverflowSafeInt<A, B>(a) % b; }

	/* Operators for modulo of non-overflow-safe integers */
	template <class I = intmax_t, class = typename std::enable_if<std::is_integral<I>::value>::type>
	friend inline OverflowSafeInt operator % (      OverflowSafeInt  a, const I  b) { return a %= b; }
	template <class I = intmax_t, class = typename std::enable_if<std::is_integral<I>::value>::type>
	friend inline OverflowSafeInt operator % (const I  a, const OverflowSafeInt& b) { return OverflowSafeInt(a) % b; }

	/* Safe implementation of left shift.
	 * @param shift the number of bits to shift by.
	 * @note when the bitshift would yield more than T_MAX (or less than T_MIN),
	 *       it will be T_MAX (respectively T_MIN).
	 */

	inline OverflowSafeInt& operator <<= (const int shift)
	{
		assert(shift >= 0);
		const T limit = Limit(this->m_value);
		if (unlikely((abs(limit) >> shift) < abs(this->m_value))) {
			 this->m_value = limit;
		} else {
			this->m_value <<= shift;
		}
		return *this;
	}

	/* Operators for shifting */
	inline OverflowSafeInt  operator <<  (const int shift) const { auto res = *this; res <<= shift; return res; }
	inline OverflowSafeInt& operator >>= (const int shift)       { assert(shift >= 0); this->m_value >>= shift; return *this; }
	inline OverflowSafeInt  operator >>  (const int shift) const { auto res = *this; res >>= shift; return res; }

	/* Comparison operators */
	template <class A = OverflowSafeInt> inline bool operator == (const A& other) const { return cmp(*this, other) == 0; }
	template <class A = OverflowSafeInt> inline bool operator != (const A& other) const { return cmp(*this, other) != 0; }
	template <class A = OverflowSafeInt> inline bool operator >  (const A& other) const { return cmp(*this, other) >  0; }
	template <class A = OverflowSafeInt> inline bool operator >= (const A& other) const { return cmp(*this, other) >= 0; }
	template <class A = OverflowSafeInt> inline bool operator <  (const A& other) const { return cmp(*this, other) <  0; }
	template <class A = OverflowSafeInt> inline bool operator <= (const A& other) const { return cmp(*this, other) <= 0; }

	template <class A = intmax_t, class = typename std::enable_if<!std::is_base_of<OverflowSafeInt, A>::value>::type>
	friend inline bool operator == (const A& a, const OverflowSafeInt& b) { return cmp(a, b) == 0; }
	template <class A = intmax_t, class = typename std::enable_if<!std::is_base_of<OverflowSafeInt, A>::value>::type>
	friend inline bool operator != (const A& a, const OverflowSafeInt& b) { return cmp(a, b) != 0; }
	template <class A = intmax_t, class = typename std::enable_if<!std::is_base_of<OverflowSafeInt, A>::value>::type>
	friend inline bool operator >  (const A& a, const OverflowSafeInt& b) { return cmp(a, b) >  0; }
	template <class A = intmax_t, class = typename std::enable_if<!std::is_base_of<OverflowSafeInt, A>::value>::type>
	friend inline bool operator >= (const A& a, const OverflowSafeInt& b) { return cmp(a, b) >= 0; }
	template <class A = intmax_t, class = typename std::enable_if<!std::is_base_of<OverflowSafeInt, A>::value>::type>
	friend inline bool operator <  (const A& a, const OverflowSafeInt& b) { return cmp(a, b) <  0; }
	template <class A = intmax_t, class = typename std::enable_if<!std::is_base_of<OverflowSafeInt, A>::value>::type>
	friend inline bool operator <= (const A& a, const OverflowSafeInt& b) { return cmp(a, b) <= 0; }

	/* Typecasting operators. Explicit typecasting is used in order to prevent subtle bugs caused by
	 * overflow-safe integrals being implicitly and unexpectedly typecast to overflow-unsafe integrals
	 * and then used in calculations.
	 */
	inline T Value() const              { return this->m_value; }
	template <class I = intmax_t, class = typename std::enable_if<std::is_integral<I>::value>::type>
	inline explicit operator I () const { return ClampTo<I>(this->m_value); }
};

/* Declare convenience typedefs */
typedef OverflowSafeInt<int64, uint64> OverflowSafeInt64;
typedef OverflowSafeInt<int32, uint32> OverflowSafeInt32;

/* Unit tests */
//static auto should_be_okay1 = -OverflowSafeInt<int32, int64>(1);
//static auto should_not_compile1 = -OverflowSafeInt<int64, uint32>(1);
//static auto should_not_compile2 = -OverflowSafeInt<int32, int32>(1);
//static auto should_not_compile3 = -OverflowSafeInt<uint>(1);
//static auto should_not_compile4 = -OverflowSafeInt<float, double>(1);

#endif /* OVERFLOWSAFE_TYPE_HPP */
