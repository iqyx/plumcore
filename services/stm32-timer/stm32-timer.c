/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 timer driver
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include <main.h>

#include "stm32-timer.h"

#if defined(STM32G4)
	#include <stm32g4xx.h>
#elif defined(STM32H7)
	#include <stm32h7xx.h>
#elif defined(STM32U5)
	#include <stm32u5xx.h>
#else
	#error "stm32-timer service is not compatible with this MCU family"
#endif

#define MODULE_NAME "stm32-timer"


/**********************************************************************************************************************
 * Pwm interface implementation
 **********************************************************************************************************************/

/* CCR1..CCR4 are contiguous in the timer register block, so the channel index
 * derived from the Pwm interface position directly addresses the matching
 * capture/compare register. */
static volatile uint32_t *pwm_ccr(Stm32Timer *self, Pwm *pwm) {
	TIM_TypeDef *tim = self->tim;
	int ch = pwm - &(self->pwm[0]);
	return &tim->CCR1 + ch;
}


static pwm_ret_t pwm_set_pwm(Pwm *pwm, float duty) {
	Stm32Timer *self = pwm->parent;
	TIM_TypeDef *tim = self->tim;

	if (duty < 0.0f) {
		duty = 0.0f;
	}
	if (duty > 1.0f) {
		duty = 1.0f;
	}
	*pwm_ccr(self, pwm) = (uint32_t)(duty * (float)(tim->ARR + 1));

	return PWM_RET_OK;
}


static pwm_ret_t pwm_get_pwm(Pwm *pwm, float *duty) {
	Stm32Timer *self = pwm->parent;
	TIM_TypeDef *tim = self->tim;

	if (duty == NULL) {
		return PWM_RET_FAILED;
	}
	*duty = (float)(*pwm_ccr(self, pwm)) / (float)(tim->ARR + 1);

	return PWM_RET_OK;
}


static pwm_ret_t pwm_set_freq(Pwm *pwm, uint32_t freq_hz) {
	Stm32Timer *self = pwm->parent;
	TIM_TypeDef *tim = self->tim;

	if (self->tim_ck_hz == 0 || freq_hz == 0) {
		return PWM_RET_FAILED;
	}

	/* Keep the current prescaler and adjust the auto-reload to obtain the
	 * requested frequency. The period is shared by all four channels. */
	uint32_t presc = tim->PSC + 1;
	uint32_t arr = self->tim_ck_hz / (presc * freq_hz);
	if (arr == 0) {
		return PWM_RET_FAILED;
	}
	tim->ARR = arr - 1;

	return PWM_RET_OK;
}


static pwm_ret_t pwm_get_freq(Pwm *pwm, uint32_t *freq_hz) {
	Stm32Timer *self = pwm->parent;
	TIM_TypeDef *tim = self->tim;

	if (freq_hz == NULL || self->tim_ck_hz == 0) {
		return PWM_RET_FAILED;
	}
	uint32_t presc = tim->PSC + 1;
	uint32_t reload = tim->ARR + 1;
	*freq_hz = self->tim_ck_hz / (presc * reload);

	return PWM_RET_OK;
}


static const struct pwm_vmt stm32_timer_pwm_vmt = {
	.set_pwm = pwm_set_pwm,
	.get_pwm = pwm_get_pwm,
	.set_freq = pwm_set_freq,
	.get_freq = pwm_get_freq,
};


/**********************************************************************************************************************
 * Service implementation
 **********************************************************************************************************************/

stm32_timer_ret_t stm32_timer_init(Stm32Timer *self, void *timer_base, uint32_t tim_ck_hz) {
	memset(self, 0, sizeof(Stm32Timer));
	self->tim = timer_base;
	self->tim_ck_hz = tim_ck_hz;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("timer initialized at %p (%lu Hz)"),
		timer_base,
		(unsigned long)tim_ck_hz
	);

	return STM32_TIMER_RET_OK;
}


stm32_timer_ret_t stm32_timer_pwm_init(Stm32Timer *self, uint8_t channel, Pwm **pwm) {
	if (channel < 1 || channel > 4) {
		return STM32_TIMER_RET_FAILED;
	}
	TIM_TypeDef *tim = self->tim;

	/* Edge-aligned, up-counting, with the auto-reload register buffered. Safe to
	 * repeat for every channel as the timer configuration is shared. */
	tim->CR1 &= ~(TIM_CR1_DIR | TIM_CR1_CMS);
	tim->CR1 |= TIM_CR1_ARPE;

	/* Configure the requested channel as a PWM mode 1 output with its compare
	 * register buffered (output preload enabled), leaving the others untouched. */
	switch (channel) {
		case 1:
			tim->CCMR1 = (tim->CCMR1 & ~(TIM_CCMR1_OC1M | TIM_CCMR1_CC1S | TIM_CCMR1_OC1PE)) |
				TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1PE;
			tim->CCER |= TIM_CCER_CC1E;
			break;
		case 2:
			tim->CCMR1 = (tim->CCMR1 & ~(TIM_CCMR1_OC2M | TIM_CCMR1_CC2S | TIM_CCMR1_OC2PE)) |
				TIM_CCMR1_OC2M_2 | TIM_CCMR1_OC2M_1 | TIM_CCMR1_OC2PE;
			tim->CCER |= TIM_CCER_CC2E;
			break;
		case 3:
			tim->CCMR2 = (tim->CCMR2 & ~(TIM_CCMR2_OC3M | TIM_CCMR2_CC3S | TIM_CCMR2_OC3PE)) |
				TIM_CCMR2_OC3M_2 | TIM_CCMR2_OC3M_1 | TIM_CCMR2_OC3PE;
			tim->CCER |= TIM_CCER_CC3E;
			break;
		case 4:
			tim->CCMR2 = (tim->CCMR2 & ~(TIM_CCMR2_OC4M | TIM_CCMR2_CC4S | TIM_CCMR2_OC4PE)) |
				TIM_CCMR2_OC4M_2 | TIM_CCMR2_OC4M_1 | TIM_CCMR2_OC4PE;
			tim->CCER |= TIM_CCER_CC4E;
			break;
		default:
			return STM32_TIMER_RET_FAILED;
	}

	/* Advanced-control timers gate the outputs behind the main output enable. */
	tim->BDTR |= TIM_BDTR_MOE;

	/* Zero the compare register before the update event latches it — this guarantees
	 * the channel starts at 0 % duty regardless of the register's previous contents. */
	(&tim->CCR1)[channel - 1] = 0;

	/* Latch the buffered prescaler/auto-reload values and start the counter. */
	tim->EGR |= TIM_EGR_UG;
	tim->CR1 |= TIM_CR1_CEN;

	/* Wire up the channel's Pwm interface. The interfaces share a vmt but have
	 * distinct addresses so the channel can be recovered from the pointer. */
	Pwm *p = &self->pwm[channel - 1];
	p->parent = self;
	p->vmt = &stm32_timer_pwm_vmt;

	if (pwm != NULL) {
		*pwm = p;
	}

	return STM32_TIMER_RET_OK;
}
