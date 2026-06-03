/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ST LIS2HH12 3 axis accelerometer driver
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <main.h>

#include <interfaces/i2c-bus.h>
#include <interfaces/sensor.h>
#include <interfaces/waveform-source.h>

#include "lis2hh12.h"

//#define DEBUG
#define MODULE_NAME "lis2hh12"


static uint8_t read8(Lis2hh12 *self, lis2hh12_reg_t addr) {
	/* Write the sub-address, then read a single byte after a repeated start. */
	uint8_t reg = addr & 0x7f;
	uint8_t rxbuf = 0;
	self->i2c->vmt->transfer(self->i2c, self->addr, &reg, 1, &rxbuf, 1);
	#if defined(DEBUG)
		u_log(system_log, LOG_TYPE_DEBUG, "read8 [0x%02x] = 0x%02x", addr, rxbuf);
	#endif
	return rxbuf;
}


static void readn(Lis2hh12 *self, lis2hh12_reg_t addr, uint8_t *buf, size_t len) {
	/* The MSB of the sub-address enables register address auto-increment during
	 * multi-byte reads (IF_ADD_INC in CTRL4 must be enabled as well). */
	uint8_t reg = 0x80 | (addr & 0x7f);
	self->i2c->vmt->transfer(self->i2c, self->addr, &reg, 1, buf, len);
}


static void write8(Lis2hh12 *self, lis2hh12_reg_t addr, uint8_t value) {
	uint8_t txbuf[2] = {addr & 0x7f, value};
	self->i2c->vmt->transfer(self->i2c, self->addr, txbuf, 2, NULL, 0);
	#if defined(DEBUG)
		u_log(system_log, LOG_TYPE_DEBUG, "write8 [0x%02x] = 0x%02x", addr, value);
	#endif
}


lis2hh12_ret_t lis2hh12_detect(Lis2hh12 *self) {
	uint8_t who_am_i = read8(self, LIS2HH12_REG_WHO_AM_I);
	if (who_am_i == LIS2HH12_WHO_AM_I_VALUE) {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("LIS2HH12 detected, who_am_i = 0x%02x"), who_am_i);
		return LIS2HH12_RET_OK;
	}

	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("detection failed, who_am_i = 0x%02x"), who_am_i);
	return LIS2HH12_RET_FAILED;
}


static uint32_t lis2hh12_fifo_entries(Lis2hh12 *self) {
	uint8_t src = read8(self, LIS2HH12_REG_FIFO_SRC);
	if (src & LIS2HH12_FIFO_SRC_OVR) {
		/* The FIFO is 32 samples deep and has overrun, all slots are full. */
		return 32;
	}
	return src & LIS2HH12_FIFO_SRC_FSS_MASK;
}


/* Map a requested sample rate to the closest ODR field value greater than or equal to it.
 * The actual configured rate is returned through actual_Hz when not NULL. */
static uint8_t lis2hh12_odr_bits(float sample_rate_Hz, float *actual_Hz) {
	uint8_t bits;
	float actual;
	if (sample_rate_Hz <= 10.0f) {
		bits = LIS2HH12_CTRL1_ODR_10HZ;
		actual = 10.0f;
	} else if (sample_rate_Hz <= 50.0f) {
		bits = LIS2HH12_CTRL1_ODR_50HZ;
		actual = 50.0f;
	} else if (sample_rate_Hz <= 100.0f) {
		bits = LIS2HH12_CTRL1_ODR_100HZ;
		actual = 100.0f;
	} else if (sample_rate_Hz <= 200.0f) {
		bits = LIS2HH12_CTRL1_ODR_200HZ;
		actual = 200.0f;
	} else if (sample_rate_Hz <= 400.0f) {
		bits = LIS2HH12_CTRL1_ODR_400HZ;
		actual = 400.0f;
	} else {
		bits = LIS2HH12_CTRL1_ODR_800HZ;
		actual = 800.0f;
	}
	if (actual_Hz != NULL) {
		*actual_Hz = actual;
	}
	return bits;
}


/**********************************************************************************************************************
 * Waveform source API
 **********************************************************************************************************************/

static waveform_source_ret_t lis2hh12_start(WaveformSource *source) {
	Lis2hh12 *self = (Lis2hh12 *)source->parent;
	/* Leave power-down mode by writing the configured ODR and enabling all three axes. */
	write8(self, LIS2HH12_REG_CTRL1, lis2hh12_odr_bits(self->sample_rate_Hz, NULL) |
	       LIS2HH12_CTRL1_BDU | LIS2HH12_CTRL1_XEN | LIS2HH12_CTRL1_YEN | LIS2HH12_CTRL1_ZEN);
	return WAVEFORM_SOURCE_RET_OK;
}


static waveform_source_ret_t lis2hh12_stop(WaveformSource *source) {
	Lis2hh12 *self = (Lis2hh12 *)source->parent;
	/* Power-down mode (ODR = 0) stops conversions but keeps the configuration. */
	write8(self, LIS2HH12_REG_CTRL1, LIS2HH12_CTRL1_ODR_PD);
	return WAVEFORM_SOURCE_RET_OK;
}


