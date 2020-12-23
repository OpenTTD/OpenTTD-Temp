/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file station_map.h Maps accessors for stations. */

#ifndef STATION_MAP_H
#define STATION_MAP_H

#include "rail_map.h"
#include "road_map.h"
#include "water_map.h"
#include "station_func.h"
#include "rail.h"
#include "road.h"

typedef byte StationGfx; ///< Index of station graphics. @see _station_display_datas

/**
 * Get StationID from a tile
 * @param t Tile to query station ID from
 * @pre IsTileType(t, MP_STATION)
 * @return Station ID of the station at \a t
 */
static inline StationID GetStationIndex(TileIndex t)
{
	assert(IsTileType(t, MP_STATION));
	return (StationID)_m[t].m2;
}


static const int GFX_DOCK_BASE_WATER_PART          =  4; ///< The offset for the water parts.
static const int GFX_TRUCK_BUS_DRIVETHROUGH_OFFSET =  4; ///< The offset for the drive through parts.

/**
 * Get the station type of this tile
 * @param t the tile to query
 * @pre IsTileType(t, MP_STATION)
 * @return the station type
 */
static inline StationType GetStationType(TileIndex t)
{
	assert(IsTileType(t, MP_STATION));
	return (StationType)GB(_me[t].m6, 3, 3);
}

/**
 * Get the road stop type of this tile
 * @param t the tile to query
 * @pre GetStationType(t) == STATION_TRUCK || GetStationType(t) == STATION_BUS
 * @return the road stop type
 */
static inline RoadStopType GetRoadStopType(TileIndex t)
{
	assert(GetStationType(t) == STATION_TRUCK || GetStationType(t) == STATION_BUS);
	return GetStationType(t) == STATION_TRUCK ? ROADSTOP_TRUCK : ROADSTOP_BUS;
}

/**
 * Get the station graphics of this tile
 * @param t the tile to query
 * @pre IsTileType(t, MP_STATION)
 * @return the station graphics
 */
static inline StationGfx GetStationGfx(TileIndex t)
{
	assert(IsTileType(t, MP_STATION));
	return _m[t].m5;
}

/**
 * Set the station graphics of this tile
 * @param t the tile to update
 * @param gfx the new graphics
 * @pre IsTileType(t, MP_STATION)
 */
static inline void SetStationGfx(TileIndex t, StationGfx gfx)
{
	assert(IsTileType(t, MP_STATION));
	_m[t].m5 = gfx;
}

/**
 * Is this station tile a rail station?
 * @param t the tile to get the information from
 * @pre IsTileType(t, MP_STATION)
 * @return true if and only if the tile is a rail station
 */
static inline bool IsRailStation(TileIndex t)
{
	return GetStationType(t) == STATION_RAIL;
}

/**
 * Is this tile a station tile and a rail station?
 * @param t the tile to get the information from
 * @return true if and only if the tile is a rail station
 */
static inline bool IsRailStationTile(TileIndex t)
{
	return IsTileType(t, MP_STATION) && IsRailStation(t);
}

/**
 * Is this station tile a rail waypoint?
 * @param t the tile to get the information from
 * @pre IsTileType(t, MP_STATION)
 * @return true if and only if the tile is a rail waypoint
 */
static inline bool IsRailWaypoint(TileIndex t)
{
	return GetStationType(t) == STATION_WAYPOINT;
}

/**
 * Is this tile a station tile and a rail waypoint?
 * @param t the tile to get the information from
 * @return true if and only if the tile is a rail waypoint
 */
static inline bool IsRailWaypointTile(TileIndex t)
{
	return IsTileType(t, MP_STATION) && IsRailWaypoint(t);
}

/**
 * Has this station tile a rail? In other words, is this station
 * tile a rail station or rail waypoint?
 * @param t the tile to check
 * @pre IsTileType(t, MP_STATION)
 * @return true if and only if the tile has rail
 */
static inline bool HasStationRail(TileIndex t)
{
	return IsRailStation(t) || IsRailWaypoint(t);
}

/**
 * Has this station tile a rail? In other words, is this station
 * tile a rail station or rail waypoint?
 * @param t the tile to check
 * @return true if and only if the tile is a station tile and has rail
 */
