/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_tile.hpp Everything to query and manipulate tiles. */

#ifndef SCRIPT_TILE_HPP
#define SCRIPT_TILE_HPP

#include "script_error.hpp"
#include "script_company.hpp"
#include "../../slope_type.h"
#include "../../transport_type.h"

/**
 * Class that handles all tile related functions.
 * @api ai game
 */
class ScriptTile : public ScriptObject {
public:
	/**
	 * Error messages related to modifying tiles.
	 */
	enum ErrorMessages {

		/** Base for tile related errors */
		ERR_TILE_BASE = ScriptError::ERR_CAT_TILE << ScriptError::ERR_CAT_BIT_SIZE,

		/** Tile can't be raised any higher */
		ERR_TILE_TOO_HIGH,                     // [STR_ERROR_ALREADY_AT_SEA_LEVEL]

		/** Tile can't be lowered any lower */
		ERR_TILE_TOO_LOW,                      // [STR_ERROR_ALREADY_AT_SEA_LEVEL]

		/** The area was already flat */
		ERR_AREA_ALREADY_FLAT,                 // [STR_ERROR_ALREADY_LEVELLED]

		/** There is a tunnel underneath */
		ERR_EXCAVATION_WOULD_DAMAGE,           // [STR_ERROR_EXCAVATION_WOULD_DAMAGE]

		/** Reached the limit for terraforming/clearing/planting */
		ERR_LIMIT_REACHED,                     // [STR_ERROR_TERRAFORM_LIMIT_REACHED, STR_ERROR_CLEARING_LIMIT_REACHED, STR_ERROR_TREE_PLANT_LIMIT_REACHED]
	};

	/**
	 * Enumeration for corners of tiles.
	 */
	enum Corner {
		/* Note: these values represent part of the in-game Corner enum */
		CORNER_W       = ::CORNER_W,       ///< West corner
		CORNER_S       = ::CORNER_S,       ///< South corner
		CORNER_E       = ::CORNER_E,       ///< East corner
		CORNER_N       = ::CORNER_N,       ///< North corner

		CORNER_INVALID = ::CORNER_INVALID, ///< An invalid corner
	};

	/**
	 * Enumeration for the slope-type.
	 *
	 * This enumeration use the chars N, E, S, W corresponding the
	 *  direction North, East, South and West. The top corner of a tile
	 *  is the north-part of the tile.
	 */
	enum Slope {
		/* Note: these values represent part of the in-game Slope enum */
		SLOPE_FLAT     = ::SLOPE_FLAT,     ///< A flat tile
		SLOPE_W        = ::SLOPE_W,        ///< The west corner of the tile is raised
		SLOPE_S        = ::SLOPE_S,        ///< The south corner of the tile is raised
		SLOPE_E        = ::SLOPE_E,        ///< The east corner of the tile is raised
		SLOPE_N        = ::SLOPE_N,        ///< The north corner of the tile is raised
		SLOPE_STEEP    = ::SLOPE_STEEP,    ///< Indicates the slope is steep (The corner opposite of the not-raised corner is raised two times)
		SLOPE_NW       = ::SLOPE_NW,       ///< North and west corner are raised
		SLOPE_SW       = ::SLOPE_SW,       ///< South and west corner are raised
		SLOPE_SE       = ::SLOPE_SE,       ///< South and east corner are raised
		SLOPE_NE       = ::SLOPE_NE,       ///< North and east corner are raised
		SLOPE_EW       = ::SLOPE_EW,       ///< East and west corner are raised
		SLOPE_NS       = ::SLOPE_NS,       ///< North and south corner are raised
		SLOPE_ELEVATED = ::SLOPE_ELEVATED, ///< Bit mask containing all 'simple' slopes. Does not appear as a slope.
		SLOPE_NWS      = ::SLOPE_NWS,      ///< North, west and south corner are raised
		SLOPE_WSE      = ::SLOPE_WSE,      ///< West, south and east corner are raised
		SLOPE_SEN      = ::SLOPE_SEN,      ///< South, east and north corner are raised
		SLOPE_ENW      = ::SLOPE_ENW,      ///< East, north and west corner are raised
		SLOPE_STEEP_W  = ::SLOPE_STEEP_W,  ///< A steep slope falling to east (from west)
		SLOPE_STEEP_S  = ::SLOPE_STEEP_S,  ///< A steep slope falling to north (from south)
		SLOPE_STEEP_E  = ::SLOPE_STEEP_E,  ///< A steep slope falling to west (from east)
		SLOPE_STEEP_N  = ::SLOPE_STEEP_N,  ///< A steep slope falling to south (from north)

