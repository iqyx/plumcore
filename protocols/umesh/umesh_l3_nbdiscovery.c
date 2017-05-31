/**
 * uMeshFw uMesh layer 3 neightbor discovery protocol
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
#include "u_assert.h"
#include "u_log.h"
#include "port.h"
#include "hal_module.h"
#include "module_umesh.h"
#include "umesh_l2_nbtable.h"
#include "umesh_l2_pbuf.h"
#include "umesh_l2_send.h"
#include "umesh_l3_nbdiscovery.h"
#include "umesh_l3_protocol.h"


/******************************************************************************
 * Sending chain
 ******************************************************************************/

/**
 * The main neighbor discovery loop that schedules sending of different
 * protocol messages at their configured intervals.
 */
static void umesh_l3_nbdiscovery_task(void *p) {
	struct umesh_l3_nbdiscovery *nbd = (struct umesh_l3_nbdiscovery *)p;
	(void)nbd;

	umesh_l2_pbuf_init(&(nbd->pbuf));

	while (1) {
		if (nbd->last_adv_basic_ms >= UMESH_L3_NBDISCOVERY_ADV_BASIC_INTERVAL) {
			umesh_l3_nbdiscovery_send_adv_basic(nbd);
			nbd->last_adv_basic_ms = 0;
		}

		if (nbd->tid_current == 0) {
			/* If a new TID is requested (current tid == 0), try to
			 * get one. */
			uint16_t tid = 0;
			if (nbd->rng != NULL) {
				interface_rng_read(nbd->rng, (uint8_t *)&tid, sizeof(tid));
			}
			/** @todo remove, testing only */
			tid = 0xc206;
			if (tid != 0) {
				//~ u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE "nbdiscovery: new TID generated %08x", "umesh", (unsigned int)tid);
				nbd->tid_current = tid;
				nbd->tid_age_ms = 0;
				/* Keep the last tid intact. */
			}
		} else {
			/* If the current tid timeouted, a new one is requested.
			 * It is done by moving the current tid to the last tid
			 * and clearing it. */
			if (nbd->tid_age_ms >= UMESH_L3_NBDISCOVERY_VALID_TID_AGE_MAX) {
				nbd->tid_last = nbd->tid_current;
				nbd->tid_current = 0;
			}
		}

		/* Advance in time. */
		nbd->last_adv_basic_ms += UMESH_L3_NBDISCOVERY_STEP_INTERVAL;
		nbd->tid_age_ms += UMESH_L3_NBDISCOVERY_STEP_INTERVAL;
		vTaskDelay(UMESH_L3_NBDISCOVERY_STEP_INTERVAL);
	}

	umesh_l2_pbuf_free(&(nbd->pbuf));
	vTaskDelete(NULL);
}


int32_t umesh_l3_nbdiscovery_send(struct umesh_l3_nbdiscovery *nbd, struct umesh_l3_nbdiscovery_msg *msg) {
	if (u_assert(nbd != NULL && msg != NULL)) {
		return UMESH_L3_NBDISCOVERY_SEND_FAILED;
	}

	/* Prepare the layer 2 packet buffer first (default contents). */
	umesh_l2_pbuf_clear(&(nbd->pbuf));

	/* Build the message in the generic build function. */
	if (umesh_l3_nbdiscovery_build(nbd, msg, &(nbd->pbuf)) != UMESH_L3_NBDISCOVERY_BUILD_OK) {
		return UMESH_L3_NBDISCOVERY_SEND_FAILED;
	}

	/** @todo Layer 2 send should be a dependency. This is wrong. */
	if (umesh_l2_send(&umesh, &(nbd->pbuf)) != UMESH_L2_SEND_OK) {
		return UMESH_L3_NBDISCOVERY_SEND_FAILED;
	}

	return UMESH_L3_NBDISCOVERY_SEND_OK;
}


int32_t umesh_l3_nbdiscovery_build(struct umesh_l3_nbdiscovery *nbd, struct umesh_l3_nbdiscovery_msg *msg, struct umesh_l2_pbuf *pbuf) {
	if (u_assert(nbd != NULL && msg != NULL && pbuf != NULL)) {
		return UMESH_L3_NBDISCOVERY_BUILD_FAILED;
	}

	pbuf->sec_type = UMESH_L2_PBUF_SECURITY_VERIFY;
	pbuf->flags |= UMESH_L2_PBUF_FLAG_BROADCAST;
	pbuf->proto = UMESH_L3_PROTOCOL_NBDISCOVERY;

	size_t remaining = pbuf->size;
	uint8_t *data = pbuf->data;

	/* Write the packet header. Check if there is enough space. */
	if (remaining < 1) {
		return UMESH_L3_NBDISCOVERY_BUILD_FAILED;
	}
	*data = msg->type;
	data++;
	remaining--;

	switch (msg->type) {
		case UMESH_L3_NBDISCOVERY_PACKET_TID_ADV_BASIC:
			pbuf->stid = msg->data.adv_basic.tid;
			break;

		default:
			/* Unknown or unsupported packet type. */
			return UMESH_L3_NBDISCOVERY_BUILD_FAILED;
	}

	pbuf->len = (data - nbd->pbuf.data);

	return UMESH_L3_NBDISCOVERY_BUILD_OK;
}

