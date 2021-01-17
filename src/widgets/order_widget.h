/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file order_widget.h Types related to the order widgets. */

#ifndef WIDGETS_ORDER_WIDGET_H
#define WIDGETS_ORDER_WIDGET_H

/** Widgets of the #OrdersWindow class. */
enum OrderWidgets {
	WID_O_CAPTION,                   ///< Caption of the window.
	WID_O_TIMETABLE_VIEW,            ///< Toggle timetable view.
	WID_O_ORDER_LIST,                ///< Order list panel.
	WID_O_SCROLLBAR,                 ///< Order list scrollbar.
	WID_O_SKIP,                      ///< Skip current order.
	WID_O_DELETE,                    ///< Delete selected order.
	WID_O_STOP_SHARING,              ///< Stop sharing orders.
	WID_O_NON_STOP,                  ///< Goto non-stop to destination.
	WID_O_AUTOMATIC_SEPARATION,      ///< Toggle automatic separation.
	WID_O_GOTO,                      ///< Goto destination.
	WID_O_FULL_LOAD,                 ///< Select full load.
	WID_O_UNLOAD,                    ///< Select unload.
	WID_O_REFIT,                     ///< Select refit.
	WID_O_SERVICE,                   ///< Select service (at depot).
	WID_O_EMPTY,                     ///< Placeholder for refit dropdown when not owner.
	WID_O_REFIT_DROPDOWN,            ///< Open refit options.
	WID_O_COND_VARIABLE,             ///< Choose condition variable.
	WID_O_COND_COMPARATOR,           ///< Choose condition type.
	WID_O_COND_VALUE,                ///< Choose condition value.
	WID_O_SEL_TOP_LEFT,              ///< #NWID_SELECTION widget for left part of the top row of the 'your train' order window.
	WID_O_SEL_TOP_MIDDLE,            ///< #NWID_SELECTION widget for middle part of the top row of the 'your train' order window.
	WID_O_SEL_TOP_RIGHT,             ///< #NWID_SELECTION widget for right part of the top row of the 'your train' order window.
	WID_O_SEL_TOP_ROW_GROUNDVEHICLE, ///< #NWID_SELECTION widget for the top row of the 'your train' order window.
	WID_O_SEL_TOP_ROW,               ///< #NWID_SELECTION widget for the top row of the 'your non-trains' order window.
	WID_O_SEL_BOTTOM_MIDDLE,         ///< #NWID_SELECTION widget for the middle part of the bottom row of the 'your train' order window.
	WID_O_SHARED_ORDER_LIST,         ///< Open list of shared vehicles.
};

#endif /* WIDGETS_ORDER_WIDGET_H */
