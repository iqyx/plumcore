/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Notification window component of the full-screen GUI window manager
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 *
 * A notification window. notify_window_run() creates a window in the overlay layer of the compositor (above all
 * other windows), paints an icon, the word-wrapped message and a row of action buttons, then waits for a key of one
 * of the actions or for a timeout. The window is destroyed before returning, nothing is kept between calls.
 *
 * Window events can only be received by a blocking listen(). It is run in a short-lived helper task forwarding the
 * selected action index to a queue, the caller waits on the queue with a timeout and deletes the helper afterwards.
 *
 * An asynchronous notification additionally shows a progress bar and is returned open to the caller as struct
 * notify_window, which keeps the window, its painter and a task animating the bar until notify_window_close().
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <main.h>
#include "u_log.h"
#include "u_assert.h"

#include <interfaces/window.h>
#include <interfaces/painter.h>
#include <interfaces/event.h>
#include <services/fb-compositor/fb-compositor.h>
#include <services/fb-painter/fb-painter.h>

#include "notify-window.h"

#define MODULE_NAME "notify-window"

#include "assets/assets.h"

#define NOTIFY_WINDOW_MARGIN 6
#define NOTIFY_WINDOW_GAP 4
#define NOTIFY_WINDOW_LINE_H 10
#define NOTIFY_WINDOW_BUTTON_H 14
#define NOTIFY_WINDOW_BAR_H 12
#define NOTIFY_WINDOW_BAR_INSET 18
#define NOTIFY_WINDOW_BAR_PERIOD_MS 100

/* Indeterminate bar animation: the [0, NOTIFY_WINDOW_MARQUEE_TOTAL] range is mapped onto the bar and a window
 * NOTIFY_WINDOW_MARQUEE_SEG wide slides right by NOTIFY_WINDOW_MARQUEE_STEP each period, wrapping around. */
#define NOTIFY_WINDOW_MARQUEE_TOTAL 100
#define NOTIFY_WINDOW_MARQUEE_SEG 25
#define NOTIFY_WINDOW_MARQUEE_STEP 5


/* Shared between notify_window_run() and its helper task, lives on the caller's stack. */
struct notify_window_listen {
	Event *event;
	QueueHandle_t selected;
	const struct notify_window_action *actions;
	size_t count;
};


/* Forward the index of the action whose key was pressed. Runs until deleted by notify_window_run(). */
static void notify_window_listen_task(void *p) {
	struct notify_window_listen *l = (struct notify_window_listen *)p;

	while (true) {
		enum event_type type = EV_TYPE_NONE;
		enum event_code code = EV_CODE_NONE;
		int32_t value = 0;
		if (l->event->vmt->listen(l->event, &type, &code, &value) != EV_RET_OK || value == 0) {
			continue;
		}
		for (size_t i = 0; i < l->count; i++) {
			if (l->actions[i].key == code) {
				xQueueSend(l->selected, &i, 0);
				break;
			}
		}
	}
}


/* Draw @p text centered horizontally at @p cx. */
static void notify_window_text_centered(Painter *painter, int16_t cx, int16_t y, const char *text) {
	uint16_t w = 0;
	painter->vmt->text_size(painter, text, &w, NULL);
	painter->vmt->text(painter, cx - w / 2, y, text);
}


/* Paint the notification. With @p progress, the message is one line shorter and space for the progress bar is left
 * below it, its box is set in @p progress. */
