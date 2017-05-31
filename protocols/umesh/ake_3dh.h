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

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "interface_rng.h"       /** For generating session ID and ephemeral secret keys. */
#include "ake_3dh.pb.h"          /** Message definitions. */

/**
 * Sizes of the ephemeral and identity secret and public keys. For X25519 they are always 256 bits long.
 */
#define AKE_3DH_ESK_SIZE            32
#define AKE_3DH_EPK_SIZE            32
#define AKE_3DH_ISK_SIZE            32
#define AKE_3DH_IPK_SIZE            32

/**
 * Computed shared secret key size. For X25519, it is always 256 bits long.
 */
#define AKE_3DH_SH_SIZE             32

/**
 * Maximum size of the session identifier in bytes. If a single instance handles multiple sessions, it is used to
 * differentiate between them.
 */
#define AKE_3DH_SESSION_SIZE        4

/**
 * Size of the master key computed as a result of the 3DH key exchange. TX and RX keys are then derived from the
 * master key and are of the same size.
 */
#define AKE_3DH_MASTER_KEY_SIZE     32

/**
 * Interval of the 3DH AKE main loop in milliseconds. 100-500ms is recommended. In the future it may not be fixed
 * (the loop will be executed when the processor is not sleeping).
 */
#define AKE_3DH_STEP_INTERVAL_MS    100

/** @todo rework packet rate limiting */
#define AKE_3DH_EPK_REQUEST_INITIAL_INTERVAL      200
#define AKE_3DH_EPK_REQUEST_INTERVAL_MULTIPLIER   2
#define AKE_3DH_EPK_REQUEST_INTERVAL_MAX          2000

#define AKE_3DH_IPK_REQUEST_INITIAL_INTERVAL      200
#define AKE_3DH_IPK_REQUEST_INTERVAL_MULTIPLIER   2
#define AKE_3DH_IPK_REQUEST_INTERVAL_MAX          2000

/**
 * Result of the authenticated key exchange. It is NONE if not set or OK/FAILED if the authentication is
 * successful or failed.
 */
enum ake_3dh_result {
	AKE_3DH_RESULT_NONE = 0,
	AKE_3DH_RESULT_OK,
	AKE_3DH_RESULT_FAILED
};

/**
 * SH2 and SH3 shared secret key computation depends on the peer role. It has to be determined prior to computation.
 */
enum ake_3dh_who_i_am {
	AKE_3DH_ROLE_UNKNOWN = 0,
	AKE_3DH_ALICE,
	AKE_3DH_BOB
};

/**
 * A single 3DH AKE protocol session.
 *
 * @todo volatile is used to provide a sort of "atomic variables" to let other threads know that a variable is
 *       ready. This approach is not correct and a proper locking mechanism will need to be done.
 *
 * There are some common prefixes and suffixes for the following variables:
 *   * my_ - our identity/ephemeral/other key or value, either generated or computed
 *   * peer_ - value/variable received from our peer
 *   * _isk - identity secret key
 *   * _ipk - identity public key
 *   * _esk - ephemeral secret key
 *   * _epk - ephemeral public key
 *   * _shX - shared secret key X (1, 2, 3)
 */
typedef struct ake_3dh_session {
	/** Set to true if the session record is active and used. */
	bool used;

	uint8_t session_id[AKE_3DH_SESSION_SIZE];
	volatile bool session_id_generated;

	uint8_t my_esk[AKE_3DH_ESK_SIZE];
	volatile bool my_esk_generated;

	uint8_t my_epk[AKE_3DH_EPK_SIZE];
	/** @todo rename to computed */
	volatile bool my_epk_calculated;
	volatile bool my_epk_requested;

	uint8_t peer_epk[AKE_3DH_EPK_SIZE];
	uint32_t peer_epk_request_interval;
	uint32_t peer_epk_request_time;
	volatile bool peer_epk_received;

	uint8_t *my_isk;
	uint8_t *my_ipk;
	volatile bool my_ipk_requested;

	uint8_t peer_ipk[AKE_3DH_IPK_SIZE];
	uint32_t peer_ipk_request_interval;
	uint32_t peer_ipk_request_time;
	volatile bool peer_ipk_received;

	uint8_t sh1[AKE_3DH_SH_SIZE];
	volatile bool sh1_computed;

	/** The role is determined during SH1 key computation. */
	enum ake_3dh_who_i_am who_i_am;

	uint8_t sh2[AKE_3DH_SH_SIZE];
	volatile bool sh2_computed;

	uint8_t sh3[AKE_3DH_SH_SIZE];
	volatile bool sh3_computed;

	uint8_t master_key[AKE_3DH_MASTER_KEY_SIZE];
	volatile bool master_key_computed;

	uint8_t master_tx_key[AKE_3DH_MASTER_KEY_SIZE];
	uint8_t master_rx_key[AKE_3DH_MASTER_KEY_SIZE];
	volatile bool tx_rx_keys_computed;

	enum ake_3dh_result result;
} Ake3dhSession;

