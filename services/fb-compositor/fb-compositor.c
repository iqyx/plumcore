/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Framebuffer compositor service
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * A simple compositor for small LCDs. It manages a stack of rectangular windows, each backed by its
 * own framebuffer, and composes them onto a single output Fb device. Every window exposes an Fb
 * interface (a drawing surface for the client) and a Window interface (for layout management).
 *
 * v1 limitations, to be lifted later:
 * - all windows and the output share the same fb_mode (no per-window mode conversion),
 * - the whole screen is recomposed on every damage event (no damage-region tracking),
 * - windows are fully opaque (no alpha or color-key transparency),
 * - the background color is a raw pixel value (only meaningful for gray/indexed modes).
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <main.h>
#include <interfaces/fb.h>
#include <interfaces/window.h>

#include "fb-compositor.h"

#define MODULE_NAME "fb-compositor"

/* One input event queued for delivery to a window. */
struct window_event {
	enum event_type type;
	enum event_code code;
	int32_t value;
};


/***************************************************************************************************
 * Packed framebuffer pixel helpers
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


/* Size in bytes of a packed w*h framebuffer in the given mode. */
static size_t fb_buf_size(size_t w, size_t h, enum fb_mode mode) {
	return (w * h * (size_t)mode + 7) / 8;
}


static uint32_t fb_get_pixel(const uint8_t *buf, size_t width, enum fb_mode mode, size_t x, size_t y) {
	size_t bpp = (size_t)mode;
	size_t idx = y * width + x;

	if (bpp >= 8) {
		size_t bytes = bpp / 8;
		size_t off = idx * bytes;
		uint32_t v = 0;
		for (size_t i = 0; i < bytes; i++) {
			v = (v << 8) | buf[off + i];
		}
		return v;
	}

	/* Sub-byte modes are packed MSB-first (leftmost pixel in the high bits). */
	size_t ppb = 8 / bpp;
	size_t off = idx / ppb;
	size_t sub = idx % ppb;
	size_t shift = 8 - bpp * (sub + 1);
	uint32_t mask = (1u << bpp) - 1;
	return (buf[off] >> shift) & mask;
}


static void fb_set_pixel(uint8_t *buf, size_t width, enum fb_mode mode, size_t x, size_t y, uint32_t value) {
	size_t bpp = (size_t)mode;
	size_t idx = y * width + x;

	if (bpp >= 8) {
		size_t bytes = bpp / 8;
		size_t off = idx * bytes;
		for (size_t i = 0; i < bytes; i++) {
			buf[off + bytes - 1 - i] = value & 0xff;
			value >>= 8;
		}
		return;
	}

	size_t ppb = 8 / bpp;
	size_t off = idx / ppb;
	size_t sub = idx % ppb;
	size_t shift = 8 - bpp * (sub + 1);
	uint32_t mask = (1u << bpp) - 1;
	buf[off] = (buf[off] & ~(mask << shift)) | ((value & mask) << shift);
}


/* Fill a whole packed framebuffer with a single pixel value. */
static void fb_fill(uint8_t *buf, size_t w, size_t h, enum fb_mode mode, uint32_t value) {
	size_t bpp = (size_t)mode;
	size_t size = fb_buf_size(w, h, mode);

	if (bpp <= 8) {
		uint32_t mask = (1u << bpp) - 1;
		uint8_t b = 0;
		for (size_t s = 0; s < 8 / bpp; s++) {
			b = (b << bpp) | (value & mask);
		}
		memset(buf, b, size);
		return;
	}

	for (size_t y = 0; y < h; y++) {
		for (size_t x = 0; x < w; x++) {
			fb_set_pixel(buf, w, mode, x, y, value);
		}
	}
}


/***************************************************************************************************
 * Window list management (sorted by z ascending, head = back, tail = front)
 ***************************************************************************************************/

static void window_list_remove(FbCompositor *self, FbCompositorWindow *win) {
	FbCompositorWindow **pp = &self->windows;
	while (*pp != NULL) {
		if (*pp == win) {
			*pp = win->next;
			win->next = NULL;
			return;
		}
		pp = &(*pp)->next;
	}
}


