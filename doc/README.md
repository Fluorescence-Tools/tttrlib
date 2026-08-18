# Documentation for tttrlib

This directory contains the full manual and web site as displayed at
http://tttrlib.peulen.xyz.

Largely the documentation of scikit-learn was used as a template.

## Layout

| path | what it is |
|---|---|
| `*.rst` | the manual: `getting-started`, `quickstart`, the user guide (`tttr-core`, `file-formats`, `burst-analysis`, `fcs-correlation`, `clsm-flim-guide`, `fit-guide`, `pipelines`, `plugins`, ...), `languages`, `r-package`, `imagej-plugin`, `javascript-package`, `conformance`, `faq`, `support` |
| `formats/` | one page per file format tttrlib reads or writes, including `pto.rst` (the container) and `pto-mfdb.rst` (the profile) |
| `modules/` | per-module API pages |
| `includes/`, `sphinxext/`, `static_root/`, `logos/`, `img/` | templates, extensions, CSS and figures the build uses |
| `whats_new/` | the release notes fragments |
| `auto_examples/` | **generated** by sphinx-gallery from `examples/`; not edited by hand |
| `xml/`, `_build/`, `_doxygen_build/` | **generated** (Doxygen XML, the built site); not in version control |

Build it with `make -C doc html` after `pip install -e .`; the gallery runs
every example, so a broken example fails the docs build.
