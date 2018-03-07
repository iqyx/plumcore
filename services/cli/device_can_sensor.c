/*
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

/* Common functions and helpers for the CLI service. */
#include "cli_table_helper.h"
#include "services/cli.h"

/* Helper defines for tree construction. */
#include "services/cli/system_cli_tree.h"

#include "services/interfaces/servicelocator.h"
#include "uhal/interfaces/can.h"
#include "uhal/interfaces/ccan.h"
#include "port.h"

#include "device_can_sensor.h"
#include "modules/sensor_over_can.h"


const struct cli_table_cell device_can_sensor_table[] = {
	{.type = TYPE_STRING, .size = 24, .alignment = ALIGN_RIGHT}, /* driver instance */
	{.type = TYPE_STRING, .size = 16, .alignment = ALIGN_RIGHT}, /* running */
	{.type = TYPE_STRING, .size = 16, .alignment = ALIGN_RIGHT}, /* value status */
	{.type = TYPE_UINT32, .size = 16, .alignment = ALIGN_RIGHT}, /* received values */
	{.type = TYPE_END}
};


const struct treecli_node *device_can_sensor_can_sensorN = Node {
	Name "N",
	Commands {
		Command {
			Name "sniff",
			Exec device_can_sensor_can_sensorN_sniff,
		},
		End
	},
	Values {
		Value {
			Name "enabled",
			.set = device_can_sensor_can_sensorN_enabled_set,
			Type TREECLI_VALUE_BOOL,
		},
		Value {
			Name "id",
			.set = device_can_sensor_can_sensorN_id_set,
			Type TREECLI_VALUE_UINT32,
		},
		Value {
			Name "can",
			.set = device_can_sensor_can_sensorN_can_set,
			Type TREECLI_VALUE_STR,
		},
		Value {
			Name "name",
			.set = device_can_sensor_can_sensorN_name_set,
			Type TREECLI_VALUE_STR,
		},
		Value {
			Name "quantity-name",
			.set = device_can_sensor_can_sensorN_quantity_name_set,
			Type TREECLI_VALUE_STR,
		},
		Value {
			Name "quantity-symbol",
			.set = device_can_sensor_can_sensorN_quantity_symbol_set,
			Type TREECLI_VALUE_STR,
		},
		Value {
			Name "unit-name",
			.set = device_can_sensor_can_sensorN_unit_name_set,
			Type TREECLI_VALUE_STR,
		},
		Value {
			Name "unit-symbol",
			.set = device_can_sensor_can_sensorN_unit_symbol_set,
			Type TREECLI_VALUE_STR,
		},
		End
	},
};


int32_t device_can_sensor_print(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;

	table_print_header(cli->stream, device_can_sensor_table, (const char *[]){
		"Driver instance name",
		"Status",
		"Value status",
		"Values",
	});
	table_print_row_separator(cli->stream, device_can_sensor_table);

	Interface *interface;
	for (size_t i = 0; (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_CAN_SENSOR_DRIVER, i, &interface)) != ISERVICELOCATOR_RET_FAILED; i++) {
		const char *name = "";
		iservicelocator_get_name(locator, interface, &name);
		SensorOverCan *sensor = (SensorOverCan *)interface;

		const char *status = "stopped";
		if (sensor->running) {
			status = "running";
		}

		const char *value_status = "stalled";
		if (sensor->value_status == SENSOR_OVER_CAN_VALUE_STATUS_FRESH) {
			value_status = "fresh";
		}

		table_print_row(cli->stream, device_can_sensor_table, (const union cli_table_cell_content []) {
			{.string = name},
			{.string = status},
			{.string = value_status},
			{.uint32 = sensor->received_values},
		});
	}
	return 0;
}


int32_t device_can_sensor_export(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;

	Interface *interface;
	for (size_t i = 0; (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_CAN_SENSOR_DRIVER, i, &interface)) != ISERVICELOCATOR_RET_FAILED; i++) {
		SensorOverCan *sensor = (SensorOverCan *)interface;

		module_cli_output("add new-driver\r\n", cli);

		char line[50];

		const char *name = "";
		iservicelocator_get_name(locator, interface, &name);
		snprintf(line, sizeof(line), "name = %s ", name);
		module_cli_output(line, cli);

		snprintf(line, sizeof(line), "id = %lu ", sensor->can_id);
		module_cli_output(line, cli);

		name = NULL;
		iservicelocator_get_name(locator, &sensor->can->interface, &name);
		if (name != NULL) {
			snprintf(line, sizeof(line), "can = %s ", name);
			module_cli_output(line, cli);
		}

		module_cli_output("\r\n", cli);

		snprintf(line, sizeof(line), "quantity-name = \"%s\" ", sensor->quantity_name);
		module_cli_output(line, cli);
		snprintf(line, sizeof(line), "quantity-symbol = \"%s\" ", sensor->quantity_symbol);
		module_cli_output(line, cli);
		snprintf(line, sizeof(line), "unit-name = \"%s\" ", sensor->unit_name);
		module_cli_output(line, cli);
		snprintf(line, sizeof(line), "unit-symbol = \"%s\" ", sensor->unit_symbol);
		module_cli_output(line, cli);

		module_cli_output("\r\n", cli);

		snprintf(line, sizeof(line), "enabled = %s ", sensor->running ? "1" : "0");
		module_cli_output(line, cli);

		module_cli_output("\r\n..\r\n", cli);
	}
	return 0;
}


