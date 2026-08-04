/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Window list component of the full-screen GUI window manager
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include <interfaces/window.h>
#include <services/fb-compositor/fb-compositor.h>
#include <services/fb-painter/fb-painter.h>

/* How long the window list stays on screen after being shown, in milliseconds. */
#define WINDOW_LIST_SHOW_MS 3000

/* Upper bound on the number of windows shown at once. */
#define WINDOW_LIST_MAX_WINDOWS 16

typedef enum {
	WINDOW_LIST_RET_OK = 0,
	WINDOW_LIST_RET_FAILED,
	WINDOW_LIST_RET_NULL,
	WINDOW_LIST_RET_NOMEM,
} window_list_ret_t;

/* Configuration and dependencies passed to window_list_init(). Not typedef'd per project policy. */
struct window_list_conf {
	/** Compositor to create the list window on and to enumerate the windows of. */
	FbCompositor *compositor;
	/** Position and size of the list window, in output pixels. */
	struct window_geometry geometry;
};

#define WINDOW_LIST_MAX 16

typedef struct {
	struct window_list_conf conf;

	/* Full-screen window holding the rendered list, plus its painter. The backing store is owned by
	 * the compositor. The window is kept hidden and only shown on request. */
	Window *window;
	FbPainter painter;

	/* Task showing the list on request; show_req signals it to render, show and auto-hide. */
	TaskHandle_t task;
	SemaphoreHandle_t show_req;
	volatile bool can_run;
	volatile bool running;

	/* Window list filled on show. */
	Window *windows[WINDOW_LIST_MAX];
	size_t window_count;
	size_t window_list_offset;
	size_t window_selected;
} WindowList;


window_list_ret_t window_list_init(WindowList *self, const struct window_list_conf *conf);
window_list_ret_t window_list_free(WindowList *self);

/* Request the window list to be rendered, shown for WINDOW_LIST_SHOW_MS and then hidden again. */
window_list_ret_t window_list_show(WindowList *self);