/* Insert keeping z ascending; among equal z the new window goes on top (after the equals). */
static void window_list_insert(FbCompositor *self, FbCompositorWindow *win) {
	FbCompositorWindow **pp = &self->windows;
	while (*pp != NULL && (*pp)->z <= win->z) {
		pp = &(*pp)->next;
	}
	win->next = *pp;
	*pp = win;
}


static FbCompositorWindow *window_list_tail(FbCompositor *self) {
	FbCompositorWindow *tail = self->windows;
	while (tail != NULL && tail->next != NULL) {
		tail = tail->next;
	}
	return tail;
}


/* The top-level window: the front-most (highest z) window that is actually on screen. Input events
 * are routed here only. The list is back-to-front, so the last visible match wins. */
static FbCompositorWindow *window_list_top(FbCompositor *self) {
	FbCompositorWindow *top = NULL;
	for (FbCompositorWindow *win = self->windows; win != NULL; win = win->next) {
		if (win->visible && win->state != WINDOW_STATE_MINIMIZED) {
			top = win;
		}
	}
	return top;
}


static void compositor_damage(FbCompositor *self) {
	if (self->damaged != NULL) {
		xSemaphoreGive(self->damaged);
	}
}


/* Drop a window back to its normal (non-maximized) geometry. The caller updates the geometry. */
static void window_ensure_normal(FbCompositorWindow *win) {
	if (win->state == WINDOW_STATE_MAXIMIZED) {
		win->geometry = win->saved;
	}
	win->state = WINDOW_STATE_NORMAL;
}


/***************************************************************************************************
 * Window framebuffer interface (the drawing surface)
 ***************************************************************************************************/

static fb_ret_t window_fb_stat(Fb *self, struct fb_stat *stat) {
	FbCompositorWindow *win = self->parent;

	if (stat == NULL) {
		return FB_RET_FAILED;
	}
	stat->mode = win->mode;
	stat->w = win->geometry.w;
	stat->h = win->geometry.h;

	return FB_RET_OK;
}


static fb_ret_t window_fb_write(Fb *self, size_t seek, const void *buf, size_t len, enum fb_mode mode) {
	FbCompositorWindow *win = self->parent;

	/* No per-window mode conversion yet. */
	if (mode != win->mode) {
		return FB_RET_FAILED;
	}
	size_t cur_size = fb_buf_size(win->geometry.w, win->geometry.h, win->mode);
	if (seek >= cur_size || (seek + len) > cur_size) {
		return FB_RET_FAILED;
	}

	xSemaphoreTake(win->compositor->lock, portMAX_DELAY);
	memcpy(win->buf + seek, buf, len);
	xSemaphoreGive(win->compositor->lock);

	return FB_RET_OK;
}


static fb_ret_t window_fb_read(Fb *self, size_t seek, void *buf, size_t len, enum fb_mode mode) {
	FbCompositorWindow *win = self->parent;

	if (mode != win->mode) {
		return FB_RET_FAILED;
	}
	size_t cur_size = fb_buf_size(win->geometry.w, win->geometry.h, win->mode);
	if (seek >= cur_size || (seek + len) > cur_size) {
		return FB_RET_FAILED;
	}

	xSemaphoreTake(win->compositor->lock, portMAX_DELAY);
	memcpy(buf, win->buf + seek, len);
	xSemaphoreGive(win->compositor->lock);

	return FB_RET_OK;
}


static fb_ret_t window_fb_flush(Fb *self) {
	FbCompositorWindow *win = self->parent;

	/* Presenting a window just requests a recompose of the whole screen. */
	compositor_damage(win->compositor);

	return FB_RET_OK;
}


static const struct fb_vmt window_fb_vmt = {
	.stat = window_fb_stat,
	.read = window_fb_read,
	.write = window_fb_write,
	.flush = window_fb_flush,
};


/***************************************************************************************************
 * Window input event interface (events routed to the window by the compositor)
 ***************************************************************************************************/

static event_ret_t window_event_listen(Event *self, enum event_type *type, enum event_code *code, int32_t *value) {
	FbCompositorWindow *win = self->parent;
	struct window_event ev = {0};

	if (xQueueReceive(win->event_queue, &ev, portMAX_DELAY) != pdTRUE) {
		return EV_RET_FAILED;
	}
	if (type != NULL) {
		*type = ev.type;
	}
	if (code != NULL) {
		*code = ev.code;
	}
	if (value != NULL) {
		*value = ev.value;
	}

	return EV_RET_OK;
}


