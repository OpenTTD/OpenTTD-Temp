/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file table/engines.h
 *  This file contains all the data for vehicles
 */

#ifndef ENGINES_H
#define ENGINES_H

/**
 * Writes the properties of a train into the EngineInfo struct.
 * @see EngineInfo
 * @param a base introduction date (days since 1920-01-01)
 * @param b decay speed
 * @param c life length (years)
 * @param d base life (years)
 * @param e cargo type
 * @param f Bitmask of the climates
 * @note the 5 between b and f is the load amount
 */
#define MT(a, b, c, d, e, f) { DAYS_TILL_ORIGINAL_BASE_YEAR + a, c, d, b, 5, f, e, 0, 8, 1 << EF_RAIL_FLIPS, 0, 0, STR_EMPTY, CARGO_AGING_TICKS }

/**
 * Writes the properties of a multiple-unit train into the EngineInfo struct.
 * @see EngineInfo
 * @param a base introduction date (days since 1920-01-01)
 * @param b decay speed
 * @param c life length (years)
 * @param d base life (years)
 * @param e cargo type
 * @param f Bitmask of the climates
 * @note the 5 between b and f is the load amount
 */
#define MM(a, b, c, d, e, f) { DAYS_TILL_ORIGINAL_BASE_YEAR + a, c, d, b, 5, f, e, 0, 8, 1 << EF_RAIL_FLIPS | 1 << EF_RAIL_IS_MU, 0, 0, STR_EMPTY, CARGO_AGING_TICKS }

/**
 * Writes the properties of a train carriage into the EngineInfo struct.
 * @param a base introduction date (days since 1920-01-01)
 * @param b decay speed
 * @param c life length (years)
 * @param d base life (years)
 * @param e cargo type
 * @param f Bitmask of the climates
 * @see MT
 * @note the 5 between b and f is the load amount
 */
#define MW(a, b, c, d, e, f) { DAYS_TILL_ORIGINAL_BASE_YEAR + a, c, d, b, 5, f, e, 0, 8, 1 << EF_RAIL_FLIPS, 0, 0, STR_EMPTY, CARGO_AGING_TICKS }

/**
 * Writes the properties of a road vehicle into the EngineInfo struct.
 * @see EngineInfo
 * @param a base introduction date (days since 1920-01-01)
 * @param b decay speed
 * @param c life length (years)
 * @param d base life (years)
 * @param e cargo type
 * @param f Bitmask of the climates
 * @note the 5 between b and f is the load amount
 */
#define MR(a, b, c, d, e, f) { DAYS_TILL_ORIGINAL_BASE_YEAR + a, c, d, b, 5, f, e, 0, 8, 0, 0, 0, STR_EMPTY, CARGO_AGING_TICKS }

/**
 * Writes the properties of a ship into the EngineInfo struct.
 * @param a base introduction date (days since 1920-01-01)
 * @param b decay speed
 * @param c life length (years)
 * @param d base life (years)
 * @param e cargo type
 * @param f Bitmask of the climates
 * @note the 10 between b and f is the load amount
 */
#define MS(a, b, c, d, e, f) { DAYS_TILL_ORIGINAL_BASE_YEAR + a, c, d, b, 10, f, e, 0, 8, 0, 0, 0, STR_EMPTY, CARGO_AGING_TICKS }

/**
 * Writes the properties of an aeroplane into the EngineInfo struct.
 * @param a base introduction date (days since 1920-01-01)
 * @param b decay speed
 * @param c life length (years)
 * @param d base life (years)
 * @param e Bitmask of the climates
 * @note the 20 between b and e is the load amount
 */
#define MA(a, b, c, d, e) { DAYS_TILL_ORIGINAL_BASE_YEAR + a, c, d, b, 20, e, CT_INVALID, 0, 8, 0, 0, 0, STR_EMPTY, CARGO_AGING_TICKS }

/* Climates
 * T = Temperate
 * A = Sub-Arctic
 * S = Sub-Tropic
 * Y = Toyland */
