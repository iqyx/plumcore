/*
 * uMesh layer 2 key manager module
 *
 * Copyright (C) 2016, Marek Koza, qyx@krtko.org
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
#include "interface_rng.h"
#include "module_umesh.h"
#include "umesh_l2_send.h"
#include "umesh_l2_pbuf.h"

#include "pb_decode.h"
#include "pb_encode.h"
#include "umesh_l2_keymgr.pb.h"
#include "curve25519-donna.h"


#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "l2_keymgr"


/* String representation of the session states. */
const char *umesh_l2_keymgr_states[] = {
	"empty", "new", "initial_ake", "auth", "nauth", "autz", "nautz", "managed", "expired", "old"
};

const uint8_t test_isk[32] = {
	0xfc, 0x18, 0xf6, 0xe8, 0xa3, 0x37, 0x40, 0x0d, 0xdb, 0x0a, 0xe6, 0x7a, 0x2c, 0x5c, 0x61, 0xa1,
	0x1e, 0xcb, 0x7a, 0x9f, 0xdf, 0xf8, 0x20, 0x41, 0x08, 0x9c, 0x9c, 0xb1, 0x32, 0x34, 0x08, 0xc7
};


static void set_session_state(L2KeyManager *self, L2KeyManagerSession *session, enum umesh_l2_keymgr_session_state state);


/**********************************************************************************************************************
 * Internal helper functions
 **********************************************************************************************************************/


/* The session table is not locked, it is responsibility of the caller because the lock must be kept from allocating
 * the session until setting it as NEW. */
static L2KeyManagerSession *allocate_session(L2KeyManager *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->session_table != NULL) ||
	    u_assert(self->session_table_size > 0)) {
		return NULL;
	}

	for (size_t i = 0; i < self->session_table_size; i++) {
		if (self->session_table[i].state == UMESH_L2_KEYMGR_SESSION_STATE_EMPTY) {
			return &(self->session_table[i]);
		}
	}

	return NULL;
}


/**********************************************************************************************************************
 * Handle key manager protocols (send and dispatch messages from/to key manager submodules)
 **********************************************************************************************************************/


