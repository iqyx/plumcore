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

#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "interface.h"
#include "interfaces/radio-mac/descriptor.h"


iradio_mac_ret_t iradio_mac_init(IRadioMac *self) {
	if (self == NULL) {
		return IRADIO_MAC_RET_FAILED;
	}

	memset(self, 0, sizeof(IRadioMac));

	self->txqueue = xQueueCreate(5, sizeof(struct iradio_mac_tx_message));
	if(self->txqueue == NULL) {
		return IRADIO_MAC_RET_FAILED;
	}

	return IRADIO_MAC_RET_OK;
}


iradio_mac_ret_t iradio_mac_free(IRadioMac *self) {
	if (self == NULL) {
		return IRADIO_MAC_RET_FAILED;
	}

	if (self->txqueue != NULL) {
		vQueueDelete(self->txqueue);
	}

	memset(self, 0, sizeof(IRadioMac));

	return IRADIO_MAC_RET_OK;
}

