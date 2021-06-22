/* SPDX-License-Identifier: BSD-2-Clause
 *
 * LoRa device configuration
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

#include <main.h>

/* Common functions and helpers for the CLI service. */
#include "cli_table_helper.h"
#include "cli.h"
#include "services/cli/system_cli_tree.h"

#include <interfaces/servicelocator.h>
#include <interfaces/lora.h>

#include "device_lora.h"


const struct treecli_node *device_lora_loraN = Node {
	Name "N",
	Commands {
		Command {
			Name "print",
			Exec device_lora_loraN_print,
		},
		Command {
			Name "export",
			Exec device_lora_loraN_export,
		},
		Command {
			Name "join",
			Exec device_lora_loraN_join,
		},
		Command {
			Name "leave",
			Exec device_lora_loraN_leave,
		},
		End
	},
	Values {
		Value {
			Name "mode",
			.set = device_lora_loraN_mode_set,
			Type TREECLI_VALUE_STR,
		},
		Value {
			Name "appkey",
			.set = device_lora_loraN_appkey_set,
			Type TREECLI_VALUE_STR,
		},
		Value {
			Name "appeui",
			.set = device_lora_loraN_appeui_set,
			Type TREECLI_VALUE_STR,
		},
		Value {
			Name "deveui",
			.set = device_lora_loraN_deveui_set,
			Type TREECLI_VALUE_STR,
		},
		Value {
			Name "devaddr",
			.set = device_lora_loraN_devaddr_set,
			Type TREECLI_VALUE_STR,
		},
		Value {
			Name "nwkskey",
			.set = device_lora_loraN_nwkskey_set,
			Type TREECLI_VALUE_STR,
		},
		Value {
			Name "appskey",
			.set = device_lora_loraN_appskey_set,
			Type TREECLI_VALUE_STR,
		},
		Value {
			Name "datarate",
			.set = device_lora_loraN_datarate_set,
			Type TREECLI_VALUE_UINT32,
		},
		Value {
			Name "adr",
			.set = device_lora_loraN_adr_set,
			Type TREECLI_VALUE_STR,
		},
		End
	},
};

const struct cli_table_cell device_lora_loraN_params[] = {
	{.type = TYPE_STRING, .size = 20, .alignment = ALIGN_LEFT}, /* device name */
	{.type = TYPE_STRING, .size = 20, .alignment = ALIGN_LEFT}, /* parameter name */
	{.type = TYPE_STRING, .size = 40, .alignment = ALIGN_LEFT}, /* parameter value */
	{.type = TYPE_END}
};


const struct cli_table_cell device_lora_table[] = {
	{.type = TYPE_STRING, .size = 20, .alignment = ALIGN_LEFT}, /* device name */
	{.type = TYPE_STRING, .size = 40, .alignment = ALIGN_LEFT}, /* devEUI */
	{.type = TYPE_STRING, .size = 20, .alignment = ALIGN_LEFT}, /* enabled */
	{.type = TYPE_END}
};


int32_t device_lora_print(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;

	table_print_header(cli->stream, device_lora_table, (const char *[]){
		"Device name",
		"devEUI",
		"enabled",
	});
	table_print_row_separator(cli->stream, device_lora_table);

	LoRa *lora = NULL;
	for (size_t i = 0; (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_LORA, i, (Interface **)&lora)) == ISERVICELOCATOR_RET_OK; i++) {
		const char *name = NULL;
		iservicelocator_get_name(locator, (Interface *)lora, &name);
		char deveui[40] = {0};

		if (lora->vmt->get_deveui != NULL) {
			lora->vmt->get_deveui(lora, deveui);
		}

		table_print_row(cli->stream, device_lora_table, (const union cli_table_cell_content []) {
			{.string = name},
			{.string = deveui},
			{.string = "?"},
		});
	}

	return 0;
}

/*
	table_print_header(cli->stream, files_fs_table, (const char *[]){
		"Filesystem name",
		"Space used",
		"Space total",
	});
	table_print_row_separator(cli->stream, files_fs_table);

	Fs *fs = NULL;
	for (size_t i = 0; (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_FS, i, (Interface **)&fs)) == ISERVICELOCATOR_RET_OK; i++) {
		const char *name = NULL;
		iservicelocator_get_name(locator, (Interface *)fs, &name);
	
		struct fs_info info = {0};
		fs->vmt->info(fs, &info);

		char used_str[20] = {0};
		size_to_str(info.size_used, used_str, sizeof(used_str));
		char total_str[20] = {0};
		size_to_str(info.size_total, total_str, sizeof(total_str));

		table_print_row(cli->stream, files_fs_table, (const union cli_table_cell_content []) {
			{.string = name},
			{.string = used_str},
			{.string = total_str},
		});
	}

	return 0;
}
*/

