/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * nwdaq-inc5 inclination measurement application
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include "config.h"

#include <interfaces/sensor.h>
#include <interfaces/mq.h>
#include <interfaces/conf.h>
#include <configlib.h>

#include <services/mq-compensation/mq-compensation.h>
#include <services/nbus2/nbus2.h>
#include <services/nbus-flash/nbus-flash.h>
#include <services/proto-dgtext/proto-dgtext.h>
#include <services/proto-conf/proto-conf.h>

typedef enum {
	APP_RET_OK = 0,
	APP_RET_FAILED,
} app_ret_t;

/* Measurement channels the application discovers and, if present, publishes and compensates.
 * A port advertises only the sensors it actually has (eg. the single-axis inc5 exposes just
 * inc_x), so channels missing from the service locator are silently skipped. */
#define APP_CHANNEL_COUNT 4

typedef struct {
	Mq *mq;
	MqClient *mqc;

	/* Discovered measurement channels, indexed the same as the static channel table in app.c.
	 * A NULL entry means the sensor was not advertised by the port. */
	Sensor *channels[APP_CHANNEL_COUNT];

	/* Board temperature feeding the compensation polynomial (optional). */
	Sensor *temp_x;

	/* Polynomial offset/gain/temperature compensation of the raw measured values. The
	 * compensated results are read back over a dedicated client and logged. */
	MqCompensation comp;
	MqClient *log_mqc;

	/* Application-level configuration tree. */
	ConfiglibValue root_conf;

	/* API: nbus2 over proto-dgtext on the serial console, exposing nbus-flash and proto-conf. */
	Stream *console;
	ProtoDgtext console_dgtext;
	Nbus console_nbus;
	struct nbus_socket *console_proto_flash_socket;
	NbusFlash console_proto_flash;
	struct nbus_socket *console_proto_conf_socket;
	ProtoConf console_proto_conf;

	TaskHandle_t task;
	TaskHandle_t log_task;
} App;


/**
 * @brief Prepare the new instance, allocate resources and start
 *        the application task.
 *
 * @param self Preallocated memory for the instance
 * @return APP_RET_FAILED if the application initialization failed,
 *         APP_RET_OK otherwise
 */
app_ret_t app_init(App *self);

/**
 * @brief Free the application instance, release all allocated resources
 *
 * @param self Instance of the application
 * @return APP_RET_FAILED on error,
 *         APP_RET_OK otherwise.
 */
app_ret_t app_free(App *self);
