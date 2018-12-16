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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"

#include "interfaces/adc.h"
#include "module_power_adc.h"


static void module_power_adc_measure(struct module_power_adc *self) {
	if (u_assert(self != NULL)) {
		return;
	}

	if (self->config->measure_voltage) {
		int32_t voltage;
		iadc_sample(self->adc, self->config->voltage_channel, &voltage);
		self->voltage = voltage * self->config->voltage_multiplier / self->config->voltage_divider / 1000;
	}

	if (self->config->measure_current) {
		int32_t current;
		iadc_sample(self->adc, self->config->current_channel, &current);
		self->current = current * self->config->current_multiplier / self->config->current_divider / 1000;
	}
}


static int32_t iface_measure(void *context) {
	if (u_assert(context != NULL)) {
		return INTERFACE_POWER_MEASURE_FAILED;
	}
	struct module_power_adc *self = (struct module_power_adc *)context;
	module_power_adc_measure(self);
	return INTERFACE_POWER_MEASURE_OK;
}


static int32_t iface_voltage(void *context) {
	if (u_assert(context != NULL)) {
		return 0;
	}
	struct module_power_adc *self = (struct module_power_adc *)context;
	return self->voltage;
}


static int32_t iface_current(void *context) {
	if (u_assert(context != NULL)) {
		return 0;
	}
	struct module_power_adc *self = (struct module_power_adc *)context;
	return self->current;
}


static int32_t iface_capacity(void *context) {
	if (u_assert(context != NULL)) {
		return 0;
	}
	struct module_power_adc *self = (struct module_power_adc *)context;
	return self->capacity_1000perc;
}


int32_t module_power_adc_init(struct module_power_adc *self, const char *name, struct interface_adc *adc, struct module_power_adc_config *config) {
	if (u_assert(self != NULL) ||
	    u_assert(name != NULL) ||
	    u_assert(adc != NULL)) {
		return MODULE_POWER_ADC_INIT_FAILED;
	}

	memset(self, 0, sizeof(struct module_power_adc));

	interface_power_init(&(self->iface));
	self->iface.descriptor.iface_ptr = (void *)(&self->iface);
	self->iface.vmt.context = (void *)self;
	self->iface.vmt.measure = iface_measure;
	self->iface.vmt.voltage = iface_voltage;
	self->iface.vmt.current = iface_current;
	self->iface.vmt.capacity = iface_capacity;

	/* Save ADC interface dependency. */
	self->adc = adc;
	self->config = config;

	module_power_adc_measure(self);

	u_log(system_log, LOG_TYPE_INFO, "initialized, voltage=%dmV, current=%dmA", self->voltage, self->current);
	return MODULE_POWER_ADC_INIT_OK;
err:
	u_log(system_log, LOG_TYPE_ERROR, "initialization failed");
	return MODULE_POWER_ADC_INIT_FAILED;
}

