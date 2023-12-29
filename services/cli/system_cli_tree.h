/*
 * uMeshFw Command Line Interface configuration tree
 *
 * Copyright (C) 2017, Marek Koza, qyx@krtko.org
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

#include "treecli_parser.h"

/* Helper defines to make the command tree more clear. */

#define End NULL

#define Subnodes .subnodes = &(const struct treecli_node*[])
#define DSubnodes .dsubnodes = &(const struct treecli_dnode*[])
#define Commands .commands = &(const struct treecli_command*[])
#define Values .values = &(const struct treecli_value*[])

#define Node &(const struct treecli_node)
#define DNode &(const struct treecli_dnode)
#define Command &(const struct treecli_command)
#define Value &(const struct treecli_value)

#define Name .name =
#define Exec .exec =
#define Type .value_type =



extern const struct treecli_node *system_cli_tree;

