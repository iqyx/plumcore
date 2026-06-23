/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Framebuffer compositor service
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <main.h>
#include <interfaces/fb.h>
#include <interfaces/window.h>

typedef enum {
	FB_COMPOSITOR_RET_OK = 0,
	FB_COMPOSITOR_RET_FAILED,
	FB_COMPOSITOR_RET_NULL,
	FB_COMPOSITOR_RET_NOMEM,
} fb_compositor_ret_t;

typedef struct fb_compositor FbCompositor;
typedef struct fb_compositor_window FbCompositorWindow;

struct fb_compositor_window {
	Fb fb;                           /* surface the client draws on (backed by buf) */
	Window window;                   /* management interface */

	FbCompositor *compositor;
	FbCompositorWindow *next;        /* intrusive list, sorted by z ascending (back to front) */

	uint8_t *buf;                    /* caller-provided backing store */
	size_t buf_size;                 /* must hold the window's largest geometry */
	enum fb_mode mode;               /* mode of the backing store, must match the compositor */

	struct window_geometry geometry; /* current effective geometry */
	struct window_geometry saved;    /* normal geometry saved across maximize */
	enum window_state state;
	int16_t z;
	bool visible;
};

typedef struct fb_compositor {
	WindowFactory factory;           /* window create/destroy interface, use &self->factory */

	Fb *out;                         /* output framebuffer (the LCD) */
	size_t out_w;
	size_t out_h;
	enum fb_mode mode;               /* working/composition mode (== out native mode) */

	uint8_t *scratch;                /* full-screen composing buffer */
	size_t scratch_size;

	FbCompositorWindow *windows;     /* z-sorted, head = back, tail = front */
	FbCompositorWindow *focused;     /* reserved for a future input router */
	uint8_t bg_color;                /* fill where no window covers */

	TaskHandle_t task;
	SemaphoreHandle_t damaged;       /* given on any window flush / layout change */
	SemaphoreHandle_t lock;          /* guards the window list and scratch buffer */
	bool can_run;
	bool running;
} FbCompositor;


fb_compositor_ret_t fb_compositor_init(FbCompositor *self, Fb *out);
fb_compositor_ret_t fb_compositor_free(FbCompositor *self);
fb_compositor_ret_t fb_compositor_set_background(FbCompositor *self, uint8_t color);

/* Force an immediate recompose + flush (the render task does this on damage). Windows are created
 * and destroyed through the WindowFactory interface exposed as the factory member. */
fb_compositor_ret_t fb_compositor_render(FbCompositor *self);
