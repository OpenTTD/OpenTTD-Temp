/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_rail.hpp Everything to query and build rails. */

#ifndef SCRIPT_RAIL_HPP
#define SCRIPT_RAIL_HPP

#include "script_tile.hpp"
#include "../../signal_type.h"
#include "../../track_type.h"

/**
 * Class that handles all rail related functions.
 * @api ai game
 */
class ScriptRail : public ScriptObject {
public:
	/**
	 * All rail related error messages.
	 */
	enum ErrorMessages {
		/** Base for rail building / maintaining errors */
		ERR_RAIL_BASE = ScriptError::ERR_CAT_RAIL << ScriptError::ERR_CAT_BIT_SIZE,

		/** One-way roads cannot have crossings */
		ERR_CROSSING_ON_ONEWAY_ROAD,       // [STR_ERROR_CROSSING_ON_ONEWAY_ROAD]

		/** No suitable track could be found */
		ERR_UNSUITABLE_TRACK,              // [STR_ERROR_NO_SUITABLE_RAILROAD_TRACK, STR_ERROR_THERE_IS_NO_RAILROAD_TRACK, STR_ERROR_THERE_ARE_NO_SIGNALS, STR_ERROR_THERE_IS_NO_STATION]

		/** This railtype cannot have crossings */
		ERR_RAILTYPE_DISALLOWS_CROSSING,   // [STR_ERROR_CROSSING_DISALLOWED_RAIL]
	};

	/**
	 * Types of rail known to the game.
	 */
	enum RailType : byte {
		/* Note: these values represent part of the in-game static values */
		RAILTYPE_INVALID  = ::INVALID_RAILTYPE, ///< Invalid RailType.
	};

	/**
	 * A bitmap with all possible rail tracks on a tile.
	 */
	enum RailTrack {
		/* Note: these values represent part of the in-game TrackBits enum */
		RAILTRACK_NE_SW   = ::TRACK_BIT_X,       ///< Track along the x-axis (north-east to south-west).
		RAILTRACK_NW_SE   = ::TRACK_BIT_Y,       ///< Track along the y-axis (north-west to south-east).
		RAILTRACK_NW_NE   = ::TRACK_BIT_UPPER,   ///< Track in the upper corner of the tile (north).
		RAILTRACK_SW_SE   = ::TRACK_BIT_LOWER,   ///< Track in the lower corner of the tile (south).
		RAILTRACK_NW_SW   = ::TRACK_BIT_LEFT,    ///< Track in the left corner of the tile (west).
		RAILTRACK_NE_SE   = ::TRACK_BIT_RIGHT,   ///< Track in the right corner of the tile (east).
		RAILTRACK_INVALID = ::INVALID_TRACK_BIT, ///< Flag for an invalid track.
	};

	/**
	 * Types of signal known to the game.
	 */
	enum SignalType {
		/* Note: these values represent part of the in-game SignalType enum */
		SIGNALTYPE_NORMAL        = ::SIGTYPE_NORMAL,     ///< Normal signal.
		SIGNALTYPE_ENTRY         = ::SIGTYPE_ENTRY,      ///< Entry presignal.
		SIGNALTYPE_EXIT          = ::SIGTYPE_EXIT,       ///< Exit signal.
		SIGNALTYPE_COMBO         = ::SIGTYPE_COMBO,      ///< Combo signal.
		SIGNALTYPE_PBS           = ::SIGTYPE_PBS,        ///< Normal PBS signal.
		SIGNALTYPE_PBS_ONEWAY    = ::SIGTYPE_PBS_ONEWAY, ///< No-entry PBS signal.

		SIGNALTYPE_TWOWAY        = 8, ///< Bit mask for twoway signal.
		SIGNALTYPE_NORMAL_TWOWAY = SIGNALTYPE_NORMAL | SIGNALTYPE_TWOWAY, ///< Normal twoway signal.
		SIGNALTYPE_ENTRY_TWOWAY  = SIGNALTYPE_ENTRY  | SIGNALTYPE_TWOWAY, ///< Entry twoway signal.
		SIGNALTYPE_EXIT_TWOWAY   = SIGNALTYPE_EXIT   | SIGNALTYPE_TWOWAY, ///< Exit twoway signal.
		SIGNALTYPE_COMBO_TWOWAY  = SIGNALTYPE_COMBO  | SIGNALTYPE_TWOWAY, ///< Combo twoway signal.

		SIGNALTYPE_NONE          = 0xFF, ///< No signal.
	};

