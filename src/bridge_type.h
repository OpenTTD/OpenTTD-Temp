/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

 /** @file bridge_type.h Types related to bridges */

#ifndef BRIDGE_TYPE_H
#define BRIDGE_TYPE_H

/**
 * This enum is related to the definition of bridge pieces,
 * which is used to determine the proper sprite table to use
 * while drawing a given bridge part.
 */
enum BridgePieces {
	BRIDGE_PIECE_NORTH = 0,
	BRIDGE_PIECE_SOUTH,
	BRIDGE_PIECE_INNER_NORTH,
	BRIDGE_PIECE_INNER_SOUTH,
	BRIDGE_PIECE_MIDDLE_ODD,
	BRIDGE_PIECE_MIDDLE_EVEN,
	BRIDGE_PIECE_HEAD,
	BRIDGE_PIECE_INVALID,
};

DECLARE_POSTFIX_INCREMENT(BridgePieces)

/** Sprite groups for a bridge. */
enum BridgeSpriteGroup {
	BSG_GUI,          ///< GUI icon images
	BSG_FRONT,        ///< Sprites in front of vehicle
	BSG_BACK,         ///< Sprites below/behind vehicle
	BSG_PILLARS,      ///< Pillars
	BSG_RAMPS,        ///< Ramps
	BSG_END,
};

typedef uint32 BridgeID;
typedef uint BridgeType; ///< Bridge spec number.

static const BridgeID NUM_BRIDGES_PER_GRF = 255;    ///< Number of supported bridges per NewGRF; limited to 255 to allow extending Action3 with an extended byte later on.

static const BridgeID NEW_BRIDGE_OFFSET = 13;       ///< Offset for new bridges.
static const BridgeID NUM_BRIDGES = 64000;          ///< Number of supported bridges
static const BridgeID INVALID_BRIDGE_TYPE = 0xFFFF; ///< An invalid bridge

struct Bridge;
struct BridgeSpec;

#endif /* BRIDGE_TYPE_H */
