/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Minimal JSON pull parser
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "json.h"


static const char *json_skip_ws(const char *p, const char *end) {
	while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
		p++;
	}
	return p;
}


/* Skip the string starting at the quote @p p, returns the position after the closing quote or NULL. */
static const char *json_skip_string(const char *p, const char *end) {
	for (p++; p < end; p++) {
		if (*p == '\\') {
			p++;
		} else if (*p == '"') {
			return p + 1;
		}
	}
	return NULL;
}


/* Skip the value starting at @p p including its content, returns the position after it or NULL. */
static const char *json_skip_value(const char *p, const char *end) {
	if (p >= end) {
		return NULL;
	}
	if (*p == '"') {
		return json_skip_string(p, end);
	}
	if (*p == '{' || *p == '[') {
		uint32_t depth = 0;
		while (p < end) {
			if (*p == '"') {
				p = json_skip_string(p, end);
				if (p == NULL) {
					return NULL;
				}
				continue;
			}
			if (*p == '{' || *p == '[') {
				depth++;
			} else if (*p == '}' || *p == ']') {
				depth--;
				if (depth == 0) {
					return p + 1;
				}
			}
			p++;
		}
		return NULL;
	}

	/* A number or a literal ends with a delimiter. */
	const char *start = p;
	while (p < end && strchr(",:]} \t\r\n", *p) == NULL) {
		p++;
	}
	return (p > start) ? p : NULL;
}


/* Move the cursor to the value following the position @p p, skipping the separator. */
static json_ret_t json_next(struct json_value *it, const char *p) {
	p = json_skip_ws(p, it->end);
	if (p < it->end && (*p == ',' || *p == ':')) {
		p = json_skip_ws(p + 1, it->end);
		if (p >= it->end || *p == it->close) {
			return JSON_RET_MALFORMED;
		}
	}
	it->pos = p;
	return JSON_RET_OK;
}


json_ret_t json_init(struct json_value *it, const char *buf, size_t len) {
	it->end = buf + len;
	it->close = '\0';
	it->pos = json_skip_ws(buf, it->end);
	return (it->pos < it->end) ? JSON_RET_OK : JSON_RET_MALFORMED;
}


bool json_at_end(const struct json_value *it) {
	return it->pos >= it->end || (it->close != '\0' && *it->pos == it->close);
}


enum json_type json_get_type(const struct json_value *it) {
	if (json_at_end(it)) {
		return JSON_TYPE_INVALID;
	}
	switch (*it->pos) {
		case '{':
			return JSON_TYPE_OBJECT;
		case '[':
			return JSON_TYPE_ARRAY;
		case '"':
			return JSON_TYPE_STRING;
		case 't':
		case 'f':
			return JSON_TYPE_BOOL;
		case 'n':
			return JSON_TYPE_NULL;
		default:
			return (*it->pos == '-' || (*it->pos >= '0' && *it->pos <= '9')) ? JSON_TYPE_NUMBER : JSON_TYPE_INVALID;
	}
}


json_ret_t json_advance(struct json_value *it) {
	if (json_at_end(it)) {
		return JSON_RET_FAILED;
	}
	const char *p = json_skip_value(it->pos, it->end);
	if (p == NULL) {
		return JSON_RET_MALFORMED;
	}
	return json_next(it, p);
}


json_ret_t json_enter_container(const struct json_value *it, struct json_value *child) {
	enum json_type type = json_get_type(it);
	if (type != JSON_TYPE_OBJECT && type != JSON_TYPE_ARRAY) {
		return JSON_RET_TYPE;
	}
	child->end = it->end;
	child->close = (type == JSON_TYPE_OBJECT) ? '}' : ']';
	child->pos = json_skip_ws(it->pos + 1, it->end);
	return (child->pos < child->end) ? JSON_RET_OK : JSON_RET_MALFORMED;
}


json_ret_t json_leave_container(struct json_value *it, const struct json_value *child) {
	if (child->pos >= child->end || *child->pos != child->close) {
		return JSON_RET_FAILED;
	}
	return json_next(it, child->pos + 1);
}


json_ret_t json_map_find_value(const struct json_value *map, const char *key, struct json_value *value) {
	struct json_value it;
	json_ret_t ret = json_enter_container(map, &it);
	if (ret != JSON_RET_OK) {
		return ret;
	}
	if (it.close != '}') {
		return JSON_RET_TYPE;
	}

	size_t key_len = strlen(key);
	while (!json_at_end(&it)) {
		if (json_get_type(&it) != JSON_TYPE_STRING) {
			return JSON_RET_MALFORMED;
		}
		bool match = (size_t)(it.end - it.pos) > key_len + 1 && !memcmp(it.pos + 1, key, key_len) &&
		             it.pos[key_len + 1] == '"';
		/* Move to the value, then past it if the key does not match. */
		if (json_advance(&it) != JSON_RET_OK || json_at_end(&it)) {
			return JSON_RET_MALFORMED;
		}
		if (match) {
			*value = it;
			return JSON_RET_OK;
		}
		if (json_advance(&it) != JSON_RET_OK) {
			return JSON_RET_MALFORMED;
		}
	}
	return JSON_RET_NOT_FOUND;
}


