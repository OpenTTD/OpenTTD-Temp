/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file depot_gui.cpp The GUI for depots. */

#include "stdafx.h"
#include "train.h"
#include "roadveh.h"
#include "ship.h"
#include "aircraft.h"
#include "gui.h"
#include "textbuf_gui.h"
#include "viewport_func.h"
#include "command_func.h"
#include "depot_base.h"
#include "spritecache.h"
#include "strings_func.h"
#include "vehicle_func.h"
#include "company_func.h"
#include "tilehighlight_func.h"
#include "window_gui.h"
#include "vehiclelist.h"
#include "vehicle_func.h"
#include "order_backup.h"
#include "zoom_func.h"
#include "error.h"

#include "widgets/depot_widget.h"

#include "table/strings.h"

#include "safeguards.h"

/*
 * Since all depot window sizes aren't the same, we need to modify sizes a little.
 * It's done with the following arrays of widget indexes. Each of them tells if a widget side should be moved and in what direction.
 * How long they should be moved and for what window types are controlled in ShowDepotWindow()
 */

/** Nested widget definition for train depots. */
static const NWidgetPart _nested_train_depot_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_D_CAPTION), SetDataTip(STR_DEPOT_CAPTION, STR_NULL),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_VERTICAL),
			NWidget(WWT_MATRIX, COLOUR_GREY, WID_D_MATRIX), SetResize(1, 1), SetScrollbar(WID_D_V_SCROLL),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_D_SHOW_H_SCROLL),
				NWidget(NWID_HSCROLLBAR, COLOUR_GREY, WID_D_H_SCROLL),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_VERTICAL),
			NWidget(WWT_IMGBTN, COLOUR_GREY, WID_D_SELL), SetDataTip(0x0, STR_NULL), SetResize(0, 1), SetFill(0, 1),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_D_SHOW_SELL_CHAIN),
				NWidget(WWT_IMGBTN, COLOUR_GREY, WID_D_SELL_CHAIN), SetDataTip(SPR_SELL_CHAIN_TRAIN, STR_DEPOT_DRAG_WHOLE_TRAIN_TO_SELL_TOOLTIP), SetResize(0, 1), SetFill(0, 1),
			EndContainer(),
			NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_D_SELL_ALL), SetDataTip(0x0, STR_NULL),
			NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_D_AUTOREPLACE), SetDataTip(0x0, STR_NULL),
		EndContainer(),
		NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_D_V_SCROLL),
	EndContainer(),
	NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_D_BUILD), SetDataTip(0x0, STR_NULL), SetFill(1, 1), SetResize(1, 0),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_D_CLONE), SetDataTip(0x0, STR_NULL), SetFill(1, 1), SetResize(1, 0),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_D_LOCATION), SetDataTip(STR_BUTTON_LOCATION, STR_NULL), SetFill(1, 1), SetResize(1, 0),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_D_SHOW_RENAME), // rename button
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_D_RENAME), SetDataTip(STR_BUTTON_RENAME, STR_DEPOT_RENAME_TOOLTIP), SetFill(1, 1), SetResize(1, 0),
		EndContainer(),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_D_VEHICLE_LIST), SetDataTip(0x0, STR_NULL), SetFill(0, 1),
		NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_D_STOP_ALL), SetDataTip(SPR_FLAG_VEH_STOPPED, STR_NULL), SetFill(0, 1),
		NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_D_START_ALL), SetDataTip(SPR_FLAG_VEH_RUNNING, STR_NULL), SetFill(0, 1),
		NWidget(WWT_RESIZEBOX, COLOUR_GREY),
	EndContainer(),
};

static WindowDesc _train_depot_desc(
	WDP_AUTO, "depot_train", 362, 123,
	WC_VEHICLE_DEPOT, WC_NONE,
	0,
	_nested_train_depot_widgets, lengthof(_nested_train_depot_widgets)
);

static WindowDesc _road_depot_desc(
	WDP_AUTO, "depot_roadveh", 316, 97,
	WC_VEHICLE_DEPOT, WC_NONE,
	0,
	_nested_train_depot_widgets, lengthof(_nested_train_depot_widgets)
);

static WindowDesc _ship_depot_desc(
	WDP_AUTO, "depot_ship", 306, 99,
	WC_VEHICLE_DEPOT, WC_NONE,
	0,
	_nested_train_depot_widgets, lengthof(_nested_train_depot_widgets)
);

static WindowDesc _aircraft_depot_desc(
	WDP_AUTO, "depot_aircraft", 332, 99,
	WC_VEHICLE_DEPOT, WC_NONE,
	0,
	_nested_train_depot_widgets, lengthof(_nested_train_depot_widgets)
);

extern void DepotSortList(VehicleList *list);

/**
 * This is the Callback method after the cloning attempt of a vehicle
 * @param result the result of the cloning command
 * @param tile unused
 * @param p1 unused
 * @param p2 unused
 * @param cmd unused
 */
void CcCloneVehicle(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2, uint32 cmd)
{
	if (result.Failed()) return;

	const Vehicle *v = Vehicle::Get(_new_vehicle_id);

	ShowVehicleViewWindow(v);
}

static void TrainDepotMoveVehicle(const Vehicle *wagon, VehicleID sel, const Vehicle *head)
{
	const Vehicle *v = Vehicle::Get(sel);

	if (v == wagon) return;

	if (wagon == nullptr) {
		if (head != nullptr) wagon = head->Last();
	} else {
		wagon = wagon->Previous();
		if (wagon == nullptr) return;
	}

	if (wagon == v) return;

	DoCommandP(v->tile, v->index | (_ctrl_pressed ? 1 : 0) << 20, wagon == nullptr ? INVALID_VEHICLE : wagon->index, CMD_MOVE_RAIL_VEHICLE | CMD_MSG(STR_ERROR_CAN_T_MOVE_VEHICLE));
}

static VehicleCellSize _base_block_sizes_depot[VEH_COMPANY_END];    ///< Cell size for vehicle images in the depot view.
static VehicleCellSize _base_block_sizes_purchase[VEH_COMPANY_END]; ///< Cell size for vehicle images in the purchase list.
static uint _consistent_train_width;                                ///< Whether trains of all lengths are consistently scaled. Either TRAININFO_DEFAULT_VEHICLE_WIDTH, VEHICLEINFO_FULL_VEHICLE_WIDTH, or 0.

/**
 * Get the GUI cell size for a vehicle image.
 * @param type Vehicle type to get the size for.
 * @param image_type Image type to get size for.
 * @pre image_type == EIT_IN_DEPOT || image_type == EIT_PURCHASE
 * @return Cell dimensions for the vehicle and image type.
 */
