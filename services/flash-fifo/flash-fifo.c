/* SPDX-License-Identifier: BSD-2-Clause
 *
 * FIFO in a flash device
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>

#include "config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "u_log.h"
#include "u_assert.h"

#include <interfaces/flash.h>
#include <interfaces/fs.h>

#include "flash-fifo.h"

#define MODULE_NAME "flash-fifo"


static flash_fifo_ret_t read_header(FlashFifo *self, uint32_t pos, struct flash_fifo_header *h) {
	if (self->flash->vmt->read(self->flash, (pos % self->blocks) * self->block_size, h, sizeof(struct flash_fifo_header)) != FLASH_RET_OK) {
		return FLASH_FIFO_RET_FAILED;
	}
	return FLASH_FIFO_RET_OK;
}


static flash_fifo_ret_t write_header(FlashFifo *self, uint32_t pos, struct flash_fifo_header *h) {
	if (self->flash->vmt->write(self->flash, (pos % self->blocks) * self->block_size, h, sizeof(struct flash_fifo_header)) != FLASH_RET_OK) {
		return FLASH_FIFO_RET_FAILED;
	}
	return FLASH_FIFO_RET_OK;
}


static flash_fifo_ret_t find_block(FlashFifo *self, uint32_t start, uint32_t magic, uint32_t *pos) {
	struct flash_fifo_header h = {0};
	for (uint32_t i = start; i < (self->blocks + start); i++) {
		if (read_header(self, i, &h) != FLASH_FIFO_RET_OK) {
			return FLASH_FIFO_RET_FAILED;
		}
		if (h.magic == magic) {
			*pos = i;
			return FLASH_FIFO_RET_OK;
		}
	}
	return FLASH_FIFO_RET_FAILED;
}


static flash_fifo_ret_t find_fifo(FlashFifo *self) {
	/* Lets start at 0. Find the first erased block. */
	uint32_t pos = 0;
	if (find_block(self, pos, FLASH_FIFO_MAGIC_ERASED, &pos) != FLASH_FIFO_RET_OK) {
		/* No erased block in the FIFO length means the FIFO is corrupted.
		 * We have to maintain at least one erased block. */
		return FLASH_FIFO_RET_FAILED;
	}
	/* Search forward to find the tail. */
	if (find_block(self, pos, FLASH_FIFO_MAGIC_TAIL, &pos) != FLASH_FIFO_RET_OK) {
		/* No tail. That's okay, it means the whole tail is garbage collected. */
		if (find_block(self, pos, FLASH_FIFO_MAGIC_FIFO, &pos) != FLASH_FIFO_RET_OK) {
			/* No FIFO at all. Maybe it is only 1 block long. */
			if (find_block(self, pos, FLASH_FIFO_MAGIC_HEAD, &pos) != FLASH_FIFO_RET_OK) {
				/* No head. Much bigger problem. We need to create one. */
				return FLASH_FIFO_RET_EMPTY;
			}
		}
		/* Found the FIFO. Mark it's last block and tail. */
		self->tail = pos;
		self->last = pos;
	} else {
		/* Mark the tail and continue to find the last FIFO block. */
		self->tail = pos;
		if (find_block(self, pos, FLASH_FIFO_MAGIC_FIFO, &pos) != FLASH_FIFO_RET_OK) {
			/* No FIFO found. Whose tail was that?! Maybe the head is there. */
			if (find_block(self, pos, FLASH_FIFO_MAGIC_HEAD, &pos) != FLASH_FIFO_RET_OK) {
				/* No head again. Corrupted. */
				return FLASH_FIFO_RET_FAILED;
			}
		}
		self->last = pos;
	}
	/* Find the head. There may be none, beware. */
	if (find_block(self, pos, FLASH_FIFO_MAGIC_HEAD, &pos) != FLASH_FIFO_RET_OK) {
		/* No problem at all. It just means the head is fully written and
		 * no new block is prepared yet. Find the first erased block. */
		if (find_block(self, pos, FLASH_FIFO_MAGIC_ERASED, &pos) != FLASH_FIFO_RET_OK) {
			/* No erased block. Even if we wanted to prepare a new head, we can not. */
			return FLASH_FIFO_RET_FAILED;
		}
		/* Found it. So the head is one block back. Fully written, weird. */
		self->head = pos - 1;
	} else {
		/* No multiple heads, sorry. */
		self->head = pos;
	}
	return FLASH_FIFO_RET_OK;
}


static size_t bitmap_to_offset(FlashFifo *self, uint32_t *b, size_t len) {
	size_t z = 0;
	while (len && *b == 0) {
		z += 32;
		b++;
		len--;
	}
	if (len) {
		z += __builtin_clz(*b);
		b++;
		len--;
	}
	/* The rest is filled with ones */
	return (self->block_size / FLASH_FIFO_BITMAP_SIZE) * z;
}