		/* Custom added value, only valid for this API */
		SLOPE_INVALID  = 0xFFFF,           ///< An invalid slope
	};

	/**
	 * The different transport types a tile can have.
	 */
	enum TransportType {
		/* Note: these values represent part of the in-game TransportType enum */
		TRANSPORT_RAIL    =  ::TRANSPORT_RAIL,  ///< Tile with rail.
		TRANSPORT_ROAD    =  ::TRANSPORT_ROAD,  ///< Tile with road.
		TRANSPORT_WATER   =  ::TRANSPORT_WATER, ///< Tile with navigable waterways.
		TRANSPORT_AIR     =  ::TRANSPORT_AIR,   ///< Tile with airport.

		/* Custom added value, only valid for this API */
		TRANSPORT_INVALID = -1, ///< Tile without any transport type.
	};

	/**
	 * Get the base cost for building/clearing several things.
	 */
	enum BuildType {
		BT_FOUNDATION,   ///< Build a foundation under something
		BT_TERRAFORM,    ///< Terraform
		BT_BUILD_TREES,  ///< Build trees
		BT_CLEAR_GRASS,  ///< Clear a tile with just grass
		BT_CLEAR_ROUGH,  ///< Clear a rough tile
		BT_CLEAR_ROCKY,  ///< Clear a tile with rocks
		BT_CLEAR_FIELDS, ///< Clear a tile with farm fields
		BT_CLEAR_HOUSE,  ///< Clear a tile with a house
		BT_CLEAR_WATER,  ///< Clear a tile with either river or sea
	};

	/**
	 * The types of terrain a tile can have.
	 *
	 * @note When a desert or rainforest tile are changed, their terrain type will remain the same. In other words, a sea tile can be of the desert terrain type.
	 * @note The snow terrain type can change to the normal terrain type and vice versa based on landscaping or variable snow lines from NewGRFs.
	 */
	enum TerrainType {
		TERRAIN_NORMAL,     ///< A normal tile (default); not desert, rainforest or snow.
		TERRAIN_DESERT,     ///< A tile in the desert (manually set in in scenarios, below certain height and certain distance from water in random games).
		TERRAIN_RAINFOREST, ///< A tile in the rainforest (manually set in scenarios, certain distance away from deserts in random games),
		TERRAIN_SNOW        ///< A tile on or above the snowline level.
	};

	/**
	 * Check if this tile is buildable, i.e. no things on it that needs
	 *  demolishing.
	 * @param tile The tile to check on.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @return True if it is buildable, false if not.
	 * @note For trams you also might want to check for ScriptRoad::IsRoad(),
	 *   as you can build tram-rails on road-tiles.
	 * @note For rail you also might want to check for ScriptRoad::IsRoad(),
	 *   as in some cases you can build rails on road-tiles.
	 */
	static bool IsBuildable(TileIndex tile);

	/**
	 * Check if this tile is buildable in a rectangle around a tile, with the
	 *  entry in the list as top-left.
	 * @param tile The tile to check on.
	 * @param width The width of the rectangle.
	 * @param height The height of the rectangle.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @return True if it is buildable, false if not.
	 */
	static bool IsBuildableRectangle(TileIndex tile, uint width, uint height);

	/**
	 * Checks whether the given tile is actually a sea tile.
	 * @param tile The tile to check on.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @return True if and only if the tile is a sea tile.
	 */
	static bool IsSeaTile(TileIndex tile);

	/**
	 * Checks whether the given tile is actually a river tile.
	 * @param tile The tile to check on.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @return True if and only if the tile is a river tile.
	 */
	static bool IsRiverTile(TileIndex tile);

	/**
	 * Checks whether the given tile is actually a water tile.
	 * @param tile The tile to check on.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @return True if and only if the tile is a water tile.
	 * @note Returns false when a buoy was placed on the tile.
	 */
	static bool IsWaterTile(TileIndex tile);

	/**
	 * Checks whether the given tile is actually a coast tile.
	 * @param tile The tile to check.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @return True if and only if the tile is a coast tile.
	 * @note Building on coast tiles in general is more expensive. This is not
	 *  true if there are also trees on the tile, see #HasTreeOnTile.
	 */
	static bool IsCoastTile(TileIndex tile);