VehicleCellSize GetVehicleImageCellSize(VehicleType type, EngineImageType image_type)
{
	switch (image_type) {
		case EIT_IN_DEPOT: return _base_block_sizes_depot[type];
		case EIT_PURCHASE: return _base_block_sizes_purchase[type];
		default: NOT_REACHED();
	}
}

static void InitBlocksizeForVehicles(VehicleType type, EngineImageType image_type)
{
	int max_extend_left  = 0;
	int max_extend_right = 0;
	uint max_height = 0;

	for (const Engine *e : Engine::IterateType(type)) {
		if (!e->IsEnabled()) continue;

		EngineID eid = e->index;
		uint x, y;
		int x_offs, y_offs;

		switch (type) {
			default: NOT_REACHED();
			case VEH_TRAIN:    GetTrainSpriteSize(   eid, x, y, x_offs, y_offs, image_type); break;
			case VEH_ROAD:     GetRoadVehSpriteSize( eid, x, y, x_offs, y_offs, image_type); break;
			case VEH_SHIP:     GetShipSpriteSize(    eid, x, y, x_offs, y_offs, image_type); break;
			case VEH_AIRCRAFT: GetAircraftSpriteSize(eid, x, y, x_offs, y_offs, image_type); break;
		}
		if (y > max_height) max_height = y;
		if (-x_offs > max_extend_left) max_extend_left = -x_offs;
		if ((int)x + x_offs > max_extend_right) max_extend_right = x + x_offs;
	}

	int min_extend = ScaleGUITrad(16);
	int max_extend = ScaleGUITrad(98);

	switch (image_type) {
		case EIT_IN_DEPOT:
			_base_block_sizes_depot[type].height       = max<uint>(ScaleGUITrad(GetVehicleHeight(type)), max_height);
			_base_block_sizes_depot[type].extend_left  = Clamp(max_extend_left, min_extend, max_extend);
			_base_block_sizes_depot[type].extend_right = Clamp(max_extend_right, min_extend, max_extend);
			break;
		case EIT_PURCHASE:
			_base_block_sizes_purchase[type].height       = max<uint>(ScaleGUITrad(GetVehicleHeight(type)), max_height);
			_base_block_sizes_purchase[type].extend_left  = Clamp(max_extend_left, min_extend, max_extend);
			_base_block_sizes_purchase[type].extend_right = Clamp(max_extend_right, min_extend, max_extend);
			break;

		default: NOT_REACHED();
	}
}

/**
 * Set the size of the blocks in the window so we can be sure that they are big enough for the vehicle sprites in the current game.
 * @note Calling this function once for each game is enough.
 */
void InitDepotWindowBlockSizes()
{
	for (VehicleType vt = VEH_BEGIN; vt < VEH_COMPANY_END; vt++) {
		InitBlocksizeForVehicles(vt, EIT_IN_DEPOT);
		InitBlocksizeForVehicles(vt, EIT_PURCHASE);
	}

	_consistent_train_width = TRAININFO_DEFAULT_VEHICLE_WIDTH;
	bool first = true;
	for (const Engine *e : Engine::IterateType(VEH_TRAIN)) {
		if (!e->IsEnabled()) continue;

		uint w = TRAININFO_DEFAULT_VEHICLE_WIDTH;
		if (e->GetGRF() != nullptr && is_custom_sprite(e->u.rail.image_index)) {
			w = e->GetGRF()->traininfo_vehicle_width;
			if (w != VEHICLEINFO_FULL_VEHICLE_WIDTH) {
				/* Hopeless.
				 * This is a NewGRF vehicle that uses TRAININFO_DEFAULT_VEHICLE_WIDTH.
				 * If the vehicles are shorter than 8/8 we have fractional lengths, which are not consistent after rounding.
				 */
				_consistent_train_width = 0;
				break;
			}
		}

		if (first) {
			_consistent_train_width = w;
			first = false;
		} else if (w != _consistent_train_width) {
			_consistent_train_width = 0;
			break;
		}
	}
}

static void DepotSellAllConfirmationCallback(Window *w, bool confirmed);
const Sprite *GetAircraftSprite(EngineID engine);

struct DepotWindow : Window {
	VehicleID sel;
	VehicleID vehicle_over; ///< Rail vehicle over which another one is dragged, \c INVALID_VEHICLE if none.
	VehicleType type;
	bool generate_list;
	int hovered_widget; ///< Index of the widget being hovered during drag/drop. -1 if no drag is in progress.
	VehicleList vehicle_list;
	VehicleList wagon_list;
	uint unitnumber_digits;
	uint num_columns;       ///< Number of columns.
	Scrollbar *hscroll;     ///< Only for trains.
	Scrollbar *vscroll;

	DepotWindow(WindowDesc *desc, TileIndex tile, VehicleType type) : Window(desc)
	{
		assert(IsCompanyBuildableVehicleType(type)); // ensure that we make the call with a valid type

		this->sel = INVALID_VEHICLE;
		this->vehicle_over = INVALID_VEHICLE;
		this->generate_list = true;
		this->hovered_widget = -1;
		this->type = type;
		this->num_columns = 1; // for non-trains this gets set in FinishInitNested()
		this->unitnumber_digits = 2;

		this->CreateNestedTree();
		this->hscroll = (this->type == VEH_TRAIN ? this->GetScrollbar(WID_D_H_SCROLL) : nullptr);
		this->vscroll = this->GetScrollbar(WID_D_V_SCROLL);
		/* Don't show 'rename button' of aircraft hangar */
		this->GetWidget<NWidgetStacked>(WID_D_SHOW_RENAME)->SetDisplayedPlane(type == VEH_AIRCRAFT ? SZSP_NONE : 0);
		/* Only train depots have a horizontal scrollbar and a 'sell chain' button */
		if (type == VEH_TRAIN) this->GetWidget<NWidgetCore>(WID_D_MATRIX)->widget_data = 1 << MAT_COL_START;
		this->GetWidget<NWidgetStacked>(WID_D_SHOW_H_SCROLL)->SetDisplayedPlane(type == VEH_TRAIN ? 0 : SZSP_HORIZONTAL);
		this->GetWidget<NWidgetStacked>(WID_D_SHOW_SELL_CHAIN)->SetDisplayedPlane(type == VEH_TRAIN ? 0 : SZSP_NONE);
		this->SetupWidgetData(type);
		this->FinishInitNested(tile);

		this->owner = GetTileOwner(tile);
		OrderBackup::Reset();
	}

