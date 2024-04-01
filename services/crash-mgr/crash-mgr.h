/* SPDX-License-Identifier: GPL-3.0-or-later
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
	CRASH_MGR_FAULT_NMI,
	CRASH_MGR_FAULT_HARD,
	CRASH_MGR_FAULT_BUS,
	CRASH_MGR_FAULT_MEM,
	CRASH_MGR_FAULT_USAGE,
};

enum crash_mgr_state {
	/* Default state, crash manager is not initialised yet. We do not catch faults
	 * because that could make things worse. */
	CRASH_MGR_STATE_NONE = 0,

	/* Ready to handle crashes/faults. */
	CRASH_MGR_STATE_READY,
};


struct crash_mgr_region {
	void *addr;
	size_t size;
};

#define COREDUMP_VERSION 1

typedef struct coredump {
	/* Top level map CBOR encoder instances. Used to encode core dumps. */
	CborEncoder encoder;
	CborEncoder encoder_map;

	heatshrink_encoder *hse;
	/* Reserve space for the encoder and the buffer. See heatshrink.h. */
	uint8_t hse_buffer[sizeof(heatshrink_encoder) + (2 << CONFIG_CRASH_MGR_COMP_HS_WINDOW_BITS)];
	uint8_t hse_search_index[(2 << CONFIG_CRASH_MGR_COMP_HS_WINDOW_BITS) * sizeof(uint16_t) + sizeof(struct hs_index)];

	/* Temporary working buffer to save core dump to. */
	uint8_t *work_buf;
	size_t work_buf_size;
} CoreDump;

typedef struct crash_mgr {
	volatile enum crash_mgr_state state;
	TaskHandle_t handler_task;

	/* Memory regions to dump after a crash. If NULL, no memory is dumped. */
	const struct crash_mgr_region *memdump_regions;

	/* Core dumper instance. It is initialised when the core dump is requested. */
	CoreDump coredump;

	/* If not NULL, core dump will be saved to a file to a selected filesystem. */
	Fs *coredump_fs;
	const char *coredump_filename;

	/* Backtrace unwinding related. */
	backtrace_t bt_buf[CRASH_MGR_BACKTRACE_SIZE];
	uint32_t bt_len;

	/* Crash data. */
	TaskHandle_t task;
	enum crash_mgr_fault fault;

	size_t work_buf_size;
	/* Flexible array member. Size computed in crash_mgr_init. */
	uint8_t work_buf[];
} CrashMgr;


/* There is only a single instance of the crash manager. Make it accessible. */
extern CrashMgr *crash_mgr;

/**
 * @brief Initialise a core dumper instance
 *
 * @param self Core dumper instance
 * @param buf Working buffer to save core dumps to
 * @param len Size of the @p buf working buffer in bytes
 */
crash_mgr_ret_t coredump_init(CoreDump *self, uint8_t *buf, size_t len);

/**
 * @brief Finish the core dump output
 *
 * Once the dump is complete and all the dumping tasks are called
 * as appropriate, the dump must be finished to be ready to be saved.
 *
 * @param self Core dumper instance
 */
crash_mgr_ret_t coredump_finish(CoreDump *self);

/**
 * @brief Save the core dump
 *
 * @param self Core dumper instance
 * @param fs Filesystem interface where the file to save to resides
 * @param filename Name of the file on @p fs filesystem
 */
crash_mgr_ret_t coredump_save(CoreDump *self, Fs *fs, const char *filename);

/**
 * @brief Add a memory region dump to the core dump
 *
 * Core dumper must be properly initialised. After dumping the requested memory region(s)
 * and doing other dumps, the dump must be finished by calling @p coredump_finish.
 *
 * @param self Core dumper instance
 * @param buf Start of the memory region to dump
 * @param size Size of the memory region to dump
 */
crash_mgr_ret_t coredump_mem(CoreDump *self, uint8_t *buf, size_t size);

/**
 * @brief Add a task list to the core dump
 */
crash_mgr_ret_t coredump_task_list(CoreDump *self);

/**
 * @brief Save basic fault information to the dump
 */
crash_mgr_ret_t coredump_fault_info(CoreDump *self, TaskHandle_t task, enum crash_mgr_fault fault);

/**
 * @brief Save processor register values to the dump
 */
crash_mgr_ret_t coredump_registers(CoreDump *self, TaskHandle_t task, enum crash_mgr_fault fault);

/**
 * @brief Save the current log ring buffer content to the dump
 */
crash_mgr_ret_t coredump_log(CoreDump *self);

/**
 * @brief Save backtrace of the failed thread to the dump
 */
crash_mgr_ret_t coredump_backtrace(CoreDump *self, backtrace_t *bt, size_t bt_len);


/**
 * @brief Initialise the crash manager instance
 *
 * Call to initialise the crash manager state, create the handler task and start
 * handling crashes/faults. Use the @p max_instance_size variable to restrict the size
 * of the instance (including the working buffer) to the size of the available memory block.
 *
 * @param self Crash manager instance
 * @param max_instance_size Maximum size of the crash manager instance in bytes,
 *                          including the working buffer to store the core dump.
 */
crash_mgr_ret_t crash_mgr_init(CrashMgr *self, size_t max_instance_size);

/**
 * @brief Free all instance resources, stop the handling task
 *
 * @todo Unimplemented
 */
crash_mgr_ret_t crash_mgr_free(CrashMgr *self);

/**
 * @brief Enable saving core dumps to a file
 *
 * Set the crash manager to save core dumps to a filesystem @p fs in a file named @p filename.
 */
crash_mgr_ret_t crash_mgr_save_coredump(CrashMgr *self, Fs *fs, const char *filename);

/**
 * @brief Set memory regions to dump (port dependent configuration)
 *
 * @param regions Array of memory regions to save fhen a fault occurs.
 *                The last member must be {NULL, 0}.
 */
crash_mgr_ret_t crash_mgr_enable_memdump(CrashMgr *self, const struct crash_mgr_region *regions);


crash_mgr_ret_t crash_mgr_invalid_instruction_fault(void);
crash_mgr_ret_t crash_mgr_hard_fault(void);
crash_mgr_ret_t crash_mgr_nmi(void);


