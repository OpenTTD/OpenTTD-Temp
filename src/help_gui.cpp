/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

 /** @file help_gui.cpp GUI to access manuals and related. */

#include "stdafx.h"
#include "gui.h"
#include "window_gui.h"
#include "textfile_gui.h"
#include "fileio_func.h"
#include <string>
#include <regex>
#include "table/control_codes.h"
#include "string_func.h"

#include "help_gui.h"
#include "widgets/help_widget.h"
#include "widgets/misc_widget.h"


extern void OpenBrowser(const char *url);


static const char README_FILENAME[] = "README.md";
static const char CHANGELOG_FILENAME[] = "changelog.txt";
static const char KNOWN_BUGS_FILENAME[] = "known-bugs.txt";
static const char LICENSE_FILENAME[] = "COPYING.md";

static std::string FindGameManualFilePath(const char *filename)
{
	/* FIXME: this is very arbitrary, should perhaps add a dedicated documentation searchpath? */
	static const Searchpath searchpaths[] = {
		SP_APPLICATION_BUNDLE_DIR, SP_INSTALLATION_DIR, SP_SHARED_DIR, SP_BINARY_DIR, SP_WORKING_DIR
	};

	std::string file_path;

	for (Searchpath sp : searchpaths) {
		file_path = FioGetDirectory(sp, BASE_DIR) + filename;
		if (FioCheckFileExists(file_path, NO_DIRECTORY)) return file_path;
	}
	return "";
}


struct GameManualTextfileWindow : public TextfileWindow {
	struct HistoryEntry {
		std::string filepath;
		int scrollpos;
	};

	std::string filename;
	std::string filepath;
	std::vector<Hyperlink> link_anchors; ///< Anchor names of headings that can be linked to

	std::vector<HistoryEntry> history;
	size_t history_pos;

	static const std::regex markdown_link_regex;

	enum class LinkType {
		Internal, Web, File, Unknown
	};
	static LinkType ClassifyHyperlink(const std::string &destination)
	{
		if (destination.empty()) return LinkType::Unknown;
		if (destination[0] == '#') return LinkType::Internal;
		if (destination.compare(0, 4, "http") == 0) return LinkType::Web;
		if (destination.compare(0, 2, "./") == 0) return LinkType::File;
		return LinkType::Unknown;
	}

	static std::string MakeAnchorSlug(const char *line)
	{
		std::string r = "#";
		uint state = 0;
		for (const char *c = line; *c != '\0'; ++c) {
			/* State 0: Skip leading hashmarks and spaces. */
			if (state == 0) {
				if (*c == '#') continue;
				if (*c == ' ') continue;
				state = 1;
			}
			if (state == 2) {
				/* State 2: Wait for a non-space/dash character.
				 * When found, output a dash and that character. */
				if (*c == ' ' || *c == '-') continue;
				r += '-';
				state = 1;
			}
			if (state == 1) {
				/* State 1: Normal text.
				 * Lowercase alphanumerics,
				 * spaces and dashes become dashes,
				 * everything else is removed. */
				if (isalnum(*c)) {
					r += tolower(*c);
				} else if (*c == ' ' || *c == '-') {
					state = 2;
				}
			}
		}

		return r;
	}

	GameManualTextfileWindow(const char *filename) : TextfileWindow(TFT_GAME_MANUAL), filename(filename), history_pos(0)
	{
		this->filepath = FindGameManualFilePath(filename);
		if (!filepath.empty()) this->LoadTextfile(filepath.c_str(), NO_DIRECTORY);
		this->history.push_back(HistoryEntry{ this->filepath, 0 });
		this->DisableWidget(WID_TF_NAVBACK);
		this->DisableWidget(WID_TF_NAVFORWARD);
		this->OnClick({ 0, 0 }, WID_TF_WRAPTEXT, 1);
	}