	/**
	 * Types of rail-related objects in the game.
	 */
	enum BuildType {
		BT_TRACK,    ///< Build a track
		BT_SIGNAL,   ///< Build a signal
		BT_DEPOT,    ///< Build a depot
		BT_STATION,  ///< Build a station
		BT_WAYPOINT, ///< Build a rail waypoint
	};

	/**
	 * Get the name of a rail type.
	 * @param rail_type The rail type to get the name of.
	 * @pre IsRailTypeAvailable(rail_type).
	 * @return The name the rail type has.
	 * @note Since there is no string with only the name of the track, the text which
	 *  is shown in the dropdown where you can chose a track type is returned. This
	 *  means that the name could be something like "Maglev construction" instead
	 *  of just "Maglev".
	 */
	static char *GetName(RailType rail_type);

	/**
	 * Checks whether the given tile is actually a tile with rail that can be
	 *  used to traverse a tile. This excludes rail depots but includes
	 *  stations and waypoints.
	 * @param tile The tile to check.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @return True if and only if the tile has rail.
	 */
	static bool IsRailTile(TileIndex tile);

	/**
	 * Checks whether there is a road / rail crossing on a tile.
	 * @param tile The tile to check.
	 * @return True if and only if there is a road / rail crossing.
	 */
	static bool IsLevelCrossingTile(TileIndex tile);

	/**
	 * Checks whether the given tile is actually a tile with a rail depot.
	 * @param tile The tile to check.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @return True if and only if the tile has a rail depot.
	 */
	static bool IsRailDepotTile(TileIndex tile);

	/**
	 * Checks whether the given tile is actually a tile with a rail station.
	 * @param tile The tile to check.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @return True if and only if the tile has a rail station.
	 */
	static bool IsRailStationTile(TileIndex tile);

	/**
	 * Checks whether the given tile is actually a tile with a rail waypoint.
	 * @param tile The tile to check.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @return True if and only if the tile has a rail waypoint.
	 */
	static bool IsRailWaypointTile(TileIndex tile);

	/**
	 * Check if a given RailType is available.
	 * @param rail_type The RailType to check for.
	 * @return True if this RailType can be used.
	 */
	static bool IsRailTypeAvailable(RailType rail_type);

	/**
	 * Get the current RailType set for all ScriptRail functions.
	 * @return The RailType currently set.
	 */
	static RailType GetCurrentRailType();

	/**
	 * Set the RailType for all further ScriptRail functions.
	 * @param rail_type The RailType to set.
	 */
	static void SetCurrentRailType(RailType rail_type);

	/**
	 * Check if a train build for a rail type can run on another rail type.
	 * @param engine_rail_type The rail type the train is build for.
	 * @param track_rail_type The type you want to check.
	 * @pre ScriptRail::IsRailTypeAvailable(engine_rail_type).
	 * @pre ScriptRail::IsRailTypeAvailable(track_rail_type).
	 * @return Whether a train build for 'engine_rail_type' can run on 'track_rail_type'.
	 * @note Even if a train can run on a RailType that doesn't mean that it'll be
	 *   able to power the train. Use TrainHasPowerOnRail for that.
	 */
	static bool TrainCanRunOnRail(ScriptRail::RailType engine_rail_type, ScriptRail::RailType track_rail_type);

	/**
	 * Check if a train build for a rail type has power on another rail type.
	 * @param engine_rail_type The rail type the train is build for.
	 * @param track_rail_type The type you want to check.
	 * @pre ScriptRail::IsRailTypeAvailable(engine_rail_type).
	 * @pre ScriptRail::IsRailTypeAvailable(track_rail_type).
	 * @return Whether a train build for 'engine_rail_type' has power on 'track_rail_type'.
	 */
	static bool TrainHasPowerOnRail(ScriptRail::RailType engine_rail_type, ScriptRail::RailType track_rail_type);

	/**
	 * Get the RailType that is used on a tile.
	 * @param tile The tile to check.
	 * @pre ScriptTile::HasTransportType(tile, ScriptTile.TRANSPORT_RAIL).
	 * @return The RailType that is used on a tile.
	 */
	static RailType GetRailType(TileIndex tile);

