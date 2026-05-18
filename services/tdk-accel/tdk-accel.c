/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Generic driver for TDK accelerometers and gyroscopes
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>

#include <main.h>

#include <interfaces/spi.h>
#include <interfaces/sensor.h>

#include "tdk-accel.h"

#define MODULE_NAME "tdk-accel"


/** @todo this simply doesn't work as the datasheet says. Zero is returned instead. */
static uint32_t read_sys(TdkAccel *self, uint8_t reg) {
	uint8_t txbuf[3] = {0x7c, reg, 0x01};
	uint8_t rxbuf[3] = {};

	/* Send a 3 byte command to read the requested register, see GYPRO4300 DS, fig. 21. */
	self->spidev->vmt->select(self->spidev);
	self->spidev->vmt->exchange(self->spidev, txbuf, rxbuf, sizeof(txbuf));
	self->spidev->vmt->deselect(self->spidev);

	vTaskDelay(2);

	/* Read the result. */
	uint8_t txbuf2[5] = {0x58, 0x00, 0x00, 0x00, 0x00};
	uint8_t rxbuf2[5] = {};

	self->spidev->vmt->select(self->spidev);
	self->spidev->vmt->exchange(self->spidev, txbuf2, rxbuf2, sizeof(txbuf2));
	self->spidev->vmt->deselect(self->spidev);

	return rxbuf2[1] << 24 | rxbuf2[2] << 16 | rxbuf2[3] << 8 | rxbuf2[4];
}


/***************************************************************************************************
 * Sensor interface implementation
 ***************************************************************************************************/

static sensor_ret_t tdk_accel_sensor_value_f(Sensor *sensor, float *value) {
	TdkAccel *self = (TdkAccel *)sensor->parent;

	if (self->dry_en) {
		xSemaphoreTake(self->dry_sem, portMAX_DELAY);
	}

	/* Capture the timestamp as soon as possible. */
	if (self->ts_clock != NULL) {
		if (self->ts_clock->vmt->get(self->ts_clock, &self->timestamp) != CLOCK_RET_OK) {
			self->timestamp.tv_sec = 0;
			self->timestamp.tv_nsec = 0;
		}
	}

	uint8_t txbuf[5] = {0x50, 0x00, 0x00, 0x00, 0x00};
	uint8_t rxbuf[5] = {};

	self->spidev->vmt->select(self->spidev);
	self->spidev->vmt->exchange(self->spidev, txbuf, rxbuf, sizeof(txbuf));
	self->spidev->vmt->deselect(self->spidev);

	if (!(rxbuf[1] & 0x80)) {
		/* The value is not current. No new value generated. */
		return SENSOR_RET_FAILED;
	}

	uint32_t value_u = (((rxbuf[1] << 24) | (rxbuf[2] << 16) | (rxbuf[3] << 8) | rxbuf[4]) >> 7) & 0x00fffffful;
	int32_t value_s = (int32_t)(value_u << 8) >> 8;

	if (value != NULL) {
		*value = value_s / self->scale_factor;
		/* Do not return any error here. Value is allowed to be NULL. */
	}

	return SENSOR_RET_OK;
}


static sensor_ret_t tdk_accel_sensor_get_timestamp(Sensor *sensor, struct timespec *ts) {
	TdkAccel *self = (TdkAccel *)sensor->parent;

	if (ts != NULL) {
		memcpy(ts, &self->timestamp, sizeof(struct timespec));
		return SENSOR_RET_OK;
	}

	return SENSOR_RET_FAILED;
}


static const struct sensor_vmt tdk_accel_sensor_vmt = {
	.value_f = tdk_accel_sensor_value_f,
	.get_timestamp = tdk_accel_sensor_get_timestamp,
};


const struct sensor_info tdk_accel_sensor_info = {
	.description = "Accel/gyro output",
	.unit = "",
};


/***************************************************************************************************
 * Service implementation
 ***************************************************************************************************/

tdk_accel_ret_t tdk_accel_init(TdkAccel *self, SpiDev *spidev) {
	if (u_assert(self != NULL) ||
	    u_assert(spidev != NULL)) {
		return TDK_ACCEL_RET_BAD_PARAM;
	}
	memset(self, 0, sizeof(TdkAccel));
	self->spidev = spidev;
	self->uid = read_sys(self, 0x03);
	self->scale_factor = 1.0f;

	self->dry_sem = xSemaphoreCreateBinary();
	if (self->dry_sem == NULL) {
		goto err;
	}

	self->sensor.parent = self;
	self->sensor.vmt = &tdk_accel_sensor_vmt;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialized, UID = %08x"), self->uid);
	return TDK_ACCEL_RET_OK;
err:
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("init failed"));
	return TDK_ACCEL_RET_FAILED;
}


tdk_accel_ret_t tdk_accel_free(TdkAccel *self) {
	if (u_assert(self != NULL)) {
		return TDK_ACCEL_RET_BAD_PARAM;
	}

	/* Nothing to free now. */

	return TDK_ACCEL_RET_OK;
}


tdk_accel_ret_t tdk_accel_set_scale_factor(TdkAccel *self, float scale_factor) {
	if (u_assert(self != NULL)) {
		return TDK_ACCEL_RET_BAD_PARAM;
	}

	self->scale_factor = scale_factor;

	return TDK_ACCEL_RET_OK;
}


tdk_accel_ret_t tdk_accel_enable_drdy(TdkAccel *self) {
	if (u_assert(self != NULL)) {
		return TDK_ACCEL_RET_BAD_PARAM;
	}

	self->dry_en = true;

	return TDK_ACCEL_RET_OK;
}


tdk_accel_ret_t tdk_accel_drdy(TdkAccel *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->dry_en)) {
		return TDK_ACCEL_RET_BAD_PARAM;
	}

	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	xSemaphoreGiveFromISR(self->dry_sem, &xHigherPriorityTaskWoken);
	portYIELD_FROM_ISR(xHigherPriorityTaskWoken);

	return TDK_ACCEL_RET_OK;
}


tdk_accel_ret_t tdk_accel_enable_timestamping(TdkAccel *self, Clock *ts_clock) {
	if (u_assert(self != NULL) ||
	    u_assert(ts_clock != NULL)) {
		return TDK_ACCEL_RET_BAD_PARAM;
	}

	self->ts_clock = ts_clock;

	return TDK_ACCEL_RET_OK;
}
