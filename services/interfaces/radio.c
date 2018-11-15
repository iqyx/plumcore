/*
 * A generic radio interface
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

#include "interfaces/radio.h"


iradio_ret_t iradio_init(IRadio *self) {
	if (self == NULL) {
		return IRADIO_RET_NULL;
	}

	memset(self, 0, sizeof(IRadio));

	return IRADIO_RET_OK;
}


iradio_ret_t iradio_free(IRadio *self) {
	if (self == NULL) {
		return IRADIO_RET_NULL;
	}

	/* Nothing to free. */

	return IRADIO_RET_OK;
}


iradio_ret_t radio_open(Radio *self, IRadio *descriptor) {
	if (self == NULL) {
		return IRADIO_RET_NULL;
	}
	if (descriptor == NULL) {
		return IRADIO_RET_BAD_ARG;
	}

	/** @todo proper locking */
	if (descriptor->clients >= 1) {
		return IRADIO_RET_FAILED;
	}

	descriptor->clients++;
	self->descriptor = descriptor;

	return IRADIO_RET_OK;
}


iradio_ret_t radio_close(Radio *self) {
	if (self == NULL) {
		return IRADIO_RET_NULL;
	}

	if (self->descriptor == NULL) {
		/* Not opened. */
		return IRADIO_RET_FAILED;
	}

	self->descriptor->clients--;
	self->descriptor = NULL;

	return IRADIO_RET_OK;
}


iradio_ret_t radio_send(Radio *self, const uint8_t *buf, size_t len, const struct iradio_send_params *params) {
	if (self == NULL) {
		return IRADIO_RET_NULL;
	}
	if (buf == NULL || len == 0) {
		return IRADIO_RET_BAD_ARG;
	}

	if (self->descriptor->vmt.send == NULL) {
		return IRADIO_RET_NOT_IMPLEMENTED;
	}

	return self->descriptor->vmt.send(self->descriptor->vmt.context, buf, len, params);
}


iradio_ret_t radio_receive(Radio *self, uint8_t *buf, size_t len, size_t *received_len, struct iradio_receive_params *params, uint32_t timeout_us) {
	if (self == NULL) {
		return IRADIO_RET_NULL;
	}
	if (buf == NULL || len == 0) {
		return IRADIO_RET_BAD_ARG;
	}

	if (self->descriptor->vmt.receive == NULL) {
		return IRADIO_RET_NOT_IMPLEMENTED;
	}

	return self->descriptor->vmt.receive(self->descriptor->vmt.context, buf, len, received_len, params, timeout_us);
}


iradio_ret_t radio_set_tx_power(Radio *self, int32_t power_dbm) {
	if (self == NULL) {
		return IRADIO_RET_NULL;
	}

	if (self->descriptor->vmt.set_tx_power == NULL) {
		return IRADIO_RET_NOT_IMPLEMENTED;
	}

	return self->descriptor->vmt.set_tx_power(self->descriptor->vmt.context, power_dbm);
}


iradio_ret_t radio_set_frequency(Radio *self, uint64_t freq_hz) {
	if (self == NULL) {
		return IRADIO_RET_NULL;
	}

	if (self->descriptor->vmt.set_frequency == NULL) {
		return IRADIO_RET_NOT_IMPLEMENTED;
	}

	return self->descriptor->vmt.set_frequency(self->descriptor->vmt.context, freq_hz);
}


iradio_ret_t radio_set_bit_rate(Radio *self, uint32_t bit_rate_bps) {
	if (self == NULL) {
		return IRADIO_RET_NULL;
	}

	if (self->descriptor->vmt.set_bit_rate == NULL) {
		return IRADIO_RET_NOT_IMPLEMENTED;
	}

	return self->descriptor->vmt.set_bit_rate(self->descriptor->vmt.context, bit_rate_bps);
}


iradio_ret_t radio_set_sync(Radio *self, const uint8_t *bytes, size_t sync_length) {
	if (self == NULL) {
		return IRADIO_RET_NULL;
	}

	if (self->descriptor->vmt.set_sync == NULL) {
		return IRADIO_RET_NOT_IMPLEMENTED;
	}

	return self->descriptor->vmt.set_sync(self->descriptor->vmt.context, bytes, sync_length);
}
