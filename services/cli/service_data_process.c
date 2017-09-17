/* No license because of shit-quality code. */

#include "cli_table_helper.h"
#include "services/cli.h"
#include "port.h"
#include "services/data-process/data-process.h"
#include "services/data-process/sensor-source.h"

static const struct cli_table_cell service_data_process_node_table[] = {
	{.type = TYPE_STRING, .size = 16, .alignment = ALIGN_RIGHT}, /* name */
	{.type = TYPE_STRING, .size = 16, .alignment = ALIGN_RIGHT}, /* type */
	{.type = TYPE_STRING, .size = 20, .alignment = ALIGN_RIGHT}, /* status */
	{.type = TYPE_END}
};



static int32_t service_data_process_print(struct treecli_parser *parser, void *exec_context) {
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
			{.string = "blah"},
			{.string = "blah"},
		});

		node = node->next;
	}

	return 0;
}


static int32_t service_data_process_sensor_source_add(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;

	struct dp_sensor_source *new = malloc(sizeof(struct dp_sensor_source));
	if (new == NULL) {
		return 1;
	}
	dp_sensor_source_init(new, "new-node");
	dp_graph_add_node(&data_process_graph, (void *)new, DP_NODE_SENSOR_SOURCE, "new-node");

	/* Move to the newly created d-subnode. */
	treecli_parser_parse_line(parser, "new-node");

	return 0;
}


static int32_t service_data_process_sensor_source_export(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;

	struct dp_graph_node *node = data_process_graph.nodes;
	while (node != NULL) {
		if (node->type == DP_NODE_SENSOR_SOURCE) {
			module_cli_output("add\r\n", parser->context);
			module_cli_output("new-node\r\n", parser->context);
			char line[50];
			snprintf(line, sizeof(line), "%s export", node->name);
			treecli_parser_parse_line(parser, line);
			module_cli_output("..\r\n", parser->context);
		}
		node = node->next;
	}

	return 0;
}


static int32_t service_data_process_sensor_source_N_export(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;

	size_t index = parser->pos.levels[parser->pos.depth - 1].dnode_index;
	size_t current_index = 0;
	struct dp_graph_node *graph_node = data_process_graph.nodes;
	while (graph_node != NULL) {
		if (graph_node->type == DP_NODE_SENSOR_SOURCE) {
			if (index == current_index) {
				struct dp_sensor_source *s = (struct dp_sensor_source *)graph_node->node;

				char line[50];
				snprintf(line, sizeof(line), "name = %s\r\n", graph_node->name);
				module_cli_output(line, parser->context);
				snprintf(line, sizeof(line), "interval = %u\r\n", (unsigned int)s->interval_ms);
				module_cli_output(line, parser->context);
				snprintf(line, sizeof(line), "enabled = %u\r\n", (unsigned int)s->running);
				module_cli_output(line, parser->context);

				const char *name = NULL;
				enum interface_directory_type t;
				for (size_t i = 0; (t = interface_directory_get_type(&interfaces, i)) != INTERFACE_TYPE_NONE; i++) {
					if (t == INTERFACE_TYPE_SENSOR) {
						ISensor *sensor = (ISensor *)interface_directory_get_interface(&interfaces, i);
						if (sensor == s->sensor) {
							name = interface_directory_get_name(&interfaces, i);
							break;
						}

					}
				}
				if (name != NULL) {
					snprintf(line, sizeof(line), "sensor = %s\r\n", name);
					module_cli_output(line, parser->context);
				}

				return 0;
			}
			current_index++;
		}
		graph_node = graph_node->next;
	}

	return 0;
}


static const struct treecli_command *service_data_process_sensor_source_N_commands[] = {
	&(const struct treecli_command) {
		.name = "export",
		.exec = service_data_process_sensor_source_N_export,
	},
	NULL
};


static int32_t service_data_process_sensor_source_N_name_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)ctx;
	(void)value;

	size_t index = parser->pos.levels[parser->pos.depth - 1].dnode_index;
	size_t current_index = 0;
	struct dp_graph_node *graph_node = data_process_graph.nodes;
	while (graph_node != NULL) {
		if (graph_node->type == DP_NODE_SENSOR_SOURCE) {
			if (index == current_index) {
				strncpy(graph_node->name, buf, len);
				graph_node->name[len] = '\0';

				return 0;
			}
			current_index++;
		}
		graph_node = graph_node->next;
	}

	return 0;
}


static int32_t service_data_process_sensor_source_N_enabled_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)len;
	(void)ctx;
	(void)value;

	size_t index = parser->pos.levels[parser->pos.depth - 1].dnode_index;
	size_t current_index = 0;
	struct dp_graph_node *graph_node = data_process_graph.nodes;
	while (graph_node != NULL) {
		if (graph_node->type == DP_NODE_SENSOR_SOURCE) {
			if (index == current_index) {

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
			current_index++;
		}
		graph_node = graph_node->next;
	}

	return 0;
}


