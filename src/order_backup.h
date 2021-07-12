/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file order_backup.h Functions related to order backups. */

#ifndef ORDER_BACKUP_H
#define ORDER_BACKUP_H

#include "core/pool_type.hpp"
#include "group_type.h"
#include "tile_type.h"
#include "vehicle_type.h"
#include "base_consist.h"
#include "saveload/saveload.h"
#include "depot_type.h"

/** Unique identifier for an order backup. */
typedef uint8 OrderBackupID;
struct OrderBackup;

/** The pool type for order backups. */
typedef Pool<OrderBackup, OrderBackupID, 1, 256> OrderBackupPool;
/** The pool with order backups. */
extern OrderBackupPool _order_backup_pool;

/** Flag to pass to the vehicle construction command when an order should be preserved. */
static const uint32 MAKE_ORDER_BACKUP_FLAG = 1U << 31;

/**
 * Data for backing up an order of a vehicle so it can be
 * restored after a vehicle is rebuilt in the same depot.
 */
struct OrderBackup : OrderBackupPool::PoolItem<&_order_backup_pool>, BaseConsist {
private:
	friend SaveLoadTable GetOrderBackupDescription(); ///< Saving and loading of order backups.
	friend struct BKORChunkHandler; ///< Creating empty orders upon savegame loading.
	uint32 user;               ///< The user that requested the backup.
	DepotID depot_id;          ///< Depot where the order was changed.
	GroupID group;             ///< The group the vehicle was part of.

	const Vehicle *clone;      ///< Vehicle this vehicle was a clone of.
	Order *orders;             ///< The actual orders if the vehicle was not a clone.

	/** Creation for savegame restoration. */
	OrderBackup() {}
	OrderBackup(const Vehicle *v, uint32 user);

	void DoRestore(Vehicle *v);

public:
	~OrderBackup();

	static void Backup(const Vehicle *v, uint32 user);
	static void Restore(Vehicle *v, uint32 user);

	static void ResetOfUser(DepotID depot_id, uint32 user);
	static void ResetUser(uint32 user);
	static void Reset(DepotID depot = INVALID_DEPOT, bool from_gui = true);

	static void ClearGroup(GroupID group);
	static void ClearVehicle(const Vehicle *v);
	static void RemoveOrder(OrderType type, DestinationID destination);
};

#endif /* ORDER_BACKUP_H */
