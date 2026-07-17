#!/usr/bin/env python3
"""H2MM competitor: the reference C library ``H2MM_C`` (Harris/Pirchi).

Loads the shared simulated bursts written by ``bench_h2mm.py`` and runs the
reference pthreads C engine for the identical fixed EM-map budget from the
identical initial model, so the wall-clock is directly comparable to the
tttrlib C++ engine.  Run inside the ``h2mm_c`` venv.
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from common import bench, RESULTS  # noqa: E402

import H2MM_C as h2  # noqa: E402

SHARED = os.path.join(RESULTS, "shared", "h2mm")


def load():
    d = np.load(os.path.join(SHARED, "data.npz"))
    off = d["offsets"]
    times = [d["times"][off[i]:off[i + 1]].astype(np.int64) for i in range(len(off) - 1)]
    idx = [d["streams"][off[i]:off[i + 1]].astype(np.uint8) for i in range(len(off) - 1)]
    return times, idx, d


def main():
    times, idx, d = load()
    max_iter = int(d["max_iter"])
    tol = float(d["tol"])
    prior = d["init_prior"].astype(np.float64)
    trans = d["init_trans"].astype(np.float64)
    obs = d["init_obs"].astype(np.float64)          # (n_states, n_streams)
    n_phot = int(sum(len(t) for t in times))

    model = h2.h2mm_model(prior, trans, obs)

    # Plain EM to convergence (same tol as tttrlib/numba); num_cores=all is
    # H2MM_C's default (pthreads per burst).
    def run():
        m = h2.h2mm_model(prior.copy(), trans.copy(), obs.copy())
        h2.EM_H2MM_C(m, idx, times, max_iter=max_iter,
                     converged_min=tol, max_time=np.inf, print_func=None)
        return m

    m = run()
    bench("h2mm", "H2MM_C", f"EM {model.nstate}-state (plain, tol={tol:g})",
          run, repeat=5, warmup=1,
          n_items=n_phot, unit="photon", dataset="simulated",
          extra={"max_iter": max_iter, "n_iter": int(m.niter),
                 "impl": "pthreads-C reference"})


if __name__ == "__main__":
    main()
