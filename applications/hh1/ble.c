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
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("--------------- starting BLE services ----------------"));

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

	/* Export the remote configuration protocol as a single-session RemoteInterface. proto-conf predates
	 * RemoteInterface, so it is wired to the proxy's session Datagram. It runs with a NULL root, exposing
	 * every Conf tree advertised via the service locator, each mounted under its (space-delimited) name. The
	 * load and save commands run the conf-cbor jobs. */
	if (remote_interface_proxy_init(&self->conf_proxy, &(const struct remote_interface_proxy_conf) {
		.desc = {
			.protocol = "conf",
			.protocol_version = "1.0.0",
			.mtu = CONFIG_SERVICE_PROTO_CONF_MAX_DATAGRAM_LEN,
			.max_sessions = 1,
			.session_idle_ms = 0,
		},
		.queue_len = 2,
	}) != REMOTE_INTERFACE_PROXY_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start the conf proxy"));
		return APP_RET_FAILED;
	}
	if (proto_conf_init(&self->proto_conf, &(const struct proto_conf_conf) {
		.d = &self->conf_proxy.sessions[0].dgram,
		.root = NULL,
		.load = &self->conf_cbor.jobs.load.job,
		.save = &self->conf_cbor.jobs.save.job,
	}) != PROTO_CONF_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start proto-conf"));
		return APP_RET_FAILED;
	}
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_REMOTE_INTERFACE, (Interface *)&self->conf_proxy.iface, "conf");

	/* Export this device's own flash, served by nbus-flash, as "flash". */
	if (remote_interface_proxy_init(&self->flash_proxy, &(const struct remote_interface_proxy_conf) {
		.desc = {
			.protocol = "flash",
			.protocol_version = "1.0.0",
			.mtu = NBUS_FLASH_RX_BUF_LEN,
			.max_sessions = 1,
			.session_idle_ms = 0,
		},
		.queue_len = 2,
	}) != REMOTE_INTERFACE_PROXY_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start the flash proxy"));
		return APP_RET_FAILED;
	}
	if (nbus_flash_init(&self->nbus_flash, &self->flash_proxy.sessions[0].dgram) != NBUS_FLASH_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start nbus-flash"));
		return APP_RET_FAILED;
	}
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_REMOTE_INTERFACE, (Interface *)&self->flash_proxy.iface, "flash");

	/* Export the local message queue, served by the nbus-mq-poll bridge, as "mq". */
	Mq *mq = NULL;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_MQ, 0, (Interface **)&mq) == ISERVICELOCATOR_RET_OK) {
		if (remote_interface_proxy_init(&self->mq_proxy, &(const struct remote_interface_proxy_conf) {
			.desc = {
				.protocol = "mq",
				.protocol_version = "1.0.0",
				.mtu = NBUS_MQ_POLL_NBUS_BUF_LEN,
				.max_sessions = 1,
				.session_idle_ms = 0,
			},
			.queue_len = 2,
		}) != REMOTE_INTERFACE_PROXY_RET_OK) {
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start the mq proxy"));
			return APP_RET_FAILED;
		}
		nbus_mq_poll_init(&self->nbus_mq_poll, &(const struct nbus_mq_poll_conf) {
			.mq = mq,
			.d = &self->mq_proxy.sessions[0].dgram,
			.topic = "#",
			.device_name = "nwdaq-main-hh1",
		});
		nbus_mq_poll_start(&self->nbus_mq_poll);
		iservicelocator_add(locator, ISERVICELOCATOR_TYPE_REMOTE_INTERFACE, (Interface *)&self->mq_proxy.iface, "mq");
	} else {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("no message queue, mq not exported"));
	}

	#if defined(CONFIG_APP_HH1_PROFILE_FF14_FORCE)
		/* Export the measurement card's flash as "ff14 flash". The nbus-flash-proxy relaying the session to
		 * the card is wired in app_setup_backplane, once the nbus2 stack is up. */
		if (remote_interface_proxy_init(&self->ff14_flash_proxy, &(const struct remote_interface_proxy_conf) {
			.desc = {
				.protocol = "flash",
				.protocol_version = "1.0.0",
				.mtu = NBUS_FLASH_PROXY_BUF_LEN,
				.max_sessions = 1,
				.session_idle_ms = 0,
			},
			.queue_len = 2,
		}) != REMOTE_INTERFACE_PROXY_RET_OK) {
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start the ff14 flash proxy"));
			return APP_RET_FAILED;
		}
		iservicelocator_add(locator, ISERVICELOCATOR_TYPE_REMOTE_INTERFACE, (Interface *)&self->ff14_flash_proxy.iface, "ff14 flash");
	#endif

	/* Export all advertised RemoteInterfaces over BLE. The transport is the only user of the device and owns
	 * its event callback, the application handler is forwarded to it. Its GATT service is built here, before
	 * the server is registered below. */
	if (remote_interface_ble_init(&self->ri_ble, &(const struct remote_interface_ble_conf) {
		.ble = self->ble,
		.event_cb = app_ble_event_handler,
		.event_cb_ctx = self,
	}) != REMOTE_INTERFACE_BLE_RET_OK ||
	    remote_interface_ble_start(&self->ri_ble) != REMOTE_INTERFACE_BLE_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start the RemoteInterface BLE transport"));
		return APP_RET_FAILED;
	}

	/* Enable authenticated pairing with Passkey Entry: this device only has a display (DISPLAY_ONLY), so
	 * the module generates the PIN and reports it through BLE_EVENT_PASSKEY_DISPLAY while the peer types it
	 * in. */
	self->ble->vmt->set_security(self->ble, BLE_IO_CAP_DISPLAY_ONLY, BLE_SEC_LEVEL_AUTH);

	/* Register the GATT server (the RemoteInterface transport service built above). */
	self->ble->vmt->server_start(self->ble);

	/* Override the module's default GAP appearance (0x0341, "Heart Rate Sensor") with 0x1480
	 * ("Generic Industrial Measurement Device"). */
	self->ble->vmt->set_appearance(self->ble, 0x1480);

	self->ble->vmt->advertising_start(self->ble);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("all BLE services started successfully"));
	return APP_RET_OK;
}
