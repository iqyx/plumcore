/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * MQ polynomial offset/gain/nonlinearity and temperature compensation service
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "u_log.h"
#include "u_assert.h"

#include <types/ndarray.h>
#include <interfaces/mq.h>
#include <interfaces/conf.h>
#include <configlib.h>

#include "mq-compensation.h"

#define MODULE_NAME "mq-compensation"

/* Names of the polynomial coefficient configuration nodes. configlib does not copy the name
 * string so it must have static storage duration. */
static const char *mq_compensation_coef_names[] = {
	"c0", "c1", "c2", "c3", "c4", "c5", "c6", "c7",
};
#if (MQ_COMPENSATION_MAX_ORDER + 1) > 8
	#error "Increase the mq_compensation_coef_names table to cover MQ_COMPENSATION_MAX_ORDER"
#endif


/* Compute the compensated value for a single channel from the raw value @p x and the current
 * temperature @p temp_c. See the description of struct mq_compensation_coefs for the model. */
static float mq_compensation_apply(const struct mq_compensation_channel *ch, float x, float temp_c) {
	/* Center the input around the calibration reference point. */
	float xr = x - ch->coefs.x_ref;

	/* Evaluate the correction polynomial using Horner's method. */
	float y = 0.0f;
	for (int i = MQ_COMPENSATION_MAX_ORDER; i >= 0; i--) {
		y = y * xr + ch->coefs.c[i];
	}

	/* Apply the quadratic temperature compensation. */
	float tr = temp_c - ch->coefs.t_ref;
	float tc = 1.0f + tr * (ch->coefs.tc1 + tr * ch->coefs.tc2);
	if (tc != 0.0f) {
		y /= tc;
	}

	return y;
}


/* A received message is usable when it carries at least one element of a numeric dtype. The
 * character dtype is intentionally excluded as it does not represent a measured quantity. */
static bool mq_compensation_is_numeric_array(const NdArray *array) {
	if (array->asize < 1 || array->buf == NULL) {
		return false;
	}
	switch (array->dtype) {
		case DTYPE_BYTE:
		case DTYPE_INT8:
		case DTYPE_UINT8:
		case DTYPE_INT16:
		case DTYPE_UINT16:
		case DTYPE_INT32:
		case DTYPE_UINT32:
		case DTYPE_INT64:
		case DTYPE_UINT64:
		case DTYPE_FLOAT:
		case DTYPE_DOUBLE:
			return true;
		default:
			return false;
	}
}


/* Read element @p i of a numeric ndarray promoted to float. The compensation math always runs in
 * float regardless of the storage type because the coefficients are float. */
static float mq_compensation_get_value(const NdArray *array, size_t i) {
	switch (array->dtype) {
		case DTYPE_BYTE:
		case DTYPE_UINT8:
			return (float)((uint8_t *)array->buf)[i];
		case DTYPE_INT8:
			return (float)((int8_t *)array->buf)[i];
		case DTYPE_INT16:
			return (float)((int16_t *)array->buf)[i];
		case DTYPE_UINT16:
			return (float)((uint16_t *)array->buf)[i];
		case DTYPE_INT32:
			return (float)((int32_t *)array->buf)[i];
		case DTYPE_UINT32:
			return (float)((uint32_t *)array->buf)[i];
		case DTYPE_INT64:
			return (float)((int64_t *)array->buf)[i];
		case DTYPE_UINT64:
			return (float)((uint64_t *)array->buf)[i];
		case DTYPE_DOUBLE:
			return (float)((double *)array->buf)[i];
		case DTYPE_FLOAT:
		default:
			return ((float *)array->buf)[i];
	}
}


/* Round and clamp @p value to the inclusive range [lo, hi] before a narrowing integer store. */
static float mq_compensation_clamp(float value, float lo, float hi) {
	value = roundf(value);
	if (value < lo) {
		value = lo;
	}
	if (value > hi) {
		value = hi;
	}
	return value;
}


/* Store @p value into element @p i of a numeric ndarray, converting back to its storage type.
 * Integer types are rounded to the nearest representable value and clamped to their range. */
