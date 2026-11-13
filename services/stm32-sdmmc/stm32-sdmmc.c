/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 SDMMC SD card driver
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <main.h>

#include <interfaces/block.h>

#if defined(STM32U5)
	#include <stm32u5xx.h>
#else
	#error "stm32-sdmmc service is not compatible with this MCU family"
#endif

#include "stm32-sdmmc.h"

#define MODULE_NAME "stm32-sdmmc"

#define SDMMC_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE + 256)
#define SDMMC_INIT_FREQ 400000
#define SDMMC_CMD_TIMEOUT pdMS_TO_TICKS(100)
#define SDMMC_DATA_TIMEOUT pdMS_TO_TICKS(1000)
#define SDMMC_BUSY_TIMEOUT pdMS_TO_TICKS(1000)
#define SDMMC_OP_COND_TIMEOUT pdMS_TO_TICKS(1000)

#define SDMMC_BLOCK_SIZE_LOG2 9
#define SDMMC_BLOCK_SIZE (1UL << SDMMC_BLOCK_SIZE_LOG2)

/* Maximum number of blocks transferred by a single command, limited by the DLEN register width. */
#define SDMMC_MAX_BLOCKS ((SDMMC_DLEN_DATALENGTH >> SDMMC_DLEN_DATALENGTH_Pos) / SDMMC_BLOCK_SIZE)

/* Number of bytes moved when the FIFO is half full (RX) or half empty (TX). */
#define SDMMC_FIFO_HALF 32

/* Command response types (the WAITRESP field of the CMD register). */
#define RESP_NONE 0
#define RESP_SHORT SDMMC_CMD_WAITRESP_0
#define RESP_SHORT_NOCRC SDMMC_CMD_WAITRESP_1
#define RESP_LONG (SDMMC_CMD_WAITRESP_0 | SDMMC_CMD_WAITRESP_1)

#define SDMMC_ICR_ALL ( \
	SDMMC_ICR_CCRCFAILC | SDMMC_ICR_DCRCFAILC | SDMMC_ICR_CTIMEOUTC | SDMMC_ICR_DTIMEOUTC | \
	SDMMC_ICR_TXUNDERRC | SDMMC_ICR_RXOVERRC | SDMMC_ICR_CMDRENDC | SDMMC_ICR_CMDSENTC | SDMMC_ICR_DATAENDC | \
	SDMMC_ICR_DHOLDC | SDMMC_ICR_DBCKENDC | SDMMC_ICR_DABORTC | SDMMC_ICR_BUSYD0ENDC \
)

#define SDMMC_STA_RX_ERRORS (SDMMC_STA_DCRCFAIL | SDMMC_STA_DTIMEOUT | SDMMC_STA_RXOVERR)
#define SDMMC_STA_TX_ERRORS (SDMMC_STA_DCRCFAIL | SDMMC_STA_DTIMEOUT | SDMMC_STA_TXUNDERR)

/* Card status (R1) fields */
#define SD_R1_ERRORS 0xfdffe008UL
#define SD_R1_READY_FOR_DATA (1UL << 8)
#define SD_R1_STATE(x) (((x) >> 9) & 0xf)
#define SD_STATE_TRAN 4

/* Published RCA response (R6) error bits */
#define SD_R6_ERRORS 0xe000UL

/* OCR register fields (R3) */
#define SD_OCR_BUSY (1UL << 31)
#define SD_OCR_CCS (1UL << 30)
#define SD_OCR_VOLTAGE_WINDOW 0x00ff8000UL

/* CMD8 argument: 2.7-3.6 V supply and the check pattern echoed back by the card */
#define SD_IF_COND_ARG 0x1aaUL

/* ACMD6 argument selecting the 4-bit bus */
#define SD_BUS_WIDTH_4 2

