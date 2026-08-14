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


/* Crop the string in place so that it is at most w pixels wide when drawn with the current font. A
 * string which does not fit is cut and terminated with three periods, which are still guaranteed to
 * fit in the requested width. The ellipsis overwrites the tail of the string, so the result is never
 * longer than the input. Words are not considered, the string is cut at an arbitrary character. */
fb_painter_ret_t fb_painter_crop_text(FbPainter *self, char *text, uint16_t w);

/* Word wrap the string in place to fit a w x h pixel box when drawn with the current font. Lines are
 * separated with a newline character and the resulting number of lines is returned in lines (which
 * may be NULL). A single word too wide to fit a line on its own is cropped with fb_painter_crop_text.
 * If the text does not fit the requested height, the last line takes the rest of the text and is
 * cropped, hence it always ends with three periods. The text is never longer than the input. */
fb_painter_ret_t fb_painter_wrap_text(FbPainter *self, char *text, uint16_t w, uint16_t h, uint16_t *lines);

fb_painter_ret_t fb_painter_init(FbPainter *self, Fb *fb);
fb_painter_ret_t fb_painter_free(FbPainter *self);
