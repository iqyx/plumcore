/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Logging interface
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdarg.h>

enum log_level {
	LOG_LEVEL_TRACE = 1,
	LOG_LEVEL_DEBUG = 2,
	LOG_LEVEL_INFO = 3,
	LOG_LEVEL_WARNING = 4,
	LOG_LEVEL_ERROR = 5,
	LOG_LEVEL_CRITICAL = 6,
	LOG_LEVEL_ASSERT = 7,

	/* Legacy types */
	LOG_TYPE_TRACE = 1,
	LOG_TYPE_DEBUG = 2,
	LOG_TYPE_INFO = 3,
	LOG_TYPE_WARN = 4,
	LOG_TYPE_ERROR = 5,
	LOG_TYPE_CRIT = 6,
	LOG_TYPE_ASSERT = 7,
};

typedef enum {
	LOG_RET_OK = 0,
	LOG_RET_FAILED,
} log_ret_t;

typedef struct log Log;
struct log_vmt {
	log_ret_t (*log)(Log *self, const char *source, enum log_level level, const char *fmt, va_list args);
};

typedef struct log {
	const struct log_vmt *vmt;
	void *parent;
} Log;


