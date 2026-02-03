/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Simple scrollable text console on a framebuffer device
 *
 * Copyright (c) 2024, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * This is an extremely simple POC code of a LCD text console with part of the screen
 * scrolling as the text is added. It understands some basic ANSI escape sequences to
 * set bold and normal font and also some colors. Other than that, it most probably
 * won't work for anything useful. There is a bunch of things which need to be
 * addressed before considering this code at least alpha quality:
 *
 * - font rendering needs to be moved to a separate library, probably plumcore-grlib
 * - basic geometric shapes too (drawing frames, "windows", buttons, etc.)
 * - picture displaying too
 * - tool for converting pictures to C arrays needs to be made more generic
 * - making console header configurable and optional, it must be possible to
 *   display custom text and logo
 * - scroll are configurable
 * - display size configurable (atm it only works with a 240x160 px display)
 * - Fb mode agnostic (now it works only in 2bpp G2 mode)
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <main.h>
#include <interfaces/stream.h>
#include <interfaces/fb.h>

#include "fb-console.h"

#define MODULE_NAME "fb-console"

#include "nokia-fonts.inc"
#include "plum-pictures.inc"


/**********************************************************************************************************************
 * Stream interface API
 **********************************************************************************************************************/

static stream_ret_t stream_write(Stream *self, const void *buf, size_t size) {
	if (u_assert(self != NULL) ||
	    u_assert(buf != NULL)) {
		return STREAM_RET_FAILED;
	}
	FbConsole *fb_console = self->parent;

	xSemaphoreTake(fb_console->lock, portMAX_DELAY);
	fb_console_process(fb_console, buf, size);
	xSemaphoreGive(fb_console->lock);

	return STREAM_RET_OK;
}


static stream_ret_t stream_read(Stream *self, void *buf, size_t size, size_t *read) {
	(void)self;
	(void)buf;
	(void)size;

	/* Always return immediately with no character available. */
	*read = 0;

	return STREAM_RET_OK;
}


static stream_ret_t stream_write_timeout(Stream *self, const void *buf, size_t size, size_t *written, uint32_t timeout_ms) {
	(void)timeout_ms;

	stream_ret_t ret = stream_write(self, buf, size);
	*written = size;
	return ret;
}


static stream_ret_t stream_read_timeout(Stream *self, void *buf, size_t size, size_t *read, uint32_t timeout_ms) {
	(void)self;

	/* Always return timeout after the specified time (simulating no characters on the input). */
	vTaskDelay(timeout_ms);
	*read = 0;

	/** @todo why? */
	if (size >= 1) {
		*((char *)buf) = '\0';
	}

	return STREAM_RET_TIMEOUT;
}


static const struct stream_vmt fb_console_stream_vmt = {
	.write = stream_write,
	.read = stream_read,
	.write_timeout = stream_write_timeout,
	.read_timeout = stream_read_timeout
};


/**********************************************************************************************************************
 * Service implementation
 **********************************************************************************************************************/

fb_console_ret_t fb_console_init(FbConsole *self, Fb *fb) {
	memset(self, 0, sizeof(FbConsole));

	self->fb = fb;
	self->auto_update = true;

	struct fb_stat stat = {0};
	if (fb->vmt->stat != NULL && self->fb->vmt->stat(self->fb, &stat) == FB_RET_OK) {
		self->fb_w = stat.w;
		self->fb_h = stat.h;
		self->fb_mode = stat.mode;
		self->fb_bpp = (int)stat.mode;
	} else {
		self->fb_w = 240;
		self->fb_h = 160;
		self->fb_bpp = 2;
	}

	self->color = 0x3;
	self->font = nokia_small_data;

	self->lock = xSemaphoreCreateMutex();
	if (self->lock == NULL) {
		goto err;
	}

	self->stream.parent = self;
	self->stream.vmt = &fb_console_stream_vmt;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialized, fb dev (%lu x %lu), %d bpp"), self->fb_w, self->fb_h, self->fb_bpp);
	return FB_CONSOLE_RET_OK;
err:
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot initialize the service"));
	return FB_CONSOLE_RET_FAILED;
}


fb_console_ret_t fb_console_free(FbConsole *self) {
	(void)self;

	return FB_CONSOLE_RET_OK;
}


fb_console_ret_t fb_console_set_auto_update(FbConsole *self, bool auto_update) {
	self->auto_update = auto_update;

	return FB_CONSOLE_RET_OK;
}


fb_console_ret_t fb_console_set_scroll(FbConsole *self, size_t start, size_t end) {
	self->posy = start;
	self->scrolly_start = start;
	self->scrolly_end = end;

	return FB_CONSOLE_RET_OK;
}


