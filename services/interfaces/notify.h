/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Generic notification interface
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * A Notify interface is implemented by the party which wants to be notified (a consumer). The producer holds
 * a pointer to it and calls notify() whenever something the consumer may be interested in changes. The meaning
 * of the value is defined by the producer's interface (eg. a number of free bytes in a buffer, a number of
 * messages waiting to be read).
 *
 * A notification is a hint only, it carries no data. The consumer is expected to query the producer for the
 * actual state afterwards. notify() may be called from any task context of the producer, possibly with its
 * internal locks held. The implementation must therefore not block and must not call back into the producer,
 * it should only wake up the consumer (give a semaphore, send a task notification, ...).
 */

#pragma once

#include <stdint.h>


typedef enum {
	NOTIFY_RET_OK = 0,
	NOTIFY_RET_FAILED,
} notify_ret_t;


typedef struct notify Notify;

struct notify_vmt {
	/**
	 * @brief Notify the consumer
	 *
	 * @param self Notify instance (the consumer)
	 * @param value Arbitrary notification status defined by the producer
	 *
	 * @return NOTIFY_RET_OK on success or NOTIFY_RET_FAILED if the notification cannot be delivered.
	 */
	notify_ret_t (*notify)(Notify *self, uint32_t value);
};

struct notify {
	const struct notify_vmt *vmt;
	void *parent;
};
