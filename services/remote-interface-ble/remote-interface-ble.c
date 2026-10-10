/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * BLE GATT transport for RemoteInterface services
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <main.h>
#include <message_buffer.h>

#include <cbor.h>

#include <interfaces/ble.h>
#include <interfaces/notify.h>
#include <interfaces/remote-interface.h>
#include <interfaces/servicelocator.h>

#include "remote-interface-ble.h"

#define MODULE_NAME "remote-interface-ble"

#if defined(CONFIG_SERVICE_REMOTE_INTERFACE_BLE_DEBUG)
	#define REMOTE_INTERFACE_BLE_DEBUG(...) u_log(system_log, LOG_TYPE_DEBUG, __VA_ARGS__)
#else
	#define REMOTE_INTERFACE_BLE_DEBUG(...) ((void)0)
#endif

/* Last byte of the 128 bit UUID selecting the attribute. */
#define REMOTE_INTERFACE_BLE_UUID_SERVICE 0x00
#define REMOTE_INTERFACE_BLE_UUID_CONTROL 0x01
#define REMOTE_INTERFACE_BLE_UUID_DATA 0x02

/* Largest characteristic value carried by a single write or notification (ATT MTU 247 minus the 3 byte ATT
 * header), and the default ATT MTU used until an exchange completes. */
#define REMOTE_INTERFACE_BLE_VALUE_MAX 244
#define REMOTE_INTERFACE_BLE_DEFAULT_ATT_MTU 23

/* Data fragment header. */
#define REMOTE_INTERFACE_BLE_HDR_START 0x80
#define REMOTE_INTERFACE_BLE_HDR_END 0x40
#define REMOTE_INTERFACE_BLE_HDR_CH_MASK 0x3f

/* Task notification bits. */
#define REMOTE_INTERFACE_BLE_EV_RX 0x01
#define REMOTE_INTERFACE_BLE_EV_TX 0x02
#define REMOTE_INTERFACE_BLE_EV_DISCONNECTED 0x04

#define REMOTE_INTERFACE_BLE_CONTROL_QUEUE_LEN 2
#define REMOTE_INTERFACE_BLE_WRITE_TIMEOUT_MS 100
#define REMOTE_INTERFACE_BLE_STACK_SIZE (configMINIMAL_STACK_SIZE + 768)


/**********************************************************************************************************************
 * Helpers
 **********************************************************************************************************************/

/* Build one of the service's 128 bit UUIDs: the first 15 bytes of sha256("RemoteInterfaceBle") followed by the
 * attribute selector. */
static void make_uuid(struct ble_uuid *uuid, uint8_t attr) {
	static const uint8_t prefix[15] = {
		0xe1, 0x46, 0xbe, 0xcd, 0xab, 0x61, 0xd4, 0x23, 0x65, 0x86, 0x22, 0xa4, 0xb4, 0x35, 0xe7,
	};
	uuid->type = BLE_UUID_128;
	memcpy(uuid->u128, prefix, sizeof(prefix));
	uuid->u128[15] = attr;
}


/* Largest characteristic value the current link carries in a single notification. */
static size_t value_max(RemoteInterfaceBle *self) {
	uint16_t mtu = 0;
	self->conf.ble->vmt->get_mtu(self->conf.ble, &mtu);
	if (mtu == 0) {
		mtu = REMOTE_INTERFACE_BLE_DEFAULT_ATT_MTU;
	}
	size_t max = (size_t)mtu - 3;
	if (max > REMOTE_INTERFACE_BLE_VALUE_MAX) {
		max = REMOTE_INTERFACE_BLE_VALUE_MAX;
	}
	return max;
}


/* Find an advertised RemoteInterface by the name in the CBOR text string @p n. Interfaces advertised without
 * a name are not addressable. */
static RemoteInterface *find_interface(CborValue *n) {
	Interface *iface = NULL;
	for (size_t i = 0; iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_REMOTE_INTERFACE, i, &iface) == ISERVICELOCATOR_RET_OK; i++) {
		const char *name = NULL;
		if (iservicelocator_get_name(locator, iface, &name) != ISERVICELOCATOR_RET_OK || name == NULL) {
			continue;
		}
		bool eq = false;
		if (cbor_value_text_string_equals(n, name, &eq) == CborNoError && eq) {
			return (RemoteInterface *)iface;
		}
	}
	return NULL;
}


