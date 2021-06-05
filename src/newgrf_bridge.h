/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_bridge.h Functions related to NewGRF bridges. */

#ifndef NEWGRF_BRIDGE_H
#define NEWGRF_BRIDGE_H

#include "newgrf_callbacks.h"
#include "newgrf_spritegroup.h"
#include "date_type.h"
#include "bridge_type.h"
#include "bridge.h"
#include "bridge_map.h"
#include "newgrf_town.h"
#include "newgrf_commons.h"


/**
 * Struct containing information about a single bridge type
 */
struct BridgeSpec {
	Year avail_year;                    ///< the year where it becomes available
	byte min_length;                    ///< the minimum length (not counting start and end tile)
	uint16 max_length;                  ///< the maximum length (not counting start and end tile)
	uint16 price;                       ///< the price multiplier
	uint16 speed;                       ///< maximum travel speed (1 unit = 1/1.6 mph = 1 km-ish/h)
	SpriteID sprite;                    ///< the sprite which is used in the GUI
	PaletteID pal;                      ///< the palette which is used in the GUI
	StringID material;                  ///< the string that contains the bridge description
	StringID transport_name[2];         ///< description of the bridge, when built for road or rail
	PalSpriteID **sprite_table;         ///< table of sprites for drawing the bridge
	byte flags;                         ///< bit 0 set: disable drawing of far pillars.
	bool enabled;                       ///< the bridge is available to build (true by default, but can be disabled by newgrf)

	GRFFilePropsBase<BSG_END> grf_prop; ///< Properties related the the grf file
	bool use_custom_sprites;            ///< Does action3 exist for this item

	/**
	 * Get the specification of a bridge type.
	 * @param i The type of bridge to get the specification for.
	 * @return The specification.
	 */
	static inline BridgeSpec *Get(BridgeType i) {
		extern BridgeSpec _bridge_specs[];

		assert(i < NUM_BRIDGES);
		return &_bridge_specs[i];
	}
};

/** Bridge scope resolver. */
struct BridgeScopeResolver : public ScopeResolver {
	struct Bridge *bridge;  ///< The bridge the callback is ran for.
	const BridgeSpec *spec; ///< Specification of the bridge type.
	TileIndex tile;         ///< The tile related to the bridge.

	/**
	 * Constructor of an bridge scope resolver.
	 * @param ro Surrounding resolver.
	 * @param bridge Bridge being resolved.
	 * @param tile %Tile of the object.
	 */
	BridgeScopeResolver(ResolverObject& ro, Bridge *bridge, const BridgeSpec *spec, TileIndex tile)
		: ScopeResolver(ro), bridge(bridge), spec(spec), tile(tile)
	{
	}

	uint32 GetRandomBits() const override;
	uint32 GetVariable(byte variable, uint32 parameter, bool *available) const override;
};

/** A resolver object to be used with feature 06 spritegroups. */
struct BridgeResolverObject : public ResolverObject {
	BridgeScopeResolver bridge_scope; ///< The bridge scope resolver.
	TownScopeResolver *town_scope;    ///< The town scope resolver (created on the first call).

	BridgeResolverObject(const BridgeSpec *spec, Bridge *bridge, TileIndex tile, BridgeSpriteGroup bsg,
		CallbackID callback = CBID_NO_CALLBACK, uint32 param1 = 0, uint32 param2 = 0);
	~BridgeResolverObject();

	ScopeResolver *GetScope(VarSpriteGroupScope scope = VSG_SCOPE_SELF, byte relative = 0) override
	{
		switch (scope) {
		case VSG_SCOPE_SELF:
			return &this->bridge_scope;

		case VSG_SCOPE_PARENT: {
			TownScopeResolver *tsr = this->GetTown();
			if (tsr != nullptr) return tsr;
			FALLTHROUGH;
		}

		default:
			return ResolverObject::GetScope(scope, relative);
		}
	}

	const SpriteGroup *ResolveReal(const RealSpriteGroup *group) const override;

	GrfSpecFeature GetFeature() const override;
	uint32 GetDebugID() const override;

private:
	TownScopeResolver *GetTown();
};

SpriteID GetCustomBridgeSprites(const BridgeSpec *spec, Bridge *bridge, TileIndex tile, BridgeSpriteGroup bsg, uint *num_results = nullptr);

#endif /* NEWGRF_BRIDGE_H */
