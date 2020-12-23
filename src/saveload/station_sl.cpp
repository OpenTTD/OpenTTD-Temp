/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file station_sl.cpp Code handling saving and loading of stations. */

#include "../stdafx.h"

#include "saveload.h"
#include "compat/station_sl_compat.h"

#include "../station_base.h"
#include "../waypoint_base.h"
#include "../roadstop_base.h"
#include "../vehicle_base.h"
#include "../newgrf_station.h"

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
			for (TileIndex t : train_st) {
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
	SLE_VAR(RoadStop, status,       SLE_UINT8),
	SLE_REF(RoadStop, next,         REF_ROADSTOPS),
};

static uint16 _waiting_acceptance;
static uint32 _old_num_flows;
static uint16 _cargo_source;
static uint32 _cargo_source_xy;
static uint8  _cargo_days;
static Money  _cargo_feeder_share;

std::list<CargoPacket *> _packets;
uint32 _old_num_dests;

struct FlowSaveLoad {
	FlowSaveLoad() : source(0), via(0), share(0), restricted(false) {}
	StationID source;
	StationID via;
	uint32 share;
	bool restricted;
};

typedef std::pair<const StationID, std::list<CargoPacket *> > StationCargoPair;

static OldPersistentStorage _old_st_persistent_storage;

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

class SlStationSpecList : public DefaultSaveLoadHandler<SlStationSpecList, BaseStation> {
public:
	inline static const SaveLoad description[] = {
		SLE_CONDVAR(StationSpecList, grfid,    SLE_UINT32, SLV_27, SL_MAX_VERSION),
		SLE_CONDVAR(StationSpecList, localidx, SLE_UINT8,  SLV_27, SL_MAX_VERSION),
	};
	inline const static SaveLoadCompatTable compat_description = _station_spec_list_sl_compat;

	void Save(BaseStation *bst) const override
	{
		SlSetStructListLength(bst->num_specs);
		for (uint i = 0; i < bst->num_specs; i++) {
			SlObject(&bst->speclist[i], this->GetDescription());
		}
	}

	void Load(BaseStation *bst) const override
	{
		if (!IsSavegameVersionBefore(SLV_SAVELOAD_LIST_LENGTH)) {
			bst->num_specs = (uint8)SlGetStructListLength(UINT8_MAX);
		}

		if (bst->num_specs != 0) {
			/* Allocate speclist memory when loading a game */
			bst->speclist = CallocT<StationSpecList>(bst->num_specs);
			for (uint i = 0; i < bst->num_specs; i++) {
				SlObject(&bst->speclist[i], this->GetLoadDescription());
			}
		}
	}
};

class SlStationCargo : public DefaultSaveLoadHandler<SlStationCargo, GoodsEntry> {
public:
	inline static const SaveLoad description[] = {
		    SLE_VAR(StationCargoPair, first,  SLE_UINT16),
		SLE_REFLIST(StationCargoPair, second, REF_CARGO_PACKET),
	};
	inline const static SaveLoadCompatTable compat_description = _station_cargo_sl_compat;

	void Save(GoodsEntry *ge) const override
	{
		SlSetStructListLength(ge->cargo.Packets()->MapSize());
		for (StationCargoPacketMap::ConstMapIterator it(ge->cargo.Packets()->begin()); it != ge->cargo.Packets()->end(); ++it) {
			SlObject(const_cast<StationCargoPacketMap::value_type *>(&(*it)), this->GetDescription());
		}
	}

	void Load(GoodsEntry *ge) const override
	{
		size_t num_dests = IsSavegameVersionBefore(SLV_SAVELOAD_LIST_LENGTH) ? _old_num_dests : SlGetStructListLength(UINT32_MAX);

		StationCargoPair pair;
		for (uint j = 0; j < num_dests; ++j) {
			SlObject(&pair, this->GetLoadDescription());
			const_cast<StationCargoPacketMap &>(*(ge->cargo.Packets()))[pair.first].swap(pair.second);
			assert(pair.second.empty());
		}
	}

