/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 1-Wire bus master interface
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * Generic 1-Wire(R) bus master interface. A provider drives the single-wire half-duplex bus and exposes the link
 * layer primitives needed to talk to the devices sitting on it: a bus reset with presence detection, addressing of
 * the device and the byte-level data exchange.
 */

typedef enum {
	OW_RET_OK = 0,
	OW_RET_FAILED,
} ow_ret_t;

typedef struct ow Ow;
struct ow_vmt {
	/**
	 * @brief Exchange a sequence of bytes with the bus
	 *
	 * Shift @p n bytes out of @p out onto the bus while sampling the bus response into @p in. If @p out is NULL
	 * all slots are read slots (all-ones written). If @p in is NULL the read-back bytes are discarded.
	 *
	 * @param self Ow interface instance
	 * @param out Buffer with the bytes to write, or NULL for read-only slots
	 * @param in Buffer for the bytes read back, or NULL to discard them
	 * @param n Number of bytes to exchange
	 * @return OW_RET_FAILED on error or OW_RET_OK otherwise.
	 */
	ow_ret_t (*exchange)(Ow *self, const uint8_t *out, uint8_t *in, size_t n);

	/**
	 * @brief Generate a bus reset and detect the device presence pulse
	 *
	 * @param self Ow interface instance
	 * @param present Set to true if at least one device pulled the line low during the presence window. May be NULL.
	 * @return OW_RET_FAILED on error or OW_RET_OK otherwise.
	 */
	ow_ret_t (*reset)(Ow *self, bool *present);

	/**
	 * @brief Reset the bus and address the single device on it
	 *
	 * Generate a bus reset and, if a device is present, address it using SKIP ROM. Fails if no device responds.
	 *
	 * @param self Ow interface instance
	 * @return OW_RET_FAILED on error or if no device is present, OW_RET_OK otherwise.
	 */
	ow_ret_t (*select)(Ow *self);
};

typedef struct ow {
	const struct ow_vmt *vmt;
	void *parent;
} Ow;
