/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Basic application template
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <main.h>
#include "app.h"

#define MODULE_NAME "app"


app_ret_t app_init(App *self) {
	memset(self, 0, sizeof(App));
	return APP_RET_OK;
}


app_ret_t app_free(App *self) {
	(void)self;
	return APP_RET_OK;
}
