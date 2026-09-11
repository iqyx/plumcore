/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * nwdaq-main-hh1 battery monitor application
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <main.h>
#include "config.h"

#include <interfaces/power.h>
#include <interfaces/fb.h>
#include <interfaces/window.h>
#include <interfaces/stream.h>
#include <interfaces/sensor.h>
#include <interfaces/painter.h>
#include <interfaces/event.h>
#include <interfaces/beeper.h>
#include <interfaces/ble.h>
#include <services/fb-compositor/fb-compositor.h>
#include <services/fb-painter/fb-painter.h>
#include <services/gui-wm-fs/gui-wm-fs.h>
#include <interfaces/datagram.h>
#include <services/proto-dgble/proto-dgble.h>
#include <services/nbus-flash/nbus-flash.h>
#include <services/nbus-flash-proxy/nbus-flash-proxy.h>
#if defined(CONFIG_SERVICE_NBUS_MQ_CLIENT)
	#include <interfaces/mq.h>
	#include <services/nbus2/nbus2.h>
	#include <services/proto-dgstream/proto-dgstream.h>
	#include <services/nbus-mq-client/nbus-mq-client.h>
#endif

typedef enum {
	APP_RET_OK = 0,
	APP_RET_FAILED,
} app_ret_t;

typedef struct {
	TaskHandle_t task;

	/* Battery charging power device advertised by the BQ25798 charger driver. */
	Power *charger;

	/* Inductive keypad event source advertised by the port. */
	Event *keypad;

	/* Piezo beeper advertised by the port, used for key-press feedback. */
	Beeper *beeper;

	/* Battery fuel gauge measurements advertised by the BQ27441 driver. */
	Sensor *bat_voltage;
	Sensor *bat_current;
	Sensor *bat_soc;
	Sensor *bat_soh;
	Sensor *bat_remaining;

	/* Serial console stream advertised by the port. */
	Stream *console;

	/* Generic BLE device advertised by the port and the GATT server built on top of it (see ble.c). */
	Ble *ble;
	BleSrv ble_srv;
	BleChar ble_chr;

	/* Datagram-over-BLE tunnel built on the same device and the flash endpoint's Datagram. */
	ProtoDgble dgble;
	Datagram *dgble_flash;
	/* nbus-flash access service served over the flash tunnel endpoint. */
	NbusFlash dgble_nbus_flash;

	/* Second endpoint (ep 2) on the dgble tunnel above, proxied to the measurement card's nbus-flash
	 * service over the backplane. Exposed on the same tunnel because the ST67W611 does not reliably route
	 * peer writes to a second GATT service. */
	Datagram *dgble_proxy_flash;

	/* Pairing dialog: a hidden compositor window brought on top while a passkey is being entered,
	 * painted through its own painter. The passkey is kept as a preformatted six digit string. */
	Window *ble_pair_window;
	FbPainter ble_pair_painter;
	char ble_passkey[8];

	/* LCD framebuffer advertised by the port, driven through the compositor below. */
	Fb *lcd;

	/* Full-screen GUI window manager running on top of the LCD (owns the compositor and its bars). */
	GuiWmFs gui;

	#if defined(CONFIG_SERVICE_NBUS_MQ_CLIENT)
		/* Message queue the measurement card's values are republished into. */
		Mq *mq;

		/* nbus2 stack on the backplane stream and the poll client pulling values from the
		 * measurement card's nbus-mq-poll service. */
		ProtoDgstream nbus_dgstream;
		Nbus nbus;
		struct nbus_socket *nbus_mq_socket;
		NbusMqClient nbus_mq;

		/* Socket connected to the measurement card's nbus-flash service and the proxy relaying the BLE
		 * flash tunnel endpoint to it. */
		struct nbus_socket *nbus_flash_socket;
		NbusFlashProxy nbus_flash_proxy;
	#endif
} App;


/**
 * @brief Discover the BLE device advertised by the port and build the GATT server on it.
 *
 * Sets up the event handler, device name, pairing security, the GATT service and characteristic,
 * the GAP appearance and starts advertising. Implemented in ble.c.
 *
 * @param self Instance of the application
 * @return APP_RET_FAILED if the BLE device was not found or setup failed,
 *         APP_RET_OK otherwise.
 */
app_ret_t app_ble_init(App *self);

/**
 * @brief Prepare the new instance, allocate resources and start
 *        the application task.
 *
 * @param self Preallocated memory for the instance
 * @return APP_RET_FAILED if the application initialization failed,
 *         APP_RET_OK otherwise
 */
app_ret_t app_init(App *self);

/**
 * @brief Free the application instance, release all allocated resources
 *
 * @param self Instance of the application
 * @return APP_RET_FAILED on error,
 *         APP_RET_OK otherwise.
 */
app_ret_t app_free(App *self);
