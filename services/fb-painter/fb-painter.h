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

/* A single mcu-renderer font together with its bounding box size (the largest glyph cell), which sizes the
 * text rendering buffer, and its vertical metrics: the ascent (distance from the top of the text line to the
 * baseline) and the cap height (height of capital letters and digits above the baseline). A NULL data pointer
 * marks a missing style variant. The optional glyph offset table (SERVICE_FB_PAINTER_FONTS_GLYPH_TABLE) covers
 * glyph_count codepoints starting at glyph_first, without it the glyphs are looked up in the font blocks. */
struct fb_painter_font {
	const uint8_t *data;
	uint8_t bbox_w;
	uint8_t bbox_h;
	uint8_t ascent;
	uint8_t cap_height;
	const uint16_t *glyph_table;
	uint16_t glyph_first;
	uint16_t glyph_count;
};

/* A font face bundles the style variants of a font, a missing style falls back to the normal one. Additional faces
 * are generated from the configured fonts into assets/fonts/fonts.h, pass &fb_painter_face_<name> to set_font. */
struct fb_painter_font_face {
	struct fb_painter_font normal;
	struct fb_painter_font bold;
	struct fb_painter_font italic;
};

/* Built-in default face (Nokia 3310 small font, 8 px line height), used when set_font is given NULL. */
extern const struct fb_painter_font_face fb_painter_face_nokia_small;

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

	/* Current text font: a face (NULL selects the default face) plus the style applied on top of it. */
	const struct fb_painter_font_face *font;
	painter_font_style_t font_style;

	/* Current point of path drawing, set by move_to and advanced by line_to. */
	int16_t pos_x;
	int16_t pos_y;
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
