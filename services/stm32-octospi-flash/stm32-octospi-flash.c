/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 OCTOSPI NOR flash memory driver (QSPI mode)
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>

#include <main.h>

#include <interfaces/flash.h>
#include <interfaces/clock.h>

#if defined(STM32U5)
	#include <stm32u5xx.h>
#else
	#error "stm32-octospi-flash service is not compatible with this MCU family"
#endif

#include "stm32-octospi-flash.h"

#define MODULE_NAME "stm32-octospi-flash"

/* Pollute the local namespace with some syntactic sugar */
#define NONE 0
#define SINGLE 1
#define DUAL 2
#define QUAD 3

#define ADSIZE_8BIT 0
#define ADSIZE_16BIT 1
#define ADSIZE_24BIT 2
#define ADSIZE_32BIT 3

/* Frame phase configuration fields of the CCR register. Unlike the QUADSPI
 * peripheral, the OCTOSPI keeps the instruction, the dummy cycles and the
 * functional mode in separate registers (IR, TCR and CR respectively). */
#define IMODE(x) ((x) << XSPI_CCR_IMODE_Pos)
#define ADMODE(x) ((x) << XSPI_CCR_ADMODE_Pos)
#define ABMODE(x) ((x) << XSPI_CCR_ABMODE_Pos)
#define DMODE(x) ((x) << XSPI_CCR_DMODE_Pos)
#define ADSIZE(x) ((x) << XSPI_CCR_ADSIZE_Pos)
#define DCYC(x) ((x) << XSPI_TCR_DCYC_Pos)

/* Functional mode selection in the CR register. */
#define READI (1UL << XSPI_CR_FMODE_Pos)
#define WRITEI (0UL << XSPI_CR_FMODE_Pos)


/* Module configuration */
#ifdef CONFIG_SERVICE_STM32_OCTOSPI_FLASH_QSPI_BUSY_TIMEOUT
#define QSPI_BUSY_TIMEOUT pdMS_TO_TICKS(CONFIG_SERVICE_STM32_OCTOSPI_FLASH_QSPI_BUSY_TIMEOUT)
#else
#define QSPI_BUSY_TIMEOUT pdMS_TO_TICKS(1000)
#endif

#ifdef CONFIG_SERVICE_STM32_OCTOSPI_FLASH_MEM_BUSY_TIMEOUT
#define MEM_BUSY_TIMEOUT pdMS_TO_TICKS(CONFIG_SERVICE_STM32_OCTOSPI_FLASH_MEM_BUSY_TIMEOUT)
#else
/* Full chip erase takes several seconds. */
#define MEM_BUSY_TIMEOUT pdMS_TO_TICKS(20000)
#endif

#ifdef CONFIG_SERVICE_STM32_OCTOSPI_FLASH_READ_TIMEOUT
#define READ_TIMEOUT pdMS_TO_TICKS(CONFIG_SERVICE_STM32_OCTOSPI_FLASH_READ_TIMEOUT)
#else
#define READ_TIMEOUT pdMS_TO_TICKS(1000)
#endif


static const struct stm32_octospi_flash_info ids[] = {
	/* {id, manufacturer, type, size, block_size, sector_size, page_size} (all sizes as log2 bytes) */
	{0xef6017, "Winbond", "w25q64dw", 23, 16, 12, 8},
	{0x016017, "Cypress", "s25fl064l", 23, 16, 12, 8},
	{0x014014, "Spansion", "s25fl208k", 20, 16, 12, 8},
	{0x009d70, "ISSI", "is25xp016d", 21, 16, 12, 8},
	{0x1f8701, "Adesto", "at25sf321", 22, 16, 12, 8},
	{0x1f8501, "Adesto", "at25sf081", 20, 16, 12, 8},
	{0}
};


/* Program a regular-command frame into the OCTOSPI registers. The transaction
 * is triggered by the final register write: writing the instruction register
 * (IR) starts an instruction-only frame, while frames with an address phase
 * start once the caller writes the address register (AR). */
