/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file depot.cpp Handling of depots. */

#include "stdafx.h"
#include "depot_base.h"
#include "order_backup.h"
#include "order_func.h"
#include "window_func.h"
#include "core/pool_func.hpp"
#include "vehicle_base.h"
#include "vehicle_gui.h"
#include "vehiclelist.h"

#include "safeguards.h"

/** All our depots tucked away in a pool. */
DepotPool _depot_pool("Depot");
INSTANTIATE_POOL_METHODS(Depot)

/**
 * Clean up a depot
 */
Depot::~Depot()
{
	if (CleaningPool()) return;

	/* Clear the depot from all order-lists */
	RemoveOrderFromAllVehicles(OT_GOTO_DEPOT, this->index);
}

/**
 * Cancel deletion of this depot (reuse it).
 * @param xy New location of the depot.
 * @see Depot::IsInUse
 * @see Depot::Disuse
 */
void Depot::Reuse(TileIndex xy)
{
	this->delete_ctr = 0;
	this->xy = xy;
}

/**
 * Schedule deletion of this depot.
 *
 * This method is ought to be called after demolishing a depot.
 * The depot will be kept in the pool for a while so it can be
 * placed again later without messing vehicle orders.
 *
 * @see Depot::IsInUse
 * @see Depot::Reuse
 */
void Depot::Disuse()
{
	/* Delete the depot-window */
	DeleteWindowById(WC_VEHICLE_DEPOT, this->xy);

	/* Delete the depot list */
	DeleteWindowById(GetWindowClassForVehicleType(this->type), VehicleListIdentifier(VL_DEPOT_LIST, this->type, this->owner, this->index).Pack());

	/* Clear the order backup. */
	OrderBackup::Reset(this->xy, false);

	/* Mark that the depot is demolished and start the countdown. */
	this->delete_ctr = 8;

	/* Make sure no vehicle is going to the old depot. */
	Vehicle *v;
	FOR_ALL_VEHICLES(v) {
		if (v->dest_tile == this->xy && v->First() == v && v->current_order.IsType(OT_GOTO_DEPOT)) {
			v->SetDestTile(v->GetOrderDepotLocation(this->index));
		}
	}
}
