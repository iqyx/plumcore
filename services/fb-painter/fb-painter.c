/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 2D painter on top of a framebuffer device
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * A minimal implementation of the Painter interface backed by a single framebuffer (Fb) device. It
 * gives clients a stateful 2D drawing surface: a paint session is opened with begin and closed with
 * end, and the whole session is serialised by an internal mutex so multiple tasks can paint on the
 * same framebuffer safely. Colors are given as packed 0xAARRGGBB values and quantised down to the
 * framebuffer's native mode (grayscale by luminance, truecolor by channel truncation).
 *
 * v1 limitations, to be lifted later:
 * - the pen/brush state, rect, line, text and image primitives are implemented; circle, ellipse, fill and
 *   the move_to/line_to path API are still NULL,
 * - no dithering, colors are hard-quantised,
 * - rows are assumed byte-aligned (w * bpp is a multiple of 8, true for all real modes here).
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <main.h>
#include <interfaces/fb.h>
#include <interfaces/painter.h>

#include <mcu-renderer.h>

#include "fb-painter.h"
#include "font_nokia_small.h"
#include "font_nokia_small_bold.h"

#define MODULE_NAME "fb-painter"


/***************************************************************************************************
 * Fonts
 ***************************************************************************************************/

/* Built-in default face, the Nokia 3310 small font converted to the mcu-renderer format. */
const struct fb_painter_font_face fb_painter_face_nokia_small = {
	.normal = {font_nokia_small, FONT_NOKIA_SMALL_BOUNDINGBOX_WIDTH, FONT_NOKIA_SMALL_BOUNDINGBOX_HEIGHT,
	           FONT_NOKIA_SMALL_ASCENT, FONT_NOKIA_SMALL_CAP_HEIGHT},
	.bold = {font_nokia_small_bold, FONT_NOKIA_SMALL_BOLD_BOUNDINGBOX_WIDTH, FONT_NOKIA_SMALL_BOLD_BOUNDINGBOX_HEIGHT,
	         FONT_NOKIA_SMALL_BOLD_ASCENT, FONT_NOKIA_SMALL_BOLD_CAP_HEIGHT},
};


/* Pick the font of the current face in the current style. A missing style falls back to the normal variant,
 * a face without a normal variant to whichever style it has. */
static const struct fb_painter_font *font_select(const FbPainter *self) {
	const struct fb_painter_font_face *face = (self->font != NULL) ? self->font : &fb_painter_face_nokia_small;
	const struct fb_painter_font *font = &face->normal;

	switch (self->font_style) {
		case PAINTER_FONT_BOLD:
			font = &face->bold;
			break;
		case PAINTER_FONT_ITALIC:
			font = &face->italic;
			break;
		case PAINTER_FONT_NORMAL:
		default:
			break;
	}
	if (font->data == NULL) {
		font = &face->normal;
	}
	if (font->data == NULL) {
		font = &face->bold;
	}
	if (font->data == NULL) {
		font = &face->italic;
	}

	return font;
}


/***************************************************************************************************
 * Packed framebuffer pixel helpers (operating on a single scanline buffer)
 ***************************************************************************************************/

static bool fb_mode_supported(enum fb_mode mode) {
	switch (mode) {
		case FB_MODE_G1:
		case FB_MODE_G2:
		case FB_MODE_G4:
		case FB_MODE_G8:
		case FB_MODE_RGB565:
		case FB_MODE_RGB888:
			return true;
		default:
			return false;
	}
}


/* Size in bytes of a packed run of w pixels in the given mode. */
static size_t fb_packed_size(size_t w, enum fb_mode mode) {
	return (w * (size_t)mode + 7) / 8;
}


/* Set pixel x in a packed scanline. Bytes-per-pixel modes are stored big-endian, sub-byte modes are
 * packed MSB-first (leftmost pixel in the high bits), matching the compositor and the LCD driver. */
