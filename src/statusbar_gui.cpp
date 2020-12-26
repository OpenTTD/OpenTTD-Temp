/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file statusbar_gui.cpp The GUI for the bottom status bar. */

#include "stdafx.h"
#include "date_func.h"
#include "gfx_func.h"
#include "news_func.h"
#include "company_func.h"
#include "string_func.h"
#include "strings_func.h"
#include "company_base.h"
#include "tilehighlight_func.h"
#include "news_gui.h"
#include "company_gui.h"
#include "window_gui.h"
#include "saveload/saveload.h"
#include "window_func.h"
#include "statusbar_gui.h"
#include "toolbar_gui.h"
#include "core/geometry_func.hpp"
#include "guitimer_func.h"
#include "zoom_func.h"

#include "widgets/statusbar_widget.h"

#include "table/strings.h"
#include "table/sprites.h"

#include "safeguards.h"

static bool DrawScrollingStatusText(const NewsItem *ni, int scroll_pos, int left, int right, int top, int bottom)
{
	CopyInDParam(0, ni->params, lengthof(ni->params));
	StringID str = ni->string_id;

	char buf[512];
	GetString(buf, str, lastof(buf));
	const char *s = buf;

	char buffer[256];
	char *d = buffer;
	const char *last = lastof(buffer);

	for (;;) {
		WChar c = Utf8Consume(&s);
		if (c == 0) {
			break;
		} else if (c == '\n') {
			if (d + 4 >= last) break;
			d[0] = d[1] = d[2] = d[3] = ' ';
			d += 4;
		} else if (IsPrintable(c)) {
			if (d + Utf8CharLen(c) >= last) break;
			d += Utf8Encode(d, c);
		}
	}
	*d = '\0';

	DrawPixelInfo tmp_dpi;
	if (!FillDrawPixelInfo(&tmp_dpi, left, top, right - left, bottom)) return true;

	int width = GetStringBoundingBox(buffer).width;
	int pos = (_current_text_dir == TD_RTL) ? (scroll_pos - width) : (right - scroll_pos - left);

	DrawPixelInfo *old_dpi = _cur_dpi;
	_cur_dpi = &tmp_dpi;
	DrawString(pos, INT16_MAX, 0, buffer, TC_LIGHT_BLUE, SA_LEFT | SA_FORCE);
	_cur_dpi = old_dpi;

	return (_current_text_dir == TD_RTL) ? (pos < right - left) : (pos + width > 0);
}

struct StatusBarWindow : Window {
	bool saving;
	int ticker_scroll;
	GUITimer ticker_timer;
	GUITimer reminder_timeout;

	static const int TICKER_STOP    = 1640; ///< scrolling is finished when counter reaches this value
	static const int REMINDER_START = 1350; ///< time in ms for reminder notification (red dot on the right) to stay
	static const int REMINDER_STOP  =    0; ///< reminder disappears when counter reaches this value
	static const int COUNTER_STEP   =    2; ///< this is subtracted from active counters every tick

	StatusBarWindow(WindowDesc *desc) : Window(desc)
	{
		this->ticker_scroll = TICKER_STOP;
		this->ticker_timer.SetInterval(15);
		this->reminder_timeout.SetInterval(REMINDER_STOP);

		this->InitNested();
		CLRBITS(this->flags, WF_WHITE_BORDER);
		PositionStatusbar(this);
	}

	Point OnInitialPosition(int16 sm_width, int16 sm_height, int window_number) override
	{
		Point pt = { 0, _screen.height - sm_height };
		return pt;
	}

	void FindWindowPlacementAndResize(int def_width, int def_height) override
	{
		Window::FindWindowPlacementAndResize(_toolbar_width, def_height);
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		Dimension d;
		switch (widget) {
			case WID_S_LEFT:
				SetDParamMaxValue(0, MAX_YEAR * DAYS_IN_YEAR);
				d = GetStringBoundingBox(STR_WHITE_DATE_LONG);
				break;

			case WID_S_RIGHT: {
				Money max_money = UINT32_MAX;
				for (const Company *c : Company::Iterate()) max_money = max(c->money, max_money);
				SetDParam(0, 100LL * max_money);
				d = GetStringBoundingBox(STR_COMPANY_MONEY);
				break;
			}

			default:
				return;
		}

		d.width += padding.width;
		d.height += padding.height;
		*size = maxdim(d, *size);
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		int text_offset = max(0, ((int)(r.bottom - r.top + 1) - FONT_HEIGHT_NORMAL) / 2); // Offset for rendering the text vertically centered
		int text_top = r.top + text_offset;
		switch (widget) {
			case WID_S_LEFT:
				/* Draw the date */
				SetDParam(0, _date);
				DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, text_top, STR_WHITE_DATE_LONG, TC_FROMSTRING, SA_HOR_CENTER);
				break;

			case WID_S_RIGHT: {
				/* Draw company money, if any */
				const Company *c = Company::GetIfValid(_local_company);
				if (c != nullptr) {
					SetDParam(0, c->money);
					DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, text_top, STR_COMPANY_MONEY, TC_FROMSTRING, SA_HOR_CENTER);
				}
				break;
			}