static int32_t umesh_l2_keymgr_receive_handler(struct umesh_l2_pbuf *pbuf, void *context) {
	if (u_assert(pbuf != NULL) ||
	    u_assert(context != NULL)) {
		return UMESH_L3_PROTOCOL_RECEIVE_FAILED;
	}
	L2KeyManager *self = (L2KeyManager *)context;

	/** @todo parse here */
	pb_istream_t stream;
        stream = pb_istream_from_buffer(pbuf->data, pbuf->len);

	L2KeyManagerMessage msg;
	if (!pb_decode(&stream, L2KeyManagerMessage_fields, &msg))
	{
		return UMESH_L3_PROTOCOL_RECEIVE_FAILED;
	}

	if (msg.which_content == L2KeyManagerMessage_ake_3dh_tag) {
		int32_t res = ake_3dh_receive_handler(&self->ake_3dh, &msg.content.ake_3dh);

		if (res == AKE_3DH_RECEIVE_HANDLER_NO_SESSION) {
			/* When an initial AKE protocol message is received and it cannot be matched to any existing
			 * session, a new session is created. When the AKE is successfully finished and the keymgr
			 * manages the new peer, the old session is deleted (even if it is not expired yet).
			 */

			/** @todo duplicated code */
			/* Check if no session with the same TID exists. No new session can be added if there is an existing one
			 * in one of the initial states. */
			/*
			for (size_t i = 0; i < self->session_table_size; i++) {
				L2KeyManagerSession *session = &(self->session_table[i]);
				if ((session->state == UMESH_L2_KEYMGR_SESSION_STATE_NEW ||
				     session->state == UMESH_L2_KEYMGR_SESSION_STATE_INITIAL_AKE ||
				     session->state == UMESH_L2_KEYMGR_SESSION_STATE_AUTH ||
				     session->state == UMESH_L2_KEYMGR_SESSION_STATE_AUTZ) &&
				    session->peer_tid == pbuf->stid)  {
					return UMESH_L2_KEYMGR_MANAGE_FAILED;
				}
			}
			*/

			/** @todo this behavior should be probably handled by a dedicated negotiation messages. */
			L2KeyManagerSession *session = allocate_session(self);
			if (session == NULL) {
				/* No new session can be allocated for this peer. */
				return UMESH_L3_PROTOCOL_RECEIVE_FAILED;
			}

			u_log(
				system_log,
				LOG_TYPE_INFO,
				U_LOG_MODULE_PREFIX("remote request for new session %p from %08x"),
				(void *)session,
				pbuf->stid
			);
			session->peer_tid = pbuf->stid;

			/* Set the state to INITIAL_AKE immediately and start an AKE session. */
			set_session_state(self, session, UMESH_L2_KEYMGR_SESSION_STATE_INITIAL_AKE);
			Ake3dhSession *ake_session = ake_3dh_start_session(
				&self->ake_3dh,
				self->test_isk,
				self->test_ipk,
				msg.content.ake_3dh.session_id.bytes,
				msg.content.ake_3dh.session_id.size
			);

			if (ake_session == NULL) {
				set_session_state(self, session, UMESH_L2_KEYMGR_SESSION_STATE_NAUTH);
				return UMESH_L3_PROTOCOL_RECEIVE_FAILED;
			}

			session->ake_3dh = ake_session;

			/* And process the message again. */
			res = ake_3dh_receive_handler(&self->ake_3dh, &msg.content.ake_3dh);
		}
		if (res != AKE_3DH_RECEIVE_HANDLER_OK) {
			return UMESH_L3_PROTOCOL_RECEIVE_FAILED;
		}
	}

	return UMESH_L3_PROTOCOL_RECEIVE_OK;
}


#define UMESH_L2_KEYMGR_SEND_OK 0
#define UMESH_L2_KEYMGR_SEND_FAILED -1
static int32_t umesh_l2_keymgr_send(L2KeyManager *self, L2KeyManagerMessage *msg, uint32_t peer_tid) {
	if (u_assert(self != NULL) ||
	    u_assert(msg != NULL)) {
		return UMESH_L2_KEYMGR_SEND_FAILED;
	}

	/* Prepare the layer 2 packet buffer first (default contents). */
	umesh_l2_pbuf_clear(&(self->pbuf));

	self->pbuf.dtid = peer_tid;
	self->pbuf.sec_type = UMESH_L2_PBUF_SECURITY_VERIFY;
	self->pbuf.proto = UMESH_L3_PROTOCOL_KEYMGR;

	/** @todo build the message */
	pb_ostream_t stream;
        stream = pb_ostream_from_buffer(self->pbuf.data, self->pbuf.size);

        if (!pb_encode(&stream, L2KeyManagerMessage_fields, msg)) {
		return UMESH_L2_KEYMGR_SEND_FAILED;
	}
	self->pbuf.len = stream.bytes_written;

	/** @todo Layer 2 send should be a dependency. This is wrong. */
	if (umesh_l2_send(&umesh, &(self->pbuf)) != UMESH_L2_SEND_OK) {
		return UMESH_L2_KEYMGR_SEND_FAILED;
	}

	return UMESH_L2_KEYMGR_SEND_OK;
}


