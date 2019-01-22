/*
 * A generic interface host for accessing the radio MAC
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

#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "interface.h"
#include "host.h"
#include "client.h"
#include "interfaces/radio-mac/descriptor.h"


hradio_mac_ret_t hradio_mac_connect(HRadioMac *self, IRadioMac *iradio_mac) {
	if (self == NULL) {
		return HRADIO_MAC_RET_NULL;
	}
	if (iradio_mac == NULL) {
		return HRADIO_MAC_RET_BAD_ARG;
	}

	memset(self, 0, sizeof(HRadioMac));
	self->iradio_mac = iradio_mac;

	return HRADIO_MAC_RET_OK;
}


hradio_mac_ret_t hradio_mac_disconnect(HRadioMac *self) {
	if (self == NULL) {
		return HRADIO_MAC_RET_NULL;
	}

	if (self->iradio_mac == NULL) {
		return HRADIO_MAC_RET_FAILED;
	}

	memset(self, 0, sizeof(HRadioMac));
	return HRADIO_MAC_RET_OK;
}


hradio_mac_ret_t hradio_mac_get_packet_to_send(HRadioMac *self, struct iradio_mac_tx_message *msg) {
	if (self == NULL) {
		return HRADIO_MAC_RET_NULL;
	}
	if (msg == NULL) {
		return HRADIO_MAC_RET_BAD_ARG;
	}

	if (xQueueReceive(self->iradio_mac->txqueue, msg, portMAX_DELAY) != pdTRUE) {
		return HRADIO_MAC_RET_FAILED;
	}

	return HRADIO_MAC_RET_OK;
}


hradio_mac_ret_t hradio_mac_put_received_packet(HRadioMac *self, const struct iradio_mac_rx_message *msg, uint32_t context) {
	if (self == NULL) {
		return HRADIO_MAC_RET_NULL;
	}
	if (msg == NULL) {
		return HRADIO_MAC_RET_BAD_ARG;
	}

	/* Find the client. */
	/* Remove this client from the linked list. */
	RadioMac *client = self->iradio_mac->first_client;
	while (client != NULL) {
		if (client->context == context) {
			/* Do not block the MAC! */
			if (xQueueSend(client->rxqueue, msg, 0) != pdTRUE) {
				return RADIO_MAC_RET_FAILED;
			}
		}
		client = client->next;
	}
	return RADIO_MAC_RET_OK;
}

