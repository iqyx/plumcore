/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Microchip RN2483 LoRaWAN modem driver
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "config.h"
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include "u_log.h"
#include "u_assert.h"

#include <interfaces/stream.h>
#include "lora-rn2483.h"

#define MODULE_NAME "lora-rn2483"
#define DEBUG 1


/* Do not allow running multiple concurrent commands. This function cannot fail,
 * it may wait forever though. */
static void command_lock(LoraModem *self) {
	xSemaphoreTake(self->command_lock, portMAX_DELAY);
}

static void command_unlock(LoraModem *self) {
	xSemaphoreGive(self->command_lock);
}

#define PARAM_CHECK(p, r) if (!(p)) return r;

/*********************************************************************************
 * LoRa interface implementation
 *********************************************************************************/

static const lora_vmt_t lora_modem_vmt = {
	.set_appkey = (typeof(lora_modem_vmt.set_appkey))&lora_modem_set_appkey,
	.set_appeui = (typeof(lora_modem_vmt.set_appeui))&lora_modem_set_appeui,
	.set_deveui = (typeof(lora_modem_vmt.set_deveui))&lora_modem_set_deveui,
	.get_deveui = (typeof(lora_modem_vmt.get_deveui))&lora_modem_get_deveui,
	.set_devaddr = (typeof(lora_modem_vmt.set_devaddr))&lora_modem_set_devaddr,
	.set_nwkskey = (typeof(lora_modem_vmt.set_nwkskey))&lora_modem_set_nwkskey,
	.set_appskey = (typeof(lora_modem_vmt.set_appskey))&lora_modem_set_appskey,
	.join_abp = (typeof(lora_modem_vmt.join_abp))&lora_modem_join_abp,
	.join_otaa = (typeof(lora_modem_vmt.join_otaa))&lora_modem_join_otaa,
	// .leave = (typeof(lora_modem_vmt.leave))&lora_modem_leave,
	.set_datarate = (typeof(lora_modem_vmt.set_datarate))&lora_modem_set_datarate,
	.set_adr = (typeof(lora_modem_vmt.set_adr))&lora_modem_set_adr,
	.send = (typeof(lora_modem_vmt.send))&lora_modem_send,
	.receive = (typeof(lora_modem_vmt.receive))&lora_modem_receive,
};


static lora_modem_ret_t response_check(const char *response) {
	if (!strcmp(response, "ok")) return LORA_MODEM_RET_OK;
	if (!strcmp(response, "invalid_param")) return LORA_MODEM_RET_INVALID_PARAM;
	if (!strcmp(response, "accepted")) return LORA_MODEM_RET_ACCEPTED;
	if (!strcmp(response, "denied")) return LORA_MODEM_RET_DENIED;
	if (!strcmp(response, "not_joined")) return LORA_MODEM_RET_NOT_JOINED;
	if (!strcmp(response, "no_free_ch")) return LORA_MODEM_RET_NO_FREE_CHANNEL;
	if (!strcmp(response, "silent")) return LORA_MODEM_RET_SILENT;
	if (!strcmp(response, "frame_counter_err_rejoin_needed")) return LORA_MODEM_RET_FRAME_COUNTER_ERROR;
	if (!strcmp(response, "busy")) return LORA_MODEM_RET_BUSY;
	if (!strcmp(response, "mac_paused")) return LORA_MODEM_RET_PAUSED;
	if (!strcmp(response, "invalid_data_len")) return LORA_MODEM_RET_INVALID_DATA_LEN;
	if (!strcmp(response, "mac_tx_ok")) return LORA_MODEM_RET_TX_OK;
	if (!strncmp(response, "mac_rx", 6)) return LORA_MODEM_RET_RX;
	if (!strcmp(response, "mac_err")) return LORA_MODEM_RET_TX_ERR;
	return LORA_MODEM_RET_FAILED;
}


static lora_ret_t lora_response(lora_modem_ret_t ret) {
	switch (ret) {
		case LORA_MODEM_RET_OK:
		case LORA_MODEM_RET_ACCEPTED:
		case LORA_MODEM_RET_TX_OK:
		case LORA_MODEM_RET_RX:
			return LORA_RET_OK;
		default:
			return LORA_RET_FAILED;
	}
}


