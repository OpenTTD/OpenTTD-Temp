/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file network_content_gui.cpp Implementation of the Network Content related GUIs. */

#include "../stdafx.h"
#include "../strings_func.h"
#include "../gfx_func.h"
#include "../window_func.h"
#include "../error.h"
#include "../ai/ai.hpp"
#include "../game/game.hpp"
#include "../base_media_base.h"
#include "../openttd.h"
#include "../sortlist_type.h"
#include "../stringfilter_type.h"
#include "../querystring_gui.h"
#include "../core/geometry_func.hpp"
#include "../textfile_gui.h"
#include "network_content_gui.h"


#include "table/strings.h"
#include "../table/sprites.h"

#include <bitset>

#include "../safeguards.h"


/** Whether the user accepted to enter external websites during this session. */
static bool _accepted_external_search = false;


/** Window for displaying the textfile of an item in the content list. */
struct ContentTextfileWindow : public TextfileWindow {
	const ContentInfo *ci; ///< View the textfile of this ContentInfo.

	ContentTextfileWindow(TextfileType file_type, const ContentInfo *ci) : TextfileWindow(file_type), ci(ci)
	{
		const char *textfile = this->ci->GetTextfile(file_type);
		this->LoadTextfile(textfile, GetContentInfoSubDir(this->ci->type));
	}

	StringID GetTypeString() const
	{
		switch (this->ci->type) {
			case CONTENT_TYPE_NEWGRF:        return STR_CONTENT_TYPE_NEWGRF;
			case CONTENT_TYPE_BASE_GRAPHICS: return STR_CONTENT_TYPE_BASE_GRAPHICS;
			case CONTENT_TYPE_BASE_SOUNDS:   return STR_CONTENT_TYPE_BASE_SOUNDS;
			case CONTENT_TYPE_BASE_MUSIC:    return STR_CONTENT_TYPE_BASE_MUSIC;
			case CONTENT_TYPE_AI:            return STR_CONTENT_TYPE_AI;
			case CONTENT_TYPE_AI_LIBRARY:    return STR_CONTENT_TYPE_AI_LIBRARY;
			case CONTENT_TYPE_GAME:          return STR_CONTENT_TYPE_GAME_SCRIPT;
			case CONTENT_TYPE_GAME_LIBRARY:  return STR_CONTENT_TYPE_GS_LIBRARY;
			case CONTENT_TYPE_SCENARIO:      return STR_CONTENT_TYPE_SCENARIO;
			case CONTENT_TYPE_HEIGHTMAP:     return STR_CONTENT_TYPE_HEIGHTMAP;
			default: NOT_REACHED();
		}
	}

	void SetStringParameters(int widget) const override
	{
		if (widget == WID_TF_CAPTION) {
			SetDParam(0, this->GetTypeString());
			SetDParamStr(1, this->ci->name);
		}
	}
};

void ShowContentTextfileWindow(TextfileType file_type, const ContentInfo *ci)
{
	DeleteWindowById(WC_TEXTFILE, file_type);
	new ContentTextfileWindow(file_type, ci);
}

/** Nested widgets for the download window. */
static const NWidgetPart _nested_network_content_download_status_window_widgets[] = {
	NWidget(WWT_CAPTION, COLOUR_GREY), SetDataTip(STR_CONTENT_DOWNLOAD_TITLE, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	NWidget(WWT_PANEL, COLOUR_GREY, WID_NCDS_BACKGROUND),
		NWidget(NWID_SPACER), SetMinimalSize(350, 0), SetMinimalTextLines(3, WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM + 30),
		NWidget(NWID_HORIZONTAL),
			NWidget(NWID_SPACER), SetMinimalSize(125, 0),
			NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NCDS_CANCELOK), SetMinimalSize(101, 12), SetDataTip(STR_BUTTON_CANCEL, STR_NULL),
			NWidget(NWID_SPACER), SetFill(1, 0),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 4),
	EndContainer(),
};

/** Window description for the download window */
static WindowDesc _network_content_download_status_window_desc(
	WDP_CENTER, nullptr, 0, 0,
	WC_NETWORK_STATUS_WINDOW, WC_NONE,
	WDF_MODAL,
	_nested_network_content_download_status_window_widgets, lengthof(_nested_network_content_download_status_window_widgets)
);

BaseNetworkContentDownloadStatusWindow::BaseNetworkContentDownloadStatusWindow(WindowDesc *desc) :
		Window(desc), cur_id(UINT32_MAX)
{
	_network_content_client.AddCallback(this);
	_network_content_client.DownloadSelectedContent(this->total_files, this->total_bytes);

	this->InitNested(WN_NETWORK_STATUS_WINDOW_CONTENT_DOWNLOAD);
}

BaseNetworkContentDownloadStatusWindow::~BaseNetworkContentDownloadStatusWindow()
{
	_network_content_client.RemoveCallback(this);
}

void BaseNetworkContentDownloadStatusWindow::DrawWidget(const Rect &r, int widget) const
{
	if (widget != WID_NCDS_BACKGROUND) return;

	/* Draw nice progress bar :) */
	DrawFrameRect(r.left + 20, r.top + 4, r.left + 20 + (int)((this->width - 40LL) * this->downloaded_bytes / this->total_bytes), r.top + 14, COLOUR_MAUVE, FR_NONE);

	int y = r.top + 20;
	SetDParam(0, this->downloaded_bytes);
	SetDParam(1, this->total_bytes);
	SetDParam(2, this->downloaded_bytes * 100LL / this->total_bytes);
	DrawString(r.left + 2, r.right - 2, y, STR_CONTENT_DOWNLOAD_PROGRESS_SIZE, TC_FROMSTRING, SA_HOR_CENTER);

	StringID str;
	if (this->downloaded_bytes == this->total_bytes) {
		str = STR_CONTENT_DOWNLOAD_COMPLETE;
	} else if (!StrEmpty(this->name)) {
		SetDParamStr(0, this->name);
		SetDParam(1, this->downloaded_files);
		SetDParam(2, this->total_files);
		str = STR_CONTENT_DOWNLOAD_FILE;
	} else {
		str = STR_CONTENT_DOWNLOAD_INITIALISE;
	}

	y += FONT_HEIGHT_NORMAL + 5;
	DrawStringMultiLine(r.left + 2, r.right - 2, y, y + FONT_HEIGHT_NORMAL * 2, str, TC_FROMSTRING, SA_CENTER);
}

