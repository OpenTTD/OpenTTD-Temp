/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file viewport_func.h Functions related to (drawing on) viewports. */

#ifndef VIEWPORT_FUNC_H
#define VIEWPORT_FUNC_H

#include "gfx_type.h"
#include "viewport_type.h"
#include "window_type.h"
#include "tile_map.h"
#include "station_type.h"

static const int TILE_HEIGHT_STEP = 50; ///< One Z unit tile height difference is displayed as 50m.

void SetSelectionRed(bool);

void DeleteWindowViewport(Window *w);
void InitializeWindowViewport(Window *w, int x, int y, int width, int height, uint32 follow_flags, ZoomLevel zoom);
Viewport *IsPtInWindowViewport(const Window *w, int x, int y);
Point TranslateXYToTileCoord(const Viewport *vp, int x, int y, bool clamp_to_map = true);
Point GetTileBelowCursor();
void UpdateViewportPosition(Window *w);

bool MarkAllViewportsDirty(int left, int top, int right, int bottom);

bool DoZoomInOutWindow(ZoomStateChange how, Window *w);
void ZoomInOrOutToCursorWindow(bool in, Window * w);
Point GetTileZoomCenterWindow(bool in, Window * w);
void FixTitleGameZoom(int zoom_adjust = 0);
void HandleZoomMessage(Window *w, const Viewport *vp, byte widget_zoom_in, byte widget_zoom_out);

/**
 * Zoom a viewport as far as possible in the given direction.
 * @param how Zooming direction.
 * @param w   Window owning the viewport.
 * @pre \a how should not be #ZOOM_NONE.
 */
static inline void MaxZoomInOut(ZoomStateChange how, Window *w)
{
	while (DoZoomInOutWindow(how, w)) {};
}

void OffsetGroundSprite(int x, int y);

void DrawGroundSprite(SpriteID image, PaletteID pal, const SubSprite *sub = nullptr, int extra_offs_x = 0, int extra_offs_y = 0);
void DrawGroundSpriteAt(SpriteID image, PaletteID pal, int32 x, int32 y, int z, const SubSprite *sub = nullptr, int extra_offs_x = 0, int extra_offs_y = 0);
void AddSortableSpriteToDraw(SpriteID image, PaletteID pal, int x, int y, int w, int h, int dz, int z, bool transparent = false, int bb_offset_x = 0, int bb_offset_y = 0, int bb_offset_z = 0, const SubSprite *sub = nullptr);
void AddChildSpriteScreen(SpriteID image, PaletteID pal, int x, int y, bool transparent = false, const SubSprite *sub = nullptr, bool scale = true);
void ViewportAddString(const DrawPixelInfo *dpi, ZoomLevel small_from, const ViewportSign *sign, StringID string_normal, StringID string_small, StringID string_small_shadow, uint64 params_1, uint64 params_2 = 0, Colours colour = INVALID_COLOUR);


void StartSpriteCombine();
void EndSpriteCombine();

bool HandleViewportClicked(const Viewport *vp, int x, int y);
void SetRedErrorSquare(TileIndex tile);
void SetTileSelectSize(int w, int h);
void SetTileSelectBigSize(int ox, int oy, int sx, int sy);

void ViewportDoDraw(const Viewport *vp, int left, int top, int right, int bottom);

bool ScrollWindowToTile(TileIndex tile, Window *w, bool instant = false);
bool ScrollWindowTo(int x, int y, int z, Window *w, bool instant = false);

void RebuildViewportOverlay(Window *w);

bool ScrollMainWindowToTile(TileIndex tile, bool instant = false);
bool ScrollMainWindowTo(int x, int y, int z = -1, bool instant = false);

void UpdateAllVirtCoords();
void ClearAllCachedNames();

extern Point _tile_fract_coords;

void MarkTileDirtyByTile(TileIndex tile, int bridge_level_offset, int tile_height_override);

/**
 * Mark a tile given by its index dirty for repaint.
 * @param tile The tile to mark dirty.
 * @param bridge_level_offset Height of bridge on tile to also mark dirty. (Height level relative to north corner.)
 * @ingroup dirty
 */
static inline void MarkTileDirtyByTile(TileIndex tile, int bridge_level_offset = 0)
{
	MarkTileDirtyByTile(tile, bridge_level_offset, TileHeight(tile));
}

Point GetViewportStationMiddle(const Viewport *vp, const Station *st);

struct Station;
struct Town;

void SetViewportCatchmentStation(const Station *st, bool sel);
void SetViewportCatchmentTown(const Town *t, bool sel);

#endif /* VIEWPORT_FUNC_H */
