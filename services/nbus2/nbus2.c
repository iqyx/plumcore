/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * nbus2 messaging bus implementation
 *
 * Copyright (c) 2023-2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 * @brief nbus2 protocol driver (data link layer)
 *
 * nbus2 is a lightweight, UDP-like messaging bus: services/nodes are identified by 32-bit IDs and
 * 4-bit endpoints, with sockets resembling srcID:srcEP -> dstID:dstEP. This file implements the
 * protocol itself (packet assembly, parsing, dispatch and the data-link-layer authenticated
 * encryption). Medium access and framing of whole packets onto the physical medium are out of
 * scope and are provided through the Datagram interface configured in struct nbus_config.
 *
 * Every packet is authenticated and encrypted at the data link layer with a SIV-mode construction
 * (the synthetic IV doubles as the MAC tag and as the keystream IV). Two interchangeable schemes
 * are supported and selected at runtime through struct nbus_config:
 *
 * - NBUS_CRYPTO_CHACHA20_HALFSIPHASH: a ChaCha20 keystream with a HalfSipHash tag (the legacy
 *   firmware scheme).
 * - NBUS_CRYPTO_BLAKE2S_SIV: a BLAKE2s-only SIV-mode construction (b2s_crypt + b2s_siv).
 *
 * A sender emits a single configured scheme on transmit. A receiver may accept several schemes at
 * once: because both ciphers are an XOR of a deterministic keystream (keyed by ke and the cleartext
 * SIV), a failed authentication attempt is undone by re-applying the same keystream, so the next
 * scheme can be tried on the original ciphertext.
 *
 * The on-wire packet format, key derivation and the security considerations of both schemes are
 * documented in nbus2.rst next to this file.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <main.h>
#include <interfaces/datagram.h>
#include <chacha20.h>
#include <cbor.h>

#include "nbus2.h"
#include "blake2s-siv.h"
#include "halfsiphash.h"

#define MODULE_NAME "nbus"


uint8_t nbus_my_id[4] = {0, 0, 0, 1};
uint8_t nbus_my_ep = 0;
uint16_t nbus_tx_counter = 0;


/**********************************************************************************************************************
 * Packet protection (data link crypto)
 **********************************************************************************************************************/

/*
 * Each scheme provides a protect()/unprotect() pair operating in place on a packet buffer. Both
 * ciphers are an XOR of a keystream that depends only on the encryption key and the cleartext SIV
 * (buf[0..7], never encrypted), so any in-place transform is its own inverse. unprotect() therefore
 * restores the original ciphertext on any failure, which lets the receiver try the next scheme.
 *
 * On entry to unprotect() self->buf_len is the number of bytes received; on success it is trimmed to
 * the declared packet length and the cleartext header and payload are left in place.
 */


/* Decrypt/encrypt the 16-byte fixed header in place (ChaCha20 block 0). Self-inverse. */
static void nbus_chacha_header(struct nbus_pbuf *self) {
	ChaCha20 ch;
	chacha20_keysetup(&ch, self->ke, 128);
	chacha20_nonce(&ch, self->buf);
	chacha20_encrypt(&ch, self->buf + 8, self->buf + 8, 16);
}


/* Decrypt/encrypt the payload in place; it starts in ChaCha20 block 1 (the header is block 0). */
static void nbus_chacha_payload(struct nbus_pbuf *self, size_t packet_len) {
	if (packet_len == 0) {
		return;
	}
	ChaCha20 ch;
	chacha20_keysetup(&ch, self->ke, 128);
	chacha20_nonce(&ch, self->buf);
	/* Advance over block 0 (the header) so the payload is keyed with block 1 onward. */
	uint8_t skip[8] = {0};
	chacha20_encrypt(&ch, skip, skip, sizeof(skip));
	chacha20_encrypt(&ch, self->buf + 24, self->buf + 24, packet_len);
}