static int32_t umesh_l2_keymgr_ake_3dh_send(const Ake3dhMessage *msg, Ake3dhSession *session, void *context) {

	L2KeyManager *self = (L2KeyManager *)context;

	L2KeyManagerMessage km_msg = L2KeyManagerMessage_init_zero;
	memcpy(&km_msg.content.ake_3dh, msg, sizeof(Ake3dhMessage));
	km_msg.which_content = L2KeyManagerMessage_ake_3dh_tag;

	/* Find the corresponding session. */
	L2KeyManagerSession *km_session = NULL;
	for (size_t i = 0; i < self->session_table_size; i++) {
		if (self->session_table[i].ake_3dh == session) {
			km_session = &(self->session_table[i]);
		}
	}
	if (km_session == NULL) {
		return AKE_3DH_SEND_CALLBACK_FAILED;
	}

	if (umesh_l2_keymgr_send(self, &km_msg, km_session->peer_tid) != UMESH_L2_KEYMGR_SEND_OK) {
		return AKE_3DH_SEND_CALLBACK_FAILED;
	}

	return AKE_3DH_SEND_CALLBACK_OK;
}


static int32_t umesh_l2_keymgr_ake_3dh_result_callback(Ake3dhSession *session, enum ake_3dh_result result, void *context) {
	L2KeyManager *self = (L2KeyManager *)context;

	/* Find the corresponding session. */
	L2KeyManagerSession *km_session = NULL;
	for (size_t i = 0; i < self->session_table_size; i++) {
		if (self->session_table[i].ake_3dh == session && self->session_table[i].state == UMESH_L2_KEYMGR_SESSION_STATE_INITIAL_AKE) {
			km_session = &(self->session_table[i]);
		}
	}
	if (km_session == NULL) {
		return AKE_3DH_SESSION_RESULT_CALLBACK_FAILED;
	}

	/* Set session state depending on the AKE result. */
	u_assert(result != AKE_3DH_RESULT_NONE);
	if (result == AKE_3DH_RESULT_OK) {
		memcpy(km_session->master_tx_key, session->master_tx_key, UMESH_L2_KEYMGR_MASTER_TX_KEY_SIZE);
		memcpy(km_session->master_rx_key, session->master_rx_key, UMESH_L2_KEYMGR_MASTER_RX_KEY_SIZE);
		set_session_state(self, km_session, UMESH_L2_KEYMGR_SESSION_STATE_AUTH);
	}
	if (result == AKE_3DH_RESULT_FAILED) {
		set_session_state(self, km_session, UMESH_L2_KEYMGR_SESSION_STATE_NAUTH);
	}
	/* Ignore other states and stop 3DH AKE protocol session. */
	ake_3dh_stop_session(&self->ake_3dh, session);

	return AKE_3DH_SESSION_RESULT_CALLBACK_OK;
}



/**********************************************************************************************************************
 * Key manager state machine
 **********************************************************************************************************************/


static void set_session_state(L2KeyManager *self, L2KeyManagerSession *session, enum umesh_l2_keymgr_session_state state) {
	u_assert(self != NULL);
	u_assert(session != NULL);

/*
	u_log(
		system_log,
		LOG_TYPE_DEBUG,
		U_LOG_MODULE_PREFIX("session %p: set state %s->%s"),
		(void *)session,
		umesh_l2_keymgr_states[session->state],
		umesh_l2_keymgr_states[state]
	);
*/
	session->state = state;

	/* Entering new state. */
	switch (session->state) {
		case UMESH_L2_KEYMGR_SESSION_STATE_EMPTY:
			/* Do nothing. */
			break;

		case UMESH_L2_KEYMGR_SESSION_STATE_NEW:
			session->state_timeout_ms = UMESH_L2_KEYMGR_TIMEOUT_NEW_MS;
			break;

		case UMESH_L2_KEYMGR_SESSION_STATE_INITIAL_AKE:
			session->ake_time = 0;
			session->state_timeout_ms = UMESH_L2_KEYMGR_TIMEOUT_INITIAL_AKE_MS;
			break;

		case UMESH_L2_KEYMGR_SESSION_STATE_AUTH:
			u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("ake_time auth %u"), session->ake_time);
			session->state_timeout_ms = UMESH_L2_KEYMGR_TIMEOUT_AUTH_MS;
			break;

		case UMESH_L2_KEYMGR_SESSION_STATE_NAUTH:
			u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("ake_time nauth %u"), session->ake_time);
			session->state_timeout_ms = UMESH_L2_KEYMGR_TIMEOUT_NAUTH_MS;
			break;

		case UMESH_L2_KEYMGR_SESSION_STATE_AUTZ:
			session->state_timeout_ms = UMESH_L2_KEYMGR_TIMEOUT_AUTZ_MS;
			break;

		case UMESH_L2_KEYMGR_SESSION_STATE_NAUTZ:
			session->state_timeout_ms = UMESH_L2_KEYMGR_TIMEOUT_NAUTZ_MS;
			break;

		case UMESH_L2_KEYMGR_SESSION_STATE_MANAGED:
			session->state_timeout_ms = UMESH_L2_KEYMGR_TIMEOUT_MANAGED_MS;
			break;

		case UMESH_L2_KEYMGR_SESSION_STATE_EXPIRED:
			session->state_timeout_ms = UMESH_L2_KEYMGR_TIMEOUT_EXPIRED_MS;
			break;

		case UMESH_L2_KEYMGR_SESSION_STATE_OLD:
			session->state_timeout_ms = UMESH_L2_KEYMGR_TIMEOUT_OLD_MS;
			break;

		default:
			break;
	}
}


