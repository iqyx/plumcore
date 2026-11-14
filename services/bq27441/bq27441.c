/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Texas Instruments BQ27441-G1 system-side Impedance Track fuel gauge driver
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdbool.h>
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

/* Battery temperature used by the gauging algorithm, 0.1 K per LSB. */
#define BQ27441_CMD_TEMPERATURE             0x02

/* Flags() status word. */
#define BQ27441_CMD_FLAGS                   0x06
#define BQ27441_FLAGS_ITPOR                 (1 << 5)
#define BQ27441_FLAGS_CFGUPMODE             (1 << 4)

/* Battery voltage, 1 mV per LSB. */
#define BQ27441_CMD_VOLTAGE                 0x04

/* Remaining capacity (smoothed), 1 mAh per LSB. */
#define BQ27441_CMD_REMAINING_CAPACITY      0x0c

/* Full charge capacity (smoothed), 1 mAh per LSB. */
#define BQ27441_CMD_FULL_CHARGE_CAPACITY    0x0e

/* Average current, 2's complement, 1 mA per LSB. Negative while discharging. */
#define BQ27441_CMD_AVERAGE_CURRENT         0x10

/* Predicted state-of-charge (smoothed), 1 % per LSB. */
#define BQ27441_CMD_STATE_OF_CHARGE         0x1c

/* State-of-health: low byte is the SOH percentage, high byte is the SOH status. */
#define BQ27441_CMD_STATE_OF_HEALTH         0x20

/* Extended commands giving access to the RAM-based data memory in 32-byte blocks. Block data is big endian. */
#define BQ27441_CMD_DATA_CLASS              0x3e
#define BQ27441_CMD_DATA_BLOCK              0x3f
#define BQ27441_CMD_BLOCK_DATA              0x40
#define BQ27441_CMD_BLOCK_DATA_CHECKSUM     0x60
#define BQ27441_CMD_BLOCK_DATA_CONTROL      0x61
#define BQ27441_BLOCK_SIZE                  32

/* Control() subcommands. */
#define BQ27441_CONTROL_STATUS              0x0000
#define BQ27441_CONTROL_STATUS_SS           (1 << 13)
#define BQ27441_CONTROL_DEVICE_TYPE         0x0001
#define BQ27441_CONTROL_SET_CFGUPDATE       0x0013
#define BQ27441_CONTROL_SEALED              0x0020
#define BQ27441_CONTROL_SOFT_RESET          0x0042

/* Default UNSEAL key, written to Control() twice. */
#define BQ27441_UNSEAL_KEY                  0x8000

/* State subclass parameters (TRM SLUUAC9, data memory subclass 82). */
#define BQ27441_DM_STATE                    82
#define BQ27441_DM_STATE_DESIGN_CAPACITY    10
#define BQ27441_DM_STATE_DESIGN_ENERGY      12
#define BQ27441_DM_STATE_TAPER_RATE         27
#define BQ27441_DM_STATE_TAPER_VOLTAGE      29

/* Nominal cell voltage used to derive the Design Energy from the Design Capacity, same ratio as the defaults. */
#define BQ27441_NOMINAL_CELL_VOLTAGE_MV     3700

#define BQ27441_CFGUPDATE_TIMEOUT_MS        2000

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


static bq27441_ret_t bq27441_write(Bq27441 *self, const uint8_t *txdata, size_t len) {
	if (self->i2c->vmt->transfer(self->i2c, self->addr, txdata, len, NULL, 0) != I2C_BUS_RET_OK) {
		return BQ27441_RET_FAILED;
	}
	return BQ27441_RET_OK;
}


static bq27441_ret_t bq27441_write_u8(Bq27441 *self, uint8_t reg, uint8_t val) {
	uint8_t txdata[2] = {reg, val};
	return bq27441_write(self, txdata, sizeof(txdata));
}