static void nbus_protect_chacha(struct nbus_pbuf *self) {
	/* The SIV is a HalfSipHash tag over the cleartext header and payload. */
	halfsiphash(self->buf + 8, self->buf_len - 8, self->km, self->buf, 8);
	nbus_chacha_header(self);
	nbus_chacha_payload(self, self->buf_len - 24);
}


static nbus_ret_t nbus_unprotect_chacha(struct nbus_pbuf *self) {
	nbus_chacha_header(self);

	/* Check the magic right at the beginning of the fixed header. */
	if (self->buf[8] != 'n' || self->buf[9] != '2') {
		nbus_chacha_header(self);
		return NBUS_RET_FAILED;
	}

	/* The declared payload length must fit within the received frame. */
	size_t packet_len = self->buf[10] << 8 | self->buf[11];
	if ((24 + packet_len) > self->buf_len) {
		nbus_chacha_header(self);
		return NBUS_RET_FAILED;
	}

	nbus_chacha_payload(self, packet_len);

	/* Authenticate the now-cleartext header and payload against the SIV from the packet. */
	uint8_t mac[8];
	halfsiphash(self->buf + 8, (24 + packet_len) - 8, self->km, mac, sizeof(mac));
	if (memcmp(self->buf, mac, 8)) {
		/* Undo decryption so another scheme may be tried on the original bytes. */
		nbus_chacha_payload(self, packet_len);
		nbus_chacha_header(self);
		return NBUS_RET_FAILED;
	}

	self->buf_len = 24 + packet_len;
	return NBUS_RET_OK;
}


static void nbus_protect_b2ssiv(struct nbus_pbuf *self) {
	/* The SIV is a keyed BLAKE2s tag over the cleartext header and payload. */
	b2s_siv(self->buf + 8, self->buf_len - 8, self->buf, 8, self->km);
	/* The header and payload are encrypted as one contiguous region keyed by the SIV. */
	b2s_crypt(self->buf + 8, self->buf_len - 8, self->buf, 8, self->ke);
}


static nbus_ret_t nbus_unprotect_b2ssiv(struct nbus_pbuf *self) {
	/* Decrypt the whole received region in one contiguous pass (b2s_crypt has no per-field gap). */
	b2s_crypt(self->buf + 8, self->buf_len - 8, self->buf, 8, self->ke);

	if (self->buf[8] != 'n' || self->buf[9] != '2') {
		b2s_crypt(self->buf + 8, self->buf_len - 8, self->buf, 8, self->ke);
		return NBUS_RET_FAILED;
	}

	size_t packet_len = self->buf[10] << 8 | self->buf[11];
	if ((24 + packet_len) > self->buf_len) {
		b2s_crypt(self->buf + 8, self->buf_len - 8, self->buf, 8, self->ke);
		return NBUS_RET_FAILED;
	}

	/* The SIV authenticates the declared cleartext header and payload. */
	uint8_t siv[8];
	b2s_siv(self->buf + 8, (24 + packet_len) - 8, siv, 8, self->km);
	if (memcmp(self->buf, siv, 8)) {
		b2s_crypt(self->buf + 8, self->buf_len - 8, self->buf, 8, self->ke);
		return NBUS_RET_FAILED;
	}

	self->buf_len = 24 + packet_len;
	return NBUS_RET_OK;
}


/**
 * @brief Receive and authenticate a single nbus2 packet from the medium
 *
 * A whole packet/datagram is read from the underlying Datagram interface into the packet buffer and
 * then tried against every cryptographic scheme enabled in the receive configuration until one
 * authenticates. See nbus2.rst for the packet format.
 */
