/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Wren (interpreted) applet runner service
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 *
 * Runs Wren (interpreted) applets. This is the Wren sibling of the native applet runner: it takes an
 * Applet carrying a Wren source string, sets up the environment the applet expects and interprets the
 * source on an embedded Wren VM.
 *
 * Each applet_runner_wren_run() spawns a dedicated task and returns immediately, so any number of
 * applets can run at once through a single runner. The task owns everything it needs on its own stack:
 * it creates the applet's window on the configured compositor, populates the applet arguments (stdio,
 * logger, framebuffer, event source) and creates a Wren VM.
 *
 * The native capabilities exposed to a script are provided by a registry of pluggable Wren modules
 * (see wren-module.h), each a self-contained C file binding one Wren class to the system. Preamble
 * modules (`args`, `Logger`) are injected into the applet's own module and are always available;
 * named modules (`fb`, ...) are served on demand when the applet imports them. This runner is only the
 * glue: it hands the module sources to the VM and routes Wren's foreign-method/class/module callbacks
 * to the owning module. When the script finishes, the task frees the VM, releases everything it
 * allocated and deletes itself.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "u_log.h"
#include "u_assert.h"

#include <interfaces/applet.h>
#include <interfaces/window.h>
#include <services/fb-compositor/fb-compositor.h>

#include "wren.h"
#include "wren-module.h"
#include "applet-runner-wren.h"

#define MODULE_NAME "applet-runner-wren"

/* The Wren compiler nests a large Compiler struct on the C stack for every scope; even with the lib's
 * lowered MAX_LOCALS/MAX_UPVALUES (see lib/wren/SConscript) a small applet plus the glue modules need
 * a generous stack next to the VM heap. Mirror the footprint used by the script-wren PoC. */
#define APPLET_RUNNER_WREN_TASK_STACK (configMINIMAL_STACK_SIZE + 2500)

/* Modules glued into every applet VM. Modules with a NULL name form the always-available preamble
 * injected into the applet's main module (their classes and top-level variables need no import); named
 * modules are served to Wren on demand when the applet imports them. Preamble modules are concatenated
 * in this order, so a module may rely on classes declared by an earlier one (the Args module wires
 * args.logger and args.fb to a Logger and an Fb, hence both precede args). Add a capability by dropping
 * a wren-module-<name>.c file and listing its descriptor here. */
extern const struct wren_module wren_module_logger;
extern const struct wren_module wren_module_fb;
extern const struct wren_module wren_module_event;
#if defined(CONFIG_SERVICE_FB_PAINTER)
extern const struct wren_module wren_module_painter;
extern const struct wren_module wren_module_fb_painter;
#endif
extern const struct wren_module wren_module_args;

static const struct wren_module * const applet_runner_wren_modules[] = {
	&wren_module_logger,
	&wren_module_fb,
	&wren_module_event,
#if defined(CONFIG_SERVICE_FB_PAINTER)
	&wren_module_painter,
	&wren_module_fb_painter,
#endif
	&wren_module_args,
};
#define APPLET_RUNNER_WREN_MODULE_COUNT (sizeof(applet_runner_wren_modules) / sizeof(applet_runner_wren_modules[0]))

/* Handed to a freshly created applet task. Allocated by run(), freed by the task once it has read it. */
struct applet_runner_wren_run_ctx {
	AppletRunnerWren *self;
	Applet *applet;
};


/* The Wren module a registry module's classes live in: a named module uses its own name, a preamble
 * module is compiled into the applet's "main" module. */
static const char *applet_runner_wren_wren_module(const struct wren_module *mod) {
	return mod->name != NULL ? mod->name : "main";
}


/* Resolve a foreign method by delegating to the module owning the Wren module it was declared in. */
static WrenForeignMethodFn applet_runner_wren_bind_method(WrenVM *vm, const char *module,
    const char *class_name, bool is_static, const char *signature) {
	(void)vm;
	for (size_t i = 0; i < APPLET_RUNNER_WREN_MODULE_COUNT; i++) {
		if (applet_runner_wren_modules[i]->bind_method == NULL ||
		    strcmp(module, applet_runner_wren_wren_module(applet_runner_wren_modules[i]))) {
			continue;
		}
		WrenForeignMethodFn fn = applet_runner_wren_modules[i]->bind_method(class_name, is_static, signature);
		if (fn != NULL) {
			return fn;
		}
	}
	return NULL;
}


