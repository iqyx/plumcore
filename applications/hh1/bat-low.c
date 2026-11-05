/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * nwdaq-main-hh1 application low battery notification
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdbool.h>

#include <main.h>

#include "app.h"

#if defined(CONFIG_APP_HH1_BAT_LOW_NOTIFY_ENABLE)

#define MODULE_NAME "hh1-bat-low"

#define APP_BAT_LOW_NOTIFY_TIMEOUT_MS 5000


/* The battery is low if its voltage is below the threshold and it is not charging (positive current). */
static bool app_bat_low(App *self) {
	float voltage = 0.0f;
	float current = 0.0f;
	if (self->bat_voltage->vmt->value_f(self->bat_voltage, &voltage) != SENSOR_RET_OK ||
	    self->bat_current->vmt->value_f(self->bat_current, &current) != SENSOR_RET_OK) {
		return false;
	}
	return voltage < (float)CONFIG_APP_HH1_BAT_LOW_NOTIFY_VOLTAGE / 1000.0f && current <= 0.0f;
}


/* Check the battery every notification period and show the notification while it is low. */
static void app_bat_low_task(void *p) {
	App *self = p;

	const struct notify_window_action actions[] = {
		{.key = EV_KEY_ESC, .label = "ESC: Dismiss"},
	};
	while (true) {
		vTaskDelay(pdMS_TO_TICKS(CONFIG_APP_HH1_BAT_LOW_NOTIFY_PERIOD * 1000));
		if (!app_bat_low(self)) {
			continue;
		}
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("battery low"));
		size_t index = 0;
		gui_wm_fs_notify(&self->gui, NOTIFY_WINDOW_ICON_WARNING, "Battery low", actions,
		                 sizeof(actions) / sizeof(actions[0]), APP_BAT_LOW_NOTIFY_TIMEOUT_MS, &index);
	}
}


app_ret_t app_bat_low_init(App *self) {
	if (!self->gui.compositor_up || self->bat_voltage == NULL || self->bat_current == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("no GUI or battery sensors, notification disabled"));
		return APP_RET_FAILED;
	}

	xTaskCreate(app_bat_low_task, "bat-low", configMINIMAL_STACK_SIZE + 128, (void *)self, 1, &self->bat_low_task);
	if (self->bat_low_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		return APP_RET_FAILED;
	}

	return APP_RET_OK;
}

#endif
