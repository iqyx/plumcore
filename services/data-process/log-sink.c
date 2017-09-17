/*
 * Copyright (c) 2017, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "stdint.h"
#include "stdbool.h"
#include "stdlib.h"

#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"

#include "data-process.h"
#include "log-sink.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "dp-log-sink"


static void dp_log_sink_task(void *p) {
	struct dp_log_sink *self = (struct dp_log_sink *)p;

	self->running = true;
	while (self->can_run) {
		struct dp_data data;
		dp_input_read(&self->in, &data);
		if (u_assert(data.type == self->type)) {
			/* Wrong data, stop the node. */
			u_log(system_log, LOG_TYPE_ERROR, "wrong data type on the input, stopping");
			break;
		}
		if (data.type == DP_DATA_TYPE_FLOAT) {
			u_log(system_log, LOG_TYPE_INFO, "%d.%03d", (int)(*data.content.dp_float), (int)(*data.content.dp_float * 1000.0));
			continue;
		}

		/* Unsupported data type, stop the node. */
		u_log(system_log, LOG_TYPE_ERROR, "unsupported data type, stopping");
		break;
	}
	self->running = false;

	vTaskDelete(NULL);
}


static struct dp_input *get_input_by_name(const char *name, void *context) {
	if (name == NULL) {
		return NULL;
	}

	struct dp_log_sink *self = (struct dp_log_sink *)context;
	if (!strcmp(name, "in") || !strcmp(name, "default")) {
		return &(self->in);
	}

	return NULL;
}


static struct dp_output *get_output_by_name(const char *name, void *context) {
	(void)name;
	(void)context;

	/* No outputs, always return NULL. */
	return NULL;
}


dp_ret_t dp_log_sink_init(struct dp_log_sink *self, enum dp_data_type type) {
	if (u_assert(self != NULL)) {
		return DATA_PROCESS_RET_FAILED;
	}

	memset(self, 0, sizeof(struct dp_log_sink));

	self->type = type;
	dp_input_init(&self->in, self->type);
	self->descriptor.get_input_by_name = get_input_by_name;
	self->descriptor.get_output_by_name = get_output_by_name;
	self->descriptor.context = (void *)self;

	self->initialized = true;

	return DATA_PROCESS_RET_OK;
}


dp_ret_t dp_log_sink_free(struct dp_log_sink *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->initialized == true)) {
		return DATA_PROCESS_RET_FAILED;
	}

	dp_input_free(&self->in);

	return DATA_PROCESS_RET_OK;
}


dp_ret_t dp_log_sink_start(struct dp_log_sink *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->initialized == true)) {
		return DATA_PROCESS_RET_FAILED;
	}

	self->can_run = true;
	xTaskCreate(dp_log_sink_task, "dp-log-sink", configMINIMAL_STACK_SIZE + 128, (void *)self, 1, &self->task);
	if (self->task == NULL) {
		return DATA_PROCESS_RET_FAILED;
	}

	return DATA_PROCESS_RET_OK;
}


dp_ret_t dp_log_sink_stop(struct dp_log_sink *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->running == true)) {
		return DATA_PROCESS_RET_FAILED;
	}

	self->can_run = false;
	while (self->running) {
		vTaskDelay(1);
	}

	return DATA_PROCESS_RET_OK;
}