/* Resolve a foreign class the same way. */
static WrenForeignClassMethods applet_runner_wren_bind_class(WrenVM *vm, const char *module,
    const char *class_name) {
	(void)vm;
	WrenForeignClassMethods methods = {NULL, NULL};
	for (size_t i = 0; i < APPLET_RUNNER_WREN_MODULE_COUNT; i++) {
		if (applet_runner_wren_modules[i]->bind_class == NULL ||
		    strcmp(module, applet_runner_wren_wren_module(applet_runner_wren_modules[i]))) {
			continue;
		}
		if (applet_runner_wren_modules[i]->bind_class(class_name, &methods)) {
			break;
		}
	}
	return methods;
}


/* Serve a named module's source when the applet imports it. Preamble modules are not importable; they
 * are injected into the main module by applet_runner_wren_build_source() instead. */
static WrenLoadModuleResult applet_runner_wren_load_module(WrenVM *vm, const char *name) {
	(void)vm;
	WrenLoadModuleResult result = {0};
	for (size_t i = 0; i < APPLET_RUNNER_WREN_MODULE_COUNT; i++) {
		if (applet_runner_wren_modules[i]->name != NULL &&
		    !strcmp(name, applet_runner_wren_modules[i]->name)) {
			result.source = applet_runner_wren_modules[i]->source;
			break;
		}
	}
	return result;
}


/* Wren routes the output of System.print() through this callback. */
static void applet_runner_wren_write(WrenVM *vm, const char *text) {
	/* System.print() appends its own newline; the logger adds one too, so drop a lone trailing newline. */
	if (!strcmp(text, "\n")) {
		return;
	}
	struct wren_module_run_state *state = wren_module_state(vm);
	wren_module_log(state->args->logger, state->applet->name, LOG_TYPE_INFO, text);
}


static void applet_runner_wren_error(WrenVM *vm, WrenErrorType type, const char *module, int line,
    const char *msg) {
	struct wren_module_run_state *state = wren_module_state(vm);
	switch (type) {
		case WREN_ERROR_COMPILE:
			u_log(state->args->logger, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("%s: %s line %d: %s"),
			      state->applet->name, module, line, msg);
			break;
		case WREN_ERROR_STACK_TRACE:
			u_log(state->args->logger, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("%s: %s line %d: in %s"),
			      state->applet->name, module, line, msg);
			break;
		case WREN_ERROR_RUNTIME:
			u_log(state->args->logger, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("%s: runtime error: %s"),
			      state->applet->name, msg);
			break;
		default:
			break;
	}
}


/* Build the applet's main-module source: every preamble module's source (NULL name) in registry order
 * followed by the applet's own source, so the script sees the always-available glue (`args`, `Logger`,
 * ...) as top-level declarations. Caller frees the returned buffer. */
static char *applet_runner_wren_build_source(Applet *applet) {
	size_t len = strlen(applet->executable.wren.source) + 1;
	for (size_t i = 0; i < APPLET_RUNNER_WREN_MODULE_COUNT; i++) {
		if (applet_runner_wren_modules[i]->name == NULL) {
			len += strlen(applet_runner_wren_modules[i]->source);
		}
	}
	char *source = malloc(len);
	if (source == NULL) {
		return NULL;
	}
	source[0] = '\0';
	for (size_t i = 0; i < APPLET_RUNNER_WREN_MODULE_COUNT; i++) {
		if (applet_runner_wren_modules[i]->name == NULL) {
			strcat(source, applet_runner_wren_modules[i]->source);
		}
	}
	strcat(source, applet->executable.wren.source);
	return source;
}


/* Compile and run the applet source on a fresh VM, with the preamble modules prepended and the named
 * modules available for import through the runner's module registry. */
