/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Minimal JSON pull parser
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * A JSON document in a memory buffer is walked with a cursor (struct json_value), similar to tinycbor's CborValue.
 * Nothing is allocated and nothing is copied until a value is requested, the buffer must stay valid while the
 * cursors are used.
 *
 * The cursor points to a single value. json_advance() moves it to the next value of the same container,
 * json_enter_container() creates a cursor iterating the values of an object or an array and
 * json_leave_container() moves the container cursor past the container once the child cursor reached its end.
 * Inside an object the keys and the values alternate, the key is a string value followed by its value, like
 * a CBOR map. json_map_find_value() looks up a value by its key.
 *
 *     struct json_value root, entries, entry, name;
 *     json_init(&root, buf, len);
 *     json_map_find_value(&root, "entries", &entries);
 *     json_enter_container(&entries, &entry);
 *     while (!json_at_end(&entry)) {
 *         json_map_find_value(&entry, "name", &name);
 *         json_copy_string(&name, str, sizeof(str), NULL);
 *         json_advance(&entry);
 *     }
 *
 * The parser checks the syntax only as far as it walks the document. Keys are compared without unescaping.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>


typedef enum {
	JSON_RET_OK = 0,
	JSON_RET_FAILED,
	/** The document is not valid JSON or ended unexpectedly. */
	JSON_RET_MALFORMED,
	/** The value is of a different type than requested. */
	JSON_RET_TYPE,
	/** The key was not found in the object. */
	JSON_RET_NOT_FOUND,
	/** The value does not fit the output buffer or type. */
	JSON_RET_OVERFLOW,
} json_ret_t;

enum json_type {
	/** The cursor is at the end of its container or at a malformed value. */
	JSON_TYPE_INVALID = 0,
	JSON_TYPE_NULL,
	JSON_TYPE_BOOL,
	JSON_TYPE_NUMBER,
	JSON_TYPE_STRING,
	JSON_TYPE_ARRAY,
	JSON_TYPE_OBJECT,
};

struct json_value {
	/* Start of the current value, or the end of the container. */
	const char *pos;
	/* End of the buffer. */
	const char *end;
	/* Character closing the iterated container, '\0' at the top level. */
	char close;
};


/**
 * @brief Create a cursor pointing to the top level value of the document in @p buf
 *
 * @return JSON_RET_MALFORMED if the document is empty, JSON_RET_OK otherwise.
 */
json_ret_t json_init(struct json_value *it, const char *buf, size_t len);

/**
 * @brief Test whether the cursor is past the last value of its container
 */
bool json_at_end(const struct json_value *it);

/**
 * @brief Get the type of the value the cursor points to
 */
enum json_type json_get_type(const struct json_value *it);

/**
 * @brief Move the cursor to the next value of its container, skipping the current value including its content
 *
 * @return JSON_RET_MALFORMED if the current value cannot be skipped, JSON_RET_OK otherwise.
 */
json_ret_t json_advance(struct json_value *it);

/**
 * @brief Create the cursor @p child iterating the values of the object or array @p it points to
 *
 * @return JSON_RET_TYPE if @p it is not an object or an array, JSON_RET_OK otherwise.
 */
json_ret_t json_enter_container(const struct json_value *it, struct json_value *child);

/**
 * @brief Move the cursor @p it past its container once @p child reached the container end
 *
 * @return JSON_RET_FAILED if @p child is not at the end, JSON_RET_OK otherwise.
 */
json_ret_t json_leave_container(struct json_value *it, const struct json_value *child);

/**
 * @brief Find the value of the key @p key in the object @p map points to
 *
 * @return JSON_RET_TYPE if @p map is not an object, JSON_RET_NOT_FOUND if there is no such key,
 *         JSON_RET_MALFORMED on a malformed object, JSON_RET_OK otherwise.
 */
json_ret_t json_map_find_value(const struct json_value *map, const char *key, struct json_value *value);

/**
 * @brief Copy the unescaped string value as a NUL terminated string
 *
 * Unicode escapes are converted to UTF-8, surrogate pairs are replaced by '?'.
 *
 * @param len Length of the copied string without the terminator, may be NULL
 * @return JSON_RET_TYPE if the value is not a string, JSON_RET_OVERFLOW if it does not fit @p size (the copy is
 *         truncated), JSON_RET_MALFORMED on a malformed string, JSON_RET_OK otherwise.
 */
json_ret_t json_copy_string(const struct json_value *it, char *buf, size_t size, size_t *len);

/**
 * @brief Get an integer number value
 *
 * @return JSON_RET_TYPE if the value is not an integer number, JSON_RET_OVERFLOW if it does not fit int64_t,
 *         JSON_RET_OK otherwise.
 */
json_ret_t json_get_int(const struct json_value *it, int64_t *value);

/**
 * @brief Get a boolean value
 *
 * @return JSON_RET_TYPE if the value is not a boolean, JSON_RET_OK otherwise.
 */
json_ret_t json_get_bool(const struct json_value *it, bool *value);
