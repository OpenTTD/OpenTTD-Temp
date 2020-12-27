/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file viewport.cpp Handling of all viewports.
 *
 * \verbatim
 * The in-game coordinate system looks like this *
 *                                               *
 *                    ^ Z                        *
 *                    |                          *
 *                    |                          *
 *                    |                          *
 *                    |                          *
 *                 /     \                       *
 *              /           \                    *
 *           /                 \                 *
 *        /                       \              *
 *   X <                             > Y         *
 * \endverbatim
 */

/**
 * @defgroup vp_column_row Rows and columns in the viewport
 *
 * Columns are vertical sections of the viewport that are half a tile wide.
 * The origin, i.e. column 0, is through the northern and southern most tile.
 * This means that the column of e.g. Tile(0, 0) and Tile(100, 100) are in
 * column number 0. The negative columns are towards the left of the screen,
 * or towards the west, whereas the positive ones are towards respectively
 * the right and east.
 * With half a tile wide is meant that the next column of tiles directly west
 * or east of the centre line are respectively column -1 and 1. Their tile
 * centers are only half a tile from the center of their adjoining tile when
 * looking only at the X-coordinate.
 *
 * \verbatim
 *        ╳        *
 *       ╱ ╲       *
 *      ╳ 0 ╳      *
 *     ╱ ╲ ╱ ╲     *
 *    ╳-1 ╳ 1 ╳    *
 *   ╱ ╲ ╱ ╲ ╱ ╲   *
 *  ╳-2 ╳ 0 ╳ 2 ╳  *
 *   ╲ ╱ ╲ ╱ ╲ ╱   *
 *    ╳-1 ╳ 1 ╳    *
 *     ╲ ╱ ╲ ╱     *
 *      ╳ 0 ╳      *
 *       ╲ ╱       *
 *        ╳        *
 * \endverbatim
 *
 *
 * Rows are horizontal sections of the viewport, also half a tile wide.
 * This time the northern most tile on the map defines 0 and
 * everything south of that has a positive number.
 */

#include "stdafx.h"
#include "landscape.h"
#include "viewport_func.h"
#include "station_base.h"
#include "waypoint_base.h"
#include "town.h"
#include "signs_base.h"
#include "signs_func.h"
#include "vehicle_base.h"
#include "vehicle_gui.h"
#include "blitter/factory.hpp"
#include "strings_func.h"
#include "zoom_func.h"
#include "vehicle_func.h"
#include "company_func.h"
#include "waypoint_func.h"
#include "window_func.h"
#include "tilehighlight_func.h"
#include "window_gui.h"
#include "linkgraph/linkgraph_gui.h"
#include "viewport_kdtree.h"
#include "town_kdtree.h"
#include "viewport_sprite_sorter.h"
#include "bridge_map.h"
#include "company_base.h"
#include "command_func.h"
#include "network/network_func.h"
#include "framerate_type.h"
#include "depot_map.h"

#include <forward_list>
#include <map>
#include <stack>

#include "table/strings.h"
#include "table/string_colours.h"

#include "safeguards.h"

Point _tile_fract_coords;


ViewportSignKdtree _viewport_sign_kdtree(&Kdtree_ViewportSignXYFunc);
static int _viewport_sign_maxwidth = 0;


static const int MAX_TILE_EXTENT_LEFT   = ZOOM_LVL_BASE * TILE_PIXELS;                     ///< Maximum left   extent of tile relative to north corner.
static const int MAX_TILE_EXTENT_RIGHT  = ZOOM_LVL_BASE * TILE_PIXELS;                     ///< Maximum right  extent of tile relative to north corner.
static const int MAX_TILE_EXTENT_TOP    = ZOOM_LVL_BASE * MAX_BUILDING_PIXELS;             ///< Maximum top    extent of tile relative to north corner (not considering bridges).
static const int MAX_TILE_EXTENT_BOTTOM = ZOOM_LVL_BASE * (TILE_PIXELS + 2 * TILE_HEIGHT); ///< Maximum bottom extent of tile relative to north corner (worst case: #SLOPE_STEEP_N).

struct StringSpriteToDraw {
	StringID string;
	Colours colour;
	int32 x;
	int32 y;
	uint64 params[2];
	uint16 width;
};

struct TileSpriteToDraw {
	SpriteID image;
	PaletteID pal;
	const SubSprite *sub;           ///< only draw a rectangular part of the sprite
	int32 x;                        ///< screen X coordinate of sprite
	int32 y;                        ///< screen Y coordinate of sprite
};

struct ChildScreenSpriteToDraw {
	SpriteID image;
	PaletteID pal;
	const SubSprite *sub;           ///< only draw a rectangular part of the sprite
	int32 x;
	int32 y;
	int next;                       ///< next child to draw (-1 at the end)
};

/** Enumeration of multi-part foundations */
enum FoundationPart {
	FOUNDATION_PART_NONE     = 0xFF,  ///< Neither foundation nor groundsprite drawn yet.
	FOUNDATION_PART_NORMAL   = 0,     ///< First part (normal foundation or no foundation)
	FOUNDATION_PART_HALFTILE = 1,     ///< Second part (halftile foundation)
	FOUNDATION_PART_END
};

/**
 * Mode of "sprite combining"
 * @see StartSpriteCombine
 */
enum SpriteCombineMode {
	SPRITE_COMBINE_NONE,     ///< Every #AddSortableSpriteToDraw start its own bounding box
	SPRITE_COMBINE_PENDING,  ///< %Sprite combining will start with the next unclipped sprite.
	SPRITE_COMBINE_ACTIVE,   ///< %Sprite combining is active. #AddSortableSpriteToDraw outputs child sprites.
};

typedef std::vector<TileSpriteToDraw> TileSpriteToDrawVector;
typedef std::vector<StringSpriteToDraw> StringSpriteToDrawVector;
typedef std::vector<ParentSpriteToDraw> ParentSpriteToDrawVector;
typedef std::vector<ChildScreenSpriteToDraw> ChildScreenSpriteToDrawVector;

/** Data structure storing rendering information */
struct ViewportDrawer {
	DrawPixelInfo dpi;

	StringSpriteToDrawVector string_sprites_to_draw;
	TileSpriteToDrawVector tile_sprites_to_draw;
	ParentSpriteToDrawVector parent_sprites_to_draw;
	ParentSpriteToSortVector parent_sprites_to_sort; ///< Parent sprite pointer array used for sorting
	ChildScreenSpriteToDrawVector child_screen_sprites_to_draw;

	int *last_child;

	SpriteCombineMode combine_sprites;               ///< Current mode of "sprite combining". @see StartSpriteCombine

	int foundation[FOUNDATION_PART_END];             ///< Foundation sprites (index into parent_sprites_to_draw).
	FoundationPart foundation_part;                  ///< Currently active foundation for ground sprite drawing.
	int *last_foundation_child[FOUNDATION_PART_END]; ///< Tail of ChildSprite list of the foundations. (index into child_screen_sprites_to_draw)
	Point foundation_offset[FOUNDATION_PART_END];    ///< Pixel offset for ground sprites on the foundations.
};

static bool MarkViewportDirty(const Viewport *vp, int left, int top, int right, int bottom);

static ViewportDrawer _vd;

TileHighlightData _thd;
static TileInfo *_cur_ti;
bool _draw_bounding_boxes = false;
bool _draw_dirty_blocks = false;
uint _dirty_block_colour = 0;
static VpSpriteSorter _vp_sprite_sorter = nullptr;

static Point MapXYZToViewport(const Viewport *vp, int x, int y, int z)
{
	Point p = RemapCoords(x, y, z);
	p.x -= vp->virtual_width / 2;
	p.y -= vp->virtual_height / 2;
	return p;
}

void DeleteWindowViewport(Window *w)
{
	if (w->viewport == nullptr) return;

	delete w->viewport->overlay;
	free(w->viewport);
	w->viewport = nullptr;
}

/**
 * Initialize viewport of the window for use.
 * @param w Window to use/display the viewport in
 * @param x Offset of left edge of viewport with respect to left edge window \a w
 * @param y Offset of top edge of viewport with respect to top edge window \a w
 * @param width Width of the viewport
 * @param height Height of the viewport
 * @param follow_flags Flags controlling the viewport.
 *        - If bit 31 is set, the lower 20 bits are the vehicle that the viewport should follow.
 *        - If bit 31 is clear, it is a #TileIndex.
 * @param zoom Zoomlevel to display
 */
void InitializeWindowViewport(Window *w, int x, int y,
	int width, int height, uint32 follow_flags, ZoomLevel zoom)
{
	assert(w->viewport == nullptr);

	ViewportData *vp = CallocT<ViewportData>(1);

	vp->left = x + w->left;
	vp->top = y + w->top;
	vp->width = width;
	vp->height = height;

	vp->zoom = static_cast<ZoomLevel>(Clamp(zoom, _settings_client.gui.zoom_min, _settings_client.gui.zoom_max));

	vp->virtual_width = ScaleByZoom(width, zoom);
	vp->virtual_height = ScaleByZoom(height, zoom);

	Point pt;

	if (follow_flags & 0x80000000) {
		const Vehicle *veh;

		vp->follow_vehicle = (VehicleID)(follow_flags & 0xFFFFF);
		veh = Vehicle::Get(vp->follow_vehicle);
		pt = MapXYZToViewport(vp, veh->x_pos, veh->y_pos, veh->z_pos);
	} else {
		x = TileX(follow_flags) * TILE_SIZE;
		y = TileY(follow_flags) * TILE_SIZE;

		vp->follow_vehicle = INVALID_VEHICLE;
		pt = MapXYZToViewport(vp, x, y, GetSlopePixelZ(x, y));
	}

	vp->scrollpos_x = pt.x;
	vp->scrollpos_y = pt.y;
	vp->dest_scrollpos_x = pt.x;
	vp->dest_scrollpos_y = pt.y;

	vp->overlay = nullptr;

	w->viewport = vp;
	vp->virtual_left = 0; // pt.x;
	vp->virtual_top = 0;  // pt.y;
}

static Point _vp_move_offs;

static void DoSetViewportPosition(Window::IteratorToFront it, int left, int top, int width, int height)
{
	for (; !it.IsEnd(); ++it) {
		const Window *w = *it;
		if (left + width > w->left &&
				w->left + w->width > left &&
				top + height > w->top &&
				w->top + w->height > top) {

			if (left < w->left) {
				DoSetViewportPosition(it, left, top, w->left - left, height);
				DoSetViewportPosition(it, left + (w->left - left), top, width - (w->left - left), height);
				return;
			}

			if (left + width > w->left + w->width) {
				DoSetViewportPosition(it, left, top, (w->left + w->width - left), height);
				DoSetViewportPosition(it, left + (w->left + w->width - left), top, width - (w->left + w->width - left), height);
				return;
			}

			if (top < w->top) {
				DoSetViewportPosition(it, left, top, width, (w->top - top));
				DoSetViewportPosition(it, left, top + (w->top - top), width, height - (w->top - top));
				return;
			}

			if (top + height > w->top + w->height) {
				DoSetViewportPosition(it, left, top, width, (w->top + w->height - top));
				DoSetViewportPosition(it, left, top + (w->top + w->height - top), width, height - (w->top + w->height - top));
				return;
			}

			return;
		}
	}

	{
		int xo = _vp_move_offs.x;
		int yo = _vp_move_offs.y;

		if (abs(xo) >= width || abs(yo) >= height) {
			/* fully_outside */
			RedrawScreenRect(left, top, left + width, top + height);
			return;
		}

		GfxScroll(left, top, width, height, xo, yo);

		if (xo > 0) {
			RedrawScreenRect(left, top, xo + left, top + height);
			left += xo;
			width -= xo;
		} else if (xo < 0) {
			RedrawScreenRect(left + width + xo, top, left + width, top + height);
			width += xo;
		}

		if (yo > 0) {
			RedrawScreenRect(left, top, width + left, top + yo);
		} else if (yo < 0) {
			RedrawScreenRect(left, top + height + yo, width + left, top + height);
		}
	}
}

static void SetViewportPosition(Window *w, int x, int y)
{
	Viewport *vp = w->viewport;
	int old_left = vp->virtual_left;
	int old_top = vp->virtual_top;
	int i;
	int left, top, width, height;

	vp->virtual_left = x;
	vp->virtual_top = y;

	/* Viewport is bound to its left top corner, so it must be rounded down (UnScaleByZoomLower)
	 * else glitch described in FS#1412 will happen (offset by 1 pixel with zoom level > NORMAL)
	 */
	old_left = UnScaleByZoomLower(old_left, vp->zoom);
	old_top = UnScaleByZoomLower(old_top, vp->zoom);
	x = UnScaleByZoomLower(x, vp->zoom);
	y = UnScaleByZoomLower(y, vp->zoom);

	old_left -= x;
	old_top -= y;

	if (old_top == 0 && old_left == 0) return;

	_vp_move_offs.x = old_left;
	_vp_move_offs.y = old_top;

	left = vp->left;
	top = vp->top;
	width = vp->width;
	height = vp->height;

	if (left < 0) {
		width += left;
		left = 0;
	}

	i = left + width - _screen.width;
	if (i >= 0) width -= i;

	if (width > 0) {
		if (top < 0) {
			height += top;
			top = 0;
		}

		i = top + height - _screen.height;
		if (i >= 0) height -= i;

		if (height > 0) {
			Window::IteratorToFront it(w);
			++it;
			DoSetViewportPosition(it, left, top, width, height);
		}
	}
}

/**
 * Is a xy position inside the viewport of the window?
 * @param w Window to examine its viewport
 * @param x X coordinate of the xy position
 * @param y Y coordinate of the xy position
 * @return Pointer to the viewport if the xy position is in the viewport of the window,
 *         otherwise \c nullptr is returned.
 */
Viewport *IsPtInWindowViewport(const Window *w, int x, int y)
{
	Viewport *vp = w->viewport;

	if (vp != nullptr &&
			IsInsideMM(x, vp->left, vp->left + vp->width) &&
			IsInsideMM(y, vp->top, vp->top + vp->height))
		return vp;

	return nullptr;
}

/**
 * Translate screen coordinate in a viewport to underlying tile coordinate.
 *
 * Returns exact point of the map that is visible in the given place
 * of the viewport (3D perspective), height of tiles and foundations matter.
 *
 * @param vp  Viewport that contains the (\a x, \a y) screen coordinate
 * @param x   Screen x coordinate, distance in pixels from the left edge of viewport frame
 * @param y   Screen y coordinate, distance in pixels from the top edge of viewport frame
 * @param clamp_to_map Clamp the coordinate outside of the map to the closest, non-void tile within the map
 * @return Tile coordinate or (-1, -1) if given x or y is not within viewport frame
 */
Point TranslateXYToTileCoord(const Viewport *vp, int x, int y, bool clamp_to_map)
{
	if (!IsInsideBS(x, vp->left, vp->width) || !IsInsideBS(y, vp->top, vp->height)) {
		Point pt = { -1, -1 };
		return pt;
	}

	return InverseRemapCoords2(
			ScaleByZoom(x - vp->left, vp->zoom) + vp->virtual_left,
			ScaleByZoom(y - vp->top, vp->zoom) + vp->virtual_top, clamp_to_map);
}

/* When used for zooming, check area below current coordinates (x,y)
 * and return the tile of the zoomed out/in position (zoom_x, zoom_y)
 * when you just want the tile, make x = zoom_x and y = zoom_y */
static Point GetTileFromScreenXY(int x, int y, int zoom_x, int zoom_y)
{
	Window *w;
	Viewport *vp;
	Point pt;

	if ( (w = FindWindowFromPt(x, y)) != nullptr &&
			 (vp = IsPtInWindowViewport(w, x, y)) != nullptr)
				return TranslateXYToTileCoord(vp, zoom_x, zoom_y);

	pt.y = pt.x = -1;
	return pt;
}

Point GetTileBelowCursor()
{
	return GetTileFromScreenXY(_cursor.pos.x, _cursor.pos.y, _cursor.pos.x, _cursor.pos.y);
}


Point GetTileZoomCenterWindow(bool in, Window * w)
{
	int x, y;
	Viewport *vp = w->viewport;

	if (in) {
		x = ((_cursor.pos.x - vp->left) >> 1) + (vp->width >> 2);
		y = ((_cursor.pos.y - vp->top) >> 1) + (vp->height >> 2);
	} else {
		x = vp->width - (_cursor.pos.x - vp->left);
		y = vp->height - (_cursor.pos.y - vp->top);
	}
	/* Get the tile below the cursor and center on the zoomed-out center */
	return GetTileFromScreenXY(_cursor.pos.x, _cursor.pos.y, x + vp->left, y + vp->top);
}

/**
 * Update the status of the zoom-buttons according to the zoom-level
 * of the viewport. This will update their status and invalidate accordingly
 * @param w Window pointer to the window that has the zoom buttons
 * @param vp pointer to the viewport whose zoom-level the buttons represent
 * @param widget_zoom_in widget index for window with zoom-in button
 * @param widget_zoom_out widget index for window with zoom-out button
 */
void HandleZoomMessage(Window *w, const Viewport *vp, byte widget_zoom_in, byte widget_zoom_out)
{
	w->SetWidgetDisabledState(widget_zoom_in, vp->zoom <= _settings_client.gui.zoom_min);
	w->SetWidgetDirty(widget_zoom_in);

	w->SetWidgetDisabledState(widget_zoom_out, vp->zoom >= _settings_client.gui.zoom_max);
	w->SetWidgetDirty(widget_zoom_out);
}

/**
 * Schedules a tile sprite for drawing.
 *
 * @param image the image to draw.
 * @param pal the provided palette.
 * @param x position x (world coordinates) of the sprite.
 * @param y position y (world coordinates) of the sprite.
 * @param z position z (world coordinates) of the sprite.
 * @param sub Only draw a part of the sprite.
 * @param extra_offs_x Pixel X offset for the sprite position.
 * @param extra_offs_y Pixel Y offset for the sprite position.
 */
static void AddTileSpriteToDraw(SpriteID image, PaletteID pal, int32 x, int32 y, int z, const SubSprite *sub = nullptr, int extra_offs_x = 0, int extra_offs_y = 0)
{
	assert((image & SPRITE_MASK) < MAX_SPRITES);

	TileSpriteToDraw &ts = _vd.tile_sprites_to_draw.emplace_back();
	ts.image = image;
	ts.pal = pal;
	ts.sub = sub;
	Point pt = RemapCoords(x, y, z);
	ts.x = pt.x + extra_offs_x;
	ts.y = pt.y + extra_offs_y;
}

