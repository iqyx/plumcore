/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * meas-generic nbus2 API on the backplane stream
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include "config.h"

#if defined(CONFIG_APP_MEAS_GENERIC_NBUS_API)

#include <main.h>

#include <interfaces/conf.h>

#include <services/nbus2/nbus2.h>
#include <services/nbus-flash/nbus-flash.h>
#include <services/proto-dgstream/proto-dgstream.h>
#include <services/proto-conf/proto-conf.h>
#if defined(CONFIG_SERVICE_NBUS_MQ_POLL)
	#include <services/nbus-mq-poll/nbus-mq-poll.h>
#endif

/*
 * The API builds an nbus2 stack on the backplane byte stream advertised by the port ("nbus"), framing datagrams with
 * proto-dgstream and exposing the flash partitions (nbus-flash), the application configuration tree (proto-conf) and,
 * optionally, the measured values (nbus-mq-poll). The port only provides the raw stream; framing, MAC and the
 * protocols live here.
 */

typedef enum {
	API_RET_OK = 0,
	API_RET_FAILED,
} api_ret_t;

typedef struct {
	/* nbus2 on the backplane stream discovered from the port, framed with proto-dgstream and exposing nbus-flash
	 * and proto-conf. */
	ProtoDgstream nbus_dgstream;
	Nbus nbus_iface;
	struct nbus_socket *nbus_flash_socket;
	NbusFlash nbus_flash;
	struct nbus_socket *nbus_conf_socket;
	ProtoConf nbus_conf;

	#if defined(CONFIG_SERVICE_NBUS_MQ_POLL)
		/* MQ -> nbus2 poll bridge exposing the measured values to a polling host. */
		struct nbus_socket *nbus_mq_socket;
		NbusMqPoll nbus_mq;
	#endif
} Api;


/**
 * @brief Build the nbus2 API stack on the backplane stream advertised by the port
 *
 * Discovers the "nbus" backplane stream from the service locator and exports the flash partitions and the given
 * configuration tree over nbus2. When nbus-mq-poll is enabled it also bridges the discovered message queue.
 *
 * @param self Preallocated memory for the instance
 * @param conf Application configuration tree to serve over proto-conf
 * @return API_RET_FAILED on error, API_RET_OK otherwise.
 */
api_ret_t api_init(Api *self, Conf *conf);

/**
 * @brief Free the API instance and release all allocated resources
 *
 * @param self Instance of the API
 * @return API_RET_FAILED on error, API_RET_OK otherwise.
 */
api_ret_t api_free(Api *self);

#endif