static event_ret_t window_event_subscribe(Event *self, enum event_type *type) {
	FbCompositorWindow *win = self->parent;

	if (type == NULL) {
		return EV_RET_FAILED;
	}
	/* Store the type bitmask; the dispatcher drops events whose type is not requested. */
	win->event_filter = *type;

	return EV_RET_OK;
}


static const struct event_vmt window_event_vmt = {
	.listen = window_event_listen,
	.subscribe = window_event_subscribe,
};


/***************************************************************************************************
 * Window management interface
 ***************************************************************************************************/

static window_ret_t window_mgmt_stat(Window *self, struct window_stat *stat) {
	FbCompositorWindow *win = self->parent;

	if (stat == NULL) {
		return WINDOW_RET_FAILED;
	}
	xSemaphoreTake(win->compositor->lock, portMAX_DELAY);
	stat->geometry = win->geometry;
	stat->state = win->state;
	stat->z = win->z;
	stat->visible = win->visible;
	stat->focused = (win->compositor->focused == win);
	stat->title = win->title;
	stat->icon = win->icon;
	xSemaphoreGive(win->compositor->lock);

	return WINDOW_RET_OK;
}


static window_ret_t window_mgmt_set_title(Window *self, const char *title) {
	FbCompositorWindow *win = self->parent;

	xSemaphoreTake(win->compositor->lock, portMAX_DELAY);
	strlcpy(win->title, (title != NULL) ? title : "", sizeof(win->title));
	xSemaphoreGive(win->compositor->lock);

	return WINDOW_RET_OK;
}


static window_ret_t window_mgmt_set_icon(Window *self, const struct painter_raw_image *icon) {
	FbCompositorWindow *win = self->parent;

	/* Store the borrowed pointer only, no pixel data is copied. */
	xSemaphoreTake(win->compositor->lock, portMAX_DELAY);
	win->icon = icon;
	xSemaphoreGive(win->compositor->lock);

	return WINDOW_RET_OK;
}


static window_ret_t window_mgmt_move(Window *self, int16_t x, int16_t y) {
	FbCompositorWindow *win = self->parent;

	xSemaphoreTake(win->compositor->lock, portMAX_DELAY);
	window_ensure_normal(win);
	win->geometry.x = x;
	win->geometry.y = y;
	xSemaphoreGive(win->compositor->lock);

	compositor_damage(win->compositor);
	return WINDOW_RET_OK;
}


static window_ret_t window_mgmt_resize(Window *self, uint16_t w, uint16_t h) {
	FbCompositorWindow *win = self->parent;

	if (fb_buf_size(w, h, win->mode) > win->buf_size) {
		return WINDOW_RET_NOMEM;
	}
	xSemaphoreTake(win->compositor->lock, portMAX_DELAY);
	window_ensure_normal(win);
	win->geometry.w = w;
	win->geometry.h = h;
	xSemaphoreGive(win->compositor->lock);

	compositor_damage(win->compositor);
	return WINDOW_RET_OK;
}


static window_ret_t window_mgmt_set_geometry(Window *self, const struct window_geometry *geometry) {
	FbCompositorWindow *win = self->parent;

	if (geometry == NULL) {
		return WINDOW_RET_FAILED;
	}
	if (fb_buf_size(geometry->w, geometry->h, win->mode) > win->buf_size) {
		return WINDOW_RET_NOMEM;
	}
	xSemaphoreTake(win->compositor->lock, portMAX_DELAY);
	win->state = WINDOW_STATE_NORMAL;
	win->geometry = *geometry;
	xSemaphoreGive(win->compositor->lock);

	compositor_damage(win->compositor);
	return WINDOW_RET_OK;
}


static window_ret_t window_mgmt_maximize(Window *self) {
	FbCompositorWindow *win = self->parent;
	FbCompositor *c = win->compositor;

	if (fb_buf_size(c->out_w, c->out_h, win->mode) > win->buf_size) {
		return WINDOW_RET_NOMEM;
	}
	xSemaphoreTake(c->lock, portMAX_DELAY);
	if (win->state != WINDOW_STATE_MAXIMIZED) {
		win->saved = win->geometry;
	}
	win->geometry.x = 0;
	win->geometry.y = 0;
	win->geometry.w = c->out_w;
	win->geometry.h = c->out_h;
	win->state = WINDOW_STATE_MAXIMIZED;
	xSemaphoreGive(c->lock);

	compositor_damage(c);
	return WINDOW_RET_OK;
}


