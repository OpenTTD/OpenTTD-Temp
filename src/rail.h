/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file rail.h Rail specific functions. */

#ifndef RAIL_H
#define RAIL_H

#include "rail_type.h"
#include "track_type.h"
#include "gfx_type.h"
#include "core/bitmath_func.hpp"
#include "economy_func.h"
#include "slope_type.h"
#include "strings_type.h"
#include "date_type.h"
#include "signal_type.h"
#include "settings_type.h"

/** Railtype flags. */
enum RailTypeFlags {
	RTF_CATENARY          = 0,                           ///< Bit number for drawing a catenary.
	RTF_NO_LEVEL_CROSSING = 1,                           ///< Bit number for disallowing level crossings.
	RTF_HIDDEN            = 2,                           ///< Bit number for hiding from selection.
	RTF_NO_SPRITE_COMBINE = 3,                           ///< Bit number for using non-combined junctions.
	RTF_ALLOW_90DEG       = 4,                           ///< Bit number for always allowed 90 degree turns, regardless of setting.
	RTF_DISALLOW_90DEG    = 5,                           ///< Bit number for never allowed 90 degree turns, regardless of setting.

	RTFB_NONE              = 0,                          ///< All flags cleared.
	RTFB_CATENARY          = 1 << RTF_CATENARY,          ///< Value for drawing a catenary.
	RTFB_NO_LEVEL_CROSSING = 1 << RTF_NO_LEVEL_CROSSING, ///< Value for disallowing level crossings.
	RTFB_HIDDEN            = 1 << RTF_HIDDEN,            ///< Value for hiding from selection.
	RTFB_NO_SPRITE_COMBINE = 1 << RTF_NO_SPRITE_COMBINE, ///< Value for using non-combined junctions.
	RTFB_ALLOW_90DEG       = 1 << RTF_ALLOW_90DEG,       ///< Value for always allowed 90 degree turns, regardless of setting.
	RTFB_DISALLOW_90DEG    = 1 << RTF_DISALLOW_90DEG,    ///< Value for never allowed 90 degree turns, regardless of setting.
};
DECLARE_ENUM_AS_BIT_SET(RailTypeFlags)

struct SpriteGroup;

/** Sprite groups for a railtype. */
enum RailTypeSpriteGroup {
	RTSG_CURSORS,     ///< Cursor and toolbar icon images
	RTSG_OVERLAY,     ///< Images for overlaying track
	RTSG_GROUND,      ///< Main group of ground images
	RTSG_TUNNEL,      ///< Main group of ground images for snow or desert
	RTSG_WIRES,       ///< Catenary wires
	RTSG_PYLONS,      ///< Catenary pylons
	RTSG_BRIDGE,      ///< Bridge surface images
	RTSG_CROSSING,    ///< Level crossing overlay images
	RTSG_DEPOT,       ///< Depot images
	RTSG_FENCES,      ///< Fence images
	RTSG_TUNNEL_PORTAL, ///< Tunnel portal overlay
	RTSG_SIGNALS,     ///< Signal images
	RTSG_GROUND_COMPLETE, ///< Complete ground images
	RTSG_END,
};

/**
 * Offsets for sprites within an overlay/underlay set.
 * These are the same for overlay and underlay sprites.
 */
enum RailTrackOffset {
	RTO_X,            ///< Piece of rail in X direction
	RTO_Y,            ///< Piece of rail in Y direction
	RTO_N,            ///< Piece of rail in northern corner
	RTO_S,            ///< Piece of rail in southern corner
	RTO_E,            ///< Piece of rail in eastern corner
	RTO_W,            ///< Piece of rail in western corner
	RTO_SLOPE_NE,     ///< Piece of rail on slope with north-east raised
	RTO_SLOPE_SE,     ///< Piece of rail on slope with south-east raised
	RTO_SLOPE_SW,     ///< Piece of rail on slope with south-west raised
	RTO_SLOPE_NW,     ///< Piece of rail on slope with north-west raised
	RTO_CROSSING_XY,  ///< Crossing of X and Y rail, with ballast
	RTO_JUNCTION_SW,  ///< Ballast for junction 'pointing' SW
	RTO_JUNCTION_NE,  ///< Ballast for junction 'pointing' NE
	RTO_JUNCTION_SE,  ///< Ballast for junction 'pointing' SE
	RTO_JUNCTION_NW,  ///< Ballast for junction 'pointing' NW
	RTO_JUNCTION_NSEW,///< Ballast for full junction
};