static void scan_set_pixel(uint8_t *row, enum fb_mode mode, size_t x, uint32_t value) {
	size_t bpp = (size_t)mode;

	if (bpp >= 8) {
		size_t bytes = bpp / 8;
		size_t off = x * bytes;
		for (size_t i = 0; i < bytes; i++) {
			row[off + bytes - 1 - i] = value & 0xff;
			value >>= 8;
		}
		return;
	}

	size_t ppb = 8 / bpp;
	size_t off = x / ppb;
	size_t sub = x % ppb;
	size_t shift = 8 - bpp * (sub + 1);
	uint32_t mask = (1u << bpp) - 1;
	row[off] = (row[off] & ~(mask << shift)) | ((value & mask) << shift);
}


/* Read pixel x from a packed scanline. The exact inverse of scan_set_pixel: bytes-per-pixel modes
 * are read big-endian, sub-byte modes MSB-first. */
static uint32_t scan_get_pixel(const uint8_t *row, enum fb_mode mode, size_t x) {
	size_t bpp = (size_t)mode;

	if (bpp >= 8) {
		size_t bytes = bpp / 8;
		size_t off = x * bytes;
		uint32_t value = 0;
		for (size_t i = 0; i < bytes; i++) {
			value = (value << 8) | row[off + i];
		}
		return value;
	}

	size_t ppb = 8 / bpp;
	size_t off = x / ppb;
	size_t sub = x % ppb;
	size_t shift = 8 - bpp * (sub + 1);
	uint32_t mask = (1u << bpp) - 1;
	return (row[off] >> shift) & mask;
}


/* Expand a native pixel value in the given mode back into a packed 0xAARRGGBB color. Loosely the
 * inverse of color_to_native (bits truncated during quantisation cannot be recovered). Used to
 * carry a source image pixel through a mode change onto the framebuffer. */
static painter_color_t native_to_color(enum fb_mode mode, uint32_t value) {
	switch (mode) {
		case FB_MODE_G1:
		case FB_MODE_G2:
		case FB_MODE_G4:
		case FB_MODE_G8: {
			/* Scale the bpp-bit gray level to a full 0..255 luma and replicate to all channels. */
			uint32_t maxval = (1u << (uint32_t)mode) - 1;
			uint32_t luma = value * 255 / maxval;
			return (luma << 16) | (luma << 8) | luma;
		}
		case FB_MODE_RGBX222: {
			uint32_t r = ((value >> 4) & 0x3) * 0x55;
			uint32_t g = ((value >> 2) & 0x3) * 0x55;
			uint32_t b = (value & 0x3) * 0x55;
			return (r << 16) | (g << 8) | b;
		}
		case FB_MODE_RGB565: {
			uint32_t r = ((value >> 11) & 0x1f) << 3;
			uint32_t g = ((value >> 5) & 0x3f) << 2;
			uint32_t b = (value & 0x1f) << 3;
			return (r << 16) | (g << 8) | b;
		}
		case FB_MODE_RGB888:
			return value & 0xffffff;
		default:
			return 0;
	}
}


/* Quantise a 0xAARRGGBB color down to the native framebuffer mode. Alpha is ignored. */
static uint32_t color_to_native(const FbPainter *self, painter_color_t color) {
	uint8_t r = (color >> 16) & 0xff;
	uint8_t g = (color >> 8) & 0xff;
	uint8_t b = color & 0xff;

	switch (self->mode) {
		case FB_MODE_G1:
		case FB_MODE_G2:
		case FB_MODE_G4:
		case FB_MODE_G8: {
			/* ITU-R BT.601 luma, then keep the top bits for the target depth. */
			uint32_t luma = (r * 77 + g * 150 + b * 29) >> 8;
			return luma >> (8 - (uint32_t)self->mode);
		}
		case FB_MODE_RGB565:
			return ((uint32_t)(r >> 3) << 11) | ((uint32_t)(g >> 2) << 5) | (uint32_t)(b >> 3);
		case FB_MODE_RGB888:
			return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
		default:
			return 0;
	}
}


/* Read/write a whole scanline from/to the bound framebuffer into the scratch row buffer. */
static fb_ret_t row_read(FbPainter *self, size_t y) {
	return self->fb->vmt->read(self->fb, y * self->row_bytes, self->row, self->row_bytes, self->mode);
}


