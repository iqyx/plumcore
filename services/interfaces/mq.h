/* SPDX-License-Identifier: BSD-2-Clause
 *
 * A generic plumCore message queue interface
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include <types/ndarray.h>


typedef enum {
	/* The function returned without error. */
	MQ_RET_OK = 0,
	
	/* Failed with other unspecified error. */
	MQ_RET_FAILED,

	/* Static buffers are full or dynamic memory allocation failed. */
	MQ_RET_NO_MEM,

	/* Returned by MqSubscriber.receive if the received message did not fit
	 * inside the provided buffer. Otherwise the same as MQ_RET_OK. */
	MQ_RET_OK_TRUNCATED,

	/* The action was not completed within the specified timeout. */
	MQ_RET_TIMEOUT,
} mq_ret_t;

typedef struct mq Mq;

/************************** MQ client ***************************************/

typedef struct mq_client MqClient;

struct mq_client_vmt {
	/**
	 * @brief Subscribe to a topic
	 *
	 * @param filter Topic filter to subscribe to (MQTT format)
	 * @return MQ_RET_NO_MEM if memory allocation failed, MQ_RET_FAILED
	 *         on other error, MQ_RET_OK otherwise.
	 */
	mq_ret_t (*subscribe)(MqClient *self, const char *filter);

	/**
	 * @brief Unsubscribe a topic
	 *
	 * @param filter Topic filter to unsubscribe to (MQTT format)
	 * @return MQ_RET_FAILED on error, MQ_RET_OK otherwise.
	 */
	mq_ret_t (*unsubscribe)(MqClient *self, const char *filter);

	/**
	 * @brief Receive data from the MQ
	 *
	 * Wait for the data to appear on the message queue and receive it.
	 * The MqPublisher instance must be subscribed to the topic we are
	 * interested in first. The function blocks the calling thread.
	 *
	 * @param topic Buffer for a topic of the received message.
	 * @param topic_size Size of the @p topic buffer. The actual received
	 *                   topic may be truncated if it doesn't fit.
	 * @param array Definition of a multidimensional array with
	 *              a preallocated data buffer.
	 * @param tv Time when the message was created.
	 *
	 * @return MQ_RET_OK_TRUNCATED if the received message didn't fit inside
	 *         the preallocated @p array buffer, MQ_RET_FAILED on error or
	 *         MQ_RET_OK otherwise.
	 */
	mq_ret_t (*receive)(MqClient *self, char *topic, size_t topic_size, struct ndarray *array, struct timespec *ts);

	/**
	 * @brief Publish a message to the message queue
	 *
	 * The publish method is used to send a message to the message queue
	 * and deliver it to all connected and subscribed subscribers.
	 *
	 * @param topic A topic of the message to publish to in the MQTT format.
	 * @param ndarray The message to publish (a multidimensional array
	 *                header with a raw message buffer).
	 * @param tv Time when the message was created. The time is preserved
	 *           during the whole processing and is delivered to subscribers
	 *           unchanged.
	 * @return MQ_RET_FAILED when the message couldn't be published,
	 *         MQ_RET_OK_TRUNCATED if the message was delivered truncated
	 *         to one or more subscribers or MQ_RET_OK otherwise.
	 */
	mq_ret_t (*publish)(MqClient *self, const char *topic, const struct ndarray *array, const struct timespec *ts);

	/**
	 * @brief Close the client instance and release all resources
	 *
	 * @param self The instance to close
	 * @return MQ_RET_FAILED if an error occured or MQ_RET_OK otherwise.
	 */
	mq_ret_t (*close)(MqClient *self);

	mq_ret_t (*set_timeout)(MqClient *self, uint32_t timeout_ms);
};

typedef struct mq_client {
	struct mq_client_vmt *vmt;
	Mq *parent;

	/* Pointer to the next instance to ease the implementation a bit. */
	MqClient *next;
} MqClient;

/******************************* MQ *********************************************/

struct mq_vmt {
	MqClient *(*open)(Mq *self);
	
};

typedef struct mq {
	const struct mq_vmt *vmt;
	void *parent;

} Mq;


mq_ret_t mq_client_init(MqClient *self, Mq *parent);
mq_ret_t mq_client_free(MqClient *self);

mq_ret_t mq_init(Mq *self);
mq_ret_t mq_free(Mq *self);


