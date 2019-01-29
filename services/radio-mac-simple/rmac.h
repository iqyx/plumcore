/*
 * rMAC (PRF-based radio MAC) public declarations and API
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

#include "main.h"

#include "interface.h"
#include "interfaces/radio.h"
#include "interfaces/radio-mac/descriptor.h"
#include "interfaces/radio-mac/host.h"
#include "interfaces/clock/descriptor.h"
#include "rmac_pbuf.pb.h"


#define MAC_SIMPLE_MAC_KEY_LEN 32
#define RMAC_RADIO_SYNC_SIZE 4
#define CHECK(c, r) if (!(c)) return r
#define MAC_SIMPLE_BANDS_PER_MCS 4
#define RMAC_UNIVERSE_KEY_MAX 32


typedef enum {
	NBTABLE_RET_OK = 0,
	NBTABLE_RET_FAILED,
	NBTABLE_RET_NULL,
	NBTABLE_RET_BAD_ARG,
} nbtable_ret_t;

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




enum rmac_mcs_fec_type {
	RMAC_MCS_FEC_TYPE_NONE = 0,
	RMAC_MCS_FEC_TYPE_GOLAY1224,
};


struct rmac_sas {
	/* Frequency-domain slots. */
	uint32_t channel_size_hz;
	uint32_t channel_count;

	/* Time-domain slots. */
	uint32_t slot_length_us;
	uint32_t epoch_length_slots;
};


struct rmac_mcs {
	const char *name;
	enum rmac_mcs_fec_type fec;
	struct iradio_modulation modulation;
	uint32_t channel_width_hz;
	uint32_t bitrate;


};


#define RMAC_PBUF_TMP_LEN 256
#define RMAC_PBUF_KE_LEN 16
#define RMAC_PBUF_KM_LEN 16
#define RMAC_PBUF_SIV_LEN 16
#define RMAC_PBUF_PACKET_LEN_MAX 255

typedef enum {
	RMAC_PBUF_RET_OK = 0,
	RMAC_PBUF_RET_FAILED,
	RMAC_PBUF_RET_NULL,
	RMAC_PBUF_RET_BAD_ARG,
	RMAC_PBUF_RET_NO_BUF,
	RMAC_PBUF_RET_DECRYPT_FAILED,
	RMAC_PBUF_RET_ENCRYPT_FAILED,
	RMAC_PBUF_RET_MAC_FAILED,
	RMAC_PBUF_RET_DECODING_FAILED,
	RMAC_PBUF_RET_ENCODING_FAILED,
} rmac_pbuf_ret_t;

typedef struct rmac_pbuf {
	bool used;

	/* The protobuf message structure is used to save packet data. */
	RmacPacket msg;

	uint8_t key_encrypt[RMAC_PBUF_KE_LEN];
	uint8_t key_mac[RMAC_PBUF_KM_LEN];

} RmacPbuf;


enum rmac_slot_type {
	/* Radio is in the RX mode during the search slot. The scheduler is not
	 * expecting any data to be received from current neighbors. */
	RMAC_SLOT_TYPE_RX_SEARCH,

	/* Unmanaged slots can be used to receive data from any neighbor, even from
	 * an unmanaged one (eg. from a newly connected neighbor). */
	RMAC_SLOT_TYPE_RX_UNMANAGED,

	/* The slot is scheduled in the same time as the neighbor's unicast TX slot. */
	RMAC_SLOT_TYPE_RX_UNICAST,

	/* Data to be received by all neighbors. */
	RMAC_SLOT_TYPE_TX_BROADCAST,

	/* Small control slots received by all neighbors. */
	RMAC_SLOT_TYPE_TX_CONTROL,

	/* The slot is scheduled in the same time as the neighbors unicast RX slot. */
	RMAC_SLOT_TYPE_TX_UNICAST,
};

struct rmac_slot {
	/* Absolute time in microseconds when the slot starts. */
	uint64_t slot_start_us;

	/* Length of the slot in microseconds. */
	uint32_t slot_length_us;

	enum rmac_slot_type type;
	uint32_t peer_id;

	struct radio_scheduler_packet *packet;

};


/* Priority-queue containing scheduled RX/TX slots. The slot on the top of the
 * structure is always the one occurring first. */
struct rmac_slot_queue {
	struct rmac_slot *items;

	/* Size of the items array (allocated). */
	size_t size;

