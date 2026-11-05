/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Texas Instruments BQ25798 buck-boost battery charger driver
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <math.h>

#include <main.h>

#include <interfaces/i2c-bus.h>
#include <interfaces/sensor.h>
#include <interfaces/power.h>
#include "bq25798.h"

#define MODULE_NAME "bq25798"


/***************************************************************************************************
 * Device register map (BQ25798, datasheet SLUSDV2C)
 *
 * The register address is a single byte. Multi-byte registers hold the most significant byte at
 * the lower offset, so they are both transmitted and received most significant byte first.
 ***************************************************************************************************/

/* Charge voltage regulation limit, 11-bit field, 10 mV per LSB. */
#define BQ25798_REG_CHARGE_VOLTAGE_LIMIT    0x01

/* Charge current limit, 9-bit field, 10 mA per LSB. */
#define BQ25798_REG_CHARGE_CURRENT_LIMIT    0x03

#define BQ25798_REG_CHARGER_CONTROL_0       0x0f
#define BQ25798_CHARGER_CONTROL_0_EN_CHG    (1 << 5)

/* Charger control 1 holds the I2C watchdog timer configuration in its low three bits. */
#define BQ25798_REG_CHARGER_CONTROL_1       0x10
#define BQ25798_CHARGER_CONTROL_1_WATCHDOG_MASK 0x07

/* Charger control 5 enables the battery current (IBAT) ADC sensing, needed to report the
 * discharging current on top of the charging current. */
#define BQ25798_REG_CHARGER_CONTROL_5       0x14
#define BQ25798_CHARGER_CONTROL_5_EN_IBAT   (1 << 5)

/* NTC control 1, TS_IGNORE makes the charger disregard the TS pin feedback. */
#define BQ25798_REG_NTC_CONTROL_1           0x18
#define BQ25798_NTC_CONTROL_1_TS_IGNORE     (1 << 0)

#define BQ25798_REG_ADC_CONTROL             0x2e
#define BQ25798_ADC_CONTROL_ADC_EN          (1 << 7)

#define BQ25798_REG_ADC_FUNCTION_DISABLE_0  0x2f
#define BQ25798_ADC_FUNCTION_DISABLE_0_TS_ADC_DIS (1 << 2)

/* Input current (IBUS) ADC result, 16-bit 2's complement, 1 mA per LSB. Positive while drawing current from
 * the selected input source. */
#define BQ25798_REG_IBUS_ADC                0x31

/* Battery current (IBAT) ADC result, 16-bit 2's complement, 1 mA per LSB. Positive while charging,
 * negative while discharging. */
#define BQ25798_REG_IBAT_ADC                0x33

/* Input source voltage (VAC1, VAC2) ADC results, 16 bits, 1 mV per LSB. */
#define BQ25798_REG_VAC1_ADC                0x37
#define BQ25798_REG_VAC2_ADC                0x39

/* Battery remote sensing (VBATP) voltage ADC result, 16 bits, 1 mV per LSB. */
#define BQ25798_REG_VBAT_ADC                0x3b

/* TS pin voltage ADC result, 16 bits, as a ratio of REGN, 0.0976563 % per LSB. */
#define BQ25798_REG_TS_ADC                  0x3f

/* Part information: device part number in bits 5:3 and silicon revision in bits 2:0. */
#define BQ25798_REG_PART_INFORMATION        0x48
#define BQ25798_PART_INFORMATION_PN_MASK    0x38
#define BQ25798_PART_INFORMATION_PN_BQ25798 0x18   /* PN = 011b, shifted into bits 5:3 */
#define BQ25798_PART_INFORMATION_DEV_REV_MASK 0x07

/* Charge current limit field range (9 bits). */
#define BQ25798_CHARGE_CURRENT_LIMIT_MAX    0x1ff

/* 103AT-2 battery NTC thermistor parameters, as recommended by the datasheet. */
#define BQ25798_NTC_R25_OHM                 10000.0f
#define BQ25798_NTC_BETA                    3435.0f


