/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file build_vehicle_gui.cpp GUI for building vehicles. */

#include "stdafx.h"
#include "engine_base.h"
#include "engine_func.h"
#include "station_base.h"
#include "network/network.h"
#include "articulated_vehicles.h"
#include "textbuf_gui.h"
#include "command_func.h"
#include "company_func.h"
#include "vehicle_gui.h"
#include "newgrf_engine.h"
#include "newgrf_text.h"
#include "group.h"
#include "string_func.h"
#include "strings_func.h"
#include "window_func.h"
#include "date_func.h"
#include "vehicle_func.h"
#include "widgets/dropdown_func.h"
#include "engine_gui.h"
#include "cargotype.h"
#include "core/geometry_func.hpp"
#include "autoreplace_func.h"

#include "widgets/build_vehicle_widget.h"

#include "table/strings.h"

#include "safeguards.h"

/**
 * Get the height of a single 'entry' in the engine lists.
 * @param type the vehicle type to get the height of
 * @return the height for the entry
 */
uint GetEngineListHeight(VehicleType type)
{
	return max<uint>(FONT_HEIGHT_NORMAL + WD_MATRIX_TOP + WD_MATRIX_BOTTOM, GetVehicleImageCellSize(type, EIT_PURCHASE).height);
}

static const NWidgetPart _nested_build_vehicle_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_BV_CAPTION), SetDataTip(STR_WHITE_STRING, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY),
		NWidget(NWID_VERTICAL),
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_BV_SORT_ASCENDING_DESCENDING), SetDataTip(STR_BUTTON_SORT_BY, STR_TOOLTIP_SORT_ORDER),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_BV_SORT_DROPDOWN), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_JUST_STRING, STR_TOOLTIP_SORT_CRITERIA),
			EndContainer(),
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BV_SHOW_HIDDEN_ENGINES),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_BV_CARGO_FILTER_DROPDOWN), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_JUST_STRING, STR_TOOLTIP_FILTER_CRITERIA),
			EndContainer(),
		EndContainer(),
	EndContainer(),
	/* Vehicle list. */
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_MATRIX, COLOUR_GREY, WID_BV_LIST), SetResize(1, 1), SetFill(1, 0), SetMatrixDataTip(1, 0, STR_NULL), SetScrollbar(WID_BV_SCROLLBAR),
		NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_BV_SCROLLBAR),
	EndContainer(),
	/* Panel with details. */
	NWidget(WWT_PANEL, COLOUR_GREY, WID_BV_PANEL), SetMinimalSize(240, 122), SetResize(1, 0), EndContainer(),
	/* Build/rename buttons, resize button. */
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BV_BUILD_SEL),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_BV_BUILD), SetResize(1, 0), SetFill(1, 0),
		EndContainer(),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_BV_SHOW_HIDE), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_JUST_STRING, STR_NULL),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_BV_RENAME), SetResize(1, 0), SetFill(1, 0),
		NWidget(WWT_RESIZEBOX, COLOUR_GREY),
	EndContainer(),
};

/** Special cargo filter criteria */
static const CargoID CF_ANY  = CT_NO_REFIT; ///< Show all vehicles independent of carried cargo (i.e. no filtering)
static const CargoID CF_NONE = CT_INVALID;  ///< Show only vehicles which do not carry cargo (e.g. train engines)

bool _engine_sort_direction; ///< \c false = descending, \c true = ascending.
byte _engine_sort_last_criteria[]       = {0, 0, 0, 0};                 ///< Last set sort criteria, for each vehicle type.
bool _engine_sort_last_order[]          = {false, false, false, false}; ///< Last set direction of the sort order, for each vehicle type.
bool _engine_sort_show_hidden_engines[] = {false, false, false, false}; ///< Last set 'show hidden engines' setting for each vehicle type.
static CargoID _engine_sort_last_cargo_criteria[] = {CF_ANY, CF_ANY, CF_ANY, CF_ANY}; ///< Last set filter criteria, for each vehicle type.

/**
 * Determines order of engines by engineID
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool EngineNumberSorter(const EngineID &a, const EngineID &b)
{
	int r = Engine::Get(a)->list_position - Engine::Get(b)->list_position;

	return _engine_sort_direction ? r > 0 : r < 0;
}

/**
 * Determines order of engines by introduction date
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool EngineIntroDateSorter(const EngineID &a, const EngineID &b)
{
	const int va = Engine::Get(a)->intro_date;
	const int vb = Engine::Get(b)->intro_date;
	const int r = va - vb;

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b);
	return _engine_sort_direction ? r > 0 : r < 0;
}

/* cached values for EngineNameSorter to spare many GetString() calls */
static EngineID _last_engine[2] = { INVALID_ENGINE, INVALID_ENGINE };

/**
 * Determines order of engines by name
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool EngineNameSorter(const EngineID &a, const EngineID &b)
{
	static char     last_name[2][64] = { "", "" };

	if (a != _last_engine[0]) {
		_last_engine[0] = a;
		SetDParam(0, a);
		GetString(last_name[0], STR_ENGINE_NAME, lastof(last_name[0]));
	}

	if (b != _last_engine[1]) {
		_last_engine[1] = b;
		SetDParam(0, b);
		GetString(last_name[1], STR_ENGINE_NAME, lastof(last_name[1]));
	}

	int r = strnatcmp(last_name[0], last_name[1]); // Sort by name (natural sorting).

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b);
	return _engine_sort_direction ? r > 0 : r < 0;
}

/**
 * Determines order of engines by reliability
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool EngineReliabilitySorter(const EngineID &a, const EngineID &b)
{
	const int va = Engine::Get(a)->reliability;
	const int vb = Engine::Get(b)->reliability;
	const int r = va - vb;

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b);
	return _engine_sort_direction ? r > 0 : r < 0;
}

/**
 * Determines order of engines by purchase cost
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool EngineCostSorter(const EngineID &a, const EngineID &b)
{
	const Money va = Engine::Get(a)->GetCost();
	const Money vb = Engine::Get(b)->GetCost();
	const Money r = va - vb;

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b);
	return _engine_sort_direction ? r > 0 : r < 0;
}

/**
 * Determines order of engines by speed
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool EngineSpeedSorter(const EngineID &a, const EngineID &b)
{
	int va = Engine::Get(a)->GetDisplayMaxSpeed();
	int vb = Engine::Get(b)->GetDisplayMaxSpeed();
	int r = va - vb;

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b);
	return _engine_sort_direction ? r > 0 : r < 0;
}

/**
 * Determines order of engines by power
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool EnginePowerSorter(const EngineID &a, const EngineID &b)
{
	int va = Engine::Get(a)->GetPower();
	int vb = Engine::Get(b)->GetPower();
	int r = va - vb;

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b);
	return _engine_sort_direction ? r > 0 : r < 0;
}

/**
 * Determines order of engines by tractive effort
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool EngineTractiveEffortSorter(const EngineID &a, const EngineID &b)
{
	int va = Engine::Get(a)->GetDisplayMaxTractiveEffort();
	int vb = Engine::Get(b)->GetDisplayMaxTractiveEffort();
	int r = va - vb;

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b);
	return _engine_sort_direction ? r > 0 : r < 0;
}

/**
 * Determines order of engines by running costs
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool EngineRunningCostSorter(const EngineID &a, const EngineID &b)
{
	const Money va = Engine::Get(a)->GetRunningCost();
	const Money vb = Engine::Get(b)->GetRunningCost();
	const Money r = va - vb;

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b);
	return _engine_sort_direction ? r > 0 : r < 0;
}

/**
 * Determines order of engines by running costs
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool EnginePowerVsRunningCostSorter(const EngineID &a, const EngineID &b)
{
	const Engine *e_a = Engine::Get(a);
	const Engine *e_b = Engine::Get(b);
	const uint p_a = e_a->GetPower();
	const uint p_b = e_b->GetPower();
	const Money r_a = e_a->GetRunningCost();
	const Money r_b = e_b->GetRunningCost();
	/* Check if running cost is zero in one or both engines.
	 * If only one of them is zero then that one has higher value,
	 * else if both have zero cost then compare powers. */
	if (r_a == 0) {
		if (r_b == 0) {
			/* If it is ambiguous which to return go with their ID */
			if (p_a == p_b) return EngineNumberSorter(a, b);
			return _engine_sort_direction != (p_a < p_b);
		}
		return !_engine_sort_direction;
	}
	if (r_b == 0) return _engine_sort_direction;
	/* Using double for more precision when comparing close values.
	 * This shouldn't have any major effects in performance nor in keeping
	 * the game in sync between players since it's used in GUI only in client side */
	double v_a = (double)p_a / (double)r_a.Value();
	double v_b = (double)p_b / (double)r_b.Value();
	/* Use EngineID to sort if both have same power/running cost,
	 * since we want consistent sorting.
	 * Also if both have no power then sort with reverse of running cost to simulate
	 * previous sorting behaviour for wagons. */
	if (v_a == 0 && v_b == 0) return !EngineRunningCostSorter(a, b);
	if (v_a == v_b)  return EngineNumberSorter(a, b);
	return _engine_sort_direction != (v_a < v_b);
}