#define T 1
#define A 2
#define S 4
#define Y 8
static const EngineInfo _orig_engine_info[] = {
	/*      base_intro     base_life
	 *      |    decay_speed         cargo_type
	 *      |    |    lifelength     |         climates
	 *      |    |    |    |         |         | */
	MT(  1827,  20,  15,  30, 0              , T      ), //   0 Kirby Paul Tank (Steam)
	MT( 12784,  20,  22,  30, 0              ,   A|S  ), //   1 MJS 250 (Diesel)
	MT(  9497,  20,  20,  50, 0              ,       Y), //   2 Ploddyphut Choo-Choo
	MT( 11688,  20,  20,  30, 0              ,       Y), //   3 Powernaut Choo-Choo
	MT( 16802,  20,  20,  30, 0              ,       Y), //   4 Mightymover Choo-Choo
	MT( 18993,  20,  20,  30, 0              ,       Y), //   5 Ploddyphut Diesel
	MT( 20820,  20,  20,  30, 0              ,       Y), //   6 Powernaut Diesel
	MT(  8766,  20,  20,  30, 0              ,   A|S  ), //   7 Wills 2-8-0 (Steam)
	MT(  5114,  20,  21,  30, 0              , T      ), //   8 Chaney 'Jubilee' (Steam)
	MT(  5479,  20,  20,  30, 0              , T      ), //   9 Ginzu 'A4' (Steam)
	MT( 12419,  20,  23,  25, 0              , T      ), //  10 SH '8P' (Steam)
	MM( 13149,  20,  12,  30, CT_PASSENGERS  , T      ), //  11 Manley-Morel DMU (Diesel)
	MM( 23376,  20,  15,  35, CT_PASSENGERS  , T      ), //  12 'Dash' (Diesel)
	MT( 14976,  20,  18,  28, 0              , T      ), //  13 SH/Hendry '25' (Diesel)
	MT( 14245,  20,  20,  30, 0              , T      ), //  14 UU '37' (Diesel)
	MT( 15341,  20,  22,  33, 0              , T      ), //  15 Floss '47' (Diesel)
	MT( 14976,  20,  20,  25, 0              ,   A|S  ), //  16 CS 4000 (Diesel)
	MT( 16437,  20,  20,  30, 0              ,   A|S  ), //  17 CS 2400 (Diesel)
	MT( 18993,  20,  22,  30, 0              ,   A|S  ), //  18 Centennial (Diesel)
	MT( 13880,  20,  22,  30, 0              ,   A|S  ), //  19 Kelling 3100 (Diesel)
	MM( 20454,  20,  22,  30, 0              ,   A|S  ), //  20 Turner Turbo (Diesel)
	MT( 16071,  20,  22,  30, 0              ,   A|S  ), //  21 MJS 1000 (Diesel)
	MT( 20820,  20,  20,  25, CT_MAIL        , T      ), //  22 SH '125' (Diesel)
	MT( 16437,  20,  23,  30, 0              , T      ), //  23 SH '30' (Electric)
	MT( 19359,  20,  23,  80, 0              , T      ), //  24 SH '40' (Electric)
	MM( 23376,  20,  25,  30, 0              , T      ), //  25 'T.I.M.' (Electric)
	MM( 26298,  20,  25,  50, 0              , T      ), //  26 'AsiaStar' (Electric)
	MW(  1827,  20,  20,  50, CT_PASSENGERS  , T|A|S|Y), //  27 Passenger Carriage
	MW(  1827,  20,  20,  50, CT_MAIL        , T|A|S|Y), //  28 Mail Van
	MW(  1827,  20,  20,  50, CT_COAL        , T|A    ), //  29 Coal Truck
	MW(  1827,  20,  20,  50, CT_OIL         , T|A|S  ), //  30 Oil Tanker
	MW(  1827,  20,  20,  50, CT_LIVESTOCK   , T|A    ), //  31 Livestock Van
	MW(  1827,  20,  20,  50, CT_GOODS       , T|A|S  ), //  32 Goods Van
	MW(  1827,  20,  20,  50, CT_GRAIN       , T|A|S  ), //  33 Grain Hopper
	MW(  1827,  20,  20,  50, CT_WOOD        , T|A|S  ), //  34 Wood Truck
	MW(  1827,  20,  20,  50, CT_IRON_ORE    , T      ), //  35 Iron Ore Hopper
	MW(  1827,  20,  20,  50, CT_STEEL       , T      ), //  36 Steel Truck
	MW(  1827,  20,  20,  50, CT_VALUABLES   , T|A|S  ), //  37 Armoured Van
	MW(  1827,  20,  20,  50, CT_FOOD        ,   A|S  ), //  38 Food Van
	MW(  1827,  20,  20,  50, CT_PAPER       ,   A    ), //  39 Paper Truck
	MW(  1827,  20,  20,  50, CT_COPPER_ORE  ,     S  ), //  40 Copper Ore Hopper
	MW(  1827,  20,  20,  50, CT_WATER       ,     S  ), //  41 Water Tanker
	MW(  1827,  20,  20,  50, CT_FRUIT       ,     S  ), //  42 Fruit Truck
	MW(  1827,  20,  20,  50, CT_RUBBER      ,     S  ), //  43 Rubber Truck
	MW(  1827,  20,  20,  50, CT_SUGAR       ,       Y), //  44 Sugar Truck
	MW(  1827,  20,  20,  50, CT_COTTON_CANDY,       Y), //  45 Candyfloss Hopper
	MW(  1827,  20,  20,  50, CT_TOFFEE      ,       Y), //  46 Toffee Hopper
	MW(  1827,  20,  20,  50, CT_BUBBLES     ,       Y), //  47 Bubble Van
	MW(  1827,  20,  20,  50, CT_COLA        ,       Y), //  48 Cola Tanker
	MW(  1827,  20,  20,  50, CT_CANDY       ,       Y), //  49 Sweet Van
	MW(  1827,  20,  20,  50, CT_TOYS        ,       Y), //  50 Toy Van
	MW(  1827,  20,  20,  50, CT_BATTERIES   ,       Y), //  51 Battery Truck
	MW(  1827,  20,  20,  50, CT_FIZZY_DRINKS,       Y), //  52 Fizzy Drink Truck
	MW(  1827,  20,  20,  50, CT_PLASTIC     ,       Y), //  53 Plastic Truck
	MT( 28490,  20,  20,  50, 0              , T|A|S  ), //  54 'X2001' (Electric)
	MT( 31047,  20,  20,  50, CT_PASSENGERS  , T|A|S  ), //  55 'Millennium Z1' (Electric)
	MT( 28855,  20,  20,  50, 0              ,       Y), //  56 Wizzowow Z99
	MW(  1827,  20,  20,  50, CT_PASSENGERS  , T|A|S|Y), //  57 Passenger Carriage
	MW(  1827,  20,  20,  50, CT_MAIL        , T|A|S|Y), //  58 Mail Van
	MW(  1827,  20,  20,  50, CT_COAL        , T|A    ), //  59 Coal Truck
	MW(  1827,  20,  20,  50, CT_OIL         , T|A|S  ), //  60 Oil Tanker
	MW(  1827,  20,  20,  50, CT_LIVESTOCK   , T|A    ), //  61 Livestock Van
	MW(  1827,  20,  20,  50, CT_GOODS       , T|A|S  ), //  62 Goods Van
	MW(  1827,  20,  20,  50, CT_GRAIN       , T|A|S  ), //  63 Grain Hopper
	MW(  1827,  20,  20,  50, CT_WOOD        , T|A|S  ), //  64 Wood Truck
	MW(  1827,  20,  20,  50, CT_IRON_ORE    , T      ), //  65 Iron Ore Hopper
	MW(  1827,  20,  20,  50, CT_STEEL       , T      ), //  66 Steel Truck
	MW(  1827,  20,  20,  50, CT_VALUABLES   , T|A|S  ), //  67 Armoured Van
	MW(  1827,  20,  20,  50, CT_FOOD        ,   A|S  ), //  68 Food Van
	MW(  1827,  20,  20,  50, CT_PAPER       ,   A    ), //  69 Paper Truck
	MW(  1827,  20,  20,  50, CT_COPPER_ORE  ,     S  ), //  70 Copper Ore Hopper
	MW(  1827,  20,  20,  50, CT_WATER       ,     S  ), //  71 Water Tanker
	MW(  1827,  20,  20,  50, CT_FRUIT       ,     S  ), //  72 Fruit Truck
	MW(  1827,  20,  20,  50, CT_RUBBER      ,     S  ), //  73 Rubber Truck
	MW(  1827,  20,  20,  50, CT_SUGAR       ,       Y), //  74 Sugar Truck
	MW(  1827,  20,  20,  50, CT_COTTON_CANDY,       Y), //  75 Candyfloss Hopper
	MW(  1827,  20,  20,  50, CT_TOFFEE      ,       Y), //  76 Toffee Hopper
	MW(  1827,  20,  20,  50, CT_BUBBLES     ,       Y), //  77 Bubble Van
	MW(  1827,  20,  20,  50, CT_COLA        ,       Y), //  78 Cola Tanker
	MW(  1827,  20,  20,  50, CT_CANDY       ,       Y), //  79 Sweet Van
	MW(  1827,  20,  20,  50, CT_TOYS        ,       Y), //  80 Toy Van
	MW(  1827,  20,  20,  50, CT_BATTERIES   ,       Y), //  81 Battery Truck
	MW(  1827,  20,  20,  50, CT_FIZZY_DRINKS,       Y), //  82 Fizzy Drink Truck
	MW(  1827,  20,  20,  50, CT_PLASTIC     ,       Y), //  83 Plastic Truck
	MT( 36525,  20,  20,  50, 0              , T|A|S  ), //  84 Lev1 'Leviathan' (Electric)
	MT( 39447,  20,  20,  50, 0              , T|A|S  ), //  85 Lev2 'Cyclops' (Electric)
	MT( 42004,  20,  20,  50, 0              , T|A|S  ), //  86 Lev3 'Pegasus' (Electric)
	MT( 42735,  20,  20,  50, 0              , T|A|S  ), //  87 Lev4 'Chimaera' (Electric)
	MT( 36891,  20,  20,  60, 0              ,       Y), //  88 Wizzowow Rocketeer
	MW(  1827,  20,  20,  50, CT_PASSENGERS  , T|A|S|Y), //  89 Passenger Carriage
	MW(  1827,  20,  20,  50, CT_MAIL        , T|A|S|Y), //  90 Mail Van
	MW(  1827,  20,  20,  50, CT_COAL        , T|A    ), //  91 Coal Truck
	MW(  1827,  20,  20,  50, CT_OIL         , T|A|S  ), //  92 Oil Tanker
	MW(  1827,  20,  20,  50, CT_LIVESTOCK   , T|A    ), //  93 Livestock Van
	MW(  1827,  20,  20,  50, CT_GOODS       , T|A|S  ), //  94 Goods Van
	MW(  1827,  20,  20,  50, CT_GRAIN       , T|A|S  ), //  95 Grain Hopper
	MW(  1827,  20,  20,  50, CT_WOOD        , T|A|S  ), //  96 Wood Truck
	MW(  1827,  20,  20,  50, CT_IRON_ORE    , T      ), //  97 Iron Ore Hopper
	MW(  1827,  20,  20,  50, CT_STEEL       , T      ), //  98 Steel Truck
	MW(  1827,  20,  20,  50, CT_VALUABLES   , T|A|S  ), //  99 Armoured Van
	MW(  1827,  20,  20,  50, CT_FOOD        ,   A|S  ), // 100 Food Van
	MW(  1827,  20,  20,  50, CT_PAPER       ,   A    ), // 101 Paper Truck
	MW(  1827,  20,  20,  50, CT_COPPER_ORE  ,     S  ), // 102 Copper Ore Hopper
	MW(  1827,  20,  20,  50, CT_WATER       ,     S  ), // 103 Water Tanker
	MW(  1827,  20,  20,  50, CT_FRUIT       ,     S  ), // 104 Fruit Truck
	MW(  1827,  20,  20,  50, CT_RUBBER      ,     S  ), // 105 Rubber Truck
	MW(  1827,  20,  20,  50, CT_SUGAR       ,       Y), // 106 Sugar Truck
	MW(  1827,  20,  20,  50, CT_COTTON_CANDY,       Y), // 107 Candyfloss Hopper
	MW(  1827,  20,  20,  50, CT_TOFFEE      ,       Y), // 108 Toffee Hopper
	MW(  1827,  20,  20,  50, CT_BUBBLES     ,       Y), // 109 Bubble Van
	MW(  1827,  20,  20,  50, CT_COLA        ,       Y), // 110 Cola Tanker
	MW(  1827,  20,  20,  50, CT_CANDY       ,       Y), // 111 Sweet Van
	MW(  1827,  20,  20,  50, CT_TOYS        ,       Y), // 112 Toy Van
	MW(  1827,  20,  20,  50, CT_BATTERIES   ,       Y), // 113 Battery Truck
	MW(  1827,  20,  20,  50, CT_FIZZY_DRINKS,       Y), // 114 Fizzy Drink Truck
	MW(  1827,  20,  20,  50, CT_PLASTIC     ,       Y), // 115 Plastic Truck
	MR(  3378,  20,  12,  40, CT_PASSENGERS  , T|A|S  ), // 116 MPS Regal Bus
	MR( 16071,  20,  15,  30, CT_PASSENGERS  , T|A|S  ), // 117 Hereford Leopard Bus
	MR( 24107,  20,  15,  40, CT_PASSENGERS  , T|A|S  ), // 118 Foster Bus
	MR( 32142,  20,  15,  80, CT_PASSENGERS  , T|A|S  ), // 119 Foster MkII Superbus
	MR(  9132,  20,  15,  40, CT_PASSENGERS  ,       Y), // 120 Ploddyphut MkI Bus
	MR( 18993,  20,  15,  40, CT_PASSENGERS  ,       Y), // 121 Ploddyphut MkII Bus
	MR( 32873,  20,  15,  80, CT_PASSENGERS  ,       Y), // 122 Ploddyphut MkIII Bus
	MR(  5479,  20,  15,  55, CT_COAL        , T|A    ), // 123 Balogh Coal Truck
	MR( 20089,  20,  15,  55, CT_COAL        , T|A    ), // 124 Uhl Coal Truck
	MR( 33969,  20,  15,  85, CT_COAL        , T|A    ), // 125 DW Coal Truck
	MR(  5479,  20,  15,  55, CT_MAIL        , T|A|S  ), // 126 MPS Mail Truck
	MR( 21550,  20,  15,  55, CT_MAIL        , T|A|S  ), // 127 Reynard Mail Truck
	MR( 35795,  20,  15,  85, CT_MAIL        , T|A|S  ), // 128 Perry Mail Truck
	MR(  5479,  20,  15,  55, CT_MAIL        ,       Y), // 129 MightyMover Mail Truck
	MR( 21550,  20,  15,  55, CT_MAIL        ,       Y), // 130 Powernaught Mail Truck
	MR( 35795,  20,  15,  85, CT_MAIL        ,       Y), // 131 Wizzowow Mail Truck
	MR(  5479,  20,  15,  55, CT_OIL         , T|A|S  ), // 132 Witcombe Oil Tanker
	MR( 19359,  20,  15,  55, CT_OIL         , T|A|S  ), // 133 Foster Oil Tanker
	MR( 31047,  20,  15,  85, CT_OIL         , T|A|S  ), // 134 Perry Oil Tanker
	MR(  5479,  20,  15,  55, CT_LIVESTOCK   , T|A    ), // 135 Talbott Livestock Van
	MR( 21915,  20,  15,  55, CT_LIVESTOCK   , T|A    ), // 136 Uhl Livestock Van
	MR( 37256,  20,  15,  85, CT_LIVESTOCK   , T|A    ), // 137 Foster Livestock Van
	MR(  5479,  20,  15,  55, CT_GOODS       , T|A|S  ), // 138 Balogh Goods Truck
	MR( 19724,  20,  15,  55, CT_GOODS       , T|A|S  ), // 139 Craighead Goods Truck
	MR( 31047,  20,  15,  85, CT_GOODS       , T|A|S  ), // 140 Goss Goods Truck
	MR(  5479,  20,  15,  55, CT_GRAIN       , T|A|S  ), // 141 Hereford Grain Truck
	MR( 21185,  20,  15,  55, CT_GRAIN       , T|A|S  ), // 142 Thomas Grain Truck
	MR( 32873,  20,  15,  85, CT_GRAIN       , T|A|S  ), // 143 Goss Grain Truck
	MR(  5479,  20,  15,  55, CT_WOOD        , T|A|S  ), // 144 Witcombe Wood Truck
	MR( 19724,  20,  15,  55, CT_WOOD        , T|A|S  ), // 145 Foster Wood Truck
	MR( 35430,  20,  15,  85, CT_WOOD        , T|A|S  ), // 146 Moreland Wood Truck
	MR(  5479,  20,  15,  55, CT_IRON_ORE    , T      ), // 147 MPS Iron Ore Truck
	MR( 20820,  20,  15,  55, CT_IRON_ORE    , T      ), // 148 Uhl Iron Ore Truck
	MR( 33238,  20,  15,  85, CT_IRON_ORE    , T      ), // 149 Chippy Iron Ore Truck
	MR(  5479,  20,  15,  55, CT_STEEL       , T      ), // 150 Balogh Steel Truck
	MR( 21185,  20,  15,  55, CT_STEEL       , T      ), // 151 Uhl Steel Truck
	MR( 31777,  20,  15,  85, CT_STEEL       , T      ), // 152 Kelling Steel Truck
	MR(  5479,  20,  15,  55, CT_VALUABLES   , T|A|S  ), // 153 Balogh Armoured Truck
	MR( 22281,  20,  15,  55, CT_VALUABLES   , T|A|S  ), // 154 Uhl Armoured Truck
	MR( 33603,  20,  15,  85, CT_VALUABLES   , T|A|S  ), // 155 Foster Armoured Truck
	MR(  5479,  20,  15,  55, CT_FOOD        ,   A|S  ), // 156 Foster Food Van
	MR( 18628,  20,  15,  55, CT_FOOD        ,   A|S  ), // 157 Perry Food Van
	MR( 30681,  20,  15,  85, CT_FOOD        ,   A|S  ), // 158 Chippy Food Van
	MR(  5479,  20,  15,  55, CT_PAPER       ,   A    ), // 159 Uhl Paper Truck
	MR( 21185,  20,  15,  55, CT_PAPER       ,   A    ), // 160 Balogh Paper Truck
	MR( 31777,  20,  15,  85, CT_PAPER       ,   A    ), // 161 MPS Paper Truck
	MR(  5479,  20,  15,  55, CT_COPPER_ORE  ,     S  ), // 162 MPS Copper Ore Truck
	MR( 20820,  20,  15,  55, CT_COPPER_ORE  ,     S  ), // 163 Uhl Copper Ore Truck
	MR( 33238,  20,  15,  85, CT_COPPER_ORE  ,     S  ), // 164 Goss Copper Ore Truck
	MR(  5479,  20,  15,  55, CT_WATER       ,     S  ), // 165 Uhl Water Tanker
	MR( 20970,  20,  15,  55, CT_WATER       ,     S  ), // 166 Balogh Water Tanker
	MR( 33388,  20,  15,  85, CT_WATER       ,     S  ), // 167 MPS Water Tanker
	MR(  5479,  20,  15,  55, CT_FRUIT       ,     S  ), // 168 Balogh Fruit Truck
	MR( 21335,  20,  15,  55, CT_FRUIT       ,     S  ), // 169 Uhl Fruit Truck
	MR( 33753,  20,  15,  85, CT_FRUIT       ,     S  ), // 170 Kelling Fruit Truck
	MR(  5479,  20,  15,  55, CT_RUBBER      ,     S  ), // 171 Balogh Rubber Truck
	MR( 20604,  20,  15,  55, CT_RUBBER      ,     S  ), // 172 Uhl Rubber Truck
	MR( 33023,  20,  15,  85, CT_RUBBER      ,     S  ), // 173 RMT Rubber Truck
	MR(  5479,  20,  15,  55, CT_SUGAR       ,       Y), // 174 MightyMover Sugar Truck
	MR( 19724,  20,  15,  55, CT_SUGAR       ,       Y), // 175 Powernaught Sugar Truck
	MR( 33238,  20,  15,  85, CT_SUGAR       ,       Y), // 176 Wizzowow Sugar Truck
	MR(  5479,  20,  15,  55, CT_COLA        ,       Y), // 177 MightyMover Cola Truck
	MR( 20089,  20,  15,  55, CT_COLA        ,       Y), // 178 Powernaught Cola Truck
	MR( 33603,  20,  15,  85, CT_COLA        ,       Y), // 179 Wizzowow Cola Truck
	MR(  5479,  20,  15,  55, CT_COTTON_CANDY,       Y), // 180 MightyMover Candyfloss Truck
	MR( 20454,  20,  15,  55, CT_COTTON_CANDY,       Y), // 181 Powernaught Candyfloss Truck
	MR( 33969,  20,  15,  85, CT_COTTON_CANDY,       Y), // 182 Wizzowow Candyfloss Truck
	MR(  5479,  20,  15,  55, CT_TOFFEE      ,       Y), // 183 MightyMover Toffee Truck
	MR( 20820,  20,  15,  55, CT_TOFFEE      ,       Y), // 184 Powernaught Toffee Truck
	MR( 34334,  20,  15,  85, CT_TOFFEE      ,       Y), // 185 Wizzowow Toffee Truck
	MR(  5479,  20,  15,  55, CT_TOYS        ,       Y), // 186 MightyMover Toy Van
	MR( 21185,  20,  15,  55, CT_TOYS        ,       Y), // 187 Powernaught Toy Van
	MR( 34699,  20,  15,  85, CT_TOYS        ,       Y), // 188 Wizzowow Toy Van
	MR(  5479,  20,  15,  55, CT_CANDY       ,       Y), // 189 MightyMover Sweet Truck
	MR( 21550,  20,  15,  55, CT_CANDY       ,       Y), // 190 Powernaught Sweet Truck
	MR( 35064,  20,  15,  85, CT_CANDY       ,       Y), // 191 Wizzowow Sweet Truck
	MR(  5479,  20,  15,  55, CT_BATTERIES   ,       Y), // 192 MightyMover Battery Truck
	MR( 19874,  20,  15,  55, CT_BATTERIES   ,       Y), // 193 Powernaught Battery Truck
	MR( 35430,  20,  15,  85, CT_BATTERIES   ,       Y), // 194 Wizzowow Battery Truck
	MR(  5479,  20,  15,  55, CT_FIZZY_DRINKS,       Y), // 195 MightyMover Fizzy Drink Truck
	MR( 20239,  20,  15,  55, CT_FIZZY_DRINKS,       Y), // 196 Powernaught Fizzy Drink Truck
	MR( 35795,  20,  15,  85, CT_FIZZY_DRINKS,       Y), // 197 Wizzowow Fizzy Drink Truck
	MR(  5479,  20,  15,  55, CT_PLASTIC     ,       Y), // 198 MightyMover Plastic Truck
	MR( 20604,  20,  15,  55, CT_PLASTIC     ,       Y), // 199 Powernaught Plastic Truck
	MR( 32873,  20,  15,  85, CT_PLASTIC     ,       Y), // 200 Wizzowow Plastic Truck
	MR(  5479,  20,  15,  55, CT_BUBBLES     ,       Y), // 201 MightyMover Bubble Truck
	MR( 20970,  20,  15,  55, CT_BUBBLES     ,       Y), // 202 Powernaught Bubble Truck
	MR( 33023,  20,  15,  85, CT_BUBBLES     ,       Y), // 203 Wizzowow Bubble Truck
	MS(  2922,   5,  30,  50, CT_OIL         , T|A|S  ), // 204 MPS Oil Tanker
	MS( 17167,   5,  30,  90, CT_OIL         , T|A|S  ), // 205 CS-Inc. Oil Tanker
	MS(  2192,   5,  30,  55, CT_PASSENGERS  , T|A|S  ), // 206 MPS Passenger Ferry
	MS( 18628,   5,  30,  90, CT_PASSENGERS  , T|A|S  ), // 207 FFP Passenger Ferry
	MS( 17257,  10,  25,  90, CT_PASSENGERS  , T|A|S  ), // 208 Bakewell 300 Hovercraft
	MS(  9587,   5,  30,  40, CT_PASSENGERS  ,       Y), // 209 Chugger-Chug Passenger Ferry
	MS( 20544,   5,  30,  90, CT_PASSENGERS  ,       Y), // 210 Shivershake Passenger Ferry
	MS(  2557,   5,  30,  55, CT_GOODS       , T|A|S  ), // 211 Yate Cargo ship
	MS( 19724,   5,  30,  98, CT_GOODS       , T|A|S  ), // 212 Bakewell Cargo ship
	MS(  9587,   5,  30,  45, CT_GOODS       ,       Y), // 213 Mightymover Cargo ship
	MS( 22371,   5,  30,  90, CT_GOODS       ,       Y), // 214 Powernaut Cargo ship
	MA(  2922,  20,  20,  20,                  T|A|S  ), // 215 Sampson U52
	MA(  9922,  20,  24,  20,                  T|A|S  ), // 216 Coleman Count
	MA( 12659,  20,  18,  20,                  T|A|S  ), // 217 FFP Dart
	MA( 17652,  20,  25,  35,                  T|A|S  ), // 218 Yate Haugan
	MA(  4929,  20,  30,  30,                  T|A|S  ), // 219 Bakewell Cotswald LB-3
	MA( 13695,  20,  23,  25,                  T|A|S  ), // 220 Bakewell Luckett LB-8
	MA( 16341,  20,  26,  30,                  T|A|S  ), // 221 Bakewell Luckett LB-9
	MA( 21395,  20,  25,  30,                  T|A|S  ), // 222 Bakewell Luckett LB80
	MA( 18263,  20,  20,  30,                  T|A|S  ), // 223 Bakewell Luckett LB-10
	MA( 25233,  20,  25,  30,                  T|A|S  ), // 224 Bakewell Luckett LB-11
	MA( 15371,  20,  22,  25,                  T|A|S  ), // 225 Yate Aerospace YAC 1-11
	MA( 15461,  20,  25,  25,                  T|A|S  ), // 226 Darwin 100
	MA( 16952,  20,  22,  25,                  T|A|S  ), // 227 Darwin 200
	MA( 17227,  20,  25,  30,                  T|A|S  ), // 228 Darwin 300
	MA( 22371,  20,  25,  35,                  T|A|S  ), // 229 Darwin 400
	MA( 22341,  20,  25,  30,                  T|A|S  ), // 230 Darwin 500
	MA( 27209,  20,  25,  30,                  T|A|S  ), // 231 Darwin 600
	MA( 17988,  20,  20,  30,                  T|A|S  ), // 232 Guru Galaxy
	MA( 18993,  20,  24,  35,                  T|A|S  ), // 233 Airtaxi A21
	MA( 22401,  20,  24,  30,                  T|A|S  ), // 234 Airtaxi A31
	MA( 24472,  20,  24,  30,                  T|A|S  ), // 235 Airtaxi A32
	MA( 26724,  20,  24,  30,                  T|A|S  ), // 236 Airtaxi A33
	MA( 22005,  20,  25,  30,                  T|A|S  ), // 237 Yate Aerospace YAe46
	MA( 24107,  20,  20,  35,                  T|A|S  ), // 238 Dinger 100
	MA( 29310,  20,  25,  60,                  T|A|S  ), // 239 AirTaxi A34-1000
	MA( 35520,  20,  22,  30,                  T|A|S  ), // 240 Yate Z-Shuttle
	MA( 36981,  20,  22,  30,                  T|A|S  ), // 241 Kelling K1
	MA( 38807,  20,  22,  50,                  T|A|S  ), // 242 Kelling K6
	MA( 42094,  20,  25,  30,                  T|A|S  ), // 243 Kelling K7
	MA( 44651,  20,  23,  30,                  T|A|S  ), // 244 Darwin 700
	MA( 40268,  20,  25,  30,                  T|A|S  ), // 245 FFP Hyperdart 2
	MA( 33693,  20,  25,  50,                  T|A|S  ), // 246 Dinger 200
	MA( 32963,  20,  20,  60,                  T|A|S  ), // 247 Dinger 1000
	MA(  9222,  20,  20,  35,                        Y), // 248 Ploddyphut 100
	MA( 12874,  20,  20,  35,                        Y), // 249 Ploddyphut 500
	MA( 16892,  20,  20,  35,                        Y), // 250 Flashbang X1
	MA( 21275,  20,  20,  99,                        Y), // 251 Juggerplane M1
	MA( 23832,  20,  20,  99,                        Y), // 252 Flashbang Wizzer
	MA( 13575,  20,  20,  40,                  T|A|S  ), // 253 Tricario Helicopter
	MA( 28215,  20,  20,  30,                  T|A|S  ), // 254 Guru X2 Helicopter
	MA( 13575,  20,  20,  99,                        Y), // 255 Powernaut Helicopter
};
#undef Y
#undef S
#undef A
#undef T
#undef MT
#undef MW
#undef MR
#undef MS
#undef MA