	/**
	 * Convert the tracks on all tiles within a rectangle to another RailType.
	 * @param start_tile One corner of the rectangle.
	 * @param end_tile The opposite corner of the rectangle.
	 * @param convert_to The RailType you want to convert the rails to.
	 * @pre ScriptMap::IsValidTile(start_tile).
	 * @pre ScriptMap::IsValidTile(end_tile).
	 * @pre IsRailTypeAvailable(convert_to).
	 * @game @pre Valid ScriptCompanyMode active in scope.
	 * @exception ScriptRail::ERR_UNSUITABLE_TRACK
	 * @return Whether at least some rail has been converted successfully.
	 */
	static bool ConvertRailType(TileIndex start_tile, TileIndex end_tile, ScriptRail::RailType convert_to);

	/**
	 * Gets the tile in front of a rail depot.
	 * @param depot The rail depot tile.
	 * @pre IsRailDepotTile(depot).
	 * @return The tile in front of the depot.
	 */
	static TileIndex GetRailDepotFrontTile(TileIndex depot);

	/**
	 * Gets the direction of a rail station tile.
	 * @param tile The rail station tile.
	 * @pre IsRailStationTile(tile).
	 * @return The direction of the station (either RAILTRACK_NE_SW or RAILTRACK_NW_SE).
	 */
	static RailTrack GetRailStationDirection(TileIndex tile);

	/**
	 * Builds a rail depot.
	 * @param tile Place to build the depot.
	 * @param front The tile exactly in front of the depot.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @pre ScriptMap::IsValidTile(front).
	 * @pre 'tile' is not equal to 'front', but in a straight line of it.
	 * @pre IsRailTypeAvailable(GetCurrentRailType()).
	 * @game @pre Valid ScriptCompanyMode active in scope.
	 * @exception ScriptError::ERR_FLAT_LAND_REQUIRED
	 * @exception ScriptError::ERR_AREA_NOT_CLEAR
	 * @return Whether the rail depot has been/can be build or not.
	 */
	static bool BuildRailDepot(TileIndex tile, TileIndex front);

	/**
	 * Removes rail depots from an area.
	 * @param start_tile Start tile of the area.
	 * @param end_tile End tile of the area.
	 * @pre ScriptMap::IsValidTile(start_tile).
	 * @pre ScriptMap::IsValidTile(end_tile).
	 * @game @pre Valid ScriptCompanyMode active in scope.
	 * @exception ScriptError::ERR_FLAT_LAND_REQUIRED
	 * @return Whether all depot tiles of the owner in the area have been/can be cleared or not.
	 * @todo look for all the exceptions.
	 */
	static bool RemoveRailDepot(TileIndex start_tile, TileIndex end_tile);

	/**
	 * Build a rail station.
	 * @param tile Place to build the station.
	 * @param direction The direction to build the station.
	 * @param num_platforms The number of platforms to build.
	 * @param platform_length The length of each platform.
	 * @param station_id The station to join, ScriptStation::STATION_NEW or ScriptStation::STATION_JOIN_ADJACENT.
	 * @pre IsRailTypeAvailable(GetCurrentRailType()).
	 * @pre ScriptMap::IsValidTile(tile).
	 * @pre direction == RAILTRACK_NW_SE || direction == RAILTRACK_NE_SW.
	 * @pre num_platforms > 0 && num_platforms <= 255.
	 * @pre platform_length > 0 && platform_length <= 255.
	 * @pre station_id == ScriptStation::STATION_NEW || station_id == ScriptStation::STATION_JOIN_ADJACENT || ScriptStation::IsValidStation(station_id).
	 * @game @pre Valid ScriptCompanyMode active in scope.
	 * @exception ScriptError::ERR_OWNED_BY_ANOTHER_COMPANY
	 * @exception ScriptError::ERR_AREA_NOT_CLEAR
	 * @exception ScriptError::ERR_FLAT_LAND_REQUIRED
	 * @exception ScriptStation::ERR_STATION_TOO_CLOSE_TO_ANOTHER_STATION
	 * @exception ScriptStation::ERR_STATION_TOO_MANY_STATIONS
	 * @exception ScriptStation::ERR_STATION_TOO_MANY_STATIONS_IN_TOWN
	 * @return Whether the station has been/can be build or not.
	 */
	static bool BuildRailStation(TileIndex tile, RailTrack direction, uint num_platforms, uint platform_length, StationID station_id);

