/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file station_sl.cpp Code handling saving and loading of stations. */

#include "../stdafx.h"
#include "../station_base.h"
#include "../waypoint_base.h"
#include "../roadstop_base.h"
#include "../vehicle_base.h"
#include "../newgrf_station.h"

#include "saveload.h"
#include "table/strings.h"

#include "../safeguards.h"

/**
 * Update the buoy orders to be waypoint orders.
 * @param o the order 'list' to check.
 */
static void UpdateWaypointOrder(Order *o)
{
	if (!o->IsType(OT_GOTO_STATION)) return;

	const Station *st = Station::Get(o->GetDestination());
	if ((st->had_vehicle_of_type & HVOT_WAYPOINT) == 0) return;

	o->MakeGoToWaypoint(o->GetDestination());
}

/**
 * Perform all steps to upgrade from the old station buoys to the new version
 * that uses waypoints. This includes some old saveload mechanics.
 */
void MoveBuoysToWaypoints()
{
	/* Buoy orders become waypoint orders */
	for (OrderList *ol : OrderList::Iterate()) {
		VehicleType vt = ol->GetFirstSharedVehicle()->type;
		if (vt != VEH_SHIP && vt != VEH_TRAIN) continue;

		for (Order *o = ol->GetFirstOrder(); o != nullptr; o = o->next) UpdateWaypointOrder(o);
	}

	for (Vehicle *v : Vehicle::Iterate()) {
		VehicleType vt = v->type;
		if (vt != VEH_SHIP && vt != VEH_TRAIN) continue;

		UpdateWaypointOrder(&v->current_order);
	}

	/* Now make the stations waypoints */
	for (Station *st : Station::Iterate()) {
		if ((st->had_vehicle_of_type & HVOT_WAYPOINT) == 0) continue;

		StationID index    = st->index;
		TileIndex xy       = st->xy;
		Town *town         = st->town;
		StringID string_id = st->string_id;
		std::string name   = st->name;
		Date build_date    = st->build_date;
		/* TTDPatch could use "buoys with rail station" for rail waypoints */
		bool train         = st->train_station.tile != INVALID_TILE;
		TileArea train_st  = st->train_station;

		/* Delete the station, so we can make it a real waypoint. */
		delete st;

		/* Stations and waypoints are in the same pool, so if a station
		 * is deleted there must be place for a Waypoint. */
		assert(Waypoint::CanAllocateItem());
		Waypoint *wp   = new (index) Waypoint(xy);
		wp->town       = town;
		wp->string_id  = train ? STR_SV_STNAME_WAYPOINT : STR_SV_STNAME_BUOY;
		wp->name       = name;
		wp->delete_ctr = 0; // Just reset delete counter for once.
		wp->build_date = build_date;
		wp->owner      = train ? GetTileOwner(xy) : OWNER_NONE;

		if (IsInsideBS(string_id, STR_SV_STNAME_BUOY, 9)) wp->town_cn = string_id - STR_SV_STNAME_BUOY;

		if (train) {
			/* When we make a rail waypoint of the station, convert the map as well. */
			TILE_AREA_LOOP(t, train_st) {
				if (!IsTileType(t, MP_STATION) || GetStationIndex(t) != index) continue;

				SB(_me[t].m6, 3, 3, STATION_WAYPOINT);
				wp->rect.BeforeAddTile(t, StationRect::ADD_FORCE);
			}

			wp->train_station = train_st;
			wp->facilities |= FACIL_TRAIN;
		} else if (IsBuoyTile(xy) && GetStationIndex(xy) == index) {
			wp->rect.BeforeAddTile(xy, StationRect::ADD_FORCE);
			wp->facilities |= FACIL_DOCK;
		}
	}
}

