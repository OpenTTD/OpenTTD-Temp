/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file textfile_gui.cpp Implementation of textfile window. */

#include "stdafx.h"
#include "fileio_func.h"
#include "fontcache.h"
#include "gfx_type.h"
#include "gfx_func.h"
#include "string_func.h"
#include "textfile_gui.h"
#include "widgets/dropdown_type.h"
#include "gfx_layout.h"
#include "debug.h"
#include <algorithm>

#include "widgets/misc_widget.h"

#include "table/strings.h"

#if defined(WITH_ZLIB)
#include <zlib.h>
#endif

#if defined(WITH_LIBLZMA)
#include <lzma.h>
#endif

#include "safeguards.h"

/** Widgets for the textfile window. */
static const NWidgetPart _nested_textfile_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_MAUVE),
		NWidget(WWT_PUSHARROWBTN, COLOUR_MAUVE, WID_TF_NAVBACK), SetFill(0, 1), SetMinimalSize(15, 1), SetDataTip(AWV_DECREASE, STR_TEXTFILE_NAVBACK_TOOLTIP),
		NWidget(WWT_PUSHARROWBTN, COLOUR_MAUVE, WID_TF_NAVFORWARD), SetFill(0, 1), SetMinimalSize(15, 1), SetDataTip(AWV_INCREASE, STR_TEXTFILE_NAVFORWARD_TOOLTIP),
		NWidget(WWT_CAPTION, COLOUR_MAUVE, WID_TF_CAPTION), SetDataTip(STR_NULL, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_TEXTBTN, COLOUR_MAUVE, WID_TF_WRAPTEXT), SetDataTip(STR_TEXTFILE_WRAP_TEXT, STR_TEXTFILE_WRAP_TEXT_TOOLTIP),
		NWidget(WWT_DEFSIZEBOX, COLOUR_MAUVE),
	EndContainer(),
	NWidget(WWT_DROPDOWN, COLOUR_MAUVE, WID_TF_JUMPLIST), SetDataTip(STR_TEXTFILE_JUMPLIST, STR_TEXTFILE_JUMPLIST_TOOLTIP), SetResize(1, 0),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_MAUVE, WID_TF_BACKGROUND), SetMinimalSize(200, 125), SetResize(1, 12), SetScrollbar(WID_TF_VSCROLLBAR),
		EndContainer(),
		NWidget(NWID_VERTICAL),
			NWidget(NWID_VSCROLLBAR, COLOUR_MAUVE, WID_TF_VSCROLLBAR),
		EndContainer(),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_HSCROLLBAR, COLOUR_MAUVE, WID_TF_HSCROLLBAR),
		NWidget(WWT_RESIZEBOX, COLOUR_MAUVE),
	EndContainer(),
};

/** Window definition for the textfile window */
static WindowDesc _textfile_desc(
	WDP_CENTER, "textfile", 630, 460,
	WC_TEXTFILE, WC_NONE,
	0,
	_nested_textfile_widgets, lengthof(_nested_textfile_widgets)
);

TextfileWindow::TextfileWindow(TextfileType file_type) : Window(&_textfile_desc), file_type(file_type), text_direction(TD_LTR), text(nullptr), search_iterator(0)
{
	this->CreateNestedTree();
	this->vscroll = this->GetScrollbar(WID_TF_VSCROLLBAR);
	this->hscroll = this->GetScrollbar(WID_TF_HSCROLLBAR);
	this->FinishInitNested(file_type);
	this->GetWidget<NWidgetCore>(WID_TF_CAPTION)->SetDataTip(STR_TEXTFILE_README_CAPTION + file_type, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS);

	this->hscroll->SetStepSize(10); // Speed up horizontal scrollbar
	this->vscroll->SetStepSize(FONT_HEIGHT_MONO);
}

/* virtual */ TextfileWindow::~TextfileWindow()
{
	free(this->text);
}

/**
 * Get the total height of the content displayed in this window, if wrapping is disabled.
 * @return the height in pixels
 */
uint TextfileWindow::GetContentHeight()
{
	int max_width = this->GetWidget<NWidgetCore>(WID_TF_BACKGROUND)->current_x - WD_FRAMETEXT_LEFT - WD_FRAMERECT_RIGHT;

	uint height = 0;
	for (uint i = 0; i < this->lines.size(); i++) {
		this->lines[i].wrapped_top = height;
		height += GetStringHeight(this->lines[i].text, max_width, FS_MONO);
	}

	return height;
}

