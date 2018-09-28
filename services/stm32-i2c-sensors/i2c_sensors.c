/*
 * plumCore I2C sensor test
 *
 * Copyright (C) 2017, Marek Koza, qyx@krtko.org
 *
 * This file is part of uMesh node firmware (http://qyx.krtko.org/projects/umesh)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libopencm3/stm32/i2c.h>
#include <libopencm3/stm32/gpio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"

#include "module.h"
#include "interfaces/sensor.h"

#include "i2c_sensors.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "i2c-sensors"

#define I2C_TIMEOUT 100000


static void i2c_sensors_reset(uint32_t i2c) {
	i2c_peripheral_disable(i2c);
	gpio_mode_setup(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, GPIO8 | GPIO9);
	gpio_mode_setup(GPIOB, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO8 | GPIO9);
	i2c_reset(i2c);
	i2c_set_fast_mode(i2c);
	i2c_set_clock_frequency(i2c, I2C_CR2_FREQ_42MHZ);
	i2c_set_ccr(i2c, 35);
	i2c_set_trise(i2c, 43);
	i2c_peripheral_enable(i2c);
}


static void i2c_transfer(uint32_t i2c, uint8_t addr, const uint8_t *txdata, size_t txlen, uint8_t *rxdata, size_t rxlen) {

	uint32_t t = I2C_TIMEOUT;
	while ((I2C_SR2(i2c) & I2C_SR2_BUSY)) {
		if (--t == 0) {
			i2c_sensors_reset(i2c);
			return;
		}
	}

	i2c_send_start(i2c);
	t = I2C_TIMEOUT;
	while (!((I2C_SR1(i2c) & I2C_SR1_SB) & (I2C_SR2(i2c) & (I2C_SR2_MSL | I2C_SR2_BUSY)))) {
		if (--t == 0) {
			i2c_sensors_reset(i2c);
			return;
		}
	}

	i2c_send_7bit_address(i2c, addr, I2C_WRITE);
	t = I2C_TIMEOUT;
	while (!(I2C_SR1(i2c) & I2C_SR1_ADDR)) {
		if (--t == 0) {
			i2c_sensors_reset(i2c);
			return;
		}
	}

	uint32_t reg32 = I2C_SR2(i2c);
	(void)reg32;

	for (size_t i = 0; i < txlen; i++) {
		i2c_send_data(i2c, txdata[i]);
		t = I2C_TIMEOUT;
		while (!(I2C_SR1(I2C1) & (I2C_SR1_BTF))) {
			if (--t == 0) {
				i2c_sensors_reset(i2c);
				return;
			}
		}
	}


	if (rxlen > 0) {
		i2c_send_start(i2c);
		t = I2C_TIMEOUT;
		while (!((I2C_SR1(i2c) & I2C_SR1_SB) & (I2C_SR2(i2c) & (I2C_SR2_MSL | I2C_SR2_BUSY)))) {
			if (--t == 0) {
				i2c_sensors_reset(i2c);
				return;
			}
		}

		i2c_send_7bit_address(i2c, addr, I2C_READ);
		t = I2C_TIMEOUT;
		while (!(I2C_SR1(i2c) & I2C_SR1_ADDR)) {
			if (--t == 0) {
				i2c_sensors_reset(i2c);
				return;
			}
		}

		i2c_enable_ack(i2c);

		reg32 = I2C_SR2(i2c);
		(void)reg32;

		for (size_t i = 0; i < rxlen - 1; i++) {
			t = I2C_TIMEOUT;
			while (!(I2C_SR1(i2c) & I2C_SR1_RxNE)) {
				if (--t == 0) {
					i2c_sensors_reset(i2c);
					return;
				}
			}
			rxdata[i] = i2c_get_data(i2c);
		}

		i2c_disable_ack(i2c);
		i2c_send_stop(i2c);

		t = I2C_TIMEOUT;
		while (!(I2C_SR1(i2c) & I2C_SR1_RxNE)) {
			if (--t == 0) {
				i2c_sensors_reset(i2c);
				return;
			}
		}
		rxdata[rxlen - 1] = i2c_get_data(i2c);
		I2C_SR1(i2c) &= ~I2C_SR1_AF;
	} else {
		i2c_send_stop(i2c);
	}
}


/********************************** Si7021 ************************************/

static interface_sensor_ret_t si7021_temp_info(void *context, ISensorInfo *info) {
	if (u_assert(context != NULL) ||
	    u_assert(info != NULL)) {
		return INTERFACE_SENSOR_RET_FAILED;
	}

	I2cSensors *self = (I2cSensors *)context;
	info->quantity_name = "Temperature";
	info->quantity_symbol = "t";
	info->unit_name = "Degree Celsius";
	info->unit_symbol = "°C";

	return INTERFACE_SENSOR_RET_OK;
}


