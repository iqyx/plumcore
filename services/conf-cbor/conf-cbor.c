/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Load/save the discovered configuration tree as CBOR
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 *
 * The configuration document is a CBOR map of node names to values (leaves) or nested maps (subtrees).
 * Advertised Conf subtrees are mounted at the position given by their service locator name split on single
 * spaces, the same way proto-conf presents them. Nodes above and between the mount points are virtual,
 * a mount point shadows a real node at the same position. Conf subtrees advertised as "system",
 * "system bootloader" and "mib" are therefore saved as:
 *
 *     {"system": {"hostname": "abc", "bootloader": {"timeout": 5}}, "mib": {...}}
 *
 * Leaf values are encoded using the natural CBOR type of the node: unsigned/negative integers for integer
 * and enum types, float, bool, text string and byte string. All maps are indefinite-length as the number
 * of entries is not known in advance.
 *
 * Neither saving nor loading buffers the document in RAM. The encoder writes directly through the
 * write handler, the parser reads through the read handler. Node names and string values are compared
 * and read directly from the medium without copying them to a bounded buffer.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <main.h>
#include <cbor.h>

#include <interfaces/flash.h>
#include <interfaces/conf.h>
#include <interfaces/job.h>
#include <interfaces/servicelocator.h>

#include "conf-cbor.h"

#define MODULE_NAME "conf-cbor"

#define CONF_CBOR_FLASH_CHUNK 32
#define CONF_CBOR_CMP_CHUNK 16
#define CONF_CBOR_JOB_STACK_SIZE (configMINIMAL_STACK_SIZE + 512)


/**
 * Storage access handlers. Exactly one open/close pair is active at a time. Writes are always sequential,
 * starting at position 0. When writing, close is called with @p commit set if the whole document was written
 * successfully, the new document should become valid only on commit.
 */
struct conf_cbor_io {
	conf_cbor_ret_t (*open)(ConfCbor *self, bool write, size_t *size);
	conf_cbor_ret_t (*read)(ConfCbor *self, size_t pos, void *buf, size_t len);
	conf_cbor_ret_t (*write)(ConfCbor *self, size_t pos, const void *buf, size_t len);
	conf_cbor_ret_t (*close)(ConfCbor *self, bool commit);
};


/**
 * Position within the document being read or written. Used as the tinycbor reader/writer token. The parser
 * keeps a single shared cursor, the tree must therefore be traversed strictly linearly.
 */
struct conf_cbor_cursor {
	ConfCbor *self;
	size_t pos;
	size_t len;

	/* Load statistics. */
	uint32_t loaded;
	uint32_t skipped;
};


/*********************************************************************************************************************
 * Flash handlers
 *********************************************************************************************************************/

static conf_cbor_ret_t conf_cbor_flash_open(ConfCbor *self, bool write, size_t *size) {
	size_t part_size = 0;
	flash_block_ops_t ops;
	if (self->conf.flash->vmt->get_size(self->conf.flash, 0, &part_size, &ops) != FLASH_RET_OK ||
	    self->conf.offset >= part_size) {
		return CONF_CBOR_RET_IO;
	}
	*size = part_size - self->conf.offset;
	if (self->conf.max_size != 0 && self->conf.max_size < *size) {
		*size = self->conf.max_size;
	}

	if (!write) {
		return CONF_CBOR_RET_OK;
	}

	/* First half holds the held-back document head, the second half is the current staging chunk. */
	self->wbuf = malloc(2 * CONF_CBOR_FLASH_CHUNK);
	if (self->wbuf == NULL) {
		return CONF_CBOR_RET_NOMEM;
	}
	self->wbuf_pos = 0;

	if (self->conf.flash->vmt->erase(self->conf.flash, self->conf.offset, *size) != FLASH_RET_OK) {
		free(self->wbuf);
		self->wbuf = NULL;
		return CONF_CBOR_RET_IO;
	}
	return CONF_CBOR_RET_OK;
}


static conf_cbor_ret_t conf_cbor_flash_read(ConfCbor *self, size_t pos, void *buf, size_t len) {
	if (self->conf.flash->vmt->read(self->conf.flash, self->conf.offset + pos, buf, len) != FLASH_RET_OK) {
		return CONF_CBOR_RET_IO;
	}
	return CONF_CBOR_RET_OK;
}


/* Write the full staging chunk at @p chunk_pos. The very first chunk is only copied aside, see close. */
static conf_cbor_ret_t conf_cbor_flash_flush(ConfCbor *self, size_t chunk_pos) {
	if (chunk_pos == 0) {
		memcpy(self->wbuf, self->wbuf + CONF_CBOR_FLASH_CHUNK, CONF_CBOR_FLASH_CHUNK);
		return CONF_CBOR_RET_OK;
	}
	if (self->conf.flash->vmt->write(self->conf.flash, self->conf.offset + chunk_pos,
	                                 self->wbuf + CONF_CBOR_FLASH_CHUNK, CONF_CBOR_FLASH_CHUNK) != FLASH_RET_OK) {
		return CONF_CBOR_RET_IO;
	}
	return CONF_CBOR_RET_OK;
}


