/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file window.cpp Windowing system, widgets and events */

#include "stdafx.h"
#include <stdarg.h>
#include "company_func.h"
#include "gfx_func.h"
#include "console_func.h"
#include "console_gui.h"
#include "viewport_func.h"
#include "progress.h"
#include "blitter/factory.hpp"
#include "zoom_func.h"
#include "vehicle_base.h"
#include "window_func.h"
#include "tilehighlight_func.h"
#include "network/network.h"
#include "querystring_gui.h"
#include "widgets/dropdown_func.h"
#include "strings_func.h"
#include "settings_type.h"
#include "settings_func.h"
#include "ini_type.h"
#include "newgrf_debug.h"
#include "hotkeys.h"
#include "toolbar_gui.h"
#include "statusbar_gui.h"
#include "error.h"
#include "game/game.hpp"
#include "video/video_driver.hpp"
#include "framerate_type.h"
#include "network/network_func.h"
#include "guitimer_func.h"
#include "news_func.h"

#include "safeguards.h"

/** Values for _settings_client.gui.auto_scrolling */
enum ViewportAutoscrolling {
	VA_DISABLED,                  //!< Do not autoscroll when mouse is at edge of viewport.
	VA_MAIN_VIEWPORT_FULLSCREEN,  //!< Scroll main viewport at edge when using fullscreen.
	VA_MAIN_VIEWPORT,             //!< Scroll main viewport at edge.
	VA_EVERY_VIEWPORT,            //!< Scroll all viewports at their edges.
};

static Point _drag_delta; ///< delta between mouse cursor and upper left corner of dragged window
static Window *_mouseover_last_w = nullptr; ///< Window of the last OnMouseOver event.
static Window *_last_scroll_window = nullptr; ///< Window of the last scroll event.

/** List of windows opened at the screen sorted from the front. */
Window *_z_front_window = nullptr;
/** List of windows opened at the screen sorted from the back. */
Window *_z_back_window  = nullptr;

/** If false, highlight is white, otherwise the by the widget defined colour. */
bool _window_highlight_colour = false;

/*
 * Window that currently has focus. - The main purpose is to generate
 * #FocusLost events, not to give next window in z-order focus when a
 * window is closed.
 */
Window *_focused_window;

Point _cursorpos_drag_start;

int _scrollbar_start_pos;
int _scrollbar_size;
byte _scroller_click_timeout = 0;

bool _scrolling_viewport;  ///< A viewport is being scrolled with the mouse.
bool _mouse_hovering;      ///< The mouse is hovering over the same point.

SpecialMouseMode _special_mouse_mode; ///< Mode of the mouse.

/**
 * List of all WindowDescs.
 * This is a pointer to ensure initialisation order with the various static WindowDesc instances.
 */
static std::vector<WindowDesc*> *_window_descs = nullptr;

/** Config file to store WindowDesc */
std::string _windows_file;

/** Window description constructor. */
WindowDesc::WindowDesc(WindowPosition def_pos, const char *ini_key, int16 def_width_trad, int16 def_height_trad,
			WindowClass window_class, WindowClass parent_class, uint32 flags,
			const NWidgetPart *nwid_parts, int16 nwid_length, HotkeyList *hotkeys) :
	default_pos(def_pos),
	cls(window_class),
	parent_cls(parent_class),
	ini_key(ini_key),
	flags(flags),
	nwid_parts(nwid_parts),
	nwid_length(nwid_length),
	hotkeys(hotkeys),
	pref_sticky(false),
	pref_width(0),
	pref_height(0),
	default_width_trad(def_width_trad),
	default_height_trad(def_height_trad)
{
	if (_window_descs == nullptr) _window_descs = new std::vector<WindowDesc*>();
	_window_descs->push_back(this);
}

WindowDesc::~WindowDesc()
{
	_window_descs->erase(std::find(_window_descs->begin(), _window_descs->end(), this));
}

/**
 * Determine default width of window.
 * This is either a stored user preferred size, or the built-in default.
 * @return Width in pixels.
 */
int16 WindowDesc::GetDefaultWidth() const
{
	return this->pref_width != 0 ? this->pref_width : ScaleGUITrad(this->default_width_trad);
}

/**
 * Determine default height of window.
 * This is either a stored user preferred size, or the built-in default.
 * @return Height in pixels.
 */
int16 WindowDesc::GetDefaultHeight() const
{
	return this->pref_height != 0 ? this->pref_height : ScaleGUITrad(this->default_height_trad);
}

/**
 * Load all WindowDesc settings from _windows_file.
 */
void WindowDesc::LoadFromConfig()
{
	IniFile *ini = new IniFile();
	ini->LoadFromDisk(_windows_file, NO_DIRECTORY);
	for (WindowDesc *wd : *_window_descs) {
		if (wd->ini_key == nullptr) continue;
		IniLoadWindowSettings(ini, wd->ini_key, wd);
	}
	delete ini;
}

/**
 * Sort WindowDesc by ini_key.
 */
static bool DescSorter(WindowDesc* const &a, WindowDesc* const &b)
{
	if (a->ini_key != nullptr && b->ini_key != nullptr) return strcmp(a->ini_key, b->ini_key) < 0;
	return a->ini_key != nullptr;
}

/**
 * Save all WindowDesc settings to _windows_file.
 */
void WindowDesc::SaveToConfig()
{
	/* Sort the stuff to get a nice ini file on first write */
	std::sort(_window_descs->begin(), _window_descs->end(), DescSorter);

	IniFile *ini = new IniFile();
	ini->LoadFromDisk(_windows_file, NO_DIRECTORY);
	for (WindowDesc *wd : *_window_descs) {
		if (wd->ini_key == nullptr) continue;
		IniSaveWindowSettings(ini, wd->ini_key, wd);
	}
	ini->SaveToDisk(_windows_file);
	delete ini;
}

/**
 * Read default values from WindowDesc configuration an apply them to the window.
 */
void Window::ApplyDefaults()
{
	if (this->nested_root != nullptr && this->nested_root->GetWidgetOfType(WWT_STICKYBOX) != nullptr) {
		if (this->window_desc->pref_sticky) this->flags |= WF_STICKY;
	} else {
		/* There is no stickybox; clear the preference in case someone tried to be funny */
		this->window_desc->pref_sticky = false;
	}
}

/**
 * Compute the row of a widget that a user clicked in.
 * @param clickpos    Vertical position of the mouse click.
 * @param widget      Widget number of the widget clicked in.
 * @param padding     Amount of empty space between the widget edge and the top of the first row.
 * @param line_height Height of a single row. A negative value means using the vertical resize step of the widget.
 * @return Row number clicked at. If clicked at a wrong position, #INT_MAX is returned.
 * @note The widget does not know where a list printed at the widget ends, so below a list is not a wrong position.
 */
int Window::GetRowFromWidget(int clickpos, int widget, int padding, int line_height) const
{
	const NWidgetBase *wid = this->GetWidget<NWidgetBase>(widget);
	if (line_height < 0) line_height = wid->resize_y;
	if (clickpos < (int)wid->pos_y + padding) return INT_MAX;
	return (clickpos - (int)wid->pos_y - padding) / line_height;
}

/**
 * Disable the highlighted status of all widgets.
 */
void Window::DisableAllWidgetHighlight()
{
	for (uint i = 0; i < this->nested_array_size; i++) {
		NWidgetBase *nwid = this->GetWidget<NWidgetBase>(i);
		if (nwid == nullptr) continue;

		if (nwid->IsHighlighted()) {
			nwid->SetHighlighted(TC_INVALID);
			this->SetWidgetDirty(i);
		}
	}

	CLRBITS(this->flags, WF_HIGHLIGHTED);
}

/**
 * Sets the highlighted status of a widget.
 * @param widget_index index of this widget in the window
 * @param highlighted_colour Colour of highlight, or TC_INVALID to disable.
 */
void Window::SetWidgetHighlight(byte widget_index, TextColour highlighted_colour)
{
	assert(widget_index < this->nested_array_size);

	NWidgetBase *nwid = this->GetWidget<NWidgetBase>(widget_index);
	if (nwid == nullptr) return;

	nwid->SetHighlighted(highlighted_colour);
	this->SetWidgetDirty(widget_index);

	if (highlighted_colour != TC_INVALID) {
		/* If we set a highlight, the window has a highlight */
		this->flags |= WF_HIGHLIGHTED;
	} else {
		/* If we disable a highlight, check all widgets if anyone still has a highlight */
		bool valid = false;
		for (uint i = 0; i < this->nested_array_size; i++) {
			NWidgetBase *nwid = this->GetWidget<NWidgetBase>(i);
			if (nwid == nullptr) continue;
			if (!nwid->IsHighlighted()) continue;

			valid = true;
		}
		/* If nobody has a highlight, disable the flag on the window */
		if (!valid) CLRBITS(this->flags, WF_HIGHLIGHTED);
	}
}

/**
 * Gets the highlighted status of a widget.
 * @param widget_index index of this widget in the window
 * @return status of the widget ie: highlighted = true, not highlighted = false
 */
bool Window::IsWidgetHighlighted(byte widget_index) const
{
	assert(widget_index < this->nested_array_size);

	const NWidgetBase *nwid = this->GetWidget<NWidgetBase>(widget_index);
	if (nwid == nullptr) return false;

	return nwid->IsHighlighted();
}

/**
 * A dropdown window associated to this window has been closed.
 * @param pt the point inside the window the mouse resides on after closure.
 * @param widget the widget (button) that the dropdown is associated with.
 * @param index the element in the dropdown that is selected.
 * @param instant_close whether the dropdown was configured to close on mouse up.
 */
void Window::OnDropdownClose(Point pt, int widget, int index, bool instant_close)
{
	if (widget < 0) return;

	if (instant_close) {
		/* Send event for selected option if we're still
		 * on the parent button of the dropdown (behaviour of the dropdowns in the main toolbar). */
		if (GetWidgetFromPos(this, pt.x, pt.y) == widget) {
			this->OnDropdownSelect(widget, index);
		}
	}

	/* Raise the dropdown button */
	NWidgetCore *nwi2 = this->GetWidget<NWidgetCore>(widget);
	if ((nwi2->type & WWT_MASK) == NWID_BUTTON_DROPDOWN) {
		nwi2->disp_flags &= ~ND_DROPDOWN_ACTIVE;
	} else {
		this->RaiseWidget(widget);
	}
	this->SetWidgetDirty(widget);
}

/**
 * Return the Scrollbar to a widget index.
 * @param widnum Scrollbar widget index
 * @return Scrollbar to the widget
 */
const Scrollbar *Window::GetScrollbar(uint widnum) const
{
	return this->GetWidget<NWidgetScrollbar>(widnum);
}

/**
 * Return the Scrollbar to a widget index.
 * @param widnum Scrollbar widget index
 * @return Scrollbar to the widget
 */
Scrollbar *Window::GetScrollbar(uint widnum)
{
	return this->GetWidget<NWidgetScrollbar>(widnum);
}

/**
 * Return the querystring associated to a editbox.
 * @param widnum Editbox widget index
 * @return QueryString or nullptr.
 */
const QueryString *Window::GetQueryString(uint widnum) const
{
	auto query = this->querystrings.Find(widnum);
	return query != this->querystrings.end() ? query->second : nullptr;
}

/**
 * Return the querystring associated to a editbox.
 * @param widnum Editbox widget index
 * @return QueryString or nullptr.
 */
QueryString *Window::GetQueryString(uint widnum)
{
	SmallMap<int, QueryString*>::Pair *query = this->querystrings.Find(widnum);
	return query != this->querystrings.End() ? query->second : nullptr;
}

/**
 * Get the current input text if an edit box has the focus.
 * @return The currently focused input text or nullptr if no input focused.
 */
/* virtual */ const char *Window::GetFocusedText() const
{
	if (this->nested_focus != nullptr && this->nested_focus->type == WWT_EDITBOX) {
		return this->GetQueryString(this->nested_focus->index)->GetText();
	}

	return nullptr;
}

/**
 * Get the string at the caret if an edit box has the focus.
 * @return The text at the caret or nullptr if no edit box is focused.
 */
/* virtual */ const char *Window::GetCaret() const
{
	if (this->nested_focus != nullptr && this->nested_focus->type == WWT_EDITBOX) {
		return this->GetQueryString(this->nested_focus->index)->GetCaret();
	}

	return nullptr;
}

/**
 * Get the range of the currently marked input text.
 * @param[out] length Length of the marked text.
 * @return Pointer to the start of the marked text or nullptr if no text is marked.
 */
/* virtual */ const char *Window::GetMarkedText(size_t *length) const
{
	if (this->nested_focus != nullptr && this->nested_focus->type == WWT_EDITBOX) {
		return this->GetQueryString(this->nested_focus->index)->GetMarkedText(length);
	}

	return nullptr;
}

/**
 * Get the current caret position if an edit box has the focus.
 * @return Top-left location of the caret, relative to the window.
 */
/* virtual */ Point Window::GetCaretPosition() const
{
	if (this->nested_focus != nullptr && this->nested_focus->type == WWT_EDITBOX && !this->querystrings.empty()) {
		return this->GetQueryString(this->nested_focus->index)->GetCaretPosition(this, this->nested_focus->index);
	}

	Point pt = {0, 0};
	return pt;
}

/**
 * Get the bounding rectangle for a text range if an edit box has the focus.
 * @param from Start of the string range.
 * @param to End of the string range.
 * @return Rectangle encompassing the string range, relative to the window.
 */
/* virtual */ Rect Window::GetTextBoundingRect(const char *from, const char *to) const
{
	if (this->nested_focus != nullptr && this->nested_focus->type == WWT_EDITBOX) {
		return this->GetQueryString(this->nested_focus->index)->GetBoundingRect(this, this->nested_focus->index, from, to);
	}

	Rect r = {0, 0, 0, 0};
	return r;
}

/**
 * Get the character that is rendered at a position by the focused edit box.
 * @param pt The position to test.
 * @return Pointer to the character at the position or nullptr if no character is at the position.
 */
/* virtual */ const char *Window::GetTextCharacterAtPosition(const Point &pt) const
{
	if (this->nested_focus != nullptr && this->nested_focus->type == WWT_EDITBOX) {
		return this->GetQueryString(this->nested_focus->index)->GetCharAtPosition(this, this->nested_focus->index, pt);
	}

	return nullptr;
}

/**
 * Set the window that has the focus
 * @param w The window to set the focus on
 */
void SetFocusedWindow(Window *w)
{
	if (_focused_window == w) return;

	/* Invalidate focused widget */
	if (_focused_window != nullptr) {
		if (_focused_window->nested_focus != nullptr) _focused_window->nested_focus->SetDirty(_focused_window);
	}

	/* Remember which window was previously focused */
	Window *old_focused = _focused_window;
	_focused_window = w;

	/* So we can inform it that it lost focus */
	if (old_focused != nullptr) old_focused->OnFocusLost();
	if (_focused_window != nullptr) _focused_window->OnFocus();
}

/**
 * Check if an edit box is in global focus. That is if focused window
 * has a edit box as focused widget, or if a console is focused.
 * @return returns true if an edit box is in global focus or if the focused window is a console, else false
 */
bool EditBoxInGlobalFocus()
{
	if (_focused_window == nullptr) return false;

	/* The console does not have an edit box so a special case is needed. */
	if (_focused_window->window_class == WC_CONSOLE) return true;

	return _focused_window->nested_focus != nullptr && _focused_window->nested_focus->type == WWT_EDITBOX;
}

/**
 * Check if a console is focused.
 * @return returns true if the focused window is a console, else false
 */
bool FocusedWindowIsConsole()
{
	return _focused_window && _focused_window->window_class == WC_CONSOLE;
}

/**
 * Makes no widget on this window have focus. The function however doesn't change which window has focus.
 */
void Window::UnfocusFocusedWidget()
{
	if (this->nested_focus != nullptr) {
		if (this->nested_focus->type == WWT_EDITBOX) VideoDriver::GetInstance()->EditBoxLostFocus();

		/* Repaint the widget that lost focus. A focused edit box may else leave the caret on the screen. */
		this->nested_focus->SetDirty(this);
		this->nested_focus = nullptr;
	}
}

