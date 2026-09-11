/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STMicroelectronics ST67W611M1 low-power Wi-Fi/BLE combo module driver
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 *
 * The ST67W611M1 is an all-in-one Wi-Fi 6 / Bluetooth LE 5.4 coprocessor module. It runs its own
 * firmware from an on-module flash and exposes an AT-command network-coprocessor (NCP) interface to
 * the host over SPI (or UART). This driver talks to it over its mission-mode SPI interface.
 *
 * The SPI link is a full-duplex slave with an extra SPI_RDY handshake line. Every message is framed
 * with an 8-byte header (little-endian):
 *
 *   [0..1] sync word 0x55AA (low byte first)
 *   [2..3] payload length DL, a multiple of 4
 *   [4]    frame byte (version in bits[0:1], rx_stall in bit[2], flags in bits[3:7])
 *   [5]    traffic type (0x00 = AT commands, 0x01 = STA data, 0x02 = AP data)
 *   [6..7] reserved, 0x0000
 *
 * A transfer is only clocked while SPI_RDY is asserted: the module raises SPI_RDY when it has data to
 * hand over (or, for host-initiated transfers, once it is ready to accept the host's header) and
 * de-asserts it when the current frame is done. The host must not de-assert chip-select before
 * SPI_RDY has gone low again.
 *
 * st67w611_init() powers the module up, waits for the boot "ready" banner it emits over SPI, verifies
 * the link with an AT/OK round-trip and then starts a background task. That task continuously polls
 * SPI_RDY and drains every frame the module offers, because the module freely interleaves unsolicited
 * event messages with the responses to host commands. st67w611_command() sends one AT command at a
 * time (serialized by a command mutex) and blocks until the background task recognizes and captures its
 * response; unsolicited frames received while no command runs are handled separately.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <main.h>

#include <interfaces/spi.h>
#include <interfaces/gpio.h>

#include "st67w611.h"

#define MODULE_NAME "st67w611"

/* SPI frame header, transmitted little-endian. */
#define ST67W611_HEADER_LEN   8
#define ST67W611_SYNC_LO      0xaa
#define ST67W611_SYNC_HI      0x55

/* Frame byte (header[4]) bit fields. */
#define ST67W611_FRAME_RX_STALL  0x04  /* module could not accept the host's data this transaction */

/* Traffic type field values. */
#define ST67W611_TYPE_AT      0x00

/* The payload length must be a multiple of 4; the host pads short payloads with this byte value. */
#define ST67W611_PAD_BYTE     0x88
#define ST67W611_PAD_ALIGN    4

/* Retries budget (as a time window) for delivering a host-initiated frame that the module keeps
 * stalling. */
#define ST67W611_SEND_TIMEOUT_MS  1000

/* The boot banner and AT/OK responses are tiny, so a small probe buffer is plenty. */
#define ST67W611_PROBE_BUF    64

/* Time budget for the module to boot and assert SPI_RDY after being enabled, and for the shorter
 * intra-transaction handshake edges. */
#define ST67W611_BOOT_TIMEOUT_MS  3000
#define ST67W611_RDY_TIMEOUT_MS   100

/* Hold CHIP_EN low long enough to guarantee a full power-down (the datasheet shutdown sequence needs
 * a couple of ms for VDDCORE to collapse) so that every probe starts from a deterministic cold boot
 * and the module re-emits its "ready" banner. */
#define ST67W611_RESET_LOW_MS     10

/* Time budget for a command's response to arrive; some AT commands (scans, connects) take a while. */
#define ST67W611_CMD_TIMEOUT_MS   5000

#define ST67W611_RX_TASK_STACK    (configMINIMAL_STACK_SIZE + 256)
#define ST67W611_RX_TASK_PRIORITY 2

/* Upper bound on 8-byte chunks clocked out while draining a frame's trailer and any idle tail, a safety
 * valve so a module that holds SPI_RDY asserted indefinitely cannot spin the receive task forever. */
#define ST67W611_RX_DRAIN_MAX     64

#define ST67W611_SEC_TASK_STACK    (configMINIMAL_STACK_SIZE + 512)
#define ST67W611_SEC_TASK_PRIORITY 2
#define ST67W611_SEC_QUEUE_LEN     4
#define ST67W611_SEC_TASK_POLL_MS  100

/* Scratch buffer for building parametrized AT commands. */
#define ST67W611_CMD_MAX          64


/* Kinds of deferred security action the receive task hands off to the security task. These run as blocking
 * AT commands, which the receive task cannot issue itself (it would deadlock waiting for its own response). */
enum st67w611_sec_job {
	ST67W611_SEC_JOB_PAIR,
	ST67W611_SEC_JOB_DISCONNECT,
	ST67W611_SEC_JOB_CONN_PARAM,
};

struct st67w611_sec_msg {
	enum st67w611_sec_job job;
	ble_conn_t conn;
};


/* SPI_RDY is asserted high by the module. Poll it until it reaches the requested state or the
 * timeout expires. */
static st67w611_ret_t st67w611_wait_rdy(St67w611 *self, bool state, uint32_t timeout_ms) {
	TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
	while (true) {
		bool rdy = false;
		self->conf.rdy_gpio->vmt->get(self->conf.rdy_gpio, &rdy);
		if (rdy == state) {
			return ST67W611_RET_OK;
		}
		if (xTaskGetTickCount() >= deadline) {
			return ST67W611_RET_TIMEOUT;
		}
		vTaskDelay(1);
	}
}


/* Receive one module-initiated frame with chip-select already asserted by the caller and neither touching
 * chip-select nor SPI_RDY, so several frames can be drained back-to-back within one chip-select assertion.
 * The 8-byte header is read and its sync word validated, then the DL-byte payload is copied into @p buf and
 * its length returned in @p out_len (zero for the empty handshake frames the module interleaves). A payload
 * longer than @p bufsize is treated as a protocol error rather than silently truncated. */
static st67w611_ret_t st67w611_recv_one(St67w611 *self, uint8_t *buf, size_t bufsize, size_t *out_len) {
	uint8_t header[ST67W611_HEADER_LEN];
	if (self->conf.spidev->vmt->receive(self->conf.spidev, header, ST67W611_HEADER_LEN) != SPI_RET_OK) {
		return ST67W611_RET_FAILED;
	}
	if (header[0] != ST67W611_SYNC_LO || header[1] != ST67W611_SYNC_HI) {
		/* An all-ones header is just the idle bus: the module asserted SPI_RDY but is not driving data
		 * yet (e.g. busy between frames), so stay quiet. Anything else is an unexpected desync. */
		if (header[0] != 0xff || header[1] != 0xff) {
			u_log(system_log, LOG_TYPE_WARN,
			      U_LOG_MODULE_PREFIX("bad frame sync hdr %02x %02x %02x %02x %02x %02x %02x %02x"),
			      header[0], header[1], header[2], header[3], header[4], header[5], header[6], header[7]);
		}
		return ST67W611_RET_FAILED;
	}

	size_t dl = (size_t)header[2] | ((size_t)header[3] << 8);
	if (dl > bufsize) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("frame too large (%u bytes)"), (unsigned)dl);
		return ST67W611_RET_FAILED;
	}
	if (self->conf.spidev->vmt->receive(self->conf.spidev, buf, dl) != SPI_RET_OK) {
		return ST67W611_RET_FAILED;
	}
	*out_len = dl;
	return ST67W611_RET_OK;
}


/* After a frame's payload the module clocks out a short trailer (an extra CRLF and alignment padding) and
 * de-asserts SPI_RDY only once those bytes have been clocked; it also keeps SPI_RDY asserted while briefly
 * busy between frames. Clock out and discard bytes until SPI_RDY de-asserts, so chip-select is released on
 * a real frame boundary and the next frame stays byte-aligned. Just polling the line will not do: the
 * module needs SPI clocks to push the trailer out, so dummy bytes must actually be shifted. Bounded so a
 * line stuck high cannot spin forever. */
static void st67w611_drain_to_idle(St67w611 *self) {
	uint8_t dummy[ST67W611_HEADER_LEN];
	for (unsigned int i = 0; i < ST67W611_RX_DRAIN_MAX; i++) {
		bool rdy = false;
		self->conf.rdy_gpio->vmt->get(self->conf.rdy_gpio, &rdy);
		if (!rdy) {
			return;
		}
		if (self->conf.spidev->vmt->receive(self->conf.spidev, dummy, sizeof(dummy)) != SPI_RET_OK) {
			return;
		}
	}
}


