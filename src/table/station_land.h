/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file station_land.h Sprites to use and how to display them for station tiles. */

/**
 * Constructor macro for an image without a palette in a DrawTileSeqStruct array.
 * @param dx  Offset in x direction
 * @param dy  Offset in y direction
 * @param dz  Offset in z direction
 * @param sx  Size in x direction
 * @param sy  Size in y direction
 * @param sz  Size in z direction
 * @param img Sprite to draw
 */
#define TILE_SEQ_LINE(dx, dy, dz, sx, sy, sz, img) TILE_SEQ_LINE_PAL(dx, dy, dz, sx, sy, sz, img, PAL_NONE)

/**
 * Constructor macro for an image with a palette in a DrawTileSeqStruct array.
 * @param dx  Offset in x direction
 * @param dy  Offset in y direction
 * @param dz  Offset in z direction
 * @param sx  Size in x direction
 * @param sy  Size in y direction
 * @param sz  Size in z direction
 * @param img Sprite to draw
 * @param pal Palette sprite
 */
#define TILE_SEQ_LINE_PAL(dx, dy, dz, sx, sy, sz, img, pal) { dx, dy, dz, sx, sy, sz, {img, pal} },

/**
 * Constructor macro for an image without bounding box.
 * @param dx  Screen X offset from parent sprite
 * @param dy  Screen Y offset from parent sprite
 * @param img Sprite to draw
 * @param pal Palette sprite
 */
#define TILE_SEQ_CHILD(dx, dy, img, pal) TILE_SEQ_LINE_PAL(dx, dy, (int8)0x80, 0, 0, 0, img, pal)

/**
 * Constructor macro for additional ground sprites.
 * These need to be at the front of a DrawTileSeqStruct sequence.
 * @param dx  Offset in x direction
 * @param dy  Offset in y direction
 * @param dz  Offset in z direction
 * @param img Sprite to draw
 */
#define TILE_SEQ_GROUND(dx, dy, dz, img) TILE_SEQ_CHILD(2 * (dy - dx), dx + dy - dz, img, PAL_NONE)

/** Constructor macro for a terminating DrawTileSeqStruct entry in an array */
#define TILE_SEQ_END() { (int8)0x80, 0, 0, 0, 0, 0, {0, 0} }

