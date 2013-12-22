/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file road.h Road specific functions. */

#ifndef ROAD_H
#define ROAD_H

#include "road_type.h"
#include "gfx_type.h"
#include "core/bitmath_func.hpp"
#include "strings_type.h"
#include "date_type.h"
#include "core/enum_type.hpp"
#include "newgrf.h"
#include "economy_func.h"

#include <vector>

enum RoadTramType : bool {
	RTT_ROAD,
	RTT_TRAM,
};

enum RoadTramTypes : uint8 {
	RTTB_ROAD = 1 << RTT_ROAD,
	RTTB_TRAM = 1 << RTT_TRAM,
};
DECLARE_ENUM_AS_BIT_SET(RoadTramTypes)

static const RoadTramType _roadtramtypes[] = { RTT_ROAD, RTT_TRAM };

/** Roadtype flags. Starts with RO instead of R because R is used for rails */
enum RoadTypeFlags {
	ROTF_CATENARY = 0,                                     ///< Bit number for adding catenary
	ROTF_NO_LEVEL_CROSSING,                                ///< Bit number for disabling level crossing
	ROTF_NO_HOUSES,                                        ///< Bit number for setting this roadtype as not house friendly
	ROTF_HIDDEN,                                           ///< Bit number for hidden from construction.
	ROTF_TOWN_BUILD,                                       ///< Bit number for allowing towns to build this roadtype.

	ROTFB_NONE = 0,                                        ///< All flags cleared.
	ROTFB_CATENARY          = 1 << ROTF_CATENARY,          ///< Value for drawing a catenary.
	ROTFB_NO_LEVEL_CROSSING = 1 << ROTF_NO_LEVEL_CROSSING, ///< Value for disabling a level crossing.
	ROTFB_NO_HOUSES         = 1 << ROTF_NO_HOUSES,         ///< Value for for setting this roadtype as not house friendly.
	ROTFB_HIDDEN            = 1 << ROTF_HIDDEN,            ///< Value for hidden from construction.
	ROTFB_TOWN_BUILD        = 1 << ROTF_TOWN_BUILD,        ///< Value for allowing towns to build this roadtype.
};
DECLARE_ENUM_AS_BIT_SET(RoadTypeFlags)

struct SpriteGroup;

/** Sprite groups for a roadtype. */
enum RoadTypeSpriteGroup {
	ROTSG_CURSORS,        ///< Optional: Cursor and toolbar icon images
	ROTSG_OVERLAY,        ///< Optional: Images for overlaying track
	ROTSG_GROUND,         ///< Required: Main group of ground images
	ROTSG_reserved1,      ///<           Placeholder, if we need specific tunnel sprites.
	ROTSG_CATENARY_FRONT, ///< Optional: Catenary front
	ROTSG_CATENARY_BACK,  ///< Optional: Catenary back
	ROTSG_BRIDGE,         ///< Required: Bridge surface images
	ROTSG_reserved2,      ///<           Placeholder, if we need specific level crossing sprites.
	ROTSG_DEPOT,          ///< Optional: Depot images
	ROTSG_reserved3,      ///<           Placeholder, if we add road fences (for highways).
	ROTSG_ROADSTOP,       ///< Required: Drive-in stop surface
	ROTSG_END,
};

/** List of road type labels. */
typedef std::vector<RoadTypeLabel> RoadTypeLabelList;

class RoadTypeInfo {
public:
	/**
	 * struct containing the sprites for the road GUI. @note only sprites referred to
	 * directly in the code are listed
	 */
	struct {
		SpriteID build_x_road;        ///< button for building single rail in X direction
		SpriteID build_y_road;        ///< button for building single rail in Y direction
		SpriteID auto_road;           ///< button for the autoroad construction
		SpriteID build_depot;         ///< button for building depots
		SpriteID build_tunnel;        ///< button for building a tunnel
		SpriteID convert_road;        ///< button for converting road types
	} gui_sprites;

