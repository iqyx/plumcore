/*
 * System clock counting microseconds using STM32 hardware.
 *
 * Copyright (c) 2018, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"
#include "port.h"

#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/gpio.h>

#include "clock.h"
#include "interfaces/clock/descriptor.h"


#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "system-clock"


system_clock_ret_t system_clock_get(SystemClock *self, struct timespec *time) {
	if (u_assert(self != NULL)) {
		return SYSTEM_CLOCK_RET_NULL;
	}
	if (u_assert(time != NULL)) {
		return SYSTEM_CLOCK_RET_FAILED;
	}

	uint32_t c = timer_get_counter(self->timer);

	time->tv_sec = ((uint64_t)self->overflows * (uint64_t)(self->period + 1ULL) + (uint64_t)c) / 1000000ULL;
	time->tv_nsec = (((uint64_t)self->overflows * (uint64_t)(self->period + 1ULL) + (uint64_t)c) % 1000000ULL) * 1000UL;

	return SYSTEM_CLOCK_RET_OK;
}


system_clock_ret_t system_clock_set(SystemClock *self, struct timespec *time) {
	if (u_assert(self != NULL)) {
		return SYSTEM_CLOCK_RET_NULL;
	}
	if (u_assert(time != NULL)) {
		return SYSTEM_CLOCK_RET_FAILED;
	}

	uint64_t c = (uint64_t)time->tv_sec * 1000000ULL + (uint64_t)(time->tv_nsec / 1000UL);

	timer_disable_counter(self->timer);
	self->overflows = c / (uint64_t)(self->period + 1ULL);
	timer_set_counter(self->timer, (uint32_t)(c % (uint64_t)(self->period + 1ULL)));
	timer_enable_counter(self->timer);

	return SYSTEM_CLOCK_RET_OK;
}


/* Implementation of the IClock interface clock_get method. */
static iclock_ret_t iclock_clock_get(void *context, struct timespec *time) {
	return system_clock_get((SystemClock *)context, time);
}


/* Implementation of the IClock interface clock_set method. */
static iclock_ret_t iclock_clock_set(void *context, struct timespec *time) {
	return system_clock_set((SystemClock *)context, time);
}


static iclock_ret_t iclock_clock_get_source(void *context, enum iclock_source *source) {
	*source = ICLOCK_SOURCE_UNKNOWN;
	return ICLOCK_RET_OK;
}


static iclock_ret_t iclock_clock_get_status(void *context, enum iclock_status *status) {
	*status = ICLOCK_STATUS_UNKNOWN;
	return ICLOCK_RET_OK;
}


system_clock_ret_t system_clock_init(SystemClock *self, uint32_t timer, uint32_t prescaler, uint32_t period) {
	if (u_assert(self != NULL)) {
		return SYSTEM_CLOCK_RET_NULL;
	}

	memset(self, 0, sizeof(SystemClock));
	self->timer = timer;
	self->prescaler = prescaler;
	self->period = period;

	/* COnfigure the selected hardware timer. */
	timer_set_prescaler(self->timer, self->prescaler);
	timer_continuous_mode(self->timer);
	timer_set_period(self->timer, self->period);
	/* Generate update event. */
	TIM_EGR(self->timer) |= TIM_EGR_UG;

	timer_enable_counter(self->timer);
	timer_enable_irq(self->timer, TIM_DIER_UIE);

	iclock_init(&self->iface);
	self->iface.vmt.clock_get = iclock_clock_get;
	self->iface.vmt.clock_set = iclock_clock_set;
	self->iface.vmt.clock_get_source = iclock_clock_get_source;
	self->iface.vmt.clock_get_status = iclock_clock_get_status;
	self->iface.vmt.context = (void *)self;

	/** @todo remove */
	system_clock_set(self, &(struct timespec){.tv_sec = 1537213488, .tv_nsec = 0});
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("system clock timer started"));

	gpio_set(GPIOB, GPIO10);
	self->initialized = true;
	return SYSTEM_CLOCK_RET_OK;
}


system_clock_ret_t system_clock_free(SystemClock *self) {
	if (self == NULL) {
		return SYSTEM_CLOCK_RET_NULL;
	}

	if (self->initialized == false) {
		return SYSTEM_CLOCK_RET_FAILED;
	}

	self->initialized = false;
	return SYSTEM_CLOCK_RET_OK;
}


system_clock_ret_t system_clock_overflow_handler(SystemClock *self) {
	if (self == NULL) {
		return SYSTEM_CLOCK_RET_NULL;
	}

	if (self->initialized == false) {
		return SYSTEM_CLOCK_RET_FAILED;
	}

	self->overflows++;

	return SYSTEM_CLOCK_RET_OK;
}
