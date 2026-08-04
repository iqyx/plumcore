/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STMicroelectronics ST67W611M1 low-power Wi-Fi/BLE combo module driver
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <main.h>

#include <interfaces/spi.h>
#include <interfaces/gpio.h>

#define ST67W611_RESP_MAX 512

typedef enum {
	ST67W611_RET_OK = 0,
	ST67W611_RET_FAILED,
	ST67W611_RET_TIMEOUT,
	ST67W611_RET_OVERFLOW,
	ST67W611_RET_NULL,
} st67w611_ret_t;

/* Service configuration passed to st67w611_init(). Not typedef'd per project policy. */
struct st67w611_conf {
	/** SPI device (chip-select bound) used for the module's mission-mode SPI interface. */
	SpiDev *spidev;
	/** CHIP_EN power-on control, driven high to enable the module. */
	Gpio *en_gpio;
	/** BOOT bootstrap select, sampled at power-on. Driven low to boot from SPI. */
	Gpio *boot_gpio;
	/** SPI_RDY handshake line, an input asserted high by the module. */
	Gpio *rdy_gpio;
};

typedef struct {
	struct st67w611_conf conf;

	/* Background task draining module-initiated frames, and its keep-running flag. */
	TaskHandle_t rx_task;
	volatile bool rx_task_running;

	/* cmd_lock serializes commands so only one is in flight at a time. comm_lock serializes the actual
	 * SPI transactions between the command sender and the background receiver. cmd_sem is given by the
	 * receiver once the running command's response has been captured, unblocking the sender. */
	SemaphoreHandle_t cmd_lock;
	SemaphoreHandle_t comm_lock;
	SemaphoreHandle_t cmd_sem;

	/* Shared state of the command currently awaiting a response. Written by the sender before the
	 * command goes out, consulted and completed by the background task. */
	volatile bool cmd_running;
	uint8_t resp_buf[ST67W611_RESP_MAX];
	size_t resp_len;
	bool resp_overflow;

	/* Scratch buffer the background task receives frames into. */
	uint8_t rx_buf[ST67W611_RESP_MAX];
} St67w611;


st67w611_ret_t st67w611_init(St67w611 *self, const struct st67w611_conf *conf);
st67w611_ret_t st67w611_free(St67w611 *self);

/* Send an AT command and return its response. Blocks until the module answers or the command times
 * out. Only one command runs at a time; concurrent callers are serialized. The response text (module
 * output up to and including the final OK/ERROR result) is copied into @p resp (NUL-terminated, at most
 * @p resp_size bytes) and its length returned via @p resp_len; both may be NULL if not needed. */
st67w611_ret_t st67w611_command(St67w611 *self, const char *cmd, char *resp, size_t resp_size, size_t *resp_len);

/* Bring BLE up in the client role, set the advertised device name to @p name and start advertising.
 * Runs the AT+BLEINIT / AT+BLENAME / AT+BLEADVSTART sequence, stopping at the first command the module
 * does not acknowledge. */
st67w611_ret_t st67w611_ble_advertise(St67w611 *self, const char *name);