	void FixPointers(GoodsEntry *ge) const override
	{
		for (StationCargoPacketMap::ConstMapIterator it = ge->cargo.Packets()->begin(); it != ge->cargo.Packets()->end(); ++it) {
			SlObject(const_cast<StationCargoPair *>(&(*it)), this->GetDescription());
		}
	}
};

class SlStationFlow : public DefaultSaveLoadHandler<SlStationFlow, GoodsEntry> {
public:
	inline static const SaveLoad description[] = {
		    SLE_VAR(FlowSaveLoad, source,     SLE_UINT16),
		    SLE_VAR(FlowSaveLoad, via,        SLE_UINT16),
		    SLE_VAR(FlowSaveLoad, share,      SLE_UINT32),
		SLE_CONDVAR(FlowSaveLoad, restricted, SLE_BOOL, SLV_187, SL_MAX_VERSION),
	};
	inline const static SaveLoadCompatTable compat_description = _station_flow_sl_compat;

	void Save(GoodsEntry *ge) const override
	{
		uint32 num_flows = 0;
		for (FlowStatMap::const_iterator it(ge->flows.begin()); it != ge->flows.end(); ++it) {
			num_flows += (uint32)it->second.GetShares()->size();
		}
		SlSetStructListLength(num_flows);

		for (FlowStatMap::const_iterator outer_it(ge->flows.begin()); outer_it != ge->flows.end(); ++outer_it) {
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
				SlObject(&flow, this->GetDescription());
			}
		}
	}

	void Load(GoodsEntry *ge) const override
	{
		size_t num_flows = IsSavegameVersionBefore(SLV_SAVELOAD_LIST_LENGTH) ? _old_num_flows : SlGetStructListLength(UINT32_MAX);

		FlowSaveLoad flow;
		FlowStat *fs = nullptr;
		StationID prev_source = INVALID_STATION;
		for (uint32 j = 0; j < num_flows; ++j) {
			SlObject(&flow, this->GetLoadDescription());
			if (fs == nullptr || prev_source != flow.source) {
				fs = &(ge->flows.insert(std::make_pair(flow.source, FlowStat(flow.via, flow.share, flow.restricted))).first->second);
			} else {
				fs->AppendShare(flow.via, flow.share, flow.restricted);
			}
			prev_source = flow.source;
		}
	}
};

