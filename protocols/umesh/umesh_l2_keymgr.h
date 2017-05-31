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

#pragma once

/**
 * This uMesh module maintains a table of all layer 2 key exchanges in progress. Only one key exchange can happen at any
 * time between two peers described by a pair of TIDs. Each peer may support different sets of key exchange protocols.
 * The protocols should be tried in a sequence ordered by priority. Result of each key exchange may be following:
 *   - KE_OK - the key exchange was done successfully. Shared keys and peer's identity keys are available and can be
 *             retrieved.
 *   - KE_FAILED - it is known that the key exchange cannot succeed either because of an error in the software or
 *                 because the peer responded wrongly (including the wrong key),
 *   - KE_TIMEOUT - the peer is not responding to this protocol.
 *
 * @todo in the future, this module should handle the key management for layer 2 too, it should be renamed then.
 */


#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "umesh_l3_protocol.h"

#include "interface_rng.h"
#include "ake_3dh.h"


enum umesh_l2_keymgr_session_state {
	/**
	 * Initially all sessions are in the EMPTY state (after being zeroed by memset). When a new session is started,
	 * the state changes to NEW and it is picked up by the initial key exchange process (changing state to
	 * INITIAL_AKE). Result of the AKE may be either AUTH or NAUTH state. If authenticated (AUTH state), peer's
	 * identity is recorded in the session. Authorization process checks if the peer is allowed to communicate with
	 * us and sets the authorization state to AUTZ or NAUTZ. If authorized, the session changes its state to
	 * MANAGED and it stays there until it is considered as expired (state is set to EXPIRED). The session can be
	 * closed (setting its state to OLD) and the garbage collector removes it (setting its state to EMPTY).
	 */
	UMESH_L2_KEYMGR_SESSION_STATE_EMPTY = 0,
	UMESH_L2_KEYMGR_SESSION_STATE_NEW,
	UMESH_L2_KEYMGR_SESSION_STATE_INITIAL_AKE,
	UMESH_L2_KEYMGR_SESSION_STATE_AUTH,
	UMESH_L2_KEYMGR_SESSION_STATE_NAUTH,
	UMESH_L2_KEYMGR_SESSION_STATE_AUTZ,
	UMESH_L2_KEYMGR_SESSION_STATE_NAUTZ,
	UMESH_L2_KEYMGR_SESSION_STATE_MANAGED,
	UMESH_L2_KEYMGR_SESSION_STATE_EXPIRED,
	UMESH_L2_KEYMGR_SESSION_STATE_OLD
};

/**
 * Session state timeouts.
 */
#define UMESH_L2_KEYMGR_TIMEOUT_NEW_MS           5000    /* Waiting to be processed. */
#define UMESH_L2_KEYMGR_TIMEOUT_INITIAL_AKE_MS   20000   /* Waiting for AKE to finish. */
#define UMESH_L2_KEYMGR_TIMEOUT_AUTH_MS          5000    /* Waiting to be processed. */
#define UMESH_L2_KEYMGR_TIMEOUT_NAUTH_MS         5000    /* Waiting to be processed. */
#define UMESH_L2_KEYMGR_TIMEOUT_AUTZ_MS          5000    /* Waiting to be processed. */
#define UMESH_L2_KEYMGR_TIMEOUT_NAUTZ_MS         5000    /* Waiting to be processed. */
#define UMESH_L2_KEYMGR_TIMEOUT_MANAGED_MS       (10 * 60 * 1000)   /* New session should be created. */
#define UMESH_L2_KEYMGR_TIMEOUT_EXPIRED_MS       5000    /* Waiting to be processed. */
#define UMESH_L2_KEYMGR_TIMEOUT_OLD_MS           5000    /* Waiting to be processed. */

/**
 * Interval between processing steps. In the future it will be used to shedule processor wake-up.
 */
#define UMESH_L2_KEYMGR_STEP_INTERVAL_MS         100

/**
 * Sizes of the TX and RX master keys. As they are derived using 512 bit hash function, maximum 64 bytes are allowed.
 */
#define UMESH_L2_KEYMGR_MASTER_TX_KEY_SIZE       32
#define UMESH_L2_KEYMGR_MASTER_RX_KEY_SIZE       32
#define UMESH_L2_KEYMGR_IDENTITY_SIZE            32

/**
 * A single key management session created between us and our neighbor. There may be neighbors without any
 * active session created (ie. when there are no resources available, a node may decide to create key management
 * sessions only with some of its neighbors).
 */
typedef struct umesh_l2_keymgr_session {

	/** TIDs of the participating peers. */
	uint32_t my_tid;
	uint32_t peer_tid;

	Ake3dhSession *ake_3dh;
	uint32_t ake_time;

	enum umesh_l2_keymgr_session_state state;
	uint32_t state_timeout_ms;

	/**
	 * Master keys are valid through the whole key management session. They can be altered by various key management
	 * algorithms and protocols. They are used to produce chain and message keys for encryption and authentication.
	 */
	uint8_t master_tx_key[UMESH_L2_KEYMGR_MASTER_TX_KEY_SIZE];
	uint8_t master_rx_key[UMESH_L2_KEYMGR_MASTER_RX_KEY_SIZE];

	uint8_t peer_identity[UMESH_L2_KEYMGR_IDENTITY_SIZE];

} L2KeyManagerSession;


typedef struct umesh_l2_keymgr {

	/**
	 * Key manager session table is used to hold data about all managed sessions and states. The same peer TID
	 * can have multiple sessions. Keys from the newest session are used for encryption.
	 */
	struct umesh_l2_keymgr_session *session_table;
	uint32_t session_table_size;
	/** @todo session_table lock */
	/** @todo session state change callback */

	TaskHandle_t keymgr_task;

	struct umesh_l3_protocol protocol;
	struct umesh_l2_pbuf pbuf;
	Ake3dh ake_3dh;

	/** @todo identity management */
	uint8_t test_isk[32];
	uint8_t test_ipk[32];

	/** Dependencies. */
	struct interface_rng *rng;

} L2KeyManager;


int32_t umesh_l2_keymgr_init(L2KeyManager *self, uint32_t table_size);
#define UMESH_L2_KEYMGR_INIT_OK 0
#define UMESH_L2_KEYMGR_INIT_FAILED -1
#define UMESH_L2_KEYMGR_INIT_NOMEM -2

int32_t umesh_l2_keymgr_free(L2KeyManager *self);
#define UMESH_L2_KEYMGR_FREE_OK 0
#define UMESH_L2_KEYMGR_FREE_FAILED -1

int32_t umesh_l2_keymgr_set_rng(L2KeyManager *self, struct interface_rng *rng);
#define UMESH_L2_KEYMGR_SET_RNG_OK 0
#define UMESH_L2_KEYMGR_SET_RNG_FAILED -1


/**
 * @brief Start a key manager session with the specified peer TID
 */
int32_t umesh_l2_keymgr_manage(L2KeyManager *self, uint32_t tid);
#define UMESH_L2_KEYMGR_MANAGE_OK 0
#define UMESH_L2_KEYMGR_MANAGE_FAILED -1

/**
 * @brief Get the most recent session for the specified peer
 *
 * @param self L2 key manager instance
 * @param peer_tid TID of the peer whose session is to be returned
 *
 * @return NULL if no such session is found or error occured,
 *         or a valid pointer to the corresponding L2KeyManagerSession.
 */
L2KeyManagerSession *umesh_l2_keymgr_find_session(L2KeyManager *self, uint32_t peer_tid);

extern const char *umesh_l2_keymgr_states[];
