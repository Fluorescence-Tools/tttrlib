.. _examples:

Applications
============

This directory contains runnable example scripts for `tttrlib`. The Python
files named `plot_*.py` are automatically converted into a gallery by
Sphinx-Gallery when building the documentation.

How to use
----------
- Run locally: execute any `plot_*.py` with Python. Some examples use sample
  data under `test/data/`.
- In docs: after building the documentation, browse the rendered gallery at
  `auto_examples/index`.

Data paths
----------
Many examples expect a dataset tree. You can set the environment variable
`TTTRLIB_DATA` to point to the dataset root. If not set, examples fall back to
`../../tttr-data` relative to the example file.

Notes
-----
- Subfolders called `broken/` are ignored by Sphinx-Gallery (work-in-progress
  or outdated examples).
- Each subdirectory contains a brief `README.rst` that describes its examples.

