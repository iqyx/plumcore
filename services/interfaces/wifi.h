/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Generic Wi-Fi device interface
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * This is a generic interface for services/modules providing access to a Wi-Fi device. It controls the link
 * layer only: the operating mode, the network name and the security parameters. Data is carried by the network
 * interfaces (Tcp, Tls) implemented by the same driver or by a TCP/IP stack on top of it.
 *
 * The setters only store the configuration, it is applied by start(). Joining a network may take seconds, so
 * start() and stop() only initiate the change and return. The result is reported asynchronously through the
 * event callback and can be polled with get_status. Configuration changed while the device is started is applied
 * by calling start() again.
 *
 * The event callback has a single owner. Consumers which do not own it (a status bar) poll get_status instead.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>


typedef enum {
	/** The function performed as intended with no unexpected result. */
	WIFI_RET_OK = 0,

	/** A generic failure not covered by a more specific code. */
	WIFI_RET_FAILED,

	/** A NULL pointer was given as an instance or a required argument. */
	WIFI_RET_NULL,

	/** A wrong argument value was used (eg. an SSID or a passphrase too long). */
	WIFI_RET_BAD_ARG,

	/** The mode or the security is not supported by the device. */
	WIFI_RET_NOT_IMPLEMENTED,
} wifi_ret_t;


/** Maximum length of an SSID in bytes, excluding the terminating NUL. */
#define WIFI_SSID_LEN_MAX 32

/** Maximum length of a WPA passphrase in characters, excluding the terminating NUL. */
#define WIFI_PSK_LEN_MAX 64


enum wifi_mode {
	/** The radio is disabled. */
	WIFI_MODE_OFF = 0,

	/** Station mode, join the network with the configured SSID. */
	WIFI_MODE_STA,

	/** Access point mode, create a network with the configured SSID. */
	WIFI_MODE_AP,
};

/** Mode name lookup table initializer, indexed by enum wifi_mode. */
#define WIFI_MODE_NAMES { "off", "sta", "ap" }


enum wifi_security {
	/** Open network, no passphrase. */
	WIFI_SECURITY_OPEN = 0,

	/** WPA2 personal with a passphrase. */
	WIFI_SECURITY_WPA2_PSK,

	/** WPA3 personal (SAE) with a passphrase. */
	WIFI_SECURITY_WPA3_SAE,
};

/** Security name lookup table initializer, indexed by enum wifi_security. */
#define WIFI_SECURITY_NAMES { "open", "wpa2-psk", "wpa3-sae" }


enum wifi_state {
	/** Not connected and not trying to (stopped, mode off or a connection attempt failed). */
	WIFI_STATE_IDLE = 0,

	/** Joining the network (station mode). */
	WIFI_STATE_CONNECTING,

	/** Associated with the access point, no IP address yet (station mode). */
	WIFI_STATE_CONNECTED,

	/** The network is usable: an IP address was obtained (station mode) or the access point is running. */
	WIFI_STATE_UP,
};


/**
 * A snapshot of the current link and traffic state, filled in by get_status. The byte counters are free-running
 * totals of the data carried by the network interfaces of the device since the driver started (they wrap at
 * 2^32), a poller detects traffic by comparing successive samples.
 */
struct wifi_status {
	enum wifi_state state;
	uint32_t rx_bytes;
	uint32_t tx_bytes;
};


enum wifi_event_type {
	/** Associated with the access point (station mode). */
	WIFI_EVENT_CONNECTED,

	/** An IP address was obtained, the network is usable (station mode). */
	WIFI_EVENT_GOT_IP,

	/** The link was lost or the device disconnected (station mode). */
	WIFI_EVENT_DISCONNECTED,

	/** A connection attempt failed (station mode). @p reason holds a device specific reason code. */
	WIFI_EVENT_CONNECT_FAILED,
};

struct wifi_event {
	enum wifi_event_type type;

	/** Device specific reason code for WIFI_EVENT_CONNECT_FAILED, 0 if not known. */
	int32_t reason;
};

/**
 * @brief Event callback
 *
 * Invoked by the driver when an asynchronous event occurs. @p cb_ctx is the opaque pointer supplied to
 * set_event_handler. The @p event is valid only for the duration of the call. The callback runs in a driver task
 * and must not block or call back into the driver.
 */
