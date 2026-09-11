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
#include <interfaces/flash.h>
#include <interfaces/fs.h>
#include <interfaces/stream.h>
#include <services/chainloader/tinyelf.h>
#include <blake2s.h>
#include <ed25519.h>

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
	//u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("load_xz_image_cache block %lu"), block);

	size_t block_start = 0;
	size_t block_end = block;

	/* Skip if the same block is requested again, with the exception of block 0. */
	if (block == self->image_cache_block && block > 0) {
		return FLASH_UPDATER_RET_OK;
	}

	/* Determine if the newly requested block is successive to the current one,
	 * continue decoding if yes. Restart the decoder if no. */
	if (block > self->image_cache_block) {
		block_start = self->image_cache_block + 1;
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

	//u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("start = %lu, end = %lu"), block_start, block_end);

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
				//u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("xz: stream end while decoding cache block %lu"), b);
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
					u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("xz: decoding error %d at block %lu"), ret, b);
					break;
			}
			return FLASH_UPDATER_RET_FAILED;
		}
	}

	return FLASH_UPDATER_RET_OK;
}


static flash_updater_ret_t load_image_cache(FlashUpdater *self, size_t block) {
	if (self->method == FLASH_UPDATER_SOURCE_METHOD_RAW) {
		if (abstract_source_read(self, block * CONFIG_IMAGE_CACHE_SIZE, self->image_cache, CONFIG_IMAGE_CACHE_SIZE) != FLASH_UPDATER_RET_OK) {
			return FLASH_UPDATER_RET_FAILED;
		}
		self->image_cache_block = block;
		return FLASH_UPDATER_RET_OK;
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

	flash_block_ops_t flash_ops = {0};
	if (self->target->vmt->get_size(self->target, 0, &self->target_size, &flash_ops) != FLASH_RET_OK ||
	    self->target->vmt->get_size(self->target, 1, &self->target_erase_size, &flash_ops) != FLASH_RET_OK ||
	    self->target->vmt->get_size(self->target, 3, &self->target_write_size, &flash_ops) != FLASH_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot retrieve target flash metadata"));
		return FLASH_UPDATER_RET_FAILED;
	}
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialized, target flash size = %lu KB, erase_size = %lu KB, write_size = %lu B"), self->target_size / 1024, self->target_erase_size / 1024, self->target_write_size);

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

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("source access method = flash volume"));

	return FLASH_UPDATER_RET_OK;
}


/**
 * @todo This progressbar rendering is not functional/pretty on the current implementation
 * of a graphic terminal console on a framebuffer device. It should be fixed on the terminal
 * side.
 *
 * The whole progress bar implementation is suboptimal and disabled now.
 */
static void progress_bar(FlashUpdater *self, size_t pos, size_t total) {
	return;

	if (self->console == NULL) {
		return;
	}

	char s[16];
	int32_t len = snprintf(s, sizeof(s), "\r%u / %u [", pos, total);
	self->console->vmt->write(self->console, s, len);
	for (uint32_t i = 0; i <= total; i++) {
		if (i < pos) {
			self->console->vmt->write(self->console, "#", 1);
		} else {
			self->console->vmt->write(self->console, " ", 1);
		}
	}
	self->console->vmt->write(self->console, "]", 1);
}


static void progress_bar_finish(FlashUpdater *self) {
	return;

	if (self->console == NULL) {
		return;
	}

	self->console->vmt->write(self->console, "\r\n", 2);
}



flash_updater_ret_t flash_updater_find_signature(FlashUpdater *self) {
	struct tinyelf_section_header hdr = {0};
	if (tinyelf_section_find_by_name(&self->elf, ".sign.ed25519", &hdr) != TINYELF_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("update signature not found"));
		return FLASH_UPDATER_RET_FAILED;
	}
	self->ed25519_sig_pos = hdr.offset;
	self->ed25519_sig_size = hdr.size;

	return FLASH_UPDATER_RET_OK;
}


