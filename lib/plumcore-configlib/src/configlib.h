/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Library for managing configuration trees in plumCore
 *
 * Copyright (c) 2024, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <interfaces/conf.h>



typedef enum {
	CONFIGLIB_RET_OK = 0,
	CONFIGLIB_RET_FAILED,
} configlib_ret_t;


typedef struct configlib_value ConfiglibValue;

typedef struct configlib_value {
	Conf conf;
	ConfiglibValue *next;
	ConfiglibValue *child;
	/* Parent reference is important to avoid recursion while walking. */
	ConfiglibValue *parent;

	void *var;
	size_t *len;
	size_t size;

	const char *name;
	enum conf_type type;
	enum conf_flag flags;

	bool has_default;
	union conf_val default_val;

	bool has_constraints;
	union conf_constraint constraint;

	const char *brief;
	const char *detail;

} ConfiglibValue;


configlib_ret_t configlib_init(ConfiglibValue *self, const char *name);
configlib_ret_t configlib_map(ConfiglibValue *self, void *var, enum conf_type type);
configlib_ret_t configlib_map_string(ConfiglibValue *self, char *str, size_t size);
configlib_ret_t configlib_append(ConfiglibValue *self, ConfiglibValue *parent, enum conf_dir dir);
configlib_ret_t configlib_set_default(ConfiglibValue *self, union conf_val val);
configlib_ret_t configlib_set_description(ConfiglibValue *self, const char *brief, const char *detail);
configlib_ret_t configlib_set_constraint(ConfiglibValue *self, union conf_constraint c);

configlib_ret_t configlib_init_map(ConfiglibValue *self, const char *name, void *var, enum conf_type type);
configlib_ret_t configlib_init_map_append(ConfiglibValue *self, const char *name, void *var, enum conf_type type, ConfiglibValue *parent, enum conf_dir dir);

/* Render a leaf node's value into @p buf as human-readable text. Only the scalar and string types are
 * printed: integers, float, bool, text strings and byte strings (as a space-separated 0x.. hex dump).
 * Non-value types (subtrees, unreadable nodes) and anything else yield an empty string. */
void configlib_value_str(Conf *self, enum conf_type type, char *buf, size_t size);

conf_ret_t configlib_log_value(Conf *self, uint32_t indent);
conf_ret_t configlib_log_walk(Conf *self);
