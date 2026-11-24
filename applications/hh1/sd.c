/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * nwdaq-main-hh1 application SD card auto-mount
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include <main.h>

#include "app.h"

#define MODULE_NAME "hh1-sd"

#define APP_SD_PERIOD_MS 500
#define APP_SD_NOTIFY_TIMEOUT_MS 5000


/* Format a size in decimal units as printed on SD cards, with one decimal place above 1 GB. */
static void app_sd_size_str(uint64_t size, char *s, size_t len) {
	if (size >= 1000000000ULL) {
		uint64_t tenths = size / 100000000ULL;
		snprintf(s, len, "%lu.%lu GB", (unsigned long)(tenths / 10), (unsigned long)(tenths % 10));
	} else {
		snprintf(s, len, "%lu MB", (unsigned long)(size / 1000000ULL));
	}
}


static void app_sd_notify(App *self, enum notify_window_icon icon, const char *message) {
	const struct notify_window_action actions[] = {
		{.key = EV_KEY_ESC, .label = "ESC: Dismiss"},
	};
	size_t index = 0;
	gui_wm_fs_notify(&self->gui, icon, message, actions, sizeof(actions) / sizeof(actions[0]),
	                 APP_SD_NOTIFY_TIMEOUT_MS, &index, NULL);
}


/* The filesystem is mounted on its first access, reading its info triggers it. The mount result message with the
 * filesystem info and the card size is prepared in @p message. */
static bool app_sd_mount(App *self, const struct block_status *status, char *message, size_t size) {
	char label[12] = {0};
	struct fs_info info = {.label = label, .label_size = sizeof(label)};
	if (self->sd_fs->vmt->info(self->sd_fs, &info) != FS_RET_OK) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("cannot mount the SD card"));
		snprintf(message, size, "Cannot mount the SD card");
		return false;
	}

	char card[16];
	char total[16];
	char used[16];
	app_sd_size_str((uint64_t)status->block_count * status->block_size, card, sizeof(card));
	app_sd_size_str(info.size_total, total, sizeof(total));
	app_sd_size_str(info.size_used, used, sizeof(used));
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("SD card mounted, label '%s', card %s, filesystem %s, used %s"),
		label, card, total, used);
	snprintf(message, size, "SD card mounted\n%s\nCard size %s\nUsed %s of %s",
	         (label[0] != '\0') ? label : "(no label)", card, used, total);
	return true;
}


/* Poll the SD card status. Whenever a card is inserted (or present at startup), its filesystem is mounted and the
 * mount result is notified. A removal is notified as well. An unknown status (the driver is stopped in the off state)
 * is ignored. */
static void app_sd_task(void *p) {
	App *self = p;

	bool present = false;
	while (true) {
		struct block_status status = {0};
		if (self->sd->vmt->get_status(self->sd, &status) == BLOCK_RET_OK) {
			if (status.present && !present) {
				char message[128];
				bool mounted = app_sd_mount(self, &status, message, sizeof(message));
				app_sd_notify(self, mounted ? NOTIFY_WINDOW_ICON_SD : NOTIFY_WINDOW_ICON_NO_SD, message);
			} else if (!status.present && present) {
				app_sd_notify(self, NOTIFY_WINDOW_ICON_NO_SD, "SD card removed");
			}
			present = status.present;
		}
		vTaskDelay(pdMS_TO_TICKS(APP_SD_PERIOD_MS));
	}
}


app_ret_t app_sd_init(App *self) {
	if (self->sd == NULL || self->sd->vmt->get_status == NULL || self->sd_fs == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("no SD card or filesystem, auto-mount disabled"));
		return APP_RET_FAILED;
	}

	xTaskCreate(app_sd_task, "sd", configMINIMAL_STACK_SIZE + 384, (void *)self, 1, &self->sd_task);
	if (self->sd_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		return APP_RET_FAILED;
	}

	return APP_RET_OK;
}
