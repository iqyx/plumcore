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
	LORA_RET_MODE,
} lora_ret_t;

enum lora_mode {
	LORA_MODE_NONE = 0,
	LORA_MODE_OTAA,
	LORA_MODE_ABP,
};

typedef struct lora LoRa;
typedef struct {
	lora_ret_t (*set_mode)(LoRa *self, enum lora_mode mode);
	lora_ret_t (*get_mode)(LoRa *self, enum lora_mode *mode);

	lora_ret_t (*set_appkey)(LoRa *self, const char *appkey);
	lora_ret_t (*get_appkey)(LoRa *self, char *appkey, size_t size);

	lora_ret_t (*set_appeui)(LoRa *self, const char *appeui);
	lora_ret_t (*get_appeui)(LoRa *self, char *appeui, size_t size);

	lora_ret_t (*set_deveui)(LoRa *self, const char *deveui);
	lora_ret_t (*get_deveui)(LoRa *self, char *deveui);
	lora_ret_t (*set_devaddr)(LoRa *self, const uint8_t devaddr[4]);
	lora_ret_t (*set_nwkskey)(LoRa *self, const char *nwkskey);
	lora_ret_t (*set_appskey)(LoRa *self, const char *appskey);
	lora_ret_t (*join)(LoRa *self);
	lora_ret_t (*leave)(LoRa *self);

	lora_ret_t (*set_datarate)(LoRa *self, uint8_t datarate);
	lora_ret_t (*get_datarate)(LoRa *self, uint8_t *datarate);

	lora_ret_t (*set_adr)(LoRa *self, bool adr);
	lora_ret_t (*send)(LoRa *self, uint8_t port, const uint8_t *data, size_t len);
	lora_ret_t (*receive)(LoRa *self, uint8_t port, uint8_t *data, size_t size, size_t *len, uint32_t timeout);
} lora_vmt_t;


typedef struct lora {
	const lora_vmt_t *vmt;
	void *parent;
} LoRa;


lora_ret_t lora_init(LoRa *self, const lora_vmt_t *vmt);
lora_ret_t lora_free(LoRa *self);