/**
 * Offsets for sprites within a bridge surface overlay set.
 */
enum RailTrackBridgeOffset {
	RTBO_X,     ///< Piece of rail in X direction
	RTBO_Y,     ///< Piece of rail in Y direction
	RTBO_SLOPE, ///< Sloped rail pieces, in order NE, SE, SW, NW
};

/**
 * Offsets from base sprite for fence sprites. These are in the order of
 *  the sprites in the original data files.
 */
enum RailFenceOffset {
	RFO_FLAT_X_NW,     //!< Slope FLAT, Track X,     Fence NW
	RFO_FLAT_Y_NE,     //!< Slope FLAT, Track Y,     Fence NE
	RFO_FLAT_LEFT,     //!< Slope FLAT, Track LEFT,  Fence E
	RFO_FLAT_UPPER,    //!< Slope FLAT, Track UPPER, Fence S
	RFO_SLOPE_SW_NW,   //!< Slope SW,   Track X,     Fence NW
	RFO_SLOPE_SE_NE,   //!< Slope SE,   Track Y,     Fence NE
	RFO_SLOPE_NE_NW,   //!< Slope NE,   Track X,     Fence NW
	RFO_SLOPE_NW_NE,   //!< Slope NW,   Track Y,     Fence NE
	RFO_FLAT_X_SE,     //!< Slope FLAT, Track X,     Fence SE
	RFO_FLAT_Y_SW,     //!< Slope FLAT, Track Y,     Fence SW
	RFO_FLAT_RIGHT,    //!< Slope FLAT, Track RIGHT, Fence W
	RFO_FLAT_LOWER,    //!< Slope FLAT, Track LOWER, Fence N
	RFO_SLOPE_SW_SE,   //!< Slope SW,   Track X,     Fence SE
	RFO_SLOPE_SE_SW,   //!< Slope SE,   Track Y,     Fence SW
	RFO_SLOPE_NE_SE,   //!< Slope NE,   Track X,     Fence SE
	RFO_SLOPE_NW_SW,   //!< Slope NW,   Track Y,     Fence SW
};

/** List of rail type labels. */
typedef std::vector<RailTypeLabel> RailTypeLabelList;

/**
 * This struct contains all the info that is needed to draw and construct tracks.
 */
class RailtypeInfo {
public:
	/**
	 * Struct containing the main sprites. @note not all sprites are listed, but only
	 *  the ones used directly in the code
	 */
	struct {
		SpriteID track_y;      ///< single piece of rail in Y direction, with ground
		SpriteID track_ns;     ///< two pieces of rail in North and South corner (East-West direction)
		SpriteID ground;       ///< ground sprite for a 3-way switch
		SpriteID single_x;     ///< single piece of rail in X direction, without ground
		SpriteID single_y;     ///< single piece of rail in Y direction, without ground
		SpriteID single_n;     ///< single piece of rail in the northern corner
		SpriteID single_s;     ///< single piece of rail in the southern corner
		SpriteID single_e;     ///< single piece of rail in the eastern corner
		SpriteID single_w;     ///< single piece of rail in the western corner
		SpriteID single_sloped;///< single piece of rail for slopes
		SpriteID crossing;     ///< level crossing, rail in X direction
		SpriteID tunnel;       ///< tunnel sprites base
	} base_sprites;