	struct {
		CursorID road_swne;     ///< Cursor for building rail in X direction
		CursorID road_nwse;     ///< Cursor for building rail in Y direction
		CursorID autoroad;      ///< Cursor for autorail tool
		CursorID depot;         ///< Cursor for building a depot
		CursorID tunnel;        ///< Cursor for building a tunnel
		SpriteID convert_road;  ///< Cursor for converting road types
	} cursor;                       ///< Cursors associated with the road type.

	struct {
		StringID name;            ///< Name of this rail type.
		StringID toolbar_caption; ///< Caption in the construction toolbar GUI for this rail type.
		StringID menu_text;       ///< Name of this rail type in the main toolbar dropdown.
		StringID build_caption;   ///< Caption of the build vehicle GUI for this rail type.
		StringID replace_text;    ///< Text used in the autoreplace GUI.
		StringID new_engine;      ///< Name of an engine for this type of road in the engine preview GUI.

		StringID err_build_road;        ///< Building a normal piece of road
		StringID err_remove_road;       ///< Removing a normal piece of road
		StringID err_depot;             ///< Building a depot
		StringID err_build_station[2];  ///< Building a bus or truck station
		StringID err_remove_station[2]; ///< Removing of a bus or truck station
		StringID err_convert_road;      ///< Converting a road type

		StringID picker_title[2];       ///< Title for the station picker for bus or truck stations
		StringID picker_tooltip[2];     ///< Tooltip for the station picker for bus or truck stations
	} strings;                        ///< Strings associated with the rail type.

	/** bitmask to the OTHER roadtypes on which a vehicle of THIS roadtype generates power */
	RoadTypes powered_roadtypes;

	/**
	 * Bit mask of road type flags
	 */
	RoadTypeFlags flags;

	/**
	 * Cost multiplier for building this road type
	 */
	uint16 cost_multiplier;

	/**
	 * Cost multiplier for maintenance of this road type
	 */
	uint16 maintenance_multiplier;

	/**
	 * Maximum speed for vehicles travelling on this road type
	 */
	uint16 max_speed;

	/**
	 * Unique 32 bit road type identifier
	 */
	RoadTypeLabel label;

	/**
	 * Road type labels this type provides in addition to the main label.
	 */
	RoadTypeLabelList alternate_labels;

	/**
	 * Colour on mini-map
	 */
	byte map_colour;

	/**
	 * Introduction date.
	 * When #INVALID_DATE or a vehicle using this roadtype gets introduced earlier,
	 * the vehicle's introduction date will be used instead for this roadtype.
	 * The introduction at this date is furthermore limited by the
	 * #introduction_required_types.
	 */
	Date introduction_date;

	/**
	 * Bitmask of roadtypes that are required for this roadtype to be introduced
	 * at a given #introduction_date.
	 */
	RoadTypes introduction_required_roadtypes;

	/**
	 * Bitmask of which other roadtypes are introduced when this roadtype is introduced.
	 */
	RoadTypes introduces_roadtypes;

	/**
	 * The sorting order of this roadtype for the toolbar dropdown.
	 */
	byte sorting_order;

	/**
	 * NewGRF providing the Action3 for the roadtype. nullptr if not available.
	 */
	const GRFFile *grffile[ROTSG_END];

	/**
	 * Sprite groups for resolving sprites
	 */
	const SpriteGroup *group[ROTSG_END];

	inline bool UsesOverlay() const
	{
		return this->group[ROTSG_GROUND] != nullptr;
	}
};

extern RoadTypes _roadtypes_type;

static inline bool RoadTypeIsRoad(RoadType roadtype)
{
	return !HasBit(_roadtypes_type, roadtype);
}

static inline bool RoadTypeIsTram(RoadType roadtype)
{
	return HasBit(_roadtypes_type, roadtype);
}

static inline RoadTramType GetRoadTramType(RoadType roadtype)
{
	return RoadTypeIsTram(roadtype) ? RTT_TRAM : RTT_ROAD;
}

static inline RoadTramType OtherRoadTramType(RoadTramType rtt)
{
	return rtt == RTT_ROAD ? RTT_TRAM : RTT_ROAD;
}

