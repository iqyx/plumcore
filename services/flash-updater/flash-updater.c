/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Flash updater srevice
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

#include <main.h>
#include <xz.h>

#include "flash-updater.h"
#include <services/chainloader/tinyelf.h>

#define MODULE_NAME "flash-updater"



static flash_updater_ret_t abstract_source_read(FlashUpdater *self, const size_t addr, void *buf, size_t len) {
	if (self->fs) {
		/** @todo not implemented */
		return FLASH_UPDATER_RET_FAILED;
	}
	if (self->flash) {
		if (self->flash->vmt->read(self->flash, addr, buf, len) == FLASH_RET_OK) {
			return FLASH_UPDATER_RET_OK;
		}
	}

	return FLASH_UPDATER_RET_FAILED;
}


static flash_updater_ret_t load_xz_image_cache(FlashUpdater *self, size_t block) {
	u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("load_xz_image_cache block %lu"), block);

	size_t block_start = 0;
	size_t block_end = block;

	/* Skip if the same block is requested again, with the exception of block 0. */
	if (block == self->image_cache_block && block > 0) {
		return FLASH_UPDATER_RET_OK;
	}

	/* Determine if the newly requested block is successive to the current one,
	 * continue decoding if yes. Restart the decoder if no. */
	if (block > self->image_cache_block) {
		block_start = self->image_cache_block;
	}
	self->image_cache_block = block;

	if (block_start == 0) {
		/* Reinitialize the decoder. */
		xz_dec_reset(self->xz);

		self->xz_buf.in = self->xz_read_buf;
		self->xz_buf.in_pos = 0;
		self->xz_buf.in_size = 0;
		self->xz_buf.out = self->image_cache;
		self->xz_buf.out_pos = 0;
		self->xz_buf.out_size = CONFIG_IMAGE_CACHE_SIZE;
		self->xz_pos = 0;
	}

	u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("start = %lu, end = %lu"), block_start, block_end);

	for (size_t b = block_start; b <= block_end; b++) {
		//u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("read xz block %lu"), b);

		while (true) {
			if (self->xz_buf.in_pos == self->xz_buf.in_size) {
				abstract_source_read(self, self->xz_pos, self->xz_read_buf, CONFIG_XZ_READ_BUF_SIZE);
				self->xz_buf.in_pos = 0;
				self->xz_buf.in_size = CONFIG_XZ_READ_BUF_SIZE;
				self->xz_pos += CONFIG_XZ_READ_BUF_SIZE;
			}

			enum xz_ret ret = xz_dec_run(self->xz, &self->xz_buf);
			//u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("ret = %d"), ret);

			if (self->xz_buf.out_pos == self->xz_buf.out_size) {
				/* Output is full, input data remains. Prepare for the next cache block,
				 * do not continue decompression. */
				self->xz_buf.out_pos = 0;
				break;
			}

			if (ret == XZ_OK) {
				/* Output buffer is not full yet but input data is required. Go back and
				 * continue decoding this cache block. */
				continue;
			}

			/* Nothing more to decode. Handle incomplete last cache buffer. */

			if (ret == XZ_STREAM_END) {
				u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("xz stream end while decoding cache block %lu"), b);
				//osize += self->xz_buf.out_pos;
				return FLASH_UPDATER_RET_OK;
			}

			switch (ret) {
				case XZ_UNSUPPORTED_CHECK:
					u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("xz: unsupported check type"));
					break;

				case XZ_MEM_ERROR:
				case XZ_MEMLIMIT_ERROR:
					u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("xz: was too optimistic about memory usage..."));
					break;

				case XZ_FORMAT_ERROR:
					u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("xz: not a xz, format error"));
					break;

				case XZ_OPTIONS_ERROR:
					u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("xz: unknwon decoding option/filter"));
					break;

				case XZ_DATA_ERROR:
					u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("xz: not a xz, bad data"));
					break;

				default:
					u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("XZ decoding error %d at block %lu"), ret, b);
					break;
			}
			return FLASH_UPDATER_RET_FAILED;
		}
	}

	return FLASH_UPDATER_RET_OK;
}


static flash_updater_ret_t load_image_cache(FlashUpdater *self, size_t block) {
	if (self->method == FLASH_UPDATER_SOURCE_METHOD_RAW) {
		return abstract_source_read(self, block * CONFIG_IMAGE_CACHE_SIZE, self->image_cache, CONFIG_IMAGE_CACHE_SIZE);
	} else if (self->method == FLASH_UPDATER_SOURCE_METHOD_XZ) {
		return load_xz_image_cache(self, block);
	} else {
		return FLASH_UPDATER_RET_FAILED;
	}
}


