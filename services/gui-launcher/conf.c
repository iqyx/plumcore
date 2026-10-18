/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GUI applet launcher configuration tree
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 *
 * All launcher configuration lives in a single Conf tree whose root is the "launcher" node. The application
 * advertises it as "service launcher", mounting it at /service/launcher:
 *
 *     /service/launcher/autostart  name of the applet launched right after start
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "config.h"
#include <interfaces/conf.h>
#include <configlib.h>

#include "gui-launcher.h"


gui_launcher_ret_t gui_launcher_conf_init(GuiLauncher *self) {
	/* Defaults, overridden once the stored configuration is loaded. */
	self->settings.autostart[0] = '\0';

	configlib_init_map(&self->conf_tree.launcher, "launcher", NULL, CONF_SUBTREE);

	configlib_init(&self->conf_tree.autostart, "autostart");
	configlib_map_string(&self->conf_tree.autostart, self->settings.autostart, sizeof(self->settings.autostart));
	configlib_append(&self->conf_tree.autostart, &self->conf_tree.launcher, CONF_DIR_CHILD);
	self->conf_tree.autostart.flags = CONF_READ | CONF_WRITE;
	configlib_set_default(&self->conf_tree.autostart, (union conf_val){.str = {.buf = (char *)"", .len = 0}});
	configlib_set_constraint(&self->conf_tree.autostart, (union conf_constraint){
		.str.max_len = GUI_LAUNCHER_NAME_MAX - 1,
	});
	configlib_set_description(&self->conf_tree.autostart, "Applet launched right after start",
		"Name of the applet the launcher starts once the stored configuration is loaded, exactly as it is shown "
		"in the launcher. Empty disables the auto-start.");

	return GUI_LAUNCHER_RET_OK;
}
