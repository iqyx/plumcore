/**
 * uMeshFw uMesh layer 2 neightbor table
 *
 * Copyright (C) 2015, Marek Koza, qyx@krtko.org
 *
 * This file is part of uMesh node firmware (http://qyx.krtko.org/projects/umesh)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _UMESH_L2_NBTABLE_H_
#define _UMESH_L2_NBTABLE_H_

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "umesh_l2_keymgr.h"


#define UMESH_L2_NBTABLE_STEP_INTERVAL_MS 100
#define UMESH_L2_NBTABLE_UNREACHABLE_TIME_THRESHOLD 2000
#define UMESH_L2_NBTABLE_UNREACHABLE_TIME_MAX 30000


enum umesh_l2_nbtable_item_state {
	/**
	 * An item with its state set to EMPTY is not used at the moment and can
	 * be used to insert a new neighbor.
	 *
	 * State timeout: max
	 * State timeout transition: do nothing
	 * Beacon timeout: configured beacon timeout
	 * Beacon timeout transition: do nothing
	 * nbtable main loop: do nothing
	 */
	UMESH_L2_NBTABLE_ITEM_STATE_EMPTY = 0,

	UMESH_L2_NBTABLE_ITEM_STATE_VALID,


	/**
	 * A new neighbor was inserted. This is the only state which can be set
	 * when the new neighbor is inserted.
	 *
	 * State timeout: must be set to be longer than the time it takes the
	 *                longest loop operation to finish its job.
	 *                5 seconds recommended.
	 * State timeout transition: GUARD
	 * Beacon timeout: configured beacon timeout
	 * Beacon timeout transition: UNREACHABLE
	 * nbtable main loop: Request a key exchange to be started tiwh the
	 *                    neighbor. State transition: KE_PENDING. Save the
	 *                    key exchange process reference (to be able to
	 *                    check the KE state later).
	 */
	UMESH_L2_NBTABLE_ITEM_STATE_NEW,

	/**
	 * Guard state (guard interval) is used to prevent any other operations
	 * on the affected item if the previous action failed. It causes any
	 * other requests to insert the same TID to fail.
	 *
	 * State timeout: It defines how much time it takes for the same TID to
	 *                be reinserted to the neighbor table and the failed
	 *                action to be repeated.
	 * State timeout transition: OLD
	 * Beacon timeout: configured beacon timeout
	 * Beacon timeout transition: UNREACHABLE
	 * nbtable main loop: do nothing
	 */
	UMESH_L2_NBTABLE_ITEM_STATE_GUARD,

	/**
	 * This item can be safely removed from the nbtable.
	 *
	 * nbtable main loop: remove the item from the nbtable.
	 */
	UMESH_L2_NBTABLE_ITEM_STATE_OLD,
};


struct umesh_l2_nbtable_item {

	enum umesh_l2_nbtable_item_state state;

	uint32_t state_timeout_msec;
	uint32_t unreachable_time_msec;

	uint32_t tid;

	/* Neighbor statistics */
	int32_t fei_hz;
	int16_t rssi_10dBm;
	uint8_t lqi_percent;
	uint16_t txcounter;
	uint32_t rx_packets;
	uint32_t rx_packets_dropped;
	uint32_t rx_bytes;
	uint32_t rx_bytes_dropped;
	uint32_t tx_packets;
	uint32_t tx_bytes;

	L2KeyManagerSession *key_manager_session;
};

struct umesh_l2_nbtable {
	struct umesh_l2_nbtable_item *nbtable;
	size_t size;

	TaskHandle_t main_task;

	L2KeyManager *key_manager;

};


int32_t umesh_l2_nbtable_init(struct umesh_l2_nbtable *nbtable, size_t nbtable_size);
#define UMESH_L2_NBTABLE_INIT_OK 0
#define UMESH_L2_NBTABLE_INIT_FAILED -1

int32_t umesh_l2_nbtable_free(struct umesh_l2_nbtable *nbtable);
#define UMESH_L2_NBTABLE_FREE_OK 0
#define UMESH_L2_NBTABLE_FREE_FAILED -1

