/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GUI applet launcher service
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 *
 * A minimal applet launcher. On gui_launcher_show() it enumerates all applets advertised through the
 * service locator, creates a single window on the compositor (sized from the configuration), renders
 * a plain textual list of the discovered applets into it and brings it on screen. A task listens on
 * the launcher window's routed event source (delivered by the compositor while the launcher is the
 * top-level window); pressing ESC hides the window, which can be shown again by another
 * gui_launcher_show() call.
 *
 * The applet list is a simple text list for now, without selection or launching.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <main.h>

#include <interfaces/event.h>
#include <interfaces/window.h>
#include <interfaces/painter.h>
#include <interfaces/applet.h>
#include <interfaces/servicelocator.h>
#include <services/fb-compositor/fb-compositor.h>
#include <services/fb-painter/fb-painter.h>

#include "gui-launcher.h"

#define MODULE_NAME "gui-launcher"

/* Applets are laid out in a grid of fixed-size cells, GUI_LAUNCHER_APPLETS_PER_SCREEN columns wide and
 * GUI_LAUNCHER_ROWS_PER_SCREEN rows tall fitting the window at once. Applets fill the grid row by row
 * left to right, exactly like text flows, so the grid grows downwards and scrolls vertically should the
 * selected cell fall off the top or bottom (and horizontally in the same manner, though the columns
 * always span the full width). Each cell holds the applet icon on top and its word-wrapped, centred name
 * below. All dimensions are in pixels. */
#define GUI_LAUNCHER_APPLETS_PER_SCREEN 3
#define GUI_LAUNCHER_ROWS_PER_SCREEN 2
#define GUI_LAUNCHER_CELL_PAD 2
#define GUI_LAUNCHER_ICON_SIZE 32
#define GUI_LAUNCHER_ICON_TOP 8
#define GUI_LAUNCHER_SEL_PAD 2
#define GUI_LAUNCHER_NAME_GAP 4
#define GUI_LAUNCHER_LINE_H 10

/* Width of the scroll bar drawn along the right edge of the window, in pixels. */
#define GUI_LAUNCHER_SCROLLBAR_W 8

/* Upper bound on a single applet name, including the terminator. */
#define GUI_LAUNCHER_NAME_MAX 64


/* Snapshot all applets advertised through the service locator into self->applets. */
static void gui_launcher_capture(GuiLauncher *self) {
	self->applet_count = 0;
	self->selected = 0;
	self->x_scroll = 0;
	self->y_scroll = 0;
	Applet *applet = NULL;
	for (size_t i = 0; self->applet_count < GUI_LAUNCHER_MAX_APPLETS &&
	     iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_APPLET, i, (Interface **)&applet) ==
	     ISERVICELOCATOR_RET_OK; i++) {
		self->applets[self->applet_count] = applet;
		self->applet_count++;
	}
}


/* Draw a single line horizontally centred around cx with its top at y, using the current font. The
 * line is cropped in place to max_w first (an ellipsis replaces the overflowing tail), so a word too
 * wide to fit on its own never overflows the cell. A solid rectangle padded by GUI_LAUNCHER_SEL_PAD is
 * filled behind the glyphs, then the text is drawn on top: a highlighted line is black text on a white
 * rectangle, a normal one white text on a black rectangle. Because the rectangle always covers the same
 * footprint, repainting a name this way overwrites any previous highlight and so needs no window clear
 * beforehand. */
