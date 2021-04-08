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
#include "../gui.h"
#include "network_udp.h"
#include "../window_func.h"
#include "../gfx_func.h"
#include "../widgets/dropdown_func.h"
#include "../querystring_gui.h"
#include "../sortlist_type.h"
#include "../company_func.h"
#include "../core/geometry_func.hpp"
#include "../genworld.h"
#include "../map_type.h"
#include "../guitimer_func.h"
#include "../zoom_func.h"
#include "social_presence.h"
#include "../error.h"

#include "../widgets/network_widget.h"

#include "table/strings.h"
#include "../table/sprites.h"

#include "../stringfilter_type.h"

#include "../safeguards.h"

#ifdef __EMSCRIPTEN__
#	include <emscripten.h>
#endif

static void ShowNetworkStartServerWindow();
static void ShowNetworkLobbyWindow(NetworkGameList *ngl);

/**
 * Advertisement options in the start server window
 */
static const StringID _connection_types_dropdown[] = {
	STR_NETWORK_START_SERVER_UNADVERTISED,
	STR_NETWORK_START_SERVER_ADVERTISED,
	INVALID_STRING_ID
};

static std::vector<StringID> _language_dropdown;

void SortNetworkLanguages()
{
	/* Init the strings */
	if (_language_dropdown.empty()) {
		for (int i = 0; i < NETLANG_COUNT; i++) _language_dropdown.emplace_back(STR_NETWORK_LANG_ANY + i);
		_language_dropdown.emplace_back(INVALID_STRING_ID);
	}

	/* Sort the strings (we don't move 'any' and the 'invalid' one) */
	std::sort(_language_dropdown.begin() + 1, _language_dropdown.end() - 1, StringIDSorter);
}

/**
 * Update the network new window because a new server is
 * found on the network.
 */
void UpdateNetworkGameWindow()
{
	InvalidateWindowData(WC_NETWORK_WINDOW, WN_NETWORK_WINDOW_GAME, 0);
}

