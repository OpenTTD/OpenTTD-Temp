/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file order_sl.cpp Code handling saving and loading of orders */

#include "../stdafx.h"

#include "saveload.h"
#include "compat/order_sl_compat.h"

#include "saveload_internal.h"
#include "../order_backup.h"
#include "../settings_type.h"
#include "../network/network.h"
#include "../depot_map.h"

#include "../safeguards.h"

/**
 * Converts this order from an old savegame's version;
 * it moves all bits to the new location.
 */
void Order::ConvertFromOldSavegame()
{
	uint8 old_flags = this->flags;
	this->flags = 0;

	/* First handle non-stop - use value from savegame if possible, else use value from config file */
	if (_settings_client.gui.sg_new_nonstop || (IsSavegameVersionBefore(SLV_22) && _savegame_type != SGT_TTO && _savegame_type != SGT_TTD && _settings_client.gui.new_nonstop)) {
		/* OFB_NON_STOP */
		this->SetNonStopType((old_flags & 8) ? ONSF_NO_STOP_AT_ANY_STATION : ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS);
	} else {
		this->SetNonStopType((old_flags & 8) ? ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS : ONSF_STOP_EVERYWHERE);
	}

	switch (this->GetType()) {
		/* Only a few types need the other savegame conversions. */
		case OT_GOTO_DEPOT: case OT_GOTO_STATION: case OT_LOADING: break;
		default: return;
	}

	if (this->GetType() != OT_GOTO_DEPOT) {
		/* Then the load flags */
		if ((old_flags & 2) != 0) { // OFB_UNLOAD
			this->SetLoadType(OLFB_NO_LOAD);
		} else if ((old_flags & 4) == 0) { // !OFB_FULL_LOAD
			this->SetLoadType(OLF_LOAD_IF_POSSIBLE);
		} else {
			/* old OTTD versions stored full_load_any in config file - assume it was enabled when loading */
			this->SetLoadType(_settings_client.gui.sg_full_load_any || IsSavegameVersionBefore(SLV_22) ? OLF_FULL_LOAD_ANY : OLFB_FULL_LOAD);
		}

		if (this->IsType(OT_GOTO_STATION)) this->SetStopLocation(OSL_PLATFORM_FAR_END);

		/* Finally fix the unload flags */
		if ((old_flags & 1) != 0) { // OFB_TRANSFER
			this->SetUnloadType(OUFB_TRANSFER);
		} else if ((old_flags & 2) != 0) { // OFB_UNLOAD
			this->SetUnloadType(OUFB_UNLOAD);
		} else {
			this->SetUnloadType(OUF_UNLOAD_IF_POSSIBLE);
		}
	} else {
		/* Then the depot action flags */
		this->SetDepotActionType(((old_flags & 6) == 4) ? ODATFB_HALT : ODATF_SERVICE_ONLY);

		/* Finally fix the depot type flags */
		uint t = ((old_flags & 6) == 6) ? ODTFB_SERVICE : ODTF_MANUAL;
		if ((old_flags & 2) != 0) t |= ODTFB_PART_OF_ORDERS;
		this->SetDepotOrderType((OrderDepotTypeFlags)t);
	}
}

/**
 * Unpacks a order from savegames with version 4 and lower
 * @param packed packed order
 * @return unpacked order
 */
static Order UnpackVersion4Order(uint16 packed)
{
	return Order(GB(packed, 8, 8) << 16 | GB(packed, 4, 4) << 8 | GB(packed, 0, 4));
}

/**
 * Unpacks a order from savegames made with TTD(Patch)
 * @param packed packed order
 * @return unpacked order
 */
Order UnpackOldOrder(uint16 packed)
{
	Order order = UnpackVersion4Order(packed);

	/*
	 * Sanity check
	 * TTD stores invalid orders as OT_NOTHING with non-zero flags/station
	 */
	if (order.IsType(OT_NOTHING) && packed != 0) order.MakeDummy();

	return order;
}

