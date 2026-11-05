/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Charging window component of the full-screen GUI window manager
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 *
 * A small self-contained component of the gui-wm-fs service. It creates a window on the compositor and brings
 * it to front whenever a charger power source gets connected or disconnected. The window shows a process diagram
 * of the power flow: the two power source ports with their voltages and currents feed the DC/DC charger, which
 * charges or is discharged from the battery, the flow is labelled with the battery current. The window is dismissed
 * by ESC or after CHARGE_WINDOW_SHOW_MS, it is shown again on the next connection change.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include <main.h>
#include "u_log.h"
#include "u_assert.h"

#include <interfaces/window.h>
#include <interfaces/painter.h>
#include <interfaces/sensor.h>
#include <services/fb-compositor/fb-compositor.h>

#include "charge-window.h"

#define MODULE_NAME "charge-window"

#include "assets/assets.h"

#define CHARGE_WINDOW_MARGIN 8
#define CHARGE_WINDOW_ARROW_SIZE 4


/* Read a sensor value, false if the sensor is missing or cannot be read. */
static bool charge_window_read(Sensor *sensor, float *value) {
	return sensor != NULL && sensor->vmt->value_f(sensor, value) == SENSOR_RET_OK;
}


/* Whether any of the power sources is connected. */
static bool charge_window_connected(ChargeWindow *self) {
	float v = 0.0f;
	return (charge_window_read(self->conf.source1_voltage, &v) && v > CHARGE_WINDOW_SOURCE_MIN_V) ||
	       (charge_window_read(self->conf.source2_voltage, &v) && v > CHARGE_WINDOW_SOURCE_MIN_V);
}


/* Draw "value unit" centered horizontally at @p cx, the value with two decimal places or "--" if not valid. */
static void charge_window_label(Painter *painter, int16_t cx, int16_t y, bool valid, float value, const char *unit) {
	char label[16];
	if (valid) {
		int32_t v = (int32_t)(value * 100.0f);
		snprintf(label, sizeof(label), "%s%ld.%02ld %s", (v < 0) ? "-" : "", labs(v) / 100, labs(v) % 100, unit);
	} else {
		snprintf(label, sizeof(label), "-- %s", unit);
	}
	uint16_t w = 0;
	painter->vmt->text_size(painter, label, &w, NULL);
	painter->vmt->text(painter, cx - w / 2, y, label);
}


/* Draw a filled arrowhead with its tip at (x, y), pointing right for @p dir 1 and left for -1. */
static void charge_window_arrowhead(Painter *painter, int16_t x, int16_t y, int16_t dir) {
	painter->vmt->set_pen(painter, 0xffffffff, 1);
	for (int16_t i = 0; i <= CHARGE_WINDOW_ARROW_SIZE; i++) {
		painter->vmt->line(painter, x - dir * i, y - i, x - dir * i, y + i);
	}
}


/* Draw a power source: its port icon with the top edge at @p top, the voltage and current below it and a flow
 * line routed to the DC/DC converter input at (@p dcdc_x, @p entry_y). The line is drawn thick with an arrowhead
 * if the source is connected. The input current is measured for the selected source only, it is read if
 * @p selected is set, the current is zero otherwise. Returns whether the source is connected. */
static bool charge_window_source(ChargeWindow *self, const struct painter_raw_image *icon, int16_t top,
                                 Sensor *voltage, bool selected, int16_t dcdc_x, int16_t entry_y) {
	Painter *painter = &self->painter.painter;
	int16_t cx = CHARGE_WINDOW_MARGIN + icon->w / 2;
	int16_t cy = top + icon->h / 2;

	painter->vmt->image(painter, CHARGE_WINDOW_MARGIN, top, icon, PAINTER_MODE_INVERTED);

	float v = 0.0f;
	bool v_valid = charge_window_read(voltage, &v);
	bool connected = v_valid && v > CHARGE_WINDOW_SOURCE_MIN_V;
	float i = 0.0f;
	bool i_valid = true;
	if (connected && selected) {
		i_valid = charge_window_read(self->conf.source_current, &i);
	}

	painter->vmt->set_pen(painter, 0xffffffff, 1);
	painter->vmt->set_font(painter, PAINTER_FONT_BOLD, NULL);
	charge_window_label(painter, cx, top + icon->h + 3, v_valid, v, "V");
	charge_window_label(painter, cx, top + icon->h + 13, i_valid, i, "A");

	/* Route the flow line right from the icon, then vertically along the input bus and right again into the
	 * converter. */
	int16_t bus_x = dcdc_x - 16;
	painter->vmt->set_pen(painter, 0xffffffff, connected ? 2 : 1);
	painter->vmt->move_to(painter, CHARGE_WINDOW_MARGIN + icon->w + 4, cy);
	painter->vmt->line_to(painter, bus_x, cy);
	painter->vmt->line_to(painter, bus_x, entry_y);
	painter->vmt->line_to(painter, dcdc_x - 2, entry_y);
	if (connected) {
		charge_window_arrowhead(painter, dcdc_x - 2, entry_y, 1);
	}

	return connected;
}


