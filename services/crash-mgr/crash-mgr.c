/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Crash manager and debug service
 *
 * Copyright (c) 2023-2024, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

#include <main.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/cm3/nvic.h>
#include <cbor.h>

#include "crash-mgr.h"

#define MODULE_NAME "crash-mgr"


/* Crash manager is a singleton service, a special one. There is exactly one
 * instance of the crash manager if there is any at all. */
CrashMgr *crash_mgr;

static const char *fault_str[] = {"HARD", "BUS", "MEM", "USAGE"};

static crash_mgr_ret_t do_logdump(CrashMgr *self, TaskHandle_t task, enum crash_mgr_fault fault) {
	(void)self;

	TaskStatus_t task_status = {0};
	vTaskGetInfo(task, &task_status, pdFALSE, eReady);
	/* The first member MUST be the current stack pointer (top of stack). */
	uint32_t *sp = *(uint32_t **)task;

	u_log(system_log, LOG_TYPE_CRIT, U_LOG_MODULE_PREFIX("%s: %s fault"), task_status.pcTaskName, fault_str[fault]);
	u_log(system_log, LOG_TYPE_CRIT, U_LOG_MODULE_PREFIX("%s: SP = %0p, TCB = %0p"), task_status.pcTaskName, sp, task);

	u_log(system_log, LOG_TYPE_CRIT, U_LOG_MODULE_PREFIX("%s: xPSR = %0p, PC = %0p, LR = %0p, R12 = %0p"), task_status.pcTaskName, sp[16], sp[15], sp[14], sp[13]);
	u_log(system_log, LOG_TYPE_CRIT, U_LOG_MODULE_PREFIX("%s: R3 = %0p, R2 = %0p, R1 = %0p, R0 = %0p"), task_status.pcTaskName, sp[12], sp[11], sp[10], sp[9]);
	u_log(system_log, LOG_TYPE_CRIT, U_LOG_MODULE_PREFIX("%s: EXC_RETURN = %0p"), task_status.pcTaskName, sp[8]);

	u_log(system_log, LOG_TYPE_CRIT, U_LOG_MODULE_PREFIX("%s: R11 = %0p, R10 = %0p, R9 = %0p, R8 = %0p"), task_status.pcTaskName, sp[7], sp[6], sp[5], sp[4]);
	u_log(system_log, LOG_TYPE_CRIT, U_LOG_MODULE_PREFIX("%s: R7 = %0p, R6 = %0p, R5 = %0p, R4 = %0p"), task_status.pcTaskName, sp[3], sp[2], sp[1], sp[0]);

	return CRASH_MGR_RET_OK;
}


static crash_mgr_ret_t prepare_coredump(CrashMgr *self) {
	cbor_encoder_init(&self->encoder, self->work_buf, self->work_buf_size, 0);
	cbor_encoder_create_map(&self->encoder, &self->encoder_map, CborIndefiniteLength);
	cbor_encode_text_stringz(&self->encoder_map, "cbcd");
	cbor_encode_int(&self->encoder_map, 1);

	/* We are using a compressor. Save compression config as it is needed for decompression. */
	cbor_encode_text_stringz(&self->encoder_map, "comp");
	cbor_encode_text_stringz(&self->encoder_map, "hs");
	cbor_encode_text_stringz(&self->encoder_map, "hs-wsz2");
	cbor_encode_int(&self->encoder_map, CRASH_MGR_HS_WINDOW_BITS);
	cbor_encode_text_stringz(&self->encoder_map, "hs-lsz2");
	cbor_encode_int(&self->encoder_map, CRASH_MGR_HS_LOOKAHEAD_BITS);


	return CRASH_MGR_RET_OK;
}


