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
#include <time.h>
#include <math.h>

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
#include "port.h"

#include <interfaces/mq.h>

#include "service_plog_router.h"


int32_t service_plog_router_sniff(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;

	Mq *mq = NULL;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_MQ, 0, (Interface **)&mq) != ISERVICELOCATOR_RET_OK) {
		return 1;
	}

	MqClient *c = mq->vmt->open(mq);
	if (c == NULL) {
		module_cli_output("error: cannot open MqClient\r\n", cli);
		return -2;
	}
	c->vmt->subscribe(c, "#");
	c->vmt->set_timeout(c, 500);
	
	module_cli_output("plog-router sniffer started... (press any key to interrupt)\r\n", cli);
	while (true) {
		/* Allocate a ndarray on the stack, do not assign any buffer space
		 * because we are not interested in the data. */
		struct ndarray ndarray = {0};
		struct timespec ts = {0};
		char topic[32];
		mq_ret_t ret = c->vmt->receive(c, topic, sizeof(topic), &ndarray, &ts);
		if (ret == MQ_RET_OK || ret == MQ_RET_OK_TRUNCATED) {
			/* Format the time first. */
			struct tm tm = {0};
			localtime_r(&ts.tv_sec, &tm);
			char tstr[35] = {0};
			strftime(tstr, sizeof(tstr) - 1, "[%FT%TZ] ", &tm);
			module_cli_output(tstr, cli);

			module_cli_output(topic, cli);

			snprintf(tstr, sizeof(tstr) - 1,  ": [data array_size = %u]\r\n", ndarray.array_size);
			tstr[sizeof(tstr) - 1] = '\0';
			module_cli_output(tstr, cli);
		}

		/* Check if a key was pressed. Interrupt if yes. */
		uint8_t chr = 0;
		int16_t read = interface_stream_read_timeout(cli->stream, &chr, 1, 0);
		if (read != 0) {
			break;
		}
	}
	c->vmt->close(c);

	return 0;
}