/**********************************************************************************************************************
 * Sending to the central
 **********************************************************************************************************************/

/* Send a message to the central on channel @p ch as a sequence of notifications on the data characteristic. An
 * empty message is sent as a single header-only fragment. The message is abandoned if a notification fails. */
static void send_message(RemoteInterfaceBle *self, uint8_t ch, const uint8_t *buf, size_t len) {
	uint8_t frag[REMOTE_INTERFACE_BLE_VALUE_MAX];
	size_t frag_max = value_max(self) - 1;

	REMOTE_INTERFACE_BLE_DEBUG(U_LOG_MODULE_PREFIX("ch %u tx, len %u"), (unsigned)ch, (unsigned)len);

	size_t pos = 0;
	do {
		size_t n = len - pos;
		if (n > frag_max) {
			n = frag_max;
		}
		frag[0] = ch;
		if (pos == 0) {
			frag[0] |= REMOTE_INTERFACE_BLE_HDR_START;
		}
		if (pos + n == len) {
			frag[0] |= REMOTE_INTERFACE_BLE_HDR_END;
		}
		memcpy(&frag[1], &buf[pos], n);
		if (self->data_chr.vmt->notify(&self->data_chr, BLE_CONN_ANY, frag, n + 1) != BLE_RET_OK) {
			REMOTE_INTERFACE_BLE_DEBUG(U_LOG_MODULE_PREFIX("ch %u tx failed"), (unsigned)ch);
			return;
		}
		pos += n;
	} while (pos < len);
}


/* The interface closed the session: tell the central with an empty message and free the interface's session
 * slot right away. The channel stays reserved until the central closes it. */
static void channel_closed(RemoteInterfaceBle *self, uint8_t ch) {
	struct remote_interface_ble_channel *c = &self->channels[ch - 1];

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("ch %u closed by the interface"), (unsigned)ch);
	send_message(self, ch, NULL, 0);
	c->ri->vmt->close(c->ri, c->session);
	c->session = NULL;
}


/* Free a channel, closing its session if still open. */
static void channel_free(RemoteInterfaceBle *self, uint8_t ch) {
	struct remote_interface_ble_channel *c = &self->channels[ch - 1];

	if (c->session != NULL) {
		c->ri->vmt->close(c->ri, c->session);
	}
	memset(c, 0, sizeof(struct remote_interface_ble_channel));
	if (self->rx_active && self->rx_ch == ch) {
		self->rx_active = false;
	}
}


/* Pull messages from all open sessions and send them to the central. One message per channel is taken in each
 * round so a busy channel does not starve the others. */
static void process_tx(RemoteInterfaceBle *self) {
	bool progress = true;
	while (progress) {
		progress = false;
		for (uint8_t ch = 1; ch <= CONFIG_SERVICE_REMOTE_INTERFACE_BLE_CHANNELS; ch++) {
			struct remote_interface_ble_channel *c = &self->channels[ch - 1];
			if (c->session == NULL) {
				continue;
			}
			size_t len = 0;
			remote_interface_ret_t ret = c->ri->vmt->read(c->ri, c->session, self->tx_msg, self->msg_size, &len, 0);
			if (ret == REMOTE_INTERFACE_RET_OK) {
				send_message(self, ch, self->tx_msg, len);
				progress = true;
			} else if (ret == REMOTE_INTERFACE_RET_CLOSED) {
				channel_closed(self, ch);
			}
		}
	}
}


/**********************************************************************************************************************
 * Control commands
 **********************************************************************************************************************/

static void encode_err(CborEncoder *omap, const char *err) {
	cbor_encode_text_stringz(omap, "err");
	cbor_encode_text_stringz(omap, err);
}


