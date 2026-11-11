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
#include "fonts.h"

#define MODULE_NAME "fb-painter"


/***************************************************************************************************
 * Fonts
 ***************************************************************************************************/

/* The text rendering buffer (fb_painter_text_buffer, generated in fonts.c) is sized for the biggest font and
 * shared by all painter instances, smaller fonts use it whole and get a wider buffer. Painters may draw from
 * different tasks, hence the buffer has its own lock, held only while a string is rendered. */
static SemaphoreHandle_t text_buffer_lock;


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


/* Select the font in an mcu-renderer instance, including its glyph offset table if it has one. */
static void font_set(mr_t *mr, const struct fb_painter_font *font) {
	mr_set_font(mr, font->data);
	if (font->glyph_table != NULL) {
		mr_set_glyph_table(mr, font->glyph_table, font->glyph_first, font->glyph_count);
	}
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


/* Scanlines are packed the same way as in the compositor and the LCD driver: bytes-per-pixel modes are stored
 * big-endian, sub-byte modes MSB-first (leftmost pixel in the high bits). The helpers below never touch single
 * pixels through a generic accessor, they work on whole bytes and runs instead. */

/* Fill n pixels of a packed scanline starting at pixel x with a native value. Sub-byte modes replicate the value
 * into a byte pattern, mask the partial head and tail bytes and memset the whole bytes in between. Byte modes
 * write the first pixel and replicate it with doubling memcpy. */
static void scan_fill(uint8_t *row, enum fb_mode mode, size_t x, size_t n, uint32_t value) {
	size_t bpp = (size_t)mode;

	if (n == 0) {
		return;
	}

	if (bpp >= 8) {
		size_t bytes = bpp / 8;
		uint8_t *d = row + x * bytes;
		for (size_t i = 0; i < bytes; i++) {
			d[bytes - 1 - i] = (value >> (8 * i)) & 0xff;
		}
		for (size_t done = bytes; done < n * bytes; done *= 2) {
			memcpy(d + done, d, (done < n * bytes - done) ? done : n * bytes - done);
		}
		return;
	}

	uint32_t maxv = (1u << bpp) - 1;
	uint8_t pattern = (uint8_t)((value & maxv) * (0xff / maxv));
	size_t bit0 = x * bpp;
	size_t bit1 = (x + n) * bpp;
	uint8_t *b = row + bit0 / 8;
	uint8_t *e = row + bit1 / 8;
	uint8_t head = 0xff >> (bit0 & 7);
	uint8_t tail = (uint8_t)~(0xff >> (bit1 & 7));
	if (b == e) {
		*b = (*b & ~(head & tail)) | (pattern & head & tail);
		return;
	}
	*b = (*b & ~head) | (pattern & head);
	b++;
	memset(b, pattern, e - b);
	if (bit1 & 7) {
		*e = (*e & ~tail) | (pattern & tail);
	}
}


/* Copy n pixels from src (starting at pixel sx) to dst (starting at pixel dx), both packed in the same mode,
 * optionally inverting them. Byte modes are copied with memcpy, sub-byte modes one destination byte at a time:
 * the bits for the byte are fetched from at most two source bytes and merged in with a mask. */
static void scan_copy(uint8_t *dst, size_t dx, const uint8_t *src, size_t sx, size_t n, enum fb_mode mode,
                      bool invert) {
	size_t bpp = (size_t)mode;

	if (bpp >= 8 && !invert) {
		memcpy(dst + dx * bpp / 8, src + sx * bpp / 8, n * bpp / 8);
		return;
	}

	size_t dbit = dx * bpp;
	size_t sbit = sx * bpp;
	size_t end = dbit + n * bpp;
	while (dbit < end) {
		uint32_t chunk = 8 - (dbit & 7);
		if (chunk > end - dbit) {
			chunk = end - dbit;
		}
		uint32_t off = sbit & 7;
		uint32_t w = (uint32_t)src[sbit / 8] << 8;
		if (off + chunk > 8) {
			w |= src[sbit / 8 + 1];
		}
		uint32_t bits = (w >> (16 - off - chunk)) & ((1u << chunk) - 1);
		if (invert) {
			bits = ~bits & ((1u << chunk) - 1);
		}
		uint32_t shift = 8 - (dbit & 7) - chunk;
		uint8_t mask = (uint8_t)(((1u << chunk) - 1) << shift);
		dst[dbit / 8] = (dst[dbit / 8] & ~mask) | (uint8_t)(bits << shift);
		dbit += chunk;
		sbit += chunk;
	}
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


/* Blend a run of n text coverage values (0..32) in the pen color onto the scratch row starting at pixel x. Sub-byte
 * modes keep the current destination byte in a register, walk the pixel shift inside it and store each byte once.
 * Gray modes blend directly on the native gray levels, RGB modes per channel. Zero coverage leaves the pixel. */
static void scan_blend(FbPainter *self, size_t x, const uint8_t *alpha, size_t n, uint32_t pen) {
	size_t bpp = (size_t)self->mode;

	if (bpp < 8) {
		uint32_t maxv = (1u << bpp) - 1;
		uint8_t *b = self->row + x * bpp / 8;
		uint32_t shift = 8 - bpp - ((x * bpp) & 7);
		uint32_t byte = *b;
		for (size_t i = 0; i < n; i++) {
			if (alpha[i] != 0) {
				uint32_t v = pen;
				if (alpha[i] < 32) {
					v = (pen * alpha[i] + ((byte >> shift) & maxv) * (32 - alpha[i]) + 16) >> 5;
				}
				byte = (byte & ~(maxv << shift)) | (v << shift);
			}
			if (shift == 0) {
				*b++ = (uint8_t)byte;
				if ((i + 1) < n) {
					byte = *b;
				}
				shift = 8 - bpp;
			} else {
				shift -= bpp;
			}
		}
		if (shift != 8 - bpp) {
			*b = (uint8_t)byte;
		}
		return;
	}

	size_t bytes = bpp / 8;
	uint8_t *d = self->row + x * bytes;
	for (size_t i = 0; i < n; i++, d += bytes) {
		if (alpha[i] == 0) {
			continue;
		}
		uint32_t v = pen;
		if (alpha[i] < 32) {
			uint32_t bg = 0;
			for (size_t j = 0; j < bytes; j++) {
				bg = (bg << 8) | d[j];
			}
			if (self->mode == FB_MODE_G8) {
				v = (pen * alpha[i] + bg * (32 - alpha[i]) + 16) >> 5;
			} else {
				painter_color_t bgc = native_to_color(self->mode, bg);
				painter_color_t color = 0;
				for (uint32_t sh = 0; sh < 24; sh += 8) {
					uint32_t f = (self->pen_color >> sh) & 0xff;
					uint32_t c = (bgc >> sh) & 0xff;
					color |= ((f * alpha[i] + c * (32 - alpha[i])) / 32) << sh;
				}
				v = color_to_native(self, color);
			}
		}
		for (size_t j = 0; j < bytes; j++) {
			d[bytes - 1 - j] = (v >> (8 * j)) & 0xff;
		}
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
 * every pixel of the screen rectangle. Each row is trimmed to its covered part and blended in a single run,
 * rows without any coverage are not touched at all. */
static void text_draw_textbuffer(mr_t *mr, uint8_t *buffer, uint32_t buffer_pitch, mr_rectangle_t *rectangle) {
	struct text_ctx *ctx = mr->display;

	for (int16_t ty = 0; ty < rectangle->height; ty++) {
		const uint8_t *alpha = buffer + ty * buffer_pitch;
		int16_t tx0 = 0;
		int16_t tx1 = rectangle->width;
		while (tx0 < tx1 && alpha[tx0] == 0) {
			tx0++;
		}
		if (tx0 == tx1) {
			continue;
		}
		while (alpha[tx1 - 1] == 0) {
			tx1--;
		}

		if (row_read(ctx->p, rectangle->y + ty) != FB_RET_OK) {
			ctx->failed = true;
			return;
		}
		scan_blend(ctx->p, rectangle->x + tx0, alpha + tx0, tx1 - tx0, ctx->pen);
		if (row_write(ctx->p, rectangle->y + ty) != FB_RET_OK) {
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

	/* mcu-renderer renders the string into the coverage buffer in chunks as wide as the buffer allows and
	 * hands each chunk over to text_draw_textbuffer. It clears what it has drawn after every chunk, hence the
	 * buffer stays zeroed between calls. */
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
	mr.buffer = fb_painter_text_buffer;
	mr.buffer_size = fb_painter_text_buffer_size;
	font_set(&mr, font_select(p));

	/* The whole framebuffer is the clipping rectangle, the offset places the top (ascent) left corner of the
	 * text at (x, y). */
	mr_rectangle_t clip = {0, 0, (int16_t)p->w, (int16_t)p->h};
	mr_point_t offset = {x, y};
	xSemaphoreTake(text_buffer_lock, portMAX_DELAY);
	mr_draw_text(&mr, text, &clip, &offset);
	xSemaphoreGive(text_buffer_lock);

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
	font_set(&mr, font_select(p));

	if (w != NULL) {
		*w = (uint16_t)mr_get_text_width(&mr, text);
	}
	if (h != NULL) {
		*h = (uint16_t)mr_get_line_height(&mr);
	}

	return PAINTER_RET_OK;
}


/* Compose one scanline of the rectangle [x0, x1) clipped to the framebuffer in the scratch row as up to three
 * runs. A border row is all pen, any other row is pen on the left and right pw columns with the brush (if set)
 * in between. */
static void rect_fill_row(FbPainter *p, int32_t x0, int32_t x1, int32_t pw, bool border_row, uint32_t pen,
                          uint32_t brush) {
	int32_t cx0 = (x0 < 0) ? 0 : x0;
	int32_t cx1 = (x1 > (int32_t)p->w) ? (int32_t)p->w : x1;
	if (cx0 >= cx1) {
		return;
	}
	if (border_row) {
		scan_fill(p->row, p->mode, cx0, cx1 - cx0, pen);
		return;
	}

	int32_t l = x0 + pw;
	int32_t r = x1 - pw;
	l = (l < cx0) ? cx0 : (l > cx1) ? cx1 : l;
	r = (r < l) ? l : (r > cx1) ? cx1 : r;
	scan_fill(p->row, p->mode, cx0, l - cx0, pen);
	if (p->brush_set) {
		scan_fill(p->row, p->mode, l, r - l, brush);
	}
	scan_fill(p->row, p->mode, r, cx1 - r, pen);
}


/* Fast path for a filled rectangle spanning the whole framebuffer width. Every scanline it covers is
 * fully defined by the rectangle (nothing outside to preserve), and only two distinct scanlines ever
 * occur: an all-pen border row (top/bottom pw rows) and a pen-edges + brush middle row. Each is built
 * once in the scratch row and written to every row that shares it, without reading the framebuffer. */
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
			rect_fill_row(p, x0, x1, pw, true, pen, brush);
			ready = ROW_BORDER;
		} else if (!border_row && ready != ROW_MIDDLE) {
			rect_fill_row(p, x0, x1, pw, false, pen, brush);
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

	/* Half-open rectangle [x0, x1) x [y0, y1), clipped to the framebuffer. */
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
		rect_fill_row(p, x0, x1, pw, pw > 0 && (yy < y0 + pw || yy >= y1 - pw), pen, brush);
		if (row_write(p, yy) != FB_RET_OK) {
			return PAINTER_RET_FAILED;
		}
	}

	return PAINTER_RET_OK;
}


/* Plot a pen-width x pen-width native-colour block centred on (cx, cy) through per-scanline read-modify-write,
 * clipped to the framebuffer. A width of 1 draws a single pixel. */
static painter_ret_t painter_plot_thick(FbPainter *p, int32_t cx, int32_t cy, uint32_t color, int32_t pw) {
	int32_t l = cx - pw / 2;
	int32_t r = l + pw;
	l = (l < 0) ? 0 : l;
	r = (r > (int32_t)p->w) ? (int32_t)p->w : r;
	if (l >= r) {
		return PAINTER_RET_OK;
	}
	for (int32_t yy = cy - pw / 2; yy < cy - pw / 2 + pw; yy++) {
		if (yy < 0 || yy >= (int32_t)p->h) {
			continue;
		}
		if (row_read(p, yy) != FB_RET_OK) {
			return PAINTER_RET_FAILED;
		}
		scan_fill(p->row, p->mode, l, r - l, color);
		if (row_write(p, yy) != FB_RET_OK) {
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

	/* The horizontally visible part of the image [ix0, ix1), the rest is clipped. */
	int32_t ix0 = (x < 0) ? -x : 0;
	int32_t ix1 = ((int32_t)x + (int32_t)image->w > (int32_t)p->w) ? (int32_t)p->w - x : (int32_t)image->w;
	if (ix0 >= ix1) {
		return PAINTER_RET_OK;
	}

	/* When the image is already in the framebuffer's native mode (the common case, as tools/imtocdata.py
	 * generates images in the target mode) each visible row is copied as a single run. Only a genuine mode
	 * mismatch pays for the (lossy) colour conversion, which is inherently done pixel by pixel. Inversion is a
	 * plain bitwise complement: every packed mode encodes intensity/channels directly, and only the relevant
	 * low bits of a value are stored, so ~value negates it. */
	bool inverted = (mode == PAINTER_MODE_INVERTED);
	size_t sbpp = (size_t)image->mode;

	for (size_t iy = 0; iy < image->h; iy++) {
		int32_t yy = y + (int32_t)iy;
		if (yy < 0 || yy >= (int32_t)p->h) {
			continue;
		}
		if (row_read(p, yy) != FB_RET_OK) {
			return PAINTER_RET_FAILED;
		}
		const uint8_t *src_row = image->data + iy * fb_packed_size(image->w, image->mode);
		if (image->mode == p->mode) {
			scan_copy(p->row, x + ix0, src_row, ix0, ix1 - ix0, p->mode, inverted);
		} else {
			for (int32_t ix = ix0; ix < ix1; ix++) {
				uint32_t value = 0;
				for (size_t j = 0; j < (sbpp + 7) / 8; j++) {
					value = (value << 8) | src_row[ix * sbpp / 8 + j];
				}
				if (sbpp < 8) {
					value = (value >> (8 - sbpp - ((ix * sbpp) & 7))) & ((1u << sbpp) - 1);
				}
				value = color_to_native(p, native_to_color(image->mode, value));
				scan_fill(p->row, p->mode, x + ix, 1, inverted ? ~value : value);
			}
		}
		if (row_write(p, yy) != FB_RET_OK) {
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
	 * the result never grows and the buffer cannot overflow. The width is the sum of the glyph advances,
	 * hence the kept prefix is shortened one character at a time by subtracting the advance of the
	 * dropped character instead of measuring the whole string again. */
	size_t len = strlen(text);
	size_t ellipsis = (len < 3) ? len : 3;
	size_t keep = len - ellipsis;
	uint16_t dots_w = 0;
	uint16_t tail_w = 0;
	if (painter_text_size(&self->painter, &"..."[3 - ellipsis], &dots_w, NULL) != PAINTER_RET_OK ||
	    painter_text_size(&self->painter, text + keep, &tail_w, NULL) != PAINTER_RET_OK) {
		return FB_PAINTER_RET_FAILED;
	}
	tw -= tail_w;
	while ((tw + dots_w) > w) {
		if (keep == 0) {
			/* Not even the ellipsis alone fits in the requested width. */
			text[0] = '\0';
			return FB_PAINTER_RET_OK;
		}
		keep--;
		char c[2] = {text[keep], '\0'};
		uint16_t c_w = 0;
		if (painter_text_size(&self->painter, c, &c_w, NULL) != PAINTER_RET_OK) {
			return FB_PAINTER_RET_FAILED;
		}
		tw -= c_w;
	}
	memset(text + keep, '.', ellipsis);
	text[keep + ellipsis] = '\0';

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
	/* The text buffer lock is shared by all instances, the first one creates it. */
	vTaskSuspendAll();
	if (text_buffer_lock == NULL) {
		text_buffer_lock = xSemaphoreCreateMutex();
	}
	xTaskResumeAll();
	if (text_buffer_lock == NULL) {
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
	if (self->lock != NULL) {
		vSemaphoreDelete(self->lock);
		self->lock = NULL;
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
