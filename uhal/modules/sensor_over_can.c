/*
 * Copyright (c) 2018, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"
#include "port.h"

#include "interface.h"
#include "sensor_over_can.h"
#include "interfaces/can.h"
#include "interfaces/ccan.h"
#include "interfaces/sensor.h"


#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "sensor-over-can"


static sensor_over_can_ret_t buf_to_value(const uint8_t *buf, size_t len, size_t offset, enum sensor_over_can_value_type value_type, float *value) {
	switch (value_type) {
		case SENSOR_OVER_CAN_VALUE_TYPE_FLOAT: {
			if ((len + offset) < 4) {
				return SENSOR_OVER_CAN_RET_FAILED;
			}
			float ret;
			memcpy((void *)&ret, buf + offset, 4);
			*value = ret;
			break;
		}
		case SENSOR_OVER_CAN_VALUE_TYPE_UINT8: {
			if ((len + offset) < 1) {
				return SENSOR_OVER_CAN_RET_FAILED;
			}
			uint8_t ret;
			memcpy((void *)&ret, buf + offset, 1);
			*value = (float)ret;
			break;
		}
		case SENSOR_OVER_CAN_VALUE_TYPE_INT8: {
			if ((len + offset) < 1) {
				return SENSOR_OVER_CAN_RET_FAILED;
			}
			int8_t ret;
			memcpy((void *)&ret, buf + offset, 1);
			*value = (float)ret;
			break;
		}
		case SENSOR_OVER_CAN_VALUE_TYPE_UINT16: {
			if ((len + offset) < 2) {
				return SENSOR_OVER_CAN_RET_FAILED;
			}
			uint16_t ret;
			memcpy((void *)&ret, buf + offset, 2);
			*value = (float)ret;
			break;
		}
		case SENSOR_OVER_CAN_VALUE_TYPE_INT16: {
			if ((len + offset) < 2) {
				return SENSOR_OVER_CAN_RET_FAILED;
			}
			int16_t ret;
			memcpy((void *)&ret, buf + offset, 2);
			*value = (float)ret;
			break;
		}
		case SENSOR_OVER_CAN_VALUE_TYPE_UINT32: {
			if ((len + offset) < 4) {
				return SENSOR_OVER_CAN_RET_FAILED;
			}
			uint32_t ret;
			memcpy((void *)&ret, buf + offset, 4);
			*value = (float)ret;
			break;
		}
		case SENSOR_OVER_CAN_VALUE_TYPE_INT32: {
			if ((len + offset) < 4) {
				return SENSOR_OVER_CAN_RET_FAILED;
			}
			int32_t ret;
			memcpy((void *)&ret, buf + offset, 4);
			*value = (float)ret;
			break;
		}
		default:
			return SENSOR_OVER_CAN_RET_FAILED;
	}

	return SENSOR_OVER_CAN_RET_OK;
}


static void sensor_over_can_task(void *p) {
	SensorOverCan *self = (SensorOverCan *)p;

	CCan ccan;
	if (ccan_open(&ccan, self->can) != CCAN_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot open the CAN interface"));
		goto err;
	}

	while (self->can_run) {
		self->running = true;

		uint8_t buf[8] = {0};
		size_t len = 0;
		uint32_t id = 0;
		if (ccan_receive(&ccan, buf, &len, &id, 1000) == CCAN_RET_OK) {
			if (id != self->can_id) {
				continue;
			}

			float f = 0.0;
			buf_to_value(buf, len, self->buffer_offset, self->value_type, &f);
			self->value = f;
			self->no_value_sec = 0;
			self->value_status = SENSOR_OVER_CAN_VALUE_STATUS_FRESH;
			self->received_values++;
		} else {
			self->no_value_sec++;
		}
		if (self->no_value_sec >= SENSOR_OVER_CAN_FRESH_VALUE_TIMEOUT_SEC) {
			self->value_status = SENSOR_OVER_CAN_VALUE_STATUS_STALLED;
		}
	}

err:
	ccan_close(&ccan);
	self->running = false;
	vTaskDelete(NULL);
}


static interface_sensor_ret_t sensor_over_can_info(void *context, ISensorInfo *info) {
	if (u_assert(context != NULL) ||
	    u_assert(info != NULL)) {
		return INTERFACE_SENSOR_RET_FAILED;
	}

	SensorOverCan *self = (SensorOverCan *)context;

	/** @todo how to populate this? */
	info->quantity_name = self->quantity_name;
	info->quantity_symbol = self->quantity_symbol;
	info->unit_name = self->unit_name;
	info->unit_symbol = self->unit_symbol;

	return INTERFACE_SENSOR_RET_OK;
}


