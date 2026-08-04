/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Texas Instruments BQ25798 buck-boost battery charger driver
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>

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

#define BQ25798_REG_ADC_CONTROL             0x2e
#define BQ25798_ADC_CONTROL_ADC_EN          (1 << 7)

/* Battery current (IBAT) ADC result, 16-bit 2's complement, 1 mA per LSB. Positive while charging,
 * negative while discharging. */
#define BQ25798_REG_IBAT_ADC                0x33

/* Battery remote sensing (VBATP) voltage ADC result, 16 bits, 1 mV per LSB. */
#define BQ25798_REG_VBAT_ADC                0x3b

/* Part information: device part number in bits 5:3 and silicon revision in bits 2:0. */
#define BQ25798_REG_PART_INFORMATION        0x48
#define BQ25798_PART_INFORMATION_PN_MASK    0x38
#define BQ25798_PART_INFORMATION_PN_BQ25798 0x18   /* PN = 011b, shifted into bits 5:3 */
#define BQ25798_PART_INFORMATION_DEV_REV_MASK 0x07

/* Charge current limit field range (9 bits). */
#define BQ25798_CHARGE_CURRENT_LIMIT_MAX    0x1ff


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


bq25798_ret_t bq25798_get_power(Bq25798 *self, Power **power) {
	if (power == NULL) {
		return BQ25798_RET_FAILED;
	}
	*power = &self->power;

	return BQ25798_RET_OK;
}