/**
 * Adds a child sprite to the active foundation.
 *
 * The pixel offset of the sprite relative to the ParentSprite is the sum of the offset passed to OffsetGroundSprite() and extra_offs_?.
 *
 * @param image the image to draw.
 * @param pal the provided palette.
 * @param sub Only draw a part of the sprite.
 * @param foundation_part Foundation part.
 * @param extra_offs_x Pixel X offset for the sprite position.
 * @param extra_offs_y Pixel Y offset for the sprite position.
 */
static void AddChildSpriteToFoundation(SpriteID image, PaletteID pal, const SubSprite *sub, FoundationPart foundation_part, int extra_offs_x, int extra_offs_y)
{
	assert(IsInsideMM(foundation_part, 0, FOUNDATION_PART_END));
	assert(_vd.foundation[foundation_part] != -1);
	Point offs = _vd.foundation_offset[foundation_part];

	/* Change the active ChildSprite list to the one of the foundation */
	int *old_child = _vd.last_child;
	_vd.last_child = _vd.last_foundation_child[foundation_part];

	AddChildSpriteScreen(image, pal, offs.x + extra_offs_x, offs.y + extra_offs_y, false, sub, false);

	/* Switch back to last ChildSprite list */
	_vd.last_child = old_child;
}

/**
 * Draws a ground sprite at a specific world-coordinate relative to the current tile.
 * If the current tile is drawn on top of a foundation the sprite is added as child sprite to the "foundation"-ParentSprite.
 *
 * @param image the image to draw.
 * @param pal the provided palette.
 * @param x position x (world coordinates) of the sprite relative to current tile.
 * @param y position y (world coordinates) of the sprite relative to current tile.
 * @param z position z (world coordinates) of the sprite relative to current tile.
 * @param sub Only draw a part of the sprite.
 * @param extra_offs_x Pixel X offset for the sprite position.
 * @param extra_offs_y Pixel Y offset for the sprite position.
 */
void DrawGroundSpriteAt(SpriteID image, PaletteID pal, int32 x, int32 y, int z, const SubSprite *sub, int extra_offs_x, int extra_offs_y)
{
	/* Switch to first foundation part, if no foundation was drawn */
	if (_vd.foundation_part == FOUNDATION_PART_NONE) _vd.foundation_part = FOUNDATION_PART_NORMAL;

	if (_vd.foundation[_vd.foundation_part] != -1) {
		Point pt = RemapCoords(x, y, z);
		AddChildSpriteToFoundation(image, pal, sub, _vd.foundation_part, pt.x + extra_offs_x * ZOOM_LVL_BASE, pt.y + extra_offs_y * ZOOM_LVL_BASE);
	} else {
		AddTileSpriteToDraw(image, pal, _cur_ti->x + x, _cur_ti->y + y, _cur_ti->z + z, sub, extra_offs_x * ZOOM_LVL_BASE, extra_offs_y * ZOOM_LVL_BASE);
	}
}

/**
 * Draws a ground sprite for the current tile.
 * If the current tile is drawn on top of a foundation the sprite is added as child sprite to the "foundation"-ParentSprite.
 *
 * @param image the image to draw.
 * @param pal the provided palette.
 * @param sub Only draw a part of the sprite.
 * @param extra_offs_x Pixel X offset for the sprite position.
 * @param extra_offs_y Pixel Y offset for the sprite position.
 */
void DrawGroundSprite(SpriteID image, PaletteID pal, const SubSprite *sub, int extra_offs_x, int extra_offs_y)
{
	DrawGroundSpriteAt(image, pal, 0, 0, 0, sub, extra_offs_x, extra_offs_y);
}

/**
 * Called when a foundation has been drawn for the current tile.
 * Successive ground sprites for the current tile will be drawn as child sprites of the "foundation"-ParentSprite, not as TileSprites.
 *
 * @param x sprite x-offset (screen coordinates) of ground sprites relative to the "foundation"-ParentSprite.
 * @param y sprite y-offset (screen coordinates) of ground sprites relative to the "foundation"-ParentSprite.
 */
void OffsetGroundSprite(int x, int y)
{
	/* Switch to next foundation part */
	switch (_vd.foundation_part) {
		case FOUNDATION_PART_NONE:
			_vd.foundation_part = FOUNDATION_PART_NORMAL;
			break;
		case FOUNDATION_PART_NORMAL:
			_vd.foundation_part = FOUNDATION_PART_HALFTILE;
			break;
		default: NOT_REACHED();
	}

	/* _vd.last_child == nullptr if foundation sprite was clipped by the viewport bounds */
	if (_vd.last_child != nullptr) _vd.foundation[_vd.foundation_part] = (uint)_vd.parent_sprites_to_draw.size() - 1;

	_vd.foundation_offset[_vd.foundation_part].x = x * ZOOM_LVL_BASE;
	_vd.foundation_offset[_vd.foundation_part].y = y * ZOOM_LVL_BASE;
	_vd.last_foundation_child[_vd.foundation_part] = _vd.last_child;
}

/**
 * Adds a child sprite to a parent sprite.
 * In contrast to "AddChildSpriteScreen()" the sprite position is in world coordinates
 *
 * @param image the image to draw.
 * @param pal the provided palette.
 * @param x position x of the sprite.
 * @param y position y of the sprite.
 * @param z position z of the sprite.
 * @param sub Only draw a part of the sprite.
 */
static void AddCombinedSprite(SpriteID image, PaletteID pal, int x, int y, int z, const SubSprite *sub)
{
	Point pt = RemapCoords(x, y, z);
	const Sprite *spr = GetSprite(image & SPRITE_MASK, ST_NORMAL);

	if (pt.x + spr->x_offs >= _vd.dpi.left + _vd.dpi.width ||
			pt.x + spr->x_offs + spr->width <= _vd.dpi.left ||
			pt.y + spr->y_offs >= _vd.dpi.top + _vd.dpi.height ||
			pt.y + spr->y_offs + spr->height <= _vd.dpi.top)
		return;

	const ParentSpriteToDraw &pstd = _vd.parent_sprites_to_draw.back();
	AddChildSpriteScreen(image, pal, pt.x - pstd.left, pt.y - pstd.top, false, sub, false);
}

/**
 * Draw a (transparent) sprite at given coordinates with a given bounding box.
 * The bounding box extends from (x + bb_offset_x, y + bb_offset_y, z + bb_offset_z) to (x + w - 1, y + h - 1, z + dz - 1), both corners included.
 * Bounding boxes with bb_offset_x == w or bb_offset_y == h or bb_offset_z == dz are allowed and produce thin slices.
 *
 * @note Bounding boxes are normally specified with bb_offset_x = bb_offset_y = bb_offset_z = 0. The extent of the bounding box in negative direction is
 *       defined by the sprite offset in the grf file.
 *       However if modifying the sprite offsets is not suitable (e.g. when using existing graphics), the bounding box can be tuned by bb_offset.
 *
 * @pre w >= bb_offset_x, h >= bb_offset_y, dz >= bb_offset_z. Else w, h or dz are ignored.
 *
 * @param image the image to combine and draw,
 * @param pal the provided palette,
 * @param x position X (world) of the sprite,
 * @param y position Y (world) of the sprite,
 * @param w bounding box extent towards positive X (world),
 * @param h bounding box extent towards positive Y (world),
 * @param dz bounding box extent towards positive Z (world),
 * @param z position Z (world) of the sprite,
 * @param transparent if true, switch the palette between the provided palette and the transparent palette,
 * @param bb_offset_x bounding box extent towards negative X (world),
 * @param bb_offset_y bounding box extent towards negative Y (world),
 * @param bb_offset_z bounding box extent towards negative Z (world)
 * @param sub Only draw a part of the sprite.
 */
void AddSortableSpriteToDraw(SpriteID image, PaletteID pal, int x, int y, int w, int h, int dz, int z, bool transparent, int bb_offset_x, int bb_offset_y, int bb_offset_z, const SubSprite *sub)
{
	int32 left, right, top, bottom;

	assert((image & SPRITE_MASK) < MAX_SPRITES);

	/* make the sprites transparent with the right palette */
	if (transparent) {
		SetBit(image, PALETTE_MODIFIER_TRANSPARENT);
		pal = PALETTE_TO_TRANSPARENT;
	}

	if (_vd.combine_sprites == SPRITE_COMBINE_ACTIVE) {
		AddCombinedSprite(image, pal, x, y, z, sub);
		return;
	}

	_vd.last_child = nullptr;

	Point pt = RemapCoords(x, y, z);
	int tmp_left, tmp_top, tmp_x = pt.x, tmp_y = pt.y;

	/* Compute screen extents of sprite */
	if (image == SPR_EMPTY_BOUNDING_BOX) {
		left = tmp_left = RemapCoords(x + w          , y + bb_offset_y, z + bb_offset_z).x;
		right           = RemapCoords(x + bb_offset_x, y + h          , z + bb_offset_z).x + 1;
		top  = tmp_top  = RemapCoords(x + bb_offset_x, y + bb_offset_y, z + dz         ).y;
		bottom          = RemapCoords(x + w          , y + h          , z + bb_offset_z).y + 1;
	} else {
		const Sprite *spr = GetSprite(image & SPRITE_MASK, ST_NORMAL);
		left = tmp_left = (pt.x += spr->x_offs);
		right           = (pt.x +  spr->width );
		top  = tmp_top  = (pt.y += spr->y_offs);
		bottom          = (pt.y +  spr->height);
	}

	if (_draw_bounding_boxes && (image != SPR_EMPTY_BOUNDING_BOX)) {
		/* Compute maximal extents of sprite and its bounding box */
		left   = std::min(left  , RemapCoords(x + w          , y + bb_offset_y, z + bb_offset_z).x);
		right  = std::max(right , RemapCoords(x + bb_offset_x, y + h          , z + bb_offset_z).x + 1);
		top    = std::min(top   , RemapCoords(x + bb_offset_x, y + bb_offset_y, z + dz         ).y);
		bottom = std::max(bottom, RemapCoords(x + w          , y + h          , z + bb_offset_z).y + 1);
	}

	/* Do not add the sprite to the viewport, if it is outside */
	if (left   >= _vd.dpi.left + _vd.dpi.width ||
	    right  <= _vd.dpi.left                 ||
	    top    >= _vd.dpi.top + _vd.dpi.height ||
	    bottom <= _vd.dpi.top) {
		return;
	}

	ParentSpriteToDraw &ps = _vd.parent_sprites_to_draw.emplace_back();
	ps.x = tmp_x;
	ps.y = tmp_y;

	ps.left = tmp_left;
	ps.top  = tmp_top;

	ps.image = image;
	ps.pal = pal;
	ps.sub = sub;
	ps.xmin = x + bb_offset_x;
	ps.xmax = x + std::max(bb_offset_x, w) - 1;

	ps.ymin = y + bb_offset_y;
	ps.ymax = y + std::max(bb_offset_y, h) - 1;

	ps.zmin = z + bb_offset_z;
	ps.zmax = z + std::max(bb_offset_z, dz) - 1;

	ps.first_child = -1;

	_vd.last_child = &ps.first_child;

	if (_vd.combine_sprites == SPRITE_COMBINE_PENDING) _vd.combine_sprites = SPRITE_COMBINE_ACTIVE;
}

/**
 * Starts a block of sprites, which are "combined" into a single bounding box.
 *
 * Subsequent calls to #AddSortableSpriteToDraw will be drawn into the same bounding box.
 * That is: The first sprite that is not clipped by the viewport defines the bounding box, and
 * the following sprites will be child sprites to that one.
 *
 * That implies:
 *  - The drawing order is definite. No other sprites will be sorted between those of the block.
 *  - You have to provide a valid bounding box for all sprites,
 *    as you won't know which one is the first non-clipped one.
 *    Preferable you use the same bounding box for all.
 *  - You cannot use #AddChildSpriteScreen inside the block, as its result will be indefinite.
 *
 * The block is terminated by #EndSpriteCombine.
 *
 * You cannot nest "combined" blocks.
 */
void StartSpriteCombine()
{
	assert(_vd.combine_sprites == SPRITE_COMBINE_NONE);
	_vd.combine_sprites = SPRITE_COMBINE_PENDING;
}

/**
 * Terminates a block of sprites started by #StartSpriteCombine.
 * Take a look there for details.
 */
void EndSpriteCombine()
{
	assert(_vd.combine_sprites != SPRITE_COMBINE_NONE);
	_vd.combine_sprites = SPRITE_COMBINE_NONE;
}

/**
 * Check if the parameter "check" is inside the interval between
 * begin and end, including both begin and end.
 * @note Whether \c begin or \c end is the biggest does not matter.
 *       This method will account for that.
 * @param begin The begin of the interval.
 * @param end   The end of the interval.
 * @param check The value to check.
 */
static bool IsInRangeInclusive(int begin, int end, int check)
{
	if (begin > end) Swap(begin, end);
	return begin <= check && check <= end;
}

/**
 * Checks whether a point is inside the selected a diagonal rectangle given by _thd.size and _thd.pos
 * @param x The x coordinate of the point to be checked.
 * @param y The y coordinate of the point to be checked.
 * @return True if the point is inside the rectangle, else false.
 */
bool IsInsideRotatedRectangle(int x, int y)
{
	int dist_a = (_thd.size.x + _thd.size.y);      // Rotated coordinate system for selected rectangle.
	int dist_b = (_thd.size.x - _thd.size.y);      // We don't have to divide by 2. It's all relative!
	int a = ((x - _thd.pos.x) + (y - _thd.pos.y)); // Rotated coordinate system for the point under scrutiny.
	int b = ((x - _thd.pos.x) - (y - _thd.pos.y));

	/* Check if a and b are between 0 and dist_a or dist_b respectively. */
	return IsInRangeInclusive(dist_a, 0, a) && IsInRangeInclusive(dist_b, 0, b);
}

/**
 * Add a child sprite to a parent sprite.
 *
 * @param image the image to draw.
 * @param pal the provided palette.
 * @param x sprite x-offset (screen coordinates) relative to parent sprite.
 * @param y sprite y-offset (screen coordinates) relative to parent sprite.
 * @param transparent if true, switch the palette between the provided palette and the transparent palette,
 * @param sub Only draw a part of the sprite.
 */
void AddChildSpriteScreen(SpriteID image, PaletteID pal, int x, int y, bool transparent, const SubSprite *sub, bool scale)
{
	assert((image & SPRITE_MASK) < MAX_SPRITES);

	/* If the ParentSprite was clipped by the viewport bounds, do not draw the ChildSprites either */
	if (_vd.last_child == nullptr) return;

	/* make the sprites transparent with the right palette */
	if (transparent) {
		SetBit(image, PALETTE_MODIFIER_TRANSPARENT);
		pal = PALETTE_TO_TRANSPARENT;
	}

	*_vd.last_child = (uint)_vd.child_screen_sprites_to_draw.size();

	ChildScreenSpriteToDraw &cs = _vd.child_screen_sprites_to_draw.emplace_back();
	cs.image = image;
	cs.pal = pal;
	cs.sub = sub;
	cs.x = scale ? x * ZOOM_LVL_BASE : x;
	cs.y = scale ? y * ZOOM_LVL_BASE : y;
	cs.next = -1;

	/* Append the sprite to the active ChildSprite list.
	 * If the active ParentSprite is a foundation, update last_foundation_child as well.
	 * Note: ChildSprites of foundations are NOT sequential in the vector, as selection sprites are added at last. */
	if (_vd.last_foundation_child[0] == _vd.last_child) _vd.last_foundation_child[0] = &cs.next;
	if (_vd.last_foundation_child[1] == _vd.last_child) _vd.last_foundation_child[1] = &cs.next;
	_vd.last_child = &cs.next;
}

static void AddStringToDraw(int x, int y, StringID string, uint64 params_1, uint64 params_2, Colours colour, uint16 width)
{
	assert(width != 0);
	StringSpriteToDraw &ss = _vd.string_sprites_to_draw.emplace_back();
	ss.string = string;
	ss.x = x;
	ss.y = y;
	ss.params[0] = params_1;
	ss.params[1] = params_2;
	ss.width = width;
	ss.colour = colour;
}


/**
 * Draws sprites between ground sprite and everything above.
 *
 * The sprite is either drawn as TileSprite or as ChildSprite of the active foundation.
 *
 * @param image the image to draw.
 * @param pal the provided palette.
 * @param ti TileInfo Tile that is being drawn
 * @param z_offset Z offset relative to the groundsprite. Only used for the sprite position, not for sprite sorting.
 * @param foundation_part Foundation part the sprite belongs to.
 */
static void DrawSelectionSprite(SpriteID image, PaletteID pal, const TileInfo *ti, int z_offset, FoundationPart foundation_part)
{
	/* FIXME: This is not totally valid for some autorail highlights that extend over the edges of the tile. */
	if (_vd.foundation[foundation_part] == -1) {
		/* draw on real ground */
		AddTileSpriteToDraw(image, pal, ti->x, ti->y, ti->z + z_offset);
	} else {
		/* draw on top of foundation */
		AddChildSpriteToFoundation(image, pal, nullptr, foundation_part, 0, -z_offset * ZOOM_LVL_BASE);
	}
}

/**
 * Draws a selection rectangle on a tile.
 *
 * @param ti TileInfo Tile that is being drawn
 * @param pal Palette to apply.
 */
static void DrawTileSelectionRect(const TileInfo *ti, PaletteID pal)
{
	if (!IsValidTile(ti->tile)) return;

	SpriteID sel;
	if (IsHalftileSlope(ti->tileh)) {
		Corner halftile_corner = GetHalftileSlopeCorner(ti->tileh);
		SpriteID sel2 = SPR_HALFTILE_SELECTION_FLAT + halftile_corner;
		DrawSelectionSprite(sel2, pal, ti, 7 + TILE_HEIGHT, FOUNDATION_PART_HALFTILE);

		Corner opposite_corner = OppositeCorner(halftile_corner);
		if (IsSteepSlope(ti->tileh)) {
			sel = SPR_HALFTILE_SELECTION_DOWN;
		} else {
			sel = ((ti->tileh & SlopeWithOneCornerRaised(opposite_corner)) != 0 ? SPR_HALFTILE_SELECTION_UP : SPR_HALFTILE_SELECTION_FLAT);
		}
		sel += opposite_corner;
	} else {
		sel = SPR_SELECT_TILE + SlopeToSpriteOffset(ti->tileh);
	}
	DrawSelectionSprite(sel, pal, ti, 7, FOUNDATION_PART_NORMAL);
}