/**
 * Writes the properties of a rail vehicle into the RailVehicleInfo struct.
 * @see RailVehicleInfo
 * @param a image_index
 * @param b type
 * @param c cost_factor
 * @param d max_speed (1 unit = 1/1.6 mph = 1 km-ish/h)
 * @param e power (hp)
 * @param f weight (tons)
 * @param g running_cost
 * @param h running_cost_class
 * @param i capacity (persons, bags, tons, pieces, items, cubic metres, ...)
 * @param j railtype
 * @param k engclass
 * Tractive effort coefficient by default is the same as TTDPatch, 0.30*256=76
 * Air drag value depends on the top speed of the vehicle.
 */
#define RVI(a, b, c, d, e, f, g, h, i, j, k) { a, b, c, j, d, e, f, g, h, k, i, 0, 0, 0, VE_DEFAULT, 0, 76, 0, 0, 0 }
#define M RAILVEH_MULTIHEAD
#define W RAILVEH_WAGON
#define G RAILVEH_SINGLEHEAD
#define S EC_STEAM
#define D EC_DIESEL
#define E EC_ELECTRIC
#define N EC_MONORAIL
#define V EC_MAGLEV
/* Wagons always have engine type 0, i.e. steam. */
#define A EC_STEAM

#define R RAILTYPE_RAIL
#define C RAILTYPE_ELECTRIC
#define O RAILTYPE_MONO
#define L RAILTYPE_MAGLEV

