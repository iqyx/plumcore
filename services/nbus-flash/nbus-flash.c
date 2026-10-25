/* SPDX-License-Identifier: BSD-2-Clause
 *
 * NBUS flash access service
 *
 * Copyright (c) 2024, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

#include <main.h>
#include <cbor.h>

#include <libopencm3/cm3/scb.h>

#include <interfaces/flash.h>
#include <interfaces/datagram.h>

#include "nbus-flash.h"

#define MODULE_NAME "nbus-flash"



static bool cbor_map_get_str(CborValue *map, const char *key, char *buf, size_t size) {
	CborValue value;
	cbor_value_map_find_value(map, key, &value);
	if (!cbor_value_is_valid(&value) || !cbor_value_is_text_string(&value)) {
		buf[0] = '\0';
		return false;
	}
	size_t sw = size;
	cbor_value_copy_text_string(&value, buf, &sw, NULL);
	buf[size - 1] = '\0';
	return true;
}


static bool cbor_map_get_data(CborValue *map, const char *key, uint8_t *buf, size_t *size) {
	CborValue value;
	cbor_value_map_find_value(map, key, &value);
	if (!cbor_value_is_valid(&value) || !cbor_value_is_byte_string(&value)) {
		return false;
	}
	cbor_value_copy_byte_string(&value, buf, size, NULL);
	return true;
}


static bool cbor_map_get_uint(CborValue *map, const char *key, uint32_t *i) {
	CborValue value;
	cbor_value_map_find_value(map, key, &value);
	if (!cbor_value_is_valid(&value) || !cbor_value_is_unsigned_integer(&value)) {
		return false;
	}
	uint64_t uint = 0;
	cbor_value_get_uint64(&value, &uint);
	*i = uint;
	return true;
}


/* Find the opened session given by the "sid" of the request and mark it as used. Returns NULL if there is none. */
static struct nbus_flash_session *session_find(NbusFlash *self, CborValue *imap) {
	uint32_t sid;
	if (!cbor_map_get_uint(imap, "sid", &sid)) {
		return NULL;
	}
	for (size_t i = 0; i < NBUS_FLASH_SESSIONS; i++) {
		struct nbus_flash_session *session = &self->sessions[i];
		if (session->flash != NULL && session->sid == sid) {
			session->used = ++self->use_seq;
			return session;
		}
	}
	return NULL;
}


/* Beware this implementation accepts any flash partition name and is a serious
 * security hole. */
static nbus_flash_ret_t process_cc_info(NbusFlash *self, CborValue *imap, CborEncoder *omap) {
	(void)self;
	char dev_name[16];
	cbor_map_get_str(imap, "n", dev_name, sizeof(dev_name));

	Flash *flash = NULL;
	if (iservicelocator_query_name_type(locator, dev_name, ISERVICELOCATOR_TYPE_FLASH, (Interface **)&flash) == ISERVICELOCATOR_RET_OK) {

		/* Get flash block sizes */
		if (flash->vmt->get_size != NULL) {
			cbor_encode_text_stringz(omap, "sizes");
			CborEncoder size_list;
			cbor_encoder_create_array(omap, &size_list, CborIndefiniteLength);

			uint32_t step = 0;
			size_t size = 0;
			flash_block_ops_t ops;
			while (flash->vmt->get_size(flash, step, &size, &ops) == FLASH_RET_OK) {
				CborEncoder device_size;
				cbor_encoder_create_map(&size_list, &device_size, CborIndefiniteLength);

				cbor_encode_text_stringz(&device_size, "s");
				cbor_encode_uint(&device_size, size);
				cbor_encode_text_stringz(&device_size, "ops");
				cbor_encode_uint(&device_size, ops);

				cbor_encoder_close_container(&size_list, &device_size);
				step++;
			}

			cbor_encoder_close_container(omap, &size_list);
		}
	} else {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "device not found");
	}

	return NBUS_FLASH_RET_OK;
}


static nbus_flash_ret_t process_cc_list(NbusFlash *self, CborValue *imap, CborEncoder *omap) {
	(void)self;
	(void)imap;
	cbor_encode_text_stringz(omap, "d");

	CborEncoder device_list;
	cbor_encoder_create_array(omap, &device_list, CborIndefiniteLength);

	Flash *flash = NULL;
	for (size_t i = 0; (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_FLASH, i, (Interface **)&flash)) == ISERVICELOCATOR_RET_OK; i++) {
		CborEncoder device;
		cbor_encoder_create_map(&device_list, &device, CborIndefiniteLength);

		const char *name = NULL;
		iservicelocator_get_name(locator, (Interface *)flash, &name);
		cbor_encode_text_stringz(&device, "n");
		cbor_encode_text_stringz(&device, name);

		cbor_encoder_close_container(&device_list, &device);
	}

	cbor_encoder_close_container(omap, &device_list);
	return NBUS_FLASH_RET_OK;
}


