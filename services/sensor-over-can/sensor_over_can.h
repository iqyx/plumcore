/*
 * Copyright (c) 2018, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"

#include "interfaces/can.h"
#include "interfaces/sensor.h"


#define SENSOR_OVER_CAN_NAME_LEN 32
#define SENSOR_OVER_CAN_QUANTITY_NAME_LEN 32
#define SENSOR_OVER_CAN_QUANTITY_SYMBOL_LEN 8
#define SENSOR_OVER_CAN_FRESH_VALUE_TIMEOUT_SEC 5

typedef enum {
	SENSOR_OVER_CAN_RET_OK = 0,
	SENSOR_OVER_CAN_RET_FAILED,
	SENSOR_OVER_CAN_RET_NULL,
	SENSOR_OVER_CAN_RET_BAD_ARG,
	SENSOR_OVER_CAN_RET_UNINITIALIZED,
} sensor_over_can_ret_t;

enum sensor_over_can_value_type {
	SENSOR_OVER_CAN_VALUE_TYPE_FLOAT = 0,
	SENSOR_OVER_CAN_VALUE_TYPE_UINT8,
	SENSOR_OVER_CAN_VALUE_TYPE_INT8,
	SENSOR_OVER_CAN_VALUE_TYPE_UINT16,
	SENSOR_OVER_CAN_VALUE_TYPE_INT16,
	SENSOR_OVER_CAN_VALUE_TYPE_UINT32,
	SENSOR_OVER_CAN_VALUE_TYPE_INT32,
};

enum sensor_over_can_value_status {
	SENSOR_OVER_CAN_VALUE_STATUS_STALLED = 0,
	SENSOR_OVER_CAN_VALUE_STATUS_FRESH,
};

typedef struct {
	/* We need to include a interface descriptor stub in order to be
	 * added to the service locator. */
	Interface interface;

	bool initialized;
	volatile bool can_run;
	volatile bool running;
	TaskHandle_t task;

	ICan *can;
	uint32_t can_id;
	ISensor sensor;
	float value;

	char name[SENSOR_OVER_CAN_NAME_LEN];

	char quantity_name[SENSOR_OVER_CAN_QUANTITY_NAME_LEN];
	char quantity_symbol[SENSOR_OVER_CAN_QUANTITY_SYMBOL_LEN];
	char unit_name[SENSOR_OVER_CAN_QUANTITY_NAME_LEN];
	char unit_symbol[SENSOR_OVER_CAN_QUANTITY_SYMBOL_LEN];

	enum sensor_over_can_value_type value_type;
	size_t buffer_offset;
	enum sensor_over_can_value_status value_status;
	uint32_t no_value_sec;
	uint32_t received_values;
} SensorOverCan;


sensor_over_can_ret_t sensor_over_can_init(SensorOverCan *self);
sensor_over_can_ret_t sensor_over_can_free(SensorOverCan *self);
sensor_over_can_ret_t sensor_over_can_start(SensorOverCan *self);
sensor_over_can_ret_t sensor_over_can_stop(SensorOverCan *self);
sensor_over_can_ret_t sensor_over_can_set_can_bus(SensorOverCan *self, ICan *can);
sensor_over_can_ret_t sensor_over_can_set_id(SensorOverCan *self, uint32_t id);
sensor_over_can_ret_t sensor_over_can_set_name(SensorOverCan *self, const char *name);
sensor_over_can_ret_t sensor_over_can_set_quantity_name(SensorOverCan *self, const char *name);
sensor_over_can_ret_t sensor_over_can_set_quantity_symbol(SensorOverCan *self, const char *symbol);
sensor_over_can_ret_t sensor_over_can_set_unit_name(SensorOverCan *self, const char *name);
sensor_over_can_ret_t sensor_over_can_set_unit_symbol(SensorOverCan *self, const char *symbol);
