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

#include "cli-identity.h"

char identity_device_name[IDENTITY_HOSTNAME_LEN] = "default";
char identity_serial_number[IDENTITY_HOSTNAME_LEN] = "000";


int32_t identity_export(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;

	module_cli_output("name=\"", cli);
	module_cli_output(identity_device_name, cli);
	module_cli_output("\"\r\n", cli);

	module_cli_output("serial-number=\"", cli);
	module_cli_output(identity_serial_number, cli);
	module_cli_output("\"\r\n", cli);

	return 0;
}


int32_t identity_print(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;

	module_cli_output("name: ", cli);
	module_cli_output(identity_device_name, cli);
	module_cli_output("\r\n", cli);

	module_cli_output("serial-number: ", cli);
	module_cli_output(identity_serial_number, cli);
	module_cli_output("\r\n", cli);

	return 0;
}


int32_t identity_generic_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)value;
	ServiceCli *cli = (ServiceCli *)parser->context;

	if (len >= IDENTITY_HOSTNAME_LEN) {
		len = IDENTITY_HOSTNAME_LEN - 1;
	}
	strncpy((char *)ctx, buf, len);
	((char *)ctx)[len] = '\0';

	if (ctx == identity_device_name) {
		cli->sh.hostname = identity_device_name;
	}

	return 0;
}

