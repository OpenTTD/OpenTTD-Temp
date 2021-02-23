/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_debug_data.h Data 'tables' for NewGRF debugging. */

#include "../newgrf_house.h"
#include "../newgrf_engine.h"
#include "../newgrf_roadtype.h"

/* Helper for filling property tables */
#define NIP(prop, base, variable, type, name) { name, [] (const void *b) -> const void * { return std::addressof(static_cast<const base *>(b)->variable); }, cpp_sizeof(base, variable), prop, type }
#define NIP_END() { nullptr, 0, 0, 0, 0 }

/* Helper for filling callback tables */
#define NIC(cb_id, base, variable, bit) { #cb_id, [] (const void *b) -> const void * { return std::addressof(static_cast<const base *>(b)->variable); }, cpp_sizeof(base, variable), bit, cb_id }
#define NIC_END() { nullptr, 0, 0, 0, 0 }

/* Helper for filling variable tables */
#define NIV(var, name) { name, var }
#define NIV_END() { nullptr, 0 }


/*** NewGRF Vehicles ***/

#define NICV(cb_id, bit) NIC(cb_id, Engine, info.callback_mask, bit)
static const NICallback _nic_vehicles[] = {
	NICV(CBID_VEHICLE_VISUAL_EFFECT,         CBM_VEHICLE_VISUAL_EFFECT),
	NICV(CBID_VEHICLE_LENGTH,                CBM_VEHICLE_LENGTH),
	NICV(CBID_VEHICLE_LOAD_AMOUNT,           CBM_VEHICLE_LOAD_AMOUNT),
	NICV(CBID_VEHICLE_REFIT_CAPACITY,        CBM_VEHICLE_REFIT_CAPACITY),
	NICV(CBID_VEHICLE_ARTIC_ENGINE,          CBM_VEHICLE_ARTIC_ENGINE),
	NICV(CBID_VEHICLE_CARGO_SUFFIX,          CBM_VEHICLE_CARGO_SUFFIX),
	NICV(CBID_TRAIN_ALLOW_WAGON_ATTACH,      CBM_NO_BIT),
	NICV(CBID_VEHICLE_ADDITIONAL_TEXT,       CBM_NO_BIT),
	NICV(CBID_VEHICLE_COLOUR_MAPPING,        CBM_VEHICLE_COLOUR_REMAP),
	NICV(CBID_VEHICLE_START_STOP_CHECK,      CBM_NO_BIT),
	NICV(CBID_VEHICLE_32DAY_CALLBACK,        CBM_NO_BIT),
	NICV(CBID_VEHICLE_SOUND_EFFECT,          CBM_VEHICLE_SOUND_EFFECT),
	NICV(CBID_VEHICLE_AUTOREPLACE_SELECTION, CBM_NO_BIT),
	NICV(CBID_VEHICLE_MODIFY_PROPERTY,       CBM_NO_BIT),
	NIC_END()
};


static const NIVariable _niv_vehicles[] = {
	NIV(0x40, "position in consist and length"),
	NIV(0x41, "position and length of chain of same vehicles"),
	NIV(0x42, "transported cargo types"),
	NIV(0x43, "player info"),
	NIV(0x44, "aircraft info"),
	NIV(0x45, "curvature info"),
	NIV(0x46, "motion counter"),
	NIV(0x47, "vehicle cargo info"),
	NIV(0x48, "vehicle type info"),
	NIV(0x49, "year of construction"),
	NIV(0x4A, "current rail/road type info"),
	NIV(0x4B, "long date of last service"),
	NIV(0x4C, "current max speed"),
	NIV(0x4D, "position in articulated vehicle"),
	NIV(0x60, "count vehicle id occurrences"),
	// 0x61 not useful, since it requires register 0x10F
	NIV(0x62, "curvature/position difference to other vehicle"),
	NIV(0x63, "tile compatibility wrt. track-type"),
	NIV_END()
};

class NIHVehicle : public NIHelper {
	bool IsInspectable(uint index) const override        { return Vehicle::Get(index)->GetGRF() != nullptr; }
	uint GetParent(uint index) const override            { const Vehicle *first = Vehicle::Get(index)->First(); return GetInspectWindowNumber(GetGrfSpecFeature(first->type), first->index); }
	const void *GetInstance(uint index)const override    { return Vehicle::Get(index); }
	const void *GetSpec(uint index) const override       { return Vehicle::Get(index)->GetEngine(); }
	void SetStringParameters(uint index) const override  { this->SetSimpleStringParameters(STR_VEHICLE_NAME, index); }
	uint32 GetGRFID(uint index) const override                   { return Vehicle::Get(index)->GetGRFID(); }

