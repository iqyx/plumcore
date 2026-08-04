/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Texas Instruments BQ27441-G1 system-side Impedance Track fuel gauge driver
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>

#include <main.h>

#include <interfaces/i2c-bus.h>
#include <interfaces/sensor.h>
#include "bq27441.h"

#define MODULE_NAME "bq27441"


/***************************************************************************************************
 * Device command map (BQ27441-G1, datasheet SLUSBH1C)
 *
 * The command code is a single byte. Standard commands return a two-byte word transmitted least
 * significant byte first, so the even command code holds the low byte and the odd one the high byte.
 ***************************************************************************************************/

/* Control() takes a two-byte subcommand and returns its two-byte result at the same address. */
#define BQ27441_CMD_CONTROL                 0x00

/* Battery voltage, 1 mV per LSB. */
#define BQ27441_CMD_VOLTAGE                 0x04

/* Remaining capacity (smoothed), 1 mAh per LSB. */
#define BQ27441_CMD_REMAINING_CAPACITY      0x0c

/* Average current, 2's complement, 1 mA per LSB. Negative while discharging. */
#define BQ27441_CMD_AVERAGE_CURRENT         0x10

/* Predicted state-of-charge (smoothed), 1 % per LSB. */
#define BQ27441_CMD_STATE_OF_CHARGE         0x1c

/* State-of-health: low byte is the SOH percentage, high byte is the SOH status. */
#define BQ27441_CMD_STATE_OF_HEALTH         0x20

/* Control() subcommands. */
#define BQ27441_CONTROL_DEVICE_TYPE         0x0001

/* DEVICE_TYPE reported by the BQ27441-G1, used to identify the device on the bus. */
#define BQ27441_DEVICE_TYPE                 0x0421


static bq27441_ret_t bq27441_read_u16(Bq27441 *self, uint8_t reg, uint16_t *val) {
	uint8_t rxdata[2] = {0};
	if (self->i2c->vmt->transfer(self->i2c, self->addr, &reg, sizeof(reg), rxdata, sizeof(rxdata)) != I2C_BUS_RET_OK) {
		return BQ27441_RET_FAILED;
	}
	/* Standard command words are transmitted least significant byte first. */
	*val = ((uint16_t)rxdata[1] << 8) | rxdata[0];
	return BQ27441_RET_OK;
}


static bq27441_ret_t bq27441_write_u16(Bq27441 *self, uint8_t reg, uint16_t val) {
	/* Standard command words expect the least significant byte first. */
	uint8_t txdata[3] = {reg, (uint8_t)(val & 0xff), (uint8_t)(val >> 8)};
	if (self->i2c->vmt->transfer(self->i2c, self->addr, txdata, sizeof(txdata), NULL, 0) != I2C_BUS_RET_OK) {
		return BQ27441_RET_FAILED;
	}
	return BQ27441_RET_OK;
}


/* Issue a Control() subcommand and read back its two-byte result. */
static bq27441_ret_t bq27441_control(Bq27441 *self, uint16_t subcmd, uint16_t *result) {
	if (bq27441_write_u16(self, BQ27441_CMD_CONTROL, subcmd) != BQ27441_RET_OK) {
		return BQ27441_RET_FAILED;
	}
	return bq27441_read_u16(self, BQ27441_CMD_CONTROL, result);
}


/***************************************************************************************************
 * Raw measurement accessors (device-native units)
 ***************************************************************************************************/

bq27441_ret_t bq27441_read_voltage_mv(Bq27441 *self, uint16_t *mv) {
	return bq27441_read_u16(self, BQ27441_CMD_VOLTAGE, mv);
}


bq27441_ret_t bq27441_read_current_ma(Bq27441 *self, int16_t *ma) {
	uint16_t raw = 0;
	if (bq27441_read_u16(self, BQ27441_CMD_AVERAGE_CURRENT, &raw) != BQ27441_RET_OK) {
		return BQ27441_RET_FAILED;
	}
	/* The average current is a 2's complement value scaled to 1 mA per LSB. */
	*ma = (int16_t)raw;
	return BQ27441_RET_OK;
}