/* virtual */ void TextfileWindow::UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
{
	switch (widget) {
		case WID_TF_BACKGROUND:
			resize->height = 1;

			size->height = 4 * resize->height + TOP_SPACING + BOTTOM_SPACING; // At least 4 lines are visible.
			size->width = std::max(200u, size->width); // At least 200 pixels wide.
			break;

		case WID_TF_JUMPLIST:
			if (this->jumplist.empty()) {
				size->height = 0;
			} else {
				size->height = FONT_HEIGHT_NORMAL + WD_DROPDOWNTEXT_TOP + WD_DROPDOWNTEXT_BOTTOM;
			}
			break;

		case WID_TF_NAVBACK:
		case WID_TF_NAVFORWARD:
			if (this->file_type != TFT_GAME_MANUAL) size->width = 0;
			break;
	}
}

/** Set scrollbars to the right lengths. */
void TextfileWindow::SetupScrollbars()
{
	if (IsWidgetLowered(WID_TF_WRAPTEXT)) {
		this->vscroll->SetCount(this->GetContentHeight());
		this->hscroll->SetCount(0);
	} else {
		uint max_length = 0;
		for (uint i = 0; i < this->lines.size(); i++) {
			max_length = std::max(max_length, GetStringBoundingBox(this->lines[i].text, FS_MONO).width);
		}
		this->vscroll->SetCount((uint)this->lines.size() * FONT_HEIGHT_MONO);
		this->hscroll->SetCount(max_length + WD_FRAMETEXT_LEFT + WD_FRAMETEXT_RIGHT);
	}

	this->SetWidgetDisabledState(WID_TF_HSCROLLBAR, IsWidgetLowered(WID_TF_WRAPTEXT));
}

void TextfileWindow::CheckHyperlinkClick(Point pt)
{
	if (this->links.empty()) return;

	/* Which line was clicked */
	const int y = this->GetRowFromWidget(pt.y, WID_TF_BACKGROUND, WD_FRAMETEXT_TOP, 1) + this->GetScrollbar(WID_TF_VSCROLLBAR)->GetPosition();
	int line;
	int subline;
	if (IsWidgetLowered(WID_TF_WRAPTEXT)) {
		auto it = std::lower_bound(this->lines.cbegin(), this->lines.cend(), y, [](const Line &l, int y) { return l.wrapped_top <= y; });
		if (it == this->lines.cend()) return;
		--it; // lower_bound has found the first line past the one clicked, so take one step back
		line = it - this->lines.cbegin();
		subline = (y - it->wrapped_top) / FONT_HEIGHT_MONO;
		DEBUG(misc, 2, "TextfileWindow check hyperlink: line=%d y=%d, line.wrapped_top=%d, subline=%d", line, y, it->wrapped_top, subline);
	} else {
		line = y / FONT_HEIGHT_MONO;
		subline = 0;
	}

	/* Find hyperlinks in this line */
	std::vector<Hyperlink> found_links;
	for (const auto &link : this->links) {
		if (link.line == (size_t)line) found_links.push_back(link);
	}
	if (found_links.empty()) return;

	/* Build line layout to figure out character position that was clicked */
	uint window_width = IsWidgetLowered(WID_TF_WRAPTEXT) ? this->GetWidget<NWidgetCore>(WID_TF_BACKGROUND)->current_x - WD_FRAMETEXT_LEFT - WD_FRAMETEXT_RIGHT : INT_MAX;
	Layouter layout(this->lines[line].text, window_width, this->lines[line].colour, FS_MONO);
	assert(subline >= 0);
	assert((size_t)subline < layout.size());
	const char *found_pos = layout.GetCharAtPosition(pt.x - WD_FRAMETEXT_LEFT, subline);
	if (found_pos == nullptr) return;
	ptrdiff_t char_index = found_pos - this->lines[line].text;
	assert(char_index >= 0);
	DEBUG(misc, 2, "TextfileWindow check hyperlink click: line=%d, subline=%d, char_index=%d", line, subline, (int)char_index);

	/* Found character index in line, check if any links are at that position */
	for (const auto &link : found_links) {
		DEBUG(misc, 2, "Checking link from char " PRINTF_SIZE " to " PRINTF_SIZE, link.begin, link.end);
		if ((size_t)char_index >= link.begin && (size_t)char_index < link.end) {
			this->OnHyperlinkClick(link);
			return;
		}
	}
}

