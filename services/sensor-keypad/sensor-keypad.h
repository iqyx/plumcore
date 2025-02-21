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
#include <interfaces/event.h>


#define SENSOR_KEYPAD_EMA_WEIGHT 0.05f
#define SENSOR_KEYPAD_EVENT_QUEUE_SIZE 4

typedef enum  {
	SENSOR_KEYPAD_RET_OK = 0,
	SENSOR_KEYPAD_RET_FAILED,
} sensor_keypad_ret_t;

struct sensor_keypad_event {
	enum event_type type;
	enum event_code code;
	int32_t value;

};

struct sensor_keypad_key {
	Sensor *input;
	enum event_type type;
	enum event_code code;

	/* Runtime data */
	float value;
	float ema;
	float threshold;

	bool down;

};

typedef struct sensor_keypad {
	struct sensor_keypad_key *keys;
	TaskHandle_t keypad_task;
	QueueHandle_t event_queue;

	Event event;
} SensorKeypad;


sensor_keypad_ret_t sensor_keypad_init(SensorKeypad *self, struct sensor_keypad_key *keys);

