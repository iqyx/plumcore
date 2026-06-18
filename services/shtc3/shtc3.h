#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <waveform-source.h>

#include <main.h>

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

	/* Both sensor values come from a single conversion. The lock makes the check-and-refresh of the cache below
	 * atomic, so temperature and humidity are always read from the same measurement even when the two Sensor
	 * instances are queried from different tasks. */
	SemaphoreHandle_t lock;
	bool valid;
	TickType_t last_update;
	float temp_value;
	float rh_value;
} Shtc3;


shtc3_ret_t shtc3_init(Shtc3 *self, I2cBus *i2c);
shtc3_ret_t shtc3_free(Shtc3 *self);

