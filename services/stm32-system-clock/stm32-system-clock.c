/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * System clock counting microseconds using STM32 hardware.
 *
 * Copyright (c) 2018-2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include <main.h>
#include <interfaces/clock.h>

#include "stm32-system-clock.h"

#if defined(STM32G4)
	#include <stm32g4xx.h>
#elif defined(STM32H7)
	#include <stm32h7xx.h>
#else
	#error "stm32-system-clock service is not compatible with this MCU family"
#endif

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "system-clock"


/***************************************************************************************************
 * Clock interface implementation
 ***************************************************************************************************/

static clock_ret_t stm32_system_clock_get(Clock *clock, struct timespec *time) {
	if (u_assert(clock != NULL) ||
	    u_assert(time != NULL)) {
		return CLOCK_RET_FAILED;
	}
	Stm32SystemClock *self = (Stm32SystemClock *)clock->parent;
	TIM_TypeDef *base = (TIM_TypeDef *)self->timer_base;

	/** @todo handle the race condition! */
	time->tv_sec = self->overflows;
	time->tv_nsec = (uint64_t)base->CNT * 1000000000ULL / self->freq_hz;

	return CLOCK_RET_OK;;
}


static clock_ret_t stm32_system_clock_set(Clock *clock, const struct timespec *time) {
	if (u_assert(clock != NULL) ||
	    u_assert(time != NULL)) {
		return CLOCK_RET_FAILED;
	}
	Stm32SystemClock *self = (Stm32SystemClock *)clock->parent;
	TIM_TypeDef *base = (TIM_TypeDef *)self->timer_base;

	base->CR1 &= ~TIM_CR1_CEN;
	self->overflows = time->tv_sec;
	base->CNT = (uint32_t)((uint64_t)time->tv_nsec * self->freq_hz / 1000000000ULL);
	base->CR1 |= TIM_CR1_CEN;

	return CLOCK_RET_OK;;
}


static const struct clock_vmt stm32_system_clock_vmt = {
	.get = stm32_system_clock_get,
	.set = stm32_system_clock_set,
};


/***************************************************************************************************
 * Service implementation
 ***************************************************************************************************/

stm32_system_clock_ret_t stm32_system_clock_init(Stm32SystemClock *self, void *timer_base, uint32_t freq_hz) {
	if (u_assert(self != NULL)) {
		goto err;
	}
	memset(self, 0, sizeof(Stm32SystemClock));

	self->timer_base = timer_base;
	self->freq_hz = freq_hz;

	TIM_TypeDef *base = (TIM_TypeDef *)self->timer_base;
	/* Disable the timer before configuring. */
	base->CR1 = 0;

	/* On G4 the timer input clock equals HCLK (APBx prescaler = 1 is the standard configuration).
	 * On H7 the timer input clock is 2x PCLK due to the timer doubling mechanism. */
	#if defined(STM32G4)
		uint32_t tim_ck = SystemCoreClock;
	#elif defined(STM32H7)
		uint32_t tim_ck = SystemCoreClock * 2;
	#endif
	if (freq_hz > tim_ck) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("timer frequency too high"));
		goto err;
	}
	uint32_t prescaler = tim_ck / freq_hz;
	self->freq_hz = tim_ck / prescaler;
	base->PSC = prescaler - 1ul;

	base->ARR = self->freq_hz - 1ul;
	base->EGR |= TIM_EGR_UG;
	base->CR1 |= TIM_CR1_CEN;
	base->DIER |= TIM_DIER_UIE;

	self->clock.parent = self;
	self->clock.vmt = &stm32_system_clock_vmt;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("timer started, freq = %lu MHz"), freq_hz / 1000 / 1000);
	self->initialized = true;
	return STM32_SYSTEM_CLOCK_RET_OK;
err:
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("init failed"));
	return STM32_SYSTEM_CLOCK_RET_FAILED;
}


stm32_system_clock_ret_t stm32_system_clock_free(Stm32SystemClock *self) {
	if (u_assert(self != NULL ) ||
	    u_assert(self->initialized == true)) {
		return STM32_SYSTEM_CLOCK_RET_FAILED;
	}

	/* Nothing to free yet. */

	self->initialized = false;
	return STM32_SYSTEM_CLOCK_RET_OK;
}


stm32_system_clock_ret_t stm32_system_clock_irq_handler(Stm32SystemClock *self) {
	if (u_assert(self != NULL ) ||
	    u_assert(self->initialized == true)) {
		return STM32_SYSTEM_CLOCK_RET_FAILED;
	}
	TIM_TypeDef *base = (TIM_TypeDef *)self->timer_base;

	if (base->SR & TIM_SR_UIF) {
		base->SR &= ~TIM_SR_UIF;
		self->overflows++;
	}

	return STM32_SYSTEM_CLOCK_RET_OK;
}
