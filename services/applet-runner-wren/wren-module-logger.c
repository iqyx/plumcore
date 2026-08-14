/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Wren module: logging (`Logger`)
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 *
 * Preamble module exposing a `Logger` foreign class, a thin wrapper around a single log buffer
 * instance (like the C u_log() calls, which operate on a specific `struct log_cbuffer`, not a global).
 * The applet does not construct one directly; it receives the instance wrapping its own log buffer
 * through `args.logger`. The instance methods write a log line at the usual severities, each tagged
 * with the applet name.
 */

#include <stdbool.h>
#include <string.h>

#include "wren.h"
#include "wren-module.h"

/* The foreign data stored with a Logger instance: a borrowed pointer to the log buffer it writes to. */
struct wren_module_logger {
	struct log_cbuffer *log;
};


/* Logger.new(): wrap the running applet's own log buffer (args->logger). The constructor takes no
 * arguments; the buffer always comes from the applet's arguments. */
static void wren_module_logger_allocate(WrenVM *vm) {
	struct wren_module_logger *self = wrenSetSlotNewForeign(vm, 0, 0, sizeof(*self));
	self->log = wren_module_state(vm)->args->logger;
}


/* Shared body of the four severity methods: write slot-1's message to the wrapped log buffer. */
static void wren_module_logger_write(WrenVM *vm, uint8_t type) {
	struct wren_module_logger *self = wrenGetSlotForeign(vm, 0);
	wren_module_log(self->log, wren_module_state(vm)->applet->name, type, wrenGetSlotString(vm, 1));
}


static void wren_module_logger_info(WrenVM *vm) {
	wren_module_logger_write(vm, LOG_TYPE_INFO);
}


static void wren_module_logger_warn(WrenVM *vm) {
	wren_module_logger_write(vm, LOG_TYPE_WARN);
}


static void wren_module_logger_error(WrenVM *vm) {
	wren_module_logger_write(vm, LOG_TYPE_ERROR);
}


static void wren_module_logger_debug(WrenVM *vm) {
	wren_module_logger_write(vm, LOG_TYPE_DEBUG);
}


static WrenForeignMethodFn wren_module_logger_bind_method(const char *class_name, bool is_static,
    const char *signature) {
	if (!strcmp(class_name, "Logger") && !is_static) {
		if (!strcmp(signature, "info(_)")) {
			return wren_module_logger_info;
		}
		if (!strcmp(signature, "warn(_)")) {
			return wren_module_logger_warn;
		}
		if (!strcmp(signature, "error(_)")) {
			return wren_module_logger_error;
		}
		if (!strcmp(signature, "debug(_)")) {
			return wren_module_logger_debug;
		}
	}
	return NULL;
}


static bool wren_module_logger_bind_class(const char *class_name, WrenForeignClassMethods *methods) {
	if (!strcmp(class_name, "Logger")) {
		methods->allocate = wren_module_logger_allocate;
		methods->finalize = NULL;
		return true;
	}
	return false;
}


const struct wren_module wren_module_logger = {
	.name = NULL,
	.source =
		"foreign class Logger {\n"
		"  construct new() {}\n"
		"  foreign info(msg)\n"
		"  foreign warn(msg)\n"
		"  foreign error(msg)\n"
		"  foreign debug(msg)\n"
		"}\n",
	.bind_method = wren_module_logger_bind_method,
	.bind_class = wren_module_logger_bind_class,
};
