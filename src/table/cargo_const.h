/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file cargo_const.h Table of all default cargo types */

/** Construction macros for the #CargoSpec StringID entries. */
#define MK_STR_CARGO_PLURAL(label_plural) STR_CARGO_PLURAL_ ## label_plural
#define MK_STR_CARGO_SINGULAR(label_singular) STR_CARGO_SINGULAR_ ## label_singular
#define MK_STR_QUANTITY(label_plural) STR_QUANTITY_ ## label_plural
#define MK_STR_ABBREV(label_plural) STR_ABBREV_ ## label_plural
/** Construction macros for the #CargoSpec SpriteID entry. */
#define MK_SPRITE(label_plural) SPR_CARGO_ ## label_plural

/**
 * Construction macro for a #CargoSpec structure.
 * The order of arguments matches the order in which they are defined in #CargoSpec.
 * Some macros are used to automatically expand to the correct StringID consts, this
 * means that adding/changing a cargo spec requires updating of the following strings:
 * - STR_CARGO_PLURAL_<str_plural>
 * - STR_CARGO_SINGULAR_<str_singular>
 * - STR_QUANTITY_<str_plural>
 * - STR_ABBREV_<str_plural>
 * And the following sprite:
 * - SPR_CARGO_<str_plural>
 *
 * @param bt           Cargo bit number, is #INVALID_CARGO for a non-used spec.
 * @param label        Unique label of the cargo type.
 * @param colour       CargoSpec->legend_colour and CargoSpec->rating_colour.
 * @param weight       Weight of a single unit of this cargo type in 1/16 ton (62.5 kg).
 * @param mult         Capacity multiplier for vehicles. (8 fractional bits).
 * @param ip           CargoSpec->initial_payment.
 * @param td1          CargoSpec->transit_days[0].
 * @param td2          CargoSpec->transit_days[1].
 * @param age          Number of ticks before in-transit cargo waiting in a station is aged (CargoSpec->cargo_station_age_period).
 * @param freight      Cargo type is considered to be freight (affects train freight multiplier).
 * @param te           The effect that delivering this cargo type has on towns. Also affects destination of subsidies.
 * @param str_plural   The name suffix used to populate CargoSpec->name, CargoSpec->quantifier,
 *                     CargoSpec->abbrev and CargoSpec->sprite. See above for more detailed information.
 * @param str_singular The name suffix used to populate CargoSpec->name_single. See above for more information.
 * @param str_volume   Name of a single unit of cargo of this type.
 * @param classes      Classes of this cargo type. @see CargoClass
 */
#define MK(bt, label, colour, weight, mult, ip, td1, td2, age, freight, te, str_plural, str_singular, str_volume, classes) \
		{bt, label, colour, colour, weight, mult, ip, {td1, td2}, age, {255, 31}, freight, te, 0, 0, \
		MK_STR_CARGO_PLURAL(str_plural), MK_STR_CARGO_SINGULAR(str_singular), str_volume, MK_STR_QUANTITY(str_plural), MK_STR_ABBREV(str_plural), \
		MK_SPRITE(str_plural), classes, nullptr, nullptr, 0}