SaveLoadTable GetOrderDescription()
{
	static const SaveLoad _order_desc[] = {
		     SLE_VAR(Order, type,           SLE_UINT8),
		     SLE_VAR(Order, flags,          SLE_UINT8),
		     SLE_VAR(Order, dest,           SLE_UINT16),
		     SLE_REF(Order, next,           REF_ORDER),
		 SLE_CONDVAR(Order, refit_cargo,    SLE_UINT8,   SLV_36, SL_MAX_VERSION),
		 SLE_CONDVAR(Order, wait_time,      SLE_UINT16,  SLV_67, SL_MAX_VERSION),
		 SLE_CONDVAR(Order, travel_time,    SLE_UINT16,  SLV_67, SL_MAX_VERSION),
		 SLE_CONDVAR(Order, max_speed,      SLE_UINT16, SLV_172, SL_MAX_VERSION),
	};

	return _order_desc;
}

struct ORDRChunkHandler : ChunkHandler {
	ORDRChunkHandler() : ChunkHandler('ORDR', CH_TABLE) {}

	void Save() const override
	{
		const SaveLoadTable slt = GetOrderDescription();
		SlTableHeader(slt);

		for (Order *order : Order::Iterate()) {
			SlSetArrayIndex(order->index);
			SlObject(order, slt);
		}
	}

	void Load() const override
	{
		if (IsSavegameVersionBefore(SLV_5, 2)) {
			/* Version older than 5.2 did not have a ->next pointer. Convert them
			 * (in the old days, the orderlist was 5000 items big) */
			size_t len = SlGetFieldLength();

			if (IsSavegameVersionBefore(SLV_5)) {
				/* Pre-version 5 had another layout for orders
				 * (uint16 instead of uint32) */
				len /= sizeof(uint16);
				uint16 *orders = MallocT<uint16>(len + 1);

				SlCopy(orders, len, SLE_UINT16);

				for (size_t i = 0; i < len; ++i) {
					Order *o = new (i) Order();
					o->AssignOrder(UnpackVersion4Order(orders[i]));
				}

				free(orders);
			} else if (IsSavegameVersionBefore(SLV_5, 2)) {
				len /= sizeof(uint32);
				uint32 *orders = MallocT<uint32>(len + 1);

				SlCopy(orders, len, SLE_UINT32);

				for (size_t i = 0; i < len; ++i) {
					new (i) Order(orders[i]);
				}

				free(orders);
			}

			/* Update all the next pointer */
			for (Order *o : Order::Iterate()) {
				size_t order_index = o->index;
				/* Delete invalid orders */
				if (o->IsType(OT_NOTHING)) {
					delete o;
					continue;
				}
				/* The orders were built like this:
				 * While the order is valid, set the previous will get its next pointer set */
				Order *prev = Order::GetIfValid(order_index - 1);
				if (prev != nullptr) prev->next = o;
			}
		} else {
			const std::vector<SaveLoad> slt = SlCompatTableHeader(GetOrderDescription(), _order_sl_compat);

			int index;

			while ((index = SlIterateArray()) != -1) {
				Order *order = new (index) Order();
				SlObject(order, slt);
			}
		}
	}

	void FixPointers() const override
	{
		/* Orders from old savegames have pointers corrected in Load_ORDR */
		if (IsSavegameVersionBefore(SLV_5, 2)) return;

		for (Order *o : Order::Iterate()) {
			SlObject(o, GetOrderDescription());
		}
	}
};

SaveLoadTable GetOrderListDescription()
{
	static const SaveLoad _orderlist_desc[] = {
		SLE_REF(OrderList, first,              REF_ORDER),
	};

	return _orderlist_desc;
}

struct ORDLChunkHandler : ChunkHandler {
	ORDLChunkHandler() : ChunkHandler('ORDL', CH_TABLE) {}

