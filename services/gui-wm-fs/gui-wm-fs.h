/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Full-screen GUI window manager service
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "config.h"
#include "FreeRTOS.h"
#include "task.h"

#include <interfaces/fb.h>
#include <interfaces/event.h>
#include <interfaces/window.h>
#include <interfaces/painter.h>
#include <interfaces/beeper.h>
#include <interfaces/sensor.h>
#include <interfaces/pm.h>
#include <services/fb-compositor/fb-compositor.h>
#include <services/fb-painter/fb-painter.h>
#include <services/gui-launcher/gui-launcher.h>

#include "status-bar.h"
#include "window-list.h"

/* Height of the top status bar, in pixels. */
#define GUI_WM_FS_TOPBAR_H 18

typedef enum {
	GUI_WM_FS_RET_OK = 0,
	GUI_WM_FS_RET_FAILED,
	GUI_WM_FS_RET_NULL,
	GUI_WM_FS_RET_NOMEM,
} gui_wm_fs_ret_t;

/* Service configuration passed to gui_wm_fs_init(). Not typedef'd per project policy. */
struct gui_wm_fs_conf {
	/** Output framebuffer the GUI takes over full-screen. */
	Fb *fb;
	/** Input event source driving the GUI (optional, may be NULL). */
	Event *event;
	/** Beeper for key-press feedback (optional, may be NULL). */
	Beeper *beeper;

	/** Power manager, requested to full power on key activity and toggled between on_state and off_state by
	 * the onoff key (optional, may be NULL). */
	Pm *pm;
	/** Key event code that toggles the device power on/off (e.g. EV_KEY_ONOFF). */
	enum event_code onoff_event;
	/** Power state representing "on" (full operation); entered from off_state on the onoff key. */
	enum pm_state on_state;
	/** Power state representing "off" (low power); entered from on_state on the onoff key. While the manager
	 * is in this state all key presses other than onoff_event are gated. */
	enum pm_state off_state;

	Sensor *bat_soc;
	Sensor *bat_current;
};

typedef struct {
	struct gui_wm_fs_conf conf;

	/* Compositor owning the output framebuffer; all GUI content is drawn into its windows. Created
	 * in start(); compositor_up tracks whether it needs tearing down. */
	FbCompositor compositor;
	bool compositor_up;

	/* Top status bar overlay: a full-width bar hosting status icons and texts. */
	StatusBar status_bar;
	bool status_bar_up;

	/* Full-screen window list overlay, shown on demand (F1). */
	WindowList window_list;
	bool window_list_up;

	/* Applet launcher overlay, shown on demand (F2). */
	GuiLauncher launcher;
	bool launcher_up;

	/* Event source handed to the compositor. Its listen() pumps conf.event in the compositor input
	 * task's context, consumes global keys and forwards the rest to the top-level window. */
	Event event;
} GuiWmFs;


gui_wm_fs_ret_t gui_wm_fs_init(GuiWmFs *self, const struct gui_wm_fs_conf *conf);
gui_wm_fs_ret_t gui_wm_fs_free(GuiWmFs *self);
