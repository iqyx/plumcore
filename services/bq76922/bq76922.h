/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * BQ76922 battery gauge driver service
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <i2c-bus.h>

#include <main.h>

#include <interfaces/sensor.h>

typedef enum {
	BQ76922_CMD_CONTROL = 0x00,

	BQ76922_CMD_MAC = 0x3e,
	BQ76922_CMD_MAC_DATA = 0x40,
	BQ76922_CMD_MAC_DATA_SUM = 0x60,
	BQ76922_CMD_MAC_DATA_LEN = 0x61,
} bq76922_cmd_t;

typedef enum {
	BQ76922_RET_OK = 0,
	BQ76922_RET_FAILED = -1,
} bq76922_ret_t;

typedef enum {
	BQ76922_CONTROL_DEVICENUMBER = 0x0001,
	BQ76922_CONTROL_FW_VERSION = 0x0002,
	BQ76922_CONTROL_HW_VERSION = 0x0003,
	BQ76922_CONTROL_RESET = 0x0012,
	BQ76922_CONTROL_SET_CFGUPDATE = 0x0090,
	BQ76922_CONTROL_EXIT_CFGUPDATE = 0x0092,
	BQ76922_CONTROL_SLEEP_DISABLE = 0x009a,
	BQ76922_CONTROL_SCDL_RECOVER = 0x009C,

	BQ76922_CONTROL_EXIT_DEEPSLEEP = 0x000e,
	BQ76922_CONTROL_FET_ENABLE = 0x0022,
	BQ76922_CONTROL_ALL_FETS_ON = 0x0096,

	BQ76922_DATA_OCC_THRESHOLD = 0x9280,
	BQ76922_DATA_VCELL_MODE = 0x9304,
	BQ76922_DATA_OCC_RECOVERY_THRESHOLD = 0x9288,
} bq76922_control_t;


typedef struct {
	I2cBus *i2c;
	uint8_t who_am_i;

} Bq76922;


bq76922_ret_t bq76922_mac(Bq76922 *self, bq76922_control_t control, const uint8_t *txdata, size_t txlen, uint8_t *rxdata, size_t rxlen);
bq76922_ret_t bq76922_init(Bq76922 *self, I2cBus *i2c);
bq76922_ret_t bq76922_free(Bq76922 *self);


