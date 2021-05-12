/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file direction_type.h Different types to 'show' directions. */

#ifndef DIRECTION_TYPE_H
#define DIRECTION_TYPE_H

#include "core/enum_type.hpp"

/**
 * Defines the 8 directions on the map.
 *
 * This enum defines 8 possible directions which are used for
 * the vehicles in the game. The directions are aligned straight
 * to the viewport, not to the map. So north points to the top of
 * your viewport and not rotated by 45 degrees left or right to get
 * a "north" used in you games.
 */
enum Direction : byte {
	DIR_BEGIN = 0,          ///< Used to iterate
	DIR_N   = 0,            ///< North
	DIR_NE  = 1,            ///< Northeast
	DIR_E   = 2,            ///< East
	DIR_SE  = 3,            ///< Southeast
	DIR_S   = 4,            ///< South
	DIR_SW  = 5,            ///< Southwest
	DIR_W   = 6,            ///< West
	DIR_NW  = 7,            ///< Northwest
	DIR_END,                ///< Used to iterate
	INVALID_DIR = 0xFF,     ///< Flag for an invalid direction
};

/** Allow incrementing of Direction variables */
DECLARE_POSTFIX_INCREMENT(Direction)

/** Define basic enum properties */
template <> struct EnumPropsT<Direction> : MakeEnumPropsT<Direction, byte, DIR_BEGIN, DIR_END, INVALID_DIR, 3> {};


/**
 * Enumeration for the difference between two directions.
 *
 * This enumeration is used to mark differences between
 * two directions. If you get one direction you can align
 * a second direction in 8 different ways. This enumeration
 * only contains 6 of these 8 differences, but the remaining
 * two can be calculated by adding to differences together.
 * This also means you can add two differences together and
 * get the difference you really want to get. The difference
 * of 45 degrees left + the difference of 45 degrees right results in the
 * difference of 0 degrees.
 *
 * @note To get this mentioned addition of direction you must use
 *       modulo DIR_END or use the #ChangeDirDiff(DirDiff, DirDiff) function.
 * @see ChangeDirDiff(DirDiff, DirDiff)
 */
enum DirDiff {
	DIRDIFF_SAME    = 0,    ///< Both directions faces to the same direction
	DIRDIFF_45RIGHT = 1,    ///< Angle of 45 degrees right
	DIRDIFF_90RIGHT = 2,    ///< Angle of 90 degrees right
	DIRDIFF_REVERSE = 4,    ///< One direction is the opposite of the other one
	DIRDIFF_90LEFT  = 6,    ///< Angle of 90 degrees left
	DIRDIFF_45LEFT  = 7,    ///< Angle of 45 degrees left
};


/**
 * Enumeration for diagonal directions.
 *
 * This enumeration is used for the 4 direction of the tile-edges.
 */
enum DiagDirection : byte {
	DIAGDIR_BEGIN = 0,      ///< Used for iterations
	DIAGDIR_NE  = 0,        ///< Northeast, upper right on your monitor
	DIAGDIR_SE  = 1,        ///< Southeast
	DIAGDIR_SW  = 2,        ///< Southwest
	DIAGDIR_NW  = 3,        ///< Northwest
	DIAGDIR_END,            ///< Used for iterations
	INVALID_DIAGDIR = 0xFF, ///< Flag for an invalid DiagDirection
};

/** Allow incrementing of DiagDirection variables */
DECLARE_POSTFIX_INCREMENT(DiagDirection)

/** Define basic enum properties */
template <> struct EnumPropsT<DiagDirection> : MakeEnumPropsT<DiagDirection, byte, DIAGDIR_BEGIN, DIAGDIR_END, INVALID_DIAGDIR, 2> {};


/**
 * Enumeration for the difference between to DiagDirection.
 *
 * As the DiagDirection only contains 4 possible directions the
 * difference between two of these directions can only be in 4 ways.
 * As the DirDiff enumeration the values can be added together and
 * you will get the resulting difference (use modulo DIAGDIR_END).
 *
 * @see DirDiff
 */
enum DiagDirDiff {
	DIAGDIRDIFF_SAME    = 0,        ///< Same directions
	DIAGDIRDIFF_90RIGHT = 1,        ///< 90 degrees right
	DIAGDIRDIFF_REVERSE = 2,        ///< Reverse directions
	DIAGDIRDIFF_90LEFT  = 3,        ///< 90 degrees left
};

/** Allow incrementing of DiagDirDiff variables */
DECLARE_POSTFIX_INCREMENT(DiagDirDiff)


/**
 * Enumeration for the two axis X and Y
 *
 * This enumeration represents the two axis X and Y in the game.
 * The X axis is the one which goes align the north-west edge
 * (and south-east edge). The Y axis must be so the one which goes
 * align the north-east edge (and south-west) edge.
 */
enum Axis {
	AXIS_X = 0,          ///< The X axis
	AXIS_Y = 1,          ///< The y axis
	AXIS_END,            ///< Used for iterations and to represent both axis (in overlapping bridges)
	INVALID_AXIS = 0xFF, ///< Flag for an invalid Axis
};
/** Helper information for extract tool. */
template <> struct EnumPropsT<Axis> : MakeEnumPropsT<Axis, byte, AXIS_X, AXIS_END, INVALID_AXIS, 1> {};

#endif /* DIRECTION_TYPE_H */
