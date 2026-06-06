/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * MQ polynomial offset/gain/nonlinearity and temperature compensation service
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "config.h"
#include "FreeRTOS.h"
#include "task.h"

#include <types/ndarray.h>
#include <interfaces/mq.h>
#include <interfaces/conf.h>
#include <configlib.h>


#define MQ_COMPENSATION_MAX_TOPIC_LEN CONFIG_SERVICE_MQ_COMPENSATION_MAX_TOPIC_LEN
#define MQ_COMPENSATION_MAX_ORDER CONFIG_SERVICE_MQ_COMPENSATION_MAX_ORDER
#define MQ_COMPENSATION_RXBUF_SAMPLES CONFIG_SERVICE_MQ_COMPENSATION_RXBUF_SAMPLES

/* Temperature substituted into the compensation polynomial until a value is received on the
 * temperature topic. Chosen to match the reference point used during calibration. */
#define MQ_COMPENSATION_DEFAULT_TEMP_C (25.0f)

typedef enum {
	MQ_COMPENSATION_RET_OK = 0,
	MQ_COMPENSATION_RET_FAILED,
} mq_compensation_ret_t;


/**
 * Set of calibration coefficients applied to a single channel.
 *
 * The measured value @p x is first centered around @p x_ref and corrected by the polynomial
 *
 *     y = c[0] + c[1]*(x - x_ref) + c[2]*(x - x_ref)^2 + ... + c[N]*(x - x_ref)^N
 *
 * where c[0] absorbs the offset error, c[1] the gain error and the higher order terms the
 * nonlinearity. The result is then divided by a quadratic temperature factor
 *
 *     y /= 1 + tc1*(T - t_ref) + tc2*(T - t_ref)^2
 *
 * normalised to 1.0 at @p t_ref so tc1/tc2 express the relative deviation per degree and per
 * degree squared. Centering both polynomials around a reference keeps the float coefficients
 * well conditioned around the operating point.
 */
struct mq_compensation_coefs {
	/* Value polynomial reference point and coefficients. */
	float x_ref;
	float c[MQ_COMPENSATION_MAX_ORDER + 1];

	/* Quadratic temperature compensation reference and coefficients. */
	float t_ref;
	float tc1;
	float tc2;
};


struct mq_compensation_channel {
	char input_topic[MQ_COMPENSATION_MAX_TOPIC_LEN];
	char output_topic[MQ_COMPENSATION_MAX_TOPIC_LEN];

	struct mq_compensation_coefs coefs;

	/* Configuration subtree exposing the coefficients of this channel. */
	ConfiglibValue channel_conf;
	ConfiglibValue x_ref_conf;
	ConfiglibValue c_conf[MQ_COMPENSATION_MAX_ORDER + 1];
	ConfiglibValue t_ref_conf;
	ConfiglibValue tc1_conf;
	ConfiglibValue tc2_conf;

	/* Channels are arranged as a linked list. */
	struct mq_compensation_channel *next;
};


typedef struct mq_compensation {
	/* Dependency injected in init(). */
	Mq *mq;

	/* A MQ client instance is created in start(). */
	MqClient *mqc;

	/* Buffer used to receive values from the message queue. Holds up to
	 * MQ_COMPENSATION_RXBUF_SAMPLES float elements that are compensated in place. */
	NdArray rxbuf;

	/* Latest temperature received on the temperature topic. An empty topic disables the
	 * temperature subscription and keeps the default temperature. */
	char temp_topic[MQ_COMPENSATION_MAX_TOPIC_LEN];
	float temp_c;

	/* Start of the channel linked list. */
	struct mq_compensation_channel *first_channel;

	/* Root of the configuration tree exposing all channels. */
	ConfiglibValue root_conf;

	volatile bool can_run;
	volatile bool running;
	TaskHandle_t task;
} MqCompensation;


/**
 * @brief Initialize the MqCompensation service
 *
 * @param self The instance to initialize. Must be allocated beforehand.
 * @param mq A valid message queue interface dependency.
 *
 * @return MQ_COMPENSATION_RET_FAILED on error or MQ_COMPENSATION_RET_OK otherwise.
 */
mq_compensation_ret_t mq_compensation_init(MqCompensation *self, Mq *mq);


mq_compensation_ret_t mq_compensation_free(MqCompensation *self);


/**
 * @brief Add a compensation channel
 *
 * Values received on @p input_topic are compensated using @p coefs and published on
 * @p output_topic. A configuration subtree named after @p output_topic exposing the
 * coefficients is appended to the service configuration tree.
 *
 * @param self The service instance.
 * @param input_topic Topic to receive raw values from.
 * @param output_topic Topic to publish compensated values to. Also used as the name of the
 *                     channel configuration subtree.
 * @param coefs Initial set of calibration coefficients. The structure is copied.
 *
 * @return MQ_COMPENSATION_RET_FAILED on error or MQ_COMPENSATION_RET_OK otherwise.
 */
mq_compensation_ret_t mq_compensation_add_channel(MqCompensation *self, const char *input_topic, const char *output_topic, const struct mq_compensation_coefs *coefs);


/**
 * @brief Set the topic carrying the temperature used for compensation
 *
 * The latest value received on @p topic is substituted as the temperature into every channel's
 * temperature compensation polynomial. Until a value is received, @p MQ_COMPENSATION_DEFAULT_TEMP_C
 * is used.
 */
mq_compensation_ret_t mq_compensation_set_temp_topic(MqCompensation *self, const char *topic);


/**
 * @brief Get the configuration tree root of the service
 *
 * The returned node can be attached to a parent configuration tree or handed to a service
 * exporting the tree (eg. proto-conf).
 *
 * @param conf Returns the root Conf node.
 */
mq_compensation_ret_t mq_compensation_get_conf(MqCompensation *self, Conf **conf);


mq_compensation_ret_t mq_compensation_start(MqCompensation *self, uint32_t prio);


mq_compensation_ret_t mq_compensation_stop(MqCompensation *self);
