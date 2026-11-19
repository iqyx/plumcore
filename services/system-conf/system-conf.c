/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Service exposing system settings and statistics as a configuration subtree
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
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

/* Names of the enum system_conf_dst values, in the enum order. */
static const char * const system_conf_dst_names[] = {"none", "eu", "us"};


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


/* Find the status of the task with the given task number in the @p status array of @p count entries. */
static TaskStatus_t *system_conf_process_status(TaskStatus_t *status, UBaseType_t count, UBaseType_t number) {
	for (UBaseType_t i = 0; i < count; i++) {
		if (status[i].xTaskNumber == number) {
			return &status[i];
		}
	}
	return NULL;
}


static bool system_conf_process_name_used(SystemConf *self, const char *name) {
	for (struct system_conf_process *p = self->processes; p != NULL; p = p->next) {
		if (p->number != 0 && strcmp(p->name, name) == 0) {
			return true;
		}
	}
	return false;
}


/* Remove a process subtree from the "processes" children list. The next pointer of the removed node is
 * kept intact so a tree walker currently positioned on it can continue. */
static void system_conf_process_unlink(SystemConf *self, struct system_conf_process *p) {
	if (self->processes_conf.child == &p->process_conf) {
		self->processes_conf.child = p->process_conf.next;
		return;
	}
	for (ConfiglibValue *v = self->processes_conf.child; v != NULL; v = v->next) {
		if (v->next == &p->process_conf) {
			v->next = p->process_conf.next;
			return;
		}
	}
}


/* Create a process subtree for a newly discovered task, reusing an unused entry if there is one. */
static void system_conf_process_add(SystemConf *self, TaskStatus_t *status) {
	struct system_conf_process *p = self->processes;
	while (p != NULL && p->number != 0) {
		p = p->next;
	}
	if (p == NULL) {
		p = malloc(sizeof(struct system_conf_process));
		if (p == NULL) {
			return;
		}
		p->next = self->processes;
		p->number = 0;
		self->processes = p;
	}

	/* Make the name unique while the entry is still unused, so it doesn't clash with itself. */
	strlcpy(p->name, status->pcTaskName, sizeof(p->name));
	for (uint32_t n = 1; system_conf_process_name_used(self, p->name); n++) {
		snprintf(p->name, sizeof(p->name), "%s-%lu", status->pcTaskName, (unsigned long)n);
	}

	p->number = status->xTaskNumber;
	p->run_time_prev = 0;
	p->addr = (uint32_t)(uintptr_t)status->xHandle;
	p->stack_total = (uint32_t)((uintptr_t)status->pxEndOfStack - (uintptr_t)status->pxStackBase) + sizeof(StackType_t);
	p->stack_used = 0;
	p->time_used = 0.0f;

	configlib_init_map(&p->process_conf, p->name, NULL, CONF_SUBTREE);
	configlib_init_map_append(&p->addr_conf, "addr", &p->addr, CONF_U32, &p->process_conf, CONF_DIR_CHILD);
	p->addr_conf.flags = CONF_READ | CONF_STATUS;
	configlib_init_map_append(&p->stack_total_conf, "stack-total", &p->stack_total, CONF_U32, &p->process_conf,
		CONF_DIR_CHILD);
	p->stack_total_conf.flags = CONF_READ | CONF_STATUS;
	configlib_init_map_append(&p->stack_used_conf, "stack-used", &p->stack_used, CONF_U32, &p->process_conf,
		CONF_DIR_CHILD);
	p->stack_used_conf.flags = CONF_READ | CONF_STATUS;
	configlib_init_map_append(&p->time_used_conf, "time-used", &p->time_used, CONF_F, &p->process_conf,
		CONF_DIR_CHILD);
	p->time_used_conf.flags = CONF_READ | CONF_STATUS;
	configlib_append(&p->process_conf, &self->processes_conf, CONF_DIR_CHILD);
}


/* Synchronize the "processes" subtree with the current FreeRTOS task list. The total CPU time of the
 * refresh interval is the sum of the task run time deltas, not the run time counter itself, as the
 * counter may be a short timer wrapping many times between refreshes. */
