/* SPDX-License-Identifier: BSD-2-Clause
 *
 * nwDAQ bus implementation, peripheral-side only
 *
 * Copyright (c) 2022, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "nbus.h"


typedef struct nbus_root {
	Nbus *nbus;
	NbusChannel channel;
} NbusRoot;


nbus_ret_t nbus_root_init(NbusRoot *self, Nbus *nbus, const void *anything, size_t len);
