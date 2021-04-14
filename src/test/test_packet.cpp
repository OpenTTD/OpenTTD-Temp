/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */
#include <catch2/catch.hpp>

#include "../stdafx.h"

#include "../network/core/packet.h"

TEST_CASE("Packet")
{
	auto packet = Packet(42);

	CHECK(packet.pos == 0);
	CHECK(packet.size == 3);

	auto* newData = packet.buffer + 1;
	CHECK(*++newData == 42);

	SECTION("Send_bool")
	{
		packet.Send_bool(true);
		packet.Send_bool(false);

		CHECK(packet.size == 5);
		CHECK(*++newData == 1);
		CHECK(*++newData == 0);
	}

	SECTION("Send_uint8")
	{
		packet.Send_uint8(0xCD);
		packet.Send_uint8(0xEF);

		CHECK(packet.size == 5);
		CHECK(*++newData == 0xCD);
		CHECK(*++newData == 0xEF);
	}

	SECTION("Send_uint16")
	{
		packet.Send_uint16(0x89AB);
		packet.Send_uint16(0xCDEF);

		CHECK(packet.size == 7);
		CHECK(*++newData == 0xAB);
		CHECK(*++newData == 0x89);
		CHECK(*++newData == 0xEF);
		CHECK(*++newData == 0xCD);
	}

	SECTION("Send_uint32")
	{
		packet.Send_uint32(0x89ABCDEF);

		CHECK(packet.size == 7);
		CHECK(*++newData == 0xEF);
		CHECK(*++newData == 0xCD);
		CHECK(*++newData == 0xAB);
		CHECK(*++newData == 0x89);
	}

	SECTION("Send_uint64")
	{
		packet.Send_uint64(0x0123456789ABCDEF);

		CHECK(packet.size == 11);
		CHECK(*++newData == 0xEF);
		CHECK(*++newData == 0xCD);
		CHECK(*++newData == 0xAB);
		CHECK(*++newData == 0x89);
		CHECK(*++newData == 0x67);
		CHECK(*++newData == 0x45);
		CHECK(*++newData == 0x23);
		CHECK(*++newData == 0x01);
	}

	SECTION("Send_string - empty")
	{
		packet.Send_string("");

		CHECK(packet.size == 4);
		CHECK(*++newData == '\0');
	}

	SECTION("Send_string")
	{
		packet.Send_string("openttd");

		CHECK(packet.size == 11);
		CHECK(*++newData == 0x6F);
		CHECK(*++newData == 0x70);
		CHECK(*++newData == 0x65);
		CHECK(*++newData == 0x6E);
		CHECK(*++newData == 0x74);
		CHECK(*++newData == 0x74);
		CHECK(*++newData == 0x64);
		CHECK(*++newData == '\0');
	}

	SECTION("Send_string - null character")
	{
		packet.Send_string("open\0ttd");

		CHECK(packet.size == 8);
		CHECK(*++newData == 0x6F);
		CHECK(*++newData == 0x70);
		CHECK(*++newData == 0x65);
		CHECK(*++newData == 0x6E);
		CHECK(*++newData == '\0');
	}

	SECTION("Send_string - emoji")
	{
		packet.Send_string("🚂🚌🚆🚗");

		CHECK(packet.size == 20);
		CHECK(*++newData == 0xF0); // locomotive
		CHECK(*++newData == 0x9F);
		CHECK(*++newData == 0x9A);
		CHECK(*++newData == 0x82);
		CHECK(*++newData == 0xF0); // bus
		CHECK(*++newData == 0x9F);
		CHECK(*++newData == 0x9A);
		CHECK(*++newData == 0x8C);
		CHECK(*++newData == 0xF0); // train
		CHECK(*++newData == 0x9F);
		CHECK(*++newData == 0x9A);
		CHECK(*++newData == 0x86);
		CHECK(*++newData == 0xF0); // automobile
		CHECK(*++newData == 0x9F);
		CHECK(*++newData == 0x9A);
		CHECK(*++newData == 0x97);
		CHECK(*++newData == '\0');
	}
}
