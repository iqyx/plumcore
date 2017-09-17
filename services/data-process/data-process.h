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

/**
 * A framework for processing data using flow graphs
 *
 * A flow graph consists of multiple nodes. Each node may have any number of inputs
 * and any number of outputs. Nodes with no inputs are called sources (they provide
 * some data), nodes with no outputs are called sinks (they only accept data). Each
 * node should have it's own task to process the data. Each input must be connected
 * to the corresponding output. Outputs can be left unconnected or can be connected
 * to multiple inputs (data is being copied in this case).
 */

/**
 * @todo
 *   - statistics node computing average, min, max (+ other) from a value backlog
 *     of a configurable size
 *   - tcp upload sink
 *   - sysem log sink
 *   - flash memory circular FIFO buffer (to overcome connectivity problems)
 *   - raw flash memory logging
 *   - simple graph sink
 */

#pragma once

#include "stdint.h"
#include "stdbool.h"
#include "stdlib.h"

/* Maximum number of connected inputs for a single output. */
#define DATA_PROCESS_MAX_INPUTS 4

#define DP_NODE_NAME_MAX_LEN 16

typedef enum {
	DATA_PROCESS_RET_OK = 0,
	DATA_PROCESS_RET_FAILED,
} dp_ret_t;

enum dp_data_flags {
	/* The data is not marked with any of the flags below. */
	DP_DATA_NONE = 0,
	/* A new data frame is started now. */
	DP_DATA_FRAME_START = 1,
	/* This piece of data is the last in the current frame. */
	DP_DATA_FRAME_END = 2,
};

/* Content of the data piece passed between outputs and inputs. */
union dp_data_content {
	uint32_t *dp_uint32;
	int32_t *dp_int32;
	float *dp_float;
	uint8_t *dp_bype;
};

/* Type of inputs, outputs and processed data. */
enum dp_data_type {
	DP_DATA_TYPE_UINT32,
	DP_DATA_TYPE_INT32,
	DP_DATA_TYPE_FLOAT,
	DP_DATA_TYPE_BYTE,
};

/* The data piece itself. */
struct dp_data {
	/** @todo time */
	union dp_data_content content;
	size_t len;
	enum dp_data_flags flags;
	enum dp_data_type type;
};

/* Forward declaration. */
struct dp_output;
struct dp_input;

struct dp_input {
	bool initialized;

	/* Reference to the connected output. NULL if disconnected. */
	struct dp_output *output;

	/* Shared buffer for data to be passed from the corresponding output. */
	struct dp_data *data;

	/* This semaphore is signalled when the output has written data
	 * to the shared buffer. The input can read the data now. */
	SemaphoreHandle_t data_can_read;

	/* The output then waits until the data is read. The input signals this
	 * semaphore to len the output know it can continue (and overwrite the
	 * shared buffer with a new data. */
	SemaphoreHandle_t data_has_read;

	/* Type of the input. */
	enum dp_data_type type;
};

struct dp_output {
	bool initialized;

	/* References to all inputs connected to this output. Each connected
	 * input must have a reference pointing back to the output itself. */
	struct dp_input *inputs[DATA_PROCESS_MAX_INPUTS];

	/* Type of the output. */
	enum dp_data_type type;
};

enum dp_node_type {
	DP_NODE_CONSTANT_SOURCE,
	DP_NODE_SENSOR_SOURCE,
	DP_NODE_STATISTICS_NODE,
	DP_NODE_LOG_SINK,
};

struct dp_graph_node {
	enum dp_node_type type;
	void *node;
	struct dp_graph_node *next;
	char name[DP_NODE_NAME_MAX_LEN];
};


struct dp_graph {
	bool initialized;
	struct dp_graph_node *nodes;


};

extern struct dp_graph data_process_graph;



dp_ret_t dp_graph_init(struct dp_graph *self);
dp_ret_t dp_graph_free(struct dp_graph *self);
dp_ret_t dp_graph_add_node(struct dp_graph *self, void *node, enum dp_node_type type, const char *name);
dp_ret_t dp_graph_remove_node(struct dp_graph *self, void *node, const char *name);



/**
 * @brief Initialize the input instance
 */
dp_ret_t dp_input_init(struct dp_input *self, enum dp_data_type type);

/**
 * @brief Free the input instance
 */
dp_ret_t dp_input_free(struct dp_input *self);

/**
 * @brief Check if the input is connected to an output
 */
dp_ret_t dp_input_is_connected(struct dp_input *self);

/**
 * @brief Connect the input to an output
 */
dp_ret_t dp_connect_input_to_output(struct dp_input *self, struct dp_output *output);

/**
 * @brief Read a single piece of data from the input
 */
dp_ret_t dp_input_read(struct dp_input *self, struct dp_data *data);


/**
 * @brief Initialize the output instance
 */
dp_ret_t dp_output_init(struct dp_output *self, enum dp_data_type type);

/**
 * @brief Free the output instance
 */
dp_ret_t dp_output_free(struct dp_output *self);

/**
 * @brief Check if the output is connected to some inputs
 */
bool dp_output_is_connected(struct dp_output *self);

/**
 * @brief Write a single piece of data to the output
 */
dp_ret_t dp_output_write(struct dp_output *self, struct dp_data *data);

