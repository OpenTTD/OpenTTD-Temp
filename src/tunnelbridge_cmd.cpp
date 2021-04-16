/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file tunnelbridge_cmd.cpp
 * This file deals with tunnels and bridges (non-gui stuff)
 * @todo separate this file into two
 */

#include "stdafx.h"
#include "newgrf_object.h"
#include "viewport_func.h"
#include "cmd_helper.h"
#include "command_func.h"
#include "town.h"
#include "train.h"
#include "ship.h"
#include "roadveh.h"
#include "pathfinder/yapf/yapf_cache.h"
#include "newgrf_sound.h"
#include "autoslope.h"
#include "tunnelbridge_map.h"
#include "strings_func.h"
#include "date_func.h"
#include "clear_func.h"
#include "vehicle_func.h"
#include "sound_func.h"
#include "tunnelbridge.h"
#include "cheat_type.h"
#include "elrail_func.h"
#include "pbs.h"
#include "company_base.h"
#include "newgrf_railtype.h"
#include "newgrf_roadtype.h"
#include "object_base.h"
#include "water.h"
#include "company_gui.h"
#include "station_func.h"

#include "table/strings.h"
#include "table/bridge_land.h"

#include "safeguards.h"

BridgeSpec _bridge[MAX_BRIDGES]; ///< The specification of all bridges.
TileIndex _build_tunnel_endtile; ///< The end of a tunnel; as hidden return from the tunnel build command for GUI purposes.

/** Z position of the bridge sprites relative to bridge height (downwards) */
static const int BRIDGE_Z_START = 3;


/**
 * Mark bridge tiles dirty.
 * Note: The bridge does not need to exist, everything is passed via parameters.
 * @param begin Start tile.
 * @param end End tile.
 * @param direction Direction from \a begin to \a end.
 * @param bridge_height Bridge height level.
 */
void MarkBridgeDirty(TileIndex begin, TileIndex end, DiagDirection direction, uint bridge_height)
{
	TileIndexDiff delta = TileOffsByDiagDir(direction);
	for (TileIndex t = begin; t != end; t += delta) {
		MarkTileDirtyByTile(t, bridge_height - TileHeight(t));
	}
	MarkTileDirtyByTile(end);
}

/**
 * Mark bridge tiles dirty.
 * @param tile Bridge head.
 */
void MarkBridgeDirty(TileIndex tile)
{
	MarkBridgeDirty(tile, GetOtherTunnelBridgeEnd(tile), GetTunnelBridgeDirection(tile), GetBridgeHeight(tile));
}

/** Reset the data been eventually changed by the grf loaded. */
void ResetBridges()
{
	/* First, free sprite table data */
	for (BridgeType i = 0; i < MAX_BRIDGES; i++) {
		if (_bridge[i].sprite_table != nullptr) {
			for (BridgePieces j = BRIDGE_PIECE_NORTH; j < BRIDGE_PIECE_INVALID; j++) free(_bridge[i].sprite_table[j]);
			free(_bridge[i].sprite_table);
		}
	}

	/* Then, wipe out current bridges */
	memset(&_bridge, 0, sizeof(_bridge));
	/* And finally, reinstall default data */
	memcpy(&_bridge, &_orig_bridge, sizeof(_orig_bridge));
}

/**
 * Calculate the price factor for building a long bridge.
 * Basically the cost delta is 1,1, 1, 2,2, 3,3,3, 4,4,4,4, 5,5,5,5,5, 6,6,6,6,6,6,  7,7,7,7,7,7,7,  8,8,8,8,8,8,8,8,
 * @param length Length of the bridge.
 * @return Price factor for the bridge.
 */
int CalcBridgeLenCostFactor(int length)
{
	if (length < 2) return length;

	length -= 2;
	int sum = 2;
	for (int delta = 1;; delta++) {
		for (int count = 0; count < delta; count++) {
			if (length == 0) return sum;
			sum += delta;
			length--;
		}
	}
}

/**
 * Get the foundation for a bridge.
 * @param tileh The slope to build the bridge on.
 * @param axis The axis of the bridge entrance.
 * @return The foundation required.
 */
Foundation GetBridgeFoundation(Slope tileh, Axis axis)
{
	if (tileh == SLOPE_FLAT ||
			((tileh == SLOPE_NE || tileh == SLOPE_SW) && axis == AXIS_X) ||
			((tileh == SLOPE_NW || tileh == SLOPE_SE) && axis == AXIS_Y)) return FOUNDATION_NONE;

	return (HasSlopeHighestCorner(tileh) ? InclinedFoundation(axis) : FlatteningFoundation(tileh));
}

/**
 * Determines if the track on a bridge ramp is flat or goes up/down.
 *
 * @param tileh Slope of the tile under the bridge head
 * @param axis Orientation of bridge
 * @return true iff the track is flat.
 */
bool HasBridgeFlatRamp(Slope tileh, Axis axis)
{
	ApplyFoundationToSlope(GetBridgeFoundation(tileh, axis), &tileh);
	/* If the foundation slope is flat the bridge has a non-flat ramp and vice versa. */
	return (tileh != SLOPE_FLAT);
}

static inline const PalSpriteID *GetBridgeSpriteTable(int index, BridgePieces table)
{
	const BridgeSpec *bridge = GetBridgeSpec(index);
	assert(table < BRIDGE_PIECE_INVALID);
	if (bridge->sprite_table == nullptr || bridge->sprite_table[table] == nullptr) {
		return _bridge_sprite_table[index][table];
	} else {
		return bridge->sprite_table[table];
	}
}


/**
 * Determines the foundation for the bridge head, and tests if the resulting slope is valid.
 *
 * @param bridge_piece Direction of the bridge head.
 * @param axis Axis of the bridge
 * @param tileh Slope of the tile under the north bridge head; returns slope on top of foundation
 * @param z TileZ corresponding to tileh, gets modified as well
 * @return Error or cost for bridge foundation
 */
static CommandCost CheckBridgeSlope(BridgePieces bridge_piece, Axis axis, Slope *tileh, int *z)
{
	assert(bridge_piece == BRIDGE_PIECE_NORTH || bridge_piece == BRIDGE_PIECE_SOUTH);

	Foundation f = GetBridgeFoundation(*tileh, axis);
	*z += ApplyFoundationToSlope(f, tileh);

	Slope valid_inclined;
	if (bridge_piece == BRIDGE_PIECE_NORTH) {
		valid_inclined = (axis == AXIS_X ? SLOPE_NE : SLOPE_NW);
	} else {
		valid_inclined = (axis == AXIS_X ? SLOPE_SW : SLOPE_SE);
	}
	if ((*tileh != SLOPE_FLAT) && (*tileh != valid_inclined)) return CMD_ERROR;

	if (f == FOUNDATION_NONE) return CommandCost();

	return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_FOUNDATION]);
}

/**
 * Is a bridge of the specified type and length available?
 * @param bridge_type Wanted type of bridge.
 * @param bridge_len  Wanted length of the bridge.
 * @param flags       Type of operation.
 * @return A succeeded (the requested bridge is available) or failed (it cannot be built) command.
 */
CommandCost CheckBridgeAvailability(BridgeType bridge_type, uint bridge_len, DoCommandFlag flags)
{
	if (flags & DC_QUERY_COST) {
		if (bridge_len <= _settings_game.construction.max_bridge_length) return CommandCost();
		return_cmd_error(STR_ERROR_BRIDGE_TOO_LONG);
	}

	if (bridge_type >= MAX_BRIDGES) return CMD_ERROR;

	const BridgeSpec *b = GetBridgeSpec(bridge_type);
	if (b->avail_year > _cur_year) return CMD_ERROR;

	uint max = std::min(b->max_length, _settings_game.construction.max_bridge_length);

	if (b->min_length > bridge_len) return CMD_ERROR;
	if (bridge_len <= max) return CommandCost();
	return_cmd_error(STR_ERROR_BRIDGE_TOO_LONG);
}

/**
 * Calculate the base cost of clearing a tunnel/bridge per tile.
 * @param tile Start tile of the tunnel/bridge.
 * @return How much clearing this tunnel/bridge costs per tile.
 */
static Money TunnelBridgeClearCost(TileIndex tile, Price base_price)
{
	Money base_cost = _price[base_price];

	/* Add the cost of the transport that is on the tunnel/bridge. */
	switch (GetTunnelBridgeTransportType(tile)) {
		case TRANSPORT_ROAD: {
			RoadType road_rt = GetRoadTypeRoad(tile);
			RoadType tram_rt = GetRoadTypeTram(tile);

			if (road_rt != INVALID_ROADTYPE) {
				base_cost += 2 * RoadClearCost(road_rt);
			}
			if (tram_rt != INVALID_ROADTYPE) {
				base_cost += 2 * RoadClearCost(tram_rt);
			}
		} break;

		case TRANSPORT_RAIL: base_cost += RailClearCost(GetRailType(tile)); break;
		/* Aquaducts have their own clear price. */
		case TRANSPORT_WATER: base_cost = _price[PR_CLEAR_AQUEDUCT]; break;
		default: break;
	}

	return base_cost;
}

