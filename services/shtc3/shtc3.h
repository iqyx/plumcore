#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <waveform_source.h>

#include <interfaces/i2c-bus.h>
#include <interfaces/sensor.h>

#define SHTC3_ADDR 0x70


typedef enum {
	SHTC3_RET_OK = 0,
	SHTC3_RET_FAILED = -1,
} shtc3_ret_t;

typedef struct {
	I2cBus *i2c;
	Sensor temp;
	Sensor rh;
	uint8_t id[3];
} Shtc3;


shtc3_ret_t shtc3_init(Shtc3 *self, I2cBus *i2c);
shtc3_ret_t shtc3_free(Shtc3 *self);

