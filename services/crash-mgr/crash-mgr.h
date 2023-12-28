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


#define CRASH_MGR_MAX_DST_REGIONS 8
#define CRASH_MGR_TASK_STACK 512

#define CRASH_MGR_HS_WINDOW_BITS 8
#define CRASH_MGR_HS_LOOKAHEAD_BITS 5

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

typedef struct crash_mgr {
	volatile enum crash_mgr_state state;
	TaskHandle_t handler_task;

	bool logdump_enabled;

	/* Core dump configuration. */
	Fs *coredump_fs;
	const char *coredump_filename;
	CborEncoder encoder;
	CborEncoder encoder_map;

	/* Mem dump configuration. */
	const struct crash_mgr_region *memdump_regions;

	/* Crash data. */
	TaskHandle_t task;
	enum crash_mgr_fault fault;

	heatshrink_encoder *hse;
	/* Reserve space for the encoder and the buffer. See heatshrink.h. */
	uint8_t hse_buffer[sizeof(heatshrink_encoder) + (2 << CRASH_MGR_HS_WINDOW_BITS)];
	uint8_t hse_search_index[(2 << CRASH_MGR_HS_WINDOW_BITS) * sizeof(uint16_t) + sizeof(struct hs_index)];

	size_t work_buf_size;
	size_t work_buf_used;
	uint8_t work_buf[];

} CrashMgr;


extern CrashMgr *crash_mgr;

crash_mgr_ret_t crash_mgr_init(CrashMgr *self, size_t max_instance_size);
crash_mgr_ret_t crash_mgr_free(CrashMgr *self);
crash_mgr_ret_t crash_mgr_save_coredump(CrashMgr *self, Fs *fs, const char *filename);
crash_mgr_ret_t crash_mgr_enable_memdump(CrashMgr *self, const struct crash_mgr_region *regions);

crash_mgr_ret_t crash_mgr_invalid_instruction_fault(void);
crash_mgr_ret_t crash_mgr_hard_fault(void);