#define RC_S PR_RUNNING_TRAIN_STEAM
#define RC_D PR_RUNNING_TRAIN_DIESEL
#define RC_E PR_RUNNING_TRAIN_ELECTRIC
#define RC_W INVALID_PRICE

static const RailVehicleInfo _orig_rail_vehicle_info[] = {
	/*   image_index  max_speed          running_cost      engclass
	 *   |  type      |        power       |  running_cost_class
	 *   |  |    cost_factor   |    weight |  |      capacity
	 *   |  |    |    |        |    |      |  |      |  railtype
	 *   |  |    |    |        |    |      |  |      |  |  | */
	/* Rail */
	RVI( 2, G,   7,  64,     300,  47,    50, RC_S,  0, R, S), //   0 Kirby Paul Tank (Steam)
	RVI(19, G,   8,  80,     600,  65,    65, RC_D,  0, R, D), //   1 MJS 250 (Diesel)
	RVI( 2, G,  10,  72,     400,  85,    90, RC_S,  0, R, S), //   2 Ploddyphut Choo-Choo
	RVI( 0, G,  15,  96,     900, 130,   130, RC_S,  0, R, S), //   3 Powernaut Choo-Choo
	RVI( 1, G,  19, 112,    1000, 140,   145, RC_S,  0, R, S), //   4 Mightymover Choo-Choo
	RVI(12, G,  16, 120,    1400,  95,   125, RC_D,  0, R, D), //   5 Ploddyphut Diesel
	RVI(14, G,  20, 152,    2000, 120,   135, RC_D,  0, R, D), //   6 Powernaut Diesel
	RVI( 3, G,  14,  88,    1100, 145,   130, RC_S,  0, R, S), //   7 Wills 2-8-0 (Steam)
	RVI( 0, G,  13, 112,    1000, 131,   120, RC_S,  0, R, S), //   8 Chaney 'Jubilee' (Steam)
	RVI( 1, G,  19, 128,    1200, 162,   140, RC_S,  0, R, S), //   9 Ginzu 'A4' (Steam)
	RVI( 0, G,  22, 144,    1600, 170,   130, RC_S,  0, R, S), //  10 SH '8P' (Steam)
	RVI( 8, M,  11, 112,     600,  32,    85, RC_D, 38, R, D), //  11 Manley-Morel DMU (Diesel)
	RVI(10, M,  14, 120,     700,  38,    70, RC_D, 40, R, D), //  12 'Dash' (Diesel)
	RVI( 4, G,  15, 128,    1250,  72,    95, RC_D,  0, R, D), //  13 SH/Hendry '25' (Diesel)
	RVI( 5, G,  17, 144,    1750, 101,   120, RC_D,  0, R, D), //  14 UU '37' (Diesel)
	RVI( 4, G,  18, 160,    2580, 112,   140, RC_D,  0, R, D), //  15 Floss '47' (Diesel)
	RVI(14, G,  23,  96,    4000, 150,   135, RC_D,  0, R, D), //  16 CS 4000 (Diesel)
	RVI(12, G,  16, 112,    2400, 120,   105, RC_D,  0, R, D), //  17 CS 2400 (Diesel)
	RVI(13, G,  30, 112,    6600, 207,   155, RC_D,  0, R, D), //  18 Centennial (Diesel)
	RVI(15, G,  18, 104,    1500, 110,   105, RC_D,  0, R, D), //  19 Kelling 3100 (Diesel)
	RVI(16, M,  35, 160,    3500,  95,   205, RC_D,  0, R, D), //  20 Turner Turbo (Diesel)
	RVI(18, G,  21, 104,    2200, 120,   145, RC_D,  0, R, D), //  21 MJS 1000 (Diesel)
	RVI( 6, M,  20, 200,    4500,  70,   190, RC_D,  4, R, D), //  22 SH '125' (Diesel)
	RVI(20, G,  26, 160,    3600,  84,   180, RC_E,  0, C, E), //  23 SH '30' (Electric)
	RVI(20, G,  30, 176,    5000,  82,   205, RC_E,  0, C, E), //  24 SH '40' (Electric)
	RVI(21, M,  40, 240,    7000,  90,   240, RC_E,  0, C, E), //  25 'T.I.M.' (Electric)
	RVI(23, M,  43, 264,    8000,  95,   250, RC_E,  0, C, E), //  26 'AsiaStar' (Electric)
	RVI(33, W, 247,   0,       0,  25,     0, RC_W, 40, R, A), //  27 Passenger Carriage
	RVI(35, W, 228,   0,       0,  21,     0, RC_W, 30, R, A), //  28 Mail Van
	RVI(34, W, 176,   0,       0,  18,     0, RC_W, 30, R, A), //  29 Coal Truck
	RVI(36, W, 200,   0,       0,  24,     0, RC_W, 30, R, A), //  30 Oil Tanker
	RVI(37, W, 192,   0,       0,  20,     0, RC_W, 25, R, A), //  31 Livestock Van
	RVI(38, W, 190,   0,       0,  21,     0, RC_W, 25, R, A), //  32 Goods Van
	RVI(39, W, 182,   0,       0,  19,     0, RC_W, 30, R, A), //  33 Grain Hopper
	RVI(40, W, 181,   0,       0,  16,     0, RC_W, 30, R, A), //  34 Wood Truck
	RVI(41, W, 179,   0,       0,  19,     0, RC_W, 30, R, A), //  35 Iron Ore Hopper
	RVI(42, W, 196,   0,       0,  18,     0, RC_W, 20, R, A), //  36 Steel Truck
	RVI(43, W, 255,   0,       0,  30,     0, RC_W, 20, R, A), //  37 Armoured Van
	RVI(44, W, 191,   0,       0,  22,     0, RC_W, 25, R, A), //  38 Food Van
	RVI(45, W, 196,   0,       0,  18,     0, RC_W, 20, R, A), //  39 Paper Truck
	RVI(46, W, 179,   0,       0,  19,     0, RC_W, 30, R, A), //  40 Copper Ore Hopper
	RVI(47, W, 199,   0,       0,  25,     0, RC_W, 25, R, A), //  41 Water Tanker
	RVI(48, W, 182,   0,       0,  18,     0, RC_W, 25, R, A), //  42 Fruit Truck
	RVI(49, W, 185,   0,       0,  19,     0, RC_W, 21, R, A), //  43 Rubber Truck
	RVI(50, W, 176,   0,       0,  19,     0, RC_W, 30, R, A), //  44 Sugar Truck
	RVI(51, W, 178,   0,       0,  20,     0, RC_W, 30, R, A), //  45 Candyfloss Hopper
	RVI(52, W, 192,   0,       0,  20,     0, RC_W, 30, R, A), //  46 Toffee Hopper
	RVI(53, W, 190,   0,       0,  21,     0, RC_W, 20, R, A), //  47 Bubble Van
	RVI(54, W, 182,   0,       0,  24,     0, RC_W, 25, R, A), //  48 Cola Tanker
	RVI(55, W, 181,   0,       0,  21,     0, RC_W, 25, R, A), //  49 Sweet Van
	RVI(56, W, 183,   0,       0,  21,     0, RC_W, 20, R, A), //  50 Toy Van
	RVI(57, W, 196,   0,       0,  18,     0, RC_W, 22, R, A), //  51 Battery Truck
	RVI(58, W, 193,   0,       0,  18,     0, RC_W, 25, R, A), //  52 Fizzy Drink Truck
	RVI(59, W, 191,   0,       0,  18,     0, RC_W, 30, R, A), //  53 Plastic Truck
	/* Monorail */
	RVI(25, G,  52, 304,    9000,  95,   230, RC_E,  0, O, N), //  54 'X2001' (Electric)
	RVI(26, M,  60, 336,   10000,  85,   240, RC_E, 25, O, N), //  55 'Millennium Z1' (Electric)
	RVI(26, G,  53, 320,    5000,  95,   230, RC_E,  0, O, N), //  56 Wizzowow Z99
	RVI(60, W, 247,   0,       0,  25,     0, RC_W, 45, O, A), //  57 Passenger Carriage
	RVI(62, W, 228,   0,       0,  21,     0, RC_W, 35, O, A), //  58 Mail Van
	RVI(61, W, 176,   0,       0,  18,     0, RC_W, 35, O, A), //  59 Coal Truck
	RVI(63, W, 200,   0,       0,  24,     0, RC_W, 35, O, A), //  60 Oil Tanker
	RVI(64, W, 192,   0,       0,  20,     0, RC_W, 30, O, A), //  61 Livestock Van
	RVI(65, W, 190,   0,       0,  21,     0, RC_W, 30, O, A), //  62 Goods Van
	RVI(66, W, 182,   0,       0,  19,     0, RC_W, 35, O, A), //  63 Grain Hopper
	RVI(67, W, 181,   0,       0,  16,     0, RC_W, 35, O, A), //  64 Wood Truck
	RVI(68, W, 179,   0,       0,  19,     0, RC_W, 35, O, A), //  65 Iron Ore Hopper
	RVI(69, W, 196,   0,       0,  18,     0, RC_W, 25, O, A), //  66 Steel Truck
	RVI(70, W, 255,   0,       0,  30,     0, RC_W, 25, O, A), //  67 Armoured Van
	RVI(71, W, 191,   0,       0,  22,     0, RC_W, 30, O, A), //  68 Food Van
	RVI(72, W, 196,   0,       0,  18,     0, RC_W, 25, O, A), //  69 Paper Truck
	RVI(73, W, 179,   0,       0,  19,     0, RC_W, 35, O, A), //  70 Copper Ore Hopper
	RVI(47, W, 199,   0,       0,  25,     0, RC_W, 30, O, A), //  71 Water Tanker
	RVI(48, W, 182,   0,       0,  18,     0, RC_W, 30, O, A), //  72 Fruit Truck
	RVI(49, W, 185,   0,       0,  19,     0, RC_W, 26, O, A), //  73 Rubber Truck
	RVI(50, W, 176,   0,       0,  19,     0, RC_W, 35, O, A), //  74 Sugar Truck
	RVI(51, W, 178,   0,       0,  20,     0, RC_W, 35, O, A), //  75 Candyfloss Hopper
	RVI(52, W, 192,   0,       0,  20,     0, RC_W, 35, O, A), //  76 Toffee Hopper
	RVI(53, W, 190,   0,       0,  21,     0, RC_W, 25, O, A), //  77 Bubble Van
	RVI(54, W, 182,   0,       0,  24,     0, RC_W, 30, O, A), //  78 Cola Tanker
	RVI(55, W, 181,   0,       0,  21,     0, RC_W, 30, O, A), //  79 Sweet Van
	RVI(56, W, 183,   0,       0,  21,     0, RC_W, 25, O, A), //  80 Toy Van
	RVI(57, W, 196,   0,       0,  18,     0, RC_W, 27, O, A), //  81 Battery Truck
	RVI(58, W, 193,   0,       0,  18,     0, RC_W, 30, O, A), //  82 Fizzy Drink Truck
	RVI(59, W, 191,   0,       0,  18,     0, RC_W, 35, O, A), //  83 Plastic Truck
	/* Maglev */
	RVI(28, G,  70, 400,   10000, 105,   250, RC_E,  0, L, V), //  84 Lev1 'Leviathan' (Electric)
	RVI(29, G,  74, 448,   12000, 120,   253, RC_E,  0, L, V), //  85 Lev2 'Cyclops' (Electric)
	RVI(30, G,  82, 480,   15000, 130,   254, RC_E,  0, L, V), //  86 Lev3 'Pegasus' (Electric)
	RVI(31, M,  95, 640,   20000, 150,   255, RC_E,  0, L, V), //  87 Lev4 'Chimaera' (Electric)
	RVI(28, G,  70, 480,   10000, 120,   250, RC_E,  0, L, V), //  88 Wizzowow Rocketeer
	RVI(60, W, 247,   0,       0,  25,     0, RC_W, 47, L, A), //  89 Passenger Carriage
	RVI(62, W, 228,   0,       0,  21,     0, RC_W, 37, L, A), //  90 Mail Van
	RVI(61, W, 176,   0,       0,  18,     0, RC_W, 37, L, A), //  91 Coal Truck
	RVI(63, W, 200,   0,       0,  24,     0, RC_W, 37, L, A), //  92 Oil Tanker
	RVI(64, W, 192,   0,       0,  20,     0, RC_W, 32, L, A), //  93 Livestock Van
	RVI(65, W, 190,   0,       0,  21,     0, RC_W, 32, L, A), //  94 Goods Van
	RVI(66, W, 182,   0,       0,  19,     0, RC_W, 37, L, A), //  95 Grain Hopper
	RVI(67, W, 181,   0,       0,  16,     0, RC_W, 37, L, A), //  96 Wood Truck
	RVI(68, W, 179,   0,       0,  19,     0, RC_W, 37, L, A), //  97 Iron Ore Hopper
	RVI(69, W, 196,   0,       0,  18,     0, RC_W, 27, L, A), //  98 Steel Truck
	RVI(70, W, 255,   0,       0,  30,     0, RC_W, 27, L, A), //  99 Armoured Van
	RVI(71, W, 191,   0,       0,  22,     0, RC_W, 32, L, A), // 100 Food Van
	RVI(72, W, 196,   0,       0,  18,     0, RC_W, 27, L, A), // 101 Paper Truck
	RVI(73, W, 179,   0,       0,  19,     0, RC_W, 37, L, A), // 102 Copper Ore Hopper
	RVI(47, W, 199,   0,       0,  25,     0, RC_W, 32, L, A), // 103 Water Tanker
	RVI(48, W, 182,   0,       0,  18,     0, RC_W, 32, L, A), // 104 Fruit Truck
	RVI(49, W, 185,   0,       0,  19,     0, RC_W, 28, L, A), // 105 Rubber Truck
	RVI(50, W, 176,   0,       0,  19,     0, RC_W, 37, L, A), // 106 Sugar Truck
	RVI(51, W, 178,   0,       0,  20,     0, RC_W, 37, L, A), // 107 Candyfloss Hopper
	RVI(52, W, 192,   0,       0,  20,     0, RC_W, 37, L, A), // 108 Toffee Hopper
	RVI(53, W, 190,   0,       0,  21,     0, RC_W, 27, L, A), // 109 Bubble Van
	RVI(54, W, 182,   0,       0,  24,     0, RC_W, 32, L, A), // 110 Cola Tanker
	RVI(55, W, 181,   0,       0,  21,     0, RC_W, 32, L, A), // 111 Sweet Van
	RVI(56, W, 183,   0,       0,  21,     0, RC_W, 27, L, A), // 112 Toy Van
	RVI(57, W, 196,   0,       0,  18,     0, RC_W, 29, L, A), // 113 Battery Truck
	RVI(58, W, 193,   0,       0,  18,     0, RC_W, 32, L, A), // 114 Fizzy Drink Truck
	RVI(59, W, 191,   0,       0,  18,     0, RC_W, 37, L, A), // 115 Plastic Truck
};
#undef RC_W
#undef RC_E
#undef RC_D
#undef RC_S
#undef L
#undef O
#undef C
#undef R
#undef V
#undef N
#undef E
#undef D
#undef S
#undef G
#undef W
#undef M
#undef RVI

