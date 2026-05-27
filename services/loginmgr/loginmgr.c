/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Console login manager service
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 *
 * This is a very basic and minimal implementation of the new login manager. It follows EP-73c. So far, this
 * functionality is implemented:
 * - dummy and password authenticators, although in an unoptimal way
 * - console idle/wait for user keypress
 * - automatic timeout after a configurable time
 * - console passthru from the downstream service
 * - automatic creation of the downstream service after the auth succeeds
 * - logout using ctrl+d, which destroys the downstream service instance
 *
 * Now, there are a few things to do:
 * @todo
 * - redo authenticators. They should be separate services consuming a Stream interface
 *   and providing another Stream interface. it should be able to chain them, eventually
 *   with the Stream interface of the service they are authenticating.
 * - add Stream interface factory.. interface (interfaces/stream-factory.h) to create new
 *   services with Stream interfaces in a modular way.
 * - redo channel status. There is only a one global status, which is wrong. It should be
 *   a per-channel status and channels should be switchable.
 * - change shortcuts. CTRL+TAB nor SHIFT+CTRL+TAB are not available.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <main.h>
#include <interfaces/stream.h>
#include <interfaces/pm.h>

#include "loginmgr.h"

#define MODULE_NAME "loginmgr"

/* Maximum wait for the remainder of an escape sequence before discarding it. */
#define LOGINMGR_ESC_TIMEOUT_MS 100u


/*********************************************************************************************************************
 * Channel Stream interface implementation
 *********************************************************************************************************************/

static stream_ret_t channel_write(Stream *self, const void *buf, size_t size) {
	LoginMgrChannel *ch = (LoginMgrChannel *)self->parent;
	if (ch->authenticated) {
		return ch->parent->host->vmt->write(ch->parent->host, buf, size);
	}
	return STREAM_RET_EOF;
}


static stream_ret_t channel_read(Stream *self, void *buf, size_t size, size_t *read) {
	/* Read-side not implemented yet (log-passthrough channels have no consumer reading). */
	(void)self;
	(void)buf;
	(void)size;
	(void)read;
	return STREAM_RET_EOF;
}


static stream_ret_t channel_write_timeout(Stream *self, const void *buf, size_t size, size_t *written, uint32_t timeout_ms) {
	LoginMgrChannel *ch = (LoginMgrChannel *)self->parent;
	if (ch->authenticated) {
		return ch->parent->host->vmt->write_timeout(ch->parent->host, buf, size, written, timeout_ms);
	}
	return STREAM_RET_EOF;
}


static stream_ret_t channel_read_timeout(Stream *self, void *buf, size_t size, size_t *read, uint32_t timeout_ms) {
	(void)self;
	(void)buf;
	(void)size;
	(void)read;
	(void)timeout_ms;
	return STREAM_RET_EOF;
}


static const struct stream_vmt channel_stream_vmt = {
	.write = channel_write,
	.read = channel_read,
	.write_timeout = channel_write_timeout,
	.read_timeout = channel_read_timeout,
};


/*********************************************************************************************************************
 * Built-in no-auth authenticator
 *********************************************************************************************************************/

loginmgr_ret_t loginmgr_auth_noauth(void *ctx, Stream *console) {
	(void)ctx;
	(void)console;
	return LOGINMGR_RET_OK;
}


/*********************************************************************************************************************
 * Internal channel management
 *********************************************************************************************************************/

/**
 * Destroy channel at @p index: call destroy_downstream if set, reset state, then compact the array.
 * The destroy callback is invoked before the state is cleared so it still sees a valid Stream.
 */
static loginmgr_ret_t loginmgr_destroy_channel(LoginMgr *self, uint8_t index) {
	if (index >= self->channel_count) {
		return LOGINMGR_RET_FAILED;
	}
	LoginMgrChannel *ch = &self->channels[index];
	const struct loginmgr_channel_conf *chconf = &self->config.channel_confs[ch->conf_index];
	if (chconf->destroy_downstream != NULL) {
		chconf->destroy_downstream(chconf->downstream_ctx);
	}
	ch->authenticated = false;
	/* Compact the array so there are no holes, fixing up iface.parent after each shift. */
	for (uint8_t i = index; i < self->channel_count - 1u; i++) {
		self->channels[i] = self->channels[i + 1];
		self->channels[i].iface.parent = &self->channels[i];
	}
	memset(&self->channels[self->channel_count - 1], 0, sizeof(LoginMgrChannel));
	self->channel_count--;
	return LOGINMGR_RET_OK;
}


