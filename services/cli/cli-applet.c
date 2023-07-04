/* SPDX-License-Identifier: BSD-2-Clause
 *
 * CLI for starting applets
 *
 * Copyright (c) 2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

#include <main.h>

/* Common functions and helpers for the CLI service. */
#include "cli_table_helper.h"
#include "cli.h"

/* Helper defines for tree construction. */
#include "services/cli/system_cli_tree.h"

#include "services/interfaces/servicelocator.h"
#include <interfaces/applet.h>

#include "cli-applet.h"

const struct treecli_node *applet_appletN = Node {
	Name "N",
	Commands {
		Command {
			Name "run",
			Exec applet_appletN_run,
		},
		End
	},
};

const struct cli_table_cell applet_table[] = {
	{.type = TYPE_STRING, .size = 20, .alignment = ALIGN_LEFT},
	{.type = TYPE_STRING, .size = 32, .alignment = ALIGN_LEFT},
	{.type = TYPE_END}
};


int32_t applet_print(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;

	table_print_header(cli->stream, applet_table, (const char *[]){
		"Applet",
		"Description",
	});
	table_print_row_separator(cli->stream, applet_table);

	Applet *applet = NULL;
	for (size_t i = 0; (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_APPLET, i, (Interface **)&applet)) == ISERVICELOCATOR_RET_OK; i++) {
		const char *name = NULL;
		iservicelocator_get_name(locator, (Interface *)applet, &name);

		table_print_row(cli->stream, applet_table, (const union cli_table_cell_content []) {
			{.string = name},
			{.string = applet->name},
		});
	}

	return 0;
}


int32_t applet_appletN_create(struct treecli_parser *parser, uint32_t index, struct treecli_node *node, void *ctx) {
	(void)parser;
	(void)ctx;

	Interface *interface;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_APPLET, index, &interface) == ISERVICELOCATOR_RET_OK) {
		const char *name = "";
		iservicelocator_get_name(locator, interface, &name);
		if (node->name != NULL) {
			strcpy(node->name, name);
		}
		node->commands = applet_appletN->commands;
		// node->values = files_fsN->values;
		// node->subnodes = files_fsN->subnodes;
		return 0;
	}
	return -1;
}


int32_t applet_appletN_run(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;

	/* Find the right filesystem */
	Applet *applet = NULL;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_APPLET, DNODE_INDEX(parser, -1), (Interface **)&applet) != ISERVICELOCATOR_RET_OK) {
		return 1;
	}

	module_cli_output("running applet ", cli);
	module_cli_output(applet->name, cli);
	module_cli_output("\r\n", cli);

	struct applet_args args = {0};
	args.logger = system_log;
	args.stdio = cli->stream;
	if (applet->executable.compiled.main != NULL) {
		applet->executable.compiled.main(applet, &args);
	}

	return 0;
}