/* Hash the byte range [start, end) of the update image into the running Blake2s state. Reads are done in
 * small chunks that never cross an image cache block boundary. The range is hashed byte-exact so the
 * signature section may sit at any (even unaligned) offset, matching the chainloader's verification. */
static flash_updater_ret_t hash_image_range(FlashUpdater *self, blake2s_state *s, size_t start, size_t end) {
	size_t pos = start;
	while (pos < end) {
		uint8_t buf[64];
		size_t chunk = end - pos;
		size_t block_left = CONFIG_IMAGE_CACHE_SIZE - (pos % CONFIG_IMAGE_CACHE_SIZE);
		if (chunk > block_left) {
			chunk = block_left;
		}
		if (chunk > sizeof(buf)) {
			chunk = sizeof(buf);
		}
		if (abstract_image_read(self, pos, buf, chunk) != FLASH_UPDATER_RET_OK) {
			return FLASH_UPDATER_RET_FAILED;
		}
		blake2s_update(s, buf, chunk);
		if ((pos % 1024) == 0) {
			progress_bar(self, pos / 1024, self->elf_size / 1024);
		}
		pos += chunk;
	}
	return FLASH_UPDATER_RET_OK;
}


static flash_updater_ret_t flash_updater_elf_b2s(FlashUpdater *self, uint8_t h[32]) {
	blake2s_state s;
	blake2s_init(&s, 32);

	/* Part before the excluded signature region. */
	if (hash_image_range(self, &s, 0, self->ed25519_sig_pos) != FLASH_UPDATER_RET_OK) {
		return FLASH_UPDATER_RET_FAILED;
	}

	/* The excluded signature region is hashed as a run of zeros. */
	uint8_t hm[32] = {0};
	size_t excluded = self->ed25519_sig_size;
	while (excluded > 0) {
		size_t chunk = excluded < sizeof(hm) ? excluded : sizeof(hm);
		blake2s_update(&s, hm, chunk);
		excluded -= chunk;
	}

	/* Part immediately following the excluded region to the end of the ELF. */
	if (hash_image_range(self, &s, self->ed25519_sig_pos + self->ed25519_sig_size, self->elf_size) != FLASH_UPDATER_RET_OK) {
		return FLASH_UPDATER_RET_FAILED;
	}
	progress_bar_finish(self);
	blake2s_final(&s, h);
	return FLASH_UPDATER_RET_OK;
}


flash_updater_ret_t flash_updater_check_signature(FlashUpdater *self, const uint8_t pubkey[32]) {
	/* Blake2s hash of the ELF file must be computed with the signature section
	 * excluded and replaced with zeros. */
	uint8_t h[32] = {0};

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("checking update ELF signature..."));

	uint8_t sig[64] = {0};
	if (abstract_image_read(self, self->ed25519_sig_pos, sig, self->ed25519_sig_size) != FLASH_UPDATER_RET_OK) {
		return FLASH_UPDATER_RET_FAILED;
	}

	if (flash_updater_find_signature(self) == FLASH_UPDATER_RET_OK &&
	    flash_updater_elf_b2s(self, h) == FLASH_UPDATER_RET_OK &&
	    ed25519_verify(sig, pubkey, h, 32) == ED25519_VERIFY_OK) {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("update ELF signature verified OK"));
		return FLASH_UPDATER_RET_OK;
	}

	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("update ELF signature verification failed"));
	return FLASH_UPDATER_RET_FAILED;
}


