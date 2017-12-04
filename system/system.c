/*
 * plumCore system initialization
 *
 * Copyright (C) 2015-2017, Marek Koza, qyx@krtko.org
 *
 * This file is part of uMesh node firmware (http://qyx.krtko.org/projects/umesh)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include "FreeRTOS.h"
#include "task.h"
#include "port.h"
#include "u_assert.h"
#include "u_log.h"

#include "system.h"

#include "interfaces/servicelocator.h"
#include "module_loginmgr.h"
#include "services/cli.h"
#include "services/cli/system_cli_tree.h"


#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "system"


/**
 * Discover the first stream interface in the system, it should be the system console.
 * Use it to initialize a login manager service which then runs a command line interface.
 */
static void main_console_init(void) {
	Interface *console;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_STREAM, 0, &console) != ISERVICELOCATOR_RET_OK) {
		return;
	}

	struct module_loginmgr *console_loginmgr = malloc(sizeof(struct module_loginmgr));
	if (console_loginmgr == NULL) {
		return;
	}

	module_loginmgr_init(console_loginmgr, "login1", (struct interface_stream *)console);
	hal_interface_set_name(&(console_loginmgr->iface.descriptor), "login1");

	ServiceCli *console_cli = malloc(sizeof(ServiceCli));
	if (console_cli == NULL) {
		return;
	}

	service_cli_init(console_cli, &(console_loginmgr->iface), system_cli_tree);
	service_cli_start(console_cli);
}


void system_init(void) {
	main_console_init();
}
