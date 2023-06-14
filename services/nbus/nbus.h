/* SPDX-License-Identifier: BSD-2-Clause
 *
 * nbus
 *
 * Copyright (c) 2018-2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>

#include <main.h>
#include <interfaces/can.h>
#include "blake2s-siv.h"
#include "cc-rpc.h"

typedef uint16_t nbus_channel_id_t;
typedef uint8_t nbus_opcode_t;
typedef uint8_t nbus_endpoint_t;

enum nbus_direction {
	NBUS_DIR_REQUEST = 0,
	NBUS_DIR_RESPONSE = 1,
	NBUS_DIR_PUBLISH = 2,
	NBUS_DIR_SUBSCRIBE = 3
};

struct __attribute__((__packed__)) nbus_id {
	uint8_t res1:4;
	nbus_channel_id_t channel:16;
	enum nbus_direction direction:2;
	uint8_t res2:2;
	nbus_opcode_t opcode:8;
};


#define NBUS_CHANNEL_MTU (64*8)

#define NBUS_OP_LEADING_MIN 0x00
#define NBUS_OP_LEADING_MAX 0x3f
#define NBUS_OP_DATA_MIN 0x40
#define NBUS_OP_DATA_MAX 0xbf
#define NBUS_OP_TRAILING 0xc0
#define NBUS_OP_ADVERTISEMENT 0xc1

#define NBUS_KEY_SIZE 16
#define NBUS_SIV_LEN 8

#define NBUS_ADV_TIME 2

struct __attribute__((__packed__)) nbus_leading_frame_msg {
	uint32_t counter;
	uint16_t len;
	uint16_t flags;
};


typedef enum {
	NBUS_RET_OK = 0,
	NBUS_RET_FAILED,
	NBUS_RET_BAD_PARAM,
	NBUS_RET_VOID,
	NBUS_RET_INVALID,
	NBUS_RET_BIG,
} nbus_ret_t;

typedef uint32_t nbus_short_id_t;

typedef struct nbus Nbus;
typedef struct nbus_channel NbusChannel;

/**********************************************************************************************************************
 * TX packet declarations
 **********************************************************************************************************************/

enum nbus_txp_state {
	NBUS_TXP_EMPTY = 0,
	NBUS_TXP_LEADING,
	NBUS_TXP_DATA,
	NBUS_TXP_TRAILING,
	NBUS_TXP_DONE,
};

typedef struct nbus_txpacket {
	enum nbus_txp_state state;

	nbus_channel_id_t channel_id;
	nbus_endpoint_t ep;
	uint32_t packet_counter;

	/* Progress variables */
	size_t next_frag;
	size_t packet_sent;


	/* Preallocated scratchpad. */
	void *buf;
	size_t len;
} NbusTxPacket;

/**********************************************************************************************************************
 * RX packet declarations
 **********************************************************************************************************************/

enum nbus_rxp_state {
	NBUS_RXP_EMPTY = 0,
	NBUS_RXP_READY,
	NBUS_RXP_DATA,
	NBUS_RXP_TRAILING,
	NBUS_RXP_DONE,
	NBUS_RXP_INVALID,
	NBUS_RXP_VOID,
	NBUS_RXP_INVALID_ID,
};

/* Buffer for assembling fragmented received packets. Beware there
 * is only one buffer for all endpoints. Endpoint packets cannot interleave. */
typedef struct nbus_rxpacket {
	enum nbus_rxp_state state;

	uint8_t *buf;
	size_t buf_size;

	uint32_t frame_expected;
	size_t packet_size;
	size_t expected_packet_size;
	nbus_endpoint_t ep;

	SemaphoreHandle_t done;
} NbusRxPacket;

/**********************************************************************************************************************
 * NBUS channel & related declarations
 **********************************************************************************************************************/

typedef struct nbus_channel {
	Nbus *nbus;
	NbusChannel *next;
	NbusChannel *parent;

	/* Basic channel parameters. */
	nbus_channel_id_t channel_id;
	bool channel_id_valid;
	nbus_short_id_t short_id;
	const char *name;

	/* Packet instances. */
	NbusTxPacket txpacket;
	NbusRxPacket rxpacket;

	/* RPC client instance for EP 0 channel C&C. */
	CborRpc ccrpc;
	struct cbor_rpc_handler name_handler;
	struct cbor_rpc_handler parent_handler;

	/* A monotonically increasing counter. The calue is increased by one
	 * every time a discovery probe response message is sent. */
	uint32_t counter;

	uint32_t packet_counter;

	/* Blake2s-SIV message authentication and ancryption */
	// uint8_t key[NBUS_KEY_SIZE];
	// uint8_t ke[B2S_KE_LEN];
	// uint8_t km[B2S_KM_LEN];
	time_t adv_time;


} NbusChannel;


typedef struct nbus {
	/* Linked list of all associated channels */
	NbusChannel *first;

	Can *can;

	TaskHandle_t receive_task;
	TaskHandle_t housekeeping_task;
} Nbus;


nbus_ret_t nbus_init(Nbus *self, Can *can);
nbus_ret_t nbus_add_channel(Nbus *self, NbusChannel *channel);
nbus_ret_t nbus_channel_process_cc(NbusChannel *self, void *buf, size_t len);
nbus_ret_t nbus_irq_handler(Nbus *self);

nbus_ret_t nbus_channel_init(NbusChannel *self, const char *name);
/**
 * @brief Send packet using a nbus protocol
 *
 * Packet fragmentation and sending occurs in the caller thread and is fully synchronous. The function returns after
 * the complete packet is processed and sent. The buffer can be freed after the function returns.
 *
 * A channel mutex lock is acquired before the packet is allowed to be sent. The caller thread is suspended indefinitely
 * until the lock is acquired.
 */
nbus_ret_t nbus_channel_send(NbusChannel *self, nbus_endpoint_t ep, void *buf, size_t len);
nbus_ret_t nbus_channel_receive(NbusChannel *self, nbus_endpoint_t *ep, void *buf, size_t buf_size, size_t *len, uint32_t timeout_ms);

nbus_ret_t nbus_channel_set_explicit_short_id(NbusChannel *self, nbus_short_id_t short_id);
nbus_ret_t nbus_channel_set_parent(NbusChannel *self, NbusChannel *parent);

void nbus_parse_id(uint32_t id, struct nbus_id *sid);
uint32_t nbus_build_id(struct nbus_id *id);