static window_ret_t window_mgmt_minimize(Window *self) {
	FbCompositorWindow *win = self->parent;

	xSemaphoreTake(win->compositor->lock, portMAX_DELAY);
	win->state = WINDOW_STATE_MINIMIZED;
	xSemaphoreGive(win->compositor->lock);

	compositor_damage(win->compositor);
	return WINDOW_RET_OK;
}


static window_ret_t window_mgmt_restore(Window *self) {
	FbCompositorWindow *win = self->parent;

	xSemaphoreTake(win->compositor->lock, portMAX_DELAY);
	if (win->state == WINDOW_STATE_MAXIMIZED) {
		win->geometry = win->saved;
	}
	win->state = WINDOW_STATE_NORMAL;
	xSemaphoreGive(win->compositor->lock);

	compositor_damage(win->compositor);
	return WINDOW_RET_OK;
}


static window_ret_t window_mgmt_show(Window *self, bool visible) {
	FbCompositorWindow *win = self->parent;

	xSemaphoreTake(win->compositor->lock, portMAX_DELAY);
	win->visible = visible;
	xSemaphoreGive(win->compositor->lock);

	compositor_damage(win->compositor);
	return WINDOW_RET_OK;
}


static window_ret_t window_mgmt_to_front(Window *self) {
	FbCompositorWindow *win = self->parent;
	FbCompositor *c = win->compositor;

	xSemaphoreTake(c->lock, portMAX_DELAY);
	window_list_remove(c, win);
	FbCompositorWindow *tail = window_list_tail(c);
	win->z = (tail != NULL) ? (int16_t)(tail->z + 1) : 0;
	window_list_insert(c, win);
	xSemaphoreGive(c->lock);

	compositor_damage(c);
	return WINDOW_RET_OK;
}


static window_ret_t window_mgmt_to_back(Window *self) {
	FbCompositorWindow *win = self->parent;
	FbCompositor *c = win->compositor;

	xSemaphoreTake(c->lock, portMAX_DELAY);
	window_list_remove(c, win);
	win->z = (c->windows != NULL) ? (int16_t)(c->windows->z - 1) : 0;
	window_list_insert(c, win);
	xSemaphoreGive(c->lock);

	compositor_damage(c);
	return WINDOW_RET_OK;
}


static window_ret_t window_mgmt_set_z(Window *self, int16_t z) {
	FbCompositorWindow *win = self->parent;
	FbCompositor *c = win->compositor;

	xSemaphoreTake(c->lock, portMAX_DELAY);
	window_list_remove(c, win);
	win->z = z;
	window_list_insert(c, win);
	xSemaphoreGive(c->lock);

	compositor_damage(c);
	return WINDOW_RET_OK;
}


static window_ret_t window_mgmt_set_focus(Window *self) {
	FbCompositorWindow *win = self->parent;

	xSemaphoreTake(win->compositor->lock, portMAX_DELAY);
	win->compositor->focused = win;
	xSemaphoreGive(win->compositor->lock);

	return WINDOW_RET_OK;
}


static window_ret_t window_mgmt_get_fb(Window *self, Fb **fb) {
	FbCompositorWindow *win = self->parent;

	if (fb == NULL) {
		return WINDOW_RET_FAILED;
	}
	*fb = &win->fb;

	return WINDOW_RET_OK;
}


static window_ret_t window_mgmt_get_event(Window *self, Event **event) {
	FbCompositorWindow *win = self->parent;

	if (event == NULL) {
		return WINDOW_RET_FAILED;
	}
	*event = &win->event;

	return WINDOW_RET_OK;
}


