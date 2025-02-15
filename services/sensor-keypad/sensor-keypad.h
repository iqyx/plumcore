/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Keypad implemented using Sensor devices
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <main.h>

#include <interfaces/sensor.h>


#define SENSOR_KEYPAD_EMA_WEIGHT 0.05f

typedef enum  {
	SENSOR_KEYPAD_RET_OK = 0,
	SENSOR_KEYPAD_RET_FAILED,
} sensor_keypad_ret_t;

struct sensor_keypad_key {
	Sensor *input;
	uint32_t code;

	/* Runtime data */
	float value;
	float ema;
	float threshold;

	bool down;

};

typedef struct sensor_keypad {
	struct sensor_keypad_key *keys;
	TaskHandle_t keypad_task;
} SensorKeypad;


sensor_keypad_ret_t sensor_keypad_init(SensorKeypad *self, struct sensor_keypad_key *keys);