static bq25798_ret_t bq25798_write_u8(Bq25798 *self, uint8_t reg, uint8_t val) {
	uint8_t txdata[2] = {reg, val};
	if (self->i2c->vmt->transfer(self->i2c, self->addr, txdata, sizeof(txdata), NULL, 0) != I2C_BUS_RET_OK) {
		return BQ25798_RET_FAILED;
	}
	return BQ25798_RET_OK;
}


static bq25798_ret_t bq25798_read_u8(Bq25798 *self, uint8_t reg, uint8_t *val) {
	if (self->i2c->vmt->transfer(self->i2c, self->addr, &reg, sizeof(reg), val, sizeof(uint8_t)) != I2C_BUS_RET_OK) {
		return BQ25798_RET_FAILED;
	}
	return BQ25798_RET_OK;
}


static bq25798_ret_t bq25798_read_u16(Bq25798 *self, uint8_t reg, uint16_t *val) {
	uint8_t rxdata[2] = {0};
	if (self->i2c->vmt->transfer(self->i2c, self->addr, &reg, sizeof(reg), rxdata, sizeof(rxdata)) != I2C_BUS_RET_OK) {
		return BQ25798_RET_FAILED;
	}
	/* 16-bit result registers are transmitted most significant byte first. */
	*val = ((uint16_t)rxdata[0] << 8) | rxdata[1];
	return BQ25798_RET_OK;
}


static bq25798_ret_t bq25798_write_u16(Bq25798 *self, uint8_t reg, uint16_t val) {
	/* 16-bit registers expect the most significant byte first. */
	uint8_t txdata[3] = {reg, (uint8_t)(val >> 8), (uint8_t)(val & 0xff)};
	if (self->i2c->vmt->transfer(self->i2c, self->addr, txdata, sizeof(txdata), NULL, 0) != I2C_BUS_RET_OK) {
		return BQ25798_RET_FAILED;
	}
	return BQ25798_RET_OK;
}


/* Read-modify-write the given single-byte register, setting or clearing the supplied bit mask. */
static bq25798_ret_t bq25798_update_u8(Bq25798 *self, uint8_t reg, uint8_t mask, bool set) {
	uint8_t val = 0;
	if (bq25798_read_u8(self, reg, &val) != BQ25798_RET_OK) {
		return BQ25798_RET_FAILED;
	}
	if (set) {
		val |= mask;
	} else {
		val &= ~mask;
	}
	return bq25798_write_u8(self, reg, val);
}


/***************************************************************************************************
 * Sensor interface for the battery voltage
 ***************************************************************************************************/

