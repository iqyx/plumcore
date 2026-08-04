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

/* Text style applied on top of the selected font face. */
typedef enum {
	PAINTER_FONT_NORMAL = 0,
	PAINTER_FONT_BOLD,
	PAINTER_FONT_ITALIC,
} painter_font_style_t;

enum painter_mode {
	PAINTER_MODE_NORMAL,
	PAINTER_MODE_INVERTED = 1,
};

/* Opaque handle to a font face. The concrete meaning is defined by the painter implementation; how a
 * caller obtains such a handle is still TBD. A NULL handle selects the painter's default face. */
typedef void *painter_font_t;

typedef struct painter Painter;

/* An uncompressed raster image: w x h pixels packed in the given framebuffer mode (the same packed
 * layout the painter uses internally). The painter converts it to the bound framebuffer's native
 * mode when blitting. The tools/imtocdata.py converter produces arrays in this layout. */
struct painter_raw_image {
	size_t w;
	size_t h;
	enum fb_mode mode;
	uint8_t data[];
};

struct painter_vmt {
	painter_ret_t (*begin)(Painter *self);
	painter_ret_t (*end)(Painter *self);

	/* Stroke style used to outline shapes and to draw lines. A width of 0 disables outlining. */
	painter_ret_t (*set_pen)(Painter *self, painter_color_t color, uint16_t width);
	/* Fill style used for the interior of closed shapes and for fill. */
	painter_ret_t (*set_brush)(Painter *self, painter_color_t color);
	/* Font face and style used to draw text. A NULL font selects the painter's default face. */
	painter_ret_t (*set_font)(Painter *self, painter_font_style_t style, painter_font_t font);

	/* Closed shapes are filled with the current brush and outlined with the current pen. */
	painter_ret_t (*rect)(Painter *self, int16_t x, int16_t y, uint16_t w, uint16_t h);
	painter_ret_t (*circle)(Painter *self, int16_t x, int16_t y, uint16_t r);
	painter_ret_t (*ellipse)(Painter *self, int16_t x, int16_t y, uint16_t rx, uint16_t ry);

	/* Flood fill the contiguous region around (x, y) with the current brush. */
	painter_ret_t (*fill)(Painter *self, int16_t x, int16_t y);

	/* Stroke a standalone segment with the current pen. */
	painter_ret_t (*line)(Painter *self, int16_t x1, int16_t y1, int16_t x2, int16_t y2);

	/* Draw a text string with its top-left corner at (x, y) using the current font and pen color. */
	painter_ret_t (*text)(Painter *self, int16_t x, int16_t y, const char *text);

	/* Blit an uncompressed raster image with its top-left corner at (x, y). The image is converted
	 * from its own mode to the framebuffer's native mode and clipped to the framebuffer. */
	painter_ret_t (*image)(Painter *self, int16_t x, int16_t y, const struct painter_raw_image *image, enum painter_mode mode);

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
