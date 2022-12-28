/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Generic ADC interface
 *
 * Copyright (c) 2022, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

typedef enum {
	ADC_RET_OK,
	ADC_RET_FAILED,
} adc_ret_t;

typedef struct adc Adc;
typedef uint32_t adc_channel_t;
typedef int32_t adc_sample_t;

struct adc_vmt {
	adc_ret_t (*convert_single)(Adc *self, adc_channel_t input, adc_sample_t *sample);
};

typedef struct adc {
	const struct adc_vmt *vmt;
	void *parent;
} Adc;