	/**
	 * struct containing the sprites for the rail GUI. @note only sprites referred to
	 * directly in the code are listed
	 */
	struct {
		SpriteID build_ns_rail;      ///< button for building single rail in N-S direction
		SpriteID build_x_rail;       ///< button for building single rail in X direction
		SpriteID build_ew_rail;      ///< button for building single rail in E-W direction
		SpriteID build_y_rail;       ///< button for building single rail in Y direction
		SpriteID auto_rail;          ///< button for the autorail construction
		SpriteID build_depot;        ///< button for building depots
		SpriteID build_tunnel;       ///< button for building a tunnel
		SpriteID convert_rail;       ///< button for converting rail
		SpriteID signals[SIGTYPE_END][2][2]; ///< signal GUI sprites (type, variant, state)
	} gui_sprites;

	struct {
		CursorID rail_ns;    ///< Cursor for building rail in N-S direction
		CursorID rail_swne;  ///< Cursor for building rail in X direction
		CursorID rail_ew;    ///< Cursor for building rail in E-W direction
		CursorID rail_nwse;  ///< Cursor for building rail in Y direction
		CursorID autorail;   ///< Cursor for autorail tool
		CursorID depot;      ///< Cursor for building a depot
		CursorID tunnel;     ///< Cursor for building a tunnel
		CursorID convert;    ///< Cursor for converting track
	} cursor;                    ///< Cursors associated with the rail type.

	struct {
		StringID name;            ///< Name of this rail type.
		StringID toolbar_caption; ///< Caption in the construction toolbar GUI for this rail type.
		StringID menu_text;       ///< Name of this rail type in the main toolbar dropdown.
		StringID build_caption;   ///< Caption of the build vehicle GUI for this rail type.
		StringID replace_text;    ///< Text used in the autoreplace GUI.
		StringID new_loco;        ///< Name of an engine for this type of rail in the engine preview GUI.
	} strings;                        ///< Strings associated with the rail type.

	/** sprite number difference between a piece of track on a snowy ground and the corresponding one on normal ground */
	SpriteID snow_offset;

	/** bitmask to the OTHER railtypes on which an engine of THIS railtype generates power */
	RailTypes powered_railtypes;

	/** bitmask to the OTHER railtypes on which an engine of THIS railtype can physically travel */
	RailTypes compatible_railtypes;

	/**
	 * Bridge offset
	 */
	SpriteID bridge_offset;

	/**
	 * Original railtype number to use when drawing non-newgrf railtypes, or when drawing stations.
	 */
	byte fallback_railtype;

	/**
	 * Multiplier for curve maximum speed advantage
	 */
	byte curve_speed;

	/**
	 * Bit mask of rail type flags
	 */
	RailTypeFlags flags;

	/**
	 * Cost multiplier for building this rail type
	 */
	uint16 cost_multiplier;

	/**
	 * Cost multiplier for maintenance of this rail type
	 */
	uint16 maintenance_multiplier;

	/**
	 * Acceleration type of this rail type
	 */
	uint8 acceleration_type;

	/**
	 * Maximum speed for vehicles travelling on this rail type
	 */
	uint16 max_speed;

	/**
	 * Unique 32 bit rail type identifier
	 */
	RailTypeLabel label;

	/**
	 * Rail type labels this type provides in addition to the main label.
	 */
	RailTypeLabelList alternate_labels;

	/**
	 * Colour on mini-map
	 */
	byte map_colour;

	/**
	 * Introduction date.
	 * When #INVALID_DATE or a vehicle using this railtype gets introduced earlier,
	 * the vehicle's introduction date will be used instead for this railtype.
	 * The introduction at this date is furthermore limited by the
	 * #introduction_required_railtypes.
	 */
	Date introduction_date;

	/**
	 * Bitmask of railtypes that are required for this railtype to be introduced
	 * at a given #introduction_date.
	 */
	RailTypes introduction_required_railtypes;

	/**
	 * Bitmask of which other railtypes are introduced when this railtype is introduced.
	 */
	RailTypes introduces_railtypes;

	/**
	 * The sorting order of this railtype for the toolbar dropdown.
	 */
	byte sorting_order;

	/**
	 * NewGRF providing the Action3 for the railtype. nullptr if not available.
	 */
	const GRFFile *grffile[RTSG_END];

