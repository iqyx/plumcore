/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * nwdaq-main-hh1 application sensor calibration loader
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include <main.h>

#include <interfaces/servicelocator.h>
#include "app.h"

#if defined(CONFIG_APP_HH1_PROFILE_FF14_FORCE)

#define MODULE_NAME "hh1-calib"

#define APP_SENSOR_CALIBRATION_INTERVAL_MS 5000
#define APP_SENSOR_CALIBRATION_TIMEOUT_MS 2000


/* Get the force channel calibrated by the TEDS partition @p name (teds0 to teds3). */
static bool app_sensor_calibration_channel(const char *name, uint32_t *ch) {
	if (strncmp(name, "teds", 4) || name[4] < '0' || name[5] != '\0') {
		return false;
	}
	*ch = name[4] - '0';
	return *ch < APP_SENSOR_CALIBRATION_CHANNELS;
}


/* Log the reason the certificate of the TEDS partition @p name was skipped for, unless it was logged already. */
static void app_sensor_calibration_skip(App *self, const char *name, uint32_t ch, const char *reason) {
	if (self->teds_skip[ch] != reason) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("%s: skipped, %s"), name, reason);
		self->teds_skip[ch] = reason;
	}
}


/* Open the TEDS partition @p name and load its certificate into the force channel @p ch if the certificate
 * identifier differs from the one seen last. The identifier is kept even if loading fails, the certificate is not
 * loaded again until the identifier changes or a read failure (eg. the sensor was detached) forgets it. */
static void app_sensor_calibration_check(App *self, const char *name, uint32_t ch) {
	if (nbus_flash_client_open(&self->ff14_flash, name) != NBUS_FLASH_CLIENT_RET_OK) {
		app_sensor_calibration_skip(self, name, ch, "partition cannot be opened");
		self->teds_cert_id[ch][0] = '\0';
		return;
	}

	struct mq_compensation_cert cert;
	char id[MQ_COMPENSATION_CERT_STR_LEN];
	const char *reason = NULL;
	if (mq_compensation_cert_init(&cert, &self->ff14_flash.flash) != MQ_COMPENSATION_RET_OK) {
		reason = "no certificate header";
	} else if (mq_compensation_cert_get_id(&cert, id, sizeof(id)) != MQ_COMPENSATION_RET_OK) {
		reason = "no certificate id";
	}
	if (reason != NULL) {
		app_sensor_calibration_skip(self, name, ch, reason);
		/* No certificate (eg. the sensor was removed or has none), do not keep the calibration of a previous
		 * sensor. The same certificate is loaded again when it reappears. */
		if (self->teds_cert_id[ch][0] != '\0') {
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("%s: no certificate, channel reset"), name);
		}
		mq_compensation_reset_channel(&self->force, ch);
		self->teds_cert_id[ch][0] = '\0';
		nbus_flash_client_close(&self->ff14_flash);
		return;
	}

	self->teds_skip[ch] = NULL;

	if (strcmp(id, self->teds_cert_id[ch])) {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("%s: new certificate '%s'"), name, id);
		strcpy(self->teds_cert_id[ch], id);
		if (mq_compensation_load_certificate_channel(&self->force, &self->ff14_flash.flash, NULL, ch) != MQ_COMPENSATION_RET_OK) {
			/* Do not keep the calibration of a previous sensor. */
			u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("%s: certificate '%s' cannot be loaded, channel reset"), name, id);
			mq_compensation_reset_channel(&self->force, ch);
		}
	}

	nbus_flash_client_close(&self->ff14_flash);
}


static void app_sensor_calibration_task(void *p) {
	App *self = p;

	while (true) {
		/* Enumerate the partitions of the measurement card, the TEDS partitions appear once a sensor is
		 * discovered. */
		char name[NBUS_FLASH_CLIENT_NAME_LEN];
		for (uint32_t i = 0; nbus_flash_client_list(&self->ff14_flash, i, name, sizeof(name)) == NBUS_FLASH_CLIENT_RET_OK; i++) {
			uint32_t ch = 0;
			if (app_sensor_calibration_channel(name, &ch)) {
				app_sensor_calibration_check(self, name, ch);
			}
		}
		vTaskDelay(pdMS_TO_TICKS(APP_SENSOR_CALIBRATION_INTERVAL_MS));
	}
	vTaskDelete(NULL);
}


app_ret_t app_sensor_calibration_init(App *self) {
	/* Bind a third local socket and connect it to the measurement card's nbus-flash endpoint at SID 00000010,
	 * ep 1. A distinct local endpoint (ep 3) keeps its responses separate from the flash proxy socket (ep 2). */
	const uint8_t local_id[4] = {0x00, 0x00, 0x00, 0x20};
	const uint8_t flash_id[4] = {0x00, 0x00, 0x00, 0x10};
	self->nbus_flash_client_socket = nbus_socket_allocate(&self->nbus);
	if (self->nbus_flash_client_socket == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("backplane not available"));
		return APP_RET_FAILED;
	}
	nbus_socket_bind(self->nbus_flash_client_socket, local_id, 3);
	nbus_socket_connect(self->nbus_flash_client_socket, flash_id, 1);
	nbus_socket_set_timeout(self->nbus_flash_client_socket, APP_SENSOR_CALIBRATION_TIMEOUT_MS);

	const struct nbus_flash_client_conf flash_client_conf = {
		.d = &self->nbus_flash_client_socket->datagram,
	};
	nbus_flash_client_init(&self->ff14_flash, &flash_client_conf);

	xTaskCreate(app_sensor_calibration_task, "hh1-calib", configMINIMAL_STACK_SIZE + 512, (void *)self, 1, &(self->sensor_calibration_task));
	if (self->sensor_calibration_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create the sensor calibration task"));
		return APP_RET_FAILED;
	}

	return APP_RET_OK;
}

#endif
