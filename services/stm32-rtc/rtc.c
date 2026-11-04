/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 internal RTC clock driver service
 *
 * Copyright (c) 2018-2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include <main.h>
#include "u_assert.h"
#include "u_log.h"

#if defined(STM32G4)
	#include <stm32g4xx.h>
#elif defined(STM32U5)
	#include <stm32u5xx.h>
#else
	#error "stm32-rtc service is not compatible with this MCU family"
#endif

#include "rtc.h"

#define MODULE_NAME "stm32-rtc"

/* The asynchronous prescaler is kept at its maximum (/128) to save power, the synchronous one divides the
 * remaining 256 Hz (LSE) or 250 Hz (LSI) down to 1 Hz. The sub-second resolution is one ck_apre period. */
#define STM32_RTC_PREDIV_A 127
#define STM32_RTC_LSE_FREQ 32768
#define STM32_RTC_LSI_FREQ 32000
#define STM32_RTC_LSE_TIMEOUT_MS 2500
#define STM32_RTC_TIMEOUT_MS 100

#define RTC_BDCR_RTCSEL_LSE 1
#define RTC_BDCR_RTCSEL_LSI 2


/* Wait until the masked register bits are all set (or all cleared). */
static bool stm32_rtc_wait(volatile uint32_t *reg, uint32_t mask, bool set, uint32_t timeout_ms) {
	while (((*reg & mask) == mask) != set) {
		if (timeout_ms == 0) {
			return false;
		}
		vTaskDelay(1);
		timeout_ms--;
	}
	return true;
}


static uint32_t bcd_to_bin(uint32_t v) {
	return (v >> 4) * 10 + (v & 0xf);
}


static uint32_t bin_to_bcd(uint32_t v) {
	return ((v / 10) << 4) | (v % 10);
}


/* Convert a broken-down UTC time to seconds since the epoch. Unlike mktime() it never applies a timezone. Uses the
 * days-from-civil algorithm by Howard Hinnant, valid for years >= 0. */
static time_t stm32_rtc_mkutc(const struct tm *tm) {
	int32_t y = tm->tm_year + 1900;
	int32_t m = tm->tm_mon + 1;
	if (m <= 2) {
		y--;
	}
	int32_t era = y / 400;
	int32_t yoe = y - era * 400;
	int32_t doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + tm->tm_mday - 1;
	int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	int64_t days = (int64_t)era * 146097 + doe - 719468;

	return days * 86400 + tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec;
}


static uint32_t stm32_rtc_prediv_s(void) {
	return RTC->PRER & RTC_PRER_PREDIV_S;
}


static void stm32_rtc_unlock(void) {
	RTC->WPR = 0xca;
	RTC->WPR = 0x53;
}


static void stm32_rtc_lock(void) {
	RTC->WPR = 0xff;
}


/* Enter the initialization mode. The calendar is stopped and can be programmed. */
static bool stm32_rtc_enter_init(void) {
	RTC->ICSR |= RTC_ICSR_INIT;
	return stm32_rtc_wait(&RTC->ICSR, RTC_ICSR_INITF, true, STM32_RTC_TIMEOUT_MS);
}


/* Leave the initialization mode and wait until the calendar shadow registers are synchronised again. */
static bool stm32_rtc_exit_init(void) {
	RTC->ICSR &= ~(RTC_ICSR_INIT | RTC_ICSR_RSF);
	return stm32_rtc_wait(&RTC->ICSR, RTC_ICSR_RSF, true, STM32_RTC_TIMEOUT_MS);
}


/**********************************************************************************************************************
 * Clock interface implementation
 **********************************************************************************************************************/