static nbus_ret_t nbus_pbuf_receive(struct nbus_pbuf *self, Nbus *nbus) {
	size_t len = self->buf_size;
	if (nbus->config.dgram->vmt->read(nbus->config.dgram, self->buf, &len, NULL) != DATAGRAM_RET_OK) {
		return NBUS_RET_FAILED;
	}
	self->buf_len = len;

	/* A valid packet always carries at least the fixed 24 byte header. */
	if (self->buf_len < 24) {
		return NBUS_RET_FAILED;
	}

	if (((nbus->config.rx_crypto & NBUS_CRYPTO_BLAKE2S_SIV) && nbus_unprotect_b2ssiv(self) == NBUS_RET_OK) ||
	    ((nbus->config.rx_crypto & NBUS_CRYPTO_CHACHA20_HALFSIPHASH) && nbus_unprotect_chacha(self) == NBUS_RET_OK)) {
		/* Deserialize the packet flags now that the header is in the clear. */
		self->multicast = self->buf[15] & NBUS_FLAG_MULTICAST;
		return NBUS_RET_OK;
	}

	return NBUS_RET_FAILED;
}


static nbus_ret_t nbus_pbuf_transmit(struct nbus_pbuf *self, Nbus *nbus) {

	self->buf[8] = 'n';
	self->buf[9] = '2';

	self->buf[10] = (self->buf_len - 24) / 256;
	self->buf[11] = (self->buf_len - 24) % 256;

	self->buf[12] = nbus_tx_counter / 256;
	self->buf[13] = nbus_tx_counter % 256;
	nbus_tx_counter++;

	/* buf[14] are endpoints. */

	/* Serialize the packet flags. */
	self->buf[15] = 0;
	if (self->multicast) {
		self->buf[15] |= NBUS_FLAG_MULTICAST;
	}

	/* buf[16-23] are destination and source id. */

	/* Protect the packet with the single configured transmit scheme. */
	switch (nbus->config.tx_crypto) {
		case NBUS_CRYPTO_BLAKE2S_SIV:
			nbus_protect_b2ssiv(self);
			break;
		case NBUS_CRYPTO_CHACHA20_HALFSIPHASH:
		default:
			nbus_protect_chacha(self);
			break;
	}

	if (nbus->config.dgram->vmt->write(nbus->config.dgram, self->buf, self->buf_len, NULL) != DATAGRAM_RET_OK) {
		return NBUS_RET_FAILED;
	}

	return NBUS_RET_OK;
}


static nbus_ret_t nbus_pbuf_dispatch(Nbus *self, struct nbus_pbuf *pbuf) {
	/* We are going to traverse the socket list, obtain the mutex first. Do not block if unable. */
	if (xSemaphoreTake(self->socket_lock, 0) != pdTRUE) {
		return NBUS_RET_FAILED;
	}

	/* Try to match the socket naively. Receive everything for now. */
	for (size_t i = 0; i < NBUS_SOCKET_COUNT; i++) {
		if (self->sockets[i].used && self->sockets[i].enabled) {
			/* A multicast socket only receives multicast packets and vice versa. */
			if (self->sockets[i].multicast != pbuf->multicast) {
				continue;
			}

			/* If the socket is locally bound, check the destination ID. */
			if (memcmp(self->sockets[i].local_id, (uint8_t[4]){0, 0, 0, 0}, 4)) {
				uint8_t dst_addr[4] = {0};
				uint8_t dst_ep = 0;
				nbus_pbuf_get_destination(pbuf, dst_addr, &dst_ep);
				if (memcmp(dst_addr, self->sockets[i].local_id, 4) || dst_ep != self->sockets[i].local_ep) {
					/* Not ours, not interested. */
					continue;
				}
			}

			if (xQueueSend(self->sockets[i].rx_queue, &pbuf, 0) != pdTRUE) {
				/* Couldn't queue the pbuf, treat as failed reception. */
				break;
			}
			/* Return! First match only, we are transferring the ownership
			 * of the packet buffer to the receiver. */
			xSemaphoreGive(self->socket_lock);
			return NBUS_RET_OK;
		}
	}

	/* Release the packet buffer if dispatching failed (we are the owner
	 * and nobody was interested). */
	nbus_pbuf_release(self, pbuf);
	xSemaphoreGive(self->socket_lock);
	return NBUS_RET_FAILED;
}


