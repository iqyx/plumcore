/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * nwdaq-main-hh1 battery monitor application
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <main.h>
#include "config.h"

#include <interfaces/power.h>
#include <interfaces/fb.h>
#include <interfaces/window.h>
#include <interfaces/stream.h>
#include <interfaces/sensor.h>
#include <interfaces/painter.h>
#include <interfaces/event.h>
#include <interfaces/beeper.h>
#include <services/fb-compositor/fb-compositor.h>
#include <services/fb-painter/fb-painter.h>
#include <services/gui-wm-fs/gui-wm-fs.h>

typedef enum {
	APP_RET_OK = 0,
	APP_RET_FAILED,
} app_ret_t;

typedef struct {
	TaskHandle_t task;

	/* Battery charging power device advertised by the BQ25798 charger driver. */
	Power *charger;

	/* Inductive keypad event source advertised by the port. */
	Event *keypad;

	/* Piezo beeper advertised by the port, used for key-press feedback. */
	Beeper *beeper;

	/* Battery fuel gauge measurements advertised by the BQ27441 driver. */
	Sensor *bat_voltage;
	Sensor *bat_current;
	Sensor *bat_soc;
	Sensor *bat_soh;
	Sensor *bat_remaining;

	/* Serial console stream advertised by the port. */
	Stream *console;

	/* LCD framebuffer advertised by the port, driven through the compositor below. */
	Fb *lcd;

	/* Full-screen GUI window manager running on top of the LCD (owns the compositor and its bars). */
	GuiWmFs gui;
} App;


/**
 * @brief Prepare the new instance, allocate resources and start
 *        the application task.
 *
 * @param self Preallocated memory for the instance
 * @return APP_RET_FAILED if the application initialization failed,
 *         APP_RET_OK otherwise
 */
app_ret_t app_init(App *self);

/**
 * @brief Free the application instance, release all allocated resources
 *
 * @param self Instance of the application
 * @return APP_RET_FAILED on error,
 *         APP_RET_OK otherwise.
 */
app_ret_t app_free(App *self);
