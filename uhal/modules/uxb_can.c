/*
 * plumCore CAN interface UXB driver
 *
 * Copyright (C) 2017, Marek Koza, qyx@krtko.org
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

#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"

#include "module.h"
#include "protocols/uxb/can.pb.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "uhal/interfaces/uxbslot.h"

#include "uxb_can.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "uxb_can"





static void process_message(uint8_t *buf, size_t len) {

	CanResponse msg = CanResponse_init_zero;
	pb_istream_t istream;
        istream = pb_istream_from_buffer(buf, len);

	if (pb_decode(&istream, CanResponse_fields, &msg)) {
		if (msg.which_content == CanResponse_received_message_tag) {
			for (size_t i = 0; i < msg.content.received_message.message_count; i++) {
				u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("received id=%d len=%d"), msg.content.received_message.message[i].id, msg.content.received_message.message[i].data.size);

			}
		}
	}
}


static void encode_query_received_buffer(uint8_t *buf, size_t size, size_t *len) {
	CanRequest msg = CanRequest_init_zero;
	msg.which_content = CanRequest_query_received_buffer_tag;

	pb_ostream_t ostream;
        ostream = pb_ostream_from_buffer(buf, size);
        if (pb_encode(&ostream, CanRequest_fields, &msg)) {
		*len = ostream.bytes_written;
	} else {
		*len = 0;
	}
}


static void receive_task(void *p) {
	UxbCan *self = (UxbCan *)p;

	while (self->receive_can_run) {
		vTaskDelay(100);

		uint8_t buf[64];
		size_t len = 0;

		encode_query_received_buffer(buf, 64, &len);
		if (len == 0) {
			continue;
		}
		if (iuxbslot_send(self->slot, buf, len, false) != IUXBSLOT_RET_OK) {
			continue;
		}
		if (iuxbslot_receive(self->slot, buf, 64, &len) != IUXBSLOT_RET_OK) {
			continue;
		}

		process_message(buf, len);
	}

	vTaskDelete(NULL);
}


uxb_can_ret_t uxb_can_init(UxbCan *self, IUxbSlot *slot) {
	if (u_assert(self != NULL)) {
		return UXB_CAN_RET_FAILED;
	}

	memset(self, 0, sizeof(UxbCan));
	uhal_module_init(&self->module);
	self->slot = slot;

	/* The slot is initialized, number is set. */
	iuxbslot_set_slot_buffer(self->slot, self->slot_buffer, 64);

	self->receive_can_run = true;
	xTaskCreate(receive_task, "uxb-can-recv", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &(self->receive_task));
	if (self->receive_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		return UXB_CAN_RET_FAILED;
	}


	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("module initialized"));
	return UXB_CAN_RET_OK;
}

uxb_can_ret_t uxb_can_free(UxbCan *self) {
	if (u_assert(self != NULL)) {
		return UXB_CAN_RET_FAILED;
	}

	return UXB_CAN_RET_OK;
}


