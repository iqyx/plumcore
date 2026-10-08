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
#include <interfaces/waveform-source.h>
#include <interfaces/painter.h>
#include <interfaces/event.h>
#include <interfaces/beeper.h>
#include <interfaces/led.h>
#include <interfaces/ble.h>
#include <interfaces/pm.h>
#include <services/fb-compositor/fb-compositor.h>
#include <services/fb-painter/fb-painter.h>
#include <services/gui-wm-fs/gui-wm-fs.h>
#include <interfaces/datagram.h>
#include <services/proto-dgble/proto-dgble.h>
#include <services/nbus-flash/nbus-flash.h>
#include <services/nbus-flash-proxy/nbus-flash-proxy.h>
#include <services/nbus-mq-poll/nbus-mq-poll.h>
#include <services/proto-conf/proto-conf.h>
#include <services/conf-cbor/conf-cbor.h>
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

	/* Power manager advertised by the port, requested to full power on key activity. */
	Pm *pm;

	/* Battery fuel gauge measurements advertised by the BQ27441 driver. */
	Sensor *bat_voltage;
	Sensor *bat_current;
	Sensor *bat_soc;
	Sensor *bat_soh;
	Sensor *bat_remaining;

	/* LIS2HH12 accelerometer advertised by the port. */
	WaveformSource *accel;

	/* Battery status LED advertised by the port and the blink sequence currently pushed to it, kept so the
	 * blink is only restarted when the current band actually changes. */
	Led *led_bat;
	const led_seq_item_t *bat_led_seq;

	/* Serial console stream advertised by the port. */
	Stream *console;

	/* Generic BLE device advertised by the port and the GATT server built on top of it (see ble.c). */
	Ble *ble;

	/* Datagram-over-BLE tunnel built on the same device and the flash endpoint's Datagram. */
	ProtoDgble dgble;
	Datagram *dgble_flash;
	/* nbus-flash access service served over the flash tunnel endpoint. */
	NbusFlash dgble_nbus_flash;

	/* Second endpoint (ep 2) on the dgble tunnel above, proxied to the measurement card's nbus-flash
	 * service over the backplane. Exposed on the same tunnel because the ST67W611 does not reliably route
	 * peer writes to a second GATT service. */
	Datagram *dgble_proxy_flash;

	/* Third endpoint (ep 3) on the dgble tunnel above and the nbus-mq-poll bridge serving the local
	 * message queue over it, so a BLE client can poll the values pulled in from the measurement card. */
	Datagram *dgble_mq_poll;
	NbusMqPoll dgble_nbus_mq_poll;

	/* Fourth endpoint (ep 4) on the dgble tunnel above and the remote configuration protocol served
	 * over it. It runs with a NULL root, so it exposes every Conf tree advertised via the service
	 * locator, each mounted under its (space-delimited) name. */
	Datagram *dgble_conf;
	ProtoConf dgble_proto_conf;

	/* Pairing dialog: a hidden compositor window brought on top while a passkey is being entered,
	 * painted through its own painter. The passkey is kept as a preformatted six digit string. */
	Window *ble_pair_window;
	FbPainter ble_pair_painter;
	char ble_passkey[8];

	/* LCD framebuffer advertised by the port, driven through the compositor below. */
	Fb *lcd;

	/* Full-screen GUI window manager running on top of the LCD (owns the compositor and its bars). */
	GuiWmFs gui;

	/* Configuration tree of all advertised Conf subtrees saved as CBOR to the "conf" flash partition. */
	Flash *conf_flash;
	ConfCbor conf_cbor;

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
 * Sets up the event handler, device name, pairing security, the datagram-over-BLE tunnel and its
 * endpoints, the GAP appearance and starts advertising. Implemented in ble.c.
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