static bool IsPartOfAutoLine(int px, int py)
{
	px -= _thd.selstart.x;
	py -= _thd.selstart.y;

	if ((_thd.drawstyle & HT_DRAG_MASK) != HT_LINE) return false;

	switch (_thd.drawstyle & HT_DIR_MASK) {
		case HT_DIR_X:  return py == 0; // x direction
		case HT_DIR_Y:  return px == 0; // y direction
		case HT_DIR_HU: return px == -py || px == -py - 16; // horizontal upper
		case HT_DIR_HL: return px == -py || px == -py + 16; // horizontal lower
		case HT_DIR_VL: return px == py || px == py + 16; // vertical left
		case HT_DIR_VR: return px == py || px == py - 16; // vertical right
		default:
			NOT_REACHED();
	}
}

/* [direction][side] */
static const HighLightStyle _autorail_type[6][2] = {
	{ HT_DIR_X,  HT_DIR_X },
	{ HT_DIR_Y,  HT_DIR_Y },
	{ HT_DIR_HU, HT_DIR_HL },
	{ HT_DIR_HL, HT_DIR_HU },
	{ HT_DIR_VL, HT_DIR_VR },
	{ HT_DIR_VR, HT_DIR_VL }
};

#include "table/autorail.h"

/**
 * Draws autorail highlights.
 *
 * @param *ti TileInfo Tile that is being drawn
 * @param autorail_type Offset into _AutorailTilehSprite[][]
 */
static void DrawAutorailSelection(const TileInfo *ti, uint autorail_type)
{
	SpriteID image;
	PaletteID pal;
	int offset;

	FoundationPart foundation_part = FOUNDATION_PART_NORMAL;
	Slope autorail_tileh = RemoveHalftileSlope(ti->tileh);
	if (IsHalftileSlope(ti->tileh)) {
		static const uint _lower_rail[4] = { 5U, 2U, 4U, 3U };
		Corner halftile_corner = GetHalftileSlopeCorner(ti->tileh);
		if (autorail_type != _lower_rail[halftile_corner]) {
			foundation_part = FOUNDATION_PART_HALFTILE;
			/* Here we draw the highlights of the "three-corners-raised"-slope. That looks ok to me. */
			autorail_tileh = SlopeWithThreeCornersRaised(OppositeCorner(halftile_corner));
		}
	}

	offset = _AutorailTilehSprite[autorail_tileh][autorail_type];
	if (offset >= 0) {
		image = SPR_AUTORAIL_BASE + offset;
		pal = PAL_NONE;
	} else {
		image = SPR_AUTORAIL_BASE - offset;
		pal = PALETTE_SEL_TILE_RED;
	}

	DrawSelectionSprite(image, _thd.make_square_red ? PALETTE_SEL_TILE_RED : pal, ti, 7, foundation_part);
}

enum TileHighlightType {
	THT_NONE,
	THT_WHITE,
	THT_BLUE,
	THT_RED,
};

const Station *_viewport_highlight_station; ///< Currently selected station for coverage area highlight
const Town *_viewport_highlight_town;       ///< Currently selected town for coverage area highlight
DepotID _viewport_highlight_depot = INVALID_DEPOT;    ///< Currently selected depot for depot highlight

/**
 * Get tile highlight type of coverage area for a given tile.
 * @param t Tile that is being drawn
 * @return Tile highlight type to draw
 */
static TileHighlightType GetTileHighlightType(TileIndex t)
{
	if (_viewport_highlight_depot != INVALID_DEPOT) {
		if (IsDepotTile(t) && GetDepotIndex(t) == _viewport_highlight_depot) return THT_WHITE;
	}

	if (_viewport_highlight_station != nullptr) {
		if (IsTileType(t, MP_STATION) && GetStationIndex(t) == _viewport_highlight_station->index) return THT_WHITE;
		if (_viewport_highlight_station->TileIsInCatchment(t)) return THT_BLUE;
	}

	if (_viewport_highlight_town != nullptr) {
		if (IsTileType(t, MP_HOUSE)) {
			if (GetTownIndex(t) == _viewport_highlight_town->index) {
				TileHighlightType type = THT_RED;
				for (const Station *st : _viewport_highlight_town->stations_near) {
					if (st->owner != _current_company) continue;
					if (st->TileIsInCatchment(t)) return THT_BLUE;
				}
				return type;
			}
		} else if (IsTileType(t, MP_STATION)) {
			for (const Station *st : _viewport_highlight_town->stations_near) {
				if (st->owner != _current_company) continue;
				if (GetStationIndex(t) == st->index) return THT_WHITE;
			}
		}
	}

	return THT_NONE;
}

/**
 * Draw tile highlight for coverage area highlight.
 * @param *ti TileInfo Tile that is being drawn
 * @param tht Highlight type to draw.
 */
static void DrawTileHighlightType(const TileInfo *ti, TileHighlightType tht)
{
	switch (tht) {
		default:
		case THT_NONE: break;
		case THT_WHITE: DrawTileSelectionRect(ti, PAL_NONE); break;
		case THT_BLUE:  DrawTileSelectionRect(ti, PALETTE_SEL_TILE_BLUE); break;
		case THT_RED:   DrawTileSelectionRect(ti, PALETTE_SEL_TILE_RED); break;
	}
}

/**
 * Highlights tiles insede local authority of selected towns.
 * @param *ti TileInfo Tile that is being drawn
 */
static void HighlightTownLocalAuthorityTiles(const TileInfo *ti)
{
	/* Going through cases in order of computational time. */

	if (_town_local_authority_kdtree.Count() == 0) return;

	/* Tile belongs to town regardless of distance from town. */
	if (GetTileType(ti->tile) == MP_HOUSE) {
		if (!Town::GetByTile(ti->tile)->show_zone) return;

		DrawTileSelectionRect(ti, PALETTE_CRASH);
		return;
	}

	/* If the closest town in the highlighted list is far, we can stop searching. */
	TownID tid = _town_local_authority_kdtree.FindNearest(TileX(ti->tile), TileY(ti->tile));
	Town *closest_highlighted_town = Town::Get(tid);

	if (DistanceManhattan(ti->tile, closest_highlighted_town->xy) >= _settings_game.economy.dist_local_authority) return;

	/* Tile is inside of the local autrhority distance of a highlighted town,
	   but it is possible that a non-highlighted town is even closer. */
	Town *closest_town = ClosestTownFromTile(ti->tile, _settings_game.economy.dist_local_authority);

	if (closest_town->show_zone) {
		DrawTileSelectionRect(ti, PALETTE_CRASH);
	}

}

/**
 * Checks if the specified tile is selected and if so draws selection using correct selectionstyle.
 * @param *ti TileInfo Tile that is being drawn
 */
static void DrawTileSelection(const TileInfo *ti)
{
	/* Highlight tiles insede local authority of selected towns. */
	HighlightTownLocalAuthorityTiles(ti);

	/* Draw a red error square? */
	bool is_redsq = _thd.redsq == ti->tile;
	if (is_redsq) DrawTileSelectionRect(ti, PALETTE_TILE_RED_PULSATING);

	TileHighlightType tht = GetTileHighlightType(ti->tile);
	DrawTileHighlightType(ti, tht);

	/* No tile selection active? */
	if ((_thd.drawstyle & HT_DRAG_MASK) == HT_NONE) return;

	if (_thd.diagonal) { // We're drawing a 45 degrees rotated (diagonal) rectangle
		if (IsInsideRotatedRectangle((int)ti->x, (int)ti->y)) goto draw_inner;
		return;
	}

	/* Inside the inner area? */
	if (IsInsideBS(ti->x, _thd.pos.x, _thd.size.x) &&
			IsInsideBS(ti->y, _thd.pos.y, _thd.size.y)) {
draw_inner:
		if (_thd.drawstyle & HT_RECT) {
			if (!is_redsq) DrawTileSelectionRect(ti, _thd.make_square_red ? PALETTE_SEL_TILE_RED : PAL_NONE);
		} else if (_thd.drawstyle & HT_POINT) {
			/* Figure out the Z coordinate for the single dot. */
			int z = 0;
			FoundationPart foundation_part = FOUNDATION_PART_NORMAL;
			if (ti->tileh & SLOPE_N) {
				z += TILE_HEIGHT;
				if (RemoveHalftileSlope(ti->tileh) == SLOPE_STEEP_N) z += TILE_HEIGHT;
			}
			if (IsHalftileSlope(ti->tileh)) {
				Corner halftile_corner = GetHalftileSlopeCorner(ti->tileh);
				if ((halftile_corner == CORNER_W) || (halftile_corner == CORNER_E)) z += TILE_HEIGHT;
				if (halftile_corner != CORNER_S) {
					foundation_part = FOUNDATION_PART_HALFTILE;
					if (IsSteepSlope(ti->tileh)) z -= TILE_HEIGHT;
				}
			}
			DrawSelectionSprite(_cur_dpi->zoom <= ZOOM_LVL_DETAIL ? SPR_DOT : SPR_DOT_SMALL, PAL_NONE, ti, z, foundation_part);
		} else if (_thd.drawstyle & HT_RAIL) {
			/* autorail highlight piece under cursor */
			HighLightStyle type = _thd.drawstyle & HT_DIR_MASK;
			assert(type < HT_DIR_END);
			DrawAutorailSelection(ti, _autorail_type[type][0]);
		} else if (IsPartOfAutoLine(ti->x, ti->y)) {
			/* autorail highlighting long line */
			HighLightStyle dir = _thd.drawstyle & HT_DIR_MASK;
			uint side;

			if (dir == HT_DIR_X || dir == HT_DIR_Y) {
				side = 0;
			} else {
				TileIndex start = TileVirtXY(_thd.selstart.x, _thd.selstart.y);
				side = Delta(Delta(TileX(start), TileX(ti->tile)), Delta(TileY(start), TileY(ti->tile)));
			}

			DrawAutorailSelection(ti, _autorail_type[dir][side]);
		}
		return;
	}

	/* Check if it's inside the outer area? */
	if (!is_redsq && (tht == THT_NONE || tht == THT_RED) && _thd.outersize.x > 0 &&
			IsInsideBS(ti->x, _thd.pos.x + _thd.offs.x, _thd.size.x + _thd.outersize.x) &&
			IsInsideBS(ti->y, _thd.pos.y + _thd.offs.y, _thd.size.y + _thd.outersize.y)) {
		/* Draw a blue rect. */
		DrawTileSelectionRect(ti, PALETTE_SEL_TILE_BLUE);
		return;
	}
}

/**
 * Returns the y coordinate in the viewport coordinate system where the given
 * tile is painted.
 * @param tile Any tile.
 * @return The viewport y coordinate where the tile is painted.
 */
static int GetViewportY(Point tile)
{
	/* Each increment in X or Y direction moves down by half a tile, i.e. TILE_PIXELS / 2. */
	return (tile.y * (int)(TILE_PIXELS / 2) + tile.x * (int)(TILE_PIXELS / 2) - TilePixelHeightOutsideMap(tile.x, tile.y)) << ZOOM_LVL_SHIFT;
}

/**
 * Add the landscape to the viewport, i.e. all ground tiles and buildings.
 */
static void ViewportAddLandscape()
{
	assert(_vd.dpi.top <= _vd.dpi.top + _vd.dpi.height);
	assert(_vd.dpi.left <= _vd.dpi.left + _vd.dpi.width);

	Point upper_left = InverseRemapCoords(_vd.dpi.left, _vd.dpi.top);
	Point upper_right = InverseRemapCoords(_vd.dpi.left + _vd.dpi.width, _vd.dpi.top);

	/* Transformations between tile coordinates and viewport rows/columns: See vp_column_row
	 *   column = y - x
	 *   row    = x + y
	 *   x      = (row - column) / 2
	 *   y      = (row + column) / 2
	 * Note: (row, columns) pairs are only valid, if they are both even or both odd.
	 */

	/* Columns overlap with neighbouring columns by a half tile.
	 *  - Left column is column of upper_left (rounded down) and one column to the left.
	 *  - Right column is column of upper_right (rounded up) and one column to the right.
	 * Note: Integer-division does not round down for negative numbers, so ensure rounding with another increment/decrement.
	 */
	int left_column = (upper_left.y - upper_left.x) / (int)TILE_SIZE - 2;
	int right_column = (upper_right.y - upper_right.x) / (int)TILE_SIZE + 2;

	int potential_bridge_height = ZOOM_LVL_BASE * TILE_HEIGHT * _settings_game.construction.max_bridge_height;

	/* Rows overlap with neighbouring rows by a half tile.
	 * The first row that could possibly be visible is the row above upper_left (if it is at height 0).
	 * Due to integer-division not rounding down for negative numbers, we need another decrement.
	 */
	int row = (upper_left.x + upper_left.y) / (int)TILE_SIZE - 2;
	bool last_row = false;
	for (; !last_row; row++) {
		last_row = true;
		for (int column = left_column; column <= right_column; column++) {
			/* Valid row/column? */
			if ((row + column) % 2 != 0) continue;

			Point tilecoord;
			tilecoord.x = (row - column) / 2;
			tilecoord.y = (row + column) / 2;
			assert(column == tilecoord.y - tilecoord.x);
			assert(row == tilecoord.y + tilecoord.x);

			TileType tile_type;
			TileInfo tile_info;
			_cur_ti = &tile_info;
			tile_info.x = tilecoord.x * TILE_SIZE; // FIXME tile_info should use signed integers
			tile_info.y = tilecoord.y * TILE_SIZE;

			if (IsInsideBS(tilecoord.x, 0, MapSizeX()) && IsInsideBS(tilecoord.y, 0, MapSizeY())) {
				/* This includes the south border at MapMaxX / MapMaxY. When terraforming we still draw tile selections there. */
				tile_info.tile = TileXY(tilecoord.x, tilecoord.y);
				tile_type = GetTileType(tile_info.tile);
			} else {
				tile_info.tile = INVALID_TILE;
				tile_type = MP_VOID;
			}

			if (tile_type != MP_VOID) {
				/* We are inside the map => paint landscape. */
				tile_info.tileh = GetTilePixelSlope(tile_info.tile, &tile_info.z);
			} else {
				/* We are outside the map => paint black. */
				tile_info.tileh = GetTilePixelSlopeOutsideMap(tilecoord.x, tilecoord.y, &tile_info.z);
			}

			int viewport_y = GetViewportY(tilecoord);

			if (viewport_y + MAX_TILE_EXTENT_BOTTOM < _vd.dpi.top) {
				/* The tile in this column is not visible yet.
				 * Tiles in other columns may be visible, but we need more rows in any case. */
				last_row = false;
				continue;
			}

			int min_visible_height = viewport_y - (_vd.dpi.top + _vd.dpi.height);
			bool tile_visible = min_visible_height <= 0;

			if (tile_type != MP_VOID) {
				/* Is tile with buildings visible? */
				if (min_visible_height < MAX_TILE_EXTENT_TOP) tile_visible = true;

				if (IsBridgeAbove(tile_info.tile)) {
					/* Is the bridge visible? */
					TileIndex bridge_tile = GetNorthernBridgeEnd(tile_info.tile);
					int bridge_height = ZOOM_LVL_BASE * (GetBridgePixelHeight(bridge_tile) - TilePixelHeight(tile_info.tile));
					if (min_visible_height < bridge_height + MAX_TILE_EXTENT_TOP) tile_visible = true;
				}

				/* Would a higher bridge on a more southern tile be visible?
				 * If yes, we need to loop over more rows to possibly find one. */
				if (min_visible_height < potential_bridge_height + MAX_TILE_EXTENT_TOP) last_row = false;
			} else {
				/* Outside of map. If we are on the north border of the map, there may still be a bridge visible,
				 * so we need to loop over more rows to possibly find one. */
				if ((tilecoord.x <= 0 || tilecoord.y <= 0) && min_visible_height < potential_bridge_height + MAX_TILE_EXTENT_TOP) last_row = false;
			}

			if (tile_visible) {
				last_row = false;
				_vd.foundation_part = FOUNDATION_PART_NONE;
				_vd.foundation[0] = -1;
				_vd.foundation[1] = -1;
				_vd.last_foundation_child[0] = nullptr;
				_vd.last_foundation_child[1] = nullptr;

				_tile_type_procs[tile_type]->draw_tile_proc(&tile_info);
				if (tile_info.tile != INVALID_TILE) DrawTileSelection(&tile_info);
			}
		}
	}
}

/**
 * Add a string to draw in the viewport
 * @param dpi current viewport area
 * @param small_from Zoomlevel from when the small font should be used
 * @param sign sign position and dimension
 * @param string_normal String for normal and 2x zoom level
 * @param string_small String for 4x and 8x zoom level
 * @param string_small_shadow Shadow string for 4x and 8x zoom level; or #STR_NULL if no shadow
 * @param colour colour of the sign background; or INVALID_COLOUR if transparent
 */
void ViewportAddString(const DrawPixelInfo *dpi, ZoomLevel small_from, const ViewportSign *sign, StringID string_normal, StringID string_small, StringID string_small_shadow, uint64 params_1, uint64 params_2, Colours colour)
{
	bool small = dpi->zoom >= small_from;

	int left   = dpi->left;
	int top    = dpi->top;
	int right  = left + dpi->width;
	int bottom = top + dpi->height;

	int sign_height     = ScaleByZoom(VPSM_TOP + FONT_HEIGHT_NORMAL + VPSM_BOTTOM, dpi->zoom);
	int sign_half_width = ScaleByZoom((small ? sign->width_small : sign->width_normal) / 2, dpi->zoom);

	if (bottom < sign->top ||
			top   > sign->top + sign_height ||
			right < sign->center - sign_half_width ||
			left  > sign->center + sign_half_width) {
		return;
	}

	if (!small) {
		AddStringToDraw(sign->center - sign_half_width, sign->top, string_normal, params_1, params_2, colour, sign->width_normal);
	} else {
		int shadow_offset = 0;
		if (string_small_shadow != STR_NULL) {
			shadow_offset = 4;
			AddStringToDraw(sign->center - sign_half_width + shadow_offset, sign->top, string_small_shadow, params_1, params_2, INVALID_COLOUR, sign->width_small);
		}
		AddStringToDraw(sign->center - sign_half_width, sign->top - shadow_offset, string_small, params_1, params_2,
				colour, sign->width_small | 0x8000);
	}
}