static conf_cbor_ret_t conf_cbor_flash_write(ConfCbor *self, size_t pos, const void *buf, size_t len) {
	(void)pos;
	const uint8_t *b = buf;
	while (len > 0) {
		size_t used = self->wbuf_pos % CONF_CBOR_FLASH_CHUNK;
		size_t n = CONF_CBOR_FLASH_CHUNK - used;
		if (n > len) {
			n = len;
		}
		memcpy(self->wbuf + CONF_CBOR_FLASH_CHUNK + used, b, n);
		self->wbuf_pos += n;
		b += n;
		len -= n;

		if ((self->wbuf_pos % CONF_CBOR_FLASH_CHUNK) == 0) {
			conf_cbor_ret_t ret = conf_cbor_flash_flush(self, self->wbuf_pos - CONF_CBOR_FLASH_CHUNK);
			if (ret != CONF_CBOR_RET_OK) {
				return ret;
			}
		}
	}
	return CONF_CBOR_RET_OK;
}


static conf_cbor_ret_t conf_cbor_flash_close(ConfCbor *self, bool commit) {
	if (self->wbuf == NULL) {
		return CONF_CBOR_RET_OK;
	}

	conf_cbor_ret_t ret = CONF_CBOR_RET_OK;
	if (commit) {
		/* Pad the last partial chunk with the erased value, the parser stops at the end of the root map. */
		size_t used = self->wbuf_pos % CONF_CBOR_FLASH_CHUNK;
		if (used > 0) {
			memset(self->wbuf + CONF_CBOR_FLASH_CHUNK + used, 0xff, CONF_CBOR_FLASH_CHUNK - used);
			ret = conf_cbor_flash_flush(self, self->wbuf_pos - used);
		}
		/* Write the head last. Until now it reads as erased and the document is invalid. */
		if (ret == CONF_CBOR_RET_OK &&
		    self->conf.flash->vmt->write(self->conf.flash, self->conf.offset, self->wbuf,
		                                 CONF_CBOR_FLASH_CHUNK) != FLASH_RET_OK) {
			ret = CONF_CBOR_RET_IO;
		}
	}

	free(self->wbuf);
	self->wbuf = NULL;
	return ret;
}


static const struct conf_cbor_io conf_cbor_flash_io = {
	.open = conf_cbor_flash_open,
	.read = conf_cbor_flash_read,
	.write = conf_cbor_flash_write,
	.close = conf_cbor_flash_close,
};


/*********************************************************************************************************************
 * tinycbor reader/writer callbacks
 *********************************************************************************************************************/

static bool conf_cbor_reader_can_read(void *token, size_t len) {
	struct conf_cbor_cursor *c = token;
	return (len <= c->len) && (c->pos <= c->len - len);
}


static void *conf_cbor_reader_read(void *token, void *dst, size_t offset, size_t len) {
	struct conf_cbor_cursor *c = token;
	if (offset > c->len - c->pos || len > c->len - c->pos - offset) {
		return NULL;
	}
	if (c->self->io->read(c->self, c->pos + offset, dst, len) != CONF_CBOR_RET_OK) {
		return NULL;
	}
	return dst;
}


static void conf_cbor_reader_advance(void *token, size_t len) {
	struct conf_cbor_cursor *c = token;
	c->pos += len;
}


/* Strings are compared and read directly through the read handler, their data is never requested from the
 * parser. The parser still transfers a string to skip it when advancing past it, the cursor is therefore only
 * moved after the string and no data pointer is provided. */
static CborError conf_cbor_reader_transfer_string(void *token, const void **userptr, size_t offset, size_t len) {
	struct conf_cbor_cursor *c = token;
	if (offset > c->len - c->pos || len > c->len - c->pos - offset) {
		return CborErrorUnexpectedEOF;
	}
	*userptr = NULL;
	c->pos += offset + len;
	return CborNoError;
}


static const struct CborParserOperations conf_cbor_reader_ops = {
	.can_read_bytes = conf_cbor_reader_can_read,
	.read_bytes = conf_cbor_reader_read,
	.advance_bytes = conf_cbor_reader_advance,
	.transfer_string = conf_cbor_reader_transfer_string,
};


static CborError conf_cbor_writer(void *token, const void *data, size_t len, CborEncoderAppendType append) {
	(void)append;
	struct conf_cbor_cursor *c = token;
	if (len > c->len - c->pos) {
		return CborErrorOutOfMemory;
	}
	if (c->self->io->write(c->self, c->pos, data, len) != CONF_CBOR_RET_OK) {
		return CborErrorIO;
	}
	c->pos += len;
	return CborNoError;
}


