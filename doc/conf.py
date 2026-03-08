# -*- coding: utf-8 -*-
"""
Minimal, tiered Sphinx configuration for reliable builds and iterative restore.

Usage:
  BUILD_TIER=0  → core only (safest)
  BUILD_TIER=1  → + style/UX (copybutton, design, opengraph)
  BUILD_TIER=2  → + numpydoc
  BUILD_TIER=3  → + notebooks/plots (nbsphinx, matplotlib directive, IPython)
  BUILD_TIER=4  → + citations + gallery (bibtex, sphinx-gallery)

Example:
  BUILD_TIER=0 make html
  BUILD_TIER=1 make html
  ...
"""
import warnings
warnings.filterwarnings("ignore", category=RuntimeWarning)
warnings.filterwarnings("ignore", category=UserWarning)

import os
import re
import sys
from datetime import datetime
from pathlib import Path
from importlib import import_module

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
HERE = Path(__file__).parent.resolve()
PROJECT_ROOT = HERE.parent

# Add project root and examples to path
sys.path.insert(0, str(PROJECT_ROOT))
sys.path.insert(0, str(PROJECT_ROOT / "examples"))

# Ensure static dir exists
(HERE / "_static").mkdir(exist_ok=True)

# ---------------------------------------------------------------------------
# Project info
# ---------------------------------------------------------------------------
project = "tttrlib"
author = "tttrlib developers"
copyright = f"2021 - {datetime.now().year}, tttrlib developers (BSD)"

# Version (don’t hard-fail on RTD/CI/bootstrap)
version = release = "0.0.0"
try:
    import tttrlib  # noqa
    version = getattr(tttrlib, "__version__", version)
    release = version
except Exception:
    pass

# ---------------------------------------------------------------------------
# Core Sphinx config
# ---------------------------------------------------------------------------
root_doc = "index"

# Accept both .rst and .ipynb as source files
# --- Make sure notebooks are parsed as notebooks ---
# Sphinx 4–7: either form is fine; this one is explicit and robust
source_suffix = {
    ".rst": "restructuredtext"
}
print("[conf] source_suffix   :", source_suffix)

templates_path = ["_templates"] if (HERE / "_templates").exists() else []

# Exclusions (keep minimal at tier 0)
exclude_patterns = [
    "_build",
    "Thumbs.db",
    ".DS_Store",
]

# Default role makes `identifier` try to resolve refs but stays literal if unknown
default_role = "any"

# Syntax highlighting
pygments_style = "sphinx"

# Autodoc defaults (safe)
autodoc_default_options = {
    "members": True,
    "inherited-members": True,
    "show-inheritance": False,
}

# ---------------------------------------------------------------------------
# Tiered extensions
# ---------------------------------------------------------------------------
def _try_import(modname: str) -> bool:
    try:
        import_module(modname)
        return True
    except Exception:
        print(f"[conf] Skipping '{modname}' (not importable in this env).")
        return False

BUILD_TIER = int(os.environ.get("BUILD_TIER", "0"))

CORE_EXTS = [
    "sphinx.ext.autodoc",
    "sphinx.ext.autosummary",
    "sphinx.ext.intersphinx",
    "sphinx.ext.mathjax",
    "sphinx.ext.viewcode",
    # keep doctest off by default; re-enable later if you want:
    # "sphinx.ext.doctest",
]

STYLE_EXTS = [
    "sphinx_copybutton",
    "sphinx_design",
    "sphinxext.opengraph",
]

NUMPYDOC_EXTS = [
    "numpydoc",
]

NB_EXTS = [
    "nbsphinx",
    "matplotlib.sphinxext.plot_directive",
    "IPython.sphinxext.ipython_console_highlighting",
]

BIB_EXTS = [
    "sphinxcontrib.bibtex",
]

GALLERY_EXTS = [
    "sphinx_gallery.gen_gallery",
]

extensions = []
# Tier 0: core
for e in CORE_EXTS:
    if _try_import(e):
        extensions.append(e)

# Tier 1: style/UX
if BUILD_TIER >= 1:
    for e in STYLE_EXTS:
        if _try_import(e):
            extensions.append(e)

# Tier 2: numpydoc
if BUILD_TIER >= 2:
    for e in NUMPYDOC_EXTS:
        if _try_import(e):
            extensions.append(e)
    # Numpydoc tweaks (safe defaults)
    numpydoc_class_members_toctree = False
    numpydoc_show_class_members = False

# Tier 3: notebooks/plots
if BUILD_TIER >= 3:
    for e in NB_EXTS:
        if _try_import(e):
            extensions.append(e)
    # Conservative notebook settings
    nbsphinx_execute = "never"
    nbsphinx_allow_errors = True
    # Avoid duplicate example filenames if you later add a gallery:
    exclude_patterns += [
        "auto_examples/*.ipynb",
        "auto_examples/**/*.ipynb",
    ]
print("[conf] exclude_patterns:", exclude_patterns)
print("[conf] extensions", extensions)