	/**
	 * Checks whether the given tile is a station tile of any station.
	 * @param tile The tile to check.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @return True if and only if the tile is a station tile.
	 */
	static bool IsStationTile(TileIndex tile);

	/**
	 * Check if a tile has a steep slope.
	 * Steep slopes are slopes with a height difference of 2 across one diagonal of the tile.
	 * @param slope The slope to check on.
	 * @pre slope != SLOPE_INVALID.
	 * @return True if the slope is a steep slope.
	 */
	static bool IsSteepSlope(Slope slope);

	/**
	 * Check if a tile has a halftile slope.
	 * Halftile slopes appear on top of halftile foundations. E.g. the slope you get when building a horizontal railtrack on the top of a SLOPE_N or SLOPE_STEEP_N.
	 * @param slope The slope to check on.
	 * @pre slope != SLOPE_INVALID.
	 * @return True if the slope is a halftile slope.
	 * @note Currently there is no API function that would return or accept a halftile slope.
	 */
	static bool IsHalftileSlope(Slope slope);

	/**
	 * Check if the tile has any tree on it.
	 * @param tile The tile to check on.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @return True if and only if there is a tree on the tile.
	 */
	static bool HasTreeOnTile(TileIndex tile);

	/**
	 * Check if the tile is a farmland tile.
	 * @param tile The tile to check on.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @return True if and only if the tile is farmland.
	 */
	static bool IsFarmTile(TileIndex tile);

	/**
	 * Check if the tile is a rock tile.
	 * @param tile The tile to check on.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @return True if and only if the tile is rock tile.
	 */
	static bool IsRockTile(TileIndex tile);

	/**
	 * Check if the tile is a rough tile.
	 * @param tile The tile to check on.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @return True if and only if the tile is rough tile.
	 */
	static bool IsRoughTile(TileIndex tile);

	/**
	 * Check if the tile without buildings or infrastructure is a snow tile.
	 * @note If you want to know if a tile (with or without buildings and infrastructure) is on or above the snowline, use ScriptTile::GetTerrainType(tile).
	 * @param tile The tile to check on.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @return True if and only if the tile is snow tile.
	 */
	static bool IsSnowTile(TileIndex tile);

	/**
	 * Check if the tile without buildings or infrastructure is a desert tile.
	 * @note If you want to know if a tile (with or without buildings and infrastructure) is in a desert, use ScriptTile::GetTerrainType(tile).
	 * @param tile The tile to check on.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @return True if and only if the tile is desert tile.
	 */
	static bool IsDesertTile(TileIndex tile);

	/**
	 * Get the type of terrain regardless of buildings or infrastructure.
	 * @note When a desert or rainforest tile are changed, their terrain type will remain the same. In other words, a sea tile can be of the desert terrain type.
	 * @note The snow terrain type can change to the normal terrain type and vice versa based on landscaping or variable snow lines from NewGRFs.
	 * @param tile The tile to check on.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @return The #TerrainType.
	 */
	static TerrainType GetTerrainType(TileIndex tile);

	/**
	 * Get the slope of a tile.
	 * This is the slope of the bare tile. A possible foundation on the tile does not influence this slope.
	 * @param tile The tile to check on.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @return Bit mask encoding the slope. See #Slope for a description of the returned values.
	 */
	static Slope GetSlope(TileIndex tile);

	/**
	 * Get the complement of the slope.
	 * @param slope The slope to get the complement of.
	 * @pre slope != SLOPE_INVALID.
	 * @pre !IsSteepSlope(slope).
	 * @pre !IsHalftileSlope(slope).
	 * @return The complement of a slope. This means that all corners that
	 *  weren't raised, are raised, and visa versa.
	 */
	static Slope GetComplementSlope(Slope slope);

	/**
	 * Get the minimal height on a tile.
	 * The returned height is the height of the bare tile. A possible foundation on the tile does not influence this height.
	 * @param tile The tile to check on.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @return The height of the lowest corner of the tile, ranging from 0 to 15.
	 */
	static int32 GetMinHeight(TileIndex tile);

	/**
	 * Get the maximal height on a tile.
	 * The returned height is the height of the bare tile. A possible foundation on the tile does not influence this height.
	 * @param tile The tile to check on.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @return The height of the highest corner of the tile, ranging from 0 to 15.
	 */
	static int32 GetMaxHeight(TileIndex tile);

