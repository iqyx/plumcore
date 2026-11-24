/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Notification window component of the full-screen GUI window manager
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#include <main.h>

#include <interfaces/event.h>
#include <interfaces/window.h>
#include <services/fb-compositor/fb-compositor.h>
#include <services/fb-painter/fb-painter.h>

#define NOTIFY_WINDOW_LINES 4

typedef enum {
	NOTIFY_WINDOW_RET_OK = 0,
	NOTIFY_WINDOW_RET_FAILED,
	NOTIFY_WINDOW_RET_NULL,
	NOTIFY_WINDOW_RET_NOMEM,
} notify_window_ret_t;

enum notify_window_icon {
	NOTIFY_WINDOW_ICON_INFO = 0,
	NOTIFY_WINDOW_ICON_WARNING,
	NOTIFY_WINDOW_ICON_ERROR,
	NOTIFY_WINDOW_ICON_SD,
	NOTIFY_WINDOW_ICON_NO_SD,
};

/* An action button: pressing @p key selects the action, @p label is shown on the button. */
struct notify_window_action {
	enum event_code key;
	const char *label;
};


/* An open asynchronous notification with a progress bar, see notify_window_run(). */
struct notify_window {
	/* Events of the notification window, the client listens on it. */
	Event *event;

	FbCompositor *compositor;
	Window *window;
	FbPainter painter;

	/* Box of the progress bar inside the window. */
	int16_t bar_x;
	int16_t bar_y;
	uint16_t bar_w;

	/* The latest progress, sampled by the task animating the bar at its own pace. */
	volatile uint32_t total;
	volatile uint32_t value;

	TaskHandle_t task;
	volatile bool can_run;
	volatile bool running;
};


/* Show a notification window with an icon on top, the word-wrapped @p message (at most NOTIFY_WINDOW_LINES lines)
 * below it and a row of buttons, one for each of the @p count @p actions, at the bottom. The window covers
 * @p geometry and is shown above all other windows.
 *
 * If @p instance is NULL, the call blocks until a key of one of the actions is pressed or @p timeout_ms elapses, then
 * the window is destroyed and the index of the selected action is returned in @p index. The default index 0 is
 * returned on timeout.
 *
 * If @p instance is not NULL, the notification is asynchronous. A progress bar is shown between the message (one line
 * shorter) and the buttons, the call returns immediately with the open notification in @p instance and @p timeout_ms
 * and @p index are not used. The keys of the actions are only shown on the buttons, the client receives all the
 * events of the window on (*instance)->event. The bar starts indeterminate, it is set by notify_window_set_progress()
 * and the notification is closed by notify_window_close(). */
notify_window_ret_t notify_window_run(FbCompositor *compositor, const struct window_geometry *geometry,
                                      enum notify_window_icon icon, const char *message,
                                      const struct notify_window_action *actions, size_t count, uint32_t timeout_ms,
                                      size_t *index, struct notify_window **instance);

/* Set the progress of an asynchronous notification to @p value out of @p total. A @p total of 0 shows an
 * indeterminate bar scrolling from left to right. Returns immediately, the bar is redrawn by the notification task. */
notify_window_ret_t notify_window_set_progress(struct notify_window *self, uint32_t total, uint32_t value);

/* Close an asynchronous notification, destroy its window and free it. The pointer is invalid afterwards. */
notify_window_ret_t notify_window_close(struct notify_window *self);
