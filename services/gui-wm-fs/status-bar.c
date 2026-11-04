/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Status bar component of the full-screen GUI window manager
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 *
 * A small self-contained component of the gui-wm-fs service. It creates a single full-width window
 * on the compositor along the top edge and paints a status bar into it.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include <main.h>
#include "u_log.h"
#include "u_assert.h"

#include <interfaces/window.h>
#include <interfaces/painter.h>
#include <interfaces/servicelocator.h>
#include <services/fb-compositor/fb-compositor.h>
#include <services/system-conf/system-conf.h>

#include "status-bar.h"

#define MODULE_NAME "status-bar"

#include "assets/assets.h"


static const struct painter_raw_image *bat_soc_images[11] = {
	&battery_0_data, &battery_10_data, &battery_20_data, &battery_30_data,
	&battery_40_data, &battery_50_data, &battery_60_data, &battery_70_data,
	&battery_80_data, &battery_90_data, &battery_100_data
};

/* Find a direct child of a configuration node by name. */
static conf_ret_t status_bar_conf_child(Conf *parent, const char *name, Conf **child) {
	Conf *node = NULL;
	if (parent->vmt->walk(parent, CONF_DIR_CHILD, &node) != CONF_RET_OK) {
		return CONF_RET_FAILED;
	}
	while (node != NULL) {
		const char *node_name = NULL;
		enum conf_type type;
		enum conf_flag flags;
		if (node->vmt->stat(node, &node_name, &type, &flags) == CONF_RET_OK && node_name != NULL &&
		    strcmp(node_name, name) == 0) {
			*child = node;
			return CONF_RET_OK;
		}
		if (node->vmt->walk(node, CONF_DIR_NEXT, &node) != CONF_RET_OK) {
			return CONF_RET_FAILED;
		}
	}
	return CONF_RET_FAILED;
}


/* Day of the month of the n-th Sunday of the month @p t falls into. */
static int status_bar_nth_sunday(const struct tm *t, int n) {
	int first_wday = (t->tm_wday - (t->tm_mday - 1) % 7 + 7) % 7;
	return 1 + (7 - first_wday) % 7 + 7 * (n - 1);
}


/* Day of the month of the last Sunday of a 31-day month @p t falls into. */
static int status_bar_last_sunday(const struct tm *t) {
	return 31 - (t->tm_wday + 31 - t->tm_mday) % 7;
}


/* Whether DST is in effect at the time @p t, March and the transition month @p end_mon are checked against the
 * transition day and hour, the months in between are always DST. */
static bool status_bar_dst_active(const struct tm *t, int start_mday, int start_hour, int end_mon, int end_mday,
                                  int end_hour) {
	if (t->tm_mon > 2 && t->tm_mon < end_mon) {
		return true;
	}
	if (t->tm_mon == 2) {
		return t->tm_mday > start_mday || (t->tm_mday == start_mday && t->tm_hour >= start_hour);
	}
	if (t->tm_mon == end_mon) {
		return t->tm_mday < end_mday || (t->tm_mday == end_mday && t->tm_hour < end_hour);
	}
	return false;
}


/* Offset of the local time from UTC at @p utc in seconds, determined from the "system timezone" configuration.
 * UTC is used if the configuration is not available. */
static int32_t status_bar_utc_offset(StatusBar *self, time_t utc) {
	Conf *tz = NULL;
	if (self->system == NULL || status_bar_conf_child(self->system, "timezone", &tz) != CONF_RET_OK) {
		return 0;
	}

	Conf *node = NULL;
	union conf_val offset = {0};
	union conf_val dst = {0};
	if (status_bar_conf_child(tz, "offset", &node) == CONF_RET_OK) {
		node->vmt->read(node, &offset);
	}
	if (status_bar_conf_child(tz, "dst", &node) == CONF_RET_OK) {
		node->vmt->read(node, &dst);
	}

	int32_t std_offset = offset.i16 * 60;
	struct tm t = {0};
	switch (dst.u32) {
		case SYSTEM_CONF_DST_EU:
			/* Transitions at 01:00 UTC, evaluated on the UTC time. */
			gmtime_r(&utc, &t);
			if (status_bar_dst_active(&t, status_bar_last_sunday(&t), 1, 9, status_bar_last_sunday(&t), 1)) {
				return std_offset + 3600;
			}
			break;
		case SYSTEM_CONF_DST_US: {
			/* Transitions at 02:00 local time, evaluated on the standard time. DST ends at 02:00 DST, which
			 * is 01:00 standard time. */
			time_t std = utc + std_offset;
			gmtime_r(&std, &t);
			if (status_bar_dst_active(&t, status_bar_nth_sunday(&t, 2), 2, 10, status_bar_nth_sunday(&t, 1), 1)) {
				return std_offset + 3600;
			}
			break;
		}
		default:
			break;
	}
	return std_offset;
}


