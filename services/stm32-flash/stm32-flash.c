/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Driver for STM32 internal flash memory
 *
 * Copyright (c) 2022, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <main.h>
#include <libopencm3/cm3/cortex.h>
#include <libopencm3/stm32/flash.h>
#include <interfaces/flash.h>
#include "stm32-flash.h"

#define MODULE_NAME "stm32-flash"


/***************************************************************************************************
 * Flash interface implementation
 ***************************************************************************************************/

static flash_ret_t stm32_flash_get_size(Flash *flash, uint32_t i, size_t *size, flash_block_ops_t *ops) {
	Stm32Flash *self = flash->parent;

	switch (i) {
		case 0:
			*size = STM32_FLASH_SIZE;
			*ops = FLASH_BLOCK_OPS_ERASE;
			break;
		case 1:
			*size = STM32_FLASH_SIZE;
			*ops = FLASH_BLOCK_OPS_ERASE;
			break;
		case 2:
			*size = STM32_FLASH_SECTOR_SIZE;
			*ops = FLASH_BLOCK_OPS_ERASE;
			break;
		case 3:
			*size = STM32_FLASH_PAGE_SIZE;
			*ops = FLASH_BLOCK_OPS_READ | FLASH_BLOCK_OPS_WRITE;
			break;
		default:
			return FLASH_RET_BAD_ARG;
	}
	return FLASH_RET_OK;
}


static flash_ret_t stm32_flash_erase(Flash *flash, const size_t addr, size_t len) {
	Stm32Flash *self = flash->parent;

	#if defined(STM32G4)
		/* Always perform erase in sectors. */
		if (addr % STM32_FLASH_SECTOR_SIZE) {
			return FLASH_RET_FAILED;
		}
		if (len % STM32_FLASH_SECTOR_SIZE) {
			return FLASH_RET_FAILED;
		}
		cm_disable_interrupts();
		flash_unlock();
		for (size_t i = 0; i < (len / STM32_FLASH_SECTOR_SIZE); i++) {
			size_t sector = i + (addr / STM32_FLASH_SECTOR_SIZE);
			/* Beware, wrong naming. */
			flash_erase_page(sector);
		}
		flash_lock();
		cm_enable_interrupts();
	#else
		return FLASH_RET_FAILED;
	#endif

	return FLASH_RET_OK;
}


static flash_ret_t stm32_flash_write(Flash *flash, const size_t addr, const void *buf, size_t len) {
	Stm32Flash *self = flash->parent;

	#if defined(STM32G4)
		/* Always perform write in doublewords. */
		if (addr % STM32_FLASH_PAGE_SIZE) {
			return FLASH_RET_FAILED;
		}
		if (len % STM32_FLASH_PAGE_SIZE) {
			return FLASH_RET_FAILED;
		}

		cm_disable_interrupts();
		flash_unlock();
		for (size_t i = 0; i < len; i += 8) {
			flash_wait_for_last_operation();
			FLASH_CR |= FLASH_CR_PG;
			MMIO32(STM32_FLASH_BASE + addr + i) = *(const uint32_t *)((const uint8_t *)buf + i);
			MMIO32(STM32_FLASH_BASE + addr + i + 4) = *(const uint32_t *)((const uint8_t *)buf + i + 4);
			flash_wait_for_last_operation();
			FLASH_CR &= ~FLASH_CR_PG;
		}
		flash_lock();
		cm_enable_interrupts();
	#else
		return FLASH_RET_FAILED;
	#endif

	return FLASH_RET_OK;
}


static flash_ret_t stm32_flash_read(Flash *flash, const size_t addr, void *buf, size_t len) {
	Stm32Flash *self = flash->parent;

	/* We are not limited in reading, we do not have to respect page/sector sizes.
	 * Do what is requested unless it overflows past the end of the flash. */
	if ((addr + len) > STM32_FLASH_SIZE) {
		return FLASH_RET_BAD_ARG;
	}

	memcpy(buf, (uint8_t *)(STM32_FLASH_BASE + addr), len);

	return FLASH_RET_OK;
}


static const struct flash_vmt stm32_flash_vmt = {
	.get_size = stm32_flash_get_size,
	.erase = stm32_flash_erase,
	.write = stm32_flash_write,
	.read = stm32_flash_read,
};


stm32_flash_ret_t stm32_flash_init(Stm32Flash *self) {
	memset(self, 0, sizeof(Stm32Flash));

	self->flash.parent = self;
	self->flash.vmt = &stm32_flash_vmt;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("MCU = %s, flash size = %lu B, sector = %lu B, page = %lu B"),
		CONFIG_MCU_FAMILY,
		STM32_FLASH_SIZE,
		STM32_FLASH_SECTOR_SIZE,
		STM32_FLASH_PAGE_SIZE
	);

	return STM32_FLASH_RET_OK;
}


stm32_flash_ret_t stm32_flash_free(Stm32Flash *self) {

	return STM32_FLASH_RET_OK;
}



