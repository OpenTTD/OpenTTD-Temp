/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file timetable_gui.cpp GUI for time tabling. */

#include "stdafx.h"
#include "command_func.h"
#include "gui.h"
#include "window_gui.h"
#include "window_func.h"
#include "textbuf_gui.h"
#include "strings_func.h"
#include "vehicle_base.h"
#include "string_func.h"
#include "gfx_func.h"
#include "company_func.h"
#include "date_func.h"
#include "date_gui.h"
#include "vehicle_gui.h"
#include "settings_type.h"

#include "widgets/timetable_widget.h"

#include "table/sprites.h"
#include "table/strings.h"

#include "safeguards.h"



/** Container for the arrival/departure dates of a vehicle */
struct TimetableArrivalDeparture {
	Ticks arrival;   ///< The arrival time
	Ticks departure; ///< The departure time
};

/**
 * Set the timetable parameters in the format as described by the setting.
 * @param param1 the first DParam to fill
 * @param param2 the second DParam to fill
 * @param ticks  the number of ticks to 'draw'
 */
void SetTimetableParams(int param1, int param2, Ticks ticks)
{
	if (_settings_client.gui.timetable_in_ticks) {
		SetDParam(param1, STR_TIMETABLE_TICKS);
		SetDParam(param2, ticks);
	} else {
		SetDParam(param1, STR_TIMETABLE_DAYS);
		SetDParam(param2, ticks / DAY_TICKS);
	}
}

/**
 * Check whether it is possible to determine how long the order takes.
 * @param order the order to check.
 * @param travelling whether we are interested in the travel or the wait part.
 * @return true if the travel/wait time can be used.
 */
static bool CanDetermineTimeTaken(const Order *order, bool travelling)
{
	/* Current order is conditional */
	if (order->IsType(OT_CONDITIONAL) || order->IsType(OT_IMPLICIT)) return false;
	/* No travel time and we have not already finished travelling */
	if (travelling && !order->IsTravelTimetabled()) return false;
	/* No wait time but we are loading at this timetabled station */
	if (!travelling && !order->IsWaitTimetabled() && order->IsType(OT_GOTO_STATION) &&
			!(order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION)) {
		return false;
	}

	return true;
}


/**
 * Fill the table with arrivals and departures
 * @param v Vehicle which must have at least 2 orders.
 * @param start order index to start at
 * @param travelling Are we still in the travelling part of the start order
 * @param table Fill in arrival and departures including intermediate orders
 * @param offset Add this value to result and all arrivals and departures
 */
static void FillTimetableArrivalDepartureTable(const Vehicle *v, VehicleOrderID start, bool travelling, TimetableArrivalDeparture *table, Ticks offset)
{
	assert(table != nullptr);
	assert(v->GetNumOrders() >= 2);
	assert(start < v->GetNumOrders());

	Ticks sum = offset;
	VehicleOrderID i = start;
	const Order *order = v->GetOrder(i);

	/* Pre-initialize with unknown time */
	for (int i = 0; i < v->GetNumOrders(); ++i) {
		table[i].arrival = table[i].departure = INVALID_TICKS;
	}

	/* Cyclically loop over all orders until we reach the current one again.
	 * As we may start at the current order, do a post-checking loop */
	do {
		/* Automatic orders don't influence the overall timetable;
		 * they just add some untimetabled entries, but the time till
		 * the next non-implicit order can still be known. */
		if (!order->IsType(OT_IMPLICIT)) {
			if (travelling || i != start) {
				if (!CanDetermineTimeTaken(order, true)) return;
				sum += order->GetTimetabledTravel();
				table[i].arrival = sum;
			}

			if (!CanDetermineTimeTaken(order, false)) return;
			sum += order->GetTimetabledWait();
			table[i].departure = sum;
		}

		++i;
		order = order->next;
		if (i >= v->GetNumOrders()) {
			i = 0;
			assert(order == nullptr);
			order = v->orders.list->GetFirstOrder();
		}
	} while (i != start);

	/* When loading at a scheduled station we still have to treat the
	 * travelling part of the first order. */
	if (!travelling) {
		if (!CanDetermineTimeTaken(order, true)) return;
		sum += order->GetTimetabledTravel();
		table[i].arrival = sum;
	}
}


/**
 * Callback for when a time has been chosen to start the time table
 * @param w the window related to the setting of the date
 * @param date the actually chosen date
 */
static void ChangeTimetableStartCallback(const Window *w, Date date)
{
	DoCommandP(0, w->window_number, date, CMD_SET_TIMETABLE_START | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));
}