void BaseNetworkContentDownloadStatusWindow::OnDownloadProgress(const ContentInfo *ci, int bytes)
{
	if (ci->id != this->cur_id) {
		strecpy(this->name, ci->filename, lastof(this->name));
		this->cur_id = ci->id;
		this->downloaded_files++;
	}

	this->downloaded_bytes += bytes;
	this->SetDirty();
}


/** Window for showing the download status of content */
struct NetworkContentDownloadStatusWindow : public BaseNetworkContentDownloadStatusWindow {
private:
	std::vector<ContentType> receivedTypes;     ///< Types we received so we can update their cache

public:
	/**
	 * Create a new download window based on a list of content information
	 * with flags whether to download them or not.
	 */
	NetworkContentDownloadStatusWindow() : BaseNetworkContentDownloadStatusWindow(&_network_content_download_status_window_desc)
	{
		this->parent = FindWindowById(WC_NETWORK_WINDOW, WN_NETWORK_WINDOW_CONTENT_LIST);
	}

	/** Free whatever we've allocated */
	~NetworkContentDownloadStatusWindow()
	{
		TarScanner::Mode mode = TarScanner::NONE;
		for (auto ctype : this->receivedTypes) {
			switch (ctype) {
				case CONTENT_TYPE_AI:
				case CONTENT_TYPE_AI_LIBRARY:
					/* AI::Rescan calls the scanner. */
					break;
				case CONTENT_TYPE_GAME:
				case CONTENT_TYPE_GAME_LIBRARY:
					/* Game::Rescan calls the scanner. */
					break;

				case CONTENT_TYPE_BASE_GRAPHICS:
				case CONTENT_TYPE_BASE_SOUNDS:
				case CONTENT_TYPE_BASE_MUSIC:
					mode |= TarScanner::BASESET;
					break;

				case CONTENT_TYPE_NEWGRF:
					/* ScanNewGRFFiles calls the scanner. */
					break;

				case CONTENT_TYPE_SCENARIO:
				case CONTENT_TYPE_HEIGHTMAP:
					mode |= TarScanner::SCENARIO;
					break;

				default:
					break;
			}
		}

		TarScanner::DoScan(mode);

		/* Tell all the backends about what we've downloaded */
		for (auto ctype : this->receivedTypes) {
			switch (ctype) {
				case CONTENT_TYPE_AI:
				case CONTENT_TYPE_AI_LIBRARY:
					AI::Rescan();
					break;

				case CONTENT_TYPE_GAME:
				case CONTENT_TYPE_GAME_LIBRARY:
					Game::Rescan();
					break;

				case CONTENT_TYPE_BASE_GRAPHICS:
					BaseGraphics::FindSets();
					SetWindowDirty(WC_GAME_OPTIONS, WN_GAME_OPTIONS_GAME_OPTIONS);
					break;

				case CONTENT_TYPE_BASE_SOUNDS:
					BaseSounds::FindSets();
					SetWindowDirty(WC_GAME_OPTIONS, WN_GAME_OPTIONS_GAME_OPTIONS);
					break;

				case CONTENT_TYPE_BASE_MUSIC:
					BaseMusic::FindSets();
					SetWindowDirty(WC_GAME_OPTIONS, WN_GAME_OPTIONS_GAME_OPTIONS);
					break;

				case CONTENT_TYPE_NEWGRF:
					RequestNewGRFScan();
					break;

				case CONTENT_TYPE_SCENARIO:
				case CONTENT_TYPE_HEIGHTMAP:
					extern void ScanScenarios();
					ScanScenarios();
					InvalidateWindowData(WC_SAVELOAD, 0, 0);
					break;

				default:
					break;
			}
		}

		/* Always invalidate the download window; tell it we are going to be gone */
		InvalidateWindowData(WC_NETWORK_WINDOW, WN_NETWORK_WINDOW_CONTENT_LIST, 2);
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		if (widget == WID_NCDS_CANCELOK) {
			if (this->downloaded_bytes != this->total_bytes) {
				_network_content_client.Close();
				delete this;
			} else {
				/* If downloading succeeded, close the online content window. This will close
				 * the current window as well. */
				DeleteWindowById(WC_NETWORK_WINDOW, WN_NETWORK_WINDOW_CONTENT_LIST);
			}
		}
	}

	void OnDownloadProgress(const ContentInfo *ci, int bytes) override
	{
		BaseNetworkContentDownloadStatusWindow::OnDownloadProgress(ci, bytes);
		include(this->receivedTypes, ci->type);

		/* When downloading is finished change cancel in ok */
		if (this->downloaded_bytes == this->total_bytes) {
			this->GetWidget<NWidgetCore>(WID_NCDS_CANCELOK)->widget_data = STR_BUTTON_OK;
		}
	}
};

/** Filter data for NetworkContentListWindow. */
struct ContentListFilterData {
	StringFilter string_filter; ///< Text filter of content list
	std::bitset<CONTENT_TYPE_END> types; ///< Content types displayed
};

/** Filter criteria for NetworkContentListWindow. */
enum ContentListFilterCriteria {
	CONTENT_FILTER_TEXT = 0,        ///< Filter by query sting
	CONTENT_FILTER_TYPE_OR_SELECTED,///< Filter by being of displayed type or selected for download
};

/** Window that lists the content that's at the content server */
class NetworkContentListWindow : public Window, ContentCallback {
	/** List with content infos. */
	typedef GUIList<const ContentInfo *, ContentListFilterData &> GUIContentList;

	static const uint EDITBOX_MAX_SIZE   =  50; ///< Maximum size of the editbox in characters.

	static Listing last_sorting;     ///< The last sorting setting.
	static Filtering last_filtering; ///< The last filtering setting.
	static GUIContentList::SortFunction * const sorter_funcs[];   ///< Sorter functions
	static GUIContentList::FilterFunction * const filter_funcs[]; ///< Filter functions.
	GUIContentList content;      ///< List with content
	bool auto_select;            ///< Automatically select all content when the meta-data becomes available
	ContentListFilterData filter_data; ///< Filter for content list
	QueryString filter_editbox;  ///< Filter editbox;
	Dimension checkbox_size;     ///< Size of checkbox/"blot" sprite

