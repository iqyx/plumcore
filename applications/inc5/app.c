/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * nwdaq-inc5 inclination measurement application
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <main.h>
#include "app.h"

#include <interfaces/servicelocator.h>
#include <interfaces/conf.h>
#include <configlib.h>
#include <types/ndarray.h>
#include <blake2s.h>

#define MODULE_NAME "app-inc5"


/* Measurement channels the application knows about. Each entry maps a sensor advertised by the
 * port (by its service locator name) to the raw and compensated message queue topics. The
 * compensation channel is registered under the sensor name so its coefficient subtree is
 * addressable as compensation/<sensor_name> in the configuration tree. */
static const struct app_channel_desc {
	const char *sensor_name;
	const char *input_topic;
	const char *output_topic;
} app_channels[APP_CHANNEL_COUNT] = {
	{"inc_x", "inc/x", "inc/x/comp"},
	{"inc_y", "inc/y", "inc/y/comp"},
	{"acc_x", "acc/x", "acc/x/comp"},
	{"acc_y", "acc/y", "acc/y/comp"},
};


/**
 * @brief Read a single sensor value and publish it on the message queue
 *
 * The sensor read blocks until the next measurement cycle finishes (the port gates this on
 * the measurement-ready semaphore inside the Sensor interface).
 */
static void publish_sensor(App *self, Sensor *sensor, const char *topic) {
	float f = 0.0f;
	if (sensor->vmt->value_f(sensor, &f) != SENSOR_RET_OK) {
		return;
	}

	struct timespec ts = {0};
	NdArray array;
	ndarray_init_view(&array, DTYPE_FLOAT, 1, &f, sizeof(f));
	self->mqc->vmt->publish(self->mqc, topic, &array, &ts);
}


static void app_task(void *p) {
	App *self = p;

	while (true) {
		/* Publish every discovered channel and, if present, the compensation temperature.
		 * The first read blocks until the next measurement cycle; the port then makes all
		 * sensor values available at once, so the remaining reads return immediately. */
		for (size_t i = 0; i < APP_CHANNEL_COUNT; i++) {
			if (self->channels[i] != NULL) {
				publish_sensor(self, self->channels[i], app_channels[i].input_topic);
			}
		}
		if (self->temp_x != NULL) {
			publish_sensor(self, self->temp_x, "temp");
		}
	}
	vTaskDelete(NULL);
}


/**
 * @brief Continuously read compensated inclination values from the message queue and print them
 */
static void comp_log_task(void *p) {
	App *self = p;

	/* Open a separate client to read the compensated values back for logging. */
	self->log_mqc = self->mq->vmt->open(self->mq);
	if (self->log_mqc == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot open MQ client"));
		goto err;
	}
	self->log_mqc->vmt->subscribe(self->log_mqc, "#");

	/* Receive buffer for the scalar compensated values. */
	NdArray buf;
	if (ndarray_init_empty(&buf, DTYPE_FLOAT, 1) != NDARRAY_RET_OK) {
		vTaskDelete(NULL);
		return;
	}

	while (true) {
		char topic[MQ_COMPENSATION_MAX_TOPIC_LEN] = {0};
		struct timespec ts = {0};
		if (self->log_mqc->vmt->receive(self->log_mqc, topic, sizeof(topic), &buf, &ts) != MQ_RET_OK) {
			continue;
		}
		if (buf.dtype != DTYPE_FLOAT || buf.asize < 1 || buf.buf == NULL) {
			continue;
		}
		if (self->console != NULL) {
			char line[64] = {0};
			int len = snprintf(line, sizeof(line), "%s=%.3f\r\n", topic, ((float *)buf.buf)[0]);
			self->console->vmt->write(self->console, line, len);
		}
	}

err:
	vTaskDelete(NULL);
}


static app_ret_t config_init(App *self) {
	configlib_init(&self->root_conf, "root");

	/* Attach the MIB subtree if the port parsed a valid MIB from flash. */
	Conf *mib = NULL;
	if (iservicelocator_query_name_type(locator, "mib", ISERVICELOCATOR_TYPE_CONF, (Interface **)&mib) == ISERVICELOCATOR_RET_OK) {
		configlib_append((ConfiglibValue *)mib->parent, &self->root_conf, CONF_DIR_CHILD);
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("attached MIB configuration subtree"));
	}

	/* Attach the MqCompensation config tree directly by its ConfiglibValue — type safe, no cast needed. */
	configlib_append(&self->comp.root_conf, &self->root_conf, CONF_DIR_CHILD);

	return APP_RET_OK;
}


