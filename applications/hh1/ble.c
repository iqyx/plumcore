/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * nwdaq-main-hh1 application BLE GATT server
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include <main.h>

#include <interfaces/servicelocator.h>
#include "app.h"

#define MODULE_NAME "hh1-ble"

/* Padding from the window edges and the height of a line of instruction text, in pixels. */
#define APP_BLE_PAIR_PAD 6
#define APP_BLE_PAIR_LINE_H 11


/* Repaint the pairing dialog window: a black background, the instruction text at the top and the current
 * passkey drawn large and centred below it. Does nothing when the window was never created. */
static void app_ble_pair_redraw(App *self) {
	if (self->ble_pair_window == NULL) {
		return;
	}

	Fb *fb = NULL;
	self->ble_pair_window->vmt->get_fb(self->ble_pair_window, &fb);
	struct fb_stat stat = {0};
	if (fb == NULL || fb->vmt->stat(fb, &stat) != FB_RET_OK) {
		return;
	}
	uint16_t w = (uint16_t)stat.w;
	uint16_t h = (uint16_t)stat.h;

	Painter *painter = &self->ble_pair_painter.painter;
	painter->vmt->begin(painter);

	/* Clear the whole window to a black background. */
	painter->vmt->set_pen(painter, 0xff000000, 1);
	painter->vmt->set_brush(painter, 0xff000000);
	painter->vmt->rect(painter, 0, 0, w, h);

	/* Instruction text, in white, wrapped onto two short lines to fit the narrow display. */
	painter->vmt->set_pen(painter, 0xffffffff, 1);
	painter->vmt->set_font(painter, PAINTER_FONT_NORMAL, NULL);
	painter->vmt->text(painter, APP_BLE_PAIR_PAD, APP_BLE_PAIR_PAD, "Enter this code on");
	painter->vmt->text(painter, APP_BLE_PAIR_PAD, APP_BLE_PAIR_PAD + APP_BLE_PAIR_LINE_H, "the client device:");

	/* Passkey in the bold font, centred in the area below the instruction text. */
	painter->vmt->set_font(painter, PAINTER_FONT_BOLD, NULL);
	uint16_t tw = 0;
	uint16_t th = 0;
	painter->vmt->text_size(painter, self->ble_passkey, &tw, &th);
	int16_t code_top = (int16_t)(APP_BLE_PAIR_PAD + 2 * APP_BLE_PAIR_LINE_H);
	int16_t code_x = (int16_t)((w - tw) / 2);
	int16_t code_y = (int16_t)(code_top + (h - code_top - th) / 2);
	painter->vmt->text(painter, code_x, code_y, self->ble_passkey);

	painter->vmt->end(painter);
}


/* Create the hidden pairing dialog window on the compositor and bind its painter. The window covers the
 * content area below the status bar and stays hidden until a passkey needs to be shown. Requires the GUI
 * window manager (and thus its compositor) to be up; a headless build simply gets no dialog. */
static void app_ble_pair_window_init(App *self) {
	if (!self->gui.compositor_up) {
		return;
	}

	const struct window_geometry geom = {
		.x = 0,
		.y = GUI_WM_FS_TOPBAR_H,
		.w = (uint16_t)self->gui.compositor.out_w,
		.h = (uint16_t)(self->gui.compositor.out_h - GUI_WM_FS_TOPBAR_H),
	};
	if (self->gui.compositor.factory.vmt->create(&self->gui.compositor.factory, &geom,
	                                             &self->ble_pair_window) != WINDOW_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create the pairing window"));
		self->ble_pair_window = NULL;
		return;
	}

	Fb *win_fb = NULL;
	self->ble_pair_window->vmt->get_fb(self->ble_pair_window, &win_fb);
	if (fb_painter_init(&self->ble_pair_painter, win_fb) != FB_PAINTER_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start the pairing window painter"));
		self->gui.compositor.factory.vmt->destroy(&self->gui.compositor.factory, self->ble_pair_window);
		self->ble_pair_window = NULL;
		return;
	}

	self->ble_pair_window->vmt->set_title(self->ble_pair_window, "BLE pairing");
}


