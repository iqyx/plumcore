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

#pragma once

#include <stdarg.h>
#include <interfaces/stream.h>
#include <interfaces/log.h>
#include <services/syslog/syslog.h>


#define U_LOG_MODULE            "\x1b[33m" "%s: " "\x1b[0m"
#define U_LOG_MODULE_PREFIX(x)  U_LOG_MODULE x, MODULE_NAME
extern Syslog syslog_service;
extern Log *system_log;

void u_log_init(void);
void u_log_set_stream(Stream *stream);
void u_log(Log *log, enum log_level level, const char *fmt, ...);
