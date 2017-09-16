/**
 * uMeshFw login manager
 *
 * Copyright (C) 2015, Marek Koza, qyx@krtko.org
 *
 * This file is part of uMesh node firmware (http://qyx.krtko.org/projects/umesh)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"
#include "hal_module.h"
#include "interface_stream.h"
#include "module_loginmgr.h"
#include "lineedit.h"
#include "port.h"
#include "module_crypto_test.h"

/**
 * @todo login manager cound have two different stream interfaces, one for the CLI console itself and the other one
 *       for a log writer. Before the login manager is initialized, log writer is directed to the same stream interface
 *       (main console) as the login manager. After it becomes initialized, the log writer output is redirected to
 *       the login manager log stream interface. It would transparently forward all output from the log writer to the
 *       main console until a user logs in.
 */


/* Print handler function used by the lineedit library. */
#define MODULE_LOGINMGR_PRINT_HANDLER_OK 0
#define MODULE_LOGINMGR_PRINT_HANDLER_FAILED -1
static int32_t module_loginmgr_print_handler(const char *s, void *ctx) {
	if (u_assert(ctx != NULL)) {
		return MODULE_LOGINMGR_PRINT_HANDLER_FAILED;
	}
	struct module_loginmgr *loginmgr = (struct module_loginmgr *)ctx;

	interface_stream_write(loginmgr->host, (const uint8_t *)s, strlen(s) + 1);

	return MODULE_LOGINMGR_PRINT_HANDLER_OK;
}


/* All login manager state transitions are handler here. */
#define MODULE_LOGINMGR_SET_STATE_OK 0
#define MODULE_LOGINMGR_SET_STATE_FAILED -1
static int32_t module_loginmgr_set_state(struct module_loginmgr *loginmgr, enum module_loginmgr_state state) {
	if (u_assert(loginmgr != NULL)) {
		return MODULE_LOGINMGR_SET_STATE_FAILED;
	}

	switch (state) {
		case LOGINMGR_STATE_BANNER:
			/* Login banner is requested. Nothing special should
			 * happen here, banner printing is handled in the login
			 * manager main task. */
			break;

		case LOGINMGR_STATE_UNAUTH:
			/* Not authenticated state was requested as a result of
			 * failed password authentication. */
			break;

		case LOGINMGR_STATE_LOGIN:
			/* Login prompt is about to be displayed. Lineedit library
			 * is used for this purpose. Set all required parameters
			 * and let the main task handle all input and pass it to
			 * the lineedit library. */
			loginmgr->le_prompt = "Login: ";
			loginmgr->le.pwchar = '\0';
			lineedit_clear(&(loginmgr->le));
			lineedit_refresh(&(loginmgr->le));
			break;

		case LOGINMGR_STATE_PASSWORD:
			/* Same as above apply. */
			loginmgr->le_prompt = "Password: ";
			loginmgr->le.pwchar = '*';
			lineedit_clear(&(loginmgr->le));
			lineedit_refresh(&(loginmgr->le));
			break;

		case LOGINMGR_STATE_CHECK:
			/* Login and password was entered, authentication is
			 * requested. It is handled by the main login manager task */
			break;

		case LOGINMGR_STATE_AUTH:
			/* Authentication was successfull, login manager is set
			 * to the auth state and the state is kept until the
			 * stream is closed. Welcome screen is printed here. */
			module_loginmgr_print_welcome(loginmgr);

			/* Usually a CLI console is used with the login manager.
			 * Send a Ctrl+R to refresh the current line. */
			loginmgr->host_refresh_pending = true;

			/* TODO: remove or print critical log entries only. */
			/* log_cbuffer_print(system_log); */
			break;

		default:
			/* Handle all other state requests by setting not authenticated
			 * state and returning proper error code. */
			loginmgr->state = LOGINMGR_STATE_UNAUTH;
			return MODULE_LOGINMGR_SET_STATE_FAILED;
	};

	/* Set state machine state for all valid requests. */
	loginmgr->state = state;
	return MODULE_LOGINMGR_SET_STATE_OK;
}


