/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Service exposing system settings and statistics as a configuration subtree
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <interfaces/conf.h>
#include <configlib.h>


/* Length of the backing buffers for the string leaves. configlib does not copy the value string so
 * each leaf is backed by a buffer with the lifetime of the service instance. */
#define SYSTEM_CONF_STR_LEN 48
#define SYSTEM_CONF_HOSTNAME_LEN 32

typedef enum {
	SYSTEM_CONF_RET_OK = 0,
	SYSTEM_CONF_RET_FAILED,
} system_conf_ret_t;

/* Daylight saving time rules selectable in the "timezone dst" leaf, the values are the enum indices. */
enum system_conf_dst {
	/* No daylight saving time. */
	SYSTEM_CONF_DST_NONE = 0,
	/* EU: +1 h from the last Sunday of March 01:00 UTC to the last Sunday of October 01:00 UTC. */
	SYSTEM_CONF_DST_EU,
	/* US: +1 h from the second Sunday of March 02:00 to the first Sunday of November 02:00 local time. */
	SYSTEM_CONF_DST_US,
};


typedef struct system_conf {
	/* Root of the configuration tree, advertised as "system". */
	ConfiglibValue root_conf;

	/* "firmware" subtree exposing the compile-time build identity. */
	ConfiglibValue firmware_conf;
	ConfiglibValue version_conf;
	ConfiglibValue port_conf;
	ConfiglibValue application_conf;
	ConfiglibValue date_conf;

	/* Backing storage for the string leaves above. */
	char version[SYSTEM_CONF_STR_LEN];
	char port[SYSTEM_CONF_STR_LEN];
	char application[SYSTEM_CONF_STR_LEN];
	char date[SYSTEM_CONF_STR_LEN];

	/* "hostname" leaf, a writable device name of up to SYSTEM_CONF_HOSTNAME_LEN characters. */
	ConfiglibValue hostname_conf;
	char hostname[SYSTEM_CONF_HOSTNAME_LEN + 1];

	/* "uptime" leaf (seconds since the scheduler started). The node is wired only when the FreeRTOS
	 * tick counter is available; the value is computed on read from it, so no task is needed. */
	ConfiglibValue uptime_conf;
	uint32_t uptime;

	/* "memory" subtree with byte counters. The constant values (total/static/heap) are computed once
	 * in init(); "used" is computed on read. All values are in bytes. */
	ConfiglibValue memory_conf;
	ConfiglibValue mem_total_conf;
	ConfiglibValue mem_static_conf;
	ConfiglibValue mem_heap_conf;
	ConfiglibValue mem_used_conf;
	uint32_t mem_total;
	uint32_t mem_static;
	uint32_t mem_heap;
	uint32_t mem_used;

	/* "timezone" subtree used to display the UTC system time as local time. The time itself is always kept in
	 * UTC. "offset" is the standard time offset in minutes east of UTC, "dst" an enum system_conf_dst rule. */
	ConfiglibValue timezone_conf;
	ConfiglibValue tz_offset_conf;
	ConfiglibValue tz_dst_conf;
	int16_t tz_offset;
	uint32_t tz_dst;

	/* "reset" trigger leaf. Reads always false; writing true resets the device. */
	ConfiglibValue reset_conf;
} SystemConf;


/**
 * @brief Initialize the SystemConf service
 *
 * Builds a configuration tree rooted at "system" holding a "firmware" subtree with the version,
 * port, application and build date of the running firmware. The values are read from the compile
 * time identity provided through main.h.
 *
 * @param self The instance to initialize. Must be allocated beforehand.
 *
 * @return SYSTEM_CONF_RET_FAILED on error or SYSTEM_CONF_RET_OK otherwise.
 */
system_conf_ret_t system_conf_init(SystemConf *self);


system_conf_ret_t system_conf_free(SystemConf *self);


/**
 * @brief Get the configuration tree root of the service
 *
 * The returned node can be attached to a parent configuration tree or handed to a service exporting
 * the tree (eg. proto-conf) or registered with the service locator as ISERVICELOCATOR_TYPE_CONF.
 *
 * @param conf Returns the root Conf node.
 */
system_conf_ret_t system_conf_get_conf(SystemConf *self, Conf **conf);
