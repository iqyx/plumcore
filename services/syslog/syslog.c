/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Syslog-style system-wide logger
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include <main.h>
#include <interfaces/log.h>
#include <interfaces/stream.h>

#include "syslog.h"

#define MODULE_NAME "syslog"

const char *str_log_levels[] = {
	"",
	"TRACE",
	"DEBUG",
	"INFO",
	"WARN",
	"ERROR",
	"CRIT",
	"ASSERT",
};

/**********************************************************************************************************************
 * Log interface implementation
 **********************************************************************************************************************/

static log_ret_t syslog_log(Log *log, const char *source, enum log_level level, const char *fmt, va_list args) {
	Syslog *self = log->parent;

	if (self->logout == NULL) {
		return LOG_RET_FAILED;
	}

	self->logout->vmt->write(self->logout, str_log_levels[level], strlen(str_log_levels[level]));
	self->logout->vmt->write(self->logout, " ", 1);
	self->logout->vmt->write(self->logout, source, strlen(source));
	self->logout->vmt->write(self->logout, ": ", 2);
	self->logout->vmt->write(self->logout, fmt, strlen(fmt));
	self->logout->vmt->write(self->logout, "\r\n", 2);

	return LOG_RET_OK;
}


static const struct log_vmt syslog_log_vmt = {
	.log = syslog_log,
};


/**********************************************************************************************************************
 * Service implementation
 **********************************************************************************************************************/

syslog_ret_t syslog_init(Syslog *self) {
	memset(self, 0, sizeof(Syslog));

	self->log.parent = self;
	self->log.vmt = &syslog_log_vmt;

	return SYSLOG_RET_OK;
}


syslog_ret_t syslog_free(Syslog *self) {
	(void)self;
	/* Nothing to free. */
	return SYSLOG_RET_OK;
}


syslog_ret_t syslog_set_stream(Syslog *self, Stream *stream) {
	self->logout = stream;

	return SYSLOG_RET_OK;
}
