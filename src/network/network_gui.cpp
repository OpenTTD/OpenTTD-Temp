/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file network_gui.cpp Implementation of the Network related GUIs. */

#include "../stdafx.h"
#include "../strings_func.h"
#include "../date_func.h"
#include "../fios.h"
#include "network_client.h"
#include "network_gui.h"
#include "network_gamelist.h"
#include "network.h"
#include "network_base.h"
#include "network_content.h"
#include "network_server.h"
#include "network_coordinator.h"
#include "../gui.h"
#include "network_udp.h"
#include "../window_func.h"
#include "../gfx_func.h"
#include "../widgets/dropdown_type.h"
#include "../widgets/dropdown_func.h"
#include "../querystring_gui.h"
#include "../sortlist_type.h"
#include "../company_func.h"
#include "../command_func.h"
#include "../core/geometry_func.hpp"
#include "../genworld.h"
#include "../map_type.h"
#include "../guitimer_func.h"
#include "../zoom_func.h"
#include "../sprite.h"
#include "../settings_internal.h"

#include "../widgets/network_widget.h"

#include "table/strings.h"
#include "../table/sprites.h"

#include "../stringfilter_type.h"

#ifdef __EMSCRIPTEN__
#	include <emscripten.h>
#endif

#include <map>

#include "../safeguards.h"

static void ShowNetworkStartServerWindow();
static void ShowNetworkLobbyWindow(NetworkGameList *ngl);

static const int NETWORK_LIST_REFRESH_DELAY = 30; ///< Time, in seconds, between updates of the network list.

static ClientID _admin_client_id = INVALID_CLIENT_ID; ///< For what client a confirmation window is open.
static CompanyID _admin_company_id = INVALID_COMPANY; ///< For what company a confirmation window is open.

/**
 * Update the network new window because a new server is
 * found on the network.
 */
void UpdateNetworkGameWindow()
{
	InvalidateWindowData(WC_NETWORK_WINDOW, WN_NETWORK_WINDOW_GAME, 0);
}

static DropDownList BuildVisibilityDropDownList()
{
	DropDownList list;

	list.emplace_back(new DropDownListStringItem(STR_NETWORK_SERVER_VISIBILITY_LOCAL, SERVER_GAME_TYPE_LOCAL, false));
	list.emplace_back(new DropDownListStringItem(STR_NETWORK_SERVER_VISIBILITY_INVITE_ONLY, SERVER_GAME_TYPE_INVITE_ONLY, false));
	list.emplace_back(new DropDownListStringItem(STR_NETWORK_SERVER_VISIBILITY_PUBLIC, SERVER_GAME_TYPE_PUBLIC, false));

	return list;
}

typedef GUIList<NetworkGameList*, StringFilter&> GUIGameServerList;
typedef int ServerListPosition;
static const ServerListPosition SLP_INVALID = -1;

/** Full blown container to make it behave exactly as we want :) */
class NWidgetServerListHeader : public NWidgetContainer {
	static const uint MINIMUM_NAME_WIDTH_BEFORE_NEW_HEADER = 150; ///< Minimum width before adding a new header
	bool visible[6]; ///< The visible headers
public:
	NWidgetServerListHeader() : NWidgetContainer(NWID_HORIZONTAL)
	{
		NWidgetLeaf *leaf = new NWidgetLeaf(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NG_NAME, STR_NETWORK_SERVER_LIST_GAME_NAME, STR_NETWORK_SERVER_LIST_GAME_NAME_TOOLTIP);
		leaf->SetResize(1, 0);
		leaf->SetFill(1, 0);
		this->Add(leaf);

		this->Add(new NWidgetLeaf(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NG_CLIENTS, STR_NETWORK_SERVER_LIST_CLIENTS_CAPTION, STR_NETWORK_SERVER_LIST_CLIENTS_CAPTION_TOOLTIP));
		this->Add(new NWidgetLeaf(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NG_MAPSIZE, STR_NETWORK_SERVER_LIST_MAP_SIZE_CAPTION, STR_NETWORK_SERVER_LIST_MAP_SIZE_CAPTION_TOOLTIP));
		this->Add(new NWidgetLeaf(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NG_DATE, STR_NETWORK_SERVER_LIST_DATE_CAPTION, STR_NETWORK_SERVER_LIST_DATE_CAPTION_TOOLTIP));
		this->Add(new NWidgetLeaf(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NG_YEARS, STR_NETWORK_SERVER_LIST_YEARS_CAPTION, STR_NETWORK_SERVER_LIST_YEARS_CAPTION_TOOLTIP));

		leaf = new NWidgetLeaf(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NG_INFO, STR_EMPTY, STR_NETWORK_SERVER_LIST_INFO_ICONS_TOOLTIP);
		leaf->SetMinimalSize(14 + GetSpriteSize(SPR_LOCK, nullptr, ZOOM_LVL_OUT_4X).width
		                        + GetSpriteSize(SPR_BLOT, nullptr, ZOOM_LVL_OUT_4X).width
		                        + GetSpriteSize(SPR_FLAGS_BASE, nullptr, ZOOM_LVL_OUT_4X).width, 12);
		leaf->SetFill(0, 1);
		this->Add(leaf);

		/* First and last are always visible, the rest is implicitly zeroed */
		this->visible[0] = true;
		*lastof(this->visible) = true;
	}

	void SetupSmallestSize(Window *w, bool init_array) override
	{
		/* Oh yeah, we ought to be findable! */
		w->nested_array[WID_NG_HEADER] = this;

		this->smallest_y = 0; // Biggest child.
		this->fill_x = 1;
		this->fill_y = 0;
		this->resize_x = 1; // We only resize in this direction
		this->resize_y = 0; // We never resize in this direction

		/* First initialise some variables... */
		for (NWidgetBase *child_wid = this->head; child_wid != nullptr; child_wid = child_wid->next) {
			child_wid->SetupSmallestSize(w, init_array);
			this->smallest_y = std::max(this->smallest_y, child_wid->smallest_y + child_wid->padding_top + child_wid->padding_bottom);
		}

		/* ... then in a second pass make sure the 'current' sizes are set. Won't change for most widgets. */
		for (NWidgetBase *child_wid = this->head; child_wid != nullptr; child_wid = child_wid->next) {
			child_wid->current_x = child_wid->smallest_x;
			child_wid->current_y = this->smallest_y;
		}

		this->smallest_x = this->head->smallest_x + this->tail->smallest_x; // First and last are always shown, rest not
	}

	void AssignSizePosition(SizingType sizing, uint x, uint y, uint given_width, uint given_height, bool rtl) override
	{
		assert(given_width >= this->smallest_x && given_height >= this->smallest_y);

		this->pos_x = x;
		this->pos_y = y;
		this->current_x = given_width;
		this->current_y = given_height;

		given_width -= this->tail->smallest_x;
		NWidgetBase *child_wid = this->head->next;
		/* The first and last widget are always visible, determine which other should be visible */
		for (uint i = 1; i < lengthof(this->visible) - 1; i++) {
			if (given_width > MINIMUM_NAME_WIDTH_BEFORE_NEW_HEADER + child_wid->smallest_x && this->visible[i - 1]) {
				this->visible[i] = true;
				given_width -= child_wid->smallest_x;
			} else {
				this->visible[i] = false;
			}
			child_wid = child_wid->next;
		}

		/* All remaining space goes to the first (name) widget */
		this->head->current_x = given_width;

		/* Now assign the widgets to their rightful place */
		uint position = 0; // Place to put next child relative to origin of the container.
		uint i = rtl ? lengthof(this->visible) - 1 : 0;
		child_wid = rtl ? this->tail : this->head;
		while (child_wid != nullptr) {
			if (this->visible[i]) {
				child_wid->AssignSizePosition(sizing, x + position, y, child_wid->current_x, this->current_y, rtl);
				position += child_wid->current_x;
			}

			child_wid = rtl ? child_wid->prev : child_wid->next;
			i += rtl ? -1 : 1;
		}
	}

	void Draw(const Window *w) override
	{
		int i = 0;
		for (NWidgetBase *child_wid = this->head; child_wid != nullptr; child_wid = child_wid->next) {
			if (!this->visible[i++]) continue;

			child_wid->Draw(w);
		}
	}

	NWidgetCore *GetWidgetFromPos(int x, int y) override
	{
		if (!IsInsideBS(x, this->pos_x, this->current_x) || !IsInsideBS(y, this->pos_y, this->current_y)) return nullptr;

		int i = 0;
		for (NWidgetBase *child_wid = this->head; child_wid != nullptr; child_wid = child_wid->next) {
			if (!this->visible[i++]) continue;
			NWidgetCore *nwid = child_wid->GetWidgetFromPos(x, y);
			if (nwid != nullptr) return nwid;
		}
		return nullptr;
	}

	/**
	 * Checks whether the given widget is actually visible.
	 * @param widget the widget to check for visibility
	 * @return true iff the widget is visible.
	 */
	bool IsWidgetVisible(NetworkGameWidgets widget) const
	{
		assert((uint)(widget - WID_NG_NAME) < lengthof(this->visible));
		return this->visible[widget - WID_NG_NAME];
	}
};

class NetworkGameWindow : public Window {
protected:
	/* Runtime saved values */
	static Listing last_sorting;

	/* Constants for sorting servers */
	static GUIGameServerList::SortFunction * const sorter_funcs[];
	static GUIGameServerList::FilterFunction * const filter_funcs[];

	NetworkGameList *server;      ///< selected server
	NetworkGameList *last_joined; ///< the last joined server
	GUIGameServerList servers;    ///< list with game servers.
	ServerListPosition list_pos;  ///< position of the selected server
	Scrollbar *vscroll;           ///< vertical scrollbar of the list of servers
	QueryString name_editbox;     ///< Client name editbox.
	QueryString filter_editbox;   ///< Editbox for filter on servers
	GUITimer requery_timer;       ///< Timer for network requery
	bool searched_internet;       ///< Did we ever press "Search Internet" button?

	int lock_offset; ///< Left offset for lock icon.
	int blot_offset; ///< Left offset for green/yellow/red compatibility icon.
	int flag_offset; ///< Left offset for language flag icon.

	/**
	 * (Re)build the GUI network game list (a.k.a. this->servers) as some
	 * major change has occurred. It ensures appropriate filtering and
	 * sorting, if both or either one is enabled.
	 */
	void BuildGUINetworkGameList()
	{
		if (!this->servers.NeedRebuild()) return;

		/* Create temporary array of games to use for listing */
		this->servers.clear();

		bool found_current_server = false;
		for (NetworkGameList *ngl = _network_game_list; ngl != nullptr; ngl = ngl->next) {
			this->servers.push_back(ngl);
			if (ngl == this->server) {
				found_current_server = true;
			}
		}
		/* A refresh can cause the current server to be delete; so unselect. */
		if (!found_current_server) {
			if (this->server == this->last_joined) this->last_joined = nullptr;
			this->server = nullptr;
			this->list_pos = SLP_INVALID;
		}

		/* Apply the filter condition immediately, if a search string has been provided. */
		StringFilter sf;
		sf.SetFilterTerm(this->filter_editbox.text.buf);

		if (!sf.IsEmpty()) {
			this->servers.SetFilterState(true);
			this->servers.Filter(sf);
		} else {
			this->servers.SetFilterState(false);
		}

		this->servers.shrink_to_fit();
		this->servers.RebuildDone();
		this->vscroll->SetCount((int)this->servers.size());

		/* Sort the list of network games as requested. */
		this->servers.Sort();
		this->UpdateListPos();
	}

	/** Sort servers by name. */
	static bool NGameNameSorter(NetworkGameList * const &a, NetworkGameList * const &b)
	{
		int r = strnatcmp(a->info.server_name.c_str(), b->info.server_name.c_str(), true); // Sort by name (natural sorting).
		if (r == 0) r = a->connection_string.compare(b->connection_string);

		return r < 0;
	}

	/**
	 * Sort servers by the amount of clients online on a
	 * server. If the two servers have the same amount, the one with the
	 * higher maximum is preferred.
	 */
	static bool NGameClientSorter(NetworkGameList * const &a, NetworkGameList * const &b)
	{
		/* Reverse as per default we are interested in most-clients first */
		int r = a->info.clients_on - b->info.clients_on;

		if (r == 0) r = a->info.clients_max - b->info.clients_max;
		if (r == 0) return NGameNameSorter(a, b);

		return r < 0;
	}

	/** Sort servers by map size */
	static bool NGameMapSizeSorter(NetworkGameList * const &a, NetworkGameList * const &b)
	{
		/* Sort by the area of the map. */
		int r = (a->info.map_height) * (a->info.map_width) - (b->info.map_height) * (b->info.map_width);

		if (r == 0) r = a->info.map_width - b->info.map_width;
		return (r != 0) ? r < 0 : NGameClientSorter(a, b);
	}

	/** Sort servers by current date */
	static bool NGameDateSorter(NetworkGameList * const &a, NetworkGameList * const &b)
	{
		int r = a->info.game_date - b->info.game_date;
		return (r != 0) ? r < 0 : NGameClientSorter(a, b);
	}

	/** Sort servers by the number of days the game is running */
	static bool NGameYearsSorter(NetworkGameList * const &a, NetworkGameList * const &b)
	{
		int r = a->info.game_date - a->info.start_date - b->info.game_date + b->info.start_date;
		return (r != 0) ? r < 0: NGameDateSorter(a, b);
	}

	/**
	 * Sort servers by joinability. If both servers are the
	 * same, prefer the non-passworded server first.
	 */
	static bool NGameAllowedSorter(NetworkGameList * const &a, NetworkGameList * const &b)
	{
		/* The servers we do not know anything about (the ones that did not reply) should be at the bottom) */
		int r = a->info.server_revision.empty() - b->info.server_revision.empty();

		/* Reverse default as we are interested in version-compatible clients first */
		if (r == 0) r = b->info.version_compatible - a->info.version_compatible;
		/* The version-compatible ones are then sorted with NewGRF compatible first, incompatible last */
		if (r == 0) r = b->info.compatible - a->info.compatible;
		/* Passworded servers should be below unpassworded servers */
		if (r == 0) r = a->info.use_password - b->info.use_password;

		/* Finally sort on the number of clients of the server in reverse order. */
		return (r != 0) ? r < 0 : NGameClientSorter(b, a);
	}

	/** Sort the server list */
	void SortNetworkGameList()
	{
		if (this->servers.Sort()) this->UpdateListPos();
	}

	/** Set this->list_pos to match this->server */
	void UpdateListPos()
	{
		this->list_pos = SLP_INVALID;
		for (uint i = 0; i != this->servers.size(); i++) {
			if (this->servers[i] == this->server) {
				this->list_pos = i;
				break;
			}
		}
	}

	static bool CDECL NGameSearchFilter(NetworkGameList * const *item, StringFilter &sf)
	{
		assert(item != nullptr);
		assert((*item) != nullptr);

		sf.ResetState();
		sf.AddLine((*item)->info.server_name.c_str());
		return sf.GetState();
	}