static void addr_to_str(uint8_t addr[4], char *s, size_t size) {
	snprintf(s, size, "0x%02x%02x%02x%02x", addr[0], addr[1], addr[2], addr[3]);
}

/*
static void print_pbuf(Nbus *self, struct nbus_pbuf *pbuf, const char *prefix) {
	(void)self;

	char dstid[11];
	addr_to_str(pbuf->dstid, dstid, sizeof(dstid));
	char srcid[11];
	addr_to_str(pbuf->srcid, srcid, sizeof(srcid));

	u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("%s%s -> %s, len %u, 0x%02x 0x%02x"), prefix, srcid, dstid, pbuf->buf_len, pbuf->buf[24], pbuf->buf[25]);
}
*/

static void nbus_rx_task(void *p) {
	Nbus *self = (Nbus *)p;

	while (true) {
		struct nbus_pbuf *pbuf = nbus_pbuf_allocate(self);
		if (pbuf == NULL) {
			/* Cannot allocate a buffer, we must drop the packet. Yield to let other tasks run
			 * (and packet buffers be released) before trying again. */
			vTaskDelay(1);
			u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("buffer allocation error"));
			continue;
		}

		if (nbus_pbuf_receive(pbuf, self) == NBUS_RET_OK) {
			/* Dispatch the received packet to the right socket, optionally discard the packet
			 * if there is no suitable socket bound. Give up the pbuf ownership. */
			nbus_pbuf_dispatch(self, pbuf);
		} else {
			nbus_pbuf_release(self, pbuf);
		}
	}
	vTaskDelete(NULL);
}


/**
 * @brief Send a single descriptor advertisement for the given socket
 *
 * The advertisement is a multicast packet sent from the socket's own local ID and endpoint to the
 * well-known descriptor advertisement multicast ID. Its payload is a CBOR map carrying the mandatory
 * "adv" (protocol version) and "p" (protocol name) keys. Sockets without a bound local ID or without
 * a descriptor protocol are skipped.
 */
static void nbus_socket_advertise(Nbus *self, struct nbus_socket *socket) {
	if (socket->descriptor.protocol == NULL) {
		return;
	}
	/* A source ID is required so receivers know who advertised. */
	if (!memcmp(socket->local_id, (uint8_t[4]){0, 0, 0, 0}, 4)) {
		return;
	}

	struct nbus_pbuf *pbuf = nbus_pbuf_allocate(self);
	if (pbuf == NULL) {
		return;
	}

	/* Encode the advertisement payload into the payload area: the mandatory "adv" (protocol version)
	 * and "p" (protocol name) keys, plus the optional "pv" (protocol version) key when set. */
	CborEncoder encoder;
	CborEncoder map;
	cbor_encoder_init(&encoder, pbuf->buf + 24, pbuf->buf_size - 24, 0);
	cbor_encoder_create_map(&encoder, &map, CborIndefiniteLength);
	cbor_encode_text_stringz(&map, "adv");
	cbor_encode_int(&map, NBUS_ADV_VERSION);
	cbor_encode_text_stringz(&map, "p");
	cbor_encode_text_stringz(&map, socket->descriptor.protocol);
	if (socket->descriptor.protocol_version != NULL) {
		cbor_encode_text_stringz(&map, "pv");
		cbor_encode_text_stringz(&map, socket->descriptor.protocol_version);
	}
	cbor_encoder_close_container(&encoder, &map);
	pbuf->buf_len = 24 + cbor_encoder_get_buffer_size(&encoder, pbuf->buf + 24);

	/* Advertisements are multicast, sent from the socket to the well-known descriptor address. */
	pbuf->multicast = true;
	nbus_pbuf_set_source(pbuf, socket->local_id, socket->local_ep);
	nbus_pbuf_set_destination(pbuf, (uint8_t[4])NBUS_ADV_MULTICAST_ID, NBUS_ADV_MULTICAST_EP);

	xSemaphoreTake(self->tx_lock, portMAX_DELAY);
	nbus_pbuf_transmit(pbuf, self);
	xSemaphoreGive(self->tx_lock);

	nbus_pbuf_release(self, pbuf);
}