/**
 * Set focus within this window to the given widget. The function however doesn't change which window has focus.
 * @param widget_index Index of the widget in the window to set the focus to.
 * @return Focus has changed.
 */
bool Window::SetFocusedWidget(int widget_index)
{
	/* Do nothing if widget_index is already focused, or if it wasn't a valid widget. */
	if ((uint)widget_index >= this->nested_array_size) return false;

	assert(this->nested_array[widget_index] != nullptr); // Setting focus to a non-existing widget is a bad idea.
	if (this->nested_focus != nullptr) {
		if (this->GetWidget<NWidgetCore>(widget_index) == this->nested_focus) return false;

		/* Repaint the widget that lost focus. A focused edit box may else leave the caret on the screen. */
		this->nested_focus->SetDirty(this);
		if (this->nested_focus->type == WWT_EDITBOX) VideoDriver::GetInstance()->EditBoxLostFocus();
	}
	this->nested_focus = this->GetWidget<NWidgetCore>(widget_index);
	if (this->nested_focus->type == WWT_EDITBOX) VideoDriver::GetInstance()->EditBoxGainedFocus();
	return true;
}

/**
 * Called when window gains focus
 */
void Window::OnFocus()
{
	if (this->nested_focus != nullptr && this->nested_focus->type == WWT_EDITBOX) VideoDriver::GetInstance()->EditBoxGainedFocus();
}

/**
 * Called when window loses focus
 */
void Window::OnFocusLost()
{
	if (this->nested_focus != nullptr && this->nested_focus->type == WWT_EDITBOX) VideoDriver::GetInstance()->EditBoxLostFocus();
}

/**
 * Sets the enabled/disabled status of a list of widgets.
 * By default, widgets are enabled.
 * On certain conditions, they have to be disabled.
 * @param disab_stat status to use ie: disabled = true, enabled = false
 * @param widgets list of widgets ended by WIDGET_LIST_END
 */
void CDECL Window::SetWidgetsDisabledState(bool disab_stat, int widgets, ...)
{
	va_list wdg_list;

	va_start(wdg_list, widgets);

	while (widgets != WIDGET_LIST_END) {
		SetWidgetDisabledState(widgets, disab_stat);
		widgets = va_arg(wdg_list, int);
	}

	va_end(wdg_list);
}

/**
 * Sets the lowered/raised status of a list of widgets.
 * @param lowered_stat status to use ie: lowered = true, raised = false
 * @param widgets list of widgets ended by WIDGET_LIST_END
 */
void CDECL Window::SetWidgetsLoweredState(bool lowered_stat, int widgets, ...)
{
	va_list wdg_list;

	va_start(wdg_list, widgets);

	while (widgets != WIDGET_LIST_END) {
		SetWidgetLoweredState(widgets, lowered_stat);
		widgets = va_arg(wdg_list, int);
	}

	va_end(wdg_list);
}

/**
 * Raise the buttons of the window.
 * @param autoraise Raise only the push buttons of the window.
 */
void Window::RaiseButtons(bool autoraise)
{
	for (uint i = 0; i < this->nested_array_size; i++) {
		if (this->nested_array[i] == nullptr) continue;
		WidgetType type = this->nested_array[i]->type;
		if (((type & ~WWB_PUSHBUTTON) < WWT_LAST || type == NWID_PUSHBUTTON_DROPDOWN) &&
				(!autoraise || (type & WWB_PUSHBUTTON) || type == WWT_EDITBOX) && this->IsWidgetLowered(i)) {
			this->RaiseWidget(i);
			this->SetWidgetDirty(i);
		}
	}

	/* Special widgets without widget index */
	NWidgetCore *wid = this->nested_root != nullptr ? (NWidgetCore*)this->nested_root->GetWidgetOfType(WWT_DEFSIZEBOX) : nullptr;
	if (wid != nullptr) {
		wid->SetLowered(false);
		wid->SetDirty(this);
	}
}

/**
 * Invalidate a widget, i.e. mark it as being changed and in need of redraw.
 * @param widget_index the widget to redraw.
 */
void Window::SetWidgetDirty(byte widget_index) const
{
	/* Sometimes this function is called before the window is even fully initialized */
	if (this->nested_array == nullptr) return;

	this->nested_array[widget_index]->SetDirty(this);
}

/**
 * A hotkey has been pressed.
 * @param hotkey  Hotkey index, by default a widget index of a button or editbox.
 * @return #ES_HANDLED if the key press has been handled, and the hotkey is not unavailable for some reason.
 */
EventState Window::OnHotkey(int hotkey)
{
	if (hotkey < 0) return ES_NOT_HANDLED;

	NWidgetCore *nw = this->GetWidget<NWidgetCore>(hotkey);
	if (nw == nullptr || nw->IsDisabled()) return ES_NOT_HANDLED;

	if (nw->type == WWT_EDITBOX) {
		if (this->IsShaded()) return ES_NOT_HANDLED;

		/* Focus editbox */
		this->SetFocusedWidget(hotkey);
		SetFocusedWindow(this);
	} else {
		/* Click button */
		this->OnClick(Point(), hotkey, 1);
	}
	return ES_HANDLED;
}

/**
 * Do all things to make a button look clicked and mark it to be
 * unclicked in a few ticks.
 * @param widget the widget to "click"
 */
void Window::HandleButtonClick(byte widget)
{
	this->LowerWidget(widget);
	this->SetTimeout();
	this->SetWidgetDirty(widget);
}

static void StartWindowDrag(Window *w);
static void StartWindowSizing(Window *w, bool to_left);

/**
 * Dispatch left mouse-button (possibly double) click in window.
 * @param w Window to dispatch event in
 * @param x X coordinate of the click
 * @param y Y coordinate of the click
 * @param click_count Number of fast consecutive clicks at same position
 */
static void DispatchLeftClickEvent(Window *w, int x, int y, int click_count)
{
	NWidgetCore *nw = w->nested_root->GetWidgetFromPos(x, y);
	WidgetType widget_type = (nw != nullptr) ? nw->type : WWT_EMPTY;

	bool focused_widget_changed = false;
	/* If clicked on a window that previously did not have focus */
	if (_focused_window != w &&                 // We already have focus, right?
			(w->window_desc->flags & WDF_NO_FOCUS) == 0 &&  // Don't lose focus to toolbars
			widget_type != WWT_CLOSEBOX) {          // Don't change focused window if 'X' (close button) was clicked
		focused_widget_changed = true;
		SetFocusedWindow(w);
	}

	if (nw == nullptr) return; // exit if clicked outside of widgets

	/* don't allow any interaction if the button has been disabled */
	if (nw->IsDisabled()) return;

	int widget_index = nw->index; ///< Index of the widget

	/* Clicked on a widget that is not disabled.
	 * So unless the clicked widget is the caption bar, change focus to this widget.
	 * Exception: In the OSK we always want the editbox to stay focused. */
	if (widget_type != WWT_CAPTION && w->window_class != WC_OSK) {
		/* focused_widget_changed is 'now' only true if the window this widget
		 * is in gained focus. In that case it must remain true, also if the
		 * local widget focus did not change. As such it's the logical-or of
		 * both changed states.
		 *
		 * If this is not preserved, then the OSK window would be opened when
		 * a user has the edit box focused and then click on another window and
		 * then back again on the edit box (to type some text).
		 */
		focused_widget_changed |= w->SetFocusedWidget(widget_index);
	}

	/* Close any child drop down menus. If the button pressed was the drop down
	 * list's own button, then we should not process the click any further. */
	if (HideDropDownMenu(w) == widget_index && widget_index >= 0) return;

	if ((widget_type & ~WWB_PUSHBUTTON) < WWT_LAST && (widget_type & WWB_PUSHBUTTON)) w->HandleButtonClick(widget_index);

	Point pt = { x, y };

	switch (widget_type) {
		case NWID_VSCROLLBAR:
		case NWID_HSCROLLBAR:
			ScrollbarClickHandler(w, nw, x, y);
			break;

		case WWT_EDITBOX: {
			QueryString *query = w->GetQueryString(widget_index);
			if (query != nullptr) query->ClickEditBox(w, pt, widget_index, click_count, focused_widget_changed);
			break;
		}

		case WWT_CLOSEBOX: // 'X'
			delete w;
			return;

		case WWT_CAPTION: // 'Title bar'
			StartWindowDrag(w);
			return;

		case WWT_RESIZEBOX:
			/* When the resize widget is on the left size of the window
			 * we assume that that button is used to resize to the left. */
			StartWindowSizing(w, (int)nw->pos_x < (w->width / 2));
			nw->SetDirty(w);
			return;

		case WWT_DEFSIZEBOX: {
			if (_ctrl_pressed) {
				w->window_desc->pref_width = w->width;
				w->window_desc->pref_height = w->height;
			} else {
				int16 def_width = std::max<int16>(std::min<int16>(w->window_desc->GetDefaultWidth(), _screen.width), w->nested_root->smallest_x);
				int16 def_height = std::max<int16>(std::min<int16>(w->window_desc->GetDefaultHeight(), _screen.height - 50), w->nested_root->smallest_y);

				int dx = (w->resize.step_width  == 0) ? 0 : def_width  - w->width;
				int dy = (w->resize.step_height == 0) ? 0 : def_height - w->height;
				/* dx and dy has to go by step.. calculate it.
				 * The cast to int is necessary else dx/dy are implicitly casted to unsigned int, which won't work. */
				if (w->resize.step_width  > 1) dx -= dx % (int)w->resize.step_width;
				if (w->resize.step_height > 1) dy -= dy % (int)w->resize.step_height;
				ResizeWindow(w, dx, dy, false);
			}

			nw->SetLowered(true);
			nw->SetDirty(w);
			w->SetTimeout();
			break;
		}

		case WWT_DEBUGBOX:
			w->ShowNewGRFInspectWindow();
			break;

		case WWT_SHADEBOX:
			nw->SetDirty(w);
			w->SetShaded(!w->IsShaded());
			return;

		case WWT_STICKYBOX:
			w->flags ^= WF_STICKY;
			nw->SetDirty(w);
			if (_ctrl_pressed) w->window_desc->pref_sticky = (w->flags & WF_STICKY) != 0;
			return;

		default:
			break;
	}

	/* Widget has no index, so the window is not interested in it. */
	if (widget_index < 0) return;

	/* Check if the widget is highlighted; if so, disable highlight and dispatch an event to the GameScript */
	if (w->IsWidgetHighlighted(widget_index)) {
		w->SetWidgetHighlight(widget_index, TC_INVALID);
		Game::NewEvent(new ScriptEventWindowWidgetClick((ScriptWindow::WindowClass)w->window_class, w->window_number, widget_index));
	}

	w->OnClick(pt, widget_index, click_count);
}

/**
 * Dispatch right mouse-button click in window.
 * @param w Window to dispatch event in
 * @param x X coordinate of the click
 * @param y Y coordinate of the click
 */
static void DispatchRightClickEvent(Window *w, int x, int y)
{
	NWidgetCore *wid = w->nested_root->GetWidgetFromPos(x, y);
	if (wid == nullptr) return;

	Point pt = { x, y };

	/* No widget to handle, or the window is not interested in it. */
	if (wid->index >= 0) {
		if (w->OnRightClick(pt, wid->index)) return;
	}

	/* Right-click close is enabled and there is a closebox */
	if (_settings_client.gui.right_mouse_wnd_close && w->nested_root->GetWidgetOfType(WWT_CLOSEBOX)) {
		delete w;
	} else if (_settings_client.gui.hover_delay_ms == 0 && !w->OnTooltip(pt, wid->index, TCC_RIGHT_CLICK) && wid->tool_tip != 0) {
		GuiShowTooltips(w, wid->tool_tip, 0, nullptr, TCC_RIGHT_CLICK);
	}
}

/**
 * Dispatch hover of the mouse over a window.
 * @param w Window to dispatch event in.
 * @param x X coordinate of the click.
 * @param y Y coordinate of the click.
 */
static void DispatchHoverEvent(Window *w, int x, int y)
{
	NWidgetCore *wid = w->nested_root->GetWidgetFromPos(x, y);

	/* No widget to handle */
	if (wid == nullptr) return;

	Point pt = { x, y };

	/* Show the tooltip if there is any */
	if (!w->OnTooltip(pt, wid->index, TCC_HOVER) && wid->tool_tip != 0) {
		GuiShowTooltips(w, wid->tool_tip);
		return;
	}

	/* Widget has no index, so the window is not interested in it. */
	if (wid->index < 0) return;

	w->OnHover(pt, wid->index);
}

/**
 * Dispatch the mousewheel-action to the window.
 * The window will scroll any compatible scrollbars if the mouse is pointed over the bar or its contents
 * @param w Window
 * @param nwid the widget where the scrollwheel was used
 * @param wheel scroll up or down
 */
static void DispatchMouseWheelEvent(Window *w, NWidgetCore *nwid, int wheel)
{
	if (nwid == nullptr) return;

	/* Using wheel on caption/shade-box shades or unshades the window. */
	if (nwid->type == WWT_CAPTION || nwid->type == WWT_SHADEBOX) {
		w->SetShaded(wheel < 0);
		return;
	}

	/* Wheeling a vertical scrollbar. */
	if (nwid->type == NWID_VSCROLLBAR) {
		NWidgetScrollbar *sb = static_cast<NWidgetScrollbar *>(nwid);
		if (sb->GetCount() > sb->GetCapacity()) {
			sb->UpdatePosition(wheel);
			w->SetDirty();
		}
		return;
	}

	/* Scroll the widget attached to the scrollbar. */
	Scrollbar *sb = (nwid->scrollbar_index >= 0 ? w->GetScrollbar(nwid->scrollbar_index) : nullptr);
	if (sb != nullptr && sb->GetCount() > sb->GetCapacity()) {
		sb->UpdatePosition(wheel);
		w->SetDirty();
	}
}

/**
 * Returns whether a window may be shown or not.
 * @param w The window to consider.
 * @return True iff it may be shown, otherwise false.
 */
static bool MayBeShown(const Window *w)
{
	/* If we're not modal, everything is okay. */
	if (!HasModalProgress()) return true;

	switch (w->window_class) {
		case WC_MAIN_WINDOW:    ///< The background, i.e. the game.
		case WC_MODAL_PROGRESS: ///< The actual progress window.
		case WC_CONFIRM_POPUP_QUERY: ///< The abort window.
			return true;

		default:
			return false;
	}
}

/**
 * Generate repaint events for the visible part of window w within the rectangle.
 *
 * The function goes recursively upwards in the window stack, and splits the rectangle
 * into multiple pieces at the window edges, so obscured parts are not redrawn.
 *
 * @param w Window that needs to be repainted
 * @param left Left edge of the rectangle that should be repainted
 * @param top Top edge of the rectangle that should be repainted
 * @param right Right edge of the rectangle that should be repainted
 * @param bottom Bottom edge of the rectangle that should be repainted
 */
static void DrawOverlappedWindow(Window *w, int left, int top, int right, int bottom)
{
	const Window *v;
	FOR_ALL_WINDOWS_FROM_BACK_FROM(v, w->z_front) {
		if (MayBeShown(v) &&
				right > v->left &&
				bottom > v->top &&
				left < v->left + v->width &&
				top < v->top + v->height) {
			/* v and rectangle intersect with each other */
			int x;

			if (left < (x = v->left)) {
				DrawOverlappedWindow(w, left, top, x, bottom);
				DrawOverlappedWindow(w, x, top, right, bottom);
				return;
			}

			if (right > (x = v->left + v->width)) {
				DrawOverlappedWindow(w, left, top, x, bottom);
				DrawOverlappedWindow(w, x, top, right, bottom);
				return;
			}

			if (top < (x = v->top)) {
				DrawOverlappedWindow(w, left, top, right, x);
				DrawOverlappedWindow(w, left, x, right, bottom);
				return;
			}

			if (bottom > (x = v->top + v->height)) {
				DrawOverlappedWindow(w, left, top, right, x);
				DrawOverlappedWindow(w, left, x, right, bottom);
				return;
			}

			return;
		}
	}

	/* Setup blitter, and dispatch a repaint event to window *wz */
	DrawPixelInfo *dp = _cur_dpi;
	dp->width = right - left;
	dp->height = bottom - top;
	dp->left = left - w->left;
	dp->top = top - w->top;
	dp->pitch = _screen.pitch;
	dp->dst_ptr = BlitterFactory::GetCurrentBlitter()->MoveTo(_screen.dst_ptr, left, top);
	dp->zoom = ZOOM_LVL_NORMAL;
	w->OnPaint();
}