	/**
	 * Draw a single server line.
	 * @param cur_item  the server to draw.
	 * @param y         from where to draw?
	 * @param highlight does the line need to be highlighted?
	 */
	void DrawServerLine(const NetworkGameList *cur_item, uint y, bool highlight) const
	{
		const NWidgetBase *nwi_name = this->GetWidget<NWidgetBase>(WID_NG_NAME);
		const NWidgetBase *nwi_info = this->GetWidget<NWidgetBase>(WID_NG_INFO);

		/* show highlighted item with a different colour */
		if (highlight) GfxFillRect(nwi_name->pos_x + 1, y + 1, nwi_info->pos_x + nwi_info->current_x - 2, y + this->resize.step_height - 2, PC_GREY);

		/* offsets to vertically centre text and icons */
		int text_y_offset = (this->resize.step_height - FONT_HEIGHT_NORMAL) / 2 + 1;
		int icon_y_offset = (this->resize.step_height - GetSpriteSize(SPR_BLOT).height) / 2;
		int lock_y_offset = (this->resize.step_height - GetSpriteSize(SPR_LOCK).height) / 2;

		DrawString(nwi_name->pos_x + WD_FRAMERECT_LEFT, nwi_name->pos_x + nwi_name->current_x - WD_FRAMERECT_RIGHT, y + text_y_offset, cur_item->info.server_name, TC_BLACK);

		/* only draw details if the server is online */
		if (cur_item->online) {
			const NWidgetServerListHeader *nwi_header = this->GetWidget<NWidgetServerListHeader>(WID_NG_HEADER);

			if (nwi_header->IsWidgetVisible(WID_NG_CLIENTS)) {
				const NWidgetBase *nwi_clients = this->GetWidget<NWidgetBase>(WID_NG_CLIENTS);
				SetDParam(0, cur_item->info.clients_on);
				SetDParam(1, cur_item->info.clients_max);
				SetDParam(2, cur_item->info.companies_on);
				SetDParam(3, cur_item->info.companies_max);
				DrawString(nwi_clients->pos_x, nwi_clients->pos_x + nwi_clients->current_x - 1, y + text_y_offset, STR_NETWORK_SERVER_LIST_GENERAL_ONLINE, TC_FROMSTRING, SA_HOR_CENTER);
			}

			if (nwi_header->IsWidgetVisible(WID_NG_MAPSIZE)) {
				/* map size */
				const NWidgetBase *nwi_mapsize = this->GetWidget<NWidgetBase>(WID_NG_MAPSIZE);
				SetDParam(0, cur_item->info.map_width);
				SetDParam(1, cur_item->info.map_height);
				DrawString(nwi_mapsize->pos_x, nwi_mapsize->pos_x + nwi_mapsize->current_x - 1, y + text_y_offset, STR_NETWORK_SERVER_LIST_MAP_SIZE_SHORT, TC_FROMSTRING, SA_HOR_CENTER);
			}

			if (nwi_header->IsWidgetVisible(WID_NG_DATE)) {
				/* current date */
				const NWidgetBase *nwi_date = this->GetWidget<NWidgetBase>(WID_NG_DATE);
				YearMonthDay ymd;
				ConvertDateToYMD(cur_item->info.game_date, &ymd);
				SetDParam(0, ymd.year);
				DrawString(nwi_date->pos_x, nwi_date->pos_x + nwi_date->current_x - 1, y + text_y_offset, STR_JUST_INT, TC_BLACK, SA_HOR_CENTER);
			}

			if (nwi_header->IsWidgetVisible(WID_NG_YEARS)) {
				/* number of years the game is running */
				const NWidgetBase *nwi_years = this->GetWidget<NWidgetBase>(WID_NG_YEARS);
				YearMonthDay ymd_cur, ymd_start;
				ConvertDateToYMD(cur_item->info.game_date, &ymd_cur);
				ConvertDateToYMD(cur_item->info.start_date, &ymd_start);
				SetDParam(0, ymd_cur.year - ymd_start.year);
				DrawString(nwi_years->pos_x, nwi_years->pos_x + nwi_years->current_x - 1, y + text_y_offset, STR_JUST_INT, TC_BLACK, SA_HOR_CENTER);
			}

			/* draw a lock if the server is password protected */
			if (cur_item->info.use_password) DrawSprite(SPR_LOCK, PAL_NONE, nwi_info->pos_x + this->lock_offset, y + lock_y_offset);

			/* draw red or green icon, depending on compatibility with server */
			DrawSprite(SPR_BLOT, (cur_item->info.compatible ? PALETTE_TO_GREEN : (cur_item->info.version_compatible ? PALETTE_TO_YELLOW : PALETTE_TO_RED)), nwi_info->pos_x + this->blot_offset, y + icon_y_offset + 1);
		}
	}

	/**
	 * Scroll the list up or down to the currently selected server.
	 * If the server is below the currently displayed servers, it will
	 * scroll down an amount so that the server appears at the bottom.
	 * If the server is above the currently displayed servers, it will
	 * scroll up so that the server appears at the top.
	 */
	void ScrollToSelectedServer()
	{
		if (this->list_pos == SLP_INVALID) return; // no server selected
		this->vscroll->ScrollTowards(this->list_pos);
	}

public:
	NetworkGameWindow(WindowDesc *desc) : Window(desc), name_editbox(NETWORK_CLIENT_NAME_LENGTH), filter_editbox(120), searched_internet(false)
	{
		this->list_pos = SLP_INVALID;
		this->server = nullptr;

		this->lock_offset = 5;
		this->blot_offset = this->lock_offset + 3 + GetSpriteSize(SPR_LOCK).width;
		this->flag_offset = this->blot_offset + 2 + GetSpriteSize(SPR_BLOT).width;

		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_NG_SCROLLBAR);
		this->FinishInitNested(WN_NETWORK_WINDOW_GAME);

		this->querystrings[WID_NG_CLIENT] = &this->name_editbox;
		this->name_editbox.text.Assign(_settings_client.network.client_name.c_str());

		this->querystrings[WID_NG_FILTER] = &this->filter_editbox;
		this->filter_editbox.cancel_button = QueryString::ACTION_CLEAR;
		this->SetFocusedWidget(WID_NG_FILTER);

		/* As the master-server doesn't support "websocket" servers yet, we
		 * let "os/emscripten/pre.js" hardcode a list of servers people can
		 * join. This means the serverlist is curated for now, but it is the
		 * best we can offer. */
#ifdef __EMSCRIPTEN__
		EM_ASM(if (window["openttd_server_list"]) openttd_server_list());
#endif

		this->last_joined = NetworkAddServer(_settings_client.network.last_joined, false);
		this->server = this->last_joined;

		this->requery_timer.SetInterval(NETWORK_LIST_REFRESH_DELAY * 1000);

		this->servers.SetListing(this->last_sorting);
		this->servers.SetSortFuncs(this->sorter_funcs);
		this->servers.SetFilterFuncs(this->filter_funcs);
		this->servers.ForceRebuild();
	}

	~NetworkGameWindow()
	{
		this->last_sorting = this->servers.GetListing();
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_NG_MATRIX:
				resize->height = WD_MATRIX_TOP + std::max(GetSpriteSize(SPR_BLOT).height, (uint)FONT_HEIGHT_NORMAL) + WD_MATRIX_BOTTOM;
				fill->height = resize->height;
				size->height = 12 * resize->height;
				break;

			case WID_NG_LASTJOINED:
				size->height = WD_MATRIX_TOP + std::max(GetSpriteSize(SPR_BLOT).height, (uint)FONT_HEIGHT_NORMAL) + WD_MATRIX_BOTTOM;
				break;

			case WID_NG_LASTJOINED_SPACER:
				size->width = NWidgetScrollbar::GetVerticalDimension().width;
				break;

			case WID_NG_NAME:
				size->width += 2 * Window::SortButtonWidth(); // Make space for the arrow
				break;

			case WID_NG_CLIENTS:
				size->width += 2 * Window::SortButtonWidth(); // Make space for the arrow
				SetDParamMaxValue(0, MAX_CLIENTS);
				SetDParamMaxValue(1, MAX_CLIENTS);
				SetDParamMaxValue(2, MAX_COMPANIES);
				SetDParamMaxValue(3, MAX_COMPANIES);
				*size = maxdim(*size, GetStringBoundingBox(STR_NETWORK_SERVER_LIST_GENERAL_ONLINE));
				break;

			case WID_NG_MAPSIZE:
				size->width += 2 * Window::SortButtonWidth(); // Make space for the arrow
				SetDParamMaxValue(0, MAX_MAP_SIZE);
				SetDParamMaxValue(1, MAX_MAP_SIZE);
				*size = maxdim(*size, GetStringBoundingBox(STR_NETWORK_SERVER_LIST_MAP_SIZE_SHORT));
				break;

			case WID_NG_DATE:
			case WID_NG_YEARS:
				size->width += 2 * Window::SortButtonWidth(); // Make space for the arrow
				SetDParamMaxValue(0, 5);
				*size = maxdim(*size, GetStringBoundingBox(STR_JUST_INT));
				break;
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_NG_MATRIX: {
				uint16 y = r.top;

				const int max = std::min(this->vscroll->GetPosition() + this->vscroll->GetCapacity(), (int)this->servers.size());

				for (int i = this->vscroll->GetPosition(); i < max; ++i) {
					const NetworkGameList *ngl = this->servers[i];
					this->DrawServerLine(ngl, y, ngl == this->server);
					y += this->resize.step_height;
				}
				break;
			}

			case WID_NG_LASTJOINED:
				/* Draw the last joined server, if any */
				if (this->last_joined != nullptr) this->DrawServerLine(this->last_joined, r.top, this->last_joined == this->server);
				break;

			case WID_NG_DETAILS:
				this->DrawDetails(r);
				break;

			case WID_NG_NAME:
			case WID_NG_CLIENTS:
			case WID_NG_MAPSIZE:
			case WID_NG_DATE:
			case WID_NG_YEARS:
			case WID_NG_INFO:
				if (widget - WID_NG_NAME == this->servers.SortType()) this->DrawSortButtonState(widget, this->servers.IsDescSortOrder() ? SBS_DOWN : SBS_UP);
				break;
		}
	}


	void OnPaint() override
	{
		if (this->servers.NeedRebuild()) {
			this->BuildGUINetworkGameList();
		}
		if (this->servers.NeedResort()) {
			this->SortNetworkGameList();
		}

		NetworkGameList *sel = this->server;
		/* 'Refresh' button invisible if no server selected */
		this->SetWidgetDisabledState(WID_NG_REFRESH, sel == nullptr);
		/* 'Join' button disabling conditions */
		this->SetWidgetDisabledState(WID_NG_JOIN, sel == nullptr || // no Selected Server
				!sel->online || // Server offline
				sel->info.clients_on >= sel->info.clients_max || // Server full
				!sel->info.compatible); // Revision mismatch

		/* 'NewGRF Settings' button invisible if no NewGRF is used */
		this->GetWidget<NWidgetStacked>(WID_NG_NEWGRF_SEL)->SetDisplayedPlane(sel == nullptr || !sel->online || sel->info.grfconfig == nullptr);
		this->GetWidget<NWidgetStacked>(WID_NG_NEWGRF_MISSING_SEL)->SetDisplayedPlane(sel == nullptr || !sel->online || sel->info.grfconfig == nullptr || !sel->info.version_compatible || sel->info.compatible);

#ifdef __EMSCRIPTEN__
		this->SetWidgetDisabledState(WID_NG_SEARCH_INTERNET, true);
		this->SetWidgetDisabledState(WID_NG_SEARCH_LAN, true);
		this->SetWidgetDisabledState(WID_NG_ADD, true);
		this->SetWidgetDisabledState(WID_NG_START, true);
#endif

		this->DrawWidgets();
	}

	void DrawDetails(const Rect &r) const
	{
		NetworkGameList *sel = this->server;

		const int detail_height = 6 + 8 + 6 + 3 * FONT_HEIGHT_NORMAL;

		/* Draw the right menu */
		GfxFillRect(r.left + 1, r.top + 1, r.right - 1, r.top + detail_height - 1, PC_DARK_BLUE);
		if (sel == nullptr) {
			DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, r.top + 6 + 4 + FONT_HEIGHT_NORMAL, STR_NETWORK_SERVER_LIST_GAME_INFO, TC_FROMSTRING, SA_HOR_CENTER);
		} else if (!sel->online) {
			DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, r.top + 6 + 4 + FONT_HEIGHT_NORMAL, sel->info.server_name, TC_ORANGE, SA_HOR_CENTER); // game name

			DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, r.top + detail_height + 4, STR_NETWORK_SERVER_LIST_SERVER_OFFLINE, TC_FROMSTRING, SA_HOR_CENTER); // server offline
		} else { // show game info

			DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, r.top + 6, STR_NETWORK_SERVER_LIST_GAME_INFO, TC_FROMSTRING, SA_HOR_CENTER);
			DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, r.top + 6 + 4 + FONT_HEIGHT_NORMAL, sel->info.server_name, TC_ORANGE, SA_HOR_CENTER); // game name

			uint16 y = r.top + detail_height + 4;

			SetDParam(0, sel->info.clients_on);
			SetDParam(1, sel->info.clients_max);
			SetDParam(2, sel->info.companies_on);
			SetDParam(3, sel->info.companies_max);
			DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_NETWORK_SERVER_LIST_CLIENTS);
			y += FONT_HEIGHT_NORMAL;

			SetDParam(0, STR_CHEAT_SWITCH_CLIMATE_TEMPERATE_LANDSCAPE + sel->info.landscape);
			DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_NETWORK_SERVER_LIST_LANDSCAPE); // landscape
			y += FONT_HEIGHT_NORMAL;

			SetDParam(0, sel->info.map_width);
			SetDParam(1, sel->info.map_height);
			DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_NETWORK_SERVER_LIST_MAP_SIZE); // map size
			y += FONT_HEIGHT_NORMAL;

			SetDParamStr(0, sel->info.server_revision);
			DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_NETWORK_SERVER_LIST_SERVER_VERSION); // server version
			y += FONT_HEIGHT_NORMAL;

			SetDParamStr(0, sel->connection_string);
			DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_NETWORK_SERVER_LIST_SERVER_ADDRESS); // server address
			y += FONT_HEIGHT_NORMAL;

			SetDParam(0, sel->info.start_date);
			DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_NETWORK_SERVER_LIST_START_DATE); // start date
			y += FONT_HEIGHT_NORMAL;

			SetDParam(0, sel->info.game_date);
			DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_NETWORK_SERVER_LIST_CURRENT_DATE); // current date
			y += FONT_HEIGHT_NORMAL;

			if (sel->info.gamescript_version != -1) {
				SetDParamStr(0, sel->info.gamescript_name);
				SetDParam(1, sel->info.gamescript_version);
				DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_NETWORK_SERVER_LIST_GAMESCRIPT); // gamescript name and version
				y += FONT_HEIGHT_NORMAL;
			}

			y += WD_PAR_VSEP_NORMAL;

			if (!sel->info.compatible) {
				DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, sel->info.version_compatible ? STR_NETWORK_SERVER_LIST_GRF_MISMATCH : STR_NETWORK_SERVER_LIST_VERSION_MISMATCH, TC_FROMSTRING, SA_HOR_CENTER); // server mismatch
			} else if (sel->info.clients_on == sel->info.clients_max) {
				/* Show: server full, when clients_on == max_clients */
				DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_NETWORK_SERVER_LIST_SERVER_FULL, TC_FROMSTRING, SA_HOR_CENTER); // server full
			} else if (sel->info.use_password) {
				DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_NETWORK_SERVER_LIST_PASSWORD, TC_FROMSTRING, SA_HOR_CENTER); // password warning
			}
		}
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_NG_CANCEL: // Cancel button
				CloseWindowById(WC_NETWORK_WINDOW, WN_NETWORK_WINDOW_GAME);
				break;

			case WID_NG_NAME:    // Sort by name
			case WID_NG_CLIENTS: // Sort by connected clients
			case WID_NG_MAPSIZE: // Sort by map size
			case WID_NG_DATE:    // Sort by date
			case WID_NG_YEARS:   // Sort by years
			case WID_NG_INFO:    // Connectivity (green dot)
				if (this->servers.SortType() == widget - WID_NG_NAME) {
					this->servers.ToggleSortOrder();
					if (this->list_pos != SLP_INVALID) this->list_pos = (ServerListPosition)this->servers.size() - this->list_pos - 1;
				} else {
					this->servers.SetSortType(widget - WID_NG_NAME);
					this->servers.ForceResort();
					this->SortNetworkGameList();
				}
				this->ScrollToSelectedServer();
				this->SetDirty();
				break;

			case WID_NG_MATRIX: { // Show available network games
				uint id_v = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_NG_MATRIX);
				this->server = (id_v < this->servers.size()) ? this->servers[id_v] : nullptr;
				this->list_pos = (server == nullptr) ? SLP_INVALID : id_v;
				this->SetDirty();

				/* FIXME the disabling should go into some InvalidateData, which is called instead of the SetDirty */
				if (click_count > 1 && !this->IsWidgetDisabled(WID_NG_JOIN)) this->OnClick(pt, WID_NG_JOIN, 1);
				break;
			}

			case WID_NG_LASTJOINED: {
				if (this->last_joined != nullptr) {
					this->server = this->last_joined;

					/* search the position of the newly selected server */
					this->UpdateListPos();
					this->ScrollToSelectedServer();
					this->SetDirty();

					/* FIXME the disabling should go into some InvalidateData, which is called instead of the SetDirty */
					if (click_count > 1 && !this->IsWidgetDisabled(WID_NG_JOIN)) this->OnClick(pt, WID_NG_JOIN, 1);
				}
				break;
			}

			case WID_NG_SEARCH_INTERNET:
				_network_coordinator_client.GetListing();
				this->searched_internet = true;
				break;

			case WID_NG_SEARCH_LAN:
				NetworkUDPSearchGame();
				break;

			case WID_NG_ADD: // Add a server
				SetDParamStr(0, _settings_client.network.connect_to_ip);
				ShowQueryString(
					STR_JUST_RAW_STRING,
					STR_NETWORK_SERVER_LIST_ENTER_SERVER_ADDRESS,
					NETWORK_HOSTNAME_PORT_LENGTH,  // maximum number of characters including '\0'
					this, CS_ALPHANUMERAL, QSF_ACCEPT_UNCHANGED);
				break;

			case WID_NG_START: // Start server
				ShowNetworkStartServerWindow();
				break;

			case WID_NG_JOIN: // Join Game
				if (this->server != nullptr) {
					ShowNetworkLobbyWindow(this->server);
				}
				break;

			case WID_NG_REFRESH: // Refresh
				if (this->server != nullptr) NetworkQueryServer(this->server->connection_string);
				break;

			case WID_NG_NEWGRF: // NewGRF Settings
				if (this->server != nullptr) {
					NetworkQueryServer(this->server->connection_string);
					ShowNewGRFSettings(false, false, false, &this->server->info.grfconfig);
				}
				break;

			case WID_NG_NEWGRF_MISSING: // Find missing content online
				if (this->server != nullptr) ShowMissingContentWindow(this->server->info.grfconfig);
				break;
		}
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		this->servers.ForceRebuild();
		this->SetDirty();
	}

	EventState OnKeyPress(WChar key, uint16 keycode) override
	{
		EventState state = ES_NOT_HANDLED;

		/* handle up, down, pageup, pagedown, home and end */
		if (this->vscroll->UpdateListPositionOnKeyPress(this->list_pos, keycode) == ES_HANDLED) {
			if (this->list_pos == SLP_INVALID) return ES_HANDLED;

			this->server = this->servers[this->list_pos];

			/* Scroll to the new server if it is outside the current range. */
			this->ScrollToSelectedServer();

			/* redraw window */
			this->SetDirty();
			return ES_HANDLED;
		}

		if (this->server != nullptr) {
			if (keycode == WKC_DELETE) { // Press 'delete' to remove servers
				NetworkGameListRemoveItem(this->server);
				if (this->server == this->last_joined) this->last_joined = nullptr;
				this->server = nullptr;
				this->list_pos = SLP_INVALID;
			}
		}

		return state;
	}

	void OnEditboxChanged(int wid) override
	{
		switch (wid) {
			case WID_NG_FILTER: {
				this->servers.ForceRebuild();
				this->BuildGUINetworkGameList();
				this->ScrollToSelectedServer();
				this->SetDirty();
				break;
			}

			case WID_NG_CLIENT:
				/* Validation of the name will happen once the user tries to join or start a game, as getting
				 * error messages while typing (e.g. when you clear the name) defeats the purpose of the check. */
				_settings_client.network.client_name = this->name_editbox.text.buf;
				break;
		}
	}

	void OnQueryTextFinished(char *str) override
	{
		if (!StrEmpty(str)) {
			_settings_client.network.connect_to_ip = str;
			NetworkAddServer(str);
			NetworkRebuildHostList();
		}
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_NG_MATRIX);
	}

	void OnRealtimeTick(uint delta_ms) override
	{
		if (!this->searched_internet) return;
		if (!this->requery_timer.Elapsed(delta_ms)) return;
		this->requery_timer.SetInterval(NETWORK_LIST_REFRESH_DELAY * 1000);

		_network_coordinator_client.GetListing();
	}
};

