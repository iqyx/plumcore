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
 * This is part of the CLI residing under the /service/data-process subtree
 *
 * Subtrees, commands and value manipulation contained here allow you to
 * manipulate data-process flow graphs using the CLI, start and stop
 * individual nodes, connect nodes and set their properties.
 *
 * If the CLI is used for startup configuration loading, export commands
 * are available to export the current running configuration in a format
 * suitable for parsing after startup.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"

/* Common functions and helpers for the CLI service. */
#include "cli_table_helper.h"
#include "cli.h"

/* Helper defines for tree construction. */
#include "services/cli/system_cli_tree.h"

#include "services/interfaces/servicelocator.h"
#include "interfaces/sensor.h"
#include "port.h"

/* Includes of the service itself. */
#include "services/data-process/data-process.h"
#include "services/data-process/sensor-source.h"
#include "services/data-process/log-sink.h"

#include "service_data_process.h"


/**
 * Tables for printing.
 */

const struct cli_table_cell service_data_process_node_table[] = {
	{.type = TYPE_STRING, .size = 16, .alignment = ALIGN_RIGHT}, /* name */
	{.type = TYPE_STRING, .size = 16, .alignment = ALIGN_RIGHT}, /* type */
	{.type = TYPE_STRING, .size = 20, .alignment = ALIGN_RIGHT}, /* status */
	{.type = TYPE_END}
};

const struct cli_table_cell service_data_process_sensor_source_table[] = {
	{.type = TYPE_STRING, .size = 16, .alignment = ALIGN_RIGHT}, /* Sensor source name */
	{.type = TYPE_STRING, .size = 8, .alignment = ALIGN_RIGHT}, /* State (is enabled?) */
	{.type = TYPE_STRING, .size = 16, .alignment = ALIGN_RIGHT}, /* Sensor interface name */
	{.type = TYPE_STRING, .size = 12, .alignment = ALIGN_RIGHT}, /* Interval in ms */
	{.type = TYPE_STRING, .size = 12, .alignment = ALIGN_RIGHT}, /* Multiplier */
	{.type = TYPE_STRING, .size = 12, .alignment = ALIGN_RIGHT}, /* Offset */
	{.type = TYPE_END}
};


/**
 * Subtrees.
 */

const struct treecli_node *service_data_process_sensor_source_N = Node {
	Name "N",
	Commands {
		Command {
			Name "export",
			Exec service_data_process_sensor_source_N_export,
		},
		End
	},
	Values {
		Value {
			Name "name",
			.set = service_data_process_sensor_source_N_name_set,
			Type TREECLI_VALUE_STR,
		},
		Value {
			Name "enabled",
			.set = service_data_process_sensor_source_N_enabled_set,
			Type TREECLI_VALUE_BOOL,
		},
		Value {
			Name "sensor",
			.set = service_data_process_sensor_source_N_sensor_set,
			Type TREECLI_VALUE_STR,
		},
		Value {
			Name "interval",
			.set = service_data_process_sensor_source_N_interval_set,
			Type TREECLI_VALUE_UINT32,
		},
		End
	},
};


const struct treecli_node *service_data_process_log_sink_N = Node {
	Name "N",
	Values {
		Value {
			Name "name",
			.set = service_data_process_log_sink_N_name_set,
			Type TREECLI_VALUE_STR,
		},
		Value {
			Name "enabled",
			.set = service_data_process_log_sink_N_enabled_set,
			Type TREECLI_VALUE_BOOL,
		},
		Value {
			Name "input",
			.set = service_data_process_log_sink_N_input_set,
			Type TREECLI_VALUE_STR,
		},
		End
	},
};


/**
 * Find a next node of type @p type starting from the @p current node.
 */

static struct dp_graph_node *find_node(struct dp_graph_node *current, enum dp_node_type type) {
	while (current != NULL) {
		if (current->type == type) {
			return current;
		}
		current = current->next;
	}
	return NULL;
}


/**
 * Find a next node of type @p type with an @p index (counting from zero) from the @p current node.
 */

