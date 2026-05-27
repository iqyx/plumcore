/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Single-line editor with command history
 *
 * Copyright (c) 2014-2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include <main.h>
#include "lineedit.h"


static lineedit_ret_t lineedit_print(LineEdit *self, const char *s) {
	if (u_assert(self != NULL) || u_assert(self->conf.stream != NULL)) {
		return LINEEDIT_RET_FAILED;
	}
	if (self->conf.stream->vmt->write(self->conf.stream, s, strlen(s)) != STREAM_RET_OK) {
		return LINEEDIT_RET_FAILED;
	}
	return LINEEDIT_RET_OK;
}


static lineedit_ret_t lineedit_csi(LineEdit *self, const char *csi, ...) {
	if (u_assert(self != NULL)) {
		return LINEEDIT_RET_FAILED;
	}
	self->conf.stream->vmt->write(self->conf.stream, "\x1b[", 2);
	if (strcmp(csi, LE_CSI_FONT) == 0) {
		va_list ap;
		va_start(ap, csi);
		int param = va_arg(ap, int);
		va_end(ap);
		char buf[12];
		snprintf(buf, sizeof(buf), "%d%s", param, csi);
		self->conf.stream->vmt->write(self->conf.stream, buf, strlen(buf));
	} else {
		self->conf.stream->vmt->write(self->conf.stream, csi, strlen(csi));
	}
	return LINEEDIT_RET_OK;
}


static lineedit_ret_t lineedit_history_append(LineEdit *self, const char *line) {
	if (u_assert(self != NULL) || u_assert(line != NULL)) {
		return LINEEDIT_RET_FAILED;
	}
	if (self->conf.history_max_len == 0) {
		return LINEEDIT_RET_OK;
	}
	for (uint32_t i = self->conf.history_max_len - 1; i > 0; i--) {
		strlcpy(self->history + (i * self->conf.max_len), self->history + ((i - 1) * self->conf.max_len), self->conf.max_len);
	}
	strlcpy(self->history, line, self->conf.max_len);
	return LINEEDIT_RET_OK;
}


static lineedit_ret_t lineedit_history_recall(LineEdit *self, char **line, int32_t index) {
	if (u_assert(self != NULL) || u_assert(line != NULL)) {
		return LINEEDIT_RET_FAILED;
	}
	if (index >= (int32_t)self->conf.history_max_len || index < -1) {
		return LINEEDIT_RET_FAILED;
	}
	if (index == -1) {
		*line = "";
	} else {
		*line = self->history + (index * self->conf.max_len);
	}
	return LINEEDIT_RET_OK;
}


static lineedit_ret_t lineedit_backspace(LineEdit *self) {
	if (u_assert(self != NULL)) {
		return LINEEDIT_RET_FAILED;
	}
	if (strlen(self->text) == 0 || self->cursor == 0) {
		return LINEEDIT_RET_FAILED;
	}
	self->cursor--;
	lineedit_csi(self, LE_CSI_CURSOR_LEFT);

	int32_t i = self->cursor;
	while (self->text[i] != 0) {
		self->text[i] = self->text[i + 1];
		i++;
	}

	lineedit_csi(self, LE_CSI_CURSOR_SAVE);
	i = self->cursor;
	while (self->text[i]) {
		char ch[2] = {(self->conf.pwchar != '\0') ? self->conf.pwchar : self->text[i], '\0'};
		lineedit_print(self, ch);
		i++;
	}
	lineedit_csi(self, LE_CSI_ERASE_LINE_END);
	lineedit_csi(self, LE_CSI_CURSOR_RESTORE);
	return LINEEDIT_RET_OK;
}