	void NavigateToFile(const std::string &newfile, size_t line)
	{
		/* Double-check that the file link begins with ./ as a relative path */
		if (newfile.substr(0, 2) != "./") return;

		/* Get the path portion of the current file path */
		std::string newpath = this->filepath;
		size_t pos = newpath.find_last_of(PATHSEPCHAR);
		if (pos == std::string::npos) {
			newpath.clear();
		} else {
			newpath.erase(pos + 1);
		}

		/* Convert link destination to acceptable local filename (replace forward slashes with correct path separator) */
		std::string newfn = newfile.substr(2);
		if (PATHSEPCHAR != '/') {
			for (char &c : newfn) {
				if (c == '/') c = PATHSEPCHAR;
			}
		}

		/* Check for anchor in link */
		size_t anchor_pos = newfn.find_last_of('#');
		std::string anchor;
		if (anchor_pos != std::string::npos) {
			anchor = newfn.substr(anchor_pos);
			newfn.erase(anchor_pos);
		}

		/* Paste the two together and check file exists */
		newpath = newpath + newfn;
		if (!FioCheckFileExists(newpath.c_str(), NO_DIRECTORY)) return;

		/* Update history */
		this->AppendHistory(newpath);

		/* Load the new file */
		this->filepath = newpath;
		this->filename = newpath.substr(newpath.find_last_of(PATHSEP) + 1);
		this->LoadTextfile(this->filepath.c_str(), NO_DIRECTORY);
		this->GetScrollbar(WID_TF_HSCROLLBAR)->SetPosition(0);
		this->GetScrollbar(WID_TF_VSCROLLBAR)->SetPosition(0);
		if (anchor.empty() || line != 0) {
			this->ScrollToLine(line);
		} else {
			auto anchor_dest = std::find_if(this->link_anchors.cbegin(), this->link_anchors.cend(), [&](const Hyperlink &other) { return anchor == other.destination; });
			if (anchor_dest != this->link_anchors.cend()) {
				this->ScrollToLine(anchor_dest->line);
				this->UpdateHistoryScrollpos();
			}
		}
	}

	void SetStringParameters(int widget) const override
	{
		if (widget == WID_TF_CAPTION) {
			SetDParamStr(0, this->filename.c_str());
		}
	}

	void AppendHistory(const std::string &filepath)
	{
		this->history.erase(this->history.begin() + this->history_pos + 1, this->history.end());
		this->UpdateHistoryScrollpos();
		this->history.push_back(HistoryEntry{ filepath, 0 });
		this->EnableWidget(WID_TF_NAVBACK);
		this->DisableWidget(WID_TF_NAVFORWARD);
		this->history_pos = this->history.size() - 1;
	}

	void UpdateHistoryScrollpos()
	{
		this->history[this->history_pos].scrollpos = this->GetScrollbar(WID_TF_VSCROLLBAR)->GetPosition();
	}

