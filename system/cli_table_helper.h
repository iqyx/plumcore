/*
 * CLI table rendering helpers
 *
 * Copyright (C) 2016, Marek Koza, qyx@krtko.org
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
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "lineedit.h"

#include "interface_stream.h"

#ifndef ESC_COLOR_FG_WHITE
#error ESC_ERASE_LINE_END
#endif

#define CLI_TABLE_HEADER_STYLE     ESC_ERASE_LINE_END ESC_COLOR_FG_WHITE ESC_BOLD
#define CLI_TABLE_SEPARATOR_STYLE  ESC_ERASE_LINE_END ESC_COLOR_FG_WHITE ESC_BOLD
#define CLI_TABLE_LINE_STYLE       ESC_ERASE_LINE_END ESC_COLOR_FG_WHITE
#define CLI_TABLE_VERTICAL_LINE    ' '
#define CLI_TABLE_HORIZONTAL_LINE  '-'
#define CLI_TABLE_LINE_CROSSING    '+'
#define CLI_TABLE_EXPAND           ' '


enum cli_table_cell_alignment {
	ALIGN_LEFT = 0,
	ALIGN_RIGHT,
	ALIGN_CENTER
};

enum cli_table_cell_content_type {
	TYPE_END = 0,
	TYPE_INT32,
	TYPE_UINT32,
	TYPE_STRING,
	TYPE_KEY
};

union cli_table_cell_content {
	int32_t int32;
	uint32_t uint32;
	const char *string;
	uint8_t *key;
};

struct cli_table_cell {
	enum cli_table_cell_content_type type;
	const char *format;
	size_t size;
	enum cli_table_cell_alignment alignment;
};


void table_print_header(struct interface_stream *stream, const struct cli_table_cell *cols, const char *headers[]);
void table_print_row_separator(struct interface_stream *stream, const struct cli_table_cell *cols);
void table_print_row(struct interface_stream *stream, const struct cli_table_cell *cols, const union cli_table_cell_content *content);
