/*
 * clock interface descriptor
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

#include "interface.h"


typedef enum {
	ICLOCK_RET_OK = 0,
	ICLOCK_RET_FAILED,
	ICLOCK_RET_NULL,
	ICLOCK_RET_BAD_ARG,
} iclock_ret_t;


enum iclock_source {
	ICLOCK_SOURCE_UNKNOWN = 0,
};


enum iclock_status {
	ICLOCK_STATUS_UNKNOWN = 0,
};

struct iclock_vmt {
	iclock_ret_t (*clock_get)(void *context, struct timespec *time);
	iclock_ret_t (*clock_set)(void *context, struct timespec *time);
	iclock_ret_t (*clock_get_source)(void *context, enum iclock_source *source);
	iclock_ret_t (*clock_get_status)(void *context, enum iclock_status *status);
	void *context;
};


typedef struct iclock {
	Interface interface;
	struct iclock_vmt vmt;
} IClock;


iclock_ret_t iclock_init(IClock *self);
iclock_ret_t iclock_free(IClock *self);
iclock_ret_t iclock_get(IClock *self, struct timespec *time);
iclock_ret_t iclock_set(IClock *self, struct timespec *time);
iclock_ret_t iclock_get_source(IClock *self, enum iclock_source *source);
iclock_ret_t iclock_get_status(IClock *self, enum iclock_status *status);