	uint Resolve(uint index, uint var, uint param, bool *avail) const override
	{
		Vehicle *v = Vehicle::Get(index);
		VehicleResolverObject ro(v->engine_type, v, VehicleResolverObject::WO_CACHED);
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, avail);
	}
};

static const NIFeature _nif_vehicle = {
	nullptr,
	_nic_vehicles,
	_niv_vehicles,
	new NIHVehicle(),
};


/*** NewGRF station (tiles) ***/

#define NICS(cb_id, bit) NIC(cb_id, StationSpec, callback_mask, bit)
static const NICallback _nic_stations[] = {
	NICS(CBID_STATION_AVAILABILITY,     CBM_STATION_AVAIL),
	NICS(CBID_STATION_SPRITE_LAYOUT,    CBM_STATION_SPRITE_LAYOUT),
	NICS(CBID_STATION_TILE_LAYOUT,      CBM_NO_BIT),
	NICS(CBID_STATION_ANIM_START_STOP,  CBM_NO_BIT),
	NICS(CBID_STATION_ANIM_NEXT_FRAME,  CBM_STATION_ANIMATION_NEXT_FRAME),
	NICS(CBID_STATION_ANIMATION_SPEED,  CBM_STATION_ANIMATION_SPEED),
	NICS(CBID_STATION_LAND_SLOPE_CHECK, CBM_STATION_SLOPE_CHECK),
	NIC_END()
};

static const NIVariable _niv_stations[] = {
	NIV(0x40, "platform info and relative position"),
	NIV(0x41, "platform info and relative position for individually built sections"),
	NIV(0x42, "terrain and track type"),
	NIV(0x43, "player info"),
	NIV(0x44, "path signalling info"),
	NIV(0x45, "rail continuation info"),
	NIV(0x46, "platform info and relative position from middle"),
	NIV(0x47, "platform info and relative position from middle for individually built sections"),
	NIV(0x48, "bitmask of accepted cargoes"),
	NIV(0x49, "platform info and relative position of same-direction section"),
	NIV(0x4A, "current animation frame"),
	NIV(0x60, "amount of cargo waiting"),
	NIV(0x61, "time since last cargo pickup"),
	NIV(0x62, "rating of cargo"),
	NIV(0x63, "time spent on route"),
	NIV(0x64, "information about last vehicle picking cargo up"),
	NIV(0x65, "amount of cargo acceptance"),
	NIV(0x66, "animation frame of nearby tile"),
	NIV(0x67, "land info of nearby tiles"),
	NIV(0x68, "station info of nearby tiles"),
	NIV(0x69, "information about cargo accepted in the past"),
	NIV(0x6A, "GRFID of nearby station tiles"),
	NIV_END()
};

class NIHStation : public NIHelper {
	bool IsInspectable(uint index) const override        { return GetStationSpec(index) != nullptr; }
	uint GetParent(uint index) const override            { return GetInspectWindowNumber(GSF_FAKE_TOWNS, Station::GetByTile(index)->town->index); }
	const void *GetInstance(uint index)const override    { return nullptr; }
	const void *GetSpec(uint index) const override       { return GetStationSpec(index); }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_STATION_NAME, GetStationIndex(index), index); }
	uint32 GetGRFID(uint index) const override           { return (this->IsInspectable(index)) ? GetStationSpec(index)->grf_prop.grffile->grfid : 0; }

	uint Resolve(uint index, uint var, uint param, bool *avail) const override
	{
		StationResolverObject ro(GetStationSpec(index), Station::GetByTile(index), index);
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, avail);
	}
};

static const NIFeature _nif_station = {
	nullptr,
	_nic_stations,
	_niv_stations,
	new NIHStation(),
};


/*** NewGRF house tiles ***/

