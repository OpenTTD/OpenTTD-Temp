/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file network_chat_gui.cpp GUI for handling chat messages. */

#include <stdarg.h> /* va_list */

#include "../stdafx.h"
#include "../strings_func.h"
#include "../blitter/factory.hpp"
#include "../console_func.h"
#include "../video/video_driver.hpp"
#include "../querystring_gui.h"
#include "../town.h"
#include "../window_func.h"
#include "../toolbar_gui.h"
#include "../core/geometry_func.hpp"
#include "network.h"
#include "network_client.h"
#include "network_base.h"
#include "social_presence.h"

#include "../widgets/network_chat_widget.h"

#include "table/strings.h"

#include "../safeguards.h"

/** The draw buffer must be able to contain the chat message, client name and the "[All]" message,
 * some spaces and possible translations of [All] to other languages. */
static_assert((int)DRAW_STRING_BUFFER >= (int)NETWORK_CHAT_LENGTH + NETWORK_NAME_LENGTH + 40);

/** Spacing between chat lines. */
static const uint NETWORK_CHAT_LINE_SPACING = 3;

/** Container for a message. */
struct ChatMessage {
	char message[DRAW_STRING_BUFFER]; ///< The action message.
	TextColour colour;  ///< The colour of the message.
	std::chrono::steady_clock::time_point remove_time; ///< The time to remove the message.
};

/* used for chat window */
static ChatMessage *_chatmsg_list = nullptr; ///< The actual chat message list.
static bool _chatmessage_dirty = false;   ///< Does the chat message need repainting?
static bool _chatmessage_visible = false; ///< Is a chat message visible.
static bool _chat_tab_completion_active;  ///< Whether tab completion is active.
static uint MAX_CHAT_MESSAGES = 0;        ///< The limit of chat messages to show.

/**
 * The chatbox grows from the bottom so the coordinates are pixels from
 * the left and pixels from the bottom. The height is the maximum height.
 */
static PointDimension _chatmsg_box;
static uint8 *_chatmessage_backup = nullptr; ///< Backup in case text is moved.

/**
 * Count the chat messages.
 * @return The number of chat messages.
 */
static inline uint GetChatMessageCount()
{
	uint i = 0;
	for (; i < MAX_CHAT_MESSAGES; i++) {
		if (_chatmsg_list[i].message[0] == '\0') break;
	}

	return i;
}

/**
 * Add a text message to the 'chat window' to be shown
 * @param colour The colour this message is to be shown in
 * @param duration The duration of the chat message in seconds
 * @param message message itself in printf() style
 */
void CDECL NetworkAddChatMessage(TextColour colour, uint duration, const char *message, ...)
{
	char buf[DRAW_STRING_BUFFER];
	va_list va;

	va_start(va, message);
	vseprintf(buf, lastof(buf), message, va);
	va_end(va);

	Utf8TrimString(buf, DRAW_STRING_BUFFER);

	uint msg_count = GetChatMessageCount();
	if (MAX_CHAT_MESSAGES == msg_count) {
		memmove(&_chatmsg_list[0], &_chatmsg_list[1], sizeof(_chatmsg_list[0]) * (msg_count - 1));
		msg_count = MAX_CHAT_MESSAGES - 1;
	}

	ChatMessage *cmsg = &_chatmsg_list[msg_count++];
	strecpy(cmsg->message, buf, lastof(cmsg->message));
	cmsg->colour = (colour & TC_IS_PALETTE_COLOUR) ? colour : TC_WHITE;
	cmsg->remove_time = std::chrono::steady_clock::now() + std::chrono::seconds(duration);

	_chatmessage_dirty = true;
}

/** Initialize all font-dependent chat box sizes. */
void NetworkReInitChatBoxSize()
{
	_chatmsg_box.y       = 3 * FONT_HEIGHT_NORMAL;
	_chatmsg_box.height  = MAX_CHAT_MESSAGES * (FONT_HEIGHT_NORMAL + NETWORK_CHAT_LINE_SPACING) + 4;
	_chatmessage_backup  = ReallocT(_chatmessage_backup, _chatmsg_box.width * _chatmsg_box.height * BlitterFactory::GetCurrentBlitter()->GetBytesPerPixel());
}

