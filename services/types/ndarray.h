/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Numpy-like ndarray generic multi-dimensional array type
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#define NDARRAY_MAX_DIM 3

enum dtype_byteorder {
	DTYPE_BYTEORDER_LITTLE = 0,
	DTYPE_BYTEORDER_BIG,
};

enum dtype {
	DTYPE_TYPE_CHAR,
	DTYPE_TYPE_INT8,
	DTYPE_TYPE_UINT8,
	DTYPE_TYPE_INT16,
	DTYPE_TYPE_UINT16,
	DTYPE_TYPE_INT32,
	DTYPE_TYPE_UINT32,
	DTYPE_TYPE_INT64,
	DTYPE_TYPE_UINT64,
	DTYPE_TYPE_FLOAT,
	DTYPE_TYPE_DOUBLE,
};

struct ndarray {
	uint8_t dimensions;
	enum dtype dtype;
	enum dtype_byteorder byteorder;
	uint8_t dtype_size;
	size_t shape[NDARRAY_MAX_DIM];
	size_t array_size;
	void *buf;
	size_t buf_size;
};