	/**
	 * Get the height of a certain corner of a tile.
	 * The returned height is the height of the bare tile. A possible foundation on the tile does not influence this height.
	 * @param tile The tile to check on.
	 * @param corner The corner to query.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @return The height of the lowest corner of the tile, ranging from 0 to 15.
	 */
	static int32 GetCornerHeight(TileIndex tile, Corner corner);

	/**
	 * Get the owner of the tile.
	 * @param tile The tile to get the owner from.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @return The CompanyID of the owner of the tile, or COMPANY_INVALID if
	 *  there is no owner (grass/industry/water tiles, etc.).
	 */
	static ScriptCompany::CompanyID GetOwner(TileIndex tile);

	/**
	 * Checks whether the given tile contains parts suitable for the given
	 *  TransportType.
	 * @param tile The tile to check.
	 * @param transport_type The TransportType to check against.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @pre transport_type != TRANSPORT_AIR.
	 * @note Returns false on tiles with roadworks and on road tiles with only
	 *       a single piece of road as these tiles cannot be used to transport
	 *       anything on. It furthermore returns true on some coast tile for
	 *       TRANSPORT_WATER because ships can navigate over them.
	 * @note Use ScriptAirport.IsAirportTile to check for airport tiles. Aircraft
	 *       can fly over every tile on the map so using HasTransportType
	 *       doesn't make sense for TRANSPORT_AIR.
	 * @return True if and only if the tile has the given TransportType.
	 */
	static bool HasTransportType(TileIndex tile, TransportType transport_type);

	/**
	 * Check how much cargo this tile accepts.
	 *  It creates a radius around the tile, and adds up all acceptance of this
	 *   cargo.
	 * @param tile The tile to check on.
	 * @param cargo_type The cargo to check the acceptance of.
	 * @param width The width of the station.
	 * @param height The height of the station.
	 * @param radius The radius of the station.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @pre ScriptCargo::IsValidCargo(cargo_type)
	 * @pre width > 0.
	 * @pre height > 0.
	 * @pre radius >= 0.
	 * @return Values below 8 mean no acceptance; the more the better.
	 */
	static int32 GetCargoAcceptance(TileIndex tile, CargoID cargo_type, int width, int height, int radius);

	/**
	 * Checks how many producers in the radius produces this cargo.
	 *  It creates a radius around the tile, and counts all producer of this cargo.
	 * @param tile The tile to check on.
	 * @param cargo_type The cargo to check the production of.
	 * @param width The width of the station.
	 * @param height The height of the station.
	 * @param radius The radius of the station.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @pre ScriptCargo::IsValidCargo(cargo_type)
	 * @pre width > 0.
	 * @pre height > 0.
	 * @pre radius >= 0.
	 * @return The number of producers that produce this cargo within radius of the tile.
	 */
	static int32 GetCargoProduction(TileIndex tile, CargoID cargo_type, int width, int height, int radius);

	/**
	 * Get the manhattan distance from the tile to the tile.
	 * @param tile_from The tile to get the distance to.
	 * @param tile_to The tile to get the distance to.
	 * @return The distance between the two tiles.
	 */
	static int32 GetDistanceManhattanToTile(TileIndex tile_from, TileIndex tile_to);

	/**
	 * Get the square distance from the tile to the tile.
	 * @param tile_from The tile to get the distance to.
	 * @param tile_to The tile to get the distance to.
	 * @return The distance between the two tiles.
	 */
	static int32 GetDistanceSquareToTile(TileIndex tile_from, TileIndex tile_to);

	/**
	 * Raise the given corners of the tile. The corners can be combined,
	 *  for example: SLOPE_N | SLOPE_W (= SLOPE_NW) will raise the west and the north corner.
	 * @note The corners will be modified in the order west (first), south, east, north (last).
	 *       Changing one corner might cause another corner to be changed too. So modifiing
	 *       multiple corners may result in changing some corners by multiple steps.
	 * @param tile The tile to raise.
	 * @param slope Corners to raise (SLOPE_xxx).
	 * @pre tile < ScriptMap::GetMapSize().
	 * @game @pre Valid ScriptCompanyMode active in scope.
	 * @exception ScriptError::ERR_AREA_NOT_CLEAR
	 * @exception ScriptError::ERR_TOO_CLOSE_TO_EDGE
	 * @exception ScriptTile::ERR_TILE_TOO_HIGH
	 * @return 0 means failed, 1 means success.
	 */
	static bool RaiseTile(TileIndex tile, int32 slope);