typedef GUIList<NetworkGameList*, StringFilter&> GUIGameServerList;
typedef uint16 ServerListPosition;
static const ServerListPosition SLP_INVALID = 0xFFFF;

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
		leaf->SetMinimalSize(14 + GetSpriteSize(SPR_LOCK).width + GetSpriteSize(SPR_BLOT).width + GetSpriteSize(SPR_FLAGS_BASE).width, 12);
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

		for (NetworkGameList *ngl = _network_game_list; ngl != nullptr; ngl = ngl->next) {
			this->servers.push_back(ngl);
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
		int r = strnatcmp(a->info.server_name, b->info.server_name, true); // Sort by name (natural sorting).
		return r == 0 ? a->address.CompareTo(b->address) < 0: r < 0;
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
		int r = StrEmpty(a->info.server_revision) - StrEmpty(b->info.server_revision);

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
		sf.AddLine((*item)->info.server_name);
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

			/* draw flag according to server language */
			DrawSprite(SPR_FLAGS_BASE + cur_item->info.server_lang, PAL_NONE, nwi_info->pos_x + this->flag_offset, y + (this->resize.step_height - GetSpriteSize(SPR_FLAGS_BASE + cur_item->info.server_lang).height) / 2);
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
	NetworkGameWindow(WindowDesc *desc) : Window(desc), name_editbox(NETWORK_CLIENT_NAME_LENGTH), filter_editbox(120)
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
		this->name_editbox.text.Assign(_settings_client.network.client_name);

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

		this->last_joined = NetworkGameListAddItem(NetworkAddress(_settings_client.network.last_host, _settings_client.network.last_port));
		this->server = this->last_joined;
		if (this->last_joined != nullptr) NetworkUDPQueryServer(this->last_joined->address);

		this->requery_timer.SetInterval(MILLISECONDS_PER_TICK);

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
			DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, r.top + 6 + 8 + 2 * FONT_HEIGHT_NORMAL, sel->info.map_name, TC_BLACK, SA_HOR_CENTER); // map name

			uint16 y = r.top + detail_height + 4;

			SetDParam(0, sel->info.clients_on);
			SetDParam(1, sel->info.clients_max);
			SetDParam(2, sel->info.companies_on);
			SetDParam(3, sel->info.companies_max);
			DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_NETWORK_SERVER_LIST_CLIENTS);
			y += FONT_HEIGHT_NORMAL;

			SetDParam(0, STR_NETWORK_LANG_ANY + sel->info.server_lang);
			DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_NETWORK_SERVER_LIST_LANGUAGE); // server language
			y += FONT_HEIGHT_NORMAL;

			SetDParam(0, STR_CHEAT_SWITCH_CLIMATE_TEMPERATE_LANDSCAPE + sel->info.map_set);
			DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_NETWORK_SERVER_LIST_LANDSCAPE); // landscape
			y += FONT_HEIGHT_NORMAL;

			SetDParam(0, sel->info.map_width);
			SetDParam(1, sel->info.map_height);
			DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_NETWORK_SERVER_LIST_MAP_SIZE); // map size
			y += FONT_HEIGHT_NORMAL;

			SetDParamStr(0, sel->info.server_revision);
			DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_NETWORK_SERVER_LIST_SERVER_VERSION); // server version
			y += FONT_HEIGHT_NORMAL;

			char network_addr_buffer[NETWORK_HOSTNAME_LENGTH + 6 + 7];
			sel->address.GetAddressAsString(network_addr_buffer, lastof(network_addr_buffer));
			SetDParamStr(0, network_addr_buffer);
			DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_NETWORK_SERVER_LIST_SERVER_ADDRESS); // server address
			y += FONT_HEIGHT_NORMAL;

			SetDParam(0, sel->info.start_date);
			DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_NETWORK_SERVER_LIST_START_DATE); // start date
			y += FONT_HEIGHT_NORMAL;

			SetDParam(0, sel->info.game_date);
			DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_NETWORK_SERVER_LIST_CURRENT_DATE); // current date
			y += FONT_HEIGHT_NORMAL;

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
				DeleteWindowById(WC_NETWORK_WINDOW, WN_NETWORK_WINDOW_GAME);
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
				NetworkUDPQueryMasterServer();
				break;

			case WID_NG_SEARCH_LAN:
				NetworkUDPSearchGame();
				break;

			case WID_NG_ADD: // Add a server
				SetDParamStr(0, _settings_client.network.connect_to_ip);
				ShowQueryString(
					STR_JUST_RAW_STRING,
					STR_NETWORK_SERVER_LIST_ENTER_IP,
					NETWORK_HOSTNAME_LENGTH,  // maximum number of characters including '\0'
					this, CS_ALPHANUMERAL, QSF_ACCEPT_UNCHANGED);
				break;

			case WID_NG_START: // Start server
				ShowNetworkStartServerWindow();
				break;

			case WID_NG_JOIN: // Join Game
				if (this->server != nullptr) {
					seprintf(_settings_client.network.last_host, lastof(_settings_client.network.last_host), "%s", this->server->address.GetHostname());
					_settings_client.network.last_port = this->server->address.GetPort();
					ShowNetworkLobbyWindow(this->server);
				}
				break;

			case WID_NG_REFRESH: // Refresh
				if (this->server != nullptr) NetworkUDPQueryServer(this->server->address);
				break;

			case WID_NG_NEWGRF: // NewGRF Settings
				if (this->server != nullptr) ShowNewGRFSettings(false, false, false, &this->server->info.grfconfig);
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
		if (keycode == WKC_UP || keycode == WKC_DOWN || keycode == WKC_PAGEUP || keycode == WKC_PAGEDOWN || keycode == WKC_HOME || keycode == WKC_END) {
			if (this->servers.size() == 0) return ES_HANDLED;
			switch (keycode) {
				case WKC_UP:
					/* scroll up by one */
					if (this->list_pos == SLP_INVALID) return ES_HANDLED;
					if (this->list_pos > 0) this->list_pos--;
					break;
				case WKC_DOWN:
					/* scroll down by one */
					if (this->list_pos == SLP_INVALID) return ES_HANDLED;
					if (this->list_pos < this->servers.size() - 1) this->list_pos++;
					break;
				case WKC_PAGEUP:
					/* scroll up a page */
					if (this->list_pos == SLP_INVALID) return ES_HANDLED;
					this->list_pos = (this->list_pos < this->vscroll->GetCapacity()) ? 0 : this->list_pos - this->vscroll->GetCapacity();
					break;
				case WKC_PAGEDOWN:
					/* scroll down a page */
					if (this->list_pos == SLP_INVALID) return ES_HANDLED;
					this->list_pos = std::min(this->list_pos + this->vscroll->GetCapacity(), (int)this->servers.size() - 1);
					break;
				case WKC_HOME:
					/* jump to beginning */
					this->list_pos = 0;
					break;
				case WKC_END:
					/* jump to end */
					this->list_pos = (ServerListPosition)this->servers.size() - 1;
					break;
				default: NOT_REACHED();
			}

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
				/* Make sure the name does not start with a space, so TAB completion works */
				if (!StrEmpty(this->name_editbox.text.buf) && this->name_editbox.text.buf[0] != ' ') {
					strecpy(_settings_client.network.client_name, this->name_editbox.text.buf, lastof(_settings_client.network.client_name));
				} else {
					strecpy(_settings_client.network.client_name, "Player", lastof(_settings_client.network.client_name));
				}
				break;
		}
	}

	void OnQueryTextFinished(char *str) override
	{
		if (!StrEmpty(str)) NetworkAddServer(str);
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_NG_MATRIX);
	}

	void OnRealtimeTick(uint delta_ms) override
	{
		if (!this->requery_timer.Elapsed(delta_ms)) return;
		this->requery_timer.SetInterval(MILLISECONDS_PER_TICK);

		NetworkGameListRequery();
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
							NWidget(WWT_EMPTY, INVALID_COLOUR, WID_NG_DETAILS_SPACER), SetMinimalSize(140, 155), SetResize(0, 1), SetFill(1, 1), // Make sure it's at least this wide
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
	DeleteWindowById(WC_NETWORK_WINDOW, WN_NETWORK_WINDOW_LOBBY);
	DeleteWindowById(WC_NETWORK_WINDOW, WN_NETWORK_WINDOW_START);

	/* Only show once */
	if (first) {
		first = false;
		/* Add all servers from the config file to our list. */
		for (const auto &iter : _network_host_list) {
			NetworkAddServer(iter.c_str());
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
		this->name_editbox.text.Assign(_settings_client.network.server_name);

		this->SetFocusedWidget(WID_NSS_GAMENAME);
	}

	void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_NSS_CONNTYPE_BTN:
				SetDParam(0, _connection_types_dropdown[_settings_client.network.server_advertise]);
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

			case WID_NSS_LANGUAGE_BTN:
				SetDParam(0, STR_NETWORK_LANG_ANY + _settings_client.network.server_lang);
				break;
		}
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_NSS_CONNTYPE_BTN:
				*size = maxdim(GetStringBoundingBox(_connection_types_dropdown[0]), GetStringBoundingBox(_connection_types_dropdown[1]));
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
				if (!StrEmpty(_settings_client.network.server_password)) DrawString(r.right + WD_FRAMERECT_LEFT, this->width - WD_FRAMERECT_RIGHT, r.top, "*", TC_RED);
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
				ShowDropDownMenu(this, _connection_types_dropdown, _settings_client.network.server_advertise, WID_NSS_CONNTYPE_BTN, 0, 0); // do it for widget WID_NSS_CONNTYPE_BTN
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

			case WID_NSS_LANGUAGE_BTN: { // Language
				uint sel = 0;
				for (uint i = 0; i < _language_dropdown.size() - 1; i++) {
					if (_language_dropdown[i] == STR_NETWORK_LANG_ANY + _settings_client.network.server_lang) {
						sel = i;
						break;
					}
				}
				ShowDropDownMenu(this, _language_dropdown.data(), sel, WID_NSS_LANGUAGE_BTN, 0, 0);
				break;
			}

			case WID_NSS_GENERATE_GAME: // Start game
				_is_network_server = true;
				if (_ctrl_pressed) {
					StartNewGameWithoutGUI(GENERATE_NEW_SEED);
				} else {
					ShowGenerateLandscape();
				}
				break;

			case WID_NSS_LOAD_GAME:
				_is_network_server = true;
				ShowSaveLoadDialog(FT_SAVEGAME, SLO_LOAD);
				break;

			case WID_NSS_PLAY_SCENARIO:
				_is_network_server = true;
				ShowSaveLoadDialog(FT_SCENARIO, SLO_LOAD);
				break;

			case WID_NSS_PLAY_HEIGHTMAP:
				_is_network_server = true;
				ShowSaveLoadDialog(FT_HEIGHTMAP,SLO_LOAD);
				break;
		}
	}

	void OnDropdownSelect(int widget, int index) override
	{
		switch (widget) {
			case WID_NSS_CONNTYPE_BTN:
				_settings_client.network.server_advertise = (index != 0);
				break;
			case WID_NSS_LANGUAGE_BTN:
				_settings_client.network.server_lang = _language_dropdown[index] - STR_NETWORK_LANG_ANY;
				break;
			default:
				NOT_REACHED();
		}

		this->SetDirty();
	}

	void OnEditboxChanged(int wid) override
	{
		if (wid == WID_NSS_GAMENAME) {
			strecpy(_settings_client.network.server_name, this->name_editbox.text.buf, lastof(_settings_client.network.server_name));
		}
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
			strecpy(_settings_client.network.server_password, str, lastof(_settings_client.network.server_password));
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
					NWidget(WWT_TEXT, COLOUR_LIGHT_BLUE, WID_NSS_CONNTYPE_LABEL), SetFill(1, 0), SetDataTip(STR_NETWORK_START_SERVER_ADVERTISED_LABEL, STR_NULL),
					NWidget(WWT_DROPDOWN, COLOUR_LIGHT_BLUE, WID_NSS_CONNTYPE_BTN), SetFill(1, 0), SetDataTip(STR_BLACK_STRING, STR_NETWORK_START_SERVER_ADVERTISED_TOOLTIP),
				EndContainer(),
				NWidget(NWID_VERTICAL), SetPIP(0, 1, 0),
					NWidget(WWT_TEXT, COLOUR_LIGHT_BLUE, WID_NSS_LANGUAGE_LABEL), SetFill(1, 0), SetDataTip(STR_NETWORK_START_SERVER_LANGUAGE_SPOKEN, STR_NULL),
					NWidget(WWT_DROPDOWN, COLOUR_LIGHT_BLUE, WID_NSS_LANGUAGE_BTN), SetFill(1, 0), SetDataTip(STR_BLACK_STRING, STR_NETWORK_START_SERVER_LANGUAGE_TOOLTIP),
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
	DeleteWindowById(WC_NETWORK_WINDOW, WN_NETWORK_WINDOW_GAME);
	DeleteWindowById(WC_NETWORK_WINDOW, WN_NETWORK_WINDOW_LOBBY);

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

		SocialBeginEnterMultiplayer(server->info.server_name, server->address.GetAddressAsString(false));
	}

	CompanyID NetworkLobbyFindCompanyIndex(byte pos) const
	{
		/* Scroll through all this->company_info and get the 'pos' item that is not empty. */
		for (CompanyID i = COMPANY_FIRST; i < MAX_COMPANIES; i++) {
			if (!StrEmpty(this->company_info[i].company_name)) {
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

		if (this->company == INVALID_COMPANY || StrEmpty(this->company_info[this->company].company_name)) return;

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
				NetworkClientConnectGame(NetworkAddress(_settings_client.network.last_host, _settings_client.network.last_port), this->company);
				break;

			case WID_NL_NEW:      // New company
				NetworkClientConnectGame(NetworkAddress(_settings_client.network.last_host, _settings_client.network.last_port), COMPANY_NEW_COMPANY);
				break;

			case WID_NL_SPECTATE: // Spectate game
				NetworkClientConnectGame(NetworkAddress(_settings_client.network.last_host, _settings_client.network.last_port), COMPANY_SPECTATOR);
				break;

			case WID_NL_REFRESH:  // Refresh
				NetworkTCPQueryServer(NetworkAddress(_settings_client.network.last_host, _settings_client.network.last_port)); // company info
				NetworkUDPQueryServer(NetworkAddress(_settings_client.network.last_host, _settings_client.network.last_port)); // general data
				/* Clear the information so removed companies don't remain */
				for (auto &company : this->company_info) company = {};
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
	DeleteWindowById(WC_NETWORK_WINDOW, WN_NETWORK_WINDOW_START);
	DeleteWindowById(WC_NETWORK_WINDOW, WN_NETWORK_WINDOW_GAME);

	NetworkTCPQueryServer(NetworkAddress(_settings_client.network.last_host, _settings_client.network.last_port)); // company info
	NetworkUDPQueryServer(NetworkAddress(_settings_client.network.last_host, _settings_client.network.last_port)); // general data

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

/* The window below gives information about the connected clients
 * and also makes able to kick them (if server) and stuff like that. */

extern void DrawCompanyIcon(CompanyID cid, int x, int y);

/**
 * Prototype for ClientList actions.
 * @param ci The information about the current client.
 */
typedef void ClientList_Action_Proc(const NetworkClientInfo *ci);

static const NWidgetPart _nested_client_list_popup_widgets[] = {
	NWidget(WWT_PANEL, COLOUR_GREY, WID_CLP_PANEL), EndContainer(),
};

static WindowDesc _client_list_popup_desc(
	WDP_AUTO, nullptr, 0, 0,
	WC_CLIENT_LIST_POPUP, WC_CLIENT_LIST,
	0,
	_nested_client_list_popup_widgets, lengthof(_nested_client_list_popup_widgets)
);

/* Here we start to define the options out of the menu */
static void ClientList_Kick(const NetworkClientInfo *ci)
{
	NetworkServerKickClient(ci->client_id, nullptr);
}

static void ClientList_Ban(const NetworkClientInfo *ci)
{
	NetworkServerKickOrBanIP(ci->client_id, true, nullptr);
}

static void ClientList_SpeakToClient(const NetworkClientInfo *ci)
{
	ShowNetworkChatQueryWindow(DESTTYPE_CLIENT, ci->client_id);
}

static void ClientList_SpeakToCompany(const NetworkClientInfo *ci)
{
	ShowNetworkChatQueryWindow(DESTTYPE_TEAM, ci->client_playas);
}

static void ClientList_SpeakToAll(const NetworkClientInfo *ci)
{
	ShowNetworkChatQueryWindow(DESTTYPE_BROADCAST, 0);
}

/** Popup selection window to chose an action to perform */
struct NetworkClientListPopupWindow : Window {
	/** Container for actions that can be executed. */
	struct ClientListAction {
		StringID name;                ///< Name of the action to execute
		ClientList_Action_Proc *proc; ///< Action to execute
	};

	uint sel_index;
	ClientID client_id;
	Point desired_location;
	std::vector<ClientListAction> actions; ///< Actions to execute

	/**
	 * Add an action to the list of actions to execute.
	 * @param name the name of the action
	 * @param proc the procedure to execute for the action
	 */
	inline void AddAction(StringID name, ClientList_Action_Proc *proc)
	{
		this->actions.push_back({name, proc});
	}

	NetworkClientListPopupWindow(WindowDesc *desc, int x, int y, ClientID client_id) :
			Window(desc),
			sel_index(0), client_id(client_id)
	{
		this->desired_location.x = x;
		this->desired_location.y = y;

		const NetworkClientInfo *ci = NetworkClientInfo::GetByClientID(client_id);

		if (_network_own_client_id != ci->client_id) {
			this->AddAction(STR_NETWORK_CLIENTLIST_SPEAK_TO_CLIENT, &ClientList_SpeakToClient);
		}

		if (Company::IsValidID(ci->client_playas) || ci->client_playas == COMPANY_SPECTATOR) {
			this->AddAction(STR_NETWORK_CLIENTLIST_SPEAK_TO_COMPANY, &ClientList_SpeakToCompany);
		}
		this->AddAction(STR_NETWORK_CLIENTLIST_SPEAK_TO_ALL, &ClientList_SpeakToAll);

		/* A server can kick clients (but not himself). */
		if (_network_server && _network_own_client_id != ci->client_id) {
			this->AddAction(STR_NETWORK_CLIENTLIST_KICK, &ClientList_Kick);
			this->AddAction(STR_NETWORK_CLIENTLIST_BAN, &ClientList_Ban);
		}

		this->InitNested(client_id);
		CLRBITS(this->flags, WF_WHITE_BORDER);
	}

	Point OnInitialPosition(int16 sm_width, int16 sm_height, int window_number) override
	{
		return this->desired_location;
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		Dimension d = *size;
		for (const ClientListAction &action : this->actions) {
			d = maxdim(GetStringBoundingBox(action.name), d);
		}

		d.height *= (uint)this->actions.size();
		d.width += WD_FRAMERECT_LEFT + WD_FRAMERECT_RIGHT;
		d.height += WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM;
		*size = d;
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		/* Draw the actions */
		int sel = this->sel_index;
		int y = r.top + WD_FRAMERECT_TOP;
		for (const ClientListAction &action : this->actions) {
			TextColour colour;
			if (sel-- == 0) { // Selected item, highlight it
				GfxFillRect(r.left + 1, y, r.right - 1, y + FONT_HEIGHT_NORMAL - 1, PC_BLACK);
				colour = TC_WHITE;
			} else {
				colour = TC_BLACK;
			}

			DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, action.name, colour);
			y += FONT_HEIGHT_NORMAL;
		}
	}

	void OnMouseLoop() override
	{
		/* We selected an action */
		uint index = (_cursor.pos.y - this->top - WD_FRAMERECT_TOP) / FONT_HEIGHT_NORMAL;

		if (_left_button_down) {
			if (index == this->sel_index || index >= this->actions.size()) return;

			this->sel_index = index;
			this->SetDirty();
		} else {
			if (index < this->actions.size() && _cursor.pos.y >= this->top) {
				const NetworkClientInfo *ci = NetworkClientInfo::GetByClientID(this->client_id);
				if (ci != nullptr) this->actions[index].proc(ci);
			}

			DeleteWindowByClass(WC_CLIENT_LIST_POPUP);
		}
	}
};

/**
 * Show the popup (action list)
 */
static void PopupClientList(ClientID client_id, int x, int y)
{
	DeleteWindowByClass(WC_CLIENT_LIST_POPUP);

	if (NetworkClientInfo::GetByClientID(client_id) == nullptr) return;

	new NetworkClientListPopupWindow(&_client_list_popup_desc, x, y, client_id);
}

static const NWidgetPart _nested_client_list_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY), SetDataTip(STR_NETWORK_COMPANY_LIST_CLIENT_LIST_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY, WID_CL_PANEL), SetMinimalSize(250, WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM), SetResize(1, 1), EndContainer(),
};

