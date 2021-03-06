/* SPDX-License-Identifier: BSD-2-Clause
 *
 * WORN log interface (write-once, read never)
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>

#include "interface.h"

typedef enum {
	WORN_RET_OK = 0,
	WORN_RET_FAILED,
	WORN_RET_FULL,
} worn_ret_t;


typedef struct {
	/* Lock the device and prepare a new writing session. */
	worn_ret_t (*prepare)(void *parent);
	
	/* Write user data. Function can be called multiple times. */
	worn_ret_t (*write)(void *parent, const uint8_t *buf, size_t len);

	/* Commit the written data, close the session. */
	worn_ret_t (*commit)(void *parent);
	
	worn_ret_t (*get_info)(void *parent, uint64_t *size_total, uint64_t *size_free);
} worn_vmt_t;

typedef struct {
	worn_vmt_t *vmt;
	void *parent;
} Worn;


worn_ret_t worn_init(Worn *self, worn_vmt_t *vmt);
worn_ret_t worn_free(Worn *self);


