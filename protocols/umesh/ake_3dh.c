/*
 * 3DH authenticated key exchange protocol
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

#include "ake_3dh.h"
#include "ake_3dh.pb.h"
#include "curve25519-donna.h"
#include "sha2.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "ake-3dh"


static void ake_3dh_set_result(Ake3dh *self, Ake3dhSession *session, enum ake_3dh_result result) {
	if (u_assert(self != NULL) ||
	    u_assert(session != NULL) ||
	    u_assert(result != AKE_3DH_RESULT_NONE)) {
		return;
	}

	/* Return if the result is already set. */
	if (session->result != AKE_3DH_RESULT_NONE) {
		return;
	}

	if (self->debug & AKE_3DH_DEBUG_LOGS) {
		u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("session %p: result %u"), (void *)session, result);
	}

	/* Set the result and call the result callback function. */
	session->result = result;
	if (self->session_result_callback != NULL) {
		self->session_result_callback(session, result, self->session_result_context);
	}
}


static void ake_3dh_advance_timers(Ake3dh *self, Ake3dhSession *session, uint32_t step_time_ms) {
	(void)self;
	if (u_assert(self != NULL) ||
	    u_assert(session != NULL)) {
		return;
	}

	session->peer_epk_request_time += step_time_ms;
	session->peer_ipk_request_time += step_time_ms;
}


static void ake_3dh_generate_session_id(Ake3dh *self, Ake3dhSession *session) {
	if (self->debug & AKE_3DH_DEBUG_LOGS) {
		u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("session %p: generating session_id"), (void *)session);
	}
	interface_rng_read(self->rng, session->session_id, AKE_3DH_SESSION_SIZE);
	session->session_id_generated = true;
}


static void ake_3dh_generate_epk(Ake3dh *self, Ake3dhSession *session) {
	if (self->debug & AKE_3DH_DEBUG_LOGS) {
		u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("session %p: generating ephemeral secure key"), (void *)session);
	}
	interface_rng_read(self->rng, session->my_esk, AKE_3DH_ESK_SIZE);
	session->my_esk_generated = true;
}


static void ake_3dh_compute_epk(Ake3dh *self, Ake3dhSession *session) {
	/** @todo move to dedicated function */
	if (self->debug & AKE_3DH_DEBUG_LOGS) {
		u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("session %p: computing ephemeral public key"), (void *)session);
	}
	const uint8_t basepoint[32] = CURVE25519_BASEPOINT;
	curve25519_donna(session->my_epk, session->my_esk, basepoint);
	session->my_epk_calculated = true;
}


static void ake_3dh_determine_role(Ake3dh *self, Ake3dhSession *session) {
	(void)self;

	int res = memcmp(session->my_epk, session->peer_epk, AKE_3DH_EPK_SIZE);
	if (res == 0) {
		/* Public keys are equal -> error. */
		/** @todo we are Alice temporarily */
		session->who_i_am = AKE_3DH_ALICE;
	} else if (res < 0) {
		session->who_i_am = AKE_3DH_ALICE;
	} else {
		session->who_i_am = AKE_3DH_BOB;
	}
}


static void ake_3dh_compute_sh1(Ake3dh *self, Ake3dhSession *session) {
	if (self->debug & AKE_3DH_DEBUG_LOGS) {
		u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("session %p: computing shared key SH1"), (void *)session);
	}
	curve25519_donna(session->sh1, session->my_esk, session->peer_epk);
	session->sh1_computed = true;
}


static void ake_3dh_compute_sh2_alice(Ake3dh *self, Ake3dhSession *session) {
	/* compute sh2 = scalarmult(my_isk, peer_epk) */
	if (self->debug & AKE_3DH_DEBUG_LOGS) {
		u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("session %p: computing shared key SH2"), (void *)session);
	}
	curve25519_donna(session->sh2, session->my_isk, session->peer_epk);
	session->sh2_computed = true;
}


