/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Texas Instruments BQ27441-G1 system-side Impedance Track fuel gauge driver
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>

#include <interfaces/i2c-bus.h>
#include <interfaces/sensor.h>

/* Fixed 7-bit I2C address of the BQ27441-G1. The device has no address selection pins; the 8-bit
 * bus addresses are 0xaa (write) and 0xab (read). */
#define BQ27441_I2C_ADDR 0x55

typedef enum {
	BQ27441_RET_OK = 0,
	BQ27441_RET_FAILED,
} bq27441_ret_t;


typedef struct {
	I2cBus *i2c;
	uint8_t addr;

	/* Fuel gauge measurements exposed through the Sensor interface. */
	Sensor battery_voltage;
	Sensor battery_current;
	Sensor state_of_charge;
	Sensor state_of_health;
	Sensor remaining_capacity;
} Bq27441;


bq27441_ret_t bq27441_init(Bq27441 *self, I2cBus *i2c, uint8_t addr);
bq27441_ret_t bq27441_free(Bq27441 *self);

/* Return the battery voltage Sensor interface (volts) through @p sensor. */
bq27441_ret_t bq27441_get_battery_voltage(Bq27441 *self, Sensor **sensor);

/* Return the average battery current Sensor interface (amps, negative while discharging). */
bq27441_ret_t bq27441_get_battery_current(Bq27441 *self, Sensor **sensor);

/* Return the state-of-charge Sensor interface (percent) through @p sensor. */
bq27441_ret_t bq27441_get_state_of_charge(Bq27441 *self, Sensor **sensor);

/* Return the state-of-health Sensor interface (percent) through @p sensor. */
bq27441_ret_t bq27441_get_state_of_health(Bq27441 *self, Sensor **sensor);

/* Return the remaining capacity Sensor interface (milliamp-hours) through @p sensor. */
bq27441_ret_t bq27441_get_remaining_capacity(Bq27441 *self, Sensor **sensor);

/* Read the latest battery voltage in millivolts. */
bq27441_ret_t bq27441_read_voltage_mv(Bq27441 *self, uint16_t *mv);

/* Read the latest average battery current in milliamps (2's complement, negative while discharging). */
bq27441_ret_t bq27441_read_current_ma(Bq27441 *self, int16_t *ma);

/* Read the latest predicted state-of-charge in percent. */
bq27441_ret_t bq27441_read_soc_percent(Bq27441 *self, uint16_t *percent);

/* Read the latest battery state-of-health in percent. */
bq27441_ret_t bq27441_read_soh_percent(Bq27441 *self, uint8_t *percent);

/* Read the latest remaining capacity in milliamp-hours. */
bq27441_ret_t bq27441_read_remaining_capacity_mah(Bq27441 *self, uint16_t *mah);