	~DepotWindow()
	{
		DeleteWindowById(WC_BUILD_VEHICLE, this->window_number);
		OrderBackup::Reset(this->window_number);
	}

	/**
	 * Draw a vehicle in the depot window in the box with the top left corner at x,y.
	 * @param v     Vehicle to draw.
	 * @param left  Left side of the box to draw in.
	 * @param right Right side of the box to draw in.
	 * @param y     Top of the box to draw in.
	 */
	void DrawVehicleInDepot(const Vehicle *v, int left, int right, int y) const
	{
		bool free_wagon = false;
		int sprite_y = y + (this->resize.step_height - ScaleGUITrad(GetVehicleHeight(v->type))) / 2;

		bool rtl = _current_text_dir == TD_RTL;
		int image_left  = rtl ? left  + this->count_width  : left  + this->header_width;
		int image_right = rtl ? right - this->header_width : right - this->count_width;

		switch (v->type) {
			case VEH_TRAIN: {
				const Train *u = Train::From(v);
				free_wagon = u->IsFreeWagon();

				uint x_space = free_wagon ?
						ScaleGUITrad(_consistent_train_width != 0 ? _consistent_train_width : TRAININFO_DEFAULT_VEHICLE_WIDTH) :
						0;

				DrawTrainImage(u, image_left + (rtl ? 0 : x_space), image_right - (rtl ? x_space : 0), sprite_y - 1,
						this->sel, EIT_IN_DEPOT, free_wagon ? 0 : this->hscroll->GetPosition(), this->vehicle_over);

				/* Length of consist in tiles with 1 fractional digit (rounded up) */
				SetDParam(0, CeilDiv(u->gcache.cached_total_length * 10, TILE_SIZE));
				SetDParam(1, 1);
				DrawString(rtl ? left + WD_FRAMERECT_LEFT : right - this->count_width, rtl ? left + this->count_width : right - WD_FRAMERECT_RIGHT, y + (this->resize.step_height - FONT_HEIGHT_SMALL) / 2, STR_TINY_BLACK_DECIMAL, TC_FROMSTRING, SA_RIGHT); // Draw the counter
				break;
			}

			case VEH_ROAD:     DrawRoadVehImage( v, image_left, image_right, sprite_y, this->sel, EIT_IN_DEPOT); break;
			case VEH_SHIP:     DrawShipImage(    v, image_left, image_right, sprite_y, this->sel, EIT_IN_DEPOT); break;
			case VEH_AIRCRAFT: DrawAircraftImage(v, image_left, image_right, sprite_y, this->sel, EIT_IN_DEPOT); break;
			default: NOT_REACHED();
		}

		uint diff_x, diff_y;
		if (v->IsGroundVehicle()) {
			/* Arrange unitnumber and flag horizontally */
			diff_x = this->flag_width + WD_FRAMERECT_LEFT;
			diff_y = (this->resize.step_height - this->flag_height) / 2 - 2;
		} else {
			/* Arrange unitnumber and flag vertically */
			diff_x = WD_FRAMERECT_LEFT;
			diff_y = FONT_HEIGHT_NORMAL + WD_PAR_VSEP_NORMAL;
		}
		int text_left  = rtl ? right - this->header_width - 1 : left + diff_x;
		int text_right = rtl ? right - diff_x : left + this->header_width - 1;

		if (free_wagon) {
			DrawString(text_left, text_right, y + 2, STR_DEPOT_NO_ENGINE);
		} else {
			DrawSprite((v->vehstatus & VS_STOPPED) ? SPR_FLAG_VEH_STOPPED : SPR_FLAG_VEH_RUNNING, PAL_NONE, rtl ? right - this->flag_width : left + WD_FRAMERECT_LEFT, y + diff_y);

			SetDParam(0, v->unitnumber);
			DrawString(text_left, text_right, y + 2, (uint16)(v->max_age - DAYS_IN_LEAP_YEAR) >= v->age ? STR_BLACK_COMMA : STR_RED_COMMA);
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		if (widget != WID_D_MATRIX) return;

		bool rtl = _current_text_dir == TD_RTL;

		/* Set the row and number of boxes in each row based on the number of boxes drawn in the matrix */
		const NWidgetCore *wid = this->GetWidget<NWidgetCore>(WID_D_MATRIX);

		/* Draw vertical separators at whole tiles.
		 * This only works in two cases:
		 *  - All vehicles use VEHICLEINFO_FULL_VEHICLE_WIDTH as reference width.
		 *  - All vehicles are 8/8. This cannot be checked for NewGRF, so instead we check for "all vehicles are original vehicles".
		 */
		if (this->type == VEH_TRAIN && _consistent_train_width != 0) {
			int w = ScaleGUITrad(2 * _consistent_train_width);
			int col = _colour_gradient[wid->colour][4];
			int image_left  = rtl ? r.left  + this->count_width  : r.left  + this->header_width;
			int image_right = rtl ? r.right - this->header_width : r.right - this->count_width;
			int first_line = w + (-this->hscroll->GetPosition()) % w;
			if (rtl) {
				for (int x = image_right - first_line; x >= image_left; x -= w) {
					GfxDrawLine(x, r.top, x, r.bottom, col, 1, 3);
				}
			} else {
				for (int x = image_left + first_line; x <= image_right; x += w) {
					GfxDrawLine(x, r.top, x, r.bottom, col, 1, 3);
				}
			}
		}

		uint16 rows_in_display = wid->current_y / wid->resize_y;

		uint16 num = this->vscroll->GetPosition() * this->num_columns;
		int maxval = min((uint)this->vehicle_list.size(), num + (rows_in_display * this->num_columns));
		int y;
		for (y = r.top + 1; num < maxval; y += this->resize.step_height) { // Draw the rows
			for (byte i = 0; i < this->num_columns && num < maxval; i++, num++) {
				/* Draw all vehicles in the current row */
				const Vehicle *v = this->vehicle_list[num];
				if (this->num_columns == 1) {
					this->DrawVehicleInDepot(v, r.left, r.right, y);
				} else {
					int x = r.left + (rtl ? (this->num_columns - i - 1) : i) * this->resize.step_width;
					this->DrawVehicleInDepot(v, x, x + this->resize.step_width - 1, y);
				}
			}
		}

		maxval = min((uint)this->vehicle_list.size() + (uint)this->wagon_list.size(), (this->vscroll->GetPosition() * this->num_columns) + (rows_in_display * this->num_columns));

		/* Draw the train wagons without an engine in front. */
		for (; num < maxval; num++, y += this->resize.step_height) {
			const Vehicle *v = this->wagon_list[num - this->vehicle_list.size()];
			this->DrawVehicleInDepot(v, r.left, r.right, y);
		}
	}

	void SetStringParameters(int widget) const override
	{
		if (widget != WID_D_CAPTION) return;

		/* locate the depot struct */
		TileIndex tile = this->window_number;
		SetDParam(0, this->type);
		SetDParam(1, (this->type == VEH_AIRCRAFT) ? GetStationIndex(tile) : GetDepotIndex(tile));
	}

	struct GetDepotVehiclePtData {
		const Vehicle *head;
		const Vehicle *wagon;
	};

	enum DepotGUIAction {
		MODE_ERROR,
		MODE_DRAG_VEHICLE,
		MODE_SHOW_VEHICLE,
		MODE_START_STOP,
	};

	DepotGUIAction GetVehicleFromDepotWndPt(int x, int y, const Vehicle **veh, GetDepotVehiclePtData *d) const
	{
		const NWidgetCore *matrix_widget = this->GetWidget<NWidgetCore>(WID_D_MATRIX);
		/* In case of RTL the widgets are swapped as a whole */
		if (_current_text_dir == TD_RTL) x = matrix_widget->current_x - x;

		uint xt = 0, xm = 0, ym = 0;
		if (this->type == VEH_TRAIN) {
			xm = x;
		} else {
			xt = x / this->resize.step_width;
			xm = x % this->resize.step_width;
			if (xt >= this->num_columns) return MODE_ERROR;
		}
		ym = y % this->resize.step_height;

		uint row = y / this->resize.step_height;
		if (row >= this->vscroll->GetCapacity()) return MODE_ERROR;

		uint pos = ((row + this->vscroll->GetPosition()) * this->num_columns) + xt;

		if (this->vehicle_list.size() + this->wagon_list.size() <= pos) {
			/* Clicking on 'line' / 'block' without a vehicle */
			if (this->type == VEH_TRAIN) {
				/* End the dragging */
				d->head  = nullptr;
				d->wagon = nullptr;
				return MODE_DRAG_VEHICLE;
			} else {
				return MODE_ERROR; // empty block, so no vehicle is selected
			}
		}

		bool wagon = false;
		if (this->vehicle_list.size() > pos) {
			*veh = this->vehicle_list[pos];
			/* Skip vehicles that are scrolled off the list */
			if (this->type == VEH_TRAIN) x += this->hscroll->GetPosition();
		} else {
			pos -= (uint)this->vehicle_list.size();
			*veh = this->wagon_list[pos];
			/* free wagons don't have an initial loco. */
			x -= ScaleGUITrad(VEHICLEINFO_FULL_VEHICLE_WIDTH);
			wagon = true;
		}

		const Train *v = nullptr;
		if (this->type == VEH_TRAIN) {
			v = Train::From(*veh);
			d->head = d->wagon = v;
		}

		if (xm <= this->header_width) {
			switch (this->type) {
				case VEH_TRAIN:
					if (wagon) return MODE_ERROR;
					FALLTHROUGH;

				case VEH_ROAD:
					if (xm <= this->flag_width) return MODE_START_STOP;
					break;

				case VEH_SHIP:
				case VEH_AIRCRAFT:
					if (xm <= this->flag_width && ym >= (uint)(FONT_HEIGHT_NORMAL + WD_PAR_VSEP_NORMAL)) return MODE_START_STOP;
					break;

				default: NOT_REACHED();
			}
			return MODE_SHOW_VEHICLE;
		}

		if (this->type != VEH_TRAIN) return MODE_DRAG_VEHICLE;

		/* Clicking on the counter */
		if (xm >= matrix_widget->current_x - this->count_width) return wagon ? MODE_ERROR : MODE_SHOW_VEHICLE;

		/* Account for the header */
		x -= this->header_width;

		/* find the vehicle in this row that was clicked */
		for (; v != nullptr; v = v->Next()) {
			x -= v->GetDisplayImageWidth();
			if (x < 0) break;
		}

		d->wagon = (v != nullptr ? v->GetFirstEnginePart() : nullptr);

		return MODE_DRAG_VEHICLE;
	}

	/**
	 * Handle click in the depot matrix.
	 * @param x Horizontal position in the matrix widget in pixels.
	 * @param y Vertical position in the matrix widget in pixels.
	 */
	void DepotClick(int x, int y)
	{
		GetDepotVehiclePtData gdvp = { nullptr, nullptr };
		const Vehicle *v = nullptr;
		DepotGUIAction mode = this->GetVehicleFromDepotWndPt(x, y, &v, &gdvp);

		if (this->type == VEH_TRAIN) v = gdvp.wagon;

		switch (mode) {
			case MODE_ERROR: // invalid
				return;

			case MODE_DRAG_VEHICLE: { // start dragging of vehicle
				if (v != nullptr && VehicleClicked(v)) return;

				VehicleID sel = this->sel;

				if (this->type == VEH_TRAIN && sel != INVALID_VEHICLE) {
					this->sel = INVALID_VEHICLE;
					TrainDepotMoveVehicle(v, sel, gdvp.head);
				} else if (v != nullptr) {
					SetObjectToPlaceWnd(SPR_CURSOR_MOUSE, PAL_NONE, HT_DRAG, this);
					SetMouseCursorVehicle(v, EIT_IN_DEPOT);
					_cursor.vehchain = _ctrl_pressed;

					this->sel = v->index;
					this->SetDirty();
				}
				break;
			}

			case MODE_SHOW_VEHICLE: // show info window
				ShowVehicleViewWindow(v);
				break;

			case MODE_START_STOP: // click start/stop flag
				StartStopVehicle(v, false);
				break;

			default: NOT_REACHED();
		}
	}

	/**
	 * Function to set up vehicle specific widgets (mainly sprites and strings).
	 * Only use this function to if the widget is used for several vehicle types and each has
	 * different text/sprites. If the widget is only used for a single vehicle type, or the same
	 * text/sprites are used every time, use the nested widget array to initialize the widget.
	 */
	void SetupWidgetData(VehicleType type)
	{
		this->GetWidget<NWidgetCore>(WID_D_STOP_ALL)->tool_tip     = STR_DEPOT_MASS_STOP_DEPOT_TRAIN_TOOLTIP + type;
		this->GetWidget<NWidgetCore>(WID_D_START_ALL)->tool_tip    = STR_DEPOT_MASS_START_DEPOT_TRAIN_TOOLTIP + type;
		this->GetWidget<NWidgetCore>(WID_D_SELL)->tool_tip         = STR_DEPOT_TRAIN_SELL_TOOLTIP + type;
		this->GetWidget<NWidgetCore>(WID_D_SELL_ALL)->tool_tip     = STR_DEPOT_SELL_ALL_BUTTON_TRAIN_TOOLTIP + type;

		this->GetWidget<NWidgetCore>(WID_D_BUILD)->SetDataTip(STR_DEPOT_TRAIN_NEW_VEHICLES_BUTTON + type, STR_DEPOT_TRAIN_NEW_VEHICLES_TOOLTIP + type);
		this->GetWidget<NWidgetCore>(WID_D_CLONE)->SetDataTip(STR_DEPOT_CLONE_TRAIN + type, STR_DEPOT_CLONE_TRAIN_DEPOT_INFO + type);

		this->GetWidget<NWidgetCore>(WID_D_LOCATION)->tool_tip     = STR_DEPOT_TRAIN_LOCATION_TOOLTIP + type;
		this->GetWidget<NWidgetCore>(WID_D_VEHICLE_LIST)->tool_tip = STR_DEPOT_VEHICLE_ORDER_LIST_TRAIN_TOOLTIP + type;
		this->GetWidget<NWidgetCore>(WID_D_AUTOREPLACE)->tool_tip  = STR_DEPOT_AUTOREPLACE_TRAIN_TOOLTIP + type;
		this->GetWidget<NWidgetCore>(WID_D_MATRIX)->tool_tip       = STR_DEPOT_TRAIN_LIST_TOOLTIP + this->type;

		switch (type) {
			default: NOT_REACHED();

			case VEH_TRAIN:
				this->GetWidget<NWidgetCore>(WID_D_VEHICLE_LIST)->widget_data = STR_TRAIN;

				/* Sprites */
				this->GetWidget<NWidgetCore>(WID_D_SELL)->widget_data        = SPR_SELL_TRAIN;
				this->GetWidget<NWidgetCore>(WID_D_SELL_ALL)->widget_data    = SPR_SELL_ALL_TRAIN;
				this->GetWidget<NWidgetCore>(WID_D_AUTOREPLACE)->widget_data = SPR_REPLACE_TRAIN;
				break;

			case VEH_ROAD:
				this->GetWidget<NWidgetCore>(WID_D_VEHICLE_LIST)->widget_data = STR_LORRY;

				/* Sprites */
				this->GetWidget<NWidgetCore>(WID_D_SELL)->widget_data        = SPR_SELL_ROADVEH;
				this->GetWidget<NWidgetCore>(WID_D_SELL_ALL)->widget_data    = SPR_SELL_ALL_ROADVEH;
				this->GetWidget<NWidgetCore>(WID_D_AUTOREPLACE)->widget_data = SPR_REPLACE_ROADVEH;
				break;

			case VEH_SHIP:
				this->GetWidget<NWidgetCore>(WID_D_VEHICLE_LIST)->widget_data = STR_SHIP;

				/* Sprites */
				this->GetWidget<NWidgetCore>(WID_D_SELL)->widget_data        = SPR_SELL_SHIP;
				this->GetWidget<NWidgetCore>(WID_D_SELL_ALL)->widget_data    = SPR_SELL_ALL_SHIP;
				this->GetWidget<NWidgetCore>(WID_D_AUTOREPLACE)->widget_data = SPR_REPLACE_SHIP;
				break;

			case VEH_AIRCRAFT:
				this->GetWidget<NWidgetCore>(WID_D_VEHICLE_LIST)->widget_data = STR_PLANE;

				/* Sprites */
				this->GetWidget<NWidgetCore>(WID_D_SELL)->widget_data        = SPR_SELL_AIRCRAFT;
				this->GetWidget<NWidgetCore>(WID_D_SELL_ALL)->widget_data    = SPR_SELL_ALL_AIRCRAFT;
				this->GetWidget<NWidgetCore>(WID_D_AUTOREPLACE)->widget_data = SPR_REPLACE_AIRCRAFT;
				break;
		}
	}

	uint count_width;
	uint header_width;
	uint flag_width;
	uint flag_height;

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_D_MATRIX: {
				uint min_height = 0;

				if (this->type == VEH_TRAIN) {
					SetDParamMaxValue(0, 1000, 0, FS_SMALL);
					SetDParam(1, 1);
					this->count_width = GetStringBoundingBox(STR_TINY_BLACK_DECIMAL).width + WD_FRAMERECT_LEFT + WD_FRAMERECT_RIGHT;
				} else {
					this->count_width = 0;
				}

				SetDParamMaxDigits(0, this->unitnumber_digits);
				Dimension unumber = GetStringBoundingBox(STR_BLACK_COMMA);
				const Sprite *spr = GetSprite(SPR_FLAG_VEH_STOPPED, ST_NORMAL);
				this->flag_width  = UnScaleGUI(spr->width) + WD_FRAMERECT_RIGHT;
				this->flag_height = UnScaleGUI(spr->height);

				if (this->type == VEH_TRAIN || this->type == VEH_ROAD) {
					min_height = max<uint>(unumber.height + WD_MATRIX_TOP, UnScaleGUI(spr->height));
					this->header_width = unumber.width + this->flag_width + WD_FRAMERECT_LEFT;
				} else {
					min_height = unumber.height + UnScaleGUI(spr->height) + WD_MATRIX_TOP + WD_PAR_VSEP_NORMAL + WD_MATRIX_BOTTOM;
					this->header_width = max<uint>(unumber.width, this->flag_width) + WD_FRAMERECT_RIGHT;
				}
				int base_width = this->count_width + this->header_width;

				resize->height = max<uint>(GetVehicleImageCellSize(this->type, EIT_IN_DEPOT).height, min_height);
				if (this->type == VEH_TRAIN) {
					resize->width = 1;
					size->width = base_width + 2 * ScaleGUITrad(29); // about 2 parts
					size->height = resize->height * 6;
				} else {
					resize->width = base_width + GetVehicleImageCellSize(this->type, EIT_IN_DEPOT).extend_left + GetVehicleImageCellSize(this->type, EIT_IN_DEPOT).extend_right;
					size->width = resize->width * (this->type == VEH_ROAD ? 5 : 3);
					size->height = resize->height * (this->type == VEH_ROAD ? 5 : 3);
				}
				fill->width = resize->width;
				fill->height = resize->height;
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
		this->generate_list = true;
	}

	void OnPaint() override
	{
		if (this->generate_list) {
			/* Generate the vehicle list
			 * It's ok to use the wagon pointers for non-trains as they will be ignored */
			BuildDepotVehicleList(this->type, this->window_number, &this->vehicle_list, &this->wagon_list);
			this->generate_list = false;
			DepotSortList(&this->vehicle_list);

			uint new_unitnumber_digits = GetUnitNumberDigits(this->vehicle_list);
			/* Only increase the size; do not decrease to prevent constant changes */
			if (this->unitnumber_digits < new_unitnumber_digits) {
				this->unitnumber_digits = new_unitnumber_digits;
				this->ReInit();
			}
		}

		/* determine amount of items for scroller */
		if (this->type == VEH_TRAIN) {
			uint max_width = ScaleGUITrad(VEHICLEINFO_FULL_VEHICLE_WIDTH);
			for (uint num = 0; num < this->vehicle_list.size(); num++) {
				uint width = 0;
				for (const Train *v = Train::From(this->vehicle_list[num]); v != nullptr; v = v->Next()) {
					width += v->GetDisplayImageWidth();
				}
				max_width = max(max_width, width);
			}
			/* Always have 1 empty row, so people can change the setting of the train */
			this->vscroll->SetCount((uint)this->vehicle_list.size() + (uint)this->wagon_list.size() + 1);
			/* Always make it longer than the longest train, so you can attach vehicles at the end, and also see the next vertical tile separator line */
			this->hscroll->SetCount(max_width + ScaleGUITrad(2 * VEHICLEINFO_FULL_VEHICLE_WIDTH + 1));
		} else {
			this->vscroll->SetCount(CeilDiv((uint)this->vehicle_list.size(), this->num_columns));
		}

		/* Setup disabled buttons. */
		TileIndex tile = this->window_number;
		this->SetWidgetsDisabledState(!IsTileOwner(tile, _local_company),
			WID_D_STOP_ALL,
			WID_D_START_ALL,
			WID_D_SELL,
			WID_D_SELL_CHAIN,
			WID_D_SELL_ALL,
			WID_D_BUILD,
			WID_D_CLONE,
			WID_D_RENAME,
			WID_D_AUTOREPLACE,
			WIDGET_LIST_END);

		this->DrawWidgets();
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_D_MATRIX: { // List
				NWidgetBase *nwi = this->GetWidget<NWidgetBase>(WID_D_MATRIX);
				this->DepotClick(pt.x - nwi->pos_x, pt.y - nwi->pos_y);
				break;
			}

			case WID_D_BUILD: // Build vehicle
				ResetObjectToPlace();
				ShowBuildVehicleWindow(this->window_number, this->type);
				break;

			case WID_D_CLONE: // Clone button
				this->SetWidgetDirty(WID_D_CLONE);
				this->ToggleWidgetLoweredState(WID_D_CLONE);

				if (this->IsWidgetLowered(WID_D_CLONE)) {
					static const CursorID clone_icons[] = {
						SPR_CURSOR_CLONE_TRAIN, SPR_CURSOR_CLONE_ROADVEH,
						SPR_CURSOR_CLONE_SHIP, SPR_CURSOR_CLONE_AIRPLANE
					};

					SetObjectToPlaceWnd(clone_icons[this->type], PAL_NONE, HT_VEHICLE, this);
				} else {
					ResetObjectToPlace();
				}
				break;

			case WID_D_LOCATION:
				if (_ctrl_pressed) {
					ShowExtraViewportWindow(this->window_number);
				} else {
					ScrollMainWindowToTile(this->window_number);
				}
				break;

			case WID_D_RENAME: // Rename button
				SetDParam(0, this->type);
				SetDParam(1, Depot::GetByTile((TileIndex)this->window_number)->index);
				ShowQueryString(STR_DEPOT_NAME, STR_DEPOT_RENAME_DEPOT_CAPTION, MAX_LENGTH_DEPOT_NAME_CHARS, this, CS_ALPHANUMERAL, QSF_ENABLE_DEFAULT | QSF_LEN_IN_CHARS);
				break;

			case WID_D_STOP_ALL:
			case WID_D_START_ALL: {
				VehicleListIdentifier vli(VL_DEPOT_LIST, this->type, this->owner);
				DoCommandP(this->window_number, (widget == WID_D_START_ALL ? (1 << 0) : 0), vli.Pack(), CMD_MASS_START_STOP);
				break;
			}

			case WID_D_SELL_ALL:
				/* Only open the confirmation window if there are anything to sell */
				if (this->vehicle_list.size() != 0 || this->wagon_list.size() != 0) {
					TileIndex tile = this->window_number;
					byte vehtype = this->type;

					SetDParam(0, vehtype);
					SetDParam(1, (vehtype == VEH_AIRCRAFT) ? GetStationIndex(tile) : GetDepotIndex(tile));
					ShowQuery(
						STR_DEPOT_CAPTION,
						STR_DEPOT_SELL_CONFIRMATION_TEXT,
						this,
						DepotSellAllConfirmationCallback
					);
				}
				break;

			case WID_D_VEHICLE_LIST:
				ShowVehicleListWindow(GetTileOwner(this->window_number), this->type, (TileIndex)this->window_number);
				break;

			case WID_D_AUTOREPLACE:
				DoCommandP(this->window_number, this->type, 0, CMD_DEPOT_MASS_AUTOREPLACE);
				break;

		}
	}

