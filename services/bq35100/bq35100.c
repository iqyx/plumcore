/* SPDX-License-Identifier: BSD-2-Clause
 *
 * BQ35100 primary battery gauge driver service
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include "config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "u_log.h"
#include "u_assert.h"

#include <i2c-bus.h>
#include "bq35100.h"

#define MODULE_NAME "bq35100"


bq35100_ret_t bq35100_read_uint8(Bq35100 *self, bq35100_cmd_t reg, uint8_t *val) {
	uint8_t txbuf = reg;
	self->i2c->transfer(self->i2c->parent, 0x55, &txbuf, 1, (uint8_t *)val, sizeof(uint8_t));
	return BQ35100_RET_OK;
}


bq35100_ret_t bq35100_read_int16(Bq35100 *self, bq35100_cmd_t reg, int16_t *val) {
	uint8_t txbuf = reg;
	uint16_t rxbuf = 0;
	self->i2c->transfer(self->i2c->parent, 0x55, &txbuf, 1, (uint8_t *)&rxbuf, sizeof(uint16_t));
	*val = (int16_t)rxbuf;
	return BQ35100_RET_OK;
}


bq35100_ret_t bq35100_read_uint16(Bq35100 *self, bq35100_cmd_t reg, uint16_t *val) {
	uint8_t txbuf = reg;
	self->i2c->transfer(self->i2c->parent, 0x55, &txbuf, 1, (uint8_t *)val, sizeof(uint16_t));
	return BQ35100_RET_OK;
}


bq35100_ret_t bq35100_read_int32(Bq35100 *self, bq35100_cmd_t reg, int32_t *val) {
	uint8_t txbuf = reg;
	self->i2c->transfer(self->i2c->parent, 0x55, &txbuf, 1, (uint8_t *)val, sizeof(int32_t));
	return BQ35100_RET_OK;
}


bq35100_ret_t bq35100_read_uint32(Bq35100 *self, bq35100_cmd_t reg, uint32_t *val) {
	uint8_t txbuf = reg;
	self->i2c->transfer(self->i2c->parent, 0x55, &txbuf, 1, (uint8_t *)val, sizeof(uint32_t));
	return BQ35100_RET_OK;
}


bq35100_ret_t bq35100_read_data(Bq35100 *self, bq35100_cmd_t reg, uint8_t *data, size_t len) {
	uint8_t txbuf = reg;
	self->i2c->transfer(self->i2c->parent, 0x55, &txbuf, 1, data, len);

	return BQ35100_RET_OK;
}


bq35100_ret_t bq35100_write_uint8(Bq35100 *self, bq35100_cmd_t reg, uint8_t val) {
	if (self->i2c->transfer(self->i2c->parent, 0x55, (uint8_t[]){(uint8_t)reg, val}, 1 + sizeof(val), NULL, 0) != I2C_BUS_RET_OK) {
		return BQ35100_RET_FAILED;
	}
	return BQ35100_RET_OK;
}


bq35100_ret_t bq35100_write_uint16(Bq35100 *self, bq35100_cmd_t reg, uint16_t val) {
	if (self->i2c->transfer(self->i2c->parent, 0x55, (uint8_t[]){(uint8_t)reg, val & 0xff, val >> 8}, 1 + sizeof(val), NULL, 0) != I2C_BUS_RET_OK) {
		return BQ35100_RET_FAILED;
	}
	return BQ35100_RET_OK;
}


bq35100_ret_t bq35100_write_data(Bq35100 *self, bq35100_cmd_t reg, const uint8_t *data, size_t len) {
	uint8_t txbuf[1 + len];
	txbuf[0] = reg;
	memcpy(&(txbuf[1]), data, len);
	if (self->i2c->transfer(self->i2c->parent, 0x55, txbuf, 1 + len, NULL, 0) != I2C_BUS_RET_OK) {
		return BQ35100_RET_FAILED;
	}
	return BQ35100_RET_OK;
}


bq35100_ret_t bq35100_mac(Bq35100 *self, bq35100_control_t control, const uint8_t *txdata, size_t txlen, uint8_t *rxdata, size_t rxlen) {
	if (txlen > 32) {
		return BQ35100_RET_FAILED;
	}

	/* Write control to MAC, send data, compute and set checksum, set data length. */
	bq35100_write_uint16(self, BQ35100_CMD_MAC, (uint16_t)control);
	vTaskDelay(10);

	if (txdata != NULL) {
		bq35100_write_data(self, BQ35100_CMD_MAC_DATA, txdata, txlen);

		/* The control word is included in the checksum. */
		uint8_t checksum = ((uint16_t)control & 0xff) + ((uint16_t)control >> 8);
		for (size_t i = 0; i < txlen; i++) {
			checksum += txdata[i];
		}
		checksum = ~checksum;
		bq35100_write_uint8(self, BQ35100_CMD_MAC_DATA_SUM, checksum);

		/* Append 4 to MAC_DATA_LEN for whatever reason, reference manual says so. */
		bq35100_write_uint8(self, BQ35100_CMD_MAC_DATA_LEN, (uint8_t)txlen + 4);
	}

	if (rxdata != NULL) {
		/* Verify the MAC command, read data, verify checksum and return the result. */
		/** @todo verify command, checksum, data, len */
		uint16_t read_mac = 0;
		bq35100_read_uint16(self, BQ35100_CMD_MAC, &read_mac);
		/* Read 32 bytes max. Fewer bytes may be actually read. */
		uint8_t read_data[32] = {0};
		bq35100_read_data(self, BQ35100_CMD_MAC_DATA, read_data, sizeof(read_data));
		uint8_t read_checksum = 0;
		bq35100_read_uint8(self, BQ35100_CMD_MAC_DATA_SUM, &read_checksum);
		uint8_t read_len = 0;
		bq35100_read_uint8(self, BQ35100_CMD_MAC_DATA_LEN, &read_len);

		/* Subtract those 4 bytes we added earlier. */
		read_len -= 4;

		/* The control word is included in the checksum. */
		uint8_t checksum = ((uint16_t)read_mac & 0xff) + ((uint16_t)read_mac >> 8);
		for (size_t i = 0; i < read_len; i++) {
			checksum += read_data[i];
		}
		checksum = ~checksum;
		u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("read_mac = %04x, read_len = %u checksum = 0x%02x, read_checksum = 0x%02x"), read_mac, read_len, checksum, read_checksum);
		if (read_checksum != checksum) {
			return BQ35100_RET_FAILED;
		}
		if (rxlen > read_len) {
			rxlen = read_len;
		}
		memcpy(rxdata, read_data, rxlen);
	}
	return BQ35100_RET_OK;
}