/* virtual */ void TextfileWindow::OnClick(Point pt, int widget, int click_count)
{
	switch (widget) {
		case WID_TF_WRAPTEXT:
			this->ToggleWidgetLoweredState(WID_TF_WRAPTEXT);
			this->SetupScrollbars();
			this->InvalidateData();
			break;

		case WID_TF_JUMPLIST: {
			DropDownList list;
			for (size_t line : this->jumplist) {
				DropDownListCharStringItem *item = new DropDownListCharStringItem(this->lines[line].text, (int)line, false);
				item->string = STR_TEXTFILE_JUMPLIST_ITEM;
				list.emplace_back(item);
			}
			ShowDropDownList(this, std::move(list), -1, widget);
			break;
		}

		case WID_TF_BACKGROUND:
			CheckHyperlinkClick(pt);
			break;
	}
}

/* virtual */ void TextfileWindow::DrawWidget(const Rect &r, int widget) const
{
	if (widget != WID_TF_BACKGROUND) return;

	const int x = r.left + WD_FRAMETEXT_LEFT;
	const int y = r.top + WD_FRAMETEXT_TOP;
	const int right = r.right - WD_FRAMETEXT_RIGHT;
	const int bottom = r.bottom - WD_FRAMETEXT_BOTTOM;

	DrawPixelInfo new_dpi;
	if (!FillDrawPixelInfo(&new_dpi, x, y, right - x + 1, bottom - y + 1)) return;
	DrawPixelInfo *old_dpi = _cur_dpi;
	_cur_dpi = &new_dpi;

	/* Draw content (now coordinates given to DrawString* are local to the new clipping region). */
	int line_height = FONT_HEIGHT_MONO;
	int y_offset = -this->vscroll->GetPosition();

	TextDirection old_text_dir = _current_text_dir;
	_current_text_dir = this->text_direction;

	for (uint i = 0; i < this->lines.size(); i++) {
		const Line &line = this->lines[i];
		if (IsWidgetLowered(WID_TF_WRAPTEXT)) {
			/* Check that line is visible before trying to draw */
			if (i < this->lines.size() - 1 && this->lines[i + 1].wrapped_top <= -y_offset) continue;
			if (line.wrapped_top >= bottom - y - y_offset) break;
			/* Draw the line */
			DrawStringMultiLine(0, right - x, line.wrapped_top + y_offset, bottom - y, line.text, line.colour, SA_TOP | SA_LEFT, false, FS_MONO);
		} else {
			DrawString(-this->hscroll->GetPosition(), right - x, y_offset, line.text, line.colour, SA_TOP | SA_LEFT, false, FS_MONO);
			y_offset += line_height; // margin to previous element
		}
	}

	_current_text_dir = old_text_dir;
	_cur_dpi = old_dpi;
}

/* virtual */ void TextfileWindow::OnResize()
{
	this->vscroll->SetCapacityFromWidget(this, WID_TF_BACKGROUND, TOP_SPACING + BOTTOM_SPACING);
	this->hscroll->SetCapacityFromWidget(this, WID_TF_BACKGROUND);

	this->SetupScrollbars();
}

void TextfileWindow::OnDropdownSelect(int widget, int index)
{
	if (widget != WID_TF_JUMPLIST) return;

	this->ScrollToLine(index);
}

void TextfileWindow::ScrollToLine(size_t line)
{
	Scrollbar *sb = this->GetScrollbar(WID_TF_VSCROLLBAR);
	int newpos;
	if (this->IsWidgetLowered(WID_TF_WRAPTEXT)) {
		newpos = this->lines[line].wrapped_top;
	} else {
		newpos = (int)line * FONT_HEIGHT_MONO;
	}
	sb->SetPosition(std::min(newpos, sb->GetCount() - sb->GetCapacity()));
	this->SetDirty();
}

/* virtual */ void TextfileWindow::Reset()
{
	this->search_iterator = 0;
}

/* virtual */ FontSize TextfileWindow::DefaultSize()
{
	return FS_MONO;
}

