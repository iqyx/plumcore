/*
 * CLI for accessing the plog-router functions
 *
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
#include "port.h"

/* Includes of the service itself. */
#include "services/interfaces/plog/descriptor.h"
#include "services/interfaces/plog/client.h"

#include "service_plog_router.h"


static plog_ret_t service_plog_router_sniff_recv_handler(void *context, const IPlogMessage *msg) {
	ServiceCli *cli = (ServiceCli *)context;

	/* Time of the message reception by the router. */
	module_cli_output("[?] ", cli);
	module_cli_output(msg->topic, cli);

	char num[32] = {0};
	const char *message_types[] = {
		"NONE",
		"LOG",
		"INT32",
		"UINT32",
		"FLOAT",
		"DATA"
	};
	module_cli_output(" ", cli);
	module_cli_output(message_types[msg->type], cli);
	module_cli_output(":", cli);

	switch (msg->type) {
		case IPLOG_MESSAGE_TYPE_FLOAT:
			snprintf(num, sizeof(num), "%d.%03d", (int)msg->content.cfloat, (unsigned int)(msg->content.cfloat / 1000));
			module_cli_output(num, cli);
			break;

		case IPLOG_MESSAGE_TYPE_DATA:
			for (size_t i = 0; i < msg->content.data.len; i++) {
				snprintf(num, sizeof(num), "%02x", msg->content.data.buf[i]);
				module_cli_output(num, cli);
			}

			snprintf(num, sizeof(num), " (size %dB)", msg->content.data.len);
			module_cli_output(num, cli);
			break;

		default:
			break;
	}
	module_cli_output("\r\n", cli);

	return PLOG_RET_OK;
}


int32_t service_plog_router_sniff(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;

	Interface *interface;
	if (iservicelocator_query_name_type(locator, "plog-router", ISERVICELOCATOR_TYPE_PLOG_ROUTER, &interface) != ISERVICELOCATOR_RET_OK) {
		return 1;
	}
	IPlog *iplog = (IPlog *)interface;

	Plog p;
	if (plog_open(&p, iplog) != PLOG_RET_OK) {
		module_cli_output("error: cannot open the plog-router interface\r\n", cli);
		return 1;
	}
	plog_set_recv_handler(&p, service_plog_router_sniff_recv_handler, (void *)cli);
	plog_subscribe(&p, "#");

	module_cli_output("plog-router sniffer started... (press any key to interrupt)\r\n", cli);
	while (true) {
		plog_receive(&p, 500);

		uint8_t chr = 0;
		int16_t read = interface_stream_read_timeout(cli->stream, &chr, 1, 0);
		if (read != 0) {
			break;
		}
	}

	plog_close(&p);


/*
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
*/
	return 0;
}