static void command(Stm32OctospiFlash *self, uint32_t fmode, uint32_t ccr, uint8_t inst, uint32_t tcr) {
	XSPI_TypeDef *base = (XSPI_TypeDef *)self->mmio_base;

	base->CR = (base->CR & ~XSPI_CR_FMODE_Msk) | fmode;
	base->TCR = tcr;
	base->CCR = ccr;
	base->IR = inst;
}


static stm32_octospi_flash_ret_t read(Stm32OctospiFlash *self, uint8_t *buf, size_t size, size_t *len) {
	XSPI_TypeDef *base = (XSPI_TypeDef *)self->mmio_base;

	uint32_t status;
	size_t l = 0;
	volatile uint8_t *pdr = (volatile uint8_t *)&base->DR;

	TickType_t tm_st = xTaskGetTickCount();
	do {
		status = base->SR;
		if (status & (XSPI_SR_FTF | XSPI_SR_TCF)) {
			*buf = *pdr;
			buf++;
			l++;
			if (l >= size) {
				break;
			}
		}
		if ((xTaskGetTickCount() - tm_st) > READ_TIMEOUT) {
			return STM32_OCTOSPI_FLASH_RET_TIMEOUT;
		}
	} while (status & XSPI_SR_BUSY);
	if (len != NULL) {
		*len = l;
	}
	base->FCR |= XSPI_FCR_CTCF;

	return STM32_OCTOSPI_FLASH_RET_OK;
}


static stm32_octospi_flash_ret_t wait_qspi_busy(Stm32OctospiFlash *self) {
	XSPI_TypeDef *base = (XSPI_TypeDef *)self->mmio_base;

	TickType_t tm_st = xTaskGetTickCount();
	while (base->SR & XSPI_SR_BUSY) {
		if ((xTaskGetTickCount() - tm_st) > QSPI_BUSY_TIMEOUT) {
			return STM32_OCTOSPI_FLASH_RET_TIMEOUT;
		}
	}
	base->FCR |= XSPI_FCR_CTCF;

	return STM32_OCTOSPI_FLASH_RET_OK;
}


static enum stm32_octospi_flash_status read_mem_status(Stm32OctospiFlash *self, bool status2) {
	XSPI_TypeDef *base = (XSPI_TypeDef *)self->mmio_base;

	base->DLR = 0;
	if (status2) {
		command(self, READI, IMODE(SINGLE) | DMODE(SINGLE), 0x35, 0);
	} else {
		command(self, READI, IMODE(SINGLE) | DMODE(SINGLE), 0x05, 0);
	}
	uint8_t status = 0;
	size_t len = 0;
	if (read(self, (void *)&status, sizeof(status), &len) != STM32_OCTOSPI_FLASH_RET_OK || len != 1) {
		return 0;
	}
	if (status2) {
		return status << 8;
	} else {
		return status;
	}
}


static stm32_octospi_flash_ret_t wait_mem_busy(Stm32OctospiFlash *self) {
	TickType_t tm_st = xTaskGetTickCount();
	while (read_mem_status(self, false) & STM32_OCTOSPI_FLASH_STATUS_BUSY) {
		if ((xTaskGetTickCount() - tm_st) > MEM_BUSY_TIMEOUT) {
			return STM32_OCTOSPI_FLASH_RET_TIMEOUT;
		}
		vTaskDelay(2);
	}
	return STM32_OCTOSPI_FLASH_RET_OK;
}


static void reset(Stm32OctospiFlash *self) {
	command(self, WRITEI, IMODE(SINGLE), 0x66, 0);
	wait_qspi_busy(self);
	command(self, WRITEI, IMODE(SINGLE), 0x99, 0);
	wait_qspi_busy(self);
}


stm32_octospi_flash_ret_t stm32_octospi_flash_read_id(Stm32OctospiFlash *self, uint32_t *id) {
	XSPI_TypeDef *base = (XSPI_TypeDef *)self->mmio_base;
	base->DLR = 3 - 1;
	command(self, READI, DMODE(SINGLE) | IMODE(SINGLE), 0x9f, 0);
	size_t len = 0;
	uint8_t buf[3] = {0};
	if (read(self, (void *)buf, sizeof(buf), &len) != STM32_OCTOSPI_FLASH_RET_OK || len != 3) {
		return STM32_OCTOSPI_FLASH_RET_FAILED;
	}
	if (id) {
		*id = (buf[0] << 16) | (buf[1] << 8) | buf[2];
	}
	return STM32_OCTOSPI_FLASH_RET_OK;
}


