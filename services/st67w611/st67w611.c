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

/* Scratch buffer for building parametrized AT commands. */
#define ST67W611_CMD_MAX          64


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


/* Receive one module-initiated frame. SPI_RDY is expected to be already asserted by the caller. The
 * payload is copied into @p buf and its length is returned in @p out_len; the SPI layer handles the
 * whole transfer in one call, so no chunking is done here. A payload longer than @p bufsize is treated
 * as a protocol error rather than silently truncated. */
static st67w611_ret_t st67w611_recv_frame(St67w611 *self, uint8_t *buf, size_t bufsize, size_t *out_len) {
	st67w611_ret_t ret = ST67W611_RET_FAILED;

	if (self->conf.spidev->vmt->select(self->conf.spidev) != SPI_RET_OK) {
		return ST67W611_RET_FAILED;
	}

	uint8_t header[ST67W611_HEADER_LEN];
	if (self->conf.spidev->vmt->receive(self->conf.spidev, header, ST67W611_HEADER_LEN) != SPI_RET_OK) {
		goto out;
	}
	if (header[0] != ST67W611_SYNC_LO || header[1] != ST67W611_SYNC_HI) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("bad frame sync 0x%02x%02x"), header[1], header[0]);
		goto out;
	}

	size_t dl = (size_t)header[2] | ((size_t)header[3] << 8);
	if (dl > bufsize) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("frame too large (%u bytes)"), (unsigned)dl);
		goto out;
	}
	if (self->conf.spidev->vmt->receive(self->conf.spidev, buf, dl) != SPI_RET_OK) {
		goto out;
	}

	/* Chip-select may only be released once the module has de-asserted SPI_RDY. */
	st67w611_wait_rdy(self, false, ST67W611_RDY_TIMEOUT_MS);
	*out_len = dl;
	ret = ST67W611_RET_OK;

out:
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

		/* AT responses terminate with a final result code; that is what marks the command complete.
		 * Check the just-received frame rather than the accumulated buffer, so completion is still
		 * detected once the buffer has overflowed and no longer holds the tail of the response. */
		if (strstr((const char *)frame, "OK") != NULL || strstr((const char *)frame, "ERROR") != NULL) {
			self->cmd_running = false;
			xSemaphoreGive(self->cmd_sem);
		}
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

	/* Arm the command and send it while holding comm_lock, so the background task cannot process any
	 * frame between marking the command running and finishing the send: that would let it match an
	 * unsolicited message as the response, or miss a response the module returns immediately. */
	xSemaphoreTake(self->comm_lock, portMAX_DELAY);
	self->resp_len = 0;
	self->resp_buf[0] = '\0';
	self->resp_overflow = false;
	self->cmd_running = true;
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

	/* Wait for the background task to capture the response. */
	if (xSemaphoreTake(self->cmd_sem, pdMS_TO_TICKS(ST67W611_CMD_TIMEOUT_MS)) != pdTRUE) {
		xSemaphoreTake(self->comm_lock, portMAX_DELAY);
		self->cmd_running = false;
		xSemaphoreGive(self->comm_lock);
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("no response to '%s'"), cmd);
		xSemaphoreGive(self->cmd_lock);
		return ST67W611_RET_TIMEOUT;
	}

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


st67w611_ret_t st67w611_ble_advertise(St67w611 *self, const char *name) {
	if (u_assert(self != NULL) ||
	    u_assert(name != NULL)) {
		return ST67W611_RET_NULL;
	}

	/* Initialize BLE in the server role (1 = client, 2 = server, 3 = dual). */
	if (st67w611_command(self, "AT+BLEINIT=2\r\n", NULL, 0, NULL) != ST67W611_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("BLE init failed"));
		return ST67W611_RET_FAILED;
	}

	char cmd[ST67W611_CMD_MAX];
	snprintf(cmd, sizeof(cmd), "AT+BLENAME=\"%s\"\r\n", name);
	if (st67w611_command(self, cmd, NULL, 0, NULL) != ST67W611_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("setting BLE name failed"));
		return ST67W611_RET_FAILED;
	}

	if (st67w611_command(self, "AT+BLEADVSTART\r\n", NULL, 0, NULL) != ST67W611_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("starting BLE advertising failed"));
		return ST67W611_RET_FAILED;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("BLE advertising as '%s'"), name);
	return ST67W611_RET_OK;
}


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

	/* Probe synchronously before the background task takes over the SPI link. */
	if (st67w611_probe(self) != ST67W611_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("probe failed"));
		return ST67W611_RET_FAILED;
	}

	self->cmd_lock = xSemaphoreCreateMutex();
	self->comm_lock = xSemaphoreCreateMutex();
	self->cmd_sem = xSemaphoreCreateBinary();
	if (self->cmd_lock == NULL || self->comm_lock == NULL || self->cmd_sem == NULL) {
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

	/* Ask the background task to stop; it deletes itself once it observes the flag. */
	self->rx_task_running = false;

	if (self->cmd_lock != NULL) {
		vSemaphoreDelete(self->cmd_lock);
	}
	if (self->comm_lock != NULL) {
		vSemaphoreDelete(self->comm_lock);
	}
	if (self->cmd_sem != NULL) {
		vSemaphoreDelete(self->cmd_sem);
	}
	return ST67W611_RET_OK;
}
