/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Tunnelling of datagrams over a BLE GATT service
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include <main.h>
#include <message_buffer.h>

#include <cbor.h>

#include <interfaces/ble.h>
#include <interfaces/datagram.h>

#include "proto-dgble.h"

#define MODULE_NAME "proto-dgble"

/* Debug logging, compiled out to a no-op unless SERVICE_PROTO_DGBLE_DEBUG is enabled. */
#if defined(CONFIG_SERVICE_PROTO_DGBLE_DEBUG)
	#define PROTO_DGBLE_DEBUG(...) u_log(system_log, LOG_TYPE_DEBUG, __VA_ARGS__)
#else
	#define PROTO_DGBLE_DEBUG(...) ((void)0)
#endif

/** Version of the descriptor characteristic layout emitted as the "adv" key. */
#define PROTO_DGBLE_DESC_VERSION 1

/* Byte 11 of the 128 bit UUID selecting the attribute group within the vendor prefix. */
#define PROTO_DGBLE_GROUP_SERVICE 0x90
#define PROTO_DGBLE_GROUP_DESCRIPTOR 0x91
#define PROTO_DGBLE_GROUP_ENDPOINT 0x92

/* Storage of one inbound datagram in a message buffer costs its payload plus a small length header. */
#define PROTO_DGBLE_RX_BUF_SIZE (CONFIG_SERVICE_PROTO_DGBLE_RX_QUEUE_LEN * \
                                 (CONFIG_SERVICE_PROTO_DGBLE_MAX_DATAGRAM + sizeof(size_t)))


/**********************************************************************************************************************
 * Helpers
 **********************************************************************************************************************/

/* Build one of the service's 128 bit UUIDs from the shared vendor prefix, the attribute group selector
 * and a 32 bit tail (the service ID, zero for the descriptor, or the endpoint number). */
static void proto_dgble_make_uuid(struct ble_uuid *uuid, uint8_t group, uint32_t tail) {
	static const uint8_t prefix[11] = {
		0xa3, 0xde, 0xf8, 0xc0, 0xac, 0xdd, 0xa6, 0x02, 0xe3, 0xa1, 0x78,
	};
	uuid->type = BLE_UUID_128;
	memcpy(uuid->u128, prefix, sizeof(prefix));
	uuid->u128[11] = group;
	uuid->u128[12] = (uint8_t)(tail >> 24);
	uuid->u128[13] = (uint8_t)(tail >> 16);
	uuid->u128[14] = (uint8_t)(tail >> 8);
	uuid->u128[15] = (uint8_t)(tail);
}


/* Route an inbound peer write to the owning endpoint's receive buffer, then forward every event to the
 * caller's callback. Runs in the driver's receive task, so it only enqueues and must not call back into
 * blocking driver methods. */
static void proto_dgble_event_handler(void *ctx, const struct ble_event *event) {
	ProtoDgble *self = ctx;

	if (event->type == BLE_EVENT_WRITE && event->chr != NULL && event->len > 0) {
		for (size_t i = 0; i < CONFIG_SERVICE_PROTO_DGBLE_MAX_ENDPOINTS; i++) {
			struct proto_dgble_endpoint *ep = &self->endpoints[i];
			if (ep->used && event->chr == &ep->chr) {
				PROTO_DGBLE_DEBUG(U_LOG_MODULE_PREFIX("ep %u rx, len %u"),
					(unsigned)ep->number, (unsigned)event->len);
				xMessageBufferSend(ep->rx_buf, event->data, event->len, 0);
				break;
			}
		}
	}

	if (self->user_event_cb != NULL) {
		self->user_event_cb(self->user_event_cb_ctx, event);
	}
}


/* Encode the descriptor CBOR map (service ID and the registered endpoints) and publish it as the
 * descriptor characteristic's value. */