int32_t device_lora_loraN_create(struct treecli_parser *parser, uint32_t index, struct treecli_node *node, void *ctx) {
	(void)parser;
	(void)ctx;

	Interface *interface;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_LORA, index, &interface) == ISERVICELOCATOR_RET_OK) {
		const char *name = "";
		iservicelocator_get_name(locator, interface, &name);
		if (node->name != NULL) {
			strcpy(node->name, name);
		}
		node->commands = device_lora_loraN->commands;
		node->values = device_lora_loraN->values;
		node->subnodes = device_lora_loraN->subnodes;
		return 0;
	}
	return -1;
}


#define FIND_LORA LoRa *lora = NULL; \
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_LORA, DNODE_INDEX(parser, -1), (Interface **)&lora) != ISERVICELOCATOR_RET_OK) { \
		return 1;\
	}


int32_t device_lora_loraN_print(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;
	FIND_LORA;

	table_print_header(cli->stream, device_lora_loraN_params, (const char *[]){
		"LoRa device",
		"Parameter name",
		"Value"
	});
	table_print_row_separator(cli->stream, device_lora_loraN_params);


	const char *name = "";
	iservicelocator_get_name(locator, (Interface *)lora, &name);

	table_print_row(cli->stream, device_lora_loraN_params, (const union cli_table_cell_content []) {
		{.string = name},
		{.string = "enabled"},
		{.string = ""},
	});

	return 0;
}


int32_t device_lora_loraN_export(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;
	FIND_LORA;

	enum lora_mode lora_mode = LORA_MODE_NONE;
	if (lora->vmt->get_mode(lora, &lora_mode) != LORA_RET_OK) {
		return -1;
	}

	if (lora_mode == LORA_MODE_NONE) {
		return -2;
	}

	char s[40];
	if (lora_mode == LORA_MODE_ABP) {
		module_cli_output("mode = abp\r\n", cli);
	} else if (lora_mode == LORA_MODE_OTAA) {
		module_cli_output("mode = otaa\r\n", cli);

		if (lora->vmt->get_appkey(lora, s, sizeof(s)) == LORA_RET_OK) {
			char o[60];
			snprintf(o, sizeof(o), "appkey = \"%s\"\r\n", s);
			module_cli_output(o, cli);
		}
	}

	/* Common parameters for all modes */
	if (lora->vmt->get_deveui(lora, s) == LORA_RET_OK) {
		char o[60];
		snprintf(o, sizeof(o), "deveui = \"%s\"\r\n", s);
		module_cli_output(o, cli);
	}
	if (lora->vmt->get_appeui(lora, s, sizeof(s)) == LORA_RET_OK) {
		char o[60];
		snprintf(o, sizeof(o), "appeui = \"%s\"\r\n", s);
		module_cli_output(o, cli);
	}
	uint8_t dr = 0;
	if (lora->vmt->get_datarate(lora, &dr) == LORA_RET_OK) {
		char o[60];
		snprintf(o, sizeof(o), "datarate = %u\r\n", dr);
		module_cli_output(o, cli);
	}

	/* Always try to join after the parameters are set. */
	module_cli_output("join\r\n", cli);

	return 0;
}


int32_t device_lora_loraN_join(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;
	FIND_LORA;

	if (lora->vmt->join(lora) != LORA_RET_OK) {
		return -1;
	}

	return 0;
}


int32_t device_lora_loraN_leave(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;
	FIND_LORA;

	return 0;
}


int32_t device_lora_loraN_mode_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)value;
	(void)len;
	ServiceCli *cli = (ServiceCli *)parser->context;
	FIND_LORA;

	if (len == 4 && !strncmp(buf, "otaa", 4)) {
		if (lora->vmt->set_mode(lora, LORA_MODE_OTAA) != LORA_RET_OK) {
			module_cli_output("error: cannot set OTAA mode\r\n", cli);
			return -1;
		}
	} else if (len == 3 && !strncmp(buf, "abp", 3)) {
		if (lora->vmt->set_mode(lora, LORA_MODE_ABP) != LORA_RET_OK) {
			module_cli_output("error: cannot set ABP mode\r\n", cli);
			return -1;
		}
	} else {
		module_cli_output("error: unknown mode, valid are \"otaa\" | \"abp\"\r\n", cli);
		return -1;
	}

	return 0;
}


