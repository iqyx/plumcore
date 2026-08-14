/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Settings applet skeleton
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <main.h>
#include <interfaces/applet.h>

/* The applet icon lives among the fb-painter assets, which are only compiled when the fb-painter
 * service is enabled. */
#if defined(CONFIG_SERVICE_FB_PAINTER)
#include <services/fb-painter/assets/assets.h>
#endif

#define MODULE_NAME "settings"


static applet_ret_t settings_main(Applet *self, struct applet_args *args) {
	(void)self;
	if (args->logger != NULL) {
		u_log(args->logger, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("settings applet started"));
	}

	return APPLET_RET_OK;
}


const Applet settings = {
	.executable.native = {
		.main = settings_main
	},
	.name = "Settings",
	.help = "View and edit device settings",
	#if defined(CONFIG_SERVICE_FB_PAINTER)
		.icon = &settings_data,
	#endif
};
