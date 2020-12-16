/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file network_widget.h Types related to the network widgets. */

#ifndef WIDGETS_NETWORK_WIDGET_H
#define WIDGETS_NETWORK_WIDGET_H

/** Widgets of the #NetworkGameWindow class. */
enum NetworkGameWidgets {
	WID_NG_MAIN,               ///< Main panel.

	WID_NG_CONNECTION,         ///< Label in front of connection droplist.
	WID_NG_CONN_BTN,           ///< 'Connection' droplist button.
	WID_NG_CLIENT_LABEL,       ///< Label in front of client name edit box.
	WID_NG_CLIENT,             ///< Panel with editbox to set client name.
	WID_NG_FILTER_LABEL,       ///< Label in front of the filter/search edit box.
	WID_NG_FILTER,             ///< Panel with the edit box to enter the search text.

	WID_NG_HEADER,             ///< Header container of the matrix.
	WID_NG_NAME,               ///< 'Name' button.
	WID_NG_CLIENTS,            ///< 'Clients' button.
	WID_NG_MAPSIZE,            ///< 'Map size' button.
	WID_NG_DATE,               ///< 'Date' button.
	WID_NG_YEARS,              ///< 'Years' button.
	WID_NG_INFO,               ///< Third button in the game list panel.

	WID_NG_MATRIX,             ///< Panel with list of games.
	WID_NG_SCROLLBAR,          ///< Scrollbar of matrix.

	WID_NG_LASTJOINED_LABEL,   ///< Label "Last joined server:".
	WID_NG_LASTJOINED,         ///< Info about the last joined server.
	WID_NG_LASTJOINED_SPACER,  ///< Spacer after last joined server panel.

	WID_NG_DETAILS,            ///< Panel with game details.
	WID_NG_DETAILS_SPACER,     ///< Spacer for game actual details.
	WID_NG_JOIN,               ///< 'Join game' button.
	WID_NG_REFRESH,            ///< 'Refresh server' button.
	WID_NG_NEWGRF,             ///< 'NewGRF Settings' button.
	WID_NG_NEWGRF_SEL,         ///< Selection 'widget' to hide the NewGRF settings.
	WID_NG_NEWGRF_MISSING,     ///< 'Find missing NewGRF online' button.
	WID_NG_NEWGRF_MISSING_SEL, ///< Selection widget for the above button.

	WID_NG_FIND,               ///< 'Find server' button.
	WID_NG_ADD,                ///< 'Add server' button.
	WID_NG_START,              ///< 'Start server' button.
	WID_NG_CANCEL,             ///< 'Cancel' button.
};

/** Widgets of the #NetworkStartServerWindow class. */
enum NetworkStartServerWidgets {
	WID_NSS_BACKGROUND,        ///< Background of the window.
	WID_NSS_GAMENAME_LABEL,    ///< Label for the game name.
	WID_NSS_GAMENAME,          ///< Background for editbox to set game name.
	WID_NSS_SETPWD,            ///< 'Set password' button.
	WID_NSS_CONNTYPE_LABEL,    ///< Label for 'connection type'.
	WID_NSS_CONNTYPE_BTN,      ///< 'Connection type' droplist button.
	WID_NSS_CLIENTS_LABEL,     ///< Label for 'max clients'.
	WID_NSS_CLIENTS_BTND,      ///< 'Max clients' downarrow.
	WID_NSS_CLIENTS_TXT,       ///< 'Max clients' text.
	WID_NSS_CLIENTS_BTNU,      ///< 'Max clients' uparrow.
	WID_NSS_COMPANIES_LABEL,   ///< Label for 'max companies'.
	WID_NSS_COMPANIES_BTND,    ///< 'Max companies' downarrow.
	WID_NSS_COMPANIES_TXT,     ///< 'Max companies' text.
	WID_NSS_COMPANIES_BTNU,    ///< 'Max companies' uparrow.
	WID_NSS_SPECTATORS_LABEL,  ///< Label for 'max spectators'.
	WID_NSS_SPECTATORS_BTND,   ///< 'Max spectators' downarrow.
	WID_NSS_SPECTATORS_TXT,    ///< 'Max spectators' text.
	WID_NSS_SPECTATORS_BTNU,   ///< 'Max spectators' uparrow.

	WID_NSS_LANGUAGE_LABEL,    ///< Label for 'language spoken'.
	WID_NSS_LANGUAGE_BTN,      ///< 'Language spoken' droplist button.

	WID_NSS_GENERATE_GAME,     ///< New game button.
	WID_NSS_LOAD_GAME,         ///< Load game button.
	WID_NSS_PLAY_SCENARIO,     ///< Play scenario button.
	WID_NSS_PLAY_HEIGHTMAP,    ///< Play heightmap button.

	WID_NSS_CANCEL,            ///< 'Cancel' button.
};

/** Widgets of the #NetworkLobbyWindow class. */
enum NetworkLobbyWidgets {
	WID_NL_BACKGROUND, ///< Background of the window.
	WID_NL_TEXT,       ///< Heading text.
	WID_NL_HEADER,     ///< Header above list of companies.
	WID_NL_MATRIX,     ///< List of companies.
	WID_NL_SCROLLBAR,  ///< Scroll bar.
	WID_NL_DETAILS,    ///< Company details.
	WID_NL_JOIN,       ///< 'Join company' button.
	WID_NL_NEW,        ///< 'New company' button.
	WID_NL_SPECTATE,   ///< 'Spectate game' button.
	WID_NL_REFRESH,    ///< 'Refresh server' button.
	WID_NL_CANCEL,     ///< 'Cancel' button.
};

/** Widgets of the #NetworkClientListWindow class. */
enum ClientListWidgets {
	WID_CL_PANEL, ///< Panel of the window.
};

/** Widgets of the #NetworkClientListPopupWindow class. */
enum ClientListPopupWidgets {
	WID_CLP_PANEL, ///< Panel of the window.
};

/** Widgets of the #NetworkJoinStatusWindow class. */
enum NetworkJoinStatusWidgets {
	WID_NJS_BACKGROUND, ///< Background of the window.
	WID_NJS_CANCELOK,   ///< Cancel / OK button.
};

/** Widgets of the #NetworkCompanyPasswordWindow class. */
enum NetworkCompanyPasswordWidgets {
	WID_NCP_BACKGROUND,               ///< Background of the window.
	WID_NCP_LABEL,                    ///< Label in front of the password field.
	WID_NCP_PASSWORD,                 ///< Input field for the password.
	WID_NCP_SAVE_AS_DEFAULT_PASSWORD, ///< Toggle 'button' for saving the current password as default password.
	WID_NCP_PROTECT_PUBKEY,           ///< Button for protecting company with pubkey.
	WID_NCP_UNPROTECT_PUBKEY,         ///< Button for unprotecting company with pubkey.
	WID_NCP_WARNING,                  ///< Warning text about password security
	WID_NCP_CANCEL,                   ///< Close the window without changing anything.
	WID_NCP_OK,                       ///< Safe the password etc.
};

#endif /* WIDGETS_NETWORK_WIDGET_H */
