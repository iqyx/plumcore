/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Generic bidirectional stream interface
 *
 * Copyright (c) 2018-2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdlib.h>


typedef enum stream_ret {
	STREAM_RET_OK = 0,
	STREAM_RET_FAILED,
	STREAM_RET_TIMEOUT,
	STREAM_RET_EOF,
} stream_ret_t;

typedef struct stream Stream;
struct stream_vmt {
	/**
	 * @brief Write data to a stream
	 *
	 * If supported, an application can write @p size number of bytes from the @p buf buffer
	 * to the provided stream. This method blocks until all bytes are written.
	 * If the @p write pointer is NULL, writing to the stream is not supported.
	 *
	 * @param self Instance of the stream to write to
	 * @param buf Data buffer containing the data
	 * @param size Size of the buffer in bytes
	 *
	 * @return STREAM_RET_OK of all data was written successfully,
	 *         STREAM_RET_EOF if no more bytes can be written (stream closed),
	 *         STREAM_RET_FAILED on error.
	 */
	stream_ret_t (*write)(Stream *self, const void *buf, size_t size);

	/**
	 * @brief Read data from a stream
	 *
	 * If supported, read up to @p size bytes to the buffer @p buf. The implementation
	 * is free to read any number of bytes up to @p size and return the actual number
	 * of bytes read in the @p read argument. If the @p read pointer is NULL, reading
	 * from the stream is not supported. The method blocks until at least one byte is
	 * available for reading.
	 *
	 * @param self Instance of the stream to read from
	 * @param buf Buffer where the data will be saved
	 * @param size Size of the buffer (= the maximum number of bytes to read)
	 * @param read Number of bytes actually read
	 *
	 * @return STREAM_RET_OK if at least one byte was read successfully,
	 *         STREAM_RET_EOF of there are no more bytes to read from the stream (closed),
	 *         STREAM_RET_FAILED on other error.
	 */
	stream_ret_t (*read)(Stream *self, void *buf, size_t size, size_t *read);

	/**
	 * @brief Write data to a stream until timeout occurs
	 *
	 * The implementation is the same as @p write method, except it waits until
	 * a timeout occurs and returns the actual number of bytes written.
	 *
	 * @param self Instance of the stream to write to
	 * @param buf Data buffer containing the data
	 * @param size Size of the buffer in bytes
	 * @param written Number of bytes written until timeout occured
	 * @param timeout_ms Time in ms to wait for data to be written
	 *
	 * @return STREAM_RET_OK of all data was written successfully,
	 *         STREAM_RET_EOF if no more bytes can be written (stream closed),
	 *         STREAM_RET_TIMEOUT if a timeout occured during write,
	 *         STREAM_RET_FAILED on error.
	 */
	stream_ret_t (*write_timeout)(Stream *self, const void *buf, size_t size, size_t *written, uint32_t timeout_ms);
	
	/**
	 * @brief Read data from a stream until timeout occurs
	 *
	 * The implementation is the same as @p read method, except it waits until
	 * a timeout occurs.
	 *
	 * @param self Instance of the stream to read from
	 * @param buf Buffer where the data will be saved
	 * @param size Size of the buffer (= the maximum number of bytes to read)
	 * @param read Number of bytes actually read
	 *
	 * @return STREAM_RET_OK if at least one byte was read successfully,
	 *         STREAM_RET_EOF of there are no more bytes to read from the stream (closed),
	 *         STREAM_RET_TIMEOUT if a timeout occured during read,
	 *         STREAM_RET_FAILED on other error.
	 */
	stream_ret_t (*read_timeout)(Stream *self, void *buf, size_t size, size_t *read, uint32_t timeout_ms);
};

typedef struct stream {
	const struct stream_vmt *vmt;
	void *parent;
} Stream;


