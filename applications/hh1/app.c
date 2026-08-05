/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * nwdaq-main-hh1 battery monitor application
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <main.h>

#include <interfaces/servicelocator.h>
#include <interfaces/power.h>
#include <interfaces/sensor.h>
#include <interfaces/stream.h>
#include "app.h"

#define MODULE_NAME "hh1"

#define APP_BATTERY_W 28
#define APP_BATTERY_H 46

/* Read a sensor value in its SI base units, returning zero if the sensor is missing or fails. */
static float app_read_sensor(Sensor *sensor) {
	float value = 0.0f;
	if (sensor != NULL) {
		sensor->vmt->value_f(sensor, &value);
	}
	return value;
}


/* Draw a vertical 14500 cell on a painter with a 10-bar state-of-charge gauge inside it and a text
 * readout to its right. (x, y) is the top-left corner of the cell's bounding box (positive terminal
 * included). The cell is two rectangles: a framed body and a small solid positive terminal button on
 * top. soc is a 0.0f..1.0f ratio; voltage and current are in SI base units (V, A) and the current
 * sign decides the charging/discharging status. The overall size is set by APP_BATTERY_W/H. */
static void app_draw_battery(Painter *painter, int16_t x, int16_t y, float soc, float voltage, float current) {
	const uint16_t term_w = 12;          /* positive terminal button on top */
	const uint16_t term_h = 6;
	const uint16_t body_w = APP_BATTERY_W;
	const uint16_t body_h = APP_BATTERY_H - term_h;
	const int16_t inset = 4;             /* body frame + padding before the bars start */
	const int16_t text_x = x + (int16_t)body_w + 6;
	const int16_t line_h = 10;
	const int bars_total = 10;

	if (soc < 0.0f) {
		soc = 0.0f;
	}
	if (soc > 1.0f) {
		soc = 1.0f;
	}
	int bars_on = (int)(soc * bars_total + 0.5f);
	int16_t body_y = y + (int16_t)term_h;

	painter->vmt->begin(painter);

	/* Positive terminal button on top, a solid black nub, horizontally centred. */
	painter->vmt->set_pen(painter, 0xff000000, 1);
	painter->vmt->set_brush(painter, 0xff000000);
	painter->vmt->rect(painter, x + (int16_t)(body_w - term_w) / 2, y + 2, term_w, term_h);

	/* Body: a white cell with a chunky black frame. */
	painter->vmt->set_pen(painter, 0xff000000, 2);
	painter->vmt->set_brush(painter, 0xffffffff);
	painter->vmt->rect(painter, x, body_y, body_w, body_h);

	/* Ten horizontal SoC bars filling the body interior from the bottom up: the first bars_on are
	 * solid, the rest are left empty (white with a thin outline) so the gauge always shows "n of ten". */
	int16_t inner_x = x + inset;
	int16_t inner_y = body_y + inset;
	uint16_t inner_w = body_w - 2 * inset;
	uint16_t inner_h = body_h - 2 * inset;
	uint16_t slot = inner_h / bars_total;
	uint16_t bar_h = (slot > 1) ? (slot - 1) : 1;

	for (int i = 0; i < bars_total; i++) {
		int16_t bar_y = inner_y + (int16_t)(inner_h - (i + 1) * slot);
		painter->vmt->set_pen(painter, (i < bars_on) ? 0xff000000 : 0xffffffff, 1);
		painter->vmt->set_brush(painter, (i < bars_on) ? 0xff000000 : 0xffffffff);
		painter->vmt->rect(painter, inner_x, bar_y, inner_w, bar_h);
	}

	painter->vmt->set_pen(painter, 0xff444444, 1);
	painter->vmt->set_brush(painter, 0xff444444);
	painter->vmt->rect(painter, x + APP_BATTERY_W, y, 120 - APP_BATTERY_W, 48);

	/* Text readout to the right: a bold charging/discharging status, then SoC, voltage and current. */
	char line[24];
	int32_t mv = (int32_t)(voltage * 1000.0f);
	int32_t ma = (int32_t)(current * 1000.0f);

	painter->vmt->set_pen(painter, 0xffffffff, 1);
	painter->vmt->set_font(painter, PAINTER_FONT_BOLD, NULL);
	painter->vmt->text(painter, text_x, body_y, current >= 0.0f ? "Charging" : "Discharging");

	painter->vmt->set_font(painter, PAINTER_FONT_NORMAL, NULL);
	snprintf(line, sizeof(line), "SoC: %ld%%", (int32_t)(soc * 100.0f + 0.5f));
	painter->vmt->text(painter, text_x, body_y + line_h, line);
	snprintf(line, sizeof(line), "%ld.%02ld V", mv / 1000, (mv % 1000) / 10);
	painter->vmt->text(painter, text_x, body_y + 2 * line_h, line);
	snprintf(line, sizeof(line), "%ld mA", ma);
	painter->vmt->text(painter, text_x, body_y + 3 * line_h, line);

	painter->vmt->end(painter);
}