static crash_mgr_ret_t do_memdump(CrashMgr *self, uint8_t *buf, size_t size) {
	/* Prepare the encoder (indefinite byte string). */
	cbor_encode_text_stringz(&self->encoder_map, "mem");

	CborEncoder encoder_dump;
	cbor_encoder_create_map(&self->encoder_map, &encoder_dump, CborIndefiniteLength);
	cbor_encode_text_stringz(&encoder_dump, "addr");
	cbor_encode_int(&encoder_dump, (uint32_t)buf);
	cbor_encode_text_stringz(&encoder_dump, "size");
	cbor_encode_int(&encoder_dump, size);

	cbor_encode_text_stringz(&encoder_dump, "data");
	CborEncoder encoder_dump_data;
	cbor_encoder_create_byte_string_array(&encoder_dump, &encoder_dump_data, CborIndefiniteLength);

	/* Prepare the heatshrink compressor. */
	self->hse = (heatshrink_encoder *)self->hse_buffer;
	self->hse->search_index = (struct hs_index *)self->hse_search_index;
	self->hse->search_index->size = (2 << CRASH_MGR_HS_WINDOW_BITS) * sizeof(uint16_t);
	self->hse->window_sz2 = CRASH_MGR_HS_WINDOW_BITS;
	self->hse->lookahead_sz2 = CRASH_MGR_HS_LOOKAHEAD_BITS;
	heatshrink_encoder_reset(self->hse);

	/* Process the whole region */
	while (size) {
		/* Feed the compressor state machine with some bytes first. */
		size_t sink_size;
		HSE_sink_res sres = heatshrink_encoder_sink(self->hse, buf, size, &sink_size);
		if (sres < 0) {
			return CRASH_MGR_RET_FAILED;
		}
		size -= sink_size;
		buf += sink_size;

		/* Poll for compressed data if there is any. */
		size_t poll_size = 0;
		HSE_poll_res pres = 0;
		do {
			uint8_t cd[32];
			pres = heatshrink_encoder_poll(self->hse, cd, sizeof(cd), &poll_size);
			if (pres < 0) {
				return CRASH_MGR_RET_FAILED;
			}

			/* Output the result now, up to sizeof(cd) length. */
			cbor_encode_byte_string(&encoder_dump_data, cd, poll_size);
		} while (pres == HSER_POLL_MORE);
	}

	/* Now try to finish the compression. Call encoder finish until no more data is left. */
	HSE_finish_res fres = 0;
	while (true) {
		fres = heatshrink_encoder_finish(self->hse);
		if (fres == HSER_FINISH_MORE) {
			/* Poll for compressed data if there is any. */
			size_t poll_size = 0;
			HSE_poll_res pres = 0;
			do {
				uint8_t cd[32];
				pres = heatshrink_encoder_poll(self->hse, cd, sizeof(cd), &poll_size);
				if (pres < 0) {
					return CRASH_MGR_RET_FAILED;
				}
				cbor_encode_byte_string(&encoder_dump_data, cd, poll_size);
			} while (pres == HSER_POLL_MORE);
		} else {
			break;
		}
	}

	/* Now the region is fully compressed, close the indefinite byte string container now. */
	cbor_encoder_close_container(&encoder_dump, &encoder_dump_data);
	cbor_encoder_close_container(&self->encoder_map, &encoder_dump);

	return CRASH_MGR_RET_OK;
}


static crash_mgr_ret_t do_coredump(CrashMgr *self, TaskHandle_t task, enum crash_mgr_fault fault) {

	/** @todo dump registers */

	/* Dump user selected memory regions */
	if (self->memdump_regions != NULL) {
		for (const struct crash_mgr_region *r = self->memdump_regions; r->addr != NULL; r++) {
			do_memdump(self, r->addr, r->size);
		}
	}

	return CRASH_MGR_RET_OK;
}


static crash_mgr_ret_t finish_coredump(CrashMgr *self) {
	cbor_encoder_close_container(&self->encoder, &self->encoder_map);

	return CRASH_MGR_RET_OK;
}


static crash_mgr_ret_t save_coredump(CrashMgr *self) {
	size_t cbor_len = cbor_encoder_get_buffer_size(&self->encoder, self->work_buf);

	/* Now the state is properly saved in a volatile memory, save it to a file. */
	File f;
	if (self->coredump_fs->vmt->open(self->coredump_fs, &f, self->coredump_filename, FS_MODE_CREATE | FS_MODE_WRITEONLY | FS_MODE_TRUNCATE) != FS_RET_OK) {
		return CRASH_MGR_RET_FAILED;
	}
	size_t written = 0;
	self->coredump_fs->vmt->write(self->coredump_fs, &f, self->work_buf, cbor_len, &written);
	self->coredump_fs->vmt->close(self->coredump_fs, &f);

	if (written == cbor_len) {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("coredump saved in '%s', %lu bytes written"), self->coredump_filename, written);
		return CRASH_MGR_RET_OK;
	}
	return CRASH_MGR_RET_FAILED;
}