/**
 * Returns a pointer to the Roadtype information for a given roadtype
 * @param roadtype the road type which the information is requested for
 * @return The pointer to the RoadTypeInfo
 */
static inline const RoadTypeInfo *GetRoadTypeInfo(RoadType roadtype)
{
	extern RoadTypeInfo _roadtypes[ROADTYPE_END];
	assert(roadtype < ROADTYPE_END);
	return &_roadtypes[roadtype];
}

/**
 * Checks if an engine of the given RoadType got power on a tile with a given
 * RoadType. This would normally just be an equality check, but for electrified
 * roads (which also support non-electric vehicles).
 * @return Whether the engine got power on this tile.
 * @param  enginetype The RoadType of the engine we are considering.
 * @param  tiletype   The RoadType of the tile we are considering.
 */
static inline bool HasPowerOnRoad(RoadType enginetype, RoadType tiletype)
{
	return HasBit(GetRoadTypeInfo(enginetype)->powered_roadtypes, tiletype);
}

/**
 * Checks if an engine of the given RoadType got power on a set of tiles
 * with a given RoadTypes. This would normally just be an equality check,
 * but for electrified roads (which also support non-electric vehicles).
 * @return Whether the engine got power on this tile.
 * @param  enginetype The RoadType of the engine we are considering.
 * @param  railtypes  The RoadTypes of the tiles we are considering.
 */
static inline bool HasPowerOnRoads(RoadType enginetype, RoadTypes road_types)
{
	return (GetRoadTypeInfo(enginetype)->powered_roadtypes & road_types) != 0;
}

/**
 * Returns the cost of building the specified roadtype.
 * @param roadtype The roadtype being built.
 * @return The cost multiplier.
 */
static inline Money RoadBuildCost(RoadType roadtype)
{
	assert(roadtype < ROADTYPE_END);
	return (_price[PR_BUILD_ROAD] * GetRoadTypeInfo(roadtype)->cost_multiplier) >> 3;
}

/**
 * Returns the cost of clearing the specified roadtype.
 * @param roadtype The roadtype being removed.
 * @return The cost.
 */
static inline Money RoadClearCost(RoadType roadtype)
{
	assert(roadtype < ROADTYPE_END);

	/* Flat fee for removing road. */
	if (RoadTypeIsRoad(roadtype)) return _price[PR_CLEAR_ROAD];

	/* Clearing tram earns a little money, but also incurs the standard clear road cost,
	 * so no profit can be made. */
	return _price[PR_CLEAR_ROAD] - RoadBuildCost(roadtype) * 3 / 4;
}

/**
 * Calculates the cost of road conversion
 * @param from The roadtype we are converting from
 * @param to   The roadtype we are converting to
 * @return Cost per RoadBit
 */
static inline Money RoadConvertCost(RoadType from, RoadType to)
{
	/* Don't apply convert costs when converting to the same roadtype (ex. building a roadstop over existing road) */
	if (from == to) return (Money)0;

	/* Same cost as removing and then building. */
	return RoadBuildCost(to) + RoadClearCost(from);
}

/**
 * Test if road disallows level crossings
 * @param roadtype The roadtype we are testing
 * @return True iff the roadtype disallows level crossings
 */
static inline bool RoadNoLevelCrossing(RoadType roadtype)
{
	assert(roadtype < ROADTYPE_END);
	return HasBit(GetRoadTypeInfo(roadtype)->flags, ROTF_NO_LEVEL_CROSSING);
}

RoadType GetRoadTypeByLabel(RoadTypeLabel label, bool allow_alternate_labels = true);

void ResetRoadTypes();
void InitRoadTypes();
RoadType AllocateRoadType(RoadTypeLabel label, RoadTramType rtt);
bool HasAnyRoadTypesAvail(CompanyID company, RoadTramType rtt);

extern std::vector<RoadType> _sorted_roadtypes;
extern RoadTypes _roadtypes_hidden_mask;

#endif /* ROAD_H */