/* Paint a process diagram: the two power source ports on the left feed the DC/DC charger in the middle, which
 * is connected to the battery on the right. */
static void charge_window_render(ChargeWindow *self) {
	Painter *painter = &self->painter.painter;
	int16_t w = self->conf.geometry.w;
	int16_t h = self->conf.geometry.h;

	painter->vmt->begin(painter);

	/* Clear the whole window to a black background. */
	painter->vmt->set_pen(painter, 0xff000000, 1);
	painter->vmt->set_brush(painter, 0xff000000);
	painter->vmt->rect(painter, 0, 0, w, h);

	/* DC/DC charger in the center. */
	int16_t dcdc_x = (w - (int16_t)dcdc_data.w) / 2;
	int16_t dcdc_y = (h - (int16_t)dcdc_data.h) / 2;
	painter->vmt->image(painter, dcdc_x, dcdc_y, &dcdc_data, PAINTER_MODE_INVERTED);

	/* Power source ports stacked on the left, each centered in its half of the window together with the voltage
	 * and current lines below the icon. The charger prefers source 1, the input current belongs to source 2 only
	 * if source 1 is not connected. */
	int16_t half = h / 2;
	int16_t block_h = port_usb_c_data.h + 23;
	bool usb_connected = charge_window_source(self, &port_usb_c_data, (half - block_h) / 2, self->conf.source1_voltage,
	                                          true, dcdc_x, dcdc_y + dcdc_data.h / 3);
	charge_window_source(self, &port_circular_data, half + (half - block_h) / 2, self->conf.source2_voltage,
	                     !usb_connected, dcdc_x, dcdc_y + dcdc_data.h * 2 / 3);

	/* Battery on the right, filled from the bottom according to the state of charge. The fill area is the inside
	 * of the battery_big outline with a 1 pixel gap. */
	int16_t bat_x = w - CHARGE_WINDOW_MARGIN - battery_big_data.w;
	int16_t bat_y = (h - (int16_t)battery_big_data.h) / 2;
	painter->vmt->image(painter, bat_x, bat_y, &battery_big_data, PAINTER_MODE_INVERTED);
	float bat_soc = 0.0f;
	if (charge_window_read(self->conf.bat_soc, &bat_soc)) {
		if (bat_soc < 0.0f) {
			bat_soc = 0.0f;
		}
		if (bat_soc > 1.0f) {
			bat_soc = 1.0f;
		}
		uint16_t fill_h = (uint16_t)(37.0f * bat_soc);
		if (fill_h > 0) {
			painter->vmt->set_pen(painter, 0xffffffff, 0);
			painter->vmt->set_brush(painter, 0xffffffff);
			painter->vmt->rect(painter, bat_x + 4, bat_y + 44 - fill_h, 18, fill_h);
		}
	}

	/* Flow line between the charger and the battery labelled with the battery current. The arrow points to the
	 * battery while charging (positive current) and to the charger while discharging. */
	int16_t x1 = dcdc_x + dcdc_data.w + 1;
	int16_t x2 = bat_x - 2;
	int16_t y = h / 2;
	float bat_current = 0.0f;
	bool bat_current_valid = charge_window_read(self->conf.bat_current, &bat_current);
	painter->vmt->set_pen(painter, 0xffffffff, 2);
	painter->vmt->line(painter, x1, y, x2, y);
	if (bat_current_valid && bat_current > 0.0f) {
		charge_window_arrowhead(painter, x2, y, 1);
	} else if (bat_current_valid && bat_current < 0.0f) {
		charge_window_arrowhead(painter, x1, y, -1);
	}
	painter->vmt->set_pen(painter, 0xffffffff, 1);
	painter->vmt->set_font(painter, PAINTER_FONT_BOLD, NULL);
	charge_window_label(painter, (x1 + x2) / 2, y - 14, bat_current_valid, bat_current, "A");

	painter->vmt->end(painter);
}


