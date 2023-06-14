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


typedef struct nbus_log {
	NbusChannel channel;

} NbusLog;


nbus_ret_t nbus_log_init(NbusLog *self, NbusChannel *parent);
nbus_ret_t nbus_log_free(NbusLog *self);