static Rect ExpandRectWithViewportSignMargins(Rect r, ZoomLevel zoom)
{
	/* Pessimistically always use normal font, but also assume small font is never larger in either dimension */
	const int fh = FONT_HEIGHT_NORMAL;
	const int max_tw = _viewport_sign_maxwidth / 2 + 1;
	const int expand_y = ScaleByZoom(VPSM_TOP + fh + VPSM_BOTTOM, zoom);
	const int expand_x = ScaleByZoom(VPSM_LEFT + max_tw + VPSM_RIGHT, zoom);

	r.left -= expand_x;
	r.right += expand_x;
	r.top -= expand_y;
	r.bottom += expand_y;

	return r;
}

static void ViewportAddKdtreeSigns(DrawPixelInfo *dpi)
{
	Rect search_rect{ dpi->left, dpi->top, dpi->left + dpi->width, dpi->top + dpi->height };
	search_rect = ExpandRectWithViewportSignMargins(search_rect, dpi->zoom);

	bool show_stations = HasBit(_display_opt, DO_SHOW_STATION_NAMES) && _game_mode != GM_MENU;
	bool show_waypoints = HasBit(_display_opt, DO_SHOW_WAYPOINT_NAMES) && _game_mode != GM_MENU;
	bool show_towns = HasBit(_display_opt, DO_SHOW_TOWN_NAMES) && _game_mode != GM_MENU;
	bool show_signs = HasBit(_display_opt, DO_SHOW_SIGNS) && !IsInvisibilitySet(TO_SIGNS);
	bool show_competitors = HasBit(_display_opt, DO_SHOW_COMPETITOR_SIGNS);

	const BaseStation *st;
	const Sign *si;

	/* Collect all the items first and draw afterwards, to ensure layering */
	std::vector<const BaseStation *> stations;
	std::vector<const Town *> towns;
	std::vector<const Sign *> signs;

	_viewport_sign_kdtree.FindContained(search_rect.left, search_rect.top, search_rect.right, search_rect.bottom, [&](const ViewportSignKdtreeItem & item) {
		switch (item.type) {
			case ViewportSignKdtreeItem::VKI_STATION:
				if (!show_stations) break;
				st = BaseStation::Get(item.id.station);

				/* Don't draw if station is owned by another company and competitor station names are hidden. Stations owned by none are never ignored. */
				if (!show_competitors && _local_company != st->owner && st->owner != OWNER_NONE) break;

				stations.push_back(st);
				break;

			case ViewportSignKdtreeItem::VKI_WAYPOINT:
				if (!show_waypoints) break;
				st = BaseStation::Get(item.id.station);

				/* Don't draw if station is owned by another company and competitor station names are hidden. Stations owned by none are never ignored. */
				if (!show_competitors && _local_company != st->owner && st->owner != OWNER_NONE) break;

				stations.push_back(st);
				break;

			case ViewportSignKdtreeItem::VKI_TOWN:
				if (!show_towns) break;
				towns.push_back(Town::Get(item.id.town));
				break;

			case ViewportSignKdtreeItem::VKI_SIGN:
				if (!show_signs) break;
				si = Sign::Get(item.id.sign);

				/* Don't draw if sign is owned by another company and competitor signs should be hidden.
				* Note: It is intentional that also signs owned by OWNER_NONE are hidden. Bankrupt
				* companies can leave OWNER_NONE signs after them. */
				if (!show_competitors && _local_company != si->owner && si->owner != OWNER_DEITY) break;

				signs.push_back(si);
				break;

			default:
				NOT_REACHED();
		}
	});

	/* Layering order (bottom to top): Town names, signs, stations */

	for (const auto *t : towns) {
		ViewportAddString(dpi, ZOOM_LVL_OUT_16X, &t->cache.sign,
			_settings_client.gui.population_in_label ? STR_VIEWPORT_TOWN_POP : STR_VIEWPORT_TOWN,
			STR_VIEWPORT_TOWN_TINY_WHITE, STR_VIEWPORT_TOWN_TINY_BLACK,
			t->index, t->cache.population);
	}

	for (const auto *si : signs) {
		ViewportAddString(dpi, ZOOM_LVL_OUT_16X, &si->sign,
			STR_WHITE_SIGN,
			(IsTransparencySet(TO_SIGNS) || si->owner == OWNER_DEITY) ? STR_VIEWPORT_SIGN_SMALL_WHITE : STR_VIEWPORT_SIGN_SMALL_BLACK, STR_NULL,
			si->index, 0, (si->owner == OWNER_NONE) ? COLOUR_GREY : (si->owner == OWNER_DEITY ? INVALID_COLOUR : _company_colours[si->owner]));
	}

	for (const auto *st : stations) {
		if (Station::IsExpected(st)) {
			/* Station */
			ViewportAddString(dpi, ZOOM_LVL_OUT_16X, &st->sign,
				STR_VIEWPORT_STATION, STR_VIEWPORT_STATION + 1, STR_NULL,
				st->index, st->facilities, (st->owner == OWNER_NONE || !st->IsInUse()) ? COLOUR_GREY : _company_colours[st->owner]);
		} else {
			/* Waypoint */
			ViewportAddString(dpi, ZOOM_LVL_OUT_16X, &st->sign,
				STR_VIEWPORT_WAYPOINT, STR_VIEWPORT_WAYPOINT + 1, STR_NULL,
				st->index, st->facilities, (st->owner == OWNER_NONE || !st->IsInUse()) ? COLOUR_GREY : _company_colours[st->owner]);
		}
	}
}


/**
 * Update the position of the viewport sign.
 * @param center the (preferred) center of the viewport sign
 * @param top    the new top of the sign
 * @param str    the string to show in the sign
 * @param str_small the string to show when zoomed out. STR_NULL means same as \a str
 */
void ViewportSign::UpdatePosition(int center, int top, StringID str, StringID str_small)
{
	if (this->width_normal != 0) this->MarkDirty();

	this->top = top;

	char buffer[DRAW_STRING_BUFFER];

	GetString(buffer, str, lastof(buffer));
	this->width_normal = VPSM_LEFT + Align(GetStringBoundingBox(buffer).width, 2) + VPSM_RIGHT;
	this->center = center;

	/* zoomed out version */
	if (str_small != STR_NULL) {
		GetString(buffer, str_small, lastof(buffer));
	}
	this->width_small = VPSM_LEFT + Align(GetStringBoundingBox(buffer, FS_SMALL).width, 2) + VPSM_RIGHT;

	this->MarkDirty();
}

/**
 * Mark the sign dirty in all viewports.
 * @param maxzoom Maximum %ZoomLevel at which the text is visible.
 *
 * @ingroup dirty
 */
void ViewportSign::MarkDirty(ZoomLevel maxzoom) const
{
	Rect zoomlevels[ZOOM_LVL_COUNT];

	for (ZoomLevel zoom = ZOOM_LVL_BEGIN; zoom != ZOOM_LVL_END; zoom++) {
		/* FIXME: This doesn't switch to width_small when appropriate. */
		zoomlevels[zoom].left   = this->center - ScaleByZoom(this->width_normal / 2 + 1, zoom);
		zoomlevels[zoom].top    = this->top    - ScaleByZoom(1, zoom);
		zoomlevels[zoom].right  = this->center + ScaleByZoom(this->width_normal / 2 + 1, zoom);
		zoomlevels[zoom].bottom = this->top    + ScaleByZoom(VPSM_TOP + FONT_HEIGHT_NORMAL + VPSM_BOTTOM + 1, zoom);
	}

	for (const Window *w : Window::Iterate()) {
		Viewport *vp = w->viewport;
		if (vp != nullptr && vp->zoom <= maxzoom) {
			assert(vp->width != 0);
			Rect &zl = zoomlevels[vp->zoom];
			MarkViewportDirty(vp, zl.left, zl.top, zl.right, zl.bottom);
		}
	}
}

static void ViewportDrawTileSprites(const TileSpriteToDrawVector *tstdv)
{
	for (const TileSpriteToDraw &ts : *tstdv) {
		DrawSpriteViewport(ts.image, ts.pal, ts.x, ts.y, ts.sub);
	}
}

/** This fallback sprite checker always exists. */
static bool ViewportSortParentSpritesChecker()
{
	return true;
}

/** Sort parent sprites pointer array replicating the way original sorter did it. */
static void ViewportSortParentSprites(ParentSpriteToSortVector *psdv)
{
	if (psdv->size() < 2) return;

	/* We rely on sprites being, for the most part, already ordered.
	 * So we don't need to move many of them and can keep track of their
	 * order efficiently by using stack. We always move sprites to the front
	 * of the current position, i.e. to the top of the stack.
	 * Also use special constants to indicate sorting state without
	 * adding extra fields to ParentSpriteToDraw structure.
	 */
	const uint32 ORDER_COMPARED = UINT32_MAX; // Sprite was compared but we still need to compare the ones preceding it
	const uint32 ORDER_RETURNED = UINT32_MAX - 1; // Makr sorted sprite in case there are other occurrences of it in the stack
	std::stack<ParentSpriteToDraw *> sprite_order;
	uint32 next_order = 0;

	std::forward_list<std::pair<int64, ParentSpriteToDraw *>> sprite_list;  // We store sprites in a list sorted by xmin+ymin

	/* Initialize sprite list and order. */
	for (auto p = psdv->rbegin(); p != psdv->rend(); p++) {
		sprite_list.push_front(std::make_pair((*p)->xmin + (*p)->ymin, *p));
		sprite_order.push(*p);
		(*p)->order = next_order++;
	}

	sprite_list.sort();

	std::vector<ParentSpriteToDraw *> preceding;  // Temporarily stores sprites that precede current and their position in the list
	auto preceding_prev = sprite_list.begin(); // Store iterator in case we need to delete a single preciding sprite
	auto out = psdv->begin();  // Iterator to output sorted sprites

	while (!sprite_order.empty()) {

		auto s = sprite_order.top();
		sprite_order.pop();

		/* Sprite is already sorted, ignore it. */
		if (s->order == ORDER_RETURNED) continue;

		/* Sprite was already compared, just need to output it. */
		if (s->order == ORDER_COMPARED) {
			*(out++) = s;
			s->order = ORDER_RETURNED;
			continue;
		}

		preceding.clear();

		/* We only need sprites with xmin <= s->xmax && ymin <= s->ymax && zmin <= s->zmax
		 * So by iterating sprites with xmin + ymin <= s->xmax + s->ymax
		 * we get all we need and some more that we filter out later.
		 * We don't include zmin into the sum as there are usually more neighbors on x and y than z
		 * so including it will actually increase the amount of false positives.
		 * Also min coordinates can be > max so using max(xmin, xmax) + max(ymin, ymax)
		 * to ensure that we iterate the current sprite as we need to remove it from the list.
		 */
		auto ssum = std::max(s->xmax, s->xmin) + std::max(s->ymax, s->ymin);
		auto prev = sprite_list.before_begin();
		auto x = sprite_list.begin();
		while (x != sprite_list.end() && ((*x).first <= ssum)) {
			auto p = (*x).second;
			if (p == s) {
				/* We found the current sprite, remove it and move on. */
				x = sprite_list.erase_after(prev);
				continue;
			}

			auto p_prev = prev;
			prev = x++;

			if (s->xmax < p->xmin || s->ymax < p->ymin || s->zmax < p->zmin) continue;
			if (s->xmin <= p->xmax && // overlap in X?
					s->ymin <= p->ymax && // overlap in Y?
					s->zmin <= p->zmax) { // overlap in Z?
				if (s->xmin + s->xmax + s->ymin + s->ymax + s->zmin + s->zmax <=
						p->xmin + p->xmax + p->ymin + p->ymax + p->zmin + p->zmax) {
					continue;
				}
			}
			preceding.push_back(p);
			preceding_prev = p_prev;
		}

		if (preceding.empty()) {
			/* No preceding sprites, add current one to the output */
			*(out++) = s;
			s->order = ORDER_RETURNED;
			continue;
		}

		/* Optimization for the case when we only have 1 sprite to move. */
		if (preceding.size() == 1) {
			auto p = preceding[0];
			/* We can only output the preceding sprite if there can't be any other sprites preceding it. */
			if (p->xmax <= s->xmax && p->ymax <= s->ymax && p->zmax <= s->zmax) {
				p->order = ORDER_RETURNED;
				s->order = ORDER_RETURNED;
				sprite_list.erase_after(preceding_prev);
				*(out++) = p;
				*(out++) = s;
				continue;
			}
		}

		/* Sort all preceding sprites by order and assign new orders in reverse (as original sorter did). */
		std::sort(preceding.begin(), preceding.end(), [](const ParentSpriteToDraw *a, const ParentSpriteToDraw *b) {
			return a->order > b->order;
		});

		s->order = ORDER_COMPARED;
		sprite_order.push(s);  // Still need to output so push it back for now

		for (auto p: preceding) {
			p->order = next_order++;
			sprite_order.push(p);
		}
	}
}


static void ViewportDrawParentSprites(const ParentSpriteToSortVector *psd, const ChildScreenSpriteToDrawVector *csstdv)
{
	for (const ParentSpriteToDraw *ps : *psd) {
		if (ps->image != SPR_EMPTY_BOUNDING_BOX) DrawSpriteViewport(ps->image, ps->pal, ps->x, ps->y, ps->sub);

		int child_idx = ps->first_child;
		while (child_idx >= 0) {
			const ChildScreenSpriteToDraw *cs = csstdv->data() + child_idx;
			child_idx = cs->next;
			DrawSpriteViewport(cs->image, cs->pal, ps->left + cs->x, ps->top + cs->y, cs->sub);
		}
	}
}

/**
 * Draws the bounding boxes of all ParentSprites
 * @param psd Array of ParentSprites
 */
static void ViewportDrawBoundingBoxes(const ParentSpriteToSortVector *psd)
{
	for (const ParentSpriteToDraw *ps : *psd) {
		Point pt1 = RemapCoords(ps->xmax + 1, ps->ymax + 1, ps->zmax + 1); // top front corner
		Point pt2 = RemapCoords(ps->xmin    , ps->ymax + 1, ps->zmax + 1); // top left corner
		Point pt3 = RemapCoords(ps->xmax + 1, ps->ymin    , ps->zmax + 1); // top right corner
		Point pt4 = RemapCoords(ps->xmax + 1, ps->ymax + 1, ps->zmin    ); // bottom front corner

		DrawBox(        pt1.x,         pt1.y,
		        pt2.x - pt1.x, pt2.y - pt1.y,
		        pt3.x - pt1.x, pt3.y - pt1.y,
		        pt4.x - pt1.x, pt4.y - pt1.y);
	}
}

/**
 * Draw/colour the blocks that have been redrawn.
 */
static void ViewportDrawDirtyBlocks()
{
	Blitter *blitter = BlitterFactory::GetCurrentBlitter();
	const DrawPixelInfo *dpi = _cur_dpi;
	void *dst;
	int right =  UnScaleByZoom(dpi->width,  dpi->zoom);
	int bottom = UnScaleByZoom(dpi->height, dpi->zoom);

	int colour = _string_colourmap[_dirty_block_colour & 0xF];

	dst = dpi->dst_ptr;

	byte bo = UnScaleByZoom(dpi->left + dpi->top, dpi->zoom) & 1;
	do {
		for (int i = (bo ^= 1); i < right; i += 2) blitter->SetPixel(dst, i, 0, (uint8)colour);
		dst = blitter->MoveTo(dst, 0, 1);
	} while (--bottom > 0);
}

static void ViewportDrawStrings(ZoomLevel zoom, const StringSpriteToDrawVector *sstdv)
{
	for (const StringSpriteToDraw &ss : *sstdv) {
		TextColour colour = TC_BLACK;
		bool small = HasBit(ss.width, 15);
		int w = GB(ss.width, 0, 15);
		int x = UnScaleByZoom(ss.x, zoom);
		int y = UnScaleByZoom(ss.y, zoom);
		int h = VPSM_TOP + (small ? FONT_HEIGHT_SMALL : FONT_HEIGHT_NORMAL) + VPSM_BOTTOM;

		SetDParam(0, ss.params[0]);
		SetDParam(1, ss.params[1]);

		if (ss.colour != INVALID_COLOUR) {
			/* Do not draw signs nor station names if they are set invisible */
			if (IsInvisibilitySet(TO_SIGNS) && ss.string != STR_WHITE_SIGN) continue;

			if (IsTransparencySet(TO_SIGNS) && ss.string != STR_WHITE_SIGN) {
				/* Don't draw the rectangle.
				 * Real colours need the TC_IS_PALETTE_COLOUR flag.
				 * Otherwise colours from _string_colourmap are assumed. */
				colour = (TextColour)_colour_gradient[ss.colour][6] | TC_IS_PALETTE_COLOUR;
			} else {
				/* Draw the rectangle if 'transparent station signs' is off,
				 * or if we are drawing a general text sign (STR_WHITE_SIGN). */
				DrawFrameRect(
					x, y, x + w, y + h, ss.colour,
					IsTransparencySet(TO_SIGNS) ? FR_TRANSPARENT : FR_NONE
				);
			}
		}

		DrawString(x + VPSM_LEFT, x + w - 1 - VPSM_RIGHT, y + VPSM_TOP, ss.string, colour, SA_HOR_CENTER);
	}
}

void ViewportDoDraw(const Viewport *vp, int left, int top, int right, int bottom)
{
	DrawPixelInfo *old_dpi = _cur_dpi;
	_cur_dpi = &_vd.dpi;

	_vd.dpi.zoom = vp->zoom;
	int mask = ScaleByZoom(-1, vp->zoom);

	_vd.combine_sprites = SPRITE_COMBINE_NONE;

	_vd.dpi.width = (right - left) & mask;
	_vd.dpi.height = (bottom - top) & mask;
	_vd.dpi.left = left & mask;
	_vd.dpi.top = top & mask;
	_vd.dpi.pitch = old_dpi->pitch;
	_vd.last_child = nullptr;

	int x = UnScaleByZoom(_vd.dpi.left - (vp->virtual_left & mask), vp->zoom) + vp->left;
	int y = UnScaleByZoom(_vd.dpi.top - (vp->virtual_top & mask), vp->zoom) + vp->top;

	_vd.dpi.dst_ptr = BlitterFactory::GetCurrentBlitter()->MoveTo(old_dpi->dst_ptr, x - old_dpi->left, y - old_dpi->top);

	ViewportAddLandscape();
	ViewportAddVehicles(&_vd.dpi);

	ViewportAddKdtreeSigns(&_vd.dpi);

	DrawTextEffects(&_vd.dpi);

	if (_vd.tile_sprites_to_draw.size() != 0) ViewportDrawTileSprites(&_vd.tile_sprites_to_draw);

	for (auto &psd : _vd.parent_sprites_to_draw) {
		_vd.parent_sprites_to_sort.push_back(&psd);
	}

	_vp_sprite_sorter(&_vd.parent_sprites_to_sort);
	ViewportDrawParentSprites(&_vd.parent_sprites_to_sort, &_vd.child_screen_sprites_to_draw);

	if (_draw_bounding_boxes) ViewportDrawBoundingBoxes(&_vd.parent_sprites_to_sort);
	if (_draw_dirty_blocks) ViewportDrawDirtyBlocks();

	DrawPixelInfo dp = _vd.dpi;
	ZoomLevel zoom = _vd.dpi.zoom;
	dp.zoom = ZOOM_LVL_NORMAL;
	dp.width = UnScaleByZoom(dp.width, zoom);
	dp.height = UnScaleByZoom(dp.height, zoom);
	_cur_dpi = &dp;

	if (vp->overlay != nullptr && vp->overlay->GetCargoMask() != 0 && vp->overlay->GetCompanyMask() != 0) {
		/* translate to window coordinates */
		dp.left = x;
		dp.top = y;
		vp->overlay->Draw(&dp);
	}

	if (_vd.string_sprites_to_draw.size() != 0) {
		/* translate to world coordinates */
		dp.left = UnScaleByZoom(_vd.dpi.left, zoom);
		dp.top = UnScaleByZoom(_vd.dpi.top, zoom);
		ViewportDrawStrings(zoom, &_vd.string_sprites_to_draw);
	}

	_cur_dpi = old_dpi;

	_vd.string_sprites_to_draw.clear();
	_vd.tile_sprites_to_draw.clear();
	_vd.parent_sprites_to_draw.clear();
	_vd.parent_sprites_to_sort.clear();
	_vd.child_screen_sprites_to_draw.clear();
}

