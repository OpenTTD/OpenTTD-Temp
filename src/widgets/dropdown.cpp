/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file dropdown.cpp Implementation of the dropdown widget. */

#include "../stdafx.h"
#include "../window_gui.h"
#include "../string_func.h"
#include "../strings_func.h"
#include "../window_func.h"
#include "../guitimer_func.h"
#include "dropdown_type.h"

#include "dropdown_widget.h"

#include "../safeguards.h"


void DropDownListItem::Draw(int left, int right, int top, int bottom, bool sel, Colours bg_colour) const
{
	int c1 = _colour_gradient[bg_colour][3];
	int c2 = _colour_gradient[bg_colour][7];

	int mid = top + this->Height(0) / 2;
	GfxFillRect(left + 1, mid - 2, right - 1, mid - 2, c1);
	GfxFillRect(left + 1, mid - 1, right - 1, mid - 1, c2);
}

uint DropDownListStringItem::Width() const
{
	char buffer[512];
	GetString(buffer, this->String(), lastof(buffer));
	return GetStringBoundingBox(buffer).width;
}

void DropDownListStringItem::Draw(int left, int right, int top, int bottom, bool sel, Colours bg_colour) const
{
	DrawString(left + WD_FRAMERECT_LEFT, right - WD_FRAMERECT_RIGHT, top, this->String(), sel ? TC_WHITE : TC_BLACK);
}

/**
 * Natural sorting comparator function for DropDownList::sort().
 * @param first Left side of comparison.
 * @param second Right side of comparison.
 * @return true if \a first precedes \a second.
 * @warning All items in the list need to be derivates of DropDownListStringItem.
 */
/* static */ bool DropDownListStringItem::NatSortFunc(std::unique_ptr<const DropDownListItem> const &first, std::unique_ptr<const DropDownListItem> const &second)
{
	char buffer1[512], buffer2[512];
	GetString(buffer1, static_cast<const DropDownListStringItem*>(first.get())->String(), lastof(buffer1));
	GetString(buffer2, static_cast<const DropDownListStringItem*>(second.get())->String(), lastof(buffer2));
	return strnatcmp(buffer1, buffer2) < 0;
}

StringID DropDownListParamStringItem::String() const
{
	for (uint i = 0; i < lengthof(this->decode_params); i++) SetDParam(i, this->decode_params[i]);
	return this->string;
}

StringID DropDownListCharStringItem::String() const
{
	SetDParamStr(0, this->raw_string.c_str());
	return this->string;
}

DropDownListIconItem::DropDownListIconItem(SpriteID sprite, PaletteID pal, StringID string, int result, bool masked) : DropDownListParamStringItem(string, result, masked), sprite(sprite), pal(pal)
{
	this->dim = GetSpriteSize(sprite);
	if (this->dim.height < (uint)FONT_HEIGHT_NORMAL) {
		this->sprite_y = (FONT_HEIGHT_NORMAL - dim.height) / 2;
		this->text_y = 0;
	} else {
		this->sprite_y = 0;
		this->text_y = (dim.height - FONT_HEIGHT_NORMAL) / 2;
	}
}

uint DropDownListIconItem::Height(uint width) const
{
	return std::max(this->dim.height, (uint)FONT_HEIGHT_NORMAL);
}

uint DropDownListIconItem::Width() const
{
	return DropDownListStringItem::Width() + this->dim.width + WD_FRAMERECT_LEFT;
}

void DropDownListIconItem::Draw(int left, int right, int top, int bottom, bool sel, Colours bg_colour) const
{
	bool rtl = _current_text_dir == TD_RTL;
	DrawSprite(this->sprite, this->pal, rtl ? right - this->dim.width - WD_FRAMERECT_RIGHT : left + WD_FRAMERECT_LEFT, top + this->sprite_y);
	DrawString(left + WD_FRAMERECT_LEFT + (rtl ? 0 : (this->dim.width + WD_FRAMERECT_LEFT)), right - WD_FRAMERECT_RIGHT - (rtl ? (this->dim.width + WD_FRAMERECT_RIGHT) : 0), top + this->text_y, this->String(), sel ? TC_WHITE : TC_BLACK);
}

