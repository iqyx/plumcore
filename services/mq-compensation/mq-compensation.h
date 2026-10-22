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
#include <interfaces/flash.h>
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
 * Configuration for a single compensation channel, including routing topics, a human-readable
 * name, and the calibration coefficients.
 *
 * If @p reciprocal is set, the measured value is first replaced by its reciprocal (x = 1/x), eg. to
 * convert a resistance to a conductance. A zero input yields FLT_MAX (saturated). The value @p x is
 * then centered around @p x_ref and corrected by the polynomial
 *
 *     y = c[0] + c[1]*(x - x_ref) + c[2]*(x - x_ref)^2 + ... + c[N]*(x - x_ref)^N
 *
 * where c[0] absorbs the offset error, c[1] the gain error and the higher order terms the
 * nonlinearity. The result is raised to the power of @p exp (sign-preserving, sign(y)*|y|^exp, so a
 * negative polynomial output does not produce NaN). Use exp = 1.0 to disable it. Together with the
 * reciprocal this models hyperbolic/power-law curves, eg. a force sensing resistor:
 *
 *     F = (k/R + c)^d    (reciprocal = true, x_ref = 0, c[0] = c, c[1] = k, exp = d)
 *
 * The result is then divided by a quadratic temperature factor
 *
 *     y /= 1 + tc1*(T - t_ref) + tc2*(T - t_ref)^2
 *
 * normalised to 1.0 at @p t_ref so tc1/tc2 express the relative deviation per degree and per
 * degree squared. Centering both polynomials around a reference keeps the float coefficients
 * well conditioned around the operating point.
 */
struct mq_compensation_channel_conf {
	char name[MQ_COMPENSATION_MAX_TOPIC_LEN];
	char input_topic[MQ_COMPENSATION_MAX_TOPIC_LEN];
	char output_topic[MQ_COMPENSATION_MAX_TOPIC_LEN];

	/* Replace the input value by its reciprocal before the polynomial. */
	bool reciprocal;

	/* Value polynomial reference point and coefficients. */
	float x_ref;
	float c[MQ_COMPENSATION_MAX_ORDER + 1];

	/* Exponent applied to the polynomial output, 1.0 to disable. */
	float exp;

	/* Quadratic temperature compensation reference and coefficients. */
	float t_ref;
	float tc1;
	float tc2;
};


/**
 * A key-value pair of a configuration of the calibration certificate (an entry of its top-level "cfg" map), eg.
 * {"pga", "1"}. The value is given as text, it is compared with a text, a number or a bool ("true"/"false") of the
 * certificate. A floating point number is compared in single precision.
 */
struct mq_compensation_cert_kv {
	const char *key;
	const char *value;
};


/**
 * A compensation channel loaded from the calibration certificate.
 */
struct mq_compensation_cert_channel {
	/* Name of a channel added with mq_compensation_add_channel() beforehand. */
	const char *name;

	/* NULL-terminated hierarchical path of the certificate targets from the root target to the one the compensation
	 * model is loaded from, eg. {"dev", "adc0", "ch0", NULL}. */
	const char *const *path;
};


/**
 * Parameters of loading the calibration certificate, see mq_compensation_load_certificate().
 */
struct mq_compensation_cert_conf {
	/* Flash partition the certificate is loaded from. It starts at the beginning of the partition. */
	Flash *flash;

	/* NULL-terminated array of base64 encoded ed25519 public keys used to verify the signatures of the certificate.
	 * NULL if no keys are provided. */
	const char *const *public_keys;

	/* Force the verification. Loading fails if the certificate signatures cannot be verified. */
	bool verify;

	/* Key-value pairs of the device configuration terminated by a pair with a NULL key. The first configuration
	 * of the certificate having all these pairs is used, its other pairs are ignored. NULL to use only results with
	 * no configuration. */
	const struct mq_compensation_cert_kv *cfg;

	/* Channels to load, terminated by a channel with a NULL name. */
	const struct mq_compensation_cert_channel *channels;
};


struct mq_compensation_channel {
	struct mq_compensation_channel_conf conf;

	/* Configuration subtree exposing the coefficients of this channel. */
	ConfiglibValue channel_conf;
	ConfiglibValue reciprocal_conf;
	ConfiglibValue x_ref_conf;
	ConfiglibValue c_conf[MQ_COMPENSATION_MAX_ORDER + 1];
	ConfiglibValue exp_conf;
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
 * Values received on @p conf->input_topic are compensated using the coefficients in @p conf and
 * published on @p conf->output_topic. A configuration subtree named after @p conf->name exposing
 * the coefficients is appended to the service configuration tree.
 *
 * @param self The service instance.
 * @param conf Channel configuration (topics, name, coefficients). The structure is copied.
 *
 * @return MQ_COMPENSATION_RET_FAILED on error or MQ_COMPENSATION_RET_OK otherwise.
 */
mq_compensation_ret_t mq_compensation_add_channel(MqCompensation *self, const struct mq_compensation_channel_conf *conf);


/**
 * @brief Set the topic carrying the temperature used for compensation
 *
 * The latest value received on @p topic is substituted as the temperature into every channel's
 * temperature compensation polynomial. Until a value is received, @p MQ_COMPENSATION_DEFAULT_TEMP_C
 * is used.
 */
mq_compensation_ret_t mq_compensation_set_temp_topic(MqCompensation *self, const char *topic);


/**
 * @brief Load the compensation coefficients from an EP-7d3 calibration certificate
 *
 * The configuration of the certificate matching @p conf->cfg is found first. Then for each channel of
 * @p conf->channels, the hierarchy of its target path is checked and the compensation model ("comp") of the first
 * result group of the last target of the path, taken under the matching configuration (or under no configuration),
 * is loaded into the channel coefficients. Absent steps of the model are loaded as identity.
 *
 * The parameters are expected to be passed as a compound literal:
 *
 *     mq_compensation_load_certificate(&comp, &(const struct mq_compensation_cert_conf) {
 *         .flash = calib_flash,
 *         .cfg = (const struct mq_compensation_cert_kv[]) {
 *             {"pga", "1"},
 *             {NULL, NULL},
 *         },
 *         .channels = (const struct mq_compensation_cert_channel[]) {
 *             {"inc_x", (const char *[]) {"dev", "adc0", "ch0", NULL}},
 *             {"inc_y", (const char *[]) {"dev", "adc0", "ch1", NULL}},
 *             {NULL, NULL},
 *         },
 *     });
 *
 * @param self The service instance.
 * @param conf Loading parameters.
 *
 * @return MQ_COMPENSATION_RET_FAILED if the certificate cannot be loaded or verified, or if any of the channels
 *         cannot be loaded (the other channels are loaded), MQ_COMPENSATION_RET_OK otherwise.
 */
mq_compensation_ret_t mq_compensation_load_certificate(MqCompensation *self, const struct mq_compensation_cert_conf *conf);


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
