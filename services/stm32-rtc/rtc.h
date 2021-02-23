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

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "module.h"
#include "interfaces/clock/descriptor.h"
#include "interfaces/clock.h"


typedef enum {
	STM32_RTC_RET_OK = 0,
	STM32_RTC_RET_FAILED,
	STM32_RTC_RET_NULL,
} stm32_rtc_ret_t;


typedef struct {
	Module module;
	bool initialized;
	IClock iface;
	bool lse_available;
	Clock clock;
} Stm32Rtc;


stm32_rtc_ret_t stm32_rtc_init(Stm32Rtc *self);
stm32_rtc_ret_t stm32_rtc_free(Stm32Rtc *self);

clock_ret_t stm32_rtc_get(Stm32Rtc *self, struct timespec *time);
clock_ret_t stm32_rtc_set(Stm32Rtc *self, const struct timespec *time);
clock_ret_t stm32_rtc_shift(Stm32Rtc *self, int32_t time_ns);