/**
 * From a rectangle that needs redrawing, find the windows that intersect with the rectangle.
 * These windows should be re-painted.
 * @param left Left edge of the rectangle that should be repainted
 * @param top Top edge of the rectangle that should be repainted
 * @param right Right edge of the rectangle that should be repainted
 * @param bottom Bottom edge of the rectangle that should be repainted
 */
void DrawOverlappedWindowForAll(int left, int top, int right, int bottom)
{
	Window *w;

	DrawPixelInfo *old_dpi = _cur_dpi;
	DrawPixelInfo bk;
	_cur_dpi = &bk;

	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (MayBeShown(w) &&
				right > w->left &&
				bottom > w->top &&
				left < w->left + w->width &&
				top < w->top + w->height) {
			/* Window w intersects with the rectangle => needs repaint */
			DrawOverlappedWindow(w, std::max(left, w->left), std::max(top, w->top), std::min(right, w->left + w->width), std::min(bottom, w->top + w->height));
		}
	}
	_cur_dpi = old_dpi;
}

/**
 * Mark entire window as dirty (in need of re-paint)
 * @ingroup dirty
 */
void Window::SetDirty() const
{
	AddDirtyBlock(this->left, this->top, this->left + this->width, this->top + this->height);
}

/**
 * Re-initialize a window, and optionally change its size.
 * @param rx Horizontal resize of the window.
 * @param ry Vertical resize of the window.
 * @note For just resizing the window, use #ResizeWindow instead.
 */
void Window::ReInit(int rx, int ry)
{
	this->SetDirty(); // Mark whole current window as dirty.

	/* Save current size. */
	int window_width  = this->width;
	int window_height = this->height;

	this->OnInit();
	/* Re-initialize the window from the ground up. No need to change the nested_array, as all widgets stay where they are. */
	this->nested_root->SetupSmallestSize(this, false);
	this->nested_root->AssignSizePosition(ST_SMALLEST, 0, 0, this->nested_root->smallest_x, this->nested_root->smallest_y, _current_text_dir == TD_RTL);
	this->width  = this->nested_root->smallest_x;
	this->height = this->nested_root->smallest_y;
	this->resize.step_width  = this->nested_root->resize_x;
	this->resize.step_height = this->nested_root->resize_y;

	/* Resize as close to the original size + requested resize as possible. */
	window_width  = std::max(window_width  + rx, this->width);
	window_height = std::max(window_height + ry, this->height);
	int dx = (this->resize.step_width  == 0) ? 0 : window_width  - this->width;
	int dy = (this->resize.step_height == 0) ? 0 : window_height - this->height;
	/* dx and dy has to go by step.. calculate it.
	 * The cast to int is necessary else dx/dy are implicitly casted to unsigned int, which won't work. */
	if (this->resize.step_width  > 1) dx -= dx % (int)this->resize.step_width;
	if (this->resize.step_height > 1) dy -= dy % (int)this->resize.step_height;

	ResizeWindow(this, dx, dy);
	/* ResizeWindow() does this->SetDirty() already, no need to do it again here. */
}

/**
 * Set the shaded state of the window to \a make_shaded.
 * @param make_shaded If \c true, shade the window (roll up until just the title bar is visible), else unshade/unroll the window to its original size.
 * @note The method uses #Window::ReInit(), thus after the call, the whole window should be considered changed.
 */
void Window::SetShaded(bool make_shaded)
{
	if (this->shade_select == nullptr) return;

	int desired = make_shaded ? SZSP_HORIZONTAL : 0;
	if (this->shade_select->shown_plane != desired) {
		if (make_shaded) {
			if (this->nested_focus != nullptr) this->UnfocusFocusedWidget();
			this->unshaded_size.width  = this->width;
			this->unshaded_size.height = this->height;
			this->shade_select->SetDisplayedPlane(desired);
			this->ReInit(0, -this->height);
		} else {
			this->shade_select->SetDisplayedPlane(desired);
			int dx = ((int)this->unshaded_size.width  > this->width)  ? (int)this->unshaded_size.width  - this->width  : 0;
			int dy = ((int)this->unshaded_size.height > this->height) ? (int)this->unshaded_size.height - this->height : 0;
			this->ReInit(dx, dy);
		}
	}
}

/**
 * Find the Window whose parent pointer points to this window
 * @param w parent Window to find child of
 * @param wc Window class of the window to remove; #WC_INVALID if class does not matter
 * @return a Window pointer that is the child of \a w, or \c nullptr otherwise
 */
static Window *FindChildWindow(const Window *w, WindowClass wc)
{
	Window *v;
	FOR_ALL_WINDOWS_FROM_BACK(v) {
		if ((wc == WC_INVALID || wc == v->window_class) && v->parent == w) return v;
	}

	return nullptr;
}

/**
 * Delete all children a window might have in a head-recursive manner
 * @param wc Window class of the window to remove; #WC_INVALID if class does not matter
 */
void Window::DeleteChildWindows(WindowClass wc) const
{
	Window *child = FindChildWindow(this, wc);
	while (child != nullptr) {
		delete child;
		child = FindChildWindow(this, wc);
	}
}

/**
 * Remove window and all its child windows from the window stack.
 */
Window::~Window()
{
	if (_thd.window_class == this->window_class &&
			_thd.window_number == this->window_number) {
		ResetObjectToPlace();
	}

	/* Prevent Mouseover() from resetting mouse-over coordinates on a non-existing window */
	if (_mouseover_last_w == this) _mouseover_last_w = nullptr;

	/* We can't scroll the window when it's closed. */
	if (_last_scroll_window == this) _last_scroll_window = nullptr;

	/* Make sure we don't try to access non-existing query strings. */
	this->querystrings.clear();

	/* Make sure we don't try to access this window as the focused window when it doesn't exist anymore. */
	if (_focused_window == this) {
		this->OnFocusLost();
		_focused_window = nullptr;
	}

	this->DeleteChildWindows();

	if (this->viewport != nullptr) DeleteWindowViewport(this);

	this->SetDirty();

	free(this->nested_array); // Contents is released through deletion of #nested_root.
	delete this->nested_root;

	/*
	 * Make fairly sure that this is written, and not "optimized" away.
	 * The delete operator is overwritten to not delete it; the deletion
	 * happens at a later moment in time after the window has been
	 * removed from the list of windows to prevent issues with items
	 * being removed during the iteration as not one but more windows
	 * may be removed by a single call to ~Window by means of the
	 * DeleteChildWindows function.
	 */
	const_cast<volatile WindowClass &>(this->window_class) = WC_INVALID;
}

/**
 * Find a window by its class and window number
 * @param cls Window class
 * @param number Number of the window within the window class
 * @return Pointer to the found window, or \c nullptr if not available
 */
Window *FindWindowById(WindowClass cls, WindowNumber number)
{
	Window *w;
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->window_class == cls && w->window_number == number) return w;
	}

	return nullptr;
}

/**
 * Find any window by its class. Useful when searching for a window that uses
 * the window number as a #WindowClass, like #WC_SEND_NETWORK_MSG.
 * @param cls Window class
 * @return Pointer to the found window, or \c nullptr if not available
 */
Window *FindWindowByClass(WindowClass cls)
{
	Window *w;
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->window_class == cls) return w;
	}

	return nullptr;
}

/**
 * Delete a window by its class and window number (if it is open).
 * @param cls Window class
 * @param number Number of the window within the window class
 * @param force force deletion; if false don't delete when stickied
 */
void DeleteWindowById(WindowClass cls, WindowNumber number, bool force)
{
	Window *w = FindWindowById(cls, number);
	if (w != nullptr && (force || (w->flags & WF_STICKY) == 0)) {
		delete w;
	}
}

/**
 * Delete all windows of a given class
 * @param cls Window class of windows to delete
 */
void DeleteWindowByClass(WindowClass cls)
{
	Window *w;

restart_search:
	/* When we find the window to delete, we need to restart the search
	 * as deleting this window could cascade in deleting (many) others
	 * anywhere in the z-array */
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->window_class == cls) {
			delete w;
			goto restart_search;
		}
	}
}

/**
 * Delete all windows of a company. We identify windows of a company
 * by looking at the caption colour. If it is equal to the company ID
 * then we say the window belongs to the company and should be deleted
 * @param id company identifier
 */
void DeleteCompanyWindows(CompanyID id)
{
	Window *w;

restart_search:
	/* When we find the window to delete, we need to restart the search
	 * as deleting this window could cascade in deleting (many) others
	 * anywhere in the z-array */
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->owner == id) {
			delete w;
			goto restart_search;
		}
	}

	/* Also delete the company specific windows that don't have a company-colour. */
	DeleteWindowById(WC_BUY_COMPANY, id);
}

/**
 * Change the owner of all the windows one company can take over from another
 * company in the case of a company merger. Do not change ownership of windows
 * that need to be deleted once takeover is complete
 * @param old_owner original owner of the window
 * @param new_owner the new owner of the window
 */
void ChangeWindowOwner(Owner old_owner, Owner new_owner)
{
	Window *w;
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->owner != old_owner) continue;

		switch (w->window_class) {
			case WC_COMPANY_COLOUR:
			case WC_FINANCES:
			case WC_STATION_LIST:
			case WC_TRAINS_LIST:
			case WC_ROADVEH_LIST:
			case WC_SHIPS_LIST:
			case WC_AIRCRAFT_LIST:
			case WC_BUY_COMPANY:
			case WC_COMPANY:
			case WC_COMPANY_INFRASTRUCTURE:
			case WC_VEHICLE_ORDERS: // Changing owner would also require changing WindowDesc, which is not possible; however keeping the old one crashes because of missing widgets etc.. See ShowOrdersWindow().
				continue;

			default:
				w->owner = new_owner;
				break;
		}
	}
}

static void BringWindowToFront(Window *w);

/**
 * Find a window and make it the relative top-window on the screen.
 * The window gets unshaded if it was shaded, and a white border is drawn at its edges for a brief period of time to visualize its "activation".
 * @param cls WindowClass of the window to activate
 * @param number WindowNumber of the window to activate
 * @return a pointer to the window thus activated
 */
Window *BringWindowToFrontById(WindowClass cls, WindowNumber number)
{
	Window *w = FindWindowById(cls, number);

	if (w != nullptr) {
		if (w->IsShaded()) w->SetShaded(false); // Restore original window size if it was shaded.

		w->SetWhiteBorder();
		BringWindowToFront(w);
		w->SetDirty();
	}

	return w;
}

static inline bool IsVitalWindow(const Window *w)
{
	switch (w->window_class) {
		case WC_MAIN_TOOLBAR:
		case WC_STATUS_BAR:
		case WC_NEWS_WINDOW:
		case WC_SEND_NETWORK_MSG:
			return true;

		default:
			return false;
	}
}

/**
 * Get the z-priority for a given window. This is used in comparison with other z-priority values;
 * a window with a given z-priority will appear above other windows with a lower value, and below
 * those with a higher one (the ordering within z-priorities is arbitrary).
 * @param wc The window class of window to get the z-priority for
 * @pre wc != WC_INVALID
 * @return The window's z-priority
 */
static uint GetWindowZPriority(WindowClass wc)
{
	assert(wc != WC_INVALID);

	uint z_priority = 0;

	switch (wc) {
		case WC_ENDSCREEN:
			++z_priority;
			FALLTHROUGH;

		case WC_HIGHSCORE:
			++z_priority;
			FALLTHROUGH;

		case WC_TOOLTIPS:
			++z_priority;
			FALLTHROUGH;

		case WC_DROPDOWN_MENU:
			++z_priority;
			FALLTHROUGH;

		case WC_MAIN_TOOLBAR:
		case WC_STATUS_BAR:
			++z_priority;
			FALLTHROUGH;

		case WC_OSK:
			++z_priority;
			FALLTHROUGH;

		case WC_QUERY_STRING:
		case WC_SEND_NETWORK_MSG:
			++z_priority;
			FALLTHROUGH;

		case WC_ERRMSG:
		case WC_CONFIRM_POPUP_QUERY:
		case WC_MODAL_PROGRESS:
		case WC_NETWORK_STATUS_WINDOW:
		case WC_SAVE_PRESET:
			++z_priority;
			FALLTHROUGH;

		case WC_GENERATE_LANDSCAPE:
		case WC_SAVELOAD:
		case WC_GAME_OPTIONS:
		case WC_CUSTOM_CURRENCY:
		case WC_NETWORK_WINDOW:
		case WC_GRF_PARAMETERS:
		case WC_AI_LIST:
		case WC_AI_SETTINGS:
		case WC_TEXTFILE:
			++z_priority;
			FALLTHROUGH;

		case WC_CONSOLE:
			++z_priority;
			FALLTHROUGH;

		case WC_NEWS_WINDOW:
			++z_priority;
			FALLTHROUGH;

		default:
			++z_priority;
			FALLTHROUGH;

		case WC_MAIN_WINDOW:
			return z_priority;
	}
}

/**
 * Adds a window to the z-ordering, according to its z-priority.
 * @param w Window to add
 */
static void AddWindowToZOrdering(Window *w)
{
	assert(w->z_front == nullptr && w->z_back == nullptr);

	if (_z_front_window == nullptr) {
		/* It's the only window. */
		_z_front_window = _z_back_window = w;
		w->z_front = w->z_back = nullptr;
	} else {
		/* Search down the z-ordering for its location. */
		Window *v = _z_front_window;
		uint last_z_priority = UINT_MAX;
		(void)last_z_priority; // Unused without asserts
		while (v != nullptr && (v->window_class == WC_INVALID || GetWindowZPriority(v->window_class) > GetWindowZPriority(w->window_class))) {
			if (v->window_class != WC_INVALID) {
				/* Sanity check z-ordering, while we're at it. */
				assert(last_z_priority >= GetWindowZPriority(v->window_class));
				last_z_priority = GetWindowZPriority(v->window_class);
			}

			v = v->z_back;
		}

		if (v == nullptr) {
			/* It's the new back window. */
			w->z_front = _z_back_window;
			w->z_back = nullptr;
			_z_back_window->z_back = w;
			_z_back_window = w;
		} else if (v == _z_front_window) {
			/* It's the new front window. */
			w->z_front = nullptr;
			w->z_back = _z_front_window;
			_z_front_window->z_front = w;
			_z_front_window = w;
		} else {
			/* It's somewhere else in the z-ordering. */
			w->z_front = v->z_front;
			w->z_back = v;
			v->z_front->z_back = w;
			v->z_front = w;
		}
	}
}


/**
 * Removes a window from the z-ordering.
 * @param w Window to remove
 */
static void RemoveWindowFromZOrdering(Window *w)
{
	if (w->z_front == nullptr) {
		assert(_z_front_window == w);
		_z_front_window = w->z_back;
	} else {
		w->z_front->z_back = w->z_back;
	}

	if (w->z_back == nullptr) {
		assert(_z_back_window == w);
		_z_back_window = w->z_front;
	} else {
		w->z_back->z_front = w->z_front;
	}

	w->z_front = w->z_back = nullptr;
}

/**
 * On clicking on a window, make it the frontmost window of all windows with an equal
 * or lower z-priority. The window is marked dirty for a repaint
 * @param w window that is put into the relative foreground
 */
