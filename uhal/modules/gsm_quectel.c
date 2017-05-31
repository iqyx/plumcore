/*
 * Driver for the Quectel M66 GSM/GPRS module
 *
 * Copyright (C) 2017, Marek Koza, qyx@krtko.org
 *
 * This file is part of uMesh node firmware (http://qyx.krtko.org/projects/umesh)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* Non-optimal implementation providing a single TCP/UDP socket over
 * a GPRS connection (in ideal conditions). */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <string.h>

/* System includes */
#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"
#include "semphr.h"
#include <libopencm3/stm32/gpio.h>

/* HAL includes */
#include "module.h"
#include "interface_stream.h"

#include "gsm_quectel.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "gsm-quectel"

static const char *gsm_quectel_tcpip_states[GSM_QUECTEL_TCPIP_STATUS_MAX] = {
	"UNKNOWN",
	"IP_INITIAL",
	"IP_START",
	"IP_CONFIG",
	"IP_IND",
	"IP_GPRSACT",
	"IP_STATUS",
	"TCP_CONNECTING",
	"UDP_CONNECTING",
	"IP_CLOSE",
	"CONNECT_OK",
	"PDP_DEACT",
};

static gsm_quectel_ret_t read_line(GsmQuectel *self, char *buf, size_t max_buf, size_t *len) {
	if (u_assert(self != NULL) ||
	    u_assert(buf != NULL) ||
	    u_assert(max_buf > 0) ||
	    u_assert(len != NULL)) {
		return GSM_QUECTEL_RET_FAILED;
	}

	uint8_t c;
	*len = 0;
	while (true) {
		/* Read with a timeout of ~10ms to avoid looping fast. */
		int r = interface_stream_read_timeout(self->usart, &c, 1, 10);

		/* Wait for a newline even if no character is received. */
		if (r == 0) {
			continue;
		}

		if (c == '\n' || c == '\r') {
			if (*len > 0) {
				/* At least some non-whitespace characters were received. End with
				 * reception of the current line. */
				return GSM_QUECTEL_RET_OK;
			} else {
				/* If no non-whitespace characters were received yet, just eat
				 * all newline characters until a valid line begins. */
				continue;
			}
		}

		/* A valid character is received. If there is enough buffer space, save it. */
		if (*len < max_buf) {
			buf[*len] = c;
			(*len)++;
		} else {
			/* The line is too long. Drop the current character and wait for the
			 * end of line anyway.*/
			continue;
		}
	}

	/* Unreachable. Return FAILED to avoid warning. */
	return GSM_QUECTEL_RET_FAILED;
}


static gsm_quectel_ret_t write_line(GsmQuectel *self, const char *buf, size_t len) {
	if (u_assert(self != NULL) ||
	    u_assert(buf != NULL) ||
	    u_assert(len > 0)) {
		return GSM_QUECTEL_RET_FAILED;
	}
	size_t written = 0;

	while (written < len) {
		written += interface_stream_write(self->usart, (const uint8_t *)(buf + written), len - written);
	}

	return GSM_QUECTEL_RET_OK;
}


