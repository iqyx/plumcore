/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GUI applet launcher service
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "config.h"
#include "FreeRTOS.h"
#include "task.h"

#include <interfaces/event.h>
#include <interfaces/window.h>
#include <interfaces/applet.h>
#include <interfaces/conf.h>
#include <configlib.h>
#include <services/fb-compositor/fb-compositor.h>
#include <services/fb-painter/fb-painter.h>
#include <services/applet-runner-native/applet-runner-native.h>
#if defined(CONFIG_SERVICE_APPLET_RUNNER_WREN)
#include <services/applet-runner-wren/applet-runner-wren.h>
#endif

/* Upper bound on the number of applets shown at once. */
#define GUI_LAUNCHER_MAX_APPLETS 32

/* Upper bound on a single applet name, including the terminator. */
#define GUI_LAUNCHER_NAME_MAX 64

typedef enum {
	GUI_LAUNCHER_RET_OK = 0,
	GUI_LAUNCHER_RET_FAILED,
	GUI_LAUNCHER_RET_NULL,
	GUI_LAUNCHER_RET_NOMEM,
} gui_launcher_ret_t;

/* Service configuration passed to gui_launcher_init(). Not typedef'd per project policy. */
struct gui_launcher_conf {
	/** Compositor the launcher window is created on. */
	FbCompositor *compositor;
	/** Position and size of the launcher window, in output pixels. */
	struct window_geometry geometry;
};

typedef struct {
	struct gui_launcher_conf conf;

	/* Window holding the rendered applet list, plus its painter. The backing store is owned by the
	 * compositor. Created on the first gui_launcher_show() and kept hidden between showings. */
	Window *window;
	FbPainter painter;

	/* Runner used to launch the selected native applet. */
	AppletRunnerNative runner;

	/* Runner used to launch the selected Wren applet, when the Wren runner is built in. */
	#if defined(CONFIG_SERVICE_APPLET_RUNNER_WREN)
	AppletRunnerWren wren_runner;
	#endif

	/* Task running the key event loop; started on the first gui_launcher_show(). */
	TaskHandle_t task;
	volatile bool can_run;
	volatile bool running;

	/* Applet list captured on show, the currently selected entry and the scroll offsets (in pixels) the
	 * grid is drawn shifted left and up by, so the selected cell can stay visible. */
	Applet *applets[GUI_LAUNCHER_MAX_APPLETS];
	size_t applet_count;
	size_t selected;
	int16_t x_scroll;
	int16_t y_scroll;

	/* Launcher configuration tree, advertised by the application at /service/launcher. Built in conf.c, the
	 * nodes map the values in settings. */
	struct {
		ConfiglibValue launcher;
		ConfiglibValue autostart;
	} conf_tree;

	struct {
		char autostart[GUI_LAUNCHER_NAME_MAX];
	} settings;
} GuiLauncher;


gui_launcher_ret_t gui_launcher_init(GuiLauncher *self, const struct gui_launcher_conf *conf);
gui_launcher_ret_t gui_launcher_free(GuiLauncher *self);

/* Discover the available applets, render them into the launcher window and bring it on screen. The
 * first call also creates the window and starts the event loop task. */
gui_launcher_ret_t gui_launcher_show(GuiLauncher *self);

/* Launch the applet named in the autostart setting, if any. Call once the stored configuration is loaded and
 * the launcher was shown. */
gui_launcher_ret_t gui_launcher_autostart(GuiLauncher *self);

/* Set the launcher settings to their defaults and build the configuration tree (conf.c). */
gui_launcher_ret_t gui_launcher_conf_init(GuiLauncher *self);
