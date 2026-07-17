#!/usr/bin/env python
"""pycorrelate FCS benchmark (isolated pycorrelate venv).

pycorrelate (Ingargiola, OpenSMFS) is the reference pure-NumPy photon-timestamp
correlator. It cross-correlates the IDENTICAL two channel streams tttrlib does
(results/shared/correlation.npz) over the same log-spaced lag bins.
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from common import bench, RESULTS   # noqa: E402

import pycorrelate as pyc   # noqa: E402

SHARED = os.path.join(RESULTS, "shared")
ENV = "venv-pycorrelate"


def bench_corr():
    z = np.load(os.path.join(SHARED, "correlation.npz"))
    t0 = np.ascontiguousarray(z["t0"], dtype=np.int64)
    t2 = np.ascontiguousarray(z["t2"], dtype=np.int64)
    bins = np.ascontiguousarray(z["bins"], dtype=np.int64)
    n = int(z["n_photons"])
    print("   [pycorrelate] ch0=%d ch2=%d photons, %d lag bins" % (t0.size, t2.size, bins.size - 1))

    def run():
        return pyc.pcorrelate(t0, t2, bins, normalize=True)
    bench("correlation", "pycorrelate", "cross-correlation (FCS, direct)", run,
          repeat=5, n_items=n, unit="photons", dataset="pq_ptu_hh_t3.ptu", env=ENV)


if __name__ == "__main__":
    print("=" * 70)
    print("pycorrelate benchmarks (%s)" % ENV)
    print("=" * 70)
    try:
        bench_corr()
    except Exception as e:
        import traceback
        print("!! failed: %s: %s" % (type(e).__name__, e))
        traceback.print_exc()