static lineedit_ret_t lineedit_insert_char(LineEdit *self, int c) {
	if (u_assert(self != NULL)) {
		return LINEEDIT_RET_FAILED;
	}
	if (c < 32 || c > 127) {
		return LINEEDIT_RET_FAILED;
	}
	if ((self->conf.max_len - strlen(self->text) - 1) == 0) {
		return LINEEDIT_RET_FAILED;
	}

	int32_t i = strlen(self->text);
	while (i >= (int32_t)self->cursor) {
		self->text[i + 1] = self->text[i];
		i--;
	}
	self->text[self->cursor] = c;
	self->cursor++;

	char ch[2] = {(self->conf.pwchar != '\0') ? self->conf.pwchar : c, '\0'};
	lineedit_print(self, ch);

	lineedit_csi(self, LE_CSI_CURSOR_SAVE);
	i = self->cursor;
	while (self->text[i]) {
		char rest[2] = {(self->conf.pwchar != '\0') ? self->conf.pwchar : self->text[i], '\0'};
		lineedit_print(self, rest);
		i++;
	}
	lineedit_csi(self, LE_CSI_CURSOR_RESTORE);
	return LINEEDIT_RET_OK;
}


lineedit_ret_t lineedit_init(LineEdit *self, const struct lineedit_conf *conf) {
	if (u_assert(self != NULL) || u_assert(conf != NULL) || u_assert(conf->max_len > 0)) {
		return LINEEDIT_RET_FAILED;
	}
	memset(self, 0, sizeof(LineEdit));
	memcpy(&self->conf, conf, sizeof(self->conf));
	self->escape = LE_ESC_STATE_NONE;
	self->recall_index = -1;

	if (self->conf.timeout == 0) {
		self->conf.timeout = 60000ul;
	}
	self->text = calloc(1, self->conf.max_len);
	if (self->text == NULL) {
		lineedit_free(self);
		return LINEEDIT_RET_FAILED;
	}
	if (self->conf.text != NULL) {
		strlcpy(self->text, self->conf.text, self->conf.max_len);
		self->cursor = strlen(self->text);
	}
	if (self->conf.history_max_len > 0) {
		self->history = calloc(self->conf.history_max_len, self->conf.max_len);
		if (self->history == NULL) {
			lineedit_free(self);
			return LINEEDIT_RET_FAILED;
		}
	}
	return LINEEDIT_RET_OK;
}


lineedit_ret_t lineedit_free(LineEdit *self) {
	if (u_assert(self != NULL)) {
		return LINEEDIT_RET_FAILED;
	}
	free(self->history);
	free(self->text);
	self->history = NULL;
	self->text = NULL;
	return LINEEDIT_RET_OK;
}




static lineedit_ret_t lineedit_refresh(LineEdit *self) {
	if (u_assert(self != NULL)) {
		return LINEEDIT_RET_FAILED;
	}
	lineedit_print(self, "\r");
	lineedit_csi(self, LE_CSI_ERASE_LINE_END);

	lineedit_csi(self, LE_CSI_FONT, 0);
	if (self->conf.label != NULL) {
		lineedit_print(self, self->conf.label);
	}
	if (self->conf.fg_color != 0) {
		lineedit_csi(self, LE_CSI_FONT, (int)self->conf.fg_color);
	}
	if (self->conf.bg_color != 0) {
		lineedit_csi(self, LE_CSI_FONT, (int)self->conf.bg_color);
	}

	/* Create an edit box. */
	lineedit_print(self, " ");
	lineedit_csi(self, LE_CSI_CURSOR_SAVE);
	for (uint32_t i = 0; i <= self->conf.max_len; i++) {
		lineedit_print(self, " ");
	}
	lineedit_csi(self, LE_CSI_CURSOR_RESTORE);

	bool cursor_saved = false;
	uint32_t i = 0;
	while (self->text[i] != '\0') {
		if (self->cursor == i) {
			lineedit_csi(self, LE_CSI_CURSOR_SAVE);
			cursor_saved = true;
		}
		char ch[2] = {(self->conf.pwchar != '\0') ? self->conf.pwchar : self->text[i], '\0'};
		lineedit_print(self, ch);
		i++;
	}

	if (cursor_saved) {
		lineedit_csi(self, LE_CSI_CURSOR_RESTORE);
	}


	return LINEEDIT_RET_OK;
}