class SlStationGoods : public DefaultSaveLoadHandler<SlStationGoods, BaseStation> {
public:
#if defined(_MSC_VER) && (_MSC_VER == 1915 || _MSC_VER == 1916)
	/* This table access private members of other classes; they have this
	 * class as friend. For MSVC CL 19.15 and 19.16 this doesn't work for
	 * "inline static const", so we are forced to wrap the table in a
	 * function. CL 19.16 is the latest for VS2017. */
	inline static const SaveLoad description[] = {{}};
	SaveLoadTable GetDescription() const override {
#else
	inline
#endif
	static const SaveLoad description[] = {
		SLEG_CONDVAR("waiting_acceptance", _waiting_acceptance, SLE_UINT16,        SL_MIN_VERSION, SLV_68),
		 SLE_CONDVAR(GoodsEntry, status,               SLE_UINT8,                  SLV_68, SL_MAX_VERSION),
		     SLE_VAR(GoodsEntry, time_since_pickup,    SLE_UINT8),
		     SLE_VAR(GoodsEntry, rating,               SLE_UINT8),
		SLEG_CONDVAR("cargo_source", _cargo_source,    SLE_FILE_U8 | SLE_VAR_U16,   SL_MIN_VERSION, SLV_7),
		SLEG_CONDVAR("cargo_source", _cargo_source,    SLE_UINT16,                  SLV_7, SLV_68),
		SLEG_CONDVAR("cargo_source_xy", _cargo_source_xy, SLE_UINT32,               SLV_44, SLV_68),
		SLEG_CONDVAR("cargo_days", _cargo_days,        SLE_UINT8,                   SL_MIN_VERSION, SLV_68),
		     SLE_VAR(GoodsEntry, last_speed,           SLE_UINT8),
		     SLE_VAR(GoodsEntry, last_age,             SLE_UINT8),
		SLEG_CONDVAR("cargo_feeder_share", _cargo_feeder_share,  SLE_FILE_U32 | SLE_VAR_I64, SLV_14, SLV_65),
		SLEG_CONDVAR("cargo_feeder_share", _cargo_feeder_share,  SLE_INT64,                  SLV_65, SLV_68),
		 SLE_CONDVAR(GoodsEntry, amount_fract,         SLE_UINT8,                 SLV_150, SL_MAX_VERSION),
		SLEG_CONDREFLIST("packets", _packets,          REF_CARGO_PACKET,           SLV_68, SLV_183),
		SLEG_CONDVAR("old_num_dests", _old_num_dests,  SLE_UINT32,                SLV_183, SLV_SAVELOAD_LIST_LENGTH),
		 SLE_CONDVAR(GoodsEntry, cargo.reserved_count, SLE_UINT,                  SLV_181, SL_MAX_VERSION),
		 SLE_CONDVAR(GoodsEntry, link_graph,           SLE_UINT16,                SLV_183, SL_MAX_VERSION),
		 SLE_CONDVAR(GoodsEntry, node,                 SLE_UINT16,                SLV_183, SL_MAX_VERSION),
		SLEG_CONDVAR("old_num_flows", _old_num_flows,  SLE_UINT32,                SLV_183, SLV_SAVELOAD_LIST_LENGTH),
		 SLE_CONDVAR(GoodsEntry, max_waiting_cargo,    SLE_UINT32,                SLV_183, SL_MAX_VERSION),
		SLEG_CONDSTRUCTLIST("flow", SlStationFlow,                                SLV_183, SL_MAX_VERSION),
		SLEG_CONDSTRUCTLIST("cargo", SlStationCargo,                              SLV_183, SL_MAX_VERSION),
	};
#if defined(_MSC_VER) && (_MSC_VER == 1915 || _MSC_VER == 1916)
		return description;
	}
#endif
	inline const static SaveLoadCompatTable compat_description = _station_goods_sl_compat;

	/**
	 * Get the number of cargoes used by this savegame version.
	 * @return The number of cargoes used by this savegame version.
	 */
	size_t GetNumCargo() const
	{
		if (IsSavegameVersionBefore(SLV_55)) return 12;
		if (IsSavegameVersionBefore(SLV_EXTEND_CARGOTYPES)) return 32;
		if (IsSavegameVersionBefore(SLV_SAVELOAD_LIST_LENGTH)) return NUM_CARGO;
		/* Read from the savegame how long the list is. */
		return SlGetStructListLength(NUM_CARGO);
	}

	void Save(BaseStation *bst) const override
	{
		Station *st = Station::From(bst);

		SlSetStructListLength(NUM_CARGO);

		for (CargoID i = 0; i < NUM_CARGO; i++) {
			SlObject(&st->goods[i], this->GetDescription());
		}
	}

