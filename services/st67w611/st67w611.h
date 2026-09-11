/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STMicroelectronics ST67W611M1 low-power Wi-Fi/BLE combo module driver
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <main.h>

#include <interfaces/spi.h>
#include <interfaces/gpio.h>
#include <interfaces/ble.h>

#define ST67W611_RESP_MAX 512

/* The module's GATT server holds at most three services (indices 0..2). */
#define ST67W611_GATTS_SRV_MAX 3

/* Each GATT service holds at most five characteristics (indices 0..4). */
#define ST67W611_GATTS_CHAR_MAX 5

/* Bytes of a characteristic's value cached locally to answer get_value; a longer value set is truncated
 * in the cache while the full value is still pushed to the module. */
#define ST67W611_CHAR_VALUE_MAX 64

typedef enum {
	ST67W611_RET_OK = 0,
	ST67W611_RET_FAILED,
	ST67W611_RET_TIMEOUT,
	ST67W611_RET_OVERFLOW,
	ST67W611_RET_NULL,
} st67w611_ret_t;

/* Connection security policy applied to inbound links. The default accepts connections without requiring
 * pairing; PAIR_ON_CONNECT makes the driver start pairing as soon as a peer connects (using the parameters
 * configured through the Ble interface's set_security) and drop the link if that pairing fails. Not
 * typedef'd per project policy. */
enum st67w611_conn_sec {
	ST67W611_CONN_SEC_OPEN = 0,
	ST67W611_CONN_SEC_PAIR_ON_CONNECT,
};

/* Service configuration passed to st67w611_init(). Not typedef'd per project policy. */
struct st67w611_conf {
	/** SPI device (chip-select bound) used for the module's mission-mode SPI interface. */
	SpiDev *spidev;
	/** CHIP_EN power-on control, driven high to enable the module. */
	Gpio *en_gpio;
	/** BOOT bootstrap select, sampled at power-on. Driven low to boot from SPI. */
	Gpio *boot_gpio;
	/** SPI_RDY handshake line, an input asserted high by the module. */
	Gpio *rdy_gpio;
};

/* Driver-private per-characteristic context, pointed to by BleChar.priv. Holds the module-side indices
 * used to address the characteristic in AT commands, plus a cached copy of the last value set so peer
 * reads can be answered locally through get_value. */
struct st67w611_char {
	/* The caller's BleChar this context backs, referenced when reporting events for the characteristic. */
	BleChar *chr;
	uint8_t srv_index;
	uint8_t char_index;
	/* Last subscription state reported by the peer, so a SUBSCRIBE event can carry both flags. */
	bool notify_en;
	bool indicate_en;
	size_t value_len;
	uint8_t value[ST67W611_CHAR_VALUE_MAX];
};

typedef struct {
	struct st67w611_conf conf;

	/* Generic BLE device interface exposed on top of the module's AT command set. */
	Ble ble;

	/* Number of GATT services created so far, used as the next service index. */
	uint8_t srv_count;

	/* Per-service count of characteristics created so far, used as the next characteristic index. */
	uint8_t srv_char_count[ST67W611_GATTS_SRV_MAX];

	/* Per-characteristic contexts, allocated in order as characteristics are added. */
	struct st67w611_char chars[ST67W611_GATTS_SRV_MAX * ST67W611_GATTS_CHAR_MAX];
	uint8_t char_total;

	/* Application event callback, invoked from the receive task when the module reports an asynchronous
	 * event (connect, disconnect, peer write, subscription change). */
	ble_event_cb event_cb;
	void *event_ctx;

	/* Cached link/traffic status maintained from the same events, so consumers that do not own the event
	 * callback can poll it through the Ble interface's get_status. */
	struct ble_status status;

	/* Security level requested through the Ble interface's set_security, passed on to AT+BLESECSTART when
	 * pairing is initiated. */
	enum ble_sec_level sec_level;

	/* The ATT MTU exchange reports its result as a "+BLE:MTUSIZE" frame that arrives asynchronously after
	 * AT+BLEEXCHANGEMTU has already answered OK. The receive task parses it and caches the negotiated size
	 * here (0 until the first exchange completes) for get_mtu to read back. */
	volatile uint16_t neg_mtu;

	/* Connection security policy and, while PAIR_ON_CONNECT is driving a pairing, the connection it applies
	 * to (a pairing failure reports only the peer address, so the connection index is remembered here to
	 * disconnect the right link). */
	enum st67w611_conn_sec conn_sec;
	ble_conn_t pending_sec_conn;

	/* Security task running the blocking pairing/disconnect AT commands the receive task cannot issue
	 * itself, fed through sec_queue, plus its keep-running flag. */
	TaskHandle_t sec_task;
	volatile bool sec_task_running;
	QueueHandle_t sec_queue;

	/* Background task draining module-initiated frames, and its keep-running flag. */
	TaskHandle_t rx_task;
	volatile bool rx_task_running;

	/* cmd_lock serializes commands so only one is in flight at a time. comm_lock serializes the actual
	 * SPI transactions between the command sender and the background receiver. cmd_sem is given by the
	 * receiver once the running command's response has been captured, unblocking the sender. cmd_prompt_sem
	 * is given when the module emits its '>' data prompt, unblocking the payload phase of a two-step
	 * (read/notify/indicate) command. */
	SemaphoreHandle_t cmd_lock;
	SemaphoreHandle_t comm_lock;
	SemaphoreHandle_t cmd_sem;
	SemaphoreHandle_t cmd_prompt_sem;

	/* Shared state of the command currently awaiting a response. Written by the sender before the
	 * command goes out, consulted and completed by the background task. */
	volatile bool cmd_running;

	/* Set while a two-step data command (AT+BLEGATTSNTFY / IND / non-empty RD) is still waiting for the
	 * module to accept its command line. This firmware first acknowledges with a bare "OK" and only then
	 * emits the ">" data prompt, so the background task must treat that interim "OK" as a ready ack rather
	 * than the command's final result (completing on it would skip the payload phase). */
	volatile bool cmd_wait_prompt;
	uint8_t resp_buf[ST67W611_RESP_MAX];
	size_t resp_len;
	bool resp_overflow;

	/* Scratch buffer the background task receives frames into. */
	uint8_t rx_buf[ST67W611_RESP_MAX];
} St67w611;


st67w611_ret_t st67w611_init(St67w611 *self, const struct st67w611_conf *conf);
st67w611_ret_t st67w611_free(St67w611 *self);

/* Send an AT command and return its response. Blocks until the module answers or the command times
 * out. Only one command runs at a time; concurrent callers are serialized. The response text (module
 * output up to and including the final OK/ERROR result) is copied into @p resp (NUL-terminated, at most
 * @p resp_size bytes) and its length returned via @p resp_len; both may be NULL if not needed. */
st67w611_ret_t st67w611_command(St67w611 *self, const char *cmd, char *resp, size_t resp_size, size_t *resp_len);

/* Return the generic BLE device interface through @p ble. */
st67w611_ret_t st67w611_get_ble(St67w611 *self, Ble **ble);

/* Set the connection security policy. With ST67W611_CONN_SEC_PAIR_ON_CONNECT the driver starts pairing on
 * every incoming connection and disconnects the peer if pairing fails; the pairing parameters (IO
 * capability, level) must first be configured through the Ble interface's set_security. */
st67w611_ret_t st67w611_set_conn_security(St67w611 *self, enum st67w611_conn_sec policy);
