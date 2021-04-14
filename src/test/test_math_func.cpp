/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */
#include <catch2/catch.hpp>

#include "../stdafx.h"

#include "../core/math_func.hpp"

TEST_CASE("LeastCommonMultipleTest - Zero")
{
	CHECK(0 == LeastCommonMultiple(0, 0));
	CHECK(0 == LeastCommonMultiple(0, 600));
	CHECK(0 == LeastCommonMultiple(600, 0));
}

TEST_CASE("LeastCommonMultipleTest - FindLCM")
{
	CHECK(25 == LeastCommonMultiple(5, 25));
	CHECK(25 == LeastCommonMultiple(25, 5));
	CHECK(130 == LeastCommonMultiple(5, 26));
	CHECK(130 == LeastCommonMultiple(26, 5));
}

TEST_CASE("GreatestCommonDivisorTest - Negative")
{
	CHECK(4 == GreatestCommonDivisor(4, -52));
	// CHECK(3 == GreatestCommonDivisor(-27, 6)); // error - returns -3
}

TEST_CASE("GreatestCommonDivisorTest - Zero")
{
	CHECK(27 == GreatestCommonDivisor(0, 27));
	CHECK(27 == GreatestCommonDivisor(27, 0));
}

TEST_CASE("GreatestCommonDivisorTest - FindGCD")
{
	CHECK(5 == GreatestCommonDivisor(5, 25));
	CHECK(5 == GreatestCommonDivisor(25, 5));
	CHECK(1 == GreatestCommonDivisor(7, 27));
	CHECK(1 == GreatestCommonDivisor(27, 7));
}

TEST_CASE("DivideApproxTest - Negative")
{
	CHECK(-2 == DivideApprox(-5, 2));
	CHECK(2 == DivideApprox(-5, -2));
	CHECK(-1 == DivideApprox(-66, 80));
}

TEST_CASE("DivideApproxTest, Divide")
{
	CHECK(2 == DivideApprox(5, 2));
	CHECK(3 == DivideApprox(80, 30));
	CHECK(3 == DivideApprox(8, 3));
	CHECK(0 == DivideApprox(3, 8));
}

TEST_CASE("IntSqrtTest - Zero")
{
	CHECK(0 == IntSqrt(0));
}

TEST_CASE("IntSqrtTest - FindSqRt")
{
	CHECK(5 == IntSqrt(25));
	CHECK(10 == IntSqrt(100));
	CHECK(9 == IntSqrt(88));
	CHECK(1696 == IntSqrt(2876278));
}