clock_ret_t stm32_rtc_get(Stm32Rtc *self, struct timespec *time) {
	if (u_assert(self != NULL) ||
	    u_assert(time != NULL)) {
		return CLOCK_RET_FAILED;
	}

	/* Reading SSR locks TR and DR in the shadow registers until DR is read, the three are consistent. */
	taskENTER_CRITICAL();
	uint32_t ssr = RTC->SSR;
	uint32_t tr = RTC->TR;
	uint32_t dr = RTC->DR;
	taskEXIT_CRITICAL();

	struct tm ts = {
		.tm_sec = bcd_to_bin((tr >> RTC_TR_SU_Pos) & 0x7f),
		.tm_min = bcd_to_bin((tr >> RTC_TR_MNU_Pos) & 0x7f),
		.tm_hour = bcd_to_bin((tr >> RTC_TR_HU_Pos) & 0x3f),
		.tm_mday = bcd_to_bin((dr >> RTC_DR_DU_Pos) & 0x3f),
		.tm_mon = bcd_to_bin((dr >> RTC_DR_MU_Pos) & 0x1f) - 1,
		.tm_year = bcd_to_bin((dr >> RTC_DR_YU_Pos) & 0xff) + 100,
	};
	time->tv_sec = stm32_rtc_mkutc(&ts);

	/* The sub-second register counts down from PREDIV_S. */
	uint32_t prediv_s = stm32_rtc_prediv_s();
	time->tv_nsec = (uint64_t)(prediv_s - ssr) * 1000000000ULL / (prediv_s + 1);

	return CLOCK_RET_OK;
}


clock_ret_t stm32_rtc_set(Stm32Rtc *self, const struct timespec *time) {
	if (u_assert(self != NULL) ||
	    u_assert(time != NULL)) {
		return CLOCK_RET_FAILED;
	}

	struct tm ts = {0};
	gmtime_r(&time->tv_sec, &ts);

	/* The calendar holds years 2000 - 2099 only. */
	if (ts.tm_year < 100 || ts.tm_year > 199) {
		return CLOCK_RET_BAD_ARG;
	}

	uint32_t tr =
		(bin_to_bcd(ts.tm_hour) << RTC_TR_HU_Pos) |
		(bin_to_bcd(ts.tm_min) << RTC_TR_MNU_Pos) |
		(bin_to_bcd(ts.tm_sec) << RTC_TR_SU_Pos);

	/* Week day is 1 (Monday) to 7 (Sunday), 0 is forbidden. */
	uint32_t dr =
		(bin_to_bcd(ts.tm_year - 100) << RTC_DR_YU_Pos) |
		((ts.tm_wday == 0 ? 7 : ts.tm_wday) << RTC_DR_WDU_Pos) |
		(bin_to_bcd(ts.tm_mon + 1) << RTC_DR_MU_Pos) |
		(bin_to_bcd(ts.tm_mday) << RTC_DR_DU_Pos);

	stm32_rtc_unlock();
	if (!stm32_rtc_enter_init()) {
		stm32_rtc_lock();
		return CLOCK_RET_FAILED;
	}
	RTC->TR = tr;
	RTC->DR = dr;
	bool synced = stm32_rtc_exit_init();
	stm32_rtc_lock();
	if (!synced) {
		return CLOCK_RET_FAILED;
	}

	/* Leaving the initialization mode restarts the second at zero. The sub-second part cannot be written
	 * directly, advance the clock by it instead. */
	if (time->tv_nsec > 0) {
		return stm32_rtc_shift(self, time->tv_nsec);
	}

	return CLOCK_RET_OK;
}


clock_ret_t stm32_rtc_shift(Stm32Rtc *self, int32_t time_ns) {
	if (u_assert(self != NULL)) {
		return CLOCK_RET_FAILED;
	}

	if (RTC->ICSR & RTC_ICSR_SHPF) {
		/* A shift operation is already pending. */
		return CLOCK_RET_FAILED;
	}

	uint32_t prediv_s = stm32_rtc_prediv_s();
	uint32_t ticks = (uint64_t)abs(time_ns) * (prediv_s + 1) / 1000000000ULL;
	if (ticks > prediv_s) {
		ticks = prediv_s;
	}
	if (ticks == 0) {
		return CLOCK_RET_OK;
	}

	/* SUBFS delays the clock by a fraction of a second. Advancing is done by adding a whole second and
	 * delaying by the rest. */
	uint32_t shift = ticks;
	if (time_ns > 0) {
		shift = RTC_SHIFTR_ADD1S | (prediv_s + 1 - ticks);
	}

	stm32_rtc_unlock();
	RTC->SHIFTR = shift;
	stm32_rtc_lock();

	if (!stm32_rtc_wait(&RTC->ICSR, RTC_ICSR_SHPF, false, STM32_RTC_TIMEOUT_MS)) {
		return CLOCK_RET_FAILED;
	}

	return CLOCK_RET_OK;
}


