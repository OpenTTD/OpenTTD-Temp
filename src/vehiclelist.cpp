/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file vehiclelist.cpp Lists of vehicles. */

#include "stdafx.h"
#include "train.h"
#include "vehiclelist.h"
#include "group.h"
#include "depot_map.h"

#include "safeguards.h"

/**
 * Pack a VehicleListIdentifier in a single uint32.
 * @return The packed identifier.
 */
uint32 VehicleListIdentifier::Pack() const
{
	byte c = this->company == OWNER_NONE ? 0xF : (byte)this->company;
	assert(c             < (1 <<  4));
	assert(this->vtype   < (1 <<  2));
	assert(this->index   < (1 << 20));
	assert(this->type    < VLT_END);
	static_assert(VLT_END <= (1 <<  3));

	return c << 28 | this->type << 23 | this->vtype << 26 | this->index;
}

/**
 * Unpack a VehicleListIdentifier from a single uint32.
 * @param data The data to unpack.
 * @return true iff the data was valid (enough).
 */
bool VehicleListIdentifier::UnpackIfValid(uint32 data)
{
	byte c        = GB(data, 28, 4);
	this->company = c == 0xF ? OWNER_NONE : (CompanyID)c;
	this->type    = (VehicleListType)GB(data, 23, 3);
	this->vtype   = (VehicleType)GB(data, 26, 2);
	this->index   = GB(data, 0, 20);

	return this->type < VLT_END;
}

/**
 * Decode a packed vehicle list identifier into a new one.
 * @param data The data to unpack.
 */
/* static */ VehicleListIdentifier VehicleListIdentifier::UnPack(uint32 data)
{
	VehicleListIdentifier result;
	[[maybe_unused]] bool ret = result.UnpackIfValid(data);
	assert(ret);
	return result;
}

/**
 * Generate a list of vehicles inside a depot.
 * @param type     Type of vehicle
 * @param depot_id The id of the depot
 * @param engines  Pointer to list to add vehicles to
 * @param wagons   Pointer to list to add wagons to (can be nullptr)
 * @param individual_wagons If true add every wagon to \a wagons which is not attached to an engine. If false only add the first wagon of every row.
 */
void BuildDepotVehicleList(VehicleType type, DepotID depot_id, VehicleList *engines, VehicleList *wagons, bool individual_wagons)
{
	engines->clear();
	if (wagons != nullptr && wagons != engines) wagons->clear();

	for (const Vehicle *v : Vehicle::Iterate()) {
		/* General tests for all vehicle types */
		if (v->type != type) continue;
		if (!IsDepotTypeTile(v->tile, (TransportType)type)) continue;
		if (GetDepotIndex(v->tile) != depot_id) continue;

		switch (type) {
			case VEH_TRAIN: {
				const Train *t = Train::From(v);
				if (t->IsArticulatedPart() || t->IsRearDualheaded()) continue;
				if (!t->IsInDepot()) continue;
				if (wagons != nullptr && t->First()->IsFreeWagon()) {
					if (individual_wagons || t->IsFreeWagon()) wagons->push_back(t);
					continue;
				}
				break;
			}

			default:
				if (!v->IsInDepot()) continue;
				break;
		}

		if (!v->IsPrimaryVehicle()) continue;

		engines->push_back(v);
	}

	/* Ensure the lists are not wasting too much space. If the lists are fresh
	 * (i.e. built within a command) then this will actually do nothing. */
	engines->shrink_to_fit();
	if (wagons != nullptr && wagons != engines) wagons->shrink_to_fit();
}

/**
 * Generate a list of vehicles based on window type.
 * @param list Pointer to list to add vehicles to
 * @param vli  The identifier of this vehicle list.
 * @return false if invalid list is requested
 */
bool GenerateVehicleSortList(VehicleList *list, const VehicleListIdentifier &vli)
{
	list->clear();

	switch (vli.type) {
		case VL_STATION_LIST:
			for (const Vehicle *v : Vehicle::Iterate()) {
				if (v->type == vli.vtype && v->IsPrimaryVehicle()) {
					for (const Order *order : v->Orders()) {
						if ((order->IsType(OT_GOTO_STATION) || order->IsType(OT_GOTO_WAYPOINT) || order->IsType(OT_IMPLICIT))
								&& order->GetDestination() == vli.index) {
							list->push_back(v);
							break;
						}
					}
				}
			}
			break;

		case VL_SHARED_ORDERS: {
			/* Add all vehicles from this vehicle's shared order list */
			const Vehicle *v = Vehicle::GetIfValid(vli.index);
			if (v == nullptr || v->type != vli.vtype || !v->IsPrimaryVehicle()) return false;

			for (; v != nullptr; v = v->NextShared()) {
				list->push_back(v);
			}
			break;
		}

		case VL_GROUP_LIST:
			if (vli.index != ALL_GROUP) {
				for (const Vehicle *v : Vehicle::Iterate()) {
					if (v->type == vli.vtype && v->IsPrimaryVehicle() &&
							v->owner == vli.company && GroupIsInGroup(v->group_id, vli.index)) {
						list->push_back(v);
					}
				}
				break;
			}
			FALLTHROUGH;

		case VL_STANDARD:
			for (const Vehicle *v : Vehicle::Iterate()) {
				if (v->type == vli.vtype && v->owner == vli.company && v->IsPrimaryVehicle()) {
					list->push_back(v);
				}
			}
			break;

		case VL_DEPOT_LIST:
			for (const Vehicle *v : Vehicle::Iterate()) {
				if (v->type == vli.vtype && v->IsPrimaryVehicle()) {
					for (const Order *order : v->Orders()) {
						if (order->IsType(OT_GOTO_DEPOT) && !(order->GetDepotActionType() & ODATFB_NEAREST_DEPOT) && order->GetDestination() == vli.index) {
							list->push_back(v);
							break;
						}
					}
				}
			}
			break;

		default: return false;
	}

	list->shrink_to_fit();
	return true;
}