	/**
	 * Sprite groups for resolving sprites
	 */
	const SpriteGroup *group[RTSG_END];

	inline bool UsesOverlay() const
	{
		return this->group[RTSG_GROUND] != nullptr;
	}

	/**
	 * Offset between the current railtype and normal rail. This means that:<p>
	 * 1) All the sprites in a railset MUST be in the same order. This order
	 *    is determined by normal rail. Check sprites 1005 and following for this order<p>
	 * 2) The position where the railtype is loaded must always be the same, otherwise
	 *    the offset will fail.
	 */
	inline uint GetRailtypeSpriteOffset() const
	{
		return 82 * this->fallback_railtype;
	}
};


/**
 * Returns a pointer to the Railtype information for a given railtype
 * @param railtype the rail type which the information is requested for
 * @return The pointer to the RailtypeInfo
 */
static inline const RailtypeInfo *GetRailTypeInfo(RailType railtype)
{
	extern RailtypeInfo _railtypes[RAILTYPE_END];
	assert(railtype < RAILTYPE_END);
	return &_railtypes[railtype];
}

/**
 * Checks if an engine of the given RailType can drive on a tile with a given
 * RailType. This would normally just be an equality check, but for electric
 * rails (which also support non-electric engines).
 * @return Whether the engine can drive on this tile.
 * @param  enginetype The RailType of the engine we are considering.
 * @param  tiletype   The RailType of the tile we are considering.
 */
static inline bool IsCompatibleRail(RailType enginetype, RailType tiletype)
{
	return HasBit(GetRailTypeInfo(enginetype)->compatible_railtypes, tiletype);
}

/**
 * Checks if an engine of the given RailType got power on a tile with a given
 * RailType. This would normally just be an equality check, but for electric
 * rails (which also support non-electric engines).
 * @return Whether the engine got power on this tile.
 * @param  enginetype The RailType of the engine we are considering.
 * @param  tiletype   The RailType of the tile we are considering.
 */
static inline bool HasPowerOnRail(RailType enginetype, RailType tiletype)
{
	return HasBit(GetRailTypeInfo(enginetype)->powered_railtypes, tiletype);
}

/**
 * Checks if an engine of the given RailType got power on a set of tiles
 * provided with various RailTypes. This would normally just be an equality
 * check, but for electric rails (which also support non-electric engines).
 * @return Whether the engine got power on this tile.
 * @param  enginetype The RailType of the engine we are considering.
 * @param  rail_types The RailTypes we are considering.
 */
static inline bool HasPowerOnRails(RailType enginetype, RailTypes rail_types)
{
	return (GetRailTypeInfo(enginetype)->powered_railtypes & rail_types) != 0;
}

/**
 * Test if a RailType disallows build of level crossings.
 * @param rt The RailType to check.
 * @return Whether level crossings are not allowed.
 */
static inline bool RailNoLevelCrossings(RailType rt)
{
	return HasBit(GetRailTypeInfo(rt)->flags, RTF_NO_LEVEL_CROSSING);
}

/**
 * Test if 90 degree turns are disallowed between two railtypes.
 * @param rt1 First railtype to test for.
 * @param rt2 Second railtype to test for.
 * @param def Default value to use if the rail type doesn't specify anything.
 * @return True if 90 degree turns are disallowed between the two rail types.
 */
static inline bool Rail90DegTurnDisallowed(RailType rt1, RailType rt2, bool def = _settings_game.pf.forbid_90_deg)
{
	if (rt1 == INVALID_RAILTYPE || rt2 == INVALID_RAILTYPE) return def;

	const RailtypeInfo *rti1 = GetRailTypeInfo(rt1);
	const RailtypeInfo *rti2 = GetRailTypeInfo(rt2);

	bool rt1_90deg = HasBit(rti1->flags, RTF_DISALLOW_90DEG) || (!HasBit(rti1->flags, RTF_ALLOW_90DEG) && def);
	bool rt2_90deg = HasBit(rti2->flags, RTF_DISALLOW_90DEG) || (!HasBit(rti2->flags, RTF_ALLOW_90DEG) && def);

	return rt1_90deg || rt2_90deg;
}

