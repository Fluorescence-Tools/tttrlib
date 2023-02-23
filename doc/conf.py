# -*- coding: utf-8 -*-
#
# Configuration file for the Sphinx documentation builder.
#
# This file does only contain a selection of the most common options. For a
# full list see the documentation:
# http://www.sphinx-doc.org/en/master/config

# -- Path setup --------------------------------------------------------------

# If extensions (or modules to document with autodoc) are in another directory,
# add these directories to sys.path here. If the directory is relative to the
# documentation root, use os.path.abspath to make it absolute, like shown here.
#
import os
import subprocess
import sys
import warnings
import re
from datetime import datetime
from pathlib import Path

# If extensions (or modules to document with autodoc) are in another
# directory, add these directories to sys.path here. If the directory
# is relative to the documentation root, use os.path.abspath to make it
# absolute, like shown here.
sys.path.insert(0, os.path.abspath('sphinxext'))

import sphinx_gallery

# -- General configuration ---------------------------------------------------
root_doc = 'contents'

# Add any Sphinx extension module names here, as strings. They can be
# extensions coming with Sphinx (named 'sphinx.ext.*') or your custom
# ones.
extensions = [
    'sphinx.ext.autodoc',
    'sphinx.ext.autosummary',
    'numpydoc',
    'nbsphinx',
    'sphinx_gallery.gen_gallery',
    'sphinx.ext.doctest',
    'sphinx.ext.intersphinx',
    'sphinx.ext.imgconverter',
    'add_toctree_functions',
    'matplotlib.sphinxext.plot_directive',
    'sphinx.ext.autosectionlabel'
]

# this is needed for some reason...
# see https://github.com/numpy/numpydoc/issues/69
numpydoc_class_members_toctree = False

# For maths, use mathjax by default and svg if NO_MATHJAX env variable is set
# (useful for viewing the doc offline)
if os.environ.get('NO_MATHJAX'):
    extensions.append('sphinx.ext.imgmath')
    imgmath_image_format = 'svg'
    mathjax_path = ''
else:
    extensions.append('sphinx.ext.mathjax')
    mathjax_path = ('https://cdn.jsdelivr.net/npm/mathjax@3/es5/'
                    'tex-chtml.js')

autodoc_default_options = {
    'members': True,
    'inherited-members': True
}

# # Add any paths that contain templates here, relative to this directory.
templates_path = ['templates']

# generate autosummary even if no references
autosummary_generate = True

# The suffix of source filenames.
source_suffix = '.rst'

# -- Project information -----------------------------------------------------
project = u'tttrlib'
copyright = (
    f'2021 - {datetime.now().year}, tttrlib developers (BSD License)'
)
import tttrlib
version = tttrlib.__version__

# List of patterns, relative to source directory, that match files and
# directories to ignore when looking for source files.
# This pattern also affects html_static_path and html_extra_path.
exclude_patterns = [u'_build', 'Thumbs.db', '.DS_Store']

# The reST default role (used for this markup: `text`) to use for all
# documents.
default_role = 'literal'

# If true, '()' will be appended to :func: etc. cross-reference text.
add_function_parentheses = False

# The name of the Pygments (syntax highlighting) style to use.
pygments_style = 'sphinx'

# -- Options for HTML output -------------------------------------------------

# The theme to use for HTML and HTML Help pages.  Major themes that come with
# Sphinx are currently 'default' and 'sphinxdoc'.
on_rtd = os.environ.get('READTHEDOCS') == 'True'
if on_rtd:
    try:
        import sphinx_rtd_theme
    except ImportError:
        pass  # assume we have sphinx >= 1.3
    else:
        html_theme_path = sphinx_rtd_theme.get_html_theme_path()
    html_theme = 'sphinx_rtd_theme'
else:
    # Add any paths that contain custom themes here, relative to this directory.
    html_theme_path = ['themes']
    html_theme = 'scikit-learn-modern'
    # Theme options are theme-specific and customize the look and feel of a theme
    # further.  For a list of options available for each theme, see the
    # documentation.
    html_theme_options = {'google_analytics': True,
                          'mathjax_path': mathjax_path}

# The name for this set of Sphinx documents.  If None, it defaults to
# "<project> v<release> documentation".
#html_title = None