/**
 * Build a Bridge
 * @param end_tile end tile
 * @param flags type of operation
 * @param p1 packed start tile coords (~ dx)
 * @param p2 various bitstuffed elements
 * - p2 = (bit  0- 7) - bridge type (hi bh)
 * - p2 = (bit  8-13) - rail type or road types.
 * - p2 = (bit 15-16) - transport type.
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdBuildBridge(TileIndex end_tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	CompanyID company = _current_company;

	RailType railtype = INVALID_RAILTYPE;
	RoadType roadtype = INVALID_ROADTYPE;

	/* unpack parameters */
	BridgeType bridge_type = GB(p2, 0, 8);

	if (!IsValidTile(p1)) return_cmd_error(STR_ERROR_BRIDGE_THROUGH_MAP_BORDER);

	TransportType transport_type = Extract<TransportType, 15, 2>(p2);

	/* type of bridge */
	switch (transport_type) {
		case TRANSPORT_ROAD:
			roadtype = Extract<RoadType, 8, 6>(p2);
			if (!ValParamRoadType(roadtype)) return CMD_ERROR;
			break;

		case TRANSPORT_RAIL:
			railtype = Extract<RailType, 8, 6>(p2);
			if (!ValParamRailtype(railtype)) return CMD_ERROR;
			break;

		case TRANSPORT_WATER:
			break;

		default:
			/* Airports don't have bridges. */
			return CMD_ERROR;
	}
	TileIndex tile_start = p1;
	TileIndex tile_end = end_tile;

	if (company == OWNER_DEITY) {
		if (transport_type != TRANSPORT_ROAD) return CMD_ERROR;
		const Town *town = CalcClosestTownFromTile(tile_start);

		company = OWNER_TOWN;

		/* If we are not within a town, we are not owned by the town */
		if (town == nullptr || DistanceSquare(tile_start, town->xy) > town->cache.squared_town_zone_radius[HZB_TOWN_EDGE]) {
			company = OWNER_NONE;
		}
	}

	if (tile_start == tile_end) {
		return_cmd_error(STR_ERROR_CAN_T_START_AND_END_ON);
	}

	Axis direction;
	if (TileX(tile_start) == TileX(tile_end)) {
		direction = AXIS_Y;
	} else if (TileY(tile_start) == TileY(tile_end)) {
		direction = AXIS_X;
	} else {
		return_cmd_error(STR_ERROR_START_AND_END_MUST_BE_IN);
	}

	if (tile_end < tile_start) Swap(tile_start, tile_end);

	uint bridge_len = GetTunnelBridgeLength(tile_start, tile_end);
	if (transport_type != TRANSPORT_WATER) {
		/* set and test bridge length, availability */
		CommandCost ret = CheckBridgeAvailability(bridge_type, bridge_len, flags);
		if (ret.Failed()) return ret;
	} else {
		if (bridge_len > _settings_game.construction.max_bridge_length) return_cmd_error(STR_ERROR_BRIDGE_TOO_LONG);
	}
	bridge_len += 2; // begin and end tiles/ramps

	int z_start;
	int z_end;
	Slope tileh_start = GetTileSlope(tile_start, &z_start);
	Slope tileh_end = GetTileSlope(tile_end, &z_end);
	bool pbs_reservation = false;

	CommandCost terraform_cost_north = CheckBridgeSlope(BRIDGE_PIECE_NORTH, direction, &tileh_start, &z_start);
	CommandCost terraform_cost_south = CheckBridgeSlope(BRIDGE_PIECE_SOUTH, direction, &tileh_end,   &z_end);

	/* Aqueducts can't be built of flat land. */
	if (transport_type == TRANSPORT_WATER && (tileh_start == SLOPE_FLAT || tileh_end == SLOPE_FLAT)) return_cmd_error(STR_ERROR_LAND_SLOPED_IN_WRONG_DIRECTION);
	if (z_start != z_end) return_cmd_error(STR_ERROR_BRIDGEHEADS_NOT_SAME_HEIGHT);

	CommandCost cost(EXPENSES_CONSTRUCTION);
	Owner owner;
	bool is_new_owner;
	RoadType road_rt = INVALID_ROADTYPE;
	RoadType tram_rt = INVALID_ROADTYPE;
	if (IsBridgeTile(tile_start) && IsBridgeTile(tile_end) &&
			GetOtherBridgeEnd(tile_start) == tile_end &&
			GetTunnelBridgeTransportType(tile_start) == transport_type) {
		/* Replace a current bridge. */

		switch (transport_type) {
			case TRANSPORT_RAIL:
				/* Keep the reservation, the path stays valid. */
				pbs_reservation = HasTunnelBridgeReservation(tile_start);
				break;

			case TRANSPORT_ROAD:
				/* Do not remove road types when upgrading a bridge */
				road_rt = GetRoadTypeRoad(tile_start);
				tram_rt = GetRoadTypeTram(tile_start);
				break;

			default: break;
		}

		/* If this is a railway bridge, make sure the railtypes match. */
		if (transport_type == TRANSPORT_RAIL && GetRailType(tile_start) != railtype) {
			return_cmd_error(STR_ERROR_MUST_DEMOLISH_BRIDGE_FIRST);
		}

		/* If this is a road bridge, make sure the roadtype matches. */
		if (transport_type == TRANSPORT_ROAD) {
			RoadType existing_rt = RoadTypeIsRoad(roadtype) ? road_rt : tram_rt;
			if (existing_rt != roadtype && existing_rt != INVALID_ROADTYPE) {
				return_cmd_error(STR_ERROR_MUST_DEMOLISH_BRIDGE_FIRST);
			}
		}

		/* Do not replace town bridges with lower speed bridges, unless in scenario editor. */
		if (!(flags & DC_QUERY_COST) && IsTileOwner(tile_start, OWNER_TOWN) &&
				GetBridgeSpec(bridge_type)->speed < GetBridgeSpec(GetBridgeType(tile_start))->speed &&
				_game_mode != GM_EDITOR) {
			Town *t = ClosestTownFromTile(tile_start, UINT_MAX);

			if (t == nullptr) {
				return CMD_ERROR;
			} else {
				SetDParam(0, t->index);
				return_cmd_error(STR_ERROR_LOCAL_AUTHORITY_REFUSES_TO_ALLOW_THIS);
			}
		}

		/* Do not replace the bridge with the same bridge type. */
		if (!(flags & DC_QUERY_COST) && (bridge_type == GetBridgeType(tile_start)) && (transport_type != TRANSPORT_ROAD || road_rt == roadtype || tram_rt == roadtype)) {
			return_cmd_error(STR_ERROR_ALREADY_BUILT);
		}

		/* Do not allow replacing another company's bridges. */
		if (!IsTileOwner(tile_start, company) && !IsTileOwner(tile_start, OWNER_TOWN) && !IsTileOwner(tile_start, OWNER_NONE)) {
			return_cmd_error(STR_ERROR_AREA_IS_OWNED_BY_ANOTHER);
		}

		/* The cost of clearing the current bridge. */
		cost.AddCost(bridge_len * TunnelBridgeClearCost(tile_start, PR_CLEAR_BRIDGE));
		owner = GetTileOwner(tile_start);

		/* If bridge belonged to bankrupt company, it has a new owner now */
		is_new_owner = (owner == OWNER_NONE);
		if (is_new_owner) owner = company;
	} else {
		/* Build a new bridge. */

		bool allow_on_slopes = (_settings_game.construction.build_on_slopes && transport_type != TRANSPORT_WATER);

		/* Try and clear the start landscape */
		CommandCost ret = DoCommand(tile_start, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
		if (ret.Failed()) return ret;
		cost = ret;

		if (terraform_cost_north.Failed() || (terraform_cost_north.GetCost() != 0 && !allow_on_slopes)) return_cmd_error(STR_ERROR_LAND_SLOPED_IN_WRONG_DIRECTION);
		cost.AddCost(terraform_cost_north);

		/* Try and clear the end landscape */
		ret = DoCommand(tile_end, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
		if (ret.Failed()) return ret;
		cost.AddCost(ret);

		/* false - end tile slope check */
		if (terraform_cost_south.Failed() || (terraform_cost_south.GetCost() != 0 && !allow_on_slopes)) return_cmd_error(STR_ERROR_LAND_SLOPED_IN_WRONG_DIRECTION);
		cost.AddCost(terraform_cost_south);

		const TileIndex heads[] = {tile_start, tile_end};
		for (int i = 0; i < 2; i++) {
			if (IsBridgeAbove(heads[i])) {
				if (GetBridgeAxis(heads[i]) == direction || GetBridgeAxis(heads[i]) == AXIS_END) return_cmd_error(STR_ERROR_MUST_DEMOLISH_BRIDGE_FIRST);

				TileIndex north_head_x = GetNorthernBridgeEndAtAxis(heads[i], AXIS_X);
				TileIndex north_head_y = GetNorthernBridgeEndAtAxis(heads[i], AXIS_Y);

				if ((north_head_x != INVALID_TILE && z_start + 1 == GetBridgeHeight(north_head_x))
					|| (north_head_y != INVALID_TILE && z_start + 1 == GetBridgeHeight(north_head_y))) {
					return_cmd_error(STR_ERROR_MUST_DEMOLISH_BRIDGE_FIRST);
				}
			}
		}

		TileIndexDiff delta = (direction == AXIS_X ? TileDiffXY(1, 0) : TileDiffXY(0, 1));
		for (TileIndex tile = tile_start + delta; tile != tile_end; tile += delta) {
			if (GetTileMaxZ(tile) > z_start) return_cmd_error(STR_ERROR_BRIDGE_TOO_LOW_FOR_TERRAIN);

			if (z_start >= (GetTileZ(tile) + _settings_game.construction.max_bridge_height)) {
				/*
				 * Disallow too high bridges.
				 * Properly rendering a map where very high bridges (might) exist is expensive.
				 * See http://www.tt-forums.net/viewtopic.php?f=33&t=40844&start=980#p1131762
				 * for a detailed discussion. z_start here is one heightlevel below the bridge level.
				 */
				return_cmd_error(STR_ERROR_BRIDGE_TOO_HIGH_FOR_TERRAIN);
			}

			if (IsBridgeAbove(tile)) {
				TileIndex existing_bridge_head = GetNorthernBridgeEndAtAxis(tile, direction == AXIS_X ? AXIS_Y : AXIS_X);
				int existing_bridge_z_start = GetBridgeHeight(existing_bridge_head);

				if (z_start + 1 == existing_bridge_z_start) {
					/* Disallow crossing bridges at same elevation */
					return_cmd_error(STR_ERROR_MUST_DEMOLISH_BRIDGE_FIRST);
				}
			}

			switch (GetTileType(tile)) {
				case MP_WATER:
					if (!IsWater(tile) && !IsCoast(tile)) goto not_valid_below;
					break;

				case MP_RAILWAY:
					if (!IsPlainRail(tile)) goto not_valid_below;
					break;

				case MP_ROAD:
					if (IsRoadDepot(tile)) goto not_valid_below;
					break;

				case MP_TUNNELBRIDGE:
					if (IsTunnel(tile)) break;
					if (direction == DiagDirToAxis(GetTunnelBridgeDirection(tile))) goto not_valid_below;
					if (z_start < GetBridgeHeight(tile)) goto not_valid_below;
					break;

				case MP_OBJECT: {
					const ObjectSpec *spec = ObjectSpec::GetByTile(tile);
					if ((spec->flags & OBJECT_FLAG_ALLOW_UNDER_BRIDGE) == 0) goto not_valid_below;
					if (GetTileMaxZ(tile) + spec->height > z_start) goto not_valid_below;
					break;
				}

				case MP_CLEAR:
					break;

				default:
	not_valid_below:;
					/* try and clear the middle landscape */
					ret = DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
					if (ret.Failed()) return ret;
					cost.AddCost(ret);
					break;
			}

			if (flags & DC_EXEC) {
				/* We do this here because when replacing a bridge with another
				 * type calling SetBridgeMiddle isn't needed. After all, the
				 * tile already has the has_bridge_above bits set. */
				SetBridgeMiddle(tile, direction);
			}
		}

		owner = company;
		is_new_owner = true;
	}

	bool hasroad = road_rt != INVALID_ROADTYPE;
	bool hastram = tram_rt != INVALID_ROADTYPE;
	if (transport_type == TRANSPORT_ROAD) {
		if (RoadTypeIsRoad(roadtype)) road_rt = roadtype;
		if (RoadTypeIsTram(roadtype)) tram_rt = roadtype;
	}

	/* do the drill? */
	if (flags & DC_EXEC) {
		DiagDirection dir = AxisToDiagDir(direction);

		Company *c = Company::GetIfValid(company);
		switch (transport_type) {
			case TRANSPORT_RAIL:
				/* Add to company infrastructure count if required. */
				if (is_new_owner && c != nullptr) c->infrastructure.rail[railtype] += bridge_len * TUNNELBRIDGE_TRACKBIT_FACTOR;
				MakeRailBridgeRamp(tile_start, owner, bridge_type, dir,                 railtype);
				MakeRailBridgeRamp(tile_end,   owner, bridge_type, ReverseDiagDir(dir), railtype);
				SetTunnelBridgeReservation(tile_start, pbs_reservation);
				SetTunnelBridgeReservation(tile_end,   pbs_reservation);
				break;

			case TRANSPORT_ROAD: {
				if (is_new_owner) {
					/* Also give unowned present roadtypes to new owner */
					if (hasroad && GetRoadOwner(tile_start, RTT_ROAD) == OWNER_NONE) hasroad = false;
					if (hastram && GetRoadOwner(tile_start, RTT_TRAM) == OWNER_NONE) hastram = false;
				}
				if (c != nullptr) {
					/* Add all new road types to the company infrastructure counter. */
					if (!hasroad && road_rt != INVALID_ROADTYPE) {
						/* A full diagonal road tile has two road bits. */
						c->infrastructure.road[road_rt] += bridge_len * 2 * TUNNELBRIDGE_TRACKBIT_FACTOR;
					}
					if (!hastram && tram_rt != INVALID_ROADTYPE) {
						/* A full diagonal road tile has two road bits. */
						c->infrastructure.road[tram_rt] += bridge_len * 2 * TUNNELBRIDGE_TRACKBIT_FACTOR;
					}
				}
				Owner owner_road = hasroad ? GetRoadOwner(tile_start, RTT_ROAD) : company;
				Owner owner_tram = hastram ? GetRoadOwner(tile_start, RTT_TRAM) : company;
				MakeRoadBridgeRamp(tile_start, owner, owner_road, owner_tram, bridge_type, dir, road_rt, tram_rt);
				MakeRoadBridgeRamp(tile_end,   owner, owner_road, owner_tram, bridge_type, ReverseDiagDir(dir), road_rt, tram_rt);
				break;
			}

			case TRANSPORT_WATER:
				if (is_new_owner && c != nullptr) c->infrastructure.water += bridge_len * TUNNELBRIDGE_TRACKBIT_FACTOR;
				MakeAqueductBridgeRamp(tile_start, owner, dir);
				MakeAqueductBridgeRamp(tile_end,   owner, ReverseDiagDir(dir));
				CheckForDockingTile(tile_start);
				CheckForDockingTile(tile_end);
				break;

			default:
				NOT_REACHED();
		}

		/* Mark all tiles dirty */
		MarkBridgeDirty(tile_start, tile_end, AxisToDiagDir(direction), z_start);
		DirtyCompanyInfrastructureWindows(company);
	}

	if ((flags & DC_EXEC) && transport_type == TRANSPORT_RAIL) {
		Track track = AxisToTrack(direction);
		AddSideToSignalBuffer(tile_start, INVALID_DIAGDIR, company);
		YapfNotifyTrackLayoutChange(tile_start, track);
	}

	/* Human players that build bridges get a selection to choose from (DC_QUERY_COST)
	 * It's unnecessary to execute this command every time for every bridge.
	 * So it is done only for humans and cost is computed in bridge_gui.cpp.
	 * For (non-spectated) AI, Towns this has to be of course calculated. */
	Company *c = Company::GetIfValid(company);
	if (!(flags & DC_QUERY_COST) || (c != nullptr && c->is_ai && company != _local_company)) {
		switch (transport_type) {
			case TRANSPORT_ROAD:
				if (road_rt != INVALID_ROADTYPE) {
					cost.AddCost(bridge_len * 2 * RoadBuildCost(road_rt));
				}
				if (tram_rt != INVALID_ROADTYPE) {
					cost.AddCost(bridge_len * 2 * RoadBuildCost(tram_rt));
				}
				break;

			case TRANSPORT_RAIL: cost.AddCost(bridge_len * RailBuildCost(railtype)); break;
			default: break;
		}

		if (c != nullptr) bridge_len = CalcBridgeLenCostFactor(bridge_len);

		if (transport_type != TRANSPORT_WATER) {
			cost.AddCost((int64)bridge_len * _price[PR_BUILD_BRIDGE] * GetBridgeSpec(bridge_type)->price >> 8);
		} else {
			/* Aqueducts use a separate base cost. */
			cost.AddCost((int64)bridge_len * _price[PR_BUILD_AQUEDUCT]);
		}

	}

	return cost;
}


/**
 * Build Tunnel.
 * @param start_tile start tile of tunnel
 * @param flags type of operation
 * @param p1 bit 0-5 railtype or roadtype
 *           bit 8-9 transport type
 * @param p2 unused
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdBuildTunnel(TileIndex start_tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	CompanyID company = _current_company;

	TransportType transport_type = Extract<TransportType, 8, 2>(p1);
	RailType railtype = INVALID_RAILTYPE;
	RoadType roadtype = INVALID_ROADTYPE;
	_build_tunnel_endtile = 0;
	switch (transport_type) {
		case TRANSPORT_RAIL:
			railtype = Extract<RailType, 0, 6>(p1);
			if (!ValParamRailtype(railtype)) return CMD_ERROR;
			break;

		case TRANSPORT_ROAD:
			roadtype = Extract<RoadType, 0, 6>(p1);
			if (!ValParamRoadType(roadtype)) return CMD_ERROR;
			break;

		default: return CMD_ERROR;
	}

	if (company == OWNER_DEITY) {
		if (transport_type != TRANSPORT_ROAD) return CMD_ERROR;
		const Town *town = CalcClosestTownFromTile(start_tile);

		company = OWNER_TOWN;

		/* If we are not within a town, we are not owned by the town */
		if (town == nullptr || DistanceSquare(start_tile, town->xy) > town->cache.squared_town_zone_radius[HZB_TOWN_EDGE]) {
			company = OWNER_NONE;
		}
	}

	int start_z;
	int end_z;
	Slope start_tileh = GetTileSlope(start_tile, &start_z);
	DiagDirection direction = GetInclinedSlopeDirection(start_tileh);
	if (direction == INVALID_DIAGDIR) return_cmd_error(STR_ERROR_SITE_UNSUITABLE_FOR_TUNNEL);

	if (HasTileWaterGround(start_tile)) return_cmd_error(STR_ERROR_CAN_T_BUILD_ON_WATER);

	CommandCost ret = DoCommand(start_tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
	if (ret.Failed()) return ret;

	/* XXX - do NOT change 'ret' in the loop, as it is used as the price
	 * for the clearing of the entrance of the tunnel. Assigning it to
	 * cost before the loop will yield different costs depending on start-
	 * position, because of increased-cost-by-length: 'cost += cost >> 3' */

	TileIndexDiff delta = TileOffsByDiagDir(direction);
	DiagDirection tunnel_in_way_dir;
	if (DiagDirToAxis(direction) == AXIS_Y) {
		tunnel_in_way_dir = (TileX(start_tile) < (MapMaxX() / 2)) ? DIAGDIR_SW : DIAGDIR_NE;
	} else {
		tunnel_in_way_dir = (TileY(start_tile) < (MapMaxX() / 2)) ? DIAGDIR_SE : DIAGDIR_NW;
	}

	TileIndex end_tile = start_tile;

	/* Tile shift coefficient. Will decrease for very long tunnels to avoid exponential growth of price*/
	int tiles_coef = 3;
	/* Number of tiles from start of tunnel */
	int tiles = 0;
	/* Number of tiles at which the cost increase coefficient per tile is halved */
	int tiles_bump = 25;

	CommandCost cost(EXPENSES_CONSTRUCTION);
	Slope end_tileh;
	for (;;) {
		end_tile += delta;
		if (!IsValidTile(end_tile)) return_cmd_error(STR_ERROR_TUNNEL_THROUGH_MAP_BORDER);
		end_tileh = GetTileSlope(end_tile, &end_z);

		if (start_z == end_z) break;

		if (!_cheats.crossing_tunnels.value && IsTunnelInWayDir(end_tile, start_z, tunnel_in_way_dir)) {
			return_cmd_error(STR_ERROR_ANOTHER_TUNNEL_IN_THE_WAY);
		}

		tiles++;
		if (tiles == tiles_bump) {
			tiles_coef++;
			tiles_bump *= 2;
		}

		cost.AddCost(_price[PR_BUILD_TUNNEL]);
		cost.AddCost(cost.GetCost() >> tiles_coef); // add a multiplier for longer tunnels
	}

	/* Add the cost of the entrance */
	cost.AddCost(_price[PR_BUILD_TUNNEL]);
	cost.AddCost(ret);

	/* if the command fails from here on we want the end tile to be highlighted */
	_build_tunnel_endtile = end_tile;

	if (tiles > _settings_game.construction.max_tunnel_length) return_cmd_error(STR_ERROR_TUNNEL_TOO_LONG);

	if (HasTileWaterGround(end_tile)) return_cmd_error(STR_ERROR_CAN_T_BUILD_ON_WATER);

	/* Clear the tile in any case */
	ret = DoCommand(end_tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
	if (ret.Failed()) return_cmd_error(STR_ERROR_UNABLE_TO_EXCAVATE_LAND);
	cost.AddCost(ret);

	/* slope of end tile must be complementary to the slope of the start tile */
	if (end_tileh != ComplementSlope(start_tileh)) {
		/* Mark the tile as already cleared for the terraform command.
		 * Do this for all tiles (like trees), not only objects. */
		ClearedObjectArea *coa = FindClearedObject(end_tile);
		if (coa == nullptr) {
			coa = &_cleared_object_areas.emplace_back(ClearedObjectArea{ end_tile, TileArea(end_tile, 1, 1) });
		}

		/* Hide the tile from the terraforming command */
		TileIndex old_first_tile = coa->first_tile;
		coa->first_tile = INVALID_TILE;

		/* CMD_TERRAFORM_LAND may append further items to _cleared_object_areas,
		 * however it will never erase or re-order existing items.
		 * _cleared_object_areas is a value-type self-resizing vector, therefore appending items
		 * may result in a backing-store re-allocation, which would invalidate the coa pointer.
		 * The index of the coa pointer into the _cleared_object_areas vector remains valid,
		 * and can be used safely after the CMD_TERRAFORM_LAND operation.
		 * Deliberately clear the coa pointer to avoid leaving dangling pointers which could
		 * inadvertently be dereferenced.
		 */
		ClearedObjectArea *begin = _cleared_object_areas.data();
		assert(coa >= begin && coa < begin + _cleared_object_areas.size());
		size_t coa_index = coa - begin;
		assert(coa_index < UINT_MAX); // more than 2**32 cleared areas would be a bug in itself
		coa = nullptr;

		ret = DoCommand(end_tile, end_tileh & start_tileh, 0, flags, CMD_TERRAFORM_LAND);
		_cleared_object_areas[(uint)coa_index].first_tile = old_first_tile;
		if (ret.Failed()) return_cmd_error(STR_ERROR_UNABLE_TO_EXCAVATE_LAND);
		cost.AddCost(ret);
	}
	cost.AddCost(_price[PR_BUILD_TUNNEL]);

	/* Pay for the rail/road in the tunnel including entrances */
	switch (transport_type) {
		case TRANSPORT_ROAD: cost.AddCost((tiles + 2) * RoadBuildCost(roadtype) * 2); break;
		case TRANSPORT_RAIL: cost.AddCost((tiles + 2) * RailBuildCost(railtype)); break;
		default: NOT_REACHED();
	}

	if (flags & DC_EXEC) {
		Company *c = Company::GetIfValid(company);
		uint num_pieces = (tiles + 2) * TUNNELBRIDGE_TRACKBIT_FACTOR;
		if (transport_type == TRANSPORT_RAIL) {
			if (c != nullptr) c->infrastructure.rail[railtype] += num_pieces;
			MakeRailTunnel(start_tile, company, direction,                 railtype);
			MakeRailTunnel(end_tile,   company, ReverseDiagDir(direction), railtype);
			AddSideToSignalBuffer(start_tile, INVALID_DIAGDIR, company);
			YapfNotifyTrackLayoutChange(start_tile, DiagDirToDiagTrack(direction));
		} else {
			if (c != nullptr) c->infrastructure.road[roadtype] += num_pieces * 2; // A full diagonal road has two road bits.
			RoadType road_rt = RoadTypeIsRoad(roadtype) ? roadtype : INVALID_ROADTYPE;
			RoadType tram_rt = RoadTypeIsTram(roadtype) ? roadtype : INVALID_ROADTYPE;
			MakeRoadTunnel(start_tile, company, direction,                 road_rt, tram_rt);
			MakeRoadTunnel(end_tile,   company, ReverseDiagDir(direction), road_rt, tram_rt);
		}
		DirtyCompanyInfrastructureWindows(company);
	}

	return cost;
}


/**
 * Are we allowed to remove the tunnel or bridge at \a tile?
 * @param tile End point of the tunnel or bridge.
 * @return A succeeded command if the tunnel or bridge may be removed, a failed command otherwise.
 */
static inline CommandCost CheckAllowRemoveTunnelBridge(TileIndex tile)
{
	/* Floods can remove anything as well as the scenario editor */
	if (_current_company == OWNER_WATER || _game_mode == GM_EDITOR) return CommandCost();

	switch (GetTunnelBridgeTransportType(tile)) {
		case TRANSPORT_ROAD: {
			RoadType road_rt = GetRoadTypeRoad(tile);
			RoadType tram_rt = GetRoadTypeTram(tile);
			Owner road_owner = _current_company;
			Owner tram_owner = _current_company;

			if (road_rt != INVALID_ROADTYPE) road_owner = GetRoadOwner(tile, RTT_ROAD);
			if (tram_rt != INVALID_ROADTYPE) tram_owner = GetRoadOwner(tile, RTT_TRAM);

			/* We can remove unowned road and if the town allows it */
			if (road_owner == OWNER_TOWN && _current_company != OWNER_TOWN && !(_settings_game.construction.extra_dynamite || _cheats.magic_bulldozer.value)) {
				/* Town does not allow */
				return CheckTileOwnership(tile);
			}
			if (road_owner == OWNER_NONE || road_owner == OWNER_TOWN) road_owner = _current_company;
			if (tram_owner == OWNER_NONE) tram_owner = _current_company;

			CommandCost ret = CheckOwnership(road_owner, tile);
			if (ret.Succeeded()) ret = CheckOwnership(tram_owner, tile);
			return ret;
		}

		case TRANSPORT_RAIL:
			return CheckOwnership(GetTileOwner(tile));

		case TRANSPORT_WATER: {
			/* Always allow to remove aqueducts without owner. */
			Owner aqueduct_owner = GetTileOwner(tile);
			if (aqueduct_owner == OWNER_NONE) aqueduct_owner = _current_company;
			return CheckOwnership(aqueduct_owner);
		}

		default: NOT_REACHED();
	}
}

/**
 * Remove a tunnel from the game, update town rating, etc.
 * @param tile Tile containing one of the endpoints of the tunnel.
 * @param flags Command flags.
 * @return Succeeded or failed command.
 */
static CommandCost DoClearTunnel(TileIndex tile, DoCommandFlag flags)
{
	CommandCost ret = CheckAllowRemoveTunnelBridge(tile);
	if (ret.Failed()) return ret;

	TileIndex endtile = GetOtherTunnelEnd(tile);

	ret = TunnelBridgeIsFree(tile, endtile);
	if (ret.Failed()) return ret;

	_build_tunnel_endtile = endtile;

	Town *t = nullptr;
	if (IsTileOwner(tile, OWNER_TOWN) && _game_mode != GM_EDITOR) {
		t = ClosestTownFromTile(tile, UINT_MAX); // town penalty rating

		/* Check if you are allowed to remove the tunnel owned by a town
		 * Removal depends on difficulty settings */
		CommandCost ret = CheckforTownRating(flags, t, TUNNELBRIDGE_REMOVE);
		if (ret.Failed()) return ret;
	}

	/* checks if the owner is town then decrease town rating by RATING_TUNNEL_BRIDGE_DOWN_STEP until
	 * you have a "Poor" (0) town rating */
	if (IsTileOwner(tile, OWNER_TOWN) && _game_mode != GM_EDITOR) {
		ChangeTownRating(t, RATING_TUNNEL_BRIDGE_DOWN_STEP, RATING_TUNNEL_BRIDGE_MINIMUM, flags);
	}

	Money base_cost = TunnelBridgeClearCost(tile, PR_CLEAR_TUNNEL);
	uint len = GetTunnelBridgeLength(tile, endtile) + 2; // Don't forget the end tiles.

	if (flags & DC_EXEC) {
		if (GetTunnelBridgeTransportType(tile) == TRANSPORT_RAIL) {
			/* We first need to request values before calling DoClearSquare */
			DiagDirection dir = GetTunnelBridgeDirection(tile);
			Track track = DiagDirToDiagTrack(dir);
			Owner owner = GetTileOwner(tile);

			Train *v = nullptr;
			if (HasTunnelBridgeReservation(tile)) {
				v = GetTrainForReservation(tile, track);
				if (v != nullptr) FreeTrainTrackReservation(v);
			}

			if (Company::IsValidID(owner)) {
				Company::Get(owner)->infrastructure.rail[GetRailType(tile)] -= len * TUNNELBRIDGE_TRACKBIT_FACTOR;
				DirtyCompanyInfrastructureWindows(owner);
			}

			DoClearSquare(tile);
			DoClearSquare(endtile);

			/* cannot use INVALID_DIAGDIR for signal update because the tunnel doesn't exist anymore */
			AddSideToSignalBuffer(tile,    ReverseDiagDir(dir), owner);
			AddSideToSignalBuffer(endtile, dir,                 owner);

			YapfNotifyTrackLayoutChange(tile,    track);
			YapfNotifyTrackLayoutChange(endtile, track);

			if (v != nullptr) TryPathReserve(v);
		} else {
			/* A full diagonal road tile has two road bits. */
			UpdateCompanyRoadInfrastructure(GetRoadTypeRoad(tile), GetRoadOwner(tile, RTT_ROAD), -(int)(len * 2 * TUNNELBRIDGE_TRACKBIT_FACTOR));
			UpdateCompanyRoadInfrastructure(GetRoadTypeTram(tile), GetRoadOwner(tile, RTT_TRAM), -(int)(len * 2 * TUNNELBRIDGE_TRACKBIT_FACTOR));

			DoClearSquare(tile);
			DoClearSquare(endtile);
		}
	}

	return CommandCost(EXPENSES_CONSTRUCTION, len * base_cost);
}


/**
 * Remove a bridge from the game, update town rating, etc.
 * @param tile Tile containing one of the endpoints of the bridge.
 * @param flags Command flags.
 * @return Succeeded or failed command.
 */
static CommandCost DoClearBridge(TileIndex tile, DoCommandFlag flags)
{
	CommandCost ret = CheckAllowRemoveTunnelBridge(tile);
	if (ret.Failed()) return ret;

	TileIndex endtile = GetOtherBridgeEnd(tile);

	ret = TunnelBridgeIsFree(tile, endtile);
	if (ret.Failed()) return ret;

	DiagDirection direction = GetTunnelBridgeDirection(tile);
	TileIndexDiff delta = TileOffsByDiagDir(direction);

	Town *t = nullptr;
	if (IsTileOwner(tile, OWNER_TOWN) && _game_mode != GM_EDITOR) {
		t = ClosestTownFromTile(tile, UINT_MAX); // town penalty rating

		/* Check if you are allowed to remove the bridge owned by a town
		 * Removal depends on difficulty settings */
		CommandCost ret = CheckforTownRating(flags, t, TUNNELBRIDGE_REMOVE);
		if (ret.Failed()) return ret;
	}

	/* checks if the owner is town then decrease town rating by RATING_TUNNEL_BRIDGE_DOWN_STEP until
	 * you have a "Poor" (0) town rating */
	if (IsTileOwner(tile, OWNER_TOWN) && _game_mode != GM_EDITOR) {
		ChangeTownRating(t, RATING_TUNNEL_BRIDGE_DOWN_STEP, RATING_TUNNEL_BRIDGE_MINIMUM, flags);
	}

	Money base_cost = TunnelBridgeClearCost(tile, PR_CLEAR_BRIDGE);
	uint len = GetTunnelBridgeLength(tile, endtile) + 2; // Don't forget the end tiles.

	if (flags & DC_EXEC) {
		/* read this value before actual removal of bridge */
		bool rail = GetTunnelBridgeTransportType(tile) == TRANSPORT_RAIL;
		Owner owner = GetTileOwner(tile);
		int height = GetBridgeHeight(tile);
		Train *v = nullptr;

		if (rail && HasTunnelBridgeReservation(tile)) {
			v = GetTrainForReservation(tile, DiagDirToDiagTrack(direction));
			if (v != nullptr) FreeTrainTrackReservation(v);
		}

		bool removetile = false;
		bool removeendtile = false;

		/* Update company infrastructure counts. */
		if (rail) {
			if (Company::IsValidID(owner)) Company::Get(owner)->infrastructure.rail[GetRailType(tile)] -= len * TUNNELBRIDGE_TRACKBIT_FACTOR;
		} else if (GetTunnelBridgeTransportType(tile) == TRANSPORT_ROAD) {
			/* A full diagonal road tile has two road bits. */
			UpdateCompanyRoadInfrastructure(GetRoadTypeRoad(tile), GetRoadOwner(tile, RTT_ROAD), -(int)(len * 2 * TUNNELBRIDGE_TRACKBIT_FACTOR));
			UpdateCompanyRoadInfrastructure(GetRoadTypeTram(tile), GetRoadOwner(tile, RTT_TRAM), -(int)(len * 2 * TUNNELBRIDGE_TRACKBIT_FACTOR));
		} else { // Aqueduct
			if (Company::IsValidID(owner)) Company::Get(owner)->infrastructure.water -= len * TUNNELBRIDGE_TRACKBIT_FACTOR;
			removetile    = IsDockingTile(tile);
			removeendtile = IsDockingTile(endtile);
		}
		DirtyCompanyInfrastructureWindows(owner);

		DoClearSquare(tile);
		DoClearSquare(endtile);

		if (removetile)    RemoveDockingTile(tile);
		if (removeendtile) RemoveDockingTile(endtile);
		for (TileIndex c = tile + delta; c != endtile; c += delta) {
			/* do not let trees appear from 'nowhere' after removing bridge */
			if (IsNormalRoadTile(c) && GetRoadside(c) == ROADSIDE_TREES) {
				int minz = GetTileMaxZ(c) + 3;
				if (height < minz) SetRoadside(c, ROADSIDE_PAVED);
			}
			ClearSingleBridgeMiddle(c, DiagDirToAxis(direction));
			MarkTileDirtyByTile(c, height - TileHeight(c));
		}

		if (rail) {
			/* cannot use INVALID_DIAGDIR for signal update because the bridge doesn't exist anymore */
			AddSideToSignalBuffer(tile,    ReverseDiagDir(direction), owner);
			AddSideToSignalBuffer(endtile, direction,                 owner);

			Track track = DiagDirToDiagTrack(direction);
			YapfNotifyTrackLayoutChange(tile,    track);
			YapfNotifyTrackLayoutChange(endtile, track);

			if (v != nullptr) TryPathReserve(v, true);
		}
	}

	return CommandCost(EXPENSES_CONSTRUCTION, len * base_cost);
}

/**
 * Remove a tunnel or a bridge from the game.
 * @param tile Tile containing one of the endpoints.
 * @param flags Command flags.
 * @return Succeeded or failed command.
 */
static CommandCost ClearTile_TunnelBridge(TileIndex tile, DoCommandFlag flags)
{
	if (IsTunnel(tile)) {
		if (flags & DC_AUTO) return_cmd_error(STR_ERROR_MUST_DEMOLISH_TUNNEL_FIRST);
		return DoClearTunnel(tile, flags);
	} else { // IsBridge(tile)
		if (flags & DC_AUTO) return_cmd_error(STR_ERROR_MUST_DEMOLISH_BRIDGE_FIRST);
		return DoClearBridge(tile, flags);
	}
}

/**
 * Draw a single pillar sprite.
 * @param psid      Pillarsprite
 * @param x         Pillar X
 * @param y         Pillar Y
 * @param z         Pillar Z
 * @param w         Bounding box size in X direction
 * @param h         Bounding box size in Y direction
 * @param subsprite Optional subsprite for drawing halfpillars
 */
static inline void DrawPillar(const PalSpriteID *psid, int x, int y, int z, int w, int h, const SubSprite *subsprite)
{
	static const int PILLAR_Z_OFFSET = TILE_HEIGHT - BRIDGE_Z_START; ///< Start offset of pillar wrt. bridge (downwards)
	AddSortableSpriteToDraw(psid->sprite, psid->pal, x, y, w, h, BB_HEIGHT_UNDER_BRIDGE - PILLAR_Z_OFFSET, z, IsTransparencySet(TO_BRIDGES), 0, 0, -PILLAR_Z_OFFSET, subsprite);
}

/**
 * Draw two bridge pillars (north and south).
 * @param z_bottom Bottom Z
 * @param z_top    Top Z
 * @param psid     Pillarsprite
 * @param x        Pillar X
 * @param y        Pillar Y
 * @param w        Bounding box size in X direction
 * @param h        Bounding box size in Y direction
 * @return Reached Z at the bottom
 */
static int DrawPillarColumn(int z_bottom, int z_top, const PalSpriteID *psid, int x, int y, int w, int h)
{
	int cur_z;
	for (cur_z = z_top; cur_z >= z_bottom; cur_z -= TILE_HEIGHT) {
		DrawPillar(psid, x, y, cur_z, w, h, nullptr);
	}
	return cur_z;
}

/**
 * Draws the pillars under high bridges.
 *
 * @param psid Image and palette of a bridge pillar.
 * @param ti #TileInfo of current bridge-middle-tile.
 * @param axis Orientation of bridge.
 * @param drawfarpillar Whether to draw the pillar at the back
 * @param x Sprite X position of front pillar.
 * @param y Sprite Y position of front pillar.
 * @param z_bridge Absolute height of bridge bottom.
 */
static void DrawBridgePillars(const PalSpriteID *psid, const TileInfo *ti, Axis axis, bool drawfarpillar, int x, int y, int z_bridge)
{
	static const int bounding_box_size[2]  = {16, 2}; ///< bounding box size of pillars along bridge direction
	static const int back_pillar_offset[2] = { 0, 9}; ///< sprite position offset of back facing pillar

	static const int INF = 1000; ///< big number compared to sprite size
	static const SubSprite half_pillar_sub_sprite[2][2] = {
		{ {  -14, -INF, INF, INF }, { -INF, -INF, -15, INF } }, // X axis, north and south
		{ { -INF, -INF,  15, INF }, {   16, -INF, INF, INF } }, // Y axis, north and south
	};

	if (psid->sprite == 0) return;

	/* Determine ground height under pillars */
	DiagDirection south_dir = AxisToDiagDir(axis);
	int z_front_north = ti->z;
	int z_back_north = ti->z;
	int z_front_south = ti->z;
	int z_back_south = ti->z;
	GetSlopePixelZOnEdge(ti->tileh, south_dir, &z_front_south, &z_back_south);
	GetSlopePixelZOnEdge(ti->tileh, ReverseDiagDir(south_dir), &z_front_north, &z_back_north);

	/* Shared height of pillars */
	int z_front = std::max(z_front_north, z_front_south);
	int z_back = std::max(z_back_north, z_back_south);

	/* x and y size of bounding-box of pillars */
	int w = bounding_box_size[axis];
	int h = bounding_box_size[OtherAxis(axis)];
	/* sprite position of back facing pillar */
	int x_back = x - back_pillar_offset[axis];
	int y_back = y - back_pillar_offset[OtherAxis(axis)];

	/* Draw front pillars */
	int bottom_z = DrawPillarColumn(z_front, z_bridge, psid, x, y, w, h);
	if (z_front_north < z_front) DrawPillar(psid, x, y, bottom_z, w, h, &half_pillar_sub_sprite[axis][0]);
	if (z_front_south < z_front) DrawPillar(psid, x, y, bottom_z, w, h, &half_pillar_sub_sprite[axis][1]);

	/* Draw back pillars, skip top two parts, which are hidden by the bridge */
	int z_bridge_back = z_bridge - 2 * (int)TILE_HEIGHT;
	if (drawfarpillar && (z_back_north <= z_bridge_back || z_back_south <= z_bridge_back)) {
		bottom_z = DrawPillarColumn(z_back, z_bridge_back, psid, x_back, y_back, w, h);
		if (z_back_north < z_back) DrawPillar(psid, x_back, y_back, bottom_z, w, h, &half_pillar_sub_sprite[axis][0]);
		if (z_back_south < z_back) DrawPillar(psid, x_back, y_back, bottom_z, w, h, &half_pillar_sub_sprite[axis][1]);
	}
}

/**
 * Retrieve the sprites required for catenary on a road/tram bridge.
 * @param rti              RoadTypeInfo for the road or tram type to get catenary for
 * @param head_tile        Bridge head tile with roadtype information
 * @param offset           Sprite offset identifying flat to sloped bridge tiles
 * @param head             Are we drawing bridge head?
 * @param[out] spr_back    Back catenary sprite to use
 * @param[out] spr_front   Front catenary sprite to use
 */
static void GetBridgeRoadCatenary(const RoadTypeInfo *rti, TileIndex head_tile, int offset, bool head, SpriteID &spr_back, SpriteID &spr_front)
{
	static const SpriteID back_offsets[6]  = { 95,  96,  99, 102, 100, 101 };
	static const SpriteID front_offsets[6] = { 97,  98, 103, 106, 104, 105 };

	/* Simplified from DrawRoadTypeCatenary() to remove all the special cases required for regular ground road */
	spr_back = GetCustomRoadSprite(rti, head_tile, ROTSG_CATENARY_BACK, head ? TCX_NORMAL : TCX_ON_BRIDGE);
	spr_front = GetCustomRoadSprite(rti, head_tile, ROTSG_CATENARY_FRONT, head ? TCX_NORMAL : TCX_ON_BRIDGE);
	if (spr_back == 0 && spr_front == 0) {
		spr_back = SPR_TRAMWAY_BASE + back_offsets[offset];
		spr_front = SPR_TRAMWAY_BASE + front_offsets[offset];
	} else {
		if (spr_back != 0) spr_back += 23 + offset;
		if (spr_front != 0) spr_front += 23 + offset;
	}
}

/**
 * Draws the road and trambits over an already drawn (lower end) of a bridge.
 * @param head_tile    bridge head tile with roadtype information
 * @param x            the x of the bridge
 * @param y            the y of the bridge
 * @param z            the z of the bridge
 * @param offset       sprite offset identifying flat to sloped bridge tiles
 * @param head         are we drawing bridge head?
 */
static void DrawBridgeRoadBits(TileIndex head_tile, int x, int y, int z, int offset, bool head)
{
	RoadType road_rt = GetRoadTypeRoad(head_tile);
	RoadType tram_rt = GetRoadTypeTram(head_tile);
	const RoadTypeInfo *road_rti = road_rt == INVALID_ROADTYPE ? nullptr : GetRoadTypeInfo(road_rt);
	const RoadTypeInfo *tram_rti = tram_rt == INVALID_ROADTYPE ? nullptr : GetRoadTypeInfo(tram_rt);

	SpriteID seq_back[4] = { 0 };
	bool trans_back[4] = { false };
	SpriteID seq_front[4] = { 0 };
	bool trans_front[4] = { false };

	static const SpriteID overlay_offsets[6] = {   0,   1,  11,  12,  13,  14 };
	if (head || !IsInvisibilitySet(TO_BRIDGES)) {
		/* Road underlay takes precedence over tram */
		trans_back[0] = !head && IsTransparencySet(TO_BRIDGES);
		if (road_rti != nullptr) {
			if (road_rti->UsesOverlay()) {
				seq_back[0] = GetCustomRoadSprite(road_rti, head_tile, ROTSG_BRIDGE, head ? TCX_NORMAL : TCX_ON_BRIDGE) + offset;
			}
		} else if (tram_rti != nullptr) {
			if (tram_rti->UsesOverlay()) {
				seq_back[0] = GetCustomRoadSprite(tram_rti, head_tile, ROTSG_BRIDGE, head ? TCX_NORMAL : TCX_ON_BRIDGE) + offset;
			} else {
				seq_back[0] = SPR_TRAMWAY_BRIDGE + offset;
			}
		}

		/* Draw road overlay */
		trans_back[1] = !head && IsTransparencySet(TO_BRIDGES);
		if (road_rti != nullptr) {
			if (road_rti->UsesOverlay()) {
				seq_back[1] = GetCustomRoadSprite(road_rti, head_tile, ROTSG_OVERLAY, head ? TCX_NORMAL : TCX_ON_BRIDGE);
				if (seq_back[1] != 0) seq_back[1] += overlay_offsets[offset];
			}
		}

		/* Draw tram overlay */
		trans_back[2] = !head && IsTransparencySet(TO_BRIDGES);
		if (tram_rti != nullptr) {
			if (tram_rti->UsesOverlay()) {
				seq_back[2] = GetCustomRoadSprite(tram_rti, head_tile, ROTSG_OVERLAY, head ? TCX_NORMAL : TCX_ON_BRIDGE);
				if (seq_back[2] != 0) seq_back[2] += overlay_offsets[offset];
			} else if (road_rti != nullptr) {
				seq_back[2] = SPR_TRAMWAY_OVERLAY + overlay_offsets[offset];
			}
		}

		/* Road catenary takes precedence over tram */
		trans_back[3] = IsTransparencySet(TO_CATENARY);
		trans_front[0] = IsTransparencySet(TO_CATENARY);
		if (road_rti != nullptr && HasRoadCatenaryDrawn(road_rt)) {
			GetBridgeRoadCatenary(road_rti, head_tile, offset, head, seq_back[3], seq_front[0]);
		} else if (tram_rti != nullptr && HasRoadCatenaryDrawn(tram_rt)) {
			GetBridgeRoadCatenary(tram_rti, head_tile, offset, head, seq_back[3], seq_front[0]);
		}
	}

	static const uint size_x[6] = {  1, 16, 16,  1, 16,  1 };
	static const uint size_y[6] = { 16,  1,  1, 16,  1, 16 };
	static const uint front_bb_offset_x[6] = { 15,  0,  0, 15,  0, 15 };
	static const uint front_bb_offset_y[6] = {  0, 15, 15,  0, 15,  0 };

	/* The sprites under the vehicles are drawn as SpriteCombine. StartSpriteCombine() has already been called
	 * The bounding boxes here are the same as for bridge front/roof */
	for (uint i = 0; i < lengthof(seq_back); ++i) {
		if (seq_back[i] != 0) {
			AddSortableSpriteToDraw(seq_back[i], PAL_NONE,
				x, y, size_x[offset], size_y[offset], 0x28, z,
				trans_back[i]);
		}
	}

	/* Start a new SpriteCombine for the front part */
	EndSpriteCombine();
	StartSpriteCombine();

	for (uint i = 0; i < lengthof(seq_front); ++i) {
		if (seq_front[i] != 0) {
			AddSortableSpriteToDraw(seq_front[i], PAL_NONE,
				x, y, size_x[offset] + front_bb_offset_x[offset], size_y[offset] + front_bb_offset_y[offset], 0x28, z,
				trans_front[i],
				front_bb_offset_x[offset], front_bb_offset_y[offset]);
		}
	}
}

/**
 * Draws a tunnel or bridge tile.
 * For tunnels, this is rather simple, as you only need to draw the entrance.
 * Bridges are a bit more complex. base_offset is where the sprite selection comes into play
 * and it works a bit like a bitmask.<p> For bridge heads:
 * @param ti TileInfo of the structure to draw
 * <ul><li>Bit 0: direction</li>
 * <li>Bit 1: northern or southern heads</li>
 * <li>Bit 2: Set if the bridge head is sloped</li>
 * <li>Bit 3 and more: Railtype Specific subset</li>
 * </ul>
 * Please note that in this code, "roads" are treated as railtype 1, whilst the real railtypes are 0, 2 and 3
 */
static void DrawTile_TunnelBridge(TileInfo *ti)
{
	TransportType transport_type = GetTunnelBridgeTransportType(ti->tile);
	DiagDirection tunnelbridge_direction = GetTunnelBridgeDirection(ti->tile);

	if (IsTunnel(ti->tile)) {
		/* Front view of tunnel bounding boxes:
		 *
		 *   122223  <- BB_Z_SEPARATOR
		 *   1    3
		 *   1    3                1,3 = empty helper BB
		 *   1    3                  2 = SpriteCombine of tunnel-roof and catenary (tram & elrail)
		 *
		 */

		static const int _tunnel_BB[4][12] = {
			/*  tunnel-roof  |  Z-separator  | tram-catenary
			 * w  h  bb_x bb_y| x   y   w   h |bb_x bb_y w h */
			{  1,  0, -15, -14,  0, 15, 16,  1, 0, 1, 16, 15 }, // NE
			{  0,  1, -14, -15, 15,  0,  1, 16, 1, 0, 15, 16 }, // SE
			{  1,  0, -15, -14,  0, 15, 16,  1, 0, 1, 16, 15 }, // SW
			{  0,  1, -14, -15, 15,  0,  1, 16, 1, 0, 15, 16 }, // NW
		};
		const int *BB_data = _tunnel_BB[tunnelbridge_direction];

		bool catenary = false;

		SpriteID image;
		SpriteID railtype_overlay = 0;
		if (transport_type == TRANSPORT_RAIL) {
			const RailtypeInfo *rti = GetRailTypeInfo(GetRailType(ti->tile));
			image = rti->base_sprites.tunnel;
			if (rti->UsesOverlay()) {
				/* Check if the railtype has custom tunnel portals. */
				railtype_overlay = GetCustomRailSprite(rti, ti->tile, RTSG_TUNNEL_PORTAL);
				if (railtype_overlay != 0) image = SPR_RAILTYPE_TUNNEL_BASE; // Draw blank grass tunnel base.
			}
		} else {
			image = SPR_TUNNEL_ENTRY_REAR_ROAD;
		}

		if (HasTunnelBridgeSnowOrDesert(ti->tile)) image += railtype_overlay != 0 ? 8 : 32;

		image += tunnelbridge_direction * 2;
		DrawGroundSprite(image, PAL_NONE);

		if (transport_type == TRANSPORT_ROAD) {
			RoadType road_rt = GetRoadTypeRoad(ti->tile);
			RoadType tram_rt = GetRoadTypeTram(ti->tile);
			const RoadTypeInfo *road_rti = road_rt == INVALID_ROADTYPE ? nullptr : GetRoadTypeInfo(road_rt);
			const RoadTypeInfo *tram_rti = tram_rt == INVALID_ROADTYPE ? nullptr : GetRoadTypeInfo(tram_rt);
			uint sprite_offset = DiagDirToAxis(tunnelbridge_direction) == AXIS_X ? 1 : 0;

			DrawRoadOverlays(ti, PAL_NONE, road_rti, tram_rti, sprite_offset, sprite_offset);

			/* Road catenary takes precedence over tram */
			SpriteID catenary_sprite_base = 0;
			if (road_rti != nullptr && HasRoadCatenaryDrawn(road_rt)) {
				catenary_sprite_base = GetCustomRoadSprite(road_rti, ti->tile, ROTSG_CATENARY_FRONT);
				if (catenary_sprite_base == 0) {
					catenary_sprite_base = SPR_TRAMWAY_TUNNEL_WIRES;
				} else {
					catenary_sprite_base += 19;
				}
			} else if (tram_rti != nullptr && HasRoadCatenaryDrawn(tram_rt)) {
				catenary_sprite_base = GetCustomRoadSprite(tram_rti, ti->tile, ROTSG_CATENARY_FRONT);
				if (catenary_sprite_base == 0) {
					catenary_sprite_base = SPR_TRAMWAY_TUNNEL_WIRES;
				} else {
					catenary_sprite_base += 19;
				}
			}

			if (catenary_sprite_base != 0) {
				catenary = true;
				StartSpriteCombine();
				AddSortableSpriteToDraw(catenary_sprite_base + tunnelbridge_direction, PAL_NONE, ti->x, ti->y, BB_data[10], BB_data[11], TILE_HEIGHT, ti->z, IsTransparencySet(TO_CATENARY), BB_data[8], BB_data[9], BB_Z_SEPARATOR);
			}
		} else {
			const RailtypeInfo *rti = GetRailTypeInfo(GetRailType(ti->tile));
			if (rti->UsesOverlay()) {
				SpriteID surface = GetCustomRailSprite(rti, ti->tile, RTSG_TUNNEL);
				if (surface != 0) DrawGroundSprite(surface + tunnelbridge_direction, PAL_NONE);
			}

			/* PBS debugging, draw reserved tracks darker */
			if (_game_mode != GM_MENU && _settings_client.gui.show_track_reservation && HasTunnelBridgeReservation(ti->tile)) {
				if (rti->UsesOverlay()) {
					SpriteID overlay = GetCustomRailSprite(rti, ti->tile, RTSG_OVERLAY);
					DrawGroundSprite(overlay + RTO_X + DiagDirToAxis(tunnelbridge_direction), PALETTE_CRASH);
				} else {
					DrawGroundSprite(DiagDirToAxis(tunnelbridge_direction) == AXIS_X ? rti->base_sprites.single_x : rti->base_sprites.single_y, PALETTE_CRASH);
				}
			}

			if (HasRailCatenaryDrawn(GetRailType(ti->tile))) {
				/* Maybe draw pylons on the entry side */
				DrawRailCatenary(ti);

				catenary = true;
				StartSpriteCombine();
				/* Draw wire above the ramp */
				DrawRailCatenaryOnTunnel(ti);
			}
		}

		if (railtype_overlay != 0 && !catenary) StartSpriteCombine();

		AddSortableSpriteToDraw(image + 1, PAL_NONE, ti->x + TILE_SIZE - 1, ti->y + TILE_SIZE - 1, BB_data[0], BB_data[1], TILE_HEIGHT, ti->z, false, BB_data[2], BB_data[3], BB_Z_SEPARATOR);
		/* Draw railtype tunnel portal overlay if defined. */
		if (railtype_overlay != 0) AddSortableSpriteToDraw(railtype_overlay + tunnelbridge_direction, PAL_NONE, ti->x + TILE_SIZE - 1, ti->y + TILE_SIZE - 1, BB_data[0], BB_data[1], TILE_HEIGHT, ti->z, false, BB_data[2], BB_data[3], BB_Z_SEPARATOR);

		if (catenary || railtype_overlay != 0) EndSpriteCombine();

		/* Add helper BB for sprite sorting that separates the tunnel from things beside of it. */
		AddSortableSpriteToDraw(SPR_EMPTY_BOUNDING_BOX, PAL_NONE, ti->x,              ti->y,              BB_data[6], BB_data[7], TILE_HEIGHT, ti->z);
		AddSortableSpriteToDraw(SPR_EMPTY_BOUNDING_BOX, PAL_NONE, ti->x + BB_data[4], ti->y + BB_data[5], BB_data[6], BB_data[7], TILE_HEIGHT, ti->z);

		DrawBridgeMiddle(ti);
	} else { // IsBridge(ti->tile)
		const PalSpriteID *psid;
		int base_offset;
		bool ice = HasTunnelBridgeSnowOrDesert(ti->tile);

		if (transport_type == TRANSPORT_RAIL) {
			base_offset = GetRailTypeInfo(GetRailType(ti->tile))->bridge_offset;
			assert(base_offset != 8); // This one is used for roads
		} else {
			base_offset = 8;
		}

		/* as the lower 3 bits are used for other stuff, make sure they are clear */
		assert( (base_offset & 0x07) == 0x00);

		DrawFoundation(ti, GetBridgeFoundation(ti->tileh, DiagDirToAxis(tunnelbridge_direction)));

		/* HACK Wizardry to convert the bridge ramp direction into a sprite offset */
		base_offset += (6 - tunnelbridge_direction) % 4;

		/* Table number BRIDGE_PIECE_HEAD always refers to the bridge heads for any bridge type */
		if (transport_type != TRANSPORT_WATER) {
			if (ti->tileh == SLOPE_FLAT) base_offset += 4; // sloped bridge head
			psid = &GetBridgeSpriteTable(GetBridgeType(ti->tile), BRIDGE_PIECE_HEAD)[base_offset];
		} else {
			psid = _aqueduct_sprites + base_offset;
		}

		if (!ice) {
			TileIndex next = ti->tile + TileOffsByDiagDir(tunnelbridge_direction);
			if (ti->tileh != SLOPE_FLAT && ti->z == 0 && HasTileWaterClass(next) && GetWaterClass(next) == WATER_CLASS_SEA) {
				DrawShoreTile(ti->tileh);
			} else {
				DrawClearLandTile(ti, 3);
			}
		} else {
			DrawGroundSprite(SPR_FLAT_SNOW_DESERT_TILE + SlopeToSpriteOffset(ti->tileh), PAL_NONE);
		}

		/* draw ramp */

		/* Draw Trambits and PBS Reservation as SpriteCombine */
		if (transport_type == TRANSPORT_ROAD || transport_type == TRANSPORT_RAIL) StartSpriteCombine();

		/* HACK set the height of the BB of a sloped ramp to 1 so a vehicle on
		 * it doesn't disappear behind it
		 */
		/* Bridge heads are drawn solid no matter how invisibility/transparency is set */
		AddSortableSpriteToDraw(psid->sprite, psid->pal, ti->x, ti->y, 16, 16, ti->tileh == SLOPE_FLAT ? 0 : 8, ti->z);

		if (transport_type == TRANSPORT_ROAD) {
			uint offset = tunnelbridge_direction;
			int z = ti->z;
			if (ti->tileh != SLOPE_FLAT) {
				offset = (offset + 1) & 1;
				z += TILE_HEIGHT;
			} else {
				offset += 2;
			}

			/* DrawBridgeRoadBits() calls EndSpriteCombine() and StartSpriteCombine() */
			DrawBridgeRoadBits(ti->tile, ti->x, ti->y, z, offset, true);

			EndSpriteCombine();
		} else if (transport_type == TRANSPORT_RAIL) {
			const RailtypeInfo *rti = GetRailTypeInfo(GetRailType(ti->tile));
			if (rti->UsesOverlay()) {
				SpriteID surface = GetCustomRailSprite(rti, ti->tile, RTSG_BRIDGE);
				if (surface != 0) {
					if (HasBridgeFlatRamp(ti->tileh, DiagDirToAxis(tunnelbridge_direction))) {
						AddSortableSpriteToDraw(surface + ((DiagDirToAxis(tunnelbridge_direction) == AXIS_X) ? RTBO_X : RTBO_Y), PAL_NONE, ti->x, ti->y, 16, 16, 0, ti->z + 8);
					} else {
						AddSortableSpriteToDraw(surface + RTBO_SLOPE + tunnelbridge_direction, PAL_NONE, ti->x, ti->y, 16, 16, 8, ti->z);
					}
				}
				/* Don't fallback to non-overlay sprite -- the spec states that
				 * if an overlay is present then the bridge surface must be
				 * present. */
			}

			/* PBS debugging, draw reserved tracks darker */
			if (_game_mode != GM_MENU && _settings_client.gui.show_track_reservation && HasTunnelBridgeReservation(ti->tile)) {
				if (rti->UsesOverlay()) {
					SpriteID overlay = GetCustomRailSprite(rti, ti->tile, RTSG_OVERLAY);
					if (HasBridgeFlatRamp(ti->tileh, DiagDirToAxis(tunnelbridge_direction))) {
						AddSortableSpriteToDraw(overlay + RTO_X + DiagDirToAxis(tunnelbridge_direction), PALETTE_CRASH, ti->x, ti->y, 16, 16, 0, ti->z + 8);
					} else {
						AddSortableSpriteToDraw(overlay + RTO_SLOPE_NE + tunnelbridge_direction, PALETTE_CRASH, ti->x, ti->y, 16, 16, 8, ti->z);
					}
				} else {
					if (HasBridgeFlatRamp(ti->tileh, DiagDirToAxis(tunnelbridge_direction))) {
						AddSortableSpriteToDraw(DiagDirToAxis(tunnelbridge_direction) == AXIS_X ? rti->base_sprites.single_x : rti->base_sprites.single_y, PALETTE_CRASH, ti->x, ti->y, 16, 16, 0, ti->z + 8);
					} else {
						AddSortableSpriteToDraw(rti->base_sprites.single_sloped + tunnelbridge_direction, PALETTE_CRASH, ti->x, ti->y, 16, 16, 8, ti->z);
					}
				}
			}

			EndSpriteCombine();
			if (HasRailCatenaryDrawn(GetRailType(ti->tile))) {
				DrawRailCatenary(ti);
			}
		}

		DrawBridgeMiddle(ti);
	}
}


/**
 * Compute bridge piece. Computes the bridge piece to display depending on the position inside the bridge.
 * bridges pieces sequence (middle parts).
 * Note that it is not covering the bridge heads, which are always referenced by the same sprite table.
 * bridge len 1: BRIDGE_PIECE_NORTH
 * bridge len 2: BRIDGE_PIECE_NORTH  BRIDGE_PIECE_SOUTH
 * bridge len 3: BRIDGE_PIECE_NORTH  BRIDGE_PIECE_MIDDLE_ODD   BRIDGE_PIECE_SOUTH
 * bridge len 4: BRIDGE_PIECE_NORTH  BRIDGE_PIECE_INNER_NORTH  BRIDGE_PIECE_INNER_SOUTH  BRIDGE_PIECE_SOUTH
 * bridge len 5: BRIDGE_PIECE_NORTH  BRIDGE_PIECE_INNER_NORTH  BRIDGE_PIECE_MIDDLE_EVEN  BRIDGE_PIECE_INNER_SOUTH  BRIDGE_PIECE_SOUTH
 * bridge len 6: BRIDGE_PIECE_NORTH  BRIDGE_PIECE_INNER_NORTH  BRIDGE_PIECE_INNER_SOUTH  BRIDGE_PIECE_INNER_NORTH  BRIDGE_PIECE_INNER_SOUTH  BRIDGE_PIECE_SOUTH
 * bridge len 7: BRIDGE_PIECE_NORTH  BRIDGE_PIECE_INNER_NORTH  BRIDGE_PIECE_INNER_SOUTH  BRIDGE_PIECE_MIDDLE_ODD   BRIDGE_PIECE_INNER_NORTH  BRIDGE_PIECE_INNER_SOUTH  BRIDGE_PIECE_SOUTH
 * #0 - always as first, #1 - always as last (if len>1)
 * #2,#3 are to pair in order
 * for odd bridges: #5 is going in the bridge middle if on even position, #4 on odd (counting from 0)
 * @param north Northernmost tile of bridge
 * @param south Southernmost tile of bridge
 * @return Index of bridge piece
 */
static BridgePieces CalcBridgePiece(uint north, uint south)
{
	if (north == 1) {
		return BRIDGE_PIECE_NORTH;
	} else if (south == 1) {
		return BRIDGE_PIECE_SOUTH;
	} else if (north < south) {
		return north & 1 ? BRIDGE_PIECE_INNER_SOUTH : BRIDGE_PIECE_INNER_NORTH;
	} else if (north > south) {
		return south & 1 ? BRIDGE_PIECE_INNER_NORTH : BRIDGE_PIECE_INNER_SOUTH;
	} else {
		return north & 1 ? BRIDGE_PIECE_MIDDLE_EVEN : BRIDGE_PIECE_MIDDLE_ODD;
	}
}


/** Bridge middle subsprite for overlapping bridges */
static const int INF = 1000;
static const SubSprite bridge_subsprite_per_height[8] = {
	{ -INF, 1 * (int)TILE_HEIGHT, INF, INF },
	{ -INF, 0 * (int)TILE_HEIGHT, INF, INF },
	{ -INF, -1 * (int)TILE_HEIGHT, INF, INF },
	{ -INF, -2 * (int)TILE_HEIGHT, INF, INF },
	{ -INF, -3 * (int)TILE_HEIGHT, INF, INF },
	{ -INF, -4 * (int)TILE_HEIGHT, INF, INF },
	{ -INF, -5 * (int)TILE_HEIGHT, INF, INF },
	{ -INF, -6 * (int)TILE_HEIGHT, INF, INF },
};

/**
 * Draws the floor and the far part of bridge.
 * @param psid the bridge sprite to be drawn
 * @param a    the given axis
 * @param x    the x position
 * @param y    the y position
 * @param z    the z position
 * @param sub  the bridge middle subsprite, if needed
 */
void DrawBridgeFloor(const PalSpriteID* psid, Axis axis, int x, int y, int z, const SubSprite *sub)
{
	if (IsInvisibilitySet(TO_BRIDGES)) return;

	if (axis == AXIS_X) {
		AddSortableSpriteToDraw(psid->sprite, psid->pal, x, y, 16, 1, 0x28, z, IsTransparencySet(TO_BRIDGES), 0, 0, BRIDGE_Z_START, sub);
	} else {
		AddSortableSpriteToDraw(psid->sprite, psid->pal, x, y, 1, 16, 0x28, z, IsTransparencySet(TO_BRIDGES), 0, 0, BRIDGE_Z_START, sub);
	}
}

/**
 * Draws the bridge roof, which is the component which is logically between the vehicle and the camera.
 * @param psid the bridge sprite to be drawn
 * @param a    the given axis
 * @param x    the x position
 * @param y    the y position
 * @param z    the z position
 * @param sub  the bridge middle subsprite, if needed
 */
void DrawBridgeRoof(const PalSpriteID *psid, Axis axis, int x, int y, int z, const SubSprite *sub)
{
	if (IsInvisibilitySet(TO_BRIDGES)) return;

	if (axis == AXIS_X) {
		if (psid->sprite & SPRITE_MASK) AddSortableSpriteToDraw(psid->sprite, psid->pal, x, y, 16, 4, 0x28, z, IsTransparencySet(TO_BRIDGES), 0, 3, BRIDGE_Z_START, sub);
	} else {
		if (psid->sprite & SPRITE_MASK) AddSortableSpriteToDraw(psid->sprite, psid->pal, x, y, 4, 16, 0x28, z, IsTransparencySet(TO_BRIDGES), 3, 0, BRIDGE_Z_START, sub);
	}
}

/**
 * Draws the middle bits of a bridge that goes through the given axis.
 * @param ti tile information of the tile to draw it on.
 * @param a  the given axis
 */
void DrawBridgeMiddleAtAxis(const TileInfo *ti, Axis axis)
{
	/* Sectional view of bridge bounding boxes:
	 *
	 *  1           2                                1,2 = SpriteCombine of Bridge front/(back&floor) and RoadCatenary
	 *  1           2                                  3 = empty helper BB
	 *  1     7     2                                4,5 = pillars under higher bridges
	 *  1 6 88888 6 2                                  6 = elrail-pylons
	 *  1 6 88888 6 2                                  7 = elrail-wire
	 *  1 6 88888 6 2  <- TILE_HEIGHT                  8 = rail-vehicle on bridge
	 *  3333333333333  <- BB_Z_SEPARATOR
	 *                 <- unused
	 *    4       5    <- BB_HEIGHT_UNDER_BRIDGE
	 *    4       5
	 *    4       5
	 *
	 */

	TileIndex rampnorth = GetNorthernBridgeEndAtAxis(ti->tile, axis);
	TileIndex rampsouth = GetSouthernBridgeEndAtAxis(ti->tile, axis);
	TransportType transport_type = GetTunnelBridgeTransportType(rampsouth);

	/* Check whether there is another bridge above this bridge tile */
	TileIndex other_bridge_head = GetNorthernBridgeEndAtAxis(ti->tile, axis == AXIS_X ? AXIS_Y : AXIS_X);
	int bridge_height_diff = other_bridge_head != INVALID_TILE ? GetBridgeHeight(other_bridge_head) - GetBridgeHeight(rampnorth) : -1;

	const SubSprite *sub_floor = nullptr;
	const SubSprite *sub_roof = nullptr;
	if (bridge_height_diff > 0) {
		/* Pick the appropiated subsprite for the given height difference */
		//TODO: still figuring this out
		sub_floor = &bridge_subsprite_per_height[std::min(bridge_height_diff, (int)lengthof(bridge_subsprite_per_height)) - 1];
		sub_roof = &bridge_subsprite_per_height[std::min(bridge_height_diff, (int)lengthof(bridge_subsprite_per_height)) - 1];
	}

	BridgePieces piece = CalcBridgePiece(
		GetTunnelBridgeLength(ti->tile, rampnorth) + 1,
		GetTunnelBridgeLength(ti->tile, rampsouth) + 1
	);

	const PalSpriteID *psid;
	bool drawfarpillar;
	if (transport_type != TRANSPORT_WATER) {
		BridgeType type = GetBridgeType(rampsouth);
		drawfarpillar = !HasBit(GetBridgeSpec(type)->flags, 0);

		uint base_offset;
		if (transport_type == TRANSPORT_RAIL) {
			base_offset = GetRailTypeInfo(GetRailType(rampsouth))->bridge_offset;
		} else {
			base_offset = 8;
		}

		psid = base_offset + GetBridgeSpriteTable(type, piece);
	} else {
		drawfarpillar = true;
		psid = _aqueduct_sprites;
	}

	if (axis != AXIS_X) psid += 4;

	int x = ti->x;
	int y = ti->y;
	uint bridge_z = GetBridgePixelHeight(rampsouth);
	int z = bridge_z - BRIDGE_Z_START;

	/* Add a bounding box that separates the bridge from things below it. */
	AddSortableSpriteToDraw(SPR_EMPTY_BOUNDING_BOX, PAL_NONE, x, y, 16, 16, 1, bridge_z - TILE_HEIGHT + BB_Z_SEPARATOR);

	/* Draw Trambits as SpriteCombine */
	if (transport_type == TRANSPORT_ROAD || transport_type == TRANSPORT_RAIL) StartSpriteCombine();

	/* Draw floor and far part of bridge */
	DrawBridgeFloor(psid, axis, x, y, z, sub_floor);

	psid++;

	if (transport_type == TRANSPORT_ROAD) {
		/* DrawBridgeRoadBits() calls EndSpriteCombine() and StartSpriteCombine() */
		DrawBridgeRoadBits(rampsouth, x, y, bridge_z, axis ^ 1, false);
	} else if (transport_type == TRANSPORT_RAIL) {
		const RailtypeInfo *rti = GetRailTypeInfo(GetRailType(rampsouth));
		if (rti->UsesOverlay() && !IsInvisibilitySet(TO_BRIDGES)) {
			SpriteID surface = GetCustomRailSprite(rti, rampsouth, RTSG_BRIDGE, TCX_ON_BRIDGE);
			if (surface != 0) {
				AddSortableSpriteToDraw(surface + axis, PAL_NONE, x, y, 16, 16, 0, bridge_z, IsTransparencySet(TO_BRIDGES));
			}
		}

		if (_game_mode != GM_MENU && _settings_client.gui.show_track_reservation && !IsInvisibilitySet(TO_BRIDGES) && HasTunnelBridgeReservation(rampnorth)) {
			if (rti->UsesOverlay()) {
				SpriteID overlay = GetCustomRailSprite(rti, ti->tile, RTSG_OVERLAY);
				AddSortableSpriteToDraw(overlay + RTO_X + axis, PALETTE_CRASH, ti->x, ti->y, 16, 16, 0, bridge_z, IsTransparencySet(TO_BRIDGES));
			} else {
				AddSortableSpriteToDraw(axis == AXIS_X ? rti->base_sprites.single_x : rti->base_sprites.single_y, PALETTE_CRASH, ti->x, ti->y, 16, 16, 0, bridge_z, IsTransparencySet(TO_BRIDGES));
			}
		}

		EndSpriteCombine();

		if (HasRailCatenaryDrawn(GetRailType(rampsouth))) {
			DrawRailCatenaryOnBridge(ti, axis);
		}
	}

	if (axis == AXIS_X) {
		y += 12;
	} else {
		x += 12;
	}

	/* draw roof, the component of the bridge which is logically between the vehicle and the camera */
	DrawBridgeRoof(psid, axis, x, y, z, sub_roof);

	/* Draw TramFront as SpriteCombine */
	if (transport_type == TRANSPORT_ROAD) EndSpriteCombine();

	/* Do not draw anything more if bridges are invisible */
	if (IsInvisibilitySet(TO_BRIDGES)) return;

	psid++;
	DrawBridgePillars(psid, ti, axis, drawfarpillar, x, y, z);
}

/**
 * Draw the middle bits of a bridge.
 * @param ti Tile information of the tile to draw it on.
 */
void DrawBridgeMiddle(const TileInfo *ti)
{
	if (!IsBridgeAbove(ti->tile)) return;

	TileIndex rampnorth_x = GetNorthernBridgeEndAtAxis(ti->tile, AXIS_X);
	TileIndex rampnorth_y = GetNorthernBridgeEndAtAxis(ti->tile, AXIS_Y);

	if (rampnorth_x != INVALID_TILE && rampnorth_y != INVALID_TILE) {
		/* This tile has two crossing bridges so draw the lowest one first */
		int height_x = TileHeight(rampnorth_x);
		int height_y = TileHeight(rampnorth_y);
		if (height_x < height_y) {
			DrawBridgeMiddleAtAxis(ti, AXIS_X);
			DrawBridgeMiddleAtAxis(ti, AXIS_Y);
		} else {
			DrawBridgeMiddleAtAxis(ti, AXIS_Y);
			DrawBridgeMiddleAtAxis(ti, AXIS_X);
		}
	} else {
		/* There is a single bridge to draw */
		if (rampnorth_x != INVALID_TILE) DrawBridgeMiddleAtAxis(ti, AXIS_X);
		if (rampnorth_y != INVALID_TILE) DrawBridgeMiddleAtAxis(ti, AXIS_Y);
	}
}


static int GetSlopePixelZ_TunnelBridge(TileIndex tile, uint x, uint y)
{
	int z;
	Slope tileh = GetTilePixelSlope(tile, &z);

	x &= 0xF;
	y &= 0xF;

	if (IsTunnel(tile)) {
		uint pos = (DiagDirToAxis(GetTunnelBridgeDirection(tile)) == AXIS_X ? y : x);

		/* In the tunnel entrance? */
		if (5 <= pos && pos <= 10) return z;
	} else { // IsBridge(tile)
		DiagDirection dir = GetTunnelBridgeDirection(tile);
		uint pos = (DiagDirToAxis(dir) == AXIS_X ? y : x);

		z += ApplyPixelFoundationToSlope(GetBridgeFoundation(tileh, DiagDirToAxis(dir)), &tileh);

		/* On the bridge ramp? */
		if (5 <= pos && pos <= 10) {
			int delta;

			if (tileh != SLOPE_FLAT) return z + TILE_HEIGHT;

			switch (dir) {
				default: NOT_REACHED();
				case DIAGDIR_NE: delta = (TILE_SIZE - 1 - x) / 2; break;
				case DIAGDIR_SE: delta = y / 2; break;
				case DIAGDIR_SW: delta = x / 2; break;
				case DIAGDIR_NW: delta = (TILE_SIZE - 1 - y) / 2; break;
			}
			return z + 1 + delta;
		}
	}

	return z + GetPartialPixelZ(x, y, tileh);
}

static Foundation GetFoundation_TunnelBridge(TileIndex tile, Slope tileh)
{
	return IsTunnel(tile) ? FOUNDATION_NONE : GetBridgeFoundation(tileh, DiagDirToAxis(GetTunnelBridgeDirection(tile)));
}

static void GetTileDesc_TunnelBridge(TileIndex tile, TileDesc *td)
{
	TransportType tt = GetTunnelBridgeTransportType(tile);

	if (IsTunnel(tile)) {
		td->str = (tt == TRANSPORT_RAIL) ? STR_LAI_TUNNEL_DESCRIPTION_RAILROAD : STR_LAI_TUNNEL_DESCRIPTION_ROAD;
	} else { // IsBridge(tile)
		td->str = (tt == TRANSPORT_WATER) ? STR_LAI_BRIDGE_DESCRIPTION_AQUEDUCT : GetBridgeSpec(GetBridgeType(tile))->transport_name[tt];
	}
	td->owner[0] = GetTileOwner(tile);

	Owner road_owner = INVALID_OWNER;
	Owner tram_owner = INVALID_OWNER;
	RoadType road_rt = GetRoadTypeRoad(tile);
	RoadType tram_rt = GetRoadTypeTram(tile);
	if (road_rt != INVALID_ROADTYPE) {
		const RoadTypeInfo *rti = GetRoadTypeInfo(road_rt);
		td->roadtype = rti->strings.name;
		td->road_speed = rti->max_speed / 2;
		road_owner = GetRoadOwner(tile, RTT_ROAD);
	}
	if (tram_rt != INVALID_ROADTYPE) {
		const RoadTypeInfo *rti = GetRoadTypeInfo(tram_rt);
		td->tramtype = rti->strings.name;
		td->tram_speed = rti->max_speed / 2;
		tram_owner = GetRoadOwner(tile, RTT_TRAM);
	}

	/* Is there a mix of owners? */
	if ((tram_owner != INVALID_OWNER && tram_owner != td->owner[0]) ||
			(road_owner != INVALID_OWNER && road_owner != td->owner[0])) {
		uint i = 1;
		if (road_owner != INVALID_OWNER) {
			td->owner_type[i] = STR_LAND_AREA_INFORMATION_ROAD_OWNER;
			td->owner[i] = road_owner;
			i++;
		}
		if (tram_owner != INVALID_OWNER) {
			td->owner_type[i] = STR_LAND_AREA_INFORMATION_TRAM_OWNER;
			td->owner[i] = tram_owner;
		}
	}

	if (tt == TRANSPORT_RAIL) {
		const RailtypeInfo *rti = GetRailTypeInfo(GetRailType(tile));
		td->rail_speed = rti->max_speed;
		td->railtype = rti->strings.name;

		if (!IsTunnel(tile)) {
			uint16 spd = GetBridgeSpec(GetBridgeType(tile))->speed;
			if (td->rail_speed == 0 || spd < td->rail_speed) {
				td->rail_speed = spd;
			}
		}
	} else if (tt == TRANSPORT_ROAD && !IsTunnel(tile)) {
		uint16 spd = GetBridgeSpec(GetBridgeType(tile))->speed;
		if (road_rt != INVALID_ROADTYPE && (td->road_speed == 0 || spd < td->road_speed)) td->road_speed = spd;
		if (tram_rt != INVALID_ROADTYPE && (td->tram_speed == 0 || spd < td->tram_speed)) td->tram_speed = spd;
	}
}


static void TileLoop_TunnelBridge(TileIndex tile)
{
	bool snow_or_desert = HasTunnelBridgeSnowOrDesert(tile);
	switch (_settings_game.game_creation.landscape) {
		case LT_ARCTIC: {
			/* As long as we do not have a snow density, we want to use the density
			 * from the entry edge. For tunnels this is the lowest point for bridges the highest point.
			 * (Independent of foundations) */
			int z = IsBridge(tile) ? GetTileMaxZ(tile) : GetTileZ(tile);
			if (snow_or_desert != (z > GetSnowLine())) {
				SetTunnelBridgeSnowOrDesert(tile, !snow_or_desert);
				MarkTileDirtyByTile(tile);
			}
			break;
		}

		case LT_TROPIC:
			if (GetTropicZone(tile) == TROPICZONE_DESERT && !snow_or_desert) {
				SetTunnelBridgeSnowOrDesert(tile, true);
				MarkTileDirtyByTile(tile);
			}
			break;

		default:
			break;
	}
}

static TrackStatus GetTileTrackStatus_TunnelBridge(TileIndex tile, TransportType mode, uint sub_mode, DiagDirection side)
{
	TransportType transport_type = GetTunnelBridgeTransportType(tile);
	if (transport_type != mode || (transport_type == TRANSPORT_ROAD && !HasTileRoadType(tile, (RoadTramType)sub_mode))) return 0;

	DiagDirection dir = GetTunnelBridgeDirection(tile);
	if (side != INVALID_DIAGDIR && side != ReverseDiagDir(dir)) return 0;
	return CombineTrackStatus(TrackBitsToTrackdirBits(DiagDirToDiagTrackBits(dir)), TRACKDIR_BIT_NONE);
}

static void ChangeTileOwner_TunnelBridge(TileIndex tile, Owner old_owner, Owner new_owner)
{
	TileIndex other_end = GetOtherTunnelBridgeEnd(tile);
	/* Set number of pieces to zero if it's the southern tile as we
	 * don't want to update the infrastructure counts twice. */
	uint num_pieces = tile < other_end ? (GetTunnelBridgeLength(tile, other_end) + 2) * TUNNELBRIDGE_TRACKBIT_FACTOR : 0;

	FOR_ALL_ROADTRAMTYPES(rtt) {
		/* Update all roadtypes, no matter if they are present */
		if (GetRoadOwner(tile, rtt) == old_owner) {
			RoadType rt = GetRoadType(tile, rtt);
			if (rt != INVALID_ROADTYPE) {
				/* Update company infrastructure counts. A full diagonal road tile has two road bits.
				 * No need to dirty windows here, we'll redraw the whole screen anyway. */
				Company::Get(old_owner)->infrastructure.road[rt] -= num_pieces * 2;
				if (new_owner != INVALID_OWNER) Company::Get(new_owner)->infrastructure.road[rt] += num_pieces * 2;
			}

			SetRoadOwner(tile, rtt, new_owner == INVALID_OWNER ? OWNER_NONE : new_owner);
		}
	}

	if (!IsTileOwner(tile, old_owner)) return;

	/* Update company infrastructure counts for rail and water as well.
	 * No need to dirty windows here, we'll redraw the whole screen anyway. */
	TransportType tt = GetTunnelBridgeTransportType(tile);
	Company *old = Company::Get(old_owner);
	if (tt == TRANSPORT_RAIL) {
		old->infrastructure.rail[GetRailType(tile)] -= num_pieces;
		if (new_owner != INVALID_OWNER) Company::Get(new_owner)->infrastructure.rail[GetRailType(tile)] += num_pieces;
	} else if (tt == TRANSPORT_WATER) {
		old->infrastructure.water -= num_pieces;
		if (new_owner != INVALID_OWNER) Company::Get(new_owner)->infrastructure.water += num_pieces;
	}

	if (new_owner != INVALID_OWNER) {
		SetTileOwner(tile, new_owner);
	} else {
		if (tt == TRANSPORT_RAIL) {
			/* Since all of our vehicles have been removed, it is safe to remove the rail
			 * bridge / tunnel. */
			CommandCost ret = DoCommand(tile, 0, 0, DC_EXEC | DC_BANKRUPT, CMD_LANDSCAPE_CLEAR);
			(void)ret; // assert only
			assert(ret.Succeeded());
		} else {
			/* In any other case, we can safely reassign the ownership to OWNER_NONE. */
			SetTileOwner(tile, OWNER_NONE);
		}
	}
}

/**
 * Frame when the 'enter tunnel' sound should be played. This is the second
 * frame on a tile, so the sound is played shortly after entering the tunnel
 * tile, while the vehicle is still visible.
 */
static const byte TUNNEL_SOUND_FRAME = 1;

/**
 * Frame when a vehicle should be hidden in a tunnel with a certain direction.
 * This differs per direction, because of visibility / bounding box issues.
 * Note that direction, in this case, is the direction leading into the tunnel.
 * When entering a tunnel, hide the vehicle when it reaches the given frame.
 * When leaving a tunnel, show the vehicle when it is one frame further
 * to the 'outside', i.e. at (TILE_SIZE-1) - (frame) + 1
 */
extern const byte _tunnel_visibility_frame[DIAGDIR_END] = {12, 8, 8, 12};

static VehicleEnterTileStatus VehicleEnter_TunnelBridge(Vehicle *v, TileIndex tile, int x, int y)
{
	int z = GetSlopePixelZ(x, y) - v->z_pos;

	if (abs(z) > 2) return VETSB_CANNOT_ENTER;
	/* Direction into the wormhole */
	const DiagDirection dir = GetTunnelBridgeDirection(tile);
	/* Direction of the vehicle */
	const DiagDirection vdir = DirToDiagDir(v->direction);
	/* New position of the vehicle on the tile */
	byte pos = (DiagDirToAxis(vdir) == AXIS_X ? x : y) & TILE_UNIT_MASK;
	/* Number of units moved by the vehicle since entering the tile */
	byte frame = (vdir == DIAGDIR_NE || vdir == DIAGDIR_NW) ? TILE_SIZE - 1 - pos : pos;

	if (IsTunnel(tile)) {
		if (v->type == VEH_TRAIN) {
			Train *t = Train::From(v);

			if (t->track != TRACK_BIT_WORMHOLE && dir == vdir) {
				if (t->IsFrontEngine() && frame == TUNNEL_SOUND_FRAME) {
					if (!PlayVehicleSound(t, VSE_TUNNEL) && RailVehInfo(t->engine_type)->engclass == 0) {
						SndPlayVehicleFx(SND_05_TRAIN_THROUGH_TUNNEL, v);
					}
					return VETSB_CONTINUE;
				}
				if (frame == _tunnel_visibility_frame[dir]) {
					t->tile = tile;
					t->track = TRACK_BIT_WORMHOLE;
					t->vehstatus |= VS_HIDDEN;
					return VETSB_ENTERED_WORMHOLE;
				}
			}

			if (dir == ReverseDiagDir(vdir) && frame == TILE_SIZE - _tunnel_visibility_frame[dir] && z == 0) {
				/* We're at the tunnel exit ?? */
				t->tile = tile;
				t->track = DiagDirToDiagTrackBits(vdir);
				assert(t->track);
				t->vehstatus &= ~VS_HIDDEN;
				return VETSB_ENTERED_WORMHOLE;
			}
		} else if (v->type == VEH_ROAD) {
			RoadVehicle *rv = RoadVehicle::From(v);

			/* Enter tunnel? */
			if (rv->state != RVSB_WORMHOLE && dir == vdir) {
				if (frame == _tunnel_visibility_frame[dir]) {
					/* Frame should be equal to the next frame number in the RV's movement */
					assert(frame == rv->frame + 1);
					rv->tile = tile;
					rv->state = RVSB_WORMHOLE;
					rv->vehstatus |= VS_HIDDEN;
					return VETSB_ENTERED_WORMHOLE;
				} else {
					return VETSB_CONTINUE;
				}
			}

			/* We're at the tunnel exit ?? */
			if (dir == ReverseDiagDir(vdir) && frame == TILE_SIZE - _tunnel_visibility_frame[dir] && z == 0) {
				rv->tile = tile;
				rv->state = DiagDirToDiagTrackdir(vdir);
				rv->frame = frame;
				rv->vehstatus &= ~VS_HIDDEN;
				return VETSB_ENTERED_WORMHOLE;
			}
		}
	} else { // IsBridge(tile)
		if (v->type != VEH_SHIP) {
			/* modify speed of vehicle */
			uint16 spd = GetBridgeSpec(GetBridgeType(tile))->speed;

			if (v->type == VEH_ROAD) spd *= 2;
			Vehicle *first = v->First();
			first->cur_speed = std::min(first->cur_speed, spd);
		}

		if (vdir == dir) {
			/* Vehicle enters bridge at the last frame inside this tile. */
			if (frame != TILE_SIZE - 1) return VETSB_CONTINUE;
			switch (v->type) {
				case VEH_TRAIN: {
					Train *t = Train::From(v);
					t->track = TRACK_BIT_WORMHOLE;
					ClrBit(t->gv_flags, GVF_GOINGUP_BIT);
					ClrBit(t->gv_flags, GVF_GOINGDOWN_BIT);
					break;
				}

				case VEH_ROAD: {
					RoadVehicle *rv = RoadVehicle::From(v);
					rv->state = RVSB_WORMHOLE;
					/* There are no slopes inside bridges / tunnels. */
					ClrBit(rv->gv_flags, GVF_GOINGUP_BIT);
					ClrBit(rv->gv_flags, GVF_GOINGDOWN_BIT);
					break;
				}

				case VEH_SHIP:
					Ship::From(v)->state = TRACK_BIT_WORMHOLE;
					break;

				default: NOT_REACHED();
			}
			return VETSB_ENTERED_WORMHOLE;
		} else if (vdir == ReverseDiagDir(dir)) {
			v->tile = tile;
			switch (v->type) {
				case VEH_TRAIN: {
					Train *t = Train::From(v);
					if (t->track == TRACK_BIT_WORMHOLE) {
						t->track = DiagDirToDiagTrackBits(vdir);
						return VETSB_ENTERED_WORMHOLE;
					}
					break;
				}

				case VEH_ROAD: {
					RoadVehicle *rv = RoadVehicle::From(v);
					if (rv->state == RVSB_WORMHOLE) {
						rv->state = DiagDirToDiagTrackdir(vdir);
						rv->frame = 0;
						return VETSB_ENTERED_WORMHOLE;
					}
					break;
				}

				case VEH_SHIP: {
					Ship *ship = Ship::From(v);
					if (ship->state == TRACK_BIT_WORMHOLE) {
						ship->state = DiagDirToDiagTrackBits(vdir);
						return VETSB_ENTERED_WORMHOLE;
					}
					break;
				}

				default: NOT_REACHED();
			}
		}
	}
	return VETSB_CONTINUE;
}

static CommandCost TerraformTile_TunnelBridge(TileIndex tile, DoCommandFlag flags, int z_new, Slope tileh_new)
{
	if (_settings_game.construction.build_on_slopes && AutoslopeEnabled() && IsBridge(tile) && GetTunnelBridgeTransportType(tile) != TRANSPORT_WATER) {
		DiagDirection direction = GetTunnelBridgeDirection(tile);
		Axis axis = DiagDirToAxis(direction);
		CommandCost res;
		int z_old;
		Slope tileh_old = GetTileSlope(tile, &z_old);

		/* Check if new slope is valid for bridges in general (so we can safely call GetBridgeFoundation()) */
		if ((direction == DIAGDIR_NW) || (direction == DIAGDIR_NE)) {
			CheckBridgeSlope(BRIDGE_PIECE_SOUTH, axis, &tileh_old, &z_old);
			res = CheckBridgeSlope(BRIDGE_PIECE_SOUTH, axis, &tileh_new, &z_new);
		} else {
			CheckBridgeSlope(BRIDGE_PIECE_NORTH, axis, &tileh_old, &z_old);
			res = CheckBridgeSlope(BRIDGE_PIECE_NORTH, axis, &tileh_new, &z_new);
		}

		/* Surface slope is valid and remains unchanged? */
		if (res.Succeeded() && (z_old == z_new) && (tileh_old == tileh_new)) return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_FOUNDATION]);
	}

	return DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
}

extern const TileTypeProcs _tile_type_tunnelbridge_procs = {
	DrawTile_TunnelBridge,           // draw_tile_proc
	GetSlopePixelZ_TunnelBridge,     // get_slope_z_proc
	ClearTile_TunnelBridge,          // clear_tile_proc
	nullptr,                            // add_accepted_cargo_proc
	GetTileDesc_TunnelBridge,        // get_tile_desc_proc
	GetTileTrackStatus_TunnelBridge, // get_tile_track_status_proc
	nullptr,                            // click_tile_proc
	nullptr,                            // animate_tile_proc
	TileLoop_TunnelBridge,           // tile_loop_proc
	ChangeTileOwner_TunnelBridge,    // change_tile_owner_proc
	nullptr,                            // add_produced_cargo_proc
	VehicleEnter_TunnelBridge,       // vehicle_enter_tile_proc
	GetFoundation_TunnelBridge,      // get_foundation_proc
	TerraformTile_TunnelBridge,      // terraform_tile_proc
};
