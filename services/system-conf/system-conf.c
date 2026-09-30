/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Service exposing system settings and statistics as a configuration subtree
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <main.h>

#include <interfaces/conf.h>
#include <configlib.h>

#include "system-conf.h"

#define MODULE_NAME "system-conf"

/* RAM region bounds provided by the linker script and the end of the statically allocated RAM. The
 * addresses of these symbols carry the values (see the linker script). */
extern char _ram_start[];
extern char _ram_size[];
extern char _ebss[];


/* The uptime and heap-used leaves are "computed" nodes: created as ordinary configlib nodes (so tree
 * walking, stat, etc. keep working) but with their value derived on demand instead of stored, so no
 * task or sampling is needed. Only the read handler differs from a plain configlib node; the storage
 * for each node's overridden vmt is here and system_conf_make_computed() below wires it up. */
#if defined(configTICK_RATE_HZ)
static struct conf_vmt system_conf_uptime_vmt;
#endif
#if defined(configTOTAL_HEAP_SIZE)
static struct conf_vmt system_conf_mem_used_vmt;
#endif
static struct conf_vmt system_conf_reset_vmt;


/* Turn an already-appended configlib node into a computed node: start from the vmt configlib assigned
 * to it (so walk/stat/get_* keep working) and override its read and write handlers. Pass NULL for a
 * handler to keep it unimplemented - proto clients treat a NULL write as "not writable" and a NULL
 * read as "not readable". */
static void system_conf_make_computed(ConfiglibValue *node, struct conf_vmt *vmt,
                                      conf_ret_t (*read)(Conf *self, union conf_val *val),
                                      conf_ret_t (*write)(Conf *self, const union conf_val val)) {
	*vmt = *node->conf.vmt;
	vmt->read = read;
	vmt->write = write;
	node->conf.vmt = vmt;
}


/* The "reset" leaf is a write-triggered action: it always reads false, and writing true resets the
 * device via the Cortex-M SYSRESETREQ bit in SCB->AIRCR (0xE000ED0C). A short delay first lets the log
 * line and any in-flight reply drain. */
static conf_ret_t system_conf_reset_read(Conf *self, union conf_val *val) {
	(void)self;
	val->b = false;
	return CONF_RET_OK;
}


static conf_ret_t system_conf_reset_write(Conf *self, const union conf_val val) {
	(void)self;
	if (val.b) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("reset requested"));
		vTaskDelay(pdMS_TO_TICKS(500));
		*(volatile uint32_t *)0xE000ED0C = 0x05FA0004UL;
		while (1) {
			;
		}
	}
	return CONF_RET_OK;
}


#if defined(configTICK_RATE_HZ)
/* Uptime in whole seconds, derived from the FreeRTOS tick counter on read.
 *
 * WRAPAROUND: xTaskGetTickCount() is a 32-bit counter here (configUSE_16_BIT_TICKS = 0), so the tick
 * count - and therefore this uptime value - wraps back to zero every 2^32 / configTICK_RATE_HZ seconds,
 * i.e. ~49.7 days at 1000 Hz (2^32 / 1000 = 4294967 s = 49 d 17 h 2 m 47 s). This is accepted for now:
 * we deliberately do NOT track the wraps. Extending the range would require either a wider tick type
 * (configTICK_TYPE_WIDTH_IN_BITS = 64, a global platform change) or maintaining a local overflow
 * counter fed by periodic sampling (FreeRTOS keeps its own xNumOfOverflows private, so it cannot be
 * reused). Revisit here if a longer uptime horizon is needed. */
static conf_ret_t system_conf_uptime_read(Conf *self, union conf_val *val) {
	ConfiglibValue *value = self->parent;
	*(uint32_t *)value->var = (uint32_t)(xTaskGetTickCount() / configTICK_RATE_HZ);
	val->u32 = *(uint32_t *)value->var;
	return CONF_RET_OK;
}
#endif


#if defined(configTOTAL_HEAP_SIZE)
/* Currently used heap in bytes, computed on read as the configured heap size minus the free heap
 * reported by the FreeRTOS allocator. */
static conf_ret_t system_conf_mem_used_read(Conf *self, union conf_val *val) {
	ConfiglibValue *value = self->parent;
	*(uint32_t *)value->var = (uint32_t)configTOTAL_HEAP_SIZE - (uint32_t)xPortGetFreeHeapSize();
	val->u32 = *(uint32_t *)value->var;
	return CONF_RET_OK;
}
#endif


/* Append a read-only string leaf named @p name under @p parent. The value is copied into @p buf
 * (of @p size bytes) which backs the configuration node, so it stays valid for the lifetime of the
 * service instance. The leaves are compile-time constants, hence flagged read-only. */
static void system_conf_add_string(ConfiglibValue *node, const char *name, char *buf, size_t size,
                                   const char *value, ConfiglibValue *parent) {
	strlcpy(buf, value, size);
	configlib_init(node, name);
	configlib_map_string(node, buf, size);
	configlib_append(node, parent, CONF_DIR_CHILD);
	node->flags = CONF_READ | CONF_CONST;
}