static void notify_window_render(FbPainter *fbp, uint16_t w, uint16_t h, enum notify_window_icon icon, char *message,
                                 const struct notify_window_action *actions, size_t count,
                                 struct notify_window *progress) {
	Painter *painter = &fbp->painter;

	painter->vmt->begin(painter);

	/* Clear the whole window to a black background. */
	painter->vmt->set_pen(painter, 0xff000000, 1);
	painter->vmt->set_brush(painter, 0xff000000);
	painter->vmt->rect(painter, 0, 0, w, h);
	painter->vmt->set_pen(painter, 0xffffffff, 1);

	/* Message wrapped in place to the full window width. */
	uint16_t text_h = 0;
	uint16_t lines = 0;
	painter->vmt->set_font(painter, PAINTER_FONT_BOLD, NULL);
	painter->vmt->text_size(painter, "", NULL, &text_h);
	uint16_t max_lines = (progress != NULL) ? NOTIFY_WINDOW_LINES - 1 : NOTIFY_WINDOW_LINES;
	fb_painter_wrap_text(fbp, message, w - 2 * NOTIFY_WINDOW_MARGIN, max_lines * text_h, &lines);

	const struct painter_raw_image *image = &notify_info_data;
	switch (icon) {
		case NOTIFY_WINDOW_ICON_WARNING:
			image = &notify_warning_data;
			break;
		case NOTIFY_WINDOW_ICON_ERROR:
			image = &notify_error_data;
			break;
		case NOTIFY_WINDOW_ICON_SD:
			image = &notify_sd_data;
			break;
		case NOTIFY_WINDOW_ICON_NO_SD:
			image = &notify_no_sd_data;
			break;
		default:
			break;
	}

	/* The icon, the message and the progress bar are centered vertically in the area above the buttons. */
	int16_t area_h = h - NOTIFY_WINDOW_MARGIN - NOTIFY_WINDOW_BUTTON_H;
	int16_t block_h = (int16_t)image->h + NOTIFY_WINDOW_GAP + lines * NOTIFY_WINDOW_LINE_H;
	if (progress != NULL) {
		block_h += NOTIFY_WINDOW_GAP + NOTIFY_WINDOW_BAR_H;
	}
	int16_t y = (area_h - block_h) / 2;
	painter->vmt->image(painter, (w - (int16_t)image->w) / 2, y, image, PAINTER_MODE_INVERTED);
	y += (int16_t)image->h + NOTIFY_WINDOW_GAP;

	/* Each line centered horizontally. */
	char *line = message;
	while (*line != '\0') {
		char *end = strchr(line, '\n');
		if (end != NULL) {
			*end = '\0';
		}
		notify_window_text_centered(painter, w / 2, y, line);
		y += NOTIFY_WINDOW_LINE_H;
		if (end == NULL) {
			break;
		}
		line = end + 1;
	}

	/* The bar itself is drawn by the notification task. */
	if (progress != NULL) {
		progress->bar_x = NOTIFY_WINDOW_BAR_INSET;
		progress->bar_y = y + NOTIFY_WINDOW_GAP;
		progress->bar_w = w - 2 * NOTIFY_WINDOW_BAR_INSET;
	}

	/* Equally wide buttons spread over the bottom row. */
	if (count > 0) {
		int16_t by = h - NOTIFY_WINDOW_MARGIN - NOTIFY_WINDOW_BUTTON_H;
		uint16_t bw = (w - 2 * NOTIFY_WINDOW_MARGIN - (count - 1) * NOTIFY_WINDOW_GAP) / count;
		for (size_t i = 0; i < count; i++) {
			int16_t bx = NOTIFY_WINDOW_MARGIN + i * (bw + NOTIFY_WINDOW_GAP);
			painter->vmt->set_pen(painter, 0xffffffff, 1);
			painter->vmt->set_brush(painter, 0xff000000);
			painter->vmt->rect(painter, bx, by, bw, NOTIFY_WINDOW_BUTTON_H);
			notify_window_text_centered(painter, bx + bw / 2, by + (NOTIFY_WINDOW_BUTTON_H - text_h) / 2,
			                            actions[i].label);
		}
	}

	painter->vmt->end(painter);
}


/* Repaint the whole progress bar and fill the [start, stop] window mapped onto the [0, total] range. A window with
 * stop < start wraps past the right edge back to the left. */
