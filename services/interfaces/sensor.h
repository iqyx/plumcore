/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Simple interface for reading sensor values
 *
 * Copyright (c) 2017-2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>


typedef enum {
	SENSOR_RET_OK = 0,
	SENSOR_RET_FAILED,
} sensor_ret_t;

struct sensor_info {
	char *description; /* eg. die temperature */
	char *unit; /* eg. °C */
};

typedef struct sensor Sensor;
struct sensor_vmt {
	sensor_ret_t (*value_f)(Sensor *self, float *value);
	sensor_ret_t (*value_i32)(Sensor *self, int32_t *value);
};

typedef struct sensor {
	const struct sensor_vmt *vmt;
	const struct sensor_info *info;
	void *parent;
} Sensor;





