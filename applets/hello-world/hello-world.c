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

#define MODULE_NAME "hello-world"


static applet_ret_t hello_world_main(Applet *self, struct applet_args *args) {
	(void)self;
	if (args->logger != NULL) {
		u_log(args->logger, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX(CONFIG_APPLET_HELLO_WORLD_GREETING));
	}

	return APPLET_RET_OK;
}


const Applet hello_world = {
	.executable.compiled = {
		.main = hello_world_main
	},
	.name = "Hello world"
};
