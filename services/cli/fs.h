/* SPDX-License-Identifier: BSD-2-Clause
 *
 * FIFO in a flash device
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

int32_t fs_print(struct treecli_parser *parser, void *exec_context);
int32_t files_fsN_create(struct treecli_parser *parser, uint32_t index, struct treecli_node *node, void *ctx);
int32_t files_fsN_print(struct treecli_parser *parser, void *exec_context);
int32_t files_fsN_sha256sum(struct treecli_parser *parser, void *exec_context);
int32_t files_fsN_fname_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len);
int32_t files_fsN_format_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len);
int32_t files_fsN_cat(struct treecli_parser *parser, void *exec_context);
int32_t files_fsN_put(struct treecli_parser *parser, void *exec_context);
int32_t files_fsN_remove(struct treecli_parser *parser, void *exec_context);