static const struct window_vmt window_mgmt_vmt = {
	.stat = window_mgmt_stat,
	.move = window_mgmt_move,
	.resize = window_mgmt_resize,
	.set_geometry = window_mgmt_set_geometry,
	.maximize = window_mgmt_maximize,
	.minimize = window_mgmt_minimize,
	.restore = window_mgmt_restore,
	.show = window_mgmt_show,
	.set_title = window_mgmt_set_title,
	.set_icon = window_mgmt_set_icon,
	.to_front = window_mgmt_to_front,
	.to_back = window_mgmt_to_back,
	.set_z = window_mgmt_set_z,
	.set_focus = window_mgmt_set_focus,
	.get_fb = window_mgmt_get_fb,
	.get_event = window_mgmt_get_event,
};


/***************************************************************************************************
 * Window factory interface
 ***************************************************************************************************/

static window_ret_t window_factory_create(WindowFactory *self, const struct window_geometry *geometry,
                                          Window **window) {
	FbCompositor *c = self->parent;

	if (geometry == NULL || window == NULL) {
		return WINDOW_RET_FAILED;
	}

	FbCompositorWindow *win = calloc(1, sizeof(FbCompositorWindow));
	if (win == NULL) {
		return WINDOW_RET_NOMEM;
	}
	win->event_queue = xQueueCreate(FB_COMPOSITOR_EVENT_QUEUE_LEN, sizeof(struct window_event));
	if (win->event_queue == NULL) {
		free(win);
		return WINDOW_RET_NOMEM;
	}
	/* v1: a window uses the same mode as the output; size its backing store to the requested geometry. */
	win->mode = c->mode;
	win->buf_size = fb_buf_size(geometry->w, geometry->h, win->mode);
	win->buf = calloc(1, win->buf_size);
	if (win->buf == NULL) {
		vQueueDelete(win->event_queue);
		free(win);
		return WINDOW_RET_NOMEM;
	}
	win->compositor = c;
	win->geometry = *geometry;
	win->saved = *geometry;
	win->state = WINDOW_STATE_NORMAL;
	win->visible = true;

	win->fb.parent = win;
	win->fb.vmt = &window_fb_vmt;
	win->window.parent = win;
	win->window.vmt = &window_mgmt_vmt;
	win->event.parent = win;
	win->event.vmt = &window_event_vmt;

	xSemaphoreTake(c->lock, portMAX_DELAY);
	/* New windows land on top of the stack. */
	FbCompositorWindow *tail = window_list_tail(c);
	win->z = (tail != NULL) ? (int16_t)(tail->z + 1) : 0;
	window_list_insert(c, win);
	xSemaphoreGive(c->lock);

	*window = &win->window;
	compositor_damage(c);
	return WINDOW_RET_OK;
}


static window_ret_t window_factory_destroy(WindowFactory *self, Window *window) {
	FbCompositor *c = self->parent;

	if (window == NULL) {
		return WINDOW_RET_FAILED;
	}
	FbCompositorWindow *win = window->parent;

	xSemaphoreTake(c->lock, portMAX_DELAY);
	window_list_remove(c, win);
	if (c->focused == win) {
		c->focused = NULL;
	}
	xSemaphoreGive(c->lock);

	compositor_damage(c);
	vQueueDelete(win->event_queue);
	free(win->buf);
	free(win);
	return WINDOW_RET_OK;
}


static const struct window_factory_vmt window_factory_vmt = {
	.create = window_factory_create,
	.destroy = window_factory_destroy,
};


/***************************************************************************************************
 * Composition
 ***************************************************************************************************/