flash_updater_ret_t flash_updater_validate_source(FlashUpdater *self) {
	uint8_t magic[8];
	if (abstract_source_read(self, 0, magic, sizeof(magic)) != FLASH_UPDATER_RET_OK) {
		return FLASH_UPDATER_RET_FAILED;
	}

	if (!memcmp(magic, &(uint8_t[])ELF_MAGIC, ELF_MAGIC_LEN)) {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("raw ELF source found"));
		self->method = FLASH_UPDATER_SOURCE_METHOD_RAW;
	} else if (!memcmp(magic, &(uint8_t[])XZ_MAGIC, XZ_MAGIC_LEN)) {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("XZ compressed ELF source found"));
		self->method = FLASH_UPDATER_SOURCE_METHOD_XZ;

		/* initialize the decompressor before the image is accessed. */
		xz_crc32_init();
		self->xz = xz_dec_init(XZ_PREALLOC, 8192);
		if (self->xz == NULL) {
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("xz: cannot initialize"));
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
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot find ELF header"));
		return FLASH_UPDATER_RET_FAILED;
	}

	tinyelf_init(&self->elf, tinyelf_read, self);
	if (tinyelf_parse(&self->elf) != TINYELF_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("update ELF parsing error"));
		return FLASH_UPDATER_RET_FAILED;
	}
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("ELF header OK"));

	struct tinyelf_section_header hdr = {0};
	if (tinyelf_section_find_by_name(&self->elf, ".comment", &hdr) == TINYELF_RET_OK) {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("comment section found, size = %lu"), hdr.size);
	}

	self->elf_size = self->elf.elf_header.shoff + self->elf.elf_header.shentsize * self->elf.elf_header.shnum;

	return FLASH_UPDATER_RET_OK;
}


flash_updater_ret_t flash_updater_write(FlashUpdater *self) {
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("erasing target..."));

	/* Workaround for erasing the whole flash volume, full volume erase doesn't work properly. */
	for (size_t i = 0; i < (self->target_size / self->target_erase_size); i++) {
		progress_bar(self, i, self->target_size / self->target_erase_size);
		if (self->target->vmt->erase(self->target, i * self->target_erase_size, self->target_erase_size) != FLASH_RET_OK) {
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("target flash erasing failed"));
			return FLASH_UPDATER_RET_FAILED;
		}
	}
	progress_bar_finish(self);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("writing target..."));

	/* The target is programmed one write block at a time (an 8 byte double-word on STM32G4,
	 * a 16 byte quad-word on STM32U5, ...). Write the ELF rounded up to a whole write block;
	 * the ELF is only 4-byte aligned so the last block may read a few bytes past its end. */
	size_t write_size = self->target_write_size;
	if (write_size == 0 || write_size > CONFIG_IMAGE_CACHE_SIZE) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("unsupported target write size %lu"), write_size);
		return FLASH_UPDATER_RET_FAILED;
	}
	size_t blocks = (self->elf_size + write_size - 1) / write_size;
	for (size_t i = 0; i < blocks; i++) {
		uint8_t block[CONFIG_IMAGE_CACHE_SIZE];
		if (abstract_image_read(self, i * write_size, block, write_size) != FLASH_UPDATER_RET_OK) {
			return FLASH_UPDATER_RET_FAILED;
		}
		if (self->target->vmt->write(self->target, i * write_size, block, write_size) != FLASH_RET_OK) {
			return FLASH_UPDATER_RET_FAILED;
		}
		if (self->console && (i % 128) == 0) {
			progress_bar(self, i / 128, self->elf_size / 1024);
		}
	}
	progress_bar_finish(self);

	return FLASH_UPDATER_RET_OK;
}


flash_updater_ret_t flash_updater_set_console(FlashUpdater *self, Stream *console) {
	self->console = console;

	return FLASH_UPDATER_RET_OK;
}


flash_updater_ret_t flash_updater_disable_update(FlashUpdater *self) {
	/* Disables any further update by erasing the whole update source partition. */
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("disable further updates, nuke the source"));

	if (self->flash) {
		size_t source_size = 0;
		flash_block_ops_t ops = 0;
		if (self->flash->vmt->get_size(self->flash, 0, &source_size, &ops) != FLASH_RET_OK) {
			return FLASH_UPDATER_RET_FAILED;
		}
		if (self->flash->vmt->erase(self->flash, 0, source_size) != FLASH_RET_OK) {
			return FLASH_UPDATER_RET_FAILED;
		}
	}

	return FLASH_UPDATER_RET_OK;
}
