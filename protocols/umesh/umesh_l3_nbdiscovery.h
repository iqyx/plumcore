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

#ifndef _UMESH_L3_NBDISCOVERY_H_
#define _UMESH_L3_NBDISCOVERY_H_

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "umesh_l3_protocol.h"
#include "interface_rng.h"

/**
 * Compile-time configurable constants for the neighbor discovery protocol.
 */
#define UMESH_L3_NBDISCOVERY_ADV_BASIC_INTERVAL 1000
#define UMESH_L3_NBDISCOVERY_STEP_INTERVAL 100

#define UMESH_L3_NBDISCOVERY_VALID_TID_AGE_MAX 3600000

/**
 * A recommended pattern for building layer 3 protocol receive handlers and
 * packet processing:
 * -----------------------------------------------------------------------------
 *
 *   * all actions inside the receive handler are serial except the message
 *     processing part which makes the decision step and calls other functions
 *     for different message types.
 *   * Input to the receive handler, check function and the parsing function is
 *     the packet buffer from layer 2 (umesh_l2_pbuf). Those are the parts which
 *     ma depend on the protocol structure.
 *   * Protocol message buffer is used later in the receive chain. This message
 *     buffer is protocol structure independent.
 *
 * receive handler
 * ------->+-+
 *         | |  check
 *         | |-------->+-+       Make sure that the layer 2 packet is really
 *         | |         | |       meant to be processed by this protocol handler
 *         | |         | |       (asserts) and that it is properly error checked
 *         | |<--------+-+       and authenticated (as required by the protocol)
 *         | |
 *         | | prepare           Prepare a message buffer which will hold the
 *         | |-------->+-+       result of packet parsing.
 *         | |         | |
 *         | |         | |
 *         | |<--------+-+
 *         | |
 *         | |  parse            Call the packet parsing function. It will store
 *         | |-------->+-+       the result in the message buffer. This must be
 *         | |         | |       the single place where the protocol structure
 *         | |         | |       dependent code resides. The message buffer
 *         | |<--------+-+       must be protocol structure independent (but it
 *         | |                   is protocol logic dependent of course).
 *         | | process
 *         | |-------->+-+                     Process function serves as a
 *         | |         | | process message     decision maker for protocol
 *         | |         | |-------->+-+         messages and selects the function
 *         | |         | |         | |         which is responsible for the
 *         | |         | |<--------+-+         message processing.
 *         | |         | |
 *         | |<--------+-+
 *         | |
 *         | | tidy up
 *         | |-------->+-+       At the end we can free the message buffer and
 *         | |         | |       return back to layer 2 declaring success (or
 *         | |         | |       we return a proper error code).
 *         | |<--------+-+
 *         | |
 * <-------+-+
 *
 * Recommended function prototypes:
 *
 *   * int32_t receive_handler(struct umesh_l2_pbuf *pbuf, void *context);
 *     (context is cast to protocol-specific context structure inside)
 *   * int32_t receive_check(struct protocol_context *proto, struct
 *     umesh_l2_pbuf *pbuf);
 *   * int32_t parse(struct protocol_context *proto, struct umesh_l2_pbuf *pbuf,
 *     struct protocol_message *msg);
 *   * int32_t process(struct protocol_context *proto, struct protocol_message
 *     *msg);
 *   * int32_t process_message(struct protocol_context *proto, struct
 *     protocol_message *msg);
 *
 *
 * A recommended pattern for the sending chain part of the layer 3 protocol
 * implementation
 * -----------------------------------------------------------------------------
 *
 *   * A received packet processing MUST NOT trigger sending of any protocol
 *     message directly (stack usage). Even if triggered indirectly (using
 *     a protocol message scheduler for example), actions depending on the
 *     incoming packets should be throttled.
 *   * A dedicated message build function is called whenever a message sending
 *     is requested. Message send function is then called which does all the
 *     required processing and sending.
 *
 * message send
 * ------->+-+
 *         | |
 *         | | prepare proto    A protocol message buffer is prepared on the
 *         | |-----+   msg      stack and filled with all required information.
 *         | |     |            This should be protocol structure independent.
 *         |+++<---+            A generic sending function is then called.
 *         || |
 *         || |                 The whole protocol logic should be contained
 *         || |                 on the beginning of this function.
 *         |+++
 *         | |   send
 *         | |-------->+-+
 *         | |         | |  prepare pbuf
 *         | |         | |-------->+-+
 *         | |         | |         | |    A new layer 2 packet buffer is
 *         | |         | |         | |    prepared (on the stack or shared one).
 *         | |         | |-------->+-+
 *         | |         | |
 *         | |         | |  build message
 *         | |         | |-------->+-+    Message building function is called.
 *         | |         | |         | |    This is the only part of the process
 *         | |         | |         | |    which is protocol structure dependent.
 *         | |         | |-------->+-+    The message is built and saved to the
 *         | |         | |                provided layer 2 packet buffer.
 *         | |         | |  L2 send
 *         | |         | |-------->+-+    Laeyr 2 send function is called to
 *         | |         | |         | |    send the message built in the
 *         | |         | |         | |    previous step.
 *         | |         | |-------->+-+
 *         | |         | |
 *         | |         | |  tidy up
 *         | |         | |-------->+-+
 *         | |         | |         | |
 *         | |         | |         | |
 *         | |         | |-------->+-+
 *         | |         | |                Finally return to the calling message
 *         | |-------->+-+                specific send function with a proper
 *         | |                            return value.
 * <-------+-+
 *
 */