/* virtual */ const char *TextfileWindow::NextString()
{
	if (this->search_iterator >= this->lines.size()) return nullptr;

	return this->lines[this->search_iterator++].text;
}

/* virtual */ bool TextfileWindow::Monospace()
{
	return true;
}

/* virtual */ void TextfileWindow::SetFontNames(FreeTypeSettings *settings, const char *font_name, const void *os_data)
{
#if defined(WITH_FREETYPE) || defined(_WIN32) || defined(WITH_COCOA)
	strecpy(settings->mono.font, font_name, lastof(settings->mono.font));
	settings->mono.os_handle = os_data;
#endif
}

#if defined(WITH_ZLIB)

/**
 * Do an in-memory gunzip operation. This works on a raw deflate stream,
 * or a file with gzip or zlib header.
 * @param bufp  A pointer to a buffer containing the input data. This
 *              buffer will be freed and replaced by a buffer containing
 *              the uncompressed data.
 * @param sizep A pointer to the buffer size. Before the call, the value
 *              pointed to should contain the size of the input buffer.
 *              After the call, it contains the size of the uncompressed
 *              data.
 *
 * When decompressing fails, *bufp is set to nullptr and *sizep to 0. The
 * compressed buffer passed in is still freed in this case.
 */
static void Gunzip(byte **bufp, size_t *sizep)
{
	static const int BLOCKSIZE  = 8192;
	byte             *buf       = nullptr;
	size_t           alloc_size = 0;
	z_stream         z;
	int              res;

	memset(&z, 0, sizeof(z));
	z.next_in = *bufp;
	z.avail_in = (uInt)*sizep;

	/* window size = 15, add 32 to enable gzip or zlib header processing */
	res = inflateInit2(&z, 15 + 32);
	/* Z_BUF_ERROR just means we need more space */
	while (res == Z_OK || (res == Z_BUF_ERROR && z.avail_out == 0)) {
		/* When we get here, we're either just starting, or
		 * inflate is out of output space - allocate more */
		alloc_size += BLOCKSIZE;
		z.avail_out += BLOCKSIZE;
		buf = ReallocT(buf, alloc_size);
		z.next_out = buf + alloc_size - z.avail_out;
		res = inflate(&z, Z_FINISH);
	}

	free(*bufp);
	inflateEnd(&z);

	if (res == Z_STREAM_END) {
		*bufp = buf;
		*sizep = alloc_size - z.avail_out;
	} else {
		/* Something went wrong */
		*bufp = nullptr;
		*sizep = 0;
		free(buf);
	}
}
#endif

#if defined(WITH_LIBLZMA)

/**
 * Do an in-memory xunzip operation. This works on a .xz or (legacy)
 * .lzma file.
 * @param bufp  A pointer to a buffer containing the input data. This
 *              buffer will be freed and replaced by a buffer containing
 *              the uncompressed data.
 * @param sizep A pointer to the buffer size. Before the call, the value
 *              pointed to should contain the size of the input buffer.
 *              After the call, it contains the size of the uncompressed
 *              data.
 *
 * When decompressing fails, *bufp is set to nullptr and *sizep to 0. The
 * compressed buffer passed in is still freed in this case.
 */
static void Xunzip(byte **bufp, size_t *sizep)
{
	static const int BLOCKSIZE  = 8192;
	byte             *buf       = nullptr;
	size_t           alloc_size = 0;
	lzma_stream      z = LZMA_STREAM_INIT;
	int              res;

	z.next_in = *bufp;
	z.avail_in = *sizep;

	res = lzma_auto_decoder(&z, UINT64_MAX, LZMA_CONCATENATED);
	/* Z_BUF_ERROR just means we need more space */
	while (res == LZMA_OK || (res == LZMA_BUF_ERROR && z.avail_out == 0)) {
		/* When we get here, we're either just starting, or
		 * inflate is out of output space - allocate more */
		alloc_size += BLOCKSIZE;
		z.avail_out += BLOCKSIZE;
		buf = ReallocT(buf, alloc_size);
		z.next_out = buf + alloc_size - z.avail_out;
		res = lzma_code(&z, LZMA_FINISH);
	}

	free(*bufp);
	lzma_end(&z);

	if (res == LZMA_STREAM_END) {
		*bufp = buf;
		*sizep = alloc_size - z.avail_out;
	} else {
		/* Something went wrong */
		*bufp = nullptr;
		*sizep = 0;
		free(buf);
	}
}
#endif