static void BringWindowToFront(Window *w)
{
	RemoveWindowFromZOrdering(w);
	AddWindowToZOrdering(w);

	w->SetDirty();
}

/**
 * Initializes the data (except the position and initial size) of a new Window.
 * @param window_number Number being assigned to the new window
 * @return Window pointer of the newly created window
 * @pre If nested widgets are used (\a widget is \c nullptr), #nested_root and #nested_array_size must be initialized.
 *      In addition, #nested_array is either \c nullptr, or already initialized.
 */
void Window::InitializeData(WindowNumber window_number)
{
	/* Set up window properties; some of them are needed to set up smallest size below */
	this->window_class = this->window_desc->cls;
	this->SetWhiteBorder();
	if (this->window_desc->default_pos == WDP_CENTER) this->flags |= WF_CENTERED;
	this->owner = INVALID_OWNER;
	this->nested_focus = nullptr;
	this->window_number = window_number;

	this->OnInit();
	/* Initialize nested widget tree. */
	if (this->nested_array == nullptr) {
		this->nested_array = CallocT<NWidgetBase *>(this->nested_array_size);
		this->nested_root->SetupSmallestSize(this, true);
	} else {
		this->nested_root->SetupSmallestSize(this, false);
	}
	/* Initialize to smallest size. */
	this->nested_root->AssignSizePosition(ST_SMALLEST, 0, 0, this->nested_root->smallest_x, this->nested_root->smallest_y, _current_text_dir == TD_RTL);

	/* Further set up window properties,
	 * this->left, this->top, this->width, this->height, this->resize.width, and this->resize.height are initialized later. */
	this->resize.step_width  = this->nested_root->resize_x;
	this->resize.step_height = this->nested_root->resize_y;

	/* Give focus to the opened window unless a text box
	 * of focused window has focus (so we don't interrupt typing). But if the new
	 * window has a text box, then take focus anyway. */
	if (!EditBoxInGlobalFocus() || this->nested_root->GetWidgetOfType(WWT_EDITBOX) != nullptr) SetFocusedWindow(this);

	/* Insert the window into the correct location in the z-ordering. */
	AddWindowToZOrdering(this);
}

/**
 * Set the position and smallest size of the window.
 * @param x          Offset in pixels from the left of the screen of the new window.
 * @param y          Offset in pixels from the top of the screen of the new window.
 * @param sm_width   Smallest width in pixels of the window.
 * @param sm_height  Smallest height in pixels of the window.
 */
void Window::InitializePositionSize(int x, int y, int sm_width, int sm_height)
{
	this->left = x;
	this->top = y;
	this->width = sm_width;
	this->height = sm_height;
}

/**
 * Resize window towards the default size.
 * Prior to construction, a position for the new window (for its default size)
 * has been found with LocalGetWindowPlacement(). Initially, the window is
 * constructed with minimal size. Resizing the window to its default size is
 * done here.
 * @param def_width default width in pixels of the window
 * @param def_height default height in pixels of the window
 * @see Window::Window(), Window::InitializeData(), Window::InitializePositionSize()
 */
void Window::FindWindowPlacementAndResize(int def_width, int def_height)
{
	def_width  = std::max(def_width,  this->width); // Don't allow default size to be smaller than smallest size
	def_height = std::max(def_height, this->height);
	/* Try to make windows smaller when our window is too small.
	 * w->(width|height) is normally the same as min_(width|height),
	 * but this way the GUIs can be made a little more dynamic;
	 * one can use the same spec for multiple windows and those
	 * can then determine the real minimum size of the window. */
	if (this->width != def_width || this->height != def_height) {
		/* Think about the overlapping toolbars when determining the minimum window size */
		int free_height = _screen.height;
		const Window *wt = FindWindowById(WC_STATUS_BAR, 0);
		if (wt != nullptr) free_height -= wt->height;
		wt = FindWindowById(WC_MAIN_TOOLBAR, 0);
		if (wt != nullptr) free_height -= wt->height;

		int enlarge_x = std::max(std::min(def_width  - this->width,  _screen.width - this->width),  0);
		int enlarge_y = std::max(std::min(def_height - this->height, free_height   - this->height), 0);

		/* X and Y has to go by step.. calculate it.
		 * The cast to int is necessary else x/y are implicitly casted to
		 * unsigned int, which won't work. */
		if (this->resize.step_width  > 1) enlarge_x -= enlarge_x % (int)this->resize.step_width;
		if (this->resize.step_height > 1) enlarge_y -= enlarge_y % (int)this->resize.step_height;

		ResizeWindow(this, enlarge_x, enlarge_y);
		/* ResizeWindow() calls this->OnResize(). */
	} else {
		/* Always call OnResize; that way the scrollbars and matrices get initialized. */
		this->OnResize();
	}

	int nx = this->left;
	int ny = this->top;

	if (nx + this->width > _screen.width) nx -= (nx + this->width - _screen.width);

	const Window *wt = FindWindowById(WC_MAIN_TOOLBAR, 0);
	ny = std::max(ny, (wt == nullptr || this == wt || this->top == 0) ? 0 : wt->height);
	nx = std::max(nx, 0);

	if (this->viewport != nullptr) {
		this->viewport->left += nx - this->left;
		this->viewport->top  += ny - this->top;
	}
	this->left = nx;
	this->top = ny;

	this->SetDirty();
}

/**
 * Decide whether a given rectangle is a good place to open a completely visible new window.
 * The new window should be within screen borders, and not overlap with another already
 * existing window (except for the main window in the background).
 * @param left    Left edge of the rectangle
 * @param top     Top edge of the rectangle
 * @param width   Width of the rectangle
 * @param height  Height of the rectangle
 * @param toolbar_y Height of main toolbar
 * @param pos     If rectangle is good, use this parameter to return the top-left corner of the new window
 * @return Boolean indication that the rectangle is a good place for the new window
 */
static bool IsGoodAutoPlace1(int left, int top, int width, int height, int toolbar_y, Point &pos)
{
	int right  = width + left;
	int bottom = height + top;

	if (left < 0 || top < toolbar_y || right > _screen.width || bottom > _screen.height) return false;

	/* Make sure it is not obscured by any window. */
	const Window *w;
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->window_class == WC_MAIN_WINDOW) continue;

		if (right > w->left &&
				w->left + w->width > left &&
				bottom > w->top &&
				w->top + w->height > top) {
			return false;
		}
	}

	pos.x = left;
	pos.y = top;
	return true;
}

/**
 * Decide whether a given rectangle is a good place to open a mostly visible new window.
 * The new window should be mostly within screen borders, and not overlap with another already
 * existing window (except for the main window in the background).
 * @param left    Left edge of the rectangle
 * @param top     Top edge of the rectangle
 * @param width   Width of the rectangle
 * @param height  Height of the rectangle
 * @param toolbar_y Height of main toolbar
 * @param pos     If rectangle is good, use this parameter to return the top-left corner of the new window
 * @return Boolean indication that the rectangle is a good place for the new window
 */
static bool IsGoodAutoPlace2(int left, int top, int width, int height, int toolbar_y, Point &pos)
{
	bool rtl = _current_text_dir == TD_RTL;

	/* Left part of the rectangle may be at most 1/4 off-screen,
	 * right part of the rectangle may be at most 1/2 off-screen
	 */
	if (rtl) {
		if (left < -(width >> 1) || left > _screen.width - (width >> 2)) return false;
	} else {
		if (left < -(width >> 2) || left > _screen.width - (width >> 1)) return false;
	}

	/* Bottom part of the rectangle may be at most 1/4 off-screen */
	if (top < toolbar_y || top > _screen.height - (height >> 2)) return false;

	/* Make sure it is not obscured by any window. */
	const Window *w;
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->window_class == WC_MAIN_WINDOW) continue;

		if (left + width > w->left &&
				w->left + w->width > left &&
				top + height > w->top &&
				w->top + w->height > top) {
			return false;
		}
	}

	pos.x = left;
	pos.y = top;
	return true;
}

/**
 * Find a good place for opening a new window of a given width and height.
 * @param width  Width of the new window
 * @param height Height of the new window
 * @return Top-left coordinate of the new window
 */
static Point GetAutoPlacePosition(int width, int height)
{
	Point pt;

	bool rtl = _current_text_dir == TD_RTL;

	/* First attempt, try top-left of the screen */
	const Window *main_toolbar = FindWindowByClass(WC_MAIN_TOOLBAR);
	const int toolbar_y =  main_toolbar != nullptr ? main_toolbar->height : 0;
	if (IsGoodAutoPlace1(rtl ? _screen.width - width : 0, toolbar_y, width, height, toolbar_y, pt)) return pt;

	/* Second attempt, try around all existing windows.
	 * The new window must be entirely on-screen, and not overlap with an existing window.
	 * Eight starting points are tried, two at each corner.
	 */
	const Window *w;
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->window_class == WC_MAIN_WINDOW) continue;

		if (IsGoodAutoPlace1(w->left + w->width,         w->top,                      width, height, toolbar_y, pt)) return pt;
		if (IsGoodAutoPlace1(w->left            - width, w->top,                      width, height, toolbar_y, pt)) return pt;
		if (IsGoodAutoPlace1(w->left,                    w->top + w->height,          width, height, toolbar_y, pt)) return pt;
		if (IsGoodAutoPlace1(w->left,                    w->top             - height, width, height, toolbar_y, pt)) return pt;
		if (IsGoodAutoPlace1(w->left + w->width,         w->top + w->height - height, width, height, toolbar_y, pt)) return pt;
		if (IsGoodAutoPlace1(w->left            - width, w->top + w->height - height, width, height, toolbar_y, pt)) return pt;
		if (IsGoodAutoPlace1(w->left + w->width - width, w->top + w->height,          width, height, toolbar_y, pt)) return pt;
		if (IsGoodAutoPlace1(w->left + w->width - width, w->top             - height, width, height, toolbar_y, pt)) return pt;
	}

	/* Third attempt, try around all existing windows.
	 * The new window may be partly off-screen, and must not overlap with an existing window.
	 * Only four starting points are tried.
	 */
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->window_class == WC_MAIN_WINDOW) continue;

		if (IsGoodAutoPlace2(w->left + w->width, w->top,             width, height, toolbar_y, pt)) return pt;
		if (IsGoodAutoPlace2(w->left    - width, w->top,             width, height, toolbar_y, pt)) return pt;
		if (IsGoodAutoPlace2(w->left,            w->top + w->height, width, height, toolbar_y, pt)) return pt;
		if (IsGoodAutoPlace2(w->left,            w->top - height,    width, height, toolbar_y, pt)) return pt;
	}

	/* Fourth and final attempt, put window at diagonal starting from (0, toolbar_y), try multiples
	 * of the closebox
	 */
	int left = rtl ? _screen.width - width : 0, top = toolbar_y;
	int offset_x = rtl ? -(int)NWidgetLeaf::closebox_dimension.width : (int)NWidgetLeaf::closebox_dimension.width;
	int offset_y = std::max<int>(NWidgetLeaf::closebox_dimension.height, FONT_HEIGHT_NORMAL + WD_CAPTIONTEXT_TOP + WD_CAPTIONTEXT_BOTTOM);

restart:
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->left == left && w->top == top) {
			left += offset_x;
			top += offset_y;
			goto restart;
		}
	}

	pt.x = left;
	pt.y = top;
	return pt;
}

/**
 * Computer the position of the top-left corner of a window to be opened right
 * under the toolbar.
 * @param window_width the width of the window to get the position for
 * @return Coordinate of the top-left corner of the new window.
 */
Point GetToolbarAlignedWindowPosition(int window_width)
{
	const Window *w = FindWindowById(WC_MAIN_TOOLBAR, 0);
	assert(w != nullptr);
	Point pt = { _current_text_dir == TD_RTL ? w->left : (w->left + w->width) - window_width, w->top + w->height };
	return pt;
}

/**
 * Compute the position of the top-left corner of a new window that is opened.
 *
 * By default position a child window at an offset of 10/10 of its parent.
 * With the exception of WC_BUILD_TOOLBAR (build railway/roads/ship docks/airports)
 * and WC_SCEN_LAND_GEN (landscaping). Whose child window has an offset of 0/toolbar-height of
 * its parent. So it's exactly under the parent toolbar and no buttons will be covered.
 * However if it falls too extremely outside window positions, reposition
 * it to an automatic place.
 *
 * @param *desc         The pointer to the WindowDesc to be created.
 * @param sm_width      Smallest width of the window.
 * @param sm_height     Smallest height of the window.
 * @param window_number The window number of the new window.
 *
 * @return Coordinate of the top-left corner of the new window.
 */
static Point LocalGetWindowPlacement(const WindowDesc *desc, int16 sm_width, int16 sm_height, int window_number)
{
	Point pt;
	const Window *w;

	int16 default_width  = std::max(desc->GetDefaultWidth(),  sm_width);
	int16 default_height = std::max(desc->GetDefaultHeight(), sm_height);

	if (desc->parent_cls != WC_NONE && (w = FindWindowById(desc->parent_cls, window_number)) != nullptr) {
		bool rtl = _current_text_dir == TD_RTL;
		if (desc->parent_cls == WC_BUILD_TOOLBAR || desc->parent_cls == WC_SCEN_LAND_GEN) {
			pt.x = w->left + (rtl ? w->width - default_width : 0);
			pt.y = w->top + w->height;
			return pt;
		} else {
			/* Position child window with offset of closebox, but make sure that either closebox or resizebox is visible
			 *  - Y position: closebox of parent + closebox of child + statusbar
			 *  - X position: closebox on left/right, resizebox on right/left (depending on ltr/rtl)
			 */
			int indent_y = std::max<int>(NWidgetLeaf::closebox_dimension.height, FONT_HEIGHT_NORMAL + WD_CAPTIONTEXT_TOP + WD_CAPTIONTEXT_BOTTOM);
			if (w->top + 3 * indent_y < _screen.height) {
				pt.y = w->top + indent_y;
				int indent_close = NWidgetLeaf::closebox_dimension.width;
				int indent_resize = NWidgetLeaf::resizebox_dimension.width;
				if (_current_text_dir == TD_RTL) {
					pt.x = std::max(w->left + w->width - default_width - indent_close, 0);
					if (pt.x + default_width >= indent_close && pt.x + indent_resize <= _screen.width) return pt;
				} else {
					pt.x = std::min(w->left + indent_close, _screen.width - default_width);
					if (pt.x + default_width >= indent_resize && pt.x + indent_close <= _screen.width) return pt;
				}
			}
		}
	}

	switch (desc->default_pos) {
		case WDP_ALIGN_TOOLBAR: // Align to the toolbar
			return GetToolbarAlignedWindowPosition(default_width);

		case WDP_AUTO: // Find a good automatic position for the window
			return GetAutoPlacePosition(default_width, default_height);

		case WDP_CENTER: // Centre the window horizontally
			pt.x = (_screen.width - default_width) / 2;
			pt.y = (_screen.height - default_height) / 2;
			break;

		case WDP_MANUAL:
			pt.x = 0;
			pt.y = 0;
			break;

		default:
			NOT_REACHED();
	}

	return pt;
}

/* virtual */ Point Window::OnInitialPosition(int16 sm_width, int16 sm_height, int window_number)
{
	return LocalGetWindowPlacement(this->window_desc, sm_width, sm_height, window_number);
}

/**
 * Perform the first part of the initialization of a nested widget tree.
 * Construct a nested widget tree in #nested_root, and optionally fill the #nested_array array to provide quick access to the uninitialized widgets.
 * This is mainly useful for setting very basic properties.
 * @param fill_nested Fill the #nested_array (enabling is expensive!).
 * @note Filling the nested array requires an additional traversal through the nested widget tree, and is best performed by #FinishInitNested rather than here.
 */
void Window::CreateNestedTree(bool fill_nested)
{
	int biggest_index = -1;
	this->nested_root = MakeWindowNWidgetTree(this->window_desc->nwid_parts, this->window_desc->nwid_length, &biggest_index, &this->shade_select);
	this->nested_array_size = (uint)(biggest_index + 1);

	if (fill_nested) {
		this->nested_array = CallocT<NWidgetBase *>(this->nested_array_size);
		this->nested_root->FillNestedArray(this->nested_array, this->nested_array_size);
	}
}

