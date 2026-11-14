/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Charging window component of the full-screen GUI window manager
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <main.h>

#include <interfaces/window.h>
#include <interfaces/sensor.h>
#include <services/fb-compositor/fb-compositor.h>
#include <services/fb-painter/fb-painter.h>

/* How often the power source voltages are sampled and the window repainted, in milliseconds. */
#define CHARGE_WINDOW_PERIOD_MS 1000

/* How long the window stays on screen after a power source gets connected or disconnected, in milliseconds. */
#define CHARGE_WINDOW_SHOW_MS 10000

/* A power source with its voltage above this threshold is considered connected, in volts. */
#define CHARGE_WINDOW_SOURCE_MIN_V 3.5f

typedef enum {
	CHARGE_WINDOW_RET_OK = 0,
	CHARGE_WINDOW_RET_FAILED,
	CHARGE_WINDOW_RET_NULL,
	CHARGE_WINDOW_RET_NOMEM,
} charge_window_ret_t;

/* Configuration and dependencies passed to charge_window_init(). Not typedef'd per project policy. */
struct charge_window_conf {
	/** Compositor to create the charging window on. */
	FbCompositor *compositor;
	/** Position and size of the charging window, in output pixels. */
	struct window_geometry geometry;

	Sensor *bat_soc;
	Sensor *bat_current;
	/** Battery voltage, remaining and full charge capacity shown below the battery. */
	Sensor *bat_voltage;
	Sensor *bat_remaining;
	Sensor *bat_full;

	/** Voltages of the two charger power sources. The window is shown while any of them is connected. */
	Sensor *source1_voltage;
	Sensor *source2_voltage;
	/** Current drawn from the selected power source. The charger prefers source 1, the current is attributed
	 * to it whenever it is connected and to source 2 otherwise. */
	Sensor *source_current;
};

typedef struct {
	struct charge_window_conf conf;

	/* Window holding the rendered charging information, plus its painter. The backing store is owned by
	 * the compositor. The window is kept hidden until the power source connection changes. */
	Window *window;
	FbPainter painter;

	/* Task periodically sampling the power sources, showing the window when they get connected or disconnected,
	 * hiding it when the show timeout expires and repainting it while shown. connected holds the last sampled
	 * state, show_ms the time remaining until the window is hidden (0 while hidden). */
	TaskHandle_t task;
	volatile bool can_run;
	volatile bool running;
	bool connected;
	volatile int32_t show_ms;

	/* Task listening on the window's own event source, dismissing the window on ESC. */
	TaskHandle_t event_task;
} ChargeWindow;


charge_window_ret_t charge_window_init(ChargeWindow *self, const struct charge_window_conf *conf);
charge_window_ret_t charge_window_free(ChargeWindow *self);