static flash_updater_ret_t abstract_image_read(FlashUpdater *self, const size_t addr, void *buf, size_t len) {
	if (len > CONFIG_IMAGE_CACHE_SIZE) {
		return FLASH_UPDATER_RET_FAILED;
	}

	if (((addr % CONFIG_IMAGE_CACHE_SIZE) + len) > CONFIG_IMAGE_CACHE_SIZE) {
		return FLASH_UPDATER_RET_FAILED;
	}

	size_t block = addr / CONFIG_IMAGE_CACHE_SIZE;
	if (block != ((addr + len - 1) / CONFIG_IMAGE_CACHE_SIZE)) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("abstract_image_read: unaligned access"));
		return FLASH_UPDATER_RET_FAILED;
	}

	if (block != self->image_cache_block) {
		if (load_image_cache(self, block) != FLASH_UPDATER_RET_OK) {
			return FLASH_UPDATER_RET_FAILED;
		}
	}

	//u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("ab im read addr = %p, len = %lu"), addr, len);
	memcpy(buf, self->image_cache + (addr % CONFIG_IMAGE_CACHE_SIZE), len);

	return FLASH_UPDATER_RET_OK;
}


static tinyelf_ret_t tinyelf_read(Elf *elf, size_t pos, void *buf, size_t len, size_t *read) {
	FlashUpdater *self = elf->read_ctx;

	if (abstract_image_read(self, pos, buf, len) == FLASH_UPDATER_RET_OK) {
		*read = len;
		//u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("tinyelf read %p = 0x%02x, len = %lu"), pos, ((uint8_t *)buf)[0], len);
		return TINYELF_RET_OK;
	}

	return TINYELF_RET_FAILED;
}


flash_updater_ret_t flash_updater_init(FlashUpdater *self, Flash *target) {
	memset(self, 0, sizeof(FlashUpdater));
	self->target = target;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initializing"));
	return FLASH_UPDATER_RET_OK;
}


flash_updater_ret_t flash_updater_free(FlashUpdater *self) {
	memset(self, 0, sizeof(FlashUpdater));

	return FLASH_UPDATER_RET_OK;
}


flash_updater_ret_t flash_updater_set_source_fs(FlashUpdater *self, Fs *fs) {
	if (self->flash != NULL) {
		/* Already set flash access to the update. */
		return FLASH_UPDATER_RET_FAILED;
	}
	self->fs = fs;

	/** @todo not implemented */
	return FLASH_UPDATER_RET_FAILED;
}


flash_updater_ret_t flash_updater_set_source_flash(FlashUpdater *self, Flash *flash) {
	if (self->fs != NULL) {
		/* Already set filesystem access to the update. */
		return FLASH_UPDATER_RET_FAILED;
	}
	self->flash = flash;

	/** @todo read flash properties here */

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("update access method set to: flash volume"));

	return FLASH_UPDATER_RET_OK;
}


flash_updater_ret_t flash_updater_validate_source(FlashUpdater *self) {
	uint8_t magic[8];
	if (abstract_source_read(self, 0, magic, sizeof(magic)) != FLASH_UPDATER_RET_OK) {
		return FLASH_UPDATER_RET_FAILED;
	}

	if (!memcmp(magic, &(uint8_t[])ELF_MAGIC, ELF_MAGIC_LEN)) {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("raw ELF source update image detected"));
		self->method = FLASH_UPDATER_SOURCE_METHOD_RAW;
	} else if (!memcmp(magic, &(uint8_t[])XZ_MAGIC, XZ_MAGIC_LEN)) {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("compressed XZ source update image detected"));
		self->method = FLASH_UPDATER_SOURCE_METHOD_XZ;

		/* initialize the decompressor before the image is accessed. */
		xz_crc32_init();
		self->xz = xz_dec_init(XZ_PREALLOC, 8192);
		//u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("xz_dec_init = %p"), self->xz);
		if (self->xz == NULL) {
			return FLASH_UPDATER_RET_FAILED;
		}
	} else {
		/* No known method recognized. */
		return FLASH_UPDATER_RET_FAILED;
	}
	load_image_cache(self, 0);

	/* Do it again with the right method set. */
	memset(magic, 0, sizeof(magic));
	if (abstract_image_read(self, 0, magic, sizeof(magic)) != FLASH_UPDATER_RET_OK) {
		return FLASH_UPDATER_RET_FAILED;
	}

	tinyelf_init(&self->elf, tinyelf_read, self);
	if (tinyelf_parse(&self->elf) != TINYELF_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("update ELF parsing error"));
		return FLASH_UPDATER_RET_FAILED;
	}
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("update ELF image header found and parsed"));

	struct tinyelf_section_header hdr = {0};
	if (tinyelf_section_find_by_name(&self->elf, ".sign.ed25519", &hdr) == TINYELF_RET_OK) {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("update contains ed25519 signature, verification not implemented"));
	}

	if (tinyelf_section_find_by_name(&self->elf, ".comment", &hdr) == TINYELF_RET_OK) {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("comment section found, size = %lu"), hdr.size);
	}


	return FLASH_UPDATER_RET_OK;
}