static fb_ret_t row_write(FbPainter *self, size_t y) {
	return self->fb->vmt->write(self->fb, y * self->row_bytes, self->row, self->row_bytes, self->mode);
}


/***************************************************************************************************
 * Painter interface
 ***************************************************************************************************/

static painter_ret_t painter_begin(Painter *self) {
	FbPainter *p = self->parent;

	if (p->lock == NULL) {
		return PAINTER_RET_FAILED;
	}
	xSemaphoreTake(p->lock, portMAX_DELAY);
	p->active = true;

	return PAINTER_RET_OK;
}


static painter_ret_t painter_end(Painter *self) {
	FbPainter *p = self->parent;

	if (!p->active) {
		return PAINTER_RET_NOTARGET;
	}
	/* Present whatever was drawn during this session before releasing the surface. */
	if (p->fb->vmt->flush != NULL) {
		p->fb->vmt->flush(p->fb);
	}
	p->active = false;
	xSemaphoreGive(p->lock);

	return PAINTER_RET_OK;
}


static painter_ret_t painter_set_pen(Painter *self, painter_color_t color, uint16_t width) {
	FbPainter *p = self->parent;

	if (!p->active) {
		return PAINTER_RET_NOTARGET;
	}
	p->pen_color = color;
	p->pen_width = width;

	return PAINTER_RET_OK;
}


static painter_ret_t painter_set_brush(Painter *self, painter_color_t color) {
	FbPainter *p = self->parent;

	if (!p->active) {
		return PAINTER_RET_NOTARGET;
	}
	p->brush_color = color;
	p->brush_set = true;

	return PAINTER_RET_OK;
}


static painter_ret_t painter_set_font(Painter *self, painter_font_style_t style, painter_font_t font) {
	FbPainter *p = self->parent;

	if (!p->active) {
		return PAINTER_RET_NOTARGET;
	}
	p->font = font;
	p->font_style = style;

	return PAINTER_RET_OK;
}


/* State shared with the mcu-renderer callbacks while a string is drawn, passed in mr_t.display. */
struct text_ctx {
	FbPainter *p;
	uint32_t pen;
	bool failed;
};


/* mcu-renderer fills the text rectangle background with this callback. Text is drawn transparently, hence
 * there is nothing to do. */
static void text_draw_rectangle(mr_t *mr, const mr_rectangle_t *rectangle) {
	(void)mr;
	(void)rectangle;
}


/* Blend a chunk of rendered text onto the framebuffer. The buffer contains a coverage (alpha) value 0..32 for
 * every pixel of the screen rectangle, the pen color is blended over the current framebuffer content with
 * it. Fully covered pixels get the pen color directly, uncovered pixels and rows are left untouched. */
static void text_draw_textbuffer(mr_t *mr, uint8_t *buffer, uint32_t buffer_pitch, mr_rectangle_t *rectangle) {
	struct text_ctx *ctx = mr->display;
	FbPainter *p = ctx->p;

	for (int16_t ty = 0; ty < rectangle->height; ty++) {
		const uint8_t *alpha = buffer + ty * buffer_pitch;
		int16_t tx = 0;
		while (tx < rectangle->width && alpha[tx] == 0) {
			tx++;
		}
		if (tx == rectangle->width) {
			continue;
		}

		int32_t yy = rectangle->y + ty;
		if (row_read(p, yy) != FB_RET_OK) {
			ctx->failed = true;
			return;
		}
		for (; tx < rectangle->width; tx++) {
			if (alpha[tx] == 0) {
				continue;
			}
			size_t xx = rectangle->x + tx;
			uint32_t value = ctx->pen;
			if (alpha[tx] < 32) {
				painter_color_t bg = native_to_color(p->mode, scan_get_pixel(p->row, p->mode, xx));
				painter_color_t color = 0;
				for (uint32_t shift = 0; shift < 24; shift += 8) {
					uint32_t f = (p->pen_color >> shift) & 0xff;
					uint32_t b = (bg >> shift) & 0xff;
					color |= ((f * alpha[tx] + b * (32 - alpha[tx])) / 32) << shift;
				}
				value = color_to_native(p, color);
			}
			scan_set_pixel(p->row, p->mode, xx, value);
		}
		if (row_write(p, yy) != FB_RET_OK) {
			ctx->failed = true;
			return;
		}
	}
}