# A shorter title for the navigation bar.  Default is the same as html_title.
html_short_title = 'tttrlib'

# The name of an image file (relative to this directory) to place at the top
# of the sidebar.
html_logo = 'logos/tttrlib-logo-small.png'

# The name of an image file (within the static path) to use as favicon of the
# docs.  This file should be a Windows icon file (.ico) being 16x16 or 32x32
# pixels large.
html_favicon = 'logos/favicon.ico'

# Add any paths that contain custom static files (such as style sheets) here,
# relative to this directory. They are copied after the builtin static files,
# so a file named "default.css" will overwrite the builtin "default.css".
html_static_path = []

# Additional templates that should be rendered to pages, maps page names to
# template names.
html_additional_pages = {
    'index': 'index.html',
    'documentation': 'documentation.html'}  # redirects to index

# If false, no module index is generated.
html_domain_indices = False

# If false, no index is generated.
html_use_index = False

# Output file base name for HTML help builder.
htmlhelp_basename = 'tttrlibdoc'

# If true, the reST sources are included in the HTML build as _sources/name.
html_copy_source = True

# Adds variables into templates
html_context = {}
# finds latest release highlights and places it into HTML context for
# index.html
release_highlights_dir = Path("..") / "examples" / "release_highlights"
# Finds the highlight with the latest version number
latest_highlights = sorted(release_highlights_dir.glob(
    "plot_release_highlights_*.py"))[-1]
latest_highlights = latest_highlights.with_suffix('').name
html_context["release_highlights"] = \
    f"examples/release_highlights/{latest_highlights}"

# get version from higlight name assuming highlights have the form
# plot_release_highlights_0_22_0
highlight_version = ".".join(latest_highlights.split("_")[-3:-1])
html_context["release_highlights_version"] = highlight_version

# -- Options for LaTeX output ------------------------------------------------
latex_elements = {
    # The paper size ('letterpaper' or 'a4paper').
    # 'papersize': 'letterpaper',

    # The font size ('10pt', '11pt' or '12pt').
    # 'pointsize': '10pt',

    # Additional stuff for the LaTeX preamble.
    'preamble': r"""
        \usepackage{amsmath}\usepackage{amsfonts}\usepackage{bm}
        \usepackage{morefloats}\usepackage{enumitem} \setlistdepth{10}
        \let\oldhref\href
        \renewcommand{\href}[2]{\oldhref{#1}{\hbox{#2}}}
        """
}

# Grouping the document tree into LaTeX files. List of tuples
# (source start file, target name, title, author, documentclass
# [howto/manual]).
latex_documents = [('contents', 'user_guide.tex', 'scikit-learn user guide',
                    'scikit-learn developers', 'manual'), ]

# The name of an image file (relative to this directory) to place at the top of
# the title page.
latex_logo = "logos/tttrlib-logo.png"

# Documents to append as an appendix to all manuals.
# latex_appendices = []

# If false, no module index is generated.
latex_domain_indices = False

trim_doctests_flags = True

# intersphinx configuration
intersphinx_mapping = {
    'python': ('https://docs.python.org/{.major}'.format(
        sys.version_info), None),
    'numpy': ('https://numpy.org/doc/stable', None),
    'scipy': ('https://docs.scipy.org/doc/scipy/reference', None),
    'matplotlib': ('https://matplotlib.org/', None),
    'pandas': ('https://pandas.pydata.org/pandas-docs/stable/', None),
    'joblib': ('https://joblib.readthedocs.io/en/latest/', None),
    'seaborn': ('https://seaborn.pydata.org/', None),
}


class SubSectionTitleOrder:
    """Sort example gallery by title of subsection.
    Assumes README.rst exists for all subsections and uses the subsection with
    dashes, '---', as the adornment.
    """
    def __init__(self, src_dir):
        self.src_dir = src_dir
        self.regex = re.compile(r"^([\w ]+)\n-", re.MULTILINE)

    def __repr__(self):
        return '<%s>' % (self.__class__.__name__,)

    def __call__(self, directory):
        src_path = os.path.normpath(os.path.join(self.src_dir, directory))

        # Forces Release Highlights to the top
        if os.path.basename(src_path) == "release_highlights":
            return "0"

        readme = os.path.join(src_path, "README.rst")

        try:
            with open(readme, 'r') as f:
                content = f.read()
        except FileNotFoundError:
            return directory

        title_match = self.regex.search(content)
        if title_match is not None:
            return title_match.group(1)
        return directory


