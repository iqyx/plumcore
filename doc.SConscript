Import("env")

# Documentation build targets. The sources live in doc/ as Markdown and are
# built with zensical (HTML site) and with pandoc + WeasyPrint (single PDF, no
# LaTeX step). These are on-demand aliases only; they are not part of the
# default firmware build.
#
#   scons doc-html    build the HTML site into doc/build/
#   scons doc-pdf     build doc/plumcore.pdf
#   scons doc         build both
#
# The documentation toolchain (zensical, pandoc, weasyprint) must be on PATH,
# which it is when scons is run inside the project environment (pdm run scons).

import base64
import datetime
import glob
import os
import subprocess
import tempfile
import tomllib

docdir = Dir("#doc").srcnode().abspath

# The zensical configuration is the single source of truth: the PDF is the
# nav pages in order, titled with the site name and headed by the theme logo.
config = File("#doc/zensical.toml")
project = tomllib.loads(open(config.srcnode().abspath).read())["project"]


def flatten_nav(nav):
	"""Return the nav's Markdown targets in order. An entry is a bare path or a
	single-key mapping to a path (page) or a list (section)."""
	pages = []
	for entry in nav:
		if isinstance(entry, str):
			pages.append(entry)
		elif isinstance(entry, dict):
			for _title, target in entry.items():
				if isinstance(target, str):
					pages.append(target)
				elif isinstance(target, list):
					pages.extend(flatten_nav(target))
	return pages


pages = [File("#doc/" + page) for page in flatten_nav(project.get("nav", []))]
extra_css = File("#doc/css/extra.css")
print_css = File("#doc/css/print.css")
logo = File("#doc/" + project.get("theme", {}).get("logo", "assets/plum.svg"))
all_md = [File(path) for path in glob.glob(os.path.join(docdir, "**", "*.md"), recursive=True)]


# --- HTML: the whole doc/ tree is rendered by zensical -----------------------

html = env.Command(
	target = File("#doc/build/index.html"),
	source = all_md + [config, extra_css, print_css],
	action = Action("cd %s && zensical build" % docdir, "Building HTML documentation with zensical"),
)
env.Alias("doc-html", html)


# --- PDF: nav pages concatenated by pandoc, rendered by WeasyPrint -----------

def build_pdf(target, source, env):
	"""Render the nav pages into a single PDF via pandoc + WeasyPrint.

	The running page header (logo + project name) is injected before the body
	with the logo embedded as a data URI, so the intermediate HTML pandoc hands
	to WeasyPrint needs no external lookups; print.css lifts it into the page
	margin."""
	site_name = project.get("site_name", "Documentation")
	version = env.get("VERSION", "unknown")
	built = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")

	image = ""
	logo_path = logo.srcnode().abspath
	if os.path.isfile(logo_path):
		data = base64.b64encode(open(logo_path, "rb").read()).decode("ascii")
		image = '<img src="data:image/svg+xml;base64,%s" alt=""/>' % data

	# Two-line project name next to the logo: first word, then the rest.
	words = site_name.split()
	name = words[0] + ("<br/>" + " ".join(words[1:]) if len(words) > 1 else "")

	# Running header (top-left) and footer (bottom-left, version + build stamp),
	# both lifted into page margins by print.css.
	header = tempfile.NamedTemporaryFile("w", suffix=".html", delete=False, encoding="utf-8")
	header.write(
		'<div class="pdf-running-header">%s<span class="pdf-header-name">%s</span></div>\n'
		'<div class="pdf-running-footer">Version %s<br/>Built %s</div>\n'
		% (image, name, version, built)
	)
	header.close()

	command = [
		"pandoc",
		"--from=gfm",
		"--standalone",
		"--embed-resources",
		"--pdf-engine=weasyprint",
		"--toc",
		"--toc-depth=2",
		"--number-sections",
		"--metadata", "title=" + site_name,
		"--metadata", "date=" + datetime.date.today().isoformat(),
		"--resource-path", docdir,
		"--css", extra_css.srcnode().abspath,
		"--css", print_css.srcnode().abspath,
		"--include-before-body", header.name,
		"--output", target[0].abspath,
		*[page.srcnode().abspath for page in pages],
	]
	try:
		return subprocess.run(command, cwd=docdir).returncode
	finally:
		os.unlink(header.name)


pdf = env.Command(
	target = File("#doc/plumcore.pdf"),
	source = pages + [extra_css, print_css, logo, config],
	action = Action(build_pdf, "Building PDF documentation with pandoc + WeasyPrint"),
)
env.Alias("doc-pdf", pdf)

env.Alias("doc", ["doc-html", "doc-pdf"])
