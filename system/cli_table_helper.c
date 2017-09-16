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

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

#include "interface_stream.h"
#include "cli_table_helper.h"

#define ESC_CURSOR_UP "\x1b[A"
#define ESC_CURSOR_DOWN "\x1b[B"
#define ESC_CURSOR_RIGHT "\x1b[C"
#define ESC_CURSOR_LEFT "\x1b[D"
#define ESC_DEFAULT "\x1b[0m"
#define ESC_BOLD "\x1b[1m"
#define ESC_CURSOR_SAVE "\x1b[s"
#define ESC_CURSOR_RESTORE "\x1b[u"
#define ESC_ERASE_LINE_END "\x1b[K"
#define ESC_COLOR_FG_BLACK "\x1b[30m"
#define ESC_COLOR_FG_RED "\x1b[31m"
#define ESC_COLOR_FG_GREEN "\x1b[32m"
#define ESC_COLOR_FG_YELLOW "\x1b[33m"
#define ESC_COLOR_FG_BLUE "\x1b[34m"
#define ESC_COLOR_FG_MAGENTA "\x1b[35m"
#define ESC_COLOR_FG_CYAN "\x1b[36m"
#define ESC_COLOR_FG_WHITE "\x1b[37m"

#define CLI_TABLE_HEADER_STYLE     ESC_ERASE_LINE_END ESC_COLOR_FG_WHITE ESC_BOLD
#define CLI_TABLE_SEPARATOR_STYLE  ESC_ERASE_LINE_END ESC_COLOR_FG_WHITE ESC_BOLD
#define CLI_TABLE_LINE_STYLE       ESC_ERASE_LINE_END ESC_COLOR_FG_WHITE
#define CLI_TABLE_VERTICAL_LINE    ' '
#define CLI_TABLE_HORIZONTAL_LINE  '-'
#define CLI_TABLE_LINE_CROSSING    '+'
#define CLI_TABLE_EXPAND           ' '


static void table_compute_expand(const struct cli_table_cell *cols, const struct cli_table_cell *cell, size_t content_size, size_t *left_expand, size_t *right_expand) {
	(void)cols;

	*left_expand = 0;
	*right_expand = 0;

	if (cell->alignment == ALIGN_RIGHT) {
		*left_expand = cell->size - content_size;
	}
	if (cell->alignment == ALIGN_CENTER) {
		*left_expand = (cell->size - content_size) / 2;
	}

	if (cell->alignment == ALIGN_LEFT) {
		*right_expand = cell->size - content_size;
	}
	if (cell->alignment == ALIGN_CENTER) {
		*right_expand = cell->size - content_size - *left_expand;
	}
}


/** @todo add max_len parameter */
static void table_align_cell(const struct cli_table_cell *cols, const struct cli_table_cell *cell, const char *content, char *output) {

	char *output_pos = output;
	size_t content_length = strlen(content);

	bool add_ellipsis = false;
	if (content_length > cell->size) {
		content_length = cell->size;
		add_ellipsis = true;
	}

	*output_pos = CLI_TABLE_VERTICAL_LINE;
	output_pos++;

	size_t left_expand, right_expand;
	table_compute_expand(cols, cell, content_length, &left_expand, &right_expand);

	for (size_t j = 0; j < left_expand; j++) {
		*output_pos = CLI_TABLE_EXPAND;
		output_pos++;
	}

	if (add_ellipsis) {
		strncpy(output_pos, content, content_length - 1);
		output_pos += content_length - 1;
		strcpy(output_pos, "…");
		output_pos += strlen("…");
	} else {
		strncpy(output_pos, content, content_length);
		output_pos += content_length;
	}

	for (size_t j = 0; j < right_expand; j++) {
		*output_pos = CLI_TABLE_EXPAND;
		output_pos++;
	}

	*output_pos = '\0';
	output_pos++;

}


static void table_render_cell(const struct cli_table_cell *cols, const struct cli_table_cell *cell, const union cli_table_cell_content content, char *output, size_t max_len) {
	(void)cols;

	switch (cell->type) {
		case TYPE_INT32:
			snprintf(output, max_len, cell->format ? cell->format : "%ld", content.int32);
			break;

		case TYPE_UINT32:
			snprintf(output, max_len, cell->format ? cell->format : "%lu", content.uint32);
			break;

		case TYPE_STRING:
			strncpy(output, content.string, max_len);
			break;

		case TYPE_KEY:
			strncpy(output, "<@todo key>", max_len);
			break;

		default:
			strncpy(output, "???", max_len);
	}
}


void table_print_header(struct interface_stream *stream, const struct cli_table_cell *cols, const char *headers[]) {
	interface_stream_write(stream, (const uint8_t *)(CLI_TABLE_HEADER_STYLE), strlen(CLI_TABLE_HEADER_STYLE));
	for (size_t i = 0; cols[i].type != TYPE_END; i++) {

		/* Cell size is size + vertical line character + \0 + optional UTF8 ellipsis. */
		char output[cols[i].size + 2 + 3];

		table_align_cell(cols, &(cols[i]), headers[i], output);

		interface_stream_write(stream, (const uint8_t *)output, strlen(output));
	}
	interface_stream_write(stream, (const uint8_t[]){CLI_TABLE_VERTICAL_LINE, '\r', '\n'}, 3);
	interface_stream_write(stream, (const uint8_t *)(ESC_DEFAULT), strlen(ESC_DEFAULT));
}


void table_print_row_separator(struct interface_stream *stream, const struct cli_table_cell *cols) {
	interface_stream_write(stream, (const uint8_t *)(CLI_TABLE_SEPARATOR_STYLE), strlen(CLI_TABLE_SEPARATOR_STYLE));
	for (size_t i = 0; cols[i].type != TYPE_END; i++) {

		char cell[cols[i].size + 2];
		char *cell_pos = cell;

		*cell_pos = CLI_TABLE_LINE_CROSSING;
		cell_pos++;

		for (size_t j = 0; j < cols[i].size; j++) {
			*cell_pos = CLI_TABLE_HORIZONTAL_LINE;
			cell_pos++;
		}

		*cell_pos = '\0';
		cell_pos++;

		interface_stream_write(stream, (const uint8_t *)cell, strlen(cell));
	}
	interface_stream_write(stream, (const uint8_t[]){CLI_TABLE_LINE_CROSSING, '\r', '\n'}, 3);
	interface_stream_write(stream, (const uint8_t *)(ESC_DEFAULT), strlen(ESC_DEFAULT));
}


void table_print_row(struct interface_stream *stream, const struct cli_table_cell *cols, const union cli_table_cell_content *content) {
	interface_stream_write(stream, (const uint8_t *)(CLI_TABLE_LINE_STYLE), strlen(CLI_TABLE_LINE_STYLE));
	for (size_t i = 0; cols[i].type != TYPE_END; i++) {

		/** @todo okay, meh */
		char content_str[50];

		table_render_cell(cols, &(cols[i]), content[i], content_str, 50);

		/* Cell size is size + vertical line character + \0 + optional UTF8 ellipsis. */
		char output[cols[i].size + 2 + 3];
		table_align_cell(cols, &(cols[i]), content_str, output);

		interface_stream_write(stream, (const uint8_t *)output, strlen(output));
	}
	interface_stream_write(stream, (const uint8_t[]){CLI_TABLE_VERTICAL_LINE, '\r', '\n'}, 3);
	interface_stream_write(stream, (const uint8_t *)(ESC_DEFAULT), strlen(ESC_DEFAULT));
}

