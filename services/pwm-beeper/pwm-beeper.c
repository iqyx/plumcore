/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PWM beeper service
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

#include <main.h>
#include "pwm-beeper.h"

#include <interfaces/beeper.h>
#include <interfaces/pwm.h>

#define MODULE_NAME "pwm-beeper"
#define SEQ_BUF_LEN 16


static pwm_beeper_ret_t create_task(PwmBeeper *self);

/**********************************************************************************************************************
 * Beeper interface implementation
 **********************************************************************************************************************/

static beeper_ret_t beeper_sequence(Beeper *beeper, const beeper_seq_item_t *sequence) {
	PwmBeeper *self = beeper->parent;
	self->sequence = sequence;
	create_task(self);

	return BEEPER_RET_OK;
}


static const struct beeper_vmt pwm_beeper_vmt = {
	.sequence = beeper_sequence,
};


/**********************************************************************************************************************
 * Service implementation
 **********************************************************************************************************************/

static void task(void *p) {
	PwmBeeper *self = p;
	beeper_seq_item_t seq[SEQ_BUF_LEN] = {0};
	uint32_t repeats_done = 0;

	while (self->task_needed) {
		/* Assume the sequence ends after first run by default. */
		self->task_needed = false;

		if (self->sequence) {
			size_t i = 0;
			for (i = 0; i < SEQ_BUF_LEN && self->sequence[i]; i++) {
				seq[i] = self->sequence[i];
			}
			seq[i] = BEEPER_SEQ_END;
		}

		for (size_t pos = 0; pos < SEQ_BUF_LEN && seq[pos]; pos++) {
			uint32_t cmd = seq[pos] & 0x3u;
			uint32_t time_ms = (seq[pos] >> 2) & 0xffffu;
			uint32_t freq_hz = (seq[pos] >> 18) & 0x3fffu;

			if (cmd == BEEPER_SEQ_REPEAT) {
				/* The repeat count shares the frequency bit field. Zero means
				 * repeat indefinitely, otherwise repeat the given number of times. */
				uint32_t count = freq_hz;
				if (count == 0 || repeats_done < count) {
					repeats_done++;
					self->task_needed = true;
				}
			} else if (cmd == BEEPER_SEQ_BEEP && freq_hz > 0) {
				self->pwm->vmt->set_freq(self->pwm, freq_hz);
				self->pwm->vmt->set_pwm(self->pwm, 0.5f);
			} else {
				self->pwm->vmt->set_pwm(self->pwm, 0.0f);
			}

			if (time_ms > 0) {
				vTaskDelay(pdMS_TO_TICKS(time_ms));
			}
		}

		self->pwm->vmt->set_pwm(self->pwm, 0.0f);
		vTaskDelay(10);
	}

	vTaskDelete(NULL);
}


static pwm_beeper_ret_t create_task(PwmBeeper *self) {

	self->task_needed = true;
	xTaskCreate(task, "pwm-beeper", configMINIMAL_STACK_SIZE, (void *)self, 1, &(self->task));
	if (self->task == NULL) {
		return PWM_BEEPER_RET_FAILED;
	}

	return PWM_BEEPER_RET_OK;
}


pwm_beeper_ret_t pwm_beeper_init(PwmBeeper *self, Pwm *pwm) {
	memset(self, 0, sizeof(PwmBeeper));
	self->pwm = pwm;

	self->beeper.parent = self;
	self->beeper.vmt = &pwm_beeper_vmt;

	return PWM_BEEPER_RET_OK;
}


pwm_beeper_ret_t pwm_beeper_free(PwmBeeper *self) {
	memset(self, 0, sizeof(PwmBeeper));

	return PWM_BEEPER_RET_OK;
}