Listing NetworkGameWindow::last_sorting = {false, 5};
GUIGameServerList::SortFunction * const NetworkGameWindow::sorter_funcs[] = {
	&NGameNameSorter,
	&NGameClientSorter,
	&NGameMapSizeSorter,
	&NGameDateSorter,
	&NGameYearsSorter,
	&NGameAllowedSorter
};

GUIGameServerList::FilterFunction * const NetworkGameWindow::filter_funcs[] = {
	&NGameSearchFilter
};

static NWidgetBase *MakeResizableHeader(int *biggest_index)
{
	*biggest_index = std::max<int>(*biggest_index, WID_NG_INFO);
	return new NWidgetServerListHeader();
}

static const NWidgetPart _nested_network_game_widgets[] = {
	/* TOP */
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_LIGHT_BLUE),
		NWidget(WWT_CAPTION, COLOUR_LIGHT_BLUE), SetDataTip(STR_NETWORK_SERVER_LIST_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_DEFSIZEBOX, COLOUR_LIGHT_BLUE),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_LIGHT_BLUE, WID_NG_MAIN),
		NWidget(NWID_VERTICAL), SetPIP(10, 7, 0),
			NWidget(NWID_HORIZONTAL), SetPIP(10, 7, 10),
				/* LEFT SIDE */
				NWidget(NWID_VERTICAL), SetPIP(0, 7, 0),
					NWidget(NWID_HORIZONTAL), SetPIP(0, 7, 0),
						NWidget(WWT_TEXT, COLOUR_LIGHT_BLUE, WID_NG_FILTER_LABEL), SetDataTip(STR_LIST_FILTER_TITLE, STR_NULL),
						NWidget(WWT_EDITBOX, COLOUR_LIGHT_BLUE, WID_NG_FILTER), SetMinimalSize(251, 12), SetFill(1, 0), SetResize(1, 0),
											SetDataTip(STR_LIST_FILTER_OSKTITLE, STR_LIST_FILTER_TOOLTIP),
					EndContainer(),
					NWidget(NWID_HORIZONTAL),
						NWidget(NWID_VERTICAL),
							NWidgetFunction(MakeResizableHeader),
							NWidget(WWT_MATRIX, COLOUR_LIGHT_BLUE, WID_NG_MATRIX), SetResize(1, 1), SetFill(1, 0),
												SetMatrixDataTip(1, 0, STR_NETWORK_SERVER_LIST_CLICK_GAME_TO_SELECT), SetScrollbar(WID_NG_SCROLLBAR),
						EndContainer(),
						NWidget(NWID_VSCROLLBAR, COLOUR_LIGHT_BLUE, WID_NG_SCROLLBAR),
					EndContainer(),
					NWidget(NWID_VERTICAL),
						NWidget(WWT_TEXT, COLOUR_LIGHT_BLUE, WID_NG_LASTJOINED_LABEL), SetFill(1, 0),
											SetDataTip(STR_NETWORK_SERVER_LIST_LAST_JOINED_SERVER, STR_NULL), SetResize(1, 0),
						NWidget(NWID_HORIZONTAL),
							NWidget(WWT_PANEL, COLOUR_LIGHT_BLUE, WID_NG_LASTJOINED), SetFill(1, 0), SetResize(1, 0),
												SetDataTip(0x0, STR_NETWORK_SERVER_LIST_CLICK_TO_SELECT_LAST),
							EndContainer(),
							NWidget(WWT_EMPTY, INVALID_COLOUR, WID_NG_LASTJOINED_SPACER), SetFill(0, 0),
						EndContainer(),
					EndContainer(),
				EndContainer(),
				/* RIGHT SIDE */
				NWidget(NWID_VERTICAL), SetPIP(0, 7, 0),
					NWidget(NWID_HORIZONTAL), SetPIP(0, 7, 0),
						NWidget(WWT_TEXT, COLOUR_LIGHT_BLUE, WID_NG_CLIENT_LABEL), SetDataTip(STR_NETWORK_SERVER_LIST_PLAYER_NAME, STR_NULL),
						NWidget(WWT_EDITBOX, COLOUR_LIGHT_BLUE, WID_NG_CLIENT), SetMinimalSize(151, 12), SetFill(1, 0), SetResize(1, 0),
											SetDataTip(STR_NETWORK_SERVER_LIST_PLAYER_NAME_OSKTITLE, STR_NETWORK_SERVER_LIST_ENTER_NAME_TOOLTIP),
					EndContainer(),
					NWidget(WWT_PANEL, COLOUR_LIGHT_BLUE, WID_NG_DETAILS),
						NWidget(NWID_VERTICAL, NC_EQUALSIZE), SetPIP(5, 5, 5),
							NWidget(WWT_EMPTY, INVALID_COLOUR, WID_NG_DETAILS_SPACER), SetMinimalSize(270, 0), SetMinimalTextLines(15, 24 + WD_PAR_VSEP_NORMAL), SetResize(0, 1), SetFill(1, 1), // Make sure it's at least this wide
							NWidget(NWID_HORIZONTAL, NC_NONE), SetPIP(5, 5, 5),
								NWidget(NWID_SELECTION, INVALID_COLOUR, WID_NG_NEWGRF_MISSING_SEL),
									NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NG_NEWGRF_MISSING), SetFill(1, 0), SetDataTip(STR_NEWGRF_SETTINGS_FIND_MISSING_CONTENT_BUTTON, STR_NEWGRF_SETTINGS_FIND_MISSING_CONTENT_TOOLTIP),
									NWidget(NWID_SPACER), SetFill(1, 0),
								EndContainer(),
							EndContainer(),
							NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(5, 5, 5),
								NWidget(NWID_SPACER), SetFill(1, 0),
								NWidget(NWID_SELECTION, INVALID_COLOUR, WID_NG_NEWGRF_SEL),
									NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NG_NEWGRF), SetFill(1, 0), SetDataTip(STR_INTRO_NEWGRF_SETTINGS, STR_NULL),
									NWidget(NWID_SPACER), SetFill(1, 0),
								EndContainer(),
							EndContainer(),
							NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(5, 5, 5),
								NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NG_JOIN), SetFill(1, 0), SetDataTip(STR_NETWORK_SERVER_LIST_JOIN_GAME, STR_NULL),
								NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NG_REFRESH), SetFill(1, 0), SetDataTip(STR_NETWORK_SERVER_LIST_REFRESH, STR_NETWORK_SERVER_LIST_REFRESH_TOOLTIP),
							EndContainer(),
						EndContainer(),
					EndContainer(),
				EndContainer(),
			EndContainer(),
			/* BOTTOM */
			NWidget(NWID_HORIZONTAL),
				NWidget(NWID_VERTICAL),
					NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(10, 7, 4),
						NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NG_SEARCH_INTERNET), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_NETWORK_SERVER_LIST_SEARCH_SERVER_INTERNET, STR_NETWORK_SERVER_LIST_SEARCH_SERVER_INTERNET_TOOLTIP),
						NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NG_SEARCH_LAN), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_NETWORK_SERVER_LIST_SEARCH_SERVER_LAN, STR_NETWORK_SERVER_LIST_SEARCH_SERVER_LAN_TOOLTIP),
						NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NG_ADD), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_NETWORK_SERVER_LIST_ADD_SERVER, STR_NETWORK_SERVER_LIST_ADD_SERVER_TOOLTIP),
						NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NG_START), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_NETWORK_SERVER_LIST_START_SERVER, STR_NETWORK_SERVER_LIST_START_SERVER_TOOLTIP),
						NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NG_CANCEL), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_BUTTON_CANCEL, STR_NULL),
					EndContainer(),
					NWidget(NWID_SPACER), SetMinimalSize(0, 6), SetResize(1, 0), SetFill(1, 0),
				EndContainer(),
				NWidget(NWID_VERTICAL),
					NWidget(NWID_SPACER), SetFill(0, 1),
					NWidget(WWT_RESIZEBOX, COLOUR_LIGHT_BLUE),
				EndContainer(),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _network_game_window_desc(
	WDP_CENTER, "list_servers", 1000, 730,
	WC_NETWORK_WINDOW, WC_NONE,
	0,
	_nested_network_game_widgets, lengthof(_nested_network_game_widgets)
);

void ShowNetworkGameWindow()
{
	static bool first = true;
	CloseWindowById(WC_NETWORK_WINDOW, WN_NETWORK_WINDOW_LOBBY);
	CloseWindowById(WC_NETWORK_WINDOW, WN_NETWORK_WINDOW_START);

	/* Only show once */
	if (first) {
		first = false;
		/* Add all servers from the config file to our list. */
		for (const auto &iter : _network_host_list) {
			NetworkAddServer(iter);
		}
	}

	new NetworkGameWindow(&_network_game_window_desc);
}

struct NetworkStartServerWindow : public Window {
	byte widget_id;              ///< The widget that has the pop-up input menu
	QueryString name_editbox;    ///< Server name editbox.

	NetworkStartServerWindow(WindowDesc *desc) : Window(desc), name_editbox(NETWORK_NAME_LENGTH)
	{
		this->InitNested(WN_NETWORK_WINDOW_START);

		this->querystrings[WID_NSS_GAMENAME] = &this->name_editbox;
		this->name_editbox.text.Assign(_settings_client.network.server_name.c_str());

		this->SetFocusedWidget(WID_NSS_GAMENAME);
	}

	void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_NSS_CONNTYPE_BTN:
				SetDParam(0, STR_NETWORK_SERVER_VISIBILITY_LOCAL + _settings_client.network.server_game_type);
				break;

			case WID_NSS_CLIENTS_TXT:
				SetDParam(0, _settings_client.network.max_clients);
				break;

			case WID_NSS_COMPANIES_TXT:
				SetDParam(0, _settings_client.network.max_companies);
				break;

