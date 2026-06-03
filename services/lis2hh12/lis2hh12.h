/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ST LIS2HH12 3 axis accelerometer driver
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <interfaces/i2c-bus.h>
#include <interfaces/sensor.h>
#include <interfaces/waveform-source.h>

/* 7 bit I2C slave address. The least significant bit reflects the state of the SA0 pin. */
#define LIS2HH12_I2C_ADDR_SA0_LOW 0x1e
#define LIS2HH12_I2C_ADDR_SA0_HIGH 0x1d


typedef enum {
	/* Temperature output, little endian, 16 bit. */
	LIS2HH12_REG_TEMP_L = 0x0b,
	LIS2HH12_REG_TEMP_H = 0x0c,

	/* Device identification register, reads as 0x41. */
	LIS2HH12_REG_WHO_AM_I = 0x0f,
	#define LIS2HH12_WHO_AM_I_VALUE 0x41

	LIS2HH12_REG_ACT_THS = 0x1e,
	LIS2HH12_REG_ACT_DUR = 0x1f,

	/* Output data rate, block data update and per-axis enables. */
	LIS2HH12_REG_CTRL1 = 0x20,
	#define LIS2HH12_CTRL1_XEN (1 << 0)
	#define LIS2HH12_CTRL1_YEN (1 << 1)
	#define LIS2HH12_CTRL1_ZEN (1 << 2)
	#define LIS2HH12_CTRL1_BDU (1 << 3)
	#define LIS2HH12_CTRL1_ODR_MASK 0xf0
	#define LIS2HH12_CTRL1_ODR_PD (0x0 << 4)
	#define LIS2HH12_CTRL1_ODR_10HZ (0x1 << 4)
	#define LIS2HH12_CTRL1_ODR_50HZ (0x2 << 4)
	#define LIS2HH12_CTRL1_ODR_100HZ (0x3 << 4)
	#define LIS2HH12_CTRL1_ODR_200HZ (0x4 << 4)
	#define LIS2HH12_CTRL1_ODR_400HZ (0x5 << 4)
	#define LIS2HH12_CTRL1_ODR_800HZ (0x6 << 4)

	LIS2HH12_REG_CTRL2 = 0x21,

	/* Interrupt routing and FIFO enable. */
	LIS2HH12_REG_CTRL3 = 0x22,
	#define LIS2HH12_CTRL3_FIFO_EN (1 << 7)

	/* Bandwidth, full scale and serial interface configuration. */
	LIS2HH12_REG_CTRL4 = 0x23,
	#define LIS2HH12_CTRL4_SIM (1 << 0)
	#define LIS2HH12_CTRL4_I2C_DIS (1 << 1)
	#define LIS2HH12_CTRL4_IF_ADD_INC (1 << 2)
	#define LIS2HH12_CTRL4_BW_SCALE_ODR (1 << 3)
	#define LIS2HH12_CTRL4_FS_2G (0x0 << 4)
	#define LIS2HH12_CTRL4_FS_4G (0x2 << 4)
	#define LIS2HH12_CTRL4_FS_8G (0x3 << 4)
	#define LIS2HH12_CTRL4_BW_400 (0x0 << 6)
	#define LIS2HH12_CTRL4_BW_200 (0x1 << 6)
	#define LIS2HH12_CTRL4_BW_100 (0x2 << 6)
	#define LIS2HH12_CTRL4_BW_50 (0x3 << 6)


	LIS2HH12_REG_CTRL5 = 0x24,
	LIS2HH12_REG_CTRL6 = 0x25,
	LIS2HH12_REG_CTRL7 = 0x26,

	LIS2HH12_REG_STATUS = 0x27,

	/* Acceleration output, little endian, low byte first. */
	LIS2HH12_REG_OUT_X_L = 0x28,
	LIS2HH12_REG_OUT_X_H = 0x29,
	LIS2HH12_REG_OUT_Y_L = 0x2a,
	LIS2HH12_REG_OUT_Y_H = 0x2b,
	LIS2HH12_REG_OUT_Z_L = 0x2c,
	LIS2HH12_REG_OUT_Z_H = 0x2d,

	/* FIFO control and status. */
	LIS2HH12_REG_FIFO_CTRL = 0x2e,
	#define LIS2HH12_FIFO_CTRL_FMODE_BYPASS (0x0 << 5)
	#define LIS2HH12_FIFO_CTRL_FMODE_STREAM (0x2 << 5)

	LIS2HH12_REG_FIFO_SRC = 0x2f,
	#define LIS2HH12_FIFO_SRC_FSS_MASK 0x1f
	#define LIS2HH12_FIFO_SRC_EMPTY (1 << 5)
	#define LIS2HH12_FIFO_SRC_OVR (1 << 6)
	#define LIS2HH12_FIFO_SRC_FTH (1 << 7)
} lis2hh12_reg_t;

typedef enum {
	LIS2HH12_RET_OK = 0,
	LIS2HH12_RET_FAILED = -1,
} lis2hh12_ret_t;

typedef struct {
	WaveformSource source;
	I2cBus *i2c;
	uint8_t addr;
	Sensor temp;
	float sample_rate_Hz;
} Lis2hh12;

lis2hh12_ret_t lis2hh12_detect(Lis2hh12 *self);
lis2hh12_ret_t lis2hh12_init_defaults(Lis2hh12 *self);
lis2hh12_ret_t lis2hh12_init(Lis2hh12 *self, I2cBus *i2c, uint8_t addr);
lis2hh12_ret_t lis2hh12_free(Lis2hh12 *self);
