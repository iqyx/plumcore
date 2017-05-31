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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "port.h"
#include "u_assert.h"
#include "u_log.h"
#include "umesh_l2_nbtable.h"
#include "umesh_l2_keymgr.h"


static void umesh_l2_nbtable_task(void *p) {

	struct umesh_l2_nbtable *nbtable = (struct umesh_l2_nbtable *)p;

	while (1) {
		for (size_t i = 0; i < nbtable->size; i++) {
			/* We are assuming that the following operations take
			 * ideally zero time. */
			umesh_l2_nbtable_item_check_unreachable_time(&(nbtable->nbtable[i]));
			umesh_l2_nbtable_item_check_state_timeout(nbtable, &(nbtable->nbtable[i]));
			umesh_l2_nbtable_item_loop_step(nbtable, &(nbtable->nbtable[i]));
			umesh_l2_nbtable_item_adjust_timeouts(&(nbtable->nbtable[i]), UMESH_L2_NBTABLE_STEP_INTERVAL_MS);
		}
		umesh_l2_nbtable_update_led(nbtable);
		vTaskDelay(UMESH_L2_NBTABLE_STEP_INTERVAL_MS);
	}

	vTaskDelete(NULL);
}


int32_t umesh_l2_nbtable_init(struct umesh_l2_nbtable *nbtable, size_t nbtable_size) {
	if (u_assert(nbtable != NULL)) {
		return UMESH_L2_NBTABLE_INIT_FAILED;
	}

	/* Clear the nbtable structure. This also sets all nbtable items to an
	 * empty state. */
	memset(nbtable, 0, sizeof(struct umesh_l2_nbtable));

	nbtable->size = nbtable_size;
	nbtable->nbtable = (struct umesh_l2_nbtable_item *)malloc(sizeof(struct umesh_l2_nbtable_item) * nbtable_size);
	if (nbtable->nbtable == NULL) {
		goto err;
	}

	memset(nbtable->nbtable, 0, nbtable->size * sizeof(struct umesh_l2_nbtable_item));

	xTaskCreate(umesh_l2_nbtable_task, "umesh_nbtable", configMINIMAL_STACK_SIZE + 128, (void *)nbtable, 1, &(nbtable->main_task));
	if (nbtable->main_task == NULL) {
		goto err;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE "neighbor table initialized, max %u items", "umesh", nbtable->size);
	return UMESH_L2_NBTABLE_INIT_OK;
err:
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE "neighbor table initialization failed", "umesh");
	umesh_l2_nbtable_free(nbtable);
	return UMESH_L2_NBTABLE_INIT_FAILED;
}


int32_t umesh_l2_nbtable_free(struct umesh_l2_nbtable *nbtable) {
	if (u_assert(nbtable != NULL)) {
		return UMESH_L2_NBTABLE_FREE_FAILED;
	}

	if (nbtable->nbtable != NULL) {
		free(nbtable->nbtable);
	}

	if (nbtable->main_task != NULL) {
		vTaskDelete(nbtable->main_task);
	}

	return UMESH_L2_NBTABLE_FREE_OK;
}


int32_t umesh_l2_nbtable_set_key_manager(struct umesh_l2_nbtable *nbtable, L2KeyManager *key_manager) {
	if (u_assert(nbtable != NULL) ||
	    u_assert(key_manager != NULL)) {
		return UMESH_L2_NBTABLE_SET_KEY_MANAGER_FAILED;
	}

	nbtable->key_manager = key_manager;

	return UMESH_L2_NBTABLE_SET_KEY_MANAGER_OK;
}


int32_t umesh_l2_nbtable_item_set_state(struct umesh_l2_nbtable_item *item, enum umesh_l2_nbtable_item_state state) {
	if (u_assert(item != NULL)) {
		return UMESH_L2_NBTABLE_ITEM_SET_STATE_FAILED;
	}

	switch (state) {
		case UMESH_L2_NBTABLE_ITEM_STATE_EMPTY:
			//~ umesh_l2_nbtable_item_clear(item);
			break;

		case UMESH_L2_NBTABLE_ITEM_STATE_NEW:
			item->state_timeout_msec = 5 * 1000;
			break;

		case UMESH_L2_NBTABLE_ITEM_STATE_VALID:
			item->state_timeout_msec = 10 * 60 * 1000;
			break;

		case UMESH_L2_NBTABLE_ITEM_STATE_GUARD:
			item->state_timeout_msec = 30 * 1000;
			break;

		case UMESH_L2_NBTABLE_ITEM_STATE_OLD:
			item->state_timeout_msec = UINT32_MAX;
			break;

		default:
			return UMESH_L2_NBTABLE_ITEM_SET_STATE_FAILED;
	}
	item->state = state;

	return UMESH_L2_NBTABLE_ITEM_SET_STATE_OK;
}


