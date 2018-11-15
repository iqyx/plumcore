/*
 * A simple (CSMA) radio MAC
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
 * The radio-mac-simple service provides a method to access a generic
 * radio interface and allows other services to send and receive packets
 * of data.
 *
 * It periodically wakes up the radio and listens for incoming packets for
 * a specified time. When the listening period ends, a transmit queue is
 * checked and a number of packets is transmitted if there are any.
 *
 * No other checks or rx/tx scheduling is done.
 *
 * Features/misfeatures:
 *   - neighbor identifiers are 32bits long transmitted as a variable-length
 *     unsigned integer
 *   - the MAC must be configured to use a single MCS (message coding scheme).
 *     To overcome this limitation, a translating node with two radios/MACs
 *     must be used.
 *   - the MCS defines modulation, bit rate, FEC and DC-free encoding -
 *   - the MCS also defines channelization for a given modulation and a given
 *     frequency band (eg. 433MHz, 2.4GHz)
 *     required parameters to properly decode the message
 *   - there is no crypto at the moment
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "u_assert.h"

#include "interface.h"
#include "interfaces/radio.h"
#include "interfaces/radio-mac/descriptor.h"
#include "interfaces/radio-mac/host.h"
#include "mcs.h"


#define MAC_SIMPLE_MAC_KEY_LEN 32
#define MAC_SIMPLE_NETWORK_ID_SIZE 4
#define CHECK(c, r) if (!(c)) return r
#define ASSERT(c, r) if (u_assert(c)) return r
#define MAC_SIMPLE_BANDS_PER_MCS 4

typedef enum {
	MAC_SIMPLE_RET_OK = 0,
	MAC_SIMPLE_RET_NULL,
	MAC_SIMPLE_RET_BAD_ARG,
	MAC_SIMPLE_RET_FAILED,
	MAC_SIMPLE_RET_TIMEOUT,
} mac_simple_ret_t;


enum mac_simple_state {
	MAC_SIMPLE_STATE_UNINITIALIZED = 0,
	MAC_SIMPLE_STATE_STOPPED,
	MAC_SIMPLE_STATE_RUNNING,
	MAC_SIMPLE_STATE_STOP_REQ,
};


enum mac_simple_mcs_index {
	MAC_SIMPLE_MCS_NONE = 0,
};


enum mac_simple_fec_type {
	MAC_SIMPLE_FEC_TYPE_NONE = 0,
	MAC_SIMPLE_FEC_TYPE_GOLAY1224,
};


enum mac_simple_wh_type {
	MAC_SIMPLE_WH_TYPE_NONE = 0,
};


struct mac_simple_band {
	const char *name;
	uint64_t freq_first_hz;
	uint32_t channel_count;
	uint32_t channel_width_hz;
	/** @todo txpower, duty cycle */
};


struct mac_simple_mcs {
	/* Name of the specified MCS table item. Use NULL to finish the table. */
	const char *name;
	struct iradio_modulation modulation;
	uint32_t bit_rate_bps;
	enum mac_simple_fec_type fec;
	enum mac_simple_wh_type whitening;
	const struct mac_simple_band *bands[4];
};


enum mac_simple_header {
	MAC_SIMPLE_HEADER_NONE = 0,
	MAC_SIMPLE_HEADER_SOURCE = 1,
	MAC_SIMPLE_HEADER_DESTINATION = 2,
	MAC_SIMPLE_HEADER_CONTEXT = 3,
	MAC_SIMPLE_HEADER_DATA = 4,
	MAC_SIMPLE_HEADER_CRC16 = 5,
};


enum mac_simple_vtype {
	MAC_SIMPLE_VTYPE_NOP = 0,
	MAC_SIMPLE_VTYPE_VARINT = 1,
	MAC_SIMPLE_VTYPE_DATA8 = 2,
};


struct nbtable_item {
	bool used;
	uint32_t id;
	float rssi_dbm;
	uint32_t rxpackets;
	uint32_t rxbytes;
	uint32_t rxmissed;
	uint32_t txpackets;
	uint32_t txbytes;
	uint8_t counter;
	uint8_t time;
};


typedef struct {
	size_t count;
	struct nbtable_item *items;

} NbTable;


typedef struct {
	Interface interface;
	Radio *radio;
	volatile enum mac_simple_state state;
	IRadioMac iface;
	HRadioMac iface_host;

	struct mac_simple_mcs *mcs;
	struct mac_simple_band *band;
	uint32_t channel;

	uint8_t mac_key[MAC_SIMPLE_MAC_KEY_LEN];
	uint8_t network_id[MAC_SIMPLE_NETWORK_ID_SIZE];
	uint32_t node_id;
	uint8_t tx_counter;
	NbTable nbtable;
	bool debug;
	bool low_power;

	TaskHandle_t task;
} MacSimple;



mac_simple_ret_t mac_simple_init(MacSimple *self, Radio *radio);
mac_simple_ret_t mac_simple_free(MacSimple *self);
mac_simple_ret_t mac_simple_set_network_name(MacSimple *self, const char *network);
mac_simple_ret_t mac_simple_set_mcs(MacSimple *self, struct mac_simple_mcs *mcs);

