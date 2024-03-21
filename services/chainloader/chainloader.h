/* SPDX-License-Identifier: GPL-3.0-or-later
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
#include <cbor.h>

#include "tinyelf.h"

/**
 * Chainloading only works with ELF files loaded in the memory (either flash or SRAM).
 * Hence the API uses uint8_t * pointer to pass addresses and size_t to pass sizes.
 * Tinyelf library API uses uint32_t for this purpose as ELF always talks about offsets.
 */

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

/**
 * @brief Try to find an ELF header in the specified range, parse it
 *
 * @param self Chainloader instance
 * @param mstart Beginning of the memory region to search
 * @param msize Size of the memory region to search
 * @param mstep Alignment of the ELF image header. The search is done
 *              in multiplies of @p mstep from the @p mstart until @p end is reached.
 */
chainloader_ret_t chainloader_find_elf(ChainLoader *self, uint8_t *mstart, size_t msize, size_t mstep);

/**
 * @brief Set the ELF position and size explicitly, parse it
 *
 * If the position of the ELF is known beforehand, it can be set explicitly
 * without doing prior search.
 *
 * @param buf Pointer to the ELF image
 * @param size Size of the ELF image in memory
 */
chainloader_ret_t chainloader_set_elf(ChainLoader *self, uint8_t *buf, size_t size);

/**
 * @brief Chainload/boot a parsed ELF
 *
 * Setup te system to chainload a previously parsed ELF. The current system
 * ceases to exist, data memory is fully used by the new chainloaded application
 * image. Vector table is relocated to the new position.
 */
chainloader_ret_t chainloader_boot(ChainLoader *self);

/** @todo functions to be moved to the tinyelf library */
chainloader_ret_t chainloader_find_signature(ChainLoader *self, uint8_t **addr, size_t *size);
chainloader_ret_t chainloader_check_signature(ChainLoader *self, const uint8_t pubkey[32]);
