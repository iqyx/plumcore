/*
 * uMeshFw HAL ADC interface
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

#include "u_assert.h"
#include "interface.h"
#include "adc.h"


interface_adc_ret_t interface_adc_init(IAdc *self) {
	if (u_assert(self != NULL)) {
		return INTERFACE_ADC_RET_FAILED;
	}

	memset(self, 0, sizeof(IAdc));
	uhal_interface_init(&self->interface);

	return INTERFACE_ADC_RET_OK;
}


interface_adc_ret_t interface_adc_sample(IAdc *self, uint32_t input, int32_t *sample) {
	if (u_assert(self != NULL)) {
		return INTERFACE_ADC_RET_FAILED;
	}
	if (self->vmt.sample != NULL) {
		return self->vmt.sample(self->vmt.context, input, sample);
	}
	return INTERFACE_ADC_RET_FAILED;
}

