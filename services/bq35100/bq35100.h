/* SPDX-License-Identifier: BSD-2-Clause
 *
 * BQ35100 primary battery gauge driver service
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <i2c-bus.h>

typedef enum {
	BQ35100_CMD_CONTROL = 0x00,
	BQ35100_CMD_ACCUMULATED_CAPACITY = 0x02,
	BQ35100_CMD_TEMPERATURE = 0x06,
	BQ35100_CMD_VOLTAGE = 0x08,
	BQ35100_CMD_BATTERY_STATUS = 0x0a,
	BQ35100_CMD_BATTERY_ALERT = 0x0b,
	BQ35100_CMD_CURRENT = 0x0c,
	BQ35100_CMD_SCALED_R = 0x16,
	BQ35100_CMD_MEASURED_Z = 0x22,
	BQ35100_CMD_INT_TEMPERATURE = 0x28,
	BQ35100_CMD_STATE_OF_HEALTH = 0x2e,
	BQ35100_CMD_DESIGN_CAPACITY = 0x3c,
	BQ35100_CMD_MAC = 0x3e,
	BQ35100_CMD_MAC_DATA = 0x40,
	BQ35100_CMD_MAC_DATA_SUM = 0x60,
	BQ35100_CMD_MAC_DATA_LEN = 0x61,
	BQ35100_CMD_CAL_COUNT = 0x79,
	BQ35100_CMD_CAL_CURRENT = 0x7a,
	BQ35100_CMD_CAL_VOLTAGE = 0x7c,
	BQ35100_CMD_CAL_TEMPERATURE = 0x7e,
} bq35100_cmd_t;

typedef enum {
	BQ35100_RET_OK = 0,
	BQ35100_RET_FAILED = -1,
} bq35100_ret_t;

typedef enum {
	BQ35100_CONTROL_STATUS = 0x0000,
	BQ35100_CONTROL_DEVICE_TYPE = 0x0001,
	BQ35100_CONTROL_FW_VERSION = 0x0002,
	BQ35100_CONTROL_HW_VERSION = 0x0003,
	BQ35100_CONTROL_STATIC_CHEM_CHKSUM = 0x0005,
	BQ35100_CONTROL_CHEM_ID = 0x0006,
	BQ35100_CONTROL_PREV_MACWRITE = 0x0007,
	BQ35100_CONTROL_BOARD_OFFSET = 0x0009,
	BQ35100_CONTROL_CC_OFFSET = 0x000a,
	BQ35100_CONTROL_CC_OFFSET_SAVE = 0x000b,
	BQ35100_CONTROL_GAUGE_START = 0x0011,
	BQ35100_CONTROL_GAUGE_STOP = 0x0012,
	BQ35100_CONTROL_SEALED = 0x0020,
	BQ35100_CONTROL_CAL_ENABLE = 0x002d,
	BQ35100_CONTROL_LT_ENABLE = 0x002e,
	BQ35100_CONTROL_RESET = 0x0041,
	BQ35100_CONTROL_EXIT_CAL = 0x0080,
	BQ35100_CONTROL_ENTER_CAL = 0x0081,
	BQ35100_CONTROL_NEW_BATTERY = 0xa613,
} bq35100_control_t;

#define BQ35100_CONTROL_STATUS_CAL_MODE (1 << 12)
#define BQ35100_CONTROL_STATUS_CCA (1 << 10)


typedef struct {
	I2cBus *i2c;
	uint8_t who_am_i;
} Bq35100;


bq35100_ret_t bq35100_read_uint8(Bq35100 *self, bq35100_cmd_t reg, uint8_t *val);
bq35100_ret_t bq35100_read_int16(Bq35100 *self, bq35100_cmd_t reg, int16_t *val);
bq35100_ret_t bq35100_read_uint16(Bq35100 *self, bq35100_cmd_t reg, uint16_t *val);
bq35100_ret_t bq35100_read_int32(Bq35100 *self, bq35100_cmd_t reg, int32_t *val);
bq35100_ret_t bq35100_read_uint32(Bq35100 *self, bq35100_cmd_t reg, uint32_t *val);
bq35100_ret_t bq35100_read_data(Bq35100 *self, bq35100_cmd_t reg, uint8_t *data, size_t len);

bq35100_ret_t bq35100_write_uint8(Bq35100 *self, bq35100_cmd_t reg, uint8_t val);
bq35100_ret_t bq35100_write_uint16(Bq35100 *self, bq35100_cmd_t reg, uint16_t val);
bq35100_ret_t bq35100_write_data(Bq35100 *self, bq35100_cmd_t reg, const uint8_t *data, size_t len);

bq35100_ret_t bq35100_mac(Bq35100 *self, bq35100_control_t control, const uint8_t *txdata, size_t txlen, uint8_t *rxdata, size_t rxlen);
bq35100_ret_t bq35100_gauge_start(Bq35100 *self);
int32_t bq35100_capacity(Bq35100 *self);
uint16_t bq35100_voltage(Bq35100 *self);
int16_t bq35100_temperature(Bq35100 *self);
int16_t bq35100_current(Bq35100 *self);
bq35100_ret_t bq35100_set_cc_gain(Bq35100 *self, float calib);
uint8_t bq35100_battery_status(Bq35100 *self);

bq35100_ret_t bq35100_init(Bq35100 *self, I2cBus *i2c);
bq35100_ret_t bq35100_free(Bq35100 *self);


