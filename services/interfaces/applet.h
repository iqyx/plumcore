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
#include "system_log.h"

typedef enum applet_ret {
	APPLET_RET_OK = 0,
	APPLET_RET_FAILED,
	APPLET_RET_NULL,
} applet_ret_t;


struct applet_args {
	Stream *stdio;
	/** @todo logger */
	struct log_cbuffer *logger;
};

typedef struct applet Applet;

struct applet_ex_compiled {
	applet_ret_t (*main)(Applet *self, struct applet_args *args);
};

typedef struct applet {
	union {
		struct applet_ex_compiled compiled;
	} executable;

	const char *name;
	const char *help;

	bool start_thread;
	size_t stack_size;

} Applet;



