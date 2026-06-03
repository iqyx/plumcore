#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include <main.h>

#include <interfaces/i2c-bus.h>
#include <interfaces/waveform-source.h>

#include "shtc3.h"

//#define DEBUG
#define MODULE_NAME "shtc3"


static shtc3_ret_t shtc3_cmd(Shtc3 *self, uint16_t cmd, uint8_t *ret, size_t len) {
	uint8_t txbuf[2] = {cmd >> 8, cmd & 0xff};

	if (self->i2c->vmt->transfer(self->i2c, SHTC3_ADDR, txbuf, sizeof(txbuf), ret, len) != I2C_BUS_RET_OK) {
		return SHTC3_RET_FAILED;
	}

	return SHTC3_RET_OK;
}


/**********************************************************************************************************************
 * Temperature Sensor API
 **********************************************************************************************************************/

static sensor_ret_t temp_sensor_value_f(Sensor *sensor, float *value) {
	Shtc3 *self = sensor->parent;

	uint8_t d[3] = {0};
	if (shtc3_cmd(self, 0x7ca2, d, sizeof(d)) != SHTC3_RET_OK) {
		return SENSOR_RET_FAILED;
	}

	float temp = -45 + 175 * ((d[0] << 8) | d[1]) / 65536;
	//float rh = 100 * ((d[3] << 8) | d[4]) / 65536;

	if (value != NULL) {
		*value = temp;
		return SENSOR_RET_OK;
	}

	return SENSOR_RET_FAILED;
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

	uint8_t d[3] = {0};
	if (shtc3_cmd(self, 0x5c24, d, sizeof(d)) != SHTC3_RET_OK) {
		return SENSOR_RET_FAILED;
	}

	float rh = 100.0f * ((d[0] << 8) | d[1]) / 65536;

	if (value != NULL) {
		*value = rh;
		return SENSOR_RET_OK;
	}

	return SENSOR_RET_FAILED;
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

	if (shtc3_cmd(self, 0xefc8, self->id, sizeof(self->id)) != SHTC3_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("SHTC3 sensor not found"));
		return SHTC3_RET_FAILED;
	}

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
	(void)self;

	return SHTC3_RET_OK;
}



