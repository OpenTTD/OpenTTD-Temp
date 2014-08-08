/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file pathfinder_func.h General functions related to pathfinders. */

#ifndef PATHFINDER_FUNC_H
#define PATHFINDER_FUNC_H

#include "../tile_cmd.h"
#include "../waypoint_base.h"
#include "../depot_base.h"

/**
 * Calculates the tile of given station that is closest to a given tile
 * for this we assume the station is a rectangle,
 * as defined by its tile are (st->train_station)
 * @param station The station to calculate the distance to
 * @param tile The tile from where to calculate the distance
 * @param station_type the station type to get the closest tile of
 * @return The closest station tile to the given tile.
 */
static inline TileIndex CalcClosestStationTile(StationID station, TileIndex tile, StationType station_type)
{
	const BaseStation *st = BaseStation::Get(station);
	TileArea ta;
	st->GetTileArea(&ta, station_type);

	/* If the rail station is (temporarily) not present, use the station sign to drive near the station */
	if (ta.tile == INVALID_TILE) return st->xy;

	uint minx = TileX(ta.tile);  // topmost corner of station
	uint miny = TileY(ta.tile);
	uint maxx = minx + ta.w - 1; // lowermost corner of station
	uint maxy = miny + ta.h - 1;

	/* we are going the aim for the x coordinate of the closest corner
	 * but if we are between those coordinates, we will aim for our own x coordinate */
	uint x = ClampU(TileX(tile), minx, maxx);

	/* same for y coordinate, see above comment */
	uint y = ClampU(TileY(tile), miny, maxy);

	/* return the tile of our target coordinates */
	return TileXY(x, y);
}

/**
 * Calculates the tile of a depot that is closest to a given tile.
 * @param depot_id The depot to calculate the distance to.
 * @param tile The tile from where to calculate the distance.
 * @return The closest depot tile to the given tile.
 */
static inline TileIndex CalcClosestDepotTile(DepotID depot_id, TileIndex tile)
{
	assert(Depot::IsValidID(depot_id));
	const Depot *dep = Depot::Get(depot_id);

	/* If tile area is empty, use the xy tile. */
	if (dep->ta.tile == INVALID_TILE) {
		assert(dep->xy != INVALID_TILE);
		return dep->xy;
	}

	TileIndex best_tile = INVALID_TILE;
	uint best_distance = UINT_MAX;

	for (std::vector<TileIndex>::const_iterator it = dep->depot_tiles.begin(); it != dep->depot_tiles.end(); ++it) {
		TileIndex new_tile = *it;
		uint new_distance = DistanceManhattan(new_tile, tile);
		if (new_distance < best_distance) {
			best_tile = new_tile;
			best_distance = new_distance;
		}
	}

	return best_tile;
}

/**
 * Wrapper around GetTileTrackStatus() and TrackStatusToTrackdirBits(), as for
 * single tram bits GetTileTrackStatus() returns 0. The reason for this is
 * that there are no half-tile TrackBits in OpenTTD.
 * This tile, however, is a valid tile for trams, one on which they can
 * reverse safely. To "fix" this, pretend that if we are on a half-tile, we
 * are in fact on a straight tram track tile. CFollowTrackT will make sure
 * the pathfinders cannot exit on the wrong side and allows reversing on such
 * tiles.
 */
static inline TrackdirBits GetTrackdirBitsForRoad(TileIndex tile, RoadTramType rtt)
{
	TrackdirBits bits = TrackStatusToTrackdirBits(GetTileTrackStatus(tile, TRANSPORT_ROAD, rtt));

	if (rtt == RTT_TRAM && bits == TRACKDIR_BIT_NONE) {
		if (IsNormalRoadTile(tile)) {
			RoadBits rb = GetRoadBits(tile, RTT_TRAM);
			switch (rb) {
				case ROAD_NE:
				case ROAD_SW:
					bits = TRACKDIR_BIT_X_NE | TRACKDIR_BIT_X_SW;
					break;

				case ROAD_NW:
				case ROAD_SE:
					bits = TRACKDIR_BIT_Y_NW | TRACKDIR_BIT_Y_SE;
					break;

				default: break;
			}
		}
	}

	return bits;
}

#endif /* PATHFINDER_FUNC_H */
