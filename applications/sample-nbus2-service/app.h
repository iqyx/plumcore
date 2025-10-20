#pragma once

#include "config.h"

#include <interfaces/stream.h>
#include <interfaces/datagram.h>
#include <services/nbus2/nbus2.h>
#include <services/nbus-flash/nbus-flash.h>


typedef enum {
	APP_RET_OK = 0,
	APP_RET_FAILED,
} app_ret_t;

typedef struct {

	Stream *nbus_stream;
	TaskHandle_t nbus_flash_task;

	Nbus nbus;
	struct nbus_socket *rpc_socket;
	Datagram *rpc_datagram;
	NbusFlash flash_proto;

} App;


/**
 * @brief Prepare the new instance, allocate resources and start
 *        the application task.
 * @param self Preallocated memory for the instance
 * @return APP_RET_OK if the task was started successfully, error otherwise
 */
app_ret_t app_init(App *self);
app_ret_t app_free(App *self);

