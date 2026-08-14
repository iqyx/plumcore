/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Native (compiled) applet runner service
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 *
 * Runs native (compiled-in) applets. Historically applets were invoked directly from the CLI; this
 * runner is one of a family of runners (native, JavaScript, Wren, ...) that instead take an Applet,
 * set up the environment it expects and launch it.
 *
 * Each applet_runner_native_run() spawns a dedicated task and returns immediately, so any number of
 * applets can run at once through a single runner. The task owns everything it needs on its own
 * stack: it creates the applet's window on the configured compositor using the preferred geometry,
 * hands the applet the window's framebuffer and input event source (or, when no compositor is
 * configured, the framebuffer and event source configured directly on the runner) together with a
 * stdio stream and a logger, and calls the applet main. When the applet main returns, the task
 * releases everything it allocated and deletes itself.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "u_log.h"
#include "u_assert.h"

#include <interfaces/applet.h>
#include <interfaces/window.h>
#include <services/fb-compositor/fb-compositor.h>

#include "applet-runner-native.h"

#define MODULE_NAME "applet-runner-native"

#define APPLET_RUNNER_NATIVE_DEFAULT_APPLET_STACK (configMINIMAL_STACK_SIZE + 128)
#define APPLET_RUNNER_NATIVE_STACK_OVERHEAD 128

/* Handed to a freshly created applet task. Allocated by run(), freed by the task once it has read it. */
struct applet_runner_native_run_ctx {
	AppletRunnerNative *self;
	Applet *applet;
};


static void applet_runner_native_task(void *p) {
	struct applet_runner_native_run_ctx *ctx = (struct applet_runner_native_run_ctx *)p;
	AppletRunnerNative *self = ctx->self;
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

	applet_ret_t ret = APPLET_RET_FAILED;
	if (applet->executable.native.main != NULL) {
		ret = applet->executable.native.main(applet, &args);
	}
	if (ret != APPLET_RET_OK) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("applet '%s' returned %d"), applet->name, ret);
	}

	/* The applet main has returned, release everything allocated for this run and kill the task. */
	if (window != NULL) {
		self->conf.compositor->factory.vmt->destroy(&self->conf.compositor->factory, window);
	}
	vTaskDelete(NULL);
}


applet_runner_native_ret_t applet_runner_native_init(AppletRunnerNative *self, const struct applet_runner_native_conf *conf) {
	if (u_assert(self != NULL) ||
	    u_assert(conf != NULL)) {
		return APPLET_RUNNER_NATIVE_RET_NULL;
	}
	memset(self, 0, sizeof(AppletRunnerNative));
	memcpy(&self->conf, conf, sizeof(struct applet_runner_native_conf));

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("init ok"));
	return APPLET_RUNNER_NATIVE_RET_OK;
}


applet_runner_native_ret_t applet_runner_native_free(AppletRunnerNative *self) {
	if (u_assert(self != NULL)) {
		return APPLET_RUNNER_NATIVE_RET_FAILED;
	}

	/* Running applet tasks are independent and reference the runner configuration, so the caller must
	 * ensure no applet is still running before freeing the runner. */
	return APPLET_RUNNER_NATIVE_RET_OK;
}


applet_runner_native_ret_t applet_runner_native_run(AppletRunnerNative *self, Applet *applet) {
	if (u_assert(self != NULL) ||
	    u_assert(applet != NULL)) {
		return APPLET_RUNNER_NATIVE_RET_NULL;
	}

	/* This runner only executes native applets; leave anything else for another runner. */
	if (applet->type != APPLET_TYPE_NATIVE) {
		return APPLET_RUNNER_NATIVE_RET_CANNOT_RUN;
	}

	/* The task outlives this call and reads its context once, then frees it. */
	struct applet_runner_native_run_ctx *ctx = malloc(sizeof(*ctx));
	if (ctx == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot allocate the run context"));
		return APPLET_RUNNER_NATIVE_RET_FAILED;
	}
	ctx->self = self;
	ctx->applet = applet;

	/* Size the task stack for the applet itself plus the runner's own runtime data living on top. */
	uint16_t stack = (applet->stack_size ? applet->stack_size : APPLET_RUNNER_NATIVE_DEFAULT_APPLET_STACK) +
	                 APPLET_RUNNER_NATIVE_STACK_OVERHEAD;
	if (xTaskCreate(applet_runner_native_task, "applet-native", stack, (void *)ctx, 1, NULL) != pdPASS) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create the applet task"));
		free(ctx);
		return APPLET_RUNNER_NATIVE_RET_FAILED;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("running applet '%s'"), applet->name);
	return APPLET_RUNNER_NATIVE_RET_OK;
}