			case WID_NSS_SPECTATORS_TXT:
				SetDParam(0, _settings_client.network.max_spectators);
				break;
		}
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_NSS_CONNTYPE_BTN:
				*size = maxdim(maxdim(GetStringBoundingBox(STR_NETWORK_SERVER_VISIBILITY_LOCAL), GetStringBoundingBox(STR_NETWORK_SERVER_VISIBILITY_PUBLIC)), GetStringBoundingBox(STR_NETWORK_SERVER_VISIBILITY_INVITE_ONLY));
				size->width += padding.width;
				size->height += padding.height;
				break;
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_NSS_SETPWD:
				/* If password is set, draw red '*' next to 'Set password' button. */
				if (!_settings_client.network.server_password.empty()) DrawString(r.right + WD_FRAMERECT_LEFT, this->width - WD_FRAMERECT_RIGHT, r.top, "*", TC_RED);
		}
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_NSS_CANCEL: // Cancel button
				ShowNetworkGameWindow();
				break;

			case WID_NSS_SETPWD: // Set password button
				this->widget_id = WID_NSS_SETPWD;
				SetDParamStr(0, _settings_client.network.server_password);
				ShowQueryString(STR_JUST_RAW_STRING, STR_NETWORK_START_SERVER_SET_PASSWORD, 20, this, CS_ALPHANUMERAL, QSF_NONE);
				break;

			case WID_NSS_CONNTYPE_BTN: // Connection type
				ShowDropDownList(this, BuildVisibilityDropDownList(), _settings_client.network.server_game_type, WID_NSS_CONNTYPE_BTN);
				break;

			case WID_NSS_CLIENTS_BTND:    case WID_NSS_CLIENTS_BTNU:    // Click on up/down button for number of clients
			case WID_NSS_COMPANIES_BTND:  case WID_NSS_COMPANIES_BTNU:  // Click on up/down button for number of companies
			case WID_NSS_SPECTATORS_BTND: case WID_NSS_SPECTATORS_BTNU: // Click on up/down button for number of spectators
				/* Don't allow too fast scrolling. */
				if (!(this->flags & WF_TIMEOUT) || this->timeout_timer <= 1) {
					this->HandleButtonClick(widget);
					this->SetDirty();
					switch (widget) {
						default: NOT_REACHED();
						case WID_NSS_CLIENTS_BTND: case WID_NSS_CLIENTS_BTNU:
							_settings_client.network.max_clients    = Clamp(_settings_client.network.max_clients    + widget - WID_NSS_CLIENTS_TXT,    2, MAX_CLIENTS);
							break;
						case WID_NSS_COMPANIES_BTND: case WID_NSS_COMPANIES_BTNU:
							_settings_client.network.max_companies  = Clamp(_settings_client.network.max_companies  + widget - WID_NSS_COMPANIES_TXT,  1, MAX_COMPANIES);
							break;
						case WID_NSS_SPECTATORS_BTND: case WID_NSS_SPECTATORS_BTNU:
							_settings_client.network.max_spectators = Clamp(_settings_client.network.max_spectators + widget - WID_NSS_SPECTATORS_TXT, 0, MAX_CLIENTS);
							break;
					}
				}
				_left_button_clicked = false;
				break;

			case WID_NSS_CLIENTS_TXT:    // Click on number of clients
				this->widget_id = WID_NSS_CLIENTS_TXT;
				SetDParam(0, _settings_client.network.max_clients);
				ShowQueryString(STR_JUST_INT, STR_NETWORK_START_SERVER_NUMBER_OF_CLIENTS,    4, this, CS_NUMERAL, QSF_NONE);
				break;

			case WID_NSS_COMPANIES_TXT:  // Click on number of companies
				this->widget_id = WID_NSS_COMPANIES_TXT;
				SetDParam(0, _settings_client.network.max_companies);
				ShowQueryString(STR_JUST_INT, STR_NETWORK_START_SERVER_NUMBER_OF_COMPANIES,  3, this, CS_NUMERAL, QSF_NONE);
				break;

			case WID_NSS_SPECTATORS_TXT: // Click on number of spectators
				this->widget_id = WID_NSS_SPECTATORS_TXT;
				SetDParam(0, _settings_client.network.max_spectators);
				ShowQueryString(STR_JUST_INT, STR_NETWORK_START_SERVER_NUMBER_OF_SPECTATORS, 4, this, CS_NUMERAL, QSF_NONE);
				break;

			case WID_NSS_GENERATE_GAME: // Start game
				if (!CheckServerName()) return;
				_is_network_server = true;
				if (_ctrl_pressed) {
					StartNewGameWithoutGUI(GENERATE_NEW_SEED);
				} else {
					ShowGenerateLandscape();
				}
				break;

			case WID_NSS_LOAD_GAME:
				if (!CheckServerName()) return;
				_is_network_server = true;
				ShowSaveLoadDialog(FT_SAVEGAME, SLO_LOAD);
				break;

			case WID_NSS_PLAY_SCENARIO:
				if (!CheckServerName()) return;
				_is_network_server = true;
				ShowSaveLoadDialog(FT_SCENARIO, SLO_LOAD);
				break;

			case WID_NSS_PLAY_HEIGHTMAP:
				if (!CheckServerName()) return;
				_is_network_server = true;
				ShowSaveLoadDialog(FT_HEIGHTMAP,SLO_LOAD);
				break;
		}
	}

	void OnDropdownSelect(int widget, int index) override
	{
		switch (widget) {
			case WID_NSS_CONNTYPE_BTN:
				_settings_client.network.server_game_type = (ServerGameType)index;
				break;
			default:
				NOT_REACHED();
		}

		this->SetDirty();
	}

	bool CheckServerName()
	{
		std::string str = this->name_editbox.text.buf;
		if (!NetworkValidateServerName(str)) return false;

		SetSettingValue(GetSettingFromName("network.server_name")->AsStringSetting(), str);
		return true;
	}

	void OnTimeout() override
	{
		static const int raise_widgets[] = {WID_NSS_CLIENTS_BTND, WID_NSS_CLIENTS_BTNU, WID_NSS_COMPANIES_BTND, WID_NSS_COMPANIES_BTNU, WID_NSS_SPECTATORS_BTND, WID_NSS_SPECTATORS_BTNU, WIDGET_LIST_END};
		for (const int *widget = raise_widgets; *widget != WIDGET_LIST_END; widget++) {
			if (this->IsWidgetLowered(*widget)) {
				this->RaiseWidget(*widget);
				this->SetWidgetDirty(*widget);
			}
		}
	}

	void OnQueryTextFinished(char *str) override
	{
		if (str == nullptr) return;

		if (this->widget_id == WID_NSS_SETPWD) {
			_settings_client.network.server_password = str;
		} else {
			int32 value = atoi(str);
			this->SetWidgetDirty(this->widget_id);
			switch (this->widget_id) {
				default: NOT_REACHED();
				case WID_NSS_CLIENTS_TXT:    _settings_client.network.max_clients    = Clamp(value, 2, MAX_CLIENTS); break;
				case WID_NSS_COMPANIES_TXT:  _settings_client.network.max_companies  = Clamp(value, 1, MAX_COMPANIES); break;
				case WID_NSS_SPECTATORS_TXT: _settings_client.network.max_spectators = Clamp(value, 0, MAX_CLIENTS); break;
			}
		}

		this->SetDirty();
	}
};

static const NWidgetPart _nested_network_start_server_window_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_LIGHT_BLUE),
		NWidget(WWT_CAPTION, COLOUR_LIGHT_BLUE), SetDataTip(STR_NETWORK_START_SERVER_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_LIGHT_BLUE, WID_NSS_BACKGROUND),
		NWidget(NWID_VERTICAL), SetPIP(10, 6, 10),
			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(10, 6, 10),
				NWidget(NWID_VERTICAL), SetPIP(0, 1, 0),
					/* Game name widgets */
					NWidget(WWT_TEXT, COLOUR_LIGHT_BLUE, WID_NSS_GAMENAME_LABEL), SetFill(1, 0), SetDataTip(STR_NETWORK_START_SERVER_NEW_GAME_NAME, STR_NULL),
					NWidget(WWT_EDITBOX, COLOUR_LIGHT_BLUE, WID_NSS_GAMENAME), SetMinimalSize(10, 12), SetFill(1, 0), SetDataTip(STR_NETWORK_START_SERVER_NEW_GAME_NAME_OSKTITLE, STR_NETWORK_START_SERVER_NEW_GAME_NAME_TOOLTIP),
				EndContainer(),
			EndContainer(),

			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(10, 6, 10),
				NWidget(NWID_VERTICAL), SetPIP(0, 1, 0),
					NWidget(WWT_TEXT, COLOUR_LIGHT_BLUE, WID_NSS_CONNTYPE_LABEL), SetFill(1, 0), SetDataTip(STR_NETWORK_START_SERVER_VISIBILITY_LABEL, STR_NULL),
					NWidget(WWT_DROPDOWN, COLOUR_LIGHT_BLUE, WID_NSS_CONNTYPE_BTN), SetFill(1, 0), SetDataTip(STR_BLACK_STRING, STR_NETWORK_START_SERVER_VISIBILITY_TOOLTIP),
				EndContainer(),
				NWidget(NWID_VERTICAL), SetPIP(0, 1, 0),
					NWidget(NWID_SPACER), SetFill(1, 1),
					NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NSS_SETPWD), SetFill(1, 0), SetDataTip(STR_NETWORK_START_SERVER_SET_PASSWORD, STR_NETWORK_START_SERVER_PASSWORD_TOOLTIP),
				EndContainer(),
			EndContainer(),

			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(10, 6, 10),
				NWidget(NWID_VERTICAL), SetPIP(0, 1, 0),
					NWidget(WWT_TEXT, COLOUR_LIGHT_BLUE, WID_NSS_CLIENTS_LABEL), SetFill(1, 0), SetDataTip(STR_NETWORK_START_SERVER_NUMBER_OF_CLIENTS, STR_NULL),
					NWidget(NWID_HORIZONTAL),
						NWidget(WWT_IMGBTN, COLOUR_LIGHT_BLUE, WID_NSS_CLIENTS_BTND), SetMinimalSize(12, 12), SetFill(0, 1), SetDataTip(SPR_ARROW_DOWN, STR_NETWORK_START_SERVER_NUMBER_OF_CLIENTS_TOOLTIP),
						NWidget(WWT_PUSHTXTBTN, COLOUR_LIGHT_BLUE, WID_NSS_CLIENTS_TXT), SetFill(1, 0), SetDataTip(STR_NETWORK_START_SERVER_CLIENTS_SELECT, STR_NETWORK_START_SERVER_NUMBER_OF_CLIENTS_TOOLTIP),
						NWidget(WWT_IMGBTN, COLOUR_LIGHT_BLUE, WID_NSS_CLIENTS_BTNU), SetMinimalSize(12, 12), SetFill(0, 1), SetDataTip(SPR_ARROW_UP, STR_NETWORK_START_SERVER_NUMBER_OF_CLIENTS_TOOLTIP),
					EndContainer(),
				EndContainer(),

				NWidget(NWID_VERTICAL), SetPIP(0, 1, 0),
					NWidget(WWT_TEXT, COLOUR_LIGHT_BLUE, WID_NSS_COMPANIES_LABEL), SetFill(1, 0), SetDataTip(STR_NETWORK_START_SERVER_NUMBER_OF_COMPANIES, STR_NULL),
					NWidget(NWID_HORIZONTAL),
						NWidget(WWT_IMGBTN, COLOUR_LIGHT_BLUE, WID_NSS_COMPANIES_BTND), SetMinimalSize(12, 12), SetFill(0, 1), SetDataTip(SPR_ARROW_DOWN, STR_NETWORK_START_SERVER_NUMBER_OF_COMPANIES_TOOLTIP),
						NWidget(WWT_PUSHTXTBTN, COLOUR_LIGHT_BLUE, WID_NSS_COMPANIES_TXT), SetFill(1, 0), SetDataTip(STR_NETWORK_START_SERVER_COMPANIES_SELECT, STR_NETWORK_START_SERVER_NUMBER_OF_COMPANIES_TOOLTIP),
						NWidget(WWT_IMGBTN, COLOUR_LIGHT_BLUE, WID_NSS_COMPANIES_BTNU), SetMinimalSize(12, 12), SetFill(0, 1), SetDataTip(SPR_ARROW_UP, STR_NETWORK_START_SERVER_NUMBER_OF_COMPANIES_TOOLTIP),
					EndContainer(),
				EndContainer(),

				NWidget(NWID_VERTICAL), SetPIP(0, 1, 0),
					NWidget(WWT_TEXT, COLOUR_LIGHT_BLUE, WID_NSS_SPECTATORS_LABEL), SetFill(1, 0), SetDataTip(STR_NETWORK_START_SERVER_NUMBER_OF_SPECTATORS, STR_NULL),
					NWidget(NWID_HORIZONTAL),
						NWidget(WWT_IMGBTN, COLOUR_LIGHT_BLUE, WID_NSS_SPECTATORS_BTND), SetMinimalSize(12, 12), SetFill(0, 1), SetDataTip(SPR_ARROW_DOWN, STR_NETWORK_START_SERVER_NUMBER_OF_SPECTATORS_TOOLTIP),
						NWidget(WWT_PUSHTXTBTN, COLOUR_LIGHT_BLUE, WID_NSS_SPECTATORS_TXT), SetFill(1, 0), SetDataTip(STR_NETWORK_START_SERVER_SPECTATORS_SELECT, STR_NETWORK_START_SERVER_NUMBER_OF_SPECTATORS_TOOLTIP),
						NWidget(WWT_IMGBTN, COLOUR_LIGHT_BLUE, WID_NSS_SPECTATORS_BTNU), SetMinimalSize(12, 12), SetFill(0, 1), SetDataTip(SPR_ARROW_UP, STR_NETWORK_START_SERVER_NUMBER_OF_SPECTATORS_TOOLTIP),
					EndContainer(),
				EndContainer(),
			EndContainer(),

			/* 'generate game' and 'load game' buttons */
			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(10, 6, 10),
				NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NSS_GENERATE_GAME), SetDataTip(STR_INTRO_NEW_GAME, STR_INTRO_TOOLTIP_NEW_GAME), SetFill(1, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NSS_LOAD_GAME), SetDataTip(STR_INTRO_LOAD_GAME, STR_INTRO_TOOLTIP_LOAD_GAME), SetFill(1, 0),
			EndContainer(),

			/* 'play scenario' and 'play heightmap' buttons */
			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(10, 6, 10),
				NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NSS_PLAY_SCENARIO), SetDataTip(STR_INTRO_PLAY_SCENARIO, STR_INTRO_TOOLTIP_PLAY_SCENARIO), SetFill(1, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NSS_PLAY_HEIGHTMAP), SetDataTip(STR_INTRO_PLAY_HEIGHTMAP, STR_INTRO_TOOLTIP_PLAY_HEIGHTMAP), SetFill(1, 0),
			EndContainer(),

			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(10, 0, 10),
				NWidget(NWID_SPACER), SetFill(1, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NSS_CANCEL), SetDataTip(STR_BUTTON_CANCEL, STR_NULL), SetMinimalSize(128, 12),
				NWidget(NWID_SPACER), SetFill(1, 0),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _network_start_server_window_desc(
	WDP_CENTER, nullptr, 0, 0,
	WC_NETWORK_WINDOW, WC_NONE,
	0,
	_nested_network_start_server_window_widgets, lengthof(_nested_network_start_server_window_widgets)
);

static void ShowNetworkStartServerWindow()
{
	if (!NetworkValidateOurClientName()) return;

	CloseWindowById(WC_NETWORK_WINDOW, WN_NETWORK_WINDOW_GAME);
	CloseWindowById(WC_NETWORK_WINDOW, WN_NETWORK_WINDOW_LOBBY);

	new NetworkStartServerWindow(&_network_start_server_window_desc);
}

struct NetworkLobbyWindow : public Window {
	CompanyID company;       ///< Selected company
	NetworkGameList *server; ///< Selected server
	NetworkCompanyInfo company_info[MAX_COMPANIES];
	Scrollbar *vscroll;

