/* SPDX-License-Identifier: BSD-2-Clause
 *
 * nbus2 messaging bus implementation
 *
 * Copyright (c) 2023-2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <interfaces/stream.h>
#include <libopencm3/stm32/gpio.h>
#include <main.h>
#include "nbus2.h"
#include <blake2.h>
#include "blake2s-siv.h"
#include "halfsiphash.h"
#include <chacha20.h>

#define MODULE_NAME "nbus"


uint8_t nbus_my_id[4] = {0, 0, 0, 1};
uint8_t nbus_my_ep = 0;
uint16_t nbus_tx_counter = 0;


/**
 * tl;dr of the nbus2 packet format
 *
 * NBUS2 protocol uses UART framing over a CAN PHY, generally at 1 or 4 MBaud.
 * Other PHYs are possible, some at lower speeds (such as single-wire TTL-level
 * UART at 250 kBaud).
 *
 * Services/nodes are identified by 32 bit identifiers. Endpoints are 4 bits
 * (for 16 total source and destination endpoints). The protocol heavily resembles
 * UDP with sockets srcIP:srcPort->dstIP:dstPort being srcID:srcEP -> dstID:dstEP.
 *
 * Protocol encryption and MAC uses a SIV-mode cipher constructed from a single
 * primitive (blake2s PRF). Keys are preshared. This layer creates a basic
 * feeling of security (incl. data integrity).
 *
 * There is a fixed 8 byte SIV header + 8 byte fixed header at the beginning of
 * the packet. Then a variable number of variably sized extension headers follow.
 * Headers are padded to 4 bytes. Data follows, padded to 4 bytes.
 *
 * Packets are framed using an interpacket gap of at least 3 bytes.
 *
 * MAC is of a CSMA/CD type. When a reception is ongoing, no transmission is
 * allowed. When the MAC senses free medium, it starts transmitting a packet
 * if requested to do so. A variable length (3-11 bytes) interpacket gap is added
 * between packets to avoid transmitting all pending packets at the same time
 * once the previous transmission is completed.
 */


static void marker(uint32_t port, uint32_t pin, bool len) {
	gpio_set(port, pin);
	if (len) {
		vTaskDelay(1);
	} else {
		for (int i = 0; i < 1000; i++) {
			;
		}
	}
	gpio_clear(port, pin);
}


/**
 * @brief Receive exact number of bytes from the input stream
 *
 * This is a wrapper for the Stream::read() method which doesn't wait for the requested
 * number of bytes to be received. For protocol parsing, we need to be sure we received
 * the exact number of bytes as we requested (and eventually wait until we do).
 */
static nbus_ret_t stream_receive_expect(Nbus *self, uint8_t *buf, size_t size) {
	size_t i = 0;

	//u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("receive %d bytes"), size);
	while (i < size) {

		/** @todo Do not read too much. Must be less then the source stream buffer size (FreeRTOS StreamBuffer).
		 *        @see https://forums.freertos.org/t/how-to-deal-with-this-scenario-about-messagebuffer/8386/8 */
		size_t read = 0;
		size_t to_read = size - i;
		if (to_read > 64) {
			to_read = 64;
		}
		stream_ret_t ret = self->stream->vmt->read_timeout(self->stream, &(buf[i]), to_read, &read, NBUS_TIMEOUT_MS);
		//u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("ret %d, read %d"), ret, read);

		if (ret == STREAM_RET_OK) {
			/* All good, move on in the buffer. */
			i += read;
		} else if (ret == STREAM_RET_TIMEOUT) {
			return NBUS_RET_TIMEOUT;
		} else {
			/* Something wrong, EOF, EOT or whatever. */
			u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("stream_receive_expect: no-OK %d"), ret);
			return NBUS_RET_FAILED;
		}
	}
	return NBUS_RET_OK;
}