static void check_session_state_timeout(L2KeyManager *self, L2KeyManagerSession *session) {
	u_assert(self != NULL);
	u_assert(session != NULL);

	if (session->state_timeout_ms == 0) {
		switch (session->state) {
			case UMESH_L2_KEYMGR_SESSION_STATE_EMPTY:
				/* Do nothing. */
				break;

			case UMESH_L2_KEYMGR_SESSION_STATE_NEW:
				set_session_state(self, session, UMESH_L2_KEYMGR_SESSION_STATE_OLD);
				break;

			case UMESH_L2_KEYMGR_SESSION_STATE_INITIAL_AKE:
				ake_3dh_stop_session(&self->ake_3dh, session->ake_3dh);
				set_session_state(self, session, UMESH_L2_KEYMGR_SESSION_STATE_NAUTH);
				break;

			case UMESH_L2_KEYMGR_SESSION_STATE_AUTH:
			case UMESH_L2_KEYMGR_SESSION_STATE_NAUTH:
			case UMESH_L2_KEYMGR_SESSION_STATE_AUTZ:
			case UMESH_L2_KEYMGR_SESSION_STATE_NAUTZ:
			case UMESH_L2_KEYMGR_SESSION_STATE_EXPIRED:
				set_session_state(self, session, UMESH_L2_KEYMGR_SESSION_STATE_OLD);
				break;

			case UMESH_L2_KEYMGR_SESSION_STATE_MANAGED:
				set_session_state(self, session, UMESH_L2_KEYMGR_SESSION_STATE_EXPIRED);
				break;

			case UMESH_L2_KEYMGR_SESSION_STATE_OLD:
/*
				u_log(
					system_log,
					LOG_TYPE_DEBUG,
					U_LOG_MODULE_PREFIX("session %p: removed by garbage collector"),
					(void *)session
				);
*/
				/* Clear the session data. */
				memset(session, 0, sizeof(L2KeyManagerSession));
				break;

			default:
				break;
		}
	}
}


static void session_loop_step(L2KeyManager *self, L2KeyManagerSession *session) {
	u_assert(self != NULL);
	u_assert(session != NULL);

	switch (session->state) {
		case UMESH_L2_KEYMGR_SESSION_STATE_EMPTY:
			/* Do nothing. */
			break;

		case UMESH_L2_KEYMGR_SESSION_STATE_NEW: {
			/* We are going to start the initial AKE now. */
			Ake3dhSession *ake_session = ake_3dh_start_session(
				&self->ake_3dh,
				self->test_isk,
				self->test_ipk,
				NULL,
				0
			);
			if (ake_session == NULL) {
				/* No session can be started at the moment, set not authenticated state and try later. */
				set_session_state(self, session, UMESH_L2_KEYMGR_SESSION_STATE_NAUTH);
			} else {
				session->ake_3dh = ake_session;
				set_session_state(self, session, UMESH_L2_KEYMGR_SESSION_STATE_INITIAL_AKE);
			}
			break;
		}

		case UMESH_L2_KEYMGR_SESSION_STATE_AUTH: {
			/** @todo for now, AUTH state is enough to get the peer keys managed. */
			set_session_state(self, session, UMESH_L2_KEYMGR_SESSION_STATE_MANAGED);
			break;
		}

		default:
			break;
	}
}