/* Receive a single frame in its own chip-select assertion: select, read one frame, drain the trailer so
 * the module de-asserts SPI_RDY (chip-select may only be released between frames), then deselect. The drain
 * runs after a mis-synced read too, so alignment is recovered before the next frame. */
static st67w611_ret_t st67w611_recv_frame(St67w611 *self, uint8_t *buf, size_t bufsize, size_t *out_len) {
	if (self->conf.spidev->vmt->select(self->conf.spidev) != SPI_RET_OK) {
		return ST67W611_RET_FAILED;
	}
	st67w611_ret_t ret = st67w611_recv_one(self, buf, bufsize, out_len);
	st67w611_drain_to_idle(self);
	self->conf.spidev->vmt->deselect(self->conf.spidev);
	return ret;
}


/* Send one host-initiated frame and confirm the module accepted it. The 8-byte header is clocked out
 * first with a full-duplex exchange: the bytes shifted back carry the module's own header, whose
 * rx_stall bit reports whether it was able to receive. Only once the header is accepted is the payload
 * clocked out (its 0x88 padding tail in a second transfer), straight from the caller's buffer so no
 * frame-sized copy lands on the stack. The module continuously offers frames of its own, so a write can
 * be stalled; on a stall the frame is retransmitted in a fresh transaction until it is accepted or the
 * send window elapses. */
static st67w611_ret_t st67w611_send_frame(St67w611 *self, uint8_t type, const uint8_t *data, size_t len) {
	size_t padded = (len + (ST67W611_PAD_ALIGN - 1)) & ~(size_t)(ST67W611_PAD_ALIGN - 1);

	uint8_t header[ST67W611_HEADER_LEN];
	uint8_t resp[ST67W611_HEADER_LEN];
	header[0] = ST67W611_SYNC_LO;
	header[1] = ST67W611_SYNC_HI;
	header[2] = (uint8_t)(padded & 0xff);
	header[3] = (uint8_t)((padded >> 8) & 0xff);
	header[4] = 0x00;
	header[5] = type;
	header[6] = 0x00;
	header[7] = 0x00;

	/* Small constant tail of pad bytes appended after the payload so its length matches the DL field. */
	uint8_t pad[ST67W611_PAD_ALIGN];
	memset(pad, ST67W611_PAD_BYTE, sizeof(pad));

	TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(ST67W611_SEND_TIMEOUT_MS);
	while (xTaskGetTickCount() < deadline) {
		if (self->conf.spidev->vmt->select(self->conf.spidev) != SPI_RET_OK) {
			return ST67W611_RET_FAILED;
		}

		bool accepted = false;
		if (st67w611_wait_rdy(self, true, ST67W611_RDY_TIMEOUT_MS) == ST67W611_RET_OK &&
		    self->conf.spidev->vmt->exchange(self->conf.spidev, header, resp, ST67W611_HEADER_LEN) == SPI_RET_OK) {
			bool sync_ok = (resp[0] == ST67W611_SYNC_LO && resp[1] == ST67W611_SYNC_HI);
			accepted = sync_ok && !(resp[4] & ST67W611_FRAME_RX_STALL);
			if (accepted) {
				/* Header taken; clock out the payload and its padding. The dummy fill the module
				 * shifts back during this phase is discarded, so a plain send() suffices. */
				if (len > 0) {
					self->conf.spidev->vmt->send(self->conf.spidev, data, len);
				}
				if (padded > len) {
					self->conf.spidev->vmt->send(self->conf.spidev, pad, padded - len);
				}
			}
		}

		/* Chip-select may only be released once the module has de-asserted SPI_RDY. */
		st67w611_wait_rdy(self, false, ST67W611_RDY_TIMEOUT_MS);
		self->conf.spidev->vmt->deselect(self->conf.spidev);

		if (accepted) {
			return ST67W611_RET_OK;
		}
		/* Module stalled the write or was not offering a transaction; back off and retry. */
		vTaskDelay(pdMS_TO_TICKS(2));
	}
	return ST67W611_RET_TIMEOUT;
}


/* Read whole frames until a non-empty one arrives or the deadline elapses. The module precedes its
 * actual payload (the boot banner, an AT response, ...) with a number of empty zero-length handshake
 * frames, which are skipped here. The payload is returned NUL-terminated in @p buf with its length in
 * @p out_len. */
static st67w611_ret_t st67w611_read_payload(St67w611 *self, uint8_t *buf, size_t bufsize, size_t *out_len,
                                            uint32_t timeout_ms) {
	TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
	while (xTaskGetTickCount() < deadline) {
		size_t len = 0;
		if (st67w611_wait_rdy(self, true, ST67W611_RDY_TIMEOUT_MS) != ST67W611_RET_OK) {
			continue;
		}
		if (st67w611_recv_frame(self, buf, bufsize, &len) != ST67W611_RET_OK) {
			continue;
		}
		if (len > 0) {
			buf[len] = '\0';
			*out_len = len;
			return ST67W611_RET_OK;
		}
	}
	return ST67W611_RET_TIMEOUT;
}


/* Look up the characteristic context by its module-side service and characteristic indices. */
static struct st67w611_char *st67w611_find_char(St67w611 *self, unsigned srv_index, unsigned char_index) {
	for (uint8_t i = 0; i < self->char_total; i++) {
		if (self->chars[i].srv_index == srv_index && self->chars[i].char_index == char_index) {
			return &self->chars[i];
		}
	}
	return NULL;
}


/* Drive the connection security policy for a just-parsed event. Under ST67W611_CONN_SEC_PAIR_ON_CONNECT a
 * new connection triggers pairing and a pairing failure triggers a disconnect; both are blocking AT
 * commands, so they are handed to the security task through sec_queue rather than run here in the receive
 * task. A no-op under the default open policy. */
static void st67w611_apply_conn_security(St67w611 *self, const struct ble_event *ev) {
	if (self->conn_sec != ST67W611_CONN_SEC_PAIR_ON_CONNECT || self->sec_queue == NULL) {
		return;
	}

	struct st67w611_sec_msg msg;
	if (ev->type == BLE_EVENT_CONNECTED) {
		/* Remember which link is being secured so a later pairing failure (which reports only the peer
		 * address) can be tied back to a connection index, then ask the security task to start pairing. */
		self->pending_sec_conn = ev->conn;
		msg.job = ST67W611_SEC_JOB_PAIR;
		msg.conn = ev->conn;
		xQueueSend(self->sec_queue, &msg, 0);
	} else if (ev->type == BLE_EVENT_PAIRING_FAILED) {
		/* Pairing was refused; drop the connection so an unpaired peer cannot stay connected. The
		 * resulting +BLE:DISCONNECTED is what delivers the disconnected event to the application. */
		msg.job = ST67W611_SEC_JOB_DISCONNECT;
		msg.conn = self->pending_sec_conn;
		xQueueSend(self->sec_queue, &msg, 0);
	}
}


/* On a new connection, ask the module to renegotiate the link to the connection interval configured for
 * the driver. A shorter interval cuts the per-datagram round-trip time. The update is a blocking AT
 * command, so it is handed to the connection task through sec_queue rather than run in the receive task. */
static void st67w611_request_conn_params(St67w611 *self, const struct ble_event *ev) {
	if (ev->type != BLE_EVENT_CONNECTED || self->sec_queue == NULL) {
		return;
	}
	struct st67w611_sec_msg msg = {
		.job = ST67W611_SEC_JOB_CONN_PARAM,
		.conn = ev->conn,
	};
	xQueueSend(self->sec_queue, &msg, 0);
}


/* Update the cached link/traffic status from a just-parsed event so get_status can report it without the
 * event callback being registered. Runs in the receive task alongside event delivery. */
static void st67w611_track_status(St67w611 *self, const struct ble_event *ev) {
	switch (ev->type) {
		case BLE_EVENT_CONNECTED:
			self->status.connected = true;
			self->status.paired = false;
			/* Under the pair-on-connect policy pairing begins immediately for the new link. */
			//self->status.pairing = (self->conn_sec == ST67W611_CONN_SEC_PAIR_ON_CONNECT);
			break;
		case BLE_EVENT_DISCONNECTED:
			self->status.connected = false;
			self->status.paired = false;
			self->status.pairing = false;
			break;
		case BLE_EVENT_PASSKEY_DISPLAY:
			self->status.pairing = true;
			break;
		case BLE_EVENT_PAIRING_COMPLETE:
			self->status.paired = true;
			self->status.pairing = false;
			break;
		case BLE_EVENT_PAIRING_FAILED:
			self->status.paired = false;
			self->status.pairing = false;
			break;
		case BLE_EVENT_WRITE:
			self->status.rx_bytes += (uint32_t)ev->len;
			break;
		default:
			break;
	}
}


