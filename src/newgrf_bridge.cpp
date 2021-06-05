/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_bridge.cpp Handling of bridge NewGRFs. */

#include "stdafx.h"
#include "debug.h"
#include "newgrf_bridge.h"
#include "bridge.h"
#include "bridge_map.h"
#include "tunnelbridge_map.h"
#include "tile_cmd.h"
#include "town.h"
#include "water.h"
#include "newgrf_animation_base.h"
#include "newgrf_railtype.h"
#include "newgrf_roadtype.h"

#include "safeguards.h"

/** The override manager for our bridges. */
BridgeOverrideManager _bridge_mngr(NEW_BRIDGE_OFFSET, NUM_BRIDGES, INVALID_BRIDGE_TYPE);

BridgeSpec _bridge_specs[NUM_BRIDGES]; ///< The specification of all bridges.

/**
 * Make an analysis of a tile and get the bridge type.
 * @param bridge the bridge been queried for
 * @param tile TileIndex of the tile to query
 * @param cur_grfid GRFID of the current callback chain
 * @return value encoded as per NFO specs
 */
static uint32 GetBridgeIDAtOffset(const Bridge *b,  TileIndex tile, uint32 cur_grfid)
{

	const Bridge *b2;

	if (IsBridgeTile(tile)) {
		b2 = Bridge::Get(GetBridgeIndex(tile));;
	} else if (IsBridgeAbove(tile)) {
		b2 = GetBridgeFromMiddle(tile);
	} else {
		return 0xFFFF;
	}

	if (GetBridgeHeight(b->heads[0]) != GetBridgeHeight(b2->heads[0])) {
		return 0xFFFF;
	}

	if (b->GetAxis() != b2->GetAxis()) {
		return 0xFFFF;
	}

	const BridgeSpec *spec = BridgeSpec::Get(b2->type);

	/* Default objects have no associated NewGRF file */
	if (spec->grf_prop.grffile == nullptr) {
		return 0xFFFE; // Defined in another grf file
	}

	if (spec->grf_prop.grffile->grfid == cur_grfid) { // same bridge, same grf ?
		return spec->grf_prop.local_id;
	}

	return 0xFFFE; // Defined in another grf file
}

/* virtual */ uint32 BridgeScopeResolver::GetRandomBits() const
{
	return bridge->random;
}

/** Used by the resolver to get values for feature 06 deterministic spritegroups. */
/* virtual */ uint32 BridgeScopeResolver::GetVariable(byte variable, uint32 parameter, bool *available) const
{
	/* We get the town from the bridge, or we calculate the closest
	 * town if we need to when there's no bridge. */
	const Town *t = nullptr;

	if (this->bridge == nullptr) {
		switch (variable) {
			/* Allow these when there's no bridge. */
			case 0x41:
				break;

			/* Construction date */
			case 0x40: return _date;

			/* Position along bridge */
			case 0x42: return 0;

			/* Length of bridge */
			case 0x43: return 0;

			/* Transport Type */
			case 0x44: return 0;

		default:
			goto unhandled;
		}

		/* If there's an invalid tile, then we don't have enough information at all. */
		if (!IsValidTile(this->tile)) goto unhandled;
	}
	else {
		t = this->bridge->town;
	}

	switch (variable) {
		/* Construction date */
		case 0x40: return this->bridge->build_date;

		/* Tile information. */
		case 0x41: return GetTerrainType(this->tile);

		/* Position along bridge */
		case 0x42: {
			uint length = this->bridge->GetLength();
			uint16 offset, offset_reverse;
			TileIndexDiffC diff = TileIndexToTileIndexDiffC(this->tile, this->bridge->heads[0]);

			offset = diff.x != 0 ? diff.x : diff.y;
			offset_reverse = length - offset - 1;

			return offset_reverse << 16 | offset;
		}

		/* Length of bridge */
		case 0x43: return this->bridge->GetLength();

		/* Transport type */
		case 0x44: {
			TransportType transport_type = GetTunnelBridgeTransportType(this->bridge->heads[0]);

			if (transport_type == TRANSPORT_RAIL) {
				return (GetReverseRailTypeTranslation(GetRailType(this->bridge->heads[0]), this->spec->grf_prop.grffile) << 8);
			} else if (transport_type == TRANSPORT_ROAD) {
				uint32 result = 0;
				if (HasRoadTypeRoad(this->bridge->heads[0])) {
					SetBit(result, 0);
					result |= (GetReverseRoadTypeTranslation(GetRoadTypeRoad(this->bridge->heads[0]), this->spec->grf_prop.grffile) << 8);
				}
				if (HasRoadTypeTram(this->bridge->heads[0])) {
					SetBit(result, 1);
					result |= (GetReverseRoadTypeTranslation(GetRoadTypeTram(this->bridge->heads[0]), this->spec->grf_prop.grffile) << 16);
				}

				return result;
			} else {
				NOT_REACHED();
			}
		}

		/* Get bridge ID at offset param */
		case 0x60: return GetBridgeIDAtOffset(this->bridge, GetNearbyTile(parameter, this->tile), this->ro.grffile->grfid);
	}

unhandled:
	DEBUG(grf, 1, "Unhandled bridge variable 0x%X", variable);

	*available = false;
	return UINT_MAX;
}