	const ContentInfo *selected; ///< The selected content info
	int list_pos;                ///< Our position in the list
	uint filesize_sum;           ///< The sum of all selected file sizes
	Scrollbar *vscroll;          ///< Cache of the vertical scrollbar

	static char content_type_strs[CONTENT_TYPE_END][64]; ///< Cached strings for all content types.

	/** Search external websites for content */
	void OpenExternalSearch()
	{
		extern void OpenBrowser(const char *url);

		char url[1024];
		const char *last = lastof(url);

		char *pos = strecpy(url, "https://grfsearch.openttd.org/?", last);

		if (this->auto_select) {
			pos = strecpy(pos, "do=searchgrfid&q=", last);

			bool first = true;
			for (const ContentInfo *ci : this->content) {
				if (ci->state != ContentInfo::DOES_NOT_EXIST) continue;

				if (!first) pos = strecpy(pos, ",", last);
				first = false;

				pos += seprintf(pos, last, "%08X", ci->unique_id);
				pos = strecpy(pos, ":", last);
				pos = md5sumToString(pos, last, ci->md5sum);
			}
		} else {
			pos = strecpy(pos, "do=searchtext&q=", last);

			/* Escape search term */
			for (const char *search = this->filter_editbox.text.buf; *search != '\0'; search++) {
				/* Remove quotes */
				if (*search == '\'' || *search == '"') continue;

				/* Escape special chars, such as &%,= */
				if (*search < 0x30) {
					pos += seprintf(pos, last, "%%%02X", *search);
				} else if (pos < last) {
					*pos = *search;
					*++pos = '\0';
				}
			}
		}

		OpenBrowser(url);
	}

	/**
	 * Callback function for disclaimer about entering external websites.
	 */
	static void ExternalSearchDisclaimerCallback(Window *w, bool accepted)
	{
		if (accepted) {
			_accepted_external_search = true;
			((NetworkContentListWindow*)w)->OpenExternalSearch();
		}
	}

	/**
	 * (Re)build the network game list as its amount has changed because
	 * an item has been added or deleted for example
	 */
	void BuildContentList()
	{
		if (!this->content.NeedRebuild()) return;

		/* Create temporary array of games to use for listing */
		this->content.clear();

		bool all_available = true;

		for (ConstContentIterator iter = _network_content_client.Begin(); iter != _network_content_client.End(); iter++) {
			if ((*iter)->state == ContentInfo::DOES_NOT_EXIST) all_available = false;
			this->content.push_back(*iter);
		}

		this->SetWidgetDisabledState(WID_NCL_SEARCH_EXTERNAL, this->auto_select && all_available);

		this->FilterContentList();
		this->content.shrink_to_fit();
		this->content.RebuildDone();
		this->SortContentList();

		this->vscroll->SetCount((int)this->content.size()); // Update the scrollbar
		this->ScrollToSelected();
	}

	/** Sort content by name. */
	static bool NameSorter(const ContentInfo * const &a, const ContentInfo * const &b)
	{
		return strnatcmp(a->name, b->name, true) < 0; // Sort by name (natural sorting).
	}

	/** Sort content by type. */
	static bool TypeSorter(const ContentInfo * const &a, const ContentInfo * const &b)
	{
		int r = 0;
		if (a->type != b->type) {
			r = strnatcmp(content_type_strs[a->type], content_type_strs[b->type]);
		}
		if (r == 0) return NameSorter(a, b);
		return r < 0;
	}

	/** Sort content by state. */
	static bool StateSorter(const ContentInfo * const &a, const ContentInfo * const &b)
	{
		int r = a->state - b->state;
		if (r == 0) return TypeSorter(a, b);
		return r < 0;
	}

	/** Sort the content list */
	void SortContentList()
	{
		if (!this->content.Sort()) return;

		int idx = find_index(this->content, this->selected);
		if (idx >= 0) this->list_pos = idx;
	}

	/** Filter content by tags/name */
	static bool CDECL TagNameFilter(const ContentInfo * const *a, ContentListFilterData &filter)
	{
		filter.string_filter.ResetState();
		for (int i = 0; i < (*a)->tag_count; i++) {
			filter.string_filter.AddLine((*a)->tags[i]);
		}
		filter.string_filter.AddLine((*a)->name);
		return filter.string_filter.GetState();
	}

	/** Filter content by type, but still show content selected for download. */
	static bool CDECL TypeOrSelectedFilter(const ContentInfo * const *a, ContentListFilterData &filter)
	{
		if (filter.types.none()) return true;
		if (filter.types[(*a)->type]) return true;
		return ((*a)->state == ContentInfo::SELECTED || (*a)->state == ContentInfo::AUTOSELECTED);
	}

	/** Filter the content list */
	void FilterContentList()
	{
		/* Apply filters. */
		bool changed = false;
		if (!this->filter_data.string_filter.IsEmpty()) {
			this->content.SetFilterType(CONTENT_FILTER_TEXT);
			changed |= this->content.Filter(this->filter_data);
		}
		if (this->filter_data.types.any()) {
			this->content.SetFilterType(CONTENT_FILTER_TYPE_OR_SELECTED);
			changed |= this->content.Filter(this->filter_data);
		}
		if (!changed) return;

		/* update list position */
		int idx = find_index(this->content, this->selected);
		if (idx >= 0) {
			this->list_pos = idx;
			return;
		}

		/* previously selected item not in list anymore */
		this->selected = nullptr;
		this->list_pos = 0;
	}

	/**
	 * Update filter state based on current window state.
	 * @return true if filter state was changed, otherwise false.
	 */
	bool UpdateFilterState()
	{
		Filtering old_params = this->content.GetFiltering();
		bool new_state = !this->filter_data.string_filter.IsEmpty() || this->filter_data.types.any();
		if (new_state != old_params.state) {
			this->content.SetFilterState(new_state);
		}
		return new_state != old_params.state;
	}