/* Parse an unsolicited "+BLE:..." event frame, drive the connection security policy and deliver it to the
 * application callback. Returns true if the frame was recognized as an event. Runs in the receive task, so
 * the callback must not call back into blocking driver methods; it should copy what it needs and defer. */
static bool st67w611_emit_event(St67w611 *self, const uint8_t *frame, size_t len) {
	struct ble_event ev;
	memset(&ev, 0, sizeof(ev));
	const char *s = (const char *)frame;

	if (!strncmp(s, "+BLE:CONNECTED:", 15)) {
		ev.type = BLE_EVENT_CONNECTED;
		ev.conn = (ble_conn_t)strtoul(s + 15, NULL, 10);
	} else if (!strncmp(s, "+BLE:DISCONNECTED:", 18)) {
		ev.type = BLE_EVENT_DISCONNECTED;
		ev.conn = (ble_conn_t)strtoul(s + 18, NULL, 10);
	} else if (!strncmp(s, "+BLE:GATTWRITE:", 15)) {
		/* +BLE:GATTWRITE:<conn>,<srv>,<char>,<len>,<data>. Parse the four decimal fields, then take the
		 * trailing <len> bytes verbatim (the payload is binary and may contain commas or NULs). */
		char *p = (char *)s + 15;
		unsigned long conn = strtoul(p, &p, 10);
		if (*p != ',') {
			return false;
		}
		unsigned long srv = strtoul(p + 1, &p, 10);
		if (*p != ',') {
			return false;
		}
		unsigned long chr = strtoul(p + 1, &p, 10);
		if (*p != ',') {
			return false;
		}
		unsigned long dlen = strtoul(p + 1, &p, 10);
		if (*p != ',') {
			return false;
		}
		struct st67w611_char *cc = st67w611_find_char(self, srv, chr);
		if (cc == NULL) {
			return false;
		}
		const uint8_t *data = (const uint8_t *)p + 1;
		size_t avail = (frame + len) - data;
		ev.type = BLE_EVENT_WRITE;
		ev.conn = (ble_conn_t)conn;
		ev.chr = cc->chr;
		ev.data = data;
		ev.len = (dlen < avail) ? dlen : avail;
	} else if (!strncmp(s, "+BLE:NOTIFICATION:", 18) || !strncmp(s, "+BLE:INDICATION:", 16)) {
		/* Subscription state (CCCD) change: +BLE:NOTIFICATION:<status>,<srv>,<char> (status 0/1) or
		 * +BLE:INDICATION:<status>,<srv>,<char> (status 0/1, or 2 = indication acknowledged). */
		bool is_ind = (s[5] == 'I');
		char *p = (char *)s + (is_ind ? 16 : 18);
		unsigned long status = strtoul(p, &p, 10);
		if (*p != ',') {
			return false;
		}
		unsigned long srv = strtoul(p + 1, &p, 10);
		if (*p != ',') {
			return false;
		}
		unsigned long chr = strtoul(p + 1, &p, 10);
		struct st67w611_char *cc = st67w611_find_char(self, srv, chr);
		if (cc == NULL) {
			return false;
		}
		if (is_ind && status == 2) {
			ev.type = BLE_EVENT_INDICATE_CONFIRM;
		} else {
			if (is_ind) {
				cc->indicate_en = (status == 1);
			} else {
				cc->notify_en = (status == 1);
			}
			ev.type = BLE_EVENT_SUBSCRIBE;
			ev.notify_en = cc->notify_en;
			ev.indicate_en = cc->indicate_en;
		}
		ev.chr = cc->chr;
	} else if (!strncmp(s, "+BLE:MTUSIZE:", 13)) {
		/* Result of the ATT MTU exchange, "+BLE:MTUSIZE:<conn>,<mtu>". It arrives asynchronously after
		 * AT+BLEEXCHANGEMTU has already answered OK, so cache the negotiated size for get_mtu. Skip the
		 * connection field and read the MTU. This report carries no application event. */
		const char *p = strchr(s + 13, ',');
		if (p != NULL) {
			self->neg_mtu = (uint16_t)strtoul(p + 1, NULL, 10);
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("ATT MTU negotiated: %u bytes"),
			      (unsigned)self->neg_mtu);
		}
		return true;
	} else if (!strncmp(s, "+BLE:CONNPARAM:", 15)) {
		/* The link's connection parameters were (re)negotiated: "+BLE:CONNPARAM:<idx>,<interval>,
		 * <latency>,<timeout>,<min_ce>,<max_ce>". It arrives after our AT+BLECONNPARAM request settles
		 * (and whenever the central changes them), reporting the values actually in effect. Interval is
		 * in 1.25 ms units and timeout in 10 ms units; log them in milliseconds. Carries no application
		 * event. */
		char *p = (char *)s + 15;
		strtoul(p, &p, 10);                            /* <idx> */
		unsigned long interval = (*p == ',') ? strtoul(p + 1, &p, 10) : 0;
		unsigned long latency = (*p == ',') ? strtoul(p + 1, &p, 10) : 0;
		unsigned long timeout = (*p == ',') ? strtoul(p + 1, &p, 10) : 0;
		u_log(system_log, LOG_TYPE_INFO,
		      U_LOG_MODULE_PREFIX("connection parameters: interval %lu.%02lu ms, latency %lu, timeout %lu ms"),
		      interval * 125 / 100, interval * 125 % 100, latency, timeout * 10);
		return true;
	} else if (!strncmp(s, "+BLE:PASSKEYDISPLAY:", 20)) {
		/* The security manager generated a passkey for the peer to enter; deliver it for display. The
		 * event carries no connection index, so leave conn at BLE_CONN_ANY. */
		ev.type = BLE_EVENT_PASSKEY_DISPLAY;
		ev.conn = BLE_CONN_ANY;
		ev.passkey = (uint32_t)strtoul(s + 20, NULL, 10);
	} else if (!strncmp(s, "+BLE:PAIRINGCOMPLETED:", 22)) {
		/* +BLE:PAIRINGCOMPLETED:<bonded> BTADDR: <addr> LTK: <ltk>; the peer address rather than a
		 * connection index is reported, so leave conn at BLE_CONN_ANY. */
		ev.type = BLE_EVENT_PAIRING_COMPLETE;
		ev.conn = BLE_CONN_ANY;
	} else if (!strncmp(s, "+BLE:PAIRINGFAILED:", 19)) {
		ev.type = BLE_EVENT_PAIRING_FAILED;
		ev.conn = BLE_CONN_ANY;
	} else {
		return false;
	}

	/* Track the link/traffic status and apply the security policy (which may hand pairing/disconnect work to
	 * the security task) before delivering the event upward. Both run even with no application callback. */
	st67w611_track_status(self, &ev);
	st67w611_apply_conn_security(self, &ev);
	st67w611_request_conn_params(self, &ev);

	if (self->event_cb != NULL) {
		self->event_cb(self->event_ctx, &ev);
	}
	return true;
}


/* Log one AT command line, its response or an unsolicited URC at DEBUG level, with surrounding CR/LF trimmed
 * so it shows on a single line. @p tag distinguishes the direction (">" sent, "<" received). Compiled out to
 * a no-op unless SERVICE_ST67W611_DEBUG is enabled, so the call sites' arguments are not evaluated either. */
#if defined(CONFIG_SERVICE_ST67W611_DEBUG)
static void st67w611_log_at(const char *tag, const char *text, size_t len) {
	while (len > 0 && (*text == '\r' || *text == '\n')) {
		text++;
		len--;
	}
	while (len > 0 && (text[len - 1] == '\r' || text[len - 1] == '\n')) {
		len--;
	}
	u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("%s %.*s"), tag, (int)len, text);
}
#else
#define st67w611_log_at(tag, text, len) ((void)0)
#endif


/* Process one frame the background task has just received. Runs with comm_lock held, so the command
 * state it touches is not concurrently modified by the sender. While a command is running the frame is
 * appended to that command's response; once the accumulated response carries a final result code the
 * command is completed and its waiter released. Frames received with no command running are unsolicited
 * event messages. */