	NetworkLobbyWindow(WindowDesc *desc, NetworkGameList *ngl) :
			Window(desc), company(INVALID_COMPANY), server(ngl)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_NL_SCROLLBAR);
		this->FinishInitNested(WN_NETWORK_WINDOW_LOBBY);
	}

	CompanyID NetworkLobbyFindCompanyIndex(byte pos) const
	{
		/* Scroll through all this->company_info and get the 'pos' item that is not empty. */
		for (CompanyID i = COMPANY_FIRST; i < MAX_COMPANIES; i++) {
			if (!this->company_info[i].company_name.empty()) {
				if (pos-- == 0) return i;
			}
		}

		return COMPANY_FIRST;
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_NL_HEADER:
				size->height = WD_MATRIX_TOP + FONT_HEIGHT_NORMAL + WD_MATRIX_BOTTOM;
				break;

			case WID_NL_MATRIX:
				resize->height = WD_MATRIX_TOP + std::max<uint>(std::max(GetSpriteSize(SPR_LOCK).height, GetSpriteSize(SPR_PROFIT_LOT).height), FONT_HEIGHT_NORMAL) + WD_MATRIX_BOTTOM;
				size->height = 10 * resize->height;
				break;

			case WID_NL_DETAILS:
				size->height = 30 + 11 * FONT_HEIGHT_NORMAL;
				break;
		}
	}

	void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_NL_TEXT:
				SetDParamStr(0, this->server->info.server_name);
				break;
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_NL_DETAILS:
				this->DrawDetails(r);
				break;

			case WID_NL_MATRIX:
				this->DrawMatrix(r);
				break;
		}
	}

	void OnPaint() override
	{
		const NetworkGameInfo *gi = &this->server->info;

		/* Join button is disabled when no company is selected and for AI companies. */
		this->SetWidgetDisabledState(WID_NL_JOIN, this->company == INVALID_COMPANY || GetLobbyCompanyInfo(this->company)->ai);
		/* Cannot start new company if there are too many. */
		this->SetWidgetDisabledState(WID_NL_NEW, gi->companies_on >= gi->companies_max);
		/* Cannot spectate if there are too many spectators. */
		this->SetWidgetDisabledState(WID_NL_SPECTATE, gi->spectators_on >= gi->spectators_max);

		this->vscroll->SetCount(gi->companies_on);

		/* Draw window widgets */
		this->DrawWidgets();
	}

	void DrawMatrix(const Rect &r) const
	{
		bool rtl = _current_text_dir == TD_RTL;
		uint left = r.left + WD_FRAMERECT_LEFT;
		uint right = r.right - WD_FRAMERECT_RIGHT;
		uint text_offset = (this->resize.step_height - WD_MATRIX_TOP - WD_MATRIX_BOTTOM - FONT_HEIGHT_NORMAL) / 2 + WD_MATRIX_TOP;

		Dimension lock_size = GetSpriteSize(SPR_LOCK);
		int lock_width      = lock_size.width;
		int lock_y_offset   = (this->resize.step_height - WD_MATRIX_TOP - WD_MATRIX_BOTTOM - lock_size.height) / 2 + WD_MATRIX_TOP;

		Dimension profit_size = GetSpriteSize(SPR_PROFIT_LOT);
		int profit_width      = lock_size.width;
		int profit_y_offset   = (this->resize.step_height - WD_MATRIX_TOP - WD_MATRIX_BOTTOM - profit_size.height) / 2 + WD_MATRIX_TOP;

		uint text_left   = left  + (rtl ? lock_width + profit_width + 4 : 0);
		uint text_right  = right - (rtl ? 0 : lock_width + profit_width + 4);
		uint profit_left = rtl ? left : right - profit_width;
		uint lock_left   = rtl ? left + profit_width + 2 : right - profit_width - lock_width - 2;

		int y = r.top;
		/* Draw company list */
		int pos = this->vscroll->GetPosition();
		while (pos < this->server->info.companies_on) {
			byte company = NetworkLobbyFindCompanyIndex(pos);
			bool income = false;
			if (this->company == company) {
				GfxFillRect(r.left + WD_BEVEL_LEFT, y + 1, r.right - WD_BEVEL_RIGHT, y + this->resize.step_height - 2, PC_GREY);  // show highlighted item with a different colour
			}

			DrawString(text_left, text_right, y + text_offset, this->company_info[company].company_name, TC_BLACK);
			if (this->company_info[company].use_password != 0) DrawSprite(SPR_LOCK, PAL_NONE, lock_left, y + lock_y_offset);

			/* If the company's income was positive puts a green dot else a red dot */
			if (this->company_info[company].income >= 0) income = true;
			DrawSprite(income ? SPR_PROFIT_LOT : SPR_PROFIT_NEGATIVE, PAL_NONE, profit_left, y + profit_y_offset);

			pos++;
			y += this->resize.step_height;
			if (pos >= this->vscroll->GetPosition() + this->vscroll->GetCapacity()) break;
		}
	}

	void DrawDetails(const Rect &r) const
	{
		const int detail_height = 12 + FONT_HEIGHT_NORMAL + 12;
		/* Draw info about selected company when it is selected in the left window. */
		GfxFillRect(r.left + 1, r.top + 1, r.right - 1, r.top + detail_height - 1, PC_DARK_BLUE);
		DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, r.top + 12, STR_NETWORK_GAME_LOBBY_COMPANY_INFO, TC_FROMSTRING, SA_HOR_CENTER);

		if (this->company == INVALID_COMPANY || this->company_info[this->company].company_name.empty()) return;

		int y = r.top + detail_height + 4;
		const NetworkGameInfo *gi = &this->server->info;

		SetDParam(0, gi->clients_on);
		SetDParam(1, gi->clients_max);
		SetDParam(2, gi->companies_on);
		SetDParam(3, gi->companies_max);
		DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_NETWORK_SERVER_LIST_CLIENTS);
		y += FONT_HEIGHT_NORMAL;

		SetDParamStr(0, this->company_info[this->company].company_name);
		DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_NETWORK_GAME_LOBBY_COMPANY_NAME);
		y += FONT_HEIGHT_NORMAL;

		SetDParam(0, this->company_info[this->company].inaugurated_year);
		DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_NETWORK_GAME_LOBBY_INAUGURATION_YEAR); // inauguration year
		y += FONT_HEIGHT_NORMAL;

		SetDParam(0, this->company_info[this->company].company_value);
		DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_NETWORK_GAME_LOBBY_VALUE); // company value
		y += FONT_HEIGHT_NORMAL;

		SetDParam(0, this->company_info[this->company].money);
		DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_NETWORK_GAME_LOBBY_CURRENT_BALANCE); // current balance
		y += FONT_HEIGHT_NORMAL;

		SetDParam(0, this->company_info[this->company].income);
		DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_NETWORK_GAME_LOBBY_LAST_YEARS_INCOME); // last year's income
		y += FONT_HEIGHT_NORMAL;

		SetDParam(0, this->company_info[this->company].performance);
		DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_NETWORK_GAME_LOBBY_PERFORMANCE); // performance
		y += FONT_HEIGHT_NORMAL;

		SetDParam(0, this->company_info[this->company].num_vehicle[NETWORK_VEH_TRAIN]);
		SetDParam(1, this->company_info[this->company].num_vehicle[NETWORK_VEH_LORRY]);
		SetDParam(2, this->company_info[this->company].num_vehicle[NETWORK_VEH_BUS]);
		SetDParam(3, this->company_info[this->company].num_vehicle[NETWORK_VEH_SHIP]);
		SetDParam(4, this->company_info[this->company].num_vehicle[NETWORK_VEH_PLANE]);
		DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_NETWORK_GAME_LOBBY_VEHICLES); // vehicles
		y += FONT_HEIGHT_NORMAL;

		SetDParam(0, this->company_info[this->company].num_station[NETWORK_VEH_TRAIN]);
		SetDParam(1, this->company_info[this->company].num_station[NETWORK_VEH_LORRY]);
		SetDParam(2, this->company_info[this->company].num_station[NETWORK_VEH_BUS]);
		SetDParam(3, this->company_info[this->company].num_station[NETWORK_VEH_SHIP]);
		SetDParam(4, this->company_info[this->company].num_station[NETWORK_VEH_PLANE]);
		DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_NETWORK_GAME_LOBBY_STATIONS); // stations
		y += FONT_HEIGHT_NORMAL;

		SetDParamStr(0, this->company_info[this->company].clients);
		DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_NETWORK_GAME_LOBBY_PLAYERS); // players
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_NL_CANCEL:   // Cancel button
				ShowNetworkGameWindow();
				break;

			case WID_NL_MATRIX: { // Company list
				uint id_v = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_NL_MATRIX);
				this->company = (id_v >= this->server->info.companies_on) ? INVALID_COMPANY : NetworkLobbyFindCompanyIndex(id_v);
				this->SetDirty();

				/* FIXME the disabling should go into some InvalidateData, which is called instead of the SetDirty */
				if (click_count > 1 && !this->IsWidgetDisabled(WID_NL_JOIN)) this->OnClick(pt, WID_NL_JOIN, 1);
				break;
			}

			case WID_NL_JOIN:     // Join company
				/* Button can be clicked only when it is enabled. */
				NetworkClientConnectGame(this->server->connection_string, this->company);
				break;

			case WID_NL_NEW:      // New company
				NetworkClientConnectGame(this->server->connection_string, COMPANY_NEW_COMPANY);
				break;

			case WID_NL_SPECTATE: // Spectate game
				NetworkClientConnectGame(this->server->connection_string, COMPANY_SPECTATOR);
				break;

			case WID_NL_REFRESH:  // Refresh
				/* Clear the information so removed companies don't remain */
				for (auto &company : this->company_info) company = {};

				NetworkQueryLobbyServer(this->server->connection_string);
				break;
		}
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_NL_MATRIX);
	}
};

static const NWidgetPart _nested_network_lobby_window_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_LIGHT_BLUE),
		NWidget(WWT_CAPTION, COLOUR_LIGHT_BLUE), SetDataTip(STR_NETWORK_GAME_LOBBY_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_LIGHT_BLUE, WID_NL_BACKGROUND),
		NWidget(WWT_TEXT, COLOUR_LIGHT_BLUE, WID_NL_TEXT), SetDataTip(STR_NETWORK_GAME_LOBBY_PREPARE_TO_JOIN, STR_NULL), SetResize(1, 0), SetPadding(10, 10, 0, 10),
		NWidget(NWID_SPACER), SetMinimalSize(0, 3),
		NWidget(NWID_HORIZONTAL), SetPIP(10, 0, 10),
			/* Company list. */
			NWidget(NWID_VERTICAL),
				NWidget(WWT_PANEL, COLOUR_WHITE, WID_NL_HEADER), SetMinimalSize(146, 0), SetResize(1, 0), SetFill(1, 0), EndContainer(),
				NWidget(WWT_MATRIX, COLOUR_LIGHT_BLUE, WID_NL_MATRIX), SetMinimalSize(146, 0), SetResize(1, 1), SetFill(1, 1), SetMatrixDataTip(1, 0, STR_NETWORK_GAME_LOBBY_COMPANY_LIST_TOOLTIP), SetScrollbar(WID_NL_SCROLLBAR),
			EndContainer(),
			NWidget(NWID_VSCROLLBAR, COLOUR_LIGHT_BLUE, WID_NL_SCROLLBAR),
			NWidget(NWID_SPACER), SetMinimalSize(5, 0), SetResize(0, 1),
			/* Company info. */
			NWidget(WWT_PANEL, COLOUR_LIGHT_BLUE, WID_NL_DETAILS), SetMinimalSize(232, 0), SetResize(1, 1), SetFill(1, 1), EndContainer(),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 9),
		/* Buttons. */
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(10, 3, 10),
			NWidget(NWID_VERTICAL, NC_EQUALSIZE), SetPIP(0, 3, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NL_JOIN), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_NETWORK_GAME_LOBBY_JOIN_COMPANY, STR_NETWORK_GAME_LOBBY_JOIN_COMPANY_TOOLTIP),
				NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NL_NEW), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_NETWORK_GAME_LOBBY_NEW_COMPANY, STR_NETWORK_GAME_LOBBY_NEW_COMPANY_TOOLTIP),
			EndContainer(),
			NWidget(NWID_VERTICAL, NC_EQUALSIZE), SetPIP(0, 3, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NL_SPECTATE), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_NETWORK_GAME_LOBBY_SPECTATE_GAME, STR_NETWORK_GAME_LOBBY_SPECTATE_GAME_TOOLTIP),
				NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NL_REFRESH), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_NETWORK_SERVER_LIST_REFRESH, STR_NETWORK_SERVER_LIST_REFRESH_TOOLTIP),
			EndContainer(),
			NWidget(NWID_VERTICAL, NC_EQUALSIZE), SetPIP(0, 3, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NL_CANCEL), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_BUTTON_CANCEL, STR_NULL),
				NWidget(NWID_SPACER), SetFill(1, 1),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 8),
	EndContainer(),
};

static WindowDesc _network_lobby_window_desc(
	WDP_CENTER, nullptr, 0, 0,
	WC_NETWORK_WINDOW, WC_NONE,
	0,
	_nested_network_lobby_window_widgets, lengthof(_nested_network_lobby_window_widgets)
);

/**
 * Show the networklobbywindow with the selected server.
 * @param ngl Selected game pointer which is passed to the new window.
 */
static void ShowNetworkLobbyWindow(NetworkGameList *ngl)
{
	if (!NetworkValidateOurClientName()) return;

	CloseWindowById(WC_NETWORK_WINDOW, WN_NETWORK_WINDOW_START);
	CloseWindowById(WC_NETWORK_WINDOW, WN_NETWORK_WINDOW_GAME);

	_settings_client.network.last_joined = ngl->connection_string;

	NetworkQueryLobbyServer(ngl->connection_string);

	new NetworkLobbyWindow(&_network_lobby_window_desc, ngl);
}

/**
 * Get the company information of a given company to fill for the lobby.
 * @param company the company to get the company info struct from.
 * @return the company info struct to write the (downloaded) data to.
 */
NetworkCompanyInfo *GetLobbyCompanyInfo(CompanyID company)
{
	NetworkLobbyWindow *lobby = dynamic_cast<NetworkLobbyWindow*>(FindWindowById(WC_NETWORK_WINDOW, WN_NETWORK_WINDOW_LOBBY));
	return (lobby != nullptr && company < MAX_COMPANIES) ? &lobby->company_info[company] : nullptr;
}

/**
 * Get the game information for the lobby.
 * @return the game info struct to write the (downloaded) data to.
 */
NetworkGameList *GetLobbyGameInfo()
{
	NetworkLobbyWindow *lobby = dynamic_cast<NetworkLobbyWindow *>(FindWindowById(WC_NETWORK_WINDOW, WN_NETWORK_WINDOW_LOBBY));
	return lobby != nullptr ? lobby->server : nullptr;
}

/* The window below gives information about the connected clients
 * and also makes able to kick them (if server) and stuff like that. */

extern void DrawCompanyIcon(CompanyID cid, int x, int y);

