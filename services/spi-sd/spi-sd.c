/* SPDX-License-Identifier: BSD-2-Clause
 *
 * SD/MMC driver using the SPI interface
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include "config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "u_log.h"
#include "u_assert.h"

#include <interface_spidev.h>
#include <interfaces/block.h>
#include "spi-sd.h"

#define MODULE_NAME "spi-sd"
#define DEBUG 1


static block_vmt_t block_vmt;

static spi_sd_ret_t spi_sd_wait_idle(SpiSd *self, uint32_t timeout_ms) {
	uint8_t dummy = 0xff;
	for (uint32_t i = 0; i < (timeout_ms / 2); i++) {
		uint8_t rxbuf = 0;
		interface_spidev_exchange(self->spidev, &dummy, &rxbuf, 1);
		if (rxbuf == 0xff) {
			return SPI_SD_RET_OK;
		}
		//vTaskDelay(2);
	}
	return SPI_SD_RET_TIMEOUT;
}


static spi_sd_ret_t status_to_ret(uint8_t status) {
	switch (status) {
		case 0x00:
			return SPI_SD_RET_OK;
			break;
		case 0x01:
			return SPI_SD_RET_IDLE;
			break;
		case 0xff:
			return SPI_SD_RET_TIMEOUT;
			break;
		default:
			return SPI_SD_RET_FAILED;
	}
}


static spi_sd_ret_t spi_sd_send_raw_cmd_arg(SpiSd *self, sdmmc_cmd_t cmd, uint32_t arg, uint32_t timeout_cycles) {
	/* Send CMD55 if the command is ACMD. */
	if (cmd & 0x80) {
		cmd &= 0x7f;
		spi_sd_ret_t ret = spi_sd_send_raw_cmd_arg(self, 55, 0UL, timeout_cycles);
		if (ret != SPI_SD_RET_OK && ret != SPI_SD_RET_IDLE) {
			return ret;
		}
	}

	/* Send the actual command bytes with crc. */
	uint8_t crc = 0x01;
	if (cmd == SDMMC_CMD_GO_IDLE_STATE) {
		crc = 0x95;
	}
	if (cmd == SDMMC_CMD_SEND_IF_COND) {
		crc = 0x87;
	}
	uint8_t txbuf[6] = {
		cmd | 0x40,
		(arg >> 24) & 0xff,
		(arg >> 16) & 0xff,
		(arg >> 8) & 0xff,
		arg & 0xff,
		crc
	};
	if (self->debug) {
		u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("cmd %02x %02x %02x %02x %02x %02x"), txbuf[0], txbuf[1], txbuf[2], txbuf[3], txbuf[4], txbuf[5]);
	}
	interface_spidev_send(self->spidev, txbuf, 6);

	/* Wait for the reseponse. */
	uint8_t status = 0;
	for (uint32_t i = 0; i < timeout_cycles; i++) {
		/* Correct response is the first non-0xff byte read. */
		uint8_t dummy = 0xff;
		interface_spidev_exchange(self->spidev, &dummy, &status, sizeof(status));
		if (!(status & 0x80)) {
			break;
		}
	}

	if (self->debug) {
		u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("status = 0x%02x"), status);
	}
	return status_to_ret(status);
}