void DropDownListIconItem::SetDimension(Dimension d)
{
	this->dim = d;
}

static const NWidgetPart _nested_dropdown_menu_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_END, WID_DM_ITEMS), SetMinimalSize(1, 1), SetScrollbar(WID_DM_SCROLL), EndContainer(),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_DM_SHOW_SCROLL),
			NWidget(NWID_VSCROLLBAR, COLOUR_END, WID_DM_SCROLL),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _dropdown_desc(
	WDP_MANUAL, nullptr, 0, 0,
	WC_DROPDOWN_MENU, WC_NONE,
	WDF_NO_FOCUS,
	_nested_dropdown_menu_widgets, lengthof(_nested_dropdown_menu_widgets)
);

/** Drop-down menu window */
struct DropdownWindow : Window {
	WindowClass parent_wnd_class; ///< Parent window class.
	WindowNumber parent_wnd_num;  ///< Parent window number.
	int parent_button;            ///< Parent widget number where the window is dropped from.
	const DropDownList list;      ///< List with dropdown menu items.
	int selected_index;           ///< Index of the selected item in the list.
	byte click_delay;             ///< Timer to delay selection.
	bool drag_mode;
	bool instant_close;           ///< Close the window when the mouse button is raised.
	int scrolling;                ///< If non-zero, auto-scroll the item list (one time).
	GUITimer scrolling_timer;     ///< Timer for auto-scroll of the item list.
	Point position;               ///< Position of the topleft corner of the window.
	Scrollbar *vscroll;

	/**
	 * Create a dropdown menu.
	 * @param parent        Parent window.
	 * @param list          Dropdown item list.
	 * @param selected      Index of the selected item in the list.
	 * @param button        Widget of the parent window doing the dropdown.
	 * @param instant_close Close the window when the mouse button is raised.
	 * @param position      Topleft position of the dropdown menu window.
	 * @param size          Size of the dropdown menu window.
	 * @param wi_colour     Colour of the parent widget.
	 * @param scroll        Dropdown menu has a scrollbar.
	 */
	DropdownWindow(Window *parent, DropDownList &&list, int selected, int button, bool instant_close, const Point &position, const Dimension &size, Colours wi_colour, bool scroll)
			: Window(&_dropdown_desc), list(std::move(list))
	{
		assert(this->list.size() > 0);

		this->position = position;

		this->CreateNestedTree();

		this->vscroll = this->GetScrollbar(WID_DM_SCROLL);

		uint items_width = size.width - (scroll ? NWidgetScrollbar::GetVerticalDimension().width : 0);
		NWidgetCore *nwi = this->GetWidget<NWidgetCore>(WID_DM_ITEMS);
		nwi->SetMinimalSize(items_width, size.height + 4);
		nwi->colour = wi_colour;

		nwi = this->GetWidget<NWidgetCore>(WID_DM_SCROLL);
		nwi->colour = wi_colour;

		this->GetWidget<NWidgetStacked>(WID_DM_SHOW_SCROLL)->SetDisplayedPlane(scroll ? 0 : SZSP_NONE);

		this->FinishInitNested(0);
		CLRBITS(this->flags, WF_WHITE_BORDER);

		/* Total length of list */
		int list_height = 0;
		for (const auto &item : this->list) {
			list_height += item->Height(items_width);
		}

		/* Capacity is the average number of items visible */
		this->vscroll->SetCapacity(size.height * (uint16)this->list.size() / list_height);
		this->vscroll->SetCount((uint16)this->list.size());

		this->parent_wnd_class = parent->window_class;
		this->parent_wnd_num   = parent->window_number;
		this->parent_button    = button;
		this->selected_index   = selected;
		this->click_delay      = 0;
		this->drag_mode        = true;
		this->instant_close    = instant_close;
		this->scrolling_timer  = GUITimer(MILLISECONDS_PER_TICK);
	}

