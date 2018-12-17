/*
 * ECMAScript interpreter service using Duktape
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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"
#include "port.h"

#include "script-es.h"
#include "duktape.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "script-es"


/** @todo WIP service to check feasibility of running Duktape ECMAScript interpreter
 *        as a part of the plumCore. */

static void script_es_task(void *p) {
	ScriptEs *self = (ScriptEs *)p;

	duk_context *ctx = duk_create_heap_default();
	if (ctx != NULL) {
		duk_eval_string(ctx, "1 + 2");
		u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("output = %d"), (int)duk_get_int(ctx, -1));
		duk_destroy_heap(ctx);
	} else {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create Duktape context"));
	}

	while (1) {
		vTaskDelay(100);
	}

	vTaskDelete(NULL);
}


script_es_ret_t script_es_init(ScriptEs *self) {
	if (u_assert(self != NULL)) {
		return SCRIPT_ES_RET_NULL;
	}

	xTaskCreate(script_es_task, "script-es", configMINIMAL_STACK_SIZE + 1536, (void *)self, 1, &(self->task));
	if (self->task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		return SCRIPT_ES_RET_FAILED;
	}
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("module started"));

	return SCRIPT_ES_RET_OK;
}


script_es_ret_t script_es_free(ScriptEs *self) {
	if (u_assert(self != NULL)) {
		return SCRIPT_ES_RET_NULL;
	}

	return SCRIPT_ES_RET_OK;
}
