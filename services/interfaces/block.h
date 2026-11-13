/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Block memory device interface descriptor
 *
 * Copyright (c) 2021-2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
	BLOCK_RET_OK = 0,
	BLOCK_RET_FAILED,
	BLOCK_RET_BAD_ARG,
	BLOCK_RET_TIMEOUT,
	BLOCK_RET_NO_MEDIA,
	BLOCK_RET_READ_ONLY,
	BLOCK_RET_DEVICE_CHANGED,
} block_ret_t;

typedef struct block Block;

struct block_status {
	/** The media is present and ready to be accessed. */
	bool present;
	/** Size of a block in bytes and the number of blocks of the present media. */
	size_t block_size;
	uint32_t block_count;
};

/**
 * @brief Block device interface
 *
 * The device is an array of fixed-size blocks (sectors) addressed by a logical block address (LBA) starting
 * at 0. All methods are blocking and mandatory. Implementations are responsible for locking, the interface may be
 * used from multiple tasks concurrently.
 *
 * Removable media: every time the media appears or disappears, all methods except @p get_geometry return
 * BLOCK_RET_DEVICE_CHANGED until @p get_geometry is called. The user is expected to re-read the geometry and
 * invalidate anything it knows about the previous media.
 */
struct block_vmt {
	/**
	 * @brief Get the device geometry
	 *
	 * @param self Block interface instance
	 * @param block_size Size of a single block in bytes. It is fixed for the device.
	 * @param block_count Total number of blocks of the device
	 * @return BLOCK_RET_NO_MEDIA if the media is not present, BLOCK_RET_FAILED on error or BLOCK_RET_OK otherwise.
	 *         Acknowledges a pending media change.
	 */
	block_ret_t (*get_geometry)(Block *self, size_t *block_size, uint32_t *block_count);

	/**
	 * @brief Read consecutive blocks
	 *
	 * @param self Block interface instance
	 * @param lba Address of the first block to read
	 * @param buf Buffer where the data will be saved, at least @p count * block size bytes long
	 * @param count Number of blocks to read
	 * @return BLOCK_RET_BAD_ARG if @p count is 0 or the range is out of the device, BLOCK_RET_TIMEOUT if the device
	 *         is not responding, BLOCK_RET_NO_MEDIA if the media is not present, BLOCK_RET_DEVICE_CHANGED if the
	 *         media change was not acknowledged yet, BLOCK_RET_FAILED on error or BLOCK_RET_OK otherwise.
	 */
	block_ret_t (*read)(Block *self, uint32_t lba, void *buf, size_t count);

	/**
	 * @brief Write consecutive blocks
	 *
	 * The data may be cached by the device. Use @p flush to make sure it is committed to the media.
	 *
	 * @param self Block interface instance
	 * @param lba Address of the first block to write
	 * @param buf Buffer containing the data to write, at least @p count * block size bytes long
	 * @param count Number of blocks to write
	 * @return BLOCK_RET_BAD_ARG if @p count is 0 or the range is out of the device, BLOCK_RET_READ_ONLY if the media
	 *         is write protected, BLOCK_RET_TIMEOUT if the device is not responding, BLOCK_RET_NO_MEDIA if the media
	 *         is not present, BLOCK_RET_DEVICE_CHANGED if the media change was not acknowledged yet,
	 *         BLOCK_RET_FAILED on error or BLOCK_RET_OK otherwise.
	 */
	block_ret_t (*write)(Block *self, uint32_t lba, const void *buf, size_t count);

	/**
	 * @brief Commit all written data to the media
	 *
	 * @param self Block interface instance
	 * @return BLOCK_RET_TIMEOUT if the device is not responding, BLOCK_RET_NO_MEDIA if the media is not present,
	 *         BLOCK_RET_DEVICE_CHANGED if the media change was not acknowledged yet, BLOCK_RET_FAILED on error or
	 *         BLOCK_RET_OK otherwise (including the case when there is nothing to commit).
	 */
	block_ret_t (*flush)(Block *self);

	/**
	 * @brief Get the media status
	 *
	 * Non-blocking. A pending media change is not acknowledged, the method is meant for status displays which
	 * must not interfere with the user of the device. NULL if the device has no removable media.
	 *
	 * @param self Block interface instance
	 * @param status Current media status
	 * @return BLOCK_RET_FAILED if the status is not known (eg. the device driver is stopped) or BLOCK_RET_OK
	 *         otherwise.
	 */
	block_ret_t (*get_status)(Block *self, struct block_status *status);
};

typedef struct block {
	const struct block_vmt *vmt;
	void *parent;
} Block;