static void gui_launcher_draw_line(GuiLauncher *self, char *line, int16_t cx, int16_t y, uint16_t max_w,
                                   bool highlight) {
	Painter *painter = &self->painter.painter;
	fb_painter_crop_text(&self->painter, line, max_w);
	uint16_t w = 0;
	uint16_t h = 0;
	painter->vmt->text_size(painter, line, &w, &h);
	int16_t x = (int16_t)(cx - w / 2);
	painter->vmt->set_pen(painter, highlight ? 0xffffffff : 0xff000000, 0);
	painter->vmt->set_brush(painter, highlight ? 0xffffffff : 0xff000000);
	painter->vmt->rect(painter, (int16_t)(x - GUI_LAUNCHER_SEL_PAD), (int16_t)(y - GUI_LAUNCHER_SEL_PAD),
	                   (uint16_t)(w + 2 * GUI_LAUNCHER_SEL_PAD), (uint16_t)(h + 2 * GUI_LAUNCHER_SEL_PAD));
	painter->vmt->set_pen(painter, highlight ? 0xff000000 : 0xffffffff, 1);
	painter->vmt->text(painter, x, y, line);
}


/* Draw text centred around cx, starting at top, greedily word-wrapping it in the current font so no
 * line is wider than max_w (a single word too wide to fit is placed on its own line and cropped
 * there). Returns the top y of the line that would follow, so blocks can be stacked. */
static int16_t gui_launcher_draw_wrapped(GuiLauncher *self, const char *text, int16_t cx, int16_t top,
                                         uint16_t max_w, bool highlight) {
	Painter *painter = &self->painter.painter;
	char line[GUI_LAUNCHER_NAME_MAX];
	size_t line_len = 0;
	line[0] = '\0';
	int16_t y = top;

	const char *p = text;
	while (*p != '\0') {
		/* Take the next whitespace-delimited word. */
		while (*p == ' ') {
			p++;
		}
		if (*p == '\0') {
			break;
		}
		const char *word = p;
		while (*p != '\0' && *p != ' ') {
			p++;
		}
		size_t word_len = (size_t)(p - word);
		if (word_len >= GUI_LAUNCHER_NAME_MAX) {
			word_len = GUI_LAUNCHER_NAME_MAX - 1;
		}

		/* Build the current line extended by this word and see whether it still fits. */
		char candidate[GUI_LAUNCHER_NAME_MAX];
		size_t clen = 0;
		if (line_len > 0) {
			memcpy(candidate, line, line_len);
			clen = line_len;
			candidate[clen++] = ' ';
		}
		if (clen + word_len >= GUI_LAUNCHER_NAME_MAX) {
			word_len = GUI_LAUNCHER_NAME_MAX - 1 - clen;
		}
		memcpy(candidate + clen, word, word_len);
		clen += word_len;
		candidate[clen] = '\0';

		uint16_t w = 0;
		painter->vmt->text_size(painter, candidate, &w, NULL);
		if (w > max_w && line_len > 0) {
			/* The word does not fit: flush the current line and start a new one with just the word. */
			gui_launcher_draw_line(self, line, cx, y, max_w, highlight);
			y = (int16_t)(y + GUI_LAUNCHER_LINE_H);
			memcpy(line, word, word_len);
			line[word_len] = '\0';
			line_len = word_len;
		} else {
			memcpy(line, candidate, clen + 1);
			line_len = clen;
		}
	}
	if (line_len > 0) {
		gui_launcher_draw_line(self, line, cx, y, max_w, highlight);
		y = (int16_t)(y + GUI_LAUNCHER_LINE_H);
	}
	return y;
}


/* Width of a single applet cell in pixels. The scroll bar along the right edge is excluded so cells never
 * draw underneath it. */
static uint16_t gui_launcher_cell_w(GuiLauncher *self) {
	return (uint16_t)((self->conf.geometry.w - GUI_LAUNCHER_SCROLLBAR_W) / GUI_LAUNCHER_APPLETS_PER_SCREEN);
}


/* Height of a single applet cell in pixels. */
static uint16_t gui_launcher_cell_h(GuiLauncher *self) {
	return (uint16_t)(self->conf.geometry.h / GUI_LAUNCHER_ROWS_PER_SCREEN);
}