system_conf_ret_t system_conf_init(SystemConf *self) {
	if (u_assert(self != NULL)) {
		return SYSTEM_CONF_RET_FAILED;
	}
	memset(self, 0, sizeof(SystemConf));

	/* Root subtree advertised to the service locator as "system". */
	configlib_init_map(&self->root_conf, "system", NULL, CONF_SUBTREE);

	/* "firmware" subtree holding the compile-time build/version identity. */
	configlib_init_map_append(&self->firmware_conf, "firmware", NULL, CONF_SUBTREE, &self->root_conf, CONF_DIR_CHILD);

	system_conf_add_string(&self->version_conf, "version", self->version, sizeof(self->version),
	                       UMESH_VERSION, &self->firmware_conf);
	system_conf_add_string(&self->port_conf, "port", self->port, sizeof(self->port),
	                       PORT_NAME, &self->firmware_conf);
	system_conf_add_string(&self->application_conf, "application", self->application, sizeof(self->application),
	                       CONFIG_APP_NAME, &self->firmware_conf);
	system_conf_add_string(&self->date_conf, "date", self->date, sizeof(self->date),
	                       UMESH_BUILD_DATE, &self->firmware_conf);

#if defined(configTICK_RATE_HZ)
	/* "uptime" leaf directly under the root. Created as a CONF_U32 configlib node backing the counter,
	 * then its vmt is swapped for the computed-on-read implementation above. */
	configlib_init_map_append(&self->uptime_conf, "uptime", &self->uptime, CONF_U32, &self->root_conf, CONF_DIR_CHILD);
	self->uptime_conf.flags = CONF_READ | CONF_STATUS;
	system_conf_make_computed(&self->uptime_conf, &system_conf_uptime_vmt, system_conf_uptime_read, NULL);
#endif

	/* "memory" subtree with byte counters. All values are in bytes.
	 *   total  - total RAM region available to the firmware (LENGTH(ram) from the linker script)
	 *   static - RAM statically allocated at link time (data + bss + noinit), from the region start up
	 *            to _ebss; the heap starts above _ebss so it is naturally excluded
	 *   heap   - heap size configured in the FreeRTOS config (configTOTAL_HEAP_SIZE)
	 *   used   - currently used heap, computed on read from the allocator */
	self->mem_total = (uint32_t)(uintptr_t)_ram_size;
	self->mem_static = (uint32_t)(uintptr_t)_ebss - (uint32_t)(uintptr_t)_ram_start;
	configlib_init_map_append(&self->memory_conf, "memory", NULL, CONF_SUBTREE, &self->root_conf, CONF_DIR_CHILD);
	configlib_init_map_append(&self->mem_total_conf, "total", &self->mem_total, CONF_U32, &self->memory_conf, CONF_DIR_CHILD);
	self->mem_total_conf.flags = CONF_READ | CONF_CONST;
	configlib_init_map_append(&self->mem_static_conf, "static", &self->mem_static, CONF_U32, &self->memory_conf, CONF_DIR_CHILD);
	self->mem_static_conf.flags = CONF_READ | CONF_CONST;
#if defined(configTOTAL_HEAP_SIZE)
	self->mem_heap = (uint32_t)configTOTAL_HEAP_SIZE;
	configlib_init_map_append(&self->mem_heap_conf, "heap", &self->mem_heap, CONF_U32, &self->memory_conf, CONF_DIR_CHILD);
	self->mem_heap_conf.flags = CONF_READ | CONF_CONST;
	configlib_init_map_append(&self->mem_used_conf, "used", &self->mem_used, CONF_U32, &self->memory_conf, CONF_DIR_CHILD);
	self->mem_used_conf.flags = CONF_READ | CONF_STATUS;
	system_conf_make_computed(&self->mem_used_conf, &system_conf_mem_used_vmt, system_conf_mem_used_read, NULL);
#endif

	/* "reset" trigger leaf under the root. Reads false; writing true resets the device. */
	configlib_init_map_append(&self->reset_conf, "reset", NULL, CONF_B, &self->root_conf, CONF_DIR_CHILD);
	self->reset_conf.flags = CONF_READ | CONF_WRITE;
	system_conf_make_computed(&self->reset_conf, &system_conf_reset_vmt, system_conf_reset_read, system_conf_reset_write);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialized"));
	return SYSTEM_CONF_RET_OK;
}


system_conf_ret_t system_conf_free(SystemConf *self) {
	if (u_assert(self != NULL)) {
		return SYSTEM_CONF_RET_FAILED;
	}

	return SYSTEM_CONF_RET_OK;
}


system_conf_ret_t system_conf_get_conf(SystemConf *self, Conf **conf) {
	if (u_assert(self != NULL) ||
	    u_assert(conf != NULL)) {
		return SYSTEM_CONF_RET_FAILED;
	}

	*conf = &self->root_conf.conf;
	return SYSTEM_CONF_RET_OK;
}