stm32_octospi_flash_ret_t stm32_octospi_flash_read_winbond_uniq(Stm32OctospiFlash *self, uint8_t *uniq) {
	XSPI_TypeDef *base = (XSPI_TypeDef *)self->mmio_base;
	/* Cannot generate 32 dummy cycles. Use empty 32bit address instead. */
	base->DLR = 8 - 1;
	command(self, READI, DMODE(SINGLE) | IMODE(SINGLE) | ADMODE(SINGLE) | ADSIZE(ADSIZE_32BIT), 0x4b, 0);
	base->AR = 0;
	size_t len = 0;
	if (read(self, (void *)uniq, 8, &len) != STM32_OCTOSPI_FLASH_RET_OK || len != 8) {
		return STM32_OCTOSPI_FLASH_RET_FAILED;
	}
	return STM32_OCTOSPI_FLASH_RET_OK;
}


stm32_octospi_flash_ret_t stm32_octospi_flash_write_enable(Stm32OctospiFlash *self, bool e) {
	if (e) {
		command(self, WRITEI, IMODE(SINGLE), 0x06, 0);
	} else {
		command(self, WRITEI, IMODE(SINGLE), 0x04, 0);
	}
	if (wait_qspi_busy(self) != STM32_OCTOSPI_FLASH_RET_OK) {
		return STM32_OCTOSPI_FLASH_RET_FAILED;
	}
	return STM32_OCTOSPI_FLASH_RET_OK;
}


static stm32_octospi_flash_ret_t write(Stm32OctospiFlash *self, const uint8_t *buf, size_t len) {
	XSPI_TypeDef *base = (XSPI_TypeDef *)self->mmio_base;
	volatile uint8_t *pdr = (volatile uint8_t *)&base->DR;
	while (len > 0) {
		if (base->SR & XSPI_SR_FTF) {
			*pdr = *buf;
			buf++;
			len--;
		}
	}

	return wait_qspi_busy(self);
}


stm32_octospi_flash_ret_t stm32_octospi_flash_read_page(Stm32OctospiFlash *self, size_t addr, void *buf, size_t size) {
	if (u_assert(buf != NULL) ||
	    u_assert(size <= (1UL << self->info->page_size))) {
		return STM32_OCTOSPI_FLASH_RET_FAILED;
	}

	XSPI_TypeDef *base = (XSPI_TypeDef *)self->mmio_base;
	base->DLR = size - 1;
	command(self, READI, DMODE(SINGLE) | IMODE(SINGLE) | ADMODE(SINGLE) | ADSIZE(ADSIZE_24BIT), 0x0b, DCYC(8));
	base->AR = addr;
	size_t len = 0;
	if (read(self, buf, size, &len) != STM32_OCTOSPI_FLASH_RET_OK || len != size) {
		return STM32_OCTOSPI_FLASH_RET_FAILED;
	}
	return STM32_OCTOSPI_FLASH_RET_OK;
}


stm32_octospi_flash_ret_t stm32_octospi_flash_read_page_fast_q(Stm32OctospiFlash *self, size_t addr, void *buf, size_t size) {
	if (u_assert(buf != NULL) ||
	    u_assert(size <= (1UL << self->info->page_size))) {
		return STM32_OCTOSPI_FLASH_RET_FAILED;
	}

	XSPI_TypeDef *base = (XSPI_TypeDef *)self->mmio_base;
	base->DLR = size - 1;
	command(self, READI, DMODE(QUAD) | IMODE(SINGLE) | ADMODE(QUAD) | ADSIZE(ADSIZE_24BIT), 0xeb, DCYC(6));
	base->AR = addr;
	size_t len = 0;
	if (read(self, buf, size, &len) != STM32_OCTOSPI_FLASH_RET_OK || len != size) {
		return STM32_OCTOSPI_FLASH_RET_FAILED;
	}
	return STM32_OCTOSPI_FLASH_RET_OK;
}


