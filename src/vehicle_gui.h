/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file vehicle_gui.h Functions related to the vehicle's GUIs. */

#ifndef VEHICLE_GUI_H
#define VEHICLE_GUI_H

#include "window_type.h"
#include "vehicle_type.h"
#include "vehicle_gui_base.h"
#include "vehiclelist.h"
#include "order_type.h"
#include "station_type.h"
#include "engine_type.h"
#include "company_type.h"

void ShowVehicleRefitWindow(const Vehicle *v, VehicleOrderID order, Window *parent, bool auto_refit = false);

/** The tabs in the train details window */
enum TrainDetailsWindowTabs {
	TDW_TAB_CARGO = 0, ///< Tab with cargo carried by the vehicles
	TDW_TAB_INFO,      ///< Tab with name and value of the vehicles
	TDW_TAB_CAPACITY,  ///< Tab with cargo capacity of the vehicles
	TDW_TAB_TOTALS,    ///< Tab with sum of total cargo transported
};

/** Special values for vehicle-related windows for the data parameter of #InvalidateWindowData. */
enum VehicleInvalidateWindowData {
	VIWD_REMOVE_ALL_ORDERS = -1, ///< Removed / replaced all orders (after deleting / sharing).
	VIWD_MODIFY_ORDERS     = -2, ///< Other order modifications.
	VIWD_CONSIST_CHANGED   = -3, ///< Vehicle composition was changed.
	VIWD_AUTOREPLACE       = -4, ///< Autoreplace replaced the vehicle.
};

/** Extra information about refitted cargo and capacity */
struct TestedEngineDetails {
	Money cost;           ///< Refit cost
	CargoID cargo;        ///< Cargo type
	uint capacity;        ///< Cargo capacity
	uint16 mail_capacity; ///< Mail capacity if available
};

int DrawVehiclePurchaseInfo(int left, int right, int y, EngineID engine_number, TestedEngineDetails &te);

void DrawTrainImage(const Train *v, int left, int right, int y, VehicleID selection, EngineImageType image_type, int skip, VehicleID drag_dest = INVALID_VEHICLE);
void DrawRoadVehImage(const Vehicle *v, int left, int right, int y, VehicleID selection, EngineImageType image_type, int skip = 0);
void DrawShipImage(const Vehicle *v, int left, int right, int y, VehicleID selection, EngineImageType image_type);
void DrawAircraftImage(const Vehicle *v, int left, int right, int y, VehicleID selection, EngineImageType image_type);

void ShowBuildVehicleWindow(TileIndex tile, VehicleType type);

uint ShowRefitOptionsList(int left, int right, int y, EngineID engine);
StringID GetCargoSubtypeText(const Vehicle *v);

void ShowVehicleListWindow(const Vehicle *v);
void ShowVehicleListWindow(CompanyID company, VehicleType vehicle_type);
void ShowVehicleListWindow(CompanyID company, VehicleType vehicle_type, StationID station);
void ShowVehicleListWindow(CompanyID company, VehicleType vehicle_type, TileIndex depot_tile);

/**
 * Get the height of a single vehicle in the GUIs.
 * @param type the vehicle type to look at
 * @return the height
 */
static inline uint GetVehicleHeight(VehicleType type)
{
	return (type == VEH_TRAIN || type == VEH_ROAD) ? 14 : 24;
}

int GetSingleVehicleWidth(const Vehicle *v, EngineImageType image_type);
int GetVehicleWidth(const Vehicle *v, EngineImageType image_type);

/** Dimensions of a cell in the purchase/depot windows. */
struct VehicleCellSize {
	uint height;       ///< Vehicle cell height.
	uint extend_left;  ///< Extend of the cell to the left.
	uint extend_right; ///< Extend of the cell to the right.
};

VehicleCellSize GetVehicleImageCellSize(VehicleType type, EngineImageType image_type);

/**
 * Get WindowClass for vehicle list of given vehicle type
 * @param vt vehicle type to check
 * @return corresponding window class
 * @note works only for company buildable vehicle types
 */
static inline WindowClass GetWindowClassForVehicleType(VehicleType vt)
{
	switch (vt) {
		default: NOT_REACHED();
		case VEH_TRAIN:    return WC_TRAINS_LIST;
		case VEH_ROAD:     return WC_ROADVEH_LIST;
		case VEH_SHIP:     return WC_SHIPS_LIST;
		case VEH_AIRCRAFT: return WC_AIRCRAFT_LIST;
	}
}

/* Unified window procedure */
void ShowVehicleViewWindow(const Vehicle *v);
bool VehicleClicked(const Vehicle *v);
bool VehicleClicked(VehicleList::const_iterator begin, VehicleList::const_iterator end);
bool VehicleClicked(const GUIVehicleGroup &vehgroup);
void StartStopVehicle(const Vehicle *v, bool texteffect);

Vehicle *CheckClickOnVehicle(const struct Viewport *vp, int x, int y);

void DrawVehicleImage(const Vehicle *v, int left, int right, int y, VehicleID selection, EngineImageType image_type, int skip);
void SetMouseCursorVehicle(const Vehicle *v, EngineImageType image_type);

#endif /* VEHICLE_GUI_H */
