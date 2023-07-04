/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Output adc-composite temperature coeff calibration data as a CSV
 *
 * Copyright (c) 2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <main.h>
#include <interfaces/applet.h>
#include <interfaces/mq.h>
#include <interfaces/servicelocator.h>

#include "tempco-cal.h"

#define MODULE_NAME "tempco-cal"


static applet_ret_t tempco_calibration_main(Applet *self, struct applet_args *args) {
	(void)self;
	if (args->logger == NULL || args->stdio == NULL) {
		/* We need a logger and a stdio stream. */
		return APPLET_RET_FAILED;
	}

	Mq *mq = NULL;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_MQ, 0, (Interface **)&mq) != ISERVICELOCATOR_RET_OK) {
		u_log(args->logger, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot find a MQ"));
		return APPLET_RET_FAILED;
	}

	MqClient *c = mq->vmt->open(mq);
	if (c == NULL) {
		u_log(args->logger, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot open a MQ client"));
		return APPLET_RET_FAILED;
	}
	u_log(args->logger, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("message queue opened (client 0x%08x)"), c);

	u_log(args->logger, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("subscribing topic '%s'"), SUBSCRIBE_TOPIC);
	c->vmt->subscribe(c, SUBSCRIBE_TOPIC);
	c->vmt->set_timeout(c, 100);

	u_log(args->logger, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("(press any key to interrupt the applet)"));
	int32_t line = -1;

	NdArray array;
	/* Allocate reasonable size to receive floats :> */
	ndarray_init_empty(&array, DTYPE_INT8, 16);
	while (true) {
		struct timespec ts = {0};
		char topic[32];
		mq_ret_t ret = c->vmt->receive(c, topic, sizeof(topic), &array, &ts);
		if (ret == MQ_RET_OK || ret == MQ_RET_OK_TRUNCATED) {
			char tstr[150] = {0};

			/* Prepend newline and time information only when the channel group starts */
			if (!strcmp(topic, FIRST_TOPIC)) {
				line++;

				struct tm tm = {0};
				localtime_r(&ts.tv_sec, &tm);
				switch (line) {
					case -1:
						break;
					case 0:
						snprintf(tstr, sizeof(tstr), "time ");
						break;
					default:
						snprintf(tstr, sizeof(tstr), "\r\n%lu ", (uint32_t)ts.tv_sec);
						// strftime(tstr, sizeof(tstr) - 1, "\r\n\"%FT%TZ\" ", &tm);
						break;
				}
				args->stdio->vmt->write(args->stdio, tstr, strlen(tstr));
			}

			ndarray_value_to_str(&array, 0, tstr, sizeof(tstr));
			switch (line) {
				case -1:
					break;
				case 0:
					args->stdio->vmt->write(args->stdio, topic, strlen(topic));
					args->stdio->vmt->write(args->stdio, " ", 1);
					break;
				default:
					args->stdio->vmt->write(args->stdio, tstr, strlen(tstr));
					args->stdio->vmt->write(args->stdio, " ", 1);
					break;
			}
		}

		/* Check if a key was pressed. Interrupt if yes. */
		uint8_t chr = 0;
		if (args->stdio->vmt->read_timeout(args->stdio, &chr, sizeof(chr), NULL, 0) == STREAM_RET_OK) {
			break;
		}
	}
	ndarray_free(&array);
	c->vmt->close(c);

	return APPLET_RET_OK;
}


const Applet tempco_calibration = {
	.executable.compiled = {
		.main = tempco_calibration_main
	},
	.name = "tempco-calibration",
	.help = "Output temperature and processed channel data as a CSV to ease temperature coefficient computation & calibration"
};