	void Load(BaseStation *bst) const override
	{
		Station *st = Station::From(bst);

		/* Before savegame version 161, persistent storages were not stored in a pool. */
		if (IsSavegameVersionBefore(SLV_161) && !IsSavegameVersionBefore(SLV_145) && st->facilities & FACIL_AIRPORT) {
			/* Store the old persistent storage. The GRFID will be added later. */
			assert(PersistentStorage::CanAllocateItem());
			st->airport.psa = new PersistentStorage(0, 0, 0);
			memcpy(st->airport.psa->storage, _old_st_persistent_storage.storage, sizeof(_old_st_persistent_storage.storage));
		}

		size_t num_cargo = this->GetNumCargo();
		for (CargoID i = 0; i < num_cargo; i++) {
			GoodsEntry *ge = &st->goods[i];
			SlObject(ge, this->GetLoadDescription());
			if (IsSavegameVersionBefore(SLV_183)) {
				SwapPackets(ge);
			}
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
	}

	void FixPointers(BaseStation *bst) const override
	{
		Station *st = Station::From(bst);

		uint num_cargo = IsSavegameVersionBefore(SLV_55) ? 12 : IsSavegameVersionBefore(SLV_EXTEND_CARGOTYPES) ? 32 : NUM_CARGO;
		for (CargoID i = 0; i < num_cargo; i++) {
			GoodsEntry *ge = &st->goods[i];
			if (IsSavegameVersionBefore(SLV_183)) {
				SwapPackets(ge); // We have to swap back again to be in the format pre-183 expects.
				SlObject(ge, this->GetDescription());
				SwapPackets(ge);
			} else {
				SlObject(ge, this->GetDescription());
			}
		}
	}
};

static const SaveLoad _old_station_desc[] = {
	SLE_CONDVAR(Station, xy,                         SLE_FILE_U16 | SLE_VAR_U32,  SL_MIN_VERSION, SLV_6),
	SLE_CONDVAR(Station, xy,                         SLE_UINT32,                  SLV_6, SL_MAX_VERSION),
	SLE_CONDVAR(Station, train_station.tile,         SLE_FILE_U16 | SLE_VAR_U32,  SL_MIN_VERSION, SLV_6),
	SLE_CONDVAR(Station, train_station.tile,         SLE_UINT32,                  SLV_6, SL_MAX_VERSION),
	SLE_CONDVAR(Station, airport.tile,               SLE_FILE_U16 | SLE_VAR_U32,  SL_MIN_VERSION, SLV_6),
	SLE_CONDVAR(Station, airport.tile,               SLE_UINT32,                  SLV_6, SL_MAX_VERSION),
	    SLE_REF(Station, town,                       REF_TOWN),
	    SLE_VAR(Station, train_station.w,            SLE_FILE_U8 | SLE_VAR_U16),
	SLE_CONDVAR(Station, train_station.h,            SLE_FILE_U8 | SLE_VAR_U16,   SLV_2, SL_MAX_VERSION),

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
	SLE_CONDVAR(Station, airport.flags,              SLE_VAR_U64 | SLE_FILE_U16,  SL_MIN_VERSION,  SLV_3),
	SLE_CONDVAR(Station, airport.flags,              SLE_VAR_U64 | SLE_FILE_U32,  SLV_3, SLV_46),
	SLE_CONDVAR(Station, airport.flags,              SLE_UINT64,                 SLV_46, SL_MAX_VERSION),

	SLE_CONDVAR(Station, last_vehicle_type,          SLE_UINT8,                  SLV_26, SL_MAX_VERSION),

	SLE_CONDVAR(Station, build_date,                 SLE_FILE_U16 | SLE_VAR_I32,  SLV_3, SLV_31),
	SLE_CONDVAR(Station, build_date,                 SLE_INT32,                  SLV_31, SL_MAX_VERSION),

	SLE_CONDREF(Station, bus_stops,                  REF_ROADSTOPS,               SLV_6, SL_MAX_VERSION),
	SLE_CONDREF(Station, truck_stops,                REF_ROADSTOPS,               SLV_6, SL_MAX_VERSION),

	/* Used by newstations for graphic variations */
	SLE_CONDVAR(Station, random_bits,                SLE_UINT16,                 SLV_27, SL_MAX_VERSION),
	SLE_CONDVAR(Station, waiting_triggers,           SLE_UINT8,                  SLV_27, SL_MAX_VERSION),
	SLE_CONDVAR(Station, num_specs,                  SLE_UINT8,                  SLV_27, SL_MAX_VERSION),

	SLE_CONDREFLIST(Station, loading_vehicles,       REF_VEHICLE,                SLV_57, SL_MAX_VERSION),

	SLEG_STRUCTLIST("goods", SlStationGoods),
	SLEG_CONDSTRUCTLIST("speclist", SlStationSpecList,                           SLV_27, SL_MAX_VERSION),
};

struct STNSChunkHandler : ChunkHandler {
	STNSChunkHandler() : ChunkHandler('STNS', CH_READONLY) {}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlCompatTableHeader(_old_station_desc, _old_station_sl_compat);

		_cargo_source_xy = 0;
		_cargo_days = 0;
		_cargo_feeder_share = 0;

		int index;
		while ((index = SlIterateArray()) != -1) {
			Station *st = new (index) Station();

			_waiting_acceptance = 0;
			SlObject(st, slt);
		}
	}