static int32_t service_data_process_sensor_source_N_sensor_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)len;
	(void)ctx;
	(void)value;

	ISensor *sensor = NULL;
	enum interface_directory_type t;
	for (size_t i = 0; (t = interface_directory_get_type(&interfaces, i)) != INTERFACE_TYPE_NONE; i++) {
		if (t == INTERFACE_TYPE_SENSOR) {
			const char *name = interface_directory_get_name(&interfaces, i);
			if (len == strlen(name) && !strncmp(buf, name, len)) {
				sensor = (ISensor *)interface_directory_get_interface(&interfaces, i);
			}
		}
	}

	if (sensor == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, "no sensor interface was found");
		return 1;
	}

	size_t index = parser->pos.levels[parser->pos.depth - 1].dnode_index;
	size_t current_index = 0;
	struct dp_graph_node *graph_node = data_process_graph.nodes;
	while (graph_node != NULL) {
		if (graph_node->type == DP_NODE_SENSOR_SOURCE) {
			if (index == current_index) {
				struct dp_sensor_source *s = (struct dp_sensor_source *)graph_node->node;
				dp_sensor_source_set_sensor(s, sensor);
				return 0;
			}
			current_index++;
		}
		graph_node = graph_node->next;
	}

	return 0;
}


static int32_t service_data_process_sensor_source_N_interval_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)len;
	(void)ctx;
	(void)value;

	size_t index = parser->pos.levels[parser->pos.depth - 1].dnode_index;
	size_t current_index = 0;
	struct dp_graph_node *graph_node = data_process_graph.nodes;
	while (graph_node != NULL) {
		if (graph_node->type == DP_NODE_SENSOR_SOURCE) {
			if (index == current_index) {
				struct dp_sensor_source *s = (struct dp_sensor_source *)graph_node->node;
				uint32_t interval = *(uint32_t *)buf;
				dp_sensor_source_interval(s, interval);
				return 0;
			}
			current_index++;
		}
		graph_node = graph_node->next;
	}

	return 0;
}


static const struct treecli_value *service_data_process_sensor_source_N_values[] = {
	&(const struct treecli_value) {
		.name = "name",
		.set = service_data_process_sensor_source_N_name_set,
		.value_type = TREECLI_VALUE_STR,
	},
	&(const struct treecli_value) {
		.name = "enabled",
		.set = service_data_process_sensor_source_N_enabled_set,
		.value_type = TREECLI_VALUE_BOOL,
	},
	&(const struct treecli_value) {
		.name = "sensor",
		.set = service_data_process_sensor_source_N_sensor_set,
		.value_type = TREECLI_VALUE_STR,
	},
	&(const struct treecli_value) {
		.name = "interval",
		.set = service_data_process_sensor_source_N_interval_set,
		.value_type = TREECLI_VALUE_UINT32,
	},
	NULL
};


static int32_t service_data_process_sensor_source_create(struct treecli_parser *parser, uint32_t index, struct treecli_node *node, void *ctx) {
	(void)ctx;
	(void)parser;

	size_t current_index = 0;
	struct dp_graph_node *graph_node = data_process_graph.nodes;
	while (graph_node != NULL) {
		if (graph_node->type == DP_NODE_SENSOR_SOURCE) {
			if (index == current_index) {
				strcpy(node->name, graph_node->name);
				node->commands = &service_data_process_sensor_source_N_commands;
				node->values = &service_data_process_sensor_source_N_values;
				return 0;
			}
			current_index++;
		}
		graph_node = graph_node->next;
	}
	return -1;
}


static int32_t service_data_process_statistics_node_create(struct treecli_parser *parser, uint32_t index, struct treecli_node *node, void *ctx) {
	(void)ctx;
	(void)parser;

	size_t current_index = 0;
	struct dp_graph_node *graph_node = data_process_graph.nodes;
	while (graph_node != NULL) {
		if (graph_node->type == DP_NODE_STATISTICS_NODE) {
			if (index == current_index) {
				strcpy(node->name, graph_node->name);
				return 0;
			}
			current_index++;
		}
		graph_node = graph_node->next;
	}
	return -1;
}


static int32_t service_data_process_log_sink_create(struct treecli_parser *parser, uint32_t index, struct treecli_node *node, void *ctx) {
	(void)ctx;
	(void)parser;

	size_t current_index = 0;
	struct dp_graph_node *graph_node = data_process_graph.nodes;
	while (graph_node != NULL) {
		if (graph_node->type == DP_NODE_LOG_SINK) {
			if (index == current_index) {
				strcpy(node->name, graph_node->name);
				return 0;
			}
			current_index++;
		}
		graph_node = graph_node->next;
	}
	return -1;
}


