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


/* Show a notification window with an icon on top, the word-wrapped @p message (at most NOTIFY_WINDOW_LINES lines)
 * below it and a row of buttons, one for each of the @p count @p actions, at the bottom. The window covers
 * @p geometry and is shown above all other windows. The call blocks until a key of one of the actions is pressed or
 * @p timeout_ms elapses, then the window is destroyed and the index of the selected action is returned in @p index.
 * The default index 0 is returned on timeout. */
notify_window_ret_t notify_window_run(FbCompositor *compositor, const struct window_geometry *geometry,
                                      enum notify_window_icon icon, const char *message,
                                      const struct notify_window_action *actions, size_t count, uint32_t timeout_ms,
                                      size_t *index);