static nbus_flash_ret_t process_cc_open(NbusFlash *self, CborValue *imap, CborEncoder *omap) {
	char dev_name[16];
	cbor_map_get_str(imap, "n", dev_name, sizeof(dev_name));

	Flash *flash = NULL;
	if (iservicelocator_query_name_type(locator, dev_name, ISERVICELOCATOR_TYPE_FLASH, (Interface **)&flash) != ISERVICELOCATOR_RET_OK) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "device not found");
		return NBUS_FLASH_RET_OK;
	}

	/* Take a free session or reuse the least recently used one if all are taken (eg. by a client which
	 * disappeared without closing its session). */
	struct nbus_flash_session *session = &self->sessions[0];
	for (size_t i = 0; i < NBUS_FLASH_SESSIONS; i++) {
		if (self->sessions[i].flash == NULL) {
			session = &self->sessions[i];
			break;
		}
		if (self->sessions[i].used < session->used) {
			session = &self->sessions[i];
		}
	}

	/* Session IDs are never reused (until wrapping around) and 0 is never assigned. */
	self->last_sid++;
	if (self->last_sid == 0) {
		self->last_sid = 1;
	}
	session->sid = self->last_sid;
	session->flash = flash;
	session->used = ++self->use_seq;

	cbor_encode_text_stringz(omap, "sid");
	cbor_encode_uint(omap, session->sid);

	return NBUS_FLASH_RET_OK;
}


static nbus_flash_ret_t process_cc_close(NbusFlash *self, CborValue *imap, CborEncoder *omap) {
	struct nbus_flash_session *session = session_find(self, imap);
	if (session == NULL) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "no session opened");
		return NBUS_FLASH_RET_FAILED;
	}

	cbor_encode_text_stringz(omap, "sid");
	cbor_encode_uint(omap, session->sid);

	session->flash = NULL;
	session->sid = 0;

	return NBUS_FLASH_RET_OK;
}


static nbus_flash_ret_t process_cc_erase(NbusFlash *self, CborValue *imap, CborEncoder *omap) {
	struct nbus_flash_session *session = session_find(self, imap);
	if (session == NULL) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "session error");
		return NBUS_FLASH_RET_FAILED;
	}
	Flash *flash = session->flash;

	uint32_t addr;
	uint32_t len;
	if (!cbor_map_get_uint(imap, "addr", &addr) || !cbor_map_get_uint(imap, "len", &len)) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "bad arguments");
		return NBUS_FLASH_RET_FAILED;
	}

	if (flash->vmt->erase != NULL && flash->vmt->erase(flash, addr, len) != FLASH_RET_OK) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "erasing failed");
		return NBUS_FLASH_RET_FAILED;
	}

	/* We need to return something */
	cbor_encode_text_stringz(omap, "ret");
	cbor_encode_text_stringz(omap, "ok");

	return NBUS_FLASH_RET_OK;
}


static nbus_flash_ret_t process_cc_write(NbusFlash *self, CborValue *imap, CborEncoder *omap) {
	struct nbus_flash_session *session = session_find(self, imap);
	if (session == NULL) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "session error");
		return NBUS_FLASH_RET_FAILED;
	}
	Flash *flash = session->flash;

	uint32_t addr;
	uint32_t len;
	uint8_t buf[NBUS_FLASH_BLOCK_LEN];
	size_t buf_size = sizeof(buf);
	if (!cbor_map_get_uint(imap, "addr", &addr) ||
	    !cbor_map_get_uint(imap, "len", &len) ||
	    !cbor_map_get_data(imap, "d", buf, &buf_size) ||
	    buf_size != len ||
	    len > NBUS_FLASH_BLOCK_LEN) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "bad arguments");
		return NBUS_FLASH_RET_FAILED;
	}

	if (flash->vmt->write != NULL && flash->vmt->write(flash, addr, buf, len) != FLASH_RET_OK) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "writing failed");
		return NBUS_FLASH_RET_FAILED;
	}

	cbor_encode_text_stringz(omap, "ret");
	cbor_encode_text_stringz(omap, "ok");

	return NBUS_FLASH_RET_OK;
}