static inline bool HasStationTileRail(TileIndex t)
{
	return IsTileType(t, MP_STATION) && HasStationRail(t);
}

/**
 * Is this station tile an airport?
 * @param t the tile to get the information from
 * @pre IsTileType(t, MP_STATION)
 * @return true if and only if the tile is an airport
 */
static inline bool IsAirport(TileIndex t)
{
	return GetStationType(t) == STATION_AIRPORT;
}

/**
 * Is this tile a station tile and an airport tile?
 * @param t the tile to get the information from
 * @return true if and only if the tile is an airport
 */
static inline bool IsAirportTile(TileIndex t)
{
	return IsTileType(t, MP_STATION) && IsAirport(t);
}

bool IsHangar(TileIndex t);

/**
 * Is the station at \a t a truck stop?
 * @param t Tile to check
 * @pre IsTileType(t, MP_STATION)
 * @return \c true if station is a truck stop, \c false otherwise
 */
static inline bool IsTruckStop(TileIndex t)
{
	return GetStationType(t) == STATION_TRUCK;
}

/**
 * Is the station at \a t a bus stop?
 * @param t Tile to check
 * @pre IsTileType(t, MP_STATION)
 * @return \c true if station is a bus stop, \c false otherwise
 */
static inline bool IsBusStop(TileIndex t)
{
	return GetStationType(t) == STATION_BUS;
}

/**
 * Is the station at \a t a road station?
 * @param t Tile to check
 * @pre IsTileType(t, MP_STATION)
 * @return \c true if station at the tile is a bus top or a truck stop, \c false otherwise
 */
static inline bool IsRoadStop(TileIndex t)
{
	assert(IsTileType(t, MP_STATION));
	return IsTruckStop(t) || IsBusStop(t);
}

/**
 * Is tile \a t a road stop station?
 * @param t Tile to check
 * @return \c true if the tile is a station tile and a road stop
 */
static inline bool IsRoadStopTile(TileIndex t)
{
	return IsTileType(t, MP_STATION) && IsRoadStop(t);
}

/**
 * Is tile \a t a standard (non-drive through) road stop station?
 * @param t Tile to check
 * @return \c true if the tile is a station tile and a standard road stop
 */
static inline bool IsStandardRoadStopTile(TileIndex t)
{
	return IsRoadStopTile(t) && GetStationGfx(t) < GFX_TRUCK_BUS_DRIVETHROUGH_OFFSET;
}

/**
 * Is tile \a t a drive through road stop station?
 * @param t Tile to check
 * @return \c true if the tile is a station tile and a drive through road stop
 */
static inline bool IsDriveThroughStopTile(TileIndex t)
{
	return IsRoadStopTile(t) && GetStationGfx(t) >= GFX_TRUCK_BUS_DRIVETHROUGH_OFFSET;
}

/**
 * Get the station graphics of this airport tile
 * @param t the tile to query
 * @pre IsAirport(t)
 * @return the station graphics
 */
static inline StationGfx GetAirportGfx(TileIndex t)
{
	assert(IsAirport(t));
	extern StationGfx GetTranslatedAirportTileID(StationGfx gfx);
	return GetTranslatedAirportTileID(GetStationGfx(t));
}

/**
 * Gets the direction the road stop entrance points towards.
 * @param t the tile of the road stop
 * @pre IsRoadStopTile(t)
 * @return the direction of the entrance
 */
static inline DiagDirection GetRoadStopDir(TileIndex t)
{
	StationGfx gfx = GetStationGfx(t);
	assert(IsRoadStopTile(t));
	if (gfx < GFX_TRUCK_BUS_DRIVETHROUGH_OFFSET) {
		return (DiagDirection)(gfx);
	} else {
		return (DiagDirection)(gfx - GFX_TRUCK_BUS_DRIVETHROUGH_OFFSET);
	}
}

/**
 * Is tile \a t part of an oilrig?
 * @param t Tile to check
 * @pre IsTileType(t, MP_STATION)
 * @return \c true if the tile is an oilrig tile
 */
static inline bool IsOilRig(TileIndex t)
{
	return GetStationType(t) == STATION_OILRIG;
}

