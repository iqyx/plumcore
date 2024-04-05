/* SPDX-License-Identifier: GPL-3.0-or-later
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
#include <libopencm3/stm32/iwdg.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>

#include <cbor.h>
#include "backtrace.h"

#include "crash-mgr.h"

#define MODULE_NAME "crash-mgr"


/* Crash manager is a singleton service, a special one. There is exactly one
 * instance of the crash manager if there is any at all. */
CrashMgr *crash_mgr;

static const char *fault_str[] = {"NMI", "HARD", "BUS", "MEM", "USAGE"};


/***********************************************************************************************************************
 * Core dump functionality
 **********************************************************************************************************************/

crash_mgr_ret_t coredump_init(CoreDump *self, uint8_t *buf, size_t len) {
	if (u_assert(self != NULL) ||
	    u_assert(buf != NULL) ||
	    u_assert(len > 0)) {
		return CRASH_MGR_RET_FAILED;
	}
	memset(self, 0, sizeof(CoreDump));

	self->work_buf = buf;
	self->work_buf_size = len;

	/* Write the header. CBOR encoded core dump versions are simple unsigned integers. */
	cbor_encoder_init(&self->encoder, self->work_buf, self->work_buf_size, 0);
	cbor_encoder_create_map(&self->encoder, &self->encoder_map, CborIndefiniteLength);
	cbor_encode_text_stringz(&self->encoder_map, "cbcd");
	cbor_encode_int(&self->encoder_map, COREDUMP_VERSION);

	/* We are using a compressor for memory dumps. Save compression config as it is needed for decompression.
	 * As of now, Heathshrink library is used. */
	cbor_encode_text_stringz(&self->encoder_map, "comp");
	cbor_encode_text_stringz(&self->encoder_map, "hs");
	cbor_encode_text_stringz(&self->encoder_map, "hs-wsz2");
	cbor_encode_int(&self->encoder_map, CONFIG_CRASH_MGR_COMP_HS_WINDOW_BITS);
	cbor_encode_text_stringz(&self->encoder_map, "hs-lsz2");
	cbor_encode_int(&self->encoder_map, CONFIG_CRASH_MGR_COMP_HS_LOOKAHEAD_BITS);

	/* Now we are free to add key-value pairs using the encoder_map encoder. */
	return CRASH_MGR_RET_OK;
}


crash_mgr_ret_t coredump_finish(CoreDump *self) {
	cbor_encoder_close_container(&self->encoder, &self->encoder_map);

	return CRASH_MGR_RET_OK;
}


crash_mgr_ret_t coredump_save(CoreDump *self, Fs *fs, const char *filename) {
	if (u_assert(self != NULL) ||
	    u_assert(fs != NULL) ||
	    u_assert(filename != NULL)) {
		return CRASH_MGR_RET_FAILED;
	}

	size_t extra = cbor_encoder_get_extra_bytes_needed(&self->encoder);
	if (extra > 0) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("work buffer not big enough, core dump is saved truncated (%lu B extra needed)"), extra);
		/* Continue anyway */
	}
	size_t cbor_len = cbor_encoder_get_buffer_size(&self->encoder, self->work_buf);

	File f;
	if (fs->vmt->open(fs, &f, filename, FS_MODE_CREATE | FS_MODE_WRITEONLY | FS_MODE_TRUNCATE) != FS_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot open '%s' for writing"), filename);
		return CRASH_MGR_RET_FAILED;
	}
	size_t written = 0;
	fs->vmt->write(fs, &f, self->work_buf, cbor_len, &written);
	/* SPIFFS workaround, writing sometimes fail. */
	if (written == 0) {
		fs->vmt->write(fs, &f, self->work_buf, cbor_len, &written);
	}
	fs->vmt->close(fs, &f);

	if (written == cbor_len) {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("coredump saved in '%s', %lu bytes written"), filename, written);
		return CRASH_MGR_RET_OK;
	}

	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("coredump saving error"));
	return CRASH_MGR_RET_FAILED;
}


