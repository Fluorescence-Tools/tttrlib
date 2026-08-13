"""A/B: DecayFit23's exact (AD) gradient vs central differences.

PRD-010 (okf/prds/PRD-010-neural-net-and-surrogate-models.md) converted
DecayFit23's general (tau/gamma) BFGS branch to an exact forward-mode gradient
(`decay23_gradient`, DecayFit23.cpp) in place of i_lbfgs's central-difference
default. This measures the whole-fit cost through the same path a caller
actually uses -- `tttrlib.Fit23`, the deprecated-but-still-exercised shim
`test_fit2x_compat.py` pins -- not just the gradient in isolation, and checks
that the fitted parameters are unchanged, not only that the fit is faster.

The A/B needs two builds, so this script only *measures* one build at a time.
Run it once with the analytic gradient wired in (`bfgs_o.set_gradient(...)`
present in DecayFit23.cpp) and once with that call commented out, `pip install
-e . --no-build-isolation` between the two, and diff the printed numbers --
see PRD-010's "Phase 6" section for the recorded result.

    time python -m pip install -e . --no-build-isolation -q
    python benchmarks/bench_decayfit23_ad.py
"""
from __future__ import division

import time
import warnings

import numpy as np

import tttrlib

FN = 32
DT = 0.5
DATA = np.array([
    0, 0, 0, 1, 9, 7, 5, 5, 5, 2, 2, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 2, 2, 2, 2, 3, 0, 1, 0,
    1, 1, 1, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
], dtype=float)


def _irf():
    irf = np.zeros(2 * FN)
    for half in (0, FN):
        for i in range(FN):
            irf[half + i] = np.exp(-((i - 8.0) ** 2) / (2 * 0.5 * 0.5))
    return irf


def cpu_ms(fn, reps):
    """Minimum over `reps` process-CPU-time calls -- see bench_gradvec.cpp's
    docstring for why wall clock is the wrong clock on a shared machine."""
    best = float("inf")
    for _ in range(reps):
        t0 = time.process_time()
        fn()
        best = min(best, time.process_time() - t0)
    return best * 1e3


def main():
    warnings.simplefilter("ignore", DeprecationWarning)
    kwargs = dict(dt=DT, irf=_irf(), period=2.0 * FN, g_factor=1.0,
                  l1=0.1, l2=0.1, convolution_stop=FN // 2 - 1)
    fit = tttrlib.Fit23(background=np.zeros(2 * FN), **kwargs)

    # fixed=[0,0,1,1]: tau AND gamma free -- the general BFGS branch
    # decay23_gradient is registered on (r0/rho stay fixed either way, see
    # DecayFit23::fit). This is the exact case test_fit2x_compat.py pins.
    def run_once():
        return fit(DATA, initial_values=[1.0, 0.01, 0.38, 1.2], fixed=[0, 0, 1, 1])

    r = run_once()
    print("tau         = %.6f" % r["x"][0])
    print("gamma (x1)  = %.6f" % r["x"][1])
    print("twoIstar    = %.6f" % r["twoIstar"])
    print("r_scatter   = %.6f" % r["x"][6])
    print("r_exp (x7)  = %.6f" % r["x"][7])

    reps = 3000
    t_ms = cpu_ms(run_once, reps)
    print("\nmin CPU time per fit() call, best of %d: %.4f ms" % (reps, t_ms))

    # tau-only fast path (Brent, no gradient at all) is untouched by this
    # change -- included as a sanity check that it still agrees and that the
    # comparison above isn't accidentally exercising it instead.
    def run_tau_only():
        return fit(DATA, initial_values=[1.0, 0.0, 0.0, 1.2], fixed=[0, 1, 1, 1])

    r2 = run_tau_only()
    t2_ms = cpu_ms(run_tau_only, reps)
    print("\n[tau-only Brent path, unaffected by this change -- reference]")
    print("tau = %.6f, twoIstar = %.6f, %.4f ms/call" %
          (r2["x"][0], r2["twoIstar"], t2_ms))


if __name__ == "__main__":
    main()