static waveform_source_ret_t lis2hh12_read(WaveformSource *source, void *data, size_t sample_count, size_t *read) {
	Lis2hh12 *self = (Lis2hh12 *)source->parent;
	/* Each sample is a triplet of signed 16 bit values (X, Y, Z). */
	int16_t *out = (int16_t *)data;

	/* Do not read more samples than the FIFO currently holds. */
	uint32_t fifo_count = lis2hh12_fifo_entries(self);
	if (sample_count > fifo_count) {
		sample_count = fifo_count;
	}

	#if defined(DEBUG)
		u_log(system_log, LOG_TYPE_DEBUG, "ws_read(data=0x%08x, count=%d, read=0x%08x)", data, sample_count, read);
	#endif

	*read = 0;
	while (sample_count > 0) {
		/* One FIFO slot holds six consecutive bytes (X_L, X_H, Y_L, Y_H, Z_L, Z_H).
		 * The device is little endian and so is the MCU, copy the triplet directly. */
		readn(self, LIS2HH12_REG_OUT_X_L, (uint8_t *)out, 3 * sizeof(int16_t));

		out += 3;
		sample_count--;
		(*read)++;
	}

	return WAVEFORM_SOURCE_RET_OK;
}


static waveform_source_ret_t lis2hh12_get_format(WaveformSource *source, enum waveform_source_format *format, uint32_t *channels) {
	(void)source;
	*format = WAVEFORM_SOURCE_FORMAT_S16;
	*channels = 3;
	return WAVEFORM_SOURCE_RET_OK;
}


static waveform_source_ret_t lis2hh12_set_sample_rate(WaveformSource *source, float sample_rate_Hz) {
	Lis2hh12 *self = (Lis2hh12 *)source->parent;
	uint8_t odr = lis2hh12_odr_bits(sample_rate_Hz, &self->sample_rate_Hz);

	/* Replace only the ODR field, keep the axis enables and BDU bit untouched. */
	uint8_t ctrl1 = read8(self, LIS2HH12_REG_CTRL1) & ~LIS2HH12_CTRL1_ODR_MASK;
	write8(self, LIS2HH12_REG_CTRL1, ctrl1 | odr);

	return WAVEFORM_SOURCE_RET_OK;
}


static waveform_source_ret_t lis2hh12_get_sample_rate(WaveformSource *source, float *sample_rate_Hz) {
	Lis2hh12 *self = (Lis2hh12 *)source->parent;
	*sample_rate_Hz = self->sample_rate_Hz;
	return WAVEFORM_SOURCE_RET_OK;
}


static const struct waveform_source_vmt lis2hh12_source_vmt = {
	.start = lis2hh12_start,
	.stop = lis2hh12_stop,
	.read = lis2hh12_read,
	.get_format = lis2hh12_get_format,
	.set_sample_rate = lis2hh12_set_sample_rate,
	.get_sample_rate = lis2hh12_get_sample_rate,
};


lis2hh12_ret_t lis2hh12_init_defaults(Lis2hh12 *self) {
	/* Enable register address auto-increment for multi-byte transfers, full scale +-2 g. */
	write8(self, LIS2HH12_REG_CTRL4, LIS2HH12_CTRL4_IF_ADD_INC | LIS2HH12_CTRL4_FS_2G | LIS2HH12_CTRL4_BW_SCALE_ODR);

	/* Enable all three axes and block data update, 100 Hz output data rate. */
	self->sample_rate_Hz = 50.0f;
	write8(self, LIS2HH12_REG_CTRL1, LIS2HH12_CTRL1_ODR_50HZ |
		LIS2HH12_CTRL1_BDU | LIS2HH12_CTRL1_XEN | LIS2HH12_CTRL1_YEN | LIS2HH12_CTRL1_ZEN);

	/* Enable the FIFO and put it into stream mode so samples are buffered continuously. */
	write8(self, LIS2HH12_REG_CTRL3, LIS2HH12_CTRL3_FIFO_EN);
	write8(self, LIS2HH12_REG_FIFO_CTRL, LIS2HH12_FIFO_CTRL_FMODE_STREAM);

	return LIS2HH12_RET_OK;
}


/**********************************************************************************************************************
 * Die temperature sensor Sensor API
 **********************************************************************************************************************/

static sensor_ret_t temp_sensor_value_f(Sensor *sensor, float *value) {
	Lis2hh12 *self = (Lis2hh12 *)sensor->parent;

	uint8_t buf[2] = {0};
	readn(self, LIS2HH12_REG_TEMP_L, buf, sizeof(buf));

	/* 12 bit, two's complement, 8 LSB/degC with 0 LSB representing 25 degC.
	 * Sign-extend the 12 bit value held in a 16 bit container. */
	int16_t raw = (int16_t)((buf[1] << 8) | buf[0]);
	raw = (int16_t)(raw << 4) >> 4;

	if (value != NULL) {
		*value = 25.0f + (float)raw / 8.0f;
		return SENSOR_RET_OK;
	}

	return SENSOR_RET_FAILED;
}


static const struct sensor_vmt temp_sensor_vmt = {
	.value_f = temp_sensor_value_f,
};


static const struct sensor_info temp_sensor_info = {
	.description = "accelerometer die temperature",
	.unit = "°C",
};


lis2hh12_ret_t lis2hh12_init(Lis2hh12 *self, I2cBus *i2c, uint8_t addr) {
	memset(self, 0, sizeof(Lis2hh12));
	self->i2c = i2c;
	self->addr = addr;

	if (lis2hh12_detect(self) != LIS2HH12_RET_OK) {
		return LIS2HH12_RET_FAILED;
	}

	lis2hh12_init_defaults(self);

	self->source.vmt = &lis2hh12_source_vmt;
	self->source.parent = self;

	self->temp.vmt = &temp_sensor_vmt;
	self->temp.info = &temp_sensor_info;
	self->temp.parent = self;

	return LIS2HH12_RET_OK;
}


lis2hh12_ret_t lis2hh12_free(Lis2hh12 *self) {
	(void)self;
	return LIS2HH12_RET_OK;
}