void AfterLoadStations()
{
	/* Update the speclists of all stations to point to the currently loaded custom stations. */
	for (BaseStation *st : BaseStation::Iterate()) {
		for (uint i = 0; i < st->num_specs; i++) {
			if (st->speclist[i].grfid == 0) continue;

			st->speclist[i].spec = StationClass::GetByGrf(st->speclist[i].grfid, st->speclist[i].localidx, nullptr);
		}

		if (Station::IsExpected(st)) {
			Station *sta = Station::From(st);
			for (const RoadStop *rs = sta->bus_stops; rs != nullptr; rs = rs->next) sta->bus_station.Add(rs->xy);
			for (const RoadStop *rs = sta->truck_stops; rs != nullptr; rs = rs->next) sta->truck_station.Add(rs->xy);
		}

		StationUpdateCachedTriggers(st);
	}
}

/**
 * (Re)building of road stop caches after loading a savegame.
 */
void AfterLoadRoadStops()
{
	/* First construct the drive through entries */
	for (RoadStop *rs : RoadStop::Iterate()) {
		if (IsDriveThroughStopTile(rs->xy)) rs->MakeDriveThrough();
	}
	/* And then rebuild the data in those entries */
	for (RoadStop *rs : RoadStop::Iterate()) {
		if (!HasBit(rs->status, RoadStop::RSSFB_BASE_ENTRY)) continue;

		rs->GetEntry(DIAGDIR_NE)->Rebuild(rs);
		rs->GetEntry(DIAGDIR_NW)->Rebuild(rs);
	}
}

static const SaveLoad _roadstop_desc[] = {
	SLE_VAR(RoadStop, xy,           SLE_UINT32),
	SLE_CONDNULL(1, SL_MIN_VERSION, SLV_45),
	SLE_VAR(RoadStop, status,       SLE_UINT8),
	/* Index was saved in some versions, but this is not needed */
	SLE_CONDNULL(4, SL_MIN_VERSION, SLV_9),
	SLE_CONDNULL(2, SL_MIN_VERSION, SLV_45),
	SLE_CONDNULL(1, SL_MIN_VERSION, SLV_26),

	SLE_REF(RoadStop, next,         REF_ROADSTOPS),
	SLE_CONDNULL(2, SL_MIN_VERSION, SLV_45),

	SLE_CONDNULL(4, SL_MIN_VERSION, SLV_25),
	SLE_CONDNULL(1, SLV_25, SLV_26),

	SLE_END()
};

