/* SPDX-License-Identifier: BSD-2-Clause
 *
 * WORN log (write-once, read-never) implementation using
 * a block device backend storage.
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include "config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "u_log.h"
#include "u_assert.h"

#include <interfaces/block.h>
#include <interfaces/worn-log.h>
#include "worn-log-block.h"

#define MODULE_NAME "worn-log"
#define DEBUG 1


static worn_vmt_t worn_log_vmt = {
	.prepare = (typeof(worn_log_vmt.prepare))worn_log_block_prepare,
	.write = (typeof(worn_log_vmt.write))worn_log_block_write,
	.commit = (typeof(worn_log_vmt.commit))worn_log_block_commit,
	.get_info = (typeof(worn_log_vmt.get_info))worn_log_block_get_info,
};


worn_ret_t worn_log_block_prepare(WornLogBlock *self) {
	(void)self;
	return WORN_RET_OK;
}


worn_ret_t worn_log_block_write(WornLogBlock *self, const uint8_t *buf, size_t len) {
	(void)self;
	(void)buf;
	(void)len;
	return WORN_RET_OK;
}


worn_ret_t worn_log_block_commit(WornLogBlock *self) {
	(void)self;
	return WORN_RET_OK;
}


worn_ret_t worn_log_block_get_info(WornLogBlock *self, uint64_t *size_total, uint64_t *size_free) {
	(void)self;
	(void)size_total;
	(void)size_free;
	return WORN_RET_OK;
}

worn_log_block_ret_t worn_log_block_init(WornLogBlock *self, Block *storage) {
	memset(self, 0, sizeof(WornLogBlock));
	self->storage = storage;

	if (worn_init(&self->worn, &worn_log_vmt) != WORN_RET_OK) {
		return WORN_LOG_BLOCK_RET_FAILED;
	}
	self->worn.parent = self;
	return WORN_LOG_BLOCK_RET_OK;
}

worn_log_block_ret_t worn_log_block_free(WornLogBlock *self) {
	worn_free(&self->worn);
	return WORN_LOG_BLOCK_RET_OK;
}
