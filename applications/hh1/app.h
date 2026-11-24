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
#include <interfaces/clock.h>
#include <interfaces/waveform-source.h>
#include <interfaces/block.h>
#include <interfaces/fs.h>
#include <interfaces/painter.h>
#include <interfaces/event.h>
#include <interfaces/beeper.h>
#include <interfaces/led.h>
#include <interfaces/ble.h>
#include <interfaces/wifi.h>
#include <interfaces/pm.h>
#include <services/fb-compositor/fb-compositor.h>
#include <services/fb-painter/fb-painter.h>
#include <services/gui-wm-fs/gui-wm-fs.h>
#include <interfaces/datagram.h>
#include <services/remote-interface-ble/remote-interface-ble.h>
#include <services/remote-interface-proxy/remote-interface-proxy.h>
#include <services/proto-conf/proto-conf.h>
#include <services/nbus-flash/nbus-flash.h>
#include <services/nbus-mq-poll/nbus-mq-poll.h>
#include <services/conf-cbor/conf-cbor.h>
#include <services/mq-sensor-source/mq-sensor-source.h>
#if defined(CONFIG_APP_HH1_PROFILE_FF14_FORCE)
	#include <interfaces/mq.h>
	#include <services/nbus2/nbus2.h>
	#include <services/proto-dgstream/proto-dgstream.h>
	#include <services/nbus-mq-client/nbus-mq-client.h>
	#include <services/nbus-flash-proxy/nbus-flash-proxy.h>
	#include <services/mq-compensation/mq-compensation.h>
	#include <services/mq-compensation/mq-compensation-cert.h>
	#include <services/nbus-flash-client/nbus-flash-client.h>

	/* Number of TEDS partitions (teds0 to teds3) of the measurement card, each calibrating one force channel. */
	#define APP_SENSOR_CALIBRATION_CHANNELS 4

	/* A target of the certificate loaded from a TEDS partition, exposed as "target <id> sn" (see
	 * sensor-calibration.c). Allocated when the certificate is loaded, freed when it changes or disappears. */
	struct app_teds_target {
		ConfiglibValue conf;
		ConfiglibValue sn_conf;
		char id[MQ_COMPENSATION_CERT_STR_LEN];
		char sn[MQ_COMPENSATION_CERT_STR_LEN];
		struct app_teds_target *next;
	};
#endif

