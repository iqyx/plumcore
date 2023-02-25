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

#include <main.h>

#include <interfaces/stream.h>
#include <interfaces/power.h>
#include "lora-rn2483.h"

#define MODULE_NAME "lora-rn2483"
#define DEBUG 1
#define LOG_LINE u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("%s:%d"), __func__, __LINE__)
#define PARAM_CHECK(p, r) if (!(p)) return r;


/* Do not allow running multiple concurrent commands. This function cannot fail,
 * it may wait forever though. */
static void command_lock(LoraModem *self) {
	xSemaphoreTake(self->command_lock, portMAX_DELAY);
}

static void command_unlock(LoraModem *self) {
	xSemaphoreGive(self->command_lock);
}


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


static lora_ret_t lora_response(LoraModem *self, lora_modem_ret_t ret) {
	switch (ret) {
		case LORA_MODEM_RET_TX_OK:
			/* A successful transmission always clears the error counter. */
			self->error_counter = 0;
			return LORA_RET_OK;

		case LORA_MODEM_RET_OK:
			/* fall-through */
		case LORA_MODEM_RET_ACCEPTED:
			/* fall-through */
		case LORA_MODEM_RET_RX:
			return LORA_RET_OK;

		case LORA_MODEM_RET_DENIED:
			/* If the join was denied, either the gateway is disconnected (join response packet
			 * was missed) or the network doesn't want us to connect. In either case, try again. */
			/* fall-through */
		case LORA_MODEM_RET_NOT_JOINED:
			/* Not joined for whatever reason, try again later. */
			self->join_req = true;
			return LORA_RET_FAILED;

		case LORA_MODEM_RET_NO_FREE_CHANNEL:
			/* Not an error, considered a NOOP, signal failure and continue. */
			u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("no free channel, consider increasing the message interval"));
			return LORA_RET_FAILED;

		default:
			self->error_counter++;
			u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("other error ret = %d (counter = %lu)"), ret, self->error_counter);
			if (self->error_counter >= LORA_MAX_ERRORS_BEFORE_REJOIN) {
				u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("rejoin requested"));
				self->join_req = true;
				/* And start over. No backoff algo. */
				self->error_counter = 0;
			}
			return LORA_RET_FAILED;
	}
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


/*********************************************************************************************************************
 * LoRa interface implementation
 *********************************************************************************************************************/

#include "lora-api.c"
static const lora_vmt_t lora_modem_vmt = {
	.set_mode = lora_modem_set_mode,
	.get_mode = lora_modem_get_mode,

	.set_appkey = lora_modem_set_appkey,
	.get_appkey = lora_modem_get_appkey,

	.set_appeui = lora_modem_set_appeui,
	.get_appeui = lora_modem_get_appeui,

	.set_deveui = lora_modem_set_deveui,
	.get_deveui = lora_modem_get_deveui,
	.set_devaddr = lora_modem_set_devaddr,
	.set_nwkskey = lora_modem_set_nwkskey,
	.set_appskey = lora_modem_set_appskey,
	.join = lora_modem_join,
	// .leave = lora_modem_leave,
	.set_datarate = lora_modem_set_datarate,
	.get_datarate = lora_modem_get_datarate,

	.set_adr = lora_modem_set_adr,
	.send = lora_modem_send,
	.receive = lora_modem_receive,
};

/*********************************************************************************************************************/


static lora_modem_ret_t get_status(LoraModem *self, uint32_t *status) {
	command_lock(self);
	lora_modem_clear_input(self);
	lora_modem_write_line(self, "mac get status");
	if (lora_modem_read_line(self, self->response_str, LORA_RESPONSE_LEN, &self->response_len) != LORA_MODEM_RET_OK) {
		/* No response was received. */
		command_unlock(self);
		return LORA_MODEM_RET_FAILED;
	}
	/* Check if the response is exactly 8 character long. Filter out garbage. */
	if (self->response_len != 8) {
		command_unlock(self);
		return LORA_MODEM_RET_FAILED;
	}

	// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("response %s"), self->response_str);
	*status = strtol(self->response_str, NULL, 16);
	command_unlock(self);
	return LORA_MODEM_RET_OK;
}


static void lora_status_task(void *p) {
	LoraModem *self = (LoraModem *)p;

	vTaskDelay(2000);
	self->running = true;
	while (self->can_run) {
		lora_modem_sleep(self, 800);

		uint32_t status = 0;
		if (get_status(self, &status) == LORA_MODEM_RET_OK) {
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
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("no response from the modem, reset"));
			lora_modem_regain_comms(self);
		}

		if (self->rejoin_needed) {
			self->join_req = true;
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
			lora_modem_set_deveui(&self->lora, self->deveui);
			lora_modem_set_appeui(&self->lora, self->appeui);
			lora_modem_set_appkey(&self->lora, self->appkey);
			lora_modem_join(&self->lora);
		}

		lora_modem_get_vdd(self, &self->status_vdd_mV);

		/* Allow to run other commands. If we do not yield here,
		 * this task will cycle forever. */
		taskYIELD();
	}
	self->running = false;
	vTaskDelete(NULL);
}


/*********************************************************************************************************************
 * Sensor interface for voltage measurement
 *********************************************************************************************************************/

static sensor_ret_t vdd_sensor_value_f(Sensor *self, float *value) {
	if (u_assert(self != NULL)) {
		return SENSOR_RET_FAILED;
	}
	LoraModem *lora = (LoraModem *)self->parent;
	*value = lora->status_vdd_mV / 1000.0f;

	return SENSOR_RET_OK;
}

static const struct sensor_vmt vdd_sensor_vmt = {
	.value_f = vdd_sensor_value_f
};

static const struct sensor_info vdd_sensor_info = {
	.description = "Vdd",
	.unit = "V"
};

