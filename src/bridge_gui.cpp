/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file bridge_gui.cpp Graphical user interface for bridge construction */

#include "stdafx.h"
#include "error.h"
#include "command_func.h"
#include "rail.h"
#include "road.h"
#include "strings_func.h"
#include "window_func.h"
#include "sound_func.h"
#include "gfx_func.h"
#include "tunnelbridge.h"
#include "sortlist_type.h"
#include "widgets/dropdown_func.h"
#include "core/geometry_func.hpp"
#include "cmd_helper.h"
#include "tunnelbridge_map.h"
#include "road_gui.h"
#include "newgrf_bridge.h"

#include "widgets/bridge_widget.h"

#include "table/strings.h"

#include "safeguards.h"

/** The type of the last built rail bridge */
static BridgeType _last_railbridge_type = 0;
/** The type of the last built road bridge */
static BridgeType _last_roadbridge_type = 0;

/**
 * Carriage for the data we need if we want to build a bridge
 */
struct BuildBridgeData {
	BridgeType index;
	const BridgeSpec *spec;
	Money cost;
};

typedef GUIList<BuildBridgeData> GUIBridgeList; ///< List of bridges, used in #BuildBridgeWindow.

/**
 * Callback executed after a build Bridge CMD has been called
 *
 * @param result Whether the build succeeded
 * @param end_tile End tile of the bridge.
 * @param p1 packed start tile coords (~ dx)
 * @param p2 various bitstuffed elements
 * - p2 = (bit  0-15) - bridge type (hi bh)
 * - p2 = (bit 16-21) - rail type or road types.
 * - p2 = (bit 23-24) - transport type.
 * @param cmd unused
 */
void CcBuildBridge(const CommandCost &result, TileIndex end_tile, uint32 p1, uint32 p2, uint32 cmd)
{
	if (result.Failed()) return;
	if (_settings_client.sound.confirm) SndPlayTileFx(SND_27_CONSTRUCTION_BRIDGE, end_tile);

	TransportType transport_type = Extract<TransportType, 23, 2>(p2);

	if (transport_type == TRANSPORT_ROAD) {
		DiagDirection end_direction = ReverseDiagDir(GetTunnelBridgeDirection(end_tile));
		ConnectRoadToStructure(end_tile, end_direction);

		DiagDirection start_direction = ReverseDiagDir(GetTunnelBridgeDirection(p1));
		ConnectRoadToStructure(p1, start_direction);
	}
}

/** Window class for handling the bridge-build GUI. */
class BuildBridgeWindow : public Window {
private:
	/* Runtime saved values */
	static Listing last_sorting; ///< Last setting of the sort.

	/* Constants for sorting the bridges */
	static const StringID sorter_names[];
	static GUIBridgeList::SortFunction * const sorter_funcs[];

	/* Internal variables */
	TileIndex start_tile;
	TileIndex end_tile;
	uint32 type;
	GUIBridgeList *bridges;
	int bridgetext_offset; ///< Horizontal offset of the text describing the bridge properties in #WID_BBS_BRIDGE_LIST relative to the left edge.
	Scrollbar *vscroll;

	/** Sort the bridges by their index */
	static bool BridgeIndexSorter(const BuildBridgeData &a, const BuildBridgeData &b)
	{
		return a.index < b.index;
	}

	/** Sort the bridges by their price */
	static bool BridgePriceSorter(const BuildBridgeData &a, const BuildBridgeData &b)
	{
		return a.cost < b.cost;
	}

	/** Sort the bridges by their maximum speed */
	static bool BridgeSpeedSorter(const BuildBridgeData &a, const BuildBridgeData &b)
	{
		return a.spec->speed < b.spec->speed;
	}