	/** Make sure that the currently selected content info is within the visible part of the matrix */
	void ScrollToSelected()
	{
		if (this->selected == nullptr) return;

		this->vscroll->ScrollTowards(this->list_pos);
	}

	friend void BuildContentTypeStringList();
public:
	/**
	 * Create the content list window.
	 * @param desc the window description to pass to Window's constructor.
	 * @param select_all Whether the select all button is allowed or not.
	 * @param types the main type of content to display or #CONTENT_TYPE_END.
	 *   When a type other than #CONTENT_TYPE_END is given, dependencies of
	 *   other types are only shown when content that depend on them are
	 *   selected.
	 */
	NetworkContentListWindow(WindowDesc *desc, bool select_all, const std::bitset<CONTENT_TYPE_END> &types) :
			Window(desc),
			auto_select(select_all),
			filter_editbox(EDITBOX_MAX_SIZE),
			selected(nullptr),
			list_pos(0)
	{
		this->checkbox_size = maxdim(maxdim(GetSpriteSize(SPR_BOX_EMPTY), GetSpriteSize(SPR_BOX_CHECKED)), GetSpriteSize(SPR_BLOT));

		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_NCL_SCROLLBAR);
		this->FinishInitNested(WN_NETWORK_WINDOW_CONTENT_LIST);

		this->GetWidget<NWidgetStacked>(WID_NCL_SEL_ALL_UPDATE)->SetDisplayedPlane(select_all);

		this->querystrings[WID_NCL_FILTER] = &this->filter_editbox;
		this->filter_editbox.cancel_button = QueryString::ACTION_CLEAR;
		this->SetFocusedWidget(WID_NCL_FILTER);
		this->SetWidgetDisabledState(WID_NCL_SEARCH_EXTERNAL, this->auto_select);
		this->filter_data.types = types;