stm32_octospi_flash_ret_t stm32_octospi_flash_write_page(Stm32OctospiFlash *self, size_t addr, const void *buf, size_t size) {
	if (u_assert(size <= (1UL << self->info->page_size)) ||
	    u_assert((addr + size) << (1UL << self->info->size)) ||
	    u_assert(buf != NULL)) {
		return STM32_OCTOSPI_FLASH_RET_FAILED;
	}

	XSPI_TypeDef *base = (XSPI_TypeDef *)self->mmio_base;
	base->DLR = size - 1;
	command(self, WRITEI, DMODE(SINGLE) | IMODE(SINGLE) | ADMODE(SINGLE) | ADSIZE(ADSIZE_24BIT), 0x02, 0);
	base->AR = addr;
	if (write(self, buf, size) != STM32_OCTOSPI_FLASH_RET_OK) {
		return STM32_OCTOSPI_FLASH_RET_FAILED;
	}
	if (wait_qspi_busy(self) != STM32_OCTOSPI_FLASH_RET_OK) {
		return STM32_OCTOSPI_FLASH_RET_FAILED;
	}
	if (wait_mem_busy(self) != STM32_OCTOSPI_FLASH_RET_OK) {
		return STM32_OCTOSPI_FLASH_RET_FAILED;
	}

	return STM32_OCTOSPI_FLASH_RET_OK;
}


stm32_octospi_flash_ret_t stm32_octospi_flash_erase_sector(Stm32OctospiFlash *self, size_t addr) {
	if (u_assert(self != NULL) ||
	    u_assert((addr % (1 << self->info->sector_size)) == 0)) {
		return STM32_OCTOSPI_FLASH_RET_FAILED;
	}

	XSPI_TypeDef *base = (XSPI_TypeDef *)self->mmio_base;
	command(self, WRITEI, IMODE(SINGLE) | ADMODE(SINGLE) | ADSIZE(ADSIZE_24BIT), 0x20, 0);
	base->AR = addr;
	if (wait_qspi_busy(self) != STM32_OCTOSPI_FLASH_RET_OK) {
		return STM32_OCTOSPI_FLASH_RET_FAILED;
	}
	if (wait_mem_busy(self) != STM32_OCTOSPI_FLASH_RET_OK) {
		return STM32_OCTOSPI_FLASH_RET_FAILED;
	}
	return STM32_OCTOSPI_FLASH_RET_OK;
}


stm32_octospi_flash_ret_t stm32_octospi_flash_erase_block(Stm32OctospiFlash *self, size_t addr) {
	if (u_assert(self != NULL) ||
	    u_assert((addr % (1 << self->info->block_size)) == 0)) {
		return STM32_OCTOSPI_FLASH_RET_FAILED;
	}

	XSPI_TypeDef *base = (XSPI_TypeDef *)self->mmio_base;
	command(self, WRITEI, IMODE(SINGLE) | ADMODE(SINGLE) | ADSIZE(ADSIZE_24BIT), 0xd8, 0);
	base->AR = addr;
	if (wait_qspi_busy(self) != STM32_OCTOSPI_FLASH_RET_OK) {
		return STM32_OCTOSPI_FLASH_RET_FAILED;
	}
	if (wait_mem_busy(self) != STM32_OCTOSPI_FLASH_RET_OK) {
		return STM32_OCTOSPI_FLASH_RET_FAILED;
	}
	return STM32_OCTOSPI_FLASH_RET_OK;
}


stm32_octospi_flash_ret_t stm32_octospi_flash_erase_chip(Stm32OctospiFlash *self) {
	if (u_assert(self != NULL)) {
		return STM32_OCTOSPI_FLASH_RET_FAILED;
	}

	command(self, WRITEI, IMODE(SINGLE), 0xc7, 0);
	if (wait_qspi_busy(self) != STM32_OCTOSPI_FLASH_RET_OK) {
		return STM32_OCTOSPI_FLASH_RET_FAILED;
	}
	if (wait_mem_busy(self) != STM32_OCTOSPI_FLASH_RET_OK) {
		return STM32_OCTOSPI_FLASH_RET_FAILED;
	}
	return STM32_OCTOSPI_FLASH_RET_OK;
}




