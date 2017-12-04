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
#include "statistics-node.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "dp-statistics"


static void dp_statistics_node_task(void *p) {
	struct dp_statistics_node *self = (struct dp_statistics_node *)p;

	self->running = true;
	while (self->can_run) {
		struct dp_data data_in;
		dp_input_read(&self->in, &data_in);

		/* Add the current value to the history. */
		self->history[self->history_pos] = data_in.content;
		self->history_pos = (self->history_pos + 1) % self->history_len;


		/* Compute floating min, max, avg. */
		switch (self->type) {
			case DP_DATA_TYPE_FLOAT: {
				/* Initialize. */
				float avg = 0.0;
				float min = 0.0;
				float max = 0.0;

				/* Compute. */
				for (uint32_t i = 0; i < self->history_len; i++) {
					avg += *self->history[i].dp_float;
					if (*data_in.content.dp_float < min) {
						min = *data_in.content.dp_float;
					}
					if (*data_in.content.dp_float > max) {
						max = *data_in.content.dp_float;
					}
				}
				avg /= (float)self->history_len;

				/* Prepare the output value. */
				struct dp_data data_out = {
					.len = 1,
					.flags = DP_DATA_FRAME_START | DP_DATA_FRAME_END,
					.type = self->type,
					/* Do not initialize content. */
				};

				/* Output computed values every self->history_len values. */
				if (self->history_pos == 0) {
					data_out.content.dp_float = &avg;
					dp_output_write(&self->out_avg, &data_out);

					data_out.content.dp_float = &min;
					dp_output_write(&self->out_min, &data_out);

					data_out.content.dp_float = &max;
					dp_output_write(&self->out_max, &data_out);
				}

				/* Output floating computed values every time a new value is read. */
				data_out.content.dp_float = &avg;
				dp_output_write(&self->out_avg_floating, &data_out);

				data_out.content.dp_float = &min;
				dp_output_write(&self->out_min_floating, &data_out);

				data_out.content.dp_float = &max;
				dp_output_write(&self->out_max_floating, &data_out);

				break;
			}
			default:
				/* Nothing. */
				break;
		}
	}
	self->running = false;

	vTaskDelete(NULL);
}


dp_ret_t dp_statistics_node_init(struct dp_statistics_node *self, const char *name, enum dp_data_type type) {
	if (u_assert(self != NULL) ||
	    u_assert(name != NULL)) {
		return DATA_PROCESS_RET_FAILED;
	}

	memset(self, 0, sizeof(struct dp_statistics_node));

	/* Default value. */
	self->history_len = 10;

	self->type = type;

	dp_output_init(&self->out_avg, type);
	dp_output_init(&self->out_min, type);
	dp_output_init(&self->out_max, type);
	dp_output_init(&self->out_avg_floating, type);
	dp_output_init(&self->out_min_floating, type);
	dp_output_init(&self->out_max_floating, type);
	dp_input_init(&self->in, type);
	self->initialized = true;

	return DATA_PROCESS_RET_OK;
}


dp_ret_t dp_statistics_node_free(struct dp_statistics_node *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->initialized == true)) {
		return DATA_PROCESS_RET_FAILED;
	}

	dp_output_free(&self->out_avg_floating);
	dp_output_free(&self->out_min_floating);
	dp_output_free(&self->out_max_floating);
	dp_input_free(&self->in);

	return DATA_PROCESS_RET_OK;
}


dp_ret_t dp_statistics_node_start(struct dp_statistics_node *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->initialized == true)) {
		return DATA_PROCESS_RET_FAILED;
	}

	/* Allocate the history buffer first. */
	self->history = calloc(sizeof(union dp_data_content), self->history_len);
	if (self->history == NULL) {
		return DATA_PROCESS_RET_FAILED;
	}
	self->history_pos = 0;

	self->can_run = true;
	xTaskCreate(dp_statistics_node_task, "dp-statistics", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &self->task);
	if (self->task == NULL) {
		return DATA_PROCESS_RET_FAILED;
	}

	return DATA_PROCESS_RET_OK;
}


dp_ret_t dp_statistics_node_stop(struct dp_statistics_node *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->running == true)) {
		return DATA_PROCESS_RET_FAILED;
	}

	self->can_run = false;
	while (self->running) {
		vTaskDelay(1);
	}

	free(self->history);

	return DATA_PROCESS_RET_OK;
}

