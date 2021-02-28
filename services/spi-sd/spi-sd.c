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
#include "spi-sd.h"

#define MODULE_NAME "spi-sd"
#define DEBUG 1

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
		if (status != 0xff) {
			break;
		}
	}
	if (self->debug) {
		u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("status = 0x%02x"), status);
	}
	return status_to_ret(status);
}


static spi_sd_ret_t spi_sd_send_cmd(SpiSd *self, sdmmc_cmd_t cmd, uint32_t arg, uint8_t *response, size_t resp_len, uint32_t timeout) {
	interface_spidev_select(self->spidev);

	/* Wait for the card and send the first part. */
	if (spi_sd_wait_idle(self, timeout) != SPI_SD_RET_OK) {
		return SPI_SD_RET_TIMEOUT;
	}

	spi_sd_ret_t ret = spi_sd_send_raw_cmd_arg(self, (uint8_t)cmd, arg, timeout);
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
				//u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("resp 0x%02x"), resp);
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
	sdmmc_type_t type = 0;

	/* Release the chip select first and output some dummy clocks to switch to SPI mode. */
	interface_spidev_deselect(self->spidev);
	for (uint32_t i = 0; i < 10; i++) {
		uint8_t dummy = 0xff;
		interface_spidev_send(self->spidev, &dummy, sizeof(dummy));
	}
	vTaskDelay(10);

	if (spi_sd_send_cmd(self, SDMMC_CMD_GO_IDLE_STATE, 0, NULL, 0, timeout) != SPI_SD_RET_IDLE) {
		goto ret;
	}

	uint8_t ret[4] = {0};
	if (spi_sd_send_cmd(self, SDMMC_CMD_SEND_IF_COND, 0x1aa, ret, sizeof(ret), timeout) == SPI_SD_RET_IDLE) {
		type = SDMMC_TYPE_SDC2;
		if (ret[2] != 0x01 || ret[3] != 0xaa) {
			goto ret;
		}
	} else {
		goto ret;
	}

	uint32_t i = timeout;
	while (i--) {
		if (!i) {
			type = SDMMC_TYPE_NONE;
			goto ret;
		}
		if (spi_sd_send_cmd(self, SDMMC_CMD_SEND_OP_COND, 1UL << 30, NULL, 0, timeout) == SPI_SD_RET_OK) {
			break;
		}
		vTaskDelay(1000);
	}

	if (spi_sd_send_cmd(self, SDMMC_CMD_READ_OCR, 0, ret, sizeof(ret), timeout) == SPI_SD_RET_OK) {
		/* Check if 2.8-2.9 V range is supported. */
		if ((ret[1] & 0x01) == 0) {
			type = SDMMC_TYPE_NONE;
			goto ret;
		}
		if (ret[0] & 0x40) {
			/* SDHC */
			type |= SDMMC_TYPE_BLOCK;
		}
	}

	if (spi_sd_send_cmd(self, SDMMC_CMD_SEND_CID, 0, self->cid, 16, timeout) != SPI_SD_RET_OK) {
		if (self->debug) {
			u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("cannot read CID"));
		}
		goto ret;
	}

	if (spi_sd_send_cmd(self, SDMMC_CMD_SEND_CSD, 0, self->csd, 16, timeout) != SPI_SD_RET_OK) {
		if (self->debug) {
			u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("cannot read CSD"));
		}
		goto ret;
	}

ret:
	if (type == SDMMC_TYPE_NONE) {
		return SPI_SD_RET_FAILED;
	} else {
		self->type = type;
		return SPI_SD_RET_OK;
	}
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

	if (spi_sd_init_card(self, 100) != SPI_SD_RET_OK) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("SD card detection failed"));
		return SPI_SD_RET_FAILED;
	}

	self->product_info = (struct spi_sd_product_info *)self->cid;
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("SD card detected, type %s"), type_to_str(self->type));
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
		uint32_t size = (((self->csd[7] << 16) | (self->csd[8] << 8) | self->csd[9]) & ((1UL << 22) - 1)) / 2;
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("CSD v2: size %lu MB"),
			size
		);


	}

	return SPI_SD_RET_OK;
}


spi_sd_ret_t spi_sd_free(SpiSd *self) {
	(void)self;
	return SPI_SD_RET_OK;
}

