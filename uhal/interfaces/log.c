/*
 * Copyright (c) 2016, Marek Koza (qyx@krtko.org)
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

#include "log.h"


interface_log_ret_t interface_log_init(ILog *self) {
	memset(self, 0, sizeof(ILog));

	return INTERFACE_LOG_RET_OK;
}


interface_log_ret_t interface_log_free(ILog *self) {
	(void)self;

	return INTERFACE_LOG_RET_OK;
}


interface_log_ret_t interface_log_message(ILog *self, struct interface_log_time time, const char *module, enum interface_log_level level, const char *message) {
	if (self == NULL) {
		return INTERFACE_LOG_RET_FAILED;
	}
	if (self->vmt.message) {
		return self->vmt.message(self->vmt.context, time, module, level, message);
	} else

	return INTERFACE_LOG_RET_FAILED;
}


interface_log_ret_t interface_log_printf(ILog *self, const char *module, enum interface_log_level level, const char *fmt, ...) {
	if (self == NULL) {
		return INTERFACE_LOG_RET_FAILED;
	}

	va_list args;
	char str[INTERFACE_LOG_MAX_MESSAGE_LEN];

	va_start(args, fmt);
	vsnprintf(str, sizeof(str), fmt, args);
	va_end(args);

	struct interface_log_time time;
	memset(&time, 0, sizeof(struct interface_log_time));

	if (self->vmt.message) {
		return self->vmt.message(self->vmt.context, time, module, level, str);
	} else

	return INTERFACE_LOG_RET_FAILED;
}
