/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Bootloader splash screen GUI component
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <main.h>

#include <interfaces/fb.h>
#include <interfaces/painter.h>
#include <services/fb-painter/fb-painter.h>

/* Repaint period of the animated progressbar, in milliseconds. */
#define GUI_PROGRESS_PERIOD_MS 100

/* Maximum length of a status message, including the terminator. */
#define GUI_STATUS_TEXT_MAX 64

typedef enum {
	GUI_RET_OK = 0,
	GUI_RET_FAILED,
	GUI_RET_NULL,
} gui_ret_t;

typedef struct {
	/* Framebuffer discovered through the service locator at init, plus its native pixel format and
	 * geometry sampled once through its stat method. */
	Fb *fb;
	enum fb_mode mode;
	size_t w;
	size_t h;

	/* Painter bound to the framebuffer above, used for all drawing. */
	FbPainter painter;

	/* Latest progress state set through gui_set_progress and consumed by the task at its own pace.
	 * A total of zero selects the never-ending marquee. Overwriting these is the whole update protocol:
	 * the task redraws only when a value actually changed. */
	volatile uint32_t progress_total;
	volatile uint32_t progress_value;
	volatile char status[GUI_STATUS_TEXT_MAX];

	/* Task drawing the static splash screen once and then animating the progressbar forever. */
	TaskHandle_t task;
} Gui;


/**
 * @brief Discover the first framebuffer, build the splash screen and start its animation task.
 *
 * @param self Preallocated instance memory.
 * @return GUI_RET_OK if a framebuffer was found and the task was started, an error otherwise.
 */
gui_ret_t gui_init(Gui *self);

/**
 * @brief Stop the animation task and release the painter.
 */
gui_ret_t gui_free(Gui *self);

/**
 * @brief Set the latest progress state of the splash screen and return immediately.
 *
 * The values simply overwrite the current state; the gui task picks them up at its own pace and
 * redraws only what changed. The progressbar shows progress out of total and the message is drawn as
 * the status line below it. A total of zero selects the never-ending marquee. An empty or NULL message
 * clears the status line. Safe to call as often as desired; redundant updates are coalesced.
 *
 * @param self Instance of the gui component.
 * @param total Full-scale value, or zero for the never-ending marquee.
 * @param progress Current progress value, clamped to total.
 * @param message Status text to show below the bar, or NULL to clear it.
 * @return GUI_RET_OK on success.
 */
gui_ret_t gui_set_progress(Gui *self, uint32_t total, uint32_t progress, const char *message);
