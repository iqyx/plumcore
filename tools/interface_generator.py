import yaml
import sys
import os

iface_license = """
/*
 * %s
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
"""

autogenerated_note = "/* Do not edit this file directly, it is autogenerated. */"

interface_header = """
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "interface.h"

"""

interface_impl_header = """
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "u_assert.h"
#include "interface.h"
"""

with open(sys.argv[1], "r") as f:
	iface = yaml.load(f.read())

name = iface.get("name")

file_h = ""
file_h += iface_license % iface.get("description") + "\n"
file_h += autogenerated_note + "\n"
file_h += interface_header + "\n"

if iface.get("depends"):
	for d in iface["depends"]:
		file_h += "#include \"interfaces/" + d.lower() + ".h\"\n";


if iface.get("return-values"):
	file_h += "typedef enum {\n"

	for return_value in iface["return-values"]:
		file_h += "\tI" + name.upper() + "_RET_" + return_value.upper() + ",\n"

	file_h += "} i" + name.lower() + "_ret_t;\n"
	file_h += "\n"

if iface.get("enums"):
	for (enum, enum_values) in iface["enums"].items():
		file_h += "enum i" + name.lower() + "_" + enum.lower() + " {\n"
		for enum_value in enum_values:
			file_h += "\tI" + name.upper() + "_" + enum.upper() + "_" + enum_value.upper() + ",\n"
		file_h += "};\n\n"

if iface.get("methods"):
	iface["methods"]["init"] = None
	iface["methods"]["free"] = None

	file_h += "struct i" + name.lower() + "_vmt {\n"

	for (method, parameters) in iface["methods"].items():
		if method == "init" or method == "free":
			continue

		m = "i" + name.lower() + "_ret_t (*" + method + ")(void *context"
		if parameters:
			for param in parameters:
				m += ", " + param["type"] + " " + param["name"]
		m += ");"
		file_h += "\t" + m + "\n"

	file_h += "\tvoid *context;\n";
	file_h += "};\n\n"

file_h += "typedef struct {\n"
file_h += "\tInterface interface;\n"
file_h += "\tstruct i" + name.lower() + "_vmt vmt;\n"
file_h += "} I" + name + ";\n\n\n"

for (method, parameters) in iface["methods"].items():
	m = "i" + name.lower() + "_ret_t i" + name.lower() + "_" + method + "(I" + name + " *self"
	if parameters:
		for param in parameters:
			m += ", " + param["type"] + " " + param["name"]
	m += ");"
	file_h += m + "\n"

file_c = ""

file_c += iface_license % iface.get("description") + "\n"
file_c += autogenerated_note + "\n"
file_c += interface_impl_header + "\n"

file_c += "#include \"" + name.lower() + ".h\"\n\n"

file_c += "i" + name.lower() + "_ret_t i" + name.lower() + "_init(I" + name + " *self) {\n"
file_c += "\tif(u_assert(self != NULL)) {\n"
file_c += "\t\treturn I" + name.upper() + "_RET_FAILED;\n"
file_c += "\t}\n\n"

file_c += "\tmemset(self, 0, sizeof(I" + name + "));\n"
file_c += "\tuhal_interface_init(&self->interface);\n\n"
file_c += "\treturn I" + name.upper() + "_RET_OK;\n"
file_c += "}\n\n\n"

file_c += "i" + name.lower() + "_ret_t i" + name.lower() + "_free(I" + name + " *self) {\n";
file_c += "\tif(u_assert(self != NULL)) {\n"
file_c += "\t\treturn I" + name.upper() + "_RET_FAILED;\n"
file_c += "\t}\n\n"
file_c += "\treturn I" + name.upper() + "_RET_OK;\n"
file_c += "}\n\n\n"


for (method, parameters) in iface["methods"].items():
	# Those methods are specific
	if method == "init" or method == "free":
		continue

	m = "i" + name.lower() + "_ret_t i" + name.lower() + "_" + method + "(I" + name + " *self"
	if parameters:
		for param in parameters:
			m += ", " + param["type"] + " " + param["name"]
	m += ") {"
	file_c += m + "\n"
	file_c += "\tif (self->vmt." + method + " != NULL) {\n"

	p = ""
	if parameters:
		for param in parameters:
			p += ", " + param["name"]

	file_c += "\t\treturn self->vmt." + method + "(self->vmt.context" + p + ");\n"

	file_c += "\t}\n\n"
	file_c += "\treturn I" + name.upper() + "_RET_FAILED;\n"
	file_c += "}\n\n\n"

d = os.path.dirname(sys.argv[1]) + "/"

with open(d + name.lower() + ".h", "w") as f:
	f.write(file_h)

with open(d + name.lower() + ".c", "w") as f:
	f.write(file_c)

