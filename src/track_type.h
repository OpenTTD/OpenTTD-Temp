/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file track_type.h All types related to tracks */

#ifndef TRACK_TYPE_H
#define TRACK_TYPE_H

#include "core/enum_type.hpp"

/**
 * These are used to specify a single track.
 * Can be translated to a trackbit with TrackToTrackbit.
 * TRACK_WORMHOLE and TRACK_DEPOT do not represent single tracks but states; they cannot be translated to trackbits.
 */
enum Track : byte {
	TRACK_BEGIN = 0,        ///< Used for iterations
	TRACK_X     = 0,        ///< Track along the x-axis (north-east to south-west)
	TRACK_Y     = 1,        ///< Track along the y-axis (north-west to south-east)
	TRACK_UPPER = 2,        ///< Track in the upper corner of the tile (north)
	TRACK_LOWER = 3,        ///< Track in the lower corner of the tile (south)
	TRACK_LEFT  = 4,        ///< Track in the left corner of the tile (west)
	TRACK_RIGHT = 5,        ///< Track in the right corner of the tile (east)
	TRACK_END   = 6,        ///< Used for iterations
	TRACK_WORMHOLE = TRACK_END,
	TRACK_DEPOT = 7,
	INVALID_TRACK = 0xFF,   ///< Flag for an invalid track
};

/** Allow incrementing of Track variables */
DECLARE_POSTFIX_INCREMENT(Track)
/** Define basic enum properties */
template <> struct EnumPropsT<Track> : MakeEnumPropsT<Track, byte, TRACK_BEGIN, TRACK_END, INVALID_TRACK, 3> {};


/** Bitfield corresponding to Track */
enum TrackBits : byte {
	TRACK_BIT_NONE    = 0U,                                                 ///< No track
	TRACK_BIT_X       = 1U << TRACK_X,                                      ///< X-axis track
	TRACK_BIT_Y       = 1U << TRACK_Y,                                      ///< Y-axis track
	TRACK_BIT_UPPER   = 1U << TRACK_UPPER,                                  ///< Upper track
	TRACK_BIT_LOWER   = 1U << TRACK_LOWER,                                  ///< Lower track
	TRACK_BIT_LEFT    = 1U << TRACK_LEFT,                                   ///< Left track
	TRACK_BIT_RIGHT   = 1U << TRACK_RIGHT,                                  ///< Right track
	TRACK_BIT_CROSS   = TRACK_BIT_X     | TRACK_BIT_Y,                      ///< X-Y-axis cross
	TRACK_BIT_HORZ    = TRACK_BIT_UPPER | TRACK_BIT_LOWER,                  ///< Upper and lower track
	TRACK_BIT_VERT    = TRACK_BIT_LEFT  | TRACK_BIT_RIGHT,                  ///< Left and right track
	TRACK_BIT_3WAY_NE = TRACK_BIT_X     | TRACK_BIT_UPPER | TRACK_BIT_RIGHT,///< "Arrow" to the north-east
	TRACK_BIT_3WAY_SE = TRACK_BIT_Y     | TRACK_BIT_LOWER | TRACK_BIT_RIGHT,///< "Arrow" to the south-east
	TRACK_BIT_3WAY_SW = TRACK_BIT_X     | TRACK_BIT_LOWER | TRACK_BIT_LEFT, ///< "Arrow" to the south-west
	TRACK_BIT_3WAY_NW = TRACK_BIT_Y     | TRACK_BIT_UPPER | TRACK_BIT_LEFT, ///< "Arrow" to the north-west
	TRACK_BIT_ALL     = TRACK_BIT_CROSS | TRACK_BIT_HORZ  | TRACK_BIT_VERT, ///< All possible tracks
	TRACK_BIT_MASK    = 0x3FU,                                              ///< Bitmask for the first 6 bits
	TRACK_BIT_WORMHOLE = 1U << TRACK_WORMHOLE,                              ///< Bitflag for a wormhole (used for tunnels)
	TRACK_BIT_DEPOT   = 1U << TRACK_DEPOT,                                  ///< Bitflag for a depot
	INVALID_TRACK_BIT = 0xFF,                                               ///< Flag for an invalid trackbits value
};
DECLARE_ENUM_AS_BIT_SET(TrackBits)

/**
 * Enumeration for tracks and directions.
 *
 * These are a combination of tracks and directions. Values are 0-5 in one
 * direction (corresponding to the Track enum) and 8-13 in the other direction.
 * 6, 7, 14 and 15 are used to encode the reversing of road vehicles. Those
 * reversing track dirs are not considered to be 'valid' except in a small
 * corner in the road vehicle controller.
 */
