/*
 * A service for uploading sensor values to the plog message router
 *
 * Copyright (c) 2018, Marek Koza (qyx@krtko.org)
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

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "module.h"

#include "interfaces/plog/descriptor.h"
#include "interfaces/plog/client.h"


typedef enum {
	PLOG_SENSOR_UPLOAD_RET_OK = 0,
	PLOG_SENSOR_UPLOAD_RET_FAILED,
	PLOG_SENSOR_UPLOAD_RET_NULL,
} plog_sensor_upload_ret_t;


typedef struct {
	Module module;

	TaskHandle_t task;
	bool initialized;
	volatile bool can_run;
	volatile bool running;
	uint32_t interval_ms;
	Plog plog;

} PlogSensorUpload;


plog_sensor_upload_ret_t plog_sensor_upload_init(PlogSensorUpload *self, uint32_t interval_ms);
plog_sensor_upload_ret_t plog_sensor_upload_free(PlogSensorUpload *self);

