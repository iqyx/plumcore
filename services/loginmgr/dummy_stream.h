/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Null-sink stream — write-to-nowhere, read-returns-nothing
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <interfaces/stream.h>


typedef enum {
	DUMMY_STREAM_RET_OK = 0,
	DUMMY_STREAM_RET_FAILED,
} dummy_stream_ret_t;

typedef struct {
	Stream iface;
} DummyStream;


dummy_stream_ret_t dummy_stream_init(DummyStream *self);
dummy_stream_ret_t dummy_stream_free(DummyStream *self);
