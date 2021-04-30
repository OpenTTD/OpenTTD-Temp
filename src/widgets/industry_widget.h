/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file industry_widget.h Types related to the industry widgets. */

#ifndef WIDGETS_INDUSTRY_WIDGET_H
#define WIDGETS_INDUSTRY_WIDGET_H

/** Widgets of the #BuildIndustryWindow class. */
enum DynamicPlaceIndustriesWidgets {
	WID_DPI_SCENARIO_EDITOR_PANE,            ///< Pane containing SE-only widgets.
	WID_DPI_REMOVE_ALL_INDUSTRIES_WIDGET,    ///< Remove all industries button.
	WID_DPI_CREATE_RANDOM_INDUSTRIES_WIDGET, ///< Create random industries button.
	WID_DPI_MATRIX_WIDGET,                   ///< Matrix of the industries.
	WID_DPI_SCROLLBAR,                       ///< Scrollbar of the matrix.
	WID_DPI_INFOPANEL,                       ///< Info panel about the industry.
	WID_DPI_DISPLAY_WIDGET,                  ///< Display chain button.
	WID_DPI_FUND_WIDGET,                     ///< Fund button.
};

/** Widgets of the #IndustryViewWindow class. */
enum IndustryViewWidgets {
	WID_IV_CAPTION,  ///< Caption of the window.
	WID_IV_VIEWPORT, ///< Viewport of the industry.
	WID_IV_INFO,     ///< Info of the industry.
	WID_IV_GOTO,     ///< Goto button.
	WID_IV_DISPLAY,  ///< Display chain button.
};

/** Widgets of the #IndustryDirectoryWindow class. */
enum IndustryDirectoryWidgets {
	WID_ID_DROPDOWN_ORDER,       ///< Dropdown for the order of the sort.
	WID_ID_DROPDOWN_CRITERIA,    ///< Dropdown for the criteria of the sort.
	WID_ID_FILTER_BY_ACC_CARGO,  ///< Accepted cargo filter dropdown list.
	WID_ID_FILTER_BY_PROD_CARGO, ///< Produced cargo filter dropdown list.
	WID_ID_FILTER,               ///< Filter of name.
	WID_ID_INDUSTRY_LIST,        ///< Industry list.
	WID_ID_SCROLLBAR,            ///< Scrollbar of the list.
};

/** Widgets of the #IndustryCargoesWindow class */
enum IndustryCargoesWidgets {
	WID_IC_CAPTION,        ///< Caption of the window.
	WID_IC_NOTIFY,         ///< Row of buttons at the bottom.
	WID_IC_PANEL,          ///< Panel that shows the chain.
	WID_IC_SCROLLBAR,      ///< Scrollbar of the panel.
	WID_IC_CARGO_DROPDOWN, ///< Select cargo dropdown.
	WID_IC_IND_DROPDOWN,   ///< Select industry dropdown.
};

#endif /* WIDGETS_INDUSTRY_WIDGET_H */
