/* SPDX-License-Identifier: BSD-2-Clause
 *
 * FIFO in a flash device
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include <interfaces/flash.h>
#include <interfaces/fs.h>


#define FLASH_FIFO_BITMAP_SIZE 1024
#define FLASH_FIFO_KEY_SIZE 16

/* Flash devices allow writing 0 bits only. Bits can be flipped back only
 * using the erase command. Hence an empty block must have 0xff in the header
 * and 1's may be changed to 0's along the way - erased -> half -> full -> gc. */
#define FLASH_FIFO_MAGIC_ERASED 0xffffffff
#define FLASH_FIFO_MAGIC_HEAD 0x77777777
#define FLASH_FIFO_MAGIC_FIFO 0x33333333
#define FLASH_FIFO_MAGIC_TAIL 0x00000000

#define FS_FILE_READING 0
#define FS_FILE_WRITING 1

struct flash_fifo_header {
	uint32_t magic;
	uint32_t bitmap[FLASH_FIFO_BITMAP_SIZE / 32];
	uint8_t iv[FLASH_FIFO_KEY_SIZE];
	uint8_t mac[FLASH_FIFO_KEY_SIZE];
};

typedef enum {
	FLASH_FIFO_RET_OK = 0,
	FLASH_FIFO_RET_FAILED,
	FLASH_FIFO_RET_FULL,
	FLASH_FIFO_RET_EMPTY,
} flash_fifo_ret_t;

typedef struct {
	Flash *flash;
	size_t flash_size;
	size_t block_size;
	size_t page_size;
	size_t blocks;
	uint8_t *page_buf;

	/* Tail is the last dirty FIFO block. All blocks earlier are properly erased. */
	uint32_t tail;
	/* Last is the last unread FIFO block. It is increased when the FIFO is rotated. */
	uint32_t last;
	/* Head is the first block containing data. It may not be complete. */
	uint32_t head;

	/* IFs interface */
	size_t read_offset;
	Fs fs;
} FlashFifo;


flash_fifo_ret_t flash_fifo_find_block(FlashFifo *self, size_t from, uint32_t magic, size_t *addr_l, size_t *addr_h);
flash_fifo_ret_t flash_fifo_stats(FlashFifo *self, size_t *erased, size_t *half, size_t *full, size_t *gc, size_t *inv, uint32_t *seq_l, uint32_t *seq_h);
flash_fifo_ret_t flash_fifo_write(FlashFifo *self, const uint8_t *buf, size_t len, size_t *written);
flash_fifo_ret_t flash_fifo_read(FlashFifo *self, uint8_t *buf, size_t len, size_t *read);

flash_fifo_ret_t flash_fifo_init(FlashFifo *self, Flash *flash);
flash_fifo_ret_t flash_fifo_free(FlashFifo *self);