/* Number of bytes a CBOR head (initial byte plus argument) occupies for the argument @p val. */
static size_t conf_cbor_head_size(uint64_t val) {
	if (val < 24u) {
		return 1u;
	}
	if (val < 0x100u) {
		return 2u;
	}
	if (val < 0x10000u) {
		return 3u;
	}
	if (val < 0x100000000ull) {
		return 5u;
	}
	return 9u;
}


/*********************************************************************************************************************
 * Mount tree
 *
 * Advertised Conf subtrees are mounted at the position given by their space-delimited service locator name.
 * A position in the mount tree is identified by the first @p level components of a reference name @p ref,
 * any advertised name mounted at or below the position. The names are walked directly, nothing is copied.
 *********************************************************************************************************************/

/* Node name being looked up, either the text string @p val at the cursor position or @p len bytes of @p str. */
struct conf_cbor_key {
	struct conf_cbor_cursor *c;
	CborValue *val;
	const char *str;
	size_t len;
};


/* Compare the text string @p key at the cursor position with @p len bytes of @p name, reading it directly from
 * the medium. */
static bool conf_cbor_key_equals(struct conf_cbor_cursor *c, CborValue *key, const char *name, size_t len) {
	size_t key_len = 0;
	if (cbor_value_get_string_length(key, &key_len) != CborNoError || key_len != len) {
		return false;
	}

	size_t pos = c->pos + conf_cbor_head_size(len);
	uint8_t buf[CONF_CBOR_CMP_CHUNK];
	while (len > 0) {
		size_t n = len < sizeof(buf) ? len : sizeof(buf);
		if (c->self->io->read(c->self, pos, buf, n) != CONF_CBOR_RET_OK || memcmp(buf, name, n) != 0) {
			return false;
		}
		pos += n;
		name += n;
		len -= n;
	}
	return true;
}


static bool conf_cbor_key_match(const struct conf_cbor_key *key, const char *name, size_t len) {
	if (key->c != NULL) {
		return conf_cbor_key_equals(key->c, key->val, name, len);
	}
	return key->len == len && memcmp(key->str, name, len) == 0;
}


/* Get the space-delimited component @p level of the service locator @p name. */
static bool conf_cbor_name_comp(const char *name, size_t level, const char **comp, size_t *len) {
	const char *p = name;
	for (size_t i = 0; *p != '\0'; i++) {
		const char *start = p;
		while (*p != '\0' && *p != ' ') {
			p++;
		}
		if (i == level) {
			*comp = start;
			*len = (size_t)(p - start);
			return true;
		}
		if (*p == ' ') {
			p++;
		}
	}
	return false;
}


/* Test whether the first @p level components of @p name and @p ref are equal. */
static bool conf_cbor_name_prefix(const char *name, const char *ref, size_t level) {
	for (size_t i = 0; i < level; i++) {
		const char *a = NULL;
		const char *b = NULL;
		size_t alen = 0;
		size_t blen = 0;
		if (!conf_cbor_name_comp(name, i, &a, &alen) || !conf_cbor_name_comp(ref, i, &b, &blen) || alen != blen ||
		    memcmp(a, b, alen) != 0) {
			return false;
		}
	}
	return true;
}


/* Find a real child of @p node named @p key. */
static Conf *conf_cbor_child_find(Conf *node, const struct conf_cbor_key *key) {
	if (node == NULL) {
		return NULL;
	}
	Conf *child = NULL;
	if (node->vmt->walk(node, CONF_DIR_CHILD, &child) != CONF_RET_OK) {
		child = NULL;
	}
	while (child != NULL) {
		const char *name = NULL;
		enum conf_type type = CONF_NONE;
		enum conf_flag flags = 0;
		if (child->vmt->stat(child, &name, &type, &flags) == CONF_RET_OK && name != NULL &&
		    conf_cbor_key_match(key, name, strlen(name))) {
			return child;
		}
		if (child->vmt->walk(child, CONF_DIR_NEXT, &child) != CONF_RET_OK) {
			child = NULL;
		}
	}
	return NULL;
}


/* Look up the child @p key of the mount tree position (@p ref, @p level). @p first is set to the first advertised
 * name mounted at or below the child position (NULL if there is none), @p mount to the Conf mounted exactly at the
 * child position (NULL if there is none). */
static void conf_cbor_mount_find(const struct conf_cbor_key *key, const char *ref, size_t level, Conf **mount,
                                 const char **first) {
	*mount = NULL;
	*first = NULL;
	Interface *iface = NULL;
	for (size_t i = 0; iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_CONF, i, &iface) ==
	     ISERVICELOCATOR_RET_OK; i++) {
		const char *name = NULL;
		const char *comp = NULL;
		size_t len = 0;
		if (iservicelocator_get_name(locator, iface, &name) != ISERVICELOCATOR_RET_OK || name == NULL ||
		    !conf_cbor_name_prefix(name, ref, level) || !conf_cbor_name_comp(name, level, &comp, &len) ||
		    !conf_cbor_key_match(key, comp, len)) {
			continue;
		}
		if (*first == NULL) {
			*first = name;
		}
		if (*mount == NULL && !conf_cbor_name_comp(name, level + 1, &comp, &len)) {
			*mount = (Conf *)iface;
		}
	}
}


