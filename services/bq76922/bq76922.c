/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * BQ76922 battery gauge driver service
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include <main.h>

#include <interfaces/i2c-bus.h>
#include <interfaces/sensor.h>
#include "bq76922.h"

#define MODULE_NAME "bq76922"
#define BQ76922_ADDR 0x08

bq76922_ret_t bq76922_read_uint8(Bq76922 *self, bq76922_cmd_t reg, uint8_t *val) {
	uint8_t txbuf = reg;
	self->i2c->transfer(self->i2c->parent, BQ76922_ADDR, &txbuf, 1, (uint8_t *)val, sizeof(uint8_t));
	return BQ76922_RET_OK;
}


bq76922_ret_t bq76922_read_int16(Bq76922 *self, bq76922_cmd_t reg, int16_t *val) {
	uint8_t txbuf = reg;
	uint16_t rxbuf = 0;
	self->i2c->transfer(self->i2c->parent, BQ76922_ADDR, &txbuf, 1, (uint8_t *)&rxbuf, sizeof(uint16_t));
	*val = (int16_t)rxbuf;
	return BQ76922_RET_OK;
}


bq76922_ret_t bq76922_read_uint16(Bq76922 *self, bq76922_cmd_t reg, uint16_t *val) {
	uint8_t txbuf = reg;
	self->i2c->transfer(self->i2c->parent, BQ76922_ADDR, &txbuf, 1, (uint8_t *)val, sizeof(uint16_t));
	return BQ76922_RET_OK;
}


bq76922_ret_t bq76922_read_int32(Bq76922 *self, bq76922_cmd_t reg, int32_t *val) {
	uint8_t txbuf = reg;
	self->i2c->transfer(self->i2c->parent, BQ76922_ADDR, &txbuf, 1, (uint8_t *)val, sizeof(int32_t));
	return BQ76922_RET_OK;
}


bq76922_ret_t bq76922_read_uint32(Bq76922 *self, bq76922_cmd_t reg, uint32_t *val) {
	uint8_t txbuf = reg;
	self->i2c->transfer(self->i2c->parent, BQ76922_ADDR, &txbuf, 1, (uint8_t *)val, sizeof(uint32_t));
	return BQ76922_RET_OK;
}


bq76922_ret_t bq76922_read_data(Bq76922 *self, bq76922_cmd_t reg, uint8_t *data, size_t len) {
	uint8_t txbuf = reg;
	self->i2c->transfer(self->i2c->parent, BQ76922_ADDR, &txbuf, 1, data, len);

	return BQ76922_RET_OK;
}


bq76922_ret_t bq76922_write_uint8(Bq76922 *self, bq76922_cmd_t reg, uint8_t val) {
	if (self->i2c->transfer(self->i2c->parent, BQ76922_ADDR, (uint8_t[]){(uint8_t)reg, val}, 1 + sizeof(val), NULL, 0) != I2C_BUS_RET_OK) {
		return BQ76922_RET_FAILED;
	}
	return BQ76922_RET_OK;
}


bq76922_ret_t bq76922_write_uint16(Bq76922 *self, bq76922_cmd_t reg, uint16_t val) {
	if (self->i2c->transfer(self->i2c->parent, BQ76922_ADDR, (uint8_t[]){(uint8_t)reg, val & 0xff, val >> 8}, 1 + sizeof(val), NULL, 0) != I2C_BUS_RET_OK) {
		return BQ76922_RET_FAILED;
	}
	return BQ76922_RET_OK;
}


bq76922_ret_t bq76922_write_data(Bq76922 *self, bq76922_cmd_t reg, const uint8_t *data, size_t len) {
	uint8_t txbuf[1 + len];
	txbuf[0] = reg;
	memcpy(&(txbuf[1]), data, len);
	if (self->i2c->transfer(self->i2c->parent, BQ76922_ADDR, txbuf, 1 + len, NULL, 0) != I2C_BUS_RET_OK) {
		return BQ76922_RET_FAILED;
	}
	return BQ76922_RET_OK;
}


bq76922_ret_t bq76922_mac(Bq76922 *self, bq76922_control_t control, const uint8_t *txdata, size_t txlen, uint8_t *rxdata, size_t rxlen) {
	if (txlen > 32) {
		return BQ76922_RET_FAILED;
	}

	/* Write control to MAC, send data, compute and set checksum, set data length. */
	bq76922_write_uint16(self, BQ76922_CMD_MAC, (uint16_t)control);

	if (txdata != NULL) {
		bq76922_write_data(self, BQ76922_CMD_MAC_DATA, txdata, txlen);

		/* The control word is included in the checksum. */
		uint8_t checksum = ((uint16_t)control & 0xff) + ((uint16_t)control >> 8);
		for (size_t i = 0; i < txlen; i++) {
			checksum += txdata[i];
		}
		checksum = ~checksum;
		bq76922_write_uint8(self, BQ76922_CMD_MAC_DATA_SUM, checksum);

		/* Append 4 to MAC_DATA_LEN for whatever reason, reference manual says so. */
		bq76922_write_uint8(self, BQ76922_CMD_MAC_DATA_LEN, (uint8_t)txlen + 4);
	}

	if (rxdata != NULL) {
		/* Verify the MAC command, read data, verify checksum and return the result. */
		/** @todo verify command, checksum, data, len */
		uint16_t read_mac = 0;
		bq76922_read_uint16(self, BQ76922_CMD_MAC, &read_mac);
		/* Read 32 bytes max. Fewer bytes may be actually read. */
		uint8_t read_data[32] = {0};
		bq76922_read_data(self, BQ76922_CMD_MAC_DATA, read_data, sizeof(read_data));
		uint8_t read_checksum = 0;
		bq76922_read_uint8(self, BQ76922_CMD_MAC_DATA_SUM, &read_checksum);
		uint8_t read_len = 0;
		bq76922_read_uint8(self, BQ76922_CMD_MAC_DATA_LEN, &read_len);

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
			return BQ76922_RET_FAILED;
		}
		if (rxlen > read_len) {
			rxlen = read_len;
		}
		memcpy(rxdata, read_data, rxlen);
	}
	return BQ76922_RET_OK;
}


bq76922_ret_t bq76922_init(Bq76922 *self, I2cBus *i2c) {
	memset(self, 0, sizeof(Bq76922));
	self->i2c = i2c;

	//bq76922_mac(self, BQ76922_CONTROL_EXIT_DEEPSLEEP, NULL, 0, NULL, 0);
	//vTaskDelay(100);
	bq76922_mac(self, BQ76922_CONTROL_RESET, NULL, 0, NULL, 0);
	vTaskDelay(100);
	bq76922_mac(self, BQ76922_CONTROL_FET_ENABLE, NULL, 0, NULL, 0);
	vTaskDelay(10);
	bq76922_mac(self, BQ76922_CONTROL_ALL_FETS_ON, NULL, 0, NULL, 0);
	vTaskDelay(10);

	return BQ76922_RET_OK;
}


bq76922_ret_t bq76922_free(Bq76922 *self) {
	return BQ76922_RET_OK;
}

