/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * nwdaq-main-hh1 application Wi-Fi link
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include <main.h>

#include <interfaces/servicelocator.h>
#include "app.h"

#define MODULE_NAME "hh1-wifi"


/* Wi-Fi event handler. Runs in the driver's receive task, so it only logs. @p ctx is the owning App. */
static void app_wifi_event_handler(void *ctx, const struct wifi_event *event) {
	(void)ctx;
	switch (event->type) {
		case WIFI_EVENT_CONNECTED:
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("Wi-Fi connected"));
			break;
		case WIFI_EVENT_GOT_IP:
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("Wi-Fi network up"));
			break;
		case WIFI_EVENT_DISCONNECTED:
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("Wi-Fi disconnected"));
			break;
		case WIFI_EVENT_CONNECT_FAILED:
			u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("Wi-Fi connection failed (reason %ld)"),
			      (long)event->reason);
			break;
		default:
			break;
	}
}


app_ret_t app_wifi_init(App *self) {
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("--------------- starting Wi-Fi ----------------"));

	/* Discover the generic Wi-Fi device advertised by the port. */
	self->wifi = NULL;
	if (iservicelocator_query_name_type(locator, "wifi", ISERVICELOCATOR_TYPE_WIFI, (Interface **)&self->wifi) != ISERVICELOCATOR_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("Wi-Fi device not found"));
		return APP_RET_FAILED;
	}

	self->wifi->vmt->set_event_handler(self->wifi, app_wifi_event_handler, self);

	/* Announce the device under its hostname, the port name is used when the hostname is missing or empty. */
	char hostname[33];
	union conf_val val = {0};
	if (app_conf_str("system", "hostname", &val) == CONF_RET_OK && val.str.len > 0) {
		snprintf(hostname, sizeof(hostname), "%.*s", (int)val.str.len, val.str.buf);
	} else {
		snprintf(hostname, sizeof(hostname), "%s", PORT_NAME);
	}
	if (self->wifi->vmt->set_hostname != NULL && self->wifi->vmt->set_hostname(self->wifi, hostname) != WIFI_RET_OK) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("cannot set the host name"));
	}

	/* The mode, SSID and security were already loaded into the device's configuration subtree, start the link
	 * with them. The link comes up asynchronously, reported through the event handler. */
	if (self->wifi->vmt->start(self->wifi) != WIFI_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start Wi-Fi"));
		return APP_RET_FAILED;
	}

	return APP_RET_OK;
}