/** Initialize all buffers of the chat visualisation. */
void NetworkInitChatMessage()
{
	MAX_CHAT_MESSAGES    = _settings_client.gui.network_chat_box_height;

	_chatmsg_list        = ReallocT(_chatmsg_list, _settings_client.gui.network_chat_box_height);
	_chatmsg_box.x       = 10;
	_chatmsg_box.width   = _settings_client.gui.network_chat_box_width_pct * _screen.width / 100;
	NetworkReInitChatBoxSize();
	_chatmessage_visible = false;

	for (uint i = 0; i < MAX_CHAT_MESSAGES; i++) {
		_chatmsg_list[i].message[0] = '\0';
	}
}

/** Hide the chatbox */
void NetworkUndrawChatMessage()
{
	/* Sometimes we also need to hide the cursor
	 *   This is because both textmessage and the cursor take a shot of the
	 *   screen before drawing.
	 *   Now the textmessage takes his shot and paints his data before the cursor
	 *   does, so in the shot of the cursor is the screen-data of the textmessage
	 *   included when the cursor hangs somewhere over the textmessage. To
	 *   avoid wrong repaints, we undraw the cursor in that case, and everything
	 *   looks nicely ;)
	 * (and now hope this story above makes sense to you ;))
	 */
	if (_cursor.visible &&
			_cursor.draw_pos.x + _cursor.draw_size.x >= _chatmsg_box.x &&
			_cursor.draw_pos.x <= _chatmsg_box.x + _chatmsg_box.width &&
			_cursor.draw_pos.y + _cursor.draw_size.y >= _screen.height - _chatmsg_box.y - _chatmsg_box.height &&
			_cursor.draw_pos.y <= _screen.height - _chatmsg_box.y) {
		UndrawMouseCursor();
	}

	if (_chatmessage_visible) {
		Blitter *blitter = BlitterFactory::GetCurrentBlitter();
		int x      = _chatmsg_box.x;
		int y      = _screen.height - _chatmsg_box.y - _chatmsg_box.height;
		int width  = _chatmsg_box.width;
		int height = _chatmsg_box.height;
		if (y < 0) {
			height = std::max(height + y, std::min(_chatmsg_box.height, _screen.height));
			y = 0;
		}
		if (x + width >= _screen.width) {
			width = _screen.width - x;
		}
		if (width <= 0 || height <= 0) return;

		_chatmessage_visible = false;
		/* Put our 'shot' back to the screen */
		blitter->CopyFromBuffer(blitter->MoveTo(_screen.dst_ptr, x, y), _chatmessage_backup, width, height);
		/* And make sure it is updated next time */
		VideoDriver::GetInstance()->MakeDirty(x, y, width, height);

		_chatmessage_dirty = true;
	}
}

/** Check if a message is expired. */
void NetworkChatMessageLoop()
{
	for (uint i = 0; i < MAX_CHAT_MESSAGES; i++) {
		ChatMessage *cmsg = &_chatmsg_list[i];
		if (cmsg->message[0] == '\0') continue;

		/* Message has expired, remove from the list */
		if (std::chrono::steady_clock::now() > cmsg->remove_time) {
			/* Move the remaining messages over the current message */
			if (i != MAX_CHAT_MESSAGES - 1) memmove(cmsg, cmsg + 1, sizeof(*cmsg) * (MAX_CHAT_MESSAGES - i - 1));

			/* Mark the last item as empty */
			_chatmsg_list[MAX_CHAT_MESSAGES - 1].message[0] = '\0';
			_chatmessage_dirty = true;

			/* Go one item back, because we moved the array 1 to the left */
			i--;
		}
	}
}