/**
 * Perform the second part of the initialization of a nested widget tree.
 * @param window_number Number of the new window.
 */
void Window::FinishInitNested(WindowNumber window_number)
{
	this->InitializeData(window_number);
	this->ApplyDefaults();
	Point pt = this->OnInitialPosition(this->nested_root->smallest_x, this->nested_root->smallest_y, window_number);
	this->InitializePositionSize(pt.x, pt.y, this->nested_root->smallest_x, this->nested_root->smallest_y);
	this->FindWindowPlacementAndResize(this->window_desc->GetDefaultWidth(), this->window_desc->GetDefaultHeight());
}

/**
 * Perform complete initialization of the #Window with nested widgets, to allow use.
 * @param window_number Number of the new window.
 */
void Window::InitNested(WindowNumber window_number)
{
	this->CreateNestedTree(false);
	this->FinishInitNested(window_number);
}

/**
 * Empty constructor, initialization has been moved to #InitNested() called from the constructor of the derived class.
 * @param desc The description of the window.
 */
Window::Window(WindowDesc *desc) : window_desc(desc), mouse_capture_widget(-1)
{
}

/**
 * Do a search for a window at specific coordinates. For this we start
 * at the topmost window, obviously and work our way down to the bottom
 * @param x position x to query
 * @param y position y to query
 * @return a pointer to the found window if any, nullptr otherwise
 */
Window *FindWindowFromPt(int x, int y)
{
	Window *w;
	FOR_ALL_WINDOWS_FROM_FRONT(w) {
		if (MayBeShown(w) && IsInsideBS(x, w->left, w->width) && IsInsideBS(y, w->top, w->height)) {
			return w;
		}
	}

	return nullptr;
}

/**
 * (re)initialize the windowing system
 */
void InitWindowSystem()
{
	IConsoleClose();

	_z_back_window = nullptr;
	_z_front_window = nullptr;
	_focused_window = nullptr;
	_mouseover_last_w = nullptr;
	_last_scroll_window = nullptr;
	_scrolling_viewport = false;
	_mouse_hovering = false;

	NWidgetLeaf::InvalidateDimensionCache(); // Reset cached sizes of several widgets.
	NWidgetScrollbar::InvalidateDimensionCache();

	ShowFirstError();
}

/**
 * Close down the windowing system
 */
void UnInitWindowSystem()
{
	UnshowCriticalError();

	Window *w;
	FOR_ALL_WINDOWS_FROM_FRONT(w) delete w;

	for (w = _z_front_window; w != nullptr; /* nothing */) {
		Window *to_del = w;
		w = w->z_back;
		free(to_del);
	}

	_z_front_window = nullptr;
	_z_back_window = nullptr;
}

/**
 * Reset the windowing system, by means of shutting it down followed by re-initialization
 */
void ResetWindowSystem()
{
	UnInitWindowSystem();
	InitWindowSystem();
	_thd.Reset();
}

static void DecreaseWindowCounters()
{
	static byte hundredth_tick_timeout = 100;

	if (_scroller_click_timeout != 0) _scroller_click_timeout--;
	if (hundredth_tick_timeout != 0) hundredth_tick_timeout--;

	Window *w;
	FOR_ALL_WINDOWS_FROM_FRONT(w) {
		if (!_network_dedicated && hundredth_tick_timeout == 0) w->OnHundredthTick();

		if (_scroller_click_timeout == 0) {
			/* Unclick scrollbar buttons if they are pressed. */
			for (uint i = 0; i < w->nested_array_size; i++) {
				NWidgetBase *nwid = w->nested_array[i];
				if (nwid != nullptr && (nwid->type == NWID_HSCROLLBAR || nwid->type == NWID_VSCROLLBAR)) {
					NWidgetScrollbar *sb = static_cast<NWidgetScrollbar*>(nwid);
					if (sb->disp_flags & (ND_SCROLLBAR_UP | ND_SCROLLBAR_DOWN)) {
						sb->disp_flags &= ~(ND_SCROLLBAR_UP | ND_SCROLLBAR_DOWN);
						w->mouse_capture_widget = -1;
						sb->SetDirty(w);
					}
				}
			}
		}

		/* Handle editboxes */
		for (SmallMap<int, QueryString*>::Pair &pair : w->querystrings) {
			pair.second->HandleEditBox(w, pair.first);
		}

		w->OnMouseLoop();
	}

	FOR_ALL_WINDOWS_FROM_FRONT(w) {
		if ((w->flags & WF_TIMEOUT) && --w->timeout_timer == 0) {
			CLRBITS(w->flags, WF_TIMEOUT);

			w->OnTimeout();
			w->RaiseButtons(true);
		}
	}

	if (hundredth_tick_timeout == 0) hundredth_tick_timeout = 100;
}

static void HandlePlacePresize()
{
	if (_special_mouse_mode != WSM_PRESIZE) return;

	Window *w = _thd.GetCallbackWnd();
	if (w == nullptr) return;

	Point pt = GetTileBelowCursor();
	if (pt.x == -1) {
		_thd.selend.x = -1;
		return;
	}

	w->OnPlacePresize(pt, TileVirtXY(pt.x, pt.y));
}

/**
 * Handle dragging and dropping in mouse dragging mode (#WSM_DRAGDROP).
 * @return State of handling the event.
 */
static EventState HandleMouseDragDrop()
{
	if (_special_mouse_mode != WSM_DRAGDROP) return ES_NOT_HANDLED;

	if (_left_button_down && _cursor.delta.x == 0 && _cursor.delta.y == 0) return ES_HANDLED; // Dragging, but the mouse did not move.

	Window *w = _thd.GetCallbackWnd();
	if (w != nullptr) {
		/* Send an event in client coordinates. */
		Point pt;
		pt.x = _cursor.pos.x - w->left;
		pt.y = _cursor.pos.y - w->top;
		if (_left_button_down) {
			w->OnMouseDrag(pt, GetWidgetFromPos(w, pt.x, pt.y));
		} else {
			w->OnDragDrop(pt, GetWidgetFromPos(w, pt.x, pt.y));
		}
	}

	if (!_left_button_down) ResetObjectToPlace(); // Button released, finished dragging.
	return ES_HANDLED;
}

/** Report position of the mouse to the underlying window. */
static void HandleMouseOver()
{
	Window *w = FindWindowFromPt(_cursor.pos.x, _cursor.pos.y);

	/* We changed window, put an OnMouseOver event to the last window */
	if (_mouseover_last_w != nullptr && _mouseover_last_w != w) {
		/* Reset mouse-over coordinates of previous window */
		Point pt = { -1, -1 };
		_mouseover_last_w->OnMouseOver(pt, 0);
	}

	/* _mouseover_last_w will get reset when the window is deleted, see DeleteWindow() */
	_mouseover_last_w = w;

	if (w != nullptr) {
		/* send an event in client coordinates. */
		Point pt = { _cursor.pos.x - w->left, _cursor.pos.y - w->top };
		const NWidgetCore *widget = w->nested_root->GetWidgetFromPos(pt.x, pt.y);
		if (widget != nullptr) w->OnMouseOver(pt, widget->index);
	}
}

/** The minimum number of pixels of the title bar must be visible in both the X or Y direction */
static const int MIN_VISIBLE_TITLE_BAR = 13;

/** Direction for moving the window. */
enum PreventHideDirection {
	PHD_UP,   ///< Above v is a safe position.
	PHD_DOWN, ///< Below v is a safe position.
};

/**
 * Do not allow hiding of the rectangle with base coordinates \a nx and \a ny behind window \a v.
 * If needed, move the window base coordinates to keep it visible.
 * @param nx   Base horizontal coordinate of the rectangle.
 * @param ny   Base vertical coordinate of the rectangle.
 * @param rect Rectangle that must stay visible for #MIN_VISIBLE_TITLE_BAR pixels (horizontally, vertically, or both)
 * @param v    Window lying in front of the rectangle.
 * @param px   Previous horizontal base coordinate.
 * @param dir  If no room horizontally, move the rectangle to the indicated position.
 */
static void PreventHiding(int *nx, int *ny, const Rect &rect, const Window *v, int px, PreventHideDirection dir)
{
	if (v == nullptr) return;

	int v_bottom = v->top + v->height;
	int v_right = v->left + v->width;
	int safe_y = (dir == PHD_UP) ? (v->top - MIN_VISIBLE_TITLE_BAR - rect.top) : (v_bottom + MIN_VISIBLE_TITLE_BAR - rect.bottom); // Compute safe vertical position.

	if (*ny + rect.top <= v->top - MIN_VISIBLE_TITLE_BAR) return; // Above v is enough space
	if (*ny + rect.bottom >= v_bottom + MIN_VISIBLE_TITLE_BAR) return; // Below v is enough space

	/* Vertically, the rectangle is hidden behind v. */
	if (*nx + rect.left + MIN_VISIBLE_TITLE_BAR < v->left) { // At left of v.
		if (v->left < MIN_VISIBLE_TITLE_BAR) *ny = safe_y; // But enough room, force it to a safe position.
		return;
	}
	if (*nx + rect.right - MIN_VISIBLE_TITLE_BAR > v_right) { // At right of v.
		if (v_right > _screen.width - MIN_VISIBLE_TITLE_BAR) *ny = safe_y; // Not enough room, force it to a safe position.
		return;
	}

	/* Horizontally also hidden, force movement to a safe area. */
	if (px + rect.left < v->left && v->left >= MIN_VISIBLE_TITLE_BAR) { // Coming from the left, and enough room there.
		*nx = v->left - MIN_VISIBLE_TITLE_BAR - rect.left;
	} else if (px + rect.right > v_right && v_right <= _screen.width - MIN_VISIBLE_TITLE_BAR) { // Coming from the right, and enough room there.
		*nx = v_right + MIN_VISIBLE_TITLE_BAR - rect.right;
	} else {
		*ny = safe_y;
	}
}

/**
 * Make sure at least a part of the caption bar is still visible by moving
 * the window if necessary.
 * @param w The window to check.
 * @param nx The proposed new x-location of the window.
 * @param ny The proposed new y-location of the window.
 */
static void EnsureVisibleCaption(Window *w, int nx, int ny)
{
	/* Search for the title bar rectangle. */
	Rect caption_rect;
	const NWidgetBase *caption = w->nested_root->GetWidgetOfType(WWT_CAPTION);
	if (caption != nullptr) {
		caption_rect.left   = caption->pos_x;
		caption_rect.right  = caption->pos_x + caption->current_x;
		caption_rect.top    = caption->pos_y;
		caption_rect.bottom = caption->pos_y + caption->current_y;

		/* Make sure the window doesn't leave the screen */
		nx = Clamp(nx, MIN_VISIBLE_TITLE_BAR - caption_rect.right, _screen.width - MIN_VISIBLE_TITLE_BAR - caption_rect.left);
		ny = Clamp(ny, 0, _screen.height - MIN_VISIBLE_TITLE_BAR);

		/* Make sure the title bar isn't hidden behind the main tool bar or the status bar. */
		PreventHiding(&nx, &ny, caption_rect, FindWindowById(WC_MAIN_TOOLBAR, 0), w->left, PHD_DOWN);
		PreventHiding(&nx, &ny, caption_rect, FindWindowById(WC_STATUS_BAR,   0), w->left, PHD_UP);
	}

	if (w->viewport != nullptr) {
		w->viewport->left += nx - w->left;
		w->viewport->top  += ny - w->top;
	}

	w->left = nx;
	w->top  = ny;
}

/**
 * Resize the window.
 * Update all the widgets of a window based on their resize flags
 * Both the areas of the old window and the new sized window are set dirty
 * ensuring proper redrawal.
 * @param w       Window to resize
 * @param delta_x Delta x-size of changed window (positive if larger, etc.)
 * @param delta_y Delta y-size of changed window
 * @param clamp_to_screen Whether to make sure the whole window stays visible
 */
void ResizeWindow(Window *w, int delta_x, int delta_y, bool clamp_to_screen)
{
	if (delta_x != 0 || delta_y != 0) {
		if (clamp_to_screen) {
			/* Determine the new right/bottom position. If that is outside of the bounds of
			 * the resolution clamp it in such a manner that it stays within the bounds. */
			int new_right  = w->left + w->width  + delta_x;
			int new_bottom = w->top  + w->height + delta_y;
			if (new_right  >= (int)_screen.width)  delta_x -= Ceil(new_right  - _screen.width,  std::max(1U, w->nested_root->resize_x));
			if (new_bottom >= (int)_screen.height) delta_y -= Ceil(new_bottom - _screen.height, std::max(1U, w->nested_root->resize_y));
		}

		w->SetDirty();

		uint new_xinc = std::max(0, (w->nested_root->resize_x == 0) ? 0 : (int)(w->nested_root->current_x - w->nested_root->smallest_x) + delta_x);
		uint new_yinc = std::max(0, (w->nested_root->resize_y == 0) ? 0 : (int)(w->nested_root->current_y - w->nested_root->smallest_y) + delta_y);
		assert(w->nested_root->resize_x == 0 || new_xinc % w->nested_root->resize_x == 0);
		assert(w->nested_root->resize_y == 0 || new_yinc % w->nested_root->resize_y == 0);

		w->nested_root->AssignSizePosition(ST_RESIZE, 0, 0, w->nested_root->smallest_x + new_xinc, w->nested_root->smallest_y + new_yinc, _current_text_dir == TD_RTL);
		w->width  = w->nested_root->current_x;
		w->height = w->nested_root->current_y;
	}

	EnsureVisibleCaption(w, w->left, w->top);

	/* Always call OnResize to make sure everything is initialised correctly if it needs to be. */
	w->OnResize();
	w->SetDirty();
}

/**
 * Return the top of the main view available for general use.
 * @return Uppermost vertical coordinate available.
 * @note Above the upper y coordinate is often the main toolbar.
 */
int GetMainViewTop()
{
	Window *w = FindWindowById(WC_MAIN_TOOLBAR, 0);
	return (w == nullptr) ? 0 : w->top + w->height;
}

/**
 * Return the bottom of the main view available for general use.
 * @return The vertical coordinate of the first unusable row, so 'top + height <= bottom' gives the correct result.
 * @note At and below the bottom y coordinate is often the status bar.
 */
int GetMainViewBottom()
{
	Window *w = FindWindowById(WC_STATUS_BAR, 0);
	return (w == nullptr) ? _screen.height : w->top;
}

static bool _dragging_window; ///< A window is being dragged or resized.

/**
 * Handle dragging/resizing of a window.
 * @return State of handling the event.
 */
