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
 * A data-process framework source to read a sensor and output it's value
 * in a configurable interval. This is basically a proxy-class with some glue.
 */

#pragma once

#include "stdint.h"
#include "stdbool.h"
#include "stdlib.h"

#include "FreeRTOS.h"
#include "task.h"

#include "interfaces/sensor.h"
#include "data-process.h"


struct dp_sensor_source {
	struct dp_graph_node_descriptor descriptor;

	struct dp_output out;

	bool initialized;
	volatile bool can_run;
	volatile bool running;
	TaskHandle_t task;
	uint32_t interval_ms;

	ISensor *sensor;
	float offset;
	float multiplier;
};


dp_ret_t dp_sensor_source_init(struct dp_sensor_source *self);
dp_ret_t dp_sensor_source_free(struct dp_sensor_source *self);
dp_ret_t dp_sensor_source_start(struct dp_sensor_source *self);
dp_ret_t dp_sensor_source_stop(struct dp_sensor_source *self);
dp_ret_t dp_sensor_source_set_sensor(struct dp_sensor_source *self, ISensor *sensor);
dp_ret_t dp_sensor_source_interval(struct dp_sensor_source *self, uint32_t interval_ms);
dp_ret_t dp_sensor_source_set_offset_multiplier(struct dp_sensor_source *self, float offset, float multiplier);