	/* Number of items in the array. */
	size_t len;

	/* Mutex for accessing the data structure. */
	SemaphoreHandle_t lock;

	SemaphoreHandle_t tx_available;
};


/* OK */
typedef enum {
	RMAC_RET_OK = 0,
	RMAC_RET_NULL,
	RMAC_RET_BAD_ARG,
	RMAC_RET_FAILED,
	RMAC_RET_TIMEOUT,
	RMAC_RET_NOOP,
	RMAC_RET_BAD_STATE,
} rmac_ret_t;


/* OK */
enum rmac_state {
	RMAC_STATE_UNINITIALIZED = 0,
	RMAC_STATE_STOPPED,
	RMAC_STATE_SEARCHING,
	RMAC_STATE_RUNNING,
	RMAC_STATE_STOP_REQ,
};


/* What algorithm is used to select the frequency of a particular slot. */
enum rmac_fhss_algo {
	/* Only a single channel is used for all transmissions and receptions. */
	RMAC_FHSS_ALGO_SINGLE = 0,

	/* Radio channel is determined by a PRF (pseudo-random function) output.
	 * Input to the PRF is negotiated using a custom protocol. Blake2s-based
	 * algorithm is used. */
	RMAC_FHSS_ALGO_HASH,
};

/* How are slots scheduled in time. */
enum rmac_tdma_algo {
	/* A TX slot is scheduled whenever there is a packet to transmit and
	 * no reception is ongoing. RX slots are scheduled whenever possible.
	 * (ie. the radio is in full-RX mode all the time except when a packet
	 * is to be transmitted). */
	RMAC_TDMA_ALGO_CSMA = 0,

	/* A TX slot is scheduled whenever there is a packet to transmit and
	 * no reception is ongoing. A short RX slot is scheduled immediately
	 * after the TX slot ends. If a packet is received, another RX slot is
	 * scheduled until no more packets are received. */
	RMAC_TDMA_ALGO_IMMEDIATE_RX,

	/* All slots are scheduled based on a PRF (pseudo-random function)
	 * output. Input to the PRF is negotiated using a custom protocol.
	 * Blake2s-based algorithm is used. */
	RMAC_TDMA_ALGO_HASH,
};


struct radio_scheduler_rxpacket {
	uint8_t buf[256];
	size_t len;
	struct iradio_receive_params rxparams;
};

struct radio_scheduler_txpacket {
	uint8_t buf[256];
	size_t len;
	struct iradio_send_params txparams;
};

struct radio_scheduler_packet {
	volatile bool used;
	union {
		struct radio_scheduler_rxpacket rxpacket;
		struct radio_scheduler_txpacket txpacket;
	};
};


/* Preallocated pool of serialized/received packets to avoid
 * allocating/deallocating in runtime. */
typedef struct {
	struct radio_scheduler_packet *pool;
	size_t size;
	SemaphoreHandle_t lock;
} RmacPacketPool;


typedef struct {
	Interface interface;

	/* Dependencies and interfaces. */
	Radio *radio;
	IRadioMac iface;
	HRadioMac iface_host;
	IClock *clock;

	/* rMAC state. */
	volatile enum rmac_state state;
	uint32_t node_id;
	uint8_t tx_counter;
	NbTable nbtable;
	bool debug;
	int32_t slot_start_time_error_ema_us;
	enum rmac_fhss_algo fhss_algo;
	enum rmac_tdma_algo tdma_algo;
	uint8_t universe_key[RMAC_UNIVERSE_KEY_MAX];
	size_t universe_key_len;
	uint8_t radio_sync[RMAC_RADIO_SYNC_SIZE];

	/* Tasks. */
	TaskHandle_t radio_scheduler_task;
	TaskHandle_t slot_scheduler_task;
	QueueHandle_t radio_scheduler_rxqueue;
	TaskHandle_t rx_process_task;
	TaskHandle_t tx_process_task;

	/* Preallocated structures. */
	struct rmac_slot_queue slot_queue;
	RmacPacketPool packet_pool;
	RmacPbuf rx_pbuf;
	RmacPbuf tx_pbuf;
} Rmac;


