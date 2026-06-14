/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Protocol service for remote configuration tree access
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include <main.h>
#include <cbor.h>

#include <interfaces/conf.h>
#include <interfaces/datagram.h>

#include "proto-conf.h"

#define MODULE_NAME "proto-conf"


/***********************************************************************************************************************
 * CBOR request helpers
 **********************************************************************************************************************/

static bool cbor_map_get_str(CborValue *map, const char *key, char *buf, size_t size) {
	CborValue v;
	cbor_value_map_find_value(map, key, &v);
	if (!cbor_value_is_valid(&v) || !cbor_value_is_text_string(&v)) {
		buf[0] = '\0';
		return false;
	}
	size_t len = size;
	cbor_value_copy_text_string(&v, buf, &len, NULL);
	buf[size - 1] = '\0';
	return true;
}


static bool cbor_map_get_uint(CborValue *map, const char *key, uint32_t *out) {
	CborValue v;
	cbor_value_map_find_value(map, key, &v);
	if (!cbor_value_is_valid(&v) || !cbor_value_is_unsigned_integer(&v)) {
		return false;
	}
	uint64_t u = 0;
	cbor_value_get_uint64(&v, &u);
	*out = (uint32_t)u;
	return true;
}


/* Extract the "p" path array from a request map. Empty/absent path is valid (means root). */
static bool cbor_map_get_path(CborValue *map, char path[][CONFIG_SERVICE_PROTO_CONF_MAX_PATH_COMP_LEN], size_t *depth) {
	*depth = 0;
	CborValue arr;
	cbor_value_map_find_value(map, "p", &arr);
	if (!cbor_value_is_valid(&arr) || !cbor_value_is_array(&arr)) {
		return true;
	}

	CborValue elem;
	cbor_value_enter_container(&arr, &elem);

	while (!cbor_value_at_end(&elem) && *depth < CONFIG_SERVICE_PROTO_CONF_MAX_PATH_DEPTH) {
		if (!cbor_value_is_text_string(&elem)) {
			return false;
		}
		size_t len = CONFIG_SERVICE_PROTO_CONF_MAX_PATH_COMP_LEN;
		CborValue next_elem;
		cbor_value_copy_text_string(&elem, path[*depth], &len, &next_elem);
		path[*depth][CONFIG_SERVICE_PROTO_CONF_MAX_PATH_COMP_LEN - 1] = '\0';
		(*depth)++;
		elem = next_elem;
	}

	return true;
}


/***********************************************************************************************************************
 * Conf tree helpers
 **********************************************************************************************************************/

/* Walk from root to the node identified by path. An empty path yields root itself. */
static conf_ret_t resolve_path(Conf *root, char path[][CONFIG_SERVICE_PROTO_CONF_MAX_PATH_COMP_LEN], size_t depth, Conf **result) {
	Conf *cur = root;

	for (size_t i = 0; i < depth; i++) {
		Conf *child = NULL;
		if (cur->vmt->walk(cur, CONF_DIR_CHILD, &child) != CONF_RET_OK || child == NULL) {
			return CONF_RET_FAILED;
		}

		bool found = false;
		while (child != NULL) {
			const char *name = NULL;
			enum conf_type type;
			enum conf_flag flags;
			if (child->vmt->stat(child, &name, &type, &flags) == CONF_RET_OK &&
			    name != NULL && !strcmp(name, path[i])) {
				cur = child;
				found = true;
				break;
			}
			Conf *next = NULL;
			if (child->vmt->walk(child, CONF_DIR_NEXT, &next) != CONF_RET_OK) {
				break;
			}
			child = next;
		}

		if (!found) {
			return CONF_RET_FAILED;
		}
	}

	*result = cur;
	return CONF_RET_OK;
}


/***********************************************************************************************************************
 * Typed value encoding / decoding
 **********************************************************************************************************************/