static lora_modem_ret_t get_status(LoraModem *self) {
	command_lock(self);
	lora_modem_write_line(self, "mac get status");
	lora_modem_read_line(self, self->response_str, LORA_RESPONSE_LEN, &self->response_len);
	command_unlock(self);

	if (self->response_len == 8) {
		return LORA_MODEM_RET_OK;
	} else {
		return LORA_MODEM_RET_FAILED;
	}
}


static void lora_status_task(void *p) {
	LoraModem *self = (LoraModem *)p;

	vTaskDelay(2000);
	self->running = true;
	while (self->can_run) {
		lora_modem_sleep(self, 800);

		if (get_status(self) == LORA_MODEM_RET_OK) {
			uint32_t status = strtol(self->response_str, NULL, 16);
			self->mac_state = status & 0xf;
			self->joined = status & (1 << 4);
			self->automatic_reply_enabled = status & (1 << 5);
			self->adr_enabled = status & (1 << 6);
			self->silent_immediately = status & (1 << 7);
			self->mac_paused = status & (1 << 8);
			self->rx_done = status & (1 << 9);
			self->line_check_enabled = status & (1 << 10);
			self->channels_updated = status & (1 << 11);
			self->power_updated = status & (1 << 12);
			self->nbrep_updated = status & (1 << 13);
			self->prescaler_updated = status & (1 << 14);
			self->rx2_updated = status & (1 << 15);
			self->rx_timing_updated = status & (1 << 16);
			self->rejoin_needed = status & (1 << 17);
			self->multicast_enabled = status & (1 << 18);
		} else {
			lora_modem_regain_comms(self);
		}

/** @todo no LED support yet */
/*
		if (self->led) {
			self->led->pause = 10000;
			if (self->joined) {
				self->led->status_code = 0x1;
			} else {
				self->led->status_code = 0x4;
			}
		}
*/

		if (self->join_req) {
			self->join_req = false;
			lora_modem_join_otaa(self);
		}

		// lora_modem_ret_t ret = lora_modem_get_vdd(self, &self->status_vdd_mV);
	}
	self->running = false;
	vTaskDelete(NULL);
}


lora_modem_ret_t lora_modem_init(LoraModem *self, Stream *usart) {
	memset(self, 0, sizeof(LoraModem));
	self->usart = usart;

	lora_init(&self->lora, &lora_modem_vmt);
	self->lora.parent = self;

	self->command_lock = xSemaphoreCreateMutex();
	if (self->command_lock == NULL) {
		return LORA_MODEM_RET_FAILED;
	}

	self->rx_data_release = xSemaphoreCreateBinary();
	if (self->rx_data_release == NULL) {
		return LORA_MODEM_RET_FAILED;
	}

	if (lora_modem_regain_comms(self) != LORA_MODEM_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("modem not responding"));
		return LORA_MODEM_RET_FAILED;

	}
	char buf[50] = {0};
	lora_modem_get_ver(self, buf, sizeof(buf));
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("modem version: %s"), buf);

	/* The status task runs at minimal priority (1) to execute commands only on background. */
	self->can_run = true;
	xTaskCreate(lora_status_task, "lora_status", configMINIMAL_STACK_SIZE + 128, (void *)self, 1, NULL);
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("init ok"));

	return LORA_MODEM_RET_OK;
}


lora_modem_ret_t lora_modem_free(LoraModem *self) {
	self->can_run = false;
	while (self->running) {
		vTaskDelay(100);
	}
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("task stopped"));

	vSemaphoreDelete(self->command_lock);
	vSemaphoreDelete(self->rx_data_release);
	for (size_t i = 0; i < 16; i++) {
		vSemaphoreDelete(self->rx_lock[i]);
	}
	lora_free(&self->lora);

	return LORA_MODEM_RET_OK;
}


