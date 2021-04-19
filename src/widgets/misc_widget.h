/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file misc_widget.h Types related to the misc widgets. */

#ifndef WIDGETS_MISC_WIDGET_H
#define WIDGETS_MISC_WIDGET_H

/** Widgets of the #LandInfoWindow class. */
enum LandInfoWidgets {
	WID_LI_LOCATION, ///< Scroll to location.
	WID_LI_BACKGROUND, ///< Background of the window.
};

/** Widgets of the #TooltipsWindow class. */
enum ToolTipsWidgets {
	WID_TT_BACKGROUND, ///< Background of the window.
};

/** Widgets of the #AboutWindow class. */
enum AboutWidgets {
	WID_A_SCROLLING_TEXT, ///< The actually scrolling text.
	WID_A_WEBSITE,        ///< URL of OpenTTD website.
	WID_A_COPYRIGHT,      ///< Copyright string
};

/** Widgets of the #QueryStringWindow class. */
enum QueryStringWidgets {
	WID_QS_CAPTION, ///< Caption of the window.
	WID_QS_TEXT,    ///< Text of the query.
	WID_QS_WARNING, ///< Warning label about password security
	WID_QS_DEFAULT, ///< Default button.
	WID_QS_CANCEL,  ///< Cancel button.
	WID_QS_OK,      ///< OK button.
};

/** Widgets of the #QueryWindow class. */
enum QueryWidgets {
	WID_Q_CAPTION, ///< Caption of the window.
	WID_Q_TEXT,    ///< Text of the query.
	WID_Q_NO,      ///< Yes button.
	WID_Q_YES,     ///< No button.
};

/** Widgets of the #TextfileWindow class. */
enum TextfileWidgets {
	WID_TF_CAPTION,    ///< The caption of the window.
	WID_TF_NAVBACK,    ///< Navigate back button.
	WID_TF_NAVFORWARD, ///< Navigate forward button.
	WID_TF_WRAPTEXT,   ///< Whether or not to wrap the text.
	WID_TF_JUMPLIST,   ///< List to jump around the file.
	WID_TF_BACKGROUND, ///< Panel to draw the textfile on.
	WID_TF_VSCROLLBAR, ///< Vertical scrollbar to scroll through the textfile up-and-down.
	WID_TF_HSCROLLBAR, ///< Horizontal scrollbar to scroll through the textfile left-to-right.
};

#endif /* WIDGETS_MISC_WIDGET_H */