static void app_task(void *p) {
	App *self = p;

	/* Start charging the battery. */
	self->charger->vmt->enable(self->charger, true);

	self->charger->vmt->set_current_limit(self->charger, 1.5f);

	while (true) {
		/* Read the fuel gauge sensors (SI base units) and write the battery status to the serial
		 * console, scaled back to the human-friendly display units. */
		char s[96];
		snprintf(s, sizeof(s), "Vbat: %ld mV  Ibat: %ld mA  SoC: %ld %%  SoH: %ld %%  Qrem: %ld mAh\r\n",
			(int32_t)(app_read_sensor(self->bat_voltage) * 1000.0f),
			(int32_t)(app_read_sensor(self->bat_current) * 1000.0f),
			(int32_t)(app_read_sensor(self->bat_soc) * 100.0f),
			(int32_t)(app_read_sensor(self->bat_soh) * 100.0f),
			(int32_t)(app_read_sensor(self->bat_remaining) * 1000.0f));
		if (self->console != NULL) {
			self->console->vmt->write(self->console, s, strlen(s));
		}

		vTaskDelay(1000);
	}
	vTaskDelete(NULL);
}


/* Bring up the full-screen GUI window manager on the LCD. The gui-wm-fs service runs a compositor on
 * the discovered framebuffer and lays out a top status bar and a bottom button bar; the keypad, if
 * present, drives its input. */
static app_ret_t app_setup_ui(App *self) {
	if (self->lcd == NULL) {
		return APP_RET_FAILED;
	}

	struct gui_wm_fs_conf gui_conf = {
		.fb = self->lcd,
		.event = self->keypad,
		.beeper = self->beeper,
		.bat_soc = self->bat_soc,
		.bat_current = self->bat_current,
	};
	if (gui_wm_fs_init(&self->gui, &gui_conf) != GUI_WM_FS_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start the GUI"));
		return APP_RET_FAILED;
	}

	return APP_RET_OK;
}


app_ret_t app_init(App *self) {
	memset(self, 0, sizeof(App));

	/* Discover the battery charging power device advertised by the BQ25798 charger driver. */
	self->charger = NULL;
	if (iservicelocator_query_name_type(locator, "charger", ISERVICELOCATOR_TYPE_POWER, (Interface **)&self->charger) != ISERVICELOCATOR_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("charger power device not found"));
		return APP_RET_FAILED;
	}

	/* Discover the serial console stream the port advertises for the fuel gauge readout. */
	self->console = NULL;
	if (iservicelocator_query_name_type(locator, "console", ISERVICELOCATOR_TYPE_STREAM, (Interface **)&self->console) != ISERVICELOCATOR_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("console stream not found"));
	}

	/* Discover the battery fuel gauge sensors advertised by the BQ27441 driver. */
	iservicelocator_query_name_type(locator, "bat_voltage", ISERVICELOCATOR_TYPE_SENSOR, (Interface **)&self->bat_voltage);
	iservicelocator_query_name_type(locator, "bat_current", ISERVICELOCATOR_TYPE_SENSOR, (Interface **)&self->bat_current);
	iservicelocator_query_name_type(locator, "bat_soc", ISERVICELOCATOR_TYPE_SENSOR, (Interface **)&self->bat_soc);
	iservicelocator_query_name_type(locator, "bat_soh", ISERVICELOCATOR_TYPE_SENSOR, (Interface **)&self->bat_soh);
	iservicelocator_query_name_type(locator, "bat_remaining", ISERVICELOCATOR_TYPE_SENSOR, (Interface **)&self->bat_remaining);

	/* Discover the piezo beeper advertised by the port, used for key-press feedback. */
	self->beeper = NULL;
	if (iservicelocator_query_name_type(locator, "beeper", ISERVICELOCATOR_TYPE_BEEPER, (Interface **)&self->beeper) != ISERVICELOCATOR_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("beeper not found"));
	}

	/* Discover the inductive keypad event source advertised by the port. It is handed to the GUI
	 * window manager, which is the sole consumer of its events (the keypad delivers each event to a
	 * single listener, so the app must not listen on it in parallel). */
	self->keypad = NULL;
	if (iservicelocator_query_name_type(locator, "keypad", ISERVICELOCATOR_TYPE_EVENT, (Interface **)&self->keypad) != ISERVICELOCATOR_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("keypad event source not found"));
	}

	/* Discover the LCD framebuffer advertised by the port and bring up the windowed UI on it. */
	self->lcd = NULL;
	if (iservicelocator_query_name_type(locator, "lcd", ISERVICELOCATOR_TYPE_FB, (Interface **)&self->lcd) == ISERVICELOCATOR_RET_OK) {
		app_setup_ui(self);
	} else {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("LCD framebuffer not found"));
	}

	xTaskCreate(app_task, "app", configMINIMAL_STACK_SIZE + 128, (void *)self, 1, &(self->task));
	if (self->task == NULL) {
		return APP_RET_FAILED;
	}

	return APP_RET_OK;
}


app_ret_t app_free(App *self) {
	(void)self;
	return APP_RET_OK;
}