/* Commands used by the driver. Application specific commands (ACMD) must be preceded by APP_CMD. */
enum sd_cmd {
	SD_CMD_GO_IDLE_STATE = 0,
	SD_CMD_ALL_SEND_CID = 2,
	SD_CMD_SEND_RELATIVE_ADDR = 3,
	SD_ACMD_SET_BUS_WIDTH = 6,
	SD_CMD_SELECT_CARD = 7,
	SD_CMD_SEND_IF_COND = 8,
	SD_CMD_SEND_CSD = 9,
	SD_CMD_STOP_TRANSMISSION = 12,
	SD_CMD_SEND_STATUS = 13,
	SD_CMD_SET_BLOCKLEN = 16,
	SD_CMD_READ_SINGLE_BLOCK = 17,
	SD_CMD_READ_MULTIPLE_BLOCK = 18,
	SD_CMD_WRITE_BLOCK = 24,
	SD_CMD_WRITE_MULTIPLE_BLOCK = 25,
	SD_ACMD_SD_SEND_OP_COND = 41,
	SD_CMD_APP_CMD = 55,
};


static const struct block_vmt block_vmt;


static bool timed_out(TickType_t start, TickType_t timeout) {
	return (xTaskGetTickCount() - start) > timeout;
}


static void sdmmc_set_clock(Stm32Sdmmc *self, uint32_t freq) {
	SDMMC_TypeDef *base = (SDMMC_TypeDef *)self->conf.mmio_base;

	/* SDMMC_CK = kernel clock / (2 * CLKDIV). Round the divider up not to exceed the requested frequency. */
	uint32_t div = (self->conf.kernel_freq + 2 * freq - 1) / (2 * freq);
	base->CLKCR = (base->CLKCR & ~SDMMC_CLKCR_CLKDIV) | (div << SDMMC_CLKCR_CLKDIV_Pos);
	self->ck_freq = self->conf.kernel_freq / (2 * div);
}


static stm32_sdmmc_ret_t sdmmc_cmd(Stm32Sdmmc *self, enum sd_cmd cmd, uint32_t arg, uint32_t flags) {
	SDMMC_TypeDef *base = (SDMMC_TypeDef *)self->conf.mmio_base;

	base->ICR = SDMMC_ICR_ALL;
	base->ARG = arg;
	base->CMD = ((uint32_t)cmd << SDMMC_CMD_CMDINDEX_Pos) | flags | SDMMC_CMD_CPSMEN;

	/* Commands without a response end with CMDSENT, all others with CMDREND or an error. */
	uint32_t done = SDMMC_STA_CTIMEOUT | SDMMC_STA_CCRCFAIL;
	if (flags & SDMMC_CMD_WAITRESP) {
		done |= SDMMC_STA_CMDREND;
	} else {
		done |= SDMMC_STA_CMDSENT;
	}

	TickType_t start = xTaskGetTickCount();
	uint32_t sta = 0;
	while (((sta = base->STA) & done) == 0) {
		if (timed_out(start, SDMMC_CMD_TIMEOUT)) {
			return STM32_SDMMC_RET_TIMEOUT;
		}
	}

	if (sta & SDMMC_STA_CTIMEOUT) {
		return STM32_SDMMC_RET_TIMEOUT;
	}
	if (sta & SDMMC_STA_CCRCFAIL) {
		return STM32_SDMMC_RET_CRC;
	}
	return STM32_SDMMC_RET_OK;
}


/* Send a command with a R1 response and check the card status for errors. */
static stm32_sdmmc_ret_t sdmmc_cmd_r1(Stm32Sdmmc *self, enum sd_cmd cmd, uint32_t arg, uint32_t flags) {
	stm32_sdmmc_ret_t ret = sdmmc_cmd(self, cmd, arg, RESP_SHORT | flags);
	if (ret != STM32_SDMMC_RET_OK) {
		return ret;
	}
	if (((SDMMC_TypeDef *)self->conf.mmio_base)->RESP1 & SD_R1_ERRORS) {
		return STM32_SDMMC_RET_FAILED;
	}
	return STM32_SDMMC_RET_OK;
}


