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

/**
 * This is an interface for services/modules providing access to common
 * radio transceivers usually working in the sub-1GHz range.
 *
 * It is intended to be used with transceivers capable of doing
 * packet handling. As it tries to be very generic, it omits some
 * capabilities provided by some manufacturers, such as hardware CRC
 * computation, hardware error correction and such.
 *
 * A generic transceiver can be in a stop mode (the lowest power consumption),
 * idle mode (usually a XTAL oscillator is running and the module is ready
 * to transmit or receive), receive and transmit mode.
 *
 * Only few parameters are actually required to get such basic radio running:
 *   - center frequency
 *   - modulation type
 *   - output power
 *   - preamble (how does it look like, its length, etc.)
 *   - sequence of bytes used to mark the packet (sync bytes)
 *   - packet length
 *   - packet data
 *
 * Various manufacturers are using different settings. Some chips are
 * configurable more than the others. Some even need a bunch of unknown
 * register writes to get the radio working. All those things should be
 * handled by the driver service/module itself to make the common
 * functionality manufacturer/chip independent.
 *
 * Rest of the documentation is spread across this file and the client
 * header file.
 *
 * IRadio and iradio_ prefixes are used for the radio interface descriptor,
 * Radio and radio_ prefixes are reserved for the radio interface client.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "interface.h"

typedef enum {
	/** The function performed as intended with no unexpected result. */
	IRADIO_RET_OK,

	/** NULL pointer was given as an instance. The function returns before
	 *  any other actions are done. */
	IRADIO_RET_NULL,

	/** This interface allows some functions to be unimplemented. */
	IRADIO_RET_NOT_IMPLEMENTED,

	/** If a function is implemented but a wrong value is used when
	 *  calling it, a BAD_ARG is returned. */
	IRADIO_RET_BAD_ARG,

	/** If a function is implemented, all arguments are valid but the
	 *  radio rejects it at the current state, a BAD_STATE is returned. */
	IRADIO_RET_BAD_STATE,

	/** In all other fail cases a FAILED is returned. */
	IRADIO_RET_FAILED,

	/** If a timeout occurs and a non-zero timeout is specified when calling
	 *  a function, a TIMEOUT is returned. */
	IRADIO_RET_TIMEOUT,
} iradio_ret_t;


enum iradio_modulation_type {
	IRADIO_MODULATION_NONE = 0,
	IRADIO_MODULATION_FSK,
};


enum iradio_modulation_shaping {
	IRADIO_MODULATION_SHAPING_NONE = 0,
	IRADIO_MODULATION_SHAPING_0_3,
	IRADIO_MODULATION_SHAPING_0_5,
};


struct iradio_modulation_params_fsk {
	uint32_t fdev;
	enum iradio_modulation_shaping shaping;
};


union iradio_modulation_params {
	struct iradio_modulation_params_fsk fsk;
};


struct iradio_modulation {
	enum iradio_modulation_type type;
	union iradio_modulation_params params;
};


struct iradio_send_params {
	uint32_t placeholder;
};


struct iradio_receive_params {
	int32_t rssi_dbm;
	int32_t freq_error_hz;
};


/**
 * Interface descriptor
 */

struct iradio_vmt {
	iradio_ret_t (*set_stop_mode)(void *context);
	iradio_ret_t (*set_frequency)(void *context, uint64_t freq_hz);
	iradio_ret_t (*set_modulation)(void *context);
	iradio_ret_t (*set_bit_rate)(void *context, uint32_t bit_rate_bps);
	iradio_ret_t (*set_tx_power)(void *context, int32_t power_dbm);
	iradio_ret_t (*set_preamble)(void *context, uint32_t length_bits);
	iradio_ret_t (*set_sync)(void *context, const uint8_t *bytes, size_t sync_length);
	iradio_ret_t (*send)(void *context, const uint8_t *buf, size_t len, const struct iradio_send_params *params);
	iradio_ret_t (*receive)(void *context, uint8_t *buf, size_t buf_len, size_t *received_len, struct iradio_receive_params *params, uint32_t timeout_us);
	void *context;
};


typedef struct {
	Interface interface;
	struct iradio_vmt vmt;

	/** Number of connected clients. It can be 0 or 1. */
	uint32_t clients;
} IRadio;


iradio_ret_t iradio_init(IRadio *self);
iradio_ret_t iradio_free(IRadio *self);


/**
 * Interface client
 */

typedef struct {
	IRadio *descriptor;
} Radio;


/**
 * @brief Open the radio instance
 *
 * Initializes the radio client instance and connects it to the supplied
 * IRadio descriptor. Only a single connection at a time is allowed.
 *
 * @param self A preallocated Radio instance to initialize (not NULL)
 * @param descriptor IRadio descriptor to connect to
 */
iradio_ret_t radio_open(Radio *self, IRadio *descriptor);

/**
 * @brief Disconnect the radio instance
 *
 * The client radio instance is disconnected from the IRadio descriptor.
 * Allocated resources are not released, use radio_free for this purpose.
 */
iradio_ret_t radio_close(Radio *self);
iradio_ret_t radio_set_stop_mode(Radio *self, bool stop_mode);

/**
 * @brief Set frequency of the radio transceiver
 *
 * Sets operating frequency of the radio transceiver. If the transceiver
 * requires special handling of different bands, channels, filters, etc.,
 * it is the responsibility of the driver service to do that.
 * The interface requires only a frequency value in Hertz to be set.
 */
iradio_ret_t radio_set_frequency(Radio *self, uint64_t freq_hz);
iradio_ret_t radio_set_modulation(Radio *self, struct iradio_modulation modulation);
iradio_ret_t radio_set_bit_rate(Radio *self, uint32_t bit_rate_bps);
iradio_ret_t radio_set_tx_power(Radio *self, int32_t power_dbm);
iradio_ret_t radio_set_preamble(Radio *self, uint32_t length_bits);
iradio_ret_t radio_set_sync(Radio *self, const uint8_t *bytes, size_t sync_length);
iradio_ret_t radio_send(Radio *self, const uint8_t *buf, size_t len, const struct iradio_send_params *params);
iradio_ret_t radio_receive(Radio *self, uint8_t *buf, size_t len, size_t *received_len, struct iradio_receive_params *params, uint32_t timeout_us);


