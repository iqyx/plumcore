/* SPDX-License-Identifier: BSD-2-Clause
 *
 * STM32 QSPI flash memory driver
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
#include <libopencm3/stm32/quadspi.h>

#include "stm32-qspi-flash.h"

#define MODULE_NAME "stm32-qspi-flash"

/* Pollute the local namespace with some syntactic sugar */
#define NONE 0
#define SINGLE 1
#define DUAL 2
#define QUAD 3

#define ADSIZE_8BIT 0
#define ADSIZE_16BIT 1
#define ADSIZE_24BIT 2
#define ADSIZE_32BIT 3

#define DMODE(x) (x << QUADSPI_CCR_DMODE_SHIFT)
#define ADMODE(x) (x << QUADSPI_CCR_ADMODE_SHIFT)
#define ABMODE(x) (x << QUADSPI_CCR_ABMODE_SHIFT)
#define IMODE(x) (x << QUADSPI_CCR_IMODE_SHIFT)
#define DCYC(x) (x << QUADSPI_CCR_DCYC_SHIFT)
#define INST(x) (x << QUADSPI_CCR_INST_SHIFT)
#define ADSIZE(x) (x << QUADSPI_CCR_ADSIZE_SHIFT)

#define READI (1 << QUADSPI_CCR_FMODE_SHIFT)
#define WRITEI (0 << QUADSPI_CCR_FMODE_SHIFT)


/* Module configuration */
#ifdef CONFIG_SERVICE_STM32_QSPI_FLASH_QSPI_BUSY_TIMEOUT
#define QSPI_BUSY_TIMEOUT pdMS_TO_TICKS(CONFIG_SERVICE_STM32_QSPI_FLASH_QSPI_BUSY_TIMEOUT)
#else
#define QSPI_BUSY_TIMEOUT pdMS_TO_TICKS(1000)
#endif

#ifdef CONFIG_SERVICE_STM32_QSPI_FLASH_MEM_BUSY_TIMEOUT
#define MEM_BUSY_TIMEOUT pdMS_TO_TICKS(CONFIG_SERVICE_STM32_QSPI_FLASH_MEM_BUSY_TIMEOUT)
#else
/* Full chip erase takes several seconds. */
#define MEM_BUSY_TIMEOUT pdMS_TO_TICKS(20000)
#endif

#ifdef CONFIG_SERVICE_STM32_QSPI_FLASH_READ_TIMEOUT
#define READ_TIMEOUT pdMS_TO_TICKS(CONFIG_SERVICE_STM32_QSPI_FLASH_READ_TIMEOUT)
#else
#define READ_TIMEOUT pdMS_TO_TICKS(1000)
#endif


static const struct stm32_qspi_flash_info ids[] = {
	{0xef6017, "Winbond", "w25q64dw", 23, 16, 12, 8},
	{0}
};


static stm32_qspi_flash_ret_t read(Stm32QspiFlash *self, uint8_t *buf, size_t size, size_t *len) {
	(void)self;

	uint32_t status;
	size_t l = 0;

	TickType_t tm_st = xTaskGetTickCount();
	do {
		status = QUADSPI_SR;
		if (status & (QUADSPI_SR_FTF | QUADSPI_SR_TCF)) {
			*buf = QUADSPI_BYTE_DR;
			buf++;
			l++;
			if (l >= size) {
				break;
			}
		}
		if ((xTaskGetTickCount() - tm_st) > READ_TIMEOUT) {
			return STM32_QSPI_FLASH_RET_TIMEOUT;
		}
	} while (status & QUADSPI_SR_BUSY); 
	if (len != NULL) {
		*len = l;
	}
	QUADSPI_FCR |= QUADSPI_FCR_CTCF;

	return STM32_QSPI_FLASH_RET_OK;
}


static stm32_qspi_flash_ret_t wait_qspi_busy(Stm32QspiFlash *self) {
	(void)self;

	TickType_t tm_st = xTaskGetTickCount();
	while (QUADSPI_SR & QUADSPI_SR_BUSY) {
		if ((xTaskGetTickCount() - tm_st) > QSPI_BUSY_TIMEOUT) {
			return STM32_QSPI_FLASH_RET_TIMEOUT;
		}
	}
	QUADSPI_FCR |= QUADSPI_FCR_CTCF;

	return STM32_QSPI_FLASH_RET_OK;
}


static enum stm32_qspi_flash_status read_mem_status(Stm32QspiFlash *self, bool status2) {
	QUADSPI_DLR = 0;
	if (status2) {
		QUADSPI_CCR = READI | IMODE(SINGLE) | DMODE(SINGLE) | INST(0x35);
	} else {
		QUADSPI_CCR = READI | IMODE(SINGLE) | DMODE(SINGLE) | INST(0x05);
	}
	uint8_t status = 0;
	size_t len = 0;
	if (read(self, (void *)&status, sizeof(status), &len) != STM32_QSPI_FLASH_RET_OK || len != 1) {
		return 0;
	}
	if (status2) {
		return status << 8;
	} else {
		return status;
	}
}


