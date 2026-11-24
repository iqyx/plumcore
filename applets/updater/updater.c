/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Firmware updater applet
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 *
 * Fetches the directory listing of the configured URL with its own HTTP client running over the Tcp or Tls
 * interface given by the port (updater_conf.net). The query of the configured URL is replaced by "?list&latest", the
 * server then returns the latest file of the directory as JSON:
 *
 *     {"entries":[{"mime":"...","mtime":"...","name":"...","size":61380,"type":"file","url":"..."}],"path":"..."}
 *
 * The parameters of the latest firmware are parsed from it and its name is reported. With the GUI, a notification
 * with an indeterminate progress bar is shown while fetching and a failure is shown in a notification offering to
 * retry. The applet runs without a GUI, the result is logged. If the GUI is enabled
 * (APPLET_UPDATER_GUI) and the applet is given a framebuffer, the result is also shown until ESC is pressed.
 *
 * The configuration tree is advertised as "applet updater", mounted at /applet/updater:
 *
 *     /applet/updater/url         URL the updates are fetched from (http or https)
 *     /applet/updater/username    user name of the HTTP Basic authentication, empty = none
 *     /applet/updater/password    password of the HTTP Basic authentication
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include <main.h>
#include <interfaces/applet.h>
#include <interfaces/conf.h>
#include <interfaces/http.h>
#include <configlib.h>
#include <json.h>
#include <services/proto-http/proto-http.h>

#if defined(CONFIG_APPLET_UPDATER_GUI)
#include <interfaces/event.h>
#include <interfaces/fb.h>
#include <interfaces/painter.h>
#include <services/fb-painter/fb-painter.h>
#endif

#include "updater.h"

#define MODULE_NAME "updater"

#define UPDATER_CONNECT_TIMEOUT_MS 10000
#define UPDATER_RESPONSE_TIMEOUT_MS 15000
#define UPDATER_READ_TIMEOUT_MS 10000
#define UPDATER_LIST_QUERY "?list&latest"
#define UPDATER_LIST_LEN 1024
#define UPDATER_TEXT_LEN 48
#define UPDATER_NAME_LEN 96

#if defined(CONFIG_APPLET_UPDATER_GUI)
#define UPDATER_LINE_H 12
#endif


struct updater_conf updater_conf;

struct updater {
	struct applet_args *args;

	/* The last reported error. */
	char error[UPDATER_TEXT_LEN];

	/* Parameters of the latest firmware on the server, the URL is relative to the server. */
	struct {
		char name[UPDATER_NAME_LEN];
		char url[UPDATER_URL_LEN];
		int64_t size;
	} latest;

#if defined(CONFIG_APPLET_UPDATER_GUI)
	/* The applet was given a framebuffer and an event source, the status is shown on the screen. */
	bool gui;
	FbPainter painter;
	uint16_t w;
	uint16_t h;
	/* Number of status lines shown so far. */
	uint16_t lines;
#endif
};


/* Report a status line, it is logged and shown on the screen if the GUI is active. */
static void updater_report(struct updater *self, uint8_t type, const char *text) {
	u_log((self->args->logger != NULL) ? self->args->logger : system_log, type, U_LOG_MODULE_PREFIX("%s"), text);
	if (type == LOG_TYPE_ERROR) {
		snprintf(self->error, sizeof(self->error), "%s", text);
	}

#if defined(CONFIG_APPLET_UPDATER_GUI)
	if (!self->gui) {
		return;
	}
	Painter *painter = &self->painter.painter;
	painter->vmt->begin(painter);
	painter->vmt->set_pen(painter, 0xffffffff, 1);
	painter->vmt->set_font(painter, PAINTER_FONT_NORMAL, NULL);

	/* Wrap a text wider than the window, each line is shortened until it fits. */
	size_t len = strlen(text);
	while (len > 0) {
		char line[UPDATER_NAME_LEN];
		size_t n = (len < sizeof(line) - 1) ? len : sizeof(line) - 1;
		memcpy(line, text, n);
		line[n] = '\0';
		uint16_t tw = 0;
		uint16_t th = 0;
		while (n > 1 && painter->vmt->text_size(painter, line, &tw, &th) == PAINTER_RET_OK && tw > self->w - 4) {
			line[--n] = '\0';
		}
		painter->vmt->text(painter, 2, (int16_t)(2 + self->lines * UPDATER_LINE_H), line);
		self->lines++;
		text += n;
		len -= n;
	}
	painter->vmt->end(painter);
#endif
}


