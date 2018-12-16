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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "u_assert.h"
#include "u_log.h"
#include "interface_rng.h"
#include "module_prng_simple.h"
#include "sha2.h"


/** @todo task to periodically add entropy to the prng state from an ADC input. */

static void module_prng_seed_task(void *p) {
	struct module_prng_simple *prng = (struct module_prng_simple *)p;

	while (1) {

		/** @todo get a value from the ADC and pass it to the
		 *        module_prng_simple_seed function. */
		xSemaphoreTake(prng->prng_mutex, portMAX_DELAY);
		sha512_update(&(prng->state), (const uint8_t *)"abcd", 4);
		xSemaphoreGive(prng->prng_mutex);

		vTaskDelay(100);
	}

	vTaskDelete(NULL);
}


static int32_t module_prng_simple_read(void *context, uint8_t *buf, size_t len) {
	if (u_assert(context != NULL && buf != NULL && len > 0)) {
		return INTERFACE_RNG_READ_FAILED;
	}
	struct module_prng_simple *prng = (struct module_prng_simple *)context;

	xSemaphoreTake(prng->prng_mutex, portMAX_DELAY);
	while (len > 0) {
		uint8_t res[64];
		sha512_final(&(prng->state), res);

		if (len >= 8) {
			/* We can fill full 8 byte block. */
			memcpy(buf, res, 8);
			len -= 8;
			buf += 8;
		} else {
			/* Tail if the buffer. */
			memcpy(buf, res, len);
			len -= len;
			buf += len;
		}

		/* Update the internal state with the generated hash. */
		sha512_update(&(prng->state), res, 64);
	}
	xSemaphoreGive(prng->prng_mutex);

	return INTERFACE_RNG_READ_OK;
}


int32_t module_prng_simple_init(struct module_prng_simple *prng, const char *name) {
	if (u_assert(prng != NULL && name != NULL)) {
		return MODULE_PRNG_INIT_FAILED;
	}

	memset(prng, 0, sizeof(struct module_prng_simple));

	/* Initialize the internal state. */
	sha512_init(&(prng->state));

	/* Super-pro initial seeding. */
	sha512_update(&(prng->state), (const uint8_t *)"abcd", 4);

	/** @todo for loop to seed the prng from an ADC input. */

	/* Initialize RNG interface. */
	interface_rng_init(&(prng->iface));
	prng->iface.vmt.context = (void *)prng;
	prng->iface.vmt.read = module_prng_simple_read;

	xTaskCreate(module_prng_seed_task, "prng_seed", configMINIMAL_STACK_SIZE + 256, (void *)prng, 1, &(prng->seed_task));
	prng->prng_mutex = xSemaphoreCreateMutex();

	if (prng->seed_task == NULL && prng->prng_mutex == NULL) {
		module_prng_simple_free(prng);
		return MODULE_PRNG_INIT_FAILED;
	}

	u_log(system_log, LOG_TYPE_INFO, "module PRNG-simple initialized");
	return MODULE_PRNG_INIT_OK;
}


int32_t module_prng_simple_free(struct module_prng_simple *prng) {
	if (u_assert(prng != NULL)) {
		return MODULE_PRNG_FREE_FAILED;
	}

	if (prng->seed_task != NULL) {
		vTaskDelete(prng->seed_task);
		prng->seed_task = NULL;
	}
	if (prng->prng_mutex != NULL) {
		vSemaphoreDelete(prng->prng_mutex);
		prng->prng_mutex = NULL;
	}

	return MODULE_PRNG_FREE_FAILED;
}


int32_t module_prng_simple_seed(struct module_prng_simple *prng, uint8_t *data, size_t len) {
	if (u_assert(prng != NULL && data != NULL && len > 0)) {
		return MODULE_PRNG_SIMPLE_SEED_FAILED;
	}

	/* Update the internal state with the provided bytes. */
	xSemaphoreTake(prng->prng_mutex, portMAX_DELAY);
	sha512_update(&(prng->state), data, len);
	xSemaphoreGive(prng->prng_mutex);

	return MODULE_PRNG_SIMPLE_SEED_OK;
}