#define NICH(cb_id, bit) NIC(cb_id, HouseSpec, callback_mask, bit)
static const NICallback _nic_house[] = {
	NICH(CBID_HOUSE_ALLOW_CONSTRUCTION,        CBM_HOUSE_ALLOW_CONSTRUCTION),
	NICH(CBID_HOUSE_ANIMATION_NEXT_FRAME,      CBM_HOUSE_ANIMATION_NEXT_FRAME),
	NICH(CBID_HOUSE_ANIMATION_START_STOP,      CBM_HOUSE_ANIMATION_START_STOP),
	NICH(CBID_HOUSE_CONSTRUCTION_STATE_CHANGE, CBM_HOUSE_CONSTRUCTION_STATE_CHANGE),
	NICH(CBID_HOUSE_COLOUR,                    CBM_HOUSE_COLOUR),
	NICH(CBID_HOUSE_CARGO_ACCEPTANCE,          CBM_HOUSE_CARGO_ACCEPTANCE),
	NICH(CBID_HOUSE_ANIMATION_SPEED,           CBM_HOUSE_ANIMATION_SPEED),
	NICH(CBID_HOUSE_DESTRUCTION,               CBM_HOUSE_DESTRUCTION),
	NICH(CBID_HOUSE_ACCEPT_CARGO,              CBM_HOUSE_ACCEPT_CARGO),
	NICH(CBID_HOUSE_PRODUCE_CARGO,             CBM_HOUSE_PRODUCE_CARGO),
	NICH(CBID_HOUSE_DENY_DESTRUCTION,          CBM_HOUSE_DENY_DESTRUCTION),
	NICH(CBID_HOUSE_WATCHED_CARGO_ACCEPTED,    CBM_NO_BIT),
	NICH(CBID_HOUSE_CUSTOM_NAME,               CBM_NO_BIT),
	NICH(CBID_HOUSE_DRAW_FOUNDATIONS,          CBM_HOUSE_DRAW_FOUNDATIONS),
	NICH(CBID_HOUSE_AUTOSLOPE,                 CBM_HOUSE_AUTOSLOPE),
	NIC_END()
};

static const NIVariable _niv_house[] = {
	NIV(0x40, "construction state of tile and pseudo-random value"),
	NIV(0x41, "age of building in years"),
	NIV(0x42, "town zone"),
	NIV(0x43, "terrain type"),
	NIV(0x44, "building counts"),
	NIV(0x45, "town expansion bits"),
	NIV(0x46, "current animation frame"),
	NIV(0x47, "xy coordinate of the building"),
	NIV(0x60, "other building counts (old house type)"),
	NIV(0x61, "other building counts (new house type)"),
	NIV(0x62, "land info of nearby tiles"),
	NIV(0x63, "current animation frame of nearby house tile"),
	NIV(0x64, "cargo acceptance history of nearby stations"),
	NIV(0x65, "distance of nearest house matching a given criterion"),
	NIV(0x66, "class and ID of nearby house tile"),
	NIV(0x67, "GRFID of nearby house tile"),
	NIV_END()
};

class NIHHouse : public NIHelper {
	bool IsInspectable(uint index) const override        { return HouseSpec::Get(GetHouseType(index))->grf_prop.grffile != nullptr; }
	uint GetParent(uint index) const override            { return GetInspectWindowNumber(GSF_FAKE_TOWNS, GetTownIndex(index)); }
	const void *GetInstance(uint index)const override    { return nullptr; }
	const void *GetSpec(uint index) const override       { return HouseSpec::Get(GetHouseType(index)); }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_TOWN_NAME, GetTownIndex(index), index); }
	uint32 GetGRFID(uint index) const override           { return (this->IsInspectable(index)) ? HouseSpec::Get(GetHouseType(index))->grf_prop.grffile->grfid : 0; }

	uint Resolve(uint index, uint var, uint param, bool *avail) const override
	{
		HouseResolverObject ro(GetHouseType(index), index, Town::GetByTile(index));
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, avail);
	}
};

static const NIFeature _nif_house = {
	nullptr,
	_nic_house,
	_niv_house,
	new NIHHouse(),
};


/*** NewGRF industry tiles ***/

#define NICIT(cb_id, bit) NIC(cb_id, IndustryTileSpec, callback_mask, bit)
static const NICallback _nic_industrytiles[] = {
	NICIT(CBID_INDTILE_ANIM_START_STOP,  CBM_NO_BIT),
	NICIT(CBID_INDTILE_ANIM_NEXT_FRAME,  CBM_INDT_ANIM_NEXT_FRAME),
	NICIT(CBID_INDTILE_ANIMATION_SPEED,  CBM_INDT_ANIM_SPEED),
	NICIT(CBID_INDTILE_CARGO_ACCEPTANCE, CBM_INDT_CARGO_ACCEPTANCE),
	NICIT(CBID_INDTILE_ACCEPT_CARGO,     CBM_INDT_ACCEPT_CARGO),
	NICIT(CBID_INDTILE_SHAPE_CHECK,      CBM_INDT_SHAPE_CHECK),
	NICIT(CBID_INDTILE_DRAW_FOUNDATIONS, CBM_INDT_DRAW_FOUNDATIONS),
	NICIT(CBID_INDTILE_AUTOSLOPE,        CBM_INDT_AUTOSLOPE),
	NIC_END()
};

