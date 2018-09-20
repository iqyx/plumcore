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

#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

#include "interface.h"
#include "interfaces/clock/descriptor.h"


iclock_ret_t iclock_init(IClock *self) {
	if(self == NULL) {
		return ICLOCK_RET_FAILED;
	}

	memset(self, 0, sizeof(IClock));
	uhal_interface_init(&self->interface);

	return ICLOCK_RET_OK;
}


iclock_ret_t iclock_free(IClock *self) {
	if (self == NULL) {
		return ICLOCK_RET_FAILED;
	}

	memset(self, 0, sizeof(IClock));

	return ICLOCK_RET_OK;
}


iclock_ret_t iclock_get(IClock *self, struct timespec *time) {
	if (self == NULL) {
		return ICLOCK_RET_NULL;
	}
	if (time == NULL) {
		return ICLOCK_RET_BAD_ARG;
	}

	if (self->vmt.clock_get != NULL) {
		return self->vmt.clock_get(self->vmt.context, time);
	}

	return ICLOCK_RET_FAILED;
}


iclock_ret_t iclock_set(IClock *self, struct timespec *time) {
	if (self == NULL) {
		return ICLOCK_RET_NULL;
	}
	if (time == NULL) {
		return ICLOCK_RET_BAD_ARG;
	}

	if (self->vmt.clock_set != NULL) {
		return self->vmt.clock_set(self->vmt.context, time);
	}

	return ICLOCK_RET_FAILED;
}


iclock_ret_t iclock_get_source(IClock *self, enum iclock_source *source) {
	if (self == NULL) {
		return ICLOCK_RET_NULL;
	}
	if (source == NULL) {
		return ICLOCK_RET_BAD_ARG;
	}

	if (self->vmt.clock_get_source != NULL) {
		return self->vmt.clock_get_source(self->vmt.context, source);
	}

	return ICLOCK_RET_FAILED;
}


iclock_ret_t iclock_get_status(IClock *self, enum iclock_status *status) {
	if (self == NULL) {
		return ICLOCK_RET_NULL;
	}
	if (status == NULL) {
		return ICLOCK_RET_BAD_ARG;
	}

	if (self->vmt.clock_get_status != NULL) {
		return self->vmt.clock_get_status(self->vmt.context, status);
	}

	return ICLOCK_RET_FAILED;
}
