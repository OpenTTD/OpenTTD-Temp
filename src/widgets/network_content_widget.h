/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file network_content_widget.h Types related to the network content widgets. */

#ifndef WIDGETS_NETWORK_CONTENT_WIDGET_H
#define WIDGETS_NETWORK_CONTENT_WIDGET_H

#include "../textfile_type.h"

/** Widgets of the #NetworkContentDownloadStatusWindow class. */
enum NetworkContentDownloadStatusWidgets {
	WID_NCDS_BACKGROUND, ///< Background of the window.
	WID_NCDS_CANCELOK,   ///< (Optional) Cancel/OK button.
};

/** Widgets of the #NetworkContentListWindow class. */
enum NetworkContentListWidgets {
	WID_NCL_BACKGROUND,     ///< Resize button.

	WID_NCL_FILTER_CAPT,    ///< Caption for the filter editbox.
	WID_NCL_FILTER,         ///< Filter editbox.

	WID_NCL_CHECKBOX,       ///< Button above checkboxes.
	WID_NCL_TYPE,           ///< 'Type' button.
	WID_NCL_NAME,           ///< 'Name' button.
	WID_NCL_UPLOAD_DATE,    ///< 'Upload date' button.

	WID_NCL_MATRIX,         ///< Panel with list of content.
	WID_NCL_SCROLLBAR,      ///< Scrollbar of matrix.

	WID_NCL_DETAILS,        ///< Panel with content details.
	WID_NCL_TEXTFILE,       ///< Open readme, changelog (+1) or license (+2) of a file in the content window.

	WID_NCL_SELECT_ALL = WID_NCL_TEXTFILE + TFT_END, ///< 'Select all' button.
	WID_NCL_SELECT_UPDATE,  ///< 'Select updates' button.
	WID_NCL_UNSELECT,       ///< 'Unselect all' button.
	WID_NCL_OPEN_URL,       ///< 'Open url' button.
	WID_NCL_CANCEL,         ///< 'Cancel' button.
	WID_NCL_DOWNLOAD,       ///< 'Download' button.

	WID_NCL_SEL_ALL_UPDATE, ///< #NWID_SELECTION widget for select all/update buttons..
	WID_NCL_SEARCH_EXTERNAL, ///< Search external sites for missing NewGRF.
};

#endif /* WIDGETS_NETWORK_CONTENT_WIDGET_H */
