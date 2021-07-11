/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file vehicle_widget.h Types related to the vehicle widgets. */

#ifndef WIDGETS_VEHICLE_WIDGET_H
#define WIDGETS_VEHICLE_WIDGET_H

/** Widgets of the #VehicleViewWindow class. */
enum VehicleViewWidgets {
	WID_VV_CAPTION,            ///< Caption of window.
	WID_VV_VIEWPORT,           ///< Viewport widget.
	WID_VV_START_STOP,         ///< Start or stop this vehicle, and show information about the current state.
	WID_VV_RENAME,             ///< Rename vehicle
	WID_VV_LOCATION,           ///< Center the main view on this vehicle.
	WID_VV_ORDER_LOCATION,     ///< Center the main view on the order's target location.
	WID_VV_GOTO_DEPOT,         ///< Order this vehicle to go to the depot.
	WID_VV_REFIT,              ///< Open the refit window.
	WID_VV_SHOW_ORDERS,        ///< Show the orders of this vehicle.
	WID_VV_SHOW_DETAILS,       ///< Show details of this vehicle.
	WID_VV_CLONE,              ///< Clone this vehicle.
	WID_VV_SELECT_DEPOT_CLONE, ///< Selection widget between 'goto depot', and 'clone vehicle' buttons.
	WID_VV_SELECT_REFIT_TURN,  ///< Selection widget between 'refit' and 'turn around' buttons.
	WID_VV_TURN_AROUND,        ///< Turn this vehicle around.
	WID_VV_FORCE_PROCEED,      ///< Force this vehicle to pass a signal at danger.
};

/** Widgets of the #RefitWindow class. */
enum VehicleRefitWidgets {
	WID_VR_CAPTION,               ///< Caption of window.
	WID_VR_VEHICLE_PANEL_DISPLAY, ///< Display with a representation of the vehicle to refit.
	WID_VR_SHOW_HSCROLLBAR,       ///< Selection widget for the horizontal scrollbar.
	WID_VR_HSCROLLBAR,            ///< Horizontal scrollbar or the vehicle display.
	WID_VR_SELECT_HEADER,         ///< Header with question about the cargo to carry.
	WID_VR_MATRIX,                ///< Options to refit to.
	WID_VR_SCROLLBAR,             ///< Scrollbar for the refit options.
	WID_VR_INFO,                  ///< Information about the currently selected refit option.
	WID_VR_REFIT,                 ///< Perform the refit.
};

/** Widgets of the #VehicleDetailsWindow class. */
enum VehicleDetailsWidgets {
	WID_VD_CAPTION,                     ///< Caption of window.
	WID_VD_TOP_DETAILS,                 ///< Panel with generic details.
	WID_VD_INCREASE_SERVICING_INTERVAL, ///< Increase the servicing interval.
	WID_VD_DECREASE_SERVICING_INTERVAL, ///< Decrease the servicing interval.
	WID_VD_SERVICE_INTERVAL_DROPDOWN,   ///< Dropdown to select default/days/percent service interval.
	WID_VD_SERVICING_INTERVAL,          ///< Information about the servicing interval.
	WID_VD_MIDDLE_DETAILS,              ///< Details for non-trains.
	WID_VD_MATRIX,                      ///< List of details for trains.
	WID_VD_SCROLLBAR,                   ///< Scrollbar for train details.
	WID_VD_DETAILS_CARGO_CARRIED,       ///< Show carried cargo per part of the train.
	WID_VD_DETAILS_TRAIN_VEHICLES,      ///< Show all parts of the train with their description.
	WID_VD_DETAILS_CAPACITY_OF_EACH,    ///< Show the capacity of all train parts.
	WID_VD_DETAILS_TOTAL_CARGO,         ///< Show the capacity and carried cargo amounts aggregated per cargo of the train.
};

/** Widgets of the #VehicleListWindow class. */
enum VehicleListWidgets {
	WID_VL_CAPTION,                  ///< Caption of window (for non shared orders windows).
	WID_VL_CAPTION_SHARED_ORDERS,    ///< Caption of window (for shared orders windows).
	WID_VL_CAPTION_SELECTION,        ///< Selection for caption.
	WID_VL_ORDER_VIEW,               ///< Button to open order window (for shared orders windows).
	WID_VL_GROUP_ORDER,              ///< Group order.
	WID_VL_GROUP_BY_PULLDOWN,        ///< Group by dropdown list.
	WID_VL_SORT_ORDER,               ///< Sort order.
	WID_VL_SORT_BY_PULLDOWN,         ///< Sort by dropdown list.
	WID_VL_LIST,                     ///< List of the vehicles.
	WID_VL_SCROLLBAR,                ///< Scrollbar for the list.
	WID_VL_HIDE_BUTTONS,             ///< Selection to hide the buttons.
	WID_VL_AVAILABLE_VEHICLES,       ///< Available vehicles.
	WID_VL_MANAGE_VEHICLES_DROPDOWN, ///< Manage vehicles dropdown list.
	WID_VL_STOP_ALL,                 ///< Stop all button.
	WID_VL_START_ALL,                ///< Start all button.
};

#endif /* WIDGETS_VEHICLE_WIDGET_H */
