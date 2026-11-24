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
 * Fetches the configured URL with its own HTTP client running over the Tcp or Tls interface given by the port
 * (updater_conf.net) and reports the response status and the size of the fetched content. The applet runs without a GUI, the result is logged. If the GUI is enabled
 * (APPLET_UPDATER_GUI) and the applet is given a framebuffer, the result is also shown until ESC is pressed.
 *
 * The configuration tree is advertised as "applet updater", mounted at /applet/updater:
 *
 *     /applet/updater/url    URL the updates are fetched from (http or https)
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
#define UPDATER_READ_BUF_LEN 128
#define UPDATER_TEXT_LEN 48

#if defined(CONFIG_APPLET_UPDATER_GUI)
#define UPDATER_LINE_H 12
#endif


struct updater_conf updater_conf;

struct updater {
	struct applet_args *args;

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

#if defined(CONFIG_APPLET_UPDATER_GUI)
	if (!self->gui) {
		return;
	}
	Painter *painter = &self->painter.painter;
	painter->vmt->begin(painter);
	painter->vmt->set_pen(painter, 0xffffffff, 1);
	painter->vmt->set_font(painter, PAINTER_FONT_NORMAL, NULL);
	painter->vmt->text(painter, 2, (int16_t)(2 + self->lines * UPDATER_LINE_H), text);
	painter->vmt->end(painter);
	self->lines++;
#endif
}


/* Fetch the configured URL with @p http, read the whole body and report the response status and the body size. */
static applet_ret_t updater_fetch(struct updater *self, Http *http) {
	char text[UPDATER_TEXT_LEN];

	if (updater_conf.settings.url[0] == '\0') {
		updater_report(self, LOG_TYPE_ERROR, "no URL configured");
		return APPLET_RET_FAILED;
	}
	u_log((self->args->logger != NULL) ? self->args->logger : system_log, LOG_TYPE_INFO,
	      U_LOG_MODULE_PREFIX("fetching %s"), updater_conf.settings.url);
	updater_report(self, LOG_TYPE_INFO, "fetching...");

	const struct http_request request = {
		.method = HTTP_METHOD_GET,
		.target = updater_conf.settings.url,
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
	snprintf(text, sizeof(text), "status %u", (unsigned)response.status);
	updater_report(self, LOG_TYPE_INFO, text);

	/* Only the size of the content is reported, the data is dropped. */
	uint8_t buf[UPDATER_READ_BUF_LEN];
	size_t size = 0;
	size_t len = 0;
	while ((ret = http->vmt->read(http, session, buf, sizeof(buf), &len, UPDATER_READ_TIMEOUT_MS)) == HTTP_RET_OK) {
		size += len;
	}
	http->vmt->close(http, session);
	if (ret != HTTP_RET_EOF) {
		snprintf(text, sizeof(text), "read failed (%d) after %lu B", (int)ret, (unsigned long)size);
		updater_report(self, LOG_TYPE_ERROR, text);
		return APPLET_RET_FAILED;
	}
	snprintf(text, sizeof(text), "content size %lu B", (unsigned long)size);
	updater_report(self, LOG_TYPE_INFO, text);
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

	Painter *painter = &self->painter.painter;
	painter->vmt->begin(painter);
	painter->vmt->set_pen(painter, 0xff000000, 1);
	painter->vmt->set_brush(painter, 0xff000000);
	painter->vmt->rect(painter, 0, 0, self->w, self->h);
	painter->vmt->end(painter);
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


static applet_ret_t updater_main(Applet *self, struct applet_args *args) {
	(void)self;
	struct updater u;
	memset(&u, 0, sizeof(u));
	u.args = args;

#if defined(CONFIG_APPLET_UPDATER_GUI)
	updater_gui_init(&u);
#endif

	/* The HTTP client lives only while the applet runs. */
	applet_ret_t ret = APPLET_RET_FAILED;
	ProtoHttp http;
	if (proto_http_init(&http, &(struct proto_http_conf) {
		.tcp = updater_conf.net.tcp,
		.tls = updater_conf.net.tls,
	}) == PROTO_HTTP_RET_OK && proto_http_start(&http) == PROTO_HTTP_RET_OK) {
		ret = updater_fetch(&u, &http.http);
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

	/* No URL by default, it is set by the stored configuration. */
	updater_conf.settings.url[0] = '\0';

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

	return APPLET_RET_OK;
}


const Applet updater = {
	.executable.native = {
		.main = updater_main
	},
	.name = "Updater",
	.help = "Fetch firmware updates",
	.stack_size = 1280,
	.conf = &updater_conf.tree.updater.conf,
	.conf_init = updater_conf_init,
};