static gsm_quectel_ret_t command(GsmQuectel *self, enum gsm_quectel_command command, uint32_t timeout_ms) {
	if (u_assert(self != NULL)) {
		return GSM_QUECTEL_RET_FAILED;
	}

	gsm_quectel_ret_t r = GSM_QUECTEL_RET_FAILED;

	char command_str[256] = {0};
	switch (command) {
		case GSM_QUECTEL_CMD_TEST:
			strcpy(command_str, "AT");
			break;
		case GSM_QUECTEL_CMD_GET_IMSI:
			strcpy(command_str, "AT+CIMI");
			break;
		case GSM_QUECTEL_CMD_DISABLE_ECHO:
			strcpy(command_str, "ATE0");
			break;
		case GSM_QUECTEL_CMD_ENABLE_CREG_URC:
			strcpy(command_str, "AT+CREG=2");
			break;
		case GSM_QUECTEL_CMD_START_TCPIP:
			strcpy(command_str, "AT+QIREGAPP=\"internet\",\"\",\"\"");
			break;
		case GSM_QUECTEL_CMD_ACTIVATE_CONTEXT:
			strcpy(command_str, "AT+QIACT");
			break;
		case GSM_QUECTEL_CMD_TCPIP_STAT:
			strcpy(command_str, "AT+QISTAT");
			break;
		case GSM_QUECTEL_CMD_ENABLE_TCPIP_MUX:
			strcpy(command_str, "AT+QIMUX=1");
			break;
		case GSM_QUECTEL_CMD_CONNECT:
			strcpy(command_str, "AT+QIOPEN=\"TCP\",\"147.175.187.202\",222");
			break;
		case GSM_QUECTEL_CMD_GET_IP:
			strcpy(command_str, "AT+QILOCIP");
			break;
		case GSM_QUECTEL_CMD_PDP_STATUS:
			strcpy(command_str, "AT+CGACT?");
			break;
		case GSM_QUECTEL_CMD_PDP_CGDCONT:
			strcpy(command_str, "AT+CGDCONT=1,\"IP\",\"internet\"");
			break;
		case GSM_QUECTEL_CMD_PDP_ACTIVATE:
			strcpy(command_str, "AT+CGACT=1");
			break;
		case GSM_QUECTEL_CMD_IP_SEND:
			if (self->data_to_send != NULL && self->data_to_send_len > 0) {
				snprintf(command_str, sizeof(command_str), "AT+QISEND=%u", self->data_to_send_len);
			}
			break;

		case GSM_QUECTEL_CMD_NONE:
		default:
			strcpy(command_str, "");
			break;
	}

	if (strlen(command_str) == 0) {
		/* Return immediatelly before locking the mutex. */
		return GSM_QUECTEL_RET_FAILED;
	}

	// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("running command %s"), command_str);

	/* Only one command can be run at a time. */
	xSemaphoreTake(self->command_lock, portMAX_DELAY);

	/* Clear the response status before running the command. */
	self->current_command = command;
	self->command_response = GSM_QUECTEL_CMD_RESPONSE_ERROR;
	self->cme_error_code = 0;

	//~ u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("sending command"));
	if (write_line(self, command_str, strlen(command_str)) != GSM_QUECTEL_RET_OK) {
		r = GSM_QUECTEL_RET_FAILED;
	}
	if (write_line(self, "\r\n", 2) != GSM_QUECTEL_RET_OK) {
		r = GSM_QUECTEL_RET_FAILED;
	}

	if (command == GSM_QUECTEL_CMD_IP_SEND) {
		/** @todo send data here */

		vTaskDelay(10);
		write_line(self, self->data_to_send, self->data_to_send_len);
		write_line(self, "\x1a", 1);
		write_line(self, "\r\n", 2);
	}

	/* Reset the semaphore to avoid taking it immediatelly (if an unrelated response
	 * was received prior to taking it). */
	xSemaphoreTake(self->response_lock, 2);

	/* Wait for the response. */
	if (xSemaphoreTake(self->response_lock, timeout_ms) == pdFALSE) {
		/* No response was received within the specified timeout. */
		r = GSM_QUECTEL_RET_FAILED;
	} else {

		if (self->command_response == GSM_QUECTEL_CMD_RESPONSE_OK) {
			r = GSM_QUECTEL_RET_OK;
		} else {
			r = GSM_QUECTEL_RET_FAILED;
			u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("command '%s' caused an error, CME error code = %u"), command_str, self->cme_error_code);
		}
	}

	/* Allow the next command to be run. */
	xSemaphoreGive(self->command_lock);

	return r;
}


static gsm_quectel_ret_t wait_for_modem(GsmQuectel *self, uint32_t timeout_ms) {
	if (u_assert(self != NULL)) {
		return GSM_QUECTEL_RET_FAILED;
	}

	while (command(self, GSM_QUECTEL_CMD_TEST, 100) != GSM_QUECTEL_RET_OK) {
		if (timeout_ms > 100) {
			timeout_ms -= 100;
		} else {
			timeout_ms = 0;
		}

		if (timeout_ms == 0) {
			return GSM_QUECTEL_RET_FAILED;
		}
	}

	return GSM_QUECTEL_RET_OK;
}


