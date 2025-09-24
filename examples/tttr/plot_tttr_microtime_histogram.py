"""
=====================
Micro time histograms
=====================

Overview
--------
This example computes and plots microtime histograms from a TTTR file for
different coarsening values. Microtime histograms are useful to inspect
instrument response, , and to visually choose microtime gates
for background suppression or PIE-gating.

Prerequisites
-------------
- Set the environment variable ``TTTRLIB_DATA`` to point to a directory that
  mirrors the expected structure (e.g. ``pq/ptu/...``, ``bh/...``). If not set,
  the script falls back to the repository layout: ``../../tttr-data``.

Outputs
-------
- A semilog plot of microtime histograms for several ``micro_time_coarsening``
  values so you can compare how binning affects the distribution.

See also
--------
- ``examples/tttr/plot_tttr_files.py`` for an overview of TTTR data access.
- ``examples/correlation/plot_gated_correlation.py`` for using microtime gating
  in correlation analysis.

"""
import os
from pathlib import Path
import tttrlib
import pylab as p

# Use TTTRLIB_DATA if set, otherwise fall back to repository layout
DATA_ROOT = Path(os.environ.get("TTTRLIB_DATA", "../../tttr-data")).resolve()

data = tttrlib.TTTR(str(DATA_ROOT / 'bh/bh_spc132.spc'), 'SPC-130')
h, t = data.get_microtime_histogram(
    micro_time_coarsening=32
)
p.semilogy(t, h, label="micro_time_coarsening=32")

h, t = data.get_microtime_histogram(
    micro_time_coarsening=8
)
p.semilogy(t, h, label="micro_time_coarsening=8")

h, t = data.get_microtime_histogram(
    micro_time_coarsening=4
)
p.semilogy(t, h, label="micro_time_coarsening=4")
p.legend()
p.show()