static struct dp_graph_node *find_node_by_index(struct dp_graph_node *current, enum dp_node_type type, size_t index) {
	size_t i = 0;
	while ((current = find_node(current, type)) != NULL) {
		if (i == index) {
			return current;
		}
		i++;
		current = current->next;
	}
	return NULL;
}


/**
 * Informational and printing commands.
 */

int32_t service_data_process_print(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;

	table_print_header(cli->stream, service_data_process_node_table, (const char *[]){
		"Node name",
		"Node type",
		"Status",
	});
	table_print_row_separator(cli->stream, service_data_process_node_table);

	struct dp_graph_node *node = data_process_graph.nodes;
	while (node != NULL) {

		table_print_row(cli->stream, service_data_process_node_table, (const union cli_table_cell_content []) {
			{.string = node->name},
			{.string = dp_node_type_str[node->type]},
			{.string = "blah"},
		});

		node = node->next;
	}
	return 0;
}


int32_t service_data_process_sensor_source_print(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;

	table_print_header(cli->stream, service_data_process_sensor_source_table, (const char *[]){
		"Node name",
		"Enabled",
		"Sensor interface",
		"Interval",
		"Multiplier",
		"Offset",
	});
	table_print_row_separator(cli->stream, service_data_process_sensor_source_table);

	struct dp_graph_node *node = data_process_graph.nodes;
	while ((node = find_node(node, DP_NODE_SENSOR_SOURCE)) != NULL) {
		struct dp_sensor_source *sensor = (struct dp_sensor_source *)node->node;


		/* Convert values to strings as the table helper macros and functions do
		 * not support floats and unit suffixes. */
		char interval_str[10];
		snprintf(interval_str, sizeof(interval_str), "%lu ms", sensor->interval_ms);

		char multiplier_str[10];
		snprintf(multiplier_str, sizeof(multiplier_str), "x%d.%03d", (int)sensor->multiplier, (int)(sensor->multiplier * 1000.0f) % 1000);

		char offset_str[10];
		snprintf(offset_str, sizeof(offset_str), "+%d.%03d", (int)sensor->offset, (int)(sensor->offset * 1000.0f) % 1000);

		table_print_row(cli->stream, service_data_process_sensor_source_table, (const union cli_table_cell_content []) {
			{.string = node->name},
			{.string = sensor->running ? "yes" : "no"},
			{.string = "blah"},
			{.string = interval_str},
			{.string = multiplier_str},
			{.string = offset_str},
		});

		node = node->next;
	}
	return 0;
}


/**
 * Configuration exporting commands.
 */

int32_t service_data_process_sensor_source_export(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;

	struct dp_graph_node *node = data_process_graph.nodes;
	while ((node = find_node(node, DP_NODE_SENSOR_SOURCE)) != NULL) {

		module_cli_output("add\r\n", parser->context);
		module_cli_output("new-node\r\n", parser->context);
		char line[50];
		snprintf(line, sizeof(line), "%s export", node->name);
		treecli_parser_parse_line(parser, line);
		module_cli_output("..\r\n", parser->context);

		node = node->next;
	}
	return 0;
}


int32_t service_data_process_sensor_source_N_export(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;

	struct dp_graph_node *graph_node = find_node_by_index(data_process_graph.nodes, DP_NODE_SENSOR_SOURCE, DNODE_INDEX(parser, -1));
	struct dp_sensor_source *s = (struct dp_sensor_source *)graph_node->node;
	if (s == NULL) {
		return 1;
	}

	char line[50];
	snprintf(line, sizeof(line), "name = %s\r\n", graph_node->name);
	module_cli_output(line, parser->context);
	snprintf(line, sizeof(line), "interval = %u\r\n", (unsigned int)s->interval_ms);
	module_cli_output(line, parser->context);
	snprintf(line, sizeof(line), "enabled = %u\r\n", (unsigned int)s->running);
	module_cli_output(line, parser->context);


	Interface *interface;
	for (size_t i = 0; (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_SENSOR, i, &interface)) != ISERVICELOCATOR_RET_FAILED; i++) {
		ISensor *sensor = (ISensor *)interface;
		const char *name = "";
		iservicelocator_get_name(locator, interface, &name);

		if (sensor == s->sensor) {
			snprintf(line, sizeof(line), "sensor = %s\r\n", name);
			module_cli_output(line, parser->context);
		}
	}

	return 0;
}