/***************************************************************************************************
 * Flash interface API
 ***************************************************************************************************/

static flash_ret_t flash_get_size(Flash *self, uint32_t i, size_t *size, flash_block_ops_t *ops) {
	Stm32OctospiFlash *qspi = (Stm32OctospiFlash *)self->parent;

	switch (i) {
		case 0:
			*size = 1UL << qspi->info->size;
			*ops = FLASH_BLOCK_OPS_ERASE;
			break;
		case 1:
			*size = 1UL << qspi->info->block_size;
			*ops = FLASH_BLOCK_OPS_ERASE;
			break;
		case 2:
			*size = 1UL << qspi->info->sector_size;
			*ops = FLASH_BLOCK_OPS_ERASE;
			break;
		case 3:
			*size = 1UL << qspi->info->page_size;
			*ops = FLASH_BLOCK_OPS_READ | FLASH_BLOCK_OPS_WRITE;
			break;
		default:
			/** @todo fix FLASH_RET_BAD_ARG */
			return FLASH_RET_FAILED;
	}

	return FLASH_RET_OK;
}


static flash_ret_t flash_erase(Flash *self, const size_t addr, size_t len) {
	Stm32OctospiFlash *qspi = (Stm32OctospiFlash *)self->parent;
	xSemaphoreTake(qspi->lock, portMAX_DELAY);

	if (stm32_octospi_flash_write_enable(qspi, true) != STM32_OCTOSPI_FLASH_RET_OK) {
		xSemaphoreGive(qspi->lock);
		return FLASH_RET_FAILED;
	}

	/* For now, the size parameter must be the exact size of a sector,
	 * a block or a full chip and the address must be properly aligned. */
	if (len == (1UL << qspi->info->size) && addr == 0) {
		if (stm32_octospi_flash_erase_chip(qspi) == STM32_OCTOSPI_FLASH_RET_OK) {
			xSemaphoreGive(qspi->lock);
			return FLASH_RET_OK;
		}
	} else if (len == (1UL << qspi->info->block_size) && ((addr % (1UL << qspi->info->block_size)) == 0)) {
		if (stm32_octospi_flash_erase_block(qspi, addr) == STM32_OCTOSPI_FLASH_RET_OK) {
			xSemaphoreGive(qspi->lock);
			return FLASH_RET_OK;
		}
	} else if (len == (1UL << qspi->info->sector_size) && ((addr % (1UL << qspi->info->sector_size)) == 0)) {
		if (stm32_octospi_flash_erase_sector(qspi, addr) == STM32_OCTOSPI_FLASH_RET_OK) {
			xSemaphoreGive(qspi->lock);
			return FLASH_RET_OK;
		}
	}

	xSemaphoreGive(qspi->lock);
	return FLASH_RET_FAILED;
}


static flash_ret_t flash_write(Flash *self, const size_t addr, const void *buf, size_t len) {
	Stm32OctospiFlash *qspi = (Stm32OctospiFlash *)self->parent;
	xSemaphoreTake(qspi->lock, portMAX_DELAY);

	if (stm32_octospi_flash_write_enable(qspi, true) != STM32_OCTOSPI_FLASH_RET_OK) {
		xSemaphoreGive(qspi->lock);
		return FLASH_RET_FAILED;
	}
	if (stm32_octospi_flash_write_page(qspi, addr, buf, len) != STM32_OCTOSPI_FLASH_RET_OK) {
		xSemaphoreGive(qspi->lock);
		return FLASH_RET_FAILED;
	}

	xSemaphoreGive(qspi->lock);
	return FLASH_RET_OK;
}


static flash_ret_t flash_read(Flash *self, const size_t addr, void *buf, size_t len) {
	Stm32OctospiFlash *qspi = (Stm32OctospiFlash *)self->parent;
	xSemaphoreTake(qspi->lock, portMAX_DELAY);

	if (stm32_octospi_flash_read_page(qspi, addr, buf, len) != STM32_OCTOSPI_FLASH_RET_OK) {
		xSemaphoreGive(qspi->lock);
		return FLASH_RET_FAILED;
	}

	xSemaphoreGive(qspi->lock);
	return FLASH_RET_OK;
}


