/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Load/save the discovered configuration tree as CBOR
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <main.h>
#include <interfaces/flash.h>
#include <interfaces/conf.h>
#include <interfaces/job.h>

typedef enum {
	CONF_CBOR_RET_OK = 0,
	CONF_CBOR_RET_FAILED,
	CONF_CBOR_RET_NULL,
	CONF_CBOR_RET_NOMEM,
	CONF_CBOR_RET_IO,
	CONF_CBOR_RET_MALFORMED,
} conf_cbor_ret_t;

typedef struct conf_cbor ConfCbor;

/* Storage access handlers, selected internally depending on the configured storage. */
struct conf_cbor_io;

/* A configuration load or save run as a job in a temporary task. */
struct conf_cbor_job {
	/* Job interface, its parent points to the ConfCbor instance. */
	Job job;
	volatile enum job_state state;
	enum job_result result;
	/* Return value of the last ended run, used to produce the error message. */
	conf_cbor_ret_t ret;
};

/* Service configuration passed to conf_cbor_init(). Not typedef'd per project policy. */
struct conf_cbor_conf {
	/** Flash partition the configuration is stored in. */
	Flash *flash;
	/** Offset of the document within the partition. Must be aligned to the erase block size. */
	size_t offset;
	/** Maximum document size in bytes. 0 = up to the end of the partition. Must be a multiple of the erase block
	 *  size as the whole window is erased before writing. */
	size_t max_size;
};

struct conf_cbor {
	struct conf_cbor_conf conf;

	/* Storage access handlers selected in init() depending on the configured storage. */
	const struct conf_cbor_io *io;

	/* Serializes load and save calls. Created in init(). */
	SemaphoreHandle_t lock;

	/* Flash handler write staging buffer, allocated only while writing. The first chunk of the document
	 * is held back and written last on commit, an interrupted save therefore leaves an erased (invalid) head. */
	uint8_t *wbuf;
	/* Number of bytes passed to the flash write handler. */
	size_t wbuf_pos;

	/* conf-save and conf-load jobs, each running conf_cbor_save() or conf_cbor_load() in a temporary task.
	 * They cannot be cancelled or suspended and are serialized by the lock above. */
	struct {
		struct conf_cbor_job save;
		struct conf_cbor_job load;
	} jobs;
};


conf_cbor_ret_t conf_cbor_init(ConfCbor *self, const struct conf_cbor_conf *conf);
conf_cbor_ret_t conf_cbor_free(ConfCbor *self);

/**
 * @brief Save the configuration tree
 *
 * All Conf subtrees advertised in the service locator are walked and every readable and writable value
 * (not constant, not status) is saved. Each subtree is placed at the position given by its space-delimited
 * service locator name, nested inside maps of its parent components. The document is a map of node names to
 * values or nested maps. Subtrees without any saveable value are omitted.
 */
conf_cbor_ret_t conf_cbor_save(ConfCbor *self);

/**
 * @brief Load the configuration tree
 *
 * Parse the stored CBOR document, find the matching Conf node for each value and write it. Values without a
 * matching node or with an incompatible type are skipped.
 */
conf_cbor_ret_t conf_cbor_load(ConfCbor *self);