static const SaveLoad _old_station_desc[] = {
	SLE_CONDVAR(Station, xy,                         SLE_FILE_U16 | SLE_VAR_U32,  SL_MIN_VERSION, SLV_6),
	SLE_CONDVAR(Station, xy,                         SLE_UINT32,                  SLV_6, SL_MAX_VERSION),
	SLE_CONDNULL(4, SL_MIN_VERSION, SLV_6),  ///< bus/lorry tile
	SLE_CONDVAR(Station, train_station.tile,         SLE_FILE_U16 | SLE_VAR_U32,  SL_MIN_VERSION, SLV_6),
	SLE_CONDVAR(Station, train_station.tile,         SLE_UINT32,                  SLV_6, SL_MAX_VERSION),
	SLE_CONDVAR(Station, airport.tile,               SLE_FILE_U16 | SLE_VAR_U32,  SL_MIN_VERSION, SLV_6),
	SLE_CONDVAR(Station, airport.tile,               SLE_UINT32,                  SLV_6, SL_MAX_VERSION),
	SLE_CONDNULL(2, SL_MIN_VERSION, SLV_6),
	SLE_CONDNULL(4, SLV_6, SLV_MULTITILE_DOCKS),
	    SLE_REF(Station, town,                       REF_TOWN),
	    SLE_VAR(Station, train_station.w,            SLE_FILE_U8 | SLE_VAR_U16),
	SLE_CONDVAR(Station, train_station.h,            SLE_FILE_U8 | SLE_VAR_U16,   SLV_2, SL_MAX_VERSION),

	SLE_CONDNULL(1, SL_MIN_VERSION, SLV_4),  ///< alpha_order

	    SLE_VAR(Station, string_id,                  SLE_STRINGID),
	SLE_CONDSSTR(Station, name,                      SLE_STR | SLF_ALLOW_CONTROL, SLV_84, SL_MAX_VERSION),
	SLE_CONDVAR(Station, indtype,                    SLE_UINT8,                 SLV_103, SL_MAX_VERSION),
	SLE_CONDVAR(Station, had_vehicle_of_type,        SLE_FILE_U16 | SLE_VAR_U8,   SL_MIN_VERSION, SLV_122),
	SLE_CONDVAR(Station, had_vehicle_of_type,        SLE_UINT8,                 SLV_122, SL_MAX_VERSION),

	    SLE_VAR(Station, time_since_load,            SLE_UINT8),
	    SLE_VAR(Station, time_since_unload,          SLE_UINT8),
	    SLE_VAR(Station, delete_ctr,                 SLE_UINT8),
	    SLE_VAR(Station, owner,                      SLE_UINT8),
	    SLE_VAR(Station, facilities,                 SLE_UINT8),
	    SLE_VAR(Station, airport.type,               SLE_UINT8),

	SLE_CONDNULL(2, SL_MIN_VERSION, SLV_6),  ///< Truck/bus stop status
	SLE_CONDNULL(1, SL_MIN_VERSION, SLV_5),  ///< Blocked months

	SLE_CONDVAR(Station, airport.flags,              SLE_VAR_U64 | SLE_FILE_U16,  SL_MIN_VERSION,  SLV_3),
	SLE_CONDVAR(Station, airport.flags,              SLE_VAR_U64 | SLE_FILE_U32,  SLV_3, SLV_46),
	SLE_CONDVAR(Station, airport.flags,              SLE_UINT64,                 SLV_46, SL_MAX_VERSION),

	SLE_CONDNULL(2, SL_MIN_VERSION, SLV_26), ///< last-vehicle
	SLE_CONDVAR(Station, last_vehicle_type,          SLE_UINT8,                  SLV_26, SL_MAX_VERSION),

	SLE_CONDNULL(2, SLV_3, SLV_26), ///< custom station class and id
	SLE_CONDVAR(Station, build_date,                 SLE_FILE_U16 | SLE_VAR_I32,  SLV_3, SLV_31),
	SLE_CONDVAR(Station, build_date,                 SLE_INT32,                  SLV_31, SL_MAX_VERSION),

	SLE_CONDREF(Station, bus_stops,                  REF_ROADSTOPS,               SLV_6, SL_MAX_VERSION),
	SLE_CONDREF(Station, truck_stops,                REF_ROADSTOPS,               SLV_6, SL_MAX_VERSION),

	/* Used by newstations for graphic variations */
	SLE_CONDVAR(Station, random_bits,                SLE_UINT16,                 SLV_27, SL_MAX_VERSION),
	SLE_CONDVAR(Station, waiting_triggers,           SLE_UINT8,                  SLV_27, SL_MAX_VERSION),
	SLE_CONDVAR(Station, num_specs,                  SLE_UINT8,                  SLV_27, SL_MAX_VERSION),

	SLE_CONDLST(Station, loading_vehicles,           REF_VEHICLE,                SLV_57, SL_MAX_VERSION),

	/* reserve extra space in savegame here. (currently 32 bytes) */
	SLE_CONDNULL(32, SLV_2, SL_MAX_VERSION),

	SLE_END()
};

static uint16 _waiting_acceptance;
static uint32 _num_flows;
static uint16 _cargo_source;
static uint32 _cargo_source_xy;
static uint8  _cargo_days;
static Money  _cargo_feeder_share;

static const SaveLoad _station_speclist_desc[] = {
	SLE_CONDVAR(StationSpecList, grfid,    SLE_UINT32, SLV_27, SL_MAX_VERSION),
	SLE_CONDVAR(StationSpecList, localidx, SLE_UINT8,  SLV_27, SL_MAX_VERSION),

	SLE_END()
};

std::list<CargoPacket *> _packets;
uint32 _num_dests;

struct FlowSaveLoad {
	FlowSaveLoad() : source(0), via(0), share(0), restricted(false) {}
	StationID source;
	StationID via;
	uint32 share;
	bool restricted;
};

