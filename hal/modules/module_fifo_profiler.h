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

#ifndef _MODULE_FIFO_PROFILER_H_
#define _MODULE_FIFO_PROFILER_H_

#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "hal_module.h"
#include "interface_stream.h"
#include "interface_profiling.h"


struct module_fifo_profiler_msg {
	uint8_t module;
	uint8_t message;
	uint32_t time;
	int16_t param;
};

struct module_fifo_profiler {
	struct hal_module_descriptor module;
	struct interface_profiling iface;
	uint32_t timer;

	QueueHandle_t queue;

	struct interface_stream *stream;
	TaskHandle_t task;
};


int32_t module_fifo_profiler_init(struct module_fifo_profiler *profiler, const char *name, uint32_t timer, uint32_t length);
#define MODULE_FIFO_PROFILER_INIT_OK 0
#define MODULE_FIFO_PROFILER_INIT_FAILED -1

int32_t module_fifo_profiler_free(struct module_fifo_profiler *profiler);
#define MODULE_FIFO_PROFILER_FREE_OK 0
#define MODULE_FIFO_PROFILER_FREE_FAILED -1

int32_t module_fifo_profiler_stream_enable(struct module_fifo_profiler *profiler, struct interface_stream *stream);
#define MODULE_FIFO_PROFILER_STREAM_ENABLE_OK 0
#define MODULE_FIFO_PROFILER_STREAM_ENABLE_FAILED -1

int32_t module_fifo_profiler_stream_disable(struct module_fifo_profiler *profiler);
#define MODULE_FIFO_PROFILER_STREAM_DISABLE_OK 0
#define MODULE_FIFO_PROFILER_STREAM_DISABLE_FAILED -1

#endif