/**
 * Loads the textfile text from file and setup #lines.
 */
/* virtual */ void TextfileWindow::LoadTextfile(const char *textfile, Subdirectory dir)
{
	if (textfile == nullptr) return;

	this->lines.clear();
	this->jumplist.clear();

	/* Get text from file */
	size_t filesize;
	FILE *handle = FioFOpenFile(textfile, "rb", dir, &filesize);
	if (handle == nullptr) return;

	this->text = ReallocT(this->text, filesize);
	size_t read = fread(this->text, 1, filesize, handle);
	fclose(handle);

	if (read != filesize) return;

#if defined(WITH_ZLIB) || defined(WITH_LIBLZMA)
	const char *suffix = strrchr(textfile, '.');
	if (suffix != nullptr) {

# if defined(WITH_ZLIB)
		/* In-place gunzip */
		if (strcmp(suffix, ".gz") == 0) Gunzip((byte **)&this->text, &filesize);
# endif

# if defined(WITH_LIBLZMA)
		/* In-place xunzip */
		if (strcmp(suffix, ".xz") == 0) Xunzip((byte **)&this->text, &filesize);
# endif
	}
#endif

	if (!this->text) return;

	/* Add space for trailing \0 */
	this->text = ReallocT(this->text, filesize + 1);
	this->text[filesize] = '\0';

	/* Replace tabs and line feeds with a space since str_validate removes those. */
	for (char *p = this->text; *p != '\0'; p++) {
		if (*p == '\t' || *p == '\r') *p = ' ';
	}

	/* Check for the byte-order-mark, and skip it if needed. */
	char *p = this->text + (strncmp(u8"\ufeff", this->text, 3) == 0 ? 3 : 0);

	/* Make sure the string is a valid UTF-8 sequence. */
	str_validate(p, this->text + filesize, SVS_REPLACE_WITH_QUESTION_MARK | SVS_ALLOW_NEWLINE);

	/* Split the string on newlines. */
	this->lines.push_back(p);
	for (; *p != '\0'; p++) {
		if (*p == '\n') {
			*p = '\0';
			this->lines.push_back(p + 1);
		}
	}

	this->FillJumplist();

	CheckForMissingGlyphs(true, this);
}

/**
 * Search a textfile file next to the given content.
 * @param type The type of the textfile to search for.
 * @param dir The subdirectory to search in.
 * @param filename The filename of the content to look for.
 * @return The path to the textfile, \c nullptr otherwise.
 */
const char *GetTextfile(TextfileType type, Subdirectory dir, const char *filename, TextDirection *text_dir)
{
	static const char * const prefixes[] = {
		"readme",
		"changelog",
		"license",
		"", // game manual, should not be used with this function
	};
	static_assert(lengthof(prefixes) == TFT_END);

	const char *prefix = prefixes[type];

	if (filename == nullptr) return nullptr;

	static char file_path[MAX_PATH];
	strecpy(file_path, filename, lastof(file_path));

	char *slash = strrchr(file_path, PATHSEPCHAR);
	if (slash == nullptr) return nullptr;

	static const char * const exts[] = {
		"txt",
#if defined(WITH_ZLIB)
		"txt.gz",
#endif
#if defined(WITH_LIBLZMA)
		"txt.xz",
#endif
	};

	for (size_t i = 0; i < lengthof(exts); i++) {
		if (text_dir != nullptr) *text_dir = _current_text_dir;
		seprintf(slash + 1, lastof(file_path), "%s_%s.%s", prefix, GetCurrentLanguageIsoCode(), exts[i]);
		if (FioCheckFileExists(file_path, dir)) return file_path;

		seprintf(slash + 1, lastof(file_path), "%s_%.2s.%s", prefix, GetCurrentLanguageIsoCode(), exts[i]);
		if (FioCheckFileExists(file_path, dir)) return file_path;

		if (text_dir != nullptr) *text_dir = TD_LTR;
		seprintf(slash + 1, lastof(file_path), "%s.%s", prefix, exts[i]);
		if (FioCheckFileExists(file_path, dir)) return file_path;
	}
	return nullptr;
}