static sensor_ret_t battery_voltage_value_f(Sensor *self, float *value) {
	Bq25798 *bq = (Bq25798 *)self->parent;

	uint16_t mv = 0;
	if (bq25798_read_battery_voltage_mv(bq, &mv) != BQ25798_RET_OK) {
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


/***************************************************************************************************
 * Sensor interfaces for the two input source (VAC1, VAC2) voltages
 ***************************************************************************************************/

/* Read a voltage ADC result register scaled to 1 mV per LSB, in volts. */
static sensor_ret_t input_voltage_read(Bq25798 *self, uint8_t reg, float *value) {
	uint16_t mv = 0;
	if (bq25798_read_u16(self, reg, &mv) != BQ25798_RET_OK) {
		return SENSOR_RET_FAILED;
	}
	*value = (float)mv / 1000.0f;

	return SENSOR_RET_OK;
}


static sensor_ret_t input1_voltage_value_f(Sensor *self, float *value) {
	return input_voltage_read((Bq25798 *)self->parent, BQ25798_REG_VAC1_ADC, value);
}


static sensor_ret_t input2_voltage_value_f(Sensor *self, float *value) {
	return input_voltage_read((Bq25798 *)self->parent, BQ25798_REG_VAC2_ADC, value);
}


static const struct sensor_vmt input1_voltage_vmt = {
	.value_f = input1_voltage_value_f,
};

static const struct sensor_vmt input2_voltage_vmt = {
	.value_f = input2_voltage_value_f,
};

static const struct sensor_info input1_voltage_info = {
	.description = "input 1 (VAC1) voltage",
	.unit = "V",
};

static const struct sensor_info input2_voltage_info = {
	.description = "input 2 (VAC2) voltage",
	.unit = "V",
};


/***************************************************************************************************
 * Sensor interface for the input (IBUS) current
 ***************************************************************************************************/

static sensor_ret_t input_current_value_f(Sensor *self, float *value) {
	Bq25798 *bq = (Bq25798 *)self->parent;

	/* The IBUS ADC result is a 2's complement value scaled to 1 mA per LSB. */
	uint16_t raw = 0;
	if (bq25798_read_u16(bq, BQ25798_REG_IBUS_ADC, &raw) != BQ25798_RET_OK) {
		return SENSOR_RET_FAILED;
	}
	*value = (float)(int16_t)raw / 1000.0f;

	return SENSOR_RET_OK;
}


static const struct sensor_vmt input_current_vmt = {
	.value_f = input_current_value_f,
};

static const struct sensor_info input_current_info = {
	.description = "input current",
	.unit = "A",
};


/***************************************************************************************************
 * Sensor interface for the battery temperature (NTC thermistor on the TS pin)
 ***************************************************************************************************/

static sensor_ret_t battery_temperature_value_f(Sensor *self, float *value) {
	Bq25798 *bq = (Bq25798 *)self->parent;

	uint16_t raw = 0;
	if (bq25798_read_u16(bq, BQ25798_REG_TS_ADC, &raw) != BQ25798_RET_OK) {
		return SENSOR_RET_FAILED;
	}

	/* The TS pin sits in a REGN - RT1 - TS - (RT2 || NTC) - GND divider. Recover the lower leg resistance from
	 * the TS/REGN ratio, then remove RT2 to get the NTC resistance. */
	float ratio = (float)raw * 0.000976563f;
	if (ratio <= 0.0f || ratio >= 1.0f) {
		return SENSOR_RET_FAILED;
	}
	float r_low = bq->ts.rt1_ohm * ratio / (1.0f - ratio);
	if (r_low >= bq->ts.rt2_ohm) {
		return SENSOR_RET_FAILED;
	}
	float r_ntc = r_low * bq->ts.rt2_ohm / (bq->ts.rt2_ohm - r_low);

	/* Beta equation of the NTC thermistor, in degrees Celsius. */
	*value = 1.0f / (1.0f / 298.15f + logf(r_ntc / BQ25798_NTC_R25_OHM) / BQ25798_NTC_BETA) - 273.15f;

	return SENSOR_RET_OK;
}


static const struct sensor_vmt battery_temperature_vmt = {
	.value_f = battery_temperature_value_f,
};

static const struct sensor_info battery_temperature_info = {
	.description = "battery temperature",
	.unit = "°C",
};


/***************************************************************************************************
 * Power interface for managing the battery charging
 ***************************************************************************************************/

/* Enable or disable the battery charging. */
static power_ret_t power_enable(Power *self, bool enable) {
	Bq25798 *bq = (Bq25798 *)self->parent;

	if (bq25798_update_u8(bq, BQ25798_REG_CHARGER_CONTROL_0, BQ25798_CHARGER_CONTROL_0_EN_CHG, enable) != BQ25798_RET_OK) {
		return POWER_RET_FAILED;
	}

	return POWER_RET_OK;
}


static power_ret_t power_set_voltage(Power *self, float voltage_v) {
	(void)self;
	(void)voltage_v;
	/* The charge voltage regulation limit is fixed to a safe per-cell value at init and must not
	 * be changed at runtime, so setting the voltage is intentionally a no-op. */
	return POWER_RET_OK;
}


/* Set the battery charging current limit. */
static power_ret_t power_set_current_limit(Power *self, float current_i) {
	Bq25798 *bq = (Bq25798 *)self->parent;

	if (current_i < 0.0f) {
		current_i = 0.0f;
	}
	/* The charge current limit register is scaled to 10 mA per LSB. */
	uint16_t ichg = (uint16_t)(current_i * 1000.0f) / 10;
	if (ichg > BQ25798_CHARGE_CURRENT_LIMIT_MAX) {
		ichg = BQ25798_CHARGE_CURRENT_LIMIT_MAX;
	}
	if (bq25798_write_u16(bq, BQ25798_REG_CHARGE_CURRENT_LIMIT, ichg) != BQ25798_RET_OK) {
		return POWER_RET_FAILED;
	}

	return POWER_RET_OK;
}


/* Return the actual battery voltage. */
static power_ret_t power_get_voltage(Power *self, float *voltage_v) {
	Bq25798 *bq = (Bq25798 *)self->parent;

	uint16_t mv = 0;
	if (bq25798_read_battery_voltage_mv(bq, &mv) != BQ25798_RET_OK) {
		return POWER_RET_FAILED;
	}
	*voltage_v = (float)mv / 1000.0f;

	return POWER_RET_OK;
}


/* Return the actual battery current, positive while charging and negative while discharging. */
static power_ret_t power_get_current(Power *self, float *current_i) {
	Bq25798 *bq = (Bq25798 *)self->parent;

	uint16_t raw = 0;
	if (bq25798_read_u16(bq, BQ25798_REG_IBAT_ADC, &raw) != BQ25798_RET_OK) {
		return POWER_RET_FAILED;
	}
	/* The IBAT ADC result is a 2's complement value scaled to 1 mA per LSB. */
	*current_i = (float)(int16_t)raw / 1000.0f;

	return POWER_RET_OK;
}


static const struct power_vmt power_vmt = {
	.enable = power_enable,
	.set_voltage = power_set_voltage,
	.set_current_limit = power_set_current_limit,
	.get_voltage = power_get_voltage,
	.get_current = power_get_current,
};


/***************************************************************************************************
 * Service implementation
 ***************************************************************************************************/

bq25798_ret_t bq25798_read_battery_voltage_mv(Bq25798 *self, uint16_t *mv) {
	/* The VBAT ADC result register is already scaled to 1 mV per LSB. */
	return bq25798_read_u16(self, BQ25798_REG_VBAT_ADC, mv);
}


/* Verify the device responds on the bus and identifies itself as a BQ25798. */
static bq25798_ret_t bq25798_probe(Bq25798 *self) {
	uint8_t part = 0;
	if (bq25798_read_u8(self, BQ25798_REG_PART_INFORMATION, &part) != BQ25798_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("no response at address 0x%02x"), self->addr);
		return BQ25798_RET_FAILED;
	}
	if ((part & BQ25798_PART_INFORMATION_PN_MASK) != BQ25798_PART_INFORMATION_PN_BQ25798) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("unexpected part number (REG48 = 0x%02x)"), part);
		return BQ25798_RET_FAILED;
	}
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("found BQ25798 at 0x%02x, silicon revision %u"),
		self->addr,
		part & BQ25798_PART_INFORMATION_DEV_REV_MASK
	);

	return BQ25798_RET_OK;
}