static void mq_compensation_set_value(NdArray *array, size_t i, float value) {
	switch (array->dtype) {
		case DTYPE_BYTE:
		case DTYPE_UINT8:
			((uint8_t *)array->buf)[i] = (uint8_t)mq_compensation_clamp(value, 0.0f, (float)UINT8_MAX);
			break;
		case DTYPE_INT8:
			((int8_t *)array->buf)[i] = (int8_t)mq_compensation_clamp(value, (float)INT8_MIN, (float)INT8_MAX);
			break;
		case DTYPE_INT16:
			((int16_t *)array->buf)[i] = (int16_t)mq_compensation_clamp(value, (float)INT16_MIN, (float)INT16_MAX);
			break;
		case DTYPE_UINT16:
			((uint16_t *)array->buf)[i] = (uint16_t)mq_compensation_clamp(value, 0.0f, (float)UINT16_MAX);
			break;
		case DTYPE_INT32:
			((int32_t *)array->buf)[i] = (int32_t)mq_compensation_clamp(value, (float)INT32_MIN, (float)INT32_MAX);
			break;
		case DTYPE_UINT32:
			((uint32_t *)array->buf)[i] = (uint32_t)mq_compensation_clamp(value, 0.0f, (float)UINT32_MAX);
			break;
		case DTYPE_INT64:
			((int64_t *)array->buf)[i] = (int64_t)mq_compensation_clamp(value, (float)INT64_MIN, (float)INT64_MAX);
			break;
		case DTYPE_UINT64:
			((uint64_t *)array->buf)[i] = (uint64_t)mq_compensation_clamp(value, 0.0f, (float)UINT64_MAX);
			break;
		case DTYPE_DOUBLE:
			((double *)array->buf)[i] = (double)value;
			break;
		case DTYPE_FLOAT:
		default:
			((float *)array->buf)[i] = value;
			break;
	}
}


/* A single thread receives raw values and the temperature from the message queue, compensates
 * each value and republishes it. The temperature is cached and reused for all channels. */
static void mq_compensation_task(void *p) {
	MqCompensation *self = (MqCompensation *)p;

	self->can_run = true;
	self->running = true;
	while (self->can_run) {
		struct timespec ts = {0};
		char topic[MQ_COMPENSATION_MAX_TOPIC_LEN] = {0};
		if (self->mqc->vmt->receive(self->mqc, topic, MQ_COMPENSATION_MAX_TOPIC_LEN, &self->rxbuf, &ts) != MQ_RET_OK) {
			continue;
		}

		if (!mq_compensation_is_numeric_array(&self->rxbuf)) {
			continue;
		}

		/* A message on the temperature topic only updates the cached temperature. For an array
		 * the first element is taken as the representative temperature. */
		if (self->temp_topic[0] != '\0' && !strcmp(topic, self->temp_topic)) {
			self->temp_c = mq_compensation_get_value(&self->rxbuf, 0);
			continue;
		}

		/* Otherwise find the channel the message belongs to and compensate every element of the
		 * array in place, then republish it with its original dtype and shape preserved. */
		struct mq_compensation_channel *ch = self->first_channel;
		while (ch != NULL) {
			if (!strcmp(topic, ch->input_topic)) {
				for (size_t i = 0; i < self->rxbuf.asize; i++) {
					float out = mq_compensation_apply(ch, mq_compensation_get_value(&self->rxbuf, i), self->temp_c);
					mq_compensation_set_value(&self->rxbuf, i, out);
				}
				self->mqc->vmt->publish(self->mqc, ch->output_topic, &self->rxbuf, &ts);
				break;
			}
			ch = ch->next;
		}
	}
	self->running = false;
	vTaskDelete(NULL);
}


mq_compensation_ret_t mq_compensation_init(MqCompensation *self, Mq *mq) {
	if (u_assert(self != NULL) ||
	    u_assert(mq != NULL)) {
		return MQ_COMPENSATION_RET_FAILED;
	}
	memset(self, 0, sizeof(MqCompensation));
	self->mq = mq;
	self->temp_c = MQ_COMPENSATION_DEFAULT_TEMP_C;

	configlib_init(&self->root_conf, "compensation");

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialized"));
	return MQ_COMPENSATION_RET_OK;
}


mq_compensation_ret_t mq_compensation_free(MqCompensation *self) {
	if (u_assert(self != NULL)) {
		return MQ_COMPENSATION_RET_FAILED;
	}

	struct mq_compensation_channel *ch = self->first_channel;
	while (ch != NULL) {
		struct mq_compensation_channel *next = ch->next;
		free(ch);
		ch = next;
	}
	self->first_channel = NULL;

	return MQ_COMPENSATION_RET_OK;
}


