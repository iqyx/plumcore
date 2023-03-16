/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Power manager for the STM32L4 MCU family
 *
 * Copyright (c) 2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>

#include <main.h>

#include <libopencm3/cm3/scb.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/pwr.h>
#include <libopencm3/stm32/lptimer.h>
#include <libopencm3/stm32/usart.h>

#include <interfaces/pm.h>

#include "stm32-l4-pm.h"

#define MODULE_NAME "stm32-l4-pm"

/* The global singleton instance used to handle service IRQs. */
extern Stm32L4Pm STM32_L4_PM_LPTIM_IRQ_DELEGATE;
Stm32L4Pm *irq_delegate = &STM32_L4_PM_LPTIM_IRQ_DELEGATE;

/* LPTIM1 is a 16 bit timer. To get reasonable time span, we need to extend it to 32 bit.
 * Increase the extension on overflow. This is a global, instance independent. */
static volatile uint16_t lptim1_ext;


static void stm32_l4_pm_schedule_wakeup(clock_t increment_ticks, clock_t *current_ticks) {
	clock_t this_tick = 0;
	while (this_tick != lptimer_get_counter(STM32_L4_PM_LPTIM)) {
		this_tick = lptimer_get_counter(STM32_L4_PM_LPTIM);
	}
	clock_t next_tick = (this_tick + increment_ticks) % STM32_L4_PM_LPTIM_ARR;

	/* Cannot write 0xffff to the compare register. Cheat a bit in this case.
	 * This happens only if we try to go too much to the future. */
	if (next_tick == 0xffff) {
		next_tick = 0;
	}
	lptimer_set_compare(STM32_L4_PM_LPTIM, next_tick);
	if (current_ticks != NULL) {
		*current_ticks = this_tick;
	}
}


static clock_t lptim_time_till_now(clock_t lptim, clock_t last_time) {
	uint16_t time_now = 0;
	while (time_now != lptimer_get_counter(lptim)) {
		time_now = lptimer_get_counter(lptim);
	}

	return time_now - last_time;
}


static bool stm32_l4_pm_check_wakelock(Stm32L4Pm *self, enum pm_state state) {
	(void)self;
	(void)state;
	/** @todo Check sleep wakelock */

	/** @deprecated */
	/* Check if all IO transmissions are completed.
	 * Going to sleep in the middle of a tx/rx would corrupt the data. */
	return ((USART_CR1(USART1) & USART_CR1_UE) && ((USART_ISR(USART1) & USART_ISR_TC) == 0)) ||
	       ((USART_CR1(USART2) & USART_CR1_UE) && ((USART_ISR(USART2) & USART_ISR_TC) == 0));
}


static void stm32_l4_pm_enter_low_power(Stm32L4Pm *self, clock_t len) {

	/* Enter a critical section first, disabling any interrupts. */
	__asm volatile("cpsid i");
	__asm volatile("dsb");
	__asm volatile("isb");

	/* Check if any interrupts are pending. This may happen if an interrupt was scheduled before
	 * we entered the critical section. Abort in the middle. */
	eSleepModeStatus sleep_status = eTaskConfirmSleepModeStatus();
	if (sleep_status == eAbortSleep) {
		__asm volatile("cpsie i");
		return;
	}

	systick_counter_disable();

	PWR_CR1 &= ~PWR_CR1_LPMS_MASK;
	SCB_SCR &= ~SCB_SCR_SLEEPDEEP;
	if (stm32_l4_pm_check_wakelock(self, PM_STATE_S3) == false) {
		/* Enter STOP 1 (S3) mode. */
		if (len > STM32_L4_PM_MAX_STOP1_TIME) {
			len = STM32_L4_PM_MAX_STOP1_TIME;
		}
		SCB_SCR |= SCB_SCR_SLEEPDEEP;
		PWR_CR1 |= PWR_CR1_LPMS_STOP_1;
	} else {
		/* Enter SLEEP mode only. */
	}

	/* Wake up in the future. Next tick is calculated and properly cropped inside */
	clock_t time_before_sleep = 0;
	stm32_l4_pm_schedule_wakeup(len * STM32_L4_PM_LPTIM_PER_TICK, &time_before_sleep);

	__asm volatile("dsb");
	__asm volatile("wfi");
	__asm volatile("isb");

	/* Woken up now. */
	clock_t ticks_completed = lptim_time_till_now(LPTIM1, time_before_sleep) / STM32_L4_PM_LPTIM_PER_TICK;
	if (ticks_completed > len) {
		ticks_completed = len;
	}
	vTaskStepTick(ticks_completed);

	systick_counter_enable();
	__asm volatile("cpsie i");
}


