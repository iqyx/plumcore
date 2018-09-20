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

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "module.h"
#include "interfaces/clock/descriptor.h"


typedef enum {
	SYSTEM_CLOCK_RET_OK = 0,
	SYSTEM_CLOCK_RET_FAILED,
	SYSTEM_CLOCK_RET_NULL,
} system_clock_ret_t;


typedef struct {
	Module module;

	bool initialized;

	uint32_t timer;
	uint32_t prescaler;
	uint32_t period;

	uint32_t overflows;

	IClock iface;

} SystemClock;


system_clock_ret_t system_clock_init(SystemClock *self, uint32_t timer, uint32_t prescaler, uint32_t period);
system_clock_ret_t system_clock_free(SystemClock *self);
system_clock_ret_t system_clock_overflow_handler(SystemClock *self);
system_clock_ret_t system_clock_get(SystemClock *self, struct timespec *time);
system_clock_ret_t system_clock_set(SystemClock *self, struct timespec *time);

