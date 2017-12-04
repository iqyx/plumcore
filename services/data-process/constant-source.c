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
#include "constant-source.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "constant-source"


static void constant_source_task(void *p) {
	struct dp_constant_source *self = (struct dp_constant_source *)p;

	self->running = true;
	while (self->can_run) {
		if (dp_output_is_connected(&self->out)) {
			struct dp_data data = {
				.len = 1,
				.flags = DP_DATA_NONE,
				.type = DP_DATA_TYPE_BYTE,
				.content.dp_bype = (uint8_t *)"abcd",
			};
			dp_output_write(&self->out, &data);
		}

		vTaskDelay(2000);
	}
	self->running = false;

	vTaskDelete(NULL);
}


static void test_task(void *p) {
	struct dp_constant_source *self = (struct dp_constant_source *)p;

	while (1) {
		struct dp_data data;
		dp_input_read(&self->in, &data);
		u_log(system_log, LOG_TYPE_DEBUG, "read %d", (int32_t)(*data.content.dp_float * 1000.0));
	}
	vTaskDelete(NULL);
}


dp_ret_t constant_source_init(struct dp_constant_source *self, enum constant_source_type type) {
	if (u_assert(self != NULL)) {
		return DATA_PROCESS_RET_FAILED;
	}

	self->type = type;
	dp_output_init(&self->out, DP_DATA_TYPE_BYTE);
	dp_input_init(&self->in, DP_DATA_TYPE_FLOAT);
	self->initialized = true;

	return DATA_PROCESS_RET_OK;
}


dp_ret_t constant_source_free(struct dp_constant_source *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->initialized == true)) {
		return DATA_PROCESS_RET_FAILED;
	}

	dp_output_free(&self->out);

	return DATA_PROCESS_RET_OK;
}


dp_ret_t constant_source_start(struct dp_constant_source *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->initialized == true)) {
		return DATA_PROCESS_RET_FAILED;
	}

	self->can_run = true;
	xTaskCreate(constant_source_task, "constant-source", configMINIMAL_STACK_SIZE + 128, (void *)self, 1, &self->task);
	xTaskCreate(test_task, "cs-read-test", configMINIMAL_STACK_SIZE + 128, (void *)self, 1, NULL);
	if (self->task == NULL) {
		return DATA_PROCESS_RET_FAILED;
	}

	return DATA_PROCESS_RET_OK;
}


dp_ret_t constant_source_stop(struct dp_constant_source *self) {
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

