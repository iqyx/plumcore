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
#include "config.h"

#include "system.h"
#include "interfaces/servicelocator.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "system"



/**
 * Discover a stream interface called "cosole", it should be the system console.
 * Use it to initialize a login manager service which then runs a command line interface.
 */
#if defined(CONFIG_DEFAULT_CONSOLE_CLI)
#include "services/loginmgr/module_loginmgr.h"
#include "services/cli/cli.h"
#include "services/cli/system_cli_tree.h"
static void main_console_init(void) {
	Stream *console;
	if (iservicelocator_query_name(locator, "console", (Interface **)&console) != ISERVICELOCATOR_RET_OK) {
		return;
	}

	struct module_loginmgr *console_loginmgr = malloc(sizeof(struct module_loginmgr));
	if (console_loginmgr == NULL) {
		return;
	}

	module_loginmgr_init(console_loginmgr, "login1", console);

	ServiceCli *console_cli = malloc(sizeof(ServiceCli));
	if (console_cli == NULL) {
		return;
	}

	service_cli_init(console_cli, &(console_loginmgr->iface), system_cli_tree);
	service_cli_start(console_cli);

	#if defined(CONFIG_CONFIG_LOAD_FILE)
		/* Wait for the extension board discovery. */
		vTaskDelay(1000);

		Interface *interface;
		if (iservicelocator_query_name_type(locator, "system", ISERVICELOCATOR_TYPE_FS, &interface) == ISERVICELOCATOR_RET_OK) {
			Fs *fs = (Fs *)interface;
			service_cli_load_file(console_cli, fs, CONFIG_CONFIG_LOAD_FILE_FILENAME);
		}
	#endif
}
#endif

/**
 * Initialize and run the system-wide message router. It is used for system logging purposes and
 * to deliver measured data to an appropriate sink (memory, network, etc.)
 */
#if defined(CONFIG_DEFAULT_PLOG_ROUTER)
#include "services/plog-router/plog_router.h"
#include "interfaces/plog/descriptor.h"
#include "interfaces/plog/client.h"
PlogRouter plog_router;
static void system_plog_router_init(void) {
	plog_router_init(&plog_router);
	iservicelocator_add(
		locator,
		ISERVICELOCATOR_TYPE_MQ,
		(Interface *)&plog_router.mq,
		"plog-mq"
	);
}
#endif


/**
 * Plog sensor upload is a service which periodically searches for all connected sensors,
 * reads their values and publishes them to the plog message router.
 */
#if defined(CONFIG_DEFAULT_PLOG_SENSOR_UPLOAD)
#include "services/plog-sensor-upload/plog_sensor_upload.h"
PlogSensorUpload plog_sensor_upload;
static void system_plog_sensor_upload_init(void) {
	plog_sensor_upload_init(&plog_sensor_upload, 1000);
}
#endif


#if defined(CONFIG_SERVICE_DATA_PROCESS)
#include "services/data-process/data-process.h"
static void system_data_process_init(void) {
	dp_graph_init(&data_process_graph);
}
#endif

void system_init(void) {
	#if defined(CONFIG_DEFAULT_CONSOLE_CLI)
		main_console_init();
	#endif
	#if defined(CONFIG_DEFAULT_PLOG_ROUTER)
		system_plog_router_init();
	#endif
	#if defined(CONFIG_DEFAULT_PLOG_SENSOR_UPLOAD)
		system_plog_sensor_upload_init();
	#endif
	#if defined(CONFIG_SERVICE_DATA_PROCESS)
		system_data_process_init();
	#endif
}
