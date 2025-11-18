/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Syslog-style system-wide logger
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <interfaces/log.h>
#include <interfaces/stream.h>

typedef enum {
	SYSLOG_RET_OK,
	SYSLOG_RET_FAILED,
} syslog_ret_t;


typedef struct {
	Log log;
	Stream *logout;
} Syslog;

syslog_ret_t syslog_init(Syslog *self);
syslog_ret_t syslog_free(Syslog *self);
syslog_ret_t syslog_set_stream(Syslog *self, Stream *stream);