static void st67w611_dispatch_frame(St67w611 *self, const uint8_t *frame, size_t len) {
	if (self->cmd_running) {
		size_t space = sizeof(self->resp_buf) - 1 - self->resp_len;
		size_t copy = (len < space) ? len : space;
		memcpy(self->resp_buf + self->resp_len, frame, copy);
		self->resp_len += copy;
		self->resp_buf[self->resp_len] = '\0';
		/* Note when the frame did not fit so the command can report ST67W611_RET_OVERFLOW. */
		if (copy < len) {
			self->resp_overflow = true;
		}

		if (self->cmd_wait_prompt) {
			/* A two-step data command (notify/indicate/non-empty read) is waiting for the module to signal
			 * it is ready for the payload. This firmware is not uniform: some data commands ack the command
			 * line with a bare "OK" (then also emit a '>' prompt), others answer only with the '>' prompt
			 * (e.g. AT+BLEGATTSRD). Take EITHER as the go-ahead to stream the payload, and keep the command
			 * running so the real result (a later "SEND OK"/"SEND FAIL") completes it. A busy notice
			 * ("busy p...") is only interim and matches neither, so it is ignored and the wait continues. An
			 * "ERROR" rejects the command outright, with no data phase, so it completes immediately. Check
			 * the frame, not the accumulated buffer, so a "SEND OK" tail seen later is not matched here. */
			if (strstr((const char *)frame, "ERROR") != NULL) {
				self->cmd_running = false;
				self->cmd_wait_prompt = false;
				xSemaphoreGive(self->cmd_sem);
				xSemaphoreGive(self->cmd_prompt_sem);
			} else if (strstr((const char *)frame, "OK") != NULL || memchr(frame, '>', len) != NULL) {
				self->cmd_wait_prompt = false;
				xSemaphoreGive(self->cmd_prompt_sem);
			}
			return;
		}

		/* Single-step command, or a data command past its ready ack awaiting the final result. AT responses
		 * terminate with a result code: "OK"/"ERROR", or "SEND OK"/"SEND FAIL" after a streamed payload.
		 * Check the just-received frame rather than the accumulated buffer, so completion is still detected
		 * once the buffer has overflowed and no longer holds the tail of the response. */
		if (strstr((const char *)frame, "OK") != NULL || strstr((const char *)frame, "ERROR") != NULL ||
		    strstr((const char *)frame, "FAIL") != NULL) {
			self->cmd_running = false;
			xSemaphoreGive(self->cmd_sem);
			/* Also wake a payload phase that may be waiting on the prompt, so an early completion does not
			 * stall it until the timeout. */
			xSemaphoreGive(self->cmd_prompt_sem);
		}
		return;
	}

	/* An unsolicited URC frame: log the raw line before parsing it into an event. */
	st67w611_log_at("<", (const char *)frame, len);

	if (st67w611_emit_event(self, frame, len)) {
		return;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("unsolicited: %s"), frame);
}


/* Background receive task. Continuously polls SPI_RDY and, whenever the module offers a frame, receives
 * and dispatches it. The receive transaction and the dispatch that follows run under comm_lock so they
 * are serialized against the command sender. */
static void st67w611_rx_task(void *p) {
	St67w611 *self = (St67w611 *)p;

	while (self->rx_task_running) {
		bool rdy = false;
		self->conf.rdy_gpio->vmt->get(self->conf.rdy_gpio, &rdy);
		if (!rdy) {
			vTaskDelay(1);
			continue;
		}

		xSemaphoreTake(self->comm_lock, portMAX_DELAY);
		/* Re-check under the lock: a command send may have consumed this frame while we blocked. */
		self->conf.rdy_gpio->vmt->get(self->conf.rdy_gpio, &rdy);
		if (rdy) {
			size_t len = 0;
			if (st67w611_recv_frame(self, self->rx_buf, sizeof(self->rx_buf) - 1, &len) == ST67W611_RET_OK &&
			    len > 0) {
				self->rx_buf[len] = '\0';
				st67w611_dispatch_frame(self, self->rx_buf, len);
			}
		}
		xSemaphoreGive(self->comm_lock);
	}

	vTaskDelete(NULL);
}


st67w611_ret_t st67w611_command(St67w611 *self, const char *cmd, char *resp, size_t resp_size, size_t *resp_len) {
	if (u_assert(self != NULL) ||
	    u_assert(cmd != NULL)) {
		return ST67W611_RET_NULL;
	}

	/* Only one command may be in flight at a time. */
	xSemaphoreTake(self->cmd_lock, portMAX_DELAY);
	st67w611_log_at(">", cmd, strlen(cmd));

	/* Arm the command and send it while holding comm_lock, so the background task cannot process any
	 * frame between marking the command running and finishing the send: that would let it match an
	 * unsolicited message as the response, or miss a response the module returns immediately. */
	xSemaphoreTake(self->comm_lock, portMAX_DELAY);
	self->resp_len = 0;
	self->resp_buf[0] = '\0';
	self->resp_overflow = false;
	self->cmd_running = true;
	/* Single-step command: complete on the first terminating result code, no data phase. */
	self->cmd_wait_prompt = false;
	/* Discard any stale completion signal left over from a previous command. */
	xSemaphoreTake(self->cmd_sem, 0);
	st67w611_ret_t ret = st67w611_send_frame(self, ST67W611_TYPE_AT, (const uint8_t *)cmd, strlen(cmd));
	xSemaphoreGive(self->comm_lock);

	if (ret != ST67W611_RET_OK) {
		xSemaphoreTake(self->comm_lock, portMAX_DELAY);
		self->cmd_running = false;
		xSemaphoreGive(self->comm_lock);
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("command '%s' not accepted"), cmd);
		xSemaphoreGive(self->cmd_lock);
		return ret;
	}

	/* Wait for the background task to capture the response. A "busy p..." frame is only an interim status
	 * (the module is busy but continues and delivers the real result in a later frame), so completion is
	 * driven purely by the terminating OK/ERROR and the wait naturally rides through it. */
	if (xSemaphoreTake(self->cmd_sem, pdMS_TO_TICKS(ST67W611_CMD_TIMEOUT_MS)) != pdTRUE) {
		xSemaphoreTake(self->comm_lock, portMAX_DELAY);
		self->cmd_running = false;
		xSemaphoreGive(self->comm_lock);
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("no response to '%s'"), cmd);
		xSemaphoreGive(self->cmd_lock);
		return ST67W611_RET_TIMEOUT;
	}

	st67w611_log_at("<", (const char *)self->resp_buf, self->resp_len);

	/* The response is now sitting in resp_buf; hand it back to the caller. */
	if (resp != NULL && resp_size > 0) {
		size_t copy = (self->resp_len < resp_size - 1) ? self->resp_len : resp_size - 1;
		memcpy(resp, self->resp_buf, copy);
		resp[copy] = '\0';
	}
	if (resp_len != NULL) {
		*resp_len = self->resp_len;
	}
	/* The response is captured (possibly truncated); report the overflow so the caller knows. */
	st67w611_ret_t cmd_ret = self->resp_overflow ? ST67W611_RET_OVERFLOW : ST67W611_RET_OK;

	xSemaphoreGive(self->cmd_lock);
	return cmd_ret;
}


/* Run a two-step command whose payload is streamed after the module answers the command line with a '>'
 * prompt (AT+BLEGATTSRD / AT+BLEGATTSNTFY / AT+BLEGATTSIND). The command line goes out first; once the
 * prompt arrives the @p len data bytes are clocked out as a second frame, and the function blocks until
 * the module reports the final result. Serialized against other commands by the same cmd_lock. */