/**
 * Dynamic node creation functions.
 */

int32_t service_data_process_sensor_source_create(struct treecli_parser *parser, uint32_t index, struct treecli_node *node, void *ctx) {
	(void)ctx;
	(void)parser;

	struct dp_graph_node *graph_node = NULL;
	graph_node = find_node_by_index(data_process_graph.nodes, DP_NODE_SENSOR_SOURCE, index);
	if (graph_node == NULL) {
		return -1;
	}

	strcpy(node->name, graph_node->name);
	node->commands = service_data_process_sensor_source_N->commands;
	node->values = service_data_process_sensor_source_N->values;
	return 0;
}


int32_t service_data_process_statistics_node_create(struct treecli_parser *parser, uint32_t index, struct treecli_node *node, void *ctx) {
	(void)ctx;
	(void)parser;

	struct dp_graph_node *graph_node = NULL;
	graph_node = find_node_by_index(data_process_graph.nodes, DP_NODE_STATISTICS_NODE, index);
	if (graph_node == NULL) {
		return -1;
	}
	strcpy(node->name, graph_node->name);
	return 0;
}


int32_t service_data_process_log_sink_create(struct treecli_parser *parser, uint32_t index, struct treecli_node *node, void *ctx) {
	(void)ctx;
	(void)parser;

	struct dp_graph_node *graph_node = NULL;
	graph_node = find_node_by_index(data_process_graph.nodes, DP_NODE_LOG_SINK, index);
	if (graph_node == NULL) {
		return -1;
	}
	strcpy(node->name, graph_node->name);
	node->values = service_data_process_log_sink_N->values;
	return 0;
}


/**
 * Commands for adding and removing items.
 */

int32_t service_data_process_sensor_source_add(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	(void)parser;

	/** @todo prevent adding new-node twice */

	struct dp_sensor_source *new = malloc(sizeof(struct dp_sensor_source));
	if (new == NULL) {
		return 1;
	}
	dp_sensor_source_init(new);
	dp_graph_add_node(&data_process_graph, (void *)new, DP_NODE_SENSOR_SOURCE, "new-node");
	return 0;
}


int32_t service_data_process_log_sink_add(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	(void)parser;

	/** @todo prevent adding new-node twice */

	struct dp_log_sink *new = malloc(sizeof(struct dp_log_sink));
	if (new == NULL) {
		return 1;
	}
	dp_log_sink_init(new, DP_DATA_TYPE_FLOAT);
	dp_graph_add_node(&data_process_graph, (void *)new, DP_NODE_LOG_SINK, "new-node");

	return 0;
}


/**
 * Value manipulation functions.
 */

int32_t service_data_process_sensor_source_N_name_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)ctx;
	(void)value;

	struct dp_graph_node *graph_node = find_node_by_index(data_process_graph.nodes, DP_NODE_SENSOR_SOURCE, DNODE_INDEX(parser, -1));
	if (graph_node == NULL) {
		return -1;
	}
	strncpy(graph_node->name, buf, len);
	graph_node->name[len] = '\0';
	return 0;
}


int32_t service_data_process_sensor_source_N_enabled_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)len;
	(void)ctx;
	(void)value;

	struct dp_graph_node *graph_node = find_node_by_index(data_process_graph.nodes, DP_NODE_SENSOR_SOURCE, DNODE_INDEX(parser, -1));
	if (graph_node == NULL) {
		return -1;
	}
	struct dp_sensor_source *s = (struct dp_sensor_source *)graph_node->node;
	bool e = *(bool *)buf;

	if (e) {
		if (s->running) {
			u_log(system_log, LOG_TYPE_ERROR, "cannot enable a running node");
		} else {
			dp_sensor_source_start(s);
		}
	} else {
		if (s->running) {
			dp_sensor_source_stop(s);
		} else {
			u_log(system_log, LOG_TYPE_ERROR, "cannot disable a stopped node");
		}
	}
	return 0;
}


