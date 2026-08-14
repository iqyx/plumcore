/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Wren module: 2D painter interface (`Painter`)
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 *
 * Importable module (`import "painter" for Painter`) binding the 2D painter interface (interfaces/
 * painter.h) as a `Painter` Wren class. Like in C, a Painter is not a drawing target on its own: an
 * instance only borrows a `Painter *` produced elsewhere (e.g. the fb-painter module's `.painter`
 * getter, which hands out its `&self->painter`). Scripts never construct one directly; they receive it
 * from a producer and draw through it. The methods are a thin one-to-one mapping of the painter vmt.
 */

#include "config.h"

#if defined(CONFIG_SERVICE_FB_PAINTER)

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "wren.h"
#include "wren-module.h"
#include "wren-module-painter.h"

#include <interfaces/painter.h>

/* The foreign data stored with a Painter instance: a borrowed pointer to a painter interface. */
struct wren_module_painter {
	Painter *painter;
};


/* Reach the borrowed painter interface, or NULL when the instance wraps none. */
static Painter *wren_module_painter_get(WrenVM *vm) {
	return ((struct wren_module_painter *)wrenGetSlotForeign(vm, 0))->painter;
}


void wren_module_painter_wrap(WrenVM *vm, Painter *painter) {
	wrenEnsureSlots(vm, 2);
	/* The Painter class lives in this module; the caller has imported it, so it is resolvable here. */
	wrenGetVariable(vm, "painter", "Painter", 1);
	((struct wren_module_painter *)wrenSetSlotNewForeign(vm, 0, 1, sizeof(struct wren_module_painter)))->painter =
	    painter;
}


/* Wren never constructs a Painter (it has no constructor in the module source), but a foreign class
 * must still provide an allocator; this one only ever runs if that contract changes, and wraps no
 * interface. */
static void wren_module_painter_allocate(WrenVM *vm) {
	((struct wren_module_painter *)wrenSetSlotNewForeign(vm, 0, 0, sizeof(struct wren_module_painter)))->painter =
	    NULL;
}


static void wren_module_painter_begin(WrenVM *vm) {
	Painter *p = wren_module_painter_get(vm);
	if (p != NULL) {
		p->vmt->begin(p);
	}
}


static void wren_module_painter_end(WrenVM *vm) {
	Painter *p = wren_module_painter_get(vm);
	if (p != NULL) {
		p->vmt->end(p);
	}
}


static void wren_module_painter_set_pen(WrenVM *vm) {
	Painter *p = wren_module_painter_get(vm);
	if (p != NULL) {
		p->vmt->set_pen(p, (painter_color_t)wrenGetSlotDouble(vm, 1), (uint16_t)wrenGetSlotDouble(vm, 2));
	}
}


static void wren_module_painter_set_brush(WrenVM *vm) {
	Painter *p = wren_module_painter_get(vm);
	if (p != NULL) {
		p->vmt->set_brush(p, (painter_color_t)wrenGetSlotDouble(vm, 1));
	}
}


static void wren_module_painter_set_font(WrenVM *vm) {
	Painter *p = wren_module_painter_get(vm);
	if (p != NULL) {
		p->vmt->set_font(p, (painter_font_style_t)wrenGetSlotDouble(vm, 1), NULL);
	}
}


static void wren_module_painter_rect(WrenVM *vm) {
	Painter *p = wren_module_painter_get(vm);
	if (p != NULL) {
		p->vmt->rect(p, (int16_t)wrenGetSlotDouble(vm, 1), (int16_t)wrenGetSlotDouble(vm, 2),
		             (uint16_t)wrenGetSlotDouble(vm, 3), (uint16_t)wrenGetSlotDouble(vm, 4));
	}
}


static void wren_module_painter_circle(WrenVM *vm) {
	Painter *p = wren_module_painter_get(vm);
	if (p != NULL) {
		p->vmt->circle(p, (int16_t)wrenGetSlotDouble(vm, 1), (int16_t)wrenGetSlotDouble(vm, 2),
		               (uint16_t)wrenGetSlotDouble(vm, 3));
	}
}


static void wren_module_painter_ellipse(WrenVM *vm) {
	Painter *p = wren_module_painter_get(vm);
	if (p != NULL) {
		p->vmt->ellipse(p, (int16_t)wrenGetSlotDouble(vm, 1), (int16_t)wrenGetSlotDouble(vm, 2),
		                (uint16_t)wrenGetSlotDouble(vm, 3), (uint16_t)wrenGetSlotDouble(vm, 4));
	}
}


static void wren_module_painter_fill(WrenVM *vm) {
	Painter *p = wren_module_painter_get(vm);
	if (p != NULL) {
		p->vmt->fill(p, (int16_t)wrenGetSlotDouble(vm, 1), (int16_t)wrenGetSlotDouble(vm, 2));
	}
}


