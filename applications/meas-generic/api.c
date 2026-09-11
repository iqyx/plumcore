/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * meas-generic nbus2 API on the backplane stream
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <main.h>

#include "api.h"

#if defined(CONFIG_APP_MEAS_GENERIC_NBUS_API)

#include <interfaces/servicelocator.h>
#include <interfaces/mq.h>

#define MODULE_NAME "app-meas-generic-api"


api_ret_t api_init(Api *self, Conf *conf) {
	memset(self, 0, sizeof(Api));

	Stream *nbus_stream = NULL;
	if (iservicelocator_query_name_type(locator, "nbus", ISERVICELOCATOR_TYPE_STREAM, (Interface **)&nbus_stream) != ISERVICELOCATOR_RET_OK) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("no nbus stream, API not available"));
		return API_RET_FAILED;
	}

	/* Frame nbus2 packets onto the backplane byte stream and run nbus2 on top of the resulting
	 * Datagram interface. Transmit the ChaCha20+HalfSipHash scheme; accept both schemes on receive. */
	Datagram *nbus_dgram = NULL;
	proto_dgstream_init(&self->nbus_dgstream, nbus_stream);
	proto_dgstream_get_datagram(&self->nbus_dgstream, &nbus_dgram);

	const struct nbus_config nbus_config = {
		.dgram = nbus_dgram,
		.tx_crypto = NBUS_CRYPTO_CHACHA20_HALFSIPHASH,
		.rx_crypto = NBUS_CRYPTO_BLAKE2S_SIV | NBUS_CRYPTO_CHACHA20_HALFSIPHASH,
	};
	nbus_init(&self->nbus_iface, &nbus_config);
	nbus_set_mac_key(&self->nbus_iface, (uint8_t *)"abcd", 4);

	/* Descriptors periodically advertised on the flash and configuration sockets so hosts can
	 * discover them. The nbus2 housekeeping task emits these at its own fixed interval. */
	const struct nbus_socket_descriptor flash_descriptor = {
		.protocol = "flash",
		.protocol_version = NBUS_FLASH_INTERFACE_VERSION,
	};
	const struct nbus_socket_descriptor conf_descriptor = {
		.protocol = "conf",
		.protocol_version = "1.0.0",
	};

	/* Expose the advertised flash partitions for remote access and firmware updates. */
	const uint8_t flash_id[4] = {0x00, 0x00, 0x00, 0x10};
	self->nbus_flash_socket = nbus_socket_allocate(&self->nbus_iface);
	nbus_socket_bind(self->nbus_flash_socket, flash_id, NBUS_FLASH_MAIN_EP);
	nbus_flash_init(&self->nbus_flash, &self->nbus_flash_socket->datagram);
	nbus_socket_set_descriptor(self->nbus_flash_socket, &flash_descriptor);
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("nbus-flash on backplane at %02x%02x%02x%02x ep %d"), flash_id[0], flash_id[1], flash_id[2], flash_id[3], NBUS_FLASH_MAIN_EP);

	/* Serve the application configuration tree on an adjacent SID. */
	const uint8_t conf_id[4] = {0x00, 0x00, 0x00, 0x11};
	self->nbus_conf_socket = nbus_socket_allocate(&self->nbus_iface);
	nbus_socket_bind(self->nbus_conf_socket, conf_id, 1);
	proto_conf_init(&self->nbus_conf, &self->nbus_conf_socket->datagram, conf);
	nbus_socket_set_descriptor(self->nbus_conf_socket, &conf_descriptor);
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("proto-conf on backplane at %02x%02x%02x%02x ep %d"), conf_id[0], conf_id[1], conf_id[2], conf_id[3], 1);

	#if defined(CONFIG_SERVICE_NBUS_MQ_POLL)
		/* Bridge the message queue to nbus2 so a host can poll the compensated measured values. The "+/comp"
		 * filter matches the two-level output topics (<name>/comp) only, leaving the single-level raw input
		 * topics out. */
		Mq *mq = NULL;
		if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_MQ, 0, (Interface **)&mq) == ISERVICELOCATOR_RET_OK) {
			const struct nbus_socket_descriptor mq_descriptor = {
				.protocol = "mq",
				.protocol_version = "1.0.0",
			};
			const uint8_t mq_id[4] = {0x00, 0x00, 0x00, 0x12};
			self->nbus_mq_socket = nbus_socket_allocate(&self->nbus_iface);
			nbus_socket_bind(self->nbus_mq_socket, mq_id, 1);
			nbus_socket_set_descriptor(self->nbus_mq_socket, &mq_descriptor);

			const struct nbus_mq_poll_conf mq_poll_conf = {
				.mq = mq,
				.d = &self->nbus_mq_socket->datagram,
				.topic = "out/#",
				.device_name = PORT_NAME,
			};
			nbus_mq_poll_init(&self->nbus_mq, &mq_poll_conf);
			nbus_mq_poll_start(&self->nbus_mq);
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("nbus-mq-poll on backplane at %02x%02x%02x%02x ep %d"), mq_id[0], mq_id[1], mq_id[2], mq_id[3], 1);
		}
	#endif

	return API_RET_OK;
}


api_ret_t api_free(Api *self) {
	(void)self;
	return API_RET_OK;
}

#endif
