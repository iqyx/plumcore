/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Full-screen GUI window manager service
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 *
 * A minimal full-screen GUI: it takes over a framebuffer output through an fb-compositor and lays
 * out a fixed desktop of two internal windows. A top status bar (icons and texts, only a text
 * placeholder for now) and a bottom button bar hosting a row of virtual buttons. Input arrives from
 * an Event source and is pumped by an internal task (routing to be implemented).
 *
 * The two bars are ordinary compositor windows painted with the fb-painter service, so application
 * content can later occupy the free area between them.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "u_log.h"
#include "u_assert.h"

#include <interfaces/fb.h>
#include <interfaces/event.h>
#include <interfaces/window.h>
#include <interfaces/painter.h>

#include "gui-wm-fs.h"

#include "assets/assets.h"

#define MODULE_NAME "gui-wm-fs"

/* A single short key-click beep: 20 ms at 2700 Hz. */
#define GUI_WM_FS_BEEPER_SEQ_KEY_CLICK BEEPER_SEQ { \
	BEEPER_SEQ_BEEP | BEEPER_SEQ_FREQ_HZ(2700) | BEEPER_SEQ_TIME_MS(20), \
	BEEPER_SEQ_END \
}


/*********************************************************************************************************************
 * Bar windows and painting
 *********************************************************************************************************************/

/* Create a compositor window covering the given geometry (it allocates and owns the backing store)
 * and put a painter on its drawing surface. */
static gui_wm_fs_ret_t gui_wm_fs_create_bar(GuiWmFs *self, const struct window_geometry *geometry,
                                            Window **window, FbPainter *painter) {
	if (self->compositor.factory.vmt->create(&self->compositor.factory, geometry, window) != WINDOW_RET_OK) {
		return GUI_WM_FS_RET_FAILED;
	}

	Fb *win_fb = NULL;
	(*window)->vmt->get_fb(*window, &win_fb);
	if (fb_painter_init(painter, win_fb) != FB_PAINTER_RET_OK) {
		return GUI_WM_FS_RET_FAILED;
	}

	return GUI_WM_FS_RET_OK;
}


/* Paint the bottom button bar: a row of GUI_WM_FS_BUTTONS evenly spaced virtual buttons. */
static void gui_wm_fs_draw_buttonbar(GuiWmFs *self) {
	Painter *painter = &self->buttonbar_painter.painter;
	uint16_t bw = (uint16_t)(self->compositor.out_w / GUI_WM_FS_BUTTONS);

	painter->vmt->begin(painter);
	painter->vmt->set_pen(painter, 0xffffffff, 1);
	painter->vmt->set_brush(painter, 0xff000000);
	painter->vmt->rect(painter, 0, 0, bw, GUI_WM_FS_BUTTONBAR_H);
	painter->vmt->set_pen(painter, 0xff000000, 1);
	painter->vmt->rect(painter, 1, 1, bw - 2, GUI_WM_FS_BUTTONBAR_H);

	painter->vmt->set_font(painter, PAINTER_FONT_BOLD, NULL);
	painter->vmt->set_pen(painter, 0xffffffff, 1);
	painter->vmt->image(painter, 8, 2, &window_list_small_data, PAINTER_MODE_INVERTED);
	painter->vmt->text(painter, 32, 5, "F1");


	painter->vmt->end(painter);
}


/*********************************************************************************************************************
 * Input event source
 *********************************************************************************************************************/

/* Event source handed to the compositor. Its listen() runs in the compositor's input task context:
 * it borrows that caller to pump the upstream source, consumes globally-handled keys itself and
 * returns everything else so the compositor can route it to the top-level window. */
static event_ret_t gui_wm_fs_event_listen(Event *self, enum event_type *type, enum event_code *code, int32_t *value) {
	GuiWmFs *g = self->parent;

	while (true) {
		enum event_type t = EV_TYPE_NONE;
		enum event_code c = EV_CODE_NONE;
		int32_t v = 0;
		if (g->conf.event->vmt->listen(g->conf.event, &t, &c, &v) != EV_RET_OK) {
			return EV_RET_FAILED;
		}

		/* Give a single short beep on every key press (not on release). */
		if (v != 0 && g->conf.beeper != NULL) {
			//g->conf.beeper->vmt->sequence(g->conf.beeper, GUI_WM_FS_BEEPER_SEQ_KEY_CLICK);
		}

		/* F1 pops up the window list overlay (auto-hides after a few seconds). Handled globally, so
		 * it is not forwarded to any window. */
		if (c == EV_KEY_F1 && v != 0 && g->window_list_up) {
			window_list_show(&g->window_list);
			continue;
		}

		if (type != NULL) {
			*type = t;
		}
		if (code != NULL) {
			*code = c;
		}
		if (value != NULL) {
			*value = v;
		}
		return EV_RET_OK;
	}
}


