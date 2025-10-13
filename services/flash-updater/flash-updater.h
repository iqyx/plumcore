/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Flash updater service
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <main.h>
#include <interfaces/flash.h>
#include <interfaces/fs.h>
#include <services/chainloader/tinyelf.h>
#include <xz.h>


#define CONFIG_IMAGE_CACHE_SIZE 1024
#define CONFIG_XZ_READ_BUF_SIZE 256

typedef enum {
	FLASH_UPDATER_RET_OK = 0,
	FLASH_UPDATER_RET_FAILED,
} flash_updater_ret_t;

enum flash_updater_source_method {
	FLASH_UPDATER_SOURCE_METHOD_NONE = 0,
	FLASH_UPDATER_SOURCE_METHOD_RAW,
	FLASH_UPDATER_SOURCE_METHOD_XZ,
};

#define ELF_MAGIC {0x7f, 'E', 'L', 'F'}
#define ELF_MAGIC_LEN 4

#define XZ_MAGIC {0xfd, '7', 'z', 'X', 'Z', 0x00}
#define XZ_MAGIC_LEN 6


typedef struct flash_updater {
	/* Flash device target to write the image to. Must always be set.
	 * NULL means the service is not initialized properly. */
	Flash *target;

	/* Source for update images. Can be filesystem or an object store
	 * with a @p Fs interface. */
	Fs *fs;

	Flash *flash;

	/* Abstract access to the update source content. */
	size_t image_cache_block;
	uint8_t image_cache[CONFIG_IMAGE_CACHE_SIZE];
	enum flash_updater_source_method method;

	struct xz_dec *xz;
	uint8_t xz_read_buf[CONFIG_XZ_READ_BUF_SIZE];
	struct xz_buf xz_buf;
	size_t xz_pos;

	/* Update ELF image. */
	Elf elf;

} FlashUpdater;



flash_updater_ret_t flash_updater_init(FlashUpdater *self, Flash *target);
flash_updater_ret_t flash_updater_free(FlashUpdater *self);

/**
 * @brief Set @p Fs filesystem source for firmware images
 */
flash_updater_ret_t flash_updater_set_source_fs(FlashUpdater *self, Fs *fs);
flash_updater_ret_t flash_updater_set_source_flash(FlashUpdater *self, Flash *flash);
flash_updater_ret_t flash_updater_validate_source(FlashUpdater *self);