/* Blit a single window onto the scratch buffer, clipped to the output rectangle. */
static void compositor_blit_window(FbCompositor *self, FbCompositorWindow *win) {
	const struct window_geometry *g = &win->geometry;
	size_t bpp = (size_t)self->mode;

	/* A run of pixels can be memcpy'd only when source and destination share the pixel mode and the
	 * run starts on a byte boundary in both buffers and spans whole bytes; that also needs a bit depth
	 * that tiles a byte cleanly. Anything else (leading/trailing sub-byte pixels, clipped edges, a
	 * mismatched mode) falls back to the per-pixel copy below. */
	bool can_bulk = (win->mode == self->mode) && (bpp % 8 == 0 || 8 % bpp == 0);

	for (int32_t sy = 0; sy < (int32_t)g->h; sy++) {
		int32_t dy = g->y + sy;
		if (dy < 0 || dy >= (int32_t)self->out_h) {
			continue;
		}
		for (int32_t sx = 0; sx < (int32_t)g->w; ) {
			int32_t dx = g->x + sx;
			if (dx < 0 || dx >= (int32_t)self->out_w) {
				sx++;
				continue;
			}

			/* Copy the largest byte-aligned run starting at this pixel in one memcpy. */
			if (can_bulk) {
				size_t src_bit = ((size_t)sy * g->w + (size_t)sx) * bpp;
				size_t dst_bit = ((size_t)dy * self->out_w + (size_t)dx) * bpp;
				if (src_bit % 8 == 0 && dst_bit % 8 == 0) {
					int32_t run_px = (int32_t)g->w - sx;
					if ((int32_t)self->out_w - dx < run_px) {
						run_px = (int32_t)self->out_w - dx;
					}
					size_t run_bytes = ((size_t)run_px * bpp) / 8;
					if (run_bytes > 0) {
						memcpy(self->scratch + dst_bit / 8, win->buf + src_bit / 8, run_bytes);
						sx += (int32_t)(run_bytes * 8 / bpp);
						continue;
					}
				}
			}

			uint32_t px = fb_get_pixel(win->buf, g->w, win->mode, sx, sy);
			fb_set_pixel(self->scratch, self->out_w, self->mode, dx, dy, px);
			sx++;
		}
	}
}


fb_compositor_ret_t fb_compositor_render(FbCompositor *self) {
	if (self == NULL) {
		return FB_COMPOSITOR_RET_NULL;
	}

	xSemaphoreTake(self->lock, portMAX_DELAY);

	fb_fill(self->scratch, self->out_w, self->out_h, self->mode, self->bg_color);

	/* Painter's algorithm: compose from the back (lowest z) to the front (highest z). */
	for (FbCompositorWindow *win = self->windows; win != NULL; win = win->next) {
		if (!win->visible || win->state == WINDOW_STATE_MINIMIZED) {
			continue;
		}
		compositor_blit_window(self, win);
	}

	self->out->vmt->write(self->out, 0, self->scratch, self->scratch_size, self->mode);

	xSemaphoreGive(self->lock);

	if (self->out->vmt->flush != NULL) {
		self->out->vmt->flush(self->out);
	}

	return FB_COMPOSITOR_RET_OK;
}


fb_compositor_ret_t fb_compositor_get_active_window(FbCompositor *self, Window **window) {
	if (self == NULL || window == NULL) {
		return FB_COMPOSITOR_RET_NULL;
	}

	xSemaphoreTake(self->lock, portMAX_DELAY);
	FbCompositorWindow *top = window_list_top(self);
	*window = (top != NULL) ? &top->window : NULL;
	xSemaphoreGive(self->lock);

	return FB_COMPOSITOR_RET_OK;
}


static void fb_compositor_task(void *p) {
	FbCompositor *self = (FbCompositor *)p;

	self->running = true;
	while (self->can_run) {
		/* The timeout lets the loop notice can_run going false during shutdown. */
		if (xSemaphoreTake(self->damaged, pdMS_TO_TICKS(100)) == pdTRUE) {
			fb_compositor_render(self);
		}
	}
	self->running = false;

	vTaskDelete(NULL);
}


/* Pump the input event source and route every event to the current top-level window. The source
 * listen() blocks indefinitely, so this task cannot be joined on shutdown (see fb_compositor_free). */
static void fb_compositor_input_task(void *p) {
	FbCompositor *self = (FbCompositor *)p;

	while (self->can_run) {
		enum event_type type = EV_TYPE_NONE;
		enum event_code code = EV_CODE_NONE;
		int32_t value = 0;
		if (self->input->vmt->listen(self->input, &type, &code, &value) != EV_RET_OK) {
			continue;
		}

		xSemaphoreTake(self->lock, portMAX_DELAY);
		FbCompositorWindow *top = window_list_top(self);
		QueueHandle_t queue = (top != NULL) ? top->event_queue : NULL;
		enum event_type filter = (top != NULL) ? top->event_filter : EV_TYPE_NONE;
		xSemaphoreGive(self->lock);

		/* filter == 0 (EV_TYPE_NONE) means the window did not narrow its subscription. */
		if (queue == NULL || (filter != EV_TYPE_NONE && (type & filter) == 0)) {
			continue;
		}
		struct window_event ev = {
			.type = type,
			.code = code,
			.value = value,
		};
		/* Drop the event rather than block the pump if the window is not draining its queue. */
		xQueueSend(queue, &ev, 0);
	}

	vTaskDelete(NULL);
}


