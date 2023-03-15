/* SPDX-License-Identifier: BSD-2-Clause
 *
 * A generic power device
 *
 * Copyright (c) 2022, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <main.h>
#include <interfaces/power.h>
#include <libopencm3/stm32/gpio.h>
#include <interfaces/dac.h>

#include "generic-power.h"

#define MODULE_NAME "generic-power"


/***************************************************************************************************
 * Power interface API
 ***************************************************************************************************/

static power_ret_t generic_power_enable(Power *self, bool enable) {
	GenericPower *power = (GenericPower *)self->parent;

	if (enable != power->enable_invert) {
		gpio_set(power->locm3_enable_port, power->locm3_enable_pin);
	} else {
		gpio_clear(power->locm3_enable_port, power->locm3_enable_pin);
	}

	return POWER_RET_OK;
}


static power_ret_t generic_power_set_voltage(Power *self, float voltage_v) {
	GenericPower *power = (GenericPower *)self->parent;

	if (power->dac_p == NULL || power->vref_v == 0.0f) {
		/* No DAC is set, cannot set output voltage. The same applies if we don't know the reference voltage. */
		return POWER_RET_FAILED;
	}
	if (power->dac_m == NULL) {
		/* Single ended, the voltage must be positive. */
		if (voltage_v < 0.0f) {
			return POWER_RET_FAILED;
		}
		/** @todo support some voltage manipulation (dividers, etc.). Currently the
		 *        output voltage is the same as the DAC voltage.
		 */
		if (voltage_v > power->vref_v) {
			return POWER_RET_FAILED;
		}
		power->dac_p->vmt->set_single(power->dac_p, voltage_v / power->vref_v);
	} else {
		/* Differential */
		if (voltage_v > power->vref_v || voltage_v < (-power->vref_v)) {
			return POWER_RET_FAILED;
		}
		power->dac_p->vmt->set_single(power->dac_p, 0.5f + (voltage_v / power->vref_v) / 2.0f);
		power->dac_m->vmt->set_single(power->dac_m, 0.5f - (voltage_v / power->vref_v) / 2.0f);
	}

	return POWER_RET_OK;
}


static const struct power_vmt generic_power_vmt = {
	.enable = generic_power_enable,
	.set_voltage = generic_power_set_voltage,
	/* set_current_limit not implemented, current limit cannot be set by software */
	/* get_voltage not implemented, output voltage cannot be measured */
	/* get_current not implemented, excitation current cannot be measured in the current hardware version */
};


generic_power_ret_t generic_power_init(GenericPower *self) {
	memset(self, 0, sizeof(GenericPower));

	self->power.parent = self;
	self->power.vmt = &generic_power_vmt;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialised"));
	return GENERIC_POWER_RET_OK;
}


generic_power_ret_t generic_power_free(GenericPower *self) {
	(void)self;
	return GENERIC_POWER_RET_OK;
}


generic_power_ret_t generic_power_set_enable_gpio(GenericPower *self, uint32_t port, uint32_t pin, bool invert) {
	self->locm3_enable_port = port;
	self->locm3_enable_pin = pin;
	self->enable_invert = invert;
	return GENERIC_POWER_RET_OK;
}

generic_power_ret_t generic_power_set_voltage_dac(GenericPower *self, Dac *dac_p, Dac *dac_m) {
	if (dac_p && dac_m) {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("voltage output is differential"));
	}
	self->dac_p = dac_p;
	self->dac_m = dac_m;
	return GENERIC_POWER_RET_OK;
}

generic_power_ret_t generic_power_set_vref(GenericPower *self, float vref_v) {
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("set voltage reference = %.3f V"), vref_v);
	self->vref_v = vref_v;
	return GENERIC_POWER_RET_OK;
}