static event_ret_t gui_wm_fs_event_subscribe(Event *self, enum event_type *type) {
	GuiWmFs *g = self->parent;

	if (g->conf.event->vmt->subscribe == NULL) {
		return EV_RET_FAILED;
	}
	return g->conf.event->vmt->subscribe(g->conf.event, type);
}


static const struct event_vmt gui_wm_fs_event_vmt = {
	.listen = gui_wm_fs_event_listen,
	.subscribe = gui_wm_fs_event_subscribe,
};


/*********************************************************************************************************************
 * Public API
 *********************************************************************************************************************/

gui_wm_fs_ret_t gui_wm_fs_init(GuiWmFs *self, const struct gui_wm_fs_conf *conf) {
	if (u_assert(self != NULL) ||
	    u_assert(conf != NULL)) {
		return GUI_WM_FS_RET_NULL;
	}
	memset(self, 0, sizeof(GuiWmFs));
	memcpy(&self->conf, conf, sizeof(struct gui_wm_fs_conf));

	self->event.parent = self;
	self->event.vmt = &gui_wm_fs_event_vmt;

	/* The compositor takes over the output framebuffer; the GUI draws into its windows only. It is
	 * given our own event source (which pumps conf.event and handles global keys) so it can route the
	 * remaining events to the top-level window. */
	if (fb_compositor_init(&self->compositor, self->conf.fb,
	    (self->conf.event != NULL) ? &self->event : NULL) != FB_COMPOSITOR_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start the compositor"));
		return GUI_WM_FS_RET_FAILED;
	}
	self->compositor_up = true;

	/* Full-width top status bar overlay anchored to the top edge. */
	if (status_bar_init(&self->status_bar, &(struct status_bar_conf){
		.compositor = &self->compositor,
		.geometry = {
			.x = 0,
			.y = 0,
			.w = (uint16_t)self->compositor.out_w,
			.h = GUI_WM_FS_TOPBAR_H,
		},
		.bat_soc = self->conf.bat_soc,
		.bat_current = self->conf.bat_current,
	}) != STATUS_BAR_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start the status bar"));
		goto err;
	}
	self->status_bar_up = true;

	/* Full-width button bar anchored to the bottom edge. */
	struct window_geometry bottom_geometry = {
		.x = 0,
		.y = (int16_t)(self->compositor.out_h - GUI_WM_FS_BUTTONBAR_H),
		.w = (uint16_t)self->compositor.out_w,
		.h = GUI_WM_FS_BUTTONBAR_H,
	};
	if (gui_wm_fs_create_bar(self, &bottom_geometry, &self->buttonbar, &self->buttonbar_painter) != GUI_WM_FS_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create the button bar window"));
		goto err;
	}
	self->buttonbar->vmt->set_title(self->buttonbar, "button bar");

	gui_wm_fs_draw_buttonbar(self);
	self->buttonbar->vmt->show(self->buttonbar, true);

	/* Window list overlay filling the area between the top and bottom bars. */
	if (window_list_init(&self->window_list, &(struct window_list_conf){
		.compositor = &self->compositor,
		.geometry = {
			.x = 0,
			.y = GUI_WM_FS_TOPBAR_H,
			.w = (uint16_t)self->compositor.out_w,
			.h = (uint16_t)(self->compositor.out_h - GUI_WM_FS_TOPBAR_H - GUI_WM_FS_BUTTONBAR_H),
		},
	}) != WINDOW_LIST_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start the window list"));
		goto err;
	}
	self->window_list_up = true;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("init ok"));
	return GUI_WM_FS_RET_OK;
err:
	return GUI_WM_FS_RET_FAILED;
}


gui_wm_fs_ret_t gui_wm_fs_free(GuiWmFs *self) {
	/* Tear the overlays down before the compositor, they destroy their windows through the factory. */
	if (self->window_list_up) {
		window_list_free(&self->window_list);
		self->window_list_up = false;
	}
	if (self->status_bar_up) {
		status_bar_free(&self->status_bar);
		self->status_bar_up = false;
	}

	if (self->buttonbar != NULL) {
		fb_painter_free(&self->buttonbar_painter);
		self->compositor.factory.vmt->destroy(&self->compositor.factory, self->buttonbar);
		self->buttonbar = NULL;
	}

	if (self->compositor_up) {
		fb_compositor_free(&self->compositor);
		self->compositor_up = false;
	}

	return GUI_WM_FS_RET_OK;
}