static void nbus_hk_task(void *p) {
	Nbus *self = (Nbus *)p;

	while (true) {
		vTaskDelay(pdMS_TO_TICKS(2000));

		/* Once every 2 seconds advertise the descriptor of every active socket. */
		if (xSemaphoreTake(self->socket_lock, portMAX_DELAY) != pdTRUE) {
			continue;
		}
		for (size_t i = 0; i < NBUS_SOCKET_COUNT; i++) {
			if (self->sockets[i].used && self->sockets[i].enabled) {
				nbus_socket_advertise(self, &self->sockets[i]);
			}
		}
		xSemaphoreGive(self->socket_lock);
	}
	vTaskDelete(NULL);
}



/**********************************************************************************************************************
 * Datagram API
 **********************************************************************************************************************/

static datagram_ret_t nbus_socket_write(Datagram *datagram, const void *buf, size_t len, const struct datagram_msg *msg) {
	struct nbus_socket *self = datagram->parent;
	(void)buf;
	(void)len;
	(void)msg;

	struct nbus_pbuf *pbuf = nbus_pbuf_allocate(self->parent);
	if (pbuf == NULL) {
		return DATAGRAM_RET_FAILED;
	}

	/* Check if there is space required for the whole datagram. */
	if ((len + 24) > pbuf->buf_size) {
		nbus_pbuf_release(self->parent, pbuf);
		return DATAGRAM_RET_FAILED;
	}

	/* Set all required packet fields + copy data. */
	if (memcmp(self->local_id, (uint8_t[4]){0, 0, 0, 0}, 4)) {
		/* Socket localy bound. */
		nbus_pbuf_set_source(pbuf, self->local_id, self->local_ep);
	} else if (msg != NULL && msg->addr_size == 4 && memcmp(msg->src_addr, (uint8_t[4]){0, 0, 0, 0}, 4)) {
		/* Source ID/EP supplied using msg. */
		nbus_pbuf_set_source(pbuf, msg->src_addr, msg->src_port);
	} else {
		/* Cannot determine source ID. */
		nbus_pbuf_release(self->parent, pbuf);
		return DATAGRAM_RET_FAILED;
	}

	if (memcmp(self->remote_id, (uint8_t[4]){0, 0, 0, 0}, 4)) {
		nbus_pbuf_set_destination(pbuf, self->remote_id, self->remote_ep);
	} else if (msg != NULL && msg->addr_size == 4 && memcmp(msg->dst_addr, (uint8_t[4]){0, 0, 0, 0}, 4)) {
		nbus_pbuf_set_destination(pbuf, msg->dst_addr, msg->dst_port);
	} else {
		/* Cannot determine destination ID. */
		nbus_pbuf_release(self->parent, pbuf);
		return DATAGRAM_RET_FAILED;
	}

	/* A multicast socket always sends multicast packets. */
	pbuf->multicast = self->multicast;

	memcpy(pbuf->buf + 24, buf, len);
	pbuf->buf_len = len + 24;

	/* Encrypt and transmit the packet directly in the caller context. The tx lock serializes the
	 * sequence counter and the medium writes across concurrent senders; the underlying Datagram
	 * interface takes care of framing the packet onto the medium and the inter-frame gap. */
	xSemaphoreTake(self->parent->tx_lock, portMAX_DELAY);
	nbus_ret_t ret = nbus_pbuf_transmit(pbuf, self->parent);
	xSemaphoreGive(self->parent->tx_lock);

	nbus_pbuf_release(self->parent, pbuf);

	return (ret == NBUS_RET_OK) ? DATAGRAM_RET_OK : DATAGRAM_RET_FAILED;
}


