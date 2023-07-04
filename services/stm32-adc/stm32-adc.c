/* SPDX-License-Identifier: BSD-2-Clause
 *
 * STM32 internal ADC driver
 *
 * Copyright (c) 2016-2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libopencm3/stm32/adc.h>

#include <main.h>
#include <interfaces/adc.h>
#include "stm32-adc.h"

#define MODULE_NAME "adc-stm32"


static adc_ret_t stm32_adc_convert_single(Adc *adc, adc_channel_t input, adc_sample_t *sample) {
	Stm32Adc *self = (Stm32Adc *)adc->parent;
	/** @todo lock ADC device */

	uint8_t channels[16];

	/* Convert the desired input now. */
	channels[0] = input;
	adc_set_regular_sequence(self->adc, 1, channels);
	adc_start_conversion_regular(self->adc);
	while (!adc_eoc(self->adc)) {
		;
	}

	/* And compute the resulting value in millivolts. */
	*sample = adc_read_regular(ADC1);
	return ADC_RET_OK;
}


static const struct adc_vmt stm32_adc_vmt = {
	.convert_single = stm32_adc_convert_single,
};


stm32_adc_ret_t stm32_adc_init(Stm32Adc *self, uint32_t adc) {
	memset(self, 0, sizeof(Stm32Adc));
	self->adc = adc;

	#if defined(STM32L4) || defined(STM32G4)
		ADC_CR(adc) &= ~ADC_CR_DEEPPWD;
		adc_enable_regulator(adc);
		vTaskDelay(1);
		adc_calibrate(adc);
		adc_power_on(adc);
		adc_set_sample_time_on_all_channels(adc, ADC_SMPR_SMP_12DOT5CYC);
		adc_set_resolution(adc, ADC_CFGR1_RES_12_BIT);
		adc_enable_vrefint();
	#else
		adc_calibrate(adc);
		adc_power_on(adc);
		adc_disable_scan_mode(adc);
		adc_set_sample_time_on_all_channels(adc, ADC_SMPR_SMP_112CYC);
		ADC_CCR |= ADC_CCR_TSVREFE;
	#endif
	adc_set_single_conversion_mode(adc);


	/* Setup the Adc interface now. */
	self->iface.vmt = &stm32_adc_vmt;
	self->iface.parent = self;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("ADC initialised"));

	return STM32_ADC_RET_OK;
}