	void Save() const override
	{
		const SaveLoadTable slt = GetOrderListDescription();
		SlTableHeader(slt);

		for (OrderList *list : OrderList::Iterate()) {
			SlSetArrayIndex(list->index);
			SlObject(list, slt);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlCompatTableHeader(GetOrderListDescription(), _orderlist_sl_compat);

		int index;

		while ((index = SlIterateArray()) != -1) {
			/* set num_orders to 0 so it's a valid OrderList */
			OrderList *list = new (index) OrderList(0);
			SlObject(list, slt);
		}

	}

	void FixPointers() const override
	{
		for (OrderList *list : OrderList::Iterate()) {
			SlObject(list, GetOrderListDescription());
		}
	}
};

static TileIndex _tile;

SaveLoadTable GetOrderBackupDescription()
{
	static const SaveLoad _order_backup_desc[] = {
		     SLE_VAR(OrderBackup, user,                     SLE_UINT32),
		SLEG_CONDVAR("tile", _tile,                         SLE_UINT32,         SL_MIN_VERSION, SLV_MULTITILE_DEPOTS),
		 SLE_CONDVAR(OrderBackup, depot_id,                 SLE_UINT16,      SLV_MULTITILE_DEPOTS, SL_MAX_VERSION),
		     SLE_VAR(OrderBackup, group,                    SLE_UINT16),
		 SLE_CONDVAR(OrderBackup, service_interval,         SLE_FILE_U32 | SLE_VAR_U16,  SL_MIN_VERSION, SLV_192),
		 SLE_CONDVAR(OrderBackup, service_interval,         SLE_UINT16,                SLV_192, SL_MAX_VERSION),
		    SLE_SSTR(OrderBackup, name,                     SLE_STR),
		 SLE_CONDREF(OrderBackup, clone,                    REF_VEHICLE,               SLV_192, SL_MAX_VERSION),
		     SLE_VAR(OrderBackup, cur_real_order_index,     SLE_UINT8),
		 SLE_CONDVAR(OrderBackup, cur_implicit_order_index, SLE_UINT8,                 SLV_176, SL_MAX_VERSION),
		 SLE_CONDVAR(OrderBackup, current_order_time,       SLE_UINT32,                SLV_176, SL_MAX_VERSION),
		 SLE_CONDVAR(OrderBackup, lateness_counter,         SLE_INT32,                 SLV_176, SL_MAX_VERSION),
		 SLE_CONDVAR(OrderBackup, timetable_start,          SLE_INT32,                 SLV_176, SL_MAX_VERSION),
		 SLE_CONDVAR(OrderBackup, vehicle_flags,            SLE_FILE_U8 | SLE_VAR_U16, SLV_176, SLV_180),
		 SLE_CONDVAR(OrderBackup, vehicle_flags,            SLE_UINT16,                SLV_180, SL_MAX_VERSION),
		     SLE_REF(OrderBackup, orders,                   REF_ORDER),
	};

	return _order_backup_desc;
}

struct BKORChunkHandler : ChunkHandler {
	BKORChunkHandler() : ChunkHandler('BKOR', CH_TABLE) {}

	void Save() const override
	{
		const SaveLoadTable slt = GetOrderBackupDescription();
		SlTableHeader(slt);

		/* We only save this when we're a network server
		 * as we want this information on our clients. For
		 * normal games this information isn't needed. */
		if (!_networking || !_network_server) return;

		for (OrderBackup *ob : OrderBackup::Iterate()) {
			SlSetArrayIndex(ob->index);
			SlObject(ob, slt);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlCompatTableHeader(GetOrderBackupDescription(), _order_backup_sl_compat);

		int index;

		while ((index = SlIterateArray()) != -1) {
			/* set num_orders to 0 so it's a valid OrderList */
			OrderBackup *ob = new (index) OrderBackup();
			SlObject(ob, slt);
		}

		if (IsSavegameVersionBefore(SLV_MULTITILE_DEPOTS)) _order_backup_pool.CleanPool();
	}

	void FixPointers() const override
	{
		for (OrderBackup *ob : OrderBackup::Iterate()) {
			SlObject(ob, GetOrderBackupDescription());
		}
	}
};

static const BKORChunkHandler BKOR;
static const ORDRChunkHandler ORDR;
static const ORDLChunkHandler ORDL;
static const ChunkHandlerRef order_chunk_handlers[] = {
	BKOR,
	ORDR,
	ORDL,
};

extern const ChunkHandlerTable _order_chunk_handlers(order_chunk_handlers);