crash_mgr_ret_t coredump_mem(CoreDump *self, uint8_t *buf, size_t size) {
	/* Prepare the encoder (indefinite byte string) and save memory region metadata. */
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
	self->hse->search_index->size = (2 << CONFIG_CRASH_MGR_COMP_HS_WINDOW_BITS) * sizeof(uint16_t);
	self->hse->window_sz2 = CONFIG_CRASH_MGR_COMP_HS_WINDOW_BITS;
	self->hse->lookahead_sz2 = CONFIG_CRASH_MGR_COMP_HS_LOOKAHEAD_BITS;
	heatshrink_encoder_reset(self->hse);

	/* Process the whole region */
	while (size) {
		/* Reset the watchdog. */
		/** @todo remove STM32 dependency */
		IWDG_KR = IWDG_KR_RESET;

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


crash_mgr_ret_t coredump_task_list(CoreDump *self) {
	(void)self;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("dumping task list"));
	/** @todo Iterate over all tasks/threads and get their TCB addresses and names. */

	return CRASH_MGR_RET_OK;
}


crash_mgr_ret_t coredump_fault_info(CoreDump *self, TaskHandle_t task, enum crash_mgr_fault fault) {
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("dumping basic fault info"));

	cbor_encode_text_stringz(&self->encoder_map, "fault");
	cbor_encode_text_stringz(&self->encoder_map, fault_str[fault]);

	TaskStatus_t task_status = {0};
	vTaskGetInfo(task, &task_status, pdFALSE, eReady);
	cbor_encode_text_stringz(&self->encoder_map, "thread");
	cbor_encode_text_stringz(&self->encoder_map, task_status.pcTaskName);

	cbor_encode_text_stringz(&self->encoder_map, "thread-tcb");
	cbor_encode_uint(&self->encoder_map, (uint32_t)task);

	return CRASH_MGR_RET_OK;
}


crash_mgr_ret_t coredump_registers(CoreDump *self, TaskHandle_t task, enum crash_mgr_fault fault) {
	(void)self;
	(void)fault;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("dumping registers"));

	cbor_encode_text_stringz(&self->encoder_map, "registers");
	CborEncoder encoder_reg;
	cbor_encoder_create_map(&self->encoder_map, &encoder_reg, CborIndefiniteLength);

	uint32_t *sp = *(uint32_t **)task;
	cbor_encode_text_stringz(&encoder_reg, "sp");
	cbor_encode_uint(&encoder_reg, (uint32_t)sp);
	cbor_encode_text_stringz(&encoder_reg, "xPSR");
	cbor_encode_uint(&encoder_reg, sp[16]);
	cbor_encode_text_stringz(&encoder_reg, "PC");
	cbor_encode_uint(&encoder_reg, sp[15]);
	cbor_encode_text_stringz(&encoder_reg, "LR");
	cbor_encode_uint(&encoder_reg, sp[14]);
	cbor_encode_text_stringz(&encoder_reg, "R12");
	cbor_encode_uint(&encoder_reg, sp[13]);

	cbor_encode_text_stringz(&encoder_reg, "CFSR");
	cbor_encode_uint(&encoder_reg, SCB_CFSR);
	cbor_encode_text_stringz(&encoder_reg, "HFSR");
	cbor_encode_uint(&encoder_reg, SCB_HFSR);
	cbor_encode_text_stringz(&encoder_reg, "BFAR");
	cbor_encode_uint(&encoder_reg, SCB_BFAR);

	cbor_encoder_close_container(&self->encoder_map, &encoder_reg);

	return CRASH_MGR_RET_OK;
}


crash_mgr_ret_t coredump_log(CoreDump *self) {
	(void)self;

	u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("dumping log buffer content not implemented"));
	/** @todo Dump the log ring buffer */

	return CRASH_MGR_RET_OK;
}


crash_mgr_ret_t coredump_backtrace(CoreDump *self, backtrace_t *bt, size_t bt_len) {
	(void)self;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("dumping backtrace"));
	for (uint32_t i = 0; i < bt_len; i++) {
		if (i > 0 && bt[i].address == bt[i - 1].address) {
			break;
		}
		/** @todo Dump the backtrace here */
	}

	return CRASH_MGR_RET_OK;
}


/***********************************************************************************************************************
 * Fault logging functionality
 **********************************************************************************************************************/