/* Wait until the card releases the busy signal on D0 after a R1b response. */
static stm32_sdmmc_ret_t sdmmc_wait_busy(Stm32Sdmmc *self) {
	TickType_t start = xTaskGetTickCount();
	while (((SDMMC_TypeDef *)self->conf.mmio_base)->STA & SDMMC_STA_BUSYD0) {
		if (timed_out(start, SDMMC_BUSY_TIMEOUT)) {
			return STM32_SDMMC_RET_TIMEOUT;
		}
		vTaskDelay(1);
	}
	return STM32_SDMMC_RET_OK;
}


/* Wait until the card finishes programming and is ready to accept a new transfer. */
static stm32_sdmmc_ret_t sdmmc_wait_ready(Stm32Sdmmc *self) {
	TickType_t start = xTaskGetTickCount();
	while (true) {
		stm32_sdmmc_ret_t ret = sdmmc_cmd_r1(self, SD_CMD_SEND_STATUS, self->rca << 16, 0);
		if (ret != STM32_SDMMC_RET_OK) {
			return ret;
		}
		uint32_t status = ((SDMMC_TypeDef *)self->conf.mmio_base)->RESP1;
		if ((status & SD_R1_READY_FOR_DATA) && SD_R1_STATE(status) == SD_STATE_TRAN) {
			return STM32_SDMMC_RET_OK;
		}
		if (timed_out(start, SDMMC_BUSY_TIMEOUT)) {
			return STM32_SDMMC_RET_TIMEOUT;
		}
		vTaskDelay(1);
	}
}


/* Stop a multiple block transfer or abort a failed one. The card may report an out of range error when a
 * multiple block transfer ends at the last block, the response status is therefore ignored. */
static stm32_sdmmc_ret_t sdmmc_stop_transfer(Stm32Sdmmc *self) {
	stm32_sdmmc_ret_t ret = sdmmc_cmd(self, SD_CMD_STOP_TRANSMISSION, 0, RESP_SHORT | SDMMC_CMD_CMDSTOP);
	if (ret != STM32_SDMMC_RET_OK) {
		return ret;
	}
	return sdmmc_wait_busy(self);
}


static stm32_sdmmc_ret_t sdmmc_wait_data_end(Stm32Sdmmc *self, uint32_t errors) {
	SDMMC_TypeDef *base = (SDMMC_TypeDef *)self->conf.mmio_base;

	TickType_t start = xTaskGetTickCount();
	while (true) {
		uint32_t sta = base->STA;
		if (sta & SDMMC_STA_DTIMEOUT) {
			return STM32_SDMMC_RET_TIMEOUT;
		}
		if (sta & SDMMC_STA_DCRCFAIL) {
			return STM32_SDMMC_RET_CRC;
		}
		if (sta & errors) {
			return STM32_SDMMC_RET_FAILED;
		}
		if (sta & SDMMC_STA_DATAEND) {
			return STM32_SDMMC_RET_OK;
		}
		if (timed_out(start, SDMMC_DATA_TIMEOUT)) {
			return STM32_SDMMC_RET_TIMEOUT;
		}
	}
}


/* Prepare the data path. The transfer itself is started by the next command sent with CMDTRANS set. */
static void sdmmc_setup_data(Stm32Sdmmc *self, size_t count, bool to_host) {
	SDMMC_TypeDef *base = (SDMMC_TypeDef *)self->conf.mmio_base;

	/* Data timeout of 1 s, counted in SDMMC_CK cycles. */
	base->DTIMER = self->ck_freq;
	base->DLEN = count * SDMMC_BLOCK_SIZE;
	base->DCTRL = (SDMMC_BLOCK_SIZE_LOG2 << SDMMC_DCTRL_DBLOCKSIZE_Pos) | (to_host ? SDMMC_DCTRL_DTDIR : 0);
}


/* The data FIFO is serviced by the CPU. Hardware flow control stops the bus clock when the FIFO is full (RX)
 * or empty (TX), so the transfer survives being preempted. Buffers have no alignment requirements. */