static void process_cc_walk(CborValue *imap, CborEncoder *omap) {
	CborValue n;
	cbor_value_map_find_value(imap, "n", &n);
	bool found = !cbor_value_is_valid(&n) || cbor_value_is_null(&n);
	if (!found && !cbor_value_is_text_string(&n)) {
		encode_err(omap, "missing name");
		return;
	}

	/* Single pass: once the requested interface is found, the next named one is returned. */
	Interface *iface = NULL;
	for (size_t i = 0; iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_REMOTE_INTERFACE, i, &iface) == ISERVICELOCATOR_RET_OK; i++) {
		const char *name = NULL;
		if (iservicelocator_get_name(locator, iface, &name) != ISERVICELOCATOR_RET_OK || name == NULL) {
			continue;
		}
		if (found) {
			cbor_encode_text_stringz(omap, "n");
			cbor_encode_text_stringz(omap, name);
			return;
		}
		bool eq = false;
		if (cbor_value_text_string_equals(&n, name, &eq) == CborNoError && eq) {
			found = true;
		}
	}

	if (!found) {
		encode_err(omap, "not found");
		return;
	}
	cbor_encode_text_stringz(omap, "n");
	cbor_encode_null(omap);
}


static void process_cc_desc(CborValue *imap, CborEncoder *omap) {
	CborValue n;
	cbor_value_map_find_value(imap, "n", &n);
	if (!cbor_value_is_valid(&n) || !cbor_value_is_text_string(&n)) {
		encode_err(omap, "missing name");
		return;
	}
	RemoteInterface *ri = find_interface(&n);
	if (ri == NULL) {
		encode_err(omap, "not found");
		return;
	}

	cbor_encode_text_stringz(omap, "p");
	cbor_encode_text_stringz(omap, ri->desc->protocol);
	if (ri->desc->protocol_version != NULL) {
		cbor_encode_text_stringz(omap, "pv");
		cbor_encode_text_stringz(omap, ri->desc->protocol_version);
	}
	cbor_encode_text_stringz(omap, "mtu");
	cbor_encode_uint(omap, ri->desc->mtu);
	cbor_encode_text_stringz(omap, "ms");
	cbor_encode_uint(omap, ri->desc->max_sessions);
}


static void process_cc_open(RemoteInterfaceBle *self, CborValue *imap, CborEncoder *omap) {
	CborValue n;
	cbor_value_map_find_value(imap, "n", &n);
	if (!cbor_value_is_valid(&n) || !cbor_value_is_text_string(&n)) {
		encode_err(omap, "missing name");
		return;
	}
	RemoteInterface *ri = find_interface(&n);
	if (ri == NULL) {
		encode_err(omap, "not found");
		return;
	}

	uint8_t ch = 0;
	for (uint8_t i = 1; i <= CONFIG_SERVICE_REMOTE_INTERFACE_BLE_CHANNELS; i++) {
		if (!self->channels[i - 1].used) {
			ch = i;
			break;
		}
	}
	if (ch == 0) {
		encode_err(omap, "no channel");
		return;
	}

	/* The message buffers were sized at start, an interface advertised later may not fit. */
	if (ri->desc->mtu > self->msg_size) {
		encode_err(omap, "open failed");
		return;
	}

	void *session = NULL;
	remote_interface_ret_t ret = ri->vmt->open(ri, &self->notify, &session, 0);
	if (ret == REMOTE_INTERFACE_RET_TIMEOUT) {
		encode_err(omap, "busy");
		return;
	}
	if (ret != REMOTE_INTERFACE_RET_OK) {
		encode_err(omap, "open failed");
		return;
	}

	self->channels[ch - 1].used = true;
	self->channels[ch - 1].ri = ri;
	self->channels[ch - 1].session = session;
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("ch %u opened, protocol '%s'"), (unsigned)ch,
	      ri->desc->protocol);

	cbor_encode_text_stringz(omap, "ret");
	cbor_encode_text_stringz(omap, "ok");
	cbor_encode_text_stringz(omap, "ch");
	cbor_encode_uint(omap, ch);
}


static void process_cc_close(RemoteInterfaceBle *self, CborValue *imap, CborEncoder *omap) {
	CborValue v;
	cbor_value_map_find_value(imap, "ch", &v);
	if (!cbor_value_is_valid(&v) || !cbor_value_is_unsigned_integer(&v)) {
		encode_err(omap, "missing channel");
		return;
	}
	uint64_t ch = 0;
	cbor_value_get_uint64(&v, &ch);
	if (ch < 1 || ch > CONFIG_SERVICE_REMOTE_INTERFACE_BLE_CHANNELS || !self->channels[ch - 1].used) {
		encode_err(omap, "not found");
		return;
	}

	channel_free(self, (uint8_t)ch);
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("ch %u closed"), (unsigned)ch);

	cbor_encode_text_stringz(omap, "ret");
	cbor_encode_text_stringz(omap, "ok");
}