static painter_ret_t painter_text(Painter *self, int16_t x, int16_t y, const char *text) {
	FbPainter *p = self->parent;

	if (!p->active) {
		return PAINTER_RET_NOTARGET;
	}
	if (text == NULL) {
		return PAINTER_RET_FAILED;
	}

	/* mcu-renderer renders the string into a coverage buffer in chunks and hands each chunk over to
	 * text_draw_textbuffer. The buffer is one glyph cell high and two glyph cells wide (a chunk must fit at
	 * least the widest glyph). It is allocated only for the duration of the call as it may be too large for
	 * the stack of the calling task with big fonts. */
	const struct fb_painter_font *font = font_select(p);
	size_t buffer_size = (size_t)font->bbox_w * 2 * font->bbox_h;
	uint8_t *buffer = calloc(buffer_size, sizeof(uint8_t));
	if (buffer == NULL) {
		return PAINTER_RET_FAILED;
	}

	struct text_ctx ctx = {
		.p = p,
		.pen = color_to_native(p, p->pen_color),
		.failed = false,
	};
	mr_t mr;
	mr_init(&mr);
	mr.draw_rectangle_callback = text_draw_rectangle;
	mr.draw_string_callback = mr_draw_string_textbuffer;
	mr.draw_textbuffer_callback = text_draw_textbuffer;
	mr.display_width = (int16_t)p->w;
	mr.display_height = (int16_t)p->h;
	mr.display = &ctx;
	mr.buffer = buffer;
	mr.buffer_size = buffer_size;
	mr_set_font(&mr, font->data);

	/* The whole framebuffer is the clipping rectangle, the offset places the top (ascent) left corner of the
	 * text at (x, y). */
	mr_rectangle_t clip = {0, 0, (int16_t)p->w, (int16_t)p->h};
	mr_point_t offset = {x, y};
	mr_draw_text(&mr, text, &clip, &offset);

	free(buffer);

	return ctx.failed ? PAINTER_RET_FAILED : PAINTER_RET_OK;
}


static painter_ret_t painter_text_size(Painter *self, const char *text, uint16_t *w, uint16_t *h) {
	FbPainter *p = self->parent;

	if (!p->active) {
		return PAINTER_RET_NOTARGET;
	}
	if (text == NULL) {
		return PAINTER_RET_FAILED;
	}

	mr_t mr;
	mr_init(&mr);
	mr_set_font(&mr, font_select(p)->data);

	if (w != NULL) {
		*w = (uint16_t)mr_get_text_width(&mr, text);
	}
	if (h != NULL) {
		*h = (uint16_t)mr_get_line_height(&mr);
	}

	return PAINTER_RET_OK;
}


/* Fast path for a filled rectangle spanning the whole framebuffer width. Every scanline it covers is
 * fully defined by the rectangle (nothing outside to preserve), and only two distinct scanlines ever
 * occur: an all-pen border row (top/bottom pw rows) and a pen-edges + brush middle row. Each is built
 * once in the scratch row and written to every row that shares it, turning an O(w*h) fill into O(w)
 * pixel work plus h scanline writes. */