static st67w611_ret_t st67w611_command_data(St67w611 *self, const char *cmd, const uint8_t *data, size_t len) {
	xSemaphoreTake(self->cmd_lock, portMAX_DELAY);
	st67w611_log_at(">", cmd, strlen(cmd));

	xSemaphoreTake(self->comm_lock, portMAX_DELAY);
	self->resp_len = 0;
	self->resp_buf[0] = '\0';
	self->resp_overflow = false;
	self->cmd_running = true;
	/* A non-empty data command waits for the module's "OK" ready ack before streaming its payload; a
	 * zero-length one has no data phase and completes on the first result code like a single-step command. */
	self->cmd_wait_prompt = (len > 0);
	/* Discard any stale completion/prompt signals left over from a previous command. */
	xSemaphoreTake(self->cmd_sem, 0);
	xSemaphoreTake(self->cmd_prompt_sem, 0);
	st67w611_ret_t ret = st67w611_send_frame(self, ST67W611_TYPE_AT, (const uint8_t *)cmd, strlen(cmd));
	xSemaphoreGive(self->comm_lock);

	if (ret != ST67W611_RET_OK) {
		xSemaphoreTake(self->comm_lock, portMAX_DELAY);
		self->cmd_running = false;
		xSemaphoreGive(self->comm_lock);
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("command '%s' not accepted"), cmd);
		xSemaphoreGive(self->cmd_lock);
		return ret;
	}

	/* Wait for the module's "OK" ready ack (or an early completion, e.g. an ERROR rejecting the command). */
	if (xSemaphoreTake(self->cmd_prompt_sem, pdMS_TO_TICKS(ST67W611_CMD_TIMEOUT_MS)) != pdTRUE) {
		xSemaphoreTake(self->comm_lock, portMAX_DELAY);
		self->cmd_running = false;
		xSemaphoreGive(self->comm_lock);
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("no ready ack for '%s'"), cmd);
		xSemaphoreGive(self->cmd_lock);
		return ST67W611_RET_TIMEOUT;
	}

	/* If the command already completed (no data phase, e.g. rejected), skip straight to the result. */
	if (self->cmd_running) {
		xSemaphoreTake(self->comm_lock, portMAX_DELAY);
		ret = st67w611_send_frame(self, ST67W611_TYPE_AT, data, len);
		xSemaphoreGive(self->comm_lock);
		if (ret != ST67W611_RET_OK) {
			xSemaphoreTake(self->comm_lock, portMAX_DELAY);
			self->cmd_running = false;
			xSemaphoreGive(self->comm_lock);
			u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("payload for '%s' not accepted"), cmd);
			xSemaphoreGive(self->cmd_lock);
			return ret;
		}

		/* Wait for the final SEND OK / ERROR after the payload. */
		if (xSemaphoreTake(self->cmd_sem, pdMS_TO_TICKS(ST67W611_CMD_TIMEOUT_MS)) != pdTRUE) {
			xSemaphoreTake(self->comm_lock, portMAX_DELAY);
			self->cmd_running = false;
			xSemaphoreGive(self->comm_lock);
			u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("no result for '%s'"), cmd);
			xSemaphoreGive(self->cmd_lock);
			return ST67W611_RET_TIMEOUT;
		}
	}

	st67w611_log_at("<", (const char *)self->resp_buf, self->resp_len);

	/* The module reports failures as a final "ERROR" (command rejected) or "SEND FAIL" (data phase failed);
	 * treat anything else as success. */
	st67w611_ret_t cmd_ret = (strstr((char *)self->resp_buf, "ERROR") != NULL ||
	                          strstr((char *)self->resp_buf, "FAIL") != NULL) ? ST67W611_RET_FAILED
	                                                                          : ST67W611_RET_OK;
	xSemaphoreGive(self->cmd_lock);
	return cmd_ret;
}


/**********************************************************************************************************************
 * Generic BLE device interface (interfaces/ble.h)
 *
 * The module drives a full BLE stack behind its AT command set, so the interface methods map almost one to
 * one onto AT commands. The peripheral (server) role is wired up: GAP advertising, GATT server
 * construction, the asynchronous event callback and Security Manager pairing (Passkey Entry with the PIN
 * displayed locally). Only the central (client) role is left unimplemented, its vmt slots staying NULL.
 **********************************************************************************************************************/

/* BLE advertising and scan-response payloads are at most 31 bytes on air. */
#define ST67W611_ADV_DATA_MAX 31

/* Buffer for building an AT+BLE...DATA command: the fixed wrapper plus the hex-encoded payload (two
 * characters per byte). */
#define ST67W611_ADV_CMD_MAX  96

/* Buffer for building an AT+BLEGATTS... command: the fixed wrapper plus a 128 bit UUID hex string. */
#define ST67W611_GATTS_CMD_MAX 96

/* The module accepts at most 244 bytes of characteristic value data per read/notify/indicate. */
#define ST67W611_GATTS_DATA_MAX 244


/* Run one AT command that only reports success or failure and map the module's answer to a ble_ret_t. The
 * response is captured so a final ERROR result (which st67w611_command still reports as OK, having simply
 * seen the terminating result code) can be told apart from a real OK. */
static ble_ret_t st67w611_ble_at(St67w611 *self, const char *cmd) {
	char resp[ST67W611_RESP_MAX];
	st67w611_ret_t ret = st67w611_command(self, cmd, resp, sizeof(resp), NULL);
	if (ret == ST67W611_RET_TIMEOUT) {
		return BLE_RET_TIMEOUT;
	}
	if (ret != ST67W611_RET_OK) {
		return BLE_RET_FAILED;
	}
	if (strstr(resp, "ERROR") != NULL) {
		return BLE_RET_FAILED;
	}
	return BLE_RET_OK;
}


/* Issue AT+BLEDISCONN for a connection. BLE_CONN_ANY maps to the module's default connection index 0. The
 * module answers the command and then emits +BLE:DISCONNECTED, which the receive task turns into a
 * BLE_EVENT_DISCONNECTED for the application. */
static ble_ret_t st67w611_conn_disconnect(St67w611 *self, ble_conn_t conn) {
	char cmd[ST67W611_CMD_MAX];
	snprintf(cmd, sizeof(cmd), "AT+BLEDISCONN=%u\r\n", (conn == BLE_CONN_ANY) ? 0u : (unsigned)conn);
	return st67w611_ble_at(self, cmd);
}


/* Connection task. Runs the blocking connection AT commands that connection events trigger - pairing and
 * disconnect for the pair-on-connect policy, and the connection-parameter update - handed to it by the
 * receive task through sec_queue (the receive task cannot run them itself as it would deadlock waiting for
 * its own response). */
static void st67w611_sec_task(void *p) {
	St67w611 *self = (St67w611 *)p;

	while (self->sec_task_running) {
		struct st67w611_sec_msg msg;
		if (xQueueReceive(self->sec_queue, &msg, pdMS_TO_TICKS(ST67W611_SEC_TASK_POLL_MS)) != pdTRUE) {
			continue;
		}
		switch (msg.job) {
			case ST67W611_SEC_JOB_PAIR: {
				/* Start authenticated pairing at the configured level. If the module will not even start
				 * it, drop the link so an unpaired peer cannot stay connected. */
				char cmd[ST67W611_CMD_MAX];
				snprintf(cmd, sizeof(cmd), "AT+BLESECSTART=%u,%u\r\n", (unsigned)msg.conn,
				         (unsigned)self->sec_level);
				if (st67w611_ble_at(self, cmd) != BLE_RET_OK) {
					st67w611_conn_disconnect(self, msg.conn);
				}
				break;
			}
			case ST67W611_SEC_JOB_DISCONNECT:
				st67w611_conn_disconnect(self, msg.conn);
				break;
			case ST67W611_SEC_JOB_CONN_PARAM: {
				/* Renegotiate the link to the configured (typically faster) connection interval so each
				 * request/response round trip costs fewer connection events. A rejected update is not
				 * fatal: the link simply stays at the parameters the central negotiated. */
				char cmd[ST67W611_CMD_MAX];
				snprintf(cmd, sizeof(cmd), "AT+BLECONNPARAM=%u,%u,%u,%u,%u\r\n",
				         (msg.conn == BLE_CONN_ANY) ? 0u : (unsigned)msg.conn,
				         (unsigned)CONFIG_SERVICE_ST67W611_CONN_PARAM_MIN_INTERVAL,
				         (unsigned)CONFIG_SERVICE_ST67W611_CONN_PARAM_MAX_INTERVAL,
				         (unsigned)CONFIG_SERVICE_ST67W611_CONN_PARAM_LATENCY,
				         (unsigned)CONFIG_SERVICE_ST67W611_CONN_PARAM_TIMEOUT);
				if (st67w611_ble_at(self, cmd) != BLE_RET_OK) {
					u_log(system_log, LOG_TYPE_WARN,
					      U_LOG_MODULE_PREFIX("connection parameter update rejected"));
				}
				break;
			}
			default:
				break;
		}
	}

	vTaskDelete(NULL);
}


/* Convert @p len raw bytes into a lowercase hex string (two characters per byte) written NUL-terminated to
 * @p out, which must hold at least 2 * @p len + 1 bytes. */
static void st67w611_to_hex(const uint8_t *data, size_t len, char *out) {
	static const char hex[] = "0123456789abcdef";
	for (size_t i = 0; i < len; i++) {
		out[i * 2] = hex[data[i] >> 4];
		out[i * 2 + 1] = hex[data[i] & 0x0f];
	}
	out[len * 2] = '\0';
}


/* Format a BLE UUID as the hex string the module's GATT commands expect (most significant octet first)
 * and report its AT uuid_type code (0 = 16 bit, 2 = 128 bit) through @p uuid_type. @p out must hold at
 * least 33 bytes. */
