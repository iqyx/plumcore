/*
 * STM32 internal RTC clock driver service.
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

#include <libopencm3/stm32/rtc.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/pwr.h>

#include "rtc.h"
#include "interfaces/clock/descriptor.h"


#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "stm32-rtc"


stm32_rtc_ret_t stm32_rtc_get(Stm32Rtc *self, struct timespec *time) {
	if (u_assert(self != NULL)) {
		return STM32_RTC_RET_NULL;
	}
	if (u_assert(time != NULL)) {
		return STM32_RTC_RET_FAILED;
	}

	struct tm ts = {0};
	uint32_t tr = RTC_TR;
	ts.tm_hour = ((tr >> RTC_TR_HT_SHIFT) & RTC_TR_HT_MASK) * 10 + ((tr >> RTC_TR_HU_SHIFT) & RTC_TR_HU_MASK);
	ts.tm_min = ((tr >> RTC_TR_MNT_SHIFT) & RTC_TR_MNT_MASK) * 10 + ((tr >> RTC_TR_MNU_SHIFT) & RTC_TR_MNU_MASK);
	ts.tm_sec = ((tr >> RTC_TR_ST_SHIFT) & RTC_TR_ST_MASK) * 10 + ((tr >> RTC_TR_SU_SHIFT) & RTC_TR_SU_MASK);
	uint32_t dr = RTC_DR;
	ts.tm_year = 100 + ((dr >> RTC_DR_YT_SHIFT) & RTC_DR_YT_MASK) * 10 + ((dr >> RTC_DR_YU_SHIFT) & RTC_DR_YU_MASK);
	ts.tm_mon = ((dr >> RTC_DR_MT_SHIFT) & 1) * 10 + ((dr >> RTC_DR_MU_SHIFT) & RTC_DR_MU_MASK) - 1;
	ts.tm_mday = ((dr >> RTC_DR_DT_SHIFT) & RTC_DR_DT_MASK) * 10 + ((dr >> RTC_DR_DU_SHIFT) & RTC_DR_DU_MASK);

	/** @todo get subseconds */

	time->tv_sec = mktime(&ts);
	time->tv_nsec = 0;

	return STM32_RTC_RET_OK;
}


stm32_rtc_ret_t stm32_rtc_set(Stm32Rtc *self, struct timespec *time) {
	if (u_assert(self != NULL)) {
		return STM32_RTC_RET_NULL;
	}
	if (u_assert(time != NULL)) {
		return STM32_RTC_RET_FAILED;
	}

	struct tm ts = {0};
	localtime_r(&time->tv_sec, &ts);

	uint32_t tr =
		(((ts.tm_hour / 10) & RTC_TR_HT_MASK) << RTC_TR_HT_SHIFT) +
		(((ts.tm_hour % 10) & RTC_TR_HU_MASK) << RTC_TR_HU_SHIFT) +
		(((ts.tm_min / 10) & RTC_TR_MNT_MASK) << RTC_TR_MNT_SHIFT) +
		(((ts.tm_min % 10) & RTC_TR_MNU_MASK) << RTC_TR_MNU_SHIFT) +
		(((ts.tm_sec / 10) & RTC_TR_ST_MASK) << RTC_TR_ST_SHIFT) +
		(((ts.tm_sec % 10) & RTC_TR_SU_MASK) << RTC_TR_SU_SHIFT);

	uint32_t dr =
		((((ts.tm_year - 100) / 10) & RTC_DR_YT_MASK) << RTC_DR_YT_SHIFT) +
		((((ts.tm_year - 100) % 10) & RTC_DR_YU_MASK) << RTC_DR_YU_SHIFT) +
		((((ts.tm_mon + 1) / 10) & 1) << RTC_DR_MT_SHIFT) +
		((((ts.tm_mon + 1) % 10) & RTC_DR_MU_MASK) << RTC_DR_MU_SHIFT) +
		(((ts.tm_mday / 10) & RTC_DR_DT_MASK) << RTC_DR_DT_SHIFT) +
		(((ts.tm_mday % 10) & RTC_DR_DU_MASK) << RTC_DR_DU_SHIFT);

	/* Unlock the RTC write access. */
	#if defined(STM32L4)
		PWR_CR1 |= PWR_CR1_DBP;
	#else
		PWR_CR |= PWR_CR_DBP;
	#endif
	RTC_WPR = (uint8_t)0xca;
	RTC_WPR = (uint8_t)0x53;

	/* Init mode to set the clock. */
	RTC_ISR |= RTC_ISR_INIT;
	vTaskDelay(50);
	if (!(RTC_ISR & RTC_ISR_INITF)) {
		return STM32_RTC_RET_FAILED;
	}

	RTC_TR = tr;
	RTC_DR = dr;

	/* End the init mode. */
	RTC_ISR &= ~RTC_ISR_INIT;
	RTC_WPR = 0xff;

	return STM32_RTC_RET_OK;
}


/* Implementation of the IClock interface clock_get method. */
static iclock_ret_t iclock_clock_get(void *context, struct timespec *time) {
	return stm32_rtc_get((Stm32Rtc *)context, time);
}