static stm32_qspi_flash_ret_t wait_mem_busy(Stm32QspiFlash *self) {
	TickType_t tm_st = xTaskGetTickCount();
	while (read_mem_status(self, false) & STM32_QSPI_FLASH_STATUS_BUSY) {
		if ((xTaskGetTickCount() - tm_st) > MEM_BUSY_TIMEOUT) {
			return STM32_QSPI_FLASH_RET_TIMEOUT;
		}
		vTaskDelay(2);
	}
	return STM32_QSPI_FLASH_RET_OK;
}


static void reset(Stm32QspiFlash *self) {
	QUADSPI_CCR = IMODE(SINGLE) | INST(0x66);
	wait_qspi_busy(self);
	QUADSPI_CCR = IMODE(SINGLE) | INST(0x99);
	wait_qspi_busy(self);
}


stm32_qspi_flash_ret_t stm32_qspi_flash_read_id(Stm32QspiFlash *self, uint32_t *id) {
	QUADSPI_DLR = 3 - 1;
	QUADSPI_CCR = READI | DMODE(SINGLE) | IMODE(SINGLE) | INST(0x9f);
	size_t len = 0;
	uint8_t buf[3] = {0};
	if (read(self, (void *)buf, sizeof(buf), &len) != STM32_QSPI_FLASH_RET_OK || len != 3) {
		return STM32_QSPI_FLASH_RET_FAILED;
	}
	if (id) {
		*id = (buf[0] << 16) | (buf[1] << 8) | buf[2];
	}
	return STM32_QSPI_FLASH_RET_OK;
}


stm32_qspi_flash_ret_t stm32_qspi_flash_read_winbond_uniq(Stm32QspiFlash *self, uint8_t *uniq) {
	/* Cannot generate 32 dummy cycles. Use empty 32bit address instead. */
	QUADSPI_DLR = 8 - 1;
	QUADSPI_CCR = READI | DMODE(SINGLE) | IMODE(SINGLE) | ADMODE(SINGLE) | ADSIZE(ADSIZE_32BIT) | INST(0x4b);
	QUADSPI_AR = 0;
	size_t len = 0;
	if (read(self, (void *)uniq, 8, &len) != STM32_QSPI_FLASH_RET_OK || len != 8) {
		return STM32_QSPI_FLASH_RET_FAILED;
	}
	return STM32_QSPI_FLASH_RET_OK;
}


stm32_qspi_flash_ret_t stm32_qspi_flash_write_enable(Stm32QspiFlash *self, bool e) {
	if (e) {
		QUADSPI_CCR = WRITEI | IMODE(SINGLE) | INST(0x06);
	} else {
		QUADSPI_CCR = WRITEI | IMODE(SINGLE) | INST(0x04);
	}
	if (wait_qspi_busy(self) != STM32_QSPI_FLASH_RET_OK) {
		return STM32_QSPI_FLASH_RET_FAILED;
	}
	return STM32_QSPI_FLASH_RET_OK;
}


static stm32_qspi_flash_ret_t write(Stm32QspiFlash *self, const uint8_t *buf, size_t len) {
	while (len > 0) {
		if (QUADSPI_SR & QUADSPI_SR_FTF) {
			QUADSPI_BYTE_DR = *buf;
			buf++;
			len--;
		}
	}

	return wait_qspi_busy(self);
}


static stm32_qspi_flash_ret_t write_status(Stm32QspiFlash *self, enum stm32_qspi_flash_status status) {
	(void)self;
	(void)status;
	/* Not implemented */
	return STM32_QSPI_FLASH_RET_FAILED;
}


stm32_qspi_flash_ret_t stm32_qspi_flash_read_page(Stm32QspiFlash *self, size_t addr, void *buf, size_t size) {
	if (u_assert(buf != NULL) ||
	    u_assert(size <= (1UL << self->info->page_size))) {
		return STM32_QSPI_FLASH_RET_FAILED;
	}
	QUADSPI_DLR = size - 1;
	QUADSPI_CCR = READI | DMODE(SINGLE) | IMODE(SINGLE) | ADMODE(SINGLE) | ADSIZE(ADSIZE_24BIT) | DCYC(8) | INST(0x0b);
	QUADSPI_AR = addr;
	size_t len = 0;
	if (read(self, buf, size, &len) != STM32_QSPI_FLASH_RET_OK || len != size) {
		return STM32_QSPI_FLASH_RET_FAILED;
	}
	return STM32_QSPI_FLASH_RET_OK;
}


