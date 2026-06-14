/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Manufacturer information block (MIB) stored in a flash partition as CBOR
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 *
 * The MIB is a CBOR map stored directly in a flash partition, describing a configuration tree
 * which is turned into a live configlib-backed Conf tree mirroring the same structure.
 *
 * The implementation never buffers the whole partition in RAM. CBOR is parsed straight from flash
 * through a tinycbor reader callback.
 *
 * The CBOR map maps a node name (the text-string key) to a value:
 *
 *     - a nested map     -> a subtree, recursed into;
 *     - any other value  -> a leaf whose configuration type is inferred from the CBOR value:
 *           unsigned int -> CONF_U32,  negative int -> CONF_S32,  float -> CONF_F,
 *           bool -> CONF_B,  text string -> CONF_STR,  byte string -> CONF_BSTR.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <main.h>
#include <cbor.h>
#include <base64.h>
#include <blake2s.h>

#include <interfaces/flash.h>
#include <interfaces/conf.h>
#include <configlib.h>

#include "flash-cbor-mib.h"

#define MODULE_NAME "flash-cbor-mib"

/** Maximum length (including the terminator) of a node name read from the MIB. */
#define FLASH_CBOR_MIB_MAX_NAME_LEN 64u
/** Size of the reader's staging buffer for the tinycbor transfer_string callback. */
#define FLASH_CBOR_MIB_READ_SCRATCH 64u
/** Initial byte of an indefinite-length CBOR map: the MIB root. */
#define FLASH_CBOR_MIB_MAP_HEAD 0xbfu
/** Mandatory first key of a MIB map; its value carries the structure version. */
#define FLASH_CBOR_MIB_MAGIC "CBORMIB"
/** Top-level marker key selecting blake2s content verification (the digest is stored as a trailer). */
#define FLASH_CBOR_MIB_CHECK_BLAKE2S "check-blake2s"
/** Chunk size used when streaming the image through the verification hash. */
#define FLASH_CBOR_MIB_HASH_CHUNK 64u


/**
 * A dynamically allocated configuration node. The configlib value points its backing storage at
 * the members below; the node name is stored inline as a flexible array so the pointer configlib
 * keeps stays valid for the node's lifetime. Not typedef'd per project policy.
 */
struct flash_cbor_mib_node {
	ConfiglibValue value;
	struct flash_cbor_mib_node *alloc_next;

	/** Backing storage for scalar values. */
	union {
		float f;
		uint32_t u32;
		int32_t i32;
		bool b;
	} scalar;

	/** Backing storage for CONF_STR / CONF_BSTR values, malloc'd separately, NULL otherwise. */
	uint8_t *blob;
	size_t blob_len;

	char name[];
};


/**
 * tinycbor reader token: a window [base, base+len) of a Flash device read on demand. The parser
 * keeps a single shared cursor (@p pos), so the tree must be traversed strictly linearly using
 * enter_container/leave_container — there is no second independent cursor as with a memory buffer.
 */
struct mib_flash_reader {
	Flash *flash;
	size_t base;
	size_t len;
	size_t pos;
	uint8_t scratch[FLASH_CBOR_MIB_READ_SCRATCH];
};


/*********************************************************************************************************************
 * tinycbor flash reader callbacks
 *********************************************************************************************************************/

static bool mib_reader_can_read(void *token, size_t len) {
	struct mib_flash_reader *r = token;
	return (len <= r->len) && (r->pos <= r->len - len);
}


static void *mib_reader_read(void *token, void *dst, size_t offset, size_t len) {
	struct mib_flash_reader *r = token;
	if (offset > r->len - r->pos || len > r->len - r->pos - offset) {
		return NULL;
	}
	if (r->flash->vmt->read(r->flash, r->base + r->pos + offset, dst, len) != FLASH_RET_OK) {
		return NULL;
	}
	return dst;
}


static void mib_reader_advance(void *token, size_t len) {
	struct mib_flash_reader *r = token;
	r->pos += len;
}


