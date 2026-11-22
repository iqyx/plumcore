/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * MQ sensor source, periodically publishing sensor values
 *
 * Copyright (c) 2023-2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <main.h>
#include <interfaces/sensor.h>
#include <interfaces/mq.h>
#include <interfaces/clock.h>


typedef enum {
	MQ_SENSOR_SOURCE_RET_OK = 0,
	MQ_SENSOR_SOURCE_RET_FAILED,
	MQ_SENSOR_SOURCE_RET_NULL,
} mq_sensor_source_ret_t;


/* A sensor published by the service and the topic its value is published on. */
struct mq_sensor_source_sensor {
	/** Sensor to read. A NULL sensor is skipped, eg. one which was not found by the service locator. */
	Sensor *sensor;
	/** Topic the sensor value is published on. A NULL topic terminates the sensor array. */
	const char *topic;
};


/* Service configuration passed to mq_sensor_source_init(). */
struct mq_sensor_source_conf {
	/** Message queue the sensor values are published to. */
	Mq *mq;
	/** Clock timestamping the published values. NULL to publish with a zero timestamp. */
	Clock *clock;
	/** Sensors to publish, terminated by an entry with a NULL topic. The array is not copied, it must be kept
	 *  by the caller as long as the service is running. */
	const struct mq_sensor_source_sensor *sensors;
	/** Period of reading and publishing all the sensors. */
	uint32_t period_ms;
};


typedef struct {
	struct mq_sensor_source_conf conf;

	/* A MQ client instance is created in start() */
	MqClient *mqc;

	/* Set to false to request thread stop */
	volatile bool can_run;

	/* True if the thread is running */
	volatile bool running;

	TaskHandle_t task;
} MqSensorSource;


/**
 * @brief Initialize the MQ sensor source service
 *
 * @param self Preallocated instance.
 * @param conf Service configuration. The structure is copied.
 *
 * @return MQ_SENSOR_SOURCE_RET_NULL on a NULL argument, MQ_SENSOR_SOURCE_RET_OK otherwise.
 */
mq_sensor_source_ret_t mq_sensor_source_init(MqSensorSource *self, const struct mq_sensor_source_conf *conf);

mq_sensor_source_ret_t mq_sensor_source_free(MqSensorSource *self);

/**
 * @brief Start publishing the sensor values
 *
 * Every @p conf->period_ms all the sensors are read and each value is published as a single float on the topic of
 * its sensor. All values of a period share the same timestamp. A sensor failing to read is not published in that
 * period.
 *
 * @return MQ_SENSOR_SOURCE_RET_FAILED on an invalid configuration or if the service cannot be started,
 *         MQ_SENSOR_SOURCE_RET_OK otherwise.
 */
mq_sensor_source_ret_t mq_sensor_source_start(MqSensorSource *self);

mq_sensor_source_ret_t mq_sensor_source_stop(MqSensorSource *self);
