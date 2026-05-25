/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Library for managing configuration trees in plumCore
 *
 * Copyright (c) 2024, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <u_log.h>
#include <stdio.h>

#include "configlib.h"

#define MODULE_NAME "configlib"

/** @todo read/write helpers with a string path */


static const char *configlib_type_str[] = {"none", "subtree", "float", "u32", "s32", "u16", "s16", "u8", "s8", "bool", "bstr", "str", "enum"};

static conf_ret_t configlib_write(Conf *self, const union conf_val val) {
	ConfiglibValue *value = self->parent;

	switch (value->type) {
		case CONF_F:    *(float *)value->var    = val.f;   break;
		case CONF_U32:  *(uint32_t *)value->var = val.u32; break;
		case CONF_S32:  *(int32_t *)value->var  = val.i32; break;
		case CONF_U16:  *(uint16_t *)value->var = val.u16; break;
		case CONF_S16:  *(int16_t *)value->var  = val.i16; break;
		case CONF_U8:   *(uint8_t *)value->var  = val.u8;  break;
		case CONF_S8:   *(int8_t *)value->var   = val.i8;  break;
		case CONF_B:    *(bool *)value->var      = val.b;  break;
		case CONF_ENUM: *(uint32_t *)value->var  = val.u32; break;
		case CONF_STR: {
			size_t copy_len = val.str.len < value->size - 1 ? val.str.len : value->size - 1;
			memcpy(value->var, val.str.buf, copy_len);
			((char *)value->var)[copy_len] = '\0';
			break;
		}
		case CONF_BSTR: {
			size_t copy_len = val.bstr.len < value->size ? val.bstr.len : value->size;
			memcpy(value->var, val.bstr.buf, copy_len);
			if (value->len != NULL) {
				*value->len = copy_len;
			}
			break;
		}
		default:
			return CONF_RET_FAILED;
	}

	return CONF_RET_OK;
}


static conf_ret_t configlib_read(Conf *self, union conf_val *val) {
	ConfiglibValue *value = self->parent;

	switch (value->type) {
		case CONF_F:    val->f   = *(float *)value->var;    break;
		case CONF_U32:  val->u32 = *(uint32_t *)value->var; break;
		case CONF_S32:  val->i32 = *(int32_t *)value->var;  break;
		case CONF_U16:  val->u16 = *(uint16_t *)value->var; break;
		case CONF_S16:  val->i16 = *(int16_t *)value->var;  break;
		case CONF_U8:   val->u8  = *(uint8_t *)value->var;  break;
		case CONF_S8:   val->i8  = *(int8_t *)value->var;   break;
		case CONF_B:    val->b   = *(bool *)value->var;     break;
		case CONF_ENUM: val->u32 = *(uint32_t *)value->var; break;
		case CONF_STR:
			val->str.buf = (char *)value->var;
			val->str.len = strlen((char *)value->var);
			break;
		case CONF_BSTR:
			val->bstr.buf = (uint8_t *)value->var;
			val->bstr.len = (value->len != NULL) ? *value->len : 0;
			break;
		default:
			return CONF_RET_FAILED;
	}

	return CONF_RET_OK;
}


static conf_ret_t configlib_walk(Conf *self, enum conf_dir direction, Conf **next) {
	ConfiglibValue *value = self->parent;

	if (direction == CONF_DIR_CHILD && value->child != NULL) {
		*next = &value->child->conf;
		return CONF_RET_OK;
	}

	if (direction == CONF_DIR_NEXT && value->next != NULL) {
		*next = &value->next->conf;
		return CONF_RET_OK;
	}

	if (direction == CONF_DIR_UP && value->parent != NULL) {
		*next = &value->parent->conf;
		return CONF_RET_OK;
	}

	/* Other directions not supported or the required pointer is missing. */
	return CONF_RET_FAILED;
}


static conf_ret_t configlib_get_default(Conf *self, union conf_val *val) {
	ConfiglibValue *value = self->parent;

	if (!value->has_default) {
		return CONF_RET_FAILED;
	}

	*val = value->default_val;
	return CONF_RET_OK;
}


static conf_ret_t configlib_get_description(Conf *self, const char **brief, const char **detail) {
	ConfiglibValue *value = self->parent;

	if (value->brief == NULL && value->detail == NULL) {
		return CONF_RET_FAILED;
	}

	*brief = value->brief;
	*detail = value->detail;
	return CONF_RET_OK;
}


static conf_ret_t configlib_get_constraints(Conf *self, union conf_constraint *c) {
	ConfiglibValue *value = self->parent;

	if (!value->has_constraints) {
		return CONF_RET_FAILED;
	}

	*c = value->constraint;
	return CONF_RET_OK;
}


static conf_ret_t configlib_stat(Conf *self, const char **name, enum conf_type *type, enum conf_flag *flags) {
	ConfiglibValue *value = self->parent;

	*name = value->name;
	*type = value->type;
	*flags = value->flags;

	return CONF_RET_OK;
}


