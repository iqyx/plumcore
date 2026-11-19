/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * nwdaq-main-hh1 application sensor calibration loader
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

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


/* Publish the force channel @p ch on math/force<ch>/<sn> identifying the calibrated sensor by its serial number @p sn,
 * or on math/force<ch> if @p sn is NULL. */
static void app_sensor_calibration_set_topic(App *self, uint32_t ch, const char *sn) {
	char topic[MQ_COMPENSATION_MAX_TOPIC_LEN];
	if (sn != NULL) {
		snprintf(topic, sizeof(topic), "math/force%u/%s", (unsigned int)ch, sn);
	} else {
		snprintf(topic, sizeof(topic), "math/force%u", (unsigned int)ch);
	}
	mq_compensation_set_output_topic(&self->force, ch, topic);
}


/* Link the subtree of each TEDS partition with a certificate to the "device teds" tree, in the order of the
 * partitions. The subtrees are static, a concurrent walk of the tree never reaches a released node. */
static void app_sensor_calibration_link(App *self) {
	ConfiglibValue *first = NULL;
	for (uint32_t i = APP_SENSOR_CALIBRATION_CHANNELS; i-- > 0;) {
		if (self->teds_cert_id[i][0] != '\0') {
			self->teds_conf.dev[i].conf.next = first;
			self->teds_conf.dev[i].conf.parent = &self->teds_conf.root;
			first = &self->teds_conf.dev[i].conf;
		}
	}
	self->teds_conf.root.child = first;
}


/* Forget the certificate of the TEDS partition @p ch, its identifier and its targets. Its subtree is removed from the
 * "device teds" tree and the targets are unlinked before they are freed. */
static void app_sensor_calibration_forget(App *self, uint32_t ch) {
	self->teds_cert_id[ch][0] = '\0';
	app_sensor_calibration_link(self);
	struct app_teds_target *t = self->teds_conf.dev[ch].targets;
	self->teds_conf.dev[ch].target.child = NULL;
	self->teds_conf.dev[ch].targets = NULL;
	while (t != NULL) {
		struct app_teds_target *next = t->next;
		free(t);
		t = next;
	}
}


/* Expose each target of the certificate @p cert as "target <id> sn" in the configuration tree of the TEDS partition
 * @p ch, keeping the order of the certificate. A target is linked to the tree once it is complete. */
static void app_sensor_calibration_load_targets(App *self, struct mq_compensation_cert *cert, uint32_t ch) {
	char id[MQ_COMPENSATION_CERT_STR_LEN];
	struct app_teds_target *prev = NULL;
	for (uint32_t i = 0; mq_compensation_cert_get_target_id(cert, i, id, sizeof(id)) == MQ_COMPENSATION_RET_OK; i++) {
		struct app_teds_target *t = malloc(sizeof(struct app_teds_target));
		if (t == NULL) {
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot allocate certificate target '%s'"), id);
			return;
		}
		memset(t, 0, sizeof(struct app_teds_target));
		strcpy(t->id, id);
		/* A target with no serial number is exposed with an empty one. */
		if (mq_compensation_cert_get_sn(cert, t->id, t->sn, sizeof(t->sn)) != MQ_COMPENSATION_RET_OK) {
			t->sn[0] = '\0';
		}

		configlib_init_map(&t->conf, t->id, NULL, CONF_SUBTREE);
		configlib_set_description(&t->conf, "Calibration target",
			"A target of the calibration certificate (eg. the sensor or its part), named by its identifier in the "
			"certificate.");
		configlib_init(&t->sn_conf, "sn");
		configlib_map_string(&t->sn_conf, t->sn, sizeof(t->sn));
		configlib_append(&t->sn_conf, &t->conf, CONF_DIR_CHILD);
		t->sn_conf.flags = CONF_READ | CONF_STATUS;
		configlib_set_description(&t->sn_conf, "Target serial number",
			"Serial number of the calibration target as stated in the certificate. Empty if the certificate does "
			"not state any.");

		if (prev == NULL) {
			configlib_append(&t->conf, &self->teds_conf.dev[ch].target, CONF_DIR_CHILD);
			self->teds_conf.dev[ch].targets = t;
		} else {
			configlib_append(&t->conf, &prev->conf, CONF_DIR_NEXT);
			prev->next = t;
		}
		prev = t;
	}
}


/* Open the TEDS partition @p name and load its certificate into the force channel @p ch if the certificate
 * identifier differs from the one seen last. The identifier is kept even if loading fails, the certificate is not
 * loaded again until the identifier changes or a read failure (eg. the sensor was detached) forgets it. */