# This is largely from
# https://github.com/scikit-learn/scikit-learn/blob/master/doc/conf.py
sphinx_gallery_conf = {
    'doc_module': 'tttrlib',
    'show_memory': False,
    'examples_dirs': ['../examples'],
    'gallery_dirs': ['examples'],
    'subsection_order': SubSectionTitleOrder('../examples'),
    # avoid generating too many cross links
    'inspect_global_variables': False,
    'remove_config_comments': True,
    'ignore_pattern': r'^(?!_.*$)*.py' # match files ending with .py not starting with _
}

# The following dictionary contains the information used to create the
# thumbnails for the front page of the scikit-learn home page.
# key: first image in set
# values: (number of plot in set, height of thumbnail)
carousel_thumbs = {
    'sphx_glr_plot_single_molecule_mcs_001.png': 600,
    'sphx_glr_plot_imaging_representations_001.png': 600,
    'sphx_glr_plot_gated_correlation_001': 600,
    'sphx_glr_plot_imaging_ics_fit_001': 600,
    'sphx_glr_plot_decay_fit_irf_bg_001': 600
}



def make_carousel_thumbs(app, exception):
    """produces the final resized carousel images"""
    if exception is not None:
        return
    print('Preparing carousel images')

    image_dir = os.path.join(app.builder.outdir, '_images')
    for glr_plot, max_width in carousel_thumbs.items():
        image = os.path.join(image_dir, glr_plot)
        if os.path.exists(image):
            c_thumb = os.path.join(image_dir, glr_plot[:-4] + '_carousel.png')
            sphinx_gallery.gen_rst.scale_image(image, c_thumb, max_width, 190)


def filter_search_index(app, exception):
    if exception is not None:
        return

    # searchindex only exist when generating html
    if app.builder.name != 'html':
        return

    print('Removing methods from search index')

    searchindex_path = os.path.join(app.builder.outdir, 'searchindex.js')
    with open(searchindex_path, 'r') as f:
        searchindex_text = f.read()

    searchindex_text = re.sub(r'{__init__.+?}', '{}', searchindex_text)
    searchindex_text = re.sub(r'{__call__.+?}', '{}', searchindex_text)

    with open(searchindex_path, 'w') as f:
        f.write(searchindex_text)


breathe_projects = {}
# latex and breathe do not play very well together. Therefore,
# breathe is only used for the webpage.
# compatible with readthedocs online builder and local builder
if sys.argv[0].endswith('sphinx-build') and \
        ('html' in sys.argv or sys.argv[-1] == '_build/html'):
    subprocess.call('doxygen', shell=True)
    breathe_projects['tttrlib'] = './_build/xml'
    breathe_default_project = "tttrlib"
    extensions += ['breathe']




# Hack to get kwargs to appear in docstring #18434
# TODO: Remove when https://github.com/sphinx-doc/sphinx/pull/8234 gets
# merged
from sphinx.util import inspect  # noqa
from sphinx.ext.autodoc import ClassDocumenter  # noqa


class PatchedClassDocumenter(ClassDocumenter):

    def _get_signature(self):
        old_signature = inspect.signature

        def patch_signature(subject, bound_method=False, follow_wrapped=True):
            # changes the default of follow_wrapped to True
            return old_signature(subject, bound_method=bound_method,
                                 follow_wrapped=follow_wrapped)
        inspect.signature = patch_signature
        result = super()._get_signature()
        inspect.signature = old_signature
        return result


def setup(app):
    app.registry.documenters['class'] = PatchedClassDocumenter
    # to hide/show the prompt in code examples:
    app.connect('build-finished', make_carousel_thumbs)
    app.connect('build-finished', filter_search_index)


warnings.filterwarnings("ignore", category=UserWarning,
                        message='Matplotlib is currently using agg, which is a'
                                ' non-GUI backend, so cannot show the figure.')