/* Paint the status bar: a white strip with the current time, the active window title and a few status icons. */
static void status_bar_render(StatusBar *self) {
	Painter *painter = &self->painter.painter;

	painter->vmt->begin(painter);
	painter->vmt->set_pen(painter, 0xffffffff, 1);
	painter->vmt->set_brush(painter, 0xffffffff);
	painter->vmt->rect(painter, 0, 0, 240, 18);
	painter->vmt->image(painter, 35, 0, &tab_left_data, PAINTER_MODE_NORMAL);

	painter->vmt->set_pen(painter, 0xff000000, 1);
	painter->vmt->set_brush(painter, 0xff000000);
	painter->vmt->rect(painter, 0, 0, 37, 17);
	painter->vmt->image(painter, 32, 0, &tab_left_data, PAINTER_MODE_NORMAL);
	painter->vmt->rect(painter, 180, 0, 60, 17);
	painter->vmt->image(painter, 175, 0, &tab_right_data, PAINTER_MODE_NORMAL);


	painter->vmt->set_font(painter, PAINTER_FONT_BOLD, NULL);
	painter->vmt->set_pen(painter, 0xffffffff, 1);
	/* The clock runs in UTC, it is converted to local time for display only. */
	char time_str[6] = "--:--";
	struct timespec now = {0};
	if (self->conf.clock != NULL && self->conf.clock->vmt->get(self->conf.clock, &now) == CLOCK_RET_OK) {
		time_t local = now.tv_sec + status_bar_utc_offset(self, now.tv_sec);
		struct tm tm = {0};
		gmtime_r(&local, &tm);
		strftime(time_str, sizeof(time_str), "%H:%M", &tm);
	}
	painter->vmt->text(painter, 2, 5, time_str);

	struct window_stat stat = {.title = "Unknown"};
	if (self->active_window && self->active_window->vmt->stat && self->active_window->vmt->stat(self->active_window, &stat) == WINDOW_RET_OK) {
		painter->vmt->set_pen(painter, 0xff000000, 1);
		painter->vmt->text(painter, 44, 5, stat.title);
	}

	int16_t current_x = self->conf.geometry.w - 1;

	/* Render battery/cell picture depending on the actual SoC reported by a sensor (conf dependency). */
	float bat_soc = 0.0f;
	const struct painter_raw_image *bat_image = &battery_70_data;
	if (self->conf.bat_soc != NULL && self->conf.bat_soc->vmt->value_f(self->conf.bat_soc, &bat_soc) == SENSOR_RET_OK) {
		if (bat_soc < 0.0f) {
			bat_soc = 0.0f;
		}
		if (bat_soc > 1.0f) {
			bat_soc = 1.0f;
		}
		bat_image = bat_soc_images[(int)(bat_soc * 10.0f)];
	}
	painter->vmt->image(painter, (current_x -= 12), 0, bat_image, PAINTER_MODE_INVERTED);

	/* Try to obtain a charging current value. If it is positive, draw a charging icon next to the battery. */
	float bat_current = 0.0f;
	if (self->conf.bat_current != NULL &&
	    self->conf.bat_current->vmt->value_f(self->conf.bat_current, &bat_current) == SENSOR_RET_OK &&
	    bat_current > 0.0f) {
		painter->vmt->image(painter, (current_x -= 6), 0, &charging_data, PAINTER_MODE_INVERTED);
	}

	/* Bluetooth icon: shown steadily while a discovered BLE device is connected, and blinked while it is
	 * pairing by toggling its visibility on each repaint. */
	struct ble_status ble_status = {0};
	if (self->ble != NULL && self->ble->vmt->get_status(self->ble, &ble_status) == BLE_RET_OK) {
		if (ble_status.pairing) {
			self->bt_icon_shown = !self->bt_icon_shown;
		} else {
			self->bt_icon_shown = ble_status.connected;
		}
	} else {
		self->bt_icon_shown = false;
	}
	if (self->bt_icon_shown) {
		painter->vmt->image(painter, (current_x -= 12), 0, &bt_data, PAINTER_MODE_INVERTED);
	}

	painter->vmt->end(painter);
}