static void notify_window_draw_bar(struct notify_window *self, uint32_t total, uint32_t start, uint32_t stop) {
	Painter *painter = &self->painter.painter;

	painter->vmt->begin(painter);
	painter->vmt->set_pen(painter, 0xffffffff, 1);
	painter->vmt->set_brush(painter, 0xff000000);
	painter->vmt->rect(painter, self->bar_x, self->bar_y, self->bar_w, NOTIFY_WINDOW_BAR_H);

	/* Inset by two pixels: one for the outline plus a one pixel padding around the filled part. */
	int16_t inner_x = self->bar_x + 2;
	uint16_t inner_w = self->bar_w - 4;
	int16_t inner_y = self->bar_y + 2;
	uint16_t inner_h = NOTIFY_WINDOW_BAR_H - 4;
	int16_t px_start = inner_x + (int16_t)((uint32_t)inner_w * start / total);
	int16_t px_stop = inner_x + (int16_t)((uint32_t)inner_w * stop / total);

	painter->vmt->set_brush(painter, 0xffffffff);
	if (stop >= start) {
		if (px_stop > px_start) {
			painter->vmt->rect(painter, px_start, inner_y, px_stop - px_start, inner_h);
		}
	} else {
		painter->vmt->rect(painter, px_start, inner_y, (inner_x + inner_w) - px_start, inner_h);
		if (px_stop > inner_x) {
			painter->vmt->rect(painter, inner_x, inner_y, px_stop - inner_x, inner_h);
		}
	}
	painter->vmt->end(painter);
}


/* Animate the indeterminate bar of an asynchronous notification, redraw the determinate one when the progress
 * changes. Runs until notify_window_close(). */
static void notify_window_bar_task(void *p) {
	struct notify_window *self = (struct notify_window *)p;

	uint32_t pos = 0;
	uint32_t shown_total = 0;
	uint32_t shown_value = 0;
	bool shown = false;
	while (self->can_run) {
		uint32_t total = self->total;
		uint32_t value = self->value;
		if (total == 0) {
			notify_window_draw_bar(self, NOTIFY_WINDOW_MARQUEE_TOTAL, pos,
			                       (pos + NOTIFY_WINDOW_MARQUEE_SEG) % NOTIFY_WINDOW_MARQUEE_TOTAL);
			pos = (pos + NOTIFY_WINDOW_MARQUEE_STEP) % NOTIFY_WINDOW_MARQUEE_TOTAL;
			shown = false;
		} else {
			if (value > total) {
				value = total;
			}
			if (!shown || total != shown_total || value != shown_value) {
				notify_window_draw_bar(self, total, 0, value);
				shown_total = total;
				shown_value = value;
				shown = true;
			}
		}
		vTaskDelay(pdMS_TO_TICKS(NOTIFY_WINDOW_BAR_PERIOD_MS));
	}

	self->running = false;
	vTaskDelete(NULL);
}


/* Open an asynchronous notification. The instance and the copy of the message wrapped in place are allocated
 * together. */
static notify_window_ret_t notify_window_open(FbCompositor *compositor, const struct window_geometry *geometry,
                                              enum notify_window_icon icon, const char *message,
                                              const struct notify_window_action *actions, size_t count,
                                              struct notify_window **instance) {
	struct notify_window *self = malloc(sizeof(struct notify_window) + strlen(message) + 1);
	if (self == NULL) {
		return NOTIFY_WINDOW_RET_NOMEM;
	}
	memset(self, 0, sizeof(struct notify_window));
	char *text = (char *)(self + 1);
	strcpy(text, message);
	self->compositor = compositor;

	if (compositor->factory.vmt->create(&compositor->factory, geometry, &self->window) != WINDOW_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create the window"));
		goto err;
	}
	Fb *fb = NULL;
	self->window->vmt->get_fb(self->window, &fb);
	if (fb_painter_init(&self->painter, fb) != FB_PAINTER_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start the window painter"));
		goto err_window;
	}
	self->window->vmt->set_title(self->window, "Notification");
	self->window->vmt->get_event(self->window, &self->event);
	notify_window_render(&self->painter, geometry->w, geometry->h, icon, text, actions, count, self);

	self->can_run = true;
	self->running = true;
	if (xTaskCreate(notify_window_bar_task, "notify-bar", configMINIMAL_STACK_SIZE + 128, (void *)self, 1,
	                &self->task) != pdPASS) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create the progress task"));
		goto err_painter;
	}

	/* The overlay layer keeps the window above windows brought to front while it is shown. */
	self->window->vmt->set_z(self->window, FB_COMPOSITOR_Z_OVERLAY);
	self->window->vmt->show(self->window, true);

	*instance = self;
	return NOTIFY_WINDOW_RET_OK;

