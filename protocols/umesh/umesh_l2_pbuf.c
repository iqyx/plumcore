/**
 * uMeshFw uMesh layer 2 packet buffer implementation
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
#include <malloc.h>

#include "u_assert.h"
#include "u_log.h"
#include "umesh_l2_pbuf.h"


int32_t umesh_l2_pbuf_init(struct umesh_l2_pbuf *pbuf) {
	if (u_assert(pbuf != NULL)) {
		return UMESH_L2_PBUF_INIT_FAILED;
	}

	pbuf->data = (uint8_t *)malloc(UMESH_L2_PACKET_SIZE);
	pbuf->size = UMESH_L2_PACKET_SIZE;

	if (pbuf->data == NULL) {
		return UMESH_L2_PBUF_INIT_FAILED;
	}

	umesh_l2_pbuf_clear(pbuf);

	return UMESH_L2_PBUF_INIT_OK;
}


int32_t umesh_l2_pbuf_clear(struct umesh_l2_pbuf *pbuf) {
	if (u_assert(pbuf != NULL)) {
		return UMESH_L2_PBUF_CLEAR_FAILED;
	}

	/* Temporarily save the pointer to allocated data. */
	uint8_t *tmp = pbuf->data;
	size_t size = pbuf->size;

	/* Clear the structure and restore the pointer. */
	memset(pbuf, 0, sizeof(struct umesh_l2_pbuf));
	pbuf->data = tmp;
	pbuf->size = size;

	/* Clear the actual packet data. */
	memset(pbuf->data, 0, pbuf->size);

	return UMESH_L2_PBUF_CLEAR_OK;
}


int32_t umesh_l2_pbuf_free(struct umesh_l2_pbuf *pbuf) {
	if (u_assert(pbuf != NULL)) {
		return UMESH_L2_PBUF_FREE_FAILED;
	}

	free(pbuf->data);

	return UMESH_L2_PBUF_FREE_OK;
}
