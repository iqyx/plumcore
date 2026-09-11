/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * CAN bus interface
 *
 * Copyright (c) 2018-2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>


typedef enum {
	CAN_RET_OK = 0,
	CAN_RET_FAILED,
	/** @brief The requested operation timed out before it could complete. */
	CAN_RET_TIMEOUT,
	/** @brief The peripheral entered the bus-off state and needs to recover. */
	CAN_RET_BUS_OFF,
} can_ret_t;


/**
 * @brief A single CAN/CAN-FD frame
 *
 * The same structure is used for both classic CAN (up to 8 data bytes) and
 * CAN-FD (up to 64 data bytes) frames. Not typedef'd per project policy.
 */
struct can_message {
	/** @brief Remote transmission request frame (no data payload). */
	bool rtr;
	/** @brief Extended (29 bit) identifier when true, standard (11 bit) otherwise. */
	bool extid;
	/** @brief Frame identifier, either 11 or 29 bit wide depending on @p extid. */
	uint32_t id;
	/** @brief Number of valid data bytes in @p buf. */
	size_t len;
	/** @brief Frame payload, holding both classic CAN and CAN-FD messages. */
	uint8_t buf[64];
	/** @brief Peripheral capture time of the frame (implementation defined units). */
	uint32_t timestamp;
};


typedef struct can Can;

struct can_vmt {
	/**
	 * @brief Send a single CAN frame
	 *
	 * Enqueue @p msg for transmission and block the calling thread until the
	 * frame is accepted by the peripheral or @p timeout_ms elapses.
	 *
	 * @param self Instance of the CAN interface
	 * @param msg Frame to transmit
	 * @param timeout_ms Maximum time to wait for a free transmit slot, in milliseconds
	 *
	 * @return CAN_RET_OK when the frame was queued for transmission,
	 *         CAN_RET_TIMEOUT when no transmit slot became available in time,
	 *         CAN_RET_BUS_OFF when the peripheral is in the bus-off state,
	 *         CAN_RET_FAILED otherwise.
	 */
	can_ret_t (*send)(Can *self, const struct can_message *msg, uint32_t timeout_ms);

	/**
	 * @brief Receive a single CAN frame
	 *
	 * Block the calling thread until a frame is available in the receive queue
	 * or @p timeout_ms elapses. On success @p msg is filled with the received frame.
	 *
	 * @param self Instance of the CAN interface
	 * @param msg Pointer to the structure receiving the frame
	 * @param timeout_ms Maximum time to wait for a frame, in milliseconds
	 *
	 * @return CAN_RET_OK when a frame was received,
	 *         CAN_RET_TIMEOUT when no frame arrived in time,
	 *         CAN_RET_FAILED otherwise.
	 */
	can_ret_t (*receive)(Can *self, struct can_message *msg, uint32_t timeout_ms);
};

typedef struct can {
	const struct can_vmt *vmt;
	void *parent;
} Can;
