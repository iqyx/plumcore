/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Configuration of the system debug facilities
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


int32_t system_debug_swd_enabled_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len);


