/*
 * rMAC (PRF-based radio MAC)
 *
 * Copyright (c) 2018, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "rmac.h"

#include "interfaces/radio.h"
#include "interfaces/radio-mac/host.h"
#include "interfaces/clock/descriptor.h"



#define MODULE_NAME "rmac"
#define ASSERT(c, r) if (u_assert(c)) return r


/**
 * Service goals:
 * 1. Export just a minimum API. Support full-RX and immediate-RX mode.
 *    No neighbor table is built, only a single neighbor is supported.
 *    It is possible to piggy-back data on slots. Transmit a current
 *    timestamp in microseconds as a POC.
 * 2. The service is known as rMAC. API is tidied-up and all functions
 *    are moved to their modules. Unused code is removed and the rest
 *    is well-commented. State machine is implemented.
 *    No optimizations yet. Maximum 10% PER is acceptable.
 * 3. The rMAC is configurable using its CLI.
 * 4. Neighbor table is introduced. When responding, the device is aware
 *    of all neighbors and schedules TX slots accordingly. It is possible
 *    to build a star topology from sleeping devices and a single hub.
 *    The device knows the actual time of all neighbors. If required,
 *    all delays in the processing are documented and measured.
 * 5. Slot allocation schemes are introduced. Only TDMA allocation is
 *    supported. Devices are able to negotiate RX/TX slots and communicate
 *    using them.
 */


rmac_ret_t rmac_init(Rmac *self, Radio *radio) {
	ASSERT(self != NULL, RMAC_RET_NULL);
	ASSERT(radio != NULL, RMAC_RET_BAD_ARG);

	memset(self, 0, sizeof(Rmac));
	self->radio = radio;

	if (iradio_mac_init(&self->iface) != IRADIO_MAC_RET_OK) {
		goto err;
	}

	if (hradio_mac_connect(&self->iface_host, &self->iface)) {
		goto err;
	}

	if (nbtable_init(&self->nbtable, 16) != NBTABLE_RET_OK) {
		goto err;
	}

	self->debug = false;

	if (radio_scheduler_start(self) != RMAC_RET_OK) {
		goto err;
	}

	self->state = RMAC_STATE_STOPPED;

	/** @todo set default parameters here. */
	rmac_set_universe_key(self, (uint8_t *)"Universe", 8);
	rmac_set_fhss_algo(self, RMAC_FHSS_ALGO_SINGLE);
	rmac_set_tdma_algo(self, RMAC_TDMA_ALGO_CSMA);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("service started"));
	return RMAC_RET_OK;

err:
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("cannot start the service"));
	return RMAC_RET_FAILED;;
}


rmac_ret_t rmac_free(Rmac *self) {
	ASSERT(self != NULL, RMAC_RET_NULL);
	ASSERT(self->state == RMAC_STATE_STOPPED, RMAC_RET_BAD_STATE);

	nbtable_free(&self->nbtable);

	return RMAC_RET_OK;
}


rmac_ret_t rmac_start(Rmac *self) {
	ASSERT(self != NULL, RMAC_RET_NULL);
	ASSERT(self->state == RMAC_STATE_STOPPED, RMAC_RET_BAD_STATE);

	/* Change the state prior to starting threads. */
	self->state = RMAC_STATE_SEARCHING;

	/** @todo start all processes here. */

	return RMAC_RET_OK;
}


rmac_ret_t rmac_stop(Rmac *self) {
	ASSERT(self != NULL, RMAC_RET_NULL);
	ASSERT(self->state == RMAC_STATE_SEARCHING || self->state == RMAC_STATE_RUNNING, RMAC_RET_BAD_STATE);

	self->state = RMAC_STATE_STOP_REQ;

	bool still_running = false;
	while (still_running) {
		vTaskDelay(10);
	}
	self->state = RMAC_STATE_STOPPED;

	return RMAC_RET_OK;
}


rmac_ret_t rmac_set_mcs_single(Rmac *self, struct rmac_mcs *mcs) {
	ASSERT(self != NULL, RMAC_RET_NULL);

	return RMAC_RET_OK;
}


rmac_ret_t rmac_set_sas(Rmac *self, struct rmac_sas *sas) {
	ASSERT(self != NULL, RMAC_RET_NULL);

	return RMAC_RET_OK;
}


rmac_ret_t rmac_set_fhss_algo(Rmac *self, enum rmac_fhss_algo fhss) {
	ASSERT(self != NULL, RMAC_RET_NULL);

	self->fhss_algo = fhss;

	return RMAC_RET_OK;
}


rmac_ret_t rmac_set_tdma_algo(Rmac *self, enum rmac_tdma_algo tdma) {
	ASSERT(self != NULL, RMAC_RET_NULL);

	self->tdma_algo = tdma;

	return RMAC_RET_OK;
}


rmac_ret_t rmac_set_frequency(Rmac *self, uint64_t frequency_hz) {

}


rmac_ret_t rmac_set_universe_key(Rmac *self, uint8_t *key, size_t len) {
	ASSERT(self != NULL, RMAC_RET_NULL);
	ASSERT(key != NULL, RMAC_RET_BAD_ARG);
	ASSERT(len > 0, RMAC_RET_BAD_ARG);

	if (len > RMAC_UNIVERSE_KEY_MAX) {
		return RMAC_RET_FAILED;
	}

	/* Copy the universe key - it will be used for rx_pbuf and tx_pbuf. */
	memcpy(self->universe_key, key, len);
	self->universe_key_len = len;

	/* And set the radio sync. */
	memcpy(self->radio_sync, "abcd", 4);

	return RMAC_RET_OK;
}


rmac_ret_t rmac_set_clock(Rmac *self, IClock *clock) {
	ASSERT(self != NULL, RMAC_RET_NULL);
	ASSERT(clock != NULL, RMAC_RET_BAD_ARG);

	self->clock = clock;

	return RMAC_RET_OK;
}