/* Train sorting functions */

/**
 * Determines order of train engines by capacity
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool TrainEngineCapacitySorter(const EngineID &a, const EngineID &b)
{
	const RailVehicleInfo *rvi_a = RailVehInfo(a);
	const RailVehicleInfo *rvi_b = RailVehInfo(b);

	int va = GetTotalCapacityOfArticulatedParts(a) * (rvi_a->railveh_type == RAILVEH_MULTIHEAD ? 2 : 1);
	int vb = GetTotalCapacityOfArticulatedParts(b) * (rvi_b->railveh_type == RAILVEH_MULTIHEAD ? 2 : 1);
	int r = va - vb;

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b);
	return _engine_sort_direction ? r > 0 : r < 0;
}

/**
 * Determines order of train engines by engine / wagon
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool TrainEnginesThenWagonsSorter(const EngineID &a, const EngineID &b)
{
	int val_a = (RailVehInfo(a)->railveh_type == RAILVEH_WAGON ? 1 : 0);
	int val_b = (RailVehInfo(b)->railveh_type == RAILVEH_WAGON ? 1 : 0);
	int r = val_a - val_b;

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b);
	return _engine_sort_direction ? r > 0 : r < 0;
}

/* Road vehicle sorting functions */

/**
 * Determines order of road vehicles by capacity
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool RoadVehEngineCapacitySorter(const EngineID &a, const EngineID &b)
{
	int va = GetTotalCapacityOfArticulatedParts(a);
	int vb = GetTotalCapacityOfArticulatedParts(b);
	int r = va - vb;

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b);
	return _engine_sort_direction ? r > 0 : r < 0;
}

/* Ship vehicle sorting functions */

/**
 * Determines order of ships by capacity
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool ShipEngineCapacitySorter(const EngineID &a, const EngineID &b)
{
	const Engine *e_a = Engine::Get(a);
	const Engine *e_b = Engine::Get(b);

	int va = e_a->GetDisplayDefaultCapacity();
	int vb = e_b->GetDisplayDefaultCapacity();
	int r = va - vb;

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b);
	return _engine_sort_direction ? r > 0 : r < 0;
}

/* Aircraft sorting functions */

/**
 * Determines order of aircraft by cargo
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool AircraftEngineCargoSorter(const EngineID &a, const EngineID &b)
{
	const Engine *e_a = Engine::Get(a);
	const Engine *e_b = Engine::Get(b);

	uint16 mail_a, mail_b;
	int va = e_a->GetDisplayDefaultCapacity(&mail_a);
	int vb = e_b->GetDisplayDefaultCapacity(&mail_b);
	int r = va - vb;

	if (r == 0) {
		/* The planes have the same passenger capacity. Check mail capacity instead */
		r = mail_a - mail_b;

		if (r == 0) {
			/* Use EngineID to sort instead since we want consistent sorting */
			return EngineNumberSorter(a, b);
		}
	}
	return _engine_sort_direction ? r > 0 : r < 0;
}

/**
 * Determines order of aircraft by range.
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool AircraftRangeSorter(const EngineID &a, const EngineID &b)
{
	uint16 r_a = Engine::Get(a)->GetRange();
	uint16 r_b = Engine::Get(b)->GetRange();

	int r = r_a - r_b;

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b);
	return _engine_sort_direction ? r > 0 : r < 0;
}

/** Sort functions for the vehicle sort criteria, for each vehicle type. */
EngList_SortTypeFunction * const _engine_sort_functions[][11] = {{
	/* Trains */
	&EngineNumberSorter,
	&EngineCostSorter,
	&EngineSpeedSorter,
	&EnginePowerSorter,
	&EngineTractiveEffortSorter,
	&EngineIntroDateSorter,
	&EngineNameSorter,
	&EngineRunningCostSorter,
	&EnginePowerVsRunningCostSorter,
	&EngineReliabilitySorter,
	&TrainEngineCapacitySorter,
}, {
	/* Road vehicles */
	&EngineNumberSorter,
	&EngineCostSorter,
	&EngineSpeedSorter,
	&EnginePowerSorter,
	&EngineTractiveEffortSorter,
	&EngineIntroDateSorter,
	&EngineNameSorter,
	&EngineRunningCostSorter,
	&EnginePowerVsRunningCostSorter,
	&EngineReliabilitySorter,
	&RoadVehEngineCapacitySorter,
}, {
	/* Ships */
	&EngineNumberSorter,
	&EngineCostSorter,
	&EngineSpeedSorter,
	&EngineIntroDateSorter,
	&EngineNameSorter,
	&EngineRunningCostSorter,
	&EngineReliabilitySorter,
	&ShipEngineCapacitySorter,
}, {
	/* Aircraft */
	&EngineNumberSorter,
	&EngineCostSorter,
	&EngineSpeedSorter,
	&EngineIntroDateSorter,
	&EngineNameSorter,
	&EngineRunningCostSorter,
	&EngineReliabilitySorter,
	&AircraftEngineCargoSorter,
	&AircraftRangeSorter,
}};

/** Dropdown menu strings for the vehicle sort criteria. */
const StringID _engine_sort_listing[][12] = {{
	/* Trains */
	STR_SORT_BY_ENGINE_ID,
	STR_SORT_BY_COST,
	STR_SORT_BY_MAX_SPEED,
	STR_SORT_BY_POWER,
	STR_SORT_BY_TRACTIVE_EFFORT,
	STR_SORT_BY_INTRO_DATE,
	STR_SORT_BY_NAME,
	STR_SORT_BY_RUNNING_COST,
	STR_SORT_BY_POWER_VS_RUNNING_COST,
	STR_SORT_BY_RELIABILITY,
	STR_SORT_BY_CARGO_CAPACITY,
	INVALID_STRING_ID
}, {
	/* Road vehicles */
	STR_SORT_BY_ENGINE_ID,
	STR_SORT_BY_COST,
	STR_SORT_BY_MAX_SPEED,
	STR_SORT_BY_POWER,
	STR_SORT_BY_TRACTIVE_EFFORT,
	STR_SORT_BY_INTRO_DATE,
	STR_SORT_BY_NAME,
	STR_SORT_BY_RUNNING_COST,
	STR_SORT_BY_POWER_VS_RUNNING_COST,
	STR_SORT_BY_RELIABILITY,
	STR_SORT_BY_CARGO_CAPACITY,
	INVALID_STRING_ID
}, {
	/* Ships */
	STR_SORT_BY_ENGINE_ID,
	STR_SORT_BY_COST,
	STR_SORT_BY_MAX_SPEED,
	STR_SORT_BY_INTRO_DATE,
	STR_SORT_BY_NAME,
	STR_SORT_BY_RUNNING_COST,
	STR_SORT_BY_RELIABILITY,
	STR_SORT_BY_CARGO_CAPACITY,
	INVALID_STRING_ID
}, {
	/* Aircraft */
	STR_SORT_BY_ENGINE_ID,
	STR_SORT_BY_COST,
	STR_SORT_BY_MAX_SPEED,
	STR_SORT_BY_INTRO_DATE,
	STR_SORT_BY_NAME,
	STR_SORT_BY_RUNNING_COST,
	STR_SORT_BY_RELIABILITY,
	STR_SORT_BY_CARGO_CAPACITY,
	STR_SORT_BY_RANGE,
	INVALID_STRING_ID
}};