static const NWidgetPart _nested_client_list_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY), SetDataTip(STR_NETWORK_CLIENT_LIST_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_CL_SERVER_SELECTOR),
			NWidget(WWT_FRAME, COLOUR_GREY), SetDataTip(STR_NETWORK_CLIENT_LIST_SERVER, STR_NULL), SetPadding(4, 4, 0, 4), SetPIP(0, 2, 0),
				NWidget(NWID_HORIZONTAL), SetPIP(0, 3, 0),
					NWidget(WWT_TEXT, COLOUR_GREY), SetMinimalTextLines(1, 0), SetDataTip(STR_NETWORK_CLIENT_LIST_SERVER_NAME, STR_NULL),
					NWidget(NWID_SPACER), SetMinimalSize(10, 0),
					NWidget(WWT_TEXT, COLOUR_GREY, WID_CL_SERVER_NAME), SetFill(1, 0), SetMinimalTextLines(1, 0), SetResize(1, 0), SetDataTip(STR_BLACK_RAW_STRING, STR_NETWORK_CLIENT_LIST_SERVER_NAME_TOOLTIP), SetAlignment(SA_VERT_CENTER | SA_RIGHT),
					NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_CL_SERVER_NAME_EDIT), SetMinimalSize(12, 14), SetDataTip(SPR_RENAME, STR_NETWORK_CLIENT_LIST_SERVER_NAME_EDIT_TOOLTIP),
				EndContainer(),
				NWidget(NWID_HORIZONTAL), SetPIP(0, 3, 0),
					NWidget(WWT_TEXT, COLOUR_GREY), SetMinimalTextLines(1, 0), SetDataTip(STR_NETWORK_CLIENT_LIST_SERVER_VISIBILITY, STR_NULL),
					NWidget(NWID_SPACER), SetMinimalSize(10, 0), SetFill(1, 0), SetResize(1, 0),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_CL_SERVER_VISIBILITY), SetDataTip(STR_BLACK_STRING, STR_NETWORK_CLIENT_LIST_SERVER_VISIBILITY_TOOLTIP),
				EndContainer(),
				NWidget(NWID_HORIZONTAL), SetPIP(0, 3, 0),
					NWidget(WWT_TEXT, COLOUR_GREY), SetMinimalTextLines(1, 0), SetDataTip(STR_NETWORK_CLIENT_LIST_SERVER_JOIN_KEY, STR_NULL),
					NWidget(NWID_SPACER), SetMinimalSize(10, 0),
					NWidget(WWT_TEXT, COLOUR_GREY, WID_CL_SERVER_JOIN_KEY), SetFill(1, 0), SetMinimalTextLines(1, 0), SetResize(1, 0), SetDataTip(STR_BLACK_RAW_STRING, STR_NETWORK_CLIENT_LIST_SERVER_JOIN_KEY_TOOLTIP), SetAlignment(SA_VERT_CENTER | SA_RIGHT),
				EndContainer(),
				NWidget(NWID_HORIZONTAL), SetPIP(0, 3, 0),
					NWidget(WWT_TEXT, COLOUR_GREY), SetMinimalTextLines(1, 0), SetDataTip(STR_NETWORK_CLIENT_LIST_SERVER_CONNECTION_TYPE, STR_NULL),
					NWidget(NWID_SPACER), SetMinimalSize(10, 0),
					NWidget(WWT_TEXT, COLOUR_GREY, WID_CL_SERVER_CONNECTION_TYPE), SetFill(1, 0), SetMinimalTextLines(1, 0), SetResize(1, 0), SetDataTip(STR_BLACK_STRING, STR_NETWORK_CLIENT_LIST_SERVER_CONNECTION_TYPE_TOOLTIP), SetAlignment(SA_VERT_CENTER | SA_RIGHT),
				EndContainer(),
			EndContainer(),
		EndContainer(),
		NWidget(WWT_FRAME, COLOUR_GREY), SetDataTip(STR_NETWORK_CLIENT_LIST_PLAYER, STR_NULL), SetPadding(4, 4, 4, 4), SetPIP(0, 2, 0),
			NWidget(NWID_HORIZONTAL), SetPIP(0, 3, 0),
				NWidget(WWT_TEXT, COLOUR_GREY), SetMinimalTextLines(1, 0), SetDataTip(STR_NETWORK_CLIENT_LIST_PLAYER_NAME, STR_NULL),
				NWidget(NWID_SPACER), SetMinimalSize(10, 0),
				NWidget(WWT_TEXT, COLOUR_GREY, WID_CL_CLIENT_NAME), SetFill(1, 0), SetMinimalTextLines(1, 0), SetResize(1, 0), SetDataTip(STR_BLACK_RAW_STRING, STR_NETWORK_CLIENT_LIST_PLAYER_NAME_TOOLTIP), SetAlignment(SA_VERT_CENTER | SA_RIGHT),
				NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_CL_CLIENT_NAME_EDIT), SetMinimalSize(12, 14), SetDataTip(SPR_RENAME, STR_NETWORK_CLIENT_LIST_PLAYER_NAME_EDIT_TOOLTIP),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_HORIZONTAL),
			NWidget(NWID_VERTICAL),
				NWidget(WWT_MATRIX, COLOUR_GREY, WID_CL_MATRIX), SetMinimalSize(180, 0), SetResize(1, 1), SetFill(1, 1), SetMatrixDataTip(1, 0, STR_NULL), SetScrollbar(WID_CL_SCROLLBAR),
				NWidget(WWT_TEXT, COLOUR_GREY, WID_CL_CLIENT_COMPANY_COUNT), SetFill(1, 0), SetMinimalTextLines(1, 0), SetResize(1, 0), SetPadding(2, 1, 2, 1), SetAlignment(SA_CENTER), SetDataTip(STR_NETWORK_CLIENT_LIST_CLIENT_COMPANY_COUNT, STR_NULL),
			EndContainer(),
			NWidget(NWID_VERTICAL),
				NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_CL_SCROLLBAR),
				NWidget(WWT_RESIZEBOX, COLOUR_GREY),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _client_list_desc(
	WDP_AUTO, "list_clients", 220, 300,
	WC_CLIENT_LIST, WC_NONE,
	0,
	_nested_client_list_widgets, lengthof(_nested_client_list_widgets)
);

/**
 * The possibly entries in a DropDown for an admin.
 * Client and companies are mixed; they just have to be unique.
 */
enum DropDownAdmin {
	DD_CLIENT_ADMIN_KICK,
	DD_CLIENT_ADMIN_BAN,
	DD_COMPANY_ADMIN_RESET,
	DD_COMPANY_ADMIN_UNLOCK,
};

/**
 * Callback function for admin command to kick client.
 * @param w The window which initiated the confirmation dialog.
 * @param confirmed Iff the user pressed Yes.
 */
static void AdminClientKickCallback(Window *w, bool confirmed)
{
	if (confirmed) NetworkServerKickClient(_admin_client_id, {});
}

/**
 * Callback function for admin command to ban client.
 * @param w The window which initiated the confirmation dialog.
 * @param confirmed Iff the user pressed Yes.
 */
static void AdminClientBanCallback(Window *w, bool confirmed)
{
	if (confirmed) NetworkServerKickOrBanIP(_admin_client_id, true, {});
}

/**
 * Callback function for admin command to reset company.
 * @param w The window which initiated the confirmation dialog.
 * @param confirmed Iff the user pressed Yes.
 */
static void AdminCompanyResetCallback(Window *w, bool confirmed)
{
	if (confirmed) {
		if (NetworkCompanyHasClients(_admin_company_id)) return;
		DoCommandP(0, CCA_DELETE | _admin_company_id << 16 | CRR_MANUAL << 24, 0, CMD_COMPANY_CTRL);
	}
}

/**
 * Callback function for admin command to unlock company.
 * @param w The window which initiated the confirmation dialog.
 * @param confirmed Iff the user pressed Yes.
 */
static void AdminCompanyUnlockCallback(Window *w, bool confirmed)
{
	if (confirmed) NetworkServerSetCompanyPassword(_admin_company_id, "", false);
}

/**
 * Button shown for either a company or client in the client-list.
 *
 * These buttons are dynamic and strongly depends on which company/client
 * what buttons are available. This class allows dynamically creating them
 * as the current Widget system does not.
 */
class ButtonCommon {
public:
	SpriteID sprite;   ///< The sprite to use on the button.
	StringID tooltip;  ///< The tooltip of the button.
	Colours colour;    ///< The colour of the button.
	bool disabled;     ///< Is the button disabled?
	uint height;       ///< Calculated height of the button.
	uint width;        ///< Calculated width of the button.

	ButtonCommon(SpriteID sprite, StringID tooltip, Colours colour, bool disabled = false) :
		sprite(sprite),
		tooltip(tooltip),
		colour(colour),
		disabled(disabled)
	{
		Dimension d = GetSpriteSize(sprite);
		this->height = d.height + ScaleGUITrad(WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM);
		this->width = d.width + ScaleGUITrad(WD_FRAMERECT_LEFT + WD_FRAMERECT_RIGHT);
	}
	virtual ~ButtonCommon() {}

	/**
	 * OnClick handler for when the button is pressed.
	 */
	virtual void OnClick(struct NetworkClientListWindow *w, Point pt) = 0;
};

/**
 * Template version of Button, with callback support.
 */
template<typename T>
class Button : public ButtonCommon {
private:
	typedef void (*ButtonCallback)(struct NetworkClientListWindow *w, Point pt, T id); ///< Callback function to call on click.
	T id;                 ///< ID this button belongs to.
	ButtonCallback proc;  ///< Callback proc to call when button is pressed.

public:
	Button(SpriteID sprite, StringID tooltip, Colours colour, T id, ButtonCallback proc, bool disabled = false) :
		ButtonCommon(sprite, tooltip, colour, disabled),
		id(id),
		proc(proc)
	{
		assert(proc != nullptr);
	}

	void OnClick(struct NetworkClientListWindow *w, Point pt) override
	{
		if (this->disabled) return;

		this->proc(w, pt, this->id);
	}
};

using CompanyButton = Button<CompanyID>;
using ClientButton = Button<ClientID>;

/**
 * Main handle for clientlist
 */
struct NetworkClientListWindow : Window {
private:
	ClientListWidgets query_widget; ///< During a query this tracks what widget caused the query.
	CompanyID join_company; ///< During query for company password, this stores what company we wanted to join.

	ClientID dd_client_id; ///< During admin dropdown, track which client this was for.
	CompanyID dd_company_id; ///< During admin dropdown, track which company this was for.

	Scrollbar *vscroll; ///< Vertical scrollbar of this window.
	uint line_height; ///< Current lineheight of each entry in the matrix.
	uint line_count; ///< Amount of lines in the matrix.
	int hover_index; ///< Index of the current line we are hovering over, or -1 if none.
	int player_self_index; ///< The line the current player is on.
	int player_host_index; ///< The line the host is on.

	std::map<uint, std::vector<std::unique_ptr<ButtonCommon>>> buttons; ///< Per line which buttons are available.

	static const int CLIENT_OFFSET_LEFT = 12; ///< Offset of client entries compared to company entries.

	/**
	 * Chat button on a Company is clicked.
	 * @param w The instance of this window.
	 * @param pt The point where this button was clicked.
	 * @param company_id The company this button was assigned to.
	 */
	static void OnClickCompanyChat(NetworkClientListWindow *w, Point pt, CompanyID company_id)
	{
		ShowNetworkChatQueryWindow(DESTTYPE_TEAM, company_id);
	}

	/**
	 * Join button on a Company is clicked.
	 * @param w The instance of this window.
	 * @param pt The point where this button was clicked.
	 * @param company_id The company this button was assigned to.
	 */
	static void OnClickCompanyJoin(NetworkClientListWindow *w, Point pt, CompanyID company_id)
	{
		if (_network_server) {
			NetworkServerDoMove(CLIENT_ID_SERVER, company_id);
			MarkWholeScreenDirty();
		} else if (NetworkCompanyIsPassworded(company_id)) {
			w->query_widget = WID_CL_COMPANY_JOIN;
			w->join_company = company_id;
			ShowQueryString(STR_EMPTY, STR_NETWORK_NEED_COMPANY_PASSWORD_CAPTION, NETWORK_PASSWORD_LENGTH, w, CS_ALPHANUMERAL, QSF_PASSWORD);
		} else {
			NetworkClientRequestMove(company_id);
		}
	}

	/**
	 * Crete new company button is clicked.
	 * @param w The instance of this window.
	 * @param pt The point where this button was clicked.
	 * @param company_id The company this button was assigned to.
	 */
	static void OnClickCompanyNew(NetworkClientListWindow *w, Point pt, CompanyID company_id)
	{
		if (_network_server) {
			DoCommandP(0, CCA_NEW, _network_own_client_id, CMD_COMPANY_CTRL);
		} else {
			NetworkSendCommand(0, CCA_NEW, 0, CMD_COMPANY_CTRL, nullptr, {}, _local_company);
		}
	}

	/**
	 * Admin button on a Client is clicked.
	 * @param w The instance of this window.
	 * @param pt The point where this button was clicked.
	 * @param client_id The client this button was assigned to.
	 */
	static void OnClickClientAdmin(NetworkClientListWindow *w, Point pt, ClientID client_id)
	{
		DropDownList list;
		list.emplace_back(new DropDownListStringItem(STR_NETWORK_CLIENT_LIST_ADMIN_CLIENT_KICK, DD_CLIENT_ADMIN_KICK, false));
		list.emplace_back(new DropDownListStringItem(STR_NETWORK_CLIENT_LIST_ADMIN_CLIENT_BAN, DD_CLIENT_ADMIN_BAN, false));

		Rect wi_rect;
		wi_rect.left   = pt.x;
		wi_rect.right  = pt.x;
		wi_rect.top    = pt.y;
		wi_rect.bottom = pt.y;

		w->dd_client_id = client_id;
		ShowDropDownListAt(w, std::move(list), -1, WID_CL_MATRIX, wi_rect, COLOUR_GREY, true, true);
	}

	/**
	 * Admin button on a Company is clicked.
	 * @param w The instance of this window.
	 * @param pt The point where this button was clicked.
	 * @param company_id The company this button was assigned to.
	 */
	static void OnClickCompanyAdmin(NetworkClientListWindow *w, Point pt, CompanyID company_id)
	{
		DropDownList list;
		list.emplace_back(new DropDownListStringItem(STR_NETWORK_CLIENT_LIST_ADMIN_COMPANY_RESET, DD_COMPANY_ADMIN_RESET, NetworkCompanyHasClients(company_id)));
		list.emplace_back(new DropDownListStringItem(STR_NETWORK_CLIENT_LIST_ADMIN_COMPANY_UNLOCK, DD_COMPANY_ADMIN_UNLOCK, !NetworkCompanyIsPassworded(company_id)));

		Rect wi_rect;
		wi_rect.left   = pt.x;
		wi_rect.right  = pt.x;
		wi_rect.top    = pt.y;
		wi_rect.bottom = pt.y;

		w->dd_company_id = company_id;
		ShowDropDownListAt(w, std::move(list), -1, WID_CL_MATRIX, wi_rect, COLOUR_GREY, true, true);
	}
	/**
	 * Chat button on a Client is clicked.
	 * @param w The instance of this window.
	 * @param pt The point where this button was clicked.
	 * @param client_id The client this button was assigned to.
	 */
	static void OnClickClientChat(NetworkClientListWindow *w, Point pt, ClientID client_id)
	{
		ShowNetworkChatQueryWindow(DESTTYPE_CLIENT, client_id);
	}

	/**
	 * Part of RebuildList() to create the information for a single company.
	 * @param company_id The company to build the list for.
	 * @param own_ci The NetworkClientInfo of the client itself.
	 */
	void RebuildListCompany(CompanyID company_id, const NetworkClientInfo *own_ci)
	{
		ButtonCommon *chat_button = new CompanyButton(SPR_CHAT, company_id == COMPANY_SPECTATOR ? STR_NETWORK_CLIENT_LIST_CHAT_SPECTATOR_TOOLTIP : STR_NETWORK_CLIENT_LIST_CHAT_COMPANY_TOOLTIP, COLOUR_ORANGE, company_id, &NetworkClientListWindow::OnClickCompanyChat);

		if (_network_server) this->buttons[line_count].emplace_back(new CompanyButton(SPR_ADMIN, STR_NETWORK_CLIENT_LIST_ADMIN_COMPANY_TOOLTIP, COLOUR_RED, company_id, &NetworkClientListWindow::OnClickCompanyAdmin, company_id == COMPANY_SPECTATOR));
		this->buttons[line_count].emplace_back(chat_button);
		if (own_ci->client_playas != company_id) this->buttons[line_count].emplace_back(new CompanyButton(SPR_JOIN, STR_NETWORK_CLIENT_LIST_JOIN_TOOLTIP, COLOUR_ORANGE, company_id, &NetworkClientListWindow::OnClickCompanyJoin, company_id != COMPANY_SPECTATOR && Company::Get(company_id)->is_ai));

		this->line_count += 1;

		bool has_players = false;
		for (const NetworkClientInfo *ci : NetworkClientInfo::Iterate()) {
			if (ci->client_playas != company_id) continue;
			has_players = true;

			if (_network_server) this->buttons[line_count].emplace_back(new ClientButton(SPR_ADMIN, STR_NETWORK_CLIENT_LIST_ADMIN_CLIENT_TOOLTIP, COLOUR_RED, ci->client_id, &NetworkClientListWindow::OnClickClientAdmin, _network_own_client_id == ci->client_id));
			if (_network_own_client_id != ci->client_id) this->buttons[line_count].emplace_back(new ClientButton(SPR_CHAT, STR_NETWORK_CLIENT_LIST_CHAT_CLIENT_TOOLTIP, COLOUR_ORANGE, ci->client_id, &NetworkClientListWindow::OnClickClientChat));

			if (ci->client_id == _network_own_client_id) {
				this->player_self_index = this->line_count;
			} else if (ci->client_id == CLIENT_ID_SERVER) {
				this->player_host_index = this->line_count;
			}

			this->line_count += 1;
		}

		/* Disable the chat button when there are players in this company. */
		chat_button->disabled = !has_players;
	}

