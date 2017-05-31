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
#include <stdio.h>

#include "interfaces/log.h"
#include "usart_logger.h"


static interface_log_ret_t message(void *context, struct interface_log_time time, const char *module, enum interface_log_level level, const char *message) {
	const char *t = "?";
	const char *c = "";

	switch (level) {
		case LOG_LEVEL_TRACE: t = "TRACE"; c = ESC_COLOR_FG_WHITE; break;
		case LOG_LEVEL_DEBUG: t = "DEBUG"; c = ESC_COLOR_FG_GREEN; break;
		case LOG_LEVEL_INFO: t = "INFO"; c = ESC_COLOR_FG_BLUE; break;
		case LOG_LEVEL_WARNING: t = "WARNING"; c = ESC_COLOR_FG_YELLOW; break;
		case LOG_LEVEL_ERROR: t = "ERROR"; c = ESC_COLOR_FG_MAGENTA; break;
		case LOG_LEVEL_CRITICAL: t = "CRITICAL"; c = ESC_COLOR_FG_RED; break;
		case LOG_LEVEL_ASSERT: t = "ASSERT"; c = ESC_COLOR_FG_RED; break;
		default: break;
	}

	printf(ESC_BOLD ESC_COLOR_FG_WHITE "[00:00:00T2000-00-00Z]" ESC_BOLD " %s%s " ESC_DEFAULT ESC_COLOR_FG_YELLOW "%s: " ESC_DEFAULT "%s\n", c, t, module, message);
	return INTERFACE_LOG_RET_OK;
}


void usart_logger_init(UsartLogger *self, uint32_t port) {

	memset(self, 0, sizeof(UsartLogger));
	interface_log_init(&self->log_iface);

	/* Log interface implementation. */
	self->log_iface.vmt.context = (void *)self;
	self->log_iface.vmt.message = message;

	printf("usart logger initialized\n");
	interface_log_printf(usart_logger_iface(self), "usart_logger", LOG_LEVEL_INFO, "initialized on port %p", port);
}


ILog *usart_logger_iface(UsartLogger *self) {
	return &self->log_iface;
}
