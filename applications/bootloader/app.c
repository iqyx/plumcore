#include <main.h>

#include <services/chainloader/chainloader.h>

#include "app.h"

#define MODULE_NAME "bootloader"


static const char *bl_states[] = {
	"init",
	"find-app",
	"boot",
	"all-failed",
};


static void bl_set_state(App *self, enum bl_state state) {
	u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("state '%s' -> '%s'"), bl_states[self->state], bl_states[state]);
	self->state = state;

}


static app_ret_t bl_step(App *self) {
	switch (self->state) {
		case BL_STATE_FIND_APP:
			chainloader_init(&self->chainloader);
			if (chainloader_find_elf(&self->chainloader, CONFIG_CHAINLOADER_FIND_START, CONFIG_CHAINLOADER_FIND_SIZE, CONFIG_CHAINLOADER_FIND_STEP) != CHAINLOADER_RET_OK) {
				u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot find ELF firmware to chainload"));
				bl_set_state(self, BL_STATE_ALL_FAILED);
			} else {
				bl_set_state(self, BL_STATE_BOOT);
			}
			break;

		case BL_STATE_BOOT:
			chainloader_boot(&self->chainloader);
			break;

		case BL_STATE_ALL_FAILED:
			break;

		case BL_STATE_INIT:
		default:
			bl_set_state(self, BL_STATE_FIND_APP);
	}
	return APP_RET_OK;
}


app_ret_t app_init(App *self) {
	memset(self, 0, sizeof(App));

	while (true) {
		bl_step(self);
	}

	return APP_RET_OK;
}


app_ret_t app_free(App *self) {
	(void)self;
	return APP_RET_OK;
}
