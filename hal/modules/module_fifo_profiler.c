/**
 * uMeshFw FIFO stream output time profiler
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

#include <libopencm3/stm32/timer.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "u_assert.h"
#include "u_log.h"
#include "interface_stream.h"
#include "interface_profiling.h"
#include "module_fifo_profiler.h"


#define MODULE_FIFO_PROFILER_MESSAGE_PRINT_OK 0
#define MODULE_FIFO_PROFILER_MESSAGE_PRINT_FAILED -1
static int32_t module_fifo_profiler_message_print(struct module_fifo_profiler_msg *msg, struct interface_stream *stream) {

	char s[25];

	snprintf(s, sizeof(s), "[e%02x%02x,%x,%d]", msg->module, msg->message, (unsigned int)msg->time, msg->param);
	interface_stream_write(stream, (const uint8_t *)s, strlen(s));

	return MODULE_FIFO_PROFILER_MESSAGE_PRINT_OK;
}


static void module_fifo_profiler_task(void *p) {
	if (u_assert(p != NULL)) {
		vTaskDelete(NULL);
	}
	struct module_fifo_profiler *profiler = (struct module_fifo_profiler *)p;

	while (1) {
		struct module_fifo_profiler_msg msg;
		if (xQueueReceive(profiler->queue, &msg, portMAX_DELAY) == pdTRUE) {
			module_fifo_profiler_message_print(&msg, profiler->stream);
		}
	}
	vTaskDelete(NULL);
}


static int32_t module_fifo_profiler_event(void *context, uint8_t module, uint8_t message, int16_t param) {
	if (u_assert(context != NULL)) {
		return INTERFACE_PROFILING_EVENT_FAILED;
	}
	struct module_fifo_profiler *profiler = (struct module_fifo_profiler *)context;

	struct module_fifo_profiler_msg msg;
	msg.module = module;
	msg.message = message;
	/** @todo replace direct libopencm3 timer calls with hrtimer interface
	 *        calls. */
	msg.time = timer_get_counter(profiler->timer);
	msg.param = param;

	if (xQueueSendToBack(profiler->queue, &msg, 0) == pdFALSE) {
		return INTERFACE_PROFILING_EVENT_FAILED;
	}

	return INTERFACE_PROFILING_EVENT_OK;
}


int32_t module_fifo_profiler_init(struct module_fifo_profiler *profiler, const char *name, uint32_t timer, uint32_t length) {
	if (u_assert(profiler != NULL)) {
		return MODULE_FIFO_PROFILER_INIT_FAILED;
	}

	memset(profiler, 0, sizeof(struct module_fifo_profiler));

	/* Initialize the profiling interface. */
	interface_profiling_init(&(profiler->iface));
	profiler->iface.vmt.context = (void *)profiler;
	profiler->iface.vmt.event = module_fifo_profiler_event;

	profiler->timer = timer;
	profiler->queue = xQueueCreate(length, sizeof(struct module_fifo_profiler_msg));
	if (profiler->queue == NULL) {
		goto err;
	}

	u_log(system_log, LOG_TYPE_INFO, "FIFO time profiler initialized, queue length %u", length);
	return MODULE_FIFO_PROFILER_INIT_OK;
err:
	module_fifo_profiler_free(profiler);
	u_log(system_log, LOG_TYPE_ERROR, "FIFO time profiler failed to initialize");
	return MODULE_FIFO_PROFILER_INIT_FAILED;
}


int32_t module_fifo_profiler_free(struct module_fifo_profiler *profiler) {
	if (u_assert(profiler != NULL)) {
		return MODULE_FIFO_PROFILER_FREE_FAILED;
	}

	if (profiler->queue != NULL) {
		vQueueDelete(profiler->queue);
		profiler->queue = NULL;
	}

	return MODULE_FIFO_PROFILER_FREE_OK;
}


int32_t module_fifo_profiler_stream_enable(struct module_fifo_profiler *profiler, struct interface_stream *stream) {
	if (u_assert(profiler != NULL && stream != NULL)) {
		return MODULE_FIFO_PROFILER_STREAM_ENABLE_FAILED;
	}

	profiler->stream = stream;
	profiler->task = NULL;
	xTaskCreate(module_fifo_profiler_task, "profiler_task", configMINIMAL_STACK_SIZE + 96, (void *)profiler, 1, &(profiler->task));

	if (profiler->task == NULL) {
		return MODULE_FIFO_PROFILER_STREAM_ENABLE_FAILED;
	}

	return MODULE_FIFO_PROFILER_STREAM_ENABLE_OK;
}


int32_t module_fifo_profiler_stream_disable(struct module_fifo_profiler *profiler) {
	if (u_assert(profiler != NULL)) {
		return MODULE_FIFO_PROFILER_STREAM_DISABLE_FAILED;
	}

	if (profiler->task != NULL) {
		vTaskDelete(profiler->task);
	}

	return MODULE_FIFO_PROFILER_STREAM_DISABLE_OK;
}