static bool encode_conf_val(CborEncoder *enc, enum conf_type type, const union conf_val *val) {
	switch (type) {
		case CONF_F:    return cbor_encode_float(enc, val->f) == CborNoError;
		case CONF_U32:  return cbor_encode_uint(enc, val->u32) == CborNoError;
		case CONF_S32:  return cbor_encode_int(enc, val->i32) == CborNoError;
		case CONF_U16:  return cbor_encode_uint(enc, val->u16) == CborNoError;
		case CONF_S16:  return cbor_encode_int(enc, val->i16) == CborNoError;
		case CONF_U8:   return cbor_encode_uint(enc, val->u8) == CborNoError;
		case CONF_S8:   return cbor_encode_int(enc, val->i8) == CborNoError;
		case CONF_B:    return cbor_encode_boolean(enc, val->b) == CborNoError;
		case CONF_BSTR: return cbor_encode_byte_string(enc, val->bstr.buf, val->bstr.len) == CborNoError;
		case CONF_STR:  return cbor_encode_text_string(enc, val->str.buf, val->str.len) == CborNoError;
		case CONF_ENUM: return cbor_encode_uint(enc, val->u32) == CborNoError;
		default:        return false;
	}
}


/* Decode the "val" key from a request map into a typed conf_val.
 * buf/buf_len is scratch storage used for BSTR and STR types. */
static bool decode_conf_val_from_map(CborValue *map, enum conf_type type, union conf_val *val, uint8_t *buf, size_t buf_len) {
	CborValue v;
	cbor_value_map_find_value(map, "val", &v);
	if (!cbor_value_is_valid(&v)) {
		return false;
	}

	switch (type) {
		case CONF_F: {
			if (cbor_value_is_float(&v)) {
				cbor_value_get_float(&v, &val->f);
			} else if (cbor_value_is_double(&v)) {
				double d = 0.0;
				cbor_value_get_double(&v, &d);
				val->f = (float)d;
			} else {
				return false;
			}
			return true;
		}
		case CONF_U32:
		case CONF_ENUM: {
			if (!cbor_value_is_unsigned_integer(&v)) {
				return false;
			}
			uint64_t u = 0;
			cbor_value_get_uint64(&v, &u);
			val->u32 = (uint32_t)u;
			return true;
		}
		case CONF_U16: {
			if (!cbor_value_is_unsigned_integer(&v)) {
				return false;
			}
			uint64_t u = 0;
			cbor_value_get_uint64(&v, &u);
			val->u16 = (uint16_t)u;
			return true;
		}
		case CONF_U8: {
			if (!cbor_value_is_unsigned_integer(&v)) {
				return false;
			}
			uint64_t u = 0;
			cbor_value_get_uint64(&v, &u);
			val->u8 = (uint8_t)u;
			return true;
		}
		case CONF_S32: {
			if (!cbor_value_is_integer(&v)) {
				return false;
			}
			int64_t i = 0;
			cbor_value_get_int64(&v, &i);
			val->i32 = (int32_t)i;
			return true;
		}
		case CONF_S16: {
			if (!cbor_value_is_integer(&v)) {
				return false;
			}
			int64_t i = 0;
			cbor_value_get_int64(&v, &i);
			val->i16 = (int16_t)i;
			return true;
		}
		case CONF_S8: {
			if (!cbor_value_is_integer(&v)) {
				return false;
			}
			int64_t i = 0;
			cbor_value_get_int64(&v, &i);
			val->i8 = (int8_t)i;
			return true;
		}
		case CONF_B: {
			if (!cbor_value_is_boolean(&v)) {
				return false;
			}
			cbor_value_get_boolean(&v, &val->b);
			return true;
		}
		case CONF_BSTR: {
			size_t len = buf_len;
			if (!cbor_value_is_byte_string(&v)) {
				return false;
			}
			cbor_value_copy_byte_string(&v, buf, &len, NULL);
			val->bstr.buf = buf;
			val->bstr.len = len;
			return true;
		}
		case CONF_STR: {
			size_t len = buf_len;
			if (!cbor_value_is_text_string(&v)) {
				return false;
			}
			cbor_value_copy_text_string(&v, (char *)buf, &len, NULL);
			val->str.buf = (char *)buf;
			val->str.len = len;
			return true;
		}
		default:
			return false;
	}
}


