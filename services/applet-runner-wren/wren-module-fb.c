/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Wren module: framebuffer (`Fb`)
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 *
 * Preamble module exposing the framebuffer interface as an `Fb` foreign class. Like the logger, the
 * applet does not construct one directly; it receives the instance wrapping its own framebuffer
 * (args->fb, the compositor window's framebuffer when running windowed) through `args.fb`. Thin for
 * now: it reports the surface geometry and native mode and flushes pending output.
 */

#include <stdbool.h>
#include <string.h>

#include "wren.h"
#include "wren-module.h"
#include "wren-module-fb.h"

#include <interfaces/fb.h>

/* The foreign data stored with an Fb instance: a borrowed pointer to the applet's framebuffer. */
struct wren_module_fb {
	Fb *fb;
};


Fb *wren_module_fb_unwrap(WrenVM *vm, int slot) {
	return ((struct wren_module_fb *)wrenGetSlotForeign(vm, slot))->fb;
}


/* Fb.new(): wrap the running applet's framebuffer (args->fb). */
static void wren_module_fb_allocate(WrenVM *vm) {
	struct wren_module_fb *self = wrenSetSlotNewForeign(vm, 0, 0, sizeof(*self));
	self->fb = wren_module_state(vm)->args->fb;
}


/* Read the framebuffer geometry/mode once; returns zeroed stat when there is no framebuffer. */
static struct fb_stat wren_module_fb_stat(WrenVM *vm) {
	struct wren_module_fb *self = wrenGetSlotForeign(vm, 0);
	struct fb_stat stat = {0};
	if (self->fb != NULL) {
		self->fb->vmt->stat(self->fb, &stat);
	}
	return stat;
}


static void wren_module_fb_width(WrenVM *vm) {
	wrenSetSlotDouble(vm, 0, (double)wren_module_fb_stat(vm).w);
}


static void wren_module_fb_height(WrenVM *vm) {
	wrenSetSlotDouble(vm, 0, (double)wren_module_fb_stat(vm).h);
}


static void wren_module_fb_mode(WrenVM *vm) {
	wrenSetSlotDouble(vm, 0, (double)wren_module_fb_stat(vm).mode);
}


static void wren_module_fb_flush(WrenVM *vm) {
	struct wren_module_fb *self = wrenGetSlotForeign(vm, 0);
	if (self->fb != NULL) {
		self->fb->vmt->flush(self->fb);
	}
}


static WrenForeignMethodFn wren_module_fb_bind_method(const char *class_name, bool is_static,
    const char *signature) {
	if (!strcmp(class_name, "Fb") && !is_static) {
		if (!strcmp(signature, "width")) {
			return wren_module_fb_width;
		}
		if (!strcmp(signature, "height")) {
			return wren_module_fb_height;
		}
		if (!strcmp(signature, "mode")) {
			return wren_module_fb_mode;
		}
		if (!strcmp(signature, "flush()")) {
			return wren_module_fb_flush;
		}
	}
	return NULL;
}


static bool wren_module_fb_bind_class(const char *class_name, WrenForeignClassMethods *methods) {
	if (!strcmp(class_name, "Fb")) {
		methods->allocate = wren_module_fb_allocate;
		methods->finalize = NULL;
		return true;
	}
	return false;
}


const struct wren_module wren_module_fb = {
	.name = NULL,
	.source =
		"foreign class Fb {\n"
		"  construct new() {}\n"
		"  foreign width\n"
		"  foreign height\n"
		"  foreign mode\n"
		"  foreign flush()\n"
		"}\n",
	.bind_method = wren_module_fb_bind_method,
	.bind_class = wren_module_fb_bind_class,
};