		_network_content_client.AddCallback(this);
		this->content.SetListing(this->last_sorting);
		this->content.SetFiltering(this->last_filtering);
		this->content.SetSortFuncs(this->sorter_funcs);
		this->content.SetFilterFuncs(this->filter_funcs);
		this->UpdateFilterState();
		this->content.ForceRebuild();
		this->FilterContentList();
		this->SortContentList();
		this->InvalidateData();
	}

	/** Free everything we allocated */
	~NetworkContentListWindow()
	{
		_network_content_client.RemoveCallback(this);
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_NCL_FILTER_CAPT:
				*size = maxdim(*size, GetStringBoundingBox(STR_CONTENT_FILTER_TITLE));
				break;

			case WID_NCL_CHECKBOX:
				size->width = this->checkbox_size.width + WD_MATRIX_RIGHT + WD_MATRIX_LEFT;
				break;

			case WID_NCL_TYPE: {
				Dimension d = *size;
				for (int i = CONTENT_TYPE_BEGIN; i < CONTENT_TYPE_END; i++) {
					d = maxdim(d, GetStringBoundingBox(STR_CONTENT_TYPE_BASE_GRAPHICS + i - CONTENT_TYPE_BASE_GRAPHICS));
				}
				size->width = d.width + WD_MATRIX_RIGHT + WD_MATRIX_LEFT;
				break;
			}

			case WID_NCL_MATRIX:
				resize->height = std::max(this->checkbox_size.height, (uint)FONT_HEIGHT_NORMAL) + WD_MATRIX_TOP + WD_MATRIX_BOTTOM;
				size->height = 10 * resize->height;
				break;
		}
	}


	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_NCL_FILTER_CAPT:
				DrawString(r.left, r.right, r.top, STR_CONTENT_FILTER_TITLE, TC_FROMSTRING, SA_RIGHT);
				break;

			case WID_NCL_DETAILS:
				this->DrawDetails(r);
				break;

			case WID_NCL_MATRIX:
				this->DrawMatrix(r);
				break;
		}
	}

	void OnPaint() override
	{
		const SortButtonState arrow = this->content.IsDescSortOrder() ? SBS_DOWN : SBS_UP;

		if (this->content.NeedRebuild()) {
			this->BuildContentList();
		}

		this->DrawWidgets();

		switch (this->content.SortType()) {
			case WID_NCL_CHECKBOX - WID_NCL_CHECKBOX: this->DrawSortButtonState(WID_NCL_CHECKBOX, arrow); break;
			case WID_NCL_TYPE     - WID_NCL_CHECKBOX: this->DrawSortButtonState(WID_NCL_TYPE,     arrow); break;
			case WID_NCL_NAME     - WID_NCL_CHECKBOX: this->DrawSortButtonState(WID_NCL_NAME,     arrow); break;
		}
	}

	/**
	 * Draw/fill the matrix with the list of content to download.
	 * @param r The boundaries of the matrix.
	 */
	void DrawMatrix(const Rect &r) const
	{
		const NWidgetBase *nwi_checkbox = this->GetWidget<NWidgetBase>(WID_NCL_CHECKBOX);
		const NWidgetBase *nwi_name = this->GetWidget<NWidgetBase>(WID_NCL_NAME);
		const NWidgetBase *nwi_type = this->GetWidget<NWidgetBase>(WID_NCL_TYPE);

		int line_height = std::max(this->checkbox_size.height, (uint)FONT_HEIGHT_NORMAL);

		/* Fill the matrix with the information */
		int sprite_y_offset = WD_MATRIX_TOP + (line_height - this->checkbox_size.height) / 2 - 1;
		int text_y_offset = WD_MATRIX_TOP + (line_height - FONT_HEIGHT_NORMAL) / 2;
		uint y = r.top;

		auto iter = this->content.begin() + this->vscroll->GetPosition();
		size_t last = this->vscroll->GetPosition() + this->vscroll->GetCapacity();
		auto end = (last < this->content.size()) ? this->content.begin() + last : this->content.end();

		for (/**/; iter != end; iter++) {
			const ContentInfo *ci = *iter;

			if (ci == this->selected) GfxFillRect(r.left + 1, y + 1, r.right - 1, y + this->resize.step_height - 1, PC_GREY);

			SpriteID sprite;
			SpriteID pal = PAL_NONE;
			switch (ci->state) {
				case ContentInfo::UNSELECTED:     sprite = SPR_BOX_EMPTY;   break;
				case ContentInfo::SELECTED:       sprite = SPR_BOX_CHECKED; break;
				case ContentInfo::AUTOSELECTED:   sprite = SPR_BOX_CHECKED; break;
				case ContentInfo::ALREADY_HERE:   sprite = SPR_BLOT; pal = PALETTE_TO_GREEN; break;
				case ContentInfo::DOES_NOT_EXIST: sprite = SPR_BLOT; pal = PALETTE_TO_RED;   break;
				default: NOT_REACHED();
			}
			DrawSprite(sprite, pal, nwi_checkbox->pos_x + (pal == PAL_NONE ? 2 : 3), y + sprite_y_offset + (pal == PAL_NONE ? 1 : 0));

			StringID str = STR_CONTENT_TYPE_BASE_GRAPHICS + ci->type - CONTENT_TYPE_BASE_GRAPHICS;
			DrawString(nwi_type->pos_x, nwi_type->pos_x + nwi_type->current_x - 1, y + text_y_offset, str, TC_BLACK, SA_HOR_CENTER);

			DrawString(nwi_name->pos_x + WD_FRAMERECT_LEFT, nwi_name->pos_x + nwi_name->current_x - WD_FRAMERECT_RIGHT, y + text_y_offset, ci->name, TC_BLACK);
			y += this->resize.step_height;
		}
	}

	/**
	 * Helper function to draw the details part of this window.
	 * @param r the rectangle to stay within while drawing
	 */
	void DrawDetails(const Rect &r) const
	{
		static const int DETAIL_LEFT         =  5; ///< Number of pixels at the left
		static const int DETAIL_RIGHT        =  5; ///< Number of pixels at the right
		static const int DETAIL_TOP          =  5; ///< Number of pixels at the top

		/* Height for the title banner */
		int DETAIL_TITLE_HEIGHT = 5 * FONT_HEIGHT_NORMAL;

		/* Create the nice grayish rectangle at the details top */
		GfxFillRect(r.left + 1, r.top + 1, r.right - 1, r.top + DETAIL_TITLE_HEIGHT, PC_DARK_BLUE);
		DrawString(r.left + WD_INSET_LEFT, r.right - WD_INSET_RIGHT, r.top + FONT_HEIGHT_NORMAL + WD_INSET_TOP, STR_CONTENT_DETAIL_TITLE, TC_FROMSTRING, SA_HOR_CENTER);

		/* Draw the total download size */
		SetDParam(0, this->filesize_sum);
		DrawString(r.left + DETAIL_LEFT, r.right - DETAIL_RIGHT, r.bottom - FONT_HEIGHT_NORMAL - WD_PAR_VSEP_NORMAL, STR_CONTENT_TOTAL_DOWNLOAD_SIZE);

		if (this->selected == nullptr) return;

		/* And fill the rest of the details when there's information to place there */
		DrawStringMultiLine(r.left + WD_INSET_LEFT, r.right - WD_INSET_RIGHT, r.top + DETAIL_TITLE_HEIGHT / 2, r.top + DETAIL_TITLE_HEIGHT, STR_CONTENT_DETAIL_SUBTITLE_UNSELECTED + this->selected->state, TC_FROMSTRING, SA_CENTER);

		/* Also show the total download size, so keep some space from the bottom */
		const uint max_y = r.bottom - FONT_HEIGHT_NORMAL - WD_PAR_VSEP_WIDE;
		int y = r.top + DETAIL_TITLE_HEIGHT + DETAIL_TOP;

		if (this->selected->upgrade) {
			SetDParam(0, STR_CONTENT_TYPE_BASE_GRAPHICS + this->selected->type - CONTENT_TYPE_BASE_GRAPHICS);
			y = DrawStringMultiLine(r.left + DETAIL_LEFT, r.right - DETAIL_RIGHT, y, max_y, STR_CONTENT_DETAIL_UPDATE);
			y += WD_PAR_VSEP_WIDE;
		}

		SetDParamStr(0, this->selected->name);
		y = DrawStringMultiLine(r.left + DETAIL_LEFT, r.right - DETAIL_RIGHT, y, max_y, STR_CONTENT_DETAIL_NAME);

		if (!StrEmpty(this->selected->version)) {
			SetDParamStr(0, this->selected->version);
			y = DrawStringMultiLine(r.left + DETAIL_LEFT, r.right - DETAIL_RIGHT, y, max_y, STR_CONTENT_DETAIL_VERSION);
		}

		if (!StrEmpty(this->selected->description)) {
			SetDParamStr(0, this->selected->description);
			y = DrawStringMultiLine(r.left + DETAIL_LEFT, r.right - DETAIL_RIGHT, y, max_y, STR_CONTENT_DETAIL_DESCRIPTION);
		}

		if (!StrEmpty(this->selected->url)) {
			SetDParamStr(0, this->selected->url);
			y = DrawStringMultiLine(r.left + DETAIL_LEFT, r.right - DETAIL_RIGHT, y, max_y, STR_CONTENT_DETAIL_URL);
		}

		SetDParam(0, STR_CONTENT_TYPE_BASE_GRAPHICS + this->selected->type - CONTENT_TYPE_BASE_GRAPHICS);
		y = DrawStringMultiLine(r.left + DETAIL_LEFT, r.right - DETAIL_RIGHT, y, max_y, STR_CONTENT_DETAIL_TYPE);

		y += WD_PAR_VSEP_WIDE;
		SetDParam(0, this->selected->filesize);
		y = DrawStringMultiLine(r.left + DETAIL_LEFT, r.right - DETAIL_RIGHT, y, max_y, STR_CONTENT_DETAIL_FILESIZE);

		if (this->selected->dependency_count != 0) {
			/* List dependencies */
			char buf[DRAW_STRING_BUFFER] = "";
			char *p = buf;
			for (uint i = 0; i < this->selected->dependency_count; i++) {
				ContentID cid = this->selected->dependencies[i];

				/* Try to find the dependency */
				ConstContentIterator iter = _network_content_client.Begin();
				for (; iter != _network_content_client.End(); iter++) {
					const ContentInfo *ci = *iter;
					if (ci->id != cid) continue;

					p += seprintf(p, lastof(buf), p == buf ? "%s" : ", %s", (*iter)->name);
					break;
				}
			}
			SetDParamStr(0, buf);
			y = DrawStringMultiLine(r.left + DETAIL_LEFT, r.right - DETAIL_RIGHT, y, max_y, STR_CONTENT_DETAIL_DEPENDENCIES);
		}

		if (this->selected->tag_count != 0) {
			/* List all tags */
			char buf[DRAW_STRING_BUFFER] = "";
			char *p = buf;
			for (uint i = 0; i < this->selected->tag_count; i++) {
				p += seprintf(p, lastof(buf), i == 0 ? "%s" : ", %s", this->selected->tags[i]);
			}
			SetDParamStr(0, buf);
			y = DrawStringMultiLine(r.left + DETAIL_LEFT, r.right - DETAIL_RIGHT, y, max_y, STR_CONTENT_DETAIL_TAGS);
		}

		if (this->selected->IsSelected()) {
			/* When selected show all manually selected content that depends on this */
			ConstContentVector tree;
			_network_content_client.ReverseLookupTreeDependency(tree, this->selected);

			char buf[DRAW_STRING_BUFFER] = "";
			char *p = buf;
			for (const ContentInfo *ci : tree) {
				if (ci == this->selected || ci->state != ContentInfo::SELECTED) continue;

				p += seprintf(p, lastof(buf), buf == p ? "%s" : ", %s", ci->name);
			}
			if (p != buf) {
				SetDParamStr(0, buf);
				y = DrawStringMultiLine(r.left + DETAIL_LEFT, r.right - DETAIL_RIGHT, y, max_y, STR_CONTENT_DETAIL_SELECTED_BECAUSE_OF);
			}
		}
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		if (widget >= WID_NCL_TEXTFILE && widget < WID_NCL_TEXTFILE + TFT_END) {
			if (this->selected == nullptr || this->selected->state != ContentInfo::ALREADY_HERE) return;

			ShowContentTextfileWindow((TextfileType)(widget - WID_NCL_TEXTFILE), this->selected);
			return;
		}

		switch (widget) {
			case WID_NCL_MATRIX: {
				uint id_v = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_NCL_MATRIX);
				if (id_v >= this->content.size()) return; // click out of bounds

				this->selected = this->content[id_v];
				this->list_pos = id_v;

				const NWidgetBase *checkbox = this->GetWidget<NWidgetBase>(WID_NCL_CHECKBOX);
				if (click_count > 1 || IsInsideBS(pt.x, checkbox->pos_x, checkbox->current_x)) {
					_network_content_client.ToggleSelectedState(this->selected);
					this->content.ForceResort();
				}

				if (this->filter_data.types.any()) {
					this->content.ForceRebuild();
				}

				this->InvalidateData();
				break;
			}

			case WID_NCL_CHECKBOX:
			case WID_NCL_TYPE:
			case WID_NCL_NAME:
				if (this->content.SortType() == widget - WID_NCL_CHECKBOX) {
					this->content.ToggleSortOrder();
					if (this->content.size() > 0) this->list_pos = (int)this->content.size() - this->list_pos - 1;
				} else {
					this->content.SetSortType(widget - WID_NCL_CHECKBOX);
					this->content.ForceResort();
					this->SortContentList();
				}
				this->ScrollToSelected();
				this->InvalidateData();
				break;

			case WID_NCL_SELECT_ALL:
				_network_content_client.SelectAll();
				this->InvalidateData();
				break;

			case WID_NCL_SELECT_UPDATE:
				_network_content_client.SelectUpgrade();
				this->InvalidateData();
				break;

			case WID_NCL_UNSELECT:
				_network_content_client.UnselectAll();
				this->InvalidateData();
				break;

			case WID_NCL_CANCEL:
				delete this;
				break;

			case WID_NCL_OPEN_URL:
				if (this->selected != nullptr) {
					extern void OpenBrowser(const char *url);
					OpenBrowser(this->selected->url);
				}
				break;

			case WID_NCL_DOWNLOAD:
				if (BringWindowToFrontById(WC_NETWORK_STATUS_WINDOW, WN_NETWORK_STATUS_WINDOW_CONTENT_DOWNLOAD) == nullptr) new NetworkContentDownloadStatusWindow();
				break;

			case WID_NCL_SEARCH_EXTERNAL:
				if (_accepted_external_search) {
					this->OpenExternalSearch();
				} else {
					ShowQuery(STR_CONTENT_SEARCH_EXTERNAL_DISCLAIMER_CAPTION, STR_CONTENT_SEARCH_EXTERNAL_DISCLAIMER, this, ExternalSearchDisclaimerCallback);
				}
				break;
		}
	}

	EventState OnKeyPress(WChar key, uint16 keycode) override
	{
		switch (keycode) {
			case WKC_UP:
				/* scroll up by one */
				if (this->list_pos > 0) this->list_pos--;
				break;
			case WKC_DOWN:
				/* scroll down by one */
				if (this->list_pos < (int)this->content.size() - 1) this->list_pos++;
				break;
			case WKC_PAGEUP:
				/* scroll up a page */
				this->list_pos = (this->list_pos < (int)this->vscroll->GetCapacity()) ? 0 : this->list_pos - this->vscroll->GetCapacity();
				break;
			case WKC_PAGEDOWN:
				/* scroll down a page */
				this->list_pos = std::min<int>(this->list_pos + this->vscroll->GetCapacity(), this->content.size() - 1);
				break;
			case WKC_HOME:
				/* jump to beginning */
				this->list_pos = 0;
				break;
			case WKC_END:
				/* jump to end */
				this->list_pos = (int)this->content.size() - 1;
				break;

			case WKC_SPACE:
			case WKC_RETURN:
				if (keycode == WKC_RETURN || !IsWidgetFocused(WID_NCL_FILTER)) {
					if (this->selected != nullptr) {
						_network_content_client.ToggleSelectedState(this->selected);
						this->content.ForceResort();
						this->InvalidateData();
					}
					if (this->filter_data.types.any()) {
						this->content.ForceRebuild();
						this->InvalidateData();
					}
					return ES_HANDLED;
				}
				/* space is pressed and filter is focused. */
				FALLTHROUGH;

			default:
				return ES_NOT_HANDLED;
		}

		if (this->content.size() == 0) {
			this->list_pos = 0; // above stuff may result in "-1".
			if (this->UpdateFilterState()) {
				this->content.ForceRebuild();
				this->InvalidateData();
			}
			return ES_HANDLED;
		}

		this->selected = this->content[this->list_pos];

		if (this->UpdateFilterState()) {
			this->content.ForceRebuild();
		} else {
			/* Scroll to the new content if it is outside the current range. */
			this->ScrollToSelected();
		}

		/* redraw window */
		this->InvalidateData();
		return ES_HANDLED;
	}

	void OnEditboxChanged(int wid) override
	{
		if (wid == WID_NCL_FILTER) {
			this->filter_data.string_filter.SetFilterTerm(this->filter_editbox.text.buf);
			this->UpdateFilterState();
			this->content.ForceRebuild();
			this->InvalidateData();
		}
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_NCL_MATRIX);
	}

	void OnReceiveContentInfo(const ContentInfo *rci) override
	{
		if (this->auto_select && !rci->IsSelected()) _network_content_client.ToggleSelectedState(rci);
		this->content.ForceRebuild();
		this->InvalidateData(0, false);
	}

	void OnDownloadComplete(ContentID cid) override
	{
		this->content.ForceResort();
		this->InvalidateData();
	}

	void OnConnect(bool success) override
	{
		if (!success) {
			ShowErrorMessage(STR_CONTENT_ERROR_COULD_NOT_CONNECT, INVALID_STRING_ID, WL_ERROR);
			delete this;
			return;
		}

		this->InvalidateData();
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (!gui_scope) return;
		if (this->content.NeedRebuild()) this->BuildContentList();

		/* To sum all the bytes we intend to download */
		this->filesize_sum = 0;
		bool show_select_all = false;
		bool show_select_upgrade = false;
		for (const ContentInfo *ci : this->content) {
			switch (ci->state) {
				case ContentInfo::SELECTED:
				case ContentInfo::AUTOSELECTED:
					this->filesize_sum += ci->filesize;
					break;

				case ContentInfo::UNSELECTED:
					show_select_all = true;
					show_select_upgrade |= ci->upgrade;
					break;

				default:
					break;
			}
		}

		/* If data == 2 then the status window caused this OnInvalidate */
		this->SetWidgetDisabledState(WID_NCL_DOWNLOAD, this->filesize_sum == 0 || (FindWindowById(WC_NETWORK_STATUS_WINDOW, WN_NETWORK_STATUS_WINDOW_CONTENT_DOWNLOAD) != nullptr && data != 2));
		this->SetWidgetDisabledState(WID_NCL_UNSELECT, this->filesize_sum == 0);
		this->SetWidgetDisabledState(WID_NCL_SELECT_ALL, !show_select_all);
		this->SetWidgetDisabledState(WID_NCL_SELECT_UPDATE, !show_select_upgrade);
		this->SetWidgetDisabledState(WID_NCL_OPEN_URL, this->selected == nullptr || StrEmpty(this->selected->url));
		for (TextfileType tft = TFT_BEGIN; tft < TFT_END; tft++) {
			this->SetWidgetDisabledState(WID_NCL_TEXTFILE + tft, this->selected == nullptr || this->selected->state != ContentInfo::ALREADY_HERE || this->selected->GetTextfile(tft) == nullptr);
		}

		this->GetWidget<NWidgetCore>(WID_NCL_CANCEL)->widget_data = this->filesize_sum == 0 ? STR_AI_SETTINGS_CLOSE : STR_AI_LIST_CANCEL;
	}
};

