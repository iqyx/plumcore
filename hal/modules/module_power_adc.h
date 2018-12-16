/*
 * Simple power device using ADC to measure voltage and current
 *
 * Copyright (C) 2016, Marek Koza, qyx@krtko.org
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

#pragma once


#include <stdint.h>
#include <stdbool.h>

#include "interfaces/adc.h"
#include "interface_power.h"


enum module_power_adc_chemistry {
	MODULE_POWER_ADC_CHEMISTRY_2NIMH,
	MODULE_POWER_ADC_CHEMISTRY_3NIMH,
	MODULE_POWER_ADC_CHEMISTRY_4NIMH,
	MODULE_POWER_ADC_CHEMISTRY_LIFEPO4,
	MODULE_POWER_ADC_CHEMISTRY_LIION
};

struct module_power_adc_config {
	bool measure_voltage;
	uint32_t voltage_channel;
	uint32_t voltage_multiplier;
	uint32_t voltage_divider;

	bool measure_current;
	uint32_t current_channel;
	uint32_t current_multiplier;
	uint32_t current_divider;

	bool estimate_remaining_capacity;
	enum module_power_adc_chemistry chemistry;
};

struct module_power_adc {
	struct interface_power iface;

	struct interface_adc *adc;
	struct module_power_adc_config *config;

	/** @todo add units */
	int32_t voltage;
	int32_t current;
	uint32_t capacity_1000perc;

};


int32_t module_power_adc_init(struct module_power_adc *self, const char *name, struct interface_adc *adc, struct module_power_adc_config *config);
#define MODULE_POWER_ADC_INIT_OK 0
#define MODULE_POWER_ADC_INIT_FAILED -1

