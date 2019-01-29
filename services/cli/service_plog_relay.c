/*
 * plog_relay service CLI configuration
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

#include "main.h"

/* Common functions and helpers for the CLI service. */
#include "cli_table_helper.h"
#include "cli.h"

/* Helper defines for tree construction. */
#include "services/cli/system_cli_tree.h"

#include "services/interfaces/servicelocator.h"
#include "services/plog-relay/plog-relay.h"
#include "service_plog_relay.h"

#define DNODE_INDEX(p, i) p->pos.levels[p->pos.depth + i].dnode_index

const char *service_plog_relay_states[] = {
	"uninitialized",
	"stopped",
	"running",
	"stop-request",
};

const struct cli_table_cell service_plog_relay_table[] = {
	{.type = TYPE_STRING, .size = 24, .alignment = ALIGN_RIGHT}, /* service instance */
	{.type = TYPE_STRING, .size = 16, .alignment = ALIGN_RIGHT}, /* current state */
	{.type = TYPE_END}
};


const struct treecli_node *service_plog_relay_instanceN = Node {
	Name "N",
	Commands {
		Command {
			Name "export",
			// Exec device_can_sensor_can_sensorN_sniff,
		},
		End
	},
	Values {
		Value {
			Name "enabled",
			.set = service_plog_relay_instanceN_enabled_set,
			Type TREECLI_VALUE_BOOL,
		},
		// Value {
			// Name "id",
			// .set = device_can_sensor_can_sensorN_id_set,
			// Type TREECLI_VALUE_UINT32,
		// },
		// Value {
			// Name "can",
			// .set = device_can_sensor_can_sensorN_can_set,
			// Type TREECLI_VALUE_STR,
		// },
		Value {
			Name "name",
			.set = service_plog_relay_instanceN_name_set,
			Type TREECLI_VALUE_STR,
		},
		// Value {
			// Name "quantity-name",
			// .set = device_can_sensor_can_sensorN_quantity_name_set,
			// Type TREECLI_VALUE_STR,
		// },
		// Value {
			// Name "quantity-symbol",
			// .set = device_can_sensor_can_sensorN_quantity_symbol_set,
			// Type TREECLI_VALUE_STR,
		// },
		// Value {
			// Name "unit-name",
			// .set = device_can_sensor_can_sensorN_unit_name_set,
			// Type TREECLI_VALUE_STR,
		// },
		// Value {
			// Name "unit-symbol",
			// .set = device_can_sensor_can_sensorN_unit_symbol_set,
			// Type TREECLI_VALUE_STR,
		// },
		End
	},
};


int32_t service_plog_relay_print(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;

	table_print_header(cli->stream, service_plog_relay_table, (const char *[]){
		"Service instance",
		"State",
	});
	table_print_row_separator(cli->stream, service_plog_relay_table);

	Interface *interface;
	for (size_t i = 0; (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_PLOG_RELAY, i, &interface)) != ISERVICELOCATOR_RET_FAILED; i++) {
		const char *name = "";
		iservicelocator_get_name(locator, interface, &name);
		PlogRelay *service = (PlogRelay *)interface;

		const char *state_str = service_plog_relay_states[service->state];

		table_print_row(cli->stream, service_plog_relay_table, (const union cli_table_cell_content []) {
			{.string = name},
			{.string = state_str},
		});
	}
	return 0;
}


int32_t service_plog_relay_add(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;

	PlogRelay *instance = malloc(sizeof(PlogRelay));
	if (instance == NULL) {
		module_cli_output("error: cannot allocate memory\r\n", cli);
		return 1;
	}
	if (plog_relay_init(instance) != PLOG_RELAY_RET_OK) {
		module_cli_output("error: cannot initialize new instance\r\n", cli);
		return 1;
	}

	plog_relay_set_name(instance, "new-relay");

	iservicelocator_add(
		locator,
		ISERVICELOCATOR_TYPE_PLOG_RELAY,
		&instance->interface,
		instance->name
	);

	return 0;
}


int32_t service_plog_relay_instanceN_create(struct treecli_parser *parser, uint32_t index, struct treecli_node *node, void *ctx) {
	(void)parser;
	(void)ctx;

	Interface *interface;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_PLOG_RELAY, index, &interface) == ISERVICELOCATOR_RET_OK) {
		const char *name = "";
		iservicelocator_get_name(locator, interface, &name);
		if (node->name != NULL) {
			strcpy(node->name, name);
		}
		node->commands = service_plog_relay_instanceN->commands;
		node->values = service_plog_relay_instanceN->values;
		node->subnodes = service_plog_relay_instanceN->subnodes;
		return 0;
	}
	return -1;
}


int32_t service_plog_relay_instanceN_enabled_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)ctx;
	(void)value;
	(void)len;
	ServiceCli *cli = (ServiceCli *)parser->context;

	Interface *interface;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_PLOG_RELAY, DNODE_INDEX(parser, -1), &interface) != ISERVICELOCATOR_RET_OK) {
		return 1;
	}
	PlogRelay *instance = (PlogRelay *)interface;

	bool e = *(bool *)buf;
	if (e != (instance->state == PLOG_RELAY_STATE_RUNNING)) {
		if (e) {
			if (plog_relay_start(instance) != PLOG_RELAY_RET_OK) {
				module_cli_output("cannot start the service instance\r\n", cli);
				return 1;
			}
			module_cli_output("service instance started\r\n", cli);
		} else {
			if (plog_relay_stop(instance) != PLOG_RELAY_RET_OK) {
				module_cli_output("cannot stop the service instance\r\n", cli);
				return 1;
			}
			module_cli_output("service instance stopped\r\n", cli);
		}
	}

	return 0;
}


int32_t service_plog_relay_instanceN_name_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)ctx;
	(void)value;
	(void)len;
	ServiceCli *cli = (ServiceCli *)parser->context;

	Interface *interface;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_PLOG_RELAY, DNODE_INDEX(parser, -1), &interface) != ISERVICELOCATOR_RET_OK) {
		return 1;
	}
	PlogRelay *instance = (PlogRelay *)interface;

	char *name = malloc(len + 1);
	if (name == NULL) {
		module_cli_output("error: cannot allocate memory\r\n", cli);
		return 1;
	}
	memcpy(name, buf, len);
	name[len] = '\0';

	if (plog_relay_set_name(instance, name) != PLOG_RELAY_RET_OK) {
		module_cli_output("error: cannot set driver instance name\r\n", cli);
		free(name);
		return 1;
	}

	free(name);
	return 0;
}
