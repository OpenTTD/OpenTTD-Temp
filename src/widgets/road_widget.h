/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file road_widget.h Types related to the road widgets. */

#ifndef WIDGETS_ROAD_WIDGET_H
#define WIDGETS_ROAD_WIDGET_H

/** Widgets of the #BuildRoadToolbarWindow class. */
enum RoadToolbarWidgets {
	/* Name starts with RO instead of R, because of collision with RailToolbarWidgets */
	WID_ROT_CAPTION,        ///< Caption of the window
	WID_ROT_ROAD_X,         ///< Build road in x-direction.
	WID_ROT_ROAD_Y,         ///< Build road in y-direction.
	WID_ROT_AUTOROAD,       ///< Autorail.
	WID_ROT_DEMOLISH,       ///< Demolish.
	WID_ROT_DEPOT,          ///< Build depot.
	WID_ROT_BUS_STATION,    ///< Build bus station.
	WID_ROT_TRUCK_STATION,  ///< Build truck station.
	WID_ROT_ONE_WAY,        ///< Build one-way road.
	WID_ROT_BUILD_BRIDGE,   ///< Build bridge.
	WID_ROT_BUILD_TUNNEL,   ///< Build tunnel.
	WID_ROT_REMOVE,         ///< Remove road.
	WID_ROT_CONVERT_ROAD,   ///< Convert road.
};

/** Widgets of the #BuildRoadDepotWindow class. */
enum BuildRoadDepotWidgets {
	/* Name starts with BRO instead of BR, because of collision with BuildRailDepotWidgets */
	WID_BROD_CAPTION,   ///< Caption of the window.
	WID_BROD_DEPOT_NE,  ///< Depot with NE entry.
	WID_BROD_DEPOT_SE,  ///< Depot with SE entry.
	WID_BROD_DEPOT_SW,  ///< Depot with SW entry.
	WID_BROD_DEPOT_NW,  ///< Depot with NW entry.
};

/** Widgets of the #BuildRoadStationWindow class. */
enum BuildRoadStationWidgets {
	/* Name starts with BRO instead of BR, because of collision with BuildRailStationWidgets */
	WID_BROS_CAPTION,       ///< Caption of the window.
	WID_BROS_BACKGROUND,    ///< Background of the window.
	WID_BROS_STATION_NE,    ///< Terminal station with NE entry.
	WID_BROS_STATION_SE,    ///< Terminal station with SE entry.
	WID_BROS_STATION_SW,    ///< Terminal station with SW entry.
	WID_BROS_STATION_NW,    ///< Terminal station with NW entry.
	WID_BROS_STATION_X,     ///< Drive-through station in x-direction.
	WID_BROS_STATION_Y,     ///< Drive-through station in y-direction.
	WID_BROS_LT_OFF,        ///< Turn off area highlight.
	WID_BROS_LT_ON,         ///< Turn on area highlight.
	WID_BROS_INFO,          ///< Station acceptance info.

	WID_BROS_MATRIX,                     ///< Matrix widget displaying all available road stops.
	WID_BROS_IMAGE,                      ///< Panel used for each image of the matrix.
	WID_BROS_MATRIX_SCROLL,              ///< Scrollbar of the #WID_BROS_SHOW_NEWST_ADDITIONS.

	WID_BROS_SHOW_NEWST_DEFSIZE,         ///< Selection for default-size button for new road stops.
	WID_BROS_SHOW_NEWST_ADDITIONS,       ///< Selection for new class selection list.
	WID_BROS_SHOW_NEWST_MATRIX,          ///< Selection for new stop image matrix.
	WID_BROS_SHOW_NEWST_RESIZE,          ///< Selection for panel and resize at bottom right for new stops.
	WID_BROS_SHOW_NEWST_TYPE,            ///< Display of selected stop type.
	WID_BROS_NEWST_LIST,                 ///< List with new road stops.
	WID_BROS_NEWST_SCROLL,               ///< Scrollbar of the #WID_BROS_NEWST_LIST.
};

#endif /* WIDGETS_ROAD_WIDGET_H */