	void OnQueryTextFinished(char *str) override
	{
		if (str == nullptr) return;

		/* Do depot renaming */
		DoCommandP(0, GetDepotIndex(this->window_number), 0, CMD_RENAME_DEPOT | CMD_MSG(STR_ERROR_CAN_T_RENAME_DEPOT), nullptr, str);
	}

	bool OnRightClick(Point pt, int widget) override
	{
		if (widget != WID_D_MATRIX) return false;

		GetDepotVehiclePtData gdvp = { nullptr, nullptr };
		const Vehicle *v = nullptr;
		NWidgetBase *nwi = this->GetWidget<NWidgetBase>(WID_D_MATRIX);
		DepotGUIAction mode = this->GetVehicleFromDepotWndPt(pt.x - nwi->pos_x, pt.y - nwi->pos_y, &v, &gdvp);

		if (this->type == VEH_TRAIN) v = gdvp.wagon;

		if (v == nullptr || mode != MODE_DRAG_VEHICLE) return false;

		CargoArray capacity, loaded;

		/* Display info for single (articulated) vehicle, or for whole chain starting with selected vehicle */
		bool whole_chain = (this->type == VEH_TRAIN && _ctrl_pressed);

		/* loop through vehicle chain and collect cargoes */
		uint num = 0;
		for (const Vehicle *w = v; w != nullptr; w = w->Next()) {
			if (w->cargo_cap > 0 && w->cargo_type < NUM_CARGO) {
				capacity[w->cargo_type] += w->cargo_cap;
				loaded  [w->cargo_type] += w->cargo.StoredCount();
			}

			if (w->type == VEH_TRAIN && !w->HasArticulatedPart()) {
				num++;
				if (!whole_chain) break;
			}
		}

		/* Build tooltipstring */
		static char details[1024];
		details[0] = '\0';
		char *pos = details;

		for (CargoID cargo_type = 0; cargo_type < NUM_CARGO; cargo_type++) {
			if (capacity[cargo_type] == 0) continue;

			SetDParam(0, cargo_type);           // {CARGO} #1
			SetDParam(1, loaded[cargo_type]);   // {CARGO} #2
			SetDParam(2, cargo_type);           // {SHORTCARGO} #1
			SetDParam(3, capacity[cargo_type]); // {SHORTCARGO} #2
			pos = GetString(pos, STR_DEPOT_VEHICLE_TOOLTIP_CARGO, lastof(details));
		}

		/* Show tooltip window */
		uint64 args[2];
		args[0] = (whole_chain ? num : v->engine_type);
		args[1] = (uint64)(size_t)details;
		GuiShowTooltips(this, whole_chain ? STR_DEPOT_VEHICLE_TOOLTIP_CHAIN : STR_DEPOT_VEHICLE_TOOLTIP, 2, args, TCC_RIGHT_CLICK);

		return true;
	}

