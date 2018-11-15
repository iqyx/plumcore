/*
 * A generic interface client for accessing the radio MAC
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

/**
 * A MAC is a service which handles all the low level stuff to make framed
 * data going through the radio interface. It is responsible for the timing
 * of sending and reception and for basic addressing of neighbors.
 *
 * The MAC may be very simple - even the basic precautions to prevent collisions
 * are omitted and the collisions are simply accepted. Or a simple LBT (listen-
 * before-talk) can be used. More complex time-slot based solutions are also
 * possible.
 *
 * In either way, the RadioMac exposes a simple interface which allows other
 * services to send and receive data to and from direct neighbors.
 * The exact service is addressed by an address of 1-n bytes long which is
 * (randomly) allocated when the service starts. There is an optional flag which
 * indicates that all running RadioMac services within the radio range will
 * receive the packet (broadcast flag).
 * The client of the service interface is addressed by a context identifier (which
 * is basically a protocol number known from TCP/IP) which is 1-n bytes long.
 *
 * As the configuration of the mac itself can be complicated, it is not handled
 * by the generic RadioMac interface.
 *
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "interface.h"
#include "interfaces/radio-mac/descriptor.h"


typedef enum {
	RADIO_MAC_RET_OK = 0,
	RADIO_MAC_RET_FAILED,
	RADIO_MAC_RET_NULL,
	RADIO_MAC_RET_BAD_ARG,
	RADIO_MAC_RET_NOT_OPENED,
} radio_mac_ret_t;


typedef struct radio_mac {
	IRadioMac *iradio_mac;
	QueueHandle_t rxqueue;
	QueueHandle_t txqueue;
	uint32_t context;

	struct radio_mac *next;
} RadioMac;


radio_mac_ret_t radio_mac_open(RadioMac *self, IRadioMac *iradio_mac, uint32_t context);
radio_mac_ret_t radio_mac_close(RadioMac *self);

/**
 * @brief Send packet to the RadioMac interface
 *
 * The function may block until the packet is successfully enqueued for transmission.
 *
 * @param destination Address of the neighbor to send the packet to. The destination
 *                    address has to be locally unique (either preallocated or randomly
 *                    chosen). The MAC may encode the address in different ways to
 *                    make its overall length shorter. The address is temporary and
 *                    may change over time.
 *                    Set to 0 if broadcasting is required.
 * @param buf Data buffer to send
 * @param len Length of the data buffer
 */
radio_mac_ret_t radio_mac_send(RadioMac *self, uint32_t destination, const uint8_t *buf, size_t len);


radio_mac_ret_t radio_mac_receive(RadioMac *self, uint32_t *source, uint8_t *buf, size_t buf_size, size_t *len);