static void ake_3dh_compute_sh2_bob(Ake3dh *self, Ake3dhSession *session) {
	/* compute sh2 = scalarmult(my_esk, peer_ipk) */
	if (self->debug & AKE_3DH_DEBUG_LOGS) {
		u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("session %p: computing shared key SH2"), (void *)session);
	}
	curve25519_donna(session->sh2, session->my_esk, session->peer_ipk);
	session->sh2_computed = true;
}


static void ake_3dh_compute_sh3_alice(Ake3dh *self, Ake3dhSession *session) {
	/* compute sh3 = scalarmult(my_esk, peer_ipk) */
	if (self->debug & AKE_3DH_DEBUG_LOGS) {
		u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("session %p: computing shared key SH3"), (void *)session);
	}
	curve25519_donna(session->sh3, session->my_esk, session->peer_ipk);
	session->sh3_computed = true;
}


static void ake_3dh_compute_sh3_bob(Ake3dh *self, Ake3dhSession *session) {
	/* compute sh3 = scalarmult(my_isk, peer_epk) */
	if (self->debug & AKE_3DH_DEBUG_LOGS) {
		u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("session %p: computing shared key SH3"), (void *)session);
	}
	curve25519_donna(session->sh3, session->my_isk, session->peer_epk);
	session->sh3_computed = true;
}


static void ake_3dh_compute_master_key(Ake3dh *self, Ake3dhSession *session) {
	if (self->debug & AKE_3DH_DEBUG_LOGS) {
		u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("session %p: computing master keys"), (void *)session);
	}

	sha256_ctx ctx;
	sha256_init(&ctx);
	sha256_update(&ctx, session->sh1, AKE_3DH_SH_SIZE);
	sha256_update(&ctx, session->sh2, AKE_3DH_SH_SIZE);
	sha256_update(&ctx, session->sh3, AKE_3DH_SH_SIZE);
	sha256_final(&ctx, session->master_key);
	session->master_key_computed = true;
}


static void ake_3dh_compute_tx_rx_keys(Ake3dh *self, Ake3dhSession *session) {
	if (self->debug & AKE_3DH_DEBUG_LOGS) {
		u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("session %p: computing TX/RX keys"), (void *)session);
	}

	sha256_ctx ctx;
	sha256_init(&ctx);
	sha256_update(&ctx, session->master_key, AKE_3DH_MASTER_KEY_SIZE);
	sha256_update(&ctx, (uint8_t *)"first", 5);
	if (session->who_i_am == AKE_3DH_ALICE) {
		sha256_final(&ctx, session->master_tx_key);
	} else {
		sha256_final(&ctx, session->master_rx_key);
	}

	sha256_init(&ctx);
	sha256_update(&ctx, session->master_key, AKE_3DH_MASTER_KEY_SIZE);
	sha256_update(&ctx, (uint8_t *)"second", 6);
	if (session->who_i_am == AKE_3DH_ALICE) {
		sha256_final(&ctx, session->master_rx_key); /* tx/rx exchanged! */
	} else {
		sha256_final(&ctx, session->master_tx_key); /* tx/rx exchanged! */
	}

	session->tx_rx_keys_computed = true;
}