static clock_ret_t clock_get(Clock *self, struct timespec *time) {
	return stm32_rtc_get(self->parent, time);
}


static clock_ret_t clock_set(Clock *self, const struct timespec *time) {
	return stm32_rtc_set(self->parent, time);
}


static clock_ret_t clock_shift(Clock *self, int32_t time_ns) {
	return stm32_rtc_shift(self->parent, time_ns);
}


static const struct clock_vmt stm32_rtc_clock_vmt = {
	.get = clock_get,
	.set = clock_set,
	.shift = clock_shift,
};


/**********************************************************************************************************************
 * Configuration tree
 **********************************************************************************************************************/

#if defined(CONFIG_SERVICE_STM32_RTC_CONF)

/* The date and time leaves are numeric nodes backed by the date fields. Their read and write handlers are
 * overridden: a read refreshes the fields from the RTC and returns the one of the node, a write refreshes them,
 * replaces the one of the node (constraints are checked by configlib) and sets the RTC. The leaf parent is the
 * "rtc" subtree node holding the instance. The vmt is copied from the one configlib assigned to the nodes so
 * walk/stat/get_* keep working. */
static const struct conf_vmt *stm32_rtc_configlib_vmt;
static struct conf_vmt stm32_rtc_date_vmt;


static clock_ret_t stm32_rtc_date_load(Stm32Rtc *self) {
	struct timespec ts = {0};
	if (stm32_rtc_get(self, &ts) != CLOCK_RET_OK) {
		return CLOCK_RET_FAILED;
	}
	struct tm tm = {0};
	gmtime_r(&ts.tv_sec, &tm);

	self->date.year = tm.tm_year + 1900;
	self->date.month = tm.tm_mon + 1;
	self->date.day = tm.tm_mday;
	self->date.hour = tm.tm_hour;
	self->date.minute = tm.tm_min;
	self->date.second = tm.tm_sec;

	return CLOCK_RET_OK;
}


/* Set the RTC from the date fields. The day is limited to the length of the month, eg. when changing the month of
 * a 31st. */
static clock_ret_t stm32_rtc_date_store(Stm32Rtc *self) {
	static const uint8_t month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	uint8_t days = month_days[self->date.month - 1];
	if (self->date.month == 2 && self->date.year % 4 == 0) {
		days++;
	}
	if (self->date.day > days) {
		self->date.day = days;
	}

	struct tm tm = {
		.tm_year = self->date.year - 1900,
		.tm_mon = self->date.month - 1,
		.tm_mday = self->date.day,
		.tm_hour = self->date.hour,
		.tm_min = self->date.minute,
		.tm_sec = self->date.second,
	};
	struct timespec ts = {.tv_sec = stm32_rtc_mkutc(&tm)};

	return stm32_rtc_set(self, &ts);
}


static conf_ret_t stm32_rtc_date_read(Conf *self, union conf_val *val) {
	Stm32Rtc *rtc = ((ConfiglibValue *)self->parent)->parent->var;

	if (stm32_rtc_date_load(rtc) != CLOCK_RET_OK) {
		return CONF_RET_FAILED;
	}

	return stm32_rtc_configlib_vmt->read(self, val);
}


static conf_ret_t stm32_rtc_date_write(Conf *self, const union conf_val val) {
	Stm32Rtc *rtc = ((ConfiglibValue *)self->parent)->parent->var;

	if (stm32_rtc_date_load(rtc) != CLOCK_RET_OK ||
	    stm32_rtc_configlib_vmt->write(self, val) != CONF_RET_OK ||
	    stm32_rtc_date_store(rtc) != CLOCK_RET_OK) {
		return CONF_RET_FAILED;
	}

	return CONF_RET_OK;
}


/* Append a date or time leaf after @p prev (or as the first child of the "rtc" subtree if @p prev is NULL). */
static void stm32_rtc_add_leaf(Stm32Rtc *self, ConfiglibValue *node, ConfiglibValue *prev, const char *name,
                               void *var, enum conf_type type, union conf_constraint c, const char *brief) {
	if (prev == NULL) {
		configlib_init_map_append(node, name, var, type, &self->conf_tree.rtc, CONF_DIR_CHILD);
	} else {
		configlib_init_map_append(node, name, var, type, prev, CONF_DIR_NEXT);
	}
	/* The time changes on its own, flag it as a status value so it is not saved and restored with the
	 * configuration. */
	node->flags = CONF_READ | CONF_WRITE | CONF_STATUS;
	configlib_set_constraint(node, c);
	configlib_set_description(node, brief, "UTC, writing it sets the clock.");

	stm32_rtc_configlib_vmt = node->conf.vmt;
	node->conf.vmt = &stm32_rtc_date_vmt;
}