stm32_qspi_flash_ret_t stm32_qspi_flash_read_page_fast_q(Stm32QspiFlash *self, size_t addr, void *buf, size_t size) {
	if (u_assert(buf != NULL) ||
	    u_assert(size <= (1UL << self->info->page_size))) {
		return STM32_QSPI_FLASH_RET_FAILED;
	}

	QUADSPI_DLR = size - 1;
	QUADSPI_CCR = READI | DMODE(QUAD) | IMODE(SINGLE) | ADMODE(QUAD) | ADSIZE(ADSIZE_24BIT) | DCYC(6) | INST(0xeb);
	QUADSPI_AR = addr;
	size_t len = 0;
	if (read(self, buf, size, &len) != STM32_QSPI_FLASH_RET_OK || len != size) {
		return STM32_QSPI_FLASH_RET_FAILED;
	}
	return STM32_QSPI_FLASH_RET_OK;
}


stm32_qspi_flash_ret_t stm32_qspi_flash_write_page(Stm32QspiFlash *self, size_t addr, const void *buf, size_t size) {
	if (u_assert(size <= (1UL << self->info->page_size)) ||
	    u_assert((addr + size) << (1UL << self->info->size)) ||
	    u_assert(buf != NULL)) {
		return STM32_QSPI_FLASH_RET_FAILED;
	}

	QUADSPI_DLR = size - 1;
	QUADSPI_CCR = WRITEI | DMODE(SINGLE) | IMODE(SINGLE) | ADMODE(SINGLE) | ADSIZE(ADSIZE_24BIT) | INST(0x02);
	QUADSPI_AR = addr;
	if (write(self, buf, size) != STM32_QSPI_FLASH_RET_OK) {
		return STM32_QSPI_FLASH_RET_FAILED;
	}
	if (wait_qspi_busy(self) != STM32_QSPI_FLASH_RET_OK) {
		return STM32_QSPI_FLASH_RET_FAILED;
	}
	if (wait_mem_busy(self) != STM32_QSPI_FLASH_RET_OK) {
		return STM32_QSPI_FLASH_RET_FAILED;
	}

	return STM32_QSPI_FLASH_RET_OK;
}


stm32_qspi_flash_ret_t stm32_qspi_flash_erase_sector(Stm32QspiFlash *self, size_t addr) {
	if (u_assert(self != NULL) ||
	    u_assert((addr % (1 << self->info->sector_size)) == 0)) {
		return STM32_QSPI_FLASH_RET_FAILED;
	}

	QUADSPI_CCR = WRITEI | IMODE(SINGLE) | ADMODE(SINGLE) | ADSIZE(ADSIZE_24BIT) | INST(0x20);
	QUADSPI_AR = addr;
	if (wait_qspi_busy(self) != STM32_QSPI_FLASH_RET_OK) {
		return STM32_QSPI_FLASH_RET_FAILED;
	}
	if (wait_mem_busy(self) != STM32_QSPI_FLASH_RET_OK) {
		return STM32_QSPI_FLASH_RET_FAILED;
	}
	return STM32_QSPI_FLASH_RET_OK;
}


stm32_qspi_flash_ret_t stm32_qspi_flash_erase_block(Stm32QspiFlash *self, size_t addr) {
	if (u_assert(self != NULL) ||
	    u_assert((addr % (1 << self->info->block_size)) == 0)) {
		return STM32_QSPI_FLASH_RET_FAILED;
	}

	QUADSPI_CCR = WRITEI | IMODE(SINGLE) | ADMODE(SINGLE) | ADSIZE(ADSIZE_24BIT) | INST(0xd8);
	QUADSPI_AR = addr;
	if (wait_qspi_busy(self) != STM32_QSPI_FLASH_RET_OK) {
		return STM32_QSPI_FLASH_RET_FAILED;
	}
	if (wait_mem_busy(self) != STM32_QSPI_FLASH_RET_OK) {
		return STM32_QSPI_FLASH_RET_FAILED;
	}
	return STM32_QSPI_FLASH_RET_OK;
}


stm32_qspi_flash_ret_t stm32_qspi_flash_erase_chip(Stm32QspiFlash *self) {
	if (u_assert(self != NULL)) {
		return STM32_QSPI_FLASH_RET_FAILED;
	}

	QUADSPI_CCR = WRITEI | IMODE(SINGLE) | INST(0xc7);
	if (wait_qspi_busy(self) != STM32_QSPI_FLASH_RET_OK) {
		return STM32_QSPI_FLASH_RET_FAILED;
	}
	if (wait_mem_busy(self) != STM32_QSPI_FLASH_RET_OK) {
		return STM32_QSPI_FLASH_RET_FAILED;
	}
	return STM32_QSPI_FLASH_RET_OK;
}





/***************************************************************************************************
 * Flash interface API
 ***************************************************************************************************/