/**
 * Writes the properties of a ship into the ShipVehicleInfo struct.
 * @see ShipVehicleInfo
 * @param a image_index
 * @param b cost_factor
 * @param c max_speed (1 unit = 1/3.2 mph = 0.5 km-ish/h)
 * @param d capacity (persons, bags, tons, pieces, items, cubic metres, ...)
 * @param e running_cost
 * @param f sound effect
 * @param g refittable
 */
#define SVI(a, b, c, d, e, f, g) { a, b, c, d, e, f, g, VE_DEFAULT, 0, 0 }
static const ShipVehicleInfo _orig_ship_vehicle_info[] = {
	/*   image_index    capacity                              refittable
	 *   |    cost_factor    running_cost                     |
	 *   |    |    max_speed |  sfx                           |
	 *   |    |    |    |    |  |                             | */
	SVI( 1, 160,  48, 220, 140, SND_06_DEPARTURE_CARGO_SHIP,  0 ), //  0 MPS Oil Tanker
	SVI( 1, 176,  80, 350, 125, SND_06_DEPARTURE_CARGO_SHIP,  0 ), //  1 CS-Inc. Oil Tanker
	SVI( 2,  96,  64, 100,  90, SND_07_DEPARTURE_FERRY,       0 ), //  2 MPS Passenger Ferry
	SVI( 2, 112, 128, 130,  80, SND_07_DEPARTURE_FERRY,       0 ), //  3 FFP Passenger Ferry
	SVI( 3, 148, 224, 100, 190, SND_07_DEPARTURE_FERRY,       0 ), //  4 Bakewell 300 Hovercraft
	SVI( 2,  96,  64, 100,  90, SND_07_DEPARTURE_FERRY,       0 ), //  5 Chugger-Chug Passenger Ferry
	SVI( 2, 112, 128, 130,  80, SND_07_DEPARTURE_FERRY,       0 ), //  6 Shivershake Passenger Ferry
	SVI( 0, 128,  48, 160, 150, SND_06_DEPARTURE_CARGO_SHIP,  1 ), //  7 Yate Cargo ship
	SVI( 0, 144,  80, 190, 113, SND_06_DEPARTURE_CARGO_SHIP,  1 ), //  8 Bakewell Cargo ship
	SVI( 0, 128,  48, 160, 150, SND_06_DEPARTURE_CARGO_SHIP,  1 ), //  9 Mightymover Cargo ship
	SVI( 0, 144,  80, 190, 113, SND_06_DEPARTURE_CARGO_SHIP,  1 ), // 10 Powernaut Cargo ship
};
#undef SVI