bq27441_ret_t bq27441_read_soc_percent(Bq27441 *self, uint16_t *percent) {
	return bq27441_read_u16(self, BQ27441_CMD_STATE_OF_CHARGE, percent);
}


bq27441_ret_t bq27441_read_soh_percent(Bq27441 *self, uint8_t *percent) {
	uint16_t raw = 0;
	if (bq27441_read_u16(self, BQ27441_CMD_STATE_OF_HEALTH, &raw) != BQ27441_RET_OK) {
		return BQ27441_RET_FAILED;
	}
	/* Only the low byte carries the SOH percentage; the high byte is the SOH status. */
	*percent = (uint8_t)(raw & 0xff);
	return BQ27441_RET_OK;
}


bq27441_ret_t bq27441_read_remaining_capacity_mah(Bq27441 *self, uint16_t *mah) {
	return bq27441_read_u16(self, BQ27441_CMD_REMAINING_CAPACITY, mah);
}


/***************************************************************************************************
 * Sensor interfaces. All values are reported in SI base units (V, A, Ah, dimensionless ratio).
 ***************************************************************************************************/

static sensor_ret_t battery_voltage_value_f(Sensor *self, float *value) {
	uint16_t mv = 0;
	if (bq27441_read_voltage_mv((Bq27441 *)self->parent, &mv) != BQ27441_RET_OK) {
		return SENSOR_RET_FAILED;
	}
	*value = (float)mv / 1000.0f;
	return SENSOR_RET_OK;
}


static const struct sensor_vmt battery_voltage_vmt = {
	.value_f = battery_voltage_value_f,
};

static const struct sensor_info battery_voltage_info = {
	.description = "battery voltage",
	.unit = "V",
};


static sensor_ret_t battery_current_value_f(Sensor *self, float *value) {
	int16_t ma = 0;
	if (bq27441_read_current_ma((Bq27441 *)self->parent, &ma) != BQ27441_RET_OK) {
		return SENSOR_RET_FAILED;
	}
	*value = (float)ma / 1000.0f;
	return SENSOR_RET_OK;
}


static const struct sensor_vmt battery_current_vmt = {
	.value_f = battery_current_value_f,
};

static const struct sensor_info battery_current_info = {
	.description = "battery current",
	.unit = "A",
};


static sensor_ret_t state_of_charge_value_f(Sensor *self, float *value) {
	uint16_t percent = 0;
	if (bq27441_read_soc_percent((Bq27441 *)self->parent, &percent) != BQ27441_RET_OK) {
		return SENSOR_RET_FAILED;
	}
	/* Report the state-of-charge as a 0.0..1.0 ratio. */
	*value = (float)percent / 100.0f;
	return SENSOR_RET_OK;
}


static const struct sensor_vmt state_of_charge_vmt = {
	.value_f = state_of_charge_value_f,
};

static const struct sensor_info state_of_charge_info = {
	.description = "state of charge",
	.unit = "1",
};


static sensor_ret_t state_of_health_value_f(Sensor *self, float *value) {
	uint8_t percent = 0;
	if (bq27441_read_soh_percent((Bq27441 *)self->parent, &percent) != BQ27441_RET_OK) {
		return SENSOR_RET_FAILED;
	}
	/* Report the state-of-health as a 0.0..1.0 ratio. */
	*value = (float)percent / 100.0f;
	return SENSOR_RET_OK;
}


static const struct sensor_vmt state_of_health_vmt = {
	.value_f = state_of_health_value_f,
};

static const struct sensor_info state_of_health_info = {
	.description = "state of health",
	.unit = "1",
};


static sensor_ret_t remaining_capacity_value_f(Sensor *self, float *value) {
	uint16_t mah = 0;
	if (bq27441_read_remaining_capacity_mah((Bq27441 *)self->parent, &mah) != BQ27441_RET_OK) {
		return SENSOR_RET_FAILED;
	}
	/* Report the remaining capacity in amp-hours. */
	*value = (float)mah / 1000.0f;
	return SENSOR_RET_OK;
}