static EventState HandleWindowDragging()
{
	/* Get out immediately if no window is being dragged at all. */
	if (!_dragging_window) return ES_NOT_HANDLED;

	/* If button still down, but cursor hasn't moved, there is nothing to do. */
	if (_left_button_down && _cursor.delta.x == 0 && _cursor.delta.y == 0) return ES_HANDLED;

	/* Otherwise find the window... */
	Window *w;
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->flags & WF_DRAGGING) {
			/* Stop the dragging if the left mouse button was released */
			if (!_left_button_down) {
				w->flags &= ~WF_DRAGGING;
				break;
			}

			w->SetDirty();

			int x = _cursor.pos.x + _drag_delta.x;
			int y = _cursor.pos.y + _drag_delta.y;
			int nx = x;
			int ny = y;

			if (_settings_client.gui.window_snap_radius != 0) {
				const Window *v;

				int hsnap = _settings_client.gui.window_snap_radius;
				int vsnap = _settings_client.gui.window_snap_radius;
				int delta;

				FOR_ALL_WINDOWS_FROM_BACK(v) {
					if (v == w) continue; // Don't snap at yourself

					if (y + w->height > v->top && y < v->top + v->height) {
						/* Your left border <-> other right border */
						delta = abs(v->left + v->width - x);
						if (delta <= hsnap) {
							nx = v->left + v->width;
							hsnap = delta;
						}

						/* Your right border <-> other left border */
						delta = abs(v->left - x - w->width);
						if (delta <= hsnap) {
							nx = v->left - w->width;
							hsnap = delta;
						}
					}

					if (w->top + w->height >= v->top && w->top <= v->top + v->height) {
						/* Your left border <-> other left border */
						delta = abs(v->left - x);
						if (delta <= hsnap) {
							nx = v->left;
							hsnap = delta;
						}

						/* Your right border <-> other right border */
						delta = abs(v->left + v->width - x - w->width);
						if (delta <= hsnap) {
							nx = v->left + v->width - w->width;
							hsnap = delta;
						}
					}

					if (x + w->width > v->left && x < v->left + v->width) {
						/* Your top border <-> other bottom border */
						delta = abs(v->top + v->height - y);
						if (delta <= vsnap) {
							ny = v->top + v->height;
							vsnap = delta;
						}

						/* Your bottom border <-> other top border */
						delta = abs(v->top - y - w->height);
						if (delta <= vsnap) {
							ny = v->top - w->height;
							vsnap = delta;
						}
					}

					if (w->left + w->width >= v->left && w->left <= v->left + v->width) {
						/* Your top border <-> other top border */
						delta = abs(v->top - y);
						if (delta <= vsnap) {
							ny = v->top;
							vsnap = delta;
						}

						/* Your bottom border <-> other bottom border */
						delta = abs(v->top + v->height - y - w->height);
						if (delta <= vsnap) {
							ny = v->top + v->height - w->height;
							vsnap = delta;
						}
					}
				}
			}

			EnsureVisibleCaption(w, nx, ny);

			w->SetDirty();
			return ES_HANDLED;
		} else if (w->flags & WF_SIZING) {
			/* Stop the sizing if the left mouse button was released */
			if (!_left_button_down) {
				w->flags &= ~WF_SIZING;
				w->SetDirty();
				break;
			}

			/* Compute difference in pixels between cursor position and reference point in the window.
			 * If resizing the left edge of the window, moving to the left makes the window bigger not smaller.
			 */
			int x, y = _cursor.pos.y - _drag_delta.y;
			if (w->flags & WF_SIZING_LEFT) {
				x = _drag_delta.x - _cursor.pos.x;
			} else {
				x = _cursor.pos.x - _drag_delta.x;
			}

			/* resize.step_width and/or resize.step_height may be 0, which means no resize is possible. */
			if (w->resize.step_width  == 0) x = 0;
			if (w->resize.step_height == 0) y = 0;

			/* Check the resize button won't go past the bottom of the screen */
			if (w->top + w->height + y > _screen.height) {
				y = _screen.height - w->height - w->top;
			}

			/* X and Y has to go by step.. calculate it.
			 * The cast to int is necessary else x/y are implicitly casted to
			 * unsigned int, which won't work. */
			if (w->resize.step_width  > 1) x -= x % (int)w->resize.step_width;
			if (w->resize.step_height > 1) y -= y % (int)w->resize.step_height;

			/* Check that we don't go below the minimum set size */
			if ((int)w->width + x < (int)w->nested_root->smallest_x) {
				x = w->nested_root->smallest_x - w->width;
			}
			if ((int)w->height + y < (int)w->nested_root->smallest_y) {
				y = w->nested_root->smallest_y - w->height;
			}

			/* Window already on size */
			if (x == 0 && y == 0) return ES_HANDLED;

			/* Now find the new cursor pos.. this is NOT _cursor, because we move in steps. */
			_drag_delta.y += y;
			if ((w->flags & WF_SIZING_LEFT) && x != 0) {
				_drag_delta.x -= x; // x > 0 -> window gets longer -> left-edge moves to left -> subtract x to get new position.
				w->SetDirty();
				w->left -= x;  // If dragging left edge, move left window edge in opposite direction by the same amount.
				/* ResizeWindow() below ensures marking new position as dirty. */
			} else {
				_drag_delta.x += x;
			}

			/* ResizeWindow sets both pre- and after-size to dirty for redrawal */
			ResizeWindow(w, x, y);
			return ES_HANDLED;
		}
	}

	_dragging_window = false;
	return ES_HANDLED;
}

/**
 * Start window dragging
 * @param w Window to start dragging
 */
static void StartWindowDrag(Window *w)
{
	w->flags |= WF_DRAGGING;
	w->flags &= ~WF_CENTERED;
	_dragging_window = true;

	_drag_delta.x = w->left - _cursor.pos.x;
	_drag_delta.y = w->top  - _cursor.pos.y;

	BringWindowToFront(w);
	DeleteWindowById(WC_DROPDOWN_MENU, 0);
}

/**
 * Start resizing a window.
 * @param w       Window to start resizing.
 * @param to_left Whether to drag towards the left or not
 */
static void StartWindowSizing(Window *w, bool to_left)
{
	w->flags |= to_left ? WF_SIZING_LEFT : WF_SIZING_RIGHT;
	w->flags &= ~WF_CENTERED;
	_dragging_window = true;

	_drag_delta.x = _cursor.pos.x;
	_drag_delta.y = _cursor.pos.y;

	BringWindowToFront(w);
	DeleteWindowById(WC_DROPDOWN_MENU, 0);
}

/**
 * Handle scrollbar scrolling with the mouse.
 * @param w window with active scrollbar.
 */
static void HandleScrollbarScrolling(Window *w)
{
	int i;
	NWidgetScrollbar *sb = w->GetWidget<NWidgetScrollbar>(w->mouse_capture_widget);
	bool rtl = false;

	if (sb->type == NWID_HSCROLLBAR) {
		i = _cursor.pos.x - _cursorpos_drag_start.x;
		rtl = _current_text_dir == TD_RTL;
	} else {
		i = _cursor.pos.y - _cursorpos_drag_start.y;
	}

	if (sb->disp_flags & ND_SCROLLBAR_BTN) {
		if (_scroller_click_timeout == 1) {
			_scroller_click_timeout = 3;
			sb->UpdatePosition(rtl == HasBit(sb->disp_flags, NDB_SCROLLBAR_UP) ? 1 : -1);
			w->SetDirty();
		}
		return;
	}

	/* Find the item we want to move to and make sure it's inside bounds. */
	int pos = std::min<int>(RoundDivSU(std::max(0, i + _scrollbar_start_pos) * sb->GetCount(), _scrollbar_size), std::max<int>(0, sb->GetCount() - sb->GetCapacity()));
	if (rtl) pos = std::max<int>(0, sb->GetCount() - sb->GetCapacity() - pos);
	if (pos != (int)sb->GetPosition()) {
		sb->SetPosition(pos);
		w->SetDirty();
	}
}

/**
 * Handle active widget (mouse draggin on widget) with the mouse.
 * @return State of handling the event.
 */
static EventState HandleActiveWidget()
{
	Window *w;
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->mouse_capture_widget >= 0) {
			/* Abort if no button is clicked any more. */
			if (!_left_button_down) {
				w->mouse_capture_widget = -1;
				w->SetDirty();
				return ES_HANDLED;
			}

			/* Handle scrollbar internally, or dispatch click event */
			WidgetType type = w->GetWidget<NWidgetBase>(w->mouse_capture_widget)->type;
			if (type == NWID_VSCROLLBAR || type == NWID_HSCROLLBAR) {
				HandleScrollbarScrolling(w);
			} else {
				/* If cursor hasn't moved, there is nothing to do. */
				if (_cursor.delta.x == 0 && _cursor.delta.y == 0) return ES_HANDLED;

				Point pt = { _cursor.pos.x - w->left, _cursor.pos.y - w->top };
				w->OnClick(pt, w->mouse_capture_widget, 0);
			}
			return ES_HANDLED;
		}
	}

	return ES_NOT_HANDLED;
}

/**
 * Handle viewport scrolling with the mouse.
 * @return State of handling the event.
 */
static EventState HandleViewportScroll()
{
	bool scrollwheel_scrolling = _settings_client.gui.scrollwheel_scrolling == 1 && (_cursor.v_wheel != 0 || _cursor.h_wheel != 0);

	if (!_scrolling_viewport) return ES_NOT_HANDLED;

	/* When we don't have a last scroll window we are starting to scroll.
	 * When the last scroll window and this are not the same we went
	 * outside of the window and should not left-mouse scroll anymore. */
	if (_last_scroll_window == nullptr) _last_scroll_window = FindWindowFromPt(_cursor.pos.x, _cursor.pos.y);

	if (_last_scroll_window == nullptr || !((_settings_client.gui.scroll_mode != VSM_MAP_LMB && _right_button_down) || scrollwheel_scrolling || (_settings_client.gui.scroll_mode == VSM_MAP_LMB && _left_button_down))) {
		_cursor.fix_at = false;
		_scrolling_viewport = false;
		_last_scroll_window = nullptr;
		return ES_NOT_HANDLED;
	}

	if (_last_scroll_window == FindWindowById(WC_MAIN_WINDOW, 0) && _last_scroll_window->viewport->follow_vehicle != INVALID_VEHICLE) {
		/* If the main window is following a vehicle, then first let go of it! */
		const Vehicle *veh = Vehicle::Get(_last_scroll_window->viewport->follow_vehicle);
		ScrollMainWindowTo(veh->x_pos, veh->y_pos, veh->z_pos, true); // This also resets follow_vehicle
		return ES_NOT_HANDLED;
	}

	Point delta;
	if (scrollwheel_scrolling) {
		/* We are using scrollwheels for scrolling */
		delta.x = _cursor.h_wheel;
		delta.y = _cursor.v_wheel;
		_cursor.v_wheel = 0;
		_cursor.h_wheel = 0;
	} else {
		if (_settings_client.gui.scroll_mode != VSM_VIEWPORT_RMB_FIXED) {
			delta.x = -_cursor.delta.x;
			delta.y = -_cursor.delta.y;
		} else {
			delta.x = _cursor.delta.x;
			delta.y = _cursor.delta.y;
		}
	}

	/* Create a scroll-event and send it to the window */
	if (delta.x != 0 || delta.y != 0) _last_scroll_window->OnScroll(delta);

	_cursor.delta.x = 0;
	_cursor.delta.y = 0;
	return ES_HANDLED;
}

/**
 * Check if a window can be made relative top-most window, and if so do
 * it. If a window does not obscure any other windows, it will not
 * be brought to the foreground. Also if the only obscuring windows
 * are so-called system-windows, the window will not be moved.
 * The function will return false when a child window of this window is a
 * modal-popup; function returns a false and child window gets a white border
 * @param w Window to bring relatively on-top
 * @return false if the window has an active modal child, true otherwise
 */
static bool MaybeBringWindowToFront(Window *w)
{
	bool bring_to_front = false;

	if (w->window_class == WC_MAIN_WINDOW ||
			IsVitalWindow(w) ||
			w->window_class == WC_TOOLTIPS ||
			w->window_class == WC_DROPDOWN_MENU) {
		return true;
	}

	/* Use unshaded window size rather than current size for shaded windows. */
	int w_width  = w->width;
	int w_height = w->height;
	if (w->IsShaded()) {
		w_width  = w->unshaded_size.width;
		w_height = w->unshaded_size.height;
	}

	Window *u;
	FOR_ALL_WINDOWS_FROM_BACK_FROM(u, w->z_front) {
		/* A modal child will prevent the activation of the parent window */
		if (u->parent == w && (u->window_desc->flags & WDF_MODAL)) {
			u->SetWhiteBorder();
			u->SetDirty();
			return false;
		}

		if (u->window_class == WC_MAIN_WINDOW ||
				IsVitalWindow(u) ||
				u->window_class == WC_TOOLTIPS ||
				u->window_class == WC_DROPDOWN_MENU) {
			continue;
		}

		/* Window sizes don't interfere, leave z-order alone */
		if (w->left + w_width <= u->left ||
				u->left + u->width <= w->left ||
				w->top  + w_height <= u->top ||
				u->top + u->height <= w->top) {
			continue;
		}

		bring_to_front = true;
	}

	if (bring_to_front) BringWindowToFront(w);
	return true;
}

/**
 * Process keypress for editbox widget.
 * @param wid Editbox widget.
 * @param key     the Unicode value of the key.
 * @param keycode the untranslated key code including shift state.
 * @return #ES_HANDLED if the key press has been handled and no other
 *         window should receive the event.
 */
EventState Window::HandleEditBoxKey(int wid, WChar key, uint16 keycode)
{
	QueryString *query = this->GetQueryString(wid);
	if (query == nullptr) return ES_NOT_HANDLED;

	int action = QueryString::ACTION_NOTHING;

	switch (query->text.HandleKeyPress(key, keycode)) {
		case HKPR_EDITING:
			this->SetWidgetDirty(wid);
			this->OnEditboxChanged(wid);
			break;

		case HKPR_CURSOR:
			this->SetWidgetDirty(wid);
			/* For the OSK also invalidate the parent window */
			if (this->window_class == WC_OSK) this->InvalidateData();
			break;

		case HKPR_CONFIRM:
			if (this->window_class == WC_OSK) {
				this->OnClick(Point(), WID_OSK_OK, 1);
			} else if (query->ok_button >= 0) {
				this->OnClick(Point(), query->ok_button, 1);
			} else {
				action = query->ok_button;
			}
			break;

		case HKPR_CANCEL:
			if (this->window_class == WC_OSK) {
				this->OnClick(Point(), WID_OSK_CANCEL, 1);
			} else if (query->cancel_button >= 0) {
				this->OnClick(Point(), query->cancel_button, 1);
			} else {
				action = query->cancel_button;
			}
			break;

		case HKPR_NOT_HANDLED:
			return ES_NOT_HANDLED;

		default: break;
	}

	switch (action) {
		case QueryString::ACTION_DESELECT:
			this->UnfocusFocusedWidget();
			break;

		case QueryString::ACTION_CLEAR:
			if (query->text.bytes <= 1) {
				/* If already empty, unfocus instead */
				this->UnfocusFocusedWidget();
			} else {
				query->text.DeleteAll();
				this->SetWidgetDirty(wid);
				this->OnEditboxChanged(wid);
			}
			break;

		default:
			break;
	}

	return ES_HANDLED;
}

/**
 * Handle keyboard input.
 * @param keycode Virtual keycode of the key.
 * @param key Unicode character of the key.
 */
void HandleKeypress(uint keycode, WChar key)
{
	/* World generation is multithreaded and messes with companies.
	 * But there is no company related window open anyway, so _current_company is not used. */
	assert(HasModalProgress() || IsLocalCompany());

	/*
	 * The Unicode standard defines an area called the private use area. Code points in this
	 * area are reserved for private use and thus not portable between systems. For instance,
	 * Apple defines code points for the arrow keys in this area, but these are only printable
	 * on a system running OS X. We don't want these keys to show up in text fields and such,
	 * and thus we have to clear the unicode character when we encounter such a key.
	 */
	if (key >= 0xE000 && key <= 0xF8FF) key = 0;

	/*
	 * If both key and keycode is zero, we don't bother to process the event.
	 */
	if (key == 0 && keycode == 0) return;

	/* Check if the focused window has a focused editbox */
	if (EditBoxInGlobalFocus()) {
		/* All input will in this case go to the focused editbox */
		if (_focused_window->window_class == WC_CONSOLE) {
			if (_focused_window->OnKeyPress(key, keycode) == ES_HANDLED) return;
		} else {
			if (_focused_window->HandleEditBoxKey(_focused_window->nested_focus->index, key, keycode) == ES_HANDLED) return;
		}
	}

	/* Call the event, start with the uppermost window, but ignore the toolbar. */
	Window *w;
	FOR_ALL_WINDOWS_FROM_FRONT(w) {
		if (w->window_class == WC_MAIN_TOOLBAR) continue;
		if (w->window_desc->hotkeys != nullptr) {
			int hotkey = w->window_desc->hotkeys->CheckMatch(keycode);
			if (hotkey >= 0 && w->OnHotkey(hotkey) == ES_HANDLED) return;
		}
		if (w->OnKeyPress(key, keycode) == ES_HANDLED) return;
	}

	w = FindWindowById(WC_MAIN_TOOLBAR, 0);
	/* When there is no toolbar w is null, check for that */
	if (w != nullptr) {
		if (w->window_desc->hotkeys != nullptr) {
			int hotkey = w->window_desc->hotkeys->CheckMatch(keycode);
			if (hotkey >= 0 && w->OnHotkey(hotkey) == ES_HANDLED) return;
		}
		if (w->OnKeyPress(key, keycode) == ES_HANDLED) return;
	}

	HandleGlobalHotkeys(key, keycode);
}