static interface_sensor_ret_t si7021_temp_value(void *context, float *value) {
	if (u_assert(context != NULL) ||
	    u_assert(value != NULL)) {
		return INTERFACE_SENSOR_RET_FAILED;
	}

	I2cSensors *self = (I2cSensors *)context;

	/* Read relative humidity and temperature. */
	uint8_t rh[2] = {0, 0};
	i2c_transfer(self->i2c, 0x40, &(uint8_t){0xe5}, 1, rh, 2);
	uint8_t temp[2] = {0, 0};
	i2c_transfer(self->i2c, 0x40, &(uint8_t){0xe0}, 1, temp, 2);

	int32_t temp_calc = ((((temp[0] << 8) | temp[1]) * 17572) >> 16) - 4685;

	*value = temp_calc;

	return INTERFACE_SENSOR_RET_OK;
}


static interface_sensor_ret_t si7021_rh_info(void *context, ISensorInfo *info) {
	if (u_assert(context != NULL) ||
	    u_assert(info != NULL)) {
		return INTERFACE_SENSOR_RET_FAILED;
	}

	I2cSensors *self = (I2cSensors *)context;
	info->quantity_name = "Relative humidity";
	info->quantity_symbol = "RH";
	info->unit_name = "Percent";
	info->unit_symbol = "%";

	return INTERFACE_SENSOR_RET_OK;
}


static interface_sensor_ret_t si7021_rh_value(void *context, float *value) {
	if (u_assert(context != NULL) ||
	    u_assert(value != NULL)) {
		return INTERFACE_SENSOR_RET_FAILED;
	}

	I2cSensors *self = (I2cSensors *)context;

	/* Read relative humidity and temperature. */
	uint8_t rh[2] = {0, 0};
	i2c_transfer(self->i2c, 0x40, &(uint8_t){0xe5}, 1, rh, 2);
	uint8_t temp[2] = {0, 0};
	i2c_transfer(self->i2c, 0x40, &(uint8_t){0xe0}, 1, temp, 2);

	int32_t rh_calc = ((((rh[0] << 8) | rh[1]) * 12500) >> 16) - 600;

	*value = rh_calc;

	return INTERFACE_SENSOR_RET_OK;
}


static interface_sensor_ret_t lum_info(void *context, ISensorInfo *info) {
	if (u_assert(context != NULL) ||
	    u_assert(info != NULL)) {
		return INTERFACE_SENSOR_RET_FAILED;
	}

	I2cSensors *self = (I2cSensors *)context;
	info->quantity_name = "Luminosity";
	info->quantity_symbol = "";
	info->unit_name = "Lux";
	info->unit_symbol = "lux";

	return INTERFACE_SENSOR_RET_OK;
}


static interface_sensor_ret_t lum_value(void *context, float *value) {
	if (u_assert(context != NULL) ||
	    u_assert(value != NULL)) {
		return INTERFACE_SENSOR_RET_FAILED;
	}

	I2cSensors *self = (I2cSensors *)context;

	uint8_t l0[2] = {0, 0};
	uint8_t l1[2] = {0, 0};
	i2c_transfer(I2C1, 0x39, &(uint8_t){0xac}, 1, l0, 2);
	i2c_transfer(I2C1, 0x39, &(uint8_t){0xae}, 1, l1, 2);

	*value = (l0[1] << 8) | l0[0];

	return INTERFACE_SENSOR_RET_OK;
}


i2c_sensors_ret_t i2c_sensors_init(I2cSensors *self, uint32_t i2c) {
	if (u_assert(self != NULL)) {
		return I2C_SENSORS_RET_FAILED;
	}

	memset(self, 0, sizeof(I2cSensors));
	uhal_module_init(&self->module);
	self->i2c = i2c;

	i2c_sensors_reset(i2c);

	interface_sensor_init(&(self->si7021_temp));
	self->si7021_temp.vmt.info = si7021_temp_info;
	self->si7021_temp.vmt.value = si7021_temp_value;
	self->si7021_temp.vmt.context = (void *)self;

	interface_sensor_init(&(self->si7021_rh));
	self->si7021_rh.vmt.info = si7021_rh_info;
	self->si7021_rh.vmt.value = si7021_rh_value;
	self->si7021_rh.vmt.context = (void *)self;

	interface_sensor_init(&(self->lum));
	self->lum.vmt.info = lum_info;
	self->lum.vmt.value = lum_value;
	self->lum.vmt.context = (void *)self;

	/* No initialization for the Si7021 sensor. */
	const uint8_t poweron[2] = {0x80, 0x03};
	const uint8_t timing[2] = {0x81, 0x00};
	i2c_transfer(self->i2c, 0x39, poweron, 2, NULL, 0);
	i2c_transfer(self->i2c, 0x39, timing, 2, NULL, 0);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialized"));

	return I2C_SENSORS_RET_OK;
}

i2c_sensors_ret_t i2c_sensors_free(I2cSensors *self) {
	if (u_assert(self != NULL)) {
		return I2C_SENSORS_RET_FAILED;
	}

	return I2C_SENSORS_RET_OK;
}

