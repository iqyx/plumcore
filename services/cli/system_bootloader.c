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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"

/* Common functions and helpers for the CLI service. */
#include "cli_table_helper.h"
#include "services/cli.h"

/* Helper defines for tree construction. */
#include "services/cli/system_cli_tree.h"

#include "services/interfaces/servicelocator.h"

/* The port header is still needed for SFFS. */
#include "port.h"
#include "sffs.h"
#include "system_bootloader.h"


struct ubload_config ubload_current_config;

const struct cli_table_cell system_bootloader_config_table[] = {
	{.type = TYPE_STRING, .size = 30, .alignment = ALIGN_RIGHT}, /* config key */
	{.type = TYPE_STRING, .size = 30, .alignment = ALIGN_RIGHT}, /* config value */
	{.type = TYPE_END}
};


int32_t system_bootloader_config_load(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;

	module_cli_output("loading ubload configuration...\r\n", cli);

	struct sffs_file f;
	if (sffs_open(&fs, &f, "ubload.cfg", SFFS_READ) != SFFS_OPEN_OK) {
		module_cli_output("error: cannot open saved configuration\r\n", cli);
		return 1;
	}
	if (sffs_read(&f, (uint8_t *)&ubload_current_config, sizeof(struct ubload_config)) != sizeof(struct ubload_config)) {
		module_cli_output("error: configuration reading failed\r\n", cli);
		return 1;
	}
	sffs_close(&f);
	module_cli_output("OK\r\n", cli);

	return 0;
}


int32_t system_bootloader_config_print(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;

	table_print_header(cli->stream, system_bootloader_config_table, (const char *[]){
		"Config key",
		"Config value",
	});
	table_print_row_separator(cli->stream, system_bootloader_config_table);

	table_print_row(cli->stream, system_bootloader_config_table, (const union cli_table_cell_content []) {
		{.string = "Last working firmware"},
		{.string = ubload_current_config.fw_working},
	});
	table_print_row(cli->stream, system_bootloader_config_table, (const union cli_table_cell_content []) {
		{.string = "Firmware update request"},
		{.string = ubload_current_config.fw_request},
	});
	table_print_row(cli->stream, system_bootloader_config_table, (const union cli_table_cell_content []) {
		{.string = "Hostname"},
		{.string = ubload_current_config.host},
	});

	char tmp[10] = {0};

	snprintf(tmp, sizeof(tmp), "%d", ubload_current_config.serial_enabled);
	table_print_row(cli->stream, system_bootloader_config_table, (const union cli_table_cell_content []) {
		{.string = "Serial console enabled"},
		{.string = tmp},
	});
	snprintf(tmp, sizeof(tmp), "%lu", ubload_current_config.serial_speed);
	table_print_row(cli->stream, system_bootloader_config_table, (const union cli_table_cell_content []) {
		{.string = "Serial console speed"},
		{.string = tmp},
	});

	return 0;
}


int32_t system_bootloader_config_save(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;

	module_cli_output("saving ubload configuration...\r\n", cli);

	struct sffs_file f;
	if (sffs_open(&fs, &f, "ubload.cfg", SFFS_OVERWRITE) != SFFS_OPEN_OK) {
		module_cli_output("error: cannot open configuration file for writing\r\n", cli);
		return 1;
	}
	if (sffs_write(&f, (uint8_t *)&ubload_current_config, sizeof(struct ubload_config)) != sizeof(struct ubload_config)) {
		module_cli_output("error: configuration saving failed\r\n", cli);
		return 1;
	}
	sffs_close(&f);
	module_cli_output("OK\r\n", cli);

	return 0;
}


int32_t system_bootloader_config_host_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)ctx;
	(void)value;
	(void)parser;

	if (len < 32) {
		memcpy(ubload_current_config.host, buf, len);
		ubload_current_config.host[len] = '\0';
		return 0;
	}

	return 1;
}


int32_t system_bootloader_config_fw_working_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)ctx;
	(void)value;
	(void)parser;

	if (len < CONFIG_FIRMWARE_ID_LEN) {
		memcpy(ubload_current_config.fw_working, buf, len);
		ubload_current_config.fw_working[len] = '\0';
		return 0;
	}

	return 1;
}


int32_t system_bootloader_config_fw_request_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)ctx;
	(void)value;
	(void)parser;

	if (len < CONFIG_FIRMWARE_ID_LEN) {
		memcpy(ubload_current_config.fw_request, buf, len);
		ubload_current_config.fw_request[len] = '\0';
		return 0;
	}

	return 1;
}


int32_t system_bootloader_config_console_enabled_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)ctx;
	(void)value;
	(void)parser;
	(void)len;

	ubload_current_config.serial_enabled = *(bool *)buf;
	return 0;
}


int32_t system_bootloader_config_console_speed_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)ctx;
	(void)value;
	(void)parser;
	(void)len;

	ubload_current_config.serial_speed = *(uint32_t *)buf;
	return 0;
}
