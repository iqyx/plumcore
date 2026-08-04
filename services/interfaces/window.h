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
#include "event.h"

/* Only ever referenced by pointer here (see painter.h for the full definition). */
struct painter_raw_image;

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
	const char *title;               /* borrowed, owned by the window, valid until the next call */
	const struct painter_raw_image *icon; /* borrowed, set via set_icon, NULL if none */
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

	/* Set the window title. The string is copied into the window; passing NULL clears the title. */
	window_ret_t (*set_title)(Window *self, const char *title);

	/* Set the window icon. Only the pointer is stored (no data is copied), so the image must outlive
	 * the window or be replaced/cleared with NULL before it is freed. */
	window_ret_t (*set_icon)(Window *self, const struct painter_raw_image *icon);

	/* Stacking. */
	window_ret_t (*to_front)(Window *self);
	window_ret_t (*to_back)(Window *self);
	window_ret_t (*set_z)(Window *self, int16_t z);

	/* Input focus (reserved: only bookkeeping for now, no routing). */
	window_ret_t (*set_focus)(Window *self);

	/* Drawing surface handed to the client that paints this window. */
	window_ret_t (*get_fb)(Window *self, Fb **fb);

	/* Input event source delivering events routed to this window. Only the top-level window
	 * actually receives events; a window that is not on top simply blocks on listen. */
	window_ret_t (*get_event)(Window *self, Event **event);
};

typedef struct window {
	const struct window_vmt *vmt;
	void *parent;
} Window;


/* A factory creating and destroying windows on some output (e.g. a compositor). The factory owns the
 * window object and its backing pixel buffer, which it allocates itself sized to the requested
 * geometry and in a framebuffer mode of its own choosing. It returns the Window management interface,
 * from which the drawing Fb (carrying the chosen mode) is reachable via get_fb. */
typedef struct window_factory WindowFactory;

struct window_factory_vmt {
	window_ret_t (*create)(WindowFactory *self, const struct window_geometry *geometry, Window **window);
	window_ret_t (*destroy)(WindowFactory *self, Window *window);
};

typedef struct window_factory {
	const struct window_factory_vmt *vmt;
	void *parent;
} WindowFactory;
