/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tilehighlight_type.h Types related to highlighting tiles. */

#ifndef TILEHIGHLIGHT_TYPE_H
#define TILEHIGHLIGHT_TYPE_H

#include "core/geometry_type.hpp"
#include "window_type.h"
#include "tile_type.h"
#include "viewport_type.h"

/** Highlighting draw styles */
enum HighLightStyle {
	HT_NONE      = 0x000, ///< default
	HT_RECT      = 0x010, ///< rectangle (stations, depots, ...)
	HT_POINT     = 0x020, ///< point (lower land, raise land, level land, ...)
	HT_SPECIAL   = 0x030, ///< special mode used for highlighting while dragging (and for tunnels/docks)
	HT_DRAG      = 0x040, ///< dragging items in the depot windows
	HT_LINE      = 0x008, ///< used for autorail highlighting (longer stretches), lower bits: direction
	HT_RAIL      = 0x080, ///< autorail (one piece), lower bits: direction
	HT_VEHICLE   = 0x100, ///< vehicle is accepted as target as well (bitmask)
	HT_DIAGONAL  = 0x200, ///< Also allow 'diagonal rectangles'. Only usable in combination with #HT_RECT or #HT_POINT.
	HT_DRAG_MASK = 0x0F8, ///< Mask for the tile drag-type modes.

	/* lower bits (used with HT_LINE and HT_RAIL):
	 * (see ASCII art in table/autorail.h for a visual interpretation) */
	HT_DIR_X  = 0,     ///< X direction
	HT_DIR_Y  = 1,     ///< Y direction
	HT_DIR_HU = 2,     ///< horizontal upper
	HT_DIR_HL = 3,     ///< horizontal lower
	HT_DIR_VL = 4,     ///< vertical left
	HT_DIR_VR = 5,     ///< vertical right
	HT_DIR_END,        ///< end marker
	HT_DIR_MASK = 0x7, ///< masks the drag-direction
};
DECLARE_ENUM_AS_BIT_SET(HighLightStyle)


/** Metadata about the current highlighting. */
struct TileHighlightData {
	Point pos;           ///< Location, in tile "units", of the northern tile of the selected area.
	Point size;          ///< Size, in tile "units", of the white/red selection area.
	Point offs;          ///< Offset, in tile "units", for the blue coverage area from the selected area's northern tile.
	Point outersize;     ///< Size, in tile "units", of the blue coverage area excluding the side of the selected area.
	bool diagonal;       ///< Whether the dragged area is a 45 degrees rotated rectangle.

	bool freeze;         ///< Freeze highlight in place.

	Point new_pos;       ///< New value for \a pos; used to determine whether to redraw the selection.
	Point new_size;      ///< New value for \a size; used to determine whether to redraw the selection.
	Point new_outersize; ///< New value for \a outersize; used to determine whether to redraw the selection.
	byte dirty;          ///< Whether the build station window needs to redraw due to the changed selection.

	Point selstart;      ///< The location where the dragging started.
	Point selend;        ///< The location where the drag currently ends.
	byte sizelimit;      ///< Whether the selection is limited in length, and what the maximum length is.
	byte fixed_size;     ///< The fixed length for one of the sides.

	HighLightStyle drawstyle;      ///< Lower bits 0-3 are reserved for detailed highlight information.
	HighLightStyle next_drawstyle; ///< Queued, but not yet drawn style.

	HighLightStyle place_mode;     ///< Method which is used to place the selection.
	WindowClass window_class;      ///< The \c WindowClass of the window that is responsible for the selection mode.
	WindowNumber window_number;    ///< The \c WindowNumber of the window that is responsible for the selection mode.

	bool make_square_red;          ///< Whether to give a tile a red selection.
	TileIndex redsq;               ///< The tile that has to get a red selection.

	ViewportPlaceMethod select_method;            ///< The method which governs how tiles are selected.
	ViewportDragDropSelectionProcess select_proc; ///< The procedure that has to be called when the selection is done.

	void Reset();

	bool IsDraggingDiagonal();
	Window *GetCallbackWnd();
};

#endif /* TILEHIGHLIGHT_TYPE_H */
