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

#include "config_export.h"


int32_t default_export(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;

	/* Now traverse all subnodes at the current position. */
	const struct treecli_node *node = NULL;

	if (parser->pos.depth == 0) {
		node = parser->top;
	} else {
		node = parser->pos.levels[parser->pos.depth - 1].node;
	}

	const struct treecli_node *n;
	for (size_t i = 0; (n = (*(node->subnodes))[i]) != NULL; i++) {

		/* Print the current path. */
		module_cli_output("/ ", cli);
		treecli_parser_pos_print(parser, true);
		module_cli_output(" ", cli);
		module_cli_output(n->name, cli);
		module_cli_output("\r\n", cli);

		char line[50];
		snprintf(line, sizeof(line), "%s export", n->name);
		treecli_parser_parse_line(parser, line);
	}

	return 0;
}