static void offset_to_bitmap(FlashFifo *self, uint32_t *b, size_t len, size_t offset) {

	size_t byte_size = self->block_size / FLASH_FIFO_BITMAP_SIZE * 32;
	while (len && offset > byte_size) {
		*b = 0;
		b++;
		len--;
		offset -= byte_size;
	}
	if (len) {
		uint32_t bits = offset / (self->block_size / FLASH_FIFO_BITMAP_SIZE);
		*b = UINT32_MAX >> bits;
		b++;
		len--;
	}
	while (len) {
		*b = UINT32_MAX;
		b++;
		len--;
	}
}


static void log_fifo(FlashFifo *self, const char *action) {
	char s[self->blocks + 1];
	struct flash_fifo_header h = {0};

	for (uint32_t i = 0; i < self->blocks; i++) {
		if (read_header(self, i, &h) != FLASH_FIFO_RET_OK) {
			return;
		}
		switch (h.magic) {
			case FLASH_FIFO_MAGIC_ERASED:
				s[i] = '-';
				break;
			case FLASH_FIFO_MAGIC_HEAD:
				s[i] = '>';
				break;
			case FLASH_FIFO_MAGIC_FIFO:
				s[i] = 'F';
				break;
			case FLASH_FIFO_MAGIC_TAIL:
				s[i] = '<';
				break;
			default:
				s[i] = '?';
		}
	}
	s[self->blocks] = '\0';
	u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("action %10s: %s"), action, s);
}


/**
 * Prepare a new head at position @p pos. It may fail if there is no free space left.
 */
static flash_fifo_ret_t prepare_head(FlashFifo *self, uint32_t pos) {
	struct flash_fifo_header h = {0};

	/* Check the next block. It must be erased too in order to keep at least
	 * one erased block after converting the current one to a new head. */
	read_header(self, pos + 1, &h);
	if (h.magic != FLASH_FIFO_MAGIC_ERASED) {
		return FLASH_FIFO_RET_FULL;
	}

	/* And check if the current one is erased. */
	read_header(self, pos, &h);
	if (h.magic != FLASH_FIFO_MAGIC_ERASED) {
		return FLASH_FIFO_RET_FULL;
	}

	/* Create a new header. */
	h.magic = FLASH_FIFO_MAGIC_HEAD;
	/** @todo generate a random IV */
	memset(h.iv, 0x55, FLASH_FIFO_KEY_SIZE);

	if (write_header(self, pos, &h) != FLASH_FIFO_RET_OK) {
		return FLASH_FIFO_RET_FAILED;
	}
	log_fifo(self, "new-head");

	return FLASH_FIFO_RET_OK;
}


static flash_fifo_ret_t close_head(FlashFifo *self, uint32_t pos) {
	struct flash_fifo_header h = {0};

	read_header(self, pos, &h);
	if (h.magic != FLASH_FIFO_MAGIC_HEAD) {
		return FLASH_FIFO_RET_FAILED;
	}

	/* Update the header and compute MAC */
	h.magic = FLASH_FIFO_MAGIC_FIFO;

	if (write_header(self, pos, &h) != FLASH_FIFO_RET_OK) {
		return FLASH_FIFO_RET_FAILED;
	}
	log_fifo(self, "close-head");

	return FLASH_FIFO_RET_OK;
}


static void format(FlashFifo *self) {
	self->flash->vmt->erase(self->flash, 0, self->flash_size);
	prepare_head(self, 0);
	log_fifo(self, "format");
}


static flash_fifo_ret_t get_block_write_range(FlashFifo *self, size_t *offset, size_t *len) {
	if (*offset >= self->block_size) {
		/* No more data can be fit into the current block. */
		return FLASH_FIFO_RET_FAILED;
	}
	if (*offset == 0) {
		/* Zero offset means we are starting in an empty block
		 * or we simply don't know where to start. Read the block
		 * bitmap and determine range of the data already written. */
		struct flash_fifo_header h = {0};
		read_header(self, self->head, &h);
		if (h.magic != FLASH_FIFO_MAGIC_HEAD) {
			return FLASH_FIFO_RET_FAILED;
		}
		*offset = bitmap_to_offset(self, h.bitmap, FLASH_FIFO_BITMAP_SIZE / 32);
	}

	/* Do not go beyond the block boundary */
	size_t end = self->block_size - self->page_size;
	/* Do not write more than a page_size */
	if ((end - *offset) > self->page_size) {
		end = *offset + self->page_size;
	}
	/* The end must be within the same page. */
	end -= end % self->page_size;
	*len = end - *offset;

	return FLASH_FIFO_RET_OK;
}


