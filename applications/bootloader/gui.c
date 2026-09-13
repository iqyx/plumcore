/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Bootloader splash screen GUI component
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 *
 * A small self-contained component of the bootloader application. It discovers the first framebuffer
 * device advertised by the port, paints a static splash screen (a plum picture, a bold product name
 * and the firmware version) and then animates a never-ending progressbar underneath it.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <main.h>
#include "u_log.h"
#include "u_assert.h"

#include <interfaces/fb.h>
#include <interfaces/painter.h>
#include <interfaces/servicelocator.h>
#include <services/fb-painter/fb-painter.h>

#include "gui.h"
#include "assets/assets.h"

#define MODULE_NAME "gui"

/* Foreground/background colors on the (grayscale) splash screen. */
#define GUI_COLOR_BG 0xff000000
#define GUI_COLOR_FG 0xffffffff

/* Never-ending progressbar animation: the [0, GUI_PROGRESS_TOTAL] range is mapped onto the bar and a
 * window GUI_PROGRESS_SEG wide slides right by GUI_PROGRESS_STEP each period, wrapping around. */
#define GUI_PROGRESS_TOTAL 100
#define GUI_PROGRESS_SEG 25
#define GUI_PROGRESS_STEP 5


/* Compute the progressbar box from the framebuffer geometry. The status text is drawn in the strip
 * directly below it. */
static void gui_bar_box(Gui *self, int16_t *x, int16_t *y, uint16_t *w, uint16_t *h) {
	*x = 24;
	*w = self->w - 2 * *x;
	*h = 12;
	*y = self->h - *h - 24;
}


/* Repaint the whole progressbar and fill the [start, stop] window mapped onto the [0, total] range. A
 * window with stop < start wraps past the right edge back to the left. A total of zero draws just the
 * empty bar. The bar geometry is derived from the framebuffer size so the bar can be repainted on its
 * own, without redrawing the rest of the splash screen. */
static void gui_draw_progressbar(Gui *self, uint32_t total, uint32_t start, uint32_t stop) {
	Painter *painter = &self->painter.painter;

	int16_t x, y;
	uint16_t w, h;
	gui_bar_box(self, &x, &y, &w, &h);

	painter->vmt->begin(painter);

	painter->vmt->set_pen(painter, GUI_COLOR_FG, 1);
	painter->vmt->set_brush(painter, GUI_COLOR_BG);
	painter->vmt->rect(painter, x, y, w, h);

	if (total > 0) {
		/* Inset by two pixels: one for the outline plus a one pixel padding around the filled bar. */
		int16_t inner_x = x + 2;
		uint16_t inner_w = w - 4;
		int16_t inner_y = y + 2;
		uint16_t inner_h = h - 4;
		int16_t px_start = inner_x + (int16_t)((uint32_t)inner_w * start / total);
		int16_t px_stop = inner_x + (int16_t)((uint32_t)inner_w * stop / total);

		painter->vmt->set_brush(painter, GUI_COLOR_FG);
		if (stop >= start) {
			/* A single contiguous segment. */
			if (px_stop > px_start) {
				painter->vmt->rect(painter, px_start, inner_y, px_stop - px_start, inner_h);
			}
		} else {
			/* The window wraps past the end: fill from start to the right edge and from the left edge to stop. */
			painter->vmt->rect(painter, px_start, inner_y, (inner_x + inner_w) - px_start, inner_h);
			if (px_stop > inner_x) {
				painter->vmt->rect(painter, inner_x, inner_y, px_stop - inner_x, inner_h);
			}
		}
	}

	painter->vmt->end(painter);
}


/* Draw the never-ending marquee indicator with its window at position pos. */
static void gui_draw_marquee(Gui *self, uint32_t pos) {
	gui_draw_progressbar(self, GUI_PROGRESS_TOTAL, pos % GUI_PROGRESS_TOTAL, (pos + GUI_PROGRESS_SEG) % GUI_PROGRESS_TOTAL);
}


/* Draw, or clear when text is NULL or empty, a centered status line in the strip below the
 * progressbar. Static text only; the caller decides when to update it. */
static void gui_draw_status(Gui *self, const char *text) {
	Painter *painter = &self->painter.painter;

	int16_t x, y;
	uint16_t w, h;
	gui_bar_box(self, &x, &y, &w, &h);
	int16_t status_y = y + (int16_t)h + 4;

	painter->vmt->begin(painter);

	/* Clear the whole strip below the bar. */
	painter->vmt->set_pen(painter, GUI_COLOR_BG, 1);
	painter->vmt->set_brush(painter, GUI_COLOR_BG);
	painter->vmt->rect(painter, 0, status_y, self->w, self->h - status_y);

	if (text != NULL && text[0] != '\0') {
		uint16_t tw = 0;
		uint16_t th = 0;
		painter->vmt->set_pen(painter, GUI_COLOR_FG, 1);
		painter->vmt->set_font(painter, PAINTER_FONT_NORMAL, NULL);
		painter->vmt->text_size(painter, text, &tw, &th);
		painter->vmt->text(painter, (int16_t)(self->w - tw) / 2, status_y, text);
	}

	painter->vmt->end(painter);
}


/* Paint the static part of the splash screen: the plum picture centered near the top, a bold product
 * name below it and the firmware version underneath. */