/**
 * Is tile \a t a dock tile?
 * @param t Tile to check
 * @pre IsTileType(t, MP_STATION)
 * @return \c true if the tile is a dock
 */
static inline bool IsDock(TileIndex t)
{
	return GetStationType(t) == STATION_DOCK;
}

/**
 * Is tile \a t a dock tile?
 * @param t Tile to check
 * @return \c true if the tile is a dock
 */
static inline bool IsDockTile(TileIndex t)
{
	return IsTileType(t, MP_STATION) && GetStationType(t) == STATION_DOCK;
}

/**
 * Is tile \a t a buoy tile?
 * @param t Tile to check
 * @pre IsTileType(t, MP_STATION)
 * @return \c true if the tile is a buoy
 */
static inline bool IsBuoy(TileIndex t)
{
	return GetStationType(t) == STATION_BUOY;
}

/**
 * Is tile \a t a buoy tile?
 * @param t Tile to check
 * @return \c true if the tile is a buoy
 */
static inline bool IsBuoyTile(TileIndex t)
{
	return IsTileType(t, MP_STATION) && IsBuoy(t);
}

/**
 * Is tile \a t an hangar tile?
 * @param t Tile to check
 * @return \c true if the tile is an hangar
 */
static inline bool IsHangarTile(TileIndex t)
{
	return IsTileType(t, MP_STATION) && IsHangar(t);
}

/**
 * Get the rail direction of a rail station.
 * @param t Tile to query
 * @pre HasStationRail(t)
 * @return The direction of the rails on tile \a t.
 */
static inline Axis GetRailStationAxis(TileIndex t)
{
	assert(HasStationRail(t));
	return HasBit(GetStationGfx(t), 0) ? AXIS_Y : AXIS_X;
}

/**
 * Get the rail track of a rail station tile.
 * @param t Tile to query
 * @pre HasStationRail(t)
 * @return The rail track of the rails on tile \a t.
 */
static inline Track GetRailStationTrack(TileIndex t)
{
	return AxisToTrack(GetRailStationAxis(t));
}

/**
 * Get the trackbits of a rail station tile.
 * @param t Tile to query
 * @pre HasStationRail(t)
 * @return The trackbits of the rails on tile \a t.
 */
static inline TrackBits GetRailStationTrackBits(TileIndex t)
{
	return AxisToTrackBits(GetRailStationAxis(t));
}

/**
 * Get the reservation state of the rail station
 * @pre HasStationRail(t)
 * @param t the station tile
 * @return reservation state
 */
static inline bool HasStationReservation(TileIndex t)
{
	assert(HasStationRail(t));
	return HasBit(_me[t].m6, 2);
}

/**
 * Set the reservation state of the rail station
 * @pre HasStationRail(t)
 * @param t the station tile
 * @param b the reservation state
 */
static inline void SetRailStationReservation(TileIndex t, bool b)
{
	assert(HasStationRail(t));
	SB(_me[t].m6, 2, 1, b ? 1 : 0);
}

/**
 * Get the reserved track bits for a waypoint
 * @pre HasStationRail(t)
 * @param t the tile
 * @return reserved track bits
 */
static inline TrackBits GetStationReservationTrackBits(TileIndex t)
{
	return HasStationReservation(t) ? GetRailStationTrackBits(t) : TRACK_BIT_NONE;
}

/**
 * Get the direction of a dock.
 * @param t Tile to query
 * @pre IsDock(t)
 * @pre \a t is the land part of the dock
 * @return The direction of the dock on tile \a t.
 */
static inline DiagDirection GetDockDirection(TileIndex t)
{
	StationGfx gfx = GetStationGfx(t);
	assert(IsDock(t) && gfx < GFX_DOCK_BASE_WATER_PART);
	return (DiagDirection)(gfx);
}

/**
 * Get the tileoffset from this tile a ship should target to get to this dock.
 * @param t Tile to query
 * @pre IsTileType(t, MP_STATION)
 * @pre IsBuoy(t) || IsOilRig(t) || IsDock(t)
 * @return The offset from this tile that should be used as destination for ships.
 */
