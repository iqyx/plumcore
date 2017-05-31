/**
 * uMeshFw HAL random number generator interface
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

#ifndef _INTERFACE_RNG_H_
#define _INTERFACE_RNG_H_

#include <stdint.h>
#include "hal_interface.h"


struct interface_rng_vmt {
	int32_t (*read)(void *context, uint8_t *buf, size_t len);
	void *context;
};

struct interface_rng {
	struct hal_interface_descriptor descriptor;
	struct interface_rng_vmt vmt;
};


/**
 * @brief Initialize the random number generator interface
 *
 * @param rng The RNG interface to initialize.
 *
 * @return INTERFACE_RNG_INIT_OK on success or
 *         INTERFACE_RNG_INIT_FAILED otherwise.
 */
int32_t interface_rng_init(struct interface_rng *rng);
#define INTERFACE_RNG_INIT_OK 0
#define INTERFACE_RNG_INIT_FAILED -1

/**
 * @brief Read bytes from the random number generator
 *
 * The function reads @p len bytes from the (pseudo)random number generator
 * even if not enough entropy is available and saves them to the @p buf buffer.
 *
 * @param rng The RNG interface to use.
 * @param buf A buffer where the data will be saved.
 * @param len The length of data to be retrieved.
 *
 * @return INTERFACE_RNG_READ_OK on success or
 *         INTERFACE_RNG_READ_FAILED otherwise.
 */
int32_t interface_rng_read(struct interface_rng *rng, uint8_t *buf, size_t len);
#define INTERFACE_RNG_READ_OK 0
#define INTERFACE_RNG_READ_FAILED -1

#endif