static nbus_flash_ret_t process_cc_read(NbusFlash *self, CborValue *imap, CborEncoder *omap) {
	struct nbus_flash_session *session = session_find(self, imap);
	if (session == NULL) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "session error");
		return NBUS_FLASH_RET_FAILED;
	}
	Flash *flash = session->flash;

	uint32_t addr;
	uint32_t len;
	if (!cbor_map_get_uint(imap, "addr", &addr) || !cbor_map_get_uint(imap, "len", &len) || len > NBUS_FLASH_BLOCK_LEN) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "bad arguments");
		return NBUS_FLASH_RET_FAILED;
	}

	uint8_t buf[NBUS_FLASH_BLOCK_LEN];
	if (flash->vmt->read != NULL && flash->vmt->read(flash, addr, buf, len) != FLASH_RET_OK) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "reading failed");
		return NBUS_FLASH_RET_FAILED;
	}

	/* Echo the requested address so the host can match this reply to its request and reject a stale/desynced one. */
	cbor_encode_text_stringz(omap, "addr");
	cbor_encode_uint(omap, addr);
	cbor_encode_text_stringz(omap, "d");
	cbor_encode_byte_string(omap, buf, len);

	return NBUS_FLASH_RET_OK;
}


/* Estimate usage of the opened flash partition. Each bit of the returned bitmap "b" represents "ba" consecutive erase
 * blocks (block n maps to bit (n / ba) % 8 of byte (n / ba) / 8, LSB first). A bit is 0 only if all its erase blocks
 * are erased. A block is considered erased if its first 32 bit word reads as 0xffffffff. "ba" is chosen as the smallest
 * value which lets the bitmap fit in NBUS_FLASH_BLOCK_LEN bytes. */
static nbus_flash_ret_t process_cc_usage(NbusFlash *self, CborValue *imap, CborEncoder *omap) {
	struct nbus_flash_session *session = session_find(self, imap);
	if (session == NULL) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "session error");
		return NBUS_FLASH_RET_FAILED;
	}
	Flash *flash = session->flash;

	if (flash->vmt->get_size == NULL || flash->vmt->read == NULL) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "not supported");
		return NBUS_FLASH_RET_FAILED;
	}

	/* Level 0 is the whole partition, the last level supporting erase is the smallest erase block. */
	size_t part_size = 0;
	size_t erase_size = 0;
	size_t size = 0;
	flash_block_ops_t ops;
	for (uint32_t i = 0; flash->vmt->get_size(flash, i, &size, &ops) == FLASH_RET_OK; i++) {
		if (i == 0) {
			part_size = size;
		}
		if (ops & FLASH_BLOCK_OPS_ERASE) {
			erase_size = size;
		}
	}
	if (erase_size == 0 || part_size < erase_size) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "bad flash geometry");
		return NBUS_FLASH_RET_FAILED;
	}

	uint32_t blocks = part_size / erase_size;
	uint32_t ba = (blocks + NBUS_FLASH_BLOCK_LEN * 8 - 1) / (NBUS_FLASH_BLOCK_LEN * 8);

	uint8_t bitmap[NBUS_FLASH_BLOCK_LEN] = {0};
	for (uint32_t i = 0; i < blocks; i++) {
		uint32_t bit = i / ba;
		if (bitmap[bit / 8] & (1 << (bit % 8))) {
			/* Already marked as used by another block in the same group, no need to read. */
			continue;
		}
		uint32_t word = 0;
		if (flash->vmt->read(flash, i * erase_size, &word, sizeof(word)) != FLASH_RET_OK) {
			cbor_encode_text_stringz(omap, "err");
			cbor_encode_text_stringz(omap, "reading failed");
			return NBUS_FLASH_RET_FAILED;
		}
		if (word != 0xffffffff) {
			bitmap[bit / 8] |= 1 << (bit % 8);
		}
	}

	cbor_encode_text_stringz(omap, "ba");
	cbor_encode_uint(omap, ba);
	cbor_encode_text_stringz(omap, "b");
	cbor_encode_byte_string(omap, bitmap, ((blocks + ba - 1) / ba + 7) / 8);

	return NBUS_FLASH_RET_OK;
}