static WindowDesc _client_list_desc(
	WDP_AUTO, "list_clients", 0, 0,
	WC_CLIENT_LIST, WC_NONE,
	0,
	_nested_client_list_widgets, lengthof(_nested_client_list_widgets)
);

/**
 * Main handle for clientlist
 */
struct NetworkClientListWindow : Window {
	int selected_item;

	uint server_client_width;
	uint line_height;

	Dimension icon_size;

	NetworkClientListWindow(WindowDesc *desc, WindowNumber window_number) :
			Window(desc),
			selected_item(-1)
	{
		this->InitNested(window_number);
	}

	/**
	 * Finds the amount of clients and set the height correct
	 */
	bool CheckClientListHeight()
	{
		int num = 0;

		/* Should be replaced with a loop through all clients */
		for (const NetworkClientInfo *ci : NetworkClientInfo::Iterate()) {
			if (ci->client_playas != COMPANY_INACTIVE_CLIENT) num++;
		}

		num *= this->line_height;

		int diff = (num + WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM) - (this->GetWidget<NWidgetBase>(WID_CL_PANEL)->current_y);
		/* If height is changed */
		if (diff != 0) {
			ResizeWindow(this, 0, diff, false);
			return false;
		}
		return true;
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		if (widget != WID_CL_PANEL) return;

		this->server_client_width = std::max(GetStringBoundingBox(STR_NETWORK_SERVER).width, GetStringBoundingBox(STR_NETWORK_CLIENT).width) + WD_FRAMERECT_RIGHT;
		this->icon_size = GetSpriteSize(SPR_COMPANY_ICON);
		this->line_height = std::max(this->icon_size.height + 2U, (uint)FONT_HEIGHT_NORMAL);

		uint width = 100; // Default width
		for (const NetworkClientInfo *ci : NetworkClientInfo::Iterate()) {
			width = std::max(width, GetStringBoundingBox(ci->client_name).width);
		}

		size->width = WD_FRAMERECT_LEFT + this->server_client_width + this->icon_size.width + WD_FRAMERECT_LEFT + width + WD_FRAMERECT_RIGHT;
	}