static CborError mib_reader_transfer_string(void *token, const void **userptr, size_t offset, size_t len) {
	struct mib_flash_reader *r = token;
	r->pos += offset;
	if (len > sizeof(r->scratch) || len > r->len || r->pos > r->len - len) {
		return CborErrorUnexpectedEOF;
	}
	if (r->flash->vmt->read(r->flash, r->base + r->pos, r->scratch, len) != FLASH_RET_OK) {
		return CborErrorIO;
	}
	*userptr = r->scratch;
	r->pos += len;
	return CborNoError;
}


static const struct CborParserOperations mib_reader_ops = {
	.can_read_bytes = mib_reader_can_read,
	.read_bytes = mib_reader_read,
	.advance_bytes = mib_reader_advance,
	.transfer_string = mib_reader_transfer_string,
};


/* Number of bytes a CBOR head (initial byte plus argument) occupies for the argument @p val. */
static size_t mib_head_size(uint64_t val) {
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
 * Node allocation and tree building
 *********************************************************************************************************************/

static struct flash_cbor_mib_node *mib_alloc_node(FlashCborMib *self, const char *name) {
	size_t name_len = strlen(name);
	struct flash_cbor_mib_node *node = malloc(sizeof(struct flash_cbor_mib_node) + name_len + 1);
	if (node == NULL) {
		return NULL;
	}
	memset(node, 0, sizeof(struct flash_cbor_mib_node));
	memcpy(node->name, name, name_len + 1);

	configlib_init(&node->value, node->name);
	node->value.flags = CONF_READ;

	/* Link into the allocation list so flash_cbor_mib_free can release everything. */
	node->alloc_next = self->nodes;
	self->nodes = node;
	return node;
}


/* Attach @p node either as the first child of @p parent (prev == NULL) or after @p prev sibling. */
static flash_cbor_mib_ret_t mib_attach_node(struct flash_cbor_mib_node *node, ConfiglibValue *parent,
                                            struct flash_cbor_mib_node *prev) {
	configlib_ret_t r;
	if (prev == NULL) {
		r = configlib_append(&node->value, parent, CONF_DIR_CHILD);
	} else {
		r = configlib_append(&node->value, &prev->value, CONF_DIR_NEXT);
	}
	return (r == CONFIGLIB_RET_OK) ? FLASH_CBOR_MIB_RET_OK : FLASH_CBOR_MIB_RET_FAILED;
}


/* Store a scalar value @p v of inferred @p type into a node mapped to its inline backing store. */
static flash_cbor_mib_ret_t mib_set_scalar(struct flash_cbor_mib_node *node, enum conf_type type, CborValue *v) {
	configlib_map(&node->value, &node->scalar, type);

	union conf_val val = {0};
	switch (type) {
		case CONF_F: {
			if (cbor_value_is_double(v)) {
				double d = 0;
				cbor_value_get_double(v, &d);
				val.f = (float)d;
			} else {
				cbor_value_get_float(v, &val.f);
			}
			break;
		}
		case CONF_U32: {
			uint64_t u = 0;
			cbor_value_get_uint64(v, &u);
			val.u32 = (uint32_t)u;
			break;
		}
		case CONF_S32: {
			int64_t i = 0;
			cbor_value_get_int64(v, &i);
			val.i32 = (int32_t)i;
			break;
		}
		case CONF_B: {
			cbor_value_get_boolean(v, &val.b);
			break;
		}
		default:
			return FLASH_CBOR_MIB_RET_FAILED;
	}

	return (node->value.conf.vmt->write(&node->value.conf, val) == CONF_RET_OK)
	       ? FLASH_CBOR_MIB_RET_OK : FLASH_CBOR_MIB_RET_FAILED;
}


/*
 * Store a CONF_STR / CONF_BSTR value. The data is read straight from flash into freshly allocated
 * backing storage (bypassing the bounded reader scratch buffer), so arbitrarily long values are
 * supported. @p rd must be positioned at the value's CBOR head; the caller advances past it after.
 */
static flash_cbor_mib_ret_t mib_set_blob(struct flash_cbor_mib_node *node, struct mib_flash_reader *rd,
                                         enum conf_type type, CborValue *v) {
	bool is_str = (type == CONF_STR);

	size_t len = 0;
	if (cbor_value_get_string_length(v, &len) != CborNoError) {
		return FLASH_CBOR_MIB_RET_FAILED;
	}

	/* Strings keep a trailing NUL so they can be handed out as C strings by configlib. */
	size_t alloc = is_str ? len + 1u : len;
	node->blob = malloc(alloc != 0 ? alloc : 1u);
	if (node->blob == NULL) {
		return FLASH_CBOR_MIB_RET_NOMEM;
	}

	size_t data_off = rd->base + rd->pos + mib_head_size(len);
	if (len > 0 && rd->flash->vmt->read(rd->flash, data_off, node->blob, len) != FLASH_RET_OK) {
		return FLASH_CBOR_MIB_RET_NO_MIB;
	}

	if (is_str) {
		node->blob[len] = '\0';
		configlib_map_string(&node->value, (char *)node->blob, alloc);
	} else {
		node->blob_len = len;
		configlib_map(&node->value, node->blob, CONF_BSTR);
		node->value.size = alloc;
		node->value.len = &node->blob_len;
	}

	return FLASH_CBOR_MIB_RET_OK;
}


/* Infer the configuration type from the CBOR value @p v and store it in @p node. */
static flash_cbor_mib_ret_t mib_set_value(struct flash_cbor_mib_node *node, struct mib_flash_reader *rd,
                                          CborValue *v) {
	if (cbor_value_is_float(v) || cbor_value_is_double(v)) {
		return mib_set_scalar(node, CONF_F, v);
	}
	if (cbor_value_is_unsigned_integer(v)) {
		return mib_set_scalar(node, CONF_U32, v);
	}
	if (cbor_value_is_integer(v)) {
		return mib_set_scalar(node, CONF_S32, v);
	}
	if (cbor_value_is_boolean(v)) {
		return mib_set_scalar(node, CONF_B, v);
	}
	if (cbor_value_is_text_string(v)) {
		return mib_set_blob(node, rd, CONF_STR, v);
	}
	if (cbor_value_is_byte_string(v)) {
		return mib_set_blob(node, rd, CONF_BSTR, v);
	}
	return FLASH_CBOR_MIB_RET_FAILED;
}


/*
 * Build a configuration subtree from the CBOR map @p map, attaching one node per entry under
 * @p parent. Traversal is linear over the shared reader cursor: the key advances to the value,
 * then either the value is consumed (leaf) or recursed into and finished with leave_container.
 */
static flash_cbor_mib_ret_t mib_build_map(FlashCborMib *self, CborValue *map, struct mib_flash_reader *rd,
                                          ConfiglibValue *parent) {
	CborValue elem;
	if (cbor_value_enter_container(map, &elem) != CborNoError) {
		return FLASH_CBOR_MIB_RET_NO_MIB;
	}

	struct flash_cbor_mib_node *prev = NULL;
	while (!cbor_value_at_end(&elem)) {
		/* Map key: the node name. cbor_value_copy_text_string advances elem to the value. */
		if (!cbor_value_is_text_string(&elem)) {
			return FLASH_CBOR_MIB_RET_NO_MIB;
		}
		char name[FLASH_CBOR_MIB_MAX_NAME_LEN];
		size_t name_len = sizeof(name);
		if (cbor_value_copy_text_string(&elem, name, &name_len, &elem) != CborNoError) {
			return FLASH_CBOR_MIB_RET_NO_MIB;
		}
		name[sizeof(name) - 1] = '\0';

		struct flash_cbor_mib_node *node = mib_alloc_node(self, name);
		if (node == NULL) {
			return FLASH_CBOR_MIB_RET_NOMEM;
		}

		flash_cbor_mib_ret_t ret;
		if (cbor_value_is_map(&elem)) {
			node->value.type = CONF_SUBTREE;
			ret = mib_attach_node(node, parent, prev);
			if (ret != FLASH_CBOR_MIB_RET_OK) {
				return ret;
			}
			/* Recurse linearly; on return elem is positioned right after the child map. */
			ret = mib_build_map(self, &elem, rd, &node->value);
			if (ret != FLASH_CBOR_MIB_RET_OK) {
				return ret;
			}
		} else {
			ret = mib_set_value(node, rd, &elem);
			if (ret != FLASH_CBOR_MIB_RET_OK) {
				return ret;
			}
			ret = mib_attach_node(node, parent, prev);
			if (ret != FLASH_CBOR_MIB_RET_OK) {
				return ret;
			}
			if (cbor_value_advance(&elem) != CborNoError) {
				return FLASH_CBOR_MIB_RET_NO_MIB;
			}
		}

		prev = node;
	}

	cbor_value_leave_container(map, &elem);
	return FLASH_CBOR_MIB_RET_OK;
}


/*********************************************************************************************************************
 * MIB location
 *********************************************************************************************************************/

/* Determine the readable window [offset, offset+len) of the MIB within the flash partition. */
static flash_cbor_mib_ret_t mib_window(FlashCborMib *self, size_t *base, size_t *len) {
	size_t part_size = 0;
	flash_block_ops_t ops;
	if (self->config.flash->vmt->get_size(self->config.flash, 0, &part_size, &ops) != FLASH_RET_OK) {
		return FLASH_CBOR_MIB_RET_NO_MIB;
	}
	if (self->config.offset >= part_size) {
		return FLASH_CBOR_MIB_RET_NO_MIB;
	}
	size_t window = part_size - self->config.offset;
	if (self->config.max_size != 0 && self->config.max_size < window) {
		window = self->config.max_size;
	}
	*base = self->config.offset;
	*len = window;
	return FLASH_CBOR_MIB_RET_OK;
}


/*
 * Identify a MIB at @p base and return its structure version, or 0 when no (recognisable) MIB is
 * present. The MIB root is an indefinite-length CBOR map (initial byte 0xbf) whose mandatory first
 * entry maps the magic key "CBORMIB" to a non-zero unsigned structure version. Verifying both the
 * map head and the magic key makes detection robust against erased flash (reads back as 0xff) and
 * against unrelated CBOR documents that happen to start with a map.
 *
 * On a sane structure the whole map is traversed to its 0xff break and its total length (including
 * the 0xbf head and the break) is recorded in @p self->cbor_len.
 */
static uint32_t mib_present(FlashCborMib *self, size_t base, size_t window) {
	/* The root must be an indefinite-length map; check its initial byte before parsing. */
	uint8_t first = 0xff;
	if (self->config.flash->vmt->read(self->config.flash, base, &first, 1) != FLASH_RET_OK ||
	    first != FLASH_CBOR_MIB_MAP_HEAD) {
		return 0;
	}

	struct mib_flash_reader rd = {.flash = self->config.flash, .base = base, .len = window, .pos = 0};
	CborParser parser;
	CborValue it;
	CborValue e;
	if (cbor_parser_init_reader(&mib_reader_ops, &parser, &it, &rd) != CborNoError ||
	    !cbor_value_is_map(&it) ||
	    cbor_value_enter_container(&it, &e) != CborNoError) {
		return 0;
	}

	/* First key: the magic string identifying the document as a MIB. */
	char magic[sizeof(FLASH_CBOR_MIB_MAGIC)];
	size_t magic_len = sizeof(magic);
	if (!cbor_value_is_text_string(&e) ||
	    cbor_value_copy_text_string(&e, magic, &magic_len, &e) != CborNoError ||
	    strcmp(magic, FLASH_CBOR_MIB_MAGIC) != 0) {
		return 0;
	}

	/* Its value: the structure version, a non-zero unsigned integer. */
	uint64_t version = 0;
	if (!cbor_value_is_unsigned_integer(&e) ||
	    cbor_value_get_uint64(&e, &version) != CborNoError ||
	    version == 0 || version > UINT32_MAX) {
		return 0;
	}

	/* Skip the version value and every remaining entry so the cursor reaches the 0xff break, then
	 * leave the container. The shared reader cursor then sits just past the break: the total
	 * length of the MIB image including the 0xbf head and the closing break. */
	if (cbor_value_advance(&e) != CborNoError) {
		return 0;
	}
	while (!cbor_value_at_end(&e)) {
		if (cbor_value_advance(&e) != CborNoError) {
			return 0;
		}
	}
	if (cbor_value_leave_container(&it, &e) != CborNoError) {
		return 0;
	}

	self->cbor_len = rd.pos;
	return (uint32_t)version;
}


/*********************************************************************************************************************
 * MIB content verification
 *********************************************************************************************************************/

/*
 * Compute the blake2s digest over the MIB map ([base, base + cbor_len)) by streaming it from flash
 * in chunks, store it in @p self->check_blake2s, and compare it to the 32-byte digest trailer stored
 * immediately after the map's 0xff break (at base + cbor_len). Keeping the expected digest outside
 * the hashed region avoids the circularity of hashing the digest itself. The computed digest is kept
 * regardless of the outcome.
 */
static flash_cbor_mib_ret_t mib_check_blake2s(FlashCborMib *self, size_t base, size_t window) {
	/* The digest trailer must fit in the window right after the map. */
	if (window - self->cbor_len < sizeof(self->check_blake2s)) {
		return FLASH_CBOR_MIB_RET_NO_MIB;
	}
	uint8_t expected[sizeof(self->check_blake2s)];
	if (self->config.flash->vmt->read(self->config.flash, base + self->cbor_len, expected,
	                                  sizeof(expected)) != FLASH_RET_OK) {
		return FLASH_CBOR_MIB_RET_NO_MIB;
	}

	blake2s_ctx ctx;
	blake2s_init(&ctx, sizeof(self->check_blake2s));

	uint8_t chunk[FLASH_CBOR_MIB_HASH_CHUNK];
	size_t remaining = self->cbor_len;
	size_t off = base;
	while (remaining > 0) {
		size_t n = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
		if (self->config.flash->vmt->read(self->config.flash, off, chunk, n) != FLASH_RET_OK) {
			return FLASH_CBOR_MIB_RET_NO_MIB;
		}
		blake2s_update(&ctx, chunk, n);
		off += n;
		remaining -= n;
	}
	blake2s_final(&ctx, self->check_blake2s);

	return (memcmp(self->check_blake2s, expected, sizeof(self->check_blake2s)) == 0)
	       ? FLASH_CBOR_MIB_RET_OK : FLASH_CBOR_MIB_RET_BAD_SIGNATURE;
}


/*
 * Verify the integrity of the MIB image. The top-level keys are scanned for a supported check
 * method; currently only the "check-blake2s" marker is recognised, which selects a blake2s digest
 * of the map stored as a trailer immediately after the 0xff break.
 *
 * @return FLASH_CBOR_MIB_RET_OK if a check was found and passed,
 *         FLASH_CBOR_MIB_RET_BAD_SIGNATURE if a check was found but did not match,
 *         FLASH_CBOR_MIB_RET_NO_KEY if no supported check method is present,
 *         FLASH_CBOR_MIB_RET_NO_MIB on a malformed structure.
 */
static flash_cbor_mib_ret_t mib_verify(FlashCborMib *self, size_t base, size_t window) {
	struct mib_flash_reader rd = {.flash = self->config.flash, .base = base, .len = window, .pos = 0};
	CborParser parser;
	CborValue it;
	CborValue e;
	if (cbor_parser_init_reader(&mib_reader_ops, &parser, &it, &rd) != CborNoError ||
	    !cbor_value_is_map(&it) ||
	    cbor_value_enter_container(&it, &e) != CborNoError) {
		return FLASH_CBOR_MIB_RET_NO_MIB;
	}

	while (!cbor_value_at_end(&e)) {
		/* Map key. cbor_value_copy_text_string advances e to the value. */
		char key[FLASH_CBOR_MIB_MAX_NAME_LEN];
		size_t key_len = sizeof(key);
		if (!cbor_value_is_text_string(&e) ||
		    cbor_value_copy_text_string(&e, key, &key_len, &e) != CborNoError) {
			return FLASH_CBOR_MIB_RET_NO_MIB;
		}
		key[sizeof(key) - 1] = '\0';

		if (strcmp(key, FLASH_CBOR_MIB_CHECK_BLAKE2S) == 0) {
			return mib_check_blake2s(self, base, window);
		}

		/* Not a recognised check entry: skip its value and keep scanning. */
		if (cbor_value_advance(&e) != CborNoError) {
			return FLASH_CBOR_MIB_RET_NO_MIB;
		}
	}

	return FLASH_CBOR_MIB_RET_NO_KEY;
}


static void flash_cbor_mib_task(void *p) {
	FlashCborMib *self = p;

	configlib_init(&self->root, self->config.root_name != NULL ? self->config.root_name : "mib");
	self->root.type = CONF_SUBTREE;
	self->root.flags = CONF_READ;

	size_t base = 0;
	size_t window = 0;
	flash_cbor_mib_ret_t ret = mib_window(self, &base, &window);
	if (ret != FLASH_CBOR_MIB_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot access MIB partition"));
		goto err;
	}

	uint32_t version = mib_present(self, base, window);
	if (version == 0) {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("no MIB present in flash"));
		ret = FLASH_CBOR_MIB_RET_NO_MIB;
		goto err;
	}

	/* Verify the image integrity before building the tree from it. */
	ret = mib_verify(self, base, window);
	switch (ret) {
		case FLASH_CBOR_MIB_RET_OK:
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("MIB content verified (blake2s)"));
			break;
		case FLASH_CBOR_MIB_RET_NO_KEY:
			u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("MIB has no integrity check, loading unverified"));
			break;
		case FLASH_CBOR_MIB_RET_BAD_SIGNATURE:
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("MIB content verification failed"));
			goto err;
		default:
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("malformed MIB integrity check"));
			goto err;
	}

	/* The partition directly holds a CBOR map describing the configuration tree; parse it from flash. */
	struct mib_flash_reader rd = {.flash = self->config.flash, .base = base, .len = window, .pos = 0};
	CborParser parser;
	CborValue map;
	if (cbor_parser_init_reader(&mib_reader_ops, &parser, &map, &rd) != CborNoError ||
	    !cbor_value_is_map(&map)) {
		flash_cbor_mib_free(self);
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("malformed MIB"));
		ret = FLASH_CBOR_MIB_RET_NO_MIB;
		goto err;
	}

	ret = mib_build_map(self, &map, &rd, &self->root);
	if (ret != FLASH_CBOR_MIB_RET_OK) {
		flash_cbor_mib_free(self);
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("failed to build MIB tree"));
		goto err;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("MIB loaded (structure version %lu, %lu bytes)"),
	      (unsigned long)version, (unsigned long)self->cbor_len);
