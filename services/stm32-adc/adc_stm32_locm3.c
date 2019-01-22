/*
 * uMeshFw libopencm3 STM32 ADC module
 *
 * Copyright (C) 2016, Marek Koza, qyx@krtko.org
 *
 * This file is part of uMesh node firmware (http://qyx.krtko.org/projects/umesh)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libopencm3/stm32/adc.h>
#include <libopencm3/stm32/rcc.h>

#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"

#include "module.h"
#include "interfaces/adc.h"

#include "adc_stm32_locm3.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "adc-stm32"


static iadc_ret_t sample(void *context, uint32_t input, int32_t *sample) {
	if (u_assert(context != NULL) ||
	    u_assert(sample != NULL)) {
		return IADC_RET_FAILED;
	}
	/* struct module_adc_stm32_locm3 *self = (struct module_adc_stm32_locm3 *)context; */

	/** @todo lock ADC device */

	uint8_t channels[16];

	/* Capture voltage of the internal voltage reference. */
	#if defined(STM32L4)
		channels[0] = 0;
	#else
		channels[0] = 17;
	#endif
	adc_set_regular_sequence(ADC1, 1, channels);
	adc_start_conversion_regular(ADC1);
	while (!adc_eoc(ADC1)) {
		;
	}

	/* Read Vref reading at 3.3V Vdda (calibration values, see the datasheet) and compute its voltage. */
	#if defined(STM32L4)
		uint16_t vref_cal = ST_VREFINT_CAL;
		uint32_t vref_uv = vref_cal * 3000000ull / 4096ull;
	#else
		uint16_t vref_cal = *(uint16_t *)0x1fff7a2a;
		uint32_t vref_uv = vref_cal * 3300000ull / 4096ull;
	#endif

	/* Compute Vdda using computed value of the internal Vref */
	uint32_t vdda_uv = vref_uv * 4096ull / adc_read_regular(ADC1);

	/* Convert the desired input now. */
	channels[0] = input;
	adc_set_regular_sequence(ADC1, 1, channels);
	adc_start_conversion_regular(ADC1);
	while (!adc_eoc(ADC1)) {
		;
	}

	/* And compute the resulting value in millivolts. */
	int32_t reading = adc_read_regular(ADC1);
	*sample = (int32_t)((uint64_t)reading * (uint64_t)vdda_uv / 4096ull);

	return IADC_RET_OK;
}


adc_stm32_locm3_ret_t adc_stm32_locm3_init(AdcStm32Locm3 *self, uint32_t adc) {
	if (u_assert(self != NULL)) {
		return ADC_STM32_LOCM3_RET_FAILED;
	}

	memset(self, 0, sizeof(AdcStm32Locm3));
	uhal_module_init(&self->module);

	iadc_init(&(self->iface));
	self->iface.vmt.context = (void *)self;
	self->iface.vmt.sample = sample;

	self->adc = adc;

	if (adc == ADC1) {
		rcc_periph_clock_enable(RCC_ADC1);
	}
	#if !defined(STM32L4)
	if (adc == ADC2) {
		rcc_periph_clock_enable(RCC_ADC2);
	}
	#endif

	#if defined(STM32L4)
		ADC_CR(adc) &= ~ADC_CR_DEEPPWD;
		RCC_CCIPR |= 3 << 28;
		adc_enable_regulator(adc);
		vTaskDelay(1);
		adc_power_on(adc);
		adc_set_sample_time_on_all_channels(adc, ADC_SMPR_SMP_6DOT5CYC);
		adc_set_resolution(adc, ADC_CFGR1_RES_12_BIT);
		adc_enable_vrefint();

		/* Hardware oversampler is available on L4. Use it to
		 * oversample the input a bit. */
		ADC_CFGR2(adc) |= ADC_CFGR2_OVSR_64x;
		ADC_CFGR2(adc) |= ADC_CFGR2_OVSS_6BITS;
		ADC_CFGR2(adc) |= ADC_CFGR2_ROVSE;
	#else
		adc_power_on(adc);
		adc_disable_scan_mode(adc);
		adc_set_sample_time_on_all_channels(adc, ADC_SMPR_SMP_112CYC);
		ADC_CCR |= ADC_CCR_TSVREFE;
	#endif
	adc_set_single_conversion_mode(adc);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("ADC initialized"));

	return ADC_STM32_LOCM3_RET_OK;
}