int32_t umesh_l3_nbdiscovery_send_adv_basic(struct umesh_l3_nbdiscovery *nbd) {
	if (u_assert(nbd != NULL)) {
		return UMESH_L3_NBDISCOVERY_SEND_ADV_BASIC_FAILED;
	}

	/* Do not check the return value. If we are unable to send the packet,
	 * we handle this situation silently. */
	umesh_l3_nbdiscovery_send(nbd, &(struct umesh_l3_nbdiscovery_msg) {
		.type = UMESH_L3_NBDISCOVERY_PACKET_TID_ADV_BASIC,
		.data.adv_basic = {
			.tid = nbd->tid_current
		}
	});

	return UMESH_L3_NBDISCOVERY_SEND_ADV_BASIC_OK;
}


/******************************************************************************
 * Receiving chain
 ******************************************************************************/

/**
 * Registered as a layer 3 protocol receive handler in the protocol descriptor
 * (registration is done in the init function when the context is initialized).
 */
static int32_t umesh_l3_nbdiscovery_receive_handler(struct umesh_l2_pbuf *pbuf, void *context) {
	if (u_assert(pbuf != NULL && context != NULL)) {
		return UMESH_L3_PROTOCOL_RECEIVE_FAILED;
	}
	struct umesh_l3_nbdiscovery *nbd = (struct umesh_l3_nbdiscovery *)context;

	if (umesh_l3_nbdiscovery_receive_check(nbd, pbuf) != UMESH_L3_NBDISCOVERY_RECEIVE_CHECK_OK) {
		return UMESH_L3_PROTOCOL_RECEIVE_FAILED;
	}

	/* Prepare a new protocol message structure first. */
	struct umesh_l3_nbdiscovery_msg msg;
	memset(&msg, 0, sizeof(struct umesh_l3_nbdiscovery_msg));

	/* Parse the received packet. */
	if (umesh_l3_nbdiscovery_parse(nbd, pbuf, &msg) != UMESH_L3_NBDISCOVERY_PARSE_OK) {
		return UMESH_L3_PROTOCOL_RECEIVE_FAILED;
	}

	if (umesh_l3_nbdiscovery_process(nbd, &msg) != UMESH_L3_NBDISCOVERY_PROCESS_OK) {

		return UMESH_L3_PROTOCOL_RECEIVE_FAILED;
	}

	//~ u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE "nbdiscovery: receive_handler, stid=%04x", "umesh", pbuf->stid);

	return UMESH_L3_PROTOCOL_RECEIVE_OK;
}


int32_t umesh_l3_nbdiscovery_receive_check(struct umesh_l3_nbdiscovery *nbd, struct umesh_l2_pbuf *pbuf) {
	if (u_assert(nbd != NULL && pbuf != NULL)) {
		return UMESH_L3_NBDISCOVERY_RECEIVE_CHECK_FAILED;
	}

	/* Drop nbdiscovery packets with no error checking. */
	if (pbuf->sec_type != UMESH_L2_PBUF_SECURITY_VERIFY) {
		return UMESH_L3_NBDISCOVERY_RECEIVE_CHECK_FAILED;
	}

	/** @todo drop packets with unfeasible lqi/ber */

	return UMESH_L3_NBDISCOVERY_RECEIVE_CHECK_OK;
}


int32_t umesh_l3_nbdiscovery_parse(struct umesh_l3_nbdiscovery *nbd, struct umesh_l2_pbuf *pbuf, struct umesh_l3_nbdiscovery_msg *msg) {
	if (u_assert(nbd != NULL && pbuf != NULL && msg != NULL)) {
		return UMESH_L3_NBDISCOVERY_PARSE_FAILED;
	}

	/* Make a copy of the packet length and data - we will be incrementing
	 * the position and decrementing the lenght. */
	size_t len = pbuf->len;
	uint8_t *data = pbuf->data;

	/* We are trying to read the header (packet type). */
	if (len < 1) {
		return UMESH_L3_NBDISCOVERY_PARSE_FAILED;
	}
	msg->type = *data;
	data++;
	len--;

	switch (msg->type) {
		case UMESH_L3_NBDISCOVERY_PACKET_TID_ADV_BASIC:
			msg->data.adv_basic.fei_hz = pbuf->fei;
			msg->data.adv_basic.rssi_10dBm = pbuf->rssi;
			msg->data.adv_basic.tid = pbuf->stid;
			break;

		default:
			/* Unknown or unsupported packet header. */
			return UMESH_L3_NBDISCOVERY_PARSE_FAILED;
	}

	/* If there is some redundant data at the end, something probably
	 * went wrong. */
	if (len > 0) {
		return UMESH_L3_NBDISCOVERY_PARSE_FAILED;
	}

	return UMESH_L3_NBDISCOVERY_PARSE_OK;
}