static void ake_3dh_session_step(Ake3dh *self, Ake3dhSession *session, uint32_t step_time_ms) {
	(void)step_time_ms;
	if (u_assert(self != NULL) ||
	    u_assert(session != NULL)) {
		return;
	}

	if (session->session_id_generated == false &&
	    self->rng != NULL) {
		ake_3dh_generate_session_id(self, session);
		return;
	}
	if (session->my_esk_generated == false &&
	    self->rng != NULL) {
		ake_3dh_generate_epk(self, session);
		return;
	}
	if (session->my_epk_calculated == false &&
	    session->my_esk_generated == true) {
		ake_3dh_compute_epk(self, session);
		return;
	}
	if (session->peer_epk_received == false) {
		ake_3dh_send_epk_request(self, session);
		/* No return here. */
	}
	if (session->peer_ipk_received == false) {
		ake_3dh_send_ipk_request(self, session);
		/* No return here. */
	}
	if (session->my_epk_requested == true) {
		ake_3dh_send_epk_response(self, session);
		/* No return here. */
	}
	if (session->my_ipk_requested == true) {
		ake_3dh_send_ipk_response(self, session);
		/* No return here. */
	}
	if (session->my_epk_calculated &&
	    session->peer_epk_received &&
	    session->sh1_computed == false) {
		ake_3dh_determine_role(self, session);
		ake_3dh_compute_sh1(self, session);
		return;
	}
	if (session->who_i_am == AKE_3DH_ALICE &&
	    session->peer_epk_received &&
	    session->sh2_computed == false) {
		ake_3dh_compute_sh2_alice(self, session);
		return;
	}
	if (session->who_i_am == AKE_3DH_BOB &&
	    session->my_esk_generated &&
	    session->peer_ipk_received &&
	    session->sh2_computed == false) {
		ake_3dh_compute_sh2_bob(self, session);
		return;
	}
	if (session->who_i_am == AKE_3DH_ALICE &&
	    session->my_esk_generated &&
	    session->peer_ipk_received &&
	    session->sh3_computed == false) {
		ake_3dh_compute_sh3_alice(self, session);
		return;
	}
	if (session->who_i_am == AKE_3DH_BOB &&
	    session->peer_epk_received &&
	    session->sh3_computed == false) {
		ake_3dh_compute_sh3_bob(self, session);
		return;
	}
	if (session->who_i_am != AKE_3DH_ROLE_UNKNOWN &&
	    session->sh1_computed &&
	    session->sh2_computed &&
	    session->sh3_computed &&
	    session->master_key_computed == false) {
		ake_3dh_compute_master_key(self, session);
		return;
	}
	if (session->master_key_computed &&
	    session->tx_rx_keys_computed == false) {
		ake_3dh_compute_tx_rx_keys(self, session);
		ake_3dh_set_result(self, session, AKE_3DH_RESULT_OK);
		/* Now the master is allowed to clear the session, we cannot do anything more. */
		return;
	}
}


static void ake_3dh_task(void *p) {
	Ake3dh *self = (Ake3dh *)p;

	while (1) {
		for (size_t i = 0; i < self->session_table_size; i++) {
			if (self->session_table[i].used) {
				ake_3dh_session_step(self, &(self->session_table[i]), AKE_3DH_STEP_INTERVAL_MS);
				ake_3dh_advance_timers(self, &(self->session_table[i]), AKE_3DH_STEP_INTERVAL_MS);
			}
		}
		vTaskDelay(AKE_3DH_STEP_INTERVAL_MS);
	}

	vTaskDelete(NULL);
}


int32_t ake_3dh_init(Ake3dh *self, uint32_t max_sessions) {
	if (u_assert(self != NULL) ||
	    u_assert(max_sessions > 0)) {
		return AKE_3DH_INIT_FAILED;
	}

	/* Initialize the session table. */
	self->session_table = (Ake3dhSession *)malloc(sizeof(Ake3dhSession) * max_sessions);
	if (self->session_table == NULL) {
		return AKE_3DH_INIT_NOMEM;
	}
	self->session_table_size = max_sessions;
	memset(self->session_table, 0, sizeof(Ake3dhSession) * self->session_table_size);

	/* Initialize the main protocol task. */
	xTaskCreate(ake_3dh_task, MODULE_NAME, configMINIMAL_STACK_SIZE + 1024, (void *)self, 1, &(self->main_task));
	if (self->main_task == NULL) {
		return AKE_3DH_INIT_FAILED;
	}

	/* Enable/disable debugging features. */
	//~ self->debug = AKE_3DH_DEBUG_LOGS;

	return AKE_3DH_INIT_OK;
}


int32_t ake_3dh_free(Ake3dh *self) {
	if (u_assert(self != NULL)) {
		return AKE_3DH_FREE_FAILED;
	}

	if (self->session_table != NULL) {
		free(self->session_table);
	}

	if (self->main_task != NULL) {
		vTaskDelete(self->main_task);
	}

	return AKE_3DH_FREE_OK;
}


int32_t ake_3dh_set_rng(Ake3dh *self, struct interface_rng *rng) {
	if (u_assert(self != NULL) ||
	    u_assert(rng != NULL)) {
		return AKE_3DH_SET_RNG_FAILED;
	}

	self->rng = rng;

	return AKE_3DH_SET_RNG_OK;
}


