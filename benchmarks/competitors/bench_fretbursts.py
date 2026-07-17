#!/usr/bin/env python
"""FRETBursts burst-search benchmark (isolated fretbursts venv).

FRETBursts is the de-facto standard Python package for confocal single-molecule
burst analysis. We build its ``Data`` object directly from the IDENTICAL photon
stream tttrlib read (results/shared/burst_stream.npz) and time its burst search.
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from common import timeit, record, RESULTS   # noqa: E402

from fretbursts import Data, bg, Ph_sel   # noqa: E402

SHARED = os.path.join(RESULTS, "shared")
ENV = "venv-fretbursts"


def bench_burst():
    z = np.load(os.path.join(SHARED, "burst_stream.npz"))
    macro = z["macro_times"].astype(np.int64)
    ch = z["channels"].astype(np.int16)
    clk_p = float(z["macro_time_resolution"])   # seconds per macro-time tick
    n = macro.size
    a_em = (ch == 2)                            # acceptor channel in this file

    def build():
        d = Data(fname="mem", clk_p=clk_p, nch=1, ALEX=False, alternated=False,
                 meas_type="smFRET",
                 ph_times_m=[macro], A_em=[a_em])
        return d

    # background estimation is part of the standard FRETBursts pipeline
    d = build()
    d.calc_bg(bg.exp_fit, time_s=5, tail_min_us=50)
    d.burst_search(L=30, m=10, F=6, ph_sel=Ph_sel("all"), computefret=False, mute=True)
    nb = int(np.atleast_1d(d.num_bursts)[0])
    print("   [FRETBursts] %d bursts" % nb)

    # time the search alone (Data + calc_bg reused; matches tttrlib timing the search)
    def run_search():
        d.burst_search(L=30, m=10, F=6, ph_sel=Ph_sel("all"),
                       computefret=False, mute=True)
    best, mean, allt = timeit(run_search, repeat=5, warmup=1)
    record("burst_search", "FRETBursts", "sliding-window burst search",
           best, mean, allt, n_items=n, unit="photons", dataset="pq_ptu_hh_t3.ptu",
           env=ENV, extra={"n_bursts": nb, "L": 30, "m": 10, "F": 6})

    # also report the full pipeline (build Data + calc_bg + search), the cold-start cost
    def run_full():
        dd = build()
        dd.calc_bg(bg.exp_fit, time_s=5, tail_min_us=50)
        dd.burst_search(L=30, m=10, F=6, ph_sel=Ph_sel("all"),
                        computefret=False, mute=True)
    fb, fm, fall = timeit(run_full, repeat=3, warmup=0)
    record("burst_search", "FRETBursts (full pipeline)",
           "build+calc_bg+search", fb, fm, fall, n_items=n, unit="photons",
           dataset="pq_ptu_hh_t3.ptu", env=ENV, extra={"n_bursts": nb})


if __name__ == "__main__":
    print("=" * 70)
    print("FRETBursts benchmarks (%s)" % ENV)
    print("=" * 70)
    try:
        bench_burst()
    except Exception as e:
        import traceback
        print("!! failed: %s: %s" % (type(e).__name__, e))
        traceback.print_exc()