static flash_ret_t flash_get_size(Flash *self, uint32_t i, size_t *size, flash_block_ops_t *ops) {
	Stm32QspiFlash *qspi = (Stm32QspiFlash *)self->parent;

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
			return FLASH_RET_FAILED;
	}
	return FLASH_RET_OK;
}


static flash_ret_t flash_erase(Flash *self, const size_t addr, size_t len) {
	Stm32QspiFlash *qspi = (Stm32QspiFlash *)self->parent;

	if (stm32_qspi_flash_write_enable(qspi, true) != STM32_QSPI_FLASH_RET_OK) {
		return FLASH_RET_FAILED;
	}

	/* For now, the size parameter must be the exact size of a sector,
	 * a block or a full chip and the address must be properly aligned. */
	if (len == (1UL << qspi->info->size) && addr == 0) {
		if (stm32_qspi_flash_erase_chip(qspi) == STM32_QSPI_FLASH_RET_OK) {
			return FLASH_RET_OK;
		}
	} else if (len == (1UL << qspi->info->block_size) && ((addr % (1UL << qspi->info->block_size)) == 0)) {
		if (stm32_qspi_flash_erase_block(qspi, addr) == STM32_QSPI_FLASH_RET_OK) {
			return FLASH_RET_OK;
		}
	} else if (len == (1UL << qspi->info->sector_size) && ((addr % (1UL << qspi->info->sector_size)) == 0)) {
		if (stm32_qspi_flash_erase_sector(qspi, addr) == STM32_QSPI_FLASH_RET_OK) {
			return FLASH_RET_OK;
		}
	}
	return FLASH_RET_FAILED;
}


static flash_ret_t flash_write(Flash *self, const size_t addr, const void *buf, size_t len) {
	Stm32QspiFlash *qspi = (Stm32QspiFlash *)self->parent;
	if (stm32_qspi_flash_write_enable(qspi, true) != STM32_QSPI_FLASH_RET_OK) {
		return FLASH_RET_FAILED;
	}
	if (stm32_qspi_flash_write_page(qspi, addr, buf, len) != STM32_QSPI_FLASH_RET_OK) {
		return FLASH_RET_FAILED;
	}
	return FLASH_RET_OK;
}


static flash_ret_t flash_read(Flash *self, const size_t addr, void *buf, size_t len) {
	Stm32QspiFlash *qspi = (Stm32QspiFlash *)self->parent;
	if (stm32_qspi_flash_read_page(qspi, addr, buf, len) != STM32_QSPI_FLASH_RET_OK) {
		return FLASH_RET_FAILED;
	}
	return FLASH_RET_OK;
}


static const struct flash_vmt iface_vmt = {
	.get_size = flash_get_size,
	.erase = flash_erase,
	.write = flash_write,
	.read = flash_read,
};


stm32_qspi_flash_ret_t stm32_qspi_flash_init(Stm32QspiFlash *self) {
	if (u_assert(self != NULL)) {
		return STM32_QSPI_FLASH_RET_FAILED;
	}
	memset(self, 0, sizeof(Stm32QspiFlash));
	quadspi_enable();
	reset(self);	

	/* The size is unknown until we read ID */
	QUADSPI_DCR = (10 << QUADSPI_DCR_FSIZE_SHIFT);
	uint32_t id = 0;
	stm32_qspi_flash_read_id(self, &id);

	/* Try to match the ID with a record from the table of known IDs */
	size_t i = 0;
	while (ids[i].id != 0 && ids[i].id != id) {
		i++;
	}
	if (ids[i].id != id) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("couldn't detect QSPI flash (id = 0x%06x)"), id);
		goto err;
	}
	self->info = &ids[i];

	/* Now the flash parameters are known, set the correct flash size */
	QUADSPI_DCR = ((self->info->size - 1) << QUADSPI_DCR_FSIZE_SHIFT);
	
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
		stm32_qspi_flash_read_winbond_uniq(self, uniq);
		/* Wow. */
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("Winbond unique ID = %02x%02x %02x%02x %02x%02x %02x%02x"),
			uniq[0], uniq[1], uniq[2], uniq[3], uniq[4], uniq[5], uniq[6], uniq[7]
		);
	}

	/* Prepare the interface */
	self->iface.parent = (void *)self;
	self->iface.vmt = &iface_vmt;

	return STM32_QSPI_FLASH_RET_OK;

err:
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("initialization failed"));
	return STM32_QSPI_FLASH_RET_FAILED;
}


stm32_qspi_flash_ret_t stm32_qspi_flash_free(Stm32QspiFlash *self) {
	if (u_assert(self != NULL)) {
		return STM32_QSPI_FLASH_RET_FAILED;
	}

	return STM32_QSPI_FLASH_RET_OK;
}