/* Parse the parameters of the latest firmware from the directory listing in @p list. */
static applet_ret_t updater_parse_latest(struct updater *self, const char *list, size_t len) {
	struct json_value root;
	struct json_value entries;
	struct json_value entry;
	struct json_value value;
	if (json_init(&root, list, len) != JSON_RET_OK ||
	    json_map_find_value(&root, "entries", &entries) != JSON_RET_OK ||
	    json_enter_container(&entries, &entry) != JSON_RET_OK ||
	    json_get_type(&entry) != JSON_TYPE_OBJECT) {
		return APPLET_RET_FAILED;
	}
	if (json_map_find_value(&entry, "name", &value) != JSON_RET_OK ||
	    json_copy_string(&value, self->latest.name, sizeof(self->latest.name), NULL) != JSON_RET_OK ||
	    json_map_find_value(&entry, "url", &value) != JSON_RET_OK ||
	    json_copy_string(&value, self->latest.url, sizeof(self->latest.url), NULL) != JSON_RET_OK ||
	    json_map_find_value(&entry, "size", &value) != JSON_RET_OK ||
	    json_get_int(&value, &self->latest.size) != JSON_RET_OK) {
		return APPLET_RET_FAILED;
	}
	return APPLET_RET_OK;
}


/* Fetch the directory listing of the configured URL with @p http and find the latest firmware in it. */
static applet_ret_t updater_fetch(struct updater *self, Http *http) {
	char text[UPDATER_TEXT_LEN];

	if (updater_conf.settings.url[0] == '\0') {
		updater_report(self, LOG_TYPE_ERROR, "no URL configured");
		return APPLET_RET_FAILED;
	}

	/* Replace any query (and fragment) of the configured URL with the listing query. */
	char target[UPDATER_URL_LEN + sizeof(UPDATER_LIST_QUERY)];
	snprintf(target, sizeof(target), "%.*s" UPDATER_LIST_QUERY, (int)strcspn(updater_conf.settings.url, "?#"),
	         updater_conf.settings.url);
	u_log((self->args->logger != NULL) ? self->args->logger : system_log, LOG_TYPE_INFO,
	      U_LOG_MODULE_PREFIX("fetching %s"), target);
	updater_report(self, LOG_TYPE_INFO, "fetching...");

	const struct http_request request = {
		.method = HTTP_METHOD_GET,
		.target = target,
		.has_content_length = true,
		.content_length = 0,
	};
	void *session = NULL;
	http_ret_t ret = http->vmt->open(http, &request, &session, UPDATER_CONNECT_TIMEOUT_MS);
	if (ret != HTTP_RET_OK) {
		snprintf(text, sizeof(text), "cannot connect (%d)", (int)ret);
		updater_report(self, LOG_TYPE_ERROR, text);
		return APPLET_RET_FAILED;
	}

	struct http_response response = {0};
	ret = http->vmt->get_response(http, session, &response, UPDATER_RESPONSE_TIMEOUT_MS);
	if (ret != HTTP_RET_OK) {
		http->vmt->close(http, session);
		snprintf(text, sizeof(text), "no response (%d)", (int)ret);
		updater_report(self, LOG_TYPE_ERROR, text);
		return APPLET_RET_FAILED;
	}
	if (response.status != 200) {
		http->vmt->close(http, session);
		snprintf(text, sizeof(text), "status %u", (unsigned)response.status);
		updater_report(self, LOG_TYPE_ERROR, text);
		return APPLET_RET_FAILED;
	}

	/* The listing is read whole into the buffer. */
	char list[UPDATER_LIST_LEN];
	size_t size = 0;
	size_t len = 0;
	while (size < sizeof(list) &&
	       (ret = http->vmt->read(http, session, list + size, sizeof(list) - size, &len, UPDATER_READ_TIMEOUT_MS)) ==
	       HTTP_RET_OK) {
		size += len;
	}
	http->vmt->close(http, session);
	if (size == sizeof(list)) {
		updater_report(self, LOG_TYPE_ERROR, "listing too large");
		return APPLET_RET_FAILED;
	}
	if (ret != HTTP_RET_EOF) {
		snprintf(text, sizeof(text), "read failed (%d)", (int)ret);
		updater_report(self, LOG_TYPE_ERROR, text);
		return APPLET_RET_FAILED;
	}

	if (updater_parse_latest(self, list, size) != APPLET_RET_OK) {
		updater_report(self, LOG_TYPE_ERROR, "no firmware found");
		return APPLET_RET_FAILED;
	}
	char latest[UPDATER_NAME_LEN + 8];
	snprintf(latest, sizeof(latest), "latest: %s", self->latest.name);
	updater_report(self, LOG_TYPE_INFO, latest);
	return APPLET_RET_OK;
}