/** @todo no baudrate setting support yet */
lora_modem_ret_t lora_modem_regain_comms(LoraModem *self) {
	/* Clear RX buffer. */
	size_t r = 0;
	uint8_t c = 0;
	stream_ret_t ret;
	do {
		ret = self->usart->vmt->read_timeout(self->usart, &c, 1, &r, 0);
	} while (ret == STREAM_RET_OK);
	
	/* Perform auto baud rate detection. Change baudrate to 300 and send 0x00 (break). */
	// usart_baudrate(self->usart, 300);
	// usart_write(self->usart, (uint8_t *)"\0", 1, NULL);
	/* Now change to the desired baudrate and send 0x55 to auto-detect it. */
	// usart_baudrate(self->usart, 57600);
	self->usart->vmt->write(self->usart, (void *)"\x55", 1);
	// lora_modem_read_line(self, self->response_str, LORA_RESPONSE_LEN, &self->response_len);
	vTaskDelay(300);

	/* Eat the "invalid_param" response. */
	uint32_t timeout = 10;
	do {
		// lora_modem_write_line(self, "sys get ver");
		lora_modem_read_line(self, self->response_str, LORA_RESPONSE_LEN, &self->response_len);
		timeout--;
		if (timeout == 0) {
			return LORA_MODEM_RET_FAILED;
		}
	} while (strncmp(self->response_str, "RN2483", 6));
	lora_modem_read_line(self, self->response_str, LORA_RESPONSE_LEN, &self->response_len);

	return LORA_MODEM_RET_OK;
}

/*
lora_modem_ret_t lora_modem_set_led(LoraModem *self, StatusLed *led) {
	self->led = led;
	led->output_status_code = true;

	return LORA_MODEM_RET_OK;
}
*/

lora_modem_ret_t lora_modem_write(LoraModem *self, const char *buf) {
	// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("-> %s"), buf);
	self->usart->vmt->write(self->usart, buf, strlen(buf));

	return LORA_MODEM_RET_OK;
}


lora_modem_ret_t lora_modem_write_line(LoraModem *self, const char *buf) {
	lora_modem_write(self, buf);
	self->usart->vmt->write(self->usart, (void *)"\r\n", 2);

	return LORA_MODEM_RET_OK;
}


lora_modem_ret_t lora_modem_read_line(LoraModem *self, char *buf, size_t max_buf, size_t *read) {
	memset(buf, 0, max_buf);

	uint8_t c;
	size_t len = 0;
	*read = 0;
	while (true) {
		/* Read with a timeout of ~10ms to avoid looping fast. */
		size_t r = 0;
		self->usart->vmt->read_timeout(self->usart, &c, 1, &r, 1000);

		if (r == 0) {
			return LORA_MODEM_RET_FAILED;
		}

		if (c == '\n' || c == '\r') {
			if (len > 0) {
				/* At least some non-whitespace characters were received. End with
				 * reception of the current line. Terminate with \0. */
				buf[max_buf - 1] = '\0';
				if (read) {
					*read = len;
				}
				// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("<- %s"), buf);
				return LORA_MODEM_RET_OK;
			} else {
				/* If no non-whitespace characters were received yet, just eat
				 * all newline characters until a valid line begins. */
				continue;
			}
		}

		/* A valid character is received. If there is enough buffer space, save it. */
		if (len < max_buf) {
			buf[len] = c;
			len++;
		} else {
			/* The line is too long. Drop the current character and wait for the
			 * end of line anyway.*/
			continue;
		}
	}

	/* Unreachable. Return FAILED to avoid warning. */
	return LORA_MODEM_RET_FAILED;
}


/* LoRa API */
lora_ret_t lora_modem_set_appkey(LoraModem *self, const char *appkey) {
	PARAM_CHECK(appkey != NULL, LORA_RET_BAD_PARAM);
	char cmd[50] = {0};
	snprintf(cmd, sizeof(cmd), "mac set appkey %s", appkey);

	command_lock(self);
	lora_modem_write_line(self, cmd);
	lora_modem_read_line(self, self->response_str, LORA_RESPONSE_LEN, &self->response_len);
	command_unlock(self);

	return lora_response(response_check(self->response_str));
}


/* LoRa API */
lora_ret_t lora_modem_set_appeui(LoraModem *self, const char *appeui) {
	PARAM_CHECK(appeui != NULL, LORA_RET_BAD_PARAM);
	char cmd[50] = {0};
	snprintf(cmd, sizeof(cmd), "mac set appeui %s", appeui);

	command_lock(self);
	lora_modem_write_line(self, cmd);
	lora_modem_read_line(self, self->response_str, LORA_RESPONSE_LEN, &self->response_len);
	command_unlock(self);

	return lora_response(response_check(self->response_str));
}


/* LoRa API */
lora_ret_t lora_modem_set_deveui(LoraModem *self, const char *deveui) {
	PARAM_CHECK(deveui != NULL, LORA_RET_BAD_PARAM);
	char cmd[50] = {0};
	snprintf(cmd, sizeof(cmd), "mac set deveui %s", deveui);

	command_lock(self);
	lora_modem_write_line(self, cmd);
	lora_modem_read_line(self, self->response_str, LORA_RESPONSE_LEN, &self->response_len);
	command_unlock(self);

	return lora_response(response_check(self->response_str));
}


