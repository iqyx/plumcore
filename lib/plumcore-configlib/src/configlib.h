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
	const char *name;
	enum conf_type type;


} ConfiglibValue;


configlib_ret_t configlib_init(ConfiglibValue *self, const char *name);
configlib_ret_t configlib_map(ConfiglibValue *self, void *var, enum conf_type type);
configlib_ret_t configlib_append(ConfiglibValue *self, ConfiglibValue *parent, enum conf_dir dir);


configlib_ret_t configlib_init_map(ConfiglibValue *self, const char *name, void *var, enum conf_type type);
configlib_ret_t configlib_init_map_append(ConfiglibValue *self, const char *name, void *var, enum conf_type type, ConfiglibValue *parent, enum conf_dir dir);

conf_ret_t configlib_log_walk(Conf *self);
