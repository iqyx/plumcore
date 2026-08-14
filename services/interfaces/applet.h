/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Applet interface
 *
 * Copyright (c) 2018-2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @brief Interface for managing and running applets
 *
 * Applet is a part of the system commonly not needed for its usual work. It may be a helper application,
 * a data export application, a resource check script, calibration application, etc.
 *
 * The main differences compared to services, applications and jobs are:
 * - application is a program defining the use case of the hardware, applet is not
 * - service is a long running program monitored by a service manager, applet is not
 * - job is an explicitly defined and prepared action within a service which can be run/paused/resumed
 * - applet has a stream (stdin/stdout) for user interaction, services and applications don't
 * - applet has a logger, services and applications don't (unless they request one)
 * - applet is run on demand, usually by a user (over the network, using the CLI)
 * - applet can be interpreted (ecmascript, Wren)
 * - applet can be advertised using the service locator service
 */

#pragma once

#include <interfaces/stream.h>
#include <interfaces/fb.h>
#include <interfaces/event.h>
#include <interfaces/painter.h>
#include <interfaces/window.h>
#include "system_log.h"

typedef enum applet_ret {
	APPLET_RET_OK = 0,
	APPLET_RET_FAILED,
	APPLET_RET_NULL,
} applet_ret_t;

/* Kind of executable an applet carries, selecting which runner is able to execute it. The native
 * kind is the default (value 0), so an applet that does not set the type explicitly is a native
 * (compiled-in) applet. */
enum applet_type {
	APPLET_TYPE_NATIVE = 0,
	APPLET_TYPE_JS,
	APPLET_TYPE_WREN,
};


struct applet_args {
	Stream *stdio;
	/** @todo logger */
	struct log_cbuffer *logger;

	/** Framebuffer the applet draws its GUI into (optional, may be NULL). */
	Fb *fb;
	/** Input event source delivering key presses to the applet (optional, may be NULL). */
	Event *event;
	/** Window the applet runs in, for title/geometry management (optional, may be NULL when no compositor). */
	Window *window;
};

typedef struct applet Applet;

struct applet_ex_native {
	applet_ret_t (*main)(Applet *self, struct applet_args *args);
};

/* Wren applet: the executable is a Wren source code string compiled and interpreted on demand by the
 * Wren applet runner. */
struct applet_ex_wren {
	const char *source;
};

typedef struct applet {
	enum applet_type type;

	union {
		struct applet_ex_native native;
		struct applet_ex_wren wren;
	} executable;

	const char *name;
	const char *help;

	/** Icon shown for the applet in a GUI launcher (optional, may be NULL). */
	const struct painter_raw_image *icon;

	bool start_thread;
	size_t stack_size;

} Applet;