/* LoRa API */
lora_ret_t lora_modem_get_deveui(LoraModem *self, char *deveui) {
	PARAM_CHECK(deveui != NULL, LORA_RET_BAD_PARAM);

	command_lock(self);
	lora_modem_write_line(self, "mac get deveui");
	lora_modem_read_line(self, self->response_str, LORA_RESPONSE_LEN, &self->response_len);
	strcpy(deveui, self->response_str);
	command_unlock(self);

	return LORA_RET_OK;
}


/* LoRa API */
lora_ret_t lora_modem_set_devaddr(LoraModem *self, const uint8_t devaddr[4]) {
	char cmd[50] = {0};
	snprintf(cmd, sizeof(cmd), "mac set devaddr %02x%02x%02x%02x", devaddr[0], devaddr[1], devaddr[2], devaddr[3]);
	command_lock(self);
	lora_modem_write_line(self, cmd);
	lora_modem_read_line(self, self->response_str, LORA_RESPONSE_LEN, &self->response_len);
	command_unlock(self);

	return lora_response(response_check(self->response_str));
}


/* LoRa API */
lora_ret_t lora_modem_set_nwkskey(LoraModem *self, const char *nwkskey) {
	PARAM_CHECK(nwkskey != NULL, LORA_RET_BAD_PARAM);
	char cmd[50] = {0};
	snprintf(cmd, sizeof(cmd), "mac set nwkskey %s", nwkskey);

	command_lock(self);
	lora_modem_write_line(self, cmd);
	lora_modem_read_line(self, self->response_str, LORA_RESPONSE_LEN, &self->response_len);
	command_unlock(self);

	return lora_response(response_check(self->response_str));
}


/* LoRa API */
lora_ret_t lora_modem_set_appskey(LoraModem *self, const char *appskey) {
	PARAM_CHECK(appskey != NULL, LORA_RET_BAD_PARAM);
	char cmd[50] = {0};
	snprintf(cmd, sizeof(cmd), "mac set appskey %s", appskey);

	command_lock(self);
	lora_modem_write_line(self, cmd);
	lora_modem_read_line(self, self->response_str, LORA_RESPONSE_LEN, &self->response_len);
	command_unlock(self);

	return lora_response(response_check(self->response_str));
}


/* LoRa API */
lora_ret_t lora_modem_set_datarate(LoraModem *self, uint8_t datarate) {
	char cmd[50] = {0};
	snprintf(cmd, sizeof(cmd), "mac set dr %u", datarate);

	command_lock(self);
	lora_modem_write_line(self, cmd);
	lora_modem_read_line(self, self->response_str, LORA_RESPONSE_LEN, &self->response_len);
	command_unlock(self);

	return lora_response(response_check(self->response_str));
}


lora_modem_ret_t lora_modem_get_vdd(LoraModem *self, uint32_t *vdd) {
	command_lock(self);
	lora_modem_write_line(self, "sys get vdd");
	lora_modem_read_line(self, self->response_str, LORA_RESPONSE_LEN, &self->response_len);
	*vdd = (uint32_t)(strtol(self->response_str, NULL, 10));
	command_unlock(self);

	return LORA_MODEM_RET_OK;
}


/* LoRa API */
lora_ret_t lora_modem_set_adr(LoraModem *self, bool adr) {
	command_lock(self);
	if (adr) {
		lora_modem_write_line(self, "mac set adr on");
	} else {
		lora_modem_write_line(self, "mac set adr off");
	}
	lora_modem_read_line(self, self->response_str, LORA_RESPONSE_LEN, &self->response_len);
	command_unlock(self);

	return lora_response(response_check(self->response_str));
}


lora_modem_ret_t lora_modem_save(LoraModem *self) {
	command_lock(self);
	lora_modem_write_line(self, "mac save");
	lora_modem_read_line(self, self->response_str, LORA_RESPONSE_LEN, &self->response_len);
	command_unlock(self);

	return response_check(self->response_str);
}