/** Cargo filter functions */
static bool CDECL CargoFilter(const EngineID *eid, const CargoID cid)
{
	if (cid == CF_ANY) return true;
	CargoTypes refit_mask = GetUnionOfArticulatedRefitMasks(*eid, true) & _standard_cargo_mask;
	return (cid == CF_NONE ? refit_mask == 0 : HasBit(refit_mask, cid));
}

static GUIEngineList::FilterFunction * const _filter_funcs[] = {
	&CargoFilter,
};

static int DrawCargoCapacityInfo(int left, int right, int y, EngineID engine, TestedEngineDetails &te)
{
	CargoArray cap;
	CargoTypes refits;
	GetArticulatedVehicleCargoesAndRefits(engine, &cap, &refits, te.cargo, te.capacity);

	for (CargoID c = 0; c < NUM_CARGO; c++) {
		if (cap[c] == 0) continue;

		SetDParam(0, c);
		SetDParam(1, cap[c]);
		SetDParam(2, HasBit(refits, c) ? STR_PURCHASE_INFO_REFITTABLE : STR_EMPTY);
		DrawString(left, right, y, STR_PURCHASE_INFO_CAPACITY);
		y += FONT_HEIGHT_NORMAL;
	}

	return y;
}

/* Draw rail wagon specific details */
static int DrawRailWagonPurchaseInfo(int left, int right, int y, EngineID engine_number, const RailVehicleInfo *rvi, TestedEngineDetails &te)
{
	const Engine *e = Engine::Get(engine_number);

	/* Purchase cost */
	if (te.cost != 0) {
		SetDParam(0, e->GetCost() + te.cost);
		SetDParam(1, te.cost);
		DrawString(left, right, y, STR_PURCHASE_INFO_COST_REFIT);
	} else {
		SetDParam(0, e->GetCost());
		DrawString(left, right, y, STR_PURCHASE_INFO_COST);
	}
	y += FONT_HEIGHT_NORMAL;

	/* Wagon weight - (including cargo) */
	uint weight = e->GetDisplayWeight();
	SetDParam(0, weight);
	uint cargo_weight = (e->CanCarryCargo() ? CargoSpec::Get(te.cargo)->weight * te.capacity / 16 : 0);
	SetDParam(1, cargo_weight + weight);
	DrawString(left, right, y, STR_PURCHASE_INFO_WEIGHT_CWEIGHT);
	y += FONT_HEIGHT_NORMAL;

	/* Wagon speed limit, displayed if above zero */
	if (_settings_game.vehicle.wagon_speed_limits) {
		uint max_speed = e->GetDisplayMaxSpeed();
		if (max_speed > 0) {
			SetDParam(0, max_speed);
			DrawString(left, right, y, STR_PURCHASE_INFO_SPEED);
			y += FONT_HEIGHT_NORMAL;
		}
	}

	/* Running cost */
	if (rvi->running_cost_class != INVALID_PRICE) {
		SetDParam(0, e->GetRunningCost());
		DrawString(left, right, y, STR_PURCHASE_INFO_RUNNINGCOST);
		y += FONT_HEIGHT_NORMAL;
	}

	return y;
}

/* Draw locomotive specific details */
static int DrawRailEnginePurchaseInfo(int left, int right, int y, EngineID engine_number, const RailVehicleInfo *rvi, TestedEngineDetails &te)
{
	const Engine *e = Engine::Get(engine_number);

	/* Purchase Cost - Engine weight */
	if (te.cost != 0) {
		SetDParam(0, e->GetCost() + te.cost);
		SetDParam(1, te.cost);
		SetDParam(2, e->GetDisplayWeight());
		DrawString(left, right, y, STR_PURCHASE_INFO_COST_REFIT_WEIGHT);
	} else {
		SetDParam(0, e->GetCost());
		SetDParam(1, e->GetDisplayWeight());
		DrawString(left, right, y, STR_PURCHASE_INFO_COST_WEIGHT);
	}
	y += FONT_HEIGHT_NORMAL;

	/* Max speed - Engine power */
	SetDParam(0, e->GetDisplayMaxSpeed());
	SetDParam(1, e->GetPower());
	DrawString(left, right, y, STR_PURCHASE_INFO_SPEED_POWER);
	y += FONT_HEIGHT_NORMAL;

	/* Max tractive effort - not applicable if old acceleration or maglev */
	if (_settings_game.vehicle.train_acceleration_model != AM_ORIGINAL && GetRailTypeInfo(rvi->railtype)->acceleration_type != 2) {
		SetDParam(0, e->GetDisplayMaxTractiveEffort());
		DrawString(left, right, y, STR_PURCHASE_INFO_MAX_TE);
		y += FONT_HEIGHT_NORMAL;
	}

	/* Running cost */
	if (rvi->running_cost_class != INVALID_PRICE) {
		SetDParam(0, e->GetRunningCost());
		DrawString(left, right, y, STR_PURCHASE_INFO_RUNNINGCOST);
		y += FONT_HEIGHT_NORMAL;
	}

	/* Powered wagons power - Powered wagons extra weight */
	if (rvi->pow_wag_power != 0) {
		SetDParam(0, rvi->pow_wag_power);
		SetDParam(1, rvi->pow_wag_weight);
		DrawString(left, right, y, STR_PURCHASE_INFO_PWAGPOWER_PWAGWEIGHT);
		y += FONT_HEIGHT_NORMAL;
	}

	return y;
}

/* Draw road vehicle specific details */
static int DrawRoadVehPurchaseInfo(int left, int right, int y, EngineID engine_number, TestedEngineDetails &te)
{
	const Engine *e = Engine::Get(engine_number);

	if (_settings_game.vehicle.roadveh_acceleration_model != AM_ORIGINAL) {
		/* Purchase Cost */
		if (te.cost != 0) {
			SetDParam(0, e->GetCost() + te.cost);
			SetDParam(1, te.cost);
			DrawString(left, right, y, STR_PURCHASE_INFO_COST_REFIT);
		} else {
			SetDParam(0, e->GetCost());
			DrawString(left, right, y, STR_PURCHASE_INFO_COST);
		}
		y += FONT_HEIGHT_NORMAL;

		/* Road vehicle weight - (including cargo) */
		int16 weight = e->GetDisplayWeight();
		SetDParam(0, weight);
		uint cargo_weight = (e->CanCarryCargo() ? CargoSpec::Get(te.cargo)->weight * te.capacity / 16 : 0);
		SetDParam(1, cargo_weight + weight);
		DrawString(left, right, y, STR_PURCHASE_INFO_WEIGHT_CWEIGHT);
		y += FONT_HEIGHT_NORMAL;

		/* Max speed - Engine power */
		SetDParam(0, e->GetDisplayMaxSpeed());
		SetDParam(1, e->GetPower());
		DrawString(left, right, y, STR_PURCHASE_INFO_SPEED_POWER);
		y += FONT_HEIGHT_NORMAL;

		/* Max tractive effort */
		SetDParam(0, e->GetDisplayMaxTractiveEffort());
		DrawString(left, right, y, STR_PURCHASE_INFO_MAX_TE);
		y += FONT_HEIGHT_NORMAL;
	} else {
		/* Purchase cost - Max speed */
		if (te.cost != 0) {
			SetDParam(0, e->GetCost() + te.cost);
			SetDParam(1, te.cost);
			SetDParam(2, e->GetDisplayMaxSpeed());
			DrawString(left, right, y, STR_PURCHASE_INFO_COST_REFIT_SPEED);
		} else {
			SetDParam(0, e->GetCost());
			SetDParam(1, e->GetDisplayMaxSpeed());
			DrawString(left, right, y, STR_PURCHASE_INFO_COST_SPEED);
		}
		y += FONT_HEIGHT_NORMAL;
	}

	/* Running cost */
	SetDParam(0, e->GetRunningCost());
	DrawString(left, right, y, STR_PURCHASE_INFO_RUNNINGCOST);
	y += FONT_HEIGHT_NORMAL;

	return y;
}