	void BuildBridge(uint8 i)
	{
		switch ((TransportType)(this->type >> 23)) {
			case TRANSPORT_RAIL: _last_railbridge_type = this->bridges->at(i).index; break;
			case TRANSPORT_ROAD: _last_roadbridge_type = this->bridges->at(i).index; break;
			default: break;
		}
		DoCommandP(this->end_tile, this->start_tile, this->type | this->bridges->at(i).index,
					CMD_BUILD_BRIDGE | CMD_MSG(STR_ERROR_CAN_T_BUILD_BRIDGE_HERE), CcBuildBridge);
	}

	/** Sort the builable bridges */
	void SortBridgeList()
	{
		this->bridges->Sort();

		/* Display the current sort variant */
		this->GetWidget<NWidgetCore>(WID_BBS_DROPDOWN_CRITERIA)->widget_data = this->sorter_names[this->bridges->SortType()];

		/* Set the modified widgets dirty */
		this->SetWidgetDirty(WID_BBS_DROPDOWN_CRITERIA);
		this->SetWidgetDirty(WID_BBS_BRIDGE_LIST);
	}

public:
	BuildBridgeWindow(WindowDesc *desc, TileIndex start, TileIndex end, uint32 br_type, GUIBridgeList *bl) : Window(desc),
		start_tile(start),
		end_tile(end),
		type(br_type),
		bridges(bl)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_BBS_SCROLLBAR);
		/* Change the data, or the caption of the gui. Set it to road or rail, accordingly. */
		this->GetWidget<NWidgetCore>(WID_BBS_CAPTION)->widget_data = (GB(this->type, 23, 2) == TRANSPORT_ROAD) ? STR_SELECT_ROAD_BRIDGE_CAPTION : STR_SELECT_RAIL_BRIDGE_CAPTION;
		this->FinishInitNested(GB(br_type, 23, 2)); // Initializes 'this->bridgetext_offset'.

		this->parent = FindWindowById(WC_BUILD_TOOLBAR, GB(this->type, 15, 2));
		this->bridges->SetListing(this->last_sorting);
		this->bridges->SetSortFuncs(this->sorter_funcs);
		this->bridges->NeedResort();
		this->SortBridgeList();

		this->vscroll->SetCount((uint)bl->size());
	}

	~BuildBridgeWindow()
	{
		this->last_sorting = this->bridges->GetListing();

		delete bridges;
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_BBS_DROPDOWN_ORDER: {
				Dimension d = GetStringBoundingBox(this->GetWidget<NWidgetCore>(widget)->widget_data);
				d.width += padding.width + Window::SortButtonWidth() * 2; // Doubled since the string is centred and it also looks better.
				d.height += padding.height;
				*size = maxdim(*size, d);
				break;
			}
			case WID_BBS_DROPDOWN_CRITERIA: {
				Dimension d = {0, 0};
				for (const StringID *str = this->sorter_names; *str != INVALID_STRING_ID; str++) {
					d = maxdim(d, GetStringBoundingBox(*str));
				}
				d.width += padding.width;
				d.height += padding.height;
				*size = maxdim(*size, d);
				break;
			}
			case WID_BBS_BRIDGE_LIST: {
				Dimension sprite_dim = {0, 0}; // Biggest bridge sprite dimension
				Dimension text_dim   = {0, 0}; // Biggest text dimension
				for (int i = 0; i < (int)this->bridges->size(); i++) {
					const BridgeSpec *b = this->bridges->at(i).spec;
					SpriteID sprite = b->sprite;
					if (b->use_custom_sprites) {
						sprite = GetCustomBridgeSprites(b, nullptr, INVALID_TILE, BSG_GUI);
					}

					sprite_dim = maxdim(sprite_dim, GetSpriteSize(sprite));

					SetDParam(2, this->bridges->at(i).cost);
					SetDParam(1, b->speed);
					SetDParam(0, b->material);
					text_dim = maxdim(text_dim, GetStringBoundingBox(_game_mode == GM_EDITOR ? STR_SELECT_BRIDGE_SCENEDIT_INFO : STR_SELECT_BRIDGE_INFO));
				}
				sprite_dim.height++; // Sprite is rendered one pixel down in the matrix field.
				text_dim.height++; // Allowing the bottom row pixels to be rendered on the edge of the matrix field.
				resize->height = std::max(sprite_dim.height, text_dim.height) + 2; // Max of both sizes + account for matrix edges.

				this->bridgetext_offset = WD_MATRIX_LEFT + sprite_dim.width + 1; // Left edge of text, 1 pixel distance from the sprite.
				size->width = this->bridgetext_offset + text_dim.width + WD_MATRIX_RIGHT;
				size->height = 4 * resize->height; // Smallest bridge gui is 4 entries high in the matrix.
				break;
			}
		}
	}

	Point OnInitialPosition(int16 sm_width, int16 sm_height, int window_number) override
	{
		/* Position the window so hopefully the first bridge from the list is under the mouse pointer. */
		NWidgetBase *list = this->GetWidget<NWidgetBase>(WID_BBS_BRIDGE_LIST);
		Point corner; // point of the top left corner of the window.
		corner.y = Clamp(_cursor.pos.y - list->pos_y - 5, GetMainViewTop(), GetMainViewBottom() - sm_height);
		corner.x = Clamp(_cursor.pos.x - list->pos_x - 5, 0, _screen.width - sm_width);
		return corner;
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_BBS_DROPDOWN_ORDER:
				this->DrawSortButtonState(widget, this->bridges->IsDescSortOrder() ? SBS_DOWN : SBS_UP);
				break;

			case WID_BBS_BRIDGE_LIST: {
				uint y = r.top;
				for (int i = this->vscroll->GetPosition(); this->vscroll->IsVisible(i) && i < (int)this->bridges->size(); i++) {
					const BridgeSpec *b = this->bridges->at(i).spec;

					SetDParam(2, this->bridges->at(i).cost);
					SetDParam(1, b->speed);
					SetDParam(0, b->material);

					SpriteID sprite = b->sprite;
					if (b->use_custom_sprites) {
						sprite = BridgeResolverObject(b, nullptr, INVALID_TILE, BSG_GUI).Resolve()->GetResult();
					}

					PaletteID pal = b->use_custom_sprites ? PAL_NONE : b->pal;

					DrawSprite(sprite, pal, r.left + WD_MATRIX_LEFT, y + this->resize.step_height - 1 - GetSpriteSize(b->sprite).height);
					DrawStringMultiLine(r.left + this->bridgetext_offset, r.right, y + 2, y + this->resize.step_height,
							_game_mode == GM_EDITOR ? STR_SELECT_BRIDGE_SCENEDIT_INFO : STR_SELECT_BRIDGE_INFO);
					y += this->resize.step_height;
				}
				break;
			}
		}
	}

	EventState OnKeyPress(WChar key, uint16 keycode) override
	{
		const uint8 i = keycode - '1';
		if (i < 9 && i < this->bridges->size()) {
			/* Build the requested bridge */
			this->BuildBridge(i);
			this->Close();
			return ES_HANDLED;
		}
		return ES_NOT_HANDLED;
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			default: break;
			case WID_BBS_BRIDGE_LIST: {
				uint i = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_BBS_BRIDGE_LIST);
				if (i < this->bridges->size()) {
					this->BuildBridge(i);
					this->Close();
				}
				break;
			}

			case WID_BBS_DROPDOWN_ORDER:
				this->bridges->ToggleSortOrder();
				this->SetDirty();
				break;

			case WID_BBS_DROPDOWN_CRITERIA:
				ShowDropDownMenu(this, this->sorter_names, this->bridges->SortType(), WID_BBS_DROPDOWN_CRITERIA, 0, 0);
				break;
		}
	}

	void OnDropdownSelect(int widget, int index) override
	{
		if (widget == WID_BBS_DROPDOWN_CRITERIA && this->bridges->SortType() != index) {
			this->bridges->SetSortType(index);

			this->SortBridgeList();
		}
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_BBS_BRIDGE_LIST);
	}
};

