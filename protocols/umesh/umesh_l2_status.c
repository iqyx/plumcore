/* A simple status protocol for sensor & power device info broadcasting. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "u_assert.h"
#include "u_log.h"
#include "port.h"
#include "hal_module.h"
#include "module_umesh.h"
#include "umesh_l2_pbuf.h"
#include "umesh_l2_send.h"
#include "umesh_l3_protocol.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "umesh_l2_status.h"
#include "umesh_l2_status.pb.h"
#include "uhal/interfaces/sensor.h"


#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "l2-status"

static struct status_sensor status_sensors[UMESH_L2_STATUS_SENSORS_MAX];
static struct status_power_device status_power_devices[UMESH_L2_STATUS_POWER_DEVICES_MAX];


static void umesh_l2_status_task(void *p) {
	UmeshL2Status *self = (UmeshL2Status *)p;

	umesh_l2_pbuf_init(&self->pbuf);

	while (true) {
		if (self->last_broadcast_ms >= UMESH_L2_STATUS_STEP_INTERVAL_MS) {
			umesh_l2_status_send_broadcast(self);
			umesh_l2_status_send_broadcast_power(self);
			self->last_broadcast_ms = 0;
		}

		/* Advance in time. */
		self->last_broadcast_ms += UMESH_L2_STATUS_STEP_INTERVAL_MS;
		vTaskDelay(UMESH_L2_STATUS_STEP_INTERVAL_MS);
	}

	umesh_l2_pbuf_free(&self->pbuf);
	vTaskDelete(NULL);
}


void umesh_l2_status_send_broadcast(UmeshL2Status *self) {

	for (size_t i = 0; i < UMESH_L2_STATUS_SENSORS_MAX; i++) {
		if (status_sensors[i].sensor != NULL) {

			ISensorInfo info;
			float value;
			interface_sensor_info(status_sensors[i].sensor, &info);
			interface_sensor_value(status_sensors[i].sensor, &value);

			StatusProtoMessage msg = StatusProtoMessage_init_zero;
			msg.which_content = StatusProtoMessage_sensor_message_tag;

			memcpy(&msg.content.sensor_message.id.bytes, status_sensors[i].name, strlen(status_sensors[i].name));
			msg.content.sensor_message.id.size = strlen(status_sensors[i].name);

			memcpy(&msg.content.sensor_message.unit_symbol.bytes, info.unit_symbol, strlen(info.unit_symbol));
			msg.content.sensor_message.unit_symbol.size = strlen(info.unit_symbol);

			msg.content.sensor_message.value = value;

			umesh_l2_pbuf_clear(&self->pbuf);

			self->pbuf.flags |= UMESH_L2_PBUF_FLAG_BROADCAST;
			self->pbuf.sec_type = UMESH_L2_PBUF_SECURITY_VERIFY;
			self->pbuf.proto = UMESH_L3_PROTOCOL_STATUS;

			pb_ostream_t stream;
		        stream = pb_ostream_from_buffer(self->pbuf.data, self->pbuf.size);

		        if (!pb_encode(&stream, StatusProtoMessage_fields, &msg)) {
				return;;
			}
			self->pbuf.len = stream.bytes_written;

			if (umesh_l2_send(&umesh, &(self->pbuf)) != UMESH_L2_SEND_OK) {
				return;
			}
		}
	}
}


void umesh_l2_status_send_broadcast_power(UmeshL2Status *self) {

	for (size_t i = 0; i < UMESH_L2_STATUS_POWER_DEVICES_MAX; i++) {
		if (status_power_devices[i].power != NULL) {

			StatusProtoMessage msg = StatusProtoMessage_init_zero;
			msg.which_content = StatusProtoMessage_sensor_message_tag;

			memcpy(&msg.content.sensor_message.id.bytes, status_power_devices[i].name, strlen(status_power_devices[i].name));
			msg.content.sensor_message.id.size = strlen(status_power_devices[i].name);

			memcpy(&msg.content.sensor_message.unit_symbol.bytes, "mV", 2);
			msg.content.sensor_message.unit_symbol.size = 2;

			interface_power_measure(status_power_devices[i].power);
			msg.content.sensor_message.value = interface_power_voltage(status_power_devices[i].power);

			umesh_l2_pbuf_clear(&self->pbuf);

			self->pbuf.flags |= UMESH_L2_PBUF_FLAG_BROADCAST;
			self->pbuf.sec_type = UMESH_L2_PBUF_SECURITY_VERIFY;
			self->pbuf.proto = UMESH_L3_PROTOCOL_STATUS;

			pb_ostream_t stream;
		        stream = pb_ostream_from_buffer(self->pbuf.data, self->pbuf.size);

		        if (!pb_encode(&stream, StatusProtoMessage_fields, &msg)) {
				return;;
			}
			self->pbuf.len = stream.bytes_written;

			if (umesh_l2_send(&umesh, &(self->pbuf)) != UMESH_L2_SEND_OK) {
				return;
			}
		}
	}
}


static int32_t umesh_l2_status_receive_handler(struct umesh_l2_pbuf *pbuf, void *context) {
	if (u_assert(pbuf != NULL && context != NULL)) {
		return UMESH_L3_PROTOCOL_RECEIVE_FAILED;
	}

	UmeshL2Status *self = (UmeshL2Status *)context;

	pb_istream_t stream;
        stream = pb_istream_from_buffer(pbuf->data, pbuf->len);

	StatusProtoMessage msg;
	if (!pb_decode(&stream, StatusProtoMessage_fields, &msg)) {
		return UMESH_L3_PROTOCOL_RECEIVE_FAILED;
	}

	switch (msg.which_content) {
		case StatusProtoMessage_sensor_message_tag:
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("sensor message received"));
			break;

		default:
			return UMESH_L3_PROTOCOL_RECEIVE_FAILED;
	}

	/* Do not process the received data. */

	return UMESH_L3_PROTOCOL_RECEIVE_OK;
}


void umesh_l2_status_init(UmeshL2Status *self) {
	if (u_assert(self != NULL)) {
		return;
	}

	memset(self, 0, sizeof(UmeshL2Status));
	for (size_t i = 0; i < UMESH_L2_STATUS_SENSORS_MAX; i++) {
		status_sensors[i].sensor = NULL;
	}

	self->protocol.name = "status";
	self->protocol.context = (void *)self;
	self->protocol.receive_handler = umesh_l2_status_receive_handler;

	umesh_l2_pbuf_init(&self->pbuf);

	xTaskCreate(umesh_l2_status_task, "umesh_status", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &(self->status_task));
	if (self->status_task == NULL) {
		return;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("status broadcast proto initialized"));



}


void umesh_l2_status_free(UmeshL2Status *self) {
	return;
}


void umesh_l2_status_add_sensor(ISensor *sensor, const char *name) {
	for (size_t i = 0; i < UMESH_L2_STATUS_SENSORS_MAX; i++) {
		if (status_sensors[i].sensor == NULL) {
			strcpy(status_sensors[i].name, name);
			status_sensors[i].sensor = sensor;
			return;
		}
	}
}


void umesh_l2_status_add_power_device(struct interface_power *power, const char *name) {
	for (size_t i = 0; i < UMESH_L2_STATUS_POWER_DEVICES_MAX; i++) {
		if (status_power_devices[i].power == NULL) {
			strcpy(status_power_devices[i].name, name);
			status_power_devices[i].power = power;
			return;
		}
	}
}
