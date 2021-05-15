/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Flash memory test suite
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
#include <interfaces/clock.h>

#include "flash-test.h"

#define MODULE_NAME "flash-test"

/*
Tests:
  - write zero page by page, check if writing succeeds for the entire flash
  - timed erase sector by sector, check sector size (should not overflow), check erase state
  - timed write alternating pattern, timed check if succeeds
  - timed erase block by block, check block size
  - write PRNG pattern, check if succeeds (all flash block containt different values)
  - timed erase chip
  - check erased state (full chip)
*/


/* The biggest block readable/writable in a single call to read/write */
static size_t get_page_size(Flash *flash) {
	flash_block_ops_t ops = 0;
	size_t page_size = 0;
	uint32_t i = 0;
	while (flash->vmt->get_size(flash, i, &page_size, &ops) == FLASH_RET_OK) {
		/* Get the biggest (first) readable/writable block size */
		if ((ops & FLASH_BLOCK_OPS_WRITE) && (ops & FLASH_BLOCK_OPS_READ)) {
			return page_size;
		}
		i++;
	}
	/* No readable/writable flash block size found */
	return 0;
}


/* The smallest erasable block size (sector) */
static size_t get_erase_sector_size(Flash *flash) {
	flash_block_ops_t ops = 0;
	size_t sector_size = 0;
	size_t r = 0;
	uint32_t i = 0;
	while (flash->vmt->get_size(flash, i, &sector_size, &ops) == FLASH_RET_OK) {
		/* Get the biggest (first) readable/writable block size */
		if (ops & FLASH_BLOCK_OPS_ERASE) {
			r = sector_size;
		}
		i++;
	}
	return r;
}


/* The biggest erasable block size */
static size_t get_erase_block_size(Flash *flash) {
	flash_block_ops_t ops = 0;
	size_t block_size = 0;
	/* Start with 1, 0 is the whole chip. */
	uint32_t i = 1;
	while (flash->vmt->get_size(flash, i, &block_size, &ops) == FLASH_RET_OK) {
		if (ops & FLASH_BLOCK_OPS_ERASE) {
			return block_size;
		}
		i++;
	}
	return 0;
}


static size_t get_flash_size(Flash *flash) {
	flash_block_ops_t ops = 0;
	size_t flash_size = 0;
	if (flash->vmt->get_size(flash, 0, &flash_size, &ops) == FLASH_RET_OK) {
		return flash_size;
	}
	return 0;
}


static int32_t timespec_diff(struct timespec *a, struct timespec *b) {
	int64_t a_us = a->tv_sec * 1000000 + a->tv_nsec / 1000;
	int64_t b_us = b->tv_sec * 1000000 + b->tv_nsec / 1000;
	return a_us - b_us;
}


flash_test_ret_t flash_test_write_pattern(FlashTest *self, struct flash_test_dev *dev, uint8_t pattern) {
	uint8_t *buf = malloc(sizeof(uint8_t) * dev->page_size);
	if (buf == NULL) {
		return FLASH_TEST_RET_FAILED;
	}
	memset(buf, pattern, sizeof(uint8_t) * dev->page_size);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("TEST write entire flash with 0x%02x, %u pages, page size %u B"),
		pattern,
		dev->flash_size / dev->page_size,
		dev->page_size
	);

	/* Timed test code */
	struct timespec start = {0};
	self->rtc->get(self->rtc->parent, &start);
	for (size_t page = 0; page < (dev->flash_size / dev->page_size); page++) {
		dev->flash->vmt->write(dev->flash, page * dev->page_size, buf, dev->page_size);
	}
	struct timespec end = {0};
	self->rtc->get(self->rtc->parent, &end);
	uint32_t time_us = timespec_diff(&end, &start);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("  -> total time %u ms, page write time %u ms, speed %u KB/s"),
		time_us / 1000,
		time_us / 1000 / (dev->flash_size / dev->page_size),
		dev->flash_size / (time_us / 1000) * 1000 / 1024
	);

	free(buf);
	return FLASH_TEST_RET_OK;
}


