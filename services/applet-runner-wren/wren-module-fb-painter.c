/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Wren module: framebuffer painter (`FbPainter`)
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 *
 * Importable module (`import "fb-painter" for FbPainter`) wrapping the fb-painter service. Mirrors the
 * C usage exactly: `FbPainter.new(fb)` initialises a painter on a framebuffer (like
 * fb_painter_init(&fbp, fb)) and its `.painter` getter hands out the bound Painter interface (like
 * taking &fbp.painter). Scripts draw through that Painter (see the painter module). The FbPainter owns
 * the painter's resources, freed by the finalizer; the borrowed Painter must not outlive it.
 */

#include "config.h"

#if defined(CONFIG_SERVICE_FB_PAINTER)

#include <stdbool.h>
#include <string.h>

#include "wren.h"
#include "wren-module.h"
#include "wren-module-fb.h"
#include "wren-module-painter.h"

#include <services/fb-painter/fb-painter.h>

/* The foreign data stored with an FbPainter instance: the concrete painter and whether it initialised.
 * The painter is embedded, so the Wren object owns it and the finalizer releases it. */
struct wren_module_fb_painter {
	FbPainter fb_painter;
	bool ready;
};


/* FbPainter.new(fb): initialise a painter on the framebuffer wrapped by the Fb argument. */
static void wren_module_fb_painter_allocate(WrenVM *vm) {
	Fb *fb = wren_module_fb_unwrap(vm, 1);
	struct wren_module_fb_painter *self = wrenSetSlotNewForeign(vm, 0, 0, sizeof(*self));
	memset(self, 0, sizeof(*self));
	self->ready = fb_painter_init(&self->fb_painter, fb) == FB_PAINTER_RET_OK;
}


static void wren_module_fb_painter_finalize(void *data) {
	fb_painter_free(&((struct wren_module_fb_painter *)data)->fb_painter);
}


/* FbPainter.painter getter: return the bound Painter interface (C's &self->painter), or null when the
 * painter failed to initialise. */
static void wren_module_fb_painter_painter(WrenVM *vm) {
	struct wren_module_fb_painter *self = wrenGetSlotForeign(vm, 0);
	if (!self->ready) {
		wrenSetSlotNull(vm, 0);
		return;
	}
	wren_module_painter_wrap(vm, &self->fb_painter.painter);
}


static WrenForeignMethodFn wren_module_fb_painter_bind_method(const char *class_name, bool is_static,
    const char *signature) {
	if (!strcmp(class_name, "FbPainter") && !is_static && !strcmp(signature, "painter")) {
		return wren_module_fb_painter_painter;
	}
	return NULL;
}


static bool wren_module_fb_painter_bind_class(const char *class_name, WrenForeignClassMethods *methods) {
	if (!strcmp(class_name, "FbPainter")) {
		methods->allocate = wren_module_fb_painter_allocate;
		methods->finalize = wren_module_fb_painter_finalize;
		return true;
	}
	return false;
}


const struct wren_module wren_module_fb_painter = {
	.name = "fb-painter",
	.source =
		"import \"painter\" for Painter\n"
		"foreign class FbPainter {\n"
		"  construct new(fb) {}\n"
		"  foreign painter\n"
		"}\n",
	.bind_method = wren_module_fb_painter_bind_method,
	.bind_class = wren_module_fb_painter_bind_class,
};

#endif
