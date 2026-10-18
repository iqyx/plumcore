/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Full-screen GUI window manager configuration tree
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 *
 * All GUI configuration lives in a single Conf tree whose root is the "gui" node. The application advertises it
 * as "service gui", mounting it at /service/gui:
 *
 *     /service/gui/accel/wake-enable        wake up to D0 on device movement
 *     /service/gui/accel/wake-threshold     movement threshold (raw accelerometer LSB)
 *     /service/gui/accel/wake-window        movement detection window length (s)
 *     /service/gui/accel/awake-upside-down  hold D2 while the display faces down
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "config.h"
#include <interfaces/conf.h>
#include <configlib.h>

#include "gui-wm-fs.h"

#define GUI_WM_FS_ACCEL_THRESHOLD_LSB 250.0f
#define GUI_WM_FS_ACCEL_WINDOW_S 0.5f


gui_wm_fs_ret_t gui_wm_fs_conf_init(GuiWmFs *self) {
	/* Defaults, overridden once the stored configuration is loaded. */
	self->settings.accel.wake_enable = true;
	self->settings.accel.wake_threshold = GUI_WM_FS_ACCEL_THRESHOLD_LSB;
	self->settings.accel.wake_window = GUI_WM_FS_ACCEL_WINDOW_S;
	self->settings.accel.awake_upside_down = false;

	configlib_init_map(&self->conf_tree.gui, "gui", NULL, CONF_SUBTREE);
	configlib_init_map_append(&self->conf_tree.accel, "accel", NULL, CONF_SUBTREE, &self->conf_tree.gui, CONF_DIR_CHILD);

	/* The first value is added as the child, the rest are chained after it to keep the order. */
	configlib_init_map_append(&self->conf_tree.accel_wake_enable, "wake-enable", &self->settings.accel.wake_enable,
		CONF_B, &self->conf_tree.accel, CONF_DIR_CHILD);
	self->conf_tree.accel_wake_enable.flags = CONF_READ | CONF_WRITE;
	configlib_set_default(&self->conf_tree.accel_wake_enable, (union conf_val){.b = true});
	configlib_set_description(&self->conf_tree.accel_wake_enable, "Wake up on device movement",
		"When set, movement detected by the accelerometer counts as user activity and brings the device back "
		"to full power (D0), like a key press. It never wakes the device from the off state.");

	configlib_init_map_append(&self->conf_tree.accel_wake_threshold, "wake-threshold",
		&self->settings.accel.wake_threshold, CONF_F, &self->conf_tree.accel_wake_enable, CONF_DIR_NEXT);
	self->conf_tree.accel_wake_threshold.flags = CONF_READ | CONF_WRITE;
	configlib_set_default(&self->conf_tree.accel_wake_threshold, (union conf_val){.f = GUI_WM_FS_ACCEL_THRESHOLD_LSB});
	configlib_set_constraint(&self->conf_tree.accel_wake_threshold, (union conf_constraint){
		.numeric = {.min = {.f = 0.0f}, .max = {.f = 32768.0f}},
	});
	configlib_set_description(&self->conf_tree.accel_wake_threshold, "Movement detection threshold (LSB)",
		"Standard deviation of the acceleration vector over the wake-window, in raw accelerometer LSB, above which "
		"the device is considered moving. Lower values make the movement detection more sensitive.");

	configlib_init_map_append(&self->conf_tree.accel_wake_window, "wake-window", &self->settings.accel.wake_window,
		CONF_F, &self->conf_tree.accel_wake_threshold, CONF_DIR_NEXT);
	self->conf_tree.accel_wake_window.flags = CONF_READ | CONF_WRITE | CONF_DETAIL;
	configlib_set_default(&self->conf_tree.accel_wake_window, (union conf_val){.f = GUI_WM_FS_ACCEL_WINDOW_S});
	configlib_set_constraint(&self->conf_tree.accel_wake_window, (union conf_constraint){
		.numeric = {.min = {.f = 0.1f}, .max = {.f = 2.0f}},
	});
	configlib_set_description(&self->conf_tree.accel_wake_window, "Movement detection window (s)",
		"Length of the window the acceleration variance is computed over. A short jolt is averaged over the whole "
		"window: a longer window ignores brief jolts, a shorter one reacts to them. It also sets how fast the "
		"display facing down is detected.");

	configlib_init_map_append(&self->conf_tree.accel_awake_upside_down, "awake-upside-down",
		&self->settings.accel.awake_upside_down, CONF_B, &self->conf_tree.accel_wake_window, CONF_DIR_NEXT);
	self->conf_tree.accel_awake_upside_down.flags = CONF_READ | CONF_WRITE;
	configlib_set_default(&self->conf_tree.accel_awake_upside_down, (union conf_val){.b = false});
	configlib_set_description(&self->conf_tree.accel_awake_upside_down, "Stay awake with the display facing down",
		"When set, the device does not go to D3 while it lies with its display facing down (upside down "
		"compared to the normal position). A D2 lock is held in the power manager for as long as it stays so.");

	return GUI_WM_FS_RET_OK;
}