/* Hide the window and stop the show timeout. */
static void charge_window_hide(ChargeWindow *self) {
	self->show_ms = 0;
	self->window->vmt->show(self->window, false);
}


/* Periodically sample the power sources. Bring the window to front when they get connected or disconnected,
 * hide it when the show timeout expires and keep repainting it while shown. */
static void charge_window_task(void *p) {
	ChargeWindow *self = (ChargeWindow *)p;

	self->running = true;
	while (self->can_run) {
		bool connected = charge_window_connected(self);
		if (connected != self->connected) {
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("power source %s"),
			      connected ? "connected" : "disconnected");
			charge_window_render(self);
			self->show_ms = CHARGE_WINDOW_SHOW_MS;
			self->window->vmt->to_front(self->window);
			self->window->vmt->show(self->window, true);
		} else if (self->show_ms > 0) {
			self->show_ms -= CHARGE_WINDOW_PERIOD_MS;
			if (self->show_ms > 0) {
				charge_window_render(self);
			} else {
				charge_window_hide(self);
			}
		}
		self->connected = connected;

		vTaskDelay(pdMS_TO_TICKS(CHARGE_WINDOW_PERIOD_MS));
	}
	self->running = false;

	vTaskDelete(NULL);
}


/* Dismiss the window on ESC. Events are routed here only while the window is the top-level one. */
static void charge_window_event_task(void *p) {
	ChargeWindow *self = (ChargeWindow *)p;

	Event *event = NULL;
	self->window->vmt->get_event(self->window, &event);

	while (true) {
		enum event_type type = EV_TYPE_NONE;
		enum event_code code = EV_CODE_NONE;
		int32_t value = 0;
		if (event->vmt->listen(event, &type, &code, &value) == EV_RET_OK && code == EV_KEY_ESC && value == 1) {
			charge_window_hide(self);
		}
	}
}


charge_window_ret_t charge_window_init(ChargeWindow *self, const struct charge_window_conf *conf) {
	if (u_assert(self != NULL) ||
	    u_assert(conf != NULL)) {
		return CHARGE_WINDOW_RET_NULL;
	}
	memset(self, 0, sizeof(ChargeWindow));
	memcpy(&self->conf, conf, sizeof(struct charge_window_conf));

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

	self->window->vmt->set_title(self->window, "Charging");

	self->can_run = true;
	xTaskCreate(charge_window_task, "charge-window", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &self->task);
	if (self->task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		self->can_run = false;
		goto err;
	}

	xTaskCreate(charge_window_event_task, "charge-window-ev", configMINIMAL_STACK_SIZE, (void *)self, 1,
	            &self->event_task);
	if (self->event_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create the event task"));
		goto err;
	}

	return CHARGE_WINDOW_RET_OK;
err:
	charge_window_free(self);
	return CHARGE_WINDOW_RET_FAILED;
}


charge_window_ret_t charge_window_free(ChargeWindow *self) {
	if (u_assert(self != NULL)) {
		return CHARGE_WINDOW_RET_FAILED;
	}

	/* Stop the task first so nothing touches the window while it is torn down. */
	self->can_run = false;
	while (self->running) {
		vTaskDelay(100);
	}

	/* The event task blocks on the window event queue forever, delete it before the window goes away. */
	if (self->event_task != NULL) {
		vTaskDelete(self->event_task);
		self->event_task = NULL;
	}

	if (self->window != NULL) {
		fb_painter_free(&self->painter);
		self->conf.compositor->factory.vmt->destroy(&self->conf.compositor->factory, self->window);
		self->window = NULL;
	}

	return CHARGE_WINDOW_RET_OK;
}