#if defined(CONFIG_APPLET_UPDATER_GUI)
/* Set up the GUI if the applet was given a framebuffer and an event source, it runs without one otherwise. */
static void updater_gui_init(struct updater *self) {
	struct applet_args *args = self->args;
	struct fb_stat stat = {0};
	if (args->fb == NULL || args->event == NULL || args->fb->vmt->stat(args->fb, &stat) != FB_RET_OK ||
	    fb_painter_init(&self->painter, args->fb) != FB_PAINTER_RET_OK) {
		return;
	}
	self->gui = true;
	self->w = (uint16_t)stat.w;
	self->h = (uint16_t)stat.h;
	if (args->window != NULL) {
		args->window->vmt->set_title(args->window, "Updater");
	}
}


/* Clear the window, the status lines start from the top again. */
static void updater_gui_clear(struct updater *self) {
	if (!self->gui) {
		return;
	}
	Painter *painter = &self->painter.painter;
	painter->vmt->begin(painter);
	painter->vmt->set_pen(painter, 0xff000000, 1);
	painter->vmt->set_brush(painter, 0xff000000);
	painter->vmt->rect(painter, 0, 0, self->w, self->h);
	painter->vmt->end(painter);
	self->lines = 0;
}


/* Keep the result shown until ESC is pressed, then release the GUI. */
static void updater_gui_free(struct updater *self) {
	if (!self->gui) {
		return;
	}
	updater_report(self, LOG_TYPE_INFO, "press ESC to exit");
	while (true) {
		enum event_type type = EV_TYPE_NONE;
		enum event_code code = EV_CODE_NONE;
		int32_t value = 0;
		if (self->args->event->vmt->listen(self->args->event, &type, &code, &value) == EV_RET_OK &&
		    type == EV_TYPE_KEY && code == EV_KEY_ESC && value == 1) {
			break;
		}
	}
	fb_painter_free(&self->painter);
}
#endif


/* Fetch the listing. With the GUI, a progress notification is shown meanwhile and a failure is shown in a notification
 * offering to retry. */
static applet_ret_t updater_check(struct updater *self, Http *http) {
	while (true) {
#if defined(CONFIG_APPLET_UPDATER_GUI)
		bool notify = self->gui && updater_conf.gui != NULL;
		struct notify_window *progress = NULL;
		if (notify) {
			updater_gui_clear(self);
			gui_wm_fs_notify(updater_conf.gui, NOTIFY_WINDOW_ICON_INFO, "Checking for updates", NULL, 0, 0, NULL,
			                 &progress);
		}
#endif

		applet_ret_t ret = updater_fetch(self, http);

#if defined(CONFIG_APPLET_UPDATER_GUI)
		if (progress != NULL) {
			notify_window_close(progress);
		}
		if (ret != APPLET_RET_OK && notify) {
			/* The default action 0 (also returned while the device is off) must not retry. */
			static const struct notify_window_action actions[] = {
				{.key = EV_KEY_ESC, .label = "ESC: Dismiss"},
				{.key = EV_KEY_ENTER, .label = "OK: Retry"},
			};
			char message[UPDATER_TEXT_LEN + 24];
			snprintf(message, sizeof(message), "Update check failed: %s", self->error);
			/* UINT32_MAX ms is the maximal delay, the notification waits for the user. */
			size_t index = 0;
			gui_wm_fs_notify(updater_conf.gui, NOTIFY_WINDOW_ICON_ERROR, message, actions,
			                 sizeof(actions) / sizeof(actions[0]), UINT32_MAX, &index, NULL);
			if (index == 1) {
				continue;
			}
		}
#endif
		return ret;
	}
}