#define AKE_3DH_DEBUG_LOGS (1 << 0)
#define AKE_3DH_DEBUG_MESSAGE_LOGS (1 << 1)
#define AKE_3DH_DEBUG_WARNINGS (1 << 2)

typedef struct ake_3dh_context {
	Ake3dhSession *session_table;
	size_t session_table_size;

	struct interface_rng *rng;
	TaskHandle_t main_task;
	uint32_t debug;

	/**
	 * Callback called when the protocol needs to send a protocol message to its peer. It should be bound
	 * to a proper data sending function in the module using Ake3dh.
	 */
	int32_t (*send_callback)(const Ake3dhMessage *msg, Ake3dhSession *session, void *context);
	void *send_context;
	#define AKE_3DH_SEND_CALLBACK_OK 0
	#define AKE_3DH_SEND_CALLBACK_FAILED -1

	/**
	 * Called when the authentication result is known.
	 */
	int32_t (*session_result_callback)(Ake3dhSession *session, enum ake_3dh_result result, void *context);
	void *session_result_context;
	#define AKE_3DH_SESSION_RESULT_CALLBACK_OK 0
	#define AKE_3DH_SESSION_RESULT_CALLBACK_FAILED -1

} Ake3dh;


/**
 * @brief Initialize a 3DH AKE instance
 *
 * Allocates and initializes required resources for a new 3DH AKE instance.
 *
 * @param self The 3DH AKE instance
 * @param max_sessions Number of concurrent sessions which can be served
 *
 * @return
 */
int32_t ake_3dh_init(Ake3dh *self, uint32_t max_sessions);
#define AKE_3DH_INIT_OK 0
#define AKE_3DH_INIT_FAILED -1
#define AKE_3DH_INIT_NOMEM -2

int32_t ake_3dh_free(Ake3dh *self);
#define AKE_3DH_FREE_OK 0
#define AKE_3DH_FREE_FAILED -1

/**
 * @brief Set the random number generator dependency
 *
 * It must be done right after the initialization, otherwise the protocol will not work as expected
 * (no keys and IDs will be generated).
 *
 * @param self The 3DH AKE instance
 * @param rng interface of a random number generator
 *
 * @return AKE_3DH_SET_RNG_OK on success or
 *         AKE_3DH_SET_RNG_FAILED otherwise.
 */
int32_t ake_3dh_set_rng(Ake3dh *self, struct interface_rng *rng);
#define AKE_3DH_SET_RNG_OK 0
#define AKE_3DH_SET_RNG_FAILED -1

Ake3dhSession *ake_3dh_start_session(Ake3dh *self, uint8_t *my_isk, uint8_t *my_ipk, uint8_t *session_id, size_t session_id_len);
void ake_3dh_stop_session(Ake3dh *self, Ake3dhSession *session);

/**
 * @brief Get protocol completion status as a percentage
 *
 * @param self 3DH AKE instance
 * @param session 3DH AKE session
 *
 * @return An unsigned value between 0-100 (including) corresponding to the percentage of the protocol
 *         completion, 0 on error.
 */
size_t ake_3dh_protocol_status(Ake3dh *self, Ake3dhSession *session);

int32_t ake_3dh_receive_handler(Ake3dh *self, const Ake3dhMessage *msg);
#define AKE_3DH_RECEIVE_HANDLER_OK 0
#define AKE_3DH_RECEIVE_HANDLER_FAILED -1
#define AKE_3DH_RECEIVE_HANDLER_NO_SESSION -2

int32_t ake_3dh_process_epk_request(Ake3dh *self, const EphemeralPublicKeyRequest *msg);
#define AKE_3DH_PROCESS_EPK_REQUEST_OK 0
#define AKE_3DH_PROCESS_EPK_REQUEST_FAILED -1



int32_t ake_3dh_send_epk_request(Ake3dh *self, Ake3dhSession *session);
#define AKE_3DH_SEND_EPK_REQUEST_OK 0
#define AKE_3DH_SEND_EPK_REQUEST_FAILED -1

int32_t ake_3dh_send_ipk_request(Ake3dh *self, Ake3dhSession *session);
#define AKE_3DH_SEND_IPK_REQUEST_OK 0
#define AKE_3DH_SEND_IPK_REQUEST_FAILED -1

int32_t ake_3dh_send_epk_response(Ake3dh *self, Ake3dhSession *session);
#define AKE_3DH_SEND_EPK_RESPONSE_OK 0
#define AKE_3DH_SEND_EPK_RESPONSE_FAILED -1

int32_t ake_3dh_send_ipk_response(Ake3dh *self, Ake3dhSession *session);
#define AKE_3DH_SEND_IPK_RESPONSE_OK 0
#define AKE_3DH_SEND_IPK_RESPONSE_FAILED -1