static datagram_ret_t nbus_socket_read(Datagram *datagram, void *buf, size_t *len, struct datagram_msg *msg) {
	struct nbus_socket *self = datagram->parent;
	(void)buf;
	(void)len;
	(void)msg;

	struct nbus_pbuf *pbuf = NULL;
	if (xQueueReceive(self->rx_queue, &pbuf, portMAX_DELAY) != pdTRUE) {
		return DATAGRAM_RET_FAILED;
	}

	/* Interested in data? */
	if (buf != NULL && len != NULL && *len > 0) {
		if (*len >= (pbuf->buf_len - 24)) {
			memcpy(buf, pbuf->buf + 24, pbuf->buf_len - 24);
			*len = pbuf->buf_len - 24;
		} else {
			/* Interested, but the buffer is not big enough. */
			nbus_pbuf_release(self->parent, pbuf);
			return DATAGRAM_RET_FAILED;
		}
	}

	/* Interested in metadata? */
	if (msg != NULL) {
		msg->addr_size = 4;
		uint8_t dst_ep = 0;
		uint8_t src_ep = 0;
		nbus_pbuf_get_destination(pbuf, msg->dst_addr, &dst_ep);
		msg->dst_port = dst_ep;
		nbus_pbuf_get_source(pbuf, msg->src_addr, &src_ep);
		msg->src_port = src_ep;
	}

	/* Not needed anymore. */
	nbus_pbuf_release(self->parent, pbuf);
	return DATAGRAM_RET_OK;
}

static const struct datagram_vmt nbus_socket_vmt = {
	.write = nbus_socket_write,
	.read = nbus_socket_read,
};



nbus_ret_t nbus_init(Nbus *self, const struct nbus_config *config) {
	memset(self, 0, sizeof(Nbus));
	memcpy(&self->config, config, sizeof(self->config));

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialize protocol driver"));

	/* Allocate packet buffers. */
	self->pbuf_lock = xSemaphoreCreateMutex();
	if (self->pbuf_lock == NULL) {
		goto err;
	}

	for (size_t i = 0; i < NBUS_PBUF_COUNT; i++) {
		self->pbufs[i].buf = malloc(NBUS_PBUF_DATA_SIZE);
		if (self->pbufs[i].buf == NULL) {
			goto err;
		}
		self->pbufs[i].buf_size = NBUS_PBUF_DATA_SIZE;
		self->pbufs[i].s = xSemaphoreCreateBinary();
		if (self->pbufs[i].s == NULL) {
			goto err;
		}
	}

	/* Sockets are allocated within the Nbus object, initialize the locking mutex. */
	self->socket_lock = xSemaphoreCreateMutex();
	if (self->socket_lock == NULL) {
		goto err;
	}
	for (size_t i = 0; i < NBUS_SOCKET_COUNT; i++) {
		self->sockets[i].rx_queue = xQueueCreate(NBUS_SOCKET_RX_QUEUE_LEN, sizeof(struct nbus_pbuf *));
		if (self->sockets[i].rx_queue == NULL) {
			goto err;
		}
	}

	/* Serializes the transmit path, which runs in the context of the calling threads. */
	self->tx_lock = xSemaphoreCreateMutex();
	if (self->tx_lock == NULL) {
		goto err;
	}

	/* Set default keys. */
	self->mac_key = "abcd";
	self->mac_key_len = 4;

	xTaskCreate(nbus_rx_task, "nbus-rx", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &(self->rx_task));
	if (self->rx_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create RX task"));
		goto err;
	}

	xTaskCreate(nbus_hk_task, "nbus-hk", configMINIMAL_STACK_SIZE + 128, (void *)self, 1, &(self->hk_task));
	if (self->hk_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create housekeeping task"));
		goto err;
	}

	return NBUS_RET_OK;

err:
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("init failed"));
	return NBUS_RET_FAILED;
}