/** Draw the chat message-box */
void NetworkDrawChatMessage()
{
	Blitter *blitter = BlitterFactory::GetCurrentBlitter();
	if (!_chatmessage_dirty) return;

	/* First undraw if needed */
	NetworkUndrawChatMessage();

	if (_iconsole_mode == ICONSOLE_FULL) return;

	/* Check if we have anything to draw at all */
	uint count = GetChatMessageCount();
	if (count == 0) return;

	int x      = _chatmsg_box.x;
	int y      = _screen.height - _chatmsg_box.y - _chatmsg_box.height;
	int width  = _chatmsg_box.width;
	int height = _chatmsg_box.height;
	if (y < 0) {
		height = std::max(height + y, std::min(_chatmsg_box.height, _screen.height));
		y = 0;
	}
	if (x + width >= _screen.width) {
		width = _screen.width - x;
	}
	if (width <= 0 || height <= 0) return;

	assert(blitter->BufferSize(width, height) <= (int)(_chatmsg_box.width * _chatmsg_box.height * blitter->GetBytesPerPixel()));

	/* Make a copy of the screen as it is before painting (for undraw) */
	blitter->CopyToBuffer(blitter->MoveTo(_screen.dst_ptr, x, y), _chatmessage_backup, width, height);

	_cur_dpi = &_screen; // switch to _screen painting

	int string_height = 0;
	for (uint i = 0; i < count; i++) {
		SetDParamStr(0, _chatmsg_list[i].message);
		string_height += GetStringLineCount(STR_JUST_RAW_STRING, width - 1) * FONT_HEIGHT_NORMAL + NETWORK_CHAT_LINE_SPACING;
	}

	string_height = std::min<uint>(string_height, MAX_CHAT_MESSAGES * (FONT_HEIGHT_NORMAL + NETWORK_CHAT_LINE_SPACING));

	int top = _screen.height - _chatmsg_box.y - string_height - 2;
	int bottom = _screen.height - _chatmsg_box.y - 2;
	/* Paint a half-transparent box behind the chat messages */
	GfxFillRect(_chatmsg_box.x, top - 2, _chatmsg_box.x + _chatmsg_box.width - 1, bottom,
			PALETTE_TO_TRANSPARENT, FILLRECT_RECOLOUR // black, but with some alpha for background
		);

	/* Paint the chat messages starting with the lowest at the bottom */
	int ypos = bottom - 2;

	for (int i = count - 1; i >= 0; i--) {
		ypos = DrawStringMultiLine(_chatmsg_box.x + 3, _chatmsg_box.x + _chatmsg_box.width - 1, top, ypos, _chatmsg_list[i].message, _chatmsg_list[i].colour, SA_LEFT | SA_BOTTOM | SA_FORCE) - NETWORK_CHAT_LINE_SPACING;
		if (ypos < top) break;
	}

	/* Make sure the data is updated next flush */
	VideoDriver::GetInstance()->MakeDirty(x, y, width, height);

	_chatmessage_visible = true;
	_chatmessage_dirty = false;
}

/**
 * Send an actual chat message.
 * @param buf The message to send.
 * @param type The type of destination.
 * @param dest The actual destination index.
 */
static void SendChat(const char *buf, DestType type, int dest)
{
	if (StrEmpty(buf)) return;
	if (!_network_server) {
		MyClient::SendChat((NetworkAction)(NETWORK_ACTION_CHAT + type), type, dest, buf, 0);
	} else {
		NetworkServerSendChat((NetworkAction)(NETWORK_ACTION_CHAT + type), type, dest, buf, CLIENT_ID_SERVER);
	}
}

/** Window to enter the chat message in. */
struct NetworkChatWindow : public Window {
	DestType dtype;       ///< The type of destination.
	StringID dest_string; ///< String representation of the destination.
	int dest;             ///< The identifier of the destination.
	QueryString message_editbox; ///< Message editbox.

	/**
	 * Create a chat input window.
	 * @param desc Description of the looks of the window.
	 * @param type The type of destination.
	 * @param dest The actual destination index.
	 */
	NetworkChatWindow(WindowDesc *desc, DestType type, int dest) : Window(desc), message_editbox(NETWORK_CHAT_LENGTH)
	{
		this->dtype   = type;
		this->dest    = dest;
		this->querystrings[WID_NC_TEXTBOX] = &this->message_editbox;
		this->message_editbox.cancel_button = WID_NC_CLOSE;
		this->message_editbox.ok_button = WID_NC_SENDBUTTON;

		static const StringID chat_captions[] = {
			STR_NETWORK_CHAT_ALL_CAPTION,
			STR_NETWORK_CHAT_COMPANY_CAPTION,
			STR_NETWORK_CHAT_CLIENT_CAPTION
		};
		assert((uint)this->dtype < lengthof(chat_captions));
		this->dest_string = chat_captions[this->dtype];

		this->InitNested(type);

		this->SetFocusedWidget(WID_NC_TEXTBOX);
		InvalidateWindowData(WC_NEWS_WINDOW, 0, this->height);
		_chat_tab_completion_active = false;

		PositionNetworkChatWindow(this);
	}