flash_test_ret_t flash_test_verify_pattern(FlashTest *self, struct flash_test_dev *dev, uint8_t pattern) {
	uint8_t *buf = malloc(sizeof(uint8_t) * dev->page_size);
	if (buf == NULL) {
		return FLASH_TEST_RET_FAILED;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("TEST verify entire flash == 0x%02x"),pattern);

	/* Timed test code */
	uint8_t total_errors = 0;
	struct timespec start = {0};
	self->rtc->get(self->rtc->parent, &start);
	for (size_t page = 0; page < (dev->flash_size / dev->page_size); page++) {
		memset(buf, 0, sizeof(uint8_t) * dev->page_size);
		dev->flash->vmt->read(dev->flash, page * dev->page_size, buf, dev->page_size);
		uint32_t errors = 0;
		for (size_t i = 0; i < dev->page_size; i++) {
			if (buf[i] != pattern) {
				errors++;
			}
		}
		if (errors > 0) {
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("  page %u, errors %u"),
				page,
				errors
			);
			total_errors += errors;
		}
	}
	struct timespec end = {0};
	self->rtc->get(self->rtc->parent, &end);
	int32_t time_us = timespec_diff(&end, &start);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("  -> total time %u ms, page read time %u ms, speed %u KB/s, errors %u"),
		time_us / 1000,
		time_us / 1000 / (dev->flash_size / dev->page_size),
		dev->flash_size / (time_us / 1000) * 1000 / 1024,
		total_errors
	);
	if (total_errors > 0) {
		free(buf);
		return FLASH_TEST_RET_FAILED;
	}
	free(buf);
	return FLASH_TEST_RET_OK;
}


flash_test_ret_t flash_test_erase(FlashTest *self, struct flash_test_dev *dev, size_t size) {
	uint8_t *buf = malloc(sizeof(uint8_t) * dev->page_size);
	if (buf == NULL) {
		return FLASH_TEST_RET_FAILED;
	}
	memset(buf, 0, sizeof(uint8_t) * dev->page_size);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("TEST erase by %u B blocks"), size);

	struct timespec start = {0};
	self->rtc->get(self->rtc->parent, &start);
	for (size_t block = 0; block < (dev->flash_size / size); block++) {
		if (dev->flash->vmt->erase(dev->flash, block * size, size) != FLASH_RET_OK) {
			u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("erase block %u error"), block);
		}
	}
	struct timespec end = {0};
	self->rtc->get(self->rtc->parent, &end);
	int32_t time_us = timespec_diff(&end, &start);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("  -> total time %u ms, block erase time %u ms, speed %u KB/s"),
		time_us / 1000,
		time_us / 1000 / (dev->flash_size / size),
		dev->flash_size / (time_us / 1000) * 1000 / 1024
	);

	free(buf);
	return FLASH_TEST_RET_OK;
}



flash_test_ret_t flash_test_init(FlashTest *self, Clock *rtc) {
	if (u_assert(self != NULL)) {
		return FLASH_TEST_RET_FAILED;
	}
	memset(self, 0, sizeof(FlashTest));
	self->rtc = rtc;
	return FLASH_TEST_RET_OK;
}


flash_test_ret_t flash_test_free(FlashTest *self) {
	if (u_assert(self != NULL)) {
		return FLASH_TEST_RET_FAILED;
	}

	return FLASH_TEST_RET_OK;
}


flash_test_ret_t flash_test_dev(FlashTest *self, Flash *flash, const char *name) {
	if (u_assert(self != NULL)) {
		return FLASH_TEST_RET_FAILED;
	}

	struct flash_test_dev dev = {
		.name = name,
		.page_size = get_page_size(flash),
		.sector_size = get_erase_sector_size(flash),
		.block_size = get_erase_block_size(flash),
		.flash_size = get_flash_size(flash),
		.flash = flash,
	};

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("flash dev test suite '%s', size %u, page %u, sector %u, block %u"),
		dev.name,
		dev.flash_size,
		dev.page_size,
		dev.sector_size,
		dev.block_size
	);

	flash_test_write_pattern(self, &dev, 0x00);
	flash_test_verify_pattern(self, &dev, 0x00);

	flash_test_erase(self, &dev, dev.sector_size);
	flash_test_verify_pattern(self, &dev, 0xff);

	flash_test_write_pattern(self, &dev, 0x55);
	flash_test_verify_pattern(self, &dev, 0x55);

	flash_test_erase(self, &dev, dev.block_size);
	flash_test_verify_pattern(self, &dev, 0xff);

	flash_test_write_pattern(self, &dev, 0xaa);
	flash_test_verify_pattern(self, &dev, 0xaa);

	flash_test_erase(self, &dev, dev.flash_size);
	flash_test_verify_pattern(self, &dev, 0xff);

	return FLASH_TEST_RET_OK;
}


flash_test_ret_t flash_test_all(FlashTest *self) {

	return FLASH_TEST_RET_OK;
}
