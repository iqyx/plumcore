/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * EP-7d3 calibration certificate parser of the mq-compensation service
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <cbor.h>
#include <interfaces/flash.h>

#include "mq-compensation.h"

/* Size of the buffers holding text strings read from the certificate (keys, identifiers), including the terminator. */
#define MQ_COMPENSATION_CERT_STR_LEN 32


/**
 * A certificate stored in a flash partition, parsed directly from the flash. The tinycbor reader keeps a single
 * shared cursor, therefore every lookup parses the certificate again from its beginning and traverses it linearly.
 */
struct mq_compensation_cert {
	Flash *flash;

	/* Size of the partition and the position of the reader cursor. */
	size_t len;
	size_t pos;

	/* Staging buffer of the tinycbor transfer_string callback. */
	uint8_t scratch[MQ_COMPENSATION_CERT_STR_LEN];

	CborParser parser;
	CborValue root;
};


/**
 * @brief Check the certificate in a flash partition
 *
 * Fails if the partition does not start with the magic header of a version 1 certificate.
 */
mq_compensation_ret_t mq_compensation_cert_init(struct mq_compensation_cert *self, Flash *flash);


/**
 * @brief Log the certificate information
 *
 * Logs the laboratory and customer names and the issue date of the administrative data, and the part number and
 * the serial number of each top-level target (a target with no parent). Long strings are truncated in the log.
 *
 * @return MQ_COMPENSATION_RET_FAILED if the administrative data or the targets cannot be read (eg. are absent).
 */
mq_compensation_ret_t mq_compensation_cert_log_info(struct mq_compensation_cert *self);


/**
 * @brief Find the first configuration of the certificate matching all key-value pairs
 *
 * A configuration (an entry of the top-level "cfg" map) matches if it has all the pairs in @p kv, in any order. Its
 * other pairs are ignored.
 *
 * @param kv Array of key-value pairs terminated by a pair with a NULL key.
 * @param id Returns the identifier of the matching configuration.
 * @param size Size of the @p id buffer.
 */
mq_compensation_ret_t mq_compensation_cert_find_cfg(struct mq_compensation_cert *self, const struct mq_compensation_cert_kv *kv, char *id, size_t size);


/**
 * @brief Check a hierarchical path of targets
 *
 * Each target of the @p path must exist in the top-level "tgt" map and have the preceding one as its parent ("p"),
 * the first target must have no parent.
 *
 * @param path NULL-terminated array of target identifiers.
 * @param tgt Returns the last target of the path.
 */
mq_compensation_ret_t mq_compensation_cert_check_path(struct mq_compensation_cert *self, const char *const *path, const char **tgt);


/**
 * @brief Find the compensation model of a target
 *
 * The model is taken from the first result group with a "comp" model of the target @p tgt, taken under the
 * configuration @p cfg. A result group with no configuration applies to any. Only the coefficients of @p comp are
 * set, absent steps of the model are set to identity.
 *
 * @param cfg Identifier of the configuration, NULL to accept only result groups with no configuration.
 */
mq_compensation_ret_t mq_compensation_cert_find_comp(struct mq_compensation_cert *self, const char *tgt, const char *cfg, struct mq_compensation_channel_conf *comp);
