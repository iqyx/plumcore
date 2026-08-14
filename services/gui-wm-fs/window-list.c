/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Window list component of the full-screen GUI window manager
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 *
 * A small self-contained component of the gui-wm-fs service. It creates a single full-screen window
 * on the compositor and renders a simple textual list of all windows (geometry, stacking order and
 * visibility) into it on demand. A task listens on the window's own event source and logs every
 * event routed to it while it is the top-level window.
 *
 * The compositor list is walked directly under the compositor lock, but the lock is dropped around
 * each paint call: the painter draws into this component's own window, whose fb read/write path takes
 * the very same (non-recursive) compositor lock, so painting while holding it would deadlock.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "u_log.h"
#include "u_assert.h"

#include <interfaces/window.h>
#include <interfaces/painter.h>
#include <services/fb-compositor/fb-compositor.h>

#include "window-list.h"

#define MODULE_NAME "window-list"

#include "assets/assets.h"


/* Render the compositor window list into this component's window. The compositor list is walked
 * directly under the compositor lock, but the lock is dropped around each text() call: painting into
 * our own window re-enters the very same (non-recursive) compositor lock through the window fb write
 * path, so holding it across a paint would deadlock. */
static void window_list_render(WindowList *self) {
	Painter *painter = &self->painter.painter;

	painter->vmt->begin(painter);

	/* Clear the whole window to a white background. */
	painter->vmt->set_pen(painter, 0xff000000, 1);
	painter->vmt->set_brush(painter, 0xff000000);
	painter->vmt->rect(painter, 0, 0, self->conf.geometry.w, self->conf.geometry.h);

	painter->vmt->set_pen(painter, 0xff000000, 1);
	painter->vmt->set_font(painter, PAINTER_FONT_BOLD, NULL);

	painter->vmt->set_pen(painter, 0xffffffff, 1);

	for (size_t i = 0; i < self->window_count && i < 4; i++) {
		struct window_stat stat = {};
		Window *w = self->windows[i];

		if (w->vmt->stat && w->vmt->stat(w, &stat) == WINDOW_RET_OK) {
			if ((self->window_list_offset + i) == self->window_selected) {
				painter->vmt->image(painter, 2, i * 36 + 10, &arrow1_right_data, PAINTER_MODE_INVERTED);
			}

			if (stat.icon == NULL) {
				stat.icon = &window_data;
			}
			painter->vmt->image(painter, 16, i * 36 + 2, stat.icon, PAINTER_MODE_INVERTED);
			painter->vmt->text(painter, 60, i * 36 + 10, stat.title);
		}
	}

	painter->vmt->end(painter);
}


static void event_task(void *p) {
	WindowList *self = (WindowList *)p;

	/* Events routed to this window by the compositor (only while it is the top-level window). */
	Event *event = NULL;
	self->window->vmt->get_event(self->window, &event);

	self->can_run = true;
	self->running = true;
	while (self->can_run) {
		enum event_type type = EV_TYPE_NONE;
		enum event_code code = EV_CODE_NONE;
		int32_t value = 0;
		if (event->vmt->listen(event, &type, &code, &value) != EV_RET_OK || value != 1) {
			continue;
		}

		switch (code) {
			case EV_KEY_ESC:
				self->window->vmt->show(self->window, false);
				break;

			case EV_KEY_ENTER:
				self->window->vmt->show(self->window, false);
				if (self->window_selected < self->window_count) {
					Window *w = self->windows[self->window_selected];
					w->vmt->to_front(w);
				}
				break;

			case EV_KEY_LEFT:
				if (self->window_selected > 0) {
					self->window_selected--;
					window_list_render(self);
				}
				break;

			case EV_KEY_RIGHT:
				if (self->window_selected + 1 < self->window_count) {
					self->window_selected++;
					window_list_render(self);
				}
				break;

			default:
				/* passtrhough */
		}
	}
	self->running = false;
	vTaskDelete(NULL);
}


window_list_ret_t window_list_init(WindowList *self, const struct window_list_conf *conf) {
	if (u_assert(self != NULL) ||
	    u_assert(conf != NULL)) {
		return WINDOW_LIST_RET_NULL;
	}
	memset(self, 0, sizeof(WindowList));
	memcpy(&self->conf, conf, sizeof(struct window_list_conf));

	if (u_assert(self->conf.compositor != NULL) ||
	    u_assert(self->conf.geometry.w > 0) ||
	    u_assert(self->conf.geometry.h > 0)) {
		return WINDOW_LIST_RET_FAILED;
	}

	/* Create the list window on the compositor; it allocates and owns the backing store. */
	if (self->conf.compositor->factory.vmt->create(&self->conf.compositor->factory, &self->conf.geometry,
	    &self->window) != WINDOW_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create the window"));
		goto err;
	}

	Fb *win_fb = NULL;
	self->window->vmt->get_fb(self->window, &win_fb);
	if (fb_painter_init(&self->painter, win_fb) != FB_PAINTER_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start the window painter"));
		goto err;
	}

	self->window->vmt->set_title(self->window, "Window list");
	self->window->vmt->set_icon(self->window, &window_list_data);

	xTaskCreate(event_task, "window-list", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &self->task);
	if (self->task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		goto err;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("init ok, %ux%u"),
	      (unsigned)self->conf.geometry.w, (unsigned)self->conf.geometry.h);
	return WINDOW_LIST_RET_OK;
err:
	window_list_free(self);
	return WINDOW_LIST_RET_FAILED;
}


window_list_ret_t window_list_free(WindowList *self) {
	if (u_assert(self != NULL)) {
		return WINDOW_LIST_RET_FAILED;
	}

	/* Stop the task first so nothing touches the window while it is torn down. */
	self->can_run = false;
	while (self->running) {
		vTaskDelay(100);
	}

	if (self->window != NULL) {
		fb_painter_free(&self->painter);
		self->conf.compositor->factory.vmt->destroy(&self->conf.compositor->factory, self->window);
		self->window = NULL;
	}

	return WINDOW_LIST_RET_OK;
}


/* Snapshot the current compositor window list into self->windows under the compositor lock, then
 * invert the captured order in place so the items are shown in reverse compositor order. */
static void window_list_capture(WindowList *self) {
	xSemaphoreTake(self->conf.compositor->lock, portMAX_DELAY);
	self->window_list_offset = 0;
	self->window_selected = 0;
	self->window_count = 0;
	for (FbCompositorWindow *w = self->conf.compositor->windows; w != NULL; w = w->next) {
		if (self->window_count >= WINDOW_LIST_MAX) {
			break;
		}
		/* Only visible windows with a title set are listed (untitled windows such as the status and
		 * button bars are internal and stay hidden from the list). */
		if (!w->visible || w->title[0] == '\0') {
			continue;
		}
		self->windows[self->window_count] = &w->window;
		self->window_count++;
	}
	xSemaphoreGive(self->conf.compositor->lock);

	for (size_t i = 0; i < self->window_count / 2; i++) {
		Window *tmp = self->windows[i];
		self->windows[i] = self->windows[self->window_count - 1 - i];
		self->windows[self->window_count - 1 - i] = tmp;
	}
}


window_list_ret_t window_list_show(WindowList *self) {
	if (u_assert(self != NULL)) {
		return WINDOW_LIST_RET_FAILED;
	}

	/* Grab the current window list, then render it and bring the overlay on screen. */
	window_list_capture(self);
	window_list_render(self);
	self->window->vmt->to_front(self->window);
	self->window->vmt->show(self->window, true);
	return WINDOW_LIST_RET_OK;
}