			case WID_S_MIDDLE:
				/* Draw status bar */
				if (this->saving) { // true when saving is active
					DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, text_top, STR_STATUSBAR_SAVING_GAME, TC_FROMSTRING, SA_HOR_CENTER | SA_VERT_CENTER);
				} else if (_do_autosave) {
					DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, text_top, STR_STATUSBAR_AUTOSAVE, TC_FROMSTRING, SA_HOR_CENTER);
				} else if (_pause_mode != PM_UNPAUSED) {
					StringID msg = (_pause_mode & PM_PAUSED_LINK_GRAPH) ? STR_STATUSBAR_PAUSED_LINK_GRAPH : STR_STATUSBAR_PAUSED;
					DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, text_top, msg, TC_FROMSTRING, SA_HOR_CENTER);
				} else if (this->ticker_scroll < TICKER_STOP && _statusbar_news_item != nullptr && _statusbar_news_item->string_id != 0) {
					/* Draw the scrolling news text */
					if (!DrawScrollingStatusText(_statusbar_news_item, ScaleGUITrad(this->ticker_scroll), r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, r.top + WD_FRAMERECT_TOP, r.bottom)) {
						InvalidateWindowData(WC_STATUS_BAR, 0, SBI_NEWS_DELETED);
						if (Company::IsValidID(_local_company)) {
							/* This is the default text */
							SetDParam(0, _local_company);
							DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, text_top, STR_STATUSBAR_COMPANY_NAME, TC_FROMSTRING, SA_HOR_CENTER);
						}
					}
				} else {
					if (Company::IsValidID(_local_company)) {
						/* This is the default text */
						SetDParam(0, _local_company);
						DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, text_top, STR_STATUSBAR_COMPANY_NAME, TC_FROMSTRING, SA_HOR_CENTER);
					}
				}

				if (!this->reminder_timeout.HasElapsed()) {
					Dimension icon_size = GetSpriteSize(SPR_UNREAD_NEWS);
					DrawSprite(SPR_UNREAD_NEWS, PAL_NONE, r.right - WD_FRAMERECT_RIGHT - icon_size.width, r.top + max(0, ((int)(r.bottom - r.top + 1) - (int)icon_size.height) / 2));
				}
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
		if (!gui_scope) return;
		switch (data) {
			default: NOT_REACHED();
			case SBI_SAVELOAD_START:  this->saving = true;  break;
			case SBI_SAVELOAD_FINISH: this->saving = false; break;
			case SBI_SHOW_TICKER:     this->ticker_scroll = 0; break;
			case SBI_SHOW_REMINDER:   this->reminder_timeout.SetInterval(REMINDER_START); break;
			case SBI_NEWS_DELETED:
				this->ticker_scroll    =   TICKER_STOP; // reset ticker ...
				this->reminder_timeout.SetInterval(REMINDER_STOP); // ... and reminder
				break;
		}
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_S_MIDDLE: ShowLastNewsMessage(); break;
			case WID_S_RIGHT:  if (_local_company != COMPANY_SPECTATOR) ShowCompanyFinances(_local_company); break;
			default: ResetObjectToPlace();
		}
	}

	void OnRealtimeTick(uint delta_ms) override
	{
		if (_pause_mode != PM_UNPAUSED) return;

		if (this->ticker_scroll < TICKER_STOP) { // Scrolling text
			uint count = this->ticker_timer.CountElapsed(delta_ms);
			if (count > 0) {
				this->ticker_scroll += count;
				this->SetWidgetDirty(WID_S_MIDDLE);
			}
		}

		// Red blot to show there are new unread newsmessages
		if (this->reminder_timeout.Elapsed(delta_ms)) {
			this->SetWidgetDirty(WID_S_MIDDLE);
		}
	}
};

static const NWidgetPart _nested_main_status_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_GREY, WID_S_LEFT), SetMinimalSize(140, 12), EndContainer(),
		NWidget(WWT_PUSHBTN, COLOUR_GREY, WID_S_MIDDLE), SetMinimalSize(40, 12), SetDataTip(0x0, STR_STATUSBAR_TOOLTIP_SHOW_LAST_NEWS), SetResize(1, 0),
		NWidget(WWT_PUSHBTN, COLOUR_GREY, WID_S_RIGHT), SetMinimalSize(140, 12),
	EndContainer(),
};

static WindowDesc _main_status_desc(
	WDP_MANUAL, nullptr, 0, 0,
	WC_STATUS_BAR, WC_NONE,
	WDF_NO_FOCUS,
	_nested_main_status_widgets, lengthof(_nested_main_status_widgets)
);

/**
 * Checks whether the news ticker is currently being used.
 */
bool IsNewsTickerShown()
{
	const StatusBarWindow *w = dynamic_cast<StatusBarWindow*>(FindWindowById(WC_STATUS_BAR, 0));
	return w != nullptr && w->ticker_scroll < StatusBarWindow::TICKER_STOP;
}

/**
 * Show our status bar.
 */
void ShowStatusBar()
{
	new StatusBarWindow(&_main_status_desc);
}