Ake3dhSession *ake_3dh_start_session(Ake3dh *self, uint8_t *my_isk, uint8_t *my_ipk, uint8_t *session_id, size_t session_id_len) {
	if (u_assert(self != NULL) ||
	    u_assert(my_isk != NULL) ||
	    u_assert(my_ipk != NULL)) {
		return NULL;
	}

	/** @todo lock session table */
	/* Find new empty session. */
	Ake3dhSession *session = NULL;
	for (size_t i = 0; i < self->session_table_size; i++) {
		if (self->session_table[i].used == false) {
			session = &(self->session_table[i]);
		}
	}
	if (session == NULL) {
		if (self->debug & AKE_3DH_DEBUG_WARNINGS) {
			u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("cannot start new session, table full"));
		}
		return NULL;
	}

	/* Clear all fields. */
	memset(session, 0, sizeof(Ake3dhSession));

	session->used = true;
	memcpy(session->my_isk, my_isk, AKE_3DH_ISK_SIZE);
	memcpy(session->my_ipk, my_ipk, AKE_3DH_IPK_SIZE);

	/** @todo better manage session ID lengths or make them fixed */
	if (session_id != NULL) {
		if (session_id_len > AKE_3DH_SESSION_SIZE) {
			session_id_len = AKE_3DH_SESSION_SIZE;
		}
		memcpy(session->session_id, session_id, session_id_len);
		session->session_id_generated = true;
	}

	/* I am Alice by default. */
	session->who_i_am = AKE_3DH_ALICE;

	if (self->debug & AKE_3DH_DEBUG_LOGS) {
		u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("starting session %p"), session);
	}
	/** @todo unlock */

	return session;
}


void ake_3dh_stop_session(Ake3dh *self, Ake3dhSession *session) {
	(void)self;

	/* Clear all fields (sets used to false). */
	memset(session, 0, sizeof(Ake3dhSession));

	if (self->debug & AKE_3DH_DEBUG_LOGS) {
		u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("session %p stopped"), session);
	}
}


/** @todo */
size_t ake_3dh_protocol_status(Ake3dh *self, Ake3dhSession *session) {
	if (u_assert(self != NULL) ||
	    u_assert(session != NULL)) {
		return 0;
	}

	size_t percent = 0;
	percent += session->my_esk_generated ? 10 : 0;
	percent += session->my_epk_calculated ? 10 : 0;
	percent += session->my_epk_requested ? 20 : 0;
	percent += session->peer_epk_received ? 20 : 0;
	percent += session->my_ipk_requested ? 20 : 0;
	percent += session->peer_ipk_received ? 20 : 0;

	return percent;
}