static lora_ret_t lora_modem_join_common(LoraModem *self) {
	/* The command is sent, we are waiting for the response now.
	   Command lock is already acquired. */
	lora_modem_read_line(self, self->response_str, LORA_RESPONSE_LEN, &self->response_len);
	lora_modem_ret_t ret = response_check(self->response_str);

	if (ret != LORA_MODEM_RET_OK) {
		command_unlock(self);
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("network join failed"));
		return lora_response(ret);
	}

	/* Wait for "accepted" or "denied" */
	for (int i = 0; i < 100; i++) {
		lora_modem_read_line(self, self->response_str, LORA_RESPONSE_LEN, &self->response_len);
		if (self->response_len == 0) {
			continue;
		}

		/* A response was received, return immediatelly */
		ret = response_check(self->response_str);
		command_unlock(self);

		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("network join ret = %d '%s'"), ret, self->response_str);
		return lora_response(ret);
	}

	/* Nothing was received so far. */
	command_unlock(self);
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("network join timeout"));
	return LORA_RET_FAILED;
}


/* LoRa API */
lora_ret_t lora_modem_join_abp(LoraModem *self) {
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("join (ABP method)"));
	command_lock(self);
	lora_modem_write_line(self, "mac join abp");
	return lora_modem_join_common(self);
}


lora_ret_t lora_modem_join_otaa(LoraModem *self) {
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("join (OTAA method)"));
	command_lock(self);
	lora_modem_write_line(self, "mac join otaa");
	return lora_modem_join_common(self);
}



static uint32_t hex2uint(const char *hex, size_t len) {
	uint32_t u = 0;

	for (size_t i = 0; i < len; i++) {
		u <<= 4;
		if (hex[i] >= '0' && hex[i] <= '9') {
			u |= hex[i] - '0';
		} else if (hex[i] >= 'A' && hex[i] <= 'F') {
			u |= hex[i] - 'A';
		} else {
			return 0;
		}
	}
	return u;
}


/* Such code. It is 4 AM. */
static lora_modem_ret_t parse_rx_data(LoraModem *self) {
	char *s = self->response_str;
	size_t len = self->response_len;
	if (len < 11) {
		/* There is at least "mac_rx N XX". No need to control remaining chars anymore. */
		return LORA_MODEM_RET_FAILED;
	}

	if (strncmp(self->response_str, "mac_rx", 6)) {
		return LORA_MODEM_RET_FAILED;
	}

	/* Eat "mac_rx" and the following space. */
	s += 7; len -= 7;

	/* A decimal port number follows. */
	self->rx_data_port = (*s) - '0';
	s++; len--;

	if (*s != ' ') {
		/* Not finished yet, there's a second digit. */
		self->rx_data_port = self->rx_data_port * 10 + (*s) - '0';
		s++; len--;
	}

	/* No more digits. There should be a space. Check. */
	if (*s != ' ') {
		return LORA_MODEM_RET_FAILED;
	}

	/* Eat the space */
	s++; len--;

	if (len % 2) {
		/* Odd number of chars remains. The hex string is corrupted. */
		return LORA_MODEM_RET_FAILED;
	}
	self->rx_data_len = len / 2;

	for (size_t i = 0; i < self->rx_data_len; i++) {
		self->rx_data[i] = (uint8_t)hex2uint(s, 2);
		s += 2;
	}

	return LORA_MODEM_RET_OK;
}