static void system_conf_process_refresh(SystemConf *self) {
	UBaseType_t count = uxTaskGetNumberOfTasks();
	TaskStatus_t *status = malloc(count * sizeof(TaskStatus_t));
	if (status == NULL) {
		return;
	}
	/* Returns 0 if a task was created in the meantime and the array is too small, skip this round. */
	count = uxTaskGetSystemState(status, count, NULL);

	/* Drop the entries of deleted tasks, a NULL handle marks the tasks already having an entry. */
	for (struct system_conf_process *p = self->processes; p != NULL; p = p->next) {
		if (p->number == 0) {
			continue;
		}
		TaskStatus_t *s = system_conf_process_status(status, count, p->number);
		if (s == NULL) {
			system_conf_process_unlink(self, p);
			p->number = 0;
		} else {
			s->xHandle = NULL;
		}
	}

	for (UBaseType_t i = 0; i < count; i++) {
		if (status[i].xHandle != NULL) {
			system_conf_process_add(self, &status[i]);
		}
	}

	configRUN_TIME_COUNTER_TYPE total = 0;
	for (struct system_conf_process *p = self->processes; p != NULL; p = p->next) {
		TaskStatus_t *s = system_conf_process_status(status, count, p->number);
		if (p->number != 0 && s != NULL) {
			total += s->ulRunTimeCounter - p->run_time_prev;
		}
	}

	for (struct system_conf_process *p = self->processes; p != NULL; p = p->next) {
		TaskStatus_t *s = system_conf_process_status(status, count, p->number);
		if (p->number == 0 || s == NULL) {
			continue;
		}
		p->stack_used = p->stack_total - (uint32_t)s->usStackHighWaterMark * sizeof(StackType_t);
		if (total > 0) {
			p->time_used = (float)(s->ulRunTimeCounter - p->run_time_prev) / (float)total;
		}
		p->run_time_prev = s->ulRunTimeCounter;
	}

	free(status);
}


static void system_conf_process_task(void *p) {
	SystemConf *self = (SystemConf *)p;

	while (true) {
		system_conf_process_refresh(self);
		vTaskDelay(pdMS_TO_TICKS(SYSTEM_CONF_PROCESS_INTERVAL_MS));
	}
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

	/* Writable "hostname" leaf under the root, empty until configured. Longer writes are refused. */
	configlib_init(&self->hostname_conf, "hostname");
	configlib_map_string(&self->hostname_conf, self->hostname, sizeof(self->hostname));
	configlib_append(&self->hostname_conf, &self->root_conf, CONF_DIR_CHILD);
	self->hostname_conf.flags = CONF_READ | CONF_WRITE;
	configlib_set_constraint(&self->hostname_conf, (union conf_constraint) {
		.str.max_len = SYSTEM_CONF_HOSTNAME_LEN,
	});

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

	/* "processes" subtree, filled and kept in sync with the task list by the process-status task. */
	configlib_init_map_append(&self->processes_conf, "processes", NULL, CONF_SUBTREE, &self->root_conf, CONF_DIR_CHILD);
	self->processes_conf.flags = CONF_READ | CONF_STATUS;
	if (xTaskCreate(system_conf_process_task, "process-status", SYSTEM_CONF_PROCESS_STACK_SIZE, (void *)self, 1,
	                &self->process_task) != pdPASS) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create the process-status task"));
	}

	/* "timezone" subtree, UTC without daylight saving time until configured. */
	configlib_init_map_append(&self->timezone_conf, "timezone", NULL, CONF_SUBTREE, &self->root_conf, CONF_DIR_CHILD);
	configlib_init_map_append(&self->tz_offset_conf, "offset", &self->tz_offset, CONF_S16, &self->timezone_conf,
		CONF_DIR_CHILD);
	self->tz_offset_conf.flags = CONF_READ | CONF_WRITE;
	configlib_set_default(&self->tz_offset_conf, (union conf_val){.i16 = 0});
	configlib_set_constraint(&self->tz_offset_conf, (union conf_constraint) {
		.numeric = {.min.i16 = -720, .max.i16 = 840},
	});
	configlib_set_description(&self->tz_offset_conf, "Timezone offset",
		"Standard (winter) time offset from UTC in minutes, positive east of Greenwich (eg. 60 for CET).");
	configlib_init_map_append(&self->tz_dst_conf, "dst", &self->tz_dst, CONF_ENUM, &self->timezone_conf,
		CONF_DIR_CHILD);
	self->tz_dst_conf.flags = CONF_READ | CONF_WRITE;
	configlib_set_default(&self->tz_dst_conf, (union conf_val){.u32 = SYSTEM_CONF_DST_NONE});
	configlib_set_constraint(&self->tz_dst_conf, (union conf_constraint) {
		.enumeration = {
			.values = system_conf_dst_names,
			.count = sizeof(system_conf_dst_names) / sizeof(system_conf_dst_names[0]),
		},
	});
	configlib_set_description(&self->tz_dst_conf, "Daylight saving time rule",
		"none - no DST, eu - last Sunday of March to last Sunday of October at 01:00 UTC, us - second Sunday of "
		"March to first Sunday of November at 02:00 local time.");

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

	if (self->process_task != NULL) {
		vTaskDelete(self->process_task);
	}
	while (self->processes != NULL) {
		struct system_conf_process *p = self->processes;
		self->processes = p->next;
		free(p);
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
