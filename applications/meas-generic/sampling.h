/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * meas-generic periodic sensor sampling and compensation
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include "config.h"

#include <stdint.h>
#include <stddef.h>

#include <main.h>

#include <interfaces/sensor.h>
#include <interfaces/mq.h>

#include <services/mq-compensation/mq-compensation.h>

/*
 * Sampling drives the periodic measurement loop: it discovers the sensors named in the Kconfig list, registers a
 * polynomial compensation channel for each and, from a background task, samples them at a configurable interval and
 * publishes the raw values on the message queue. The compensation service republishes the compensated results on the
 * corresponding output topics.
 */

/* Upper bound on the number of measurement sensors the application can drive. The actual number is derived at runtime
 * from the comma-separated sensor name list configured in Kconfig. */
#define SAMPLING_MAX_SENSORS CONFIG_APP_MEAS_GENERIC_MAX_SENSORS

/* Longest sensor name and derived topic string kept per channel. The compensation service imposes its own limit on
 * the topic length it stores. */
#define SAMPLING_NAME_LEN 32
#define SAMPLING_TOPIC_LEN MQ_COMPENSATION_MAX_TOPIC_LEN

typedef enum {
	SAMPLING_RET_OK = 0,
	SAMPLING_RET_FAILED,
} sampling_ret_t;

/* One measured quantity discovered by name from the service locator, sampled periodically and published to its raw
 * input topic. A compensation channel republishes it on the output topic. */
struct sampling_channel {
	char name[SAMPLING_NAME_LEN];
	char input_topic[SAMPLING_TOPIC_LEN];
	Sensor *sensor;
};

typedef struct sampling {
	/* Message queue the raw values are published on. */
	Mq *mq;
	MqClient *mqc;

	/* Measurement channels discovered from the configured sensor name list. */
	struct sampling_channel channels[SAMPLING_MAX_SENSORS];
	size_t channel_count;

	/* Optional board temperature feeding the compensation polynomial. */
	Sensor *temp;
	char temp_topic[SAMPLING_TOPIC_LEN];

	/* Interval between two sampling cycles in milliseconds. Exposed in the configuration tree and read by the
	 * sampling task on every cycle so it can be changed at runtime. */
	uint32_t sample_interval_ms;

	/* Polynomial offset/gain/temperature compensation of the raw measured values. The compensated results are
	 * republished on the corresponding output topics. */
	MqCompensation comp;

	TaskHandle_t task;
} Sampling;


/**
 * @brief Initialise the sampling instance and start the background sampling task
 *
 * Discovers the message queue from the service locator and opens a client on it, registers a compensation channel for
 * every discovered sensor, sets up the optional temperature compensation and starts the periodic sampling task.
 *
 * @param self Preallocated memory for the instance
 * @return SAMPLING_RET_FAILED on error, SAMPLING_RET_OK otherwise.
 */
sampling_ret_t sampling_init(Sampling *self);

/**
 * @brief Free the sampling instance and release all allocated resources
 *
 * @param self Instance of the sampling
 * @return SAMPLING_RET_FAILED on error, SAMPLING_RET_OK otherwise.
 */
sampling_ret_t sampling_free(Sampling *self);