	void OnPaint() override
	{
		/* Check if we need to reset the height */
		if (!this->CheckClientListHeight()) return;

		this->DrawWidgets();
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		if (widget != WID_CL_PANEL) return;

		bool rtl = _current_text_dir == TD_RTL;
		int icon_offset = (this->line_height - icon_size.height) / 2;
		int text_offset = (this->line_height - FONT_HEIGHT_NORMAL) / 2;

		uint y = r.top + WD_FRAMERECT_TOP;
		uint left = r.left + WD_FRAMERECT_LEFT;
		uint right = r.right - WD_FRAMERECT_RIGHT;
		uint type_icon_width = this->server_client_width + this->icon_size.width + WD_FRAMERECT_LEFT;


		uint type_left  = rtl ? right - this->server_client_width : left;
		uint type_right = rtl ? right : left + this->server_client_width - 1;
		uint icon_left  = rtl ? right - type_icon_width + WD_FRAMERECT_LEFT : left + this->server_client_width;
		uint name_left  = rtl ? left : left + type_icon_width;
		uint name_right = rtl ? right - type_icon_width : right;

		int i = 0;
		for (const NetworkClientInfo *ci : NetworkClientInfo::Iterate()) {
			TextColour colour;
			if (this->selected_item == i++) { // Selected item, highlight it
				GfxFillRect(r.left + 1, y, r.right - 1, y + this->line_height - 1, PC_BLACK);
				colour = TC_WHITE;
			} else {
				colour = TC_BLACK;
			}

			if (ci->client_id == CLIENT_ID_SERVER) {
				DrawString(type_left, type_right, y + text_offset, STR_NETWORK_SERVER, colour);
			} else {
				DrawString(type_left, type_right, y + text_offset, STR_NETWORK_CLIENT, colour);
			}

			/* Filter out spectators */
			if (Company::IsValidID(ci->client_playas)) DrawCompanyIcon(ci->client_playas, icon_left, y + icon_offset);

			DrawString(name_left, name_right, y + text_offset, ci->client_name, colour);

			y += line_height;
		}
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		/* Show the popup with option */
		if (this->selected_item != -1) {
			int client_no = this->selected_item;
			for (NetworkClientInfo *ci : NetworkClientInfo::Iterate()) {
				if (client_no == 0) {
					PopupClientList(ci->client_id, pt.x + this->left, pt.y + this->top);
					break;
				}
				client_no--;
			}
		}
	}