/* Encode node identity fields {n, t, f} into an open response map. */
static void encode_stat_fields(CborEncoder *omap, const char *name, enum conf_type type, enum conf_flag flags) {
	cbor_encode_text_stringz(omap, "n");
	cbor_encode_text_stringz(omap, name != NULL ? name : "");
	cbor_encode_text_stringz(omap, "t");
	cbor_encode_uint(omap, (uint32_t)type);
	cbor_encode_text_stringz(omap, "f");
	cbor_encode_uint(omap, (uint32_t)flags);
}


/***********************************************************************************************************************
 * Command handlers
 **********************************************************************************************************************/

static proto_conf_ret_t process_cc_stat(ProtoConf *self, CborValue *imap, CborEncoder *omap) {
	char path[CONFIG_SERVICE_PROTO_CONF_MAX_PATH_DEPTH][CONFIG_SERVICE_PROTO_CONF_MAX_PATH_COMP_LEN];
	size_t depth = 0;
	if (!cbor_map_get_path(imap, path, &depth)) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "bad path");
		return PROTO_CONF_RET_FAILED;
	}

	Conf *node = NULL;
	if (resolve_path(self->root, path, depth, &node) != CONF_RET_OK) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "path not found");
		return PROTO_CONF_RET_FAILED;
	}

	const char *name = NULL;
	enum conf_type type = CONF_NONE;
	enum conf_flag flags = 0;
	if (node->vmt->stat == NULL || node->vmt->stat(node, &name, &type, &flags) != CONF_RET_OK) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "stat failed");
		return PROTO_CONF_RET_FAILED;
	}

	encode_stat_fields(omap, name, type, flags);
	return PROTO_CONF_RET_OK;
}


static proto_conf_ret_t process_cc_read(ProtoConf *self, CborValue *imap, CborEncoder *omap) {
	char path[CONFIG_SERVICE_PROTO_CONF_MAX_PATH_DEPTH][CONFIG_SERVICE_PROTO_CONF_MAX_PATH_COMP_LEN];
	size_t depth = 0;
	if (!cbor_map_get_path(imap, path, &depth)) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "bad path");
		return PROTO_CONF_RET_FAILED;
	}

	Conf *node = NULL;
	if (resolve_path(self->root, path, depth, &node) != CONF_RET_OK) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "path not found");
		return PROTO_CONF_RET_FAILED;
	}

	const char *name = NULL;
	enum conf_type type = CONF_NONE;
	enum conf_flag flags = 0;
	if (node->vmt->stat == NULL || node->vmt->stat(node, &name, &type, &flags) != CONF_RET_OK) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "stat failed");
		return PROTO_CONF_RET_FAILED;
	}

	union conf_val val = {0};
	if (node->vmt->read == NULL || node->vmt->read(node, &val) != CONF_RET_OK) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "read failed");
		return PROTO_CONF_RET_FAILED;
	}

	cbor_encode_text_stringz(omap, "t");
	cbor_encode_uint(omap, (uint32_t)type);
	cbor_encode_text_stringz(omap, "val");
	encode_conf_val(omap, type, &val);

	return PROTO_CONF_RET_OK;
}


/* write request: p, t (conf_type), val */
static proto_conf_ret_t process_cc_write(ProtoConf *self, CborValue *imap, CborEncoder *omap) {
	char path[CONFIG_SERVICE_PROTO_CONF_MAX_PATH_DEPTH][CONFIG_SERVICE_PROTO_CONF_MAX_PATH_COMP_LEN];
	size_t depth = 0;
	if (!cbor_map_get_path(imap, path, &depth)) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "bad path");
		return PROTO_CONF_RET_FAILED;
	}

	uint32_t type_u = 0;
	if (!cbor_map_get_uint(imap, "t", &type_u)) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "missing type");
		return PROTO_CONF_RET_FAILED;
	}
	enum conf_type type = (enum conf_type)type_u;

	/* Decode value before resolving path to keep str_buf on the stack during the write call. */
	uint8_t str_buf[CONFIG_SERVICE_PROTO_CONF_MAX_STR_LEN];
	union conf_val val = {0};
	if (!decode_conf_val_from_map(imap, type, &val, str_buf, sizeof(str_buf))) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "bad value");
		return PROTO_CONF_RET_FAILED;
	}

	Conf *node = NULL;
	if (resolve_path(self->root, path, depth, &node) != CONF_RET_OK) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "path not found");
		return PROTO_CONF_RET_FAILED;
	}

	if (node->vmt->write == NULL || node->vmt->write(node, val) != CONF_RET_OK) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "write failed");
		return PROTO_CONF_RET_FAILED;
	}

	cbor_encode_text_stringz(omap, "ret");
	cbor_encode_text_stringz(omap, "ok");
	return PROTO_CONF_RET_OK;
}


