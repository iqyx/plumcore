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


typedef enum {
	NDARRAY_RET_OK = 0,
	NDARRAY_RET_FAILED,
} ndarray_ret_t;

enum dtype {
	DTYPE_CHAR = 0,
	DTYPE_BYTE = 1,
	DTYPE_INT8 = 2,
	DTYPE_UINT8 = 3,
	DTYPE_INT16 = 4,
	DTYPE_UINT16 = 5,
	DTYPE_INT32 = 6,
	DTYPE_UINT32 = 7,
	DTYPE_INT64 = 8,
	DTYPE_UINT64 = 9,
	DTYPE_FLOAT = 10,
	DTYPE_DOUBLE = 11,
};

typedef struct ndarray {
	enum dtype dtype;
	size_t dsize;

	size_t asize;

	void *buf;
	size_t bufsize;
} NdArray;


ndarray_ret_t ndarray_init_zero(NdArray *self, enum dtype dtype, size_t asize);
ndarray_ret_t ndarray_init_empty(NdArray *self, enum dtype dtype, size_t asize_max);
ndarray_ret_t ndarray_init_view(NdArray *self, enum dtype dtype, size_t asize, void *buf, size_t bufsize);
ndarray_ret_t ndarray_free(NdArray *self);

size_t ndarray_get_dsize(enum dtype dtype);
ndarray_ret_t ndarray_to_str(NdArray *self, char *s, size_t max);
ndarray_ret_t ndarray_value_to_str(NdArray *self, size_t i, char *s, size_t max);
ndarray_ret_t ndarray_move(NdArray *self, size_t offset_to, size_t offset_from, size_t size);
ndarray_ret_t ndarray_copy_from(NdArray *self, size_t offset_to, const NdArray *from, size_t offset_from, size_t size);
ndarray_ret_t ndarray_zero(NdArray *self);
ndarray_ret_t ndarray_sqrt(NdArray *self);
ndarray_ret_t ndarray_append(NdArray *self, const NdArray *from);
const char *ndarray_dtype_str(NdArray *self);