/**
 * Protocol messages share a common message type (which should be declared as an
 * enum of all possible message types). The data part of the protocol message
 * is message type dependent and should be declared as an union of different
 * structs.
 */

/**
 * This structures contain additional data for different message types. They are
 * later declared as an union.
 */
struct umesh_l3_nbdiscovery_msg_adv_basic {
	/* Basic TID advertising message need some layer 2 specific data. */
	uint32_t tid;
	int32_t fei_hz;
	int32_t rssi_10dBm;
	uint8_t lqi_percent;
};

/**
 * The union with all possible message data structures.
 */
union umesh_l3_nbdiscovery_msg_data {
	struct umesh_l3_nbdiscovery_msg_adv_basic adv_basic;

};

/**
 * List of all protocol messages.
 */
enum umesh_l3_nbdiscovery_msg_type {
	UMESH_L3_NBDISCOVERY_PACKET_TID_ADV_BASIC = 0,
};

/**
 * The abstract protocol message which can contain any of the previously
 * declared protocol message types. The message type enum is the only common
 * member shared among all messages. Message specific data should be in the
 * data union declared above.
 */
struct umesh_l3_nbdiscovery_msg {
	/* Common members. */
	enum umesh_l3_nbdiscovery_msg_type type;

	/* Message type specific members. */
	union umesh_l3_nbdiscovery_msg_data data;
};


struct umesh_l3_nbdiscovery {
	struct umesh_l3_protocol protocol;
	struct umesh_l2_nbtable *nbtable;
	struct interface_rng *rng;

	/**
	 * Layer 2 node identification (TID). We are continuously advertising
	 * the current TID to all our neighbors.
	 */
	uint32_t tid_current;
	uint32_t tid_last;

	/**
	 * Beaconing task (protocol message scheduler).
	 */
	TaskHandle_t beacon_task;

	/**
	 * Time in milliseconds from the last invocation of a particular
	 * action. Used by the protocol message scheduler.
	 */
	uint32_t last_adv_basic_ms;
	uint32_t tid_age_ms;

	/**
	 * Shared L2 packet buffer used to send messages.
	 */
	struct umesh_l2_pbuf pbuf;

	/**
	 * Runtime configuration variables.
	 */
	/** @todo */
};


/******************************************************************************
 * Layer 3 neighbor discovery protocol context manipulation
 ******************************************************************************/

/**
 * @brief Initialize the layer 3 neighbor discovery protocol context
 *
 * @param nbd The context to be initialized.
 * @param nbtable A neighbor table dependency.
 *
 * @return UMESH_L3_NBDISCOVERY_INIT_OK on success or
 *         UMESH_L3_NBDISCOVERY_INIT_FAILED otherwise.
 */
int32_t umesh_l3_nbdiscovery_init(struct umesh_l3_nbdiscovery *nbd, struct umesh_l2_nbtable *nbtable);
#define UMESH_L3_NBDISCOVERY_INIT_OK 0
#define UMESH_L3_NBDISCOVERY_INIT_FAILED -1

/**
 * @brief Free a previously initialized neighbor discovery protocol context
 *
 * @param nbd The context to be freed.
 *
 * @return UMESH_L3_NBDISCOVERY_FREE_OK on success or
 *         UMESH_L3_NBDISCOVERY_FREE_FAILED otherwise.
 */
int32_t umesh_l3_nbdiscovery_free(struct umesh_l3_nbdiscovery *nbd);
#define UMESH_L3_NBDISCOVERY_FREE_OK 0
#define UMESH_L3_NBDISCOVERY_FREE_FAILED -1