/* walk request: p, dir (conf_dir) → stat fields of the resulting node */
/* walk: step from the given node in direction "dir", returning a "nodes" array.
 * For CONF_DIR_NEXT and CONF_DIR_PREV, up to min("limit", MAX_SIBLINGS) consecutive
 * nodes are collected. For CONF_DIR_CHILD and CONF_DIR_UP, at most one node is returned. */
static proto_conf_ret_t process_cc_walk(ProtoConf *self, CborValue *imap, CborEncoder *omap) {
	char path[CONFIG_SERVICE_PROTO_CONF_MAX_PATH_DEPTH][CONFIG_SERVICE_PROTO_CONF_MAX_PATH_COMP_LEN];
	size_t depth = 0;
	if (!cbor_map_get_path(imap, path, &depth)) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "bad path");
		return PROTO_CONF_RET_FAILED;
	}

	uint32_t dir_u = 0;
	if (!cbor_map_get_uint(imap, "dir", &dir_u)) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "missing direction");
		return PROTO_CONF_RET_FAILED;
	}
	enum conf_dir dir = (enum conf_dir)dir_u;

	uint32_t limit = CONFIG_SERVICE_PROTO_CONF_MAX_SIBLINGS;
	uint32_t req_limit = 0;
	if (cbor_map_get_uint(imap, "limit", &req_limit) && req_limit < limit) {
		limit = req_limit;
	}

	Conf *node = NULL;
	if (resolve_path(self->root, path, depth, &node) != CONF_RET_OK) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "path not found");
		return PROTO_CONF_RET_FAILED;
	}

	cbor_encode_text_stringz(omap, "nodes");
	CborEncoder arr;
	cbor_encoder_create_array(omap, &arr, CborIndefiniteLength);

	if (dir == CONF_DIR_NEXT || dir == CONF_DIR_PREV) {
		Conf *cur = node;
		size_t count = 0;
		while (count < limit) {
			Conf *next = NULL;
			if (cur->vmt->walk == NULL || cur->vmt->walk(cur, dir, &next) != CONF_RET_OK || next == NULL) {
				break;
			}
			const char *name = NULL;
			enum conf_type type = CONF_NONE;
			enum conf_flag flags = 0;
			if (next->vmt->stat != NULL && next->vmt->stat(next, &name, &type, &flags) == CONF_RET_OK) {
				CborEncoder entry;
				cbor_encoder_create_map(&arr, &entry, CborIndefiniteLength);
				encode_stat_fields(&entry, name, type, flags);
				cbor_encoder_close_container(&arr, &entry);
				count++;
			}
			cur = next;
		}
	} else {
		/* CONF_DIR_CHILD or CONF_DIR_UP: single step. */
		Conf *next = NULL;
		if (node->vmt->walk != NULL && node->vmt->walk(node, dir, &next) == CONF_RET_OK && next != NULL) {
			const char *name = NULL;
			enum conf_type type = CONF_NONE;
			enum conf_flag flags = 0;
			if (next->vmt->stat != NULL && next->vmt->stat(next, &name, &type, &flags) == CONF_RET_OK) {
				CborEncoder entry;
				cbor_encoder_create_map(&arr, &entry, CborIndefiniteLength);
				encode_stat_fields(&entry, name, type, flags);
				cbor_encoder_close_container(&arr, &entry);
			}
		}
	}

	cbor_encoder_close_container(omap, &arr);
	return PROTO_CONF_RET_OK;
}