/* Periodically sample the active window and repaint the bar whenever it changes.
 * Also repaint periodically to update status icons. */
static void status_bar_task(void *p) {
	StatusBar *self = (StatusBar *)p;

	self->running = true;
	int repaint_timeout = 1000;
	while (self->can_run) {
		Window *active = NULL;
		if (fb_compositor_get_active_window(self->conf.compositor, &active) == FB_COMPOSITOR_RET_OK && active != self->active_window) {
			self->active_window = active;
			repaint_timeout = 0;
		}

		if (repaint_timeout <= 0) {
			status_bar_render(self);
			repaint_timeout = 1000;
		}

		vTaskDelay(pdMS_TO_TICKS(STATUS_BAR_PERIOD_MS));
		repaint_timeout -= STATUS_BAR_PERIOD_MS;
	}
	self->running = false;

	vTaskDelete(NULL);
}


status_bar_ret_t status_bar_init(StatusBar *self, const struct status_bar_conf *conf) {
	if (u_assert(self != NULL) ||
	    u_assert(conf != NULL)) {
		return STATUS_BAR_RET_NULL;
	}
	memset(self, 0, sizeof(StatusBar));
	memcpy(&self->conf, conf, sizeof(struct status_bar_conf));

	/* Discover a BLE device to show a connection icon for. A port without one simply never draws the icon. */
	self->ble = NULL;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_BLE, 0, (Interface **)&self->ble) !=
	    ISERVICELOCATOR_RET_OK) {
		self->ble = NULL;
	}

	/* Discover the system configuration holding the timezone. Without it the time is shown in UTC. */
	self->system = NULL;
	if (iservicelocator_query_name_type(locator, "system", ISERVICELOCATOR_TYPE_CONF, (Interface **)&self->system) !=
	    ISERVICELOCATOR_RET_OK) {
		self->system = NULL;
	}

	if (self->conf.compositor->factory.vmt->create(&self->conf.compositor->factory, &self->conf.geometry,
	    &self->window) != WINDOW_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create the window"));
		goto err;
	}

	Fb *win_fb = NULL;
	self->window->vmt->get_fb(self->window, &win_fb);
	if (fb_painter_init(&self->painter, win_fb) != FB_PAINTER_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start the window painter"));
		goto err;
	}

	status_bar_render(self);
	self->window->vmt->show(self->window, true);

	self->can_run = true;
	xTaskCreate(status_bar_task, "status-bar", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &self->task);
	if (self->task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		self->can_run = false;
		goto err;
	}

	return STATUS_BAR_RET_OK;
err:
	status_bar_free(self);
	return STATUS_BAR_RET_FAILED;
}


status_bar_ret_t status_bar_free(StatusBar *self) {
	if (u_assert(self != NULL)) {
		return STATUS_BAR_RET_FAILED;
	}

	/* Stop the task first so nothing touches the window while it is torn down. */
	self->can_run = false;
	while (self->running) {
		vTaskDelay(100);
	}

	if (self->window != NULL) {
		fb_painter_free(&self->painter);
		self->conf.compositor->factory.vmt->destroy(&self->conf.compositor->factory, self->window);
		self->window = NULL;
	}

	return STATUS_BAR_RET_OK;
}
