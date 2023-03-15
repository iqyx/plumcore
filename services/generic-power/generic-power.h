/* SPDX-License-Identifier: BSD-2-Clause
 *
 * A generic power device
 *
 * Copyright (c) 2022, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <main.h>
#include <interfaces/power.h>
#include <interfaces/dac.h>

typedef enum  {
	GENERIC_POWER_RET_OK = 0,
	GENERIC_POWER_RET_FAILED,
} generic_power_ret_t;


typedef struct generic_power {
	/* libopencm3 GPIO style enable */
	uint32_t locm3_enable_port;
	uint32_t locm3_enable_pin;
	bool enable_invert;

	/* Setting of the output voltage is only supported if at least one DAC device is available. In this case
	 * a single ended output voltage is set referenced to the ground. If a second DAC device is available,
	 * the output is considered fully differential and swings around Vref/2. It may be negative. */
	float vref_v;
	float voltage_v;
	Dac *dac_p;
	Dac *dac_m;


	/* Host power interface instance. */
	Power power;
} GenericPower;


generic_power_ret_t generic_power_init(GenericPower *self);
generic_power_ret_t generic_power_free(GenericPower *self);

generic_power_ret_t generic_power_set_enable_gpio(GenericPower *self, uint32_t port, uint32_t pin, bool invert);
generic_power_ret_t generic_power_set_voltage_dac(GenericPower *self, Dac *dac_p, Dac *dac_m);
generic_power_ret_t generic_power_set_vref(GenericPower *self, float vref_v);
