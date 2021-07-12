/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_marine.hpp Everything to query and build marine. */

#ifndef SCRIPT_MARINE_HPP
#define SCRIPT_MARINE_HPP

#include "script_error.hpp"

/**
 * Class that handles all marine related functions.
 * @api ai game
 */
class ScriptMarine : public ScriptObject {
public:
	/**
	 * All marine related error messages.
	 */
	enum ErrorMessages {
		/** Base for marine related errors */
		ERR_MARINE_BASE = ScriptError::ERR_CAT_MARINE << ScriptError::ERR_CAT_BIT_SIZE,

		/** Infrastructure must be built on water */
		ERR_MARINE_MUST_BE_BUILT_ON_WATER,                  // [STR_ERROR_MUST_BE_BUILT_ON_WATER]
	};

	/**
	 * Types of water-related objects in the game.
	 */
	enum BuildType {
		BT_DOCK,  ///< Build a dock
		BT_DEPOT, ///< Build a ship depot
		BT_BUOY,  ///< Build a buoy
		BT_LOCK,  ///< Build a lock
		BT_CANAL, ///< Build a canal
	};

	/**
	 * Checks whether the given tile is actually a tile with a water depot.
	 * @param tile The tile to check.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @return True if and only if the tile has a water depot.
	 */
	static bool IsWaterDepotTile(TileIndex tile);

	/**
	 * Checks whether the given tile is actually a tile with a dock.
	 * @param tile The tile to check.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @return True if and only if the tile has a dock.
	 */
	static bool IsDockTile(TileIndex tile);

	/**
	 * Checks whether the given tile is actually a tile with a buoy.
	 * @param tile The tile to check.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @return True if and only if the tile has a buoy.
	 */
	static bool IsBuoyTile(TileIndex tile);

	/**
	 * Checks whether the given tile is actually a tile with a lock.
	 * @param tile The tile to check.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @return True if and only if the tile has a lock.
	 */
	static bool IsLockTile(TileIndex tile);

	/**
	 * Checks whether the given tile is actually a tile with a canal.
	 * @param tile The tile to check.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @return True if and only if the tile has a canal.
	 */
	static bool IsCanalTile(TileIndex tile);

	/**
	 * Checks whether the given tiles are directly connected, i.e. whether
	 *  a ship vehicle can travel from the center of the first tile to the
	 *  center of the second tile.
	 * @param tile_from The source tile.
	 * @param tile_to The destination tile.
	 * @pre ScriptMap::IsValidTile(tile_from).
	 * @pre ScriptMap::IsValidTile(tile_to).
	 * @pre 'tile_from' and 'tile_to' are directly neighbouring tiles.
	 * @return True if and only if a ship can go from tile_from to tile_to.
	 */
	static bool AreWaterTilesConnected(TileIndex tile_from, TileIndex tile_to);

	/**
	 * Builds a water depot on tile.
	 * @param tile The tile where the water depot will be build.
	 * @param front A tile on the same axis with 'tile' as the depot shall be oriented.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @pre ScriptMap::IsValidTile(front).
	 * @game @pre Valid ScriptCompanyMode active in scope.
	 * @exception ScriptError::ERR_AREA_NOT_CLEAR
	 * @exception ScriptError::ERR_SITE_UNSUITABLE
	 * @exception ScriptMarine::ERR_MARINE_MUST_BE_BUILT_ON_WATER
	 * @return Whether the water depot has been/can be build or not.
	 * @note A WaterDepot is 1 tile in width, and 2 tiles in length.
	 * @note The depot will be built towards the south from 'tile', not necessarily towards 'front'.
	 */
	static bool BuildWaterDepot(TileIndex tile, TileIndex front);