	/**
	 * Build a NewGRF rail station. This calls callback 18 to let a NewGRF
	 *  provide the station class / id to build, so we don't end up with
	 *  only the default stations on the map.
	 * When no NewGRF provides a rail station, or an unbuildable rail station is
	 *  returned by a NewGRF, this function will fall back to building a default
	 *  non-NewGRF station as if ScriptRail::BuildRailStation was called.
	 * @param tile Place to build the station.
	 * @param direction The direction to build the station.
	 * @param num_platforms The number of platforms to build.
	 * @param platform_length The length of each platform.
	 * @param station_id The station to join, ScriptStation::STATION_NEW or ScriptStation::STATION_JOIN_ADJACENT.
	 * @param cargo_id The CargoID of the cargo that will be transported from / to this station.
	 * @param source_industry The IndustryType of the industry you'll transport goods from, ScriptIndustryType::INDUSTRYTYPE_UNKNOWN or ScriptIndustryType::INDUSTRYTYPE_TOWN.
	 * @param goal_industry The IndustryType of the industry you'll transport goods to, ScriptIndustryType::INDUSTRYTYPE_UNKNOWN or ScriptIndustryType::INDUSTRYTYPE_TOWN.
	 * @param distance The manhattan distance you'll transport the cargo over.
	 * @param source_station True if this is the source station, false otherwise.
	 * @pre IsRailTypeAvailable(GetCurrentRailType()).
	 * @pre ScriptMap::IsValidTile(tile).
	 * @pre direction == RAILTRACK_NW_SE || direction == RAILTRACK_NE_SW.
	 * @pre num_platforms > 0 && num_platforms <= 255.
	 * @pre platform_length > 0 && platform_length <= 255.
	 * @pre station_id == ScriptStation::STATION_NEW || station_id == ScriptStation::STATION_JOIN_ADJACENT || ScriptStation::IsValidStation(station_id).
	 * @pre ScriptCargo::IsValidCargo(cargo_type)
	 * @pre source_industry == ScriptIndustryType::INDUSTRYTYPE_UNKNOWN || source_industry == ScriptIndustryType::INDUSTRYTYPE_TOWN || ScriptIndustryType::IsValidIndustryType(source_industry).
	 * @pre goal_industry == ScriptIndustryType::INDUSTRYTYPE_UNKNOWN || goal_industry == ScriptIndustryType::INDUSTRYTYPE_TOWN || ScriptIndustryType::IsValidIndustryType(goal_industry).
	 * @game @pre Valid ScriptCompanyMode active in scope.
	 * @exception ScriptError::ERR_OWNED_BY_ANOTHER_COMPANY
	 * @exception ScriptError::ERR_AREA_NOT_CLEAR
	 * @exception ScriptError::ERR_FLAT_LAND_REQUIRED
	 * @exception ScriptStation::ERR_STATION_TOO_CLOSE_TO_ANOTHER_STATION
	 * @exception ScriptStation::ERR_STATION_TOO_MANY_STATIONS
	 * @exception ScriptStation::ERR_STATION_TOO_MANY_STATIONS_IN_TOWN
	 * @return Whether the station has been/can be build or not.
	 */
	static bool BuildNewGRFRailStation(TileIndex tile, RailTrack direction, uint num_platforms, uint platform_length, StationID station_id, CargoID cargo_id, IndustryType source_industry, IndustryType goal_industry, int distance, bool source_station);

	/**
	 * Build a rail waypoint.
	 * @param tile Place to build the waypoint.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @pre IsRailTile(tile).
	 * @pre GetRailTracks(tile) == RAILTRACK_NE_SW || GetRailTracks(tile) == RAILTRACK_NW_SE.
	 * @pre IsRailTypeAvailable(GetCurrentRailType()).
	 * @game @pre Valid ScriptCompanyMode active in scope.
	 * @exception ScriptError::ERR_FLAT_LAND_REQUIRED
	 * @return Whether the rail waypoint has been/can be build or not.
	 */
	static bool BuildRailWaypoint(TileIndex tile);

	/**
	 * Remove all rail waypoint pieces within a rectangle on the map.
	 * @param tile One corner of the rectangle to clear.
	 * @param tile2 The opposite corner.
	 * @param keep_rail Whether to keep the rail after removal.
	 * @pre IsValidTile(tile).
	 * @pre IsValidTile(tile2).
	 * @game @pre Valid ScriptCompanyMode active in scope.
	 * @exception ScriptRail::ERR_UNSUITABLE_TRACK
	 * @return Whether at least one tile has been/can be cleared or not.
	 */
	static bool RemoveRailWaypointTileRectangle(TileIndex tile, TileIndex tile2, bool keep_rail);