/* Implementation of the IClock interface clock_set method. */
static iclock_ret_t iclock_clock_set(void *context, struct timespec *time) {
	return stm32_rtc_set((Stm32Rtc *)context, time);
}


static iclock_ret_t iclock_clock_get_source(void *context, enum iclock_source *source) {
	*source = ICLOCK_SOURCE_UNKNOWN;
	return ICLOCK_RET_OK;
}


static iclock_ret_t iclock_clock_get_status(void *context, enum iclock_status *status) {
	*status = ICLOCK_STATUS_UNKNOWN;
	return ICLOCK_RET_OK;
}


static stm32_rtc_ret_t stm32_rtc_set_clock(Stm32Rtc *self) {
	if (u_assert(self != NULL)) {
		return STM32_RTC_RET_NULL;
	}

	if (self->lse_available) {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("using LSE as the RTC clock source"));
		RCC_BDCR |= (1 << 8);
	} else {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("using LSI as the RTC clock source, the clock may be inaccurate"));
		RCC_BDCR |= (2 << 8);
	}

	return STM32_RTC_RET_OK;
}


static stm32_rtc_ret_t stm32_rtc_init_rtc(Stm32Rtc *self) {
	if (u_assert(self != NULL)) {
		return STM32_RTC_RET_NULL;
	}

	#if defined(STM32L4)
		PWR_CR1 |= PWR_CR1_DBP;
	#else
		PWR_CR |= PWR_CR_DBP;
	#endif
	stm32_rtc_set_clock(self);
	RCC_BDCR |= RCC_BDCR_RTCEN;

	/* Unlock the RTC write access. */
	RTC_WPR = (uint8_t)0xca;
	RTC_WPR = (uint8_t)0x53;

	/* Initialize the RTC. */
	RTC_ISR |= RTC_ISR_INIT;
	uint32_t timeout = 20;
	while (timeout > 0) {
		if (RTC_ISR & RTC_ISR_INITF) {
			break;
		}
		vTaskDelay(10);
		timeout--;
	}
	if (!(RTC_ISR & RTC_ISR_INITF)) {
		u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("tu som"));
		return STM32_RTC_RET_FAILED;
	}

	/* Write prescaler twice. */
	/* PREDIV_A = 127, PREDIV_S = 255 */
	uint32_t prediv = (0x7f << 16) | 0xff;
	RTC_PRER = prediv;
	RTC_PRER = prediv;

	RTC_DR |= (1 << 16);

	/* End the initialization and lock the registers. */
	RTC_ISR &= ~RTC_ISR_INIT;
	RTC_WPR = 0xff;

	return STM32_RTC_RET_OK;
}


stm32_rtc_ret_t stm32_rtc_init(Stm32Rtc *self) {
	if (u_assert(self != NULL)) {
		return STM32_RTC_RET_NULL;
	}

	memset(self, 0, sizeof(Stm32Rtc));

	iclock_init(&self->iface);
	self->iface.vmt.clock_get = iclock_clock_get;
	self->iface.vmt.clock_set = iclock_clock_set;
	self->iface.vmt.clock_get_source = iclock_clock_get_source;
	self->iface.vmt.clock_get_status = iclock_clock_get_status;
	self->iface.vmt.context = (void *)self;

	/* Enable the peripheral and disable backup domain write protection. */
	rcc_periph_clock_enable(RCC_PWR);

	#if defined(STM32L4)
		PWR_CR1 |= PWR_CR1_DBP;
	#else
		rcc_periph_clock_enable(RCC_RTC);
		PWR_CR |= PWR_CR_DBP;
	#endif

	/* Enable LSE oscillator and wait for it to stabilize. */
	RCC_BDCR |= RCC_BDCR_LSEON;
	self->lse_available = false;
	uint32_t timeout = 20;
	while (timeout > 0) {
		if (RCC_BDCR & RCC_BDCR_LSERDY) {
			self->lse_available = true;
			break;
		}
		vTaskDelay(100);
		timeout--;
	}

	if (self->lse_available == false) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("LSE oscillator is not available"));

		/* Enable LSI. */
		RCC_CSR |= RCC_CSR_LSION;
		vTaskDelay(50);
		if (!(RCC_CSR & RCC_CSR_LSIRDY)) {
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("LSI oscillator is not available, failed to initialize the RTC clock"));
			return STM32_RTC_RET_FAILED;
		}
	}

	if (RTC_ISR & RTC_ISR_INITS) {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("RTC ready, skipping initialization"));
	} else {
		if (stm32_rtc_init_rtc(self) == STM32_RTC_RET_OK) {
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("RTC initialized"));
		} else {
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot initialize the RTC"));
			return STM32_RTC_RET_FAILED;
		}
	}

	self->initialized = true;
	return STM32_RTC_RET_OK;
}


stm32_rtc_ret_t stm32_rtc_free(Stm32Rtc *self) {
	if (self == NULL) {
		return STM32_RTC_RET_NULL;
	}

	if (self->initialized == false) {
		return STM32_RTC_RET_FAILED;
	}

	self->initialized = false;
	return STM32_RTC_RET_OK;
}