/**
 * Writes the properties of an aircraft into the AircraftVehicleInfo struct.
 * @see AircraftVehicleInfo
 * @param a image_index
 * @param b cost_factor
 * @param c running_Cost
 * @param d subtype (bit 0 - plane, bit 1 - large plane)
 * @param e sound effect
 * @param f acceleration (1 unit = 3/8 mph/tick = 3/5 km-ish/h/tick) (stays the same in the variable)
 * @param g max_speed (1 unit = 8 mph = 12.8 km-ish/h) (is converted to km-ish/h by the macro)
 * @param h mail_capacity (bags)
 * @param i passenger_capacity (persons)
 */
#define AVI(a, b, c, d, e, f, g, h, i) { a, b, c, d, e, f, (g * 128) / 10, h, i, 0 }
#define H AIR_HELI
#define P AIR_CTOL
#define J AIR_CTOL | AIR_FAST
static const AircraftVehicleInfo _orig_aircraft_vehicle_info[] = {
	/*    image_index         sfx                             acceleration
	 *    |   cost_factor     |                               |    max_speed
	 *    |   |    running_cost                               |    |   mail_capacity
	 *    |   |    |  subtype |                               |    |   |    passenger_capacity
	 *    |   |    |  |       |                               |    |   |    | */
	AVI(  1, 14,  85, P, SND_08_TAKEOFF_PROPELLER,           18,  37,  4,  25 ), //  0 Sampson U52
	AVI(  0, 15, 100, P, SND_08_TAKEOFF_PROPELLER,           20,  37,  8,  65 ), //  1 Coleman Count
	AVI(  2, 16, 130, J, SND_09_TAKEOFF_JET,                 35,  74, 10,  90 ), //  2 FFP Dart
	AVI(  8, 75, 250, J, SND_3B_TAKEOFF_JET_FAST,            50, 181, 20, 100 ), //  3 Yate Haugan
	AVI(  5, 15,  98, P, SND_08_TAKEOFF_PROPELLER,           20,  37,  6,  30 ), //  4 Bakewell Cotswald LB-3
	AVI(  6, 18, 240, J, SND_09_TAKEOFF_JET,                 40,  74, 30, 200 ), //  5 Bakewell Luckett LB-8
	AVI(  2, 17, 150, P, SND_09_TAKEOFF_JET,                 35,  74, 15, 100 ), //  6 Bakewell Luckett LB-9
	AVI(  2, 18, 245, J, SND_09_TAKEOFF_JET,                 40,  74, 30, 150 ), //  7 Bakewell Luckett LB80
	AVI(  3, 19, 192, J, SND_09_TAKEOFF_JET,                 40,  74, 40, 220 ), //  8 Bakewell Luckett LB-10
	AVI(  3, 20, 190, J, SND_09_TAKEOFF_JET,                 40,  74, 25, 230 ), //  9 Bakewell Luckett LB-11
	AVI(  2, 16, 135, J, SND_09_TAKEOFF_JET,                 35,  74, 10,  95 ), // 10 Yate Aerospace YAC 1-11
	AVI(  2, 18, 240, J, SND_09_TAKEOFF_JET,                 40,  74, 35, 170 ), // 11 Darwin 100
	AVI(  4, 17, 155, J, SND_09_TAKEOFF_JET,                 40,  74, 15, 110 ), // 12 Darwin 200
	AVI(  7, 30, 253, J, SND_3D_TAKEOFF_JET_BIG,             40,  74, 50, 300 ), // 13 Darwin 300
	AVI(  4, 18, 210, J, SND_09_TAKEOFF_JET,                 40,  74, 25, 200 ), // 14 Darwin 400
	AVI(  4, 19, 220, J, SND_09_TAKEOFF_JET,                 40,  74, 25, 240 ), // 15 Darwin 500
	AVI(  4, 27, 230, J, SND_09_TAKEOFF_JET,                 40,  74, 40, 260 ), // 16 Darwin 600
	AVI(  3, 25, 225, J, SND_09_TAKEOFF_JET,                 40,  74, 35, 240 ), // 17 Guru Galaxy
	AVI(  4, 20, 235, J, SND_09_TAKEOFF_JET,                 40,  74, 30, 260 ), // 18 Airtaxi A21
	AVI(  4, 19, 220, J, SND_09_TAKEOFF_JET,                 40,  74, 25, 210 ), // 19 Airtaxi A31
	AVI(  4, 18, 170, J, SND_09_TAKEOFF_JET,                 40,  74, 20, 160 ), // 20 Airtaxi A32
	AVI(  4, 26, 210, J, SND_09_TAKEOFF_JET,                 40,  74, 20, 220 ), // 21 Airtaxi A33
	AVI(  6, 16, 125, P, SND_09_TAKEOFF_JET,                 50,  74, 10,  80 ), // 22 Yate Aerospace YAe46
	AVI(  2, 17, 145, P, SND_09_TAKEOFF_JET,                 40,  74, 10,  85 ), // 23 Dinger 100
	AVI( 11, 16, 130, P, SND_09_TAKEOFF_JET,                 40,  74, 10,  75 ), // 24 AirTaxi A34-1000
	AVI( 10, 16, 149, P, SND_09_TAKEOFF_JET,                 40,  74, 10,  85 ), // 25 Yate Z-Shuttle
	AVI( 15, 17, 170, P, SND_09_TAKEOFF_JET,                 40,  74, 18,  65 ), // 26 Kelling K1
	AVI( 12, 18, 210, J, SND_09_TAKEOFF_JET,                 40,  74, 25, 110 ), // 27 Kelling K6
	AVI( 13, 20, 230, J, SND_09_TAKEOFF_JET,                 40,  74, 60, 180 ), // 28 Kelling K7
	AVI( 14, 21, 220, J, SND_09_TAKEOFF_JET,                 40,  74, 65, 150 ), // 29 Darwin 700
	AVI( 16, 19, 160, J, SND_09_TAKEOFF_JET,                 40, 181, 45,  85 ), // 30 FFP Hyperdart 2
	AVI( 17, 24, 248, J, SND_3D_TAKEOFF_JET_BIG,             40,  74, 80, 400 ), // 31 Dinger 200
	AVI( 18, 80, 251, J, SND_3B_TAKEOFF_JET_FAST,            50, 181, 45, 130 ), // 32 Dinger 1000
	AVI( 20, 13,  85, P, SND_45_TAKEOFF_PROPELLER_TOYLAND_1, 18,  37,  5,  25 ), // 33 Ploddyphut 100
	AVI( 21, 18, 100, P, SND_46_TAKEOFF_PROPELLER_TOYLAND_2, 20,  37,  9,  60 ), // 34 Ploddyphut 500
	AVI( 22, 25, 140, P, SND_09_TAKEOFF_JET,                 40,  74, 12,  90 ), // 35 Flashbang X1
	AVI( 23, 32, 220, J, SND_3D_TAKEOFF_JET_BIG,             40,  74, 40, 200 ), // 36 Juggerplane M1
	AVI( 24, 80, 255, J, SND_3B_TAKEOFF_JET_FAST,            50, 181, 30, 100 ), // 37 Flashbang Wizzer
	AVI(  9, 15,  81, H, SND_09_TAKEOFF_JET,                 20,  25, 15,  40 ), // 38 Tricario Helicopter
	AVI( 19, 17,  77, H, SND_09_TAKEOFF_JET,                 20,  40, 20,  55 ), // 39 Guru X2 Helicopter
	AVI( 25, 15,  80, H, SND_09_TAKEOFF_JET,                 20,  25, 10,  40 ), // 40 Powernaut Helicopter
};
#undef J
#undef P
#undef H
#undef AVI