static stm32_sdmmc_ret_t sdmmc_read_blocks(Stm32Sdmmc *self, uint32_t lba, uint8_t *buf, size_t count) {
	SDMMC_TypeDef *base = (SDMMC_TypeDef *)self->conf.mmio_base;

	sdmmc_setup_data(self, count, true);
	enum sd_cmd cmd = (count > 1) ? SD_CMD_READ_MULTIPLE_BLOCK : SD_CMD_READ_SINGLE_BLOCK;
	uint32_t addr = self->high_capacity ? lba : lba * SDMMC_BLOCK_SIZE;
	stm32_sdmmc_ret_t ret = sdmmc_cmd_r1(self, cmd, addr, SDMMC_CMD_CMDTRANS);
	if (ret != STM32_SDMMC_RET_OK) {
		goto err;
	}

	size_t remaining = count * SDMMC_BLOCK_SIZE;
	TickType_t start = xTaskGetTickCount();
	while (remaining > 0) {
		uint32_t sta = base->STA;
		if (sta & SDMMC_STA_RX_ERRORS) {
			break;
		}
		if (sta & SDMMC_STA_RXFIFOHF) {
			for (size_t i = 0; i < SDMMC_FIFO_HALF; i += sizeof(uint32_t)) {
				uint32_t w = base->FIFO;
				memcpy(buf, &w, sizeof(w));
				buf += sizeof(w);
			}
			remaining -= SDMMC_FIFO_HALF;
			start = xTaskGetTickCount();
		} else if (timed_out(start, SDMMC_DATA_TIMEOUT)) {
			ret = STM32_SDMMC_RET_TIMEOUT;
			goto err;
		}
	}

	ret = sdmmc_wait_data_end(self, SDMMC_STA_RXOVERR);
	if (ret != STM32_SDMMC_RET_OK) {
		goto err;
	}
	if (count > 1) {
		return sdmmc_stop_transfer(self);
	}
	return STM32_SDMMC_RET_OK;

err:
	sdmmc_stop_transfer(self);
	return ret;
}


static stm32_sdmmc_ret_t sdmmc_write_blocks(Stm32Sdmmc *self, uint32_t lba, const uint8_t *buf, size_t count) {
	SDMMC_TypeDef *base = (SDMMC_TypeDef *)self->conf.mmio_base;

	sdmmc_setup_data(self, count, false);
	enum sd_cmd cmd = (count > 1) ? SD_CMD_WRITE_MULTIPLE_BLOCK : SD_CMD_WRITE_BLOCK;
	uint32_t addr = self->high_capacity ? lba : lba * SDMMC_BLOCK_SIZE;
	stm32_sdmmc_ret_t ret = sdmmc_cmd_r1(self, cmd, addr, SDMMC_CMD_CMDTRANS);
	if (ret != STM32_SDMMC_RET_OK) {
		goto err;
	}

	size_t remaining = count * SDMMC_BLOCK_SIZE;
	TickType_t start = xTaskGetTickCount();
	while (remaining > 0) {
		uint32_t sta = base->STA;
		if (sta & SDMMC_STA_TX_ERRORS) {
			break;
		}
		if (sta & SDMMC_STA_TXFIFOHE) {
			for (size_t i = 0; i < SDMMC_FIFO_HALF; i += sizeof(uint32_t)) {
				uint32_t w = 0;
				memcpy(&w, buf, sizeof(w));
				base->FIFO = w;
				buf += sizeof(w);
			}
			remaining -= SDMMC_FIFO_HALF;
			start = xTaskGetTickCount();
		} else if (timed_out(start, SDMMC_DATA_TIMEOUT)) {
			ret = STM32_SDMMC_RET_TIMEOUT;
			goto err;
		}
	}

	ret = sdmmc_wait_data_end(self, SDMMC_STA_TXUNDERR);
	if (ret != STM32_SDMMC_RET_OK) {
		goto err;
	}
	if (count > 1) {
		ret = sdmmc_stop_transfer(self);
		if (ret != STM32_SDMMC_RET_OK) {
			return ret;
		}
	}
	return sdmmc_wait_ready(self);

err:
	sdmmc_stop_transfer(self);
	return ret;
}