/* Number of entries of the sensors published by mq-sensor-source, including the terminating one. */
#define APP_SENSOR_SOURCE_SENSORS 10

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
	Sensor *bat_full;

	/* Voltages of the two charger power sources advertised by the BQ25798 driver. */
	Sensor *charger_vac1;
	Sensor *charger_vac2;
	Sensor *charger_ibus;

	/* The battery and charger sensors above periodically published as sensor/<name>. The sensor list is referenced
	 * by the service configuration, it is kept here for the lifetime of the service. */
	struct mq_sensor_source_sensor sensor_source_sensors[APP_SENSOR_SOURCE_SENSORS];
	MqSensorSource sensor_source;

	/* RTC wall clock advertised by the port, shown in the GUI status bar. */
	Clock *rtc;

	/* LIS2HH12 accelerometer advertised by the port. */
	WaveformSource *accel;

	/* SD card block device advertised by the port, shown in the GUI status bar, and the filesystem on it. */
	Block *sd;
	Fs *sd_fs;

	/* Battery status LED advertised by the port and the blink sequence currently pushed to it, kept so the
	 * blink is only restarted when the current band actually changes. */
	Led *led_bat;
	const led_seq_item_t *bat_led_seq;

	/* Serial console stream advertised by the port. */
	Stream *console;

	/* Generic BLE device advertised by the port and the GATT server built on top of it (see ble.c). */
	Ble *ble;

	/* Generic Wi-Fi device advertised by the port, configured through its "device wifi wifi0" Conf subtree (see
	 * wifi.c). */
	Wifi *wifi;

	/* RemoteInterface transport owning the BLE device, exporting every advertised RemoteInterface. */
	RemoteInterfaceBle ri_ble;

	/* Remote configuration protocol exported as a single-session RemoteInterface. proto-conf runs with a
	 * NULL root, so it exposes every Conf tree advertised via the service locator, each mounted under its
	 * (space-delimited) name. */
	RemoteInterfaceProxy conf_proxy;
	ProtoConf proto_conf;

	/* This device's own flash served by nbus-flash, exported as "flash". */
	RemoteInterfaceProxy flash_proxy;
	NbusFlash nbus_flash;

	/* The local message queue served by the nbus-mq-poll bridge, exported as "mq", so a BLE client can poll
	 * the values published there (eg. the measurement card's values when an application profile pulls them
	 * in). */
	RemoteInterfaceProxy mq_proxy;
	NbusMqPoll nbus_mq_poll;

	#if defined(CONFIG_APP_HH1_PROFILE_FF14_FORCE)
		/* The measurement card's flash exported as "ff14 flash". Created in app_setup_backplane together with
		 * the nbus-flash-proxy relaying its session to the card's nbus-flash service. */
		RemoteInterfaceProxy ff14_flash_proxy;
	#endif

	/* Pairing dialog: a hidden compositor window brought on top while a passkey is being entered,
	 * painted through its own painter. The passkey is kept as a preformatted six digit string. */
	Window *ble_pair_window;
	FbPainter ble_pair_painter;
	char ble_passkey[8];

	/* Connection state changes handed off from the BLE event handler to a task showing them as notifications.
	 * The handler runs in the driver's receive task and must not block on the notification. */
	QueueHandle_t ble_notify_queue;
	TaskHandle_t ble_notify_task;

	/* Task periodically showing the low battery notification (see bat-low.c). */
	TaskHandle_t bat_low_task;

	/* Task mounting the SD card filesystem on card insertion and notifying the card status changes (see sd.c). */
	TaskHandle_t sd_task;

	/* LCD framebuffer advertised by the port, driven through the compositor below. */
	Fb *lcd;

	/* Full-screen GUI window manager running on top of the LCD (owns the compositor and its bars). */
	GuiWmFs gui;

	/* Configuration tree of all advertised Conf subtrees saved as CBOR to the "conf" flash partition. */
	Flash *conf_flash;
	ConfCbor conf_cbor;

	#if defined(CONFIG_APP_HH1_PROFILE_FF14_FORCE)
		/* Message queue the measurement card's values are republished into. */
		Mq *mq;

		/* nbus2 stack on the backplane stream and the poll client pulling values from the
		 * measurement card's nbus-mq-poll service. */
		ProtoDgstream nbus_dgstream;
		Nbus nbus;
		struct nbus_socket *nbus_mq_socket;
		NbusMqClient nbus_mq;

		/* Socket connected to the measurement card's nbus-flash service and the proxy relaying the
		 * "ff14 flash" RemoteInterface session to it. */
		struct nbus_socket *nbus_flash_socket;
		NbusFlashProxy nbus_flash_proxy;

		/* Compensation converting the measurement card's raw channels 0-3 into force channels 0-3. */
		MqCompensation force;

		/* Socket connected to the measurement card's nbus-flash service and the client reading the sensor
		 * calibration certificates from its TEDS partitions (see sensor-calibration.c). */
		struct nbus_socket *nbus_flash_client_socket;
		NbusFlashClient ff14_flash;
		TaskHandle_t sensor_calibration_task;

		/* Identifier of the certificate last loaded from each TEDS partition, empty if none. */
		char teds_cert_id[APP_SENSOR_CALIBRATION_CHANNELS][MQ_COMPENSATION_CERT_STR_LEN];

		/* Reason the certificate of each TEDS partition was last skipped for, NULL if it was not. Kept to log
		 * each reason once only. */
		const char *teds_skip[APP_SENSOR_CALIBRATION_CHANNELS];

		/* Configuration tree advertised as "device teds". It holds a subtree "<n>" for each TEDS partition with a
		 * certificate, exposing the identifier of the certificate as "id" and the serial number of each
		 * certificate target as "target <id> sn". The subtree is removed when the certificate disappears. */
		struct {
			ConfiglibValue root;
			struct {
				char name[4];
				ConfiglibValue conf;
				ConfiglibValue id;
				ConfiglibValue target;
				struct app_teds_target *targets;
			} dev[APP_SENSOR_CALIBRATION_CHANNELS];
		} teds_conf;
	#endif
} App;


