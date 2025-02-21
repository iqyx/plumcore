/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Generic HMI implementation
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <main.h>
#include <interfaces/event.h>
#include "app.h"

#define MODULE_NAME "hmi"


static void com_task(void *p) {
	App *self = p;

	struct nbus_socket *socket = nbus_socket_allocate(&nbus);
	while (true) {
		uint8_t local_id[] = {0x00, 0x00, 0x00, 0x10};
		nbus_socket_bind(socket, local_id, 1);

		struct datagram_msg rxmsg = {0};
		static uint8_t packet_buffer[1024];
		size_t len = sizeof(packet_buffer);

		if (socket->datagram.vmt->read(&socket->datagram, packet_buffer, &len, &rxmsg) == DATAGRAM_RET_OK) {
			uint32_t offset = packet_buffer[0] << 24 | packet_buffer[1] << 16 | packet_buffer[2] << 8 | packet_buffer[3];
			len -= 4;

			if ((offset + len) > 9600) {
				continue;
			}
			self->fb->vmt->write(self->fb, offset, packet_buffer + 4, len, FB_MODE_G2);

			if ((offset + len) == 9600) {
				self->fb->vmt->flush(self->fb);
			}
		}

	}
	nbus_socket_release(&nbus, socket);
	vTaskDelete(NULL);
}


static void input_task(void *p) {
	App *self = p;

	while (true) {
		enum event_type type = EV_TYPE_NONE;
		enum event_code code = EV_CODE_NONE;
		int32_t value = 0;

		if (self->input->vmt->listen(self->input, &type, &code, &value) == EV_RET_OK) {
			if (value == 1) {
				/* On key down. */
				self->speaker->vmt->start(self->speaker);
			}
		}
	}
	vTaskDelete(NULL);
}


app_ret_t app_init(App *self) {
	memset(self, 0, sizeof(App));

	self->fb = NULL;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_FB, 0, (Interface **)&self->fb) != ISERVICELOCATOR_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("no framebuffer device found"));
		return APP_RET_FAILED;
	}

	/* Get any i2c available. Allow configuration in the future (along with input devices). */
	self->i2c = NULL;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_I2C, 0, (Interface **)&self->i2c) != ISERVICELOCATOR_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("no suitable i2c device found"));
		return APP_RET_FAILED;
	}

	/** @todo configure which input device to use.  */
	self->input = NULL;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_EVENT, 0, (Interface **)&self->input) != ISERVICELOCATOR_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("no suitable input device found"));
		return APP_RET_FAILED;
	}

	self->speaker = NULL;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_WAVEFORM_SINK, 0, (Interface **)&self->speaker) != ISERVICELOCATOR_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("no suitable speaker found"));
		return APP_RET_FAILED;
	}

	xTaskCreate(com_task, "hmi-com", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &(self->com_task));
	if (self->com_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create application com task"));
		goto err;
	}

	xTaskCreate(input_task, "hmi-input", configMINIMAL_STACK_SIZE + 128, (void *)self, 1, &(self->input_task));
	if (self->input_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create application input task"));
		goto err;
	}


	return APP_RET_OK;
err:
	return APP_RET_FAILED;
}


app_ret_t app_free(App *self) {
	return APP_RET_OK;
}
