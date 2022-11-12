/* SPDX-License-Identifier: BSD-2-Clause
 *
 * /device/sensor configuration subtree
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>


#define DNODE_INDEX(p, i) p->pos.levels[p->pos.depth + i].dnode_index

static int32_t device_sensor_print(struct treecli_parser *parser, void *exec_context);

