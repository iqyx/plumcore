/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Simple scrollable text console on a framebuffer device
 *
 * Copyright (c) 2024, Marek Koza (qyx@krtko.org)
 * All rights reserved.
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

const struct imdata im_plum = {
        32,
        30,
        FB_MODE_G2,
        {
                0xff, 0xff, 0x5f, 0xff, 0xff, 0xff, 0xff, 0xff,
                0xff, 0xff, 0x0b, 0xff, 0xff, 0xff, 0xff, 0xff,
                0xff, 0xfe, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff,
                0xff, 0xfe, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff,
                0xff, 0xfe, 0x07, 0xff, 0xaa, 0xaf, 0xff, 0xff,
                0xff, 0xea, 0x42, 0xf9, 0x55, 0x56, 0xff, 0xff,
                0xff, 0xab, 0x80, 0xa5, 0x6a, 0x55, 0x7f, 0xff,
                0xfe, 0xaf, 0xd0, 0x55, 0x5a, 0xa9, 0x5b, 0xff,
                0xfe, 0xbf, 0xe5, 0x55, 0x55, 0xaa, 0x56, 0xff,
                0xfa, 0xff, 0x95, 0x6a, 0x95, 0x6a, 0xa5, 0xbf,
                0xfb, 0xff, 0x56, 0xaa, 0xa9, 0x5a, 0xa9, 0x7f,
                0xeb, 0xfe, 0x5a, 0xaa, 0xaa, 0x56, 0xa9, 0x5f,
                0xef, 0xfd, 0x5a, 0xaa, 0xaa, 0x95, 0xaa, 0x5b,
                0xaf, 0xfd, 0x6a, 0xaa, 0xaa, 0xa5, 0xaa, 0x97,
                0xaf, 0xf9, 0x6a, 0xaa, 0xaa, 0xa5, 0x6a, 0x97,
                0xaf, 0xf9, 0x6a, 0xaa, 0xaa, 0xa9, 0x5a, 0x96,
                0xaf, 0xf9, 0x6a, 0xaa, 0xaa, 0xaa, 0x5a, 0xa6,
                0xaf, 0xea, 0x5a, 0xaa, 0xaa, 0xaa, 0x56, 0xa5,
                0xef, 0xee, 0x5a, 0xaa, 0xaa, 0xaa, 0x96, 0xa5,
                0xeb, 0xaf, 0x5a, 0xaa, 0xaa, 0xaa, 0x96, 0xa5,
                0xfa, 0xaf, 0x96, 0xaa, 0xaa, 0xaa, 0x95, 0xa5,
                0xfe, 0xbf, 0xd6, 0xaa, 0xaa, 0xaa, 0x95, 0x96,
                0xff, 0xbf, 0xd5, 0xaa, 0xaa, 0xaa, 0xa5, 0x97,
                0xff, 0xff, 0xf5, 0xaa, 0xaa, 0xaa, 0xa5, 0x57,
                0xff, 0xff, 0xf9, 0x6a, 0xaa, 0xaa, 0x95, 0x5f,
                0xff, 0xff, 0xfe, 0x5a, 0xaa, 0xaa, 0x95, 0x6f,
                0xff, 0xff, 0xff, 0x95, 0xaa, 0xaa, 0x55, 0xbf,
                0xff, 0xff, 0xff, 0xe5, 0x5a, 0xa5, 0x5b, 0xff,
                0xff, 0xff, 0xff, 0xfe, 0x55, 0x55, 0x7f, 0xff,
                0xff, 0xff, 0xff, 0xff, 0xe9, 0x6b, 0xff, 0xff,
        }
};


/***************************************************************************************************
 * Stream interface API
 ***************************************************************************************************/


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
	*read = 0;
	return STREAM_RET_OK;
}


static stream_ret_t stream_write_timeout(Stream *self, const void *buf, size_t size, size_t *written, uint32_t timeout_ms) {
	/** @todo call stream write */
	stream_ret_t ret = stream_write(self, buf, size);
	*written = size;
	return ret;
}