static spi_sd_ret_t spi_sd_send_cmd(SpiSd *self, sdmmc_cmd_t cmd, uint32_t arg, uint8_t *response, size_t resp_len, uint32_t timeout) {
	for (uint32_t i = 0; i < 4; i++) {
		uint8_t dummy = 0xff;
		interface_spidev_send(self->spidev, &dummy, sizeof(dummy));
	}

	interface_spidev_select(self->spidev);

	/* Wait for the card and send the first part. */
	if (spi_sd_wait_idle(self, timeout) != SPI_SD_RET_OK) {
		return SPI_SD_RET_TIMEOUT;
	}

	spi_sd_ret_t ret = spi_sd_send_raw_cmd_arg(self, (uint8_t)cmd, arg, timeout);
	// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("som tu 0x%02x ret 0x%02x"), cmd, ret);
	if (ret != SPI_SD_RET_OK && ret != SPI_SD_RET_IDLE) {
		goto ret;
	}

	/* Some commands have responses other than R1. */
	if (cmd == SDMMC_CMD_SEND_IF_COND || cmd == SDMMC_CMD_READ_OCR) {
		const uint8_t dummy[4] = {0xff, 0xff, 0xff, 0xff};
		uint8_t read[4] = {0};
		interface_spidev_exchange(self->spidev, dummy, read, sizeof(read));

		if (response != NULL) {
			if (resp_len < sizeof(read)) {
				return SPI_SD_RET_FAILED;
			}
			memcpy(response, read, sizeof(read));
		}
	}

	if (cmd == SDMMC_CMD_READ_SINGLE_BLOCK || cmd == SDMMC_CMD_SEND_CID || cmd == SDMMC_CMD_SEND_CSD) {
		uint8_t dummy[2] = {0xff, 0xff};
		uint8_t token = 0;
		for (uint32_t i = 0; i < timeout; i++) {
			interface_spidev_exchange(self->spidev, dummy, &token, sizeof(token));
			if (token != 0xff) {
				if (token != 0xfe) {
					ret = SPI_SD_RET_FAILED;
					goto ret;
				}
				interface_spidev_receive(self->spidev, response, resp_len);
				uint16_t crc = 0;
				interface_spidev_exchange(self->spidev, dummy, (uint8_t *)&crc, sizeof(crc));
				goto ret;
			}
		}
		ret = SPI_SD_RET_TIMEOUT;
		goto ret;
	}

	if (cmd == SDMMC_CMD_WRITE_SINGLE_BLOCK) {
		/* Wait a single dummy byte. */
		const uint8_t dummy = 0xff;
		interface_spidev_send(self->spidev, &dummy, sizeof(dummy));

		const uint8_t token = 0xfe;
		interface_spidev_send(self->spidev, &token, sizeof(token));

		/* Send block of data. */
		interface_spidev_send(self->spidev, response, resp_len);

		uint16_t crc = 0xffff;
		interface_spidev_send(self->spidev, (uint8_t *)&crc, sizeof(crc));

		uint8_t resp = 0;
		interface_spidev_receive(self->spidev, &resp, sizeof(resp));
		if ((resp & 0x1f) != 0x05) {
			ret = SPI_SD_RET_FAILED;
		}
		goto ret;
	}

	if (cmd == SDMMC_CMD_WRITE_MULTIPLE_BLOCK) {
		/* Wait a single dummy byte. */
		const uint8_t dummy = 0xff;
		interface_spidev_send(self->spidev, &dummy, sizeof(dummy));

		size_t blocks = resp_len / 512;
		for (size_t i = 0; i < blocks; i++) {
			const uint8_t token = 0xfc;
			interface_spidev_send(self->spidev, &token, sizeof(token));

			/* Send block of data. */
			interface_spidev_send(self->spidev, response + i * 512, 512);

			uint16_t crc = 0xffff;
			interface_spidev_send(self->spidev, (uint8_t *)&crc, sizeof(crc));

			uint8_t resp = 0;
			interface_spidev_receive(self->spidev, &resp, sizeof(resp));
			if ((resp & 0x1f) != 0x05) {
				// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("resp 0x%02x"), resp);
				ret = SPI_SD_RET_FAILED;
				goto ret;
			}
			if (spi_sd_wait_idle(self, timeout) != SPI_SD_RET_OK) {
				ret = SPI_SD_RET_TIMEOUT;
				goto ret;
			}

		}
		const uint8_t token = 0xfd;
		interface_spidev_send(self->spidev, &token, sizeof(token));
		interface_spidev_send(self->spidev, &dummy, sizeof(dummy));
		goto ret;
	}

ret:
	interface_spidev_deselect(self->spidev);
	return ret;
}


spi_sd_ret_t spi_sd_go_idle(SpiSd *self) {
	return spi_sd_send_cmd(self, SDMMC_CMD_GO_IDLE_STATE, 0, NULL, 0, 1000);
}