int32_t device_lora_loraN_appkey_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)value;
	(void)len;
	ServiceCli *cli = (ServiceCli *)parser->context;
	FIND_LORA;

	if (lora->vmt->set_appkey != NULL) {
		if (len != 32) {
			module_cli_output("error: appkey must be a 32 character long hexadecimal string\r\n", cli);
			return -1;
		}

		char appkey[32 + 1];
		memcpy(appkey, buf, 32);
		appkey[32] = '\0';
		lora_ret_t ret = lora->vmt->set_appkey(lora, appkey);

		if (ret == LORA_RET_MODE) {
			module_cli_output("error: wrong mode, set mode to OTAA first\r\n", cli);
			return -1;
		} else if (ret != LORA_RET_OK) {
			module_cli_output("error: cannot set appkey\r\n", cli);
			return -1;
		}
	} else {
		module_cli_output("error: set appkey not supported\r\n", cli);
		return -1;
	}

	return 0;
}


int32_t device_lora_loraN_appeui_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)value;
	(void)len;
	ServiceCli *cli = (ServiceCli *)parser->context;
	FIND_LORA;

	if (lora->vmt->set_appeui != NULL) {
		if (len != 16) {
			module_cli_output("error: appEUI must be a 16 character long hexadecimal string\r\n", cli);
			return -1;
		}

		char appeui[16 + 1];
		memcpy(appeui, buf, 16);
		appeui[16] = '\0';
		lora_ret_t ret = lora->vmt->set_appeui(lora, appeui);

		if (ret != LORA_RET_OK) {
			module_cli_output("error: cannot set appEUI\r\n", cli);
			return -1;
		}
	} else {
		module_cli_output("error: set appEUI not supported\r\n", cli);
		return -1;
	}

	return 0;
}


int32_t device_lora_loraN_deveui_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)value;
	(void)len;
	ServiceCli *cli = (ServiceCli *)parser->context;
	FIND_LORA;

	if (lora->vmt->set_deveui != NULL) {
		if (len != 16) {
			module_cli_output("error: devEUI must be a 16 character long hexadecimal string\r\n", cli);
			return -1;
		}

		char deveui[17];
		memcpy(deveui, buf, 16);
		deveui[16] = '\0';
		lora_ret_t ret = lora->vmt->set_deveui(lora, deveui);

		if (ret != LORA_RET_OK) {
			char e[50];
			snprintf(e, sizeof(e), "error: cannot set devEUI, ret = %d\r\n", ret);
			module_cli_output(e, cli);
			return -1;
		}
	} else {
		module_cli_output("error: set devEUI not supported\r\n", cli);
		return -1;
	}

	return 0;
}


int32_t device_lora_loraN_devaddr_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)value;
	(void)len;
	ServiceCli *cli = (ServiceCli *)parser->context;
	FIND_LORA;

	return 0;
}


int32_t device_lora_loraN_nwkskey_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)value;
	(void)len;
	ServiceCli *cli = (ServiceCli *)parser->context;
	FIND_LORA;

	return 0;
}


int32_t device_lora_loraN_appskey_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)value;
	(void)len;
	ServiceCli *cli = (ServiceCli *)parser->context;
	FIND_LORA;

	return 0;
}


int32_t device_lora_loraN_datarate_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)value;
	(void)len;
	ServiceCli *cli = (ServiceCli *)parser->context;
	FIND_LORA;

	if (lora->vmt->set_datarate != NULL) {
		uint32_t datarate = *(uint32_t *)buf;
		if (datarate > 5) {
			module_cli_output("error: range allowed 0-5\r\n", cli);
			return -1;
		}

		lora_ret_t ret = lora->vmt->set_datarate(lora, datarate);

		if (ret != LORA_RET_OK) {
			module_cli_output("error: cannot set datarate\r\n", cli);
			return -1;
		}
	} else {
		module_cli_output("error: datarate setting not supported\r\n", cli);
		return -1;
	}

	return 0;
}


int32_t device_lora_loraN_adr_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)value;
	(void)len;
	ServiceCli *cli = (ServiceCli *)parser->context;
	FIND_LORA;

	return 0;
}