	~DropdownWindow()
	{
		/* Make the dropdown "invisible", so it doesn't affect new window placement.
		 * Also mark it dirty in case the callback deals with the screen. (e.g. screenshots). */
		this->window_class = WC_INVALID;
		this->SetDirty();

		Window *w2 = FindWindowById(this->parent_wnd_class, this->parent_wnd_num);
		if (w2 != nullptr) {
			Point pt = _cursor.pos;
			pt.x -= w2->left;
			pt.y -= w2->top;
			w2->OnDropdownClose(pt, this->parent_button, this->selected_index, this->instant_close);
		}
	}

	virtual Point OnInitialPosition(int16 sm_width, int16 sm_height, int window_number)
	{
		return this->position;
	}

	/**
	 * Find the dropdown item under the cursor.
	 * @param[out] value Selected item, if function returns \c true.
	 * @return Cursor points to a dropdown item.
	 */
	bool GetDropDownItem(int &value)
	{
		if (GetWidgetFromPos(this, _cursor.pos.x - this->left, _cursor.pos.y - this->top) < 0) return false;

		NWidgetBase *nwi = this->GetWidget<NWidgetBase>(WID_DM_ITEMS);
		int y     = _cursor.pos.y - this->top - nwi->pos_y - 2;
		int width = nwi->current_x - 4;
		int pos   = this->vscroll->GetPosition();

		for (const auto &item : this->list) {
			/* Skip items that are scrolled up */
			if (--pos >= 0) continue;

			int item_height = item->Height(width);

			if (y < item_height) {
				if (item->masked || !item->Selectable()) return false;
				value = item->result;
				return true;
			}

			y -= item_height;
		}

		return false;
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		if (widget != WID_DM_ITEMS) return;

		Colours colour = this->GetWidget<NWidgetCore>(widget)->colour;

		int y = r.top + 2;
		int pos = this->vscroll->GetPosition();
		for (const auto &item : this->list) {
			int item_height = item->Height(r.right - r.left + 1);

			/* Skip items that are scrolled up */
			if (--pos >= 0) continue;

			if (y + item_height < r.bottom) {
				bool selected = (this->selected_index == item->result);
				if (selected) GfxFillRect(r.left + 2, y, r.right - 1, y + item_height - 1, PC_BLACK);

				item->Draw(r.left, r.right, y, y + item_height, selected, colour);

				if (item->masked) {
					GfxFillRect(r.left + 1, y, r.right - 1, y + item_height - 1, _colour_gradient[colour][5], FILLRECT_CHECKER);
				}
			}
			y += item_height;
		}
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		if (widget != WID_DM_ITEMS) return;
		int item;
		if (this->GetDropDownItem(item)) {
			this->click_delay = 4;
			this->selected_index = item;
			this->SetDirty();
		}
	}

	virtual void OnRealtimeTick(uint delta_ms)
	{
		if (!this->scrolling_timer.Elapsed(delta_ms)) return;
		this->scrolling_timer.SetInterval(MILLISECONDS_PER_TICK);

		if (this->scrolling != 0) {
			uint32 pos = this->vscroll->GetPosition();

			this->vscroll->UpdatePosition(this->scrolling);
			this->scrolling = 0;

			if (pos != this->vscroll->GetPosition()) {
				this->SetDirty();
			}
		}
	}

