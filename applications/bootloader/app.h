#pragma once

#include <services/chainloader/chainloader.h>

#include "config.h"

typedef enum {
	APP_RET_OK = 0,
	APP_RET_FAILED,
} app_ret_t;

enum bl_state {
	BL_STATE_INIT = 0,
	BL_STATE_FIND_APP,
	BL_STATE_BOOT,
	BL_STATE_ALL_FAILED,
};

typedef struct {

	ChainLoader chainloader;

	enum bl_state state;

} App;


/**
 * @brief Prepare the new instance, allocate resources and start
 *        the application task.
 * @param self Preallocated memory for the instance
 * @return APP_RET_OK if the task was started successfully, error otherwise
 */
app_ret_t app_init(App *self);
app_ret_t app_free(App *self);