/** Set the default sorting for the bridges */
Listing BuildBridgeWindow::last_sorting = {true, 2};

/** Available bridge sorting functions. */
GUIBridgeList::SortFunction * const BuildBridgeWindow::sorter_funcs[] = {
	&BridgeIndexSorter,
	&BridgePriceSorter,
	&BridgeSpeedSorter
};

/** Names of the sorting functions. */
const StringID BuildBridgeWindow::sorter_names[] = {
	STR_SORT_BY_NUMBER,
	STR_SORT_BY_COST,
	STR_SORT_BY_MAX_SPEED,
	INVALID_STRING_ID
};

/** Widgets of the bridge gui. */
static const NWidgetPart _nested_build_bridge_widgets[] = {
	/* Header */
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN, WID_BBS_CAPTION), SetDataTip(STR_SELECT_RAIL_BRIDGE_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_DEFSIZEBOX, COLOUR_DARK_GREEN),
	EndContainer(),

	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_VERTICAL),
			/* Sort order + criteria buttons */
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_TEXTBTN, COLOUR_DARK_GREEN, WID_BBS_DROPDOWN_ORDER), SetFill(1, 0), SetDataTip(STR_BUTTON_SORT_BY, STR_TOOLTIP_SORT_ORDER),
				NWidget(WWT_DROPDOWN, COLOUR_DARK_GREEN, WID_BBS_DROPDOWN_CRITERIA), SetFill(1, 0), SetDataTip(0x0, STR_TOOLTIP_SORT_CRITERIA),
			EndContainer(),
			/* Matrix. */
			NWidget(WWT_MATRIX, COLOUR_DARK_GREEN, WID_BBS_BRIDGE_LIST), SetFill(1, 0), SetResize(0, 22), SetMatrixDataTip(1, 0, STR_SELECT_BRIDGE_SELECTION_TOOLTIP), SetScrollbar(WID_BBS_SCROLLBAR),
		EndContainer(),

		/* scrollbar + resize button */
		NWidget(NWID_VERTICAL),
			NWidget(NWID_VSCROLLBAR, COLOUR_DARK_GREEN, WID_BBS_SCROLLBAR),
			NWidget(WWT_RESIZEBOX, COLOUR_DARK_GREEN),
		EndContainer(),
	EndContainer(),
};