	/**
	 * Clones a vehicle
	 * @param v the original vehicle to clone
	 * @return Always true.
	 */
	bool OnVehicleSelect(const Vehicle *v) override
	{
		if (_ctrl_pressed) {
			/* Share-clone, do not open new viewport, and keep tool active */
			DoCommandP(this->window_number, v->index, 1, CMD_CLONE_VEHICLE | CMD_MSG(STR_ERROR_CAN_T_BUY_TRAIN + v->type), nullptr);
		} else {
			/* Copy-clone, open viewport for new vehicle, and deselect the tool (assume player wants to changs things on new vehicle) */
			if (DoCommandP(this->window_number, v->index, 0, CMD_CLONE_VEHICLE | CMD_MSG(STR_ERROR_CAN_T_BUY_TRAIN + v->type), CcCloneVehicle)) {
				ResetObjectToPlace();
			}
		}

		return true;
	}

	/**
	 * Clones a vehicle from a vehicle list.
	 * @return Returns true if clone/copy succeeded, false otherwise.
	 */
	bool OnVehicleSelect(VehicleList::const_iterator begin, VehicleList::const_iterator end) override
	{
		if (!_ctrl_pressed) {
			/* If CTRL is not pressed: If all the vehicles in this list have the same orders, then copy orders */
			if (AllEqual(begin, end, [](const Vehicle *v1, const Vehicle *v2) {
				return VehiclesHaveSameEngineList(v1, v2);
			})) {
				if (AllEqual(begin, end, [](const Vehicle *v1, const Vehicle *v2) {
					return VehiclesHaveSameOrderList(v1, v2);
				})) {
					OnVehicleSelect(*begin);
				}
				else {
					ShowErrorMessage(STR_ERROR_CAN_T_BUY_TRAIN + (*begin)->type, STR_ERROR_CAN_T_COPY_ORDER_VEHICLE_LIST, WL_INFO);
				}
			}
			else {
				ShowErrorMessage(STR_ERROR_CAN_T_BUY_TRAIN + (*begin)->type, STR_ERROR_CAN_T_CLONE_VEHICLE_LIST, WL_INFO);
			}
		}
		else {
			/* If CTRL is pressed: If all the vehicles in this list share orders, then copy orders */
			if (AllEqual(begin, end, [](const Vehicle *v1, const Vehicle *v2) {
				return VehiclesHaveSameEngineList(v1, v2);
			})) {
				if (AllEqual(begin, end, [](const Vehicle *v1, const Vehicle *v2) {
					return v1->FirstShared() == v2->FirstShared();
				})) {
					OnVehicleSelect(*begin);
				}
				else {
					ShowErrorMessage(STR_ERROR_CAN_T_BUY_TRAIN + (*begin)->type, STR_ERROR_CAN_T_SHARE_ORDER_VEHICLE_LIST, WL_INFO);
				}
			}
			else {
				ShowErrorMessage(STR_ERROR_CAN_T_BUY_TRAIN + (*begin)->type, STR_ERROR_CAN_T_CLONE_VEHICLE_LIST, WL_INFO);
			}
		}

		return true;
	}

