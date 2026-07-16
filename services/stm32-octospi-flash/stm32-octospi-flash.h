/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 OCTOSPI NOR flash memory driver (QSPI mode)
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <main.h>

#include <interfaces/flash.h>


typedef enum {
	STM32_OCTOSPI_FLASH_RET_OK = 0,
	STM32_OCTOSPI_FLASH_RET_FAILED,
	STM32_OCTOSPI_FLASH_RET_TIMEOUT,
} stm32_octospi_flash_ret_t;

struct stm32_octospi_flash_info {
	uint32_t id;
	const char *manufacturer;
	const char *type;
	uint8_t size;
	uint8_t block_size;
	uint8_t sector_size;
	uint8_t page_size;
};

enum stm32_octospi_flash_status {
	STM32_OCTOSPI_FLASH_STATUS_QE = (1 << 9),
	STM32_OCTOSPI_FLASH_STATUS_SRP1 = (1 << 8),

	STM32_OCTOSPI_FLASH_STATUS_SRP0 = (1 << 7),
	STM32_OCTOSPI_FLASH_STATUS_SEC = (1 << 6),
	STM32_OCTOSPI_FLASH_STATUS_TB = (1 << 5),
	STM32_OCTOSPI_FLASH_STATUS_BP2 = (1 << 4),
	STM32_OCTOSPI_FLASH_STATUS_BP1 = (1 << 3),
	STM32_OCTOSPI_FLASH_STATUS_BP0 = (1 << 2),
	STM32_OCTOSPI_FLASH_STATUS_WEL = (1 << 1),
	STM32_OCTOSPI_FLASH_STATUS_BUSY = (1 << 0),
};

typedef struct {
	Flash iface;
	void *mmio_base;
	SemaphoreHandle_t lock;
	const struct stm32_octospi_flash_info *info;
} Stm32OctospiFlash;


/**
 * @brief Initialise the OCTOSPI peripheral in a single/quad SPI (QSPI) mode
 *
 * The OCTOSPI peripheral is a superset of the older QUADSPI. This driver drives
 * it as a regular-command indirect-mode QSPI controller for a NOR flash memory.
 * The caller is responsible for enabling the OCTOSPI and OCTOSPI I/O manager
 * clocks, configuring the pins and routing the peripheral to the proper I/O
 * manager port prior to calling this function.
 *
 * @param self OCTOSPI flash instance
 * @param mmio_base Base address of the OCTOSPI peripheral control registers
 */
stm32_octospi_flash_ret_t stm32_octospi_flash_qspi_init(Stm32OctospiFlash *self, void *mmio_base);
stm32_octospi_flash_ret_t stm32_octospi_flash_free(Stm32OctospiFlash *self);
stm32_octospi_flash_ret_t stm32_octospi_flash_set_prescaler(Stm32OctospiFlash *self, uint32_t prescaler);

stm32_octospi_flash_ret_t stm32_octospi_flash_read_id(Stm32OctospiFlash *self, uint32_t *id);
stm32_octospi_flash_ret_t stm32_octospi_flash_read_winbond_uniq(Stm32OctospiFlash *self, uint8_t *uniq);
stm32_octospi_flash_ret_t stm32_octospi_flash_write_enable(Stm32OctospiFlash *self, bool e);
stm32_octospi_flash_ret_t stm32_octospi_flash_read_page(Stm32OctospiFlash *self, size_t addr, void *buf, size_t size);
stm32_octospi_flash_ret_t stm32_octospi_flash_read_page_fast_q(Stm32OctospiFlash *self, size_t addr, void *buf, size_t size);
stm32_octospi_flash_ret_t stm32_octospi_flash_write_page(Stm32OctospiFlash *self, size_t addr, const void *buf, size_t size);
stm32_octospi_flash_ret_t stm32_octospi_flash_erase_sector(Stm32OctospiFlash *self, size_t addr);
stm32_octospi_flash_ret_t stm32_octospi_flash_erase_block(Stm32OctospiFlash *self, size_t addr);
stm32_octospi_flash_ret_t stm32_octospi_flash_erase_chip(Stm32OctospiFlash *self);
