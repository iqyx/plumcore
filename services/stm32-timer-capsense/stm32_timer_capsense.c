/*
 * uMeshFw HAL capacitive sensing/measurement using STM32 timers
 *
 * Copyright (C) 2017, Marek Koza, qyx@krtko.org
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

#include <libopencm3/stm32/timer.h>

#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"

#include "module.h"
#include "interfaces/sensor.h"

#include "stm32_timer_capsense.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "stm32-capsense"


static interface_sensor_ret_t probe_info(void *context, ISensorInfo *info) {
	if (u_assert(context != NULL) ||
	    u_assert(info != NULL)) {
		return INTERFACE_SENSOR_RET_FAILED;
	}

	Stm32TimerCapsense *self = (Stm32TimerCapsense *)context;
	info->quantity_name = "Capacitance";
	info->quantity_symbol = "C";
	info->unit_name = "Farad";
	info->unit_symbol = "F";

	return INTERFACE_SENSOR_RET_OK;
}


static interface_sensor_ret_t probe_value(void *context, float *value) {
	if (u_assert(context != NULL) ||
	    u_assert(value != NULL)) {
		return INTERFACE_SENSOR_RET_FAILED;
	}

	Stm32TimerCapsense *self = (Stm32TimerCapsense *)context;

	/** @todo determine the IC channel */
	switch (self->probe_ic) {
		case TIM_IC1:
			*value = (float)(TIM_CCR1(self->timer));
			break;
		case TIM_IC2:
			*value = (float)(TIM_CCR2(self->timer));
			break;
		case TIM_IC3:
			*value = (float)(TIM_CCR3(self->timer));
			break;
		case TIM_IC4:
			*value = (float)(TIM_CCR4(self->timer));
			break;
		default:
			return INTERFACE_SENSOR_RET_FAILED;
	}

	return INTERFACE_SENSOR_RET_OK;
}


stm32_timer_capsense_ret_t stm32_timer_capsense_init(Stm32TimerCapsense *self, uint32_t timer, uint32_t probe_reset_oc, uint32_t probe_ic) {
	if (u_assert(self != NULL)) {
		return STM32_TIMER_CAPSENSE_RET_FAILED;
	}

	memset(self, 0, sizeof(Stm32TimerCapsense));
	uhal_module_init(&self->module);

	interface_sensor_init(&(self->probe));
	self->probe.vmt.info = probe_info;
	self->probe.vmt.value = probe_value;
	self->probe.vmt.context = (void *)self;

	self->timer = timer;
	self->probe_reset_oc = probe_reset_oc;
	self->probe_ic = probe_ic;

	if (self->timer != NULL) {
		/* Configure the timer first. */
		timer_reset(self->timer);
		timer_set_mode(self->timer, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
		timer_continuous_mode(self->timer);
		timer_direction_up(self->timer);
		timer_enable_preload(self->timer);
		timer_enable_break_main_output(self->timer);
		stm32_timer_capsense_set_prescaler(self, 0);
		stm32_timer_capsense_set_range(self, 16);

		timer_enable_oc_preload(self->timer, self->probe_reset_oc);
		timer_set_oc_mode(self->timer, self->probe_reset_oc, TIM_OCM_PWM2);
		timer_set_oc_value(self->timer, self->probe_reset_oc, 100);
		timer_enable_oc_output(self->timer, self->probe_reset_oc);

		timer_ic_set_input(self->timer, self->probe_ic, TIM_IC_IN_TI2);
		timer_ic_set_filter(self->timer, self->probe_ic, TIM_IC_OFF);
		timer_ic_set_polarity(self->timer, self->probe_ic, TIM_IC_RISING);
		timer_ic_set_prescaler(self->timer, self->probe_ic, TIM_IC_PSC_OFF);
		timer_ic_enable(self->timer, self->probe_ic);
	}

	timer_enable_counter(self->timer);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialized"));

	return STM32_TIMER_CAPSENSE_RET_OK;
}

stm32_timer_capsense_ret_t stm32_timer_capsense_free(Stm32TimerCapsense *self) {
	if (u_assert(self != NULL)) {
		return STM32_TIMER_CAPSENSE_RET_FAILED;
	}

	timer_disable_counter(self->timer);

	return STM32_TIMER_CAPSENSE_RET_OK;
}


stm32_timer_capsense_ret_t stm32_timer_capsense_set_prescaler(Stm32TimerCapsense *self, uint32_t prescaler) {
	if (u_assert(self != NULL)) {
		return STM32_TIMER_CAPSENSE_RET_FAILED;
	}

	/** @todo check prescaler range */
	timer_disable_counter(self->timer);
	timer_set_prescaler(self->timer, prescaler);
	timer_enable_counter(self->timer);

	return STM32_TIMER_CAPSENSE_RET_OK;
}


stm32_timer_capsense_ret_t stm32_timer_capsense_set_range(Stm32TimerCapsense *self, uint32_t range_bits) {
	if (u_assert(self != NULL) ||
	    u_assert(range_bits >= 1) ||
	    u_assert(range_bits <= 16)) {
		return STM32_TIMER_CAPSENSE_RET_FAILED;
	}

	timer_disable_counter(self->timer);
	self->timer_period = (1 << range_bits);
	timer_set_period(self->timer, self->timer_period - 1);
	timer_enable_counter(self->timer);

	return STM32_TIMER_CAPSENSE_RET_OK;
}


stm32_timer_capsense_ret_t stm32_timer_capsense_set_reset_time(Stm32TimerCapsense *self, uint32_t time_ticks) {
	if (u_assert(self != NULL) ||
	    u_assert(time_ticks >= 1) ||
	    u_assert(time_ticks <= self->timer_period)) {
		return STM32_TIMER_CAPSENSE_RET_FAILED;
	}

	timer_disable_counter(self->timer);
	timer_set_oc_value(self->timer, self->probe_reset_oc, time_ticks);
	timer_enable_counter(self->timer);

	return STM32_TIMER_CAPSENSE_RET_OK;
}


ISensor *stm32_timer_capsense_probe(Stm32TimerCapsense *self) {
	if (u_assert(self != NULL)) {
		return NULL;
	}

	return &self->probe;
}
