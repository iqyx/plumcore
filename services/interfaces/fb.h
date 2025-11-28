/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Display/framebuffer interface
 *
 * Copyright (c) 2024, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
	FB_RET_OK = 0,
	FB_RET_FAILED,
} fb_ret_t;

enum fb_mode {
	FB_MODE_G1 = 1,
	FB_MODE_G2 = 2,
	FB_MODE_G4 = 4,
	FB_MODE_G8 = 8,
	FB_MODE_RGBX222 = 6,
	FB_MODE_RGB565 = 16,
	FB_MODE_RGB888 = 24,
};


typedef struct fb Fb;

struct fb_stat {
	/* Native mode of the framebuffer. */
	enum fb_mode mode;
	size_t w;
	size_t h;

};

struct fb_vmt {
	fb_ret_t (*stat)(Fb *self, struct fb_stat *stat);
	fb_ret_t (*read)(Fb *self, size_t seek, void *buf, size_t len, enum fb_mode mode);
	fb_ret_t (*write)(Fb *self, size_t seek, const void *buf, size_t len, enum fb_mode mode);
	fb_ret_t (*flush)(Fb *self);
};


typedef struct fb {
	const struct fb_vmt *vmt;
	void *parent;
} Fb;

