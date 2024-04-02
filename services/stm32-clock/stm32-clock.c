/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 clock manager service
 *
 * Copyright (c) 2023-2024, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

#include <main.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/timer.h>

#include "stm32-clock.h"

#define MODULE_NAME "stm32-clock"


const char *state_str[] = {
	"default",
	"init-lse",
	"wait-lse",
	"lse-ok",
	"lse-done",
	"init-hsi16",
	"wait-hsi16",
	"hsi16-ok",
	"init-hse",
	"wait-hse",
	"hse-ok",

	"init-done",
	"check",
	"failed",
};

const uint32_t freq_hse_allowed[] = {
	4000000UL, 8000000UL, 12000000UL, 16000000UL,
	19200000UL, 20000000UL, 24000000UL, 25000000UL,
	26000000UL, 32000000UL, 0UL
};


static uint32_t match_hse_freq(uint32_t f) {
	for (uint32_t i = 0; freq_hse_allowed[i] != 0; i++) {
		if (abs(freq_hse_allowed[i] - f) < (freq_hse_allowed[i] / 100)) {
			return freq_hse_allowed[i];
		}
	}
	return 0;
}


#define TIM_TISEL(tim_base) MMIO32((tim_base) + 0x5c)
#define TIM_OR1(tim_base) MMIO32((tim_base) + 0x68)

static uint32_t meas_timer_get_hz(Stm32Clock *self, enum stm32_clock_src c) {
	/* Keep prescaler at 0, run at tim_ker_ck, keep ARR at 0xffff, just enable the timer. */
	RCC_APB2ENR |= RCC_APB2ENR_TIM16EN;
	TIM_CR1(TIM16) |= TIM_CR1_CEN;

	switch (c) {
		default:
		case STM32_CLOCK_SRC_LSE:
			TIM_TISEL(TIM16) = 5;
			break;

		case STM32_CLOCK_SRC_HSE:
			TIM_TISEL(TIM16) = 3;
			TIM_OR1(TIM16) |= 1;
			break;
	}

	/* Select TI1 as CC1 input and enable /8 prescaler. */
	TIM_CCMR1(TIM16) = TIM_CCMR1_IC1PSC_8 | TIM_CCMR1_CC1S_IN_TI1;
	TIM_CCER(TIM16) |= TIM_CCER_CC1E;

	/* Catch stale CC1IF and reset it. */
	TIM_SR(TIM16) &= ~TIM_SR_CC1IF;

	/* Catch two capture events. */
	while (!(TIM_SR(TIM16) & TIM_SR_CC1IF)) ;
	uint16_t cc1 = TIM_CCR1(TIM16);

	while (!(TIM_SR(TIM16) & TIM_SR_CC1IF)) ;
	uint16_t cc2 = TIM_CCR1(TIM16);

	/* Disable input capture. */
	TIM_CCER(TIM16) &= ~TIM_CCER_CC1E;
	TIM_CR1(TIM16) &= ~TIM_CR1_CEN;

	return self->tim_ker_ck * 8UL / (uint16_t)(cc2 - cc1) * (c == STM32_CLOCK_SRC_HSE ? 32UL : 1UL);
}


static void set_state(Stm32Clock *self, enum stm32_clock_state state) {
	// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("state '%s' -> '%s'"), state_str[self->state], state_str[state]);
	self->state = state;
	if (state == STM32_CLOCK_STATE_INIT_DONE) {
		xSemaphoreGive(self->init_done);
	}
}