static void applet_runner_wren_interpret(Applet *applet, struct applet_args *args) {
	struct wren_module_run_state state = {
		.applet = applet,
		.args = args,
	};

	WrenConfiguration config;
	wrenInitConfiguration(&config);
	config.writeFn = applet_runner_wren_write;
	config.errorFn = applet_runner_wren_error;
	config.bindForeignMethodFn = applet_runner_wren_bind_method;
	config.bindForeignClassFn = applet_runner_wren_bind_class;
	config.loadModuleFn = applet_runner_wren_load_module;
	config.userData = &state;

	/* The desktop defaults would let the VM eat the whole MCU heap before the first collection; keep
	 * the same tight GC tuning as the script-wren PoC so the collector runs early and often. */
	config.initialHeapSize = 16 * 1024;
	config.minHeapSize = 8 * 1024;
	config.heapGrowthPercent = 25;

	WrenVM *vm = wrenNewVM(&config);
	if (vm == NULL) {
		u_log(args->logger, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create the Wren VM"));
		return;
	}

	char *source = applet_runner_wren_build_source(applet);
	if (source == NULL) {
		u_log(args->logger, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot allocate the applet source"));
		wrenFreeVM(vm);
		return;
	}

	wrenInterpret(vm, "main", source);

	free(source);
	wrenFreeVM(vm);
}


static void applet_runner_wren_task(void *p) {
	struct applet_runner_wren_run_ctx *ctx = (struct applet_runner_wren_run_ctx *)p;
	AppletRunnerWren *self = ctx->self;
	Applet *applet = ctx->applet;
	free(ctx);

	/* Every per-run resource lives on this task's own stack (the args and the window handle) so that
	 * concurrent applet tasks never share runner state. */
	struct applet_args args = {0};
	args.stdio = self->conf.stdio;
	args.logger = self->conf.logger;

	/* With a compositor the applet gets its own window and draws into the window's framebuffer while
	 * receiving input through the window's event source. Without one it falls back to the framebuffer
	 * and event source configured directly on the runner. */
	Window *window = NULL;
	if (self->conf.compositor != NULL) {
		if (self->conf.compositor->factory.vmt->create(&self->conf.compositor->factory,
		    &self->conf.geometry, &window) != WINDOW_RET_OK) {
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create the applet window"));
			vTaskDelete(NULL);
			return;
		}
		window->vmt->set_title(window, applet->name);
		window->vmt->set_icon(window, applet->icon);
		window->vmt->get_fb(window, &args.fb);
		window->vmt->get_event(window, &args.event);
		args.window = window;
		window->vmt->to_front(window);
		window->vmt->show(window, true);
	} else {
		args.fb = self->conf.fb;
		args.event = self->conf.event;
	}

	applet_runner_wren_interpret(applet, &args);

	/* The script has finished, release everything allocated for this run and kill the task. */
	if (window != NULL) {
		self->conf.compositor->factory.vmt->destroy(&self->conf.compositor->factory, window);
	}
	vTaskDelete(NULL);
}


applet_runner_wren_ret_t applet_runner_wren_init(AppletRunnerWren *self, const struct applet_runner_wren_conf *conf) {
	if (u_assert(self != NULL) ||
	    u_assert(conf != NULL)) {
		return APPLET_RUNNER_WREN_RET_NULL;
	}
	memset(self, 0, sizeof(AppletRunnerWren));
	memcpy(&self->conf, conf, sizeof(struct applet_runner_wren_conf));

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("init ok"));
	return APPLET_RUNNER_WREN_RET_OK;
}


applet_runner_wren_ret_t applet_runner_wren_free(AppletRunnerWren *self) {
	if (u_assert(self != NULL)) {
		return APPLET_RUNNER_WREN_RET_FAILED;
	}

	/* Running applet tasks are independent and reference the runner configuration, so the caller must
	 * ensure no applet is still running before freeing the runner. */
	return APPLET_RUNNER_WREN_RET_OK;
}


applet_runner_wren_ret_t applet_runner_wren_run(AppletRunnerWren *self, Applet *applet) {
	if (u_assert(self != NULL) ||
	    u_assert(applet != NULL)) {
		return APPLET_RUNNER_WREN_RET_NULL;
	}

	/* This runner only executes Wren applets; leave anything else for another runner. */
	if (applet->type != APPLET_TYPE_WREN) {
		return APPLET_RUNNER_WREN_RET_CANNOT_RUN;
	}
	if (applet->executable.wren.source == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("applet '%s' carries no Wren source"), applet->name);
		return APPLET_RUNNER_WREN_RET_FAILED;
	}

	/* The task outlives this call and reads its context once, then frees it. */
	struct applet_runner_wren_run_ctx *ctx = malloc(sizeof(*ctx));
	if (ctx == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot allocate the run context"));
		return APPLET_RUNNER_WREN_RET_FAILED;
	}
	ctx->self = self;
	ctx->applet = applet;

	if (xTaskCreate(applet_runner_wren_task, "applet-wren", APPLET_RUNNER_WREN_TASK_STACK, (void *)ctx, 1, NULL) != pdPASS) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create the applet task"));
		free(ctx);
		return APPLET_RUNNER_WREN_RET_FAILED;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("running applet '%s'"), applet->name);
	return APPLET_RUNNER_WREN_RET_OK;
}
