/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 2D painter interface
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "fb.h"

typedef enum {
	PAINTER_RET_OK = 0,
	PAINTER_RET_FAILED,
	PAINTER_RET_NOTARGET,            /* no framebuffer is bound, call begin first */
} painter_ret_t;

/* Color is a packed 0xAARRGGBB value. The painter quantises/dithers it down to whatever the bound
 * framebuffer mode (FB_MODE_G1 .. FB_MODE_RGB888) can represent; the alpha byte is reserved and
 * currently ignored. */
typedef uint32_t painter_color_t;

typedef struct painter Painter;

struct painter_vmt {
	/* Bind a target framebuffer for the following drawing operations and reset the pen, brush and
	 * current point to their defaults. end flushes any pending output and unbinds the target. */
	painter_ret_t (*begin)(Painter *self, Fb *fb);
	painter_ret_t (*end)(Painter *self);

	/* Stroke style used to outline shapes and to draw lines. A width of 0 disables outlining. */
	painter_ret_t (*set_pen)(Painter *self, painter_color_t color, uint16_t width);
	/* Fill style used for the interior of closed shapes and for fill. */
	painter_ret_t (*set_brush)(Painter *self, painter_color_t color);

	/* Closed shapes are filled with the current brush and outlined with the current pen. */
	painter_ret_t (*rect)(Painter *self, int16_t x, int16_t y, uint16_t w, uint16_t h);
	painter_ret_t (*circle)(Painter *self, int16_t x, int16_t y, uint16_t r);
	painter_ret_t (*ellipse)(Painter *self, int16_t x, int16_t y, uint16_t rx, uint16_t ry);

	/* Flood fill the contiguous region around (x, y) with the current brush. */
	painter_ret_t (*fill)(Painter *self, int16_t x, int16_t y);

	/* Stroke a standalone segment with the current pen. */
	painter_ret_t (*line)(Painter *self, int16_t x1, int16_t y1, int16_t x2, int16_t y2);

	/* Path drawing: move_to repositions the current point without drawing, line_to strokes a
	 * segment from the current point to (x, y) with the current pen and leaves the current point
	 * at (x, y). */
	painter_ret_t (*move_to)(Painter *self, int16_t x, int16_t y);
	painter_ret_t (*line_to)(Painter *self, int16_t x, int16_t y);
};

typedef struct painter {
	const struct painter_vmt *vmt;
	void *parent;
} Painter;