static void adjust_times(L2KeyManager *self, L2KeyManagerSession *session, uint32_t step) {
	u_assert(self != NULL);
	u_assert(session != NULL);

	if (session->state != UMESH_L2_KEYMGR_SESSION_STATE_EMPTY) {
		if (session->state_timeout_ms >= step) {
			session->state_timeout_ms -= step;
		} else {
			session->state_timeout_ms = 0;
		}
	}
}


static void check_and_increment_ake_time(L2KeyManager *self, L2KeyManagerSession *session, uint32_t step) {
	u_assert(self != NULL);
	u_assert(session != NULL);

	if (session->state == UMESH_L2_KEYMGR_SESSION_STATE_INITIAL_AKE) {
		session->ake_time += step;
	}
}


static void umesh_l2_keymgr_task(void *p) {

	L2KeyManager *self = (L2KeyManager *)p;

	u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("generating my identity test key pair"));
	const uint8_t basepoint[32] = CURVE25519_BASEPOINT;
	memcpy(self->test_isk, test_isk, 32);
	curve25519_donna(self->test_ipk, self->test_isk, basepoint);

	while (1) {
		for (size_t i = 0; i < self->session_table_size; i++) {
			check_session_state_timeout(self, &(self->session_table[i]));
			session_loop_step(self, &(self->session_table[i]));
			adjust_times(self, &(self->session_table[i]), UMESH_L2_KEYMGR_STEP_INTERVAL_MS);
			check_and_increment_ake_time(self, &(self->session_table[i]), UMESH_L2_KEYMGR_STEP_INTERVAL_MS);
		}
		vTaskDelay(UMESH_L2_KEYMGR_STEP_INTERVAL_MS);
	}

	vTaskDelete(NULL);
}


/**********************************************************************************************************************
 * Public API, Initialize and free the key manager module
 **********************************************************************************************************************/


int32_t umesh_l2_keymgr_init(L2KeyManager *self, uint32_t table_size) {
	if (u_assert(self != NULL) ||
	    u_assert(table_size > 0)) {
		return UMESH_L2_KEYMGR_INIT_FAILED;
	}

	/* Initialize the session table. */
	self->session_table = (L2KeyManagerSession *)malloc(sizeof(L2KeyManagerSession) * table_size);
	if (self->session_table == NULL) {
		return UMESH_L2_KEYMGR_INIT_NOMEM;
	}
	self->session_table_size = table_size;

	/* Initialze layer 3 protocol descriptor and set the receive handler. */
	self->protocol.name = "l2keymgr";
	self->protocol.context = (void *)self;
	self->protocol.receive_handler = umesh_l2_keymgr_receive_handler;

	/** @todo clear session table (the proper way) */
	memset(self->session_table, 0, sizeof(L2KeyManagerSession) * self->session_table_size);

	/* Initialize a single shared L2 packet buffer used to send protocol data. */
	umesh_l2_pbuf_init(&(self->pbuf));

	/* Initialize the main keymgr task. */
	xTaskCreate(umesh_l2_keymgr_task, MODULE_NAME, configMINIMAL_STACK_SIZE + 1024, (void *)self, 1, &(self->keymgr_task));
	if (self->keymgr_task == NULL) {
		return UMESH_L2_KEYMGR_INIT_FAILED;
	}

	/* Initialize used protocols. */
	/** @todo make max_sessions configurable */
	ake_3dh_init(&self->ake_3dh, 4);

	/* Set protocol callbacks. */
	self->ake_3dh.send_callback = umesh_l2_keymgr_ake_3dh_send;
	self->ake_3dh.send_context = (void *)self;
	self->ake_3dh.session_result_callback = umesh_l2_keymgr_ake_3dh_result_callback;
	self->ake_3dh.session_result_context = (void *)self;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("module initialized"));
	return UMESH_L2_KEYMGR_INIT_OK;
}