/* Identify and initialise a SD card (SDSC, SDHC or SDXC) and put it into the transfer state. */
static stm32_sdmmc_ret_t sdmmc_identify(Stm32Sdmmc *self) {
	SDMMC_TypeDef *base = (SDMMC_TypeDef *)self->conf.mmio_base;
	stm32_sdmmc_ret_t ret = STM32_SDMMC_RET_OK;

	/* Identification runs on a 1-bit bus at 400 kHz. Give the card at least 74 clock cycles before
	 * the first command. */
	self->rca = 0;
	base->CLKCR = SDMMC_CLKCR_HWFC_EN;
	sdmmc_set_clock(self, SDMMC_INIT_FREQ);
	vTaskDelay(2);

	ret = sdmmc_cmd(self, SD_CMD_GO_IDLE_STATE, 0, RESP_NONE);
	if (ret != STM32_SDMMC_RET_OK) {
		return ret;
	}

	/* Version 2.00+ cards echo the CMD8 check pattern, version 1.x cards do not respond at all. */
	bool v2 = false;
	ret = sdmmc_cmd(self, SD_CMD_SEND_IF_COND, SD_IF_COND_ARG, RESP_SHORT);
	if (ret == STM32_SDMMC_RET_OK) {
		if ((base->RESP1 & 0xfff) != SD_IF_COND_ARG) {
			return STM32_SDMMC_RET_NOT_SUPPORTED;
		}
		v2 = true;
	} else if (ret != STM32_SDMMC_RET_TIMEOUT) {
		return ret;
	}

	/* Wait for the card to finish its power up. Only v2 cards may be high capacity. If no card is inserted
	 * (or it is not a SD card), APP_CMD times out. */
	uint32_t ocr = 0;
	TickType_t start = xTaskGetTickCount();
	while (true) {
		ret = sdmmc_cmd_r1(self, SD_CMD_APP_CMD, 0, 0);
		if (ret != STM32_SDMMC_RET_OK) {
			return ret;
		}
		ret = sdmmc_cmd(self, SD_ACMD_SD_SEND_OP_COND, SD_OCR_VOLTAGE_WINDOW | (v2 ? SD_OCR_CCS : 0), RESP_SHORT_NOCRC);
		if (ret != STM32_SDMMC_RET_OK) {
			return ret;
		}
		ocr = base->RESP1;
		if (ocr & SD_OCR_BUSY) {
			break;
		}
		if (timed_out(start, SDMMC_OP_COND_TIMEOUT)) {
			return STM32_SDMMC_RET_TIMEOUT;
		}
		vTaskDelay(pdMS_TO_TICKS(10));
	}
	self->high_capacity = (ocr & SD_OCR_CCS) != 0;

	ret = sdmmc_cmd(self, SD_CMD_ALL_SEND_CID, 0, RESP_LONG);
	if (ret != STM32_SDMMC_RET_OK) {
		return ret;
	}
	/* Manufacturer ID is in CID[127:120], product name in CID[103:64]. */
	char name[6] = {
		(char)(base->RESP1 & 0xff),
		(char)((base->RESP2 >> 24) & 0xff),
		(char)((base->RESP2 >> 16) & 0xff),
		(char)((base->RESP2 >> 8) & 0xff),
		(char)(base->RESP2 & 0xff),
		'\0'
	};
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("manufacturer 0x%02lx, name '%s'"), base->RESP1 >> 24, name);

	ret = sdmmc_cmd(self, SD_CMD_SEND_RELATIVE_ADDR, 0, RESP_SHORT);
	if (ret != STM32_SDMMC_RET_OK) {
		return ret;
	}
	if (base->RESP1 & SD_R6_ERRORS) {
		return STM32_SDMMC_RET_FAILED;
	}
	self->rca = base->RESP1 >> 16;

	ret = sdmmc_cmd(self, SD_CMD_SEND_CSD, self->rca << 16, RESP_LONG);
	if (ret != STM32_SDMMC_RET_OK) {
		return ret;
	}
	/* RESP1..RESP4 hold CSD[127:0], CSD_STRUCTURE is in CSD[127:126]. */
	switch (base->RESP1 >> 30) {
		case 0: {
			/* CSD v1: capacity = (C_SIZE + 1) * 2^(C_SIZE_MULT + 2) * 2^READ_BL_LEN bytes */
			uint32_t read_bl_len = (base->RESP2 >> 16) & 0xf;
			uint32_t c_size = ((base->RESP2 & 0x3ff) << 2) | (base->RESP3 >> 30);
			uint32_t c_size_mult = (base->RESP3 >> 15) & 0x7;
			self->block_count = (c_size + 1) << (c_size_mult + 2 + read_bl_len - SDMMC_BLOCK_SIZE_LOG2);
			break;
		}
		case 1: {
			/* CSD v2: capacity = (C_SIZE + 1) * 512 KiB */
			uint32_t c_size = ((base->RESP2 & 0x3f) << 16) | (base->RESP3 >> 16);
			self->block_count = (c_size + 1) << 10;
			break;
		}
		default:
			return STM32_SDMMC_RET_NOT_SUPPORTED;
	}
	/* PERM_WRITE_PROTECT and TMP_WRITE_PROTECT in CSD[13:12] */
	self->read_only = ((base->RESP4 >> 12) & 0x3) != 0;

	ret = sdmmc_cmd_r1(self, SD_CMD_SELECT_CARD, self->rca << 16, 0);
	if (ret != STM32_SDMMC_RET_OK) {
		return ret;
	}
	ret = sdmmc_wait_busy(self);
	if (ret != STM32_SDMMC_RET_OK) {
		return ret;
	}

	/* SDSC cards have a configurable block length, high capacity cards are fixed to 512 bytes. */
	if (!self->high_capacity) {
		ret = sdmmc_cmd_r1(self, SD_CMD_SET_BLOCKLEN, SDMMC_BLOCK_SIZE, 0);
		if (ret != STM32_SDMMC_RET_OK) {
			return ret;
		}
	}

	if (self->conf.bus_width == STM32_SDMMC_BUS_WIDTH_4) {
		ret = sdmmc_cmd_r1(self, SD_CMD_APP_CMD, self->rca << 16, 0);
		if (ret != STM32_SDMMC_RET_OK) {
			return ret;
		}
		ret = sdmmc_cmd_r1(self, SD_ACMD_SET_BUS_WIDTH, SD_BUS_WIDTH_4, 0);
		if (ret != STM32_SDMMC_RET_OK) {
			return ret;
		}
		base->CLKCR |= SDMMC_CLKCR_WIDBUS_0;
	}

	sdmmc_set_clock(self, self->conf.bus_freq);

	return STM32_SDMMC_RET_OK;
}


