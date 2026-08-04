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
 * - only set_pen, set_brush and rect are implemented (the other vmt entries are NULL),
 * - no dithering, colors are hard-quantised,
 * - rows are assumed byte-aligned (w * bpp is a multiple of 8, true for all real modes here).
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <main.h>
#include <interfaces/fb.h>
#include <interfaces/painter.h>

#include "fb-painter.h"

#define MODULE_NAME "fb-painter"


/***************************************************************************************************
 * Font glyphs
 ***************************************************************************************************/

/* A single 8-row glyph: width is the number of used columns (packed MSB-first in each row byte) and
 * advance is how far the cursor moves after drawing it. */
struct small_char {
	uint8_t width;
	uint8_t advance;
	uint8_t rows[8];
};

#include "nokia-fonts.inc"

/* A font face bundles the glyph tables for each supported style. A missing style (e.g. italic) falls
 * back to the normal table. max_char is the highest ASCII code present in every bundled table. */
struct painter_font {
	const struct small_char *normal;
	const struct small_char *bold;
	const struct small_char *italic;
	uint8_t max_char;
};

/* Built-in default face used whenever the caller has not set an explicit font. */
static const struct painter_font painter_default_font = {
	.normal = nokia_small_data,
	.bold = nokia_small_bold_data,
	.italic = NULL,
	.max_char = 126,
};


static const struct small_char *font_glyphs(const struct painter_font *font, painter_font_style_t style) {
	const struct small_char *glyphs = NULL;

	switch (style) {
		case PAINTER_FONT_BOLD:
			glyphs = font->bold;
			break;
		case PAINTER_FONT_ITALIC:
			glyphs = font->italic;
			break;
		case PAINTER_FONT_NORMAL:
		default:
			glyphs = font->normal;
			break;
	}
	if (glyphs == NULL) {
		glyphs = font->normal;
	}

	return glyphs;
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


static painter_ret_t painter_text(Painter *self, int16_t x, int16_t y, const char *text) {
	FbPainter *p = self->parent;

	if (!p->active) {
		return PAINTER_RET_NOTARGET;
	}
	if (text == NULL) {
		return PAINTER_RET_FAILED;
	}

	const struct painter_font *font = (p->font != NULL) ? (const struct painter_font *)p->font : &painter_default_font;
	const struct small_char *glyphs = font_glyphs(font, p->font_style);
	uint32_t pen = color_to_native(p, p->pen_color);

	/* Glyphs are drawn in the pen color with their top-left corner following the cursor, one 8-row
	 * glyph at a time. Set (lit) pixels are written; the rest of the surface is left untouched. */
	int32_t cx = x;
	for (const char *s = text; *s != '\0'; s++) {
		uint8_t c = (uint8_t)*s;
		if (c < 32 || c > font->max_char) {
			continue;
		}
		const struct small_char *g = &glyphs[c - 32];
		for (uint32_t row = 0; row < 8; row++) {
			if (g->rows[row] == 0) {
				continue;
			}
			int32_t yy = y + (int32_t)row;
			if (yy < 0 || yy >= (int32_t)p->h) {
				continue;
			}
			if (row_read(p, yy) != FB_RET_OK) {
				return PAINTER_RET_FAILED;
			}
			bool dirty = false;
			for (uint32_t col = 0; col < g->width; col++) {
				if ((g->rows[row] & (0x80 >> col)) == 0) {
					continue;
				}
				int32_t xx = cx + (int32_t)col;
				if (xx < 0 || xx >= (int32_t)p->w) {
					continue;
				}
				scan_set_pixel(p->row, p->mode, xx, pen);
				dirty = true;
			}
			if (dirty && row_write(p, yy) != FB_RET_OK) {
				return PAINTER_RET_FAILED;
			}
		}
		cx += g->advance;
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
	.text = painter_text,
	.image = painter_image,
};


/***************************************************************************************************
 * Service API
 ***************************************************************************************************/

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
