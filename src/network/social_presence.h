/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

 /** @file social_presence.h Interface definitions for game to report/respond to social media presence. */

#ifndef NETWORK_SOCIAL_PRESENCE_H
#define NETWORK_SOCIAL_PRESENCE_H

#include "../company_type.h"
#include <string>

enum class SocialJoinRequestResponse {
	Accept,
	Reject,
	Ignore,
};


/* Functions implemented by social plug-in */

/** Main loop calls this to detect and initialise social plug-in */
void SocialStartup();
/** Main loop calls this to shut down social plug-in */
void SocialShutdown();
/** Main loop calls this, let social plug-in handle events */
void SocialEventLoop();
/** Game calls this when player starts/loads a singleplayer game */
void SocialEnterSingleplayer();
/** GUI calls this when player joins/starts a multiplayer */
void SocialBeginEnterMultiplayer(const std::string &server_name, const std::string &server_cookie);
/** Network code calls this when joining/starting a multiplayer game completes */
void SocialCompleteEnterMultiplayer();
/** Game calls this when player joins a company, or player's company changes name */
void SocialEnterCompany(const std::string &company_name, CompanyID company_id);
/** Game calls this when player enters spectate-mode */
void SocialEnterSpectate();
/** Game calls this when player leaves main gameplay mode */
void SocialExitGameplay();
/** Game calls this when player accepts a remote join request */
void SocialRespondJoinRequest(void *join_request_cookie, SocialJoinRequestResponse response);
/** Game calls this to open the social integration's preferred web browser */
void SocialOpenBrowser(const char *url);


/* Functions called from social plug-in on certain events */

/** Social plug-in calls this (from inside SocialEventLoop) if it receives a join request from a friend */
void SocialHandleJoinRequest(void *join_request_cookie, const std::string &friend_name);
/** Social plug-in calls this if a friend retracts a join request */
void SocialCancelJoinRequest(void *join_request_cookie);
/** Social plug-in calls this if the user received an accept on a join request */
void SocialJoinRequestedGame(const std::string &server_cookie);

#endif /* NETWORK_SOCIAL_PRESENCE_H */