static painter_ret_t painter_rect_fill_full_width(FbPainter *p, int32_t x0, int32_t x1,
                                                  int32_t y0, int32_t y1, uint32_t pen, uint32_t brush, int32_t pw) {
	int32_t cy0 = (y0 < 0) ? 0 : y0;
	int32_t cy1 = (y1 > (int32_t)p->h) ? (int32_t)p->h : y1;

	/* Row bands: [y0, y0+pw) and [y1-pw, y1) are all-pen borders, everything between them is a
	 * middle row (pen on the left/right pw columns, brush in the interior). */
	enum { ROW_NONE, ROW_BORDER, ROW_MIDDLE } ready = ROW_NONE;

	for (int32_t yy = cy0; yy < cy1; yy++) {
		bool border_row = (yy < y0 + pw) || (yy >= y1 - pw);

		/* (Re)build the scratch row only when the required scanline type changes. */
		if (border_row && ready != ROW_BORDER) {
			for (int32_t xx = 0; xx < (int32_t)p->w; xx++) {
				scan_set_pixel(p->row, p->mode, xx, pen);
			}
			ready = ROW_BORDER;
		} else if (!border_row && ready != ROW_MIDDLE) {
			for (int32_t xx = 0; xx < (int32_t)p->w; xx++) {
				bool on_border = (xx < x0 + pw) || (xx >= x1 - pw);
				scan_set_pixel(p->row, p->mode, xx, on_border ? pen : brush);
			}
			ready = ROW_MIDDLE;
		}

		if (row_write(p, yy) != FB_RET_OK) {
			return PAINTER_RET_FAILED;
		}
	}

	return PAINTER_RET_OK;
}


static painter_ret_t painter_rect(Painter *self, int16_t x, int16_t y, uint16_t w, uint16_t h) {
	FbPainter *p = self->parent;

	if (!p->active) {
		return PAINTER_RET_NOTARGET;
	}
	if (w == 0 || h == 0 || (!p->brush_set && p->pen_width == 0)) {
		return PAINTER_RET_OK;
	}

	uint32_t brush = color_to_native(p, p->brush_color);
	uint32_t pen = color_to_native(p, p->pen_color);
	int32_t pw = p->pen_width;

	/* Half-open rectangle [x0, x1) x [y0, y1), clipped per pixel to the framebuffer. */
	int32_t x0 = x;
	int32_t y0 = y;
	int32_t x1 = (int32_t)x + w;
	int32_t y1 = (int32_t)y + h;

	/* A filled rectangle covering the full width leaves nothing outside itself to preserve, so it
	 * can be composed from prebuilt scanlines without a read-modify-write per row. */
	if (p->brush_set && x0 <= 0 && x1 >= (int32_t)p->w) {
		return painter_rect_fill_full_width(p, x0, x1, y0, y1, pen, brush, pw);
	}

	for (int32_t yy = y0; yy < y1; yy++) {
		if (yy < 0 || yy >= (int32_t)p->h) {
			continue;
		}
		if (row_read(p, yy) != FB_RET_OK) {
			return PAINTER_RET_FAILED;
		}
		for (int32_t xx = x0; xx < x1; xx++) {
			if (xx < 0 || xx >= (int32_t)p->w) {
				continue;
			}
			bool on_border = pw > 0 && (xx < x0 + pw || xx >= x1 - pw || yy < y0 + pw || yy >= y1 - pw);
			if (on_border) {
				scan_set_pixel(p->row, p->mode, xx, pen);
			} else if (p->brush_set) {
				scan_set_pixel(p->row, p->mode, xx, brush);
			}
		}
		if (row_write(p, yy) != FB_RET_OK) {
			return PAINTER_RET_FAILED;
		}
	}

	return PAINTER_RET_OK;
}


/* Plot a pen-width x pen-width native-colour block centred on (cx, cy) through per-scanline read-modify-write,
 * clipped per pixel to the framebuffer. A width of 1 draws a single pixel. */
static painter_ret_t painter_plot_thick(FbPainter *p, int32_t cx, int32_t cy, uint32_t color, int32_t pw) {
	int32_t half = pw / 2;
	for (int32_t oy = -half; oy < pw - half; oy++) {
		int32_t yy = cy + oy;
		if (yy < 0 || yy >= (int32_t)p->h) {
			continue;
		}
		if (row_read(p, yy) != FB_RET_OK) {
			return PAINTER_RET_FAILED;
		}
		bool dirty = false;
		for (int32_t ox = -half; ox < pw - half; ox++) {
			int32_t xx = cx + ox;
			if (xx < 0 || xx >= (int32_t)p->w) {
				continue;
			}
			scan_set_pixel(p->row, p->mode, xx, color);
			dirty = true;
		}
		if (dirty && row_write(p, yy) != FB_RET_OK) {
			return PAINTER_RET_FAILED;
		}
	}

	return PAINTER_RET_OK;
}


