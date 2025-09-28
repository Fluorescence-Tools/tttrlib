"""
=====================
Micro time histograms
=====================

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
p.semilogy(t, h, label="micro_time_coarsening=16")

h, t = data.get_microtime_histogram(
    micro_time_coarsening=4
)
p.semilogy(t, h, label="micro_time_coarsening=8")
p.legend()
p.show()
