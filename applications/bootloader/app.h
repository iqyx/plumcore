#pragma once

#include <main.h>
#include <services/chainloader/chainloader.h>


typedef enum {
	APP_RET_OK = 0,
	APP_RET_FAILED,
} app_ret_t;

/*
 * Bootloader application is stateful. Description of the states follows.
 * Pay attention to not reordering the enums as the same ordering is used
 * in the state string table. */
enum bl_state {
	/* State entered after the application is started. */
	BL_STATE_INIT = 0,

	/* Trying to find a suitable firmware image in the flash memory.
	 * Usually this is an ELF image with XIP support. */
	BL_STATE_FIND_APP,

	/* Everything is ready and all state variables are valid.
	 * Application firmware can be booted now. */
	BL_STATE_BOOT,

	/* State entered if all other possibilities to boot the application
	 * failed. Wait a bit in this state and reboot again. */
	BL_STATE_ALL_FAILED,

	/* Signature checking is requested by bootloader configuration. */
	BL_STATE_CHECK_SIGNATURE,
};

typedef struct {
	/* Chainloader is a service accepting an ELF image residing in a memory,
	 * performing various actions on it and eventually booting it. */
	ChainLoader chainloader;

	/* Bootloader state machine state and state. */
	TaskHandle_t task;
	enum bl_state state;
} App;


/**
 * @brief Prepare a new instance, allocate resources and start
 *        the application task. Enter the INIT state automatically.
 *
 * @param self Preallocated memory for the instance
 * @return APP_RET_OK if the task was started successfully, error otherwise
 */
app_ret_t app_init(App *self);

/**
 * @brief Free the bootloader resources.
 *
 * This function does nothing as the bootloader has only two possible
 * exit scenarios - either it boots and it is not required to free any
 * resources because the application firmware takes over the hardware or
 * it fails completely and in this case the hardware is reset.
 */
app_ret_t app_free(App *self);

