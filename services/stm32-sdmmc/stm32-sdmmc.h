/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 SDMMC SD card driver
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <main.h>

#include <interfaces/block.h>


typedef enum {
	STM32_SDMMC_RET_OK = 0,
	STM32_SDMMC_RET_FAILED,
	STM32_SDMMC_RET_NULL,
	STM32_SDMMC_RET_TIMEOUT,
	STM32_SDMMC_RET_CRC,
	STM32_SDMMC_RET_NOT_SUPPORTED,
} stm32_sdmmc_ret_t;

enum stm32_sdmmc_bus_width {
	STM32_SDMMC_BUS_WIDTH_1 = 0,
	STM32_SDMMC_BUS_WIDTH_4,
};

/* Service configuration passed to stm32_sdmmc_init(). */
struct stm32_sdmmc_conf {
	/** Base address of the SDMMC peripheral registers. */
	void *mmio_base;
	/** SDMMC kernel clock frequency in Hz. */
	uint32_t kernel_freq;
	/** SDMMC_CK frequency used for data transfers in Hz. At most 25 MHz (default speed mode). */
	uint32_t bus_freq;
	/** Data bus width used after the card is identified. */
	enum stm32_sdmmc_bus_width bus_width;
	/** Card insertion/removal polling interval in milliseconds. */
	uint32_t poll_interval_ms;
};

typedef struct {
	struct stm32_sdmmc_conf conf;

	/* Block device interface of the card. */
	Block block;

	SemaphoreHandle_t lock;
	TaskHandle_t task;

	/* Set to false to request thread stop */
	volatile bool can_run;

	/* True if the thread is running */
	volatile bool running;

	/* Card state. The remaining fields are valid only if a card is present. */
	bool present;
	bool changed;
	bool high_capacity;
	bool read_only;
	uint32_t rca;
	uint32_t block_count;
	uint32_t ck_freq;
} Stm32Sdmmc;


/**
 * @brief Initialise the SDMMC SD card driver instance
 *
 * The caller is responsible for powering the card, selecting and enabling the SDMMC kernel clock and configuring
 * the pins. The card is detected by polling once the service is started.
 */
stm32_sdmmc_ret_t stm32_sdmmc_init(Stm32Sdmmc *self, const struct stm32_sdmmc_conf *conf);
stm32_sdmmc_ret_t stm32_sdmmc_free(Stm32Sdmmc *self);
stm32_sdmmc_ret_t stm32_sdmmc_start(Stm32Sdmmc *self);
stm32_sdmmc_ret_t stm32_sdmmc_stop(Stm32Sdmmc *self);
