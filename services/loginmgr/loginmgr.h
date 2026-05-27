/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Console login manager service
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <interfaces/stream.h>
#include <interfaces/pm.h>


typedef enum {
	LOGINMGR_RET_OK = 0,
	LOGINMGR_RET_FAILED,
	LOGINMGR_RET_NULL,
	LOGINMGR_RET_END,
	LOGINMGR_RET_TIMEOUT,
} loginmgr_ret_t;


#define LOGINMGR_HELP "Console manager is a simple console multiplexer allowing user to access multiple channels. \r\n" \
	"Shortcuts:\r\n" \
	"\tCTRL+N or CTRL+T: create new channel, run the authenticator (if required)\r\n" \
	"\tCTRL+X: switch channel protocol\r\n" \
	"\tCTRL+D: close/logout the current channel or authenticator\r\n"

/**
 * Describes a channel type loginmgr can instantiate on demand (e.g. on first keypress or Ctrl+N).
 *
 * When a new channel of this type is created:
 *   1. create_auth is called to authenticate the session on the console stream.
 *   2. On success, create_downstream is called to connect the channel's Stream to its consumer
 *      (log sink, CLI handler, API handler, …).
 *
 * Not typedef'd per project policy.
 */
struct loginmgr_channel_conf {
	const char *name;
	/** Authenticate the session. Returns OK if accepted, FAILED otherwise. */
	loginmgr_ret_t (*create_auth)(void *ctx, Stream *console);
	void *auth_ctx;
	/** Connect the channel Stream to its downstream consumer after successful auth. */
	loginmgr_ret_t (*create_downstream)(void *ctx, Stream *stream);
	/** Release the downstream consumer when the channel is destroyed. May be NULL. */
	loginmgr_ret_t (*destroy_downstream)(void *ctx);
	void *downstream_ctx;
};


#define LOGINMGR_MAX_CHANNEL_CONFIGS 4

/* Service-level configuration. Not typedef'd per project policy. */
struct loginmgr_config {
	/** Wakelock group to acquire on console activation. NULL to disable. */
	WakeLockGroup *wakelock_group;
	/** Inactivity timeout in ms. 0 = never go idle due to inactivity. */
	uint32_t inactivity_timeout_ms;
	/** Anti-cat-typing quiet period: drain input for this many ms before activating. */
	uint32_t quiet_timer_ms;
	/** Text printed to the host stream each time a new channel is created, before auth runs. NULL to skip. */
	const char *issue;

	const char *idle_banner;
	/** Array of channel types loginmgr may instantiate. */
	const struct loginmgr_channel_conf channel_confs[LOGINMGR_MAX_CHANNEL_CONFIGS];
	uint8_t channel_conf_count;
};


#define LOGINMGR_MAX_CHANNELS 4

typedef struct {
	/* Index into parent->config.channel_confs[]. Changed to switch protocol without recreating. */
	uint8_t conf_index;
	struct loginmgr *parent;
	Stream iface;
	bool authenticated;
} LoginMgrChannel;

/* Escape sequence parser states — mirrors the state machine in lineedit. */
enum loginmgr_escape_state {
	LOGINMGR_ESC_NONE,
	LOGINMGR_ESC_ESC,
	LOGINMGR_ESC_CSI,
	LOGINMGR_ESC_OSC,
};

enum loginmgr_state {
	LOGINMGR_IDLE = 0,
	LOGINMGR_TABBAR,
	LOGINMGR_CHECK_AUTH,
	LOGINMGR_AUTHENTICATE,
	LOGINMGR_CONNECTED,
	LOGINMGR_DESTROY,

};

typedef struct loginmgr {
	Stream *host;
	struct loginmgr_config config;
	WakeLock wakelock;

	LoginMgrChannel channels[LOGINMGR_MAX_CHANNELS];
	uint8_t channel_count;
	uint8_t active_channel;

	/* Main FSM */
	TaskHandle_t task;
	enum loginmgr_state state;

} LoginMgr;


loginmgr_ret_t loginmgr_init(LoginMgr *self, Stream *host, const struct loginmgr_config *config);
loginmgr_ret_t loginmgr_free(LoginMgr *self);
loginmgr_ret_t loginmgr_start(LoginMgr *self);
loginmgr_ret_t loginmgr_stop(LoginMgr *self);

/**
 * Built-in no-op authenticator: accepts immediately without any user interaction.
 * Signature matches the create_auth callback type; pass as .create_auth = loginmgr_auth_noauth.
 */
loginmgr_ret_t loginmgr_auth_noauth(void *ctx, Stream *console);
