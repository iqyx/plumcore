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
#define QSPI_BUSY_TIMEOUT CONFIG_SERVICE_STM32_QSPI_FLASH_QSPI_BUSY_TIMEOUT
#else
#define QSPI_BUSY_TIMEOUT 1000
#endif

#ifdef CONFIG_SERVICE_STM32_QSPI_FLASH_MEM_BUSY_TIMEOUT
#define MEM_BUSY_TIMEOUT CONFIG_SERVICE_STM32_QSPI_FLASH_MEM_BUSY_TIMEOUT
#else
#define MEM_BUSY_TIMEOUT 1000
#endif

#ifdef CONFIG_SERVICE_STM32_QSPI_FLASH_READ_TIMEOUT
#define READ_TIMEOUT CONFIG_SERVICE_STM32_QSPI_FLASH_READ_TIMEOUT
#else
#define READ_TIMEOUT 1000
#endif


static const struct stm32_qspi_flash_info ids[] = {
	{0xef6017, "Winbond", "w25q64dw", 23, 16, 12, 8},
	{0}
};


/* Test data for write & read tests of the flash memory. A const table is actually
 * easier and faster to use than a PRNG. */
static const uint8_t test_vector[512] = {
  0x60, 0x9c, 0x43, 0xdd, 0xee, 0x1d, 0x92, 0x6f, 0xa9, 0xaf, 0x5b, 0x7d, 0x67, 0x56, 0x8e, 0x48,
  0x8e, 0x05, 0xd1, 0x3f, 0x47, 0x8f, 0x74, 0x6d, 0xfc, 0x54, 0x8b, 0x09, 0xfc, 0x9b, 0x12, 0x36,
  0xbd, 0xe0, 0xe2, 0x66, 0x29, 0xd2, 0xc8, 0x36, 0xc2, 0x17, 0x02, 0x35, 0x2a, 0xb5, 0x54, 0xbc,
  0xab, 0x1c, 0xc7, 0x7b, 0x4f, 0xc1, 0x84, 0x11, 0xe8, 0x06, 0x93, 0xb2, 0x67, 0x33, 0xff, 0x3f,
  0x8e, 0xd4, 0x9e, 0xf1, 0xb3, 0xd5, 0xf6, 0xdf, 0xea, 0x91, 0x42, 0x72, 0xd8, 0xe9, 0x5d, 0xd8,
  0x4c, 0xb4, 0x33, 0xfa, 0xe3, 0x52, 0xbf, 0x5d, 0x44, 0xd5, 0xe8, 0x48, 0x3b, 0x3a, 0x78, 0x7a,
  0xca, 0x97, 0x30, 0x0f, 0xde, 0xd8, 0xfd, 0x29, 0x5a, 0x7a, 0xdf, 0xc0, 0xf9, 0x15, 0x89, 0xf3,
  0xc8, 0xc9, 0xa9, 0x73, 0x36, 0x8d, 0xe8, 0xb3, 0x3e, 0xf1, 0x63, 0x25, 0x1f, 0x8d, 0x72, 0x90,
  0x12, 0x84, 0xa2, 0xf4, 0x4b, 0x80, 0x40, 0x88, 0xc3, 0xe4, 0xf6, 0xfc, 0x3d, 0x97, 0x56, 0x29,
  0x43, 0x5d, 0x32, 0xa5, 0x4a, 0x74, 0xb9, 0xf5, 0x4d, 0xb9, 0x9e, 0x0f, 0x98, 0x38, 0x56, 0xb4,
  0xdc, 0x3f, 0x4d, 0xb8, 0x89, 0x82, 0x61, 0xa4, 0xfd, 0xb1, 0x85, 0xb9, 0xcb, 0x8e, 0x03, 0x5d,
  0x3b, 0x96, 0x47, 0x80, 0xf7, 0xae, 0x3b, 0x8b, 0xcf, 0xce, 0x01, 0x7b, 0x5c, 0xdf, 0x79, 0x62,
  0x6f, 0x47, 0xd5, 0xa4, 0xa1, 0x24, 0xdf, 0xab, 0x09, 0xd3, 0x04, 0xd1, 0x06, 0x42, 0xce, 0xa7,
  0xca, 0x21, 0x77, 0x2f, 0x42, 0xf8, 0xbc, 0x7d, 0x33, 0x44, 0x35, 0x93, 0x00, 0xd0, 0xc7, 0xf4,
  0xc2, 0x27, 0xad, 0x1a, 0xff, 0x14, 0x91, 0xc9, 0x41, 0xf3, 0x5d, 0x54, 0xe3, 0xd3, 0xa2, 0x0b,
  0x70, 0xf2, 0xe9, 0xb4, 0xcc, 0xca, 0x35, 0xe7, 0x2d, 0x61, 0xa3, 0xcc, 0xee, 0xae, 0x2f, 0xe9,
  0xce, 0x13, 0x69, 0xc2, 0xbd, 0x54, 0xff, 0xf3, 0x27, 0x33, 0x72, 0x5f, 0x53, 0xa9, 0xa2, 0xf2,
  0x60, 0x71, 0x47, 0x48, 0xfe, 0x7c, 0xa4, 0x06, 0x9b, 0x1a, 0x78, 0x90, 0x37, 0x65, 0x32, 0x51,
  0x3c, 0xc2, 0x6e, 0x30, 0xb7, 0x70, 0x03, 0xab, 0x16, 0x94, 0x20, 0xbb, 0x2d, 0xe4, 0x22, 0xab,
  0x9c, 0x04, 0x75, 0xfc, 0x48, 0x07, 0x08, 0x88, 0x96, 0x64, 0x2d, 0x85, 0xc1, 0xb5, 0x0d, 0x2a,
  0x0f, 0xa5, 0x77, 0xf7, 0x0d, 0xea, 0x2e, 0x42, 0xf5, 0xa7, 0x36, 0x24, 0x8d, 0x6d, 0x74, 0xec,
  0x33, 0x5e, 0x16, 0x69, 0x0e, 0xd2, 0x21, 0x30, 0xa3, 0x4b, 0xf7, 0x2a, 0x56, 0xfa, 0x83, 0xb1,
  0x56, 0x69, 0x66, 0x0f, 0xc8, 0x70, 0xba, 0xb8, 0xd8, 0xe2, 0xf5, 0xd7, 0x3e, 0x14, 0xb9, 0xa9,
  0x2a, 0xb5, 0xd2, 0x78, 0x24, 0xda, 0xb8, 0xe0, 0x20, 0x21, 0xd1, 0xbe, 0x48, 0xea, 0xdc, 0x54,
  0xa3, 0x32, 0x46, 0x5d, 0x1e, 0x06, 0xdb, 0x84, 0xaa, 0x56, 0xb0, 0x0c, 0x26, 0xb5, 0xad, 0xe7,
  0xc1, 0x72, 0x0f, 0x63, 0xd4, 0x7d, 0xe3, 0xea, 0x81, 0x4d, 0x09, 0xfd, 0x01, 0x49, 0x96, 0x01,
  0x3d, 0x64, 0xe4, 0x52, 0x0f, 0x22, 0x9c, 0xff, 0x41, 0xae, 0xd9, 0xe9, 0xaf, 0x78, 0x75, 0x17,
  0x63, 0xf4, 0xab, 0x49, 0x04, 0x7e, 0xc9, 0x17, 0x59, 0x0c, 0xf1, 0x6c, 0x5f, 0xd0, 0x71, 0xa6,
  0x07, 0x07, 0x16, 0xf1, 0x94, 0x29, 0x55, 0x4d, 0xaf, 0x77, 0xf3, 0x61, 0xca, 0x8d, 0x50, 0xff,
  0x43, 0x15, 0x35, 0x0c, 0xd0, 0x28, 0xbe, 0xfc, 0x5d, 0x23, 0xd3, 0xda, 0x7f, 0x4e, 0xfc, 0xe0,
  0x7f, 0x1f, 0xc0, 0xff, 0x4a, 0xe8, 0x97, 0xdd, 0xc5, 0x26, 0xcf, 0xba, 0x3a, 0x45, 0x8d, 0x63,
  0x46, 0x86, 0x9f, 0xfb, 0x5f, 0x79, 0x4d, 0x3a, 0x7e, 0x66, 0xbf, 0xbb, 0x7b, 0x99, 0x6a, 0x1e
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
	    u_assert((addr + size) < (1UL << self->info->size)) ||
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
	
	return STM32_QSPI_FLASH_RET_FAILED;
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
	return STM32_QSPI_FLASH_RET_FAILED;
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
	return FLASH_RET_FAILED;
}