static const NIVariable _niv_industrytiles[] = {
	NIV(0x40, "construction state of tile"),
	NIV(0x41, "ground type"),
	NIV(0x42, "current town zone in nearest town"),
	NIV(0x43, "relative position"),
	NIV(0x44, "animation frame"),
	NIV(0x60, "land info of nearby tiles"),
	NIV(0x61, "animation stage of nearby tiles"),
	NIV(0x62, "get industry or airport tile ID at offset"),
	NIV_END()
};

class NIHIndustryTile : public NIHelper {
	bool IsInspectable(uint index) const override        { return GetIndustryTileSpec(GetIndustryGfx(index))->grf_prop.grffile != nullptr; }
	uint GetParent(uint index) const override            { return GetInspectWindowNumber(GSF_INDUSTRIES, GetIndustryIndex(index)); }
	const void *GetInstance(uint index)const override    { return nullptr; }
	const void *GetSpec(uint index) const override       { return GetIndustryTileSpec(GetIndustryGfx(index)); }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_INDUSTRY_NAME, GetIndustryIndex(index), index); }
	uint32 GetGRFID(uint index) const override           { return (this->IsInspectable(index)) ? GetIndustryTileSpec(GetIndustryGfx(index))->grf_prop.grffile->grfid : 0; }

	uint Resolve(uint index, uint var, uint param, bool *avail) const override
	{
		IndustryTileResolverObject ro(GetIndustryGfx(index), index, Industry::GetByTile(index));
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, avail);
	}
};

static const NIFeature _nif_industrytile = {
	nullptr,
	_nic_industrytiles,
	_niv_industrytiles,
	new NIHIndustryTile(),
};


/*** NewGRF industries ***/

static const NIProperty _nip_industries[] = {
	NIP(0x25, Industry, produced_cargo[ 0], NIT_CARGO, "produced cargo 0"),
	NIP(0x25, Industry, produced_cargo[ 1], NIT_CARGO, "produced cargo 1"),
	NIP(0x25, Industry, produced_cargo[ 2], NIT_CARGO, "produced cargo 2"),
	NIP(0x25, Industry, produced_cargo[ 3], NIT_CARGO, "produced cargo 3"),
	NIP(0x25, Industry, produced_cargo[ 4], NIT_CARGO, "produced cargo 4"),
	NIP(0x25, Industry, produced_cargo[ 5], NIT_CARGO, "produced cargo 5"),
	NIP(0x25, Industry, produced_cargo[ 6], NIT_CARGO, "produced cargo 6"),
	NIP(0x25, Industry, produced_cargo[ 7], NIT_CARGO, "produced cargo 7"),
	NIP(0x25, Industry, produced_cargo[ 8], NIT_CARGO, "produced cargo 8"),
	NIP(0x25, Industry, produced_cargo[ 9], NIT_CARGO, "produced cargo 9"),
	NIP(0x25, Industry, produced_cargo[10], NIT_CARGO, "produced cargo 10"),
	NIP(0x25, Industry, produced_cargo[11], NIT_CARGO, "produced cargo 11"),
	NIP(0x25, Industry, produced_cargo[12], NIT_CARGO, "produced cargo 12"),
	NIP(0x25, Industry, produced_cargo[13], NIT_CARGO, "produced cargo 13"),
	NIP(0x25, Industry, produced_cargo[14], NIT_CARGO, "produced cargo 14"),
	NIP(0x25, Industry, produced_cargo[15], NIT_CARGO, "produced cargo 15"),
	NIP(0x26, Industry, accepts_cargo[ 0],  NIT_CARGO, "accepted cargo 0"),
	NIP(0x26, Industry, accepts_cargo[ 1],  NIT_CARGO, "accepted cargo 1"),
	NIP(0x26, Industry, accepts_cargo[ 2],  NIT_CARGO, "accepted cargo 2"),
	NIP(0x26, Industry, accepts_cargo[ 3],  NIT_CARGO, "accepted cargo 3"),
	NIP(0x26, Industry, accepts_cargo[ 4],  NIT_CARGO, "accepted cargo 4"),
	NIP(0x26, Industry, accepts_cargo[ 5],  NIT_CARGO, "accepted cargo 5"),
	NIP(0x26, Industry, accepts_cargo[ 6],  NIT_CARGO, "accepted cargo 6"),
	NIP(0x26, Industry, accepts_cargo[ 7],  NIT_CARGO, "accepted cargo 7"),
	NIP(0x26, Industry, accepts_cargo[ 8],  NIT_CARGO, "accepted cargo 8"),
	NIP(0x26, Industry, accepts_cargo[ 9],  NIT_CARGO, "accepted cargo 9"),
	NIP(0x26, Industry, accepts_cargo[10],  NIT_CARGO, "accepted cargo 10"),
	NIP(0x26, Industry, accepts_cargo[11],  NIT_CARGO, "accepted cargo 11"),
	NIP(0x26, Industry, accepts_cargo[12],  NIT_CARGO, "accepted cargo 12"),
	NIP(0x26, Industry, accepts_cargo[13],  NIT_CARGO, "accepted cargo 13"),
	NIP(0x26, Industry, accepts_cargo[14],  NIT_CARGO, "accepted cargo 14"),
	NIP(0x26, Industry, accepts_cargo[15],  NIT_CARGO, "accepted cargo 15"),
	NIP_END()
};

