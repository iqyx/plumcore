Import("env")
Import("conf")

import os
import sys
import json
from colorama import Fore, Style

# SBOM support. Sourced from the top-level SConstruct right after lib.SConscript (which defines
# the env.Git/env.Patch/env.Make fetching machinery). This file adds only SBOM concerns:
#
#  - the component registry env["SBOM_COMPONENTS"],
#  - env.Component() for first-party (in-repo) components,
#  - a thin wrapper around env.Git that records each upstream clone as an external component
#    without the fetching machinery having to know about the SBOM,
#  - the `sbom` (generate) and `sbom-check` (validate) targets.
#
# The registry is populated as the per-directory SConscripts are evaluated; because env.Git and
# env.Component only fire inside enabled "if conf[...] == y" guards, the registry ends up
# describing exactly the components the currently configured firmware compiles in. See
# tools/sbom.py for the SPDX serialization/detection logic.

env["SBOM_COMPONENTS"] = []


def _sbom_add(env, entry):
	# A directory's SConscript is evaluated once, but guard against accidental double
	# registration (e.g. a library that calls both Git and Component).
	for e in env["SBOM_COMPONENTS"]:
		if e["kind"] == entry["kind"] and e["name"] == entry["name"] and e.get("dir") == entry.get("dir"):
			return
	env["SBOM_COMPONENTS"].append(entry)


# Wrap env.Git (from lib.SConscript) so every upstream clone also registers an external SBOM
# component. The exact commit, tag and license are resolved later from the clone by the
# generator; here we only capture the clone URL and pinned ref.
_lib_git = env.Git


def Git(self, url, dir=None, branch=None):
	if dir is None:
		dir = self["LIB_DEFAULT_REPO_DIR"]
	_sbom_add(self, {
		"kind": "external",
		"name": os.path.basename(os.getcwd()),
		"dir": os.getcwd(),
		"repo_dir": dir,
		"url": url,
		"branch": branch,
	})
	return _lib_git(url, dir, branch)

env.AddMethod(Git)


def Component(self, name=None):
	# Declare a first-party (in-repo) component with no upstream, for the SBOM only. Does nothing
	# to the build. Its version is derived at generation time from the last commit touching its
	# tracked files (see tools/sbom.py). SCons chdirs into each SConscript's directory, so the
	# current working directory is this component's directory.
	_sbom_add(self, {
		"kind": "internal",
		"name": name if name is not None else os.path.basename(os.getcwd()),
		"dir": os.getcwd(),
	})

env.AddMethod(Component)


# The `sbom` target serializes the registry to an SPDX 2.3 JSON document. The action runs after
# every SConscript has been evaluated, so the registry is fully populated by then. Not built by
# default; run `scons sbom`.

def generate_sbom(target, source, env):
	tools_dir = env.Dir("#/tools").abspath
	if tools_dir not in sys.path:
		sys.path.insert(0, tools_dir)
	import sbom as sbom_gen

	doc = sbom_gen.to_spdx(
		env["SBOM_COMPONENTS"],
		doc_name="plumcore-" + conf.get("PORT_NAME", "firmware"),
		firmware_version=env.get("VERSION", "NOASSERTION"),
	)
	out = str(target[0])
	os.makedirs(os.path.dirname(out), exist_ok=True)
	with open(out, "w") as f:
		json.dump(doc, f, indent=2)
		f.write("\n")
	return None


# Written next to the firmware image (env["PORTFILE"], e.g. bin/plumcore-<port>-<app>-<version>)
# with a .spdx.json extension, so the SBOM is named after the exact image it describes.
sbom_json = env.Command(
	env["PORTFILE"] + ".spdx.json",
	None,
	Action(generate_sbom, env.get("SBOMCOMSTR", "Generating SBOM $TARGET")),
)
env.AlwaysBuild(sbom_json)


# `sbom-check` validates the generated document against the SPDX 2.3 schema using spdx-tools (a
# PDM dev dependency). It is chained after generation so `scons sbom` always validates what it
# just produced; the build fails on any validation issue. If spdx-tools is not installed (dev
# group not synced) the check warns and is skipped rather than breaking the build.
def check_sbom(target, source, env):
	sbom_path = str(source[0])
	try:
		from spdx_tools.spdx.parser.parse_anything import parse_file
		from spdx_tools.spdx.validation.document_validator import validate_full_spdx_document
	except ImportError:
		print(f'{Fore.YELLOW}{Style.BRIGHT}spdx-tools not installed{Style.RESET_ALL}, '
		      f'skipping SBOM validation (run: pdm install -dG dev)')
		with open(str(target[0]), "w") as f:
			f.write("skipped: spdx-tools not installed\n")
		return None

	errors = validate_full_spdx_document(parse_file(sbom_path))
	if errors:
		print(f'{Fore.RED}{Style.BRIGHT}SBOM validation FAILED{Style.RESET_ALL} '
		      f'with {len(errors)} issue(s) in {sbom_path}:')
		for e in errors:
			print("  -", getattr(e, "validation_message", e))
		return 1

	with open(str(target[0]), "w") as f:
		f.write("valid\n")
	return None


sbom_valid = env.Command(
	env["PORTFILE"] + ".spdx.json.valid",
	sbom_json,
	Action(check_sbom, env.get("SBOMCHECKCOMSTR", "Validating SBOM $SOURCE")),
)
env.AlwaysBuild(sbom_valid)

# `scons sbom` generates then validates; `scons sbom-check` runs the validation (regenerating
# first, since the document is always rebuilt).
env.Alias("sbom", [sbom_json, sbom_valid])
env.Alias("sbom-check", sbom_valid)