static void stm32_sdmmc_task(void *p) {
	Stm32Sdmmc *self = (Stm32Sdmmc *)p;

	self->running = true;
	while (self->can_run) {
		xSemaphoreTake(self->lock, portMAX_DELAY);
		if (self->present) {
			/* A missing response means the card was removed. */
			if (sdmmc_cmd_r1(self, SD_CMD_SEND_STATUS, self->rca << 16, 0) == STM32_SDMMC_RET_TIMEOUT) {
				self->present = false;
				self->changed = true;
				u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("card removed"));
			}
		} else {
			if (sdmmc_identify(self) == STM32_SDMMC_RET_OK) {
				self->present = true;
				self->changed = true;
				u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("%s card inserted, %lu MB, %u-bit bus at %lu kHz%s"),
					self->high_capacity ? "SDHC/SDXC" : "SDSC",
					self->block_count >> (20 - SDMMC_BLOCK_SIZE_LOG2),
					(self->conf.bus_width == STM32_SDMMC_BUS_WIDTH_4) ? 4 : 1,
					self->ck_freq / 1000,
					self->read_only ? ", write protected" : ""
				);
			}
		}
		xSemaphoreGive(self->lock);

		/* Sleep until the next poll. stm32_sdmmc_stop() wakes the task up early. */
		ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(self->conf.poll_interval_ms));
	}
	self->running = false;
	vTaskDelete(NULL);
}


