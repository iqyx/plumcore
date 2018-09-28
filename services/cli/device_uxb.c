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
#include "cli.h"

/* Helper defines for tree construction. */
#include "services/cli/system_cli_tree.h"

#include "services/interfaces/servicelocator.h"
#include "interfaces/uxbbus.h"
#include "interfaces/uxbdevice.h"
#include "interfaces/uxbslot.h"
#include "port.h"

#include "device_uxb.h"

/**
 * Tables for printing.
 */

const struct cli_table_cell device_uxb_table[] = {
	{.type = TYPE_STRING, .size = 16, .alignment = ALIGN_RIGHT}, /* name */
	{.type = TYPE_STRING, .size = 20, .alignment = ALIGN_RIGHT}, /* address */
	{.type = TYPE_STRING, .size = 16, .alignment = ALIGN_RIGHT}, /* hw-version */
	{.type = TYPE_STRING, .size = 16, .alignment = ALIGN_RIGHT}, /* fw-version */
	{.type = TYPE_STRING, .size = 16, .alignment = ALIGN_RIGHT}, /* status */
	{.type = TYPE_END}
};


/**
 * Informational and printing commands.
 */

static void id_to_str(uint8_t id[8], char *s) {
	sprintf(s, "%02x%02x.%02x%02x.%02x%02x.%02x%02x", id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7]);
}


int32_t device_uxb_print(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;

	table_print_header(cli->stream, device_uxb_table, (const char *[]){
		"Device",
		"Address",
		"HW version",
		"FW version",
		"status",
	});
	table_print_row_separator(cli->stream, device_uxb_table);

	Interface *interface;
	for (size_t i = 0; (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_UXBDEVICE, i, &interface)) != ISERVICELOCATOR_RET_FAILED; i++) {
		IUxbDevice *device = (IUxbDevice *)interface;
		char *name = "";
		iuxbdevice_get_name(device, &name);

		uint8_t id[8] = {0};
		iuxbdevice_get_id(device, id);
		char id_str[25] = {0};
		id_to_str(id, id_str);

		char *hw_version = "";
		iuxbdevice_get_hardware_version(device, &hw_version);

		char *fw_version = "";
		iuxbdevice_get_firmware_version(device, &fw_version);

		table_print_row(cli->stream, device_uxb_table, (const union cli_table_cell_content []) {
			{.string = name},
			{.string = id_str},
			{.string = hw_version},
			{.string = fw_version},
			{.string = ""},
		});
	}
	return 0;
}


