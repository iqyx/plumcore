/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Password authenticator for the login manager
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "loginmgr.h"


typedef enum {
	LOGINMGR_AUTH_PASSWORD_RET_OK = 0,
	LOGINMGR_AUTH_PASSWORD_RET_FAILED,
} loginmgr_auth_password_ret_t;


typedef struct {
	const char *login;
	const char *password;
} LoginMgrAuthPassword;


loginmgr_auth_password_ret_t loginmgr_auth_password_init(LoginMgrAuthPassword *self);
loginmgr_auth_password_ret_t loginmgr_auth_password_free(LoginMgrAuthPassword *self);

/**
 * Run the password authenticator on @p console. Prompts for login and password,
 * compares against credentials stored in @p ctx (LoginMgrAuthPassword *).
 * Returns LOGINMGR_RET_OK if accepted, LOGINMGR_RET_FAILED otherwise.
 * Signature matches the loginmgr_channel_conf.create_auth callback type.
 */
loginmgr_ret_t loginmgr_auth_password_run(void *ctx, Stream *console);