/***************************************************************************************************
 * Service API
 ***************************************************************************************************/

fb_compositor_ret_t fb_compositor_init(FbCompositor *self, Fb *out, Event *input) {
	if (self == NULL || out == NULL) {
		return FB_COMPOSITOR_RET_NULL;
	}
	memset(self, 0, sizeof(FbCompositor));
	self->out = out;
	self->input = input;
	self->factory.parent = self;
	self->factory.vmt = &window_factory_vmt;

	struct fb_stat stat = {0};
	if (out->vmt->stat == NULL || out->vmt->stat(out, &stat) != FB_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot stat the output framebuffer"));
		return FB_COMPOSITOR_RET_FAILED;
	}
	if (!fb_mode_supported(stat.mode)) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("unsupported output framebuffer mode %u"), (unsigned)stat.mode);
		return FB_COMPOSITOR_RET_FAILED;
	}
	self->out_w = stat.w;
	self->out_h = stat.h;
	self->mode = stat.mode;

	self->scratch_size = fb_buf_size(self->out_w, self->out_h, self->mode);
	self->scratch = calloc(self->scratch_size, sizeof(uint8_t));
	if (self->scratch == NULL) {
		goto err;
	}

	self->lock = xSemaphoreCreateMutex();
	self->damaged = xSemaphoreCreateBinary();
	if (self->lock == NULL || self->damaged == NULL) {
		goto err;
	}

	self->can_run = true;
	if (xTaskCreate(fb_compositor_task, "fb-compositor", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &self->task) != pdPASS) {
		goto err;
	}

	/* Route input to the top-level window only when an event source was provided. */
	if (self->input != NULL) {
		if (xTaskCreate(fb_compositor_input_task, "fb-compositor-in", configMINIMAL_STACK_SIZE + 128, (void *)self, 1, &self->input_task) != pdPASS) {
			/* The render task is already up; stop it before releasing its resources. */
			self->can_run = false;
			compositor_damage(self);
			while (self->running) {
				vTaskDelay(pdMS_TO_TICKS(10));
			}
			goto err;
		}
	}

	/* Render an initial (blank) frame. */
	compositor_damage(self);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialized, output %ux%u, mode %u bpp"),
	      (unsigned)self->out_w, (unsigned)self->out_h, (unsigned)self->mode);
	return FB_COMPOSITOR_RET_OK;

err:
	if (self->scratch != NULL) {
		free(self->scratch);
		self->scratch = NULL;
	}
	if (self->lock != NULL) {
		vSemaphoreDelete(self->lock);
		self->lock = NULL;
	}
	if (self->damaged != NULL) {
		vSemaphoreDelete(self->damaged);
		self->damaged = NULL;
	}
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot initialize the service"));
	return FB_COMPOSITOR_RET_FAILED;
}


fb_compositor_ret_t fb_compositor_free(FbCompositor *self) {
	if (self == NULL) {
		return FB_COMPOSITOR_RET_NULL;
	}

	self->can_run = false;
	compositor_damage(self);
	while (self->running) {
		vTaskDelay(pdMS_TO_TICKS(10));
	}
	/* The input task may still be blocked in the source listen(); it exits on the next event. The
	 * caller must therefore keep the event source alive until then. */

	if (self->scratch != NULL) {
		free(self->scratch);
		self->scratch = NULL;
	}
	if (self->lock != NULL) {
		vSemaphoreDelete(self->lock);
		self->lock = NULL;
	}
	if (self->damaged != NULL) {
		vSemaphoreDelete(self->damaged);
		self->damaged = NULL;
	}

	return FB_COMPOSITOR_RET_OK;
}


fb_compositor_ret_t fb_compositor_set_background(FbCompositor *self, uint8_t color) {
	if (self == NULL) {
		return FB_COMPOSITOR_RET_NULL;
	}
	xSemaphoreTake(self->lock, portMAX_DELAY);
	self->bg_color = color;
	xSemaphoreGive(self->lock);

	compositor_damage(self);
	return FB_COMPOSITOR_RET_OK;
}
