/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file platform_type.h Types related to platforms. */

#ifndef PLATFORM_TYPE_H
#define PLATFORM_TYPE_H

#include "core/enum_type.hpp"

enum PlatformType {
	PT_RAIL_STATION,
	PT_RAIL_WAYPOINT,
	PT_RAIL_DEPOT,
	PT_END,
	INVALID_PLATFORM_TYPE = PT_END,
};

#endif /* PLATFORM_TYPE_H */
