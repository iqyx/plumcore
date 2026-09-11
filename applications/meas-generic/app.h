/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * meas-generic generic measurement device application
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include "config.h"

#include <interfaces/conf.h>
#include <configlib.h>

#include "sampling.h"
#if defined(CONFIG_APP_MEAS_GENERIC_TEDS)
	#include "teds.h"
#endif
#if defined(CONFIG_APP_MEAS_GENERIC_NBUS_API)
	#include "api.h"
#endif

typedef enum {
	APP_RET_OK = 0,
	APP_RET_FAILED,
} app_ret_t;

typedef struct {
	/* Periodic sensor sampling, compensation and publishing on the message queue. */
	Sampling sampling;

	/* Application-level configuration tree assembled from the MIB, the application settings and
	 * the compensation coefficients. */
	ConfiglibValue root_conf;
	ConfiglibValue app_conf;
	ConfiglibValue sample_interval_conf;

	#if defined(CONFIG_APP_MEAS_GENERIC_TEDS)
		/* TEDS calibration-data EEPROM discovery on the "teds" 1-Wire bus fanned out by the "teds-mux". */
		Teds teds;
	#endif

	#if defined(CONFIG_APP_MEAS_GENERIC_NBUS_API)
		/* nbus2 API on the backplane stream, exposing the flash partitions, the configuration tree and the
		 * measured values. */
		Api api;
	#endif
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