static const struct flash_vmt iface_vmt = {
	.get_size = flash_get_size,
	.erase = flash_erase,
	.write = flash_write,
	.read = flash_read,
};


stm32_octospi_flash_ret_t stm32_octospi_flash_qspi_init(Stm32OctospiFlash *self, void *mmio_base) {
	if (u_assert(self != NULL)) {
		return STM32_OCTOSPI_FLASH_RET_FAILED;
	}
	memset(self, 0, sizeof(Stm32OctospiFlash));
	self->mmio_base = mmio_base;
	XSPI_TypeDef *base = (XSPI_TypeDef *)self->mmio_base;

	/* Enable the peripheral at a super low speed. Can be changed later. The
	 * clock prescaler lives in DCR2 on the OCTOSPI, not in the CR register. */
	base->DCR2 = (31ul << XSPI_DCR2_PRESCALER_Pos);
	base->CR = XSPI_CR_EN;

	reset(self);

	self->lock = xSemaphoreCreateMutex();
	if (self->lock == NULL) {
		goto err;
	}

	/* The size is unknown until we read ID */
	base->DCR1 = (10 << XSPI_DCR1_DEVSIZE_Pos);
	uint32_t id = 0;
	if (stm32_octospi_flash_read_id(self, &id) != STM32_OCTOSPI_FLASH_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("error while reading OCTOSPI flash id"), id);
		goto err;
	}

	/* Try to match the ID with a record from the table of known IDs */
	size_t i = 0;
	while (ids[i].id != 0 && ids[i].id != id) {
		i++;
	}
	if (id == 0 || ids[i].id != id) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("couldn't detect OCTOSPI flash (id = 0x%06x)"), id);
		goto err;
	}
	self->info = &ids[i];

	/* Now the flash parameters are known, set the correct flash size */
	base->DCR1 = ((self->info->size - 1) << XSPI_DCR1_DEVSIZE_Pos);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("detected '%s %s' (id = 0x%06x)"),
		self->info->manufacturer,
		self->info->type,
		self->info->id
	);
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("flash size = %u MB, erase block = %u KB, sector = %u KB, page = %u B"),
		1 << (self->info->size - 20),
		1 << (self->info->block_size - 10),
		1 << (self->info->sector_size - 10),
		1 << (self->info->page_size)
	);

	if ((self->info->id & 0xff0000) == 0xef0000) {
		uint8_t uniq[8] = {0};
		stm32_octospi_flash_read_winbond_uniq(self, uniq);
		/* Wow. */
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("Winbond unique ID = %02x%02x %02x%02x %02x%02x %02x%02x"),
			uniq[0], uniq[1], uniq[2], uniq[3], uniq[4], uniq[5], uniq[6], uniq[7]
		);
	}

	/* Prepare the interface */
	self->iface.parent = (void *)self;
	self->iface.vmt = &iface_vmt;

	return STM32_OCTOSPI_FLASH_RET_OK;

err:
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("initialization failed"));
	return STM32_OCTOSPI_FLASH_RET_FAILED;
}


stm32_octospi_flash_ret_t stm32_octospi_flash_free(Stm32OctospiFlash *self) {
	if (u_assert(self != NULL)) {
		return STM32_OCTOSPI_FLASH_RET_FAILED;
	}

	if (self->lock != NULL) {
		vSemaphoreDelete(self->lock);
	}

	return STM32_OCTOSPI_FLASH_RET_OK;
}


stm32_octospi_flash_ret_t stm32_octospi_flash_set_prescaler(Stm32OctospiFlash *self, uint32_t prescaler) {
	if (u_assert(self != NULL)) {
		return STM32_OCTOSPI_FLASH_RET_FAILED;
	}

	XSPI_TypeDef *base = (XSPI_TypeDef *)self->mmio_base;
	wait_qspi_busy(self);
	base->DCR2 = (base->DCR2 & ~XSPI_DCR2_PRESCALER_Msk) | ((prescaler & 0xff) << XSPI_DCR2_PRESCALER_Pos);

	return STM32_OCTOSPI_FLASH_RET_OK;
}