/* Draw ship specific details */
static int DrawShipPurchaseInfo(int left, int right, int y, EngineID engine_number, bool refittable, TestedEngineDetails &te)
{
	const Engine *e = Engine::Get(engine_number);

	/* Purchase cost - Max speed */
	uint raw_speed = e->GetDisplayMaxSpeed();
	uint ocean_speed = e->u.ship.ApplyWaterClassSpeedFrac(raw_speed, true);
	uint canal_speed = e->u.ship.ApplyWaterClassSpeedFrac(raw_speed, false);

	if (ocean_speed == canal_speed) {
		if (te.cost != 0) {
			SetDParam(0, e->GetCost() + te.cost);
			SetDParam(1, te.cost);
			SetDParam(2, ocean_speed);
			DrawString(left, right, y, STR_PURCHASE_INFO_COST_REFIT_SPEED);
		} else {
			SetDParam(0, e->GetCost());
			SetDParam(1, ocean_speed);
			DrawString(left, right, y, STR_PURCHASE_INFO_COST_SPEED);
		}
		y += FONT_HEIGHT_NORMAL;
	} else {
		if (te.cost != 0) {
			SetDParam(0, e->GetCost() + te.cost);
			SetDParam(1, te.cost);
			DrawString(left, right, y, STR_PURCHASE_INFO_COST_REFIT);
		} else {
			SetDParam(0, e->GetCost());
			DrawString(left, right, y, STR_PURCHASE_INFO_COST);
		}
		y += FONT_HEIGHT_NORMAL;

		SetDParam(0, ocean_speed);
		DrawString(left, right, y, STR_PURCHASE_INFO_SPEED_OCEAN);
		y += FONT_HEIGHT_NORMAL;

		SetDParam(0, canal_speed);
		DrawString(left, right, y, STR_PURCHASE_INFO_SPEED_CANAL);
		y += FONT_HEIGHT_NORMAL;
	}

	/* Cargo type + capacity */
	SetDParam(0, te.cargo);
	SetDParam(1, te.capacity);
	SetDParam(2, refittable ? STR_PURCHASE_INFO_REFITTABLE : STR_EMPTY);
	DrawString(left, right, y, STR_PURCHASE_INFO_CAPACITY);
	y += FONT_HEIGHT_NORMAL;

	/* Running cost */
	SetDParam(0, e->GetRunningCost());
	DrawString(left, right, y, STR_PURCHASE_INFO_RUNNINGCOST);
	y += FONT_HEIGHT_NORMAL;

	return y;
}

/**
 * Draw aircraft specific details in the buy window.
 * @param left Left edge of the window to draw in.
 * @param right Right edge of the window to draw in.
 * @param y Top of the area to draw in.
 * @param engine_number Engine to display.
 * @param refittable If set, the aircraft can be refitted.
 * @return Bottom of the used area.
 */
static int DrawAircraftPurchaseInfo(int left, int right, int y, EngineID engine_number, bool refittable, TestedEngineDetails &te)
{
	const Engine *e = Engine::Get(engine_number);

	/* Purchase cost - Max speed */
	if (te.cost != 0) {
		SetDParam(0, e->GetCost() + te.cost);
		SetDParam(1, te.cost);
		SetDParam(2, e->GetDisplayMaxSpeed());
		DrawString(left, right, y, STR_PURCHASE_INFO_COST_REFIT_SPEED);
	} else {
		SetDParam(0, e->GetCost());
		SetDParam(1, e->GetDisplayMaxSpeed());
		DrawString(left, right, y, STR_PURCHASE_INFO_COST_SPEED);
	}
	y += FONT_HEIGHT_NORMAL;

	/* Cargo capacity */
	if (te.mail_capacity > 0) {
		SetDParam(0, te.cargo);
		SetDParam(1, te.capacity);
		SetDParam(2, CT_MAIL);
		SetDParam(3, te.mail_capacity);
		DrawString(left, right, y, STR_PURCHASE_INFO_AIRCRAFT_CAPACITY);
	} else {
		/* Note, if the default capacity is selected by the refit capacity
		 * callback, then the capacity shown is likely to be incorrect. */
		SetDParam(0, te.cargo);
		SetDParam(1, te.capacity);
		SetDParam(2, refittable ? STR_PURCHASE_INFO_REFITTABLE : STR_EMPTY);
		DrawString(left, right, y, STR_PURCHASE_INFO_CAPACITY);
	}
	y += FONT_HEIGHT_NORMAL;

	/* Running cost */
	SetDParam(0, e->GetRunningCost());
	DrawString(left, right, y, STR_PURCHASE_INFO_RUNNINGCOST);
	y += FONT_HEIGHT_NORMAL;

	/* Aircraft type */
	SetDParam(0, e->GetAircraftTypeText());
	DrawString(left, right, y, STR_PURCHASE_INFO_AIRCRAFT_TYPE);
	y += FONT_HEIGHT_NORMAL;

	/* Aircraft range, if available. */
	uint16 range = e->GetRange();
	if (range != 0) {
		SetDParam(0, range);
		DrawString(left, right, y, STR_PURCHASE_INFO_AIRCRAFT_RANGE);
		y += FONT_HEIGHT_NORMAL;
	}

	return y;
}

/**
 * Display additional text from NewGRF in the purchase information window
 * @param left   Left border of text bounding box
 * @param right  Right border of text bounding box
 * @param y      Top border of text bounding box
 * @param engine Engine to query the additional purchase information for
 * @return       Bottom border of text bounding box
 */
static uint ShowAdditionalText(int left, int right, int y, EngineID engine)
{
	uint16 callback = GetVehicleCallback(CBID_VEHICLE_ADDITIONAL_TEXT, 0, 0, engine, nullptr);
	if (callback == CALLBACK_FAILED || callback == 0x400) return y;
	const GRFFile *grffile = Engine::Get(engine)->GetGRF();
	if (callback > 0x400) {
		ErrorUnknownCallbackResult(grffile->grfid, CBID_VEHICLE_ADDITIONAL_TEXT, callback);
		return y;
	}

	StartTextRefStackUsage(grffile, 6);
	uint result = DrawStringMultiLine(left, right, y, INT32_MAX, GetGRFStringID(grffile->grfid, 0xD000 + callback), TC_BLACK);
	StopTextRefStackUsage();
	return result;
}

/**
 * Draw the purchase info details of a vehicle at a given location.
 * @param left,right,y location where to draw the info
 * @param engine_number the engine of which to draw the info of
 * @return y after drawing all the text
 */
int DrawVehiclePurchaseInfo(int left, int right, int y, EngineID engine_number, TestedEngineDetails &te)
{
	const Engine *e = Engine::Get(engine_number);
	YearMonthDay ymd;
	ConvertDateToYMD(e->intro_date, &ymd);
	bool refittable = IsArticulatedVehicleRefittable(engine_number);
	bool articulated_cargo = false;

	switch (e->type) {
		default: NOT_REACHED();
		case VEH_TRAIN:
			if (e->u.rail.railveh_type == RAILVEH_WAGON) {
				y = DrawRailWagonPurchaseInfo(left, right, y, engine_number, &e->u.rail, te);
			} else {
				y = DrawRailEnginePurchaseInfo(left, right, y, engine_number, &e->u.rail, te);
			}
			articulated_cargo = true;
			break;

		case VEH_ROAD:
			y = DrawRoadVehPurchaseInfo(left, right, y, engine_number, te);
			articulated_cargo = true;
			break;

		case VEH_SHIP:
			y = DrawShipPurchaseInfo(left, right, y, engine_number, refittable, te);
			break;

		case VEH_AIRCRAFT:
			y = DrawAircraftPurchaseInfo(left, right, y, engine_number, refittable, te);
			break;
	}

	if (articulated_cargo) {
		/* Cargo type + capacity, or N/A */
		int new_y = DrawCargoCapacityInfo(left, right, y, engine_number, te);

		if (new_y == y) {
			SetDParam(0, CT_INVALID);
			SetDParam(2, STR_EMPTY);
			DrawString(left, right, y, STR_PURCHASE_INFO_CAPACITY);
			y += FONT_HEIGHT_NORMAL;
		} else {
			y = new_y;
		}
	}

	/* Draw details that apply to all types except rail wagons. */
	if (e->type != VEH_TRAIN || e->u.rail.railveh_type != RAILVEH_WAGON) {
		/* Design date - Life length */
		SetDParam(0, ymd.year);
		SetDParam(1, e->GetLifeLengthInDays() / DAYS_IN_LEAP_YEAR);
		DrawString(left, right, y, STR_PURCHASE_INFO_DESIGNED_LIFE);
		y += FONT_HEIGHT_NORMAL;

		/* Reliability */
		SetDParam(0, ToPercent16(e->reliability));
		DrawString(left, right, y, STR_PURCHASE_INFO_RELIABILITY);
		y += FONT_HEIGHT_NORMAL;
	}

	if (refittable) y = ShowRefitOptionsList(left, right, y, engine_number);

	/* Additional text from NewGRF */
	y = ShowAdditionalText(left, right, y, engine_number);

	/* The NewGRF's name which the vehicle comes from */
	const GRFConfig *config = GetGRFConfig(e->GetGRFID());
	if (_settings_client.gui.show_newgrf_name && config != nullptr)
	{
		DrawString(left, right, y, config->GetName(), TC_BLACK);
		y += FONT_HEIGHT_NORMAL;
	}

	return y;
}

