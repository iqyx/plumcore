/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Single-line editor with command history
 *
 * Copyright (c) 2014-2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <interfaces/stream.h>


typedef enum {
	LINEEDIT_RET_OK = 0,
	LINEEDIT_RET_FAILED,
	LINEEDIT_RET_ENTER,
	LINEEDIT_RET_TAB,
	LINEEDIT_RET_TIMEOUT,
	LINEEDIT_RET_END,
} lineedit_ret_t;


#define LINEEDIT_FG_COLOR_BLACK   30
#define LINEEDIT_FG_COLOR_RED     31
#define LINEEDIT_FG_COLOR_GREEN   32
#define LINEEDIT_FG_COLOR_YELLOW  33
#define LINEEDIT_FG_COLOR_BLUE    34
#define LINEEDIT_FG_COLOR_MAGENTA 35
#define LINEEDIT_FG_COLOR_CYAN    36
#define LINEEDIT_FG_COLOR_WHITE   37

#define LINEEDIT_BG_COLOR_BLACK   40
#define LINEEDIT_BG_COLOR_RED     41
#define LINEEDIT_BG_COLOR_GREEN   42
#define LINEEDIT_BG_COLOR_YELLOW  43
#define LINEEDIT_BG_COLOR_BLUE    44
#define LINEEDIT_BG_COLOR_MAGENTA 45
#define LINEEDIT_BG_COLOR_CYAN    46
#define LINEEDIT_BG_COLOR_WHITE   47

#define LE_CSI_CURSOR_LEFT    "D"
#define LE_CSI_CURSOR_RIGHT   "C"
#define LE_CSI_CURSOR_SAVE    "s"
#define LE_CSI_CURSOR_RESTORE "u"
#define LE_CSI_ERASE_LINE_END "K"
#define LE_CSI_FONT           "m"

enum lineedit_escape_state {
	LE_ESC_STATE_NONE,
	LE_ESC_STATE_ESC,
	LE_ESC_STATE_CSI,
	LE_ESC_STATE_OSC,
};


struct lineedit_conf {
	Stream *stream;
	const char *label;
	const char *text;
	char pwchar;
	uint8_t fg_color;
	uint8_t bg_color;
	uint32_t max_len;
	uint32_t history_max_len;
	uint32_t timeout;
};


typedef struct lineedit {
	struct lineedit_conf conf;

	uint32_t cursor;

	char *text;

	enum lineedit_escape_state escape;
	uint32_t csi_escape_mod;

	char *history;
	int32_t recall_index;
} LineEdit;


lineedit_ret_t lineedit_init(LineEdit *self, const struct lineedit_conf *conf);
lineedit_ret_t lineedit_free(LineEdit *self);
lineedit_ret_t lineedit_run(LineEdit *self);
lineedit_ret_t lineedit_get_text(LineEdit *self, char *text, size_t size);
lineedit_ret_t lineedit_set_text(LineEdit *self, const char *text);
lineedit_ret_t lineedit_insert(LineEdit *self, const char *text);