/* Parse 4 hex digits of a \u escape. */
static bool json_hex4(const char *p, const char *end, uint32_t *code) {
	if (end - p < 4) {
		return false;
	}
	*code = 0;
	for (size_t i = 0; i < 4; i++) {
		char c = p[i];
		uint32_t d = 0;
		if (c >= '0' && c <= '9') {
			d = (uint32_t)(c - '0');
		} else if (c >= 'a' && c <= 'f') {
			d = (uint32_t)(c - 'a' + 10);
		} else if (c >= 'A' && c <= 'F') {
			d = (uint32_t)(c - 'A' + 10);
		} else {
			return false;
		}
		*code = (*code << 4) | d;
	}
	return true;
}


json_ret_t json_copy_string(const struct json_value *it, char *buf, size_t size, size_t *len) {
	if (json_get_type(it) != JSON_TYPE_STRING) {
		return JSON_RET_TYPE;
	}
	if (size == 0) {
		return JSON_RET_OVERFLOW;
	}

	size_t n = 0;
	json_ret_t ret = JSON_RET_OK;
	const char *p = it->pos + 1;
	while (true) {
		if (p >= it->end) {
			ret = JSON_RET_MALFORMED;
			break;
		}
		if (*p == '"') {
			break;
		}

		/* A character encoded in up to 3 UTF-8 bytes. */
		char out[3];
		size_t out_len = 1;
		out[0] = *p++;
		if (out[0] == '\\') {
			if (p >= it->end) {
				ret = JSON_RET_MALFORMED;
				break;
			}
			char e = *p++;
			uint32_t code = 0;
			switch (e) {
				case 'b':
					out[0] = '\b';
					break;
				case 'f':
					out[0] = '\f';
					break;
				case 'n':
					out[0] = '\n';
					break;
				case 'r':
					out[0] = '\r';
					break;
				case 't':
					out[0] = '\t';
					break;
				case 'u':
					if (!json_hex4(p, it->end, &code)) {
						ret = JSON_RET_MALFORMED;
						break;
					}
					p += 4;
					if (code < 0x80) {
						out[0] = (char)code;
					} else if (code < 0x800) {
						out[0] = (char)(0xc0 | (code >> 6));
						out[1] = (char)(0x80 | (code & 0x3f));
						out_len = 2;
					} else if (code >= 0xd800 && code < 0xe000) {
						out[0] = '?';
					} else {
						out[0] = (char)(0xe0 | (code >> 12));
						out[1] = (char)(0x80 | ((code >> 6) & 0x3f));
						out[2] = (char)(0x80 | (code & 0x3f));
						out_len = 3;
					}
					break;
				default:
					/* \" \\ \/ */
					out[0] = e;
					break;
			}
			if (ret != JSON_RET_OK) {
				break;
			}
		}

		if (n + out_len >= size) {
			ret = JSON_RET_OVERFLOW;
			break;
		}
		memcpy(buf + n, out, out_len);
		n += out_len;
	}

	buf[n] = '\0';
	if (len != NULL) {
		*len = n;
	}
	return ret;
}


json_ret_t json_get_int(const struct json_value *it, int64_t *value) {
	if (json_get_type(it) != JSON_TYPE_NUMBER) {
		return JSON_RET_TYPE;
	}
	const char *p = it->pos;
	bool negative = (*p == '-');
	if (negative) {
		p++;
	}
	if (p >= it->end || *p < '0' || *p > '9') {
		return JSON_RET_MALFORMED;
	}
	uint64_t u = 0;
	while (p < it->end && *p >= '0' && *p <= '9') {
		uint64_t d = (uint64_t)(*p - '0');
		if (u > (UINT64_MAX - d) / 10) {
			return JSON_RET_OVERFLOW;
		}
		u = u * 10 + d;
		p++;
	}
	/* A fraction or an exponent makes it a non-integer number. */
	if (p < it->end && (*p == '.' || *p == 'e' || *p == 'E')) {
		return JSON_RET_TYPE;
	}
	if (u > (negative ? (uint64_t)INT64_MAX + 1 : (uint64_t)INT64_MAX)) {
		return JSON_RET_OVERFLOW;
	}
	*value = negative ? (int64_t)(0 - u) : (int64_t)u;
	return JSON_RET_OK;
}


json_ret_t json_get_bool(const struct json_value *it, bool *value) {
	size_t left = (size_t)(it->end - it->pos);
	if (json_get_type(it) == JSON_TYPE_BOOL) {
		if (left >= 4 && !memcmp(it->pos, "true", 4)) {
			*value = true;
			return JSON_RET_OK;
		}
		if (left >= 5 && !memcmp(it->pos, "false", 5)) {
			*value = false;
			return JSON_RET_OK;
		}
	}
	return JSON_RET_TYPE;
}