/* Draw applet i's word-wrapped, centred name (bold) into its grid cell, highlighted when it is the
 * selected one. The caller must have an active painter frame. As the name is drawn on a solid rectangle
 * in both states (see gui_launcher_draw_line), repainting the previously and newly selected names is
 * enough to move the highlight without clearing the window or redrawing the icons. */
static void gui_launcher_draw_name(GuiLauncher *self, size_t i) {
	uint16_t cell_w = gui_launcher_cell_w(self);
	int16_t cell_left = (int16_t)((i % GUI_LAUNCHER_APPLETS_PER_SCREEN) * cell_w - self->x_scroll);
	int16_t cell_top = (int16_t)((i / GUI_LAUNCHER_APPLETS_PER_SCREEN) * gui_launcher_cell_h(self) -
	                             self->y_scroll);
	int16_t cx = (int16_t)(cell_left + cell_w / 2);
	uint16_t text_w = (uint16_t)(cell_w - 2 * GUI_LAUNCHER_CELL_PAD);

	self->painter.painter.vmt->set_font(&self->painter.painter, PAINTER_FONT_BOLD, NULL);
	const char *name = self->applets[i]->name;
	gui_launcher_draw_wrapped(self, (name != NULL) ? name : "(unnamed)", cx,
	                          (int16_t)(cell_top + GUI_LAUNCHER_ICON_TOP + GUI_LAUNCHER_ICON_SIZE +
	                                    GUI_LAUNCHER_NAME_GAP),
	                          text_w, i == self->selected);
}


/* Draw the vertical scroll bar along the right edge of the window: a black-filled, white-bordered track
 * spanning the whole window height with a white-filled scroller inside it. The scroller's height and
 * position show how much of the applet grid is visible and how far down it is scrolled, so the user can
 * tell whether there is more below. When everything fits at once the scroller fills the whole track. */
static void gui_launcher_draw_scrollbar(GuiLauncher *self) {
	Painter *painter = &self->painter.painter;
	int16_t bar_x = (int16_t)(self->conf.geometry.w - GUI_LAUNCHER_SCROLLBAR_W);
	uint16_t bar_h = self->conf.geometry.h;

	/* Track: black fill, white 1px border. */
	painter->vmt->set_pen(painter, 0xffffffff, 1);
	painter->vmt->set_brush(painter, 0xff000000);
	painter->vmt->rect(painter, bar_x, 0, GUI_LAUNCHER_SCROLLBAR_W, bar_h);

	/* Total grid height (all rows) versus the window height that is actually visible at once. */
	uint16_t cell_h = gui_launcher_cell_h(self);
	size_t rows = (self->applet_count + GUI_LAUNCHER_APPLETS_PER_SCREEN - 1) / GUI_LAUNCHER_APPLETS_PER_SCREEN;
	int32_t content_h = (int32_t)rows * cell_h;
	int32_t inner_h = (int32_t)bar_h - 2;

	/* Scroller sized and positioned proportionally within the track's 1px inset. */
	int32_t thumb_h = inner_h;
	int32_t thumb_y = 1;
	if (content_h > bar_h) {
		thumb_h = inner_h * bar_h / content_h;
		thumb_y = 1 + (inner_h - thumb_h) * self->y_scroll / (content_h - bar_h);
	}

	painter->vmt->set_pen(painter, 0xffffffff, 0);
	painter->vmt->set_brush(painter, 0xffffffff);
	painter->vmt->rect(painter, (int16_t)(bar_x + 1), (int16_t)thumb_y,
	                   (uint16_t)(GUI_LAUNCHER_SCROLLBAR_W - 2), (uint16_t)thumb_h);
}


/* Fully render the captured applet list into the launcher window as a grid of cells scrolled by x_scroll
 * and y_scroll, each showing the applet icon on top and its word-wrapped name (bold) below it. This
 * clears the window and repaints every visible cell; it is used on the initial show and while scrolling.
 * A selection change that does not scroll repaints only the affected names in place (see
 * gui_launcher_select). */
