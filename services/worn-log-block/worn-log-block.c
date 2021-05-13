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
#include "lib/crypto/blake2.h"

#define MODULE_NAME "worn-log"
#define DEBUG 1
#define ASSERT(c, r) if (u_assert(c)) return r


static worn_vmt_t worn_log_vmt = {
	.prepare = (typeof(worn_log_vmt.prepare))worn_log_block_prepare,
	.write = (typeof(worn_log_vmt.write))worn_log_block_write,
	.commit = (typeof(worn_log_vmt.commit))worn_log_block_commit,
	.get_info = (typeof(worn_log_vmt.get_info))worn_log_block_get_info,
};


worn_ret_t worn_log_block_prepare(WornLogBlock *self) {
	(void)self;
	size_t block = 0;
	worn_log_block_append(self);
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





/* The encryption is reversible. This single function can be used bot for encryption and decryption. */
static worn_log_block_ret_t crypt_in_place(uint8_t *buf, size_t len, uint8_t siv[WORN_SIV_LEN], uint8_t key[WORN_KEY_LEN]) {
	ASSERT(buf != NULL, WORN_LOG_BLOCK_RET_BAD_ARG);
	ASSERT(len > 0, WORN_LOG_BLOCK_RET_BAD_ARG);
	ASSERT(siv != NULL, WORN_LOG_BLOCK_RET_BAD_ARG);
	ASSERT(key != NULL, WORN_LOG_BLOCK_RET_BAD_ARG);

	/* A single byte block counter. */
	uint8_t i = 0;
	while (len > 0) {
		/* Generate a BLAKE2S_OUTBYTES of keystream. */
		uint8_t keystream[BLAKE2S_OUTBYTES] = {0};
		blake2s_state s;
		blake2s_init_key(&s, BLAKE2S_OUTBYTES, key, WORN_KEY_LEN);
		blake2s_update(&s, siv, WORN_SIV_LEN);
		uint8_t b[4] = {0, 0, 0, i};
		blake2s_update(&s, b, sizeof(b));
		blake2s_final(&s, keystream, BLAKE2S_OUTBYTES);

		/* Crunch BLAKE2S_OUTBYTES or less in one step. */
		size_t block_len = len;
		if (block_len > BLAKE2S_OUTBYTES) {
			block_len = BLAKE2S_OUTBYTES;
		}
		for (size_t j = 0; j < block_len; j++) {
			buf[j] = buf[j] ^ keystream[j];
		}

		/* Advance to the next block. */
		buf += block_len;
		len -= block_len;
		i++;
	}
	/* Now len = 0 and buf is at the end. */

	return WORN_LOG_BLOCK_RET_OK;
}


static worn_log_block_ret_t block_decrypt(WornLogBlock *self, uint8_t *block, uint8_t **buf, size_t *len) {
	ASSERT(self != NULL, WORN_LOG_BLOCK_RET_BAD_ARG);
	ASSERT(buf != NULL, WORN_LOG_BLOCK_RET_BAD_ARG);

	/* There is a 16 byte SIV on the beginning. The rest is data to decrypt. */
	uint8_t *siv = block;
	uint8_t *data = block + WORN_SIV_LEN;
	size_t data_len = self->block_size - WORN_SIV_LEN;

	if (crypt_in_place(data, data_len, siv, self->access_ke) != WORN_LOG_BLOCK_RET_OK) {
		return WORN_LOG_BLOCK_RET_FAILED;
	}

	/* Now compute H() of the decrypted block and compare it to the SIV. */
	uint8_t mac_decrypted[WORN_SIV_LEN] = {0};
	blake2s(mac_decrypted, sizeof(mac_decrypted), data, data_len, self->access_km, WORN_KEY_LEN);

	/** @todo constant-time compare */
	if (memcmp(siv, mac_decrypted, WORN_SIV_LEN)) {
		/* Just to be sure nobody processes the fake data. */
		memset(data, 0, data_len);
		return WORN_LOG_BLOCK_RET_FAILED;
	}

	*buf = data;
	*len = data_len;
	return WORN_LOG_BLOCK_RET_OK;
}


static worn_log_block_ret_t block_encrypt(WornLogBlock *self, uint8_t *block, uint8_t *buf) {
	ASSERT(self != NULL, WORN_LOG_BLOCK_RET_BAD_ARG);
	ASSERT(block != NULL, WORN_LOG_BLOCK_RET_BAD_ARG);
	ASSERT(buf != NULL, WORN_LOG_BLOCK_RET_BAD_ARG);

	memcpy(block + WORN_SIV_LEN, buf, self->block_size - WORN_SIV_LEN);

	/* Put the SIV at the beginning of the block. */
	blake2s(block, WORN_SIV_LEN, buf, self->block_size - WORN_SIV_LEN, self->access_km, WORN_KEY_LEN);

	/* And finally encrypt the data in-place using the computed SIV. */
	if (crypt_in_place(block + WORN_SIV_LEN, self->block_size - WORN_SIV_LEN, block, self->access_ke) != WORN_LOG_BLOCK_RET_OK) {
		return WORN_LOG_BLOCK_RET_FAILED;
	}

	return WORN_LOG_BLOCK_RET_OK;
}


worn_log_block_ret_t worn_log_block_find_next(WornLogBlock *self, size_t *block) {
	size_t scope = 0;
	size_t scope_size = self->size;

	while (true) {
		size_t check_block = scope + scope_size / 2;
		u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("check %u, scope %u, scope_size %u"), check_block, scope, scope_size);

		uint8_t buf[self->block_size];
		uint8_t *data = NULL;
		size_t data_len = 0;

		if (self->storage->vmt->read(self->storage->parent, check_block, buf) != BLOCK_RET_OK) {
			return WORN_LOG_BLOCK_RET_FAILED;
		}
		if (block_decrypt(self, buf, &data, &data_len) == WORN_LOG_BLOCK_RET_OK) {
			scope_size = (scope - check_block) + scope_size - 1;
			scope = check_block + 1;
		} else {
			scope_size = check_block - scope;
		}

		if (scope_size == 0) {
			*block = scope;
			return WORN_LOG_BLOCK_RET_OK;	
		}
	}

	return WORN_LOG_BLOCK_RET_FAILED;
}


worn_log_block_ret_t worn_log_block_append(WornLogBlock *self) {
	size_t current_block = 0;
	worn_log_block_find_next(self, &current_block);
	
	u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("append to %u"), current_block);
	uint8_t buf[512] = {0};
	block_encrypt(self, buf, (uint8_t *)0x08000000);
	self->storage->vmt->write(self->storage->parent, current_block, buf);
}


worn_log_block_ret_t worn_log_block_init(WornLogBlock *self, Block *storage) {
	memset(self, 0, sizeof(WornLogBlock));
	self->storage = storage;

	/* Assuming the underlying storage is up. Check the block size
	 * and the storage size in blocks. */
	size_t st_block_size = 0;
	size_t st_size = 0;
	self->storage->vmt->get_block_size(self->storage->parent, &st_block_size);
	self->storage->vmt->get_size(self->storage->parent, &st_size);
	u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("block size %u, card size %u blocks, %u MB"), st_block_size, st_size, st_size / 1024 * st_block_size / 1024);
	self->block_size = st_block_size;
	self->size = st_size;

	/* No other checks are made during the initialization. */
	self->access_ke[0] = 1;

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
