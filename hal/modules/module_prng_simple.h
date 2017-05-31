/**
 * uMeshFw simple pseudorandom number generator
 *
 * Copyright (C) 2015, Marek Koza, qyx@krtko.org
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

#ifndef _MODULE_PRNG_SIMPLE_H_
#define _MODULE_PRNG_SIMPLE_H_

#include <stdint.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "hal_module.h"
#include "interface_rng.h"
#include "sha2.h"


struct module_prng_simple {

	struct hal_module_descriptor module;
	struct interface_rng iface;

	/**
	 * Task continuously adding more entropy from the ADC input.
	 */
	TaskHandle_t seed_task;
	SemaphoreHandle_t prng_mutex;

	/**
	 * Internal PRNG state is represented by SHA512 state.
	 */
	sha512_ctx state;

};


int32_t module_prng_simple_init(struct module_prng_simple *prng, const char *name);
#define MODULE_PRNG_INIT_OK 0
#define MODULE_PRNG_INIT_FAILED -1

int32_t module_prng_simple_free(struct module_prng_simple *prng);
#define MODULE_PRNG_FREE_OK 0
#define MODULE_PRNG_FREE_FAILED -1

int32_t module_prng_simple_seed(struct module_prng_simple *prng, uint8_t *data, size_t len);
#define MODULE_PRNG_SIMPLE_SEED_OK 0
#define MODULE_PRNG_SIMPLE_SEED_FAILED -1

#endif