/* Stroke a standalone segment from (x1, y1) to (x2, y2) with the current pen using Bresenham's algorithm,
 * each point drawn as a pen-width square so the pen width thickens the line. A pen width of 0 draws nothing,
 * matching the "width of 0 disables outlining" pen semantics. */
static painter_ret_t painter_line(Painter *self, int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
	FbPainter *p = self->parent;

	if (!p->active) {
		return PAINTER_RET_NOTARGET;
	}
	int32_t pw = p->pen_width;
	if (pw <= 0) {
		return PAINTER_RET_OK;
	}

	uint32_t pen = color_to_native(p, p->pen_color);

	int32_t x = x1;
	int32_t y = y1;
	int32_t dx = abs((int32_t)x2 - x1);
	int32_t dy = -abs((int32_t)y2 - y1);
	int32_t sx = (x1 < x2) ? 1 : -1;
	int32_t sy = (y1 < y2) ? 1 : -1;
	int32_t err = dx + dy;

	while (true) {
		if (painter_plot_thick(p, x, y, pen, pw) != PAINTER_RET_OK) {
			return PAINTER_RET_FAILED;
		}
		if (x == x2 && y == y2) {
			break;
		}
		int32_t e2 = 2 * err;
		if (e2 >= dy) {
			err += dy;
			x += sx;
		}
		if (e2 <= dx) {
			err += dx;
			y += sy;
		}
	}

	return PAINTER_RET_OK;
}


/* Reposition the current point of path drawing without drawing anything. */
static painter_ret_t painter_move_to(Painter *self, int16_t x, int16_t y) {
	FbPainter *p = self->parent;

	if (!p->active) {
		return PAINTER_RET_NOTARGET;
	}
	p->pos_x = x;
	p->pos_y = y;

	return PAINTER_RET_OK;
}


/* Stroke a segment from the current point to (x, y) and leave the current point there. */
static painter_ret_t painter_line_to(Painter *self, int16_t x, int16_t y) {
	FbPainter *p = self->parent;

	painter_ret_t ret = painter_line(self, p->pos_x, p->pos_y, x, y);
	if (ret != PAINTER_RET_OK) {
		return ret;
	}
	p->pos_x = x;
	p->pos_y = y;

	return PAINTER_RET_OK;
}


static painter_ret_t painter_image(Painter *self, int16_t x, int16_t y, const struct painter_raw_image *image,
                                   enum painter_mode mode) {
	FbPainter *p = self->parent;

	if (!p->active) {
		return PAINTER_RET_NOTARGET;
	}
	if (image == NULL) {
		return PAINTER_RET_FAILED;
	}

	size_t src_row_bytes = fb_packed_size(image->w, image->mode);

	/* When the image is already in the framebuffer's native mode (the common case, as
	 * tools/imtocdata.py generates images in the target mode) the pixel value is copied straight
	 * through. Only a genuine mode mismatch pays for the (lossy) per-pixel colour conversion. */
	bool inverted = (mode == PAINTER_MODE_INVERTED);
	bool same_mode = (image->mode == p->mode);

	/* Blit the image one scanline at a time. Pixels outside the framebuffer are clipped. */
	for (size_t iy = 0; iy < image->h; iy++) {
		int32_t yy = y + (int32_t)iy;
		if (yy < 0 || yy >= (int32_t)p->h) {
			continue;
		}
		if (row_read(p, yy) != FB_RET_OK) {
			return PAINTER_RET_FAILED;
		}
		const uint8_t *src_row = image->data + iy * src_row_bytes;
		bool dirty = false;
		for (size_t ix = 0; ix < image->w; ix++) {
			int32_t xx = x + (int32_t)ix;
			if (xx < 0 || xx >= (int32_t)p->w) {
				continue;
			}
			uint32_t value = scan_get_pixel(src_row, image->mode, ix);
			if (!same_mode) {
				value = color_to_native(p, native_to_color(image->mode, value));
			}
			/* Inversion is a plain bitwise complement: every packed mode encodes intensity/channels
			 * directly, and scan_set_pixel only consumes the relevant low bits, so ~value negates it. */
			if (inverted) {
				value = ~value;
			}
			scan_set_pixel(p->row, p->mode, xx, value);
			dirty = true;
		}
		if (dirty && row_write(p, yy) != FB_RET_OK) {
			return PAINTER_RET_FAILED;
		}
	}

	return PAINTER_RET_OK;
}