/* Public API */
rmac_ret_t rmac_init(Rmac *self, Radio *radio);
rmac_ret_t rmac_free(Rmac *self);
rmac_ret_t rmac_start(Rmac *self);
rmac_ret_t rmac_stop(Rmac *self);
rmac_ret_t rmac_set_mcs_single(Rmac *self, struct rmac_mcs *mcs);
rmac_ret_t rmac_set_sas(Rmac *self, struct rmac_sas *sas);
rmac_ret_t rmac_set_fhss_algo(Rmac *self, enum rmac_fhss_algo fhss);
rmac_ret_t rmac_set_tdma_algo(Rmac *self, enum rmac_tdma_algo tdma);
rmac_ret_t rmac_set_frequency(Rmac *self, uint64_t frequency_hz);
rmac_ret_t rmac_set_universe_key(Rmac *self, uint8_t *key, size_t len);
rmac_ret_t rmac_set_clock(Rmac *self, IClock *clock);


/* Radio scheduler. */
rmac_ret_t process_packet(Rmac *self, struct radio_scheduler_rxpacket *packet);
rmac_ret_t radio_scheduler_start(Rmac *self);
rmac_ret_t radio_scheduler_rx(Rmac *self, struct radio_scheduler_rxpacket *packet);


/* RX/TX packet object pool */
rmac_ret_t rmac_packet_pool_init(RmacPacketPool *self, size_t size);
rmac_ret_t rmac_packet_pool_free(RmacPacketPool *self);
struct radio_scheduler_packet *rmac_packet_pool_get(RmacPacketPool *self);
rmac_ret_t rmac_packet_pool_release(RmacPacketPool *self, struct radio_scheduler_packet *packet);


/* Functions for managing the slot queue. */
rmac_ret_t rmac_slot_queue_init(struct rmac_slot_queue *self, size_t size);
rmac_ret_t rmac_slot_queue_free(struct rmac_slot_queue *self);
void rmac_slot_queue_siftup(struct rmac_slot_queue *self, size_t index);
void rmac_slot_queue_siftdown(struct rmac_slot_queue *self, size_t index);
rmac_ret_t rmac_slot_queue_insert(struct rmac_slot_queue *self, struct rmac_slot *slot);
rmac_ret_t rmac_slot_queue_peek(struct rmac_slot_queue *self, struct rmac_slot *slot);
rmac_ret_t rmac_slot_queue_remove(struct rmac_slot_queue *self, struct rmac_slot *slot);
size_t rmac_slot_queue_len(struct rmac_slot_queue *self);
bool rmac_slot_queue_is_available(struct rmac_slot_queue *self, enum rmac_slot_type type);
rmac_ret_t rmac_slot_queue_attach_packet(struct rmac_slot_queue *self, enum rmac_slot_type type, struct radio_scheduler_packet *packet, struct rmac_slot *slot);


/* Neighbor table management functions. */
nbtable_ret_t nbtable_init(NbTable *self, size_t items);
nbtable_ret_t nbtable_free(NbTable *self);
struct nbtable_item *nbtable_find_id(NbTable *self, uint32_t id);
struct nbtable_item *nbtable_find_empty(NbTable *self);
struct nbtable_item *nbtable_find_or_add_id(NbTable *self, uint32_t id);

/* Neighbor manipulation functions. */
nbtable_ret_t nbtable_init_nb(NbTable *self, struct nbtable_item *item, bool used);
nbtable_ret_t nbtable_free_nb(NbTable *self, struct nbtable_item *item);
nbtable_ret_t nbtable_update_rx_counter(NbTable *self, struct nbtable_item *item, uint8_t counter, size_t len);
nbtable_ret_t nbtable_update_rssi(NbTable *self, struct nbtable_item *item, float rssi_dbm);


/* Packet buffer/encoder/decoder. */
/* There is no pbuf_init nor pbuf_free. No data are allocated inside. */
rmac_pbuf_ret_t rmac_pbuf_clear(RmacPbuf *self);
rmac_pbuf_ret_t rmac_pbuf_use(RmacPbuf *self);
rmac_pbuf_ret_t rmac_pbuf_universe_key(RmacPbuf *self, uint8_t *key, size_t len);
/* Note: non-const buf, decryption is done in-place! */
rmac_pbuf_ret_t rmac_pbuf_read(RmacPbuf *self, uint8_t *buf, size_t len);
rmac_pbuf_ret_t rmac_pbuf_write(RmacPbuf *self, uint8_t *buf, size_t buf_size, size_t *len);

/** @todo FEC encoding/decoding */
