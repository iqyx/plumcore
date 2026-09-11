/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Generic Bluetooth Low Energy (BLE) device interface
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * This is a generic interface for services/modules providing access to a BLE device. It abstracts a BLE
 * controller/host stack (often a self-contained module driven over a serial or SPI link) so that
 * applications can build a GATT server and exchange attribute values without depending on a particular
 * vendor's command set.
 *
 * The interface is modelled as a small object tree. The root Ble object represents the whole device. A
 * GATT server is built by adding BleSrv objects to it, and BleChar objects to each service.
 * Every child object is preallocated by the caller and filled in by the driver, and keeps a typed pointer
 * back to its owner. A method therefore dispatches on the object it acts upon: characteristic operations
 * take a BleChar, service operations take a BleSrv, and so on. Walking the parent chain
 * (characteristic -> service -> device) leads back to the driver, whose private context lives in the root
 * Ble object's parent pointer.
 *
 * The current scope covers the peripheral (server) role: advertise, build services and characteristics,
 * serve reads from a locally stored value, receive writes from the peer, and push notifications and
 * indications. The value store model is used for reads - the driver holds the last value set via
 * characteristic set_value and answers peer reads from it, while peer writes are delivered asynchronously
 * through the event callback.
 *
 * The central (client) role is not implemented yet, but the shared abstractions (UUIDs, connection
 * handles, the event callback) are role neutral so it can be added later without breaking the ABI. Method
 * slots reserved for the client role are noted below.
 *
 * Ble/ble_ prefixes are used for the device object, BleSrv/ble_srv_ and
 * BleChar/ble_char_ for the GATT server building blocks.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>


typedef enum {
	/** The function performed as intended with no unexpected result. */
	BLE_RET_OK = 0,

	/** A generic failure not covered by a more specific code. */
	BLE_RET_FAILED,

	/** A NULL pointer was given as an instance or a required argument. */
	BLE_RET_NULL,

	/** A wrong argument value was used when calling the function. */
	BLE_RET_BAD_ARG,

	/** All arguments are valid but the device rejects the call in its current state (eg. not
	 *  advertising, not connected, or the wrong role). */
	BLE_RET_BAD_STATE,

	/** The function is not implemented by this driver (eg. a client role method on a server only
	 *  driver). */
	BLE_RET_NOT_IMPLEMENTED,

	/** A timeout occurred while waiting for the device to complete the operation. */
	BLE_RET_TIMEOUT,

	/** A resource (GATT table entry, connection slot, buffer) is exhausted. */
	BLE_RET_NOMEM,
} ble_ret_t;


/**
 * A BLE attribute UUID is either a 16 bit (Bluetooth SIG assigned) or a full 128 bit value. The 128 bit
 * form is stored most significant octet first, i.e. in the human-readable UUID order (the driver reorders
 * it as needed for the wire).
 */
enum ble_uuid_type {
	BLE_UUID_16,
	BLE_UUID_128,
};

struct ble_uuid {
	enum ble_uuid_type type;
	union {
		uint16_t u16;
		uint8_t u128[16];
	};
};


/**
 * Connection handle identifying a single link to a peer. A server driver supporting a single connection
 * at a time may ignore it; BLE_CONN_ANY targets the sole (or currently active) connection and lets simple
 * applications stay connection agnostic.
 */
typedef uint16_t ble_conn_t;
#define BLE_CONN_ANY ((ble_conn_t)0xffff)


/**
 * Characteristic properties, declared as a bit mask to be combined when a characteristic is added.
 */
enum ble_char_prop {
	BLE_CHAR_PROP_READ     = 1,
	BLE_CHAR_PROP_WRITE    = 2,
	BLE_CHAR_PROP_WRITE_NR = 4,   /**< write without response */
	BLE_CHAR_PROP_NOTIFY   = 8,
	BLE_CHAR_PROP_INDICATE = 16,
};


/**
 * Input/output capabilities of the local device, advertised during pairing so the security manager can
 * pick an association model (Just Works, Passkey Entry or Numeric Comparison). A device that can only
 * show digits uses BLE_IO_CAP_DISPLAY_ONLY: the stack then generates a passkey and reports it through
 * BLE_EVENT_PASSKEY_DISPLAY for the user to read off, while the peer (with a keyboard) types it in.
 */
enum ble_io_cap {
	BLE_IO_CAP_DISPLAY_ONLY,
	BLE_IO_CAP_DISPLAY_YESNO,
	BLE_IO_CAP_KEYBOARD_ONLY,
	BLE_IO_CAP_NO_INPUT_OUTPUT,
	BLE_IO_CAP_KEYBOARD_DISPLAY,
};