	virtual void OnMouseLoop()
	{
		Window *w2 = FindWindowById(this->parent_wnd_class, this->parent_wnd_num);
		if (w2 == nullptr) {
			delete this;
			return;
		}

		if (this->click_delay != 0 && --this->click_delay == 0) {
			/* Make the dropdown "invisible", so it doesn't affect new window placement.
			 * Also mark it dirty in case the callback deals with the screen. (e.g. screenshots). */
			this->window_class = WC_INVALID;
			this->SetDirty();

			w2->OnDropdownSelect(this->parent_button, this->selected_index);
			delete this;
			return;
		}

		if (this->drag_mode) {
			int item;

			if (!_left_button_clicked) {
				this->drag_mode = false;
				if (!this->GetDropDownItem(item)) {
					if (this->instant_close) delete this;
					return;
				}
				this->click_delay = 2;
			} else {
				if (_cursor.pos.y <= this->top + 2) {
					/* Cursor is above the list, set scroll up */
					this->scrolling = -1;
					return;
				} else if (_cursor.pos.y >= this->top + this->height - 2) {
					/* Cursor is below list, set scroll down */
					this->scrolling = 1;
					return;
				}

				if (!this->GetDropDownItem(item)) return;
			}

			if (this->selected_index != item) {
				this->selected_index = item;
				this->SetDirty();
			}
		}
	}
};

/**
 * Show a drop down list.
 * @param w        Parent window for the list.
 * @param list     Prepopulated DropDownList.
 * @param selected The initially selected list item.
 * @param button   The widget which is passed to Window::OnDropdownSelect and OnDropdownClose.
 *                 Unless you override those functions, this should be then widget index of the dropdown button.
 * @param wi_rect  Coord of the parent drop down button, used to position the dropdown menu.
 * @param auto_width The width is determined by the widest item in the list,
 *                   in this case only one of \a left or \a right is used (depending on text direction).
 * @param instant_close Set to true if releasing mouse button should close the
 *                      list regardless of where the cursor is.
 */
void ShowDropDownListAt(Window *w, DropDownList &&list, int selected, int button, Rect wi_rect, Colours wi_colour, bool auto_width, bool instant_close)
{
	DeleteWindowById(WC_DROPDOWN_MENU, 0);

	/* The preferred position is just below the dropdown calling widget */
	int top = w->top + wi_rect.bottom + 1;

	/* The preferred width equals the calling widget */
	uint width = wi_rect.right - wi_rect.left + 1;

	/* Longest item in the list, if auto_width is enabled */
	uint max_item_width = 0;

	/* Total height of list */
	uint height = 0;

	for (const auto &item : list) {
		height += item->Height(width);
		if (auto_width) max_item_width = std::max(max_item_width, item->Width() + 5);
	}

	/* Scrollbar needed? */
	bool scroll = false;

	/* Is it better to place the dropdown above the widget? */
	bool above = false;

	/* Available height below (or above, if the dropdown is placed above the widget). */
	uint available_height = std::max(GetMainViewBottom() - top - 4, 0);

	/* If the dropdown doesn't fully fit below the widget... */
	if (height > available_height) {

		uint available_height_above = std::max(w->top + wi_rect.top - GetMainViewTop() - 4, 0);

		/* Put the dropdown above if there is more available space. */
		if (available_height_above > available_height) {
			above = true;
			available_height = available_height_above;
		}

		/* If the dropdown doesn't fully fit, we need a dropdown. */
		if (height > available_height) {
			scroll = true;
			uint avg_height = height / (uint)list.size();

			/* Check at least there is space for one item. */
			assert(available_height >= avg_height);

			/* Fit the list. */
			uint rows = available_height / avg_height;
			height = rows * avg_height;

			/* Add space for the scrollbar. */
			max_item_width += NWidgetScrollbar::GetVerticalDimension().width;
		}

		/* Set the top position if needed. */
		if (above) {
			top = w->top + wi_rect.top - height - 4;
		}
	}

	if (auto_width) width = std::max(width, max_item_width);

	Point dw_pos = { w->left + (_current_text_dir == TD_RTL ? wi_rect.right + 1 - (int)width : wi_rect.left), top};
	Dimension dw_size = {width, height};
	DropdownWindow *dropdown = new DropdownWindow(w, std::move(list), selected, button, instant_close, dw_pos, dw_size, wi_colour, scroll);

	/* The dropdown starts scrolling downwards when opening it towards
	 * the top and holding down the mouse button. It can be fooled by
	 * opening the dropdown scrolled to the very bottom.  */
	if (above && scroll) dropdown->vscroll->UpdatePosition(INT_MAX);
}