err_painter:
	fb_painter_free(&self->painter);
err_window:
	compositor->factory.vmt->destroy(&compositor->factory, self->window);
err:
	free(self);
	return NOTIFY_WINDOW_RET_FAILED;
}


notify_window_ret_t notify_window_run(FbCompositor *compositor, const struct window_geometry *geometry,
                                      enum notify_window_icon icon, const char *message,
                                      const struct notify_window_action *actions, size_t count, uint32_t timeout_ms,
                                      size_t *index, struct notify_window **instance) {
	if (u_assert(compositor != NULL) ||
	    u_assert(geometry != NULL) ||
	    u_assert(message != NULL) ||
	    u_assert(actions != NULL || count == 0) ||
	    u_assert(index != NULL || instance != NULL)) {
		return NOTIFY_WINDOW_RET_NULL;
	}
	if (instance != NULL) {
		return notify_window_open(compositor, geometry, icon, message, actions, count, instance);
	}
	*index = 0;

	/* The message is wrapped in place, work on a copy. */
	char *text = malloc(strlen(message) + 1);
	if (text == NULL) {
		return NOTIFY_WINDOW_RET_NOMEM;
	}
	strcpy(text, message);

	notify_window_ret_t ret = NOTIFY_WINDOW_RET_FAILED;
	Window *window = NULL;
	FbPainter painter = {0};
	struct notify_window_listen listen = {
		.actions = actions,
		.count = count,
	};
	TaskHandle_t listen_task = NULL;

	if (compositor->factory.vmt->create(&compositor->factory, geometry, &window) != WINDOW_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create the window"));
		goto err;
	}
	Fb *fb = NULL;
	window->vmt->get_fb(window, &fb);
	if (fb_painter_init(&painter, fb) != FB_PAINTER_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start the window painter"));
		goto err_window;
	}
	window->vmt->set_title(window, "Notification");
	notify_window_render(&painter, geometry->w, geometry->h, icon, text, actions, count, NULL);
	fb_painter_free(&painter);

	listen.selected = xQueueCreate(1, sizeof(size_t));
	if (listen.selected == NULL) {
		goto err_window;
	}
	window->vmt->get_event(window, &listen.event);
	xTaskCreate(notify_window_listen_task, "notify-window", configMINIMAL_STACK_SIZE, (void *)&listen, 1, &listen_task);
	if (listen_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create the event task"));
		goto err_queue;
	}

	/* The overlay layer keeps the window above windows brought to front while it is shown. */
	window->vmt->set_z(window, FB_COMPOSITOR_Z_OVERLAY);
	window->vmt->show(window, true);

	if (xQueueReceive(listen.selected, index, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
		*index = 0;
	}
	ret = NOTIFY_WINDOW_RET_OK;

	/* The helper task blocks on the window events forever, delete it before the window goes away. */
	vTaskDelete(listen_task);
err_queue:
	vQueueDelete(listen.selected);
err_window:
	compositor->factory.vmt->destroy(&compositor->factory, window);
err:
	free(text);
	return ret;
}


notify_window_ret_t notify_window_set_progress(struct notify_window *self, uint32_t total, uint32_t value) {
	if (u_assert(self != NULL)) {
		return NOTIFY_WINDOW_RET_NULL;
	}
	self->total = total;
	self->value = value;
	return NOTIFY_WINDOW_RET_OK;
}


notify_window_ret_t notify_window_close(struct notify_window *self) {
	if (u_assert(self != NULL)) {
		return NOTIFY_WINDOW_RET_NULL;
	}

	/* Stop the task first, it draws into the window. */
	self->can_run = false;
	while (self->running) {
		vTaskDelay(pdMS_TO_TICKS(10));
	}
	fb_painter_free(&self->painter);
	self->compositor->factory.vmt->destroy(&self->compositor->factory, self->window);
	free(self);
	return NOTIFY_WINDOW_RET_OK;
}