int32_t umesh_l2_nbtable_set_key_manager(struct umesh_l2_nbtable *nbtable, L2KeyManager *key_manager);
#define UMESH_L2_NBTABLE_SET_KEY_MANAGER_OK 0
#define UMESH_L2_NBTABLE_SET_KEY_MANAGER_FAILED -1

/** @todo get an empty nbtable item */
/** @todo find a nbtable item by key */
/** @todo init item */
/** @todo free item */

int32_t umesh_l2_nbtable_item_set_state(struct umesh_l2_nbtable_item *item, enum umesh_l2_nbtable_item_state state);
#define UMESH_L2_NBTABLE_ITEM_SET_STATE_OK 0
#define UMESH_L2_NBTABLE_ITEM_SET_STATE_FAILED -1

int32_t umesh_l2_nbtable_item_check_state_timeout(struct umesh_l2_nbtable *nbtable, struct umesh_l2_nbtable_item *item);
#define UMESH_L2_NBTABLE_ITEM_CHECK_STATE_TIMEOUT_ELAPSED 1
#define UMESH_L2_NBTABLE_ITEM_CHECK_STATE_TIMEOUT_OK 0
#define UMESH_L2_NBTABLE_ITEM_CHECK_STATE_TIMEOUT_FAILED -1

int32_t umesh_l2_nbtable_item_check_unreachable_time(struct umesh_l2_nbtable_item *item);
#define UMESH_L2_NBTABLE_ITEM_CHECK_UNREACHABLE_TIME_ELAPSED 1
#define UMESH_L2_NBTABLE_ITEM_CHECK_UNREACHABLE_TIME_OK 0
#define UMESH_L2_NBTABLE_ITEM_CHECK_UNREACHABLE_TIME_FAILED -1

int32_t umesh_l2_nbtable_item_adjust_timeouts(struct umesh_l2_nbtable_item *item, uint32_t step);
#define UMESH_L2_NBTABLE_ITEM_ADJUST_TIMEOUTS_OK 0
#define UMESH_L2_NBTABLE_ITEM_ADJUST_TIMEOUTS_FAILED -1

int32_t umesh_l2_nbtable_item_loop_step(struct umesh_l2_nbtable *nbtable, struct umesh_l2_nbtable_item *item);
#define UMESH_L2_NBTABLE_ITEM_LOOP_STEP_OK 0
#define UMESH_L2_NBTABLE_ITEM_LOOP_STEP_FAILED -1

int32_t umesh_l2_nbtable_item_clear(struct umesh_l2_nbtable_item *item);
#define UMESH_L2_NBTABLE_ITEM_CLEAR_OK 0
#define UMESH_L2_NBTABLE_ITEM_CLEAR_FAILED -1

int32_t umesh_l2_nbtable_find(struct umesh_l2_nbtable *nbtable, uint32_t tid, struct umesh_l2_nbtable_item **item);
#define UMESH_L2_NBTABLE_FIND_OK 0
#define UMESH_L2_NBTABLE_FIND_FAILED -1
#define UMESH_L2_NBTABLE_FIND_NOT_FOUND -2

int32_t umesh_l2_nbtable_allocate(struct umesh_l2_nbtable *nbtable, struct umesh_l2_nbtable_item **item);
#define UMESH_L2_NBTABLE_ALLOCATE_OK 0
#define UMESH_L2_NBTABLE_ALLOCATE_FAILED -1
#define UMESH_L2_NBTABLE_ALLOCATE_FULL -2

bool umesh_l2_nbtable_item_is_reachable(struct umesh_l2_nbtable_item *item, bool crypto_required);

int32_t umesh_l2_nbtable_update_led(struct umesh_l2_nbtable *nbtable);
#define UMESH_L2_NBTABLE_UPDATE_LED_OK 0
#define UMESH_L2_NBTABLE_UPDATE_LED_FAILED -1


#endif

