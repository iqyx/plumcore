/*
 * plog message relay
 *
 * Copyright (c) 2019, Marek Koza (qyx@krtko.org)
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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "main.h"
#include "interface.h"
#include "interfaces/plog/client.h"
#include "interfaces/radio-mac/client.h"


#define PLOG_RELAY_INSTANCE_NAME_LEN 32

typedef enum {
	PLOG_RELAY_RET_OK = 0,
	PLOG_RELAY_RET_NULL,
	PLOG_RELAY_RET_BAD_ARG,
	PLOG_RELAY_RET_FAILED,
	PLOG_RELAY_RET_BAD_STATE,
} plog_relay_ret_t;

enum plog_relay_state {
	PLOG_RELAY_STATE_UNINITIALIZED = 0,
	PLOG_RELAY_STATE_STOPPED,
	PLOG_RELAY_STATE_RUNNING,
	PLOG_RELAY_STATE_STOP_REQ,
};

struct plog_relay_spec_plog {
	char *topic;
	Plog *plog;
};

struct plog_relay_spec_rmac {
	uint32_t node_id;
	RadioMac *mac;
};

enum plog_relay_spec_type {
	PLOG_RELAY_SPEC_TYPE_NONE = 0,
	PLOG_RELAY_SPEC_TYPE_PLOG,
	PLOG_RELAY_SPEC_TYPE_RMAC,
};

union plog_relay_spec {
	struct plog_relay_spec_plog plog;
	struct plog_relay_spec_rmac rmac;
};

enum plog_relay_rule_action {
	PLOG_RELAY_RULE_ACTION_NONE = 0,
	PLOG_RELAY_RULE_ACTION_ACCEPT,
	PLOG_RELAY_RULE_ACTION_DROP,
	PLOG_RELAY_RULE_ACTION_RATELIMIT,
};

struct plog_relay_rule {

	struct plog_relay_rule *previous;
	struct plog_relay_rule *next;
};

typedef struct {
	Interface interface;

	volatile enum plog_relay_state state;
	union plog_relay_spec *source;
	union plog_relay_spec *destination;

	struct plog_relay_rule *rules;

	TaskHandle_t task;
	char name[PLOG_RELAY_INSTANCE_NAME_LEN];

} PlogRelay;


/* Basic service instance manipulation. */
plog_relay_ret_t plog_relay_init(PlogRelay *self);
plog_relay_ret_t plog_relay_free(PlogRelay *self);
plog_relay_ret_t plog_relay_start(PlogRelay *self);
plog_relay_ret_t plog_relay_stop(PlogRelay *self);
plog_relay_ret_t plog_relay_set_name(PlogRelay *self, const char *name);

/* Set/get source/destination. */
plog_relay_ret_t plog_relay_set_source(PlogRelay *self, union plog_relay_spec *source, enum plog_relay_spec_type type);
plog_relay_ret_t plog_relay_get_source(PlogRelay *self, union plog_relay_spec **source, enum plog_relay_spec_type *type);
plog_relay_ret_t plog_relay_set_destination(PlogRelay *self, union plog_relay_spec *destination, enum plog_relay_spec_type type);
plog_relay_ret_t plog_relay_get_destination(PlogRelay *self, union plog_relay_spec **destination, enum plog_relay_spec_type *type);

/* Add, remove and reorder rules. */
plog_relay_ret_t plog_relay_rule_add(PlogRelay *self, struct plog_relay_rule *rule);
plog_relay_ret_t plog_relay_rule_get_next(PlogRelay *self, struct plog_relay_rule *current, struct plog_relay_rule **next);
plog_relay_ret_t plog_relay_rule_move_up(PlogRelay *self, struct plog_relay_rule *rule);
plog_relay_ret_t plog_relay_rule_move_down(PlogRelay *self, struct plog_relay_rule *rule);
plog_relay_ret_t plog_relay_rule_set_enabled(PlogRelay *self, struct plog_relay_rule *rule, bool enabled);
