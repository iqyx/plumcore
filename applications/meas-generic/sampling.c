/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * meas-generic periodic sensor sampling and compensation
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <main.h>

#include <interfaces/servicelocator.h>
#include <types/ndarray.h>

#include "sampling.h"

#define MODULE_NAME "app-meas-generic-sampling"


/**
 * @brief Publish a single float value on the message queue
 */
static void publish_value(Sampling *self, float value, const char *topic) {
	struct timespec ts = {0};
	NdArray array;
	ndarray_init_view(&array, DTYPE_FLOAT, 1, &value, sizeof(value));
	self->mqc->vmt->publish(self->mqc, topic, &array, &ts);
}


/**
 * @brief Convert a raw measurement channel value to the configured ADC measure unit
 *
 * With the raw-counts unit the value is returned unchanged; with the ratiometric divider unit it
 * is converted to the measured resistance.
 */
static float compute_sensor(Sampling *self, float value) {
	(void)self;

#if defined(CONFIG_APP_MEAS_GENERIC_MEASURE_UNIT_RESISTANCE)
	/* The sensor returns the raw ADC counts of the divider voltage ratiometric to the excitation (the
	 * measured and the reference resistor are in series, the measured voltage referenced to the
	 * excitation applied across the pair). Normalise the counts to a 0..1 ratio using the ADC full
	 * scale, then convert that ratio to the measured resistance using the reference resistor,
	 * depending on which leg of the divider the reference occupies. A differential input spends one
	 * bit on sign, halving the positive full scale. */
#if defined(CONFIG_APP_MEAS_GENERIC_ADC_DIFFERENTIAL)
	float ratio = value / (float)(1UL << (CONFIG_APP_MEAS_GENERIC_ADC_BITS - 1));
#else
	float ratio = value / (float)(1UL << CONFIG_APP_MEAS_GENERIC_ADC_BITS);
#endif
	float rref = (float)CONFIG_APP_MEAS_GENERIC_REFERENCE_RESISTOR;
#if defined(CONFIG_APP_MEAS_GENERIC_REFERENCE_ON_TOP)
	/* Reference on top, measured resistor to ground: ratio = Rmeas / (Rref + Rmeas). */
	value = rref * ratio / (1.0f - ratio);
#else
	/* Reference on bottom, measured resistor toward the excitation: ratio = Rref / (Rref + Rmeas). */
	value = rref * (1.0f - ratio) / ratio;
#endif
#endif

	return value;
}


static void sampling_task(void *p) {
	Sampling *self = p;

	while (true) {
		/* Sample every discovered channel and, if present, the compensation temperature, then
		 * publish the values on their input topics. The compensation service republishes the
		 * compensated results on the corresponding output topics. */
		for (size_t i = 0; i < self->channel_count; i++) {
			float f = 0.0f;
			if (self->channels[i].sensor->vmt->value_f(self->channels[i].sensor, &f) == SENSOR_RET_OK) {
				publish_value(self, compute_sensor(self, f), self->channels[i].input_topic);
			}
		}
		if (self->temp != NULL) {
			/* The temperature is already in its physical unit; the measure-unit conversion only
			 * applies to the resistive measurement channels. */
			float f = 0.0f;
			if (self->temp->vmt->value_f(self->temp, &f) == SENSOR_RET_OK) {
				publish_value(self, f, self->temp_topic);
			}
		}

		vTaskDelay(pdMS_TO_TICKS(self->sample_interval_ms));
	}
	vTaskDelete(NULL);
}


/* Discover the sensors named in the comma-separated Kconfig list and register a compensation
 * channel for each one present. A raw value is published on the channel input topic (the sensor
 * name) and the compensated result on <name>/comp. Only sensors advertised by the port are used;
 * unknown names are skipped. */