/* Process a single control request and notify the response on the control characteristic. Malformed requests
 * (not a map, no command) are dropped. */
static void process_control(RemoteInterfaceBle *self, const uint8_t *buf, size_t len) {
	CborParser parser;
	CborValue imap;
	cbor_parser_init(buf, len, 0, &parser, &imap);
	if (!cbor_value_is_map(&imap)) {
		return;
	}

	CborValue cmd_v;
	cbor_value_map_find_value(&imap, "c", &cmd_v);
	if (!cbor_value_is_valid(&cmd_v) || !cbor_value_is_text_string(&cmd_v)) {
		return;
	}

	char cmd[8];
	size_t cmd_len = sizeof(cmd);
	if (cbor_value_copy_text_string(&cmd_v, cmd, &cmd_len, NULL) != CborNoError) {
		cmd[0] = '\0';
	}
	REMOTE_INTERFACE_BLE_DEBUG(U_LOG_MODULE_PREFIX("control '%s'"), cmd);

	uint8_t resp[REMOTE_INTERFACE_BLE_VALUE_MAX];
	CborEncoder encoder;
	CborEncoder omap;
	cbor_encoder_init(&encoder, resp, value_max(self), 0);
	cbor_encoder_create_map(&encoder, &omap, CborIndefiniteLength);

	if (!strcmp(cmd, "walk")) {
		process_cc_walk(&imap, &omap);
	} else if (!strcmp(cmd, "desc")) {
		process_cc_desc(&imap, &omap);
	} else if (!strcmp(cmd, "open")) {
		process_cc_open(self, &imap, &omap);
	} else if (!strcmp(cmd, "close")) {
		process_cc_close(self, &imap, &omap);
	} else {
		encode_err(&omap, "not supported");
	}

	if (cbor_encoder_close_container(&encoder, &omap) != CborNoError) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("control response does not fit the ATT MTU"));
		return;
	}
	size_t resp_len = cbor_encoder_get_buffer_size(&encoder, resp);
	self->control_chr.vmt->notify(&self->control_chr, BLE_CONN_ANY, resp, resp_len);
}


/**********************************************************************************************************************
 * Receiving from the central
 **********************************************************************************************************************/

/* Reassemble a data fragment. A complete message is pushed to the channel's session. Fragments not continuing
 * the message being reassembled, messages bigger than the buffer and messages for a channel without an open
 * session are dropped. */
static void process_data(RemoteInterfaceBle *self, const uint8_t *frag, size_t len) {
	if (len == 0) {
		return;
	}
	uint8_t ch = frag[0] & REMOTE_INTERFACE_BLE_HDR_CH_MASK;

	if (frag[0] & REMOTE_INTERFACE_BLE_HDR_START) {
		self->rx_active = true;
		self->rx_ch = ch;
		self->rx_len = 0;
	} else if (!self->rx_active || self->rx_ch != ch) {
		return;
	}

	if (self->rx_len + len - 1 > self->msg_size) {
		self->rx_active = false;
		return;
	}
	memcpy(&self->rx_msg[self->rx_len], &frag[1], len - 1);
	self->rx_len += len - 1;

	if (!(frag[0] & REMOTE_INTERFACE_BLE_HDR_END)) {
		return;
	}
	self->rx_active = false;

	REMOTE_INTERFACE_BLE_DEBUG(U_LOG_MODULE_PREFIX("ch %u rx, len %u"), (unsigned)ch, (unsigned)self->rx_len);

	if (ch < 1 || ch > CONFIG_SERVICE_REMOTE_INTERFACE_BLE_CHANNELS || self->rx_len == 0) {
		return;
	}
	struct remote_interface_ble_channel *c = &self->channels[ch - 1];
	if (c->session == NULL) {
		return;
	}
	remote_interface_ret_t ret = c->ri->vmt->write(c->ri, c->session, self->rx_msg, self->rx_len,
	                                               REMOTE_INTERFACE_BLE_WRITE_TIMEOUT_MS);
	if (ret == REMOTE_INTERFACE_RET_CLOSED) {
		/* Send the messages still queued before the closing indication. */
		process_tx(self);
		if (c->session != NULL) {
			channel_closed(self, ch);
		}
	}
}