	/**
	 * Rebuild the list, meaning: calculate the lines needed and what buttons go on which line.
	 */
	void RebuildList()
	{
		const NetworkClientInfo *own_ci = NetworkClientInfo::GetByClientID(_network_own_client_id);

		this->buttons.clear();
		this->line_count = 0;
		this->player_host_index = -1;
		this->player_self_index = -1;

		/* As spectator, show a line to create a new company. */
		if (own_ci->client_playas == COMPANY_SPECTATOR && !NetworkMaxCompaniesReached()) {
			this->buttons[line_count].emplace_back(new CompanyButton(SPR_JOIN, STR_NETWORK_CLIENT_LIST_NEW_COMPANY_TOOLTIP, COLOUR_ORANGE, COMPANY_SPECTATOR, &NetworkClientListWindow::OnClickCompanyNew));
			this->line_count += 1;
		}

		if (own_ci->client_playas != COMPANY_SPECTATOR) {
			this->RebuildListCompany(own_ci->client_playas, own_ci);
		}

		/* Companies */
		for (const Company *c : Company::Iterate()) {
			if (c->index == own_ci->client_playas) continue;

			this->RebuildListCompany(c->index, own_ci);
		}

		/* Spectators */
		this->RebuildListCompany(COMPANY_SPECTATOR, own_ci);

		this->vscroll->SetCount(this->line_count);
	}

	/**
	 * Get the button at a specific point on the WID_CL_MATRIX.
	 * @param pt The point to look for a button.
	 * @return The button or a nullptr if there was none.
	 */
	ButtonCommon *GetButtonAtPoint(Point pt)
	{
		uint index = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_CL_MATRIX);
		NWidgetBase *widget_matrix = this->GetWidget<NWidgetBase>(WID_CL_MATRIX);

		bool rtl = _current_text_dir == TD_RTL;
		uint x = rtl ? (uint)widget_matrix->pos_x + WD_FRAMERECT_LEFT : widget_matrix->current_x - WD_FRAMERECT_RIGHT;

		/* Find the buttons for this row. */
		auto button_find = this->buttons.find(index);
		if (button_find == this->buttons.end()) return nullptr;

		/* Check if we want to display a tooltip for any of the buttons. */
		for (auto &button : button_find->second) {
			uint left = rtl ? x : x - button->width;
			uint right = rtl ? x + button->width : x;

			if (IsInsideMM(pt.x, left, right)) {
				return button.get();
			}

			int width = button->width + WD_FRAMERECT_LEFT + WD_FRAMERECT_RIGHT;
			x += rtl ? width : -width;
		}

		return nullptr;
	}

public:
	NetworkClientListWindow(WindowDesc *desc, WindowNumber window_number) :
			Window(desc),
			hover_index(-1),
			player_self_index(-1),
			player_host_index(-1)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_CL_SCROLLBAR);
		this->OnInvalidateData();
		this->FinishInitNested(window_number);
	}

	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		this->RebuildList();

		/* Currently server information is not sync'd to clients, so we cannot show it on clients. */
		this->GetWidget<NWidgetStacked>(WID_CL_SERVER_SELECTOR)->SetDisplayedPlane(_network_server ? 0 : SZSP_HORIZONTAL);
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_CL_SERVER_VISIBILITY:
				*size = maxdim(maxdim(GetStringBoundingBox(STR_NETWORK_SERVER_VISIBILITY_LOCAL), GetStringBoundingBox(STR_NETWORK_SERVER_VISIBILITY_PUBLIC)), GetStringBoundingBox(STR_NETWORK_SERVER_VISIBILITY_INVITE_ONLY));
				size->width += padding.width;
				size->height += padding.height;
				break;

			case WID_CL_MATRIX: {
				uint height = std::max({GetSpriteSize(SPR_COMPANY_ICON).height, GetSpriteSize(SPR_JOIN).height, GetSpriteSize(SPR_ADMIN).height, GetSpriteSize(SPR_CHAT).height});
				height += ScaleGUITrad(WD_FRAMERECT_TOP) + ScaleGUITrad(WD_FRAMERECT_BOTTOM);
				this->line_height = std::max(height, (uint)FONT_HEIGHT_NORMAL) + ScaleGUITrad(WD_MATRIX_TOP + WD_MATRIX_BOTTOM);

				resize->width = 1;
				resize->height = this->line_height;
				fill->height = this->line_height;
				size->height = std::max(size->height, 5 * this->line_height);
				break;
			}
		}
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_CL_MATRIX);
	}

	void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_CL_SERVER_NAME:
				SetDParamStr(0, _settings_client.network.server_name);
				break;

			case WID_CL_SERVER_VISIBILITY:
				SetDParam(0, STR_NETWORK_SERVER_VISIBILITY_LOCAL + _settings_client.network.server_game_type);
				break;

			case WID_CL_SERVER_JOIN_KEY: {
				static std::string empty = {};
				SetDParamStr(0, _network_server_connection_type == CONNECTION_TYPE_UNKNOWN ? empty : _network_game_info.join_key);
				break;
			}

			case WID_CL_SERVER_CONNECTION_TYPE:
				SetDParam(0, STR_NETWORK_CLIENT_LIST_SERVER_CONNECTION_TYPE_UNKNOWN + _network_server_connection_type);
				break;

			case WID_CL_CLIENT_NAME:
				SetDParamStr(0, _settings_client.network.client_name);
				break;

			case WID_CL_CLIENT_COMPANY_COUNT:
				SetDParam(0, NetworkClientInfo::GetNumItems());
				SetDParam(1, Company::GetNumItems());
				break;
		}
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_CL_SERVER_NAME_EDIT:
				if (!_network_server) break;

				this->query_widget = WID_CL_SERVER_NAME_EDIT;
				SetDParamStr(0, _settings_client.network.server_name);
				ShowQueryString(STR_JUST_RAW_STRING, STR_NETWORK_CLIENT_LIST_SERVER_NAME_QUERY_CAPTION, NETWORK_NAME_LENGTH, this, CS_ALPHANUMERAL, QSF_LEN_IN_CHARS);
				break;

			case WID_CL_CLIENT_NAME_EDIT:
				this->query_widget = WID_CL_CLIENT_NAME_EDIT;
				SetDParamStr(0, _settings_client.network.client_name);
				ShowQueryString(STR_JUST_RAW_STRING, STR_NETWORK_CLIENT_LIST_PLAYER_NAME_QUERY_CAPTION, NETWORK_CLIENT_NAME_LENGTH, this, CS_ALPHANUMERAL, QSF_LEN_IN_CHARS);
				break;

			case WID_CL_SERVER_VISIBILITY:
				if (!_network_server) break;

				ShowDropDownList(this, BuildVisibilityDropDownList(), _settings_client.network.server_game_type, WID_CL_SERVER_VISIBILITY);
				break;

			case WID_CL_MATRIX: {
				ButtonCommon *button = this->GetButtonAtPoint(pt);
				if (button == nullptr) break;

				button->OnClick(this, pt);
				break;
			}
		}
	}

	bool OnTooltip(Point pt, int widget, TooltipCloseCondition close_cond) override
	{
		switch (widget) {
			case WID_CL_MATRIX: {
				int index = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_CL_MATRIX);

				bool rtl = _current_text_dir == TD_RTL;
				NWidgetBase *widget_matrix = this->GetWidget<NWidgetBase>(WID_CL_MATRIX);

				Dimension d = GetSpriteSize(SPR_COMPANY_ICON);
				uint text_left = widget_matrix->pos_x + (rtl ? (uint)WD_FRAMERECT_LEFT : d.width + 8);
				uint text_right = widget_matrix->pos_x + widget_matrix->current_x - (rtl ? d.width + 8 : (uint)WD_FRAMERECT_RIGHT);

				Dimension d2 = GetSpriteSize(SPR_PLAYER_SELF);
				uint offset_x = CLIENT_OFFSET_LEFT - d2.width - 3;

				uint player_icon_x = rtl ? text_right - offset_x - d2.width : text_left + offset_x;

				if (IsInsideMM(pt.x, player_icon_x, player_icon_x + d2.width)) {
					if (index == this->player_self_index) {
						GuiShowTooltips(this, STR_NETWORK_CLIENT_LIST_PLAYER_ICON_SELF_TOOLTIP, 0, nullptr, close_cond);
						return true;
					} else if (index == this->player_host_index) {
						GuiShowTooltips(this, STR_NETWORK_CLIENT_LIST_PLAYER_ICON_HOST_TOOLTIP, 0, nullptr, close_cond);
						return true;
					}
				}

				ButtonCommon *button = this->GetButtonAtPoint(pt);
				if (button == nullptr) return false;

				GuiShowTooltips(this, button->tooltip, 0, nullptr, close_cond);
				return true;
			};
		}

		return false;
	}

	void OnDropdownClose(Point pt, int widget, int index, bool instant_close) override
	{
		/* If you close the dropdown outside the list, don't take any action. */
		if (widget == WID_CL_MATRIX) return;

		Window::OnDropdownClose(pt, widget, index, instant_close);
	}

	void OnDropdownSelect(int widget, int index) override
	{
		switch (widget) {
			case WID_CL_SERVER_VISIBILITY:
				if (!_network_server) break;

				_settings_client.network.server_game_type = (ServerGameType)index;
				NetworkUpdateServerGameType();
				break;

			case WID_CL_MATRIX: {
				StringID text = STR_NULL;
				QueryCallbackProc *callback = nullptr;

				switch (index) {
					case DD_CLIENT_ADMIN_KICK:
						_admin_client_id = this->dd_client_id;
						text = STR_NETWORK_CLIENT_LIST_ASK_CLIENT_KICK;
						callback = AdminClientKickCallback;
						SetDParamStr(0, NetworkClientInfo::GetByClientID(_admin_client_id)->client_name);
						break;

					case DD_CLIENT_ADMIN_BAN:
						_admin_client_id = this->dd_client_id;
						text = STR_NETWORK_CLIENT_LIST_ASK_CLIENT_BAN;
						callback = AdminClientBanCallback;
						SetDParamStr(0, NetworkClientInfo::GetByClientID(_admin_client_id)->client_name);
						break;

					case DD_COMPANY_ADMIN_RESET:
						_admin_company_id = this->dd_company_id;
						text = STR_NETWORK_CLIENT_LIST_ASK_COMPANY_RESET;
						callback = AdminCompanyResetCallback;
						SetDParam(0, _admin_company_id);
						break;

					case DD_COMPANY_ADMIN_UNLOCK:
						_admin_company_id = this->dd_company_id;
						text = STR_NETWORK_CLIENT_LIST_ASK_COMPANY_UNLOCK;
						callback = AdminCompanyUnlockCallback;
						SetDParam(0, _admin_company_id);
						break;

					default:
						NOT_REACHED();
				}

				assert(text != STR_NULL);
				assert(callback != nullptr);

				/* Always ask confirmation for all admin actions. */
				ShowQuery(STR_NETWORK_CLIENT_LIST_ASK_CAPTION, text, this, callback);

				break;
			}

			default:
				NOT_REACHED();
		}

		this->SetDirty();
	}

	void OnQueryTextFinished(char *str) override
	{
		if (str == nullptr) return;

		switch (this->query_widget) {
			default: NOT_REACHED();

			case WID_CL_SERVER_NAME_EDIT: {
				if (!_network_server) break;

				SetSettingValue(GetSettingFromName("network.server_name")->AsStringSetting(), str);
				this->InvalidateData();
				break;
			}

			case WID_CL_CLIENT_NAME_EDIT: {
				SetSettingValue(GetSettingFromName("network.client_name")->AsStringSetting(), str);
				this->InvalidateData();
				break;
			}

			case WID_CL_COMPANY_JOIN:
				NetworkClientRequestMove(this->join_company, str);
				break;
		}
	}

	/**
	 * Draw the buttons for a single line in the matrix.
	 *
	 * The x-position in RTL is the most left or otherwise the most right pixel
	 * we can draw the buttons from.
	 *
	 * @param x The x-position to start with the buttons. Updated during this function.
	 * @param y The y-position to start with the buttons.
	 * @param buttons The buttons to draw.
	 */
	void DrawButtons(uint &x, uint y, const std::vector<std::unique_ptr<ButtonCommon>> &buttons) const
	{
		for (auto &button : buttons) {
			bool rtl = _current_text_dir == TD_RTL;

			uint left = rtl ? x : x - button->width;
			uint right = rtl ? x + button->width : x;

			int offset = std::max(0, ((int)(this->line_height + 1) - (int)button->height) / 2);

			DrawFrameRect(left, y + offset, right, y + offset + button->height, button->colour, FR_NONE);
			DrawSprite(button->sprite, PAL_NONE, left + ScaleGUITrad(WD_FRAMERECT_LEFT), y + offset + ScaleGUITrad(WD_FRAMERECT_TOP));
			if (button->disabled) {
				GfxFillRect(left + 1, y + offset + 1, right - 1, y + offset + button->height - 1, _colour_gradient[button->colour & 0xF][2], FILLRECT_CHECKER);
			}

			int width = button->width + WD_FRAMERECT_LEFT + WD_FRAMERECT_RIGHT;
			x += rtl ? width : -width;
		}
	}

	/**
	 * Draw a company and its clients on the matrix.
	 * @param company_id The company to draw.
	 * @param left The most left pixel of the line.
	 * @param right The most right pixel of the line.
	 * @param top The top of the first line.
	 * @param line The Nth line we are drawing. Updated during this function.
	 */
	void DrawCompany(CompanyID company_id, uint left, uint right, uint top, uint &line) const
	{
		bool rtl = _current_text_dir == TD_RTL;
		int text_y_offset = std::max(0, ((int)(this->line_height + 1) - (int)FONT_HEIGHT_NORMAL) / 2) + WD_MATRIX_BOTTOM;

		Dimension d = GetSpriteSize(SPR_COMPANY_ICON);
		int offset = std::max(0, ((int)(this->line_height + 1) - (int)d.height) / 2);

		uint text_left = left + (rtl ? (uint)WD_FRAMERECT_LEFT : d.width + 8);
		uint text_right = right - (rtl ? d.width + 8 : (uint)WD_FRAMERECT_RIGHT);

		uint line_start = this->vscroll->GetPosition();
		uint line_end = line_start + this->vscroll->GetCapacity();

		uint y = top + (this->line_height * (line - line_start));

		/* Draw the company line (if in range of scrollbar). */
		if (IsInsideMM(line, line_start, line_end)) {
			uint x = rtl ? text_left : text_right;

			/* If there are buttons for this company, draw them. */
			auto button_find = this->buttons.find(line);
			if (button_find != this->buttons.end()) {
				this->DrawButtons(x, y, button_find->second);
			}

			if (company_id == COMPANY_SPECTATOR) {
				DrawSprite(SPR_COMPANY_ICON, PALETTE_TO_GREY, rtl ? right - d.width - 4 : left + 4, y + offset);
				DrawString(rtl ? x : text_left, rtl ? text_right : x, y + text_y_offset, STR_NETWORK_CLIENT_LIST_SPECTATORS, TC_SILVER);
			} else if (company_id == COMPANY_NEW_COMPANY) {
				DrawSprite(SPR_COMPANY_ICON, PALETTE_TO_GREY, rtl ? right - d.width - 4 : left + 4, y + offset);
				DrawString(rtl ? x : text_left, rtl ? text_right : x, y + text_y_offset, STR_NETWORK_CLIENT_LIST_NEW_COMPANY, TC_WHITE);
			} else {
				DrawCompanyIcon(company_id, rtl ? right - d.width - 4 : left + 4, y + offset);

				SetDParam(0, company_id);
				SetDParam(1, company_id);
				DrawString(rtl ? x : text_left, rtl ? text_right : x, y + text_y_offset, STR_COMPANY_NAME, TC_SILVER);
			}
		}

		y += this->line_height;
		line++;

		for (const NetworkClientInfo *ci : NetworkClientInfo::Iterate()) {
			if (ci->client_playas != company_id) continue;

			/* Draw the player line (if in range of scrollbar). */
			if (IsInsideMM(line, line_start, line_end)) {
				uint x = rtl ? text_left : text_right;

				/* If there are buttons for this client, draw them. */
				auto button_find = this->buttons.find(line);
				if (button_find != this->buttons.end()) {
					this->DrawButtons(x, y, button_find->second);
				}

				SpriteID player_icon = 0;
				if (ci->client_id == _network_own_client_id) {
					player_icon = SPR_PLAYER_SELF;
				} else if (ci->client_id == CLIENT_ID_SERVER) {
					player_icon = SPR_PLAYER_HOST;
				}

				if (player_icon != 0) {
					Dimension d2 = GetSpriteSize(player_icon);
					uint offset_x = CLIENT_OFFSET_LEFT - 3;
					int offset_y = std::max(0, ((int)(this->line_height + 1) - (int)d2.height) / 2);
					DrawSprite(player_icon, PALETTE_TO_GREY, rtl ? text_right - offset_x : text_left + offset_x - d2.width, y + offset_y);
				}

				SetDParamStr(0, ci->client_name);
				DrawString(rtl ? x : text_left + CLIENT_OFFSET_LEFT, rtl ? text_right - CLIENT_OFFSET_LEFT : x, y + text_y_offset, STR_JUST_RAW_STRING, TC_BLACK);
			}

			y += this->line_height;
			line++;
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_CL_MATRIX: {
				uint line = 0;

				if (this->hover_index >= 0) {
					uint offset = this->hover_index * this->line_height;
					GfxFillRect(r.left + 2, r.top + offset, r.right - 1, r.top + offset + this->line_height - 2, GREY_SCALE(9));
				}

				NetworkClientInfo *own_ci = NetworkClientInfo::GetByClientID(_network_own_client_id);
				if (own_ci->client_playas == COMPANY_SPECTATOR && !NetworkMaxCompaniesReached()) {
					this->DrawCompany(COMPANY_NEW_COMPANY, r.left, r.right, r.top, line);
				}

				if (own_ci->client_playas != COMPANY_SPECTATOR) {
					this->DrawCompany(own_ci->client_playas, r.left, r.right, r.top, line);
				}

				for (const Company *c : Company::Iterate()) {
					if (own_ci->client_playas == c->index) continue;
					this->DrawCompany(c->index, r.left, r.right, r.top, line);
				}

				/* Specators */
				this->DrawCompany(COMPANY_SPECTATOR, r.left, r.right, r.top, line);

				break;
			}
		}
	}

	virtual void OnMouseLoop() override
	{
		if (GetWidgetFromPos(this, _cursor.pos.x - this->left, _cursor.pos.y - this->top) != WID_CL_MATRIX) {
			this->hover_index = -1;
			this->SetDirty();
			return;
		}

		NWidgetBase *nwi = this->GetWidget<NWidgetBase>(WID_CL_MATRIX);
		int y = _cursor.pos.y - this->top - nwi->pos_y - 2;
		int index = y / this->line_height;

		if (index != this->hover_index) {
			this->hover_index = index;
			this->SetDirty();
		}
	}
};

