/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file textfile_gui.h GUI functions related to textfiles. */

#ifndef TEXTFILE_GUI_H
#define TEXTFILE_GUI_H

#include "fileio_type.h"
#include "strings_func.h"
#include "textfile_type.h"
#include "window_gui.h"

const char *GetTextfile(TextfileType type, Subdirectory dir, const char *filename, TextDirection *text_dir = nullptr);

/** Window for displaying a textfile */
struct TextfileWindow : public Window, MissingGlyphSearcher {
	struct Line {
		TextColour colour;   ///< Colour to render text line in.
		int wrapped_top;     ///< Scroll position of line after text wrapping.
		const char *text;    ///< Pointer into window-owned text buffer.

		Line(const char *text) : colour(TC_WHITE), wrapped_top(0), text(text) { }
		Line() : colour(TC_WHITE), wrapped_top(0), text(nullptr) { }
	};
	struct Hyperlink {
		size_t line;             ///< Which line the link is on.
		size_t begin;            ///< Character position on line the link begins
		size_t end;              ///< Character position on line the link end
		std::string destination; ///< Destination for the link.
	};

	TextfileType file_type;          ///< Type of textfile to view.
	TextDirection text_direction;    ///< Known text direction of the file's content.
	Scrollbar *vscroll;              ///< Vertical scrollbar.
	Scrollbar *hscroll;              ///< Horizontal scrollbar.
	char *text;                      ///< Lines of text from the NewGRF's textfile.
	std::vector<Line> lines;         ///< Individual lines of text.
	std::vector<size_t> jumplist;    ///< Table of contents list, line numbers.
	std::vector<Hyperlink> links;    ///< Clickable links in lines.
	uint search_iterator;            ///< Iterator for the font check search.

	static const int TOP_SPACING    = WD_FRAMETEXT_TOP;    ///< Additional spacing at the top of the #WID_TF_BACKGROUND widget.
	static const int BOTTOM_SPACING = WD_FRAMETEXT_BOTTOM; ///< Additional spacing at the bottom of the #WID_TF_BACKGROUND widget.

	TextfileWindow(TextfileType file_type);
	~TextfileWindow();

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override;
	void OnClick(Point pt, int widget, int click_count) override;
	void DrawWidget(const Rect &r, int widget) const override;
	void OnResize() override;
	void OnDropdownSelect(int widget, int index) override;

	void Reset() override;
	FontSize DefaultSize() override;
	const char *NextString() override;
	bool Monospace() override;
	void SetFontNames(struct FreeTypeSettings *settings, const char *font_name, const void *os_data) override;
	void ScrollToLine(size_t line);

	virtual void LoadTextfile(const char *textfile, Subdirectory dir);
	virtual void FillJumplist() { }
	virtual void OnHyperlinkClick(const Hyperlink &link) { }

private:
	uint GetContentHeight();
	void SetupScrollbars();
	void CheckHyperlinkClick(Point pt);
};

#endif /* TEXTFILE_GUI_H */
