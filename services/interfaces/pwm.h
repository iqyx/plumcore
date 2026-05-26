/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PWM interface
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
	PWM_RET_OK = 0,
	PWM_RET_FAILED,
} pwm_ret_t;

typedef struct pwm Pwm;

struct pwm_vmt {
	pwm_ret_t (*set_pwm)(Pwm *self, float duty);
	pwm_ret_t (*get_pwm)(Pwm *self, float *duty);
	pwm_ret_t (*set_freq)(Pwm *self, uint32_t freq_hz);
	pwm_ret_t (*get_freq)(Pwm *self, uint32_t *freq_hz);
};


typedef struct pwm {
	const struct pwm_vmt *vmt;
	void *parent;
} Pwm;