bq25798_ret_t bq25798_init(Bq25798 *self, I2cBus *i2c, uint8_t addr) {
	memset(self, 0, sizeof(Bq25798));
	self->i2c = i2c;
	self->addr = addr;

	if (bq25798_probe(self) != BQ25798_RET_OK) {
		return BQ25798_RET_FAILED;
	}

	/* Enable the ADC in continuous conversion mode so the VBAT (and the other measurement) result
	 * registers are kept up to date. All other fields are left at their reset defaults. */
	if (bq25798_write_u8(self, BQ25798_REG_ADC_CONTROL, BQ25798_ADC_CONTROL_ADC_EN) != BQ25798_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot enable the ADC"));
		return BQ25798_RET_FAILED;
	}

	/* Disable the I2C watchdog so the charging configuration (charge enable, current limit) does
	 * not revert to the power-on defaults when the host stops talking to the device. */
	if (bq25798_update_u8(self, BQ25798_REG_CHARGER_CONTROL_1, BQ25798_CHARGER_CONTROL_1_WATCHDOG_MASK, false) != BQ25798_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot disable the watchdog"));
		return BQ25798_RET_FAILED;
	}

	/* Enforce a safe charge voltage regulation limit. It is fixed here and never changed at runtime. */
	if (bq25798_write_u16(self, BQ25798_REG_CHARGE_VOLTAGE_LIMIT, BQ25798_CHARGE_VOLTAGE_LIMIT_MV / 10) != BQ25798_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot set the charge voltage limit"));
		return BQ25798_RET_FAILED;
	}

	/* Enable the battery current ADC sensing so the discharging current can be measured too. */
	if (bq25798_update_u8(self, BQ25798_REG_CHARGER_CONTROL_5, BQ25798_CHARGER_CONTROL_5_EN_IBAT, true) != BQ25798_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot enable the battery current sensing"));
		return BQ25798_RET_FAILED;
	}

	self->battery_voltage.vmt = &battery_voltage_vmt;
	self->battery_voltage.info = &battery_voltage_info;
	self->battery_voltage.parent = self;

	self->input1_voltage.vmt = &input1_voltage_vmt;
	self->input1_voltage.info = &input1_voltage_info;
	self->input1_voltage.parent = self;

	self->input2_voltage.vmt = &input2_voltage_vmt;
	self->input2_voltage.info = &input2_voltage_info;
	self->input2_voltage.parent = self;

	self->input_current.vmt = &input_current_vmt;
	self->input_current.info = &input_current_info;
	self->input_current.parent = self;

	self->power.vmt = &power_vmt;
	self->power.parent = self;

	return BQ25798_RET_OK;
}