#define NICI(cb_id, bit) NIC(cb_id, IndustrySpec, callback_mask, bit)
static const NICallback _nic_industries[] = {
	NICI(CBID_INDUSTRY_PROBABILITY,          CBM_IND_PROBABILITY),
	NICI(CBID_INDUSTRY_LOCATION,             CBM_IND_LOCATION),
	NICI(CBID_INDUSTRY_PRODUCTION_CHANGE,    CBM_IND_PRODUCTION_CHANGE),
	NICI(CBID_INDUSTRY_MONTHLYPROD_CHANGE,   CBM_IND_MONTHLYPROD_CHANGE),
	NICI(CBID_INDUSTRY_CARGO_SUFFIX,         CBM_IND_CARGO_SUFFIX),
	NICI(CBID_INDUSTRY_FUND_MORE_TEXT,       CBM_IND_FUND_MORE_TEXT),
	NICI(CBID_INDUSTRY_WINDOW_MORE_TEXT,     CBM_IND_WINDOW_MORE_TEXT),
	NICI(CBID_INDUSTRY_SPECIAL_EFFECT,       CBM_IND_SPECIAL_EFFECT),
	NICI(CBID_INDUSTRY_REFUSE_CARGO,         CBM_IND_REFUSE_CARGO),
	NICI(CBID_INDUSTRY_DECIDE_COLOUR,        CBM_IND_DECIDE_COLOUR),
	NICI(CBID_INDUSTRY_INPUT_CARGO_TYPES,    CBM_IND_INPUT_CARGO_TYPES),
	NICI(CBID_INDUSTRY_OUTPUT_CARGO_TYPES,   CBM_IND_OUTPUT_CARGO_TYPES),
	NICI(CBID_INDUSTRY_PROD_CHANGE_BUILD,    CBM_IND_PROD_CHANGE_BUILD),
	NIC_END()
};

static const NIVariable _niv_industries[] = {
	NIV(0x40, "waiting cargo 0"),
	NIV(0x41, "waiting cargo 1"),
	NIV(0x42, "waiting cargo 2"),
	NIV(0x43, "distance to closest dry/land tile"),
	NIV(0x44, "layout number"),
	NIV(0x45, "player info"),
	NIV(0x46, "industry construction date"),
	NIV(0x60, "get industry tile ID at offset"),
	NIV(0x61, "get random tile bits at offset"),
	NIV(0x62, "land info of nearby tiles"),
	NIV(0x63, "animation stage of nearby tiles"),
	NIV(0x64, "distance on nearest industry with given type"),
	NIV(0x65, "get town zone and Manhattan distance of closest town"),
	NIV(0x66, "get square of Euclidean distance of closes town"),
	NIV(0x67, "count of industry and distance of closest instance"),
	NIV(0x68, "count of industry and distance of closest instance with layout filter"),
	NIV(0x69, "produced cargo waiting"),
	NIV(0x6A, "cargo produced this month"),
	NIV(0x6B, "cargo transported this month"),
	NIV(0x6C, "cargo produced last month"),
	NIV(0x6D, "cargo transported last month"),
	NIV(0x6E, "date since cargo was delivered"),
	NIV(0x6F, "waiting input cargo"),
	NIV(0x70, "production rate"),
	NIV(0x71, "percentage of cargo transported last month"),
	NIV_END()
};

