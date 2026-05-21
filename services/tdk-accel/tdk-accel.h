/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Generic driver for TDK accelerometers and gyroscopes
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include <main.h>

#include <interfaces/spi.h>
#include <interfaces/sensor.h>
#include <interfaces/clock.h>


typedef enum {
	TDK_ACCEL_RET_OK = 0,
	TDK_ACCEL_RET_FAILED,
	TDK_ACCEL_RET_BAD_PARAM,
} tdk_accel_ret_t;

typedef struct {
	Sensor sensor;
	SpiDev *spidev;
	uint32_t uid;
	float scale_factor;
	SemaphoreHandle_t dry_sem;
	bool dry_en;
	struct timespec timestamp;
	Clock *ts_clock;

} TdkAccel;


tdk_accel_ret_t tdk_accel_init(TdkAccel *self, SpiDev *spidev);
tdk_accel_ret_t tdk_accel_free(TdkAccel *self);
tdk_accel_ret_t tdk_accel_set_scale_factor(TdkAccel *self, float scale_factor);
tdk_accel_ret_t tdk_accel_enable_drdy(TdkAccel *self);
tdk_accel_ret_t tdk_accel_drdy(TdkAccel *self);
tdk_accel_ret_t tdk_accel_enable_timestamping(TdkAccel *self, Clock *ts_clock);