/*********************************************************************************************************************
 * Saving
 *********************************************************************************************************************/

/* A leaf is saved if it can be read back and written, constants and status values are not configuration.
 * A subtree is saved if it contains at least one saveable leaf. */
static bool conf_cbor_saveable(Conf *node) {
	const char *name = NULL;
	enum conf_type type = CONF_NONE;
	enum conf_flag flags = 0;
	if (node->vmt->stat(node, &name, &type, &flags) != CONF_RET_OK || name == NULL) {
		return false;
	}

	if (type == CONF_SUBTREE) {
		Conf *child = NULL;
		if (node->vmt->walk(node, CONF_DIR_CHILD, &child) != CONF_RET_OK) {
			child = NULL;
		}
		while (child != NULL) {
			if (conf_cbor_saveable(child)) {
				return true;
			}
			if (child->vmt->walk(child, CONF_DIR_NEXT, &child) != CONF_RET_OK) {
				child = NULL;
			}
		}
		return false;
	}

	return (flags & CONF_READ) && (flags & CONF_WRITE) && !(flags & (CONF_CONST | CONF_STATUS));
}


static CborError conf_cbor_encode_val(CborEncoder *enc, enum conf_type type, const union conf_val *val) {
	switch (type) {
		case CONF_F:    return cbor_encode_float(enc, val->f);
		case CONF_U32:  return cbor_encode_uint(enc, val->u32);
		case CONF_S32:  return cbor_encode_int(enc, val->i32);
		case CONF_U16:  return cbor_encode_uint(enc, val->u16);
		case CONF_S16:  return cbor_encode_int(enc, val->i16);
		case CONF_U8:   return cbor_encode_uint(enc, val->u8);
		case CONF_S8:   return cbor_encode_int(enc, val->i8);
		case CONF_B:    return cbor_encode_boolean(enc, val->b);
		case CONF_BSTR: return cbor_encode_byte_string(enc, val->bstr.buf, val->bstr.len);
		case CONF_STR:  return cbor_encode_text_string(enc, val->str.buf, val->str.len);
		case CONF_ENUM: return cbor_encode_uint(enc, val->u32);
		default:        return CborErrorUnknownType;
	}
}


/* Test whether any Conf mounted at or below the mount tree position (@p ref, @p level) has a saveable value. */
static bool conf_cbor_mount_saveable(const char *ref, size_t level) {
	Interface *iface = NULL;
	for (size_t i = 0; iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_CONF, i, &iface) ==
	     ISERVICELOCATOR_RET_OK; i++) {
		const char *name = NULL;
		if (iservicelocator_get_name(locator, iface, &name) == ISERVICELOCATOR_RET_OK && name != NULL &&
		    conf_cbor_name_prefix(name, ref, level) && conf_cbor_saveable((Conf *)iface)) {
			return true;
		}
	}
	return false;
}


static conf_cbor_ret_t conf_cbor_save_map(CborEncoder *enc, Conf *node, const char *ref, size_t level);

/* Encode @p len bytes of @p name followed by the map of the position, omit the entry if nothing is saveable. */
static conf_cbor_ret_t conf_cbor_save_entry(CborEncoder *map, const char *name, size_t len, Conf *node,
                                            const char *ref, size_t level) {
	if (!(node != NULL && conf_cbor_saveable(node)) && !(ref != NULL && conf_cbor_mount_saveable(ref, level))) {
		return CONF_CBOR_RET_OK;
	}
	if (cbor_encode_text_string(map, name, len) != CborNoError) {
		return CONF_CBOR_RET_IO;
	}
	return conf_cbor_save_map(map, node, ref, level);
}


/*
 * Encode a position as a map. @p node is the real Conf at the position (NULL for a virtual node), (@p ref, @p level)
 * is the mount tree position (@p ref is NULL if nothing is mounted at or below it). Real children come first, a
 * Conf mounted at the same position shadows the real child. Mount tree children not present as real children
 * follow, each emitted once at its first advertised name.
 */