static inline TileIndexDiffC GetDockOffset(TileIndex t)
{
	static const TileIndexDiffC buoy_offset = {0, 0};
	static const TileIndexDiffC oilrig_offset = {2, 0};
	static const TileIndexDiffC dock_offset[DIAGDIR_END] = {
		{-2,  0},
		{ 0,  2},
		{ 2,  0},
		{ 0, -2},
	};
	assert(IsTileType(t, MP_STATION));

	if (IsBuoy(t)) return buoy_offset;
	if (IsOilRig(t)) return oilrig_offset;

	assert(IsDock(t));

	return dock_offset[GetDockDirection(t)];
}

/**
 * Is there a custom rail station spec on this tile?
 * @param t Tile to query
 * @pre HasStationTileRail(t)
 * @return True if this station is part of a newgrf station.
 */
static inline bool IsCustomStationSpecIndex(TileIndex t)
{
	assert(HasStationTileRail(t));
	return _m[t].m4 != 0;
}

/**
 * Set the custom station spec for this tile.
 * @param t Tile to set the stationspec of.
 * @param specindex The new spec.
 * @pre HasStationTileRail(t)
 */
static inline void SetCustomStationSpecIndex(TileIndex t, byte specindex)
{
	assert(HasStationTileRail(t));
	_m[t].m4 = specindex;
}

/**
 * Get the custom station spec for this tile.
 * @param t Tile to query
 * @pre HasStationTileRail(t)
 * @return The custom station spec of this tile.
 */
static inline uint GetCustomStationSpecIndex(TileIndex t)
{
	assert(HasStationTileRail(t));
	return _m[t].m4;
}

/**
 * Set the random bits for a station tile.
 * @param t Tile to set random bits for.
 * @param random_bits The random bits.
 * @pre IsTileType(t, MP_STATION)
 */
static inline void SetStationTileRandomBits(TileIndex t, byte random_bits)
{
	assert(IsTileType(t, MP_STATION));
	SB(_m[t].m3, 4, 4, random_bits);
}

/**
 * Get the random bits of a station tile.
 * @param t Tile to query
 * @pre IsTileType(t, MP_STATION)
 * @return The random bits for this station tile.
 */
static inline byte GetStationTileRandomBits(TileIndex t)
{
	assert(IsTileType(t, MP_STATION));
	return GB(_m[t].m3, 4, 4);
}

/**
 * Make the given tile a station tile.
 * @param t the tile to make a station tile
 * @param o the owner of the station
 * @param sid the station to which this tile belongs
 * @param st the type this station tile
 * @param section the StationGfx to be used for this tile
 * @param wc The water class of the station
 */
static inline void MakeStation(TileIndex t, Owner o, StationID sid, StationType st, byte section, WaterClass wc = WATER_CLASS_INVALID)
{
	SetTileType(t, MP_STATION);
	SetTileOwner(t, o);
	SetWaterClass(t, wc);
	SetDockingTile(t, false);
	_m[t].m2 = sid;
	_m[t].m3 = 0;
	_m[t].m4 = 0;
	_m[t].m5 = section;
	SB(_me[t].m6, 2, 1, 0);
	SB(_me[t].m6, 3, 3, st);
	_me[t].m7 = 0;
	_me[t].m8 = 0;
}

/**
 * Make the given tile a rail station tile.
 * @param t the tile to make a rail station tile
 * @param o the owner of the station
 * @param sid the station to which this tile belongs
 * @param a the axis of this tile
 * @param section the StationGfx to be used for this tile
 * @param rt the railtype of this tile
 */
static inline void MakeRailStation(TileIndex t, Owner o, StationID sid, Axis a, byte section, RailType rt)
{
	MakeStation(t, o, sid, STATION_RAIL, section + a);
	SetRailType(t, rt);
	SetRailStationReservation(t, false);
}

/**
 * Make the given tile a rail waypoint tile.
 * @param t the tile to make a rail waypoint
 * @param o the owner of the waypoint
 * @param sid the waypoint to which this tile belongs
 * @param a the axis of this tile
 * @param section the StationGfx to be used for this tile
 * @param rt the railtype of this tile
 */
