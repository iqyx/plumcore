/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * LDC1312/LDC1314 (12-bit) and LDC1612/LDC1614 (28-bit) Inductance to Digital Converter driver
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <main.h>
#include "u_assert.h"

#include <interfaces/sensor.h>
#include <interfaces/i2c-bus.h>

#include "ldc1x1x.h"

#define MODULE_NAME "ldc1x1x"

#define LDC1X1X_DEFAULT_RCOUNT 0xffff
#define LDC1X1X_DEFAULT_SETTLECOUNT 0x000a
#define LDC1X1X_DEFAULT_CLOCK_DIVIDERS 0x1004
#define LDC1X1X_DEFAULT_DRIVE_CURRENT 0x8000
#define LDC1X1X_DEFAULT_MUX_CONFIG 0xc20d
#define LDC1X1X_DEFAULT_CONFIG 0x0001

/* Per-channel error flags in the top nibble [15:12] of the (MSB) data register. */
#define LDC1X1X_ERR_UR (1 << 15)       /* under-range */
#define LDC1X1X_ERR_OR (1 << 14)       /* over-range */
#define LDC1X1X_ERR_WD (1 << 13)       /* watchdog timeout */
#define LDC1X1X_ERR_AE (1 << 12)       /* amplitude error */
#define LDC1X1X_ERR_MASK 0xf000


/* A register parameter left at 0 in the configuration is not a useful value, so fall back to the default. */
static uint16_t default_if_zero(uint16_t value, uint16_t def) {
	return value != 0 ? value : def;
}


static ldc1x1x_ret_t ldc1x1x_select(Ldc1x1x *self) {
	if (self->conf.preselect_cmd != NULL) {
		self->conf.i2c->vmt->transfer(self->conf.i2c, self->conf.preselect_cmd_addr,
			self->conf.preselect_cmd, self->conf.preselect_cmd_len, NULL, 0);
		vTaskDelay(1);
	}
	return LDC1X1X_RET_OK;
}


static ldc1x1x_ret_t ldc_write(Ldc1x1x *self, uint8_t addr, uint16_t reg) {
	uint8_t tx[3] = {
		addr,
		reg >> 8,
		reg & 0xff
	};
	self->conf.i2c->vmt->transfer(self->conf.i2c, self->conf.addr, tx, sizeof(tx), NULL, 0);

	return LDC1X1X_RET_OK;
}


static ldc1x1x_ret_t ldc_read(Ldc1x1x *self, uint8_t addr, uint16_t *reg) {
	uint8_t rx[2] = {0};
	if (self->conf.i2c->vmt->transfer(self->conf.i2c, self->conf.addr, &addr, 1, rx, sizeof(rx)) != I2C_BUS_RET_OK) {
		return LDC1X1X_RET_FAILED;
	}
	if (reg != NULL) {
		*reg = rx[0] << 8 | rx[1];
		return LDC1X1X_RET_OK;
	}

	return LDC1X1X_RET_FAILED;
}


/* Log all error flags reported for a channel and, for the errors that mean the sensor is not usefully
 * oscillating (amplitude, watchdog), disable the channel so it is skipped until the next enable. */
static void ldc1x1x_report_errors(Ldc1x1x *self, size_t channel, uint16_t err) {
	u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("channel %u conversion error:%s%s%s%s"),
		(unsigned int)channel,
		(err & LDC1X1X_ERR_UR) ? " under-range" : "",
		(err & LDC1X1X_ERR_OR) ? " over-range" : "",
		(err & LDC1X1X_ERR_WD) ? " watchdog" : "",
		(err & LDC1X1X_ERR_AE) ? " amplitude" : "");

	//if (err & (LDC1X1X_ERR_AE | LDC1X1X_ERR_WD)) {
		//self->enabled[channel] = false;
		//u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("channel %u disabled"), (unsigned int)channel);
	//}
}


static sensor_ret_t ldc1x1x_sensor_value_f(Sensor *sensor, float *value) {
	Ldc1x1x *self = sensor->parent;

	if (xSemaphoreTake(self->select_lock, portMAX_DELAY) == pdTRUE) {
		ldc1x1x_select(self);
		if (value != NULL) {
			for (size_t i = 0; i < 4; i++) {
				if (sensor == &(self->out[i])) {
					/* A channel disabled at runtime (persistent amplitude/watchdog error) is
					 * no longer read. */
					if (!self->enabled[i]) {
						xSemaphoreGive(self->select_lock);
						return SENSOR_RET_FAILED;
					}

					/* Both families place each channel's data/error word at DATA_CHx = 0x00 + 2*ch
					 * (0x00, 0x02, 0x04, 0x06). Its top nibble [15:12] holds the error flags; the
					 * low 12 bits hold the result (the most-significant 12 bits on the 28-bit parts). */
					uint16_t msb = 0;
					ldc_read(self, 0x00 + i * 2, &msb);
					uint16_t err = msb & LDC1X1X_ERR_MASK;

					if (self->part == LDC1X1X_PART_28BIT) {
						/* The 28-bit parts add a separate LSB register right after the MSB one. */
						uint16_t lsb = 0;
						ldc_read(self, 0x00 + i * 2 + 1, &lsb);
						*value = (float)(((uint32_t)(msb & 0x0fff) << 16) | lsb);
					} else {
						*value = msb & 0x0fff;
					}

					if (err != 0) {
						ldc1x1x_report_errors(self, i, err);
					}

					xSemaphoreGive(self->select_lock);
					return SENSOR_RET_OK;
				}
			}
		}
		xSemaphoreGive(self->select_lock);
		return SENSOR_RET_FAILED;
	}

	/* No mutex free, not obtained for some reason. */
	return SENSOR_RET_FAILED;
}


