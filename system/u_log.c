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
#include "port.h"
#include <interfaces/stream.h>

Syslog syslog_service;
Log *system_log = &syslog_service.log;

void u_log_init(void) {
	syslog_init(&syslog_service);
}


int u_assert_func(const char *expr, const char *fname, int line) {
	(void)expr;
	(void)fname;
	(void)line;
	return 1;
}


void u_log_set_stream(Stream *stream) {
	syslog_set_stream(&syslog_service, stream);
}


void u_log(Log *log, enum log_level level, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	log->vmt->log(log, "source", level, fmt, args);
	va_end(args);
}