/* Drain everything written by the central. Kept separate from the control processing so the fragment buffer
 * and the control buffers are not on the stack at the same time. */
static void process_data_rx(RemoteInterfaceBle *self) {
	uint8_t frag[REMOTE_INTERFACE_BLE_VALUE_MAX];
	size_t len = 0;
	while ((len = xMessageBufferReceive(self->data_rx, frag, sizeof(frag), 0)) > 0) {
		process_data(self, frag, len);
	}
}


static void process_control_rx(RemoteInterfaceBle *self) {
	uint8_t req[REMOTE_INTERFACE_BLE_VALUE_MAX];
	size_t len = 0;
	while ((len = xMessageBufferReceive(self->control_rx, req, sizeof(req), 0)) > 0) {
		process_control(self, req, len);
	}
}


/* Route peer writes to the task, then forward every event to the caller's callback. Runs in the driver's
 * receive task, so it only enqueues and must not call back into blocking driver methods. */
static void event_handler(void *ctx, const struct ble_event *event) {
	RemoteInterfaceBle *self = ctx;

	if (event->type == BLE_EVENT_WRITE && event->len > 0) {
		if (event->chr == &self->control_chr) {
			xMessageBufferSend(self->control_rx, event->data, event->len, 0);
			xTaskNotify(self->task, REMOTE_INTERFACE_BLE_EV_RX, eSetBits);
		} else if (event->chr == &self->data_chr) {
			xMessageBufferSend(self->data_rx, event->data, event->len, 0);
			xTaskNotify(self->task, REMOTE_INTERFACE_BLE_EV_RX, eSetBits);
		}
	} else if (event->type == BLE_EVENT_DISCONNECTED) {
		xTaskNotify(self->task, REMOTE_INTERFACE_BLE_EV_DISCONNECTED, eSetBits);
	}

	if (self->conf.event_cb != NULL) {
		self->conf.event_cb(self->conf.event_cb_ctx, event);
	}
}


static notify_ret_t notify_notify(Notify *notify, uint32_t value) {
	(void)value;
	RemoteInterfaceBle *self = notify->parent;
	xTaskNotify(self->task, REMOTE_INTERFACE_BLE_EV_TX, eSetBits);
	return NOTIFY_RET_OK;
}


static const struct notify_vmt remote_interface_ble_notify_vmt = {
	.notify = notify_notify,
};


static void remote_interface_ble_task(void *p) {
	RemoteInterfaceBle *self = p;

	while (true) {
		uint32_t events = 0;
		xTaskNotifyWait(0, UINT32_MAX, &events, portMAX_DELAY);

		/* Writes queued before a disconnect are processed first, they belong to the lost connection. */
		process_control_rx(self);
		process_data_rx(self);

		if (events & REMOTE_INTERFACE_BLE_EV_DISCONNECTED) {
			for (uint8_t ch = 1; ch <= CONFIG_SERVICE_REMOTE_INTERFACE_BLE_CHANNELS; ch++) {
				if (self->channels[ch - 1].used) {
					channel_free(self, ch);
				}
			}
			self->rx_active = false;
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("disconnected, all channels closed"));
		}

		process_tx(self);
	}
}


/**********************************************************************************************************************
 * Public API
 **********************************************************************************************************************/

remote_interface_ble_ret_t remote_interface_ble_init(RemoteInterfaceBle *self, const struct remote_interface_ble_conf *conf) {
	if (u_assert(self != NULL) ||
	    u_assert(conf != NULL)) {
		return REMOTE_INTERFACE_BLE_RET_NULL;
	}
	memset(self, 0, sizeof(RemoteInterfaceBle));
	memcpy(&self->conf, conf, sizeof(struct remote_interface_ble_conf));

	return REMOTE_INTERFACE_BLE_RET_OK;
}


