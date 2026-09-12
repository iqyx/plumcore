# SPDX-License-Identifier: GPL-3.0-or-later
#
# SPDX SBOM generator for plumCore firmware (EU CRA).
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""SPDX 2.3 SBOM generator for plumCore firmware.

Pure auto-detection: consumes the component registry that env.Git()/env.Component() populate
during the SCons build (sbom.SConscript) and resolves every component's version, source
location and license directly from the working tree - git metadata for the version/commit and
license files (external) or SPDX-License-Identifier header tags (internal). Nothing is
hand-maintained; anything that cannot be detected is emitted as NOASSERTION rather than guessed.

Only first-level dependencies are described: each lib/ entry and the RTOS as one component, plus
the first-party in-repo components. Build/host-only Python packages never enter the firmware and
are not included.
"""

import os
import re
import subprocess
import time
import uuid


def _git(args, cwd):
	"""Run a git command in `cwd`, returning stripped stdout or None on any failure."""
	try:
		out = subprocess.run(
			["git"] + args,
			cwd=cwd,
			check=True,
			stdout=subprocess.PIPE,
			stderr=subprocess.DEVNULL,
		)
		return out.stdout.decode(errors="ignore").strip()
	except (subprocess.CalledProcessError, FileNotFoundError, OSError):
		return None


# --- license auto-detection --------------------------------------------------

# Ordered most-specific first: an entry matches when every marker substring is present in the
# license text (case-insensitive). The first match wins, so e.g. the LGPL entry must precede the
# GPL one and BSD-3 must precede BSD-2.
_LICENSE_MARKERS = [
	("Apache-2.0", ["apache license", "version 2.0"]),
	("LGPL-3.0-or-later", ["lesser general public license", "version 3"]),
	("GPL-3.0-or-later", ["general public license", "version 3"]),
	("GPL-2.0-or-later", ["general public license", "version 2"]),
	("BSD-3-Clause", ["redistribution and use", "neither the name"]),
	("BSD-2-Clause", ["redistribution and use"]),
	("MIT", ["permission is hereby granted, free of charge"]),
	("ISC", ["permission to use, copy, modify, and distribute"]),
	("0BSD", ["permission to use, copy, modify, and/or distribute this software for any"]),
	("Zlib", ["this software is provided 'as-is'"]),
	("CC0-1.0", ["cc0", "public domain"]),
	("Unlicense", ["this is free and unencumbered software released into the public domain"]),
]

_LICENSE_FILE_RE = re.compile(r"^(LICEN[CS]E|COPYING|COPYRIGHT)", re.IGNORECASE)
_SPDX_TAG_RE = re.compile(r"SPDX-License-Identifier:\s*(.+?)\s*(?:\*/|$)", re.MULTILINE)


def _match_license_text(filepath):
	try:
		with open(filepath, "r", errors="ignore") as f:
			text = f.read().lower()
	except OSError:
		return "NOASSERTION"
	for spdx, markers in _LICENSE_MARKERS:
		if all(m in text for m in markers):
			return spdx
	return "NOASSERTION"


def _best(matches):
	"""Pick the most specific SPDX id among detected matches (earliest in _LICENSE_MARKERS)."""
	matches = [m for m in matches if m != "NOASSERTION"]
	if not matches:
		return "NOASSERTION"
	prio = {spdx: i for i, (spdx, _) in enumerate(_LICENSE_MARKERS)}
	# e.g. prefer LGPL-3.0 over GPL-3.0 when a project ships both COPYING.LGPL3 and COPYING.GPL3.
	return min(matches, key=lambda s: prio.get(s, len(_LICENSE_MARKERS)))


def detect_license_from_dir(path):
	"""Best-effort SPDX id from LICENSE/COPYING files at (or just below) a clone root."""
	if not path or not os.path.isdir(path):
		return "NOASSERTION"
	candidates = []
	for root, dirs, files in os.walk(path):
		depth = root[len(path):].count(os.sep)
		if depth >= 2:
			dirs[:] = []
			continue
		for fn in files:
			if _LICENSE_FILE_RE.match(fn):
				candidates.append(os.path.join(root, fn))
	return _best(_match_license_text(c) for c in candidates)


def detect_license_from_headers(files):
	"""Detect the license of an in-repo component from its source headers.

	First honor an explicit SPDX-License-Identifier tag; otherwise fall back to matching the
	license boilerplate in the header comment (older files predate the SPDX tag convention).
	"""
	sources = [fp for fp in files if fp.endswith((".c", ".h"))]
	heads = []
	for fp in sources:
		try:
			with open(fp, "r", errors="ignore") as f:
				head = f.read(4096)
		except OSError:
			continue
		m = _SPDX_TAG_RE.search(head)
		if m:
			return m.group(1).strip()
		heads.append(head.lower())

	matches = []
	for head in heads:
		for spdx, markers in _LICENSE_MARKERS:
			if all(mk in head for mk in markers):
				matches.append(spdx)
				break
	return _best(matches)


# --- component resolution ----------------------------------------------------

def _github_purl(url, version):
	m = re.search(r"github\.com[:/]+([^/]+)/([^/.]+?)(?:\.git)?/?$", url or "")
	if not m:
		return None
	base = "pkg:github/%s/%s" % (m.group(1), m.group(2))
	return base + "@" + version if version else base


def _resolve_external(entry):
	src = os.path.join(entry["dir"], (entry.get("repo_dir") or "src/").rstrip("/"))
	commit = _git(["rev-parse", "HEAD"], src)
	describe = _git(["describe", "--tags", "--always"], src)
	version = describe or entry.get("branch") or "NOASSERTION"
	purl = _github_purl(entry.get("url"), describe or (commit[:12] if commit else None) or entry.get("branch"))
	return {
		"name": entry["name"],
		"version": version,
		"download": entry.get("url") or "NOASSERTION",
		"license": detect_license_from_dir(src),
		"purl": purl,
		"commit": commit,
	}


def _resolve_internal(entry):
	d = entry["dir"]
	tracked = _git(["ls-files"], d)
	files = [f for f in (tracked.splitlines() if tracked else []) if not f.endswith(".o")]
	version = "NOASSERTION"
	if files:
		# The last commit that touched any tracked file in the component - i.e. the version of the
		# most recently modified file - described relative to the repo's tags.
		last_commit = _git(["log", "-1", "--format=%H", "--"] + files, d)
		if last_commit:
			version = _git(["describe", "--tags", "--always", last_commit], d) or last_commit[:12]
	abs_files = [os.path.join(d, f) for f in files]
	return {
		"name": entry["name"],
		"version": version,
		"download": "NOASSERTION",
		"license": detect_license_from_headers(abs_files),
		"purl": None,
		"commit": None,
	}


def resolve_component(entry):
	"""Normalize one registry entry into name/version/download/license/purl/commit."""
	if entry["kind"] == "external":
		return _resolve_external(entry)
	return _resolve_internal(entry)


# --- SPDX document -----------------------------------------------------------

def _spdxid(name, used):
	base = "SPDXRef-Package-" + re.sub(r"[^A-Za-z0-9.-]", "-", name)
	pid = base
	n = 1
	while pid in used:
		n += 1
		pid = "%s-%d" % (base, n)
	used.add(pid)
	return pid


def to_spdx(components, doc_name="plumcore-firmware", firmware_version="NOASSERTION", namespace=None):
	"""Build an SPDX 2.3 document (as a dict) describing the firmware and its first-level deps."""
	resolved = [resolve_component(c) for c in components]
	resolved.sort(key=lambda r: r["name"].lower())
	created = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
	if namespace is None:
		namespace = "https://spdx.org/spdxdocs/%s-%s" % (doc_name, uuid.uuid4())

	used_ids = set()
	root_id = "SPDXRef-Package-firmware"
	used_ids.add(root_id)
	packages = [{
		"SPDXID": root_id,
		"name": doc_name,
		"versionInfo": firmware_version,
		"downloadLocation": "NOASSERTION",
		"filesAnalyzed": False,
		"licenseConcluded": "NOASSERTION",
		"licenseDeclared": "GPL-3.0-or-later",
		"copyrightText": "NOASSERTION",
		"supplier": "NOASSERTION",
	}]
	relationships = [{
		"spdxElementId": "SPDXRef-DOCUMENT",
		"relationshipType": "DESCRIBES",
		"relatedSpdxElement": root_id,
	}]

	for r in resolved:
		pid = _spdxid(r["name"], used_ids)
		pkg = {
			"SPDXID": pid,
			"name": r["name"],
			"versionInfo": r["version"],
			"downloadLocation": r["download"],
			"filesAnalyzed": False,
			"licenseConcluded": "NOASSERTION",
			"licenseDeclared": r["license"],
			"copyrightText": "NOASSERTION",
			"supplier": "NOASSERTION",
		}
		if r["commit"]:
			pkg["sourceInfo"] = "git commit " + r["commit"]
		if r["purl"]:
			pkg["externalRefs"] = [{
				"referenceCategory": "PACKAGE-MANAGER",
				"referenceType": "purl",
				"referenceLocator": r["purl"],
			}]
		packages.append(pkg)
		relationships.append({
			"spdxElementId": root_id,
			"relationshipType": "DEPENDS_ON",
			"relatedSpdxElement": pid,
		})

	return {
		"spdxVersion": "SPDX-2.3",
		"dataLicense": "CC0-1.0",
		"SPDXID": "SPDXRef-DOCUMENT",
		"name": doc_name,
		"documentNamespace": namespace,
		"creationInfo": {
			"created": created,
			"creators": ["Tool: plumcore-sbom"],
		},
		"packages": packages,
		"relationships": relationships,
	}