	void FixPointers() const override
	{
		/* From SLV_123 we store stations in STNN; before that in STNS. So do not
		 * fix pointers when the version is SLV_123 or up, as that would fix
		 * pointers twice: once in STNN chunk and once here. */
		if (!IsSavegameVersionBefore(SLV_123)) return;

		for (Station *st : Station::Iterate()) {
			SlObject(st, _old_station_desc);
		}
	}
};

/**
 * SaveLoad handler for the BaseStation, which all other stations / waypoints
 * make use of.
 */
class SlStationBase : public DefaultSaveLoadHandler<SlStationBase, BaseStation> {
public:
	inline static const SaveLoad description[] = {
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
		SLE_CONDVAR(BaseStation, num_specs,              SLE_UINT8,                   SL_MIN_VERSION, SLV_SAVELOAD_LIST_LENGTH),
	};
	inline const static SaveLoadCompatTable compat_description = _station_base_sl_compat;

	void Save(BaseStation *bst) const override
	{
		SlObject(bst, this->GetDescription());
	}

	void Load(BaseStation *bst) const override
	{
		SlObject(bst, this->GetLoadDescription());
	}

	void FixPointers(BaseStation *bst) const override
	{
		SlObject(bst, this->GetDescription());
	}
};

/**
 * SaveLoad handler for a normal station (read: not a waypoint).
 */
class SlStationNormal : public DefaultSaveLoadHandler<SlStationNormal, BaseStation> {
public:
	inline static const SaveLoad description[] = {
		SLEG_STRUCT("base", SlStationBase),
		    SLE_VAR(Station, train_station.tile,         SLE_UINT32),
		    SLE_VAR(Station, train_station.w,            SLE_FILE_U8 | SLE_VAR_U16),
		    SLE_VAR(Station, train_station.h,            SLE_FILE_U8 | SLE_VAR_U16),

		    SLE_REF(Station, bus_stops,                  REF_ROADSTOPS),
		    SLE_REF(Station, truck_stops,                REF_ROADSTOPS),
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
		SLE_CONDVAR(Station, airport.depot_id,           SLE_UINT16,   SLV_MULTITILE_DEPOTS, SL_MAX_VERSION),
		SLEG_CONDARR("storage", _old_st_persistent_storage.storage,  SLE_UINT32, 16, SLV_145, SLV_161),
		SLE_CONDREF(Station, airport.psa,                REF_STORAGE,               SLV_161, SL_MAX_VERSION),

		    SLE_VAR(Station, indtype,                    SLE_UINT8),

		    SLE_VAR(Station, time_since_load,            SLE_UINT8),
		    SLE_VAR(Station, time_since_unload,          SLE_UINT8),
		    SLE_VAR(Station, last_vehicle_type,          SLE_UINT8),
		    SLE_VAR(Station, had_vehicle_of_type,        SLE_UINT8),
		SLE_REFLIST(Station, loading_vehicles,           REF_VEHICLE),
		SLE_CONDVAR(Station, always_accepted,            SLE_FILE_U32 | SLE_VAR_U64, SLV_127, SLV_EXTEND_CARGOTYPES),
		SLE_CONDVAR(Station, always_accepted,            SLE_UINT64,                 SLV_EXTEND_CARGOTYPES, SL_MAX_VERSION),
		SLEG_STRUCTLIST("goods", SlStationGoods),
	};
	inline const static SaveLoadCompatTable compat_description = _station_normal_sl_compat;

	void Save(BaseStation *bst) const
	{
		if ((bst->facilities & FACIL_WAYPOINT) != 0) return;
		SlObject(bst, this->GetDescription());
	}

	void Load(BaseStation *bst) const
	{
		if ((bst->facilities & FACIL_WAYPOINT) != 0) return;
		SlObject(bst, this->GetLoadDescription());
	}

	void FixPointers(BaseStation *bst) const
	{
		if ((bst->facilities & FACIL_WAYPOINT) != 0) return;
		SlObject(bst, this->GetDescription());
	}
};

