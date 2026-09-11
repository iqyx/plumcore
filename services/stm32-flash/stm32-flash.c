/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Driver for STM32 internal flash memory
 *
 * Copyright (c) 2022, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include <main.h>
#include <interfaces/flash.h>
#include "stm32-flash.h"

#if defined(STM32G4)
	#include <libopencm3/cm3/cortex.h>
	#include <libopencm3/stm32/flash.h>
#elif defined(STM32U5)
	#include <stm32u5xx.h>
#endif

#define MODULE_NAME "stm32-flash"


#if defined(STM32U5)

/* STM32U5 internal flash: dual bank, 8 KB pages, programmed 128 bits (a quad-word) at a time through the
 * non-secure FLASH registers. */
#define FLASH_U5_PAGE_SIZE  8192
#define FLASH_U5_KEY1       0x45670123
#define FLASH_U5_KEY2       0xCDEF89AB

/* Status flags cleared before each operation (all rc_w1 error bits plus the end-of-operation flag). */
#define FLASH_U5_STATUS_CLEAR (FLASH_NSSR_OPERR | FLASH_NSSR_PROGERR | FLASH_NSSR_WRPERR | \
                               FLASH_NSSR_PGAERR | FLASH_NSSR_SIZERR | FLASH_NSSR_PGSERR | \
                               FLASH_NSSR_OPTWERR | FLASH_NSSR_EOP)

static void flash_u5_wait(void) {
	while (FLASH->NSSR & FLASH_NSSR_BSY) {
		;
	}
}


static void flash_u5_unlock(void) {
	if (FLASH->NSCR & FLASH_NSCR_LOCK) {
		FLASH->NSKEYR = FLASH_U5_KEY1;
		FLASH->NSKEYR = FLASH_U5_KEY2;
	}
}


static void flash_u5_lock(void) {
	FLASH->NSCR |= FLASH_NSCR_LOCK;
}

#endif


/***************************************************************************************************
 * Hardware probing functions
 ***************************************************************************************************/

#if defined(STM32U5)

static size_t get_flash_size(void) {
	/* The factory flash size register holds the flash size in kilobytes. */
	return (size_t)(*(const uint16_t *)FLASHSIZE_BASE) << 10UL;
}


static size_t get_sector_size(void) {
	return FLASH_U5_PAGE_SIZE;
}

#else

static uint16_t get_devid(void) {
	return ((uint32_t)MMIO16(0xe0042000)) & 0xfffUL;
}


static size_t get_flash_size(void) {
	return ((uint32_t)MMIO16(0x1fff75e0) << 10UL);
}


static bool get_dbank(void) {
	return (uint32_t)MMIO16(0x1fff7800) & (1 << 22UL);
}


static size_t get_sector_size(void) {
	uint16_t devid = get_devid();
	bool dbank = get_dbank();

	if (devid == 0x468) {
		/* STM32G4, category 2 */
		return 2048;
	} else if (devid == 0x469 && dbank == false) {
		/* STM32G4, category 3, single bank */
		return 4096;
	} else if (devid == 0x469 && dbank == true) {
		/* STM32G4, category 3, dual bank */
		return 2048;
	} else if (devid == 0x479) {
		/* STM32G4, category 4 */
		return 2048;
	} else {
		return 2048;
	}
}

#endif


/***************************************************************************************************
 * Flash interface implementation
 ***************************************************************************************************/

