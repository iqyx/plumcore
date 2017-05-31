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
#include "lineedit.h"

#define u_log log_cbuffer_printf
#define U_LOG_MODULE            ESC_COLOR_FG_YELLOW "%s: " ESC_DEFAULT
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
