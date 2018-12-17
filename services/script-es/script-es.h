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

#pragma once

#include <stdint.h>
#include <stdbool.h>

/** @todo WIP service to check feasibility of running Duktape ECMAScript interpreter
 *        as a part of the plumCore. */

typedef enum {
	SCRIPT_ES_RET_OK = 0,
	SCRIPT_ES_RET_FAILED,
	SCRIPT_ES_RET_NULL,
} script_es_ret_t;


typedef struct {
	bool initialized;
	TaskHandle_t task;

} ScriptEs;


/**
 * @brief Initialize a new script-es service instance
 *
 * A new script engine instance is initialized and all the required
 * resources are allocated. The instance contains its own sandboxed
 * environment.
 */
script_es_ret_t script_es_init(ScriptEs *self);

/**
 * @brief Free the script-es instance
 *
 * The service instance can be freed only in aborted state
 * (no script is running).
 */
script_es_ret_t script_es_free(ScriptEs *self);

/**
 * @brief Load script from a filesystem
 *
 * @todo this function is intended to load and prepare a ECMAScript
 *       file from a filesystem. The file should be compiled (if required),
 *       but not launched.
 */
script_es_ret_t script_es_load(ScriptEs *self);

/**
 * @brief Unload a loaded script freeing its resources
 */
script_es_ret_t script_es_unload(ScriptEs *self);

/**
 * @brief Run a previously loaded (and compiled) script
 */
script_es_ret_t script_es_run(ScriptEs *self);

/**
 * @brief Abort the running script
 */
script_es_ret_t script_es_abort(ScriptEs *self);