remote_interface_ble_ret_t remote_interface_ble_start(RemoteInterfaceBle *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->conf.ble != NULL)) {
		return REMOTE_INTERFACE_BLE_RET_NULL;
	}

	/* Size the message buffers to the largest interface advertised so far. */
	Interface *iface = NULL;
	for (size_t i = 0; iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_REMOTE_INTERFACE, i, &iface) == ISERVICELOCATOR_RET_OK; i++) {
		RemoteInterface *ri = (RemoteInterface *)iface;
		if (ri->desc->mtu > self->msg_size) {
			self->msg_size = ri->desc->mtu;
		}
	}
	if (self->msg_size == 0) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("no RemoteInterface advertised"));
		return REMOTE_INTERFACE_BLE_RET_FAILED;
	}

	/* Each message is stored along with its length. */
	size_t value_size = REMOTE_INTERFACE_BLE_VALUE_MAX + sizeof(configMESSAGE_BUFFER_LENGTH_TYPE);
	self->control_rx = xMessageBufferCreate(REMOTE_INTERFACE_BLE_CONTROL_QUEUE_LEN * value_size);
	self->data_rx = xMessageBufferCreate(CONFIG_SERVICE_REMOTE_INTERFACE_BLE_RX_QUEUE_LEN * value_size);
	self->rx_msg = malloc(self->msg_size);
	self->tx_msg = malloc(self->msg_size);
	if (self->control_rx == NULL || self->data_rx == NULL || self->rx_msg == NULL || self->tx_msg == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot allocate resources"));
		goto err;
	}

	self->notify.vmt = &remote_interface_ble_notify_vmt;
	self->notify.parent = self;

	/* Both characteristics are writable, the ST67W611 read-only characteristic ordering quirk does not apply.
	 * Read and write-with-response mirror the profile known to work with proto-dgble. */
	struct ble_uuid uuid;
	make_uuid(&uuid, REMOTE_INTERFACE_BLE_UUID_SERVICE);
	if (self->conf.ble->vmt->add_service(self->conf.ble, &uuid, &self->srv) != BLE_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create the GATT service"));
		goto err;
	}
	uint32_t props = BLE_CHAR_PROP_READ | BLE_CHAR_PROP_WRITE | BLE_CHAR_PROP_NOTIFY;
	make_uuid(&uuid, REMOTE_INTERFACE_BLE_UUID_CONTROL);
	if (self->srv.vmt->add_characteristic(&self->srv, &uuid, props, &self->control_chr) != BLE_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create the control characteristic"));
		goto err;
	}
	make_uuid(&uuid, REMOTE_INTERFACE_BLE_UUID_DATA);
	if (self->srv.vmt->add_characteristic(&self->srv, &uuid, props, &self->data_chr) != BLE_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create the data characteristic"));
		goto err;
	}

	/* The task must exist before the event handler starts notifying it. */
	xTaskCreate(remote_interface_ble_task, "ri-ble", REMOTE_INTERFACE_BLE_STACK_SIZE, (void *)self, 1, &self->task);
	if (self->task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start the task"));
		goto err;
	}
	self->conf.ble->vmt->set_event_handler(self->conf.ble, event_handler, self);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("service started, max message %u"), (unsigned)self->msg_size);
	return REMOTE_INTERFACE_BLE_RET_OK;
err:
	remote_interface_ble_free(self);
	return REMOTE_INTERFACE_BLE_RET_FAILED;
}


remote_interface_ble_ret_t remote_interface_ble_free(RemoteInterfaceBle *self) {
	if (u_assert(self != NULL)) {
		return REMOTE_INTERFACE_BLE_RET_NULL;
	}

	if (self->task != NULL) {
		self->conf.ble->vmt->set_event_handler(self->conf.ble, NULL, NULL);
		vTaskDelete(self->task);
		self->task = NULL;
	}
	for (uint8_t ch = 1; ch <= CONFIG_SERVICE_REMOTE_INTERFACE_BLE_CHANNELS; ch++) {
		if (self->channels[ch - 1].used) {
			channel_free(self, ch);
		}
	}
	if (self->control_rx != NULL) {
		vMessageBufferDelete(self->control_rx);
		self->control_rx = NULL;
	}
	if (self->data_rx != NULL) {
		vMessageBufferDelete(self->data_rx);
		self->data_rx = NULL;
	}
	free(self->rx_msg);
	self->rx_msg = NULL;
	free(self->tx_msg);
	self->tx_msg = NULL;

	return REMOTE_INTERFACE_BLE_RET_OK;
}
