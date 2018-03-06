/*
 * Copyright (c) 2017, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>


#define DNODE_INDEX(p, i) p->pos.levels[p->pos.depth + i].dnode_index


extern const struct cli_table_cell device_can_sensor_table[];


int32_t device_can_sensor_print(struct treecli_parser *parser, void *exec_context);
int32_t device_can_sensor_export(struct treecli_parser *parser, void *exec_context);
int32_t device_can_sensor_add(struct treecli_parser *parser, void *exec_context);
int32_t device_can_sensor_can_sensorN_create(struct treecli_parser *parser, uint32_t index, struct treecli_node *node, void *ctx);
int32_t device_can_sensor_can_sensorN_sniff(struct treecli_parser *parser, void *exec_context);
int32_t device_can_sensor_can_sensorN_enabled_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len);
int32_t device_can_sensor_can_sensorN_can_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len);
int32_t device_can_sensor_can_sensorN_id_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len);
int32_t device_can_sensor_can_sensorN_name_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len);
int32_t device_can_sensor_can_sensorN_quantity_name_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len);
int32_t device_can_sensor_can_sensorN_quantity_symbol_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len);
int32_t device_can_sensor_can_sensorN_unit_name_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len);
int32_t device_can_sensor_can_sensorN_unit_symbol_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len);