static inline void ViewportDraw(const Viewport *vp, int left, int top, int right, int bottom)
{
	if (right <= vp->left || bottom <= vp->top) return;

	if (left >= vp->left + vp->width) return;

	if (left < vp->left) left = vp->left;
	if (right > vp->left + vp->width) right = vp->left + vp->width;

	if (top >= vp->top + vp->height) return;

	if (top < vp->top) top = vp->top;
	if (bottom > vp->top + vp->height) bottom = vp->top + vp->height;

	ViewportDoDraw(vp,
		ScaleByZoom(left - vp->left, vp->zoom) + vp->virtual_left,
		ScaleByZoom(top - vp->top, vp->zoom) + vp->virtual_top,
		ScaleByZoom(right - vp->left, vp->zoom) + vp->virtual_left,
		ScaleByZoom(bottom - vp->top, vp->zoom) + vp->virtual_top
	);
}

/**
 * Draw the viewport of this window.
 */
void Window::DrawViewport() const
{
	PerformanceAccumulator framerate(PFE_DRAWWORLD);

	DrawPixelInfo *dpi = _cur_dpi;

	dpi->left += this->left;
	dpi->top += this->top;

	ViewportDraw(this->viewport, dpi->left, dpi->top, dpi->left + dpi->width, dpi->top + dpi->height);

	dpi->left -= this->left;
	dpi->top -= this->top;
}

/**
 * Ensure that a given viewport has a valid scroll position.
 *
 * There must be a visible piece of the map in the center of the viewport.
 * If there isn't, the viewport will be scrolled to nearest such location.
 *
 * @param vp The viewport.
 * @param[in,out] scroll_x Viewport X scroll.
 * @param[in,out] scroll_y Viewport Y scroll.
 */
static inline void ClampViewportToMap(const Viewport *vp, int *scroll_x, int *scroll_y)
{
	/* Centre of the viewport is hot spot. */
	Point pt = {
		*scroll_x + vp->virtual_width / 2,
		*scroll_y + vp->virtual_height / 2
	};

	/* Find nearest tile that is within borders of the map. */
	bool clamped;
	pt = InverseRemapCoords2(pt.x, pt.y, true, &clamped);

	if (clamped) {
		/* Convert back to viewport coordinates and remove centering. */
		pt = RemapCoords2(pt.x, pt.y);
		*scroll_x = pt.x - vp->virtual_width / 2;
		*scroll_y = pt.y - vp->virtual_height / 2;
	}
}

/**
 * Update the viewport position being displayed.
 * @param w %Window owning the viewport.
 */
void UpdateViewportPosition(Window *w)
{
	const Viewport *vp = w->viewport;

	if (w->viewport->follow_vehicle != INVALID_VEHICLE) {
		const Vehicle *veh = Vehicle::Get(w->viewport->follow_vehicle);
		Point pt = MapXYZToViewport(vp, veh->x_pos, veh->y_pos, veh->z_pos);

		w->viewport->scrollpos_x = pt.x;
		w->viewport->scrollpos_y = pt.y;
		SetViewportPosition(w, pt.x, pt.y);
	} else {
		/* Ensure the destination location is within the map */
		ClampViewportToMap(vp, &w->viewport->dest_scrollpos_x, &w->viewport->dest_scrollpos_y);

		int delta_x = w->viewport->dest_scrollpos_x - w->viewport->scrollpos_x;
		int delta_y = w->viewport->dest_scrollpos_y - w->viewport->scrollpos_y;

		bool update_overlay = false;
		if (delta_x != 0 || delta_y != 0) {
			if (_settings_client.gui.smooth_scroll) {
				int max_scroll = ScaleByMapSize1D(512 * ZOOM_LVL_BASE);
				/* Not at our desired position yet... */
				w->viewport->scrollpos_x += Clamp(DivAwayFromZero(delta_x, 4), -max_scroll, max_scroll);
				w->viewport->scrollpos_y += Clamp(DivAwayFromZero(delta_y, 4), -max_scroll, max_scroll);
			} else {
				w->viewport->scrollpos_x = w->viewport->dest_scrollpos_x;
				w->viewport->scrollpos_y = w->viewport->dest_scrollpos_y;
			}
			update_overlay = (w->viewport->scrollpos_x == w->viewport->dest_scrollpos_x &&
								w->viewport->scrollpos_y == w->viewport->dest_scrollpos_y);
		}

		ClampViewportToMap(vp, &w->viewport->scrollpos_x, &w->viewport->scrollpos_y);

		SetViewportPosition(w, w->viewport->scrollpos_x, w->viewport->scrollpos_y);
		if (update_overlay) RebuildViewportOverlay(w);
	}
}

/**
 * Marks a viewport as dirty for repaint if it displays (a part of) the area the needs to be repainted.
 * @param vp     The viewport to mark as dirty
 * @param left   Left edge of area to repaint
 * @param top    Top edge of area to repaint
 * @param right  Right edge of area to repaint
 * @param bottom Bottom edge of area to repaint
 * @return true if the viewport contains a dirty block
 * @ingroup dirty
 */
static bool MarkViewportDirty(const Viewport *vp, int left, int top, int right, int bottom)
{
	/* Rounding wrt. zoom-out level */
	right  += (1 << vp->zoom) - 1;
	bottom += (1 << vp->zoom) - 1;

	right -= vp->virtual_left;
	if (right <= 0) return false;

	bottom -= vp->virtual_top;
	if (bottom <= 0) return false;

	left = std::max(0, left - vp->virtual_left);

	if (left >= vp->virtual_width) return false;

	top = std::max(0, top - vp->virtual_top);

	if (top >= vp->virtual_height) return false;

	AddDirtyBlock(
		UnScaleByZoomLower(left, vp->zoom) + vp->left,
		UnScaleByZoomLower(top, vp->zoom) + vp->top,
		UnScaleByZoom(right, vp->zoom) + vp->left + 1,
		UnScaleByZoom(bottom, vp->zoom) + vp->top + 1
	);

	return true;
}

/**
 * Mark all viewports that display an area as dirty (in need of repaint).
 * @param left   Left   edge of area to repaint. (viewport coordinates, that is wrt. #ZOOM_LVL_NORMAL)
 * @param top    Top    edge of area to repaint. (viewport coordinates, that is wrt. #ZOOM_LVL_NORMAL)
 * @param right  Right  edge of area to repaint. (viewport coordinates, that is wrt. #ZOOM_LVL_NORMAL)
 * @param bottom Bottom edge of area to repaint. (viewport coordinates, that is wrt. #ZOOM_LVL_NORMAL)
 * @return true if at least one viewport has a dirty block
 * @ingroup dirty
 */
bool MarkAllViewportsDirty(int left, int top, int right, int bottom)
{
	bool dirty = false;

	for (const Window *w : Window::Iterate()) {
		Viewport *vp = w->viewport;
		if (vp != nullptr) {
			assert(vp->width != 0);
			if (MarkViewportDirty(vp, left, top, right, bottom)) dirty = true;
		}
	}

	return dirty;
}

void ConstrainAllViewportsZoom()
{
	for (Window *w : Window::Iterate()) {
		if (w->viewport == nullptr) continue;

		ZoomLevel zoom = static_cast<ZoomLevel>(Clamp(w->viewport->zoom, _settings_client.gui.zoom_min, _settings_client.gui.zoom_max));
		if (zoom != w->viewport->zoom) {
			while (w->viewport->zoom < zoom) DoZoomInOutWindow(ZOOM_OUT, w);
			while (w->viewport->zoom > zoom) DoZoomInOutWindow(ZOOM_IN, w);
		}
	}
}

/**
 * Mark a tile given by its index dirty for repaint.
 * @param tile The tile to mark dirty.
 * @param bridge_level_offset Height of bridge on tile to also mark dirty. (Height level relative to north corner.)
 * @param tile_height_override Height of the tile (#TileHeight).
 * @ingroup dirty
 */
void MarkTileDirtyByTile(TileIndex tile, int bridge_level_offset, int tile_height_override)
{
	Point pt = RemapCoords(TileX(tile) * TILE_SIZE, TileY(tile) * TILE_SIZE, tile_height_override * TILE_HEIGHT);
	MarkAllViewportsDirty(
			pt.x - MAX_TILE_EXTENT_LEFT,
			pt.y - MAX_TILE_EXTENT_TOP - ZOOM_LVL_BASE * TILE_HEIGHT * bridge_level_offset,
			pt.x + MAX_TILE_EXTENT_RIGHT,
			pt.y + MAX_TILE_EXTENT_BOTTOM);
}

/**
 * Marks the selected tiles as dirty.
 *
 * This function marks the selected tiles as dirty for repaint
 *
 * @ingroup dirty
 */
static void SetSelectionTilesDirty()
{
	int x_size = _thd.size.x;
	int y_size = _thd.size.y;

	if (!_thd.diagonal) { // Selecting in a straight rectangle (or a single square)
		int x_start = _thd.pos.x;
		int y_start = _thd.pos.y;

		if (_thd.outersize.x != 0) {
			x_size  += _thd.outersize.x;
			x_start += _thd.offs.x;
			y_size  += _thd.outersize.y;
			y_start += _thd.offs.y;
		}

		x_size -= TILE_SIZE;
		y_size -= TILE_SIZE;

		assert(x_size >= 0);
		assert(y_size >= 0);

		int x_end = Clamp(x_start + x_size, 0, MapSizeX() * TILE_SIZE - TILE_SIZE);
		int y_end = Clamp(y_start + y_size, 0, MapSizeY() * TILE_SIZE - TILE_SIZE);

		x_start = Clamp(x_start, 0, MapSizeX() * TILE_SIZE - TILE_SIZE);
		y_start = Clamp(y_start, 0, MapSizeY() * TILE_SIZE - TILE_SIZE);

		/* make sure everything is multiple of TILE_SIZE */
		assert((x_end | y_end | x_start | y_start) % TILE_SIZE == 0);

		/* How it works:
		 * Suppose we have to mark dirty rectangle of 3x4 tiles:
		 *   x
		 *  xxx
		 * xxxxx
		 *  xxxxx
		 *   xxx
		 *    x
		 * This algorithm marks dirty columns of tiles, so it is done in 3+4-1 steps:
		 * 1)  x     2)  x
		 *    xxx       Oxx
		 *   Oxxxx     xOxxx
		 *    xxxxx     Oxxxx
		 *     xxx       xxx
		 *      x         x
		 * And so forth...
		 */

		int top_x = x_end; // coordinates of top dirty tile
		int top_y = y_start;
		int bot_x = top_x; // coordinates of bottom dirty tile
		int bot_y = top_y;

		do {
			/* topmost dirty point */
			TileIndex top_tile = TileVirtXY(top_x, top_y);
			Point top = RemapCoords(top_x, top_y, GetTileMaxPixelZ(top_tile));

			/* bottommost point */
			TileIndex bottom_tile = TileVirtXY(bot_x, bot_y);
			Point bot = RemapCoords(bot_x + TILE_SIZE, bot_y + TILE_SIZE, GetTilePixelZ(bottom_tile)); // bottommost point

			/* the 'x' coordinate of 'top' and 'bot' is the same (and always in the same distance from tile middle),
			 * tile height/slope affects only the 'y' on-screen coordinate! */

			int l = top.x - TILE_PIXELS * ZOOM_LVL_BASE; // 'x' coordinate of left   side of the dirty rectangle
			int t = top.y;                               // 'y' coordinate of top    side of the dirty rectangle
			int r = top.x + TILE_PIXELS * ZOOM_LVL_BASE; // 'x' coordinate of right  side of the dirty rectangle
			int b = bot.y;                               // 'y' coordinate of bottom side of the dirty rectangle

			static const int OVERLAY_WIDTH = 4 * ZOOM_LVL_BASE; // part of selection sprites is drawn outside the selected area (in particular: terraforming)

			/* For halftile foundations on SLOPE_STEEP_S the sprite extents some more towards the top */
			MarkAllViewportsDirty(l - OVERLAY_WIDTH, t - OVERLAY_WIDTH - TILE_HEIGHT * ZOOM_LVL_BASE, r + OVERLAY_WIDTH, b + OVERLAY_WIDTH);

			/* haven't we reached the topmost tile yet? */
			if (top_x != x_start) {
				top_x -= TILE_SIZE;
			} else {
				top_y += TILE_SIZE;
			}

			/* the way the bottom tile changes is different when we reach the bottommost tile */
			if (bot_y != y_end) {
				bot_y += TILE_SIZE;
			} else {
				bot_x -= TILE_SIZE;
			}
		} while (bot_x >= top_x);
	} else { // Selecting in a 45 degrees rotated (diagonal) rectangle.
		/* a_size, b_size describe a rectangle with rotated coordinates */
		int a_size = x_size + y_size, b_size = x_size - y_size;

		int interval_a = a_size < 0 ? -(int)TILE_SIZE : (int)TILE_SIZE;
		int interval_b = b_size < 0 ? -(int)TILE_SIZE : (int)TILE_SIZE;

		for (int a = -interval_a; a != a_size + interval_a; a += interval_a) {
			for (int b = -interval_b; b != b_size + interval_b; b += interval_b) {
				uint x = (_thd.pos.x + (a + b) / 2) / TILE_SIZE;
				uint y = (_thd.pos.y + (a - b) / 2) / TILE_SIZE;

				if (x < MapMaxX() && y < MapMaxY()) {
					MarkTileDirtyByTile(TileXY(x, y));
				}
			}
		}
	}
}


void SetSelectionRed(bool b)
{
	_thd.make_square_red = b;
	SetSelectionTilesDirty();
}

/**
 * Test whether a sign is below the mouse
 * @param vp the clicked viewport
 * @param x X position of click
 * @param y Y position of click
 * @param sign the sign to check
 * @return true if the sign was hit
 */
static bool CheckClickOnViewportSign(const Viewport *vp, int x, int y, const ViewportSign *sign)
{
	bool small = (vp->zoom >= ZOOM_LVL_OUT_16X);
	int sign_half_width = ScaleByZoom((small ? sign->width_small : sign->width_normal) / 2, vp->zoom);
	int sign_height = ScaleByZoom(VPSM_TOP + (small ? FONT_HEIGHT_SMALL : FONT_HEIGHT_NORMAL) + VPSM_BOTTOM, vp->zoom);

	return y >= sign->top && y < sign->top + sign_height &&
			x >= sign->center - sign_half_width && x < sign->center + sign_half_width;
}


/**
 * Check whether any viewport sign was clicked, and dispatch the click.
 * @param vp the clicked viewport
 * @param x X position of click
 * @param y Y position of click
 * @return true if the sign was hit
 */
static bool CheckClickOnViewportSign(const Viewport *vp, int x, int y)
{
	if (_game_mode == GM_MENU) return false;

	x = ScaleByZoom(x - vp->left, vp->zoom) + vp->virtual_left;
	y = ScaleByZoom(y - vp->top, vp->zoom) + vp->virtual_top;

	Rect search_rect{ x - 1, y - 1, x + 1, y + 1 };
	search_rect = ExpandRectWithViewportSignMargins(search_rect, vp->zoom);

	bool show_stations = HasBit(_display_opt, DO_SHOW_STATION_NAMES) && !IsInvisibilitySet(TO_SIGNS);
	bool show_waypoints = HasBit(_display_opt, DO_SHOW_WAYPOINT_NAMES) && !IsInvisibilitySet(TO_SIGNS);
	bool show_towns = HasBit(_display_opt, DO_SHOW_TOWN_NAMES);
	bool show_signs = HasBit(_display_opt, DO_SHOW_SIGNS) && !IsInvisibilitySet(TO_SIGNS);
	bool show_competitors = HasBit(_display_opt, DO_SHOW_COMPETITOR_SIGNS);

	/* Topmost of each type that was hit */
	BaseStation *st = nullptr, *last_st = nullptr;
	Town *t = nullptr, *last_t = nullptr;
	Sign *si = nullptr, *last_si = nullptr;

	/* See ViewportAddKdtreeSigns() for details on the search logic */
	_viewport_sign_kdtree.FindContained(search_rect.left, search_rect.top, search_rect.right, search_rect.bottom, [&](const ViewportSignKdtreeItem & item) {
		switch (item.type) {
			case ViewportSignKdtreeItem::VKI_STATION:
				if (!show_stations) break;
				st = BaseStation::Get(item.id.station);
				if (!show_competitors && _local_company != st->owner && st->owner != OWNER_NONE) break;
				if (CheckClickOnViewportSign(vp, x, y, &st->sign)) last_st = st;
				break;

			case ViewportSignKdtreeItem::VKI_WAYPOINT:
				if (!show_waypoints) break;
				st = BaseStation::Get(item.id.station);
				if (!show_competitors && _local_company != st->owner && st->owner != OWNER_NONE) break;
				if (CheckClickOnViewportSign(vp, x, y, &st->sign)) last_st = st;
				break;

			case ViewportSignKdtreeItem::VKI_TOWN:
				if (!show_towns) break;
				t = Town::Get(item.id.town);
				if (CheckClickOnViewportSign(vp, x, y, &t->cache.sign)) last_t = t;
				break;

			case ViewportSignKdtreeItem::VKI_SIGN:
				if (!show_signs) break;
				si = Sign::Get(item.id.sign);
				if (!show_competitors && _local_company != si->owner && si->owner != OWNER_DEITY) break;
				if (CheckClickOnViewportSign(vp, x, y, &si->sign)) last_si = si;
				break;

			default:
				NOT_REACHED();
		}
	});

	/* Select which hit to handle based on priority */
	if (last_st != nullptr) {
		if (Station::IsExpected(last_st)) {
			ShowStationViewWindow(last_st->index);
		} else {
			ShowWaypointWindow(Waypoint::From(last_st));
		}
		return true;
	} else if (last_t != nullptr) {
		ShowTownViewWindow(last_t->index);
		return true;
	} else if (last_si != nullptr) {
		HandleClickOnSign(last_si);
		return true;
	} else {
		return false;
	}
}