/**
 * Returns the cost of building the specified railtype.
 * @param railtype The railtype being built.
 * @return The cost multiplier.
 */
static inline Money RailBuildCost(RailType railtype)
{
	assert(railtype < RAILTYPE_END);
	return (_price[PR_BUILD_RAIL] * GetRailTypeInfo(railtype)->cost_multiplier) >> 3;
}

/**
 * Returns the 'cost' of clearing the specified railtype.
 * @param railtype The railtype being removed.
 * @return The cost.
 */
static inline Money RailClearCost(RailType railtype)
{
	/* Clearing rail in fact earns money, but if the build cost is set
	 * very low then a loophole exists where money can be made.
	 * In this case we limit the removal earnings to 3/4s of the build
	 * cost.
	 */
	assert(railtype < RAILTYPE_END);
	return std::max(_price[PR_CLEAR_RAIL], -RailBuildCost(railtype) * 3 / 4);
}

/**
 * Calculates the cost of rail conversion
 * @param from The railtype we are converting from
 * @param to   The railtype we are converting to
 * @return Cost per TrackBit
 */
static inline Money RailConvertCost(RailType from, RailType to)
{
	/* Get the costs for removing and building anew
	 * A conversion can never be more costly */
	Money rebuildcost = RailBuildCost(to) + RailClearCost(from);

	/* Conversion between somewhat compatible railtypes:
	 * Pay 1/8 of the target rail cost (labour costs) and additionally any difference in the
	 * build costs, if the target type is more expensive (material upgrade costs).
	 * Upgrade can never be more expensive than re-building. */
	if (HasPowerOnRail(from, to) || HasPowerOnRail(to, from)) {
		Money upgradecost = RailBuildCost(to) / 8 + std::max((Money)0, RailBuildCost(to) - RailBuildCost(from));
		return std::min(upgradecost, rebuildcost);
	}

	/* make the price the same as remove + build new type for rail types
	 * which are not compatible in any way */
	return rebuildcost;
}

/**
 * Calculates the maintenance cost of a number of track bits.
 * @param railtype The railtype to get the cost of.
 * @param num Number of track bits of this railtype.
 * @param total_num Total number of track bits of all railtypes.
 * @return Total cost.
 */
static inline Money RailMaintenanceCost(RailType railtype, uint32 num, uint32 total_num)
{
	assert(railtype < RAILTYPE_END);
	return (_price[PR_INFRASTRUCTURE_RAIL] * GetRailTypeInfo(railtype)->maintenance_multiplier * num * (1 + IntSqrt(total_num))) >> 11; // 4 bits fraction for the multiplier and 7 bits scaling.
}

/**
 * Calculates the maintenance cost of a number of signals.
 * @param num Number of signals.
 * @return Total cost.
 */
static inline Money SignalMaintenanceCost(uint32 num)
{
	return (_price[PR_INFRASTRUCTURE_RAIL] * 15 * num * (1 + IntSqrt(num))) >> 8; // 1 bit fraction for the multiplier and 7 bits scaling.
}

void DrawTrainDepotSprite(int x, int y, int image, RailType railtype);
int TicksToLeaveDepot(const Train *v);

Foundation GetRailFoundation(Slope tileh, TrackBits bits);


bool HasRailtypeAvail(const CompanyID company, const RailType railtype);
bool HasAnyRailtypesAvail(const CompanyID company);
bool ValParamRailtype(const RailType rail);

RailTypes AddDateIntroducedRailTypes(RailTypes current, Date date);

RailTypes GetCompanyRailtypes(CompanyID company, bool introduces = true);
RailTypes GetRailTypes(bool introduces);

RailType GetRailTypeByLabel(RailTypeLabel label, bool allow_alternate_labels = true);

void ResetRailTypes();
void InitRailTypes();
RailType AllocateRailType(RailTypeLabel label);

extern std::vector<RailType> _sorted_railtypes;
extern RailTypes _railtypes_hidden_mask;

#endif /* RAIL_H */