/* BLE event handler. Runs in the driver's receive task, so it only logs and paints here (which touches the
 * compositor, not the driver); anything that needs to call back into the driver (e.g. reflecting a written
 * value with set_value) must be deferred to another task. @p ctx is the owning App. */
static void app_ble_event_handler(void *ctx, const struct ble_event *event) {
	App *self = ctx;
	switch (event->type) {
		case BLE_EVENT_CONNECTED:
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("BLE connected (conn %u)"), (unsigned)event->conn);
			break;
		case BLE_EVENT_DISCONNECTED:
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("BLE disconnected (conn %u)"), (unsigned)event->conn);
			break;
		case BLE_EVENT_WRITE:
			//u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("BLE write of %u bytes"), (unsigned)event->len);
			break;
		case BLE_EVENT_SUBSCRIBE:
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("BLE subscribe (notify %u, indicate %u)"),
			      (unsigned)event->notify_en, (unsigned)event->indicate_en);
			break;
		case BLE_EVENT_PASSKEY_DISPLAY:
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("BLE pairing PIN: %06lu"),
			      (unsigned long)event->passkey);
			/* Show the passkey to the user on the pairing dialog and bring it on top. */
			snprintf(self->ble_passkey, sizeof(self->ble_passkey), "%06lu", (unsigned long)event->passkey);
			app_ble_pair_redraw(self);
			if (self->ble_pair_window != NULL) {
				self->ble_pair_window->vmt->show(self->ble_pair_window, true);
				self->ble_pair_window->vmt->to_front(self->ble_pair_window);
			}
			break;
		case BLE_EVENT_PAIRING_COMPLETE:
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("BLE pairing complete"));
			/* Pairing is over: hide the passkey dialog again. */
			if (self->ble_pair_window != NULL) {
				self->ble_pair_window->vmt->show(self->ble_pair_window, false);
			}
			break;
		case BLE_EVENT_PAIRING_FAILED:
			u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("BLE pairing failed"));
			/* Pairing is over: hide the passkey dialog again. */
			if (self->ble_pair_window != NULL) {
				self->ble_pair_window->vmt->show(self->ble_pair_window, false);
			}
			break;
		default:
			break;
	}
}