static flash_ret_t flash_erase(Flash *self, const size_t addr, size_t len) {
	Stm32QspiFlash *qspi = (Stm32QspiFlash *)self->parent;

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


/***************************************************************************************************
 * Test and benchmarking functions (some may overwrite the data)
 ***************************************************************************************************/

stm32_qspi_flash_ret_t stm32_qspi_flash_page_write_test(Stm32QspiFlash *self, size_t page) {
	size_t page_size = 1UL << self->info->page_size;
	if (page_size > 256) {
		/* no worky */
		return STM32_QSPI_FLASH_RET_FAILED;
	}

	stm32_qspi_flash_write_enable(self, true);
	stm32_qspi_flash_write_page(self, page * page_size, test_vector + page % page_size, page_size);
	return STM32_QSPI_FLASH_RET_OK;
}


stm32_qspi_flash_ret_t stm32_qspi_flash_page_read_test(Stm32QspiFlash *self, size_t page) {
	size_t page_size = 1UL << self->info->page_size;
	if (page_size > 256) {
		/* no worky */
		return STM32_QSPI_FLASH_RET_FAILED;
	}

	uint8_t buf[256] = {0};
	stm32_qspi_flash_read_page_fast_q(self, page * page_size, buf, page_size);
	size_t errors = 0;
	for (size_t i = 0; i < page_size; i++) {
		if (buf[i] != *(test_vector + page % page_size + i)) {
			errors++;
		}
	}
	if (errors > 0) {
		u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("verify error page = %u, errors = %u"), page, errors);
		return STM32_QSPI_FLASH_RET_FAILED;
	}
	return STM32_QSPI_FLASH_RET_OK;
}


