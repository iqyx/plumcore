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
#define NODE_NAME(p, i) p->pos.levels[p->pos.depth + i].node->name

const char *service_plog_relay_states[] = {
	"uninitialized",
	"stopped",
	"running",
	"stop-request",
};

const char *service_plog_relay_spec_types[] = {
	"none",
	"plog",
	"rmac",
};

const struct cli_table_cell service_plog_relay_table[] = {
	{.type = TYPE_STRING, .size = 24, .alignment = ALIGN_RIGHT}, /* service instance */
	{.type = TYPE_STRING, .size = 16, .alignment = ALIGN_RIGHT}, /* current state */
	{.type = TYPE_STRING, .size = 20, .alignment = ALIGN_RIGHT}, /* source */
	{.type = TYPE_STRING, .size = 20, .alignment = ALIGN_RIGHT}, /* destination */
	{.type = TYPE_END}
};


const struct treecli_node *service_plog_relay_instanceN = Node {
	Name "N",
	Commands {
		// Command {
			// Name "export",
			// Exec device_can_sensor_can_sensorN_sniff,
		// },
		End
	},
	Subnodes {
		Node {
			Name "source",
			Values {
				Value {
					Name "type",
					Type TREECLI_VALUE_STR,
					.set = service_plog_relay_instanceN_source_destination_type_set,
				},
				Value {
					Name "plog-filter",
					Type TREECLI_VALUE_STR,
					.set = service_plog_relay_instanceN_source_destination_topic_set,
				},
				End
			},
		},
		Node {
			Name "destination",
			Values {
				Value {
					Name "type",
					Type TREECLI_VALUE_STR,
					.set = service_plog_relay_instanceN_source_destination_type_set,
				},
				Value {
					Name "plog-topic",
					Type TREECLI_VALUE_STR,
					.set = service_plog_relay_instanceN_source_destination_topic_set,
				},
				End
			},
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
		"Source",
		"Destination",
	});
	table_print_row_separator(cli->stream, service_plog_relay_table);

	Interface *interface;
	for (size_t i = 0; (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_PLOG_RELAY, i, &interface)) != ISERVICELOCATOR_RET_FAILED; i++) {
		const char *name = "";
		iservicelocator_get_name(locator, interface, &name);
		PlogRelay *service = (PlogRelay *)interface;

		const char *state_str = service_plog_relay_states[service->state];

		enum plog_relay_spec_type source_type = PLOG_RELAY_SPEC_TYPE_NONE;
		union plog_relay_spec source = {0};
		enum plog_relay_spec_type destination_type = PLOG_RELAY_SPEC_TYPE_NONE;
		union plog_relay_spec destination = {0};

		plog_relay_get_source(service, &source, &source_type);
		plog_relay_get_destination(service, &destination, &destination_type);

		const char *source_type_str = service_plog_relay_spec_types[source_type];
		const char *destination_type_str = service_plog_relay_spec_types[destination_type];


		table_print_row(cli->stream, service_plog_relay_table, (const union cli_table_cell_content []) {
			{.string = name},
			{.string = state_str},
			/** @todo proper source & destination printing */
			{.string = source_type_str},
			{.string = destination_type_str},
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


int32_t service_plog_relay_export(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;

	Interface *interface;
	for (size_t i = 0; (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_PLOG_RELAY, i, &interface)) != ISERVICELOCATOR_RET_FAILED; i++) {
		PlogRelay *service = (PlogRelay *)interface;

		module_cli_output("add new-relay\r\n", cli);

		char line[50];

		const char *name = "";
		iservicelocator_get_name(locator, interface, &name);
		snprintf(line, sizeof(line), "name = %s ", name);
		module_cli_output(line, cli);


/*		snprintf(line, sizeof(line), "id = %lu ", sensor->can_id);
		module_cli_output(line, cli);

		name = NULL;
		iservicelocator_get_name(locator, &sensor->can->interface, &name);
		if (name != NULL) {
			snprintf(line, sizeof(line), "can = %s ", name);
			module_cli_output(line, cli);
		}

		module_cli_output("\r\n", cli);
*/

		snprintf(line, sizeof(line), "enabled = %s ", service->state == PLOG_RELAY_STATE_RUNNING ? "1" : "0");
		module_cli_output(line, cli);

		module_cli_output("\r\n..\r\n", cli);
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


int32_t service_plog_relay_instanceN_source_destination_type_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)value;
	(void)len;
	(void)ctx;
	ServiceCli *cli = (ServiceCli *)parser->context;

	Interface *interface;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_PLOG_RELAY, DNODE_INDEX(parser, -2), &interface) != ISERVICELOCATOR_RET_OK) {
		return 1;
	}
	PlogRelay *instance = (PlogRelay *)interface;

	enum plog_relay_spec_type stype = PLOG_RELAY_SPEC_TYPE_NONE;
	union plog_relay_spec spec = {0};
	if (len == 4 && !memcmp(buf, "plog", 4)) {
		stype = PLOG_RELAY_SPEC_TYPE_PLOG;
	} else if (len == 4 && !memcmp(buf, "rmac", 4)) {
		stype = PLOG_RELAY_SPEC_TYPE_RMAC;
	}

	if (stype == PLOG_RELAY_SPEC_TYPE_NONE) {
		module_cli_output("error: unknown source/destination type (available options: rmac, plog)\r\n", cli);
		return 1;
	}

	if (!strcmp(NODE_NAME(parser, -1), "source")) {
		plog_relay_set_source(instance, &spec, stype);
		return 0;
	} else if (!strcmp(NODE_NAME(parser, -1), "destination")) {
		plog_relay_set_destination(instance, &spec, stype);
		return 0;
	} else {
		u_assert(false);
	}

	return 1;
}


int32_t service_plog_relay_instanceN_source_destination_topic_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)value;
	(void)len;
	(void)ctx;
	ServiceCli *cli = (ServiceCli *)parser->context;

	Interface *interface;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_PLOG_RELAY, DNODE_INDEX(parser, -2), &interface) != ISERVICELOCATOR_RET_OK) {
		return 1;
	}
	PlogRelay *instance = (PlogRelay *)interface;

	/* Get the current setting. */
	enum plog_relay_spec_type spec_type = PLOG_RELAY_SPEC_TYPE_NONE;
	union plog_relay_spec spec = {0};

	if (!strcmp(NODE_NAME(parser, -1), "source")) {
		plog_relay_get_source(instance, &spec, &spec_type);
	} else if (!strcmp(NODE_NAME(parser, -1), "destination")) {
		plog_relay_get_destination(instance, &spec, &spec_type);
	} else {
		u_assert(false);
		return 1;
	}

	if (spec_type != PLOG_RELAY_SPEC_TYPE_PLOG) {
		module_cli_output("error: topic is not valid for this source/destination type. Set the type first.\r\n", cli);
		return 1;
	}

	if (len >= PLOG_RELAY_PLOG_TOPIC_LEN) {
		module_cli_output("error: topic too long\r\n", cli);
		return 1;
	}
	memcpy(spec.plog.topic, buf, len);

	if (!strcmp(NODE_NAME(parser, -1), "source")) {
		plog_relay_set_source(instance, &spec, spec_type);
	} else if (!strcmp(NODE_NAME(parser, -1), "destination")) {
		plog_relay_set_destination(instance, &spec, spec_type);
	} else {
		u_assert(false);
		return 1;
	}

	return 0;
}