static void gui_launcher_render(GuiLauncher *self) {
	Painter *painter = &self->painter.painter;

	painter->vmt->begin(painter);

	/* Clear the whole window to a black background. */
	painter->vmt->set_pen(painter, 0xff000000, 1);
	painter->vmt->set_brush(painter, 0xff000000);
	painter->vmt->rect(painter, 0, 0, self->conf.geometry.w, self->conf.geometry.h);

	uint16_t cell_w = gui_launcher_cell_w(self);
	uint16_t cell_h = gui_launcher_cell_h(self);

	for (size_t i = 0; i < self->applet_count; i++) {
		/* Row-major grid position: applets flow left to right, then down onto the next row. */
		int16_t cell_left = (int16_t)((i % GUI_LAUNCHER_APPLETS_PER_SCREEN) * cell_w - self->x_scroll);
		int16_t cell_top = (int16_t)((i / GUI_LAUNCHER_APPLETS_PER_SCREEN) * cell_h - self->y_scroll);
		/* Skip cells scrolled fully off any edge of the window. */
		if (cell_left + cell_w <= 0 || cell_left >= self->conf.geometry.w ||
		    cell_top + cell_h <= 0 || cell_top >= self->conf.geometry.h) {
			continue;
		}

		/* Icon centred at the top of the cell, when the applet provides one. */
		if (self->applets[i]->icon != NULL) {
			painter->vmt->image(painter, (int16_t)(cell_left + cell_w / 2 - GUI_LAUNCHER_ICON_SIZE / 2),
			                    (int16_t)(cell_top + GUI_LAUNCHER_ICON_TOP), self->applets[i]->icon,
			                    PAINTER_MODE_INVERTED);
		}

		gui_launcher_draw_name(self, i);
	}

	gui_launcher_draw_scrollbar(self);

	painter->vmt->end(painter);
}


/* Bring the selected cell fully into view. If a scroll is needed (the cell hangs off any edge) x_scroll
 * and y_scroll are animated towards their targets in equal steps with a short delay between them, the
 * grid being fully redrawn at each step, and true is returned. If the cell is already fully visible
 * nothing is drawn and false is returned, leaving the caller to repaint in place. */
static bool gui_launcher_scroll_to_selected(GuiLauncher *self) {
	uint16_t cell_w = gui_launcher_cell_w(self);
	uint16_t cell_h = gui_launcher_cell_h(self);
	int16_t cell_left = (int16_t)((self->selected % GUI_LAUNCHER_APPLETS_PER_SCREEN) * cell_w);
	int16_t cell_right = (int16_t)(cell_left + cell_w);
	int16_t cell_top = (int16_t)((self->selected / GUI_LAUNCHER_APPLETS_PER_SCREEN) * cell_h);
	int16_t cell_bottom = (int16_t)(cell_top + cell_h);

	int16_t x_target = self->x_scroll;
	if (cell_left - self->x_scroll < 0) {
		x_target = cell_left;
	} else if (cell_right - self->x_scroll > self->conf.geometry.w) {
		x_target = (int16_t)(cell_right - self->conf.geometry.w);
	}

	int16_t y_target = self->y_scroll;
	if (cell_top - self->y_scroll < 0) {
		y_target = cell_top;
	} else if (cell_bottom - self->y_scroll > self->conf.geometry.h) {
		y_target = (int16_t)(cell_bottom - self->conf.geometry.h);
	}

	int16_t x_start = self->x_scroll;
	int16_t y_start = self->y_scroll;
	if (x_target == x_start && y_target == y_start) {
		return false;
	}

	/* Walk to the targets in equal steps, landing exactly on them at the last one. */
	const int steps = 3;
	for (int step = 1; step <= steps; step++) {
		self->x_scroll = (int16_t)(x_start + (int32_t)(x_target - x_start) * step / steps);
		self->y_scroll = (int16_t)(y_start + (int32_t)(y_target - y_start) * step / steps);
		gui_launcher_render(self);
		if (step < steps) {
			vTaskDelay(pdMS_TO_TICKS(10));
		}
	}
	return true;
}