	/**
	 * Builds a dock where tile is the tile still on land.
	 * @param tile The tile still on land of the dock.
	 * @param station_id The station to join, ScriptStation::STATION_NEW or ScriptStation::STATION_JOIN_ADJACENT.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @pre station_id == ScriptStation::STATION_NEW || station_id == ScriptStation::STATION_JOIN_ADJACENT || ScriptStation::IsValidStation(station_id).
	 * @game @pre Valid ScriptCompanyMode active in scope.
	 * @exception ScriptError::ERR_AREA_NOT_CLEAR
	 * @exception ScriptError::ERR_SITE_UNSUITABLE
	 * @exception ScriptStation::ERR_STATION_TOO_CLOSE_TO_ANOTHER_STATION
	 * @exception ScriptStation::ERR_STATION_TOO_MANY_STATIONS
	 * @return Whether the dock has been/can be build or not.
	 */
	static bool BuildDock(TileIndex tile, StationID station_id);

	/**
	 * Builds a buoy on tile.
	 * @param tile The tile where the buoy will be build.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @game @pre Valid ScriptCompanyMode active in scope.
	 * @exception ScriptError::ERR_AREA_NOT_CLEAR
	 * @exception ScriptError::ERR_SITE_UNSUITABLE
	 * @exception ScriptStation::ERR_STATION_TOO_MANY_STATIONS
	 * @return Whether the buoy has been/can be build or not.
	 */
	static bool BuildBuoy(TileIndex tile);

	/**
	 * Builds a lock on tile.
	 * @param tile The tile where the lock will be build.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @game @pre Valid ScriptCompanyMode active in scope.
	 * @exception ScriptError::ERR_LAND_SLOPED_WRONG
	 * @exception ScriptError::ERR_SITE_UNSUITABLE
	 * @return Whether the lock has been/can be build or not.
	 */
	static bool BuildLock(TileIndex tile);

	/**
	 * Builds a canal on tile.
	 * @param tile The tile where the canal will be build.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @game @pre Valid ScriptCompanyMode active in scope.
	 * @exception ScriptError::ERR_AREA_NOT_CLEAR
	 * @exception ScriptError::ERR_LAND_SLOPED_WRONG
	 * @exception ScriptError::ERR_OWNED_BY_ANOTHER_COMPANY
	 * @exception ScriptError::ERR_ALREADY_BUILT
	 * @return Whether the canal has been/can be build or not.
	 */
	static bool BuildCanal(TileIndex tile);

	/**
	 * Removes a water depot.
	 * @param tile Any tile of the water depot.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @game @pre Valid ScriptCompanyMode active in scope.
	 * @exception ScriptError::ERR_OWNED_BY_ANOTHER_COMPANY
	 * @return Whether the water depot has been/can be removed or not.
	 */
	static bool RemoveWaterDepot(TileIndex tile);

	/**
	 * Removes a dock.
	 * @param tile Any tile of the dock.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @game @pre Valid ScriptCompanyMode active in scope.
	 * @exception ScriptError::ERR_OWNED_BY_ANOTHER_COMPANY
	 * @return Whether the dock has been/can be removed or not.
	 */
	static bool RemoveDock(TileIndex tile);

	/**
	 * Removes a buoy.
	 * @param tile Any tile of the buoy.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @game @pre Valid ScriptCompanyMode active in scope.
	 * @exception ScriptError::ERR_OWNED_BY_ANOTHER_COMPANY
	 * @return Whether the buoy has been/can be removed or not.
	 */
	static bool RemoveBuoy(TileIndex tile);

	/**
	 * Removes a lock.
	 * @param tile Any tile of the lock.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @game @pre Valid ScriptCompanyMode active in scope.
	 * @exception ScriptError::ERR_OWNED_BY_ANOTHER_COMPANY
	 * @return Whether the lock has been/can be removed or not.
	 */
	static bool RemoveLock(TileIndex tile);

	/**
	 * Removes a canal.
	 * @param tile Any tile of the canal.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @game @pre Valid ScriptCompanyMode active in scope.
	 * @exception ScriptError::ERR_OWNED_BY_ANOTHER_COMPANY
	 * @return Whether the canal has been/can be removed or not.
	 */
	static bool RemoveCanal(TileIndex tile);

	/**
	 * Get the baseprice of building a water-related object.
	 * @param build_type the type of object to build
	 * @return The baseprice of building the given object.
	 */
	static Money GetBuildCost(BuildType build_type);
};

#endif /* SCRIPT_MARINE_HPP */