/**
 * Writes the properties of a road vehicle into the RoadVehicleInfo struct.
 * @see RoadVehicleInfo
 * @param a image_index
 * @param b cost_factor
 * @param c running_cost
 * @param d sound effect
 * @param e max_speed (1 unit = 1/3.2 mph = 0.5 km-ish/h)
 * @param f capacity (persons, bags, tons, pieces, items, cubic metres, ...)
 * @param g weight (1/4 ton)
 * @param h power (10 hp)
 * Tractive effort coefficient by default is the same as TTDPatch, 0.30*256=76
 * Air drag value depends on the top speed of the vehicle.
 */
#define ROV(a, b, c, d, e, f, g, h) { a, b, c, PR_RUNNING_ROADVEH, d, e, f, g, h, 76, 0, VE_DEFAULT, 0, ROADTYPE_ROAD }
static const RoadVehicleInfo _orig_road_vehicle_info[] = {
	/*    image_index       sfx                            max_speed    power
	 *    |    cost_factor  |                              |   capacity |
	 *    |    |    running_cost                           |   |    weight
	 *    |    |    |       |                              |   |    |   |*/
	ROV(  0, 120,  91, SND_19_DEPARTURE_OLD_RV_1,        112, 31,  42,  9), //  0 MPS Regal Bus
	ROV( 17, 140, 128, SND_1C_DEPARTURE_OLD_BUS,         176, 35,  60, 12), //  1 Hereford Leopard Bus
	ROV( 17, 150, 178, SND_1B_DEPARTURE_MODERN_BUS,      224, 37,  70, 15), //  2 Foster Bus
	ROV( 34, 160, 240, SND_1B_DEPARTURE_MODERN_BUS,      255, 40, 100, 25), //  3 Foster MkII Superbus
	ROV( 51, 120,  91, SND_3C_DEPARTURE_BUS_TOYLAND_1,   112, 30,  42,  9), //  4 Ploddyphut MkI Bus
	ROV( 51, 140, 171, SND_3E_DEPARTURE_BUS_TOYLAND_2,   192, 35,  60, 15), //  5 Ploddyphut MkII Bus
	ROV( 51, 160, 240, SND_3C_DEPARTURE_BUS_TOYLAND_1,   240, 38,  90, 25), //  6 Ploddyphut MkIII Bus
	ROV(  1, 108,  90, SND_19_DEPARTURE_OLD_RV_1,         96, 20,  38, 12), //  7 Balogh Coal Truck
	ROV( 18, 128, 168, SND_19_DEPARTURE_OLD_RV_1,        176, 25,  48, 22), //  8 Uhl Coal Truck
	ROV( 35, 138, 240, SND_19_DEPARTURE_OLD_RV_1,        224, 28,  69, 45), //  9 DW Coal Truck
	ROV(  2, 115,  90, SND_19_DEPARTURE_OLD_RV_1,         96, 22,  38, 12), // 10 MPS Mail Truck
	ROV( 19, 135, 168, SND_19_DEPARTURE_OLD_RV_1,        176, 28,  48, 22), // 11 Reynard Mail Truck
	ROV( 36, 145, 240, SND_19_DEPARTURE_OLD_RV_1,        224, 30,  69, 45), // 12 Perry Mail Truck
	ROV( 57, 115,  90, SND_3E_DEPARTURE_BUS_TOYLAND_2,    96, 22,  38, 12), // 13 MightyMover Mail Truck
	ROV( 57, 135, 168, SND_3C_DEPARTURE_BUS_TOYLAND_1,   176, 28,  48, 22), // 14 Powernaught Mail Truck
	ROV( 57, 145, 240, SND_3E_DEPARTURE_BUS_TOYLAND_2,   224, 30,  69, 45), // 15 Wizzowow Mail Truck
	ROV(  3, 110,  90, SND_19_DEPARTURE_OLD_RV_1,         96, 21,  38, 12), // 16 Witcombe Oil Tanker
	ROV( 20, 140, 168, SND_19_DEPARTURE_OLD_RV_1,        176, 25,  48, 22), // 17 Foster Oil Tanker
	ROV( 37, 150, 240, SND_19_DEPARTURE_OLD_RV_1,        224, 27,  69, 45), // 18 Perry Oil Tanker
	ROV(  4, 105,  90, SND_19_DEPARTURE_OLD_RV_1,         96, 14,  38, 12), // 19 Talbott Livestock Van
	ROV( 21, 130, 168, SND_19_DEPARTURE_OLD_RV_1,        176, 16,  48, 22), // 20 Uhl Livestock Van
	ROV( 38, 140, 240, SND_19_DEPARTURE_OLD_RV_1,        224, 18,  69, 45), // 21 Foster Livestock Van
	ROV(  5, 107,  90, SND_19_DEPARTURE_OLD_RV_1,         96, 14,  38, 12), // 22 Balogh Goods Truck
	ROV( 22, 130, 168, SND_19_DEPARTURE_OLD_RV_1,        176, 16,  48, 22), // 23 Craighead Goods Truck
	ROV( 39, 140, 240, SND_19_DEPARTURE_OLD_RV_1,        224, 18,  69, 45), // 24 Goss Goods Truck
	ROV(  6, 114,  90, SND_19_DEPARTURE_OLD_RV_1,         96, 20,  38, 12), // 25 Hereford Grain Truck
	ROV( 23, 133, 168, SND_19_DEPARTURE_OLD_RV_1,        176, 25,  48, 22), // 26 Thomas Grain Truck
	ROV( 40, 143, 240, SND_19_DEPARTURE_OLD_RV_1,        224, 30,  69, 45), // 27 Goss Grain Truck
	ROV(  7, 118,  90, SND_19_DEPARTURE_OLD_RV_1,         96, 20,  38, 12), // 28 Witcombe Wood Truck
	ROV( 24, 137, 168, SND_19_DEPARTURE_OLD_RV_1,        176, 22,  48, 22), // 29 Foster Wood Truck
	ROV( 41, 147, 240, SND_19_DEPARTURE_OLD_RV_1,        224, 24,  69, 45), // 30 Moreland Wood Truck
	ROV(  8, 121,  90, SND_19_DEPARTURE_OLD_RV_1,         96, 22,  38, 12), // 31 MPS Iron Ore Truck
	ROV( 25, 140, 168, SND_19_DEPARTURE_OLD_RV_1,        176, 25,  48, 22), // 32 Uhl Iron Ore Truck
	ROV( 42, 150, 240, SND_19_DEPARTURE_OLD_RV_1,        224, 27,  69, 45), // 33 Chippy Iron Ore Truck
	ROV(  9, 112,  90, SND_19_DEPARTURE_OLD_RV_1,         96, 15,  38, 12), // 34 Balogh Steel Truck
	ROV( 26, 135, 168, SND_19_DEPARTURE_OLD_RV_1,        176, 18,  48, 22), // 35 Uhl Steel Truck
	ROV( 43, 145, 240, SND_19_DEPARTURE_OLD_RV_1,        224, 20,  69, 45), // 36 Kelling Steel Truck
	ROV( 10, 145,  90, SND_19_DEPARTURE_OLD_RV_1,         96, 12,  38, 12), // 37 Balogh Armoured Truck
	ROV( 27, 170, 168, SND_19_DEPARTURE_OLD_RV_1,        176, 15,  48, 22), // 38 Uhl Armoured Truck
	ROV( 44, 180, 240, SND_19_DEPARTURE_OLD_RV_1,        224, 16,  69, 45), // 39 Foster Armoured Truck
	ROV( 11, 112,  90, SND_19_DEPARTURE_OLD_RV_1,         96, 17,  38, 12), // 40 Foster Food Van
	ROV( 28, 134, 168, SND_19_DEPARTURE_OLD_RV_1,        176, 20,  48, 22), // 41 Perry Food Van
	ROV( 45, 144, 240, SND_19_DEPARTURE_OLD_RV_1,        224, 22,  69, 45), // 42 Chippy Food Van
	ROV( 12, 112,  90, SND_19_DEPARTURE_OLD_RV_1,         96, 15,  38, 12), // 43 Uhl Paper Truck
	ROV( 29, 135, 168, SND_19_DEPARTURE_OLD_RV_1,        176, 18,  48, 22), // 44 Balogh Paper Truck
	ROV( 46, 145, 240, SND_19_DEPARTURE_OLD_RV_1,        224, 20,  69, 45), // 45 MPS Paper Truck
	ROV( 13, 121,  90, SND_19_DEPARTURE_OLD_RV_1,         96, 22,  38, 12), // 46 MPS Copper Ore Truck
	ROV( 30, 140, 168, SND_19_DEPARTURE_OLD_RV_1,        176, 25,  48, 22), // 47 Uhl Copper Ore Truck
	ROV( 47, 150, 240, SND_19_DEPARTURE_OLD_RV_1,        224, 27,  69, 45), // 48 Goss Copper Ore Truck
	ROV( 14, 111,  90, SND_19_DEPARTURE_OLD_RV_1,         96, 21,  38, 12), // 49 Uhl Water Tanker
	ROV( 31, 141, 168, SND_19_DEPARTURE_OLD_RV_1,        176, 25,  48, 22), // 50 Balogh Water Tanker
	ROV( 48, 151, 240, SND_19_DEPARTURE_OLD_RV_1,        224, 27,  69, 45), // 51 MPS Water Tanker
	ROV( 15, 118,  90, SND_19_DEPARTURE_OLD_RV_1,         96, 18,  38, 12), // 52 Balogh Fruit Truck
	ROV( 32, 148, 168, SND_19_DEPARTURE_OLD_RV_1,        176, 20,  48, 22), // 53 Uhl Fruit Truck
	ROV( 49, 158, 240, SND_19_DEPARTURE_OLD_RV_1,        224, 23,  69, 45), // 54 Kelling Fruit Truck
	ROV( 16, 117,  90, SND_19_DEPARTURE_OLD_RV_1,         96, 17,  38, 12), // 55 Balogh Rubber Truck
	ROV( 33, 147, 168, SND_19_DEPARTURE_OLD_RV_1,        176, 19,  48, 22), // 56 Uhl Rubber Truck
	ROV( 50, 157, 240, SND_19_DEPARTURE_OLD_RV_1,        224, 22,  69, 45), // 57 RMT Rubber Truck
	ROV( 52, 117,  90, SND_3F_DEPARTURE_TRUCK_TOYLAND_1,  96, 17,  38, 12), // 58 MightyMover Sugar Truck
	ROV( 52, 147, 168, SND_40_DEPARTURE_TRUCK_TOYLAND_2, 176, 19,  48, 22), // 59 Powernaught Sugar Truck
	ROV( 52, 157, 240, SND_3F_DEPARTURE_TRUCK_TOYLAND_1, 224, 22,  69, 45), // 60 Wizzowow Sugar Truck
	ROV( 53, 117,  90, SND_40_DEPARTURE_TRUCK_TOYLAND_2,  96, 17,  38, 12), // 61 MightyMover Cola Truck
	ROV( 53, 147, 168, SND_3F_DEPARTURE_TRUCK_TOYLAND_1, 176, 19,  48, 22), // 62 Powernaught Cola Truck
	ROV( 53, 157, 240, SND_40_DEPARTURE_TRUCK_TOYLAND_2, 224, 22,  69, 45), // 63 Wizzowow Cola Truck
	ROV( 54, 117,  90, SND_3F_DEPARTURE_TRUCK_TOYLAND_1,  96, 17,  38, 12), // 64 MightyMover Candyfloss Truck
	ROV( 54, 147, 168, SND_40_DEPARTURE_TRUCK_TOYLAND_2, 176, 19,  48, 22), // 65 Powernaught Candyfloss Truck
	ROV( 54, 157, 240, SND_3F_DEPARTURE_TRUCK_TOYLAND_1, 224, 22,  69, 45), // 66 Wizzowow Candyfloss Truck
	ROV( 55, 117,  90, SND_40_DEPARTURE_TRUCK_TOYLAND_2,  96, 17,  38, 12), // 67 MightyMover Toffee Truck
	ROV( 55, 147, 168, SND_3F_DEPARTURE_TRUCK_TOYLAND_1, 176, 19,  48, 22), // 68 Powernaught Toffee Truck
	ROV( 55, 157, 240, SND_40_DEPARTURE_TRUCK_TOYLAND_2, 224, 22,  69, 45), // 69 Wizzowow Toffee Truck
	ROV( 56, 117,  90, SND_3F_DEPARTURE_TRUCK_TOYLAND_1,  96, 17,  38, 12), // 70 MightyMover Toy Van
	ROV( 56, 147, 168, SND_40_DEPARTURE_TRUCK_TOYLAND_2, 176, 19,  48, 22), // 71 Powernaught Toy Van
	ROV( 56, 157, 240, SND_3F_DEPARTURE_TRUCK_TOYLAND_1, 224, 22,  69, 45), // 72 Wizzowow Toy Van
	ROV( 58, 117,  90, SND_40_DEPARTURE_TRUCK_TOYLAND_2,  96, 17,  38, 12), // 73 MightyMover Sweet Truck
	ROV( 58, 147, 168, SND_3F_DEPARTURE_TRUCK_TOYLAND_1, 176, 19,  48, 22), // 74 Powernaught Sweet Truck
	ROV( 58, 157, 240, SND_40_DEPARTURE_TRUCK_TOYLAND_2, 224, 22,  69, 45), // 75 Wizzowow Sweet Truck
	ROV( 59, 117,  90, SND_3F_DEPARTURE_TRUCK_TOYLAND_1,  96, 17,  38, 12), // 76 MightyMover Battery Truck
	ROV( 59, 147, 168, SND_40_DEPARTURE_TRUCK_TOYLAND_2, 176, 19,  48, 22), // 77 Powernaught Battery Truck
	ROV( 59, 157, 240, SND_3F_DEPARTURE_TRUCK_TOYLAND_1, 224, 22,  69, 45), // 78 Wizzowow Battery Truck
	ROV( 60, 117,  90, SND_40_DEPARTURE_TRUCK_TOYLAND_2,  96, 17,  38, 12), // 79 MightyMover Fizzy Drink Truck
	ROV( 60, 147, 168, SND_3F_DEPARTURE_TRUCK_TOYLAND_1, 176, 19,  48, 22), // 80 Powernaught Fizzy Drink Truck
	ROV( 60, 157, 240, SND_40_DEPARTURE_TRUCK_TOYLAND_2, 224, 22,  69, 45), // 81 Wizzowow Fizzy Drink Truck
	ROV( 61, 117,  90, SND_3F_DEPARTURE_TRUCK_TOYLAND_1,  96, 17,  38, 12), // 82 MightyMover Plastic Truck
	ROV( 61, 147, 168, SND_40_DEPARTURE_TRUCK_TOYLAND_2, 176, 19,  48, 22), // 83 Powernaught Plastic Truck
	ROV( 61, 157, 240, SND_3F_DEPARTURE_TRUCK_TOYLAND_1, 224, 22,  69, 45), // 84 Wizzowow Plastic Truck
	ROV( 62, 117,  90, SND_40_DEPARTURE_TRUCK_TOYLAND_2,  96, 17,  38, 12), // 85 MightyMover Bubble Truck
	ROV( 62, 147, 168, SND_3F_DEPARTURE_TRUCK_TOYLAND_1, 176, 19,  48, 22), // 86 Powernaught Bubble Truck
	ROV( 62, 157, 240, SND_40_DEPARTURE_TRUCK_TOYLAND_2, 224, 22,  69, 45), // 87 Wizzowow Bubble Truck
};
#undef ROV

#endif /* ENGINES_H */
