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
#include <interfaces/event.h>
#include <interfaces/window.h>

typedef enum {
	FB_COMPOSITOR_RET_OK = 0,
	FB_COMPOSITOR_RET_FAILED,
	FB_COMPOSITOR_RET_NULL,
	FB_COMPOSITOR_RET_NOMEM,
} fb_compositor_ret_t;

/* Maximum window title length including the terminating NUL. */
#define FB_COMPOSITOR_WINDOW_TITLE_LEN 32

/* Depth of each window's input event queue. */
#define FB_COMPOSITOR_EVENT_QUEUE_LEN 8

typedef struct fb_compositor FbCompositor;
typedef struct fb_compositor_window FbCompositorWindow;

struct fb_compositor_window {
	Fb fb;                           /* surface the client draws on (backed by buf) */
	Window window;                   /* management interface */
	Event event;                     /* input event source delivered to this window */

	FbCompositor *compositor;
	FbCompositorWindow *next;        /* intrusive list, sorted by z ascending (back to front) */

	QueueHandle_t event_queue;       /* events routed to this window, drained by listen */
	enum event_type event_filter;   /* type bitmask set via subscribe (0 = accept all types) */

	/* Paint session lock: claimed on the first fb write, released on flush. The compositor takes it
	 * around a window's blit so it never reads a half-drawn frame. See window_fb_write/flush. */
	SemaphoreHandle_t paint_lock;
	bool painting;                   /* true while this window's client holds paint_lock */

	uint8_t *buf;                    /* backing store, owned and allocated by the factory */
	size_t buf_size;                 /* must hold the window's largest geometry */
	enum fb_mode mode;               /* mode of the backing store, must match the compositor */

	struct window_geometry geometry; /* current effective geometry */
	struct window_geometry saved;    /* normal geometry saved across maximize */
	enum window_state state;
	int16_t z;
	bool visible;
	char title[FB_COMPOSITOR_WINDOW_TITLE_LEN];
	const struct painter_raw_image *icon; /* borrowed pointer, not owned */
};

typedef struct fb_compositor {
	WindowFactory factory;           /* window create/destroy interface, use &self->factory */

	Fb *out;                         /* output framebuffer (the LCD) */
	size_t out_w;
	size_t out_h;
	enum fb_mode mode;               /* working/composition mode (== out native mode) */

	Event *input;                    /* input event source, may be NULL (no routing) */
	TaskHandle_t input_task;         /* pumps input and routes to the top-level window */

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


/* @p input is the event source routed to the top-level window; pass NULL to disable input routing.
 * Every window exposes its own Event interface (reachable via the Window get_event method). */
fb_compositor_ret_t fb_compositor_init(FbCompositor *self, Fb *out, Event *input);
fb_compositor_ret_t fb_compositor_free(FbCompositor *self);
fb_compositor_ret_t fb_compositor_set_background(FbCompositor *self, uint8_t color);

/* Force an immediate recompose + flush (the render task does this on damage). Windows are created
 * and destroyed through the WindowFactory interface exposed as the factory member. */
fb_compositor_ret_t fb_compositor_render(FbCompositor *self);

/* Return the active (front-most visible, input-receiving) window through @p window, or NULL there if
 * no window is currently on screen. */
fb_compositor_ret_t fb_compositor_get_active_window(FbCompositor *self, Window **window);