/**
 * @brief Check for EOT at the end of the NBUS packet
 *
 * This is specific to the underlying implementation of the UART driver. If the number of bytes requested
 * during the read is the same as the number of received bytes, EOT is never returned because it is not
 * even attempted to be read from the StreamBuffer. It cannot be reasonably implemented because peek()
 * is not available.
 *
 * We are using the fact there is a interpacket delay after EOT so we may safely read one byte from
 * the stream if done *immediately* and should receive zero bytes with EOT return value. If anything other
 * is received, we may ignore it (actually we must because we lost framing).
 *
 * @return NBUS_RET_OK if EOT is correctly detected or
 *         NBUS_RET_FAILED if some data is still left but no EOT was received. Most probably a malformed
 *         packet, missing interpacket delay, interference, etc.
 */
static nbus_ret_t stream_eot_expect(Nbus *self) {
	uint8_t buf = 0;
	//u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("expect EOT"));
	if (self->stream->vmt->read_timeout(self->stream, &buf, sizeof(buf), NULL, 0) == STREAM_RET_EOT) {
		//u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("EOT"));
		return NBUS_RET_OK;
	}

	return NBUS_RET_FAILED;
}


/**
 * @brief Wait for any gap in the input stream
 *
 * This function is used whenever the MAC loses framing. It waits for a timeout
 * (no further transmission ongoing), EOT (end of current packet encountered)
 * or simply any other fail. It basically consumes any continuous stream of data.
 */
static nbus_ret_t stream_wait_for_eot(Nbus *self) {
	uint8_t buf[8];
	//u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("wait for EOT"));
	while (self->stream->vmt->read_timeout(self->stream, buf, sizeof(buf), NULL, 10) == STREAM_RET_OK) {
		;
	}
	return NBUS_RET_OK;
}


static nbus_ret_t pbuf_receive_expect(Nbus *self, struct nbus_pbuf *pbuf, size_t len) {
	if ((pbuf->buf_len + len) > pbuf->buf_size) {
		stream_wait_for_eot(self);
		return NBUS_RET_FAILED;
	}

	nbus_ret_t ret = stream_receive_expect(self, pbuf->buf + pbuf->buf_len, len);
	if (ret == NBUS_RET_TIMEOUT) {
		/* Nothing received. */
		return NBUS_RET_TIMEOUT;
	} else if (ret != NBUS_RET_OK) {
		/* Some other non-OK return value means we cannot do much more here.
		 * Try to regain framing. */
		stream_wait_for_eot(self);
		return NBUS_RET_FAILED;
	}

	pbuf->buf_len += len;
	return NBUS_RET_OK;
}


/**
 * @brief Receive and validate the fixed header
 *
 * Read the beginning of the packet containing the fixed 16 byte header.
 * It consists of:
 * - 8 byte SIV (synthetic IV) used for data authentication and encryption
 * - 8 byte fixed header with a packet magic number, length and flags
 * - 8 byte fixed ID header
 *
 * @todo The function takes 232 us to run, with chacha20 decryption 116 us.
 *       Key derivation alone is 80 us but it is done only once.
 *       Chacha20 decryption alone is 38 us.
 */
static nbus_ret_t nbus_pbuf_receive_header(struct nbus_pbuf *self, Nbus *nbus) {
	if (pbuf_receive_expect(nbus, self, 24) != NBUS_RET_OK) {
		//u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("bad header recv"));
		return NBUS_RET_FAILED;
	}
	//u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("ke = 0x%02x 0x%02x"), pbuf->ke[0], pbuf->ke[1]);

	/* Decrypt the header. */
	ChaCha20 ch;
	chacha20_keysetup(&ch, self->ke, 128);
	chacha20_nonce(&ch, self->buf);
	chacha20_encrypt(&ch, self->buf + 8, self->buf + 8, 16);

	/* Check the magic. It is right at the beginning of the fixed header. */
	if (self->buf[8] != 'n' || self->buf[9] != '2') {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("bac magic"));
		return NBUS_RET_FAILED;
	}

	return NBUS_RET_OK;
}