/* create request: p (parent path), name → stat fields of the new node */
static proto_conf_ret_t process_cc_create(ProtoConf *self, CborValue *imap, CborEncoder *omap) {
	char path[CONFIG_SERVICE_PROTO_CONF_MAX_PATH_DEPTH][CONFIG_SERVICE_PROTO_CONF_MAX_PATH_COMP_LEN];
	size_t depth = 0;
	if (!cbor_map_get_path(imap, path, &depth)) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "bad path");
		return PROTO_CONF_RET_FAILED;
	}

	char name[CONFIG_SERVICE_PROTO_CONF_MAX_PATH_COMP_LEN];
	if (!cbor_map_get_str(imap, "name", name, sizeof(name)) || name[0] == '\0') {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "missing name");
		return PROTO_CONF_RET_FAILED;
	}

	Conf *node = NULL;
	if (resolve_path(self->root, path, depth, &node) != CONF_RET_OK) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "path not found");
		return PROTO_CONF_RET_FAILED;
	}

	Conf *created = NULL;
	if (node->vmt->create == NULL || node->vmt->create(node, name, &created) != CONF_RET_OK || created == NULL) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "create failed");
		return PROTO_CONF_RET_FAILED;
	}

	const char *created_name = NULL;
	enum conf_type type = CONF_NONE;
	enum conf_flag flags = 0;
	if (created->vmt->stat == NULL || created->vmt->stat(created, &created_name, &type, &flags) != CONF_RET_OK) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "stat failed");
		return PROTO_CONF_RET_FAILED;
	}

	encode_stat_fields(omap, created_name, type, flags);
	return PROTO_CONF_RET_OK;
}


static proto_conf_ret_t process_cc_destroy(ProtoConf *self, CborValue *imap, CborEncoder *omap) {
	char path[CONFIG_SERVICE_PROTO_CONF_MAX_PATH_DEPTH][CONFIG_SERVICE_PROTO_CONF_MAX_PATH_COMP_LEN];
	size_t depth = 0;
	if (!cbor_map_get_path(imap, path, &depth)) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "bad path");
		return PROTO_CONF_RET_FAILED;
	}

	if (depth == 0) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "cannot destroy root");
		return PROTO_CONF_RET_FAILED;
	}

	Conf *node = NULL;
	if (resolve_path(self->root, path, depth, &node) != CONF_RET_OK) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "path not found");
		return PROTO_CONF_RET_FAILED;
	}

	if (node->vmt->destroy == NULL || node->vmt->destroy(node) != CONF_RET_OK) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "destroy failed");
		return PROTO_CONF_RET_FAILED;
	}

	cbor_encode_text_stringz(omap, "ret");
	cbor_encode_text_stringz(omap, "ok");
	return PROTO_CONF_RET_OK;
}


static proto_conf_ret_t process_cc_get_default(ProtoConf *self, CborValue *imap, CborEncoder *omap) {
	char path[CONFIG_SERVICE_PROTO_CONF_MAX_PATH_DEPTH][CONFIG_SERVICE_PROTO_CONF_MAX_PATH_COMP_LEN];
	size_t depth = 0;
	if (!cbor_map_get_path(imap, path, &depth)) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "bad path");
		return PROTO_CONF_RET_FAILED;
	}

	Conf *node = NULL;
	if (resolve_path(self->root, path, depth, &node) != CONF_RET_OK) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "path not found");
		return PROTO_CONF_RET_FAILED;
	}

	const char *name = NULL;
	enum conf_type type = CONF_NONE;
	enum conf_flag flags = 0;
	if (node->vmt->stat == NULL || node->vmt->stat(node, &name, &type, &flags) != CONF_RET_OK) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "stat failed");
		return PROTO_CONF_RET_FAILED;
	}

	union conf_val val = {0};
	if (node->vmt->get_default == NULL || node->vmt->get_default(node, &val) != CONF_RET_OK) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "no default");
		return PROTO_CONF_RET_FAILED;
	}

	cbor_encode_text_stringz(omap, "t");
	cbor_encode_uint(omap, (uint32_t)type);
	cbor_encode_text_stringz(omap, "val");
	encode_conf_val(omap, type, &val);

	return PROTO_CONF_RET_OK;
}