static inline void MakeRailWaypoint(TileIndex t, Owner o, StationID sid, Axis a, byte section, RailType rt)
{
	MakeStation(t, o, sid, STATION_WAYPOINT, section + a);
	SetRailType(t, rt);
	SetRailStationReservation(t, false);
}

/**
 * Make the given tile a roadstop tile.
 * @param t the tile to make a roadstop
 * @param o the owner of the roadstop
 * @param sid the station to which this tile belongs
 * @param rst the type of roadstop to make this tile
 * @param road_rt the road roadtype on this tile
 * @param tram_rt the tram roadtype on this tile
 * @param d the direction of the roadstop
 */
static inline void MakeRoadStop(TileIndex t, Owner o, StationID sid, RoadStopType rst, RoadType road_rt, RoadType tram_rt, DiagDirection d)
{
	MakeStation(t, o, sid, (rst == ROADSTOP_BUS ? STATION_BUS : STATION_TRUCK), d);
	SetRoadTypes(t, road_rt, tram_rt);
	SetRoadOwner(t, RTT_ROAD, o);
	SetRoadOwner(t, RTT_TRAM, o);
}

/**
 * Make the given tile a drivethrough roadstop tile.
 * @param t the tile to make a roadstop
 * @param station the owner of the roadstop
 * @param road the owner of the road
 * @param tram the owner of the tram
 * @param sid the station to which this tile belongs
 * @param rst the type of roadstop to make this tile
 * @param road_rt the road roadtype on this tile
 * @param tram_rt the tram roadtype on this tile
 * @param a the direction of the roadstop
 */
static inline void MakeDriveThroughRoadStop(TileIndex t, Owner station, Owner road, Owner tram, StationID sid, RoadStopType rst, RoadType road_rt, RoadType tram_rt, Axis a)
{
	MakeStation(t, station, sid, (rst == ROADSTOP_BUS ? STATION_BUS : STATION_TRUCK), GFX_TRUCK_BUS_DRIVETHROUGH_OFFSET + a);
	SetRoadTypes(t, road_rt, tram_rt);
	SetRoadOwner(t, RTT_ROAD, road);
	SetRoadOwner(t, RTT_TRAM, tram);
}

/**
 * Make the given tile an airport tile.
 * @param t the tile to make a airport
 * @param o the owner of the airport
 * @param sid the station to which this tile belongs
 * @param section the StationGfx to be used for this tile
 * @param wc the type of water on this tile
 */
static inline void MakeAirport(TileIndex t, Owner o, StationID sid, byte section, WaterClass wc)
{
	MakeStation(t, o, sid, STATION_AIRPORT, section, wc);
}

/**
 * Make the given tile a buoy tile.
 * @param t the tile to make a buoy
 * @param sid the station to which this tile belongs
 * @param wc the type of water on this tile
 */
static inline void MakeBuoy(TileIndex t, StationID sid, WaterClass wc)
{
	/* Make the owner of the buoy tile the same as the current owner of the
	 * water tile. In this way, we can reset the owner of the water to its
	 * original state when the buoy gets removed. */
	MakeStation(t, GetTileOwner(t), sid, STATION_BUOY, 0, wc);
}

/**
 * Make the given tile a dock tile.
 * @param t the tile to make a dock
 * @param o the owner of the dock
 * @param sid the station to which this tile belongs
 * @param d the direction of the dock
 * @param wc the type of water on this tile
 */
static inline void MakeDock(TileIndex t, Owner o, StationID sid, DiagDirection d, WaterClass wc)
{
	MakeStation(t, o, sid, STATION_DOCK, d);
	MakeStation(t + TileOffsByDiagDir(d), o, sid, STATION_DOCK, GFX_DOCK_BASE_WATER_PART + DiagDirToAxis(d), wc);
}

/**
 * Make the given tile an oilrig tile.
 * @param t the tile to make an oilrig
 * @param sid the station to which this tile belongs
 * @param wc the type of water on this tile
 */
static inline void MakeOilrig(TileIndex t, StationID sid, WaterClass wc)
{
	MakeStation(t, OWNER_NONE, sid, STATION_OILRIG, 0, wc);
}

#endif /* STATION_MAP_H */