static void st67w611_uuid_to_hex(const struct ble_uuid *uuid, char *out, int *uuid_type) {
	static const char hex[] = "0123456789abcdef";
	if (uuid->type == BLE_UUID_16) {
		out[0] = hex[(uuid->u16 >> 12) & 0x0f];
		out[1] = hex[(uuid->u16 >> 8) & 0x0f];
		out[2] = hex[(uuid->u16 >> 4) & 0x0f];
		out[3] = hex[uuid->u16 & 0x0f];
		out[4] = '\0';
		*uuid_type = 0;
	} else {
		/* u128 is stored most significant octet first, the same order the module's hex string expects. */
		for (size_t i = 0; i < 16; i++) {
			out[i * 2] = hex[uuid->u128[i] >> 4];
			out[i * 2 + 1] = hex[uuid->u128[i] & 0x0f];
		}
		out[32] = '\0';
		*uuid_type = 2;
	}
}


static ble_ret_t ble_start(Ble *self) {
	if (self == NULL || self->parent == NULL) {
		return BLE_RET_NULL;
	}
	/* Initialize BLE in the server role (0 = deinit, 1 = client, 2 = server, 3 = dual). */
	return st67w611_ble_at((St67w611 *)self->parent, "AT+BLEINIT=2\r\n");
}


static ble_ret_t ble_stop(Ble *self) {
	if (self == NULL || self->parent == NULL) {
		return BLE_RET_NULL;
	}
	/* Deinitialize the BLE stack. */
	return st67w611_ble_at((St67w611 *)self->parent, "AT+BLEINIT=0\r\n");
}


static ble_ret_t ble_set_device_name(Ble *self, const char *name) {
	if (self == NULL || self->parent == NULL || name == NULL) {
		return BLE_RET_NULL;
	}
	char cmd[ST67W611_CMD_MAX];
	snprintf(cmd, sizeof(cmd), "AT+BLENAME=\"%s\"\r\n", name);
	return st67w611_ble_at((St67w611 *)self->parent, cmd);
}


static ble_ret_t ble_set_event_handler(Ble *self, ble_event_cb cb, void *cb_ctx) {
	if (self == NULL || self->parent == NULL) {
		return BLE_RET_NULL;
	}
	St67w611 *drv = (St67w611 *)self->parent;
	drv->event_cb = cb;
	drv->event_ctx = cb_ctx;
	return BLE_RET_OK;
}


static ble_ret_t ble_get_status(Ble *self, struct ble_status *status) {
	if (self == NULL || self->parent == NULL || status == NULL) {
		return BLE_RET_NULL;
	}
	St67w611 *drv = (St67w611 *)self->parent;

	/* Hand back a copy of the cached status. The fields are maintained from the receive task (and tx_bytes
	 * from the caller of notify/indicate); a poller only compares samples, so a snapshot without locking is
	 * enough and keeps this callable from any task without blocking. */
	memcpy(status, &drv->status, sizeof(*status));
	return BLE_RET_OK;
}


static ble_ret_t ble_set_appearance(Ble *self, uint16_t appearance) {
	if (self == NULL || self->parent == NULL) {
		return BLE_RET_NULL;
	}
	char cmd[ST67W611_CMD_MAX];
	snprintf(cmd, sizeof(cmd), "AT+BLESETGAPAPPEARANCE=%u\r\n", (unsigned)appearance);
	return st67w611_ble_at((St67w611 *)self->parent, cmd);
}


static ble_ret_t ble_set_adv_data(Ble *self, const uint8_t *adv, size_t adv_len,
                                  const uint8_t *scan_rsp, size_t rsp_len) {
	if (self == NULL || self->parent == NULL) {
		return BLE_RET_NULL;
	}
	if (adv_len > ST67W611_ADV_DATA_MAX || rsp_len > ST67W611_ADV_DATA_MAX) {
		return BLE_RET_BAD_ARG;
	}

	char cmd[ST67W611_ADV_CMD_MAX];
	char hex[ST67W611_ADV_DATA_MAX * 2 + 1];

	if (adv != NULL && adv_len > 0) {
		st67w611_to_hex(adv, adv_len, hex);
		snprintf(cmd, sizeof(cmd), "AT+BLEADVDATA=\"%s\"\r\n", hex);
		ble_ret_t ret = st67w611_ble_at((St67w611 *)self->parent, cmd);
		if (ret != BLE_RET_OK) {
			return ret;
		}
	}
	if (scan_rsp != NULL && rsp_len > 0) {
		st67w611_to_hex(scan_rsp, rsp_len, hex);
		snprintf(cmd, sizeof(cmd), "AT+BLESCANRSPDATA=\"%s\"\r\n", hex);
		ble_ret_t ret = st67w611_ble_at((St67w611 *)self->parent, cmd);
		if (ret != BLE_RET_OK) {
			return ret;
		}
	}
	return BLE_RET_OK;
}


static ble_ret_t ble_advertising_start(Ble *self) {
	if (self == NULL || self->parent == NULL) {
		return BLE_RET_NULL;
	}
	return st67w611_ble_at((St67w611 *)self->parent, "AT+BLEADVSTART\r\n");
}


static ble_ret_t ble_advertising_stop(Ble *self) {
	if (self == NULL || self->parent == NULL) {
		return BLE_RET_NULL;
	}
	return st67w611_ble_at((St67w611 *)self->parent, "AT+BLEADVSTOP\r\n");
}


static ble_ret_t ble_disconnect(Ble *self, ble_conn_t conn) {
	if (self == NULL || self->parent == NULL) {
		return BLE_RET_NULL;
	}
	return st67w611_conn_disconnect((St67w611 *)self->parent, conn);
}


static ble_ret_t ble_set_mtu(Ble *self, ble_conn_t conn, uint16_t mtu) {
	if (self == NULL || self->parent == NULL) {
		return BLE_RET_NULL;
	}

	/* Start the ATT MTU exchange on the link. The AT reference documents only the connection index, but the
	 * module also accepts a preferred size as a second field; pass the requested MTU so the peripheral can
	 * drive the link above the 23 byte default that caps a notification at 20 bytes. A BLE_CONN_ANY target
	 * maps to the module's default connection index 0. The negotiated size is reported back asynchronously as
	 * a "+BLE:MTUSIZE" frame the receive task caches; read it with get_mtu. */
	char cmd[ST67W611_CMD_MAX];
	snprintf(cmd, sizeof(cmd), "AT+BLEEXCHANGEMTU=%u,%u\r\n", (conn == BLE_CONN_ANY) ? 0u : (unsigned)conn,
	         (unsigned)mtu);
	return st67w611_ble_at((St67w611 *)self->parent, cmd);
}


static ble_ret_t ble_get_mtu(Ble *self, uint16_t *mtu) {
	if (self == NULL || self->parent == NULL || mtu == NULL) {
		return BLE_RET_NULL;
	}
	/* Hand back the size cached from the last "+BLE:MTUSIZE" report, or 0 if no exchange has completed. */
	*mtu = ((St67w611 *)self->parent)->neg_mtu;
	return BLE_RET_OK;
}


static ble_ret_t ble_set_security(Ble *self, enum ble_io_cap io_cap, enum ble_sec_level level) {
	if (self == NULL || self->parent == NULL) {
		return BLE_RET_NULL;
	}
	if (level < BLE_SEC_LEVEL_NONE || level > BLE_SEC_LEVEL_AUTH_SC) {
		return BLE_RET_BAD_ARG;
	}
	St67w611 *drv = (St67w611 *)self->parent;

	/* Map the interface IO capability onto the module's <security parameter> code. */
	unsigned int param = 0;
	switch (io_cap) {
		case BLE_IO_CAP_DISPLAY_ONLY:
			param = 0;
			break;
		case BLE_IO_CAP_DISPLAY_YESNO:
			param = 1;
			break;
		case BLE_IO_CAP_KEYBOARD_ONLY:
			param = 2;
			break;
		case BLE_IO_CAP_NO_INPUT_OUTPUT:
			param = 3;
			break;
		case BLE_IO_CAP_KEYBOARD_DISPLAY:
			param = 4;
			break;
		default:
			return BLE_RET_BAD_ARG;
	}

	/* Remember the requested level for pair(); AT+BLESECSTART must request at least the level set here.
	 * Leave the optional <security level> field off AT+BLESECPARAM so pair() alone fixes the effective
	 * level. */
	drv->sec_level = level;
	char cmd[ST67W611_CMD_MAX];
	snprintf(cmd, sizeof(cmd), "AT+BLESECPARAM=%u\r\n", param);
	return st67w611_ble_at(drv, cmd);
}


