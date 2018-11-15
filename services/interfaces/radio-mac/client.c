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


#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "interface.h"
#include "interfaces/radio-mac/descriptor.h"
#include "interfaces/radio-mac/client.h"


radio_mac_ret_t radio_mac_open(RadioMac *self, IRadioMac *iradio_mac, uint32_t context) {
	if (self == NULL) {
		return RADIO_MAC_RET_NULL;
	}
	if (iradio_mac == NULL) {
		return RADIO_MAC_RET_BAD_ARG;
	}

	memset(self, 0, sizeof(RadioMac));

	self->next = iradio_mac->first_client;
	iradio_mac->first_client = self;

	self->txqueue = iradio_mac->txqueue;
	self->rxqueue = xQueueCreate(5, sizeof(struct iradio_mac_tx_message));
	if(self->rxqueue == NULL) {
		return RADIO_MAC_RET_FAILED;
	}

	self->context = context;
	/* Reference to the interface descriptor. It is set at the very end to
	 * indicate that the instance is properly connected to the descriptor.*/
	self->iradio_mac = iradio_mac;


	return RADIO_MAC_RET_OK;
}


radio_mac_ret_t radio_mac_close(RadioMac *self) {
	if (self == NULL) {
		return RADIO_MAC_RET_NULL;
	}
	if (self->iradio_mac == NULL) {
		return RADIO_MAC_RET_NOT_OPENED;
	}

	/* Remove this client from the linked list. */
	RadioMac *i = self->iradio_mac->first_client;
	if (i == self) {
		self->iradio_mac->first_client = NULL;
		i = NULL;
	}
	while (i != NULL) {
		if (i->next == self) {
			i->next = self->next;
			break;
		}
		i = i->next;
	}

	if (self->rxqueue != NULL) {
		vQueueDelete(self->rxqueue);
	}

	/* Clear reference to the interface descriptor. The instance is no longer
	 * correctly connected to the descriptor. */
	self->iradio_mac = NULL;

	return RADIO_MAC_RET_FAILED;
}


radio_mac_ret_t radio_mac_send(RadioMac *self, uint32_t destination, const uint8_t *buf, size_t len) {
	if (self == NULL) {
		return RADIO_MAC_RET_NULL;
	}
	if (self->iradio_mac == NULL) {
		return RADIO_MAC_RET_NOT_OPENED;
	}

	if (len > IRADIO_MAC_TX_MESSAGE_LEN) {
		return RADIO_MAC_RET_FAILED;
	}

	/* Temporarily allocate on the stack. */
	struct iradio_mac_tx_message msg;
	memcpy(msg.buf, buf, len);
	msg.len = len;
	msg.destination = destination;
	msg.context = self->context;

	if (xQueueSend(self->txqueue, &msg, portMAX_DELAY) != pdTRUE) {
		return RADIO_MAC_RET_FAILED;
	}

	return RADIO_MAC_RET_OK;
}


radio_mac_ret_t radio_mac_receive(RadioMac *self, uint32_t *source, uint8_t *buf, size_t buf_size, size_t *len) {
	if (self == NULL) {
		return RADIO_MAC_RET_NULL;
	}
	if (self->iradio_mac == NULL) {
		return RADIO_MAC_RET_NOT_OPENED;
	}

	struct iradio_mac_rx_message msg;
	if (xQueueReceive(self->rxqueue, &msg, portMAX_DELAY) != pdTRUE) {
		return RADIO_MAC_RET_FAILED;
	}

	if (msg.len > buf_size) {
		return RADIO_MAC_RET_FAILED;
	}
	memcpy(buf, msg.buf, msg.len);
	*len = msg.len;
	*source = msg.source;

	return RADIO_MAC_RET_OK;
}

