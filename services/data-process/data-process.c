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
#include "semphr.h"
#include "u_assert.h"
#include "u_log.h"

#include "data-process.h"

/* There is a single flow graph in the system (now). */
struct dp_graph data_process_graph;

const char *dp_node_type_str[] = {
	"constant-source",
	"sensor-source",
	"statistics",
	"log-sink",
};


dp_ret_t dp_graph_init(struct dp_graph *self) {
	if (u_assert(self != NULL)) {
		return DATA_PROCESS_RET_FAILED;
	}

	memset(self, 0, sizeof(struct dp_graph));

	self->initialized = true;
	return DATA_PROCESS_RET_OK;
}


dp_ret_t dp_graph_free(struct dp_graph *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->initialized = true)) {
		return DATA_PROCESS_RET_FAILED;
	}

	self->initialized = false;

	return DATA_PROCESS_RET_OK;
}


dp_ret_t dp_graph_add_node(struct dp_graph *self, void *node, enum dp_node_type type, const char *name) {
	if (u_assert(self != NULL) ||
	    u_assert(self->initialized = true)) {
		return DATA_PROCESS_RET_FAILED;
	}

	/* Allocate and construct the new node. */
	struct dp_graph_node *new_node = calloc(sizeof(struct dp_graph_node), 1);
	if (new_node == NULL) {
		return DATA_PROCESS_RET_FAILED;
	}
	new_node->type = type;
	new_node->node = node;
	new_node->next = NULL;
	strcpy(new_node->name, name);

	/* Find the last node and insert the new one on that position. */
	if (self->nodes == NULL) {
		self->nodes = new_node;
	} else {
		struct dp_graph_node *cur = self->nodes;
		while (cur->next != NULL) {
			cur = cur->next;
		}
		cur->next = new_node;
	}

	return DATA_PROCESS_RET_OK;
}


dp_ret_t dp_graph_remove_node(struct dp_graph *self, void *node, const char *name) {
	if (u_assert(self != NULL) ||
	    u_assert(self->initialized = true)) {
		return DATA_PROCESS_RET_FAILED;
	}

	/* If the node requested for removal is the only node in the list,
	 * delete it and set the list pointer to NULL. */
	if (self->nodes->next == NULL && (self->nodes->node == node || (name != NULL && (strcmp(name, self->nodes->name) != 0)))) {
		free(self->nodes);
		self->nodes = NULL;
		return DATA_PROCESS_RET_OK;
	}

	/* Otherwise try to find the corresponding node and remove it. */
	struct dp_graph_node *cur = self->nodes;
	while (cur->next != NULL) {
		if (cur->next->node == node || (name != NULL && (strcmp(name, cur->next->name) != 0))) {
			struct dp_graph_node *tmp = cur->next->next;
			free(cur->next);
			cur->next = tmp;
			return DATA_PROCESS_RET_OK;
		}
	}

	return DATA_PROCESS_RET_FAILED;
}


dp_ret_t dp_input_init(struct dp_input *self, enum dp_data_type type) {
	if (u_assert(self != NULL)) {
		return DATA_PROCESS_RET_FAILED;
	}

	memset(self, 0, sizeof(struct dp_input));
	self->type = type;

	self->data_can_read = xSemaphoreCreateBinary();
	if (self->data_can_read == NULL) {
		dp_input_free(self);
		return DATA_PROCESS_RET_FAILED;
	}
	self->data_has_read = xSemaphoreCreateBinary();
	if (self->data_has_read == NULL) {
		dp_input_free(self);
		return DATA_PROCESS_RET_FAILED;
	}

	self->initialized = true;

	return DATA_PROCESS_RET_OK;
}


dp_ret_t dp_input_free(struct dp_input *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->initialized == true)) {
		return DATA_PROCESS_RET_FAILED;
	}

	if (self->data_can_read != NULL) {
		vSemaphoreDelete(self->data_can_read);
	}
	if (self->data_has_read != NULL) {
		vSemaphoreDelete(self->data_has_read);
	}

	self->initialized = false;

	return DATA_PROCESS_RET_OK;
}


dp_ret_t dp_input_is_connected(struct dp_input *self) {
	if (u_assert(self != NULL)) {
		return DATA_PROCESS_RET_FAILED;
	}

	return DATA_PROCESS_RET_OK;
}


dp_ret_t dp_connect_input_to_output(struct dp_input *self, struct dp_output *output) {
	if (u_assert(self != NULL) ||
	    u_assert(output != NULL)) {
		return DATA_PROCESS_RET_FAILED;
	}

	/* Data types of both the input and the output must be the same. */
	if (u_assert(self->type == output->type)) {
		return DATA_PROCESS_RET_FAILED;
	}

	for (size_t i = 0; i < DATA_PROCESS_MAX_INPUTS; i++) {
		if (output->inputs[i] == NULL) {
			output->inputs[i] = self;
			self->output = output;
			return DATA_PROCESS_RET_OK;
		}
	}

	/* No free space found. */
	return DATA_PROCESS_RET_FAILED;
}


dp_ret_t dp_input_read(struct dp_input *self, struct dp_data *data) {
	if (u_assert(self != NULL) ||
	    u_assert(data != NULL)) {
		return DATA_PROCESS_RET_FAILED;
	}

	/* Wait for the data to appear on the input first. */
	xSemaphoreTake(self->data_can_read, portMAX_DELAY);

	memcpy(data, self->data, sizeof(struct dp_data));

	/* And signalize the output that the data is successfully read. */
	xSemaphoreGive(self->data_has_read);

	return DATA_PROCESS_RET_OK;
}


dp_ret_t dp_output_init(struct dp_output *self, enum dp_data_type type) {
	if (u_assert(self != NULL)) {
		return DATA_PROCESS_RET_FAILED;
	}

	memset(self, 0, sizeof(struct dp_output));
	self->type = type;

	self->initialized = true;

	return DATA_PROCESS_RET_OK;
}


dp_ret_t dp_output_free(struct dp_output *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->initialized == true)) {
		return DATA_PROCESS_RET_FAILED;
	}

	self->initialized = false;

	return DATA_PROCESS_RET_OK;
}


bool dp_output_is_connected(struct dp_output *self) {
	if (u_assert(self != NULL)) {
		return false;
	}

	for (size_t i = 0; i < DATA_PROCESS_MAX_INPUTS; i++) {
		if (self->inputs != NULL) {
			return true;
		}
	}

	return false;
}


dp_ret_t dp_output_write(struct dp_output *self, struct dp_data *data) {
	if (u_assert(self != NULL) ||
	    u_assert(data != NULL)) {
		return DATA_PROCESS_RET_FAILED;
	}

	for (size_t i = 0; i < DATA_PROCESS_MAX_INPUTS; i++) {
		/* Write data only to connected inputs. */
		if (self->inputs[i] != NULL) {
			/* Check if the data types are the same. */
			if (u_assert(data->type == self->type && data->type == self->inputs[i]->type)) {
				return DATA_PROCESS_RET_FAILED;
			}

			/* Overwrite the shared pointer to the data being sent. */
			self->inputs[i]->data = data;

			/* Signalize the input to read the data. */
			xSemaphoreGive(self->inputs[i]->data_can_read);

			/* And wait until the data is read. */
			xSemaphoreTake(self->inputs[i]->data_has_read, portMAX_DELAY);
		}
	}

	return DATA_PROCESS_RET_OK;
}