app_ret_t app_ble_init(App *self) {
	/* Discover the generic BLE device advertised by the port. */
	self->ble = NULL;
	if (iservicelocator_query_name_type(locator, "ble", ISERVICELOCATOR_TYPE_BLE, (Interface **)&self->ble) != ISERVICELOCATOR_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("BLE device not found"));
		return APP_RET_FAILED;
	}

	/* Prepare the (hidden) pairing dialog before advertising starts, so it is ready when the first peer
	 * connects and pairing begins. */
	app_ble_pair_window_init(self);

	/* Bring BLE up through the generic Ble interface and start advertising this device under its name. */
	self->ble->vmt->start(self->ble);
	self->ble->vmt->set_device_name(self->ble, "nwdaq-main-hh1");

	/* Tunnel datagrams over the same device. proto-dgble owns the device event callback (it needs the
	 * peer write events), so the application handler is forwarded to it rather than registered directly.
	 * Its GATT service and characteristics are built here, before the server is registered below. */
	/* A single dgble tunnel (BLE service ID 0x00000010) carries both flash endpoints. The ST67W611 does
	 * not reliably route peer writes to a second GATT service, so the proxy is exposed as a second
	 * endpoint on this one tunnel rather than as its own service. */
	const struct proto_dgble_config dgble_config = {
		.ble = self->ble,
		.service_id = 0x00000010,
		.event_cb = app_ble_event_handler,
		.event_cb_ctx = self,
	};
	if (proto_dgble_init(&self->dgble, &dgble_config) != PROTO_DGBLE_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start the datagram-over-BLE tunnel"));
		return APP_RET_FAILED;
	}

	static const struct proto_dgble_descriptor dgble_flash_desc = {
		.protocol = "flash",
		.protocol_version = "1.0.0",
	};

	/* Endpoint 1: this device's own flash, served by the local nbus-flash service below. */
	if (proto_dgble_add_endpoint(&self->dgble, 1, &dgble_flash_desc, &self->dgble_flash) != PROTO_DGBLE_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot add the flash tunnel endpoint"));
		return APP_RET_FAILED;
	}

	/* Endpoint 2: proxied to the measurement card's nbus-flash service over the backplane (the proxy
	 * itself is wired in app_setup_backplane, once the nbus2 stack is up). */
	if (proto_dgble_add_endpoint(&self->dgble, 2, &dgble_flash_desc, &self->dgble_proxy_flash) != PROTO_DGBLE_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot add the flash proxy tunnel endpoint"));
		return APP_RET_FAILED;
	}

	/* Finalize the tunnel: create the read-only descriptor characteristic last (ST67W611 quirk: it fails
	 * to route write events to any characteristic that follows a read-only one, so both writable endpoint
	 * characteristics must precede it). Must run before the GATT server is registered. */
	proto_dgble_start(&self->dgble);

	/* Serve the nbus-flash access protocol over endpoint 1 (this device's own flash). */
	if (nbus_flash_init(&self->dgble_nbus_flash, self->dgble_flash) != NBUS_FLASH_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start nbus-flash on the tunnel endpoint"));
	}

	/* Enable authenticated pairing with Passkey Entry: this device only has a display (DISPLAY_ONLY), so
	 * the module generates the PIN and reports it through BLE_EVENT_PASSKEY_DISPLAY while the peer types it
	 * in. */
	self->ble->vmt->set_security(self->ble, BLE_IO_CAP_DISPLAY_ONLY, BLE_SEC_LEVEL_AUTH);

	/* Build the GATT server: one primary service, then register it. The 128 bit UUID is written most
	 * significant octet first: 1ba6f7dd-31a5-7703-5bb0-89e1000004d2. */
	static const struct ble_uuid ble_srv_uuid = {
		.type = BLE_UUID_128,
		.u128 = {
			0x1b, 0xa6, 0xf7, 0xdd, 0x31, 0xa5, 0x77, 0x03,
			0x5b, 0xb0, 0x89, 0xe1, 0x00, 0x00, 0x04, 0xd2,
		},
	};
	self->ble->vmt->add_service(self->ble, &ble_srv_uuid, &self->ble_srv);

	/* Add a readable, writable, notifiable characteristic to the service. UUID (placeholder):
	 * 1ba6f7dd-31a5-7703-5bb0-89e1000004d3, most significant octet first. */
	static const struct ble_uuid ble_chr_uuid = {
		.type = BLE_UUID_128,
		.u128 = {
			0x1b, 0xa6, 0xf7, 0xdd, 0x31, 0xa5, 0x77, 0x03,
			0x5b, 0xb0, 0x89, 0xe1, 0x00, 0x00, 0x04, 0xd3,
		},
	};
	self->ble_srv.vmt->add_characteristic(&self->ble_srv, &ble_chr_uuid,
	                                      BLE_CHAR_PROP_READ | BLE_CHAR_PROP_WRITE | BLE_CHAR_PROP_NOTIFY,
	                                      &self->ble_chr);

	self->ble->vmt->server_start(self->ble);

	/* Override the module's default GAP appearance (0x0341, "Heart Rate Sensor") with 0x1480
	 * ("Generic Industrial Measurement Device"). */
	self->ble->vmt->set_appearance(self->ble, 0x1480);

	/* Give the characteristic an initial value so peer reads return something. */
	static const uint8_t ble_chr_value[] = { 0x00, 0x00, 0x00, 0x00 };
	self->ble_chr.vmt->set_value(&self->ble_chr, ble_chr_value, sizeof(ble_chr_value));

	self->ble->vmt->advertising_start(self->ble);

	return APP_RET_OK;
}