fb_console_ret_t fb_console_process(FbConsole *self, const void *buf, size_t len) {
	for (size_t i = 0; i < len; i++) {
		char c = ((const char *)buf)[i];

		if ((self->posy + 8) > self->fb_h) {
			fb_console_scroll(self, self->scrolly_start, self->scrolly_end, 8);
			self->posy -= 8;
		}
		if (self->state == FB_CONSOLE_NORMAL && c == 0x1b) {
			self->state = FB_CONSOLE_ESC;
			continue;
		}
		if (self->state == FB_CONSOLE_ESC && c == 'c') {
			/* Clear screen and reset the cursor. */
			self->state = FB_CONSOLE_NORMAL;
			self->posx = 0;
			self->posy = self->scrolly_start;
			fb_rect(self->fb, 0, self->scrolly_start, self->fb_w - 1, self->scrolly_end - 1, 0);
			if (self->auto_update) {
				self->fb->vmt->flush(self->fb);
			}
			continue;
		}
		if (self->state == FB_CONSOLE_ESC && c == 'u') {
			/* Update the screen if auto-update is off. */
			self->state = FB_CONSOLE_NORMAL;
			self->fb->vmt->flush(self->fb);
			continue;
		}
		if (self->state == FB_CONSOLE_ESC && c == '[') {
			self->state = FB_CONSOLE_ESC_PARAM;
			self->esc_param = 0;
			continue;
		}
		if (self->state == FB_CONSOLE_ESC_PARAM && c >= '0' && c <= '9') {
			self->esc_param = self->esc_param * 10 + (c - '0');
			/* Do not change the state here. */
			continue;
		}
		if (self->state == FB_CONSOLE_ESC_PARAM && c == 'm') {
			/* Process font and colors here. */
			if (self->esc_param == 1) {
				self->font = nokia_small_bold_data;
			} else if (self->esc_param == 0) {
				self->font = nokia_small_data;
				self->color = 3;
			} else if (self->esc_param >= 31 && self->esc_param <= 36) {
				/* Colored output, make it look specific. */
				self->color = 1;
			} else if (self->esc_param >= 37 && self->esc_param <= 39) {
				/* White output, reset to the default. */
				self->color = 3;
			}
			self->state = FB_CONSOLE_NORMAL;
			continue;
		}
		if (self->state == FB_CONSOLE_ESC_PARAM && c == 'K') {
			/* Line erase functions. */
			switch (self->esc_param) {
				default:
				case 0:
					fb_rect(self->fb, self->posx, self->posy, self->fb_w - 1, self->posy + 7, 0);
					break;
				case 1:
					fb_rect(self->fb, 0, self->posy, self->posx - 1, self->posy + 7, 0);
					break;
			}
			self->state = FB_CONSOLE_NORMAL;
			continue;
		}

		if (self->state == FB_CONSOLE_ESC_PARAM && c < '0' && c > '9') {
			/* Anything else than a number causes ignoring the ESC. */
			self->state = FB_CONSOLE_NORMAL;
			continue;
		}
		if (self->state != FB_CONSOLE_NORMAL) {
			/* Ignore all non-printable parts. */
			continue;
		}
		if (c >= 32) {
			const char text[2] = {c, '\0'};
			fb_text(self->fb, text, self->posx, self->posy, &self->posx, self->color, self->font);
		}
		if (c == '\r') {
			self->posx = 0;
			if (self->auto_update) {
				self->fb->vmt->flush(self->fb);
			}
		}
		if (c == '\n' || (self->posx + 8) >= self->fb_w) {
			self->posy += 8;
			self->posx = 0;
			if (self->auto_update) {
				self->fb->vmt->flush(self->fb);
			}
		}
	}

	return FB_CONSOLE_RET_OK;
}


fb_console_ret_t fb_console_scroll(FbConsole *self, size_t r_start, size_t r_end, size_t step) {
	/** @todo handle the width properly */
	size_t lb = self->fb_w * self->fb_mode / 8;

	for (size_t y = r_start; y < (r_end - step); y++) {
		uint8_t d[lb];
		self->fb->vmt->read(self->fb, (y + step) * lb, d, lb, self->fb_mode);
		self->fb->vmt->write(self->fb, y * lb, d, lb, self->fb_mode);
	}
	for (size_t y = (r_end - step); y < r_end; y++) {
		uint8_t d[lb];
		memset(d, 0, lb);
		self->fb->vmt->write(self->fb, y * lb, d, lb, self->fb_mode);
	}

	return FB_CONSOLE_RET_OK;
}


/**********************************************************************************************************************
 * Basic drawing primitives (text and image)
 **********************************************************************************************************************/

fb_ret_t fb_rect(Fb *self, size_t x1, size_t y1, size_t x2, size_t y2, uint8_t color) {
	struct fb_stat stat = {0};
	if (self->vmt->stat(self, &stat) != FB_RET_OK) {
		return FB_RET_FAILED;
	}

	for (size_t y  = y1; y <= y2; y++) {
		size_t lb = stat.w * stat.mode / 8;
		uint8_t d[lb];

		self->vmt->read(self, y * lb, d, lb, stat.mode);
		for (size_t x  = x1; x <= x2; x++) {
			/** @todo optimize a bit */
			d[x * stat.mode / 8] &= ~(((0x01 << stat.mode) - 1) << (8 - stat.mode)) >> ((x % (8 / stat.mode)) * stat.mode);
			d[x * stat.mode / 8] |= ((color & ((0x01 << stat.mode) - 1)) << (8 - stat.mode)) >> ((x % (8 / stat.mode)) * stat.mode);
		}
		self->vmt->write(self, y * lb, d, lb, stat.mode);
	}

	return FB_RET_OK;
}