enum Trackdir : byte {
	TRACKDIR_BEGIN    =  0,         ///< Used for iterations
	TRACKDIR_X_NE     =  0,         ///< X-axis and direction to north-east
	TRACKDIR_Y_SE     =  1,         ///< Y-axis and direction to south-east
	TRACKDIR_UPPER_E  =  2,         ///< Upper track and direction to east
	TRACKDIR_LOWER_E  =  3,         ///< Lower track and direction to east
	TRACKDIR_LEFT_S   =  4,         ///< Left track and direction to south
	TRACKDIR_RIGHT_S  =  5,         ///< Right track and direction to south
	TRACKDIR_RVREV_NE =  6,         ///< (Road vehicle) reverse direction north-east
	TRACKDIR_RVREV_SE =  7,         ///< (Road vehicle) reverse direction south-east
	TRACKDIR_X_SW     =  8,         ///< X-axis and direction to south-west
	TRACKDIR_Y_NW     =  9,         ///< Y-axis and direction to north-west
	TRACKDIR_UPPER_W  = 10,         ///< Upper track and direction to west
	TRACKDIR_LOWER_W  = 11,         ///< Lower track and direction to west
	TRACKDIR_LEFT_N   = 12,         ///< Left track and direction to north
	TRACKDIR_RIGHT_N  = 13,         ///< Right track and direction to north
	TRACKDIR_RVREV_SW = 14,         ///< (Road vehicle) reverse direction south-west
	TRACKDIR_RVREV_NW = 15,         ///< (Road vehicle) reverse direction north-west
	TRACKDIR_END,                   ///< Used for iterations
	INVALID_TRACKDIR  = 0xFF,       ///< Flag for an invalid trackdir
};

/** Define basic enum properties */
template <> struct EnumPropsT<Trackdir> : MakeEnumPropsT<Trackdir, byte, TRACKDIR_BEGIN, TRACKDIR_END, INVALID_TRACKDIR, 4> {};

/**
 * Enumeration of bitmasks for the TrackDirs
 *
 * These are a combination of tracks and directions. Values are 0-5 in one
 * direction (corresponding to the Track enum) and 8-13 in the other direction.
 */
enum TrackdirBits : uint16 {
	TRACKDIR_BIT_NONE     = 0U,                     ///< No track build
	TRACKDIR_BIT_X_NE     = 1U << TRACKDIR_X_NE,    ///< Track x-axis, direction north-east
	TRACKDIR_BIT_Y_SE     = 1U << TRACKDIR_Y_SE,    ///< Track y-axis, direction south-east
	TRACKDIR_BIT_UPPER_E  = 1U << TRACKDIR_UPPER_E, ///< Track upper, direction east
	TRACKDIR_BIT_LOWER_E  = 1U << TRACKDIR_LOWER_E, ///< Track lower, direction east
	TRACKDIR_BIT_LEFT_S   = 1U << TRACKDIR_LEFT_S,  ///< Track left, direction south
	TRACKDIR_BIT_RIGHT_S  = 1U << TRACKDIR_RIGHT_S, ///< Track right, direction south
	/* Again, note the two missing values here. This enables trackdir -> track conversion by doing (trackdir & 0xFF) */
	TRACKDIR_BIT_X_SW     = 1U << TRACKDIR_X_SW,    ///< Track x-axis, direction south-west
	TRACKDIR_BIT_Y_NW     = 1U << TRACKDIR_Y_NW,    ///< Track y-axis, direction north-west
	TRACKDIR_BIT_UPPER_W  = 1U << TRACKDIR_UPPER_W, ///< Track upper, direction west
	TRACKDIR_BIT_LOWER_W  = 1U << TRACKDIR_LOWER_W, ///< Track lower, direction west
	TRACKDIR_BIT_LEFT_N   = 1U << TRACKDIR_LEFT_N,  ///< Track left, direction north
	TRACKDIR_BIT_RIGHT_N  = 1U << TRACKDIR_RIGHT_N, ///< Track right, direction north
	TRACKDIR_BIT_MASK     = 0x3F3F,                 ///< Bitmask for bit-operations
	INVALID_TRACKDIR_BIT  = 0xFFFF,                 ///< Flag for an invalid trackdirbit value
};
DECLARE_ENUM_AS_BIT_SET(TrackdirBits)

typedef uint32 TrackStatus;

#endif /* TRACK_TYPE_H */
