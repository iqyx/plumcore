/* SPDX-License-Identifier: BSD-2-Clause
 *
 * WORN log (write-once, read-never) implementation using
 * a block device backend storage.
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <interfaces/block.h>
#include <interfaces/worn-log.h>

#define WORN_SIV_LEN 16
#define WORN_KEY_LEN 16

typedef enum {
	WORN_LOG_BLOCK_RET_OK = 0,
	WORN_LOG_BLOCK_RET_FAILED = -1,
	WORN_LOG_BLOCK_RET_NO_DATA = -2,
	WORN_LOG_BLOCK_RET_BAD_ARG = -3,
} worn_log_block_ret_t;


typedef struct {
	Block *storage;
	size_t block_size;
	size_t size;

	uint8_t access_ke[WORN_KEY_LEN];
	uint8_t access_km[WORN_KEY_LEN];

	size_t total_data_size;
	size_t next_data_block;
	size_t total_data_blocks;

	Worn worn;
} WornLogBlock;


worn_ret_t worn_log_block_prepare(WornLogBlock *self);
worn_ret_t worn_log_block_write(WornLogBlock *self, const uint8_t *buf, size_t len);
worn_ret_t worn_log_block_commit(WornLogBlock *self);
worn_ret_t worn_log_block_get_info(WornLogBlock *self, uint64_t *size_total, uint64_t *size_free);

worn_log_block_ret_t worn_log_block_find_next(WornLogBlock *self, size_t *block);
worn_log_block_ret_t worn_log_block_append(WornLogBlock *self);

worn_log_block_ret_t worn_log_block_init(WornLogBlock *self, Block *storage);
worn_log_block_ret_t worn_log_block_free(WornLogBlock *self);