static const struct sensor_vmt remaining_capacity_vmt = {
	.value_f = remaining_capacity_value_f,
};

static const struct sensor_info remaining_capacity_info = {
	.description = "remaining capacity",
	.unit = "Ah",
};


/***************************************************************************************************
 * Service implementation
 ***************************************************************************************************/

/* Verify the device responds on the bus and identifies itself as a BQ27441-G1. */
static bq27441_ret_t bq27441_probe(Bq27441 *self) {
	uint16_t device_type = 0;
	if (bq27441_control(self, BQ27441_CONTROL_DEVICE_TYPE, &device_type) != BQ27441_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("no response at address 0x%02x"), self->addr);
		return BQ27441_RET_FAILED;
	}
	if (device_type != BQ27441_DEVICE_TYPE) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("unexpected device type 0x%04x"), device_type);
		return BQ27441_RET_FAILED;
	}
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("found BQ27441-G1 at 0x%02x"), self->addr);

	return BQ27441_RET_OK;
}


bq27441_ret_t bq27441_init(Bq27441 *self, I2cBus *i2c, uint8_t addr) {
	memset(self, 0, sizeof(Bq27441));
	self->i2c = i2c;
	self->addr = addr;

	if (bq27441_probe(self) != BQ27441_RET_OK) {
		return BQ27441_RET_FAILED;
	}

	/* The fuel gauge is pre-configured for its cell chemistry through its OTP memory and starts
	 * gauging on its own, so no runtime configuration is required to read the measurements. */

	self->battery_voltage.vmt = &battery_voltage_vmt;
	self->battery_voltage.info = &battery_voltage_info;
	self->battery_voltage.parent = self;

	self->battery_current.vmt = &battery_current_vmt;
	self->battery_current.info = &battery_current_info;
	self->battery_current.parent = self;

	self->state_of_charge.vmt = &state_of_charge_vmt;
	self->state_of_charge.info = &state_of_charge_info;
	self->state_of_charge.parent = self;

	self->state_of_health.vmt = &state_of_health_vmt;
	self->state_of_health.info = &state_of_health_info;
	self->state_of_health.parent = self;

	self->remaining_capacity.vmt = &remaining_capacity_vmt;
	self->remaining_capacity.info = &remaining_capacity_info;
	self->remaining_capacity.parent = self;

	return BQ27441_RET_OK;
}


bq27441_ret_t bq27441_free(Bq27441 *self) {
	(void)self;
	return BQ27441_RET_OK;
}


bq27441_ret_t bq27441_get_battery_voltage(Bq27441 *self, Sensor **sensor) {
	if (sensor == NULL) {
		return BQ27441_RET_FAILED;
	}
	*sensor = &self->battery_voltage;

	return BQ27441_RET_OK;
}


bq27441_ret_t bq27441_get_battery_current(Bq27441 *self, Sensor **sensor) {
	if (sensor == NULL) {
		return BQ27441_RET_FAILED;
	}
	*sensor = &self->battery_current;

	return BQ27441_RET_OK;
}


bq27441_ret_t bq27441_get_state_of_charge(Bq27441 *self, Sensor **sensor) {
	if (sensor == NULL) {
		return BQ27441_RET_FAILED;
	}
	*sensor = &self->state_of_charge;

	return BQ27441_RET_OK;
}


bq27441_ret_t bq27441_get_state_of_health(Bq27441 *self, Sensor **sensor) {
	if (sensor == NULL) {
		return BQ27441_RET_FAILED;
	}
	*sensor = &self->state_of_health;

	return BQ27441_RET_OK;
}


bq27441_ret_t bq27441_get_remaining_capacity(Bq27441 *self, Sensor **sensor) {
	if (sensor == NULL) {
		return BQ27441_RET_FAILED;
	}
	*sensor = &self->remaining_capacity;

	return BQ27441_RET_OK;
}
