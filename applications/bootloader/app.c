#include <main.h>

#include <libopencm3/cm3/scb.h>
#include <services/chainloader/chainloader.h>
#include <interfaces/flash.h>

#include "app.h"
#include <base64.h>

#define MODULE_NAME "bl"


/* String representation of bootloader states. Ordering must be the same
 * as in the corresponding enum. */
static const char *bl_states[] = {
	"init",
	"find-app",
	"boot",
	"all-failed",
	"check-signature",
	"find-update",
	"validate-update",
	"flash-update",
	"disable-update",
	"reset",
};


static void bl_set_state(App *self, enum bl_state state) {
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("\x1b[1mstate '%s' -> '%s'"), bl_states[self->state], bl_states[state]);
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
			/* Give serial drivers some time to correctly output buffered log messages. */
			vTaskDelay(100);
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
				u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("signature check OK"));
				bl_set_state(self, BL_STATE_BOOT);
			} else {
				bl_set_state(self, BL_STATE_ALL_FAILED);
			}
			break;
		}

		case BL_STATE_FIND_UPDATE: {
			Flash *target = NULL;
			if (iservicelocator_query_name_type(locator, "app", ISERVICELOCATOR_TYPE_FLASH, (Interface **)&target) != ISERVICELOCATOR_RET_OK) {
				u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("no update target found, skipping update check"));
				bl_set_state(self, BL_STATE_FIND_APP);
			}

			Flash *update = NULL;
			if (iservicelocator_query_name_type(locator, "update", ISERVICELOCATOR_TYPE_FLASH, (Interface **)&update) != ISERVICELOCATOR_RET_OK) {
				u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("no update partition found, skipping update check"));
				bl_set_state(self, BL_STATE_FIND_APP);
			}

			if (flash_updater_init(&self->updater, target) != FLASH_UPDATER_RET_OK ||
			    flash_updater_set_source_flash(&self->updater, update) != FLASH_UPDATER_RET_OK) {
				bl_set_state(self, BL_STATE_FIND_APP);

			}

			Stream *console = NULL;
			if (iservicelocator_query_name_type(locator, "console", ISERVICELOCATOR_TYPE_STREAM, (Interface **)&console) == ISERVICELOCATOR_RET_OK) {
				/* Set only if found. */
				if (flash_updater_set_console(&self->updater, console) != FLASH_UPDATER_RET_OK) {
					bl_set_state(self, BL_STATE_FIND_APP);
				}
			}

			bl_set_state(self, BL_STATE_VALIDATE_UPDATE);
			break;
		}

		case BL_STATE_VALIDATE_UPDATE: {
			const char pubkey_b64[] = CONFIG_BL_PUBKEY;
			size_t keylen = 32;
			uint8_t pubkey[32] = {0};
			base64decode(pubkey_b64, strlen(pubkey_b64), pubkey, &keylen);
			if (keylen != 32) {
				u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("wrong pubkey size %d"), keylen);
				bl_set_state(self, BL_STATE_ALL_FAILED);
				break;
			}

			if (flash_updater_validate_source(&self->updater) == FLASH_UPDATER_RET_OK &&
			    flash_updater_find_signature(&self->updater) == FLASH_UPDATER_RET_OK &&
			    flash_updater_check_signature(&self->updater, pubkey) == FLASH_UPDATER_RET_OK) {
				/* Continue with the update process. */
				bl_set_state(self, BL_STATE_FLASH_UPDATE);
				break;
			}
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("cannot validate update image, continuing boot"));
			bl_set_state(self, BL_STATE_FIND_APP);
			break;
		}

		case BL_STATE_FLASH_UPDATE: {
			/* If something failed, we cannot do more. */
			flash_updater_write(&self->updater);
			bl_set_state(self, BL_STATE_DISABLE_UPDATE);

			break;
		}

		case BL_STATE_DISABLE_UPDATE: {
			flash_updater_disable_update(&self->updater);
			bl_set_state(self, BL_STATE_FIND_APP);

			break;
		}


		case BL_STATE_RESET: {
			SCB_AIRCR = (SCB_AIRCR_VECTKEY | SCB_AIRCR_SYSRESETREQ);
			while (true) {
				;
			}

			break;
		}

		case BL_STATE_INIT:
		default:
			bl_set_state(self, BL_STATE_FIND_UPDATE);
	}
	return APP_RET_OK;
}


static void bl_task(void *p) {
	App *self = p;
	self->state = BL_STATE_INIT;
	while (true) {
		bl_step(self);
	}
	vTaskDelete(NULL);
}


app_ret_t app_init(App *self) {
	memset(self, 0, sizeof(App));

	xTaskCreate(bl_task, "app", configMINIMAL_STACK_SIZE + 3072, (void *)self, 1, &(self->task));
	if (self->task == NULL) {
		return APP_RET_FAILED;
	}

	return APP_RET_OK;
}


app_ret_t app_free(App *self) {
	(void)self;
	return APP_RET_OK;
}