static interface_sensor_ret_t sensor_over_can_value(void *context, float *value) {
	if (u_assert(context != NULL) ||
	    u_assert(value != NULL)) {
		return INTERFACE_SENSOR_RET_FAILED;
	}

	SensorOverCan *self = (SensorOverCan *)context;

	*value = self->value;

	return INTERFACE_SENSOR_RET_OK;
}


sensor_over_can_ret_t sensor_over_can_init(SensorOverCan *self) {
	if (u_assert(self != NULL)) {
		return SENSOR_OVER_CAN_RET_NULL;
	}
	memset(self, 0, sizeof(SensorOverCan));
	uhal_interface_init(&self->interface);
	self->initialized = true;
	self->value_type = SENSOR_OVER_CAN_VALUE_TYPE_FLOAT;

	interface_sensor_init(&(self->sensor));
	self->sensor.vmt.info = sensor_over_can_info;
	self->sensor.vmt.value = sensor_over_can_value;
	self->sensor.vmt.context = (void *)self;

	iservicelocator_add(
		locator,
		ISERVICELOCATOR_TYPE_SENSOR,
		&(self->sensor.interface),
		"can-sensor"
	);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("module initialized"));
	return SENSOR_OVER_CAN_RET_OK;
}


sensor_over_can_ret_t sensor_over_can_free(SensorOverCan *self) {
	if (u_assert(self != NULL)) {
		return SENSOR_OVER_CAN_RET_NULL;
	}
	if (u_assert(self->initialized)) {
		return SENSOR_OVER_CAN_RET_UNINITIALIZED;
	}
	self->initialized = false;

	/* Sensor interface cannot be freed. */
	/* interface_sensor_free(&self->sensor); */

	return SENSOR_OVER_CAN_RET_OK;
}


sensor_over_can_ret_t sensor_over_can_start(SensorOverCan *self) {
	if (u_assert(self != NULL)) {
		return SENSOR_OVER_CAN_RET_NULL;
	}
	if (u_assert(self->initialized)) {
		return SENSOR_OVER_CAN_RET_UNINITIALIZED;
	}
	if (self->can == NULL) {
		return SENSOR_OVER_CAN_RET_FAILED;
	}

	self->can_run = true;
	xTaskCreate(sensor_over_can_task, "sensor-over-can", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &(self->task));
	if (self->task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		return SENSOR_OVER_CAN_RET_FAILED;
	}
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("module started"));

	return SENSOR_OVER_CAN_RET_OK;
}


sensor_over_can_ret_t sensor_over_can_stop(SensorOverCan *self) {
	if (u_assert(self != NULL)) {
		return SENSOR_OVER_CAN_RET_NULL;
	}
	if (u_assert(self->initialized)) {
		return SENSOR_OVER_CAN_RET_UNINITIALIZED;
	}

	/* Wait for the task to finish. */
	self->can_run = false;
	while (self->running) {
		vTaskDelay(10);
	}
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("module stopped"));

	return SENSOR_OVER_CAN_RET_OK;
}


sensor_over_can_ret_t sensor_over_can_set_can_bus(SensorOverCan *self, ICan *can) {
	if (u_assert(self != NULL)) {
		return SENSOR_OVER_CAN_RET_NULL;
	}
	if (u_assert(self->initialized)) {
		return SENSOR_OVER_CAN_RET_UNINITIALIZED;
	}
	if (u_assert(can != NULL)) {
		return SENSOR_OVER_CAN_RET_BAD_ARG;
	}

	self->can = can;

	return SENSOR_OVER_CAN_RET_OK;
}


