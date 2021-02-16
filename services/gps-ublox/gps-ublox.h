/* SPDX-License-Identifier: BSD-2-Clause
 *
 * u-blox GPS driver service
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <i2c-bus.h>


enum gps_ublox_mode {
	GPS_UBLOX_MODE_BACKUP,
	GPS_UBLOX_MODE_FULL,
};

typedef enum {
	GPS_UBLOX_RET_OK = 0,
	GPS_UBLOX_RET_FAILED = -1,
} gps_ublox_ret_t;

typedef struct {
	I2cBus *i2c;
	uint8_t i2c_addr;

	TaskHandle_t rx_task;
	volatile bool rx_can_run;
	volatile bool rx_running;

	volatile uint16_t lptim_last;

} GpsUblox;


gps_ublox_ret_t gps_ublox_set_i2c_transport(GpsUblox *self, I2cBus *i2c, uint8_t i2c_addr);
gps_ublox_ret_t gps_ublox_mode(GpsUblox *self, enum gps_ublox_mode mode);
gps_ublox_ret_t gps_ublox_start(GpsUblox *self);
gps_ublox_ret_t gps_ublox_stop(GpsUblox *self);
gps_ublox_ret_t gps_ublox_timepulse_handler(GpsUblox *self);

gps_ublox_ret_t gps_ublox_init(GpsUblox *self);
gps_ublox_ret_t gps_ublox_free(GpsUblox *self);