int32_t device_can_sensor_add(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;

	SensorOverCan *sensor = malloc(sizeof(SensorOverCan));
	if (sensor == NULL) {
		module_cli_output("error: cannot allocate memory\r\n", cli);
		return 1;
	}
	if (sensor_over_can_init(sensor) != SENSOR_OVER_CAN_RET_OK) {
		module_cli_output("error: cannot initialize new driver\r\n", cli);
		return 1;
	}

	iservicelocator_add(
		locator,
		ISERVICELOCATOR_TYPE_CAN_SENSOR_DRIVER,
		&sensor->interface,
		"new-driver"
	);

	return 0;
}


int32_t device_can_sensor_can_sensorN_create(struct treecli_parser *parser, uint32_t index, struct treecli_node *node, void *ctx) {
	(void)parser;
	(void)ctx;

	Interface *interface;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_CAN_SENSOR_DRIVER, index, &interface) == ISERVICELOCATOR_RET_OK) {
		const char *name = "";
		iservicelocator_get_name(locator, interface, &name);
		if (node->name != NULL) {
			strcpy(node->name, name);
		}
		node->commands = device_can_sensor_can_sensorN->commands;
		node->values = device_can_sensor_can_sensorN->values;
		node->subnodes = device_can_sensor_can_sensorN->subnodes;
		return 0;
	}
	return -1;
}


int32_t device_can_sensor_can_sensorN_sniff(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;

	Interface *interface;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_CAN_SENSOR_DRIVER, DNODE_INDEX(parser, -1), &interface) != ISERVICELOCATOR_RET_OK) {
		return 1;
	}
	SensorOverCan *sensor = (SensorOverCan *)interface;


	return 0;
}


int32_t device_can_sensor_can_sensorN_enabled_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)ctx;
	(void)value;
	(void)len;
	ServiceCli *cli = (ServiceCli *)parser->context;

	Interface *interface;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_CAN_SENSOR_DRIVER, DNODE_INDEX(parser, -1), &interface) != ISERVICELOCATOR_RET_OK) {
		return 1;
	}
	SensorOverCan *sensor = (SensorOverCan *)interface;

	bool e = *(bool *)buf;
	if (e != sensor->running) {
		if (e) {
			if (sensor_over_can_start(sensor) != SENSOR_OVER_CAN_RET_OK) {
				module_cli_output("cannot start the driver instance\r\n", cli);
				return 1;
			}
			module_cli_output("driver instance started\r\n", cli);
		} else {
			if (sensor_over_can_stop(sensor) != SENSOR_OVER_CAN_RET_OK) {
				module_cli_output("cannot stop the driver instance\r\n", cli);
				return 1;
			}
			module_cli_output("driver instance stopped\r\n", cli);
		}
	}

	return 0;
}


int32_t device_can_sensor_can_sensorN_can_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)ctx;
	(void)value;
	(void)len;
	ServiceCli *cli = (ServiceCli *)parser->context;

	Interface *interface;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_CAN_SENSOR_DRIVER, DNODE_INDEX(parser, -1), &interface) != ISERVICELOCATOR_RET_OK) {
		return 1;
	}
	SensorOverCan *sensor = (SensorOverCan *)interface;

	char *name = malloc(len + 1);
	if (name == NULL) {
		module_cli_output("error: cannot allocate memory\r\n", cli);
		return 1;
	}
	memcpy(name, buf, len);
	name[len] = '\0';
	Interface *can_interface;
	if (iservicelocator_query_name_type(locator, name, ISERVICELOCATOR_TYPE_CAN, &can_interface) != ISERVICELOCATOR_RET_OK) {
		module_cli_output("error: cannot find the selected CAN interface\r\n", cli);
		free(name);
		return 1;
	}

	if (sensor_over_can_set_can_bus(sensor, (ICan *)can_interface) != SENSOR_OVER_CAN_RET_OK) {
		module_cli_output("error: cannot set the selected CAN interface\r\n", cli);
		free(name);
		return 1;
	}

	free(name);
	return 0;
}


int32_t device_can_sensor_can_sensorN_id_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)ctx;
	(void)value;
	(void)len;
	ServiceCli *cli = (ServiceCli *)parser->context;

	Interface *interface;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_CAN_SENSOR_DRIVER, DNODE_INDEX(parser, -1), &interface) != ISERVICELOCATOR_RET_OK) {
		return 1;
	}
	SensorOverCan *sensor = (SensorOverCan *)interface;

	uint32_t id = *(uint32_t *)buf;
	if (sensor_over_can_set_id(sensor, id) != SENSOR_OVER_CAN_RET_OK) {
		module_cli_output("error: cannot set CAN ID\r\n", cli);
		return 1;
	}

	return 0;
}