class SlStationWaypoint : public DefaultSaveLoadHandler<SlStationWaypoint, BaseStation> {
public:
	inline static const SaveLoad description[] = {
		SLEG_STRUCT("base", SlStationBase),
		    SLE_VAR(Waypoint, town_cn,                   SLE_UINT16),

		SLE_CONDVAR(Waypoint, train_station.tile,        SLE_UINT32,                  SLV_124, SL_MAX_VERSION),
		SLE_CONDVAR(Waypoint, train_station.w,           SLE_FILE_U8 | SLE_VAR_U16,   SLV_124, SL_MAX_VERSION),
		SLE_CONDVAR(Waypoint, train_station.h,           SLE_FILE_U8 | SLE_VAR_U16,   SLV_124, SL_MAX_VERSION),
	};
	inline const static SaveLoadCompatTable compat_description = _station_waypoint_sl_compat;

	void Save(BaseStation *bst) const
	{
		if ((bst->facilities & FACIL_WAYPOINT) == 0) return;
		SlObject(bst, this->GetDescription());
	}

	void Load(BaseStation *bst) const
	{
		if ((bst->facilities & FACIL_WAYPOINT) == 0) return;
		SlObject(bst, this->GetLoadDescription());
	}

	void FixPointers(BaseStation *bst) const
	{
		if ((bst->facilities & FACIL_WAYPOINT) == 0) return;
		SlObject(bst, this->GetDescription());
	}
};

static const SaveLoad _station_desc[] = {
	SLE_SAVEBYTE(BaseStation, facilities),
	SLEG_STRUCT("normal", SlStationNormal),
	SLEG_STRUCT("waypoint", SlStationWaypoint),
	SLEG_CONDSTRUCTLIST("speclist", SlStationSpecList, SLV_27, SL_MAX_VERSION),
};

struct STNNChunkHandler : ChunkHandler {
	STNNChunkHandler() : ChunkHandler('STNN', CH_TABLE) {}

	void Save() const override
	{
		SlTableHeader(_station_desc);

		/* Write the stations */
		for (BaseStation *st : BaseStation::Iterate()) {
			SlSetArrayIndex(st->index);
			SlObject(st, _station_desc);
		}
	}


	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlCompatTableHeader(_station_desc, _station_sl_compat);

		_old_num_flows = 0;

		int index;
		while ((index = SlIterateArray()) != -1) {
			bool waypoint = (SlReadByte() & FACIL_WAYPOINT) != 0;

			BaseStation *bst = waypoint ? (BaseStation *)new (index) Waypoint() : new (index) Station();
			SlObject(bst, slt);
		}
	}

	void FixPointers() const override
	{
		/* From SLV_123 we store stations in STNN; before that in STNS. So do not
		 * fix pointers when the version is below SLV_123, as that would fix
		 * pointers twice: once in STNS chunk and once here. */
		if (IsSavegameVersionBefore(SLV_123)) return;

		for (BaseStation *bst : BaseStation::Iterate()) {
			SlObject(bst, _station_desc);
		}
	}
};

struct ROADChunkHandler : ChunkHandler {
	ROADChunkHandler() : ChunkHandler('ROAD', CH_TABLE) {}

	void Save() const override
	{
		SlTableHeader(_roadstop_desc);

		for (RoadStop *rs : RoadStop::Iterate()) {
			SlSetArrayIndex(rs->index);
			SlObject(rs, _roadstop_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlCompatTableHeader(_roadstop_desc, _roadstop_sl_compat);

		int index;

		while ((index = SlIterateArray()) != -1) {
			RoadStop *rs = new (index) RoadStop(INVALID_TILE);

			SlObject(rs, slt);
		}
	}

	void FixPointers() const override
	{
		for (RoadStop *rs : RoadStop::Iterate()) {
			SlObject(rs, _roadstop_desc);
		}
	}
};

static const STNSChunkHandler STNS;
static const STNNChunkHandler STNN;
static const ROADChunkHandler ROAD;
static const ChunkHandlerRef station_chunk_handlers[] = {
	STNS,
	STNN,
	ROAD,
};

extern const ChunkHandlerTable _station_chunk_handlers(station_chunk_handlers);
