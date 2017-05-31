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

#ifndef _INTERFACE_STREAM_H_
#define _INTERFACE_STREAM_H_

#include <stdint.h>

#include "FreeRTOS.h"
#include "semphr.h"

#include "hal_interface.h"


struct interface_stream_vmt {

	int32_t (*read)(void *context, uint8_t *buf, uint32_t len);
	int32_t (*read_timeout)(void *context, uint8_t *buf, uint32_t len, uint32_t timeout);
	int32_t (*write)(void *context, const uint8_t *buf, uint32_t len);
	void *context;
};

struct interface_stream {
	struct hal_interface_descriptor descriptor;
	struct interface_stream_vmt vmt;

	SemaphoreHandle_t lock;
};


/**
 * @brief Initialize the stream interface.
 *
 * @param stream A stream interface to initialize.
 *
 * @return INTERFACE_STREAM_INIT_OK on success or
 *         INTERFACE_STREAM_INIT_FAILED otherwise.
 */
int32_t interface_stream_init(struct interface_stream *stream);
#define INTERFACE_STREAM_INIT_OK 0
#define INTERFACE_STREAM_INIT_FAILED -1

/**
 * @brief Read from the stream interface.
 *
 * Reading of up to @a len bytes is requested. Bytes are saved to the @a buf
 * buffer. Number of bytes read is returned. Zero is returned if the stream
 * has reached its end. -1 is returned on error.
 *
 * @param stream A stream to read bytes from.
 * @param buf A buffer to save data to.
 * @param len Maximum length of the data to be read.
 *
 * @return 0 if the stream end was reached, -1 on error or number of bytes
 *         actually read and saved in the buffer.
 */
int32_t interface_stream_read(struct interface_stream *stream, uint8_t *buf, uint32_t len);
int32_t interface_stream_read_timeout(struct interface_stream *stream, uint8_t *buf, uint32_t len, uint32_t timeout);


/**
 * @brief Write to the stream interface.
 *
 * Write up to @a len bytes from the buffer @a buf to the output stream.
 * -1 is returned if there was an error writing bytes, 0 if the end of the
 * stream was reached or number of bytes actually written to the stream.
 *
 * @param stream A stream to write data to.
 * @param buf A buffer with data to write.
 * @param len Maximum length of data to write.
 *
 * @return 0 if the stream end was reached, -1 on error or number of bytes
 *         written to the stream.
 */
int32_t interface_stream_write(struct interface_stream *stream, const uint8_t *buf, uint32_t len);


#endif
