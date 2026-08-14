/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Pluggable Wren module interface for the Wren applet runner
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 *
 * A Wren module bundles a piece of Wren source (class declarations with `foreign` members) with the C
 * bindings backing those members, so a native capability can be exposed to interpreted applets as an
 * ordinary Wren class. Each module lives in its own C file exporting a single `const struct wren_module`
 * descriptor; the applet runner keeps a registry of them and glues them into every applet VM.
 *
 * Two flavours exist, distinguished by the descriptor's `name`:
 * - a preamble module (name == NULL) has its source injected into the applet's own `main` module ahead
 *   of the applet source, so its classes and top-level variables (e.g. `args`, `Log`) are always
 *   available without an import;
 * - a named module is served to Wren on demand when the applet does `import "<name>" for ...`.
 *
 * Foreign methods reach the running applet and its arguments (framebuffer, event source, logger, ...)
 * through the per-run state stored in the VM user data; use wren_module_state() to get at it.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "u_log.h"
#include "wren.h"
#include <interfaces/applet.h>

/* Per-run state reachable from every foreign method through the VM user data. Owned by the runner for
 * the duration of a single applet run. */
struct wren_module_run_state {
	Applet *applet;
	struct applet_args *args;
};

/* A pluggable Wren module: Wren source plus the C bindings for its foreign members. */
struct wren_module {
	/** Import name (e.g. "fb"), or NULL for a preamble module injected into the applet's main module. */
	const char *name;
	/** Wren source declaring the module's classes with their `foreign` members. */
	const char *source;
	/** Resolve a foreign method declared in this module, or return NULL when the signature is unknown. */
	WrenForeignMethodFn (*bind_method)(const char *class_name, bool is_static, const char *signature);
	/** Resolve a foreign class declared in this module: fill *methods and return true, or return false
	 *  when the class is not one of this module's foreign classes. May be NULL for modules without any. */
	bool (*bind_class)(const char *class_name, WrenForeignClassMethods *methods);
};


/* Reach the per-run state from inside any foreign method or VM callback. */
static inline struct wren_module_run_state *wren_module_state(WrenVM *vm) {
	return (struct wren_module_run_state *)wrenGetUserData(vm);
}


/* Emit one log line to the given log buffer instance, tagged with the applet name so scripts are easy
 * to tell apart in the log. Mirrors the U_LOG_MODULE colouring. The log buffer is the applet's own
 * (args->logger), wrapped by the Logger module and reused by the runner's own VM callbacks. */
static inline void wren_module_log(struct log_cbuffer *log, const char *name, uint8_t type, const char *msg) {
	if (log != NULL) {
		u_log(log, type, "\x1b[33m%s: \x1b[0m%s", name, msg);
	}
}