static int32_t timespec_diff(struct timespec *a, struct timespec *b) {
	int64_t a_us = a->tv_sec * 1000000 + a->tv_nsec / 1000;
	int64_t b_us = b->tv_sec * 1000000 + b->tv_nsec / 1000;
	return a_us - b_us;
}


stm32_qspi_flash_ret_t stm32_qspi_flash_page_speed_test(Stm32QspiFlash *self, size_t page, size_t pages, Clock *rtc) {
	size_t page_size = 1UL << self->info->page_size;
	size_t sector = page >> (self->info->sector_size - self->info->page_size);
	size_t sectors = pages >> (self->info->sector_size - self->info->page_size);

	u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("flash speed test erase, start sector = %u, sectors = %u..."),
		sector,
		sectors
	);
	struct timespec e_start = {0};
	rtc->get(rtc->parent, &e_start);
	for (size_t i = 0; i < sectors; i++) {
		stm32_qspi_flash_write_enable(self, true);
		stm32_qspi_flash_erase_sector(self, (sector + i) << self->info->sector_size);
		// vTaskDelay(150);
	}
	struct timespec e_end = {0};
	rtc->get(rtc->parent, &e_end);
	int32_t e_us = timespec_diff(&e_end, &e_start);
	u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("...finished, %d ms, speed = %u KB/s, erase time = %u ms"),
		e_us / 1000,
		0,
		e_us / 1000 / sectors
	);

	u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("flash speed test writing, start page = %u, pages = %u..."),
		page,
		pages
	);
	struct timespec wr_start = {0};
	rtc->get(rtc->parent, &wr_start);
	for (size_t i = 0; i < pages; i++) {
		stm32_qspi_flash_page_write_test(self, page + i);
	}
	struct timespec wr_end = {0};
	rtc->get(rtc->parent, &wr_end);
	int32_t wr_us = timespec_diff(&wr_end, &wr_start);
	u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("...finished, %d ms, speed = %u KB/s, page program time = %u ms"),
		wr_us / 1000,
		pages * page_size / (wr_us / 1000) * 1000 / 1024,
		wr_us / 1000 / pages
	);

	u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("flash speed test reading, start page = %u, pages = %u..."),
		page,
		pages
	);
	struct timespec rd_start = {0};
	rtc->get(rtc->parent, &rd_start);
	for (size_t i = 0; i < pages; i++) {
		stm32_qspi_flash_page_read_test(self, page + i);
	}
	struct timespec rd_end = {0};
	rtc->get(rtc->parent, &rd_end);
	int32_t rd_us = timespec_diff(&rd_end, &rd_start);
	u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("...finished, %d ms, speed = %u KB/s"),
		rd_us / 1000,
		pages * page_size / (rd_us / 1000) * 1000 / 1024
	);

	return STM32_QSPI_FLASH_RET_OK;
}


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