static void gsm_quectel_process_task(void *p) {
	GsmQuectel *self = (GsmQuectel *)p;

	while (self->can_run) {
		size_t len;
		char buf[256];
		read_line(self, buf, sizeof(buf) - 1, &len);
		buf[len] = '\0';

		// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("-> %s"), buf);

		if (!strcmp(buf, "OK")) {

			if (self->current_command != GSM_QUECTEL_CMD_TCPIP_STAT) {
				self->current_command = GSM_QUECTEL_CMD_NONE;
				self->command_response = GSM_QUECTEL_CMD_RESPONSE_OK;
				xSemaphoreGive(self->response_lock);
			}
			continue;
		}
		if (!strcmp(buf, "SEND OK") && self->current_command == GSM_QUECTEL_CMD_IP_SEND) {
			self->current_command = GSM_QUECTEL_CMD_NONE;
			self->command_response = GSM_QUECTEL_CMD_RESPONSE_OK;
			xSemaphoreGive(self->response_lock);
			continue;
		}
		if (!strcmp(buf, "ERROR")) {
			self->current_command = GSM_QUECTEL_CMD_NONE;
			self->command_response = GSM_QUECTEL_CMD_RESPONSE_ERROR;
			xSemaphoreGive(self->response_lock);
			continue;
		}
		if (!strncmp(buf, "+CME ERROR:", 11)) {
			self->current_command = GSM_QUECTEL_CMD_NONE;
			self->command_response = GSM_QUECTEL_CMD_RESPONSE_CME;
			xSemaphoreGive(self->response_lock);
			continue;
		}

		if (self->current_command == GSM_QUECTEL_CMD_GET_IMSI && len == 15) {
			strcpy(self->imsi, buf);
			continue;
		}

		/* Non-standard command, there is no OK response, we need to handle it. */
		if (self->current_command == GSM_QUECTEL_CMD_GET_IP && len <= 15) {
			strcpy(self->local_ip, buf);
			self->command_response = GSM_QUECTEL_CMD_RESPONSE_OK;
			xSemaphoreGive(self->response_lock);
			continue;
		}

		if (self->current_command == GSM_QUECTEL_CMD_PDP_STATUS) {
			if (!strcmp(buf, "+CGACT: 1,0")) {
				self->pdp_active = false;
				continue;
			}

			if (!strcmp(buf, "+CGACT: 1,1")) {
				self->pdp_active = true;
				continue;
			}
		}

		if (self->current_command == GSM_QUECTEL_CMD_TCPIP_STAT) {
			/* TCP/IP status is processed as an URC command as the response from the AT+QISTAT
			 * AT command is not consistent with other commands (the state is received AFTER
			 * the command is finished with OK. */
			if (!strncmp(buf, "STATE", 5)) {
				self->current_command = GSM_QUECTEL_CMD_NONE;
				self->command_response = GSM_QUECTEL_CMD_RESPONSE_OK;
				xSemaphoreGive(self->response_lock);

				self->tcpip_status = GSM_QUECTEL_TCPIP_STATUS_UNKNOWN;
				// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("-> %s"), buf);

				if (!strncmp(buf, "STATE: IP INITIAL", 17)) {
					self->tcpip_status = GSM_QUECTEL_TCPIP_STATUS_IP_INITIAL;
				}
				if (!strncmp(buf, "STATE: IP START", 15)) {
					self->tcpip_status = GSM_QUECTEL_TCPIP_STATUS_IP_START;
				}
				if (!strncmp(buf, "STATE: IP CONFIG", 16)) {
					self->tcpip_status = GSM_QUECTEL_TCPIP_STATUS_IP_CONFIG;
				}
				if (!strncmp(buf, "STATE: IP IND", 13)) {
					self->tcpip_status = GSM_QUECTEL_TCPIP_STATUS_IP_IND;
				}
				if (!strncmp(buf, "STATE: IP GPRSACT", 17)) {
					self->tcpip_status = GSM_QUECTEL_TCPIP_STATUS_IP_GPRSACT;
				}
				if (!strncmp(buf, "STATE: IP STATUS", 16)) {
					self->tcpip_status = GSM_QUECTEL_TCPIP_STATUS_IP_STATUS;
				}
				if (!strncmp(buf, "STATE: TCP CONNECTING", 21)) {
					self->tcpip_status = GSM_QUECTEL_TCPIP_STATUS_TCP_CONNECTING;
				}
				if (!strncmp(buf, "STATE: UDP CONNECTING", 21)) {
					self->tcpip_status = GSM_QUECTEL_TCPIP_STATUS_UDP_CONNECTING;
				}
				if (!strncmp(buf, "STATE: IP CLOSE", 15)) {
					self->tcpip_status = GSM_QUECTEL_TCPIP_STATUS_IP_CLOSE;
				}
				if (!strncmp(buf, "STATE: CONNECT OK", 17)) {
					self->tcpip_status = GSM_QUECTEL_TCPIP_STATUS_CONNECT_OK;
				}
				if (!strncmp(buf, "STATE: PDP DEACT", 16)) {
					self->tcpip_status = GSM_QUECTEL_TCPIP_STATUS_PDP_DEACT;
				}

				// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("parsed state = %s"), gsm_quectel_tcpip_states[self->tcpip_status]);

				continue;
			}

		}

		if (self->current_command == GSM_QUECTEL_CMD_NONE) {
			if (!strcmp(buf, "RING")) {
				/* Incoming call. */
				u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("incoming call"));
				continue;
			}

			if (!strncmp(buf, "+", 1)) {
				//~ u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("URC %s"), buf);
			}

			if (!strncmp(buf, "+CREG: ", 7)) {
				if (!strncmp(buf, "+CREG: 1", 8)) {
					self->registered = true;
					u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("registered to the home network"));
				} else {
					self->registered = false;
					u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("not registered"));
				}
				continue;
			}

			if (!strncmp(buf, "+CFUN: ", 7)) {
				if (!strcmp(buf, "+CFUN: 0")) {
					self->modem_status = GSM_QUECTEL_MODEM_STATUS_MINIMUM;
				}
				if (!strcmp(buf, "+CFUN: 1")) {
					self->modem_status = GSM_QUECTEL_MODEM_STATUS_FULL;
				}
				if (!strcmp(buf, "+CFUN: 4")) {
					self->modem_status = GSM_QUECTEL_MODEM_STATUS_RFKILL;
				}
			}
		}
	}

	vTaskDelete(NULL);
}