static void stm32_rtc_conf_init(Stm32Rtc *self) {
	configlib_init_map(&self->conf_tree.rtc, "rtc", self, CONF_SUBTREE);

	stm32_rtc_add_leaf(self, &self->conf_tree.year, NULL, "year", &self->date.year, CONF_U16,
		(union conf_constraint){.numeric = {.min.u16 = 2000, .max.u16 = 2099}}, "Year");
	stm32_rtc_add_leaf(self, &self->conf_tree.month, &self->conf_tree.year, "month", &self->date.month, CONF_U8,
		(union conf_constraint){.numeric = {.min.u8 = 1, .max.u8 = 12}}, "Month");
	stm32_rtc_add_leaf(self, &self->conf_tree.day, &self->conf_tree.month, "day", &self->date.day, CONF_U8,
		(union conf_constraint){.numeric = {.min.u8 = 1, .max.u8 = 31}}, "Day of month");
	stm32_rtc_add_leaf(self, &self->conf_tree.hour, &self->conf_tree.day, "hour", &self->date.hour, CONF_U8,
		(union conf_constraint){.numeric = {.min.u8 = 0, .max.u8 = 23}}, "Hours");
	stm32_rtc_add_leaf(self, &self->conf_tree.minute, &self->conf_tree.hour, "minute", &self->date.minute, CONF_U8,
		(union conf_constraint){.numeric = {.min.u8 = 0, .max.u8 = 59}}, "Minutes");
	stm32_rtc_add_leaf(self, &self->conf_tree.second, &self->conf_tree.minute, "second", &self->date.second, CONF_U8,
		(union conf_constraint){.numeric = {.min.u8 = 0, .max.u8 = 59}}, "Seconds");

	stm32_rtc_date_vmt = *stm32_rtc_configlib_vmt;
	stm32_rtc_date_vmt.read = stm32_rtc_date_read;
	stm32_rtc_date_vmt.write = stm32_rtc_date_write;
}

#endif


/**********************************************************************************************************************
 * Service initialization
 **********************************************************************************************************************/

static void stm32_rtc_enable_dbp(void) {
#if defined(STM32U5)
	PWR->DBPR |= PWR_DBPR_DBP;
#else
	PWR->CR1 |= PWR_CR1_DBP;
#endif
}


static bool stm32_rtc_start_lsi(void) {
#if defined(STM32U5)
	RCC->BDCR |= RCC_BDCR_LSION;
	return stm32_rtc_wait(&RCC->BDCR, RCC_BDCR_LSIRDY, true, STM32_RTC_TIMEOUT_MS);
#else
	RCC->CSR |= RCC_CSR_LSION;
	return stm32_rtc_wait(&RCC->CSR, RCC_CSR_LSIRDY, true, STM32_RTC_TIMEOUT_MS);
#endif
}


static bool stm32_rtc_start_lse(void) {
	RCC->BDCR |= RCC_BDCR_LSEON;
	if (!stm32_rtc_wait(&RCC->BDCR, RCC_BDCR_LSERDY, true, STM32_RTC_LSE_TIMEOUT_MS)) {
		RCC->BDCR &= ~RCC_BDCR_LSEON;
		return false;
	}
	return true;
}


/* Select the RTC clock source and make sure the oscillator is running. The selection survives a reset, it is
 * kept if already done (only a backup domain reset can change it). */