int32_t umesh_l2_nbtable_item_check_state_timeout(struct umesh_l2_nbtable *nbtable, struct umesh_l2_nbtable_item *item) {
	(void)nbtable;
	if (u_assert(item != NULL)) {
		return UMESH_L2_NBTABLE_ITEM_CHECK_STATE_TIMEOUT_FAILED;
	}

	if (item->state_timeout_msec == 0) {
		switch (item->state) {
			case UMESH_L2_NBTABLE_ITEM_STATE_EMPTY:
				/* Do nothing. */
				break;

			case UMESH_L2_NBTABLE_ITEM_STATE_NEW:
				umesh_l2_nbtable_item_set_state(item, UMESH_L2_NBTABLE_ITEM_STATE_GUARD);
				break;

			case UMESH_L2_NBTABLE_ITEM_STATE_VALID:
				umesh_l2_nbtable_item_set_state(item, UMESH_L2_NBTABLE_ITEM_STATE_NEW);
				break;

			case UMESH_L2_NBTABLE_ITEM_STATE_GUARD:
				umesh_l2_nbtable_item_set_state(item, UMESH_L2_NBTABLE_ITEM_STATE_OLD);
				break;

			case UMESH_L2_NBTABLE_ITEM_STATE_OLD:
				/* Do nothing */
				break;

			default:
				return UMESH_L2_NBTABLE_ITEM_CHECK_STATE_TIMEOUT_FAILED;
		}
		return UMESH_L2_NBTABLE_ITEM_CHECK_STATE_TIMEOUT_ELAPSED;
	}

	return UMESH_L2_NBTABLE_ITEM_CHECK_STATE_TIMEOUT_OK;
}


int32_t umesh_l2_nbtable_item_check_unreachable_time(struct umesh_l2_nbtable_item *item) {
	if (u_assert(item != NULL)) {
		return UMESH_L2_NBTABLE_ITEM_CHECK_UNREACHABLE_TIME_FAILED;
	}

	if (item->unreachable_time_msec >= UMESH_L2_NBTABLE_UNREACHABLE_TIME_MAX) {
		switch (item->state) {
			case UMESH_L2_NBTABLE_ITEM_STATE_EMPTY:
			case UMESH_L2_NBTABLE_ITEM_STATE_OLD:
				/* Do nothing. */
				break;

			case UMESH_L2_NBTABLE_ITEM_STATE_NEW:
			case UMESH_L2_NBTABLE_ITEM_STATE_VALID:
			case UMESH_L2_NBTABLE_ITEM_STATE_GUARD:
				umesh_l2_nbtable_item_set_state(item, UMESH_L2_NBTABLE_ITEM_STATE_OLD);
				break;

			default:
				return UMESH_L2_NBTABLE_ITEM_CHECK_UNREACHABLE_TIME_FAILED;
		}
		return UMESH_L2_NBTABLE_ITEM_CHECK_UNREACHABLE_TIME_ELAPSED;
	}

	return UMESH_L2_NBTABLE_ITEM_CHECK_UNREACHABLE_TIME_OK;
}


int32_t umesh_l2_nbtable_item_adjust_timeouts(struct umesh_l2_nbtable_item *item, uint32_t step) {
	if (u_assert(item != NULL)) {
		return UMESH_L2_NBTABLE_ITEM_ADJUST_TIMEOUTS_FAILED;
	}

	if (item->state != UMESH_L2_NBTABLE_ITEM_STATE_EMPTY) {
		if (item->state_timeout_msec >= step) {
			item->state_timeout_msec -= step;
		} else {
			item->state_timeout_msec = 0;
		}

		item->unreachable_time_msec += step;
	}

	return UMESH_L2_NBTABLE_ITEM_ADJUST_TIMEOUTS_OK;
}


