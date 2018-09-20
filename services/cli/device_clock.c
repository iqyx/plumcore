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
#include <time.h>

#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"

/* Common functions and helpers for the CLI service. */
#include "cli_table_helper.h"
#include "services/cli.h"

/* Helper defines for tree construction. */
#include "services/cli/system_cli_tree.h"

#include "interfaces/servicelocator.h"
#include "interfaces/clock/descriptor.h"

#include "device_clock.h"


const char *clock_source_str[] = {
	"unknown",
};


const char *clock_status_str[] = {
	"unknown",
};


enum clock_value_sel {
	CLOCK_VALUE_SEL_YEAR,
	CLOCK_VALUE_SEL_MONTH,
	CLOCK_VALUE_SEL_MDAY,
	CLOCK_VALUE_SEL_HOUR,
	CLOCK_VALUE_SEL_MIN,
	CLOCK_VALUE_SEL_SEC,
	CLOCK_VALUE_SEL_USEC,
};


const struct cli_table_cell device_clock_table[] = {
	{.type = TYPE_STRING, .size = 20, .alignment = ALIGN_RIGHT}, /* clock name */
	{.type = TYPE_STRING, .size = 32, .alignment = ALIGN_RIGHT}, /* current date and time */
	{.type = TYPE_STRING, .size = 16, .alignment = ALIGN_RIGHT}, /* clock status */
	{.type = TYPE_STRING, .size = 16, .alignment = ALIGN_RIGHT}, /* clock source */
	{.type = TYPE_END}
};


const struct treecli_node *device_clock_clockN = Node {
	Name "N",
	Commands {
		Command {
			Name "print",
		},
		End
	},
	Values {
		Value {
			Name "year",
			.set = device_clock_clockN_value_set,
			/* Abuse the get_set_context field to select the correct value to manipulate. */
			.get_set_context = (void *)CLOCK_VALUE_SEL_YEAR,
			Type TREECLI_VALUE_UINT32,
		},
		Value {
			Name "month",
			.set = device_clock_clockN_value_set,
			.get_set_context = (void *)CLOCK_VALUE_SEL_MONTH,
			Type TREECLI_VALUE_UINT32,
		},
		Value {
			Name "day",
			.set = device_clock_clockN_value_set,
			.get_set_context = (void *)CLOCK_VALUE_SEL_MDAY,
			Type TREECLI_VALUE_UINT32,
		},
		Value {
			Name "hour",
			.set = device_clock_clockN_value_set,
			.get_set_context = (void *)CLOCK_VALUE_SEL_HOUR,
			Type TREECLI_VALUE_UINT32,
		},
		Value {
			Name "min",
			.set = device_clock_clockN_value_set,
			.get_set_context = (void *)CLOCK_VALUE_SEL_MIN,
			Type TREECLI_VALUE_UINT32,
		},
		Value {
			Name "sec",
			.set = device_clock_clockN_value_set,
			.get_set_context = (void *)CLOCK_VALUE_SEL_SEC,
			Type TREECLI_VALUE_UINT32,
		},
		Value {
			Name "usec",
			.set = device_clock_clockN_value_set,
			.get_set_context = (void *)CLOCK_VALUE_SEL_USEC,
			Type TREECLI_VALUE_UINT32,
		},
		End
	},
};


int32_t device_clock_print(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;

	table_print_header(cli->stream, device_clock_table, (const char *[]){
		"Clock name",
		"Current Date&Time",
		"Status",
		"Source",
	});
	table_print_row_separator(cli->stream, device_clock_table);

	Interface *interface;
	for (size_t i = 0; (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_CLOCK, i, &interface)) == ISERVICELOCATOR_RET_OK; i++) {
		const char *name = "?";
		iservicelocator_get_name(locator, interface, &name);
		IClock *iclock = (IClock *)interface;

		struct timespec now = {0};
		iclock_get(iclock, &now);
		struct tm ts = {0};
		localtime_r(&now.tv_sec, &ts);

		char tstr[100];
		strftime(tstr, sizeof(tstr), "%FT%TZ", &ts);

		enum iclock_source c_source = {0};
		iclock_get_source(iclock, &c_source);

		enum iclock_status c_status = {0};
		iclock_get_status(iclock, &c_status);

		table_print_row(cli->stream, device_clock_table, (const union cli_table_cell_content []) {
			{.string = name},
			{.string = tstr},
			{.string = clock_status_str[c_status]},
			{.string = clock_source_str[c_source]},
		});
	}
	return 0;
}


int32_t device_clock_clockN_create(struct treecli_parser *parser, uint32_t index, struct treecli_node *node, void *ctx) {
	(void)parser;
	(void)ctx;

	Interface *interface;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_CLOCK, index, &interface) == ISERVICELOCATOR_RET_OK) {
		const char *name = "";
		iservicelocator_get_name(locator, interface, &name);
		if (node->name != NULL) {
			strcpy(node->name, name);
		}
		node->commands = device_clock_clockN->commands;
		node->values = device_clock_clockN->values;
		node->subnodes = device_clock_clockN->subnodes;
		return 0;
	}
	return -1;
}


int32_t device_clock_clockN_value_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)len;
	(void)value;
	ServiceCli *cli = (ServiceCli *)parser->context;

	/* Find the clock first. */
	Interface *interface;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_CLOCK, DNODE_INDEX(parser, -1), &interface) != ISERVICELOCATOR_RET_OK) {
		return 1;
	}
	IClock *iclock = (IClock *)interface;

	/* Convert the current UNIX timestamp value to individual date&time items. */
	struct timespec now = {0};
	iclock_get(iclock, &now);
	struct tm ts = {0};
	localtime_r(&now.tv_sec, &ts);

	/* Determine which value is selected for manipulation. */
	uint32_t val = *(uint32_t *)buf;
	switch ((enum clock_value_sel)ctx) {
		case CLOCK_VALUE_SEL_YEAR: {
			if (val < 1900 || val > 2038) {
				goto err_value;
			}
			ts.tm_year = val - 1900;
			break;
		}

		case CLOCK_VALUE_SEL_MONTH: {
			if (val < 1 || val > 12) {
				goto err_value;
			}
			ts.tm_mon = val - 1;
			break;
		}

		case CLOCK_VALUE_SEL_MDAY: {
			if (val < 1 || val > 31) {
				goto err_value;
			}
			ts.tm_mday = val;
			break;
		}

		case CLOCK_VALUE_SEL_HOUR: {
			if (val > 23) {
				goto err_value;
			}
			ts.tm_hour = val;
			break;
		}

		case CLOCK_VALUE_SEL_MIN: {
			if (val > 59) {
				goto err_value;
			}
			ts.tm_min = val;
			break;
		}

		case CLOCK_VALUE_SEL_SEC: {
			if (val > 59) {
				goto err_value;
			}
			ts.tm_sec = val;
			break;
		}

		case CLOCK_VALUE_SEL_USEC: {
			if (val > 999999) {
				goto err_value;
			}
			now.tv_nsec = val * 1000;
			break;
		}

		default:
			return -1;
	}

	/* Convert the modified date&time to UNIX timestamp value. */
	now.tv_sec = mktime(&ts);
	iclock_set(iclock, &now);
	return 0;

err_value:
	module_cli_output("invalid value\r\n", cli);
	return -2;

}


