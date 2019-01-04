/**
 * uMeshFw libopencm3 RTC clock module
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

#include <libopencm3/stm32/rtc.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/pwr.h>
#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"
#include "interface_rtc.h"
#include "module_rtc_locm3.h"


static int32_t module_rtc_locm3_get_time(void *context, uint8_t *hours, uint8_t *minutes, uint8_t *seconds) {
	(void)context;

	uint32_t tr = RTC_TR;
	*hours = ((tr >> RTC_TR_HT_SHIFT) & RTC_TR_HT_MASK) * 10 + ((tr >> RTC_TR_HU_SHIFT) & RTC_TR_HU_MASK);
	*minutes = ((tr >> RTC_TR_MNT_SHIFT) & RTC_TR_MNT_MASK) * 10 + ((tr >> RTC_TR_MNU_SHIFT) & RTC_TR_MNU_MASK);
	*seconds = ((tr >> RTC_TR_ST_SHIFT) & RTC_TR_ST_MASK) * 10 + ((tr >> RTC_TR_SU_SHIFT) & RTC_TR_SU_MASK);

	return INTERFACE_RTC_GET_TIME_OK;
}


static int32_t module_rtc_locm3_get_date(void *context, int32_t *year, uint8_t *month, uint8_t *day) {
	(void)context;

	uint32_t tr = RTC_DR;
	*year = 2000 + ((tr >> RTC_DR_YT_SHIFT) & RTC_DR_YT_MASK) * 10 + ((tr >> RTC_DR_YU_SHIFT) & RTC_DR_YU_MASK);
	*month = ((tr >> RTC_DR_MT_SHIFT) & 1) * 10 + ((tr >> RTC_DR_MU_SHIFT) & RTC_DR_MU_MASK);
	*day = ((tr >> RTC_DR_DT_SHIFT) & RTC_DR_DT_MASK) * 10 + ((tr >> RTC_DR_DU_SHIFT) & RTC_DR_DU_MASK);

	return INTERFACE_RTC_GET_DATE_OK;
}


int32_t module_rtc_locm3_init(struct module_rtc_locm3 *rtc, const char *name) {
	if (u_assert(rtc != NULL)) {
		return MODULE_RTC_LOCM3_INIT_FAILED;
	}

	memset(rtc, 0, sizeof(struct module_rtc_locm3));

	/* Initialize RTC interface. */
	interface_rtc_init(&(rtc->iface));
	rtc->iface.vmt.context = (void *)rtc;
	rtc->iface.vmt.get_time = module_rtc_locm3_get_time;
	rtc->iface.vmt.get_date = module_rtc_locm3_get_date;

	rcc_periph_clock_enable(RCC_PWR);

	#if defined(STM32L4)
		PWR_CR1 |= PWR_CR1_DBP;
	#else
		rcc_periph_clock_enable(RCC_RTC);
		PWR_CR |= PWR_CR_DBP;
	#endif

	/* Enable LSI oscillator and wait a bit. */
	RCC_CSR |= RCC_CSR_LSION;
	vTaskDelay(50);
	if (!(RCC_CSR & RCC_CSR_LSIRDY)) {
		u_log(system_log, LOG_TYPE_ERROR, "LSI not ready");
		return MODULE_RTC_LOCM3_INIT_FAILED;
	}

	/* Old init code removed. */

	u_log(system_log, LOG_TYPE_INFO, "module RTC initialized");

	struct interface_rtc_datetime_t datetime;
	char s[30];
	interface_rtc_get_datetime(&(rtc->iface), &datetime);
	interface_rtc_datetime_to_str(&(rtc->iface), s, sizeof(s), &datetime);

	u_log(system_log, LOG_TYPE_INFO, "current date/time is %s", s);


	return MODULE_RTC_LOCM3_INIT_OK;
}