static loginmgr_ret_t loginmgr_create_channel(LoginMgr *self, uint8_t conf_index) {
	if (conf_index >= self->config.channel_conf_count) {
		return LOGINMGR_RET_FAILED;
	}
	if (self->channel_count >= LOGINMGR_MAX_CHANNELS) {
		return LOGINMGR_RET_FAILED;
	}
	LoginMgrChannel *ch = &self->channels[self->channel_count];
	ch->conf_index = conf_index;
	ch->authenticated = false;
	ch->parent = self;
	ch->iface.parent = ch;
	ch->iface.vmt = &channel_stream_vmt;
	self->channel_count++;
	return LOGINMGR_RET_OK;
}


/*********************************************************************************************************************
 * Task implementation
 *********************************************************************************************************************/

static void loginmgr_host_write(LoginMgr *self, const char *s) {
	self->host->vmt->write(self->host, s, strlen(s));
}


/* Wait for the user to press anything before continuing. Drain any bursts so we don't react to cat-typing. */
static void loginmgr_idle_wait(LoginMgr *self) {
	uint8_t b;
	size_t n;
	self->host->vmt->read(self->host, &b, 1, &n);
	if (self->config.quiet_timer_ms > 0) {
		while (self->host->vmt->read_timeout(self->host, &b, 1, &n, self->config.quiet_timer_ms) != STREAM_RET_TIMEOUT) {
		}
	}
}


static void loginmgr_print_issue(LoginMgr *self) {
	if (self->config.issue != NULL) {
		loginmgr_host_write(self, self->config.issue);
	}
}


static void loginmgr_print_tabbar(LoginMgr *self) {
	loginmgr_host_write(self, "\r\n\r\x1b[1;37;45m plumCore \x1b[0m console manager -- channels:");
	for (uint8_t i = 0; i < self->channel_count; i++) {
		const struct loginmgr_channel_conf *chconf = &self->config.channel_confs[self->channels[i].conf_index];
		const char *name = (chconf->name != NULL) ? chconf->name : "?";
		char buf[48];
		if (i == self->active_channel) {
			snprintf(buf, sizeof(buf), " \x1b[1m[%u: %s]\x1b[0m", i, name);
		} else {
			snprintf(buf, sizeof(buf), " \x1b[0m[%u: %s]\x1b[0m", i, name);
		}
		loginmgr_host_write(self, buf);
	}
	loginmgr_host_write(self, "\x1b[K\x1b[0m\r\n\r\n");
}


static void set_state(LoginMgr *self, enum loginmgr_state state) {
	self->state = state;
}


static void loginmgr_switch_channel(LoginMgr *self, uint8_t index) {
	if (index < self->channel_count) {
		self->active_channel = index;
		set_state(self, LOGINMGR_TABBAR);
	}
}


static void loginmgr_switch_channel_next(LoginMgr *self) {
	if (self->channel_count > 0) {
		loginmgr_switch_channel(self, (self->active_channel + 1u) % self->channel_count);
	}
}


static void loginmgr_switch_channel_prev(LoginMgr *self) {
	if (self->channel_count > 0) {
		loginmgr_switch_channel(self, (self->active_channel + self->channel_count - 1u) % self->channel_count);
	}
}


