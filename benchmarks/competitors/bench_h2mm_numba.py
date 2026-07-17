#!/usr/bin/env python3
"""H2MM competitor: the ChiSurf numba engine (numpy/numba re-implementation).

This is the algorithm the tttrlib C++ engine is a port of.  Running it here
shows the numba baseline the C++ port must match or beat on the identical
shared problem.  The engine module is self-contained (numpy + numba only) and
is imported by file path.  Run inside the ``h2mm_numba`` venv.
"""
import importlib.util
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from common import bench, RESULTS  # noqa: E402

SHARED = os.path.join(RESULTS, "shared", "h2mm")
H2MM_PY = os.path.expanduser(
    "~/dev/chisurf/chisurf/plugins/burst/burst_h2mm/core/h2mm.py"
)


def _load_engine():
    spec = importlib.util.spec_from_file_location("chisurf_h2mm", H2MM_PY)
    mod = importlib.util.module_from_spec(spec)
    sys.modules["chisurf_h2mm"] = mod  # dataclass needs the module registered
    spec.loader.exec_module(mod)
    return mod


def main():
    eng = _load_engine()
    d = np.load(os.path.join(SHARED, "data.npz"))
    off = d["offsets"]
    times = [d["times"][off[i]:off[i + 1]].astype(np.int64) for i in range(len(off) - 1)]
    streams = [d["streams"][off[i]:off[i + 1]].astype(np.int32) for i in range(len(off) - 1)]
    max_iter = int(d["max_iter"])
    tol = float(d["tol"])
    n_streams = int(d["n_streams"])

    data = eng.prepare_bursts(times, streams, n_streams)
    init = eng.H2mmModel(d["init_prior"].astype(np.float64),
                         d["init_trans"].astype(np.float64),
                         d["init_obs"].astype(np.float64))
    n_phot = data.n_photons

    # Warmup triggers numba JIT (excluded from timing by common.timeit warmup=1,
    # but the first compile is slow; do an explicit compile pass here too).
    eng.optimize(init, data, max_iter=2, tol=0.0, accelerate=False)

    def run():
        return eng.optimize(init, data, max_iter=max_iter, tol=tol, accelerate=False)

    fit = run()
    bench("h2mm", "chisurf-numba", f"EM {init.n_states}-state (plain, tol={tol:g})",
          run, repeat=5, warmup=1,
          n_items=n_phot, unit="photon", dataset="simulated",
          extra={"max_iter": max_iter, "n_iter": fit.n_iter, "impl": "numpy/numba"})


if __name__ == "__main__":
    main()
