/**
 * uMeshFw HAL real-time clock interface
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

#ifndef _INTERFACE_RTC_H_
#define _INTERFACE_RTC_H_

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "hal_interface.h"

/**
 * This interface is far from complete and it needs some rethinking.
 */

struct interface_rtc_datetime_t {
	/* TODO: DST, 12/24h format */
	uint8_t hours;
	uint8_t minutes;
	uint8_t seconds;
	uint8_t month;
	uint8_t day;
	int32_t year;
};

struct interface_rtc_vmt {
	int32_t (*get_time)(void *context, uint8_t *hours, uint8_t *minutes, uint8_t *seconds);
	int32_t (*get_date)(void *context, int32_t *year, uint8_t *month, uint8_t *day);
	void *context;
};

struct interface_rtc {
	struct hal_interface_descriptor descriptor;
	struct interface_rtc_vmt vmt;
};


int32_t interface_rtc_init(struct interface_rtc *rtc);
#define INTERFACE_RTC_INIT_OK 0
#define INTERFACE_RTC_INIT_FAILED -1

int32_t interface_rtc_get_time(struct interface_rtc *rtc, uint8_t *hours, uint8_t *minutes, uint8_t *seconds);
#define INTERFACE_RTC_GET_TIME_OK 0
#define INTERFACE_RTC_GET_TIME_FAILED -1

int32_t interface_rtc_get_date(struct interface_rtc *rtc, int32_t *year, uint8_t *month, uint8_t *day);
#define INTERFACE_RTC_GET_DATE_OK 0
#define INTERFACE_RTC_GET_DATE_FAILED -1

int32_t interface_rtc_get_datetime(struct interface_rtc *rtc, struct interface_rtc_datetime_t *datetime);
#define INTERFACE_RTC_GET_DATETIME_OK 0
#define INTERFACE_RTC_GET_DATETIME_FAILED -1

int32_t interface_rtc_datetime_to_int(struct interface_rtc *rtc, uint32_t *n, const struct interface_rtc_datetime_t *datetime);
#define INTERFACE_RTC_DATETIME_TO_INT_OK 0
#define INTERFACE_RTC_DATETIME_TO_INT_FAILED -1

int32_t interface_rtc_int_to_datetime(struct interface_rtc *rtc, struct interface_rtc_datetime_t *datetime, uint32_t n);
#define INTERFACE_RTC_INT_TO_DATETIME_OK 0
#define INTERFACE_RTC_INT_TO_DATETIME_FAILED -1

int32_t interface_rtc_datetime_to_str(struct interface_rtc *rtc, char *s, size_t len, struct interface_rtc_datetime_t *datetime);
#define INTERFACE_RTC_DATETIME_TO_STR_OK 0
#define INTERFACE_RTC_DATETIME_TO_STR_FAILED -1


#endif