static ble_ret_t ble_pair(Ble *self, ble_conn_t conn) {
	if (self == NULL || self->parent == NULL) {
		return BLE_RET_NULL;
	}
	St67w611 *drv = (St67w611 *)self->parent;

	/* Start authenticated pairing at the level configured through set_security. A BLE_CONN_ANY target maps
	 * to the module's default connection index 0. */
	drv->status.pairing = true;
	char cmd[ST67W611_CMD_MAX];
	snprintf(cmd, sizeof(cmd), "AT+BLESECSTART=%u,%u\r\n", (conn == BLE_CONN_ANY) ? 0u : (unsigned)conn,
	         (unsigned)drv->sec_level);
	return st67w611_ble_at(drv, cmd);
}


static ble_ret_t ble_char_set_value(BleChar *self, const uint8_t *buf, size_t len) {
	if (self == NULL || self->priv == NULL || self->parent == NULL || self->parent->parent == NULL ||
	    self->parent->parent->parent == NULL || (buf == NULL && len > 0)) {
		return BLE_RET_NULL;
	}
	if (len > ST67W611_GATTS_DATA_MAX) {
		return BLE_RET_BAD_ARG;
	}
	St67w611 *drv = (St67w611 *)self->parent->parent->parent;
	struct st67w611_char *cc = (struct st67w611_char *)self->priv;

	/* Set the value the server returns on a read. A zero length clears it immediately with no payload
	 * phase; command_data handles that (the module completes without a '>' prompt). */
	char cmd[ST67W611_CMD_MAX];
	snprintf(cmd, sizeof(cmd), "AT+BLEGATTSRD=%u,%u,%u\r\n", (unsigned)cc->srv_index,
	         (unsigned)cc->char_index, (unsigned)len);
	if (st67w611_command_data(drv, cmd, buf, len) != ST67W611_RET_OK) {
		return BLE_RET_FAILED;
	}

	/* Cache the value (truncated to the local buffer) so get_value can answer without the module. */
	size_t cache = (len < sizeof(cc->value)) ? len : sizeof(cc->value);
	if (cache > 0) {
		memcpy(cc->value, buf, cache);
	}
	cc->value_len = cache;
	return BLE_RET_OK;
}


static ble_ret_t ble_char_get_value(BleChar *self, uint8_t *buf, size_t size, size_t *len) {
	if (self == NULL || self->priv == NULL || buf == NULL || len == NULL) {
		return BLE_RET_NULL;
	}
	/* Answer from the locally cached copy of the last value set. */
	struct st67w611_char *cc = (struct st67w611_char *)self->priv;
	size_t copy = (cc->value_len < size) ? cc->value_len : size;
	memcpy(buf, cc->value, copy);
	*len = copy;
	return BLE_RET_OK;
}


/* Shared body of notify and indicate: both take the same arguments and differ only in the AT operation
 * ("NTFY" or "IND"). The payload is streamed after the module's '>' prompt. */
static ble_ret_t st67w611_char_send(BleChar *self, const char *op, ble_conn_t conn, const uint8_t *buf,
                                    size_t len) {
	if (self == NULL || self->priv == NULL || self->parent == NULL || self->parent->parent == NULL ||
	    self->parent->parent->parent == NULL || buf == NULL) {
		return BLE_RET_NULL;
	}
	if (len == 0 || len > ST67W611_GATTS_DATA_MAX) {
		return BLE_RET_BAD_ARG;
	}
	St67w611 *drv = (St67w611 *)self->parent->parent->parent;
	struct st67w611_char *cc = (struct st67w611_char *)self->priv;

	/* This firmware's AT+BLEGATTSNTFY/IND take only <srv_index>,<char_index>,<length>; the trailing
	 * <conn_index> other versions document is rejected, so it is omitted and the module notifies its single
	 * connection. @p conn is therefore not used. */
	(void)conn;
	char cmd[ST67W611_CMD_MAX];
	snprintf(cmd, sizeof(cmd), "AT+BLEGATTS%s=%u,%u,%u\r\n", op, (unsigned)cc->srv_index,
	         (unsigned)cc->char_index, (unsigned)len);
	if (st67w611_command_data(drv, cmd, buf, len) != ST67W611_RET_OK) {
		return BLE_RET_FAILED;
	}
	drv->status.tx_bytes += (uint32_t)len;
	return BLE_RET_OK;
}


static ble_ret_t ble_char_notify(BleChar *self, ble_conn_t conn, const uint8_t *buf, size_t len) {
	return st67w611_char_send(self, "NTFY", conn, buf, len);
}


static ble_ret_t ble_char_indicate(BleChar *self, ble_conn_t conn, const uint8_t *buf, size_t len) {
	return st67w611_char_send(self, "IND", conn, buf, len);
}


static const struct ble_char_vmt ble_char_vmt = {
	.set_value = ble_char_set_value,
	.get_value = ble_char_get_value,
	.notify = ble_char_notify,
	.indicate = ble_char_indicate,
};


static ble_ret_t ble_add_characteristic(BleSrv *self, const struct ble_uuid *uuid, uint32_t props,
                                        BleChar *chr) {
	if (self == NULL || self->parent == NULL || self->parent->parent == NULL || uuid == NULL || chr == NULL) {
		return BLE_RET_NULL;
	}
	St67w611 *drv = (St67w611 *)self->parent->parent;
	uint8_t srv_index = (uint8_t)(uintptr_t)self->priv;
	if (srv_index >= ST67W611_GATTS_SRV_MAX) {
		return BLE_RET_BAD_STATE;
	}
	if (drv->srv_char_count[srv_index] >= ST67W611_GATTS_CHAR_MAX) {
		return BLE_RET_NOMEM;
	}

	char uuid_hex[33];
	int uuid_type = 0;
	st67w611_uuid_to_hex(uuid, uuid_hex, &uuid_type);

	/* Translate the interface property bit mask into the module's <char_prop> encoding and derive the
	 * <char_perm> (1 = read, 2 = write) from the readable/writable properties. */
	uint32_t at_prop = 0;
	uint32_t at_perm = 0;
	if (props & BLE_CHAR_PROP_READ) {
		at_prop |= 2;
		at_perm |= 1;
	}
	if (props & BLE_CHAR_PROP_WRITE) {
		at_prop |= 8;
		at_perm |= 2;
	}
	if (props & BLE_CHAR_PROP_WRITE_NR) {
		at_prop |= 4;
		at_perm |= 2;
	}
	if (props & BLE_CHAR_PROP_NOTIFY) {
		at_prop |= 16;
	}
	if (props & BLE_CHAR_PROP_INDICATE) {
		at_prop |= 32;
	}

	uint8_t char_index = drv->srv_char_count[srv_index];
	char cmd[ST67W611_GATTS_CMD_MAX];
	snprintf(cmd, sizeof(cmd), "AT+BLEGATTSCHARCRE=%u,%u,\"%s\",%u,%u,%d\r\n", (unsigned)srv_index,
	         (unsigned)char_index, uuid_hex, (unsigned)at_prop, (unsigned)at_perm, uuid_type);
	ble_ret_t ret = st67w611_ble_at(drv, cmd);
	if (ret != BLE_RET_OK) {
		return ret;
	}

	/* Allocate the next per-characteristic context and hand it to the caller's BleChar via priv. */
	struct st67w611_char *cc = &drv->chars[drv->char_total];
	cc->chr = chr;
	cc->srv_index = srv_index;
	cc->char_index = char_index;
	cc->notify_en = false;
	cc->indicate_en = false;
	cc->value_len = 0;

	chr->vmt = &ble_char_vmt;
	chr->parent = self;
	chr->priv = cc;
	drv->srv_char_count[srv_index]++;
	drv->char_total++;
	return BLE_RET_OK;
}


static const struct ble_srv_vmt ble_srv_vmt = {
	.add_characteristic = ble_add_characteristic,
};


static ble_ret_t ble_add_service(Ble *self, const struct ble_uuid *uuid, BleSrv *service) {
	if (self == NULL || self->parent == NULL || uuid == NULL || service == NULL) {
		return BLE_RET_NULL;
	}
	St67w611 *drv = (St67w611 *)self->parent;
	if (drv->srv_count >= ST67W611_GATTS_SRV_MAX) {
		return BLE_RET_NOMEM;
	}

	char uuid_hex[33];
	int uuid_type = 0;
	st67w611_uuid_to_hex(uuid, uuid_hex, &uuid_type);

	/* Create a primary service (srv_type 1) at the next free index. */
	char cmd[ST67W611_GATTS_CMD_MAX];
	snprintf(cmd, sizeof(cmd), "AT+BLEGATTSSRVCRE=%u,\"%s\",1,%d\r\n", (unsigned)drv->srv_count, uuid_hex,
	         uuid_type);
	ble_ret_t ret = st67w611_ble_at(drv, cmd);
	if (ret != BLE_RET_OK) {
		return ret;
	}

	service->vmt = &ble_srv_vmt;
	service->parent = self;
	service->priv = (void *)(uintptr_t)drv->srv_count;
	drv->srv_count++;
	return BLE_RET_OK;
}