static const struct sensor_vmt ldc1x1x_sensor_vmt = {
	.value_f = ldc1x1x_sensor_value_f,
};


static const struct sensor_info ldc1x1x_sensor_info = {
	.description = "LDC input value",
	.unit = "-"
};


ldc1x1x_ret_t ldc1x1x_init(Ldc1x1x *self, const struct ldc1x1x_conf *conf) {
	if (u_assert(self != NULL) ||
	    u_assert(conf != NULL)) {
		return LDC1X1X_RET_NULL;
	}
	memset(self, 0, sizeof(Ldc1x1x));
	memcpy(&self->conf, conf, sizeof(struct ldc1x1x_conf));

	if (u_assert(self->conf.i2c != NULL)) {
		return LDC1X1X_RET_FAILED;
	}

	/* Probe the device: read the manufacturer and device ID registers. Both families report the same
	 * manufacturer ID (0x5449, "TI"), while the device ID distinguishes the 12-bit from the 28-bit parts
	 * and doubles as the register layout selector used later when reading samples. */
	uint16_t manuf_id = 0;
	uint16_t device_id = 0;
	if (ldc_read(self, 0x7e, &manuf_id) != LDC1X1X_RET_OK ||
	    ldc_read(self, 0x7f, &device_id) != LDC1X1X_RET_OK) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("no device responding at address 0x%02x"), self->conf.addr);
		return LDC1X1X_RET_FAILED;
	}

	switch (device_id) {
		case 0x3054:
			self->part = LDC1X1X_PART_12BIT;
			break;
		case 0x3055:
			self->part = LDC1X1X_PART_28BIT;
			break;
		default:
			self->part = LDC1X1X_PART_UNKNOWN;
			break;
	}

	if (manuf_id != 0x5449 || self->part == LDC1X1X_PART_UNKNOWN) {
		u_log(system_log, LOG_TYPE_WARN,
			U_LOG_MODULE_PREFIX("unrecognized device at address 0x%02x (manuf id 0x%04x, device id 0x%04x)"),
			self->conf.addr, manuf_id, device_id);
		return LDC1X1X_RET_FAILED;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("detected %s at address 0x%02x (device id 0x%04x)"),
		self->part == LDC1X1X_PART_28BIT ? "LDC1612/LDC1614 (28-bit)" : "LDC1312/LDC1314 (12-bit)",
		self->conf.addr, device_id);

	self->select_lock = xSemaphoreCreateMutex();
	if (self->select_lock == NULL) {
		return LDC1X1X_RET_FAILED;
	}


	for (size_t i = 0; i < 4; i++) {
		self->out[i].vmt = &ldc1x1x_sensor_vmt;
		self->out[i].info = &ldc1x1x_sensor_info;
		self->out[i].parent = self;
		self->enabled[i] = true;
	}

	return LDC1X1X_RET_OK;
}


ldc1x1x_ret_t ldc1x1x_enable(Ldc1x1x *self) {
	if (u_assert(self != NULL)) {
		return LDC1X1X_RET_FAILED;
	}
	ldc1x1x_select(self);

	/* Program the per-channel conversion parameters before waking the device. CLOCK_DIVIDERS_CHx
	 * requires FIN_DIVIDER[15:12] >= 1, so a raw 0 (like every other unset parameter) uses the default. */
	for (size_t i = 0; i < 4; i++) {
		ldc_write(self, 0x08 + i, default_if_zero(self->conf.rcount[i], LDC1X1X_DEFAULT_RCOUNT));
		ldc_write(self, 0x10 + i, default_if_zero(self->conf.settlecount[i], LDC1X1X_DEFAULT_SETTLECOUNT));
		ldc_write(self, 0x14 + i, default_if_zero(self->conf.clock_dividers[i], LDC1X1X_DEFAULT_CLOCK_DIVIDERS));
		ldc_write(self, 0x1e + i, default_if_zero(self->conf.drive_current[i], LDC1X1X_DEFAULT_DRIVE_CURRENT));

		/* (Re-)enable every channel; a previously disabled channel is given another chance. */
		self->enabled[i] = true;
	}

	ldc_write(self, 0x1b, default_if_zero(self->conf.mux_config, LDC1X1X_DEFAULT_MUX_CONFIG));

	/* CONFIG is written last: it clears SLEEP_MODE_EN and brings the device into conversion mode. */
	ldc_write(self, 0x1a, default_if_zero(self->conf.config, LDC1X1X_DEFAULT_CONFIG));

	return LDC1X1X_RET_OK;
}

