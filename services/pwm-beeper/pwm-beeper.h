/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PWM beeper service
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <main.h>
#include <interfaces/beeper.h>
#include <interfaces/pwm.h>

typedef enum {
	PWM_BEEPER_RET_OK = 0,
	PWM_BEEPER_RET_FAILED,
} pwm_beeper_ret_t;

typedef struct pwm_beeper {
	Beeper beeper;
	Pwm *pwm;

	TaskHandle_t task;
	bool task_needed;

	volatile const beeper_seq_item_t *sequence;
} PwmBeeper;


pwm_beeper_ret_t pwm_beeper_init(PwmBeeper *self, Pwm *pwm);
pwm_beeper_ret_t pwm_beeper_free(PwmBeeper *self);