nbus_ret_t nbus_set_mac_key(Nbus *self, const uint8_t *mac_key, size_t mac_key_len) {
	self->mac_key = mac_key;
	self->mac_key_len = mac_key_len;

	return NBUS_RET_OK;
}



/**********************************************************************************************************************
 * Packet buffer pool manipulation
 **********************************************************************************************************************/


/**
 * @brief Allocate a packet buffer from the pool
 *
 * After this function returns, @p pbuf ownership transfers to the caller.
 */
struct nbus_pbuf *nbus_pbuf_allocate(Nbus *self) {
	if (u_assert(self->pbuf_lock != NULL)) {
		/* Used before initialization. Something is very wrong. */
		return NULL;
	}

	if (xSemaphoreTake(self->pbuf_lock, portMAX_DELAY) != pdTRUE) {
		return NULL;
	}

	for (size_t i = 0; i < NBUS_PBUF_COUNT; i++) {
		if (self->pbufs[i].state == NBUS_PBUF_STATE_EMPTY) {
			/* On the first free pbuf match, mark as allocated and clear it. */
			self->pbufs[i].state = NBUS_PBUF_STATE_ALLOCATED;
			self->pbufs[i].buf_len = 0;
			self->pbufs[i].multicast = false;

			/* It is sufficient to clear the header. */
			memset(self->pbufs[i].buf, 0, 24);

			/* Prepare keys for the new pbuf */
			b2s_derive_keys(self->mac_key, self->mac_key_len, self->pbufs[i].ke, self->pbufs[i].km);

			xSemaphoreGive(self->pbuf_lock);
			return &(self->pbufs[i]);
		}
	}

	/* No free packet buffer found. */
	xSemaphoreGive(self->pbuf_lock);
	return NULL;
}


/**
 * @brief Release the previously allocated packet buffer
 */
nbus_ret_t nbus_pbuf_release(Nbus *self, struct nbus_pbuf *pbuf) {
	if (u_assert(self->pbuf_lock != NULL) ||
	    u_assert(pbuf != NULL)) {
		return NBUS_RET_FAILED;
	}

	if (xSemaphoreTake(self->pbuf_lock, portMAX_DELAY) != pdTRUE) {
		return NBUS_RET_FAILED;
	}

	pbuf->state = NBUS_PBUF_STATE_EMPTY;

	xSemaphoreGive(self->pbuf_lock);
	return NBUS_RET_OK;
}


nbus_ret_t nbus_pbuf_set_destination(struct nbus_pbuf *self, const uint8_t id[4], uint8_t ep) {
	self->buf[14] &= ~0xf0;
	self->buf[14] |= (ep & 0x0f) << 4;
	memcpy(&(self->buf[16]), id, 4);

	return NBUS_RET_OK;
}


nbus_ret_t nbus_pbuf_set_source(struct nbus_pbuf *self, const uint8_t id[4], uint8_t ep) {
	self->buf[14] &= ~0x0f;
	self->buf[14] |= ep & 0x0f;
	memcpy(&(self->buf[20]), id, 4);

	return NBUS_RET_OK;
}


nbus_ret_t nbus_pbuf_get_destination(struct nbus_pbuf *self, uint8_t id[4], uint8_t *ep) {
	memcpy(id, &(self->buf[16]), 4);
	*ep = self->buf[14] >> 4;

	return NBUS_RET_OK;
}


nbus_ret_t nbus_pbuf_get_source(struct nbus_pbuf *self, uint8_t id[4], uint8_t *ep) {
	memcpy(id, &(self->buf[20]), 4);
	*ep = self->buf[14] & 0x0f;

	return NBUS_RET_OK;
}


static nbus_ret_t nbus_pbuf_send(struct nbus_pbuf *self, void *buf, size_t len, uint8_t dst_id[4], uint8_t dst_ep) {
	if ((len + 24) > self->buf_size) {
		return NBUS_RET_FAILED;
	}
	memcpy(self->buf + 24, buf, len);
	self->buf_len = len + 24;


	/* Set IDs and endpoints. */
	self->buf[14] = (dst_ep & 0xf) << 4 | (nbus_my_ep & 0xf);
	memcpy(self->buf + 16, dst_id, 4);
	memcpy(self->buf + 20, nbus_my_id, 4);

	return NBUS_RET_OK;
}