/* The handler task is run with the highest priority in the system. Once unblocked it doesn't
 * allow other tasks to run until it finishes with core/log dumps and returns back to dormant state. */
static void crash_mgr_handler_task(void *p) {
	CrashMgr *self = (CrashMgr *)p;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialised, dormant"));
	while (true) {
		vTaskSuspend(NULL);

		/* The task is resumed HERE right after a crash occurs. It is the highest priority task
		 * in the system and it is already scheduled to run. Do the following actions once
		 * and then suspend itself back. */

		/* Disable interrupts first -> stop all unscheduled background tasks and data processing.
		 * Coredump saving doesn't need interrupts working. */
		vPortEnterCritical();

		prepare_coredump(self);
		do_coredump(self, self->task, self->fault);
		finish_coredump(self);

		vTaskSuspend(self->task);
		vPortExitCritical();

		if (self->logdump_enabled) {
			do_logdump(self, self->task, self->fault);
		}
		if (self->coredump_fs != NULL) {
			save_coredump(self);
		}
	}
}


crash_mgr_ret_t crash_mgr_init(CrashMgr *self, size_t max_instance_size) {
	if (max_instance_size < sizeof(CrashMgr)) {
		/* Instance data doesn't fit even without the work buffer. */
		return CRASH_MGR_RET_FAILED;
	}
	/* Pain the instance + working buffer. */
	memset(self, 0x55, max_instance_size);
	self->work_buf_size = max_instance_size - sizeof(CrashMgr);

	self->logdump_enabled = true;

	/* Enable fault handlers */
	SCB_SHCSR |= SCB_SHCSR_USGFAULTENA | SCB_SHCSR_BUSFAULTENA | SCB_SHCSR_MEMFAULTENA;

	self->state = CRASH_MGR_STATE_READY;
	xTaskCreate(crash_mgr_handler_task, "crash-mgr", CRASH_MGR_TASK_STACK, self, configMAX_PRIORITIES - 1, &self->handler_task);

	return CRASH_MGR_RET_OK;
}


crash_mgr_ret_t crash_mgr_free(CrashMgr *self) {
	(void)self;
	return CRASH_MGR_RET_OK;
}


crash_mgr_ret_t crash_mgr_save_coredump(CrashMgr *self, Fs *fs, const char *filename) {
	self->coredump_fs = fs;
	self->coredump_filename = filename;

	return CRASH_MGR_RET_OK;
}


crash_mgr_ret_t crash_mgr_enable_memdump(CrashMgr *self, const struct crash_mgr_region *regions) {
	self->memdump_regions = regions;

	return CRASH_MGR_RET_OK;
}


#define crash_mgr_generic_handler(f) \
	/* Do not catch faults until the manager is fully initialised. */ \
	if (crash_mgr->state == CRASH_MGR_STATE_READY) { \
		/* Save the offending task. */ \
		crash_mgr->task = xTaskGetCurrentTaskHandle(); \
		crash_mgr->fault = f; \
		xTaskResumeFromISR(crash_mgr->handler_task); \
		portYIELD(); \
	} \


/* Fault handlers */
void hard_fault_handler(void)  { crash_mgr_generic_handler(CRASH_MGR_FAULT_HARD)  }
void mem_manage_handler(void)  { crash_mgr_generic_handler(CRASH_MGR_FAULT_MEM)   }
void bus_fault_handler(void)   { crash_mgr_generic_handler(CRASH_MGR_FAULT_BUS)   }
void usage_fault_handler(void) { crash_mgr_generic_handler(CRASH_MGR_FAULT_USAGE) }


/* Simulated faults */
crash_mgr_ret_t crash_mgr_invalid_instruction_fault(void) {
	invalid_function_t invalid = (invalid_function_t)0xE0000000;
	return invalid();
}

crash_mgr_ret_t crash_mgr_hard_fault(void) {
	if (*(uint32_t *)0x0 == 0x1) {
		return CRASH_MGR_RET_OK;
	}
	return CRASH_MGR_RET_FAILED;
}
