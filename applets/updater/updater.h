/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Firmware updater applet
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <interfaces/applet.h>
#include <interfaces/tcp.h>
#include <interfaces/tls.h>
#include <interfaces/flash.h>
#include <configlib.h>
#if defined(CONFIG_APPLET_UPDATER_GUI)
#include <services/gui-wm-fs/gui-wm-fs.h>
#endif

#define UPDATER_URL_LEN 192
#define UPDATER_CRED_LEN 64

/* Applet static configuration, its root exposed as updater.conf. It outlives the applet instances, so the values
 * persist between runs. */
struct updater_conf {
	/* Connections the applet's HTTP client uses, Tcp for http:// and Tls for https:// URLs. Set by the port before
	 * the applet is advertised, NULL if not available. */
	struct {
		Tcp *tcp;
		Tls *tls;
	} net;

	/* Volume the firmware image is written to, the update volume the bootloader flashes the image from. Set by the
	 * port before the applet is advertised, NULL if the device cannot be updated. */
	Flash *volume;

#if defined(CONFIG_APPLET_UPDATER_GUI)
	/* Window manager showing the progress and the error notifications, set by the application once the GUI is up.
	 * NULL if there is none. */
	GuiWmFs *gui;
#endif

	struct {
		ConfiglibValue updater;
		ConfiglibValue url;
		ConfiglibValue username;
		ConfiglibValue password;
	} tree;

	struct {
		/* URL the updates are fetched from (http or https). */
		char url[UPDATER_URL_LEN];
		/* Credentials of the HTTP Basic authentication, used if the server asks for them. Empty username = none. */
		char username[UPDATER_CRED_LEN];
		char password[UPDATER_CRED_LEN];
	} settings;
};

extern const Applet updater;
extern struct updater_conf updater_conf;

/* Set the applet settings to their defaults and build the configuration tree, used as updater.conf_init. */
applet_ret_t updater_conf_init(Applet *self);