static flash_fifo_ret_t set_block_write_usage(FlashFifo *self, size_t len) {
	struct flash_fifo_header h = {0};
	read_header(self, self->head, &h);
	if (h.magic != FLASH_FIFO_MAGIC_HEAD) {
		return FLASH_FIFO_RET_FAILED;
	}
	offset_to_bitmap(self, h.bitmap, FLASH_FIFO_BITMAP_SIZE / 32, len - 1 + self->block_size / FLASH_FIFO_BITMAP_SIZE);
	if (write_header(self, self->head, &h) != FLASH_FIFO_RET_OK) {
		return FLASH_FIFO_RET_FAILED;
	}
	return FLASH_FIFO_RET_OK;
}


static flash_fifo_ret_t block_write_data(FlashFifo *self, size_t offset, const uint8_t *buf, size_t len) {
	/* Do not check, assume we can safely write at the specified offset. */
	// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("write block data offset = %p, len = %p"), offset, len);
	if (self->flash->vmt->write(self->flash, self->head * self->block_size + self->page_size + offset, buf, len) != FLASH_RET_OK) {
		return FLASH_FIFO_RET_FAILED;
	}
	return FLASH_FIFO_RET_OK;
}


flash_fifo_ret_t flash_fifo_write(FlashFifo *self, const uint8_t *buf, size_t len, size_t *written) {
	size_t rem = len;
	/* Write data */
	size_t block_write_offset = 0;
	size_t block_write_len = 0;
	while (get_block_write_range(self, &block_write_offset, &block_write_len) == FLASH_FIFO_RET_OK && rem > 0 && block_write_len > 0) {
		if (rem < block_write_len) {
			block_write_len = rem;
		}
		block_write_data(self, block_write_offset, buf, block_write_len);
		buf += block_write_len;
		rem -= block_write_len;
		block_write_offset += block_write_len;
	}
	/* We have written as much as we could. Update the bitmap. */
	set_block_write_usage(self, block_write_offset);
	if (written) {
		*written = len - rem;
	}
	if (rem) {
		/* Some data couldn't be written to the current head block. */
		close_head(self, self->head);
		flash_fifo_ret_t ret = prepare_head(self, self->head + 1);
		if (ret == FLASH_FIFO_RET_OK) {
			self->head++;
		}
		return ret;
	}
	return FLASH_FIFO_RET_OK;
}


flash_fifo_ret_t flash_fifo_read(FlashFifo *self, uint8_t *buf, size_t len, size_t *read) {
	/* Do not go beyond the block boundary */
	size_t end = self->block_size - self->page_size;
	/* Do not write more than a page_size */
	if ((end - self->read_offset) > self->page_size) {
		end = self->read_offset + self->page_size;
	}
	/* The end must be within the same page. */
	end -= end % self->page_size;
	size_t read_len = end - self->read_offset;
	if (read_len == 0) {
		return FLASH_FIFO_RET_EMPTY;
	}
	if (len < read_len) {
		read_len = len;
	}
	if (self->flash->vmt->read(self->flash, self->read_offset, buf, len) != FLASH_RET_OK) {
		return FLASH_FIFO_RET_FAILED;
	}
	self->read_offset += len;
	if (read != NULL) {
		*read = len;
	}
	return FLASH_FIFO_RET_OK;
}


static flash_fifo_ret_t flash_fifo_gc_single(FlashFifo *self) {
	/* Check the tail. Do nothing if it is not a tail. */
	struct flash_fifo_header h = {0};
	read_header(self, self->tail, &h);
	if (h.magic != FLASH_FIFO_MAGIC_TAIL) {
		return FLASH_FIFO_RET_FAILED;
	}
	if (self->flash->vmt->erase(self->flash, (self->tail % self->blocks) * self->block_size, self->block_size) != FLASH_RET_OK) {
		return FLASH_FIFO_RET_FAILED;
	}
	self->tail++;
	log_fifo(self, "gc");

	return FLASH_FIFO_RET_OK;
}



/*************************************************************************************************
 * IFs filesystem interface implementation
 *************************************************************************************************/

static fs_ret_t fs_open(Fs *self, File *f, const char *path, enum fs_mode mode) {
	if (u_assert(f != NULL) ||
	    u_assert(path != NULL) ||
	    u_assert(self != NULL)) {
		return FS_RET_FAILED;
	}
	FlashFifo *ff = (FlashFifo *)self->parent;

	if (!strcmp(path, "fifo") && (mode & FS_MODE_READONLY)) {
		f->handle = FS_FILE_READING;
		ff->read_offset = 0;
		return FS_RET_OK;
	}
	if (!strcmp(path, "fifo") && (mode & FS_MODE_WRITEONLY)) {
		f->handle = FS_FILE_WRITING;
		ff->read_offset = 0;
		return FS_RET_OK;
	}
	return FS_RET_FAILED;
}


