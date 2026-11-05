/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Texas Instruments BQ25798 buck-boost battery charger driver
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>

#include <interfaces/i2c-bus.h>
#include <interfaces/sensor.h>
#include <interfaces/power.h>

/* Fixed 7-bit I2C address of the BQ25798. The device has no address selection pins. */
#define BQ25798_I2C_ADDR 0x6b

/* Battery charge voltage regulation limit enforced by the driver. Individual 1S Li-ion cell. */
#define BQ25798_CHARGE_VOLTAGE_LIMIT_MV 4100

typedef enum {
	BQ25798_RET_OK = 0,
	BQ25798_RET_FAILED,
} bq25798_ret_t;


typedef struct {
	I2cBus *i2c;
	uint8_t addr;

	/* Battery (VBATP remote sensing) voltage exposed through the Sensor interface. */
	Sensor battery_voltage;

	/* Voltages of the two input power sources (VAC1, VAC2) exposed through the Sensor interface. */
	Sensor input1_voltage;
	Sensor input2_voltage;

	/* Current drawn from the selected input power source exposed through the Sensor interface. */
	Sensor input_current;

	/* Battery temperature measured by the NTC thermistor on the TS pin, exposed through the Sensor interface.
	 * Available after bq25798_enable_ts(). */
	Sensor battery_temperature;
	struct {
		float rt1_ohm;
		float rt2_ohm;
	} ts;

	/* Battery charging exposed through the Power interface. */
	Power power;
} Bq25798;


bq25798_ret_t bq25798_init(Bq25798 *self, I2cBus *i2c, uint8_t addr);
bq25798_ret_t bq25798_free(Bq25798 *self);

/* Return the battery voltage Sensor interface through @p sensor. */
bq25798_ret_t bq25798_get_battery_voltage(Bq25798 *self, Sensor **sensor);

/* Return the Sensor interface of the input source @p input (1 = VAC1, 2 = VAC2) voltage through @p sensor. */
bq25798_ret_t bq25798_get_input_voltage(Bq25798 *self, uint8_t input, Sensor **sensor);

/* Return the input current Sensor interface through @p sensor. */
bq25798_ret_t bq25798_get_input_current(Bq25798 *self, Sensor **sensor);

/* Enable the battery NTC thermistor (103AT-2) on the TS pin, biased by @p rt1_ohm from REGN to TS and @p rt2_ohm
 * from TS to GND. The TS ADC channel is enabled and the charger follows the TS feedback. */
bq25798_ret_t bq25798_enable_ts(Bq25798 *self, float rt1_ohm, float rt2_ohm);

/* Return the battery temperature Sensor interface (degrees Celsius) through @p sensor. */
bq25798_ret_t bq25798_get_battery_temperature(Bq25798 *self, Sensor **sensor);

/* Return the battery charging Power interface through @p power. */
bq25798_ret_t bq25798_get_power(Bq25798 *self, Power **power);

/* Read the latest battery voltage ADC result in millivolts. */
bq25798_ret_t bq25798_read_battery_voltage_mv(Bq25798 *self, uint16_t *mv);
