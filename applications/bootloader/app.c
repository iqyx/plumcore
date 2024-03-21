#include <main.h>

#include <services/chainloader/chainloader.h>

#include "app.h"
#include <base64.h>

#define MODULE_NAME "bootloader"


/* String representation of bootloader states. Ordering must be the same
 * as in the corresponding enum. */
static const char *bl_states[] = {
	"init",
	"find-app",
	"boot",
	"all-failed",
	"check-signature",
};


static void bl_set_state(App *self, enum bl_state state) {
	u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("state '%s' -> '%s'"), bl_states[self->state], bl_states[state]);
	self->state = state;
}


static app_ret_t bl_step(App *self) {
	switch (self->state) {
		case BL_STATE_FIND_APP:
			if (chainloader_init(&self->chainloader) == CHAINLOADER_RET_OK &&
			    chainloader_find_elf(&self->chainloader, (uint8_t *)CONFIG_CHAINLOADER_FIND_START, CONFIG_CHAINLOADER_FIND_SIZE, CONFIG_CHAINLOADER_FIND_STEP) == CHAINLOADER_RET_OK
			) {
				bl_set_state(self, BL_STATE_CHECK_SIGNATURE);
			} else {
				u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot find ELF firmware to chainload"));
				bl_set_state(self, BL_STATE_ALL_FAILED);
			}
			break;

		case BL_STATE_BOOT:
			switch (chainloader_boot(&self->chainloader)) {
				case CHAINLOADER_RET_OK:
					/* unreachable */ break;
				case CHAINLOADER_RET_FAILED:
				default:
					bl_set_state(self, BL_STATE_ALL_FAILED); break;
			}
			break;

		case BL_STATE_ALL_FAILED:
			vTaskDelay(1000);
			break;

		case BL_STATE_CHECK_SIGNATURE: {
			const char pubkey_b64[] = CONFIG_BL_PUBKEY;
			size_t keylen = 32;
			uint8_t pubkey[32] = {0};
			base64decode(pubkey_b64, strlen(pubkey_b64), pubkey, &keylen);
			if (keylen != 32) {
				u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("wrong pubkey size %d"), keylen);
				bl_set_state(self, BL_STATE_ALL_FAILED);
				break;
			}
			
			if (chainloader_check_signature(&self->chainloader, pubkey) == CHAINLOADER_RET_OK) {
				bl_set_state(self, BL_STATE_BOOT);
			} else {
				bl_set_state(self, BL_STATE_ALL_FAILED);
			}
			break;
		}

		case BL_STATE_INIT:
		default:
			bl_set_state(self, BL_STATE_FIND_APP);
	}
	return APP_RET_OK;
}


static void bl_task(void *p) {
	App *self = p;
	while (true) {
		bl_step(self);
	}
	vTaskDelete(NULL);
}


app_ret_t app_init(App *self) {
	memset(self, 0, sizeof(App));

	xTaskCreate(bl_task, "app", configMINIMAL_STACK_SIZE + 2048, (void *)self, 1, &(self->task));
	if (self->task == NULL) {
		return APP_RET_FAILED;
	}

	return APP_RET_OK;
}


app_ret_t app_free(App *self) {
	(void)self;
	return APP_RET_OK;
}