static crash_mgr_ret_t crash_mgr_log_info(CrashMgr *self, TaskHandle_t task, enum crash_mgr_fault fault) {
	(void)self;

	/* The first member MUST be the current stack pointer (top of stack). */
	uint32_t *sp = *(uint32_t **)task;

	TaskStatus_t task_status = {0};
	vTaskGetInfo(task, &task_status, pdFALSE, eReady);

	u_log(system_log, LOG_TYPE_CRIT, U_LOG_MODULE_PREFIX("\x1b[33m--------------------- surprised pikachu in '%s', %s fault ---------------------\x1b[0m"), task_status.pcTaskName, fault_str[fault]);
	u_log(system_log, LOG_TYPE_CRIT, U_LOG_MODULE_PREFIX("SP = %0p, TCB = %0p, xPSR = %0p, PC = %0p, LR = %0p"), sp, task, sp[16], sp[15], sp[14]);
	u_log(system_log, LOG_TYPE_CRIT, U_LOG_MODULE_PREFIX("CSFR = %0p, HFSR = %0p, BFAR = %0p"), SCB_CFSR, SCB_HFSR, SCB_BFAR);

	return CRASH_MGR_RET_OK;
}


static crash_mgr_ret_t crash_mgr_log_registers(CrashMgr *self, TaskHandle_t task, enum crash_mgr_fault fault) {
	(void)self;
	(void)fault;

	uint32_t *sp = *(uint32_t **)task;

	u_log(system_log, LOG_TYPE_CRIT, U_LOG_MODULE_PREFIX("\x1b[33m------------------------- registers -------------------------\x1b[0m"));
	u_log(system_log, LOG_TYPE_CRIT, U_LOG_MODULE_PREFIX("R3 = %0p, R2 = %0p, R1 = %0p, R0 = %0p"), sp[12], sp[11], sp[10], sp[9]);
	u_log(system_log, LOG_TYPE_CRIT, U_LOG_MODULE_PREFIX("EXC_RETURN = %0p"), sp[8]);
	u_log(system_log, LOG_TYPE_CRIT, U_LOG_MODULE_PREFIX("R11 = %0p, R10 = %0p, R9 = %0p, R8 = %0p"), sp[7], sp[6], sp[5], sp[4]);
	u_log(system_log, LOG_TYPE_CRIT, U_LOG_MODULE_PREFIX("R7 = %0p, R6 = %0p, R5 = %0p, R4 = %0p"), sp[3], sp[2], sp[1], sp[0]);

	return CRASH_MGR_RET_OK;
}


static crash_mgr_ret_t crash_mgr_log_backtrace(CrashMgr *self) {
	u_log(system_log, LOG_TYPE_CRIT, U_LOG_MODULE_PREFIX("\x1b[33m------------------------- backtrace -------------------------\x1b[0m"));
	for (uint32_t i = 0; i < self->bt_len; i++) {
		u_log(system_log, LOG_TYPE_CRIT, U_LOG_MODULE_PREFIX("#%u %p in %s@%p"), i, self->bt_buf[i].address, self->bt_buf[i].name, self->bt_buf[i].function);
	}

	return CRASH_MGR_RET_OK;
}


static void crash_mgr_trim_backtrace(CrashMgr *self) {
	for (uint32_t i = 0; i < self->bt_len; i++) {
		if (i > 0 && self->bt_buf[i].address == self->bt_buf[i - 1].address) {
			self->bt_len = i;
			break;
		}
	}
}


static void crash_mgr_reboot(CrashMgr *self) {
	(void)self;
	/** @todo prepare for reboot here */

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("rebooting as requested --------->\n\n"));

	/* Try to delay a bit to finish logging. This should be handled by a power manager service in the future. */
	vTaskDelay(1000);
	SCB_AIRCR = 0x05FA0004;

	/* Loop in the high priority handler task forewer. If reboot fails for any reason, watchdog manages it. */
	while (true) {
		;
	}
}


/***********************************************************************************************************************
 * Handler task
 *
 * The handler task is run with the highest priority in the system. Once unblocked it doesn't allow other tasks
 * to run until it finishes with core/log dumps and returns back to the dormant state.
 **********************************************************************************************************************/

