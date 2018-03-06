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

#include "device_can.h"

/**
 * Tables for printing.
 */

const struct cli_table_cell device_can_table[] = {
	{.type = TYPE_STRING, .size = 16, .alignment = ALIGN_RIGHT}, /* name */
	{.type = TYPE_STRING, .size = 16, .alignment = ALIGN_RIGHT}, /* status */
	{.type = TYPE_END}
};


const struct treecli_node *device_can_canN = Node {
	Name "N",
	Commands {
		Command {
			Name "sniff",
			Exec device_can_canN_sniff,
		},
		End
	},
};


/**
 * Informational and printing commands.
 */

int32_t device_can_print(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;

	table_print_header(cli->stream, device_can_table, (const char *[]){
		"Device",
		"status",
	});
	table_print_row_separator(cli->stream, device_can_table);

	Interface *interface;
	for (size_t i = 0; (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_CAN, i, &interface)) != ISERVICELOCATOR_RET_FAILED; i++) {
		const char *name = "";
		iservicelocator_get_name(locator, interface, &name);

		table_print_row(cli->stream, device_can_table, (const union cli_table_cell_content []) {
			{.string = name},
			{.string = ""},
		});
	}
	return 0;
}


int32_t device_can_canN_create(struct treecli_parser *parser, uint32_t index, struct treecli_node *node, void *ctx) {
	(void)parser;
	(void)ctx;

	Interface *interface;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_CAN, index, &interface) == ISERVICELOCATOR_RET_OK) {
		const char *name = "";
		iservicelocator_get_name(locator, interface, &name);
		if (node->name != NULL) {
			strcpy(node->name, name);
		}
		node->commands = device_can_canN->commands;
		node->values = device_can_canN->values;
		node->subnodes = device_can_canN->subnodes;
		return 0;
	}
	return -1;
}


int32_t device_can_canN_sniff(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;

	Interface *interface;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_CAN, DNODE_INDEX(parser, -1), &interface) != ISERVICELOCATOR_RET_OK) {
		return 1;
	}
	ICan *ican = (ICan *)interface;

	CCan ccan;
	if (ccan_open(&ccan, ican) != CCAN_RET_OK) {
		module_cli_output("Cannot open CAN interface\r\n", cli);
		return 1;
	}

	module_cli_output("CAN sniffer started... (press any key to interrupt)\r\n", cli);

	while (1) {
		uint8_t buf[8] = {0};
		size_t len = 0;
		uint32_t id = 0;
		if (ccan_receive(&ccan, buf, &len, &id, 1000) == CCAN_RET_OK) {
			char data_str[8 * 6];
			snprintf(
				data_str,
				sizeof(data_str),
				"0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x",
				buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]
			);

			char id_str[30];
			snprintf(id_str, sizeof(id_str), "id = %08lx (%lu), ", id, id);

			char len_str[20];
			snprintf(len_str, sizeof(len_str), "len = %u, data = ", len);

			module_cli_output(id_str, cli);
			module_cli_output(len_str, cli);
			module_cli_output(data_str, cli);
			module_cli_output("\r\n", cli);
		}

		uint8_t chr = 0;
		int16_t read = interface_stream_read_timeout(cli->stream, &chr, 1, 0);
		if (read != 0) {
			break;
		}
	}

	ccan_close(&ccan);

	return 0;
}