typedef void (*wifi_event_cb)(void *cb_ctx, const struct wifi_event *event);


typedef struct wifi Wifi;

struct wifi_vmt {
	/**
	 * @brief Apply the configuration and bring the link up
	 *
	 * Apply the configured mode, SSID and security: join the network in station mode, create it in access point
	 * mode, disable the radio in the off mode. Returns as soon as the change is initiated, the result is reported
	 * through events and get_status. Calling it again re-applies the current configuration.
	 *
	 * @param self Wifi interface instance
	 *
	 * @return WIFI_RET_OK if the change was initiated or WIFI_RET_FAILED otherwise.
	 */
	wifi_ret_t (*start)(Wifi *self);

	/**
	 * @brief Bring the link down
	 *
	 * Disconnect or stop the access point and disable the radio. Returns as soon as the change is initiated.
	 *
	 * @param self Wifi interface instance
	 *
	 * @return WIFI_RET_OK if the change was initiated or WIFI_RET_FAILED otherwise.
	 */
	wifi_ret_t (*stop)(Wifi *self);

	/**
	 * @brief Set the operating mode
	 *
	 * Applied by the next start().
	 *
	 * @param self Wifi interface instance
	 * @param mode Operating mode
	 *
	 * @return WIFI_RET_OK on success,
	 *         WIFI_RET_NOT_IMPLEMENTED if the device does not support @p mode.
	 */
	wifi_ret_t (*set_mode)(Wifi *self, enum wifi_mode mode);

	/**
	 * @brief Set the network name
	 *
	 * The network to join in station mode or to create in access point mode. Applied by the next start().
	 *
	 * @param self Wifi interface instance
	 * @param ssid SSID, at most WIFI_SSID_LEN_MAX bytes
	 *
	 * @return WIFI_RET_OK on success,
	 *         WIFI_RET_NULL if @p ssid is NULL,
	 *         WIFI_RET_BAD_ARG if @p ssid is too long.
	 */
	wifi_ret_t (*set_ssid)(Wifi *self, const char *ssid);

	/**
	 * @brief Set the security parameters
	 *
	 * In access point mode @p security selects the security of the created network. In station mode the
	 * security is negotiated with the access point, @p security only tells whether the passphrase is used.
	 * Applied by the next start().
	 *
	 * @param self Wifi interface instance
	 * @param security Security type
	 * @param psk WPA passphrase, at most WIFI_PSK_LEN_MAX characters. Ignored (may be NULL) for an open network.
	 *
	 * @return WIFI_RET_OK on success,
	 *         WIFI_RET_NULL if @p psk is NULL for a secured network,
	 *         WIFI_RET_BAD_ARG if @p psk is too long,
	 *         WIFI_RET_NOT_IMPLEMENTED if the device does not support @p security.
	 */
	wifi_ret_t (*set_security)(Wifi *self, enum wifi_security security, const char *psk);

	/**
	 * @brief Set the host name
	 *
	 * Host name the device announces in station mode (eg. in its DHCP requests). Applied by the next start().
	 * NULL if the device does not support it.
	 *
	 * @param self Wifi interface instance
	 * @param hostname Host name
	 *
	 * @return WIFI_RET_OK on success,
	 *         WIFI_RET_NULL if @p hostname is NULL,
	 *         WIFI_RET_BAD_ARG if @p hostname is too long for the device.
	 */
	wifi_ret_t (*set_hostname)(Wifi *self, const char *hostname);

	/**
	 * @brief Set the event callback
	 *
	 * @param self Wifi interface instance
	 * @param cb Event callback, NULL to remove it
	 * @param cb_ctx Opaque pointer passed to @p cb
	 *
	 * @return WIFI_RET_OK.
	 */
	wifi_ret_t (*set_event_handler)(Wifi *self, wifi_event_cb cb, void *cb_ctx);

	/**
	 * @brief Get the link and traffic state
	 *
	 * Non-blocking, safe to call from any task.
	 *
	 * @param self Wifi interface instance
	 * @param status Current state
	 *
	 * @return WIFI_RET_OK on success,
	 *         WIFI_RET_NULL if @p status is NULL.
	 */
	wifi_ret_t (*get_status)(Wifi *self, struct wifi_status *status);

	/* reserved: scan */
};

struct wifi {
	const struct wifi_vmt *vmt;

	/** Driver context. */
	void *parent;
};
