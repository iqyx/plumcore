/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Microchip RN2483 LoRaWAN modem driver
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */
#pragma once

#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"

#include <interface_stream.h>
#include <interfaces/lora.h>


/* The size must be sufficient for a 256 B packet encoded in hex. */
#define LORA_RESPONSE_LEN 540
#define LORA_TX_OK_TIMEOUT 10
#define LORA_RX_DATA_LEN 256

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
	struct interface_stream *usart;
	LoRa lora;

	SemaphoreHandle_t command_lock;
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

	bool join_req;
	bool sleeping;

	uint32_t status_vdd_mV;

	SemaphoreHandle_t rx_lock[16];
	SemaphoreHandle_t rx_data_release;

} LoraModem;


lora_modem_ret_t lora_modem_init(LoraModem *self, struct interface_stream *usart);
lora_modem_ret_t lora_modem_free(LoraModem *self);

lora_modem_ret_t lora_modem_regain_comms(LoraModem *self);
/* lora_modem_ret_t lora_modem_set_led(LoraModem *self, StatusLed *led); */
lora_modem_ret_t lora_modem_write(LoraModem *self, const char *buf);
lora_modem_ret_t lora_modem_write_line(LoraModem *self, const char *buf);
lora_modem_ret_t lora_modem_read_line(LoraModem *self, char *buf, size_t max_buf, size_t *read);
lora_modem_ret_t lora_modem_save(LoraModem *self);
lora_modem_ret_t lora_modem_get_vdd(LoraModem *self, uint32_t *vdd);
lora_modem_ret_t lora_modem_sleep(LoraModem *self, uint32_t len);
lora_modem_ret_t lora_modem_factory_reset(LoraModem *self);
lora_modem_ret_t lora_modem_reset(LoraModem *self);
lora_modem_ret_t lora_modem_get_ver(LoraModem *self, char *version, size_t ver_len);
lora_modem_ret_t lora_modem_set_ar(LoraModem *self, bool ar);

/* LoRa API */
lora_ret_t lora_modem_set_appkey(LoraModem *self, const char *appkey);
lora_ret_t lora_modem_set_appeui(LoraModem *self, const char *appeui);
lora_ret_t lora_modem_set_deveui(LoraModem *self, const char *deveui);
lora_ret_t lora_modem_get_deveui(LoraModem *self, char *deveui);
lora_ret_t lora_modem_set_devaddr(LoraModem *self, const uint8_t devaddr[4]);
lora_ret_t lora_modem_set_nwkskey(LoraModem *self, const char *nwkskey);
lora_ret_t lora_modem_set_appskey(LoraModem *self, const char *appskey);
lora_ret_t lora_modem_join_abp(LoraModem *self);
lora_ret_t lora_modem_join_otaa(LoraModem *self);
lora_ret_t lora_modem_leave(LoraModem *self);
lora_ret_t lora_modem_set_datarate(LoraModem *self, uint8_t datarate);
lora_ret_t lora_modem_set_adr(LoraModem *self, bool adr);

lora_ret_t lora_modem_send(LoraModem *self, uint8_t port, const uint8_t *data, size_t len);
lora_ret_t lora_modem_receive(LoraModem *self, uint8_t port, uint8_t *data, size_t size, size_t *len, uint32_t timeout);

