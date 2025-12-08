/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Simple scrollable text console on a framebuffer device
 *
 * Copyright (c) 2024, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <main.h>
#include <interfaces/fb.h>
#include <interfaces/stream.h>


typedef enum  {
	FB_CONSOLE_RET_OK = 0,
	FB_CONSOLE_RET_FAILED,
} fb_console_ret_t;


struct small_char {
	uint8_t width;
	uint8_t advance;
	uint8_t rows[8];
};

struct imdata {
	size_t w;
	size_t h;
	enum fb_mode mode;
	uint8_t data[];
};

enum fb_console_text_state {
	FB_CONSOLE_NORMAL = 0,
	FB_CONSOLE_ESC,
	FB_CONSOLE_ESC_PARAM,
};

typedef struct fb_console {
	Fb *fb;
	size_t fb_w;
	size_t fb_h;
	int fb_bpp;
	enum fb_mode fb_mode;
	bool auto_update;

	Stream stream;
	size_t posy;
	size_t posx;
	size_t scrolly_start;
	size_t scrolly_end;
	uint8_t color;
	enum fb_console_text_state state;
	const struct small_char *font;
	uint32_t esc_param;

	SemaphoreHandle_t lock;
} FbConsole;


fb_console_ret_t fb_console_init(FbConsole *self, Fb *fb);
fb_console_ret_t fb_console_free(FbConsole *self);
fb_console_ret_t fb_console_set_auto_update(FbConsole *self, bool auto_update);
fb_console_ret_t fb_console_set_scroll(FbConsole *self, size_t start, size_t end);
fb_console_ret_t fb_console_process(FbConsole *self, const void *buf, size_t len);
fb_console_ret_t fb_console_scroll(FbConsole *self, size_t r_start, size_t r_end, size_t step);

fb_ret_t fb_rect(Fb *self, size_t x1, size_t y1, size_t x2, size_t y2, uint8_t color);
fb_ret_t fb_text(Fb *self, const char *text, size_t posx, size_t posy, size_t *advance, uint8_t color, const struct small_char *font);
fb_ret_t fb_image(Fb *self, size_t posx, size_t posy, const struct imdata *data);

fb_console_ret_t fb_console_banner_bootloader(FbConsole *self);
fb_console_ret_t fb_console_banner_bootloader_128_64(FbConsole *self);
