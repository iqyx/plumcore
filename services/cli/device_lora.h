/* SPDX-License-Identifier: BSD-2-Clause
 *
 * LoRa device configuration
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

int32_t device_lora_print(struct treecli_parser *parser, void *exec_context);
int32_t device_lora_loraN_create(struct treecli_parser *parser, uint32_t index, struct treecli_node *node, void *ctx);
int32_t device_lora_loraN_print(struct treecli_parser *parser, void *exec_context);
int32_t device_lora_loraN_export(struct treecli_parser *parser, void *exec_context);
int32_t device_lora_loraN_join(struct treecli_parser *parser, void *exec_context);
int32_t device_lora_loraN_leave(struct treecli_parser *parser, void *exec_context);
int32_t device_lora_loraN_mode_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len);
int32_t device_lora_loraN_appkey_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len);
int32_t device_lora_loraN_appeui_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len);
int32_t device_lora_loraN_deveui_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len);
int32_t device_lora_loraN_devaddr_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len);
int32_t device_lora_loraN_nwkskey_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len);
int32_t device_lora_loraN_appskey_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len);
int32_t device_lora_loraN_datarate_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len);
int32_t device_lora_loraN_adr_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len);