int32_t umesh_l3_nbdiscovery_process(struct umesh_l3_nbdiscovery *nbd, struct umesh_l3_nbdiscovery_msg *msg) {
	if (u_assert(nbd != NULL && msg != NULL)) {
		return UMESH_L3_NBDISCOVERY_PROCESS_FAILED;
	}

	switch (msg->type) {
		case UMESH_L3_NBDISCOVERY_PACKET_TID_ADV_BASIC:
			if (umesh_l3_nbdiscovery_process_adv_basic(nbd, msg) != UMESH_L3_NBDISCOVERY_PROCESS_ADV_BASIC_OK) {
				return UMESH_L3_NBDISCOVERY_PROCESS_FAILED;
			}
			break;

		default:
			/* Cannot handle this packet type. */
			return UMESH_L3_NBDISCOVERY_PROCESS_FAILED;
	}

	return UMESH_L3_NBDISCOVERY_PROCESS_OK;
}


int32_t umesh_l3_nbdiscovery_process_adv_basic(struct umesh_l3_nbdiscovery *nbd, struct umesh_l3_nbdiscovery_msg *msg) {
	if (u_assert(nbd != NULL && msg != NULL)) {
		return UMESH_L3_NBDISCOVERY_PROCESS_ADV_BASIC_FAILED;
	}

	struct umesh_l2_nbtable_item *item = NULL;

	/** @todo move to nbtable_refresh */
	/* Try to find an existing nbtable item first. */
	if (umesh_l2_nbtable_find(nbd->nbtable, msg->data.adv_basic.tid, &item) == UMESH_L2_NBTABLE_FIND_OK) {

		item->fei_hz = msg->data.adv_basic.fei_hz;
		item->rssi_10dBm = msg->data.adv_basic.rssi_10dBm;
		item->unreachable_time_msec = 0;


	} else {
		/* If nothing was found, try to allocate a new one. */
		if (umesh_l2_nbtable_allocate(nbd->nbtable, &item) != UMESH_L2_NBTABLE_ALLOCATE_OK) {
			/* If allocation isn't successfull,
			 * probably the nbtable is full. */
			return UMESH_L3_NBDISCOVERY_PROCESS_ADV_BASIC_FAILED;
		}

		item->tid = msg->data.adv_basic.tid;
		item->fei_hz = msg->data.adv_basic.fei_hz;
		item->rssi_10dBm = msg->data.adv_basic.rssi_10dBm;
		item->unreachable_time_msec = 0;

		umesh_l2_nbtable_item_set_state(item, UMESH_L2_NBTABLE_ITEM_STATE_NEW);
	}

	return UMESH_L3_NBDISCOVERY_PROCESS_ADV_BASIC_OK;
}


/******************************************************************************
 * Layer 3 neighbor discovery protocol context manipulation
 ******************************************************************************/

int32_t umesh_l3_nbdiscovery_init(struct umesh_l3_nbdiscovery *nbd, struct umesh_l2_nbtable *nbtable) {
	if (u_assert(nbd != NULL && nbtable != NULL)) {
		return UMESH_L3_NBDISCOVERY_INIT_FAILED;
	}

	memset(nbd, 0, sizeof(struct umesh_l3_nbdiscovery));
	nbd->nbtable = nbtable;

	/* Initialze layer 3 protocol descriptor and set the receive handler. */
	nbd->protocol.name = "nbdiscovery";
	nbd->protocol.context = (void *)nbd;
	nbd->protocol.receive_handler = umesh_l3_nbdiscovery_receive_handler;

	xTaskCreate(umesh_l3_nbdiscovery_task, "umesh_nbdisc", configMINIMAL_STACK_SIZE + 256, (void *)nbd, 1, &(nbd->beacon_task));
	if (nbd->beacon_task == NULL) {
		goto err;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE "neighbor discovery initialized", "umesh");
	return UMESH_L3_NBDISCOVERY_INIT_OK;
err:
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE "neighbor discovery initialization failed", "umesh");
	return UMESH_L3_NBDISCOVERY_INIT_FAILED;
}


int32_t umesh_l3_nbdiscovery_free(struct umesh_l3_nbdiscovery *nbd) {
	if (u_assert(nbd != NULL)) {
		return UMESH_L3_NBDISCOVERY_FREE_FAILED;
	}

	/* Do nothing. */

	return UMESH_L3_NBDISCOVERY_FREE_OK;
}


int32_t umesh_l3_nbdiscovery_set_rng(struct umesh_l3_nbdiscovery *nbd, struct interface_rng *rng) {
	if (u_assert(nbd != NULL && rng != NULL)) {
		return UMESH_L3_NBDISCOVERY_SET_RNG_FAILED;
	}

	nbd->rng = rng;

	return UMESH_L3_NBDISCOVERY_SET_RNG_OK;
}