/**
 * Engine drawing loop
 * @param type Type of vehicle (VEH_*)
 * @param l The left most location of the list
 * @param r The right most location of the list
 * @param y The top most location of the list
 * @param eng_list What engines to draw
 * @param min where to start in the list
 * @param max where in the list to end
 * @param selected_id what engine to highlight as selected, if any
 * @param show_count Whether to show the amount of engines or not
 * @param selected_group the group to list the engines of
 */
void DrawEngineList(VehicleType type, int l, int r, int y, const GUIEngineList *eng_list, uint16 min, uint16 max, EngineID selected_id, bool show_count, GroupID selected_group)
{
	static const int sprite_y_offsets[] = { -1, -1, -2, -2 };

	/* Obligatory sanity checks! */
	assert(max <= eng_list->size());

	bool rtl = _current_text_dir == TD_RTL;
	int step_size = GetEngineListHeight(type);
	int sprite_left  = GetVehicleImageCellSize(type, EIT_PURCHASE).extend_left;
	int sprite_right = GetVehicleImageCellSize(type, EIT_PURCHASE).extend_right;
	int sprite_width = sprite_left + sprite_right;

	int sprite_x        = rtl ? r - sprite_right - 1 : l + sprite_left + 1;
	int sprite_y_offset = sprite_y_offsets[type] + step_size / 2;

	Dimension replace_icon = {0, 0};
	int count_width = 0;
	if (show_count) {
		replace_icon = GetSpriteSize(SPR_GROUP_REPLACE_ACTIVE);
		SetDParamMaxDigits(0, 3, FS_SMALL);
		count_width = GetStringBoundingBox(STR_TINY_BLACK_COMA).width;
	}

	int text_left  = l + (rtl ? WD_FRAMERECT_LEFT + replace_icon.width + 8 + count_width : sprite_width + WD_FRAMETEXT_LEFT);
	int text_right = r - (rtl ? sprite_width + WD_FRAMETEXT_RIGHT : WD_FRAMERECT_RIGHT + replace_icon.width + 8 + count_width);
	int replace_icon_left = rtl ? l + WD_FRAMERECT_LEFT : r - WD_FRAMERECT_RIGHT - replace_icon.width;
	int count_left = l;
	int count_right = rtl ? text_left : r - WD_FRAMERECT_RIGHT - replace_icon.width - 8;

	int normal_text_y_offset = (step_size - FONT_HEIGHT_NORMAL) / 2;
	int small_text_y_offset  = step_size - FONT_HEIGHT_SMALL - WD_FRAMERECT_BOTTOM - 1;
	int replace_icon_y_offset = (step_size - replace_icon.height) / 2 - 1;

	for (; min < max; min++, y += step_size) {
		const EngineID engine = (*eng_list)[min];
		/* Note: num_engines is only used in the autoreplace GUI, so it is correct to use _local_company here. */
		const uint num_engines = GetGroupNumEngines(_local_company, selected_group, engine);

		const Engine *e = Engine::Get(engine);
		bool hidden = HasBit(e->company_hidden, _local_company);
		StringID str = hidden ? STR_HIDDEN_ENGINE_NAME : STR_ENGINE_NAME;
		TextColour tc = (engine == selected_id) ? TC_WHITE : (TC_NO_SHADE | (hidden ? TC_GREY : TC_BLACK));

		SetDParam(0, engine);
		DrawString(text_left, text_right, y + normal_text_y_offset, str, tc);
		DrawVehicleEngine(l, r, sprite_x, y + sprite_y_offset, engine, (show_count && num_engines == 0) ? PALETTE_CRASH : GetEnginePalette(engine, _local_company), EIT_PURCHASE);
		if (show_count) {
			SetDParam(0, num_engines);
			DrawString(count_left, count_right, y + small_text_y_offset, STR_TINY_BLACK_COMA, TC_FROMSTRING, SA_RIGHT | SA_FORCE);
			if (EngineHasReplacementForCompany(Company::Get(_local_company), engine, selected_group)) DrawSprite(SPR_GROUP_REPLACE_ACTIVE, num_engines == 0 ? PALETTE_CRASH : PAL_NONE, replace_icon_left, y + replace_icon_y_offset);
		}
	}
}

/**
 * Display the dropdown for the vehicle sort criteria.
 * @param w Parent window (holds the dropdown button).
 * @param vehicle_type %Vehicle type being sorted.
 * @param selected Currently selected sort criterium.
 * @param button Widget button.
 */
void DisplayVehicleSortDropDown(Window *w, VehicleType vehicle_type, int selected, int button)
{
	uint32 hidden_mask = 0;
	/* Disable sorting by power or tractive effort when the original acceleration model for road vehicles is being used. */
	if (vehicle_type == VEH_ROAD && _settings_game.vehicle.roadveh_acceleration_model == AM_ORIGINAL) {
		SetBit(hidden_mask, 3); // power
		SetBit(hidden_mask, 4); // tractive effort
		SetBit(hidden_mask, 8); // power by running costs
	}
	/* Disable sorting by tractive effort when the original acceleration model for trains is being used. */
	if (vehicle_type == VEH_TRAIN && _settings_game.vehicle.train_acceleration_model == AM_ORIGINAL) {
		SetBit(hidden_mask, 4); // tractive effort
	}
	ShowDropDownMenu(w, _engine_sort_listing[vehicle_type], selected, button, 0, hidden_mask);
}

/** GUI for building vehicles. */
struct BuildVehicleWindow : Window {
	VehicleType vehicle_type;                   ///< Type of vehicles shown in the window.
	union {
		RailType railtype;   ///< Rail type to show, or #INVALID_RAILTYPE.
		RoadType roadtype;   ///< Road type to show, or #INVALID_ROADTYPE.
	} filter;                                   ///< Filter to apply.
	bool descending_sort_order;                 ///< Sort direction, @see _engine_sort_direction
	byte sort_criteria;                         ///< Current sort criterium.
	bool show_hidden_engines;                   ///< State of the 'show hidden engines' button.
	bool listview_mode;                         ///< If set, only display the available vehicles and do not show a 'build' button.
	EngineID sel_engine;                        ///< Currently selected engine, or #INVALID_ENGINE
	EngineID rename_engine;                     ///< Engine being renamed.
	GUIEngineList eng_list;
	CargoID cargo_filter[NUM_CARGO + 2];        ///< Available cargo filters; CargoID or CF_ANY or CF_NONE
	StringID cargo_filter_texts[NUM_CARGO + 3]; ///< Texts for filter_cargo, terminated by INVALID_STRING_ID
	byte cargo_filter_criteria;                 ///< Selected cargo filter
	int details_height;                         ///< Minimal needed height of the details panels (found so far).
	Scrollbar *vscroll;
	TestedEngineDetails te;                     ///< Tested cost and capacity after refit.

	void SetBuyVehicleText()
	{
		NWidgetCore *widget = this->GetWidget<NWidgetCore>(WID_BV_BUILD);

		bool refit = this->sel_engine != INVALID_ENGINE && this->cargo_filter[this->cargo_filter_criteria] != CF_ANY && this->cargo_filter[this->cargo_filter_criteria] != CF_NONE;
		if (refit) refit = Engine::Get(this->sel_engine)->GetDefaultCargoType() != this->cargo_filter[this->cargo_filter_criteria];

		if (refit) {
			widget->widget_data = STR_BUY_VEHICLE_TRAIN_BUY_REFIT_VEHICLE_BUTTON + this->vehicle_type;
			widget->tool_tip    = STR_BUY_VEHICLE_TRAIN_BUY_REFIT_VEHICLE_TOOLTIP + this->vehicle_type;
		} else {
			widget->widget_data = STR_BUY_VEHICLE_TRAIN_BUY_VEHICLE_BUTTON + this->vehicle_type;
			widget->tool_tip    = STR_BUY_VEHICLE_TRAIN_BUY_VEHICLE_TOOLTIP + this->vehicle_type;
		}
	}