static stm32_rtc_ret_t stm32_rtc_setup_clock(Stm32Rtc *self) {
	uint32_t rtcsel = (RCC->BDCR & RCC_BDCR_RTCSEL) >> RCC_BDCR_RTCSEL_Pos;

	if (rtcsel == 0) {
		if (self->conf.lse && stm32_rtc_start_lse()) {
			rtcsel = RTC_BDCR_RTCSEL_LSE;
		} else {
			if (self->conf.lse) {
				u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("LSE oscillator failed to start"));
			}
			rtcsel = RTC_BDCR_RTCSEL_LSI;
		}
		RCC->BDCR = (RCC->BDCR & ~RCC_BDCR_RTCSEL) | (rtcsel << RCC_BDCR_RTCSEL_Pos);
	}

	if (rtcsel == RTC_BDCR_RTCSEL_LSE) {
		if (!(RCC->BDCR & RCC_BDCR_LSERDY) && !stm32_rtc_start_lse()) {
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("LSE oscillator is not running"));
			return STM32_RTC_RET_FAILED;
		}
	} else if (rtcsel == RTC_BDCR_RTCSEL_LSI) {
		if (!stm32_rtc_start_lsi()) {
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("LSI oscillator is not running"));
			return STM32_RTC_RET_FAILED;
		}
	} else {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("unsupported RTC clock source selected"));
		return STM32_RTC_RET_FAILED;
	}

	RCC->BDCR |= RCC_BDCR_RTCEN;

	return STM32_RTC_RET_OK;
}


/* Name of the selected RTC clock source for logging. */
static const char *stm32_rtc_source_name(void) {
	return ((RCC->BDCR & RCC_BDCR_RTCSEL) >> RCC_BDCR_RTCSEL_Pos) == RTC_BDCR_RTCSEL_LSE ? "lse" : "lsi";
}


/* Initialize the prescalers and the calendar if not already done. INITS is set only for a non-zero year, the
 * calendar starts at 2026-01-01 (a Thursday). */
static stm32_rtc_ret_t stm32_rtc_setup_calendar(void) {
	if (RTC->ICSR & RTC_ICSR_INITS) {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("RTC running (%s), keeping the time"), stm32_rtc_source_name());
		return STM32_RTC_RET_OK;
	}

	uint32_t freq = (strcmp(stm32_rtc_source_name(), "lse") == 0) ? STM32_RTC_LSE_FREQ : STM32_RTC_LSI_FREQ;
	uint32_t prediv_s = freq / (STM32_RTC_PREDIV_A + 1) - 1;

	stm32_rtc_unlock();
	if (!stm32_rtc_enter_init()) {
		stm32_rtc_lock();
		return STM32_RTC_RET_FAILED;
	}
	RTC->CR &= ~RTC_CR_BYPSHAD;
	/* The prescaler register must be written in two separate accesses. */
	RTC->PRER = prediv_s;
	RTC->PRER = prediv_s | (STM32_RTC_PREDIV_A << RTC_PRER_PREDIV_A_Pos);
	RTC->TR = 0;
	RTC->DR = (0x26 << RTC_DR_YU_Pos) | (4 << RTC_DR_WDU_Pos) | (1 << RTC_DR_MU_Pos) | (1 << RTC_DR_DU_Pos);
	bool synced = stm32_rtc_exit_init();
	stm32_rtc_lock();
	if (!synced) {
		return STM32_RTC_RET_FAILED;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("RTC initialized (%s)"), stm32_rtc_source_name());
	return STM32_RTC_RET_OK;
}


stm32_rtc_ret_t stm32_rtc_init(Stm32Rtc *self, const struct stm32_rtc_conf *conf) {
	if (u_assert(self != NULL) ||
	    u_assert(conf != NULL)) {
		return STM32_RTC_RET_NULL;
	}

	memset(self, 0, sizeof(Stm32Rtc));
	memcpy(&self->conf, conf, sizeof(struct stm32_rtc_conf));

	/* The RTC clock selection and the RTC registers are in the write-protected backup domain. */
	stm32_rtc_enable_dbp();

	if (stm32_rtc_setup_clock(self) != STM32_RTC_RET_OK ||
	    stm32_rtc_setup_calendar() != STM32_RTC_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot initialize the RTC"));
		return STM32_RTC_RET_FAILED;
	}

	self->clock.vmt = &stm32_rtc_clock_vmt;
	self->clock.parent = self;
	self->clock.name = "rtc";

#if defined(CONFIG_SERVICE_STM32_RTC_CONF)
	stm32_rtc_conf_init(self);
#endif

	return STM32_RTC_RET_OK;
}


stm32_rtc_ret_t stm32_rtc_free(Stm32Rtc *self) {
	if (u_assert(self != NULL)) {
		return STM32_RTC_RET_NULL;
	}

	return STM32_RTC_RET_OK;
}