/**
 * @brief Render simple text on a framebuffer device
 *
 * @todo move to plumcore-grlib library
 */
fb_ret_t fb_text(Fb *self, const char *text, size_t posx, size_t posy, size_t *advance, uint8_t color, const struct small_char *font) {
	/** @todo handle the width properly */

	struct fb_stat stat = {0};
	if (self->vmt->stat(self, &stat) != FB_RET_OK) {
		return FB_RET_FAILED;
	}

	char c;
	while ((c = *text) != 0) {
		for (size_t y = 0; y < 8; y++) {
			size_t lb = stat.w * stat.mode / 8;
			uint8_t d[lb];

			self->vmt->read(self, (posy + y) * lb, d, lb, stat.mode);
			for (size_t x = 0; x < font[c - 32].width; x++) {
				if (font[c - 32].rows[y] & (0x80 >> x)) {
					d[(x + posx) * stat.mode / 8] |= ((color & ((0x01 << stat.mode) - 1)) << (8 - stat.mode)) >> (((x + posx) % (8 / stat.mode)) * stat.mode);
				}
			}
			self->vmt->write(self, (posy + y) * lb, d, lb, stat.mode);
		}
		posx += font[c - 32].advance;
		if (advance != NULL) {
			*advance += font[c - 32].advance;
		}

		text++;
	}

	return FB_RET_OK;
}


/**
 * @brief Render simple uncompressed image on a framebuffer device
 *
 * @todo move to the plumcore-grlib library
 */
fb_ret_t fb_image(Fb *self, size_t posx, size_t posy, const struct imdata *data) {
	struct fb_stat stat = {0};
	if (self->vmt->stat(self, &stat) != FB_RET_OK) {
		return FB_RET_FAILED;
	}

	for (size_t y = 0; y < data->h; y++) {
		size_t lb = stat.w * stat.mode / 8;
		uint8_t d[lb];
		self->vmt->read(self, (posy + y) * lb, d, lb, stat.mode);

		for (size_t x = 0; x < data->w; x++) {
			d[(x + posx) * stat.mode / 8] = ~(data->data[y * (data->w * stat.mode / 8) + (x * stat.mode / 8)]);
		}

		self->vmt->write(self, (posy + y) * lb, d, lb, stat.mode);
	}

	return FB_RET_OK;
}


/**********************************************************************************************************************
 * Some standard banners
 **********************************************************************************************************************/

fb_console_ret_t fb_console_banner_bootloader(FbConsole *self) {
	if (self->fb_mode == FB_MODE_G1) {
		fb_image(self->fb, 0, 0, &im_plum_g1);
	} else if (self->fb_mode == FB_MODE_G2) {
		fb_image(self->fb, 0, 0, &im_plum_g2);
	};

	char s[64] = {0};
	strlcat(s, PORT_BANNER, sizeof(s));
	strlcat(s, " (", sizeof(s));
	strlcat(s, PORT_NAME, sizeof(s));
	strlcat(s, ")", sizeof(s));
	fb_text(self->fb, s, 40, 0, NULL, 3, nokia_small_bold_data);

	s[0] = '\0';
	strlcat(s, UMESH_VERSION, sizeof(s));
	fb_text(self->fb, s, 40, 10, NULL, 3, nokia_small_data);

	s[0] = '\0';
	strlcat(s, "App: ", sizeof(s));
	strlcat(s, CONFIG_APP_NAME, sizeof(s));
	fb_text(self->fb, s, 40, 22, NULL, 3, nokia_small_data);

	fb_console_set_scroll(self, 40, self->fb_h);
	self->fb->vmt->flush(self->fb);

	return FB_CONSOLE_RET_OK;
}


fb_console_ret_t fb_console_banner_bootloader_128_64(FbConsole *self) {
	char s[64] = {0};
	strlcat(s, PORT_BANNER, sizeof(s));
	strlcat(s, " (", sizeof(s));
	strlcat(s, PORT_NAME, sizeof(s));
	strlcat(s, ")", sizeof(s));
	fb_text(self->fb, s, 0, 0, NULL, 3, nokia_small_bold_data);

	s[0] = '\0';
	strlcat(s, UMESH_VERSION, sizeof(s));
	fb_text(self->fb, s, 0, 10, NULL, 3, nokia_small_data);
	fb_text(self->fb, "________________________________", 0, 12, NULL, 3, nokia_small_data);

	if (self->fb_mode == FB_MODE_G1) {
		fb_image(self->fb, self->fb_w / 2 - 16, 28, &im_plum_g1);
	} else if (self->fb_mode == FB_MODE_G2) {
		fb_image(self->fb, self->fb_w / 2 - 16, 28, &im_plum_g2);
	};

	fb_console_set_scroll(self, 24, self->fb_h);
	self->posy = self->fb_h;
	self->fb->vmt->flush(self->fb);

	return FB_CONSOLE_RET_OK;
}