/**
 * Show a drop down list.
 * @param w        Parent window for the list.
 * @param list     Prepopulated DropDownList.
 * @param selected The initially selected list item.
 * @param button   The widget within the parent window that is used to determine
 *                 the list's location.
 * @param width    Override the width determined by the selected widget.
 * @param auto_width Maximum width is determined by the widest item in the list.
 * @param instant_close Set to true if releasing mouse button should close the
 *                      list regardless of where the cursor is.
 */
void ShowDropDownList(Window *w, DropDownList &&list, int selected, int button, uint width, bool auto_width, bool instant_close)
{
	/* Our parent's button widget is used to determine where to place the drop
	 * down list window. */
	Rect wi_rect;
	NWidgetCore *nwi = w->GetWidget<NWidgetCore>(button);
	wi_rect.left   = nwi->pos_x;
	wi_rect.right  = nwi->pos_x + nwi->current_x - 1;
	wi_rect.top    = nwi->pos_y;
	wi_rect.bottom = nwi->pos_y + nwi->current_y - 1;
	Colours wi_colour = nwi->colour;

	if ((nwi->type & WWT_MASK) == NWID_BUTTON_DROPDOWN) {
		nwi->disp_flags |= ND_DROPDOWN_ACTIVE;
	} else {
		w->LowerWidget(button);
	}
	w->SetWidgetDirty(button);

	if (width != 0) {
		if (_current_text_dir == TD_RTL) {
			wi_rect.left = wi_rect.right + 1 - width;
		} else {
			wi_rect.right = wi_rect.left + width - 1;
		}
	}

	ShowDropDownListAt(w, std::move(list), selected, button, wi_rect, wi_colour, auto_width, instant_close);
}

/**
 * Show a dropdown menu window near a widget of the parent window.
 * The result code of the items is their index in the \a strings list.
 * @param w             Parent window that wants the dropdown menu.
 * @param strings       Menu list, end with #INVALID_STRING_ID
 * @param selected      Index of initial selected item.
 * @param button        Button widget number of the parent window \a w that wants the dropdown menu.
 * @param disabled_mask Bitmask for disabled items (items with their bit set are displayed, but not selectable in the dropdown list).
 * @param hidden_mask   Bitmask for hidden items (items with their bit set are not copied to the dropdown list).
 * @param width         Width of the dropdown menu. If \c 0, use the width of parent widget \a button.
 */
void ShowDropDownMenu(Window *w, const StringID *strings, int selected, int button, uint32 disabled_mask, uint32 hidden_mask, uint width)
{
	DropDownList list;

	for (uint i = 0; strings[i] != INVALID_STRING_ID; i++) {
		if (!HasBit(hidden_mask, i)) {
			list.emplace_back(new DropDownListStringItem(strings[i], i, HasBit(disabled_mask, i)));
		}
	}

	if (!list.empty()) ShowDropDownList(w, std::move(list), selected, button, width);
}

/**
 * Delete the drop-down menu from window \a pw
 * @param pw Parent window of the drop-down menu window
 * @return Parent widget number if the drop-down was found and closed, \c -1 if the window was not found.
 */
int HideDropDownMenu(Window *pw)
{
	Window *w;
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->window_class != WC_DROPDOWN_MENU) continue;

		DropdownWindow *dw = dynamic_cast<DropdownWindow*>(w);
		assert(dw != nullptr);
		if (pw->window_class == dw->parent_wnd_class &&
				pw->window_number == dw->parent_wnd_num) {
			int parent_button = dw->parent_button;
			delete dw;
			return parent_button;
		}
	}

	return -1;
}