int32_t ake_3dh_receive_handler(Ake3dh *self, const Ake3dhMessage *msg) {
	if (u_assert(self != NULL) ||
	    u_assert(msg != NULL)) {
		return AKE_3DH_RECEIVE_HANDLER_FAILED;
	}

	/* Get the session ID and find the corresponding session context. */
	Ake3dhSession *session = NULL;
	for (size_t i = 0; i < self->session_table_size; i++) {
		if (!memcmp(msg->session_id.bytes, self->session_table[i].session_id, msg->session_id.size)) {
			session = &(self->session_table[i]);
		}
	}
	if (session == NULL) {
		/* Couldn't match any existing session. */
		return AKE_3DH_RECEIVE_HANDLER_NO_SESSION;
	}

	/*
	 * This code is only allowed to modify the session context. It cannot directly trigger message sending
	 * or any other processes. If a response needs to be sent, a flag indicating that is set and the response
	 * is sent in the main loop instead (properly rate limited).
	 */
	switch (msg->which_content) {
		case Ake3dhMessage_epk_request_tag:
			if (self->debug & AKE_3DH_DEBUG_MESSAGE_LOGS) {
				u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("session %p: EPK request received"), session);
			}
			session->my_epk_requested = true;
			break;

		case Ake3dhMessage_epk_response_tag:
			if (self->debug & AKE_3DH_DEBUG_MESSAGE_LOGS) {
				u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("session %p: EPK response received"), session);
			}
			if (msg->content.epk_response.ephemeral_pk.size != AKE_3DH_EPK_SIZE) {
				return AKE_3DH_RECEIVE_HANDLER_FAILED;
			}
			memcpy(session->peer_epk, &msg->content.epk_response.ephemeral_pk.bytes, AKE_3DH_EPK_SIZE);
			session->peer_epk_received = true;
			break;

		case Ake3dhMessage_ipk_request_tag:
			if (self->debug & AKE_3DH_DEBUG_MESSAGE_LOGS) {
				u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("session %p: IPK request received"), session);
			}
			session->my_ipk_requested = true;
			break;

		case Ake3dhMessage_ipk_response_tag:
			if (self->debug & AKE_3DH_DEBUG_MESSAGE_LOGS) {
				u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("session %p: IPK response received"), session);
			}
			if (msg->content.ipk_response.has_identity_pk) {
				if (msg->content.ipk_response.identity_pk.size != AKE_3DH_IPK_SIZE) {
					return AKE_3DH_RECEIVE_HANDLER_FAILED;
				}
				memcpy(session->peer_ipk, &msg->content.ipk_response.identity_pk.bytes, AKE_3DH_IPK_SIZE);
			} else if (msg->content.ipk_response.has_encrypted_identity_pk) {
				/** @todo decrypt */
			} else {
				return AKE_3DH_RECEIVE_HANDLER_FAILED;
			}
			session->peer_ipk_received = true;
			break;

		default:
			return AKE_3DH_RECEIVE_HANDLER_FAILED;
	}

	return AKE_3DH_RECEIVE_HANDLER_OK;
}


#define AKE_3DH_SEND_OK 0
#define AKE_3DH_SEND_FAILED -1
static int32_t ake_3dh_send(Ake3dh *self, Ake3dhSession *session, const Ake3dhMessage *msg) {
	if (u_assert(self != NULL) ||
	    u_assert(msg != NULL)) {
		return AKE_3DH_SEND_FAILED;
	}

	if (self->send_callback == NULL) {
		return AKE_3DH_SEND_FAILED;
	}

	if (self->send_callback(msg, session, self->send_context) != AKE_3DH_SEND_CALLBACK_OK) {
		return AKE_3DH_SEND_FAILED;
	}

	return AKE_3DH_SEND_OK;
}


int32_t ake_3dh_send_epk_request(Ake3dh *self, Ake3dhSession *session) {
	if (u_assert(self != NULL) ||
	    u_assert(session != NULL)) {
		return AKE_3DH_SEND_EPK_REQUEST_FAILED;
	}

	/* Time from the last EPK request is greater than or equal to the current EPK sending interval,
	 * we can send another EPK request. */
	if (session->peer_epk_request_time >= session->peer_epk_request_interval) {
		if (self->debug & AKE_3DH_DEBUG_MESSAGE_LOGS) {
			u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("session %p: sending EPK request"), (void *)session);
		}

		Ake3dhMessage msg = Ake3dhMessage_init_zero;
		memcpy(&msg.session_id.bytes, session->session_id, AKE_3DH_SESSION_SIZE);
		msg.session_id.size = AKE_3DH_SESSION_SIZE;
		msg.which_content = Ake3dhMessage_epk_request_tag;
		ake_3dh_send(self, session, &msg);

		session->peer_epk_request_time = 0;

		/* And adjust the interval. */
		if (session->peer_epk_request_interval == 0) {
			session->peer_epk_request_interval = AKE_3DH_EPK_REQUEST_INITIAL_INTERVAL;
		} else {
			session->peer_epk_request_interval *= AKE_3DH_EPK_REQUEST_INTERVAL_MULTIPLIER;
		}
		if (session->peer_epk_request_interval > AKE_3DH_EPK_REQUEST_INTERVAL_MAX) {
			session->peer_epk_request_interval = AKE_3DH_EPK_REQUEST_INTERVAL_MAX;
		}
	}

	return AKE_3DH_SEND_EPK_REQUEST_OK;
}


