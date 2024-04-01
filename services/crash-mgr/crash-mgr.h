/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Crash manager and debug service
 *
 * Copyright (c) 2023-2024, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <main.h>
#include <interfaces/fs.h>
#include <cbor.h>
#include <heatshrink_encoder.h>
#include "backtrace.h"


#define CRASH_MGR_MAX_DST_REGIONS 8
#define CRASH_MGR_TASK_STACK 512
#define CRASH_MGR_BACKTRACE_SIZE 10


typedef enum {
	CRASH_MGR_RET_OK = 0,
	CRASH_MGR_RET_FAILED,
} crash_mgr_ret_t;

typedef crash_mgr_ret_t (*invalid_function_t)(void);

enum crash_mgr_fault {
	CRASH_MGR_FAULT_HARD,
	CRASH_MGR_FAULT_BUS,
	CRASH_MGR_FAULT_MEM,
	CRASH_MGR_FAULT_USAGE,
};

enum crash_mgr_state {
	CRASH_MGR_STATE_NONE = 0,
	CRASH_MGR_STATE_READY,
};


struct crash_mgr_region {
	void *addr;
	size_t size;
};

#define COREDUMP_VERSION 1

typedef struct coredump {
	CborEncoder encoder;
	CborEncoder encoder_map;

	heatshrink_encoder *hse;
	/* Reserve space for the encoder and the buffer. See heatshrink.h. */
	uint8_t hse_buffer[sizeof(heatshrink_encoder) + (2 << CONFIG_CRASH_MGR_COMP_HS_WINDOW_BITS)];
	uint8_t hse_search_index[(2 << CONFIG_CRASH_MGR_COMP_HS_WINDOW_BITS) * sizeof(uint16_t) + sizeof(struct hs_index)];

	uint8_t *work_buf;
	size_t work_buf_size;

} CoreDump;

typedef struct crash_mgr {
	volatile enum crash_mgr_state state;
	TaskHandle_t handler_task;

	/* Mem dump configuration. */
	const struct crash_mgr_region *memdump_regions;

	CoreDump coredump;
	Fs *coredump_fs;
	const char *coredump_filename;

	/* Backtrace unwinding related. */
	backtrace_t bt_buf[CRASH_MGR_BACKTRACE_SIZE];
	uint32_t bt_len;

	/* Crash data. */
	TaskHandle_t task;
	enum crash_mgr_fault fault;

	size_t work_buf_size;
	/* Flexible array member */
	uint8_t work_buf[];
} CrashMgr;


extern CrashMgr *crash_mgr;

crash_mgr_ret_t coredump_init(CoreDump *self, uint8_t *buf, size_t len);
crash_mgr_ret_t coredump_finish(CoreDump *self);
crash_mgr_ret_t coredump_save(CoreDump *self, Fs *fs, const char *filename);
crash_mgr_ret_t coredump_mem(CoreDump *self, uint8_t *buf, size_t size);
crash_mgr_ret_t coredump_task_list(CoreDump *self);
crash_mgr_ret_t coredump_fault_info(CoreDump *self, TaskHandle_t task, enum crash_mgr_fault fault);
crash_mgr_ret_t coredump_registers(CoreDump *self, TaskHandle_t task, enum crash_mgr_fault fault);
crash_mgr_ret_t coredump_log(CoreDump *self);
crash_mgr_ret_t coredump_backtrace(CoreDump *self, backtrace_t *bt, size_t bt_len);


crash_mgr_ret_t crash_mgr_init(CrashMgr *self, size_t max_instance_size);
crash_mgr_ret_t crash_mgr_free(CrashMgr *self);
crash_mgr_ret_t crash_mgr_save_coredump(CrashMgr *self, Fs *fs, const char *filename);
crash_mgr_ret_t crash_mgr_enable_memdump(CrashMgr *self, const struct crash_mgr_region *regions);

crash_mgr_ret_t crash_mgr_invalid_instruction_fault(void);
crash_mgr_ret_t crash_mgr_hard_fault(void);


