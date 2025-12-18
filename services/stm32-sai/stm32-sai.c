/* SPDX-License-Identifier: BSD-2-Clause
 *
 * STM32 SAI driver service
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include <main.h>

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>

#include <interfaces/waveform-sink.h>
#include "stm32-sai.h"

#define MODULE_NAME "stm32-sai"

#include "notification.inc"
size_t notification_pos = 0;

static waveform_sink_ret_t stm32_sai_waveform_sink_start(WaveformSink *ws) {
	WaveformSink *self = ws->parent;

	notification_pos = 0;
	SAI1_AIM |= SAI_AIM_FREQIE;

	return WAVEFORM_SINK_RET_OK;
}


static const struct waveform_sink_vmt stm32_sai_waveform_sink_vmt = {
	.start = &stm32_sai_waveform_sink_start,
	.stop = NULL,
	.write = NULL,
	.set_format = NULL,
	.get_format = NULL,
	.set_sample_rate = NULL,
	.get_sample_rate = NULL
};

stm32_sai_ret_t stm32_sai_init(Stm32Sai *self, uint32_t dev) {
	memset(self, 0, sizeof(Stm32Sai));
	self->dev = dev;
	SAI1_ACR1 &= ~SAI_CR1_SAIEN;
	SAI1_ACR1 = SAI_CR1_MCKEN | (15 << SAI_CR1_MCKDIV_SHIFT) | 0x80;
	SAI1_AFRCR = 0x0002001f;
	SAI1_ASLOTR = 0x00010000;

	SAI1_AIM |= SAI_AIM_FREQIE;
	SAI1_AIM |= SAI_AIM_OVRUDRIE;

	SAI1_ACR1 |= SAI_CR1_SAIEN;


	self->sink.parent = self;
	self->sink.vmt = &stm32_sai_waveform_sink_vmt;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("bus initialized"));

	return STM32_SAI_RET_OK;
}


stm32_sai_ret_t stm32_sai_free(Stm32Sai *self) {

	return STM32_SAI_RET_OK;
}


/* This is very PoC, much temporary. */
stm32_sai_ret_t stm32_sai_irq_handler(Stm32Sai *self) {
	if (SAI1_ASR & SAI_ASR_FREQ) {
		if (notification_pos < sizeof(notification)) {
			SAI1_ADR16 = *(uint16_t *)&(notification[notification_pos]);
			notification_pos += 2;
		} else {
			SAI1_AIM &= ~SAI_AIM_FREQIE;
		}
	}

	if (SAI1_ASR & SAI_ASR_OVRUDR) {
		SAI1_ACLRFR |= SAI_ACLRFR_COVRUDR;
	}


	return STM32_SAI_RET_OK;
}
