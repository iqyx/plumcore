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

#include <interfaces/flash.h>

#include <blake2.h>
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


static nbus_flash_ret_t process_cc_info(NbusFlash *self, CborValue *imap, CborEncoder *omap) {
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
/*
	if (self->flash != NULL) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "session already opened, only one supported");
		return NBUS_FLASH_RET_FAILED;
	}
*/
	char dev_name[16];
	cbor_map_get_str(imap, "n", dev_name, sizeof(dev_name));

	Flash *flash = NULL;
	if (iservicelocator_query_name_type(locator, dev_name, ISERVICELOCATOR_TYPE_FLASH, (Interface **)&flash) == ISERVICELOCATOR_RET_OK) {
		self->flash = flash;
		self->sid = (uint32_t)flash;

		cbor_encode_text_stringz(omap, "sid");
		cbor_encode_uint(omap, self->sid);
	} else {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "device not found");
	}

	return NBUS_FLASH_RET_OK;
}


static nbus_flash_ret_t process_cc_close(NbusFlash *self, CborValue *imap, CborEncoder *omap) {
	if (self->flash == NULL) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "no session opened");
		return NBUS_FLASH_RET_FAILED;
	}

	cbor_encode_text_stringz(omap, "sid");
	cbor_encode_uint(omap, self->sid);

	self->flash = NULL;
	self->sid = 0;

	return NBUS_FLASH_RET_OK;
}


static nbus_flash_ret_t process_cc_erase(NbusFlash *self, CborValue *imap, CborEncoder *omap) {
	uint32_t sid;
	if (!cbor_map_get_uint(imap, "sid", &sid) || self->flash == NULL || sid != self->sid) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "session error");
		return NBUS_FLASH_RET_FAILED;
	}

	uint32_t addr;
	uint32_t len;
	if (!cbor_map_get_uint(imap, "addr", &addr) || !cbor_map_get_uint(imap, "len", &len)) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "bad arguments");
		return NBUS_FLASH_RET_FAILED;
	}

	if (self->flash->vmt->erase != NULL && self->flash->vmt->erase(self->flash, addr, len) != FLASH_RET_OK) {
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
	uint32_t sid;
	if (!cbor_map_get_uint(imap, "sid", &sid) || self->flash == NULL || sid != self->sid) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "session error");
		return NBUS_FLASH_RET_FAILED;
	}

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

	if (self->flash->vmt->write != NULL && self->flash->vmt->write(self->flash, addr, buf, len) != FLASH_RET_OK) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "writing failed");
		return NBUS_FLASH_RET_FAILED;
	}
	
	cbor_encode_text_stringz(omap, "ret");
	cbor_encode_text_stringz(omap, "ok");

	return NBUS_FLASH_RET_OK;
}


static nbus_flash_ret_t process_cc_read(NbusFlash *self, CborValue *imap, CborEncoder *omap) {
	uint32_t sid;
	if (!cbor_map_get_uint(imap, "sid", &sid) || self->flash == NULL || sid != self->sid) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "session error");
		return NBUS_FLASH_RET_FAILED;
	}

	uint32_t addr;
	uint32_t len;
	if (!cbor_map_get_uint(imap, "addr", &addr) || !cbor_map_get_uint(imap, "len", &len) || len > NBUS_FLASH_BLOCK_LEN) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "bad arguments");
		return NBUS_FLASH_RET_FAILED;
	}

	uint8_t buf[NBUS_FLASH_BLOCK_LEN];
	if (self->flash->vmt->read != NULL && self->flash->vmt->read(self->flash, addr, buf, len) != FLASH_RET_OK) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "reading failed");
		return NBUS_FLASH_RET_FAILED;
	}
	
	cbor_encode_text_stringz(omap, "d");
	cbor_encode_byte_string(omap, buf, len);

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
			};

			/* Close the container and send the map in all circumstances (even if empty). */
			cbor_encoder_close_container(&encoder, &encoder_map);
			size_t tx_len = cbor_encoder_get_buffer_size(&encoder, self->tx_buf);
			nbus_channel_send(&self->channel, NBUS_FLASH_MAIN_EP, self->tx_buf, tx_len);

			return ret;
		}
	}

	/* Top level value is not map or no valid command was found inside. */
	return NBUS_FLASH_RET_FAILED;
}


static void nbus_task(void *p) {
	NbusFlash *self = p;

	while (true) {
		nbus_endpoint_t ep = 0;
		size_t len = 0;
		nbus_ret_t ret = nbus_channel_receive(&self->channel, &ep, &self->rx_buf, NBUS_FLASH_RX_BUF_LEN, &len, 1000);

		/* Dispatch received messages. */
		if (ret == NBUS_RET_OK && ep == NBUS_FLASH_MAIN_EP) {
			process_main_ep(self, self->rx_buf, len);
		}
	}

	vTaskDelete(NULL);
}


nbus_flash_ret_t nbus_flash_init(NbusFlash *self, NbusChannel *parent, const char *name) {
	memset(self, 0, sizeof(NbusFlash));

	nbus_channel_init(&self->channel, name);
	nbus_channel_set_parent(&self->channel, parent);
	nbus_channel_set_interface(&self->channel, NBUS_FLASH_INTERFACE_NAME, NBUS_FLASH_INTERFACE_VERSION);
	nbus_add_channel(parent->nbus, &self->channel);

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