static const SaveLoad _flow_desc[] = {
	    SLE_VAR(FlowSaveLoad, source,     SLE_UINT16),
	    SLE_VAR(FlowSaveLoad, via,        SLE_UINT16),
	    SLE_VAR(FlowSaveLoad, share,      SLE_UINT32),
	SLE_CONDVAR(FlowSaveLoad, restricted, SLE_BOOL, SLV_187, SL_MAX_VERSION),
	    SLE_END()
};

/**
 * Wrapper function to get the GoodsEntry's internal structure while
 * some of the variables itself are private.
 * @return the saveload description for GoodsEntry.
 */
const SaveLoad *GetGoodsDesc()
{
	static const SaveLoad goods_desc[] = {
		SLEG_CONDVAR(            _waiting_acceptance,  SLE_UINT16,                  SL_MIN_VERSION, SLV_68),
		 SLE_CONDVAR(GoodsEntry, status,               SLE_UINT8,                  SLV_68, SL_MAX_VERSION),
		SLE_CONDNULL(2,                                                            SLV_51, SLV_68),
		     SLE_VAR(GoodsEntry, time_since_pickup,    SLE_UINT8),
		     SLE_VAR(GoodsEntry, rating,               SLE_UINT8),
		SLEG_CONDVAR(            _cargo_source,        SLE_FILE_U8 | SLE_VAR_U16,   SL_MIN_VERSION, SLV_7),
		SLEG_CONDVAR(            _cargo_source,        SLE_UINT16,                  SLV_7, SLV_68),
		SLEG_CONDVAR(            _cargo_source_xy,     SLE_UINT32,                 SLV_44, SLV_68),
		SLEG_CONDVAR(            _cargo_days,          SLE_UINT8,                   SL_MIN_VERSION, SLV_68),
		     SLE_VAR(GoodsEntry, last_speed,           SLE_UINT8),
		     SLE_VAR(GoodsEntry, last_age,             SLE_UINT8),
		SLEG_CONDVAR(            _cargo_feeder_share,  SLE_FILE_U32 | SLE_VAR_I64, SLV_14, SLV_65),
		SLEG_CONDVAR(            _cargo_feeder_share,  SLE_INT64,                  SLV_65, SLV_68),
		 SLE_CONDVAR(GoodsEntry, amount_fract,         SLE_UINT8,                 SLV_150, SL_MAX_VERSION),
		SLEG_CONDLST(            _packets,             REF_CARGO_PACKET,           SLV_68, SLV_183),
		SLEG_CONDVAR(            _num_dests,           SLE_UINT32,                SLV_183, SL_MAX_VERSION),
		 SLE_CONDVAR(GoodsEntry, cargo.reserved_count, SLE_UINT,                  SLV_181, SL_MAX_VERSION),
		 SLE_CONDVAR(GoodsEntry, cargo_age_ctr,        SLE_UINT16,                SLV_MORE_CARGO_AGE, SL_MAX_VERSION),
		 SLE_CONDVAR(GoodsEntry, link_graph,           SLE_UINT16,                SLV_183, SL_MAX_VERSION),
		 SLE_CONDVAR(GoodsEntry, node,                 SLE_UINT16,                SLV_183, SL_MAX_VERSION),
		SLEG_CONDVAR(            _num_flows,           SLE_UINT32,                SLV_183, SL_MAX_VERSION),
		 SLE_CONDVAR(GoodsEntry, max_waiting_cargo,    SLE_UINT32,                SLV_183, SL_MAX_VERSION),
		SLE_END()
	};

	return goods_desc;
}

typedef std::pair<const StationID, std::list<CargoPacket *> > StationCargoPair;

static const SaveLoad _cargo_list_desc[] = {
	SLE_VAR(StationCargoPair, first,  SLE_UINT16),
	SLE_LST(StationCargoPair, second, REF_CARGO_PACKET),
	SLE_END()
};

/**
 * Swap the temporary packets with the packets without specific destination in
 * the given goods entry. Assert that at least one of those is empty.
 * @param ge Goods entry to swap with.
 */