static conf_cbor_ret_t conf_cbor_save_map(CborEncoder *enc, Conf *node, const char *ref, size_t level) {
	CborEncoder map;
	if (cbor_encoder_create_map(enc, &map, CborIndefiniteLength) != CborNoError) {
		return CONF_CBOR_RET_IO;
	}

	Conf *child = NULL;
	if (node == NULL || node->vmt->walk(node, CONF_DIR_CHILD, &child) != CONF_RET_OK) {
		child = NULL;
	}
	while (child != NULL) {
		const char *name = NULL;
		enum conf_type type = CONF_NONE;
		enum conf_flag flags = 0;
		if (child->vmt->stat(child, &name, &type, &flags) == CONF_RET_OK && name != NULL) {
			struct conf_cbor_key key = {.str = name, .len = strlen(name)};
			Conf *mount = NULL;
			const char *first = NULL;
			if (ref != NULL) {
				conf_cbor_mount_find(&key, ref, level, &mount, &first);
			}
			conf_cbor_ret_t ret = CONF_CBOR_RET_OK;
			if (mount != NULL || first != NULL || type == CONF_SUBTREE) {
				ret = conf_cbor_save_entry(&map, name, key.len, mount != NULL ? mount :
				                           (type == CONF_SUBTREE ? child : NULL), first, level + 1);
			} else if (conf_cbor_saveable(child)) {
				/* Read first, the key is not emitted for a value which cannot be read. */
				union conf_val val = {0};
				if (child->vmt->read(child, &val) == CONF_RET_OK &&
				    (cbor_encode_text_string(&map, name, key.len) != CborNoError ||
				     conf_cbor_encode_val(&map, type, &val) != CborNoError)) {
					ret = CONF_CBOR_RET_IO;
				}
			}
			if (ret != CONF_CBOR_RET_OK) {
				return ret;
			}
		}
		if (child->vmt->walk(child, CONF_DIR_NEXT, &child) != CONF_RET_OK) {
			child = NULL;
		}
	}

	Interface *iface = NULL;
	for (size_t i = 0; ref != NULL && iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_CONF, i, &iface) ==
	     ISERVICELOCATOR_RET_OK; i++) {
		const char *name = NULL;
		struct conf_cbor_key key = {0};
		if (iservicelocator_get_name(locator, iface, &name) != ISERVICELOCATOR_RET_OK || name == NULL ||
		    !conf_cbor_name_prefix(name, ref, level) || !conf_cbor_name_comp(name, level, &key.str, &key.len) ||
		    conf_cbor_child_find(node, &key) != NULL) {
			continue;
		}
		Conf *mount = NULL;
		const char *first = NULL;
		conf_cbor_mount_find(&key, ref, level, &mount, &first);
		if (first != name) {
			continue;
		}
		conf_cbor_ret_t ret = conf_cbor_save_entry(&map, key.str, key.len, mount, first, level + 1);
		if (ret != CONF_CBOR_RET_OK) {
			return ret;
		}
	}

	if (cbor_encoder_close_container(enc, &map) != CborNoError) {
		return CONF_CBOR_RET_IO;
	}
	return CONF_CBOR_RET_OK;
}


/*********************************************************************************************************************
 * Loading
 *********************************************************************************************************************/

static conf_cbor_ret_t conf_cbor_get_uint(CborValue *v, uint64_t max, uint64_t *u) {
	if (!cbor_value_is_unsigned_integer(v) || cbor_value_get_uint64(v, u) != CborNoError || *u > max) {
		return CONF_CBOR_RET_FAILED;
	}
	return CONF_CBOR_RET_OK;
}


static conf_cbor_ret_t conf_cbor_get_int(CborValue *v, int64_t min, int64_t max, int64_t *i) {
	if (!cbor_value_is_integer(v) || cbor_value_get_int64_checked(v, i) != CborNoError || *i < min || *i > max) {
		return CONF_CBOR_RET_FAILED;
	}
	return CONF_CBOR_RET_OK;
}


/* Read a text or byte string value @p v at the cursor position directly from the medium and write it. */
static conf_cbor_ret_t conf_cbor_load_string(struct conf_cbor_cursor *c, CborValue *v, Conf *node,
                                             enum conf_type type) {
	if ((type == CONF_STR && !cbor_value_is_text_string(v)) || (type == CONF_BSTR && !cbor_value_is_byte_string(v))) {
		return CONF_CBOR_RET_FAILED;
	}
	size_t len = 0;
	if (cbor_value_get_string_length(v, &len) != CborNoError) {
		return CONF_CBOR_RET_MALFORMED;
	}

	/* Text strings are terminated for the convenience of the node implementation. */
	uint8_t *buf = malloc(len + 1);
	if (buf == NULL) {
		return CONF_CBOR_RET_NOMEM;
	}
	conf_cbor_ret_t ret = CONF_CBOR_RET_OK;
	if (len > 0) {
		ret = c->self->io->read(c->self, c->pos + conf_cbor_head_size(len), buf, len);
	}
	buf[len] = '\0';

	if (ret == CONF_CBOR_RET_OK) {
		union conf_val val = {0};
		if (type == CONF_STR) {
			val.str.buf = (char *)buf;
			val.str.len = len;
		} else {
			val.bstr.buf = buf;
			val.bstr.len = len;
		}
		if (node->vmt->write(node, val) != CONF_RET_OK) {
			ret = CONF_CBOR_RET_FAILED;
		}
	}
	free(buf);
	return ret;
}


