/**
 * uBLoad logging services
 *
 * Copyright (C) 2015, Marek Koza, qyx@krtko.org
 *
 * This file is part of uMesh node firmware (http://qyx.krtko.org/projects/umesh)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "u_log.h"
#include "u_assert.h"
#include "system_log.h"
#include "port.h"
#include "interface_stream.h"
#include "lineedit.h"
#include "interface_rtc.h"


#if PORT_CLOG == true
	struct log_cbuffer *system_log;
#endif


int32_t u_log_init(void) {

	uint8_t *clog_pos = (void *)PORT_CLOG_BASE;
	system_log = (struct log_cbuffer *)clog_pos;

	/* TODO: move to more proper place. */
	system_log->print_handler = NULL;
	system_log->print_handler_ctx = NULL;
	system_log->time_handler = NULL;
	system_log->time_handler_ctx = NULL;

	#if PORT_CLOG_REUSE == false
		uint8_t *log_data = (uint8_t *)(clog_pos + sizeof(struct log_cbuffer));

		log_cbuffer_init(system_log, log_data, PORT_CLOG_SIZE - sizeof(struct log_cbuffer));
	#endif

	return U_LOG_INIT_OK;
}


/**
 * u_assert macro from u_assert.h is using this function to output assertion
 * failure messages. If system circular log is enabled, redirect them there.
 *
 * TODO: should be moved to u_assert.c
 */
#if PORT_CLOG == true

	int u_assert_func(const char *expr, const char *fname, int line) {

		if (system_log != NULL) {
			u_log(system_log, LOG_TYPE_ASSERT,
				"Assertion '%s' failed at %s:%d", expr, fname, line);
		}
		return 1;
	}
#else
	int u_assert_func(const char *expr, const char *fname, int line) {
		(void)expr;
		(void)fname;
		(void)line;
		return 1;
	}
#endif


/* TODO: include this function in the stream interface directly. */
static void u_log_stream_print(struct interface_stream *stream, const char *s) {
	/* Do not use assert here. */
	if (stream == NULL || s == NULL) {
		return;
	}
	/* TODO: replace 200 with a config value. */
	interface_stream_write(stream, (const uint8_t *)s, strnlen(s, 200));
}


static void u_log_print_handler(struct log_cbuffer *buf, uint32_t pos, void *ctx) {
	if (ctx == NULL || buf == NULL) {
		return;
	}

	uint8_t header;
	char *msg;
	uint32_t time;

	log_cbuffer_get_header(buf, pos, &header);
	log_cbuffer_get_message(buf, pos, &msg);
	log_cbuffer_get_time(buf, pos, &time);

	struct interface_stream *stream = (struct interface_stream *)ctx;
	struct interface_rtc *rtc = (struct interface_rtc *)buf->time_handler_ctx;

	char s[30] = {0};

	if (rtc != NULL) {
		struct interface_rtc_datetime_t datetime;
		interface_rtc_int_to_datetime(rtc, &datetime, time);
		interface_rtc_datetime_to_str(rtc, s, sizeof(s), &datetime);
	}

	u_log_stream_print(stream, "\r" ESC_ERASE_LINE_END);
	u_log_stream_print(stream, "[");
	u_log_stream_print(stream, s);
	u_log_stream_print(stream, "] ");

	u_log_stream_print(stream, ESC_BOLD);
	switch (header & 0x0f) {
		case LOG_TYPE_NULL:
			u_log_stream_print(stream, "\r\n");
			return;

		case LOG_TYPE_INFO:
			u_log_stream_print(stream, ESC_COLOR_FG_BLUE "INFO: ");
			break;

		case LOG_TYPE_DEBUG:
			u_log_stream_print(stream, ESC_COLOR_FG_GREEN "DEBUG: ");
			break;

		case LOG_TYPE_WARN:
			u_log_stream_print(stream, ESC_COLOR_FG_YELLOW "WARNING: ");
			break;

		case LOG_TYPE_ERROR:
			u_log_stream_print(stream, ESC_COLOR_FG_MAGENTA "ERROR: ");
			break;

		case LOG_TYPE_CRIT:
			u_log_stream_print(stream, ESC_COLOR_FG_RED "CRITICAL: ");
			break;

		case LOG_TYPE_ASSERT:
			u_log_stream_print(stream, ESC_COLOR_FG_RED "ASSERT: ");
			break;

		default:
			u_log_stream_print(stream, "?: ");
			break;
	}
	u_log_stream_print(stream, ESC_DEFAULT);
	u_log_stream_print(stream, msg);
	u_log_stream_print(stream, "\r\n");
}


static void u_log_time_handler(struct log_cbuffer *buf, uint32_t *time, void *ctx) {
	if (ctx == NULL || buf == NULL) {
		return;
	}

	struct interface_rtc_datetime_t datetime;
	interface_rtc_get_datetime((struct interface_rtc *)ctx, &datetime);
	interface_rtc_datetime_to_int((struct interface_rtc *)ctx, time, &datetime);
}


int32_t u_log_set_stream(struct interface_stream *stream) {
	if (u_assert(stream != NULL)) {
		return U_LOG_SET_STREAM_FAILED;
	}

	#if PORT_CLOG == true
		log_cbuffer_set_print_handler(system_log, u_log_print_handler, (void *)stream);
	#endif

	return U_LOG_SET_STREAM_OK;
}


int32_t u_log_set_rtc(struct interface_rtc *rtc) {
	if (u_assert(rtc != NULL)) {
		return U_LOG_SET_RTC_FAILED;
	}

	#if PORT_CLOG == true
		log_cbuffer_set_time_handler(system_log, u_log_time_handler, (void *)rtc);
	#endif

	return U_LOG_SET_RTC_OK;
}
