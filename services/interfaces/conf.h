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
	CONF_ENUM,
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

union conf_constraint {
	struct {
		union conf_val min;
		union conf_val max;
	} numeric;
	struct {
		size_t max_len;
		const char *regexp;
	} str;
	struct {
		size_t max_len;
	} bstr;
	struct {
		const char * const *values;
		size_t count;
	} enumeration;
};

typedef struct conf Conf;
struct conf_vmt {
	/**
	 * @brief Write configuration value
	 *
	 * @return CONF_RET_OK if the value was written successfully,
	 *         CONF_RET_FAILED if the value is not writable or the write failed.
	 */
	conf_ret_t (*write)(Conf *self, const union conf_val val);

	/**
	 * @brief Read configuration value
	 *
	 * @return CONF_RET_OK if the value was read successfully,
	 *         CONF_RET_FAILED if the value is not readable or the read failed.
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
	 * @return CONF_RET_OK if the value was created successfully,
	 *         CONF_RET_FAILED if creation is not supported at this position or the name is not unique.
	 */
	conf_ret_t (*create)(Conf *self, const char *name, Conf **new);

	/**
	 * @brief Query name, type and flags of the current value
	 *
	 * @param self The current value instance
	 * @param name Human readable name of the Conf node, valid for the lifetime of the node.
	 * @param type Type of the value (see @p enum conf_type).
	 * @param flags Flags describing the value's capabilities (see @p enum conf_flag).
	 * @return CONF_RET_OK if the stat was successful,
	 *         CONF_RET_FAILED otherwise.
	 */
	conf_ret_t (*stat)(Conf *self, const char **name, enum conf_type *type, enum conf_flag *flags);

	/**
	 * @brief Get the default value of the current Conf node
	 *
	 * Returns the default value in @p val using the same typed union as @p read and @p write.
	 * The default can be used by a CLI or user interface to show the factory/reset value, or by
	 * a configuration loader to populate a node when no stored value is present.
	 *
	 * @return CONF_RET_OK if a default value is available and was returned in @p val,
	 *         CONF_RET_FAILED if no default is defined for this node.
	 */
	conf_ret_t (*get_default)(Conf *self, union conf_val *val);

	/**
	 * @brief Get a human-readable description of the current value
	 *
	 * Returns up to two description strings for the interface user's convenience, eg. for display
	 * in a CLI help system or a user interface tooltip. Either pointer may be set to NULL if the
	 * corresponding description is not available.
	 *
	 * @p brief is a short, single-line description of the Conf node. It is recommended to keep it
	 * under 80 characters so it fits on a single terminal line without wrapping.
	 *
	 * @p detail is a longer, optionally multi-line string closely describing the Conf node: its
	 * purpose, the meaning of its value, valid ranges, units, side effects, etc.
	 *
	 * @return CONF_RET_OK if at least one description string is available,
	 *         CONF_RET_FAILED if no description is available.
	 */
	conf_ret_t (*get_description)(Conf *self, const char **brief, const char **detail);

	/**
	 * @brief Get constraints for the current value
	 *
	 * Returns type-appropriate constraints via @p union conf_constraint for the convenience of
	 * the interface user. The active member of the union matches the node's type: @p numeric for
	 * F/U8/S8/U16/S16/U32/S32, @p str for CONF_STR, @p bstr for CONF_BSTR, @p enumeration for
	 * CONF_ENUM. Constraints can be used to pre-check a value before writing it, eg. to validate
	 * user input in a CLI or to populate a user interface with the allowable value range.
	 *
	 * @return CONF_RET_OK if constraints are available and were returned in @p c,
	 *         CONF_RET_FAILED if no constraints are set or the type does not support them.
	 */
	conf_ret_t (*get_constraints)(Conf *self, union conf_constraint *c);

	/**
	 * @brief Destroy/delete the current value
	 *
	 * Remove the current value from the configuration tree and delete its
	 * content. This functionality is implementation defined (ie. it is used
	 * only when the dynamic tree functionality is required).
	 *
	 * @return CONF_RET_OK if the value was destroyed successfully,
	 *         CONF_RET_FAILED if destruction is not supported or the value cannot be removed.
	 */
	conf_ret_t (*destroy)(Conf *self);
};

typedef struct conf {
	const struct conf_vmt *vmt;
	void *parent;

} Conf;
