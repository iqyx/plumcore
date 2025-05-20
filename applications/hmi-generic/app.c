/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Generic HMI implementation
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <main.h>
#include <interfaces/event.h>
#include "cbor.h"
#include "app.h"

#define MODULE_NAME "hmi"


static void com_task(void *p) {
	App *self = p;

	while (true) {

		struct datagram_msg rxmsg = {0};
		size_t len = PACKET_BUFFER_SIZE;
		if (self->socket->datagram.vmt->read(&self->socket->datagram, self->packet_buffer, &len, &rxmsg) == DATAGRAM_RET_OK) {
			/* COnnect the socket to the remote device once the display is accessed.
			 * It is needed to send input events back. */
			nbus_socket_connect(self->socket, rxmsg.src_addr, rxmsg.src_port);

			CborParser parser;
			CborValue parser_map;
			CborValue val;

			if (cbor_parser_init(self->packet_buffer, len, 0, &parser, &parser_map) != CborNoError || !cbor_value_is_map(&parser_map)) {
				/* No valid CBOR and the top level structure is not a map. */
				continue;
			}

			/* Find and get command. */
			char command[16];
			size_t command_len = 0;
			if (cbor_value_map_find_value(&parser_map, "c", &val) == CborNoError && cbor_value_is_text_string(&val)) {
				command_len = sizeof(command);
				cbor_value_copy_text_string(&val, command, &command_len, NULL);
			}

			/* Find and get update bitmap as a byte string. */
			uint8_t bitmap[20];
			size_t bitmap_len = 0;
			if (cbor_value_map_find_value(&parser_map, "b", &val) == CborNoError && cbor_value_is_byte_string(&val)) {
				bitmap_len = sizeof(bitmap);
				cbor_value_copy_byte_string(&val, bitmap, &bitmap_len, NULL);
			}

			/* Find and get update buffer as a byte string. */
			size_t update_len = UPDATE_BUFFER_SIZE;
			if (cbor_value_map_find_value(&parser_map, "d", &val) == CborNoError && cbor_value_is_byte_string(&val)) {
				update_len = UPDATE_BUFFER_SIZE;
				cbor_value_copy_byte_string(&val, self->update_buffer, &update_len, NULL);
			}

			/* PoC of framebuffer update. */
			const uint32_t w = 240;
			const uint32_t h = 160;
			size_t chunk_offset = 0;
			for (size_t y = 0; y < (h / 16); y++) {
				for (size_t x = 0; x < (w / 16); x++) {
					/* Check if this chunk needs updating. */
					size_t chunk_id = y * (w / 16) + x;
					if (bitmap[bitmap_len - 1 - (chunk_id / 8)] & (1 << (chunk_id % 8))) {
						for (size_t cy = 0; cy < 16; cy++) {
							self->fb->vmt->write(
								self->fb,
								/* Calculate position within the framebuffer to update. */
								(y * 16 + cy) * (w / 4) + (x * 16) / 4,
								self->update_buffer + chunk_offset,
								4, /* Line of chunk in G2 mode is always 4 bytes long. */
								FB_MODE_G2
							);
							chunk_offset += 4;
						}
					}
				}
			}
			self->fb->vmt->flush(self->fb);



/*
			uint32_t offset = packet_buffer[0] << 24 | packet_buffer[1] << 16 | packet_buffer[2] << 8 | packet_buffer[3];
			len -= 4;

			if ((offset + len) > 9600) {
				continue;
			}
			self->fb->vmt->write(self->fb, offset, packet_buffer + 4, len, FB_MODE_G2);

			if ((offset + len) == 9600) {
				self->fb->vmt->flush(self->fb);
			}
*/
		}

	}
	vTaskDelete(NULL);
}


