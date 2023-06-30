/* SPDX-License-Identifier: BSD-2-Clause
 *
 * CLI /system/identity subtree
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
#define IDENTITY_HOSTNAME_LEN 32

extern char identity_device_name[IDENTITY_HOSTNAME_LEN];
extern char identity_serial_number[IDENTITY_HOSTNAME_LEN];

int32_t identity_export(struct treecli_parser *parser, void *exec_context);
int32_t identity_print(struct treecli_parser *parser, void *exec_context);
int32_t identity_generic_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len);