/* Decode the leaf value @p v according to the node @p type and write it. */
static conf_cbor_ret_t conf_cbor_load_value(struct conf_cbor_cursor *c, CborValue *v, Conf *node,
                                            enum conf_type type) {
	union conf_val val = {0};
	uint64_t u = 0;
	int64_t i = 0;
	switch (type) {
		case CONF_F: {
			if (cbor_value_is_float(v)) {
				cbor_value_get_float(v, &val.f);
			} else if (cbor_value_is_double(v)) {
				double d = 0.0;
				cbor_value_get_double(v, &d);
				val.f = (float)d;
			} else {
				return CONF_CBOR_RET_FAILED;
			}
			break;
		}
		case CONF_U32:
		case CONF_ENUM: {
			if (conf_cbor_get_uint(v, UINT32_MAX, &u) != CONF_CBOR_RET_OK) {
				return CONF_CBOR_RET_FAILED;
			}
			val.u32 = (uint32_t)u;
			break;
		}
		case CONF_U16: {
			if (conf_cbor_get_uint(v, UINT16_MAX, &u) != CONF_CBOR_RET_OK) {
				return CONF_CBOR_RET_FAILED;
			}
			val.u16 = (uint16_t)u;
			break;
		}
		case CONF_U8: {
			if (conf_cbor_get_uint(v, UINT8_MAX, &u) != CONF_CBOR_RET_OK) {
				return CONF_CBOR_RET_FAILED;
			}
			val.u8 = (uint8_t)u;
			break;
		}
		case CONF_S32: {
			if (conf_cbor_get_int(v, INT32_MIN, INT32_MAX, &i) != CONF_CBOR_RET_OK) {
				return CONF_CBOR_RET_FAILED;
			}
			val.i32 = (int32_t)i;
			break;
		}
		case CONF_S16: {
			if (conf_cbor_get_int(v, INT16_MIN, INT16_MAX, &i) != CONF_CBOR_RET_OK) {
				return CONF_CBOR_RET_FAILED;
			}
			val.i16 = (int16_t)i;
			break;
		}
		case CONF_S8: {
			if (conf_cbor_get_int(v, INT8_MIN, INT8_MAX, &i) != CONF_CBOR_RET_OK) {
				return CONF_CBOR_RET_FAILED;
			}
			val.i8 = (int8_t)i;
			break;
		}
		case CONF_B: {
			if (!cbor_value_is_boolean(v)) {
				return CONF_CBOR_RET_FAILED;
			}
			cbor_value_get_boolean(v, &val.b);
			break;
		}
		case CONF_STR:
		case CONF_BSTR:
			return conf_cbor_load_string(c, v, node, type);
		default:
			return CONF_CBOR_RET_FAILED;
	}

	if (node->vmt->write(node, val) != CONF_RET_OK) {
		return CONF_CBOR_RET_FAILED;
	}
	return CONF_CBOR_RET_OK;
}


/*
 * Load the CBOR map @p map into the position given by the real Conf @p node (NULL for a virtual node) and the
 * mount tree position (@p ref, @p level), resolved the same way as when saving. Entries are matched by name,
 * maps recurse into subtrees, leaves are written. Unmatched entries and values not matching the node type are
 * skipped. On return @p map is positioned after the map.
 */
static conf_cbor_ret_t conf_cbor_load_map(struct conf_cbor_cursor *c, CborValue *map, Conf *node, const char *ref,
                                          size_t level) {
	CborValue elem;
	if (cbor_value_enter_container(map, &elem) != CborNoError) {
		return CONF_CBOR_RET_MALFORMED;
	}

	while (!cbor_value_at_end(&elem)) {
		if (!cbor_value_is_text_string(&elem)) {
			return CONF_CBOR_RET_MALFORMED;
		}
		struct conf_cbor_key key = {.c = c, .val = &elem};
		Conf *mount = NULL;
		const char *first = NULL;
		if (ref != NULL) {
			conf_cbor_mount_find(&key, ref, level, &mount, &first);
		}
		Conf *child = mount != NULL ? mount : conf_cbor_child_find(node, &key);
		const char *name = NULL;
		enum conf_type type = CONF_NONE;
		enum conf_flag flags = 0;
		if (child == NULL || child->vmt->stat(child, &name, &type, &flags) != CONF_RET_OK) {
			child = NULL;
			type = CONF_NONE;
		}
		if (cbor_value_advance(&elem) != CborNoError) {
			return CONF_CBOR_RET_MALFORMED;
		}

		if ((first != NULL || type == CONF_SUBTREE) && cbor_value_is_map(&elem)) {
			conf_cbor_ret_t ret = conf_cbor_load_map(c, &elem, type == CONF_SUBTREE ? child : NULL, first, level + 1);
			if (ret != CONF_CBOR_RET_OK) {
				return ret;
			}
			continue;
		}

		if (child != NULL && first == NULL && type != CONF_SUBTREE && !cbor_value_is_map(&elem)) {
			conf_cbor_ret_t ret = conf_cbor_load_value(c, &elem, child, type);
			if (ret == CONF_CBOR_RET_OK) {
				c->loaded++;
			} else if (ret == CONF_CBOR_RET_FAILED) {
				c->skipped++;
			} else {
				return ret;
			}
		} else {
			c->skipped++;
		}
		if (cbor_value_advance(&elem) != CborNoError) {
			return CONF_CBOR_RET_MALFORMED;
		}
	}

	if (cbor_value_leave_container(map, &elem) != CborNoError) {
		return CONF_CBOR_RET_MALFORMED;
	}
	return CONF_CBOR_RET_OK;
}