static const struct conf_vmt configlib_conf_vmt = {
	/* Configure the tree */
	.write = configlib_write,
	.read = configlib_read,

	/* Search/traverse the tree */
	.walk = configlib_walk,
	.stat = configlib_stat,
	.get_default = configlib_get_default,
	.get_description = configlib_get_description,
	.get_constraints = configlib_get_constraints,

	/* Manipulate the tree */
	.create = NULL,
	.destroy = NULL,
};


configlib_ret_t configlib_init(ConfiglibValue *self, const char *name) {
	memset(self, 0, sizeof(ConfiglibValue));
	self->conf.vmt = &configlib_conf_vmt;
	self->conf.parent = self;
	self->type = CONF_NONE;
	self->name = name;

	return CONFIGLIB_RET_OK;
}


configlib_ret_t configlib_map(ConfiglibValue *self, void *var, enum conf_type type) {
	self->var = var;
	self->type = type;

	return CONFIGLIB_RET_OK;
}


configlib_ret_t configlib_map_string(ConfiglibValue *self, char *str, size_t size) {
	self->var = str;
	self->size = size;
	self->type = CONF_STR;

	return CONFIGLIB_RET_OK;
}


configlib_ret_t configlib_append(ConfiglibValue *self, ConfiglibValue *parent, enum conf_dir dir) {
	if (dir == CONF_DIR_CHILD) {
		if (parent->child == NULL) {
			/* If the parent's child is NULL, we add the current value as a child. */
			parent->child = self;
			self->parent = parent;
		} else {
			/* Otherwise we add it as a sibling of the current child. */
			self->next = parent->child;
			self->parent = parent->parent;
			parent->child = self;
		}
	} else if (dir == CONF_DIR_NEXT) {
		self->next = parent->next;
		self->parent = parent->parent;
		parent->next = self;

	} else {
		return CONFIGLIB_RET_FAILED;
	}

	return CONFIGLIB_RET_OK;
}


configlib_ret_t configlib_set_default(ConfiglibValue *self, union conf_val val) {
	self->default_val = val;
	self->has_default = true;
	return CONFIGLIB_RET_OK;
}


configlib_ret_t configlib_set_description(ConfiglibValue *self, const char *brief, const char *detail) {
	self->brief = brief;
	self->detail = detail;
	return CONFIGLIB_RET_OK;
}


configlib_ret_t configlib_set_constraint(ConfiglibValue *self, union conf_constraint c) {
	self->constraint = c;
	self->has_constraints = true;
	return CONFIGLIB_RET_OK;
}


configlib_ret_t configlib_init_map(ConfiglibValue *self, const char *name, void *var, enum conf_type type) {
	if (configlib_init(self, name) == CONFIGLIB_RET_OK &&
	    configlib_map(self, var, type) == CONFIGLIB_RET_OK) {
		return CONFIGLIB_RET_OK;
	}

	return CONFIGLIB_RET_FAILED;
}


configlib_ret_t configlib_init_map_append(ConfiglibValue *self, const char *name, void *var, enum conf_type type, ConfiglibValue *parent, enum conf_dir dir) {
	if (configlib_init(self, name) == CONFIGLIB_RET_OK &&
	    configlib_map(self, var, type) == CONFIGLIB_RET_OK &&
	    configlib_append(self, parent, dir)) {
		return CONFIGLIB_RET_OK;
	}

	return CONFIGLIB_RET_FAILED;
}


static void configlib_log_walk_subtree(Conf *self, uint32_t indent) {
	const char indent_str[16] = "               ";
	if (indent >= sizeof(indent_str)) {
		return;
	}

	while (self != NULL) {

		const char *conf_name = NULL;
		enum conf_type conf_type = CONF_SUBTREE;
		enum conf_flag conf_flags;
		self->vmt->stat(self, &conf_name, &conf_type, &conf_flags);

		union conf_val val = {0};
		self->vmt->read(self, &val);
		char val_str[16] = {0};

		if (conf_type == CONF_F) {
			snprintf(val_str, sizeof(val_str), "%f", val.f);
		}

		u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("%s %s(%s) = %s"), &indent_str[sizeof(indent_str) - indent - 1], conf_name, configlib_type_str[conf_type], val_str);

		Conf *next = NULL;
		if (self->vmt->walk(self, CONF_DIR_CHILD, &next) == CONF_RET_OK) {
			configlib_log_walk_subtree(next, indent + 1);
		}

		if (self->vmt->walk(self, CONF_DIR_NEXT, &next) == CONF_RET_OK) {
			self = next;
		} else {
			break;
		}
	}

}


conf_ret_t configlib_log_walk(Conf *self) {
	configlib_log_walk_subtree(self, 0);
	return CONF_RET_OK;
}

