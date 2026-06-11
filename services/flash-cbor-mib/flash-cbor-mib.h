/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Manufacturer information block (MIB) stored in a flash partition as CBOR
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <interfaces/flash.h>
#include <interfaces/conf.h>
#include <configlib.h>


typedef enum {
	FLASH_CBOR_MIB_RET_OK = 0,
	FLASH_CBOR_MIB_RET_FAILED,
	FLASH_CBOR_MIB_RET_NULL,
	FLASH_CBOR_MIB_RET_NOMEM,
	FLASH_CBOR_MIB_RET_NO_MIB,
	FLASH_CBOR_MIB_RET_NO_KEY,
	FLASH_CBOR_MIB_RET_BAD_SIGNATURE,
} flash_cbor_mib_ret_t;


/**
 * Service-level configuration passed to @p flash_cbor_mib_init.
 *
 * The MIB lives in a flash partition (exposed as a @p Flash interface). It is a CBOR map stored
 * directly in flash that describes a configuration tree: each key is a node name and each value is
 * either a nested map (a subtree) or a scalar value whose configuration type is inferred from its
 * CBOR encoding. Not typedef'd per project policy.
 */
struct flash_cbor_mib_conf {
	/** Flash partition the MIB is read from. The CBOR map starts at @p offset. */
	Flash *flash;
	/** Offset of the CBOR map within the partition. */
	size_t offset;
	/** Maximum number of bytes to read and parse. 0 = up to the end of the partition. */
	size_t max_size;
	/**
	 * Ed25519 public key as 32 raw bytes. Reserved for future signature verification; currently
	 * unused. Takes precedence over @p public_key_b64 when non-NULL.
	 */
	const uint8_t *public_key;
	/**
	 * Ed25519 public key as a base64 string (typically forwarded verbatim from a port's Kconfig).
	 * Reserved for future signature verification; currently unused.
	 */
	const char *public_key_b64;
	/** Name given to the synthesized root subtree node. NULL defaults to "mib". */
	const char *root_name;
};


/* Forward declaration of the internal dynamically-allocated node list. */
struct flash_cbor_mib_node;

typedef struct {
	struct flash_cbor_mib_conf config;

	/** Synthesized root of the configuration tree. The parsed records become its children. */
	ConfiglibValue root;

	/** Total length of the CBOR MIB image in flash, including the 0xbf head and 0xff break. */
	size_t cbor_len;

	/** blake2s digest computed over the MIB image during verification (valid once loaded). */
	uint8_t check_blake2s[32];

	/** Singly-linked list of all dynamically allocated nodes, for cleanup in free(). */
	struct flash_cbor_mib_node *nodes;

	TaskHandle_t task;
} FlashCborMib;


/**
 * @brief Initialise the service: read and parse the MIB
 *
 * Checks whether a MIB is present in the configured flash partition and, when it is, parses the
 * CBOR document record by record (recursing into subtrees) building a configlib-backed
 * configuration tree.
 *
 * @return FLASH_CBOR_MIB_RET_OK on success,
 *         FLASH_CBOR_MIB_RET_NO_MIB if no MIB is present in the partition,
 *         FLASH_CBOR_MIB_RET_NOMEM on allocation failure,
 *         FLASH_CBOR_MIB_RET_NULL on a NULL argument,
 *         FLASH_CBOR_MIB_RET_FAILED otherwise.
 */
flash_cbor_mib_ret_t flash_cbor_mib_init(FlashCborMib *self, const struct flash_cbor_mib_conf *config);

/**
 * @brief Release all resources held by the service, destroying the configuration tree.
 */
flash_cbor_mib_ret_t flash_cbor_mib_free(FlashCborMib *self);

/**
 * @brief Get the root of the parsed configuration tree as a Conf subtree.
 *
 * @param root Returns the root Conf node, valid until @p flash_cbor_mib_free is called.
 * @return FLASH_CBOR_MIB_RET_OK on success, FLASH_CBOR_MIB_RET_NULL on a NULL argument.
 */
flash_cbor_mib_ret_t flash_cbor_mib_get_root(FlashCborMib *self, Conf **root);
