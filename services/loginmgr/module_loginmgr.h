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

#ifndef _MODULE_LOGINMGR_H_
#define _MODULE_LOGINMGR_H_

#include <stdint.h>
#include <stdbool.h>
#include <interfaces/stream.h>
#include "lineedit.h"


#define MODULE_LOGINMGR_LOGIN_LENGTH 16

enum module_loginmgr_state {
	LOGINMGR_STATE_BANNER = 0,
	LOGINMGR_STATE_UNAUTH,
	LOGINMGR_STATE_LOGIN,
	LOGINMGR_STATE_PASSWORD,
	LOGINMGR_STATE_CHECK,
	LOGINMGR_STATE_AUTH,
};

struct module_loginmgr {
	/**
	 * Interface provided for the shell interpreter.
	 */
	Stream iface;

	/**
	 * Interface on which the login manager service is provided
	 * (such as the console serial port)
	 */
	Stream *host;

	/**
	 * Lineedit library context, current command prompt and saved login.
	 */
	struct lineedit le;
	const char *le_prompt;
	char login[MODULE_LOGINMGR_LOGIN_LENGTH];

	/**
	 * Current login manager state machine state.
	 */
	enum module_loginmgr_state state;
	bool host_refresh_pending;
};


int32_t module_loginmgr_init(struct module_loginmgr *loginmgr, const char *name, Stream *host);
#define MODULE_LOGINMGR_INIT_OK 0
#define MODULE_LOGINMGR_INIT_FAILED -1

int32_t module_loginmgr_print_welcome(struct module_loginmgr *loginmgr);
#define MODULE_LOGINMGR_PRINT_WELCOME_OK 0
#define MODULE_LOGINMGR_PRINT_WELCOME_FAILED -1


#endif