class NIHIndustry : public NIHelper {
	bool IsInspectable(uint index) const override        { return GetIndustrySpec(Industry::Get(index)->type)->grf_prop.grffile != nullptr; }
	uint GetParent(uint index) const override            { return GetInspectWindowNumber(GSF_FAKE_TOWNS, Industry::Get(index)->town->index); }
	const void *GetInstance(uint index)const override    { return Industry::Get(index); }
	const void *GetSpec(uint index) const override       { return GetIndustrySpec(Industry::Get(index)->type); }
	void SetStringParameters(uint index) const override  { this->SetSimpleStringParameters(STR_INDUSTRY_NAME, index); }
	uint32 GetGRFID(uint index) const override           { return (this->IsInspectable(index)) ? GetIndustrySpec(Industry::Get(index)->type)->grf_prop.grffile->grfid : 0; }

	uint Resolve(uint index, uint var, uint param, bool *avail) const override
	{
		Industry *i = Industry::Get(index);
		IndustriesResolverObject ro(i->location.tile, i, i->type);
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, avail);
	}

	uint GetPSASize(uint index, uint32 grfid) const override { return cpp_lengthof(PersistentStorage, storage); }

	const int32 *GetPSAFirstPosition(uint index, uint32 grfid) const override
	{
		const Industry *i = (const Industry *)this->GetInstance(index);
		if (i->psa == nullptr) return nullptr;
		return (int32 *)(&i->psa->storage);
	}
};

static const NIFeature _nif_industry = {
	_nip_industries,
	_nic_industries,
	_niv_industries,
	new NIHIndustry(),
};


/*** NewGRF objects ***/

#define NICO(cb_id, bit) NIC(cb_id, ObjectSpec, callback_mask, bit)
static const NICallback _nic_objects[] = {
	NICO(CBID_OBJECT_LAND_SLOPE_CHECK,     CBM_OBJ_SLOPE_CHECK),
	NICO(CBID_OBJECT_ANIMATION_NEXT_FRAME, CBM_OBJ_ANIMATION_NEXT_FRAME),
	NICO(CBID_OBJECT_ANIMATION_START_STOP, CBM_NO_BIT),
	NICO(CBID_OBJECT_ANIMATION_SPEED,      CBM_OBJ_ANIMATION_SPEED),
	NICO(CBID_OBJECT_COLOUR,               CBM_OBJ_COLOUR),
	NICO(CBID_OBJECT_FUND_MORE_TEXT,       CBM_OBJ_FUND_MORE_TEXT),
	NICO(CBID_OBJECT_AUTOSLOPE,            CBM_OBJ_AUTOSLOPE),
	NIC_END()
};

static const NIVariable _niv_objects[] = {
	NIV(0x40, "relative position"),
	NIV(0x41, "tile information"),
	NIV(0x42, "construction date"),
	NIV(0x43, "animation counter"),
	NIV(0x44, "object founder"),
	NIV(0x45, "get town zone and Manhattan distance of closest town"),
	NIV(0x46, "get square of Euclidean distance of closes town"),
	NIV(0x47, "colour"),
	NIV(0x48, "view"),
	NIV(0x60, "get object ID at offset"),
	NIV(0x61, "get random tile bits at offset"),
	NIV(0x62, "land info of nearby tiles"),
	NIV(0x63, "animation stage of nearby tiles"),
	NIV(0x64, "distance on nearest object with given type"),
	NIV_END()
};

class NIHObject : public NIHelper {
	bool IsInspectable(uint index) const override        { return ObjectSpec::GetByTile(index)->grf_prop.grffile != nullptr; }
	uint GetParent(uint index) const override            { return GetInspectWindowNumber(GSF_FAKE_TOWNS, Object::GetByTile(index)->town->index); }
	const void *GetInstance(uint index)const override    { return Object::GetByTile(index); }
	const void *GetSpec(uint index) const override       { return ObjectSpec::GetByTile(index); }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_NEWGRF_INSPECT_CAPTION_OBJECT_AT_OBJECT, INVALID_STRING_ID, index); }
	uint32 GetGRFID(uint index) const override           { return (this->IsInspectable(index)) ? ObjectSpec::GetByTile(index)->grf_prop.grffile->grfid : 0; }

	uint Resolve(uint index, uint var, uint param, bool *avail) const override
	{
		ObjectResolverObject ro(ObjectSpec::GetByTile(index), Object::GetByTile(index), index);
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, avail);
	}
};

static const NIFeature _nif_object = {
	nullptr,
	_nic_objects,
	_niv_objects,
	new NIHObject(),
};


/*** NewGRF rail types ***/

