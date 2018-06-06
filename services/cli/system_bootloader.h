/*
 * Copyright (c) 2017, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>


#define DNODE_INDEX(p, i) p->pos.levels[p->pos.depth + i].dnode_index
#define CONFIG_FIRMWARE_ID_LEN 32

enum ubload_config_led_mode {
	LED_MODE_OFF,
	LED_MODE_STILL_ON,
	LED_MODE_BASIC,
	LED_MODE_DIAG
};

struct ubload_config {
	/* Console serial port settings */
	bool serial_enabled;
	uint32_t serial_speed;

	/* Diagnostic LED settings */
	enum ubload_config_led_mode led_mode;

	/* Bootloader enter setup */
	bool cli_enabled;
	uint32_t wait_time;
	char enter_key;
	char skip_key;

	/* CLI inactivity time setup */
	uint32_t idle_time;

	/* System identification */
	char host[32];

	/* Independend watchdog */
	bool watchdog_enabled;

	/* XMODEM configuration. */
	uint32_t xmodem_retry_count;
	uint32_t xmodem_timeout;

	char fw_working[CONFIG_FIRMWARE_ID_LEN];
	char fw_request[CONFIG_FIRMWARE_ID_LEN];
};




int32_t system_bootloader_config_load(struct treecli_parser *parser, void *exec_context);
int32_t system_bootloader_config_print(struct treecli_parser *parser, void *exec_context);
int32_t system_bootloader_config_save(struct treecli_parser *parser, void *exec_context);
int32_t system_bootloader_config_host_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len);
int32_t system_bootloader_config_fw_working_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len);
int32_t system_bootloader_config_fw_request_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len);
int32_t system_bootloader_config_console_enabled_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len);
int32_t system_bootloader_config_console_speed_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len);



