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

typedef uint16_t nbus_channel_id_t;
typedef uint8_t nbus_opcode_t;
typedef uint8_t nbus_endpoint_t;

struct nbus_id {
	nbus_channel_id_t channel;
	nbus_opcode_t opcode;
	bool multicast;
	bool response;
};


#define NBUS_CHANNEL_MTU 256
#define NBUS_DESCRIPTOR_LEN 128

#define NBUS_OP_LEADING_MIN 0x00
#define NBUS_OP_LEADING_MAX 0x3f
#define NBUS_OP_DATA_MIN 0x40
#define NBUS_OP_DATA_MAX 0xbf
#define NBUS_OP_TRAILING 0xc0

#define NBUS_KEY_SIZE 16
#define NBUS_SIV_LEN 8

enum nbus_discovery_stream {
	NBUS_DISC_STREAM_MASK_PROBE = 1,
	NBUS_DISC_STREAM_SET_CHANNEL = 2,
	NBUS_DISC_STREAM_PROBE = 3,
};

struct __attribute__((__packed__)) nbus_leading_frame_msg {
	uint32_t counter;
	uint16_t len;
	uint16_t flags;
};


/**
 * @brief Channel discovery by short ID/mask
 *
 * On the reception of this message, implementation of the peripheral Nbus checks if there is
 * at least one channel with shortID in the ID/mask range from the message.
 * If yes, a confirm message is sent back saying "there is at leas one matched channel".
 */
struct __attribute__((__packed__)) nbus_discovery_mask_probe_msg {
	uint32_t id;
	uint32_t mask;
};

/**
 * @brief Set channel ID
 *
 * On the reception, channel with the shortID contained in the message is set a new channel ID.
 * The new channel ID will be used to communicate with other devices.
 */
struct __attribute__((__packed__)) nbus_discovery_set_channel_msg {
	uint32_t id;
	uint16_t channel;
};

/**
 * @brief Probe channel
 *
 * Check if there is a channel with the specified short_id. If yes, respond with its
 * channel_id and the value of a monotonically increasing counter. The counter is increased
 * when a probe message is received. This allows to check the state of the channel -
 * that means if a channel_id is assigned and what's the age of the channel.
 */
struct __attribute__((__packed__)) nbus_discovery_probe_msg {
	uint32_t id;
};

struct __attribute__((__packed__)) nbus_discovery_probe_response_msg {
	uint32_t counter;
	uint16_t channel;
};


typedef enum {
	NBUS_RET_OK = 0,
	NBUS_RET_FAILED,
	NBUS_RET_BAD_PARAM,
	NBUS_RET_VOID,
} nbus_ret_t;

typedef uint32_t short_id_t;

typedef struct nbus Nbus;
typedef struct nbus_channel NbusChannel;

typedef struct nbus_channel {
	Nbus *parent;
	NbusChannel *next;
	const char *name;

	/* A channel may have not its channel id assigned yet. This is usually
	 * before a discovery process was finished by the controller.
	 * Assignment can be done even without a discovery, using a standalone
	 * set_channel message. */
	bool channel_assigned;
	nbus_channel_id_t channel;
	bool inhibit_discovery;

	/* The short_id of a channel is always valid. When a channel is created,
	 * a suitable value should be used, most preferably a random one.
	 * If the MCU has an unique identifier assigned, a hashed value of the
	 * identifier may be used. */
	short_id_t short_id;

	/* A monotonically increasing counter. The calue is increased by one
	 * every time a discovery probe response message is sent. */
	uint32_t counter;

	/* All channel packets will be sent as multicast. This is for the future
	 * possibility to send unsolicited "responses" to many surrouding devices. */
	bool multicast;

	/* If peripheral is true, response is always set to true. Peripheral cannot sent a request.  */
	bool peripheral;

	/* Buffer for assembling fragmented received packets. Beware there
	 * is only one buffer for all endpoints. Endpoint packets cannot interleave. */
	bool in_progress;
	uint8_t buf[NBUS_CHANNEL_MTU];
	size_t buf_len;
	uint32_t frame_expected;
	size_t packet_size;
	uint32_t packet_counter;
	nbus_endpoint_t ep;

	/* A function to call when a packet for this channel is received.
	 * The rx_handler_context value is passed as the context parameter. */
	nbus_ret_t (*rx_handler)(void *context, nbus_endpoint_t ep, void *buf, size_t len);
	void *rx_handler_context;

	/* A function to call when a descriptor request was received over stream 0.
	 * The function is called multiple times as long as it returns NBUS_RET_OK. */
	nbus_ret_t (*descriptor)(void *context, uint8_t id, void *buf, size_t *len);
	void *descriptor_context;

	const void *long_id;
	size_t long_id_len;

	const char * const *descriptor_strings;
	size_t descriptor_strings_count;

	/* Blake2s-SIV message authentication and ancryption */
	uint8_t key[NBUS_KEY_SIZE];
	uint8_t ke[B2S_KE_LEN];
	uint8_t km[B2S_KM_LEN];


} NbusChannel;


typedef struct nbus {
	/* Linked list of all associated channels */
	NbusChannel *first;

	/* A special discovery protocol channel with ID 0 */
	NbusChannel discovery_channel;

	Can *can;

	/* RX thread */
	TaskHandle_t receive_task;
} Nbus;


nbus_ret_t nbus_init(Nbus *self, Can *can);
nbus_ret_t nbus_add_channel(Nbus *self, NbusChannel *channel);
nbus_ret_t nbus_irq_handler(Nbus *self);

/** @todo remove the channel number parameter */
nbus_ret_t nbus_channel_init(NbusChannel *self, const char *name);
nbus_ret_t nbus_channel_send(NbusChannel *self, nbus_endpoint_t ep, void *buf, size_t len);
nbus_ret_t nbus_channel_set_rx_handler(NbusChannel *self, nbus_ret_t (*rx_handler)(void *context, nbus_endpoint_t ep, void *buf, size_t len), void *context);
nbus_ret_t nbus_channel_set_descriptor(NbusChannel *self, nbus_ret_t (*descriptor)(void *context, uint8_t id, void *buf, size_t *len), void *context);
nbus_ret_t nbus_channel_set_descriptor_strings(NbusChannel *self, const char * const *strings, size_t count);
nbus_ret_t nbus_channel_set_short_id(NbusChannel *self, short_id_t short_id);
nbus_ret_t nbus_channel_set_long_id(NbusChannel *self, const void *long_id, size_t len);

void nbus_parse_id(uint32_t id, struct nbus_id *sid);
uint32_t nbus_build_id(struct nbus_id *id);

