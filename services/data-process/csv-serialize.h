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
 * A data-process node for value serialization. Output is a set of bytes forming a CSV line.
 * It outputs all values received on all inputs, each value on a single line. Each line is
 * a single output frame.
 */

#pragma once

#include "stdint.h"
#include "stdbool.h"
#include "stdlib.h"

#include "FreeRTOS.h"
#include "task.h"

#include "data-process.h"


#define CSV_SERIALIZE_INPUT_NAME_LEN 16

struct dp_csv_serialize_input {
	struct dp_input in;
	char name[CSV_SERIALIZE_INPUT_NAME_LEN];

	struct dp_csv_serialize_input *next;
};

struct dp_csv_serialize {
	struct dp_graph_node_descriptor descriptor;

	struct dp_output out;
	struct dp_csv_serialize_input *first_input;

	bool initialized;
	volatile bool can_run;
	volatile bool running;
	TaskHandle_t task;
};


/**
 * @brief Initialize a CSV serialization node
 *
 * @param self A csv-serialize instance
 */
dp_ret_t dp_csv_serialize_init(struct dp_csv_serialize *self);

/**
 * @brief Feee a CSV serialization node
 *
 * @param self An initialized csv-serialize instance
 */
dp_ret_t dp_csv_serialize_free(struct dp_csv_serialize *self);

/**
 * @brief Start an initialized and configured CSV serialization node
 *
 * @param self An initialized csv-serialize instance
 */
dp_ret_t dp_csv_serialize_start(struct dp_csv_serialize *self);

/**
 * @brief Stop a started CSV serialization node
 *
 * @param self A started csv-serialize instance
 */
dp_ret_t dp_csv_serialize_stop(struct dp_csv_serialize *self);

/**
 * @brief Add a new input to the CSV serialization node
 *
 * @param self An initialized csv-serialize instance. The node must not be running.
 * @param input Pointer to a DP input structure which will be set to a newly added input.
 * @param name Name of the new input.
 */
dp_ret_t dp_csv_serialize_add_input(struct dp_csv_serialize *self, struct dp_input **input, const char *name);

/**
 * @brief Remove an input identified by its name
 *
 * @param self An initialized csv-serialize instance. The node must not be running.
 * @param name Name of the node to remove.
 */
dp_ret_t dp_csv_serialize_remove_input(struct dp_csv_serialize *self, const char *name);


