/* SPDX-License-Identifier: BSD-2-Clause
 *
 * LoRaWAN modem generic interface
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "interface.h"

typedef enum {
	LORA_RET_OK = 0,
	LORA_RET_FAILED,
	LORA_RET_BAD_PARAM,
	LORA_RET_INVALID_PARAM,
	LORA_RET_ACCEPTED,
	LORA_RET_DENIED,
	LORA_RET_NOT_JOINED,
	LORA_RET_NO_FREE_CHANNEL,
	LORA_RET_FRAME_COUNTER_ERROR,
	LORA_RET_BUSY,
	LORA_RET_PAUSED,
	LORA_RET_INVALID_DATA_LEN,
	LORA_RET_TIMEOUT,
} lora_ret_t;


typedef struct {
	lora_ret_t (*set_appkey)(void *parent, const char *appkey);
	lora_ret_t (*set_appeui)(void *parent, const char *appeui);
	lora_ret_t (*set_deveui)(void *parent, const char *deveui);
	lora_ret_t (*get_deveui)(void *parent, char *deveui);
	lora_ret_t (*set_devaddr)(void *parent, const uint8_t devaddr[4]);
	lora_ret_t (*set_nwkskey)(void *parent, const char *nwkskey);
	lora_ret_t (*set_appskey)(void *parent, const char *appskey);
	lora_ret_t (*join_abp)(void *parent);
	lora_ret_t (*join_otaa)(void *parent);
	lora_ret_t (*leave)(void *parent);
	lora_ret_t (*set_datarate)(void *parent, uint8_t datarate);
	lora_ret_t (*set_adr)(void *parent, bool adr);
	lora_ret_t (*send)(void *parent, uint8_t port, const uint8_t *data, size_t len);
	lora_ret_t (*receive)(void *parent, uint8_t port, uint8_t *data, size_t size, size_t *len, uint32_t timeout);
} lora_vmt_t;


typedef struct {
	const lora_vmt_t *vmt;
	void *parent;
} LoRa;


lora_ret_t lora_init(LoRa *self, const lora_vmt_t *vmt);
lora_ret_t lora_free(LoRa *self);