static nbus_flash_ret_t process_cc_reset(NbusFlash *self, CborValue *imap, CborEncoder *omap) {
	(void)self;
	(void)imap;
	(void)omap;

	SCB_AIRCR = (SCB_AIRCR_VECTKEY | SCB_AIRCR_SYSRESETREQ);
	while (true) {
		;
	}

	return NBUS_FLASH_RET_OK;
}


static nbus_flash_ret_t process_main_ep(NbusFlash *self, uint8_t *buf, size_t len) {
	CborParser parser;
	CborValue map;

	cbor_parser_init(buf, len, 0, &parser, &map);
	if (cbor_value_is_map(&map)) {
		CborValue cmd;
		cbor_value_map_find_value(&map, "c", &cmd);
		if (cbor_value_is_valid(&cmd) && cbor_value_is_text_string(&cmd)) {
			char s[16];
			size_t slen = 16;
			cbor_value_copy_text_string(&cmd, s, &slen, NULL);

			/* Prepare a top level map for the response. */
			CborEncoder encoder;
			cbor_encoder_init(&encoder, self->tx_buf, NBUS_FLASH_TX_BUF_LEN, 0);
			CborEncoder encoder_map;
			cbor_encoder_create_map(&encoder, &encoder_map, CborIndefiniteLength);

			/* Echo the request identifier so the client can match this reply to its request and reject a late
			 * reply of an earlier request. */
			uint32_t id = 0;
			if (cbor_map_get_uint(&map, "id", &id)) {
				cbor_encode_text_stringz(&encoder_map, "id");
				cbor_encode_uint(&encoder_map, id);
			}

			nbus_flash_ret_t ret = NBUS_FLASH_RET_FAILED;

			if (!strcmp(s, "list")) {
				ret = process_cc_list(self, &map, &encoder_map);
			} else if (!strcmp(s, "info")) {
				ret = process_cc_info(self, &map, &encoder_map);
			} else if (!strcmp(s, "open")) {
				ret = process_cc_open(self, &map, &encoder_map);
			} else if (!strcmp(s, "close")) {
				ret = process_cc_close(self, &map, &encoder_map);
			} else if (!strcmp(s, "erase")) {
				ret = process_cc_erase(self, &map, &encoder_map);
			} else if (!strcmp(s, "write")) {
				ret = process_cc_write(self, &map, &encoder_map);
			} else if (!strcmp(s, "read")) {
				ret = process_cc_read(self, &map, &encoder_map);
			} else if (!strcmp(s, "usage")) {
				ret = process_cc_usage(self, &map, &encoder_map);
			} else if (!strcmp(s, "reset")) {
				ret = process_cc_reset(self, &map, &encoder_map);
			};

			/* Close the container and send the map in all circumstances (even if empty). */
			cbor_encoder_close_container(&encoder, &encoder_map);
			size_t tx_len = cbor_encoder_get_buffer_size(&encoder, self->tx_buf);

			struct datagram_msg txmsg = {0};
			txmsg.addr_size = 4;
			txmsg.dst_port = self->src_port;
			memcpy(&txmsg.dst_addr, &self->src_addr, 4);
			self->d->vmt->write(self->d, self->tx_buf, tx_len, &txmsg);

			return ret;
		}
	}

	/* Top level value is not map or no valid command was found inside. */
	return NBUS_FLASH_RET_FAILED;
}


static void nbus_task(void *p) {
	NbusFlash *self = p;

	while (true) {
		size_t len = NBUS_FLASH_RX_BUF_LEN;
		struct datagram_msg rxmsg = {0};
		if (self->d->vmt->read(self->d, &self->rx_buf, &len, &rxmsg) == DATAGRAM_RET_OK) {
			self->src_port = rxmsg.src_port;
			memcpy(&self->src_addr, &rxmsg.src_addr, 4);
			vTaskDelay(1);
			process_main_ep(self, self->rx_buf, len);
		}
	}

	vTaskDelete(NULL);
}


nbus_flash_ret_t nbus_flash_init(NbusFlash *self, Datagram *d) {
	memset(self, 0, sizeof(NbusFlash));

	self->d = d;

	xTaskCreate(nbus_task, "nbus-flash", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &(self->nbus_task));
	if (self->nbus_task == NULL) {
		return NBUS_FLASH_RET_FAILED;
	}

	return NBUS_FLASH_RET_OK;
}


nbus_flash_ret_t nbus_flash_free(NbusFlash *self) {
	(void)self;
	/** @todo stop the nbus task */
	return NBUS_FLASH_RET_OK;
}



