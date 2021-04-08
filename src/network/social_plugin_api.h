/*
 * This file is part of OpenTTD.
 * Unlike the rest of OpenTTD, this file is covered by the MIT license,
 * to allow non-free implementations of the described API.
 *
 * Copyright 2021 OpenTTD project
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/** @file social_plugin_api.h Interface definitions for game to report/respond to social media presence. */

#ifndef OPENTTD_SOCIAL_PLUGIN_API_H
#define OPENTTD_SOCIAL_PLUGIN_API_H

#ifdef __cplusplus
extern "C" {
#endif

/* Various constants for the API */
enum {
	/** Version number of the API defined in this header, must be passed to the init functions */
	OTTD_SOCIAL_PLUGIN_API_VERSION = 3,
};

/** Response values for join requests */
enum OpenTTD_SocialPluginApi_JoinRequestResponse {
	/** Player chose not to respond to the request */
	OTTD_JRR_IGNORE = 0,
	/** Player accepts the request */
	OTTD_JRR_ACCEPT = 1,
	/** Player rejects the request */
	OTTD_JRR_REJECT = 2,
};

/** Function pointers supplied by the plug-in for OpenTTD to call */
struct OpenTTD_SocialPluginApi {
	/** OpenTTD calls this function when it prepares to exit */
	void (*shutdown)();
	/** OpenTTD calls this function at regular intervals, where it is safe to call the callback functions */
	void (*event_loop)();
	/** OpenTTD calls this function when the player enters a singleplayer game */
	void (*enter_singleplayer)();
	/** OpenTTD calls this function when the player enters a multiplayer game */
	void (*enter_multiplayer)(const char *server_name, const char *server_cookie);
	/** OpenTTD calls this function when the player changes controlled company, or the company changes name */
	void (*enter_company)(const char *company_name, int company_id);
	/** OpenTTD calls this function when the player joins the spectators */
	void (*enter_spectate)();
	/** OpenTTD calls this function when the player leaves the main gameplay */
	void (*exit_gameplay)();
	/** OpenTTD calls this function when the player responds to a received join request */
	void (*respond_join_request)(void *join_request_cookie, OpenTTD_SocialPluginApi_JoinRequestResponse response);
	/** OpenTTD calls this function to display a web browser via integration */
	void (*show_web_browser)(const char *uri);
	/** OpenTTD calls this function to get the user's preferred player name */
	void (*get_preferred_player_name)(char *name_buffer, unsigned int name_buffer_size);
};

/** Function pointers supplied by OpenTTD, for the plug-in to call */
struct OpenTTD_SocialPluginCallbacks {
	/** Inform OpenTTD that another user wants to join their current game */
	void (*handle_join_request)(void *join_request_cookie, const char *friend_name);
	/** Inform OpenTTD that a user retracted their previous join request */
	void (*cancel_join_request)(void *join_request_cookie);
	/** Inform OpenTTD that the local user requested to join another player's game and was accepted */
	void (*join_requested_game)(const char *server_cookie);
	/** String indicating the launch command for OpenTTD, the plugin must make a copy of this to its own memory */
	const char *launch_command;
};

/**
 * Type of the init function the plug-in is expected to export from its dynamic library.
 * On platforms where this method of initialisation is inconvenient, a different method can be used.
 * The plugin must verify the api_version passed by OpenTTD is supported before filling the api struct.
 * @note The launch_command field of callbacks must point to a valid C string for the duration of this call,
 *       but may be freed after the call returns.
 * @param[in]  api_version  The API version OpenTTD uses.
 * @param[out] api          Structure the plugin must fill with function pointers.
 * @param[in]  callbacks    Function pointers for the plug-in to call back into OpenTTD. These will stay valid until shutdown.
 * @return                  Non-zero on success, zero if the requested api_version is not supported.
 */
typedef int (*OpenTTD_SocialPluginInit)(int api_version, struct OpenTTD_SocialPluginApi *api, const struct OpenTTD_SocialPluginCallbacks *callbacks);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* OPENTTD_SOCIAL_PLUGIN_API_H */
