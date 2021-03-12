/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file group_widget.h Types related to the group widgets. */

#ifndef WIDGETS_GROUP_WIDGET_H
#define WIDGETS_GROUP_WIDGET_H

/** Widgets of the #VehicleGroupWindow class. */
enum GroupListWidgets {
	WID_GL_CAPTION,                  ///< Caption of the window.
	WID_GL_GROUP_BY_ORDER,           ///< Group order.
	WID_GL_GROUP_BY_DROPDOWN,        ///< Group by dropdown list.
	WID_GL_SORT_BY_ORDER,            ///< Sort order.
	WID_GL_SORT_BY_DROPDOWN,         ///< Sort by dropdown list.
	WID_GL_LIST_VEHICLE,             ///< List of the vehicles.
	WID_GL_LIST_VEHICLE_SCROLLBAR,   ///< Scrollbar for the list.
	WID_GL_AVAILABLE_VEHICLES,       ///< Available vehicles.
	WID_GL_MANAGE_VEHICLES_DROPDOWN, ///< Manage vehicles dropdown list.
	WID_GL_STOP_ALL,                 ///< Stop all button.
	WID_GL_START_ALL,                ///< Start all button.

	WID_GL_ALL_VEHICLES,             ///< All vehicles entry.
	WID_GL_DEFAULT_VEHICLES,         ///< Default vehicles entry.
	WID_GL_LIST_GROUP,               ///< List of the groups.
	WID_GL_LIST_GROUP_SCROLLBAR,     ///< Scrollbar for the list.
	WID_GL_CREATE_GROUP,             ///< Create group button. In the case of drag and drop create a group with vehicle specific name and add the vehicle to it.
	WID_GL_DELETE_GROUP,             ///< Delete group button.
	WID_GL_RENAME_GROUP,             ///< Rename group button.
	WID_GL_LIVERY_GROUP,             ///< Group livery button.
	WID_GL_COLLAPSE_ALL_GROUPS,      ///< Collapse all groups button.
	WID_GL_EXPAND_ALL_GROUPS,        ///< Expand all groups button.
	WID_GL_AUTO_GROUP,               ///< Auto group all ungrouped vehicles.
	WID_GL_REPLACE_PROTECTION,       ///< Replace protection button.
	WID_GL_INFO,                     ///< Group info.
};

#endif /* WIDGETS_GROUP_WIDGET_H */
