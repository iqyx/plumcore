/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Window management interface
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "fb.h"

typedef enum {
	WINDOW_RET_OK = 0,
	WINDOW_RET_FAILED,
	WINDOW_RET_NOMEM,                /* requested geometry does not fit the backing buffer */
} window_ret_t;

enum window_state {
	WINDOW_STATE_NORMAL = 0,         /* floating, using its normal geometry */
	WINDOW_STATE_MINIMIZED,          /* not composed at all */
	WINDOW_STATE_MAXIMIZED,          /* covers the whole output */
};

/* Position is signed so a window may hang off any edge; size is unsigned. */
struct window_geometry {
	int16_t x;                       /* left, in output pixels */
	int16_t y;                       /* top, in output pixels */
	uint16_t w;
	uint16_t h;
};

struct window_stat {
	struct window_geometry geometry; /* current effective geometry */
	enum window_state state;
	int16_t z;                       /* stacking order, higher = closer to viewer */
	bool visible;
	bool focused;                    /* reserved for a future input router */
};

typedef struct window Window;

struct window_vmt {
	window_ret_t (*stat)(Window *self, struct window_stat *stat);

	/* Geometry (operates on the normal geometry). */
	window_ret_t (*move)(Window *self, int16_t x, int16_t y);
	window_ret_t (*resize)(Window *self, uint16_t w, uint16_t h);
	window_ret_t (*set_geometry)(Window *self, const struct window_geometry *geometry);

	/* State. */
	window_ret_t (*maximize)(Window *self);
	window_ret_t (*minimize)(Window *self);
	window_ret_t (*restore)(Window *self);   /* back to the saved normal geometry */
	window_ret_t (*show)(Window *self, bool visible);

	/* Stacking. */
	window_ret_t (*to_front)(Window *self);
	window_ret_t (*to_back)(Window *self);
	window_ret_t (*set_z)(Window *self, int16_t z);

	/* Input focus (reserved: only bookkeeping for now, no routing). */
	window_ret_t (*set_focus)(Window *self);

	/* Drawing surface handed to the client that paints this window. */
	window_ret_t (*get_fb)(Window *self, Fb **fb);
};

typedef struct window {
	const struct window_vmt *vmt;
	void *parent;
} Window;


/* A factory creating and destroying windows on some output (e.g. a compositor). The caller provides
 * the backing pixel buffer; the factory owns the window object itself and returns its Window
 * management interface, from which the drawing Fb is reachable via get_fb. */
typedef struct window_factory WindowFactory;

struct window_factory_vmt {
	window_ret_t (*create)(WindowFactory *self, const struct window_geometry *geometry,
	                       void *buf, size_t buf_size, enum fb_mode mode, Window **window);
	window_ret_t (*destroy)(WindowFactory *self, Window *window);
};

typedef struct window_factory {
	const struct window_factory_vmt *vmt;
	void *parent;
} WindowFactory;