/**
 * State of CONTROL key has changed
 */
void HandleCtrlChanged()
{
	/* Call the event, start with the uppermost window. */
	Window *w;
	FOR_ALL_WINDOWS_FROM_FRONT(w) {
		if (w->OnCTRLStateChange() == ES_HANDLED) return;
	}
}

/**
 * Insert a text string at the cursor position into the edit box widget.
 * @param wid Edit box widget.
 * @param str Text string to insert.
 */
/* virtual */ void Window::InsertTextString(int wid, const char *str, bool marked, const char *caret, const char *insert_location, const char *replacement_end)
{
	QueryString *query = this->GetQueryString(wid);
	if (query == nullptr) return;

	if (query->text.InsertString(str, marked, caret, insert_location, replacement_end) || marked) {
		this->SetWidgetDirty(wid);
		this->OnEditboxChanged(wid);
	}
}

/**
 * Handle text input.
 * @param str Text string to input.
 * @param marked Is the input a marked composition string from an IME?
 * @param caret Move the caret to this point in the insertion string.
 */
void HandleTextInput(const char *str, bool marked, const char *caret, const char *insert_location, const char *replacement_end)
{
	if (!EditBoxInGlobalFocus()) return;

	_focused_window->InsertTextString(_focused_window->window_class == WC_CONSOLE ? 0 : _focused_window->nested_focus->index, str, marked, caret, insert_location, replacement_end);
}

/**
 * Local counter that is incremented each time an mouse input event is detected.
 * The counter is used to stop auto-scrolling.
 * @see HandleAutoscroll()
 * @see HandleMouseEvents()
 */
static int _input_events_this_tick = 0;

/**
 * If needed and switched on, perform auto scrolling (automatically
 * moving window contents when mouse is near edge of the window).
 */
static void HandleAutoscroll()
{
	if (_game_mode == GM_MENU || HasModalProgress()) return;
	if (_settings_client.gui.auto_scrolling == VA_DISABLED) return;
	if (_settings_client.gui.auto_scrolling == VA_MAIN_VIEWPORT_FULLSCREEN && !_fullscreen) return;

	int x = _cursor.pos.x;
	int y = _cursor.pos.y;
	Window *w = FindWindowFromPt(x, y);
	if (w == nullptr || w->flags & WF_DISABLE_VP_SCROLL) return;
	if (_settings_client.gui.auto_scrolling != VA_EVERY_VIEWPORT && w->window_class != WC_MAIN_WINDOW) return;

	Viewport *vp = IsPtInWindowViewport(w, x, y);
	if (vp == nullptr) return;

	x -= vp->left;
	y -= vp->top;

	/* here allows scrolling in both x and y axis */
	static const int SCROLLSPEED = 3;
	if (x - 15 < 0) {
		w->viewport->dest_scrollpos_x += ScaleByZoom((x - 15) * SCROLLSPEED, vp->zoom);
	} else if (15 - (vp->width - x) > 0) {
		w->viewport->dest_scrollpos_x += ScaleByZoom((15 - (vp->width - x)) * SCROLLSPEED, vp->zoom);
	}
	if (y - 15 < 0) {
		w->viewport->dest_scrollpos_y += ScaleByZoom((y - 15) * SCROLLSPEED, vp->zoom);
	} else if (15 - (vp->height - y) > 0) {
		w->viewport->dest_scrollpos_y += ScaleByZoom((15 - (vp->height - y)) * SCROLLSPEED, vp->zoom);
	}
}

enum MouseClick {
	MC_NONE = 0,
	MC_LEFT,
	MC_RIGHT,
	MC_DOUBLE_LEFT,
	MC_HOVER,

	MAX_OFFSET_DOUBLE_CLICK = 5,     ///< How much the mouse is allowed to move to call it a double click
	MAX_OFFSET_HOVER = 5,            ///< Maximum mouse movement before stopping a hover event.
};
extern EventState VpHandlePlaceSizingDrag();

const std::chrono::milliseconds TIME_BETWEEN_DOUBLE_CLICK(500); ///< Time between 2 left clicks before it becoming a double click.

static void ScrollMainViewport(int x, int y)
{
	if (_game_mode != GM_MENU) {
		Window *w = FindWindowById(WC_MAIN_WINDOW, 0);
		assert(w);

		w->viewport->dest_scrollpos_x += ScaleByZoom(x, w->viewport->zoom);
		w->viewport->dest_scrollpos_y += ScaleByZoom(y, w->viewport->zoom);
	}
}

/**
 * Describes all the different arrow key combinations the game allows
 * when it is in scrolling mode.
 * The real arrow keys are bitwise numbered as
 * 1 = left
 * 2 = up
 * 4 = right
 * 8 = down
 */
static const int8 scrollamt[16][2] = {
	{ 0,  0}, ///<  no key specified
	{-2,  0}, ///<  1 : left
	{ 0, -2}, ///<  2 : up
	{-2, -1}, ///<  3 : left  + up
	{ 2,  0}, ///<  4 : right
	{ 0,  0}, ///<  5 : left  + right = nothing
	{ 2, -1}, ///<  6 : right + up
	{ 0, -2}, ///<  7 : right + left  + up = up
	{ 0,  2}, ///<  8 : down
	{-2,  1}, ///<  9 : down  + left
	{ 0,  0}, ///< 10 : down  + up    = nothing
	{-2,  0}, ///< 11 : left  + up    +  down = left
	{ 2,  1}, ///< 12 : down  + right
	{ 0,  2}, ///< 13 : left  + right +  down = down
	{ 2,  0}, ///< 14 : right + up    +  down = right
	{ 0,  0}, ///< 15 : left  + up    +  right + down  = nothing
};

static void HandleKeyScrolling()
{
	/*
	 * Check that any of the dirkeys is pressed and that the focused window
	 * doesn't have an edit-box as focused widget.
	 */
	if (_dirkeys && !EditBoxInGlobalFocus()) {
		int factor = _shift_pressed ? 50 : 10;
		ScrollMainViewport(scrollamt[_dirkeys][0] * factor, scrollamt[_dirkeys][1] * factor);
	}
}

static void MouseLoop(MouseClick click, int mousewheel)
{
	/* World generation is multithreaded and messes with companies.
	 * But there is no company related window open anyway, so _current_company is not used. */
	assert(HasModalProgress() || IsLocalCompany());

	HandlePlacePresize();
	UpdateTileSelection();

	if (VpHandlePlaceSizingDrag()  == ES_HANDLED) return;
	if (HandleMouseDragDrop()      == ES_HANDLED) return;
	if (HandleWindowDragging()     == ES_HANDLED) return;
	if (HandleActiveWidget()       == ES_HANDLED) return;
	if (HandleViewportScroll()     == ES_HANDLED) return;

	HandleMouseOver();

	bool scrollwheel_scrolling = _settings_client.gui.scrollwheel_scrolling == 1 && (_cursor.v_wheel != 0 || _cursor.h_wheel != 0);
	if (click == MC_NONE && mousewheel == 0 && !scrollwheel_scrolling) return;

	int x = _cursor.pos.x;
	int y = _cursor.pos.y;
	Window *w = FindWindowFromPt(x, y);
	if (w == nullptr) return;

	if (click != MC_HOVER && !MaybeBringWindowToFront(w)) return;
	Viewport *vp = IsPtInWindowViewport(w, x, y);

	/* Don't allow any action in a viewport if either in menu or when having a modal progress window */
	if (vp != nullptr && (_game_mode == GM_MENU || HasModalProgress())) return;

	if (mousewheel != 0) {
		/* Send mousewheel event to window, unless we're scrolling a viewport or the map */
		if (!scrollwheel_scrolling || (vp == nullptr && w->window_class != WC_SMALLMAP)) w->OnMouseWheel(mousewheel);

		/* Dispatch a MouseWheelEvent for widgets if it is not a viewport */
		if (vp == nullptr) DispatchMouseWheelEvent(w, w->nested_root->GetWidgetFromPos(x - w->left, y - w->top), mousewheel);
	}

	if (vp != nullptr) {
		if (scrollwheel_scrolling && !(w->flags & WF_DISABLE_VP_SCROLL)) {
			_scrolling_viewport = true;
			_cursor.fix_at = true;
			return;
		}

		switch (click) {
			case MC_DOUBLE_LEFT:
			case MC_LEFT:
				if (HandleViewportClicked(vp, x, y)) return;
				if (!(w->flags & WF_DISABLE_VP_SCROLL) &&
						_settings_client.gui.scroll_mode == VSM_MAP_LMB) {
					_scrolling_viewport = true;
					_cursor.fix_at = false;
					return;
				}
				break;

			case MC_RIGHT:
				if (!(w->flags & WF_DISABLE_VP_SCROLL) &&
						_settings_client.gui.scroll_mode != VSM_MAP_LMB) {
					_scrolling_viewport = true;
					_cursor.fix_at = (_settings_client.gui.scroll_mode == VSM_VIEWPORT_RMB_FIXED ||
							_settings_client.gui.scroll_mode == VSM_MAP_RMB_FIXED);
					return;
				}
				break;

			default:
				break;
		}
	}

	if (vp == nullptr || (w->flags & WF_DISABLE_VP_SCROLL)) {
		switch (click) {
			case MC_LEFT:
			case MC_DOUBLE_LEFT:
				DispatchLeftClickEvent(w, x - w->left, y - w->top, click == MC_DOUBLE_LEFT ? 2 : 1);
				return;

			default:
				if (!scrollwheel_scrolling || w == nullptr || w->window_class != WC_SMALLMAP) break;
				/* We try to use the scrollwheel to scroll since we didn't touch any of the buttons.
				 * Simulate a right button click so we can get started. */
				FALLTHROUGH;

			case MC_RIGHT:
				DispatchRightClickEvent(w, x - w->left, y - w->top);
				return;

			case MC_HOVER:
				DispatchHoverEvent(w, x - w->left, y - w->top);
				break;
		}
	}

	/* We're not doing anything with 2D scrolling, so reset the value.  */
	_cursor.h_wheel = 0;
	_cursor.v_wheel = 0;
}

/**
 * Handle a mouse event from the video driver
 */
void HandleMouseEvents()
{
	/* World generation is multithreaded and messes with companies.
	 * But there is no company related window open anyway, so _current_company is not used. */
	assert(HasModalProgress() || IsLocalCompany());

	/* Handle sprite picker before any GUI interaction */
	if (_newgrf_debug_sprite_picker.mode == SPM_REDRAW && _input_events_this_tick == 0) {
		/* We are done with the last draw-frame, so we know what sprites we
		 * clicked on. Reset the picker mode and invalidate the window. */
		_newgrf_debug_sprite_picker.mode = SPM_NONE;
		InvalidateWindowData(WC_SPRITE_ALIGNER, 0, 1);
	}

	static std::chrono::steady_clock::time_point double_click_time = {};
	static Point double_click_pos = {0, 0};

	/* Mouse event? */
	MouseClick click = MC_NONE;
	if (_left_button_down && !_left_button_clicked) {
		click = MC_LEFT;
		if (std::chrono::steady_clock::now() <= double_click_time + TIME_BETWEEN_DOUBLE_CLICK &&
				double_click_pos.x != 0 && abs(_cursor.pos.x - double_click_pos.x) < MAX_OFFSET_DOUBLE_CLICK  &&
				double_click_pos.y != 0 && abs(_cursor.pos.y - double_click_pos.y) < MAX_OFFSET_DOUBLE_CLICK) {
			click = MC_DOUBLE_LEFT;
		}
		double_click_time = std::chrono::steady_clock::now();
		double_click_pos = _cursor.pos;
		_left_button_clicked = true;
		_input_events_this_tick++;
	} else if (_right_button_clicked) {
		_right_button_clicked = false;
		click = MC_RIGHT;
		_input_events_this_tick++;
	}

	int mousewheel = 0;
	if (_cursor.wheel) {
		mousewheel = _cursor.wheel;
		_cursor.wheel = 0;
		_input_events_this_tick++;
	}

	static std::chrono::steady_clock::time_point hover_time = {};
	static Point hover_pos = {0, 0};

	if (_settings_client.gui.hover_delay_ms > 0) {
		if (!_cursor.in_window || click != MC_NONE || mousewheel != 0 || _left_button_down || _right_button_down ||
				hover_pos.x == 0 || abs(_cursor.pos.x - hover_pos.x) >= MAX_OFFSET_HOVER  ||
				hover_pos.y == 0 || abs(_cursor.pos.y - hover_pos.y) >= MAX_OFFSET_HOVER) {
			hover_pos = _cursor.pos;
			hover_time = std::chrono::steady_clock::now();
			_mouse_hovering = false;
		} else {
			if (std::chrono::steady_clock::now() > hover_time + std::chrono::milliseconds(_settings_client.gui.hover_delay_ms)) {
				click = MC_HOVER;
				_input_events_this_tick++;
				_mouse_hovering = true;
			}
		}
	}

	if (click == MC_LEFT && _newgrf_debug_sprite_picker.mode == SPM_WAIT_CLICK) {
		/* Mark whole screen dirty, and wait for the next realtime tick, when drawing is finished. */
		Blitter *blitter = BlitterFactory::GetCurrentBlitter();
		_newgrf_debug_sprite_picker.clicked_pixel = blitter->MoveTo(_screen.dst_ptr, _cursor.pos.x, _cursor.pos.y);
		_newgrf_debug_sprite_picker.sprites.clear();
		_newgrf_debug_sprite_picker.mode = SPM_REDRAW;
		MarkWholeScreenDirty();
	} else {
		MouseLoop(click, mousewheel);
	}

	/* We have moved the mouse the required distance,
	 * no need to move it at any later time. */
	_cursor.delta.x = 0;
	_cursor.delta.y = 0;
}

/**
 * Check the soft limit of deletable (non vital, non sticky) windows.
 */
static void CheckSoftLimit()
{
	if (_settings_client.gui.window_soft_limit == 0) return;

	for (;;) {
		uint deletable_count = 0;
		Window *w, *last_deletable = nullptr;
		FOR_ALL_WINDOWS_FROM_FRONT(w) {
			if (w->window_class == WC_MAIN_WINDOW || IsVitalWindow(w) || (w->flags & WF_STICKY)) continue;

			last_deletable = w;
			deletable_count++;
		}

		/* We've not reached the soft limit yet. */
		if (deletable_count <= _settings_client.gui.window_soft_limit) break;

		assert(last_deletable != nullptr);
		delete last_deletable;
	}
}

/**
 * Regular call from the global game loop
 */
void InputLoop()
{
	/* World generation is multithreaded and messes with companies.
	 * But there is no company related window open anyway, so _current_company is not used. */
	assert(HasModalProgress() || IsLocalCompany());

	CheckSoftLimit();

	/* Do the actual free of the deleted windows. */
	for (Window *v = _z_front_window; v != nullptr; /* nothing */) {
		Window *w = v;
		v = v->z_back;

		if (w->window_class != WC_INVALID) continue;

		RemoveWindowFromZOrdering(w);
		free(w);
	}

	if (_input_events_this_tick != 0) {
		/* The input loop is called only once per GameLoop() - so we can clear the counter here */
		_input_events_this_tick = 0;
		/* there were some inputs this tick, don't scroll ??? */
		return;
	}

	/* HandleMouseEvents was already called for this tick */
	HandleMouseEvents();
}

/**
 * Dispatch OnRealtimeTick event over all windows
 */