static const DrawTileSeqStruct _station_display_nothing[] = {
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_datas_0[] = {
	TILE_SEQ_LINE( 0,  0,  0, 16,  5,  2, SPR_RAIL_PLATFORM_X_REAR  | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE( 0, 11,  0, 16,  5,  2, SPR_RAIL_PLATFORM_X_FRONT | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_datas_1[] = {
	TILE_SEQ_LINE( 0,  0,  0,  5, 16,  2, SPR_RAIL_PLATFORM_Y_REAR  | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE(11,  0,  0,  5, 16,  2, SPR_RAIL_PLATFORM_Y_FRONT | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_datas_2[] = {
	TILE_SEQ_LINE( 0,  0,  0, 16,  5,  2, SPR_RAIL_PLATFORM_BUILDING_X | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE( 0, 11,  0, 16,  5,  2, SPR_RAIL_PLATFORM_X_FRONT    | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_datas_3[] = {
	TILE_SEQ_LINE( 0,  0,  0,  5, 16,  2, SPR_RAIL_PLATFORM_BUILDING_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE(11,  0,  0,  5, 16,  2, SPR_RAIL_PLATFORM_Y_FRONT    | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_datas_4[] = {
	TILE_SEQ_LINE( 0,  0,  0, 16,  5,  7, SPR_RAIL_PLATFORM_PILLARS_X_REAR | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE( 0, 11,  0, 16,  5,  2, SPR_RAIL_PLATFORM_X_FRONT        | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE( 0,  0, 16, 16, 16, 10, SPR_RAIL_ROOF_STRUCTURE_X_TILE_A | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_CHILD( 0,  0,                SPR_RAIL_ROOF_GLASS_X_TILE_A     | (1U << PALETTE_MODIFIER_TRANSPARENT), PALETTE_TO_TRANSPARENT)
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_datas_5[] = {
	TILE_SEQ_LINE( 0,  0,  0,  5, 16,  2, SPR_RAIL_PLATFORM_PILLARS_Y_REAR | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE(11,  0,  0,  5, 16,  2, SPR_RAIL_PLATFORM_Y_FRONT        | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE( 0,  0, 16, 16, 16, 10, SPR_RAIL_ROOF_STRUCTURE_Y_TILE_A | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_CHILD( 0,  0,                SPR_RAIL_ROOF_GLASS_Y_TILE_A     | (1U << PALETTE_MODIFIER_TRANSPARENT), PALETTE_TO_TRANSPARENT)
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_datas_6[] = {
	TILE_SEQ_LINE( 0,  0,  0, 16,  5,  2, SPR_RAIL_PLATFORM_X_REAR          | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE( 0, 11,  0, 16,  5,  2, SPR_RAIL_PLATFORM_PILLARS_X_FRONT | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE( 0,  0, 16, 16, 16, 10, SPR_RAIL_ROOF_STRUCTURE_X_TILE_B  | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_CHILD( 0,  0,                SPR_RAIL_ROOF_GLASS_X_TILE_B      | (1U << PALETTE_MODIFIER_TRANSPARENT), PALETTE_TO_TRANSPARENT)
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_datas_7[] = {
	TILE_SEQ_LINE( 0,  0,  0,  5, 16,  2, SPR_RAIL_PLATFORM_Y_REAR          | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE(11,  0,  0,  5, 16,  2, SPR_RAIL_PLATFORM_PILLARS_Y_FRONT | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE( 0,  0, 16, 16, 16, 10, SPR_RAIL_ROOF_STRUCTURE_Y_TILE_B  | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_CHILD( 0,  0,                SPR_RAIL_ROOF_GLASS_Y_TILE_B      | (1U << PALETTE_MODIFIER_TRANSPARENT), PALETTE_TO_TRANSPARENT)
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_fence_nw[] = {
	TILE_SEQ_GROUND( 0,  0,  0, SPR_AIRPORT_FENCE_X | (1U << PALETTE_MODIFIER_COLOUR)) // fences north
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_fence_ne[] = {
	TILE_SEQ_GROUND( 0,  0,  0, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_fence_sw[] = {
	TILE_SEQ_GROUND(15,  0,  0, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR)) // fences west
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_fence_se[] = {
	TILE_SEQ_GROUND( 0, 15,  0, SPR_AIRPORT_FENCE_X | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_terminal_a[] = {
	TILE_SEQ_LINE( 2,  0,  0, 11, 16, 40, SPR_AIRPORT_TERMINAL_A | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_tower_fence_sw[] = {
	TILE_SEQ_LINE( 3,  3,  0, 10, 10, 60, SPR_AIRPORT_TOWER | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE(15,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_concourse[] = {
	TILE_SEQ_LINE( 0,  1,  0, 14, 14, 30, SPR_AIRPORT_CONCOURSE | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_datas_terminal_b[] = {
	TILE_SEQ_LINE( 3,  3,  0, 10, 11, 35, SPR_AIRPORT_TERMINAL_B | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_datas_terminal_c[] = {
	TILE_SEQ_LINE( 0,  3,  0, 16, 11, 40, SPR_AIRPORT_TERMINAL_C | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_hangar_se[] = {
	TILE_SEQ_LINE(14,  0,  0,  2, 17, 28, SPR_AIRPORT_HANGAR_FRONT | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE( 0,  0,  0,  2, 17, 28, SPR_AIRPORT_HANGAR_REAR | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_jetway_1[] = {
	TILE_SEQ_LINE( 7, 11,  0,  3,  3, 14, SPR_AIRPORT_JETWAY_1 | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE( 0,  0,  0, 16,  1,  6, SPR_AIRPORT_FENCE_X | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_jetway_2[] = {
	TILE_SEQ_LINE( 2,  7,  0,  3,  3, 14, SPR_AIRPORT_JETWAY_2 | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_jetway_3[] = {
	TILE_SEQ_LINE( 3,  2,  0,  3,  3, 14, SPR_AIRPORT_JETWAY_3 | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_passenger_tunnel[] = {
	TILE_SEQ_LINE( 0,  8,  0, 14,  3, 14, SPR_AIRPORT_PASSENGER_TUNNEL | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_1_fence_sw[] = {
	TILE_SEQ_LINE( 7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_1)
	TILE_SEQ_LINE(15,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_2_fence_sw[] = {
	TILE_SEQ_LINE( 7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_2)
	TILE_SEQ_LINE(15,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_3_fence_sw[] = {
	TILE_SEQ_LINE( 7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_3)
	TILE_SEQ_LINE(15,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_4_fence_sw[] = {
	TILE_SEQ_LINE( 7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_4)
	TILE_SEQ_LINE(15,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_5_fence_sw[] = {
	TILE_SEQ_LINE( 7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_5)
	TILE_SEQ_LINE(15,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_6_fence_sw[] = {
	TILE_SEQ_LINE( 7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_6)
	TILE_SEQ_LINE(15,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_7_fence_sw[] = {
	TILE_SEQ_LINE( 7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_7)
	TILE_SEQ_LINE(15,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_8_fence_sw[] = {
	TILE_SEQ_LINE( 7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_8)
	TILE_SEQ_LINE(15,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_9_fence_sw[] = {
	TILE_SEQ_LINE( 7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_9)
	TILE_SEQ_LINE(15,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_10_fence_sw[] = {
	TILE_SEQ_LINE( 7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_A)
	TILE_SEQ_LINE(15,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_11_fence_sw[] = {
	TILE_SEQ_LINE( 7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_B)
	TILE_SEQ_LINE(15,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_12_fence_sw[] = {
	TILE_SEQ_LINE( 7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_C)
	TILE_SEQ_LINE(15,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_transmitter_fence_ne[] = {
	TILE_SEQ_LINE( 7,  7,  0,  2,  2, 70, SPR_TRANSMITTER)
	TILE_SEQ_LINE( 0,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_datas_terminal_c_2[] = {
	TILE_SEQ_LINE( 0,  0,  0, 15, 15, 30, SPR_AIRFIELD_TERM_C_BUILD | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_flag_1_fence_ne[] = {
	TILE_SEQ_LINE( 0,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE( 4, 11,  0,  1,  1, 20, SPR_AIRFIELD_WIND_1 | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_flag_2_fence_ne[] = {
	TILE_SEQ_LINE( 0,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE( 4, 11,  0,  1,  1, 20, SPR_AIRFIELD_WIND_2 | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_flag_3_fence_ne[] = {
	TILE_SEQ_LINE( 0,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE( 4, 11,  0,  1,  1, 20, SPR_AIRFIELD_WIND_3 | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_flag_4_fence_ne[] = {
	TILE_SEQ_LINE( 0,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE( 4, 11,  0,  1,  1, 20, SPR_AIRFIELD_WIND_4 | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_small_depot_se[] = {
	TILE_SEQ_LINE(14,  0,  0,  2, 17, 28, SPR_AIRFIELD_HANGAR_FRONT | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE( 0,  0,  0,  2, 17, 28, SPR_AIRFIELD_HANGAR_REAR | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_heliport[] = {
	TILE_SEQ_LINE( 0,  0,  0, 16, 16, 60, SPR_HELIPORT | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_datas_67[] = {
	TILE_SEQ_LINE( 0, 15,  0, 13,  1, 10, SPR_TRUCK_STOP_NE_BUILD_A | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE(13,  0,  0,  3, 16, 10, SPR_TRUCK_STOP_NE_BUILD_B | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE( 2,  0,  0, 11,  1, 10, SPR_TRUCK_STOP_NE_BUILD_C | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_datas_68[] = {
	TILE_SEQ_LINE(15,  3,  0,  1, 13, 10, SPR_TRUCK_STOP_SE_BUILD_A | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE( 0,  0,  0, 16,  3, 10, SPR_TRUCK_STOP_SE_BUILD_B | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE( 0,  3,  0,  1, 11, 10, SPR_TRUCK_STOP_SE_BUILD_C | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_datas_69[] = {
	TILE_SEQ_LINE( 3,  0,  0, 13,  1, 10, SPR_TRUCK_STOP_SW_BUILD_A | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE( 0,  0,  0,  3, 16, 10, SPR_TRUCK_STOP_SW_BUILD_B | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE( 3, 15,  0, 11,  1, 10, SPR_TRUCK_STOP_SW_BUILD_C | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_datas_70[] = {
	TILE_SEQ_LINE( 0,  0,  0,  1, 13, 10, SPR_TRUCK_STOP_NW_BUILD_A | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE( 0, 13,  0, 16,  3, 10, SPR_TRUCK_STOP_NW_BUILD_B | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE(15,  2,  0,  1, 11, 10, SPR_TRUCK_STOP_NW_BUILD_C | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_datas_71[] = {
	TILE_SEQ_LINE( 2,  0,  0, 11,  1, 10, SPR_BUS_STOP_NE_BUILD_A | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE(13,  0,  0,  3, 16, 10, SPR_BUS_STOP_NE_BUILD_B | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE( 0, 13,  0, 13,  3, 10, SPR_BUS_STOP_NE_BUILD_C | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_datas_72[] = {
	TILE_SEQ_LINE( 0,  3,  0,  1, 11, 10, SPR_BUS_STOP_SE_BUILD_A | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE( 0,  0,  0, 16,  3, 10, SPR_BUS_STOP_SE_BUILD_B | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE(13,  3,  0,  3, 13, 10, SPR_BUS_STOP_SE_BUILD_C | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_datas_73[] = {
	TILE_SEQ_LINE( 3, 15,  0, 11,  1, 10, SPR_BUS_STOP_SW_BUILD_A | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE( 0,  0,  0,  3, 16, 10, SPR_BUS_STOP_SW_BUILD_B | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE( 3,  0,  0, 13,  3, 10, SPR_BUS_STOP_SW_BUILD_C | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_datas_74[] = {
	TILE_SEQ_LINE(15,  2,  0,  1, 11, 10, SPR_BUS_STOP_NW_BUILD_A | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE( 0, 13,  0, 16,  3, 10, SPR_BUS_STOP_NW_BUILD_B | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE( 0,  0,  0,  3, 13, 10, SPR_BUS_STOP_NW_BUILD_C | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_datas_76[] = {
	TILE_SEQ_LINE( 0,  4,  0, 16,  8,  8, SPR_DOCK_SLOPE_NE | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_datas_77[] = {
	TILE_SEQ_LINE( 4,  0,  0,  8, 16,  8, SPR_DOCK_SLOPE_SE | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_datas_78[] = {
	TILE_SEQ_LINE( 0,  4,  0, 16,  8,  8, SPR_DOCK_SLOPE_SW | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_datas_79[] = {
	TILE_SEQ_LINE( 4,  0,  0,  8, 16,  8, SPR_DOCK_SLOPE_NW | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_datas_80[] = {
	TILE_SEQ_LINE( 0,  4,  0, 16,  8,  8, SPR_DOCK_FLAT_X | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_datas_81[] = {
	TILE_SEQ_LINE( 4,  0,  0,  8, 16,  8, SPR_DOCK_FLAT_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

/* Buoy, which will _always_ drown under the ship */
static const DrawTileSeqStruct _station_display_datas_82[] = {
	TILE_SEQ_LINE( 4,  -1,  0,  0,  0,  0, SPR_IMG_BUOY)
	TILE_SEQ_END()
};

/* control tower without fence */
static const DrawTileSeqStruct _station_display_tower[] = {
	TILE_SEQ_LINE( 3,  3,  0, 10, 10, 60, SPR_AIRPORT_TOWER | (1U << PALETTE_MODIFIER_COLOUR))  // control tower
	TILE_SEQ_END()
};

/* turning radar with fences on north -- needs 12 tiles
 *BEGIN */
static const DrawTileSeqStruct _station_display_radar_1_fence_ne[] = {
	TILE_SEQ_LINE(7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_1)   // turning radar
	TILE_SEQ_LINE(0,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_2_fence_ne[] = {
	TILE_SEQ_LINE(7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_2)
	TILE_SEQ_LINE(0,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_3_fence_ne[] = {
	TILE_SEQ_LINE(7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_3)
	TILE_SEQ_LINE(0,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_4_fence_ne[] = {
	TILE_SEQ_LINE(7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_4)
	TILE_SEQ_LINE(0,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_5_fence_ne[] = {
	TILE_SEQ_LINE(7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_5)
	TILE_SEQ_LINE(0,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_6_fence_ne[] = {
	TILE_SEQ_LINE(7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_6)
	TILE_SEQ_LINE(0,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_7_fence_ne[] = {
	TILE_SEQ_LINE(7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_7)
	TILE_SEQ_LINE(0,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_8_fence_ne[] = {
	TILE_SEQ_LINE(7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_8)
	TILE_SEQ_LINE(0,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_9_fence_ne[] = {
	TILE_SEQ_LINE(7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_9)
	TILE_SEQ_LINE(0,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_10_fence_ne[] = {
	TILE_SEQ_LINE(7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_A)
	TILE_SEQ_LINE(0,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_11_fence_ne[] = {
	TILE_SEQ_LINE(7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_B)
	TILE_SEQ_LINE(0,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_12_fence_ne[] = {
	TILE_SEQ_LINE(7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_C)
	TILE_SEQ_LINE(0,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};
/* END */

/* helipad for international airport */
static const DrawTileSeqStruct _station_display_helipad_fence_sw[] = {
	TILE_SEQ_LINE(10,  6,  0,  0,  0,  0, SPR_AIRPORT_HELIPAD)
	TILE_SEQ_LINE(15,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR)) // fences bottom
	TILE_SEQ_END()
};

/* helipad for commuter airport */
static const DrawTileSeqStruct _station_display_helipad_fence_nw[] = {
	TILE_SEQ_LINE(10,  6,  0,  0,  0,  0, SPR_AIRPORT_HELIPAD)
	TILE_SEQ_LINE( 0,  0,  0, 16,  1,  6, SPR_AIRPORT_FENCE_X | (1U << PALETTE_MODIFIER_COLOUR)) // fences left
	TILE_SEQ_END()
};

/* helipad for continental airport */
static const DrawTileSeqStruct _station_display_helipad[] = {
	TILE_SEQ_LINE(10,  6,  0,  0,  0,  0, SPR_AIRPORT_HELIPAD)
	TILE_SEQ_END()
};

/* asphalt tile with fences in north and south */
static const DrawTileSeqStruct _station_display_fence_ne_sw[] = {
	TILE_SEQ_GROUND( 0,  0,  0,  SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_GROUND(15,  0,  0,  SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

/* runway tiles with 2 corner fences */
static const DrawTileSeqStruct _station_display_fence_nw_sw[] = {
	TILE_SEQ_GROUND( 0,  0,  0, SPR_AIRPORT_FENCE_X | (1U << PALETTE_MODIFIER_COLOUR)) // fences north
	TILE_SEQ_GROUND(15,  0,  0, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR)) // fences west
	TILE_SEQ_END()
};

/* runway tiles with 2 corner fences */
static const DrawTileSeqStruct _station_display_fence_se_sw[] = {
	TILE_SEQ_GROUND(15,  0,  0, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR)) // fences west
	TILE_SEQ_GROUND( 0, 15,  0, SPR_AIRPORT_FENCE_X | (1U << PALETTE_MODIFIER_COLOUR)) // fences south
	TILE_SEQ_END()
};

/* runway tiles with 2 corner fences */
static const DrawTileSeqStruct _station_display_fence_ne_nw[] = {
	TILE_SEQ_GROUND( 0,  0,  0, SPR_AIRPORT_FENCE_X | (1U << PALETTE_MODIFIER_COLOUR)) // fences north
	TILE_SEQ_GROUND( 0,  0,  0, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR)) // fences east
	TILE_SEQ_END()
};

/* runway tiles with 2 corner fences */
static const DrawTileSeqStruct _station_display_fence_ne_se[] = {
	TILE_SEQ_GROUND( 0,  0,  0, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR)) // fences east
	TILE_SEQ_GROUND( 0, 15,  0, SPR_AIRPORT_FENCE_X | (1U << PALETTE_MODIFIER_COLOUR)) // fences south
	TILE_SEQ_END()
};

/* helipad tiles with 2 corner fences top+right */
static const DrawTileSeqStruct _station_display_helipad_fence_NE_SE[] = {
	TILE_SEQ_LINE(10,  6,  0,  0,  0,  0, SPR_AIRPORT_HELIPAD)
	TILE_SEQ_LINE( 0,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR)) // fences east
	TILE_SEQ_LINE( 0, 15,  0, 16,  1,  6, SPR_AIRPORT_FENCE_X | (1U << PALETTE_MODIFIER_COLOUR)) // fences south
	TILE_SEQ_END()
};

/* helidepot office with fences top + left */
static const DrawTileSeqStruct _station_display_low_building_fence_ne_nw[] = {
	TILE_SEQ_LINE( 0,  0,  0, 16,  1,  6, SPR_AIRPORT_FENCE_X | (1U << PALETTE_MODIFIER_COLOUR)) // fences left
	TILE_SEQ_LINE( 0,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR)) // fences east
	TILE_SEQ_LINE( 3,  3,  0, 10, 10, 60, SPR_AIRPORT_HELIDEPOT_OFFICE | (1U << PALETTE_MODIFIER_COLOUR))  // helidepot office
	TILE_SEQ_END()
};

/* West facing hangar */
static const DrawTileSeqStruct _station_display_hangar_sw[] = {
	TILE_SEQ_LINE(14,  0,  0,  2, 17, 28, SPR_NEWHANGAR_W | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE( 0,  0,  0,  2, 17, 28, SPR_NEWHANGAR_W_WALL | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

/* North facing hangar */
static const DrawTileSeqStruct _station_display_hangar_nw[] = {
	TILE_SEQ_LINE(14,  0,  0,  2, 16, 28, SPR_NEWHANGAR_N | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

/* East facing hangar */
static const DrawTileSeqStruct _station_display_hangar_ne[] = {
	TILE_SEQ_LINE(14,  0,  0,  2, 16, 28, SPR_NEWHANGAR_E | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

/* helipad for district airport NS */
static const DrawTileSeqStruct _station_display_helipad_fence_se_sw[] = {
	TILE_SEQ_LINE(10,  6,  0,  0,  0,  0, SPR_AIRPORT_HELIPAD)
	TILE_SEQ_LINE(15,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR)) // fences bottom
	TILE_SEQ_LINE( 0, 15,  0, 16,  1,  6, SPR_AIRPORT_FENCE_X | (1U << PALETTE_MODIFIER_COLOUR)) // fences right
	TILE_SEQ_END()
};

/* helipad for district airport NS */
static const DrawTileSeqStruct _station_display_helipad_fence_se[] = {
	TILE_SEQ_LINE(10,  6,  0,  0,  0,  0, SPR_AIRPORT_HELIPAD)
	TILE_SEQ_LINE( 0, 15,  0, 16,  1,  6, SPR_AIRPORT_FENCE_X | (1U << PALETTE_MODIFIER_COLOUR)) // fences south
	TILE_SEQ_END()
};

/* helidepot office with fence north */
static const DrawTileSeqStruct _station_display_low_building_fence_nw[] = {
	TILE_SEQ_LINE( 0,  0,  0, 16,  1,  6, SPR_AIRPORT_FENCE_X | (1U << PALETTE_MODIFIER_COLOUR)) // fences north
	TILE_SEQ_LINE( 3,  3,  0, 10, 10, 60, SPR_AIRPORT_HELIDEPOT_OFFICE | (1U << PALETTE_MODIFIER_COLOUR))  // helidepot office
	TILE_SEQ_END()
};

/* helidepot office with fence east */
static const DrawTileSeqStruct _station_display_low_building_fence_ne[] = {
	TILE_SEQ_LINE( 0,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR)) // fences east
	TILE_SEQ_LINE( 3,  3,  0, 10, 10, 60, SPR_AIRPORT_HELIDEPOT_OFFICE | (1U << PALETTE_MODIFIER_COLOUR))  // helidepot office
	TILE_SEQ_END()
};

/* helidepot office with fence west */
static const DrawTileSeqStruct _station_display_low_building_fence_sw[] = {
	TILE_SEQ_LINE(15,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR)) // fences west
	TILE_SEQ_LINE( 3,  3,  0, 10, 10, 60, SPR_AIRPORT_HELIDEPOT_OFFICE | (1U << PALETTE_MODIFIER_COLOUR))  // helidepot office
	TILE_SEQ_END()
};

/* helidepot office with fence south */
static const DrawTileSeqStruct _station_display_low_building_fence_se[] = {
	TILE_SEQ_LINE( 0, 15,  0, 16,  1,  6, SPR_AIRPORT_FENCE_X | (1U << PALETTE_MODIFIER_COLOUR)) // fences south
	TILE_SEQ_LINE( 3,  3,  0, 10, 10, 60, SPR_AIRPORT_HELIDEPOT_OFFICE | (1U << PALETTE_MODIFIER_COLOUR))  // helidepot office
	TILE_SEQ_END()
};

/* helipad for district airport EW*/
static const DrawTileSeqStruct _station_display_helipad_fence_nw_sw[] = {
	TILE_SEQ_LINE(10,  6,  0,  0,  0,  0, SPR_AIRPORT_HELIPAD)
	TILE_SEQ_LINE(15,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR)) // fences west
	TILE_SEQ_LINE( 0,  0,  0, 16,  1,  6, SPR_AIRPORT_FENCE_X | (1U << PALETTE_MODIFIER_COLOUR)) // fences north
	TILE_SEQ_END()
};

/* turning radar with fences on south -- needs 12 tiles
 *BEGIN */
static const DrawTileSeqStruct _station_display_radar_1_fence_se[] = {
	TILE_SEQ_LINE( 7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_1)   // turning radar
	TILE_SEQ_LINE( 0, 15,  0, 16,  1,  6, SPR_AIRPORT_FENCE_X | (1U << PALETTE_MODIFIER_COLOUR)) // fences south
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_2_fence_se[] = {
	TILE_SEQ_LINE( 7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_2)
	TILE_SEQ_LINE( 0, 15,  0, 16,  1,  6, SPR_AIRPORT_FENCE_X | (1U << PALETTE_MODIFIER_COLOUR)) // fences south
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_3_fence_se[] = {
	TILE_SEQ_LINE( 7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_3)
	TILE_SEQ_LINE( 0, 15,  0, 16,  1,  6, SPR_AIRPORT_FENCE_X | (1U << PALETTE_MODIFIER_COLOUR)) // fences south
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_4_fence_se[] = {
	TILE_SEQ_LINE( 7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_4)
	TILE_SEQ_LINE( 0, 15,  0, 16,  1,  6, SPR_AIRPORT_FENCE_X | (1U << PALETTE_MODIFIER_COLOUR)) // fences south
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_5_fence_se[] = {
	TILE_SEQ_LINE( 7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_5)
	TILE_SEQ_LINE( 0, 15,  0, 16,  1,  6, SPR_AIRPORT_FENCE_X | (1U << PALETTE_MODIFIER_COLOUR)) // fences south
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_6_fence_se[] = {
	TILE_SEQ_LINE( 7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_6)
	TILE_SEQ_LINE( 0, 15,  0, 16,  1,  6, SPR_AIRPORT_FENCE_X | (1U << PALETTE_MODIFIER_COLOUR)) // fences south
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_7_fence_se[] = {
	TILE_SEQ_LINE( 7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_7)
	TILE_SEQ_LINE( 0, 15,  0, 16,  1,  6, SPR_AIRPORT_FENCE_X | (1U << PALETTE_MODIFIER_COLOUR)) // fences south
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_8_fence_se[] = {
	TILE_SEQ_LINE( 7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_8)
	TILE_SEQ_LINE( 0, 15,  0, 16,  1,  6, SPR_AIRPORT_FENCE_X | (1U << PALETTE_MODIFIER_COLOUR)) // fences south
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_9_fence_se[] = {
	TILE_SEQ_LINE( 7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_9)
	TILE_SEQ_LINE( 0, 15,  0, 16,  1,  6, SPR_AIRPORT_FENCE_X | (1U << PALETTE_MODIFIER_COLOUR)) // fences south
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_10_fence_se[] = {
	TILE_SEQ_LINE( 7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_A)
	TILE_SEQ_LINE( 0, 15,  0, 16,  1,  6, SPR_AIRPORT_FENCE_X | (1U << PALETTE_MODIFIER_COLOUR)) // fences south
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_11_fence_se[] = {
	TILE_SEQ_LINE( 7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_B)
	TILE_SEQ_LINE( 0, 15,  0, 16,  1,  6, SPR_AIRPORT_FENCE_X | (1U << PALETTE_MODIFIER_COLOUR)) // fences south
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_radar_12_fence_se[] = {
	TILE_SEQ_LINE( 7,  7,  0,  2,  2,  8, SPR_AIRPORT_RADAR_C)
	TILE_SEQ_LINE( 0, 15,  0, 16,  1,  6, SPR_AIRPORT_FENCE_X | (1U << PALETTE_MODIFIER_COLOUR)) // fences south
	TILE_SEQ_END()
};
/* END */

/* helipad for helistation */
static const DrawTileSeqStruct _station_display_newhelipad_fence_se_sw[] = {
	TILE_SEQ_LINE( 0,  1,  2,  0,  0,  0, SPR_NEWHELIPAD)
	TILE_SEQ_LINE(15,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR)) // fences west
	TILE_SEQ_LINE( 0, 15,  0, 16,  1,  6, SPR_AIRPORT_FENCE_X | (1U << PALETTE_MODIFIER_COLOUR)) // fences south
	TILE_SEQ_END()
};

/* helipad for helistation */
static const DrawTileSeqStruct _station_display_newhelipad_fence_nw_sw[] = {
	TILE_SEQ_LINE( 0,  1,  2,  0,  0,  0, SPR_NEWHELIPAD)
	TILE_SEQ_LINE(15,  0,  0,  1, 16,  6, SPR_AIRPORT_FENCE_Y | (1U << PALETTE_MODIFIER_COLOUR)) // fences west
	TILE_SEQ_LINE( 0,  0,  0, 16,  1,  6, SPR_AIRPORT_FENCE_X | (1U << PALETTE_MODIFIER_COLOUR)) // fences north
	TILE_SEQ_END()
};

/* helipad for helistation */
static const DrawTileSeqStruct _station_display_newhelipad_fence_nw[] = {
	TILE_SEQ_LINE( 0,  1,  2,  0,  0,  0, SPR_NEWHELIPAD)
	TILE_SEQ_LINE( 0,  0,  0, 16,  1,  6, SPR_AIRPORT_FENCE_X | (1U << PALETTE_MODIFIER_COLOUR)) // fences north
	TILE_SEQ_END()
};

/* helidepot office without fence */
static const DrawTileSeqStruct _station_display_low_building[] = {
	TILE_SEQ_LINE( 3,  3,  0, 10, 10, 60, SPR_AIRPORT_HELIDEPOT_OFFICE | (1U << PALETTE_MODIFIER_COLOUR))  // helidepot office
	TILE_SEQ_END()
};

/* half grass half SPR_AIRPORT_APRON */
static const DrawTileSeqStruct _station_display_grass_west[] = {
	TILE_SEQ_LINE(0,  0,  0,  0,  0,  0, SPR_GRASS_LEFT)
	TILE_SEQ_END()
};

/* half grass half SPR_AIRPORT_APRON */
static const DrawTileSeqStruct _station_display_grass_east[] = {
	TILE_SEQ_LINE(0,  0,  0,  0,  0,  0, SPR_GRASS_RIGHT)
	TILE_SEQ_END()
};

/* drive-through truck stop X */
static const DrawTileSeqStruct _station_display_datas_0168[] = {
	TILE_SEQ_LINE( 0,  0,  0,  16,  3, 16, SPR_TRUCK_STOP_DT_X_W | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE( 0, 13,  0,  16,  3, 16, SPR_TRUCK_STOP_DT_X_E | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

/* drive-through truck stop Y */
static const DrawTileSeqStruct _station_display_datas_0169[] = {
	TILE_SEQ_LINE(13,  0,  0,  3, 16, 16, SPR_TRUCK_STOP_DT_Y_W | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE( 0,  0,  0,  3, 16, 16, SPR_TRUCK_STOP_DT_Y_E | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

/* drive-through bus stop X */
static const DrawTileSeqStruct _station_display_datas_0170[] = {
	TILE_SEQ_LINE( 0,  0,  0,  16,  3, 16, SPR_BUS_STOP_DT_X_W | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE( 0, 13,  0,  16,  3, 16, SPR_BUS_STOP_DT_X_E | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

/* drive-through bus stop Y */
static const DrawTileSeqStruct _station_display_datas_0171[] = {
	TILE_SEQ_LINE(13,  0,  0,  3,  16, 16, SPR_BUS_STOP_DT_Y_W | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE( 0,  0,  0,  3,  16, 16, SPR_BUS_STOP_DT_Y_E | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_datas_waypoint_X[] = {
	TILE_SEQ_LINE( 0,  0,  0, 16,  5, 23, SPR_WAYPOINT_X_1 | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE( 0, 11,  0, 16,  5, 23, SPR_WAYPOINT_X_2 | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

static const DrawTileSeqStruct _station_display_datas_waypoint_Y[] = {
	TILE_SEQ_LINE( 0,  0,  0,  5, 16, 23, SPR_WAYPOINT_Y_1 | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_LINE(11,  0,  0,  5, 16, 23, SPR_WAYPOINT_Y_2 | (1U << PALETTE_MODIFIER_COLOUR))
	TILE_SEQ_END()
};

#undef TILE_SEQ_END
#undef TILE_SEQ_LINE
#undef TILE_SEQ_LINE_PAL
#undef TILE_SEQ_CHILD
#undef TILE_SEQ_GROUND

/**
 * Constructor macro of a DrawTileSprites structure
 * @param img   Ground sprite without palette of the tile
 * @param dtss  Sequence child sprites of the tile
 */
#define TILE_SPRITE_LINE(img, dtss) { {img, PAL_NONE}, dtss },
#define TILE_SPRITE_NULL() { {0, 0}, nullptr },

extern const DrawTileSprites _station_display_datas_rail[] = {
	TILE_SPRITE_LINE(SPR_RAIL_TRACK_X,               _station_display_datas_0)
	TILE_SPRITE_LINE(SPR_RAIL_TRACK_Y,               _station_display_datas_1)
	TILE_SPRITE_LINE(SPR_RAIL_TRACK_X,               _station_display_datas_2)
	TILE_SPRITE_LINE(SPR_RAIL_TRACK_Y,               _station_display_datas_3)
	TILE_SPRITE_LINE(SPR_RAIL_TRACK_X,               _station_display_datas_4)
	TILE_SPRITE_LINE(SPR_RAIL_TRACK_Y,               _station_display_datas_5)
	TILE_SPRITE_LINE(SPR_RAIL_TRACK_X,               _station_display_datas_6)
	TILE_SPRITE_LINE(SPR_RAIL_TRACK_Y,               _station_display_datas_7)
};

static const DrawTileSprites _station_display_datas_airport[] = {
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_nothing)  // APT_APRON
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_fence_nw) // APT_APRON_FENCE_NW
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_fence_sw) // APT_APRON_FENCE_SW
	TILE_SPRITE_LINE(SPR_AIRPORT_AIRCRAFT_STAND,     _station_display_nothing)  // APT_STAND
	TILE_SPRITE_LINE(SPR_AIRPORT_TAXIWAY_NS_WEST,    _station_display_nothing)  // APT_APRON_W
	TILE_SPRITE_LINE(SPR_AIRPORT_TAXIWAY_EW_SOUTH,   _station_display_nothing)  // APT_APRON_S
	TILE_SPRITE_LINE(SPR_AIRPORT_TAXIWAY_XING_SOUTH, _station_display_nothing)  // APT_APRON_VER_CROSSING_S
	TILE_SPRITE_LINE(SPR_AIRPORT_TAXIWAY_XING_WEST,  _station_display_nothing)  // APT_APRON_HOR_CROSSING_W
	TILE_SPRITE_LINE(SPR_AIRPORT_TAXIWAY_NS_CTR,     _station_display_nothing)  // APT_APRON_VER_CROSSING_N
	TILE_SPRITE_LINE(SPR_AIRPORT_TAXIWAY_XING_EAST,  _station_display_nothing)  // APT_APRON_HOR_CROSSING_E
	TILE_SPRITE_LINE(SPR_AIRPORT_TAXIWAY_NS_EAST,    _station_display_nothing)  // APT_APRON_E
	TILE_SPRITE_LINE(SPR_AIRPORT_TAXIWAY_EW_NORTH,   _station_display_nothing)  // APT_ARPON_N
	TILE_SPRITE_LINE(SPR_AIRPORT_TAXIWAY_EW_CTR,     _station_display_nothing)  // APT_APRON_HOR
	TILE_SPRITE_LINE(SPR_AIRPORT_TAXIWAY_EW_NORTH,   _station_display_fence_sw) // APT_APRON_N_FENCE_SW
	TILE_SPRITE_LINE(SPR_AIRPORT_RUNWAY_EXIT_A,      _station_display_fence_se) // APT_RUNWAY_1
	TILE_SPRITE_LINE(SPR_AIRPORT_RUNWAY_EXIT_B,      _station_display_fence_se) // APT_RUNWAY_2
	TILE_SPRITE_LINE(SPR_AIRPORT_RUNWAY_EXIT_C,      _station_display_fence_se) // APT_RUNWAY_3
	TILE_SPRITE_LINE(SPR_AIRPORT_RUNWAY_EXIT_D,      _station_display_fence_se) // APT_RUNWAY_4
	TILE_SPRITE_LINE(SPR_AIRPORT_RUNWAY_END,         _station_display_fence_se) // APT_RUNWAY_END_FENCE_SE
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_terminal_a) // APT_BUILDING_2
	TILE_SPRITE_LINE(SPR_FLAT_GRASS_TILE,            _station_display_tower_fence_sw) // APT_TOWER_FENCE_SW
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_concourse) // APT_ROUND_TERMINAL
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_datas_terminal_b) // APT_BUILDING_3
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_datas_terminal_c) // APT_BUILDING_1
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_hangar_se) // APT_DEPOT_SE
	TILE_SPRITE_LINE(SPR_AIRPORT_AIRCRAFT_STAND,     _station_display_jetway_1)  // APT_STAND_1
	TILE_SPRITE_LINE(SPR_AIRPORT_AIRCRAFT_STAND,     _station_display_jetway_2)  // APT_STAND_PIER_NE
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_jetway_3)  // APT_PIER_NW_NE
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_passenger_tunnel) // APT_PIER
	TILE_SPRITE_LINE(SPR_FLAT_GRASS_TILE,            _station_display_nothing)   // APT_EMPTY
	TILE_SPRITE_LINE(SPR_FLAT_GRASS_TILE,            _station_display_fence_ne)  // APT_EMPTY_FENCE_NE
	TILE_SPRITE_NULL() // APT_RADAR_GRASS_FENCE_SW
	TILE_SPRITE_LINE(SPR_FLAT_GRASS_TILE,            _station_display_transmitter_fence_ne) // APT_RADIO_TOWER_FENCE_NE
	TILE_SPRITE_LINE(SPR_AIRFIELD_TERM_A,            _station_display_nothing)   // APT_SMALL_BUILDING_3
	TILE_SPRITE_LINE(SPR_AIRFIELD_TERM_B,            _station_display_nothing)   // APT_SMALL_BUILDING_2
	TILE_SPRITE_LINE(SPR_AIRFIELD_TERM_C_GROUND | (1U << PALETTE_MODIFIER_COLOUR), _station_display_datas_terminal_c_2) // APT_SMALL_BUILDING_1
	TILE_SPRITE_LINE(SPR_AIRFIELD_APRON_A,           _station_display_fence_sw)  // APT_GRASS_FENCE_SW
	TILE_SPRITE_LINE(SPR_AIRFIELD_APRON_B,           _station_display_nothing)   // APT_GRASS_2
	TILE_SPRITE_LINE(SPR_AIRFIELD_APRON_C,           _station_display_nothing)   // APT_GRASS_1
	TILE_SPRITE_NULL() // APT_GRASS_FENCE_NE_FLAG
	TILE_SPRITE_LINE(SPR_AIRFIELD_RUNWAY_NEAR_END,   _station_display_fence_se)  // APT_RUNWAY_SMALL_NEAR_END
	TILE_SPRITE_LINE(SPR_AIRFIELD_RUNWAY_MIDDLE,     _station_display_fence_se)  // APT_RUNWAY_SMALL_MIDDLE
	TILE_SPRITE_LINE(SPR_AIRFIELD_RUNWAY_FAR_END,    _station_display_fence_se)  // APT_RUNWAY_SMALL_FAR_END
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_small_depot_se) // APT_SMALL_DEPOT_SE
	TILE_SPRITE_LINE(SPR_FLAT_GRASS_TILE,            _station_display_heliport)  // APT_HELIPORT
	TILE_SPRITE_LINE(SPR_AIRPORT_RUNWAY_END,         _station_display_nothing)   // APT_RUNWAY_END
	TILE_SPRITE_LINE(SPR_AIRPORT_RUNWAY_EXIT_B,      _station_display_nothing)   // APT_RUNWAY_5
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_tower)     // APT_TOWER
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_fence_ne)  // APT_APRON_FENCE_NE
	TILE_SPRITE_LINE(SPR_AIRPORT_RUNWAY_END,         _station_display_fence_nw)  // APT_RUNWAY_END_FENCE_NW
	TILE_SPRITE_LINE(SPR_AIRPORT_RUNWAY_EXIT_B,      _station_display_fence_nw)  // APT_RUNWAY_FENCE_NW
	TILE_SPRITE_NULL() // APT_RADAR_FENCE_SW
	TILE_SPRITE_NULL() // APT_RADAR_FENCE_NE
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_helipad_fence_sw) // APT_HELIPAD_1
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_helipad_fence_nw) // APT_HELIPAD_2_FENCE_NW
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_helipad)     // APT_HELIPAD_2
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_fence_ne_sw) // APT_APRON_FENCE_NE_SW
	TILE_SPRITE_LINE(SPR_AIRPORT_RUNWAY_END,         _station_display_fence_nw_sw) // APT_RUNWAY_END_FENCE_NW_SW
	TILE_SPRITE_LINE(SPR_AIRPORT_RUNWAY_END,         _station_display_fence_se_sw) // APT_RUNWAY_END_FENCE_SE_SW
	TILE_SPRITE_LINE(SPR_AIRPORT_RUNWAY_END,         _station_display_fence_ne_nw) // APT_RUNWAY_END_FENCE_NE_NW
	TILE_SPRITE_LINE(SPR_AIRPORT_RUNWAY_END,         _station_display_fence_ne_se) // APT_RUNWAY_END_FENCE_NE_SE
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_helipad_fence_NE_SE) // APT_HELIPAD_2_FENCE_NE_SE
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_fence_se_sw) // APT_APRON_FENCE_SE_SW
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_low_building_fence_ne_nw) // APT_LOW_BUILDING_FENCE_N
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_low_building_fence_nw) // APT_LOW_BUILDING_FENCE_NW
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_fence_se) // APT_APRON_FENCE_SE
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_newhelipad_fence_se_sw) // APT_HELIPAD_3_FENCE_SE_SW
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_newhelipad_fence_nw_sw) // APT_HELIPAD_3_FENCE_NW_SW
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_newhelipad_fence_nw) // APT_HELIPAD_3_FENCE_NW
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_low_building) // APT_LOW_BUILDING
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_fence_ne_se) // APT_APRON_FENCE_NE_SE
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_grass_west) // APT_APRON_HALF_EAST
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_grass_east) // APT_APRON_HALF_WEST
	TILE_SPRITE_NULL() // APT_GRASS_FENCE_NE_FLAG_2
};

static const DrawTileSprites _station_display_datas_airport_radar_grass_fence_sw[] = {
	TILE_SPRITE_LINE(SPR_FLAT_GRASS_TILE,            _station_display_radar_1_fence_sw) // APT_RADAR_GRASS_FENCE_SW
	TILE_SPRITE_LINE(SPR_FLAT_GRASS_TILE,            _station_display_radar_2_fence_sw)
	TILE_SPRITE_LINE(SPR_FLAT_GRASS_TILE,            _station_display_radar_3_fence_sw)
	TILE_SPRITE_LINE(SPR_FLAT_GRASS_TILE,            _station_display_radar_4_fence_sw)
	TILE_SPRITE_LINE(SPR_FLAT_GRASS_TILE,            _station_display_radar_5_fence_sw)
	TILE_SPRITE_LINE(SPR_FLAT_GRASS_TILE,            _station_display_radar_6_fence_sw)
	TILE_SPRITE_LINE(SPR_FLAT_GRASS_TILE,            _station_display_radar_7_fence_sw)
	TILE_SPRITE_LINE(SPR_FLAT_GRASS_TILE,            _station_display_radar_8_fence_sw)
	TILE_SPRITE_LINE(SPR_FLAT_GRASS_TILE,            _station_display_radar_9_fence_sw)
	TILE_SPRITE_LINE(SPR_FLAT_GRASS_TILE,            _station_display_radar_10_fence_sw)
	TILE_SPRITE_LINE(SPR_FLAT_GRASS_TILE,            _station_display_radar_11_fence_sw)
	TILE_SPRITE_LINE(SPR_FLAT_GRASS_TILE,            _station_display_radar_12_fence_sw)
};

static const DrawTileSprites _station_display_datas_airport_flag_grass_fence_ne[] = {
	TILE_SPRITE_LINE(SPR_AIRFIELD_APRON_D,           _station_display_flag_1_fence_ne) // APT_GRASS_FENCE_NE_FLAG
	TILE_SPRITE_LINE(SPR_AIRFIELD_APRON_D,           _station_display_flag_2_fence_ne)
	TILE_SPRITE_LINE(SPR_AIRFIELD_APRON_D,           _station_display_flag_3_fence_ne)
	TILE_SPRITE_LINE(SPR_AIRFIELD_APRON_D,           _station_display_flag_4_fence_ne)
};

static const DrawTileSprites _station_display_datas_airport_radar_fence_sw[] = {
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_radar_1_fence_sw) // APT_RADAR_FENCE_SW
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_radar_2_fence_sw)
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_radar_3_fence_sw)
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_radar_4_fence_sw)
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_radar_5_fence_sw)
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_radar_6_fence_sw)
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_radar_7_fence_sw)
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_radar_8_fence_sw)
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_radar_9_fence_sw)
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_radar_10_fence_sw)
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_radar_11_fence_sw)
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_radar_12_fence_sw)
};

static const DrawTileSprites _station_display_datas_airport_radar_fence_ne[] = {
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_radar_1_fence_ne) // APT_RADAR_FENCE_NE
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_radar_2_fence_ne)
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_radar_3_fence_ne)
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_radar_4_fence_ne)
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_radar_5_fence_ne)
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_radar_6_fence_ne)
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_radar_7_fence_ne)
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_radar_8_fence_ne)
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_radar_9_fence_ne)
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_radar_10_fence_ne)
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_radar_11_fence_ne)
	TILE_SPRITE_LINE(SPR_AIRPORT_APRON,              _station_display_radar_12_fence_ne)
};

static const DrawTileSprites _station_display_datas_airport_flag_grass_fence_ne_2[] = {
	TILE_SPRITE_LINE(SPR_FLAT_GRASS_TILE,            _station_display_flag_1_fence_ne) // APT_GRASS_FENCE_NE_FLAG_2
	TILE_SPRITE_LINE(SPR_FLAT_GRASS_TILE,            _station_display_flag_2_fence_ne)
	TILE_SPRITE_LINE(SPR_FLAT_GRASS_TILE,            _station_display_flag_3_fence_ne)
	TILE_SPRITE_LINE(SPR_FLAT_GRASS_TILE,            _station_display_flag_4_fence_ne)
};


static const DrawTileSprites _station_display_datas_truck[] = {
	TILE_SPRITE_LINE(SPR_TRUCK_STOP_NE_GROUND | (1U << PALETTE_MODIFIER_COLOUR), _station_display_datas_67)
	TILE_SPRITE_LINE(SPR_TRUCK_STOP_SE_GROUND | (1U << PALETTE_MODIFIER_COLOUR), _station_display_datas_68)
	TILE_SPRITE_LINE(SPR_TRUCK_STOP_SW_GROUND | (1U << PALETTE_MODIFIER_COLOUR), _station_display_datas_69)
	TILE_SPRITE_LINE(SPR_TRUCK_STOP_NW_GROUND | (1U << PALETTE_MODIFIER_COLOUR), _station_display_datas_70)
	TILE_SPRITE_LINE(SPR_ROAD_PAVED_STRAIGHT_X,      _station_display_datas_0168)
	TILE_SPRITE_LINE(SPR_ROAD_PAVED_STRAIGHT_Y,      _station_display_datas_0169)
};

static const DrawTileSprites _station_display_datas_bus[] = {
	TILE_SPRITE_LINE(SPR_BUS_STOP_NE_GROUND   | (1U << PALETTE_MODIFIER_COLOUR), _station_display_datas_71)
	TILE_SPRITE_LINE(SPR_BUS_STOP_SE_GROUND   | (1U << PALETTE_MODIFIER_COLOUR), _station_display_datas_72)
	TILE_SPRITE_LINE(SPR_BUS_STOP_SW_GROUND   | (1U << PALETTE_MODIFIER_COLOUR), _station_display_datas_73)
	TILE_SPRITE_LINE(SPR_BUS_STOP_NW_GROUND   | (1U << PALETTE_MODIFIER_COLOUR), _station_display_datas_74)
	TILE_SPRITE_LINE(SPR_ROAD_PAVED_STRAIGHT_X,      _station_display_datas_0170)
	TILE_SPRITE_LINE(SPR_ROAD_PAVED_STRAIGHT_Y,      _station_display_datas_0171)
};

static const DrawTileSprites _station_display_datas_oilrig[] = {
	TILE_SPRITE_LINE(SPR_FLAT_WATER_TILE,            _station_display_nothing)
};

static const DrawTileSprites _station_display_datas_dock[] = {
	TILE_SPRITE_LINE(SPR_SHORE_BASE + SLOPE_SW,      _station_display_datas_76)
	TILE_SPRITE_LINE(SPR_SHORE_BASE + SLOPE_NW,      _station_display_datas_77)
	TILE_SPRITE_LINE(SPR_SHORE_BASE + SLOPE_NE,      _station_display_datas_78)
	TILE_SPRITE_LINE(SPR_SHORE_BASE + SLOPE_SE,      _station_display_datas_79)
	TILE_SPRITE_LINE(SPR_FLAT_WATER_TILE,            _station_display_datas_80)
	TILE_SPRITE_LINE(SPR_FLAT_WATER_TILE,            _station_display_datas_81)
};

static const DrawTileSprites _station_display_datas_buoy[] = {
	TILE_SPRITE_LINE(SPR_FLAT_WATER_TILE,            _station_display_datas_82)
};

static const DrawTileSprites _station_display_datas_waypoint[] = {
	TILE_SPRITE_LINE(SPR_RAIL_TRACK_X,               _station_display_datas_waypoint_X)
	TILE_SPRITE_LINE(SPR_RAIL_TRACK_Y,               _station_display_datas_waypoint_Y)
	TILE_SPRITE_LINE(SPR_RAIL_TRACK_X,               _station_display_datas_waypoint_X)
	TILE_SPRITE_LINE(SPR_RAIL_TRACK_Y,               _station_display_datas_waypoint_Y)
	TILE_SPRITE_LINE(SPR_RAIL_TRACK_X,               _station_display_datas_waypoint_X)
	TILE_SPRITE_LINE(SPR_RAIL_TRACK_Y,               _station_display_datas_waypoint_Y)
	TILE_SPRITE_LINE(SPR_RAIL_TRACK_X,               _station_display_datas_waypoint_X)
	TILE_SPRITE_LINE(SPR_RAIL_TRACK_Y,               _station_display_datas_waypoint_Y)
};

#undef TILE_SPRITE_LINE
#undef TILE_SPRITE_NULL

/* Default waypoint is also drawn as fallback for NewGRF waypoints.
 * As these are drawn/build like stations, they may use the same number of layouts. */
static_assert(lengthof(_station_display_datas_rail) == lengthof(_station_display_datas_waypoint));

static const DrawTileSprites * const _station_display_datas[] = {
	_station_display_datas_rail,
	_station_display_datas_airport,
	_station_display_datas_truck,
	_station_display_datas_bus,
	_station_display_datas_oilrig,
	_station_display_datas_dock,
	_station_display_datas_buoy,
	_station_display_datas_waypoint,
	_station_display_datas_bus,
	_station_display_datas_truck,
};
