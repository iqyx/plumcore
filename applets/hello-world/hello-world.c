/* SPDX-License-Identifier: BSD-2-Clause
 *
 * A Hello world applet example
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

/* The applet icon lives among the fb-painter assets, which are only compiled when the fb-painter
 * service is enabled. */
#if defined(CONFIG_SERVICE_FB_PAINTER)
#include <services/fb-painter/assets/assets.h>
#endif

#define MODULE_NAME "hello-world"


static applet_ret_t hello_world_main(Applet *self, struct applet_args *args) {
	(void)self;
	if (args->logger != NULL) {
		u_log(args->logger, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX(CONFIG_APPLET_HELLO_WORLD_GREETING));
	}

	return APPLET_RET_OK;
}


const Applet hello_world = {
	.executable.native = {
		.main = hello_world_main
	},
	.name = "Hello world",
	.help = "Simple applet to showcase the functionality, compiled version",
	#if defined(CONFIG_SERVICE_FB_PAINTER)
		.icon = &grumpy_cat_data,
	#endif
};