static void SwapPackets(GoodsEntry *ge)
{
	StationCargoPacketMap &ge_packets = const_cast<StationCargoPacketMap &>(*ge->cargo.Packets());

	if (_packets.empty()) {
		std::map<StationID, std::list<CargoPacket *> >::iterator it(ge_packets.find(INVALID_STATION));
		if (it == ge_packets.end()) {
			return;
		} else {
			it->second.swap(_packets);
		}
	} else {
		assert(ge_packets[INVALID_STATION].empty());
		ge_packets[INVALID_STATION].swap(_packets);
	}
}

static void Load_STNS()
{
	_cargo_source_xy = 0;
	_cargo_days = 0;
	_cargo_feeder_share = 0;

	uint num_cargo = IsSavegameVersionBefore(SLV_55) ? 12 : IsSavegameVersionBefore(SLV_EXTEND_CARGOTYPES) ? 32 : NUM_CARGO;
	int index;
	while ((index = SlIterateArray()) != -1) {
		Station *st = new (index) Station();

		SlObject(st, _old_station_desc);

		_waiting_acceptance = 0;

		for (CargoID i = 0; i < num_cargo; i++) {
			GoodsEntry *ge = &st->goods[i];
			SlObject(ge, GetGoodsDesc());
			SwapPackets(ge);
			if (IsSavegameVersionBefore(SLV_68)) {
				SB(ge->status, GoodsEntry::GES_ACCEPTANCE, 1, HasBit(_waiting_acceptance, 15));
				if (GB(_waiting_acceptance, 0, 12) != 0) {
					/* In old versions, enroute_from used 0xFF as INVALID_STATION */
					StationID source = (IsSavegameVersionBefore(SLV_7) && _cargo_source == 0xFF) ? INVALID_STATION : _cargo_source;

					/* Make sure we can allocate the CargoPacket. This is safe
					 * as there can only be ~64k stations and 32 cargoes in these
					 * savegame versions. As the CargoPacketPool has more than
					 * 16 million entries; it fits by an order of magnitude. */
					assert(CargoPacket::CanAllocateItem());

					/* Don't construct the packet with station here, because that'll fail with old savegames */
					CargoPacket *cp = new CargoPacket(GB(_waiting_acceptance, 0, 12), _cargo_days, source, _cargo_source_xy, _cargo_source_xy, _cargo_feeder_share);
					ge->cargo.Append(cp, INVALID_STATION);
					SB(ge->status, GoodsEntry::GES_RATING, 1, 1);
				}
			}
		}

		if (st->num_specs != 0) {
			/* Allocate speclist memory when loading a game */
			st->speclist = CallocT<StationSpecList>(st->num_specs);
			for (uint i = 0; i < st->num_specs; i++) {
				SlObject(&st->speclist[i], _station_speclist_desc);
			}
		}
	}
}

static void Ptrs_STNS()
{
	/* Don't run when savegame version is higher than or equal to 123. */
	if (!IsSavegameVersionBefore(SLV_123)) return;

	uint num_cargo = IsSavegameVersionBefore(SLV_EXTEND_CARGOTYPES) ? 32 : NUM_CARGO;
	for (Station *st : Station::Iterate()) {
		if (!IsSavegameVersionBefore(SLV_68)) {
			for (CargoID i = 0; i < num_cargo; i++) {
				GoodsEntry *ge = &st->goods[i];
				SwapPackets(ge);
				SlObject(ge, GetGoodsDesc());
				SwapPackets(ge);
			}
		}
		SlObject(st, _old_station_desc);
	}
}


static const SaveLoad _base_station_desc[] = {
	      SLE_VAR(BaseStation, xy,                     SLE_UINT32),
	      SLE_REF(BaseStation, town,                   REF_TOWN),
	      SLE_VAR(BaseStation, string_id,              SLE_STRINGID),
	     SLE_SSTR(BaseStation, name,                   SLE_STR | SLF_ALLOW_CONTROL),
	      SLE_VAR(BaseStation, delete_ctr,             SLE_UINT8),
	      SLE_VAR(BaseStation, owner,                  SLE_UINT8),
	      SLE_VAR(BaseStation, facilities,             SLE_UINT8),
	      SLE_VAR(BaseStation, build_date,             SLE_INT32),

	/* Used by newstations for graphic variations */
	      SLE_VAR(BaseStation, random_bits,            SLE_UINT16),
	      SLE_VAR(BaseStation, waiting_triggers,       SLE_UINT8),
	      SLE_VAR(BaseStation, num_specs,              SLE_UINT8),

	      SLE_END()
};

