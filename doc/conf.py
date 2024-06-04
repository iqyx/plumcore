import sphinx_rtd_theme

project = 'plumCore DAQ framework'
copyright = '2021-2024, Marek Koza'
author = 'Marek Koza'
extensions = [
	'sphinx_rtd_theme',
	'sphinxcontrib.contentui',
	'sphinx_toolbox.confval',
	'sphinx_toolbox.code',
]
default_role = 'any'
numfig = True
autosectionlabel_prefix_document = True
templates_path = ['templates']
exclude_patterns = ['build', 'static', 'lib/duktape', 'lib/zfp']

html_theme = 'sphinx_rtd_theme'
html_title = 'plumCore DAQ framework'
html_logo = '_static/plum.svg'
html_theme_options = {
	'globaltoc_collapse': False,
 	'globaltoc_includehidden': True,
	'prev_next_buttons_location': None,
	'sticky_navigation': True,
}
html_show_sourcelink = False
html_static_path = ['_static']
html_use_index = True
html_css_files = [
	'custom.css',
]

rst_prolog = """
.. |clearer| raw:: html

   <div style="clear: both"></div>

.. |plum| raw:: html

   <img style="position: relative; top: -0.15em; height: 1em; width: auto;" src="/_static/plum.svg">

.. role:: tag-button
.. role:: material-icons
"""