static void stm32_clock_step(Stm32Clock *self) {
	switch (self->state) {
		case STM32_CLOCK_STATE_DEFAULT:
		default:
			set_state(self, STM32_CLOCK_STATE_INIT_HSI16);
			break;

		/**
		 * LSE initialisation
		 */

		case STM32_CLOCK_STATE_INIT_LSE:
			/** @todo skip LSE init entirely if configured to do so (for devices without LSE) */
			if (false) {
				u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("skipping LSE init"));
				set_state(self, STM32_CLOCK_STATE_LSE_DONE);
			}

			if (RCC_BDCR & RCC_BDCR_LSERDY) {
				u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("LSE already enabled and ready"));
				set_state(self, STM32_CLOCK_STATE_LSE_OK);
			} else {
				u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("enabling LSE"));
				RCC_BDCR |= RCC_BDCR_LSEON;
				self->timeout_ms = 1000;
				set_state(self, STM32_CLOCK_STATE_WAIT_LSE);
			}
			break;

		case STM32_CLOCK_STATE_WAIT_LSE:
			if (self->timeout_ms == 0) {
				/* Not an error, LSE may not be populated or used at all. Just a warning the situation is not nominal. */
				u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("failed to start the LSE oscillator"));
				set_state(self, STM32_CLOCK_STATE_LSE_DONE);
			}
			if (RCC_BDCR & RCC_BDCR_LSERDY) {
				set_state(self, STM32_CLOCK_STATE_LSE_OK);
			}
			break;

		case STM32_CLOCK_STATE_LSE_OK: {
			/* Measure the approximate LSE frequency and try to match to the the nominal frequency.
			 * The measurement may not be precise, so do a little guessing. HSI16 frequency precision
			 * is definitely better than 1% according to the datasheet. */
			self->freq_lse_hz = meas_timer_get_hz(self, STM32_CLOCK_SRC_LSE);

			/* Check if LSE freq is within 1% range (HSI16 rough accuracy). */
			if (self->freq_lse_hz > 32440 && self->freq_lse_hz < 33096) {
				self->freq_nom_lse_hz = 32768;

				/* Now we can consider LSE to be precise and can compute HSI16. */
				self->freq_hsi16_hz = self->freq_lse_hz * (self->tim_ker_ck / 1000LU) / self->freq_nom_lse_hz * 1000LU;
			} else {
				self->freq_nom_lse_hz = 0;
				u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("LSE freq out of range (%lu Hz)"), self->freq_lse_hz);
			}
			set_state(self, STM32_CLOCK_STATE_LSE_DONE);
			break;
		}

		case STM32_CLOCK_STATE_LSE_DONE:
			set_state(self, STM32_CLOCK_STATE_INIT_HSE);
			break;

		/**
		 * HSI16 initialisation
		 */

		case STM32_CLOCK_STATE_INIT_HSI16:
			/* The default HSI16 clock may be disabled for various reasons (eg. bootloader not tidying up properly.
			 * Reenable it as we need it to measure HSE later. If already enabled and ready, skip. */
			if (RCC_CR & RCC_CR_HSIRDY) {
				set_state(self, STM32_CLOCK_STATE_HSI16_OK);
			} else {
				u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("reenabling HSI16 (not enabled by default as it should be)"));
				RCC_CR |= RCC_CR_HSION;
				self->timeout_ms = 1000;
				set_state(self, STM32_CLOCK_STATE_WAIT_HSI16);
			}
			break;

		case STM32_CLOCK_STATE_WAIT_HSI16:
			if (self->timeout_ms == 0) {
				u_log(system_log, LOG_TYPE_CRIT, U_LOG_MODULE_PREFIX("cannot enable HSI16"));
				set_state(self, STM32_CLOCK_STATE_FAILED);
			}
			if (RCC_CR & RCC_CR_HSIRDY) {
				set_state(self, STM32_CLOCK_STATE_HSI16_OK);
			}
			break;

		case STM32_CLOCK_STATE_HSI16_OK:
			/* Consider hsi16 frequency 16 MHz for now. We cannot measure it precisely. */
			self->freq_hsi16_hz = 16000000UL;

			/* Set HSI16 as SYSCLK */
			rcc_set_sysclk_source(RCC_CFGR_SWx_HSI16);
			self->tim_ker_ck = 16000000UL;
			rcc_ahb_frequency = 16000000UL;
			rcc_apb1_frequency = 16000000UL;
			rcc_apb2_frequency = 16000000UL;

			set_state(self, STM32_CLOCK_STATE_INIT_LSE);
			break;

		/**
		 * HSE initialisation
		 */

		case STM32_CLOCK_STATE_INIT_HSE:
			if (RCC_CR & RCC_CR_HSERDY) {
				u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("HSE already enabled and ready"));
				set_state(self, STM32_CLOCK_STATE_HSE_OK);
			} else {
				u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("enabling HSE"));
				RCC_CR |= RCC_CR_HSEON;
				self->timeout_ms = 200;
				set_state(self, STM32_CLOCK_STATE_WAIT_HSE);
			}
			break;

		case STM32_CLOCK_STATE_WAIT_HSE:
			if (self->timeout_ms == 0) {
				/* If no HSE is available or cannot start, continue. Try to use a different clock later. */
				u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("cannot enable HSE"));
				set_state(self, STM32_CLOCK_STATE_INIT_DONE);
			}
			if (RCC_CR & RCC_CR_HSERDY) {
				set_state(self, STM32_CLOCK_STATE_HSE_OK);
			}
			break;

		case STM32_CLOCK_STATE_HSE_OK: {
			/* Measure the approximate HSE frequency and try to match to the the nominal frequency. */
			self->freq_hse_hz = meas_timer_get_hz(self, STM32_CLOCK_SRC_HSE);
			self->freq_nom_hse_hz = match_hse_freq(self->freq_hse_hz);

			set_state(self, STM32_CLOCK_STATE_INIT_DONE);
			break;
		}


		case STM32_CLOCK_STATE_INIT_DONE:
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("clocks LSE(nom) = %lu Hz, HSI16 = %lu Hz, HSE(nom) = %lu Hz"), self->freq_nom_lse_hz, self->freq_hsi16_hz, self->freq_nom_hse_hz);
			set_state(self, STM32_CLOCK_STATE_CHECK);
			break;

		case STM32_CLOCK_STATE_CHECK:
			self->step_interval_ms = 1000;
			break;

		case STM32_CLOCK_STATE_FAILED:
			self->step_interval_ms = 1000;
			break;
	}
}


static void stm32_clock_task(void *p) {
	Stm32Clock *self = p;

	self->rough_time_ms = 0;
	/* Perform steps fast until the init is done. */
	self->step_interval_ms = 10;
	while (true) {
		stm32_clock_step(self);
		vTaskDelay(self->step_interval_ms / portTICK_PERIOD_MS);
		self->rough_time_ms += self->step_interval_ms;
		if (self->timeout_ms > 0 && self->timeout_ms >= self->step_interval_ms) {
			self->timeout_ms -= self->step_interval_ms;
		} else {
			self->timeout_ms = 0;
		}
	}
}


stm32_clock_ret_t stm32_clock_init(Stm32Clock *self) {
	memset(self, 0, sizeof(Stm32Clock));


	self->init_done = xSemaphoreCreateBinary();
	xTaskCreate(stm32_clock_task, "stm32-clock", 256, self, 1, &self->handler_task);

	return STM32_CLOCK_RET_OK;
}


stm32_clock_ret_t stm32_clock_free(Stm32Clock *self) {
	(void)self;
	return STM32_CLOCK_RET_OK;
}


stm32_clock_ret_t stm32_clock_wait_init_done(Stm32Clock *self) {
	if (self->state == STM32_CLOCK_STATE_INIT_DONE) {
		return STM32_CLOCK_RET_OK;
	}
	xSemaphoreTake(self->init_done, portMAX_DELAY);

	return STM32_CLOCK_RET_OK;
}