Listing NetworkContentListWindow::last_sorting = {false, 1};
Filtering NetworkContentListWindow::last_filtering = {false, 0};

NetworkContentListWindow::GUIContentList::SortFunction * const NetworkContentListWindow::sorter_funcs[] = {
	&StateSorter,
	&TypeSorter,
	&NameSorter,
};

NetworkContentListWindow::GUIContentList::FilterFunction * const NetworkContentListWindow::filter_funcs[] = {
	&TagNameFilter,
	&TypeOrSelectedFilter,
};

char NetworkContentListWindow::content_type_strs[CONTENT_TYPE_END][64];

/**
 * Build array of all strings corresponding to the content types.
 */
void BuildContentTypeStringList()
{
	for (int i = CONTENT_TYPE_BEGIN; i < CONTENT_TYPE_END; i++) {
		GetString(NetworkContentListWindow::content_type_strs[i], STR_CONTENT_TYPE_BASE_GRAPHICS + i - CONTENT_TYPE_BASE_GRAPHICS, lastof(NetworkContentListWindow::content_type_strs[i]));
	}
}

/** The widgets for the content list. */
static const NWidgetPart _nested_network_content_list_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_LIGHT_BLUE),
		NWidget(WWT_CAPTION, COLOUR_LIGHT_BLUE), SetDataTip(STR_CONTENT_TITLE, STR_NULL),
		NWidget(WWT_DEFSIZEBOX, COLOUR_LIGHT_BLUE),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_LIGHT_BLUE, WID_NCL_BACKGROUND),
		NWidget(NWID_SPACER), SetMinimalSize(0, 7), SetResize(1, 0),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(8, 8, 8),
			/* Top */
			NWidget(WWT_EMPTY, COLOUR_LIGHT_BLUE, WID_NCL_FILTER_CAPT), SetFill(1, 0), SetResize(1, 0),
			NWidget(WWT_EDITBOX, COLOUR_LIGHT_BLUE, WID_NCL_FILTER), SetFill(1, 0), SetResize(1, 0),
						SetDataTip(STR_LIST_FILTER_OSKTITLE, STR_LIST_FILTER_TOOLTIP),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 7), SetResize(1, 0),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(8, 8, 8),
			/* Left side. */
			NWidget(NWID_VERTICAL), SetPIP(0, 4, 0),
				NWidget(NWID_HORIZONTAL),
					NWidget(NWID_VERTICAL),
						NWidget(NWID_HORIZONTAL),
							NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NCL_CHECKBOX), SetMinimalSize(13, 1), SetDataTip(STR_EMPTY, STR_NULL),
							NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NCL_TYPE),
											SetDataTip(STR_CONTENT_TYPE_CAPTION, STR_CONTENT_TYPE_CAPTION_TOOLTIP),
							NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NCL_NAME), SetResize(1, 0), SetFill(1, 0),
											SetDataTip(STR_CONTENT_NAME_CAPTION, STR_CONTENT_NAME_CAPTION_TOOLTIP),
						EndContainer(),
						NWidget(WWT_MATRIX, COLOUR_LIGHT_BLUE, WID_NCL_MATRIX), SetResize(1, 14), SetFill(1, 1), SetScrollbar(WID_NCL_SCROLLBAR), SetMatrixDataTip(1, 0, STR_CONTENT_MATRIX_TOOLTIP),
					EndContainer(),
					NWidget(NWID_VSCROLLBAR, COLOUR_LIGHT_BLUE, WID_NCL_SCROLLBAR),
				EndContainer(),
				NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(0, 8, 0),
					NWidget(NWID_SELECTION, INVALID_COLOUR, WID_NCL_SEL_ALL_UPDATE), SetResize(1, 0), SetFill(1, 0),
						NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NCL_SELECT_UPDATE), SetResize(1, 0), SetFill(1, 0),
												SetDataTip(STR_CONTENT_SELECT_UPDATES_CAPTION, STR_CONTENT_SELECT_UPDATES_CAPTION_TOOLTIP),
						NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NCL_SELECT_ALL), SetResize(1, 0), SetFill(1, 0),
												SetDataTip(STR_CONTENT_SELECT_ALL_CAPTION, STR_CONTENT_SELECT_ALL_CAPTION_TOOLTIP),
					EndContainer(),
					NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NCL_UNSELECT), SetResize(1, 0), SetFill(1, 0),
												SetDataTip(STR_CONTENT_UNSELECT_ALL_CAPTION, STR_CONTENT_UNSELECT_ALL_CAPTION_TOOLTIP),
				EndContainer(),
			EndContainer(),
			/* Right side. */
			NWidget(NWID_VERTICAL), SetPIP(0, 4, 0),
				NWidget(WWT_PANEL, COLOUR_LIGHT_BLUE, WID_NCL_DETAILS), SetResize(1, 1), SetFill(1, 1), EndContainer(),
				NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(0, 8, 0),
					NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NCL_TEXTFILE + TFT_README), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_TEXTFILE_VIEW_README, STR_NULL),
					NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NCL_TEXTFILE + TFT_CHANGELOG), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_TEXTFILE_VIEW_CHANGELOG, STR_NULL),
				EndContainer(),
				NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(0, 8, 0),
					NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NCL_OPEN_URL), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_CONTENT_OPEN_URL, STR_CONTENT_OPEN_URL_TOOLTIP),
					NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NCL_TEXTFILE + TFT_LICENSE), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_TEXTFILE_VIEW_LICENCE, STR_NULL),
				EndContainer(),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 7), SetResize(1, 0),
		/* Bottom. */
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(8, 8, 8),
			NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NCL_SEARCH_EXTERNAL), SetResize(1, 0), SetFill(1, 0),
										SetDataTip(STR_CONTENT_SEARCH_EXTERNAL, STR_CONTENT_SEARCH_EXTERNAL_TOOLTIP),
			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(0, 8, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NCL_CANCEL), SetResize(1, 0), SetFill(1, 0),
											SetDataTip(STR_BUTTON_CANCEL, STR_NULL),
				NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, WID_NCL_DOWNLOAD), SetResize(1, 0), SetFill(1, 0),
											SetDataTip(STR_CONTENT_DOWNLOAD_CAPTION, STR_CONTENT_DOWNLOAD_CAPTION_TOOLTIP),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 2), SetResize(1, 0),
		/* Resize button. */
		NWidget(NWID_HORIZONTAL),
			NWidget(NWID_SPACER), SetFill(1, 0), SetResize(1, 0),
			NWidget(WWT_RESIZEBOX, COLOUR_LIGHT_BLUE),
		EndContainer(),
	EndContainer(),
};

