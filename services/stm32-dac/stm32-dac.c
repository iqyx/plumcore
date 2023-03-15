/* SPDX-License-Identifier: BSD-2-Clause
 *
 * STM32 DAC device
 *
 * Copyright (c) 2022, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <main.h>
#include <libopencm3/stm32/dac.h>

#include "stm32-dac.h"

#define MODULE_NAME "stm32-dac"


/***************************************************************************************************
 * DAC interface API
 ***************************************************************************************************/

static dac_ret_t set_single(Dac *self, dac_sample_t sample) {
	Stm32Dac *dac = (Stm32Dac *)self->parent;

	/* STM32 DAC configured as 12 bit, valid values are 0-4095 */
	uint16_t v = (uint16_t)(sample * 4095.0f);
	dac_load_data_buffer_single(dac->locm3_dac, v, DAC_ALIGN_RIGHT12, dac->locm3_dac_channel);

	return DAC_RET_OK;
}


static const struct dac_vmt dac_iface_vmt = {
	.set_single = set_single,
};


stm32_dac_ret_t stm32_dac_init(Stm32Dac *self, uint32_t dac, uint32_t channel) {
	memset(self, 0, sizeof(Stm32Dac));
	self->locm3_dac = dac;
	self->locm3_dac_channel = channel;

	self->dac_iface.parent = self;
	self->dac_iface.vmt = &dac_iface_vmt;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("enabling %p, channel %lu"), self->locm3_dac, self->locm3_dac_channel);
	dac_enable(self->locm3_dac, self->locm3_dac_channel);
	dac_load_data_buffer_single(self->locm3_dac, 0, DAC_ALIGN_RIGHT12, self->locm3_dac_channel);

	return STM32_DAC_RET_OK;
}


stm32_dac_ret_t stm32_dac_free(Stm32Dac *self) {
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("disabling %p, channel %lu"), self->locm3_dac, self->locm3_dac_channel);
	dac_disable(self->locm3_dac, self->locm3_dac_channel);
	return STM32_DAC_RET_OK;
}


