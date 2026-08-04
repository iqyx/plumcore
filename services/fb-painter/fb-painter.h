/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 2D painter on top of a framebuffer device
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <main.h>
#include <interfaces/fb.h>
#include <interfaces/painter.h>

typedef enum {
	FB_PAINTER_RET_OK = 0,
	FB_PAINTER_RET_FAILED,
	FB_PAINTER_RET_NULL,
	FB_PAINTER_RET_NOMEM,
} fb_painter_ret_t;

typedef struct fb_painter {
	Painter painter;                 /* painter interface exposed to clients, use &self->painter */

	Fb *fb;                          /* bound output framebuffer (the drawing target) */
	enum fb_mode mode;               /* native mode of the output */
	size_t w;
	size_t h;
	size_t row_bytes;                /* packed size of a single scanline */
	uint8_t *row;                    /* one-scanline scratch for read-modify-write blits */

	SemaphoreHandle_t lock;          /* held for the whole begin..end paint session */
	bool active;                     /* true between begin and end */

	/* Current drawing state. A pen width of 0 disables outlining; the brush is only used once it
	 * has been set at least once. */
	painter_color_t pen_color;
	uint16_t pen_width;
	painter_color_t brush_color;
	bool brush_set;

	/* Current text font: an opaque face handle (NULL selects the built-in default face) plus the
	 * style applied on top of it. */
	painter_font_t font;
	painter_font_style_t font_style;
} FbPainter;


fb_painter_ret_t fb_painter_init(FbPainter *self, Fb *fb);
fb_painter_ret_t fb_painter_free(FbPainter *self);