static void crash_mgr_handler_task(void *p) {
	CrashMgr *self = (CrashMgr *)p;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialised, dormant"));
	while (true) {
		/* We are fully prepared to catch faults. Enable their processing and wait for a crash. */
		self->state = CRASH_MGR_STATE_READY;
		vTaskSuspend(NULL);
		/* The task is resumed HERE right after a crash occurs. It is the highest priority task
		 * in the system and it is already scheduled to run. Do the following actions once
		 * and then suspend itself back. */

		/* Disable interrupts first -> stop all unscheduled background tasks and data processing.
		 * Coredump saving doesn't need interrupts working. Reenable interrupts back.
		 * The failed task is kept suspended. */
		vPortEnterCritical();

		coredump_init(&self->coredump, self->work_buf, self->work_buf_size);
		/* Dump user selected memory regions */
		if (self->memdump_regions != NULL) {
			for (const struct crash_mgr_region *r = self->memdump_regions; r->addr != NULL; r++) {
				coredump_mem(&self->coredump, r->addr, r->size);
			}
		}

		vTaskSuspend(self->task);
		vPortExitCritical();

		crash_mgr_trim_backtrace(self);

		/* We may do a bit of logging when out of the critical section. */
		crash_mgr_log_info(self, self->task, self->fault);
		#if defined(CONFIG_CRASH_MGR_LOG_REGISTERS)
			crash_mgr_log_registers(self, self->task, self->fault);
		#endif
		#if defined(CONFIG_CRASH_MGR_LOG_BACKTRACE)
			crash_mgr_log_backtrace(self);
		#endif

		/* As an alternative to log output, dump info to the coredump too. */
		coredump_fault_info(&self->coredump, self->task, self->fault);
		#if defined(CONFIG_CRASH_MGR_DUMP_REGISTERS)
			coredump_registers(&self->coredump, self->task, self->fault);
		#endif
		#if defined(CONFIG_CRASH_MGR_DUMP_BACKTRACE)
			coredump_backtrace(&self->coredump, self->bt_buf, self->bt_len);
		#endif

		coredump_finish(&self->coredump);
		if (self->coredump_fs != NULL) {
			coredump_save(&self->coredump, self->coredump_fs, self->coredump_filename);
		}

		#if defined(CONFIG_CRASH_MGR_REBOOT)
			crash_mgr_reboot(self);
		#endif
	}
}


crash_mgr_ret_t crash_mgr_init(CrashMgr *self, size_t max_instance_size) {
	if (max_instance_size < sizeof(CrashMgr)) {
		/* Instance data doesn't fit even without the work buffer. */
		return CRASH_MGR_RET_FAILED;
	}
	/* Paint the instance + working buffer. */
	memset(self, 0x55, max_instance_size);
	self->work_buf_size = max_instance_size - sizeof(CrashMgr);

	/* Enable fault handlers */
	SCB_SHCSR |= SCB_SHCSR_USGFAULTENA | SCB_SHCSR_BUSFAULTENA | SCB_SHCSR_MEMFAULTENA;

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
		crash_mgr->bt_len = backtrace_unwind(crash_mgr->bt_buf, CRASH_MGR_BACKTRACE_SIZE); \
		xTaskResumeFromISR(crash_mgr->handler_task); \
		portYIELD(); \
	} \


/* Fault handlers */
void nmi_handler(void)         {
	/* NMI is (among others) caused by the clock security system. Clear the flag to avoid
	 * calling the handler in a loop. */
	RCC_CICR |= RCC_CICR_CSSC; \
	crash_mgr_generic_handler(CRASH_MGR_FAULT_NMI)
}
void hard_fault_handler(void)  { crash_mgr_generic_handler(CRASH_MGR_FAULT_HARD)  }
void mem_manage_handler(void)  { crash_mgr_generic_handler(CRASH_MGR_FAULT_MEM)   }
void bus_fault_handler(void)   { crash_mgr_generic_handler(CRASH_MGR_FAULT_BUS)   }
void usage_fault_handler(void) { crash_mgr_generic_handler(CRASH_MGR_FAULT_USAGE) }


/***********************************************************************************************************************
 * Functions to simulate common faults
 **********************************************************************************************************************/

/** @todo Move to a dedicated crash/fault test suite (run as an applet). */

crash_mgr_ret_t crash_mgr_invalid_instruction_fault(void) {
	uint32_t invalid_instruction = 0xe0000000UL;
	invalid_function_t invalid = (invalid_function_t)((uint32_t)&invalid_instruction | 1UL);
	u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("executing instruction at %0p"), (uint32_t)invalid);
	return (crash_mgr_ret_t)invalid();
}


crash_mgr_ret_t crash_mgr_hard_fault(void) {
	if (*(uint32_t *)0x0 == 0x1) {
		return CRASH_MGR_RET_OK;
	}
	return CRASH_MGR_RET_FAILED;
}


crash_mgr_ret_t crash_mgr_nmi(void) {
	SCB_ICSR |= SCB_ICSR_NMIPENDSET;
	return CRASH_MGR_RET_FAILED;
}