int32_t umesh_l3_nbdiscovery_set_rng(struct umesh_l3_nbdiscovery *nbd, struct interface_rng *rng);
#define UMESH_L3_NBDISCOVERY_SET_RNG_OK 0
#define UMESH_L3_NBDISCOVERY_SET_RNG_FAILED -1


/******************************************************************************
 * Receiving chain
 ******************************************************************************/

/**
 * @brief Check if the received packet should be processed
 *
 * @param nbd The context of nbdiscovery.
 * @param pbuf The layer 2 packet to be checked.
 *
 * @return UMESH_L3_NBDISCOVERY_RECEIVE_CHECK_OK if the check was successfull or
 *         UMESH_L3_NBDISCOVERY_RECEIVE_CHECK_FAILED otherwise.
 */
int32_t umesh_l3_nbdiscovery_receive_check(struct umesh_l3_nbdiscovery *nbd, struct umesh_l2_pbuf *pbuf);
#define UMESH_L3_NBDISCOVERY_RECEIVE_CHECK_OK 0
#define UMESH_L3_NBDISCOVERY_RECEIVE_CHECK_FAILED -1

/**
 * @brief Parse the layer 2 packet into a protocol structure independent
 *        message buffer.
 *
 * @param nbd The context of nbdiscovery.
 * @param pbuf The layer 2 packet to be parsed.
 * @param msg A structure where the message data will be saved.
 *
 * @return UMESH_L3_NBDISCOVERY_PARSE_OK on success or
 *         UMESH_L3_NBDISCOVERY_PARSE_FAILED otherwise.
 */
int32_t umesh_l3_nbdiscovery_parse(struct umesh_l3_nbdiscovery *nbd, struct umesh_l2_pbuf *pbuf, struct umesh_l3_nbdiscovery_msg *msg);
#define UMESH_L3_NBDISCOVERY_PARSE_OK 0
#define UMESH_L3_NBDISCOVERY_PARSE_FAILED -1

/**
 * @brief Process the previously parsed message
 *
 * @param nbd The context of nbdiscovery.
 * @param msg The message to process.
 *
 * @return UMESH_L3_NBDISCOVERY_PROCESS_OK on success or
 *         UMESH_L3_NBDISCOVERY_PROCESS_FAILED otherwise.
 */
int32_t umesh_l3_nbdiscovery_process(struct umesh_l3_nbdiscovery *nbd, struct umesh_l3_nbdiscovery_msg *msg);
#define UMESH_L3_NBDISCOVERY_PROCESS_OK 0
#define UMESH_L3_NBDISCOVERY_PROCESS_FAILED -1

/**
 * @brief Process a basic neighbor advertisement message
 *
 * @param nbd The context of nbdiscovery.
 * @param msg The message to process.
 *
 * @return UMESH_L3_NBDISCOVERY_PROCESS_ADV_BASIC_OK on success or
 *         UMESH_L3_NBDISCOVERY_PROCESS_ADV_BASIC_FAILED otherwise.
 */
int32_t umesh_l3_nbdiscovery_process_adv_basic(struct umesh_l3_nbdiscovery *nbd, struct umesh_l3_nbdiscovery_msg *msg);
#define UMESH_L3_NBDISCOVERY_PROCESS_ADV_BASIC_OK 0
#define UMESH_L3_NBDISCOVERY_PROCESS_ADV_BASIC_FAILED -1


/******************************************************************************
 * Sending chain
 ******************************************************************************/

int32_t umesh_l3_nbdiscovery_send(struct umesh_l3_nbdiscovery *nbd, struct umesh_l3_nbdiscovery_msg *msg);
#define UMESH_L3_NBDISCOVERY_SEND_OK 0
#define UMESH_L3_NBDISCOVERY_SEND_FAILED -1

int32_t umesh_l3_nbdiscovery_build(struct umesh_l3_nbdiscovery *nbd, struct umesh_l3_nbdiscovery_msg *msg, struct umesh_l2_pbuf *pbuf);
#define UMESH_L3_NBDISCOVERY_BUILD_OK 0
#define UMESH_L3_NBDISCOVERY_BUILD_FAILED -1

int32_t umesh_l3_nbdiscovery_send_adv_basic(struct umesh_l3_nbdiscovery *nbd);
#define UMESH_L3_NBDISCOVERY_SEND_ADV_BASIC_OK 0
#define UMESH_L3_NBDISCOVERY_SEND_ADV_BASIC_FAILED -1


#endif