bq35100_ret_t bq35100_gauge_start(Bq35100 *self) {
	//return bq35100_mac(self, BQ35100_CONTROL_GAUGE_START, NULL, 0, NULL, 0);
	return bq35100_write_uint16(self, 0x00, (uint16_t)BQ35100_CONTROL_GAUGE_START);
}


int32_t bq35100_capacity(Bq35100 *self) {
	int32_t capacity = 0;
	bq35100_read_int32(self, BQ35100_CMD_ACCUMULATED_CAPACITY, &capacity);
	return capacity;
}


uint16_t bq35100_voltage(Bq35100 *self) {
	uint16_t voltage = 0;
	bq35100_read_uint16(self, BQ35100_CMD_VOLTAGE, &voltage);
	return voltage;
}


int16_t bq35100_temperature(Bq35100 *self) {
	int16_t temperature = 0;
	bq35100_read_int16(self, BQ35100_CMD_TEMPERATURE, &temperature);
	return temperature;
}


int16_t bq35100_current(Bq35100 *self) {
	int16_t current = 0;
	bq35100_read_int16(self, BQ35100_CMD_CURRENT, &current);
	return current;
}

static uint32_t bq35100_float_to_uint32(float value) {
	float CC_value = value; //Read CC_gain or CC_delta value from the gauge.
	int exp = 0; //Initialize the exponential for the floating to byte conversion
	float val = CC_value;
	float mod_val;
	if (val < 0) {
		mod_val = val * -1;
	} else {
		mod_val = val;
	}
	float tmpVal = mod_val;
	tmpVal = tmpVal * (1 + pow(2, -25));
	if (tmpVal < 0.5) {
		while (tmpVal < 0.5) {
			tmpVal *= 2;
			exp--;
		}
	} else if (tmpVal <= 1.0) {
		while (tmpVal >= 1.0) {
			tmpVal = tmpVal / 2;
			exp++;
		}
	}
	if (exp > 127) {
		exp = 127;
	} else if (exp < -128) {
		exp = -128;
	}

	tmpVal = pow(2, 8 - exp) * mod_val - 128;
	unsigned char byte2 = floor(tmpVal);
	tmpVal = pow(2, 8) * (tmpVal - (float) byte2);
	unsigned char byte1 = floor(tmpVal);
	tmpVal = pow(2, 8) * (float) (tmpVal - (float) byte1);
	unsigned char byte0 = floor(tmpVal);
	if (val < 0) {
		byte2 = (byte2 | 0x80);
	}
	return (exp + 128) | (byte2 << 8) | (byte1 << 16) | (byte0 << 24);
}


