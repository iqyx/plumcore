/*
 * A generic interface descriptor for accessing the radio MAC
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
#include <time.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "interface.h"


#define IRADIO_MAC_TX_MESSAGE_LEN 256
#define IRADIO_MAC_RX_MESSAGE_LEN 256


typedef enum {
	IRADIO_MAC_RET_OK = 0,
	IRADIO_MAC_RET_FAILED,
} iradio_mac_ret_t;


struct iradio_mac_tx_message {
	uint8_t buf[IRADIO_MAC_TX_MESSAGE_LEN];
	size_t len;
	uint32_t destination;
	uint32_t context;
};


struct iradio_mac_rx_message {
	uint8_t buf[IRADIO_MAC_RX_MESSAGE_LEN];
	size_t len;
	uint32_t source;
};


struct radio_mac;
typedef struct {
	QueueHandle_t txqueue;
	struct radio_mac *first_client;
} IRadioMac;


iradio_mac_ret_t iradio_mac_init(IRadioMac *self);
iradio_mac_ret_t iradio_mac_free(IRadioMac *self);