static OldPersistentStorage _old_st_persistent_storage;

static const SaveLoad _station_desc[] = {
	SLE_WRITEBYTE(Station, facilities),
	SLE_ST_INCLUDE(),

	      SLE_VAR(Station, train_station.tile,         SLE_UINT32),
	      SLE_VAR(Station, train_station.w,            SLE_FILE_U8 | SLE_VAR_U16),
	      SLE_VAR(Station, train_station.h,            SLE_FILE_U8 | SLE_VAR_U16),

	      SLE_REF(Station, bus_stops,                  REF_ROADSTOPS),
	      SLE_REF(Station, truck_stops,                REF_ROADSTOPS),
	 SLE_CONDNULL(4, SL_MIN_VERSION, SLV_MULTITILE_DOCKS),
	  SLE_CONDVAR(Station, ship_station.tile,          SLE_UINT32,                SLV_MULTITILE_DOCKS, SL_MAX_VERSION),
	  SLE_CONDVAR(Station, ship_station.w,             SLE_FILE_U8 | SLE_VAR_U16, SLV_MULTITILE_DOCKS, SL_MAX_VERSION),
	  SLE_CONDVAR(Station, ship_station.h,             SLE_FILE_U8 | SLE_VAR_U16, SLV_MULTITILE_DOCKS, SL_MAX_VERSION),
	  SLE_CONDVAR(Station, docking_station.tile,       SLE_UINT32,                SLV_MULTITILE_DOCKS, SL_MAX_VERSION),
	  SLE_CONDVAR(Station, docking_station.w,          SLE_FILE_U8 | SLE_VAR_U16, SLV_MULTITILE_DOCKS, SL_MAX_VERSION),
	  SLE_CONDVAR(Station, docking_station.h,          SLE_FILE_U8 | SLE_VAR_U16, SLV_MULTITILE_DOCKS, SL_MAX_VERSION),
	      SLE_VAR(Station, airport.tile,               SLE_UINT32),
	  SLE_CONDVAR(Station, airport.w,                  SLE_FILE_U8 | SLE_VAR_U16, SLV_140, SL_MAX_VERSION),
	  SLE_CONDVAR(Station, airport.h,                  SLE_FILE_U8 | SLE_VAR_U16, SLV_140, SL_MAX_VERSION),
	      SLE_VAR(Station, airport.type,               SLE_UINT8),
	  SLE_CONDVAR(Station, airport.layout,             SLE_UINT8,                 SLV_145, SL_MAX_VERSION),
	      SLE_VAR(Station, airport.flags,              SLE_UINT64),
	  SLE_CONDVAR(Station, airport.rotation,           SLE_UINT8,                 SLV_145, SL_MAX_VERSION),
	 SLEG_CONDARR(_old_st_persistent_storage.storage,  SLE_UINT32, 16,            SLV_145, SLV_161),
	  SLE_CONDREF(Station, airport.psa,                REF_STORAGE,               SLV_161, SL_MAX_VERSION),

	      SLE_VAR(Station, indtype,                    SLE_UINT8),

	      SLE_VAR(Station, time_since_load,            SLE_UINT8),
	      SLE_VAR(Station, time_since_unload,          SLE_UINT8),
	      SLE_VAR(Station, last_vehicle_type,          SLE_UINT8),
	      SLE_VAR(Station, had_vehicle_of_type,        SLE_UINT8),
	      SLE_LST(Station, loading_vehicles,           REF_VEHICLE),
	  SLE_CONDVAR(Station, always_accepted,            SLE_FILE_U32 | SLE_VAR_U64, SLV_127, SLV_EXTEND_CARGOTYPES),
	  SLE_CONDVAR(Station, always_accepted,            SLE_UINT64,                 SLV_EXTEND_CARGOTYPES, SL_MAX_VERSION),

	      SLE_END()
};