static void gui_draw_splash(Gui *self) {
	Painter *painter = &self->painter.painter;
	const char *title = "plumCore bootloader";
	const char *version = UMESH_VERSION;

	painter->vmt->begin(painter);

	/* Clear the whole screen to the background color. */
	painter->vmt->set_pen(painter, GUI_COLOR_BG, 1);
	painter->vmt->set_brush(painter, GUI_COLOR_BG);
	painter->vmt->rect(painter, 0, 0, self->w, self->h);

	/* Plum picture in the top-middle. */
	int16_t plum_x = (int16_t)(self->w - plum_32_data.w) / 2;
	int16_t plum_y = 10;
	painter->vmt->image(painter, plum_x, plum_y, &plum_32_data, PAINTER_MODE_INVERTED);

	painter->vmt->set_pen(painter, GUI_COLOR_FG, 1);

	/* Bold product name, centered under the picture. */
	uint16_t tw = 0;
	uint16_t th = 0;
	painter->vmt->set_font(painter, PAINTER_FONT_BOLD, NULL);
	painter->vmt->text_size(painter, title, &tw, &th);
	int16_t title_y = plum_y + (int16_t)plum_32_data.h + 6;
	painter->vmt->text(painter, (int16_t)(self->w - tw) / 2, title_y, title);

	/* Version line in the normal font, centered below the product name. */
	uint16_t vw = 0;
	uint16_t vh = 0;
	painter->vmt->set_font(painter, PAINTER_FONT_NORMAL, NULL);
	painter->vmt->text_size(painter, version, &vw, &vh);
	painter->vmt->text(painter, (int16_t)(self->w - vw) / 2, title_y + (int16_t)th + 3, version);

	painter->vmt->end(painter);

	/* Draw the initial marquee frame as part of the splash. */
	gui_draw_marquee(self, 0);
}


/* Draw the splash once, then run the never-ending progressbar animation until the task is stopped. */
static void gui_task(void *p) {
	Gui *self = (Gui *)p;

	gui_draw_splash(self);

	/* The task runs at a fixed pace and samples the latest progress state each tick. Progress may be
	 * set far more often than this; redundant updates are simply coalesced here. */
	uint32_t pos = 0;
	uint32_t shown_total = 0;
	uint32_t shown_progress = 0;
	bool bar_shown = false;
	char shown_status[GUI_STATUS_TEXT_MAX] = {0};
	while (true) {
		uint32_t total = self->progress_total;
		uint32_t progress = self->progress_value;

		if (total == 0) {
			/* No known total: keep the marquee animating. */
			gui_draw_marquee(self, pos);
			pos = (pos + GUI_PROGRESS_STEP) % GUI_PROGRESS_TOTAL;
			bar_shown = false;
		} else {
			if (progress > total) {
				progress = total;
			}
			/* Redraw the determinate bar only when it actually changed. */
			if (!bar_shown || total != shown_total || progress != shown_progress) {
				gui_draw_progressbar(self, total, 0, progress);
				shown_total = total;
				shown_progress = progress;
				bar_shown = true;
			}
		}

		/* Redraw the status line only when the message changed. */
		char status[GUI_STATUS_TEXT_MAX];
		strlcpy(status, (const char *)self->status, sizeof(status));
		if (strcmp(status, shown_status) != 0) {
			gui_draw_status(self, status);
			strlcpy(shown_status, status, sizeof(shown_status));
		}

		vTaskDelay(pdMS_TO_TICKS(GUI_PROGRESS_PERIOD_MS));
	}

	vTaskDelete(NULL);
}


gui_ret_t gui_init(Gui *self) {
	if (u_assert(self != NULL)) {
		return GUI_RET_NULL;
	}
	memset(self, 0, sizeof(Gui));

	/* Discover the first framebuffer device advertised by the port. */
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_FB, 0, (Interface **)&self->fb) != ISERVICELOCATOR_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("no framebuffer device found"));
		return GUI_RET_FAILED;
	}

	/* Sample the framebuffer size and native pixel format once. */
	struct fb_stat stat = {0};
	if (self->fb->vmt->stat(self->fb, &stat) != FB_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot stat the framebuffer"));
		return GUI_RET_FAILED;
	}
	self->mode = stat.mode;
	self->w = stat.w;
	self->h = stat.h;

	if (fb_painter_init(&self->painter, self->fb) != FB_PAINTER_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start the framebuffer painter"));
		return GUI_RET_FAILED;
	}

	xTaskCreate(gui_task, "gui", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &self->task);
	if (self->task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		fb_painter_free(&self->painter);
		return GUI_RET_FAILED;
	}

	return GUI_RET_OK;
}


gui_ret_t gui_set_progress(Gui *self, uint32_t total, uint32_t progress, const char *message) {
	if (u_assert(self != NULL)) {
		return GUI_RET_NULL;
	}

	/* Simply overwrite the latest state; the task picks it up on its next tick. */
	self->progress_total = total;
	self->progress_value = progress;
	strlcpy((char *)self->status, message != NULL ? message : "", sizeof(self->status));

	return GUI_RET_OK;
}


gui_ret_t gui_free(Gui *self) {
	if (u_assert(self != NULL)) {
		return GUI_RET_NULL;
	}

	if (self->task != NULL) {
		vTaskDelete(self->task);
		self->task = NULL;
	}
	fb_painter_free(&self->painter);

	return GUI_RET_OK;
}