static sampling_ret_t discover_channels(Sampling *self) {
	char list[SAMPLING_MAX_SENSORS * (SAMPLING_NAME_LEN + 1)] = {0};
	snprintf(list, sizeof(list), "%s", CONFIG_APP_MEAS_GENERIC_SENSORS);

	char *saveptr = NULL;
	for (char *name = strtok_r(list, ",", &saveptr); name != NULL; name = strtok_r(NULL, ",", &saveptr)) {
		/* Skip leading spaces so a list like "ch0, ch1" is accepted. */
		while (*name == ' ') {
			name++;
		}
		if (*name == '\0') {
			continue;
		}
		if (self->channel_count >= SAMPLING_MAX_SENSORS) {
			u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("sensor list truncated at %d entries"), SAMPLING_MAX_SENSORS);
			break;
		}

		Sensor *sensor = NULL;
		if (iservicelocator_query_name_type(locator, name, ISERVICELOCATOR_TYPE_SENSOR, (Interface **)&sensor) != ISERVICELOCATOR_RET_OK) {
			u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("sensor '%s' not found, skipping"), name);
			continue;
		}

		struct sampling_channel *ch = &self->channels[self->channel_count];
		snprintf(ch->name, sizeof(ch->name), "%s", name);
		snprintf(ch->input_topic, sizeof(ch->input_topic), "adc/%s", name);
		ch->sensor = sensor;

		/* Identity compensation (zero offset, unity gain, no nonlinearity or temperature
		 * correction). The coefficients are exposed in the configuration tree under the channel
		 * name and can be adjusted at runtime. */
		struct mq_compensation_channel_conf conf = {
			.x_ref = 0.0f,
			.c = {0.0f, 1.0f},
			.t_ref = MQ_COMPENSATION_DEFAULT_TEMP_C,
			.tc1 = 0.0f,
			.tc2 = 0.0f,
		};
		snprintf(conf.name, sizeof(conf.name), "%s", ch->name);
		snprintf(conf.input_topic, sizeof(conf.input_topic), "%s", ch->input_topic);
		snprintf(conf.output_topic, sizeof(conf.output_topic), "out/%s", ch->name);
		mq_compensation_add_channel(&self->comp, &conf);

		self->channel_count++;
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("discovered channel '%s'"), ch->name);
	}

	if (self->channel_count == 0) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("no measurement sensors found"));
		return SAMPLING_RET_FAILED;
	}
	return SAMPLING_RET_OK;
}


sampling_ret_t sampling_init(Sampling *self) {
	memset(self, 0, sizeof(Sampling));

	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_MQ, 0, (Interface **)&self->mq) != ISERVICELOCATOR_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("no message queue found"));
		return SAMPLING_RET_FAILED;
	}
	self->sample_interval_ms = CONFIG_APP_MEAS_GENERIC_SAMPLE_INTERVAL_MS;

	self->mqc = self->mq->vmt->open(self->mq);
	if (self->mqc == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot open message queue client"));
		return SAMPLING_RET_FAILED;
	}

	/* Discover the measurement sensors and register their compensation channels. */
	mq_compensation_init(&self->comp, self->mq);
	if (discover_channels(self) != SAMPLING_RET_OK) {
		return SAMPLING_RET_FAILED;
	}

	/* Optional board temperature feeding the compensation polynomial. */
	snprintf(self->temp_topic, sizeof(self->temp_topic), "%s", CONFIG_APP_MEAS_GENERIC_TEMP_SENSOR);
	if (self->temp_topic[0] != '\0') {
		if (iservicelocator_query_name_type(locator, self->temp_topic, ISERVICELOCATOR_TYPE_SENSOR, (Interface **)&self->temp) == ISERVICELOCATOR_RET_OK) {
			mq_compensation_set_temp_topic(&self->comp, self->temp_topic);
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("temperature compensation on '%s'"), self->temp_topic);
		} else {
			self->temp = NULL;
		}
	}

	mq_compensation_start(&self->comp, 1);

	xTaskCreate(sampling_task, "meas-app", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &(self->task));
	if (self->task == NULL) {
		return SAMPLING_RET_FAILED;
	}

	return SAMPLING_RET_OK;
}


sampling_ret_t sampling_free(Sampling *self) {
	(void)self;
	return SAMPLING_RET_OK;
}
