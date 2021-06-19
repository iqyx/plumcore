/* SPDX-License-Identifier: BSD-2-Clause
 *
 * STM32 QSPI flash memory driver
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <main.h>

#include <interfaces/flash.h>


typedef enum {
	STM32_QSPI_FLASH_RET_OK = 0,
	STM32_QSPI_FLASH_RET_FAILED,
	STM32_QSPI_FLASH_RET_TIMEOUT,
} stm32_qspi_flash_ret_t;

struct stm32_qspi_flash_info {
	uint32_t id;
	const char *manufacturer;
	const char *type;
	uint8_t size;
	uint8_t block_size;
	uint8_t sector_size;
	uint8_t page_size;
};

enum stm32_qspi_flash_status {
	STM32_QSPI_FLASH_STATUS_QE = (1 << 9),
	STM32_QSPI_FLASH_STATUS_SRP1 = (1 << 8),

	STM32_QSPI_FLASH_STATUS_SRP0 = (1 << 7),
	STM32_QSPI_FLASH_STATUS_SEC = (1 << 6),
	STM32_QSPI_FLASH_STATUS_TB = (1 << 5),
	STM32_QSPI_FLASH_STATUS_BP2 = (1 << 4),
	STM32_QSPI_FLASH_STATUS_BP1 = (1 << 3),
	STM32_QSPI_FLASH_STATUS_BP0 = (1 << 2),
	STM32_QSPI_FLASH_STATUS_WEL = (1 << 1),
	STM32_QSPI_FLASH_STATUS_BUSY = (1 << 0),
};

typedef struct {
	Flash iface;
	SemaphoreHandle_t lock;
	const struct stm32_qspi_flash_info *info;
} Stm32QspiFlash;


stm32_qspi_flash_ret_t stm32_qspi_flash_init(Stm32QspiFlash *self);
stm32_qspi_flash_ret_t stm32_qspi_flash_free(Stm32QspiFlash *self);
stm32_qspi_flash_ret_t stm32_qspi_flash_set_prescaler(Stm32QspiFlash *self, uint32_t prescaler);

stm32_qspi_flash_ret_t stm32_qspi_flash_read_id(Stm32QspiFlash *self, uint32_t *id);
stm32_qspi_flash_ret_t stm32_qspi_flash_read_winbond_uniq(Stm32QspiFlash *self, uint8_t *uniq);
stm32_qspi_flash_ret_t stm32_qspi_flash_write_enable(Stm32QspiFlash *self, bool e);
stm32_qspi_flash_ret_t stm32_qspi_flash_read_page(Stm32QspiFlash *self, size_t addr, void *buf, size_t size);
stm32_qspi_flash_ret_t stm32_qspi_flash_read_page_fast_q(Stm32QspiFlash *self, size_t addr, void *buf, size_t size);
stm32_qspi_flash_ret_t stm32_qspi_flash_write_page(Stm32QspiFlash *self, size_t addr, const void *buf, size_t size);
stm32_qspi_flash_ret_t stm32_qspi_flash_erase_sector(Stm32QspiFlash *self, size_t addr);
stm32_qspi_flash_ret_t stm32_qspi_flash_erase_block(Stm32QspiFlash *self, size_t addr);
stm32_qspi_flash_ret_t stm32_qspi_flash_erase_chip(Stm32QspiFlash *self);
