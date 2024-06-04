/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Interface for exposing a configuration tree
 *
 * Copyright (c) 2024, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
	CONF_RET_OK = 0,
	CONF_RET_FAILED,
} conf_ret_t;

enum conf_dir {
	CONF_DIR_NEXT,
	CONF_DIR_PREV,
	CONF_DIR_CHILD,
	CONF_DIR_UP,
};

enum conf_type {
	CONF_NONE,
	CONF_SUBTREE,
	CONF_F,
	CONF_U32,
	CONF_S32,
	CONF_U16,
	CONF_S16,
	CONF_U8,
	CONF_S8,
	CONF_B,
	CONF_BSTR,
	CONF_STR,
};

enum conf_flag {
	/** The configuration value can be read back once written. Not all values are readable, in that case
	 *  a shadow value may be used to provide a value to read or the value may be flagged as not readable. */
	CONF_READ = (1 << 0),

	/** Writable configuration value. Examples of values which cannot be written are locked values
	 *  (eg. calibration values), constants, status values, etc. */
	CONF_WRITE = (1 << 1),

	/** Value is a constant generated in compile time or discovered/computed once during the runtime.
	 *  It cannot change later. */
	CONF_CONST = (1 << 2),

	/** Status values change frequently during the runtime and are not writable, eg. packet counters. */
	CONF_STATUS = (1 << 3),

	/** If the value is flagged as detailed, it is not essential to be configured or displayed to the user.
	 *  Those values are shown only when in "expert" mode in the GUI or print with "detail" modifier in the CLI. */
	CONF_DETAIL = (1 << 4),

	/** The value can be used to make new children - dynamic subtree functionality. */
	CONF_CREATE = (1 << 5),
};

union conf_val {
	float f;
	uint32_t u32;
	int32_t i32;
	uint16_t u16;
	int16_t i16;
	uint8_t u8;
	int8_t i8;
	bool b;
	struct {
		uint8_t *buf;
		size_t len;
	} bstr;
	struct {
		char *buf;
		size_t len;
	} str;
};

typedef struct conf Conf;
struct conf_vmt {
	/**
	 * @brief Write configuration value
	 */
	conf_ret_t (*write)(Conf *self, const union conf_val val);

	/**
	 * @brief Read configuration value
	 */
	conf_ret_t (*read)(Conf *self, union conf_val *val);

	/**
	 * @brief Walk the configuration tree
	 *
	 * Walk the configuration tree in the specified direction. Use @p CONF_DIR_NEXT
	 * and @p CONF_DIR_PREV to walk siblings of the current value. Use
	 * @p CONF_DIR_CHILD to dive into a subtree, @p CONF_DIR_UP to go back up -
	 * the first child or the parent is returned in this case.
	 *
	 * @param self Instance of the value to start walking from
	 * @param direction Direction of the walk (see @p enum conf_dir)
	 * @param next Result of the walk. Returns NULL when there is nothing
	 *             left to walk.
	 * @return CONF_RET_OK if the walk was successful,
	 *         CONF_RET_FAILED otherwise.
	 */
	conf_ret_t (*walk)(Conf *self, enum conf_dir direction, Conf **next);

	/**
	 * @brief Create a configuration value/subtree
	 *
	 * Create a new value @p name as a child of the current value. Beware this
	 * functionality must be implemented and the value type and behaviour depends on the
	 * position in the tree and the implementation itself.
	 * The current value must be flagged as @p CONF_CREATE.
	 *
	 * @param self The current value instance
	 * @param name Human readable value identifier, must be unique.
	 * @param new Instance of the newly created configuration value.
	 */
	conf_ret_t (*create)(Conf *self, const char *name, Conf **new);

	conf_ret_t (*stat)(Conf *self, const char **name, enum conf_type *type);

	/**
	 * @brief Destroy/delete the current value
	 *
	 * Remove the current value from the configuration tree and delete its
	 * content. This functionality is implementation defined (ie. it is used
	 * only when the dynamic tree functionality is required).
	 */
	conf_ret_t (*destroy)(Conf *self);
};

typedef struct conf {
	const struct conf_vmt *vmt;
	void *parent;

} Conf;