spi_sd_ret_t spi_sd_read_data(SpiSd *self, uint32_t block, uint8_t buf[512], size_t len, uint32_t timeout) {
	return spi_sd_send_cmd(self, SDMMC_CMD_READ_SINGLE_BLOCK, block, buf, len, timeout);
}


spi_sd_ret_t spi_sd_write_data(SpiSd *self, uint32_t block, const uint8_t buf[512], size_t len, uint32_t timeout) {
	if (len == 512) {
		return spi_sd_send_cmd(self, SDMMC_CMD_WRITE_SINGLE_BLOCK, block, buf, len, timeout);
	} else {
		return spi_sd_send_cmd(self, SDMMC_CMD_WRITE_MULTIPLE_BLOCK, block, buf, len, timeout);
	}
}


static spi_sd_ret_t spi_sd_init_card(SpiSd *self, uint32_t timeout) {
	/* Release the chip select first and output some dummy clocks to switch to SPI mode. */
	interface_spidev_deselect(self->spidev);
	for (uint32_t i = 0; i < 10; i++) {
		uint8_t dummy = 0xff;
		interface_spidev_send(self->spidev, &dummy, sizeof(dummy));
	}
	vTaskDelay(10);

	/* Reset the card in SPI mode */
	if (spi_sd_send_cmd(self, SDMMC_CMD_GO_IDLE_STATE, 0, NULL, 0, timeout) != SPI_SD_RET_IDLE) {
		/* The card is not responding. Keep type NONE and return. */
		return SPI_SD_RET_NO_CARD;
	}

	/* Check the voltage range */
	uint8_t read[4] = {0};
	if (spi_sd_send_cmd(self, SDMMC_CMD_SEND_IF_COND, 0x1aa, read, sizeof(read), timeout) == SPI_SD_RET_IDLE) {
		if (read[2] != 0x01 || read[3] != 0xaa) {
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("invalid voltage range"));
			return SPI_SD_RET_NOT_COMPATIBLE;
		}
	}

	/* Try to left the idle state using CMD1 first. */
	bool retry = false;
	uint32_t i = SDMMC_SEND_OP_COND_TIMEOUT;
	while (i--) {
		if (!i) {
			retry = true;
		}
		if (spi_sd_send_cmd(self, SDMMC_CMD_SEND_OP_COND_MMC, 1UL << 30, NULL, 0, timeout) == SPI_SD_RET_OK) {
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("MMC3 initialization succeeded"));
			break;
		}
		vTaskDelay(SDMMC_SEND_OP_COND_WAIT_MS);
	}
	if (retry) {
		/* If it fails, try ACMD41 */
		i = SDMMC_SEND_OP_COND_TIMEOUT;
		while (i--) {
			if (!i) {
				u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("cannot left idle state"));
				return SPI_SD_RET_TIMEOUT;
			}
			if (spi_sd_send_cmd(self, SDMMC_CMD_SEND_OP_COND, 1UL << 30, NULL, 0, timeout) == SPI_SD_RET_OK) {
				u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("SDCv2 initialization succeeded"));
				break;
			}
			vTaskDelay(SDMMC_SEND_OP_COND_WAIT_MS);
		}
	}

	if (spi_sd_send_cmd(self, SDMMC_CMD_READ_OCR, 0, read, sizeof(read), timeout) == SPI_SD_RET_OK) {
		/* Check if 2.8-2.9 V range is supported. */
		if ((read[1] & 0x01) == 0) {
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("invalid voltage range"));
			return SPI_SD_RET_NOT_COMPATIBLE;
		}
		if (read[0] & 0x40) {
			/* SDHC, block addressing */
			self->addr_divider_bits = 9;
		} else {
			/* byte addressing */
			self->addr_divider_bits = 0;
		}
	} else {
		return SPI_SD_RET_FAILED;
	}

	if (spi_sd_send_cmd(self, SDMMC_CMD_SET_BLOCKLEN, 512, NULL, 0, timeout) != SPI_SD_RET_OK) {
		return SPI_SD_RET_FAILED;
	}

	if (spi_sd_send_cmd(self, SDMMC_CMD_SEND_CSD, 0, self->csd, 16, timeout) != SPI_SD_RET_OK) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("cannot read CSD"));
		return SPI_SD_RET_FAILED;
	}

	if (spi_sd_send_cmd(self, SDMMC_CMD_SEND_CID, 0, self->cid, 16, timeout) != SPI_SD_RET_OK) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("cannot read CID"));
		return SPI_SD_RET_FAILED;
	}

	return SPI_SD_RET_OK;
}