static proto_dgble_ret_t proto_dgble_publish_descriptor(ProtoDgble *self) {
	uint8_t buf[CONFIG_SERVICE_PROTO_DGBLE_MAX_DATAGRAM];

	CborEncoder encoder;
	CborEncoder map;
	CborEncoder eps;
	cbor_encoder_init(&encoder, buf, sizeof(buf), 0);
	cbor_encoder_create_map(&encoder, &map, CborIndefiniteLength);
	cbor_encode_text_stringz(&map, "adv");
	cbor_encode_int(&map, PROTO_DGBLE_DESC_VERSION);
	cbor_encode_text_stringz(&map, "sid");
	cbor_encode_uint(&map, self->service_id);
	cbor_encode_text_stringz(&map, "eps");
	cbor_encoder_create_map(&map, &eps, CborIndefiniteLength);
	for (size_t i = 0; i < CONFIG_SERVICE_PROTO_DGBLE_MAX_ENDPOINTS; i++) {
		struct proto_dgble_endpoint *ep = &self->endpoints[i];
		if (!ep->used) {
			continue;
		}
		CborEncoder epmap;
		cbor_encode_uint(&eps, ep->number);
		cbor_encoder_create_map(&eps, &epmap, CborIndefiniteLength);
		if (ep->protocol != NULL) {
			cbor_encode_text_stringz(&epmap, "p");
			cbor_encode_text_stringz(&epmap, ep->protocol);
		}
		if (ep->protocol_version != NULL) {
			cbor_encode_text_stringz(&epmap, "pv");
			cbor_encode_text_stringz(&epmap, ep->protocol_version);
		}
		cbor_encoder_close_container(&eps, &epmap);
	}
	cbor_encoder_close_container(&map, &eps);
	cbor_encoder_close_container(&encoder, &map);

	size_t len = cbor_encoder_get_buffer_size(&encoder, buf);
	if (self->descriptor_chr.vmt->set_value(&self->descriptor_chr, buf, len) != BLE_RET_OK) {
		return PROTO_DGBLE_RET_FAILED;
	}
	return PROTO_DGBLE_RET_OK;
}


/**********************************************************************************************************************
 * Datagram interface API
 **********************************************************************************************************************/

static datagram_ret_t dgram_write(Datagram *self, const void *buf, size_t len, const struct datagram_msg *msg) {
	(void)msg;
	if (u_assert(self != NULL) ||
	    u_assert(buf != NULL)) {
		return DATAGRAM_RET_BAD_ARG;
	}
	struct proto_dgble_endpoint *ep = self->parent;

	if (len == 0 || len > CONFIG_SERVICE_PROTO_DGBLE_MAX_DATAGRAM) {
		return DATAGRAM_RET_BAD_ARG;
	}

	PROTO_DGBLE_DEBUG(U_LOG_MODULE_PREFIX("ep %u tx, len %u"),
		(unsigned)ep->number, (unsigned)len);

	/* Push the datagram to the connected peer as a notification on the endpoint's characteristic. */
	if (ep->chr.vmt->notify(&ep->chr, BLE_CONN_ANY, buf, len) != BLE_RET_OK) {
		return DATAGRAM_RET_FAILED;
	}
	return DATAGRAM_RET_OK;
}


static datagram_ret_t dgram_read(Datagram *self, void *buf, size_t *len, struct datagram_msg *msg) {
	if (u_assert(self != NULL) ||
	    u_assert(buf != NULL) ||
	    u_assert(len != NULL)) {
		return DATAGRAM_RET_BAD_ARG;
	}
	struct proto_dgble_endpoint *ep = self->parent;

	if (msg != NULL) {
		memset(msg, 0, sizeof(struct datagram_msg));
	}

	/* Block until a peer write delivers a whole datagram. A too-small caller buffer would make the
	 * message buffer return 0 and leave the datagram queued; the caller must provide at least
	 * CONFIG_SERVICE_PROTO_DGBLE_MAX_DATAGRAM bytes. */
	size_t got = xMessageBufferReceive(ep->rx_buf, buf, *len, portMAX_DELAY);
	if (got == 0) {
		return DATAGRAM_RET_FAILED;
	}
	*len = got;
	return DATAGRAM_RET_OK;
}


static const struct datagram_vmt proto_dgble_datagram_vmt = {
	.write = dgram_write,
	.read = dgram_read,
};


/**********************************************************************************************************************
 * Public API
 **********************************************************************************************************************/

proto_dgble_ret_t proto_dgble_init(ProtoDgble *self, const struct proto_dgble_config *config) {
	if (u_assert(self != NULL) ||
	    u_assert(config != NULL) ||
	    u_assert(config->ble != NULL)) {
		return PROTO_DGBLE_RET_BAD_ARG;
	}
	memset(self, 0, sizeof(ProtoDgble));

	self->ble = config->ble;
	self->service_id = config->service_id;
	self->user_event_cb = config->event_cb;
	self->user_event_cb_ctx = config->event_cb_ctx;

	/* Take over the device's single event callback so inbound peer writes can be routed to endpoints. */
	self->ble->vmt->set_event_handler(self->ble, proto_dgble_event_handler, self);

	/* Create the primary service (96 bit prefix + 32 bit service ID). The descriptor characteristic is not
	 * created here but in proto_dgble_start(), after all endpoints have been added, so it is the last
	 * characteristic in the service: the ST67W611 GATT server fails to route write events to
	 * characteristics that follow a read-only one, so the read-only descriptor must come last. */
	struct ble_uuid srv_uuid;
	proto_dgble_make_uuid(&srv_uuid, PROTO_DGBLE_GROUP_SERVICE, self->service_id);
	if (self->ble->vmt->add_service(self->ble, &srv_uuid, &self->srv) != BLE_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create the GATT service"));
		return PROTO_DGBLE_RET_FAILED;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("service started, service ID = 0x%08lx"),
		(unsigned long)self->service_id);
	return PROTO_DGBLE_RET_OK;
}


