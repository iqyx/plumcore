/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Wren module: applet arguments (`args`)
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 *
 * Preamble module exposing the running applet's arguments as a globally accessible `args` instance of
 * the `Args` class. It surfaces the applet name and help text and, through the same instance-per-
 * resource pattern, hands out the wrapper instances for the applet's other arguments: `args.logger`
 * (a Logger wrapping the applet's log buffer), `args.fb` (an Fb wrapping the applet's framebuffer) and
 * `args.event` (an Event wrapping the applet's input event source). Each wrapper class lives in its own
 * module (see the logger, fb and event modules); Args just wires the instances up so a script reaches
 * everything through `args`.
 */

#include <stdbool.h>
#include <string.h>

#include "wren.h"
#include "wren-module.h"


/* Args.name getter: the running applet's name. */
static void wren_module_args_name(WrenVM *vm) {
	wrenSetSlotString(vm, 0, wren_module_state(vm)->applet->name);
}


/* Args.help getter: the running applet's help text (empty string when the applet has none). */
static void wren_module_args_help(WrenVM *vm) {
	const char *help = wren_module_state(vm)->applet->help;
	wrenSetSlotString(vm, 0, help != NULL ? help : "");
}


static WrenForeignMethodFn wren_module_args_bind_method(const char *class_name, bool is_static,
    const char *signature) {
	if (!strcmp(class_name, "Args") && !is_static) {
		if (!strcmp(signature, "name")) {
			return wren_module_args_name;
		}
		if (!strcmp(signature, "help")) {
			return wren_module_args_help;
		}
	}
	return NULL;
}


const struct wren_module wren_module_args = {
	.name = NULL,
	.source =
		"class Args {\n"
		"  construct new() {\n"
		"    _logger = Logger.new()\n"
		"    _fb = Fb.new()\n"
		"    _event = Event.new()\n"
		"  }\n"
		"  foreign name\n"
		"  foreign help\n"
		"  logger { _logger }\n"
		"  fb { _fb }\n"
		"  event { _event }\n"
		"}\n"
		"var args = Args.new()\n",
	.bind_method = wren_module_args_bind_method,
	.bind_class = NULL,
};