static proto_conf_ret_t process_cc_get_desc(ProtoConf *self, CborValue *imap, CborEncoder *omap) {
	char path[CONFIG_SERVICE_PROTO_CONF_MAX_PATH_DEPTH][CONFIG_SERVICE_PROTO_CONF_MAX_PATH_COMP_LEN];
	size_t depth = 0;
	if (!cbor_map_get_path(imap, path, &depth)) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "bad path");
		return PROTO_CONF_RET_FAILED;
	}

	Conf *node = NULL;
	if (resolve_path(self->root, path, depth, &node) != CONF_RET_OK) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "path not found");
		return PROTO_CONF_RET_FAILED;
	}

	const char *brief = NULL;
	const char *detail = NULL;
	if (node->vmt->get_description == NULL || node->vmt->get_description(node, &brief, &detail) != CONF_RET_OK) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "no description");
		return PROTO_CONF_RET_FAILED;
	}

	if (brief != NULL) {
		cbor_encode_text_stringz(omap, "brief");
		cbor_encode_text_stringz(omap, brief);
	}
	if (detail != NULL) {
		cbor_encode_text_stringz(omap, "detail");
		cbor_encode_text_stringz(omap, detail);
	}

	return PROTO_CONF_RET_OK;
}


static proto_conf_ret_t process_cc_get_constraints(ProtoConf *self, CborValue *imap, CborEncoder *omap) {
	char path[CONFIG_SERVICE_PROTO_CONF_MAX_PATH_DEPTH][CONFIG_SERVICE_PROTO_CONF_MAX_PATH_COMP_LEN];
	size_t depth = 0;
	if (!cbor_map_get_path(imap, path, &depth)) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "bad path");
		return PROTO_CONF_RET_FAILED;
	}

	Conf *node = NULL;
	if (resolve_path(self->root, path, depth, &node) != CONF_RET_OK) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "path not found");
		return PROTO_CONF_RET_FAILED;
	}

	const char *name = NULL;
	enum conf_type type = CONF_NONE;
	enum conf_flag flags = 0;
	if (node->vmt->stat == NULL || node->vmt->stat(node, &name, &type, &flags) != CONF_RET_OK) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "stat failed");
		return PROTO_CONF_RET_FAILED;
	}

	union conf_constraint c = {0};
	if (node->vmt->get_constraints == NULL || node->vmt->get_constraints(node, &c) != CONF_RET_OK) {
		cbor_encode_text_stringz(omap, "err");
		cbor_encode_text_stringz(omap, "no constraints");
		return PROTO_CONF_RET_FAILED;
	}

	switch (type) {
		case CONF_F:
		case CONF_U32:
		case CONF_S32:
		case CONF_U16:
		case CONF_S16:
		case CONF_U8:
		case CONF_S8: {
			cbor_encode_text_stringz(omap, "min");
			encode_conf_val(omap, type, &c.numeric.min);
			cbor_encode_text_stringz(omap, "max");
			encode_conf_val(omap, type, &c.numeric.max);
			break;
		}
		case CONF_STR: {
			cbor_encode_text_stringz(omap, "max_len");
			cbor_encode_uint(omap, (uint64_t)c.str.max_len);
			if (c.str.regexp != NULL) {
				cbor_encode_text_stringz(omap, "regexp");
				cbor_encode_text_stringz(omap, c.str.regexp);
			}
			break;
		}
		case CONF_BSTR: {
			cbor_encode_text_stringz(omap, "max_len");
			cbor_encode_uint(omap, (uint64_t)c.bstr.max_len);
			break;
		}
		case CONF_ENUM: {
			cbor_encode_text_stringz(omap, "count");
			cbor_encode_uint(omap, (uint64_t)c.enumeration.count);
			if (c.enumeration.values != NULL) {
				cbor_encode_text_stringz(omap, "values");
				CborEncoder arr;
				cbor_encoder_create_array(omap, &arr, c.enumeration.count);
				for (size_t i = 0; i < c.enumeration.count; i++) {
					cbor_encode_text_stringz(&arr, c.enumeration.values[i]);
				}
				cbor_encoder_close_container(omap, &arr);
			}
			break;
		}
		default:
			break;
	}

	return PROTO_CONF_RET_OK;
}


/***********************************************************************************************************************
 * Request dispatcher and task
 **********************************************************************************************************************/

