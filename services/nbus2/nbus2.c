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
	if (pbuf->buf_len + len > pbuf->buf_size) {
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
static nbus_ret_t receive_process_header(Nbus *self, struct nbus_pbuf *pbuf) {
	if (pbuf_receive_expect(self, pbuf, 24) != NBUS_RET_OK) {
		return NBUS_RET_FAILED;
	}

	/* 24 bytes received correctly. Decrypt the header (bytes 8-23)
	 * using the derived key Ke and SIV as a nonce (bytes 0-7). */
	b2s_derive_keys(self->mac_key, self->mac_key_len, pbuf->ke, pbuf->km);
	//b2s_crypt(
		//pbuf->buf + 8, /* Fixed part of the header starts from byte 8, it is 16 bytes long. */
		//16,
		//pbuf->buf, /* SIV starts t the beginning, it is always 8 bytees long. */
		//8,
		//pbuf->ke
	//);
	gpio_set(GPIOA, GPIO15);
	chacha20_context ctx;
	chacha20_keysetup(&ctx, pbuf->ke, 128);
	chacha20_nonce(&ctx, pbuf->buf);

	for (uint32_t counter = 0; counter < 16; counter++) {
		chacha20_counter(&ctx, counter);

		uint8_t keystream[64];
		chacha20_keystream(&ctx, keystream);
		for (size_t i = 0; i < 16; i++) {
			pbuf->buf[i + 8] ^= keystream[i];
		}
	}
	gpio_clear(GPIOA, GPIO15);

	//u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("decrypted magic 0x%02x 0x%02x"), pbuf->buf[8], pbuf->buf[9]);

	/* Check the magic. It is right at the beginning of the fixed header. */
	if (pbuf->buf[8] != 'n' || pbuf->buf[9] != '2') {
		return NBUS_RET_FAILED;
	}

	memcpy(pbuf->dstid, pbuf->buf + 16, 4);
	memcpy(pbuf->srcid, pbuf->buf + 20, 4);

	return NBUS_RET_OK;
}


static nbus_ret_t receive_process_data(Nbus *self, struct nbus_pbuf *pbuf) {
	size_t packet_len = pbuf->buf[10] << 8 | pbuf->buf[11];
	if (packet_len == 0) {
		/* Zero length packet is valid, do not read anything. */
		return NBUS_RET_OK;
	}
	if (pbuf_receive_expect(self, pbuf, packet_len) != NBUS_RET_OK) {
		return NBUS_RET_FAILED;
	}
	//u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("received packet data len %u"), packet_len);


	/* Decrypt data only, headers are already decrypted. It is not possible yet, so do a workaround
	 * (encrypt header back, decrypt as a whole). */
	/** @todo This decryption takes 2.16 ms for a 1024 byte packet */
	b2s_crypt(pbuf->buf + 8, 16, pbuf->buf, 8, pbuf->ke);
	b2s_crypt(pbuf->buf + 8, pbuf->buf_len - 8, pbuf->buf, 8, pbuf->ke);
	/** @todo The same decryption using chacha20 should be around 680 us
	 *        and 570 us when XORing 32 bits at once. */

	/* Compute MAC from header and data. Compare with the SIV from the message. */
	/** @todo MAC computation takes 440 us for a 1024 byte packet (blake2s) */
	//uint8_t mac[BLAKE2S_OUTBYTES];
	//b2s_siv(pbuf->buf + 8, pbuf->buf_len - 8, mac, sizeof(mac), pbuf->km);
	/** @todo MAC computation takes 59 us for a 1024 byte packet (halfsiphash) */
	uint8_t mac[8];
	halfsiphash(pbuf->buf + 8, pbuf->buf_len - 8, pbuf->km, mac, sizeof(mac));

	if (memcmp(pbuf->buf, mac, 8)) {
		//u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("bad MAC 0x%02x 0x%02x != 0x%02x 0x%02x"), pbuf->buf[0], pbuf->buf[1], mac[0], mac[1]);
		return NBUS_RET_FAILED;
	}

	//b2s_crypt(pbuf->data, pbuf->len, pbuf->siv, 8, self->ke);
	return NBUS_RET_OK;
}


static void addr_to_str(uint8_t addr[4], char *s, size_t size) {
	snprintf(s, size, "0x%02x%02x%02x%02x", addr[0], addr[1], addr[2], addr[3]);
}

static void print_pbuf(Nbus *self, struct nbus_pbuf *pbuf, const char *prefix) {
	(void)self;

	char dstid[11];
	addr_to_str(pbuf->dstid, dstid, sizeof(dstid));
	char srcid[11];
	addr_to_str(pbuf->srcid, srcid, sizeof(srcid));

	u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("%s%s -> %s, len %u, 0x%02x 0x%02x"), prefix, srcid, dstid, pbuf->buf_len, pbuf->buf[24], pbuf->buf[25]);
}


static void nbus_mac_task(void *p) {
	Nbus *self = (Nbus *)p;

	while (true) {
		struct nbus_pbuf *pbuf = nbus_pbuf_allocate(self);
		if (pbuf == NULL) {
			/* Cannot allocate a buffer, we must drop the packet. Wait at least for EOT. */
			stream_wait_for_eot(self);
			goto err;
		}
		if (receive_process_header(self, pbuf) != NBUS_RET_OK) {
			stream_wait_for_eot(self);
			goto err;
		}

		if (receive_process_data(self, pbuf) != NBUS_RET_OK) {
			goto err;
		}

		//print_pbuf(self, pbuf, "received ");



		/* End of packet. No additional data should be in the receive buffer. Check for EOT now
		 * before a new packet reception starts. */
		if (stream_eot_expect(self) != NBUS_RET_OK) {
			/* No EOT, additional data received, wait for one. */
			stream_wait_for_eot(self);
		}

		self->stream->vmt->write(self->stream, "\x00\x55\x00\x55\x00\x55\x00\x55", 8);

err:
		nbus_pbuf_release(self, pbuf);

	}
	vTaskDelete(NULL);
}


static void nbus_hk_task(void *p) {
	Nbus *self = (Nbus *)p;

	while (true) {
		vTaskDelay(1000);
	}
	vTaskDelete(NULL);
}


nbus_ret_t nbus_init(Nbus *self, Stream *stream) {
	memset(self, 0, sizeof(Nbus));
	self->stream = stream;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialize protocol driver"));

	for (size_t i = 0; i < NBUS_PBUF_COUNT; i++) {
		self->pbufs[i].buf = malloc(NBUS_PBUF_DATA_SIZE);
		if (self->pbufs[i].buf == NULL) {
			goto err;
		}
		self->pbufs[i].buf_size = NBUS_PBUF_DATA_SIZE;
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


struct nbus_pbuf *nbus_pbuf_allocate(Nbus *self) {
	for (size_t i = 0; i < NBUS_PBUF_COUNT; i++) {
		if (self->pbufs[i].used == false) {
			self->pbufs[i].used = true;
			self->pbufs[i].buf_len = 0;
			return &(self->pbufs[i]);
		}
	}
	return NULL;
}


nbus_ret_t nbus_pbuf_release(Nbus *self, struct nbus_pbuf *pbuf) {
	(void)self;

	pbuf->used = false;

	return NBUS_RET_OK;
}