static app_ret_t api_init(App *self) {
	self->console = NULL;
	if (iservicelocator_query_name_type(locator, "console", ISERVICELOCATOR_TYPE_STREAM, (Interface **)&self->console) != ISERVICELOCATOR_RET_OK) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("no console stream, API not available"));
		return APP_RET_FAILED;
	}

	Datagram *console_dgram = NULL;
	proto_dgtext_init(&self->console_dgtext, self->console);
	proto_dgtext_get_datagram(&self->console_dgtext, &console_dgram);

	const struct nbus_config console_nbus_config = {
		.dgram = console_dgram,
		.tx_crypto = NBUS_CRYPTO_BLAKE2S_SIV,
		.rx_crypto = NBUS_CRYPTO_BLAKE2S_SIV,
	};
	nbus_init(&self->console_nbus, &console_nbus_config);
	nbus_set_mac_key(&self->console_nbus, (uint8_t *)"abcd", 4);

	const uint8_t local_ep = 1;
	uint8_t console_id[4];
	blake2s(console_id, sizeof(console_id), "", 0, UNIQUE_ID_REG, UNIQUE_ID_REG_LEN);

	console_id[3] += 1;
	self->console_proto_flash_socket = nbus_socket_allocate(&self->console_nbus);
	nbus_socket_bind(self->console_proto_flash_socket, console_id, local_ep);
	nbus_flash_init(&self->console_proto_flash, &self->console_proto_flash_socket->datagram);
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("nbus-flash on console at %02x%02x%02x%02x ep %d"), console_id[0], console_id[1], console_id[2], console_id[3], local_ep);

	console_id[3] += 1;
	self->console_proto_conf_socket = nbus_socket_allocate(&self->console_nbus);
	nbus_socket_bind(self->console_proto_conf_socket, console_id, local_ep);
	proto_conf_init(&self->console_proto_conf, &self->console_proto_conf_socket->datagram, &self->root_conf.conf);
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("proto-conf on console at %02x%02x%02x%02x ep %d"), console_id[0], console_id[1], console_id[2], console_id[3], local_ep);

	return APP_RET_OK;
}


/* Look up a sensor by name without logging an error when it is absent. Channels are optional:
 * a port advertises only the sensors it has. */
static Sensor *find_sensor_optional(const char *name) {
	Sensor *s = NULL;
	if (iservicelocator_query_name_type(locator, name, ISERVICELOCATOR_TYPE_SENSOR, (Interface **)&s) != ISERVICELOCATOR_RET_OK) {
		return NULL;
	}
	return s;
}


/* Register a compensation channel with identity coefficients (zero offset, unity gain, no
 * nonlinearity or temperature correction). The coefficients are exposed through the service
 * configuration tree under the channel (sensor) name. */
static void add_comp_channel(App *self, const struct app_channel_desc *desc) {
	struct mq_compensation_channel_conf conf = {
		.x_ref = 0.0f,
		.c = {0.0f, 1.0f},
		.t_ref = MQ_COMPENSATION_DEFAULT_TEMP_C,
		.tc1 = 0.0f,
		.tc2 = 0.0f,
	};
	snprintf(conf.name, sizeof(conf.name), "%s", desc->sensor_name);
	snprintf(conf.input_topic, sizeof(conf.input_topic), "%s", desc->input_topic);
	snprintf(conf.output_topic, sizeof(conf.output_topic), "%s", desc->output_topic);
	mq_compensation_add_channel(&self->comp, &conf);
}


app_ret_t app_init(App *self) {
	memset(self, 0, sizeof(App));

	self->mq = NULL;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_MQ, 0, (Interface **)&self->mq) != ISERVICELOCATOR_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("no message queue found"));
		return APP_RET_FAILED;
	}

	self->mqc = self->mq->vmt->open(self->mq);
	if (self->mqc == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot open message queue client"));
		return APP_RET_FAILED;
	}

	/* Discover the measurement sensors advertised by the port and register a compensation channel
	 * for each one present. The raw values are compensated against a shared temperature topic and
	 * republished. Ports expose only the sensors they have (the single-axis inc5 has just inc_x),
	 * so absent channels are simply skipped. */
	mq_compensation_init(&self->comp, self->mq);
	size_t found = 0;
	for (size_t i = 0; i < APP_CHANNEL_COUNT; i++) {
		self->channels[i] = find_sensor_optional(app_channels[i].sensor_name);
		if (self->channels[i] == NULL) {
			continue;
		}
		add_comp_channel(self, &app_channels[i]);
		found++;
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("discovered channel '%s'"), app_channels[i].sensor_name);
	}
	if (found == 0) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("no measurement sensors found"));
		return APP_RET_FAILED;
	}

	/* Optional board temperature feeding the compensation polynomial. */
	self->temp_x = find_sensor_optional("temp");

	mq_compensation_set_temp_topic(&self->comp, "temp");
	mq_compensation_start(&self->comp, 1);

	config_init(self);
	api_init(self);

	xTaskCreate(comp_log_task, "inc5-log", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &(self->log_task));
	if (self->log_task == NULL) {
		return APP_RET_FAILED;
	}

	xTaskCreate(app_task, "inc5-app", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &(self->task));
	if (self->task == NULL) {
		return APP_RET_FAILED;
	}

	return APP_RET_OK;
}


app_ret_t app_free(App *self) {
	(void)self;
	return APP_RET_OK;
}
