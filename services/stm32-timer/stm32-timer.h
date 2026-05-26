/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 timer driver
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>

#include <interfaces/pwm.h>

typedef enum {
	STM32_TIMER_RET_OK = 0,
	STM32_TIMER_RET_FAILED,
} stm32_timer_ret_t;


typedef struct {
	void *tim;

	/* Timer kernel clock in Hz. Used by the Pwm set_freq/get_freq methods
	 * to translate between frequency and the prescaler/auto-reload registers. */
	uint32_t tim_ck_hz;

	/* One Pwm interface per capture/compare channel (CH1..CH4). */
	Pwm pwm[4];

} Stm32Timer;

stm32_timer_ret_t stm32_timer_init(Stm32Timer *self, void *timer_base, uint32_t tim_ck_hz);

/* Configure a single capture/compare channel (1..4) for PWM output. On success
 * the channel's Pwm interface is returned through @p pwm. */
stm32_timer_ret_t stm32_timer_pwm_init(Stm32Timer *self, uint8_t channel, Pwm **pwm);
