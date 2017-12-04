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
 * This is a statistics node which keeps history of a configurable number of
 * values and computes average, min and max.
 */

#pragma once

#include "stdint.h"
#include "stdbool.h"
#include "stdlib.h"

#include "FreeRTOS.h"
#include "task.h"

#include "data-process.h"


struct dp_statistics_node {
	const char *name;
	enum dp_data_type type;

	struct dp_input in;

	struct dp_output out_avg;
	struct dp_output out_min;
	struct dp_output out_max;

	struct dp_output out_avg_floating;
	struct dp_output out_min_floating;
	struct dp_output out_max_floating;

	bool initialized;
	volatile bool can_run;
	volatile bool running;
	TaskHandle_t task;

	union dp_data_content *history;
	uint32_t history_len;
	uint32_t history_pos;
};


dp_ret_t dp_statistics_node_init(struct dp_statistics_node *self, const char *name, enum dp_data_type type);
dp_ret_t dp_statistics_node_free(struct dp_statistics_node *self);
dp_ret_t dp_statistics_node_start(struct dp_statistics_node *self);
dp_ret_t dp_statistics_node_stop(struct dp_statistics_node *self);