	/**
	 * Lower the given corners of the tile. The corners can be combined,
	 *  for example: SLOPE_N | SLOPE_W (= SLOPE_NW) will lower the west and the north corner.
	 * @note The corners will be modified in the order west (first), south, east, north (last).
	 *       Changing one corner might cause another corner to be changed too. So modifiing
	 *       multiple corners may result in changing some corners by multiple steps.
	 * @param tile The tile to lower.
	 * @param slope Corners to lower (SLOPE_xxx).
	 * @pre tile < ScriptMap::GetMapSize().
	 * @game @pre Valid ScriptCompanyMode active in scope.
	 * @exception ScriptError::ERR_AREA_NOT_CLEAR
	 * @exception ScriptError::ERR_TOO_CLOSE_TO_EDGE
	 * @exception ScriptTile::ERR_TILE_TOO_LOW
	 * @return 0 means failed, 1 means success.
	 */
	static bool LowerTile(TileIndex tile, int32 slope);

	/**
	 * Level all tiles in the rectangle between start_tile and end_tile so they
	 *  are at the same height. All tiles will be raised or lowered until
	 *  they are at height ScriptTile::GetCornerHeight(start_tile, ScriptTile::CORNER_N).
	 * @param start_tile One corner of the rectangle to level.
	 * @param end_tile The opposite corner of the rectangle.
	 * @pre start_tile < ScriptMap::GetMapSize().
	 * @pre end_tile < ScriptMap::GetMapSize().
	 * @game @pre Valid ScriptCompanyMode active in scope.
	 * @exception ScriptError::ERR_AREA_NOT_CLEAR
	 * @exception ScriptError::ERR_TOO_CLOSE_TO_EDGE
	 * @return True if one or more tiles were leveled.
	 * @note Even if leveling some part fails, some other part may have been
	 *  successfully leveled already.
	 * @note This function may return true in ScriptTestMode, although it fails in
	 *  ScriptExecMode.
	 */
	static bool LevelTiles(TileIndex start_tile, TileIndex end_tile);

	/**
	 * Destroy everything on the given tile.
	 * @param tile The tile to demolish.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @exception ScriptError::ERR_AREA_NOT_CLEAR
	 * @return True if and only if the tile was demolished.
	 */
	static bool DemolishTile(TileIndex tile);

	/**
	 * Create a random tree on a tile.
	 * @param tile The tile to build a tree on.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @game @pre Valid ScriptCompanyMode active in scope.
	 * @return True if and only if a tree was added on the tile.
	 */
	static bool PlantTree(TileIndex tile);

	/**
	 * Create a random tree on a rectangle of tiles.
	 * @param tile The top left tile of the rectangle.
	 * @param width The width of the rectangle.
	 * @param height The height of the rectangle.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @pre width >= 1 && width <= 20.
	 * @pre height >= 1 && height <= 20.
	 * @game @pre Valid ScriptCompanyMode active in scope.
	 * @return True if and only if a tree was added on any of the tiles in the rectangle.
	 */
	static bool PlantTreeRectangle(TileIndex tile, uint width, uint height);

	/**
	 * Find out if this tile is within the rating influence of a town.
	 *  If a station sign would be on this tile, the servicing quality of the station would
	 *  influence the rating of the town.
	 * @param tile The tile to check.
	 * @param town_id The town to check.
	 * @return True if the tile is within the rating influence of the town.
	 */
	static bool IsWithinTownInfluence(TileIndex tile, TownID town_id);

	/**
	 * Find the town which has authority for the tile.
	 *  The rating of your company in this town will be checked and affected when
	 *  building stations, trees etc.
	 * @param tile The tile to check.
	 * @return The TownID of the town which has authority on this tile.
	 */
	static TownID GetTownAuthority(TileIndex tile);

	/**
	 * Find the town that is closest to a tile. Stations you build at this tile
	 *  will belong to this town.
	 * @param tile The tile to check.
	 * @return The TownID of the town closest to the tile.
	 */
	static TownID GetClosestTown(TileIndex tile);

	/**
	 * Get the baseprice of building/clearing various tile-related things.
	 * @param build_type the type to build
	 * @return The baseprice of building or removing the given object.
	 */
	static Money GetBuildCost(BuildType build_type);
};

#endif /* SCRIPT_TILE_HPP */