proto_dgble_ret_t proto_dgble_free(ProtoDgble *self) {
	if (u_assert(self != NULL)) {
		return PROTO_DGBLE_RET_BAD_ARG;
	}

	for (size_t i = 0; i < CONFIG_SERVICE_PROTO_DGBLE_MAX_ENDPOINTS; i++) {
		if (self->endpoints[i].rx_buf != NULL) {
			vMessageBufferDelete(self->endpoints[i].rx_buf);
			self->endpoints[i].rx_buf = NULL;
		}
	}

	return PROTO_DGBLE_RET_OK;
}


proto_dgble_ret_t proto_dgble_add_endpoint(ProtoDgble *self, uint16_t number,
                                           const struct proto_dgble_descriptor *descriptor, Datagram **dgram) {
	if (u_assert(self != NULL) ||
	    u_assert(dgram != NULL)) {
		return PROTO_DGBLE_RET_BAD_ARG;
	}

	/* Find a free slot and reject a duplicate endpoint number. */
	struct proto_dgble_endpoint *ep = NULL;
	for (size_t i = 0; i < CONFIG_SERVICE_PROTO_DGBLE_MAX_ENDPOINTS; i++) {
		if (self->endpoints[i].used && self->endpoints[i].number == number) {
			return PROTO_DGBLE_RET_BAD_ARG;
		}
		if (ep == NULL && !self->endpoints[i].used) {
			ep = &self->endpoints[i];
		}
	}
	if (ep == NULL) {
		return PROTO_DGBLE_RET_NOMEM;
	}

	ep->rx_buf = xMessageBufferCreate(PROTO_DGBLE_RX_BUF_SIZE);
	if (ep->rx_buf == NULL) {
		return PROTO_DGBLE_RET_NOMEM;
	}

	/* Add a readable, writable, notifiable characteristic carrying the endpoint number in the low 16 bits
	 * of its UUID. Read and write-with-response mirror a known-good characteristic profile; some GATT
	 * server firmwares do not surface events for write-without-response characteristics. */
	struct ble_uuid ep_uuid;
	proto_dgble_make_uuid(&ep_uuid, PROTO_DGBLE_GROUP_ENDPOINT, number);
	if (self->srv.vmt->add_characteristic(&self->srv, &ep_uuid,
	                                      BLE_CHAR_PROP_READ | BLE_CHAR_PROP_WRITE | BLE_CHAR_PROP_NOTIFY,
	                                      &ep->chr) != BLE_RET_OK) {
		vMessageBufferDelete(ep->rx_buf);
		ep->rx_buf = NULL;
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create the endpoint %u characteristic"),
			(unsigned)number);
		return PROTO_DGBLE_RET_FAILED;
	}

	ep->parent = self;
	ep->number = number;
	ep->protocol = (descriptor != NULL) ? descriptor->protocol : NULL;
	ep->protocol_version = (descriptor != NULL) ? descriptor->protocol_version : NULL;
	ep->dgram.parent = ep;
	ep->dgram.vmt = &proto_dgble_datagram_vmt;
	ep->used = true;

	*dgram = &ep->dgram;
	return PROTO_DGBLE_RET_OK;
}


proto_dgble_ret_t proto_dgble_start(ProtoDgble *self) {
	if (u_assert(self != NULL)) {
		return PROTO_DGBLE_RET_BAD_ARG;
	}

	/* Create the descriptor characteristic now, as the last characteristic in the service (see the note in
	 * proto_dgble_init). Then publish its value. */
	struct ble_uuid desc_uuid;
	proto_dgble_make_uuid(&desc_uuid, PROTO_DGBLE_GROUP_DESCRIPTOR, 0);
	if (self->srv.vmt->add_characteristic(&self->srv, &desc_uuid, BLE_CHAR_PROP_READ,
	                                      &self->descriptor_chr) != BLE_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create the descriptor characteristic"));
		return PROTO_DGBLE_RET_FAILED;
	}

	return proto_dgble_publish_descriptor(self);
}