static const struct painter_vmt fb_painter_vmt = {
	.begin = painter_begin,
	.end = painter_end,
	.set_pen = painter_set_pen,
	.set_brush = painter_set_brush,
	.set_font = painter_set_font,
	.rect = painter_rect,
	.line = painter_line,
	.text = painter_text,
	.text_size = painter_text_size,
	.image = painter_image,
	.move_to = painter_move_to,
	.line_to = painter_line_to,
};


/***************************************************************************************************
 * Service API
 ***************************************************************************************************/

fb_painter_ret_t fb_painter_crop_text(FbPainter *self, char *text, uint16_t w) {
	if (self == NULL || text == NULL) {
		return FB_PAINTER_RET_NULL;
	}

	uint16_t tw = 0;
	if (painter_text_size(&self->painter, text, &tw, NULL) != PAINTER_RET_OK) {
		return FB_PAINTER_RET_FAILED;
	}
	if (tw <= w) {
		return FB_PAINTER_RET_OK;
	}

	/* Too wide: the ellipsis overwrites the tail of the string instead of being appended to it, so
	 * the result never grows and the buffer cannot overflow. Shorten the kept prefix one character
	 * at a time until the prefix with the ellipsis fits. */
	size_t len = strlen(text);
	size_t ellipsis = (len < 3) ? len : 3;
	size_t keep = len - ellipsis;
	while (true) {
		memset(text + keep, '.', ellipsis);
		text[keep + ellipsis] = '\0';
		if (painter_text_size(&self->painter, text, &tw, NULL) != PAINTER_RET_OK) {
			return FB_PAINTER_RET_FAILED;
		}
		if (tw <= w) {
			return FB_PAINTER_RET_OK;
		}
		if (keep == 0) {
			break;
		}
		keep--;
	}

	/* Not even the ellipsis alone fits in the requested width. */
	text[0] = '\0';

	return FB_PAINTER_RET_OK;
}


/* Check if the text between start and end (exclusive) fits the requested width. The buffer is
 * terminated at end for the measurement and restored afterwards. */
static bool wrap_fits(FbPainter *self, char *start, char *end, uint16_t w) {
	char saved = *end;
	*end = '\0';
	uint16_t tw = 0;
	painter_ret_t ret = painter_text_size(&self->painter, start, &tw, NULL);
	*end = saved;

	return (ret == PAINTER_RET_OK) && (tw <= w);
}


/* Break the line at the "at" position and move the rest of the text, with its leading spaces removed,
 * right behind the break. The text is only ever shifted towards the beginning, it never grows. */
static void wrap_break_line(char *at, char *rest) {
	while (*rest == ' ') {
		rest++;
	}
	*at = '\n';
	if ((at + 1) != rest) {
		memmove(at + 1, rest, strlen(rest) + 1);
	}
}