enum QueryType
{
	TIMETABLE_QT_SPEED,
	TIMETABLE_QT_CHANGE_TIME,
	TIMETABLE_QT_MULTIPLY_TIME,
	TIMETABLE_QT_ADD_TIME,
};

struct TimetableWindow : Window {
	int sel_index;
	const Vehicle *vehicle; ///< Vehicle monitored by the window.
	bool show_expected;     ///< Whether we show expected arrival or scheduled
	uint deparr_time_width; ///< The width of the departure/arrival time
	uint deparr_abbr_width; ///< The width of the departure/arrival abbreviation
	Scrollbar *vscroll;
	QueryType query_type;

	TimetableWindow(WindowDesc *desc, WindowNumber window_number) :
			Window(desc),
			sel_index(-1),
			vehicle(Vehicle::Get(window_number)),
			show_expected(true)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_VT_SCROLLBAR);
		this->UpdateSelectionStates();
		this->FinishInitNested(window_number);

		this->owner = this->vehicle->owner;
	}

	/**
	 * Build the arrival-departure list for a given vehicle
	 * @param v the vehicle to make the list for
	 * @param table the table to fill
	 * @return if next arrival will be early
	 */
	static bool BuildArrivalDepartureList(const Vehicle *v, TimetableArrivalDeparture *table)
	{
		assert(HasBit(v->vehicle_flags, VF_TIMETABLE_STARTED));

		bool travelling = (!(v->current_order.IsType(OT_LOADING) || v->current_order.IsType(OT_WAITING)) || v->current_order.GetNonStopType() == ONSF_STOP_EVERYWHERE);
		Ticks start_time = _date_fract - v->current_order_time;

		FillTimetableArrivalDepartureTable(v, v->cur_real_order_index % v->GetNumOrders(), travelling, table, start_time);

		return (travelling && v->lateness_counter < 0);
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_VT_ARRIVAL_DEPARTURE_PANEL:
				SetDParamMaxValue(0, MAX_YEAR * DAYS_IN_YEAR, 0, FS_SMALL);
				this->deparr_time_width = GetStringBoundingBox(STR_JUST_DATE_TINY).width;
				this->deparr_abbr_width = std::max(GetStringBoundingBox(STR_TIMETABLE_ARRIVAL_ABBREVIATION).width, GetStringBoundingBox(STR_TIMETABLE_DEPARTURE_ABBREVIATION).width);
				size->width = WD_FRAMERECT_LEFT + this->deparr_abbr_width + 10 + this->deparr_time_width + WD_FRAMERECT_RIGHT;
				FALLTHROUGH;

			case WID_VT_ARRIVAL_DEPARTURE_SELECTION:
			case WID_VT_TIMETABLE_PANEL:
				resize->height = FONT_HEIGHT_NORMAL;
				size->height = WD_FRAMERECT_TOP + 8 * resize->height + WD_FRAMERECT_BOTTOM;
				break;

			case WID_VT_SUMMARY_PANEL:
				size->height = WD_FRAMERECT_TOP + 2 * FONT_HEIGHT_NORMAL + WD_FRAMERECT_BOTTOM;
				break;
		}
	}

	int GetOrderFromTimetableWndPt(int y, const Vehicle *v)
	{
		int sel = (y - this->GetWidget<NWidgetBase>(WID_VT_TIMETABLE_PANEL)->pos_y - WD_FRAMERECT_TOP) / FONT_HEIGHT_NORMAL;

		if ((uint)sel >= this->vscroll->GetCapacity()) return INVALID_ORDER;

		sel += this->vscroll->GetPosition();

		return (sel < v->GetNumOrders() * 2 && sel >= 0) ? sel : INVALID_ORDER;
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		switch (data) {
			case VIWD_AUTOREPLACE:
				/* Autoreplace replaced the vehicle */
				this->vehicle = Vehicle::Get(this->window_number);
				break;

			case VIWD_REMOVE_ALL_ORDERS:
				/* Removed / replaced all orders (after deleting / sharing) */
				if (this->sel_index == -1) break;

				this->DeleteChildWindows();
				this->sel_index = -1;
				break;

			case VIWD_MODIFY_ORDERS:
				if (!gui_scope) break;
				this->UpdateSelectionStates();
				this->ReInit();
				break;

			default: {
				if (gui_scope) break; // only do this once; from command scope

				/* Moving an order. If one of these is INVALID_VEH_ORDER_ID, then
				 * the order is being created / removed */
				if (this->sel_index == -1) break;

				VehicleOrderID from = GB(data, 0, 8);
				VehicleOrderID to   = GB(data, 8, 8);

				if (from == to) break; // no need to change anything

				/* if from == INVALID_VEH_ORDER_ID, one order was added; if to == INVALID_VEH_ORDER_ID, one order was removed */
				uint old_num_orders = this->vehicle->GetNumOrders() - (uint)(from == INVALID_VEH_ORDER_ID) + (uint)(to == INVALID_VEH_ORDER_ID);

				VehicleOrderID selected_order = (this->sel_index + 1) / 2;
				if (selected_order == old_num_orders) selected_order = 0; // when last travel time is selected, it belongs to order 0

				bool travel = HasBit(this->sel_index, 0);

				if (from != selected_order) {
					/* Moving from preceding order? */
					selected_order -= (int)(from <= selected_order);
					/* Moving to   preceding order? */
					selected_order += (int)(to   <= selected_order);
				} else {
					/* Now we are modifying the selected order */
					if (to == INVALID_VEH_ORDER_ID) {
						/* Deleting selected order */
						this->DeleteChildWindows();
						this->sel_index = -1;
						break;
					} else {
						/* Moving selected order */
						selected_order = to;
					}
				}

				/* recompute new sel_index */
				this->sel_index = 2 * selected_order - (int)travel;
				/* travel time of first order needs special handling */
				if (this->sel_index == -1) this->sel_index = this->vehicle->GetNumOrders() * 2 - 1;
				break;
			}
		}
	}


	void OnPaint() override
	{
		const Vehicle *v = this->vehicle;
		int selected = this->sel_index;

		this->vscroll->SetCount(v->GetNumOrders() * 2);

		if (v->owner == _local_company) {
			bool disable = true;
			if (selected != -1) {
				const Order *order = v->GetOrder(((selected + 1) / 2) % v->GetNumOrders());
				if (selected % 2 == 1) {
					disable = order != nullptr && (order->IsType(OT_CONDITIONAL) || order->IsType(OT_IMPLICIT));
				} else {
					disable = order == nullptr || ((!(order->IsType(OT_GOTO_STATION) || (order->IsType(OT_GOTO_DEPOT) && !(order->GetDepotActionType() & ODATFB_HALT))) || (order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION)) && !order->IsType(OT_CONDITIONAL));
				}
			}
			bool disable_speed = disable || selected % 2 != 1 || v->type == VEH_AIRCRAFT;

			this->SetWidgetDisabledState(WID_VT_CHANGE_TIME, disable);
			this->SetWidgetDisabledState(WID_VT_CLEAR_TIME, disable);
			this->SetWidgetDisabledState(WID_VT_CHANGE_SPEED, disable_speed);
			this->SetWidgetDisabledState(WID_VT_CLEAR_SPEED, disable_speed);
			this->SetWidgetDisabledState(WID_VT_SHARED_ORDER_LIST, !v->IsOrderListShared());

			this->SetWidgetDisabledState(WID_VT_START_DATE, v->orders.list == nullptr);
			this->SetWidgetDisabledState(WID_VT_RESET_LATENESS, v->orders.list == nullptr);
			this->SetWidgetDisabledState(WID_VT_AUTOFILL, v->orders.list == nullptr);
		} else {
			this->DisableWidget(WID_VT_START_DATE);
			this->DisableWidget(WID_VT_CHANGE_TIME);
			this->DisableWidget(WID_VT_CLEAR_TIME);
			this->DisableWidget(WID_VT_CHANGE_SPEED);
			this->DisableWidget(WID_VT_CLEAR_SPEED);
			this->DisableWidget(WID_VT_RESET_LATENESS);
			this->DisableWidget(WID_VT_AUTOFILL);
			this->DisableWidget(WID_VT_SHARED_ORDER_LIST);
		}

		this->SetWidgetLoweredState(WID_VT_AUTOFILL, HasBit(v->vehicle_flags, VF_AUTOFILL_TIMETABLE));

		this->DrawWidgets();
	}

	void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_VT_CAPTION: SetDParam(0, this->vehicle->index); break;
			case WID_VT_EXPECTED: SetDParam(0, this->show_expected ? STR_TIMETABLE_EXPECTED : STR_TIMETABLE_SCHEDULED); break;
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		const Vehicle *v = this->vehicle;
		int selected = this->sel_index;

		switch (widget) {
			case WID_VT_TIMETABLE_PANEL: {
				int y = r.top + WD_FRAMERECT_TOP;
				int i = this->vscroll->GetPosition();
				VehicleOrderID order_id = (i + 1) / 2;
				bool final_order = false;

				bool rtl = _current_text_dir == TD_RTL;
				SetDParamMaxValue(0, v->GetNumOrders(), 2);
				int index_column_width = GetStringBoundingBox(STR_ORDER_INDEX).width + 2 * GetSpriteSize(rtl ? SPR_ARROW_RIGHT : SPR_ARROW_LEFT).width + 3;
				int middle = rtl ? r.right - WD_FRAMERECT_RIGHT - index_column_width : r.left + WD_FRAMERECT_LEFT + index_column_width;

				const Order *order = v->GetOrder(order_id);
				while (order != nullptr) {
					/* Don't draw anything if it extends past the end of the window. */
					if (!this->vscroll->IsVisible(i)) break;

					if (i % 2 == 0) {
						DrawOrderString(v, order, order_id, y, i == selected, true, r.left + WD_FRAMERECT_LEFT, middle, r.right - WD_FRAMERECT_RIGHT);

						order_id++;

						if (order_id >= v->GetNumOrders()) {
							order = v->GetOrder(0);
							final_order = true;
						} else {
							order = order->next;
						}
					} else {
						StringID string;
						TextColour colour = (i == selected) ? TC_WHITE : TC_BLACK;
						if (order->IsType(OT_CONDITIONAL)) {
							string = STR_TIMETABLE_NO_TRAVEL;
						} else if (order->IsType(OT_IMPLICIT)) {
							string = STR_TIMETABLE_NOT_TIMETABLEABLE;
							colour = ((i == selected) ? TC_SILVER : TC_GREY) | TC_NO_SHADE;
						} else if (!order->IsTravelTimetabled()) {
							if (order->GetTravelTime() > 0) {
								SetTimetableParams(0, 1, order->GetTravelTime());
								string = order->GetMaxSpeed() != UINT16_MAX ?
										STR_TIMETABLE_TRAVEL_FOR_SPEED_ESTIMATED  :
										STR_TIMETABLE_TRAVEL_FOR_ESTIMATED;
							} else {
								string = order->GetMaxSpeed() != UINT16_MAX ?
										STR_TIMETABLE_TRAVEL_NOT_TIMETABLED_SPEED :
										STR_TIMETABLE_TRAVEL_NOT_TIMETABLED;
							}
						} else {
							SetTimetableParams(0, 1, order->GetTimetabledTravel());
							string = order->GetMaxSpeed() != UINT16_MAX ?
									STR_TIMETABLE_TRAVEL_FOR_SPEED : STR_TIMETABLE_TRAVEL_FOR;
						}
						SetDParam(2, order->GetMaxSpeed());

						DrawString(rtl ? r.left + WD_FRAMERECT_LEFT : middle, rtl ? middle : r.right - WD_FRAMERECT_LEFT, y, string, colour);

						if (final_order) break;
					}

					i++;
					y += FONT_HEIGHT_NORMAL;
				}
				break;
			}

			case WID_VT_ARRIVAL_DEPARTURE_PANEL: {
				/* Arrival and departure times are handled in an all-or-nothing approach,
				 * i.e. are only shown if we can calculate all times.
				 * Excluding order lists with only one order makes some things easier.
				 */
				Ticks total_time = v->orders.list != nullptr ? v->orders.list->GetTimetableDurationIncomplete() : 0;
				if (total_time <= 0 || v->GetNumOrders() <= 1 || !HasBit(v->vehicle_flags, VF_TIMETABLE_STARTED)) break;

				TimetableArrivalDeparture *arr_dep = AllocaM(TimetableArrivalDeparture, v->GetNumOrders());
				const VehicleOrderID cur_order = v->cur_real_order_index % v->GetNumOrders();

				VehicleOrderID earlyID = BuildArrivalDepartureList(v, arr_dep) ? cur_order : (VehicleOrderID)INVALID_VEH_ORDER_ID;

				int y = r.top + WD_FRAMERECT_TOP;

				bool show_late = this->show_expected && v->lateness_counter > DAY_TICKS;
				Ticks offset = show_late ? 0 : -v->lateness_counter;

				bool rtl = _current_text_dir == TD_RTL;
				int abbr_left  = rtl ? r.right - WD_FRAMERECT_RIGHT - this->deparr_abbr_width : r.left + WD_FRAMERECT_LEFT;
				int abbr_right = rtl ? r.right - WD_FRAMERECT_RIGHT : r.left + WD_FRAMERECT_LEFT + this->deparr_abbr_width;
				int time_left  = rtl ? r.left + WD_FRAMERECT_LEFT : r.right - WD_FRAMERECT_RIGHT - this->deparr_time_width;
				int time_right = rtl ? r.left + WD_FRAMERECT_LEFT + this->deparr_time_width : r.right - WD_FRAMERECT_RIGHT;

				for (int i = this->vscroll->GetPosition(); i / 2 < v->GetNumOrders(); ++i) { // note: i is also incremented in the loop
					/* Don't draw anything if it extends past the end of the window. */
					if (!this->vscroll->IsVisible(i)) break;

					if (i % 2 == 0) {
						if (arr_dep[i / 2].arrival != INVALID_TICKS) {
							DrawString(abbr_left, abbr_right, y, STR_TIMETABLE_ARRIVAL_ABBREVIATION, i == selected ? TC_WHITE : TC_BLACK);
							if (this->show_expected && i / 2 == earlyID) {
								SetDParam(0, _date + arr_dep[i / 2].arrival / DAY_TICKS);
								DrawString(time_left, time_right, y, STR_JUST_DATE_TINY, TC_GREEN);
							} else {
								SetDParam(0, _date + (arr_dep[i / 2].arrival + offset) / DAY_TICKS);
								DrawString(time_left, time_right, y, STR_JUST_DATE_TINY,
										show_late ? TC_RED : i == selected ? TC_WHITE : TC_BLACK);
							}
						}
					} else {
						if (arr_dep[i / 2].departure != INVALID_TICKS) {
							DrawString(abbr_left, abbr_right, y, STR_TIMETABLE_DEPARTURE_ABBREVIATION, i == selected ? TC_WHITE : TC_BLACK);
							SetDParam(0, _date + (arr_dep[i/2].departure + offset) / DAY_TICKS);
							DrawString(time_left, time_right, y, STR_JUST_DATE_TINY,
									show_late ? TC_RED : i == selected ? TC_WHITE : TC_BLACK);
						}
					}
					y += FONT_HEIGHT_NORMAL;
				}
				break;
			}

			case WID_VT_SUMMARY_PANEL: {
				int y = r.top + WD_FRAMERECT_TOP;

				Ticks total_time = v->orders.list != nullptr ? v->orders.list->GetTimetableDurationIncomplete() : 0;
				if (total_time != 0) {
					SetTimetableParams(0, 1, total_time);
					DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, v->orders.list->IsCompleteTimetable() ? STR_TIMETABLE_TOTAL_TIME : STR_TIMETABLE_TOTAL_TIME_INCOMPLETE);
				}
				y += FONT_HEIGHT_NORMAL;

				if (v->timetable_start != 0) {
					/* We are running towards the first station so we can start the
					 * timetable at the given time. */
					SetDParam(0, STR_JUST_DATE_TINY);
					SetDParam(1, v->timetable_start);
					DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_TIMETABLE_STATUS_START_AT);
				} else if (!HasBit(v->vehicle_flags, VF_TIMETABLE_STARTED)) {
					/* We aren't running on a timetable yet, so how can we be "on time"
					 * when we aren't even "on service"/"on duty"? */
					DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_TIMETABLE_STATUS_NOT_STARTED);
				} else if (v->lateness_counter == 0 || (!_settings_client.gui.timetable_in_ticks && v->lateness_counter / DAY_TICKS == 0)) {
					DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_TIMETABLE_STATUS_ON_TIME);
				} else {
					SetTimetableParams(0, 1, abs(v->lateness_counter));
					DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, v->lateness_counter < 0 ? STR_TIMETABLE_STATUS_EARLY : STR_TIMETABLE_STATUS_LATE);
				}
				break;
			}
		}
	}

	static inline uint32 PackTimetableArgs(const Vehicle *v, uint selected, QueryType query_type)
	{
		uint order_number = (selected + 1) / 2;
		ModifyTimetableFlags mtf = (selected % 2 == 1) ? (query_type==TIMETABLE_QT_SPEED ? MTF_TRAVEL_SPEED : MTF_TRAVEL_TIME) : MTF_WAIT_TIME;

		if (order_number >= v->GetNumOrders()) order_number = 0;

		return v->index | (order_number << 20) | (mtf << 28);
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		const Vehicle *v = this->vehicle;

		switch (widget) {
			case WID_VT_ORDER_VIEW: // Order view button
				ShowOrdersWindow(v);
				break;

			case WID_VT_TIMETABLE_PANEL: { // Main panel.
				int selected = GetOrderFromTimetableWndPt(pt.y, v);

				this->DeleteChildWindows();
				this->sel_index = (selected == INVALID_ORDER || selected == this->sel_index) ? -1 : selected;
				break;
			}

			case WID_VT_START_DATE: // Change the date that the timetable starts.
				ShowSetDateWindow(this, v->index | (v->orders.list->IsCompleteTimetable() && _ctrl_pressed ? 1U << 20 : 0), _date, _cur_year, _cur_year + 15, ChangeTimetableStartCallback);
				break;
			case WID_VT_MUL: {
				int selected = this->sel_index;
				VehicleOrderID real = (selected + 1) / 2;

				if (real >= v->GetNumOrders()) real = 0;

				const Order* order = v->GetOrder(real);
				StringID current = STR_EMPTY;

				if (order != nullptr) {
					SetDParam(0, 100);
					current = STR_JUST_INT;

				}

				this->query_type = TIMETABLE_QT_MULTIPLY_TIME;

				ShowQueryString(current, STR_TIMETABLE_MULTIPLY_TRAVEL_TIMES, 31, this, CS_NUMERAL, QSF_ACCEPT_UNCHANGED);
				break;
			}
			case WID_VT_ADD: {
				int selected = this->sel_index;
				VehicleOrderID real = (selected + 1) / 2;

				if (real >= v->GetNumOrders()) real = 0;

				const Order* order = v->GetOrder(real);
				StringID current = STR_EMPTY;

				if (order != nullptr) {
					SetDParam(0, 0);
					current = STR_JUST_INT;

				}

				this->query_type = TIMETABLE_QT_ADD_TIME;

				ShowQueryString(current, STR_TIMETABLE_ADD_TRAVEL_TIMES, 31, this, CS_NUMERAL, QSF_ACCEPT_UNCHANGED);
				break;
			}

			case WID_VT_CHANGE_TIME: { // "Wait For" button.
				int selected = this->sel_index;
				VehicleOrderID real = (selected + 1) / 2;

				if (real >= v->GetNumOrders()) real = 0;

				const Order *order = v->GetOrder(real);
				StringID current = STR_EMPTY;

				if (order != nullptr) {
					uint time = (selected % 2 == 1) ? order->GetTravelTime() : order->GetWaitTime();
					if (!_settings_client.gui.timetable_in_ticks) time /= DAY_TICKS;

					if (time != 0) {
						SetDParam(0, time);
						current = STR_JUST_INT;
					}
				}

				this->query_type = TIMETABLE_QT_CHANGE_TIME;

				ShowQueryString(current, STR_TIMETABLE_CHANGE_TIME, 31, this, CS_NUMERAL, QSF_ACCEPT_UNCHANGED);
				break;
			}

			case WID_VT_CHANGE_SPEED: { // Change max speed button.
				int selected = this->sel_index;
				VehicleOrderID real = (selected + 1) / 2;

				if (real >= v->GetNumOrders()) real = 0;

				StringID current = STR_EMPTY;
				const Order *order = v->GetOrder(real);
				if (order != nullptr) {
					if (order->GetMaxSpeed() != UINT16_MAX) {
						SetDParam(0, ConvertKmhishSpeedToDisplaySpeed(order->GetMaxSpeed()));
						current = STR_JUST_INT;
					}
				}

				this->query_type = TIMETABLE_QT_SPEED;

				ShowQueryString(current, STR_TIMETABLE_CHANGE_SPEED, 31, this, CS_NUMERAL, QSF_NONE);
				break;
			}

			case WID_VT_CLEAR_TIME: { // Clear waiting time.
				uint32 p1 = PackTimetableArgs(v, this->sel_index, TIMETABLE_QT_CHANGE_TIME);
				DoCommandP(0, p1, 0, CMD_CHANGE_TIMETABLE | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));
				break;
			}

			case WID_VT_CLEAR_SPEED: { // Clear max speed button.
				uint32 p1 = PackTimetableArgs(v, this->sel_index, TIMETABLE_QT_SPEED);
				DoCommandP(0, p1, UINT16_MAX, CMD_CHANGE_TIMETABLE | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));
				break;
			}

			case WID_VT_RESET_LATENESS: // Reset the vehicle's late counter.
				DoCommandP(0, v->index, 0, CMD_SET_VEHICLE_ON_TIME | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));
				break;

			case WID_VT_AUTOFILL: { // Autofill the timetable.
				uint32 p2 = 0;
				if (!HasBit(v->vehicle_flags, VF_AUTOFILL_TIMETABLE)) SetBit(p2, 0);
				if (_ctrl_pressed) SetBit(p2, 1);
				DoCommandP(0, v->index, p2, CMD_AUTOFILL_TIMETABLE | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));
				break;
			}

			case WID_VT_EXPECTED:
				this->show_expected = !this->show_expected;
				break;

			case WID_VT_SHARED_ORDER_LIST:
				ShowVehicleListWindow(v);
				break;
		}

		this->SetDirty();
	}

	void OnQueryTextFinished(char *str) override
	{
		if (str == nullptr) return;

		const Vehicle *v = this->vehicle;

		if (this->query_type == TIMETABLE_QT_ADD_TIME) {
			
			uint64 valToAdd = StrEmpty(str) ? 100 : strtoul(str, nullptr, 10);

		
			for (uint32 selected = 0; selected < (uint32)(this->vehicle->GetNumOrders() * 2); selected++)
			{
				VehicleOrderID real = (selected + 1) / 2;

				if (real >= v->GetNumOrders()) real = 0;

				const Order* order = v->GetOrder(real);
				StringID current = STR_EMPTY;

				if (order != nullptr && (selected % 2 == 1)) {

					uint32 p1 = PackTimetableArgs(v, selected, TIMETABLE_QT_CHANGE_TIME);

					uint64 val = (uint64)(order->GetTravelTime());

					if (!_settings_client.gui.timetable_in_ticks)
						val += valToAdd * DAY_TICKS;
					else
						val += valToAdd;


					uint32 p2 = std::min<uint32>(val, UINT16_MAX);

					DoCommandP(0, p1, p2, CMD_CHANGE_TIMETABLE | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));
				}
			}


		}
		else if (this->query_type == TIMETABLE_QT_MULTIPLY_TIME) {
			
			uint64 val = StrEmpty(str) ? 100 : strtoul(str, nullptr, 10);

			float valF = val / 100.0f;
		
			for(uint32 selected = 0; selected < (uint32)(this->vehicle->GetNumOrders()*2); selected++)
			{
				VehicleOrderID real = (selected + 1) / 2;

				if (real >= v->GetNumOrders()) real = 0;

				const Order* order = v->GetOrder(real);
				StringID current = STR_EMPTY;

				if (order != nullptr && (selected % 2 == 1)) {
					
					uint32 p1 = PackTimetableArgs(v, selected, TIMETABLE_QT_CHANGE_TIME);

					uint64 val = (uint64)(order->GetTravelTime());

					
					val *= valF;
		
					uint32 p2 = std::min<uint32>(val, UINT16_MAX);

					DoCommandP(0, p1, p2, CMD_CHANGE_TIMETABLE | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));
				}
			}
			
			
		}
		else
		{
			uint32 p1 = PackTimetableArgs(v, this->sel_index, this->query_type);

			uint64 val = StrEmpty(str) ? 0 : strtoul(str, nullptr, 10);

			if (this->query_type == TIMETABLE_QT_SPEED) {
				val = ConvertDisplaySpeedToKmhishSpeed(val);
			}
			else {
				if (!_settings_client.gui.timetable_in_ticks) val *= DAY_TICKS;
			}

			uint32 p2 = std::min<uint32>(val, UINT16_MAX);

			DoCommandP(0, p1, p2, CMD_CHANGE_TIMETABLE | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));
		}
		
		
	}

	void OnResize() override
	{
		/* Update the scroll bar */
		this->vscroll->SetCapacityFromWidget(this, WID_VT_TIMETABLE_PANEL, WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM);
	}

	/**
	 * Update the selection state of the arrival/departure data
	 */
	void UpdateSelectionStates()
	{
		this->GetWidget<NWidgetStacked>(WID_VT_ARRIVAL_DEPARTURE_SELECTION)->SetDisplayedPlane(_settings_client.gui.timetable_arrival_departure ? 0 : SZSP_NONE);
		this->GetWidget<NWidgetStacked>(WID_VT_EXPECTED_SELECTION)->SetDisplayedPlane(_settings_client.gui.timetable_arrival_departure ? 0 : 1);
	}
};