ViewportSignKdtreeItem ViewportSignKdtreeItem::MakeStation(StationID id)
{
	ViewportSignKdtreeItem item;
	item.type = VKI_STATION;
	item.id.station = id;

	const Station *st = Station::Get(id);
	assert(st->sign.kdtree_valid);
	item.center = st->sign.center;
	item.top = st->sign.top;

	/* Assume the sign can be a candidate for drawing, so measure its width */
	_viewport_sign_maxwidth = std::max<int>(_viewport_sign_maxwidth, st->sign.width_normal);

	return item;
}

ViewportSignKdtreeItem ViewportSignKdtreeItem::MakeWaypoint(StationID id)
{
	ViewportSignKdtreeItem item;
	item.type = VKI_WAYPOINT;
	item.id.station = id;

	const Waypoint *st = Waypoint::Get(id);
	assert(st->sign.kdtree_valid);
	item.center = st->sign.center;
	item.top = st->sign.top;

	/* Assume the sign can be a candidate for drawing, so measure its width */
	_viewport_sign_maxwidth = std::max<int>(_viewport_sign_maxwidth, st->sign.width_normal);

	return item;
}

ViewportSignKdtreeItem ViewportSignKdtreeItem::MakeTown(TownID id)
{
	ViewportSignKdtreeItem item;
	item.type = VKI_TOWN;
	item.id.town = id;

	const Town *town = Town::Get(id);
	assert(town->cache.sign.kdtree_valid);
	item.center = town->cache.sign.center;
	item.top = town->cache.sign.top;

	/* Assume the sign can be a candidate for drawing, so measure its width */
	_viewport_sign_maxwidth = std::max<int>(_viewport_sign_maxwidth, town->cache.sign.width_normal);

	return item;
}

ViewportSignKdtreeItem ViewportSignKdtreeItem::MakeSign(SignID id)
{
	ViewportSignKdtreeItem item;
	item.type = VKI_SIGN;
	item.id.sign = id;

	const Sign *sign = Sign::Get(id);
	assert(sign->sign.kdtree_valid);
	item.center = sign->sign.center;
	item.top = sign->sign.top;

	/* Assume the sign can be a candidate for drawing, so measure its width */
	_viewport_sign_maxwidth = std::max<int>(_viewport_sign_maxwidth, sign->sign.width_normal);

	return item;
}

void RebuildViewportKdtree()
{
	/* Reset biggest size sign seen */
	_viewport_sign_maxwidth = 0;

	std::vector<ViewportSignKdtreeItem> items;
	items.reserve(BaseStation::GetNumItems() + Town::GetNumItems() + Sign::GetNumItems());

	for (const Station *st : Station::Iterate()) {
		if (st->sign.kdtree_valid) items.push_back(ViewportSignKdtreeItem::MakeStation(st->index));
	}

	for (const Waypoint *wp : Waypoint::Iterate()) {
		if (wp->sign.kdtree_valid) items.push_back(ViewportSignKdtreeItem::MakeWaypoint(wp->index));
	}

	for (const Town *town : Town::Iterate()) {
		if (town->cache.sign.kdtree_valid) items.push_back(ViewportSignKdtreeItem::MakeTown(town->index));
	}

	for (const Sign *sign : Sign::Iterate()) {
		if (sign->sign.kdtree_valid) items.push_back(ViewportSignKdtreeItem::MakeSign(sign->index));
	}

	_viewport_sign_kdtree.Build(items.begin(), items.end());
}


static bool CheckClickOnLandscape(const Viewport *vp, int x, int y)
{
	Point pt = TranslateXYToTileCoord(vp, x, y);

	if (pt.x != -1) return ClickTile(TileVirtXY(pt.x, pt.y));
	return true;
}

static void PlaceObject()
{
	Point pt;
	Window *w;

	pt = GetTileBelowCursor();
	if (pt.x == -1) return;

	if ((_thd.place_mode & HT_DRAG_MASK) == HT_POINT) {
		pt.x += TILE_SIZE / 2;
		pt.y += TILE_SIZE / 2;
	}

	_tile_fract_coords.x = pt.x & TILE_UNIT_MASK;
	_tile_fract_coords.y = pt.y & TILE_UNIT_MASK;

	w = _thd.GetCallbackWnd();
	if (w != nullptr) w->OnPlaceObject(pt, TileVirtXY(pt.x, pt.y));
}


bool HandleViewportClicked(const Viewport *vp, int x, int y)
{
	const Vehicle *v = CheckClickOnVehicle(vp, x, y);

	if (_thd.place_mode & HT_VEHICLE) {
		if (v != nullptr && VehicleClicked(v)) return true;
	}

	/* Vehicle placement mode already handled above. */
	if ((_thd.place_mode & HT_DRAG_MASK) != HT_NONE) {
		PlaceObject();
		return true;
	}

	if (CheckClickOnViewportSign(vp, x, y)) return true;
	bool result = CheckClickOnLandscape(vp, x, y);

	if (v != nullptr) {
		Debug(misc, 2, "Vehicle {} (index {}) at {}", v->unitnumber, v->index, fmt::ptr(v));
		if (IsCompanyBuildableVehicleType(v)) {
			v = v->First();
			if (_ctrl_pressed && v->owner == _local_company) {
				StartStopVehicle(v, true);
			} else {
				ShowVehicleViewWindow(v);
			}
		}
		return true;
	}
	return result;
}

void RebuildViewportOverlay(Window *w)
{
	if (w->viewport->overlay != nullptr &&
			w->viewport->overlay->GetCompanyMask() != 0 &&
			w->viewport->overlay->GetCargoMask() != 0) {
		w->viewport->overlay->SetDirty();
		w->SetDirty();
	}
}

/**
 * Scrolls the viewport in a window to a given location.
 * @param x       Desired x location of the map to scroll to (world coordinate).
 * @param y       Desired y location of the map to scroll to (world coordinate).
 * @param z       Desired z location of the map to scroll to (world coordinate). Use \c -1 to scroll to the height of the map at the \a x, \a y location.
 * @param w       %Window containing the viewport.
 * @param instant Jump to the location instead of slowly moving to it.
 * @return Destination of the viewport was changed (to activate other actions when the viewport is already at the desired position).
 */
bool ScrollWindowTo(int x, int y, int z, Window *w, bool instant)
{
	/* The slope cannot be acquired outside of the map, so make sure we are always within the map. */
	if (z == -1) {
		if ( x >= 0 && x <= (int)MapSizeX() * (int)TILE_SIZE - 1
				&& y >= 0 && y <= (int)MapSizeY() * (int)TILE_SIZE - 1) {
			z = GetSlopePixelZ(x, y);
		} else {
			z = TileHeightOutsideMap(x / (int)TILE_SIZE, y / (int)TILE_SIZE);
		}
	}

	Point pt = MapXYZToViewport(w->viewport, x, y, z);
	w->viewport->follow_vehicle = INVALID_VEHICLE;

	if (w->viewport->dest_scrollpos_x == pt.x && w->viewport->dest_scrollpos_y == pt.y) return false;

	if (instant) {
		w->viewport->scrollpos_x = pt.x;
		w->viewport->scrollpos_y = pt.y;
		RebuildViewportOverlay(w);
	}

	w->viewport->dest_scrollpos_x = pt.x;
	w->viewport->dest_scrollpos_y = pt.y;
	return true;
}

/**
 * Scrolls the viewport in a window to a given location.
 * @param tile    Desired tile to center on.
 * @param w       %Window containing the viewport.
 * @param instant Jump to the location instead of slowly moving to it.
 * @return Destination of the viewport was changed (to activate other actions when the viewport is already at the desired position).
 */
bool ScrollWindowToTile(TileIndex tile, Window *w, bool instant)
{
	return ScrollWindowTo(TileX(tile) * TILE_SIZE, TileY(tile) * TILE_SIZE, -1, w, instant);
}

/**
 * Scrolls the viewport of the main window to a given location.
 * @param tile    Desired tile to center on.
 * @param instant Jump to the location instead of slowly moving to it.
 * @return Destination of the viewport was changed (to activate other actions when the viewport is already at the desired position).
 */
bool ScrollMainWindowToTile(TileIndex tile, bool instant)
{
	return ScrollMainWindowTo(TileX(tile) * TILE_SIZE + TILE_SIZE / 2, TileY(tile) * TILE_SIZE + TILE_SIZE / 2, -1, instant);
}

/**
 * Set a tile to display a red error square.
 * @param tile Tile that should show the red error square.
 */
void SetRedErrorSquare(TileIndex tile)
{
	TileIndex old;

	old = _thd.redsq;
	_thd.redsq = tile;

	if (tile != old) {
		if (tile != INVALID_TILE) MarkTileDirtyByTile(tile);
		if (old  != INVALID_TILE) MarkTileDirtyByTile(old);
	}
}

/**
 * Highlight \a w by \a h tiles at the cursor.
 * @param w Width of the highlighted tiles rectangle.
 * @param h Height of the highlighted tiles rectangle.
 */
void SetTileSelectSize(int w, int h)
{
	_thd.new_size.x = w * TILE_SIZE;
	_thd.new_size.y = h * TILE_SIZE;
	_thd.new_outersize.x = 0;
	_thd.new_outersize.y = 0;
}

void SetTileSelectBigSize(int ox, int oy, int sx, int sy)
{
	_thd.offs.x = ox * TILE_SIZE;
	_thd.offs.y = oy * TILE_SIZE;
	_thd.new_outersize.x = sx * TILE_SIZE;
	_thd.new_outersize.y = sy * TILE_SIZE;
}

/** returns the best autorail highlight type from map coordinates */
static HighLightStyle GetAutorailHT(int x, int y)
{
	return HT_RAIL | _autorail_piece[x & TILE_UNIT_MASK][y & TILE_UNIT_MASK];
}

/**
 * Reset tile highlighting.
 */
void TileHighlightData::Reset()
{
	this->pos.x = 0;
	this->pos.y = 0;
	this->new_pos.x = 0;
	this->new_pos.y = 0;
}

/**
 * Is the user dragging a 'diagonal rectangle'?
 * @return User is dragging a rotated rectangle.
 */
bool TileHighlightData::IsDraggingDiagonal()
{
	return (this->place_mode & HT_DIAGONAL) != 0 && _ctrl_pressed && _left_button_down;
}

/**
 * Get the window that started the current highlighting.
 * @return The window that requested the current tile highlighting, or \c nullptr if not available.
 */
Window *TileHighlightData::GetCallbackWnd()
{
	return FindWindowById(this->window_class, this->window_number);
}



/**
 * Updates tile highlighting for all cases.
 * Uses _thd.selstart and _thd.selend and _thd.place_mode (set elsewhere) to determine _thd.pos and _thd.size
 * Also drawstyle is determined. Uses _thd.new.* as a buffer and calls SetSelectionTilesDirty() twice,
 * Once for the old and once for the new selection.
 * _thd is TileHighlightData, found in viewport.h
 */
void UpdateTileSelection()
{
	int x1;
	int y1;

	if (_thd.freeze) return;

	HighLightStyle new_drawstyle = HT_NONE;
	bool new_diagonal = false;

	if ((_thd.place_mode & HT_DRAG_MASK) == HT_SPECIAL) {
		x1 = _thd.selend.x;
		y1 = _thd.selend.y;
		if (x1 != -1) {
			int x2 = _thd.selstart.x & ~TILE_UNIT_MASK;
			int y2 = _thd.selstart.y & ~TILE_UNIT_MASK;
			x1 &= ~TILE_UNIT_MASK;
			y1 &= ~TILE_UNIT_MASK;

			if (_thd.IsDraggingDiagonal()) {
				new_diagonal = true;
			} else {
				if (x1 >= x2) Swap(x1, x2);
				if (y1 >= y2) Swap(y1, y2);
			}
			_thd.new_pos.x = x1;
			_thd.new_pos.y = y1;
			_thd.new_size.x = x2 - x1;
			_thd.new_size.y = y2 - y1;
			if (!new_diagonal) {
				_thd.new_size.x += TILE_SIZE;
				_thd.new_size.y += TILE_SIZE;
			}
			new_drawstyle = _thd.next_drawstyle;
		}
	} else if ((_thd.place_mode & HT_DRAG_MASK) != HT_NONE) {
		Point pt = GetTileBelowCursor();
		x1 = pt.x;
		y1 = pt.y;
		if (x1 != -1) {
			switch (_thd.place_mode & HT_DRAG_MASK) {
				case HT_RECT:
					new_drawstyle = HT_RECT;
					break;
				case HT_POINT:
					new_drawstyle = HT_POINT;
					x1 += TILE_SIZE / 2;
					y1 += TILE_SIZE / 2;
					break;
				case HT_RAIL:
					/* Draw one highlighted tile in any direction */
					new_drawstyle = GetAutorailHT(pt.x, pt.y);
					break;
				case HT_LINE:
					switch (_thd.place_mode & HT_DIR_MASK) {
						case HT_DIR_X: new_drawstyle = HT_LINE | HT_DIR_X; break;
						case HT_DIR_Y: new_drawstyle = HT_LINE | HT_DIR_Y; break;

						case HT_DIR_HU:
						case HT_DIR_HL:
							new_drawstyle = (pt.x & TILE_UNIT_MASK) + (pt.y & TILE_UNIT_MASK) <= TILE_SIZE ? HT_LINE | HT_DIR_HU : HT_LINE | HT_DIR_HL;
							break;

						case HT_DIR_VL:
						case HT_DIR_VR:
							new_drawstyle = (pt.x & TILE_UNIT_MASK) > (pt.y & TILE_UNIT_MASK) ? HT_LINE | HT_DIR_VL : HT_LINE | HT_DIR_VR;
							break;

						default: NOT_REACHED();
					}
					_thd.selstart.x = x1 & ~TILE_UNIT_MASK;
					_thd.selstart.y = y1 & ~TILE_UNIT_MASK;
					break;
				default:
					NOT_REACHED();
			}
			_thd.new_pos.x = x1 & ~TILE_UNIT_MASK;
			_thd.new_pos.y = y1 & ~TILE_UNIT_MASK;
		}
	}

	/* redraw selection */
	if (_thd.drawstyle != new_drawstyle ||
			_thd.pos.x != _thd.new_pos.x || _thd.pos.y != _thd.new_pos.y ||
			_thd.size.x != _thd.new_size.x || _thd.size.y != _thd.new_size.y ||
			_thd.outersize.x != _thd.new_outersize.x ||
			_thd.outersize.y != _thd.new_outersize.y ||
			_thd.diagonal    != new_diagonal) {
		/* Clear the old tile selection? */
		if ((_thd.drawstyle & HT_DRAG_MASK) != HT_NONE) SetSelectionTilesDirty();

		_thd.drawstyle = new_drawstyle;
		_thd.pos = _thd.new_pos;
		_thd.size = _thd.new_size;
		_thd.outersize = _thd.new_outersize;
		_thd.diagonal = new_diagonal;
		_thd.dirty = 0xff;

		/* Draw the new tile selection? */
		if ((new_drawstyle & HT_DRAG_MASK) != HT_NONE) SetSelectionTilesDirty();
	}
}

/**
 * Displays the measurement tooltips when selecting multiple tiles
 * @param str String to be displayed
 * @param paramcount number of params to deal with
 * @param params (optional) up to 5 pieces of additional information that may be added to a tooltip
 * @param close_cond Condition for closing this tooltip.
 */
static inline void ShowMeasurementTooltips(StringID str, uint paramcount, const uint64 params[], TooltipCloseCondition close_cond = TCC_EXIT_VIEWPORT)
{
	if (!_settings_client.gui.measure_tooltip) return;
	GuiShowTooltips(_thd.GetCallbackWnd(), str, paramcount, params, close_cond);
}

static void HideMeasurementTooltips()
{
	CloseWindowById(WC_TOOLTIPS, 0);
}

/** highlighting tiles while only going over them with the mouse */
void VpStartPlaceSizing(TileIndex tile, ViewportPlaceMethod method, ViewportDragDropSelectionProcess process)
{
	_thd.select_method = method;
	_thd.select_proc   = process;
	_thd.selend.x = TileX(tile) * TILE_SIZE;
	_thd.selstart.x = TileX(tile) * TILE_SIZE;
	_thd.selend.y = TileY(tile) * TILE_SIZE;
	_thd.selstart.y = TileY(tile) * TILE_SIZE;

	/* Needed so several things (road, autoroad, bridges, ...) are placed correctly.
	 * In effect, placement starts from the centre of a tile
	 */
	if (method == VPM_X_OR_Y || method == VPM_FIX_X || method == VPM_FIX_Y) {
		_thd.selend.x += TILE_SIZE / 2;
		_thd.selend.y += TILE_SIZE / 2;
		_thd.selstart.x += TILE_SIZE / 2;
		_thd.selstart.y += TILE_SIZE / 2;
	}

	HighLightStyle others = _thd.place_mode & ~(HT_DRAG_MASK | HT_DIR_MASK);
	if ((_thd.place_mode & HT_DRAG_MASK) == HT_RECT) {
		_thd.place_mode = HT_SPECIAL | others;
		_thd.next_drawstyle = HT_RECT | others;
	} else if (_thd.place_mode & (HT_RAIL | HT_LINE)) {
		_thd.place_mode = HT_SPECIAL | others;
		_thd.next_drawstyle = _thd.drawstyle | others;
	} else {
		_thd.place_mode = HT_SPECIAL | others;
		_thd.next_drawstyle = HT_POINT | others;
	}
	_special_mouse_mode = WSM_SIZING;
}