static const SaveLoad _waypoint_desc[] = {
	SLE_WRITEBYTE(Waypoint, facilities),
	SLE_ST_INCLUDE(),

	      SLE_VAR(Waypoint, town_cn,                   SLE_UINT16),

	  SLE_CONDVAR(Waypoint, train_station.tile,        SLE_UINT32,                  SLV_124, SL_MAX_VERSION),
	  SLE_CONDVAR(Waypoint, train_station.w,           SLE_FILE_U8 | SLE_VAR_U16,   SLV_124, SL_MAX_VERSION),
	  SLE_CONDVAR(Waypoint, train_station.h,           SLE_FILE_U8 | SLE_VAR_U16,   SLV_124, SL_MAX_VERSION),

	      SLE_END()
};

/**
 * Get the base station description to be used for SL_ST_INCLUDE
 * @return the base station description.
 */
const SaveLoad *GetBaseStationDescription()
{
	return _base_station_desc;
}

static void RealSave_STNN(BaseStation *bst)
{
	bool waypoint = (bst->facilities & FACIL_WAYPOINT) != 0;
	SlObject(bst, waypoint ? _waypoint_desc : _station_desc);

	if (!waypoint) {
		Station *st = Station::From(bst);
		for (CargoID i = 0; i < NUM_CARGO; i++) {
			_num_dests = (uint32)st->goods[i].cargo.Packets()->MapSize();
			_num_flows = 0;
			for (FlowStatMap::const_iterator it(st->goods[i].flows.begin()); it != st->goods[i].flows.end(); ++it) {
				_num_flows += (uint32)it->second.GetShares()->size();
			}
			SlObject(&st->goods[i], GetGoodsDesc());
			for (FlowStatMap::const_iterator outer_it(st->goods[i].flows.begin()); outer_it != st->goods[i].flows.end(); ++outer_it) {
				const FlowStat::SharesMap *shares = outer_it->second.GetShares();
				uint32 sum_shares = 0;
				FlowSaveLoad flow;
				flow.source = outer_it->first;
				for (FlowStat::SharesMap::const_iterator inner_it(shares->begin()); inner_it != shares->end(); ++inner_it) {
					flow.via = inner_it->second;
					flow.share = inner_it->first - sum_shares;
					flow.restricted = inner_it->first > outer_it->second.GetUnrestricted();
					sum_shares = inner_it->first;
					assert(flow.share > 0);
					SlObject(&flow, _flow_desc);
				}
			}
			for (StationCargoPacketMap::ConstMapIterator it(st->goods[i].cargo.Packets()->begin()); it != st->goods[i].cargo.Packets()->end(); ++it) {
				SlObject(const_cast<StationCargoPacketMap::value_type *>(&(*it)), _cargo_list_desc);
			}
		}
	}

	for (uint i = 0; i < bst->num_specs; i++) {
		SlObject(&bst->speclist[i], _station_speclist_desc);
	}
}

static void Save_STNN()
{
	/* Write the stations */
	for (BaseStation *st : BaseStation::Iterate()) {
		SlSetArrayIndex(st->index);
		SlAutolength((AutolengthProc*)RealSave_STNN, st);
	}
}