static const NIVariable _niv_railtypes[] = {
	NIV(0x40, "terrain type"),
	NIV(0x41, "enhanced tunnels"),
	NIV(0x42, "level crossing status"),
	NIV(0x43, "construction date"),
	NIV(0x44, "town zone"),
	NIV_END()
};

class NIHRailType : public NIHelper {
	bool IsInspectable(uint index) const override        { return true; }
	uint GetParent(uint index) const override            { return UINT32_MAX; }
	const void *GetInstance(uint index)const override    { return nullptr; }
	const void *GetSpec(uint index) const override       { return nullptr; }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_NEWGRF_INSPECT_CAPTION_OBJECT_AT_RAIL_TYPE, INVALID_STRING_ID, index); }
	uint32 GetGRFID(uint index) const override           { return 0; }

	uint Resolve(uint index, uint var, uint param, bool *avail) const override
	{
		/* There is no unique GRFFile for the tile. Multiple GRFs can define different parts of the railtype.
		 * However, currently the NewGRF Debug GUI does not display variables depending on the GRF (like 0x7F) anyway. */
		RailTypeResolverObject ro(nullptr, index, TCX_NORMAL, RTSG_END);
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, avail);
	}
};

static const NIFeature _nif_railtype = {
	nullptr,
	nullptr,
	_niv_railtypes,
	new NIHRailType(),
};


/*** NewGRF airport tiles ***/

#define NICAT(cb_id, bit) NIC(cb_id, AirportTileSpec, callback_mask, bit)
static const NICallback _nic_airporttiles[] = {
	NICAT(CBID_AIRPTILE_DRAW_FOUNDATIONS, CBM_AIRT_DRAW_FOUNDATIONS),
	NICAT(CBID_AIRPTILE_ANIM_START_STOP,  CBM_NO_BIT),
	NICAT(CBID_AIRPTILE_ANIM_NEXT_FRAME,  CBM_AIRT_ANIM_NEXT_FRAME),
	NICAT(CBID_AIRPTILE_ANIMATION_SPEED,  CBM_AIRT_ANIM_SPEED),
	NIC_END()
};

class NIHAirportTile : public NIHelper {
	bool IsInspectable(uint index) const override        { return AirportTileSpec::Get(GetAirportGfx(index))->grf_prop.grffile != nullptr; }
	uint GetParent(uint index) const override            { return GetInspectWindowNumber(GSF_FAKE_TOWNS, Station::GetByTile(index)->town->index); }
	const void *GetInstance(uint index)const override    { return nullptr; }
	const void *GetSpec(uint index) const override       { return AirportTileSpec::Get(GetAirportGfx(index)); }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_STATION_NAME, GetStationIndex(index), index); }
	uint32 GetGRFID(uint index) const override           { return (this->IsInspectable(index)) ? AirportTileSpec::Get(GetAirportGfx(index))->grf_prop.grffile->grfid : 0; }

	uint Resolve(uint index, uint var, uint param, bool *avail) const override
	{
		AirportTileResolverObject ro(AirportTileSpec::GetByTile(index), index, Station::GetByTile(index));
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, avail);
	}
};

static const NIFeature _nif_airporttile = {
	nullptr,
	_nic_airporttiles,
	_niv_industrytiles, // Yes, they share this (at least now)
	new NIHAirportTile(),
};


/*** NewGRF towns ***/

static const NIVariable _niv_towns[] = {
	NIV(0x40, "larger town effect on this town"),
	NIV(0x41, "town index"),
	NIV(0x82, "population"),
	NIV(0x94, "zone radius 0"),
	NIV(0x96, "zone radius 1"),
	NIV(0x98, "zone radius 2"),
	NIV(0x9A, "zone radius 3"),
	NIV(0x9C, "zone radius 4"),
	NIV(0xB6, "number of buildings"),
	NIV_END()
};

class NIHTown : public NIHelper {
	bool IsInspectable(uint index) const override        { return Town::IsValidID(index); }
	uint GetParent(uint index) const override            { return UINT32_MAX; }
	const void *GetInstance(uint index)const override    { return Town::Get(index); }
	const void *GetSpec(uint index) const override       { return nullptr; }
	void SetStringParameters(uint index) const override  { this->SetSimpleStringParameters(STR_TOWN_NAME, index); }
	uint32 GetGRFID(uint index) const override           { return 0; }
	bool PSAWithParameter() const override               { return true; }
	uint GetPSASize(uint index, uint32 grfid) const override { return cpp_lengthof(PersistentStorage, storage); }

