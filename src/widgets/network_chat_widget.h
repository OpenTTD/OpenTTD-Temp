/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file network_chat_widget.h Types related to the network chat widgets. */

#ifndef WIDGETS_NETWORK_CHAT_WIDGET_H
#define WIDGETS_NETWORK_CHAT_WIDGET_H

/** Widgets of the #NetworkChatWindow class. */
enum NetWorkChatWidgets {
	WID_NC_CLOSE,       ///< Close button.
	WID_NC_BACKGROUND,  ///< Background of the window.
	WID_NC_DESTINATION, ///< Destination.
	WID_NC_TEXTBOX,     ///< Textbox.
	WID_NC_SENDBUTTON,  ///< Send button.
};

/** Widgets of the #NetworkJoinStatusWindow class. */
enum SocialJoinRequestWidgets {
	WID_SJR_MESSAGE,     ///< Message area.
	WID_SJR_ACCEPT,      ///< Accept button.
	WID_SJR_REJECT,      ///< Reject button.
};

#endif /* WIDGETS_NETWORK_CHAT_WIDGET_H */