static proto_conf_ret_t process_request(ProtoConf *self, uint8_t *buf, size_t len) {
	CborParser parser;
	CborValue map;

	cbor_parser_init(buf, len, 0, &parser, &map);
	if (!cbor_value_is_map(&map)) {
		return PROTO_CONF_RET_FAILED;
	}

	CborValue cmd_v;
	cbor_value_map_find_value(&map, "c", &cmd_v);
	if (!cbor_value_is_valid(&cmd_v) || !cbor_value_is_text_string(&cmd_v)) {
		return PROTO_CONF_RET_FAILED;
	}

	char cmd[16];
	size_t cmd_len = sizeof(cmd);
	cbor_value_copy_text_string(&cmd_v, cmd, &cmd_len, NULL);

	CborEncoder encoder;
	cbor_encoder_init(&encoder, self->tx_buf, CONFIG_SERVICE_PROTO_CONF_MAX_DATAGRAM_LEN, 0);
	CborEncoder omap;
	cbor_encoder_create_map(&encoder, &omap, CborIndefiniteLength);

	proto_conf_ret_t ret = PROTO_CONF_RET_FAILED;

	if (!strcmp(cmd, "stat")) {
		ret = process_cc_stat(self, &map, &omap);
	} else if (!strcmp(cmd, "read")) {
		ret = process_cc_read(self, &map, &omap);
	} else if (!strcmp(cmd, "write")) {
		ret = process_cc_write(self, &map, &omap);
	} else if (!strcmp(cmd, "walk")) {
		ret = process_cc_walk(self, &map, &omap);
	} else if (!strcmp(cmd, "create")) {
		ret = process_cc_create(self, &map, &omap);
	} else if (!strcmp(cmd, "destroy")) {
		ret = process_cc_destroy(self, &map, &omap);
	} else if (!strcmp(cmd, "get_default")) {
		ret = process_cc_get_default(self, &map, &omap);
	} else if (!strcmp(cmd, "get_desc")) {
		ret = process_cc_get_desc(self, &map, &omap);
	} else if (!strcmp(cmd, "get_constraints")) {
		ret = process_cc_get_constraints(self, &map, &omap);
	} else {
		cbor_encode_text_stringz(&omap, "err");
		cbor_encode_text_stringz(&omap, "unknown command");
	}

	cbor_encoder_close_container(&encoder, &omap);
	size_t tx_len = cbor_encoder_get_buffer_size(&encoder, self->tx_buf);

	struct datagram_msg txmsg = {0};
	txmsg.addr_size = 4;
	txmsg.dst_port = self->src_port;
	memcpy(&txmsg.dst_addr, &self->src_addr, 4);
	self->d->vmt->write(self->d, self->tx_buf, tx_len, &txmsg);

	return ret;
}


static void proto_conf_task(void *p) {
	ProtoConf *self = p;

	while (true) {
		size_t len = CONFIG_SERVICE_PROTO_CONF_MAX_DATAGRAM_LEN;
		struct datagram_msg rxmsg = {0};
		if (self->d->vmt->read(self->d, self->rx_buf, &len, &rxmsg) == DATAGRAM_RET_OK) {
			self->src_port = rxmsg.src_port;
			memcpy(&self->src_addr, &rxmsg.src_addr, 4);
			vTaskDelay(1);
			process_request(self, self->rx_buf, len);
		}
	}

	vTaskDelete(NULL);
}


/***********************************************************************************************************************
 * Public API
 **********************************************************************************************************************/

proto_conf_ret_t proto_conf_init(ProtoConf *self, Datagram *d, Conf *root) {
	memset(self, 0, sizeof(ProtoConf));

	self->d = d;
	self->root = root;

	xTaskCreate(proto_conf_task, "proto-conf", configMINIMAL_STACK_SIZE + 512, (void *)self, 1, &(self->task));
	if (self->task == NULL) {
		return PROTO_CONF_RET_FAILED;
	}

	return PROTO_CONF_RET_OK;
}


proto_conf_ret_t proto_conf_free(ProtoConf *self) {
	(void)self;
	/** @todo stop the task */
	return PROTO_CONF_RET_OK;
}