/* Move the selection to new_selected and reflect it on screen. If bringing the new cell into view needs
 * a scroll, the whole grid is re-rendered as it animates; otherwise only the two affected names are
 * repainted in place (the old one reverting to normal, the new one becoming highlighted), which needs no
 * window clear nor icon redraw. */
static void gui_launcher_select(GuiLauncher *self, size_t new_selected) {
	size_t old_selected = self->selected;
	self->selected = new_selected;
	if (gui_launcher_scroll_to_selected(self)) {
		return;
	}

	Painter *painter = &self->painter.painter;
	painter->vmt->begin(painter);
	gui_launcher_draw_name(self, old_selected);
	gui_launcher_draw_name(self, new_selected);
	painter->vmt->end(painter);
}


/* Move the selection by delta entries (positive = next, negative = previous), clamped to the list, and
 * repaint. A delta spanning several entries lands on a single, clamped target. */
static void gui_launcher_move(GuiLauncher *self, int32_t delta) {
	if (self->applet_count == 0) {
		return;
	}
	int32_t target = (int32_t)self->selected + delta;
	if (target < 0) {
		target = 0;
	}
	if (target >= (int32_t)self->applet_count) {
		target = (int32_t)self->applet_count - 1;
	}
	if ((size_t)target != self->selected) {
		gui_launcher_select(self, (size_t)target);
	}
}


/* Hand the applet to the available runners in turn, returning true as soon as one starts it. Each
 * runner rejects applets of a foreign kind with a CANNOT_RUN status, so the applet's own type selects
 * which runner ends up executing it. */
static bool gui_launcher_launch(GuiLauncher *self, Applet *applet) {
	if (applet_runner_native_run(&self->runner, applet) == APPLET_RUNNER_NATIVE_RET_OK) {
		return true;
	}
	#if defined(CONFIG_SERVICE_APPLET_RUNNER_WREN)
	if (applet_runner_wren_run(&self->wren_runner, applet) == APPLET_RUNNER_WREN_RET_OK) {
		return true;
	}
	#endif
	return false;
}


/* Event loop: pumps the launcher window's routed event source. EV_REL_X moves the selection (a counter
 * knob/keypad, + next, - previous), ENTER launches and ESC hides the window. */
static void gui_launcher_task(void *p) {
	GuiLauncher *self = (GuiLauncher *)p;

	/* Events routed to this window by the compositor (only while it is the top-level window). */
	Event *event = NULL;
	self->window->vmt->get_event(self->window, &event);

	self->can_run = true;
	self->running = true;
	while (self->can_run) {
		enum event_type type = EV_TYPE_NONE;
		enum event_code code = EV_CODE_NONE;
		int32_t value = 0;
		if (event->vmt->listen(event, &type, &code, &value) != EV_RET_OK) {
			continue;
		}

		/* Relative selection: EV_REL_X shifts the selection by the reported count (+ next, - previous). */
		if (type == EV_TYPE_REL && code == EV_REL_X) {
			gui_launcher_move(self, value);
			continue;
		}

		/* Everything else is a discrete key; act on the press only. */
		if (type != EV_TYPE_KEY || value != 1) {
			continue;
		}

		switch (code) {
			case EV_KEY_ESC:
				self->window->vmt->show(self->window, false);
				break;

			case EV_KEY_ENTER:
				/* Hide the launcher so the applet window becomes the top-level window, then hand the
				 * selected applet to the runners in turn. If none accepts it (all return CANNOT_RUN or
				 * fail), bring the launcher back. */
				if (self->selected < self->applet_count) {
					self->window->vmt->show(self->window, false);
					if (!gui_launcher_launch(self, self->applets[self->selected])) {
						self->window->vmt->show(self->window, true);
					}
				}
				break;

			default:
				/* passthrough */
		}
	}
	self->running = false;
	vTaskDelete(NULL);
}