	/**
	 * Remove all rail station platform pieces within a rectangle on the map.
	 * @param tile One corner of the rectangle to clear.
	 * @param tile2 The opposite corner.
	 * @param keep_rail Whether to keep the rail after removal.
	 * @pre IsValidTile(tile).
	 * @pre IsValidTile(tile2).
	 * @game @pre Valid ScriptCompanyMode active in scope.
	 * @exception ScriptRail::ERR_UNSUITABLE_TRACK
	 * @return Whether at least one tile has been/can be cleared or not.
	 */
	static bool RemoveRailStationTileRectangle(TileIndex tile, TileIndex tile2, bool keep_rail);

	/**
	 * Get all RailTracks on the given tile.
	 * @note A depot has no railtracks.
	 * @param tile The tile to check.
	 * @pre IsRailTile(tile).
	 * @return A bitmask of RailTrack with all RailTracks on the tile.
	 */
	static uint GetRailTracks(TileIndex tile);

	/**
	 * Build rail on the given tile.
	 * @param tile The tile to build on.
	 * @param rail_track The RailTrack to build.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @pre IsRailTypeAvailable(GetCurrentRailType()).
	 * @game @pre Valid ScriptCompanyMode active in scope.
	 * @exception ScriptError::ERR_AREA_NOT_CLEAR
	 * @exception ScriptError::ERR_LAND_SLOPED_WRONG
	 * @exception ScriptRoad::ERR_ROAD_WORKS_IN_PROGRESS
	 * @exception ScriptRail::ERR_CROSSING_ON_ONEWAY_ROAD
	 * @exception ScriptError::ERR_ALREADY_BUILT
	 * @return Whether the rail has been/can be build or not.
	 * @note You can only build a single track with this function so do not
	 *   use the values from RailTrack as bitmask.
	 */
	static bool BuildRailTrack(TileIndex tile, RailTrack rail_track);

	/**
	 * Remove rail on the given tile.
	 * @param tile The tile to remove rail from.
	 * @param rail_track The RailTrack to remove.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @pre (GetRailTracks(tile) & rail_track) != 0.
	 * @game @pre Valid ScriptCompanyMode active in scope.
	 * @exception ScriptRail::ERR_UNSUITABLE_TRACK
	 * @return Whether the rail has been/can be removed or not.
	 * @note You can only remove a single track with this function so do not
	 *   use the values from RailTrack as bitmask.
	 */
	static bool RemoveRailTrack(TileIndex tile, RailTrack rail_track);

	/**
	 * Check if a tile connects two adjacent tiles.
	 * @param from The first tile to connect.
	 * @param tile The tile that is checked.
	 * @param to The second tile to connect.
	 * @pre from != to.
	 * @pre ScriptMap::DistanceManhattan(from, tile) == 1.
	 * @pre ScriptMap::DistanceManhattan(to, tile) == 1.
	 * @return True if 'tile' connects 'from' and 'to'.
	 */
	static bool AreTilesConnected(TileIndex from, TileIndex tile, TileIndex to);

	/**
	 * Build a rail connection between two tiles.
	 * @param from The tile just before the tile to build on.
	 * @param tile The first tile to build on.
	 * @param to The tile just after the last tile to build on.
	 * @pre from != to.
	 * @pre ScriptMap::DistanceManhattan(from, tile) == 1.
	 * @pre ScriptMap::DistanceManhattan(to, tile) >= 1.
	 * @pre (abs(abs(ScriptMap::GetTileX(to) - ScriptMap::GetTileX(tile)) -
	 *          abs(ScriptMap::GetTileY(to) - ScriptMap::GetTileY(tile))) <= 1) ||
	 *      (ScriptMap::GetTileX(from) == ScriptMap::GetTileX(tile) && ScriptMap::GetTileX(tile) == ScriptMap::GetTileX(to)) ||
	 *      (ScriptMap::GetTileY(from) == ScriptMap::GetTileY(tile) && ScriptMap::GetTileY(tile) == ScriptMap::GetTileY(to)).
	 * @pre IsRailTypeAvailable(GetCurrentRailType()).
	 * @game @pre Valid ScriptCompanyMode active in scope.
	 * @exception ScriptError::ERR_AREA_NOT_CLEAR
	 * @exception ScriptError::ERR_LAND_SLOPED_WRONG
	 * @exception ScriptRail::ERR_CROSSING_ON_ONEWAY_ROAD
	 * @exception ScriptRoad::ERR_ROAD_WORKS_IN_PROGRESS
	 * @exception ScriptError::ERR_ALREADY_BUILT
	 * @note Construction will fail if an obstacle is found between the start and end tiles.
	 * @return Whether the rail has been/can be build or not.
	 */
	static bool BuildRail(TileIndex from, TileIndex tile, TileIndex to);

