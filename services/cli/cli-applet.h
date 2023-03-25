/* SPDX-License-Identifier: BSD-2-Clause
 *
 * CLI for starting applets
 *
 * Copyright (c) 2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>


#define DNODE_INDEX(p, i) p->pos.levels[p->pos.depth + i].dnode_index

int32_t applet_print(struct treecli_parser *parser, void *exec_context);
int32_t applet_appletN_create(struct treecli_parser *parser, uint32_t index, struct treecli_node *node, void *ctx);
int32_t applet_appletN_run(struct treecli_parser *parser, void *exec_context);

