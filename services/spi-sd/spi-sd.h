/* SPDX-License-Identifier: BSD-2-Clause
 *
 * SD/MMC driver using the SPI interface
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <interface_spidev.h>
#include <interfaces/block.h>


typedef enum {
	SDMMC_CMD_GO_IDLE_STATE = 0,	/* GO_IDLE_STATE */
	SDMMC_CMD_SEND_OP_COND_MMC = 1,	/* SEND_OP_COND */
	SDMMC_CMD_SEND_OP_COND = 0x80 + 41,	/* SEND_OP_COND (SDC) */
	SDMMC_CMD_SEND_IF_COND = 8,	/* SEND_IF_COND */
	SDMMC_CMD_SEND_CSD = 9,		/* SEND_CSD */
	SDMMC_CMD_SEND_CID = 10,	/* SEND_CID */
	SDMMC_CMD12 = 12,		/* STOP_TRANSMISSION */
	SDMMC_CMD13 = 13,		/* SEND_STATUS */
	SDMMC_ACMD13 = 0x80 + 13,	/* SD_STATUS (SDC) */
	SDMMC_CMD_SET_BLOCKLEN = 16,	/* SET_BLOCKLEN */
	SDMMC_CMD_READ_SINGLE_BLOCK = 17,/* READ_SINGLE_BLOCK */
	SDMMC_CMD18 = 18,		/* READ_MULTIPLE_BLOCK */
	SDMMC_CMD23 = 23,		/* SET_BLOCK_COUNT */
	SDMMC_ACMD23 = 0x80 + 23,	/* SET_WR_BLK_ERASE_COUNT (SDC) */
	SDMMC_CMD_WRITE_SINGLE_BLOCK = 24,/* WRITE_BLOCK */
	SDMMC_CMD_WRITE_MULTIPLE_BLOCK = 25,/* WRITE_MULTIPLE_BLOCK */
	SDMMC_CMD32 = 32,		/* ERASE_ER_BLK_START */
	SDMMC_CMD33 = 33,		/* ERASE_ER_BLK_END */
	SDMMC_CMD38 = 38,		/* ERASE */
	SDMMC_CMD55 = 55,		/* APP_CMD */
	SDMMC_CMD_READ_OCR = 58,	/* READ_OCR */
} sdmmc_cmd_t;

typedef enum {
	SDMMC_TYPE_NONE = 0x00,
	SDMMC_TYPE_MMC3 = 0x01,			/* MMC ver 3 */
	SDMMC_TYPE_MMC4 = 0x02,			/* MMC ver 4+ */
	SDMMC_TYPE_MMC = 0x03,			/* MMC */
	SDMMC_TYPE_SDC1 = 0x04,			/* SD ver 1 */
	SDMMC_TYPE_SDC2 = 0x08,			/* SD ver 2+ */
	SDMMC_TYPE_SDC = 0x0C,			/* SD */
	SDMMC_TYPE_BLOCK = 0x10,		/* Block addressing */
} sdmmc_type_t;

typedef enum {
	SPI_SD_RET_OK = 0,
	SPI_SD_RET_FAILED = -1,
	SPI_SD_RET_TIMEOUT = -2,
	SPI_SD_RET_IDLE = -3,
	SPI_SD_RET_NO_CARD = -4,
	SPI_SD_RET_NOT_COMPATIBLE = -5,
} spi_sd_ret_t;

#define SDMMC_SEND_OP_COND_TIMEOUT 10
#define SDMMC_SEND_OP_COND_WAIT_MS 100

struct __attribute__((__packed__)) spi_sd_product_info {
	uint8_t man_id;
	uint16_t app_id;
	char product_name[5];
	uint8_t product_revision;
	uint32_t product_sn;
	//uint8_t reserved :4;
	//uint16_t man_date :12;
	uint16_t man_date;
	uint8_t crc;
};

typedef struct {
	struct interface_spidev *spidev;
	bool debug;
	uint8_t addr_divider_bits;
	uint8_t cid[16];
	uint8_t csd[16];
	struct spi_sd_product_info *product_info;
	size_t size_blocks;

	/* Block device interface instance */
	Block block;
} SpiSd;


spi_sd_ret_t spi_sd_init(SpiSd *self, struct interface_spidev *spidev);
spi_sd_ret_t spi_sd_free(SpiSd *self);

spi_sd_ret_t spi_sd_go_idle(SpiSd *self);
spi_sd_ret_t spi_sd_read_data(SpiSd *self, uint32_t block, uint8_t buf[512], size_t len, uint32_t timeout);
spi_sd_ret_t spi_sd_write_data(SpiSd *self, uint32_t block, const uint8_t buf[512], size_t len, uint32_t timeout);


block_ret_t spi_sd_get_block_size(SpiSd *self, size_t *block_size);
block_ret_t spi_sd_get_size(SpiSd *self, size_t *block_size);
block_ret_t spi_sd_read(SpiSd *self, size_t block, uint8_t *buf);
block_ret_t spi_sd_write(SpiSd *self, size_t block, const uint8_t *buf);