int32_t device_can_sensor_can_sensorN_name_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)ctx;
	(void)value;
	(void)len;
	ServiceCli *cli = (ServiceCli *)parser->context;

	Interface *interface;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_CAN_SENSOR_DRIVER, DNODE_INDEX(parser, -1), &interface) != ISERVICELOCATOR_RET_OK) {
		return 1;
	}
	SensorOverCan *sensor = (SensorOverCan *)interface;

	char *name = malloc(len + 1);
	if (name == NULL) {
		module_cli_output("error: cannot allocate memory\r\n", cli);
		return 1;
	}
	memcpy(name, buf, len);
	name[len] = '\0';

	if (sensor_over_can_set_name(sensor, name) != SENSOR_OVER_CAN_RET_OK) {
		module_cli_output("error: cannot set driver instance name\r\n", cli);
		free(name);
		return 1;
	}

	free(name);
	return 0;
}


int32_t device_can_sensor_can_sensorN_quantity_name_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)ctx;
	(void)value;
	(void)len;
	ServiceCli *cli = (ServiceCli *)parser->context;

	Interface *interface;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_CAN_SENSOR_DRIVER, DNODE_INDEX(parser, -1), &interface) != ISERVICELOCATOR_RET_OK) {
		return 1;
	}
	SensorOverCan *sensor = (SensorOverCan *)interface;

	char *name = malloc(len + 1);
	if (name == NULL) {
		module_cli_output("error: cannot allocate memory\r\n", cli);
		return 1;
	}
	memcpy(name, buf, len);
	name[len] = '\0';

	if (sensor_over_can_set_quantity_name(sensor, name) != SENSOR_OVER_CAN_RET_OK) {
		module_cli_output("error: cannot set sensor quantity name\r\n", cli);
		free(name);
		return 1;
	}

	free(name);
	return 0;
}


int32_t device_can_sensor_can_sensorN_quantity_symbol_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)ctx;
	(void)value;
	(void)len;
	ServiceCli *cli = (ServiceCli *)parser->context;

	Interface *interface;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_CAN_SENSOR_DRIVER, DNODE_INDEX(parser, -1), &interface) != ISERVICELOCATOR_RET_OK) {
		return 1;
	}
	SensorOverCan *sensor = (SensorOverCan *)interface;

	char *name = malloc(len + 1);
	if (name == NULL) {
		module_cli_output("error: cannot allocate memory\r\n", cli);
		return 1;
	}
	memcpy(name, buf, len);
	name[len] = '\0';

	if (sensor_over_can_set_quantity_symbol(sensor, name) != SENSOR_OVER_CAN_RET_OK) {
		module_cli_output("error: cannot set sensor quantity symbol\r\n", cli);
		free(name);
		return 1;
	}

	free(name);
	return 0;
}


int32_t device_can_sensor_can_sensorN_unit_name_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)ctx;
	(void)value;
	(void)len;
	ServiceCli *cli = (ServiceCli *)parser->context;

	Interface *interface;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_CAN_SENSOR_DRIVER, DNODE_INDEX(parser, -1), &interface) != ISERVICELOCATOR_RET_OK) {
		return 1;
	}
	SensorOverCan *sensor = (SensorOverCan *)interface;

	char *name = malloc(len + 1);
	if (name == NULL) {
		module_cli_output("error: cannot allocate memory\r\n", cli);
		return 1;
	}
	memcpy(name, buf, len);
	name[len] = '\0';

	if (sensor_over_can_set_unit_name(sensor, name) != SENSOR_OVER_CAN_RET_OK) {
		module_cli_output("error: cannot set sensor unit name\r\n", cli);
		free(name);
		return 1;
	}

	free(name);
	return 0;
}


int32_t device_can_sensor_can_sensorN_unit_symbol_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)ctx;
	(void)value;
	(void)len;
	ServiceCli *cli = (ServiceCli *)parser->context;

	Interface *interface;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_CAN_SENSOR_DRIVER, DNODE_INDEX(parser, -1), &interface) != ISERVICELOCATOR_RET_OK) {
		return 1;
	}
	SensorOverCan *sensor = (SensorOverCan *)interface;

	char *name = malloc(len + 1);
	if (name == NULL) {
		module_cli_output("error: cannot allocate memory\r\n", cli);
		return 1;
	}
	memcpy(name, buf, len);
	name[len] = '\0';

	if (sensor_over_can_set_unit_symbol(sensor, name) != SENSOR_OVER_CAN_RET_OK) {
		module_cli_output("error: cannot set sensor unit symbol\r\n", cli);
		free(name);
		return 1;
	}

	free(name);
	return 0;
}
