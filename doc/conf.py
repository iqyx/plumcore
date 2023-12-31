import sphinx_rtd_theme

project = 'plumCore IoT/DAQ framework'
copyright = '2021, Marek Koza'
author = 'Marek Koza'
extensions = [
	'sphinx_rtd_theme',
	'rst2pdf.pdfbuilder',
	'sphinxcontrib.contentui',
	'sphinx_toolbox.confval',
	'sphinx_toolbox.code',
]
numfig = True
autosectionlabel_prefix_document = True
templates_path = ['templates']
exclude_patterns = ['build', 'static', 'lib/duktape', 'lib/zfp']

html_theme = 'sphinx_rtd_theme'
html_title = 'plumCore IoT/DAQ framework'
html_logo = 'static/plum.png'
html_theme_options = {
	'globaltoc_collapse': False,
 	'globaltoc_includehidden': True,
	'prev_next_buttons_location': None,
	'sticky_navigation': True,
}
html_show_sourcelink = False
html_static_path = ['static']
html_use_index = True
html_css_files = [
	'custom.css',
	'https://fonts.googleapis.com/css2?family=Material+Icons',
]

rst_prolog = """
.. |clearer| raw:: html

   <div style="clear: both"></div>

.. role:: tag-button
.. role:: material-icons
"""