if BUILD_TIER >= 4:
    # Headless + no-op show (helps avoid hanger)
    os.environ.setdefault("MPLBACKEND", "Agg")
    try:
        import matplotlib.pyplot as plt
        plt.show = lambda *a, **k: None
    except Exception:
        pass

    for e in BIB_EXTS + GALLERY_EXTS:
        if _try_import(e):
            extensions.append(e)


    # --- BibTeX: enable only if a .bib is available, else disable cleanly ---
    if "sphinxcontrib.bibtex" in extensions:
        bib_env = os.environ.get("BIBTEX_BIBFILES")
        if bib_env:
            bibtex_bibfiles = [x.strip() for x in bib_env.split(",") if x.strip()]
        else:
            default_bib = HERE / "references.bib"
            if default_bib.exists():
                bibtex_bibfiles = [default_bib.name]  # relative path
            else:
                print("[conf] No references.bib found — disabling sphinxcontrib.bibtex.")
                extensions.remove("sphinxcontrib.bibtex")

    if "sphinx_gallery.gen_gallery" in extensions:
        import re

        blacklist_file = HERE / "gallery_blacklist.txt"
        blacklisted = []
        if blacklist_file.exists():
            for line in blacklist_file.read_text(encoding="utf-8").splitlines():
                line = line.strip()
                if line and not line.startswith("#"):
                    blacklisted.append(line)

        # Build ignore_pattern from helpers + basenames in blacklist
        ignore_pat = r"^_"
        if blacklisted:
            escaped = [re.escape(n) for n in blacklisted]  # basenames only
            # match basename at end of path on Win/Linux: (^|[\\/])NAME$
            by_base = r"(?:^|[\\/])(?:" + "|".join(escaped) + r")$"
            ignore_pat = rf"{ignore_pat}|{by_base}"

        print("[conf] gallery blacklist file:", blacklist_file)
        print("[conf] gallery blacklisted basenames:", blacklisted)
        print("[conf] gallery ignore_pattern:", ignore_pat)

        sphinx_gallery_conf = {
            "doc_module": "tttrlib",
            "examples_dirs": [str(PROJECT_ROOT / "examples")] if (PROJECT_ROOT / "examples").exists() else [],
            "gallery_dirs": ["auto_examples"],
            "filename_pattern": r"plot_.*\.py$",
            "ignore_pattern": ignore_pat,     # <-- blacklist in effect
            "plot_gallery": True,
            "image_scrapers": ("matplotlib",),
            "reset_modules": ("matplotlib",),
            "remove_config_comments": True,
            "run_stale_examples": False,
            "abort_on_example_error": False,

        }
        exclude_patterns += ["auto_examples/**/*.py", "auto_examples/*.ipynb"]

# ---------------------------------------------------------------------------
# MathJax v3 (CDN)
# ---------------------------------------------------------------------------
mathjax_path = "https://cdn.jsdelivr.net/npm/mathjax@3/es5/tex-chtml.js"

# ---------------------------------------------------------------------------
# Theme (robust fallback)
# ---------------------------------------------------------------------------
def _first_available_theme(candidates):
    for modname, theme_name in candidates:
        if _try_import(modname):
            return theme_name
    return "alabaster"  # always present

html_theme = _first_available_theme([
    ("pydata_sphinx_theme", "pydata_sphinx_theme"),
    ("furo", "furo"),
    ("sphinx_rtd_theme", "sphinx_rtd_theme"),
    # fall back to alabaster
])
html_title = f"{project} v{version}"
html_short_title = "tttrlib"

# Logo/favicon only if present (avoids FileNotFound warnings)
if (HERE / "logos" / "tttrlib-logo-small.png").exists():
    html_logo = "logos/tttrlib-logo-small.png"
if (HERE / "logos" / "favicon.ico").exists():
    html_favicon = "logos/favicon.ico"

html_static_path = ["_static"] if (HERE / "_static").exists() else []
html_css_files = ["custom.css"] if (HERE / "_static" / "custom.css").exists() else []
html_show_sourcelink = True
html_copy_source = True
html_domain_indices = False
html_use_index = False

# Theme options kept minimal to reduce breakage
html_theme_options = {
    "navigation_depth": 3,
    "show_toc_level": 2,
}

# Optional sidebars only if the theme ships them
html_sidebars = {
    "**": ["search-field.html", "sidebar-nav-bs.html", "sourcelink.html"]
} if html_theme == "pydata_sphinx_theme" else {}

# ---------------------------------------------------------------------------
# intersphinx (lean)
# ---------------------------------------------------------------------------
intersphinx_mapping = {
    "python": (f"https://docs.python.org/{sys.version_info.major}", None),
    "numpy": ("https://numpy.org/doc/stable", None),
}

# ---------------------------------------------------------------------------
# Final: print an at-a-glance summary for debugging
# ---------------------------------------------------------------------------
print("[conf] BUILD_TIER =", BUILD_TIER)
print("[conf] html_theme  =", html_theme)
print("[conf] extensions  =", extensions)
print("[conf] exclude_patterns =", exclude_patterns)
