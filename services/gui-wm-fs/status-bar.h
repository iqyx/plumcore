/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Status bar component of the full-screen GUI window manager
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

/* How often the status bar samples the active window and repaints, in milliseconds. */
#define STATUS_BAR_PERIOD_MS 100

typedef enum {
	STATUS_BAR_RET_OK = 0,
	STATUS_BAR_RET_FAILED,
	STATUS_BAR_RET_NULL,
	STATUS_BAR_RET_NOMEM,
} status_bar_ret_t;

/* Configuration and dependencies passed to status_bar_init(). Not typedef'd per project policy. */
struct status_bar_conf {
	/** Compositor to create the status bar window on and to sample the active window of. */
	FbCompositor *compositor;
	/** Position and size of the status bar window, in output pixels. */
	struct window_geometry geometry;

	Sensor *bat_soc;
	Sensor *bat_current;
};

typedef struct {
	struct status_bar_conf conf;

	/* Full-width window holding the rendered status bar, plus its painter. The backing store is owned
	 * by the compositor. */
	Window *window;
	FbPainter painter;

	/* Task periodically sampling the active window and repainting the bar whenever it changes.
	 * active_window holds the last sampled window for change detection. */
	TaskHandle_t task;
	volatile bool can_run;
	volatile bool running;
	Window *active_window;
} StatusBar;


status_bar_ret_t status_bar_init(StatusBar *self, const struct status_bar_conf *conf);
status_bar_ret_t status_bar_free(StatusBar *self);