void ShowClientList()
{
	AllocateWindowDescFront<NetworkClientListWindow>(&_client_list_desc, 0);
}

NetworkJoinStatus _network_join_status; ///< The status of joining.
uint8 _network_join_waiting;            ///< The number of clients waiting in front of us.
uint32 _network_join_bytes;             ///< The number of bytes we already downloaded.
uint32 _network_join_bytes_total;       ///< The total number of bytes to download.

struct NetworkJoinStatusWindow : Window {
	NetworkPasswordType password_type;

	NetworkJoinStatusWindow(WindowDesc *desc) : Window(desc)
	{
		this->parent = FindWindowById(WC_NETWORK_WINDOW, WN_NETWORK_WINDOW_GAME);
		this->InitNested(WN_NETWORK_STATUS_WINDOW_JOIN);
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		if (widget != WID_NJS_BACKGROUND) return;

		uint8 progress; // used for progress bar
		DrawString(r.left + 2, r.right - 2, r.top + 20, STR_NETWORK_CONNECTING_1 + _network_join_status, TC_FROMSTRING, SA_HOR_CENTER);
		switch (_network_join_status) {
			case NETWORK_JOIN_STATUS_CONNECTING: case NETWORK_JOIN_STATUS_AUTHORIZING:
			case NETWORK_JOIN_STATUS_GETTING_COMPANY_INFO:
				progress = 10; // first two stages 10%
				break;
			case NETWORK_JOIN_STATUS_WAITING:
				SetDParam(0, _network_join_waiting);
				DrawString(r.left + 2, r.right - 2, r.top + 20 + FONT_HEIGHT_NORMAL, STR_NETWORK_CONNECTING_WAITING, TC_FROMSTRING, SA_HOR_CENTER);
				progress = 15; // third stage is 15%
				break;
			case NETWORK_JOIN_STATUS_DOWNLOADING:
				SetDParam(0, _network_join_bytes);
				SetDParam(1, _network_join_bytes_total);
				DrawString(r.left + 2, r.right - 2, r.top + 20 + FONT_HEIGHT_NORMAL, _network_join_bytes_total == 0 ? STR_NETWORK_CONNECTING_DOWNLOADING_1 : STR_NETWORK_CONNECTING_DOWNLOADING_2, TC_FROMSTRING, SA_HOR_CENTER);
				if (_network_join_bytes_total == 0) {
					progress = 15; // We don't have the final size yet; the server is still compressing!
					break;
				}
				FALLTHROUGH;

			default: // Waiting is 15%, so the resting receivement of map is maximum 70%
				progress = 15 + _network_join_bytes * (100 - 15) / _network_join_bytes_total;
		}

		/* Draw nice progress bar :) */
		DrawFrameRect(r.left + 20, r.top + 5, (int)((this->width - 20) * progress / 100), r.top + 15, COLOUR_MAUVE, FR_NONE);
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		if (widget != WID_NJS_BACKGROUND) return;

		size->height = 25 + 2 * FONT_HEIGHT_NORMAL;

		/* Account for the statuses */
		uint width = 0;
		for (uint i = 0; i < NETWORK_JOIN_STATUS_END; i++) {
			width = std::max(width, GetStringBoundingBox(STR_NETWORK_CONNECTING_1 + i).width);
		}

		/* For the number of waiting (other) players */
		SetDParamMaxValue(0, MAX_CLIENTS);
		width = std::max(width, GetStringBoundingBox(STR_NETWORK_CONNECTING_WAITING).width);

		/* Account for downloading ~ 10 MiB */
		SetDParamMaxDigits(0, 8);
		SetDParamMaxDigits(1, 8);
		width = std::max(width, GetStringBoundingBox(STR_NETWORK_CONNECTING_DOWNLOADING_1).width);
		width = std::max(width, GetStringBoundingBox(STR_NETWORK_CONNECTING_DOWNLOADING_2).width);

		/* Give a bit more clearing for the widest strings than strictly needed */
		size->width = width + WD_FRAMERECT_LEFT + WD_FRAMERECT_BOTTOM + 10;
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		if (widget == WID_NJS_CANCELOK) { // Disconnect button
			NetworkDisconnect();
			SwitchToMode(SM_MENU);
			ShowNetworkGameWindow();
		}
	}

	void OnQueryTextFinished(char *str) override
	{
		if (StrEmpty(str)) {
			NetworkDisconnect();
			ShowNetworkGameWindow();
			return;
		}

		switch (this->password_type) {
			case NETWORK_GAME_PASSWORD:    MyClient::SendGamePassword   (str); break;
			case NETWORK_COMPANY_PASSWORD: MyClient::SendCompanyPassword(str); break;
			default: NOT_REACHED();
		}
	}
};

static const NWidgetPart _nested_network_join_status_window_widgets[] = {
	NWidget(WWT_CAPTION, COLOUR_GREY), SetDataTip(STR_NETWORK_CONNECTING_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	NWidget(WWT_PANEL, COLOUR_GREY),
		NWidget(WWT_EMPTY, COLOUR_GREY, WID_NJS_BACKGROUND),
		NWidget(NWID_HORIZONTAL),
			NWidget(NWID_SPACER), SetMinimalSize(75, 0), SetFill(1, 0),
			NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NJS_CANCELOK), SetMinimalSize(101, 12), SetDataTip(STR_NETWORK_CONNECTION_DISCONNECT, STR_NULL),
			NWidget(NWID_SPACER), SetMinimalSize(75, 0), SetFill(1, 0),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 4),
	EndContainer(),
};

static WindowDesc _network_join_status_window_desc(
	WDP_CENTER, nullptr, 0, 0,
	WC_NETWORK_STATUS_WINDOW, WC_NONE,
	WDF_MODAL,
	_nested_network_join_status_window_widgets, lengthof(_nested_network_join_status_window_widgets)
);

void ShowJoinStatusWindow()
{
	CloseWindowById(WC_NETWORK_STATUS_WINDOW, WN_NETWORK_STATUS_WINDOW_JOIN);
	new NetworkJoinStatusWindow(&_network_join_status_window_desc);
}

void ShowNetworkNeedPassword(NetworkPasswordType npt)
{
	NetworkJoinStatusWindow *w = (NetworkJoinStatusWindow *)FindWindowById(WC_NETWORK_STATUS_WINDOW, WN_NETWORK_STATUS_WINDOW_JOIN);
	if (w == nullptr) return;
	w->password_type = npt;

	StringID caption;
	switch (npt) {
		default: NOT_REACHED();
		case NETWORK_GAME_PASSWORD:    caption = STR_NETWORK_NEED_GAME_PASSWORD_CAPTION; break;
		case NETWORK_COMPANY_PASSWORD: caption = STR_NETWORK_NEED_COMPANY_PASSWORD_CAPTION; break;
	}
	ShowQueryString(STR_EMPTY, caption, NETWORK_PASSWORD_LENGTH, w, CS_ALPHANUMERAL, QSF_PASSWORD);
}

struct NetworkCompanyPasswordWindow : public Window {
	QueryString password_editbox; ///< Password editbox.
	Dimension warning_size;       ///< How much space to use for the warning text

	NetworkCompanyPasswordWindow(WindowDesc *desc, Window *parent) : Window(desc), password_editbox(lengthof(_settings_client.network.default_company_pass))
	{
		this->InitNested(0);
		this->UpdateWarningStringSize();

		this->parent = parent;
		this->querystrings[WID_NCP_PASSWORD] = &this->password_editbox;
		this->password_editbox.cancel_button = WID_NCP_CANCEL;
		this->password_editbox.ok_button = WID_NCP_OK;
		this->SetFocusedWidget(WID_NCP_PASSWORD);
	}

	void UpdateWarningStringSize()
	{
		assert(this->nested_root->smallest_x > 0);
		this->warning_size.width = this->nested_root->current_x - (WD_FRAMETEXT_LEFT + WD_FRAMETEXT_RIGHT + WD_FRAMERECT_LEFT + WD_FRAMERECT_RIGHT);
		this->warning_size.height = GetStringHeight(STR_WARNING_PASSWORD_SECURITY, this->warning_size.width);
		this->warning_size.height += WD_FRAMETEXT_TOP + WD_FRAMETEXT_BOTTOM + WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM;

		this->ReInit();
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		if (widget == WID_NCP_WARNING) {
			*size = this->warning_size;
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		if (widget != WID_NCP_WARNING) return;

		DrawStringMultiLine(r.left + WD_FRAMETEXT_LEFT, r.right - WD_FRAMETEXT_RIGHT,
			r.top + WD_FRAMERECT_TOP, r.bottom - WD_FRAMERECT_BOTTOM,
			STR_WARNING_PASSWORD_SECURITY, TC_FROMSTRING, SA_CENTER);
	}

	void OnOk()
	{
		if (this->IsWidgetLowered(WID_NCP_SAVE_AS_DEFAULT_PASSWORD)) {
			_settings_client.network.default_company_pass = this->password_editbox.text.buf;
		}

		NetworkChangeCompanyPassword(_local_company, this->password_editbox.text.buf);
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_NCP_OK:
				this->OnOk();
				FALLTHROUGH;

			case WID_NCP_CANCEL:
				this->Close();
				break;

			case WID_NCP_SAVE_AS_DEFAULT_PASSWORD:
				this->ToggleWidgetLoweredState(WID_NCP_SAVE_AS_DEFAULT_PASSWORD);
				this->SetDirty();
				break;
		}
	}
};

static const NWidgetPart _nested_network_company_password_window_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY), SetDataTip(STR_COMPANY_PASSWORD_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY, WID_NCP_BACKGROUND),
		NWidget(NWID_VERTICAL), SetPIP(5, 5, 5),
			NWidget(NWID_HORIZONTAL), SetPIP(5, 5, 5),
				NWidget(WWT_TEXT, COLOUR_GREY, WID_NCP_LABEL), SetDataTip(STR_COMPANY_VIEW_PASSWORD, STR_NULL),
				NWidget(WWT_EDITBOX, COLOUR_GREY, WID_NCP_PASSWORD), SetFill(1, 0), SetMinimalSize(194, 12), SetDataTip(STR_COMPANY_VIEW_SET_PASSWORD, STR_NULL),
			EndContainer(),
			NWidget(NWID_HORIZONTAL), SetPIP(5, 0, 5),
				NWidget(NWID_SPACER), SetFill(1, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_NCP_SAVE_AS_DEFAULT_PASSWORD), SetMinimalSize(194, 12),
											SetDataTip(STR_COMPANY_PASSWORD_MAKE_DEFAULT, STR_COMPANY_PASSWORD_MAKE_DEFAULT_TOOLTIP),
			EndContainer(),
		EndContainer(),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY, WID_NCP_WARNING), EndContainer(),
	NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_NCP_CANCEL), SetFill(1, 0), SetDataTip(STR_BUTTON_CANCEL, STR_COMPANY_PASSWORD_CANCEL),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_NCP_OK), SetFill(1, 0), SetDataTip(STR_BUTTON_OK, STR_COMPANY_PASSWORD_OK),
	EndContainer(),
};

static WindowDesc _network_company_password_window_desc(
	WDP_AUTO, nullptr, 0, 0,
	WC_COMPANY_PASSWORD_WINDOW, WC_NONE,
	0,
	_nested_network_company_password_window_widgets, lengthof(_nested_network_company_password_window_widgets)
);

void ShowNetworkCompanyPasswordWindow(Window *parent)
{
	CloseWindowById(WC_COMPANY_PASSWORD_WINDOW, 0);

	new NetworkCompanyPasswordWindow(&_network_company_password_window_desc, parent);
}