int32_t umesh_l2_keymgr_free(L2KeyManager *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->session_table != NULL)) {
		return UMESH_L2_KEYMGR_FREE_FAILED;
	}

	ake_3dh_free(&self->ake_3dh);

	if (self->session_table != NULL) {
		free(self->session_table);
	}

	if (self->keymgr_task != NULL) {
		vTaskDelete(self->keymgr_task);
	}

	umesh_l2_pbuf_free(&(self->pbuf));

	return UMESH_L2_KEYMGR_FREE_OK;
}


int32_t umesh_l2_keymgr_set_rng(L2KeyManager *self, struct interface_rng *rng) {
	if (u_assert(self != NULL) ||
	    u_assert(rng != NULL)) {
		return UMESH_L2_KEYMGR_SET_RNG_FAILED;
	}

	self->rng = rng;
	ake_3dh_set_rng(&self->ake_3dh, self->rng);

	return UMESH_L2_KEYMGR_SET_RNG_FAILED;
}


int32_t umesh_l2_keymgr_manage(L2KeyManager *self, uint32_t tid) {
	if (u_assert(self != NULL)) {
		return UMESH_L2_KEYMGR_MANAGE_FAILED;
	}

	/* Check if no session with the same TID exists. No new session can be added if there is an existing one
	 * in one of the initial states. */
	/*
	for (size_t i = 0; i < self->session_table_size; i++) {
		L2KeyManagerSession *session = &(self->session_table[i]);
		if ((session->state == UMESH_L2_KEYMGR_SESSION_STATE_NEW ||
		     session->state == UMESH_L2_KEYMGR_SESSION_STATE_INITIAL_AKE ||
		     session->state == UMESH_L2_KEYMGR_SESSION_STATE_AUTH ||
		     session->state == UMESH_L2_KEYMGR_SESSION_STATE_AUTZ) &&
		    session->peer_tid == tid)  {
			return UMESH_L2_KEYMGR_MANAGE_FAILED;
		}
	}
	*/

	/** @todo lock here */
	L2KeyManagerSession *session = allocate_session(self);
	if (session == NULL) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("cannot manage neighbor %08x, session allocation failed"), tid);
		return UMESH_L2_KEYMGR_MANAGE_FAILED;
	}

	memset(session, 0, sizeof(L2KeyManagerSession));
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("managing neighbor %08x, session %p"), tid, (void *)session);
	session->peer_tid = tid;
	set_session_state(self, session, UMESH_L2_KEYMGR_SESSION_STATE_NEW);

	return UMESH_L2_KEYMGR_MANAGE_OK;
}


L2KeyManagerSession *umesh_l2_keymgr_find_session(L2KeyManager *self, uint32_t peer_tid) {
	if (u_assert(self != NULL)) {
		return NULL;
	}

	/** @todo better searching algorithm. This function is used while sending packets and could be
	 *        unoptimal with multiple sessions. */
	L2KeyManagerSession *best_result = NULL;
	uint32_t valid_time_left = 0;
	for (size_t i = 0; i < self->session_table_size; i++) {
		if (self->session_table[i].peer_tid == peer_tid &&
		    self->session_table[i].state == UMESH_L2_KEYMGR_SESSION_STATE_MANAGED &&
		    self->session_table[i].state_timeout_ms > valid_time_left) {
			best_result = &(self->session_table[i]);
			valid_time_left = self->session_table[i].state_timeout_ms;
		}
	}

	return best_result;
}
