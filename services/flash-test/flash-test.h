/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Flash memory test suite
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "config.h"

#include <interfaces/flash.h>
#include <interfaces/clock.h>
#include <interfaces/servicelocator.h>


typedef enum {
	FLASH_TEST_RET_OK = 0,
	FLASH_TEST_RET_FAILED,
} flash_test_ret_t;

struct flash_test_dev {
	const char *name;
	size_t flash_size;
	size_t block_size;
	size_t sector_size;
	size_t page_size;
	Flash *flash;
};

typedef struct {
	Clock *rtc;

} FlashTest;


flash_test_ret_t flash_test_init(FlashTest *self, Clock *rtc);
flash_test_ret_t flash_test_free(FlashTest *self);
flash_test_ret_t flash_test_dev(FlashTest *self, Flash *flash, const char *name);
flash_test_ret_t flash_test_all(FlashTest *self);
flash_test_ret_t flash_test_write_pattern(FlashTest *self, struct flash_test_dev *dev, uint8_t pattern);
flash_test_ret_t flash_test_verify_pattern(FlashTest *self, struct flash_test_dev *dev, uint8_t pattern);
flash_test_ret_t flash_test_erase(FlashTest *self, struct flash_test_dev *dev, size_t size);
