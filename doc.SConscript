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
import re
import subprocess
import tempfile
import tomllib

docdir = Dir("#doc").srcnode().abspath

# The zensical configuration is the single source of truth: the PDF mirrors the
# nav, section for section, titled with the site name and headed by the theme
# logo. The nav tree is linearised into one Markdown document whose heading
# hierarchy reproduces the nav (see nav_body), so the PDF's sections and table
# of contents match the HTML site rather than a flat page list.
config = File("#doc/zensical.toml")
project = tomllib.loads(open(config.srcnode().abspath).read())["project"]


def flatten_nav(nav):
	"""Return the nav's Markdown targets in order (used for dependency tracking).
	An entry is a bare path or a single-key mapping to a path (page) or a list
	(section)."""
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


_FENCE = re.compile(r"^\s*(```+|~~~+)")
_HEADING = re.compile(r"^(#{1,6})(?=\s|$)")


def shift_headings(text, by):
	"""Shift every ATX heading in text down by `by` levels (capped at 6), leaving
	headings inside fenced code blocks untouched, so a page's content nests below
	the heading of its nav section."""
	if by <= 0:
		return text
	out = []
	in_fence = False
	for line in text.splitlines():
		if _FENCE.match(line):
			in_fence = not in_fence
		elif not in_fence:
			match = _HEADING.match(line)
			if match:
				line = "#" * min(len(match.group(1)) + by, 6) + line[match.end(1):]
		out.append(line)
	return "\n".join(out)


def read_page(path):
	return open(os.path.join(docdir, path), encoding="utf-8").read()


def nav_body(nav, depth=1):
	"""Linearise the nav tree into a single Markdown body that mirrors its
	hierarchy. A section title becomes a synthesised heading at its nesting depth
	and each page's own headings are shifted down to nest beneath it. A bare
	index.md as a section's first child is that section's landing page (the
	mkdocs/zensical section-index convention): its own H1 stands in for the
	section heading, so no title is synthesised for it. print.css then starts each
	H1 (top-level chapter) and H2 (section) on a fresh page."""
	parts = []
	for entry in nav:
		if isinstance(entry, str):
			parts.append(shift_headings(read_page(entry), depth - 1))
		elif isinstance(entry, dict):
			for title, target in entry.items():
				if isinstance(target, str):
					parts.append(shift_headings(read_page(target), depth - 1))
				elif isinstance(target, list):
					children = target
					if children and isinstance(children[0], str) \
							and os.path.basename(children[0]) == "index.md":
						parts.append(shift_headings(read_page(children[0]), depth - 1))
						children = children[1:]
					else:
						parts.append("#" * depth + " " + title)
					parts.append(nav_body(children, depth + 1))
	return "\n\n".join(part for part in parts if part.strip())


pages = [File("#doc/" + page) for page in flatten_nav(project.get("nav", []))]
extra_css = File("#doc/css/extra.css")
print_css = File("#doc/css/print.css")
logo = File("#doc/" + project.get("theme", {}).get("logo", "assets/plum.svg"))
front = File("#doc/front-page.md")
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

	# PDF-only cover page (front-page.md, kept out of the zensical nav): convert
	# it to a self-contained HTML fragment, with {{version}} filled in and its
	# logo embedded, so it can be prepended before the body. print.css lays it out
	# as the first page.
	cover = ""
	front_path = front.srcnode().abspath
	if os.path.isfile(front_path):
		markdown = open(front_path, encoding="utf-8").read().replace("{{version}}", version)
		fragment = subprocess.run(
			["pandoc", "--from=gfm", "--to=html", "--embed-resources", "--resource-path", docdir],
			input=markdown, capture_output=True, text=True, cwd=docdir).stdout
		cover = '<section class="pdf-cover">%s</section>\n' % fragment

	# Running header (top-left) and footer (bottom-left, version + build stamp),
	# both lifted into page margins by print.css, then the cover page, then an H1
	# heading for the table of contents (pandoc emits the TOC right after this, so
	# the heading lands above it and picks up the normal H1 styling).
	header = tempfile.NamedTemporaryFile("w", suffix=".html", delete=False, encoding="utf-8")
	header.write(
		'<div class="pdf-running-header">%s<span class="pdf-header-name">%s</span></div>\n'
		'<div class="pdf-running-footer">Version %s<br/>Built %s</div>\n'
		% (image, name, version, built)
	)
	header.write(cover)
	header.write('<h1 class="toc-heading">Contents</h1>\n')
	header.close()

	# The body is the nav tree linearised into one Markdown document, so its
	# heading hierarchy (and hence the PDF's sections and TOC) mirror the nav.
	body = tempfile.NamedTemporaryFile("w", suffix=".md", delete=False, encoding="utf-8")
	body.write(nav_body(project.get("nav", [])))
	body.close()

	command = [
		"pandoc",
		"--from=gfm",
		"--standalone",
		"--embed-resources",
		"--pdf-engine=weasyprint",
		"--toc",
		"--toc-depth=3",
		"--number-sections",
		"--metadata", "title=" + site_name,
		"--metadata", "date=" + datetime.date.today().isoformat(),
		"--resource-path", docdir,
		"--css", extra_css.srcnode().abspath,
		"--css", print_css.srcnode().abspath,
		"--include-before-body", header.name,
		"--output", target[0].abspath,
		body.name,
	]
	try:
		return subprocess.run(command, cwd=docdir).returncode
	finally:
		os.unlink(header.name)
		os.unlink(body.name)


pdf = env.Command(
	target = File("#doc/plumcore.pdf"),
	source = pages + [extra_css, print_css, logo, front, config],
	action = Action(build_pdf, "Building PDF documentation with pandoc + WeasyPrint"),
)
env.Alias("doc-pdf", pdf)

env.Alias("doc", ["doc-html", "doc-pdf"])