gui_launcher_ret_t gui_launcher_init(GuiLauncher *self, const struct gui_launcher_conf *conf) {
	if (u_assert(self != NULL) ||
	    u_assert(conf != NULL)) {
		return GUI_LAUNCHER_RET_NULL;
	}
	memset(self, 0, sizeof(GuiLauncher));
	memcpy(&self->conf, conf, sizeof(struct gui_launcher_conf));

	if (u_assert(self->conf.compositor != NULL) ||
	    u_assert(self->conf.geometry.w > 0) ||
	    u_assert(self->conf.geometry.h > 0)) {
		return GUI_LAUNCHER_RET_FAILED;
	}

	/* Runner used to launch the selected applet. It creates the applet's window on the same compositor
	 * as the launcher, using the launcher geometry as the applet's preferred window geometry. */
	struct applet_runner_native_conf runner_conf = {
		.stdio = NULL,
		.logger = system_log,
		.compositor = self->conf.compositor,
		.fb = NULL,
		.event = NULL,
		.geometry = self->conf.geometry,
	};
	applet_runner_native_init(&self->runner, &runner_conf);

	/* The Wren runner, when built in, shares the same window placement as the native one so interpreted
	 * applets get a window identical to compiled ones. */
	#if defined(CONFIG_SERVICE_APPLET_RUNNER_WREN)
	struct applet_runner_wren_conf wren_runner_conf = {
		.stdio = NULL,
		.logger = system_log,
		.compositor = self->conf.compositor,
		.fb = NULL,
		.event = NULL,
		.geometry = self->conf.geometry,
	};
	applet_runner_wren_init(&self->wren_runner, &wren_runner_conf);
	#endif

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("init ok, %ux%u"),
	      (unsigned)self->conf.geometry.w, (unsigned)self->conf.geometry.h);
	return GUI_LAUNCHER_RET_OK;
}


gui_launcher_ret_t gui_launcher_free(GuiLauncher *self) {
	if (u_assert(self != NULL)) {
		return GUI_LAUNCHER_RET_FAILED;
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

	return GUI_LAUNCHER_RET_OK;
}


gui_launcher_ret_t gui_launcher_show(GuiLauncher *self) {
	if (u_assert(self != NULL)) {
		return GUI_LAUNCHER_RET_FAILED;
	}

	/* Create the window and start the event loop task on the first show. */
	if (self->window == NULL) {
		if (self->conf.compositor->factory.vmt->create(&self->conf.compositor->factory, &self->conf.geometry,
		    &self->window) != WINDOW_RET_OK) {
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create the window"));
			return GUI_LAUNCHER_RET_FAILED;
		}

		Fb *win_fb = NULL;
		self->window->vmt->get_fb(self->window, &win_fb);
		if (fb_painter_init(&self->painter, win_fb) != FB_PAINTER_RET_OK) {
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start the window painter"));
			self->conf.compositor->factory.vmt->destroy(&self->conf.compositor->factory, self->window);
			self->window = NULL;
			return GUI_LAUNCHER_RET_FAILED;
		}

		self->window->vmt->set_title(self->window, "Launcher");

		xTaskCreate(gui_launcher_task, "gui-launcher", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &self->task);
		if (self->task == NULL) {
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
			fb_painter_free(&self->painter);
			self->conf.compositor->factory.vmt->destroy(&self->conf.compositor->factory, self->window);
			self->window = NULL;
			return GUI_LAUNCHER_RET_FAILED;
		}
	}

	/* Discover the applets, render them and bring the window on screen. */
	gui_launcher_capture(self);
	gui_launcher_render(self);
	self->window->vmt->to_front(self->window);
	self->window->vmt->show(self->window, true);

	return GUI_LAUNCHER_RET_OK;
}
