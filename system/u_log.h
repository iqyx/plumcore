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

#ifndef _U_LOG_H_
#define _U_LOG_H_

#include "system_log.h"
#include "interface_stream.h"
#include "interface_rtc.h"

/* Kept for legacy reasons. */
// #define ESC_CURSOR_UP "\x1b[A"
// #define ESC_CURSOR_DOWN "\x1b[B"
// #define ESC_CURSOR_RIGHT "\x1b[C"
// #define ESC_CURSOR_LEFT "\x1b[D"
// #define ESC_DEFAULT "\x1b[0m"
// #define ESC_BOLD "\x1b[1m"
// #define ESC_CURSOR_SAVE "\x1b[s"
// #define ESC_CURSOR_RESTORE "\x1b[u"
// #define ESC_ERASE_LINE_END "\x1b[K"
// #define ESC_COLOR_FG_BLACK "\x1b[30m"
// #define ESC_COLOR_FG_RED "\x1b[31m"
// #define ESC_COLOR_FG_GREEN "\x1b[32m"
// #define ESC_COLOR_FG_YELLOW "\x1b[33m"
// #define ESC_COLOR_FG_BLUE "\x1b[34m"
// #define ESC_COLOR_FG_MAGENTA "\x1b[35m"
// #define ESC_COLOR_FG_CYAN "\x1b[36m"
// #define ESC_COLOR_FG_WHITE "\x1b[37m"


#define u_log log_cbuffer_printf
#define U_LOG_MODULE            "\x1b[33m" "%s: " "\x1b[0m"
#define U_LOG_MODULE_PREFIX(x)  U_LOG_MODULE x, MODULE_NAME
extern struct log_cbuffer *system_log;


int32_t u_log_init(void);
#define U_LOG_INIT_OK 0

int32_t u_log_set_stream(struct interface_stream *stream);
#define U_LOG_SET_STREAM_OK 0
#define U_LOG_SET_STREAM_FAILED -1

int32_t u_log_set_rtc(struct interface_rtc *rtc);
#define U_LOG_SET_RTC_OK 0
#define U_LOG_SET_RTC_FAILED -1


#endif