/**
 * Resolve sprites for drawing a station tile.
 * @param spec Bridge spec
 * @param bridge Bridge (nullptr in GUI)
 * @param tile Bridge tile being drawn (INVALID_TILE in GUI)
 * @param bsg The type of sprite to draw.
 * @param[out] num_results If not nullptr, return the number of sprites in the spriteset.
 * @return First sprite of the Action 1 spriteset to use
 */
SpriteID GetCustomBridgeSprites(const BridgeSpec *spec, Bridge *bridge, TileIndex tile, BridgeSpriteGroup bsg, uint *num_results)
{
	assert(bsg < BSG_END);

	if (spec->grf_prop.spritegroup[bsg] == nullptr) return 0;

	BridgeResolverObject object(spec, bridge, tile, bsg);
	const SpriteGroup *group = object.Resolve();
	if (group == nullptr || group->GetNumResults() == 0) return 0;

	if (num_results) *num_results = group->GetNumResults();

	return group->GetResult();
}

/**
 * Constructor of the bridge resolver.
 * @param spec Bridge spec
 * @param bridge Bridge being resolved.
 * @param tile %Tile of the bridge.
 * @param callback Callback ID.
 * @param param1 First parameter (var 10) of the callback.
 * @param param2 Second parameter (var 18) of the callback.
 */
BridgeResolverObject::BridgeResolverObject(const BridgeSpec *spec, Bridge *bridge, TileIndex tile, BridgeSpriteGroup bsg,
	CallbackID callback, uint32 param1, uint32 param2)
	: ResolverObject(spec->grf_prop.grffile, callback, param1, param2), bridge_scope(*this, bridge, spec, tile)
{
	this->town_scope = nullptr;
	this->root_spritegroup = spec != nullptr ? spec->grf_prop.spritegroup[bsg] : nullptr;
}

BridgeResolverObject::~BridgeResolverObject()
{
	delete this->town_scope;
}

/* virtual */ const SpriteGroup *BridgeResolverObject::ResolveReal(const RealSpriteGroup *group) const
{
	if (!group->loading.empty()) return group->loading[0];
	if (!group->loaded.empty()) return group->loaded[0];
	return nullptr;
}

/**
 * Get the town resolver scope that belongs to this bridge resolver.
 * On the first call, the town scope is created (if possible).
 * @return Town scope, if available.
 */
TownScopeResolver *BridgeResolverObject::GetTown()
{
	if (this->town_scope == nullptr) {
		Town *t;
		if (this->bridge_scope.bridge != nullptr) {
			t = this->bridge_scope.bridge->town;
		}
		else {
			t = ClosestTownFromTile(this->bridge_scope.tile, UINT_MAX);
		}
		if (t == nullptr) return nullptr;
		this->town_scope = new TownScopeResolver(*this, t, this->bridge_scope.bridge == nullptr);
	}
	return this->town_scope;
}

GrfSpecFeature BridgeResolverObject::GetFeature() const
{
	return GSF_BRIDGES;
}

uint32 BridgeResolverObject::GetDebugID() const
{
	return this->bridge_scope.spec->grf_prop.local_id;
}