	BuildVehicleWindow(WindowDesc *desc, TileIndex tile, VehicleType type) : Window(desc)
	{
		this->vehicle_type = type;
		this->listview_mode = tile == INVALID_TILE;
		this->window_number = this->listview_mode ? (int)type : tile;

		this->sel_engine = INVALID_ENGINE;

		this->sort_criteria         = _engine_sort_last_criteria[type];
		this->descending_sort_order = _engine_sort_last_order[type];
		this->show_hidden_engines   = _engine_sort_show_hidden_engines[type];

		this->UpdateFilterByTile();

		this->CreateNestedTree();

		this->vscroll = this->GetScrollbar(WID_BV_SCROLLBAR);

		/* If we are just viewing the list of vehicles, we do not need the Build button.
		 * So we just hide it, and enlarge the Rename button by the now vacant place. */
		if (this->listview_mode) this->GetWidget<NWidgetStacked>(WID_BV_BUILD_SEL)->SetDisplayedPlane(SZSP_NONE);

		/* disable renaming engines in network games if you are not the server */
		this->SetWidgetDisabledState(WID_BV_RENAME, _networking && !_network_server);

		NWidgetCore *widget = this->GetWidget<NWidgetCore>(WID_BV_LIST);
		widget->tool_tip = STR_BUY_VEHICLE_TRAIN_LIST_TOOLTIP + type;

		widget = this->GetWidget<NWidgetCore>(WID_BV_SHOW_HIDE);
		widget->tool_tip = STR_BUY_VEHICLE_TRAIN_HIDE_SHOW_TOGGLE_TOOLTIP + type;

		widget = this->GetWidget<NWidgetCore>(WID_BV_RENAME);
		widget->widget_data = STR_BUY_VEHICLE_TRAIN_RENAME_BUTTON + type;
		widget->tool_tip    = STR_BUY_VEHICLE_TRAIN_RENAME_TOOLTIP + type;

		widget = this->GetWidget<NWidgetCore>(WID_BV_SHOW_HIDDEN_ENGINES);
		widget->widget_data = STR_SHOW_HIDDEN_ENGINES_VEHICLE_TRAIN + type;
		widget->tool_tip    = STR_SHOW_HIDDEN_ENGINES_VEHICLE_TRAIN_TOOLTIP + type;
		widget->SetLowered(this->show_hidden_engines);

		this->details_height = ((this->vehicle_type == VEH_TRAIN) ? 10 : 9) * FONT_HEIGHT_NORMAL + WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM;

		this->FinishInitNested(tile == INVALID_TILE ? (int)type : tile);

		this->owner = (tile != INVALID_TILE) ? GetTileOwner(tile) : _local_company;

		this->eng_list.ForceRebuild();
		this->GenerateBuildList(); // generate the list, since we need it in the next line
		/* Select the first engine in the list as default when opening the window */
		if (this->eng_list.size() > 0) {
			this->SelectEngine(this->eng_list[0]);
		} else {
			this->SelectEngine(INVALID_ENGINE);
		}
	}

	/** Set the filter type according to the depot type */
	void UpdateFilterByTile()
	{
		switch (this->vehicle_type) {
			default: NOT_REACHED();
			case VEH_TRAIN:
				if (this->listview_mode) {
					this->filter.railtype = INVALID_RAILTYPE;
				} else {
					this->filter.railtype = GetRailType(this->window_number);
				}
				break;

			case VEH_ROAD:
				if (this->listview_mode) {
					this->filter.roadtype = INVALID_ROADTYPE;
				} else {
					this->filter.roadtype = GetRoadTypeRoad(this->window_number);
					if (this->filter.roadtype == INVALID_ROADTYPE) {
						this->filter.roadtype = GetRoadTypeTram(this->window_number);
					}
				}
				break;

			case VEH_SHIP:
			case VEH_AIRCRAFT:
				break;
		}
	}

	/** Populate the filter list and set the cargo filter criteria. */
	void SetCargoFilterArray()
	{
		uint filter_items = 0;

		/* Add item for disabling filtering. */
		this->cargo_filter[filter_items] = CF_ANY;
		this->cargo_filter_texts[filter_items] = STR_PURCHASE_INFO_ALL_TYPES;
		filter_items++;

		/* Add item for vehicles not carrying anything, e.g. train engines.
		 * This could also be useful for eyecandy vehicles of other types, but is likely too confusing for joe, */
		if (this->vehicle_type == VEH_TRAIN) {
			this->cargo_filter[filter_items] = CF_NONE;
			this->cargo_filter_texts[filter_items] = STR_PURCHASE_INFO_NONE;
			filter_items++;
		}

		/* Collect available cargo types for filtering. */
		const CargoSpec *cs;
		FOR_ALL_SORTED_STANDARD_CARGOSPECS(cs) {
			this->cargo_filter[filter_items] = cs->Index();
			this->cargo_filter_texts[filter_items] = cs->name;
			filter_items++;
		}

		/* Terminate the filter list. */
		this->cargo_filter_texts[filter_items] = INVALID_STRING_ID;

		/* If not found, the cargo criteria will be set to all cargoes. */
		this->cargo_filter_criteria = 0;

		/* Find the last cargo filter criteria. */
		for (uint i = 0; i < filter_items; i++) {
			if (this->cargo_filter[i] == _engine_sort_last_cargo_criteria[this->vehicle_type]) {
				this->cargo_filter_criteria = i;
				break;
			}
		}

		this->eng_list.SetFilterFuncs(_filter_funcs);
		this->eng_list.SetFilterState(this->cargo_filter[this->cargo_filter_criteria] != CF_ANY);
	}

	void SelectEngine(EngineID engine)
	{
		CargoID cargo = this->cargo_filter[this->cargo_filter_criteria];
		if (cargo == CF_ANY) cargo = CF_NONE;

		this->sel_engine = engine;
		this->SetBuyVehicleText();

		if (this->sel_engine == INVALID_ENGINE) return;

		const Engine *e = Engine::Get(this->sel_engine);
		if (!e->CanCarryCargo()) {
			this->te.cost = 0;
			this->te.cargo = CT_INVALID;
			return;
		}

		if (!this->listview_mode) {
			/* Query for cost and refitted capacity */
			CommandCost ret = DoCommand(this->window_number, this->sel_engine | (cargo << 24), 0, DC_QUERY_COST, GetCmdBuildVeh(this->vehicle_type), nullptr);
			if (ret.Succeeded()) {
				this->te.cost          = ret.GetCost() - e->GetCost();
				this->te.capacity      = _returned_refit_capacity;
				this->te.mail_capacity = _returned_mail_refit_capacity;
				this->te.cargo         = (cargo == CT_INVALID) ? e->GetDefaultCargoType() : cargo;
				return;
			}
		}

		/* Purchase test was not possible or failed, fill in the defaults instead. */
		this->te.cost     = 0;
		this->te.capacity = e->GetDisplayDefaultCapacity(&this->te.mail_capacity);
		this->te.cargo    = e->GetDefaultCargoType();
	}

	void OnInit() override
	{
		this->SetCargoFilterArray();
	}

	/** Filter the engine list against the currently selected cargo filter */
	void FilterEngineList()
	{
		this->eng_list.Filter(this->cargo_filter[this->cargo_filter_criteria]);
		if (0 == this->eng_list.size()) { // no engine passed through the filter, invalidate the previously selected engine
			this->SelectEngine(INVALID_ENGINE);
		} else if (std::find(this->eng_list.begin(), this->eng_list.end(), this->sel_engine) == this->eng_list.end()) { // previously selected engine didn't pass the filter, select the first engine of the list
			this->SelectEngine(this->eng_list[0]);
		}
	}

	/** Filter a single engine */
	bool FilterSingleEngine(EngineID eid)
	{
		CargoID filter_type = this->cargo_filter[this->cargo_filter_criteria];
		return (filter_type == CF_ANY || CargoFilter(&eid, filter_type));
	}