sensor_over_can_ret_t sensor_over_can_set_id(SensorOverCan *self, uint32_t id) {
	if (u_assert(self != NULL)) {
		return SENSOR_OVER_CAN_RET_NULL;
	}
	if (u_assert(self->initialized)) {
		return SENSOR_OVER_CAN_RET_UNINITIALIZED;
	}

	self->can_id = id;

	return SENSOR_OVER_CAN_RET_OK;
}


sensor_over_can_ret_t sensor_over_can_set_name(SensorOverCan *self, const char *name) {
	if (u_assert(self != NULL)) {
		return SENSOR_OVER_CAN_RET_NULL;
	}
	if (u_assert(self->initialized)) {
		return SENSOR_OVER_CAN_RET_UNINITIALIZED;
	}
	if (u_assert(name != NULL)) {
		return SENSOR_OVER_CAN_RET_BAD_ARG;
	}

	strlcpy(self->name, name, SENSOR_OVER_CAN_NAME_LEN);
	if (iservicelocator_set_name(locator, &(self->interface), self->name) != ISERVICELOCATOR_RET_OK) {
		return SENSOR_OVER_CAN_RET_FAILED;
	}
	if (iservicelocator_set_name(locator, &(self->sensor.interface), self->name) != ISERVICELOCATOR_RET_OK) {
		return SENSOR_OVER_CAN_RET_FAILED;
	}

	return SENSOR_OVER_CAN_RET_OK;
}


sensor_over_can_ret_t sensor_over_can_set_quantity_name(SensorOverCan *self, const char *name) {
	if (u_assert(self != NULL)) {
		return SENSOR_OVER_CAN_RET_NULL;
	}
	if (u_assert(self->initialized)) {
		return SENSOR_OVER_CAN_RET_UNINITIALIZED;
	}
	if (u_assert(name != NULL)) {
		return SENSOR_OVER_CAN_RET_BAD_ARG;
	}

	strlcpy(self->quantity_name, name, SENSOR_OVER_CAN_QUANTITY_NAME_LEN);

	return SENSOR_OVER_CAN_RET_OK;
}


sensor_over_can_ret_t sensor_over_can_set_quantity_symbol(SensorOverCan *self, const char *symbol) {
	if (u_assert(self != NULL)) {
		return SENSOR_OVER_CAN_RET_NULL;
	}
	if (u_assert(self->initialized)) {
		return SENSOR_OVER_CAN_RET_UNINITIALIZED;
	}
	if (u_assert(symbol != NULL)) {
		return SENSOR_OVER_CAN_RET_BAD_ARG;
	}

	strlcpy(self->quantity_symbol, symbol, SENSOR_OVER_CAN_QUANTITY_SYMBOL_LEN);

	return SENSOR_OVER_CAN_RET_OK;
}


sensor_over_can_ret_t sensor_over_can_set_unit_name(SensorOverCan *self, const char *name) {
	if (u_assert(self != NULL)) {
		return SENSOR_OVER_CAN_RET_NULL;
	}
	if (u_assert(self->initialized)) {
		return SENSOR_OVER_CAN_RET_UNINITIALIZED;
	}
	if (u_assert(name != NULL)) {
		return SENSOR_OVER_CAN_RET_BAD_ARG;
	}

	strlcpy(self->unit_name, name, SENSOR_OVER_CAN_QUANTITY_NAME_LEN);

	return SENSOR_OVER_CAN_RET_OK;
}


sensor_over_can_ret_t sensor_over_can_set_unit_symbol(SensorOverCan *self, const char *symbol) {
	if (u_assert(self != NULL)) {
		return SENSOR_OVER_CAN_RET_NULL;
	}
	if (u_assert(self->initialized)) {
		return SENSOR_OVER_CAN_RET_UNINITIALIZED;
	}
	if (u_assert(symbol != NULL)) {
		return SENSOR_OVER_CAN_RET_BAD_ARG;
	}

	strlcpy(self->unit_symbol, symbol, SENSOR_OVER_CAN_QUANTITY_SYMBOL_LEN);

	return SENSOR_OVER_CAN_RET_OK;
}