/** Drag over the map while holding the left mouse down. */
void VpStartDragging(ViewportDragDropSelectionProcess process)
{
	_thd.select_method = VPM_X_AND_Y;
	_thd.select_proc = process;
	_thd.selstart.x = 0;
	_thd.selstart.y = 0;
	_thd.next_drawstyle = HT_RECT;

	_special_mouse_mode = WSM_DRAGGING;
}

void VpSetPlaceSizingLimit(int limit)
{
	_thd.sizelimit = limit;
}

/**
 * Highlights all tiles between a set of two tiles. Used in dock and tunnel placement
 * @param from TileIndex of the first tile to highlight
 * @param to TileIndex of the last tile to highlight
 */
void VpSetPresizeRange(TileIndex from, TileIndex to)
{
	uint64 distance = DistanceManhattan(from, to) + 1;

	_thd.selend.x = TileX(to) * TILE_SIZE;
	_thd.selend.y = TileY(to) * TILE_SIZE;
	_thd.selstart.x = TileX(from) * TILE_SIZE;
	_thd.selstart.y = TileY(from) * TILE_SIZE;
	_thd.next_drawstyle = HT_RECT;

	/* show measurement only if there is any length to speak of */
	if (distance > 1) {
		ShowMeasurementTooltips(STR_MEASURE_LENGTH, 1, &distance);
	} else {
		HideMeasurementTooltips();
	}
}

static void VpStartPreSizing()
{
	_thd.selend.x = -1;
	_special_mouse_mode = WSM_PRESIZE;
}

/**
 * returns information about the 2x1 piece to be build.
 * The lower bits (0-3) are the track type.
 */
static HighLightStyle Check2x1AutoRail(int mode)
{
	int fxpy = _tile_fract_coords.x + _tile_fract_coords.y;
	int sxpy = (_thd.selend.x & TILE_UNIT_MASK) + (_thd.selend.y & TILE_UNIT_MASK);
	int fxmy = _tile_fract_coords.x - _tile_fract_coords.y;
	int sxmy = (_thd.selend.x & TILE_UNIT_MASK) - (_thd.selend.y & TILE_UNIT_MASK);

	switch (mode) {
		default: NOT_REACHED();
		case 0: // end piece is lower right
			if (fxpy >= 20 && sxpy <= 12) return HT_DIR_HL;
			if (fxmy < -3 && sxmy > 3) return HT_DIR_VR;
			return HT_DIR_Y;

		case 1:
			if (fxmy > 3 && sxmy < -3) return HT_DIR_VL;
			if (fxpy <= 12 && sxpy >= 20) return HT_DIR_HU;
			return HT_DIR_Y;

		case 2:
			if (fxmy > 3 && sxmy < -3) return HT_DIR_VL;
			if (fxpy >= 20 && sxpy <= 12) return HT_DIR_HL;
			return HT_DIR_X;

		case 3:
			if (fxmy < -3 && sxmy > 3) return HT_DIR_VR;
			if (fxpy <= 12 && sxpy >= 20) return HT_DIR_HU;
			return HT_DIR_X;
	}
}

/**
 * Check if the direction of start and end tile should be swapped based on
 * the dragging-style. Default directions are:
 * in the case of a line (HT_RAIL, HT_LINE):  DIR_NE, DIR_NW, DIR_N, DIR_E
 * in the case of a rect (HT_RECT, HT_POINT): DIR_S, DIR_E
 * For example dragging a rectangle area from south to north should be swapped to
 * north-south (DIR_S) to obtain the same results with less code. This is what
 * the return value signifies.
 * @param style HighLightStyle dragging style
 * @param start_tile start tile of drag
 * @param end_tile end tile of drag
 * @return boolean value which when true means start/end should be swapped
 */
static bool SwapDirection(HighLightStyle style, TileIndex start_tile, TileIndex end_tile)
{
	uint start_x = TileX(start_tile);
	uint start_y = TileY(start_tile);
	uint end_x = TileX(end_tile);
	uint end_y = TileY(end_tile);

	switch (style & HT_DRAG_MASK) {
		case HT_RAIL:
		case HT_LINE: return (end_x > start_x || (end_x == start_x && end_y > start_y));

		case HT_RECT:
		case HT_POINT: return (end_x != start_x && end_y < start_y);
		default: NOT_REACHED();
	}

	return false;
}

/**
 * Calculates height difference between one tile and another.
 * Multiplies the result to suit the standard given by #TILE_HEIGHT_STEP.
 *
 * To correctly get the height difference we need the direction we are dragging
 * in, as well as with what kind of tool we are dragging. For example a horizontal
 * autorail tool that starts in bottom and ends at the top of a tile will need the
 * maximum of SW, S and SE, N corners respectively. This is handled by the lookup table below
 * See #_tileoffs_by_dir in map.cpp for the direction enums if you can't figure out the values yourself.
 * @param style      Highlighting style of the drag. This includes direction and style (autorail, rect, etc.)
 * @param distance   Number of tiles dragged, important for horizontal/vertical drags, ignored for others.
 * @param start_tile Start tile of the drag operation.
 * @param end_tile   End tile of the drag operation.
 * @return Height difference between two tiles. The tile measurement tool utilizes this value in its tooltip.
 */
static int CalcHeightdiff(HighLightStyle style, uint distance, TileIndex start_tile, TileIndex end_tile)
{
	bool swap = SwapDirection(style, start_tile, end_tile);
	uint h0, h1; // Start height and end height.

	if (start_tile == end_tile) return 0;
	if (swap) Swap(start_tile, end_tile);

	switch (style & HT_DRAG_MASK) {
		case HT_RECT: {
			static const TileIndexDiffC heightdiff_area_by_dir[] = {
				/* Start */ {1, 0}, /* Dragging east */ {0, 0}, // Dragging south
				/* End   */ {0, 1}, /* Dragging east */ {1, 1}  // Dragging south
			};

			/* In the case of an area we can determine whether we were dragging south or
			 * east by checking the X-coordinates of the tiles */
			byte style_t = (byte)(TileX(end_tile) > TileX(start_tile));
			start_tile = TILE_ADD(start_tile, ToTileIndexDiff(heightdiff_area_by_dir[style_t]));
			end_tile   = TILE_ADD(end_tile, ToTileIndexDiff(heightdiff_area_by_dir[2 + style_t]));
			FALLTHROUGH;
		}

		case HT_POINT:
			h0 = TileHeight(start_tile);
			h1 = TileHeight(end_tile);
			break;
		default: { // All other types, this is mostly only line/autorail
			static const HighLightStyle flip_style_direction[] = {
				HT_DIR_X, HT_DIR_Y, HT_DIR_HL, HT_DIR_HU, HT_DIR_VR, HT_DIR_VL
			};
			static const TileIndexDiffC heightdiff_line_by_dir[] = {
				/* Start */ {1, 0}, {1, 1}, /* HT_DIR_X  */ {0, 1}, {1, 1}, // HT_DIR_Y
				/* Start */ {1, 0}, {0, 0}, /* HT_DIR_HU */ {1, 0}, {1, 1}, // HT_DIR_HL
				/* Start */ {1, 0}, {1, 1}, /* HT_DIR_VL */ {0, 1}, {1, 1}, // HT_DIR_VR

				/* Start */ {0, 1}, {0, 0}, /* HT_DIR_X  */ {1, 0}, {0, 0}, // HT_DIR_Y
				/* End   */ {0, 1}, {0, 0}, /* HT_DIR_HU */ {1, 1}, {0, 1}, // HT_DIR_HL
				/* End   */ {1, 0}, {0, 0}, /* HT_DIR_VL */ {0, 0}, {0, 1}, // HT_DIR_VR
			};

			distance %= 2; // we're only interested if the distance is even or uneven
			style &= HT_DIR_MASK;

			/* To handle autorail, we do some magic to be able to use a lookup table.
			 * Firstly if we drag the other way around, we switch start&end, and if needed
			 * also flip the drag-position. Eg if it was on the left, and the distance is even
			 * that means the end, which is now the start is on the right */
			if (swap && distance == 0) style = flip_style_direction[style];

			/* Use lookup table for start-tile based on HighLightStyle direction */
			byte style_t = style * 2;
			assert(style_t < lengthof(heightdiff_line_by_dir) - 13);
			h0 = TileHeight(TILE_ADD(start_tile, ToTileIndexDiff(heightdiff_line_by_dir[style_t])));
			uint ht = TileHeight(TILE_ADD(start_tile, ToTileIndexDiff(heightdiff_line_by_dir[style_t + 1])));
			h0 = std::max(h0, ht);

			/* Use lookup table for end-tile based on HighLightStyle direction
			 * flip around side (lower/upper, left/right) based on distance */
			if (distance == 0) style_t = flip_style_direction[style] * 2;
			assert(style_t < lengthof(heightdiff_line_by_dir) - 13);
			h1 = TileHeight(TILE_ADD(end_tile, ToTileIndexDiff(heightdiff_line_by_dir[12 + style_t])));
			ht = TileHeight(TILE_ADD(end_tile, ToTileIndexDiff(heightdiff_line_by_dir[12 + style_t + 1])));
			h1 = std::max(h1, ht);
			break;
		}
	}

	if (swap) Swap(h0, h1);
	return (int)(h1 - h0) * TILE_HEIGHT_STEP;
}

static const StringID measure_strings_length[] = {STR_NULL, STR_MEASURE_LENGTH, STR_MEASURE_LENGTH_HEIGHTDIFF};

/**
 * Check for underflowing the map.
 * @param test  the variable to test for underflowing
 * @param other the other variable to update to keep the line
 * @param mult  the constant to multiply the difference by for \c other
 */
static void CheckUnderflow(int &test, int &other, int mult)
{
	if (test >= 0) return;

	other += mult * test;
	test = 0;
}

/**
 * Check for overflowing the map.
 * @param test  the variable to test for overflowing
 * @param other the other variable to update to keep the line
 * @param max   the maximum value for the \c test variable
 * @param mult  the constant to multiply the difference by for \c other
 */
static void CheckOverflow(int &test, int &other, int max, int mult)
{
	if (test <= max) return;

	other += mult * (test - max);
	test = max;
}

/** while dragging */
static void CalcRaildirsDrawstyle(int x, int y, int method)
{
	HighLightStyle b;

	int dx = _thd.selstart.x - (_thd.selend.x & ~TILE_UNIT_MASK);
	int dy = _thd.selstart.y - (_thd.selend.y & ~TILE_UNIT_MASK);
	uint w = abs(dx) + TILE_SIZE;
	uint h = abs(dy) + TILE_SIZE;

	if (method & ~(VPM_RAILDIRS | VPM_SIGNALDIRS)) {
		/* We 'force' a selection direction; first four rail buttons. */
		method &= ~(VPM_RAILDIRS | VPM_SIGNALDIRS);
		int raw_dx = _thd.selstart.x - _thd.selend.x;
		int raw_dy = _thd.selstart.y - _thd.selend.y;
		switch (method) {
			case VPM_FIX_X:
				b = HT_LINE | HT_DIR_Y;
				x = _thd.selstart.x;
				break;

			case VPM_FIX_Y:
				b = HT_LINE | HT_DIR_X;
				y = _thd.selstart.y;
				break;

			case VPM_FIX_HORIZONTAL:
				if (dx == -dy) {
					/* We are on a straight horizontal line. Determine the 'rail'
					 * to build based the sub tile location. */
					b = (x & TILE_UNIT_MASK) + (y & TILE_UNIT_MASK) >= TILE_SIZE ? HT_LINE | HT_DIR_HL : HT_LINE | HT_DIR_HU;
				} else {
					/* We are not on a straight line. Determine the rail to build
					 * based on whether we are above or below it. */
					b = dx + dy >= (int)TILE_SIZE ? HT_LINE | HT_DIR_HU : HT_LINE | HT_DIR_HL;

					/* Calculate where a horizontal line through the start point and
					 * a vertical line from the selected end point intersect and
					 * use that point as the end point. */
					int offset = (raw_dx - raw_dy) / 2;
					x = _thd.selstart.x - (offset & ~TILE_UNIT_MASK);
					y = _thd.selstart.y + (offset & ~TILE_UNIT_MASK);

					/* 'Build' the last half rail tile if needed */
					if ((offset & TILE_UNIT_MASK) > (TILE_SIZE / 2)) {
						if (dx + dy >= (int)TILE_SIZE) {
							x += (dx + dy < 0) ? (int)TILE_SIZE : -(int)TILE_SIZE;
						} else {
							y += (dx + dy < 0) ? (int)TILE_SIZE : -(int)TILE_SIZE;
						}
					}

					/* Make sure we do not overflow the map! */
					CheckUnderflow(x, y, 1);
					CheckUnderflow(y, x, 1);
					CheckOverflow(x, y, (MapMaxX() - 1) * TILE_SIZE, 1);
					CheckOverflow(y, x, (MapMaxY() - 1) * TILE_SIZE, 1);
					assert(x >= 0 && y >= 0 && x <= (int)(MapMaxX() * TILE_SIZE) && y <= (int)(MapMaxY() * TILE_SIZE));
				}
				break;

			case VPM_FIX_VERTICAL:
				if (dx == dy) {
					/* We are on a straight vertical line. Determine the 'rail'
					 * to build based the sub tile location. */
					b = (x & TILE_UNIT_MASK) > (y & TILE_UNIT_MASK) ? HT_LINE | HT_DIR_VL : HT_LINE | HT_DIR_VR;
				} else {
					/* We are not on a straight line. Determine the rail to build
					 * based on whether we are left or right from it. */
					b = dx < dy ? HT_LINE | HT_DIR_VL : HT_LINE | HT_DIR_VR;

					/* Calculate where a vertical line through the start point and
					 * a horizontal line from the selected end point intersect and
					 * use that point as the end point. */
					int offset = (raw_dx + raw_dy + (int)TILE_SIZE) / 2;
					x = _thd.selstart.x - (offset & ~TILE_UNIT_MASK);
					y = _thd.selstart.y - (offset & ~TILE_UNIT_MASK);

					/* 'Build' the last half rail tile if needed */
					if ((offset & TILE_UNIT_MASK) > (TILE_SIZE / 2)) {
						if (dx - dy < 0) {
							y += (dx > dy) ? (int)TILE_SIZE : -(int)TILE_SIZE;
						} else {
							x += (dx < dy) ? (int)TILE_SIZE : -(int)TILE_SIZE;
						}
					}

					/* Make sure we do not overflow the map! */
					CheckUnderflow(x, y, -1);
					CheckUnderflow(y, x, -1);
					CheckOverflow(x, y, (MapMaxX() - 1) * TILE_SIZE, -1);
					CheckOverflow(y, x, (MapMaxY() - 1) * TILE_SIZE, -1);
					assert(x >= 0 && y >= 0 && x <= (int)(MapMaxX() * TILE_SIZE) && y <= (int)(MapMaxY() * TILE_SIZE));
				}
				break;

			default:
				NOT_REACHED();
		}
	} else if (TileVirtXY(_thd.selstart.x, _thd.selstart.y) == TileVirtXY(x, y)) { // check if we're only within one tile
		if (method & VPM_RAILDIRS) {
			b = GetAutorailHT(x, y);
		} else { // rect for autosignals on one tile
			b = HT_RECT;
		}
	} else if (h == TILE_SIZE) { // Is this in X direction?
		if (dx == (int)TILE_SIZE) { // 2x1 special handling
			b = (Check2x1AutoRail(3)) | HT_LINE;
		} else if (dx == -(int)TILE_SIZE) {
			b = (Check2x1AutoRail(2)) | HT_LINE;
		} else {
			b = HT_LINE | HT_DIR_X;
		}
		y = _thd.selstart.y;
	} else if (w == TILE_SIZE) { // Or Y direction?
		if (dy == (int)TILE_SIZE) { // 2x1 special handling
			b = (Check2x1AutoRail(1)) | HT_LINE;
		} else if (dy == -(int)TILE_SIZE) { // 2x1 other direction
			b = (Check2x1AutoRail(0)) | HT_LINE;
		} else {
			b = HT_LINE | HT_DIR_Y;
		}
		x = _thd.selstart.x;
	} else if (w > h * 2) { // still count as x dir?
		b = HT_LINE | HT_DIR_X;
		y = _thd.selstart.y;
	} else if (h > w * 2) { // still count as y dir?
		b = HT_LINE | HT_DIR_Y;
		x = _thd.selstart.x;
	} else { // complicated direction
		int d = w - h;
		_thd.selend.x = _thd.selend.x & ~TILE_UNIT_MASK;
		_thd.selend.y = _thd.selend.y & ~TILE_UNIT_MASK;

		/* four cases. */
		if (x > _thd.selstart.x) {
			if (y > _thd.selstart.y) {
				/* south */
				if (d == 0) {
					b = (x & TILE_UNIT_MASK) > (y & TILE_UNIT_MASK) ? HT_LINE | HT_DIR_VL : HT_LINE | HT_DIR_VR;
				} else if (d >= 0) {
					x = _thd.selstart.x + h;
					b = HT_LINE | HT_DIR_VL;
				} else {
					y = _thd.selstart.y + w;
					b = HT_LINE | HT_DIR_VR;
				}
			} else {
				/* west */
				if (d == 0) {
					b = (x & TILE_UNIT_MASK) + (y & TILE_UNIT_MASK) >= TILE_SIZE ? HT_LINE | HT_DIR_HL : HT_LINE | HT_DIR_HU;
				} else if (d >= 0) {
					x = _thd.selstart.x + h;
					b = HT_LINE | HT_DIR_HL;
				} else {
					y = _thd.selstart.y - w;
					b = HT_LINE | HT_DIR_HU;
				}
			}
		} else {
			if (y > _thd.selstart.y) {
				/* east */
				if (d == 0) {
					b = (x & TILE_UNIT_MASK) + (y & TILE_UNIT_MASK) >= TILE_SIZE ? HT_LINE | HT_DIR_HL : HT_LINE | HT_DIR_HU;
				} else if (d >= 0) {
					x = _thd.selstart.x - h;
					b = HT_LINE | HT_DIR_HU;
				} else {
					y = _thd.selstart.y + w;
					b = HT_LINE | HT_DIR_HL;
				}
			} else {
				/* north */
				if (d == 0) {
					b = (x & TILE_UNIT_MASK) > (y & TILE_UNIT_MASK) ? HT_LINE | HT_DIR_VL : HT_LINE | HT_DIR_VR;
				} else if (d >= 0) {
					x = _thd.selstart.x - h;
					b = HT_LINE | HT_DIR_VR;
				} else {
					y = _thd.selstart.y - w;
					b = HT_LINE | HT_DIR_VL;
				}
			}
		}
	}

	if (_settings_client.gui.measure_tooltip) {
		TileIndex t0 = TileVirtXY(_thd.selstart.x, _thd.selstart.y);
		TileIndex t1 = TileVirtXY(x, y);
		uint distance = DistanceManhattan(t0, t1) + 1;
		byte index = 0;
		uint64 params[2];

		if (distance != 1) {
			int heightdiff = CalcHeightdiff(b, distance, t0, t1);
			/* If we are showing a tooltip for horizontal or vertical drags,
			 * 2 tiles have a length of 1. To bias towards the ceiling we add
			 * one before division. It feels more natural to count 3 lengths as 2 */
			if ((b & HT_DIR_MASK) != HT_DIR_X && (b & HT_DIR_MASK) != HT_DIR_Y) {
				distance = CeilDiv(distance, 2);
			}

			params[index++] = distance;
			if (heightdiff != 0) params[index++] = heightdiff;
		}

		ShowMeasurementTooltips(measure_strings_length[index], index, params);
	}

	_thd.selend.x = x;
	_thd.selend.y = y;
	_thd.next_drawstyle = b;
}