static nbus_ret_t nbus_pbuf_receive_data(struct nbus_pbuf *self, Nbus *nbus) {
	size_t packet_len = self->buf[10] << 8 | self->buf[11];
	if (packet_len == 0) {
		/* Zero length packet is valid, do not read anything. */
		return NBUS_RET_OK;
	}
	if (pbuf_receive_expect(nbus, self, packet_len) != NBUS_RET_OK) {
		return NBUS_RET_FAILED;
	}

	//u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("received packet data len %u"), packet_len);
	//u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("data 0x%02x, 0x%02x, 0x%02x, 0x%02x"), pbuf->buf[24], pbuf->buf[25], pbuf->buf[26], pbuf->buf[27]);

	/* Decrypt data only, headers are already decrypted. */
	ChaCha20 ch;
	chacha20_keysetup(&ch, self->ke, 128);
	chacha20_nonce(&ch, self->buf);
	/* Consume first counter. */
	uint8_t foo[8] = {0};
	chacha20_encrypt(&ch, foo, foo, sizeof(foo));
	chacha20_encrypt(&ch, self->buf + 24, self->buf + 24, packet_len);

	/* Compute MAC from header and data. Compare with the SIV from the message. */
	uint8_t mac[8];
	halfsiphash(self->buf + 8, self->buf_len - 8, self->km, mac, sizeof(mac));

	if (memcmp(self->buf, mac, 8)) {
		//syslog(LOG_ERR, "pbuf: receive data bad MAC");
		return NBUS_RET_FAILED;
	}

	return NBUS_RET_OK;
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

	/* Clear flags */
	/** @todo set flags */
	self->buf[15] = 0;

	/* buf[16-23] are destination and source id. */

	/* Compute SIV first */
	halfsiphash(self->buf + 8, self->buf_len - 8, self->km, self->buf, 8);

	ChaCha20 ch;
	chacha20_keysetup(&ch, self->ke, 128);
	chacha20_nonce(&ch, self->buf);
	/* Encrypt the header first. */
	chacha20_encrypt(&ch, self->buf + 8, self->buf + 8, 16);
	/* Counter is incremented, the rest of the input is kept. Encrypt the data. */
	chacha20_encrypt(&ch, self->buf + 24, self->buf + 24, self->buf_len - 24);

	if (nbus->stream->vmt->write(nbus->stream, self->buf, self->buf_len) != STREAM_RET_OK) {
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


static struct nbus_pbuf *nbus_pbuf_collect_one(Nbus *self) {
	if (xSemaphoreTake(self->socket_lock, 0) != pdTRUE) {
		return NULL;
	}

	for (size_t i = 0; i < NBUS_SOCKET_COUNT; i++) {
		if (self->sockets[i].used && self->sockets[i].enabled) {
			struct nbus_pbuf *pbuf = NULL;
			if (xQueueReceive(self->sockets[i].tx_queue, &pbuf, 0) != pdTRUE) {
				continue;
			}
			xSemaphoreGive(self->socket_lock);
			return pbuf;
		}
	}

	xSemaphoreGive(self->socket_lock);
	return NULL;
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

static void nbus_mac_task(void *p) {
	Nbus *self = (Nbus *)p;

	while (true) {
		struct nbus_pbuf *pbuf = nbus_pbuf_allocate(self);
		if (pbuf == NULL) {
			/* Cannot allocate a buffer, we must drop the packet. Wait at least for EOT. */
			stream_wait_for_eot(self);
			u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("buffer allocation error"));
			continue;
		}

		nbus_ret_t ret = nbus_pbuf_receive_header(pbuf, self);
		if (ret == NBUS_RET_OK) {
			ret = nbus_pbuf_receive_data(pbuf, self);
			if (ret == NBUS_RET_OK) {
				/* End of packet. No additional data should be in the receive buffer. Check for EOT now
				 * before a new packet reception starts. The packet is considered valid regardless of any
				 * discarded additional data. */
				if (stream_eot_expect(self) != NBUS_RET_OK) {
					/* No EOT, additional data received, wait for one. */
					stream_wait_for_eot(self);
				}
				//print_pbuf(self, pbuf, "received ");
				marker(GPIOE, GPIO10, true);

				/* Dispatch the received packet to the right socket, optionally discard the packet
				 * if there is no suitable socket bound. Give up the pbuf ownership. */
				nbus_pbuf_dispatch(self, pbuf);
			}
		}
		marker(GPIOE, GPIO11, false);

		if (ret != NBUS_RET_OK) {
			u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("packet data reception error"));
			stream_wait_for_eot(self);
			nbus_pbuf_release(self, pbuf);
			continue;
		}

		pbuf = nbus_pbuf_collect_one(self);
		if (pbuf != NULL) {
			nbus_pbuf_transmit(pbuf, self);

			/* Consume echo until EOT. */
			stream_wait_for_eot(self);

			nbus_pbuf_release(self, pbuf);
		}
	}
	vTaskDelete(NULL);
}


static void nbus_hk_task(void *p) {
	Nbus *self = (Nbus *)p;
	(void)self;

	while (true) {
		vTaskDelay(1000);
	}
	vTaskDelete(NULL);
}



/* ****************************************** Datagram API ************************************************************/

static datagram_ret_t nbus_socket_write(Datagram *datagram, const void *buf, size_t len, const struct datagram_msg *msg) {
	struct nbus_socket *self = datagram->parent;
	(void)buf;
	(void)len;
	(void)msg;

	/* Socket must be bound to a local ID and EP. */
	if (!memcmp(self->local_id, (uint8_t[4]){0, 0, 0, 0}, 4)) {
		return DATAGRAM_RET_FAILED;
	}

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
	nbus_pbuf_set_source(pbuf, self->local_id, self->local_ep);
	if (memcmp(self->remote_id, (uint8_t[4]){0, 0, 0, 0}, 4)) {
		nbus_pbuf_set_destination(pbuf, self->remote_id, self->remote_ep);
	} else if (msg != NULL && msg->addr_size == 4 && memcmp(msg->dst_addr, (uint8_t[4]){0, 0, 0, 0}, 4)) {
		nbus_pbuf_set_destination(pbuf, msg->dst_addr, msg->dst_port);
	} else {
		/* Cannot determine destination ID. */
		nbus_pbuf_release(self->parent, pbuf);
		return DATAGRAM_RET_FAILED;
	}
	memcpy(pbuf->buf + 24, buf, len);
	pbuf->buf_len = len + 24;

	if (xQueueSend(self->tx_queue, &pbuf, 0) != pdTRUE) {
		nbus_pbuf_release(self->parent, pbuf);
		return DATAGRAM_RET_FAILED;
	}

	return DATAGRAM_RET_OK;
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



nbus_ret_t nbus_init(Nbus *self, Stream *stream) {
	memset(self, 0, sizeof(Nbus));
	self->stream = stream;

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
		self->sockets[i].tx_queue = xQueueCreate(NBUS_SOCKET_TX_QUEUE_LEN, sizeof(struct nbus_pbuf *));
		self->sockets[i].rx_queue = xQueueCreate(NBUS_SOCKET_RX_QUEUE_LEN, sizeof(struct nbus_pbuf *));
		if (self->sockets[i].tx_queue == NULL || self->sockets[i].rx_queue == NULL) {
			goto err;
		}
	}

	xTaskCreate(nbus_mac_task, "nbus-mac", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &(self->mac_task));
	if (self->mac_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create MAC task"));
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



/* ************************************* packet buffer pool manipulation ******************************************** */


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
	*ep = self->buf[14] >> 0x0f;

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


/* ******************************************** nbus socket manipulation **********************************************/


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

			memset(self->sockets[i].local_id, 0, 4);
			memset(self->sockets[i].local_id_mask, 0, 4);
			self->sockets[i].local_ep = 0;
			memset(self->sockets[i].remote_id, 0, 4);
			memset(self->sockets[i].remote_id_mask, 0, 4);
			self->sockets[i].remote_ep = 0;

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