/*********************************************************************************************************************
 * Jobs
 *
 * Each job run creates a temporary task which calls conf_cbor_save() or conf_cbor_load() and deletes itself.
 * The jobs cannot run concurrently, they are serialized by the instance lock taken in save and load.
 *********************************************************************************************************************/

static const char *conf_cbor_ret_str(conf_cbor_ret_t ret) {
	switch (ret) {
		case CONF_CBOR_RET_OK:        return "";
		case CONF_CBOR_RET_NULL:      return "no storage configured";
		case CONF_CBOR_RET_NOMEM:     return "out of memory";
		case CONF_CBOR_RET_IO:        return "storage I/O error";
		case CONF_CBOR_RET_MALFORMED: return "no valid configuration found";
		default:                      return "failed";
	}
}


/* Record the outcome of a run and make the job ready for the next one. */
static void conf_cbor_job_end(struct conf_cbor_job *j, conf_cbor_ret_t ret) {
	j->ret = ret;
	j->result = (ret == CONF_CBOR_RET_OK) ? JOB_RESULT_SUCCESSFUL : JOB_RESULT_FAILED;
	j->state = JOB_STATE_SCHEDULED;
}


static void conf_cbor_save_task(void *p) {
	ConfCbor *self = p;
	conf_cbor_job_end(&self->jobs.save, conf_cbor_save(self));
	vTaskDelete(NULL);
}


static void conf_cbor_load_task(void *p) {
	ConfCbor *self = p;
	conf_cbor_job_end(&self->jobs.load, conf_cbor_load(self));
	vTaskDelete(NULL);
}


/* Move a scheduled job to the running state and create the task executing it. */
static job_ret_t conf_cbor_job_start(Job *job, TaskFunction_t fn, const char *name) {
	struct conf_cbor_job *j = (struct conf_cbor_job *)job;

	taskENTER_CRITICAL();
	bool scheduled = j->state == JOB_STATE_SCHEDULED;
	if (scheduled) {
		j->state = JOB_STATE_RUNNING;
	}
	taskEXIT_CRITICAL();
	if (!scheduled) {
		return JOB_RET_FAILED;
	}

	if (xTaskCreate(fn, name, CONF_CBOR_JOB_STACK_SIZE, job->parent, 1, NULL) != pdPASS) {
		conf_cbor_job_end(j, CONF_CBOR_RET_NOMEM);
		return JOB_RET_FAILED;
	}
	return JOB_RET_OK;
}


static job_ret_t conf_cbor_save_job_start(Job *job) {
	return conf_cbor_job_start(job, conf_cbor_save_task, "conf-save");
}


static job_ret_t conf_cbor_load_job_start(Job *job) {
	return conf_cbor_job_start(job, conf_cbor_load_task, "conf-load");
}


/* Load and save cannot be interrupted, cancel, pause and resume are not supported. */
static job_ret_t conf_cbor_job_unsupported(Job *job) {
	(void)job;
	return JOB_RET_FAILED;
}


/* The run is a single step, it is either in progress or done. */
static job_ret_t conf_cbor_job_progress(Job *job, uint32_t *total, uint32_t *done) {
	struct conf_cbor_job *j = (struct conf_cbor_job *)job;
	*total = 1;
	*done = (j->state == JOB_STATE_SCHEDULED) ? 1 : 0;
	return JOB_RET_OK;
}


static job_ret_t conf_cbor_job_get_state(Job *job, enum job_state *state) {
	*state = ((struct conf_cbor_job *)job)->state;
	return JOB_RET_OK;
}


static job_ret_t conf_cbor_job_get_result(Job *job, enum job_result *result, char *msg, size_t msg_size) {
	struct conf_cbor_job *j = (struct conf_cbor_job *)job;
	*result = j->result;
	if (msg != NULL && msg_size > 0) {
		snprintf(msg, msg_size, "%s", (j->result == JOB_RESULT_FAILED) ? conf_cbor_ret_str(j->ret) : "");
	}
	return JOB_RET_OK;
}