stm32_sdmmc_ret_t stm32_sdmmc_init(Stm32Sdmmc *self, const struct stm32_sdmmc_conf *conf) {
	if (u_assert(self != NULL) ||
	    u_assert(conf != NULL)) {
		return STM32_SDMMC_RET_NULL;
	}

	memset(self, 0, sizeof(Stm32Sdmmc));
	memcpy(&self->conf, conf, sizeof(struct stm32_sdmmc_conf));

	self->block.vmt = &block_vmt;
	self->block.parent = self;

	return STM32_SDMMC_RET_OK;
}


stm32_sdmmc_ret_t stm32_sdmmc_free(Stm32Sdmmc *self) {
	if (u_assert(self != NULL)) {
		return STM32_SDMMC_RET_NULL;
	}

	return STM32_SDMMC_RET_OK;
}


stm32_sdmmc_ret_t stm32_sdmmc_start(Stm32Sdmmc *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->conf.mmio_base != NULL) ||
	    u_assert(self->conf.kernel_freq > 0) ||
	    u_assert(self->conf.bus_freq > 0) ||
	    u_assert(self->conf.bus_freq <= 25000000) ||
	    u_assert(self->conf.poll_interval_ms > 0)) {
		return STM32_SDMMC_RET_FAILED;
	}
	SDMMC_TypeDef *base = (SDMMC_TypeDef *)self->conf.mmio_base;

	self->lock = xSemaphoreCreateMutex();
	if (self->lock == NULL) {
		return STM32_SDMMC_RET_FAILED;
	}

	/* Power the SDMMC peripheral on, the bus clock starts running. */
	base->CLKCR = SDMMC_CLKCR_HWFC_EN;
	sdmmc_set_clock(self, SDMMC_INIT_FREQ);
	base->POWER = SDMMC_POWER_PWRCTRL;

	self->can_run = true;
	if (xTaskCreate(stm32_sdmmc_task, "sdmmc", SDMMC_TASK_STACK_SIZE, (void *)self, 1, &(self->task)) != pdPASS) {
		base->POWER = 0;
		vSemaphoreDelete(self->lock);
		self->lock = NULL;
		return STM32_SDMMC_RET_FAILED;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("started"));
	return STM32_SDMMC_RET_OK;
}


stm32_sdmmc_ret_t stm32_sdmmc_stop(Stm32Sdmmc *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->lock != NULL)) {
		return STM32_SDMMC_RET_FAILED;
	}

	self->can_run = false;
	xTaskNotifyGive(self->task);
	while (self->running) {
		vTaskDelay(1);
	}

	((SDMMC_TypeDef *)self->conf.mmio_base)->POWER = 0;
	self->present = false;
	vSemaphoreDelete(self->lock);
	self->lock = NULL;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("stopped"));
	return STM32_SDMMC_RET_OK;
}


/**********************************************************************************************************************
 * Block device interface implementation
 **********************************************************************************************************************/

static block_ret_t sdmmc_ret_to_block_ret(stm32_sdmmc_ret_t ret) {
	switch (ret) {
		case STM32_SDMMC_RET_OK:
			return BLOCK_RET_OK;
		case STM32_SDMMC_RET_TIMEOUT:
			return BLOCK_RET_TIMEOUT;
		default:
			return BLOCK_RET_FAILED;
	}
}


/* Must be called with the lock held. */
static block_ret_t sdmmc_check_access(Stm32Sdmmc *self, uint32_t lba, size_t count) {
	if (self->changed) {
		return BLOCK_RET_DEVICE_CHANGED;
	}
	if (!self->present) {
		return BLOCK_RET_NO_MEDIA;
	}
	if (lba >= self->block_count || count > self->block_count - lba) {
		return BLOCK_RET_BAD_ARG;
	}
	return BLOCK_RET_OK;
}


