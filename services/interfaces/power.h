/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Power device interface
 *
 * Copyright (c) 2022, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
	POWER_RET_OK = 0,
	POWER_RET_FAILED,
} power_ret_t;

enum power_polarity {
	POWER_POLARITY_POSITIVE = 0,
	POWER_POLARITY_NEGATIVE,
};

typedef struct power Power;

struct power_vmt {
	/**
	 * @brief Enable or disable the power device.
	 *
	 * If the power device is sourcing, @p enable usually enables the regulator sourcing
	 * the power rail. The implementation may vary, including enabling OR-ing controllers,
	 * power switches, battery disconnect switches, etc.
	 */
	power_ret_t (*enable)(Power *self, bool enable);

	power_ret_t (*set_voltage)(Power *self, float voltage_v);
	power_ret_t (*set_current_limit)(Power *self, float current_i);

	power_ret_t (*get_voltage)(Power *self, float *voltage_v);
	power_ret_t (*get_current)(Power *self, float *current_i);
};


typedef struct power {
	const struct power_vmt *vmt;
	void *parent;
} Power;

