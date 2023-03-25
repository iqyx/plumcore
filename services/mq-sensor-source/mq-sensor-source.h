/* SPDX-License-Identifier: BSD-2-Clause
 *
 * MQ sensor source
 *
 * Copyright (c) 2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "config.h"
#include "FreeRTOS.h"
#include "task.h"

#include <interfaces/sensor.h>
#include <interfaces/mq.h>
#include <interfaces/clock.h>


typedef enum {
	MQ_SENSOR_SOURCE_RET_OK = 0,
	MQ_SENSOR_SOURCE_RET_FAILED,
} mq_sensor_source_ret_t;


typedef struct {
	const char *topic;
	Sensor *sensor;
	Mq *mq;
	MqClient *mqc;
	Clock *clock;
	uint32_t period_ms;

	TaskHandle_t task;
} MqSensorSource;


mq_sensor_source_ret_t mq_sensor_source_init(MqSensorSource *self, Sensor *sensor, const char *topic, Mq *mq, Clock *clock, uint32_t period_ms);
mq_sensor_source_ret_t mq_sensor_source_free(MqSensorSource *self);