static block_ret_t stm32_sdmmc_get_geometry(Block *self, size_t *block_size, uint32_t *block_count) {
	Stm32Sdmmc *sd = (Stm32Sdmmc *)self->parent;

	xSemaphoreTake(sd->lock, portMAX_DELAY);
	sd->changed = false;
	block_ret_t ret = BLOCK_RET_NO_MEDIA;
	if (sd->present) {
		*block_size = SDMMC_BLOCK_SIZE;
		*block_count = sd->block_count;
		ret = BLOCK_RET_OK;
	}
	xSemaphoreGive(sd->lock);

	return ret;
}


static block_ret_t stm32_sdmmc_read(Block *self, uint32_t lba, void *buf, size_t count) {
	Stm32Sdmmc *sd = (Stm32Sdmmc *)self->parent;

	if (buf == NULL || count == 0) {
		return BLOCK_RET_BAD_ARG;
	}

	xSemaphoreTake(sd->lock, portMAX_DELAY);
	block_ret_t ret = sdmmc_check_access(sd, lba, count);
	while (ret == BLOCK_RET_OK && count > 0) {
		size_t n = (count > SDMMC_MAX_BLOCKS) ? SDMMC_MAX_BLOCKS : count;
		ret = sdmmc_ret_to_block_ret(sdmmc_read_blocks(sd, lba, buf, n));
		lba += n;
		buf = (uint8_t *)buf + n * SDMMC_BLOCK_SIZE;
		count -= n;
	}
	xSemaphoreGive(sd->lock);

	return ret;
}


static block_ret_t stm32_sdmmc_write(Block *self, uint32_t lba, const void *buf, size_t count) {
	Stm32Sdmmc *sd = (Stm32Sdmmc *)self->parent;

	if (buf == NULL || count == 0) {
		return BLOCK_RET_BAD_ARG;
	}

	xSemaphoreTake(sd->lock, portMAX_DELAY);
	block_ret_t ret = sdmmc_check_access(sd, lba, count);
	if (ret == BLOCK_RET_OK && sd->read_only) {
		ret = BLOCK_RET_READ_ONLY;
	}
	while (ret == BLOCK_RET_OK && count > 0) {
		size_t n = (count > SDMMC_MAX_BLOCKS) ? SDMMC_MAX_BLOCKS : count;
		ret = sdmmc_ret_to_block_ret(sdmmc_write_blocks(sd, lba, buf, n));
		lba += n;
		buf = (const uint8_t *)buf + n * SDMMC_BLOCK_SIZE;
		count -= n;
	}
	xSemaphoreGive(sd->lock);

	return ret;
}


static block_ret_t stm32_sdmmc_flush(Block *self) {
	Stm32Sdmmc *sd = (Stm32Sdmmc *)self->parent;

	/* There is no write cache in the driver and every write waits for the card to finish programming.
	 * Just make sure the card is idle. */
	xSemaphoreTake(sd->lock, portMAX_DELAY);
	block_ret_t ret = BLOCK_RET_OK;
	if (sd->changed) {
		ret = BLOCK_RET_DEVICE_CHANGED;
	} else if (!sd->present) {
		ret = BLOCK_RET_NO_MEDIA;
	} else {
		ret = sdmmc_ret_to_block_ret(sdmmc_wait_ready(sd));
	}
	xSemaphoreGive(sd->lock);

	return ret;
}


/* The presence flag is read without taking the lock, the identification holding it can take up to a second. The
 * status is unknown while the driver is stopped or stopping, a stop does not mean the card was removed. */
static block_ret_t stm32_sdmmc_get_status(Block *self, struct block_status *status) {
	Stm32Sdmmc *sd = (Stm32Sdmmc *)self->parent;

	if (!sd->can_run) {
		return BLOCK_RET_FAILED;
	}
	status->present = sd->present;
	status->block_size = SDMMC_BLOCK_SIZE;
	status->block_count = sd->present ? sd->block_count : 0;
	return BLOCK_RET_OK;
}


static const struct block_vmt block_vmt = {
	.get_geometry = stm32_sdmmc_get_geometry,
	.read = stm32_sdmmc_read,
	.write = stm32_sdmmc_write,
	.flush = stm32_sdmmc_flush,
	.get_status = stm32_sdmmc_get_status,
};