/**
 * @brief Discover the BLE device advertised by the port and build the GATT server on it.
 *
 * Sets up the event handler, device name, pairing security, the RemoteInterface transport and the
 * interfaces exported over it, the GAP appearance and starts advertising. Implemented in ble.c.
 *
 * @param self Instance of the application
 * @return APP_RET_FAILED if the BLE device was not found or setup failed,
 *         APP_RET_OK otherwise.
 */
app_ret_t app_ble_init(App *self);

/**
 * @brief Discover the Wi-Fi device advertised by the port and start the Wi-Fi link.
 *
 * Sets up the event handler and the host name and starts the link with the mode, SSID and security stored in the
 * device's configuration subtree. Implemented in wifi.c.
 *
 * @param self Instance of the application
 * @return APP_RET_FAILED if the Wi-Fi device was not found or cannot be started,
 *         APP_RET_OK otherwise.
 */
app_ret_t app_wifi_init(App *self);

/**
 * @brief Read a string leaf of a Conf tree advertised via the service locator.
 *
 * The leaf @p leaf must be a direct child of the tree advertised as @p root. Implemented in ble.c.
 *
 * @param root Name of the advertised Conf tree
 * @param leaf Name of the string leaf
 * @param val The value, pointing to the node's own storage
 * @return CONF_RET_FAILED if the tree or the leaf does not exist or the leaf is not a string,
 *         CONF_RET_OK otherwise.
 */
conf_ret_t app_conf_str(const char *root, const char *leaf, union conf_val *val);

#if defined(CONFIG_APP_HH1_BAT_LOW_NOTIFY_ENABLE)
/**
 * @brief Start the low battery notification.
 *
 * Starts a thread checking the battery every CONFIG_APP_HH1_BAT_LOW_NOTIFY_PERIOD seconds. While the battery voltage
 * is below CONFIG_APP_HH1_BAT_LOW_NOTIFY_VOLTAGE and it is not charging, a notification is shown. Implemented in
 * bat-low.c.
 *
 * @param self Instance of the application
 * @return APP_RET_FAILED if the GUI or the battery sensors are not available or the thread cannot be started,
 *         APP_RET_OK otherwise.
 */
app_ret_t app_bat_low_init(App *self);
#endif

/**
 * @brief Start the SD card auto-mount.
 *
 * Starts a thread polling the SD card status. Whenever a card is inserted (or present at startup), its filesystem
 * is mounted. The mount result and the removal are notified. Implemented in sd.c.
 *
 * @param self Instance of the application
 * @return APP_RET_FAILED if the SD card or its filesystem is not available or the thread cannot be started,
 *         APP_RET_OK otherwise.
 */
app_ret_t app_sd_init(App *self);

#if defined(CONFIG_APP_HH1_PROFILE_FF14_FORCE)
/**
 * @brief Start loading the sensor calibration certificates from the measurement card.
 *
 * Starts a thread periodically checking the TEDS partitions of the measurement card. When the certificate of a
 * partition changes, its compensation is loaded into the corresponding force channel. Implemented in
 * sensor-calibration.c.
 *
 * @param self Instance of the application
 * @return APP_RET_FAILED if the backplane is not available or the thread cannot be started,
 *         APP_RET_OK otherwise.
 */
app_ret_t app_sensor_calibration_init(App *self);
#endif

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