/** Cargo types available by default. */
static const CargoSpec _default_cargo[] = {
	MK(   0, 'PASS', 152,  1, 0x400, 3185,  0,  24, 2 * CARGO_AGING_TICKS, false, TE_PASSENGERS,   PASSENGERS,    PASSENGER, STR_PASSENGERS, CC_PASSENGERS),
	MK(   1, 'COAL',   6, 16, 0x100, 5916,  7, 255, 4 * CARGO_AGING_TICKS,  true,       TE_NONE,         COAL,         COAL,       STR_TONS, CC_BULK),
	MK(   2, 'MAIL',  15,  4, 0x200, 4550, 20,  90, 2 * CARGO_AGING_TICKS, false,       TE_MAIL,         MAIL,         MAIL,       STR_BAGS, CC_MAIL),
	/* Oil in temperate and arctic */
	MK(   3, 'OIL_', 174, 16, 0x100, 4437, 25, 255, 4 * CARGO_AGING_TICKS,  true,       TE_NONE,          OIL,          OIL,     STR_LITERS, CC_LIQUID),
	/* Oil in subtropic */
	MK(   3, 'OIL_', 174, 16, 0x100, 4892, 25, 255, 4 * CARGO_AGING_TICKS,  true,       TE_NONE,          OIL,          OIL,     STR_LITERS, CC_LIQUID),
	MK(   4, 'LVST', 208,  3, 0x100, 4322,  4,  18, 4 * CARGO_AGING_TICKS,  true,       TE_NONE,    LIVESTOCK,    LIVESTOCK,      STR_ITEMS, CC_PIECE_GOODS),
	MK(   5, 'GOOD', 194,  8, 0x200, 6144,  5,  28, 4 * CARGO_AGING_TICKS,  true,      TE_GOODS,        GOODS,        GOODS,     STR_CRATES, CC_EXPRESS),
	MK(   6, 'GRAI', 191, 16, 0x100, 4778,  4,  40, 4 * CARGO_AGING_TICKS,  true,       TE_NONE,        GRAIN,        GRAIN,       STR_TONS, CC_BULK),
	MK(   6, 'WHEA', 191, 16, 0x100, 4778,  4,  40, 4 * CARGO_AGING_TICKS,  true,       TE_NONE,        WHEAT,        WHEAT,       STR_TONS, CC_BULK),
	MK(   6, 'MAIZ', 191, 16, 0x100, 4322,  4,  40, 4 * CARGO_AGING_TICKS,  true,       TE_NONE,        MAIZE,        MAIZE,       STR_TONS, CC_BULK),
	/* Wood in temperate and arctic */
	MK(   7, 'WOOD',  84, 16, 0x100, 5005, 15, 255, 4 * CARGO_AGING_TICKS,  true,       TE_NONE,         WOOD,         WOOD,       STR_TONS, CC_PIECE_GOODS),
	/* Wood in subtropic */
	MK(   7, 'WOOD',  84, 16, 0x100, 7964, 15, 255, 4 * CARGO_AGING_TICKS,  true,       TE_NONE,         WOOD,         WOOD,       STR_TONS, CC_PIECE_GOODS),
	MK(   8, 'IORE', 184, 16, 0x100, 5120,  9, 255, 4 * CARGO_AGING_TICKS,  true,       TE_NONE,     IRON_ORE,     IRON_ORE,       STR_TONS, CC_BULK),
	MK(   9, 'STEL',  10, 16, 0x100, 5688,  7, 255, 4 * CARGO_AGING_TICKS,  true,       TE_NONE,        STEEL,        STEEL,       STR_TONS, CC_PIECE_GOODS),
	MK(  10, 'VALU', 202,  2, 0x100, 7509,  1,  32, 4 * CARGO_AGING_TICKS,  true,       TE_NONE,    VALUABLES,    VALUABLES,       STR_BAGS, CC_ARMOURED),
	MK(  10, 'GOLD', 202,  8, 0x100, 5802, 10,  40, 4 * CARGO_AGING_TICKS,  true,       TE_NONE,         GOLD,         GOLD,       STR_BAGS, CC_ARMOURED),
	MK(  10, 'DIAM', 202,  2, 0x100, 5802, 10, 255, 4 * CARGO_AGING_TICKS,  true,       TE_NONE,     DIAMONDS,      DIAMOND,       STR_BAGS, CC_ARMOURED),
	MK(  11, 'PAPR',  10, 16, 0x100, 5461,  7,  60, 4 * CARGO_AGING_TICKS,  true,       TE_NONE,        PAPER,        PAPER,       STR_TONS, CC_PIECE_GOODS),
	MK(  12, 'FOOD',  48, 16, 0x100, 5688,  0,  30, 4 * CARGO_AGING_TICKS,  true,       TE_FOOD,         FOOD,         FOOD,       STR_TONS, CC_EXPRESS | CC_REFRIGERATED),
	MK(  13, 'FRUT', 208, 16, 0x100, 4209,  0,  15, 4 * CARGO_AGING_TICKS,  true,       TE_NONE,        FRUIT,        FRUIT,       STR_TONS, CC_BULK | CC_REFRIGERATED),
	MK(  14, 'CORE', 184, 16, 0x100, 4892, 12, 255, 4 * CARGO_AGING_TICKS,  true,       TE_NONE,   COPPER_ORE,   COPPER_ORE,       STR_TONS, CC_BULK),
	MK(  15, 'WATR',  10, 16, 0x100, 4664, 20,  80, 4 * CARGO_AGING_TICKS,  true,      TE_WATER,        WATER,        WATER,     STR_LITERS, CC_LIQUID),
	MK(  16, 'RUBR',   6, 16, 0x100, 4437,  2,  20, 4 * CARGO_AGING_TICKS,  true,       TE_NONE,       RUBBER,       RUBBER,     STR_LITERS, CC_LIQUID),
	MK(  17, 'SUGR',   6, 16, 0x100, 4437, 20, 255, 4 * CARGO_AGING_TICKS,  true,       TE_NONE,        SUGAR,        SUGAR,       STR_TONS, CC_BULK),
	MK(  18, 'TOYS', 174,  2, 0x100, 5574, 25, 255, 4 * CARGO_AGING_TICKS,  true,       TE_NONE,         TOYS,          TOY,      STR_ITEMS, CC_PIECE_GOODS),
	MK(  19, 'BATT', 208,  4, 0x100, 4322,  2,  30, 4 * CARGO_AGING_TICKS,  true,       TE_NONE,    BATTERIES,      BATTERY,      STR_ITEMS, CC_PIECE_GOODS),
	MK(  20, 'SWET', 194,  5, 0x200, 6144,  8,  40, 4 * CARGO_AGING_TICKS,  true,      TE_GOODS,       SWEETS,       SWEETS,       STR_BAGS, CC_EXPRESS),
	MK(  21, 'TOFF', 191, 16, 0x100, 4778, 14,  60, 4 * CARGO_AGING_TICKS,  true,       TE_NONE,       TOFFEE,       TOFFEE,       STR_TONS, CC_BULK),
	MK(  22, 'COLA',  84, 16, 0x100, 4892,  5,  75, 4 * CARGO_AGING_TICKS,  true,       TE_NONE,         COLA,         COLA,     STR_LITERS, CC_LIQUID),
	MK(  23, 'CTCD', 184, 16, 0x100, 5005, 10,  25, 4 * CARGO_AGING_TICKS,  true,       TE_NONE,   CANDYFLOSS,   CANDYFLOSS,       STR_TONS, CC_BULK),
	MK(  24, 'BUBL',  10,  1, 0x100, 5077, 20,  80, 4 * CARGO_AGING_TICKS,  true,       TE_NONE,      BUBBLES,       BUBBLE,      STR_ITEMS, CC_PIECE_GOODS),
	MK(  25, 'PLST', 202, 16, 0x100, 4664, 30, 255, 4 * CARGO_AGING_TICKS,  true,       TE_NONE,      PLASTIC,      PLASTIC,     STR_LITERS, CC_LIQUID),
	MK(  26, 'FZDR',  48,  2, 0x100, 6250, 30,  50, 4 * CARGO_AGING_TICKS,  true,       TE_FOOD, FIZZY_DRINKS,  FIZZY_DRINK,      STR_ITEMS, CC_PIECE_GOODS),

	/* Void slot in temperate */
	MK(0xFF,      0,   1,  0, 0x100, 5688,  0,  30, 4 * CARGO_AGING_TICKS,  true,       TE_NONE,      NOTHING,      NOTHING,       STR_TONS, CC_NOAVAILABLE),
	/* Void slot in arctic */
	MK(0xFF,      0, 184,  0, 0x100, 5120,  9, 255, 4 * CARGO_AGING_TICKS,  true,       TE_NONE,      NOTHING,      NOTHING,       STR_TONS, CC_NOAVAILABLE),
};


/** Table of cargo types available in each climate, by default */
static const CargoLabel _default_climate_cargo[NUM_LANDSCAPE][12] = {
	{ 'PASS', 'COAL', 'MAIL', 'OIL_', 'LVST', 'GOOD', 'GRAI', 'WOOD', 'IORE', 'STEL', 'VALU',     33, },
	{ 'PASS', 'COAL', 'MAIL', 'OIL_', 'LVST', 'GOOD', 'WHEA', 'WOOD',     34, 'PAPR', 'GOLD', 'FOOD', },
	{ 'PASS', 'RUBR', 'MAIL',      4, 'FRUT', 'GOOD', 'MAIZ',     11, 'CORE', 'WATR', 'DIAM', 'FOOD', },
	{ 'PASS', 'SUGR', 'MAIL', 'TOYS', 'BATT', 'SWET', 'TOFF', 'COLA', 'CTCD', 'BUBL', 'PLST', 'FZDR', },
};

