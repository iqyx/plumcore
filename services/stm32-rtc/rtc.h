/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 internal RTC clock driver service
 *
 * Copyright (c) 2018-2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include <main.h>
#include <interfaces/clock.h>

#if defined(CONFIG_SERVICE_STM32_RTC_CONF)
	#include <interfaces/conf.h>
	#include <configlib.h>
#endif


typedef enum {
	STM32_RTC_RET_OK = 0,
	STM32_RTC_RET_FAILED,
	STM32_RTC_RET_NULL,
} stm32_rtc_ret_t;

/* Configuration passed to stm32_rtc_init(). Not typedef'd per project policy. */
struct stm32_rtc_conf {
	/** Try to clock the RTC from the 32.768 kHz LSE crystal. The less accurate LSI is used if the LSE is not
	 * requested or it fails to start. Ignored if the RTC clock source was already selected (it can be changed
	 * only by a backup domain reset). */
	bool lse;
};

typedef struct {
	struct stm32_rtc_conf conf;

	/* Wall-clock interface (get/set/shift). */
	Clock clock;

#if defined(CONFIG_SERVICE_STM32_RTC_CONF)
	/* Configuration subtree of the RTC with a read/write leaf for each UTC date and time field. */
	struct {
		ConfiglibValue rtc;
		ConfiglibValue year;
		ConfiglibValue month;
		ConfiglibValue day;
		ConfiglibValue hour;
		ConfiglibValue minute;
		ConfiglibValue second;
	} conf_tree;

	/* Backing fields of the date and time leaves, refreshed from the RTC on each access. */
	struct {
		uint16_t year;
		uint8_t month;
		uint8_t day;
		uint8_t hour;
		uint8_t minute;
		uint8_t second;
	} date;
#endif
} Stm32Rtc;


stm32_rtc_ret_t stm32_rtc_init(Stm32Rtc *self, const struct stm32_rtc_conf *conf);
stm32_rtc_ret_t stm32_rtc_free(Stm32Rtc *self);

clock_ret_t stm32_rtc_get(Stm32Rtc *self, struct timespec *time);
clock_ret_t stm32_rtc_set(Stm32Rtc *self, const struct timespec *time);
clock_ret_t stm32_rtc_shift(Stm32Rtc *self, int32_t time_ns);
