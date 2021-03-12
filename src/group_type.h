/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file group_type.h Types of a group. */

#ifndef GROUP_TYPE_H
#define GROUP_TYPE_H

typedef uint16 GroupID; ///< Type for all group identifiers.

static const GroupID NEW_GROUP     = 0xFFFC; ///< Sentinel for a to-be-created group.
static const GroupID ALL_GROUP     = 0xFFFD; ///< All vehicles are in this group.
static const GroupID DEFAULT_GROUP = 0xFFFE; ///< Ungrouped vehicles are in this group.
static const GroupID INVALID_GROUP = 0xFFFF; ///< Sentinel for invalid groups.

static const uint MAX_LENGTH_GROUP_NAME_CHARS = 128; ///< The maximum length of a group name in characters including '\0'

struct Group;

#endif /* GROUP_TYPE_H */
