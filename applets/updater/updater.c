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
 * retry.
 *
 * With the GUI, the applet then offers to update the device. The update volume (updater_conf.volume) is erased and
 * the firmware image is downloaded and written to it as it is received, both showing their progress in a notification
 * which cancels the update on ESC. The bootloader flashes the image from the volume after a reset, which is offered
 * once the image is written. The applet runs without a GUI, the result is logged. If the GUI is enabled
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
#define UPDATER_WRITE_BUF_LEN 512

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

	/* The user chose to exit, the applet does not wait for ESC. */
	bool exit;

	/* Progress notification of the update and the task setting cancel when ESC is pressed in it. */
	struct notify_window *progress;
	TaskHandle_t cancel_task;
	volatile bool cancel;
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


static void updater_report_latest(struct updater *self) {
	char latest[UPDATER_NAME_LEN + 8];
	snprintf(latest, sizeof(latest), "latest: %s", self->latest.name);
	updater_report(self, LOG_TYPE_INFO, latest);
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


/* Send a GET request for @p target and wait for a successful response, failures are reported. The session is open
 * on success. */
static applet_ret_t updater_get(struct updater *self, Http *http, const char *target, void **session,
                                struct http_response *response) {
	char text[UPDATER_TEXT_LEN];
	const struct http_request request = {
		.method = HTTP_METHOD_GET,
		.target = target,
		.has_content_length = true,
		.content_length = 0,
	};
	http_ret_t ret = http->vmt->open(http, &request, session, UPDATER_CONNECT_TIMEOUT_MS);
	if (ret != HTTP_RET_OK) {
		snprintf(text, sizeof(text), "cannot connect (%d)", (int)ret);
		updater_report(self, LOG_TYPE_ERROR, text);
		return APPLET_RET_FAILED;
	}
	ret = http->vmt->get_response(http, *session, response, UPDATER_RESPONSE_TIMEOUT_MS);
	if (ret != HTTP_RET_OK) {
		http->vmt->close(http, *session);
		snprintf(text, sizeof(text), "no response (%d)", (int)ret);
		updater_report(self, LOG_TYPE_ERROR, text);
		return APPLET_RET_FAILED;
	}
	if (response->status != 200) {
		http->vmt->close(http, *session);
		snprintf(text, sizeof(text), "status %u", (unsigned)response->status);
		updater_report(self, LOG_TYPE_ERROR, text);
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

	void *session = NULL;
	struct http_response response = {0};
	if (updater_get(self, http, target, &session, &response) != APPLET_RET_OK) {
		return APPLET_RET_FAILED;
	}

	/* The listing is read whole into the buffer. */
	char list[UPDATER_LIST_LEN];
	size_t size = 0;
	size_t len = 0;
	http_ret_t ret = HTTP_RET_OK;
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
	updater_report_latest(self);
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
	while (!self->exit) {
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


#if defined(CONFIG_APPLET_UPDATER_GUI)
/* Set cancel when ESC is pressed in the progress notification. Runs until deleted by updater_progress_close(). */
static void updater_cancel_task(void *p) {
	struct updater *self = (struct updater *)p;
	Event *event = self->progress->event;

	while (true) {
		enum event_type type = EV_TYPE_NONE;
		enum event_code code = EV_CODE_NONE;
		int32_t value = 0;
		if (event->vmt->listen(event, &type, &code, &value) == EV_RET_OK && type == EV_TYPE_KEY &&
		    code == EV_KEY_ESC && value != 0) {
			self->cancel = true;
		}
	}
}


/* Show a progress notification with @p message which cancels the update on ESC. */
static applet_ret_t updater_progress_open(struct updater *self, const char *message) {
	static const struct notify_window_action actions[] = {
		{.key = EV_KEY_ESC, .label = "ESC: Cancel"},
	};
	if (gui_wm_fs_notify(updater_conf.gui, NOTIFY_WINDOW_ICON_INFO, message, actions,
	                     sizeof(actions) / sizeof(actions[0]), 0, NULL, &self->progress) != GUI_WM_FS_RET_OK) {
		updater_report(self, LOG_TYPE_ERROR, "cannot show the progress");
		return APPLET_RET_FAILED;
	}
	if (xTaskCreate(updater_cancel_task, "updater-cancel", configMINIMAL_STACK_SIZE, (void *)self, 1,
	                &self->cancel_task) != pdPASS) {
		notify_window_close(self->progress);
		self->progress = NULL;
		updater_report(self, LOG_TYPE_ERROR, "cannot show the progress");
		return APPLET_RET_FAILED;
	}
	return APPLET_RET_OK;
}


static void updater_progress_close(struct updater *self) {
	if (self->progress == NULL) {
		return;
	}
	/* The task blocks on the notification events forever, delete it before the notification goes away. */
	vTaskDelete(self->cancel_task);
	notify_window_close(self->progress);
	self->progress = NULL;
}


/* Erase the whole update volume of @p volume_size bytes, block by block. */
static applet_ret_t updater_erase(struct updater *self, size_t volume_size) {
	Flash *flash = updater_conf.volume;

	/* The smallest erasable block, the deepest level supporting the erase. */
	size_t block = 0;
	size_t size = 0;
	flash_block_ops_t ops = 0;
	for (uint32_t i = 1; flash->vmt->get_size(flash, i, &size, &ops) == FLASH_RET_OK; i++) {
		if (ops & FLASH_BLOCK_OPS_ERASE) {
			block = size;
		}
	}
	if (block == 0 || volume_size % block != 0) {
		updater_report(self, LOG_TYPE_ERROR, "cannot erase the volume");
		return APPLET_RET_FAILED;
	}

	for (size_t addr = 0; addr < volume_size && !self->cancel; addr += block) {
		if (flash->vmt->erase(flash, addr, block) != FLASH_RET_OK) {
			updater_report(self, LOG_TYPE_ERROR, "erase failed");
			return APPLET_RET_FAILED;
		}
		notify_window_set_progress(self->progress, (uint32_t)volume_size, (uint32_t)(addr + block));
	}
	return APPLET_RET_OK;
}


/* Download the latest firmware with @p http and write it to the update volume as it is received. A write must not
 * cross a flash page of @p page bytes. */
static applet_ret_t updater_download(struct updater *self, Http *http, size_t volume_size, size_t page) {
	Flash *flash = updater_conf.volume;
	char text[UPDATER_TEXT_LEN];

	/* The listing gives the URL relative to the server, the scheme and the authority of the configured URL are
	 * prepended to it. */
	char target[2 * UPDATER_URL_LEN];
	const char *url = updater_conf.settings.url;
	const char *scheme_end = strstr(url, "://");
	if (strstr(self->latest.url, "://") != NULL || scheme_end == NULL) {
		snprintf(target, sizeof(target), "%s", self->latest.url);
	} else {
		size_t prefix = (size_t)(scheme_end + 3 - url) + strcspn(scheme_end + 3, "/?#");
		snprintf(target, sizeof(target), "%.*s%s", (int)prefix, url, self->latest.url);
	}
	u_log((self->args->logger != NULL) ? self->args->logger : system_log, LOG_TYPE_INFO,
	      U_LOG_MODULE_PREFIX("downloading %s"), target);

	void *session = NULL;
	struct http_response response = {0};
	if (updater_get(self, http, target, &session, &response) != APPLET_RET_OK) {
		return APPLET_RET_FAILED;
	}
	size_t total = response.has_content_length ? response.content_length : (size_t)self->latest.size;
	if (total > volume_size) {
		http->vmt->close(http, session);
		updater_report(self, LOG_TYPE_ERROR, "the image is too large");
		return APPLET_RET_FAILED;
	}

	/* The download progress is logged every 10 %. */
	struct log_cbuffer *log = (self->args->logger != NULL) ? self->args->logger : system_log;
	TickType_t start = xTaskGetTickCount();
	size_t log_step = (total >= 10) ? total / 10 : 1;
	size_t log_next = log_step;

	uint8_t buf[UPDATER_WRITE_BUF_LEN];
	size_t written = 0;
	size_t len = 0;
	http_ret_t ret = HTTP_RET_OK;
	applet_ret_t result = APPLET_RET_OK;
	while (!self->cancel &&
	       (ret = http->vmt->read(http, session, buf, sizeof(buf), &len, UPDATER_READ_TIMEOUT_MS)) == HTTP_RET_OK) {
		if (written + len > volume_size) {
			updater_report(self, LOG_TYPE_ERROR, "the image is too large");
			result = APPLET_RET_FAILED;
			break;
		}
		for (size_t off = 0; off < len;) {
			size_t n = page - written % page;
			n = (n < len - off) ? n : len - off;
			if (flash->vmt->write(flash, written, buf + off, n) != FLASH_RET_OK) {
				updater_report(self, LOG_TYPE_ERROR, "flash write failed");
				result = APPLET_RET_FAILED;
				break;
			}
			written += n;
			off += n;
		}
		if (result != APPLET_RET_OK) {
			break;
		}
		notify_window_set_progress(self->progress, (uint32_t)total, (uint32_t)written);
		if (written >= log_next) {
			u_log(log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("downloaded %lu/%lu B in %lu ms"), (unsigned long)written,
			      (unsigned long)total, (unsigned long)pdTICKS_TO_MS(xTaskGetTickCount() - start));
			log_next += log_step;
		}
	}
	http->vmt->close(http, session);
	if (written < total || ret != HTTP_RET_EOF) {
		u_log(log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("download stopped at %lu/%lu B after %lu ms (%d)"),
		      (unsigned long)written, (unsigned long)total,
		      (unsigned long)pdTICKS_TO_MS(xTaskGetTickCount() - start), (int)ret);
	}
	if (result != APPLET_RET_OK || self->cancel) {
		return APPLET_RET_FAILED;
	}
	if (ret != HTTP_RET_EOF) {
		snprintf(text, sizeof(text), "read failed (%d)", (int)ret);
		updater_report(self, LOG_TYPE_ERROR, text);
		return APPLET_RET_FAILED;
	}
	if (total > 0 && written != total) {
		updater_report(self, LOG_TYPE_ERROR, "incomplete download");
		return APPLET_RET_FAILED;
	}
	return APPLET_RET_OK;
}


/* Erase the update volume and write the latest firmware to it, each phase showing its progress. ESC cancels it. */
static applet_ret_t updater_write(struct updater *self, Http *http) {
	Flash *flash = updater_conf.volume;
	self->cancel = false;

	/* The volume size is level 0, the write unit is the level supporting the write. */
	size_t volume_size = 0;
	size_t page = 0;
	size_t size = 0;
	flash_block_ops_t ops = 0;
	flash->vmt->get_size(flash, 0, &volume_size, &ops);
	for (uint32_t i = 1; flash->vmt->get_size(flash, i, &size, &ops) == FLASH_RET_OK; i++) {
		if (ops & FLASH_BLOCK_OPS_WRITE) {
			page = size;
		}
	}
	if (volume_size == 0 || page == 0) {
		updater_report(self, LOG_TYPE_ERROR, "cannot use the update volume");
		return APPLET_RET_FAILED;
	}

	if (updater_progress_open(self, "Erasing the update volume") != APPLET_RET_OK) {
		return APPLET_RET_FAILED;
	}
	applet_ret_t ret = updater_erase(self, volume_size);
	updater_progress_close(self);
	if (ret != APPLET_RET_OK || self->cancel) {
		return APPLET_RET_FAILED;
	}

	char message[UPDATER_NAME_LEN + 16];
	snprintf(message, sizeof(message), "Downloading %s", self->latest.name);
	if (updater_progress_open(self, message) != APPLET_RET_OK) {
		return APPLET_RET_FAILED;
	}
	ret = updater_download(self, http, volume_size, page);
	updater_progress_close(self);
	return ret;
}


/* Offer to update the device to the latest firmware and reset it once the image is written. */
static void updater_offer(struct updater *self, Http *http) {
	/* The default action 0 (also returned while the device is off) is always the harmless one. */
	static const struct notify_window_action ask[] = {
		{.key = EV_KEY_ESC, .label = "ESC: No & Exit"},
		{.key = EV_KEY_ENTER, .label = "OK: Update"},
	};
	char message[UPDATER_NAME_LEN + 48];
	snprintf(message, sizeof(message), "Fetch %s and update the device?", self->latest.name);
	size_t index = 0;
	gui_wm_fs_notify(updater_conf.gui, NOTIFY_WINDOW_ICON_INFO, message, ask, sizeof(ask) / sizeof(ask[0]),
	                 UINT32_MAX, &index, NULL);
	if (index != 1) {
		self->exit = true;
		return;
	}

	applet_ret_t ret = updater_write(self, http);

	/* Back to the applet screen showing the file. */
	updater_gui_clear(self);
	updater_report_latest(self);
	if (self->cancel) {
		updater_report(self, LOG_TYPE_INFO, "update cancelled");
		return;
	}
	if (ret != APPLET_RET_OK) {
		static const struct notify_window_action dismiss[] = {
			{.key = EV_KEY_ESC, .label = "ESC: Dismiss"},
		};
		snprintf(message, sizeof(message), "Update failed: %s", self->error);
		gui_wm_fs_notify(updater_conf.gui, NOTIFY_WINDOW_ICON_ERROR, message, dismiss,
		                 sizeof(dismiss) / sizeof(dismiss[0]), UINT32_MAX, &index, NULL);
		return;
	}

	updater_report(self, LOG_TYPE_INFO, "update written to the flash");
	static const struct notify_window_action reset[] = {
		{.key = EV_KEY_ESC, .label = "ESC: No & return"},
		{.key = EV_KEY_ENTER, .label = "OK: Reset"},
	};
	index = 0;
	gui_wm_fs_notify(updater_conf.gui, NOTIFY_WINDOW_ICON_INFO,
	                 "The update was written to the flash. Reset the device now?", reset,
	                 sizeof(reset) / sizeof(reset[0]), UINT32_MAX, &index, NULL);
	if (index == 1) {
		u_log((self->args->logger != NULL) ? self->args->logger : system_log, LOG_TYPE_INFO,
		      U_LOG_MODULE_PREFIX("resetting to apply the update"));
		vTaskDelay(pdMS_TO_TICKS(100));
		platform_reset();
	}
}
#endif


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
#if defined(CONFIG_APPLET_UPDATER_GUI)
		if (ret == APPLET_RET_OK && u.gui && updater_conf.gui != NULL && updater_conf.volume != NULL) {
			updater_offer(&u, &http.http);
		}
#endif
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