/*********************************************************************************************************************/


lora_modem_ret_t lora_modem_init(LoraModem *self, Stream *usart, Power *power) {
	memset(self, 0, sizeof(LoraModem));
	self->usart = usart;
	self->power = power;

	lora_init(&self->lora, &lora_modem_vmt);
	self->lora.parent = self;

	self->vdd.vmt = &vdd_sensor_vmt;
	self->vdd.info = &vdd_sensor_info;
	self->vdd.parent = self;

	self->command_lock = xSemaphoreCreateMutex();
	if (self->command_lock == NULL) {
		return LORA_MODEM_RET_FAILED;
	}

	self->rx_data_release = xSemaphoreCreateBinary();
	if (self->rx_data_release == NULL) {
		return LORA_MODEM_RET_FAILED;
	}

	self->debug_requests = false;
	self->debug_responses = false;

	/* Reset and get the communication working. */
	if (lora_modem_regain_comms(self) != LORA_MODEM_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("modem not responding"));
		return LORA_MODEM_RET_FAILED;

	}
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("modem version: '%s'"), self->response_str);

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
	/* Clear RX buffer. */
lora_modem_ret_t lora_modem_regain_comms(LoraModem *self) {
	if (self->power != NULL) {
		self->power->vmt->enable(self->power, false);
		vTaskDelay(200);
		self->power->vmt->enable(self->power, true);
	}

	/* Perform auto baud rate detection. Change baudrate to 300 and send 0x00 (break). */
	// usart_baudrate(self->usart, 300);
	// usart_write(self->usart, (uint8_t *)"\0", 1, NULL);
	/* Now change to the desired baudrate and send 0x55 to auto-detect it. */
	// usart_baudrate(self->usart, 57600);
	lora_modem_clear_input(self);
	self->usart->vmt->write(self->usart, (void *)"\x55", 1);

	uint32_t timeout = 10;
	do {
		lora_modem_read_line(self, self->response_str, LORA_RESPONSE_LEN, &self->response_len);

		timeout--;
		if (timeout == 0) {
			return LORA_MODEM_RET_FAILED;
		}
	} while (strncmp(self->response_str, "RN2483", 6));

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
	if (self->debug_requests) {
		u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("-> '%s', len = %u"), buf, strlen(buf));
	}
	self->usart->vmt->write(self->usart, buf, strlen(buf));

	return LORA_MODEM_RET_OK;
}


lora_modem_ret_t lora_modem_write_line(LoraModem *self, const char *buf) {
	lora_modem_write(self, buf);
	self->usart->vmt->write(self->usart, (void *)"\r\n", 2);

	return LORA_MODEM_RET_OK;
}


/* Clear the stream input buffer just before running a command.
 * It ensures the response is correctly framed. */
lora_modem_ret_t lora_modem_clear_input(LoraModem *self) {
	uint8_t buf[16];
	while (self->usart->vmt->read_timeout(self->usart, buf, sizeof(buf), NULL, 0) == STREAM_RET_OK) {
		;
	}
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
		if (self->usart->vmt->read_timeout(self->usart, &c, 1, &r, 2000) != STREAM_RET_OK) {
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
				if (self->debug_responses) {
					u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("<- '%s', len = %u"), buf, len);
				}
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


lora_modem_ret_t lora_modem_get_vdd(LoraModem *self, uint32_t *vdd) {
	command_lock(self);
	lora_modem_clear_input(self);
	lora_modem_write_line(self, "sys get vdd");
	lora_modem_read_line(self, self->response_str, LORA_RESPONSE_LEN, &self->response_len);
	*vdd = (uint32_t)(strtol(self->response_str, NULL, 10));
	command_unlock(self);

	return LORA_MODEM_RET_OK;
}


lora_modem_ret_t lora_modem_save(LoraModem *self) {
	command_lock(self);
	lora_modem_clear_input(self);
	lora_modem_write_line(self, "mac save");
	lora_modem_read_line(self, self->response_str, LORA_RESPONSE_LEN, &self->response_len);
	lora_modem_ret_t ret = response_check(self->response_str);
	command_unlock(self);

	return ret;
}


lora_modem_ret_t lora_modem_sleep(LoraModem *self, uint32_t len) {
	char cmd[50] = {0};
	snprintf(cmd, sizeof(cmd), "sys sleep %lu", len);

	command_lock(self);
	lora_modem_clear_input(self);
	lora_modem_write_line(self, cmd);
	lora_modem_read_line(self, self->response_str, LORA_RESPONSE_LEN, &self->response_len);
	lora_modem_ret_t ret = response_check(self->response_str);
	command_unlock(self);

	return ret;
}


lora_modem_ret_t lora_modem_factory_reset(LoraModem *self) {
	command_lock(self);
	lora_modem_clear_input(self);
	lora_modem_write_line(self, "sys factoryRESET");
	command_unlock(self);

	return LORA_MODEM_RET_OK;
}


lora_modem_ret_t lora_modem_reset(LoraModem *self) {
	command_lock(self);
	lora_modem_clear_input(self);
	lora_modem_write_line(self, "sys reset");
	command_unlock(self);

	return LORA_MODEM_RET_OK;
}


lora_modem_ret_t lora_modem_get_ver(LoraModem *self, char *version, size_t ver_len) {
	command_lock(self);
	lora_modem_clear_input(self);
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
	lora_modem_clear_input(self);
	if (ar) {
		lora_modem_write_line(self, "mac set ar on");
	} else {
		lora_modem_write_line(self, "mac set ar off");
	}
	lora_modem_read_line(self, self->response_str, LORA_RESPONSE_LEN, &self->response_len);
	lora_modem_ret_t ret = response_check(self->response_str);
	command_unlock(self);
	return ret;
}