	void OnPlaceObjectAbort() override
	{
		/* abort clone */
		this->RaiseWidget(WID_D_CLONE);
		this->SetWidgetDirty(WID_D_CLONE);

		/* abort drag & drop */
		this->sel = INVALID_VEHICLE;
		this->vehicle_over = INVALID_VEHICLE;
		this->SetWidgetDirty(WID_D_MATRIX);

		if (this->hovered_widget != -1) {
			this->SetWidgetLoweredState(this->hovered_widget, false);
			this->SetWidgetDirty(this->hovered_widget);
			this->hovered_widget = -1;
		}
	}

	void OnMouseDrag(Point pt, int widget) override
	{
		if (this->sel == INVALID_VEHICLE) return;
		if (widget != this->hovered_widget) {
			if (this->hovered_widget == WID_D_SELL || this->hovered_widget == WID_D_SELL_CHAIN) {
				this->SetWidgetLoweredState(this->hovered_widget, false);
				this->SetWidgetDirty(this->hovered_widget);
			}
			this->hovered_widget = widget;
			if (this->hovered_widget == WID_D_SELL || this->hovered_widget == WID_D_SELL_CHAIN) {
				this->SetWidgetLoweredState(this->hovered_widget, true);
				this->SetWidgetDirty(this->hovered_widget);
			}
		}
		if (this->type != VEH_TRAIN) return;

		/* A rail vehicle is dragged.. */
		if (widget != WID_D_MATRIX) { // ..outside of the depot matrix.
			if (this->vehicle_over != INVALID_VEHICLE) {
				this->vehicle_over = INVALID_VEHICLE;
				this->SetWidgetDirty(WID_D_MATRIX);
			}
			return;
		}

		NWidgetBase *matrix = this->GetWidget<NWidgetBase>(widget);
		const Vehicle *v = nullptr;
		GetDepotVehiclePtData gdvp = {nullptr, nullptr};

		if (this->GetVehicleFromDepotWndPt(pt.x - matrix->pos_x, pt.y - matrix->pos_y, &v, &gdvp) != MODE_DRAG_VEHICLE) return;

		VehicleID new_vehicle_over = INVALID_VEHICLE;
		if (gdvp.head != nullptr) {
			if (gdvp.wagon == nullptr && gdvp.head->Last()->index != this->sel) { // ..at the end of the train.
				/* NOTE: As a wagon can't be moved at the begin of a train, head index isn't used to mark a drag-and-drop
				 * destination inside a train. This head index is then used to indicate that a wagon is inserted at
				 * the end of the train.
				 */
				new_vehicle_over = gdvp.head->index;
			} else if (gdvp.wagon != nullptr && gdvp.head != gdvp.wagon &&
					gdvp.wagon->index != this->sel &&
					gdvp.wagon->Previous()->index != this->sel) { // ..over an existing wagon.
				new_vehicle_over = gdvp.wagon->index;
			}
		}

		if (this->vehicle_over == new_vehicle_over) return;

		this->vehicle_over = new_vehicle_over;
		this->SetWidgetDirty(widget);
	}

