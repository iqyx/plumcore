/* SPDX-License-Identifier: BSD-2-Clause
 *
 * plumCore flash memory interface
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
	FLASH_RET_OK = 0,
	FLASH_RET_FAILED,
	FLASH_RET_BAD_ARG,
	FLASH_RET_TIMEOUT,
} flash_ret_t;

typedef enum {
	FLASH_BLOCK_OPS_READ = (1 << 0),
	FLASH_BLOCK_OPS_WRITE = (1 << 1),
	FLASH_BLOCK_OPS_ERASE = (1 << 2),
} flash_block_ops_t;

typedef struct flash Flash;

struct flash_vmt {
	/**
	 * @brief Get size of a flash block
	 *
	 * Function returns the size of an individually erasable block.
	 *
	 * @param self Flash interface instance
	 * @param i Level of the block size, 0 is the whole memory array
	 * @param size Size of the block
	 * @param ops Operations supported on the reported block size
	 * @return FLASH_RET_BAD_ARG if the @p i is invalid (no smaller sizes),
	 *         FLASH_RET_FAILED on error or FLASH_RET_OK otherwise.
	 */
	flash_ret_t (*get_size)(Flash *self, uint32_t i, size_t *size, flash_block_ops_t *ops);

	/**
	 * @brief Erase a block of flash
	 *
	 * Erase a block starting with an address @p addr with a size @p len.
	 * If the requested erase range is not aligned on the smalles erase
	 * block boundary, fail. The function may optimize the erase process
	 * using commands to erase different block sizes.
	 *
	 * @param self Flash interface instance
	 * @param addr Starting address of the range to be erased
	 * @param len Length of the range to be erased
	 * @return FLASH_RET_BAD_ARG if @p addr or @p len is not aligned on the
	 *         smallest erase block boundary, FLASH_RET_FAILED on error or
	 *         FLASH_RET_OK otherwise.
	 */
	flash_ret_t (*erase)(Flash *self, const size_t addr, size_t len);

	/**
	 * @brief Write block of data
	 *
	 * @param self Flash interface instance
	 * @param addr Address in the memory to write data to
	 * @param buf Buffer containing the data to be written
	 * @param len Size of the buffer (= length of the data to write)
	 * @return FLASH_RET_FAILED on error or FLASH_RET_OK otherwise.
	 */
	flash_ret_t (*write)(Flash *self, const size_t addr, const void *buf, size_t len);

	/**
	 * @brief Read block of data
	 *
	 * @param self Flash interface instance
	 * @param addr Address in the memory to read data from
	 * @param buf Buffer where the data will be saved
	 * @param len Size of the buffer (= length of the data to read)
	 *
	 * @return FLASH_RET_FAILED on error or FLASH_RET_OK otherwise.
	 */
	flash_ret_t (*read)(Flash *self, const size_t addr, void *buf, size_t len);
};

typedef struct flash {
	const struct flash_vmt *vmt;
	void *parent;
} Flash;