static ble_ret_t ble_server_start(Ble *self) {
	if (self == NULL || self->parent == NULL) {
		return BLE_RET_NULL;
	}
	/* Register the created services with the GATT server. */
	return st67w611_ble_at((St67w611 *)self->parent, "AT+BLEGATTSREGISTER=1\r\n");
}


static const struct ble_vmt ble_vmt = {
	.start = ble_start,
	.stop = ble_stop,
	.set_event_handler = ble_set_event_handler,
	.get_status = ble_get_status,
	.add_service = ble_add_service,
	.server_start = ble_server_start,
	.set_device_name = ble_set_device_name,
	.set_appearance = ble_set_appearance,
	.set_adv_data = ble_set_adv_data,
	.advertising_start = ble_advertising_start,
	.advertising_stop = ble_advertising_stop,
	.disconnect = ble_disconnect,
	.set_mtu = ble_set_mtu,
	.get_mtu = ble_get_mtu,
	.set_security = ble_set_security,
	.pair = ble_pair,
};


/* Power the module up and confirm it is present and speaking the SPI NCP protocol, following the
 * documented start-up sequence: enable, wait for the "ready" banner, then verify with an AT/OK
 * round-trip. */
static st67w611_ret_t st67w611_probe(St67w611 *self) {
	/* BOOT low selects booting the NCP firmware over SPI. Power-cycle the module first: an already
	 * running module (e.g. after an MCU-only reset that left CHIP_EN high) would not re-emit its boot
	 * banner, so force it fully off before enabling it again for a deterministic cold boot. */
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("NCP module reset"));
	self->conf.boot_gpio->vmt->set(self->conf.boot_gpio, false);
	self->conf.en_gpio->vmt->set(self->conf.en_gpio, false);
	vTaskDelay(pdMS_TO_TICKS(ST67W611_RESET_LOW_MS));
	self->conf.en_gpio->vmt->set(self->conf.en_gpio, true);

	/* After boot the module emits a few empty handshake frames followed by the "\r\nready\r\n" banner.
	 * Read frames until the first non-empty one and check it. */
	uint8_t buf[ST67W611_PROBE_BUF];
	size_t len = 0;
	if (st67w611_read_payload(self, buf, sizeof(buf) - 1, &len, ST67W611_BOOT_TIMEOUT_MS) != ST67W611_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("boot failed"));
		return ST67W611_RET_TIMEOUT;
	}
	if (strstr((char *)buf, "ready") == NULL) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("unexpected boot banner (%u bytes)"), (unsigned)len);
		goto err;
	}
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("boot ok"));

	/* Check if the module (NCP) responds. */
	static const char at_cmd[] = "at\r\n";
	if (st67w611_send_frame(self, ST67W611_TYPE_AT, (const uint8_t *)at_cmd, strlen(at_cmd)) != ST67W611_RET_OK) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("AT command not acknowledged"));
		goto err;
	}
	if (st67w611_read_payload(self, buf, sizeof(buf) - 1, &len, ST67W611_BOOT_TIMEOUT_MS) != ST67W611_RET_OK) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("no response to AT command"));
		goto err;
	}
	if (strstr((char *)buf, "OK") == NULL) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("unexpected AT response '%s'"), buf);
		goto err;
	}

	return ST67W611_RET_OK;
err:
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("NCP not responding"));
	return ST67W611_RET_FAILED;
}


st67w611_ret_t st67w611_init(St67w611 *self, const struct st67w611_conf *conf) {
	if (u_assert(self != NULL) ||
	    u_assert(conf != NULL) ||
	    u_assert(conf->spidev != NULL) ||
	    u_assert(conf->en_gpio != NULL) ||
	    u_assert(conf->boot_gpio != NULL) ||
	    u_assert(conf->rdy_gpio != NULL)) {
		return ST67W611_RET_NULL;
	}
	memset(self, 0, sizeof(St67w611));
	memcpy(&self->conf, conf, sizeof(struct st67w611_conf));

	/* Default the pairing level to authenticated (MITM) so pair() is meaningful even if the application
	 * never calls set_security; set_security overrides it. */
	self->sec_level = BLE_SEC_LEVEL_AUTH;

	/* Probe synchronously before the background task takes over the SPI link. */
	if (st67w611_probe(self) != ST67W611_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("probe failed"));
		return ST67W611_RET_FAILED;
	}

	self->cmd_lock = xSemaphoreCreateMutex();
	self->comm_lock = xSemaphoreCreateMutex();
	self->cmd_sem = xSemaphoreCreateBinary();
	self->cmd_prompt_sem = xSemaphoreCreateBinary();
	self->sec_queue = xQueueCreate(ST67W611_SEC_QUEUE_LEN, sizeof(struct st67w611_sec_msg));
	if (self->cmd_lock == NULL || self->comm_lock == NULL || self->cmd_sem == NULL ||
	    self->cmd_prompt_sem == NULL || self->sec_queue == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create synchronization primitives"));
		return ST67W611_RET_FAILED;
	}

	self->rx_task_running = true;
	if (xTaskCreate(st67w611_rx_task, "st67w611-rx", ST67W611_RX_TASK_STACK, (void *)self,
	                ST67W611_RX_TASK_PRIORITY, &self->rx_task) != pdPASS) {
		self->rx_task_running = false;
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start receive task"));
		return ST67W611_RET_FAILED;
	}

	/* The security task issues blocking AT commands whose responses the receive task captures, so it is
	 * started only after the receive task is up. */
	self->sec_task_running = true;
	if (xTaskCreate(st67w611_sec_task, "st67w611-sec", ST67W611_SEC_TASK_STACK, (void *)self,
	                ST67W611_SEC_TASK_PRIORITY, &self->sec_task) != pdPASS) {
		self->sec_task_running = false;
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start security task"));
		return ST67W611_RET_FAILED;
	}

	/* Expose the generic BLE device interface now that the command path is up. */
	self->ble.vmt = &ble_vmt;
	self->ble.parent = self;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("init ok"));

	/* Query the module firmware version through the background-task command path now that it is up. */
	char resp[ST67W611_RESP_MAX];
	if (st67w611_command(self, "AT+GMR\r\n", resp, sizeof(resp), NULL) == ST67W611_RET_OK) {
		/* Print each response line with its own u_log so the version block stays readable in the log. */
		for (char *line = strtok(resp, "\r\n"); line != NULL; line = strtok(NULL, "\r\n")) {
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("%s"), line);
		}
	} else {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("AT+GMR failed"));
	}

	return ST67W611_RET_OK;
}


st67w611_ret_t st67w611_free(St67w611 *self) {
	if (u_assert(self != NULL)) {
		return ST67W611_RET_NULL;
	}

	/* Ask the background tasks to stop; each deletes itself once it observes the flag. */
	self->rx_task_running = false;
	self->sec_task_running = false;

	if (self->cmd_lock != NULL) {
		vSemaphoreDelete(self->cmd_lock);
	}
	if (self->comm_lock != NULL) {
		vSemaphoreDelete(self->comm_lock);
	}
	if (self->cmd_sem != NULL) {
		vSemaphoreDelete(self->cmd_sem);
	}
	if (self->cmd_prompt_sem != NULL) {
		vSemaphoreDelete(self->cmd_prompt_sem);
	}
	if (self->sec_queue != NULL) {
		vQueueDelete(self->sec_queue);
	}
	return ST67W611_RET_OK;
}


st67w611_ret_t st67w611_get_ble(St67w611 *self, Ble **ble) {
	if (u_assert(self != NULL) ||
	    u_assert(ble != NULL)) {
		return ST67W611_RET_NULL;
	}
	*ble = &self->ble;
	return ST67W611_RET_OK;
}


st67w611_ret_t st67w611_set_conn_security(St67w611 *self, enum st67w611_conn_sec policy) {
	if (u_assert(self != NULL)) {
		return ST67W611_RET_NULL;
	}
	self->conn_sec = policy;
	return ST67W611_RET_OK;
}
