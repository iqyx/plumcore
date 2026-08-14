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
 * out a top status bar (icons and texts, only a text placeholder for now). Input arrives from an
 * Event source and is pumped by an internal task (routing to be implemented).
 *
 * The status bar is an ordinary compositor window painted with the fb-painter service, so application
 * content can occupy the free area below it.
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

#define MODULE_NAME "gui-wm-fs"

/* A single short key-click beep: 20 ms at 2700 Hz. */
#define GUI_WM_FS_BEEPER_SEQ_KEY_CLICK BEEPER_SEQ { \
	BEEPER_SEQ_BEEP | BEEPER_SEQ_FREQ_HZ(2700) | BEEPER_SEQ_TIME_MS(20), \
	BEEPER_SEQ_END \
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
		if (c == EV_KEY_F5 && v != 0 && g->window_list_up) {
			window_list_show(&g->window_list);
			continue;
		}

		/* F2 pops up the applet launcher overlay. Handled globally, so it is not forwarded to any
		 * window. */
		if (c == EV_KEY_F6 && v != 0 && g->launcher_up) {
			gui_launcher_show(&g->launcher);
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

	/* Window list overlay filling the area below the top bar. */
	if (window_list_init(&self->window_list, &(struct window_list_conf){
		.compositor = &self->compositor,
		.geometry = {
			.x = 0,
			.y = GUI_WM_FS_TOPBAR_H,
			.w = (uint16_t)self->compositor.out_w,
			.h = (uint16_t)(self->compositor.out_h - GUI_WM_FS_TOPBAR_H),
		},
	}) != WINDOW_LIST_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start the window list"));
		goto err;
	}
	self->window_list_up = true;

	/* Applet launcher overlay filling the area below the top bar. */
	if (gui_launcher_init(&self->launcher, &(struct gui_launcher_conf){
		.compositor = &self->compositor,
		.geometry = {
			.x = 0,
			.y = GUI_WM_FS_TOPBAR_H,
			.w = (uint16_t)self->compositor.out_w,
			.h = (uint16_t)(self->compositor.out_h - GUI_WM_FS_TOPBAR_H),
		},
	}) != GUI_LAUNCHER_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start the applet launcher"));
		goto err;
	}
	self->launcher_up = true;

	/* Bring the launcher up right away so it is the top-level window after startup. */
	gui_launcher_show(&self->launcher);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("init ok"));
	return GUI_WM_FS_RET_OK;
err:
	return GUI_WM_FS_RET_FAILED;
}


gui_wm_fs_ret_t gui_wm_fs_free(GuiWmFs *self) {
	/* Tear the overlays down before the compositor, they destroy their windows through the factory. */
	if (self->launcher_up) {
		gui_launcher_free(&self->launcher);
		self->launcher_up = false;
	}
	if (self->window_list_up) {
		window_list_free(&self->window_list);
		self->window_list_up = false;
	}
	if (self->status_bar_up) {
		status_bar_free(&self->status_bar);
		self->status_bar_up = false;
	}

	if (self->compositor_up) {
		fb_compositor_free(&self->compositor);
		self->compositor_up = false;
	}

	return GUI_WM_FS_RET_OK;
}