static void loginmgr_task(void *p) {
	struct module_loginmgr *loginmgr = (struct module_loginmgr *)p;

	while (1) {
		switch (loginmgr->state) {
			case LOGINMGR_STATE_BANNER: {
				vTaskDelay(1000);
				module_loginmgr_print_handler("\x1b[1m" "\r\nPress any key to activate this console.\r\n\r\n" "\x1b[0m", (void *)loginmgr);
				/* Wait for anything, don't even check the result. */
				uint8_t host_buf[1];
				interface_stream_read(loginmgr->host, host_buf, sizeof(host_buf));
				module_loginmgr_print_handler("\r\n\r\n", (void *)loginmgr);
				module_loginmgr_set_state(loginmgr, LOGINMGR_STATE_LOGIN);
				break;
			}

			case LOGINMGR_STATE_UNAUTH: {
				vTaskDelay(1000);
				module_loginmgr_print_handler("\x1b[31m" "Authentication failed.\r\n\r\n" "\x1b[0m", (void *)loginmgr);
				module_loginmgr_set_state(loginmgr, LOGINMGR_STATE_LOGIN);
				break;
			}

			case LOGINMGR_STATE_LOGIN:
			case LOGINMGR_STATE_PASSWORD: {
				uint8_t host_buf[10];
				int32_t host_len = interface_stream_read(loginmgr->host, host_buf, sizeof(host_buf));

				for (int32_t i = 0; i < host_len; i++) {
					int32_t res = lineedit_keypress(&(loginmgr->le), host_buf[i]);

					if (res == LINEEDIT_ENTER) {
						if (loginmgr->state == LOGINMGR_STATE_LOGIN) {
							/* Save entered login. */
							char *s;
							lineedit_get_line(&(loginmgr->le), &s);
							strlcpy(loginmgr->login, s, sizeof(loginmgr->login));

							module_loginmgr_print_handler("\r\n", (void *)loginmgr);
							module_loginmgr_set_state(loginmgr, LOGINMGR_STATE_PASSWORD);
							break;
						}
						if (loginmgr->state == LOGINMGR_STATE_PASSWORD) {
							/* Password is not saved, it stays in the lineedit context. */
							module_loginmgr_print_handler("\r\n", (void *)loginmgr);
							module_loginmgr_set_state(loginmgr, LOGINMGR_STATE_CHECK);
							break;
						}
					}
				}
				break;
			}

			case LOGINMGR_STATE_CHECK: {
				module_loginmgr_print_handler("checking auth\r\n", (void *)loginmgr);
				vTaskDelay(1000);

				/* Get password from the lineedit context. */
				char *pw;
				lineedit_get_line(&(loginmgr->le), &pw);

				if (!strcmp(loginmgr->login, "admin") && !strcmp(pw, "test")) {
					module_loginmgr_set_state(loginmgr, LOGINMGR_STATE_AUTH);
				} else {
					module_loginmgr_set_state(loginmgr, LOGINMGR_STATE_UNAUTH);
				}
				break;
			}

			case LOGINMGR_STATE_AUTH:
				/* Do nothing, just wait. */
				vTaskDelay(100);
				break;

			default:
				module_loginmgr_set_state(loginmgr, LOGINMGR_STATE_LOGIN);
				break;
		}
	}
}


static int32_t module_loginmgr_read(void *context, uint8_t *buf, uint32_t len) {
	if (u_assert(context != NULL && buf != NULL && len > 0)) {
		return -1;
	}
	struct module_loginmgr *loginmgr = (struct module_loginmgr *)context;

	/* Pass reads directly only if we are in the authenticated state. */
	if (loginmgr->state == LOGINMGR_STATE_AUTH) {
		if (loginmgr->host_refresh_pending) {
			loginmgr->host_refresh_pending = false;
			if (len >= 1) {
				buf[0] = 0x12;
				return 1;
			}
		} else {
			return interface_stream_read(loginmgr->host, buf, len);
		}
	}

	/* Signal end of stream otherwise. That should cause the shell
	 * to be closed. */
	return -1;
}


static int32_t module_loginmgr_read_timeout(void *context, uint8_t *buf, uint32_t len, uint32_t timeout) {
	if (u_assert(context != NULL && buf != NULL && len > 0)) {
		return -1;
	}
	struct module_loginmgr *loginmgr = (struct module_loginmgr *)context;

	/* Pass reads directly only if we are in the authenticated state. */
	if (loginmgr->state == LOGINMGR_STATE_AUTH) {
		if (loginmgr->host_refresh_pending) {
			loginmgr->host_refresh_pending = false;
			if (len >= 1) {
				buf[0] = 0x12;
				return 1;
			}
		} else {
			return interface_stream_read_timeout(loginmgr->host, buf, len, timeout);
		}
	}

	/* Signal end of stream otherwise. That should cause the shell
	 * to be closed. */
	return -1;
}


static int32_t module_loginmgr_write(void *context, const uint8_t *buf, uint32_t len) {
	if (u_assert(context != NULL)) {
		return -1;
	}
	struct module_loginmgr *loginmgr = (struct module_loginmgr *)context;

	/* Pass writes directly only if we are in the authenticated state. */
	if (loginmgr->state == LOGINMGR_STATE_AUTH) {
		/* TODO: write mutex should be probably obtained first. */
		return interface_stream_write(loginmgr->host, buf, len);
	}

	/* Signal end of stream otherwise. That should cause the shell
	 * to be closed. */
	return -1;
}


static int32_t module_loginmgr_prompt_callback(struct lineedit *le, void *ctx) {
	if (u_assert(ctx != NULL && le != NULL)) {
		return -1;
	}

	struct module_loginmgr *loginmgr = (struct module_loginmgr *)ctx;
	module_loginmgr_print_handler("\x1b[32m", ctx);
	module_loginmgr_print_handler(loginmgr->le_prompt, ctx);
	module_loginmgr_print_handler("\x1b[0m", ctx);

	return 0;
}