static void input_task(void *p) {
	App *self = p;

	while (true) {
		enum event_type type = EV_TYPE_NONE;
		enum event_code code = EV_CODE_NONE;
		int32_t value = 0;

		if (self->input->vmt->listen(self->input, &type, &code, &value) == EV_RET_OK) {
			if (value == 1) {
				/* On key down. */
				self->speaker->vmt->start(self->speaker);

				uint8_t response[64];
				CborEncoder encoder;
				CborEncoder encoder_map;
				cbor_encoder_init(&encoder, response, sizeof(response), 0);
				cbor_encoder_create_map(&encoder, &encoder_map, CborIndefiniteLength);

				cbor_encode_text_stringz(&encoder_map, "c");
				cbor_encode_text_stringz(&encoder_map, "event");

				cbor_encode_text_stringz(&encoder_map, "type");
				cbor_encode_int(&encoder_map, type);
				cbor_encode_text_stringz(&encoder_map, "code");
				cbor_encode_int(&encoder_map, code);
				cbor_encode_text_stringz(&encoder_map, "value");
				cbor_encode_int(&encoder_map, value);

				cbor_encoder_close_container(&encoder, &encoder_map);

				size_t response_len = cbor_encoder_get_buffer_size(&encoder, response);
				self->socket->datagram.vmt->write(&self->socket->datagram, response, response_len, NULL);
			}
		}
	}
	vTaskDelete(NULL);
}


static void reader_task(void *p) {
	App *self = p;

	while (true) {
		uint8_t buf[8];
		size_t read = 0;
		if (self->reader->vmt->read(self->reader, buf, sizeof(buf), &read) == STREAM_RET_OK) {
			u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("reader: read %u bytes"), read);
		}
	}
	vTaskDelete(NULL);
}


app_ret_t app_init(App *self) {
	memset(self, 0, sizeof(App));

	self->fb = NULL;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_FB, 0, (Interface **)&self->fb) != ISERVICELOCATOR_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("no framebuffer device found"));
		return APP_RET_FAILED;
	}

	/* Get any i2c available. Allow configuration in the future (along with input devices). */
	self->i2c = NULL;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_I2C, 0, (Interface **)&self->i2c) != ISERVICELOCATOR_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("no suitable i2c device found"));
		return APP_RET_FAILED;
	}

	/** @todo configure which input device to use.  */
	self->input = NULL;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_EVENT, 0, (Interface **)&self->input) != ISERVICELOCATOR_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("no suitable input device found"));
		return APP_RET_FAILED;
	}

	self->speaker = NULL;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_WAVEFORM_SINK, 0, (Interface **)&self->speaker) != ISERVICELOCATOR_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("no suitable speaker found"));
		return APP_RET_FAILED;
	}

	/* Create nbus2 API */
	self->socket = nbus_socket_allocate(&nbus);
	uint8_t local_id[] = {0x00, 0x00, 0x00, 0x10};
	nbus_socket_bind(self->socket, local_id, 1);


	xTaskCreate(com_task, "hmi-com", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &(self->com_task));
	if (self->com_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create application com task"));
		goto err;
	}

	xTaskCreate(input_task, "hmi-input", configMINIMAL_STACK_SIZE + 128, (void *)self, 1, &(self->input_task));
	if (self->input_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create application input task"));
		goto err;
	}

	/* Discover optional barcode reader and start its task. */
	self->reader = NULL;
	if (iservicelocator_query_name_type(locator, "reader", ISERVICELOCATOR_TYPE_STREAM, (Interface **)&self->reader) == ISERVICELOCATOR_RET_OK) {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("barcode reader found"));

		xTaskCreate(reader_task, "hmi-reader", configMINIMAL_STACK_SIZE + 128, (void *)self, 1, &(self->reader_task));
		if (self->reader_task == NULL) {
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create application reader task"));
			goto err;
		}
	}



	return APP_RET_OK;
err:
	return APP_RET_FAILED;
}


app_ret_t app_free(App *self) {
	nbus_socket_release(&nbus, self->socket);
	return APP_RET_OK;
}
