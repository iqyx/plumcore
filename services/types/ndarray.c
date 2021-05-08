/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Numpy-like ndarray generic multi-dimensional array type
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "ndarray.h"


const char *dtype_str[] = {
	"CHAR",
	"BYTE",
	"INT8",
	"UINT8",
	"INT16",
	"UINT16",
	"INT32",
	"UINT32",
	"INT64",
	"UINT64",
	"FLOAT",
	"DOUBLE",
};


size_t ndarray_get_dsize(enum dtype dtype) {
	size_t dsize = 0;
	switch (dtype) {
		case DTYPE_CHAR:
		case DTYPE_BYTE:
		case DTYPE_INT8:
		case DTYPE_UINT8:
			dsize = 1;
			break;
		case DTYPE_INT16:
		case DTYPE_UINT16:
			dsize = 2;
			break;
		case DTYPE_INT32:
		case DTYPE_UINT32:
		case DTYPE_FLOAT:
			dsize = 4;
			break;
		case DTYPE_INT64:
		case DTYPE_UINT64:
		case DTYPE_DOUBLE:
			dsize = 8;
			break;
		default:
			dsize = 0;
	}
	return dsize;
}


ndarray_ret_t ndarray_init_zero(NdArray *self, enum dtype dtype, size_t asize) {
	memset(self, 0, sizeof(NdArray));
	self->dtype = dtype;
	self->dsize = ndarray_get_dsize(dtype);
	self->asize = asize;
	self->bufsize = asize * self->dsize;
	self->buf = malloc(self->bufsize);
	if (self->buf == NULL) {
		return NDARRAY_RET_FAILED;
	}
	memset(self->buf, 0, self->bufsize);
	return NDARRAY_RET_OK;
}


ndarray_ret_t ndarray_init_empty(NdArray *self, enum dtype dtype, size_t asize_max) {
	memset(self, 0, sizeof(NdArray));
	self->dtype = dtype;
	self->dsize = ndarray_get_dsize(dtype);
	self->asize = 0;
	self->bufsize = asize_max * self->dsize;
	self->buf = malloc(self->bufsize);
	if (self->buf == NULL) {
		return NDARRAY_RET_FAILED;
	}
	return NDARRAY_RET_OK;
}


ndarray_ret_t ndarray_init_view(NdArray *self, enum dtype dtype, size_t asize, void *buf, size_t bufsize) {
	memset(self, 0, sizeof(NdArray));
	self->dtype = dtype;
	self->dsize = ndarray_get_dsize(dtype);
	if ((asize * self->dsize) > bufsize) {
		return NDARRAY_RET_FAILED;
	}
	self->asize = asize;
	self->bufsize = bufsize;
	self->buf = buf;
	return NDARRAY_RET_OK;
}


ndarray_ret_t ndarray_free(NdArray *self) {
	free(self->buf);
	memset(self, 0, sizeof(NdArray));
	return NDARRAY_RET_OK;
}


ndarray_ret_t ndarray_to_str(NdArray *self, char *s, size_t max) {
	char sz[40] = {0};
	snprintf(sz, sizeof(sz) - 1, "%s[\x1b[1;34m%u\x1b[0m] @ %8p  ", dtype_str[self->dtype], self->asize, self->buf);
	strlcpy(s, sz, max);
	strlcat(s, "[\x1b[34m ", max);
	if (self->asize > 6) {
		char v[20] = {0};
		for (size_t i = 0; i < 3 && i < self->asize; i++) {
			ndarray_value_to_str(self, i, v, sizeof(v));
			strlcat(s, v, max);
			strlcat(s, " ", max);
		}
		strlcat(s, ".. ", max);
		for (size_t i = self->asize - 3; i < self->asize; i++) {
			ndarray_value_to_str(self, i, v, sizeof(v));
			strlcat(s, v, max);
			strlcat(s, " ", max);
		}
	} else {
		char v[10] = {0};
		for (size_t i = 0; i < self->asize; i++) {
			ndarray_value_to_str(self, i, v, sizeof(v));
			strlcat(s, v, max);
			strlcat(s, " ", max);
		}
	}
	strlcat(s, "\x1b[0m]", max);
	return NDARRAY_RET_OK;
}


ndarray_ret_t ndarray_value_to_str(NdArray *self, size_t i, char *s, size_t max) {
	switch (self->dtype) {
		case DTYPE_CHAR:
			snprintf(s, max - 1, "'%c'", ((char *)self->buf)[i]);
			s[max - 1] = '\0';
			return NDARRAY_RET_OK;
		// case DTYPE_INT8:
		// case DTYPE_UINT8:
		case DTYPE_INT16:
			snprintf(s, max - 1, "%d", ((int16_t *)self->buf)[i]);
			s[max - 1] = '\0';
			return NDARRAY_RET_OK;
		// case DTYPE_UINT16:
		// case DTYPE_INT32:
		// case DTYPE_UINT32:
		case DTYPE_FLOAT:
			snprintf(s, max - 1, "%.3f", ((float *)self->buf)[i]);
			s[max - 1] = '\0';
			return NDARRAY_RET_OK;
		// case DTYPE_INT64:
		// case DTYPE_UINT64:
		// case DTYPE_DOUBLE:
		default:
			strlcpy(s, "", max);
			break;
	}
	return NDARRAY_RET_FAILED;
}


ndarray_ret_t ndarray_move(NdArray *self, size_t offset_to, size_t offset_from, size_t size) {
	memmove(
		((uint8_t *)self->buf) + (offset_to * self->dsize),
		((uint8_t *)self->buf) + (offset_from * self->dsize),
		size * self->dsize
	);
	return NDARRAY_RET_OK;
}


ndarray_ret_t ndarray_copy_from(NdArray *self, size_t offset_to, const NdArray *from, size_t offset_from, size_t size) {
	if (self->dtype != from->dtype) {
		return NDARRAY_RET_FAILED;
	}
	size_t dst_max_size = self->bufsize / self->dsize;
	if (size > (dst_max_size - offset_to)) {
		size = dst_max_size - offset_to;
	}
	if (size > (from->asize - offset_from)) {
		size = from->asize - offset_from;
	}

	memcpy(
		((uint8_t *)self->buf) + (offset_to * self->dsize),
		((uint8_t *)from->buf) + (offset_from * self->dsize),
		size * self->dsize
	);
	return NDARRAY_RET_OK;
}


ndarray_ret_t ndarray_zero(NdArray *self) {
	memset(self->buf, 0, self->asize * self->dsize);
	return NDARRAY_RET_OK;
}


ndarray_ret_t ndarray_sqrt(NdArray *self) {
	if (self->dtype == DTYPE_FLOAT) {
		float *v = (float *)self->buf;
		for (size_t i = 0; i < self->asize; i++) {
			v[i] = sqrtf(v[i]);
		}
		return NDARRAY_RET_OK;
	}
	return NDARRAY_RET_FAILED;
}


ndarray_ret_t ndarray_append(NdArray *self, const NdArray *from) {
	size_t max_asize = self->bufsize / self->dsize;
	size_t size = from->asize;
	if (size > (max_asize - self->asize)) {
		size = max_asize - self->asize;
	}
	ndarray_copy_from(self, self->asize, from, 0, size);
	self->asize += size;
	return NDARRAY_RET_OK;
}


const char *ndarray_dtype_str(NdArray *self) {
	return dtype_str[self->dtype];
}