static const struct job_vmt conf_cbor_save_job_vmt = {
	.start = conf_cbor_save_job_start,
	.cancel = conf_cbor_job_unsupported,
	.pause = conf_cbor_job_unsupported,
	.resume = conf_cbor_job_unsupported,
	.progress = conf_cbor_job_progress,
	.get_state = conf_cbor_job_get_state,
	.get_result = conf_cbor_job_get_result,
};


static const struct job_vmt conf_cbor_load_job_vmt = {
	.start = conf_cbor_load_job_start,
	.cancel = conf_cbor_job_unsupported,
	.pause = conf_cbor_job_unsupported,
	.resume = conf_cbor_job_unsupported,
	.progress = conf_cbor_job_progress,
	.get_state = conf_cbor_job_get_state,
	.get_result = conf_cbor_job_get_result,
};


/*********************************************************************************************************************
 * Public API
 *********************************************************************************************************************/

conf_cbor_ret_t conf_cbor_init(ConfCbor *self, const struct conf_cbor_conf *conf) {
	if (u_assert(self != NULL) ||
	    u_assert(conf != NULL)) {
		return CONF_CBOR_RET_NULL;
	}
	memset(self, 0, sizeof(ConfCbor));
	memcpy(&self->conf, conf, sizeof(struct conf_cbor_conf));

	/* Jobs are usable even if the initialization fails below, their runs then end as failed. */
	self->jobs.save.job.vmt = &conf_cbor_save_job_vmt;
	self->jobs.save.job.parent = self;
	self->jobs.load.job.vmt = &conf_cbor_load_job_vmt;
	self->jobs.load.job.parent = self;

	if (self->conf.flash != NULL) {
		self->io = &conf_cbor_flash_io;
	} else {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("no storage configured"));
		return CONF_CBOR_RET_NULL;
	}

	self->lock = xSemaphoreCreateMutex();
	if (self->lock == NULL) {
		return CONF_CBOR_RET_NOMEM;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialized"));
	return CONF_CBOR_RET_OK;
}


conf_cbor_ret_t conf_cbor_free(ConfCbor *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->lock != NULL)) {
		return CONF_CBOR_RET_NULL;
	}
	/* The job tasks use the instance, it cannot be freed while a job is running. */
	if (self->jobs.save.state != JOB_STATE_SCHEDULED || self->jobs.load.state != JOB_STATE_SCHEDULED) {
		return CONF_CBOR_RET_FAILED;
	}

	xSemaphoreTake(self->lock, portMAX_DELAY);
	vSemaphoreDelete(self->lock);
	self->lock = NULL;

	return CONF_CBOR_RET_OK;
}


conf_cbor_ret_t conf_cbor_save(ConfCbor *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->lock != NULL)) {
		return CONF_CBOR_RET_NULL;
	}
	xSemaphoreTake(self->lock, portMAX_DELAY);

	struct conf_cbor_cursor c = {.self = self};
	conf_cbor_ret_t ret = self->io->open(self, true, &c.len);
	if (ret == CONF_CBOR_RET_OK) {
		CborEncoder enc;
		cbor_encoder_init_writer(&enc, conf_cbor_writer, &c);
		ret = conf_cbor_save_map(&enc, NULL, "", 0);
		conf_cbor_ret_t close_ret = self->io->close(self, ret == CONF_CBOR_RET_OK);
		if (ret == CONF_CBOR_RET_OK) {
			ret = close_ret;
		}
	}

	if (ret == CONF_CBOR_RET_OK) {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("configuration saved (%lu bytes)"),
		      (unsigned long)c.pos);
	} else {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot save configuration, ret = %d"), ret);
	}

	xSemaphoreGive(self->lock);
	return ret;
}


conf_cbor_ret_t conf_cbor_load(ConfCbor *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->lock != NULL)) {
		return CONF_CBOR_RET_NULL;
	}
	xSemaphoreTake(self->lock, portMAX_DELAY);

	struct conf_cbor_cursor c = {.self = self};
	conf_cbor_ret_t ret = self->io->open(self, false, &c.len);
	if (ret == CONF_CBOR_RET_OK) {
		CborParser parser;
		CborValue it;
		if (cbor_parser_init_reader(&conf_cbor_reader_ops, &parser, &it, &c) != CborNoError ||
		    !cbor_value_is_map(&it)) {
			ret = CONF_CBOR_RET_MALFORMED;
		} else {
			ret = conf_cbor_load_map(&c, &it, NULL, "", 0);
		}
		self->io->close(self, false);
	}

	if (ret == CONF_CBOR_RET_OK) {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("configuration loaded, %lu values set, %lu skipped"),
		      (unsigned long)c.loaded, (unsigned long)c.skipped);
	} else if (ret == CONF_CBOR_RET_MALFORMED) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("no valid configuration found"));
	} else {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot load configuration, ret = %d"), ret);
	}

	xSemaphoreGive(self->lock);
	return ret;
}

