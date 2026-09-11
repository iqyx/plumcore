/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * meas-generic generic measurement device application
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <main.h>
#include "app.h"

#include <interfaces/servicelocator.h>
#include <interfaces/conf.h>
#include <configlib.h>

#define MODULE_NAME "app-meas-generic"


/* Assemble the application configuration tree: the MIB subtree (if the port parsed one), the
 * application settings and the compensation coefficients all hang off a single root. */
static app_ret_t config_init(App *self) {
	configlib_init(&self->root_conf, "root");

	/* Attach the MIB subtree if the port parsed a valid MIB from flash. */
	Conf *mib = NULL;
	if (iservicelocator_query_name_type(locator, "mib", ISERVICELOCATOR_TYPE_CONF, (Interface **)&mib) == ISERVICELOCATOR_RET_OK) {
		configlib_append((ConfiglibValue *)mib->parent, &self->root_conf, CONF_DIR_CHILD);
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("attached MIB configuration subtree"));
	}

	/* Application settings subtree. */
	configlib_init_map_append(&self->app_conf, "app", NULL, CONF_SUBTREE, &self->root_conf, CONF_DIR_CHILD);
	configlib_init_map_append(&self->sample_interval_conf, "sample_interval_ms", &self->sampling.sample_interval_ms, CONF_U32, &self->app_conf, CONF_DIR_CHILD);
	configlib_set_description(&self->sample_interval_conf, "sampling interval in milliseconds", NULL);

	/* Attach the MqCompensation config tree directly by its ConfiglibValue. */
	configlib_append(&self->sampling.comp.root_conf, &self->root_conf, CONF_DIR_CHILD);

	return APP_RET_OK;
}


app_ret_t app_init(App *self) {
	memset(self, 0, sizeof(App));

	if (sampling_init(&self->sampling) != SAMPLING_RET_OK) {
		return APP_RET_FAILED;
	}

	config_init(self);
	#if defined(CONFIG_APP_MEAS_GENERIC_NBUS_API)
		api_init(&self->api, &self->root_conf.conf);
	#endif
	#if defined(CONFIG_APP_MEAS_GENERIC_TEDS)
		teds_init(&self->teds);
	#endif

	return APP_RET_OK;
}


app_ret_t app_free(App *self) {
	(void)self;
	return APP_RET_OK;
}
