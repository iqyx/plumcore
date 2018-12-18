/*
 * uMeshFw Command Line Interface module
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

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "interface_stream.h"
#include "treecli_shell.h"

#include "interfaces/fs.h"


typedef enum {
	SERVICE_CLI_RET_OK = 0,
	SERVICE_CLI_RET_FAILED = -1,
} service_cli_ret_t;


typedef struct {
	struct interface_stream *stream;
	struct treecli_shell sh;
	IFs *fs;
	IFsFile log_file;
	bool log_file_opened;
} ServiceCli;

extern const struct treecli_node *ucli;


int32_t module_cli_output(const char *s, void *ctx);
service_cli_ret_t service_cli_init(ServiceCli *self, struct interface_stream *stream, const struct treecli_node *root);
service_cli_ret_t service_cli_start(ServiceCli *self);
service_cli_ret_t service_cli_stop(ServiceCli *self);
service_cli_ret_t service_cli_free(ServiceCli *self);
service_cli_ret_t service_cli_start_out_logging(ServiceCli *self, IFs *fs, const char *filename);
service_cli_ret_t service_cli_stop_out_logging(ServiceCli *self);
service_cli_ret_t service_cli_load_file(ServiceCli *self, IFs *fs, const char *filename);