static void Load_STNN()
{
	_num_flows = 0;

	uint num_cargo = IsSavegameVersionBefore(SLV_EXTEND_CARGOTYPES) ? 32 : NUM_CARGO;
	int index;
	while ((index = SlIterateArray()) != -1) {
		bool waypoint = (SlReadByte() & FACIL_WAYPOINT) != 0;

		BaseStation *bst = waypoint ? (BaseStation *)new (index) Waypoint() : new (index) Station();
		SlObject(bst, waypoint ? _waypoint_desc : _station_desc);

		if (!waypoint) {
			Station *st = Station::From(bst);

			/* Before savegame version 161, persistent storages were not stored in a pool. */
			if (IsSavegameVersionBefore(SLV_161) && !IsSavegameVersionBefore(SLV_145) && st->facilities & FACIL_AIRPORT) {
				/* Store the old persistent storage. The GRFID will be added later. */
				assert(PersistentStorage::CanAllocateItem());
				st->airport.psa = new PersistentStorage(0, 0, 0);
				memcpy(st->airport.psa->storage, _old_st_persistent_storage.storage, sizeof(_old_st_persistent_storage.storage));
			}

			for (CargoID i = 0; i < num_cargo; i++) {
				SlObject(&st->goods[i], GetGoodsDesc());
				FlowSaveLoad flow;
				FlowStat *fs = nullptr;
				StationID prev_source = INVALID_STATION;
				for (uint32 j = 0; j < _num_flows; ++j) {
					SlObject(&flow, _flow_desc);
					if (fs == nullptr || prev_source != flow.source) {
						fs = &(st->goods[i].flows.insert(std::make_pair(flow.source, FlowStat(flow.via, flow.share, flow.restricted))).first->second);
					} else {
						fs->AppendShare(flow.via, flow.share, flow.restricted);
					}
					prev_source = flow.source;
				}
				if (IsSavegameVersionBefore(SLV_183)) {
					SwapPackets(&st->goods[i]);
				} else {
					StationCargoPair pair;
					for (uint j = 0; j < _num_dests; ++j) {
						SlObject(&pair, _cargo_list_desc);
						const_cast<StationCargoPacketMap &>(*(st->goods[i].cargo.Packets()))[pair.first].swap(pair.second);
						assert(pair.second.empty());
					}
				}
			}
		}

		if (bst->num_specs != 0) {
			/* Allocate speclist memory when loading a game */
			bst->speclist = CallocT<StationSpecList>(bst->num_specs);
			for (uint i = 0; i < bst->num_specs; i++) {
				SlObject(&bst->speclist[i], _station_speclist_desc);
			}
		}
	}
}

static void Ptrs_STNN()
{
	/* Don't run when savegame version lower than 123. */
	if (IsSavegameVersionBefore(SLV_123)) return;

	uint num_cargo = IsSavegameVersionBefore(SLV_EXTEND_CARGOTYPES) ? 32 : NUM_CARGO;
	for (Station *st : Station::Iterate()) {
		for (CargoID i = 0; i < num_cargo; i++) {
			GoodsEntry *ge = &st->goods[i];
			if (IsSavegameVersionBefore(SLV_183)) {
				SwapPackets(ge);
				SlObject(ge, GetGoodsDesc());
				SwapPackets(ge);
			} else {
				SlObject(ge, GetGoodsDesc());
				for (StationCargoPacketMap::ConstMapIterator it = ge->cargo.Packets()->begin(); it != ge->cargo.Packets()->end(); ++it) {
					SlObject(const_cast<StationCargoPair *>(&(*it)), _cargo_list_desc);
				}
			}
		}
		SlObject(st, _station_desc);
	}

	for (Waypoint *wp : Waypoint::Iterate()) {
		SlObject(wp, _waypoint_desc);
	}
}

static void Save_ROADSTOP()
{
	for (RoadStop *rs : RoadStop::Iterate()) {
		SlSetArrayIndex(rs->index);
		SlObject(rs, _roadstop_desc);
	}
}

static void Load_ROADSTOP()
{
	int index;

	while ((index = SlIterateArray()) != -1) {
		RoadStop *rs = new (index) RoadStop(INVALID_TILE);

		SlObject(rs, _roadstop_desc);
	}
}

static void Ptrs_ROADSTOP()
{
	for (RoadStop *rs : RoadStop::Iterate()) {
		SlObject(rs, _roadstop_desc);
	}
}

extern const ChunkHandler _station_chunk_handlers[] = {
	{ 'STNS', nullptr,       Load_STNS,     Ptrs_STNS,     nullptr, CH_ARRAY },
	{ 'STNN', Save_STNN,     Load_STNN,     Ptrs_STNN,     nullptr, CH_ARRAY },
	{ 'ROAD', Save_ROADSTOP, Load_ROADSTOP, Ptrs_ROADSTOP, nullptr, CH_ARRAY | CH_LAST},
};
