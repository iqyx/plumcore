/*
 * plumCore lightning detector UXB driver
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
#include "port.h"

#include "module.h"
#include "protocols/uxb/ldet.pb.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "interfaces/uxbslot.h"
#include "interfaces/sensor.h"

#include "ldet.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "uxb-ldet"


static interface_sensor_ret_t sensor_signals_total_info(void *context, ISensorInfo *info) {
	if (u_assert(context != NULL) ||
	    u_assert(info != NULL)) {
		return INTERFACE_SENSOR_RET_FAILED;
	}

	info->quantity_name = "Count";
	info->quantity_symbol = "";
	info->unit_name = "";
	info->unit_symbol = "";

	return INTERFACE_SENSOR_RET_OK;
}


static interface_sensor_ret_t sensor_signals_total_value(void *context, float *value) {
	if (u_assert(context != NULL) ||
	    u_assert(value != NULL)) {
		return INTERFACE_SENSOR_RET_FAILED;
	}

	UxbLdet *self = (UxbLdet *)context;
	*value = (float)self->signals_total;
	return INTERFACE_SENSOR_RET_OK;
}


static interface_sensor_ret_t sensor_ema_level_info(void *context, ISensorInfo *info) {
	if (u_assert(context != NULL) ||
	    u_assert(info != NULL)) {
		return INTERFACE_SENSOR_RET_FAILED;
	}

	info->quantity_name = "Level";
	info->quantity_symbol = "";
	info->unit_name = "dB full-scale";
	info->unit_symbol = "dBFS";

	return INTERFACE_SENSOR_RET_OK;
}


static interface_sensor_ret_t sensor_ema_level_rms_value(void *context, float *value) {
	if (u_assert(context != NULL) ||
	    u_assert(value != NULL)) {
		return INTERFACE_SENSOR_RET_FAILED;
	}

	UxbLdet *self = (UxbLdet *)context;
	*value = (float)self->ema_level_rms;
	return INTERFACE_SENSOR_RET_OK;
}


static interface_sensor_ret_t sensor_ema_level_peak_value(void *context, float *value) {
	if (u_assert(context != NULL) ||
	    u_assert(value != NULL)) {
		return INTERFACE_SENSOR_RET_FAILED;
	}

	UxbLdet *self = (UxbLdet *)context;
	*value = (float)self->ema_level_peak;
	return INTERFACE_SENSOR_RET_OK;
}


static uxb_ldet_ret_t decode_get_stats(UxbLdet *self, uint8_t *buf, size_t len) {

	LdetResponse msg = LdetResponse_init_zero;
	pb_istream_t istream;
        istream = pb_istream_from_buffer(buf, len);

	if (pb_decode(&istream, LdetResponse_fields, &msg)) {
		if (msg.which_content == LdetResponse_get_stats_response_tag) {
			if (msg.content.get_stats_response.has_signals_total) {
				self->signals_total = msg.content.get_stats_response.signals_total;
			}
			if (msg.content.get_stats_response.has_ema_level_rms) {
				self->ema_level_rms = msg.content.get_stats_response.ema_level_rms;
			}
			if (msg.content.get_stats_response.has_ema_level_peak) {
				self->ema_level_peak = msg.content.get_stats_response.ema_level_peak;
			}
			return UXB_LDET_RET_OK;
		}
	}

	return UXB_LDET_RET_FAILED;
}


static uxb_ldet_ret_t encode_get_stats(uint8_t *buf, size_t size, size_t *len) {
	LdetResponse msg = LdetResponse_init_zero;
	msg.which_content = LdetRequest_get_stats_tag;

	pb_ostream_t ostream;
        ostream = pb_ostream_from_buffer(buf, size);
        if (pb_encode(&ostream, LdetRequest_fields, &msg)) {
		*len = ostream.bytes_written;
		return UXB_LDET_RET_OK;
	} else {
		*len = 0;
		return UXB_LDET_RET_FAILED;
	}
}


static void get_stats(UxbLdet *self) {
	uint8_t buf[256];
	size_t len = 0;
	encode_get_stats(buf, sizeof(buf), &len);
	if (len == 0) {
		return;
	}
	if (iuxbslot_send(self->slot, buf, len, false) != IUXBSLOT_RET_OK) {
		return;
	}
	if (iuxbslot_receive(self->slot, buf, sizeof(buf), &len) != IUXBSLOT_RET_OK) {
		return;
	}
	decode_get_stats(self, buf, len);
}


static void receive_task(void *p) {
	UxbLdet *self = (UxbLdet *)p;

	while (self->receive_can_run) {
		get_stats(self);
		vTaskDelay(1000);
	}

	vTaskDelete(NULL);
}


uxb_ldet_ret_t uxb_ldet_init(UxbLdet *self, IUxbSlot *slot) {
	if (u_assert(self != NULL)) {
		return UXB_LDET_RET_FAILED;
	}

	memset(self, 0, sizeof(UxbLdet));
	uhal_module_init(&self->module);
	self->slot = slot;

	interface_sensor_init(&(self->sensor_signals_total));
	self->sensor_signals_total.vmt.info = sensor_signals_total_info;
	self->sensor_signals_total.vmt.value = sensor_signals_total_value;
	self->sensor_signals_total.vmt.context = (void *)self;
	iservicelocator_add(
		locator,
		ISERVICELOCATOR_TYPE_SENSOR,
		&(self->sensor_signals_total.interface),
		"ldet_signals"
	);

	interface_sensor_init(&(self->sensor_ema_level_rms));
	self->sensor_ema_level_rms.vmt.info = sensor_ema_level_info;
	self->sensor_ema_level_rms.vmt.value = sensor_ema_level_rms_value;
	self->sensor_ema_level_rms.vmt.context = (void *)self;
	iservicelocator_add(
		locator,
		ISERVICELOCATOR_TYPE_SENSOR,
		&(self->sensor_ema_level_rms.interface),
		"ldet_ema_level_rms"
	);

	interface_sensor_init(&(self->sensor_ema_level_peak));
	self->sensor_ema_level_peak.vmt.info = sensor_ema_level_info;
	self->sensor_ema_level_peak.vmt.value = sensor_ema_level_peak_value;
	self->sensor_ema_level_peak.vmt.context = (void *)self;
	iservicelocator_add(
		locator,
		ISERVICELOCATOR_TYPE_SENSOR,
		&(self->sensor_ema_level_peak.interface),
		"ldet_ema_level_peak"
	);

	/* The slot is initialized, number is set. */
	iuxbslot_set_slot_buffer(self->slot, self->slot_buffer, 128);

	self->receive_can_run = true;
	xTaskCreate(receive_task, "uxb-ldet", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &(self->receive_task));
	if (self->receive_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		return UXB_LDET_RET_FAILED;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("module initialized"));
	return UXB_LDET_RET_OK;
}


uxb_ldet_ret_t uxb_ldet_free(UxbLdet *self) {
	if (u_assert(self != NULL)) {
		return UXB_LDET_RET_FAILED;
	}

	return UXB_LDET_RET_OK;
}

