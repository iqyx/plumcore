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

#pragma once

#define INTERFACE_LOG_MAX_MESSAGE_LEN 128


/* Meh. Remove. */
struct interface_log_time {
	uint8_t day;
	uint8_t month;
	uint16_t year;
	uint8_t hours;
	uint8_t minutes;
	uint8_t seconds;
};

enum interface_log_level {
	LOG_LEVEL_TRACE,
	LOG_LEVEL_DEBUG,
	LOG_LEVEL_INFO,
	LOG_LEVEL_WARNING,
	LOG_LEVEL_ERROR,
	LOG_LEVEL_CRITICAL,
	LOG_LEVEL_ASSERT
};

typedef enum {
	INTERFACE_LOG_RET_OK = 0,
	INTERFACE_LOG_RET_FAILED,

} interface_log_ret_t;

struct interface_log_vmt {
	interface_log_ret_t (*message)(void *context, struct interface_log_time time, const char *module, enum interface_log_level level, const char *message);
	void *context;

};

typedef struct interface_log {
	struct interface_log_vmt vmt;
} ILog;


interface_log_ret_t interface_log_init(ILog *self);
interface_log_ret_t interface_log_free(ILog *self);
interface_log_ret_t interface_log_message(ILog *self, struct interface_log_time time, const char *module, enum interface_log_level level, const char *message);
interface_log_ret_t interface_log_printf(ILog *self, const char *module, enum interface_log_level level, const char *fmt, ...);