static void app_sensor_calibration_check(App *self, const char *name, uint32_t ch) {
	if (nbus_flash_client_open(&self->ff14_flash, name) != NBUS_FLASH_CLIENT_RET_OK) {
		app_sensor_calibration_skip(self, name, ch, "partition cannot be opened");
		app_sensor_calibration_forget(self, ch);
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
			app_sensor_calibration_set_topic(self, ch, NULL);
		}
		mq_compensation_reset_channel(&self->force, ch);
		app_sensor_calibration_forget(self, ch);
		nbus_flash_client_close(&self->ff14_flash);
		return;
	}

	self->teds_skip[ch] = NULL;

	if (strcmp(id, self->teds_cert_id[ch])) {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("%s: new certificate '%s'"), name, id);
		app_sensor_calibration_forget(self, ch);
		strcpy(self->teds_cert_id[ch], id);
		app_sensor_calibration_load_targets(self, &cert, ch);
		app_sensor_calibration_link(self);
		char sn[MQ_COMPENSATION_CERT_STR_LEN];
		if (mq_compensation_load_certificate_channel(&self->force, &self->ff14_flash.flash, NULL, ch) != MQ_COMPENSATION_RET_OK) {
			/* Do not keep the calibration of a previous sensor. */
			u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("%s: certificate '%s' cannot be loaded, channel reset"), name, id);
			mq_compensation_reset_channel(&self->force, ch);
			app_sensor_calibration_set_topic(self, ch, NULL);
		} else if (mq_compensation_cert_get_sn(&cert, NULL, sn, sizeof(sn)) != MQ_COMPENSATION_RET_OK) {
			u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("%s: certificate '%s' has no sensor serial number"), name, id);
			app_sensor_calibration_set_topic(self, ch, NULL);
		} else {
			app_sensor_calibration_set_topic(self, ch, sn);
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

	/* Advertise the "device teds" tree. The service locator cannot remove it, it is kept even if no sensor is
	 * attached. The subtree of a TEDS partition is linked to it once its certificate is loaded. */
	configlib_init_map(&self->teds_conf.root, "teds", NULL, CONF_SUBTREE);
	configlib_set_description(&self->teds_conf.root, "Sensor TEDS",
		"Calibration certificates read from the TEDS memories of the sensors attached to the measurement card. A "
		"subtree is present for each sensor connector with a readable certificate.");
	for (uint32_t i = 0; i < APP_SENSOR_CALIBRATION_CHANNELS; i++) {
		snprintf(self->teds_conf.dev[i].name, sizeof(self->teds_conf.dev[i].name), "%u", (unsigned int)i);
		configlib_init_map(&self->teds_conf.dev[i].conf, self->teds_conf.dev[i].name, NULL, CONF_SUBTREE);
		configlib_set_description(&self->teds_conf.dev[i].conf, "Sensor connector TEDS",
			"Calibration certificate read from the TEDS memory of the sensor attached to the measurement card "
			"connector of the same number. Removed when the sensor is detached.");
		configlib_init(&self->teds_conf.dev[i].id, "id");
		configlib_map_string(&self->teds_conf.dev[i].id, self->teds_cert_id[i], sizeof(self->teds_cert_id[i]));
		configlib_append(&self->teds_conf.dev[i].id, &self->teds_conf.dev[i].conf, CONF_DIR_CHILD);
		self->teds_conf.dev[i].id.flags = CONF_READ | CONF_STATUS;
		configlib_set_description(&self->teds_conf.dev[i].id, "Certificate identifier",
			"Unique identifier of the calibration certificate within the namespace of the issuing laboratory.");
		configlib_init_map_append(&self->teds_conf.dev[i].target, "target", NULL, CONF_SUBTREE, &self->teds_conf.dev[i].id, CONF_DIR_NEXT);
		configlib_set_description(&self->teds_conf.dev[i].target, "Calibration targets",
			"Entities calibrated by the certificate (eg. the sensor and its parts), in the order of the "
			"certificate.");
	}
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_CONF, (Interface *)&self->teds_conf.root.conf, "device teds");

	xTaskCreate(app_sensor_calibration_task, "hh1-calib", configMINIMAL_STACK_SIZE + 512, (void *)self, 1, &(self->sensor_calibration_task));
	if (self->sensor_calibration_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create the sensor calibration task"));
		return APP_RET_FAILED;
	}

	return APP_RET_OK;
}

#endif