static void wren_module_painter_line(WrenVM *vm) {
	Painter *p = wren_module_painter_get(vm);
	if (p != NULL) {
		p->vmt->line(p, (int16_t)wrenGetSlotDouble(vm, 1), (int16_t)wrenGetSlotDouble(vm, 2),
		             (int16_t)wrenGetSlotDouble(vm, 3), (int16_t)wrenGetSlotDouble(vm, 4));
	}
}


static void wren_module_painter_text(WrenVM *vm) {
	Painter *p = wren_module_painter_get(vm);
	if (p != NULL) {
		p->vmt->text(p, (int16_t)wrenGetSlotDouble(vm, 1), (int16_t)wrenGetSlotDouble(vm, 2),
		             wrenGetSlotString(vm, 3));
	}
}


static void wren_module_painter_text_width(WrenVM *vm) {
	Painter *p = wren_module_painter_get(vm);
	uint16_t w = 0;
	if (p != NULL) {
		p->vmt->text_size(p, wrenGetSlotString(vm, 1), &w, NULL);
	}
	wrenSetSlotDouble(vm, 0, (double)w);
}


static void wren_module_painter_text_height(WrenVM *vm) {
	Painter *p = wren_module_painter_get(vm);
	uint16_t h = 0;
	if (p != NULL) {
		p->vmt->text_size(p, wrenGetSlotString(vm, 1), NULL, &h);
	}
	wrenSetSlotDouble(vm, 0, (double)h);
}


static void wren_module_painter_move_to(WrenVM *vm) {
	Painter *p = wren_module_painter_get(vm);
	if (p != NULL) {
		p->vmt->move_to(p, (int16_t)wrenGetSlotDouble(vm, 1), (int16_t)wrenGetSlotDouble(vm, 2));
	}
}


static void wren_module_painter_line_to(WrenVM *vm) {
	Painter *p = wren_module_painter_get(vm);
	if (p != NULL) {
		p->vmt->line_to(p, (int16_t)wrenGetSlotDouble(vm, 1), (int16_t)wrenGetSlotDouble(vm, 2));
	}
}


static WrenForeignMethodFn wren_module_painter_bind_method(const char *class_name, bool is_static,
    const char *signature) {
	if (strcmp(class_name, "Painter") || is_static) {
		return NULL;
	}
	if (!strcmp(signature, "begin()")) {
		return wren_module_painter_begin;
	}
	if (!strcmp(signature, "end()")) {
		return wren_module_painter_end;
	}
	if (!strcmp(signature, "setPen(_,_)")) {
		return wren_module_painter_set_pen;
	}
	if (!strcmp(signature, "setBrush(_)")) {
		return wren_module_painter_set_brush;
	}
	if (!strcmp(signature, "setFont(_)")) {
		return wren_module_painter_set_font;
	}
	if (!strcmp(signature, "rect(_,_,_,_)")) {
		return wren_module_painter_rect;
	}
	if (!strcmp(signature, "circle(_,_,_)")) {
		return wren_module_painter_circle;
	}
	if (!strcmp(signature, "ellipse(_,_,_,_)")) {
		return wren_module_painter_ellipse;
	}
	if (!strcmp(signature, "fill(_,_)")) {
		return wren_module_painter_fill;
	}
	if (!strcmp(signature, "line(_,_,_,_)")) {
		return wren_module_painter_line;
	}
	if (!strcmp(signature, "text(_,_,_)")) {
		return wren_module_painter_text;
	}
	if (!strcmp(signature, "textWidth(_)")) {
		return wren_module_painter_text_width;
	}
	if (!strcmp(signature, "textHeight(_)")) {
		return wren_module_painter_text_height;
	}
	if (!strcmp(signature, "moveTo(_,_)")) {
		return wren_module_painter_move_to;
	}
	if (!strcmp(signature, "lineTo(_,_)")) {
		return wren_module_painter_line_to;
	}
	return NULL;
}


static bool wren_module_painter_bind_class(const char *class_name, WrenForeignClassMethods *methods) {
	if (!strcmp(class_name, "Painter")) {
		methods->allocate = wren_module_painter_allocate;
		methods->finalize = NULL;
		return true;
	}
	return false;
}


const struct wren_module wren_module_painter = {
	.name = "painter",
	.source =
		"foreign class Painter {\n"
		"  foreign begin()\n"
		"  foreign end()\n"
		"  foreign setPen(color, width)\n"
		"  foreign setBrush(color)\n"
		"  foreign setFont(style)\n"
		"  foreign rect(x, y, w, h)\n"
		"  foreign circle(x, y, r)\n"
		"  foreign ellipse(x, y, rx, ry)\n"
		"  foreign fill(x, y)\n"
		"  foreign line(x1, y1, x2, y2)\n"
		"  foreign text(x, y, str)\n"
		"  foreign textWidth(str)\n"
		"  foreign textHeight(str)\n"
		"  foreign moveTo(x, y)\n"
		"  foreign lineTo(x, y)\n"
		"  static normal { 0 }\n"
		"  static bold { 1 }\n"
		"  static italic { 2 }\n"
		"}\n",
	.bind_method = wren_module_painter_bind_method,
	.bind_class = wren_module_painter_bind_class,
};

#endif