err:
	/* Report the minimum free stack (untouched 0xa5 paint) to help right-size the task stack. */
	u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("stack high water mark: %lu bytes free"),
	      (unsigned long)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
	vTaskDelete(NULL);
}


/*********************************************************************************************************************
 * Public API
 *********************************************************************************************************************/

flash_cbor_mib_ret_t flash_cbor_mib_init(FlashCborMib *self, const struct flash_cbor_mib_conf *config) {
	if (self == NULL || config == NULL || config->flash == NULL) {
		return FLASH_CBOR_MIB_RET_NULL;
	}
	memset(self, 0, sizeof(FlashCborMib));
	memcpy(&self->config, config, sizeof(struct flash_cbor_mib_conf));

	xTaskCreate(flash_cbor_mib_task, "flash-cbor-mib", configMINIMAL_STACK_SIZE + 192, (void *)self, 1, &(self->task));
	if (self->task == NULL) {
		return FLASH_CBOR_MIB_RET_FAILED;
	}

	return FLASH_CBOR_MIB_RET_OK;
}


flash_cbor_mib_ret_t flash_cbor_mib_free(FlashCborMib *self) {
	if (self == NULL) {
		return FLASH_CBOR_MIB_RET_NULL;
	}
	struct flash_cbor_mib_node *node = self->nodes;
	while (node != NULL) {
		struct flash_cbor_mib_node *next = node->alloc_next;
		free(node->blob);
		free(node);
		node = next;
	}
	self->nodes = NULL;
	return FLASH_CBOR_MIB_RET_OK;
}


flash_cbor_mib_ret_t flash_cbor_mib_get_root(FlashCborMib *self, Conf **root) {
	if (self == NULL || root == NULL) {
		return FLASH_CBOR_MIB_RET_NULL;
	}
	*root = &self->root.conf;
	return FLASH_CBOR_MIB_RET_OK;
}
