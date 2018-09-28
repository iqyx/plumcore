/*
 * CAN interface host
 *
 * Copyright (c) 2018, Marek Koza (qyx@krtko.org)
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
#include <stdbool.h>

#include "can.h"


typedef enum {
	HCAN_RET_OK = 0,
	HCAN_RET_FAILED,
	HCAN_RET_NULL,
	HCAN_RET_BAD_ARG,
	HCAN_RET_NOT_CONNECTED,
} hcan_ret_t;


typedef struct hcan {
	ICan *ican;

	hcan_ret_t (*send)(void *context, const uint8_t *buf, size_t len, uint32_t id);
	void *send_context;

} HCan;


hcan_ret_t hcan_init(HCan *self);
hcan_ret_t hcan_free(HCan *self);
hcan_ret_t hcan_connect(HCan *self, ICan *ican);
hcan_ret_t hcan_disconnect(HCan *self);
hcan_ret_t hcan_send(HCan *self, const uint8_t *buf, size_t len, uint32_t id);
hcan_ret_t hcan_set_send_callback(HCan *self, hcan_ret_t (*send)(void *context, const uint8_t *buf, size_t len, uint32_t id), void *send_context);
hcan_ret_t hcan_received(HCan *self, const uint8_t *buf, size_t len, uint32_t id);

