/**
 * uMeshFw HAL stream interface
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
#include "semphr.h"

#include "u_assert.h"
#include "hal_interface.h"
#include "interface_stream.h"


int32_t interface_stream_init(struct interface_stream *stream) {
	if (u_assert(stream != NULL)) {
		return INTERFACE_STREAM_INIT_FAILED;
	}

	memset(stream, 0, sizeof(struct interface_stream));
	hal_interface_init(&(stream->descriptor), HAL_INTERFACE_TYPE_STREAM);

	stream->lock = xSemaphoreCreateMutex();
	if (stream->lock == NULL) {
		return INTERFACE_STREAM_INIT_FAILED;
	}

	return INTERFACE_STREAM_INIT_OK;
}


int32_t interface_stream_read(struct interface_stream *stream, uint8_t *buf, uint32_t len) {
	if (u_assert(stream != NULL)) {
		return -1;
	}
	if (stream->vmt.read != NULL) {
		return stream->vmt.read(stream->vmt.context, buf, len);
	}
	return -1;
}


int32_t interface_stream_read_timeout(struct interface_stream *stream, uint8_t *buf, uint32_t len, uint32_t timeout) {
	if (u_assert(stream != NULL)) {
		return -1;
	}
	if (stream->vmt.read_timeout != NULL) {
		return stream->vmt.read_timeout(stream->vmt.context, buf, len, timeout);
	}
	return -1;
}


int32_t interface_stream_write(struct interface_stream *stream, const uint8_t *buf, uint32_t len) {
	if (u_assert(stream != NULL)) {
		return -1;
	}
	if (stream->vmt.write != NULL) {
		xSemaphoreTake(stream->lock, portMAX_DELAY);
		int32_t ret = stream->vmt.write(stream->vmt.context, buf, len);
		xSemaphoreGive(stream->lock);
		return ret;
	}
	return -1;
}