/* Implementation of low power sleeping during tickless idle for FreeRTOS. */
#if defined(configUSE_TICKLESS_IDLE)
void stm32_l4_pm_freertos_low_power(TickType_t idle_time) {
	stm32_l4_pm_enter_low_power(irq_delegate, (clock_t)idle_time);
}
#endif


/* libopencm3 LPTIM1 IRQ handler */
void lptim1_isr(void) {
	if (lptimer_get_flag(LPTIM1, LPTIM_ISR_CMPM)) {
		lptimer_clear_flag(LPTIM1, LPTIM_ICR_CMPMCF);
		/* The MCU is woken up here. Execution continues after WFI. */
	}

	if (lptimer_get_flag(LPTIM1, LPTIM_ISR_ARRM)) {
		lptimer_clear_flag(LPTIM1, LPTIM_ICR_ARRMCF);
		lptim1_ext++;
	}
}


clock_t stm32_l4_pm_clock(Stm32L4Pm *self) {
	(void)self;

	uint16_t e = lptim1_ext;
	uint16_t b = lptimer_get_counter(STM32_L4_PM_LPTIM);
	uint16_t e2 = lptim1_ext;
	if (e != e2) {
		if (b > 32768) {
			return (e << 16) | b;
		} else {
			return (e2 << 16) | b;
		}
	}
	return (e << 16) | b;
}


stm32_l4_pm_ret_t stm32_l4_pm_lptimer_enable(void) {
	lptimer_disable(LPTIM1);
	lptimer_set_internal_clock_source(LPTIM1);
	lptimer_enable_trigger(LPTIM1, LPTIM_CFGR_TRIGEN_SW);
	lptimer_set_prescaler(LPTIM1, LPTIM_CFGR_PRESC_1);

	lptimer_enable(LPTIM1);
	lptimer_set_period(LPTIM1, STM32_L4_PM_LPTIM_ARR - 1);
	lptimer_set_compare(LPTIM1, 0 + 31);
	lptimer_enable_irq(LPTIM1, LPTIM_IER_CMPMIE);
	nvic_set_priority(NVIC_LPTIM1_IRQ, 5 * 16);
	nvic_enable_irq(NVIC_LPTIM1_IRQ);
	lptimer_start_counter(LPTIM1, LPTIM_CR_CNTSTRT);

	return STM32_L4_PM_RET_OK;
}


static pm_ret_t acquire_wakelock(Pm *pm, WakeLock *wl, enum pm_state wl_state) {
	Stm32L4Pm *self = (Stm32L4Pm *)pm->parent;

	return PM_RET_FAILED;
}


static pm_ret_t release_wakelock(Pm *pm, WakeLock *wl) {
	Stm32L4Pm *self = (Stm32L4Pm *)pm->parent;

	return PM_RET_FAILED;
}


static const struct pm_vmt stm32_l4_pm_pm_vmt = {
	.acquire_wakelock = acquire_wakelock,
	.release_wakelock = release_wakelock
};


stm32_l4_pm_ret_t stm32_l4_pm_init(Stm32L4Pm *self) {
	memset(self, 0, sizeof(Stm32L4Pm));

	self->pm.vmt = &stm32_l4_pm_pm_vmt;
	self->pm.parent = self;

	self->pm_ok = true;
	return STM32_L4_PM_RET_OK;
}


stm32_l4_pm_ret_t stm32_l4_pm_free(Stm32L4Pm *self) {
	(void)self;
	return STM32_L4_PM_RET_OK;
}