/**
 * Security level required for a link, mirroring the Bluetooth SMP levels. Passkey Entry (a displayed or
 * entered PIN) provides man-in-the-middle protection, so it needs at least BLE_SEC_LEVEL_AUTH.
 */
enum ble_sec_level {
	BLE_SEC_LEVEL_NONE = 1,      /**< no encryption, no authentication */
	BLE_SEC_LEVEL_ENCRYPT = 2,   /**< encryption, no authentication (no MITM protection) */
	BLE_SEC_LEVEL_AUTH = 3,      /**< encryption and authentication (MITM protection) */
	BLE_SEC_LEVEL_AUTH_SC = 4,   /**< authenticated Secure Connections with a 128 bit key */
};


typedef struct ble Ble;
typedef struct ble_srv BleSrv;
typedef struct ble_char BleChar;


/**
 * A snapshot of the device's current link and traffic state, filled in by get_status. The event callback
 * has a single owner, so a consumer that does not own it (a status bar discovering the interface, say)
 * cannot observe connects, pairing or traffic through events; it polls this snapshot instead and shows a
 * connection/pairing icon with data-flow indicators. The same polling model will serve a future Wi-Fi
 * status. The byte counters are free-running totals accumulated since the driver started (they wrap at
 * 2^32); a poller detects live traffic by comparing successive samples rather than reading absolute values.
 */
struct ble_status {
	bool connected;      /**< a peer is currently connected */
	bool pairing;        /**< pairing is in progress (e.g. a passkey is being displayed/entered) */
	bool paired;         /**< the current connection completed pairing and the link is encrypted */
	uint32_t rx_bytes;   /**< total bytes received from peers (characteristic writes) */
	uint32_t tx_bytes;   /**< total bytes sent to peers (notifications and indications) */
};


/**
 * Asynchronous events reported by the device through the event callback. Connects, disconnects, peer
 * writes and subscription changes are all module initiated and cannot be polled synchronously.
 */
enum ble_event_type {
	/** A peer established a connection. @p conn identifies the new link. */
	BLE_EVENT_CONNECTED,

	/** A peer disconnected. @p conn identifies the closed link. */
	BLE_EVENT_DISCONNECTED,

	/** A peer wrote one of our characteristics. @p chr is the target, @p data / @p len the payload. */
	BLE_EVENT_WRITE,

	/** A peer changed a characteristic's CCCD. @p chr is the target, @p notify_en / @p indicate_en
	 *  give the new subscription state. */
	BLE_EVENT_SUBSCRIBE,

	/** A peer confirmed an indication we sent on @p chr. */
	BLE_EVENT_INDICATE_CONFIRM,

	/** A previously queued notification has been sent and its buffer freed. Used for flow control
	 *  when streaming: the sender may push the next chunk on @p chr. */
	BLE_EVENT_NOTIFY_TX_DONE,

	/** The security manager needs a passkey shown to the user so the peer can enter it during pairing
	 *  (the local device has a display). @p passkey holds the six digit value to display. */
	BLE_EVENT_PASSKEY_DISPLAY,

	/** Pairing (and bonding, when enabled) on @p conn finished successfully; the link is now encrypted. */
	BLE_EVENT_PAIRING_COMPLETE,

	/** Pairing on @p conn failed; the peer was not authenticated and the link stays unencrypted. */
	BLE_EVENT_PAIRING_FAILED,

	/* --- reserved for the client role, not emitted by a server only driver --- */

	/** An advertising report was received while scanning. */
	BLE_EVENT_SCAN_RESULT,

	/** A notification was received from a remote server we are subscribed to. */
	BLE_EVENT_NOTIFICATION,
};

struct ble_event {
	enum ble_event_type type;

	/** Link the event relates to. */
	ble_conn_t conn;

	/** Characteristic the event relates to (WRITE, SUBSCRIBE, INDICATE_CONFIRM), NULL otherwise. */
	BleChar *chr;

	/** Optional payload (WRITE, NOTIFICATION, SCAN_RESULT), valid only for the duration of the
	 *  callback. */
	const uint8_t *data;
	size_t len;

	/** Subscription state for BLE_EVENT_SUBSCRIBE. */
	bool notify_en;
	bool indicate_en;

	/** Passkey to display for BLE_EVENT_PASSKEY_DISPLAY (a six digit value, 0..999999). */
	uint32_t passkey;
};

/**
 * @brief Event callback
 *
 * Invoked by the driver when an asynchronous event occurs. @p cb_ctx is the opaque pointer supplied to
 * set_event_handler. The @p event and any buffers it references are valid only for the duration of the
 * call.
 */