static void gsm_quectel_main_task(void *p) {
	GsmQuectel *self = (GsmQuectel *)p;

	self->running = true;
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("starting"));

	gsm_quectel_power(self, true);
	if (wait_for_modem(self, 5000) != GSM_QUECTEL_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("no response from the modem"));
		gsm_quectel_stop(self);
	}

	/** @todo wait for command completion */
	//~ write_command(self, "AT+CREG=2");
	//~ vTaskDelay(200);
	command(self, GSM_QUECTEL_CMD_ENABLE_CREG_URC, 300);
	command(self, GSM_QUECTEL_CMD_DISABLE_ECHO, 300);

	/* Main modem loop. */
	uint32_t cnt = 0;
	enum gsm_quectel_tcpip_status last_tcpip_status = GSM_QUECTEL_TCPIP_STATUS_UNKNOWN;
	while (self->can_run) {

		/* Get status every 5 seconds. */
		if ((cnt % 50) == 0) {
			if (self->modem_status == GSM_QUECTEL_MODEM_STATUS_FULL) {
				command(self, GSM_QUECTEL_CMD_GET_IMSI, 300);
			}
		}

		/* Process the TCP/IP state machine every second. */
		if ((cnt % 5) == 0 && self->registered) {
			/* Get status. */
			command(self, GSM_QUECTEL_CMD_TCPIP_STAT, 500);

			/** @todo mute invalid status changes from unknown and to unknown */
			if (self->tcpip_status == GSM_QUECTEL_TCPIP_STATUS_UNKNOWN) {
				continue;
			}

			if (last_tcpip_status != self->tcpip_status) {
				u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("tcpip status %s -> %s"), gsm_quectel_tcpip_states[last_tcpip_status], gsm_quectel_tcpip_states[self->tcpip_status]);
			}
			last_tcpip_status = self->tcpip_status;

			switch (self->tcpip_status) {
				case GSM_QUECTEL_TCPIP_STATUS_IP_INITIAL:
					command(self, GSM_QUECTEL_CMD_START_TCPIP, 300);
					// vTaskDelay(5000);
					/* State changes to IP_START. */
					break;

				case GSM_QUECTEL_TCPIP_STATUS_IP_START:
					command(self, GSM_QUECTEL_CMD_ACTIVATE_CONTEXT, 150000);
					// vTaskDelay(2000);
					/* State changes to IP CONFIG, IP IND and IP GPRSACT. */
					break;

				case GSM_QUECTEL_TCPIP_STATUS_IP_CONFIG:
				case GSM_QUECTEL_TCPIP_STATUS_IP_IND:
					/* Just wait. */
					break;

				case GSM_QUECTEL_TCPIP_STATUS_IP_GPRSACT:
					command(self, GSM_QUECTEL_CMD_GET_IP, 150000);
					/* After the local IP is known, status becomes IP STATUS. */
					break;

				case GSM_QUECTEL_TCPIP_STATUS_IP_STATUS:
					self->tcpip_ready = true;
					vTaskDelay(5000);
					command(self, GSM_QUECTEL_CMD_CONNECT, 75000);

					break;

				case GSM_QUECTEL_TCPIP_STATUS_TCP_CONNECTING:
				case GSM_QUECTEL_TCPIP_STATUS_UDP_CONNECTING:
					break;

				case GSM_QUECTEL_TCPIP_STATUS_IP_CLOSE:
					self->tcp_ready = false;
					vTaskDelay(5000);
					command(self, GSM_QUECTEL_CMD_CONNECT, 75000);
					break;

				case GSM_QUECTEL_TCPIP_STATUS_CONNECT_OK:
					self->tcp_ready = true;

					self->data_to_send = (const uint8_t *)"lalala\n";
					self->data_to_send_len = 7;
					command(self, GSM_QUECTEL_CMD_IP_SEND, 1000);

					break;

				case GSM_QUECTEL_TCPIP_STATUS_PDP_DEACT:
				default:
					self->tcpip_ready = false;
					self->tcp_ready = false;
					break;
			}
		}


		cnt++;
		vTaskDelay(100);
	}

	gsm_quectel_power(self, false);
	self->running = false;
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("stopped"));

	vTaskDelete(NULL);
}

