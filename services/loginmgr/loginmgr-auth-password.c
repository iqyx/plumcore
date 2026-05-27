/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Password authenticator for the login manager
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <main.h>
#include <interfaces/stream.h>

#include "lineedit.h"
#include "loginmgr.h"
#include "loginmgr-auth-password.h"

#define MODULE_NAME "loginmgr-auth-password"

#define LINE_BUF_LEN 16
#define FAILURE_DELAY_MS 1000


loginmgr_ret_t loginmgr_auth_password_run(void *ctx, Stream *console) {
	LoginMgrAuthPassword *self = ctx;

	LineEdit le;
	if (lineedit_init(&le, &(const struct lineedit_conf){
		.stream = console,
		.label = "Login:    ",
		.max_len = LINE_BUF_LEN,
		.fg_color = LINEEDIT_FG_COLOR_RED,
		.bg_color = LINEEDIT_BG_COLOR_WHITE,
	}) != LINEEDIT_RET_OK) {
		return LOGINMGR_RET_FAILED;
	}
	lineedit_set_text(&le, "");

	lineedit_ret_t ret = lineedit_run(&le);
	if (ret == LINEEDIT_RET_END) {
		lineedit_free(&le);
		return LOGINMGR_RET_END;
	} else if (ret == LINEEDIT_RET_TIMEOUT) {
		lineedit_free(&le);
		return LOGINMGR_RET_TIMEOUT;
	} else if (ret != LINEEDIT_RET_ENTER) {
		lineedit_free(&le);
		return LOGINMGR_RET_FAILED;
	}
	console->vmt->write(console, "\r\n", 2);
	char saved_login[LINE_BUF_LEN];
	lineedit_get_text(&le, saved_login, sizeof(saved_login));

	le.conf.label = "Password: ";
	le.conf.pwchar = '*';
	lineedit_set_text(&le, "");

	ret = lineedit_run(&le);
	if (ret == LINEEDIT_RET_END) {
		lineedit_free(&le);
		return LOGINMGR_RET_END;
	} else if (ret == LINEEDIT_RET_TIMEOUT) {
		lineedit_free(&le);
		return LOGINMGR_RET_TIMEOUT;
	} else if (ret != LINEEDIT_RET_ENTER) {
		lineedit_free(&le);
		return LOGINMGR_RET_FAILED;
	}
	console->vmt->write(console, "\r\n", 2);
	char password[LINE_BUF_LEN];
	lineedit_get_text(&le, password, sizeof(password));

	bool accepted = (strcmp(saved_login, self->login) == 0) && (strcmp(password, self->password) == 0);

	lineedit_free(&le);

	if (!accepted) {
		const char *msg = "\x1b[31mAuthentication failed.\x1b[0m\r\n\r\n";
		console->vmt->write(console, msg, strlen(msg));
		vTaskDelay(pdMS_TO_TICKS(FAILURE_DELAY_MS));
		return LOGINMGR_RET_FAILED;
	}
	return LOGINMGR_RET_OK;
}


loginmgr_auth_password_ret_t loginmgr_auth_password_init(LoginMgrAuthPassword *self) {
	if (self == NULL) {
		return LOGINMGR_AUTH_PASSWORD_RET_FAILED;
	}
	memset(self, 0, sizeof(LoginMgrAuthPassword));
#ifdef CONFIG_SERVICE_LOGINMGR_PASSWORD_AUTH
	self->login = CONFIG_SERVICE_LOGINMGR_PASSWORD_AUTH_LOGIN;
	self->password = CONFIG_SERVICE_LOGINMGR_PASSWORD_AUTH_PASSWORD;
#else
	self->login = "admin";
	self->password = "admin";
#endif
	return LOGINMGR_AUTH_PASSWORD_RET_OK;
}


loginmgr_auth_password_ret_t loginmgr_auth_password_free(LoginMgrAuthPassword *self) {
	(void)self;
	return LOGINMGR_AUTH_PASSWORD_RET_OK;
}
