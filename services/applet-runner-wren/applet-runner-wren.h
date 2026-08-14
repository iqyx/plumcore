/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Wren (interpreted) applet runner service
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

#include <interfaces/stream.h>
#include <interfaces/fb.h>
#include <interfaces/event.h>
#include <interfaces/window.h>
#include <interfaces/applet.h>
#include <services/fb-compositor/fb-compositor.h>

typedef enum {
	APPLET_RUNNER_WREN_RET_OK = 0,
	APPLET_RUNNER_WREN_RET_FAILED,
	APPLET_RUNNER_WREN_RET_NULL,
	APPLET_RUNNER_WREN_RET_CANNOT_RUN,
} applet_runner_wren_ret_t;

/* Service configuration passed to applet_runner_wren_init(). Not typedef'd per project policy. */
struct applet_runner_wren_conf {
	/** Stream handed to the applet as its stdin/stdout for user interaction. */
	Stream *stdio;
	/** Log buffer the applet may write to (also backs the Wren Log wrapper). */
	struct log_cbuffer *logger;
	/** Compositor used to create a window for the applet. NULL runs the applet windowless. */
	FbCompositor *compositor;
	/** Framebuffer handed to the applet when no compositor is used (direct framebuffer access). */
	Fb *fb;
	/** Input event source handed to the applet when no compositor is used (direct event access). */
	Event *event;
	/** Preferred geometry of the applet window created on the compositor, in output pixels. */
	struct window_geometry geometry;
};

typedef struct {
	struct applet_runner_wren_conf conf;
} AppletRunnerWren;


applet_runner_wren_ret_t applet_runner_wren_init(AppletRunnerWren *self, const struct applet_runner_wren_conf *conf);
applet_runner_wren_ret_t applet_runner_wren_free(AppletRunnerWren *self);

/* Run a Wren (interpreted) applet. Returns CANNOT_RUN if the applet is not a Wren applet, leaving it
 * for another runner to handle. Otherwise spawns a dedicated task in which the applet's window is
 * created on the compositor (using the preferred geometry), the applet arguments are populated and a
 * Wren VM is created. A small glue preamble exposes the arguments as a globally accessible `args`
 * instance and a `Log` wrapper around u_log; the applet source is then compiled and run on top of it.
 * Returns as soon as the task is started, so several applets may run concurrently through the same
 * runner. When the script finishes, its task frees the VM and every resource it allocated and deletes
 * itself. */
applet_runner_wren_ret_t applet_runner_wren_run(AppletRunnerWren *self, Applet *applet);