	~NetworkChatWindow()
	{
		InvalidateWindowData(WC_NEWS_WINDOW, 0, 0);
	}

	void FindWindowPlacementAndResize(int def_width, int def_height) override
	{
		Window::FindWindowPlacementAndResize(_toolbar_width, def_height);
	}

	/**
	 * Find the next item of the list of things that can be auto-completed.
	 * @param item The current indexed item to return. This function can, and most
	 *     likely will, alter item, to skip empty items in the arrays.
	 * @return Returns the char that matched to the index.
	 */
	const char *ChatTabCompletionNextItem(uint *item)
	{
		static char chat_tab_temp_buffer[64];

		/* First, try clients */
		if (*item < MAX_CLIENT_SLOTS) {
			/* Skip inactive clients */
			for (NetworkClientInfo *ci : NetworkClientInfo::Iterate(*item)) {
				*item = ci->index;
				return ci->client_name;
			}
			*item = MAX_CLIENT_SLOTS;
		}

		/* Then, try townnames
		 * Not that the following assumes all town indices are adjacent, ie no
		 * towns have been deleted. */
		if (*item < (uint)MAX_CLIENT_SLOTS + Town::GetPoolSize()) {
			for (const Town *t : Town::Iterate(*item - MAX_CLIENT_SLOTS)) {
				/* Get the town-name via the string-system */
				SetDParam(0, t->index);
				GetString(chat_tab_temp_buffer, STR_TOWN_NAME, lastof(chat_tab_temp_buffer));
				return &chat_tab_temp_buffer[0];
			}
		}

		return nullptr;
	}

	/**
	 * Find what text to complete. It scans for a space from the left and marks
	 *  the word right from that as to complete. It also writes a \0 at the
	 *  position of the space (if any). If nothing found, buf is returned.
	 */
	static char *ChatTabCompletionFindText(char *buf)
	{
		char *p = strrchr(buf, ' ');
		if (p == nullptr) return buf;

		*p = '\0';
		return p + 1;
	}

	/**
	 * See if we can auto-complete the current text of the user.
	 */
	void ChatTabCompletion()
	{
		static char _chat_tab_completion_buf[NETWORK_CHAT_LENGTH];
		assert(this->message_editbox.text.max_bytes == lengthof(_chat_tab_completion_buf));

		Textbuf *tb = &this->message_editbox.text;
		size_t len, tb_len;
		uint item;
		char *tb_buf, *pre_buf;
		const char *cur_name;
		bool second_scan = false;

		item = 0;

		/* Copy the buffer so we can modify it without damaging the real data */
		pre_buf = (_chat_tab_completion_active) ? stredup(_chat_tab_completion_buf) : stredup(tb->buf);

		tb_buf  = ChatTabCompletionFindText(pre_buf);
		tb_len  = strlen(tb_buf);

		while ((cur_name = ChatTabCompletionNextItem(&item)) != nullptr) {
			item++;

			if (_chat_tab_completion_active) {
				/* We are pressing TAB again on the same name, is there another name
				 *  that starts with this? */
				if (!second_scan) {
					size_t offset;
					size_t length;

					/* If we are completing at the begin of the line, skip the ': ' we added */
					if (tb_buf == pre_buf) {
						offset = 0;
						length = (tb->bytes - 1) - 2;
					} else {
						/* Else, find the place we are completing at */
						offset = strlen(pre_buf) + 1;
						length = (tb->bytes - 1) - offset;
					}

					/* Compare if we have a match */
					if (strlen(cur_name) == length && strncmp(cur_name, tb->buf + offset, length) == 0) second_scan = true;

					continue;
				}

				/* Now any match we make on _chat_tab_completion_buf after this, is perfect */
			}

			len = strlen(cur_name);
			if (tb_len < len && strncasecmp(cur_name, tb_buf, tb_len) == 0) {
				/* Save the data it was before completion */
				if (!second_scan) seprintf(_chat_tab_completion_buf, lastof(_chat_tab_completion_buf), "%s", tb->buf);
				_chat_tab_completion_active = true;

				/* Change to the found name. Add ': ' if we are at the start of the line (pretty) */
				if (pre_buf == tb_buf) {
					this->message_editbox.text.Print("%s: ", cur_name);
				} else {
					this->message_editbox.text.Print("%s %s", pre_buf, cur_name);
				}

				this->SetDirty();
				free(pre_buf);
				return;
			}
		}

		if (second_scan) {
			/* We walked all possibilities, and the user presses tab again.. revert to original text */
			this->message_editbox.text.Assign(_chat_tab_completion_buf);
			_chat_tab_completion_active = false;

			this->SetDirty();
		}
		free(pre_buf);
	}

