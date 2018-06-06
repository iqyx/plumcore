/*
 * CAN interface client
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

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "can.h"


#define CCAN_RX_QUEUE_SIZE 4

typedef enum {
	CCAN_RET_OK = 0,
	CCAN_RET_FAILED,
	CCAN_RET_NULL,
	CCAN_RET_BAD_ARG,
	CCAN_RET_NOT_OPENED,
	CCAN_RET_EMPTY,
} ccan_ret_t;


typedef struct ccan {
	/* Not null if correctly opened. */
	ICan *ican;
	QueueHandle_t rxqueue;

	struct ccan *next;
} CCan;


ccan_ret_t ccan_open(CCan *self, ICan *ican);
ccan_ret_t ccan_add_filter(CCan *self, ICanFilter filter);
ccan_ret_t ccan_receive(CCan *self, uint8_t *buf, size_t *len, uint32_t *id, uint32_t timeout);
ccan_ret_t ccan_send(CCan *self, const uint8_t *buf, size_t len, uint32_t id);
ccan_ret_t ccan_close(CCan *self);