fb_painter_ret_t fb_painter_wrap_text(FbPainter *self, char *text, uint16_t w, uint16_t h, uint16_t *lines) {
	if (self == NULL || text == NULL) {
		return FB_PAINTER_RET_NULL;
	}

	/* All lines have the height of the font, hence the line count the requested height allows is
	 * known upfront. */
	uint16_t line_h = 0;
	if (painter_text_size(&self->painter, "", NULL, &line_h) != PAINTER_RET_OK || line_h == 0) {
		return FB_PAINTER_RET_FAILED;
	}
	uint16_t max_lines = h / line_h;
	uint16_t line_count = 0;

	if (max_lines == 0) {
		text[0] = '\0';
		if (lines != NULL) {
			*lines = 0;
		}
		return FB_PAINTER_RET_OK;
	}

	char *rest = text;
	while (*rest == ' ') {
		rest++;
	}
	if (rest != text) {
		memmove(text, rest, strlen(rest) + 1);
	}

	char *line = text;
	char *last_line = text;
	while (*line != '\0' && line_count < max_lines) {
		last_line = line;

		/* Append whole words to the line for as long as they fit the requested width. */
		char *end = line;
		char *word_end = line;
		while (*word_end != '\0') {
			char *next = word_end;
			while (*next == ' ') {
				next++;
			}
			while (*next != '\0' && *next != ' ') {
				next++;
			}
			if (next == word_end || !wrap_fits(self, line, next, w)) {
				break;
			}
			end = next;
			word_end = next;
		}
		line_count++;

		if (end == line) {
			/* Not even the first word of the line fits. Crop it, sacrificing a single character
			 * to make room for the line break, and continue with the rest on the next line. */
			while (*word_end != '\0' && *word_end != ' ') {
				word_end++;
			}
			char *cut = word_end - 1;
			char saved = *cut;
			*cut = '\0';
			fb_painter_crop_text(self, line, w);
			end = line + strlen(line);
			*cut = saved;
		} else {
			word_end = end;
		}

		if (*word_end == '\0') {
			/* The last word was processed, no line break is needed. */
			*end = '\0';
			line = end;
			break;
		}
		wrap_break_line(end, word_end);
		line = end + 1;
	}

	if (*line != '\0') {
		/* Out of vertical room. Give the rest of the text back to the last line and crop it to
		 * width, so the ellipsis shows that something was left out. */
		*(line - 1) = ' ';
		fb_painter_crop_text(self, last_line, w);
	}

	if (lines != NULL) {
		*lines = line_count;
	}

	return FB_PAINTER_RET_OK;
}


fb_painter_ret_t fb_painter_init(FbPainter *self, Fb *fb) {
	if (self == NULL || fb == NULL) {
		return FB_PAINTER_RET_NULL;
	}
	memset(self, 0, sizeof(FbPainter));
	self->fb = fb;
	self->painter.parent = self;
	self->painter.vmt = &fb_painter_vmt;

	struct fb_stat stat = {0};
	if (fb->vmt->stat == NULL || fb->vmt->stat(fb, &stat) != FB_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot stat the framebuffer"));
		return FB_PAINTER_RET_FAILED;
	}
	if (!fb_mode_supported(stat.mode)) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("unsupported framebuffer mode %u"), (unsigned)stat.mode);
		return FB_PAINTER_RET_FAILED;
	}
	self->mode = stat.mode;
	self->w = stat.w;
	self->h = stat.h;
	self->row_bytes = fb_packed_size(self->w, self->mode);

	self->row = calloc(self->row_bytes, sizeof(uint8_t));
	if (self->row == NULL) {
		goto err;
	}
	self->lock = xSemaphoreCreateMutex();
	if (self->lock == NULL) {
		goto err;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialized, target %ux%u, mode %u bpp"),
	      (unsigned)self->w, (unsigned)self->h, (unsigned)self->mode);
	return FB_PAINTER_RET_OK;

err:
	if (self->row != NULL) {
		free(self->row);
		self->row = NULL;
	}
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot initialize the service"));
	return FB_PAINTER_RET_FAILED;
}


fb_painter_ret_t fb_painter_free(FbPainter *self) {
	if (self == NULL) {
		return FB_PAINTER_RET_NULL;
	}
	if (self->row != NULL) {
		free(self->row);
		self->row = NULL;
	}
	if (self->lock != NULL) {
		vSemaphoreDelete(self->lock);
		self->lock = NULL;
	}

	return FB_PAINTER_RET_OK;
}