/**********************************************************************************************************************
 * nbus socket manipulation
 **********************************************************************************************************************/


struct nbus_socket *nbus_socket_allocate(Nbus *self) {
	if (u_assert(self->socket_lock != NULL)) {
		return NULL;
	}

	if (xSemaphoreTake(self->socket_lock, portMAX_DELAY) != pdTRUE) {
		return NULL;
	}

	for (size_t i = 0; i < NBUS_SOCKET_COUNT; i++) {
		if (self->sockets[i].used == false) {
			/* On the first free pbuf match, mark as allocated and clear it. */
			self->sockets[i].used = true;
			self->sockets[i].parent = self;
			/** @todo do not enable until bound */
			self->sockets[i].enabled = true;
			self->sockets[i].multicast = false;

			memset(self->sockets[i].local_id, 0, 4);
			memset(self->sockets[i].local_id_mask, 0, 4);
			self->sockets[i].local_ep = 0;
			memset(self->sockets[i].remote_id, 0, 4);
			memset(self->sockets[i].remote_id_mask, 0, 4);
			self->sockets[i].remote_ep = 0;
			memset(&self->sockets[i].descriptor, 0, sizeof(struct nbus_socket_descriptor));

			self->sockets[i].datagram.vmt = &nbus_socket_vmt;
			self->sockets[i].datagram.parent = &(self->sockets[i]);

			xSemaphoreGive(self->socket_lock);
			return &(self->sockets[i]);
		}
	}

	xSemaphoreGive(self->socket_lock);
	return NULL;
}

nbus_ret_t nbus_socket_release(Nbus *self, struct nbus_socket *socket) {
	if (u_assert(self->pbuf_lock != NULL) ||
	    u_assert(socket != NULL)) {
		return NBUS_RET_FAILED;
	}

	if (xSemaphoreTake(self->pbuf_lock, portMAX_DELAY) != pdTRUE) {
		return NBUS_RET_FAILED;
	}

	socket->used = false;

	xSemaphoreGive(self->pbuf_lock);
	return NBUS_RET_OK;
}


nbus_ret_t nbus_socket_bind(struct nbus_socket *socket, const uint8_t *id, uint8_t ep) {
	(void)socket;
	(void)id;
	(void)ep;

	memcpy(socket->local_id, id, 4);
	memcpy(socket->local_id_mask, (uint8_t[4]){0xff, 0xff, 0xff, 0xff}, 4);
	socket->local_ep = ep;

	return NBUS_RET_OK;

}


nbus_ret_t nbus_socket_connect(struct nbus_socket *socket, const uint8_t *id, uint8_t ep) {
	(void)socket;
	(void)id;
	(void)ep;

	memcpy(socket->remote_id, id, 4);
	memcpy(socket->remote_id_mask, (uint8_t[4]){0xff, 0xff, 0xff, 0xff}, 4);
	socket->remote_ep = ep;

	return NBUS_RET_OK;

}


nbus_ret_t nbus_socket_set_multicast(struct nbus_socket *socket, bool multicast) {
	if (u_assert(socket != NULL)) {
		return NBUS_RET_BAD_PARAM;
	}

	socket->multicast = multicast;

	return NBUS_RET_OK;
}


nbus_ret_t nbus_socket_set_descriptor(struct nbus_socket *socket, const struct nbus_socket_descriptor *descriptor) {
	if (u_assert(socket != NULL) ||
	    u_assert(descriptor != NULL)) {
		return NBUS_RET_BAD_PARAM;
	}

	memcpy(&socket->descriptor, descriptor, sizeof(struct nbus_socket_descriptor));

	return NBUS_RET_OK;
}
