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

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "stream_buffer.h"

#include "interface_stream.h"
#include "interfaces/tcpip.h"
#include "interfaces/cellular.h"


typedef enum {
	GSM_QUECTEL_RET_OK = 0,
	GSM_QUECTEL_RET_FAILED = -1,
} gsm_quectel_ret_t;

enum gsm_quectel_command {
	GSM_QUECTEL_CMD_NONE = 0,
	GSM_QUECTEL_CMD_TEST,
	GSM_QUECTEL_CMD_GET_IMSI,
	GSM_QUECTEL_CMD_GET_IMEI,
	GSM_QUECTEL_CMD_GET_OPERATOR,
	GSM_QUECTEL_CMD_DISABLE_ECHO,
	GSM_QUECTEL_CMD_ENABLE_CREG_URC,
	GSM_QUECTEL_CMD_START_TCPIP,
	GSM_QUECTEL_CMD_ACTIVATE_CONTEXT,
	GSM_QUECTEL_CMD_DEACTIVATE_CONTEXT,
	GSM_QUECTEL_CMD_TCPIP_STAT,
	GSM_QUECTEL_CMD_ENABLE_TCPIP_MUX,
	GSM_QUECTEL_CMD_CONNECT,
	GSM_QUECTEL_CMD_GET_IP,
	GSM_QUECTEL_CMD_PDP_STATUS,
	GSM_QUECTEL_CMD_PDP_CGDCONT,
	GSM_QUECTEL_CMD_PDP_ACTIVATE,
	GSM_QUECTEL_CMD_IP_SEND,
	GSM_QUECTEL_CMD_GET_REGISTRATION,
	GSM_QUECTEL_CMD_ENABLE_RFTXMON,
	GSM_QUECTEL_CMD_IP_RECV,
	GSM_QUECTEL_CMD_IP_RECV_METHOD,
	GSM_QUECTEL_CMD_IP_CLOSE,
	GSM_QUECTEL_CMD_SET_CHARACTER_SET,
	GSM_QUECTEL_CMD_SEND_USSD,
	GSM_QUECTEL_CMD_SETUP_SMS_TEXT_MODE,
	GSM_QUECTEL_CMD_SETUP_SMS_NOTIFICATION,
	GSM_QUECTEL_CMD_READ_SMS,
	GSM_QUECTEL_CMD_DEL_ALL_SMS,

};

enum gsm_quectel_command_response {
	GSM_QUECTEL_CMD_RESPONSE_OK = 0,
	GSM_QUECTEL_CMD_RESPONSE_ERROR,
	GSM_QUECTEL_CMD_RESPONSE_CME,
};

enum gsm_quectel_tcpip_status {
	GSM_QUECTEL_TCPIP_STATUS_UNKNOWN = 0,
	GSM_QUECTEL_TCPIP_STATUS_IP_INITIAL,
	GSM_QUECTEL_TCPIP_STATUS_IP_START,
	GSM_QUECTEL_TCPIP_STATUS_IP_CONFIG,
	GSM_QUECTEL_TCPIP_STATUS_IP_IND,
	GSM_QUECTEL_TCPIP_STATUS_IP_GPRSACT,
	GSM_QUECTEL_TCPIP_STATUS_IP_STATUS,
	GSM_QUECTEL_TCPIP_STATUS_TCP_CONNECTING,
	GSM_QUECTEL_TCPIP_STATUS_UDP_CONNECTING,
	GSM_QUECTEL_TCPIP_STATUS_IP_CLOSE,
	GSM_QUECTEL_TCPIP_STATUS_CONNECT_OK,
	GSM_QUECTEL_TCPIP_STATUS_PDP_DEACT,
	GSM_QUECTEL_TCPIP_STATUS_MAX,
};

enum gsm_quectel_modem_status {
	GSM_QUECTEL_MODEM_STATUS_MINIMUM = 0,
	GSM_QUECTEL_MODEM_STATUS_FULL,
	GSM_QUECTEL_MODEM_STATUS_RFKILL,
};


typedef struct {
	Module module;

	struct interface_stream *usart;
	ITcpIp tcpip;

	uint32_t pwrkey_port;
	uint32_t pwrkey_pin;

	uint32_t vddext_port;
	uint32_t vddext_pin;

	bool can_run;
	bool running;

	TaskHandle_t main_task;
	TaskHandle_t process_task;

	SemaphoreHandle_t command_lock;
	SemaphoreHandle_t response_lock;
	volatile enum gsm_quectel_command current_command;
	volatile enum gsm_quectel_command_response command_response;
	volatile uint32_t cme_error_code;

	bool pdp_active;
	bool registered;
	enum cellular_modem_status cellular_status;
	char operator[20];

	volatile enum gsm_quectel_tcpip_status tcpip_status;
	volatile enum gsm_quectel_modem_status modem_status;
	volatile bool tcpip_ready;
	volatile bool tcp_ready;

	char imsi[20];
	char imei[20];
	char local_ip[20];

	const uint8_t *data_to_send;
	size_t data_to_send_len;

	// uint8_t *data_to_receive;
	// size_t data_to_receive_len;
	// size_t data_to_receive_read;
	SemaphoreHandle_t data_waiting;

	StreamBufferHandle_t rxdata;

	ITcpIpSocket tcpip_socket;
	volatile bool tcpip_socket_used;
	const char *tcpip_address;
	uint16_t tcpip_remote_port;

	const char *ussd_request_string;
	char *ussd_response_string;
	size_t ussd_response_size;

	ICellular cellular;

	bool debug_commands;
	bool debug_responses;


} GsmQuectel;


gsm_quectel_ret_t gsm_quectel_init(GsmQuectel *self);
gsm_quectel_ret_t gsm_quectel_free(GsmQuectel *self);
gsm_quectel_ret_t gsm_quectel_start(GsmQuectel *self);
gsm_quectel_ret_t gsm_quectel_stop(GsmQuectel *self);

gsm_quectel_ret_t gsm_quectel_set_pwrkey(GsmQuectel *self, uint32_t port, uint32_t pin);
gsm_quectel_ret_t gsm_quectel_set_vddext(GsmQuectel *self, uint32_t port, uint32_t pin);

gsm_quectel_ret_t gsm_quectel_power(GsmQuectel *self, bool power);
gsm_quectel_ret_t gsm_quectel_set_usart(GsmQuectel *self, struct interface_stream *usart);

ITcpIp *gsm_quectel_tcpip(GsmQuectel *self);
