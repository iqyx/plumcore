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

#include <main.h>
#include "u_log.h"
#include "u_assert.h"

#include <interfaces/window.h>
#include <interfaces/painter.h>
#include <interfaces/servicelocator.h>
#include <services/fb-compositor/fb-compositor.h>

#include "status-bar.h"

#define MODULE_NAME "status-bar"

#include "assets/assets.h"


static const struct painter_raw_image *bat_soc_images[11] = {
	&battery_0_data, &battery_10_data, &battery_20_data, &battery_30_data,
	&battery_40_data, &battery_50_data, &battery_60_data, &battery_70_data,
	&battery_80_data, &battery_90_data, &battery_100_data
};

/* Paint the status bar: a white strip with a clock placeholder, the active window title and a few
 * status icons. */
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
	painter->vmt->text(painter, 2, 5, "12:34");

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