static fs_ret_t fs_close(Fs *self, File *f) {
	if (u_assert(self != NULL) &&
	    u_assert(f->handle == FS_FILE_READING || f->handle == FS_FILE_WRITING)) {
		return FS_RET_FAILED;
	}
	return FS_RET_OK;
}


static fs_ret_t fs_remove(Fs *self, const char *path) {
	if (u_assert(self != NULL) &&
	    u_assert(path != NULL)) {
		return FS_RET_FAILED;
	}
	FlashFifo *ff = (FlashFifo *)self->parent;

	if (!strcmp(path, "fifo")) {
		struct flash_fifo_header h = {0};
		read_header(ff, ff->last, &h);
		if (h.magic != FLASH_FIFO_MAGIC_FIFO) {
			/* Nothing to remove yet. */
			return FS_RET_FAILED;
		}
		h.magic = FLASH_FIFO_MAGIC_TAIL;
		write_header(ff, ff->last, &h);
		ff->last++;
		log_fifo(ff, "removed");
		return FS_RET_OK;
	}
	return FS_RET_FAILED;
}


static fs_ret_t fs_read(Fs *self, File *f, void *buf, size_t len, size_t *read) {
	FlashFifo *ff = (FlashFifo *)self->parent;
	if (f->handle != FS_FILE_READING) {
		return FS_RET_FAILED;
	}
	size_t r = 0;
	size_t rem = len;
	flash_fifo_ret_t ret = 0;
	while (rem > 0 && (ret = flash_fifo_read(ff, buf, rem, &r)) == FLASH_FIFO_RET_OK) {
		buf = (uint8_t *)buf + r;
		rem -= r;
	}
	if (ret == FLASH_FIFO_RET_OK) {
		if (read != NULL) {
			*read = len - rem;
		}
		return FS_RET_OK;
	}
	return FS_RET_FAILED;
}


static fs_ret_t fs_write(Fs *self, File *f, const void *buf, size_t len, size_t *written) {
	FlashFifo *ff = (FlashFifo *)self->parent;
	if (f->handle != FS_FILE_WRITING) {
		return FS_RET_FAILED;
	}

	flash_fifo_ret_t ret = flash_fifo_write(ff, buf, len, written);
	if (ret == FLASH_FIFO_RET_OK) {
		log_fifo(ff, "written");
		return FS_RET_OK;
	}

	return FS_RET_FAILED;
}


static fs_ret_t fs_info(Fs *self, struct fs_info *info) {
	if (u_assert(self != NULL)) {
		return FS_RET_FAILED;
	}
	FlashFifo *ff = (FlashFifo *)self->parent;

	info->size_total = ff->blocks * (ff->block_size - ff->page_size);
	/** @todo better computation */
	info->size_used = (ff->head - ff->tail) * (ff->block_size - ff->page_size);

	return FS_RET_OK;
}


static const struct fs_vmt vmt = {
	.open = fs_open,
	.close = fs_close,
	.read = fs_read,
	.write = fs_write,
	.remove = fs_remove,
	.info = fs_info
};


flash_fifo_ret_t flash_fifo_init(FlashFifo *self, Flash *flash) {
	if (u_assert(self != NULL) ||
	    u_assert(flash != NULL)) {
		return FLASH_FIFO_RET_FAILED;
	}
	memset(self, 0, sizeof(FlashFifo));
	self->flash = flash;
	flash_block_ops_t ops = 0;

	/** @todo find block sizes properly */
	flash->vmt->get_size(flash, 0, &self->flash_size, &ops);
	flash->vmt->get_size(flash, 1, &self->block_size, &ops);
	flash->vmt->get_size(flash, 3, &self->page_size, &ops);
	self->blocks = self->flash_size / self->block_size;

	self->page_buf = malloc(self->page_size);
	if (self->page_buf == NULL) {
		goto err;
	}

	format(self);
	if (find_fifo(self) == FLASH_FIFO_RET_FAILED) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("FIFO content missing or corrupted"));
		format(self);
		if (find_fifo(self) != FLASH_FIFO_RET_OK) {
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("didn't help, no FIFO available"));
			goto err;
		}
	}
	log_fifo(self, "init");
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("blocks %u, head %u, last %u, tail %u"), self->blocks, (self->head % self->blocks), (self->last % self->blocks), (self->tail % self->blocks));

	self->fs.parent = (void *)self;
	self->fs.vmt = &vmt;

	return FLASH_FIFO_RET_OK;
err:
	flash_fifo_free(self);
	return FLASH_FIFO_RET_FAILED;
}


flash_fifo_ret_t flash_fifo_free(FlashFifo *self) {
	if (u_assert(self != NULL)) {
		return FLASH_FIFO_RET_FAILED;
	}

	free(self->page_buf);

	return FLASH_FIFO_RET_OK;
}