	void OnDragDrop(Point pt, int widget) override
	{
		switch (widget) {
			case WID_D_MATRIX: {
				const Vehicle *v = nullptr;
				VehicleID sel = this->sel;

				this->sel = INVALID_VEHICLE;
				this->SetDirty();

				NWidgetBase *nwi = this->GetWidget<NWidgetBase>(WID_D_MATRIX);
				if (this->type == VEH_TRAIN) {
					GetDepotVehiclePtData gdvp = { nullptr, nullptr };

					if (this->GetVehicleFromDepotWndPt(pt.x - nwi->pos_x, pt.y - nwi->pos_y, &v, &gdvp) == MODE_DRAG_VEHICLE && sel != INVALID_VEHICLE) {
						if (gdvp.wagon != nullptr && gdvp.wagon->index == sel && _ctrl_pressed) {
							DoCommandP(Vehicle::Get(sel)->tile, Vehicle::Get(sel)->index, true,
									CMD_REVERSE_TRAIN_DIRECTION | CMD_MSG(STR_ERROR_CAN_T_REVERSE_DIRECTION_RAIL_VEHICLE));
						} else if (gdvp.wagon == nullptr || gdvp.wagon->index != sel) {
							this->vehicle_over = INVALID_VEHICLE;
							TrainDepotMoveVehicle(gdvp.wagon, sel, gdvp.head);
						} else if (gdvp.head != nullptr && gdvp.head->IsFrontEngine()) {
							ShowVehicleViewWindow(gdvp.head);
						}
					}
				} else if (this->GetVehicleFromDepotWndPt(pt.x - nwi->pos_x, pt.y - nwi->pos_y, &v, nullptr) == MODE_DRAG_VEHICLE && v != nullptr && sel == v->index) {
					ShowVehicleViewWindow(v);
				}
				break;
			}

			case WID_D_SELL: case WID_D_SELL_CHAIN: {
				if (this->IsWidgetDisabled(widget)) return;
				if (this->sel == INVALID_VEHICLE) return;

				this->HandleButtonClick(widget);

				const Vehicle *v = Vehicle::Get(this->sel);
				this->sel = INVALID_VEHICLE;
				this->SetDirty();

				int sell_cmd = (v->type == VEH_TRAIN && (widget == WID_D_SELL_CHAIN || _ctrl_pressed)) ? 1 : 0;
				DoCommandP(v->tile, v->index | sell_cmd << 20 | MAKE_ORDER_BACKUP_FLAG, 0, GetCmdSellVeh(v->type));
				break;
			}

			default:
				this->sel = INVALID_VEHICLE;
				this->SetDirty();
				break;
		}
		this->hovered_widget = -1;
		_cursor.vehchain = false;
	}