	void OnMouseOver(Point pt, int widget) override
	{
		/* -1 means we left the current window */
		if (pt.y == -1) {
			this->selected_item = -1;
			this->SetDirty();
			return;
		}

		/* Find the new selected item (if any) */
		pt.y -= this->GetWidget<NWidgetBase>(WID_CL_PANEL)->pos_y;
		int item = -1;
		if (IsInsideMM(pt.y, WD_FRAMERECT_TOP, this->GetWidget<NWidgetBase>(WID_CL_PANEL)->current_y - WD_FRAMERECT_BOTTOM)) {
			item = (pt.y - WD_FRAMERECT_TOP) / this->line_height;
		}

		/* It did not change.. no update! */
		if (item == this->selected_item) return;
		this->selected_item = item;

		/* Repaint */
		this->SetDirty();
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
	DeleteWindowById(WC_NETWORK_STATUS_WINDOW, WN_NETWORK_STATUS_WINDOW_JOIN);
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
			strecpy(_settings_client.network.default_company_pass, this->password_editbox.text.buf, lastof(_settings_client.network.default_company_pass));
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
				delete this;
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
	DeleteWindowById(WC_COMPANY_PASSWORD_WINDOW, 0);

	new NetworkCompanyPasswordWindow(&_network_company_password_window_desc, parent);
}

void SocialJoinRequestedGame(const std::string &server_cookie)
{
	/* If player is not on main menu, don't connect. Don't want to interrupt any current game. */
	if (_game_mode != GM_MENU) {
		ShowErrorMessage(STR_SOCIAL_ERROR_CANNOT_JOIN, STR_NONE, WL_WARNING);
		return;
	}

	/* Okay let's try to join! */
	const char *company;
	const char *port;
	char *server_cookie_cstr = stredup(server_cookie.c_str());
	ParseConnectionString(&company, &port, server_cookie_cstr);
	NetworkAddress addr(server_cookie_cstr, atoi(port));
	free(server_cookie_cstr);

	NetworkClientConnectGame(addr, COMPANY_SPECTATOR);
}
