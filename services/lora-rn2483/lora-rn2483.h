/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Microchip RN2483 LoRaWAN modem driver
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */
#pragma once

#include <stdint.h>

#include <main.h>

#include <interfaces/stream.h>
#include <interfaces/lora.h>
#include <interfaces/power.h>


/* The size must be sufficient for a 256 B packet encoded in hex. */
#define LORA_RESPONSE_LEN 540
#define LORA_TX_OK_TIMEOUT 10
#define LORA_RX_DATA_LEN 256
#define LORA_MAX_ERRORS_BEFORE_REJOIN 10

typedef enum {
	LORA_MODEM_RET_OK = 0,
	LORA_MODEM_RET_FAILED = 1,
	LORA_MODEM_RET_BAD_PARAM = 2,
	LORA_MODEM_RET_INVALID_PARAM = 3,
	LORA_MODEM_RET_ACCEPTED = 4,
	LORA_MODEM_RET_DENIED = 5,
	LORA_MODEM_RET_NOT_JOINED = 6,
	LORA_MODEM_RET_NO_FREE_CHANNEL = 7,
	LORA_MODEM_RET_SILENT = 8,
	LORA_MODEM_RET_FRAME_COUNTER_ERROR = 9,
	LORA_MODEM_RET_BUSY = 10,
	LORA_MODEM_RET_PAUSED = 11,
	LORA_MODEM_RET_INVALID_DATA_LEN = 12,
	LORA_MODEM_RET_TX_OK = 13,
	LORA_MODEM_RET_RX = 14,
	LORA_MODEM_RET_TX_ERR = 15,
} lora_modem_ret_t;

enum lora_modem_mac_state {
	LORA_MODEM_MAC_STATE_IDLE = 0,
	LORA_MODEM_MAC_STATE_TX = 1,
	LORA_MODEM_MAC_STATE_BEFORE_RX1 = 2,
	LORA_MODEM_MAC_STATE_RX1 = 3,
	LORA_MODEM_MAC_STATE_BEFORE_RX2 = 4,
	LORA_MODEM_MAC_STATE_RX2 = 5,
	LORA_MODEM_MAC_STATE_RET_DELAY = 6,
	LORA_MODEM_MAC_STATE_APB_DELAY = 7,
	LORA_MODEM_MAC_STATE_CLASS_C_RX2_1 = 8,
	LORA_MODEM_MAC_STATE_CLASS_C_RX2_2 = 9,
};

typedef struct {
	Stream *usart;
	Power *power;
	LoRa lora;
	enum lora_mode lora_mode;

	SemaphoreHandle_t command_lock;
	UBaseType_t api_prio;
	bool can_run;
	bool running;

	char response_str[LORA_RESPONSE_LEN];
	size_t response_len;

	uint8_t rx_data[LORA_RX_DATA_LEN];
	size_t rx_data_len;
	uint8_t rx_data_port;

	/* LoRaWAN status */
	enum lora_modem_mac_state mac_state;
	bool joined;
	bool automatic_reply_enabled;
	bool adr_enabled;
	bool silent_immediately;
	bool mac_paused;
	bool rx_done;
	bool line_check_enabled;
	bool channels_updated;
	bool power_updated;
	bool nbrep_updated;
	bool prescaler_updated;
	bool rx2_updated;
	bool rx_timing_updated;
	bool rejoin_needed;
	bool multicast_enabled;

	volatile bool join_req;
	bool sleeping;

	uint32_t status_vdd_mV;
	Sensor vdd;

	SemaphoreHandle_t rx_lock[16];
	SemaphoreHandle_t rx_data_release;

	bool debug_requests;
	bool debug_responses;

	/* Reading keys is not supported. We have to remember them. */
	char appkey[32 + 1];
	char nwkskey[32 + 1];
	char appskey[32 + 1];

	/* DevEUI and AppEUI are required for OTAA rejoin. */
	char deveui[16 + 1];
	char appeui[16 + 1];

	uint32_t error_counter;

} LoraModem;


lora_modem_ret_t lora_modem_init(LoraModem *self, Stream *usart, Power *power);
lora_modem_ret_t lora_modem_free(LoraModem *self);

lora_modem_ret_t lora_modem_regain_comms(LoraModem *self);
/* lora_modem_ret_t lora_modem_set_led(LoraModem *self, StatusLed *led); */
lora_modem_ret_t lora_modem_write(LoraModem *self, const char *buf);
lora_modem_ret_t lora_modem_write_line(LoraModem *self, const char *buf);
lora_modem_ret_t lora_modem_clear_input(LoraModem *self);
lora_modem_ret_t lora_modem_read_line(LoraModem *self, char *buf, size_t max_buf, size_t *read);
lora_modem_ret_t lora_modem_save(LoraModem *self);
lora_modem_ret_t lora_modem_get_vdd(LoraModem *self, uint32_t *vdd);
lora_modem_ret_t lora_modem_sleep(LoraModem *self, uint32_t len);
lora_modem_ret_t lora_modem_factory_reset(LoraModem *self);
lora_modem_ret_t lora_modem_reset(LoraModem *self);
lora_modem_ret_t lora_modem_get_ver(LoraModem *self, char *version, size_t ver_len);
lora_modem_ret_t lora_modem_set_ar(LoraModem *self, bool ar);
