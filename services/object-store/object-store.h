/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ELF object store
 *
 * Copyright (c) 2024-2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <main.h>
#include <cbor.h>


typedef enum {
	OBJECT_STORE_RET_OK = 0,
	OBJECT_STORE_RET_FAILED,
} object_store_ret_t;

typedef struct object_store {

} ObjectStore;



object_store_ret_t object_store_init(ObjectStore *self);
object_store_ret_t object_store_free(ObjectStore *self);

