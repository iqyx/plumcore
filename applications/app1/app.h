#pragma once

#include "config.h"

typedef enum {
	APP_RET_OK = 0,
	APP_RET_FAILED,
} app_ret_t;

typedef struct {
} App;


/**
 * @brief Prepare the new instance, allocate resources and start
 *        the application task.
 * @param self Preallocated memory for the instance
 * @return APP_RET_OK if the task was started successfully, error otherwise
 */
app_ret_t app_init(App *self);
app_ret_t app_free(App *self);

