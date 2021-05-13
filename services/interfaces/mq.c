/* SPDX-License-Identifier: BSD-2-Clause
 *
 * A generic plumCore message queue interface
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>

#include <interfaces/mq.h>


mq_ret_t mq_client_init(MqClient *self, Mq *parent) {
	memset(self, 0, sizeof(MqClient));
	self->parent = parent;
	return MQ_RET_OK;
}


mq_ret_t mq_client_free(MqClient *self) {
	(void)self;
	return MQ_RET_OK;
}


mq_ret_t mq_init(Mq *self) {
	memset(self, 0, sizeof(Mq));
	return MQ_RET_OK;
}


mq_ret_t mq_free(Mq *self) {
	(void)self;
	return MQ_RET_OK;
}