static applet_ret_t updater_main(Applet *self, struct applet_args *args) {
	(void)self;
	struct updater u;
	memset(&u, 0, sizeof(u));
	u.args = args;

#if defined(CONFIG_APPLET_UPDATER_GUI)
	updater_gui_init(&u);
	updater_gui_clear(&u);
#endif

	/* The HTTP client lives only while the applet runs. */
	applet_ret_t ret = APPLET_RET_FAILED;
	ProtoHttp http;
	if (proto_http_init(&http, &(struct proto_http_conf) {
		.tcp = updater_conf.net.tcp,
		.tls = updater_conf.net.tls,
		.username = (updater_conf.settings.username[0] != '\0') ? updater_conf.settings.username : NULL,
		.password = updater_conf.settings.password,
	}) == PROTO_HTTP_RET_OK && proto_http_start(&http) == PROTO_HTTP_RET_OK) {
		ret = updater_check(&u, &http.http);
		proto_http_stop(&http);
	} else {
		updater_report(&u, LOG_TYPE_ERROR, "no Tcp or Tls interface");
	}
	proto_http_free(&http);

#if defined(CONFIG_APPLET_UPDATER_GUI)
	updater_gui_free(&u);
#endif
	return ret;
}


applet_ret_t updater_conf_init(Applet *self) {
	(void)self;

	/* No URL and no credentials by default, they are set by the stored configuration. */
	updater_conf.settings.url[0] = '\0';
	updater_conf.settings.username[0] = '\0';
	updater_conf.settings.password[0] = '\0';

	configlib_init_map(&updater_conf.tree.updater, "updater", NULL, CONF_SUBTREE);

	configlib_init(&updater_conf.tree.url, "url");
	configlib_map_string(&updater_conf.tree.url, updater_conf.settings.url, sizeof(updater_conf.settings.url));
	configlib_append(&updater_conf.tree.url, &updater_conf.tree.updater, CONF_DIR_CHILD);
	configlib_set_constraint(&updater_conf.tree.url, (union conf_constraint) {
		.str = {.max_len = UPDATER_URL_LEN - 1},
	});
	configlib_set_description(&updater_conf.tree.url, "Update URL",
		"URL the updates are fetched from, http:// or https://.");
	updater_conf.tree.url.flags = CONF_READ | CONF_WRITE;

	configlib_init(&updater_conf.tree.username, "username");
	configlib_map_string(&updater_conf.tree.username, updater_conf.settings.username,
	                     sizeof(updater_conf.settings.username));
	configlib_append(&updater_conf.tree.username, &updater_conf.tree.url, CONF_DIR_NEXT);
	configlib_set_constraint(&updater_conf.tree.username, (union conf_constraint) {
		.str = {.max_len = UPDATER_CRED_LEN - 1},
	});
	configlib_set_description(&updater_conf.tree.username, "User name",
		"User name of the HTTP Basic authentication, used if the server asks for it. Empty for none.");
	updater_conf.tree.username.flags = CONF_READ | CONF_WRITE;

	configlib_init(&updater_conf.tree.password, "password");
	configlib_map_string(&updater_conf.tree.password, updater_conf.settings.password,
	                     sizeof(updater_conf.settings.password));
	configlib_append(&updater_conf.tree.password, &updater_conf.tree.username, CONF_DIR_NEXT);
	configlib_set_constraint(&updater_conf.tree.password, (union conf_constraint) {
		.str = {.max_len = UPDATER_CRED_LEN - 1},
	});
	configlib_set_description(&updater_conf.tree.password, "Password",
		"Password of the HTTP Basic authentication.");
	updater_conf.tree.password.flags = CONF_READ | CONF_WRITE;

	return APPLET_RET_OK;
}


const Applet updater = {
	.executable.native = {
		.main = updater_main
	},
	.name = "Updater",
	.help = "Fetch firmware updates",
	.stack_size = 1536,
	.conf = &updater_conf.tree.updater.conf,
	.conf_init = updater_conf_init,
};