lineedit_ret_t lineedit_run(LineEdit *self) {
	if (u_assert(self != NULL)) {
		return LINEEDIT_RET_FAILED;
	}

	while (true) {
		uint8_t c;
		size_t n;
		stream_ret_t r = self->conf.stream->vmt->read_timeout(self->conf.stream, &c, 1, &n, self->conf.timeout);
		if (r == STREAM_RET_TIMEOUT) {
			lineedit_csi(self, LE_CSI_FONT, 0);
			return LINEEDIT_RET_TIMEOUT;
		}
		if (r != STREAM_RET_OK) {
			lineedit_csi(self, LE_CSI_FONT, 0);
			return LINEEDIT_RET_FAILED;
		}

		if (self->escape == LE_ESC_STATE_NONE) {
			switch (c) {
				case 0x04:
					lineedit_csi(self, LE_CSI_FONT, 0);
					return LINEEDIT_RET_END;

				case 0x09:
					lineedit_csi(self, LE_CSI_FONT, 0);
					return LINEEDIT_RET_TAB;

				case 0x0a:
				case 0x0b:
				case 0x0c:
				case 0x0d:
					lineedit_history_append(self, self->text);
					self->recall_index = -1;
					lineedit_csi(self, LE_CSI_FONT, 0);
					return LINEEDIT_RET_ENTER;

				case 0x12:
					lineedit_refresh(self);
					break;

				case 0x18:
				case 0x1a:
					self->escape = LE_ESC_STATE_NONE;
					break;

				case 0x1b:
					self->escape = LE_ESC_STATE_ESC;
					break;

				case 0x7f:
					lineedit_backspace(self);
					break;

				case 0x9b:
					self->escape = LE_ESC_STATE_CSI;
					self->csi_escape_mod = 0;
					break;

				default:
					if (c >= 32 && c <= 126) {
						lineedit_insert_char(self, c);
					}
					break;
			}

		} else if (self->escape == LE_ESC_STATE_ESC) {
			if (c == '[') {
				self->escape = LE_ESC_STATE_CSI;
				self->csi_escape_mod = 0;
			} else if (c == ']') {
				self->escape = LE_ESC_STATE_OSC;
			}

		} else if (self->escape == LE_ESC_STATE_CSI) {
			switch (c) {
				case '0': case '1': case '2': case '3': case '4':
				case '5': case '6': case '7': case '8': case '9':
					self->csi_escape_mod = self->csi_escape_mod * 10 + (c - '0');
					break;

				case 'A': {
					char *hist;
					if (lineedit_history_recall(self, &hist, self->recall_index + 1) == LINEEDIT_RET_OK) {
						lineedit_set_text(self, hist);
						self->recall_index++;
					}
					break;
				}

				case 'B': {
					char *hist;
					if (lineedit_history_recall(self, &hist, self->recall_index - 1) == LINEEDIT_RET_OK) {
						lineedit_set_text(self, hist);
						self->recall_index--;
					}
					break;
				}

				case 'C':
					if (self->cursor < strlen(self->text)) {
						self->cursor++;
						lineedit_csi(self, LE_CSI_CURSOR_RIGHT);
					}
					break;

				case 'D':
					if (self->cursor > 0) {
						self->cursor--;
						lineedit_csi(self, LE_CSI_CURSOR_LEFT);
					}
					break;

				case '~':
					lineedit_backspace(self);
					break;

				default:
					break;
			}
			self->escape = LE_ESC_STATE_NONE;

		} else if (self->escape == LE_ESC_STATE_OSC) {
			self->escape = LE_ESC_STATE_NONE;
		}
	}
}


lineedit_ret_t lineedit_get_text(LineEdit *self, char *text, size_t size) {
	if (u_assert(self != NULL) || u_assert(text != NULL) || u_assert(size > 0)) {
		return LINEEDIT_RET_FAILED;
	}
	strlcpy(text, self->text, size);
	return LINEEDIT_RET_OK;
}


lineedit_ret_t lineedit_set_text(LineEdit *self, const char *text) {
	if (u_assert(self != NULL) || u_assert(text != NULL)) {
		return LINEEDIT_RET_FAILED;
	}
	strlcpy(self->text, text, self->conf.max_len);
	self->cursor = strlen(self->text);
	return lineedit_refresh(self);
}


lineedit_ret_t lineedit_insert(LineEdit *self, const char *text) {
	if (u_assert(self != NULL) || u_assert(text != NULL)) {
		return LINEEDIT_RET_FAILED;
	}
	while (*text) {
		lineedit_insert_char(self, *text);
		text++;
	}
	return LINEEDIT_RET_OK;
}