int32_t module_loginmgr_init(struct module_loginmgr *loginmgr, const char *name, struct interface_stream *host) {
	if (u_assert(loginmgr != NULL)) {
		return MODULE_LOGINMGR_INIT_FAILED;
	}

	memset(loginmgr, 0, sizeof(struct module_loginmgr));
	hal_module_descriptor_init(&(loginmgr->module), name);
	hal_module_descriptor_set_shm(&(loginmgr->module), (void *)loginmgr, sizeof(struct module_loginmgr));

	/* Initialize stream interface for shell interpreter. */
	interface_stream_init(&(loginmgr->iface));
	loginmgr->iface.vmt.context = (void *)loginmgr;
	loginmgr->iface.vmt.read = module_loginmgr_read;
	loginmgr->iface.vmt.read_timeout = module_loginmgr_read_timeout;
	loginmgr->iface.vmt.write = module_loginmgr_write;

	loginmgr->host = host;

	lineedit_init(&(loginmgr->le), 50);
	lineedit_set_print_handler(&(loginmgr->le), module_loginmgr_print_handler, (void *)loginmgr);
	lineedit_set_prompt_callback(&(loginmgr->le), module_loginmgr_prompt_callback, (void *)loginmgr);

	xTaskCreate(loginmgr_task, "loginmgr_task", configMINIMAL_STACK_SIZE + 64, (void *)loginmgr, 1, NULL);
	u_log(system_log, LOG_TYPE_INFO, "%s: module loginmgr initialized on interface '%s'", loginmgr->module.name, host->descriptor.name);

	return MODULE_LOGINMGR_INIT_OK;
}


static void module_loginmgr_string_line(char *s, uint32_t len, uint8_t c) {
	if (u_assert(s != NULL)) {
		return;
	}

	if (len < 2) {
		/* Cannot do that short line. */
		s[0] = '\0';
		return;
	}

	for (uint32_t i = 1; i < len - 1; i++) {
		if (c == 2) {
			s[i] = ' ';
		} else {
			s[i] = '_';
		}
	}
	if (c > 1) {
		s[0] = '|';
		s[len - 1] = '|';
	} else {
		s[0] = ' ';
		s[len - 1] = ' ';
	}
	s[len++] = '\0';
}


static void module_loginmgr_string_overlay(char *s, const char *overlay, uint32_t start) {
	if (u_assert(s != NULL && overlay != NULL)) {
		return;
	}

	if (start >= strlen(s)) {
		return;
	}

	while ((s[start] != '\0') && (*overlay != '\0')) {
		if (*overlay != ' ') {
			s[start] = *overlay;
		}
		overlay++;
		start++;
	}
}


int32_t module_loginmgr_print_welcome(struct module_loginmgr *loginmgr) {
	if (u_assert(loginmgr != NULL)) {
		return MODULE_LOGINMGR_PRINT_WELCOME_FAILED;
	}

	char s[93];
	module_loginmgr_print_handler("\x1b[34m" "\x1b[1m", (void *)loginmgr);

	/* Line 1 */
	module_loginmgr_string_line(s, sizeof(s) - 1, 1);
	module_loginmgr_print_handler(s, (void *)loginmgr);
	module_loginmgr_print_handler("\r\n", (void *)loginmgr);

	/* Line 2 */
	module_loginmgr_string_line(s, sizeof(s) - 1, 2);
	module_loginmgr_string_overlay(s, "     _____         _   _____ ", 2);
	module_loginmgr_print_handler(s, (void *)loginmgr);
	module_loginmgr_print_handler("\r\n", (void *)loginmgr);

	/* Line 3 */
	module_loginmgr_string_line(s, sizeof(s) - 1, 2);
	module_loginmgr_string_overlay(s, " _ _|     |___ ___| |_|   __|_ _ _", 2);
	module_loginmgr_string_overlay(s, PORT_BANNER ", " PORT_NAME " platform", 40);
	module_loginmgr_print_handler(s, (void *)loginmgr);
	module_loginmgr_print_handler("\r\n", (void *)loginmgr);

	/* Line 4 */
	module_loginmgr_string_line(s, sizeof(s) - 1, 2);
	module_loginmgr_string_overlay(s, "| | | | | | -_|_ -|   |   __| | | |", 2);
	module_loginmgr_string_overlay(s, UMESH_VERSION, 40);
	module_loginmgr_print_handler(s, (void *)loginmgr);
	module_loginmgr_print_handler("\r\n", (void *)loginmgr);

	/* Line 5 */
	module_loginmgr_string_line(s, sizeof(s) - 1, 2);
	module_loginmgr_string_overlay(s, "|___|_|_|_|___|___|_|_|__|  |_____|", 2);
	module_loginmgr_string_overlay(s, "build date " UMESH_BUILD_DATE, 40);
	module_loginmgr_print_handler(s, (void *)loginmgr);
	module_loginmgr_print_handler("\r\n", (void *)loginmgr);

	/* Line 6 */
	module_loginmgr_string_line(s, sizeof(s) - 1, 3);
	module_loginmgr_print_handler(s, (void *)loginmgr);
	module_loginmgr_print_handler("\r\n\r\n", (void *)loginmgr);

	module_loginmgr_print_handler("\x1b[0m", (void *)loginmgr);
	return MODULE_LOGINMGR_PRINT_WELCOME_OK;
}