mq_compensation_ret_t mq_compensation_add_channel(MqCompensation *self, const char *input_topic, const char *output_topic, const struct mq_compensation_coefs *coefs) {
	if (u_assert(self != NULL) ||
	    u_assert(input_topic != NULL) ||
	    u_assert(output_topic != NULL) ||
	    u_assert(coefs != NULL)) {
		return MQ_COMPENSATION_RET_FAILED;
	}

	struct mq_compensation_channel *ch = malloc(sizeof(struct mq_compensation_channel));
	if (ch == NULL) {
		goto err;
	}
	memset(ch, 0, sizeof(struct mq_compensation_channel));
	strlcpy(ch->input_topic, input_topic, MQ_COMPENSATION_MAX_TOPIC_LEN);
	strlcpy(ch->output_topic, output_topic, MQ_COMPENSATION_MAX_TOPIC_LEN);
	memcpy(&ch->coefs, coefs, sizeof(ch->coefs));

	/* Build the configuration subtree for this channel. The subtree is named after the output
	 * topic and holds the reference points and polynomial coefficients. */
	configlib_init_map_append(&ch->channel_conf, ch->output_topic, NULL, CONF_SUBTREE, &self->root_conf, CONF_DIR_CHILD);
	configlib_init_map_append(&ch->x_ref_conf, "x_ref", &ch->coefs.x_ref, CONF_F, &ch->channel_conf, CONF_DIR_CHILD);
	for (size_t i = 0; i <= MQ_COMPENSATION_MAX_ORDER; i++) {
		configlib_init_map_append(&ch->c_conf[i], mq_compensation_coef_names[i], &ch->coefs.c[i], CONF_F, &ch->channel_conf, CONF_DIR_CHILD);
	}
	configlib_init_map_append(&ch->t_ref_conf, "t_ref", &ch->coefs.t_ref, CONF_F, &ch->channel_conf, CONF_DIR_CHILD);
	configlib_init_map_append(&ch->tc1_conf, "tc1", &ch->coefs.tc1, CONF_F, &ch->channel_conf, CONF_DIR_CHILD);
	configlib_init_map_append(&ch->tc2_conf, "tc2", &ch->coefs.tc2, CONF_F, &ch->channel_conf, CONF_DIR_CHILD);

	/* And append it to the linked list. */
	ch->next = self->first_channel;
	self->first_channel = ch;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("added channel '%s' -> '%s'"), input_topic, output_topic);
	return MQ_COMPENSATION_RET_OK;
err:
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("error adding channel '%s' -> '%s'"), input_topic, output_topic);
	return MQ_COMPENSATION_RET_FAILED;
}


mq_compensation_ret_t mq_compensation_set_temp_topic(MqCompensation *self, const char *topic) {
	if (u_assert(self != NULL) ||
	    u_assert(topic != NULL)) {
		return MQ_COMPENSATION_RET_FAILED;
	}

	strlcpy(self->temp_topic, topic, MQ_COMPENSATION_MAX_TOPIC_LEN);
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("temperature topic set to '%s'"), topic);

	return MQ_COMPENSATION_RET_OK;
}


mq_compensation_ret_t mq_compensation_get_conf(MqCompensation *self, Conf **conf) {
	if (u_assert(self != NULL) ||
	    u_assert(conf != NULL)) {
		return MQ_COMPENSATION_RET_FAILED;
	}

	*conf = &self->root_conf.conf;
	return MQ_COMPENSATION_RET_OK;
}


mq_compensation_ret_t mq_compensation_start(MqCompensation *self, uint32_t prio) {
	if (u_assert(self != NULL)) {
		return MQ_COMPENSATION_RET_FAILED;
	}

	/* Buffer used to receive values from the message queue (compensated in place). */
	if (ndarray_init_empty(&self->rxbuf, DTYPE_FLOAT, MQ_COMPENSATION_RXBUF_SAMPLES) != NDARRAY_RET_OK) {
		goto err;
	}

	/* Create a new MQ client instance used to receive and publish messages. */
	self->mqc = self->mq->vmt->open(self->mq);
	if (self->mqc == NULL) {
		goto err;
	}

	/* Subscribe to all channel input topics and the temperature topic. */
	struct mq_compensation_channel *ch = self->first_channel;
	while (ch != NULL) {
		self->mqc->vmt->subscribe(self->mqc, ch->input_topic);
		ch = ch->next;
	}

	if (self->temp_topic[0] != '\0') {
		self->mqc->vmt->subscribe(self->mqc, self->temp_topic);
	}

	xTaskCreate(mq_compensation_task, "mq-comp", configMINIMAL_STACK_SIZE + 128, (void *)self, prio, &(self->task));
	if (self->task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		goto err;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("started"));
	return MQ_COMPENSATION_RET_OK;
err:
	mq_compensation_stop(self);
	return MQ_COMPENSATION_RET_FAILED;
}


mq_compensation_ret_t mq_compensation_stop(MqCompensation *self) {
	if (u_assert(self != NULL)) {
		return MQ_COMPENSATION_RET_FAILED;
	}

	/* Stop the thread now. */
	/** @todo timeout */
	self->can_run = false;
	while (self->running) {
		vTaskDelay(100);
	}

	if (self->mqc != NULL) {
		self->mqc->vmt->close(self->mqc);
		self->mqc = NULL;
	}

	ndarray_free(&self->rxbuf);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("stopped"));
	return MQ_COMPENSATION_RET_OK;
}
