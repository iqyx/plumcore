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
#include <types/ndarray.h>
#include <interfaces/stream.h>

#include "service_plog_router.h"

#define SUB_FILTER_MAX_LEN 32
char sniff_sub_filter[SUB_FILTER_MAX_LEN] = {"#"};

enum sniff_format {
	SNIFF_FORMAT_LOG,
	SNIFF_FORMAT_JSON,
} sniff_format;


static char base85chars[85 + 1] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?&<>()[]{}@%$#";

static void cli_print_base85(ServiceCli *self, const uint8_t *buf, size_t len) {
	while ((len % 4) != 0) {
		len++;
	}

	uint32_t acc = 0;
	for (size_t i = 0; i < len; i++) {
		acc = acc << 8 | buf[i];
		if ((i % 4) == 3) {
			uint32_t d = 85 * 85 * 85 * 85;
			char out[5] = {0};
			for (size_t j = 0; j < 5; j++) {
				out[j] = base85chars[acc / d % 85];
				d /= 85;
			}
			self->stream->vmt->write(self->stream, (const void *)out, sizeof(out));
		}
	}
}


static void cli_print_array_data(ServiceCli *self, NdArray *array) {
	if (array->dtype == DTYPE_BYTE) {
		module_cli_output("\"", self);
		cli_print_base85(self, array->buf, array->asize);
		module_cli_output("\"", self);
	} else if (array->dtype == DTYPE_CHAR) {
		module_cli_output("\"", self);
		self->stream->vmt->write(self->stream, (const void *)array->buf, array->asize);
		module_cli_output("\"", self);
	} else {
		module_cli_output("[", self);
		for (size_t i = 0; i < array->asize; i++) {
			char s[20] = {0};
			switch (array->dtype) {
				case DTYPE_INT8:
					snprintf(s, sizeof(s), "%d", ((int8_t *)array->buf)[i]);
					break;
				case DTYPE_INT16:
					snprintf(s, sizeof(s), "%d", ((int16_t *)array->buf)[i]);
					break;
				case DTYPE_INT32:
					snprintf(s, sizeof(s), "%ld", ((int32_t *)array->buf)[i]);
					break;
				case DTYPE_UINT8:
					snprintf(s, sizeof(s), "%u", ((uint8_t *)array->buf)[i]);
					break;
				case DTYPE_UINT16:
					snprintf(s, sizeof(s), "%u", ((uint16_t *)array->buf)[i]);
					break;
				case DTYPE_UINT32:
					snprintf(s, sizeof(s), "%lu", ((uint32_t *)array->buf)[i]);
					break;
				case DTYPE_FLOAT:
					snprintf(s, sizeof(s), "%.3f", ((float *)array->buf)[i]);
					break;
				default:
					strlcpy(s, "0", sizeof(s));
			}
			s[sizeof(s) - 1] = '\0';
			module_cli_output(s, self);

			if (i != (array->asize - 1)) {
				module_cli_output(", ", self);
			}
		}
		module_cli_output("]", self);
	}
}

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

	module_cli_output("subscribing to topic '", cli);
	module_cli_output(sniff_sub_filter, cli);
	module_cli_output("', output format '", cli);
	if (sniff_format == SNIFF_FORMAT_JSON) {
		module_cli_output("json'\r\n", cli);
	} else {
		module_cli_output("log'\r\n", cli);
	}

	c->vmt->subscribe(c, sniff_sub_filter);
	c->vmt->set_timeout(c, 100);

	module_cli_output("plog-router sniffer started... (press any key to interrupt)\r\n", cli);
	NdArray array;
	ndarray_init_empty(&array, DTYPE_INT8, 4096 + 128);
	while (true) {
		struct timespec ts = {0};
		char topic[32];
		mq_ret_t ret = c->vmt->receive(c, topic, sizeof(topic), &array, &ts);
		if (ret == MQ_RET_OK || ret == MQ_RET_OK_TRUNCATED) {
			if (sniff_format == SNIFF_FORMAT_JSON) {
				struct tm tm = {0};
				localtime_r(&ts.tv_sec, &tm);
				char tstr[150] = {0};
				strftime(tstr, sizeof(tstr) - 1, "{\"ts\": \"%FT%TZ\", \"topic\": \"", &tm);
				module_cli_output(tstr, cli);
				module_cli_output(topic, cli);
				module_cli_output("\", \"type\": \"", cli);
				module_cli_output(ndarray_dtype_str(&array), cli);
				module_cli_output("\", \"size\": ", cli);
				snprintf(tstr, sizeof(tstr), "%u", array.asize);
				module_cli_output(tstr, cli);
				module_cli_output(", \"data\": ", cli);
				cli_print_array_data(cli, &array);
				module_cli_output("}\r\n", cli);

			} else {
				/* Format the time first. */
				struct tm tm = {0};
				localtime_r(&ts.tv_sec, &tm);
				char tstr[150] = {0};
				strftime(tstr, sizeof(tstr) - 1, "[%FT%TZ] ", &tm);
				module_cli_output(tstr, cli);
				module_cli_output("\x1b[33m", cli);
				module_cli_output(topic, cli);
				module_cli_output("\x1b[0m: ", cli);

				ndarray_to_str(&array, tstr, sizeof(tstr));
				module_cli_output(tstr, cli);
				module_cli_output("\r\n", cli);
			}
		}

		/* Check if a key was pressed. Interrupt if yes. */
		uint8_t chr = 0;
		if (cli->stream->vmt->read_timeout(cli->stream, &chr, sizeof(chr), NULL, 0) == STREAM_RET_OK) {
			break;
		}
	}
	ndarray_free(&array);
	c->vmt->close(c);

	return 0;
}


int32_t service_plog_router_filter_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)ctx;
	(void)value;
	(void)parser;

	if (len > SUB_FILTER_MAX_LEN) {
		len = SUB_FILTER_MAX_LEN;
	}
	strncpy(sniff_sub_filter, buf, len);
	sniff_sub_filter[SUB_FILTER_MAX_LEN - 1] = '\0';

	return 1;
}


int32_t service_plog_router_format_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)ctx;
	(void)value;
	ServiceCli *cli = (ServiceCli *)parser->context;

	if (len == 3 && !strncmp(buf, "log", 3)) {
		sniff_format = SNIFF_FORMAT_LOG;
	} else if (len == 4 && !strncmp(buf, "json", 4)) {
		sniff_format = SNIFF_FORMAT_JSON;
	} else {
		module_cli_output("Unknown format (valid are: [log | json])\r\n", cli);
	}

	return 1;
}
