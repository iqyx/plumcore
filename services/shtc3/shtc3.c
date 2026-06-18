#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include <main.h>

#include <interfaces/i2c-bus.h>
#include <interfaces/waveform-source.h>

#include "shtc3.h"

//#define DEBUG
#define MODULE_NAME "shtc3"

/* How long a single conversion is reused before a new measurement is triggered. */
#define SHTC3_CACHE_VALID_MS 1000

/* SHTC3 commands (16 bit, MSB first). */
#define SHTC3_CMD_WAKEUP 0x3517
#define SHTC3_CMD_SLEEP 0xb098
#define SHTC3_CMD_READ_ID 0xefc8
/* Normal mode, temperature first, clock stretching enabled: the sensor holds SCL low while it converts and then
 * returns six bytes: T MSB, T LSB, T CRC, RH MSB, RH LSB, RH CRC. */
#define SHTC3_CMD_MEASURE 0x7ca2


static shtc3_ret_t shtc3_cmd(Shtc3 *self, uint16_t cmd, uint8_t *ret, size_t len) {
	uint8_t txbuf[2] = {cmd >> 8, cmd & 0xff};

	if (self->i2c->vmt->transfer(self->i2c, SHTC3_ADDR, txbuf, sizeof(txbuf), ret, len) != I2C_BUS_RET_OK) {
		return SHTC3_RET_FAILED;
	}

	return SHTC3_RET_OK;
}


/* CRC-8 as specified in the SHTC3 datasheet (polynomial 0x31, initial value 0xff) used to validate each 16-bit
 * word returned by the sensor. */
static uint8_t shtc3_crc(const uint8_t *data, size_t len) {
	uint8_t crc = 0xff;
	for (size_t i = 0; i < len; i++) {
		crc ^= data[i];
		for (uint8_t bit = 0; bit < 8; bit++) {
			if (crc & 0x80) {
				crc = (crc << 1) ^ 0x31;
			} else {
				crc = crc << 1;
			}
		}
	}
	return crc;
}


/* Refresh the cached temperature/humidity pair if the previous conversion is older than SHTC3_CACHE_VALID_MS (or
 * there is none yet). Must be called with self->lock held. A single measurement yields both values, so reading
 * temperature and humidity back to back triggers only one conversion. */
static shtc3_ret_t shtc3_update(Shtc3 *self) {
	TickType_t now = xTaskGetTickCount();
	if (self->valid && (now - self->last_update) < pdMS_TO_TICKS(SHTC3_CACHE_VALID_MS)) {
		return SHTC3_RET_OK;
	}

	/* The sensor ignores everything but the wakeup command while in sleep mode; it needs ~240 us to wake up. */
	if (shtc3_cmd(self, SHTC3_CMD_WAKEUP, NULL, 0) != SHTC3_RET_OK) {
		return SHTC3_RET_FAILED;
	}
	vTaskDelay(pdMS_TO_TICKS(1));

	uint8_t d[6] = {0};
	shtc3_ret_t ret = shtc3_cmd(self, SHTC3_CMD_MEASURE, d, sizeof(d));

	/* Always return to sleep, even if the measurement failed, to keep the sensor in a defined low-power state. */
	shtc3_cmd(self, SHTC3_CMD_SLEEP, NULL, 0);

	if (ret != SHTC3_RET_OK) {
		return SHTC3_RET_FAILED;
	}
	if (shtc3_crc(&d[0], 2) != d[2] || shtc3_crc(&d[3], 2) != d[5]) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("measurement CRC mismatch"));
		return SHTC3_RET_FAILED;
	}

	self->temp_value = -45.0f + 175.0f * (float)((d[0] << 8) | d[1]) / 65536.0f;
	self->rh_value = 100.0f * (float)((d[3] << 8) | d[4]) / 65536.0f;
	self->last_update = now;
	self->valid = true;

	return SHTC3_RET_OK;
}


/**********************************************************************************************************************
 * Temperature Sensor API
 **********************************************************************************************************************/

static sensor_ret_t temp_sensor_value_f(Sensor *sensor, float *value) {
	Shtc3 *self = sensor->parent;
	if (value == NULL) {
		return SENSOR_RET_FAILED;
	}

	sensor_ret_t ret = SENSOR_RET_FAILED;
	xSemaphoreTake(self->lock, portMAX_DELAY);
	if (shtc3_update(self) == SHTC3_RET_OK) {
		*value = self->temp_value;
		ret = SENSOR_RET_OK;
	}
	xSemaphoreGive(self->lock);

	return ret;
}


static const struct sensor_vmt temp_sensor_vmt = {
	.value_f = temp_sensor_value_f,
};


static const struct sensor_info temp_sensor_info = {
	.description = "PCB temperature",
	.unit = "°C",
};


/**********************************************************************************************************************
 * Relative humidity Sensor API
 **********************************************************************************************************************/

static sensor_ret_t rh_sensor_value_f(Sensor *sensor, float *value) {
	Shtc3 *self = sensor->parent;
	if (value == NULL) {
		return SENSOR_RET_FAILED;
	}

	sensor_ret_t ret = SENSOR_RET_FAILED;
	xSemaphoreTake(self->lock, portMAX_DELAY);
	if (shtc3_update(self) == SHTC3_RET_OK) {
		*value = self->rh_value;
		ret = SENSOR_RET_OK;
	}
	xSemaphoreGive(self->lock);

	return ret;
}


static const struct sensor_vmt rh_sensor_vmt = {
	.value_f = rh_sensor_value_f,
};


static const struct sensor_info rh_sensor_info = {
	.description = "PCB humidity",
	.unit = "%Rh",
};



shtc3_ret_t shtc3_init(Shtc3 *self, I2cBus *i2c) {
	memset(self, 0, sizeof(Shtc3));
	self->i2c = i2c;

	self->lock = xSemaphoreCreateMutex();
	if (self->lock == NULL) {
		return SHTC3_RET_FAILED;
	}

	/* The sensor powers up in sleep mode; wake it before probing the ID register. */
	if (shtc3_cmd(self, SHTC3_CMD_WAKEUP, NULL, 0) != SHTC3_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("SHTC3 sensor not found"));
		return SHTC3_RET_FAILED;
	}
	vTaskDelay(pdMS_TO_TICKS(1));

	if (shtc3_cmd(self, SHTC3_CMD_READ_ID, self->id, sizeof(self->id)) != SHTC3_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("SHTC3 sensor not found"));
		return SHTC3_RET_FAILED;
	}

	/* Probing is done, leave the sensor in its low-power sleep state. */
	shtc3_cmd(self, SHTC3_CMD_SLEEP, NULL, 0);

	if (!(((self->id[1] & 0x3f) == 0x07) && (self->id[0] & 0x08))) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("SHTC3 sensor probe failed (wrong ID)"));
		return SHTC3_RET_FAILED;
	}

	self->temp.parent = self;
	self->temp.vmt = &temp_sensor_vmt;
	self->temp.info = &temp_sensor_info;

	self->rh.parent = self;
	self->rh.vmt = &rh_sensor_vmt;
	self->rh.info = &rh_sensor_info;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialized, id = 0x%02x, 0x%02x, 0x%02x"), self->id[0], self->id[1], self->id[2]);

	return SHTC3_RET_OK;
}


shtc3_ret_t shtc3_free(Shtc3 *self) {
	if (self->lock != NULL) {
		vSemaphoreDelete(self->lock);
	}

	return SHTC3_RET_OK;
}