typedef void (*ble_event_cb)(void *cb_ctx, const struct ble_event *event);


/**
 * A single characteristic within a GATT service. Preallocated by the caller and populated by
 * ble_srv_add_characteristic.
 */
struct ble_char_vmt {
	ble_ret_t (*set_value)(BleChar *self, const uint8_t *buf, size_t len);
	ble_ret_t (*get_value)(BleChar *self, uint8_t *buf, size_t size, size_t *len);
	ble_ret_t (*notify)(BleChar *self, ble_conn_t conn, const uint8_t *buf, size_t len);
	ble_ret_t (*indicate)(BleChar *self, ble_conn_t conn, const uint8_t *buf, size_t len);
};

struct ble_char {
	const struct ble_char_vmt *vmt;

	/** Owning service. */
	BleSrv *parent;

	/** Driver private per characteristic handle. */
	void *priv;
};


/**
 * A single GATT service. Preallocated by the caller and populated by ble_add_service.
 */
struct ble_srv_vmt {
	ble_ret_t (*add_characteristic)(BleSrv *self, const struct ble_uuid *uuid, uint32_t props,
	                                BleChar *chr);
	/* reserved: add_descriptor for non-CCCD descriptors */
};

struct ble_srv {
	const struct ble_srv_vmt *vmt;

	/** Owning device. */
	Ble *parent;

	/** Driver private per service handle. */
	void *priv;
};


/**
 * The root BLE device object. Built by the driver service, it owns the GATT server and the GAP state.
 */
struct ble_vmt {
	ble_ret_t (*start)(Ble *self);
	ble_ret_t (*stop)(Ble *self);
	ble_ret_t (*set_event_handler)(Ble *self, ble_event_cb cb, void *cb_ctx);

	/* Read a snapshot of the current link and traffic state into @p status. Independent of the event
	 * callback and safe to call from any task, so several consumers can poll the device concurrently. */
	ble_ret_t (*get_status)(Ble *self, struct ble_status *status);

	/* GATT server construction. The child object is preallocated by the caller and filled in here. */
	ble_ret_t (*add_service)(Ble *self, const struct ble_uuid *uuid, BleSrv *service);
	ble_ret_t (*server_start)(Ble *self);

	/* GAP / advertising (peripheral role). */
	ble_ret_t (*set_device_name)(Ble *self, const char *name);
	ble_ret_t (*set_appearance)(Ble *self, uint16_t appearance);
	ble_ret_t (*set_adv_data)(Ble *self, const uint8_t *adv, size_t adv_len,
	                          const uint8_t *scan_rsp, size_t rsp_len);
	ble_ret_t (*advertising_start)(Ble *self);
	ble_ret_t (*advertising_stop)(Ble *self);

	ble_ret_t (*disconnect)(Ble *self, ble_conn_t conn);

	/* Start the ATT MTU exchange on a link, requesting @p mtu as the size. Only a GATT client may drive the
	 * exchange over the air; a peripheral driver hands the request to its module. This returns as soon as the
	 * request is issued (BLE_RET_OK) or rejected; the negotiated size is reported asynchronously by the peer
	 * and read back with get_mtu. It matters because a notification or indication carries at most MTU minus 3
	 * bytes of value, so a link left at the 23 byte default caps a notification at 20 bytes and larger pushes
	 * fail. */
	ble_ret_t (*set_mtu)(Ble *self, ble_conn_t conn, uint16_t mtu);

	/* Report through @p mtu the ATT MTU last negotiated on the link, or 0 if no exchange has completed since
	 * the driver started. */
	ble_ret_t (*get_mtu)(Ble *self, uint16_t *mtu);

	/* Pairing / bonding (security manager). set_security configures the local IO capability and the
	 * security level required of a link; with BLE_IO_CAP_DISPLAY_ONLY and BLE_SEC_LEVEL_AUTH this selects
	 * Passkey Entry, where the stack reports the PIN to show through BLE_EVENT_PASSKEY_DISPLAY. pair starts
	 * authenticated pairing on an established connection at the configured level. */
	ble_ret_t (*set_security)(Ble *self, enum ble_io_cap io_cap, enum ble_sec_level level);
	ble_ret_t (*pair)(Ble *self, ble_conn_t conn);

	/* reserved: scan_start/scan_stop, connect/connect_cancel, discover_*, gattc_read/write/subscribe */
};

struct ble {
	const struct ble_vmt *vmt;

	/** Driver context. */
	void *parent;
};