/** @todo meh, repeated code */
int32_t ake_3dh_send_ipk_request(Ake3dh *self, Ake3dhSession *session) {
	if (u_assert(self != NULL) ||
	    u_assert(session != NULL)) {
		return AKE_3DH_SEND_IPK_REQUEST_FAILED;
	}

	if (session->peer_ipk_request_time >= session->peer_ipk_request_interval) {
		if (self->debug & AKE_3DH_DEBUG_MESSAGE_LOGS) {
			u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("session %p: sending IPK request"), (void *)session);
		}

		Ake3dhMessage msg = Ake3dhMessage_init_zero;
		memcpy(&msg.session_id.bytes, session->session_id, AKE_3DH_SESSION_SIZE);
		msg.session_id.size = AKE_3DH_SESSION_SIZE;
		msg.which_content = Ake3dhMessage_ipk_request_tag;
		ake_3dh_send(self, session, &msg);

		session->peer_ipk_request_time = 0;

		/* And adjust the interval. */
		if (session->peer_ipk_request_interval == 0) {
			session->peer_ipk_request_interval = AKE_3DH_IPK_REQUEST_INITIAL_INTERVAL;
		} else {
			session->peer_ipk_request_interval *= AKE_3DH_IPK_REQUEST_INTERVAL_MULTIPLIER;
		}
		if (session->peer_ipk_request_interval > AKE_3DH_IPK_REQUEST_INTERVAL_MAX) {
			session->peer_ipk_request_interval = AKE_3DH_IPK_REQUEST_INTERVAL_MAX;
		}
	}

	return AKE_3DH_SEND_IPK_REQUEST_OK;
}


int32_t ake_3dh_send_epk_response(Ake3dh *self, Ake3dhSession *session) {
	if (u_assert(self != NULL) ||
	    u_assert(session != NULL) ||
	    u_assert(session->my_epk_calculated == true)) {
		return AKE_3DH_SEND_EPK_RESPONSE_FAILED;
	}

	if (self->debug & AKE_3DH_DEBUG_MESSAGE_LOGS) {
		u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("session %p: sending EPK response"), (void *)session);
	}

	Ake3dhMessage msg = Ake3dhMessage_init_zero;
	memcpy(&msg.session_id.bytes, session->session_id, AKE_3DH_SESSION_SIZE);
	msg.session_id.size = AKE_3DH_SESSION_SIZE;
	msg.which_content = Ake3dhMessage_epk_response_tag;
	memcpy(msg.content.epk_response.ephemeral_pk.bytes, session->my_epk, AKE_3DH_EPK_SIZE);
	msg.content.epk_response.ephemeral_pk.size = AKE_3DH_EPK_SIZE;
	ake_3dh_send(self, session, &msg);

	session->my_epk_requested = false;

	return AKE_3DH_SEND_EPK_RESPONSE_OK;
}


int32_t ake_3dh_send_ipk_response(Ake3dh *self, Ake3dhSession *session) {
	if (u_assert(self != NULL) ||
	    u_assert(session != NULL)) {
		return AKE_3DH_SEND_IPK_RESPONSE_FAILED;
	}

	if (self->debug & AKE_3DH_DEBUG_MESSAGE_LOGS) {
		u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("session %p: sending IPK response"), (void *)session);
	}

	Ake3dhMessage msg = Ake3dhMessage_init_zero;
	memcpy(&msg.session_id.bytes, session->session_id, AKE_3DH_SESSION_SIZE);
	msg.session_id.size = AKE_3DH_SESSION_SIZE;
	msg.which_content = Ake3dhMessage_ipk_response_tag;
	memcpy(&msg.content.ipk_response.identity_pk, session->my_ipk, AKE_3DH_IPK_SIZE);
	msg.content.ipk_response.identity_pk.size = AKE_3DH_IPK_SIZE;
	msg.content.ipk_response.has_identity_pk = true;
	ake_3dh_send(self, session, &msg);

	session->my_ipk_requested = false;

	return AKE_3DH_SEND_IPK_RESPONSE_OK;
}


