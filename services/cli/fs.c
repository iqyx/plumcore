/* SPDX-License-Identifier: BSD-2-Clause
 *
 * FIFO in a flash device
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
#include "port.h"
#include <sha2.h>

#include "fs.h"

/* We need some globals to store filenames for commands. This is because the treecli
 * library still doesn't support proper command parameters. This has a drawback -
 * it is not possible to run multiple commands simultaneously. */
#define PATH_MAX 32
char fname_from[PATH_MAX];
char fname_to[PATH_MAX];
char fname[PATH_MAX];

enum print_format {
	PRINT_FORMAT_BIN,
	PRINT_FORMAT_HEX,
} print_format;

uint32_t file_seek = 0;
uint32_t file_size = 0;

const struct treecli_node *files_fsN = Node {
	Name "N",
	Commands {
		Command {
			Name "print",
			Exec files_fsN_print,
		},
		Command {
			Name "sha256sum",
			Exec files_fsN_sha256sum,
		},
		Command {
			Name "bspatch",
			// Exec files_fsN_print,
		},
		Command {
			Name "cat",
			Exec files_fsN_cat,
		},
		End
	},
	Values {
		Value {
			Name "from",
			.get_set_context = (void *)&fname_from,
			.set = files_fsN_fname_set,
			Type TREECLI_VALUE_STR,
		},
		Value {
			Name "to",
			.get_set_context = (void *)&fname_to,
			.set = files_fsN_fname_set,
			Type TREECLI_VALUE_STR,
		},
		Value {
			Name "name",
			.get_set_context = (void *)&fname,
			.set = files_fsN_fname_set,
			Type TREECLI_VALUE_STR,
		},
		Value {
			Name "seek",
			.value = &file_seek,
			Type TREECLI_VALUE_UINT32,
		},
		Value {
			Name "size",
			.value = &file_size,
			Type TREECLI_VALUE_UINT32,
		},
		Value {
			Name "format",
			.set = files_fsN_format_set,
			Type TREECLI_VALUE_STR,
		},
		End
	},
};

const struct cli_table_cell files_fs_table[] = {
	{.type = TYPE_STRING, .size = 32, .alignment = ALIGN_LEFT}, /* filesystem name */
	{.type = TYPE_STRING, .size = 20, .alignment = ALIGN_RIGHT}, /* space used */
	{.type = TYPE_STRING, .size = 20, .alignment = ALIGN_RIGHT}, /* space total */
	{.type = TYPE_END}
};

const struct cli_table_cell files_file_table[] = {
	{.type = TYPE_STRING, .size = 32, .alignment = ALIGN_LEFT}, /* file name */
	{.type = TYPE_STRING, .size = 20, .alignment = ALIGN_RIGHT}, /* file size */
	{.type = TYPE_END}
};



static char base85chars[85 + 1] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?&<>()[]{}@%$#";

static void cli_print_base85(ServiceCli *self, const uint8_t *buf, size_t len) {
	while ((len % 4) != 0) {
		len++;
	}

	uint32_t acc = 0;
	for (size_t i = 0; i < len; i++) {
		acc = acc << 8 | buf[i];
		if ((i % 4) == 3) {
			uint32_t d = 85 * 85 * 85 * 85;
			char out[5] = {0};
			for (size_t j = 0; j < 5; j++) {
				out[j] = base85chars[acc / d % 85];
				d /= 85;
			}
			interface_stream_write(self->stream, (const uint8_t *)out, 5);
		}
	}
}


static void size_to_str(size_t size, char *s, size_t len) {
	if (size <= 10 * 1024) {
		snprintf(s, len - 1, "%u B", size);
	} else if (size <= 10 * 1024 * 1024) {
		snprintf(s, len - 1, "%u KB", size / 1024);
	} else {
		snprintf(s, len - 1, "%u MB", size / 1024 / 1024);
	}

}


int32_t fs_print(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;

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


int32_t files_fsN_create(struct treecli_parser *parser, uint32_t index, struct treecli_node *node, void *ctx) {
	(void)parser;
	(void)ctx;

	Interface *interface;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_FS, index, &interface) == ISERVICELOCATOR_RET_OK) {
		const char *name = "";
		iservicelocator_get_name(locator, interface, &name);
		if (node->name != NULL) {
			strcpy(node->name, name);
		}
		node->commands = files_fsN->commands;
		node->values = files_fsN->values;
		node->subnodes = files_fsN->subnodes;
		return 0;
	}
	return -1;
}


