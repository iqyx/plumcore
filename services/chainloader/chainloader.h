/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Service for chainloading another firmware
 *
 * Copyright (c) 2023-2024, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <main.h>
#include <interfaces/fs.h>
#include <cbor.h>

#include "tinyelf.h"


typedef enum {
	CHAINLOADER_RET_OK = 0,
	CHAINLOADER_RET_FAILED,
} chainloader_ret_t;

typedef struct chainloader {

	uint8_t *membuf;
	size_t memsize;

	Elf elf;
	uint32_t vector_table;

} ChainLoader;


chainloader_ret_t chainloader_init(ChainLoader *self);
chainloader_ret_t chainloader_free(ChainLoader *self);

chainloader_ret_t chainloader_find_elf(ChainLoader *self, uint32_t mstart, uint32_t msize, uint32_t mstep);
chainloader_ret_t chainloader_set_elf(ChainLoader *self, uint8_t *buf, size_t size);
chainloader_ret_t chainloader_boot(ChainLoader *self);

chainloader_ret_t chainloader_find_signature(ChainLoader *self, uint8_t **addr, size_t *size);
chainloader_ret_t chainloader_check_signature(ChainLoader *self, const uint8_t pubkey[32]);