static const NWidgetPart _nested_timetable_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_VT_CAPTION), SetDataTip(STR_TIMETABLE_TITLE, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_ORDER_VIEW), SetMinimalSize(61, 14), SetDataTip( STR_TIMETABLE_ORDER_VIEW, STR_TIMETABLE_ORDER_VIEW_TOOLTIP),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_GREY, WID_VT_TIMETABLE_PANEL), SetMinimalSize(388, 82), SetResize(1, 10), SetDataTip(STR_NULL, STR_TIMETABLE_TOOLTIP), SetScrollbar(WID_VT_SCROLLBAR), EndContainer(),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_VT_ARRIVAL_DEPARTURE_SELECTION),
			NWidget(WWT_PANEL, COLOUR_GREY, WID_VT_ARRIVAL_DEPARTURE_PANEL), SetMinimalSize(110, 0), SetFill(0, 1), SetDataTip(STR_NULL, STR_TIMETABLE_TOOLTIP), SetScrollbar(WID_VT_SCROLLBAR), EndContainer(),
		EndContainer(),
		NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_VT_SCROLLBAR),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY, WID_VT_SUMMARY_PANEL), SetMinimalSize(400, 22), SetResize(1, 0), EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
			NWidget(NWID_VERTICAL, NC_EQUALSIZE),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_CHANGE_TIME), SetResize(1, 0), SetFill(1, 1), SetDataTip(STR_TIMETABLE_CHANGE_TIME, STR_TIMETABLE_WAIT_TIME_TOOLTIP),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_CLEAR_TIME), SetResize(1, 0), SetFill(1, 1), SetDataTip(STR_TIMETABLE_CLEAR_TIME, STR_TIMETABLE_CLEAR_TIME_TOOLTIP),
			EndContainer(),
			NWidget(NWID_VERTICAL, NC_EQUALSIZE),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_CHANGE_SPEED), SetResize(1, 0), SetFill(1, 1), SetDataTip(STR_TIMETABLE_CHANGE_SPEED, STR_TIMETABLE_CHANGE_SPEED_TOOLTIP),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_CLEAR_SPEED), SetResize(1, 0), SetFill(1, 1), SetDataTip(STR_TIMETABLE_CLEAR_SPEED, STR_TIMETABLE_CLEAR_SPEED_TOOLTIP),
			EndContainer(),
			NWidget(NWID_VERTICAL, NC_EQUALSIZE),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_START_DATE), SetResize(1, 0), SetFill(1, 1), SetDataTip(STR_TIMETABLE_STARTING_DATE, STR_TIMETABLE_STARTING_DATE_TOOLTIP),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_RESET_LATENESS), SetResize(1, 0), SetFill(1, 1), SetDataTip(STR_TIMETABLE_RESET_LATENESS, STR_TIMETABLE_RESET_LATENESS_TOOLTIP),
			EndContainer(),
			NWidget(NWID_VERTICAL, NC_EQUALSIZE),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_MUL), SetResize(1, 0), SetFill(1, 1),	SetDataTip(STR_TIMETABLE_MULTIPLY_TRAVEL_TIMES, STR_TIMETABLE_MULTIPLY_TRAVEL_TIMES_TOOLTIP),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_ADD), SetResize(1, 0), SetFill(1, 1),	SetDataTip(STR_TIMETABLE_ADD_TRAVEL_TIMES, STR_TIMETABLE_ADD_TRAVEL_TIMES_TOOLTIP),
			EndContainer(),		

			NWidget(NWID_VERTICAL, NC_EQUALSIZE),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_AUTOFILL), SetResize(1, 0), SetFill(1, 1), SetDataTip(STR_TIMETABLE_AUTOFILL, STR_TIMETABLE_AUTOFILL_TOOLTIP),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_VT_EXPECTED_SELECTION),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_EXPECTED), SetResize(1, 0), SetFill(1, 1), SetDataTip(STR_BLACK_STRING, STR_TIMETABLE_EXPECTED_TOOLTIP),
					NWidget(WWT_PANEL, COLOUR_GREY), SetResize(1, 0), SetFill(1, 1), EndContainer(),
				EndContainer(),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_VERTICAL, NC_EQUALSIZE),
			NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_VT_SHARED_ORDER_LIST), SetFill(0, 1), SetDataTip(SPR_SHARED_ORDERS_ICON, STR_ORDERS_VEH_WITH_SHARED_ORDERS_LIST_TOOLTIP),
			NWidget(WWT_RESIZEBOX, COLOUR_GREY), SetFill(0, 1),
		EndContainer(),	
	EndContainer(),
};

static WindowDesc _timetable_desc(
	WDP_AUTO, "view_vehicle_timetable", 400, 150,
	WC_VEHICLE_TIMETABLE, WC_VEHICLE_VIEW,
	WDF_CONSTRUCTION,
	_nested_timetable_widgets, lengthof(_nested_timetable_widgets)
);

/**
 * Show the timetable for a given vehicle.
 * @param v The vehicle to show the timetable for.
 */
void ShowTimetableWindow(const Vehicle *v)
{
	DeleteWindowById(WC_VEHICLE_DETAILS, v->index, false);
	DeleteWindowById(WC_VEHICLE_ORDERS, v->index, false);
	AllocateWindowDescFront<TimetableWindow>(&_timetable_desc, v->index);
}