	Point OnInitialPosition(int16 sm_width, int16 sm_height, int window_number) override
	{
		Point pt = { 0, _screen.height - sm_height - FindWindowById(WC_STATUS_BAR, 0)->height };
		return pt;
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		if (widget != WID_NC_DESTINATION) return;

		if (this->dtype == DESTTYPE_CLIENT) {
			SetDParamStr(0, NetworkClientInfo::GetByClientID((ClientID)this->dest)->client_name);
		}
		Dimension d = GetStringBoundingBox(this->dest_string);
		d.width  += WD_FRAMERECT_LEFT + WD_FRAMERECT_RIGHT;
		d.height += WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM;
		*size = maxdim(*size, d);
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		if (widget != WID_NC_DESTINATION) return;

		if (this->dtype == DESTTYPE_CLIENT) {
			SetDParamStr(0, NetworkClientInfo::GetByClientID((ClientID)this->dest)->client_name);
		}
		DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, r.top + WD_FRAMERECT_TOP, this->dest_string, TC_BLACK, SA_RIGHT);
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_NC_SENDBUTTON: /* Send */
				SendChat(this->message_editbox.text.buf, this->dtype, this->dest);
				FALLTHROUGH;

			case WID_NC_CLOSE: /* Cancel */
				delete this;
				break;
		}
	}

	EventState OnKeyPress(WChar key, uint16 keycode) override
	{
		EventState state = ES_NOT_HANDLED;
		if (keycode == WKC_TAB) {
			ChatTabCompletion();
			state = ES_HANDLED;
		}
		return state;
	}

	void OnEditboxChanged(int wid) override
	{
		_chat_tab_completion_active = false;
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (data == this->dest) delete this;
	}
};

/** The widgets of the chat window. */
static const NWidgetPart _nested_chat_window_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY, WID_NC_CLOSE),
		NWidget(WWT_PANEL, COLOUR_GREY, WID_NC_BACKGROUND),
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_TEXT, COLOUR_GREY, WID_NC_DESTINATION), SetMinimalSize(62, 12), SetPadding(1, 0, 1, 0), SetDataTip(STR_NULL, STR_NULL),
				NWidget(WWT_EDITBOX, COLOUR_GREY, WID_NC_TEXTBOX), SetMinimalSize(100, 12), SetPadding(1, 0, 1, 0), SetResize(1, 0),
																	SetDataTip(STR_NETWORK_CHAT_OSKTITLE, STR_NULL),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_NC_SENDBUTTON), SetMinimalSize(62, 12), SetPadding(1, 0, 1, 0), SetDataTip(STR_NETWORK_CHAT_SEND, STR_NULL),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

/** The description of the chat window. */
static WindowDesc _chat_window_desc(
	WDP_MANUAL, nullptr, 0, 0,
	WC_SEND_NETWORK_MSG, WC_NONE,
	0,
	_nested_chat_window_widgets, lengthof(_nested_chat_window_widgets)
);


/**
 * Show the chat window.
 * @param type The type of destination.
 * @param dest The actual destination index.
 */
void ShowNetworkChatQueryWindow(DestType type, int dest)
{
	DeleteWindowByClass(WC_SEND_NETWORK_MSG);
	new NetworkChatWindow(&_chat_window_desc, type, dest);
}



struct SocialJoinRequestWindow : public Window {
	void *join_request_cookie;
	std::string friend_name;
	bool handled;