int32_t files_fsN_print(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;

	/* Find the right filesystem */
	Fs *fs = NULL;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_FS, DNODE_INDEX(parser, -1), (Interface **)&fs) != ISERVICELOCATOR_RET_OK) {
		return 1;
	}

	table_print_header(cli->stream, files_file_table, (const char *[]){
		"File name",
		"Size",
	});
	table_print_row_separator(cli->stream, files_file_table);

	Dir d = {0};
	if (fs->vmt->opendir(fs, &d, "/") == FS_RET_OK) {
		DirEntry e = {0};
		char name[32] = {0};
		e.name = name;
		e.name_size = sizeof(name);
		while (fs->vmt->readdir(fs, &d, &e) == FS_RET_OK) {
			char size_str[20] = {0};
			size_to_str(e.size, size_str, sizeof(size_str));

			table_print_row(cli->stream, files_file_table, (const union cli_table_cell_content []) {
				{.string = e.name},
				{.string = size_str},
			});
		}
	}
	return 0;
}


int32_t files_fsN_sha256sum(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;

	Fs *fs = NULL;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_FS, DNODE_INDEX(parser, -1), (Interface **)&fs) != ISERVICELOCATOR_RET_OK) {
		return 1;
	}

	/* fname is a global, must be set beforehand */
	File f;
	if (fs->vmt->open(fs, &f, fname, FS_MODE_READONLY) != FS_RET_OK) {
		module_cli_output("cannot open file '", cli);
		module_cli_output(fname, cli);
		module_cli_output("'\r\n", cli);
		return 1;
	}

	size_t read = 0;
	uint8_t buf[256];
	sha256_ctx sctx;
	sha256_init(&sctx);
	while (fs->vmt->read(fs, &f, buf, sizeof(buf), &read) == FS_RET_OK) {
		sha256_update(&sctx, buf, read);
	}
	fs->vmt->close(fs, &f);
	uint8_t digest[SHA256_DIGEST_SIZE];
	sha256_final(&sctx, digest);

	for (size_t i = 0; i < SHA256_DIGEST_SIZE; i++) {
		char byte[3] = {0};
		snprintf(byte, sizeof(byte), "%02x", digest[i]);
		module_cli_output(byte, cli);
	}
	module_cli_output("  ", cli);
	module_cli_output(fname, cli);
	module_cli_output("\r\n", cli);

	return 0;
}


int32_t files_fsN_fname_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)value;
	(void)len;
	(void)parser;

	if (len >= PATH_MAX) {
		len = PATH_MAX - 1;
	}
	strncpy((char *)ctx, buf, len);
	((char *)ctx)[len] = '\0';
	return 0;
}


int32_t files_fsN_format_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)value;
	(void)len;
	(void)ctx;
	ServiceCli *cli = (ServiceCli *)parser->context;

	if (!strncmp(buf, "bin", 3) && len == 3) {
		print_format = PRINT_FORMAT_BIN;
	} else if (!strncmp(buf, "hex", 3) && len == 3) {
		print_format = PRINT_FORMAT_HEX;
	} else {
		module_cli_output("Unknown format, valid values are \"bin\" | \"hex\".\r\n", cli);
		return 1;
	}
	return 0;
}


int32_t files_fsN_cat(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;

	Fs *fs = NULL;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_FS, DNODE_INDEX(parser, -1), (Interface **)&fs) != ISERVICELOCATOR_RET_OK) {
		return 1;
	}

	/* fname is a global, must be set beforehand */
	File f;
	if (fs->vmt->open(fs, &f, fname, FS_MODE_READONLY) != FS_RET_OK) {
		module_cli_output("cannot open file '", cli);
		module_cli_output(fname, cli);
		module_cli_output("'\r\n", cli);
		return 1;
	}

	size_t read = 0;
	size_t read_total = 0;
	uint8_t buf[64];
	const char a[] = "0123456789abcdef";
	while (fs->vmt->read(fs, &f, buf, sizeof(buf), &read) == FS_RET_OK) {

		if (print_format == PRINT_FORMAT_HEX) {
			char s[sizeof(buf) * 2 + 1] = {0};
			for (size_t i = 0; i < read; i++) {
				s[i * 2] = a[buf[i] / 16];
				s[i * 2 + 1] = a[buf[i] % 16];
			}
			module_cli_output(s, cli);
			module_cli_output("\r\n", cli);
		}
		read_total += read;
	}
	fs->vmt->close(fs, &f);

	char s[32] = {0};
	snprintf(s, sizeof(s) - 1, "Total bytes read: %u\r\n", read_total);
	module_cli_output(s, cli);

	return 0;
}