static const char *type_to_str(sdmmc_type_t type) {
	switch (type & 0x0f) {
		case SDMMC_TYPE_MMC3: return "MMC3";
		case SDMMC_TYPE_MMC4: return "MMC4";
		case SDMMC_TYPE_MMC: return "MMC";
		case SDMMC_TYPE_SDC1: return "SDC1";
		case SDMMC_TYPE_SDC2: return "SDC2";
		case SDMMC_TYPE_SDC: return "SDC";
		default: return "unknown";
	}
}


static uint16_t swap_uint16(uint16_t val) {
    return (val << 8) | (val >> 8);
}


static uint32_t swap_uint32(uint32_t val) {
    val = ((val << 8) & 0xFF00FF00) | ((val >> 8) & 0xFF00FF);
    return (val << 16) | (val >> 16);
}


spi_sd_ret_t spi_sd_init(SpiSd *self, struct interface_spidev *spidev) {
	memset(self, 0, sizeof(SpiSd));
	self->spidev = spidev;

	if (spi_sd_init_card(self, 10000) != SPI_SD_RET_OK) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("SD card detection failed"));
		return SPI_SD_RET_FAILED;
	}

	self->product_info = (struct spi_sd_product_info *)self->cid;
	char name[6] = {0};
	strncpy(name, self->product_info->product_name, 5);
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("manufacturer 0x%02x, app 0x%04x, name '%s'"),
		self->product_info->man_id,
		self->product_info->app_id,
		name
	);
	uint16_t date = swap_uint16(self->product_info->man_date);
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("revision %d.%d, serial 0x%08x, date %u-%02u"),
		self->product_info->product_revision >> 4,
		self->product_info->product_revision & 0x0f,
		self->product_info->product_sn,
		((date >> 4) & 0xff) + 2000,
		date & 0x0f
	);

	if ((self->csd[0] & 0xc0) == 0x40) {
		self->size_blocks = (((self->csd[7] << 16) | (self->csd[8] << 8) | self->csd[9]) & ((1UL << 22) - 1)) << 10;
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("CSD v2: size %lu blocks, %lu MB"),
			self->size_blocks,
			self->size_blocks >> 11
		);
	}

	/* Block device interface implementation */
	block_init(&self->block, &block_vmt);
	self->block.parent = self;

	return SPI_SD_RET_OK;
}


spi_sd_ret_t spi_sd_free(SpiSd *self) {
	block_free(&self->block);
	return SPI_SD_RET_OK;
}

/*********************************************************************************
  Block device interface implementation
*********************************************************************************/

/** @todo lock the SD card on access! */

static block_vmt_t block_vmt = {
	.get_block_size = spi_sd_get_block_size,
	.set_block_size = NULL,
	.get_size = spi_sd_get_size,
	.read = spi_sd_read,
	.write = spi_sd_write,
	.flush = NULL,
};


block_ret_t spi_sd_get_block_size(SpiSd *self, size_t *block_size) {
	*block_size = 512;
	return BLOCK_RET_OK;
}


block_ret_t spi_sd_get_size(SpiSd *self, size_t *block_size) {
	*block_size = self->size_blocks;
	return BLOCK_RET_OK;
}


block_ret_t spi_sd_read(SpiSd *self, size_t block, uint8_t *buf, size_t len) {

	return BLOCK_RET_OK;
}


block_ret_t spi_sd_write(SpiSd *self, size_t block, const uint8_t *buf, size_t len) {

	return BLOCK_RET_OK;
}