	static std::map<WindowNumber, SocialJoinRequestWindow *> _join_request_windows;
	static WindowNumber _next_wnum;

	SocialJoinRequestWindow(WindowDesc *desc, void *join_request_cookie, const std::string &friend_name)
		: Window(desc), join_request_cookie(join_request_cookie), friend_name(friend_name)
	{
		this->InitNested(_next_wnum++);

		this->handled = false;
		_join_request_windows[this->window_number] = this;
	}

	~SocialJoinRequestWindow()
	{
		if (!this->handled) SocialRespondJoinRequest(this->join_request_cookie, SocialJoinRequestResponse::Ignore);

		_join_request_windows.erase(this->window_number);
	}

	static void CancelRequest(void *join_request_cookie)
	{
		for (auto &jrw : _join_request_windows) {
			if (jrw.second->join_request_cookie == join_request_cookie) {
				delete jrw.second;
				break;
			}
		}
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_SJR_MESSAGE:
			{
				int text_width = std::max(0, (int)size->width - WD_FRAMETEXT_LEFT - WD_FRAMETEXT_RIGHT);
				SetDParamStr(0, this->friend_name.c_str());
				size->height = WD_FRAMERECT_TOP + GetStringHeight(STR_SOCIAL_JOIN_REQUEST_TEXT, text_width) + WD_FRAMERECT_BOTTOM;
				break;
			}
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_SJR_MESSAGE:
				SetDParamStr(0, this->friend_name.c_str());
				DrawStringMultiLine(r.left + WD_FRAMETEXT_LEFT, r.right - WD_FRAMETEXT_RIGHT, r.top + WD_FRAMERECT_TOP, r.bottom - WD_FRAMERECT_BOTTOM, STR_SOCIAL_JOIN_REQUEST_TEXT, TC_FROMSTRING, SA_CENTER);
				break;
		}
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_SJR_ACCEPT: /* Send */
				SocialRespondJoinRequest(this->join_request_cookie, SocialJoinRequestResponse::Accept);
				this->handled = true;
				delete this;
				break;

			case WID_SJR_REJECT: /* Cancel */
				SocialRespondJoinRequest(this->join_request_cookie, SocialJoinRequestResponse::Reject);
				this->handled = true;
				delete this;
				break;
		}
	}
};
std::map<WindowNumber, SocialJoinRequestWindow *> SocialJoinRequestWindow::_join_request_windows{};
WindowNumber SocialJoinRequestWindow::_next_wnum = 0;


/** The widgets of the join request window. */
static const NWidgetPart _nested_join_request_window_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY), SetDataTip(STR_SOCIAL_JOIN_REQUEST_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY, WID_NC_BACKGROUND),
		NWidget(NWID_VERTICAL), SetPIP(8, 15, 8),
			NWidget(WWT_EMPTY, COLOUR_GREY, WID_SJR_MESSAGE), SetMinimalSize(236, 32),
			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(20, 29, 20),
				NWidget(WWT_PUSHTXTBTN, COLOUR_LIGHT_BLUE, WID_SJR_ACCEPT), SetDataTip(STR_SOCIAL_JOIN_REQUEST_ACCEPT, STR_SOCIAL_JOIN_REQUEST_ACCEPT_TOOLTIP), SetFill(2, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SJR_REJECT), SetDataTip(STR_SOCIAL_JOIN_REQUEST_REJECT, STR_SOCIAL_JOIN_REQUEST_REJECT_TOOLTIP), SetFill(2, 0),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

/** The description of the join request window. */
static WindowDesc _join_request_window_desc(
	WDP_CENTER, nullptr, 0, 0,
	WC_SOCIAL_JOIN_REQUEST, WC_NONE,
	0,
	_nested_join_request_window_widgets, lengthof(_nested_join_request_window_widgets)
);


void SocialHandleJoinRequest(void *join_request_cookie, const std::string &friend_name)
{
	new SocialJoinRequestWindow(&_join_request_window_desc, join_request_cookie, friend_name);
}

void SocialCancelJoinRequest(void *join_request_cookie)
{
	SocialJoinRequestWindow::CancelRequest(join_request_cookie);
}