static bq27441_ret_t bq27441_read(Bq27441 *self, uint8_t reg, uint8_t *buf, size_t len) {
	if (self->i2c->vmt->transfer(self->i2c, self->addr, &reg, sizeof(reg), buf, len) != I2C_BUS_RET_OK) {
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
 * Data memory access
 ***************************************************************************************************/

/* Poll Flags() until the CONFIG UPDATE mode bit reaches the requested state. */
static bq27441_ret_t bq27441_wait_cfgupmode(Bq27441 *self, bool set) {
	for (uint32_t t = 0; t < BQ27441_CFGUPDATE_TIMEOUT_MS; t += 10) {
		uint16_t flags = 0;
		if (bq27441_read_u16(self, BQ27441_CMD_FLAGS, &flags) != BQ27441_RET_OK) {
			return BQ27441_RET_FAILED;
		}
		if (((flags & BQ27441_FLAGS_CFGUPMODE) != 0) == set) {
			return BQ27441_RET_OK;
		}
		vTaskDelay(pdMS_TO_TICKS(10));
	}
	return BQ27441_RET_FAILED;
}


/* Make the 32-byte block of @p subclass containing @p offset accessible through BlockData(). */
static bq27441_ret_t bq27441_dm_select(Bq27441 *self, uint8_t subclass, uint8_t offset) {
	if (bq27441_write_u8(self, BQ27441_CMD_BLOCK_DATA_CONTROL, 0x00) != BQ27441_RET_OK ||
	    bq27441_write_u8(self, BQ27441_CMD_DATA_CLASS, subclass) != BQ27441_RET_OK ||
	    bq27441_write_u8(self, BQ27441_CMD_DATA_BLOCK, offset / BQ27441_BLOCK_SIZE) != BQ27441_RET_OK) {
		return BQ27441_RET_FAILED;
	}
	/* Give the gauge time to transfer the block into the command space. */
	vTaskDelay(pdMS_TO_TICKS(5));
	return BQ27441_RET_OK;
}


/* Read the current value of a data memory parameter. Requires the UNSEALED state. */
static bq27441_ret_t bq27441_dm_read(Bq27441 *self, const struct bq27441_dm_param *param, int16_t *value) {
	uint8_t buf[2] = {0};
	if (bq27441_dm_select(self, param->subclass, param->offset) != BQ27441_RET_OK ||
	    bq27441_read(self, BQ27441_CMD_BLOCK_DATA + param->offset % BQ27441_BLOCK_SIZE, buf, param->size) != BQ27441_RET_OK) {
		return BQ27441_RET_FAILED;
	}
	if (param->size == 1) {
		*value = (int8_t)buf[0];
	} else {
		*value = (int16_t)(((uint16_t)buf[0] << 8) | buf[1]);
	}
	return BQ27441_RET_OK;
}


/* Write a data memory parameter. The whole block is read, patched and committed by writing its new checksum.
 * Requires the CONFIG UPDATE mode. */
static bq27441_ret_t bq27441_dm_write(Bq27441 *self, const struct bq27441_dm_param *param) {
	uint8_t block[BQ27441_BLOCK_SIZE] = {0};
	if (bq27441_dm_select(self, param->subclass, param->offset) != BQ27441_RET_OK ||
	    bq27441_read(self, BQ27441_CMD_BLOCK_DATA, block, sizeof(block)) != BQ27441_RET_OK) {
		return BQ27441_RET_FAILED;
	}

	uint8_t pos = param->offset % BQ27441_BLOCK_SIZE;
	uint8_t txdata[3] = {BQ27441_CMD_BLOCK_DATA + pos};
	if (param->size == 1) {
		txdata[1] = (uint8_t)param->value;
	} else {
		txdata[1] = (uint8_t)((uint16_t)param->value >> 8);
		txdata[2] = (uint8_t)(param->value & 0xff);
	}
	memcpy(&block[pos], &txdata[1], param->size);

	uint8_t sum = 0;
	for (size_t i = 0; i < sizeof(block); i++) {
		sum += block[i];
	}

	/* The block is transferred to the data memory once the matching checksum is written. */
	if (bq27441_write(self, txdata, 1 + param->size) != BQ27441_RET_OK ||
	    bq27441_write_u8(self, BQ27441_CMD_BLOCK_DATA_CHECKSUM, 255 - sum) != BQ27441_RET_OK) {
		return BQ27441_RET_FAILED;
	}
	vTaskDelay(pdMS_TO_TICKS(5));

	return BQ27441_RET_OK;
}


bq27441_ret_t bq27441_dm_update(Bq27441 *self, const struct bq27441_dm_param *params, size_t count) {
	uint16_t status = 0;
	if (bq27441_control(self, BQ27441_CONTROL_STATUS, &status) != BQ27441_RET_OK) {
		return BQ27441_RET_FAILED;
	}
	bool sealed = (status & BQ27441_CONTROL_STATUS_SS) != 0;
	if (sealed) {
		if (bq27441_write_u16(self, BQ27441_CMD_CONTROL, BQ27441_UNSEAL_KEY) != BQ27441_RET_OK ||
		    bq27441_write_u16(self, BQ27441_CMD_CONTROL, BQ27441_UNSEAL_KEY) != BQ27441_RET_OK) {
			return BQ27441_RET_FAILED;
		}
	}

	/* Compare first, reading does not interrupt gauging. Enter the CONFIG UPDATE mode only if a parameter
	 * differs, either because it was never configured or because the gauge reset to its ROM defaults. */
	bq27441_ret_t ret = BQ27441_RET_OK;
	bool cfgupdate = false;
	for (size_t i = 0; i < count; i++) {
		int16_t value = 0;
		if (bq27441_dm_read(self, &params[i], &value) != BQ27441_RET_OK) {
			ret = BQ27441_RET_FAILED;
			goto seal;
		}
		if (value == params[i].value) {
			continue;
		}

		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("subclass %u offset %u: %d -> %d"),
		      params[i].subclass, params[i].offset, value, params[i].value);
		if (!cfgupdate) {
			if (bq27441_control(self, BQ27441_CONTROL_SET_CFGUPDATE, &status) != BQ27441_RET_OK ||
			    bq27441_wait_cfgupmode(self, true) != BQ27441_RET_OK) {
				u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot enter the config update mode"));
				ret = BQ27441_RET_FAILED;
				goto seal;
			}
			cfgupdate = true;
		}
		if (bq27441_dm_write(self, &params[i]) != BQ27441_RET_OK) {
			ret = BQ27441_RET_FAILED;
			break;
		}
	}

	/* SOFT_RESET leaves the CONFIG UPDATE mode, clears [ITPOR] and resimulates with the new configuration. */
	if (cfgupdate) {
		if (bq27441_write_u16(self, BQ27441_CMD_CONTROL, BQ27441_CONTROL_SOFT_RESET) != BQ27441_RET_OK ||
		    bq27441_wait_cfgupmode(self, false) != BQ27441_RET_OK) {
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot exit the config update mode"));
			ret = BQ27441_RET_FAILED;
		}
	}

seal:
	if (sealed) {
		bq27441_write_u16(self, BQ27441_CMD_CONTROL, BQ27441_CONTROL_SEALED);
	}

	return ret;
}


bq27441_ret_t bq27441_set_battery(Bq27441 *self, uint16_t capacity_mah, uint16_t taper_current_ma, uint16_t taper_voltage_mv) {
	if (taper_current_ma == 0) {
		return BQ27441_RET_FAILED;
	}

	const struct bq27441_dm_param params[] = {
		{.subclass = BQ27441_DM_STATE, .offset = BQ27441_DM_STATE_DESIGN_CAPACITY, .size = 2, .value = (int16_t)capacity_mah},
		{.subclass = BQ27441_DM_STATE, .offset = BQ27441_DM_STATE_DESIGN_ENERGY, .size = 2,
		 .value = (int16_t)((uint32_t)capacity_mah * BQ27441_NOMINAL_CELL_VOLTAGE_MV / 1000)},
		/* Taper Rate is in 0.1 h units: Design Capacity / (0.1 * taper current). */
		{.subclass = BQ27441_DM_STATE, .offset = BQ27441_DM_STATE_TAPER_RATE, .size = 2,
		 .value = (int16_t)((uint32_t)capacity_mah * 10 / taper_current_ma)},
		{.subclass = BQ27441_DM_STATE, .offset = BQ27441_DM_STATE_TAPER_VOLTAGE, .size = 2, .value = (int16_t)taper_voltage_mv},
	};

	return bq27441_dm_update(self, params, sizeof(params) / sizeof(params[0]));
}


/***************************************************************************************************
 * Raw measurement accessors (device-native units)
 ***************************************************************************************************/

bq27441_ret_t bq27441_read_temperature_dk(Bq27441 *self, uint16_t *dk) {
	return bq27441_read_u16(self, BQ27441_CMD_TEMPERATURE, dk);
}


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


bq27441_ret_t bq27441_read_full_capacity_mah(Bq27441 *self, uint16_t *mah) {
	return bq27441_read_u16(self, BQ27441_CMD_FULL_CHARGE_CAPACITY, mah);
}


/***************************************************************************************************
 * Sensor interfaces. All values are reported in SI base units (V, A, Ah, dimensionless ratio).
 ***************************************************************************************************/

static sensor_ret_t battery_temperature_value_f(Sensor *self, float *value) {
	uint16_t dk = 0;
	if (bq27441_read_temperature_dk((Bq27441 *)self->parent, &dk) != BQ27441_RET_OK) {
		return SENSOR_RET_FAILED;
	}
	/* Convert from tenths of a kelvin to degrees Celsius. */
	*value = (float)dk / 10.0f - 273.15f;
	return SENSOR_RET_OK;
}


static const struct sensor_vmt battery_temperature_vmt = {
	.value_f = battery_temperature_value_f,
};

static const struct sensor_info battery_temperature_info = {
	.description = "battery temperature",
	.unit = "°C",
};


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


static sensor_ret_t full_capacity_value_f(Sensor *self, float *value) {
	uint16_t mah = 0;
	if (bq27441_read_full_capacity_mah((Bq27441 *)self->parent, &mah) != BQ27441_RET_OK) {
		return SENSOR_RET_FAILED;
	}
	/* Report the full charge capacity in amp-hours. */
	*value = (float)mah / 1000.0f;
	return SENSOR_RET_OK;
}


static const struct sensor_vmt full_capacity_vmt = {
	.value_f = full_capacity_value_f,
};

static const struct sensor_info full_capacity_info = {
	.description = "full charge capacity",
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

	self->battery_temperature.vmt = &battery_temperature_vmt;
	self->battery_temperature.info = &battery_temperature_info;
	self->battery_temperature.parent = self;

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

	self->full_capacity.vmt = &full_capacity_vmt;
	self->full_capacity.info = &full_capacity_info;
	self->full_capacity.parent = self;

	return BQ27441_RET_OK;
}


bq27441_ret_t bq27441_free(Bq27441 *self) {
	(void)self;
	return BQ27441_RET_OK;
}


bq27441_ret_t bq27441_get_battery_temperature(Bq27441 *self, Sensor **sensor) {
	if (sensor == NULL) {
		return BQ27441_RET_FAILED;
	}
	*sensor = &self->battery_temperature;

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


bq27441_ret_t bq27441_get_full_capacity(Bq27441 *self, Sensor **sensor) {
	if (sensor == NULL) {
		return BQ27441_RET_FAILED;
	}
	*sensor = &self->full_capacity;

	return BQ27441_RET_OK;
}