int32_t umesh_l2_nbtable_item_loop_step(struct umesh_l2_nbtable *nbtable, struct umesh_l2_nbtable_item *item) {
	if (u_assert(item != NULL)) {
		return UMESH_L2_NBTABLE_ITEM_LOOP_STEP_FAILED;
	}

	switch (item->state) {
		case UMESH_L2_NBTABLE_ITEM_STATE_EMPTY:
		case UMESH_L2_NBTABLE_ITEM_STATE_GUARD:
		case UMESH_L2_NBTABLE_ITEM_STATE_VALID:
			/* Do nothing. */
			break;

		case UMESH_L2_NBTABLE_ITEM_STATE_NEW:

			/** @todo */
			umesh_l2_nbtable_item_set_state(item, UMESH_L2_NBTABLE_ITEM_STATE_VALID);
			if (nbtable->key_manager != NULL) {
				umesh_l2_keymgr_manage(nbtable->key_manager, item->tid);
			}
			//~ if (nbtable->ke) {
				//~ if (umesh_l3_ke_start(nbtable->ke, &(item->ke_context), item->tid) == UMESH_L3_KE_START_OK) {
					//~ umesh_l2_nbtable_item_set_state(item, UMESH_L2_NBTABLE_ITEM_STATE_KE_PENDING);
				//~ } else {
					//~ umesh_l2_nbtable_item_set_state(item, UMESH_L2_NBTABLE_ITEM_STATE_GUARD);
				//~ }
			//~ }

			break;

		case UMESH_L2_NBTABLE_ITEM_STATE_OLD:
			/* Change state to empty to remove this item. */
			memset(item, 0, sizeof(struct umesh_l2_nbtable_item));
			umesh_l2_nbtable_item_set_state(item, UMESH_L2_NBTABLE_ITEM_STATE_EMPTY);
			break;

		default:
			return UMESH_L2_NBTABLE_ITEM_LOOP_STEP_FAILED;
	}

	return UMESH_L2_NBTABLE_ITEM_LOOP_STEP_OK;
}


int32_t umesh_l2_nbtable_item_clear(struct umesh_l2_nbtable_item *item) {
	if (u_assert(item != NULL)) {
		return UMESH_L2_NBTABLE_ITEM_CLEAR_FAILED;
	}

	memset(item, 0, sizeof(struct umesh_l2_nbtable_item));

	return UMESH_L2_NBTABLE_ITEM_CLEAR_OK;
}


int32_t umesh_l2_nbtable_find(struct umesh_l2_nbtable *nbtable, uint32_t tid, struct umesh_l2_nbtable_item **item) {
	if (u_assert(item != NULL)) {
		return UMESH_L2_NBTABLE_FIND_FAILED;
	}

	for (uint32_t i = 0; i < nbtable->size; i++) {
		if (nbtable->nbtable[i].state != UMESH_L2_NBTABLE_ITEM_STATE_EMPTY &&
		    nbtable->nbtable[i].tid == tid) {
			*item = &(nbtable->nbtable[i]);
			return UMESH_L2_NBTABLE_FIND_OK;
		}
	}

	return UMESH_L2_NBTABLE_FIND_NOT_FOUND;
}


int32_t umesh_l2_nbtable_allocate(struct umesh_l2_nbtable *nbtable, struct umesh_l2_nbtable_item **item) {
	if (u_assert(item != NULL)) {
		return UMESH_L2_NBTABLE_ALLOCATE_FAILED;
	}

	for (uint32_t i = 0; i < nbtable->size; i++) {
		if (UMESH_L2_NBTABLE_ITEM_STATE_EMPTY == nbtable->nbtable[i].state) {
			*item = &(nbtable->nbtable[i]);
			return UMESH_L2_NBTABLE_ALLOCATE_OK;
		}
	}

	return UMESH_L2_NBTABLE_ALLOCATE_FULL;
}


bool umesh_l2_nbtable_item_is_reachable(struct umesh_l2_nbtable_item *item, bool crypto_required) {
	(void)crypto_required;
	if (u_assert(item != NULL)) {
		return false;
	}

	if (item->state != UMESH_L2_NBTABLE_ITEM_STATE_VALID) {
		return false;
	}

	if (item->unreachable_time_msec >= UMESH_L2_NBTABLE_UNREACHABLE_TIME_THRESHOLD) {
		return false;
	}

	/** @todo */
	//~ if (crypto_required && item->state != UMESH_L2_NBTABLE_ITEM_STATE_KE_OK && item->state != UMESH_L2_NBTABLE_ITEM_STATE_KE_REKEY) {
		//~ return false;
	//~ }

	return true;
}


int32_t umesh_l2_nbtable_update_led(struct umesh_l2_nbtable *nbtable) {
	if (u_assert(nbtable != NULL)) {
		return UMESH_L2_NBTABLE_UPDATE_LED_FAILED;
	}

	static const uint32_t count_status[5] = {0x0, 0xf1, 0xf111, 0xf11111, 0xf1111111};

	uint32_t count_auth = 0;
	for (uint32_t i = 0; i < nbtable->size; i++) {
		if (umesh_l2_nbtable_item_is_reachable(&(nbtable->nbtable[i]), true)) {
			count_auth++;
		}
	}
	if (count_auth > 4) {
		count_auth = 4;
	}
	//~ interface_led_loop(&(led_charge.iface), count_status[count_auth]);

	return UMESH_L2_NBTABLE_UPDATE_LED_OK;
}