	void OnTimeout() override
	{
		if (!this->IsWidgetDisabled(WID_D_SELL)) {
			this->RaiseWidget(WID_D_SELL);
			this->SetWidgetDirty(WID_D_SELL);
		}
		if (this->nested_array[WID_D_SELL] != nullptr && !this->IsWidgetDisabled(WID_D_SELL_CHAIN)) {
			this->RaiseWidget(WID_D_SELL_CHAIN);
			this->SetWidgetDirty(WID_D_SELL_CHAIN);
		}
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_D_MATRIX);
		NWidgetCore *nwi = this->GetWidget<NWidgetCore>(WID_D_MATRIX);
		if (this->type == VEH_TRAIN) {
			this->hscroll->SetCapacity(nwi->current_x - this->header_width - this->count_width);
		} else {
			this->num_columns = nwi->current_x / nwi->resize_x;
		}
	}

	EventState OnCTRLStateChange() override
	{
		if (this->sel != INVALID_VEHICLE) {
			_cursor.vehchain = _ctrl_pressed;
			this->SetWidgetDirty(WID_D_MATRIX);
			return ES_HANDLED;
		}

		return ES_NOT_HANDLED;
	}
};

static void DepotSellAllConfirmationCallback(Window *win, bool confirmed)
{
	if (confirmed) {
		DepotWindow *w = (DepotWindow*)win;
		TileIndex tile = w->window_number;
		byte vehtype = w->type;
		DoCommandP(tile, vehtype, 0, CMD_DEPOT_SELL_ALL_VEHICLES);
	}
}

/**
 * Opens a depot window
 * @param tile The tile where the depot/hangar is located
 * @param type The type of vehicles in the depot
 */
void ShowDepotWindow(TileIndex tile, VehicleType type)
{
	if (BringWindowToFrontById(WC_VEHICLE_DEPOT, tile) != nullptr) return;

	WindowDesc *desc;
	switch (type) {
		default: NOT_REACHED();
		case VEH_TRAIN:    desc = &_train_depot_desc;    break;
		case VEH_ROAD:     desc = &_road_depot_desc;     break;
		case VEH_SHIP:     desc = &_ship_depot_desc;     break;
		case VEH_AIRCRAFT: desc = &_aircraft_depot_desc; break;
	}

	new DepotWindow(desc, tile, type);
}

/**
 * Removes the highlight of a vehicle in a depot window
 * @param *v Vehicle to remove all highlights from
 */
void DeleteDepotHighlightOfVehicle(const Vehicle *v)
{
	DepotWindow *w;

	/* If we haven't got any vehicles on the mouse pointer, we haven't got any highlighted in any depots either
	 * If that is the case, we can skip looping though the windows and save time
	 */
	if (_special_mouse_mode != WSM_DRAGDROP) return;

	w = dynamic_cast<DepotWindow*>(FindWindowById(WC_VEHICLE_DEPOT, v->tile));
	if (w != nullptr) {
		if (w->sel == v->index) ResetObjectToPlace();
	}
}
