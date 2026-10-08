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

		/* Sample the current power state once; both the onoff handling and the off-state gate below use it. */
		enum pm_state state = PM_STATE_NONE;
		if (g->conf.pm != NULL) {
			g->conf.pm->vmt->pm_get_state(g->conf.pm, &state);
		}

		/* While the system is in the off (low-power) state, the onoff key switches the device back on.
		 * Moving to the on state crosses the D3 -> D2 transition, which reboots into a clean, fully restored
		 * state, so this call does not return. */
		if (g->conf.pm != NULL && state == g->conf.off_state && c == g->conf.onoff_event && v != 0) {
			g->conf.pm->vmt->pm_set_state(g->conf.pm, g->conf.on_state);
			continue;
		}

		/* In the off state the onoff key handled above is the only live input; ignore everything else so no
		 * key reaches the GUI while the device is off. */
		if (g->conf.pm != NULL && state == g->conf.off_state) {
			continue;
		}

		/* Give a single short beep on every key press (not on release). */
		if (v != 0 && g->conf.beeper != NULL) {
			g->conf.beeper->vmt->sequence(g->conf.beeper, GUI_WM_FS_BEEPER_SEQ_KEY_CLICK);
		}

		/* Any key press counts as user activity: bring the system back to full power. */
		if (v != 0 && g->conf.pm != NULL) {
			g->conf.pm->vmt->pm_set_state(g->conf.pm, PM_STATE_D0);
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

		/* The onoff key while the device is on (any state other than off_state, which was handled and gated
		 * at the top of the loop) puts it into the off (low-power) state. */
		if (g->conf.pm != NULL && c == g->conf.onoff_event && v != 0) {
			vTaskDelay(100);
			g->conf.pm->vmt->pm_set_state(g->conf.pm, g->conf.off_state);
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
 * Accelerometer
 *********************************************************************************************************************/

#define GUI_WM_FS_ACCEL_PERIOD_MS 100
#define GUI_WM_FS_ACCEL_SAMPLES 8
#define GUI_WM_FS_ACCEL_WINDOW_S 0.5f
#define GUI_WM_FS_ACCEL_THRESHOLD_LSB 250.0f
#define GUI_WM_FS_ACCEL_QUIET_WINDOWS 6

/* Detect the device being handled. Samples are accumulated over a tumbling window, at its end the per-axis
 * variances are summed into the variance of the acceleration vector. The mean removes gravity, so the result
 * does not depend on the device orientation. Above the threshold the device is moving, it is considered still
 * again only after several consecutive quiet windows. */
static void gui_wm_fs_accel_task(void *p) {
	GuiWmFs *self = (GuiWmFs *)p;

	self->accel_running = true;
	self->conf.accel->vmt->start(self->conf.accel);

	float sample_rate_Hz = 0.0f;
	self->conf.accel->vmt->get_sample_rate(self->conf.accel, &sample_rate_Hz);

	float n = 0.0f;
	float s[3] = {0};
	float q[3] = {0};
	uint32_t quiet = GUI_WM_FS_ACCEL_QUIET_WINDOWS;
	while (self->accel_can_run) {
		/* Samples are X, Y, Z triplets of signed 16 bit values. */
		int16_t buf[GUI_WM_FS_ACCEL_SAMPLES][3];
		size_t read = 0;
		do {
			if (self->conf.accel->vmt->read(self->conf.accel, buf, GUI_WM_FS_ACCEL_SAMPLES, &read) != WAVEFORM_SOURCE_RET_OK) {
				break;
			}
			for (size_t i = 0; i < read; i++) {
				for (size_t a = 0; a < 3; a++) {
					s[a] += (float)buf[i][a];
					q[a] += (float)buf[i][a] * (float)buf[i][a];
				}
				n++;
			}
		} while (read == GUI_WM_FS_ACCEL_SAMPLES);

		if (n > 0.0f && n >= sample_rate_Hz * GUI_WM_FS_ACCEL_WINDOW_S) {
			float var = 0.0f;
			for (size_t a = 0; a < 3; a++) {
				var += q[a] / n - (s[a] / n) * (s[a] / n);
				s[a] = 0.0f;
				q[a] = 0.0f;
			}
			n = 0.0f;

			if (var > GUI_WM_FS_ACCEL_THRESHOLD_LSB * GUI_WM_FS_ACCEL_THRESHOLD_LSB) {
				if (quiet >= GUI_WM_FS_ACCEL_QUIET_WINDOWS) {
					u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("accel: device is moving"));
				}
				quiet = 0;

				/* Movement counts as user activity like a key press. Not in the off state, waking from it
				 * reboots the device and only the onoff key may do that. */
				enum pm_state state = PM_STATE_NONE;
				if (self->conf.pm != NULL) {
					self->conf.pm->vmt->pm_get_state(self->conf.pm, &state);
					if (state != self->conf.off_state) {
						self->conf.pm->vmt->pm_set_state(self->conf.pm, PM_STATE_D0);
					}
				}
			} else if (quiet < GUI_WM_FS_ACCEL_QUIET_WINDOWS) {
				quiet++;
				if (quiet == GUI_WM_FS_ACCEL_QUIET_WINDOWS) {
					u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("accel: device is still"));
				}
			}
		}

		vTaskDelay(pdMS_TO_TICKS(GUI_WM_FS_ACCEL_PERIOD_MS));
	}
	self->conf.accel->vmt->stop(self->conf.accel);
	self->accel_running = false;

	vTaskDelete(NULL);
}


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

	if (self->conf.accel != NULL) {
		self->accel_can_run = true;
		xTaskCreate(gui_wm_fs_accel_task, "gui-accel", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &self->accel_task);
		if (self->accel_task == NULL) {
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create the accelerometer task"));
			self->accel_can_run = false;
			goto err;
		}
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("init ok"));
	return GUI_WM_FS_RET_OK;
err:
	return GUI_WM_FS_RET_FAILED;
}


gui_wm_fs_ret_t gui_wm_fs_free(GuiWmFs *self) {
	/* Stop the accelerometer task first, it stops the accelerometer on its way out. */
	self->accel_can_run = false;
	while (self->accel_running) {
		vTaskDelay(100);
	}

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