bq35100_ret_t bq35100_set_cc_gain(Bq35100 *self, float calib) {
	uint32_t cc_gain = bq35100_float_to_uint32(calib);
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("write cc_gain = 0x%08x"), cc_gain);
	bq35100_mac(self, 0x4000, (uint8_t *)&cc_gain, sizeof(cc_gain), NULL, 0);
	vTaskDelay(100);

	uint32_t cc_delta = bq35100_float_to_uint32(calib * 1193046.0f);
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("write cc_delta = 0x%08x"), cc_delta);
	bq35100_mac(self, 0x4004, (uint8_t *)&cc_delta, sizeof(cc_delta), NULL, 0);
	vTaskDelay(100);

	/* Check. */
	uint32_t cc_gain_ret = 0;
	bq35100_mac(self, 0x4000, NULL, 0, (uint8_t *)&cc_gain_ret, sizeof(cc_gain_ret));
	uint32_t cc_delta_ret = 0;
	bq35100_mac(self, 0x4004, NULL, 0, (uint8_t *)&cc_delta_ret, sizeof(cc_delta_ret));

	if (cc_gain_ret == cc_gain && cc_delta_ret == cc_delta) {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("calibration values written correctly"));
		return BQ35100_RET_OK;
	} else {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("calibration values writing failed"));
		return BQ35100_RET_FAILED;
	}
}


uint8_t bq35100_battery_status(Bq35100 *self) {
	uint8_t status = 0;
	bq35100_ret_t ret = bq35100_read_uint8(self, BQ35100_CMD_BATTERY_STATUS, &status);
	if (ret == BQ35100_RET_OK) {
		return status;
	} else {
		return 0;
	}
}


static uint8_t bq35100_control_status(Bq35100 *self) {
	uint16_t status = 0;

	bq35100_write_uint16(self, BQ35100_CMD_CONTROL, (uint16_t)BQ35100_CONTROL_STATUS);
	vTaskDelay(2);
	bq35100_read_uint16(self, BQ35100_CMD_CONTROL, &status);

	return status;
}


static bq35100_ret_t bq35100_enter_calib(Bq35100 *self) {
	uint16_t status = 0;
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("Enter calibration mode"));
	do {
		vTaskDelay(10);
		bq35100_write_uint16(self, BQ35100_CMD_CONTROL, (uint16_t)BQ35100_CONTROL_CAL_ENABLE);
		vTaskDelay(10);
		bq35100_write_uint16(self, BQ35100_CMD_CONTROL, (uint16_t)BQ35100_CONTROL_ENTER_CAL);
		vTaskDelay(10);
		bq35100_write_uint16(self, BQ35100_CMD_CONTROL, (uint16_t)BQ35100_CONTROL_STATUS);
		vTaskDelay(10);
		bq35100_read_uint16(self, BQ35100_CMD_CONTROL, &status);
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("status = 0x%04x"), status);
	} while ((status & BQ35100_CONTROL_STATUS_CAL_MODE) == 0);
	return BQ35100_RET_OK;
}