int32_t service_data_process_log_sink_N_name_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)ctx;
	(void)value;

	struct dp_graph_node *graph_node = find_node_by_index(data_process_graph.nodes, DP_NODE_LOG_SINK, DNODE_INDEX(parser, -1));
	if (graph_node == NULL) {
		return -1;
	}
	strncpy(graph_node->name, buf, len);
	graph_node->name[len] = '\0';
	return 0;
}


int32_t service_data_process_log_sink_N_enabled_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)len;
	(void)ctx;
	(void)value;

	struct dp_graph_node *graph_node = find_node_by_index(data_process_graph.nodes, DP_NODE_LOG_SINK, DNODE_INDEX(parser, -1));
	if (graph_node == NULL) {
		return -1;
	}
	struct dp_log_sink *s = (struct dp_log_sink *)graph_node->node;
	bool e = *(bool *)buf;
	if (e) {
		if (s->running) {
			u_log(system_log, LOG_TYPE_ERROR, "cannot enable a running node");
		} else {
			dp_log_sink_start(s);
		}
	} else {
		if (s->running) {
			dp_log_sink_stop(s);
		} else {
			u_log(system_log, LOG_TYPE_ERROR, "cannot disable a stopped node");
		}
	}
	return 0;
}


int32_t service_data_process_log_sink_N_input_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)len;
	(void)ctx;
	(void)value;

	struct dp_graph_node *graph_node = find_node_by_index(data_process_graph.nodes, DP_NODE_LOG_SINK, DNODE_INDEX(parser, -1));
	if (graph_node == NULL) {
		return -1;
	}
	struct dp_log_sink *s = (struct dp_log_sink *)graph_node->node;
	char name[40];
	char *output_name = name;
	strncpy(name, buf, len);
	name[len] = '\0';
	/* Find the semicolon. */
	while (*output_name && (*output_name != ':')) {
		output_name++;
	}
	if (*output_name == ':') {
		*output_name = '\0';
		output_name++;
	} else {
		u_log(system_log, LOG_TYPE_ERROR, "malformed output name %s (should be node:output)", name);
		return 1;
	}

	struct dp_graph_node *output_node = data_process_graph.nodes;
	struct dp_output *o = NULL;
	while (output_node != NULL) {
		if (!strcmp(name, output_node->name)) {
			struct dp_graph_node_descriptor *descriptor = (struct dp_graph_node_descriptor *)output_node->node;
			o = descriptor->get_output_by_name(output_name, descriptor->context);
			break;
		}
		output_node = output_node->next;
	}
	if (o == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, "output %s:%s not found", name, output_name);
		return 2;
	}
	dp_connect_input_to_output(&(s->in), o);
	return 0;
}


int32_t service_data_process_sensor_source_N_sensor_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)len;
	(void)ctx;
	(void)value;

	char name[20];
	if (len >= sizeof(name)) {
		return 1;
	}
	strncpy(name, buf, len);
	name[len] = '\0';

	/* Get the right interface by its name first. */
	Interface *interface;
	if (iservicelocator_query_name_type(locator, name, ISERVICELOCATOR_TYPE_SENSOR, &interface) == ISERVICELOCATOR_RET_OK) {
		ISensor *sensor = (ISensor *)interface;

		struct dp_graph_node *graph_node = find_node_by_index(data_process_graph.nodes, DP_NODE_SENSOR_SOURCE, DNODE_INDEX(parser, -1));
		if (graph_node == NULL) {
			return -1;
		}
		struct dp_sensor_source *s = (struct dp_sensor_source *)graph_node->node;
		dp_sensor_source_set_sensor(s, sensor);

		return 0;
	} else {
		u_log(system_log, LOG_TYPE_ERROR, "no sensor interface was found");
		return 1;
	}
}


int32_t service_data_process_sensor_source_N_interval_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)len;
	(void)ctx;
	(void)value;

	struct dp_graph_node *graph_node = find_node_by_index(data_process_graph.nodes, DP_NODE_SENSOR_SOURCE, DNODE_INDEX(parser, -1));
	if (graph_node == NULL) {
		return -1;
	}
	struct dp_sensor_source *s = (struct dp_sensor_source *)graph_node->node;
	uint32_t interval = *(uint32_t *)buf;
	dp_sensor_source_interval(s, interval);
	return 0;
}