/** Window definition for the rail bridge selection window. */
static WindowDesc _build_bridge_desc(
	WDP_AUTO, "build_bridge", 200, 114,
	WC_BUILD_BRIDGE, WC_BUILD_TOOLBAR,
	WDF_CONSTRUCTION,
	_nested_build_bridge_widgets, lengthof(_nested_build_bridge_widgets)
);

/**
 * Prepare the data for the build a bridge window.
 *  If we can't build a bridge under the given conditions
 *  show an error message.
 *
 * @param start The start tile of the bridge
 * @param end The end tile of the bridge
 * @param transport_type The transport type
 * @param road_rail_type The road/rail type
 */
void ShowBuildBridgeWindow(TileIndex start, TileIndex end, TransportType transport_type, byte road_rail_type)
{
	CloseWindowByClass(WC_BUILD_BRIDGE);

	/* Data type for the bridge.
	 * Bit 24,23 = transport type,
	 *    22..16 = road/rail types,
	 *     15..0 = type of bridge */
	uint32 type = (transport_type << 23) | (road_rail_type << 16);

	/* The bridge length without ramps. */
	const uint bridge_len = GetTunnelBridgeLength(start, end);

	/* If Ctrl is being pressed, check whether the last bridge built is available
	 * If so, return this bridge type. Otherwise continue normally.
	 * We store bridge types for each transport type, so we have to check for
	 * the transport type beforehand.
	 */
	BridgeType last_bridge_type = 0;
	switch (transport_type) {
		case TRANSPORT_ROAD: last_bridge_type = _last_roadbridge_type; break;
		case TRANSPORT_RAIL: last_bridge_type = _last_railbridge_type; break;
		default: break; // water ways and air routes don't have bridge types
	}
	if (_ctrl_pressed && CheckBridgeAvailability(last_bridge_type, bridge_len).Succeeded()) {
		DoCommandP(end, start, type | last_bridge_type, CMD_BUILD_BRIDGE | CMD_MSG(STR_ERROR_CAN_T_BUILD_BRIDGE_HERE), CcBuildBridge);
		return;
	}

	/* only query bridge building possibility once, result is the same for all bridges!
	 * returns CMD_ERROR on failure, and price on success */
	StringID errmsg = INVALID_STRING_ID;
	CommandCost ret = DoCommand(end, start, type, CommandFlagsToDCFlags(GetCommandFlags(CMD_BUILD_BRIDGE)) | DC_QUERY_COST, CMD_BUILD_BRIDGE);

	GUIBridgeList *bl = nullptr;
	if (ret.Failed()) {
		errmsg = ret.GetErrorMessage();
	} else {
		/* check which bridges can be built */
		const uint tot_bridgedata_len = CalcBridgeLenCostFactor(bridge_len + 2);

		bl = new GUIBridgeList();

		Money infra_cost = 0;
		switch (transport_type) {
			case TRANSPORT_ROAD: {
				/* In case we add a new road type as well, we must be aware of those costs. */
				RoadType road_rt = INVALID_ROADTYPE;
				RoadType tram_rt = INVALID_ROADTYPE;
				if (IsBridgeTile(start)) {
					road_rt = GetRoadTypeRoad(start);
					tram_rt = GetRoadTypeTram(start);
				}
				if (RoadTypeIsRoad((RoadType)road_rail_type)) {
					road_rt = (RoadType)road_rail_type;
				} else {
					tram_rt = (RoadType)road_rail_type;
				}

				if (road_rt != INVALID_ROADTYPE) infra_cost += (bridge_len + 2) * 2 * RoadBuildCost(road_rt);
				if (tram_rt != INVALID_ROADTYPE) infra_cost += (bridge_len + 2) * 2 * RoadBuildCost(tram_rt);

				break;
			}
			case TRANSPORT_RAIL: infra_cost = (bridge_len + 2) * RailBuildCost((RailType)road_rail_type); break;
			default: break;
		}

		bool any_available = false;
		CommandCost type_check;
		/* loop for all bridgetypes */
		for (BridgeType brd_type = 0; brd_type != _bridge_mngr.GetMaxMapping(); brd_type++) {
			type_check = CheckBridgeAvailability(brd_type, bridge_len);
			if (type_check.Succeeded()) {
				/* bridge is accepted, add to list */
				BuildBridgeData &item = bl->emplace_back();
				item.index = brd_type;
				item.spec = BridgeSpec::Get(brd_type);
				/* Add to terraforming & bulldozing costs the cost of the
				 * bridge itself (not computed with DC_QUERY_COST) */
				item.cost = ret.GetCost() + (((int64)tot_bridgedata_len * _price[PR_BUILD_BRIDGE] * item.spec->price) >> 8) + infra_cost;
				any_available = true;
			}
		}
		/* give error cause if no bridges available here*/
		if (!any_available)
		{
			errmsg = type_check.GetErrorMessage();
		}
	}

	if (bl != nullptr && bl->size() != 0) {
		new BuildBridgeWindow(&_build_bridge_desc, start, end, type, bl);
	} else {
		delete bl;
		ShowErrorMessage(STR_ERROR_CAN_T_BUILD_BRIDGE_HERE, errmsg, WL_INFO, TileX(end) * TILE_SIZE, TileY(end) * TILE_SIZE);
	}
}
