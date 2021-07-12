/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file depot_widget.h Types related to the depot widgets. */

#ifndef WIDGETS_DEPOT_WIDGET_H
#define WIDGETS_DEPOT_WIDGET_H

/** Widgets of the #DepotWindow class. */
enum DepotWidgets {
	WID_D_CAPTION,         ///< Caption of window.
	WID_D_SELL,            ///< Sell button.
	WID_D_SHOW_SELL_CHAIN, ///< Show sell chain panel.
	WID_D_SELL_CHAIN,      ///< Sell chain button.
	WID_D_SELL_ALL,        ///< Sell all button.
	WID_D_AUTOREPLACE,     ///< Autoreplace button.
	WID_D_MATRIX,          ///< Matrix of vehicles.
	WID_D_V_SCROLL,        ///< Vertical scrollbar.
	WID_D_SHOW_H_SCROLL,   ///< Show horizontal scrollbar panel.
	WID_D_H_SCROLL,        ///< Horizontal scrollbar.
	WID_D_BUILD,           ///< Build button.
	WID_D_CLONE,           ///< Clone button.
	WID_D_LOCATION,        ///< Location button.
	WID_D_SHOW_RENAME,     ///< Show rename panel.
	WID_D_RENAME,          ///< Rename button.
	WID_D_HIGHLIGHT,       ///< Highlight button.
	WID_D_VEHICLE_LIST,    ///< List of vehicles.
	WID_D_STOP_ALL,        ///< Stop all button.
	WID_D_START_ALL,       ///< Start all button.
};

#endif /* WIDGETS_DEPOT_WIDGET_H */