	uint Resolve(uint index, uint var, uint param, bool *avail) const override
	{
		TownResolverObject ro(nullptr, Town::Get(index), true);
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, avail);
	}

	const int32 *GetPSAFirstPosition(uint index, uint32 grfid) const override
	{
		Town *t = Town::Get(index);

		std::list<PersistentStorage *>::iterator iter;
		for (iter = t->psa_list.begin(); iter != t->psa_list.end(); iter++) {
			if ((*iter)->grfid == grfid) return (int32 *)(&(*iter)->storage[0]);
		}

		return nullptr;
	}
};

static const NIFeature _nif_town = {
	nullptr,
	nullptr,
	_niv_towns,
	new NIHTown(),
};

/*** NewGRF road types ***/

static const NIVariable _niv_roadtypes[] = {
	NIV(0x40, "terrain type"),
	NIV(0x41, "enhanced tunnels"),
	NIV(0x42, "level crossing status"),
	NIV(0x43, "construction date"),
	NIV(0x44, "town zone"),
	NIV_END()
};

class NIHRoadType : public NIHelper {
	bool IsInspectable(uint index) const override        { return true; }
	uint GetParent(uint index) const override            { return UINT32_MAX; }
	const void *GetInstance(uint index) const override   { return nullptr; }
	const void *GetSpec(uint index) const override       { return nullptr; }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_NEWGRF_INSPECT_CAPTION_OBJECT_AT_RAIL_TYPE, INVALID_STRING_ID, index); }
	uint32 GetGRFID(uint index) const override           { return 0; }

	uint Resolve(uint index, uint var, uint param, bool *avail) const override
	{
		/* There is no unique GRFFile for the tile. Multiple GRFs can define different parts of the railtype.
		 * However, currently the NewGRF Debug GUI does not display variables depending on the GRF (like 0x7F) anyway. */
		RoadTypeResolverObject ro(nullptr, index, TCX_NORMAL, ROTSG_END);
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, avail);
	}
};

static const NIFeature _nif_roadtype = {
	nullptr,
	nullptr,
	_niv_roadtypes,
	new NIHRoadType(),
};

static const NIFeature _nif_tramtype = {
	nullptr,
	nullptr,
	_niv_roadtypes,
	new NIHRoadType(),
};

/*** ***/

static const NIVariable _nif_roadstops[] = {
	NIV(0x40, "test"),
	NIV_END(),
};

class NIHRoadStop : public NIHelper {
	bool IsInspectable(uint index) const override { return false; }
	uint GetParent(uint index) const override { return UINT32_MAX; }
	const void *GetInstance(uint index)const override { return nullptr; }
	const void *GetSpec(uint index) const override { return nullptr; }
	void SetStringParameters(uint index) const override { }
	uint32 GetGRFID(uint index) const override { return 0; }

	uint Resolve(uint index, uint var, uint param, bool *avail) const override
	{
		return UINT32_MAX;
	}
};

static const NIFeature _nif_roadstop = {
	nullptr,
	nullptr,
	_nif_roadstops,
	new NIHRoadStop(),
};

/** Table with all NIFeatures. */
static const NIFeature * const _nifeatures[] = {
	&_nif_vehicle,      // GSF_TRAINS
	&_nif_vehicle,      // GSF_ROADVEHICLES
	&_nif_vehicle,      // GSF_SHIPS
	&_nif_vehicle,      // GSF_AIRCRAFT
	&_nif_station,      // GSF_STATIONS
	nullptr,               // GSF_CANALS (no callbacks/action2 implemented)
	nullptr,               // GSF_BRIDGES (no callbacks/action2)
	&_nif_house,        // GSF_HOUSES
	nullptr,               // GSF_GLOBALVAR (has no "physical" objects)
	&_nif_industrytile, // GSF_INDUSTRYTILES
	&_nif_industry,     // GSF_INDUSTRIES
	nullptr,               // GSF_CARGOES (has no "physical" objects)
	nullptr,               // GSF_SOUNDFX (has no "physical" objects)
	nullptr,               // GSF_AIRPORTS (feature not implemented)
	nullptr,               // GSF_SIGNALS (feature not implemented)
	&_nif_object,       // GSF_OBJECTS
	&_nif_railtype,     // GSF_RAILTYPES
	&_nif_airporttile,  // GSF_AIRPORTTILES
	&_nif_roadtype,     // GSF_ROADTYPES
	&_nif_tramtype,     // GSF_TRAMTYPES
	&_nif_town,         // GSF_FAKE_TOWNS
	&_nif_roadstop,  // GSF_ROADSTATIONS
};
static_assert(lengthof(_nifeatures) == GSF_FAKE_END);