static void loginmgr_task(void *p) {
	LoginMgr *self = p;


	while (true) {
		switch (self->state) {
			case LOGINMGR_IDLE:
				if (self->config.wakelock_group != NULL) {
					wakelock_release(self->config.wakelock_group, &self->wakelock);
				}
				vTaskDelay(500);
				if (self->config.idle_banner != NULL) {
					loginmgr_host_write(self, self->config.idle_banner);
				} else {
					loginmgr_host_write(self, "\r\n\r\nPress any key to activate this console\r\n\r\n");
				}
				loginmgr_idle_wait(self);
				if (self->config.wakelock_group != NULL) {
					wakelock_acquire(self->config.wakelock_group, &self->wakelock);
				}

				/* Print help here. */
				loginmgr_host_write(self, LOGINMGR_HELP);

				set_state(self, LOGINMGR_TABBAR);
				break;

			case LOGINMGR_TABBAR:
				/* Auto-create the initial channel (type 0) if there is no channel. */
				if (self->config.channel_conf_count > 0 && self->channel_count == 0) {
					loginmgr_create_channel(self, 0);
					loginmgr_print_issue(self);
				}

				loginmgr_print_tabbar(self);
				set_state(self, LOGINMGR_CHECK_AUTH);
				break;

			case LOGINMGR_CHECK_AUTH:
				if (self->channels[self->active_channel].authenticated) {
					set_state(self, LOGINMGR_CONNECTED);
				} else {
					set_state(self, LOGINMGR_AUTHENTICATE);
				}
				break;

			case LOGINMGR_AUTHENTICATE: {
				const struct loginmgr_channel_conf *chconf = &self->config.channel_confs[self->channels[self->active_channel].conf_index];
				loginmgr_ret_t r = chconf->create_auth(chconf->auth_ctx, self->host);
				if (r == LOGINMGR_RET_OK) {
					chconf->create_downstream(chconf->downstream_ctx, &self->channels[self->active_channel].iface);
					self->channels[self->active_channel].authenticated = true;
					set_state(self, LOGINMGR_CONNECTED);
				} else if (r == LOGINMGR_RET_END) {
					set_state(self, LOGINMGR_DESTROY);
				} else if (r == LOGINMGR_RET_FAILED) {
					self->channels[self->active_channel].authenticated = false;
					set_state(self, LOGINMGR_CHECK_AUTH);
				} else {
					set_state(self, LOGINMGR_IDLE);
				}
				break;
			}

			case LOGINMGR_DESTROY:
				loginmgr_destroy_channel(self, self->active_channel);
				set_state(self, LOGINMGR_IDLE);
				break;

			case LOGINMGR_CONNECTED: {
				enum loginmgr_escape_state escape = LOGINMGR_ESC_NONE;

				uint8_t b;
				size_t n = 0;
				stream_ret_t r;

				while (true) {
					/* Use a short timeout while mid-sequence so a partial escape doesn't stall. */
					uint32_t timeout = (escape != LOGINMGR_ESC_NONE) ? LOGINMGR_ESC_TIMEOUT_MS
											  : self->config.inactivity_timeout_ms;

					if (timeout > 0) {
						r = self->host->vmt->read_timeout(self->host, &b, 1, &n, timeout);
					} else {
						r = self->host->vmt->read(self->host, &b, 1, &n);
					}

					if (r == STREAM_RET_TIMEOUT) {
						if (escape != LOGINMGR_ESC_NONE) {
							escape = LOGINMGR_ESC_NONE; /* Discard partial escape sequence. */
						} else {
							set_state(self, LOGINMGR_DESTROY);
							break;
						}
					} else if (r == STREAM_RET_OK && n > 0) {
						if (escape == LOGINMGR_ESC_NONE) {
							if (b == 0x04) {
								self->channels[self->active_channel].authenticated = false;
								set_state(self, LOGINMGR_TABBAR);
								break;
							} else if (b == 0x1b) {
								escape = LOGINMGR_ESC_ESC;
							}
						} else if (escape == LOGINMGR_ESC_ESC) {
							if (b == '[') {
								escape = LOGINMGR_ESC_CSI;
							} else if (b == ']') {
								escape = LOGINMGR_ESC_OSC;
							} else {
								escape = LOGINMGR_ESC_NONE;
							}
						} else if (escape == LOGINMGR_ESC_CSI) {
							escape = LOGINMGR_ESC_NONE;
							switch (b) {
								case 'I':
									loginmgr_switch_channel_next(self); /* Ctrl+Tab */
									break;
								case 'Z':
									loginmgr_switch_channel_prev(self); /* Ctrl+Shift+Tab */
									break;
								default:
									break;
							}
						} else if (escape == LOGINMGR_ESC_OSC) {
							escape = LOGINMGR_ESC_NONE;
						}
					}
				}
				break;
			}

			default:
				set_state(self, LOGINMGR_IDLE);
		}
	}
}


/*********************************************************************************************************************
 * Public API
 *********************************************************************************************************************/

loginmgr_ret_t loginmgr_init(LoginMgr *self, Stream *host, const struct loginmgr_config *config) {
	if (self == NULL || host == NULL || config == NULL) {
		return LOGINMGR_RET_NULL;
	}
	memset(self, 0, sizeof(LoginMgr));
	self->host = host;
	memcpy(&self->config, config, sizeof(struct loginmgr_config));
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("init ok"));
	return LOGINMGR_RET_OK;
}


loginmgr_ret_t loginmgr_free(LoginMgr *self) {
	(void)self;
	return LOGINMGR_RET_OK;
}


loginmgr_ret_t loginmgr_start(LoginMgr *self) {
	if (self == NULL) {
		return LOGINMGR_RET_NULL;
	}
	xTaskCreate(loginmgr_task, "loginmgr", configMINIMAL_STACK_SIZE + 384, self, 1, &self->task);
	return LOGINMGR_RET_OK;
}


loginmgr_ret_t loginmgr_stop(LoginMgr *self) {
	(void)self;
	return LOGINMGR_RET_FAILED;
}