	/* Figure out what train EngineIDs to put in the list */
	void GenerateBuildTrainList()
	{
		EngineID sel_id = INVALID_ENGINE;
		int num_engines = 0;
		int num_wagons  = 0;

		this->eng_list.clear();

		/* Make list of all available train engines and wagons.
		 * Also check to see if the previously selected engine is still available,
		 * and if not, reset selection to INVALID_ENGINE. This could be the case
		 * when engines become obsolete and are removed */
		for (const Engine *e : Engine::IterateType(VEH_TRAIN)) {
			if (!this->show_hidden_engines && e->IsHidden(_local_company)) continue;
			EngineID eid = e->index;
			const RailVehicleInfo *rvi = &e->u.rail;

			if (this->filter.railtype != INVALID_RAILTYPE && !HasPowerOnRail(rvi->railtype, this->filter.railtype)) continue;
			if (!IsEngineBuildable(eid, VEH_TRAIN, _local_company)) continue;

			/* Filter now! So num_engines and num_wagons is valid */
			if (!FilterSingleEngine(eid)) continue;

			this->eng_list.push_back(eid);

			if (rvi->railveh_type != RAILVEH_WAGON) {
				num_engines++;
			} else {
				num_wagons++;
			}

			if (eid == this->sel_engine) sel_id = eid;
		}

		this->SelectEngine(sel_id);

		/* invalidate cached values for name sorter - engine names could change */
		_last_engine[0] = _last_engine[1] = INVALID_ENGINE;

		/* make engines first, and then wagons, sorted by selected sort_criteria */
		_engine_sort_direction = false;
		EngList_Sort(&this->eng_list, TrainEnginesThenWagonsSorter);

		/* and then sort engines */
		_engine_sort_direction = this->descending_sort_order;
		EngList_SortPartial(&this->eng_list, _engine_sort_functions[0][this->sort_criteria], 0, num_engines);

		/* and finally sort wagons */
		EngList_SortPartial(&this->eng_list, _engine_sort_functions[0][this->sort_criteria], num_engines, num_wagons);
	}

	/* Figure out what road vehicle EngineIDs to put in the list */
	void GenerateBuildRoadVehList()
	{
		EngineID sel_id = INVALID_ENGINE;

		this->eng_list.clear();

		for (const Engine *e : Engine::IterateType(VEH_ROAD)) {
			if (!this->show_hidden_engines && e->IsHidden(_local_company)) continue;
			EngineID eid = e->index;
			if (!IsEngineBuildable(eid, VEH_ROAD, _local_company)) continue;
			if (this->filter.roadtype != INVALID_ROADTYPE && !HasPowerOnRoad(e->u.road.roadtype, this->filter.roadtype)) continue;

			this->eng_list.push_back(eid);

			if (eid == this->sel_engine) sel_id = eid;
		}
		this->SelectEngine(sel_id);
	}

	/* Figure out what ship EngineIDs to put in the list */
	void GenerateBuildShipList()
	{
		EngineID sel_id = INVALID_ENGINE;
		this->eng_list.clear();

		for (const Engine *e : Engine::IterateType(VEH_SHIP)) {
			if (!this->show_hidden_engines && e->IsHidden(_local_company)) continue;
			EngineID eid = e->index;
			if (!IsEngineBuildable(eid, VEH_SHIP, _local_company)) continue;
			this->eng_list.push_back(eid);

			if (eid == this->sel_engine) sel_id = eid;
		}
		this->SelectEngine(sel_id);
	}

	/* Figure out what aircraft EngineIDs to put in the list */
	void GenerateBuildAircraftList()
	{
		EngineID sel_id = INVALID_ENGINE;

		this->eng_list.clear();

		const Station *st = this->listview_mode ? nullptr : Station::GetByTile(this->window_number);

		/* Make list of all available planes.
		 * Also check to see if the previously selected plane is still available,
		 * and if not, reset selection to INVALID_ENGINE. This could be the case
		 * when planes become obsolete and are removed */
		for (const Engine *e : Engine::IterateType(VEH_AIRCRAFT)) {
			if (!this->show_hidden_engines && e->IsHidden(_local_company)) continue;
			EngineID eid = e->index;
			if (!IsEngineBuildable(eid, VEH_AIRCRAFT, _local_company)) continue;
			/* First VEH_END window_numbers are fake to allow a window open for all different types at once */
			if (!this->listview_mode && !CanVehicleUseStation(eid, st)) continue;

			this->eng_list.push_back(eid);
			if (eid == this->sel_engine) sel_id = eid;
		}

		this->SelectEngine(sel_id);
	}

	/* Generate the list of vehicles */
	void GenerateBuildList()
	{
		if (!this->eng_list.NeedRebuild()) return;

		/* Update filter type in case the road/railtype of the depot got converted */
		this->UpdateFilterByTile();

		switch (this->vehicle_type) {
			default: NOT_REACHED();
			case VEH_TRAIN:
				this->GenerateBuildTrainList();
				this->eng_list.shrink_to_fit();
				this->eng_list.RebuildDone();
				return; // trains should not reach the last sorting
			case VEH_ROAD:
				this->GenerateBuildRoadVehList();
				break;
			case VEH_SHIP:
				this->GenerateBuildShipList();
				break;
			case VEH_AIRCRAFT:
				this->GenerateBuildAircraftList();
				break;
		}

		this->FilterEngineList();

		_engine_sort_direction = this->descending_sort_order;
		EngList_Sort(&this->eng_list, _engine_sort_functions[this->vehicle_type][this->sort_criteria]);

		this->eng_list.shrink_to_fit();
		this->eng_list.RebuildDone();
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_BV_SORT_ASCENDING_DESCENDING:
				this->descending_sort_order ^= true;
				_engine_sort_last_order[this->vehicle_type] = this->descending_sort_order;
				this->eng_list.ForceRebuild();
				this->SetDirty();
				break;

			case WID_BV_SHOW_HIDDEN_ENGINES:
				this->show_hidden_engines ^= true;
				_engine_sort_show_hidden_engines[this->vehicle_type] = this->show_hidden_engines;
				this->eng_list.ForceRebuild();
				this->SetWidgetLoweredState(widget, this->show_hidden_engines);
				this->SetDirty();
				break;

			case WID_BV_LIST: {
				uint i = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_BV_LIST);
				size_t num_items = this->eng_list.size();
				this->SelectEngine((i < num_items) ? this->eng_list[i] : INVALID_ENGINE);
				this->SetDirty();
				if (_ctrl_pressed) {
					this->OnClick(pt, WID_BV_SHOW_HIDE, 1);
				} else if (click_count > 1 && !this->listview_mode) {
					this->OnClick(pt, WID_BV_BUILD, 1);
				}
				break;
			}

			case WID_BV_SORT_DROPDOWN: // Select sorting criteria dropdown menu
				DisplayVehicleSortDropDown(this, this->vehicle_type, this->sort_criteria, WID_BV_SORT_DROPDOWN);
				break;

			case WID_BV_CARGO_FILTER_DROPDOWN: // Select cargo filtering criteria dropdown menu
				ShowDropDownMenu(this, this->cargo_filter_texts, this->cargo_filter_criteria, WID_BV_CARGO_FILTER_DROPDOWN, 0, 0);
				break;

			case WID_BV_SHOW_HIDE: {
				const Engine *e = (this->sel_engine == INVALID_ENGINE) ? nullptr : Engine::Get(this->sel_engine);
				if (e != nullptr) {
					DoCommandP(0, 0, this->sel_engine | (e->IsHidden(_current_company) ? 0 : (1u << 31)), CMD_SET_VEHICLE_VISIBILITY);
				}
				break;
			}

			case WID_BV_BUILD: {
				EngineID sel_eng = this->sel_engine;
				if (sel_eng != INVALID_ENGINE) {
					CommandCallback *callback = (this->vehicle_type == VEH_TRAIN && RailVehInfo(sel_eng)->railveh_type == RAILVEH_WAGON) ? CcBuildWagon : CcBuildPrimaryVehicle;
					CargoID cargo = this->cargo_filter[this->cargo_filter_criteria];
					if (cargo == CF_ANY) cargo = CF_NONE;
					DoCommandP(this->window_number, sel_eng | (cargo << 24), 0, GetCmdBuildVeh(this->vehicle_type), callback);
				}
				break;
			}

