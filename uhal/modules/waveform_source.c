/*
 * plumCore waveform source UXB driver
 *
 * Copyright (C) 2018, Marek Koza, qyx@krtko.org
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
#include "protocols/uxb/waveform.pb.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "uhal/interfaces/uxbslot.h"

#include "waveform_source.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "uxb-wavesource"



static bool process_message(uint8_t *buf, size_t len) {

	WaveformResponse msg = WaveformResponse_init_zero;
	pb_istream_t istream;
        istream = pb_istream_from_buffer(buf, len);

	if (pb_decode(&istream, WaveformResponse_fields, &msg)) {
		if (msg.which_content == WaveformResponse_start_recording_response_tag) {
			u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("received start rec response"));
		}
		if (msg.which_content == WaveformResponse_get_data_response_tag) {
			uint32_t size = msg.buffer_stats.size;
			uint32_t occupied = msg.buffer_stats.occupied;
			uint32_t free = msg.buffer_stats.free;

			if (occupied == 0) {
				vTaskDelay(100);
				return false;
			} else {
				u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("get data size=%lu occupied=%lu free=%lu"), size, occupied, free);
				if (occupied == 128) {
					return true;
				}
				return false;
			}
		}
	}
}


static void encode_start_recording(uint8_t *buf, size_t size, size_t *len) {
	WaveformRequest msg = WaveformRequest_init_zero;
	msg.which_content = WaveformRequest_start_recording_tag;

	pb_ostream_t ostream;
        ostream = pb_ostream_from_buffer(buf, size);
        if (pb_encode(&ostream, WaveformRequest_fields, &msg)) {
		*len = ostream.bytes_written;
	} else {
		*len = 0;
	}
}


static void encode_get_data(uint8_t *buf, size_t size, size_t *len) {
	WaveformRequest msg = WaveformRequest_init_zero;
	msg.which_content = WaveformRequest_get_data_tag;

	pb_ostream_t ostream;
        ostream = pb_ostream_from_buffer(buf, size);
        if (pb_encode(&ostream, WaveformRequest_fields, &msg)) {
		*len = ostream.bytes_written;
	} else {
		*len = 0;
	}
}


static void start_recording(UxbWaveformSource *self) {
	uint8_t buf[256];
	size_t len = 0;
	encode_start_recording(buf, sizeof(buf), &len);
	if (len == 0) {
		return;
	}
	if (iuxbslot_send(self->slot, buf, len, false) != IUXBSLOT_RET_OK) {
		return;
	}
	if (iuxbslot_receive(self->slot, buf, sizeof(buf), &len) != IUXBSLOT_RET_OK) {
		return;
	}
	process_message(buf, len);
}


static bool get_data(UxbWaveformSource *self) {
	uint8_t buf[256];
	size_t len = 0;
	encode_get_data(buf, sizeof(buf), &len);
	if (len == 0) {
		return false;
	}
	if (iuxbslot_send(self->slot, buf, len, false) != IUXBSLOT_RET_OK) {
		return false;
	}
	if (iuxbslot_receive(self->slot, buf, sizeof(buf), &len) != IUXBSLOT_RET_OK) {
		return false;
	}
	return process_message(buf, len);
}


static void receive_task(void *p) {
	UxbWaveformSource *self = (UxbWaveformSource *)p;

	start_recording(self);
	while (self->receive_can_run) {
		// vTaskDelay(2);

		if (get_data(self)) {
			start_recording(self);
		}
	}

	vTaskDelete(NULL);
}


uxb_waveform_source_ret_t uxb_waveform_source_init(UxbWaveformSource *self, IUxbSlot *slot) {
	if (u_assert(self != NULL)) {
		return UXB_WAVEFORM_SOURCE_RET_FAILED;
	}

	memset(self, 0, sizeof(UxbWaveformSource));
	uhal_module_init(&self->module);
	self->slot = slot;

	/* The slot is initialized, number is set. */
	iuxbslot_set_slot_buffer(self->slot, self->slot_buffer, 128);

	self->receive_can_run = true;
	xTaskCreate(receive_task, "uxb-wavesource", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &(self->receive_task));
	if (self->receive_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		return UXB_WAVEFORM_SOURCE_RET_FAILED;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("module initialized"));
	return UXB_WAVEFORM_SOURCE_RET_OK;
}


uxb_waveform_source_ret_t uxb_waveform_source_free(UxbWaveformSource *self) {
	if (u_assert(self != NULL)) {
		return UXB_WAVEFORM_SOURCE_RET_FAILED;
	}

	return UXB_WAVEFORM_SOURCE_RET_OK;
}