void CallWindowRealtimeTickEvent(uint delta_ms)
{
	Window *w;
	FOR_ALL_WINDOWS_FROM_FRONT(w) {
		w->OnRealtimeTick(delta_ms);
	}
}

/**
 * Update the continuously changing contents of the windows, such as the viewports
 */
void UpdateWindows()
{
	static std::chrono::steady_clock::time_point last_time = std::chrono::steady_clock::now();
	uint delta_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - last_time).count();

	if (delta_ms == 0) return;

	last_time = std::chrono::steady_clock::now();

	PerformanceMeasurer framerate(PFE_DRAWING);
	PerformanceAccumulator::Reset(PFE_DRAWWORLD);

	CallWindowRealtimeTickEvent(delta_ms);

	static GUITimer network_message_timer = GUITimer(1);
	if (network_message_timer.Elapsed(delta_ms)) {
		network_message_timer.SetInterval(1000);
		NetworkChatMessageLoop();
	}

	Window *w;

	/* Process invalidations before anything else. */
	FOR_ALL_WINDOWS_FROM_FRONT(w) {
		w->ProcessScheduledInvalidations();
		w->ProcessHighlightedInvalidations();
	}

	static GUITimer window_timer = GUITimer(1);
	if (window_timer.Elapsed(delta_ms)) {
		if (_network_dedicated) window_timer.SetInterval(MILLISECONDS_PER_TICK);

		extern int _caret_timer;
		_caret_timer += 3;
		CursorTick();

		HandleKeyScrolling();
		HandleAutoscroll();
		DecreaseWindowCounters();
	}

	static GUITimer highlight_timer = GUITimer(1);
	if (highlight_timer.Elapsed(delta_ms)) {
		highlight_timer.SetInterval(450);
		_window_highlight_colour = !_window_highlight_colour;
	}

	if (!_pause_mode || _game_mode == GM_EDITOR || _settings_game.construction.command_pause_level > CMDPL_NO_CONSTRUCTION) MoveAllTextEffects(delta_ms);

	/* Skip the actual drawing on dedicated servers without screen.
	 * But still empty the invalidation queues above. */
	if (_network_dedicated) return;

	if (window_timer.HasElapsed()) {
		window_timer.SetInterval(MILLISECONDS_PER_TICK);

		FOR_ALL_WINDOWS_FROM_FRONT(w) {
			if ((w->flags & WF_WHITE_BORDER) && --w->white_border_timer == 0) {
				CLRBITS(w->flags, WF_WHITE_BORDER);
				w->SetDirty();
			}
		}
	}

	DrawDirtyBlocks();

	FOR_ALL_WINDOWS_FROM_BACK(w) {
		/* Update viewport only if window is not shaded. */
		if (w->viewport != nullptr && !w->IsShaded()) UpdateViewportPosition(w);
	}
	NetworkDrawChatMessage();
	/* Redraw mouse cursor in case it was hidden */
	DrawMouseCursor();
}

/**
 * Mark window as dirty (in need of repainting)
 * @param cls Window class
 * @param number Window number in that class
 */
void SetWindowDirty(WindowClass cls, WindowNumber number)
{
	const Window *w;
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->window_class == cls && w->window_number == number) w->SetDirty();
	}
}

/**
 * Mark a particular widget in a particular window as dirty (in need of repainting)
 * @param cls Window class
 * @param number Window number in that class
 * @param widget_index Index number of the widget that needs repainting
 */
void SetWindowWidgetDirty(WindowClass cls, WindowNumber number, byte widget_index)
{
	const Window *w;
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->window_class == cls && w->window_number == number) {
			w->SetWidgetDirty(widget_index);
		}
	}
}

/**
 * Mark all windows of a particular class as dirty (in need of repainting)
 * @param cls Window class
 */
void SetWindowClassesDirty(WindowClass cls)
{
	Window *w;
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->window_class == cls) w->SetDirty();
	}
}

/**
 * Mark this window's data as invalid (in need of re-computing)
 * @param data The data to invalidate with
 * @param gui_scope Whether the function is called from GUI scope.
 */
void Window::InvalidateData(int data, bool gui_scope)
{
	this->SetDirty();
	if (!gui_scope) {
		/* Schedule GUI-scope invalidation for next redraw. */
		this->scheduled_invalidation_data.push_back(data);
	}
	this->OnInvalidateData(data, gui_scope);
}

/**
 * Process all scheduled invalidations.
 */
void Window::ProcessScheduledInvalidations()
{
	for (int data : this->scheduled_invalidation_data) {
		if (this->window_class == WC_INVALID) break;
		this->OnInvalidateData(data, true);
	}
	this->scheduled_invalidation_data.clear();
}

/**
 * Process all invalidation of highlighted widgets.
 */
void Window::ProcessHighlightedInvalidations()
{
	if ((this->flags & WF_HIGHLIGHTED) == 0) return;

	for (uint i = 0; i < this->nested_array_size; i++) {
		if (this->IsWidgetHighlighted(i)) this->SetWidgetDirty(i);
	}
}

/**
 * Mark window data of the window of a given class and specific window number as invalid (in need of re-computing)
 *
 * Note that by default the invalidation is not considered to be called from GUI scope.
 * That means only a part of invalidation is executed immediately. The rest is scheduled for the next redraw.
 * The asynchronous execution is important to prevent GUI code being executed from command scope.
 * When not in GUI-scope:
 *  - OnInvalidateData() may not do test-runs on commands, as they might affect the execution of
 *    the command which triggered the invalidation. (town rating and such)
 *  - OnInvalidateData() may not rely on _current_company == _local_company.
 *    This implies that no NewGRF callbacks may be run.
 *
 * However, when invalidations are scheduled, then multiple calls may be scheduled before execution starts. Earlier scheduled
 * invalidations may be called with invalidation-data, which is already invalid at the point of execution.
 * That means some stuff requires to be executed immediately in command scope, while not everything may be executed in command
 * scope. While GUI-scope calls have no restrictions on what they may do, they cannot assume the game to still be in the state
 * when the invalidation was scheduled; passed IDs may have got invalid in the mean time.
 *
 * Finally, note that invalidations triggered from commands or the game loop result in OnInvalidateData() being called twice.
 * Once in command-scope, once in GUI-scope. So make sure to not process differential-changes twice.
 *
 * @param cls Window class
 * @param number Window number within the class
 * @param data The data to invalidate with
 * @param gui_scope Whether the call is done from GUI scope
 */
void InvalidateWindowData(WindowClass cls, WindowNumber number, int data, bool gui_scope)
{
	Window *w;
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->window_class == cls && w->window_number == number) {
			w->InvalidateData(data, gui_scope);
		}
	}
}

/**
 * Mark window data of all windows of a given class as invalid (in need of re-computing)
 * Note that by default the invalidation is not considered to be called from GUI scope.
 * See InvalidateWindowData() for details on GUI-scope vs. command-scope.
 * @param cls Window class
 * @param data The data to invalidate with
 * @param gui_scope Whether the call is done from GUI scope
 */
void InvalidateWindowClassesData(WindowClass cls, int data, bool gui_scope)
{
	Window *w;

	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->window_class == cls) {
			w->InvalidateData(data, gui_scope);
		}
	}
}

/**
 * Dispatch OnGameTick event over all windows
 */
void CallWindowGameTickEvent()
{
	Window *w;
	FOR_ALL_WINDOWS_FROM_FRONT(w) {
		w->OnGameTick();
	}
}

/**
 * Try to delete a non-vital window.
 * Non-vital windows are windows other than the game selection, main toolbar,
 * status bar, toolbar menu, and tooltip windows. Stickied windows are also
 * considered vital.
 */
void DeleteNonVitalWindows()
{
	Window *w;

restart_search:
	/* When we find the window to delete, we need to restart the search
	 * as deleting this window could cascade in deleting (many) others
	 * anywhere in the z-array */
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->window_class != WC_MAIN_WINDOW &&
				w->window_class != WC_SELECT_GAME &&
				w->window_class != WC_MAIN_TOOLBAR &&
				w->window_class != WC_STATUS_BAR &&
				w->window_class != WC_TOOLTIPS &&
				(w->flags & WF_STICKY) == 0) { // do not delete windows which are 'pinned'

			delete w;
			goto restart_search;
		}
	}
}

/**
 * It is possible that a stickied window gets to a position where the
 * 'close' button is outside the gaming area. You cannot close it then; except
 * with this function. It closes all windows calling the standard function,
 * then, does a little hacked loop of closing all stickied windows. Note
 * that standard windows (status bar, etc.) are not stickied, so these aren't affected
 */
void DeleteAllNonVitalWindows()
{
	Window *w;

	/* Delete every window except for stickied ones, then sticky ones as well */
	DeleteNonVitalWindows();

restart_search:
	/* When we find the window to delete, we need to restart the search
	 * as deleting this window could cascade in deleting (many) others
	 * anywhere in the z-array */
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->flags & WF_STICKY) {
			delete w;
			goto restart_search;
		}
	}
}

/**
 * Delete all messages and their corresponding window (if any).
 */
void DeleteAllMessages()
{
	InitNewsItemStructs();
	InvalidateWindowData(WC_STATUS_BAR, 0, SBI_NEWS_DELETED); // invalidate the statusbar
	InvalidateWindowData(WC_MESSAGE_HISTORY, 0); // invalidate the message history
	DeleteWindowById(WC_NEWS_WINDOW, 0); // close newspaper or general message window if shown
}

/**
 * Delete all windows that are used for construction of vehicle etc.
 * Once done with that invalidate the others to ensure they get refreshed too.
 */
void DeleteConstructionWindows()
{
	Window *w;

restart_search:
	/* When we find the window to delete, we need to restart the search
	 * as deleting this window could cascade in deleting (many) others
	 * anywhere in the z-array */
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->window_desc->flags & WDF_CONSTRUCTION) {
			delete w;
			goto restart_search;
		}
	}

	FOR_ALL_WINDOWS_FROM_BACK(w) w->SetDirty();
}

/** Delete all always on-top windows to get an empty screen */
void HideVitalWindows()
{
	DeleteWindowById(WC_MAIN_TOOLBAR, 0);
	DeleteWindowById(WC_STATUS_BAR, 0);
}

/** Re-initialize all windows. */
void ReInitAllWindows()
{
	NWidgetLeaf::InvalidateDimensionCache(); // Reset cached sizes of several widgets.
	NWidgetScrollbar::InvalidateDimensionCache();

	extern void InitDepotWindowBlockSizes();
	InitDepotWindowBlockSizes();

	Window *w;
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		w->ReInit();
	}

	void NetworkReInitChatBoxSize();
	NetworkReInitChatBoxSize();

	/* Make sure essential parts of all windows are visible */
	RelocateAllWindows(_screen.width, _screen.height);
	MarkWholeScreenDirty();
}

/**
 * (Re)position a window at the screen.
 * @param w       Window structure of the window, may also be \c nullptr.
 * @param clss    The class of the window to position.
 * @param setting The actual setting used for the window's position.
 * @return X coordinate of left edge of the repositioned window.
 */
static int PositionWindow(Window *w, WindowClass clss, int setting)
{
	if (w == nullptr || w->window_class != clss) {
		w = FindWindowById(clss, 0);
	}
	if (w == nullptr) return 0;

	int old_left = w->left;
	switch (setting) {
		case 1:  w->left = (_screen.width - w->width) / 2; break;
		case 2:  w->left = _screen.width - w->width; break;
		default: w->left = 0; break;
	}
	if (w->viewport != nullptr) w->viewport->left += w->left - old_left;
	AddDirtyBlock(0, w->top, _screen.width, w->top + w->height); // invalidate the whole row
	return w->left;
}

/**
 * (Re)position main toolbar window at the screen.
 * @param w Window structure of the main toolbar window, may also be \c nullptr.
 * @return X coordinate of left edge of the repositioned toolbar window.
 */
int PositionMainToolbar(Window *w)
{
	DEBUG(misc, 5, "Repositioning Main Toolbar...");
	return PositionWindow(w, WC_MAIN_TOOLBAR, _settings_client.gui.toolbar_pos);
}

/**
 * (Re)position statusbar window at the screen.
 * @param w Window structure of the statusbar window, may also be \c nullptr.
 * @return X coordinate of left edge of the repositioned statusbar.
 */
int PositionStatusbar(Window *w)
{
	DEBUG(misc, 5, "Repositioning statusbar...");
	return PositionWindow(w, WC_STATUS_BAR, _settings_client.gui.statusbar_pos);
}

/**
 * (Re)position news message window at the screen.
 * @param w Window structure of the news message window, may also be \c nullptr.
 * @return X coordinate of left edge of the repositioned news message.
 */
int PositionNewsMessage(Window *w)
{
	DEBUG(misc, 5, "Repositioning news message...");
	return PositionWindow(w, WC_NEWS_WINDOW, _settings_client.gui.statusbar_pos);
}

/**
 * (Re)position network chat window at the screen.
 * @param w Window structure of the network chat window, may also be \c nullptr.
 * @return X coordinate of left edge of the repositioned network chat window.
 */
int PositionNetworkChatWindow(Window *w)
{
	DEBUG(misc, 5, "Repositioning network chat window...");
	return PositionWindow(w, WC_SEND_NETWORK_MSG, _settings_client.gui.statusbar_pos);
}


/**
 * Switches viewports following vehicles, which get autoreplaced
 * @param from_index the old vehicle ID
 * @param to_index the new vehicle ID
 */
void ChangeVehicleViewports(VehicleID from_index, VehicleID to_index)
{
	Window *w;
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->viewport != nullptr && w->viewport->follow_vehicle == from_index) {
			w->viewport->follow_vehicle = to_index;
			w->SetDirty();
		}
	}
}


/**
 * Relocate all windows to fit the new size of the game application screen
 * @param neww New width of the game application screen
 * @param newh New height of the game application screen.
 */
void RelocateAllWindows(int neww, int newh)
{
	DeleteWindowById(WC_DROPDOWN_MENU, 0);

	Window *w;
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		int left, top;
		/* XXX - this probably needs something more sane. For example specifying
		 * in a 'backup'-desc that the window should always be centered. */
		switch (w->window_class) {
			case WC_MAIN_WINDOW:
			case WC_BOOTSTRAP:
				ResizeWindow(w, neww, newh);
				continue;

			case WC_MAIN_TOOLBAR:
				ResizeWindow(w, std::min<uint>(neww, _toolbar_width) - w->width, 0, false);

				top = w->top;
				left = PositionMainToolbar(w); // changes toolbar orientation
				break;

			case WC_NEWS_WINDOW:
				top = newh - w->height;
				left = PositionNewsMessage(w);
				break;

			case WC_STATUS_BAR:
				ResizeWindow(w, std::min<uint>(neww, _toolbar_width) - w->width, 0, false);

				top = newh - w->height;
				left = PositionStatusbar(w);
				break;

			case WC_SEND_NETWORK_MSG:
				ResizeWindow(w, std::min<uint>(neww, _toolbar_width) - w->width, 0, false);

				top = newh - w->height - FindWindowById(WC_STATUS_BAR, 0)->height;
				left = PositionNetworkChatWindow(w);
				break;

			case WC_CONSOLE:
				IConsoleResize(w);
				continue;

			default: {
				if (w->flags & WF_CENTERED) {
					top = (newh - w->height) >> 1;
					left = (neww - w->width) >> 1;
					break;
				}

				left = w->left;
				if (left + (w->width >> 1) >= neww) left = neww - w->width;
				if (left < 0) left = 0;

				top = w->top;
				if (top + (w->height >> 1) >= newh) top = newh - w->height;
				break;
			}
		}

		EnsureVisibleCaption(w, left, top);
	}
}

/**
 * Destructor of the base class PickerWindowBase
 * Main utility is to stop the base Window destructor from triggering
 * a free while the child will already be free, in this case by the ResetObjectToPlace().
 */
PickerWindowBase::~PickerWindowBase()
{
	this->window_class = WC_INVALID; // stop the ancestor from freeing the already (to be) child
	ResetObjectToPlace();
}