			case WID_BV_RENAME: {
				EngineID sel_eng = this->sel_engine;
				if (sel_eng != INVALID_ENGINE) {
					this->rename_engine = sel_eng;
					SetDParam(0, sel_eng);
					ShowQueryString(STR_ENGINE_NAME, STR_QUERY_RENAME_TRAIN_TYPE_CAPTION + this->vehicle_type, MAX_LENGTH_ENGINE_NAME_CHARS, this, CS_ALPHANUMERAL, QSF_ENABLE_DEFAULT | QSF_LEN_IN_CHARS);
				}
				break;
			}
		}
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (!gui_scope) return;
		/* When switching to original acceleration model for road vehicles, clear the selected sort criteria if it is not available now. */
		if (this->vehicle_type == VEH_ROAD &&
				_settings_game.vehicle.roadveh_acceleration_model == AM_ORIGINAL &&
				this->sort_criteria > 7) {
			this->sort_criteria = 0;
			_engine_sort_last_criteria[VEH_ROAD] = 0;
		}
		this->eng_list.ForceRebuild();
	}

	void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_BV_CAPTION:
				if (this->vehicle_type == VEH_TRAIN && !this->listview_mode) {
					const RailtypeInfo *rti = GetRailTypeInfo(this->filter.railtype);
					SetDParam(0, rti->strings.build_caption);
				} else if (this->vehicle_type == VEH_ROAD && !this->listview_mode) {
					const RoadTypeInfo *rti = GetRoadTypeInfo(this->filter.roadtype);
					SetDParam(0, rti->strings.build_caption);
				} else {
					SetDParam(0, (this->listview_mode ? STR_VEHICLE_LIST_AVAILABLE_TRAINS : STR_BUY_VEHICLE_TRAIN_ALL_CAPTION) + this->vehicle_type);
				}
				break;

			case WID_BV_SORT_DROPDOWN:
				SetDParam(0, _engine_sort_listing[this->vehicle_type][this->sort_criteria]);
				break;

			case WID_BV_CARGO_FILTER_DROPDOWN:
				SetDParam(0, this->cargo_filter_texts[this->cargo_filter_criteria]);
				break;

			case WID_BV_SHOW_HIDE: {
				const Engine *e = (this->sel_engine == INVALID_ENGINE) ? nullptr : Engine::Get(this->sel_engine);
				if (e != nullptr && e->IsHidden(_local_company)) {
					SetDParam(0, STR_BUY_VEHICLE_TRAIN_SHOW_TOGGLE_BUTTON + this->vehicle_type);
				} else {
					SetDParam(0, STR_BUY_VEHICLE_TRAIN_HIDE_TOGGLE_BUTTON + this->vehicle_type);
				}
				break;
			}
		}
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_BV_LIST:
				resize->height = GetEngineListHeight(this->vehicle_type);
				size->height = 3 * resize->height;
				size->width = max(size->width, GetVehicleImageCellSize(this->vehicle_type, EIT_PURCHASE).extend_left + GetVehicleImageCellSize(this->vehicle_type, EIT_PURCHASE).extend_right + 165);
				break;

			case WID_BV_PANEL:
				size->height = this->details_height;
				break;

			case WID_BV_SORT_ASCENDING_DESCENDING: {
				Dimension d = GetStringBoundingBox(this->GetWidget<NWidgetCore>(widget)->widget_data);
				d.width += padding.width + Window::SortButtonWidth() * 2; // Doubled since the string is centred and it also looks better.
				d.height += padding.height;
				*size = maxdim(*size, d);
				break;
			}

			case WID_BV_BUILD:
				*size = GetStringBoundingBox(STR_BUY_VEHICLE_TRAIN_BUY_VEHICLE_BUTTON + this->vehicle_type);
				*size = maxdim(*size, GetStringBoundingBox(STR_BUY_VEHICLE_TRAIN_BUY_REFIT_VEHICLE_BUTTON + this->vehicle_type));
				size->width += padding.width;
				size->height += padding.height;
				break;

			case WID_BV_SHOW_HIDE:
				*size = GetStringBoundingBox(STR_BUY_VEHICLE_TRAIN_HIDE_TOGGLE_BUTTON + this->vehicle_type);
				*size = maxdim(*size, GetStringBoundingBox(STR_BUY_VEHICLE_TRAIN_SHOW_TOGGLE_BUTTON + this->vehicle_type));
				size->width += padding.width;
				size->height += padding.height;
				break;
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_BV_LIST:
				DrawEngineList(this->vehicle_type, r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, r.top + WD_FRAMERECT_TOP, &this->eng_list, this->vscroll->GetPosition(), min(this->vscroll->GetPosition() + this->vscroll->GetCapacity(), (uint)this->eng_list.size()), this->sel_engine, false, DEFAULT_GROUP);
				break;

			case WID_BV_SORT_ASCENDING_DESCENDING:
				this->DrawSortButtonState(WID_BV_SORT_ASCENDING_DESCENDING, this->descending_sort_order ? SBS_DOWN : SBS_UP);
				break;
		}
	}

	void OnPaint() override
	{
		this->GenerateBuildList();
		this->vscroll->SetCount((uint)this->eng_list.size());

		this->SetWidgetsDisabledState(this->sel_engine == INVALID_ENGINE, WID_BV_SHOW_HIDE, WID_BV_BUILD, WID_BV_RENAME, WIDGET_LIST_END);

		this->DrawWidgets();

		if (!this->IsShaded()) {
			int needed_height = this->details_height;
			/* Draw details panels. */
			if (this->sel_engine != INVALID_ENGINE) {
				NWidgetBase *nwi = this->GetWidget<NWidgetBase>(WID_BV_PANEL);
				int text_end = DrawVehiclePurchaseInfo(nwi->pos_x + WD_FRAMETEXT_LEFT, nwi->pos_x + nwi->current_x - WD_FRAMETEXT_RIGHT,
						nwi->pos_y + WD_FRAMERECT_TOP, this->sel_engine, this->te);
				needed_height = max(needed_height, text_end - (int)nwi->pos_y + WD_FRAMERECT_BOTTOM);
			}
			if (needed_height != this->details_height) { // Details window are not high enough, enlarge them.
				int resize = needed_height - this->details_height;
				this->details_height = needed_height;
				this->ReInit(0, resize);
				return;
			}
		}
	}

	void OnQueryTextFinished(char *str) override
	{
		if (str == nullptr) return;

		DoCommandP(0, this->rename_engine, 0, CMD_RENAME_ENGINE | CMD_MSG(STR_ERROR_CAN_T_RENAME_TRAIN_TYPE + this->vehicle_type), nullptr, str);
	}

	void OnDropdownSelect(int widget, int index) override
	{
		switch (widget) {
			case WID_BV_SORT_DROPDOWN:
				if (this->sort_criteria != index) {
					this->sort_criteria = index;
					_engine_sort_last_criteria[this->vehicle_type] = this->sort_criteria;
					this->eng_list.ForceRebuild();
				}
				break;

			case WID_BV_CARGO_FILTER_DROPDOWN: // Select a cargo filter criteria
				if (this->cargo_filter_criteria != index) {
					this->cargo_filter_criteria = index;
					_engine_sort_last_cargo_criteria[this->vehicle_type] = this->cargo_filter[this->cargo_filter_criteria];
					/* deactivate filter if criteria is 'Show All', activate it otherwise */
					this->eng_list.SetFilterState(this->cargo_filter[this->cargo_filter_criteria] != CF_ANY);
					this->eng_list.ForceRebuild();
					this->SelectEngine(this->sel_engine);
				}
				break;
		}
		this->SetDirty();
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_BV_LIST);
	}
};

static WindowDesc _build_vehicle_desc(
	WDP_AUTO, "build_vehicle", 240, 268,
	WC_BUILD_VEHICLE, WC_NONE,
	WDF_CONSTRUCTION,
	_nested_build_vehicle_widgets, lengthof(_nested_build_vehicle_widgets)
);

void ShowBuildVehicleWindow(TileIndex tile, VehicleType type)
{
	/* We want to be able to open both Available Train as Available Ships,
	 *  so if tile == INVALID_TILE (Available XXX Window), use 'type' as unique number.
	 *  As it always is a low value, it won't collide with any real tile
	 *  number. */
	uint num = (tile == INVALID_TILE) ? (int)type : tile;

	assert(IsCompanyBuildableVehicleType(type));

	DeleteWindowById(WC_BUILD_VEHICLE, num);

	new BuildVehicleWindow(&_build_vehicle_desc, tile, type);
}