	void NavigateHistory(int delta)
	{
		if (delta == 0) return;
		if (delta < 0 && (int)this->history_pos < -delta) return;
		if (delta > 0 && (int)this->history_pos + delta >= this->history.size()) return;

		this->UpdateHistoryScrollpos();
		this->history_pos += delta;

		if (this->history[this->history_pos].filepath != this->filepath) {
			this->filepath = this->history[this->history_pos].filepath;
			this->filename = this->filepath.substr(this->filepath.find_last_of(PATHSEP) + 1);
			this->LoadTextfile(this->filepath.c_str(), NO_DIRECTORY);
		}

		this->SetWidgetDisabledState(WID_TF_NAVFORWARD, this->history_pos + 1 >= this->history.size());
		this->SetWidgetDisabledState(WID_TF_NAVBACK, this->history_pos == 0);
		this->GetScrollbar(WID_TF_VSCROLLBAR)->SetPosition(this->history[this->history_pos].scrollpos);
		this->GetScrollbar(WID_TF_HSCROLLBAR)->SetPosition(0);
		this->SetDirty();
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_TF_NAVBACK:
				this->NavigateHistory(-1);
				break;
			case WID_TF_NAVFORWARD:
				this->NavigateHistory(+1);
				break;
			default:
				this->TextfileWindow::OnClick(pt, widget, click_count);
				break;
		}
	}

	void FindHyperlinkInMarkdown(Line &line, size_t line_index)
	{
		const char *last_match_end = line.text;
		std::string fixed_line;
		char ccbuf[5];

		std::cregex_iterator matcher{ line.text, line.text + strlen(line.text), markdown_link_regex };
		while (matcher != std::cregex_iterator()) {
			std::cmatch match = *matcher;

			Hyperlink link;
			link.line = line_index;
			link.destination = std::string(match[2].first, match[2].length());
			this->links.push_back(link);

			LinkType link_type = ClassifyHyperlink(link.destination);
			StringControlCode link_colour;
			switch (link_type) {
				case LinkType::Internal:
					link_colour = SCC_GREEN;
					break;
				case LinkType::Web:
					link_colour = SCC_LTBLUE;
					break;
				case LinkType::File:
					link_colour = SCC_LTBROWN;
					break;
				default:
					/* Don't make other link types fancy as they aren't handled (yet) */
					link_colour = SCC_CONTROL_END;
					break;
			}

			if (link_colour != SCC_CONTROL_END) {
				/* Format the link to look like a link */
				fixed_line += std::string(last_match_end, match[0].first - last_match_end);
				this->links.back().begin = fixed_line.length();
				fixed_line += std::string(ccbuf, Utf8Encode(ccbuf, link_colour));
				fixed_line += std::string(match[1].first, match[1].length());
				this->links.back().end = fixed_line.length();
				fixed_line += std::string(ccbuf, Utf8Encode(ccbuf, SCC_BLUE + line.colour));
				last_match_end = match[0].second;
			}

			/* Find next link */
			++matcher;
		}
		if (last_match_end == line.text) return; // nothing found

		/* Add remaining text on line */
		fixed_line += std::string(last_match_end);

		/* Overwrite original line text with "fixed" line text */
		char *linetext = this->text + (line.text - this->text); // yes this is effectively a const cast
		if (fixed_line.size() > strlen(line.text)) return; // don't overwrite line if original is somehow shorter
		strecpy(linetext, fixed_line.c_str(), linetext + strlen(linetext));

	}

	void OnHyperlinkClick(const Hyperlink &link) override
	{
		switch (ClassifyHyperlink(link.destination)) {
			case LinkType::Internal: {
				auto it = std::find_if(this->link_anchors.cbegin(), this->link_anchors.cend(), [&](const Hyperlink &other) { return link.destination == other.destination; });
				if (it != this->link_anchors.cend()) {
					this->AppendHistory(this->filepath);
					this->ScrollToLine(it->line);
					this->UpdateHistoryScrollpos();
				}
				break;
			}
			case LinkType::Web:
				OpenBrowser(link.destination.c_str());
				break;
			case LinkType::File:
				this->NavigateToFile(link.destination, 0);
				break;
			default:
				/* Do nothing */
				break;
		}
	}

	void FillJumplist() override
	{
		this->link_anchors.clear();

		if (this->filename == CHANGELOG_FILENAME) this->FillJumplistChangelog();

		const char *ext = strrchr(this->filename.c_str(), '.');
		if (ext != nullptr && strcmp(ext, ".md") == 0) this->FillJumplistMarkdown();
	}

	void FillJumplistMarkdown()
	{
		/* Algorithm: All lines beginning with # are a heading. */
		for (size_t line = 0; line < this->lines.size(); ++line) {
			const char *line_text = this->lines[line].text;
			if (line_text != nullptr && line_text[0] == '#') {
				this->jumplist.push_back(line);
				this->lines[line].colour = TC_GOLD;
				this->link_anchors.push_back(Hyperlink{ line, 0, 0, MakeAnchorSlug(line_text) });
			} else {
				this->FindHyperlinkInMarkdown(this->lines[line], line);
			}
		}
	}

	void FillJumplistChangelog()
	{
		/* Algorithm: Look for lines beginning with ---, they indicate that the previous line was a release name. */
		for (size_t line = 0; line < this->lines.size(); ++line) {
			const char *line_text = this->lines[line].text;
			if (line_text != nullptr && strstr(line_text, "---") == line_text) {
				if (this->jumplist.size() >= 20) {
					/* Hack: limit changelog to 20 versions to prevent file viewer from becoming too long */
					this->lines.resize(line - 2);
					break;
				}
				this->lines[line - 1].colour = TC_GOLD;
				this->lines[line].colour = TC_GOLD;
				this->jumplist.push_back(line - 1);
			}
		}
	}
};

/** Regular expression that searches for Markdown links */
const std::regex GameManualTextfileWindow::markdown_link_regex{"\\[(.+?)\\]\\((.+?)\\)", std::regex_constants::ECMAScript | std::regex_constants::optimize};


struct HelpWindow : public Window {