static bq35100_ret_t bq35100_exit_calib(Bq35100 *self) {
	uint16_t status = 0;
	do {
		vTaskDelay(10);
		bq35100_write_uint16(self, BQ35100_CMD_CONTROL, (uint16_t)BQ35100_CONTROL_EXIT_CAL);
		vTaskDelay(10);
		bq35100_write_uint16(self, BQ35100_CMD_CONTROL, (uint16_t)BQ35100_CONTROL_STATUS);
		vTaskDelay(10);
		bq35100_read_uint16(self, BQ35100_CMD_CONTROL, &status);
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("status = 0x%04x"), status);
	} while (status & BQ35100_CONTROL_STATUS_CAL_MODE);
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("Exit calibration mode"));
	return BQ35100_RET_OK;
}


static bq35100_ret_t bq35100_calibrate_cc_offset(Bq35100 *self) {
	/* Start calibration and wait for CCA bit. */
	uint16_t status = 0;
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("Start CC offset calibration"));
	do {
		vTaskDelay(10);
		bq35100_write_uint16(self, BQ35100_CMD_CONTROL, (uint16_t)BQ35100_CONTROL_CC_OFFSET);
		vTaskDelay(10);
		bq35100_write_uint16(self, BQ35100_CMD_CONTROL, (uint16_t)BQ35100_CONTROL_STATUS);
		vTaskDelay(10);
		bq35100_read_uint16(self, BQ35100_CMD_CONTROL, &status);
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("status = 0x%04x"), status);
	} while ((status & BQ35100_CONTROL_STATUS_CCA) == 0);

	/* Calibrating now, wait until CCA is cleared. */
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("Calibrating CC offset..."));
	do {
		vTaskDelay(500);
		bq35100_write_uint16(self, BQ35100_CMD_CONTROL, (uint16_t)BQ35100_CONTROL_STATUS);
		vTaskDelay(10);
		bq35100_read_uint16(self, BQ35100_CMD_CONTROL, &status);
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("status = 0x%04x"), status);
	} while (status & BQ35100_CONTROL_STATUS_CCA);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("Save CC offset"));
	vTaskDelay(10);
	bq35100_write_uint16(self, BQ35100_CMD_CONTROL, (uint16_t)BQ35100_CONTROL_CC_OFFSET_SAVE);

	return BQ35100_RET_OK;
}


bq35100_ret_t bq35100_init(Bq35100 *self, I2cBus *i2c) {
	memset(self, 0, sizeof(Bq35100));
	self->i2c = i2c;

	/*
	waveform_source_init(&self->source);
	self->source.parent = self;
	self->source.start = (typeof(self->source.start))icm42688p_start;
	self->source.stop= (typeof(self->source.stop))icm42688p_stop;
	self->source.read = (typeof(self->source.read))icm42688p_read;
	self->source.get_format = (typeof(self->source.get_format))icm42688p_get_format;
	self->source.get_sample_rate = (typeof(self->source.get_sample_rate))icm42688p_get_sample_rate;
	self->source.set_sample_rate = (typeof(self->source.set_sample_rate))icm42688p_set_sample_rate;
	*/

	uint8_t status = bq35100_battery_status(self);
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("battery status = 0x%02x"), status);
	(void)status;

	bq35100_gauge_start(self);
	vTaskDelay(10);

	//bq35100_enter_calib(self);
	//bq35100_calibrate_cc_offset(self);
	//bq35100_exit_calib(self);


	/* Calibrate for 1R sensing resistor. */
	//bq35100_set_cc_gain(self, 0.00464f);

	while (false) {
		u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("U = %u mV, I = %d mA, Q = %ld uAh, CS = 0x%04x"), bq35100_voltage(self), bq35100_current(self), bq35100_capacity(self), bq35100_control_status(self));
		vTaskDelay(1000);
	}

	return BQ35100_RET_OK;
}


bq35100_ret_t bq35100_free(Bq35100 *self) {
	return BQ35100_RET_OK;
}