gsm_quectel_ret_t gsm_quectel_init(GsmQuectel *self) {
	if (u_assert(self != NULL)) {
		return GSM_QUECTEL_RET_FAILED;
	}

	memset(self, 0, sizeof(GsmQuectel));
	uhal_module_init(&self->module);

	self->command_lock = xSemaphoreCreateMutex();
	if (self->command_lock == NULL) {
		return GSM_QUECTEL_RET_FAILED;
	}

	self->response_lock = xSemaphoreCreateBinary();
	if (self->response_lock == NULL) {
		return GSM_QUECTEL_RET_FAILED;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialized"));

	return GSM_QUECTEL_RET_OK;
}


gsm_quectel_ret_t gsm_quectel_free(GsmQuectel *self) {
	if (u_assert(self != NULL)) {
		return GSM_QUECTEL_RET_FAILED;
	}

	vSemaphoreDelete(self->command_lock);
	vSemaphoreDelete(self->response_lock);

	return GSM_QUECTEL_RET_OK;
}


gsm_quectel_ret_t gsm_quectel_start(GsmQuectel *self) {
	if (u_assert(self != NULL)) {
		return GSM_QUECTEL_RET_FAILED;
	}

	if (self->running) {
		return GSM_QUECTEL_RET_FAILED;
	}

	self->can_run = true;

	xTaskCreate(gsm_quectel_process_task, "gsm_quectel_p", configMINIMAL_STACK_SIZE + 256, (void *)self, 2, &(self->process_task));
	if (self->process_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create module task (command processing)"));
		return GSM_QUECTEL_RET_FAILED;
	}

	xTaskCreate(gsm_quectel_main_task, "gsm_quectel_m", configMINIMAL_STACK_SIZE + 256, (void *)self, 2, &(self->main_task));
	if (self->main_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create module task (main)"));
		return GSM_QUECTEL_RET_FAILED;
	}

	return GSM_QUECTEL_RET_OK;
}


gsm_quectel_ret_t gsm_quectel_stop(GsmQuectel *self) {
	if (u_assert(self != NULL)) {
		return GSM_QUECTEL_RET_FAILED;
	}

	if (self->running == false) {
		return GSM_QUECTEL_RET_FAILED;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("requesting stop"));
	self->can_run = false;

	return GSM_QUECTEL_RET_OK;
}


gsm_quectel_ret_t gsm_quectel_set_pwrkey(GsmQuectel *self, uint32_t port, uint32_t pin) {
	if (u_assert(self != NULL)) {
		return GSM_QUECTEL_RET_FAILED;
	}

	self->pwrkey_port = port;
	self->pwrkey_pin = pin;

	return GSM_QUECTEL_RET_OK;
}


gsm_quectel_ret_t gsm_quectel_set_vddext(GsmQuectel *self, uint32_t port, uint32_t pin) {
	if (u_assert(self != NULL)) {
		return GSM_QUECTEL_RET_FAILED;
	}

	self->vddext_port = port;
	self->vddext_pin = pin;

	return GSM_QUECTEL_RET_OK;
}


gsm_quectel_ret_t gsm_quectel_power(GsmQuectel *self, bool power) {
	if (u_assert(self != NULL)) {
		return GSM_QUECTEL_RET_FAILED;
	}

	if (power) {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("powering up"));
		if (gpio_get(self->vddext_port, self->vddext_pin)) {
			u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("module is already powered up"));
			return GSM_QUECTEL_RET_OK;
		}

		/* At least 1s pulse on the PWR_KEY line is needed to power up
		 * the module. Use 1200ms to have some headroom. Module will
		 * power up and start to boot immediately. Wait and check if
		 * the VDD_EXT is at 2V8 level. */
		gpio_set(self->pwrkey_port, self->pwrkey_pin);
		vTaskDelay(1200);
		gpio_clear(self->pwrkey_port, self->pwrkey_pin);

		if (gpio_get(self->vddext_port, self->vddext_pin)) {
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("powered up"));
			return GSM_QUECTEL_RET_OK;
		} else {
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("module startup timeout"));
			return GSM_QUECTEL_RET_FAILED;
		}

	} else {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("powering down"));
		if (!gpio_get(self->vddext_port, self->vddext_pin)) {
			u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("module is already powered down"));
			return GSM_QUECTEL_RET_OK;
		}

		gpio_set(self->pwrkey_port, self->pwrkey_pin);
		vTaskDelay(1000);
		gpio_clear(self->pwrkey_port, self->pwrkey_pin);

		for (size_t i = 0; i < 150; i++) {
			vTaskDelay(100);
			if (!gpio_get(self->vddext_port, self->vddext_pin)) {
				u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("powered down"));
				return GSM_QUECTEL_RET_OK;
			}
		}

		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("module power down timeout"));
		return GSM_QUECTEL_RET_FAILED;
	}
}

gsm_quectel_ret_t gsm_quectel_set_usart(GsmQuectel *self, struct interface_stream *usart) {
	if (u_assert(self != NULL) ||
	    u_assert(usart != NULL)) {
		return GSM_QUECTEL_RET_FAILED;
	}

	self->usart = usart;

	return GSM_QUECTEL_RET_OK;

}