/**
 * Selects tiles while dragging
 * @param x X coordinate of end of selection
 * @param y Y coordinate of end of selection
 * @param method modifies the way tiles are selected. Possible
 * methods are VPM_* in viewport.h
 */
void VpSelectTilesWithMethod(int x, int y, ViewportPlaceMethod method)
{
	int sx, sy;
	HighLightStyle style;

	if (x == -1) {
		_thd.selend.x = -1;
		return;
	}

	/* Special handling of drag in any (8-way) direction */
	if (method & (VPM_RAILDIRS | VPM_SIGNALDIRS)) {
		_thd.selend.x = x;
		_thd.selend.y = y;
		CalcRaildirsDrawstyle(x, y, method);
		return;
	}

	/* Needed so level-land is placed correctly */
	if ((_thd.next_drawstyle & HT_DRAG_MASK) == HT_POINT) {
		x += TILE_SIZE / 2;
		y += TILE_SIZE / 2;
	}

	sx = _thd.selstart.x;
	sy = _thd.selstart.y;

	int limit = 0;

	switch (method) {
		case VPM_X_OR_Y: // drag in X or Y direction
			if (abs(sy - y) < abs(sx - x)) {
				y = sy;
				style = HT_DIR_X;
			} else {
				x = sx;
				style = HT_DIR_Y;
			}
			goto calc_heightdiff_single_direction;

		case VPM_X_LIMITED: // Drag in X direction (limited size).
			limit = (_thd.sizelimit - 1) * TILE_SIZE;
			FALLTHROUGH;

		case VPM_FIX_X: // drag in Y direction
			x = sx;
			style = HT_DIR_Y;
			goto calc_heightdiff_single_direction;

		case VPM_Y_LIMITED: // Drag in Y direction (limited size).
			limit = (_thd.sizelimit - 1) * TILE_SIZE;
			FALLTHROUGH;

		case VPM_FIX_Y: // drag in X direction
			y = sy;
			style = HT_DIR_X;

calc_heightdiff_single_direction:;
			if (limit > 0) {
				x = sx + Clamp(x - sx, -limit, limit);
				y = sy + Clamp(y - sy, -limit, limit);
			}
			if (_settings_client.gui.measure_tooltip) {
				TileIndex t0 = TileVirtXY(sx, sy);
				TileIndex t1 = TileVirtXY(x, y);
				uint distance = DistanceManhattan(t0, t1) + 1;
				byte index = 0;
				uint64 params[2];

				if (distance != 1) {
					/* With current code passing a HT_LINE style to calculate the height
					 * difference is enough. However if/when a point-tool is created
					 * with this method, function should be called with new_style (below)
					 * instead of HT_LINE | style case HT_POINT is handled specially
					 * new_style := (_thd.next_drawstyle & HT_RECT) ? HT_LINE | style : _thd.next_drawstyle; */
					int heightdiff = CalcHeightdiff(HT_LINE | style, 0, t0, t1);

					params[index++] = distance;
					if (heightdiff != 0) params[index++] = heightdiff;
				}

				ShowMeasurementTooltips(measure_strings_length[index], index, params);
			}
			break;

		case VPM_X_AND_Y_LIMITED: // Drag an X by Y constrained rect area.
			limit = (_thd.sizelimit - 1) * TILE_SIZE;
			x = sx + Clamp(x - sx, -limit, limit);
			y = sy + Clamp(y - sy, -limit, limit);
			FALLTHROUGH;

		case VPM_X_AND_Y: // drag an X by Y area
			if (_settings_client.gui.measure_tooltip) {
				static const StringID measure_strings_area[] = {
					STR_NULL, STR_NULL, STR_MEASURE_AREA, STR_MEASURE_AREA_HEIGHTDIFF
				};

				TileIndex t0 = TileVirtXY(sx, sy);
				TileIndex t1 = TileVirtXY(x, y);
				uint dx = Delta(TileX(t0), TileX(t1)) + 1;
				uint dy = Delta(TileY(t0), TileY(t1)) + 1;
				byte index = 0;
				uint64 params[3];

				/* If dragging an area (eg dynamite tool) and it is actually a single
				 * row/column, change the type to 'line' to get proper calculation for height */
				style = (HighLightStyle)_thd.next_drawstyle;
				if (_thd.IsDraggingDiagonal()) {
					/* Determine the "area" of the diagonal dragged selection.
					 * We assume the area is the number of tiles along the X
					 * edge and the number of tiles along the Y edge. However,
					 * multiplying these two numbers does not give the exact
					 * number of tiles; basically we are counting the black
					 * squares on a chess board and ignore the white ones to
					 * make the tile counts at the edges match up. There is no
					 * other way to make a proper count though.
					 *
					 * First convert to the rotated coordinate system. */
					int dist_x = TileX(t0) - TileX(t1);
					int dist_y = TileY(t0) - TileY(t1);
					int a_max = dist_x + dist_y;
					int b_max = dist_y - dist_x;

					/* Now determine the size along the edge, but due to the
					 * chess board principle this counts double. */
					a_max = abs(a_max + (a_max > 0 ? 2 : -2)) / 2;
					b_max = abs(b_max + (b_max > 0 ? 2 : -2)) / 2;

					/* We get a 1x1 on normal 2x1 rectangles, due to it being
					 * a seen as two sides. As the result for actual building
					 * will be the same as non-diagonal dragging revert to that
					 * behaviour to give it a more normally looking size. */
					if (a_max != 1 || b_max != 1) {
						dx = a_max;
						dy = b_max;
					}
				} else if (style & HT_RECT) {
					if (dx == 1) {
						style = HT_LINE | HT_DIR_Y;
					} else if (dy == 1) {
						style = HT_LINE | HT_DIR_X;
					}
				}

				if (dx != 1 || dy != 1) {
					int heightdiff = CalcHeightdiff(style, 0, t0, t1);

					params[index++] = dx - (style & HT_POINT ? 1 : 0);
					params[index++] = dy - (style & HT_POINT ? 1 : 0);
					if (heightdiff != 0) params[index++] = heightdiff;
				}

				ShowMeasurementTooltips(measure_strings_area[index], index, params);
			}
			break;

		default: NOT_REACHED();
	}

	_thd.selend.x = x;
	_thd.selend.y = y;
}

/**
 * Handle the mouse while dragging for placement/resizing.
 * @return State of handling the event.
 */
EventState VpHandlePlaceSizingDrag()
{
	if (_special_mouse_mode != WSM_SIZING && _special_mouse_mode != WSM_DRAGGING) return ES_NOT_HANDLED;

	/* stop drag mode if the window has been closed */
	Window *w = _thd.GetCallbackWnd();
	if (w == nullptr) {
		ResetObjectToPlace();
		return ES_HANDLED;
	}

	/* while dragging execute the drag procedure of the corresponding window (mostly VpSelectTilesWithMethod() ) */
	if (_left_button_down) {
		if (_special_mouse_mode == WSM_DRAGGING) {
			/* Only register a drag event when the mouse moved. */
			if (_thd.new_pos.x == _thd.selstart.x && _thd.new_pos.y == _thd.selstart.y) return ES_HANDLED;
			_thd.selstart.x = _thd.new_pos.x;
			_thd.selstart.y = _thd.new_pos.y;
		}

		w->OnPlaceDrag(_thd.select_method, _thd.select_proc, GetTileBelowCursor());
		return ES_HANDLED;
	}

	/* Mouse button released. */
	_special_mouse_mode = WSM_NONE;
	if (_special_mouse_mode == WSM_DRAGGING) return ES_HANDLED;

	/* Keep the selected tool, but reset it to the original mode. */
	HighLightStyle others = _thd.place_mode & ~(HT_DRAG_MASK | HT_DIR_MASK);
	if ((_thd.next_drawstyle & HT_DRAG_MASK) == HT_RECT) {
		_thd.place_mode = HT_RECT | others;
	} else if (_thd.select_method & VPM_SIGNALDIRS) {
		_thd.place_mode = HT_RECT | others;
	} else if (_thd.select_method & VPM_RAILDIRS) {
		_thd.place_mode = (_thd.select_method & ~VPM_RAILDIRS) ? _thd.next_drawstyle : (HT_RAIL | others);
	} else {
		_thd.place_mode = HT_POINT | others;
	}
	SetTileSelectSize(1, 1);

	HideMeasurementTooltips();
	w->OnPlaceMouseUp(_thd.select_method, _thd.select_proc, _thd.selend, TileVirtXY(_thd.selstart.x, _thd.selstart.y), TileVirtXY(_thd.selend.x, _thd.selend.y));

	return ES_HANDLED;
}

/**
 * Change the cursor and mouse click/drag handling to a mode for performing special operations like tile area selection, object placement, etc.
 * @param icon New shape of the mouse cursor.
 * @param pal Palette to use.
 * @param mode Mode to perform.
 * @param w %Window requesting the mode change.
 */
void SetObjectToPlaceWnd(CursorID icon, PaletteID pal, HighLightStyle mode, Window *w)
{
	SetObjectToPlace(icon, pal, mode, w->window_class, w->window_number);
}

#include "table/animcursors.h"

/**
 * Change the cursor and mouse click/drag handling to a mode for performing special operations like tile area selection, object placement, etc.
 * @param icon New shape of the mouse cursor.
 * @param pal Palette to use.
 * @param mode Mode to perform.
 * @param window_class %Window class of the window requesting the mode change.
 * @param window_num Number of the window in its class requesting the mode change.
 */
void SetObjectToPlace(CursorID icon, PaletteID pal, HighLightStyle mode, WindowClass window_class, WindowNumber window_num)
{
	if (_thd.window_class != WC_INVALID) {
		/* Undo clicking on button and drag & drop */
		Window *w = _thd.GetCallbackWnd();
		/* Call the abort function, but set the window class to something
		 * that will never be used to avoid infinite loops. Setting it to
		 * the 'next' window class must not be done because recursion into
		 * this function might in some cases reset the newly set object to
		 * place or not properly reset the original selection. */
		_thd.window_class = WC_INVALID;
		if (w != nullptr) {
			w->OnPlaceObjectAbort();
			HideMeasurementTooltips();
		}
	}

	/* Mark the old selection dirty, in case the selection shape or colour changes */
	if ((_thd.drawstyle & HT_DRAG_MASK) != HT_NONE) SetSelectionTilesDirty();

	SetTileSelectSize(1, 1);

	_thd.make_square_red = false;

	if (mode == HT_DRAG) { // HT_DRAG is for dragdropping trains in the depot window
		mode = HT_NONE;
		_special_mouse_mode = WSM_DRAGDROP;
	} else {
		_special_mouse_mode = WSM_NONE;
	}

	_thd.place_mode = mode;
	_thd.window_class = window_class;
	_thd.window_number = window_num;

	if ((mode & HT_DRAG_MASK) == HT_SPECIAL) { // special tools, like tunnels or docks start with presizing mode
		VpStartPreSizing();
	}

	if ((icon & ANIMCURSOR_FLAG) != 0) {
		SetAnimatedMouseCursor(_animcursors[icon & ~ANIMCURSOR_FLAG]);
	} else {
		SetMouseCursor(icon, pal);
	}

}

/** Reset the cursor and mouse mode handling back to default (normal cursor, only clicking in windows). */
void ResetObjectToPlace()
{
	SetObjectToPlace(SPR_CURSOR_MOUSE, PAL_NONE, HT_NONE, WC_MAIN_WINDOW, 0);
}

Point GetViewportStationMiddle(const Viewport *vp, const Station *st)
{
	int x = TileX(st->xy) * TILE_SIZE;
	int y = TileY(st->xy) * TILE_SIZE;
	int z = GetSlopePixelZ(Clamp(x, 0, MapSizeX() * TILE_SIZE - 1), Clamp(y, 0, MapSizeY() * TILE_SIZE - 1));

	Point p = RemapCoords(x, y, z);
	p.x = UnScaleByZoom(p.x - vp->virtual_left, vp->zoom) + vp->left;
	p.y = UnScaleByZoom(p.y - vp->virtual_top, vp->zoom) + vp->top;
	return p;
}

/** Helper class for getting the best sprite sorter. */
struct ViewportSSCSS {
	VpSorterChecker fct_checker; ///< The check function.
	VpSpriteSorter fct_sorter;   ///< The sorting function.
};

/** List of sorters ordered from best to worst. */
static ViewportSSCSS _vp_sprite_sorters[] = {
#ifdef WITH_SSE
	{ &ViewportSortParentSpritesSSE41Checker, &ViewportSortParentSpritesSSE41 },
#endif
	{ &ViewportSortParentSpritesChecker, &ViewportSortParentSprites }
};

/** Choose the "best" sprite sorter and set _vp_sprite_sorter. */
void InitializeSpriteSorter()
{
	for (uint i = 0; i < lengthof(_vp_sprite_sorters); i++) {
		if (_vp_sprite_sorters[i].fct_checker()) {
			_vp_sprite_sorter = _vp_sprite_sorters[i].fct_sorter;
			break;
		}
	}
	assert(_vp_sprite_sorter != nullptr);
}

/**
 * Scroll players main viewport.
 * @param tile tile to center viewport on
 * @param flags type of operation
 * @param p1 ViewportScrollTarget of scroll target
 * @param p2 company or client id depending on the target
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdScrollViewport(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const std::string &text)
{
	if (_current_company != OWNER_DEITY) return CMD_ERROR;
	ViewportScrollTarget target = (ViewportScrollTarget)p1;
	switch (target) {
		case VST_EVERYONE:
			break;
		case VST_COMPANY:
			if (_local_company != (CompanyID)p2) return CommandCost();
			break;
		case VST_CLIENT:
			if (_network_own_client_id != (ClientID)p2) return CommandCost();
			break;
		default:
			return CMD_ERROR;
	}

	if (flags & DC_EXEC) {
		ResetObjectToPlace();
		ScrollMainWindowToTile(tile);
	}
	return CommandCost();
}

static void MarkCatchmentTilesDirty()
{
	if (_viewport_highlight_town != nullptr) {
		MarkWholeScreenDirty();
		return;
	}
	if (_viewport_highlight_station != nullptr) {
		if (_viewport_highlight_station->catchment_tiles.tile == INVALID_TILE) {
			MarkWholeScreenDirty();
			_viewport_highlight_station = nullptr;
		} else {
			BitmapTileIterator it(_viewport_highlight_station->catchment_tiles);
			for (TileIndex tile = it; tile != INVALID_TILE; tile = ++it) {
				MarkTileDirtyByTile(tile);
			}
		}
	}
}

/**
 * Select or deselect station for coverage area highlight.
 * Selecting a station will deselect a town.
 * @param *st Station in question
 * @param sel Select or deselect given station
 */
void SetViewportCatchmentStation(const Station *st, bool sel)
{
	if (_viewport_highlight_station != nullptr) SetWindowDirty(WC_STATION_VIEW, _viewport_highlight_station->index);
	if (_viewport_highlight_town != nullptr) SetWindowDirty(WC_TOWN_VIEW, _viewport_highlight_town->index);
	if (sel && _viewport_highlight_station != st) {
		MarkCatchmentTilesDirty();
		_viewport_highlight_station = st;
		_viewport_highlight_town = nullptr;
		MarkCatchmentTilesDirty();
	} else if (!sel && _viewport_highlight_station == st) {
		MarkCatchmentTilesDirty();
		_viewport_highlight_station = nullptr;
	}
	if (_viewport_highlight_station != nullptr) SetWindowDirty(WC_STATION_VIEW, _viewport_highlight_station->index);
}

/**
 * Select or deselect town for coverage area highlight.
 * Selecting a town will deselect a station.
 * @param *t Town in question
 * @param sel Select or deselect given town
 */
void SetViewportCatchmentTown(const Town *t, bool sel)
{
	if (_viewport_highlight_town != nullptr) SetWindowDirty(WC_TOWN_VIEW, _viewport_highlight_town->index);
	if (_viewport_highlight_station != nullptr) SetWindowDirty(WC_STATION_VIEW, _viewport_highlight_station->index);
	if (sel && _viewport_highlight_town != t) {
		_viewport_highlight_station = nullptr;
		_viewport_highlight_town = t;
		MarkWholeScreenDirty();
	} else if (!sel && _viewport_highlight_town == t) {
		_viewport_highlight_town = nullptr;
		MarkWholeScreenDirty();
	}
	if (_viewport_highlight_town != nullptr) SetWindowDirty(WC_TOWN_VIEW, _viewport_highlight_town->index);
}

static void MarkDepotTilesDirty()
{
	if (_viewport_highlight_depot != INVALID_DEPOT) {
		MarkWholeScreenDirty();
		return;
	}
}

/**
 * Select or deselect depot to highlight.
 * @param *dep Depot in question
 * @param sel Select or deselect given depot
 */
void SetViewportHighlightDepot(const DepotID dep, bool sel)
{
	if (_viewport_highlight_depot != INVALID_DEPOT) SetWindowDirty(WC_VEHICLE_DEPOT, _viewport_highlight_depot);
	if (sel && _viewport_highlight_depot != dep) {
		MarkDepotTilesDirty();
		_viewport_highlight_depot = dep;
		MarkDepotTilesDirty();
	} else if (!sel && _viewport_highlight_depot == dep) {
		MarkDepotTilesDirty();
		_viewport_highlight_depot = INVALID_DEPOT;
	}
	if (_viewport_highlight_depot != INVALID_DEPOT) SetWindowDirty(WC_VEHICLE_DEPOT, _viewport_highlight_depot);
}