static flash_ret_t stm32_flash_get_size(Flash *flash, uint32_t i, size_t *size, flash_block_ops_t *ops) {
	Stm32Flash *self = flash->parent;

	switch (i) {
		case 0:
			*size = self->flash_size;
			*ops = FLASH_BLOCK_OPS_ERASE;
			break;
		case 1:
			*size = self->flash_sector_size;
			*ops = FLASH_BLOCK_OPS_ERASE;
			break;
		case 2:
			*size = self->flash_sector_size;
			*ops = FLASH_BLOCK_OPS_ERASE;
			break;
		case 3:
			*size = self->flash_page_size;
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
		if (addr % self->flash_sector_size) {
			return FLASH_RET_FAILED;
		}
		if (len % self->flash_sector_size) {
			return FLASH_RET_FAILED;
		}
		cm_disable_interrupts();
		flash_unlock();
		for (size_t i = 0; i < (len / self->flash_sector_size); i++) {
			size_t sector = i + (addr / self->flash_sector_size);

			/* Reset error flags before erasing a sector. */
			FLASH_SR |= 0x3f8;

			/* Beware, wrong naming. */
			flash_wait_for_last_operation();
			flash_erase_page(sector);
			flash_wait_for_last_operation();
		}
		flash_lock();
		cm_enable_interrupts();
	#elif defined(STM32U5)
		/* Always perform erase in whole pages. */
		if (addr % self->flash_sector_size) {
			return FLASH_RET_FAILED;
		}
		if (len % self->flash_sector_size) {
			return FLASH_RET_FAILED;
		}

		/* U575 is a dual bank device: the flash is split into two equally sized banks, page numbering
		 * restarts in each bank and the target bank is selected with the BKER bit. */
		bool dual_bank = self->flash_size > (1024 * 1024);
		size_t bank_size = dual_bank ? (self->flash_size / 2) : self->flash_size;

		__disable_irq();
		flash_u5_unlock();
		for (size_t i = 0; i < (len / self->flash_sector_size); i++) {
			size_t a = addr + i * self->flash_sector_size;
			uint32_t bank = a / bank_size;
			uint32_t page = (a % bank_size) / self->flash_sector_size;

			flash_u5_wait();
			FLASH->NSSR = FLASH_U5_STATUS_CLEAR;

			uint32_t cr = FLASH->NSCR & ~(FLASH_NSCR_PNB_Msk | FLASH_NSCR_BKER_Msk);
			cr |= FLASH_NSCR_PER | ((page << FLASH_NSCR_PNB_Pos) & FLASH_NSCR_PNB_Msk);
			if (bank) {
				cr |= FLASH_NSCR_BKER;
			}
			FLASH->NSCR = cr;
			FLASH->NSCR |= FLASH_NSCR_STRT;

			flash_u5_wait();
			FLASH->NSCR &= ~FLASH_NSCR_PER;
		}
		flash_u5_lock();
		__enable_irq();
	#else
		return FLASH_RET_FAILED;
	#endif

	return FLASH_RET_OK;
}


static flash_ret_t stm32_flash_write(Flash *flash, const size_t addr, const void *buf, size_t len) {
	Stm32Flash *self = flash->parent;

	#if defined(STM32G4)
		/* Always perform write in doublewords. */
		if (addr % self->flash_page_size) {
			return FLASH_RET_FAILED;
		}
		if (len % self->flash_page_size) {
			return FLASH_RET_FAILED;
		}

		cm_disable_interrupts();
		flash_unlock();
		for (size_t i = 0; i < len; i += 8) {
			flash_wait_for_last_operation();

			/* Reset error flags before writing. */
			FLASH_SR |= 0x3f8;

			FLASH_CR |= FLASH_CR_PG;
			MMIO32(STM32_FLASH_BASE + addr + i) = *(const uint32_t *)((const uint8_t *)buf + i);
			MMIO32(STM32_FLASH_BASE + addr + i + 4) = *(const uint32_t *)((const uint8_t *)buf + i + 4);
			flash_wait_for_last_operation();
			FLASH_CR &= ~FLASH_CR_PG;
		}
		flash_lock();
		cm_enable_interrupts();
	#elif defined(STM32U5)
		/* Always program whole quad-words (128 bits). */
		if (addr % self->flash_page_size) {
			return FLASH_RET_FAILED;
		}
		if (len % self->flash_page_size) {
			return FLASH_RET_FAILED;
		}

		__disable_irq();
		flash_u5_unlock();
		for (size_t i = 0; i < len; i += self->flash_page_size) {
			volatile uint32_t *dst = (volatile uint32_t *)(STM32_FLASH_BASE + addr + i);
			const uint8_t *src = (const uint8_t *)buf + i;

			flash_u5_wait();
			FLASH->NSSR = FLASH_U5_STATUS_CLEAR;
			FLASH->NSCR |= FLASH_NSCR_PG;

			/* Push the four words of the quad-word; the last store triggers the programming. Copy through
			 * a local to tolerate an unaligned source buffer. */
			for (size_t w = 0; w < 4; w++) {
				uint32_t word;
				memcpy(&word, src + w * 4, 4);
				dst[w] = word;
			}

			flash_u5_wait();
			FLASH->NSCR &= ~FLASH_NSCR_PG;
		}
		flash_u5_lock();
		__enable_irq();
	#else
		return FLASH_RET_FAILED;
	#endif

	return FLASH_RET_OK;
}


static flash_ret_t stm32_flash_read(Flash *flash, const size_t addr, void *buf, size_t len) {
	Stm32Flash *self = flash->parent;

	/* We are not limited in reading, we do not have to respect page/sector sizes.
	 * Do what is requested unless it overflows past the end of the flash. */
	if ((addr + len) > self->flash_size) {
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

	self->flash_size = get_flash_size();
	self->flash_sector_size = get_sector_size();
	#if defined(STM32U5)
		/* U5 programs 128 bits (a quad-word) at a time. */
		self->flash_page_size = 16;
	#else
		self->flash_page_size = 8;
	#endif

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("MCU = %s, flash size = %lu B, sector = %lu B, page = %lu B"),
		CONFIG_MCU_FAMILY,
		self->flash_size,
		self->flash_sector_size,
		self->flash_page_size
	);

	return STM32_FLASH_RET_OK;
}


stm32_flash_ret_t stm32_flash_free(Stm32Flash *self) {
	(void)self;

	return STM32_FLASH_RET_OK;
}



