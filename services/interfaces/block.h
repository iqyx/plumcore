/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Block memory device interface descriptor
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>

#include "interface.h"

typedef enum {
	BLOCK_RET_OK = 0,
	BLOCK_RET_FAILED,
	BLOCK_RET_NULL,
	BLOCK_RET_OUT_OF_RANGE,
} block_ret_t;


typedef struct {
	block_ret_t (*get_block_size)(void *parent, size_t *block_size);
	block_ret_t (*set_block_size)(void *parent, size_t block_size);
	block_ret_t (*get_size)(void *parent, size_t *size);
	block_ret_t (*read)(void *parent, size_t block, uint8_t *buf, size_t len);
	block_ret_t (*write)(void *parent, size_t block, const uint8_t *buf, size_t len);
	block_ret_t (*flush)(void *parent);
} block_vmt_t;

typedef struct {
	block_vmt_t *vmt;
	void *parent;
} Block;


block_ret_t block_init(Block *self, block_vmt_t *vmt);
block_ret_t block_free(Block *self);


