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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "u_assert.h"
#include "hal_interface.h"
#include "interface_rtc.h"


int32_t interface_rtc_init(struct interface_rtc *rtc) {
	if (u_assert(rtc != NULL)) {
		return INTERFACE_RTC_INIT_FAILED;
	}

	memset(rtc, 0, sizeof(struct interface_rtc));
	hal_interface_init(&(rtc->descriptor), HAL_INTERFACE_TYPE_RTC);

	return INTERFACE_RTC_INIT_OK;
}


int32_t interface_rtc_get_time(struct interface_rtc *rtc, uint8_t *hours, uint8_t *minutes, uint8_t *seconds) {
	if (u_assert(rtc != NULL)) {
		return INTERFACE_RTC_GET_TIME_FAILED;
	}
	if (rtc->vmt.get_time != NULL) {
		return rtc->vmt.get_time(rtc->vmt.context, hours, minutes, seconds);
	}
	return INTERFACE_RTC_GET_TIME_FAILED;
}


int32_t interface_rtc_get_date(struct interface_rtc *rtc, int32_t *year, uint8_t *month, uint8_t *day) {
	if (u_assert(rtc != NULL)) {
		return INTERFACE_RTC_GET_DATE_FAILED;
	}
	if (rtc->vmt.get_date != NULL) {
		return rtc->vmt.get_date(rtc->vmt.context, year, month, day);
	}
	return INTERFACE_RTC_GET_DATE_FAILED;
}


int32_t interface_rtc_get_datetime(struct interface_rtc *rtc, struct interface_rtc_datetime_t *datetime) {
	if (u_assert(rtc != NULL && datetime != NULL)) {
		return INTERFACE_RTC_GET_DATETIME_FAILED;
	}

	if ((interface_rtc_get_time(rtc, &(datetime->hours), &(datetime->minutes), &(datetime->seconds)) != INTERFACE_RTC_GET_TIME_OK) ||
	    (interface_rtc_get_date(rtc, &(datetime->year), &(datetime->month), &(datetime->day)) != INTERFACE_RTC_GET_DATE_OK)) {
		return INTERFACE_RTC_GET_DATETIME_FAILED;
	}

	return INTERFACE_RTC_GET_DATETIME_OK;
}


int32_t interface_rtc_datetime_to_int(struct interface_rtc *rtc, uint32_t *n, const struct interface_rtc_datetime_t *datetime) {
	if (u_assert(rtc != NULL && n != NULL && datetime != NULL)) {
		return INTERFACE_RTC_DATETIME_TO_INT_FAILED;
	}
	(void)rtc;

	/* We cannot save years before 2000, return 0 */
	if (datetime->year < 2000) {
		*n = 0;
		return INTERFACE_RTC_DATETIME_TO_INT_OK;
	}

	if (datetime->month < 1 || datetime->month > 12 ||
	    datetime->day < 1 || datetime->day > 31) {
		return INTERFACE_RTC_DATETIME_TO_INT_FAILED;
	}

	uint32_t res = 0;
	/* We are assuming that every year have all months 31 days long. */
	res += (datetime->year - 2000) * 86400 * 31 * 12;
	res += (datetime->month - 1) * 86400 * 31;
	res += (datetime->day - 1) * 86400;
	res += datetime->hours * 3600;
	res += datetime->minutes * 60;
	res += datetime->seconds;

	*n = res;

	return INTERFACE_RTC_DATETIME_TO_INT_OK;
}


int32_t interface_rtc_int_to_datetime(struct interface_rtc *rtc, struct interface_rtc_datetime_t *datetime, uint32_t n) {
	if (u_assert(rtc != NULL && datetime != NULL)) {
		return INTERFACE_RTC_INT_TO_DATETIME_FAILED;
	}
	(void)rtc;

	datetime->seconds = n % 60;
	n = n / 60;
	datetime->minutes = n % 60;
	n = n / 60;
	datetime->hours = n % 24;
	n = n / 24;

	/* Now n contains the number of remaining days. */
	datetime->day = n % 31 + 1;
	n = n / 31;
	datetime->month = n % 12 + 1;
	n = n % 12;
	datetime->year = n + 2000;

	return INTERFACE_RTC_INT_TO_DATETIME_OK;
}


int32_t interface_rtc_datetime_to_str(struct interface_rtc *rtc, char *s, size_t len, struct interface_rtc_datetime_t *datetime) {
	if (u_assert(rtc != NULL && datetime != NULL && s != NULL && len > 0)) {
		return INTERFACE_RTC_DATETIME_TO_STR_FAILED;
	}

	snprintf(s, len, "%04d-%02u-%02uT%02u:%02u:%02uZ",
		(int)datetime->year,
		datetime->month,
		datetime->day,
		datetime->hours,
		datetime->minutes,
		datetime->seconds
	);

	return INTERFACE_RTC_DATETIME_TO_STR_OK;
}