/** Window description of the content list */
static WindowDesc _network_content_list_desc(
	WDP_CENTER, "list_content", 630, 460,
	WC_NETWORK_WINDOW, WC_NONE,
	0,
	_nested_network_content_list_widgets, lengthof(_nested_network_content_list_widgets)
);

/**
 * Show the content list window with a given set of content
 * @param cv the content to show, or nullptr when it has to search for itself
 * @param type1 the first type to (only) show or #CONTENT_TYPE_END to show all.
 * @param type2 the second type to (only) show in addition to type1. If type2 is != #CONTENT_TYPE_END, then also type1 should be != #CONTENT_TYPE_END.
 *   If type2 != #CONTENT_TYPE_END, then type1 != type2 must be true.
 */
void ShowNetworkContentListWindow(ContentVector *cv, ContentType type1, ContentType type2)
{
#if defined(WITH_ZLIB)
	std::bitset<CONTENT_TYPE_END> types;
	_network_content_client.Clear();
	if (cv == nullptr) {
		assert(type1 != CONTENT_TYPE_END || type2 == CONTENT_TYPE_END);
		assert(type1 == CONTENT_TYPE_END || type1 != type2);
		_network_content_client.RequestContentList(type1);
		if (type2 != CONTENT_TYPE_END) _network_content_client.RequestContentList(type2);

		if (type1 != CONTENT_TYPE_END) types[type1] = true;
		if (type2 != CONTENT_TYPE_END) types[type2] = true;
	} else {
		_network_content_client.RequestContentList(cv, true);
	}

	DeleteWindowById(WC_NETWORK_WINDOW, WN_NETWORK_WINDOW_CONTENT_LIST);
	new NetworkContentListWindow(&_network_content_list_desc, cv != nullptr, types);
#else
	ShowErrorMessage(STR_CONTENT_NO_ZLIB, STR_CONTENT_NO_ZLIB_SUB, WL_ERROR);
	/* Connection failed... clean up the mess */
	if (cv != nullptr) {
		for (ContentInfo *ci : *cv) delete ci;
	}
#endif /* WITH_ZLIB */
}