	/**
	 * Remove a rail connection between two tiles.
	 * @param from The tile just before the tile to remove rail from.
	 * @param tile The first tile to remove rail from.
	 * @param to The tile just after the last tile to remove rail from.
	 * @pre from != to.
	 * @pre ScriptMap::DistanceManhattan(from, tile) == 1.
	 * @pre ScriptMap::DistanceManhattan(to, tile) >= 1.
	 * @pre (abs(abs(ScriptMap::GetTileX(to) - ScriptMap::GetTileX(tile)) -
	 *          abs(ScriptMap::GetTileY(to) - ScriptMap::GetTileY(tile))) <= 1) ||
	 *      (ScriptMap::GetTileX(from) == ScriptMap::GetTileX(tile) && ScriptMap::GetTileX(tile) == ScriptMap::GetTileX(to)) ||
	 *      (ScriptMap::GetTileY(from) == ScriptMap::GetTileY(tile) && ScriptMap::GetTileY(tile) == ScriptMap::GetTileY(to)).
	 * @game @pre Valid ScriptCompanyMode active in scope.
	 * @exception ScriptRail::ERR_UNSUITABLE_TRACK
	 * @return Whether the rail has been/can be removed or not.
	 */
	static bool RemoveRail(TileIndex from, TileIndex tile, TileIndex to);

	/**
	 * Get the SignalType of the signal on a tile or SIGNALTYPE_NONE if there is no signal.
	 * @pre ScriptMap::DistanceManhattan(tile, front) == 1.
	 * @param tile The tile that might have a signal.
	 * @param front The tile in front of 'tile'.
	 * @return The SignalType of the signal on 'tile' facing to 'front'.
	 */
	static SignalType GetSignalType(TileIndex tile, TileIndex front);

	/**
	 * Build a signal on a tile.
	 * @param tile The tile to build on.
	 * @param front The tile in front of the signal.
	 * @param signal The SignalType to build.
	 * @pre ScriptMap::DistanceManhattan(tile, front) == 1.
	 * @pre IsRailTile(tile) && !IsRailStationTile(tile) && !IsRailWaypointTile(tile).
	 * @game @pre Valid ScriptCompanyMode active in scope.
	 * @exception ScriptRail::ERR_UNSUITABLE_TRACK
	 * @return Whether the signal has been/can be build or not.
	 */
	static bool BuildSignal(TileIndex tile, TileIndex front, SignalType signal);

	/**
	 * Remove a signal.
	 * @param tile The tile to remove the signal from.
	 * @param front The tile in front of the signal.
	 * @pre ScriptMap::DistanceManhattan(tile, front) == 1.
	 * @pre GetSignalType(tile, front) != SIGNALTYPE_NONE.
	 * @game @pre Valid ScriptCompanyMode active in scope.
	 * @exception ScriptRail::ERR_UNSUITABLE_TRACK
	 * @return Whether the signal has been/can be removed or not.
	 */
	static bool RemoveSignal(TileIndex tile, TileIndex front);

	/**
	 * Get the baseprice of building a rail-related object.
	 * @param railtype the railtype that is build (on)
	 * @param build_type the type of object to build
	 * @pre IsRailTypeAvailable(railtype)
	 * @return The baseprice of building the given object.
	 */
	static Money GetBuildCost(RailType railtype, BuildType build_type);

	/**
	 * Get the maximum speed of trains running on this railtype.
	 * @param railtype The railtype to get the maximum speed of.
	 * @pre IsRailTypeAvailable(railtype)
	 * @return The maximum speed trains can run on this railtype
	 *   or 0 if there is no limit.
	 * @note The speed is in OpenTTD's internal speed unit.
	 *       This is mph / 1.6, which is roughly km/h.
	 *       To get km/h multiply this number by 1.00584.
	 */
	static int32 GetMaxSpeed(RailType railtype);

	/**
	 * Get the maintenance cost factor of a railtype.
	 * @param railtype The railtype to get the maintenance factor of.
	 * @pre IsRailTypeAvailable(railtype)
	 * @return Maintenance cost factor of the railtype.
	 */
	static uint16 GetMaintenanceCostFactor(RailType railtype);
};

#endif /* SCRIPT_RAIL_HPP */