/* LoRa API */
lora_ret_t lora_modem_send(LoraModem *self, uint8_t port, const uint8_t *data, size_t len) {
	PARAM_CHECK(data != NULL, LORA_RET_BAD_PARAM);
	(void)len;
	char cmd[50] = {0};
	snprintf(cmd, sizeof(cmd), "mac tx uncnf %lu ", (uint32_t)port);

/** @todo no LED support yet */
/*
	if (self->led) {
		self->led->pause = 100;
		self->led->status_code = 0x4;
	}
*/

	command_lock(self);
	lora_modem_write(self, cmd);
	lora_modem_write_line(self, (const char *)data);

	/* Check the first response. Theres a couple of possible responses meaning errors.
	 * Continue if the response is "ok". */
	lora_modem_read_line(self, self->response_str, LORA_RESPONSE_LEN, &self->response_len);
	lora_modem_ret_t ret = response_check(self->response_str);
	if (ret != LORA_MODEM_RET_OK) {

		/* Check if joined. If not, set join request flag. */
		if (ret == LORA_MODEM_RET_NOT_JOINED) {
			self->join_req = true;
		}
		command_unlock(self);
		return lora_response(ret);
	}

	/* Continue if the packet was forwarded to the MAC */
	uint32_t timeout = LORA_TX_OK_TIMEOUT;
	while (true) {
		/* If the packet from the server is sent without an ACK request, RN2483 never answers. */
		lora_modem_read_line(self, self->response_str, LORA_RESPONSE_LEN, &self->response_len);
		if (self->response_len == 0) {
			if (timeout == 0) {
				command_unlock(self);
				return LORA_RET_FAILED;
			} else {
				timeout--;
				continue;
			}
		}
		ret = response_check(self->response_str);

		if (ret == LORA_MODEM_RET_RX) {
			/* Downlink data received. */
			if (parse_rx_data(self) == LORA_MODEM_RET_OK) {
				if (self->rx_lock[self->rx_data_port] != NULL) {
					xSemaphoreGive(self->rx_lock[self->rx_data_port]);
					/* Wait with a timeout to avoid waiting forever
					 * if the receiver stopped calling receive. */
					xSemaphoreTake(self->rx_data_release, 100);
				}
			}
			/* We can safely read another line here. */
			continue;
		} else {
			/* Return in all other cases. Handle OK transmission. */
			if (ret == LORA_MODEM_RET_TX_OK) {
				ret = LORA_MODEM_RET_OK;
			}
			command_unlock(self);
			return lora_response(ret);
		}
	}
	/* Unreachable */
	command_unlock(self);
	return lora_response(ret);
}


/* LoRa API */
lora_ret_t lora_modem_receive(LoraModem *self, uint8_t port, uint8_t *data, size_t size, size_t *len, uint32_t timeout) {
	if (port > 15) {
		return LORA_RET_FAILED;
	}

	/* Calling RX on this port for the first time. Create the semaphore first. */
	if (self->rx_lock[port] == NULL) {
		self->rx_lock[port] = xSemaphoreCreateBinary();
		if (self->rx_lock[port] == pdFALSE) {
			return LORA_RET_FAILED;
		}
	}

	/* Wait on a semaphore corresponding to this port. */
	if (xSemaphoreTake(self->rx_lock[port], timeout) == pdFALSE) {
		return LORA_RET_TIMEOUT;
	}

	/* The semaphore was signalled. It means the full response is saved in the
	 * rx_data and we may process it. Signal rx_data_release to continue. */
	if (self->rx_data_len > size) {
		self->rx_data_len = size;
	}
	*len = self->rx_data_len;
	memcpy(data, self->rx_data, self->rx_data_len);

	xSemaphoreGive(self->rx_data_release);
	return LORA_RET_OK;
}


lora_modem_ret_t lora_modem_sleep(LoraModem *self, uint32_t len) {
	char cmd[50] = {0};
	snprintf(cmd, sizeof(cmd), "sys sleep %lu", len);

	command_lock(self);
	lora_modem_write_line(self, cmd);
	lora_modem_read_line(self, self->response_str, LORA_RESPONSE_LEN, &self->response_len);
	command_unlock(self);
	return response_check(self->response_str);
}


lora_modem_ret_t lora_modem_factory_reset(LoraModem *self) {
	command_lock(self);
	lora_modem_write_line(self, "sys factoryRESET");
	command_unlock(self);

	return LORA_MODEM_RET_OK;
}


lora_modem_ret_t lora_modem_reset(LoraModem *self) {
	command_lock(self);
	lora_modem_write_line(self, "sys reset");
	command_unlock(self);

	return LORA_MODEM_RET_OK;
}


lora_modem_ret_t lora_modem_get_ver(LoraModem *self, char *version, size_t ver_len) {
	command_lock(self);
	lora_modem_write_line(self, "sys get ver");
	lora_modem_read_line(self, self->response_str, LORA_RESPONSE_LEN, &self->response_len);
	if (version != NULL) {
		strncpy(version, self->response_str, ver_len);
	}
	command_unlock(self);

	return LORA_MODEM_RET_OK;
}


lora_modem_ret_t lora_modem_set_ar(LoraModem *self, bool ar) {
	command_lock(self);
	if (ar) {
		lora_modem_write_line(self, "mac set ar on");
	} else {
		lora_modem_write_line(self, "mac set ar off");
	}
	lora_modem_read_line(self, self->response_str, LORA_RESPONSE_LEN, &self->response_len);
	command_unlock(self);
	return response_check(self->response_str);
}