static stream_ret_t stream_read_timeout(Stream *self, void *buf, size_t size, size_t *read, uint32_t timeout_ms) {
	vTaskDelay(timeout_ms);
	*read = 0;
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


fb_console_ret_t fb_console_init(FbConsole *self, Fb *fb) {
	memset(self, 0, sizeof(FbConsole));

	self->fb = fb;
	self->color = 0x3;
	self->font = nokia_small_data;

	self->lock = xSemaphoreCreateMutex();
	if (self->lock == NULL) {
		goto err;
	}

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

	fb_image(self->fb, 0, 0, &im_plum);

	self->posy = 40;
	self->stream.parent = self;
	self->stream.vmt = &fb_console_stream_vmt;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialized"));
	return FB_CONSOLE_RET_OK;
err:
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot initialize the service"));
	return FB_CONSOLE_RET_FAILED;
}


fb_console_ret_t fb_console_free(FbConsole *self) {
	(void)self;
	return FB_CONSOLE_RET_OK;
}


fb_console_ret_t fb_console_process(FbConsole *self, const void *buf, size_t len) {
	for (size_t i = 0; i < len; i++) {
		char c = ((const char *)buf)[i];

		if ((self->posy + 8) > 160) {
			fb_console_scroll(self, 40, 160, 8);
			self->posy -= 8;
		}
		if (self->state == FB_CONSOLE_NORMAL && c == 0x1b) {
			self->state = FB_CONSOLE_ESC;
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
		if (c == '\n' || (self->posx + 8) >= 240) {
			self->posy += 8;
			self->posx = 0;
			self->fb->vmt->flush(self->fb);
		}
	}

	return FB_CONSOLE_RET_OK;
}


fb_console_ret_t fb_console_scroll(FbConsole *self, size_t r_start, size_t r_end, size_t step) {
	/** @todo handle the width properly */

	for (size_t y = r_start; y < (r_end - step); y++) {
		uint8_t d[60];
		self->fb->vmt->read(self->fb, (y + step) * 60, d, 60, FB_MODE_G2);
		self->fb->vmt->write(self->fb, y * 60, d, 60, FB_MODE_G2);
	}
	for (size_t y = (r_end - step); y < r_end; y++) {
		uint8_t d[60] = {0};
		self->fb->vmt->write(self->fb, y * 60, d, 60, FB_MODE_G2);
	}

	return FB_CONSOLE_RET_OK;
}


fb_ret_t fb_text(Fb *self, const char *text, size_t posx, size_t posy, size_t *advance, uint8_t color, const struct small_char *font) {
	/** @todo handle the width properly */

	char c;
	while (c = *text) {
		for (size_t y = 0; y < 8; y++) {
			uint8_t d[60];
			self->vmt->read(self, (posy + y) * 60, d, 60, FB_MODE_G2);

			for (size_t x = 0; x < font[c - 32].width; x++) {
				if (font[c - 32].rows[y] & (0x80 >> x)) {
					d[(x + posx) / 4] |= ((color & 0x03) << 6) >> (((x + posx) % 4) * 2);
				}
			}

			self->vmt->write(self, (posy + y) * 60, d, 60, FB_MODE_G2);
		}
		posx += font[c - 32].advance;
		if (advance != NULL) {
			*advance += font[c - 32].advance;
		}

		text++;
	}

	return FB_RET_OK;
}


fb_ret_t fb_image(Fb *self, size_t posx, size_t posy, const struct imdata *data) {
	for (size_t y = 0; y < data->h; y++) {
		uint8_t d[60];
		self->vmt->read(self, (posy + y) * 60, d, 60, FB_MODE_G2);

		for (size_t x = 0; x < data->w; x++) {
			d[(x + posx) / 4] = ~(data->data[y * (data->w / 4) + (x / 4)]);
		}

		self->vmt->write(self, (posy + y) * 60, d, 60, FB_MODE_G2);
	}

	return FB_RET_OK;
}