	HelpWindow(WindowDesc *desc, WindowNumber number) : Window(desc)
	{
		this->InitNested(number);

		this->EnableTextfileButton(README_FILENAME, WID_HW_README);
		this->EnableTextfileButton(CHANGELOG_FILENAME, WID_HW_CHANGELOG);
		this->EnableTextfileButton(KNOWN_BUGS_FILENAME, WID_HW_KNOWN_BUGS);
		this->EnableTextfileButton(LICENSE_FILENAME, WID_HW_LICENSE);
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_HW_README:
				new GameManualTextfileWindow(README_FILENAME);
				break;
			case WID_HW_CHANGELOG:
				new GameManualTextfileWindow(CHANGELOG_FILENAME);
				break;
			case WID_HW_KNOWN_BUGS:
				new GameManualTextfileWindow(KNOWN_BUGS_FILENAME);
				break;
			case WID_HW_LICENSE:
				new GameManualTextfileWindow(LICENSE_FILENAME);
				break;
			case WID_HW_WEBSITE:
				OpenBrowser("https://www.openttd.org/");
				break;
			case WID_HW_WIKI:
				OpenBrowser("https://wiki.openttd.org/");
				break;
			case WID_HW_BUGTRACKER:
				OpenBrowser("https://github.openttd.org/");
				break;
			case WID_HW_COMMUNITY_CONTACT:
				OpenBrowser("https://www.openttd.org/contact.html");
				break;
		}
	}

private:
	void EnableTextfileButton(const char *filename, int button_widget)
	{
		this->GetWidget<NWidgetLeaf>(button_widget)->SetDisabled(FindGameManualFilePath(filename).empty());
	}
};

static const NWidgetPart _nested_helpwin_widgets[] = {
	NWidget(NWID_HORIZONTAL), // Window header
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN), SetDataTip(STR_HELPWIN_CAPTION, STR_NULL),
	EndContainer(),

	NWidget(WWT_PANEL, COLOUR_DARK_GREEN),
		NWidget(NWID_SPACER), SetMinimalSize(0, 8),

		NWidget(NWID_HORIZONTAL),
			NWidget(NWID_SPACER), SetMinimalSize(10, 0),

			NWidget(NWID_VERTICAL), SetPIP(0, 2, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREEN, WID_HW_README), SetDataTip(STR_HELPWIN_README, STR_NULL), SetMinimalSize(128, 12), SetFill(1, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREEN, WID_HW_CHANGELOG), SetDataTip(STR_HELPWIN_CHANGELOG, STR_NULL), SetMinimalSize(128, 12), SetFill(1, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREEN, WID_HW_KNOWN_BUGS),SetDataTip(STR_HELPWIN_KNOWN_BUGS, STR_NULL), SetMinimalSize(128, 12), SetFill(1, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREEN, WID_HW_LICENSE), SetDataTip(STR_HELPWIN_LICENSE, STR_NULL), SetMinimalSize(128, 12), SetFill(1, 0),
			EndContainer(),

			NWidget(NWID_SPACER), SetMinimalSize(10, 0),

			NWidget(NWID_VERTICAL), SetPIP(0, 2, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREEN, WID_HW_WEBSITE), SetDataTip(STR_HELPWIN_WEBSITE, STR_NULL), SetMinimalSize(128, 12), SetFill(1, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREEN, WID_HW_WIKI), SetDataTip(STR_HELPWIN_WIKI, STR_NULL), SetMinimalSize(128, 12), SetFill(1, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREEN, WID_HW_BUGTRACKER), SetDataTip(STR_HELPWIN_BUGTRACKER, STR_NULL), SetMinimalSize(128, 12), SetFill(1, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREEN, WID_HW_COMMUNITY_CONTACT), SetDataTip(STR_HELPWIN_COMMUNITY_CONTACT, STR_NULL), SetMinimalSize(128, 12), SetFill(1, 0),
			EndContainer(),

			NWidget(NWID_SPACER), SetMinimalSize(10, 0),
		EndContainer(),

		NWidget(NWID_SPACER), SetMinimalSize(0, 8),
	EndContainer(),
};

static WindowDesc _helpwin_desc(
	WDP_CENTER, nullptr, 0, 0,
	WC_HELPWIN, WC_NONE,
	0,
	_nested_helpwin_widgets, lengthof(_nested_helpwin_widgets)
);

void ShowHelpWindow()
{
	AllocateWindowDescFront<HelpWindow>(&_helpwin_desc, 0);
}