bq25798_ret_t bq25798_free(Bq25798 *self) {
	(void)self;
	return BQ25798_RET_OK;
}


bq25798_ret_t bq25798_get_battery_voltage(Bq25798 *self, Sensor **sensor) {
	if (sensor == NULL) {
		return BQ25798_RET_FAILED;
	}
	*sensor = &self->battery_voltage;

	return BQ25798_RET_OK;
}


bq25798_ret_t bq25798_get_input_voltage(Bq25798 *self, uint8_t input, Sensor **sensor) {
	if (sensor == NULL || input < 1 || input > 2) {
		return BQ25798_RET_FAILED;
	}
	*sensor = (input == 1) ? &self->input1_voltage : &self->input2_voltage;

	return BQ25798_RET_OK;
}


bq25798_ret_t bq25798_get_input_current(Bq25798 *self, Sensor **sensor) {
	if (sensor == NULL) {
		return BQ25798_RET_FAILED;
	}
	*sensor = &self->input_current;

	return BQ25798_RET_OK;
}


bq25798_ret_t bq25798_enable_ts(Bq25798 *self, float rt1_ohm, float rt2_ohm) {
	self->ts.rt1_ohm = rt1_ohm;
	self->ts.rt2_ohm = rt2_ohm;

	/* Make sure the TS ADC channel is converting and the charger follows the TS feedback. Both are the
	 * reset defaults, set them explicitly anyway. */
	if (bq25798_update_u8(self, BQ25798_REG_ADC_FUNCTION_DISABLE_0, BQ25798_ADC_FUNCTION_DISABLE_0_TS_ADC_DIS, false) != BQ25798_RET_OK ||
	    bq25798_update_u8(self, BQ25798_REG_NTC_CONTROL_1, BQ25798_NTC_CONTROL_1_TS_IGNORE, false) != BQ25798_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot enable the battery thermistor"));
		return BQ25798_RET_FAILED;
	}

	self->battery_temperature.vmt = &battery_temperature_vmt;
	self->battery_temperature.info = &battery_temperature_info;
	self->battery_temperature.parent = self;

	return BQ25798_RET_OK;
}


bq25798_ret_t bq25798_get_battery_temperature(Bq25798 *self, Sensor **sensor) {
	if (sensor == NULL || self->battery_temperature.vmt == NULL) {
		return BQ25798_RET_FAILED;
	}
	*sensor = &self->battery_temperature;

	return BQ25798_RET_OK;
}


bq25798_ret_t bq25798_get_power(Bq25798 *self, Power **power) {
	if (power == NULL) {
		return BQ25798_RET_FAILED;
	}
	*power = &self->power;

	return BQ25798_RET_OK;
}
